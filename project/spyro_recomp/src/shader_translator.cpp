#include "shader_translator.h"

#define NOMINMAX
#include <windows.h>
#include <unknwn.h>  // dxcapi.h needs the COM declarations (IUnknown, IStream), which the
#include <objidl.h>  // build's WIN32_LEAN_AND_MEAN leaves out of windows.h
#include <dxcapi.h>

#define SPYRO_SHADER_IN_GAME
#include "pch.h"  // project\shader_recomp: the translator's helpers (big-endian fields, fmt)
#include "shader_recompiler.h"
#include "spyro_shader_common.inc"  // kSpyroShaderCommon: shader_common.h, embedded by CMake

#include <rex/logging.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

namespace spyro::shader_translator {
namespace {

constexpr char kDir[] = "shader_cache";  // in the working directory (bin\ in a release)

struct Job {
  uint64_t hash;
  std::vector<uint8_t> container;
};

std::mutex g_mutex;
std::condition_variable g_wake;
std::deque<Job> g_queue;
std::unordered_map<uint64_t, State> g_state;
std::unordered_map<uint64_t, std::vector<uint8_t>> g_dxil;
uint32_t g_busy = 0;

std::string FileName(uint64_t hash) {
  char name[64];
  std::snprintf(name, sizeof(name), "%s/%016llX.dxil", kDir, static_cast<unsigned long long>(hash));
  return name;
}

// dxcompiler.dll (+ dxil.dll, which signs the DXIL) from the game's folder, loaded once.
DxcCreateInstanceProc DxcFactory() {
  static DxcCreateInstanceProc s_create = [] {
    HMODULE dll = LoadLibraryW(L"dxcompiler.dll");
    auto create = dll ? reinterpret_cast<DxcCreateInstanceProc>(GetProcAddress(dll, "DxcCreateInstance")) : nullptr;
    if (!create)
      REXLOG_ERROR("[plume] dxcompiler.dll not found next to the game - shaders missing from the cache stay invisible");
    return create;
  }();
  return s_create;
}

// HLSL -> DXIL with the arguments SpyroShaderRecomp uses for the cache (dxc_compiler.cpp).
bool Compile(const std::string& hlsl, bool pixel, std::vector<uint8_t>* dxil, std::string* errors) {
  thread_local IDxcCompiler3* compiler = nullptr;
  if (!compiler) {
    DxcCreateInstanceProc create = DxcFactory();
    if (!create || FAILED(create(CLSID_DxcCompiler, IID_PPV_ARGS(&compiler)))) {
      *errors = "no DXC compiler";
      return false;
    }
  }
  DxcBuffer source{hlsl.data(), hlsl.size(), 0};
  const wchar_t* args[] = {pixel ? L"-T ps_6_0" : L"-T vs_6_0", L"-HV 2021", L"-all-resources-bound",
                           L"-Wno-ignored-attributes", L"-Qstrip_reflect", L"-Qstrip_debug"};
  IDxcResult* result = nullptr;
  if (FAILED(compiler->Compile(&source, args, UINT32(std::size(args)), nullptr, IID_PPV_ARGS(&result))) || !result) {
    *errors = "Compile call failed";
    return false;
  }
  HRESULT status = E_FAIL;
  result->GetStatus(&status);
  bool ok = false;
  if (SUCCEEDED(status)) {
    IDxcBlob* object = nullptr;
    if (SUCCEEDED(result->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&object), nullptr)) && object) {
      auto* p = static_cast<const uint8_t*>(object->GetBufferPointer());
      dxil->assign(p, p + object->GetBufferSize());
      object->Release();
      ok = !dxil->empty();
    }
  } else {
    IDxcBlobUtf8* text = nullptr;
    if (SUCCEEDED(result->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&text), nullptr)) && text) {
      errors->assign(text->GetStringPointer(), std::min<size_t>(text->GetStringLength(), 600));
      text->Release();
    }
  }
  result->Release();
  return ok;
}

void Translate(const Job& job) {
  auto start = std::chrono::steady_clock::now();
  thread_local ShaderRecompiler recompiler;
  recompiler = {};
  recompiler.recompile(job.container.data(), kSpyroShaderCommon);
  std::vector<uint8_t> dxil;
  std::string errors;
  bool ok = Compile(recompiler.out, recompiler.isPixelShader, &dxil, &errors);
  double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
  if (ok) {
    // Saved for the next start (written under another name first, so a crash never leaves half a file).
    std::error_code ec;
    std::filesystem::create_directories(kDir, ec);
    std::string name = FileName(job.hash), tmp = name + ".tmp";
    if (std::ofstream f{tmp, std::ios::binary}) f.write(reinterpret_cast<const char*>(dxil.data()), std::streamsize(dxil.size()));
    std::filesystem::rename(tmp, name, ec);
    REXLOG_INFO("[plume] shader {:016X} ({}) translated at runtime in {:.0f} ms", job.hash,
                recompiler.isPixelShader ? "pixel" : "vertex", ms);
  } else {
    REXLOG_ERROR("[plume] shader {:016X} could not be translated: {}", job.hash, errors);
  }
  std::lock_guard lock(g_mutex);
  if (ok) g_dxil[job.hash] = std::move(dxil);
  g_state[job.hash] = ok ? State::kReady : State::kFailed;
}

void Worker() {
  for (;;) {
    Job job;
    {
      std::unique_lock lock(g_mutex);
      g_wake.wait(lock, [] { return !g_queue.empty(); });
      job = std::move(g_queue.front());
      g_queue.pop_front();
      ++g_busy;
    }
    Translate(job);
    std::lock_guard lock(g_mutex);
    --g_busy;
  }
}

void StartWorkers() {
  static std::once_flag s_once;
  std::call_once(s_once, [] {
    unsigned n = std::clamp(std::thread::hardware_concurrency() / 2, 1u, 4u);
    for (unsigned i = 0; i < n; ++i) std::thread(Worker).detach();
  });
}

}  // namespace

void Request(uint64_t hash, const uint8_t* container, uint32_t size) {
  {
    std::lock_guard lock(g_mutex);
    if (g_state.count(hash)) return;
    g_state[hash] = State::kPending;
  }
  // Translated in an earlier session?
  if (std::ifstream f{FileName(hash), std::ios::binary}) {
    std::vector<uint8_t> dxil((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (!dxil.empty()) {
      std::lock_guard lock(g_mutex);
      g_dxil[hash] = std::move(dxil);
      g_state[hash] = State::kReady;
      return;
    }
  }
  StartWorkers();
  std::lock_guard lock(g_mutex);
  g_queue.push_back({hash, std::vector<uint8_t>(container, container + size)});
  g_wake.notify_one();
}

State Get(uint64_t hash, std::vector<uint8_t>* dxil) {
  std::lock_guard lock(g_mutex);
  auto it = g_state.find(hash);
  if (it == g_state.end()) return State::kFailed;
  if (it->second == State::kReady) {
    auto d = g_dxil.find(hash);
    if (d != g_dxil.end()) *dxil = d->second;
  }
  return it->second;
}

uint32_t PendingCount() {
  std::lock_guard lock(g_mutex);
  return uint32_t(g_queue.size()) + g_busy;
}

}  // namespace spyro::shader_translator

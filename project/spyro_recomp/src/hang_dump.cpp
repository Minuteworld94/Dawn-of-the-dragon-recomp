#include "hang_dump.h"

#include <rex/logging.h>
#include <rex/ppc.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "plume_renderer.h"

#if defined(_WIN32)
#include <windows.h>
#include <tlhelp32.h>
#else
#include <dlfcn.h>
#include <execinfo.h>
#include <signal.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <filesystem>
#endif

namespace spyro::hang_dump {
namespace {

std::atomic<uint64_t> g_progress{0};
std::atomic<bool> g_started{false};

// Host function start -> guest address, sorted by host address.
std::vector<std::pair<uintptr_t, uint32_t>> g_funcs;
std::vector<std::pair<uintptr_t, uint32_t>> g_extra;

void BuildTable() {
  g_funcs = g_extra;
  for (const PPCFuncMapping* m = PPCFuncMappings; m->host; ++m)
    g_funcs.emplace_back(reinterpret_cast<uintptr_t>(m->host), static_cast<uint32_t>(m->guest));
  std::sort(g_funcs.begin(), g_funcs.end());
}

// "sub_XXXXXXXX+0x.." for an address inside the game's recompiled code (false = not found).
bool GuestName(uintptr_t pc, char* buf, size_t size) {
  auto it = std::upper_bound(g_funcs.begin(), g_funcs.end(), std::make_pair(pc, UINT32_MAX));
  if (it == g_funcs.begin()) return false;
  --it;
  std::snprintf(buf, size, "sub_%08X+0x%llX", it->second, static_cast<unsigned long long>(pc - it->first));
  return true;
}

#if defined(_WIN32)
std::string Describe(uintptr_t pc) {
  char buf[96];
  HMODULE exe = GetModuleHandleW(nullptr), mod = nullptr;
  GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                     reinterpret_cast<LPCWSTR>(pc), &mod);
  if (mod == exe && GuestName(pc, buf, sizeof(buf))) return buf;
  wchar_t name[MAX_PATH] = L"?";
  if (mod) GetModuleFileNameW(mod, name, MAX_PATH);
  std::wstring w(name);
  std::string n(w.begin() + (w.find_last_of(L"\\/") + 1), w.end());
  std::snprintf(buf, sizeof(buf), "%s+0x%llX", n.c_str(),
                static_cast<unsigned long long>(pc - reinterpret_cast<uintptr_t>(mod)));
  return buf;
}

void DumpThread(DWORD tid) {
  HANDLE th = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, tid);
  if (!th) return;
  if (SuspendThread(th) == static_cast<DWORD>(-1)) {
    CloseHandle(th);
    return;
  }
  CONTEXT c{};
  c.ContextFlags = CONTEXT_FULL;
  std::string line;
  if (GetThreadContext(th, &c)) {
    for (int frame = 0; frame < 20 && c.Rip; ++frame) {
      line += " " + Describe(c.Rip);
      DWORD64 image_base = 0;
      PRUNTIME_FUNCTION fn = RtlLookupFunctionEntry(c.Rip, &image_base, nullptr);
      if (!fn) {  // leaf function: return address is at [rsp]
        c.Rip = *reinterpret_cast<DWORD64*>(c.Rsp);
        c.Rsp += 8;
        continue;
      }
      PVOID handler_data = nullptr;
      DWORD64 establisher = 0;
      RtlVirtualUnwind(UNW_FLAG_NHANDLER, image_base, c.Rip, fn, &c, &handler_data, &establisher, nullptr);
    }
  }
  ResumeThread(th);
  CloseHandle(th);
  if (line.find("sub_") != std::string::npos)  // only threads running game code
    REXLOG_INFO("[hang] thread {}:{}", tid, line);
}

void DumpAll() {
  DWORD self = GetCurrentThreadId(), pid = GetCurrentProcessId();
  HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
  if (snap == INVALID_HANDLE_VALUE) return;
  THREADENTRY32 te{};
  te.dwSize = sizeof(te);
  for (BOOL ok = Thread32First(snap, &te); ok; ok = Thread32Next(snap, &te))
    if (te.th32OwnerProcessID == pid && te.th32ThreadID != self) DumpThread(te.th32ThreadID);
  CloseHandle(snap);
}

bool Install() { return true; }
#else
// Linux (WORK_LOG 163): each thread is asked with SIGUSR2 (free on Linux - reXglue's thread
// signals are the realtime ones) to record its own stack (backtrace in the handler); the watchdog
// then names the frames. One thread at a time; the handler only writes when it is the one asked.
uintptr_t g_exe_base = 0;
void* g_frames[24];
std::atomic<int> g_frame_count{0};
std::atomic<long> g_want_tid{0};
std::atomic<bool> g_sampled{false};

void OnSample(int) {
  if (syscall(SYS_gettid) != g_want_tid.load(std::memory_order_acquire)) return;
  g_frame_count.store(backtrace(g_frames, 24), std::memory_order_relaxed);
  g_sampled.store(true, std::memory_order_release);
}

std::string Describe(uintptr_t pc) {
  char buf[128];
  Dl_info info{};
  if (dladdr(reinterpret_cast<void*>(pc), &info) && info.dli_fbase) {
    uintptr_t mod = reinterpret_cast<uintptr_t>(info.dli_fbase);
    if (mod == g_exe_base && GuestName(pc, buf, sizeof(buf))) return buf;
    const char* file = info.dli_fname ? info.dli_fname : "?";
    const char* slash = std::strrchr(file, '/');
    std::snprintf(buf, sizeof(buf), "%s+0x%llX", slash ? slash + 1 : file, static_cast<unsigned long long>(pc - mod));
    return buf;
  }
  std::snprintf(buf, sizeof(buf), "0x%llX", static_cast<unsigned long long>(pc));
  return buf;
}

void DumpAll() {
  const long pid = getpid(), self = syscall(SYS_gettid);
  std::error_code ec;
  for (const auto& e : std::filesystem::directory_iterator("/proc/self/task", ec)) {
    long tid = std::strtol(e.path().filename().c_str(), nullptr, 10);
    if (tid <= 0 || tid == self) continue;
    g_sampled.store(false);
    g_want_tid.store(tid, std::memory_order_release);
    if (syscall(SYS_tgkill, pid, tid, SIGUSR2) != 0) continue;
    for (int i = 0; i < 200 && !g_sampled.load(std::memory_order_acquire); ++i)
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    g_want_tid.store(0, std::memory_order_release);
    if (!g_sampled.load(std::memory_order_acquire)) continue;
    int n = g_frame_count.load(std::memory_order_relaxed);
    std::string line;
    for (int f = 1; f < n; ++f) line += " " + Describe(reinterpret_cast<uintptr_t>(g_frames[f]));  // 0 = OnSample
    if (line.find("sub_") != std::string::npos) REXLOG_INFO("[hang] thread {}:{}", tid, line);
  }
}

bool Install() {
  Dl_info info{};
  if (dladdr(reinterpret_cast<void*>(&BuildTable), &info)) g_exe_base = reinterpret_cast<uintptr_t>(info.dli_fbase);
  void* warm[2];
  backtrace(warm, 2);  // loads the unwinder now, not inside the signal handler
  struct sigaction sa{};
  sa.sa_handler = OnSample;
  sa.sa_flags = SA_RESTART;
  sigemptyset(&sa.sa_mask);
  return sigaction(SIGUSR2, &sa, nullptr) == 0;
}
#endif

}  // namespace

void Progress() { ++g_progress; }

void AddSymbol(const void* host_function, unsigned guest_address) {
  g_extra.emplace_back(reinterpret_cast<uintptr_t>(host_function), guest_address);
}

void Start(int seconds, int max_dumps) {
  if (g_started.exchange(true)) return;
  if (!Install()) return;
  std::thread([seconds, max_dumps] {
    BuildTable();
    uint64_t last = g_progress.load();
    auto last_change = std::chrono::steady_clock::now();
    int dumps = 0;
    while (dumps < max_dumps) {
      std::this_thread::sleep_for(std::chrono::milliseconds(500));
      uint64_t now_progress = g_progress.load();
      auto now = std::chrono::steady_clock::now();
      if (now_progress != last) {
        last = now_progress;
        last_change = now;
        continue;
      }
      if (now - last_change < std::chrono::seconds(seconds)) continue;
      REXLOG_INFO("[hang] no progress for {} s (count {}): thread stacks follow", seconds, last);
      REXLOG_INFO("[hang] renderer: {}", spyro::plume_renderer::HangState());
      DumpAll();
      rex::FlushLogging();  // on disk even if the player then closes the frozen game
      ++dumps;
      last_change = now;
    }
  }).detach();
}

}  // namespace spyro::hang_dump

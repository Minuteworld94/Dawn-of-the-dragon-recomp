#include "hang_dump.h"

#include <rex/logging.h>
#include <rex/ppc.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#include <tlhelp32.h>
#endif

namespace spyro::hang_dump {
namespace {

std::atomic<uint64_t> g_progress{0};
std::atomic<bool> g_started{false};

#if defined(_WIN32)
// Host function start -> guest address, sorted by host address.
std::vector<std::pair<uintptr_t, uint32_t>> g_funcs;
std::vector<std::pair<uintptr_t, uint32_t>> g_extra;

void BuildTable() {
  g_funcs = g_extra;
  for (const PPCFuncMapping* m = PPCFuncMappings; m->host; ++m)
    g_funcs.emplace_back(reinterpret_cast<uintptr_t>(m->host), static_cast<uint32_t>(m->guest));
  std::sort(g_funcs.begin(), g_funcs.end());
}

std::string Describe(uintptr_t pc) {
  char buf[96];
  auto it = std::upper_bound(g_funcs.begin(), g_funcs.end(), std::make_pair(pc, UINT32_MAX));
  HMODULE exe = GetModuleHandleW(nullptr), mod = nullptr;
  GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                     reinterpret_cast<LPCWSTR>(pc), &mod);
  if (mod == exe && it != g_funcs.begin()) {
    --it;
    std::snprintf(buf, sizeof(buf), "sub_%08X+0x%llX", it->second,
                  static_cast<unsigned long long>(pc - it->first));
    return buf;
  }
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
#endif

}  // namespace

void Progress() { ++g_progress; }

void AddSymbol(const void* host_function, unsigned guest_address) {
#if defined(_WIN32)
  g_extra.emplace_back(reinterpret_cast<uintptr_t>(host_function), guest_address);
#endif
}

void Start(int seconds, int max_dumps) {
#if defined(_WIN32)
  if (g_started.exchange(true)) return;
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
      DumpAll();
      ++dumps;
      last_change = now;
    }
  }).detach();
#endif
}

}  // namespace spyro::hang_dump

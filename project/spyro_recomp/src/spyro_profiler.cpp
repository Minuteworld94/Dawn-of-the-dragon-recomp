// Sampling profiler (WORK_LOG 124), debug only: env SPYRO_PROFILE=<seconds> starts a thread that
// about every millisecond pauses each other thread of the game for a moment, notes where it is
// (its instruction pointer) and, every <seconds>, logs per busy thread the functions it was found
// in most. Recompiled code is named after the original Xbox function (sub_XXXXXXXX, from the
// guest -> host table PPCFuncMappings); other code by module (+ offset in spyrodotd.exe).
// Windows only.

#include <rex/logging.h>
#include <rex/ppc.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#include <timeapi.h>
#include <tlhelp32.h>
#pragma comment(lib, "winmm.lib")
#endif

extern PPCFuncMapping PPCFuncMappings[];

namespace {

#if defined(_WIN32)

struct HostFunc {
  uintptr_t host;
  uint32_t guest;
};

// Host start address of every recompiled function, sorted.
std::vector<HostFunc> BuildFuncTable() {
  std::vector<HostFunc> t;
  for (PPCFuncMapping* m = PPCFuncMappings; m->guest; ++m)
    if (m->host) t.push_back({reinterpret_cast<uintptr_t>(m->host), uint32_t(m->guest)});
  std::sort(t.begin(), t.end(), [](const HostFunc& a, const HostFunc& b) { return a.host < b.host; });
  return t;
}

// Host code range of the recompiled functions (they are linked together); outside it the exe's
// own code (SDK, plume, our hooks) is named by offset.
uintptr_t g_recomp_lo = 0, g_recomp_hi = 0;

bool IsRecompiled(uintptr_t rip) { return rip >= g_recomp_lo && rip < g_recomp_hi; }

std::string Describe(uintptr_t rip, const std::vector<HostFunc>& funcs, uintptr_t exe_lo, uintptr_t exe_hi) {
  char buf[160];
  if (rip >= exe_lo && rip < exe_hi) {
    auto it = std::upper_bound(funcs.begin(), funcs.end(), rip, [](uintptr_t r, const HostFunc& f) { return r < f.host; });
    if (IsRecompiled(rip) && it != funcs.begin()) {
      std::snprintf(buf, sizeof(buf), "sub_%08X", (it - 1)->guest);
      return buf;
    }
    std::snprintf(buf, sizeof(buf), "exe+0x%llX", (unsigned long long)(rip - exe_lo) & ~0xFFFull);
    return buf;
  }
  HMODULE mod = nullptr;
  if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                         reinterpret_cast<LPCSTR>(rip), &mod)) {
    char path[MAX_PATH];
    GetModuleFileNameA(mod, path, MAX_PATH);
    const char* name = std::strrchr(path, '\\');
    return name ? name + 1 : path;
  }
  return "?";
}

// "module!nearest export" for a DLL address (exports sorted per module, cached).
std::string DescribeDll(uintptr_t rip) {
  HMODULE mod = nullptr;
  if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                          reinterpret_cast<LPCSTR>(rip), &mod))
    return "?";
  static std::unordered_map<HMODULE, std::vector<std::pair<uint32_t, std::string>>> s_exports;
  auto& ex = s_exports[mod];
  auto b = reinterpret_cast<const uint8_t*>(mod);
  if (ex.empty()) {
    auto nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(b + reinterpret_cast<const IMAGE_DOS_HEADER*>(b)->e_lfanew);
    const auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    if (dir.VirtualAddress) {
      auto ed = reinterpret_cast<const IMAGE_EXPORT_DIRECTORY*>(b + dir.VirtualAddress);
      auto names = reinterpret_cast<const uint32_t*>(b + ed->AddressOfNames);
      auto ords = reinterpret_cast<const uint16_t*>(b + ed->AddressOfNameOrdinals);
      auto fns = reinterpret_cast<const uint32_t*>(b + ed->AddressOfFunctions);
      for (uint32_t i = 0; i < ed->NumberOfNames; ++i)
        ex.push_back({fns[ords[i]], reinterpret_cast<const char*>(b + names[i])});
    }
    ex.push_back({0, "?"});
    std::sort(ex.begin(), ex.end());
  }
  uint32_t rva = uint32_t(rip - reinterpret_cast<uintptr_t>(mod));
  auto it = std::upper_bound(ex.begin(), ex.end(), std::make_pair(rva, std::string("\xff")));
  char path[MAX_PATH];
  GetModuleFileNameA(mod, path, MAX_PATH);
  const char* name = std::strrchr(path, '\\');
  return std::string(name ? name + 1 : path) + "!" + (it - 1)->second;
}

std::string ThreadName(HANDLE h, DWORD id) {
  using GetDesc = HRESULT(WINAPI*)(HANDLE, PWSTR*);
  static auto get = reinterpret_cast<GetDesc>(GetProcAddress(GetModuleHandleA("kernel32.dll"), "GetThreadDescription"));
  std::string name;
  PWSTR w = nullptr;
  if (get && SUCCEEDED(get(h, &w)) && w) {
    for (PWSTR p = w; *p; ++p) name += char(*p < 128 ? *p : '?');
    LocalFree(w);
  }
  return name.empty() ? "thread " + std::to_string(id) : name;
}

void ProfilerThread(double interval) {
  std::vector<HostFunc> funcs = BuildFuncTable();
  HMODULE exe = GetModuleHandleA(nullptr);
  auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(exe);
  auto nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(reinterpret_cast<const uint8_t*>(exe) + dos->e_lfanew);
  uintptr_t exe_lo = reinterpret_cast<uintptr_t>(exe), exe_hi = exe_lo + nt->OptionalHeader.SizeOfImage;
  // The recompiled range: the run of function starts with no gap over 256 KB around the median
  // (our hooks replace some sub_ symbols and live elsewhere in the exe).
  if (!funcs.empty()) {
    size_t mid = funcs.size() / 2, lo = mid, hi = mid;
    while (lo > 0 && funcs[lo].host - funcs[lo - 1].host < (256u << 10)) --lo;
    while (hi + 1 < funcs.size() && funcs[hi + 1].host - funcs[hi].host < (256u << 10)) ++hi;
    g_recomp_lo = funcs[lo].host;
    g_recomp_hi = funcs[hi].host + (64u << 10);
  }
  REXLOG_INFO("[spyro/prof] sampling profiler on: {} recompiled functions (exe+0x{:X}..+0x{:X}), report every {:.0f} s",
              funcs.size(), g_recomp_lo - exe_lo, g_recomp_hi - exe_lo, interval);
  timeBeginPeriod(1);

  // A sample: where the thread was, then the next frames up its stack (any code).
  constexpr int kDepth = 10;
  using Stack = std::array<uintptr_t, kDepth>;
  struct StackHash {
    size_t operator()(const Stack& s) const {
      size_t h = 0;
      for (uintptr_t a : s) h = h * 1000003u ^ a;
      return h;
    }
  };
  struct ThreadInfo {
    HANDLE handle = nullptr;
    std::string name;
    std::unordered_map<uintptr_t, uint32_t> hits;  // rip -> samples
    std::unordered_map<Stack, uint32_t, StackHash> stacks;
    uint32_t samples = 0, busy = 0;
  };
  std::unordered_map<DWORD, ThreadInfo> threads;
  DWORD self = GetCurrentThreadId(), pid = GetCurrentProcessId();
  auto last_list = std::chrono::steady_clock::now() - std::chrono::hours(1);
  auto last_report = std::chrono::steady_clock::now();
  uint32_t rounds = 0;
  for (;;) {
    auto now = std::chrono::steady_clock::now();
    if (now - last_list > std::chrono::seconds(1)) {
      last_list = now;
      HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
      THREADENTRY32 te{sizeof(te)};
      for (BOOL ok = Thread32First(snap, &te); ok; ok = Thread32Next(snap, &te)) {
        if (te.th32OwnerProcessID != pid || te.th32ThreadID == self || threads.count(te.th32ThreadID)) continue;
        HANDLE h = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_LIMITED_INFORMATION, FALSE,
                              te.th32ThreadID);
        if (h) threads[te.th32ThreadID] = {h, ThreadName(h, te.th32ThreadID)};
      }
      CloseHandle(snap);
    }
    ++rounds;
    for (auto& [id, t] : threads) {
      if (SuspendThread(t.handle) == DWORD(-1)) continue;
      CONTEXT c{};
      c.ContextFlags = CONTEXT_FULL;
      bool got = GetThreadContext(t.handle, &c);
      Stack stack{};
      stack[0] = c.Rip;
      if (got) {
        // Walk the (paused) stack with the unwind tables; no allocation while it is paused.
        CONTEXT u = c;
        int n = 1;
        for (int depth = 0; depth < 64 && n < kDepth && u.Rip; ++depth) {
          DWORD64 image = 0;
          PRUNTIME_FUNCTION fe = RtlLookupFunctionEntry(u.Rip, &image, nullptr);
          if (!fe) {
            u.Rip = *reinterpret_cast<DWORD64*>(u.Rsp);
            u.Rsp += 8;
          } else {
            PVOID handler;
            DWORD64 frame;
            RtlVirtualUnwind(UNW_FLAG_NHANDLER, image, u.Rip, fe, &u, &handler, &frame, nullptr);
          }
          if (u.Rip) stack[n++] = u.Rip;
        }
      }
      ResumeThread(t.handle);
      if (!got) continue;
      ++t.samples;
      // Waiting threads sit in ntdll (NtWaitFor..., NtDelayExecution); everything else is work.
      ++t.hits[c.Rip];
      ++t.stacks[stack];
    }
    if (std::chrono::duration<double>(now - last_report).count() >= interval) {
      last_report = now;
      for (auto& [id, t] : threads) {
        if (!t.samples) continue;
        std::unordered_map<std::string, uint32_t> by_func;
        for (auto& [rip, n] : t.hits) by_func[Describe(rip, funcs, exe_lo, exe_hi)] += n;
        uint32_t idle = (by_func.count("ntdll.dll") ? by_func["ntdll.dll"] : 0) +
                        (by_func.count("win32u.dll") ? by_func["win32u.dll"] : 0);
        double busy = 100.0 * (t.samples - idle) / t.samples;
        auto pct = [&](uint32_t n) { return std::to_string(int(1000.0 * n / t.samples) / 10.0).substr(0, 4) + "%"; };
        if (busy >= 5.0) {
          std::vector<std::pair<std::string, uint32_t>> top(by_func.begin(), by_func.end());
          std::sort(top.begin(), top.end(), [](auto& a, auto& b) { return a.second > b.second; });
          std::string line;
          for (size_t i = 0; i < top.size() && i < 25; ++i) line += " " + top[i].first + " " + pct(top[i].second);
          REXLOG_INFO("[spyro/prof] {} ({}): busy {:.0f}% of {} samples |{}", t.name, id, busy, t.samples, line);
          // Call stacks as raw addresses (exe frames as RVAs, named later from spyrodotd.map by
          // project\analysis\profile_names.py); ntdll/win32u leaves merged by their callers.
          std::unordered_map<std::string, uint32_t> chains;
          for (auto& [s, n] : t.stacks) {
            std::string k;
            for (int i = 0; i < kDepth && s[i]; ++i) {
              char buf[48];
              if (s[i] >= exe_lo && s[i] < exe_hi) std::snprintf(buf, sizeof(buf), " %llX", (unsigned long long)(s[i] - exe_lo));
              else {
                k += " " + DescribeDll(s[i]);
                continue;
              }
              k += buf;
            }
            chains[k] += n;
          }
          std::vector<std::pair<std::string, uint32_t>> ctop(chains.begin(), chains.end());
          std::sort(ctop.begin(), ctop.end(), [](auto& a, auto& b) { return a.second > b.second; });
          for (size_t i = 0; i < ctop.size() && i < 60; ++i)
            REXLOG_INFO("[spyro/prof]   stack {} |{}", ctop[i].second, ctop[i].first);
        }
        t.hits.clear();
        t.stacks.clear();
        t.samples = 0;
      }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
}

const bool g_profiler_started = [] {
  const char* v = std::getenv("SPYRO_PROFILE");
  if (!v) return false;
  double interval = std::max(1.0, std::atof(v));
  std::thread(ProfilerThread, interval).detach();
  return true;
}();

#endif

}  // namespace

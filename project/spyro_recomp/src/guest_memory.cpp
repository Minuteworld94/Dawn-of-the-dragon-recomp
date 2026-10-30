// Cached guest-memory readability (guest_memory.h). Before WORK_LOG 124 only plume cached its
// VirtualQuery calls; the menu / frame hooks asked Windows directly - in Avalar that was ~47% of
// the game thread (each call ~1 ms).

#include "guest_memory.h"

#include <rex/memory/utils.h>

#include <algorithm>
#include <map>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace spyro {

bool GuestReadable(const uint8_t* base, uint32_t addr, uint32_t len) {
  if (addr < 0x1000 || len == 0 || uint64_t(addr) + len > 0x100000000ull) return false;
#if defined(_WIN32)
  // Per thread (no locking): readable regions start -> end.
  thread_local std::map<uint32_t, uint64_t> t_regions;
  thread_local uint32_t t_generation = rex::memory::UnmapGeneration();
  uint32_t gen = rex::memory::UnmapGeneration();
  if (gen != t_generation) {
    t_generation = gen;
    t_regions.clear();
  }
  if (auto it = t_regions.upper_bound(addr); it != t_regions.begin()) {
    --it;
    if (addr >= it->first && uint64_t(addr) + len <= it->second) return true;
  }
  MEMORY_BASIC_INFORMATION mbi{};
  const uint8_t* p = base + addr;
  if (!VirtualQuery(p, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT) return false;
  if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) return false;
  uint32_t start = uint32_t(static_cast<const uint8_t*>(mbi.BaseAddress) - base);
  uint64_t end = std::min<uint64_t>(uint64_t(start) + mbi.RegionSize, 0x100000000ull);
  t_regions[start] = end;
  return uint64_t(addr) + len <= end;
#else
  return true;
#endif
}

}  // namespace spyro

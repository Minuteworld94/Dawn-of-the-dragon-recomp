// Is a range of guest memory safe to read from the host? (WORK_LOG 115, 124)
#pragma once

#include <cstdint>

namespace spyro {

// True when [addr, addr+len) of guest memory is committed and readable, so reading through a
// pointer the game holds can't fault. Asking Windows (VirtualQuery) costs up to ~1 ms on the
// guest's huge mapping; the answer is cached per whole region and forgotten only when reXglue
// has made guest memory unreadable since (rex::memory::UnmapGeneration).
bool GuestReadable(const uint8_t* base, uint32_t addr, uint32_t len);

}  // namespace spyro

// Runtime shader translation for the plume renderer (WORK_LOG 121): a game shader that is not in
// spyro_shaders.bin is translated on a background thread with SpyroShaderRecomp's translator
// (compiled into the game) and Microsoft's shader compiler (dxcompiler.dll + dxil.dll next to the
// game, loaded on first use), then kept in shader_cache\<HASH>.dxil so it is translated only once.

#pragma once

#include <cstdint>
#include <vector>

namespace spyro::shader_translator {

enum class State { kPending, kReady, kFailed };

// Starts translating the shader container (the game's bytes as created, big-endian) unless it is
// already on disk, translated or queued.
void Request(uint64_t hash, const uint8_t* container, uint32_t size);

// The DXIL of a requested shader once it is ready (*dxil filled).
State Get(uint64_t hash, std::vector<uint8_t>* dxil);

// Shaders queued or being translated right now (diagnostics).
uint32_t PendingCount();

}  // namespace spyro::shader_translator

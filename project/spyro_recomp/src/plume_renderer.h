// Our own renderer on plume ("Renderer: plume" in Recomp Settings), replacing the game's Xbox
// 360 Direct3D at the function level (WORK_LOG 101/104, map: analysis\D3D_FUNCTION_MAP.txt).
//
// The game's own D3D library keeps running and keeps its device state (shaders, constants,
// textures, render states) in guest memory; our D3D hooks call Draw() with the draw
// parameters and the renderer reads that state, translates it to plume and draws straight
// into the game window. Shaders come from the XenosRecomp shader cache (spyro_shaders.bin,
// built by project\shader_recomp from the shaders the game creates).
// All calls come from the game's render thread.

#pragma once

#include <cstdint>
#include <vector>

namespace spyro::plume_renderer {

// The native window (HWND) to present into; set by the app once reXglue has opened it.
void SetWindow(void* native_window);

// The D3DCOLOR (0xAARRGGBB) the game clears its screen with (from Resolve).
void SetClearColor(uint32_t argb);

// A shader the game created (CreateVertexShader / CreatePixelShader): the guest object it got
// back, the content hash of the container (cache key), for vertex shaders the vertex inputs it
// declares (usage << 8 | usage index), and the container itself (translated at runtime if the
// cache does not have it - WORK_LOG 121).
void RegisterShader(uint32_t guest_object, uint64_t hash, bool pixel_shader, std::vector<uint16_t> vertex_inputs,
                    const uint8_t* container, uint32_t container_size);

struct DrawCall {
  uint8_t* base = nullptr;       // guest memory base
  uint32_t device = 0;           // guest D3D device
  uint32_t primitive = 0;        // Xenos primitive type (4 tri list, 6 strip, 8 rect list, ...)
  // Vertex streams: guest address of vertex 0 and stride (bytes) per stream; 0 = unused.
  // UP draws only use stream 0; buffer draws (BufferDraw) fill these from the device.
  uint32_t stream_data[16] = {};
  uint32_t stream_stride[16] = {};
  uint32_t first_vertex = 0;     // non-indexed: first vertex drawn
  uint32_t vertex_count = 0;     // non-indexed: vertices drawn; UP indexed: vertices readable
  uint32_t index_data = 0;       // guest address of the indices (0 = not indexed)
  uint32_t index_count = 0;
  int32_t base_vertex = 0;       // added to every index
  bool index_32bit = false;
};

// GPU ("physical") address -> guest address the host can read: physical memory is mapped at
// 0xA0000000 (reXglue map_info; WORK_LOG 106).
inline uint32_t PhysicalToGuest(uint32_t physical) { return 0xA0000000u + (physical & 0x1FFFFFFFu); }

// Guest virtual address of GPU data (as the D3D library stores it in its objects) -> the GPU
// address, the way the library converts it: 0xE0000000-range pages sit 4 KB further in physical
// memory.
inline uint32_t VirtualToPhysical(uint32_t va) { return (va & 0x1FFFFFFFu) + (va >= 0xE0000000u ? 0x1000u : 0u); }

// One draw: "user pointer" draws (DrawVerticesUP / DrawIndexedVerticesUP) fill the call
// themselves; DrawVertices / DrawIndexedVertices read their buffers from the device first.
void Draw(const DrawCall& call);

// Fills `call`'s streams / indices from the device's bound vertex and index buffers.
// Returns false if something needed is not bound.
bool FillBufferDraw(DrawCall& call, bool indexed, uint32_t start_index);

// D3D Swap: finish the frame and show `front_texture` (the texture the game passed to Swap,
// filled by its last Resolve) in the window. Creates the plume device on first use.
void Present(uint8_t* base, uint32_t front_texture);

// D3D Resolve: copy a render target region into a texture, then optionally clear
// (WORK_LOG 106). Pointers are guest addresses (0 = none).
void Resolve(uint8_t* base, uint32_t device, uint32_t flags, uint32_t src_rect, uint32_t dest_texture,
             uint32_t dest_point, uint32_t clear_color, float clear_z, uint32_t clear_stencil);

// D3D ClearF(flags, rect, D3DVECTOR4* color, z, stencil) on the current render targets.
void ClearF(uint8_t* base, uint32_t device, uint32_t flags, uint32_t rect, uint32_t color, float z, uint32_t stencil);

// D3D SetPredication(tile mask) of predicated tiling (0 = all tiles again).
void SetPredication(uint8_t* base, uint32_t device, uint32_t mask);

// SetViewport as the game asked for it (before the D3D library clamps it to the surface).
void SetRequestedViewport(float x, float y, float w, float h);

// True during the frame chosen with env SPYRO_PLUMEDRAWS=<frame> (debug logging of every call).
bool LoggingFrame();

// Time spent in the game's own D3D draw functions run to flush render state (plume_hooks), for
// the [plume/time] profile.
void AddFlushTime(double seconds);

}  // namespace spyro::plume_renderer

// Hooks on the game's Xbox 360 Direct3D library for the plume renderer (WORK_LOG 101).
// Active only when Renderer = plume (spyro::PlumeActive()); otherwise every hook calls the
// original and the Xenos GPU emulation renders as before. Addresses and roles:
// analysis\D3D_FUNCTION_MAP.txt.
//
// Skeleton strategy: there is no GPU emulation in plume mode, so nothing would ever consume the
// command buffer the D3D library writes GPU packets into (it would stall waiting for the GPU).
// The device's command-buffer pointers (+0x30 write, +0x38 end) are pointed at a large scratch
// buffer that is rewound every Present, so D3D code we have not replaced yet writes there
// harmlessly. Present / Clear / Resolve are replaced.

#include <rex/logging.h>
#include <rex/ppc.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "hang_dump.h"
#include "plume_renderer.h"
#include "spyro_settings.h"

extern "C" {
REX_EXTERN(sub_8260DC40);  // the game's operator new(size) -> r3
REX_EXTERN(__imp__sub_82435BB0);
REX_EXTERN(__imp__sub_82436AC0);
REX_EXTERN(__imp__sub_824364B0);
REX_EXTERN(__imp__sub_8242F1B0);
REX_EXTERN(__imp__sub_824356F0);
REX_EXTERN(__imp__sub_82442670);
REX_EXTERN(__imp__sub_82428768);
REX_EXTERN(__imp__sub_82428878);
REX_EXTERN(__imp__sub_8242C6E8);
REX_EXTERN(__imp__sub_8242CAD8);
REX_EXTERN(__imp__sub_8242C0E0);
REX_EXTERN(__imp__sub_8242C648);
REX_EXTERN(__imp__sub_82436E90);
REX_EXTERN(__imp__sub_82437210);
REX_EXTERN(__imp__sub_82424840);
REX_EXTERN(__imp__sub_82424B88);
REX_EXTERN(__imp__sub_82436D20);
REX_EXTERN(__imp__sub_82444670);
REX_EXTERN(__imp__sub_82424710);
}

namespace {

uint32_t Load32(const uint8_t* base, uint32_t a) {
  uint32_t v;
  std::memcpy(&v, base + a, 4);
  return __builtin_bswap32(v);
}
void Store32(uint8_t* base, uint32_t a, uint32_t v) {
  v = __builtin_bswap32(v);
  std::memcpy(base + a, &v, 4);
}

std::string HexWords(const uint8_t* base, uint32_t a, int n) {
  std::string s;
  char b[12];
  for (int i = 0; i < n; ++i) {
    std::snprintf(b, sizeof(b), " %08X", Load32(base, a + 4 * i));
    s += b;
  }
  return s;
}

// Calls guest fn(a0) on a fresh stack area below the current frame; returns r3.
uint32_t GuestCall1(PPCContext& ctx, uint8_t* base, void (*fn)(PPCContext&, uint8_t*), uint32_t a0) {
  PPCContext saved = ctx;
  ctx.r1.u64 = (ctx.r1.u32 - 0x400u) & ~0xFu;
  ctx.r3.u64 = a0;
  fn(ctx, base);
  uint32_t ret = ctx.r3.u32;
  ctx = saved;
  return ret;
}

constexpr uint32_t kScratchSize = 8u << 20;  // 8 MB of throw-away GPU packets per frame
constexpr uint32_t kScratchSlack = 64u << 10;
uint32_t g_device = 0, g_scratch = 0;
uint32_t g_front = 0;  // the texture the game last passed to Swap (what reaches the screen)
// Draw calls per frame (diagnostics): DrawVertices, DrawIndexedVertices, DrawVerticesUP,
// DrawIndexedVerticesUP.
uint32_t g_draw_counts[4];

// GPU fences: the device counts submitted work in +0x2A9C and reads the GPU's "finished up to"
// value through the pointer at +0x2A90. Without a GPU that value never moves and every
// BlockOnFence waits forever; report everything as finished.
void RetireAllFences(uint8_t* base, uint32_t device) {
  uint32_t retired = Load32(base, device + 0x2A90);
  if (retired) Store32(base, retired, Load32(base, device + 0x2A9C));
}

void RewindScratch(uint8_t* base) {
  if (!g_device || !g_scratch) return;
  Store32(base, g_device + 0x30, g_scratch);
  Store32(base, g_device + 0x38, g_scratch + kScratchSize - kScratchSlack);
  RetireAllFences(base, g_device);
}

}  // namespace

extern "C" {

// Direct3D_CreateDevice(d3d, adapter, type, flags, pp, &device): r8 = where the device goes.
REX_FUNC(sub_82435BB0) {
  uint32_t out = ctx.r8.u32;
  __imp__sub_82435BB0(ctx, base);
  if (!spyro::PlumeActive()) return;
  uint32_t hr = ctx.r3.u32;
  g_device = out ? Load32(base, out) : 0;
  REXLOG_INFO("[plume] CreateDevice -> 0x{:08X}, device 0x{:08X}; cmd buffer +0x30 0x{:08X} +0x34 0x{:08X} "
              "+0x38 0x{:08X} +0x3C 0x{:08X}",
              hr, g_device, Load32(base, g_device + 0x30), Load32(base, g_device + 0x34),
              Load32(base, g_device + 0x38), Load32(base, g_device + 0x3C));
  if (g_device) {
    g_scratch = GuestCall1(ctx, base, sub_8260DC40, kScratchSize);
    RewindScratch(base);
    REXLOG_INFO("[plume] command buffer -> scratch 0x{:08X} ({} MB)", g_scratch, kScratchSize >> 20);
  }
  // Log where the game is stuck if Present stops coming (originals of our hooks named too).
  spyro::hang_dump::AddSymbol(reinterpret_cast<const void*>(&__imp__sub_82435BB0), 0x82435BB0);
  spyro::hang_dump::AddSymbol(reinterpret_cast<const void*>(&__imp__sub_82436AC0), 0x82436AC0);
  spyro::hang_dump::AddSymbol(reinterpret_cast<const void*>(&__imp__sub_824364B0), 0x824364B0);
  spyro::hang_dump::AddSymbol(reinterpret_cast<const void*>(&__imp__sub_82436E90), 0x82436E90);
  spyro::hang_dump::AddSymbol(reinterpret_cast<const void*>(&__imp__sub_82437210), 0x82437210);
  spyro::hang_dump::Start(6);
}

static void PlumeFrame(uint8_t* base) {
  static int s_frames = 0;
  if (++s_frames % 300 == 0)
    REXLOG_INFO("[plume/2d] frame {} draws: DrawVertices {} DrawIndexedVertices {} DrawVerticesUP {} "
                "DrawIndexedVerticesUP {}",
                s_frames, g_draw_counts[0], g_draw_counts[1], g_draw_counts[2], g_draw_counts[3]);
  std::memset(g_draw_counts, 0, sizeof(g_draw_counts));
  spyro::plume_renderer::Present(base, g_front);
  RewindScratch(base);
  spyro::hang_dump::Progress();
}

// D3DDevice_Present(device).
REX_FUNC(sub_82436AC0) {
  if (!spyro::PlumeActive()) {
    __imp__sub_82436AC0(ctx, base);
    return;
  }
  PlumeFrame(base);
}

// D3DDevice_Swap(device, front buffer, params): what the engine's end of frame (sub_823B1058)
// actually calls; the original kicks the command buffer to the GPU, waits for room
// (sub_8242F1B0 -> sub_824356F0, forever without GPU emulation) and calls VdSwap.
REX_FUNC(sub_824364B0) {
  if (!spyro::PlumeActive()) {
    __imp__sub_824364B0(ctx, base);
    return;
  }
  uint32_t front = ctx.r4.u32;
  if (front != g_front) {
    REXLOG_INFO("[plume] Swap front buffer 0x{:08X}", front);
    g_front = front;
  }
  PlumeFrame(base);
  ctx.r3.u64 = 0;
}

// Shader capture (WORK_LOG 102): every shader the game creates is passed in as an Xbox 360
// shader container {flags, virtualSize (+4), physicalSize (+8), ...}; save each one (once, by
// content hash) to shaders\ in the working directory for SpyroShaderRecomp. In plume mode
// always, otherwise with env SPYRO_DUMPSHADERS. Returns the content hash (the cache key).
static uint64_t DumpShader(const uint8_t* base, uint32_t container, const char* kind) {
  static const bool s_env = std::getenv("SPYRO_DUMPSHADERS") != nullptr;
  if ((!spyro::PlumeActive() && !s_env) || !container) return 0;
  uint32_t total = Load32(base, container + 4) + Load32(base, container + 8);
  if (total == 0 || total > (4u << 20)) return 0;
  uint64_t h = 1469598103934665603ull;  // FNV-1a
  for (uint32_t i = 0; i < total; ++i) h = (h ^ base[container + i]) * 1099511628211ull;
  char name[96];
  std::snprintf(name, sizeof(name), "shaders/%s_%016llX.bin", kind, static_cast<unsigned long long>(h));
  std::error_code ec;
  std::filesystem::create_directories("shaders", ec);
  if (std::filesystem::exists(name, ec)) return h;
  if (FILE* f = std::fopen(name, "wb")) {
    std::fwrite(base + container, 1, total, f);
    std::fclose(f);
    static int s_count = 0;
    if (++s_count <= 8 || s_count % 50 == 0)
      REXLOG_INFO("[plume] shader #{} saved: {} ({} bytes, header{})", s_count, name, total,
                  HexWords(base, container, 8));
  }
  return h;
}

// Vertex inputs a vertex shader container declares (usage << 8 | index), same layout as
// XenosRecomp's shader.h: shader at +0x18 (container offset), elements at shader + 0x24.
static std::vector<uint16_t> VertexInputs(const uint8_t* base, uint32_t container) {
  std::vector<uint16_t> inputs;
  uint32_t shader = container + Load32(base, container + 0x18);
  uint32_t first = Load32(base, shader + 0x18), count = Load32(base, shader + 0x1C);
  for (uint32_t i = 0; i < count && i < 16; ++i) {
    uint32_t v = Load32(base, shader + 0x24 + 4 * (first + i));
    inputs.push_back(static_cast<uint16_t>(((v >> 12) & 0xF) << 8 | ((v >> 16) & 0xF)));
  }
  return inputs;
}

// Bytes of a shader container (what DumpShader hashes).
static uint32_t ContainerSize(const uint8_t* base, uint32_t container) {
  return Load32(base, container + 4) + Load32(base, container + 8);
}

// CreatePixelShader(container) / CreateVertexShader(container) -> r3 = shader object.
REX_FUNC(sub_82428768) {
  uint32_t container = ctx.r3.u32;
  uint64_t hash = DumpShader(base, container, "shaderA");
  __imp__sub_82428768(ctx, base);
  if (spyro::PlumeActive() && hash && ctx.r3.u32)
    spyro::plume_renderer::RegisterShader(ctx.r3.u32, hash, true, {}, base + container, ContainerSize(base, container));
}
REX_FUNC(sub_82428878) {
  uint32_t container = ctx.r3.u32;
  uint64_t hash = DumpShader(base, container, "shaderB");
  __imp__sub_82428878(ctx, base);
  if (spyro::PlumeActive() && hash && ctx.r3.u32)
    spyro::plume_renderer::RegisterShader(ctx.r3.u32, hash, false, VertexInputs(base, container), base + container,
                                          ContainerSize(base, container));
}

// D3DDevice_BlockOnFence(device, fence, ...): waits until the GPU has finished `fence`.
REX_FUNC(sub_8242F1B0) {
  if (!spyro::PlumeActive()) {
    __imp__sub_8242F1B0(ctx, base);
    return;
  }
  RetireAllFences(base, ctx.r3.u32);
}

// The D3D library's GPU-wait step (one poll of a wait loop; 0 = stop waiting, 1 = keep
// waiting). Besides the fence (handled above), loops wait on other words the GPU writes, e.g.
// its read position in the ring the predicated-tiling code fills (sub_8242EB58 -> sub_8242E928
// -> sub_8242E438 reads *(+0x2A90)+4). Without a GPU they never move: once that ring wrapped,
// after ~35000 frames (~10 min), the game froze for good (WORK_LOG 117). Nothing consumes
// those rings in plume mode, so every wait ends at once.
REX_FUNC(sub_824356F0) {
  if (!spyro::PlumeActive()) {
    __imp__sub_824356F0(ctx, base);
    return;
  }
  static int s_logged = 0;
  if (s_logged < 8) {
    ++s_logged;
    REXLOG_INFO("[plume] GPU wait skipped (called from 0x{:08X})", uint32_t(ctx.lr));
  }
  ctx.r3.u64 = 0;
}

// D3DDevice_Clear(device, count, rects, flags, color, z, stencil): r7 = D3DCOLOR.
REX_FUNC(sub_82436E90) {
  if (!spyro::PlumeActive()) {
    __imp__sub_82436E90(ctx, base);
    return;
  }
  if (ctx.r6.u32 & 1)  // D3DCLEAR_TARGET
    spyro::plume_renderer::SetClearColor(ctx.r7.u32);
}

// Draw calls: nothing is drawn yet at the skeleton stage. The UP ("user pointer") variants
// would also wait for room in a GPU-consumed ring (sub_8242C128 -> ... -> spin).
#define PLUME_SKIP(addr)                    \
  REX_FUNC(sub_##addr) {                    \
    if (!spyro::PlumeActive()) {            \
      __imp__sub_##addr(ctx, base);         \
      return;                               \
    }                                       \
  }
#undef PLUME_SKIP

// The D3D library only brings the device's register shadows (depth control, cull mode, ...) up
// to date inside its draw functions, when it flushes the render states set since the last draw
// (WORK_LOG 111: stale values culled walls and put a backdrop in front of the lava hall). So the
// original draw runs first - its GPU packets go to the scratch command buffer - and plume reads
// the state afterwards. Env SPYRO_PLUMENOFLUSH=1 skips it (comparison).
static bool FlushDrawState() {
  static const bool s_no_flush = std::getenv("SPYRO_PLUMENOFLUSH") != nullptr;
  return !s_no_flush;
}

// Times a flush for the [plume/time] profile.
struct FlushTimer {
  std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
  ~FlushTimer() {
    spyro::plume_renderer::AddFlushTime(
        std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count());
  }
};

// The "user pointer" draws (particles, smoke, ...) flush the same states in their own draw
// function, which also copies the vertices into a ring the GPU consumes (it would wait for
// room). Instead a DrawVertices of 0 vertices flushes the states (WORK_LOG 112: without it the
// grublins' dark spawn smoke was drawn with the previous draw's blend - see-through pale
// grublins in cutscenes).
static void FlushDrawStateUP(PPCContext& ctx, uint8_t* base, uint32_t device, uint32_t primitive) {
  if (!FlushDrawState()) return;
  PPCContext saved = ctx;
  ctx.r3.u64 = device;
  ctx.r4.u64 = primitive;
  ctx.r5.u64 = 0;
  ctx.r6.u64 = 0;
  {
    FlushTimer t;
    __imp__sub_8242C6E8(ctx, base);
  }
  ctx = saved;
}

// D3DDevice_DrawVertices(device, prim, startVertex, vertexCount) -> plume.
REX_FUNC(sub_8242C6E8) {
  if (!spyro::PlumeActive()) {
    __imp__sub_8242C6E8(ctx, base);
    return;
  }
  ++g_draw_counts[0];
  spyro::plume_renderer::DrawCall call;
  call.base = base;
  call.device = ctx.r3.u32;
  call.primitive = ctx.r4.u32;
  call.first_vertex = ctx.r5.u32;
  call.vertex_count = ctx.r6.u32;
  if (FlushDrawState()) {
    FlushTimer t;
    __imp__sub_8242C6E8(ctx, base);
  }
  if (spyro::plume_renderer::FillBufferDraw(call, false, 0)) spyro::plume_renderer::Draw(call);
}

// D3DDevice_DrawIndexedVertices(device, prim, baseVertexIndex, startIndex, indexCount) -> plume
// (WORK_LOG 106: r5 goes to VGT_INDX_OFFSET, r6 offsets the index buffer address, r7 = count).
REX_FUNC(sub_8242CAD8) {
  if (!spyro::PlumeActive()) {
    __imp__sub_8242CAD8(ctx, base);
    return;
  }
  ++g_draw_counts[1];
  spyro::plume_renderer::DrawCall call;
  call.base = base;
  call.device = ctx.r3.u32;
  call.primitive = ctx.r4.u32;
  call.base_vertex = ctx.r5.s32;
  call.index_count = ctx.r7.u32;
  uint32_t start_index = ctx.r6.u32;
  if (FlushDrawState()) {
    FlushTimer t;
    __imp__sub_8242CAD8(ctx, base);
  }
  if (spyro::plume_renderer::FillBufferDraw(call, true, start_index)) spyro::plume_renderer::Draw(call);
}


// D3DDevice_DrawVerticesUP(device, prim, vertexCount, vertexData, stride) -> plume.
REX_FUNC(sub_8242C0E0) {
  if (!spyro::PlumeActive()) {
    __imp__sub_8242C0E0(ctx, base);
    return;
  }
  ++g_draw_counts[2];
  spyro::plume_renderer::DrawCall call;
  call.base = base;
  call.device = ctx.r3.u32;
  call.primitive = ctx.r4.u32;
  call.vertex_count = ctx.r5.u32;
  call.stream_data[0] = ctx.r6.u32;
  call.stream_stride[0] = ctx.r7.u32;
  FlushDrawStateUP(ctx, base, call.device, call.primitive);
  spyro::plume_renderer::Draw(call);
}

// D3DDevice_DrawIndexedVerticesUP(device, prim, minIndex, numVertices, indexCount, indexData,
//                                 indexFormat (bit 2 = 32-bit), vertexData, stride [stack +0x54]).
REX_FUNC(sub_8242C648) {
  if (!spyro::PlumeActive()) {
    __imp__sub_8242C648(ctx, base);
    return;
  }
  ++g_draw_counts[3];
  spyro::plume_renderer::DrawCall call;
  call.base = base;
  call.device = ctx.r3.u32;
  call.primitive = ctx.r4.u32;
  call.index_count = ctx.r7.u32;  // indices address vertices from vertexData (r5/r6 not needed)
  call.index_data = ctx.r8.u32;
  call.index_32bit = (ctx.r9.u32 & 4) != 0;
  call.stream_data[0] = ctx.r10.u32;
  call.stream_stride[0] = Load32(base, ctx.r1.u32 + 0x54);
  FlushDrawStateUP(ctx, base, call.device, call.primitive);
  spyro::plume_renderer::Draw(call);
}
// D3DDevice_Resolve(device, flags, srcRect, destTexture, destPoint, destLevel, destSlice,
//                   const D3DVECTOR4* clearColor (r10), float clearZ (f1), clearStencil, params)
// Copies EDRAM to a texture and can clear afterwards: this is how the game clears its screen
// (flags 0x100 = D3DRESOLVE_CLEARRENDERTARGET, 0x200 = CLEARDEPTHSTENCIL; it never calls Clear).
// No copy yet at the skeleton stage; the clear colour goes to plume.
REX_FUNC(sub_82442670) {
  if (!spyro::PlumeActive()) {
    __imp__sub_82442670(ctx, base);
    return;
  }
  uint32_t flags = ctx.r4.u32, color = ctx.r10.u32, dest = ctx.r6.u32;
  if (spyro::plume_renderer::LoggingFrame()) {
    uint32_t rect = ctx.r5.u32, point = ctx.r7.u32;
    REXLOG_INFO("[plume/call] Resolve flags 0x{:X} srcRect {} dest 0x{:08X} (fetch{}) destPoint {} level {} slice {}",
                flags, rect ? HexWords(base, rect, 4) : std::string(" -"), dest,
                dest ? HexWords(base, dest + 0x1C, 6) : std::string(" -"), point ? HexWords(base, point, 2) : std::string(" -"),
                ctx.r8.u32, ctx.r9.u32);
  }
  static int s_logged = 0;
  if (s_logged < 12) {
    ++s_logged;
    REXLOG_INFO("[plume] Resolve flags 0x{:X} src 0x{:08X} dest 0x{:08X} clearColor* 0x{:08X} ({}) z {:.3f}", flags,
                ctx.r5.u32, ctx.r6.u32, color,
                color ? fmt::format("{:08X} {:08X} {:08X} {:08X}", Load32(base, color), Load32(base, color + 4),
                                    Load32(base, color + 8), Load32(base, color + 12))
                      : std::string("-"),
                ctx.f1.f64);
  }
  spyro::plume_renderer::Resolve(base, ctx.r3.u32, flags, ctx.r5.u32, dest, ctx.r7.u32, color, float(ctx.f1.f64), 0);
  // Only the resolve into the front buffer (the texture Swap shows) decides the screen; the
  // game also resolves/clears off-screen buffers (effects) with other colours.
  if ((flags & 0x100) && color && (!g_front || dest == g_front)) {
    auto f = [&](uint32_t a) {
      uint32_t v = Load32(base, a);
      float x;
      std::memcpy(&x, &v, 4);
      return x < 0 ? 0u : x > 1 ? 255u : static_cast<uint32_t>(x * 255.0f + 0.5f);
    };
    // D3DVECTOR4 order is r, g, b, a.
    spyro::plume_renderer::SetClearColor(f(color + 12) << 24 | f(color) << 16 | f(color + 4) << 8 | f(color + 8));
  }
}

// Debug (env SPYRO_PLUMEDRAWS=<frame>): render target / viewport / tiling calls of that frame,
// in order with the draws, to see how the game builds a 3D frame (WORK_LOG 106).
static void LogCall(const char* name, PPCContext& ctx) {
  REXLOG_INFO("[plume/call] {} r3 0x{:08X} r4 0x{:08X} r5 0x{:08X} r6 0x{:08X} r7 0x{:08X} r8 0x{:08X}", name,
              ctx.r3.u32, ctx.r4.u32, ctx.r5.u32, ctx.r6.u32, ctx.r7.u32, ctx.r8.u32);
}
#define PLUME_LOGGED(addr, name)                                                    \
  REX_FUNC(sub_##addr) {                                                            \
    if (spyro::PlumeActive() && spyro::plume_renderer::LoggingFrame()) LogCall(name, ctx); \
    __imp__sub_##addr(ctx, base);                                                   \
  }
PLUME_LOGGED(82424B88, "SetDepthStencilSurface(dev, surface)")
#undef PLUME_LOGGED

// D3DDevice_SetPredication(dev, tile mask) (WORK_LOG 106): per-tile commands of predicated tiling.
REX_FUNC(sub_82436D20) {
  if (spyro::PlumeActive()) {
    if (spyro::plume_renderer::LoggingFrame()) LogCall("SetPredication(dev, tile mask)", ctx);
    spyro::plume_renderer::SetPredication(base, ctx.r3.u32, ctx.r4.u32);
  }
  __imp__sub_82436D20(ctx, base);
}

// D3DDevice_ClearF(dev, flags, rect, const D3DVECTOR4* color, float z (f1), stencil (r7)):
// the game's clears (also per tile while predicated tiling) -> plume. The original still runs
// (its GPU packets go to the scratch command buffer).
REX_FUNC(sub_82444670) {
  if (spyro::PlumeActive()) {
    if (spyro::plume_renderer::LoggingFrame()) {
      LogCall("ClearF(dev, flags, rect, color, -, stencil)", ctx);
      REXLOG_INFO("[plume/call]   ClearF z {:.4f} color{}", ctx.f1.f64,
                  ctx.r6.u32 ? HexWords(base, ctx.r6.u32, 4) : std::string(" -"));
    }
    spyro::plume_renderer::ClearF(base, ctx.r3.u32, ctx.r4.u32, ctx.r5.u32, ctx.r6.u32, float(ctx.f1.f64), ctx.r7.u32);
  }
  __imp__sub_82444670(ctx, base);
}

// SetRenderTarget(dev, index, surface).
REX_FUNC(sub_82424840) {
  if (spyro::PlumeActive() && spyro::plume_renderer::LoggingFrame()) {
    uint32_t s = ctx.r5.u32;
    REXLOG_INFO("[plume/call] SetRenderTarget {} 0x{:08X}:{}", ctx.r4.u32, s, s ? HexWords(base, s, 14) : std::string());
  }
  __imp__sub_82424840(ctx, base);
}

// SetViewport(dev, D3DVIEWPORT9* {x, y, w, h, minZ, maxZ}).
REX_FUNC(sub_82424710) {
  if (spyro::PlumeActive() && ctx.r4.u32) {
    uint32_t vp = ctx.r4.u32;
    if (spyro::plume_renderer::LoggingFrame())
      REXLOG_INFO("[plume/call] SetViewport {} {} {} {}", Load32(base, vp), Load32(base, vp + 4), Load32(base, vp + 8),
                  Load32(base, vp + 12));
    spyro::plume_renderer::SetRequestedViewport(float(Load32(base, vp)), float(Load32(base, vp + 4)),
                                                float(Load32(base, vp + 8)), float(Load32(base, vp + 12)));
  }
  __imp__sub_82424710(ctx, base);
}

// sub_82437210: second EDRAM/tiling helper (called around the frame end; was first thought to
// be Resolve). Skipped in plume mode - it drives the GPU only.
REX_FUNC(sub_82437210) {
  if (!spyro::PlumeActive()) {
    __imp__sub_82437210(ctx, base);
    return;
  }
}

}  // extern "C"

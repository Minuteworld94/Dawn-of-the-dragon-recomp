// Draw distance (WORK_LOG 127): Recomp Settings "Draw distance: Original / Max".
//
// The game hides far things five ways; Max turns off four of them (the user: no pop-in at all,
// everything on the map visible from anywhere), except the light rays' fade (they only show close
// up in the original):
//   1. per-object distance limit (sub_82109918),
//   2. per-group fade / max distance records (sub_820F0CF0),
//   3. sphere zones (a view distance) - but NOT 4. box zones, which are area logic with content
//      meant only for inside the box (sub_820F0A70 / sub_820F0480; WORK_LOG 138),
//   5. the view frustum's far plane at 150 units (sub_823B2FA8) - while the picture is clipped
//      much further out.
// Plus, in plume_renderer.cpp: the near camera's projection far plane (= the hand-over distance)
// moved out and the far-picture blend off (WORK_LOG 134).
// The render camera's far clip (sub_820DA658, ~1500+ units) is not one of them: it stays.
// Research switches (env SPYRO_*) are described at each hook; none is on by default.

#include <rex/logging.h>
#include <rex/ppc.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <chrono>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#endif

#include "guest_memory.h"
#include "plume_renderer.h"
#include "spyro_settings.h"

extern "C" {
REX_EXTERN(__imp__sub_820DA658);  // render camera setup (r3 = scene renderer)
REX_EXTERN(sub_823B1C98);         // camera: rebuild projection
REX_EXTERN(sub_823B3ED0);         // node: mark changed
}

namespace {

uint32_t Load32(const uint8_t* base, uint32_t a) {
  uint32_t v;
  std::memcpy(&v, base + a, 4);
  return __builtin_bswap32(v);
}

float LoadF(const uint8_t* base, uint32_t a) {
  uint32_t w = Load32(base, a);
  float f;
  std::memcpy(&f, &w, 4);
  return f;
}

void StoreF(uint8_t* base, uint32_t a, float f) {
  uint32_t w;
  std::memcpy(&w, &f, 4);
  w = __builtin_bswap32(w);
  std::memcpy(base + a, &w, 4);
}

// fn(a0) from inside a hook, on a fresh stack area below the frame; all registers restored.
void Call(PPCContext& ctx, uint8_t* base, void (*fn)(PPCContext&, uint8_t*), uint32_t a0) {
  PPCContext saved = ctx;
  ctx.r1.u64 = (ctx.r1.u32 - 0x400u) & ~0xFu;
  ctx.r3.u64 = a0;
  fn(ctx, base);
  ctx = saved;
}

constexpr uint32_t kCamNear = 128, kCamFar = 132;

// How much further than the game's far clip (env SPYRO_FARSCALE overrides; research:
// SPYRO_FARSCALE_TOGGLE applies it on every other frame only).
float FarScale() {
  static const float s_env = std::getenv("SPYRO_FARSCALE") ? float(std::atof(std::getenv("SPYRO_FARSCALE"))) : 0.0f;
  if (s_env > 0) {
    static const bool s_toggle = std::getenv("SPYRO_FARSCALE_TOGGLE") != nullptr;
    static uint32_t s_frame = 0;
    return !s_toggle || (++s_frame & 1) ? s_env : 1.0f;
  }
  // Not tied to the setting: the far clip (Avalar ~1500 units) is not what hides far objects -
  // drawing it 4x further on every other frame drew the same objects (WORK_LOG 127).
  return 1.0f;
}

}  // namespace

// THE per-object distance check (found with the bucket-count watchpoint below): the scene walk
// (sub_820ED200 / sub_820FFD50 / sub_820F0A70 / sub_820EB910 ...) hands each object to
// sub_820EC4F8 / sub_820F1130, which call sub_82109918(object, ..., r7 = check distance, f1 = max
// distance SQUARED). With r7 = 1 it compares the squared distance from the camera (renderer
// [[[0x82835934]+44]+56]+4 +128..+136) to the object ([obj+28] +44..+52) with f1 and, if further,
// does not put the object in a render bucket - trees, stones, the Avalar bandit camp's towers
// vanished that way (user). Draw distance Max: f1 x 16 = 4x the distance.
constexpr double kMaxDistanceFactor = 4.0;
uint32_t g_frame_parity = 0;  // research toggle (SPYRO_DISTSCALE_TOGGLE): counts render setups

// Research (env SPYRO_FARMASK=<bits>): which callers of the frustum test get the far plane
// opened - 1 group check (sub_820F0CF0), 2 animated nodes (sub_820EB9C0), 4 node objects
// (sub_820EC4F8), 8 everything else. Default: all.
uint32_t g_far_caller = 8;
struct FarCaller {
  uint32_t saved;
  explicit FarCaller(uint32_t id) : saved(g_far_caller) { g_far_caller = id; }
  ~FarCaller() { g_far_caller = saved; }
};

extern "C" REX_EXTERN(__imp__sub_82109918);
extern "C" REX_FUNC(sub_82109918) {
  static const bool s_toggle = std::getenv("SPYRO_DISTSCALE_TOGGLE") != nullptr;
  bool max = s_toggle ? (g_frame_parity & 1) != 0 : spyro::GetSetting(spyro::kDrawDistance) == 1;
  if (max && (ctx.r7.u32 & 0xFF) == 1) ctx.f1.f64 = 1e30;  // no distance limit (user: no pop-in at all)
  // Research (env SPYRO_LOGVIS): after the distance check the object's own vt slot 33 (+132)
  // decides whether it is drawn - each object vtable and its slot-33 function, once.
  static const bool s_vis = std::getenv("SPYRO_LOGVIS") != nullptr;
  if (s_vis && spyro::GuestReadable(base, ctx.r3.u32, 0x30)) {
    static std::unordered_set<uint32_t> s_vts;
    uint32_t vt = Load32(base, ctx.r3.u32);
    if (s_vts.size() < 200 && spyro::GuestReadable(base, vt, 0x88) && s_vts.insert(vt).second)
      REXLOG_INFO("[spyro/drawdist] object vt 0x{:08X} slot33 0x{:08X} r7 {}", vt, Load32(base, vt + 132), ctx.r7.u32 & 0xFF);
  }
  // Research (env SPYRO_LOGOBJS): every object handed to this check, once, with its position
  // and host stack - near vs far runs show which objects never get here from far away and
  // through which scene-walk path they came.
  // Only from 10 s of gameplay on (menu state 0x24), when a fixed research camera has settled.
  static const bool s_objs = std::getenv("SPYRO_LOGOBJS") != nullptr;
  static uint32_t s_since = 0, s_last_parity = 0;
  if (s_objs && g_frame_parity != s_last_parity) {
    s_last_parity = g_frame_parity;
    s_since = spyro::CurrentMenuId() == 0x24 ? s_since + 1 : 0;
  }
  if (s_objs && s_since > 600 && spyro::GuestReadable(base, ctx.r3.u32, 0x30)) {
    static std::unordered_set<uint32_t> s_seen;
    uint32_t obj = ctx.r3.u32, tf = Load32(base, obj + 28);
    if (s_seen.size() < 6000 && spyro::GuestReadable(base, tf, 0x38) && s_seen.insert(obj).second) {
      void* frames[24];
      USHORT n = RtlCaptureStackBackTrace(1, 24, frames, nullptr);
      uintptr_t exe = reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr));
      std::string s;
      for (USHORT i = 0; i < n; ++i) s += fmt::format(" {:X}", reinterpret_cast<uintptr_t>(frames[i]) - exe);
      // The bucket it goes to: [r4 + ([obj+20] & 31) * 4], added with its vt slot 0.
      uint32_t bucket = Load32(base, ctx.r4.u32 + (Load32(base, obj + 20) & 31) * 4);
      uint32_t bvt = spyro::GuestReadable(base, bucket, 4) ? Load32(base, bucket) : 0;
      REXLOG_INFO("[spyro/obj] 0x{:08X} vt 0x{:08X} at {:.1f} {:.1f} {:.1f} r7 {} bucket vt 0x{:08X} add 0x{:08X} |{}", obj,
                  Load32(base, obj), LoadF(base, tf + 44), LoadF(base, tf + 48), LoadF(base, tf + 52), ctx.r7.u32 & 0xFF,
                  bvt, spyro::GuestReadable(base, bvt, 4) ? Load32(base, bvt) : 0, s);
    }
  }
  __imp__sub_82109918(ctx, base);
}

// The GROUP check before it (the user still saw the bandit camp vanish with only the one above):
// node type sub_820F02C8 calls sub_820F0CF0(group, node) first. It takes a distance record
// [[[group]+24] + [group+36]*4]: +8 fade start SQUARED, +12 max distance SQUARED, +16 fade per
// squared unit, +4 flags; camera [[[0x82835904]+8]+56]+4 +64..+72, node [node+4] +128..+136.
// Past +12 the whole group is dropped; between +8 and +12 it fades (+92 of the group). Max:
// the record is scaled for the call (distances x16 = 4x, fade rate /16) and put back after.
extern "C" REX_EXTERN(__imp__sub_820F0CF0);
extern "C" REX_FUNC(sub_820F0CF0) {
  FarCaller far_caller(1);
  static const bool s_toggle = std::getenv("SPYRO_DISTSCALE_TOGGLE") != nullptr;
  bool max = s_toggle ? (g_frame_parity & 1) != 0 : spyro::GetSetting(spyro::kDrawDistance) == 1;
  uint32_t group = ctx.r3.u32, rec = 0;
  if (max && spyro::GuestReadable(base, group, 0x28)) {
    uint32_t owner = Load32(base, group), table = 0;
    if (spyro::GuestReadable(base, owner, 0x1C)) table = Load32(base, owner + 24);
    uint32_t slot = table + Load32(base, group + 36) * 4;
    if (table && spyro::GuestReadable(base, slot, 4)) rec = Load32(base, slot);
    if (!spyro::GuestReadable(base, rec, 0x14)) rec = 0;
  }
  if (!rec) {
    __imp__sub_820F0CF0(ctx, base);
    return;
  }
  const float k2 = float(kMaxDistanceFactor * kMaxDistanceFactor);
  float fade_start = LoadF(base, rec + 8), max_dist = LoadF(base, rec + 12), fade_rate = LoadF(base, rec + 16);
  // Research (env SPYRO_LOGRECORDS): each distance record once - values, flags, the group's
  // vtable (light rays vs props).
  static const bool s_log = std::getenv("SPYRO_LOGRECORDS") != nullptr;
  if (s_log) {
    // Every group (node) using a record, with its world position (node [+4] +128..+136).
    static std::unordered_set<uint64_t> s_nodes;
    uint32_t node = ctx.r4.u32, nd = spyro::GuestReadable(base, node, 8) ? Load32(base, node + 4) : 0;
    if (nd && spyro::GuestReadable(base, nd, 0x8C) && s_nodes.size() < 400 &&
        s_nodes.insert(uint64_t(rec) << 32 | node).second)
      REXLOG_INFO("[spyro/drawdist] group of record 0x{:08X} (fade {:.0f}..{:.0f}) at ({:.1f} {:.1f} {:.1f}) node 0x{:08X}",
                  rec, std::sqrt(std::max(fade_start, 0.0f)), std::sqrt(std::max(max_dist, 0.0f)),
                  LoadF(base, nd + 128), LoadF(base, nd + 132), LoadF(base, nd + 136), node);
    static std::unordered_set<uint32_t> s_seen;
    if (s_seen.size() < 300 && s_seen.insert(rec).second) {
      uint32_t node = ctx.r4.u32;
      REXLOG_INFO("[spyro/drawdist] record 0x{:08X} flags 0x{:08X} fade start {:.1f} max {:.1f} rate {:g} | group "
                  "0x{:08X} node 0x{:08X} vt 0x{:08X} node+28 flags 0x{:08X}",
                  rec, Load32(base, rec + 4), std::sqrt(std::max(fade_start, 0.0f)), std::sqrt(std::max(max_dist, 0.0f)),
                  fade_rate, group, node, spyro::GuestReadable(base, node, 4) ? Load32(base, node) : 0,
                  spyro::GuestReadable(base, node + 28, 4) && spyro::GuestReadable(base, Load32(base, node + 28), 0x18)
                      ? Load32(base, Load32(base, node + 28) + 20)
                      : 0);
    }
  }
  (void)k2;
  // The light rays keep their fade: in the original they only show once Spyro is close (user),
  // and without it every ray of the level shows at once as a yellow glare. In Avalar that is the
  // record fading from 1 unit to 45 (17 groups scattered over the level). Records fading from
  // further out are props: flowers (17..40 with the curved fade, 178 groups), rocks, trees, the
  // camp - they never fade or vanish with Max (the user wants no pop-in at all).
  bool effect = fade_start < 5.0f * 5.0f;
  // Research (env SPYRO_DISTKEEP=<fade start>,...): records whose fade start (units, rounded)
  // is listed keep the game's fade - to find which records are the light shafts.
  static const std::vector<int> s_keep = [] {
    std::vector<int> v;
    if (const char* e = std::getenv("SPYRO_DISTKEEP"))
      for (const char* p = e; *p;) {
        v.push_back(std::atoi(p));
        while (*p && *p != ',') ++p;
        if (*p) ++p;
      }
    return v;
  }();
  int start_units = int(std::lround(std::sqrt(std::max(fade_start, 0.0f))));
  if (std::find(s_keep.begin(), s_keep.end(), start_units) != s_keep.end()) effect = true;
  // Research (env SPYRO_FARSKIP=<fade start>,...): those records' groups keep the frustum's far
  // plane (caller id 16, outside SPYRO_FARMASK).
  static const std::vector<int> s_far_skip = [] {
    std::vector<int> v;
    if (const char* e = std::getenv("SPYRO_FARSKIP"))
      for (const char* p = e; *p;) {
        v.push_back(std::atoi(p));
        while (*p && *p != ',') ++p;
        if (*p) ++p;
      }
    return v;
  }();
  if (std::find(s_far_skip.begin(), s_far_skip.end(), start_units) != s_far_skip.end()) g_far_caller = 16;
  uint32_t node_arg = ctx.r4.u32;
  if (effect) {
    __imp__sub_820F0CF0(ctx, base);
  } else {
    StoreF(base, rec + 8, 3.0e38f);
    StoreF(base, rec + 12, 3.0e38f);
    __imp__sub_820F0CF0(ctx, base);
    StoreF(base, rec + 8, fade_start);
    StoreF(base, rec + 12, max_dist);
    StoreF(base, rec + 16, fade_rate);
  }
  // Research (env SPYRO_LOGGROUPS): groups the check still rejects (returns 0), once each, with
  // their bounding sphere (group +20..+32) and record - what Max still hides.
  // SPYRO_LOGWALL: every 2 s, the result for groups along the Avalar camp's back wall
  // (z -90..-70, y 18..36, x -45..45).
  static const bool s_wall = std::getenv("SPYRO_LOGWALL") != nullptr;
  if (s_wall) {
    float gx = LoadF(base, group + 20), gy = LoadF(base, group + 24), gz = LoadF(base, group + 28);
    static auto s_last = std::chrono::steady_clock::now();
    static std::string s_res;
    if (gz > -90 && gz < -70 && gy > 18 && gy < 36 && gx > -45 && gx < 45 && s_res.size() < 900)
    {
      // Its objects (node +28 list, next at +24): node +16 flags, alpha +35, +20 flags.
      std::string objs;
      uint32_t node = node_arg, o = spyro::GuestReadable(base, node, 0x20) ? Load32(base, node + 28) : 0;
      for (int k = 0; k < 3 && spyro::GuestReadable(base, o, 0x28); ++k, o = Load32(base, o + 24))
      {
        uint32_t geo = Load32(base, o + 28);
        uint32_t gvt = spyro::GuestReadable(base, geo, 4) ? Load32(base, geo) : 0;
        objs += fmt::format(" o{:08X}/a{}/f{:08X} geo {:08X} vt {:08X} s18 {:08X}", o, base[o + 35], Load32(base, o + 20), geo,
                            gvt, spyro::GuestReadable(base, gvt + 72, 4) ? Load32(base, gvt + 72) : 0);
      }
      s_res += fmt::format(" [{:08X} {:.0f},{:.0f},{:.0f} r{:.0f} fade {:.0f}..{:.0f} +92 {:.2f} -> {}{}]", group, gx, gy, gz,
                           LoadF(base, group + 32), std::sqrt(std::max(fade_start, 0.0f)),
                           std::sqrt(std::max(max_dist, 0.0f)), LoadF(base, group + 92), ctx.r3.u32 & 0xFF, objs);
    }
    if (std::chrono::steady_clock::now() - s_last > std::chrono::seconds(2)) {
      REXLOG_INFO("[spyro/group] wall:{}", s_res);
      s_res.clear();
      s_last = std::chrono::steady_clock::now();
    }
    // One tree cluster's first object and its [obj+28] block, word by word (LOD hunt), every 2 s.
    static auto s_last_tree = std::chrono::steady_clock::now();
    if (gz > -82 && gz < -78 && gx > 25 && gx < 30 &&
        std::chrono::steady_clock::now() - s_last_tree > std::chrono::seconds(2)) {
      s_last_tree = std::chrono::steady_clock::now();
      {
        uint32_t o = spyro::GuestReadable(base, node_arg, 0x20) ? Load32(base, node_arg + 28) : 0;
        if (spyro::GuestReadable(base, o, 0x40)) {
          std::string w;
          for (uint32_t k = 0; k < 0x40; k += 4) w += fmt::format(" {:08X}", Load32(base, o + k));
          uint32_t geo = Load32(base, o + 28);
          w += " | geo";
          for (uint32_t k = 0; k < 0x100 && spyro::GuestReadable(base, geo + k, 4); k += 4) w += fmt::format(" {:08X}", Load32(base, geo + k));
          REXLOG_INFO("[spyro/group] tree obj 0x{:08X}:{}", o, w);
        }
      }
    }
  }
  // SPYRO_LOGGROUPSALL: every group that passes, once, 10 s into the level (what Max draws).
  static const bool s_all_groups = std::getenv("SPYRO_LOGGROUPSALL") != nullptr;
  static uint32_t s_since_level = 0, s_parity_seen = 0;
  if (s_all_groups && g_frame_parity != s_parity_seen) {
    s_parity_seen = g_frame_parity;
    s_since_level = spyro::CurrentMenuId() == 0x24 ? s_since_level + 1 : 0;
  }
  if (s_all_groups && s_since_level > 600 && (ctx.r3.u32 & 0xFF) == 1) {
    static std::unordered_set<uint32_t> s_ok;
    if (s_ok.size() < 3000 && s_ok.insert(group).second)
      REXLOG_INFO("[spyro/group] drawn 0x{:08X} sphere {:.1f} {:.1f} {:.1f} r {:.1f} fade {:.0f}", group,
                  LoadF(base, group + 20), LoadF(base, group + 24), LoadF(base, group + 28), LoadF(base, group + 32),
                  std::sqrt(std::max(fade_start, 0.0f)));
  }
  static const bool s_groups = std::getenv("SPYRO_LOGGROUPS") != nullptr;
  if (s_groups && (ctx.r3.u32 & 0xFF) == 0) {
    static std::unordered_set<uint32_t> s_rej;
    if (s_rej.size() < 400 && s_rej.insert(group).second)
      REXLOG_INFO("[spyro/group] rejected 0x{:08X} node 0x{:08X} sphere {:.1f} {:.1f} {:.1f} r {:.1f} fade {:.0f}..{:.0f}{}",
                  group, node_arg, LoadF(base, group + 20), LoadF(base, group + 24), LoadF(base, group + 28),
                  LoadF(base, group + 32), std::sqrt(std::max(fade_start, 0.0f)), std::sqrt(std::max(max_dist, 0.0f)),
                  effect ? " (kept: effect)" : "");
  }
}

// The THIRD mechanism (the camp's towers, huts and flag still vanished with both checks above
// off - user screenshots): zone-gated nodes. Every walker (sub_820FFD50, sub_820ED200,
// sub_820EB910 ...) skips a node whose +16 bit 2 is set; node type sub_820F0A70 sets that bit
// each frame unless the camera is inside one of its zones ([+36] list, count +44), tested by
// sub_820F0480(zone, camera position): zone +28 low 2 bits = kind - 1 sphere (centre +4..+12,
// box half-size +16, radius^2 +20: in practice a view distance), 0 box (sub_820C2150), 3 never.
// Max: sphere and box zones count as "inside" for these nodes, so their content shows from
// anywhere (kind 3, "never", is left alone).
bool g_in_zone_node = false;
uint32_t g_zone_node = 0;

extern "C" REX_EXTERN(__imp__sub_820F0A70);
extern "C" REX_FUNC(sub_820F0A70) {
  uint32_t node = ctx.r3.u32;
  g_in_zone_node = true;
  g_zone_node = node;
  __imp__sub_820F0A70(ctx, base);
  g_in_zone_node = false;
  // Research (env SPYRO_LOGZONES): zone nodes left hidden (+16 bit 2), once each, with their
  // zones' kinds and centres.
  static const bool s_log = std::getenv("SPYRO_LOGZONES") != nullptr;
  if (s_log && spyro::GuestReadable(base, node, 0x30) && (Load32(base, node + 16) & 4)) {
    static std::unordered_set<uint32_t> s_seen;
    if (s_seen.size() < 300 && s_seen.insert(node).second) {
      uint32_t n = Load32(base, node + 44), list = Load32(base, node + 36);
      std::string zs;
      for (uint32_t i = 0; i < n && i < 6 && spyro::GuestReadable(base, list + i * 4, 4); ++i) {
        uint32_t z = Load32(base, list + i * 4);
        if (!spyro::GuestReadable(base, z, 0x20)) continue;
        zs += fmt::format(" [kind {} at {:.0f} {:.0f} {:.0f} half {:.0f} r2 {:.0f}]", base[z + 28] & 3, LoadF(base, z + 4),
                          LoadF(base, z + 8), LoadF(base, z + 12), LoadF(base, z + 16), LoadF(base, z + 20));
      }
      REXLOG_INFO("[spyro/zone] hidden node 0x{:08X} flags 0x{:08X} zones {}{}", node, Load32(base, node + 16), n, zs);
    }
  }
}

extern "C" REX_EXTERN(__imp__sub_820F0480);
extern "C" REX_FUNC(sub_820F0480) {
  uint32_t zone = ctx.r3.u32, g_zone_cam = ctx.r4.u32;  // r4 = the camera position (3 floats)
  __imp__sub_820F0480(ctx, base);
  if (!g_in_zone_node || (ctx.r3.u32 & 0xFF) == 1) return;
  static const bool s_toggle = std::getenv("SPYRO_DISTSCALE_TOGGLE") != nullptr;
  bool max = s_toggle ? (g_frame_parity & 1) != 0 : spyro::GetSetting(spyro::kDrawDistance) == 1;
  // Box zones are NOT forced any more (2026-11-09, WORK_LOG 138): they are area logic, not a view
  // distance - their nodes hold content meant to be seen only from inside the box, e.g. level 1's
  // giant backdrop card (one 380x320 quad, ps B8EA455F) that, forced, stood right behind the
  // player and turned the whole hall behind the ledge black. Forcing them only ever helped
  // research views 400+ units out (796 -> 2767 draws). Research: SPYRO_ZONEBOX=1 forces them,
  // =2 every other frame.
  static const int s_boxes = std::getenv("SPYRO_ZONEBOX") ? std::atoi(std::getenv("SPYRO_ZONEBOX")) : 0;
  bool boxes = s_boxes == 1 || (s_boxes == 2 && (g_frame_parity & 1));
  uint8_t kind = spyro::GuestReadable(base, zone, 0x20) ? base[zone + 28] & 3 : 3;
  static const bool s_no_sphere = std::getenv("SPYRO_NOSPHERE") != nullptr;  // research
  bool force = max && ((kind == 1 && !s_no_sphere) || (boxes && kind == 0));
  // Research (env SPYRO_LOGZONEFORCE, WORK_LOG 138): every zone the override turns from "outside"
  // to "inside", once, with its node, raw fields and the camera position (r4 of the call).
  static const bool s_log_force = std::getenv("SPYRO_LOGZONEFORCE") != nullptr;
  if (s_log_force && force && spyro::GuestReadable(base, zone, 0x60)) {
    static std::unordered_set<uint32_t> s_seen;
    if (s_seen.size() < 400 && s_seen.insert(zone).second) {
      std::string f;
      for (uint32_t o = 4; o < 0x60; o += 4) f += fmt::format(" {:.1f}", LoadF(base, zone + o));
      uint32_t cam = g_zone_cam;
      bool cok = spyro::GuestReadable(base, cam, 12);
      REXLOG_INFO("[spyro/zoneforce] zone 0x{:08X} kind {} node 0x{:08X} cam {:.0f} {:.0f} {:.0f} | +4..:{}", zone, kind, g_zone_node,
                  cok ? LoadF(base, cam) : 0.0f, cok ? LoadF(base, cam + 4) : 0.0f, cok ? LoadF(base, cam + 8) : 0.0f, f);
    }
  }
  // Research (env SPYRO_ZONEFORCE_ONLY=<n>): force only the n-th distinct zone (first-seen order).
  static const int s_only = std::getenv("SPYRO_ZONEFORCE_ONLY") ? std::atoi(std::getenv("SPYRO_ZONEFORCE_ONLY")) : -1;
  if (force && s_only >= 0) {
    static std::vector<uint32_t> s_order;
    auto it = std::find(s_order.begin(), s_order.end(), zone);
    int idx = it == s_order.end() ? (s_order.push_back(zone), int(s_order.size()) - 1) : int(it - s_order.begin());
    force = idx == s_only;
  }
  if (force) ctx.r3.u64 = 1;
}

// Research: slot 33 of the scene-object classes (0x82069558): 0 when the object's handle
// ([obj+28], an offset into the table at [0x82837FE0]) has an entry whose +4 is 0. SPYRO_LOGVIS
// counts the 0s per second, SPYRO_VISALL returns 1 instead.
extern "C" REX_EXTERN(__imp__sub_8210ABD0);
extern "C" REX_FUNC(sub_8210ABD0) {
  static const bool s_all = std::getenv("SPYRO_VISALL") != nullptr;
  static const bool s_log = std::getenv("SPYRO_LOGVIS") != nullptr;
  __imp__sub_8210ABD0(ctx, base);
  if (s_log) {
    static uint32_t s_calls = 0, s_zero = 0;
    static auto s_last = std::chrono::steady_clock::now();
    ++s_calls;
    if ((ctx.r3.u32 & 0xFF) == 0) ++s_zero;
    if (std::chrono::steady_clock::now() - s_last > std::chrono::seconds(2)) {
      REXLOG_INFO("[spyro/drawdist] slot33 calls {} zero {}", s_calls, s_zero);
      s_calls = s_zero = 0;
      s_last = std::chrono::steady_clock::now();
    }
  }
  if (s_all) ctx.r3.u64 = 1;
}

// Research: sub_820EB910 = the animated-node walker (camp towers with waving flags, WORK_LOG
// 127): skips the node when +16 bit 2 is set. SPYRO_ANIMSHOW ignores that bit for the call;
// SPYRO_LOGANIM logs hidden nodes once with their position (node [+4] +128..+136).
extern "C" REX_EXTERN(__imp__sub_820EB910);
extern "C" REX_FUNC(sub_820EB910) {
  static const bool s_show = std::getenv("SPYRO_ANIMSHOW") != nullptr;
  static const bool s_log = std::getenv("SPYRO_LOGANIM") != nullptr;
  uint32_t node = ctx.r3.u32;
  uint32_t flags = spyro::GuestReadable(base, node, 0x14) ? Load32(base, node + 16) : 0;
  if (s_log && (flags & 4)) {
    static std::unordered_set<uint32_t> s_seen;
    uint32_t nd = Load32(base, node + 4);
    if (s_seen.size() < 200 && spyro::GuestReadable(base, nd, 0x8C) && s_seen.insert(node).second)
      REXLOG_INFO("[spyro/anim] hidden node 0x{:08X} flags 0x{:08X} at {:.1f} {:.1f} {:.1f}", node, flags,
                  LoadF(base, nd + 128), LoadF(base, nd + 132), LoadF(base, nd + 136));
  }
  // SPYRO_LOGANIM: also every visit near the camp's towers (~(-4, 16, -68)), every 2 s.
  if (s_log && spyro::GuestReadable(base, Load32(base, node + 4), 0x8C)) {
    uint32_t nd = Load32(base, node + 4);
    float dx = LoadF(base, nd + 128) + 4, dy = LoadF(base, nd + 132) - 16, dz = LoadF(base, nd + 136) + 68;
    static auto s_last = std::chrono::steady_clock::now();
    static std::unordered_set<uint32_t> s_now;
    if (dx * dx + dy * dy + dz * dz < 20 * 20) s_now.insert(node);
    if (std::chrono::steady_clock::now() - s_last > std::chrono::seconds(2)) {
      std::string s;
      for (uint32_t n : s_now) s += fmt::format(" 0x{:08X}/0x{:08X}", n, Load32(base, n + 16));
      REXLOG_INFO("[spyro/anim] visited near towers:{}", s);
      s_now.clear();
      s_last = std::chrono::steady_clock::now();
    }
  }
  if (s_show && (flags & 4)) {
    uint32_t w = __builtin_bswap32(flags & ~4u);
    std::memcpy(base + node + 16, &w, 4);
    __imp__sub_820EB910(ctx, base);
    uint32_t now = Load32(base, node + 16) | 4;
    w = __builtin_bswap32(now);
    std::memcpy(base + node + 16, &w, 4);
    return;
  }
  __imp__sub_820EB910(ctx, base);
}

// Research (env SPYRO_NODETYPES): every scene node class met in the walk (the children of the
// nodes the generic walkers sub_820FFD50 / sub_820ED200 visit: list at +24, next +8), once each,
// with its walk function (vt slot 21, +84) and render function (slot 32, +128) - node types with
// their own hiding logic not looked at yet.
// Research (env SPYRO_LOGNODES, for a player's flight): every scene node met in the walk is
// kept, and once a frame every change of its hidden bit (+16 bit 2) is logged with its class
// and position - whatever switches parts of the level off while Spyro moves away.
std::vector<uint32_t> g_watch_nodes;
std::unordered_map<uint32_t, uint32_t> g_watch_flags;
// [spyro recomp] 11-08: nodes met for the first time, flushed once a second by PollNodes - a
// burst (level load) is one summary line, a trickle (parts of the level created / streamed in
// while Spyro moves, which the hidden-bit log alone cannot see) is listed node by node.
std::vector<uint32_t> g_new_nodes;
uint32_t g_gone_nodes = 0;  // watched nodes freed since the last once-a-second line

namespace spyro {
bool CameraHeroAndEye(const uint8_t* base, float hero[3], float eye[3]);  // spyro_lockon.cpp
}

namespace {
void LogNodeTypes(const uint8_t* base, uint32_t node) {
  static const bool s_nodes = std::getenv("SPYRO_LOGNODES") != nullptr;
  if (s_nodes && spyro::GuestReadable(base, node, 0x20)) {
    for (uint32_t c = Load32(base, node + 24), n = 0; c && n < 4096 && spyro::GuestReadable(base, c, 0x20);
         c = Load32(base, c + 8), ++n)
      if (g_watch_nodes.size() < 60000 && g_watch_flags.emplace(c, Load32(base, c + 16)).second) {
        g_watch_nodes.push_back(c);
        g_new_nodes.push_back(c);
      }
  }
  static const bool s_on = std::getenv("SPYRO_NODETYPES") != nullptr;
  if (!s_on || !spyro::GuestReadable(base, node, 0x20)) return;
  static std::unordered_set<uint32_t> s_vts;
  for (uint32_t c = Load32(base, node + 24), n = 0; c && n < 4096 && spyro::GuestReadable(base, c, 0x20); c = Load32(base, c + 8), ++n) {
    uint32_t vt = Load32(base, c);
    if (s_vts.size() < 200 && spyro::GuestReadable(base, vt, 0x84) && s_vts.insert(vt).second) {
      uint32_t nd = Load32(base, c + 4);
      bool pos = spyro::GuestReadable(base, nd, 0x8C);
      REXLOG_INFO("[spyro/node] vt 0x{:08X} walk 0x{:08X} render 0x{:08X} e.g. 0x{:08X} flags 0x{:08X} at {:.0f} {:.0f} {:.0f}", vt,
                  Load32(base, vt + 84), Load32(base, vt + 128), c, Load32(base, c + 16), pos ? LoadF(base, nd + 128) : 0.0f,
                  pos ? LoadF(base, nd + 132) : 0.0f, pos ? LoadF(base, nd + 136) : 0.0f);
    }
  }
}
}  // namespace

// Sector roots (class CSGSectorRoot, vtable 0x82068670, 44 bytes; registered by sub_826C0AB8,
// built by sub_820EFAE8; +40 = id?): plain walker nodes (sub_820FFD50) over one area of the level.
// The user's theory test (11-02): areas switched off by where Spyro is, not by distance from the
// camera. All roots met in the walk are kept; SPYRO_LOGSECTORS logs every change of their
// hidden bit (+16 bit 2), SPYRO_SECTORSHOW walks hidden ones anyway.
constexpr uint32_t kSectorRootVtable = 0x82068670;
std::vector<uint32_t> g_sector_roots;  // game thread only
std::unordered_map<uint32_t, uint32_t> g_sector_flags;

extern "C" REX_EXTERN(__imp__sub_820FFD50);
extern "C" REX_FUNC(sub_820FFD50) {
  uint32_t node = ctx.r3.u32;
  LogNodeTypes(base, node);
  if (spyro::GuestReadable(base, node, 0x2C) && Load32(base, node) == kSectorRootVtable) {
    if (g_sector_flags.emplace(node, Load32(base, node + 16)).second) g_sector_roots.push_back(node);
    static const bool s_show = std::getenv("SPYRO_SECTORSHOW") != nullptr;
    uint32_t flags = Load32(base, node + 16);
    if (s_show && (flags & 4)) {
      uint32_t w = __builtin_bswap32(flags & ~4u);
      std::memcpy(base + node + 16, &w, 4);
      __imp__sub_820FFD50(ctx, base);
      w = __builtin_bswap32(Load32(base, node + 16) | 4u);
      std::memcpy(base + node + 16, &w, 4);
      return;
    }
  }
  __imp__sub_820FFD50(ctx, base);
}

namespace {
void PollNodes(const uint8_t* base) {
  static const bool s_nodes = std::getenv("SPYRO_LOGNODES") != nullptr;
  if (!s_nodes) return;
  static auto s_t0 = std::chrono::steady_clock::now();
  static uint32_t s_lines = 0;
  for (size_t i = 0; i < g_watch_nodes.size();) {
    uint32_t r = g_watch_nodes[i];
    uint32_t& last = g_watch_flags[r];
    // Nodes of an earlier scene are freed and the memory reused: no node vtable any more.
    uint32_t vt = spyro::GuestReadable(base, r, 0x20) ? Load32(base, r) : 0;
    if (vt < 0x82060000u || vt >= 0x82090000u) {
      ++g_gone_nodes;
      g_watch_flags.erase(r);
      g_watch_nodes[i] = g_watch_nodes.back();
      g_watch_nodes.pop_back();
      continue;
    }
    uint32_t f = Load32(base, r + 16);
    if (((f ^ last) & 4) && s_lines < 20000) {
      ++s_lines;
      double t = std::chrono::duration<double>(std::chrono::steady_clock::now() - s_t0).count();
      uint32_t nd = Load32(base, r + 4);
      bool pos = spyro::GuestReadable(base, nd, 0x8C);
      REXLOG_INFO("[spyro/node] t={:.1f}s 0x{:08X} vt 0x{:08X} {} at {:.0f} {:.0f} {:.0f} (flags 0x{:08X})", t, r, Load32(base, r),
                  (f & 4) ? "HIDDEN" : "shown", pos ? LoadF(base, nd + 128) : 0.0f, pos ? LoadF(base, nd + 132) : 0.0f,
                  pos ? LoadF(base, nd + 136) : 0.0f, f);
    }
    last = f;
    ++i;
  }
  // Once a second: where the camera follows (the hero), for matching the changes to the flight.
  static auto s_last = std::chrono::steady_clock::now();
  if (std::chrono::steady_clock::now() - s_last > std::chrono::seconds(1)) {
    s_last = std::chrono::steady_clock::now();
    double t = std::chrono::duration<double>(s_last - s_t0).count();
    // [spyro recomp] 11-08: hero + eye from the camera service (the old renderer-camera chain
    // [[[0x82835934]+44]+56]+4 was unreadable on the 11-08 flight), and the Draw distance setting
    // (that flight was on Original by mistake).
    float hero[3] = {}, eye[3] = {};
    bool cam = spyro::CameraHeroAndEye(base, hero, eye);
    REXLOG_INFO("[spyro/node] t={:.1f}s hero at {:.0f} {:.0f} {:.0f} eye {:.0f} {:.0f} {:.0f}{}, {} nodes watched ({} new, {} gone), draw distance {}",
                t, hero[0], hero[1], hero[2], eye[0], eye[1], eye[2], cam ? "" : " (no camera)", g_watch_nodes.size(),
                g_new_nodes.size(), g_gone_nodes, spyro::GetSetting(spyro::kDrawDistance) == 1 ? "MAX" : "Original");
    g_gone_nodes = 0;
    // New nodes since the last second: more than 300 = a scene being built (one line); fewer are
    // listed with class and position.
    if (!g_new_nodes.empty()) {
      static uint32_t s_new_lines = 0;
      if (g_new_nodes.size() > 300) {
        REXLOG_INFO("[spyro/node] t={:.1f}s {} nodes created at once (scene load)", t, g_new_nodes.size());
      } else {
        for (uint32_t c : g_new_nodes) {
          if (s_new_lines >= 20000 || !spyro::GuestReadable(base, c, 0x20)) continue;
          ++s_new_lines;
          uint32_t nd = Load32(base, c + 4);
          bool pos = spyro::GuestReadable(base, nd, 0x8C);
          REXLOG_INFO("[spyro/node] t={:.1f}s 0x{:08X} vt 0x{:08X} NEW at {:.0f} {:.0f} {:.0f} (flags 0x{:08X})", t, c, Load32(base, c),
                      pos ? LoadF(base, nd + 128) : 0.0f, pos ? LoadF(base, nd + 132) : 0.0f, pos ? LoadF(base, nd + 136) : 0.0f,
                      Load32(base, c + 16));
        }
      }
      g_new_nodes.clear();
    }
  }
}

// Once a frame (render setup): sector roots whose hidden bit changed.
void PollSectors(const uint8_t* base) {
  PollNodes(base);
  static const bool s_log = std::getenv("SPYRO_LOGSECTORS") != nullptr;
  if (!s_log) return;
  static auto s_t0 = std::chrono::steady_clock::now();
  static size_t s_known = 0;
  if (g_sector_roots.size() != s_known) {
    s_known = g_sector_roots.size();
    size_t hidden = 0;
    for (uint32_t r : g_sector_roots) hidden += (Load32(base, r + 16) & 4) ? 1 : 0;
    REXLOG_INFO("[spyro/sector] {} sector roots known, {} hidden", s_known, hidden);
  }
  // Roots of an earlier scene (menu backdrop, the level before) are freed and reused: dropped.
  std::erase_if(g_sector_roots, [&](uint32_t r) {
    bool gone = !spyro::GuestReadable(base, r, 0x2C) || Load32(base, r) != kSectorRootVtable;
    if (gone) g_sector_flags.erase(r);
    return gone;
  });
  for (uint32_t r : g_sector_roots) {
    uint32_t f = Load32(base, r + 16), &last = g_sector_flags[r];
    if ((f ^ last) & 4) {
      double t = std::chrono::duration<double>(std::chrono::steady_clock::now() - s_t0).count();
      REXLOG_INFO("[spyro/sector] t={:.1f}s root 0x{:08X} id {} {} (flags 0x{:08X}, children first 0x{:08X})", t, r,
                  int32_t(Load32(base, r + 40)), (f & 4) ? "HIDDEN" : "shown", f, Load32(base, r + 24));
    }
    last = f;
  }
}
}  // namespace

extern "C" REX_EXTERN(__imp__sub_820ED200);
extern "C" REX_FUNC(sub_820ED200) {
  LogNodeTypes(base, ctx.r3.u32);
  __imp__sub_820ED200(ctx, base);
}

// Research (env SPYRO_LOGPUSH): sub_820DDEE0(array r3, &item r4) pushes onto a render bucket
// (array = bucket +4, bucket classes 0x82066F90..0x82067054): every item once, 10 s into the
// level, with its class and the caller's host return address (who adds it).
extern "C" REX_EXTERN(__imp__sub_820DDEE0);
extern "C" REX_FUNC(sub_820DDEE0) {
  static const bool s_on = std::getenv("SPYRO_LOGPUSH") != nullptr;
  if (s_on && spyro::CurrentMenuId() == 0x24 && ctx.r3.u32 >= 0x1004 && spyro::GuestReadable(base, ctx.r4.u32, 4)) {
    static uint32_t s_calls = 0;
    static std::unordered_set<uint32_t> s_seen;
    uint32_t owner_vt = Load32(base, ctx.r3.u32 - 4), item = Load32(base, ctx.r4.u32);
    if (owner_vt >= 0x82066F90u && owner_vt <= 0x82067054u && ++s_calls > 300000 && s_seen.size() < 10000 &&
        s_seen.insert(item).second) {
      uint32_t vt = spyro::GuestReadable(base, item, 4) ? Load32(base, item) : 0;
      void* frames[4];
      USHORT n = RtlCaptureStackBackTrace(1, 4, frames, nullptr);
      uintptr_t exe = reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr));
      std::string s;
      for (USHORT i = 0; i < n; ++i) s += fmt::format(" {:X}", reinterpret_cast<uintptr_t>(frames[i]) - exe);
      REXLOG_INFO("[spyro/push] item 0x{:08X} vt 0x{:08X} bucket vt 0x{:08X} |{}", item, vt, owner_vt, s);
    }
  }
  __imp__sub_820DDEE0(ctx, base);
}

// Research (env SPYRO_LOGDRAWN): sub_82109720(obj) draws one object out of a render bucket; every
// object drawn, once, from 10 s into the level, with its position ([obj+28] +44..+52).
extern "C" REX_EXTERN(__imp__sub_82109720);
extern "C" REX_FUNC(sub_82109720) {
  static const bool s_log = std::getenv("SPYRO_LOGDRAWN") != nullptr;
  // During a plume draw-log frame (SPYRO_PLUMEDRAWS_AT) every object before its draws, so the
  // draw log shows which draws belong to which object.
  if (spyro::plume_renderer::LoggingFrame() && spyro::GuestReadable(base, ctx.r3.u32, 0x30)) {
    uint32_t obj = ctx.r3.u32, tf = Load32(base, obj + 28);
    if (spyro::GuestReadable(base, tf, 0x38))
      REXLOG_INFO("[spyro/objdraw] 0x{:08X} at {:.1f} {:.1f} {:.1f} alpha {}", obj, LoadF(base, tf + 44), LoadF(base, tf + 48),
                  LoadF(base, tf + 52), base[obj + 35]);
  }
  if (s_log && spyro::CurrentMenuId() == 0x24 && spyro::GuestReadable(base, ctx.r3.u32, 0x30)) {
    static uint32_t s_calls = 0;
    static std::unordered_set<uint32_t> s_seen;
    uint32_t obj = ctx.r3.u32, tf = Load32(base, obj + 28);
    if (++s_calls > 600000 && s_seen.size() < 8000 && spyro::GuestReadable(base, tf, 0x38) && s_seen.insert(obj).second)
      REXLOG_INFO("[spyro/obj] 0x{:08X} vt 0x{:08X} at {:.1f} {:.1f} {:.1f} r7 0 | drawn", obj, Load32(base, obj),
                  LoadF(base, tf + 44), LoadF(base, tf + 48), LoadF(base, tf + 52));
  }
  __imp__sub_82109720(ctx, base);
}

// Research (env SPYRO_LOGANIM): sub_820EB9C0(node, ..., r8 = frustum) returns the animated
// node's frustum result; every 2 s the results for nodes near the camp's towers, and the
// frustum's 6 planes (+148, 20 bytes each: normal x y z, d, ?).
extern "C" REX_EXTERN(__imp__sub_820EB9C0);
extern "C" REX_FUNC(sub_820EB9C0) {
  FarCaller far_caller(2);
  static const bool s_log = std::getenv("SPYRO_LOGANIM") != nullptr;
  uint32_t node = ctx.r3.u32, frustum = ctx.r8.u32;
  __imp__sub_820EB9C0(ctx, base);
  if (!s_log || !spyro::GuestReadable(base, node, 8) || !spyro::GuestReadable(base, Load32(base, node + 4), 0x8C)) return;
  uint32_t nd = Load32(base, node + 4);
  float x = LoadF(base, nd + 128), y = LoadF(base, nd + 132), z = LoadF(base, nd + 136);
  float dx = x, dy = y - 20, dz = z + 60;  // the whole camp: within 40 of (0, 20, -60)
  static auto s_last = std::chrono::steady_clock::now();
  static std::string s_res;
  static std::unordered_set<uint32_t> s_once;
  if (dx * dx + dy * dy + dz * dz < 40 * 40 && s_res.size() < 1500 && s_once.insert(node).second)
    s_res += fmt::format(" [{:08X} {:.0f},{:.0f},{:.0f} f{:08X}={}]", node, x, y, z, Load32(base, node + 16), ctx.r3.u32 & 0xFF);
  if (std::chrono::steady_clock::now() - s_last > std::chrono::seconds(2)) {
    REXLOG_INFO("[spyro/anim] frustum results near towers:{}", s_res);
    if (spyro::GuestReadable(base, frustum, 148 + 6 * 20)) {
      std::string p;
      for (int i = 0; i < 6; ++i) {
        uint32_t a = frustum + 148 + i * 20;
        p += fmt::format(" [{:.3f} {:.3f} {:.3f} {:.1f} {:.1f}]", LoadF(base, a), LoadF(base, a + 4), LoadF(base, a + 8),
                         LoadF(base, a + 12), LoadF(base, a + 16));
      }
      REXLOG_INFO("[spyro/anim] frustum 0x{:08X} (global 0x{:08X}) planes{}", frustum, Load32(base, 0x8283553C), p);
    }
    s_res.clear();
    s_once.clear();
    s_last = std::chrono::steady_clock::now();
  }
}

// Research: sub_823B1D20(r3 = view) builds the view frustum's planes (+148) from the view's
// matrix ([+4] +80) and +104/+108 (fov tangents) / +120/+124 (near / far?). SPYRO_LOGVIEW logs
// +96..+140 every 5 s; SPYRO_VIEWFAR=<units> sets +124 for the call (restored after).
extern "C" REX_EXTERN(__imp__sub_823B1D20);
extern "C" REX_FUNC(sub_823B1D20) {
  static const bool s_log = std::getenv("SPYRO_LOGVIEW") != nullptr;
  static const float s_far = std::getenv("SPYRO_VIEWFAR") ? float(std::atof(std::getenv("SPYRO_VIEWFAR"))) : 0.0f;
  uint32_t view = ctx.r3.u32;
  bool ok = spyro::GuestReadable(base, view, 0x150);
  if (s_log && ok) {
    static auto s_last = std::chrono::steady_clock::now();
    if (std::chrono::steady_clock::now() - s_last > std::chrono::seconds(5)) {
      s_last = std::chrono::steady_clock::now();
      std::string w;
      for (uint32_t k = 96; k <= 140; k += 4) w += fmt::format(" +{}={:g}", k, LoadF(base, view + k));
      REXLOG_INFO("[spyro/view] 0x{:08X} (frustum global 0x{:08X}):{}", view, Load32(base, 0x8283553C), w);
    }
  }
  float saved = 0;
  if (s_far > 0 && ok) {
    saved = LoadF(base, view + 124);
    StoreF(base, view + 124, s_far);
  }
  __imp__sub_823B1D20(ctx, base);
  if (s_far > 0 && ok) StoreF(base, view + 124, saved);
}

// Research (env SPYRO_LOGANIM): sub_820EC4F8 (node, ..., r6 = frustum) hands a node's objects to
// the per-object check; every 2 s its flags and result for nodes near the save point
// (~(-2, 16, -53)).
extern "C" REX_EXTERN(__imp__sub_820EC4F8);
extern "C" REX_FUNC(sub_820EC4F8) {
  FarCaller far_caller(4);
  static const bool s_log = std::getenv("SPYRO_LOGANIM") != nullptr;
  uint32_t node = ctx.r3.u32;
  uint32_t flags = s_log && spyro::GuestReadable(base, node, 0x24) ? Load32(base, node + 16) : 0;
  __imp__sub_820EC4F8(ctx, base);
  if (!s_log || !spyro::GuestReadable(base, node, 0x24) || !spyro::GuestReadable(base, Load32(base, node + 4), 0x8C)) return;
  uint32_t nd = Load32(base, node + 4);
  float dx = LoadF(base, nd + 128) + 2, dy = LoadF(base, nd + 132) - 16, dz = LoadF(base, nd + 136) + 53;
  static auto s_last = std::chrono::steady_clock::now();
  static std::string s_res;
  if (dx * dx + dy * dy + dz * dz < 12 * 12 && s_res.size() < 700)
    s_res += fmt::format(" {:08X}/{:08X}={} d2 {:.0f}", node, flags, ctx.r3.u32 & 0xFF, LoadF(base, node + 32));
  if (std::chrono::steady_clock::now() - s_last > std::chrono::seconds(2)) {
    REXLOG_INFO("[spyro/anim] EC4F8 near save point:{}", s_res);
    s_res.clear();
    s_last = std::chrono::steady_clock::now();
  }
}

// The FOURTH mechanism (the camp's towers, huts and the trees around it still vanished from
// ~150 units with the three above off - user flight screenshots, fixed research camera):
// sub_823B2FA8(frustum r3, sphere r4 = centre + radius) is the view-frustum test of the group
// check (sub_820F0CF0) and the animated-node walk (sub_820EB9C0, the towers with their waving
// flags). The frustum ([0x8283553C], 6 planes at +148, 20 bytes: normal x y z, d) has its FAR
// plane first, at 150 units from the eye in Avalar (plane 1 = the near plane, opposite normal;
// a sphere is outside plane 0 when normal.centre - d > radius) - while the picture itself is
// clipped at ~1500. Max: plane 0 is moved 10000 units further out for the call and put back.
// Research: SPYRO_NOFRUSTUM passes everything (also what is behind the camera).
extern "C" REX_EXTERN(__imp__sub_823B2FA8);
extern "C" REX_FUNC(sub_823B2FA8) {
  static const bool s_all = std::getenv("SPYRO_NOFRUSTUM") != nullptr;
  static const bool s_toggle = std::getenv("SPYRO_DISTSCALE_TOGGLE") != nullptr;
  bool max = s_toggle ? (g_frame_parity & 1) != 0 : spyro::GetSetting(spyro::kDrawDistance) == 1;
  // The near / far pair is the one pair of planes with opposite normals (the 4 side planes meet
  // at the eye); both are opened, so only the side planes decide - the forward view pyramid,
  // nothing behind the camera.
  uint32_t planes = ctx.r3.u32 + 148, pair[2] = {0, 0};
  float saved[2] = {0, 0};
  bool moved = false;
  static const uint32_t s_mask = std::getenv("SPYRO_FARMASK") ? uint32_t(std::atoi(std::getenv("SPYRO_FARMASK"))) : 15u;
  if (max && (s_mask & g_far_caller) && spyro::GuestReadable(base, planes, 6 * 20)) {
    // Which two planes are the pair is the same every frame (0 and 1 in Avalar): found once per
    // frustum and checked with one dot product after that (thousands of tests a frame).
    static uint32_t s_frustum = 0, s_i = 0, s_j = 0;
    auto opposite = [&](uint32_t i, uint32_t j) {
      uint32_t a = planes + i * 20, b = planes + j * 20;
      return LoadF(base, a) * LoadF(base, b) + LoadF(base, a + 4) * LoadF(base, b + 4) + LoadF(base, a + 8) * LoadF(base, b + 8) <
             -0.99f;
    };
    if (s_frustum == ctx.r3.u32 && opposite(s_i, s_j)) {
      moved = true;
    } else {
      for (uint32_t i = 0; i < 6 && !moved; ++i)
        for (uint32_t j = i + 1; j < 6 && !moved; ++j)
          if (opposite(i, j)) {
            s_frustum = ctx.r3.u32, s_i = i, s_j = j;
            moved = true;
          }
    }
    if (moved) pair[0] = planes + s_i * 20 + 12, pair[1] = planes + s_j * 20 + 12;
    for (int k = 0; k < 2 && moved; ++k) {
      saved[k] = LoadF(base, pair[k]);
      StoreF(base, pair[k], saved[k] + 10000.0f);
    }
  }
  __imp__sub_823B2FA8(ctx, base);
  for (int k = 0; k < 2 && moved; ++k) StoreF(base, pair[k], saved[k]);
  if (s_all) ctx.r3.u64 = 1;
}

// Research (env SPYRO_ADDSTACKS): render bucket "add" (vt slot 3 of every bucket class:
// push_back on its array +4). Each distinct host stack that adds something, logged once as exe
// RVAs (named with spyrodotd.map), plus what was added - the code that decides what is drawn.
extern "C" REX_EXTERN(__imp__sub_820DCA38);
extern "C" REX_FUNC(sub_820DCA38) {
  static const bool s_on = std::getenv("SPYRO_ADDSTACKS") != nullptr;
  if (s_on) {
    static std::unordered_set<uint64_t> s_seen;
    void* frames[40];
    USHORT n = RtlCaptureStackBackTrace(1, 40, frames, nullptr);
    uint64_t h = 1469598103934665603ull;
    for (USHORT i = 0; i < n; ++i) h = (h ^ reinterpret_cast<uintptr_t>(frames[i])) * 1099511628211ull;
    if (s_seen.size() < 300 && s_seen.insert(h).second) {
      uintptr_t exe = reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr));
      std::string s;
      for (USHORT i = 0; i < n; ++i) s += fmt::format(" {:X}", reinterpret_cast<uintptr_t>(frames[i]) - exe);
      uint32_t item = ctx.r4.u32;
      uint32_t vt = spyro::GuestReadable(base, item, 4) ? Load32(base, item) : 0;
      REXLOG_INFO("[plume/stack] add stack {} item 0x{:08X} vt 0x{:08X} |{}", s_seen.size(), item, vt, s);
    }
  }
  __imp__sub_820DCA38(ctx, base);
}

// Research (env SPYRO_ADDSTACKS): the array push behind it (sub_820DDE88, r3 = array, r4 = item),
// only when the array is a render bucket's (+4 of an object whose vtable is a bucket class
// 0x82066F90..0x82067054): the per-frame filling does not go through the bucket's vt slot 3.
extern "C" REX_EXTERN(__imp__sub_820DDE88);
extern "C" REX_FUNC(sub_820DDE88) {
  static const bool s_on = std::getenv("SPYRO_ADDSTACKS") != nullptr;
  if (s_on && ctx.r3.u32 >= 0x1004) {
    uint32_t owner_vt = Load32(base, ctx.r3.u32 - 4);
    if (owner_vt >= 0x82066F90u && owner_vt <= 0x82067054u) {
      static std::unordered_set<uint64_t> s_seen;
      void* frames[40];
      USHORT n = RtlCaptureStackBackTrace(1, 40, frames, nullptr);
      uint64_t h = 1469598103934665603ull;
      for (USHORT i = 0; i < n; ++i) h = (h ^ reinterpret_cast<uintptr_t>(frames[i])) * 1099511628211ull;
      if (s_seen.size() < 300 && s_seen.insert(h).second) {
        uintptr_t exe = reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr));
        std::string s;
        for (USHORT i = 0; i < n; ++i) s += fmt::format(" {:X}", reinterpret_cast<uintptr_t>(frames[i]) - exe);
        uint32_t item = ctx.r4.u32;
        uint32_t vt = spyro::GuestReadable(base, item, 4) ? Load32(base, item) : 0;
        REXLOG_INFO("[plume/stack] push stack {} bucket vt 0x{:08X} item 0x{:08X} vt 0x{:08X} |{}", s_seen.size(),
                    owner_vt, item, vt, s);
      }
    }
  }
  __imp__sub_820DDE88(ctx, base);
}

// Research (env SPYRO_WATCHBUCKET=<bucket 0..10>): a hardware write watchpoint (x64 debug
// register DR0) on that render bucket's count (+12) on the game thread; every distinct host stack
// that writes it is logged ([plume/stack] watch ...). The buckets are emptied by their draw
// routine (sub_820DCA40 & co.) and refilled every frame by code that writes the entries inline -
// that code decides what is drawn.
#if defined(_WIN32)
namespace {
uintptr_t g_watch_host = 0;
LONG CALLBACK WatchHandler(EXCEPTION_POINTERS* info) {
  if (info->ExceptionRecord->ExceptionCode != EXCEPTION_SINGLE_STEP || !(info->ContextRecord->Dr6 & 1))
    return EXCEPTION_CONTINUE_SEARCH;
  info->ContextRecord->Dr6 = 0;
  static std::unordered_set<uint64_t> s_seen;
  if (s_seen.size() < 120) {
    // Walk from the interrupted code (the instruction after the write).
    CONTEXT u = *info->ContextRecord;
    uintptr_t exe = reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr));
    uintptr_t frames[32];
    int n = 0;
    for (int depth = 0; depth < 64 && n < 32 && u.Rip; ++depth) {
      frames[n++] = u.Rip;
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
    }
    uint64_t h = 1469598103934665603ull;
    for (int i = 0; i < n; ++i) h = (h ^ frames[i]) * 1099511628211ull;
    if (s_seen.insert(h).second) {
      uint32_t count;
      std::memcpy(&count, reinterpret_cast<void*>(g_watch_host), 4);
      std::string s;
      for (int i = 0; i < n; ++i) s += fmt::format(" {:X}", frames[i] - exe);
      REXLOG_INFO("[plume/stack] watch {} count now {} |{}", s_seen.size(), __builtin_bswap32(count), s);
    }
  }
  return EXCEPTION_CONTINUE_EXECUTION;
}

// Sets DR0 (write, 4 bytes) on thread `tid` from a helper thread (a thread can't safely change
// its own debug registers while running).
void ArmWatch(DWORD tid, uintptr_t host) {
  g_watch_host = host;
  AddVectoredExceptionHandler(1, WatchHandler);
  std::thread([tid, host] {
    HANDLE t = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_SET_CONTEXT, FALSE, tid);
    if (!t) return;
    SuspendThread(t);
    CONTEXT c{};
    c.ContextFlags = CONTEXT_DEBUG_REGISTERS;
    GetThreadContext(t, &c);
    c.Dr0 = host;
    c.Dr7 = (c.Dr7 & ~0xF0003ull) | 1ull | (1ull << 16) | (3ull << 18);  // L0, write, 4 bytes
    SetThreadContext(t, &c);
    ResumeThread(t);
    CloseHandle(t);
    REXLOG_INFO("[spyro/drawdist] write watchpoint armed at host 0x{:X} on thread {}", host, tid);
  }).detach();
}
}  // namespace
#endif

extern "C" REX_FUNC(sub_820DA658) {
  uint32_t scene = ctx.r3.u32;
  ++g_frame_parity;
  __imp__sub_820DA658(ctx, base);
  PollSectors(base);
  // Research (env SPYRO_LOGBUCKETS): the scene's 11 render buckets (+80..+120; sub_820DB210 asks
  // each vt+8 "empty?" and calls vt+24 to draw it), once a level is up.
#if defined(_WIN32)
  static const int s_watch = std::getenv("SPYRO_WATCHBUCKET") ? std::atoi(std::getenv("SPYRO_WATCHBUCKET")) : -1;
  static bool s_armed = false;
  if (s_watch >= 0 && s_watch < 11 && !s_armed && spyro::CurrentMenuId() == 0x24) {
    s_armed = true;
    uint32_t b = Load32(base, scene + 80 + 4 * s_watch);
    if (spyro::GuestReadable(base, b, 0x10)) ArmWatch(GetCurrentThreadId(), reinterpret_cast<uintptr_t>(base + b + 12));
  }
  // Research (env SPYRO_WATCHADDR=<guest hex address>): the same watchpoint on any guest word,
  // armed 10 s into the level (a tree object's +12, the distance value of the LOD hunt).
  static const uint32_t s_watch_addr =
      std::getenv("SPYRO_WATCHADDR") ? uint32_t(std::strtoul(std::getenv("SPYRO_WATCHADDR"), nullptr, 16)) : 0;
  static uint32_t s_level_frames = 0;
  s_level_frames = spyro::CurrentMenuId() == 0x24 ? s_level_frames + 1 : 0;
  if (s_watch_addr && !s_armed && s_level_frames > 600 && spyro::GuestReadable(base, s_watch_addr, 4)) {
    s_armed = true;
    ArmWatch(GetCurrentThreadId(), reinterpret_cast<uintptr_t>(base + s_watch_addr));
  }
#endif
  // Research (env SPYRO_FINDFOG): guest addresses holding the fog constant (start 5, end 200,
  // density 1 in Avalar - vertex constant c0 "u_f4Fog"), scanned once 10 s into the level.
  static const bool s_find_fog = std::getenv("SPYRO_FINDFOG") != nullptr;
  if (s_find_fog && s_level_frames == 600) {
    const uint32_t want[3] = {__builtin_bswap32(0x40A00000u), __builtin_bswap32(0x43480000u), __builtin_bswap32(0x3F800000u)};
    int found = 0;
    for (uint32_t a = 0x40000000u; a < 0x60000000u && found < 40; a += 0x10000) {
      if (!spyro::GuestReadable(base, a, 0x10000)) continue;
      for (uint32_t o = 0; o + 12 <= 0x10000; o += 4)
        if (std::memcmp(base + a + o, want, 12) == 0 && found++ < 40)
          REXLOG_INFO("[spyro/fog] 5/200/1 at 0x{:08X} (+12: {:g})", a + o, LoadF(base, a + o + 12));
    }
    REXLOG_INFO("[spyro/fog] scan done: {} found", found);
  }
  static const bool s_buckets = std::getenv("SPYRO_LOGBUCKETS") != nullptr;
  static bool s_logged_buckets = false;
  if (s_buckets && !s_logged_buckets && spyro::CurrentMenuId() == 0x24) {
    s_logged_buckets = true;
    for (uint32_t i = 0; i < 11; ++i) {
      uint32_t b = Load32(base, scene + 80 + 4 * i);
      uint32_t vt = spyro::GuestReadable(base, b, 0x40) ? Load32(base, b) : 0;
      std::string words;
      for (uint32_t k = 0; k < 16 && spyro::GuestReadable(base, b, 0x40); ++k) words += fmt::format(" {:08X}", Load32(base, b + 4 * k));
      REXLOG_INFO("[spyro/drawdist] scene 0x{:08X} bucket {} 0x{:08X} vt 0x{:08X} |{}", scene, i, b, vt, words);
    }
  }
  float k = FarScale();
  if (k == 1.0f) return;
  uint32_t holder = Load32(base, scene + 44);
  uint32_t cam = spyro::GuestReadable(base, holder, 0x34) ? Load32(base, holder + 48) : 0;
  if (!spyro::GuestReadable(base, cam, 0x88)) return;
  float near_clip = LoadF(base, cam + kCamNear), far_clip = LoadF(base, cam + kCamFar);
  {
    static uint32_t s_last_cam = 0;
    static float s_last_far = -1;
    static int s_logged = 0;
    if ((cam != s_last_cam || far_clip != s_last_far) && s_logged++ < 40) {
      s_last_cam = cam;
      s_last_far = far_clip;
      REXLOG_INFO("[spyro/drawdist] scene 0x{:08X} render camera 0x{:08X}: near {:g} far {:g} (x{:g})", scene, cam,
                  near_clip, far_clip, k);
    }
  }
  if (!(far_clip > near_clip && far_clip < 1e7f)) return;
  StoreF(base, cam + kCamFar, far_clip * k);
  Call(ctx, base, sub_823B1C98, cam);
  uint32_t node = Load32(base, cam + 4);
  if (node) Call(ctx, base, sub_823B3ED0, node);
}

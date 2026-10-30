// Lock-on camera (WORK_LOG 123): R3 (right stick click) locks onto the nearest awake enemy -
// the Golem boss first - and from then on the camera swings around the hero to look past it at
// the target (written over the game's camera result each frame; feeding the right stick does
// not work - most areas use guided rail cameras that ignore it). R3 again, the target dying /
// going away, or no enemy in range ends it. No target switching (the D-pad selects powers).
//
// What the game objects hold (found with the SPYRO_LOCKDUMP dumps below):
//   CKSrvCamera (camera service): +0x484 position the camera follows (the hero being played),
//     +0x550 eye, +0x55C look-at point.
//   CKHkS08Enemy (pooled; dormant ones keep their spawn point): +0x134 position. Its vtable slot 30
//     (sub_82203C20) runs every frame only while the enemy is awake - that is how "awake" is told.
//   CKHkGolem: same base class (CKHkMovableCharacter), slot 30 = sub_823851E8.
// Y is up.

#include <rex/logging.h>
#include <rex/ppc.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#endif

#include "guest_memory.h"
#include "spyro_settings.h"

extern "C" {
REX_EXTERN(__imp__sub_82236880);  // CKHkS08Hero init
REX_EXTERN(__imp__sub_821F4550);  // CKHkS08Enemy ctor (vtable 0x82074ED0)
REX_EXTERN(__imp__sub_82159458);  // CKSrvCamera ctor (vtable 0x8206DAD0)
REX_EXTERN(__imp__sub_82383B80);  // CKHkGolem ctor (vtable 0x820814A8)
REX_EXTERN(__imp__sub_82203C20);  // CKHkS08Enemy vtable slot 30 (per-frame while awake)
REX_EXTERN(__imp__sub_823851E8);  // CKHkGolem vtable slot 30
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

struct Vec3 {
  float x, y, z;
};

Vec3 LoadVec(const uint8_t* base, uint32_t a) { return {LoadF(base, a), LoadF(base, a + 4), LoadF(base, a + 8)}; }

bool Sane(const Vec3& v) {
  return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z) && std::fabs(v.x) < 1e5f &&
         std::fabs(v.y) < 1e5f && std::fabs(v.z) < 1e5f && (v.x != 0 || v.y != 0 || v.z != 0);
}

float Dist(const Vec3& a, const Vec3& b) {
  float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
  return std::sqrt(dx * dx + dy * dy + dz * dz);
}

constexpr uint32_t kEnemyVtable = 0x82074ED0, kGolemVtable = 0x820814A8, kCameraSrvVtable = 0x8206DAD0;
constexpr uint32_t kTargetPos = 0x134;
constexpr uint32_t kEnemyInFight = 0x6A4;  // -1 while spawning in (lockrec.bin, WORK_LOG 123)
constexpr uint32_t kCamFollow = 0x484, kCamEye = 0x550, kCamLook = 0x55C;
constexpr uint16_t kButtonR3 = 0x0080;
constexpr float kPickRange = 60.0f;     // lock only onto enemies this close
constexpr float kReleaseRange = 100.0f;  // and let go beyond this
constexpr int kAwakeFrames = 30;        // awake = slot 30 ran within this many game frames
constexpr int kLostFrames = 90;         // release after the target slept this long

std::mutex g_mutex;
std::vector<uint32_t> g_heroes, g_enemies, g_cameras;
std::unordered_map<uint32_t, uint32_t> g_last_update;  // enemy / golem -> game frame of its last slot-30 call
std::atomic<uint32_t> g_frame{0};

void Remember(std::vector<uint32_t>& list, uint32_t obj, const char* what, uint32_t lr) {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (std::find(list.begin(), list.end(), obj) != list.end()) return;
  list.push_back(obj);
  if (list.size() <= 40) REXLOG_INFO("[spyro/lockon] {} object 0x{:08X} (from LR 0x{:08X}, {} known)", what, obj, lr, list.size());
}

void Touch(uint32_t obj) {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (auto it = g_last_update.find(obj); it != g_last_update.end()) it->second = g_frame.load();
}

// Lock state (game thread).
uint32_t g_target = 0;
bool g_r3_was_down = false;

bool Awake(uint32_t obj, int frames) {
  auto it = g_last_update.find(obj);
  return it != g_last_update.end() && it->second != 0 && g_frame.load() - it->second <= uint32_t(frames);
}

uint32_t Camera(const uint8_t* base) {
  for (auto it = g_cameras.rbegin(); it != g_cameras.rend(); ++it)
    if (Load32(base, *it) == kCameraSrvVtable) return *it;
  return 0;
}

// Nearest awake enemy in range of the followed hero; the Golem wins over everything.
uint32_t PickTarget(const uint8_t* base, const Vec3& hero) {
  static const bool s_any = std::getenv("SPYRO_LOCKANY") != nullptr;  // test: dormant enemies too
  uint32_t best = 0;
  float best_d = kPickRange;
  bool best_boss = false;
  for (auto& [obj, frame] : g_last_update) {
    uint32_t vt = Load32(base, obj);
    if (vt != kEnemyVtable && vt != kGolemVtable) continue;
    if (!s_any && !Awake(obj, kAwakeFrames)) continue;
    // Still burrowing in / dropping into the arena: +0x6A4 stays -1 until the enemy has joined
    // the fight (~2 s after it starts updating; +0x75C bit 0x10000 is set in the same frame).
    if (!s_any && vt == kEnemyVtable && Load32(base, obj + kEnemyInFight) == 0xFFFFFFFFu) continue;
    Vec3 p = LoadVec(base, obj + kTargetPos);
    if (!Sane(p)) continue;
    float d = Dist(p, hero);
    bool boss = vt == kGolemVtable;
    if ((boss && !best_boss) || (boss == best_boss && d < best_d)) {
      best = obj;
      best_d = d;
      best_boss = boss;
    }
  }
  return best;
}

// Debug dump (env SPYRO_LOCKDUMP=<s>,<s>,...: seconds after gameplay starts): the object and,
// for every word in it that points to readable guest memory, 0x200 bytes there.
// File: lockdump\<tag>.bin = records {u32 addr, u32 size, bytes}.
bool Readable(const uint8_t* base, uint32_t a, uint32_t size) { return spyro::GuestReadable(base, a, size); }

void DumpObject(const uint8_t* base, uint32_t obj, uint32_t size, const std::string& tag) {
  std::error_code ec;
  std::filesystem::create_directories("lockdump", ec);
  FILE* f = std::fopen(("lockdump/" + tag + ".bin").c_str(), "wb");
  if (!f) return;
  auto record = [&](uint32_t a, uint32_t n) {
    if (!Readable(base, a, n)) return;
    std::fwrite(&a, 4, 1, f);
    std::fwrite(&n, 4, 1, f);
    std::fwrite(base + a, 1, n, f);
  };
  record(obj, size);
  if (Readable(base, obj, size))
    for (uint32_t off = 0; off < size; off += 4) {
      uint32_t p = Load32(base, obj + off);
      if (p >= 0x40000000u && p < 0x80000000u && p != obj) record(p, 0x200);
    }
  std::fclose(f);
}

void DebugDumps(const uint8_t* base) {
  static const std::vector<double> s_times = [] {
    std::vector<double> t;
    if (const char* v = std::getenv("SPYRO_LOCKDUMP"))
      for (const char* p = v; *p;) {
        t.push_back(std::atof(p));
        while (*p && *p != ',') ++p;
        if (*p) ++p;
      }
    return t;
  }();
  if (s_times.empty()) return;
  static bool s_started = false;
  static std::chrono::steady_clock::time_point s_t0;
  std::lock_guard<std::mutex> lock(g_mutex);
  if (!s_started) {
    if (spyro::CurrentMenuId() != 0x24 || g_heroes.empty()) return;  // first gameplay frame
    s_started = true;
    s_t0 = std::chrono::steady_clock::now();
  }
  static size_t s_next = 0;
  double t = std::chrono::duration<double>(std::chrono::steady_clock::now() - s_t0).count();
  if (s_next >= s_times.size() || t < s_times[s_next]) return;
  std::string n = std::to_string(s_next);
  for (size_t i = 0; i < g_heroes.size(); ++i) DumpObject(base, g_heroes[i], 0x800, "hero" + std::to_string(i) + "_t" + n);
  for (size_t i = 0; i < g_cameras.size(); ++i)
    if (Load32(base, g_cameras[i]) == kCameraSrvVtable) DumpObject(base, g_cameras[i], 0x600, "cam" + std::to_string(i) + "_t" + n);
  int dumped = 0;
  for (auto& [obj, frame] : g_last_update)
    if (Awake(obj, kAwakeFrames) && dumped < 3) DumpObject(base, obj, 0x800, "enemy" + std::to_string(dumped++) + "_t" + n);
  REXLOG_INFO("[spyro/lockon] dump {} at {:.1f} s", s_next, t);
  ++s_next;
}

}  // namespace

extern "C" {

REX_FUNC(sub_82236880) {
  uint32_t obj = ctx.r3.u32, lr = uint32_t(ctx.lr);
  __imp__sub_82236880(ctx, base);
  Remember(g_heroes, obj, "hero", lr);
}

REX_FUNC(sub_821F4550) {
  uint32_t obj = ctx.r3.u32, lr = uint32_t(ctx.lr);
  __imp__sub_821F4550(ctx, base);
  Remember(g_enemies, obj, "enemy", lr);
  std::lock_guard<std::mutex> lock(g_mutex);
  g_last_update.emplace(obj, 0);
}

REX_FUNC(sub_82383B80) {
  uint32_t obj = ctx.r3.u32, lr = uint32_t(ctx.lr);
  __imp__sub_82383B80(ctx, base);
  Remember(g_enemies, obj, "golem", lr);
  std::lock_guard<std::mutex> lock(g_mutex);
  g_last_update.emplace(obj, 0);
}

REX_FUNC(sub_82159458) {
  uint32_t obj = ctx.r3.u32, lr = uint32_t(ctx.lr);
  __imp__sub_82159458(ctx, base);
  Remember(g_cameras, obj, "camera service", lr);
}

REX_FUNC(sub_82203C20) {
  Touch(ctx.r3.u32);
  __imp__sub_82203C20(ctx, base);
}

REX_FUNC(sub_823851E8) {
  Touch(ctx.r3.u32);
  __imp__sub_823851E8(ctx, base);
}

}  // extern "C"

namespace {

void StoreF(uint8_t* base, uint32_t a, float f) {
  uint32_t w;
  std::memcpy(&w, &f, 4);
  w = __builtin_bswap32(w);
  std::memcpy(base + a, &w, 4);
}

void StoreVec(uint8_t* base, uint32_t a, const Vec3& v) {
  StoreF(base, a, v.x);
  StoreF(base, a + 4, v.y);
  StoreF(base, a + 8, v.z);
}

Vec3 Normalize(Vec3 v) {
  float l = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
  return l > 1e-6f ? Vec3{v.x / l, v.y / l, v.z / l} : Vec3{0, 0, 1};
}

float WrapAngle(float a) {
  while (a > 3.14159265f) a -= 6.2831853f;
  while (a < -3.14159265f) a += 6.2831853f;
  return a;
}

std::atomic<float> g_stick_turn{0};  // radians the view is turned from the game's camera
std::atomic<bool> g_marker_on{false};
std::atomic<float> g_marker_x{0}, g_marker_y{0};  // 0..1 across the 16:9 picture
constexpr float kMarkerHeight = 1.0f;  // above the enemy's feet
// The camera's "right" row (+0x574) points to screen LEFT (yaw grows anticlockwise seen from
// above, toward the left of the picture) - found from where the dot landed on screenshots.
constexpr float kMarkerSignX = -1.0f;

// After the game's camera update: while locked (and while easing out) swing the camera around
// the hero so it looks past the hero at the target. The game's own camera is left alone - this
// rewrites only what it hands on: the final eye / look-at / axes (+0x574..+0x5AF), their copies
// (+0x5DC..), and the camera scene node's matrix (as the end of sub_8215AF68 does).
void OverrideCamera(uint8_t* base, uint32_t cam) {
  static float s_yaw = 0, s_weight = 0;
  static auto s_last = std::chrono::steady_clock::now();
  auto now = std::chrono::steady_clock::now();
  float dt = std::clamp(std::chrono::duration<float>(now - s_last).count(), 0.0f, 0.1f);
  s_last = now;

  Vec3 target{};
  bool locked = false;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_target) {
      uint32_t vt = Load32(base, g_target);
      target = LoadVec(base, g_target + kTargetPos);
      locked = (vt == kEnemyVtable || vt == kGolemVtable) && Sane(target);
    }
  }
  if (!locked && s_weight <= 0) {
    g_stick_turn = 0;
    g_marker_on = false;
    return;
  }

  constexpr uint32_t kRight = 0x574, kUp = 0x580, kFwd = 0x58C, kEye = 0x598, kLook = 0x5A4;
  Vec3 hero = LoadVec(base, cam + kCamFollow), eye = LoadVec(base, cam + kEye), look = LoadVec(base, cam + kLook);
  if (!Sane(hero) || !Sane(eye)) return;
  float game_yaw = std::atan2(eye.x - hero.x, eye.z - hero.z);  // direction hero -> eye
  static float s_want = 0;
  if (locked) {
    if (s_weight <= 0) s_yaw = s_want = game_yaw;
    // Eye on the far side of the hero. In melee range the direction flips as Spyro dodges
    // around the enemy, so it is only updated while the target is a few units away.
    float tx = target.x - hero.x, tz = target.z - hero.z;
    if (tx * tx + tz * tz > 3.5f * 3.5f || s_weight <= 0) s_want = std::atan2(-tx, -tz);
    float step = WrapAngle(s_want - s_yaw) * std::min(1.0f, dt * 4.0f);
    float max_step = 3.5f * dt;  // at most ~200 degrees a second
    s_yaw = WrapAngle(s_yaw + std::clamp(step, -max_step, max_step));
  }
  s_weight = std::clamp(s_weight + (locked ? dt : -dt) / 0.35f, 0.0f, 1.0f);
  float w = s_weight * s_weight * (3 - 2 * s_weight);  // smoothstep
  float delta = WrapAngle(s_yaw - game_yaw) * w;

  // Rotate eye and look-at around the hero by delta, then pull the look-at toward the target.
  float c = std::cos(delta), s = std::sin(delta);
  auto rotate = [&](const Vec3& p) {
    float x = p.x - hero.x, z = p.z - hero.z;
    return Vec3{hero.x + x * c + z * s, p.y, hero.z - x * s + z * c};
  };
  Vec3 new_eye = rotate(eye), new_look = rotate(look);
  float pull = 0.35f * w;
  new_look.x += (target.x - new_look.x) * pull;
  new_look.z += (target.z - new_look.z) * pull;

  Vec3 f = Normalize({new_look.x - new_eye.x, new_look.y - new_eye.y, new_look.z - new_eye.z});
  Vec3 r = Normalize({f.z, 0, -f.x});
  Vec3 u{f.y * r.z - f.z * r.y, f.z * r.x - f.x * r.z, f.x * r.y - f.y * r.x};

  // Spyro's controls still follow the game's camera: the left stick is turned by the same angle
  // as the view (LockOnPad), so "up" keeps meaning "away from the camera".
  g_stick_turn = WrapAngle(std::atan2(f.x, f.z) - std::atan2(look.x - eye.x, look.z - eye.z));

  // Target marker (drawn by plume's present pass): the target projected with this view. The
  // game's field of view is cam+0x56C (80 degrees, horizontal; 16:9 picture).
  Vec3 mark{target.x, target.y + kMarkerHeight, target.z};
  Vec3 d{mark.x - new_eye.x, mark.y - new_eye.y, mark.z - new_eye.z};
  float vz = d.x * f.x + d.y * f.y + d.z * f.z;
  float vx = d.x * r.x + d.y * r.y + d.z * r.z, vy = d.x * u.x + d.y * u.y + d.z * u.z;
  float fov = LoadF(base, cam + 0x56C);
  if (!(fov > 10 && fov < 170)) fov = 80;
  float tan_h = std::tan(fov * 0.5f * 3.14159265f / 180.0f), tan_v = tan_h * 9.0f / 16.0f;
  if (locked && vz > 0.5f) {
    g_marker_x = 0.5f + kMarkerSignX * 0.5f * vx / (vz * tan_h);
    g_marker_y = 0.5f - 0.5f * vy / (vz * tan_v);
    g_marker_on = true;
  } else {
    g_marker_on = false;
  }

  StoreVec(base, cam + kRight, r);
  StoreVec(base, cam + kUp, u);
  StoreVec(base, cam + kFwd, f);
  StoreVec(base, cam + kEye, new_eye);
  StoreVec(base, cam + kLook, new_look);
  StoreVec(base, cam + 0x5DC, u);  // copies the game reads back from the node (sub_8215C1A0)
  StoreVec(base, cam + 0x5E8, f);
  StoreVec(base, cam + 0x5F4, new_eye);

  // The camera scene node: [cam+0x10]+4 = node data; its matrix rows at +0x10 (right, up,
  // forward, position; the w word of each row holds flags), dirty bits as the game sets them.
  uint32_t node = Load32(base, cam + 0x10);
  if (node < 0x1000) return;
  uint32_t data = Load32(base, node + 4);
  if (data < 0x1000) return;
  uint32_t m = data + 0x10;
  StoreVec(base, m + 0x00, r);
  StoreVec(base, m + 0x10, u);
  StoreVec(base, m + 0x20, f);
  StoreVec(base, m + 0x30, new_eye);
  uint32_t flags = Load32(base, m + 0x0C) & ~0x00020003u;
  flags = __builtin_bswap32(flags);
  std::memcpy(base + m + 0x0C, &flags, 4);
  uint32_t parent = Load32(base, data + 0xA0);
  if (parent >= 0x1000) base[parent + 3] |= 3;
  base[data + 3] |= 12;
}

}  // namespace

extern "C" REX_EXTERN(__imp__sub_8215AF50);
// CKSrvCamera vtable slot 30: the per-frame camera update (-> sub_8215AF68).
extern "C" REX_FUNC(sub_8215AF50) {
  uint32_t cam = ctx.r3.u32;
  // Research (env SPYRO_FARCLIP=<units>): the camera input +0x568 (150 in Avalar, copied to the
  // far plane of the view each update) before the update.
  static const float s_far = std::getenv("SPYRO_FARCLIP") ? float(std::atof(std::getenv("SPYRO_FARCLIP"))) : 0.0f;
  // SPYRO_FARCLIP_TOGGLE: only every other frame (plume counts draws per parity, A/B in one run).
  static const bool s_toggle = std::getenv("SPYRO_FARCLIP_TOGGLE") != nullptr;
  bool on = !s_toggle || (g_frame.load() & 1);
  if (s_far > 0 && on && Load32(base, cam) == kCameraSrvVtable) {
    static int s_logged = 0;
    if (s_logged++ < 3) REXLOG_INFO("[spyro/lockon] far clip input {:.1f} -> {:.1f}", LoadF(base, cam + 0x568), s_far);
    StoreF(base, cam + 0x568, s_far);
  }
  __imp__sub_8215AF50(ctx, base);
  if (Load32(base, cam) == kCameraSrvVtable && spyro::CurrentMenuId() == 0x24) OverrideCamera(base, cam);
}

namespace spyro {

bool LockOnMarker(float& x, float& y) {
  if (!g_marker_on.load()) return false;
  x = g_marker_x.load();
  y = g_marker_y.load();
  return x > -0.1f && x < 1.1f && y > -0.1f && y < 1.1f;
}

void LockOnFrame(uint8_t* base) {
  ++g_frame;
  // Research (env SPYRO_LOCKREC): every 6 frames, every awake enemy's object (0x7C8 bytes) to
  // lockrec.bin = records {u32 frame, u32 obj, bytes} - to find what changes when a grublin has
  // finished burrowing in and joins the fight.
  static FILE* s_rec = std::getenv("SPYRO_LOCKREC") ? std::fopen("lockrec.bin", "wb") : nullptr;
  if (s_rec && g_frame.load() % 6 == 0) {
    std::lock_guard<std::mutex> lock(g_mutex);
    uint32_t frame = g_frame.load();
    for (auto& [obj, last] : g_last_update)
      if (Load32(base, obj) == kEnemyVtable && Awake(obj, kAwakeFrames)) {
        std::fwrite(&frame, 4, 1, s_rec);
        std::fwrite(&obj, 4, 1, s_rec);
        std::fwrite(base + obj, 1, 0x7C8, s_rec);
      }
    std::fflush(s_rec);
  }
  DebugDumps(base);
}

void LockOnPad(uint8_t* base, uint32_t state) {
  uint16_t buttons;
  std::memcpy(&buttons, base + state + 4, 2);
  buttons = __builtin_bswap16(buttons);
  bool r3 = (buttons & kButtonR3) != 0;
  bool pressed = r3 && !g_r3_was_down;
  g_r3_was_down = r3;
  if (CurrentMenuId() != 0x24) return;  // menus, cutscene movies: leave the pad alone
  if (GetSetting(kLockOnCamera) != 0) {  // Recomp Settings: Lock-on camera Off - R3 is the game's
    std::lock_guard<std::mutex> lock(g_mutex);
    g_target = 0;
    return;
  }
  if (r3) {
    buttons &= ~kButtonR3;  // R3 belongs to the lock-on
    uint16_t be = __builtin_bswap16(buttons);
    std::memcpy(base + state + 4, &be, 2);
  }
  if (float turn = g_stick_turn.load(); turn != 0) {
    // Left stick (state +8 x, +10 y) turned with the view. Stick angle atan2(x, y) grows
    // clockwise, world yaw anticlockwise, so the stick turns by -turn.
    static const float s_sign = std::getenv("SPYRO_LOCKSTICKFLIP") ? 1.0f : -1.0f;
    int16_t raw[2];
    std::memcpy(raw, base + state + 8, 4);
    float x = int16_t(__builtin_bswap16(uint16_t(raw[0]))), y = int16_t(__builtin_bswap16(uint16_t(raw[1])));
    if (x != 0 || y != 0) {
      float c = std::cos(s_sign * turn), s = std::sin(s_sign * turn);
      float nx = std::clamp(x * c + y * s, -32767.0f, 32767.0f), ny = std::clamp(y * c - x * s, -32767.0f, 32767.0f);
      uint16_t out[2] = {__builtin_bswap16(uint16_t(int16_t(std::lround(nx)))), __builtin_bswap16(uint16_t(int16_t(std::lround(ny))))};
      std::memcpy(base + state + 8, out, 4);
      // A fresh packet number, so the game takes the turned value even while the pad is still.
      uint32_t packet = __builtin_bswap32(Load32(base, state) + (g_frame.load() << 16));
      std::memcpy(base + state, &packet, 4);
    }
  }

  std::lock_guard<std::mutex> lock(g_mutex);
  uint32_t cam = Camera(base);
  if (!cam) {
    g_target = 0;
    return;
  }
  Vec3 hero = LoadVec(base, cam + kCamFollow);
  if (pressed) {
    if (g_target) {
      REXLOG_INFO("[spyro/lockon] off (R3)");
      g_target = 0;
    } else if ((g_target = PickTarget(base, hero)) != 0) {
      REXLOG_INFO("[spyro/lockon] on: 0x{:08X} vt 0x{:08X} at {:.1f} units", g_target, Load32(base, g_target),
                  Dist(LoadVec(base, g_target + kTargetPos), hero));
    } else {
      REXLOG_INFO("[spyro/lockon] R3: no awake enemy within {:.0f} units", kPickRange);
    }
  }
  if (!g_target) return;

  uint32_t vt = Load32(base, g_target);
  Vec3 target = LoadVec(base, g_target + kTargetPos);
  static const bool s_any = std::getenv("SPYRO_LOCKANY") != nullptr;
  const char* why = nullptr;
  if (vt != kEnemyVtable && vt != kGolemVtable) why = "target gone";
  else if (!s_any && !Awake(g_target, kLostFrames)) why = "target asleep / dead";
  else if (!Sane(target) || Dist(target, hero) > kReleaseRange) why = "target out of range";
  if (why) {
    REXLOG_INFO("[spyro/lockon] off ({})", why);
    g_target = 0;
    return;
  }
  static const bool s_log = std::getenv("SPYRO_LOCKTRACE") != nullptr;
  if (s_log && g_frame.load() % 15 == 0) {
    Vec3 eye = LoadVec(base, cam + 0x598), look = LoadVec(base, cam + 0x5A4);
    float view = std::atan2(look.x - eye.x, look.z - eye.z), to = std::atan2(target.x - hero.x, target.z - hero.z);
    static Vec3 s_prev{};
    float move = std::atan2(hero.x - s_prev.x, hero.z - s_prev.z);
    bool moved = Dist(hero, s_prev) > 0.3f;
    s_prev = hero;
    REXLOG_INFO("[spyro/lockon] view yaw {:.1f}, hero->target yaw {:.1f}, hero moving {}, marker {} ({:.2f} {:.2f})",
                view * 57.2958f, to * 57.2958f, moved ? std::to_string(int(move * 57.2958f)) : std::string("-"),
                g_marker_on.load(), g_marker_x.load(), g_marker_y.load());
  }
}
}  // namespace spyro

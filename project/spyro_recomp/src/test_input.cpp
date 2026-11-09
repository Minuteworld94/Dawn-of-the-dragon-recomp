// Scripted controller for test runs without a player (WORK_LOG 106): env SPYRO_INPUTSCRIPT =
// a text file (relative to the working directory) of timed button presses that are merged into
// what the game reads from controller 1. Without the variable the hook just calls the original.
//
// The presses reach the game three ways: as pad state (XInputGetState), as keystroke events
// (XInputGetKeystroke - the title screen's "Press START") and as a connected pad
// (XInputGetCapabilities).
//
// Script lines:  <seconds since the game first polled the pad>  <buttons>  [hold seconds, 0.15]
//   buttons: A B X Y START BACK UP DOWN LEFT RIGHT LB RB LT RT R3 LSU LSD LSL LSR (left stick)
//            RSL RSR (right stick),
//            several joined with '+', e.g. "A+LSU". '#' starts a comment.
//   A line starting with "@<hex menu state>" counts its seconds from the first time the game is
//   in that menu state instead (e.g. "@00 8 START" = 8 s after gameplay first starts; boot and
//   loading times vary too much between runs for fixed times - WORK_LOG 114). Pad state only.

#include <rex/logging.h>
#include <rex/ppc.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "spyro_settings.h"

extern "C" {
REX_EXTERN(__imp__sub_82412B50);
REX_EXTERN(__imp__sub_82412B48);
REX_EXTERN(__imp__sub_82412C28);
}

namespace {

struct Press {
  double at, until;
  uint16_t buttons;
  uint8_t lt, rt;
  int16_t lx, ly, rx;
  int anchor = -1;  // menu state the times count from (-1 = script start)
};

// Script time at which each menu state was first seen (-1 = not yet).
double g_anchor_time[256];
bool g_anchor_init = [] {
  for (double& t : g_anchor_time) t = -1;
  return true;
}();

bool ParseButtons(const std::string& text, Press& p) {
  std::stringstream ss(text);
  std::string b;
  while (std::getline(ss, b, '+')) {
    if (b == "UP") p.buttons |= 0x0001;
    else if (b == "DOWN") p.buttons |= 0x0002;
    else if (b == "LEFT") p.buttons |= 0x0004;
    else if (b == "RIGHT") p.buttons |= 0x0008;
    else if (b == "START") p.buttons |= 0x0010;
    else if (b == "BACK") p.buttons |= 0x0020;
    else if (b == "LB") p.buttons |= 0x0100;
    else if (b == "RB") p.buttons |= 0x0200;
    else if (b == "A") p.buttons |= 0x1000;
    else if (b == "B") p.buttons |= 0x2000;
    else if (b == "X") p.buttons |= 0x4000;
    else if (b == "Y") p.buttons |= 0x8000;
    else if (b == "LT") p.lt = 255;
    else if (b == "RT") p.rt = 255;
    else if (b == "LSU") p.ly = 32767;
    else if (b == "LSD") p.ly = -32767;
    else if (b == "LSL") p.lx = -32767;
    else if (b == "LSR") p.lx = 32767;
    else if (b == "RSL") p.rx = -32767;
    else if (b == "RSR") p.rx = 32767;
    else if (b == "R3") p.buttons |= 0x0080;
    else return false;
  }
  return true;
}

std::vector<Press> LoadScript() {
  std::vector<Press> out;
  const char* path = std::getenv("SPYRO_INPUTSCRIPT");
  if (!path) return out;
  std::ifstream f(path);
  if (!f) {
    REXLOG_ERROR("[spyro/input] script {} not found", path);
    return out;
  }
  std::string line;
  while (std::getline(f, line)) {
    if (auto hash = line.find('#'); hash != std::string::npos) line.resize(hash);
    int anchor = -1;
    if (auto first = line.find_first_not_of(" \t"); first != std::string::npos && line[first] == '@') {
      std::size_t used = 0;
      anchor = std::stoi(line.substr(first + 1), &used, 16) & 0xFF;
      line = line.substr(first + 1 + used);
    }
    std::stringstream ss(line);
    double at = 0, hold = 0.15;
    std::string buttons;
    if (!(ss >> at >> buttons)) continue;
    ss >> hold;
    Press p{at, at + hold, 0, 0, 0, 0, 0, 0, anchor};
    if (!ParseButtons(buttons, p)) {
      REXLOG_ERROR("[spyro/input] bad buttons '{}'", buttons);
      continue;
    }
    out.push_back(p);
  }
  REXLOG_INFO("[spyro/input] script {}: {} presses", path, out.size());
  return out;
}

void Store16(uint8_t* base, uint32_t a, uint16_t v) {
  v = __builtin_bswap16(v);
  std::memcpy(base + a, &v, 2);
}

const std::vector<Press>& Script() {
  static const std::vector<Press> s_script = LoadScript();
  return s_script;
}

double ScriptTime() {
  static const auto t0 = std::chrono::steady_clock::now();
  return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
}

// The same presses as keystroke events (key down at the start, key up at the end), for menus
// that read XInputGetKeystroke, like the title screen's "Press START".
struct Keystroke {
  double at;
  uint16_t vk, flags;  // flags: 1 key down, 2 key up
};

std::vector<Keystroke> BuildKeystrokes() {
  static const std::pair<uint16_t, uint16_t> kButtonKeys[] = {
      {0x0001, 0x5810}, {0x0002, 0x5811}, {0x0004, 0x5812}, {0x0008, 0x5813}, {0x0010, 0x5814},
      {0x0020, 0x5815}, {0x0100, 0x5805}, {0x0200, 0x5804}, {0x1000, 0x5800}, {0x2000, 0x5801},
      {0x4000, 0x5802}, {0x8000, 0x5803}};
  std::vector<Keystroke> out;
  for (const Press& p : Script()) {
    if (p.anchor >= 0) continue;  // menu-anchored presses are pad state only
    std::vector<uint16_t> keys;
    for (auto [bit, vk] : kButtonKeys)
      if (p.buttons & bit) keys.push_back(vk);
    if (p.lt) keys.push_back(0x5806);
    if (p.rt) keys.push_back(0x5807);
    if (p.ly > 0) keys.push_back(0x5820);
    if (p.ly < 0) keys.push_back(0x5821);
    if (p.lx > 0) keys.push_back(0x5822);
    if (p.lx < 0) keys.push_back(0x5823);
    for (uint16_t vk : keys) {
      out.push_back({p.at, vk, 1});
      out.push_back({p.until, vk, 2});
    }
  }
  std::stable_sort(out.begin(), out.end(), [](const Keystroke& a, const Keystroke& b) { return a.at < b.at; });
  return out;
}

void MergeScript(PPCContext& ctx, uint8_t* base, uint32_t state);

// Auto boot (env SPYRO_AUTOBOOT; project\PLAY-CONTINUE.bat, test runs): straight from the logos
// into the last save, by menu state instead of fixed times (boot times vary from run to run):
// START until the main menu (the logos and the title have no menu of their own - state 0x24 "no
// menu", like gameplay), there DOWN to "Load game" (A on its first row, "New game", started a
// new game - user), then A on every screen after it until the level. A tap 1.5 s after a screen
// appears, then every 2 s while it stays; off for good once "no menu" comes back after the main
// menu (the level), or after 3 min. The player's own controller keeps working alongside.
bool AutoBoot() {
  static const bool s_on = std::getenv("SPYRO_AUTOBOOT") != nullptr;
  return s_on;
}

uint16_t AutoBootButtonsImpl();

// Logs each tap once (start of the press).
uint16_t AutoBootButtons() {
  static uint16_t s_last = 0;
  uint16_t b = AutoBootButtonsImpl();
  if (b && b != s_last)
    REXLOG_INFO("[spyro/input] auto boot: press 0x{:04X} in menu state 0x{:02X} at t={:.1f}s", b,
                spyro::CurrentMenuId() & 0xFF, ScriptTime());
  s_last = b;
  return b;
}

uint16_t AutoBootButtonsImpl() {
  static bool s_done = false, s_menus = false;
  static int s_menu = -2;
  static double s_entered = 0;
  if (!AutoBoot() || s_done) return 0;
  double t = ScriptTime();
  int menu = spyro::CurrentMenuId();
  s_menus |= menu == 0x09;  // the main menu: from here on, A until the level
  if ((s_menus && menu == 0x24) || t > 180) {
    s_done = true;
    REXLOG_INFO("[spyro/input] auto boot: {} at t={:.1f}s", t > 180 ? "gave up" : "in the level", t);
    return 0;
  }
  if (menu != s_menu) {
    s_menu = menu;
    s_entered = t;
    REXLOG_INFO("[spyro/input] auto boot: menu state 0x{:02X} at t={:.1f}s", menu & 0xFF, t);
  }
  double since = t - s_entered;
  // Main menu: "New game" is the first row - DOWN to "Load game", A, once each (a repeated A
  // landed on the load screen, which stalls the game for ~10 s reading the saves, and loaded
  // slot 1).
  if (menu == 0x09) {
    if (since >= 1.5 && since < 1.65) return 0x0002;  // DOWN
    if (since >= 2.5 && since < 2.65) return 0x1000;  // A
    return 0;
  }
  // Load game: slot 1 is highlighted - the test snapshot (tests\testuser_avalar) has the Avalar
  // save there (user moved it to slot 1). Env SPYRO_AUTOBOOT=<n>: slot n instead (RIGHT n-1 times).
  if (menu == 0x12) {
    static const int s_slot = std::max(1, std::atoi(std::getenv("SPYRO_AUTOBOOT")));
    for (int k = 0; k < s_slot - 1; ++k)
      if (since >= 1.5 + k && since < 1.65 + k) return 0x0008;  // RIGHT
    double a = 1.5 + s_slot;
    if (since >= a && std::fmod(since - a, 3.0) < 0.15) return 0x1000;
    return 0;
  }
  // START before the menus; after them A on every screen (the load-game list, its slot,
  // Continue Story Mode, Single Player) until the level.
  uint16_t want = !s_menus ? 0x0010 : menu != 0x24 ? 0x1000 : 0;
  if (!want || since < 1.5) return 0;
  return std::fmod(since - 1.5, 2.0) < 0.15 ? want : 0;
}

}  // namespace

extern "C" {

// XInputGetCapabilities wrapper (user r3, flags r4, X_INPUT_CAPABILITIES* r5): with a script,
// controller 1 is always a connected gamepad (type 1, subtype 1).
REX_FUNC(sub_82412B48) {
  uint32_t user = ctx.r3.u32, caps = ctx.r5.u32;
  __imp__sub_82412B48(ctx, base);
  if ((!std::getenv("SPYRO_INPUTSCRIPT") && !AutoBoot()) || user != 0 || !caps || ctx.r3.u32 == 0) return;
  std::memset(base + caps, 0, 20);
  base[caps] = 1;
  base[caps + 1] = 1;
  ctx.r3.u64 = 0;
}

// XInputGetKeystroke wrapper (user r3, flags r4, X_INPUT_KEYSTROKE* r5) -> 0 with a keystroke,
// else an error (ERROR_EMPTY when the queue is empty). X_INPUT_KEYSTROKE: u16 virtual key,
// u16 unicode, u16 flags, u8 user, u8 hid code (big-endian).
REX_FUNC(sub_82412C28) {
  uint32_t keystroke = ctx.r5.u32;
  __imp__sub_82412C28(ctx, base);
  // Auto boot: the presses as keystroke events too (the title screen - and maybe others - read
  // only those): key down when a tap starts, key up when it ends.
  if (AutoBoot() && keystroke && ctx.r3.u32 != 0) {
    static uint16_t s_down = 0;
    uint16_t want = AutoBootButtons() & 0x0010;
    if (want != s_down) {
      // START only: the title screen reads keystrokes; the menus read the pad (a keystroke too
      // would press twice).
      static const std::pair<uint16_t, uint16_t> kKeys[] = {{0x0010, 0x5814}};
      uint16_t changed = want ^ s_down, bit = 0, vk = 0;
      for (auto [b, k] : kKeys)
        if (changed & b) bit = b, vk = k;
      if (vk) {
        bool down = (want & bit) != 0;
        s_down = uint16_t(down ? s_down | bit : s_down & ~bit);
        std::memset(base + keystroke, 0, 8);
        Store16(base, keystroke, vk);
        Store16(base, keystroke + 4, down ? 1 : 2);
        ctx.r3.u64 = 0;
        return;
      }
      s_down = want;
    }
  }
  if (Script().empty() || !keystroke || ctx.r3.u32 == 0) return;
  static const std::vector<Keystroke> s_keys = BuildKeystrokes();
  static size_t s_next = 0;
  if (s_next >= s_keys.size() || ScriptTime() < s_keys[s_next].at) return;
  const Keystroke& k = s_keys[s_next++];
  std::memset(base + keystroke, 0, 8);
  Store16(base, keystroke, k.vk);
  Store16(base, keystroke + 4, k.flags);
  REXLOG_INFO("[spyro/input] t={:.1f}s keystroke 0x{:04X} {}", ScriptTime(), k.vk, k.flags == 1 ? "down" : "up");
  ctx.r3.u64 = 0;
}

// XInputGetState wrapper (user r3, X_INPUT_STATE* r4) -> XamInputGetState(user, 1, state).
// X_INPUT_STATE: u32 packet; u16 buttons; u8 lt, rt; s16 lx, ly, rx, ry (big-endian).
// Also where the lock-on camera reads R3 and steers the right stick (spyro_lockon.cpp).
REX_FUNC(sub_82412B50) {
  uint32_t user = ctx.r3.u32, state = ctx.r4.u32;
  __imp__sub_82412B50(ctx, base);
  if (user != 0 || !state) return;
  MergeScript(ctx, base, state);
  if (uint16_t auto_buttons = AutoBootButtons()) {
    if (ctx.r3.u32 != 0) std::memset(base + state, 0, 16);  // no real pad: start from nothing
    uint16_t buttons;
    std::memcpy(&buttons, base + state + 4, 2);
    Store16(base, state + 4, __builtin_bswap16(buttons) | auto_buttons);
    static uint32_t s_packet = 0x40000000;  // a new packet number per tap, so the game notices
    uint32_t packet = __builtin_bswap32(++s_packet);
    std::memcpy(base + state, &packet, 4);
    ctx.r3.u64 = 0;
  }
  if (ctx.r3.u32 == 0) spyro::LockOnPad(base, state);
}

}  // extern "C"

namespace {

void MergeScript(PPCContext& ctx, uint8_t* base, uint32_t state) {
  const std::vector<Press>& s_script = Script();
  if (s_script.empty()) return;

  double t = ScriptTime();
  int menu = spyro::CurrentMenuId();
  if (menu >= 0 && g_anchor_time[menu & 0xFF] < 0) {
    g_anchor_time[menu & 0xFF] = t;
    REXLOG_INFO("[spyro/input] t={:.1f}s menu state 0x{:02X} first seen", t, menu);
  }
  Press now{0, 0, 0, 0, 0, 0, 0, 0};
  for (const Press& p : s_script) {
    double t0 = p.anchor >= 0 ? g_anchor_time[p.anchor] : 0.0;
    if (t0 < 0 || t < t0 + p.at || t >= t0 + p.until) continue;
    now.buttons |= p.buttons;
    if (p.lt) now.lt = p.lt;
    if (p.rt) now.rt = p.rt;
    if (p.lx) now.lx = p.lx;
    if (p.ly) now.ly = p.ly;
    if (p.rx) now.rx = p.rx;
  }
  if (ctx.r3.u32 != 0) std::memset(base + state, 0, 16);  // no real pad: start from nothing
  uint16_t buttons;
  std::memcpy(&buttons, base + state + 4, 2);
  buttons = __builtin_bswap16(buttons) | now.buttons;
  Store16(base, state + 4, buttons);
  if (now.lt) base[state + 6] = now.lt;
  if (now.rt) base[state + 7] = now.rt;
  if (now.lx) Store16(base, state + 8, uint16_t(now.lx));
  if (now.ly) Store16(base, state + 10, uint16_t(now.ly));
  if (now.rx) Store16(base, state + 12, uint16_t(now.rx));
  // A new packet number whenever the merged state changes, so the game notices.
  static uint16_t s_last = 0;
  static uint32_t s_packet = 1;
  uint16_t sig = buttons ^ uint16_t(now.lx) ^ uint16_t(now.ly << 1) ^ uint16_t(now.lt << 3) ^ uint16_t(now.rx << 2);
  if (sig != s_last) {
    s_last = sig;
    ++s_packet;
    if (now.buttons || now.lx || now.ly)
      REXLOG_INFO("[spyro/input] t={:.1f}s buttons 0x{:04X} stick {} {}", t, now.buttons, now.lx, now.ly);
  }
  uint32_t packet = __builtin_bswap32(s_packet);
  std::memcpy(base + state, &packet, 4);
  ctx.r3.u64 = 0;  // ERROR_SUCCESS: the scripted pad is connected
}

}  // namespace

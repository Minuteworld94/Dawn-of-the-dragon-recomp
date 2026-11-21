// Customize controls (spyro_controls.h, WORK_LOG 150).
#include "spyro_controls.h"

#include <rex/logging.h>
#include <rex/ui/ui_event.h>
#include <rex/ui/window.h>
#include <rex/ui/window_listener.h>
#include <rex/ui/windowed_app_context.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

#include <windows.h>

#include "spyro_achievements.h"
#include "spyro_settings.h"
#include "spyro_ui.h"
#define XXH_INLINE_ALL
#include "xxhash.h"

namespace spyro::controls {
namespace {

using Clock = std::chrono::steady_clock;
double Now() {
  static const Clock::time_point s_t0 = Clock::now();
  return std::chrono::duration<double>(Clock::now() - s_t0).count();
}

// ---- the pad's inputs ---------------------------------------------------------------------------
enum In {
  kA, kB, kX, kY, kLB, kRB, kLT, kRT, kStart, kBack, kL3, kR3,
  kDUp, kDDown, kDLeft, kDRight,
  kNumButtons,  // the inputs above are buttons (the triggers count as buttons here)
  kLUp = kNumButtons, kLDown, kLLeft, kLRight,
  kRUp, kRDown, kRLeft, kRRight,
  kNumIn,
};

struct InputDef {
  const char* badge;  // short name drawn in the button badge
  const char* name;   // what the row says
  uint16_t bit;       // X_INPUT_GAMEPAD button bit (0 = trigger / stick)
  uint16_t pad_vk;    // the keystroke a controller sends for it (VK_PAD_*)
};
// Row texts: the button and, in brackets, what it does in the game - the Xbox 360 control list of
// darkspyro.net/dawn/console (WORK_LOG 151), matching the game's own Moves screen (A A double jump,
// X light / Y heavy attacks) and the user's words ("LB is guard, B is grab/back"). The stick clicks
// are not in that list: the left one keeps its name, the right one is our lock-on camera.
const InputDef kInputs[kNumIn] = {
    {"A", "A (jump / fly)", 0x1000, 0x5800},
    {"B", "B (grab, back in menus)", 0x2000, 0x5801},
    {"X", "X (light attack, X+Y fury)", 0x4000, 0x5802},
    {"Y", "Y (heavy attack, X+Y fury)", 0x8000, 0x5803},
    {"LB", "LB (block / guard, evade)", 0x0100, 0x5805},
    {"RB", "RB (secondary element attack)", 0x0200, 0x5804},
    {"LT", "LT (switch dragon)", 0, 0x5806},
    {"RT", "RT (element attack)", 0, 0x5807},
    {"START", "Start (pause)", 0x0010, 0x5814},
    {"BACK", "Back (objectives)", 0x0020, 0x5815},
    {"LS", "Left stick click", 0x0040, 0x5816},
    {"RS", "Right stick click (lock-on)", 0x0080, 0x5817},
    {"D-PAD", "D-pad up (fire / poison)", 0x0001, 0x5810},
    {"D-PAD", "D-pad down (ice / shadow)", 0x0002, 0x5811},
    {"D-PAD", "D-pad left (earth / fear)", 0x0004, 0x5812},
    {"D-PAD", "D-pad right (electricity / wind)", 0x0008, 0x5813},
    {"LS", "Move forward", 0, 0x5820},
    {"LS", "Move back", 0, 0x5821},
    {"LS", "Move left", 0, 0x5823},
    {"LS", "Move right", 0, 0x5822},
    {"RS", "Camera up", 0, 0x5830},
    {"RS", "Camera down", 0, 0x5831},
    {"RS", "Camera left", 0, 0x5833},
    {"RS", "Camera right", 0, 0x5832},
};
// Names of the physical controller buttons (the controller screen's right column).
const char* const kPhysicalNames[kNumButtons] = {
    "A", "B", "X", "Y", "LB", "RB", "LT", "RT", "Start", "Back", "Left stick click", "Right stick click",
    "D-pad up", "D-pad down", "D-pad left", "D-pad right"};

// ---- keys -----------------------------------------------------------------------------------------
// Key codes: Windows virtual keys (mouse buttons included: 1 left, 2 right, 4 middle, 5/6 side),
// plus the wheel.
constexpr uint16_t kWheelUp = 0x100, kWheelDown = 0x101;
constexpr int kSlots = 2;

std::string KeyName(uint16_t k) {
  if (k == 0) return "";
  if (k == kWheelUp) return "Wheel Up";
  if (k == kWheelDown) return "Wheel Down";
  switch (k) {
    case VK_LBUTTON: return "Mouse Left";
    case VK_RBUTTON: return "Mouse Right";
    case VK_MBUTTON: return "Mouse Middle";
    case VK_XBUTTON1: return "Mouse 4";
    case VK_XBUTTON2: return "Mouse 5";
    case VK_BACK: return "Backspace";
    case VK_TAB: return "Tab";
    case VK_RETURN: return "Enter";
    case VK_SHIFT: case VK_LSHIFT: return "Left Shift";
    case VK_RSHIFT: return "Right Shift";
    case VK_CONTROL: case VK_LCONTROL: return "Left Ctrl";
    case VK_RCONTROL: return "Right Ctrl";
    case VK_MENU: case VK_LMENU: return "Left Alt";
    case VK_RMENU: return "Right Alt";
    case VK_PAUSE: return "Pause";
    case VK_CAPITAL: return "Caps Lock";
    case VK_ESCAPE: return "Esc";
    case VK_SPACE: return "Space";
    case VK_PRIOR: return "Page Up";
    case VK_NEXT: return "Page Down";
    case VK_END: return "End";
    case VK_HOME: return "Home";
    case VK_LEFT: return "Left Arrow";
    case VK_UP: return "Up Arrow";
    case VK_RIGHT: return "Right Arrow";
    case VK_DOWN: return "Down Arrow";
    case VK_INSERT: return "Insert";
    case VK_DELETE: return "Delete";
    case VK_MULTIPLY: return "Num *";
    case VK_ADD: return "Num +";
    case VK_SUBTRACT: return "Num -";
    case VK_DECIMAL: return "Num .";
    case VK_DIVIDE: return "Num /";
    case VK_NUMLOCK: return "Num Lock";
    case VK_SCROLL: return "Scroll Lock";
    case VK_APPS: return "Menu key";
    default: break;
  }
  char buf[24];
  if ((k >= '0' && k <= '9') || (k >= 'A' && k <= 'Z')) return std::string(1, char(k));
  if (k >= VK_NUMPAD0 && k <= VK_NUMPAD9) {
    std::snprintf(buf, sizeof(buf), "Num %d", k - VK_NUMPAD0);
    return buf;
  }
  if (k >= VK_F1 && k <= VK_F24) {
    std::snprintf(buf, sizeof(buf), "F%d", k - VK_F1 + 1);
    return buf;
  }
  // Punctuation keys: the character on this keyboard layout.
  UINT ch = MapVirtualKeyW(k, MAPVK_VK_TO_CHAR) & 0x7FFF;
  if (ch > 32 && ch < 127) return std::string(1, char(ch));
  std::snprintf(buf, sizeof(buf), "Key %02X", k);
  return buf;
}

// ---- the layout -----------------------------------------------------------------------------------
struct Layout {
  uint16_t keys[kNumIn][kSlots] = {};
  uint8_t pad[kNumButtons] = {};  // physical button that acts as each pad button
  bool mouse_camera = true;
  int sensitivity = 5;  // 1..10
  bool invert_y = false;
};

Layout DefaultLayout() {
  Layout l;
  auto set = [&](In i, uint16_t a, uint16_t b = 0) {
    l.keys[i][0] = a;
    l.keys[i][1] = b;
  };
  set(kA, VK_SPACE);
  set(kB, VK_LSHIFT, VK_BACK);
  set(kX, VK_LBUTTON, 'U');
  set(kY, VK_RBUTTON, 'O');
  set(kLB, 'R');
  set(kRB, 'F');
  set(kLT, 'Q');
  set(kRT, 'E');
  set(kStart, 'P');
  set(kBack, VK_TAB);
  set(kL3, 'C');
  set(kR3, VK_MBUTTON, 'V');
  set(kDUp, VK_UP);
  set(kDDown, VK_DOWN);
  set(kDLeft, VK_LEFT);
  set(kDRight, VK_RIGHT);
  set(kLUp, 'W');
  set(kLDown, 'S');
  set(kLLeft, 'A');
  set(kLRight, 'D');
  set(kRUp, 'I');
  set(kRDown, 'K');
  set(kRLeft, 'J');
  set(kRRight, 'L');
  for (int i = 0; i < kNumButtons; ++i) l.pad[i] = uint8_t(i);
  return l;
}

const char* const kIniKeys[kNumIn] = {"a",       "b",     "x",       "y",        "lb",       "rb",
                                      "lt",      "rt",    "start",   "back",     "l3",       "r3",
                                      "dpad_up", "dpad_down", "dpad_left", "dpad_right", "move_up", "move_down",
                                      "move_left", "move_right", "camera_up", "camera_down", "camera_left",
                                      "camera_right"};

std::mutex g_mutex;  // g_layout (game thread reads it per poll, the screens change it)
Layout g_layout = DefaultLayout();

Layout Current() {
  std::lock_guard<std::mutex> lock(g_mutex);
  return g_layout;
}

const char* File() {
  static const char* s_file =
      std::getenv("SPYRO_CONTROLS_FILE") ? std::getenv("SPYRO_CONTROLS_FILE") : "controls.ini";
  return s_file;
}

void Save(const Layout& l) {
  std::ofstream out(File(), std::ios::trunc);
  out << "# Customize controls (written by the game: Recomp Settings -> Customize controls)\n";
  out << "# Keyboard: up to two keys per controller input, as Windows key codes (256 = wheel up, 257 = wheel down).\n";
  for (int i = 0; i < kNumIn; ++i) {
    out << "key_" << kIniKeys[i] << "=" << l.keys[i][0] << "," << l.keys[i][1] << "   # " << KeyName(l.keys[i][0]);
    if (l.keys[i][1]) out << ", " << KeyName(l.keys[i][1]);
    out << "\n";
  }
  out << "mouse_camera=" << (l.mouse_camera ? 1 : 0) << "\n";
  out << "mouse_sensitivity=" << l.sensitivity << "\n";
  out << "mouse_invert_y=" << (l.invert_y ? 1 : 0) << "\n";
  out << "# Controller: the physical button (0 A, 1 B, 2 X, 3 Y, 4 LB, 5 RB, 6 LT, 7 RT, 8 Start, 9 Back,\n";
  out << "# 10 left stick click, 11 right stick click, 12-15 D-pad up/down/left/right) for each button.\n";
  for (int i = 0; i < kNumButtons; ++i) out << "pad_" << kIniKeys[i] << "=" << int(l.pad[i]) << "\n";
}

// The controller list must stay a swap list (every physical button used once).
bool ValidPad(const Layout& l) {
  uint32_t seen = 0;
  for (int i = 0; i < kNumButtons; ++i) {
    if (l.pad[i] >= kNumButtons || (seen & (1u << l.pad[i]))) return false;
    seen |= 1u << l.pad[i];
  }
  return true;
}

// ---- window: mouse motion and the wheel -----------------------------------------------------------
rex::ui::Window* g_window = nullptr;
std::atomic<HWND> g_hwnd{nullptr};
std::mutex g_mouse_mutex;
double g_mouse_dx = 0, g_mouse_dy = 0;  // since the last poll (only while the mouse is captured)
std::atomic<bool> g_captured{false};    // relative mouse mode on (UI thread state, mirrored)
std::atomic<int> g_wheel_game{0};       // wheel detents for the game (+ up, - down)
std::atomic<int> g_wheel_ui{0};         // wheel detents for the screens

class Listener : public rex::ui::WindowInputListener {
 public:
  void OnMouseMove(rex::ui::MouseEvent& e) override {
    if (!g_captured) return;
    std::lock_guard<std::mutex> lock(g_mouse_mutex);
    g_mouse_dx += e.dx();
    g_mouse_dy += e.dy();
  }
  void OnMouseWheel(rex::ui::MouseEvent& e) override {
    int detents = e.scroll_y() / int(rex::ui::MouseEvent::kScrollPerDetent);
    if (detents == 0 && e.scroll_y() != 0) detents = e.scroll_y() > 0 ? 1 : -1;
    g_wheel_game += detents;
    g_wheel_ui += detents;
  }
} g_listener;

bool Focused() {
  HWND h = g_hwnd.load();
  return h && GetForegroundWindow() == h;
}

// Relative mouse mode (cursor hidden and locked, motion as deltas) while the mouse moves the camera.
void SetCapture(bool on) {
  static bool s_want = false;
  if (on == s_want || !g_window) return;
  s_want = on;
  rex::ui::Window* window = g_window;
  window->app_context().CallInUIThreadDeferred([window, on] {
    static rex::ui::Window::CursorVisibility s_before = rex::ui::Window::CursorVisibility::kVisible;
    if (on == g_captured.load()) return;
    if (on) {
      s_before = window->GetCursorVisibility();
      window->SetCursorVisibility(rex::ui::Window::CursorVisibility::kHidden);
      bool relative = window->SetRelativeMouseMode(true);
      {
        std::lock_guard<std::mutex> lock(g_mouse_mutex);
        g_mouse_dx = g_mouse_dy = 0;
      }
      g_captured = true;
      REXLOG_INFO("[spyro/controls] mouse camera: cursor captured{}", relative ? "" : " (no relative mode!)");
    } else {
      window->SetRelativeMouseMode(false);
      window->SetCursorVisibility(s_before);
      g_captured = false;
      REXLOG_INFO("[spyro/controls] mouse camera: cursor released");
    }
  });
}

// ---- test switch: scripted keys and mouse (env SPYRO_KEYSCRIPT) ------------------------------------
// Lines: <seconds> <key> [hold]  (key = its name with '_' for spaces, e.g. Space, W, Mouse_Left, Esc)
//        <seconds> MOUSE <x px/s> <y px/s> [hold]
// Times from the game's first controller poll, like SPYRO_INPUTSCRIPT. The keys count as held
// whether or not the window has focus.
struct ScriptKey {
  double at, until;
  uint16_t key;
  float vx, vy;
};
std::vector<ScriptKey> LoadKeyScript() {
  std::vector<ScriptKey> out;
  const char* path = std::getenv("SPYRO_KEYSCRIPT");
  if (!path) return out;
  std::ifstream f(path);
  std::string line;
  while (std::getline(f, line)) {
    if (auto hash = line.find('#'); hash != std::string::npos) line.resize(hash);
    std::stringstream ss(line);
    double at = 0, hold = 0.15;
    std::string name;
    if (!(ss >> at >> name)) continue;
    ScriptKey k{at, 0, 0, 0, 0};
    if (name == "MOUSE") {
      ss >> k.vx >> k.vy;
    } else {
      std::string want = name;
      std::replace(want.begin(), want.end(), '_', ' ');
      for (uint16_t c = 1; c <= kWheelDown && !k.key; ++c)
        if (_stricmp(KeyName(c).c_str(), want.c_str()) == 0) k.key = c;
      if (!k.key) {
        REXLOG_ERROR("[spyro/controls] key script: unknown key '{}'", name);
        continue;
      }
    }
    ss >> hold;
    k.until = at + hold;
    out.push_back(k);
  }
  REXLOG_INFO("[spyro/controls] key script {}: {} lines", path, out.size());
  return out;
}
const std::vector<ScriptKey>& KeyScript() {
  static const std::vector<ScriptKey> s = LoadKeyScript();
  return s;
}
double ScriptClock() {  // starts at the first poll
  static const double s_t0 = Now();
  return Now() - s_t0;
}
bool ScriptKeyDown(uint16_t key) {
  double t = ScriptClock();
  for (const ScriptKey& k : KeyScript())
    if (k.key == key && t >= k.at && t < k.until) return true;
  return false;
}

// ---- reading the keyboard -------------------------------------------------------------------------
double g_wheel_up_until = 0, g_wheel_down_until = 0;  // a wheel detent = a short press (game thread)

bool KeyDown(uint16_t key, bool focused) {
  if (!key) return false;
  if (ScriptKeyDown(key)) return true;
  if (key == kWheelUp) return Now() < g_wheel_up_until;
  if (key == kWheelDown) return Now() < g_wheel_down_until;
  return focused && (GetAsyncKeyState(key) & 0x8000) != 0;
}

// The game's current menu: where there is none (boot logos, title, gameplay) Esc / Enter are Start;
// in menus they are B / A.
bool StartScreen() {
  int m = spyro::CurrentMenuId();
  return m < 0 || m == 0xFF || m == 0x00 || m == 0x04 || m == 0x11 || m == 0x24;
}
bool Gameplay() {
  int m = spyro::CurrentMenuId();
  return m == 0x00 || m == 0x24;
}

// What a fixed key means is decided when it goes down and kept until it is let go: Esc closing
// the pause menu (B) must not turn into Start (pause again) when the game is back a moment later.
int Latch(std::atomic<int>& target, bool down, int now) {
  if (!down) {
    target = -1;
    return -1;
  }
  int t = target.load();
  if (t < 0) target = t = now;
  return t;
}
std::atomic<int> g_esc_target{-1}, g_enter_target{-1};

// Which pad inputs the keyboard holds (bit i = input i) - the fixed Esc / Enter included.
uint32_t KeyboardInputs(const Layout& l, bool focused) {
  uint32_t held = 0;
  for (int i = 0; i < kNumIn; ++i)
    if (KeyDown(l.keys[i][0], focused) || KeyDown(l.keys[i][1], focused)) held |= 1u << i;
  bool start_screen = StartScreen();
  int esc = Latch(g_esc_target, KeyDown(VK_ESCAPE, focused), start_screen ? kStart : kB);
  int enter = Latch(g_enter_target, KeyDown(VK_RETURN, focused), start_screen ? kStart : kA);
  if (esc >= 0) held |= 1u << esc;
  if (enter >= 0) held |= 1u << enter;
  return held;
}

int16_t Axis(bool neg, bool pos) { return int16_t((pos ? 32767 : 0) - (neg ? 32767 : 0)); }

// Keyboard inputs -> pad (sticks: full tilt, diagonals on the circle).
Pad KeyboardPad(uint32_t held) {
  Pad p;
  for (int i = 0; i < kNumButtons; ++i)
    if (held & (1u << i)) {
      if (i == kLT) p.lt = 255;
      else if (i == kRT) p.rt = 255;
      else p.buttons |= kInputs[i].bit;
    }
  auto stick = [&](In up, In down, In left, In right, int16_t& x, int16_t& y) {
    x = Axis(held & (1u << left), held & (1u << right));
    y = Axis(held & (1u << down), held & (1u << up));
    if (x && y) {
      x = int16_t(x * 0.7071f);
      y = int16_t(y * 0.7071f);
    }
  };
  stick(kLUp, kLDown, kLLeft, kLRight, p.lx, p.ly);
  stick(kRUp, kRDown, kRLeft, kRRight, p.rx, p.ry);
  return p;
}

bool Neutral(const Pad& p) {
  return !p.buttons && p.lt < 30 && p.rt < 30 && std::abs(p.lx) < 8000 && std::abs(p.ly) < 8000 &&
         std::abs(p.rx) < 8000 && std::abs(p.ry) < 8000;
}

// ---- controller layout ----------------------------------------------------------------------------
bool PhysicalDown(const Pad& p, int i) {
  if (i == kLT) return p.lt > 30;
  if (i == kRT) return p.rt > 30;
  return (p.buttons & kInputs[i].bit) != 0;
}

bool Identity(const Layout& l) {
  for (int i = 0; i < kNumButtons; ++i)
    if (l.pad[i] != i) return false;
  return true;
}

Pad Remap(const Layout& l, const Pad& in) {
  if (Identity(l)) return in;
  Pad out = in;
  out.buttons = in.buttons & 0x0C00;  // guide / unused bits stay
  out.lt = out.rt = 0;
  for (int g = 0; g < kNumButtons; ++g) {
    int p = l.pad[g];
    if (g == kLT || g == kRT) {
      uint8_t v = p == kLT ? in.lt : p == kRT ? in.rt : (PhysicalDown(in, p) ? 255 : 0);
      (g == kLT ? out.lt : out.rt) = v;
    } else if (PhysicalDown(in, p)) {
      out.buttons |= kInputs[g].bit;
    }
  }
  return out;
}

// ---- screens: shared state ----------------------------------------------------------------------
enum Screen { kClosed = 0, kChooser, kKeyboardScreen, kControllerScreen };
std::atomic<int> g_screen{kClosed};
std::atomic<bool> g_wait_release{false};
std::atomic<bool> g_open_request{false};
// The physical controller as the game thread last read it (the screens use the real buttons, so
// a strange layout can never lock anyone out).
std::atomic<uint32_t> g_raw_buttons{0};
std::atomic<int32_t> g_raw_lx{0}, g_raw_ly{0};
std::atomic<uint32_t> g_raw_triggers{0};
std::atomic<int32_t> g_raw_rx{0}, g_raw_ry{0};
std::atomic<bool> g_mouse_camera_live{false};  // the mouse camera is in charge right now (ProcessPad)

int16_t AddAxis(int16_t a, int16_t b) { return int16_t(std::clamp(int32_t(a) + int32_t(b), -32768, 32767)); }

// ---- keystrokes -------------------------------------------------------------------------------
std::mutex g_ks_mutex;
uint32_t g_ks_held = 0;  // keyboard inputs as the game was told by keystrokes
std::deque<std::pair<uint16_t, uint16_t>> g_ks_queue;

// ---- pictures from the player's own game (never shipped) ------------------------------------------
// The pause Moves screen's parchment scroll and the game's controller button icons, both decoded by
// plume when that screen comes up (found with SPYRO_TEXLOG=07, WORK_LOG 150). Matched by size,
// format and the hash of the decoded data, so only those two are taken; kept in ui_cache\ so later
// sessions have them at once.
struct GameImage {
  GameImage(const char* f, const char* n, uint32_t w, uint32_t h, uint32_t fmt, uint64_t hs)
      : file(f), name(n), width(w), height(h), format(fmt), hash(hs) {}
  const char* file;
  const char* name;
  uint32_t width, height, format;
  uint64_t hash;
  std::mutex mutex;
  std::vector<uint8_t> pending;  // RGBA waiting for a ui texture (render thread)
  uint32_t pending_w = 0, pending_h = 0;
  uint32_t texture = 0, tex_w = 0, tex_h = 0;
  bool cache_tried = false;
  std::atomic<bool> captured{false};  // this session (or from the cache)
};
GameImage g_parchment("ui_cache/parchment.bin", "parchment", 1024, 1024, 20, 0x7C97AE1A987565AAull);
GameImage g_icons("ui_cache/buttons.bin", "button icons", 512, 512, 6, 0xEDDEC1E013AFB92Aull);

// Where each pad input's icon sits in the game's 512x512 button atlas (pixels).
struct IconRect {
  uint16_t x, y, w, h;
};
const IconRect kIconRects[kNumIn] = {
    {393, 189, 60, 52},   // A
    {69, 250, 60, 53},    // B
    {198, 251, 59, 52},   // X
    {328, 250, 60, 53},   // Y
    {161, 307, 114, 53},  // LB
    {2, 368, 114, 53},    // RB
    {457, 246, 50, 58},   // LT
    {55, 307, 50, 58},    // RT
    {369, 133, 58, 47},   // Start
    {4, 195, 58, 45},     // Back
    {13, 3, 48, 56},      // left stick click (LS)
    {315, 64, 48, 57},    // right stick click (RS)
    {152, 2, 70, 58},     // D-pad up
    {304, 2, 70, 58},     // D-pad down
    {3, 63, 70, 58},      // D-pad left
    {155, 63, 70, 58},    // D-pad right
    {139, 186, 49, 57},   // move: LS
    {139, 186, 49, 57},
    {139, 186, 49, 57},
    {139, 186, 49, 57},
    {315, 64, 48, 57},    // camera up / down: RS
    {315, 64, 48, 57},
    {76, 124, 58, 57},    // camera left: RS tilted left
    {233, 124, 58, 57},   // camera right: RS tilted right
};

}  // namespace

// ================================================================================================
void Load() {
  Layout l = DefaultLayout();
  std::ifstream in(File());
  std::string line;
  bool any = false;
  while (std::getline(in, line)) {
    if (auto hash = line.find('#'); hash != std::string::npos) line.resize(hash);
    auto eq = line.find('=');
    if (eq == std::string::npos) continue;
    std::string key = line.substr(0, eq), value = line.substr(eq + 1);
    while (!key.empty() && key.back() == ' ') key.pop_back();
    any = true;
    for (int i = 0; i < kNumIn; ++i) {
      if (key == std::string("key_") + kIniKeys[i]) {
        unsigned a = 0, b = 0;
        std::sscanf(value.c_str(), "%u,%u", &a, &b);
        l.keys[i][0] = uint16_t(a <= kWheelDown ? a : 0);
        l.keys[i][1] = uint16_t(b <= kWheelDown ? b : 0);
      }
      if (i < kNumButtons && key == std::string("pad_") + kIniKeys[i]) l.pad[i] = uint8_t(std::atoi(value.c_str()));
    }
    if (key == "mouse_camera") l.mouse_camera = std::atoi(value.c_str()) != 0;
    if (key == "mouse_sensitivity") l.sensitivity = std::clamp(std::atoi(value.c_str()), 1, 10);
    if (key == "mouse_invert_y") l.invert_y = std::atoi(value.c_str()) != 0;
  }
  if (!ValidPad(l)) {
    REXLOG_INFO("[spyro/controls] controller list in {} is not a swap list - default used", File());
    for (int i = 0; i < kNumButtons; ++i) l.pad[i] = uint8_t(i);
  }
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_layout = l;
  }
  if (!any) Save(l);  // first run: write the defaults so the file exists
  REXLOG_INFO("[spyro/controls] loaded {} ({}): mouse camera {} sensitivity {}{}, controller {}", File(),
              any ? "saved layout" : "defaults", l.mouse_camera ? "on" : "off", l.sensitivity,
              l.invert_y ? " inverted" : "", Identity(l) ? "default" : "remapped");
}

void Attach(rex::ui::Window* window) {
  if (!window || g_window) return;
  g_window = window;
  g_hwnd = HWND(window->GetNativeWindowHandle());
  window->AddInputListener(&g_listener, 1000);  // first in line; never marks events handled
  REXLOG_INFO("[spyro/controls] attached to the game window");
}

bool ProcessPad(uint32_t user, Pad& pad, bool connected) {
  Layout l = Current();
  if (user != 0) {  // other players: their controller layout only
    if (connected) pad = Remap(l, pad);
    return connected;
  }
  if (!connected) pad = Pad{};
  // The real controller, for the screens (before the layout).
  g_raw_buttons = pad.buttons;
  g_raw_lx = pad.lx;
  g_raw_ly = pad.ly;
  g_raw_triggers = uint32_t(pad.lt) | uint32_t(pad.rt) << 8;
  g_raw_rx = pad.rx;
  g_raw_ry = pad.ry;
  pad = Remap(l, pad);

  bool focused = Focused();
  // Wheel detents -> short presses (only bound keys read them).
  for (int d = g_wheel_game.exchange(0); d != 0; d += d > 0 ? -1 : 1)
    (d > 0 ? g_wheel_up_until : g_wheel_down_until) = Now() + 0.09;
  uint32_t held = KeyboardInputs(l, focused);
  Pad kb = KeyboardPad(held);

  bool captured = InputCaptured() || spyro::achievements::InputCaptured();
  // The screens' closing press must not reach the game: wait until everything is let go.
  if (g_wait_release && g_screen == kClosed && Neutral(kb) && Neutral(pad)) g_wait_release = false;

  // Mouse camera: during gameplay, window in front, no screen up. The mouse turns the view itself
  // (TakeMouseLook, read by the camera override in spyro_lockon.cpp) - and so do the camera keys
  // then, instead of tilting the right stick.
  bool mouse = l.mouse_camera && focused && Gameplay() && !captured;
  SetCapture(mouse);
  g_mouse_camera_live = mouse && g_captured;
  if (g_mouse_camera_live) kb.rx = kb.ry = 0;

  pad.buttons |= kb.buttons;
  pad.lt = std::max(pad.lt, kb.lt);
  pad.rt = std::max(pad.rt, kb.rt);
  pad.lx = AddAxis(pad.lx, kb.lx);
  pad.ly = AddAxis(pad.ly, kb.ly);
  pad.rx = AddAxis(pad.rx, kb.rx);
  pad.ry = AddAxis(pad.ry, kb.ry);
  return true;  // the keyboard is always there
}

bool TakeMouseLook(float& yaw, float& pitch) {
  yaw = pitch = 0;
  static double s_last = Now();
  double now = Now(), dt = std::clamp(now - s_last, 0.0, 0.1);
  s_last = now;
  double dx, dy;
  {
    std::lock_guard<std::mutex> lock(g_mouse_mutex);
    dx = g_mouse_dx;
    dy = g_mouse_dy;
    g_mouse_dx = g_mouse_dy = 0;
  }
  Layout l = Current();
  bool live = g_mouse_camera_live;
  // Test switch: scripted mouse speed (SPYRO_KEYSCRIPT "MOUSE" lines), as if the mouse moved.
  double t = ScriptClock();
  for (const ScriptKey& k : KeyScript())
    if (!k.key && t >= k.at && t < k.until) {
      dx += k.vx * dt;
      dy += k.vy * dt;
      live = true;
    }
  if (!live) return false;
  // 1:1 like a PC game: 0.11 degrees per mouse count on sensitivity 5, each step x1.3 (raw counts:
  // relative mouse mode, no Windows pointer acceleration).
  double k = 0.0019 * std::pow(1.3, l.sensitivity - 5);
  yaw = float(dx * k);
  pitch = float(dy * k * (l.invert_y ? -1 : 1));
  // The camera keys turn the view at a steady speed.
  bool focused = Focused();
  auto held = [&](In i) { return KeyDown(l.keys[i][0], focused) || KeyDown(l.keys[i][1], focused); };
  yaw += float(((held(kRRight) ? 1 : 0) - (held(kRLeft) ? 1 : 0)) * 2.4 * dt);
  pitch += float(((held(kRDown) ? 1 : 0) - (held(kRUp) ? 1 : 0)) * 1.6 * dt);
  return true;
}

bool PadCameraActive() {
  int32_t rx = g_raw_rx.load(), ry = g_raw_ry.load();
  return rx * rx + ry * ry > 12000 * 12000;
}

bool MouseCameraEnabled() { return Current().mouse_camera; }

bool FilterKeystroke(uint16_t& vk, uint16_t flags) {
  // Screens up: no presses for the game (releases still go through).
  if ((InputCaptured() || spyro::achievements::InputCaptured()) && (flags & 1)) return false;
  if (vk < 0x5800 || vk > 0x5817) return true;  // not a button key (sticks are not remapped)
  int physical = -1;
  for (int i = 0; i < kNumButtons; ++i)
    if (kInputs[i].pad_vk == vk) physical = i;
  if (physical < 0) return true;
  Layout l = Current();
  for (int g = 0; g < kNumButtons; ++g)
    if (l.pad[g] == physical) {
      vk = kInputs[g].pad_vk;
      return true;
    }
  return false;
}

bool NextKeystroke(uint16_t& vk, uint16_t& flags) {
  std::lock_guard<std::mutex> lock(g_ks_mutex);
  if (g_ks_queue.empty()) {
    Layout l = Current();
    uint32_t held = KeyboardInputs(l, Focused());
    if (InputCaptured() || spyro::achievements::InputCaptured()) held = 0;  // only releases while a screen is up
    uint32_t changed = held ^ g_ks_held;
    for (int i = 0; i < kNumIn; ++i)
      if (changed & (1u << i)) g_ks_queue.push_back({kInputs[i].pad_vk, uint16_t((held & (1u << i)) ? 1 : 2)});
    g_ks_held = held;
  }
  if (g_ks_queue.empty()) return false;
  vk = g_ks_queue.front().first;
  flags = g_ks_queue.front().second;
  g_ks_queue.pop_front();
  return true;
}

void OpenMenu() {
  if (g_screen == kClosed) REXLOG_INFO("[spyro/controls] Customize controls opened");
  g_open_request = true;
}

bool MenuOpen() { return g_screen != kClosed || g_open_request; }

bool InputCaptured() { return g_screen != kClosed || g_open_request || g_wait_release; }

bool KeyIsBound(int vk) {
  Layout l = Current();
  for (int i = 0; i < kNumIn; ++i)
    for (int s = 0; s < kSlots; ++s)
      if (l.keys[i][s] == vk) return true;
  return false;
}

// ================================================================================================
// Parchment capture
namespace {

constexpr uint32_t kImageMagic = 0x52415053;  // "SPAR"

void Rgb565(uint16_t c, uint8_t* out) {
  out[0] = uint8_t(((c >> 11) & 31) * 255 / 31);
  out[1] = uint8_t(((c >> 5) & 63) * 255 / 63);
  out[2] = uint8_t((c & 31) * 255 / 31);
}

// DXT1/3/5 (BC1-3) block rows as plume uploads them (little-endian) -> RGBA.
void DecodeBc(const uint8_t* data, uint32_t row_pitch, uint32_t w, uint32_t h, int bc, std::vector<uint8_t>& rgba) {
  rgba.assign(size_t(w) * h * 4, 0);
  uint32_t bw = (w + 3) / 4, bh = (h + 3) / 4, block_bytes = bc == 1 ? 8 : 16;
  for (uint32_t by = 0; by < bh; ++by)
    for (uint32_t bx = 0; bx < bw; ++bx) {
      const uint8_t* b = data + size_t(by) * row_pitch + size_t(bx) * block_bytes;
      const uint8_t* color = bc == 1 ? b : b + 8;
      uint16_t c0 = uint16_t(color[0] | color[1] << 8), c1 = uint16_t(color[2] | color[3] << 8);
      uint8_t pal[4][4];
      Rgb565(c0, pal[0]);
      Rgb565(c1, pal[1]);
      pal[0][3] = pal[1][3] = 255;
      bool four = bc != 1 || c0 > c1;
      for (int k = 0; k < 3; ++k) {
        pal[2][k] = uint8_t(four ? (2 * pal[0][k] + pal[1][k]) / 3 : (pal[0][k] + pal[1][k]) / 2);
        pal[3][k] = uint8_t(four ? (pal[0][k] + 2 * pal[1][k]) / 3 : 0);
      }
      pal[2][3] = 255;
      pal[3][3] = four ? 255 : 0;
      uint32_t bits = uint32_t(color[4] | color[5] << 8 | color[6] << 16 | uint32_t(color[7]) << 24);
      uint8_t alpha[16];
      if (bc == 2) {
        for (int i = 0; i < 16; ++i) alpha[i] = uint8_t(((b[i / 2] >> ((i & 1) * 4)) & 15) * 17);
      } else if (bc == 3) {
        uint8_t a0 = b[0], a1 = b[1], ap[8] = {a0, a1};
        if (a0 > a1)
          for (int i = 1; i < 7; ++i) ap[i + 1] = uint8_t(((7 - i) * a0 + i * a1) / 7);
        else {
          for (int i = 1; i < 5; ++i) ap[i + 1] = uint8_t(((5 - i) * a0 + i * a1) / 5);
          ap[6] = 0;
          ap[7] = 255;
        }
        uint64_t abits = 0;
        for (int i = 0; i < 6; ++i) abits |= uint64_t(b[2 + i]) << (8 * i);
        for (int i = 0; i < 16; ++i) alpha[i] = ap[(abits >> (3 * i)) & 7];
      }
      for (int i = 0; i < 16; ++i) {
        uint32_t x = bx * 4 + (i & 3), y = by * 4 + (i >> 2);
        if (x >= w || y >= h) continue;
        uint8_t* o = rgba.data() + (size_t(y) * w + x) * 4;
        const uint8_t* c = pal[(bits >> (2 * i)) & 3];
        o[0] = c[0];
        o[1] = c[1];
        o[2] = c[2];
        o[3] = bc == 1 ? c[3] : alpha[i];
      }
    }
}

// A decoded texture -> RGBA with the fetch swizzle applied (false = a format we don't read).
bool ToRgba(uint32_t w, uint32_t h, uint32_t format, uint32_t swizzle, const uint8_t* data, uint32_t row_pitch,
            std::vector<uint8_t>& rgba) {
  std::vector<uint8_t> raw;
  if (format == 18 || format == 19 || format == 20) {
    DecodeBc(data, row_pitch, w, h, format - 17, raw);
  } else if (format == 6) {
    raw.resize(size_t(w) * h * 4);
    for (uint32_t y = 0; y < h; ++y) std::memcpy(raw.data() + size_t(y) * w * 4, data + size_t(y) * row_pitch, w * 4);
  } else {
    return false;
  }
  rgba.resize(raw.size());
  for (size_t p = 0; p < raw.size(); p += 4)
    for (int c = 0; c < 4; ++c) {
      uint32_t s = (swizzle >> (3 * c)) & 7;
      rgba[p + c] = s < 4 ? raw[p + s] : s == 4 ? 0 : 255;
    }
  return true;
}

void WriteBmp(const std::string& path, uint32_t w, uint32_t h, const std::vector<uint8_t>& rgba) {
  std::ofstream f(path, std::ios::binary);
  uint32_t row = w * 4, size = 54 + row * h;
  uint8_t hdr[54] = {'B', 'M'};
  auto put = [&](int at, uint32_t v) { std::memcpy(hdr + at, &v, 4); };
  put(2, size);
  put(10, 54);
  put(14, 40);
  put(18, w);
  put(22, uint32_t(-int32_t(h)));  // top-down
  hdr[26] = 1;
  hdr[28] = 32;
  put(34, row * h);
  f.write(reinterpret_cast<char*>(hdr), 54);
  std::vector<uint8_t> line(row);
  for (uint32_t y = 0; y < h; ++y) {
    for (uint32_t x = 0; x < w; ++x) {
      const uint8_t* s = rgba.data() + (size_t(y) * w + x) * 4;
      line[x * 4 + 0] = s[2];
      line[x * 4 + 1] = s[1];
      line[x * 4 + 2] = s[0];
      line[x * 4 + 3] = s[3];
    }
    f.write(reinterpret_cast<char*>(line.data()), row);
  }
}

void StoreImage(GameImage& img, uint32_t w, uint32_t h, std::vector<uint8_t>&& rgba, bool from_cache) {
  std::lock_guard<std::mutex> lock(img.mutex);
  if (img.captured) return;
  img.captured = true;
  if (!from_cache) {
    std::error_code ec;
    std::filesystem::create_directories("ui_cache", ec);
    std::ofstream f(img.file, std::ios::binary);
    uint32_t hdr[3] = {kImageMagic, w, h};
    f.write(reinterpret_cast<const char*>(hdr), sizeof(hdr));
    f.write(reinterpret_cast<const char*>(rgba.data()), std::streamsize(rgba.size()));
  }
  img.pending = std::move(rgba);
  img.pending_w = w;
  img.pending_h = h;
  REXLOG_INFO("[spyro/controls] {} {}x{} {}", img.name, w, h, from_cache ? "loaded from ui_cache" : "taken from the game");
}

}  // namespace

void OnGameTexture(uint32_t width, uint32_t height, uint32_t xenos_format, uint32_t swizzle, const uint8_t* data,
                   uint32_t row_pitch, int menu_id) {
  // Research (env SPYRO_TEXLOG): every bigger texture decoded, with the menu it came up in;
  // SPYRO_TEXLOG=<hex menu id> also saves those decoded in that menu as BMP in texdump\.
  static const char* s_log = std::getenv("SPYRO_TEXLOG");
  GameImage* wanted = nullptr;
  for (GameImage* img : {&g_parchment, &g_icons})
    if (width == img->width && height == img->height && xenos_format == img->format && !img->captured) wanted = img;
  if (!s_log && !wanted) return;
  uint32_t rows = xenos_format >= 18 && xenos_format <= 20 ? (height + 3) / 4 : height;
  uint64_t hash = XXH3_64bits(data, size_t(row_pitch) * rows);
  if (s_log && (width >= 128 || height >= 128)) {
    REXLOG_INFO("[spyro/controls] texlog {}x{} format {} swizzle {:03X} menu {:02X} hash {:016X}", width, height,
                xenos_format, swizzle, menu_id & 0xFF, hash);
    int want_menu = int(std::strtol(s_log, nullptr, 16));
    std::vector<uint8_t> rgba;
    if (want_menu > 0 && (menu_id & 0xFF) == want_menu && ToRgba(width, height, xenos_format, swizzle, data, row_pitch, rgba)) {
      std::error_code ec;
      std::filesystem::create_directories("texdump", ec);
      char name[96];
      std::snprintf(name, sizeof(name), "texdump/menu%02X_%ux%u_f%u_%016llX.bmp", menu_id & 0xFF, width, height,
                    xenos_format, static_cast<unsigned long long>(hash));
      WriteBmp(name, width, height, rgba);
    }
  }
  if (wanted && hash == wanted->hash) {
    std::vector<uint8_t> rgba;
    if (ToRgba(width, height, xenos_format, swizzle, data, row_pitch, rgba))
      StoreImage(*wanted, width, height, std::move(rgba), false);
  }
}

// ================================================================================================
// The screens
namespace {

enum RowKind { kHeader, kInputRow, kOptionRow, kResetRow };
enum Option { kMouseCamera, kSensitivity, kInvertY };
struct Row {
  RowKind kind;
  int index;  // input / option
  const char* text;
};

std::vector<Row> KeyboardRows() {
  std::vector<Row> r;
  r.push_back({kHeader, 0, "Buttons"});
  for (int i : {kA, kB, kX, kY, kLB, kRB, kLT, kRT, kStart, kBack, kL3, kR3}) r.push_back({kInputRow, i, nullptr});
  r.push_back({kHeader, 0, "Movement (left stick)"});
  for (int i : {kLUp, kLDown, kLLeft, kLRight}) r.push_back({kInputRow, i, nullptr});
  r.push_back({kHeader, 0, "Camera (right stick)"});
  for (int i : {kRUp, kRDown, kRLeft, kRRight}) r.push_back({kInputRow, i, nullptr});
  r.push_back({kHeader, 0, "D-pad"});
  for (int i : {kDUp, kDDown, kDLeft, kDRight}) r.push_back({kInputRow, i, nullptr});
  r.push_back({kHeader, 0, "Mouse"});
  r.push_back({kOptionRow, kMouseCamera, "Mouse moves the camera"});
  r.push_back({kOptionRow, kSensitivity, "Mouse sensitivity"});
  r.push_back({kOptionRow, kInvertY, "Invert mouse up / down"});
  r.push_back({kResetRow, 0, "Reset to default"});
  return r;
}

std::vector<Row> ControllerRows() {
  std::vector<Row> r;
  r.push_back({kHeader, 0, "Buttons"});
  for (int i : {kA, kB, kX, kY, kLB, kRB, kLT, kRT, kStart, kBack, kL3, kR3}) r.push_back({kInputRow, i, nullptr});
  r.push_back({kHeader, 0, "D-pad"});
  for (int i : {kDUp, kDDown, kDLeft, kDRight}) r.push_back({kInputRow, i, nullptr});
  r.push_back({kResetRow, 0, "Reset to default"});
  return r;
}

// Screen state (render thread).
struct Ui {
  int chooser = 0;  // 0 keyboard and mouse, 1 controller
  std::vector<Row> rows;
  int selected = 1, slot = 0;
  float scroll = 0;
  // Waiting for a key / button for rows[selected] (slot).
  bool waiting = false;
  double wait_start = 0;
  bool wait_keys[kWheelDown + 1] = {};  // held when the wait began (ignored until let go)
  uint32_t wait_buttons = 0;
  int wait_wheel = 0;
  bool reset_armed = false;
  double reset_armed_at = 0;
  std::string notice;
  double notice_at = -10;
  // Input edges.
  uint32_t prev_buttons = 0xFFFF;
  int prev_dir = 0;
  double repeat_at = 0;
  bool prev_keys[8] = {true, true, true, true, true, true, true, true};
  bool prev_lmb = true, prev_rmb = true;
  float mouse_x = -1, mouse_y = -1;
  float last_mouse_x = -1, last_mouse_y = -1;
  // Hit boxes from the last frame: rows (y range) and slot boxes.
  struct Hit {
    float x, y, w, h;
    int row, slot;
  };
  std::vector<Hit> hits;
} g_ui;

void Notice(const std::string& text) {
  g_ui.notice = text;
  g_ui.notice_at = Now();
}

bool Selectable(const Row& r) { return r.kind != kHeader; }

void SelectFirst() {
  g_ui.selected = 0;
  while (g_ui.selected < int(g_ui.rows.size()) && !Selectable(g_ui.rows[g_ui.selected])) ++g_ui.selected;
  g_ui.slot = 0;
  g_ui.scroll = 0;
}

void Move(int dir) {
  int n = int(g_ui.rows.size()), i = g_ui.selected;
  for (int step = 0; step < n; ++step) {
    i += dir;
    if (i < 0 || i >= n) return;  // no wrap
    if (Selectable(g_ui.rows[i])) {
      g_ui.selected = i;
      g_ui.reset_armed = false;
      return;
    }
  }
}

std::string InputLabel(int i) { return kInputs[i].name; }

void Commit(const Layout& l, const char* what) {
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_layout = l;
  }
  Save(l);
  REXLOG_INFO("[spyro/controls] {} (saved to {})", what, File());
}

void StartWait() {
  g_ui.waiting = true;
  g_ui.wait_start = Now();
  for (uint16_t k = 1; k < 256; ++k) g_ui.wait_keys[k] = (GetAsyncKeyState(k) & 0x8000) != 0 || ScriptKeyDown(k);
  g_ui.wait_buttons = g_raw_buttons | ((g_raw_triggers & 0xFF) > 30 ? 0x10000u : 0) |
                      ((g_raw_triggers >> 8) > 30 ? 0x20000u : 0);
  g_ui.wait_wheel = g_wheel_ui.load();
}

void BindKey(int input, int slot, uint16_t key) {
  Layout l = Current();
  std::string moved;
  for (int i = 0; i < kNumIn; ++i)
    for (int s = 0; s < kSlots; ++s)
      if (l.keys[i][s] == key && !(i == input && s == slot)) {
        l.keys[i][s] = 0;
        if (i != input) moved = InputLabel(i);
      }
  l.keys[input][slot] = key;
  // Keep slot 1 filled first.
  if (!l.keys[input][0] && l.keys[input][1]) std::swap(l.keys[input][0], l.keys[input][1]);
  char what[160];
  std::snprintf(what, sizeof(what), "keyboard: %s = %s (slot %d)", InputLabel(input).c_str(), KeyName(key).c_str(),
                slot + 1);
  Commit(l, what);
  if (!moved.empty()) Notice(KeyName(key) + " was taken off \"" + moved + "\"");
  else Notice(KeyName(key) + " now works as \"" + InputLabel(input) + "\"");
}

void BindButton(int input, int physical) {
  Layout l = Current();
  int old = l.pad[input], other = -1;
  for (int g = 0; g < kNumButtons; ++g)
    if (l.pad[g] == physical) other = g;
  if (other >= 0) l.pad[other] = uint8_t(old);
  l.pad[input] = uint8_t(physical);
  char what[160];
  std::snprintf(what, sizeof(what), "controller: %s = physical %s", InputLabel(input).c_str(), kPhysicalNames[physical]);
  Commit(l, what);
  if (other >= 0 && other != input)
    Notice(std::string(kPhysicalNames[physical]) + " and " + kPhysicalNames[old] + " swapped places");
  else
    Notice(std::string(kPhysicalNames[physical]) + " now works as \"" + InputLabel(input) + "\"");
}

void ChangeOption(int option, int dir) {
  Layout l = Current();
  const char* what = "";
  if (option == kMouseCamera) l.mouse_camera = !l.mouse_camera, what = "mouse camera";
  if (option == kSensitivity) l.sensitivity = std::clamp(l.sensitivity + (dir ? dir : 1), 1, 10), what = "mouse sensitivity";
  if (option == kSensitivity && !dir && l.sensitivity == Current().sensitivity) l.sensitivity = 1;  // A at 10 wraps
  if (option == kInvertY) l.invert_y = !l.invert_y, what = "invert mouse";
  char text[96];
  std::snprintf(text, sizeof(text), "%s: camera %s, sensitivity %d%s", what, l.mouse_camera ? "on" : "off",
                l.sensitivity, l.invert_y ? ", inverted" : "");
  Commit(l, text);
}

void Close(bool to_chooser) {
  g_ui.hits.clear();
  g_ui.waiting = false;
  g_ui.reset_armed = false;
  g_ui.prev_buttons = 0xFFFF;
  for (bool& b : g_ui.prev_keys) b = true;
  g_ui.prev_lmb = g_ui.prev_rmb = true;
  if (to_chooser) {
    g_screen = kChooser;
  } else {
    g_screen = kClosed;
    g_wait_release = true;
    REXLOG_INFO("[spyro/controls] closed");
  }
}

void OpenScreen(int screen) {
  g_ui.rows = screen == kKeyboardScreen ? KeyboardRows() : ControllerRows();
  g_ui.hits.clear();
  SelectFirst();
  g_ui.waiting = false;
  g_ui.reset_armed = false;
  g_ui.prev_buttons = 0xFFFF;
  for (bool& b : g_ui.prev_keys) b = true;
  g_ui.prev_lmb = g_ui.prev_rmb = true;
  g_screen = screen;
  REXLOG_INFO("[spyro/controls] {} screen", screen == kKeyboardScreen ? "keyboard and mouse" : "controller");
}

// Waiting for the new key / button.
void HandleWait(bool focused) {
  const Row& row = g_ui.rows[g_ui.selected];
  bool keyboard = g_screen == kKeyboardScreen;
  double waited = Now() - g_ui.wait_start;
  if (waited > 10.0) {
    g_ui.waiting = false;
    Notice("Nothing changed (no key pressed)");
    return;
  }
  bool esc = (focused && (GetAsyncKeyState(VK_ESCAPE) & 0x8000)) || ScriptKeyDown(VK_ESCAPE);
  if (esc && !g_ui.wait_keys[VK_ESCAPE]) {
    g_ui.waiting = false;
    g_ui.prev_keys[3] = true;  // the Esc must not also close the screen
    Notice("Nothing changed");
    return;
  }
  uint32_t buttons = g_raw_buttons | ((g_raw_triggers & 0xFF) > 30 ? 0x10000u : 0) | ((g_raw_triggers >> 8) > 30 ? 0x20000u : 0);
  uint32_t fresh = buttons & ~g_ui.wait_buttons;
  g_ui.wait_buttons &= buttons;  // let go = may count again
  if (keyboard) {
    if (fresh) {  // a controller button cancels a keyboard binding
      g_ui.waiting = false;
      g_ui.prev_buttons = 0xFFFF;
      Notice("Nothing changed");
      return;
    }
    int wheel = g_wheel_ui.load() - g_ui.wait_wheel;
    uint16_t got = wheel > 0 ? kWheelUp : wheel < 0 ? kWheelDown : 0;
    for (uint16_t k = 1; k < 256 && !got; ++k) {
      if (k == VK_ESCAPE || k == VK_RETURN || k == VK_SHIFT || k == VK_CONTROL || k == VK_MENU || k == VK_LWIN ||
          k == VK_RWIN || k == VK_PROCESSKEY || k == VK_PACKET)
        continue;
      bool down = (focused && (GetAsyncKeyState(k) & 0x8000)) || ScriptKeyDown(k);
      if (down && !g_ui.wait_keys[k]) got = k;
      if (!down) g_ui.wait_keys[k] = false;
    }
    if (got) {
      g_ui.waiting = false;
      BindKey(row.index, g_ui.slot, got);
      // The key / click that bound must not also act on the screen.
      for (bool& b : g_ui.prev_keys) b = true;
      g_ui.prev_lmb = g_ui.prev_rmb = true;
    }
  } else {
    for (int p = 0; p < kNumButtons; ++p) {
      uint32_t bit = p == kLT ? 0x10000u : p == kRT ? 0x20000u : kInputs[p].bit;
      if (fresh & bit) {
        g_ui.waiting = false;
        BindButton(row.index, p);
        g_ui.prev_buttons = 0xFFFF;  // that press must not also act on the screen
        break;
      }
    }
  }
}

// Navigation for the chooser and the binding screens.
struct Nav {
  int dir = 0;      // -1 up, +1 down (with repeat)
  int side = 0;     // -1 left, +1 right (edges)
  bool confirm = false, back = false, clear = false;
};

Nav ReadNav(bool focused) {
  Nav n;
  double now = Now();
  uint32_t buttons = g_raw_buttons.load();
  int32_t lx = g_raw_lx.load(), ly = g_raw_ly.load();
  uint32_t pressed = buttons & ~g_ui.prev_buttons;
  g_ui.prev_buttons = buttons;
  auto key = [&](int vk) { return (focused && (GetAsyncKeyState(vk) & 0x8000) != 0) || ScriptKeyDown(uint16_t(vk)); };
  bool keys[8] = {key(VK_UP), key(VK_DOWN), key(VK_RETURN) || key(VK_SPACE), key(VK_ESCAPE) || key(VK_BACK),
                  key(VK_DELETE), key(VK_LEFT), key(VK_RIGHT), false};
  bool edge[8];
  for (int i = 0; i < 8; ++i) {
    edge[i] = keys[i] && !g_ui.prev_keys[i];
    g_ui.prev_keys[i] = keys[i];
  }
  int dir = 0;
  if ((buttons & 0x0001) || ly > 16000 || keys[0]) dir = -1;
  if ((buttons & 0x0002) || ly < -16000 || keys[1]) dir = 1;
  if (dir != 0 && (dir != g_ui.prev_dir || now >= g_ui.repeat_at)) {
    n.dir = dir;
    g_ui.repeat_at = now + (dir != g_ui.prev_dir ? 0.40 : 0.085);
  }
  g_ui.prev_dir = dir;
  static int s_prev_side = 0;
  int side = 0;
  if ((buttons & 0x0004) || lx < -16000 || keys[5]) side = -1;
  if ((buttons & 0x0008) || lx > 16000 || keys[6]) side = 1;
  if (side != s_prev_side) n.side = side;
  s_prev_side = side;
  n.confirm = (pressed & 0x1000) || edge[2];
  n.back = (pressed & 0x2000) || edge[3];
  n.clear = (pressed & 0x4000) || edge[4];
  return n;
}

void ReadMouse(bool focused) {
  g_ui.mouse_x = g_ui.mouse_y = -1;
  HWND h = g_hwnd.load();
  if (!focused || !h || g_captured) return;
  POINT p;
  if (!GetCursorPos(&p) || !ScreenToClient(h, &p)) return;
  g_ui.mouse_x = float(p.x);
  g_ui.mouse_y = float(p.y);
}

const Ui::Hit* HitAt(float x, float y) {
  for (const Ui::Hit& h : g_ui.hits)
    if (x >= h.x && x < h.x + h.w && y >= h.y && y < h.y + h.h) return &h;
  return nullptr;
}

void Activate(const Row& row) {
  if (row.kind == kInputRow) {
    StartWait();
  } else if (row.kind == kOptionRow) {
    ChangeOption(row.index, 0);
  } else if (row.kind == kResetRow) {
    if (!g_ui.reset_armed || Now() - g_ui.reset_armed_at > 4.0) {
      g_ui.reset_armed = true;
      g_ui.reset_armed_at = Now();
      return;
    }
    g_ui.reset_armed = false;
    Layout l = Current(), d = DefaultLayout();
    if (g_screen == kKeyboardScreen) {
      std::memcpy(l.keys, d.keys, sizeof(l.keys));
      l.mouse_camera = d.mouse_camera;
      l.sensitivity = d.sensitivity;
      l.invert_y = d.invert_y;
    } else {
      std::memcpy(l.pad, d.pad, sizeof(l.pad));
    }
    Commit(l, g_screen == kKeyboardScreen ? "keyboard: default layout" : "controller: default layout");
    Notice("Default layout restored");
  }
}

void HandleScreenInput(bool focused) {
  if (g_ui.waiting) {
    HandleWait(focused);
    return;
  }
  Nav n = ReadNav(focused);
  ReadMouse(focused);
  bool lmb = focused && (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
  bool rmb = focused && (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
  bool click = lmb && !g_ui.prev_lmb, rclick = rmb && !g_ui.prev_rmb;
  g_ui.prev_lmb = lmb;
  g_ui.prev_rmb = rmb;
  bool moved = g_ui.mouse_x != g_ui.last_mouse_x || g_ui.mouse_y != g_ui.last_mouse_y;
  g_ui.last_mouse_x = g_ui.mouse_x;
  g_ui.last_mouse_y = g_ui.mouse_y;
  const Ui::Hit* hit = g_ui.mouse_x >= 0 ? HitAt(g_ui.mouse_x, g_ui.mouse_y) : nullptr;
  int wheel = g_wheel_ui.exchange(0);

  if (g_screen == kChooser) {
    if (hit && (hit->row < 0 || hit->row > 1)) hit = nullptr;
    if (n.dir) g_ui.chooser = std::clamp(g_ui.chooser + n.dir, 0, 1);
    if (hit && moved) g_ui.chooser = hit->row;
    if (n.confirm || (click && hit)) {
      if (click && hit) g_ui.chooser = hit->row;
      OpenScreen(g_ui.chooser == 0 ? kKeyboardScreen : kControllerScreen);
    } else if (n.back || rclick) {
      Close(false);
    }
    return;
  }

  if (n.dir) Move(n.dir);
  if (wheel) g_ui.scroll = std::clamp(g_ui.scroll - float(wheel) * 2.0f, 0.0f, float(g_ui.rows.size()));
  if (hit && moved && hit->row >= 0 && Selectable(g_ui.rows[hit->row])) {
    if (g_ui.selected != hit->row) g_ui.reset_armed = false;
    g_ui.selected = hit->row;
    if (hit->slot >= 0) g_ui.slot = hit->slot;
  }
  const Row& row = g_ui.rows[g_ui.selected];
  bool keyboard = g_screen == kKeyboardScreen;
  if (n.side) {
    if (row.kind == kOptionRow) ChangeOption(row.index, n.side);
    else if (keyboard && row.kind == kInputRow) g_ui.slot = n.side < 0 ? 0 : 1;
  }
  if (n.back) {
    Close(true);
    return;
  }
  if (click && hit && hit->row >= 0) {
    g_ui.selected = hit->row;
    if (hit->slot >= 0) g_ui.slot = hit->slot;
    Activate(g_ui.rows[hit->row]);
    return;
  }
  if (n.confirm) {
    Activate(row);
    return;
  }
  if ((n.clear || (rclick && hit && hit->slot >= 0)) && keyboard && row.kind == kInputRow) {
    Layout l = Current();
    if (l.keys[row.index][g_ui.slot]) {
      std::string was = KeyName(l.keys[row.index][g_ui.slot]);
      l.keys[row.index][g_ui.slot] = 0;
      if (!l.keys[row.index][0] && l.keys[row.index][1]) std::swap(l.keys[row.index][0], l.keys[row.index][1]);
      Commit(l, ("keyboard: cleared " + was + " from " + InputLabel(row.index)).c_str());
      Notice(was + " removed");
    }
  }
}

// ---- drawing -----------------------------------------------------------------------------------
// Ink on the parchment.
constexpr uint32_t kInk = 0xFF3B220E, kInkSoft = 0xFF6E4B2A, kInkFaint = 0xFF9C7A52;
constexpr uint32_t kTitleInk = 0xFF7A4A0C, kPurple = 0xFF6E2378;

// Width / height the parchment is drawn at: the game shows its square scroll a little wider (about
// 900 x 810 on a 1440 x 810 screen); a touch more here for the two key columns.
constexpr float kParchAspect = 1.2f;

// The parchment: the game's own when we have it, else a painted stand-in.
void DrawParchment(float x, float y, float w, float h) {
  if (g_parchment.texture) {
    ui::Image(g_parchment.texture, x, y, w, h);
    return;
  }
  float r = h * 0.03f;
  ui::RoundRect(x - h * 0.006f, y - h * 0.006f, w + h * 0.012f, h + h * 0.012f, r * 1.2f, 0xC0201006);
  ui::RoundRectGradient(x, y, w, h, r, 0xFFEBD3A0, 0xFFD9B57A);
  // Darker, burnt-looking edges.
  float e = h * 0.05f;
  ui::RectGradient(x, y, w, e, 0x70603010, 0x00603010);
  ui::RectGradient(x, y + h - e, w, e, 0x00603010, 0x80603010);
  for (int i = 0; i < 8; ++i) {
    float t = float(i) / 8.0f, ew = e * (1.0f - t);
    ui::Rect(x + w * 0.0f, y + r, ew * 0.25f + 1, h - 2 * r, ui::Rgba(0xFF603010, 0.05f));
    ui::Rect(x + w - ew * 0.25f - 1, y + r, ew * 0.25f + 1, h - 2 * r, ui::Rgba(0xFF603010, 0.05f));
  }
  ui::RoundRectOutline(x + h * 0.012f, y + h * 0.012f, w - h * 0.024f, h - h * 0.024f, r * 0.8f,
                       std::max(1.5f, h * 0.004f), 0xFFA8792E);
  ui::RoundRectOutline(x, y, w, h, r, std::max(1.5f, h * 0.003f), 0xFF7A5420);
}

// Where text may go on the parchment (fractions of its width / height): inside the game's
// ornaments for its own picture.
void ContentBox(float px, float py, float pw, float ph, float& cx, float& cy, float& cw, float& ch) {
  bool game = g_parchment.texture != 0;
  float l = game ? 0.135f : 0.06f, r = game ? 0.135f : 0.06f, t = game ? 0.065f : 0.05f, b = game ? 0.075f : 0.05f;
  cx = px + pw * l;
  cy = py + ph * t;
  cw = pw * (1.0f - l - r);
  ch = ph * (1.0f - t - b);
}

void TextCentered(ui::Font f, float s, float cx, float y, const std::string& text, uint32_t color) {
  ui::Text(f, s, cx - ui::TextWidth(f, s, text) * 0.5f, y, text, color);
}

// A light wash of paper behind text on the game's parchment (its painted characters are busy),
// with soft edges.
void Wash(float x, float y, float w, float h, float r) {
  if (!g_parchment.texture) return;
  for (int k = 0; k < 4; ++k) {
    float in = r * 0.18f * float(k);
    ui::RoundRect(x + in, y + in, w - 2 * in, h - 2 * in, std::max(1.0f, r - in), ui::Rgba(0xFFEED8AC, 0.2f));
  }
}

// The game's own icon for a pad input, fitted into h (returns false without the icons).
bool Icon(int input, float x, float y, float h, float& w_out, float max_w = 0) {
  if (!g_icons.texture) return false;
  const IconRect& r = kIconRects[input];
  float w = h * float(r.w) / float(r.h);
  if (max_w > 0 && w > max_w) {  // wide icons (the bumpers): smaller, centred on the same line
    float nh = h * max_w / w;
    y += (h - nh) * 0.5f;
    h = nh;
    w = max_w;
  }
  float s = 1.0f / 512.0f;
  ui::Image(g_icons.texture, x, y, w, h, 0xFFFFFFFF, r.x * s, r.y * s, (r.x + r.w) * s, (r.y + r.h) * s);
  w_out = w;
  return true;
}

// A controller button badge: the game's icon (centred in a column wide enough for the bumpers), or
// without it a round coloured A/B/X/Y / a small dark plate with the name. Returns the column width.
float Badge(int input, float x, float y, float h) {
  float column = h * 2.0f, iw = 0;
  if (g_icons.texture) {
    const IconRect& r = kIconRects[input];
    iw = std::min(column, h * float(r.w) / float(r.h));
    float ih = iw * float(r.h) / float(r.w);
    Icon(input, x + (column - iw) * 0.5f, y + (h - ih) * 0.5f, ih, iw);
    return column;
  }
  const char* name = kInputs[input].badge;
  uint32_t face = 0;
  if (input == kA) face = 0xFF4FA83A;
  if (input == kB) face = 0xFFC8372A;
  if (input == kX) face = 0xFF2E6FC8;
  if (input == kY) face = 0xFFD8A21E;
  float s = h * 0.55f;
  if (face) {
    ui::RoundRect(x, y, h, h, h * 0.5f, 0xFF2A1A0C);
    ui::RoundRectGradient(x + h * 0.08f, y + h * 0.08f, h * 0.84f, h * 0.84f, h * 0.42f, ui::Rgba(face, 1.0f), face);
    ui::Text(ui::kBold, s, x + (h - ui::TextWidth(ui::kBold, s, name)) * 0.5f, y + (h - s) * 0.42f, name, 0xFFFFFFFF);
    return h;
  }
  float ts = h * 0.42f, w = std::max(h, ui::TextWidth(ui::kBold, ts, name) + h * 0.5f);
  ui::RoundRect(x, y + h * 0.12f, w, h * 0.76f, h * 0.22f, 0xFF3A2614);
  ui::Text(ui::kBold, ts, x + (w - ui::TextWidth(ui::kBold, ts, name)) * 0.5f, y + (h - ts) * 0.46f, name, 0xFFF4E2C0);
  return w;
}

// A key / button box. icon >= 0: the game's icon of that pad input in front of the text.
void SlotBox(float x, float y, float w, float h, const std::string& text, bool selected, bool waiting, int row, int slot,
             int icon = -1) {
  ui::RoundRect(x, y, w, h, h * 0.25f, selected ? 0x70FFF6E0 : 0x40FFF6E0);
  ui::RoundRectOutline(x, y, w, h, h * 0.25f, selected ? std::max(2.0f, h * 0.07f) : std::max(1.0f, h * 0.035f),
                       selected ? kPurple : 0x80583A1C);
  float s = h * 0.56f;
  std::string shown = waiting ? (g_screen == kKeyboardScreen ? "Press a key..." : "Press a button...")
                              : text.empty() ? "-" : text;
  uint32_t color = waiting ? kPurple : text.empty() ? kInkFaint : kInk;
  if (waiting) color = ui::Rgba(kPurple, 0.55f + 0.45f * float(std::sin(Now() * 6.0)));
  float iw = 0;
  if (icon >= 0 && !waiting && g_icons.texture) {
    float ih = h * 0.82f;
    Icon(icon, x + h * 0.25f, y + (h - ih) * 0.5f, ih, iw, h * 1.4f);
    iw += h * 0.2f;
  }
  float tx0 = x + (iw > 0 ? h * 0.25f + iw : 0), tw = w - (tx0 - x) - h * 0.2f;
  float textw = std::min(tw, ui::TextWidth(ui::kSemibold, s, shown));
  float tx = iw > 0 ? tx0 : x + (w - textw) * 0.5f;
  ui::TextClipped(ui::kSemibold, s, tx, y + (h - s) * 0.42f, tw, shown, color);
  g_ui.hits.push_back({x, y, w, h, row, slot});
}

void DrawChooser(float ww, float wh, float gx, float gy, float gw, float gh) {
  ui::Rect(0, 0, ww, wh, 0x99000000);
  float ph = gh * 0.66f, pw = ph * kParchAspect;
  float px = gx + (gw - pw) * 0.5f, py = gy + (gh - ph) * 0.5f;
  DrawParchment(px, py, pw, ph);
  float cx, cy, cw, ch;
  ContentBox(px, py, pw, ph, cx, cy, cw, ch);
  Wash(cx, cy + ch * 0.22f, cw, ch * 0.78f, ch * 0.06f);
  float title = ch * 0.13f;
  TextCentered(ui::kBold, title, cx + cw * 0.5f, cy + ch * 0.04f, "Customize controls", kTitleInk);
  const char* names[2] = {"Keyboard and mouse", "Controller"};
  const char* sub[2] = {"Keys, mouse buttons and the mouse camera", "Swap the controller's buttons around"};
  float bh = ch * 0.22f, bw = cw * 0.86f, bx = cx + (cw - bw) * 0.5f;
  g_ui.hits.clear();
  for (int i = 0; i < 2; ++i) {
    float by = cy + ch * (0.27f + 0.29f * float(i));
    bool sel = g_ui.chooser == i;
    ui::RoundRect(bx, by, bw, bh, bh * 0.2f, sel ? 0x50FFFFFF : 0x22FFF6E0);
    ui::RoundRectOutline(bx, by, bw, bh, bh * 0.2f, sel ? std::max(2.0f, bh * 0.04f) : std::max(1.0f, bh * 0.02f),
                         sel ? kPurple : 0x80583A1C);
    TextCentered(ui::kBold, bh * 0.36f, bx + bw * 0.5f, by + bh * 0.12f, names[i], sel ? kPurple : kInk);
    TextCentered(ui::kRegular, bh * 0.24f, bx + bw * 0.5f, by + bh * 0.58f, sub[i], kInkSoft);
    g_ui.hits.push_back({bx, by, bw, bh, i, -1});
  }
  TextCentered(ui::kRegular, ch * 0.055f, cx + cw * 0.5f, cy + ch * 0.92f, "A or Enter: choose      B or Esc: close",
               kInkSoft);
}

void DrawBindings(float ww, float wh, float gx, float gy, float gw, float gh) {
  ui::Rect(0, 0, ww, wh, 0x99000000);
  bool keyboard = g_screen == kKeyboardScreen;
  float ph = gh * 0.98f, pw = std::min(gw * 0.98f, ph * kParchAspect);
  float px = gx + (gw - pw) * 0.5f, py = gy + (gh - ph) * 0.5f;
  DrawParchment(px, py, pw, ph);
  float cx, cy, cw, ch;
  ContentBox(px, py, pw, ph, cx, cy, cw, ch);
  Layout l = Current();

  float title = ch * 0.07f;
  TextCentered(ui::kBold, title, cx + cw * 0.5f, cy, keyboard ? "Keyboard and mouse" : "Controller", kTitleInk);
  float list_y = cy + title * 1.35f, footer = ch * 0.11f;
  float list_h = cy + ch - footer - list_y;
  float row_h = ch * 0.052f, gap = ch * 0.006f;
  Wash(cx - cw * 0.025f, list_y - row_h * 0.25f, cw * 1.05f, cy + ch - list_y + row_h * 0.25f, row_h * 0.7f);
  int n = int(g_ui.rows.size());
  int visible = std::max(1, int(list_h / (row_h + gap)));
  // Keep the selection in view (the wheel can scroll freely; a selection change pulls it back).
  static int s_last_sel = -1;
  if (g_ui.selected != s_last_sel) {
    s_last_sel = g_ui.selected;
    float want = std::clamp(float(g_ui.selected - visible / 2), 0.0f, float(std::max(0, n - visible)));
    if (g_ui.selected < g_ui.scroll + 1 || g_ui.selected > g_ui.scroll + visible - 2) g_ui.scroll = want;
  }
  g_ui.scroll = std::clamp(g_ui.scroll, 0.0f, float(std::max(0, n - visible)));
  static float s_shown = 0;
  s_shown += (g_ui.scroll - s_shown) * 0.3f;
  if (std::fabs(g_ui.scroll - s_shown) < 0.01f) s_shown = g_ui.scroll;

  g_ui.hits.clear();
  float slot_w = cw * (keyboard ? 0.19f : 0.27f), slot_gap = cw * 0.02f;
  float slots_x = cx + cw - (keyboard ? 2 * slot_w + slot_gap : slot_w);
  ui::PushClip(cx - cw * 0.02f, list_y, cw * 1.04f, list_h);
  for (int i = 0; i < n; ++i) {
    float ry = list_y + (float(i) - s_shown) * (row_h + gap);
    if (ry + row_h < list_y || ry > list_y + list_h) continue;
    const Row& r = g_ui.rows[i];
    bool sel = i == g_ui.selected;
    if (r.kind == kHeader) {
      float hs = row_h * 0.56f;
      ui::Text(ui::kBold, hs, cx, ry + row_h - hs * 1.15f, r.text, kTitleInk);
      ui::Rect(cx, ry + row_h - hs * 0.05f, cw, std::max(1.0f, row_h * 0.03f), 0x80805020);
      continue;
    }
    if (sel) ui::RoundRect(cx - cw * 0.01f, ry, cw * 1.02f, row_h, row_h * 0.2f, 0x38FFFFFF);
    float ts = row_h * 0.5f;
    if (r.kind == kInputRow) {
      float bw = Badge(r.index, cx, ry + row_h * 0.08f, row_h * 0.84f);
      ui::TextClipped(ui::kSemibold, ts, cx + bw + row_h * 0.3f, ry + (row_h - ts) * 0.42f,
                      slots_x - (cx + bw + row_h * 0.3f) - row_h * 0.3f, InputLabel(r.index), sel ? kPurple : kInk);
      if (keyboard) {
        for (int s = 0; s < kSlots; ++s)
          SlotBox(slots_x + float(s) * (slot_w + slot_gap), ry + row_h * 0.1f, slot_w, row_h * 0.8f,
                  KeyName(l.keys[r.index][s]), sel && g_ui.slot == s, sel && g_ui.waiting && g_ui.slot == s, i, s);
      } else {
        std::string phys = kPhysicalNames[l.pad[r.index]];
        if (l.pad[r.index] != r.index) phys += "  (moved)";
        SlotBox(slots_x, ry + row_h * 0.1f, slot_w, row_h * 0.8f, g_ui.waiting && sel ? "" : phys, sel,
                sel && g_ui.waiting, i, 0, l.pad[r.index]);
      }
    } else if (r.kind == kOptionRow) {
      ui::Text(ui::kSemibold, ts, cx + row_h * 0.2f, ry + (row_h - ts) * 0.42f, r.text, sel ? kPurple : kInk);
      char value[32];
      if (r.index == kMouseCamera) std::snprintf(value, sizeof(value), "%s", l.mouse_camera ? "On" : "Off");
      if (r.index == kSensitivity) std::snprintf(value, sizeof(value), "%d", l.sensitivity);
      if (r.index == kInvertY) std::snprintf(value, sizeof(value), "%s", l.invert_y ? "On" : "Off");
      float vw = keyboard ? 2 * slot_w + slot_gap : slot_w;
      std::string shown = std::string("<   ") + value + "   >";
      SlotBox(slots_x, ry + row_h * 0.1f, vw, row_h * 0.8f, shown, sel, false, i, -1);
    } else {  // reset
      std::string text = g_ui.reset_armed && sel ? "Press again to reset this screen to the default layout"
                                                 : r.text;
      ui::Text(ui::kBold, ts, cx + row_h * 0.2f, ry + (row_h - ts) * 0.42f, text, sel ? kPurple : kInkSoft);
      g_ui.hits.push_back({cx, ry, cw, row_h, i, -1});
    }
    if (r.kind != kResetRow) g_ui.hits.push_back({cx, ry, slots_x - cx, row_h, i, -1});
  }
  ui::PopClip();
  // Scroll hint arrows (text: the font has no arrow glyphs).
  if (s_shown > 0.5f) TextCentered(ui::kBold, row_h * 0.5f, cx + cw * 0.5f, list_y - row_h * 0.45f, "more above", kInkFaint);
  if (s_shown < float(n - visible) - 0.5f)
    TextCentered(ui::kBold, row_h * 0.5f, cx + cw * 0.5f, list_y + list_h + row_h * 0.02f, "more below", kInkFaint);

  // Footer: a notice, or the keys.
  float fs = ch * 0.03f, fy = cy + ch - footer + footer * 0.30f;
  std::string line1, line2;
  if (g_ui.waiting) {
    const Row& r = g_ui.rows[g_ui.selected];
    line1 = keyboard ? "Press the key or mouse button for \"" + InputLabel(r.index) + "\""
                     : "Press the controller button for \"" + InputLabel(r.index) + "\"";
    line2 = keyboard ? "Esc or any controller button: cancel" : "Esc: cancel      (the button that has it now swaps places)";
  } else if (Now() - g_ui.notice_at < 3.0) {
    line1 = g_ui.notice;
  } else if (keyboard) {
    line1 = "A or Enter: change      X or Delete: remove      Left / Right: other key      B or Esc: back";
    line2 = "Always: Esc = pause (back in menus), Enter = start (confirm in menus)";
  } else {
    line1 = "A or Enter: change, then press the new button      B or Esc: back";
    line2 = "Buttons swap places, so every button stays on the controller";
  }
  TextCentered(ui::kSemibold, fs, cx + cw * 0.5f, fy, line1, g_ui.waiting ? kPurple : kInk);
  if (!line2.empty()) TextCentered(ui::kRegular, fs * 0.9f, cx + cw * 0.5f, fy + fs * 1.45f, line2, kInkSoft);
}

// A picture kept from an earlier session (render thread, once), and new ones -> ui textures.
void UpdateImage(GameImage& img) {
  if (!img.cache_tried) {
    img.cache_tried = true;
    std::ifstream f(img.file, std::ios::binary);
    uint32_t hdr[3] = {};
    if (f.read(reinterpret_cast<char*>(hdr), sizeof(hdr)) && hdr[0] == kImageMagic && hdr[1] == img.width &&
        hdr[2] == img.height) {
      std::vector<uint8_t> rgba(size_t(hdr[1]) * hdr[2] * 4);
      if (f.read(reinterpret_cast<char*>(rgba.data()), std::streamsize(rgba.size())))
        StoreImage(img, hdr[1], hdr[2], std::move(rgba), true);
    }
  }
  std::lock_guard<std::mutex> lock(img.mutex);
  if (img.pending.empty()) return;
  img.texture = ui::CreateTexture(img.pending_w, img.pending_h, ui::TextureFormat::kRgba8, img.pending.data());
  img.tex_w = img.pending_w;
  img.tex_h = img.pending_h;
  img.pending.clear();
}

}  // namespace

void BuildOverlay(float window_w, float window_h, float game_x, float game_y, float game_w, float game_h,
                  bool window_focused) {
  UpdateImage(g_parchment);
  UpdateImage(g_icons);
  if (g_open_request.exchange(false) && g_screen == kClosed) {
    g_ui.chooser = 0;
    g_ui.prev_buttons = 0xFFFF;  // the A that opened it is still held
    for (bool& b : g_ui.prev_keys) b = true;
    g_ui.prev_lmb = g_ui.prev_rmb = true;
    g_screen = kChooser;
  }
  if (g_screen == kClosed || !ui::FontsReady()) return;
  HandleScreenInput(window_focused);
  if (g_screen == kChooser) DrawChooser(window_w, window_h, game_x, game_y, game_w, game_h);
  else if (g_screen != kClosed) DrawBindings(window_w, window_h, game_x, game_y, game_w, game_h);
}

}  // namespace spyro::controls

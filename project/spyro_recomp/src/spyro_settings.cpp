#include "spyro_settings.h"

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/system/gpu_plugin.h>

#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <string>
#include <thread>

namespace spyro {
namespace {

const SettingDef kDefs[kNumSettings] = {
    // A button row, not a setting: just the word, no value, no arrows (spyro_hooks.cpp); A opens
    // the achievements list. Never written to the settings file.
    {"achievements", u"Achievements", {u""}, 1, 0},
    // Also a button row: A opens the controls screens (spyro_controls.cpp, WORK_LOG 150).
    {"controls", u"Customize controls", {u""}, 1, 0},
    // The menu font has no brackets; "-" renders (it is in "Anti-aliasing").
    // Xenos = reXglue's Xbox 360 GPU emulation (from Xenia) on D3D12, or on Vulkan.
#if defined(SPYRO_DECK_OPTIONS)
    // Linux / Steam Deck port (WORK_LOG 159): 800p (the Deck's screen height) and the picture's
    // shape. Defaults: the Deck's 800p 16:10 (16:10 on a 16:9 screen just gets side bars).
    {"resolution", u"Resolution", {u"720p", u"800p", u"1080p", u"1440p", u"4K"}, 5, 1},
    {"aspect_ratio", u"Aspect ratio", {u"16:9", u"16:10"}, 2, 1},
#else
    {"resolution", u"Resolution", {u"720p", u"1080p", u"1440p", u"4K"}, 4, 0},
#endif
#if defined(_WIN32)
    {"renderer", u"Renderer", {u"Xenos", u"Xenos - Vulkan", u"plume"}, 3, 2},  // plume for everyone
#else
    // Linux (WORK_LOG 156): plume on its Vulkan backend is the only renderer - the Xenos GPU
    // emulation is not built.
    {"renderer", u"Renderer", {u"plume - Vulkan"}, 1, 0},
#endif
    {"frame_rate", u"Frame rate", {u"30 FPS", u"60 FPS", u"120 FPS", u"Unlimited"}, 4, 1},
    {"vsync", u"VSync", {u"On", u"Off"}, 2, 0},
#if defined(_WIN32)
    {"display_mode", u"Display mode", {u"Windowed", u"Borderless", u"Fullscreen"}, 3, 0},
#else
    // Linux: borderless by default - the whole screen on the Steam Deck (WORK_LOG 159).
    {"display_mode", u"Display mode", {u"Windowed", u"Borderless", u"Fullscreen"}, 3, 1},
#endif
    {"motion_blur", u"Motion blur", {u"On", u"Off"}, 2, 0},
    // MSAA on the scene (plume_renderer.cpp SceneSamples; WORK_LOG 140). New ini key: the old
    // "anti_aliasing" held FXAA choices (Xenos only, removed) - not carried over as MSAA.
    {"msaa", u"Anti-aliasing", {u"Off", u"MSAA 2x", u"MSAA 4x", u"MSAA 8x"}, 4, 0},
    {"bloom", u"Bloom", {u"Original", u"Off"}, 2, 0},                  // plume_renderer.cpp
    {"draw_distance", u"Draw distance", {u"Original", u"Max"}, 2, 0},  // spyro_drawdist.cpp
    {"lockon_camera", u"Lock-on camera", {u"On", u"Off"}, 2, 0},  // spyro_lockon.cpp
    {"online_coop", u"Online co-op", {u"Off", u"On - soon"}, 2, 0},
};

// The player's settings; test runs point env SPYRO_SETTINGS_FILE at their own copy so they
// never change the player's file (WORK_LOG 106).
const char* const kFile = std::getenv("SPYRO_SETTINGS_FILE") ? std::getenv("SPYRO_SETTINGS_FILE")
                                                               : "recomp_settings.ini";

std::atomic<int> g_values[kNumSettings];
std::atomic<bool> g_plugin_applied{false};
bool g_plume = false;  // this run uses the plume renderer (fixed at start-up)
// Frame limiter target in seconds (0 = none; the guest vblank paces the game instead).
std::atomic<double> g_frame_time{0.0};

// The settings file is meant to be edited by hand too (WORK_LOG 164; on Linux the launcher keeps
// it next to SpyroRecomp): values are written as the menu shows them ("resolution = 800p") with
// the choices listed above each one. Read back: a choice's text (case, spaces and dashes don't
// matter) or, as older files and the test scripts have them, its number (0 = the first choice).
std::string Ascii(const char16_t* s) {
  std::string r;
  for (; s && *s; ++s) r += *s < 128 ? char(*s) : '?';
  return r;
}

std::string Squash(const std::string& s) {
  std::string r;
  for (char c : s)
    if (std::isalnum(static_cast<unsigned char>(c))) r += char(std::tolower(static_cast<unsigned char>(c)));
  return r;
}

std::string Trim(const std::string& s) {
  size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
  return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

// One line of explanation per setting for the file (the menu itself has no room for it).
const char* Help(int id) {
  switch (id) {
    case kResolution: return "The size the game is drawn at (the original is 720p). Higher = sharper and slower.";
#if defined(SPYRO_DECK_OPTIONS)
    case kAspectRatio: return "16:10 fills a 16:10 screen like the Steam Deck's; on a 16:9 screen use 16:9.";
#endif
#if defined(_WIN32)
    case kRenderer: return "plume = our own renderer; Xenos = GPU emulation (D3D12 or Vulkan). Next start.";
#else
    case kRenderer: return "Always plume on Vulkan on Linux.";
#endif
    case kFrameRate: return "The game's frame rate cap.";
    case kVSync: return "Waits for the screen's refresh: no tearing.";
    case kDisplayMode: return "Windowed, Borderless (the whole screen) or exclusive Fullscreen.";
    case kMotionBlur: return "The game's blur when the camera moves fast.";
    case kAntiAliasing: return "Smoother edges (MSAA); costs speed.";
    case kBloom: return "The glow around bright things.";
    case kDrawDistance: return "Max draws far scenery further out than the original.";
    case kLockOnCamera: return "R3 locks the camera onto the nearest enemy.";
    case kOnlineCoop: return "Not there yet.";
    default: return "";
  }
}

void Save() {
  std::ofstream out(kFile, std::ios::trunc);
  out << "# Spyro: Dawn of the Dragon Recomp - Recomp Settings\n"
         "#\n"
         "# The game reads this file when it starts, and writes it whenever you change something in\n"
         "# the in-game Recomp Settings menu. To change a setting from outside the game: close the\n"
         "# game, replace the text after \"=\" with one of the choices listed above it, save the file,\n"
         "# start the game again. A line the game doesn't understand keeps that setting's default.\n";
#if defined(SPYRO_DECK_OPTIONS)
  out << "#\n"
         "# Steam Deck / Linux, for now: change Resolution HERE, not in the game - changing it in the\n"
         "# game can freeze the game.\n";
#endif
  out << "\n[Recomp Settings]\n";
  for (int i = 0; i < kNumSettings; ++i) {
    if (IsButtonRow(i)) continue;
    const SettingDef& d = kDefs[i];
    std::string choices;
    for (int v = 0; v < d.count; ++v) choices += (v ? ", " : "") + Ascii(d.values[v]);
    out << "\n# " << Ascii(d.name) << ": " << choices << "\n# " << Help(i) << "\n"
        << d.key << " = " << Ascii(d.values[g_values[i].load()]) << "\n";
  }
}

void Load() {
  for (int i = 0; i < kNumSettings; ++i) g_values[i] = kDefs[i].default_value;
  std::ifstream in(kFile);
  std::string line;
  while (std::getline(in, line)) {
    line = Trim(line);
    auto eq = line.find('=');
    if (line.empty() || line[0] == '#' || line[0] == ';' || line[0] == '[' || eq == std::string::npos) continue;
    std::string key = Squash(line.substr(0, eq)), value = Trim(line.substr(eq + 1));
    if (auto c = value.find_first_of("#;"); c != std::string::npos) value = Trim(value.substr(0, c));
    for (int i = 0; i < kNumSettings; ++i) {
      // The file's key ("msaa") or the menu's name for it ("Anti-aliasing").
      if (IsButtonRow(i) || (key != Squash(kDefs[i].key) && key != Squash(Ascii(kDefs[i].name)))) continue;
      int found = -1;
      char* end = nullptr;
      long n = std::strtol(value.c_str(), &end, 10);
      if (!value.empty() && end && *end == '\0') {
        found = n >= 0 && n < kDefs[i].count ? int(n) : -1;  // a number: the old format
      } else {
        for (int v = 0; v < kDefs[i].count && found < 0; ++v)
          if (Squash(value) == Squash(Ascii(kDefs[i].values[v]))) found = v;
      }
      if (found >= 0) {
        g_values[i] = found;
      } else {
        std::string choices;
        for (int v = 0; v < kDefs[i].count; ++v) choices += (v ? ", " : "") + Ascii(kDefs[i].values[v]);
        REXLOG_WARN("[spyro/settings] {}: '{} = {}' is not one of: {} - using {}", kFile, kDefs[i].key, value,
                    choices, Ascii(kDefs[i].values[kDefs[i].default_value]));
      }
    }
  }
}

void SetCvar(const char* name, const char* value) {
  if (!rex::cvar::SetFlagByName(name, value))
    REXLOG_INFO("[spyro/settings] could not set cvar {}={}", name, value);
}

// Display mode: windowed / borderless (desktop-sized window) / exclusive fullscreen.
void ApplyDisplayMode() {
  int v = g_values[kDisplayMode];
  SetCvar("fullscreen_exclusive", v == 2 ? "true" : "false");
  SetCvar("fullscreen", v == 0 ? "false" : "true");
}

// Frame rate. The game measures real elapsed time per frame (dt) and scales everything by it
// (WORK_LOG 98), and paces itself on the emulated vblank ("vsync" cvar: 60 Hz when on, 1 kHz
// when off). 30: vblank 60 Hz + our limiter; 60: vblank 60 Hz; 120: vblank off + limiter;
// Unlimited: vblank off, no limiter.
void ApplyFrameRate() {
  int v = g_values[kFrameRate];
  SetCvar("vsync", v <= 1 ? "true" : "false");
  g_frame_time = v == 0 ? 1.0 / 30.0 : v == 2 ? 1.0 / 120.0 : 0.0;
}

void ApplyPluginSettings() {
  static bool s_logged = false;
  if (!s_logged && !g_plume) {
    s_logged = true;
    REXLOG_INFO("[spyro/settings] Xenos resolution_scale in effect: {}", rex::cvar::GetFlagByName("resolution_scale"));
  }
  ApplyFrameRate();
}

void Apply(int id) {
  switch (id) {
    case kFrameRate:
      if (g_plugin_applied) ApplyPluginSettings();
      break;
    case kVSync:
      SetCvar("host_vsync", g_values[kVSync] == 0 ? "true" : "false");
      break;
    case kDisplayMode:
      ApplyDisplayMode();
      break;
    default:  // Renderer: next launch. Motion blur: read by its hook. Resolution and MSAA: plume
              // reads them every frame (MSAA plume only), Xenos the resolution at the next launch.
              // Co-op: not yet.
      break;
  }
}

}  // namespace

const SettingDef& Def(int id) { return kDefs[id]; }

int GetSetting(int id) { return g_values[id]; }

void SetSetting(int id, int value) {
  if (id < 0 || id >= kNumSettings || value < 0 || value >= kDefs[id].count) return;
  g_values[id] = value;
  Apply(id);
  Save();
  std::u16string v = kDefs[id].values[value];
  REXLOG_INFO("[spyro/settings] {} = {} ({})", kDefs[id].key, value, std::string(v.begin(), v.end()));
}

bool MotionBlurEnabled() { return g_values[kMotionBlur] == 0; }

bool PlumeActive() { return g_plume; }

float ResolutionScale() {
#if defined(SPYRO_DECK_OPTIONS)
  static const float kScales[] = {1.0f, 800.0f / 720.0f, 1.5f, 2.0f, 3.0f};  // 720p, 800p, 1080p, 1440p, 4K
#else
  static const float kScales[] = {1.0f, 1.5f, 2.0f, 3.0f};  // 720p, 1080p, 1440p, 4K
#endif
  return kScales[g_values[kResolution]];
}

float PictureAspect() {
#if defined(SPYRO_DECK_OPTIONS)
  if (g_values[kAspectRatio] == 1) return 16.0f / 10.0f;
#endif
  return 16.0f / 9.0f;
}

void OnPreSetup(rex::RuntimeConfig& config) {
  Load();
  Save();  // writes the file with defaults on the first run
  ApplyDisplayMode();
  Apply(kVSync);
  int renderer = g_values[kRenderer];
  REXLOG_INFO("[spyro/settings] loaded: renderer {} frame_rate {} vsync {} display {} resolution {} blur {} aa {}",
              renderer, g_values[kFrameRate].load(), g_values[kVSync].load(), g_values[kDisplayMode].load(),
              g_values[kResolution].load(), g_values[kMotionBlur].load(), g_values[kAntiAliasing].load());
#if !defined(_WIN32)
  renderer = 2;  // Linux: always plume (no Xenos plugin exists)
#endif
  if (renderer <= 1 && !config.graphics && !config.gpu_plugin.empty()) {
    // Xenos: load the GPU plugin here (instead of the SDK's default load) so its cvars exist
    // and the resolution scale can be set before it creates its render targets. It only
    // supports whole multiples of 1280x720: 720p 1x, 1080p / 1440p 2x, 4K 3x.
    config.graphics = rex::system::LoadGpuPlugin(config.gpu_plugin, renderer == 1 ? "vulkan" : "d3d12");
    REXLOG_INFO("[spyro/settings] renderer: Xenos {} backend {}", renderer == 1 ? "Vulkan" : "D3D12",
                config.graphics ? "loaded" : "FAILED (using default)");
    static const char* kXenosScale[] = {"1", "2", "2", "3", "3"};
    if (config.graphics && g_values[kResolution] != 0) SetCvar("resolution_scale", kXenosScale[g_values[kResolution]]);
  } else if (renderer == 2) {
    // plume: no Xenos GPU plugin at all. reXglue then runs in "detached mode" (it opens the
    // window, the app renders into it); our D3D hooks drive plume (plume_renderer.cpp).
    config.gpu_plugin.clear();
    g_plume = true;
    REXLOG_INFO("[spyro/settings] renderer: plume{} (no Xenos GPU emulation)",
#if defined(_WIN32)
                ""
#else
                " on Vulkan"
#endif
    );
  }
}

void OnGameFrame() {
  if (!g_plugin_applied.exchange(true)) ApplyPluginSettings();
  double target = g_frame_time;
  static auto s_last = std::chrono::steady_clock::now();
  if (target > 0) {
    auto deadline = s_last + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                                 std::chrono::duration<double>(target));
    auto now = std::chrono::steady_clock::now();
    // Coarse sleep, then a short spin for precision (Windows sleeps in ~1 ms steps).
    if (deadline - now > std::chrono::milliseconds(2))
      std::this_thread::sleep_for(deadline - now - std::chrono::milliseconds(2));
    while (std::chrono::steady_clock::now() < deadline) std::this_thread::yield();
    now = std::chrono::steady_clock::now();
    // Don't try to catch up after a long frame (loading): restart the schedule.
    s_last = now - deadline > std::chrono::milliseconds(100) ? now : deadline;
  } else {
    s_last = std::chrono::steady_clock::now();
  }
}

}  // namespace spyro

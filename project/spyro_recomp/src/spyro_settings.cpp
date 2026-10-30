#include "spyro_settings.h"

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/system/gpu_plugin.h>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <string>
#include <thread>

namespace spyro {
namespace {

const SettingDef kDefs[kNumSettings] = {
    // The menu font has no brackets; "-" renders (it is in "Anti-aliasing").
    // Xenos = reXglue's Xbox 360 GPU emulation (from Xenia) on D3D12, or on Vulkan.
    {"resolution", u"Resolution", {u"720p", u"1080p", u"1440p", u"4K"}, 4, 0},
    {"renderer", u"Renderer", {u"Xenos", u"Xenos - Vulkan", u"plume"}, 3, 2},  // plume for everyone
    {"frame_rate", u"Frame rate", {u"30 FPS", u"60 FPS", u"120 FPS", u"Unlimited"}, 4, 1},
    {"vsync", u"VSync", {u"On", u"Off"}, 2, 0},
    {"display_mode", u"Display mode", {u"Windowed", u"Borderless", u"Fullscreen"}, 3, 0},
    {"motion_blur", u"Motion blur", {u"On", u"Off"}, 2, 0},
    {"anti_aliasing", u"Anti-aliasing", {u"Off", u"FXAA", u"FXAA Extreme"}, 3, 0},
    {"bloom", u"Bloom", {u"Original", u"Off"}, 2, 0},                  // plume_renderer.cpp
    {"draw_distance", u"Draw distance", {u"Original", u"Max - soon"}, 2, 0},  // not hooked up yet
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

void Save() {
  std::ofstream out(kFile, std::ios::trunc);
  out << "# Recomp Settings (written by the game; value = index into the menu's choices)\n";
  for (int i = 0; i < kNumSettings; ++i) out << kDefs[i].key << "=" << g_values[i].load() << "\n";
}

void Load() {
  for (int i = 0; i < kNumSettings; ++i) g_values[i] = kDefs[i].default_value;
  std::ifstream in(kFile);
  std::string line;
  while (std::getline(in, line)) {
    auto eq = line.find('=');
    if (line.empty() || line[0] == '#' || eq == std::string::npos) continue;
    std::string key = line.substr(0, eq);
    int v = std::atoi(line.c_str() + eq + 1);
    for (int i = 0; i < kNumSettings; ++i)
      if (key == kDefs[i].key && v >= 0 && v < kDefs[i].count) g_values[i] = v;
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
  static const char* kEffects[] = {"none", "fxaa", "fxaa_extreme"};
  SetCvar("swap_post_effect", kEffects[g_values[kAntiAliasing]]);
}

void Apply(int id) {
  switch (id) {
    case kFrameRate:
    case kAntiAliasing:
      if (g_plugin_applied) ApplyPluginSettings();
      break;
    case kVSync:
      SetCvar("host_vsync", g_values[kVSync] == 0 ? "true" : "false");
      break;
    case kDisplayMode:
      ApplyDisplayMode();
      break;
    default:  // Renderer: next launch. Motion blur: read by its hook. Resolution: plume reads it
              // every frame, Xenos at the next launch. Co-op: not yet.
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
  static const float kScales[] = {1.0f, 1.5f, 2.0f, 3.0f};  // 720p, 1080p, 1440p, 4K
  return kScales[g_values[kResolution]];
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
  if (renderer <= 1 && !config.graphics && !config.gpu_plugin.empty()) {
    // Xenos: load the GPU plugin here (instead of the SDK's default load) so its cvars exist
    // and the resolution scale can be set before it creates its render targets. It only
    // supports whole multiples of 1280x720: 720p 1x, 1080p / 1440p 2x, 4K 3x.
    config.graphics = rex::system::LoadGpuPlugin(config.gpu_plugin, renderer == 1 ? "vulkan" : "d3d12");
    REXLOG_INFO("[spyro/settings] renderer: Xenos {} backend {}", renderer == 1 ? "Vulkan" : "D3D12",
                config.graphics ? "loaded" : "FAILED (using default)");
    static const char* kXenosScale[] = {"1", "2", "2", "3"};
    if (config.graphics && g_values[kResolution] != 0) SetCvar("resolution_scale", kXenosScale[g_values[kResolution]]);
  } else if (renderer == 2) {
    // plume: no Xenos GPU plugin at all. reXglue then runs in "detached mode" (it opens the
    // window, the app renders into it); our D3D hooks drive plume (plume_renderer.cpp).
    config.gpu_plugin.clear();
    g_plume = true;
    REXLOG_INFO("[spyro/settings] renderer: plume (no Xenos GPU emulation)");
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

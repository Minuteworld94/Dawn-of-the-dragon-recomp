// Recomp Settings: the values behind the in-game "Recomp Settings" menu, saved to
// recomp_settings.ini in the working directory, and what each one changes (WORK_LOG 98).
//
// Live (apply at once): Frame rate, VSync, Display mode, Motion blur, Anti-aliasing,
// Resolution with plume.
// Next launch: Renderer (Xenos = reXglue GPU emulation on D3D12 / Vulkan; plume = our own
// renderer, plume_renderer.cpp), Resolution with Xenos.
// Not implemented yet: Online co-op.

#pragma once

#include <rex/runtime.h>

namespace spyro {

// Menu order (top to bottom); kDefs in spyro_settings.cpp follows it.
enum SettingId {
  kResolution,
  kRenderer,
  kFrameRate,
  kVSync,
  kDisplayMode,
  kMotionBlur,
  kAntiAliasing,
  kOnlineCoop,
};

constexpr int kNumSettings = 8;
constexpr int kMaxValues = 4;

struct SettingDef {
  const char* key;  // name in recomp_settings.ini
  const char16_t* name;
  const char16_t* values[kMaxValues];
  int count;
  int default_value;
};

const SettingDef& Def(int id);
int GetSetting(int id);
// Stores the value, applies it (live settings) and saves the file.
void SetSetting(int id, int value);

// Startup (SpyroDotdApp::OnPreSetup): read the file, set the window cvars, and load the GPU
// plugin with the chosen backend (only for Vulkan; otherwise the SDK's default load runs).
void OnPreSetup(rex::RuntimeConfig& config);

// Once per game frame (game manager update): the first call applies the settings that live in
// the GPU plugin (loaded after OnPreSetup); every call runs the frame-rate limiter.
void OnGameFrame();

bool MotionBlurEnabled();

// This run renders with plume (Renderer = plume at start-up; no Xenos GPU plugin loaded).
bool PlumeActive();

// Resolution setting as a factor of the game's 1280x720 (1, 1.5, 2, 3). plume renders at
// exactly this (live); Xenos only does whole multiples, set at start-up.
float ResolutionScale();

// The game's current menu state id (0x00 = gameplay, 0x06 = pause, ...; -1 before the menus
// exist). Updated every game frame by spyro_hooks.cpp; read by the test pad script.
int CurrentMenuId();

}  // namespace spyro

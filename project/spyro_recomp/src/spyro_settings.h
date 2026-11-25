// Recomp Settings: the values behind the in-game "Recomp Settings" menu, saved to
// recomp_settings.ini (the working directory; env SPYRO_SETTINGS_FILE elsewhere - the Linux
// launcher keeps it next to SpyroRecomp), and what each one changes (WORK_LOG 98). The file is
// readable and hand-editable: "resolution = 800p", choices listed above each setting (WORK_LOG 164).
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
  kAchievements,  // not a setting: a plain "Achievements" row at the top; A opens the list (WORK_LOG 144)
  kControls,      // not a setting: "Customize controls"; A opens the controls screens (spyro_controls, WORK_LOG 150)
  kResolution,
#if defined(SPYRO_DECK_OPTIONS)
  kAspectRatio,   // Linux / Steam Deck port: 0 = 16:9, 1 = 16:10 (WORK_LOG 159)
#endif
  kRenderer,
  kFrameRate,
  kVSync,
  kDisplayMode,
  kMotionBlur,
  kAntiAliasing,
  kBloom,         // 0 = Original, 1 = Off (plume skips the bloom pass, WORK_LOG 125)
  kDrawDistance,  // 0 = Original, 1 = Max (WORK_LOG 125)
  kLockOnCamera,  // 0 = On (R3 toggles the lock), 1 = Off (R3 goes to the game)
  kOnlineCoop,
};

#if defined(SPYRO_DECK_OPTIONS)
// Linux / Steam Deck port (WORK_LOG 159): + Aspect ratio; Resolution has 800p (5 values).
constexpr int kNumSettings = 14;
constexpr int kMaxValues = 5;
#else
constexpr int kNumSettings = 13;
constexpr int kMaxValues = 4;
#endif

// Rows that are buttons, not settings: just the word (no value, no arrows); A opens a screen.
constexpr bool IsButtonRow(int id) { return id == kAchievements || id == kControls; }

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

// Resolution setting as a factor of the game's 1280x720 (1, 1.5, 2, 3; Linux port: 800p =
// 800/720 too). plume renders at exactly this (live); Xenos only does whole multiples, set at
// start-up.
float ResolutionScale();

// Width / height of the picture plume shows: 16:9, or 16:10 with the Linux port's Aspect ratio
// setting (the game's 3D view then opens a little further up and down - spyro_drawdist.cpp - and
// the picture fills a 16:10 screen like the Steam Deck's; WORK_LOG 159). Live.
float PictureAspect();

// The game's current menu state id (0x00 = gameplay, 0x06 = pause, ...; -1 before the menus
// exist). Updated every game frame by spyro_hooks.cpp; read by the test pad script.
int CurrentMenuId();

// Lock-on camera (spyro_lockon.cpp): once per game frame, and on every successful read of
// controller 1 (X_INPUT_STATE at guest address state) - R3 toggles, the right stick steers.
void LockOnFrame(uint8_t* base);
void LockOnPad(uint8_t* base, uint32_t state);
// Where to draw the lock-on target dot: 0..1 across the picture (false = none).
bool LockOnMarker(float& x, float& y);

}  // namespace spyro

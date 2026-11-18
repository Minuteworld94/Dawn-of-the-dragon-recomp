// Achievements on screen (WORK_LOG 144): the unlock popup (Xbox 360 style, fire background) and
// the list of all achievements (1 key / Recomp Settings), drawn by plume with spyro_ui.
// The unlocks themselves are the reXglue SDK's (AchievementManager: catalog from the game's XEX,
// saved in <user>\achievements\565507F9.toml) - the game reports them through XAM as on a 360.
#pragma once

#include <cstdint>
#include <vector>

namespace spyro::achievements {

// Game thread, once per game frame: registers for the SDK's unlock notifications once the
// kernel exists, and runs the test switches (env SPYRO_ACHTEST / SPYRO_ACHLIST).
void OnGameFrame();

// The list screen.
void ToggleList();
void OpenList();
bool ListOpen();
// The pad belongs to the list: while it is open, and after it closes until every button is
// released (the B that closed it must not also reach the menu behind it).
bool InputCaptured();

// Controller state while InputCaptured() (XInputGetState hook, game thread): the list reads it
// and the game gets a neutral pad.
void PadInput(uint16_t buttons, int16_t lx, int16_t ly);

// Render thread (plume Present): builds this frame's overlay with spyro_ui. game_* = the 16:9
// picture inside the window; window_focused = keyboard input belongs to the game window.
void BuildOverlay(float window_w, float window_h, float game_x, float game_y, float game_w, float game_h,
                  bool window_focused);

// Fire for the popup background: the main menu swoosh's frames as plume captured them (render
// thread, WORK_LOG 146) - RGBA, width x height each, in playing order; [0, settle) are the bright
// start of the swoosh, the rest steady fire; fps = the movie's speed. Until then: embers.
void SetFireFrames(const std::vector<std::vector<uint8_t>>& frames, uint32_t width, uint32_t height,
                   uint32_t settle, float fps);

}  // namespace spyro::achievements

// Customize controls (WORK_LOG 150): keyboard and mouse play, and rebinding for the keyboard and
// for the controller, with our own screens (Recomp Settings -> "Customize controls").
//
// The game only knows the Xbox 360 controller. Every binding therefore names one of the pad's
// inputs (A, B, ..., D-pad, stick directions):
//   - keyboard and mouse: up to two keys (or mouse buttons / wheel) per pad input; during gameplay
//     the mouse turns the camera directly like a PC game (spyro_lockon.cpp writes the view; the
//     game's stick-driven camera was laggy and speed-capped - WORK_LOG 151), cursor locked + hidden.
//     Esc and Enter are fixed menu keys: Esc = Start (pause) in the game, B (back) in menus;
//     Enter = Start where there is no menu (title, gameplay), A (confirm) in menus.
//   - controller: which physical button acts as each pad button (a swap list, so every button
//     stays reachable). Sticks are not remapped.
// Both live in controls.ini next to recomp_settings.ini (env SPYRO_CONTROLS_FILE for test runs).
//
// Where it runs: the XInputGetState / XInputGetKeystroke hooks (test_input.cpp) call ProcessPad /
// FilterKeystroke / NextKeystroke on the game thread; the screens are drawn by plume (spyro_ui)
// on the render thread; mouse motion and the wheel come from reXglue's window events.
#pragma once

#include <cstdint>
#include <filesystem>

namespace rex::ui {
class Window;
}

namespace spyro::controls {

// A controller state in host byte order (X_INPUT_GAMEPAD).
struct Pad {
  uint16_t buttons = 0;
  uint8_t lt = 0, rt = 0;
  int16_t lx = 0, ly = 0, rx = 0, ry = 0;
  bool operator==(const Pad& o) const {
    return buttons == o.buttons && lt == o.lt && rt == o.rt && lx == o.lx && ly == o.ly && rx == o.rx && ry == o.ry;
  }
};

// Startup: read controls.ini (OnPreSetup), and hook up to the game window once reXglue opened it
// (OnPostSetup, UI thread). game_root = the game's folder (default.xex, Data\): the screens'
// parchment is read from its LVL000.KXE in the background (WORK_LOG 154).
void Load();
void Attach(rex::ui::Window* window, const std::filesystem::path& game_root);

// XInputGetState (game thread) for `user`: `pad` = what reXglue read (connected = a controller is
// there). Applies the controller layout, and for user 0 adds the keyboard and the mouse. Returns
// whether the game should see a connected controller (user 0 always: the keyboard is one).
bool ProcessPad(uint32_t user, Pad& pad, bool connected);

// The mouse camera (game thread, the camera override in spyro_lockon.cpp, once per camera update):
// how far the view should turn since the last call, in radians - yaw > 0 = to the right, pitch > 0
// = down (invert applied) - from the mouse, 1:1 with its movement, and the camera keys. false =
// the mouse camera is not in use now (off, a menu / screen up, window not in front).
bool TakeMouseLook(float& yaw, float& pitch);
// The real controller's right stick is being used (the game's own camera takes over again).
bool PadCameraActive();
// The "Mouse moves the camera" option is on.
bool MouseCameraEnabled();

// XInputGetKeystroke (game thread). FilterKeystroke: a keystroke reXglue produced for a real
// controller - its pad key is changed to follow the controller layout; false = drop it.
// NextKeystroke: the keyboard's own keystroke events (pad keys, like a controller sends), one per
// call; false = none waiting.
bool FilterKeystroke(uint16_t& vk, uint16_t flags);
bool NextKeystroke(uint16_t& vk, uint16_t& flags);

// The screens.
void OpenMenu();  // Recomp Settings "Customize controls" (A)
bool MenuOpen();
// While a screen is up (and until the buttons that closed it are let go) the game gets a neutral
// pad and no keystrokes.
bool InputCaptured();
// A key the keyboard layout uses (the achievements hotkeys 1 / Delete / Insert give way to it).
bool KeyIsBound(int vk);

// Render thread (plume Present), after the achievements overlay in the same spyro_ui frame.
void BuildOverlay(float window_w, float window_h, float game_x, float game_y, float game_w, float game_h,
                  bool window_focused);

// plume, whenever it decodes a game texture (level 0, linear blocks as uploaded): the button icons
// are cut out of the game's font pages (title screen / main menu, WORK_LOG 154), and the parchment
// is taken here too if the game files could not be read (the pause "Moves" scroll). Both are kept
// in ui_cache\ for later sessions. Never shipped.
void OnGameTexture(uint32_t width, uint32_t height, uint32_t xenos_format, uint32_t swizzle, const uint8_t* data,
                   uint32_t row_pitch, int menu_id);

}  // namespace spyro::controls

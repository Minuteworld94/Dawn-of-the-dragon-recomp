// What our code needs from the operating system, in one place (WORK_LOG 156): Windows (Win32) and
// Linux (SDL3 - reXglue's window is an SDL window on both) implementations in spyro_platform.cpp.
// Keys are named by Windows virtual-key codes everywhere (spyro_vk.h).
#pragma once

#include <cstdint>
#include <filesystem>
#include <initializer_list>
#include <string>
#include <vector>

#include "spyro_vk.h"

namespace spyro::platform {

// The game window, once reXglue opened it: the HWND on Windows, the SDL_Window* on Linux (what
// plume draws into and what keyboard focus is checked against).
void SetGameWindow(void* native);
void* GameWindow();
// The game window has the keyboard (it is the window in front).
bool GameWindowFocused();
// Asks the game window to close, as if the user closed it (false = no window).
bool CloseGameWindow();

// A key or mouse button (virtual-key code) is held right now (any thread).
bool KeyDown(int vk);
// The printable character of a punctuation key on this keyboard layout (0 = none).
uint32_t KeyChar(int vk);
// The mouse position in the game window, in its pixels (false = not known).
bool MousePosition(int& x, int& y);

// Plays an in-memory .wav file in the background (one at a time).
bool PlayWav(const std::vector<uint8_t>& wav);

// The overlay's font for a weight (0 regular, 1 semibold, 2 bold): candidate files, best first.
// Windows: Segoe UI from the Windows font folder; Linux: Segoe UI / Selawik next to the game
// (fonts\), then common system fonts (Noto Sans, DejaVu Sans, Liberation Sans).
std::vector<std::filesystem::path> FontFiles(int weight);

// The folder the game's executable is in.
std::filesystem::path ExeDir();

// root / parts..., each part matched ignoring upper / lower case (Linux file names are
// case-sensitive, the game's are not: the disc has "Default.xex"). A part not found is kept as given.
std::filesystem::path FindNoCase(std::filesystem::path root, std::initializer_list<const char*> parts);

// Research stack traces: up to `count` return addresses of the caller's callers, and the address
// the executable is loaded at (logged relative to it on Windows; 0 on Linux = absolute).
int CaptureStack(void** frames, int count);
uintptr_t ExeBase();

#if !defined(_WIN32)
// Linux only (WORK_LOG 161). Ends the game process right away, logs flushed: reXglue's full
// teardown crashed on the Steam Deck (glibc mutex assertion) and nothing of ours waits for it -
// saves, settings and achievements are written when they change.
[[noreturn]] void EndProcess(int code);
// A crash by abort() (glibc / C++ assertions) prints the stack to the console output, which the
// launcher keeps in logs/game_output.log. (SIGSEGV / SIGBUS / SIGILL belong to reXglue.)
void InstallCrashReport();
#endif

}  // namespace spyro::platform

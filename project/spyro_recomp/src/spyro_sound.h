// The fire whoosh for the achievement popup (WORK_LOG 146).
//
// The whoosh is the sound of the main menu's fire swoosh. It lives in the main menu's data
// (LVL000.KXE, compressed, not loaded in levels), so it can't be played through the game's sound
// system during play. Instead it is recorded from the game's own audio mix the first time the
// Game Menu comes in each session - the music has stopped by then, so the whoosh is alone - and
// played back when a popup appears (spyro_platform.cpp PlayWav: PlaySound on Windows, SDL audio
// on Linux). Nothing is written to disk.
#pragma once

#include <cstdint>

namespace spyro::sound {

// Controller state on every successful pad read (test_input.cpp, game thread): a button press or
// stick push ends a recording early (its menu sound must not get in).
void PadActivity(uint16_t buttons, int16_t lx, int16_t ly);
// The same for a key-down keystroke (XInputGetKeystroke hook).
void KeyPressed();

// Plays the recorded whoosh (any thread; nothing until one has been recorded).
void PlayWhoosh();

// A whoosh has been recorded this session.
bool WhooshReady();

}  // namespace spyro::sound

**Pre-alpha 2.5 - the (VERY experimental) keyboard and mouse update** of the Spyro: Dawn of the
Dragon PC recomp. Bring your own copy of the game (Xbox 360, USA/Europe disc) as an `.iso` or
extracted files.

> **Very experimental.** Keyboard and mouse play is brand new. It works, but it still needs tuning
> and has bugs. Please report what feels wrong, with the newest file from the `logs` folder.

New in pre-alpha 2.5 - **keyboard and mouse**:
- Play with a **keyboard and mouse**, a controller, or both at once
- **Mouse camera**: the mouse turns the camera directly, like a PC game, and it stays where you
  leave it. Spyro runs where the camera looks. Touching the controller's right stick, or the
  lock-on, gives the camera back to the game
- New **Recomp Settings -> Customize controls** screen, drawn on the game's own parchment from the
  Moves screen with the game's own button icons:
  - **Keyboard and mouse**: two keys (or mouse buttons / the wheel) for every controller button
    and stick direction; each row says what the button does in the game (LB = block / guard,
    B = grab, RT = element attack, ...); mouse camera on / off, sensitivity 1-10, invert up / down
  - **Controller**: swap the buttons around
  - Saved in `bin\controls.ini`
- Default keys: **WASD** move, **mouse** camera (or I J K L), **Space** jump, **left / right mouse**
  light / heavy attack, **Q E R F** triggers and bumpers, **arrows** D-pad (elements), **Tab** Back,
  **P** Start, **middle mouse** lock-on. **Esc** always pauses (and goes back in menus), **Enter**
  always starts / confirms
- The parchment and button pictures come from your own copy of the game: open **Pause -> Moves**
  once and they are kept from then on (stand-ins until then)

Also new - **turn achievements off**:
- **Delete** on the keyboard turns achievements off (it asks first). While they are off, nothing
  unlocks and no popups appear; the ones you already have stay. Press **Delete** again to turn them
  back on. Remembered between sessions
- **Reset all achievements** moved to **Insert** (still asks first)

Still on the way: tuning the keyboard and mouse, more bug fixes and testing, a refined
**lock-on camera**, better **stability**, and the start of work on **online co-op**.

**How to play:** download `SpyroRecomp-{VERSION}.zip` below, unzip it, double-click
`SpyroRecomp.exe` and pick your `.iso` (or `default.xex` of an extracted game). Settings are in the
game's menus under *Recomp Settings*. If Windows shows "Windows protected your PC" (the program is
not digitally signed), click **More info -> Run anyway**.

What works:
- Native PC build with its own Direct3D 12 renderer (plume)
- Resolution up to 4K, frame rate 30/60/120/unlimited, VSync, windowed/borderless/fullscreen,
  motion blur, MSAA, bloom and draw distance options, all inside the game's own menus
- Keyboard and mouse with rebindable keys, mouse camera, controller button swaps
- Achievements with popups and a list (and an off switch)
- Lock-on camera (R3 / middle mouse) and a Quit to Desktop button on the Game Menu and the Pause screen
- Game shaders ship pre-translated; any shader missing from that cache is translated automatically
  on first use and kept for next time
- Played through the first three levels

Known issues:
- Keyboard and mouse is very experimental: it still needs tuning, and there are bugs
- The mouse camera does not steer around walls by itself; if it clips into one, move the mouse
- With a keyboard, player 1 always counts as connected, so the game no longer notices a
  controller being unplugged
- Only the first three levels are fully tested; later areas may still have rendering bugs (try
  Renderer: Xenos if something looks wrong, and please report where)
- Online co-op is not implemented yet
- The achievement popup and list, the Customize controls screens, MSAA, Draw distance Max, Bloom
  Off and the lock-on dot need the default renderer (plume); Max and MSAA 8x cost some speed on
  slower graphics cards and at 4K
- After a reset, an achievement comes back only when the game reports it again - for things
  already done in a save, that means doing them again (the same goes for ones done while
  achievements were off)

Please report bugs with the newest file from the `logs` folder.

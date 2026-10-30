**Pre-alpha 2** of the Spyro: Dawn of the Dragon PC recomp. Bring your own copy of the game
(Xbox 360, USA/Europe disc) as an `.iso` or extracted files.

New in pre-alpha 2:
- Much faster: big open areas like Avalar went from about 30 fps to well over 100 fps (smooth at
  4K)
- Lock-on camera: click the right stick (R3) near enemies to lock the camera onto one, with a
  white dot on the target; click again to let go (Recomp Settings: Lock-on camera On/Off)
- Bloom setting: Original or Off
- Quit to Desktop button on the Game Menu and the Pause screen

**How to play:** download `SpyroRecomp-{VERSION}.zip` below, unzip it, double-click
`SpyroRecomp.exe` and pick your `.iso` (or `default.xex` of an extracted game). Settings are in the
game's menus under *Recomp Settings*. If Windows shows "Windows protected your PC" (the program is
not digitally signed), click **More info → Run anyway**.

What works:
- Native PC build with its own Direct3D 12 renderer (plume)
- Resolution up to 4K, frame rate 30/60/120/unlimited, VSync, windowed/borderless/fullscreen,
  motion blur and anti-aliasing options, all inside the game's own menus
- Game shaders ship pre-translated; any shader missing from that cache is translated automatically
  on first use and kept for next time
- Played through the first three levels

Known issues:
- Only the first three levels are tested; later areas may still have rendering bugs (try Renderer:
  Xenos if something looks wrong, and please report where)
- Draw distance (Max) and online co-op are not implemented yet
- Bloom Off and the lock-on dot need the default renderer (plume)

Please report bugs with the newest file from the `logs` folder.

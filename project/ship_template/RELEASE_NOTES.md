**Pre-alpha 2.3 - the MSAA update** of the Spyro: Dawn of the Dragon PC recomp. Bring your own copy
of the game (Xbox 360, USA/Europe disc) as an `.iso` or extracted files.

New in pre-alpha 2.3:
- **MSAA anti-aliasing**: Recomp Settings -> Anti-aliasing -> Off / MSAA 2x / MSAA 4x / MSAA 8x.
  Smoother edges on the 3D scene (less jagged, less shimmering on thin poles, fences and leaves).
  4x is what the Xbox 360 version used. If your graphics card can't do the chosen level, the game
  uses the highest one it can. Needs the default renderer (plume)
- The old FXAA choices are gone (they never worked with the default renderer)

Also in this build:
- **Draw distance: Max** with no pop-in, and without the black areas that pre-alpha 2-1 showed in
  level 1 (the 2-1 hotfix)
- **Quit to Desktop** fades in and out with the menu and is no longer sometimes see-through

**This is the last update for a while** - a few days to a few weeks. The next major update will
bring **achievements**, more bug fixes, more testing, a refined **lock-on camera**, better
**stability**, and the start of work on **online co-op**.

**How to play:** download `SpyroRecomp-{VERSION}.zip` below, unzip it, double-click
`SpyroRecomp.exe` and pick your `.iso` (or `default.xex` of an extracted game). Settings are in the
game's menus under *Recomp Settings*. If Windows shows "Windows protected your PC" (the program is
not digitally signed), click **More info -> Run anyway**.

What works:
- Native PC build with its own Direct3D 12 renderer (plume)
- Resolution up to 4K, frame rate 30/60/120/unlimited, VSync, windowed/borderless/fullscreen,
  motion blur, MSAA, bloom and draw distance options, all inside the game's own menus
- Lock-on camera (R3) and a Quit to Desktop button on the Game Menu and the Pause screen
- Game shaders ship pre-translated; any shader missing from that cache is translated automatically
  on first use and kept for next time
- Played through the first three levels

Known issues:
- Only the first three levels are fully tested; later areas may still have rendering bugs (try
  Renderer: Xenos if something looks wrong, and please report where)
- Online co-op is not implemented yet
- MSAA, Draw distance Max, Bloom Off and the lock-on dot need the default renderer (plume); Max and
  MSAA 8x cost some speed on slower graphics cards and at 4K

Please report bugs with the newest file from the `logs` folder.

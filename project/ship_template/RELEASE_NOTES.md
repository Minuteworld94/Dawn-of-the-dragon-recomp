**Pre-alpha 2-1 hotfix** of the Spyro: Dawn of the Dragon PC recomp. Bring your own copy of the game
(Xbox 360, USA/Europe disc) as an `.iso` or extracted files.

Hotfix:
- **Draw distance: Max** no longer turns parts of a level black. In level 1 the whole hall behind
  the player (after one of the enemy waves) went black with Max; it now looks like Original, just
  with the longer draw distance

Also in this build (pre-alpha 2-1, a small update that fixed Draw distance and the menus):
- **Draw distance: Max now works.** No more pop-in: trees, rocks, buildings and whole hillsides are
  drawn all the way to the horizon instead of appearing as you get close (Recomp Settings ->
  Draw distance -> Max; plume renderer)
- **Quit to Desktop** button fixes: it no longer shows on top of the fire effect when the Game Menu
  opens - it now fades in and out together with the other options - and it is no longer sometimes
  see-through on the Pause screen

**How to play:** download `SpyroRecomp-{VERSION}.zip` below, unzip it, double-click
`SpyroRecomp.exe` and pick your `.iso` (or `default.xex` of an extracted game). Settings are in the
game's menus under *Recomp Settings*. If Windows shows "Windows protected your PC" (the program is
not digitally signed), click **More info -> Run anyway**.

What works:
- Native PC build with its own Direct3D 12 renderer (plume)
- Resolution up to 4K, frame rate 30/60/120/unlimited, VSync, windowed/borderless/fullscreen,
  motion blur, anti-aliasing, bloom and draw distance options, all inside the game's own menus
- Lock-on camera (R3) and a Quit to Desktop button on the Game Menu and the Pause screen
- Game shaders ship pre-translated; any shader missing from that cache is translated automatically
  on first use and kept for next time
- Played through the first three levels

Known issues:
- Only the first three levels are tested; later areas may still have rendering bugs (try Renderer:
  Xenos if something looks wrong, and please report where)
- Online co-op is not implemented yet
- Draw distance Max, Bloom Off and the lock-on dot need the default renderer (plume); Max costs
  some speed in big open views

Please report bugs with the newest file from the `logs` folder.

**Pre-alpha 2.4 - the achievements update** of the Spyro: Dawn of the Dragon PC recomp. Bring your
own copy of the game (Xbox 360, USA/Europe disc) as an `.iso` or extracted files.

New in pre-alpha 2.4 - **achievements**:
- All 50 of the game's achievements (1000 G), earned as you play, the same way as on the Xbox 360.
  They are saved with your saves (`user\achievements`)
- **Unlock popup** in the style of the Xbox 360's, but with the fire from the main menu as its
  background, and the main menu's fire whoosh instead of the 360 chime. Unlock several at once and
  they show one after another
- **Achievements list**: press **1** on the keyboard, or open **Recomp Settings -> Achievements**
  (the top row). Icons, descriptions, gamerscore and unlock dates. Up / Down scrolls, LB / RB
  pages, B or 1 closes
- **Delete** on the keyboard resets all achievements - it asks first (Yes / No, with No picked)
- The popup's fire and whoosh are taken from your own copy of the game when the Game Menu comes in
  (every way into a level passes it), and need the default renderer (plume)

Still on the way: more bug fixes and testing, a refined **lock-on camera**, better **stability**,
and the start of work on **online co-op**.

**How to play:** download `SpyroRecomp-{VERSION}.zip` below, unzip it, double-click
`SpyroRecomp.exe` and pick your `.iso` (or `default.xex` of an extracted game). Settings are in the
game's menus under *Recomp Settings*. If Windows shows "Windows protected your PC" (the program is
not digitally signed), click **More info -> Run anyway**.

What works:
- Native PC build with its own Direct3D 12 renderer (plume)
- Resolution up to 4K, frame rate 30/60/120/unlimited, VSync, windowed/borderless/fullscreen,
  motion blur, MSAA, bloom and draw distance options, all inside the game's own menus
- Achievements with popups and a list
- Lock-on camera (R3) and a Quit to Desktop button on the Game Menu and the Pause screen
- Game shaders ship pre-translated; any shader missing from that cache is translated automatically
  on first use and kept for next time
- Played through the first three levels

Known issues:
- Only the first three levels are fully tested; later areas may still have rendering bugs (try
  Renderer: Xenos if something looks wrong, and please report where)
- Online co-op is not implemented yet
- The achievement popup and list, MSAA, Draw distance Max, Bloom Off and the lock-on dot need the
  default renderer (plume); Max and MSAA 8x cost some speed on slower graphics cards and at 4K
- After a reset, an achievement comes back only when the game reports it again - for things
  already done in a save, that means doing them again

Please report bugs with the newest file from the `logs` folder.

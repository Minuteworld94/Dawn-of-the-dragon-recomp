**Pre-alpha 2.6 Linux** - the first **native Linux / Steam Deck** build of the Spyro: Dawn of the
Dragon recomp. Not Proton, not Wine: a real Linux program, drawing with our plume renderer on
**Vulkan**. Bring your own copy of the game (Xbox 360, USA/Europe disc) as an `.iso` or extracted
files.

> **Experimental.** Played on a Steam Deck OLED (SteamOS). Other 64-bit x86 Linux PCs with Vulkan
> drivers and X11 or XWayland should work too, but have not been tested yet. Please report problems
> with the files from the `logs` folder.

**Install (Steam Deck: in Desktop Mode)**
1. Download `SpyroRecomp-{VERSION}-linux.tar.gz` and unpack it **on the Deck / Linux PC itself**
   (right-click -> Extract), e.g. into `~/Games/SpyroRecomp`.
2. Copy your `.iso` next to `SpyroRecomp` and double-click `SpyroRecomp`. The ISO is unpacked once
   into the `game` folder (a few minutes, with a progress window); after that it can be deleted.
3. Gaming Mode: Steam -> *Games -> Add a Non-Steam Game to My Library* -> `SpyroRecomp`. Don't force
   Proton / a compatibility tool on it. The Deck's controls act as an Xbox 360 controller.

**New for Linux / Steam Deck**
- **Resolution 800p** - the Deck's 1280 x 800 screen
- **Aspect ratio 16:9 / 16:10** - 16:10 fills the Deck's screen: the 3D view opens a little further
  up and down instead of being stretched
- Defaults made for the Deck: 800p, 16:10, Borderless
- **`recomp_settings.ini`** next to `SpyroRecomp`: every Recomp Setting as readable text, with the
  choices listed above each one - change settings without starting the game
- The launcher finds the `.iso` next to it by itself, checks your game version, and if the game
  can't start it says why (and keeps `logs/launcher.log` and `logs/game_output.log`)
- Everything else from pre-alpha 2.5: plume renderer, MSAA, bloom / draw distance options,
  achievements, keyboard and mouse controls, lock-on camera, Quit to Desktop

**Known issues**
- Changing **Resolution in the game** can freeze it on the Steam Deck. For now, change it in
  `recomp_settings.ini` instead (close the game, edit, save, start again). All other settings are fine
  to change in the game.
- Online co-op is not there yet.
- The overlay text (achievements, Customize controls) uses a system font (Noto Sans / DejaVu Sans)
  unless you copy Segoe UI into `bin/fonts`.

Windows players: nothing changes for you in this release - keep pre-alpha 2.5 hotfix.

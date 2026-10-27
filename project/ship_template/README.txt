SPYRO: DAWN OF THE DRAGON RECOMP - pre-alpha {VERSION}
=====================================================

A native PC port of The Legend of Spyro: Dawn of the Dragon (Xbox 360), made by static
recompilation. Unofficial fan project. No game files are included: you need your own copy.

WHAT YOU NEED
  - Windows 10 or 11, 64-bit, and a graphics card with DirectX 12.
  - Your copy of the Xbox 360 game "The Legend of Spyro: Dawn of the Dragon (USA, Europe)":
    the disc image (.iso), or its extracted files (the folder with default.xex).
  - About 8 GB of free space next to this folder if you use an ISO.

HOW TO PLAY
  1. Unzip this whole folder anywhere (not inside Program Files).
  2. Double-click SpyroRecomp.exe.
  3. The first time, pick your game: the .iso file, or default.xex in an extracted game folder.
     An ISO is unpacked once into the "game" folder here (takes a few minutes).
  To pick a different copy later, hold Shift while starting SpyroRecomp.exe.
  If Windows says "Windows protected your PC": the program is not digitally signed. Click
  "More info", then "Run anyway".

SETTINGS (resolution up to 4K, frame rate, VSync, window mode, ...)
  Inside the game, under "Recomp Settings":
    - main menu: Options > Display Options > Recomp Settings
    - in a level: pause > Options > the "Recomp Settings" plank left of "Tutorial"
  The Renderer setting takes effect the next time you start the game.

WHERE THINGS ARE
  user\      your saves
  logs\      a log of each session (the last 10 are kept)
  bin\       the game program, recomp_settings.ini (your settings) and the shader caches
             (shaders not in spyro_shaders.bin are translated on first use into bin\shader_cache)

KNOWN ISSUES (pre-alpha)
  - Tested through the first two levels; later areas may still have rendering bugs.
    If something looks wrong, try Renderer: Xenos (next start) and please report where.
  - Online co-op is not there yet.

REPORTING BUGS
  Please attach the newest file from the logs folder and say what you were doing.

Not affiliated with or endorsed by Activision, Etranges Libellules, Krome Studios or Microsoft.
Third-party licenses: see the licenses folder.

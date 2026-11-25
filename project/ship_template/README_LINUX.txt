SPYRO: DAWN OF THE DRAGON RECOMP - LINUX / STEAM DECK - pre-alpha {VERSION} (EXPERIMENTAL)
====================================================================================

A native Linux port of The Legend of Spyro: Dawn of the Dragon (Xbox 360), made by static
recompilation. Unofficial fan project. No game files are included: you need your own copy.

This is the first Linux build: expect problems. When something goes wrong, please send the files
from the "logs" folder: launcher.log, game_output.log and the newest spyro_ log.

WHAT YOU NEED
  - A 64-bit Linux PC or a Steam Deck (SteamOS), with Vulkan graphics drivers (the Steam Deck has them).
  - Your copy of the Xbox 360 game "The Legend of Spyro: Dawn of the Dragon (USA, Europe)":
    the disc image (.iso), or its extracted files (the folder with default.xex).
  - About 8 GB of free space next to this folder if you use an ISO.

FIRST START (on a Steam Deck: do this in Desktop Mode)
  1. Unpack the download ON the Linux PC / Steam Deck itself (right-click the .tar.gz > Extract), somewhere
     in your home folder (for example ~/Games/SpyroRecomp). Unpacking it on Windows and copying the folder
     over loses the "this is a program" marks on the files.
  2. Easiest: copy your .iso into this folder, next to "SpyroRecomp".
  3. Double-click "SpyroRecomp" (or run ./SpyroRecomp in a terminal). If the .iso is next to it, it is
     used straight away; otherwise a window asks you to pick the .iso (or default.xex of an extracted
     game). An ISO is unpacked once into the "game" folder here (a few minutes; a window shows the
     progress). After that the .iso can be deleted.
  To pick a different copy later, start it with: ./SpyroRecomp --choose

STEAM DECK: PLAY FROM GAMING MODE
  1. In Desktop Mode, open Steam, then Games > Add a Non-Steam Game to My Library > Browse, and pick
     "SpyroRecomp" in this folder. Do NOT force a Proton / compatibility tool on it - it is a native
     Linux program.
  2. Go back to Gaming Mode: it is in your library under Non-Steam. The Deck's controls act as an
     Xbox 360 controller.

SETTINGS
  Inside the game, under "Recomp Settings" (main menu: Options > Display Options > Recomp Settings;
  in a level: pause > Options > the "Recomp Settings" plank left of "Tutorial").
  On Linux the renderer is always plume on Vulkan.
  Linux-only settings:
    - Resolution 800p: the Steam Deck's screen (1280 x 800).
    - Aspect ratio 16:9 / 16:10: 16:10 fills a 16:10 screen like the Steam Deck's - the game's view
      opens a little further up and down instead of being stretched. On a 16:9 screen use 16:9.
  The defaults are made for the Steam Deck: 800p, 16:10, Borderless.

  CHANGING SETTINGS WITHOUT THE GAME: recomp_settings.ini (next to SpyroRecomp) holds every Recomp
  Setting as readable text, with the choices listed above each one. Close the game, open the file
  with a text editor (Steam Deck: right-click > Open With > KWrite), change the text after "=" (for
  example "resolution = 1080p"), save, start the game. The in-game menu writes the same file.

WHERE THINGS ARE
  recomp_settings.ini   the Recomp Settings (see above)
  user/      your saves and achievements
  logs/      a log of each session (the last 10 are kept), launcher.log (each start of the game),
             game_output.log (what the game printed during the last session) and
             game_output_previous.log (the session before that)
  bin/       the game program, controls.ini, shader caches

KNOWN ISSUES (Linux build)
  - Changing Resolution IN THE GAME can freeze the game on the Steam Deck. For now change it in
    recomp_settings.ini instead (see above). Every other setting is fine to change in the game.
  - Not tested on many machines yet. If the game does not start or closes right away, a message says
    why: send that text plus the files in the logs folder.
  - The overlay text (achievements, Customize controls) uses Segoe UI if you copy segoeui.ttf,
    seguisb.ttf and segoeuib.ttf into a "fonts" folder inside bin/, otherwise a system font (Noto Sans,
    DejaVu Sans or Liberation Sans).
  - Online co-op is not there yet.

Not affiliated with or endorsed by Activision, Etranges Libellules, Krome Studios or Microsoft.
Third-party licenses: see the licenses folder.

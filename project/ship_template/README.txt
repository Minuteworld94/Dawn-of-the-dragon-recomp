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

KEYBOARD, MOUSE AND CONTROLLER
  Play with a keyboard and mouse, a controller, or both. Change the controls in
  Recomp Settings > Customize controls:
    - Keyboard and mouse: two keys (or mouse buttons / wheel) per controller button (each row says
      what the button does), and the mouse camera: it turns the view directly and stays where you
      leave it (on/off, sensitivity, invert). Defaults: WASD move, mouse camera (or I J K L), Space jump,
      left/right mouse button = fast/strong attack, Q E R F = triggers and bumpers, arrows = D-pad,
      Tab = Back, P = Start, middle mouse = lock-on.
      Esc always pauses (back in menus), Enter always starts / confirms.
    - Controller: swap its buttons around.
  Saved in bin\controls.ini. The screens use the game's own parchment and button pictures, taken
  from your copy of the game when it starts (kept in bin\ui_cache).

ACHIEVEMENTS
  All 50 of the game's achievements, earned as you play; a popup shows each one you unlock
  (with the main menu's fire and fire whoosh).
  List of all achievements: press 1 on the keyboard, or Recomp Settings > Achievements (top row).
    Up / Down scrolls, LB / RB pages, B or 1 closes.
  Turn achievements off: press Delete on the keyboard, then pick Yes (it asks first). While they
  are off nothing unlocks and no popups show; the ones you have stay. Press Delete again to turn
  them back on. Remembered between sessions.
  Reset all achievements: press Insert on the keyboard, then pick Yes (it asks first; No is
  picked when it opens). A copy of the old state is kept in user\achievements as
  565507F9.before_reset.toml.
  The popup and list need the default renderer (plume).

EXTRAS
  Lock-on camera: click the right stick (R3) or the middle mouse button near enemies; click again
  to let go.
  Quit to Desktop: the plank at the bottom of the Game Menu and the Pause screen (press DOWN
  from the last stone).

WHERE THINGS ARE
  user\      your saves, and your achievements (user\achievements)
  logs\      a log of each session (the last 10 are kept)
  bin\       the game program, recomp_settings.ini (your settings), controls.ini and the shader caches
             (shaders not in spyro_shaders.bin are translated on first use into bin\shader_cache)

KNOWN ISSUES (pre-alpha)
  - Keyboard and mouse play is VERY experimental: it still needs tuning and has bugs. The mouse
    camera does not steer around walls by itself.
  - Tested through the first three levels; later areas may still have rendering bugs.
    If something looks wrong, try Renderer: Xenos (next start) and please report where.
  - Online co-op is not there yet.
  - Draw distance "Max" (no pop-in) needs the default renderer (plume) and costs some speed in
    big open views.

REPORTING BUGS
  Please attach the newest file from the logs folder and say what you were doing.

Not affiliated with or endorsed by Activision, Etranges Libellules, Krome Studios or Microsoft.
Third-party licenses: see the licenses folder.

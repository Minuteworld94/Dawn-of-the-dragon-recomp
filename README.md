# Spyro: Dawn of the Dragon Recomp

A native PC port of **The Legend of Spyro: Dawn of the Dragon** (Xbox 360) for **Windows** and
**Linux / Steam Deck**, made by *static recompilation*: the game's PowerPC code is translated to C++
ahead of time with the [reXglue SDK](https://github.com/rexglue/rexglue-sdk) and compiled into a
normal program. Graphics run on **plume**, our own renderer that replaces the game's Xbox 360
Direct3D layer (Direct3D 12 on Windows, Vulkan on Linux), instead of emulating the Xbox 360 GPU.

> **This recomp is made with claude with bugtesting, feature suggestions and overall guidance done by me.**
> if you have a problem with that, **ignore this project.** I will not tolerate crying about AI usage in
> **any form** and issues pertaining to this will be swiftly closed. I like working with claude on these
> and my prompts have thought put into them. the game runs and looks incredible at this stage. I know what I
> am doing. any future decompilation/recompilation projects by me on this site will be done with claude.

> **Pre-alpha.** It boots, plays and has been played through the first three levels by me. youtuber Rafaelão
> has beaten the game with minimal issues but you should still expect bugs. Bug reports with the newest file from the `logs` folder are very welcome.

**You need your own copy of the game.** This project contains no game files. It works with the
Xbox 360 disc *The Legend of Spyro: Dawn of the Dragon (USA, Europe)*, as a disc image (`.iso`) or
as extracted game files.

## Playing

1. Download the latest release zip from the [Releases](../../releases) page and unzip it anywhere
   (keep about 8 GB free if you will give it an ISO).
2. Double-click **`SpyroRecomp.exe`**.
3. The first time, pick your game: the `.iso` file, or `default.xex` inside an extracted game
   folder. An ISO is unpacked once into the `game` folder next to the launcher.

Hold **Shift** while starting `SpyroRecomp.exe` to pick a different copy of the game later.

### Linux / Steam Deck (experimental)

A native Linux build (not Proton): plume on Vulkan, no Xbox 360 GPU emulation. Tested on a Steam
Deck OLED (SteamOS); other 64-bit x86 Linux PCs with Vulkan drivers and X11 or XWayland should work.

1. Download the `-linux.tar.gz` release and unpack it **on the Linux machine** (right-click →
   Extract), e.g. into `~/Games/SpyroRecomp`.
2. Put your `.iso` next to `SpyroRecomp` and double-click `SpyroRecomp` (Steam Deck: in Desktop
   Mode). It is unpacked once into the `game` folder.
3. Steam Deck Gaming Mode: Steam → *Add a Non-Steam Game* → `SpyroRecomp` (no Proton / compatibility
   tool - it is a native program).

Linux-only settings: Resolution **800p** (the Deck's 1280 x 800) and **Aspect ratio** 16:9 / 16:10
(16:10 shows more above and below instead of stretching). Defaults: 800p, 16:10, Borderless.
`recomp_settings.ini` next to `SpyroRecomp` holds every Recomp Setting and can be edited while the
game is closed. **Known issue:** changing Resolution in the game can freeze it on the Steam Deck -
change it in `recomp_settings.ini` for now. See the `README.txt` in the download for more.

### Settings

PC settings live inside the game's own menus, under **Recomp Settings**:

- main menu: *Options → Display Options → Recomp Settings*
- in a level: pause → *Options* → the **Recomp Settings** plank left of *Tutorial*

| Setting | Choices |
|---|---|
| Achievements | (a button) opens the achievements list |
| Customize controls | (a button) keyboard and mouse bindings, mouse camera, controller button swaps |
| Resolution | 720p, 1080p, 1440p, 4K |
| Renderer | plume (default), Xenos, Xenos - Vulkan (takes effect on the next start) |
| Frame rate | 30, 60, 120, Unlimited |
| VSync | On, Off |
| Display mode | Windowed, Borderless, Fullscreen |
| Motion blur | On, Off |
| Anti-aliasing | Off, MSAA 2x, MSAA 4x, MSAA 8x (plume renderer; 4x = what the Xbox 360 used) |
| Bloom | Original, Off (plume renderer) |
| Draw distance | Original, Max (no pop-in: the whole level is drawn to the horizon; plume renderer) |
| Lock-on camera | On, Off |
| Online co-op | coming later |

Settings are saved in `bin\recomp_settings.ini` (Linux: `recomp_settings.ini` next to
`SpyroRecomp`) - readable, with the choices listed above each setting, so it can also be edited by
hand while the game is closed. Controls are in `bin\controls.ini`, saves in `user`, logs in `logs`.

### Keyboard, mouse and controller

The game plays with a keyboard and mouse as well as a controller (both at once is fine). Change
anything in *Recomp Settings → Customize controls*:

- **Keyboard and mouse**: up to two keys (or mouse buttons / the wheel) for every controller button
  and stick direction (each row says what the button does in the game); the mouse turns the camera
  directly during gameplay, like a PC game, and it stays where you leave it (sensitivity and invert
  options; the controller's right stick gives the camera back to the game).
  Default: WASD move, mouse camera (or I J K L), Space jump, left / right mouse fast / strong attack,
  Q E R F triggers and bumpers, arrows D-pad, Tab Back, P Start, middle mouse lock-on.
  **Esc** always pauses (and goes back in menus), **Enter** always starts / confirms.
- **Controller**: swap the buttons around (choosing a button that is in use swaps the two).

The screens use the game's own parchment and button pictures, taken from your copy of the game when
it starts: the parchment from its data files, the button icons from its fonts (nothing from the game
is shipped; they are kept in `bin\ui_cache`).

### Achievements

All 50 of the game's achievements (1000 G) are earned as you play: the game reports them the way
it did on the Xbox 360, and they are saved in `user\achievements`.

- **Popup**: Xbox 360 style, with the main menu's fire as its background and the menu's fire whoosh
  as its sound. Both are taken from your own copy of the game when the Game Menu comes in (nothing
  from the game is shipped). Several unlocks at once show one after another.
- **List**: press **1** on the keyboard, or *Recomp Settings → Achievements*. Up / Down scrolls,
  LB / RB pages, B or 1 closes.
- **Turn off**: press **Delete** and pick *Yes*. While achievements are off, nothing unlocks and no
  popups appear (the ones you already have stay). Press **Delete** again to turn them back on.
  Remembered between sessions (`user\achievements\achievements_off.txt` while off).
- **Reset**: press **Insert** and pick *Yes* (No is picked when the question opens). The old state
  is kept as `user\achievements\565507F9.before_reset.toml`.

The popup and the list are drawn by the plume renderer.

### Extras

- **Lock-on camera**: click the right stick (R3, middle mouse on the keyboard) near enemies to lock the camera onto the nearest
  one (a white dot marks it); click again to let go. Turn it off in Recomp Settings to give R3
  back to the game.
- **Quit to Desktop**: a plank at the bottom of the Game Menu and the Pause screen - press DOWN
  from the last stone to select it.

### Shaders

plume draws with the game's own shaders, translated from Xbox 360 microcode to Direct3D 12 by
SpyroShaderRecomp. The release ships them pre-translated (`bin\spyro_shaders.bin`). Any shader
that is not in there is translated by the game itself the first time it is created (about 15 ms
each, on background threads) and kept in `bin\shader_cache`, so it is only done once.

### Known issues

- Keyboard and mouse play is very experimental: it still needs tuning and has bugs. The mouse
  camera does not steer around walls by itself.
- With the keyboard, player 1 always counts as connected (the game no longer notices a controller
  being unplugged).
- Gameplay has been tested through the first three levels; later areas may still have rendering bugs.
- Online co-op is not implemented yet.
- Draw distance Max costs some speed in big open views (it draws far more), and needs the plume
  renderer.
- After an achievements reset, an achievement comes back only when the game reports it again (for
  things already done in a save, that means doing them again).
- Only the USA/Europe release has been tested; the launcher warns about other versions.

## Building from source

Windows 10/11 x64. Everything is portable (nothing is installed system-wide).

1. **Toolchain**: from the repository root in PowerShell:
   ```powershell
   TOOLS\_scripts\fetch-binaries.ps1     # clang, cmake, ninja, git, dxc, 7-Zip (+ optional research tools)
   python TOOLS\_scripts\portable-msvc.py # MSVC libraries + Windows SDK into TOOLS\msvc (accepts the VS license)
   TOOLS\_scripts\fetch-sources.ps1      # reXglue SDK, plume, XenosRecomp, ...
   TOOLS\_scripts\apply-patches.ps1      # pins those to the tested commits + applies patches\*.patch
   . .\TOOLS\env.ps1                     # puts the toolchain on PATH for this shell
   ```
2. **reXglue SDK**:
   ```powershell
   cd TOOLS\src\rexglue-sdk
   cmake --preset win-amd64 -DREXGLUE_USE_D3D12=ON -DREXGLUE_USE_VULKAN=ON
   cmake --build --preset win-amd64-release
   cd ..\..\..
   ```
3. **Shader translator** (SpyroShaderRecomp, our XenosRecomp fork):
   ```powershell
   cmake -S project\shader_recomp -B TOOLS\build\SpyroShaderRecomp -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl
   cmake --build TOOLS\build\SpyroShaderRecomp --target SpyroShaderRecomp
   ```
4. **Recompile the game code** from *your* `default.xex`: copy it to
   `project\spyro_recomp\assets\default.xex`, then
   ```powershell
   cd project\spyro_recomp
   ..\..\TOOLS\src\rexglue-sdk\out\win-amd64\Release\rexglue.exe codegen spyrodotd_manifest.toml
   ```
   This writes `project\spyro_recomp\generated` (~100 MB of C++; never commit it).
5. **Build the game and the launcher**:
   ```powershell
   cmake --preset win-amd64-release "-DREXSDK_DIR=$PWD\..\..\TOOLS\src\rexglue-sdk" "-DCMAKE_CXX_FLAGS=-march=x86-64-v2"
   cmake --build --preset win-amd64-release
   ```
6. **Shader cache** (optional - without it the game translates every shader itself on first start):
   take `bin\spyro_shaders.bin` from a release, or build it from shaders captured while playing
   (`project\run\shaders`):
   `TOOLS\build\SpyroShaderRecomp\SpyroShaderRecomp.exe project\run\shaders project\run\spyro_shaders.bin project\shader_recomp\shader_common.h`
7. **Package**: `project\make-ship.ps1` assembles a release folder + zip like the published ones.
   For development, `project\PLAY.bat` runs the game against the extracted game in `extracted game\`.

### Linux build (cross-compiled on Windows)

The Linux build is made on Windows with clang + lld against the Steam Runtime 3 "sniper" SDK
(glibc 2.31, so it runs on SteamOS and most current distributions):

1. Get the sniper SDK sysroot tarball (`steamrt-sniper-sdk-sysroot-*.tar.gz` from Valve's Steam
   Runtime downloads) and unpack it with `TOOLS\linux\extract_sysroot.py` into `TOOLS\linux-sysroot`;
   the Linux DirectXShaderCompiler release into `TOOLS\dxc-linux`; build the small pkg-config with
   `TOOLS\linux\pkgconfig\build.ps1`.
2. Build the reXglue SDK and the game with the `linux-cross-release` presets (toolchain file
   `TOOLS\linux\toolchain-x86_64-linux.cmake`; never pass `CMAKE_CXX_FLAGS` - it replaces the
   toolchain's flags):
   ```powershell
   cd project\spyro_recomp
   cmake --preset linux-cross-release "-DREXSDK_DIR=$PWD\..\..\TOOLS\src\rexglue-sdk"
   cmake --build --preset linux-cross-release
   ```
   The generated code from the Windows codegen step is shared (codegen is skipped when cross-compiling).
3. **Package**: `project\make-ship-linux.ps1 -Version <v>` writes the `.tar.gz` (Linux file modes kept).

## Repository layout

| Path | What |
|---|---|
| `project/spyro_recomp/src` | Everything hand-written: renderer (`plume_renderer.cpp`, `plume_hooks.cpp`), runtime shader translation (`shader_translator.cpp`), Recomp Settings menu (`spyro_hooks.cpp`, `spyro_settings.cpp`), achievements overlay (`spyro_achievements.cpp`, `spyro_ui.cpp`, `spyro_sound.cpp`), keyboard / mouse / controller bindings (`spyro_controls.cpp`, `test_input.cpp`), reading the game's `.KXE` data files (`spyro_kxe.cpp`), texture decoding, debug tools |
| `project/spyro_recomp/*.toml` | reXglue codegen manifest and function boundaries |
| `project/launcher` | `SpyroRecomp.exe` / Linux `SpyroRecomp` (`launcher_linux.cpp`, SDL3): game picker, ISO unpacker (XDVDFS), version check |
| `project/spyro_recomp/src/spyro_platform.*` | What the game needs from the operating system (Windows / Linux) |
| `TOOLS/linux` | Linux cross-compile toolchain file, sysroot extractor, a small pkg-config |
| `project/shader_recomp` | SpyroShaderRecomp, a fork of XenosRecomp adapted to this game |
| `project/analysis` | Research scripts (Ghidra scripts, test runner `autotest-shots.ps1`, ...) |
| `project/run/tests` | Scripted-controller test inputs |
| `patches` | Our changes to the reXglue SDK and plume (applied by `apply-patches.ps1`) |
| `TOOLS` | Toolchain scripts (downloads go here, ignored by git) |
| `docs` | The development log and project overview |

## Credits

- [reXglue SDK](https://github.com/rexglue/rexglue-sdk) by Tom Clay and contributors, derived from
  the [Xenia](https://xenia.jp) project: recompiler and runtime.
- [plume](https://github.com/renderbag/plume) by renderbag and contributors: rendering hardware interface.
- [XenosRecomp](https://github.com/hedge-dev/XenosRecomp) by hedge-dev and contributors: Xbox 360
  shader translation (forked here).
- [Unleashed Recompiled](https://github.com/hedge-dev/UnleashedRecomp): the reference for replacing
  a game's Direct3D layer on plume.
- Research with [Ghidra](https://github.com/NationalSecurityAgency/ghidra) and
  [XEXLoaderWV](https://github.com/zeroKilo/XEXLoaderWV).

See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for licenses.

## Legal

This is an unofficial fan project. It is not affiliated with or endorsed by Activision, Étranges
Libellules, Krome Studios or Microsoft. *Spyro* is a trademark of Activision. This repository
contains no game code or assets; to play you must own the game and provide your own copy.

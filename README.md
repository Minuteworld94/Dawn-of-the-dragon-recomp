# Spyro: Dawn of the Dragon Recomp

A native Windows PC port of **The Legend of Spyro: Dawn of the Dragon** (Xbox 360), made by
*static recompilation*: the game's PowerPC code is translated to C++ ahead of time with the
[reXglue SDK](https://github.com/rexglue/rexglue-sdk) and compiled into a normal Windows program.
Graphics run on **plume**, our own Direct3D 12 renderer that replaces the game's Xbox 360 Direct3D
layer, instead of emulating the Xbox 360 GPU.

> **Pre-alpha.** It boots, plays and has been played through the first two levels. Expect bugs.
> Bug reports with the newest file from the `logs` folder are very welcome.

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

### Settings

PC settings live inside the game's own menus, under **Recomp Settings**:

- main menu: *Options → Display Options → Recomp Settings*
- in a level: pause → *Options* → the **Recomp Settings** plank left of *Tutorial*

| Setting | Choices |
|---|---|
| Resolution | 720p, 1080p, 1440p, 4K |
| Renderer | plume (default), Xenos, Xenos - Vulkan (takes effect on the next start) |
| Frame rate | 30, 60, 120, Unlimited |
| VSync | On, Off |
| Display mode | Windowed, Borderless, Fullscreen |
| Motion blur | On, Off |
| Anti-aliasing | Off, FXAA, FXAA Extreme |
| Online co-op | coming later |

Settings are saved in `bin\recomp_settings.ini`, saves in `user`, logs in `logs`.

### Shaders

plume draws with the game's own shaders, translated from Xbox 360 microcode to Direct3D 12 by
SpyroShaderRecomp. The release ships them pre-translated (`bin\spyro_shaders.bin`). Any shader
that is not in there is translated by the game itself the first time it is created (about 15 ms
each, on background threads) and kept in `bin\shader_cache`, so it is only done once.

### Known issues

- Gameplay has been tested through the first two levels; later areas may still have rendering bugs.
- Online co-op is not implemented yet.
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

## Repository layout

| Path | What |
|---|---|
| `project/spyro_recomp/src` | Everything hand-written: renderer (`plume_renderer.cpp`, `plume_hooks.cpp`), runtime shader translation (`shader_translator.cpp`), Recomp Settings menu (`spyro_hooks.cpp`, `spyro_settings.cpp`), texture decoding, debug tools |
| `project/spyro_recomp/*.toml` | reXglue codegen manifest and function boundaries |
| `project/launcher` | `SpyroRecomp.exe`: game picker, ISO unpacker (XDVDFS), version check |
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

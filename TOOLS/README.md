# TOOLS — Spyro: Dawn of the Dragon (X360) recomp toolchain

Everything here is portable. Nothing is installed system-wide.
Open a PowerShell in the project root and run:

```powershell
. .\TOOLS\env.ps1
```

That puts Clang, CMake, Ninja, Git, DXC, Java and the MSVC/Windows SDK libs on PATH for that shell.

Re-fetch or update: `_scripts\fetch-binaries.ps1` (binaries) and `_scripts\fetch-sources.ps1` (git repos).
Both scripts skip anything already present. Raw archives are kept in `_downloads\` (and MSVC's in `downloads\`). Both can be deleted to free ~2.3 GB.

## Build toolchain

| Folder | What | Version |
|---|---|---|
| `llvm\` | Clang/clang-cl/lld (reXglue requires Clang 18+; MSVC/GCC are rejected) | 23.1.4 |
| `msvc\` | MSVC STL/CRT libs + Windows SDK, pulled via `_scripts\portable-msvc.py` (VS license accepted) | MSVC 14.44.35207, SDK 10.0.26100.0 |
| `cmake\` | CMake | 4.4.4 |
| `ninja\` | Ninja | 1.13.2 |
| `git\` | MinGit | 2.56.0.3 |
| `7zip\` | 7-Zip standalone (`7za.exe`) | 26.05 |
| `dxc\` | DirectX Shader Compiler (HLSL → DXIL / SPIR-V) | 1.9.2609 |
| `src\vcpkg\` | vcpkg, in case we need extra libraries | latest |

## Recompilation SDK

| Folder | What |
|---|---|
| `src\rexglue-sdk\` | **reXglue SDK, `development` branch** (bleeding edge, newer than the latest release). We build from this. |
| `src\rexglue-sdk\out\win-amd64\Release\` | **Our build** of that branch (`rexglue.exe` v0.10.0.24-dev.gbd833a2, `rexruntime.dll`, `rexgpu-xenos.dll` with D3D12 + Vulkan). Rebuild: `cmake --preset win-amd64 -DREXGLUE_USE_D3D12=ON -DREXGLUE_USE_VULKAN=ON -DREXGLUE_BUILD_TESTS=ON` then `cmake --build --preset win-amd64-release`. PPC tests 1462/1462 pass; 1 unit test (`output_stamp_test.cpp:227`) fails due to a bad escape in the test's own string literal, not an SDK bug. |
| `rexglue-sdk-0.10.0-prebuilt\` | Official v0.10.0 Windows release (`rexglue.exe` CLI + libs). Fallback / sanity comparison only. |
| `src\XenonRecomp\` | hedge-dev PPC→C++ recompiler (reference) |
| `src\XenosRecomp\` | hedge-dev Xenos shader microcode → HLSL translator. Shader pipeline for our own renderers. |

## Rendering (the 3 menu options)

| Menu option | Source | Notes |
|---|---|---|
| **reXglue** | `src\rexglue-sdk\src\graphics\` | The SDK's built-in Xenos GPU *emulation* (PM4/EDRAM, D3D12 + Vulkan). Xenia-derived. Ships as a plugin (`rexgpu-xenos`). This is the compatibility baseline. |
| **Vulkan** | our code (to be written) | Our own high-level renderer: replace the game's statically linked D3D layer, talk to Vulkan directly. |
| **plume** | `src\plume\` | renderbag/plume RHI (D3D12 + Vulkan) — the layer Unleashed Recompiled uses. Our high-level renderer on top of it. |
| — | `src\UnleashedRecomp\` | Reference only: how a D3D-layer replacement renderer on plume is done in practice (shallow, no submodules). |

## Reverse engineering & debugging

| Folder | What |
|---|---|
| `ghidra\` | Ghidra **12.1.3** (pinned to match the XEX loader). Run `ghidra\ghidraRun.bat` after `env.ps1`. |
| `ghidra\Ghidra\Extensions\XEXLoaderWV\` | zeroKilo's XEX loader, pre-installed (copy also in `ghidra-XEXLoaderWV\`) |
| `jdk21\` | Temurin JDK 21 for Ghidra |
| `renderdoc\` | RenderDoc 1.46 (frame capture for all three renderers) |
| `xenia-canary-reference\` | Xenia Canary binary: compare our output against real 360 behaviour. **Not linked into the port.** |
| `src\xenia-canary\` | Xenia source (shallow): reference for Xenos formats / kernel behaviour only. |
| `extract-xiso\` | Extracts game files from a disc ISO |

## Not here yet (on purpose)

- **Vulkan SDK / validation layers.** The LunarG SDK is an installer. Headers and the loader already come with reXglue's submodules. If we want validation layers for debugging our Vulkan renderer, we can build them or install the SDK later.
- **The game.** Your own dump of Spyro: Dawn of the Dragon goes in the project, not in TOOLS, and is never committed or distributed.

# Third-party notices

The release builds include or are built from the following projects. Their full license texts
are in the `licenses` folder of each release and in the projects' own repositories.

| Project | Used for | License |
|---|---|---|
| [reXglue SDK](https://github.com/rexglue/rexglue-sdk) (commit `bd833a2`, + `patches/rexglue-sdk.patch`) | recompiler, runtime (`rexruntime.dll`), Xenos GPU plugin (`rexgpu-xenos.dll`) | BSD 3-Clause; portions © Xenia project contributors |
| [Xenia](https://github.com/xenia-project/xenia) | origin of large parts of the reXglue runtime | BSD 3-Clause |
| [plume](https://github.com/renderbag/plume) (commit `0ce184b`, + `patches/plume.patch`) | rendering hardware interface of the plume renderer | MIT |
| [D3D12 Memory Allocator](https://github.com/GPUOpen-LibrariesAndSDKs/D3D12MemoryAllocator) | GPU memory, via plume | MIT |
| [XenosRecomp](https://github.com/hedge-dev/XenosRecomp) (commit `990d03b`, forked in `project/shader_recomp`) | translating the game's shaders | MIT |
| [DirectX Shader Compiler](https://github.com/microsoft/DirectXShaderCompiler) (`dxcompiler.dll`, `dxil.dll`, v1.9.2609) | compiling translated shaders, at build time and in the game for shaders missing from the cache | LLVM Release License (dxcompiler) / Microsoft (dxil) |
| Microsoft Visual C++ runtime (`msvcp140*.dll`, `vcruntime140*.dll`) | C++ runtime shipped next to the game | Microsoft Visual Studio redistributable terms |

*The Legend of Spyro: Dawn of the Dragon* is © Activision. No part of the game is included in
this repository.

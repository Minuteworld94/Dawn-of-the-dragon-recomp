# SpyroShaderRecomp - our fork of XenosRecomp (MIT, hedge-dev; see LICENSE_XenosRecomp.md),
# forked from TOOLS\src\XenosRecomp at 990d03b (2025-08-03) and adapted to Spyro: Dawn of the
# Dragon (WORK_LOG 103). Translates the game's Xbox 360 shader containers to HLSL and compiles
# them to DXIL + SPIR-V for the plume renderer.
#
# Build (from a shell with TOOLS\env.ps1 loaded):
#   cmake -S project\shader_recomp -B TOOLS\build\SpyroShaderRecomp -G Ninja
#         -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl
#   cmake --build TOOLS\build\SpyroShaderRecomp
cmake_minimum_required(VERSION 3.20)
set(CMAKE_CXX_STANDARD 17)
project(SpyroShaderRecomp)

# Third-party libraries come from the upstream checkout (fmt, xxHash, zstd, smol-v, dxc-bin).
set(XENOS_RECOMP_THIRDPARTY_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../TOOLS/src/XenosRecomp/thirdparty")
add_subdirectory("${XENOS_RECOMP_THIRDPARTY_ROOT}" thirdparty)

if (WIN32)
    option(XENOS_RECOMP_DXIL "Generate DXIL shader cache" ON)
endif()

set(SMOLV_SOURCE_DIR "${XENOS_RECOMP_THIRDPARTY_ROOT}/smol-v/source")

add_executable(SpyroShaderRecomp
    constant_table.h
    dxc_compiler.cpp
    dxc_compiler.h
    main.cpp
    pch.h
    shader.h
    shader_code.h
    shader_recompiler.cpp
    shader_recompiler.h
    "${SMOLV_SOURCE_DIR}/smolv.cpp")

target_link_libraries(SpyroShaderRecomp PRIVATE
    Microsoft::DirectXShaderCompiler
    xxHash::xxhash
    libzstd_static
    fmt::fmt)

target_include_directories(SpyroShaderRecomp PRIVATE ${SMOLV_SOURCE_DIR})
target_precompile_headers(SpyroShaderRecomp PRIVATE pch.h)

if (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    target_compile_options(SpyroShaderRecomp PRIVATE -Wno-switch -Wno-unused-variable -Wno-null-arithmetic -fms-extensions)
endif()

if (WIN32)
    target_compile_definitions(SpyroShaderRecomp PRIVATE _CRT_SECURE_NO_WARNINGS)
    add_custom_command(TARGET SpyroShaderRecomp POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_RUNTIME_DLLS:SpyroShaderRecomp> $<TARGET_FILE_DIR:SpyroShaderRecomp>
        COMMAND_EXPAND_LISTS
    )
endif()

if (XENOS_RECOMP_DXIL)
    target_compile_definitions(SpyroShaderRecomp PRIVATE XENOS_RECOMP_DXIL)
    target_link_libraries(SpyroShaderRecomp PRIVATE Microsoft::DXIL)
endif()

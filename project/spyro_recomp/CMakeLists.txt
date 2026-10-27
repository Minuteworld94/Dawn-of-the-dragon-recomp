# spyrodotd - ReXGlue Recompiled Project
#
# SDK boilerplate lives in generated/rexglue.cmake.

cmake_minimum_required(VERSION 3.25)
project(spyrodotd LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(generated/rexglue.cmake)

# Sources
set(SPYRODOTD_SOURCES
    src/main.cpp
    src/spyro_hooks.cpp
    src/spyro_settings.cpp
    src/plume_renderer.cpp
    src/plume_hooks.cpp
    src/hang_dump.cpp
    src/xenos_texture.cpp
    src/test_input.cpp
)

if(WIN32)
    add_executable(spyrodotd WIN32 ${SPYRODOTD_SOURCES})
else()
    add_executable(spyrodotd ${SPYRODOTD_SOURCES})
endif()

rexglue_setup_target(spyrodotd GPU_PLUGINS xenos)

# plume (RHI over D3D12 / Vulkan) for our own renderer - "Renderer: plume" in Recomp Settings.
# Source: TOOLS\src\plume (TOOLS\_scripts\fetch-sources.ps1).
set(PLUME_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../TOOLS/src/plume")
add_subdirectory("${PLUME_DIR}" plume EXCLUDE_FROM_ALL)
target_link_libraries(spyrodotd PRIVATE plume)
if(WIN32)
    target_link_libraries(spyrodotd PRIVATE d3d12 dxgi dxguid)
endif()

# plume's own shaders (src/shaders/*.hlsl) -> DXIL byte arrays in headers, with the DirectX
# shader compiler from TOOLS\dxc.
set(SPYRO_DXC "${CMAKE_CURRENT_SOURCE_DIR}/../../TOOLS/dxc/bin/x64/dxc.exe")
set(SPYRO_SHADER_OUT "${CMAKE_CURRENT_BINARY_DIR}/shaders")
file(MAKE_DIRECTORY "${SPYRO_SHADER_OUT}")
set(SPYRO_BLIT_HLSL "${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/present_blit.hlsl")
add_custom_command(
    OUTPUT "${SPYRO_SHADER_OUT}/present_blit_vs.h" "${SPYRO_SHADER_OUT}/present_blit_ps.h"
    COMMAND "${SPYRO_DXC}" -nologo -T vs_6_0 -E VSMain -Vn g_present_blit_vs -Fh "${SPYRO_SHADER_OUT}/present_blit_vs.h" "${SPYRO_BLIT_HLSL}"
    COMMAND "${SPYRO_DXC}" -nologo -T ps_6_0 -E PSMain -Vn g_present_blit_ps -Fh "${SPYRO_SHADER_OUT}/present_blit_ps.h" "${SPYRO_BLIT_HLSL}"
    DEPENDS "${SPYRO_BLIT_HLSL}"
    COMMENT "Compiling plume present shader")
add_custom_target(spyro_plume_shaders DEPENDS "${SPYRO_SHADER_OUT}/present_blit_vs.h" "${SPYRO_SHADER_OUT}/present_blit_ps.h")
add_dependencies(spyrodotd spyro_plume_shaders)
target_include_directories(spyrodotd PRIVATE "${SPYRO_SHADER_OUT}")

# Runtime shader translation (WORK_LOG 121): shaders missing from spyro_shaders.bin are translated
# in the game with SpyroShaderRecomp's translator (project\shader_recomp) and dxcompiler.dll.
# shader_common.h (the HLSL put in front of every translated shader) is embedded as a string.
set(SPYRO_SHADER_RECOMP "${CMAKE_CURRENT_SOURCE_DIR}/../shader_recomp")
file(READ "${SPYRO_SHADER_RECOMP}/shader_common.h" SPYRO_SHADER_COMMON_TEXT)
file(WRITE "${SPYRO_SHADER_OUT}/spyro_shader_common.inc"
    "// Generated from project/shader_recomp/shader_common.h by CMakeLists.txt - do not edit.\n"
    "inline constexpr char kSpyroShaderCommon[] = R\"SPYROHLSL(${SPYRO_SHADER_COMMON_TEXT})SPYROHLSL\";\n")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${SPYRO_SHADER_RECOMP}/shader_common.h")
target_sources(spyrodotd PRIVATE src/shader_translator.cpp "${SPYRO_SHADER_RECOMP}/shader_recompiler.cpp")
set_source_files_properties("${SPYRO_SHADER_RECOMP}/shader_recompiler.cpp" PROPERTIES
    COMPILE_DEFINITIONS SPYRO_SHADER_IN_GAME
    COMPILE_OPTIONS "-include;${SPYRO_SHADER_RECOMP}/pch.h;-Wno-switch;-Wno-unused-variable;-Wno-unused-parameter;-Wno-sign-compare;-Wno-null-arithmetic")
target_include_directories(spyrodotd PRIVATE "${SPYRO_SHADER_RECOMP}"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../TOOLS/dxc/inc")
# The compiler DLLs the game loads on first use (dxil.dll signs the DXIL) go next to the exe.
set(SPYRO_DXC_BIN "${CMAKE_CURRENT_SOURCE_DIR}/../../TOOLS/dxc/bin/x64")
add_custom_command(TARGET spyrodotd POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${SPYRO_DXC_BIN}/dxcompiler.dll" "${SPYRO_DXC_BIN}/dxil.dll"
            "$<TARGET_FILE_DIR:spyrodotd>")

# The launcher players start (asks for the game ISO / folder, then starts spyrodotd.exe).
# Static C runtime: it must run before anything else is installed.
if(WIN32)
    add_executable(SpyroRecomp WIN32 ../launcher/launcher.cpp)
    set_property(TARGET SpyroRecomp PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded")
    target_link_libraries(SpyroRecomp PRIVATE bcrypt comctl32 ole32 shell32)
endif()

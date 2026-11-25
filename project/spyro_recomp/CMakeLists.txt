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
    src/spyro_lockon.cpp
    src/spyro_profiler.cpp
    src/guest_memory.cpp
    src/spyro_drawdist.cpp
    src/spyro_ui.cpp
    src/spyro_achievements.cpp
    src/spyro_sound.cpp
    src/spyro_controls.cpp
    src/spyro_kxe.cpp
    src/spyro_platform.cpp
)

if(WIN32)
    add_executable(spyrodotd WIN32 ${SPYRODOTD_SOURCES})
else()
    add_executable(spyrodotd ${SPYRODOTD_SOURCES})
endif()

if(WIN32)
    rexglue_setup_target(spyrodotd GPU_PLUGINS xenos)
else()
    # Linux (WORK_LOG 156): plume on its Vulkan backend is the only renderer - no Xenos GPU plugin is
    # loaded or shipped (the SDK insists on compiling one graphics backend, so its Vulkan one is
    # built, but nothing uses it).
    rexglue_setup_target(spyrodotd)
endif()

# Linux / Steam Deck port options (WORK_LOG 159): Resolution 800p and an Aspect ratio setting
# (16:9 / 16:10) in Recomp Settings. On for Linux; -DSPYRO_DECK_OPTIONS=ON tests them on Windows.
option(SPYRO_DECK_OPTIONS "800p resolution and the Aspect ratio setting (always on for Linux)" OFF)
if(NOT WIN32 OR SPYRO_DECK_OPTIONS)
    target_compile_definitions(spyrodotd PRIVATE SPYRO_DECK_OPTIONS)
endif()

# Symbol map next to the exe (spyrodotd.map): names the addresses the sampling profiler logs
# (src/spyro_profiler.cpp, WORK_LOG 124).
if(WIN32)
    target_link_options(spyrodotd PRIVATE "LINKER:/MAP")
endif()

# plume (RHI over D3D12 / Vulkan) for our own renderer - "Renderer: plume" in Recomp Settings.
# Source: TOOLS\src\plume (TOOLS\_scripts\fetch-sources.ps1).
set(PLUME_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../TOOLS/src/plume")
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # Linux: plume makes its Vulkan surface from reXglue's SDL3 window (X11 and Wayland alike).
    set(PLUME_SDL3_VULKAN_ENABLED ON CACHE BOOL "plume: SDL3 Vulkan surfaces" FORCE)
endif()
add_subdirectory("${PLUME_DIR}" plume EXCLUDE_FROM_ALL)
target_link_libraries(spyrodotd PRIVATE plume)
if(NOT WIN32)
    # spyro_platform.cpp: keyboard / mouse / focus / sound through SDL3 (the SDK builds it static).
    if(TARGET SDL3::SDL3)
        target_link_libraries(spyrodotd PRIVATE SDL3::SDL3)
    else()
        target_link_libraries(spyrodotd PRIVATE SDL3::SDL3-static)
    endif()
    target_link_libraries(spyrodotd PRIVATE ${CMAKE_DL_LIBS})  # dlopen (libdxcompiler.so)
endif()
# xxHash (header only) for plume's vertex cache; stb_truetype (ImGui's bundled copy) and stb_image
# (header only, compiled privately in spyro_ui.cpp / spyro_achievements.cpp) for the overlay UI.
target_include_directories(spyrodotd PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../../TOOLS/src/rexglue-sdk/thirdparty/xxHash"
                           "${CMAKE_CURRENT_SOURCE_DIR}/../../TOOLS/src/rexglue-sdk/thirdparty/imgui"
                           "${CMAKE_CURRENT_SOURCE_DIR}/../../TOOLS/src/rexglue-sdk/thirdparty/stb")
if(WIN32)
    target_link_libraries(spyrodotd PRIVATE d3d12 dxgi dxguid)
endif()

# plume's own shaders (src/shaders/*.hlsl) -> DXIL (D3D12) and SPIR-V (Vulkan, WORK_LOG 156) byte
# arrays in headers, with the DirectX shader compiler from TOOLS\dxc (the build machine's: dxc.exe
# on Windows, TOOLS/dxc-linux on Linux).
if(CMAKE_HOST_WIN32)
    set(SPYRO_DXC "${CMAKE_CURRENT_SOURCE_DIR}/../../TOOLS/dxc/bin/x64/dxc.exe")
else()
    set(SPYRO_DXC "${CMAKE_CURRENT_SOURCE_DIR}/../../TOOLS/dxc-linux/bin/dxc")
endif()
set(SPYRO_SHADER_OUT "${CMAKE_CURRENT_BINARY_DIR}/shaders")
file(MAKE_DIRECTORY "${SPYRO_SHADER_OUT}")
# SPIR-V: the arguments SpyroShaderRecomp uses for the game shaders (vertex shaders flip y).
set(SPYRO_SPIRV -spirv -fvk-use-dx-layout)
set(SPYRO_BLIT_HLSL "${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/present_blit.hlsl")
add_custom_command(
    OUTPUT "${SPYRO_SHADER_OUT}/present_blit_vs.h" "${SPYRO_SHADER_OUT}/present_blit_ps.h"
           "${SPYRO_SHADER_OUT}/present_blit_depthms_ps.h"
           "${SPYRO_SHADER_OUT}/present_blit_vs_spv.h" "${SPYRO_SHADER_OUT}/present_blit_ps_spv.h"
           "${SPYRO_SHADER_OUT}/present_blit_depthms_ps_spv.h"
    COMMAND "${SPYRO_DXC}" -nologo -T vs_6_0 -E VSMain -Vn g_present_blit_vs -Fh "${SPYRO_SHADER_OUT}/present_blit_vs.h" "${SPYRO_BLIT_HLSL}"
    COMMAND "${SPYRO_DXC}" -nologo -T ps_6_0 -E PSMain -Vn g_present_blit_ps -Fh "${SPYRO_SHADER_OUT}/present_blit_ps.h" "${SPYRO_BLIT_HLSL}"
    COMMAND "${SPYRO_DXC}" -nologo -T ps_6_0 -E PSDepthMS -Vn g_present_blit_depthms_ps -Fh "${SPYRO_SHADER_OUT}/present_blit_depthms_ps.h" "${SPYRO_BLIT_HLSL}"
    COMMAND "${SPYRO_DXC}" -nologo ${SPYRO_SPIRV} -fvk-invert-y -T vs_6_0 -E VSMain -Vn g_present_blit_vs_spv -Fh "${SPYRO_SHADER_OUT}/present_blit_vs_spv.h" "${SPYRO_BLIT_HLSL}"
    COMMAND "${SPYRO_DXC}" -nologo ${SPYRO_SPIRV} -T ps_6_0 -E PSMain -Vn g_present_blit_ps_spv -Fh "${SPYRO_SHADER_OUT}/present_blit_ps_spv.h" "${SPYRO_BLIT_HLSL}"
    COMMAND "${SPYRO_DXC}" -nologo ${SPYRO_SPIRV} -T ps_6_0 -E PSDepthMS -Vn g_present_blit_depthms_ps_spv -Fh "${SPYRO_SHADER_OUT}/present_blit_depthms_ps_spv.h" "${SPYRO_BLIT_HLSL}"
    DEPENDS "${SPYRO_BLIT_HLSL}"
    COMMENT "Compiling plume present shader")
set(SPYRO_UI_HLSL "${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/ui_overlay.hlsl")
add_custom_command(
    OUTPUT "${SPYRO_SHADER_OUT}/ui_overlay_vs.h" "${SPYRO_SHADER_OUT}/ui_overlay_ps.h"
           "${SPYRO_SHADER_OUT}/ui_overlay_vs_spv.h" "${SPYRO_SHADER_OUT}/ui_overlay_ps_spv.h"
    COMMAND "${SPYRO_DXC}" -nologo -T vs_6_0 -E VSMain -Vn g_ui_overlay_vs -Fh "${SPYRO_SHADER_OUT}/ui_overlay_vs.h" "${SPYRO_UI_HLSL}"
    COMMAND "${SPYRO_DXC}" -nologo -T ps_6_0 -E PSMain -Vn g_ui_overlay_ps -Fh "${SPYRO_SHADER_OUT}/ui_overlay_ps.h" "${SPYRO_UI_HLSL}"
    COMMAND "${SPYRO_DXC}" -nologo ${SPYRO_SPIRV} -fvk-invert-y -T vs_6_0 -E VSMain -Vn g_ui_overlay_vs_spv -Fh "${SPYRO_SHADER_OUT}/ui_overlay_vs_spv.h" "${SPYRO_UI_HLSL}"
    COMMAND "${SPYRO_DXC}" -nologo ${SPYRO_SPIRV} -T ps_6_0 -E PSMain -Vn g_ui_overlay_ps_spv -Fh "${SPYRO_SHADER_OUT}/ui_overlay_ps_spv.h" "${SPYRO_UI_HLSL}"
    DEPENDS "${SPYRO_UI_HLSL}"
    COMMENT "Compiling plume overlay UI shader")
add_custom_target(spyro_plume_shaders DEPENDS "${SPYRO_SHADER_OUT}/present_blit_vs.h" "${SPYRO_SHADER_OUT}/present_blit_ps.h"
                  "${SPYRO_SHADER_OUT}/present_blit_depthms_ps.h"
                  "${SPYRO_SHADER_OUT}/present_blit_vs_spv.h" "${SPYRO_SHADER_OUT}/present_blit_ps_spv.h"
                  "${SPYRO_SHADER_OUT}/present_blit_depthms_ps_spv.h"
                  "${SPYRO_SHADER_OUT}/ui_overlay_vs.h" "${SPYRO_SHADER_OUT}/ui_overlay_ps.h"
                  "${SPYRO_SHADER_OUT}/ui_overlay_vs_spv.h" "${SPYRO_SHADER_OUT}/ui_overlay_ps_spv.h")
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
target_include_directories(spyrodotd PRIVATE "${SPYRO_SHADER_RECOMP}")
if(WIN32)
    target_include_directories(spyrodotd PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../../TOOLS/dxc/inc")
    # The compiler DLLs the game loads on first use (dxil.dll signs the DXIL) go next to the exe.
    set(SPYRO_DXC_BIN "${CMAKE_CURRENT_SOURCE_DIR}/../../TOOLS/dxc/bin/x64")
    add_custom_command(TARGET spyrodotd POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${SPYRO_DXC_BIN}/dxcompiler.dll" "${SPYRO_DXC_BIN}/dxil.dll"
                "$<TARGET_FILE_DIR:spyrodotd>")
else()
    # Linux: DXC's Linux release (TOOLS/dxc-linux: include/dxc/*.h with WinAdapter.h, lib/
    # libdxcompiler.so) - SPIR-V only, so no dxil signing library is needed.
    set(SPYRO_DXC_LINUX "${CMAKE_CURRENT_SOURCE_DIR}/../../TOOLS/dxc-linux")
    target_include_directories(spyrodotd PRIVATE "${SPYRO_DXC_LINUX}/include")
    add_custom_command(TARGET spyrodotd POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${SPYRO_DXC_LINUX}/lib/libdxcompiler.so"
                "$<TARGET_FILE_DIR:spyrodotd>")
endif()

# The launcher players start (asks for the game ISO / folder, then starts spyrodotd.exe).
# Static C runtime: it must run before anything else is installed.
if(WIN32)
    add_executable(SpyroRecomp WIN32 ../launcher/launcher.cpp)
    set_property(TARGET SpyroRecomp PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded")
    target_link_libraries(SpyroRecomp PRIVATE bcrypt comctl32 ole32 shell32)
else()
    # Linux launcher (WORK_LOG 158): SDL3 message boxes / file dialog / progress window.
    add_executable(SpyroRecomp ../launcher/launcher_linux.cpp)
    if(TARGET SDL3::SDL3)
        target_link_libraries(SpyroRecomp PRIVATE SDL3::SDL3)
    else()
        target_link_libraries(SpyroRecomp PRIVATE SDL3::SDL3-static)
    endif()
endif()

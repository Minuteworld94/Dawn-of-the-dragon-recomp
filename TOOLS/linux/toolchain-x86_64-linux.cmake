# Cross-compiling the Linux (x86-64) build on Windows (WORK_LOG 158): our clang + lld, against the
# Steam Runtime "sniper" SDK sysroot (Debian 11 base, glibc 2.31 - runs on SteamOS and newer distros)
# unpacked by TOOLS\linux\extract_sysroot.py into TOOLS\linux-sysroot.
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

get_filename_component(SPYRO_TOOLS "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(CMAKE_SYSROOT "${SPYRO_TOOLS}/linux-sysroot")
set(SPYRO_TRIPLE x86_64-linux-gnu)

set(CMAKE_C_COMPILER "${SPYRO_TOOLS}/llvm/bin/clang.exe")
set(CMAKE_CXX_COMPILER "${SPYRO_TOOLS}/llvm/bin/clang++.exe")
set(CMAKE_ASM_COMPILER "${SPYRO_TOOLS}/llvm/bin/clang.exe")
set(CMAKE_C_COMPILER_TARGET ${SPYRO_TRIPLE})
set(CMAKE_CXX_COMPILER_TARGET ${SPYRO_TRIPLE})
set(CMAKE_ASM_COMPILER_TARGET ${SPYRO_TRIPLE})
set(CMAKE_AR "${SPYRO_TOOLS}/llvm/bin/llvm-ar.exe" CACHE FILEPATH "")
set(CMAKE_RANLIB "${SPYRO_TOOLS}/llvm/bin/llvm-ranlib.exe" CACHE FILEPATH "")
set(CMAKE_STRIP "${SPYRO_TOOLS}/llvm/bin/llvm-strip.exe" CACHE FILEPATH "")
set(CMAKE_OBJCOPY "${SPYRO_TOOLS}/llvm/bin/llvm-objcopy.exe" CACHE FILEPATH "")
set(CMAKE_LINKER_TYPE LLD)

# The C++ library: Valve's GCC 14 in the sysroot (usr/lib/gcc-14; Debian 11's own GCC 10 is too old
# for C++23), linked in statically: the game does not depend on the system's C++ runtime. libgcc_s
# stays shared (every Linux has it; GCC 14 here has no libgcc_eh.a - extract_sysroot.py copies GCC 10's
# libgcc_s.so linker script next to it).
set(SPYRO_GCC_DIR "${CMAKE_SYSROOT}/usr/lib/gcc-14/lib/gcc/x86_64-linux-gnu/14")
set(SPYRO_GCC_FLAG "--gcc-install-dir=\"${SPYRO_GCC_DIR}\"")
# x86-64-v2 = the Windows build's level (the Steam Deck's Zen 2 has v3). Do not pass CMAKE_C(XX)_FLAGS on
# the command line: it would replace these.
set(CMAKE_C_FLAGS_INIT "${SPYRO_GCC_FLAG} -march=x86-64-v2")
set(CMAKE_CXX_FLAGS_INIT "${SPYRO_GCC_FLAG} -march=x86-64-v2")
set(CMAKE_EXE_LINKER_FLAGS_INIT "${SPYRO_GCC_FLAG} -fuse-ld=lld -static-libstdc++")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "${SPYRO_GCC_FLAG} -fuse-ld=lld -static-libstdc++")
set(CMAKE_MODULE_LINKER_FLAGS_INIT "${SPYRO_GCC_FLAG} -fuse-ld=lld -static-libstdc++")

# pkg-config (TOOLS\linux\pkgconfig, ours): finds X11 / Wayland / ... in the sysroot for the SDK and SDL.
set(PKG_CONFIG_EXECUTABLE "${CMAKE_CURRENT_LIST_DIR}/pkgconfig/pkg-config.exe" CACHE FILEPATH "")
set(ENV{PKG_CONFIG_SYSROOT_DIR} "${CMAKE_SYSROOT}")
set(ENV{PKG_CONFIG_LIBDIR}
    "${CMAKE_SYSROOT}/usr/lib/x86_64-linux-gnu/pkgconfig;${CMAKE_SYSROOT}/usr/share/pkgconfig;${CMAKE_SYSROOT}/usr/lib/pkgconfig")

# Libraries / headers / packages only from the sysroot; programs (tools run during the build) from
# Windows.
set(CMAKE_FIND_ROOT_PATH "${CMAKE_SYSROOT}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
set(CMAKE_LIBRARY_ARCHITECTURE ${SPYRO_TRIPLE})

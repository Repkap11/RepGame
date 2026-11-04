# Steam Frame toolchain: host clang → aarch64-linux-gnu against the sniper
# (Steam Linux Runtime 3.0 ARM64, Debian 11 / glibc 2.31) sysroot. Checked-in
# replacement for the toolchain file steamframe.mk used to generate — the
# SDL3 and OpenXR-loader builds use it too, so it must NOT carry RepGame's
# -march=armv8.2-a (that's a platform flag in CMakeLists.txt).
#
#   -DREPGAME_SF_SYSROOT=<sniper sysroot>   (make steamframe-sysroot)
#
# sniper is Debian 11: libstdc++ headers under usr/include/c++/<ver>, gcc
# runtime dir under usr/lib/gcc/aarch64-linux-gnu/<ver>. Point clang at them
# explicitly — clang doesn't auto-detect a cross gcc inside a foreign sysroot.
set(REPGAME_SF_SYSROOT "" CACHE PATH "sniper arm64 sysroot (out/steamframe-sysroot)")
if(NOT EXISTS "${REPGAME_SF_SYSROOT}")
	message(FATAL_ERROR "REPGAME_SF_SYSROOT must point at the sniper sysroot (run `make steamframe-sysroot`)")
endif()
# Re-exported to try_compile so the ABI checks below see the same sysroot.
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES REPGAME_SF_SYSROOT)

file(GLOB _sf_gcc_dirs LIST_DIRECTORIES true ${REPGAME_SF_SYSROOT}/usr/lib/gcc/aarch64-linux-gnu/*)
list(GET _sf_gcc_dirs 0 SF_GCC_DIR)
get_filename_component(SF_GCC_VER ${SF_GCC_DIR} NAME)

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)
set(CMAKE_SYSROOT ${REPGAME_SF_SYSROOT})
set(CMAKE_C_COMPILER clang)
set(CMAKE_C_COMPILER_TARGET aarch64-linux-gnu)
set(CMAKE_C_FLAGS_INIT "--gcc-install-dir=${SF_GCC_DIR} -fuse-ld=lld")
set(CMAKE_CXX_COMPILER clang++)
set(CMAKE_CXX_COMPILER_TARGET aarch64-linux-gnu)
set(CMAKE_CXX_FLAGS_INIT "--gcc-install-dir=${SF_GCC_DIR} -isystem ${REPGAME_SF_SYSROOT}/usr/include/c++/${SF_GCC_VER} -isystem ${REPGAME_SF_SYSROOT}/usr/include/c++/${SF_GCC_VER}/aarch64-linux-gnu")
set(CMAKE_EXE_LINKER_FLAGS_INIT "-fuse-ld=lld")
set(CMAKE_FIND_ROOT_PATH ${REPGAME_SF_SYSROOT})
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
set(ENV{PKG_CONFIG_SYSROOT_DIR} "${REPGAME_SF_SYSROOT}")
set(ENV{PKG_CONFIG_LIBDIR} "${REPGAME_SF_SYSROOT}/usr/lib/aarch64-linux-gnu/pkgconfig:${REPGAME_SF_SYSROOT}/usr/lib/pkgconfig:${REPGAME_SF_SYSROOT}/usr/share/pkgconfig")

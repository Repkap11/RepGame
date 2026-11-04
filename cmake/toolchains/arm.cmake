# ARM toolchain: host clang → aarch64-poky-linux against the Yocto SDK for the
# aarch64 board (PowerVR, GLES 3.2, Weston/Wayland). Mirrors CC_ARM from the
# old arm.mk. Clang's integrated assembler builds the aarch64 .S blobs, so
# the SDK's ld/objcopy are no longer needed.
#
#   -DREPGAME_ARM_SDK=<yocto SDK root>
set(REPGAME_ARM_SDK "" CACHE PATH "Yocto SDK root (see makefiles/arm.mk ARM_SDK)")
if(NOT EXISTS "${REPGAME_ARM_SDK}")
	message(FATAL_ERROR "REPGAME_ARM_SDK must point at the Yocto SDK (ARM_SDK override in makefiles/arm.mk)")
endif()
# Re-exported to try_compile so the ABI checks see the same SDK.
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES REPGAME_ARM_SDK)

set(_arm_sysroot ${REPGAME_ARM_SDK}/sysroots/cortexa76-poky-linux)
# clang doesn't auto-detect the Yocto libstdc++ include dirs from the gcc
# install dir, so point at them explicitly.
file(GLOB _arm_gcc_dirs LIST_DIRECTORIES true ${_arm_sysroot}/usr/lib/aarch64-poky-linux/*)
list(GET _arm_gcc_dirs 0 ARM_GCC_DIR)
get_filename_component(ARM_GCC_VER ${ARM_GCC_DIR} NAME)

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)
set(CMAKE_SYSROOT ${_arm_sysroot})
set(CMAKE_C_COMPILER clang)
set(CMAKE_C_COMPILER_TARGET aarch64-poky-linux)
set(CMAKE_C_FLAGS_INIT "--gcc-install-dir=${ARM_GCC_DIR} -fuse-ld=lld")
set(CMAKE_CXX_COMPILER clang++)
set(CMAKE_CXX_COMPILER_TARGET aarch64-poky-linux)
set(CMAKE_CXX_FLAGS_INIT "--gcc-install-dir=${ARM_GCC_DIR} -isystem ${_arm_sysroot}/usr/include/c++/${ARM_GCC_VER} -isystem ${_arm_sysroot}/usr/include/c++/${ARM_GCC_VER}/aarch64-poky-linux -mcpu=cortex-a76")
set(CMAKE_EXE_LINKER_FLAGS_INIT "-fuse-ld=lld")
set(CMAKE_FIND_ROOT_PATH ${_arm_sysroot})
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

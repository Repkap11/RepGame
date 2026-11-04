# Cross-compile the Linux client for the aarch64 Yocto board (PowerVR, GLES
# 3.2, Weston/Wayland). Compile is CMake (REPGAME_PLATFORM=arm, toolchain
# cmake/toolchains/arm.cmake); this file is the task runner. It can be built
# alongside other platforms in one invocation (e.g. `make linux arm`).
#
#   make arm          — release binary at out/arm/release/RepGame
#   make arm_debug    — debug binary at out/arm/debug/RepGame
#   make arm-sdl3     — (re)build the static SDL3 for the sysroot
#   make arm-deploy   — rsync the release binary to $(ARM_DEVICE):$(ARM_DEPLOY_DIR)
#   make arm-run      — deploy + run against the public server over Wayland
#   make arm_debug-run — deploy + run the debug binary single-player
#
# Overrides: ARM_SDK (Yocto SDK root), ARM_DEVICE, ARM_DEPLOY_DIR,
# ARM_SDL3_VERSION.

ARM_SDK ?= /home/prepka/Software/430/001/d3-renesas-bsp/yocto-sdk
ARM_OUT ?= out/arm
ARM_DEVICE ?= root@renesas-v4h
ARM_DEPLOY_DIR ?= /home/root
ARM_SDL3_VERSION ?= 3.4.16
# SDL3 lives outside $(ARM_OUT) so out/arm is created only by the cmake build.
ARM_SDL3_DIR := out/arm-sdl3
ARM_SDL3_PREFIX := $(abspath $(ARM_SDL3_DIR))/install

CMAKE_ARM_DFLAGS := -DREPGAME_ARM_SDK=$(abspath $(ARM_SDK)) -DREPGAME_SDL3_PREFIX=$(ARM_SDL3_PREFIX) \
	-DREPGAME_AUDIO=$(REPGAME_AUDIO) -DREPGAME_SOUND_OGG_DIR=$(abspath $(SOUND_OGG_DIR))

arm: $(ARM_OUT)/release/$(TARGET)
arm_debug: $(ARM_OUT)/debug/$(TARGET)

# The static SDL3 headers are on every compile line, so SDL3 must be
# installed before cmake compiles anything.
$(ARM_OUT)/release/$(TARGET): $(SOUND_OGGS) $(ARM_SDL3_PREFIX)/lib/libSDL3.a FORCE
	$(call cmake_build,arm-release,$(ARM_OUT)/cmake-release,$(CMAKE_ARM_DFLAGS) \
		-DREPGAME_OUTPUT_DIR=$(abspath $(ARM_OUT))/release)

$(ARM_OUT)/debug/$(TARGET): $(SOUND_OGGS) $(ARM_SDL3_PREFIX)/lib/libSDL3.a FORCE
	$(call cmake_build,arm-debug,$(ARM_OUT)/cmake-debug,$(CMAKE_ARM_DFLAGS) \
		-DREPGAME_OUTPUT_DIR=$(abspath $(ARM_OUT))/debug)

# Static SDL3 built inside the Yocto SDK environment (Wayland client only, no
# X11/KMSDRM/Vulkan, GLES on). Same release tarball as the Dockerfile.
ARM_SDL3_SRC := $(abspath $(ARM_SDL3_DIR))/SDL3-$(ARM_SDL3_VERSION)

$(ARM_SDL3_PREFIX)/lib/libSDL3.a:
	mkdir -p $(ARM_SDL3_DIR)
	cd $(ARM_SDL3_DIR) && \
		wget -q https://github.com/libsdl-org/SDL/releases/download/release-$(ARM_SDL3_VERSION)/SDL3-$(ARM_SDL3_VERSION).tar.gz && \
		tar xzf SDL3-$(ARM_SDL3_VERSION).tar.gz
	. $(ARM_SDK)/environment-setup-cortexa76-poky-linux && \
		cmake -S $(ARM_SDL3_SRC) -B $(ARM_SDL3_SRC)/build -G Ninja \
			-DCMAKE_TOOLCHAIN_FILE=$$OECORE_NATIVE_SYSROOT/usr/share/cmake/OEToolchainConfig.cmake \
			-DCMAKE_BUILD_TYPE=Release \
			-DCMAKE_INSTALL_PREFIX=$(ARM_SDL3_PREFIX) \
			-DCMAKE_POSITION_INDEPENDENT_CODE=ON \
			-DSDL_SHARED=OFF -DSDL_STATIC=ON -DSDL_TESTS=OFF -DSDL_TEST_LIBRARY=OFF \
			-DSDL_EXAMPLES=OFF -DSDL_X11=OFF -DSDL_WAYLAND=ON \
			-DSDL_WAYLAND_LIBDECOR=OFF -DSDL_KMSDRM=OFF -DSDL_VULKAN=OFF \
			-DSDL_OPENGL=OFF -DSDL_OPENGLES=ON -DSDL_PIPEWIRE=OFF \
			-DSDL_JACK=OFF -DSDL_SNDIO=OFF && \
		cmake --build $(ARM_SDL3_SRC)/build && \
		cmake --install $(ARM_SDL3_SRC)/build

arm-sdl3: $(ARM_SDL3_PREFIX)/lib/libSDL3.a

arm-deploy: arm
	rsync $(ARM_OUT)/release/$(TARGET) $(ARM_DEVICE):$(ARM_DEPLOY_DIR)/

arm-run: arm-deploy
	ssh -t $(ARM_DEVICE) 'cd $(ARM_DEPLOY_DIR) && XDG_RUNTIME_DIR=/run/user/1000 WAYLAND_DISPLAY=wayland-1 ./$(TARGET) $(WORLD) www.repkap11.com'

arm_debug-run: arm_debug
	rsync $(ARM_OUT)/debug/$(TARGET) $(ARM_DEVICE):$(ARM_DEPLOY_DIR)/$(TARGET)_debug
	ssh -t $(ARM_DEVICE) 'cd $(ARM_DEPLOY_DIR) && XDG_RUNTIME_DIR=/run/user/1000 WAYLAND_DISPLAY=wayland-1 ./$(TARGET)_debug $(WORLD_DEBUG)'

clean: clean-arm
clean-arm:
	rm -rf out/arm out/arm-sdl3

.PHONY: arm arm_debug arm-sdl3 arm-deploy arm-run arm_debug-run clean-arm

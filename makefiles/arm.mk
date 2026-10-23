# Cross-compile the Linux client for the aarch64 Yocto board (PowerVR, GLES
# 3.2, Weston/Wayland). Standalone platform makefile in the style of
# windows.mk — it can be built alongside other platforms in one invocation
# (e.g. `make linux arm`).
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
ARM_SYSROOT := $(ARM_SDK)/sysroots/cortexa76-poky-linux
ARM_SDK_BIN := $(ARM_SDK)/sysroots/x86_64-pokysdk-linux/usr/bin/aarch64-poky-linux
ARM_OUT ?= out/arm
ARM_DEVICE ?= root@renesas-v4h
ARM_DEPLOY_DIR ?= /home/root
ARM_SDL3_VERSION ?= 3.4.16
# SDL3 lives outside $(ARM_OUT) so out/arm is created only by this file's
# $(ARM_OUT) dir-skeleton rule.
ARM_SDL3_DIR := out/arm-sdl3
ARM_SDL3_PREFIX := $(abspath $(ARM_SDL3_DIR))/install

# ld -b binary generates symbol names from the file path with non-alnum chars
# converted to underscores. Used for --redefine-sym in shader blob targets.
ARM_OUT_SYMBOLS := $(subst /,_,$(subst .,_,$(subst -,_,$(ARM_OUT))))

ARM_GCC_DIR := $(firstword $(wildcard $(ARM_SYSROOT)/usr/lib/aarch64-poky-linux/*))
# clang doesn't auto-detect the Yocto libstdc++ include dirs from the gcc
# install dir, so point at them explicitly.
CC_ARM := clang++ --target=aarch64-poky-linux --sysroot=$(ARM_SYSROOT) --gcc-install-dir=$(ARM_GCC_DIR) -isystem $(ARM_SYSROOT)/usr/include/c++/$(notdir $(ARM_GCC_DIR)) -isystem $(ARM_SYSROOT)/usr/include/c++/$(notdir $(ARM_GCC_DIR))/aarch64-poky-linux -mcpu=cortex-a76

# Same warning/std/pic flags as linux.mk's clang build, but GLES instead of
# desktop GL+GLEW and the static SDL3 headers from the sysroot build.
CFLAGS_ARM := $(CFLAGS_WARNINGS) -std=c++17 -fno-pie -DREPGAME_LINUX -DGLEW_NO_GLU -DREPGAME_GLES -DIMGUI_IMPL_OPENGL_ES3 -I$(ARM_SDL3_PREFIX)/include -fPIE -Wno-c++20-designator
CFLAGS_ARM += $(CFLAGS_AUDIO)
CFLAGS_ARM_RELEASE := -O3 -DREPGAME_SKIP_CHECK_FOR_GL_ERRORS
CFLAGS_ARM_DEBUG := -g
# Final link goes through lld like linux.mk. Link-only flag — see CCLD_LINUX.
CCLD_ARM := -fuse-ld=lld
LFLAGS_ARM := -z noexecstack
LIBS_ARM := $(ARM_SDL3_PREFIX)/lib/libSDL3.a -lGLESv2 -lwayland-client -lpthread -lm -ldl -static-libgcc -static-libstdc++

# The host ld/objcopy only emit x86 objects; the shader/bitmap/font -b binary
# blobs must be aarch64 objects, so use the SDK binutils.
LD_BLOB_ARM := $(ARM_SDK_BIN)/aarch64-poky-linux-ld
OBJCOPY_ARM := $(ARM_SDK_BIN)/aarch64-poky-linux-objcopy

ifeq ($(USE_CCACHE),1)
CC_ARM := ccache $(CC_ARM)
endif

OBJECTS_COMMON_ARM_RELEASE := $(patsubst src/common/%.cpp,$(ARM_OUT)/release/common/%.o, $(SRC_COMMON))
OBJECTS_COMMON_ARM_DEBUG := $(patsubst src/common/%.cpp,$(ARM_OUT)/debug/common/%.o, $(SRC_COMMON))
OBJECTS_IMGUI_ARM_RELEASE := $(patsubst imgui_build/%.cpp,$(ARM_OUT)/release/imgui/%.o, $(SRC_IMGUI))
OBJECTS_IMGUI_ARM_DEBUG := $(patsubst imgui_build/%.cpp,$(ARM_OUT)/debug/imgui/%.o, $(SRC_IMGUI))

OBJECTS_ARM_RELEASE := $(patsubst src/%.cpp,$(ARM_OUT)/release/%.o, $(wildcard src/linux/*.cpp))
OBJECTS_ARM_DEBUG := $(patsubst src/%.cpp,$(ARM_OUT)/debug/%.o, $(wildcard src/linux/*.cpp))
DEPS_ARM := $(patsubst src/%.cpp,$(ARM_OUT)/release/%.d, $(wildcard src/linux/*.cpp)) \
		$(patsubst src/%.cpp,$(ARM_OUT)/debug/%.d, $(wildcard src/linux/*.cpp)) \
		$(patsubst src/common/%.cpp,$(ARM_OUT)/debug/common/%.d, $(SRC_COMMON)) \
		$(patsubst src/common/%.cpp,$(ARM_OUT)/release/common/%.d, $(SRC_COMMON))

SHADER_BLOBS_ARM := $(patsubst src/shaders/%.glsl,$(ARM_OUT)/shaders/%.o,$(wildcard src/shaders/*.glsl))
SHADER_PROCESSED_ARM := $(patsubst src/shaders/%.glsl,$(ARM_OUT)/shaders/%.glsl,$(wildcard src/shaders/*.glsl))
BITMAP_BLOBS_ARM := $(patsubst bitmaps/%.png,$(ARM_OUT)/bitmaps/%.o,$(BITMAP_PNGS))
FONT_BLOBS_ARM := $(patsubst fonts/%.ttf,$(ARM_OUT)/fonts/%.o,$(wildcard fonts/*.ttf))
SOUND_BLOBS_ARM := $(patsubst %,$(ARM_OUT)/sounds/%.o,$(SOUNDS_ENABLED))

# Preprocess shaders through the C preprocessor at build time. The preprocessed
# .glsl file lives in $(ARM_OUT)/shaders/ alongside the .o blob. The blob symbols
# are renamed to match what MK_BLOB(src_shaders, <name>, glsl) expects.
$(ARM_OUT)/shaders/%.glsl: src/shaders/%.glsl include/common/chunk_dimensions.hpp makefiles/common.mk makefiles/arm.mk | $(ARM_OUT)
	@mkdir -p $(dir $@)
	$(SHADER_PP) -DREPGAME_LINUX -DREPGAME_GLES $< -o $@

$(ARM_OUT)/shaders/%.o : $(ARM_OUT)/shaders/%.glsl makefiles/arm.mk | $(ARM_OUT)
	$(LD_BLOB_ARM) $(LFLAGS_ARM) -r -b binary $< -o $@_no_section
	$(OBJCOPY_ARM) --rename-section .data=.rodata,CONTENTS,ALLOC,LOAD,READONLY,DATA \
		--redefine-sym _binary_$(ARM_OUT_SYMBOLS)_shaders_$*_glsl_start=_binary_src_shaders_$*_glsl_start \
		--redefine-sym _binary_$(ARM_OUT_SYMBOLS)_shaders_$*_glsl_end=_binary_src_shaders_$*_glsl_end \
		$@_no_section $@

$(ARM_OUT)/bitmaps/%.o : out/bitmaps/%.bin makefiles/arm.mk | $(ARM_OUT)
	$(LD_BLOB_ARM) $(LFLAGS_ARM) -r -b binary $< -o $@_no_section
	$(OBJCOPY_ARM) --rename-section .data=.rodata,CONTENTS,ALLOC,LOAD,READONLY,DATA $@_no_section $@

$(ARM_OUT)/fonts/%.o : fonts/%.ttf | $(ARM_OUT)
	$(LD_BLOB_ARM) $(LFLAGS_ARM) -r -b binary $< -o $@_no_section
	$(OBJCOPY_ARM) --rename-section .data=.rodata,CONTENTS,ALLOC,LOAD,READONLY,DATA $@_no_section $@

# Sound blobs: renamed to the out_sounds prefix MK_BLOB expects (see linux.mk).
$(ARM_OUT)/sounds/%.o : $(SOUND_OGG_DIR)/%.ogg makefiles/arm.mk makefiles/common.mk | $(ARM_OUT)
	@mkdir -p $(dir $@)
	$(LD_BLOB_ARM) $(LFLAGS_ARM) -r -b binary $< -o $@_no_section
	$(OBJCOPY_ARM) --rename-section .data=.rodata,CONTENTS,ALLOC,LOAD,READONLY,DATA \
		--redefine-sym _binary_$(SOUND_SRC_SYMBOLS)_sounds_$*_ogg_start=_binary_out_sounds_$*_ogg_start \
		--redefine-sym _binary_$(SOUND_SRC_SYMBOLS)_sounds_$*_ogg_end=_binary_out_sounds_$*_ogg_end \
		$@_no_section $@

arm: $(ARM_OUT)/release/$(TARGET)
arm_debug: $(ARM_OUT)/debug/$(TARGET)

ARM_DIRS := $(patsubst src%,$(ARM_OUT)/release%,$(shell find src -type d)) \
	$(patsubst src%,$(ARM_OUT)/debug%,$(shell find src -type d)) \
	$(patsubst src%,$(ARM_OUT)%,$(shell find src -type d)) \
	$(ARM_OUT)/debug $(ARM_OUT)/release \
	$(ARM_OUT)/shaders $(ARM_OUT)/bitmaps $(ARM_OUT)/fonts \
	$(ARM_OUT)/sounds \
	$(ARM_OUT)/debug/imgui $(ARM_OUT)/release/imgui \
	$(ARM_OUT)/debug/imgui/backends $(ARM_OUT)/release/imgui/backends

$(ARM_OUT)/release/%.o: src/%.cpp | $(ARM_OUT)
	@#Use g++ to build o file and a dependecy tree .d file for every cpp file
	$(CC_ARM) $(INCLUDES_COMMON) $(CFLAGS_ARM) $(CFLAGS_ARM_RELEASE) -MMD -MP -MF $(patsubst %.o,%.d,$@) -MT $(patsubst %.d,%.o,$@) -c $< -o $@

$(ARM_OUT)/debug/%.o: src/%.cpp | $(ARM_OUT)
	@#Use g++ to build o file and a dependecy tree .d file for every cpp file
	$(CC_ARM) $(INCLUDES_COMMON) $(CFLAGS_ARM) $(CFLAGS_ARM_DEBUG) -MMD -MP -MF $(patsubst %.o,%.d,$@) -MT $(patsubst %.d,%.o,$@) -c $< -o $@

# Vendored imgui keeps -Wall/-Wextra warnings but isn't held to -Werror
$(ARM_OUT)/debug/imgui/%.o: imgui_build/%.cpp | $(ARM_OUT)
	@#Use g++ to build o file and a dependecy tree .d file for every cpp file
	$(CC_ARM) $(INCLUDES_COMMON) $(CFLAGS_ARM) $(CFLAGS_ARM_DEBUG) -Wno-error -MMD -MP -MF $(patsubst %.o,%.d,$@) -MT $(patsubst %.d,%.o,$@) -c $< -o $@

$(ARM_OUT)/release/imgui/%.o: imgui_build/%.cpp | $(ARM_OUT)
	@#Use g++ to build o file and a dependecy tree .d file for every cpp file
	$(CC_ARM) $(INCLUDES_COMMON) $(CFLAGS_ARM) $(CFLAGS_ARM_RELEASE) -Wno-error -MMD -MP -MF $(patsubst %.o,%.d,$@) -MT $(patsubst %.d,%.o,$@) -c $< -o $@

#Include these .d files, so the dependicies are known for secondary builds.
-include $(DEPS_ARM)

$(ARM_OUT)/release/$(TARGET): $(ARM_OUT)/release/$(TARGET)_uncompressed
	rm -f $@
	upx-ucl -q $< -o $@
	touch $@

$(ARM_OUT)/release/$(TARGET)_uncompressed: $(OBJECTS_IMGUI_ARM_RELEASE) $(OBJECTS_COMMON_ARM_RELEASE) $(OBJECTS_ARM_RELEASE) $(SHADER_BLOBS_ARM) $(BITMAP_BLOBS_ARM) $(FONT_BLOBS_ARM) $(SOUND_BLOBS_ARM) | $(ARM_OUT)
	$(CC_ARM) -flto $(CCLD_ARM) $(CFLAGS_ARM) $(CFLAGS_ARM_RELEASE) $(OBJECTS_ARM_RELEASE) $(OBJECTS_IMGUI_ARM_RELEASE) $(OBJECTS_COMMON_ARM_RELEASE) $(SHADER_BLOBS_ARM) $(BITMAP_BLOBS_ARM) $(FONT_BLOBS_ARM) $(SOUND_BLOBS_ARM) $(LIBS_ARM) -o $@

$(ARM_OUT)/debug/$(TARGET): $(OBJECTS_IMGUI_ARM_DEBUG) $(OBJECTS_COMMON_ARM_DEBUG) $(OBJECTS_ARM_DEBUG) $(SHADER_BLOBS_ARM) $(BITMAP_BLOBS_ARM) $(FONT_BLOBS_ARM) $(SOUND_BLOBS_ARM) | $(ARM_OUT)
	$(CC_ARM) $(CCLD_ARM) $(CFLAGS_ARM) $(CFLAGS_ARM_DEBUG) $(OBJECTS_ARM_DEBUG) $(OBJECTS_IMGUI_ARM_DEBUG) $(OBJECTS_COMMON_ARM_DEBUG) $(SHADER_BLOBS_ARM) $(BITMAP_BLOBS_ARM) $(FONT_BLOBS_ARM) $(SOUND_BLOBS_ARM) $(LIBS_ARM) -o $@

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

# The static SDL3 headers are on every compile line (CFLAGS_ARM), so SDL3 must
# be installed before any object builds — gate it on the dir every object has
# as an order-only prerequisite.
$(ARM_OUT): | out $(ARM_SDL3_PREFIX)/lib/libSDL3.a
	mkdir -p $(ARM_DIRS)
	touch $@

.PRECIOUS: $(ARM_OUT)/release/$(TARGET) $(ARM_OUT)/debug/$(TARGET) $(OBJECTS_ARM_DEBUG) $(OBJECTS_ARM_RELEASE) $(OBJECTS_COMMON_ARM_RELEASE) $(OBJECTS_COMMON_ARM_DEBUG) $(SHADER_BLOBS_ARM) $(SHADER_PROCESSED_ARM) $(BITMAP_BLOBS_ARM) $(FONT_BLOBS_ARM) $(SOUND_BLOBS_ARM)

.PHONY: arm arm_debug arm-sdl3 arm-deploy arm-run arm_debug-run clean-arm

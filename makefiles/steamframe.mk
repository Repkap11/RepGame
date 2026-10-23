# Cross-compile the Linux GLES client for the Valve Steam Frame (Linux ARM64,
# runs inside Steam Linux Runtime 3.0 ARM64 "sniper", Debian 11 / glibc 2.31).
# Standalone platform makefile in the style of arm.mk — it can be built
# alongside other platforms in one invocation (e.g. `make linux steamframe`).
# Not part of `all:` — it needs a ~350MB sysroot download first.
#
#   make steamframe         — release binary at out/steamframe/release/RepGame
#   make steamframe_debug   — debug binary at out/steamframe/debug/RepGame
#   make steamframe-sysroot — download + extract + verify the sniper sysroot
#   make steamframe-sdl3    — (re)build the static SDL3 against the sysroot
#   make steamframe-dist    — copy the release binary to out/steamframe/dist/
#                             (the folder for the SteamOS Devkit Client upload)
#
# Overrides: SF_SNAPSHOT, SF_SYSROOT_DIR, SF_SDL3_VERSION, SF_OBJCOPY.

# Newest steamrt3 image snapshot that still publishes arm64 artifacts —
# Valve stopped shipping Platform-arm64 images after 2026-06-02 (checked
# 2026-10-22; 3.0.20260608 and later are amd64,i386-only). Bump after
# checking https://repo.steampowered.com/steamrt3/images/ for a newer
# snapshot that lists arm64 entries in its SHA256SUMS.
SF_SNAPSHOT ?= 3.0.20260602.240810
SF_SNAPSHOT_URL := https://repo.steampowered.com/steamrt3/images/$(SF_SNAPSHOT)
# The Sdk sysroot, not the Platform one: Platform-arm64-sniper-sysroot is a
# runtime rootfs (no dev headers, no .so symlinks, no gcc dir) — the Sdk
# sysroot is the same rootfs plus all the -dev packages.
SF_SYSROOT_TARBALL := com.valvesoftware.SteamRuntime.Sdk-arm64-sniper-sysroot.tar.gz
SF_SYSROOT_DIR ?= out/steamframe-sysroot
SF_SYSROOT := $(abspath $(SF_SYSROOT_DIR))
SF_OUT ?= out/steamframe
SF_SDL3_VERSION ?= 3.4.16
# SDL3 lives outside $(SF_OUT) so out/steamframe is created only by this
# file's $(SF_OUT) dir-skeleton rule.
SF_SDL3_DIR := out/steamframe-sdl3
SF_SDL3_PREFIX := $(abspath $(SF_SDL3_DIR))/install

# llvm-objcopy -I binary generates symbol names from the file path with
# non-alnum chars converted to underscores, same as `ld -r -b binary`.
# Used for --redefine-sym in shader blob targets.
SF_OUT_SYMBOLS := $(subst /,_,$(subst .,_,$(subst -,_,$(SF_OUT))))

# sniper is Debian 11: libstdc++ headers under usr/include/c++/<ver>, gcc
# runtime dir under usr/lib/gcc/aarch64-linux-gnu/<ver>. Point clang at them
# explicitly — clang doesn't auto-detect a cross gcc inside a foreign sysroot.
# Recursive (=) not immediate (:=): the wildcard must be re-evaluated at
# recipe time, after the steamframe-sysroot download has populated the tree.
SF_GCC_DIR = $(firstword $(wildcard $(SF_SYSROOT)/usr/lib/gcc/aarch64-linux-gnu/*))
SF_GCC_VER = $(notdir $(SF_GCC_DIR))
CC_SF = $(SF_CCACHE) clang++ --target=aarch64-linux-gnu --sysroot=$(SF_SYSROOT) --gcc-install-dir=$(SF_GCC_DIR) -isystem $(SF_SYSROOT)/usr/include/c++/$(SF_GCC_VER) -isystem $(SF_SYSROOT)/usr/include/c++/$(SF_GCC_VER)/aarch64-linux-gnu -march=armv8.2-a

# Same warning/std/pic flags as linux.mk's clang build, but GLES instead of
# desktop GL+GLEW and the static SDL3 headers from the sysroot build.
CFLAGS_SF := $(CFLAGS_WARNINGS) -std=c++17 -DREPGAME_LINUX -DGLEW_NO_GLU -DREPGAME_GLES -DREPGAME_STEAMFRAME -DIMGUI_IMPL_OPENGL_ES3 -I$(SF_SDL3_PREFIX)/include -fPIE -Wno-c++20-designator
CFLAGS_SF += $(CFLAGS_AUDIO)
CFLAGS_SF_RELEASE := -O3 -DREPGAME_SKIP_CHECK_FOR_GL_ERRORS
CFLAGS_SF_DEBUG := -g
# Final link goes through lld like linux.mk. Link-only flag — see CCLD_LINUX.
CCLD_SF := -fuse-ld=lld
LFLAGS_SF := -z noexecstack
# SDL3 loads most X11/Wayland extension libs via dlopen at runtime; -lX11 and
# -lwayland-client cover the core entry points, all present in sniper.
# A static libSDL3.a resolves its video backends at link time (SDL_*_SHARED
# ended up OFF), so the Wayland/X11 support libs it recorded must be linked
# too — all present in the sniper runtime.
LIBS_SF := $(SF_SDL3_PREFIX)/lib/libSDL3.a -lGLESv2 -lEGL \
	-lwayland-client -lwayland-cursor -lwayland-egl -lxkbcommon \
	-lX11 -lXext -lXcursor -lXi -lXfixes -lXrandr -lXss -lXtst \
	-lpulse \
	-lpthread -lm -ldl -static-libgcc -static-libstdc++

# The host ld only emits x86 objects; llvm-objcopy can emit aarch64 blobs
# directly (-O elf64-littleaarch64), replacing `ld -r -b binary`.
SF_OBJCOPY ?= /usr/lib/llvm-21/bin/llvm-objcopy

SF_CCACHE :=
ifeq ($(USE_CCACHE),1)
SF_CCACHE := ccache
endif

# OpenXR is on by default for the Frame (it's a VR headset). SF_OPENXR=0
# produces a plain flat GLES build.
SF_OPENXR ?= 1
ifeq ($(SF_OPENXR),1)
CFLAGS_SF += -DREPGAME_OPENXR -I $(OPENXR_SF_PREFIX)/include
LIBS_SF += $(OPENXR_SF_LIB)
endif

OBJECTS_COMMON_SF_RELEASE := $(patsubst src/common/%.cpp,$(SF_OUT)/release/common/%.o, $(SRC_COMMON))
OBJECTS_COMMON_SF_DEBUG := $(patsubst src/common/%.cpp,$(SF_OUT)/debug/common/%.o, $(SRC_COMMON))
OBJECTS_IMGUI_SF_RELEASE := $(patsubst imgui_build/%.cpp,$(SF_OUT)/release/imgui/%.o, $(SRC_IMGUI))
OBJECTS_IMGUI_SF_DEBUG := $(patsubst imgui_build/%.cpp,$(SF_OUT)/debug/imgui/%.o, $(SRC_IMGUI))

OBJECTS_SF_RELEASE := $(patsubst src/%.cpp,$(SF_OUT)/release/%.o, $(wildcard src/linux/*.cpp))
OBJECTS_SF_DEBUG := $(patsubst src/%.cpp,$(SF_OUT)/debug/%.o, $(wildcard src/linux/*.cpp))
DEPS_SF := $(patsubst src/%.cpp,$(SF_OUT)/release/%.d, $(wildcard src/linux/*.cpp)) \
	$(patsubst src/%.cpp,$(SF_OUT)/debug/%.d, $(wildcard src/linux/*.cpp)) \
	$(patsubst src/common/%.cpp,$(SF_OUT)/debug/common/%.d, $(SRC_COMMON)) \
	$(patsubst src/common/%.cpp,$(SF_OUT)/release/common/%.d, $(SRC_COMMON))

SHADER_BLOBS_SF := $(patsubst src/shaders/%.glsl,$(SF_OUT)/shaders/%.o,$(wildcard src/shaders/*.glsl))
SHADER_PROCESSED_SF := $(patsubst src/shaders/%.glsl,$(SF_OUT)/shaders/%.glsl,$(wildcard src/shaders/*.glsl))
BITMAP_BLOBS_SF := $(patsubst bitmaps/%.png,$(SF_OUT)/bitmaps/%.o,$(BITMAP_PNGS))
FONT_BLOBS_SF := $(patsubst fonts/%.ttf,$(SF_OUT)/fonts/%.o,$(wildcard fonts/*.ttf))
SOUND_BLOBS_SF := $(patsubst %,$(SF_OUT)/sounds/%.o,$(SOUNDS_ENABLED))

# Preprocess shaders through the C preprocessor at build time. The preprocessed
# .glsl file lives in $(SF_OUT)/shaders/ alongside the .o blob. The blob symbols
# are renamed to match what MK_BLOB(src_shaders, <name>, glsl) expects.
$(SF_OUT)/shaders/%.glsl: src/shaders/%.glsl include/common/chunk_dimensions.hpp makefiles/common.mk makefiles/steamframe.mk | $(SF_OUT)
	@mkdir -p $(dir $@)
	$(SHADER_PP) -DREPGAME_LINUX -DREPGAME_GLES -DREPGAME_STEAMFRAME $< -o $@

$(SF_OUT)/shaders/%.o : $(SF_OUT)/shaders/%.glsl makefiles/steamframe.mk | $(SF_OUT)
	$(SF_OBJCOPY) -I binary -O elf64-littleaarch64 $< $@_no_section
	$(SF_OBJCOPY) --rename-section .data=.rodata,CONTENTS,ALLOC,LOAD,READONLY,DATA \
		--redefine-sym _binary_$(SF_OUT_SYMBOLS)_shaders_$*_glsl_start=_binary_src_shaders_$*_glsl_start \
		--redefine-sym _binary_$(SF_OUT_SYMBOLS)_shaders_$*_glsl_end=_binary_src_shaders_$*_glsl_end \
		$@_no_section $@

$(SF_OUT)/bitmaps/%.o : out/bitmaps/%.bin makefiles/steamframe.mk | $(SF_OUT)
	$(SF_OBJCOPY) -I binary -O elf64-littleaarch64 $< $@_no_section
	$(SF_OBJCOPY) --rename-section .data=.rodata,CONTENTS,ALLOC,LOAD,READONLY,DATA $@_no_section $@

$(SF_OUT)/fonts/%.o : fonts/%.ttf | $(SF_OUT)
	$(SF_OBJCOPY) -I binary -O elf64-littleaarch64 $< $@_no_section
	$(SF_OBJCOPY) --rename-section .data=.rodata,CONTENTS,ALLOC,LOAD,READONLY,DATA $@_no_section $@

# Sound blobs: renamed to the out_sounds prefix MK_BLOB expects (see linux.mk).
$(SF_OUT)/sounds/%.o : $(SOUND_OGG_DIR)/%.ogg makefiles/steamframe.mk makefiles/common.mk | $(SF_OUT)
	@mkdir -p $(dir $@)
	$(SF_OBJCOPY) -I binary -O elf64-littleaarch64 $< $@_no_section
	$(SF_OBJCOPY) --rename-section .data=.rodata,CONTENTS,ALLOC,LOAD,READONLY,DATA \
		--redefine-sym _binary_$(SOUND_SRC_SYMBOLS)_sounds_$*_ogg_start=_binary_out_sounds_$*_ogg_start \
		--redefine-sym _binary_$(SOUND_SRC_SYMBOLS)_sounds_$*_ogg_end=_binary_out_sounds_$*_ogg_end \
		$@_no_section $@

steamframe: $(SF_OUT)/release/$(TARGET)
steamframe_debug: $(SF_OUT)/debug/$(TARGET)

SF_DIRS := $(patsubst src%,$(SF_OUT)/release%,$(shell find src -type d)) \
	$(patsubst src%,$(SF_OUT)/debug%,$(shell find src -type d)) \
	$(patsubst src%,$(SF_OUT)%,$(shell find src -type d)) \
	$(SF_OUT)/debug $(SF_OUT)/release \
	$(SF_OUT)/shaders $(SF_OUT)/bitmaps $(SF_OUT)/fonts \
	$(SF_OUT)/sounds \
	$(SF_OUT)/debug/imgui $(SF_OUT)/release/imgui \
	$(SF_OUT)/debug/imgui/backends $(SF_OUT)/release/imgui/backends

$(SF_OUT)/release/%.o: src/%.cpp | $(SF_OUT)
	@#Use g++ to build o file and a dependecy tree .d file for every cpp file
	$(CC_SF) $(INCLUDES_COMMON) $(CFLAGS_SF) $(CFLAGS_SF_RELEASE) -MMD -MP -MF $(patsubst %.o,%.d,$@) -MT $(patsubst %.d,%.o,$@) -c $< -o $@

$(SF_OUT)/debug/%.o: src/%.cpp | $(SF_OUT)
	@#Use g++ to build o file and a dependecy tree .d file for every cpp file
	$(CC_SF) $(INCLUDES_COMMON) $(CFLAGS_SF) $(CFLAGS_SF_DEBUG) -MMD -MP -MF $(patsubst %.o,%.d,$@) -MT $(patsubst %.d,%.o,$@) -c $< -o $@

# Vendored imgui keeps -Wall/-Wextra warnings but isn't held to -Werror
$(SF_OUT)/debug/imgui/%.o: imgui_build/%.cpp | $(SF_OUT)
	@#Use g++ to build o file and a dependecy tree .d file for every cpp file
	$(CC_SF) $(INCLUDES_COMMON) $(CFLAGS_SF) $(CFLAGS_SF_DEBUG) -Wno-error -MMD -MP -MF $(patsubst %.o,%.d,$@) -MT $(patsubst %.d,%.o,$@) -c $< -o $@

$(SF_OUT)/release/imgui/%.o: imgui_build/%.cpp | $(SF_OUT)
	@#Use g++ to build o file and a dependecy tree .d file for every cpp file
	$(CC_SF) $(INCLUDES_COMMON) $(CFLAGS_SF) $(CFLAGS_SF_RELEASE) -Wno-error -MMD -MP -MF $(patsubst %.o,%.d,$@) -MT $(patsubst %.d,%.o,$@) -c $< -o $@

#Include these .d files, so the dependicies are known for secondary builds.
-include $(DEPS_SF)

# No UPX for the Frame build — the devkit upload wants a plain ELF.
$(SF_OUT)/release/$(TARGET): $(OBJECTS_IMGUI_SF_RELEASE) $(OBJECTS_COMMON_SF_RELEASE) $(OBJECTS_SF_RELEASE) $(SHADER_BLOBS_SF) $(BITMAP_BLOBS_SF) $(FONT_BLOBS_SF) $(SOUND_BLOBS_SF) | $(SF_OUT)
	$(CC_SF) -flto $(CCLD_SF) $(CFLAGS_SF) $(CFLAGS_SF_RELEASE) $(OBJECTS_SF_RELEASE) $(OBJECTS_IMGUI_SF_RELEASE) $(OBJECTS_COMMON_SF_RELEASE) $(SHADER_BLOBS_SF) $(BITMAP_BLOBS_SF) $(FONT_BLOBS_SF) $(SOUND_BLOBS_SF) $(LIBS_SF) -o $@

$(SF_OUT)/debug/$(TARGET): $(OBJECTS_IMGUI_SF_DEBUG) $(OBJECTS_COMMON_SF_DEBUG) $(OBJECTS_SF_DEBUG) $(SHADER_BLOBS_SF) $(BITMAP_BLOBS_SF) $(FONT_BLOBS_SF) $(SOUND_BLOBS_SF) | $(SF_OUT)
	$(CC_SF) $(CCLD_SF) $(CFLAGS_SF) $(CFLAGS_SF_DEBUG) $(OBJECTS_SF_DEBUG) $(OBJECTS_IMGUI_SF_DEBUG) $(OBJECTS_COMMON_SF_DEBUG) $(SHADER_BLOBS_SF) $(BITMAP_BLOBS_SF) $(FONT_BLOBS_SF) $(SOUND_BLOBS_SF) $(LIBS_SF) -o $@

# Steam Linux Runtime 3.0 (sniper) arm64 sysroot: a rootfs tarball verified
# against the snapshot's SHA256SUMS before extraction.
SF_SYSROOT_STAMP := $(SF_SYSROOT_DIR)/.extracted

$(SF_SYSROOT_STAMP): | out makefiles/steamframe.mk
	mkdir -p $(SF_SYSROOT_DIR)
	cd $(SF_SYSROOT_DIR) && \
		wget -q $(SF_SNAPSHOT_URL)/SHA256SUMS -O SHA256SUMS && \
		wget -q $(SF_SNAPSHOT_URL)/$(SF_SYSROOT_TARBALL) -O $(SF_SYSROOT_TARBALL) && \
		grep -E ' \*?$(SF_SYSROOT_TARBALL)$$' SHA256SUMS | sha256sum -c -
	# Rootfs tarball: no sudo, skip ownership/device nodes from the image.
	tar -xzf $(SF_SYSROOT_DIR)/$(SF_SYSROOT_TARBALL) -C $(SF_SYSROOT_DIR) \
		--no-same-owner --no-same-permissions \
		--exclude='dev/*' --exclude='./dev/*'
	@# Dev-package .so symlinks are absolute (/lib/... -> host paths); rewrite
	@# them relative to the sysroot or the linker falls back to the .a archives.
	find $(SF_SYSROOT_DIR) -type l -lname '/*' | while read -r l; do \
		ln -sfn "$$(realpath -m --relative-to="$$(dirname "$$l")" "$(SF_SYSROOT)$$(readlink "$$l")")" "$$l"; \
	done
	@# Fail loudly if the headers/libs we need aren't in this sysroot.
	@missing=""; \
	for f in usr/include/GLES3/gl32.h usr/include/EGL/egl.h \
		usr/include/wayland-client.h usr/include/X11/Xlib.h; do \
		[ -e "$(SF_SYSROOT)/$$f" ] || missing="$$missing $$f"; \
	done; \
	for g in "usr/lib/aarch64-linux-gnu/libGLESv2.so*" \
		"usr/lib/aarch64-linux-gnu/libEGL.so*" \
		"usr/lib/aarch64-linux-gnu/libwayland-client.so*" \
		"usr/lib/aarch64-linux-gnu/libX11.so*" \
		"usr/lib/gcc/aarch64-linux-gnu/*"; do \
		ls $(SF_SYSROOT)/$$g >/dev/null 2>&1 || missing="$$missing $$g"; \
	done; \
	if [ -n "$$missing" ]; then \
		echo "ERROR: sniper sysroot is missing:$$missing" >&2; \
		exit 1; \
	fi
	touch $@

steamframe-sysroot: $(SF_SYSROOT_STAMP)

# Static SDL3 built against the sniper sysroot with host clang. Wayland + X11
# + offscreen video, GLES on, desktop GL/Vulkan/KMSDRM/libdecor/pipewire/
# jack/sndio off, tests/examples off. Same release tarball as arm.mk.
SF_SDL3_SRC := $(abspath $(SF_SDL3_DIR))/SDL3-$(SF_SDL3_VERSION)
SF_TOOLCHAIN := $(abspath $(SF_SDL3_DIR))/steamframe-toolchain.cmake

$(SF_TOOLCHAIN): | makefiles/steamframe.mk $(SF_SYSROOT_STAMP)
	mkdir -p $(SF_SDL3_DIR)
	rm -f $@
	@echo 'set(CMAKE_SYSTEM_NAME Linux)' >> $@
	@echo 'set(CMAKE_SYSTEM_PROCESSOR aarch64)' >> $@
	@echo 'set(CMAKE_SYSROOT $(SF_SYSROOT))' >> $@
	@echo 'set(CMAKE_C_COMPILER clang)' >> $@
	@echo 'set(CMAKE_C_COMPILER_TARGET aarch64-linux-gnu)' >> $@
	@echo 'set(CMAKE_C_FLAGS_INIT "--gcc-install-dir=$(SF_GCC_DIR) -fuse-ld=lld")' >> $@
	@echo 'set(CMAKE_CXX_COMPILER clang++)' >> $@
	@echo 'set(CMAKE_CXX_COMPILER_TARGET aarch64-linux-gnu)' >> $@
	@echo 'set(CMAKE_CXX_FLAGS_INIT "--gcc-install-dir=$(SF_GCC_DIR) -isystem $(SF_SYSROOT)/usr/include/c++/$(SF_GCC_VER) -isystem $(SF_SYSROOT)/usr/include/c++/$(SF_GCC_VER)/aarch64-linux-gnu -fuse-ld=lld")' >> $@
	@echo 'set(CMAKE_EXE_LINKER_FLAGS_INIT "-fuse-ld=lld")' >> $@
	@echo 'set(CMAKE_FIND_ROOT_PATH $(SF_SYSROOT))' >> $@
	@echo 'set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)' >> $@
	@echo 'set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)' >> $@
	@echo 'set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)' >> $@
	@echo 'set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)' >> $@
	@echo 'set(ENV{PKG_CONFIG_SYSROOT_DIR} "$(SF_SYSROOT)")' >> $@
	@echo 'set(ENV{PKG_CONFIG_LIBDIR} "$(SF_SYSROOT)/usr/lib/aarch64-linux-gnu/pkgconfig:$(SF_SYSROOT)/usr/lib/pkgconfig:$(SF_SYSROOT)/usr/share/pkgconfig")' >> $@

# sniper ships libwayland 1.18, but the host wayland-scanner is newer and
# emits code using WL_MARSHAL_FLAG_DESTROY / wl_proxy_marshal_flags (wayland
# >= 1.22) which the sysroot headers lack. Build a matching 1.18 scanner for
# the host and point SDL's find_program at it via -DWAYLAND_SCANNER.
SF_WAYLAND_VERSION := 1.18.0
SF_SCANNER := $(abspath $(SF_SDL3_DIR))/wayland-$(SF_WAYLAND_VERSION)/wayland-scanner

$(SF_SCANNER):
	mkdir -p $(SF_SDL3_DIR)
	cd $(SF_SDL3_DIR) && \
		wget -q https://wayland.freedesktop.org/releases/wayland-$(SF_WAYLAND_VERSION).tar.xz && \
		tar xJf wayland-$(SF_WAYLAND_VERSION).tar.xz
	cd $(abspath $(SF_SDL3_DIR))/wayland-$(SF_WAYLAND_VERSION) && \
		./configure --disable-documentation --disable-libraries && \
		make -j$(CPUS) wayland-scanner

$(SF_SDL3_PREFIX)/lib/libSDL3.a: $(SF_TOOLCHAIN) $(SF_SCANNER)
	mkdir -p $(SF_SDL3_DIR)
	cd $(SF_SDL3_DIR) && \
		wget -q https://github.com/libsdl-org/SDL/releases/download/release-$(SF_SDL3_VERSION)/SDL3-$(SF_SDL3_VERSION).tar.gz && \
		tar xzf SDL3-$(SF_SDL3_VERSION).tar.gz
	cmake -S $(SF_SDL3_SRC) -B $(SF_SDL3_SRC)/build \
		-DCMAKE_TOOLCHAIN_FILE=$(SF_TOOLCHAIN) \
		-DCMAKE_BUILD_TYPE=Release \
		-DCMAKE_INSTALL_PREFIX=$(SF_SDL3_PREFIX) \
		-DCMAKE_POSITION_INDEPENDENT_CODE=ON \
		-DWAYLAND_SCANNER=$(SF_SCANNER) \
		-DSDL_SHARED=OFF -DSDL_STATIC=ON -DSDL_TESTS=OFF -DSDL_TEST_LIBRARY=OFF \
		-DSDL_EXAMPLES=OFF -DSDL_X11=ON -DSDL_WAYLAND=ON -DSDL_OFFSCREEN=ON \
		-DSDL_WAYLAND_LIBDECOR=OFF -DSDL_KMSDRM=OFF -DSDL_VULKAN=OFF \
		-DSDL_OPENGL=OFF -DSDL_OPENGLES=ON -DSDL_PIPEWIRE=OFF \
		-DSDL_JACK=OFF -DSDL_SNDIO=OFF \
		-DSDL_FRIBIDI=OFF -DSDL_LIBTHAI=OFF && \
	cmake --build $(SF_SDL3_SRC)/build && \
	cmake --install $(SF_SDL3_SRC)/build

steamframe-sdl3: $(SF_SDL3_PREFIX)/lib/libSDL3.a

steamframe-dist: steamframe
	mkdir -p $(SF_OUT)/dist
	cp $(SF_OUT)/release/$(TARGET) $(SF_OUT)/dist/$(TARGET)

clean: clean-steamframe
clean-steamframe:
	rm -rf out/steamframe

clean-steamframe-deps:
	rm -rf out/steamframe-sysroot out/steamframe-sdl3

# The static SDL3 headers are on every compile line (CFLAGS_SF), so SDL3 must
# be installed before any object builds — gate it on the dir every object has
# as an order-only prerequisite.
$(SF_OUT): | out $(SF_SDL3_PREFIX)/lib/libSDL3.a
ifeq ($(SF_OPENXR),1)
$(SF_OUT): | $(OPENXR_SF_LIB)
endif
	mkdir -p $(SF_DIRS)
	touch $@

.PRECIOUS: $(SF_OUT)/release/$(TARGET) $(SF_OUT)/debug/$(TARGET) $(OBJECTS_SF_DEBUG) $(OBJECTS_SF_RELEASE) $(OBJECTS_COMMON_SF_RELEASE) $(OBJECTS_COMMON_SF_DEBUG) $(SHADER_BLOBS_SF) $(SHADER_PROCESSED_SF) $(BITMAP_BLOBS_SF) $(FONT_BLOBS_SF) $(SOUND_BLOBS_SF)

.PHONY: steamframe steamframe_debug steamframe-sysroot steamframe-sdl3 steamframe-dist clean-steamframe clean-steamframe-deps

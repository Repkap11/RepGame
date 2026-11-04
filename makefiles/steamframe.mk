# Cross-compile the Linux GLES client for the Valve Steam Frame (Linux ARM64,
# runs inside Steam Linux Runtime 3.0 ARM64 "sniper", Debian 11 / glibc 2.31).
# Compile is CMake (REPGAME_PLATFORM=steamframe, toolchain
# cmake/toolchains/steamframe.cmake); this file is the task runner. It can be
# built alongside other platforms in one invocation (e.g. `make linux
# steamframe`). Not part of `all:` — it needs a ~350MB sysroot download first.
#
#   make steamframe         — release binary at out/steamframe/release/RepGame
#   make steamframe_debug   — debug binary at out/steamframe/debug/RepGame
#   make steamframe-sysroot — download + extract + verify the sniper sysroot
#   make steamframe-sdl3    — (re)build the static SDL3 against the sysroot
#   make steamframe-dist    — copy the release binary to out/steamframe/dist/
#                             (the folder for the SteamOS Devkit Client upload)
#
# Overrides: SF_SNAPSHOT, SF_SYSROOT_DIR, SF_SDL3_VERSION.

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
# SDL3 lives outside $(SF_OUT) so out/steamframe is created only by the cmake
# build.
SF_SDL3_DIR := out/steamframe-sdl3
SF_SDL3_PREFIX := $(abspath $(SF_SDL3_DIR))/install
SF_TOOLCHAIN := $(abspath cmake/toolchains/steamframe.cmake)

# OpenXR is on by default for the Frame (it's a VR headset). SF_OPENXR=0
# produces a plain flat GLES build.
SF_OPENXR ?= 1

CMAKE_SF_DFLAGS := -DREPGAME_SF_SYSROOT=$(SF_SYSROOT) -DREPGAME_SDL3_PREFIX=$(SF_SDL3_PREFIX) \
	-DREPGAME_AUDIO=$(REPGAME_AUDIO) -DREPGAME_SOUND_OGG_DIR=$(abspath $(SOUND_OGG_DIR)) \
	-DREPGAME_OPENXR=$(SF_OPENXR) -DREPGAME_OPENXR_PREFIX=$(OPENXR_SF_PREFIX)
ifeq ($(USE_CCACHE),1)
CMAKE_SF_DFLAGS += -DCMAKE_CXX_COMPILER_LAUNCHER=ccache
endif
SF_OPENXR_DEP :=
ifeq ($(SF_OPENXR),1)
SF_OPENXR_DEP := $(OPENXR_SF_LIB)
endif

# Doc-required gate (docs/platforms.md): sniper tops out at glibc 2.31 and
# the Frame ships no libstdc++/libgcc_s (it's linked static), so a binary
# that needs either won't run on device. Checked after every link.
define sf_verify_binary
	@max_glibc=$$(readelf -V $(1) | grep -o 'GLIBC_[0-9.]*' | sort -Vu | tail -1); \
	if [ -n "$$max_glibc" ] && [ "$$(printf '%s\nGLIBC_2.31\n' "$$max_glibc" | sort -V | tail -1)" != "GLIBC_2.31" ]; then \
		echo "ERROR: $(1) needs $$max_glibc > sniper's 2.31" >&2; exit 1; \
	fi; \
	if readelf -d $(1) | grep -E 'libstdc\+\+|libgcc_s'; then \
		echo "ERROR: $(1) dynamically links libstdc++/libgcc_s" >&2; exit 1; \
	fi; \
	echo "sniper check ok: $$max_glibc, no libstdc++/libgcc_s"
endef

steamframe: $(SF_OUT)/release/$(TARGET)
steamframe_debug: $(SF_OUT)/debug/$(TARGET)

# The static SDL3 headers are on every compile line, so SDL3 (and the OpenXR
# loader) must be installed before cmake compiles anything.
$(SF_OUT)/release/$(TARGET): $(SOUND_OGGS) $(SF_SDL3_PREFIX)/lib/libSDL3.a $(SF_OPENXR_DEP) FORCE
	cmake --preset steamframe-release -B $(SF_OUT)/cmake-release $(CMAKE_SF_DFLAGS) \
		-DREPGAME_OUTPUT_DIR=$(abspath $(SF_OUT))/release
	MAKEFLAGS="$(CMAKE_MAKEFLAGS)" cmake --build $(SF_OUT)/cmake-release
	$(call sf_verify_binary,$@)

$(SF_OUT)/debug/$(TARGET): $(SOUND_OGGS) $(SF_SDL3_PREFIX)/lib/libSDL3.a $(SF_OPENXR_DEP) FORCE
	cmake --preset steamframe-debug -B $(SF_OUT)/cmake-debug $(CMAKE_SF_DFLAGS) \
		-DREPGAME_OUTPUT_DIR=$(abspath $(SF_OUT))/debug
	MAKEFLAGS="$(CMAKE_MAKEFLAGS)" cmake --build $(SF_OUT)/cmake-debug
	$(call sf_verify_binary,$@)

FORCE:

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

$(SF_SDL3_PREFIX)/lib/libSDL3.a: $(SF_SYSROOT_STAMP) $(SF_SCANNER)
	mkdir -p $(SF_SDL3_DIR)
	cd $(SF_SDL3_DIR) && \
		wget -q https://github.com/libsdl-org/SDL/releases/download/release-$(SF_SDL3_VERSION)/SDL3-$(SF_SDL3_VERSION).tar.gz && \
		tar xzf SDL3-$(SF_SDL3_VERSION).tar.gz
	cmake -S $(SF_SDL3_SRC) -B $(SF_SDL3_SRC)/build \
		-DCMAKE_TOOLCHAIN_FILE=$(SF_TOOLCHAIN) \
		-DREPGAME_SF_SYSROOT=$(SF_SYSROOT) \
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

.PHONY: steamframe steamframe_debug steamframe-sysroot steamframe-sdl3 steamframe-dist clean-steamframe clean-steamframe-deps FORCE

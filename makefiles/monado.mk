# Monado OpenXR runtime, built from source under out/. Ubuntu ships no
# monado package, and a runtime offering XR_MNDX_egl_enable (i.e. Monado —
# SteamVR for Linux lacks it) is required to actually run the
# REPGAME_OPENXR=1 client. Monado's simulated driver provides a fake HMD so
# the VR path is testable without hardware (docs/vr.md); DRIVER_REMOTE stays
# on so `monado-gui remote` (or remote-mndset) can move the simulated
# head/controllers. Not part of `all:`.
#
#   make monado        — runtime + monado-service under out/monado-src/.../build
#   make linux-vr      — client build with REPGAME_OPENXR=1 (out/linux-openxr)
#   make linux-vr-run  — both of the above, then run the game on the
#                        simulated HMD: starts monado-service if the IPC
#                        socket isn't up and points the loader at the dev
#                        runtime manifest via XR_RUNTIME_JSON
#
# Overrides: MONADO_VERSION.

MONADO_VERSION ?= 25.1.0
MONADO_SRC_ROOT := out/monado-src
MONADO_SRC := $(abspath $(MONADO_SRC_ROOT))/monado-v$(MONADO_VERSION)
MONADO_BUILD := $(MONADO_SRC)/build
# The dev manifest bakes in absolute library paths, so no install is needed.
MONADO_RUNTIME_JSON := $(MONADO_BUILD)/openxr_monado-dev.json
MONADO_SERVICE := $(MONADO_BUILD)/src/xrt/targets/service/monado-service
# monado-gui (remote-driver controls) — built only when SDL2 is found.
MONADO_GUI := $(MONADO_BUILD)/src/xrt/targets/gui/monado-gui

# Dev packages for a service + simulated-driver build. Hardware-driver deps
# (hidapi/opencv/v4l2/realsense/...) are deliberately absent — whatever is
# missing just disables drivers. libcjson-dev enables the runtime config
# file (P_OVERRIDE_ACTIVE_CONFIG=remote etc.), libbsd-dev the stale-socket
# cleanup, libsdl2-dev monado-gui.
MONADO_PACKAGES := libeigen3-dev libudev-dev libvulkan-dev glslang-tools \
	libglvnd-dev libx11-dev libxrandr-dev libxxf86vm-dev \
	libxcb1-dev libxcb-randr0-dev libx11-xcb-dev \
	libwayland-dev wayland-protocols libxkbcommon-dev \
	libcjson-dev libbsd-dev libsdl2-dev

# The service (out-of-process compositor) is required: the OpenXR client lib
# is an IPC client, so xrCreateSession has no compositor without it.
MONADO_CMAKE_FLAGS := -DCMAKE_BUILD_TYPE=Release \
	-DXRT_FEATURE_SERVICE=ON -DXRT_FEATURE_SERVICE_SYSTEMD=OFF \
	-DXRT_BUILD_DRIVER_SIMULATED=ON \
	-DBUILD_TESTING=OFF -DXRT_BUILD_SAMPLES=OFF -DXRT_MODULE_MONADO_CLI=OFF

$(MONADO_SRC)/CMakeLists.txt: | out makefiles/monado.mk
	mkdir -p $(MONADO_SRC_ROOT)
	cd $(MONADO_SRC_ROOT) && \
		wget -q https://gitlab.freedesktop.org/monado/monado/-/archive/v$(MONADO_VERSION)/monado-v$(MONADO_VERSION).tar.gz && \
		tar xzf monado-v$(MONADO_VERSION).tar.gz
	touch $@

$(MONADO_SERVICE): | monado-packages $(MONADO_SRC)/CMakeLists.txt
	cmake -G Ninja -S $(MONADO_SRC) -B $(MONADO_BUILD) $(MONADO_CMAKE_FLAGS)
	cmake --build $(MONADO_BUILD)

monado: $(MONADO_SERVICE)

# No-op when every package is already installed (avoids a sudo prompt on
# each run); the first `make monado` / `linux-vr-run` installs the list.
monado-packages:
	@dpkg -s $(MONADO_PACKAGES) >/dev/null 2>&1 || sudo apt-get install -y $(MONADO_PACKAGES)

# out/linux-openxr is LINUX_OUT under REPGAME_OPENXR=1 (common.mk); the
# nested make sets it itself, so the literal path only appears at run time.
linux-vr:
	$(MAKE) linux REPGAME_OPENXR=1

# monado-service binds $XDG_RUNTIME_DIR/monado_comp_ipc (~/.cache fallback).
# Reuse any running instance (a second service refuses the socket anyway);
# check for the PROCESS, not the socket file — a killed service leaves the
# socket stale, and a client hitting it gets ECONNREFUSED -> flat fallback.
# XRT_NO_STDIN keeps the service from epolling stdin — backgrounded from a
# recipe that fd isn't pollable, and stdin EOF would stop it. With no real
# HMD Monado falls back to the simulated driver; REPGAME_VR_MIRROR (on by
# default) blits eye 0 to the window. For interactive head/hand control run
# `P_OVERRIDE_ACTIVE_CONFIG=remote $(MONADO_SERVICE)` + `$(MONADO_GUI) remote`.
linux-vr-run: linux-vr monado
	@sock="$${XDG_RUNTIME_DIR:-$$HOME/.cache}/monado_comp_ipc"; \
	svc_pid=""; \
	cleanup() { if [ -n "$$svc_pid" ]; then \
		kill $$svc_pid 2>/dev/null; \
		for i in $$(seq 1 10); do kill -0 $$svc_pid 2>/dev/null || break; sleep 0.1; done; \
		kill -9 $$svc_pid 2>/dev/null; rm -f "$$sock"; svc_pid=""; fi; }; \
	trap cleanup EXIT; \
	trap 'exit 130' INT; trap 'exit 143' TERM; \
	if ! pgrep -x monado-service >/dev/null 2>&1; then \
		rm -f "$$sock"; \
		XRT_NO_STDIN=1 "$(MONADO_SERVICE)" & svc_pid=$$!; \
		for i in $$(seq 1 100); do [ -S "$$sock" ] && break; kill -0 $$svc_pid 2>/dev/null || break; sleep 0.1; done; \
	fi; \
	XR_RUNTIME_JSON="$(MONADO_RUNTIME_JSON)" ./out/linux-openxr/release/$(TARGET) $(WORLD) localhost

clean-monado:
	rm -rf $(MONADO_SRC_ROOT)

.PHONY: monado monado-packages linux-vr linux-vr-run clean-monado

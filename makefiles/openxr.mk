# Static Khronos OpenXR loader, built from source. Two installs: host x86_64
# for the linux build (REPGAME_OPENXR=1) and aarch64 for the steamframe build
# (SF_OPENXR=1, default on). Not part of `all:`.
#
#   make openxr-host       — host loader at out/openxr-host/install
#   make openxr-steamframe — aarch64 loader at out/openxr-steamframe/install
#
# Overrides: OPENXR_SDK_VERSION.

OPENXR_SDK_VERSION ?= 1.1.63
OPENXR_SRC_ROOT := out/openxr-src
OPENXR_SRC := $(abspath $(OPENXR_SRC_ROOT))/OpenXR-SDK-release-$(OPENXR_SDK_VERSION)
OPENXR_HOST_DIR := out/openxr-host
OPENXR_SF_DIR := out/openxr-steamframe
OPENXR_HOST_PREFIX := $(abspath $(OPENXR_HOST_DIR))/install
OPENXR_SF_PREFIX := $(abspath $(OPENXR_SF_DIR))/install
OPENXR_HOST_LIB := $(OPENXR_HOST_PREFIX)/lib/libopenxr_loader.a
OPENXR_SF_LIB := $(OPENXR_SF_PREFIX)/lib/libopenxr_loader.a

# Loader only: no conformance suite, no API layers, no hello_xr/test apps;
# the bundled jsoncpp is used (BUILD_WITH_SYSTEM_JSONCPP stays OFF) so the
# sniper sysroot doesn't need a jsoncpp dev package.
OPENXR_CMAKE_FLAGS := -DCMAKE_BUILD_TYPE=Release \
	-DCMAKE_POSITION_INDEPENDENT_CODE=ON \
	-DDYNAMIC_LOADER=OFF -DBUILD_TESTS=OFF -DBUILD_API_LAYERS=OFF \
	-DBUILD_CONFORMANCE_TESTS=OFF -DBUILD_LOADER=ON -DBUILD_WITH_SYSTEM_JSONCPP=OFF

$(OPENXR_SRC)/CMakeLists.txt: | out makefiles/openxr.mk
	mkdir -p $(OPENXR_SRC_ROOT)
	cd $(OPENXR_SRC_ROOT) && \
		wget -q https://github.com/KhronosGroup/OpenXR-SDK/archive/refs/tags/release-$(OPENXR_SDK_VERSION).tar.gz && \
		tar xzf release-$(OPENXR_SDK_VERSION).tar.gz
	touch $@

$(OPENXR_HOST_LIB): | $(OPENXR_SRC)/CMakeLists.txt
	cmake -S $(OPENXR_SRC) -B $(OPENXR_SRC)/build-host \
		-DCMAKE_INSTALL_PREFIX=$(OPENXR_HOST_PREFIX) \
		$(OPENXR_CMAKE_FLAGS) && \
	cmake --build $(OPENXR_SRC)/build-host && \
	cmake --install $(OPENXR_SRC)/build-host

# $(SF_TOOLCHAIN) is defined in makefiles/steamframe.mk, which is included
# AFTER this file — so it can only appear in the recipe (build-time
# expansion), not in prerequisites. The literal path is the same location.
$(OPENXR_SF_LIB): | $(OPENXR_SRC)/CMakeLists.txt $(abspath out/steamframe-sdl3)/steamframe-toolchain.cmake
	cmake -S $(OPENXR_SRC) -B $(OPENXR_SRC)/build-steamframe \
		-DCMAKE_TOOLCHAIN_FILE=$(SF_TOOLCHAIN) \
		-DCMAKE_INSTALL_PREFIX=$(OPENXR_SF_PREFIX) \
		$(OPENXR_CMAKE_FLAGS) && \
	cmake --build $(OPENXR_SRC)/build-steamframe && \
	cmake --install $(OPENXR_SRC)/build-steamframe

openxr-host: $(OPENXR_HOST_LIB)
openxr-steamframe: $(OPENXR_SF_LIB)

clean-openxr:
	rm -rf $(OPENXR_SRC_ROOT) out/openxr-host out/openxr-steamframe

.PHONY: openxr-host openxr-steamframe clean-openxr

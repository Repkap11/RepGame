#Linux x86_64 builds — thin wrapper over CMake (top-level CMakeLists.txt,
#cmake/, CMakePresets.json). Every build target re-runs configure so the
#-D overrides below always stick, then `cmake --build`.

REPGAME_PACKAGES += libglew-dev libxi-dev clang lld g++ libsdl3-dev cppcheck ffmpeg

# Compiler + linker overrides (LINUX_CXX=g++ gives a non-PIE binary; the
# flatpak sandbox ships only gcc and sets both).
LINUX_CXX ?= clang++
LINUX_USE_LLD ?= 1
# Thin LTO on the Release build (compile+link); set 0 to disable.
LINUX_LTO ?= 1

# Honored on the link line via CMake's CMAKE_EXE_LINKER_FLAGS init; ?=
# silences the undefined-variable warning when the environment doesn't set it
# (e.g. flatpak-builder passes -L/app/lib).
LDFLAGS ?=

# Prebuilt GPU objects dir (flatpak sandbox — see cmake/RepGameGpu.cmake).
GPU_PREBUILT_DIR ?=
OPENXR_DEP :=

CMAKE_CLIENT_DFLAGS := -DCMAKE_CXX_COMPILER=$(LINUX_CXX) -DREPGAME_USE_LLD=$(LINUX_USE_LLD) \
	-DREPGAME_LTO=$(LINUX_LTO) -DREPGAME_AUDIO=$(REPGAME_AUDIO) -DREPGAME_OPENXR=$(REPGAME_OPENXR)
ifneq ($(SOUND_OGG_DIR),)
CMAKE_CLIENT_DFLAGS += -DREPGAME_SOUND_OGG_DIR=$(abspath $(SOUND_OGG_DIR))
endif
ifneq ($(GPU_PREBUILT_DIR),)
CMAKE_CLIENT_DFLAGS += -DREPGAME_GPU_PREBUILT_DIR=$(abspath $(GPU_PREBUILT_DIR))
endif

# `make linux PERF=1` keeps frame pointers so `perf record --call-graph fp`
# gets clean stacks. The -D is passed either way so toggling reconfigures.
PERF ?= 0
ifeq ($(PERF),1)
CMAKE_CLIENT_DFLAGS += -DREPGAME_FRAME_POINTERS=ON
else
CMAKE_CLIENT_DFLAGS += -DREPGAME_FRAME_POINTERS=OFF
endif

# REPGAME_OPENXR=1 (common.mk): LINUX_OUT moves to out/linux-openxr so flat
# and VR objects never mix; the statically built host loader is a make dep.
ifeq ($(REPGAME_OPENXR),1)
CMAKE_CLIENT_DFLAGS += -DREPGAME_OPENXR_PREFIX=$(abspath out/openxr-host/install)
OPENXR_DEP := $(OPENXR_HOST_LIB)
endif

# SOUND_OGGS (common.mk) builds the .ogg files before cmake embeds them.

-include makefiles/cuda.mk
-include makefiles/hip.mk

all: linux
linux: $(LINUX_OUT)/release/$(TARGET)
linux_debug: $(LINUX_OUT)/debug/$(TARGET)

$(LINUX_OUT)/release/$(TARGET): $(SOUND_OGGS) $(OPENXR_DEP) FORCE
	$(call cmake_build,linux-release,$(LINUX_OUT)/cmake-release,$(CMAKE_CLIENT_DFLAGS) \
		-DREPGAME_OUTPUT_DIR=$(abspath $(LINUX_OUT))/release)

$(LINUX_OUT)/debug/$(TARGET): $(SOUND_OGGS) $(OPENXR_DEP) FORCE
	$(call cmake_build,linux-debug,$(LINUX_OUT)/cmake-debug,$(CMAKE_CLIENT_DFLAGS) \
		-DREPGAME_OUTPUT_DIR=$(abspath $(LINUX_OUT))/debug)

deploy: linux-deploy

linux-deploy: $(LINUX_OUT)/release/$(TARGET)
	rsync $< paul@repkap11.com:/home/paul/website/${TARGET_LOWER}

linux-run: linux
	./$(LINUX_OUT)/release/$(TARGET) $(WORLD) www.repkap11.com

linux-run2: linux
	./$(LINUX_OUT)/release/$(TARGET) $(WORLD_DEBUG) www.repkap11.com

linux-run-blocks: linux
	REPGAME_BLOCK_WORLD=1 ./$(LINUX_OUT)/release/$(TARGET) BlockWorld

dev: linux_debug-run

linux_debug-run: linux_debug
	./$(LINUX_OUT)/debug/$(TARGET) $(WORLD_DEBUG) localhost

# `make tests TESTS="net_ mobs"` runs only tests whose name contains a filter.
TESTS ?=
tests: linux_debug
	./$(LINUX_OUT)/debug/$(TARGET) --tests $(TESTS)

player:
	rm -rf ~/.repgame/$(WORLD)/player.dat

clean: clean-linux

clean-linux:
	rm -rf out/linux out/linux-portable

.PHONY: linux linux_debug linux-run linux-run2 linux-run-blocks clean-linux linux-deploy dev tests

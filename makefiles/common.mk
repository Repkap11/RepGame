#Shared between platforms

USE_CCACHE ?= 1

# C++ compilation is CMake (top-level CMakeLists.txt + cmake/); make targets
# in linux.mk/server.mk/cuda.mk/hip.mk are thin wrappers. Prefer Ninja when
# it's installed (`make install` adds ninja-build); otherwise leave
# CMAKE_GENERATOR unset (Unix Makefiles).
ifneq ($(shell command -v ninja 2>/dev/null),)
CMAKE_GENERATOR ?= Ninja
export CMAKE_GENERATOR
endif

# cmake --build invokes the generated build system's make; keep the jobserver
# flags but drop --warn-undefined-variables, which spams warnings from
# CMake's generated Makefiles.
CMAKE_MAKEFLAGS := $(filter-out --warn-undefined-variables,$(MAKEFLAGS))

ifeq ($(USE_CCACHE),1)
CMAKE_CCACHE := -DCMAKE_CXX_COMPILER_LAUNCHER=ccache
# TUs using -include-pch are uncacheable to ccache without this: pch_defines
# and time_macros let it hash the .pch contents and cache pch consumers.
export CCACHE_SLOPPINESS := pch_defines,time_macros
else
CMAKE_CCACHE :=
endif
# $(call cmake_build,<preset>,<build dir>,<-D flags>): re-run configure (so -D
# overrides always stick) then build.
define cmake_build
cmake --preset $(1) -B $(2) $(CMAKE_CCACHE) $(3)
MAKEFLAGS="$(CMAKE_MAKEFLAGS)" cmake --build $(2)
endef
FORCE:
.PHONY: FORCE

# OpenXR (VR) support for the linux build: REPGAME_OPENXR=1 compiles the
# OpenXR code paths and moves objects to out/linux-openxr so flat and VR
# objects never mix in one dir.
REPGAME_OPENXR ?= 0
ifeq ($(REPGAME_OPENXR),1)
LINUX_OUT ?= out/linux-openxr
endif

# Output directory for Linux build artifacts. The portable (Docker) build
# overrides this to out/linux-portable so host and Docker .o files don't clobber.
LINUX_OUT ?= out/linux
# Output directory for AppImage artifacts. The portable (Docker) build overrides
# this to out/appimage-portable so host and Docker AppImages don't clobber.
APPIMAGE_OUT ?= out/appimage

# Sounds: the source audio in sounds/ (untracked — see .gitignore) is listed in
# tools/sounds_manifest.txt, converted to OGG Vorbis by tools/convert_sounds.sh
# (needs ffmpeg), and embedded as blobs like the bitmaps. When sounds/ is
# absent (fresh clones) the build silently compiles the audio stub instead.
# The sources are copyrighted Minecraft WAVs, so they're fetched as a tarball
# from repkap11.com (same pattern as the glew zip in windows.mk) rather than
# tracked in git. Not an `install` dep — `make mc_pack_build` once, manually.
# The dir target makes it idempotent: skipped when sounds/mc_pack exists.
mc_pack_build: sounds/mc_pack
sounds/mc_pack:
	mkdir -p sounds
	wget -q https://repkap11.com/files/repgame/mc_pack.tar.gz -O mc_pack.tar.gz
	bsdtar -xvzf mc_pack.tar.gz -C sounds
	rm -rf mc_pack.tar.gz
ifneq ($(wildcard sounds),)
REPGAME_AUDIO ?= 1
endif
REPGAME_AUDIO ?= 0
SOUNDS_ENABLED :=
ifeq ($(REPGAME_AUDIO),1)
SOUNDS_ENABLED := $(shell grep '^[a-z]' tools/sounds_manifest.txt | cut -d'|' -f1)
endif
# Directory the .ogg files come from; the flatpak build overrides this with
# prebuilt/sounds because its sandbox has no ffmpeg (see sounds-prebuilt).
SOUND_OGG_DIR ?= out/sounds

out/sounds/%.ogg : tools/sounds_manifest.txt tools/convert_sounds.sh | out
	tools/convert_sounds.sh convert $* out/sounds

# The .ogg files are still converted by make (rules above); cmake only
# embeds/stages them. Skip when SOUND_OGG_DIR points elsewhere (flatpak
# prebuilt). Used by the linux/arm/steamframe/windows/wasm wrappers.
SOUND_OGGS :=
ifeq ($(REPGAME_AUDIO),1)
ifeq ($(SOUND_OGG_DIR),out/sounds)
SOUND_OGGS := $(patsubst %,out/sounds/%.ogg,$(SOUNDS_ENABLED))
endif
endif

# Host-side conversion for builds that can't run ffmpeg in their sandbox
# (flatpak). Mirrors the gpu-prebuilt pattern: `make flatpak` depends on this.
sounds-prebuilt:
	tools/convert_sounds.sh convert-all prebuilt/sounds

clean: clean-sounds
clean-sounds:
	rm -rf out/sounds out/include

.PRECIOUS: $(patsubst %,out/sounds/%.ogg,$(SOUNDS_ENABLED))

# Mob model/skin importer: regenerates include/common/mob_models_gen.hpp and
# bitmaps/mobs.png from tools/mobs_manifest.txt against the local
# bedrock-samples checkout (not part of the normal build — the outputs are
# committed). BEDROCK_SAMPLES points at the resource_pack dir.
BEDROCK_SAMPLES_TAG ?= v1.26.60.5
BEDROCK_SAMPLES ?= $(CURDIR)/bedrock-samples/resource_pack

# In-repo, git-ignored checkout of Mojang/bedrock-samples used by the import
# tools. Clone it once with `make bedrock_samples_build`; the dir target
# makes it idempotent (skipped when bedrock-samples/ already exists).
bedrock_samples_build: bedrock-samples
bedrock-samples:
	git clone --depth 1 --branch $(BEDROCK_SAMPLES_TAG) https://github.com/Mojang/bedrock-samples.git bedrock-samples

mobs-import:
	BEDROCK_SAMPLES=$(BEDROCK_SAMPLES) python3 tools/mob_import.py

# Block definition/texture importer: regenerates
# include/common/block_ids_gen.hpp, include/common/texture_ids_gen.hpp,
# src/common/block_definitions_gen.inc and bitmaps/blocks.png from
# data/blocks/*.json + data/textures/*.png (outputs are committed, so normal
# builds never need bedrock-samples).
blocks-import:
	BEDROCK_SAMPLES=$(BEDROCK_SAMPLES) python3 tools/blocks_import.py

# -I paths mirror the linux build (CMakeLists.txt) so project headers
# resolve; -i drops other-platform sources that can't compile under
# REPGAME_LINUX anyway. Vendored headers reached via -I are silenced by
# the `*:` entries in tools/cppcheck-suppressions.txt (-i doesn't apply
# to include-resolved files) — keep that list honest.
# out/cppcheck caches per-TU results by hash: reruns only re-analyze
# changed files (also enables whole-program/CTU analysis). Wipe the dir
# to force a full recheck.
check:
	mkdir -p out/cppcheck
	cppcheck -j$(CPUS) --quiet --enable=warning,style,performance,portability,information,missingInclude \
		-Iinclude -Ientt_build/src -Iimgui_build -Iimgui_build/backends \
		-isrc/android -isrc/wasm -isrc/windows -isrc/linux/wayland-protocols \
		-DREPGAME_LINUX -DGLEW_NO_GLU -DREPGAME_AUDIO \
		--cppcheck-build-dir=out/cppcheck \
		--suppressions-list=tools/cppcheck-suppressions.txt \
		src

# All test suites: the in-game rep_tests + the server --selftest.
# `make test-all` additionally runs the mp end-to-end scenarios (slow).
test: tests server-test

# Scripted game scenarios: each tests/scenarios/*.rds through rep-dbg
# script (kind "script", isolated HOME, flat world by default).
# SCENARIOS="substr ..." limits to files containing a substring.
scenarios: linux
	@fail=0; \
	for f in tests/scenarios/*.rds; do \
		skip=0; \
		if [ -n "$(SCENARIOS)" ]; then \
			skip=1; \
			for s in $(SCENARIOS); do \
				case "$$f" in *$$s*) skip=0;; esac; \
			done; \
		fi; \
		[ $$skip = 1 ] && continue; \
		echo "=== $$f"; \
		tools/rep-dbg script "$$f" || fail=1; \
	done; \
	exit $$fail

test-all: test mp-test scenarios

.PHONY: check mobs-import blocks-import test test-all scenarios mc_pack_build
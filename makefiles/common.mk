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

# Warning set shared by every platform (now in CMakeLists.txt's repgame_flags):
# warnings are errors so a warning on one toolchain can't silently ship on
# another.

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

# Shader preprocessor: runs the C preprocessor on GLSL sources at build time.
# -x assembler-with-cpp passes #version through untouched (it's not a valid C
# preprocessor directive) while still expanding #define/#if/#ifdef/#endif.
# -P suppresses line markers for clean GLSL output.
# -nostdinc prevents system headers from leaking into shader preprocessing.
# -I include/ lets shaders #include preprocessor-safe headers (e.g.
# include/common/chunk_dimensions.hpp). Per-platform makefiles append only
# -DREPGAME_<PLATFORM> (so that header picks the right CHUNK_SIZE_*) — the
# quality tiers are runtime-only; all platforms compile the same shaders.
SHADER_PP := gcc -E -P -x assembler-with-cpp -nostdinc -I include
# Shader dep files live outside the output dirs: android assets and
# out/wasm/fs are shipped verbatim, so .d files there would be packaged.
SHADER_DEP_DIR := out/shader-deps
# $(call shader_pp,<extra flags>) preprocesses $< to $@ and writes a gcc
# dep file so every #included header becomes a prerequisite.
define shader_pp
@mkdir -p $(dir $@) $(dir $(SHADER_DEP_DIR)/$@)
$(SHADER_PP) -MMD -MP -MT $@ -MF $(SHADER_DEP_DIR)/$@.d $(1) $< -o $@
endef

# Game textures are PNGs decoded by stb_image at load time. bitmaps/icons/
# holds packaging assets (appimage/flatpak/wasm page), not textures — the
# non-recursive wildcard only embeds the top level.
BITMAP_PNGS := $(wildcard bitmaps/*.png)
BITMAP_BINS := $(patsubst bitmaps/%.png,out/bitmaps/%.bin,$(BITMAP_PNGS))

# Embed the whole .png as-is; texture.cpp decodes it with stb_image at load.
out/bitmaps/%.bin : bitmaps/%.png makefiles/common.mk | out/bitmaps
	cp $< $@

out/bitmaps: | out
	mkdir -p out/bitmaps
	touch $@

clean: clean-bitmaps
clean-bitmaps:
	rm -rf out/bitmaps

clean: clean-shader-deps
clean-shader-deps:
	rm -rf out/shader-deps

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

# Generated MK_BLOB decls + music/sfx tables included by audio.cpp — now a
# CMake custom command per build dir (CMakeLists.txt); android.mk preprocesses
# shaders into assets but compiles audio.cpp in Gradle/CMake too.

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

check:
	cppcheck -j$(CPUS) --quiet --enable=warning,style,performance,portability,information,missingInclude -iinclude/common/vendor -DREPGAME_LINUX src
# cppcheck -j$(CPUS) --quiet --enable=warning,style,performance,portability,information,missingInclude -Iinclude -iinclude/common/vendor -DREPGAME_WINDOWS src
# cppcheck -j$(CPUS) --quiet --enable=warning,style,performance,portability,information,missingInclude -Iinclude -iinclude/common/vendor -DREPGAME_ANDROID src
# cppcheck -j$(CPUS) --quiet --enable=warning,style,performance,portability,information,missingInclude -Iinclude -iinclude/common/vendor -DREPGAME_WASM src
# ~/.local/bin/cpplint --filter=-whitespace,-legal/copyright,-readability/todo --quiet $(SRC_ALL_C)

# All test suites: the in-game rep_tests + the server --selftest.
# `make test-all` additionally runs the mp end-to-end scenarios (slow).
test: tests server-test

test-all: test mp-test

.PHONY: clean-bitmaps check mobs-import blocks-import test test-all mc_pack_build

.PRECIOUS: $(BITMAP_BINS)
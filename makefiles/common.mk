#Shared between platforms

USE_CCACHE ?= 1

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

SRC_COMMON := $(wildcard src/common/*.cpp) $(wildcard src/common/**/*.cpp) 
SRC_IMGUI := imgui_build/backends/imgui_impl_sdl3.cpp $(wildcard imgui_build/imgui*.cpp)  imgui_build/backends/imgui_impl_opengl3.cpp
SRC_ALL_C := $(wildcard src/**/*.cpp) $(wildcard src/**/**/*.cpp) $(wildcard src/**/*.cu) $(wildcard src/**/**/*.cu)
INCLUDES_COMMON := -I include/ -I include/glm -I imgui_build -I imgui_build/backends -isystem entt_build/src
HEADERS := $(wildcard include/**/*.hpp)
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

check:
	cppcheck -j$(CPUS) --quiet --enable=warning,style,performance,portability,information,missingInclude -iinclude/common/vendor -DREPGAME_LINUX src
# cppcheck -j$(CPUS) --quiet --enable=warning,style,performance,portability,information,missingInclude -Iinclude -iinclude/common/vendor -DREPGAME_WINDOWS src
# cppcheck -j$(CPUS) --quiet --enable=warning,style,performance,portability,information,missingInclude -Iinclude -iinclude/common/vendor -DREPGAME_ANDROID src
# cppcheck -j$(CPUS) --quiet --enable=warning,style,performance,portability,information,missingInclude -Iinclude -iinclude/common/vendor -DREPGAME_WASM src
# ~/.local/bin/cpplint --filter=-whitespace,-legal/copyright,-readability/todo --quiet $(SRC_ALL_C)

out/vars/%: | out/vars
	@echo Changed var:$(notdir $(basename $@)):$($(notdir $(basename $@)))
	rm -rf $(basename $@).*
	touch $@

out/vars: | out
	mkdir -p $@
	touch $@

clean: clean-vars
clean-vars:
	rm -rf out/vars

.PHONY: clean-bitmaps check clean-vars test-vars

.PRECIOUS: $(BITMAP_BINS)
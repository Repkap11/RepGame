# Cross-compile the Windows client with mingw-w64. Compile is CMake
# (REPGAME_PLATFORM=windows, toolchain cmake/toolchains/windows-mingw.cmake);
# this file is the task runner. Release only.
#
#   make windows        — builds out/windows/RepGame.exe (one portable exe:
#                         SDL3, libstdc++ and winpthread are linked static)
#   make windows-run    — run it under Wine, WORLD on www.repkap11.com
#   make windows-deploy — rsync the exe to repkap11.com
#   make windows_build  — build static SDL3 + download GLEW
#   make windows-sdl3   — (re)build the static SDL3

all: windows
deploy: windows-deploy
install: windows_build

WINDOWS_SDL3_VERSION ?= 3.4.16
WINDOWS_SDL3_DIR := windows_build/sdl3
WINDOWS_SDL3_SRC := $(abspath $(WINDOWS_SDL3_DIR))/SDL3-$(WINDOWS_SDL3_VERSION)
WINDOWS_SDL3_PREFIX := $(abspath $(WINDOWS_SDL3_DIR))/install

CMAKE_WINDOWS_DFLAGS := -DREPGAME_AUDIO=$(REPGAME_AUDIO) -DREPGAME_SOUND_OGG_DIR=$(abspath $(SOUND_OGG_DIR)) \
	-DREPGAME_OUTPUT_DIR=$(abspath out/windows) -DREPGAME_SDL3_PREFIX=$(WINDOWS_SDL3_PREFIX)

# Built/downloaded deps. Not tracked by git.
WINDOWS_DEP := $(WINDOWS_SDL3_PREFIX)/lib/libSDL3.a windows_build/glew/glew-2.1.0-mingw-w64.zip

windows : out/windows/RepGame.exe

# File target so other makefiles can depend on it. Re-running cmake every
# time is cheap and makes -D overrides stick.
out/windows/RepGame.exe : $(SOUND_OGGS) windows_build FORCE
	$(call cmake_build,windows,out/windows/cmake,$(CMAKE_WINDOWS_DFLAGS))

windows-run : out/windows/RepGame.exe
	wine out/windows/RepGame.exe "$(WORLD)" www.repkap11.com

windows-deploy : out/windows/RepGame.exe
	rsync $< paul@repkap11.com:/home/paul/website/${TARGET_LOWER}

windows_build : $(WINDOWS_DEP)

# The SDL3 mingw devel package only ships SDL3.dll + an import lib, so build
# a static libSDL3.a from the release tarball (same one as arm/steamframe)
# with the mingw toolchain.
$(WINDOWS_SDL3_PREFIX)/lib/libSDL3.a :
	mkdir -p $(WINDOWS_SDL3_DIR)
	cd $(WINDOWS_SDL3_DIR) && \
		wget -q https://github.com/libsdl-org/SDL/releases/download/release-$(WINDOWS_SDL3_VERSION)/SDL3-$(WINDOWS_SDL3_VERSION).tar.gz && \
		tar xzf SDL3-$(WINDOWS_SDL3_VERSION).tar.gz
	cmake -S $(WINDOWS_SDL3_SRC) -B $(WINDOWS_SDL3_SRC)/build -G Ninja \
		-DCMAKE_TOOLCHAIN_FILE=$(abspath cmake/toolchains/windows-mingw.cmake) \
		-DCMAKE_BUILD_TYPE=Release \
		-DCMAKE_INSTALL_PREFIX=$(WINDOWS_SDL3_PREFIX) \
		-DSDL_SHARED=OFF -DSDL_STATIC=ON -DSDL_TESTS=OFF -DSDL_TEST_LIBRARY=OFF \
		-DSDL_EXAMPLES=OFF -DSDL_VULKAN=OFF
	cmake --build $(WINDOWS_SDL3_SRC)/build
	cmake --install $(WINDOWS_SDL3_SRC)/build

windows-sdl3 : $(WINDOWS_SDL3_PREFIX)/lib/libSDL3.a

# MinGW build of glew hosted on repkap11.com (the upstream -win32 zip ships
# MSVC .lib, not libglew32.a). The zip's top-level dir is glew/, so it unpacks
# one level deep and gets moved up.
windows_build/glew/glew-2.1.0-mingw-w64.zip :
	mkdir -p windows_build/glew
	wget https://repkap11.com/files/repgame/glew-2.1.0-mingw-w64.zip -O $@
	unzip $@ -d windows_build/glew
	mv windows_build/glew/glew/* windows_build/glew/
	rm -rf windows_build/glew/glew

clean: clean-windows
clean-windows:
	rm -rf out/windows

.PHONY: windows windows-run windows-deploy windows_build windows-sdl3 clean-windows

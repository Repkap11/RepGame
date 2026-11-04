# Cross-compile the Windows client with mingw-w64. Compile is CMake
# (REPGAME_PLATFORM=windows, toolchain cmake/toolchains/windows-mingw.cmake);
# this file is the task runner. Release only.
#
#   make windows        — builds out/windows/RepGame.exe + SDL3.dll
#   make windows-run    — run it under Wine
#   make windows-deploy — rsync the exe to repkap11.com
#   make windows_build  — download SDL3 + GLEW dev packages

all: windows
deploy: windows-deploy
install: windows_build

# The SDL3.dll that must sit next to the exe at runtime (cmake copies it
# post-build). Kept as a target so `make SDL3.dll` still works.
SDL3.dll : out/windows/SDL3.dll
out/windows/SDL3.dll : out/windows/RepGame.exe

WINDOWS_SDL3_VERSION := 3.4.16

CMAKE_WINDOWS_DFLAGS := -DREPGAME_AUDIO=$(REPGAME_AUDIO) -DREPGAME_SOUND_OGG_DIR=$(abspath $(SOUND_OGG_DIR)) \
	-DREPGAME_OUTPUT_DIR=$(abspath out/windows)
ifeq ($(USE_CCACHE),1)
CMAKE_WINDOWS_DFLAGS += -DCMAKE_CXX_COMPILER_LAUNCHER=ccache
endif

# Downloaded deps. Not tracked by git.
WINDOWS_DEP := windows_build/sdl3/SDL3-$(WINDOWS_SDL3_VERSION).tar.gz windows_build/glew/glew-2.1.0-mingw-w64.zip

windows : out/windows/RepGame.exe

# File target so other makefiles can depend on it. Re-running cmake every
# time is cheap and makes -D overrides stick.
out/windows/RepGame.exe : $(SOUND_OGGS) windows_build FORCE
	cmake --preset windows -B out/windows/cmake $(CMAKE_WINDOWS_DFLAGS)
	MAKEFLAGS="$(CMAKE_MAKEFLAGS)" cmake --build out/windows/cmake

FORCE:

windows-run : out/windows/RepGame.exe
	wine out/windows/RepGame.exe "$(WORLD_DEBUG)"

windows-deploy : out/windows/RepGame.exe
	rsync $< paul@repkap11.com:/home/paul/website/${TARGET_LOWER}

windows_build : $(WINDOWS_DEP)

windows_build/sdl3/SDL3-$(WINDOWS_SDL3_VERSION).tar.gz :
	mkdir -p windows_build/sdl3
	wget https://github.com/libsdl-org/SDL/releases/download/release-$(WINDOWS_SDL3_VERSION)/SDL3-devel-$(WINDOWS_SDL3_VERSION)-mingw.tar.gz -O $@
	tar -xzf $@ -C windows_build/sdl3 --strip-components=1

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

.PHONY: windows windows-run windows-deploy windows_build clean-windows FORCE

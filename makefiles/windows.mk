#Windows x86_64 builds

REPGAME_PACKAGES += mingw-w64 upx-ucl

CFLAGS_WINDOWS := -Wall -Werror -std=c++17 -Wno-unused-variable -fno-pie -D GLEW_STATIC -mwindows -D _WIN32_WINNT=0x0A00

CFLAGS_WINDOWS += -O3 -DREPGAME_SKIP_CHECK_FOR_GL_ERRORS -DREPGAME_HW_VSYNC
# CFLAGS_WINDOWS += -g

CFLAGS_WINDOWS += -DREPGAME_WINDOWS
# SDL3 MinGW devel only ships a shared library (libSDL3.dll.a + SDL3.dll),
# so Windows now dynamically links SDL3 and ships SDL3.dll next to the exe.
LIBS_WINDOWS_SDL3 := windows_build/sdl3/x86_64-w64-mingw32/lib/libSDL3.dll.a -Wl,--no-undefined -Wl,--dynamicbase -Wl,--nxcompat -Wl,--high-entropy-va -lm -luser32 -lgdi32 -lwinmm -limm32 -lole32 -loleaut32 -lshell32 -lsetupapi -lversion -luuid
LIBS_WINDOWS := windows_build/glew/lib/libglew32.a $(LIBS_WINDOWS_SDL3) -lopengl32 -lglu32 -Wl,-Bstatic -lpthread -Wl,-Bdynamic -static-libgcc -static-libstdc++
INCLUDES_WINDOWS := -I windows_build/sdl3/x86_64-w64-mingw32/include/SDL3 -I windows_build/sdl3/x86_64-w64-mingw32/include -I windows_build/glew/include

CC_WINDOWS := x86_64-w64-mingw32-g++
# CC_WINDOWS := clang++

LD_WINDOWS := x86_64-w64-mingw32-ld

ifeq ($(USE_CCACHE),1)
CC_WINDOWS := ccache $(CC_WINDOWS)
LD_WINDOWS := ccache $(LD_WINDOWS)
endif

OBJECTS_COMMON_WINDOWS := $(patsubst src/common/%.cpp,out/windows/common/%.o, $(SRC_COMMON))
OBJECTS_IMGUI_WINDOWS := $(patsubst imgui_build/%.cpp,out/windows/imgui/%.o, $(SRC_IMGUI))
OBJECTS_WINDOWS := $(patsubst src/%.cpp,out/windows/%.o, $(wildcard src/windows/*.cpp))
DEPS_WINDOWS := $(patsubst src/%.cpp,out/windows/%.d, $(wildcard src/windows/*.cpp)) \
			$(patsubst src/common/%.cpp,out/windows/common/%.d, $(SRC_COMMON))

SHADER_BLOBS_WINDOWS := $(patsubst src/shaders/%.glsl,out/windows/shaders/%.o,$(wildcard src/shaders/*.glsl))
SHADER_PROCESSED_WINDOWS := $(patsubst src/shaders/%.glsl,out/windows/shaders/%.glsl,$(wildcard src/shaders/*.glsl))
BITMAP_BLOBS_WINDOWS := $(patsubst bitmaps/%.bmp,out/windows/bitmaps/%.o,$(wildcard bitmaps/*.bmp))
FONT_BLOBS_WINDOWS := $(patsubst fonts/%.ttf,out/windows/fonts/%.o,$(wildcard fonts/*.ttf))

# Preprocess shaders through the C preprocessor at build time. Blob symbols are
# renamed to match what MK_BLOB(src_shaders, <name>, glsl) expects.
out/windows/shaders/%.glsl: src/shaders/%.glsl | out/windows
	@mkdir -p $(dir $@)
	$(SHADER_PP) $< -o $@

out/windows/shaders/%.o : out/windows/shaders/%.glsl | out/windows
	$(LD_WINDOWS) -r -b binary $< -o $@_no_section
	x86_64-w64-mingw32-objcopy \
		--redefine-sym _binary_out_windows_shaders_$*_glsl_start=_binary_src_shaders_$*_glsl_start \
		--redefine-sym _binary_out_windows_shaders_$*_glsl_end=_binary_src_shaders_$*_glsl_end \
		$@_no_section $@

out/windows/bitmaps/%.o : out/bitmaps/%.bin | out/windows
	$(LD_WINDOWS) -r -b binary $< -o $@
	objcopy --rename-section .data=.rodata,CONTENTS,ALLOC,LOAD,READONLY,DATA --reverse-bytes=4 $@ $@

out/windows/fonts/%.o : fonts/%.ttf | out/windows
	$(LD_WINDOWS) -r -b binary $< -o $@
	objcopy --rename-section .data=.rodata,CONTENTS,ALLOC,LOAD,READONLY,DATA $@ $@

all: windows
windows:  out/windows/$(TARGET).exe

deploy: windows-deploy

windows-deploy: out/windows/$(TARGET).exe
	rsync $< paul@repkap11.com:/home/paul/website/${TARGET_LOWER}

WINDOWS_DIRS = $(patsubst src%,out/windows%,$(shell find src -type d)) \
		out/windows/shaders out/windows/bitmaps out/windows/fonts \
		out/windows/imgui \
		out/windows/imgui/backends

out/windows/%.o: src/%.cpp windows_build | out/windows
	@#Use g++ to build o file and a dependecy tree .d file for every cpp file
	$(CC_WINDOWS) $(INCLUDES_WINDOWS) $(INCLUDES_COMMON) $(CFLAGS_WINDOWS) -MMD -MP -MF $(patsubst %.o,%.d,$@) -MT $(patsubst %.d,%.o,$@) -c $< -o $@

out/windows/imgui/%.o: imgui_build/%.cpp windows_build | out/windows
	@#Use g++ to build o file and a dependecy tree .d file for every cpp file
	$(CC_WINDOWS) $(INCLUDES_WINDOWS) $(INCLUDES_COMMON) $(CFLAGS_WINDOWS) -MMD -MP -MF $(patsubst %.o,%.d,$@) -MT $(patsubst %.d,%.o,$@) -c $< -o $@

#Include these .d files, so the dependicies are known for secondary builds.
-include $(DEPS_WINDOWS)

out/windows/$(TARGET).exe: out/windows/$(TARGET)_uncompressed.exe
	rm -f $@
	upx-ucl -q $< -o $@
	touch $@

out/windows/$(TARGET)_uncompressed.exe: windows_build $(OBJECTS_COMMON_WINDOWS) $(OBJECTS_IMGUI_WINDOWS) $(OBJECTS_WINDOWS) $(SHADER_BLOBS_WINDOWS) $(BITMAP_BLOBS_WINDOWS) $(FONT_BLOBS_WINDOWS) | out/windows
	$(CC_WINDOWS) -flto $(CFLAGS_WINDOWS) $(OBJECTS_WINDOWS) $(OBJECTS_COMMON_WINDOWS) $(OBJECTS_IMGUI_WINDOWS) $(SHADER_BLOBS_WINDOWS) $(BITMAP_BLOBS_WINDOWS) $(FONT_BLOBS_WINDOWS) $(LIBS_WINDOWS) -o $@
	# SDL3 is dynamically linked on Windows, so ship SDL3.dll next to the exe.
	cp windows_build/sdl3/x86_64-w64-mingw32/bin/SDL3.dll out/windows/

windows-run: windows
	wine out/windows/$(TARGET).exe "$(WORLD)"

clean: clean-windows

clean-windows:
	rm -rf out/windows

out/windows: | out
	mkdir -p $(WINDOWS_DIRS)
	touch $@

install: windows_build
windows_build:
	rm -rf glew.zip
	rm -rf windows_build
	mkdir -p windows_build/glew
	mkdir -p windows_build/sdl3
	wget -q https://github.com/libsdl-org/SDL/releases/download/release-3.4.16/SDL3-devel-3.4.16-mingw.tar.gz -O sdl3.tar.gz
	wget -q https://repkap11.com/files/repgame/glew-2.1.0-mingw-w64.zip -O glew.zip
	bsdtar --strip-components=1 -xvzf glew.zip -C windows_build/glew
	bsdtar --strip-components=1 -xvzf sdl3.tar.gz -C windows_build/sdl3
	rm -rf sdl3.tar.gz
	rm -rf glew.zip

.PRECIOUS: out/windows/$(TARGET).exe $(OBJECTS_WINDOWS) $(OBJECTS_COMMON_WINDOWS) $(SHADER_BLOBS_WINDOWS) $(SHADER_PROCESSED_WINDOWS) $(BITMAP_BLOBS_WINDOWS) $(FONT_BLOBS_WINDOWS)

.PHONY: windows windows-run clean-windows windows-deploy

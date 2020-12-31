#Windows x86_64 builds
REP_MAKEFILES += makefiles/windows.mk

REPGAME_PACKAGES += mingw-w64

CFLAGS_WINDOWS := -Wall -Werror -std=gnu++11 -Wno-unused-variable -fno-pie -D GLEW_STATIC -mwindows

#CFLAGS_WINDOWS += -O3 -DREPGAME_FAST
CFLAGS_WINDOWS += -g

CFLAGS_WINDOWS += -DREPGAME_WINDOWS
LIBS_WINDOWS := windows_build/glew/lib/libglew32.a windows_build/sdl2/i686-w64-mingw32/bin/SDL2.dll -lopengl32 -lglu32 -Wl,-Bstatic -Wl,-Bdynamic -static-libgcc -static-libstdc++
INCLUDES_WINDOWS := -I windows_build/sdl2/i686-w64-mingw32/include -I windows_build/glew/include -I windows_build/glm/glm

# CC_WINDOWS := x86_64-w64-mingw32-g++
CC_WINDOWS := mingw32-g++
#CC_WINDOWS := clang++

ifeq ($(CC_WINDOWS),x86_64-w64-mingw32-g++)
	ifeq ($(UBUNTU_VERSION),18.04)
		CFLAGS_WINDOWS += -no-pie
	endif
endif

LD_WINDOWS := ld

ifeq ($(USE_CCACHE),1)
# CC_WINDOWS := ccache $(CC_WINDOWS)
# LD_WINDOWS := ccache $(LD_WINDOWS)
endif

OBJECTS_COMMON_WINDOWS := $(patsubst src/common/%.cpp,out/windows/common/%.o, $(SRC_COMMON))
OBJECTS_WINDOWS := $(patsubst src/%.cpp,out/windows/%.o, $(wildcard src/windows/*.cpp))
DEPS_WINDOWS := $(patsubst src/%.cpp,out/windows/%.d, $(wildcard src/windows/*.cpp)) \
			$(patsubst src/common/%.cpp,out/windows/common/%.d, $(SRC_COMMON))

SHADER_BLOBS_WINDOWS := $(patsubst src/shaders/%.glsl,out/windows/shaders/%.o,$(wildcard src/shaders/*.glsl))
BITMAP_BLOBS_WINDOWS := $(patsubst bitmaps/%.bmp,out/windows/bitmaps/%.o,$(wildcard bitmaps/*.bmp))

out/windows/shaders/%.o : src/shaders/%.glsl $(REP_MAKEFILES) | out/windows
	$(LD_WINDOWS) -r -b binary $< -o $@

out/windows/bitmaps/%.o : out/bitmaps/%.bin $(REP_MAKEFILES) | out/windows
	$(LD_WINDOWS) -r -b binary $< -o $@
	objcopy --rename-section .data=.rodata,CONTENTS,ALLOC,LOAD,READONLY,DATA --reverse-bytes=4 $@ $@

all: windows
docker-internal: windows
windows:  out/windows/$(TARGET).exe

deploy: windows-deploy

windows-deploy: out/windows/$(TARGET).exe
	rsync $< paul@repkap11.com:/home/paul/website/${TARGET_LOWER}

WINDOWS_DIRS = $(patsubst src%,out/windows%,$(shell find src -type d)) \
	   out/windows/shaders out/windows/bitmaps

out/windows/%.o: src/%.cpp $(REP_MAKEFILES) windows_build | out/windows
	@#Use g++ to build o file and a dependecy tree .d file for every cpp file
	$(CC_WINDOWS) $(INCLUDES_WINDOWS) $(INCLUDES_COMMON) $(CFLAGS_WINDOWS) -MMD -MP -MF $(patsubst %.o,%.d,$@) -MT $(patsubst %.d,%.o,$@) -c $< -o $@

#Include these .d files, so the dependicies are known for secondary builds.
-include $(DEPS_WINDOWS)

out/windows/SDL2.dll: windows_build/sdl2/i686-w64-mingw32/bin/SDL2.dll windows_build  | out/windows
	cp $< $@

out/windows/$(TARGET).exe: windows_build out/windows/SDL2.dll $(OBJECTS_COMMON_WINDOWS) $(OBJECTS_WINDOWS) $(SHADER_BLOBS_WINDOWS) $(BITMAP_BLOBS_WINDOWS) $(REP_MAKEFILES) | out/windows
	$(CC_WINDOWS) -flto $(CFLAGS_WINDOWS) $(OBJECTS_WINDOWS) $(OBJECTS_COMMON_WINDOWS) $(SHADER_BLOBS_WINDOWS) $(BITMAP_BLOBS_WINDOWS) $(LIBS_WINDOWS) -o $@

windows-run: windows
	out/windows/$(TARGET).exe "World1"

clean: clean-windows

clean-windows:
	rm -rf out/windows

out/windows: | out
	mkdir -p $(WINDOWS_DIRS)

install: windows_build
windows_build:
	# rm -rf glew.zip
	# rm -rf sdl2.tar.gz
	rm -rf windows_build
	mkdir -p windows_build/glew
	mkdir -p windows_build/sdl2
	
	# wget -q https://sourceforge.net/projects/glew/files/glew/2.1.0/glew-2.1.0.zip -O glew.zip
	# wget -q https://www.libsdl.org/release/SDL2-devel-2.0.10-mingw.tar.gz -O sdl2.tar.gz
	git clone --depth 1 https://github.com/g-truc/glm.git windows_build/glm

	#|| true needed on Windows build for some reason
	bsdtar --strip-components=1 -xvzf glew.zip -C windows_build/glew || true
	bsdtar --strip-components=1 -xvzf sdl2.tar.gz -C windows_build/sdl2 || true

	make -C windows_build/glew glew.lib
	# rm -rf sdl2.tar.gz
	# rm -rf glew.zip

.PRECIOUS: out/windows/$(TARGET).exe $(OBJECTS_WINDOWS) $(OBJECTS_COMMON_WINDOWS) $(SHADER_BLOBS_WINDOWS) $(BITMAP_BLOBS_WINDOWS)

.PHONY: windows windows-run clean-windows windows-deploy

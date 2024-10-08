#Linux x86_64 builds

REPGAME_PACKAGES += libglew-dev libxi-dev g++ libsdl2-dev upx-ucl cppcheck

CFLAGS_LINUX := -Wall -Wextra -std=c++17 -Wno-unused-parameter -Wno-unused-variable -fno-pie
LFLAGS := -z noexecstack

# In release mode, don't check for GL errors
# Use SW vsync since HW vsync causes tearing (at least for me).
CFLAGS_LINUX_RELEASE := -O3 -DREPGAME_SKIP_CHECK_FOR_GL_ERRORS -DREPGAME_SW_VSYNC
# Show GL errors, and unlimit the FPS to measure perfornamce.
CFLAGS_LINUX_DEBUG := -g

CFLAGS_LINUX += -DREPGAME_LINUX
# SDL2 is dynamically linked, because it's too hard to statically link against X11, and I don't want to compile it from source.
LIBS_LINUX := -lSDL2 -Wl,-Bstatic -lGLEW -lGLU -Wl,-Bdynamic -lGL -lpthread -lm -ldl -static-libgcc -static-libstdc++

CC_LINUX := g++
# CC_LINUX := clang++

LD_LINUX := ld
# LD_LINUX := gold

ifeq ($(CC_LINUX),g++)
		CFLAGS_LINUX += -no-pie
	ifeq ($(LD_LINUX),gold)
		CFLAGS_LINUX += -fuse-ld=gold
	endif
else
	CFLAGS_LINUX += -fPIE
endif

ifeq ($(USE_CCACHE),1)
CC_LINUX := ccache $(CC_LINUX)
endif

OBJECTS_COMMON_LINUX_RELEASE := $(patsubst src/common/%.cpp,out/linux/release/common/%.o, $(SRC_COMMON))
OBJECTS_COMMON_LINUX_DEBUG := $(patsubst src/common/%.cpp,out/linux/debug/common/%.o, $(SRC_COMMON))
OBJECTS_IMGUI_LINUX_RELEASE := $(patsubst imgui_build/%.cpp,out/linux/release/imgui/%.o, $(SRC_IMGUI))
OBJECTS_IMGUI_LINUX_DEBUG := $(patsubst imgui_build/%.cpp,out/linux/debug/imgui/%.o, $(SRC_IMGUI))

OBJECTS_LINUX_RELEASE := $(patsubst src/%.cpp,out/linux/release/%.o, $(wildcard src/linux/*.cpp))
OBJECTS_LINUX_DEBUG := $(patsubst src/%.cpp,out/linux/debug/%.o, $(wildcard src/linux/*.cpp))
DEPS_LINUX := $(patsubst src/%.cpp,out/linux/release/%.d, $(wildcard src/linux/*.cpp)) \
			$(patsubst src/%.cpp,out/linux/debug/%.d, $(wildcard src/linux/*.cpp)) \
			$(patsubst src/common/%.cpp,out/linux/debug/common/%.d, $(SRC_COMMON)) \
			$(patsubst src/common/%.cpp,out/linux/release/common/%.d, $(SRC_COMMON))

SHADER_BLOBS_LINUX := $(patsubst src/shaders/%.glsl,out/linux/shaders/%.o,$(wildcard src/shaders/*.glsl))
BITMAP_BLOBS_LINUX := $(patsubst bitmaps/%.bmp,out/linux/bitmaps/%.o,$(wildcard bitmaps/*.bmp))

out/linux/shaders/%.o : src/shaders/%.glsl | out/linux
	$(LD_LINUX) $(LFLAGS) -r -b binary $< -o $@_no_section
	objcopy --rename-section .data=.rodata,CONTENTS,ALLOC,LOAD,READONLY,DATA $@_no_section $@

out/linux/bitmaps/%.o : out/bitmaps/%.bin | out/linux
	$(LD_LINUX) $(LFLAGS) -r -b binary $< -o $@_no_section
	objcopy --rename-section .data=.rodata,CONTENTS,ALLOC,LOAD,READONLY,DATA --reverse-bytes=4 $@_no_section $@

all: linux
docker-internal: linux
linux: out/linux/release/$(TARGET)
linux_debug: out/linux/debug/$(TARGET)

deploy: linux-deploy

linux-deploy: out/linux/release/$(TARGET)
	rsync $< paul@repkap11.com:/home/paul/website/${TARGET_LOWER}

LINUX_DIRS := $(patsubst src%,out/linux/release%,$(shell find src -type d)) \
		$(patsubst src%,out/linux/debug%,$(shell find src -type d)) \
		$(patsubst src%,out/linux%,$(shell find src -type d)) \
		out/linux/debug out/linux/release \
		out/linux/shaders out/linux/bitmaps \
		out/linux/debug/imgui out/linux/release/imgui \
		out/linux/debug/imgui/backends out/linux/release/imgui/backends

-include makefiles/cuda.mk

out/linux/release/%.o: src/%.cpp | out/linux
	@#Use g++ to build o file and a dependecy tree .d file for every cpp file
	$(CC_LINUX) $(INCLUDES_COMMON) $(CFLAGS_LINUX) $(CFLAGS_LINUX_RELEASE) -MMD -MP -MF $(patsubst %.o,%.d,$@) -MT $(patsubst %.d,%.o,$@) -c $< -o $@

out/linux/debug/%.o: src/%.cpp | out/linux
	@#Use g++ to build o file and a dependecy tree .d file for every cpp file
	$(CC_LINUX) $(INCLUDES_COMMON) $(CFLAGS_LINUX) $(CFLAGS_LINUX_DEBUG) -MMD -MP -MF $(patsubst %.o,%.d,$@) -MT $(patsubst %.d,%.o,$@) -c $< -o $@


out/linux/debug/imgui/%.o: imgui_build/%.cpp | out/linux
	@#Use g++ to build o file and a dependecy tree .d file for every cpp file
	$(CC_LINUX) $(INCLUDES_COMMON) $(CFLAGS_LINUX) $(CFLAGS_LINUX_DEBUG) -MMD -MP -MF $(patsubst %.o,%.d,$@) -MT $(patsubst %.d,%.o,$@) -c $< -o $@

out/linux/release/imgui/%.o: imgui_build/%.cpp | out/linux
	@#Use g++ to build o file and a dependecy tree .d file for every cpp file
	$(CC_LINUX) $(INCLUDES_COMMON) $(CFLAGS_LINUX) $(CFLAGS_LINUX_RELEASE) -MMD -MP -MF $(patsubst %.o,%.d,$@) -MT $(patsubst %.d,%.o,$@) -c $< -o $@

#Include these .d files, so the dependicies are known for secondary builds.
-include $(DEPS_LINUX)

out/linux/release/$(TARGET): out/linux/release/$(TARGET)_uncompressed
	rm -f $@
	upx-ucl -q $< -o $@
	touch $@


out/linux/release/$(TARGET)_uncompressed: $(OBJECTS_IMGUI_LINUX_RELEASE) $(OBJECTS_COMMON_LINUX_RELEASE) $(OBJECTS_LINUX_RELEASE) $(SHADER_BLOBS_LINUX) $(BITMAP_BLOBS_LINUX) | out/linux
	$(CC_LINUX) -flto $(CFLAGS_LINUX) $(CFLAGS_LINUX_RELEASE) $(OBJECTS_LINUX_RELEASE) $(OBJECTS_IMGUI_LINUX_RELEASE) $(OBJECTS_COMMON_LINUX_RELEASE) $(SHADER_BLOBS_LINUX) $(BITMAP_BLOBS_LINUX) $(LIBS_LINUX) -o $@

out/linux/debug/$(TARGET): $(OBJECTS_IMGUI_LINUX_DEBUG) $(OBJECTS_COMMON_LINUX_DEBUG) $(OBJECTS_LINUX_DEBUG) $(SHADER_BLOBS_LINUX) $(BITMAP_BLOBS_LINUX) | out/linux
	$(CC_LINUX) $(CFLAGS_LINUX) $(CFLAGS_LINUX_DEBUG) $(OBJECTS_LINUX_DEBUG) $(OBJECTS_IMGUI_LINUX_DEBUG) $(OBJECTS_COMMON_LINUX_DEBUG) $(SHADER_BLOBS_LINUX) $(BITMAP_BLOBS_LINUX) $(LIBS_LINUX) -o $@

linux-run: linux
	./out/linux/release/$(TARGET) $(WORLD) www.repkap11.com

linux-run2: linux
	./out/linux/release/$(TARGET) World2 www.repkap11.com

dev: linux_debug-run
dev2: linux_debug-run2

linux_debug-run: linux_debug
	./out/linux/debug/$(TARGET) $(WORLD) localhost

linux_debug-run2: linux_debug
	./out/linux/debug/$(TARGET) World2 localhost

tests: linux_debug
	./out/linux/debug/$(TARGET) tests

# map:
# 	rm -rf ~/.repgame/$(WORLD)

player:
	rm -rf ~/.repgame/$(WORLD)/player.dat

clean: clean-linux

clean-linux:
	rm -rf out/linux

out/linux: | out
	mkdir -p $(LINUX_DIRS)
	touch $@

.PRECIOUS: out/linux/release/$(TARGET) out/linux/debug/$(TARGET) $(OBJECTS_LINUX_DEBUG) $(OBJECTS_LINUX_RELEASE) $(OBJECTS_COMMON_LINUX) $(SHADER_BLOBS_LINUX) $(BITMAP_BLOBS_LINUX)

.PHONY: linux linux-run clean-linux linux-deploy dev
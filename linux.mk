#Linux x86_64 builds
MAKEFILES += linux.mk

REPGAME_PACKAGES += freeglut3-dev libglew-dev libxi-dev

CFLAGS_LINUX := -Wall -Werror -std=c++98 -Wno-unused-variable -fno-pie -march=native

#CFLAGS_LINUX += -O3 -DREPGAME_FAST
CFLAGS_LINUX += -g


CFLAGS_LINUX += -DREPGAME_LINUX
LIBS_LINUX := -Wl,-Bstatic  -l GLU -l glut -lX11 -lXxf86vm -lXi -l xcb -lXau -lXdmcp -lXext -Wl,-Bdynamic -l m -l GL -l GLEW -lpthread -l dl  -static-libgcc -static-libstdc++

CC_LINUX := g++
#CC_LINUX := clang++

ifeq ($(CC_LINUX),g++)
CFLAGS_LINUX += -no-pie
endif
LD_LINUX := ld

ifeq ($(USE_CCACHE),1)
CC_LINUX := ccache $(CC_LINUX)
LD_LINUX := ccache $(LD_LINUX)
endif

OBJECTS_COMMON_LINUX := $(patsubst src/common/%.cpp,out/linux/common/%.o, $(SRC_COMMON))
OBJECTS_LINUX := $(patsubst src/%.cpp,out/linux/%.o, $(wildcard src/linux/*.cpp))
DEPS_LINUX := $(patsubst src/%.cpp,out/linux/%.d, $(wildcard src/linux/*.cpp)) \
			$(patsubst src/common/%.cpp,out/linux/common/%.d, $(SRC_COMMON))

SHADER_BLOBS_LINUX := $(patsubst src/shaders/%.glsl,out/linux/shaders/%.o.temp,$(wildcard src/shaders/*.glsl))
BITMAP_BLOBS_LINUX := $(patsubst bitmaps/%.bmp,out/linux/bitmaps/%.o,$(wildcard bitmaps/*.bmp))


out/linux/shaders/%.o.temp : src/shaders/%.glsl $(MAKEFILES) | out/linux
	$(LD_LINUX) -r -b binary $< -o $@
	objcopy --rename-section .data=.rodata,CONTENTS,ALLOC,LOAD,READONLY,DATA $@ $@

out/linux/bitmaps/%.o : out/bitmaps/%.bin $(MAKEFILES) | out/linux
	$(LD_LINUX) -r -b binary $< -o $@
	objcopy --rename-section .data=.rodata,CONTENTS,ALLOC,LOAD,READONLY,DATA --reverse-bytes=4 $@ $@

linux: $(TARGET)

linux-deploy: $(TARGET)
	rsync $< paul@repkap11.com:/home/paul/website/repgame

LINUX_DIRS = $(patsubst src%,out/linux%,$(shell find src -type d)) \
       $(patsubst src%,out/server%,$(shell find src -type d)) \
	   out/linux/shaders out/linux/bitmaps

out/linux/%.o: src/%.cpp $(MAKEFILES) | out/linux
	@#Use g++ to build o file and a dependecy tree .d file for every cpp file
	$(CC_LINUX) $(INCLUDES_COMMON) $(CFLAGS_LINUX) -MMD -MP -MF $(patsubst %.o,%.d,$@) -MT $(patsubst %.d,%.o,$@) -c $< -o $@

#Include these .d files, so the dependicies are known for secondary builds.
-include $(DEPS_LINUX)

# Server targets might depend on cuda.mk modifications
#include cuda.mk


$(TARGET): $(OBJECTS_COMMON_LINUX) $(OBJECTS_LINUX) $(MAKEFILES) $(SHADER_BLOBS_LINUX) $(BITMAP_BLOBS_LINUX) 
	$(CC_LINUX) -flto $(CFLAGS_LINUX) $(OBJECTS_LINUX) $(OBJECTS_COMMON_LINUX) $(SHADER_BLOBS_LINUX) $(BITMAP_BLOBS_LINUX) $(LIBS_LINUX) -o $@

include server.mk

linux-run: linux
	./$(TARGET)

map:
	rm -rf World1

reverse = $(if $(1),$(call reverse,$(wordlist 2,$(words $(1)),$(1)))) $(firstword $(1))

clean-linux: clean-server
	rm -rf out/linux

out/linux: | out
	#@echo Making linux $(LINUX_DIRS)
	mkdir -p $(LINUX_DIRS)

.PRECIOUS: $(TARGET) $(OBJECTS_LINUX) $(OBJECTS_COMMON_LINUX) $(LIB_TARGET_LINUX) $(SHADER_BLOBS_LINUX) $(BITMAP_BLOBS_LINUX)

.PHONY: linux linux-run clean-linux map linux-deploy
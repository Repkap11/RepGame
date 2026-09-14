#Linux x86_64 builds

REPGAME_PACKAGES += libglew-dev libxi-dev g++ libsdl3-dev upx-ucl cppcheck

# ld -b binary generates symbol names from the file path with non-alnum chars
# converted to underscores. Used for --redefine-sym in shader blob targets.
LINUX_OUT_SYMBOLS := $(subst /,_,$(subst .,_,$(subst -,_,$(LINUX_OUT))))

CFLAGS_LINUX := -Wall -Wextra -std=c++17 -Wno-unused-parameter -Wno-unused-variable -fno-pie
LFLAGS := -z noexecstack

# In release mode, don't check for GL errors
# Use HW vsync to sync to display refresh rate.
CFLAGS_LINUX_RELEASE := -O3 -DREPGAME_SKIP_CHECK_FOR_GL_ERRORS -DREPGAME_HW_VSYNC
# Show GL errors, and unlimit the FPS to measure perfornamce.
CFLAGS_LINUX_DEBUG := -g

CFLAGS_LINUX += -DREPGAME_LINUX
CFLAGS_LINUX += $(shell pkg-config --cflags sdl3)
CFLAGS_LINUX += $(shell pkg-config --cflags wayland-client)
# SDL3 is dynamically linked, because it's too hard to statically link against X11, and I don't want to compile it from source.
LIBS_LINUX := -lSDL3 -Wl,-Bstatic -lGLEW -lGLU -Wl,-Bdynamic -lGL -lpthread -lm -ldl -static-libgcc -static-libstdc++ -lwayland-client

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

OBJECTS_COMMON_LINUX_RELEASE := $(patsubst src/common/%.cpp,$(LINUX_OUT)/release/common/%.o, $(SRC_COMMON))
OBJECTS_COMMON_LINUX_DEBUG := $(patsubst src/common/%.cpp,$(LINUX_OUT)/debug/common/%.o, $(SRC_COMMON))
OBJECTS_IMGUI_LINUX_RELEASE := $(patsubst imgui_build/%.cpp,$(LINUX_OUT)/release/imgui/%.o, $(SRC_IMGUI))
OBJECTS_IMGUI_LINUX_DEBUG := $(patsubst imgui_build/%.cpp,$(LINUX_OUT)/debug/imgui/%.o, $(SRC_IMGUI))

OBJECTS_LINUX_RELEASE := $(patsubst src/%.cpp,$(LINUX_OUT)/release/%.o, $(wildcard src/linux/*.cpp))
OBJECTS_LINUX_DEBUG := $(patsubst src/%.cpp,$(LINUX_OUT)/debug/%.o, $(wildcard src/linux/*.cpp))
DEPS_LINUX := $(patsubst src/%.cpp,$(LINUX_OUT)/release/%.d, $(wildcard src/linux/*.cpp)) \
			$(patsubst src/%.cpp,$(LINUX_OUT)/debug/%.d, $(wildcard src/linux/*.cpp)) \
			$(patsubst src/common/%.cpp,$(LINUX_OUT)/debug/common/%.d, $(SRC_COMMON)) \
			$(patsubst src/common/%.cpp,$(LINUX_OUT)/release/common/%.d, $(SRC_COMMON))

SHADER_BLOBS_LINUX := $(patsubst src/shaders/%.glsl,$(LINUX_OUT)/shaders/%.o,$(wildcard src/shaders/*.glsl))
SHADER_PROCESSED_LINUX := $(patsubst src/shaders/%.glsl,$(LINUX_OUT)/shaders/%.glsl,$(wildcard src/shaders/*.glsl))
BITMAP_BLOBS_LINUX := $(patsubst bitmaps/%.bmp,$(LINUX_OUT)/bitmaps/%.o,$(wildcard bitmaps/*.bmp))
FONT_BLOBS_LINUX := $(patsubst fonts/%.ttf,$(LINUX_OUT)/fonts/%.o,$(wildcard fonts/*.ttf))

# Preprocess shaders through the C preprocessor at build time. The preprocessed
# .glsl file lives in $(LINUX_OUT)/shaders/ alongside the .o blob. The blob symbols
# are renamed to match what MK_BLOB(src_shaders, <name>, glsl) expects.
$(LINUX_OUT)/shaders/%.glsl: src/shaders/%.glsl | $(LINUX_OUT)
	@mkdir -p $(dir $@)
	$(SHADER_PP) $< -o $@

$(LINUX_OUT)/shaders/%.o : $(LINUX_OUT)/shaders/%.glsl | $(LINUX_OUT)
	$(LD_LINUX) $(LFLAGS) -r -b binary $< -o $@_no_section
	objcopy --rename-section .data=.rodata,CONTENTS,ALLOC,LOAD,READONLY,DATA \
		--redefine-sym _binary_$(LINUX_OUT_SYMBOLS)_shaders_$*_glsl_start=_binary_src_shaders_$*_glsl_start \
		--redefine-sym _binary_$(LINUX_OUT_SYMBOLS)_shaders_$*_glsl_end=_binary_src_shaders_$*_glsl_end \
		$@_no_section $@

$(LINUX_OUT)/bitmaps/%.o : out/bitmaps/%.bin | $(LINUX_OUT)
	$(LD_LINUX) $(LFLAGS) -r -b binary $< -o $@_no_section
	objcopy --rename-section .data=.rodata,CONTENTS,ALLOC,LOAD,READONLY,DATA --reverse-bytes=4 $@_no_section $@

$(LINUX_OUT)/fonts/%.o : fonts/%.ttf | $(LINUX_OUT)
	$(LD_LINUX) $(LFLAGS) -r -b binary $< -o $@_no_section
	objcopy --rename-section .data=.rodata,CONTENTS,ALLOC,LOAD,READONLY,DATA $@_no_section $@

all: linux
linux: $(LINUX_OUT)/release/$(TARGET)
linux_debug: $(LINUX_OUT)/debug/$(TARGET)

deploy: linux-deploy

linux-deploy: $(LINUX_OUT)/release/$(TARGET)
	rsync $< paul@repkap11.com:/home/paul/website/${TARGET_LOWER}

LINUX_DIRS := $(patsubst src%,$(LINUX_OUT)/release%,$(shell find src -type d)) \
		$(patsubst src%,$(LINUX_OUT)/debug%,$(shell find src -type d)) \
		$(patsubst src%,$(LINUX_OUT)%,$(shell find src -type d)) \
		$(LINUX_OUT)/debug $(LINUX_OUT)/release \
		$(LINUX_OUT)/shaders $(LINUX_OUT)/bitmaps $(LINUX_OUT)/fonts \
		$(LINUX_OUT)/debug/imgui $(LINUX_OUT)/release/imgui \
		$(LINUX_OUT)/debug/imgui/backends $(LINUX_OUT)/release/imgui/backends

-include makefiles/cuda.mk
-include makefiles/hip.mk

$(LINUX_OUT)/release/%.o: src/%.cpp | $(LINUX_OUT)
	@#Use g++ to build o file and a dependecy tree .d file for every cpp file
	$(CC_LINUX) $(INCLUDES_COMMON) $(CFLAGS_LINUX) $(CFLAGS_LINUX_RELEASE) -MMD -MP -MF $(patsubst %.o,%.d,$@) -MT $(patsubst %.d,%.o,$@) -c $< -o $@

$(LINUX_OUT)/debug/%.o: src/%.cpp | $(LINUX_OUT)
	@#Use g++ to build o file and a dependecy tree .d file for every cpp file
	$(CC_LINUX) $(INCLUDES_COMMON) $(CFLAGS_LINUX) $(CFLAGS_LINUX_DEBUG) -MMD -MP -MF $(patsubst %.o,%.d,$@) -MT $(patsubst %.d,%.o,$@) -c $< -o $@


$(LINUX_OUT)/debug/imgui/%.o: imgui_build/%.cpp | $(LINUX_OUT)
	@#Use g++ to build o file and a dependecy tree .d file for every cpp file
	$(CC_LINUX) $(INCLUDES_COMMON) $(CFLAGS_LINUX) $(CFLAGS_LINUX_DEBUG) -MMD -MP -MF $(patsubst %.o,%.d,$@) -MT $(patsubst %.d,%.o,$@) -c $< -o $@

$(LINUX_OUT)/release/imgui/%.o: imgui_build/%.cpp | $(LINUX_OUT)
	@#Use g++ to build o file and a dependecy tree .d file for every cpp file
	$(CC_LINUX) $(INCLUDES_COMMON) $(CFLAGS_LINUX) $(CFLAGS_LINUX_RELEASE) -MMD -MP -MF $(patsubst %.o,%.d,$@) -MT $(patsubst %.d,%.o,$@) -c $< -o $@

#Include these .d files, so the dependicies are known for secondary builds.
-include $(DEPS_LINUX)

$(LINUX_OUT)/release/$(TARGET): $(LINUX_OUT)/release/$(TARGET)_uncompressed
	rm -f $@
	upx-ucl -q $< -o $@
	touch $@


$(LINUX_OUT)/release/$(TARGET)_uncompressed: $(OBJECTS_IMGUI_LINUX_RELEASE) $(OBJECTS_COMMON_LINUX_RELEASE) $(OBJECTS_LINUX_RELEASE) $(SHADER_BLOBS_LINUX) $(BITMAP_BLOBS_LINUX) $(FONT_BLOBS_LINUX) | $(LINUX_OUT)
	$(CC_LINUX) -flto $(CFLAGS_LINUX) $(CFLAGS_LINUX_RELEASE) $(OBJECTS_LINUX_RELEASE) $(OBJECTS_IMGUI_LINUX_RELEASE) $(OBJECTS_COMMON_LINUX_RELEASE) $(SHADER_BLOBS_LINUX) $(BITMAP_BLOBS_LINUX) $(FONT_BLOBS_LINUX) $(LIBS_LINUX) -o $@

$(LINUX_OUT)/debug/$(TARGET): $(OBJECTS_IMGUI_LINUX_DEBUG) $(OBJECTS_COMMON_LINUX_DEBUG) $(OBJECTS_LINUX_DEBUG) $(SHADER_BLOBS_LINUX) $(BITMAP_BLOBS_LINUX) $(FONT_BLOBS_LINUX) | $(LINUX_OUT)
	$(CC_LINUX) $(CFLAGS_LINUX) $(CFLAGS_LINUX_DEBUG) $(OBJECTS_LINUX_DEBUG) $(OBJECTS_IMGUI_LINUX_DEBUG) $(OBJECTS_COMMON_LINUX_DEBUG) $(SHADER_BLOBS_LINUX) $(BITMAP_BLOBS_LINUX) $(FONT_BLOBS_LINUX) $(LIBS_LINUX) -o $@

linux-run: linux
	./$(LINUX_OUT)/release/$(TARGET) $(WORLD) www.repkap11.com

linux-run2: linux
	./$(LINUX_OUT)/release/$(TARGET) $(WORLD_DEBUG) www.repkap11.com

dev: linux_debug-run

linux_debug-run: linux_debug
	./$(LINUX_OUT)/debug/$(TARGET) $(WORLD_DEBUG) localhost

tests: linux_debug
	./$(LINUX_OUT)/debug/$(TARGET) tests

# map:
# 	rm -rf ~/.repgame/$(WORLD)

player:
	rm -rf ~/.repgame/$(WORLD)/player.dat

clean: clean-linux

clean-linux:
	rm -rf out/linux out/linux-portable

$(LINUX_OUT): | out
	mkdir -p $(LINUX_DIRS)
	touch $@

.PRECIOUS: $(LINUX_OUT)/release/$(TARGET) $(LINUX_OUT)/debug/$(TARGET) $(OBJECTS_LINUX_DEBUG) $(OBJECTS_LINUX_RELEASE) $(OBJECTS_COMMON_LINUX_RELEASE) $(OBJECTS_COMMON_LINUX_DEBUG) $(SHADER_BLOBS_LINUX) $(SHADER_PROCESSED_LINUX) $(BITMAP_BLOBS_LINUX) $(FONT_BLOBS_LINUX)

.PHONY: linux linux-run clean-linux linux-deploy dev

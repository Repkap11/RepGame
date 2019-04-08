BEFORE_VARS := $(.VARIABLES)

#This target can be used to depend on the contents of the makefiles
MAKEFILES := Makefile

CPUS ?= $(shell nproc || echo 1)
#SHELL = sh -xv

# OLD_SHELL := $(SHELL)
# SHELL = $(warning [Paul: $@ ($^) ($?)])$(OLD_SHELL)

#MAKEFLAGS += -k #Continue after failed targets
MAKEFLAGS += -r #Don't use build in commands
#MAKEFLAGS += -s #Be silent on stdout
MAKEFLAGS += --warn-undefined-variables
MAKEFLAGS += --jobs=$(CPUS)

TARGET := RepGame

#Default target
all: android linux windows wasm
	@echo "RepGame build complete..."

SRC_COMMON := $(wildcard src/common/*.cpp) $(wildcard src/common/abstract/*.cpp) $(wildcard src/common/utils/*.cpp)
INCLUDES_COMMON := -I include/ -I /usr/include/glm

HEADERS := $(wildcard include/**/*.hpp)


BITMAPS_NO_HEADER := $(patsubst bitmaps/%.bmp,out/bitmaps/%.bin,$(wildcard bitmaps/*.bmp))

out/bitmaps/%.bin : bitmaps/%.bmp $(MAKEFILES) | out
	tail -c +139 $< > $@

#Android targets might depend on linux.mk modifications
include wasm.mk
include linux.mk
include windows.mk
include android.mk

out:
	mkdir -p out out/bitmaps

run: linux android-run wasm
	${WASM_START_COMMAND} &
	./$(TARGET)

clean: clean-linux clean-windows clean-android clean-wasm
	rm -rf out/bitmaps
	rm -d out

install:
	sudo apt install freeglut3-dev libglew-dev libglm-dev libglm-doc nvidia-cuda-toolkit rsync libarchive-tools libxi-dev wget
	rm -rf freeglut.zip
	rm -rf glew.zip
	rm -rf windows_build
	mkdir -p windows_build/glew
	wget -q https://www.transmissionzero.co.uk/files/software/development/GLUT/freeglut-MinGW.zip -O freeglut.zip
	wget -q http://www.grhmedia.com/glew/glew-2.1.0-mingw-w64.zip -O glew.zip
	bsdtar --strip-components=1 -xvf glew.zip -C windows_build/glew
	bsdtar  -xvf freeglut.zip -C windows_build
	rm -rf freeglut.zip
	rm -rf glew.zip

.PHONY: run install all clean vars

nothing:

.PRECIOUS: $(BITMAPS_NO_HEADER)

vars:
	@echo "$(BEFORE_VARS) $(AFTER_VARS)" | xargs -n1 | sort | uniq -u

AFTER_VARS := $(.VARIABLES)

#$(info $$ANDROID_BITMAPS is [${ANDROID_BITMAPS}])
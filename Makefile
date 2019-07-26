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
TARGET_LOWER := repgame

USE_CCACHE := 1

#Default target
all: android linux windows wasm
	@echo "${TARGET} build complete..."

SRC_COMMON := $(wildcard src/common/*.cpp) $(wildcard src/common/**/*.cpp)
INCLUDES_COMMON := -I include/ -I /usr/include/glm

HEADERS := $(wildcard include/**/*.hpp)


BITMAPS_NO_HEADER := $(patsubst bitmaps/%.bmp,out/bitmaps/%.bin,$(wildcard bitmaps/*.bmp))

out/bitmaps/%.bin : bitmaps/%.bmp $(MAKEFILES) | out/bitmaps
	tail -c +139 $< > $@

REPGAME_PACKAGES := libglm-dev libglm-doc rsync libarchive-tools wget ccache

#Android targets might depend on linux.mk modifications
include wasm.mk
include linux.mk
include windows.mk
include android.mk
#Docker should be last since it uses REPGAME_PACKAGES
include docker.mk

out:
	mkdir -p out

out/bitmaps: | out
	mkdir -p out/bitmaps

run: linux android-run wasm
	${WASM_START_COMMAND} &
	./$(TARGET)

clean: clean-bitmaps clean-linux clean-windows clean-android clean-wasm
	rm -d out

clean-bitmaps:
	rm -rf out/bitmaps

windows_build:
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

install: windows_build
	sudo apt-get install -y $(REPGAME_PACKAGES)

.PHONY: run install all clean vars clean-bitmaps

deploy: windows-deploy linux-deploy wasm-deploy android-deploy

nothing:

.PRECIOUS: $(BITMAPS_NO_HEADER)

vars:
	@echo "$(BEFORE_VARS) $(AFTER_VARS)" | xargs -n1 | sort | uniq -u

AFTER_VARS := $(.VARIABLES)

$(info $$SRC_COMMON is [${SRC_COMMON}])
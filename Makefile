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
all: android linux wasm

SRC_COMMON := $(wildcard src/common/*.cpp) $(wildcard src/common/abstract/*.cpp) $(wildcard src/common/utils/*.cpp)
INCLUDES_COMMON := -I include/ -I /usr/include/glm

HEADERS := $(wildcard include/**/*.hpp)

#Android targets might depend on linux.mk modifications
include wasm.mk
include linux.mk
include android.mk

out:
	mkdir -p out

run: linux android-run wasm
	${WASM_START_COMMAND} &
	./$(TARGET)

clean: clean-linux clean-android clean-wasm
	rm -d out

install:
	sudo apt install freeglut3-dev libglew-dev libglm-dev libglm-doc nvidia-cuda-toolkit rsync

.PHONY: run install all clean vars

vars:
	@echo "$(BEFORE_VARS) $(AFTER_VARS)" | xargs -n1 | sort | uniq -u

AFTER_VARS := $(.VARIABLES)

#$(info $$WASM_BITMAPS is [${WASM_BITMAPS}])
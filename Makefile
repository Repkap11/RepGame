TARGET = RepGame
#CFLAGS = -Wall -Werror -std=c++98 -Wno-unused-variable -fPIC -O3
CFLAGS = -g -std=c++98 -Wno-unused-variable -fPIC
CPUS ?= $(shell nproc || echo 1)
MAKEFLAGS += --jobs=$(CPUS)
MAKEFLAGS += -k
#SHELL = sh -xv

#Default target
all: linux server android

DIRS = $(patsubst %/, %, $(sort $(dir $(patsubst src/%, out/linux/%, $(wildcard src/*) $(wildcard src/**/*)))))\
	   $(patsubst %/, %, $(sort $(dir $(patsubst src/%, out/server/%, $(wildcard src/*) $(wildcard src/**/*)))))\
	   $(patsubst %/, %, $(sort $(dir $(patsubst src/%, out/cuda/%, $(wildcard src/*) $(wildcard src/**/*)))))\
	   android/app/src/main/assets/shaders

ALL_SHARED = $(wildcard src/*.cpp) $(wildcard src/abstract/*.cpp) $(wildcard src/utils/*.cpp)
HEADERS = $(wildcard include/*.hpp) $(wildcard include/**/*.hpp)

#This makefile depends on its included makefiles
Makefile: linux.mk server.mk android.mk

include linux.mk
include android.mk

run: linux android-run
	./$(TARGET)

map:
	rm -rf World1

clean: clean-android clean-linux map

install:
	sudo apt install freeglut3-dev libglew-dev libglm-dev libglm-doc nvidia-cuda-toolkit

#$(info $$SHADERS_ANDROID is [${SHADERS_ANDROID}])
$(shell mkdir -p $(DIRS))
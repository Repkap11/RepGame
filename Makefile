TARGET = RepGame

#This target can be used to depend on the contents of the makefiles
MAKEFILES = Makefile

#CFLAGS = -Wall -Werror -std=c++98 -Wno-unused-variable -fPIC -O3
CFLAGS = -g -std=c++98 -Wno-unused-variable -fPIC
CPUS ?= $(shell nproc || echo 1)
MAKEFLAGS += --jobs=$(CPUS)
MAKEFLAGS += -k
#SHELL = sh -xv

#Default target
all: linux server android

#DIRS makes too many dirs...
DIRS = $(patsubst src%, out/linux%, $(shell find src -type d)) \
       $(patsubst src%, out/server%, $(shell find src -type d)) \
	   android/app/src/main/assets/shaders

SRC_COMMON = $(wildcard src/common/*.cpp) $(wildcard src/common/abstract/*.cpp) $(wildcard src/common/utils/*.cpp)
INCLUDES_COMMON = -I include/ -I /usr/include/glm

HEADERS = $(wildcard include/**/*.hpp)

#Android targets might depend on linux.mk modifications
include linux.mk
include android.mk

run: linux android-run
	./$(TARGET)

clean: clean-android clean-linux
	rm -rf out

install:
	sudo apt install freeglut3-dev libglew-dev libglm-dev libglm-doc nvidia-cuda-toolkit

out:
	mkdir -p $(DIRS)

.PHONY: run install all clean

#$(info $$MAKECMDGOALS is [${MAKECMDGOALS}])

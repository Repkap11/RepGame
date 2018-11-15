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

DIRS = $(patsubst src%, out/linux%, $(shell find src -type d))\
	   android/app/src/main/assets/shaders

SRC_COMMON = $(wildcard src/common/*.cpp) $(wildcard src/common/abstract/*.cpp) $(wildcard src/common/utils/*.cpp)
INCLUDES_COMMON = -I include/ -I /usr/include/glm

HEADERS = $(wildcard include/**/*.hpp)

#Android will rebuild if linux.mk changes
include linux.mk
include android.mk

run: linux android-run
	./$(TARGET)

map:
	rm -rf World1

clean: clean-android clean-linux map

install:
	sudo apt install freeglut3-dev libglew-dev libglm-dev libglm-doc nvidia-cuda-toolkit

#$(info $$MAKEFILES is [${MAKEFILES}])
$(shell mkdir -p $(DIRS))
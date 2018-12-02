BEFORE_VARS := $(.VARIABLES)

#This target can be used to depend on the contents of the makefiles
MAKEFILES := Makefile

CPUS ?= $(shell nproc || echo 1)
#SHELL = sh -xv

MAKEFLAGS += -k #Continue after failed targets
MAKEFLAGS += -r #Don't use build in commands
#MAKEFLAGS += -s #Be silent on stdout
MAKEFLAGS += --warn-undefined-variables
MAKEFLAGS += --jobs=$(CPUS)

TARGET := RepGame
CFLAGS := -Wall -Werror -std=c++98 -Wno-unused-variable -fPIC -O3
#CFLAGS := -Wall -Werror -std=c++98 -Wno-unused-variable -fPIC -g

#Default target
all: linux server android

SRC_COMMON := $(wildcard src/common/*.cpp) $(wildcard src/common/abstract/*.cpp) $(wildcard src/common/utils/*.cpp)
INCLUDES_COMMON := -I include/ -I /usr/include/glm

HEADERS := $(wildcard include/**/*.hpp)

#Android targets might depend on linux.mk modifications
include linux.mk
include android.mk

run: linux android-run
	./$(TARGET)

clean: clean-linux clean-android

install:
	sudo apt install freeglut3-dev libglew-dev libglm-dev libglm-doc nvidia-cuda-toolkit

.PHONY: run install all clean vars

vars:
	@echo "$(BEFORE_VARS) $(AFTER_VARS)" | xargs -n1 | sort | uniq -u

AFTER_VARS := $(.VARIABLES)
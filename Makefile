BEFORE_VARS := $(.VARIABLES)

#This target can be used to depend on the contents of the makefiles
REP_MAKEFILES = Makefile

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

#Default target
all: android linux windows wasm server
	@echo "${TARGET} build complete..."

#Sub makefiles might share variables. Cuda, server and linux do for sure.
include makefiles/common.mk
include makefiles/install.mk
include makefiles/wasm.mk
include makefiles/linux.mk #cuda.mk can be turned on inside linux.mk
include makefiles/server.mk
include makefiles/windows.mk
include makefiles/android.mk
include makefiles/docker.mk

out:
	mkdir -p out

clean: clean-bitmaps clean-linux clean-windows clean-android clean-wasm clean-server
	rm -d out

.PHONY: all clean vars

deploy: windows-deploy linux-deploy wasm-deploy android-deploy

nothing:

vars:
	@echo "$(BEFORE_VARS) $(AFTER_VARS)" | xargs -n1 | sort | uniq -u

AFTER_VARS := $(.VARIABLES)

#$(info $$DEPS_SERVER is [${DEPS_SERVER}])
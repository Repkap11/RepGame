BEFORE_VARS := $(.VARIABLES)

#This target can be used to depend on the contents of the makefiles
REP_MAKEFILES = Makefile

CPUS ?= $(shell nproc || echo 1)

#Current version of Ubuntu running make
UBUNTU_VERSION := $(shell lsb_release -r -s)

#Version of Ubuntu to target when build with docker
DOCKER_UBUNTU_VERSION ?= ${UBUNTU_VERSION}

#SHELL = sh -xv

# OLD_SHELL := $(SHELL)
# SHELL = $(warning [Paul: $@ ($^) ($?)])$(OLD_SHELL)

#MAKEFLAGS += -k #Continue after failed targets
MAKEFLAGS += -r #Don't use build in commands
#MAKEFLAGS += -s #Be silent on stdout
MAKEFLAGS += --warn-undefined-variables
MAKEFLAGS += --jobs=$(CPUS)
MAKEFLAGS += --no-print-directory

TARGET := RepGame
TARGET_LOWER := $(shell echo $(TARGET) | tr '[:upper:]' '[:lower:]')

#Default target
all:
	@echo "${TARGET} build complete..."

#Sub makefiles might share variables. Cuda, server and linux do for sure.
include makefiles/common.mk
include makefiles/appimage.mk
include makefiles/install.mk
include makefiles/wasm.mk
include makefiles/linux.mk #cuda.mk can be turned on inside linux.mk
include makefiles/server.mk #Must also include linux.mk
include makefiles/windows.mk
include makefiles/android.mk
include makefiles/docker.mk

out:
	mkdir -p out

clean:
	@echo "rm -fd out" ; rm -fd out || echo Forcing \'rm -rf out\'. An output was not cleaned up ; rm -rf out

.PHONY: all clean vars

deploy:
	@echo "${TARGET} deploy complete..."


nothing:

vars:
	@echo "$(BEFORE_VARS) $(AFTER_VARS)" | xargs -n1 | sort | uniq -u

AFTER_VARS := $(.VARIABLES)

#$(info $$DEPS_SERVER is [${DEPS_SERVER}])
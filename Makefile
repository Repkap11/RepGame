BUILD_DEBUG := 0

ifeq (${BUILD_DEBUG},1)
BEFORE_VARS := $(.VARIABLES)
endif

#This target can be used to depend on the contents of the makefiles

CPUS ?= $(shell nproc || echo 1)

#Current version of Ubuntu running make
UBUNTU_VERSION := $(shell lsb_release -r -s)

#SHELL = sh -xv

# OLD_SHELL := $(SHELL)
# SHELL = $(info [Paul: $@ ($^) ($?)])$(OLD_SHELL)

#MAKEFLAGS += -k #Continue after failed targets
MAKEFLAGS += -r #Don't use build in commands
#MAKEFLAGS += -s #Be silent on stdout
MAKEFLAGS += --warn-undefined-variables
MAKEFLAGS += --jobs=$(CPUS)
MAKEFLAGS += --no-print-directory

TARGET := RepGame
WORLD := World1
TARGET_LOWER := $(shell echo $(TARGET) | tr '[:upper:]' '[:lower:]')

#Default target
all:
	@echo "${TARGET} build complete..."

#Sub makefiles might share variables. Cuda, server and linux do for sure.
include makefiles/common.mk
-include makefiles/appimage.mk
-include makefiles/flatpak.mk
-include makefiles/install.mk
-include makefiles/wasm.mk
-include makefiles/linux.mk #cuda.mk can be turned on inside linux.mk
-include makefiles/server.mk #Must also include linux.mk
-include makefiles/windows.mk
-include makefiles/android.mk
-include makefiles/docker.mk
-include makefiles/imgui.mk
-include makefiles/entt.mk

out:
	mkdir -p $@
	touch $@

clean:
	@echo "rm -fd out" ; rm -fd out || echo Forcing \'rm -rf out\'. An output was not cleaned up ; rm -rf out

.PHONY: all clean vars

deploy:
	@echo "${TARGET} deploy complete..."

nothing:




update-world:
	ls -1 ~/.repgame/$(WORLD)/ | grep "^chunk_" | xargs -n1 bash -c 'printf "\x08\x0\x0\x0" | cat - ~/.repgame/$(WORLD)/$$0 > ~/.repgame/$(WORLD)/new_$$0'
	ls -1 ~/.repgame/$(WORLD)/ | grep "^chunk_" | xargs -n1 bash -c 'mv ~/.repgame/$(WORLD)/new_$$0 ~/.repgame/$(WORLD)/$$0'

ifeq (${BUILD_DEBUG},1)

vars:
	@echo "$(BEFORE_VARS) $(AFTER_VARS)" | xargs -n1 | sort | uniq -u

AFTER_VARS := $(.VARIABLES)
ALL_VARS := $(shell echo "$(BEFORE_VARS) $(AFTER_VARS)" | xargs -n1 | sort | uniq -u | grep -v "^BEFORE_VARS$$")
# $(info Val: [${ALL_VARS}])
$(info Val: [${CFLAGS_LINUX}])
endif


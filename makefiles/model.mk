# Model-to-world voxelizer: builds the venv and generates a flat world
# under ~/.repgame from local .glb file(s) via tools/model_to_world.py.
# CAR_GLB may be a space-separated list — models are placed in a row
# along +x, CAR_GAP blocks apart. See "Flat worlds / model voxelizer"
# in AGENTS.md.

MODEL_VENV := out/model_venv
MODEL_PY := $(MODEL_VENV)/bin/python

# CAR_GLB ?= $(sort $(wildcard $(HOME)/Downloads/cars/*.glb))
# CAR_WORLD ?= ModelCars

CAR_GLB ?= $(HOME)/Downloads/cars/Ferrari_Daytona_SP3.glb
CAR_WORLD ?= ModelFerrari
CAR_COLORS ?= --color Carpaint=\#8E2323
# Object_423/426 are the gas cap and its ring (part names, not materials).
CAR_PREFER ?= --prefer Object_423,Object_426,Metal_Brushed,Ferrari_Logo_Front,Ferrari_Logo_Side,Grill,Carpaint

# CAR_GLB ?= /home/paul/Downloads/2021_alfa_romeo_giulia_gtam.glb
# CAR_WORLD ?= ModelAlfa


# CAR_GLB ?= $(sort $(wildcard $(HOME)/Downloads/dogs/*.glb))
# # CAR_GLB ?= $(HOME)/Downloads/dogs/Go2.glb
# CAR_WORLD ?= ModelDog
# CAR_ROTATE ?= --rotate x-90


# Applies to every model in CAR_GLB — match names vary per file; run
# `make car-mats` to see what's in each.
CAR_COLORS ?=
CAR_PREFER ?=
# e.g. CAR_ROTATE ?= --rotate x-90   or per file: --rotate Go2=x-90
CAR_ROTATE ?=


CAR_WORLD_DIR := $(HOME)/.repgame/$(CAR_WORLD)
CAR_SIZE ?= 1024
CAR_GAP ?= 8

# Args stamp: regenerates the world when CAR_SIZE/CAR_GAP/CAR_COLORS/
# CAR_PREFER/CAR_ROTATE or the model list changes. The file's mtime only
# moves when the content actually differs.
CAR_ARGS := --size $(CAR_SIZE) --gap $(CAR_GAP) $(CAR_COLORS) $(CAR_PREFER) $(CAR_ROTATE)
CAR_ARGS_FILE := out/models/$(CAR_WORLD).args

ifneq ($(filter car car-run car-reset,$(MAKECMDGOALS)),)
$(shell mkdir -p out/models && printf '%s' '$(CAR_ARGS) $(CAR_GLB)' > '$(CAR_ARGS_FILE).new' && { cmp -s '$(CAR_ARGS_FILE).new' '$(CAR_ARGS_FILE)' 2>/dev/null || mv '$(CAR_ARGS_FILE).new' '$(CAR_ARGS_FILE)'; } && rm -f '$(CAR_ARGS_FILE).new')
endif

$(MODEL_PY):
	python3 -m venv $(MODEL_VENV)
	$(MODEL_PY) -m pip install 'trimesh==4.12.2' numpy pillow

$(CAR_GLB):
	@test -f "$@" || (echo "model not found: $@ (override with CAR_GLB=...)" && exit 1)

# world_type is the last file the tool writes; touch it so a failed run
# can't leave a fresh-looking stamp.
$(CAR_WORLD_DIR)/world_type: $(MODEL_PY) $(CAR_GLB) tools/model_to_world.py $(CAR_ARGS_FILE)
	@test -n "$(CAR_WORLD)" || (echo "CAR_WORLD must not be empty" && exit 1)
	@test -n "$(CAR_GLB)" || (echo "CAR_GLB is empty (no models matched)" && exit 1)
	rm -rf "$(CAR_WORLD_DIR)"
	$(MODEL_PY) tools/model_to_world.py $(foreach f,$(CAR_GLB),"$(f)") --world $(CAR_WORLD) $(CAR_ARGS)
	touch $@

car: $(CAR_WORLD_DIR)/world_type

# List the models' materials/nodes (with CAR_COLORS/CAR_PREFER applied so
# bad names warn) — for picking --color/--prefer names. Writes nothing.
car-mats: $(MODEL_PY) $(CAR_GLB)
	$(MODEL_PY) tools/model_to_world.py $(foreach f,$(CAR_GLB),"$(f)") --list $(CAR_COLORS) $(CAR_PREFER)

car-run: car linux
	./$(LINUX_OUT)/release/$(TARGET) $(CAR_WORLD)

# Force a fresh regen — also wipes player.dat and any in-game edits.
clean-car:
	@test -n "$(CAR_WORLD)" || (echo "CAR_WORLD must not be empty" && exit 1)
	rm -rf "$(CAR_WORLD_DIR)"

.PHONY: car car-run car-mats clean-car

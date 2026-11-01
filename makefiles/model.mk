# Model-to-world voxelizer: builds the venv, unpacks the car model, and
# generates a flat world under ~/.repgame via tools/model_to_world.py.
# See "Flat worlds / model voxelizer" in AGENTS.md.

MODEL_VENV := out/model_venv
MODEL_PY := $(MODEL_VENV)/bin/python

CAR_ZIP ?= $(HOME)/Downloads/ferrari-daytona-sp3-wwwvecarzcom/source/Ferrari_Daytona_SP3_Body_Smooth版2_上传100MB.zip
CAR_GLB := out/models/ferrari/fixV.glb
CAR_WORLD ?= ModelFerrari
CAR_COLORS ?= --color Carpaint=\#8E2323
# Object_423/426 are the gas cap and its ring (part names, not materials).
CAR_PREFER ?= --prefer Object_423,Object_426,Metal_Brushed,Ferrari_Logo_Front,Ferrari_Logo_Side,Grill,Carpaint

# CAR_GLB := /home/paul/Downloads/2021_alfa_romeo_giulia_gtam.glb
# CAR_WORLD ?= ModelAlfa

# CAR_GLB := "/home/paul/Downloads/busto-ellie-wiliams-tlou/source/Busto Ellie Wiliams.glb"
# CAR_WORLD ?= ModelEllie


CAR_WORLD_DIR := $(HOME)/.repgame/$(CAR_WORLD)
CAR_SIZE ?= 512

# Args stamp: regenerates the world when CAR_SIZE/CAR_COLORS/CAR_PREFER
# change. The file's mtime only moves when the content actually differs.
CAR_ARGS := --size $(CAR_SIZE) $(CAR_COLORS) $(CAR_PREFER)
CAR_ARGS_FILE := out/models/$(CAR_WORLD).args

ifneq ($(filter car car-run car-reset,$(MAKECMDGOALS)),)
$(shell mkdir -p out/models && printf '%s' '$(CAR_ARGS)' > '$(CAR_ARGS_FILE).new' && { cmp -s '$(CAR_ARGS_FILE).new' '$(CAR_ARGS_FILE)' 2>/dev/null || mv '$(CAR_ARGS_FILE).new' '$(CAR_ARGS_FILE)'; } && rm -f '$(CAR_ARGS_FILE).new')
endif

$(MODEL_PY):
	python3 -m venv $(MODEL_VENV)
	$(MODEL_PY) -m pip install 'trimesh==4.12.2' numpy pillow

$(CAR_GLB):
	@test -f "$(CAR_ZIP)" || (echo "CAR_ZIP not found: $(CAR_ZIP) (override with CAR_ZIP=...)" && exit 1)
	mkdir -p out/models/ferrari
	unzip -o -j "$(CAR_ZIP)" fixV.glb -d out/models/ferrari

# world_type is the last file the tool writes; touch it so a failed run
# can't leave a fresh-looking stamp.
$(CAR_WORLD_DIR)/world_type: $(MODEL_PY) $(CAR_GLB) tools/model_to_world.py $(CAR_ARGS_FILE)
	@test -n "$(CAR_WORLD)" || (echo "CAR_WORLD must not be empty" && exit 1)
	rm -rf "$(CAR_WORLD_DIR)"
	$(MODEL_PY) tools/model_to_world.py $(CAR_GLB) --world $(CAR_WORLD) $(CAR_ARGS)
	touch $@

car: $(CAR_WORLD_DIR)/world_type

car-run: car linux
	./$(LINUX_OUT)/release/$(TARGET) $(CAR_WORLD)

# Force a fresh regen — also wipes player.dat and any in-game edits.
clean-car:
	@test -n "$(CAR_WORLD)" || (echo "CAR_WORLD must not be empty" && exit 1)
	rm -rf "$(CAR_WORLD_DIR)"

.PHONY: car car-run clean-car

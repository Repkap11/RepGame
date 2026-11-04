# Cross-compile the web client with Emscripten. Compile + link is CMake
# (REPGAME_PLATFORM=wasm); this file is the task runner: emsdk install,
# delivery copies, server/deploy.
#
# Vanilla upstream emsdk in an in-repo, git-ignored checkout — `make
# emsdk_build` clones it and installs/activates the toolchain. (The old note
# about swapping in Repkap11/emscripten is stale — the fork work was
# abandoned and the pointer-lock patches it carried are upstream.)
# EM_ROOT ?= leaves room for `make wasm EM_ROOT=/elsewhere/upstream`.
EMSDK_VERSION ?= latest
EM_ROOT ?= $(CURDIR)/emsdk/upstream
EMCMAKE := $(EM_ROOT)/emscripten/emcmake
EMSDK_TOOLCHAIN := $(abspath $(EM_ROOT))/emscripten/cmake/Modules/Platform/Emscripten.cmake

CMAKE_WASM_DFLAGS := -DCMAKE_TOOLCHAIN_FILE=$(EMSDK_TOOLCHAIN) \
	-DREPGAME_AUDIO=$(REPGAME_AUDIO) -DREPGAME_SOUND_OGG_DIR=$(abspath $(SOUND_OGG_DIR)) \
	-DREPGAME_OUTPUT_DIR=$(abspath out/wasm/delivery)
ifeq ($(USE_CCACHE),1)
CMAKE_WASM_DFLAGS += -DCMAKE_CXX_COMPILER_LAUNCHER=ccache
endif

install: emsdk
wasm-install: packages
	sudo npm install -g http-server

all: wasm
wasm: out/wasm/delivery/$(TARGET).js out/wasm/delivery/index.html out/wasm/delivery/reset.css out/wasm/delivery/index.css out/wasm/delivery/index.js out/wasm/delivery/icon.png

# The preload fs is staged by CMake custom commands into out/wasm/cmake/fs
# (preprocessed shaders, WGSL + generated block_ids.inc, bitmaps, fonts,
# sounds) and linked with --preload-file.
out/wasm/delivery/$(TARGET).js: $(SOUND_OGGS) $(EM_ROOT)/emscripten/em++ FORCE
	$(EMCMAKE) cmake --preset wasm -B out/wasm/cmake $(CMAKE_WASM_DFLAGS)
	MAKEFLAGS="$(CMAKE_MAKEFLAGS)" cmake --build out/wasm/cmake

FORCE:

out/wasm/delivery/index.html: src/wasm/index.html | out/wasm
	cp $< $@
out/wasm/delivery/index.js: src/wasm/index.js | out/wasm
	cp $< $@
out/wasm/delivery/%.css: src/wasm/%.css | out/wasm
	cp $< $@

out/wasm/delivery/icon.png : bitmaps/icons/icon.png | out/wasm
	cp $< $@

wasm-start-server:
	-pkill -f "wasm_server.py" 2>/dev/null || true
	-pkill -f "http-server" 2>/dev/null || true
	sleep 0.2
	python3 wasm_server.py out/wasm/delivery 8080 &
	sleep 0.5

WASM_START_COMMAND := google-chrome --app=http://localhost:8080
# WASM_START_COMMAND := firefox http://localhost:8080

wasm-run: wasm
	${WASM_START_COMMAND}

WASM_DEPLOY_REMOTE_PATH := paul@repkap11.com:/home/paul/website/${TARGET_LOWER}

deploy: wasm-deploy

wasm-deploy: wasm
	rsync -rt out/wasm/delivery/ ${WASM_DEPLOY_REMOTE_PATH}

emsdk_build: emsdk
emsdk:
	git clone https://github.com/emscripten-core/emsdk.git emsdk
	emsdk/emsdk install $(EMSDK_VERSION)
	emsdk/emsdk activate $(EMSDK_VERSION)

clean: clean-wasm

clean-wasm:
	rm -rf out/wasm

out/wasm: | out
	mkdir -p out/wasm/delivery

.PHONY: wasm wasm-run clean-wasm wasm-deploy wasm-install emsdk_build emsdk FORCE

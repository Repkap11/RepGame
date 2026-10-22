#WASM
REPGAME_PACKAGES += npm

# USE_SDL is needed at compile time too: modern emscripten ports are no
# longer installed into the shared sysroot, the flag injects the SDL2
# include path. emdawnwebgpu likewise injects <webgpu/webgpu.h> — the Dawn-
# based WebGPU binding for browser GPU mapgen (see src/shaders/map_gen.wgsl
# and src/wasm/webgpu_mapgen.cpp).
CFLAGS_WASM := -DREPGAME_WASM -pthread -s USE_SDL=2 -O3 -DREPGAME_SKIP_CHECK_FOR_GL_ERRORS --use-port=emdawnwebgpu


CFLAGS_LINK_WASM := -O3 \
			-s WASMFS=1 \
			-s USE_SDL=2 \
			--use-port=emdawnwebgpu \
			-s MAX_WEBGL_VERSION=2 \
			-s ASSERTIONS=1 \
			-s EXPORTED_FUNCTIONS='["_main", "_repgame_wasm_start_storage", "_repgame_wasm_request_exit", "_repgame_wasm_set_server", "_repgame_main_probe_get"]' \
			-lwebsocket.js \
			-s EXPORTED_RUNTIME_METHODS='["ccall", "cwrap"]' \
			-pthread \
			-s PTHREAD_POOL_SIZE=navigator.hardwareConcurrency

#Works OK
# INITIAL_MEMORY=3968MB (wasm4gb): the default 2GB cap was hit while
# loading chunks — WebGPU mapgen fills the loader faster so more chunks
# are resident (observed demand ~3.77GB). Preallocate rather than grow:
# with pthreads, ALLOW_MEMORY_GROWTH makes a heap grow sync-park every
# worker on a futex, which deadlocks against a worker blocked in
# pthread_cond_wait (observed as a full-page freeze when the UI allocated
# while a worker waited on a WebGPU job); and the alternative —
# GROWABLE_ARRAYBUFFERS resizable SharedArrayBuffers — is rejected by
# Chrome's WebGL entry points (bufferSubData et al. refuse
# resizable-buffer views). A fixed SharedArrayBuffer commits lazily and
# sidesteps both. Stay under 4GB: exactly 4GB wraps wasm32 heap accounting
# (arena computes to ~0 -> instant OOM). >2GB needs wasm4gb, supported by
# modern Chrome/Safari and Firefox 154+.
CFLAGS_LINK_WASM += -s INITIAL_MEMORY=3968MB

# Default STACK_SIZE is 64KB. emdawnwebgpu delivers JS error/diagnostic
# strings to wasm callbacks via stringToUTF8OnStack — a large Dawn message
# (validation errors embed indented context) overflows a small stack and
# shows up as a stack-cookie smash. 8MB covers arbitrarily large messages.
CFLAGS_LINK_WASM += -s STACK_SIZE=8MB

# Vanilla upstream emsdk:
#   git clone https://github.com/emscripten-core/emsdk.git
#   ./emsdk install latest && ./emsdk activate latest
# (The old note about swapping in Repkap11/emscripten is stale — the fork
# work was abandoned and the pointer-lock patches it carried are upstream.)
EM_ROOT := /home/paul/Software/Rep/emsdk/upstream/
CC_WASM := ${EM_ROOT}/emscripten/em++

ifeq ($(USE_CCACHE),1)
CC_WASM := ccache $(CC_WASM)
endif

WASM_SHADERS = $(patsubst src/shaders/%.glsl,out/wasm/fs/src/shaders/%.glsl,$(wildcard src/shaders/*.glsl))
# WGSL compute sources live in src/shaders/*.wgsl (same preprocessor pattern
# as .glsl — the sources are dialect templates, not literal WGSL). They need
# the generated block_ids.inc prelude, which keeps block_definitions.hpp's
# enum as the single source of truth.
WASM_WGSL_SHADERS = $(patsubst src/shaders/%.wgsl,out/wasm/fs/src/shaders/%.wgsl,$(wildcard src/shaders/*.wgsl))
WASM_BITMAPS = $(patsubst bitmaps/%.bmp,out/wasm/fs/bitmaps/%.bin,$(wildcard bitmaps/*.bmp))
WASM_FONTS = $(patsubst fonts/%.ttf,out/wasm/fs/fonts/%.ttf,$(wildcard fonts/*.ttf))

OBJECTS_COMMON_WASM := $(patsubst src/common/%.cpp,out/wasm/common/%.o, $(SRC_COMMON))
OBJECTS_WASM := $(patsubst src/%.cpp,out/wasm/%.o, $(wildcard src/wasm/*.cpp))
# Same imgui set as linux's SRC_IMGUI, but with the SDL2 backend: WASM links
# SDL2 (emscripten -s USE_SDL=2), not SDL3.
SRC_IMGUI_WASM := imgui_build/backends/imgui_impl_sdl2.cpp $(wildcard imgui_build/imgui*.cpp) imgui_build/backends/imgui_impl_opengl3.cpp
OBJECTS_IMGUI_WASM := $(patsubst imgui_build/%.cpp,out/wasm/imgui/%.o, $(SRC_IMGUI_WASM))

wasm-install: packages
	sudo npm install -g http-server

all: wasm
wasm: out/wasm/delivery/$(TARGET).js out/wasm/delivery/index.html out/wasm/delivery/reset.css out/wasm/delivery/index.css out/wasm/delivery/index.js out/wasm/delivery/icon.png

WASM_DIRS = $(patsubst src%,out/wasm%,$(shell find src -type d)) \
			out/wasm \
			out/wasm/fs \
			out/wasm/fs/src \
			out/wasm/fs/src/shaders \
			out/wasm/gen \
			out/wasm/fs/bitmaps \
			out/wasm/fs/fonts \
			out/wasm/delivery \
			out/wasm/imgui \
			out/wasm/imgui/backends

out/wasm/%.o: src/%.cpp $(HEADERS) src/wasm/RepGameSDL2.cpp | out/wasm
	$(CC_WASM) $(INCLUDES_COMMON) $(CFLAGS_WASM) -c $< -o $@

# imgui_impl_opengl3 auto-detects Emscripten as GLES2/WebGL1, but the game
# creates a WebGL2/GLES3 context (SDL_GL_CONTEXT_MAJOR_VERSION=3,
# MAX_WEBGL_VERSION=2), so force the ES3 path ("#version 300 es").
out/wasm/imgui/%.o: imgui_build/%.cpp | out/wasm
	@mkdir -p $(dir $@)
	$(CC_WASM) $(INCLUDES_COMMON) $(CFLAGS_WASM) -DIMGUI_IMPL_OPENGL_ES3 -c $< -o $@

out/wasm/delivery/$(TARGET).js: $(OBJECTS_COMMON_WASM) $(OBJECTS_IMGUI_WASM) $(OBJECTS_WASM) $(WASM_SHADERS) $(WASM_WGSL_SHADERS) $(WASM_BITMAPS) $(WASM_FONTS) | out/wasm
	$(CC_WASM) $(CFLAGS_LINK_WASM) $(OBJECTS_WASM) $(OBJECTS_IMGUI_WASM) $(OBJECTS_COMMON_WASM) --preload-file out/wasm/fs@ -o $@

out/wasm/delivery/index.html: src/wasm/index.html | out/wasm
	cp $< $@

out/wasm/delivery/index.js: src/wasm/index.js | out/wasm
	cp $< $@

out/wasm/delivery/%.css: src/wasm/%.css | out/wasm
	cp $< $@

out/wasm/fs/src/shaders/%.glsl: src/shaders/%.glsl include/common/chunk_dimensions.hpp makefiles/common.mk makefiles/wasm.mk | out/wasm
	$(SHADER_PP) -DREPGAME_WASM $< -o $@

# WGSL: same preprocessor trick as .glsl, plus -DREPGAME_WGSL_GEN which flips
# the dialect macros in wgsl_dialect.hpp to emit WGSL. -I out/wasm/gen finds
# the generated block_ids.inc. Headers the dialect sources #include are listed
# so logic edits rebuild the shader.
out/wasm/gen/block_ids.inc: include/common/block_definitions.hpp tools/gen_wgsl_block_ids.awk | out/wasm
	mkdir -p $(dir $@)
	awk -f tools/gen_wgsl_block_ids.awk $< > $@

out/wasm/fs/src/shaders/%.wgsl: src/shaders/%.wgsl out/wasm/gen/block_ids.inc include/common/wgsl_dialect.hpp include/common/perlin_noise_shared.hpp include/common/map_gen_fields.hpp include/common/map_gen_fields_legacy.hpp include/common/map_gen_consts.hpp include/common/map_logic.hpp include/common/chunk_dimensions.hpp | out/wasm
	$(SHADER_PP) -I out/wasm/gen -DREPGAME_WGSL_GEN $< -o $@

out/wasm/fs/bitmaps/% : out/bitmaps/% | out/wasm
	cp $< $@

out/wasm/fs/fonts/% : fonts/% | out/wasm
	cp $< $@

out/wasm/delivery/icon.png : bitmaps/icon.png | out/wasm
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
	rsync -r out/wasm/delivery/ ${WASM_DEPLOY_REMOTE_PATH}

clean: clean-wasm

clean-wasm:
	rm -rf $(WASM_DIRS)

out/wasm: | out
	mkdir -p $(WASM_DIRS)
	touch $@

.PRECIOUS: $(TARGET) $(OBJECTS_WASM) $(OBJECTS_COMMON_WASM) $(OBJECTS_IMGUI_WASM)

.PHONY: wasm wasm-run clean-wasm wasm-deploy wasm-install

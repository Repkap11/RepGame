#WASM
MAKEFILES += wasm.mk
#
CFLAGS_WASM := -DREPGAME_WASM \
			-s USE_WEBGL2=1 \
			--no-heap-copy \
			-s EXPORTED_FUNCTIONS='["_main", "_testJS"]' \
			-s EXTRA_EXPORTED_RUNTIME_METHODS='["ccall", "cwrap"]'

#CFLAGS_WASM += -s USE_PTHREADS=1 -s TOTAL_MEMORY=512MB
CFLAGS_WASM += -s ALLOW_MEMORY_GROWTH=1

EM_ROOT := $(shell cat  ~/.emscripten | grep BINARYEN_ROOT | sed -e "s/.* = \(.*\)/\1/" )

CC_WASM := ${EM_ROOT}/emscripten/em++
ifeq ($(USE_CCACHE),1)
CC_WASM := ccache $(CC_WASM)
endif

WASM_SHADERS = $(patsubst src/shaders/%.glsl,out/wasm/fs/src/shaders/%.glsl,$(wildcard src/shaders/*.glsl))
WASM_BITMAPS = $(patsubst bitmaps/%.bmp,out/wasm/fs/bitmaps/%.bin,$(wildcard bitmaps/*.bmp))

OBJECTS_COMMON_WASM := $(patsubst src/common/%.cpp,out/wasm/common/%.bc, $(SRC_COMMON))
OBJECTS_WASM := $(patsubst src/%.cpp,out/wasm/%.bc, $(wildcard src/wasm/*.cpp))

wasm: out/wasm/delivery/$(TARGET).js out/wasm/delivery/index.html out/wasm/delivery/reset.css out/wasm/delivery/index.css out/wasm/delivery/index.js out/wasm/delivery/icon.png

WASM_DIRS = $(patsubst src%,out/wasm%,$(shell find src -type d)) \
			out/wasm \
			out/wasm/fs \
			out/wasm/fs/src \
			out/wasm/fs/src/shaders \
			out/wasm/fs/bitmaps \
			out/wasm/delivery

out/wasm/%.bc: src/%.cpp $(MAKEFILES) $(HEADERS) | out/wasm
	$(CC_WASM) $(INCLUDES_COMMON) $(CFLAGS_WASM) -c $< -o $@

out/wasm/delivery/$(TARGET).js: $(OBJECTS_COMMON_WASM) $(OBJECTS_WASM) $(WASM_SHADERS) $(WASM_BITMAPS) $(MAKEFILES) | out/wasm
	$(CC_WASM) -flto $(CFLAGS_WASM) $(OBJECTS_WASM) $(OBJECTS_COMMON_WASM) --preload-file out/wasm/fs@ -o $@

out/wasm/delivery/index.html: src/wasm/index.html | out/wasm
	cp $< $@

out/wasm/delivery/index.js: src/wasm/index.js | out/wasm
	cp $< $@

out/wasm/delivery/%.css: src/wasm/%.css | out/wasm
	cp $< $@

out/wasm/fs/src/shaders/%.glsl: src/shaders/%.glsl $(MAKEFILES) | out/wasm
	cp $< $@

out/wasm/fs/bitmaps/% : out/bitmaps/% $(MAKEFILES) | out/wasm
	cp $< $@

out/wasm/delivery/icon.png : bitmaps/icon.png | out/wasm
	cp $< $@

wasm-start-server:
	http-server out/wasm/delivery -c-1 &
	sleep 0.5

WASM_START_COMMAND := google-chrome --app=http://localhost:8080 --start-fullscreen

wasm-run: wasm
	${WASM_START_COMMAND}

WASM_DEPLOY_REMOTE_PATH := paul@repkap11.com:/home/paul/website/${TARGET_LOWER}

wasm-deploy: wasm
	rsync -r out/wasm/delivery/ ${WASM_DEPLOY_REMOTE_PATH}

clean-wasm:
	rm -rf $(WASM_DIRS)

out/wasm: | out
	mkdir -p $(WASM_DIRS)

.PRECIOUS: $(TARGET) $(OBJECTS_WASM) $(OBJECTS_COMMON_WASM) $(LIB_TARGET_WASM)

.PHONY: wasm wasm-run clean-wasm wasm-deploy

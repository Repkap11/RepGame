#WASM
REP_MAKEFILES += makefiles/wasm.mk

REPGAME_PACKAGES += npm

CFLAGS_WASM := -DREPGAME_WASM \
			-s USE_WEBGL2=1 \
			--no-heap-copy \
			-std=c++11 \
			-lidbfs.js \
			-s EXPORTED_FUNCTIONS='["_main", "_testJS"]' \
			-s EXTRA_EXPORTED_RUNTIME_METHODS='["ccall", "cwrap"]'


#Works OK
CFLAGS_WASM += -s ALLOW_MEMORY_GROWTH=1

#Locks up GPU
#CFLAGS_WASM += -s USE_PTHREADS=1 -s TOTAL_MEMORY=512MB

#Basically not supported
#CFLAGS_WASM += -s USE_PTHREADS=1 -s ALLOW_MEMORY_GROWTH=1 -s WASM_MEM_MAX=1024MB

# Clone emsdk from
#git clone https://github.com/emscripten-core/emsdk.git
#./emsdk install latest
#./emsdk activate latest
# Replace emsdk/upstream/emscripten with git clone git@github.com:Repkap11/emscripten.git
EM_ROOT := $(shell if [ -f ~/.emscripten ]; then cat  ~/.emscripten | grep BINARYEN_ROOT | sed -e "s/.* = \(.*\)/\1/" ; fi )
CC_WASM := ${EM_ROOT}/emscripten/em++

ifeq ($(USE_CCACHE),1)
CC_WASM := ccache $(CC_WASM)
endif

WASM_SHADERS = $(patsubst src/shaders/%.glsl,out/wasm/fs/src/shaders/%.glsl,$(wildcard src/shaders/*.glsl))
WASM_BITMAPS = $(patsubst bitmaps/%.bmp,out/wasm/fs/bitmaps/%.bin,$(wildcard bitmaps/*.bmp))

OBJECTS_COMMON_WASM := $(patsubst src/common/%.cpp,out/wasm/common/%.bc, $(SRC_COMMON))
OBJECTS_WASM := $(patsubst src/%.cpp,out/wasm/%.bc, $(wildcard src/wasm/*.cpp))

wasm-install: packages
	sudo npm install -g http-server

all: wasm
wasm: out/wasm/delivery/$(TARGET).js out/wasm/delivery/index.html out/wasm/delivery/reset.css out/wasm/delivery/index.css out/wasm/delivery/index.js out/wasm/delivery/icon.png

WASM_DIRS = $(patsubst src%,out/wasm%,$(shell find src -type d)) \
			out/wasm \
			out/wasm/fs \
			out/wasm/fs/src \
			out/wasm/fs/src/shaders \
			out/wasm/fs/bitmaps \
			out/wasm/delivery

out/wasm/%.bc: src/%.cpp $(REP_MAKEFILES) $(HEADERS) | out/wasm
	$(CC_WASM) $(INCLUDES_COMMON) $(CFLAGS_WASM) -c $< -o $@

out/wasm/delivery/$(TARGET).js: $(OBJECTS_COMMON_WASM) $(OBJECTS_WASM) $(WASM_SHADERS) $(WASM_BITMAPS) $(REP_MAKEFILES) | out/wasm
	$(CC_WASM) -flto $(CFLAGS_WASM) $(OBJECTS_WASM) $(OBJECTS_COMMON_WASM) --preload-file out/wasm/fs@ -o $@

out/wasm/delivery/index.html: src/wasm/index.html | out/wasm
	cp $< $@

out/wasm/delivery/index.js: src/wasm/index.js | out/wasm
	cp $< $@

out/wasm/delivery/%.css: src/wasm/%.css | out/wasm
	cp $< $@

out/wasm/fs/src/shaders/%.glsl: src/shaders/%.glsl $(REP_MAKEFILES) | out/wasm
	cp $< $@

out/wasm/fs/bitmaps/% : out/bitmaps/% $(REP_MAKEFILES) | out/wasm
	cp $< $@

out/wasm/delivery/icon.png : bitmaps/icon.png | out/wasm
	cp $< $@

wasm-start-server:
	http-server out/wasm/delivery -c-1 &
	sleep 0.5

WASM_START_COMMAND := google-chrome --app=http://localhost:8080 --start-fullscreen
#WASM_START_COMMAND := firefox http://localhost:8080

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

.PRECIOUS: $(TARGET) $(OBJECTS_WASM) $(OBJECTS_COMMON_WASM)

.PHONY: wasm wasm-run clean-wasm wasm-deploy wasm-install

#WASM
MAKEFILES += wasm.mk
#
#-s USE_PTHREADS=1 -s TOTAL_MEMORY=512MB
CFLAGS_WASM := -s ALLOW_MEMORY_GROWTH=1 -DREPGAME_WASM -s USE_WEBGL2=1

CC_WASM := ~/Software/emsdk/emsdk/emscripten/1.38.25/em++

WASM_SHADERS = $(patsubst src/shaders/%.glsl,out/wasm/fs/src/shaders/%.glsl,$(wildcard src/shaders/*.glsl))
WASM_BITMAPS = $(patsubst android/app/src/main/res/raw/%,out/wasm/fs/bitmaps/%,$(wildcard android/app/src/main/res/raw/*))

WASM_FILE_TYPE = js

OBJECTS_COMMON_WASM := $(patsubst src/common/%.cpp,out/wasm/common/%.bc, $(SRC_COMMON))
OBJECTS_WASM := $(patsubst src/%.cpp,out/wasm/%.bc, $(wildcard src/wasm/*.cpp))

wasm: out/wasm/$(TARGET).$(WASM_FILE_TYPE) out/wasm/index.html out/wasm/reset.css

WASM_DIRS = $(patsubst src%,out/wasm%,$(shell find src -type d)) \
			out/wasm/fs \
			out/wasm/fs/src \
			out/wasm/fs/src/shaders \
			out/wasm/fs/bitmaps

out/wasm/%.bc: src/%.cpp $(MAKEFILES) | out/wasm
	$(CC_WASM) $(INCLUDES_COMMON) $(CFLAGS_WASM) -c $< -o $@

out/wasm/$(TARGET).$(WASM_FILE_TYPE): $(OBJECTS_COMMON_WASM) $(OBJECTS_WASM) $(WASM_SHADERS) $(WASM_BITMAPS) $(MAKEFILES)
	$(CC_WASM) -flto $(CFLAGS_WASM) $(OBJECTS_WASM) $(OBJECTS_COMMON_WASM) --preload-file out/wasm/fs@ -o $@

out/wasm/index.html: src/wasm/index.html | out/wasm
	cp $< $@

out/wasm/reset.css: src/wasm/reset.css | out/wasm
	cp $< $@

out/wasm/fs/src/shaders/%.glsl: src/shaders/%.glsl $(MAKEFILES) | out/wasm
	cp $< $@

out/wasm/fs/bitmaps/% : android/app/src/main/res/raw/% $(MAKEFILES) | out/wasm
	cp $< $@

wasm-start-server:
	http-server out/wasm/ &

wasm-run: wasm
	#google-chrome --new-window http://localhost:8080/Repgame.html &
	google-chrome --new-window http://localhost:8080/index.html &


WASM_DEPLOY_REMOTE_PATH := "paul@repkap11.com:/home/paul/website/repgame"

wasm-deploy: wasm
	rsync --update out/wasm/index.html ${WASM_DEPLOY_REMOTE_PATH}/index.html
	rsync --update out/wasm/reset.css ${WASM_DEPLOY_REMOTE_PATH}/reset.css
	rsync --update out/wasm/RepGame.data ${WASM_DEPLOY_REMOTE_PATH}/RepGame.data
	rsync --update out/wasm/RepGame.js ${WASM_DEPLOY_REMOTE_PATH}/RepGame.js
	rsync --update out/wasm/RepGame.wasm ${WASM_DEPLOY_REMOTE_PATH}/RepGame.wasm

clean-wasm:
	rm -rf $(WASM_DIRS)

out/wasm: out
	mkdir -p $(WASM_DIRS)

.PRECIOUS: $(TARGET) $(OBJECTS_WASM) $(OBJECTS_COMMON_WASM) $(LIB_TARGET_WASM)

.PHONY: wasm wasm-run clean-wasm

#WASM
MAKEFILES += wasm.mk
#
#-s USE_PTHREADS=1 -s TOTAL_MEMORY=512MB
CFLAGS_WASM := $(CFLAGS) -s ALLOW_MEMORY_GROWTH=1 -DREPGAME_WASM -s FULL_ES3=1 -s USE_WEBGL2=1

CC_WASM := ~/Software/emsdk/emsdk/emscripten/1.38.25/em++

WASM_SHADERS = $(patsubst src/shaders/%.glsl,out/wasm/fs/src/shaders/%.glsl,$(wildcard src/shaders/*.glsl))
WASM_BITMAPS = $(patsubst android/app/src/main/res/raw/%,out/wasm/fs/bitmaps/%,$(wildcard android/app/src/main/res/raw/*))

WASM_FILE_TYPE = js

OBJECTS_COMMON_WASM := $(patsubst src/common/%.cpp,out/wasm/common/%.bc, $(SRC_COMMON))
OBJECTS_WASM := $(patsubst src/%.cpp,out/wasm/%.bc, $(wildcard src/wasm/*.cpp))

wasm: out/wasm/$(TARGET).$(WASM_FILE_TYPE) out/wasm/index.html

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

out/wasm/fs/src/shaders/%.glsl: src/shaders/%.glsl $(MAKEFILES) | out/wasm
	cp $< $@

out/wasm/fs/bitmaps/% : android/app/src/main/res/raw/% $(MAKEFILES) | out/wasm
	cp $< $@

wasm-start-server:
	http-server &

wasm-run: wasm
	#google-chrome --new-window http://localhost:8080/out/wasm/Repgame.html &
	google-chrome --new-window http://localhost:8080/out/wasm/index.html &


clean-wasm:
	rm -rf $(WASM_DIRS)

out/wasm: out
	mkdir -p $(WASM_DIRS)

.PRECIOUS: $(TARGET) $(OBJECTS_WASM) $(OBJECTS_COMMON_WASM) $(LIB_TARGET_WASM)

.PHONY: wasm wasm-run clean-wasm
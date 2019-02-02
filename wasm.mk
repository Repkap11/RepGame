#WASM
MAKEFILES += wasm.mk
CFLAGS_WASM := $(CFLAGS) -DREPGAME_WASM -s ALLOW_MEMORY_GROWTH=1 -s FULL_ES2=1 -s FULL_ES3=1 -s NO_EXIT_RUNTIME=1

CC_WASM := ~/Software/emsdk/emsdk/emscripten/1.38.25/em++

OBJECTS_COMMON_WASM := $(patsubst src/common/%.cpp,out/wasm/common/%.bc, $(SRC_COMMON))
OBJECTS_WASM := $(patsubst src/%.cpp,out/wasm/%.bc, $(wildcard src/wasm/*.cpp))

wasm: out/wasm/$(TARGET).js out/wasm/index.html

WASM_DIRS = $(patsubst src%,out/wasm%,$(shell find src -type d))

out/wasm/%.bc: src/%.cpp $(MAKEFILES) | out/wasm
	$(CC_WASM) $(INCLUDES_COMMON) $(CFLAGS_WASM) -c $< -o $@

out/wasm/$(TARGET).js: $(OBJECTS_COMMON_WASM) $(OBJECTS_WASM) $(MAKEFILES)
	$(CC_WASM) -flto $(CFLAGS_WASM) $(OBJECTS_WASM) $(OBJECTS_COMMON_WASM) --preload-file src/shaders -o $@ 

out/wasm/index.html: src/wasm/index.html | out/wasm
	cp $< $@

wasm-start-server:
	http-server &

wasm-run: wasm
	google-chrome --new-window http://localhost:8080/out/wasm/index.html


clean-wasm:
	rm -rf $(WASM_DIRS)

out/wasm: out
	mkdir -p $(WASM_DIRS)

.PRECIOUS: $(TARGET) $(OBJECTS_WASM) $(OBJECTS_COMMON_WASM) $(LIB_TARGET_WASM)

.PHONY: wasm wasm-run clean-wasm

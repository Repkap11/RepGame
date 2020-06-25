#Shared between platforms
REP_MAKEFILES += makefiles/common.mk

USE_CCACHE ?= 1

SRC_COMMON := $(wildcard src/common/*.cpp) $(wildcard src/common/**/*.cpp)
SRC_ALL_C := $(wildcard src/**/*.cpp) $(wildcard src/**/**/*.cpp) $(wildcard src/**/*.cu) $(wildcard src/**/**/*.cu)
INCLUDES_COMMON := -I include/ -I /usr/include/glm -I /usr/include/SDL2
HEADERS := $(wildcard include/**/*.hpp)
BITMAPS_NO_HEADER := $(patsubst bitmaps/%.bmp,out/bitmaps/%.bin,$(wildcard bitmaps/*.bmp))

out/bitmaps/%.bin : bitmaps/%.bmp $(REP_MAKEFILES) | out/bitmaps
	tail -c +139 $< > $@

out/bitmaps: | out
	mkdir -p out/bitmaps

clean: clean-bitmaps
clean-bitmaps:
	rm -rf out/bitmaps

check:
	cppcheck -j$(CPUS) --quiet --enable=warning,style,performance,portability,information,missingInclude -Iinclude -DREPGAME_LINUX src
	cppcheck -j$(CPUS) --quiet --enable=warning,style,performance,portability,information,missingInclude -Iinclude -DREPGAME_WINDOWS src
	cppcheck -j$(CPUS) --quiet --enable=warning,style,performance,portability,information,missingInclude -Iinclude -DREPGAME_ANDROID src
	cppcheck -j$(CPUS) --quiet --enable=warning,style,performance,portability,information,missingInclude -Iinclude -DREPGAME_WASM src
	~/.local/bin/cpplint --filter=-whitespace,-legal/copyright,-readability/todo --quiet $(SRC_ALL_C) 

.PHONY: clean-bitmaps check

.PRECIOUS: $(BITMAPS_NO_HEADER)
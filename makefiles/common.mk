#Shared between platforms
REP_MAKEFILES += makefiles/common.mk

USE_CCACHE := 1

SRC_COMMON := $(wildcard src/common/*.cpp) $(wildcard src/common/**/*.cpp)
INCLUDES_COMMON := -I include/ -I /usr/include/glm
HEADERS := $(wildcard include/**/*.hpp)
BITMAPS_NO_HEADER := $(patsubst bitmaps/%.bmp,out/bitmaps/%.bin,$(wildcard bitmaps/*.bmp))

out/bitmaps/%.bin : bitmaps/%.bmp $(REP_MAKEFILES) | out/bitmaps
	tail -c +139 $< > $@

out/bitmaps: | out
	mkdir -p out/bitmaps

clean-bitmaps:
	rm -rf out/bitmaps

.PHONY: clean-bitmaps

.PRECIOUS: $(BITMAPS_NO_HEADER)
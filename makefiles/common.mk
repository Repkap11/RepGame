#Shared between platforms

USE_CCACHE ?= 1

SRC_COMMON := $(wildcard src/common/*.cpp) $(wildcard src/common/**/*.cpp)
SRC_ALL_C := $(wildcard src/**/*.cpp) $(wildcard src/**/**/*.cpp) $(wildcard src/**/*.cu) $(wildcard src/**/**/*.cu)
INCLUDES_COMMON := -I include/ -I include/glm -I /usr/include/SDL2
HEADERS := $(wildcard include/**/*.hpp)
BITMAPS_NO_HEADER := $(patsubst bitmaps/%.bmp,out/bitmaps/%.bin,$(wildcard bitmaps/*.bmp))

out/bitmaps/%.bin : bitmaps/%.bmp | out/bitmaps
	tail -c +139 $< > $@

out/bitmaps: | out
	mkdir -p out/bitmaps
	touch $@

clean: clean-bitmaps
clean-bitmaps:
	rm -rf out/bitmaps

check:
	cppcheck -j$(CPUS) --quiet --enable=warning,style,performance,portability,information,missingInclude -iinclude/common/vendor -DREPGAME_LINUX src
# cppcheck -j$(CPUS) --quiet --enable=warning,style,performance,portability,information,missingInclude -Iinclude -iinclude/common/vendor -DREPGAME_WINDOWS src
# cppcheck -j$(CPUS) --quiet --enable=warning,style,performance,portability,information,missingInclude -Iinclude -iinclude/common/vendor -DREPGAME_ANDROID src
# cppcheck -j$(CPUS) --quiet --enable=warning,style,performance,portability,information,missingInclude -Iinclude -iinclude/common/vendor -DREPGAME_WASM src
# ~/.local/bin/cpplint --filter=-whitespace,-legal/copyright,-readability/todo --quiet $(SRC_ALL_C)

GUARD = $(foreach VAR,$(1),out/vars/$(VAR).$(shell echo $($(VAR)) | md5sum | cut -d ' ' -f 1))
CHECK = @$(foreach VAR,$(1),test -f out/vars/$(VAR).$(shell echo $($(VAR)) | md5sum | cut -d ' ' -f 1) || { echo Variable:"$(VAR)" changed after referenced; exit 1; } ;)

out/vars/%: | out/vars
	@echo Changed var:$(notdir $(basename $@)):$($(notdir $(basename $@)))
	rm -rf $(basename $@).*
	touch $@

out/vars: | out
	mkdir -p $@
	touch $@

clean: clean-vars
clean-vars:
	rm -rf out/vars

.PHONY: clean-bitmaps check clean-vars test-vars

.PRECIOUS: $(BITMAPS_NO_HEADER)
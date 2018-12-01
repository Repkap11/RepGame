#Linux x86_64 builds
MAKEFILES += linux.mk

CFLAGS_LINUX := $(CFLAGS) -march=native -DREPGAME_LINUX -flto -no-pie
LIBS_LINUX := -L/usr/local/cuda-9.2/lib64 -l m -l GL -l GLU -l GLEW -l glut -pthread

CC_LINUX := g++
LD_LINUX := ld -r

OBJECTS_COMMON_LINUX := $(patsubst src/common/%.cpp,out/linux/common/%.so, $(SRC_COMMON))
OBJECTS_LINUX := $(patsubst src/%.cpp,out/linux/%.so, $(wildcard src/linux/*.cpp))

linux: $(TARGET)

DIRS = out \
	   $(patsubst src%,out/linux%,$(shell find src -type d)) \
       $(patsubst src%,out/server%,$(shell find src -type d)) \
	   android/app/src/main/assets/shaders

out/linux/%.so: src/%.cpp $(HEADERS) $(MAKEFILES) out
	$(CC_LINUX) $(INCLUDES_COMMON) $(CFLAGS_LINUX) -c $< -o $@

# Server targets might depend on cuda.mk modifications
include cuda.mk

$(TARGET): $(OBJECTS_COMMON_LINUX) $(OBJECTS_LINUX) $(MAKEFILES) out
	$(CC_LINUX) -flto $(CFLAGS_LINUX) $(OBJECTS_LINUX) $(OBJECTS_COMMON_LINUX) $(LIBS_LINUX) -o $@

include server.mk

linux-run: linux
	./$(TARGET)

map:
	rm -rf World1

reverse = $(if $(1),$(call reverse,$(wordlist 2,$(words $(1)),$(1)))) $(firstword $(1))

clean-linux: clean-server clean-cuda map
	rm -f $(OBJECTS_COMMON_LINUX)
	rm -f $(OBJECTS_LINUX)
	rm -f $(TARGET)
	rm -fd $(call reverse,$(DIRS))

out:
	mkdir $(DIRS)

.PRECIOUS: $(TARGET) $(OBJECTS_LINUX) $(OBJECTS_COMMON_LINUX) $(LIB_TARGET_LINUX)

.PHONY: linux linux-run clean-linux map

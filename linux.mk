#Linux x86_64 builds
MAKEFILES += linux.mk

CFLAGS_CORE := -Wall -Werror -std=c++98 -Wno-unused-variable -no-pie -fno-pie -march=native
#CFLAGS_CORE += -O3
CFLAGS_CORE += -g
CFLAGS_LINUX := $(CFLAGS_CORE) -DREPGAME_LINUX
LIBS_LINUX := -L/usr/local/cuda-9.2/lib64 -l m -l GL -l GLU -l GLEW -l glut -pthread

CC_LINUX := g++
LD_LINUX := ld -r

OBJECTS_COMMON_LINUX := $(patsubst src/common/%.cpp,out/linux/common/%.o, $(SRC_COMMON))
OBJECTS_LINUX := $(patsubst src/%.cpp,out/linux/%.o, $(wildcard src/linux/*.cpp))
DEPS_LINUX := $(patsubst src/%.cpp,out/linux/%.d, $(wildcard src/linux/*.cpp)) \
			$(patsubst src/common/%.cpp,out/linux/common/%.d, $(SRC_COMMON))

linux: $(TARGET)

LINUX_DIRS = $(patsubst src%,out/linux%,$(shell find src -type d)) \
       $(patsubst src%,out/server%,$(shell find src -type d)) \
	   android/app/src/main/assets/shaders

out/linux/%.o: src/%.cpp $(MAKEFILES) | out/linux
	@#Use g++ to build o file and a dependecy tree .d file for every cpp file
	$(CC_LINUX) $(INCLUDES_COMMON) $(CFLAGS_LINUX) -MMD -MP -MF $(patsubst %.o,%.d,$@) -MT $(patsubst %.d,%.o,$@) -c $< -o $@

#Include these .d files, so the dependicies are known for secondary builds.
-include $(DEPS_LINUX)

# Server targets might depend on cuda.mk modifications
include cuda.mk


$(TARGET): $(OBJECTS_COMMON_LINUX) $(OBJECTS_LINUX) $(MAKEFILES)
	$(CC_LINUX) -flto $(CFLAGS_LINUX) $(OBJECTS_LINUX) $(OBJECTS_COMMON_LINUX) $(LIBS_LINUX) -o $@

include server.mk

linux-run: linux
	./$(TARGET)

map:
	rm -rf World1

reverse = $(if $(1),$(call reverse,$(wordlist 2,$(words $(1)),$(1)))) $(firstword $(1))

clean-linux: clean-cuda clean-server
	rm -rf out/linux

out/linux: | out
	echo Making linux $(LINUX_DIRS)
	mkdir -p $(LINUX_DIRS)

.PRECIOUS: $(TARGET) $(OBJECTS_LINUX) $(OBJECTS_COMMON_LINUX) $(LIB_TARGET_LINUX)

.PHONY: linux linux-run clean-linux map

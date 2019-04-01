#Windows x86_64 builds
MAKEFILES += windows.mk

CFLAGS_WINDOWS := -Wall -Werror -std=c++98 -Wno-unused-variable -fno-pie

#CFLAGS_WINDOWS += -O3 -DREPGAME_FAST
CFLAGS_WINDOWS += -g

CFLAGS_WINDOWS += -DREPGAME_WINDOWS
#LIBS_WINDOWS := -l m -l GL -l GLU -l GLEW -l glut -pthread
LIBS_WINDOWS := -lopengl32 -lglu32 -L windows_build/freeglut/lib/x64 -lfreeglut -L windows_build/glew/lib -l glew32 -Wl,-Bstatic -lpthread -Wl,-Bdynamic -static-libgcc -static-libstdc++
INCLUDES_WINDOWS := -I windows_build/freeglut/include -I windows_build/glew/include

CC_WINDOWS := x86_64-w64-mingw32-g++
#CC_WINDOWS := clang++

ifeq ($(CC_WINDOWS),x86_64-w64-mingw32-g++)
CFLAGS_WINDOWS += -no-pie
endif

LD_WINDOWS := ld -r

OBJECTS_COMMON_WINDOWS := $(patsubst src/common/%.cpp,out/windows/common/%.o, $(SRC_COMMON))
OBJECTS_WINDOWS := $(patsubst src/%.cpp,out/windows/%.o, $(wildcard src/windows/*.cpp))
DEPS_WINDOWS := $(patsubst src/%.cpp,out/windows/%.d, $(wildcard src/windows/*.cpp)) \
			$(patsubst src/common/%.cpp,out/windows/common/%.d, $(SRC_COMMON))

windows: $(TARGET).exe

WINDOWS_DIRS = $(patsubst src%,out/windows%,$(shell find src -type d)) \
	   android/app/src/main/assets/shaders

out/windows/%.o: src/%.cpp $(MAKEFILES) | out/windows
	@#Use g++ to build o file and a dependecy tree .d file for every cpp file
	$(CC_WINDOWS) $(INCLUDES_WINDOWS) $(INCLUDES_COMMON) $(CFLAGS_WINDOWS) -MMD -MP -MF $(patsubst %.o,%.d,$@) -MT $(patsubst %.d,%.o,$@) -c $< -o $@

#Include these .d files, so the dependicies are known for secondary builds.
-include $(DEPS_WINDOWS)

$(TARGET).exe: $(OBJECTS_COMMON_WINDOWS) $(OBJECTS_WINDOWS) $(MAKEFILES)
	$(CC_WINDOWS) -flto $(CFLAGS_WINDOWS) $(OBJECTS_WINDOWS) $(OBJECTS_COMMON_WINDOWS) $(LIBS_WINDOWS) -o $@

windows-run: windows
	#wine $(TARGET).exe

reverse = $(if $(1),$(call reverse,$(wordlist 2,$(words $(1)),$(1)))) $(firstword $(1))

clean-windows: clean-server
	rm -rf out/windows

out/windows: | out
	echo Making windows $(WINDOWS_DIRS)
	mkdir -p $(WINDOWS_DIRS)

.PRECIOUS: $(TARGET).exe $(OBJECTS_WINDOWS) $(OBJECTS_COMMON_WINDOWS) $(LIB_TARGET_WINDOWS)

.PHONY: windows windows-run clean-windows
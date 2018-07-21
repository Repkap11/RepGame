TARGET = RepGame
LIBS = -lm -l GL -l GLU -l GLEW -l glut -pthread 
CC_LINUX = g++
LD_LINUX = ld

CC_ANDROID = aarch64-linux-gnu-g++
LD_ANDROID = aarch64-linux-gnu-ld

#SHELL = sh -xv
#CFLAGS = -Wall -std=c++98 -Wno-unused-variable -O3 -march=native -flto
CFLAGS = -g -Wall -std=c++98 -Wno-unused-variable -flto 
CFLAGS_LINUX = -march=native
CFLAGS_ANDROID = -fPIC
CPUS ?= $(shell nproc || echo 1)
MAKEFLAGS += --jobs=$(CPUS)
#MAKEFLAGS += --jobs=1
LIB_TARGET_LINUX = out/linux/lib$(TARGET).so
LIB_TARGET_ANDROID = out/android/lib$(TARGET).so

run: linux Makefile
	./$(TARGET)

linux: $(TARGET)

all: linux android Makefile
	echo Nothing to do

DIRS = $(patsubst %/, %, $(sort $(dir $(patsubst src/%, out/linux/%,\
	$(wildcard src/*)\
	$(wildcard src/**/*)\
	))))\
	   $(patsubst %/, %, $(sort $(dir $(patsubst src/%, out/android/%,\
	$(wildcard src/*)\
	$(wildcard src/**/*)\
	))))
OBJECTS_SHARED_LINUX = $(patsubst src/%.cpp, out/linux/%.so,\
	$(wildcard src/*.cpp)\
	$(wildcard src/abstract/*.cpp)\
	)
OBJECTS_SHARED_ANDROID = $(patsubst src/%.cpp, out/android/%.so,\
	$(wildcard src/*.cpp)\
	$(wildcard src/abstract/*.cpp)\
	)
OBJECTS_LINUX = $(patsubst src/%.cpp, out/linux/%.so,\
	$(wildcard src/glut/*.cpp)\
	)
HEADERS = $(wildcard include/*.h) $(wildcard include/**/*.h)

out/linux/%.so: src/%.cpp $(HEADERS) Makefile
	$(CC_LINUX) -I include/ $(CFLAGS) $(CFLAGS_LINUX) -c $< -o $@
out/android/%.so:  src/%.cpp $(HEADERS) Makefile
	$(CC_ANDROID) -I include/ $(CFLAGS) $(CFLAGS_ANDROID) -c $< -o $@

$(TARGET): $(LIB_TARGET_LINUX) $(OBJECTS_LINUX) $(OBJECTS_SHARED_LINUX) Makefile 
	$(CC_LINUX) $(CFLAGS) $(CFLAGS_LINUX) $(LIB_TARGET) $(OBJECTS_LINUX) $(OBJECTS_SHARED_LINUX) -Wall $(LIBS) -o $@

$(LIB_TARGET_LINUX): $(OBJECTS_SHARED_LINUX) out Makefile
	$(LD_LINUX) -r $(OBJECTS_SHARED_LINUX) -o $@

$(LIB_TARGET_ANDROID): $(OBJECTS_SHARED_ANDROID) out Makefile
	$(LD_ANDROID) -r $(OBJECTS_SHARED_ANDROID) -o $@

clean:
	rm -rf out
	rm -f $(TARGET)

map:
	rm -rf World1

android: $(LIB_TARGET_ANDROID) Makefile
	./android/gradlew -p android installDebug
	adb shell monkey -p com.repkap11.repgame -c android.intent.category.LAUNCHER 1

install:
	apt install freeglut3-dev libglew-dev libglm-dev libglm-doc g++-multilib-arm-linux-gnueabi g++-aarch64-linux-gnu

.PRECIOUS: $(TARGET) $(OBJECTS_LINUX) $(OBJECTS_SHARED_LINUX) $(OBJECTS_SHARED_ANDROID) $(LIB_TARGET_LINUX) $(LIB_TARGET_ANDROID)

$(info $$LIB_TARGET_ANDROID is [${LIB_TARGET_ANDROID}])
.PHONY: all clean install linux run world android map
$(shell mkdir -p $(DIRS))
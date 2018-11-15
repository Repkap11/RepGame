TARGET = RepGame
REPSERVER = RepGameServer 
#CFLAGS = -Wall -Werror -std=c++98 -Wno-unused-variable -fPIC -O3
CFLAGS = -g -std=c++98 -Wno-unused-variable -fPIC
CPUS ?= $(shell nproc || echo 1)
MAKEFLAGS += --jobs=$(CPUS)
MAKEFLAGS += -k
#SHELL = sh -xv

#Linux x86_64 builds
CC_LINUX = g++
LD_LINUX = ld -r
CFLAGS_LINUX = $(CFLAGS) -march=native -DREPGAME_LINUX -flto -no-pie
LIBS_LINUX = -L/usr/local/cuda-9.2/lib64 -l m -l GL -l GLU -l GLEW -l glut -pthread
LIB_TARGET_LINUX = out/linux/lib$(TARGET).so
INCLUDES_LINUX = -I include/ -I /usr/include/glm

#Linux Cuda
CC_CUDA = /usr/bin/nvcc
#If the current system has the cuda compiler, use CUDA to accelerate terrain gen
ifneq ("$(wildcard $(CC_CUDA))","")
	CFLAGS_CUDA = -arch=sm_35 -Xcompiler -fPIC -DREPGAME_LINUX
	CFLAGS_CUDA_COMPILE = -x cu -dc -c
	CFLAGS_CUDA_LINK_DEVICE = --lib
	CFLAGS_CUDA_LINK_HOST = -dlink
	LIB_DEVICE_CUDA = out/cuda/lib$(TARGET)_device.so
	LIB_TARGET_CUDA = out/cuda/lib$(TARGET).so
	LIBS_LINUX += -lcudart
	CFLAGS_LINUX += -DLOAD_WITH_CUDA
endif

#Default target
all: linux server android

DIRS = $(patsubst %/, %, $(sort $(dir $(patsubst src/%, out/linux/%, $(wildcard src/*) $(wildcard src/**/*)))))\
	   $(patsubst %/, %, $(sort $(dir $(patsubst src/%, out/server/%, $(wildcard src/*) $(wildcard src/**/*)))))\
	   $(patsubst %/, %, $(sort $(dir $(patsubst src/%, out/cuda/%, $(wildcard src/*) $(wildcard src/**/*)))))\
	   android/app/src/main/assets/shaders

OBJECTS_LINUX = $(patsubst src/%.cpp, out/linux/%.so, $(wildcard src/linux/*.cpp))
OBJECTS_CUDA = $(patsubst src/%.cu, out/cuda/%.so, $(wildcard src/cuda/*.cu))

ALL_SHARED = $(wildcard src/*.cpp) $(wildcard src/abstract/*.cpp) $(wildcard src/utils/*.cpp)
OBJECTS_SERVER = $(patsubst server/src/%.cpp, out/server/%.so, $(wildcard server/src/*.cpp))
OBJECTS_SHARED_LINUX = $(patsubst src/%.cpp, out/linux/%.so, $(ALL_SHARED))
HEADERS = $(wildcard include/*.hpp) $(wildcard include/**/*.hpp)
SHADERS_ANDROID = $(patsubst shaders/%.glsl, android/app/src/main/assets/%.glsl, $(wildcard shaders/*.glsl))

android/app/src/main/assets/%.glsl: shaders/%.glsl Makefile
	cp $< $@

out/linux/%.so: src/%.cpp $(HEADERS) Makefile
	$(CC_LINUX) $(INCLUDES_LINUX) $(CFLAGS_LINUX) -c $< -o $@

out/cuda/%.so: src/%.cu $(HEADERS) Makefile
	$(CC_CUDA) $(CFLAGS_CUDA_COMPILE) $(INCLUDES_LINUX) $(CFLAGS_CUDA) $< -o $@

out/server/%.so: server/src/%.cpp Makefile 
	$(CC_LINUX) $(CFLAGS_LINUX) -c $< -o $@

$(LIB_DEVICE_CUDA): $(OBJECTS_CUDA) Makefile
	$(CC_CUDA) $(CFLAGS_CUDA) $(CFLAGS_CUDA_LINK_DEVICE) $(OBJECTS_CUDA) -o $@

$(LIB_TARGET_CUDA): $(LIB_DEVICE_CUDA) Makefile
	$(CC_CUDA) $(CFLAGS_CUDA) $(CFLAGS_CUDA_LINK_HOST) $(LIB_DEVICE_CUDA) -o $@

$(TARGET): $(OBJECTS_SHARED_LINUX) $(OBJECTS_LINUX) $(LIB_TARGET_CUDA) $(LIB_DEVICE_CUDA) Makefile
	$(CC_LINUX) -flto $(CFLAGS_LINUX) $(LIB_TARGET) $(OBJECTS_LINUX) $(OBJECTS_SHARED_LINUX) $(LIB_TARGET_CUDA) $(LIB_DEVICE_CUDA) $(LIBS_LINUX) -o $@

$(REPSERVER): $(OBJECTS_SERVER) Makefile
	$(CC_LINUX) -flto $(CFLAGS_LINUX) $(OBJECTS_SERVER) -o $@

run: linux android-run
	./$(TARGET)

server: $(REPSERVER)
	
linux: $(TARGET)

android: $(SHADERS_ANDROID) Makefile
	./android/gradlew -q -p android assembleDebug

linux-run: linux
	./$(TARGET)

android-run: android
	adb shell input keyevent KEYCODE_WAKEUP
	./android/gradlew -q -p android installDebug
	adb logcat -c
	adb shell monkey -p com.repkap11.repgame -c android.intent.category.LAUNCHER 1
	#adb logcat -s RepGameAndroid -v brief
	#adb logcat -v brief | grep RepGame
map:
	rm -rf World1

clean-linux: map
	rm -rf out
	rm -f $(TARGET)

clean-android:
	rm -f $(SHADERS_ANDROID)
	./android/gradlew -q -p android clean

clean: clean-android clean-linux map

install:
	sudo apt install freeglut3-dev libglew-dev libglm-dev libglm-doc nvidia-cuda-toolkit

.PRECIOUS: $(TARGET) $(OBJECTS_LINUX) $(OBJECTS_SHARED_LINUX) $(OBJECTS_SHARED_ANDROID) $(LIB_TARGET_LINUX) $(REPSERVER)

#$(info $$SHADERS_ANDROID is [${SHADERS_ANDROID}])
.PHONY: all clean install linux run android map android-run linux-run android-shaders clean-linux clean-android server
$(shell mkdir -p $(DIRS))
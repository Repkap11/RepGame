TARGET = RepGame
CFLAGS = -g -Wall -std=c++98 -Wno-unused-variable
LD_FLAGS = -r
CPUS ?= $(shell nproc || echo 1)
MAKEFLAGS += --jobs=$(CPUS)
MAKEFLAGS += -k
#SHELL = sh -xv


#Linux x86_64 builds
CC_LINUX = g++
LD_LINUX = ld -r
CFLAGS_LINUX = -march=native -DREPGAME_LINUX -flto
LIBS_LINUX = -L/usr/local/cuda-9.2/lib64 -l m -l GL -l GLU -l GLEW -l glut -pthread -lcudart
LIB_TARGET_LINUX = out/linux/lib$(TARGET).so

#Linux Cuda
CC_CUDA = /usr/local/cuda-9.2/bin/nvcc
CFLAGS_CUDA = -arch=sm_35 -Xcompiler -fPIC -DREPGAME_LINUX
CFLAGS_CUDA_COMPILE = -x cu -dc -c
CFLAGS_CUDA_LINK_DEVICE = --lib
CFLAGS_CUDA_LINK_HOST = -dlink
#If the current system has the cuda compiler, use CUDA to accelerate terrain gen
ifneq ("$(wildcard $(CC_CUDA))","")
	LIB_DEVICE_CUDA = out/cuda/lib$(TARGET)_device.so
	LIB_TARGET_CUDA = out/cuda/lib$(TARGET).so
	LIBS_LINUX += -lcudart
	CFLAGS_LINUX += -DLOAD_WITH_CUDA
endif

#Android arm64 builds
include android/local.properties
ANDROID_NDK_LOCATION = ${ndk.dir}
CC_ANDROID = $(ANDROID_NDK_LOCATION)/toolchains/aarch64-linux-android-4.9/prebuilt/linux-x86_64/bin/aarch64-linux-android-g++
LD_ANDROID = $(ANDROID_NDK_LOCATION)/toolchains/aarch64-linux-android-4.9/prebuilt/linux-x86_64/bin/aarch64-linux-android-ld
CFLAGS_ANDROID = -fPIC -Wno-attributes
LIB_TARGET_ANDROID = out/android/lib$(TARGET).so

all: linux android

DIRS = $(patsubst %/, %, $(sort $(dir $(patsubst src/%, out/linux/%, $(wildcard src/*) $(wildcard src/**/*)))))\
	   $(patsubst %/, %, $(sort $(dir $(patsubst src/%, out/android/%, $(wildcard src/*) $(wildcard src/**/*)))))\
	   $(patsubst %/, %, $(sort $(dir $(patsubst src/%, out/cuda/%, $(wildcard src/*) $(wildcard src/**/*)))))

OBJECTS_LINUX = $(patsubst src/%.cpp, out/linux/%.so, $(wildcard src/linux/*.cpp))
OBJECTS_CUDA = $(patsubst src/%.cu, out/cuda/%.so, $(wildcard src/cuda/*.cu))
OBJECTS_ANDROID = #There are no object just for Android right now. Those likely need the Android NDK, and are build by cmake and gradle

OBJECTS_SHARED_LINUX = $(patsubst src/%.cpp, out/linux/%.so, $(wildcard src/*.cpp) $(wildcard src/abstract/*.cpp))
OBJECTS_SHARED_ANDROID = $(patsubst src/%.cpp, out/android/%.so, $(wildcard src/*.cpp) $(wildcard src/abstract/*.cpp))
HEADERS = $(wildcard include/*.h) $(wildcard include/**/*.h)
SHADERS_ANDROID = $(patsubst shaders/%.glsl, android/app/src/main/assets/%.glsl, $(wildcard shaders/*.glsl))

android/app/src/main/assets/%.glsl: shaders/%.glsl Makefile
	cp $< $@

out/linux/%.so: src/%.cpp $(HEADERS) Makefile
	$(CC_LINUX) -I include/ -I /usr/include/glm $(CFLAGS) $(CFLAGS_LINUX) -c $< -o $@

out/cuda/%.so: src/%.cu $(HEADERS) Makefile
	$(CC_CUDA) $(CFLAGS_CUDA_COMPILE) -I include/ -I /usr/include/glm $(CFLAGS_CUDA) $< -o $@

out/android/%.so:  src/%.cpp $(HEADERS) Makefile
	$(CC_ANDROID) $(CFLAGS) $(CFLAGS_ANDROID) -c $< -o $@ -I include/ -I $(ANDROID_NDK_LOCATION)/sysroot/usr/include -I /usr/include/glm -I $(ANDROID_NDK_LOCATION)/sysroot/usr/include/aarch64-linux-android/ -I $(ANDROID_NDK_LOCATION)/sources/cxx-stl/gnu-libstdc++/4.9/include -I $(ANDROID_NDK_LOCATION)/sources/cxx-stl/gnu-libstdc++/4.9/libs/arm64-v8a/include/

$(LIB_TARGET_LINUX): $(OBJECTS_SHARED_LINUX) Makefile
	$(LD_LINUX) $(LD_FLAGS) $(OBJECTS_SHARED_LINUX) -o $@

$(LIB_DEVICE_CUDA): $(OBJECTS_CUDA) Makefile
	$(CC_CUDA) $(CFLAGS_CUDA) $(CFLAGS_CUDA_LINK_DEVICE) $(OBJECTS_CUDA) -o $@

$(LIB_TARGET_CUDA): $(LIB_DEVICE_CUDA) Makefile
	$(CC_CUDA) $(CFLAGS_CUDA) $(CFLAGS_CUDA_LINK_HOST) $(LIB_DEVICE_CUDA) -o $@

$(LIB_TARGET_ANDROID): $(OBJECTS_SHARED_ANDROID) Makefile
	$(LD_ANDROID) $(LD_FLAGS) $(OBJECTS_SHARED_ANDROID) -o $@

$(TARGET): $(LIB_TARGET_LINUX) $(OBJECTS_LINUX) $(LIB_TARGET_CUDA) $(LIB_DEVICE_CUDA) Makefile
	$(CC_LINUX) $(CFLAGS) $(LIB_TARGET) $(OBJECTS_LINUX) $(LIB_TARGET_LINUX) $(LIB_TARGET_CUDA) $(LIB_DEVICE_CUDA) $(LIBS_LINUX) -o $@

run: linux android-run
	./$(TARGET)

linux: $(TARGET)

android: $(LIB_TARGET_ANDROID) $(SHADERS_ANDROID) Makefile
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

clean: map
	rm -rf out
	rm -f $(TARGET)
	rm -f $(SHADERS_ANDROID)
	./android/gradlew -q -p android clean

install:
	apt install freeglut3-dev libglew-dev libglm-dev libglm-doc g++-multilib-arm-linux-gnueabi g++-aarch64-linux-gnu
	wget http://developer.download.nvidia.com/compute/cuda/repos/ubuntu1710/x86_64/cuda-repo-ubuntu1710_9.2.148-1_amd64.deb -O cuda.deb
	dpkg -i cuda.deb
	rm cuda.deb
	sudo apt-key adv --fetch-keys https://developer.download.nvidia.com/compute/cuda/repos/ubuntu1710/x86_64/7fa2af80.pub
	apt update
	apt install cuda

.PRECIOUS: $(TARGET) $(OBJECTS_LINUX) $(OBJECTS_ANDROID) $(OBJECTS_SHARED_LINUX) $(OBJECTS_SHARED_ANDROID) $(LIB_TARGET_LINUX) $(LIB_TARGET_ANDROID)

#$(info $$SHADERS_ANDROID is [${SHADERS_ANDROID}])
.PHONY: all clean install linux run android map android-run linux-run android-shaders
$(shell mkdir -p $(DIRS))
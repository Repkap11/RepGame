TARGET = RepGame
CFLAGS = -g -Wall -std=c++98 -Wno-unused-variable
#SHELL = sh -xv
CPUS ?= $(shell nproc || echo 1)
MAKEFLAGS += --jobs=$(CPUS)

#Linux x86_64 builds
CC_LINUX = g++
LD_LINUX = ld
CFLAGS_LINUX = -march=native -DREPGAME_LINUX -flto
LIBS_LINUX = -l m -l GL -l GLU -l GLEW -l glut -pthread
LIB_TARGET_LINUX = out/linux/lib$(TARGET).so

#Android arm64 builds
include android/local.properties
ANDROID_NDK_LOCATION = ${ndk.dir}
CC_ANDROID = $(ANDROID_NDK_LOCATION)/toolchains/aarch64-linux-android-4.9/prebuilt/linux-x86_64/bin/aarch64-linux-android-g++
LD_ANDROID = $(ANDROID_NDK_LOCATION)/toolchains/aarch64-linux-android-4.9/prebuilt/linux-x86_64/bin/aarch64-linux-android-ld
CFLAGS_ANDROID = -fPIC -Wno-attributes
LIB_TARGET_ANDROID = out/android/lib$(TARGET).so

all: linux android

run: linux android-run
	./$(TARGET)

linux: $(TARGET)

android: $(LIB_TARGET_ANDROID) Makefile
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

DIRS = $(patsubst %/, %, $(sort $(dir $(patsubst src/%, out/linux/%, $(wildcard src/*) $(wildcard src/**/*)))))\
	   $(patsubst %/, %, $(sort $(dir $(patsubst src/%, out/android/%, $(wildcard src/*) $(wildcard src/**/*)))))

OBJECTS_LINUX = $(patsubst src/%.cpp, out/linux/%.so, $(wildcard src/linux/*.cpp))
OBJECTS_ANDROID = #There are no object just for Android right now. Those likely need the Android NDK, and are build by cmake and gradle

OBJECTS_SHARED_LINUX = $(patsubst src/%.cpp, out/linux/%.so, $(wildcard src/*.cpp) $(wildcard src/abstract/*.cpp))
OBJECTS_SHARED_ANDROID = $(patsubst src/%.cpp, out/android/%.so, $(wildcard src/*.cpp) $(wildcard src/abstract/*.cpp))
HEADERS = $(wildcard include/*.h) $(wildcard include/**/*.h)

out/linux/%.so: src/%.cpp $(HEADERS) Makefile
	$(CC_LINUX) -I include/ -I /usr/include/glm $(CFLAGS) $(CFLAGS_LINUX) -c $< -o $@

out/android/%.so:  src/%.cpp $(HEADERS) Makefile
	$(CC_ANDROID) $(CFLAGS) $(CFLAGS_ANDROID) -c $< -o $@ -I include/ -I $(ANDROID_NDK_LOCATION)/sysroot/usr/include -I /usr/include/glm -I $(ANDROID_NDK_LOCATION)/sysroot/usr/include/aarch64-linux-android/ -I $(ANDROID_NDK_LOCATION)/sources/cxx-stl/gnu-libstdc++/4.9/include -I $(ANDROID_NDK_LOCATION)/sources/cxx-stl/gnu-libstdc++/4.9/libs/arm64-v8a/include/

$(TARGET): $(LIB_TARGET_LINUX) $(OBJECTS_LINUX) $(OBJECTS_SHARED_LINUX) Makefile 
	$(CC_LINUX) $(CFLAGS) $(CFLAGS_LINUX) $(LIB_TARGET) $(OBJECTS_LINUX) $(OBJECTS_SHARED_LINUX) -Wall $(LIBS_LINUX) -o $@

$(LIB_TARGET_LINUX): $(OBJECTS_SHARED_LINUX) out Makefile
	$(LD_LINUX) -r $(OBJECTS_SHARED_LINUX) -o $@

$(LIB_TARGET_ANDROID): $(OBJECTS_SHARED_ANDROID) out Makefile
	$(LD_ANDROID) -r $(OBJECTS_SHARED_ANDROID) -o $@

map:
	rm -rf World1

clean: map
	rm -rf out
	rm -f $(TARGET)
	./android/gradlew -q -p android clean

install:
	apt install freeglut3-dev libglew-dev libglm-dev libglm-doc g++-multilib-arm-linux-gnueabi g++-aarch64-linux-gnu

.PRECIOUS: $(TARGET) $(OBJECTS_LINUX) $(OBJECTS_ANDROID) $(OBJECTS_SHARED_LINUX) $(OBJECTS_SHARED_ANDROID) $(LIB_TARGET_LINUX) $(LIB_TARGET_ANDROID)

#$(info $$PATH is [${PATH}])
.PHONY: all clean install linux run android map android-run linux-run
$(shell mkdir -p $(DIRS))
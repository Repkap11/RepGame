#Android app
ANDROID_SHADERS = $(patsubst src/shaders/%.glsl,android/app/src/main/assets/%.glsl,$(wildcard src/shaders/*.glsl))
ANDROID_BITMAPS = $(patsubst bitmaps/%.bmp,android/app/src/main/res/raw/%.bin,$(wildcard bitmaps/*.bmp))


ANDROID_DIRS = android/app/src/main/assets/shaders

android/app/src/main/assets/shaders:
	mkdir -p $@
	touch $@

all: android

android-find-new-files:
	touch ./android/app/CMakeLists.txt

android: android-shaders android-bitmaps android-find-new-files
	./android/gradlew --console=plain -q -p android assembleDebug

android-run: android
	adb shell input keyevent KEYCODE_WAKEUP
	./android/gradlew --console=plain -q -p android installDebug
	adb logcat -c
	adb shell monkey -p com.repkap11.${TARGET_LOWER} -c android.intent.category.LAUNCHER 1
	#adb logcat -s ${TARGET}Android -v brief
	#adb logcat -v brief | grep ${TARGET}

deploy: android-deploy

android-deploy: android
	rsync android/app/build/outputs/apk/debug/${TARGET}-debug.apk paul@repkap11.com:/home/paul/website/${TARGET_LOWER}/${TARGET}.apk

clean: clean-android

clean-android:
	rm -f $(ANDROID_SHADERS)
	rm -f $(ANDROID_BITMAPS)
	rm -rf $(ANDROID_DIRS)
	rm -rf android/app/.externalNativeBuild
	./android/gradlew --console=plain -q -p android clean

android-shaders: $(ANDROID_SHADERS)

android-bitmaps: $(ANDROID_BITMAPS)

android/app/src/main/assets/%.glsl: src/shaders/%.glsl | $(ANDROID_DIRS)
	cp $< $@

android/app/src/main/res/raw/%: out/bitmaps/% | $(ANDROID_DIRS)
	cp $< $@

.PHONY: android android-run android-shaders clean-android android-deploy android-find-new-files

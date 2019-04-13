#Android app
MAKEFILES += android.mk

ANDROID_SHADERS = $(patsubst src/shaders/%.glsl,android/app/src/main/assets/%.glsl,$(wildcard src/shaders/*.glsl))
ANDROID_BITMAPS = $(patsubst bitmaps/%.bmp,android/app/src/main/res/raw/%.bin,$(wildcard bitmaps/*.bmp))


ANDROID_DIRS = android/app/src/main/assets/shaders

android/app/src/main/assets/shaders:
	mkdir -p $@

android: android-shaders android-bitmaps $(MAKEFILES)
	./android/gradlew -q -p android assembleDebug

android-run: android
	adb shell input keyevent KEYCODE_WAKEUP
	./android/gradlew -q -p android installDebug
	adb logcat -c
	adb shell monkey -p com.repkap11.repgame -c android.intent.category.LAUNCHER 1
	#adb logcat -s RepGameAndroid -v brief
	#adb logcat -v brief | grep RepGame

android-deploy: android
	rsync android/app/build/outputs/apk/debug/RepGame-debug.apk paul@repkap11.com:/home/paul/website/repgame/RepGame.apk

clean-android:
	rm -f $(ANDROID_SHADERS)
	rm -f $(ANDROID_BITMAPS)
	rm -rf $(ANDROID_DIRS)
	rm -rf android/app/.externalNativeBuild
	./android/gradlew -q -p android clean

android-shaders: $(ANDROID_SHADERS)
android-bitmaps: $(ANDROID_BITMAPS)

android/app/src/main/assets/%.glsl: src/shaders/%.glsl $(MAKEFILES) | $(ANDROID_DIRS)
	cp $< $@

android/app/src/main/res/raw/%: out/bitmaps/% $(MAKEFILES) | $(ANDROID_DIRS)
	cp $< $@

.PRECIOUS: $(OBJECTS_SHARED_ANDROID)

.PHONY: android android-run android-shaders clean-android android-deploy

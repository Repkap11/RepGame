#Android app
MAKEFILES += android.mk

SHADERS_ANDROID = $(patsubst shaders/%.glsl, android/app/src/main/assets/%.glsl, $(wildcard shaders/*.glsl))

android: $(SHADERS_ANDROID) $(MAKEFILES)
	./android/gradlew -q -p android assembleDebug

android-run: android $(MAKEFILES) dirs
	adb shell input keyevent KEYCODE_WAKEUP
	./android/gradlew -q -p android installDebug
	adb logcat -c
	adb shell monkey -p com.repkap11.repgame -c android.intent.category.LAUNCHER 1
	#adb logcat -s RepGameAndroid -v brief
	#adb logcat -v brief | grep RepGame

clean-android: $(MAKEFILES)
	rm -f $(SHADERS_ANDROID)
	./android/gradlew -q -p android clean


android/app/src/main/assets/%.glsl: shaders/%.glsl $(MAKEFILES)
	cp $< $@

.PRECIOUS: $(OBJECTS_SHARED_ANDROID)

.PHONY: android android-run android-shaders clean-android

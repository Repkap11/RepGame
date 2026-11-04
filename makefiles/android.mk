#Android app
JAVA_HOME_LOC = ~/.jdks/jbr-17.0.9

# Legacy asset paths from before shaders/bitmaps/fonts were embedded as
# .incbin blobs in libRepGameAndroid.so — clean-android still wipes them so
# existing checkouts don't ship stale files.
ANDROID_LEGACY_DIRS = android/app/src/main/assets/shaders android/app/src/main/assets/fonts
ANDROID_LEGACY_FILES = android/app/src/main/res/raw/blocks.bin android/app/src/main/res/raw/mobs.bin android/app/src/main/res/raw/sky4.bin

all: android

android-find-new-files:
	touch ./android/app/CMakeLists.txt

# Release is the everyday build: optimized native code (CMake Release =>
# -O3 -DNDEBUG) signed with the debug key so it installs without a keystore.
android: android-find-new-files
	JAVA_HOME=${JAVA_HOME_LOC} ./android/gradlew --console=plain -q -p android assembleRelease

# Debug build keeps normal debug flags (no -O) so the native code is
# debuggable — use this when you need gdb/lldb on device.
android_debug: android-find-new-files
	JAVA_HOME=${JAVA_HOME_LOC} ./android/gradlew --console=plain -q -p android assembleDebug

android-run: android
	adb shell input keyevent KEYCODE_WAKEUP
	JAVA_HOME=${JAVA_HOME_LOC} ./android/gradlew --console=plain -q -p android installRelease
	adb logcat -c
	adb shell monkey -p com.repkap11.${TARGET_LOWER} -c android.intent.category.LAUNCHER 1
	adb logcat -s ${TARGET}Android -v brief

android_debug-run: android_debug
	adb shell input keyevent KEYCODE_WAKEUP
	JAVA_HOME=${JAVA_HOME_LOC} ./android/gradlew --console=plain -q -p android installDebug
	adb logcat -c
	adb shell monkey -p com.repkap11.${TARGET_LOWER} -c android.intent.category.LAUNCHER 1
	adb logcat -s ${TARGET}Android -v brief

deploy: android-deploy

android-deploy: android
	rsync android/app/build/outputs/apk/release/${TARGET}-release.apk paul@repkap11.com:/home/paul/website/${TARGET_LOWER}/${TARGET}.apk

clean: clean-android

clean-android:
	rm -f $(ANDROID_LEGACY_FILES)
	rm -rf $(ANDROID_LEGACY_DIRS)
	rm -rf android/app/.externalNativeBuild
	JAVA_HOME=${JAVA_HOME_LOC} ./android/gradlew --console=plain -q -p android clean

.PHONY: android android_debug android-run android_debug-run clean-android android-deploy android-find-new-files

# Building the Android app

The Android app is part of this repository under `../../android/`. It does not
use a Git submodule and it does not contain a second copy of the router.

## Requirements

- JDK 17
- Android SDK with build-tools, platform 33, CMake and NDK r27+
- Boost, OpenSSL/LibreSSL and zlib built for the target Android ABI, findable
  by CMake (for example a vcpkg prefix)
- No network download happens during the CMake build itself

## Build

```sh
export ANDROID_HOME=/path/to/android-sdk
export ANDROID_NDK_HOME=$ANDROID_HOME/ndk/27.2.12479018
cd android
./gradlew assembleDebug \
  -Pi2pd.ndkVersion=27.2.12479018 \
  -Pi2pd.cmakeArgs="-DCMAKE_PREFIX_PATH=/path/to/dependencies/arm64-android"
```

APKs are produced in `android/app/build/outputs/apk/`. The Gradle build invokes
CMake through `app/src/main/cpp/CMakeLists.txt`, which enables
`I2PD_BUILD_JNI` and disables the host daemon, tools, TUI and tests.

For a vcpkg-based dependency prefix, use the corresponding
`$VCPKG_ROOT/installed/<triplet>` path as `CMAKE_PREFIX_PATH`.

## What changed from upstream i2pd-android

- `ndk-build`/`Android.mk` replaced by CMake. GNU make is not used.
- C++23 sources; NDK r27+ is required.
- The WebView administrative console and `[http]` options were removed.
- Native status summary added through JNI (`getStatusSummary`).
- App-specific external storage replaced broad `MANAGE_EXTERNAL_STORAGE`
  permissions.
- Hard-coded release signing credentials removed.
- Cleartext WebView traffic removed.
- The launcher no longer shows storage-permission gate activities.

## Not validated

The Android app was imported and adapted statically. No Gradle/Android build,
emulator run or device test was executed in this environment. CI keeps the
existing Android cross-compile of the router executable; the app build remains
a manual validation step.

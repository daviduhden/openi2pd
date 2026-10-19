# Building the Android app

The Android app is part of this repository under `../../android/`. It does not
use a Git submodule and it does not contain a second copy of the router.

## Requirements

- OpenJDK 25 (Gradle JVM may be newer; the Java toolchain is exact 25)
- Gradle 9.8.1 and AGP 9.4.1 (the wrapper is pinned)
- Android SDK platform 37, Build Tools 36.0.0 (AGP 9.4 default), CMake and
  NDK r30 LTS (`30.0.16248370` or newer)
- Boost, OpenSSL/LibreSSL, zlib and MiniUPnPc 2.2.6+ built for the target
  Android ABI, findable by CMake (for example a vcpkg prefix). MiniUPnPc backs
  the default-on UPnP client and is listed in
  `contrib/ci/android/vcpkg.json`.
- No network download happens during the CMake build itself

## Build

Static toolchain guard (no compile, Gradle or D8/R8 run):

```sh
sh android/tools/java25-probe/check-config.sh
```

```sh
export JAVA_HOME=/path/to/openjdk-25
export ANDROID_HOME=/path/to/android-sdk
export ANDROID_NDK_HOME=$ANDROID_HOME/ndk/30.0.16248370
cd android
./gradlew assembleDebug \
  -Pi2pd.ndkVersion=30.0.16248370 \
  -Pi2pd.cmakeArgs="-DCMAKE_PREFIX_PATH=/path/to/dependencies/arm64-android"
```

APKs are produced in `android/app/build/outputs/apk/`. The Gradle build invokes
CMake through `app/src/main/cpp/CMakeLists.txt`, which enables
`I2PD_BUILD_JNI` and disables the host daemon, tools, TUI and torrents. Java
source/target 25 is intentional: it compiles to class-file major 69, which
D8/R8 converts to DEX for ART. It does not bundle a JDK and does not make
JDK-only APIs available on Android; see
[Java 25 on Android](../../android-java25.md). Do not lower the Java level to
17 to work around the AGP 9.4.1 bundled-Kotlin target limit; the build keeps
`android.builtInKotlin=false` and remains Java-only.

For a vcpkg-based dependency prefix, use the corresponding
`$VCPKG_ROOT/installed/<triplet>` path as `CMAKE_PREFIX_PATH`.

The Android JNI graph is documented in
[Android JNI status](../../android-jni-status.md), and the Java 25/D8/R8/ART
limitations are in [Java 25 on Android](../../android-java25.md).

## What changed from upstream i2pd-android

- `ndk-build`/`Android.mk` replaced by CMake. GNU make is not used.
- C++23 sources; NDK r30 LTS+ is required.
- The WebView administrative console and `[http]` options were removed.
- Immutable native status snapshot added through JNI
  (`getStatusSnapshot`/`getRecentLogs`; the older `getStatusSummary` remains a
  compatibility helper). See [Android JNI status](../../android-jni-status.md).
- App-specific external storage replaced broad `MANAGE_EXTERNAL_STORAGE`
  permissions.
- Hard-coded release signing credentials removed.
- Cleartext WebView traffic removed.
- The launcher no longer shows storage-permission gate activities.
- UPnP is enabled by default. The JNI target links MiniUPnPc, the manifest
  declares `ACCESS_LOCAL_NETWORK` for Android 17+, and the service holds a
  Wi-Fi multicast lock while the router runs. Denying the runtime permission
  leaves I2P connectivity available but disables UPnP port mapping.

## Not validated

The Android app was imported and adapted statically. No current Gradle/Android
build, APK, Lint, emulator or device test was executed in this environment.
Historical javac/D8/R8 probe results and an earlier app Java compile predate the
latest lifecycle/JNI edits. CI must run `:i2pd:assembleDebug`,
`:i2pd:testDebugUnitTest` and `:i2pd:lintDebug` with OpenJDK 25 and record the
result before claiming validation. Device/FGS/rotation/boot tests remain
manual. The Android same-process service is not full privilege separation;
see [Android modernization](../../android-modernization.md) and TODO
`PRIVSEP-012`/`AND-013`.

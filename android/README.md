# openi2pd Android app

Android front end for the openi2pd router. It is built from this repository;
there is no Git submodule and no external i2pd checkout.

The native router is compiled by the main CMake project through
`app/src/main/cpp/CMakeLists.txt` and exposed to Java through
`org.openi2pd.router.I2PD_JNI`. The former HTTP administrative console and its
WebView activity have been removed; the app shows the router status, service
state and local control actions natively.

## Requirements

- OpenJDK 25 (build/runtime for Gradle; Android still runs ART)
- Gradle 9.8.1 and AGP 9.4.1 (wrapper pinned)
- Android SDK: platform 37, Build Tools 36.0.0, CMake, NDK r30 LTS
  (`30.0.16248370`) or newer
- NDK path in `ANDROID_NDK_HOME` or `local.properties`
- Boost, OpenSSL/LibreSSL, zlib and MiniUPnPc 2.2.6+ for the Android ABI,
  discoverable by CMake (for example through a vcpkg installation). No
  dependency is downloaded by the project build itself. MiniUPnPc backs the
  default-on UPnP client; Android 17+ also asks for local-network access and
  the service holds a Wi-Fi multicast lock for SSDP.

## Build

Before invoking Gradle, CI and contributors can run the compile-free toolchain
guard:

```sh
sh android/tools/java25-probe/check-config.sh
```

It rejects an accidental Java 17 downgrade, unpinned Gradle/AGP/NDK settings
and JDK-only APIs that are unavailable on ART.

```sh
export JAVA_HOME=/path/to/openjdk-25
export ANDROID_HOME=/path/to/android-sdk
export ANDROID_NDK_HOME=$ANDROID_HOME/ndk/30.0.16248370
./gradlew assembleDebug \
  -Pi2pd.ndkVersion=30.0.16248370 \
  -Pi2pd.cmakeArgs="-DCMAKE_PREFIX_PATH=/path/to/android/dependencies"
```

APKs are written to `app/build/outputs/apk/`. Use `universalApk` or the split
APK matching the device ABI.

If your dependency set is provided by vcpkg, point CMake at the installed
prefix for each ABI instead of relying on an automatic download:

```sh
./gradlew assembleDebug \
  -Pi2pd.cmakeArgs="-DCMAKE_PREFIX_PATH=$VCPKG_ROOT/installed/arm64-android"
```

The router library is built with C++23 and the in-tree sources. The Android
manifest requests only network, foreground-service, boot and battery
optimization permissions; storage is app-specific and needs no broad external
storage permission.

## Interface language

The Android application is English-only. `res/values/strings.xml` is the
single UI catalog, `res/xml/locales_config.xml` declares only `en`, and the
Gradle `androidResources.localeFilters` packaging filter keeps non-English
resource splits out of the APK. Non-language qualifiers such as
`values-night`, `values-sw600dp` and `values-v31` remain in use. User data
(torrent and file names, destinations, logs and other Unicode input) is
unaffected.

The compile-free check `perl android/tools/check-english-only.pl` fails if a
language-qualified resource directory, non-English locale configuration,
localized Fastlane listing or runtime locale override reappears. The manual
Android CI job runs it before Gradle.

## Tests

```sh
./gradlew :i2pd:testDebugUnitTest
./gradlew :i2pd:lintDebug
./gradlew :i2pd:connectedDebugAndroidTest   # requires an emulator/device
```

`RouterServiceLifecycleTest` binds and unbinds the non-exported service without
starting the native daemon. The tests are prepared but have not been run in
this environment; CI/device execution is required.

## Identity migration

The application ID and namespace are `org.openi2pd.router`. An installation of
the upstream `org.purplei2p.i2pd` package is a different Android application and
cannot be updated in place. Back up/export data before switching; automatic
migration from the old app-specific directory is not implemented yet.

## Security

- Release builds are unsigned by default. Signing is performed by the
  distribution/release process; no keystore or password is stored here.
- Cleartext WebView traffic and the administrative HTTP console are gone.
- The app uses the same local proxies and I2PControl as the desktop router.
- The native status string exposes only public counts/state.

## License

BSD-3-Clause, see `../LICENSE`. Copyright and attribution notices from the
upstream i2pd-android project are preserved in the source headers and license
files.

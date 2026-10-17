# Android modernization (OpenJDK 25)

Source baseline: PurpleI2P/i2pd-android commit
`f8633c6edc0246db310f98df3a178d19dff40810` (2.61.0), imported under
`android/`. No Android code is fetched at build time and no submodule is used.

## Toolchain matrix

| Component | Minimum / selected | Reason |
|---|---|---|
| JDK running Gradle and compiling Java | **OpenJDK 25** | Required by project policy; checked at Gradle evaluation and via Java toolchain 25 |
| Gradle | 9.8.1 | Supports Java 25/27; checksum pinned in wrapper properties |
| Android Gradle Plugin | 9.4.1 | Current stable; supports compileSdk 37 |
| compileSdk / targetSdk | 37 | Stable API supported by AGP 9.4 |
| minSdk | 24 | `getifaddrs` available; no broad-storage workaround needed |
| NDK | r27+ (default 27.2.12479018, overridable) | C++23 and modern libc++ |
| Native build | CMake (NDK toolchain) | GNU make/ndk-build removed |
| Java language level | 17 | Highest level supported by the current D8/R8 Android pipeline; **not** a claim that ART runs Java 25 |

The Java language level is deliberately separate from the build JDK. OpenJDK
25 compiles the sources; the APK targets Android's ART/D8 feature set. Java
25-only APIs are not used on Android.

## Build configuration changes

- `android/gradle/wrapper/gradle-wrapper.properties`: Gradle 9.8.1 and its
  SHA-256 checksum.
- `android/build.gradle`: AGP 9.4.1; rejects Gradle JVM < 25.
- `android/app/build.gradle`: compileSdk/targetSdk 37, minSdk 24, Java
  toolchain 25, source/target 17, CMake JNI, no hard-coded signing keys.
- `android/gradle.properties`: disables JDK auto-download; no developer paths.
- `android/app/src/main/cpp/CMakeLists.txt`: enables the main project's
  `i2pd_jni` target and disables host-only components.
- Main `build/CMakeLists.txt`: `I2PD_BUILD_JNI` builds `libi2pd.so` from the
  in-tree libraries and JNI sources.

## Application identity

- Application ID / namespace: `org.openi2pd.router` (was
  `org.purplei2p.i2pd`).
- Visible name: `Open I2Pd`.
- JNI symbols: `Java_org_openi2pd_router_I2PD_1JNI_*`.
- App-specific data directory: `open-i2pd` (no broad storage permission).
- Changing the application ID means an existing upstream app cannot be updated
  in place; migration is tracked as TODO AND-013/REBRAND-009.
- Icons are neutral placeholders from `contrib/artwork/open-i2pd-icon.svg`
  until a final visual identity is supplied.

## Architecture

```
I2PDActivity / SettingsActivity
        │
        ├── DaemonWrapper ──> JNI ──> in-tree libi2pd/libi2pdclient
        │        │
        │        └── app-specific files dir (no broad storage permission)
        │
        └── ForegroundService (notification/state)
```

- The router remains C++23; Java only controls lifecycle, UI and status.
- `DaemonWrapper` owns the start/stop thread and state listeners.
- `ForegroundService` is the long-running Android component.
- `I2PD_JNI` exposes lifecycle, service state and a public status summary.
- The former WebView console and `[http]` preferences are gone.

## Java modernization status

Applied:
- Removed WebView console activity/layout/menu/preferences.
- Removed storage-permission gate activities; app-specific external storage.
- Removed hard-coded release keystore/password and cleartext WebView traffic.
- Added native status summary (`getStatusSummary`).
- Updated dependencies (`androidx.core` 1.19.1,
  `commons-configuration2` 2.15.1).
- Gradle/AGP/JDK toolchain modernization.

Prepared but not complete:
- Records/sealed types/switch expressions in the remaining Java classes.
- Lifecycle/state separation beyond the current `DaemonWrapper`.
- Full JNI exception/local-reference audit and native shutdown/restart tests.
- Android Lint/Error Prone execution.

## JNI audit notes

- `setDataDir`/`setLanguage` currently obtain UTF chars and release them; null
  returns and pending exceptions need explicit handling.
- `getStatusSummary` creates a local string reference; no global references are
  retained.
- The router is started/stopped from a Java worker thread; native start/stop
  must remain serialized.
- No native pointers are stored in Java objects.
- Further RAII wrappers for `GetStringUTFChars` are planned (TODO SEC-010).

## Security changes

- `MANAGE_EXTERNAL_STORAGE`, read/write external storage and the permission
  gate activities removed.
- Storage moved to `Context.getExternalFilesDir()`.
- No release signing credentials in Gradle.
- WebView/cleartext/admin console removed.
- Manifest keeps only network, foreground service, boot and battery
  optimization permissions.
- Remaining audit: exported receivers, intent validation, notification
  intents, JNI exception handling, native restart races.

## Validation status

| Check | Status |
|---|---|
| Gradle/AGP configuration evaluation | Reached the missing Android SDK check; build script parsed |
| Gradle wrapper version | 9.8.1 executed successfully |
| JDK 25 enforcement | Code present; tested with JDK 27 as runtime; toolchain 25 not installed |
| APK/DEX build | **Not run** (no Android SDK/NDK) |
| Android Lint | Not run |
| JNI native build | Not run |
| Emulator/device lifecycle | Not run |
| JNI memory/sanitizer tests | Not run |

Commands for the user:

```sh
export JAVA_HOME=/path/to/openjdk-25
export ANDROID_HOME=/path/to/android-sdk
export ANDROID_NDK_HOME=$ANDROID_HOME/ndk/27.2.12479018
cd android
./gradlew :app:assembleDebug \
  -Pi2pd.ndkVersion=27.2.12479018 \
  -Pi2pd.cmakeArgs="-DCMAKE_PREFIX_PATH=/path/to/android/prefix"
```

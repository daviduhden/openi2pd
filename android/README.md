<!--
See the LICENSE file at the top of the project tree for copyright
and license details.

openi2pd: README.

Documents native Android application.

-->

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

### Native torrent administration

The **Torrents** toolbar menu uses the embedded C++23 engine through the same
bounded torrent control dispatcher as the desktop TUI. Calls pass through the
service worker and native lifecycle guard. There is one outstanding operation
per service; closing or rotating the screen does not stop downloads. The UI
supports session selection, magnets, list/progress, pause/resume, keeping files
when removing a torrent, magnet sharing, detail/peer/tracker snapshots, tracker
add/remove/edit and DHT status. Tracker changes require paused torrents with
complete metadata. Refresh is explicit; no background UI polling is added.
Per-torrent actions include file selection/priorities (skip, normal, high) and
comments/ratings with explicit enable/disable, an optional empty-by-default
pseudonym, rating-only or text submissions, and local hiding. The interface
explains that pseudonyms are unverified and publication reaches I2P peers;
private-torrent restrictions remain enforced by the native engine. Comment
commands cross JNI as bounded standard UTF-8 bytes, preserving supplementary
characters without JNI modified-UTF-8 corruption. Local persistence errors are
shown in the comments screen.
**Create torrent** accepts an existing immediate file or folder name within
the session's app-private torrent storage. Hashing runs through the native
storage job; **Creation status** reports its pending/completed result. It does
not select external SAF directories or pass arbitrary filesystem paths.

**Import torrent file** uses Android's Storage Access Framework document picker.
Only a granted `content:` document is accepted; provider names and paths never
reach native code. The service worker reads at most 8 MiB, handles unknown or
incorrect provider sizes, closes descriptors, releases newly retained grants
and requests cancellation after 20 seconds or service destruction. The JNI
byte-array bridge checks the limit again and invokes the shared embedded import
dispatcher under the native lifecycle guard. A provider that ignores platform
cancellation can still delay its Binder call; device tests must cover that case.

On startup a new `tunnels.d/AndroidTorrents.conf` is installed once, without
replacing an existing file. Its torrent data directory is internal app-private
`files/torrents`; no RPC listener is configured. Existing configurations using a
custom `tunnelsdir` must define their torrent session there. The router service
owns session lifetime independently of the Activity.

This is an embedded administration boundary, **not OS process isolation**.
Network/storage workers and a piece-storage SAF broker supporting creation from
external SAF trees remain pending. SAF metadata import does not
grant the engine access to document-tree paths. The bridge rejects filesystem
import commands rather than accepting arbitrary Android paths. External router
mode is also pending. Java 25 is a build target;
no separate JVM, Termux or browser is required on the device.

`TorrentControlTest` and `TorrentDocumentReaderTest` prepare framing, UTF-8
length, duplicate-key, magnet/tracker injection and bounded binary/provider
read regressions for manual Android CI. JNI linking, Gradle unit
and instrumentation tests, ART execution, SAF and device lifecycle validation
have not been run locally.

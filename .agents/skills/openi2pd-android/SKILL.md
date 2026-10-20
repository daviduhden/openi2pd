---
name: openi2pd-android
description: Use when changing the openi2pd Android app, Java 25 build, Material Views UI, RouterService lifecycle, JNI bridge or Storage Access Framework torrent storage.
---
<!--
See the LICENSE file at the top of the project tree for copyright
and license details.
-->

# openi2pd Android

## Activate when

Use this skill for `android/`, JNI wrappers, the foreground service, Material
Views, status/log presentation, configuration, Quick Settings, reseed/log-level
parity and SAF document/tree torrent storage. Use `openi2pd-torrent` for engine
semantics and `openi2pd-build` for the shared native graph.

Read [`../../../AGENTS.md`](../../../AGENTS.md) first. Android is a Java 25
application on ART with `minSdk 29`; no JDK is bundled in the APK.

## Inspect first

- `android/app/build.gradle`, `android/build.gradle`,
  `android/gradle.properties`, wrapper properties and
  `android/tools/java25-probe/check-config.sh`.
- `android/app/src/main/java/org/openi2pd/router/` — `RouterService`,
  `RouterRepository`, `RouterViewModel`, `DaemonWrapper`, `I2PD_JNI`,
  `RouterConfigStore`, `TorrentControl`, `TorrentSaf*`, fragments/activities.
- `android/app/src/main/cpp/` — `i2pd_android.cpp`, `DaemonAndroid.*`, JNI
  header, `CMakeLists.txt`; `android/app/src/main/aidl/`.
- Resources under `android/app/src/main/res/` and tests under
  `android/app/src/test/` and `android/app/src/androidTest/`.
- [`../../../docs/android-modernization.md`](../../../docs/android-modernization.md)
  and [`../../../docs/android-java25.md`](../../../docs/android-java25.md).

## Reference sources

- [`../../../docs/android-modernization.md`](../../../docs/android-modernization.md)
  — toolchain, presentation architecture, service lifecycle, UI and SAF.
- [`../../../docs/android-java25.md`](../../../docs/android-java25.md) — Java 25,
  class-file/DEX evidence, API limitations and pending validation.
- [`../../../docs/android-jni-status.md`](../../../docs/android-jni-status.md) —
  snapshot/log JNI contract, threading and error behavior.
- [`../../../docs/torrent/saf-storage.md`](../../../docs/torrent/saf-storage.md)
  and [`../../../docs/torrent/storage-worker.md`](../../../docs/torrent/storage-worker.md).
- [`references/android-boundaries.md`](references/android-boundaries.md) —
  service/JNI/SAF checklist and test map.

## Reconnaissance

Launch read-only `explore` subagents for:

1. Activity/Fragment/ViewModel/service ownership and lifecycle;
2. JNI calls, worker threads, lifetimes and error paths;
3. SAF document/tree grants, AIDL broker, Binder death/recovery;
4. Gradle/CMake/NDK configuration and CI steps;
5. unit/instrumentation tests and resource-policy checks.

Ask for exact Java/native paths and thread boundaries. Do not let them build,
run Gradle, install an APK or run a device test.

## Parallel implementation

- Split UI, service/state, JNI and SAF broker by independent files. Do not edit
  `build.gradle`, the JNI CMake graph or the AIDL contract concurrently without
  coordinator review.
- Torrent engine behavior belongs to `openi2pd-torrent`; Android owns the
  Java-facing control surface and SAF adapter.
- Keep UI presentation changes separate from service/JNI lifecycle changes.
- Do not let two agents edit `RouterService`, `DaemonWrapper` or the AIDL
  interface at the same time.

## Conventions

- Keep Java source/target 25, exact JDK 25 toolchain, Gradle JVM >=25, AGP
  9.4.1, compile/target 37, `minSdk 29` and NDK r30 unless a deliberate
  toolchain task updates the whole matrix.
- The APK contains ART-executed DEX, not a JDK. Do not add a JDK, V8, QuickJS or
  a runtime download.
- Keep Material 3/Views/Java; no Kotlin/Compose migration unless explicitly
  requested.
- Activities/Fragments observe immutable state; the service owns native
  lifecycle, no JNI on the main thread. Use one worker executor for status/log
  polling and graceful stop.
- The router service remains same-process and non-exported; do not expose JNI
  or raw storage to UI. A future isolated router process needs a separate
  security/lifecycle design.
- SAF uses provider URI authority. Never manufacture a filesystem path, copy
  the whole tree or add unrestricted staging. Use exact document/tree
  capabilities, persist grants, rehash existing pieces and fail closed on
  unsupported seek.
- Keep the separate-process AIDL broker non-exported; use generation fencing
  for Binder death/rebind and do not replay ambiguous writes.
- Remove pre-API-29 compatibility branches and legacy launcher fallbacks when
  touching resources.

## Do not

- Do not compile, run Gradle, unit/instrumentation tests, Lint, an emulator or
  a device locally.
- Do not bundle a JDK, add Kotlin/Compose, enable cleartext, export the service
  or reintroduce broad storage permissions.
- Do not add main-thread JNI, unbounded snapshots, raw native pointers, raw
  file paths, URI-to-path conversion or recursive SAF deletion.
- Do not weaken the SAF/AIDL authority checks or Binder recovery fencing.
- Do not commit, push, tag or release.

## Prepare for CI

- JVM tests under `android/app/src/test/` for parsers/state/config; register
  in the Gradle test task.
- Instrumentation under `android/app/src/androidTest/` for navigation,
  lifecycle, SAF provider/cancellation, English-only resources and JNI
  consistency; device execution stays a separate lane.
- Android CI: Java 25 guard, `:i2pd:testDebugUnitTest`, `lintDebug`, arm64
  native JNI link and debug APK build; vcpkg/NDK prefix must be current.
- Keep dependency coordinates pinned; no runtime download and no bundled JDK.
- Record APK/Lint/JNI/device results as pending until the Android job or a
  device run actually passes.

## Tracking

Use `ANDROID`/H, `PARITY-006`, `PRIVSEP-012` and SAF/state IDs. Record the
toolchain, minSdk, Java/native/JNI changes, tests and exact Gradle/device lane.
Update the Android design/Java 25 documents when behavior changes. Append a
dated `PROGRESS.md` entry; subagents report proposed rows to the coordinator.

## References

- [Android service, JNI and SAF checklist](references/android-boundaries.md)
- [Android modernization](../../../docs/android-modernization.md)
- [Java 25 on Android](../../../docs/android-java25.md)
- [Global policy](../../../AGENTS.md)

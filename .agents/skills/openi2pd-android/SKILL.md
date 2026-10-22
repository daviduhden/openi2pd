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

The permanent `Modern language utilization policy` applies here as well. Use
records, sealed types, exhaustive pattern switches, text blocks, functional
interfaces, generics, streams/collectors and `Optional` where they clarify the
model. Java 25 source features are not proof that a library API exists on
API 29+; virtual threads, scoped values and structured concurrency remain
guarded. Read [`../../../docs/android-java25.md`](../../../docs/android-java25.md)
for the source/class-file/runtime distinction.

## Inspect first

- `android/app/build.gradle`, `android/build.gradle`,
  `android/gradle.properties`, wrapper properties and
  `android/tools/java25-probe/check-config.sh`.
- `android/app/src/main/java/org/openi2pd/router/` — `RouterCommand`,
  `RouterService`,
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

Launch at least one read-only `explore` subagent before editing, split across
independent areas when useful. If subagents are unavailable, report that
limitation; do not substitute local execution. Ask for:

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

## Known issues and decisions

- ANDROID-019 uses ownerExecutor views over one process FIFO for all service
  worker submissions. Retire and append mandatory stop atomically; drain
  previously accepted work before stop and new-owner work. Reject stale submit
  and stale retirement. Never hold the epoch monitor during JNI or shut down
  the process FIFO from onDestroy. ServiceOwnerEpochTest is JVM seam coverage
  only; platform service recreation/JNI evidence remains pending.


- ANDROID-018 uses `daemon/DaemonInit.h` to unwind acquired logging/crypto/
  storage/context phases on failed init. Context persistence drains before
  storage revocation; stopped logging resets to stdout. Preserve durable keys
  and irreversible sandbox effects. `test-daemon-init-rollback` exercises the
  ledger only; production cleanup and JNI/device evidence remain pending.


- `DaemonLifecycle.h` owns the native start transition under `NativeCallGuard`.
  The inner `DaemonAndroidImpl::start()` must invoke `Daemon.start()` even
  while the owner has published `Starting`; treating that state as a duplicate
  skips the real start. `test-android-daemon-lifecycle` covers the portable
  owner contract; JNI linking and actual device restart remain pending CI.
  Failed start after successful init still invokes teardown to release
  crypto/logger initialization state; the common transport-failure cleanup
  alone is incomplete for retries. Phase-aware failed-init unwind is implemented separately in
  ANDROID-018; do not infer production cleanup from the helper's state reset.
- Pinned Material 1.14.0 `TextInputLayout` supports both `setHint(CharSequence)`
  and `setHint(@StringRes int)`. Check the pinned dependency source before
  classifying a resource-ID call as invalid; older API recollection is not
  evidence of a current source defect.
- `RouterCommand` is a sealed interface of nested records decoded by
  `fromAction`; `RouterService.onStartCommand` must keep an exhaustive switch
  with no `default`. Adding an action requires a record, mapping, service case
  and `RouterCommandTest` update.
- `LogLevel`/`LogFilter` use minimum-severity views (`WARN` includes errors;
  `ALL`/`DEBUG` are unfiltered). Classify native tokens with `Locale.ROOT`.
  Keep native `none/critical/error/warn/info/debug` synchronized with
  `DaemonWrapper`.
- Service commands use `stopSelfResult(startId)` through
  `ensureForegroundOrFail`/`stopSelfIfIdle`; clear-logs bumps `logsEpoch` but
  keeps the sequence cursor. Do not close a SAF descriptor from the canceller.
- UI locale catalogs are `values` (English source/fallback),
  `values-es`, `values-ru` and `values-zh-rCN`; `locales_config` and
  `localeFilters` list en/es/ru/zh-CN. Use `openi2pd-i18n` for new visible
  strings. Per-app selection uses `AppCompatDelegate`, never the device locale.
  Do not translate protocol/status tokens, config values or user data.
- Java 25 source features are not Android library APIs. Virtual threads,
  scoped values and structured concurrency remain guarded; D8/R8, Lint,
  APK and device evidence are pending.

## Do not

- Do not compile, run Gradle, unit/instrumentation tests, Lint, an emulator or
  a device locally.
- Do not bundle a JDK, add Kotlin/Compose, enable cleartext, export the service
  or reintroduce broad storage permissions.
- Do not add main-thread JNI, unbounded snapshots, raw native pointers, raw
  file paths, URI-to-path conversion or recursive SAF deletion.
- Do not weaken the SAF/AIDL authority checks or Binder recovery fencing.
- Do not create commits, tags, releases, worktrees or temporary commits, and do not push; inspect `git status`/`git diff` before editing.
- Do not install dependencies, fetch/vendor code or perform hidden downloads to enable a local build or test. Preserve LICENSE/NOTICE/provenance and technical identifiers/paths unless an approved migration exists (AGENTS.md).

## Prepare for CI

- JVM tests under `android/app/src/test/` for parsers/state/config; register
  in the Gradle test task.
- Instrumentation under `android/app/src/androidTest/` for navigation,
  lifecycle, SAF provider/cancellation, en/es/ru/zh-CN resources and JNI
  consistency; device execution stays a separate lane.
- Android CI: Java 25 guard, `:i2pd:testDebugUnitTest`, `lintDebug`, arm64
  native JNI link and debug APK build; vcpkg/NDK prefix must be current.
- Keep dependency coordinates pinned; no runtime download and no bundled JDK.
- Record APK/Lint/JNI/device results as pending until the Android job or a
  device run actually passes.

## Tracking

Use `ANDROID`/H, `PARITY-006`, `PRIVSEP-012` and SAF/state IDs. Record the
toolchain, minSdk, Java/native/JNI changes, tests and exact Gradle/device lane.
Update the Android design/Java 25 documents when behavior changes. Propose a dated `PROGRESS.md` entry and any `TODO.md`/`ROADMAP.md` changes to the coordinator; append them only when explicitly assigned; subagents report proposed rows to the coordinator.

Record the exact AGENTS.md `Tracking states` value and state which validations were not run; do not collapse them into “pending”.

## References

- [Android service, JNI and SAF checklist](references/android-boundaries.md)
- [Android modernization](../../../docs/android-modernization.md)
- [Java 25 on Android](../../../docs/android-java25.md)
- [Global policy](../../../AGENTS.md)

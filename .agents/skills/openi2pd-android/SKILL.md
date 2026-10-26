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
  `localeFilters` list en/es/ru/zh-CN. Every **translatable** key needs all
  four catalogs; `translatable="false"` identifiers (`app_name`,
  `services_bob/sam/i2cp`, `value_unknown`, `repository_url`, `license_url`)
  and the language/bandwidth/loglevel value arrays are deliberately exempt. Use `openi2pd-i18n` for new visible
  strings. Per-app selection uses `AppCompatDelegate`, never the device locale.
  Do not translate protocol/status tokens, config values or user data.
- Java 25 source features are not Android library APIs. Virtual threads,
  scoped values and structured concurrency remain guarded; D8/R8, Lint,
  APK and device evidence are pending.

- Runtime administration has an Android bridge: `I2PD_JNI` exposes
  `getSubscriptions`/`addSubscription`/`removeSubscription`/`banRouter`/
  `unbanRouter`, `RuntimeAdminPolicy` validates the closed typed inputs and
  `RuntimeAdminFragment` plus `RuntimeAdminParityTest` exercise them. Router
  blocklist *counters* still have no Android surface; `torrentCommand` must
  never be widened to accept `ADMIN` verbs.

- Administrative JNI entry points stay a closed typed set and go through the
  daemon-owned queue: tunnel start/stop (name ≤64), reseed (non-blocking status
  token), add/replace address (name ≤255, destination ≤1024, `Exists` feedback),
  subscription add/remove (URL ≤512) and NetDb ban/unban (44-char hash). Bound
  every string with `GetStringLength()` before UTF-8 conversion, reject on a
  stopped router, and never expose private keys, credentials or paths.
- The router UI is native Material; there is no web console or INTERNET-facing
  administration surface.

- Android permissions are per UID: a second same-UID process is containment,
  not privilege separation; `isolatedProcess` loses INTERNET and persisted SAF
  grants. The SAF broker stays `:torrent_storage`; the router/presentation split
  is `ANDROID-026` and needs a closed `IRouterControl` AIDL.
- `RouterService` owns the router, not an Activity. `RouterRunState` persists
  `run_phase`/`graceful_deadline_epoch_ms`/`run_generation` plus the legacy
  `should_run` mirror; an expired drain is terminal across START_STICKY/boot, a
  live drain re-arms only after `startedOkay`, and corrupt deadlines fail closed.
  `onTaskRemoved` keeps the daemon; `onDestroy` purges callbacks.
- SAF AIDL v2: `handshake` generation, one-shot owner-bound `prepareExport` →
  `openExport(token)`, `retire(generation)`, `(uid,pid)` ownership on every
  handle/root/operation/token, owner-scoped cancel/close/revoke, typed admission
  and a 15 s cancel / 30 s process fence. Admission caps are 8 concurrent
  operations, 4096 handles, 128 tree roots, 8 export tokens (30 s each), URI/
  path <=8192 chars, 32 path components and 16384-byte read/write chunks.
  Raising any cap is an AIDL contract change requiring coordinator review. No
  FD crosses Binder; never delete a user-chosen export document automatically.
- Native code claims one process role (`Router`/`StorageBroker`) before its
  first router/document entry; a conflict returns `process-role-conflict`.
  `stopDaemon` publishes the native stop token before the monitor and a start
  returns `start-canceled`; the JNI empty-string path must never pass a null
  `jchar*` and must clear exceptions/local refs on failure.
- No Gradle/NDK/device evidence exists; do not describe Android separation or
  the AIDL/JNI changes as validated.

- Android torrent commands go through `I2PD_JNI.torrentCommand`, whose allowlist
  must admit every verb the Java UI builds: single/bulk start, stop and
  reannounce, remove-all, recheck and the existing magnet/tracker/comment
  verbs. A UI action rejected by the allowlist is a defect, not a fallback.
- SAF creation tracker hints use the creation-scoped validator; they must not
  require an engine torrent id, and the admin tracker verb still requires a
  positive id. `TorrentControl.single` and `Bulk.REMOVE_ALL*` are the typed
  builders for the torrent detail/session actions.

- Torrent comments page through `TORRENT-DETAIL <tunnel> <id> [offset]
  [limit]` via `TorrentControl.commentPage`; offset 0/limit 16 keeps the
  historic line. Read `comments.offset/count/total/visible/hidden/local/
  truncated`, show age/local markers and hidden previews, and report a busy
  refusal instead of silently dropping an action. The four comment strings
  ship in en/es/ru/zh-Hans; keep placeholder parity.

- SAF payload startup probes each document via `TorrentFile::ProbePayload`
  (open exact final name, `create=false`, close). Missing/deleted/revoked
  documents stay missing and their pieces remain downloadable; zero-length
  documents are never opened. Do not mark every SAF file present or trust the
  resume bitfield for provider storage; present documents are still hashed.
- The broker watchdog starts only after `ioLock`; a queued operation must not
  be cancelled/fenced for a stalled sibling. Bind retries are 6 with 250 ms→10 s
  backoff; success clears exhaustion but restores attempts only after a 30 s
  stable binding; `initialize`/selection/status may re-arm once per window.
  No-seek/socket providers and short writes fail closed without staging.
- Instrumentation fixtures: nested multi-file tree, zero-length, deleted,
  revoked, same-length-replaced, no-seek/socket, read-only write failure and
  post-retire rebind. Device runs are the evidence.

- Java 25 is the real source/class-file target; never lower it. Records,
  sealed types and type-pattern switches require D8 to remove
  `java.lang.Record`/`ObjectMethods` and lower `SwitchBootstraps.typeSwitch`
  and `MatchException` at minSdk 29. A javac success is not ART evidence.
- The compile classpath resolves JDK 25 APIs: keep API-30+/33+
  collection/String/stream APIs and all virtual-thread/scoped-value/FFM/KDF
  APIs out of `src/main` and `androidTest`; `src/test` runs on the build JDK.
  `android.builtInKotlin=false` is the AGP 9.4 workaround; document AGP 10.
- Every Java native declaration needs a header and `Java_*` definition;
  run `android/tools/check-android-jni-symbols.pl`. `noexcept` must match,
  `GetStringChars` needs an RAII owner, pending exceptions must be cleared, and
  no Activity/Listener may be retained. Reuse the process-lifetime SAF bridge
  global class reference and keep old bridges alive for their payloads.
- Prepared gates: all nested probe classes dexed at `--min-api 29`, DEX 039
  and no record/type-switch bootstraps in the packaged DEX. There is no
  automated API denylist; API-30+/33+ usage is prevented by review only.
  Real Gradle/D8/R8/Lint/device evidence is manual CI only.

- Native GUI contract: `UiFactory` owns the programmatic-screen row and
  column helpers (48dp target, logical padding, centred `MaxWidthLinearLayout`,
  content/state description). Never build a screen-local button or a hard-coded
  dp inset in a fragment. Programmatic screens must keep the `w600dp`
  `content_max_width` cap; secondary destinations stay hidden while a detail is
  open and every fragment tag must be hidden on destination change.
- SAF grant mutation is service-owned: `RouterService.selectPayloadTree` /
  `detachPayload` run on `safWorker` and report a typed boolean on the main
  thread. Never call `TorrentSafStorage.select/detach` from a picker or row
  callback: they commit the grant map and may round-trip to the provider.
- I2PSnark snapshots are decoded by the bounded `TorrentRow` record, not by
  direct map lookups in the fragment. A non-numeric id is rejected, a bad
  hash/magnet is dropped and counters/percent are clamped; `TorrentRowTest`
  pins it on the JVM.
- Notification: one drain action only, chosen from the lifecycle state
  (Graceful stop vs Cancel graceful stop) with `setOnlyAlertOnce`; the
  graceful countdown is not a live region.
- `android_ui` is an opt-in dispatch input for the API 29 x86_64 emulator lane.
  It is prepared, not executed; do not claim device/TalkBack/visual evidence.

- Counts use `<plurals>` in all four locales (ru one/few/many/other) and
  `getQuantityString`; never concatenate a number with an English plural.
  Placeholders are positional (`%1$s`). User-visible validation errors use
  resource ids (`TorrentControl.UserMessageException`), and the log row,
  notification channel and torrent labels are resources. `zh-TW`/`zh-Hant`
  must fall back to English; `LocaleResourceTest` pins both rules.

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
- Android CI: `android/tools/java25-probe/check-config.sh`,
  `verify-java25.sh` (class-file 69, D8/R8 `--min-api 29`, DEX 039),
  `check-android-jni-symbols.pl` and `check-android-locales.pl`;
  `:i2pd:testDebugUnitTest`, `lintDebug`, arm64 native JNI link, packaged-DEX
  bootstrap scan and debug APK build; vcpkg/NDK prefix must be current. The
  opt-in `android-ui` job builds x86_64 API 29 and runs
  `connectedDebugAndroidTest`; it remains Prepared / not executed.
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

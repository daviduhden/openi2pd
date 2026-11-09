---
name: openi2pd-android
description: Use when changing the openi2pd Android app, Java 25 build, Material Views UI, RouterService lifecycle, JNI bridge or Storage Access Framework torrent storage.
---

## Maintenance & Continuous Learning

When this skill guides a repository change, review it after implementation. Update procedures, tools, configurations, assumptions, examples and validation steps when they changed or when reusable knowledge improves future work; correct inaccurate guidance and omit one-off execution details. Report synchronization in the task summary. Follow the normative process and Definition of Done in [AGENTS.md](../../../AGENTS.md#continuous-agent--skills-maintenance).
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

- Apply the [source documentation policy](../../../AGENTS.md#source-documentation-policy)
  to lifecycle, main-thread, JNI and Binder/SAF resource ownership contracts.

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
- When Lint flags resources or layout variants, inspect every locale and
  qualifier, accessibility description and actual resource reference before
  editing. Preserve brand vector geometry and intentional compact/wide
  layouts. Keep any `tools:ignore` narrowly scoped to the specific view.
- An ABI filter warning requires a complete native dependency prefix and
  artifact path, not an extra ABI filter alone.

## Known issues and decisions

- DisplayText bounds presentation strings in UTF-16 units without cutting
  valid surrogate pairs. Reuse it in snapshot, log and error caps; preserve
  each caller's null/default behavior. It does not validate malformed
  Unicode, preserve graphemes or replace protocol byte-limit validation.

- MainActivity confirmations use UiConfirmationAuthority foreground/session
  tickets, independently of Fragment view tokens. Consume once before the
  action, revoke before dismissal, and deny registration while framework
  cleanup reenters. Service changes retire consent; click-time action
  admission still needs a fresh snapshot. Keep permission/autostart policy
  separate and use harmless callbacks in Activity lifecycle regressions.

- Give the content surface one opaque owner: the theme window resolves
  colorSurface while phone/tablet activity roots remain transparent. Check
  both night modes and both width qualifiers; the Android 12 splash keeps
  its separate brand background. Do not silence Overdraw or change vector
  geometry to erase unrelated Lint warnings.

- vcpkg's emulator triplet is x64-android; Android and emulator ABI names
  remain x86_64. Keep triplet, installed prefixes and cache inputs aligned.
  Both Android overlay triplets include their pinned upstream first and set
  API 29 afterward; an outer cache variable cannot override upstream API 28.
  Workflow-negative fixtures must copy contrib/ci/android with the actions.

- Document factory validation separately from direct record construction.
  TorrentRow.from bounds decoded fields; its canonical constructor does not
  impose those checks. Settings generation fencing suppresses obsolete UI
  completion but does not cancel an admitted configuration write. UiText's
  closed vocabulary localizes known diagnostics and preserves unknown text;
  this presentation layer is not a secret-redaction boundary.

- UiText is a closed presentation vocabulary for snapshot status and safe
  error categories. DTO/wire tokens and unknown text stay unchanged. Supply
  localized labels and active-locale percentages to TorrentRow, including
  TalkBack; UTF-16 caps must not split surrogate pairs.
- Language preference presentation canonicalizes regional/script variants;
  unknown scripts use English while empty AppCompat locales mean System.
  Explicit Hans takes priority over region; Hant never becomes Simplified.
- Services and Quick Settings do not inherit Activity locale contexts on
  API 29-32. Use ContextCompat.getContextForLanguage with the pinned AndroidX
  core, and refresh notification text/channel names when locale identity
  changes even if daemon state did not. Device refresh remains CI-only.
- Plural instrumentation compares unformatted resource templates when
  checking categories; formatted counts would differ even if the category
  were wrong. Cover Russian 11/21/22 and Chinese's single other category.

- The wide activity uses a weighted outer content area and native relative
  positioning for its rail, divider and remaining fragment width. Do not
  nest a second weighted layout there or add a dependency just for bounds.
  Preserve logical start/end placement and the rail/fragment IDs. Root
  colorSurface and the theme's brand_background are different roles in
  both palettes: removing the former to silence Overdraw changes colors
  unless window paint authority is updated deliberately at the same time.

- Compound vector drawables on the log empty-state view use an explicit
  AppCompatTextView with `app:drawableTopCompat` and `app:drawableTint`.
  Keep its ID, accessibility live region and text styling unchanged when
  adapting drawable attributes; a plain platform TextView does not consume
  AppCompat attributes. Review other layouts for the same attribute pair
  rather than suppressing UseCompatTextViewDrawableXml. Lint success and a
  built APK do not replace the optional API-29 view-inflation checks.

- The AGP native wrapper is a separate top-level CMake entry point. Keep a
  literal `project(OpenI2PdAndroid LANGUAGES C CXX)` after its minimum-version
  declaration, even though the shared build subdirectory declares its own
  versioned project. Otherwise CMake inserts an implicit project before the
  wrapper policy baseline. Dependency prefixes passed by CI should use the
  active package-config discovery interface; remove unused legacy FindBoost
  include/library hints at their invocation sites, not by reading them only
  to suppress warnings. Review both arm64 and optional emulator prefixes.

- JNI static methods receive a mandatory `jclass` argument even when their
  bodies do not use it. Omit unused names in native definitions instead of
  disabling parameter warnings; preserve types, position and exported names.
  Review adjacent entry points globally, since an early core compile failure
  can hide JNI warnings seen by an earlier revision. If an unused documented
  name is removed, describe the VM argument in prose rather than retaining an
  unmatched Doxygen `@param`. Class references passed as actual operation
  operands, such as the SAF bridge type, still require named ownership.

- Dialog callbacks capture `UiDialogEpoch.Token` when the form is created,
  not when clicked. Recheck current view and mutation admission before work;
  retire authority before framework dismissal. Nested torrent forms require
  bounded registrations rather than replacing their parent, and capture the
  selected session so an old form cannot target a newly selected tunnel.
  Preserve custom OnShow button validation and SAF picker ownership when
  changing dialog construction. JVM authority and device lifecycle tests
  cover different seams; neither substitutes for the other.
  Register torrent dialogs immediately before showing, after content
  validation, so malformed pages do not consume retained registrations.
  Activity-owned confirmation/permission flows remain a separate lifecycle
  boundary; do not apply Fragment teardown mechanically to MainActivity.
- API-29 instrumentation fixtures obey the runtime floor too. A class-only
  `String.method` source pattern misses instance calls such as literal or
  variable `.repeat(...)`; the guard now checks their member-call shape and
  preserves unqualified local fixture helpers. Ambiguous names such as
  `lines`/`transform` require receiver-aware Lint instead of a blanket ban.
- SAF creation charges retained provider IDs/URI strings and relative paths
  through `TorrentSafCreationBudget` before collection insertion. Measure
  UTF-8 bytes, reject malformed surrogates, and keep the native 255-byte name
  rule. Check full and final partial-piece hashes before appending. JNI's
  byte-array metadata entry independently limits aggregate paths before copy
  or filesystem-path construction; it does not use Modified UTF-8. These
  retention budgets exclude allocations internal to provider cursors and do
  not make a blocked provider cancellable. Keep JVM and JNI multibyte/bounds
  regressions with the implementation.
- Import, creation and export hold process-wide `TorrentSafGrantLeases`
  tokens through provider IO, including borrowers needing no new mode. The
  registry aggregates acquired modes and defers mapping-detach/rollback
  revocation until the last borrower retires. All registry/framework work
  runs under mapping authority; retirement itself is atomic and nonblocking.
  Busy cleanup drains on the next authority entry. Preserve pre-existing
  modes on failed selection. JVM regressions cover overlap, cap, ownership,
  adoption and retirement; instrumentation covers authority contention.
  External-process consumers and providers ignoring cancellation remain
  outside this process-local guarantee. Failed framework cleanup retains the
  permission without an unbounded retry registry.
- Error messages published in `RouterSnapshot` are an information boundary.
  Map internal exceptions to a closed set of generic categories; never copy
  `Throwable.getMessage()`, causes, class names or stack traces into snapshots
  because provider data and local paths can appear in any of them. Keep a
  pure-JVM negative test with path/secret markers.
- A guard such as `#if !defined(ANDROID)` can compile out the only use of a
  shared parameter. Mark it `[[maybe_unused]]` with the platform reason
  (`DetectDataDir(..., [[maybe_unused]] bool isService)`) instead of
  changing the signature or adding a suppression flag (CW-010).

- The 2026-10-31 Lint report found 51 warnings across 17 issue IDs. Local
  resource/UI edits address 34 instances by static review; the remaining
  ChromeOS ABI, branded vector-path, nested-weight and background-overdraw
  findings need dependency or design review. The exact run and unresolved
  groups are tracked in `docs/ci/COMPILATION-WARNINGS.md`; none is validated
  until Android Lint runs on the corrected revision.
- Torrent SAF creation reports progress bytes separately from its terminal
  result. The terminal value carries encoded torrent metadata or a localized
  error; it does not grant authority over the stored payload. Keep comments
  aligned with that distinction when changing the ViewModel or broker.

- AGP may place ABI-split and universal APKs below variant-specific output
  directories, with names that vary across plugin versions. CI should inspect
  all discovered app APKs, exclude instrumentation APKs, select debug/release
  variants from their output metadata/path and confirm the expected arm64
  library and app assets by archive contents. Reuse the selected debug APK
  for package-content and DEX checks, export its exact path to later steps and
  print discovered APK paths when selection fails. `GITHUB_ENV` values are
  available to later runner steps but not interpolated into an action's
  `with:` expression; stage APKs to a fixed workspace directory before
  uploading artifacts.

- ANDROID-019 uses ownerExecutor views over one process FIFO for all service
  worker submissions. Retire and append mandatory stop atomically; drain
  previously accepted work before stop and new-owner work. Reject stale submit
  and stale retirement. Never hold the epoch monitor during JNI or shut down
  the process FIFO from onDestroy. ServiceOwnerEpochTest is JVM seam coverage
  only; platform service recreation/JNI evidence remains pending.

- SAF import/create/export progress and completion use
  `TorrentSafCallbackPublisher`: capture `ownerEpoch` when accepting the
  request, queue onto the main handler, and re-check the epoch at delivery.
  This closes both already-queued and provider-posts-after-purge callbacks;
  provider cleanup may still finish after retirement. The callback runs under
  the epoch monitor, so keep it short and never wait for shutdown or JNI there.
  `TorrentSafCallbackPublisherTest` covers queued-before-retire,
  post-after-retire and current-owner delivery; Android CI remains pending.


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
- `DocumentsProvider` overloads can make a fixture helper call ambiguous when
  a null argument matches both a platform `Bundle` and a helper `String`.
  Cast null to the intended overload type in instrumentation tests rather than
  changing the provider API or hiding the ambiguity through a test wrapper.
- AGP 9.4.1 does not expose `android.bootClasspath` on the application DSL.
  The manual `sourceJavadoc` task gets platform classes from
  `androidComponents.sdkComponents.bootClasspath`, kept as a task input
  provider alongside the debug Java classpath.
- `RouterCommand` is a sealed interface of nested records decoded by
  `fromAction`; `RouterService.onStartCommand` must keep an exhaustive switch
  with no `default`. Adding an action requires a record, mapping, service case
  and `RouterCommandTest` update.
- Every asynchronous lambda must capture an effectively-final owner/sink
  reference. If a local is normalised or reassigned before queuing work, copy
  the validated value into a final local before creating nested callbacks;
  use a distinct name for each nested generation token. The Android Java 25
  compile lane caught both patterns in the torrent SAF progress callback and
  address-book unblock callback.
- Address-book subscription add/remove, router ban/unban and live
  transit/bandwidth/share controls use dedicated bounded JNI calls through
  `RouterService`'s worker; keep them separate from the torrent command
  allowlist. Validate accepted values in Java and native code, reject a
  stopped router, and keep JNI off the main thread. Runtime controls enqueue
  typed daemon operations and do not update restart-required preferences.
  Fragment callbacks must be invalidated by the view generation and release
  the busy state when the view is destroyed. Subscription removal opens a
  confirmation first, then routes the positive action through
  `submitSubscription`; that submission-time gate is authoritative because
  another mutation can begin while the dialog is open. `RouterService` caps
  merged retained logs before publishing the snapshot list. The
  `control-security-static` contract pins these source boundaries; it is not
  a substitute for Android UI/runtime coverage.
- `LogLevel`/`LogFilter` use minimum-severity views (`WARN` includes errors;
  `ALL`/`DEBUG` are unfiltered). Classify native tokens with `Locale.ROOT`.
  Keep native `none/critical/error/warn/info/debug` synchronized with
  `DaemonWrapper`.
- Service commands use `stopSelfResult(startId)` through
  `ensureForegroundOrFail`/`stopSelfIfIdle`; clear-logs bumps `logsEpoch` but
  keeps the sequence cursor. Do not close a SAF descriptor from the canceller.
- Restart admission uses `DeferredRestart` and the wrapper state monitor;
  a pending ticket waits for start cleanup without polling the control FIFO.
  Stop/replacement invalidates queued tickets, and the owner epoch gates the
  short admission transition atomically with retirement. Launch and callbacks
  run outside the wrapper monitor. Acquire the epoch authority before the
  wrapper monitor; never invoke the external gate while holding that monitor.
  Completion means thread launch was
  accepted, not native readiness; keep `restartInProgress` until that result.
  Explicit Stop invalidates the service restart generation before its native
  stop reaches the FIFO. A failed Thread construction/start must unwind
  `startThreadRunning` through the same completion path. The framework-free
  regression covers deferred admission, cancellation, replacement, retirement,
  rejection and failed launch; JNI/device restart execution remains pending.
- Persisted `RouterRunState` transitions are serialized by
  `RouterService.runStateLock`. Build read/modify/write transitions inside
  `updateRunState`; for a state computed before taking the lock, use
  `RouterRunState.saveIfNewer` so the generation comparison and preference
  write share that monitor. Equal generations must be rejected because two
  candidates derived from one snapshot can conflict. The JVM regression pins
  stale/equal rejection without changing persisted keys or format.
- UI locale catalogs are `values` (English source/fallback),
  `values-es`, `values-ru` and `values-zh-rCN`; `locales_config` and
  `localeFilters` list en/es/ru/zh-CN. Every **translatable** key needs all
  four catalogs; `translatable="false"` identifiers (`app_name`,
  `services_bob/sam/i2cp`, `value_unknown`, `repository_url`, `license_url`)
  and the language/bandwidth/loglevel value arrays are deliberately exempt. Use `openi2pd-i18n` for new visible
  strings. Per-app selection uses `AppCompatDelegate`, never the device locale.
  Do not translate protocol/status tokens, config values or user data.
- Lint cleanup from run 41222227216 at SHA 45f5f739 was source-level
  evidence only. Keep the Application instance (not generic Context) as the static
  SAF registry owner, localize address-book row and accessibility formats,
  and use locale catalogs for count-bearing session labels. Since SAF byte
  progress is a `long`, keep its resource wording noun-before-number rather
  than narrowing it to Android plural APIs' `int` quantity. `values-v29` is
  redundant at minSdk 29; API-33 manifest attributes remain annotated, and
  only the API-33 launcher variant owns the monochrome layer. Keep the 48dp
  log empty-state glyph separate from the 24dp toolbar glyph. The compact
  bottom bar and wide navigation rail intentionally replace one another; keep
  the `InconsistentLayout` annotation scoped to those two views. Do not add an
  APK ABI unless the Gradle/vcpkg dependency prefix for that ABI is packaged.
  The brand vectors intentionally retain their path geometry; lint's vector
  complexity warning is not permission to redraw them. Recheck these decisions
  against the complete Lint report after manual CI runs.

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
- A broadcast never persists a run. `BootUpReceiver` requires the persisted
  non-stopped intent for both actions (plus the boot-autostart marker for
  `BOOT_COMPLETED`) and then delivers `RouterService.requestRestore(context)`,
  which is an intent with **no action** so `onStartCommand` reconciles exactly
  like `START_STICKY`. Sending `ACTION_START` from a receiver re-persists
  `Running` before the service sees the state, which resurrects a stopped
  router and converts a finished drain into a fresh run. The decision is the
  pure `BootUpReceiver.mayResume(action, autostartEnabled, runIntentRequested)`
  seam; `BootResumePolicyTest` (JVM) pins the matrix and
  `RouterServiceLifecycleTest` adds a device-side negative broadcast case.
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
- The CI Java 25 D8/R8 probe must follow `:i2pd:assembleDebug`: unit-test and
  lint tasks can finish without resolving AGP's R8 artifact. Discover the jar
  selected by the pinned AGP from the Gradle cache only after app dexing; do
  not substitute a separately pinned or historical R8 version.

- Native GUI contract: `UiFactory` owns the programmatic-screen row and
  column helpers (48dp target, logical padding, centred `MaxWidthLinearLayout`,
  content/state description). Never build a screen-local button or a hard-coded
  dp inset in a fragment. Programmatic screens must keep the `w600dp`
  `content_max_width` cap; secondary destinations stay hidden while a detail is
  open and every fragment tag must be hidden on destination change.
- SAF grant mutation is service-owned: `RouterService.selectPayloadTree` /
  `detachPayload` / `readTorrentStorageStatus` run on `safWorker` and report a
  typed result on the main thread. Never call
  `TorrentSafStorage.select/detach/status` from a picker, row or request
  callback: they commit the grant map, query live URI permissions and may
  re-arm a bounded broker bind, so the UI must neither block on them nor drive
  the bind sequencing itself.
- Replacing a persisted SAF tree releases both modes of the old,
  unreferenced tree after the mapping commit; never reuse the new tree's
  acquisition flags, since a read-only replacement can otherwise leave stale
  write authority. `releaseUnreferencedGrant` is shared by replace/detach and
  uses the same dedicated `MappingAuthority` as take/commit and failed-commit
  rollback. Obtain the preference snapshot within that authority and keep
  reference check plus framework release in one operation. Concurrent or
  reentrant mutations return false without waiting; cache invalidation and
  broker binding run after authority is released, never beneath its mutex.
  This serializes only in-process writers: a future multiprocess preference
  writer requires a single authoritative IPC owner. Framework release failure
  is best effort; instrumentation prepares mode/reference/failure, contention
  and reentrancy regressions.
- `TorrentDocumentReader` observes both thread interruption and the caller's
  cancellation supplier before and after every read, including EOF. Import
  passes its `CancellationSignal`; the reader never clears interrupts or
  closes caller-owned streams. These checks prevent another read or acceptance
  after observed cancellation; they cannot interrupt a provider already
  blocked in read. Pure-JVM regressions cover those boundary cases.
- The service SAF lane is created by `TorrentSafTaskExecutor`: one active
  operation and sixteen queued requests, with `AbortPolicy`. A blocked
  provider must never cause unbounded callback/grant retention or execute
  provider work on the UI thread. Preserve typed rejection completions and
  owner-epoch delivery fencing. `shutdownNow` discards pending tasks and
  requests interruption; it does not prove that a provider has stopped.
  The framework-free JVM regression pins saturation, recovery and retirement.
  Other executors need their own admission design: the service-owner FIFO
  must retain its mandatory final stop, and broker cleanup cannot simply
  reject descriptor closure when a generic bounded queue fills.
- SAF grant and status completions use `TorrentSafCallbackPublisher` with
  the accepting `ownerEpoch`, including invalid-input and executor-rejection
  paths. Checking `destroyed` before `Handler.post` leaves a retirement race;
  check the epoch when the queued callback actually runs. A busy SAF queue
  completes with false (grant) or an empty diagnostic (status) for a current
  owner; retirement drops delivery. These grant/status operations do not own
  `torrentInFlight` and must not clear another import/export operation's busy
  state. `TorrentSafCallbackPublisherTest` covers queued and post-retirement
  delivery suppression independently of Android framework scheduling.
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
  The locale guard models Android quantity categories per locale: English and
  Spanish use one/other, Russian uses one/few/many/other, and Simplified
  Chinese uses other only. Do not restore Chinese `one` entries to appease a
  checker; Android Lint treats them as unused.
  Placeholders are positional (`%1$s`). User-visible validation errors use
  resource ids (`TorrentControl.UserMessageException`), and the log row,
  notification channel and torrent labels are resources. `zh-TW`/`zh-Hant`
  must fall back to English; `LocaleResourceTest` pins both rules.

- Use Javadoc for lifecycle-owning services, Binder/AIDL contracts, SAF
  authority and JNI reference/exception rules; state main-thread versus
  executor responsibilities. Follow
  [`docs/source-documentation.md`](../../../docs/source-documentation.md);
  doclint runs with warnings as errors in the manual lane.


- `ANDROID-036` records address-book generation/single-flight gaps and a null
  progress-consumer assumption in `TorrentSafCreator`. New UI callbacks must
  capture the view generation and check the busy gate; do not copy the
  affected pattern. The blocked-list read, the metadata-export picker result
  and the SAF creation-source pickers now fence the view generation, and
  `RouterService.createSafTorrent` treats a null progress consumer as "no
  progress". `removeSubscription`'s single-flight and the blocked list's busy
  gate remain open.


- `ReseedPhaseToken()` and similar helpers return `std::string_view`;
  copy-initializing `std::string` from it is ill-formed because the string
  constructor is explicit. Use direct initialization, as in
  `i2pd_android.cpp` (CE-011).

- The Java 25 API-29 denylist must cover inherited `SequencedCollection`
  methods on `List` (`getFirst/getLast/addFirst/addLast/removeFirst/
  removeLast/reversed`), `String.stripIndent`, `Math.clamp` and
  `Thread.threadId`, not only class names and static factories. Keep the
  call-shape positive/negative self-test in `java25-probe/check-config.sh`.
- Metric label `TextView`s associate their value with
  `android:labelFor="@id/<value>"`; the layout checker requires the label
  immediately before the value and a resolvable id, and `UiContractTest`
  traverses the hierarchy to require exactly one nonempty label per value.
  `getLabelFor()` belongs to the label and returns the value ID; querying
  the value for a label ID reverses the contract. Labels need no view ID.
- A second same-UID process is not a privilege boundary; keep SAF authority
  and router/presentation split claims separate from Android UID isolation.

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

- `RouterStatusParser` must default a missing `statusCode` to `-1`; `0` is a
  valid online code and would make `hasStatus()`/live metrics lie. A missing
  internal files directory is an explicit `IllegalStateException`, not a
  nullable torrent path. The reseed status field sanitizes `|`, CR/LF and
  controls before the seven-field Java parser sees it.
- `content_max_width` needs a `values-w600dp` override as well as
  `values-sw600dp`; the rail layout uses `w600dp` and multi-window can select
  it without `smallestWidth >= 600dp`.
- Tunnel service names are bounded opaque protocol tokens. A slash does not
  make a name a filesystem path; Java tests must match the native length and
  control-character predicate rather than rejecting path-like spelling.

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

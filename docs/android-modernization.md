<!--
See the LICENSE file at the top of the project tree for copyright
and license details.

openi2pd: android modernization.

Documents project documentation.

-->

# Android modernization (Java 25 and Material 3)

Source baseline: PurpleI2P/i2pd-android commit
`f8633c6edc0246db310f98df3a178d19dff40810` (2.61.0), imported under
`android/`. No Android code is fetched at build time and no submodule is used.

Status: **source-level implementation complete for the current scope; APK,
Lint, device and full resource-link validation are deferred to CI/user
environment.**

## Toolchain

| Component | Selected | Notes |
|---|---|---|
| Gradle JVM / compiler | **OpenJDK 25** | Gradle JVM >=25; Java toolchain exact 25 |
| Java source/target | **25 / 25** | class-file major 69; not a JDK in the APK |
| Gradle | 9.8.1 | checksum pinned |
| Android Gradle Plugin | 9.4.1 | compileSdk 37 |
| compileSdk / targetSdk | 37 | |
| minSdk | 29 | Android 10 minimum; pre-29 compatibility paths removed |
| NDK | 30.0.16248370 (overridable) | r30 LTS; C++23 JNI build |
| Runtime | ART | executes DEX; no OpenJDK/V8 in APK |

See `docs/android-java25.md` for the `javac`/D8/R8 probe, class-file/DEX
evidence, AGP/Kotlin target limitation and Java 25 APIs unavailable on Android.

The app targets API 37 and enables UPnP by default. The Android JNI graph
links MiniUPnPc, and the manifest declares `CHANGE_WIFI_MULTICAST_STATE` and
the Android 17 runtime permission `ACCESS_LOCAL_NETWORK`. The app requests the
latter on first launch and holds a Wi-Fi multicast lock while the router runs
so SSDP discovery can reach the gateway. User denial leaves the router usable
but disables UPnP port mapping. Android 17 edge-to-edge, local-network and
foreground-service behavior still requires the manual device/Play review listed
below. No target-37 compatibility claim is made from static files alone.

## Modern Java/OOP usage

Java 25 is a source/class-file level, not a claim that every JDK 25 library API
exists on Android API 29. The application deliberately uses records for
immutable snapshots and typed results, sealed interfaces with exhaustive
pattern switches for service commands, switch expressions, lambdas/method
references, generics and `Optional` where they clarify a boundary. Streams and
collectors are not currently used in app source; prefer a clear loop or a
record-producing helper over a pipeline that hides a bound. `RouterCommand`,
`ReseedStatus` and `LogLevel` are the current examples; the service
still performs the same foreground checks, worker ordering and native calls.

Virtual threads, scoped values and structured concurrency are not adopted
because they are guarded library APIs on the supported Android levels and core
library desugaring is disabled. Material 3/Views remain the UI stack; no
Kotlin/Compose migration is implied. Dynamic Gradle/device evidence remains
pending manual CI.

## Presentation architecture

```
MainActivity (AppCompatActivity)
  ├── DashboardFragment
  ├── NetworkFragment
  ├── TunnelsFragment
  ├── StatisticsFragment
  ├── SettingsFragment (PreferenceFragmentCompat)
  ├── LogsFragment (RecyclerView)
  └── AboutFragment
        │ observe LiveData only
RouterViewModel / RouterRepository
        │ application-context binding, no JNI
RouterService (started + bound, non-exported, same process)
        │ owns DaemonWrapper and foreground notification
        ├── DaemonWrapper ──> I2PD_JNI ──> in-tree libi2pd
        └── app-specific files directory
```

- Activities/Fragments do not own the daemon, JNI or sockets. Configuration
  and autostart-marker access is bounded and runs on the settings worker, not
  the UI thread; only preference widgets are touched on the main thread.
- `RouterService` polls the bounded JNI status/log snapshot on a single worker
  executor and publishes immutable `RouterSnapshot`/`LogEntry` values on the
  main thread. The service owns the graceful-stop deadline on its main handler
  and enqueues the actual stop on that worker; `DaemonWrapper` publishes an
  explicit `stopping` state before entering native teardown. No Java timer
  thread calls JNI.
- `RouterRepository` exposes `LiveData`; `RouterViewModel` survives Activity
  recreation without restarting the daemon.
- Rotation/task recreation does not create a second daemon. The app auto-starts
  only while the service has never persisted a run state; an explicit stop (in
  the dashboard, the exit action or the Quick Settings tile) remains stopped
  across task recreation and relaunch until the user starts it again.
- The service uses an in-process `LocalBinder`. It is deliberately not
  exported. A future isolated router process would require AIDL plus
  Parcelable snapshots and a new security review; that is not claimed here.
  AIDL is not justified while the router, service and UI share one process:
  it would add parcel limits, death/rebind handling and a second ART process
  without creating a security boundary.
- Service observation is foreground-gated: the activity registers the
  repository listener in `onStart` and unregisters it in `onStop`, and hidden
  fragments are moved to `CREATED` with `setMaxLifecycle`. JNI status/log
  polling therefore stops when the UI is not visible instead of continuing in
  the background.
- The Android JNI CMake graph sets `WITH_BINARY=OFF`, `WITH_TORRENTS=ON` and
  `WITH_UPNP=ON`, builds the common daemon lifecycle plus `UPnP.cpp`, and does
  not link `i2pd_privsep`; `I2PD_NO_PRIVSEP` removes the POSIX supervisor path
  from the Android build. The Android service remains same-process; the C++
  supervisor/log worker and the planned network/storage/crypto workers are
  desktop/OpenBSD/Windows work, not an Android process boundary. `AND-013` and
  `PRIVSEP-012` remain open.
- Snapshots are deduplicated by value in `RouterRepository`; the traffic chart
  samples only while the router is running and only when uptime advances.
- The foreground service uses `specialUse` on API 34+ and `dataSync` only on
  API 29..33. Android 15 caps `dataSync` at six hours per 24 hours and blocks
  it from `BOOT_COMPLETED`; a continuous router must not use it. A timeout
  callback still shuts the daemon down cleanly if a platform ever applies one.
- The legacy manifest `CONNECTIVITY_CHANGE` receiver was removed. It was never
  delivered on the API 29+ baseline; `DaemonWrapper` registers a
  `registerDefaultNetworkCallback` only while the daemon is running.

## UI

- Material 3 DayNight theme with the fixed openi2pd palette (dynamic wallpaper
  colors are deliberately disabled to preserve the official identity),
  light/dark resources and adaptive `layout-w600dp` navigation rail.
- Dashboard shows a state-coloured status card, a single set of lifecycle
  controls, quick actions, live metrics and warnings. The state title uses
  `brand_success`/`brand_warning`/`brand_error` roles as a second cue beside
  text.
- Connections (the renamed network destination) shows connectivity, published
  IPv4/IPv6 addresses, NTCP2/SSU2 and client-service states. Unknown status is
  rendered as `—`, never as a fabricated disabled transport or zero counter.
- Tunnels shows live counts, configuration reload and a graceful-stop
  countdown with a determinate progress bar. The broken session-only
  accept-transit switch was removed; transit is controlled by the persisted
  `notransit` option in Settings.
- Statistics uses the 15-second rates, restores the chart sample cursor across
  rotation, resets stale history after a daemon restart and offers a dashed
  upload series in addition to the colour legend.
- Connections, Tunnels, Statistics, Logs, About and grouped Settings are native
  Views/XML; no Compose, Kotlin, WebView or browser assets.
- Accessibility: section headings, live regions only for discrete state
  changes (not the one-second graceful-stop countdown), 48dp touch targets,
  text scaling, tabular metric figures, non-colour-only state, confirmations for
  destructive actions and bounded lists/charts. Logs expose a live empty state
  and hide the empty RecyclerView from screen readers.
- Tablet/split-screen uses the `w600dp` window-width qualifier and a navigation
  rail; phones use a labelled bottom navigation with a minimum touch height.
  Fragment content is capped by `MaxWidthLinearLayout` (`content_max_width`,
  840dp) and centred instead of stretching cards across a wide panel. The
  toolbar carries the tinted openi2pd mark, screen title and live router-state
  subtitle; detail screens expose an Up affordance and hide duplicate actions.
- A global service banner appears when the bound service disconnects or does
  not bind, with a Retry action. Control capability is derived from the daemon
  lifecycle (`canStart`/`canStop`/`canRestart`/`canGraceful`), not from the
  presentation state alone, so a started router with a failed status snapshot
  remains stoppable.
- `UiFormat.setTextIfChanged` avoids redundant `setText`/re-layout work for
  unchanged fields in the one-second snapshot pipeline. Logs debounce search
  input for 250ms and use a DiffUtil-backed list.
- Dynamic wallpaper colors are disabled so the official openi2pd palette is
  stable in light and dark themes. Backup/device-transfer domains are explicitly
  excluded and Recents screenshots are suppressed.
- Logs use the existing `Log::GetRecent` ring via JNI; no keys or private data.
  The previous `logs_empty` `LinearLayout`/`TextView` mismatch is corrected;
  the container controls visibility and `logs_empty_text` holds the message.

## Visual identity

- The launcher icon is generated from the official vector master
  `assets/branding/symbol.svg`; no raster tracing or embedded bitmap is used in
  the vector layers.
- `mipmap-anydpi/ic_launcher.xml` is an adaptive icon with an opaque
  `@color/ic_launcher_background` (`openi2pd-white`) background and a 108dp
  `drawable/ic_launcher_foreground.xml` layer. The mark is scaled into a 68dp
  box centred in the 108dp layer, inside the 66dp foreground safe zone.
- `mipmap-anydpi-v33/ic_launcher.xml` adds the same adaptive icon with a
  `<monochrome>` layer for themed icons. The themed layer is the 108dp
  `drawable/ic_launcher_monochrome.xml`, using the same safe-zone geometry as
  the foreground.
- Legacy launcher bitmaps are generated from the same master at mdpi 48,
  hdpi 72, xhdpi 96, xxhdpi 144 and xxxhdpi 192 pixels. They are opaque
  8-bit sRGB PNGs on a white rounded plate; only the plate corners are
  transparent. The inherited generic `logo.*` mipmaps were removed.
- `drawable/ic_logo.xml` is now the full-colour symbol. The About screen shows
  it on a white rounded plate so the navy/white mark stays legible in both
  light and dark themes.
- The notification and Quick Settings icon is the single-colour
  `drawable/ic_logo_monochrome.xml`; the old `ic_notification_icon.xml` and the
  coloured Quick Settings tile icon were replaced.
- Android 12+ splash configuration lives in `values-v31/themes.xml` and uses
  only platform attributes (`windowSplashScreenBackground`,
  `windowSplashScreenAnimatedIcon`, `windowSplashScreenIconBackgroundColor`,
  `windowSplashScreenAnimationDuration`). No `core-splashscreen` or other
  dependency was added; API 29-30 keep the branded `windowBackground`.
- `values/colors.xml` and `values-night/colors.xml` now carry the semantic
  palette from `assets/branding/colors.md`: primary/on-primary/containers,
  secondary, tertiary, background/surface/on-surface, variants, outline,
  selected, focus, disabled, error, warning, success and chart colors.
  Surface-container roles map to existing palette tokens instead of new brand
  hex values.
- `themes.xml` wires the expanded Material 3 roles, control highlight/normal/
  activated colors and the branded window background. Dynamic wallpaper color
  is deliberately not enabled: the fixed `brand_*` palette is the sole source
  in both light and dark themes. The launcher icon, splash and notification
  icon are also not dynamically recoloured.
- The user-facing label is `openi2pd` (`app_name` and the About licence
  summary). The application ID `org.openi2pd.router`, Java packages, service
  actions, JNI symbol names, configuration/data paths and protocol names are
  unchanged.

## Interface language

- The Android UI ships English (`values/`), Spanish (`values-es/`), Russian
  (`values-ru/`) and Simplified Chinese (`values-zh-rCN/`); English is the
  source/fallback. Traditional Chinese is optional only with verified
  `zh-Hant` translations and must not reuse Simplified text.
- `res/xml/locales_config.xml` declares `en, es, ru, zh-CN`;
  `AndroidManifest.xml` references it, and
  `androidResources { localeFilters += ["en", "es", "ru", "zh"] }` keeps the
  resource split aligned. `values-zh` is intentionally not used because it
  would also match Traditional Chinese.
- Settings uses `AppCompatDelegate.setApplicationLocales` for per-app language;
  an empty locale list follows the system. It never calls
  `Locale.setDefault`, changes `Resources` configuration process-wide or
  mutates the device locale. `UiFormat` uses the active UI locale for
  user-visible numbers; log/token classification keeps `Locale.ROOT`.
- User data (file, torrent, destination and log text) is never translated.
  Native protocol/status tokens remain English, including inside translated
  wrapper sentences.
- `perl android/tools/check-android-locales.pl` is a compile-free guard for
  catalog key/placeholder parity, `locales_config`, locale filters, Fastlane
  allowlist and the override boundary. The manual Android job runs it before
  Gradle.

## Configuration and storage

- `RouterConfigStore` centralizes bounded INI read/write, atomic replacement,
  value validation and hot/restart classification. It caches the bounded file
  text between edits.
- Settings reads and writes that file on a single background executor and only
  updates preferences on the main thread. The autostart marker is also written
  off the main thread; only opening a vendor autostart Activity returns to the
  main thread.
- Existing `i2pd.conf` keys are preserved; no secrets are added to preferences.
- Autostart uses an app-private marker with cache migration.
- Application ID remains `org.openi2pd.router`; changing it remains
  non-updatable from the upstream app (migration tracked as REBRAND-009).

## Validation performed

- An earlier revision of the Java sources compiled with
  `:i2pd:compileDebugJavaWithJavac` using JDK 25, source/target 25,
  compileSdk 37. That was a real Gradle/javac run, not a claim about resource
  linking or APK packaging. The lifecycle/FGS/settings review edits made after
  that run have only been inspected statically; they need a fresh compile in
  CI.
- A historical standalone Java 25 probe passed javac/D8/R8 checks; the
  AGP-resolved D8/R8 path for the current app is still a CI obligation. See
  `docs/android-java25.md`.
- All Android XML resources parse with `xmllint`; no Python or ElementTree runtime check is required.
- The visual-identity pass added the brand icon, splash, palette and About
  logo. All `android/app/src/main/res/**/*.xml` files parse with `xmllint`
  after those edits; a local resource-reference scan found no missing
  `drawable`, `layout`, `menu`, `mipmap`, `xml`, `color`, `dimen`, `string`,
  `bool` or `id` references, and `git diff --check` was clean. These are static
  checks only: AAPT2 resource linking, PNG crunching, Lint and APK packaging
  were **not** run.
- Pure-JVM tests under `android/app/src/test/` cover the native parsers, state
  mapping/control enablement, INI value validation and immutable snapshot/log
  bounds. An instrumentation smoke test under `androidTest/` binds and unbinds
  the non-exported service without starting the daemon. Running
  `:i2pd:testDebugUnitTest` reached resource linking but AAPT2 timed out in this
  container, so no test result is claimed. CI should run both test tasks.
- JNI strings are decoded from ordinary UTF-8 to UTF-16 before `NewString`;
  data-directory access is mutex-protected and a native start failure is
  returned to Java. These are static changes; the malformed/4-byte/NUL cases
  and the native link are not device-tested.
- `assembleDebug`, Android Lint, JNI/NDK link, emulator/device lifecycle,
  notification/FGS behavior, rotation, TalkBack and visual comparison were
  **not** run here. Do not mark them validated.

## Pending

- CI: `:i2pd:assembleDebug`, `:i2pd:testDebugUnitTest`,
  `:i2pd:connectedDebugAndroidTest` (device/emulator; includes
  `RouterServiceLifecycleTest` and `MainActivityNavigationTest`) and
  `:i2pd:lintDebug`.
- AAPT2/APK: link the adaptive icon (`mipmap-anydpi`/`-v33`), `<monochrome>`
  layer and `values-v31` splash attributes; inspect the packaged resources and
  install the APK on API 29/30 and API 31+/33+ (adaptive/themed icon).
- Lint: run `IconLocation`, adaptive-icon, monochrome, splash-screen, NewApi
  and unused-resource checks; the palette intentionally defines tokens that
  current views do not yet consume.
- Device: verify launcher masks/crops, the Android 12+ splash on light and dark
  themes, themed icons, notification small
  icon, Quick Settings tile tint and the About logo in both themes.
- Device: FGS start/stop/restart, the `specialUse` type on API 34+,
  rotation, process death, notification permission, boot/autostart
  restrictions, TalkBack and font scaling. A Play-distributed build needs the
  Play Console `specialUse` declaration; F-Droid has no such declaration step.
- Device: second UI pass — Logs empty state and debounce, service banner/Retry,
  Connections navigation, lifecycle controls during `startFailed`/`stopping`,
  840dp tablet content width, graceful progress, dashed chart series and
  light/dark toolbar tint. `MainActivityNavigationTest` now opens Logs and
  asserts `logs_empty_text`; it is prepared, not executed.
- Confirm Java 25/D8/R8 behavior for the final APK and any AGP/Kotlin toolchain
  migration beyond AGP 10.
- Decide isolated-process/AIDL migration after the native worker separation.

## Multiprocess and SAF lifecycle continuation (static)

The router remains embedded and same-process with the Activity/UI. This preserves
`stopWithTask="false"` and the persisted `should_run` intent, so destroying an
Activity or rotating the screen does not stop router or torrent transfers.
`android:process` only provides crash/lifetime containment, not a new UID:
`INTERNET` remains per-UID and `isolatedProcess` cannot retain persisted SAF
grants. A true router/UI separation therefore needs a typed Binder/AIDL boundary
and later a companion-UID design; it is not claimed here.

SAF hardening in this increment: tree selection validates `isTreeUri` and real
read/write grants, releases a replaced persisted grant, exposes detach, and
persists/releases the export target grant. Close/release calls no longer retire
the broker on a client-side timeout; the broker queues descriptor close through a
reaper, cancels one operation without killing the process, verifies created
document names and truncates exports to zero before writing. JNI inbound strings
use UTF-16→UTF-8 decoding with NUL/surrogate rejection; start diagnostics are
bounded. The SAF broker still maps the full native library before applying its
seccomp filter; moving that helper to a minimal library and adding a session-scoped
AIDL contract remain pending.

## Manual Android CI matrix (prepared)

The only currently dispatched Android job builds `arm64-v8a` debug, runs JVM
unit tests and `lintDebug`, links the arm64 JNI target and assembles the debug
and instrumentation APKs. The following matrix is the manual target; cells are
not validated until the workflow or a device run passes.

| Axis | Values | Current coverage |
|---|---|---|
| Build type | debug, release (unsigned), AAB | debug was the only prepared build; release APK/lint release added to the manual job; AAB remains deferred because ABI splits and bundle packaging need an explicit policy |
| ABI | arm64-v8a, armeabi-v7a, x86_64, x86, universal | the default job builds arm64-v8a; the opt-in UI lane builds x86_64 for the emulator. armeabi-v7a/x86 and universal packaging are not built |
| API device | 29, 33, 36/37 | an opt-in `android_ui` dispatch input adds an API 29 x86_64 emulator lane running `connectedDebugAndroidTest`; no run exists yet |
| Java/D8/R8 | JDK 25 compiler, class-file 69, AGP-selected D8/R8, release R8 | class-file assertion added; AGP D8/R8 version remains unpinned and unproven |
| NDK/CMake | r30 `30.0.16248370`, CMake 3.31.6, C++23 JNI | arm64 configure/link only; 32-bit and x86 branches compile only in a future ABI lane |
| Resources | API 29 baseline, API 35+ edge-to-edge, API 37 target | unit/Lint only; no AAPT2/APK content assertion beyond the packaged `libi2pd.so` check |

Required manual lanes:

1. Toolchain: `./gradlew --version`, `javaToolchains`, `check-config.sh`,
   `check-android-locales.pl`; assert the exact JDK 25 compiler and class-file 69.
2. JVM/Lint: `testDebugUnitTest`, `lintDebug`, `lintRelease`; publish text/XML
   reports rather than relying on an intermediates path.
3. Native/package: arm64 JNI link, debug APK, release APK, packaged-library and
   symbol assertions; then each declared ABI in a bounded compile lane.
4. Device: `connectedDebugAndroidTest` on API 29 and a current API, including
   SAF provider faults, service lifecycle, broker death/rebind and negative
   seccomp checks. The `android-ui` job (dispatch input `android_ui`) prepares
   the API 29 x86_64 lane with sdkmanager/avdmanager, but has never run.

Known tool limitations must remain explicit: Java 25 source/class-file support
by the AGP-resolved D8/R8 is assumed until the CI class-file/DEX checks pass;
core-library desugaring is disabled, so JDK library APIs newer than the API 29
bootclasspath are unavailable; virtual threads, `ScopedValue`,
`StructuredTaskScope`, `SequencedCollection` and similar APIs are not available
on ART; R8 shrinking is disabled, so release R8 rules are not exercised; and
Lint warnings such as `UnusedResources`/`SetTextI18n` are not errors unless the
Lint policy is tightened.

## Native GUI continuation (2026-10-21, static)

The dashboard now exposes the native router status/error detail and a Reseed
action gated by connected/not-reseeding state. Settings can edit the listening
port through the existing validator and restart apply mode, and links to the app
notification settings with an enabled/disabled summary. Statistics shows both
instantaneous and 15 s rates plus the retained chart peak. Logs apply
minimum-severity filtering (Warn includes errors), wrap filter chips, announce
the summary and group each row into one screen-reader stop. I2PSnark has
validated Start all/Stop all/Reannounce all controls and shows the selected
session name; SAF selection/export labels use the active UI resource catalogue.
The address-book title survives recreation and the fragment container receives
an accessibility pane title.

Still open: per-tunnel inventory and live accept-transit control, SAF source
seeding/import-after-create, queue/bandwidth policy, notification live counters,
ViewModel-owned traffic history, process-wide daemon ownership, rotated
restart/apply banners, and the device/API instrumentation lane. `UiContractTest`
and the pure JVM filter/format/command tests are prepared but not executed.


## Native GUI Material 3 continuation (2026-10-26, static)

The router, configuration, connections (network), tunnels, statistics, logs and
I2PSnark screens were reviewed against the M3/adaptive/accessibility contract.
The following corrections are implemented by static analysis only:

- **Programmatic rows.** Torrents, address book and runtime administration
  build their rows in Java. `UiFactory` now owns the shared contract: 48dp
  minimum touch target, logical padding, a centred `MaxWidthLinearLayout`
  column (`content_max_width`, 840dp phone content / 1000dp at `w600dp`) and a
  one-utterance content/state description. Previously those screens used a
  hard-coded 16dp inset, no width cap and TalkBack read each line separately.
- **Typed I2PSnark row.** `TorrentRow` is a bounded record decoder for the
  `TORRENTS` snapshot (id/name/status/error/hash/magnet/counters/percent/ETA).
  The fragment no longer concatenates raw map lookups, a malformed hash or
  magnet is dropped, counters are clamped, an invalid id is rejected and the
  daemon's fractional `std::to_string(double)` percentage is parsed as a bounded
  double and rounded (integer parsing silently returned 0 for every row). The
  actions dialog is titled with the torrent name so the 14 verbs cannot be
  applied to the wrong row, and the private badge and error join the TalkBack
  description. `TorrentRowTest` pins the decoder on the JVM.
- **SAF authority runs off the UI thread.** Selecting or detaching a payload
  tree commits the grant map and may round-trip to the provider. Both now run
  on the service's SAF worker (`RouterService.selectPayloadTree` /
  `detachPayload`), report a typed boolean on the main thread and are ignored
  after the fragment view is gone. The former `SharedPreferences.commit()` and
  provider calls ran directly in the picker/row callback.
- **Rotation and process restart.** `TorrentsFragment` restores the selected
  session, import tunnel and SAF grant identity from instance state; it no
  longer reopens the session picker on every rotation and cannot render one
  tunnel's rows under another tunnel's name.
- **Accessibility.** Address-book rows announce name, destination and the
  blindable marker (the former hard-coded `(blinded)` string is now a
  translated `address_book_blinded` resource). The graceful-stop countdown is
  no longer a live region, so TalkBack does not announce it every second.
- **Notification.** The foreground notification offers Graceful stop while the
  router runs and Cancel graceful stop while it drains; both use the existing
  typed service actions. It is `setOnlyAlertOnce` and immediate, so a
  one-second status update never re-alerts.
- **Navigation.** Secondary entries (logs, about, I2PSnark, address book,
  runtime administration) are hidden while a detail screen is open, and
  `showDestination` hides every known fragment tag. Opening a second detail
  would silently replace the first and lose the back target.
- **Theme.** `Theme.Material3.DayNight.NoActionBar` keeps the fixed openi2pd
  role table; `values-night/colors.xml` defines every light token, and
  `forceDarkAllowed=false` prevents the platform from re-darkening the branded
  surfaces. `UiContractTest` now asserts that the resolved `colorSurface`
  equals `brand_surface` in both modes and that the night surface differs.

Still open: per-tunnel inventory and live accept-transit control, a config
search/editor beyond the validated preference subset, a two-pane tablet detail
layout for logs/torrents, queue/bandwidth policy, notification live counters,
ViewModel-owned traffic history and a process-wide daemon owner. The API 29
emulator lane is prepared but has never executed; instrumentation, TalkBack and
visual results remain manual evidence.


## Internationalization completion (2026-10-26, static)

Android ships `values`, `values-es`, `values-ru` and `values-zh-rCN` with exact
key parity. Counts use `<plurals>` (`logs_summary`, `torrent_count`,
`runtime_admin_reseed_status`) and `getQuantityString`; Russian supplies
one/few/many/other and Simplified Chinese one/other. Placeholders are
positional, and `android/tools/check-android-locales.pl` now validates plural
arity, per-quantity placeholders, non-positional conversions and references
from `src/test`/`src/androidTest` as well as `src/main`.

User-visible validation errors carry resource ids through
`TorrentControl.UserMessageException`; `torrent_creation_failed_unknown`,
`torrent_create_source_invalid` and `torrent_operation_failed_generic` replace
hardcoded English. The log row uses `log_row_description`, the foreground
notification channel has a translated name and description, the tracker label
is no longer the English token, and Russian uses `роутер`/`пересев`
consistently. `LocaleResourceTest` asserts that `zh-TW`/`zh-Hant`/`zh-HK` fall
back to English and that Russian selects distinct one/few/many forms.

No AAPT2, Gradle, Lint or device run exists for these changes.

## See also

- [android java25](android-java25.md)
- [android jni status](android-jni-status.md)
- [saf storage](torrent/saf-storage.md)
- [process capabilities](security/process-capabilities.md)

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
| minSdk | 24 | `getifaddrs` and existing support |
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
- The Android JNI CMake graph sets `WITH_BINARY=OFF`, `WITH_TORRENTS=OFF` and
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
  delivered on the API 24+ baseline; `DaemonWrapper` registers a
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
- `mipmap-anydpi-v26/ic_launcher.xml` is an adaptive icon with an opaque
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
  dependency was added; API 24-30 keep the branded `windowBackground`.
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

- The Android UI is English-only, matching the desktop policy. All 16
  `values-<language>` resource directories and non-English Fastlane listings
  were removed; `values/` is the sole string catalog.
- `res/xml/locales_config.xml` declares only `en`, `AndroidManifest.xml`
  references it, and `androidResources { localeFilters += ["en"] }` keeps
  non-English resource splits out of packaged APKs.
- The Java `appLocale`/`JNI setLanguage` path and the inert Android native
  language plumbing were removed. `UiFormat` uses English number formatting;
  log case-folding uses `Locale.ROOT`.
- Unicode input and persisted data are unchanged. The English-only policy
  applies to UI resources, not to file, torrent, destination or user text.
- `perl android/tools/check-english-only.pl` is a compile-free CI guard for
  language directories, locale metadata, Fastlane listings and locale
  overrides. It is run before Gradle in the manual Android job.

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
- The Java 25 probe passed javac/D8/R8 checks; see `docs/android-java25.md`.
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
- AAPT2/APK: link the new adaptive icon (`mipmap-anydpi-v26`/`-v33`), legacy
  PNG mipmaps, `<monochrome>` layer and `values-v31` splash attributes; inspect
  the packaged resources and install the APK on API 24/25 (legacy bitmap) and
  API 26+/33+ (adaptive/themed icon).
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

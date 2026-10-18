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
| NDK | 27.2.12479018 (overridable) | C++23 JNI build |
| Runtime | ART | executes DEX; no OpenJDK/V8 in APK |

See `docs/android-java25.md` for the `javac`/D8/R8 probe, class-file/DEX
evidence, AGP/Kotlin target limitation and Java 25 APIs unavailable on Android.

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
  main thread.
- `RouterRepository` exposes `LiveData`; `RouterViewModel` survives Activity
  recreation without restarting the daemon.
- Rotation/task recreation does not create a second daemon. The app auto-starts
  only while the service has never persisted a run state; an explicit stop (in
  the dashboard, the exit action or the Quick Settings tile) remains stopped
  across task recreation and relaunch until the user starts it again.
- The service uses an in-process `LocalBinder`. It is deliberately not
  exported. A future isolated router process would require AIDL plus
  Parcelable snapshots and a new security review; that is not claimed here.
- The foreground service uses `specialUse` on API 34+ and `dataSync` only on
  API 29..33. Android 15 caps `dataSync` at six hours per 24 hours and blocks
  it from `BOOT_COMPLETED`; a continuous router must not use it. A timeout
  callback still shuts the daemon down cleanly if a platform ever applies one.
- The legacy manifest `CONNECTIVITY_CHANGE` receiver was removed. It was never
  delivered on the API 24+ baseline; `DaemonWrapper` registers a
  `registerDefaultNetworkCallback` only while the daemon is running.

## UI

- Material 3 DayNight theme, dynamic colors where available, light/dark
  resources and adaptive `layout-sw600dp`/`layout-land` navigation.
- Dashboard shows state, controls, uptime, connectivity, addresses, NTCP2/SSU2,
  rates, peers, tunnels, reseeding and warnings.
- Tunnels shows counts, configuration reload and graceful stop. The broken
  session-only accept-transit switch was removed; transit is controlled by the
  persisted `notransit` option in Settings.
- Network, Tunnels, Statistics, Logs, About and grouped Settings are native
  Views/XML; no Compose, Kotlin, WebView or browser assets.
- Accessibility: content descriptions, 48dp touch targets, text scaling,
  non-color-only state, confirmations for destructive actions and bounded
  lists/charts.
- Logs use the existing `Log::GetRecent` ring via JNI; no keys or private data.

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
  activated colors and the branded window background. Dynamic color remains
  enabled in `MainActivity`; when it is unavailable, the branded `brand_*`
  resources are the complete fallback. The launcher icon, splash and
  notification icon are deliberately not dynamically recoloured.
- The user-facing label is `openi2pd` (`app_name` and the About licence
  summary). The application ID `org.openi2pd.router`, Java packages, service
  actions, JNI symbol names, configuration/data paths and protocol names are
  unchanged.

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
- All Android XML resources parse with `xmllint`/Python `ElementTree`.
- The visual-identity pass added the brand icon, splash, palette and About
  logo. All `android/app/src/main/res/**/*.xml` files parse with `xmllint`
  after those edits; a local resource-reference scan found no missing
  `drawable`, `layout`, `menu`, `mipmap`, `xml`, `color`, `dimen`, `string`,
  `bool` or `id` references, and `git diff --check` was clean. These are static
  checks only: AAPT2 resource linking, PNG crunching, Lint and APK packaging
  were **not** run.
- Pure-JVM tests under `android/app/src/test/` cover the native parsers, state
  mapping/control enablement, INI value validation and immutable snapshot/log
  bounds. Running `:i2pd:testDebugUnitTest` reached resource linking but AAPT2
  timed out in this container, so no test result is claimed. CI should run it.
- `assembleDebug`, Android Lint, JNI/NDK link, emulator/device lifecycle,
  notification/FGS behavior, rotation, TalkBack and visual comparison were
  **not** run here. Do not mark them validated.

## Pending

- CI: `:i2pd:assembleDebug`, `:i2pd:testDebugUnitTest`, `:i2pd:lintDebug`.
- AAPT2/APK: link the new adaptive icon (`mipmap-anydpi-v26`/`-v33`), legacy
  PNG mipmaps, `<monochrome>` layer and `values-v31` splash attributes; inspect
  the packaged resources and install the APK on API 24/25 (legacy bitmap) and
  API 26+/33+ (adaptive/themed icon).
- Lint: run `IconLocation`, adaptive-icon, monochrome, splash-screen, NewApi
  and unused-resource checks; the palette intentionally defines tokens that
  current views do not yet consume.
- Device: verify launcher masks/crops, the Android 12+ splash on light and dark
  themes, themed icons, dynamic colour on/off fallback, notification small
  icon, Quick Settings tile tint and the About logo in both themes.
- Device: FGS start/stop/restart, the `specialUse` type on API 34+,
  rotation, process death, notification permission, boot/autostart
  restrictions, TalkBack and font scaling. A Play-distributed build needs the
  Play Console `specialUse` declaration; F-Droid has no such declaration step.
- Confirm Java 25/D8/R8 behavior for the final APK and any AGP/Kotlin toolchain
  migration beyond AGP 10.
- Decide isolated-process/AIDL migration after the native worker separation.

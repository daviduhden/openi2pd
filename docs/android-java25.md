<!--
See the LICENSE file at the top of the project tree for copyright
and license details.

openi2pd: android java25.

Documents project documentation.

-->

# Java 25 on Android (openi2pd)

Status: toolchain verification, 2026-10-19. This document covers the Android
build toolchain and Java 25 language/class-file compatibility only. It does
**not** claim that the application APK builds, that Lint passes, or that any
device/emulator test ran.

The Android app is Java source compiled by JDK 25 into class-file version 69,
then converted by D8/R8 into DEX for Android's ART runtime. No OpenJDK is
packaged in the APK and Android does not execute OpenJDK bytecode.

## Toolchain matrix

| Layer | Selected | Evidence |
|---|---|---|
| Gradle launcher JVM | OpenJDK/Temurin **25.0.4.1+1-LTS** | `./gradlew --version` with `JAVA_HOME=/path/to/jdk25`; the wrapper also starts on JDK 27 |
| Gradle | **9.8.1** | wrapper properties pin the distribution and SHA-256; `./gradlew --version` |
| Java compiler toolchain | **exact JDK 25** | `./gradlew javaToolchains` selects Temurin 25 from `JAVA_HOME` |
| Java source level | **25** | `compileDebugJavaWithJavac` task: `source=25` |
| Java target / class-file | **25 / major 69** | task: `target=25`; `javap` reports major 69 |
| Android Gradle Plugin | **9.4.1** | `android/build.gradle` |
| D8/R8 (verification) | **9.4.28** | `R8 9.4.28` from the supplied `r8.jar` |
| Kotlin | **disabled** (AGP bundles Kotlin 2.4.10) | `android.builtInKotlin=false`; see the AGP investigation below |
| compileSdk / targetSdk | **37** | unchanged in `android/app/build.gradle` |
| SDK Build Tools | **36.0.0** | AGP 9.4 default; CI installs it explicitly |
| minSdk | **29** | Android 10 minimum; raised from 24 |
| NDK | **30.0.16248370** (overridable) | r30 LTS; C++23 JNI build |
| Device runtime | ART, API 29+ | no JVM/JDK in the APK |
| Core library desugaring | not enabled | not needed for the language features used |

The root build script (`android/build.gradle`) still rejects a Gradle JVM below
25. The Java toolchain is exact 25; the Gradle JVM only has to be 25 or newer.
JDK auto-download remains disabled in `android/gradle.properties`, so JDK 25
must be installed locally.

AGP 9.4 is the current stable line and supports at most API level 37; it
requires Gradle 9.6.0 or newer and defaults to SDK Build Tools 36.0.0. This
tree pins Gradle 9.8.1 and uses the SDK 37 platform, so the supported
combination is explicit. AGP's default NDK is 28.2.13676358, but this project
uses the current r30 LTS (`30.0.16248370`) for its C++23 toolchain; that is a
deliberate override, not an AGP default. AGP 9.5 is a preview and is not used.

The versions in this document are pin-level statements, not evidence that the
current APK was built. The last observed `compileDebugJavaWithJavac` success
and the synthetic D8/R8 probe predate the latest lifecycle edits. CI must
re-run the actual app compile, resource link, D8/R8, Lint and device tests.

## Source level, target level and ART

- `sourceCompatibility = JavaVersion.VERSION_25` selects the Java 25 language
  rules (records, sealed types, pattern matching, unnamed patterns, flexible
  constructor bodies, and the other final Java 25 language changes).
- `targetCompatibility = JavaVersion.VERSION_25` makes `javac` emit class-file
  major version 69.
- The Java toolchain compiles with an exact JDK 25 compiler.
- D8/R8 reads class-file 69 and emits DEX. The recorded DEX version from the
  earlier `--min-api 24` probe was **037**; rerun at the current `--min-api 29`
  is pending.
- ART executes DEX, not class files. Source/target 25 neither raises `minSdk`
  nor puts a JDK into the APK.
- Language features are a compile-time property. D8/R8 desugars records,
  pattern switches and similar constructs; the application still runs on ART.
- Library APIs are a separate property. D8/R8 does not add `java.*`
  implementations. Those come from Android's API 37 bootclasspath, and
  missing JDK APIs remain missing at runtime.

### Explicit layer limitations

| Layer | Checked fact | Not claimed |
|---|---|---|
| `javac` | The standalone Java 25 probe builds with `--release 25` and reports class-file major 69. AGP is configured for source/target 25 with an exact JDK 25 toolchain. | The current app source has not been recompiled after the latest lifecycle edits in this environment. |
| D8/R8 | The probe's class-file 69 output was accepted by D8/R8 9.4.28 at the historical `--min-api 24`, producing DEX 037. | The real app's debug/release DEX, R8 shrinking, resource linking and APK packaging are not validated; rerun at 29 is pending. |
| ART | ART executes DEX; records/pattern switches are desugared for the configured minSdk (29 in the current build). Java 25-only library APIs are absent from API 37 `android.jar`. | No emulator/device run proves every Java 25 construct or API on API 29 or current Android. |

These are separate claims. A successful `javac` or synthetic D8/R8 probe does
not imply that the openi2pd APK, minified release or ART runtime is validated.

## Build configuration changes

`android/app/build.gradle`:

- `compileOptions` now sets **both** `sourceCompatibility` and
  `targetCompatibility` to `JavaVersion.VERSION_25`. The earlier 17 setting was
  removed.
- The `java { toolchain { languageVersion = JavaLanguageVersion.of(25) } }`
  block is kept, so compilation uses JDK 25 rather than the Gradle JVM.
- Stable pinned dependencies were added for the UI work:
  `androidx.core:core:1.19.1`, `androidx.appcompat:appcompat:1.8.0`,
  `androidx.activity:activity:1.13.0`, `androidx.fragment:fragment:1.9.1`,
  `androidx.lifecycle:lifecycle-viewmodel:2.11.0`,
  `androidx.lifecycle:lifecycle-livedata:2.11.0`,
  `androidx.preference:preference:1.2.1`,
  `androidx.recyclerview:recyclerview:1.4.0`,
  `com.google.android.material:material:1.14.0`.
  Unused `constraintlayout`, `swiperefreshlayout` and
  `commons-configuration2` entries were removed; the UI uses plain
  `LinearLayout`/`RecyclerView` views and the line-based `RouterConfigStore`,
  not an INI object model.
- Prepared, not auto-run, test dependencies were added:
  JUnit `4.13.2`, `androidx.test:core:1.7.0`,
  `androidx.test:runner:1.7.0`, `androidx.test:rules:1.7.0`,
  `androidx.test.ext:junit:1.3.0`,
  `androidx.test.espresso:espresso-core:3.7.0`.
  `testInstrumentationRunner` is set. Pure-JVM tests for the status/log
  parsers, state/control presentation, INI value validation and immutable
  model bounds live under `android/app/src/test/`; they are not run
  automatically and their execution is deferred to CI.
- `compileSdk`/`targetSdk` remain 37 and `minSdk` is 29 (Android 10). The NDK pin is
  updated to r30 LTS (`30.0.16248370`) for the current C++23 Clang toolchain;
  the CMake graph remains the same. ABI splits, signing policy, release
  minification and Lint settings were not changed. No JDK is bundled and core
  library desugaring is not enabled.

`android/gradle.properties`:

- Documents the JDK 25 requirement and keeps JDK auto-download disabled.
- Sets `android.builtInKotlin=false` because AGP 9.4.1 otherwise creates an
  unused Kotlin compile task that rejects JVM target 25. This is a workaround
  for a Java-only application, not a reduction of the Java language level.

The Gradle wrapper (`9.8.1` plus SHA-256) and the AGP classpath (`9.4.1`) were
left unchanged. CI remains manual-only.

The dependency coordinates above were checked against Google Maven and Maven
Central; every POM returned HTTP 200. `:i2pd:dependencies` resolved the
`debugCompileClasspath` without a failure.

## Visual identity resources

The branding pass did not change the Java 25/Gradle toolchain or add a
dependency. It added source resources only:

- adaptive launcher icon (`mipmap-anydpi/ic_launcher.xml`, plus a v33
  `<monochrome>` variant), a 108dp vector foreground generated from
  `assets/branding/symbol.svg`, and `values-v31/themes.xml` for the platform
  Android 12+ splash;
- semantic light/dark palettes in `values/colors.xml` and
  `values-night/colors.xml`, wired into the existing Material 3 theme;
- a full-colour About mark and a single-colour notification/Quick Settings
  mark; the manifest label is now `openi2pd` while the application ID and Java
  packages are unchanged.

These are XML/PNG resources, not Java APIs. AAPT2 resource linking, APK
packaging, Lint, PNG crunching and device rendering were not run here; see the
pending list below.

## Java 25 probe and verifier

`android/tools/java25-probe/check-config.sh` is a compile-free guard for CI. It
verifies Java 25 source/target and toolchain pins, compile/target SDK 37,
minSdk 29, NDK/AGP/Gradle pins, disabled JDK auto-download, the AGP
bundled-Kotlin workaround and the certificates asset target. It also rejects
known JDK-only APIs (`java.lang.foreign`, `java.lang.invoke`,
`java.lang.classfile`, `ScopedValue`, `StableValue`, `java.util.Sequenced`,
`javax.crypto.KDF`, PEM encoder/decoder, virtual-thread/structured-concurrency
APIs and `jdk.incubator.vector`) in app source. It is not a substitute for
javac, D8/R8, Lint or device testing.

The probe is in `android/tools/java25-probe/`. It is not part of the app and is
not added to the APK. It exercises:

- records (`Started`, `Stopped`);
- a sealed interface (`Event permits Started, Stopped`);
- record deconstruction patterns and an exhaustive pattern switch;
- a guarded pattern switch label (`when at > 0`);
- unnamed patterns (`case Started(var name, _)`);
- flexible constructor bodies with validation statements before `super(...)`.

All features are final in Java 25. No preview flag is used.

Exact command:

```sh
JAVA_HOME=/path/to/jdk25 \
R8_JAR=/path/to/r8.jar \
ANDROID_JAR=/path/to/android-sdk/platforms/android-37.0/android.jar \
./android/tools/java25-probe/verify-java25.sh
```

Observed result with JDK 25. The transcript below was recorded before the
Android 10 bump and still shows `--min-api 24`; it is historical evidence and
must be rerun at `--min-api 29` before being treated as current validation.

```text
javac: javac 25.0.4.1
java:  openjdk version "25.0.4.1" 2026-08-18 LTS

== javac --release 25 ==
Java25Probe.class major version: 69

== run on the build JDK (not ART) ==
[started router, started router, stopped shutdown]
validated=25

== D8 --min-api 24 ==
.../dex/classes.dex  7528 bytes

== R8 --min-api 24 ==
.../r8/classes.dex   6880 bytes

== DEX magic ==
   d   e   x  \n   0   3   7  \0
   d   e   x  \n   0   3   7  \0
```

The same verifier was run with the host JDK 27 as the build JDK (`javac 27`,
`--release 25`). It also produced major 69, D8/R8 `classes.dex` files of 7528
and 6880 bytes, and DEX magic `dex\n037\0`. This shows D8/R8 9.4.28 accepts
class-file 69 and desugars these features at `--min-api 24`.

The probe was also used in a **synthetic** Android project outside the
repository (application id `org.openi2pd.java25probe`, no native code, no
AndroidX) with Gradle 9.8.1, AGP 9.4.1 and the JDK 25 toolchain:

```sh
JAVA_HOME=/path/to/jdk25 \
ANDROID_HOME=/path/to/android-sdk \
./android/gradlew -p /path/to/agp-probe :app:assembleDebug
```

That synthetic build was `BUILD SUCCESSFUL`. The APK reports
`minSdkVersion:'24'`, `targetSdkVersion:'37'`, `compileSdkVersion='37'`; its
class files are major 69; its DEX files are version 037; and dexdump shows
D8-generated synthetic record and type-switch classes. This is evidence for
the toolchain, **not** for the openi2pd app APK.

## AGP 9.4.1 / Gradle 9.8.1 / JDK 25 investigation

With `compileOptions` at 25 and AGP's built-in Kotlin enabled, even the task
graph fails before compilation:

```text
Execution failed for task ':i2pd:tasks' (registered by plugin 'org.gradle.help-tasks').
> Could not create task ':i2pd:compileDebugKotlin'.
   > Unknown Kotlin JVM target: 25,
     available targets are 1.8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24
```

AGP 9.4.1 bundles Kotlin 2.4.10, whose highest JVM target is 24. It creates a
Kotlin compile task even for a Java-only app and derives its target from the
Java target. Setting `android.builtInKotlin=false` makes the same task graph
succeed:

```sh
JAVA_HOME=/path/to/jdk25 ANDROID_HOME=/path/to/android-sdk \
  ./gradlew :i2pd:tasks
# BUILD SUCCESSFUL
```

The option is itself deprecated: AGP warns that `android.builtInKotlin=false`
will be removed in AGP 10.0. This is an unresolved forward-compatibility risk
until the bundled Kotlin compiler supports JVM target 25 (or an equivalent
supported configuration exists). The Java level was not lowered to make the
warning go away.

AGP accepts the Java 25 source/target. A temporary init script printed the
resolved compile task configuration:

```text
COMPILE_OPTIONS source=25 target=25 release=null args=[-XDstringConcat=inline]
BOOTSTRAP=null
CLASSPATH_HAS_ANDROID=true
```

`./gradlew javaToolchains` selected `Eclipse Temurin JDK 25 (25.0.4.1+1-LTS)`
at `/path/to/jdk25`.

An earlier revision of the app's Java compiled after the UI/architecture
source and resource identifiers were integrated:

```sh
JAVA_HOME=/path/to/jdk25 ANDROID_HOME=/path/to/android-sdk \
  ./gradlew :i2pd:compileDebugJavaWithJavac
# BUILD SUCCESSFUL
```

The earlier failure was source/resource integration (missing `BuildConfig`,
removed activity/menu IDs and non-constant `R.id.*` switch labels), not an
invalid Java release. Subsequent review edits (foreground-service type,
lifecycle fixes, asynchronous settings loading and the added tests) were not
compiled locally; CI must re-run `compileDebugJavaWithJavac`.
`assembleDebug`, resource linking, Lint and device tests were not completed
before the user froze local builds; CI owns them.

## Java 25 APIs unavailable on Android

These were checked directly against the supplied API 37 `android.jar`; each
class is absent:

| API | JDK status | API 37 android.jar |
|---|---|---|
| `java.lang.ScopedValue` | final in 25 | absent |
| `java.lang.StableValue` | preview in 25 | absent |
| `javax.crypto.KDF` | final in 25 | absent |
| `java.security.PEMEncoder` / `PEMDecoder` | final in 25 | absent |
| `java.lang.classfile` (Class-File API) | final in 24 | absent |
| `java.util.concurrent.StructuredTaskScope` | preview in 25 | absent |
| `java.lang.foreign` (FFM) | final in 22 | absent |
| `jdk.incubator.vector` (Vector API) | incubator in 25 | absent |

`java.lang.Record` **is** present in `android.jar`, so records can compile and
run. Android's `java.*` set is a subset of the JDK; D8/R8 does not supply the
missing classes. Core library desugaring was not enabled because it covers a
limited API set and does not make Java 25 platform APIs available.

Avoid all JDK-only APIs in app code. Note that the Gradle compile classpath
contains both `android.jar` and the JDK 25 platform classes (`BOOTSTRAP=null`,
`android.jar` on the classpath). `javac` may therefore resolve a JDK 25 API
that will be missing on ART. Source review and a bytecode/API scan are needed;
Android Lint alone is not a complete check for JDK-only APIs.

## Pending dynamic validation

- Run `:i2pd:assembleDebug`, resource linking and `:i2pd:lintDebug` in CI.
- AAPT2: link the adaptive icon, `<monochrome>` layer,
  `values-v31` splash attributes and palette resources; verify the packaged
  resource table and APK contents. This was not run locally.
- Lint: icon location/adaptive-icon/splash/NewApi checks and unused-resource
  warnings for the semantic tokens that views do not consume yet.
- Device: launcher mask/crop, Android 12+ splash in light/dark, themed icon,
  notification small icon, Quick Settings tint and the
  About mark on API 29 and current API levels.
- Exercise AGP's own D8/R8 on the real app (debug and minified release) and
  verify the resulting DEX at `minSdk 29`.
- Run the prepared pure-JVM tests and the added
  `RouterServiceLifecycleTest` instrumentation smoke test in CI.
- Build the native JNI library (NDK/CMake dependency set not exercised here).
- Test the JNI UTF-8-to-UTF-16 conversion with embedded NUL, supplementary
  characters, malformed sequences and a bounded large log snapshot. The
  conversion is implemented statically; no on-device test has run.
- Verify runtime behavior on API 29 and a current API level, including records
  and pattern switches.
- Re-check AGP 10, where `android.builtInKotlin=false` is scheduled for
  removal, and the Kotlin toolchain's JVM target support.
- Scan the app bytecode for references to the unavailable JDK APIs above.

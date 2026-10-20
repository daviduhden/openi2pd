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
- Android SDK: platform 37, Build Tools 36.0.0, minSdk 29 (Android 10),
  CMake, NDK r30 LTS (`30.0.16248370`) or newer
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

### SAF source metainfo creation

The torrent panel can select one document or a document tree, hash it
incrementally on the service's SAF executor, construct v1 metainfo through
native `CreateV1`, and export it with `CreateDocument`. The form supports
bounded piece sizes, explicit I2P trackers and private metadata. Operations
have cancellation/progress and retain generated bytes across screen rotation.
Sources stay URI-based; no filesystem path is manufactured from a provider URI.

This creates/export metainfo only: source seeding and downloaded payloads in
SAF trees still require a persistent descriptor/URI storage backend. Provider
revocation, providers ignoring cancellation and process-death recovery remain
pending device validation/implementation. `TorrentMetadataJniTest` is prepared;
manual CI builds its instrumentation APK, while device execution remains open.

### SAF payload broker (static implementation, manual CI pending)

The torrent action **Download / seed document tree** persists the exact tree
permission keyed by tunnel name and v1 infohash. Restart the router after
selection: an existing backend keeps its original tree capability until it is
closed. Selecting a new tree never retargets open files. Payload paths remain
root-relative document names, never URI-to-filesystem conversions. The existing
C++ piece verifier and scheduler use the same storage interface for native and
SAF IO; startup verifies available pieces and retains native state/priorities.

`TorrentSafStorageService` runs in `:torrent_storage` behind non-exported typed
AIDL. It owns descriptors and returns opaque random handles. Transfers are
exact-length, at most 16 KiB; names, offsets, file counts and directory scans are
bounded. The broker creates directories/documents through `DocumentsContract`,
uses positioned reads/writes, checks persisted authority and document existence,
and acknowledges writes after `fsync`. Pipes, nonregular descriptors, unsupported
seek/resize, ambiguous names and unexpected lengths fail closed. There is no
whole-torrent staging fallback. Providers without these operations cannot serve
as payload storage; the UI must select another provider. Same-size external
changes remain detectable by torrent piece hashes, not by a trusted provider
snapshot.

Before exposing Binder, the broker applies a native TSYNC seccomp filter denying
external socket operations, ptrace, io_uring and pidfd descriptor recovery, and
closes inherited IPv4/IPv6 sockets. Unsupported isolation disables the backend.
Document providers themselves remain outside this boundary and may use their own
network access. Android's app UID and URI grants are shared across app processes;
this increment does not isolate router persistence or claim global separation.

A five-second client deadline and one active/one queued Binder call bound stalls.
An unresponsive provider may ignore cancellation. Timeout or broker death poisons
this application's storage connection permanently: restart the whole application
before resuming; stale handles are never replayed against a new broker. Stopping
native file users releases capabilities; service unbind/destruction closes every
remaining descriptor. No automatic worker-restart or unbounded retry is attempted.
Native IO failure propagates to the torrent engine; permission revocation,
storage exhaustion and provider errors cannot fall back to native payload paths.

Prepared instrumentation covers path traversal and transfer-range overflow.
Provider/device regressions for partial IO, revocation/regrant, nonseekable
providers, deleted documents, cross-file pieces, storage exhaustion, broker death,
seccomp denial and lifecycle restoration are still pending manual CI execution
and further provider fixtures. No Android build or test was executed locally.

SAF recovery now preserves local logical root/file handles across Binder process
generations and reauthorizes them lazily with immutable URI/path authority. A
blocked provider is cancelled through an independent oneway operation and may
force broker retirement; actual old-process death fences replacement. Writes
with uncertain results fail rather than being replayed. Rebind/backoff and
thread/queue bounds are implemented statically. Android cancellation and fault
provider instrumentation is prepared, with actual cross-process persisted-grant
and death/rebind validation still pending manual CI/device execution.

SAF metainfo export now uses the bounded separate-process storage broker as well:
only an explicitly granted output document is opened, metadata remains capped at
8 MiB, and writes are sent in 16 KiB chunks. Cancellation/deadline retires that
broker rather than blocking the router service on a provider. An interrupted
export is reported as failed and may leave a partial output document for the
user to remove; ambiguous writes are never replayed. Exact payload deletion
uses a registered read-only document capability, verifies grant, regular type,
length and freshly opened device/inode identity, and never recurses into a tree.
Providers without stable identities fail deletion safely. Storage selection
displays reconnecting, retirement and revoked-permission status without exposing
provider URIs. These changes and device regressions remain pending manual CI.

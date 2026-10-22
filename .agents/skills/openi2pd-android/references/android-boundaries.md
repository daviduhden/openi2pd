<!--
See the LICENSE file at the top of the project tree for copyright
and license details.

openi2pd Android service, JNI and SAF checklist.
-->
# Android service, JNI and SAF checklist

Use with `SKILL.md`. This is a static boundary map.

## Layers

| Layer | Files | Boundary |
|---|---|---|
| Activity/Fragments | `MainActivity`, `*Fragment`, layouts/resources | observe immutable state; no daemon/JNI ownership |
| ViewModel/repository | `RouterViewModel`, `RouterRepository`, `RouterSnapshot`, `LogEntry` | LiveData; deduplicated values |
| Service | `RouterService`, `DaemonWrapper`, `ServiceStates` | owns native lifecycle, non-exported, same process |
| JNI | `I2PD_JNI`, `i2pd_android.cpp`, `DaemonAndroid.*` | worker-thread calls, bounded strings/arrays |
| Config | `RouterConfigStore`, preferences XML | line-based validation; no UI-thread disk I/O |
| Torrent control | `TorrentControl`, `TorrentsFragment` | typed commands/snapshots via bounded JNI |
| SAF broker | `TorrentSafStorage*`, `TorrentSafCreator`, AIDL, JNI | separate non-exported process, URI authority |
| Build | `app/build.gradle`, `cpp/CMakeLists.txt`, vcpkg triplet | minSdk 29, Java 25, NDK r30, no privsep in JNI graph |

## Service/lifecycle rules

- Rotation/task recreation must not create a second daemon.
- An explicit stop remains stopped across recreation until the user starts it.
- JNI/status/log polling runs on one worker executor, not the main thread.
- The service owns graceful-stop deadline; publish `stopping` before native
  teardown.
- Foreground service type: `specialUse` on API 34+, `dataSync` only on API
  29–33; no continuous `dataSync` misuse.
- Register network callbacks only while running; remove legacy broadcast paths.

## SAF rules

1. Resolve and persist URI/tree grants; key by tunnel + infohash.
2. Use exact document/tree authority; preserve the URI authority string.
3. Never convert a URI to a filesystem path or copy the whole tree.
4. Use exact bounded positional reads/writes; reject providers that deny seek
   or required write behavior.
5. Rehash existing provider pieces before serving/seeding.
6. Withhold unflushed pieces from HAVE/bitfield/serving/promotion.
7. On Binder death/timeout, retire the generation and rebind; stale callbacks
   cannot authorize a new writer.
8. Cancellation is independent of I/O locks; do not auto-replay ambiguous
   writes.
9. Deletion is exact document or selected tree authority; close cached
   descriptors and verify identity/size; no recursive deletion.

## Test map

| Test | Lane |
|---|---|
| `PresentationModelTest`, `UiFormatTest`, `RouterStatusParserTest` | JVM unit |
| `RouterConfigStoreTest`, `TorrentControlTest`, `TorrentDocumentReaderTest` | JVM unit |
| `TorrentMetadataJniTest`, `RuntimeAdminParityTest` | instrumentation |
| `MainActivityNavigationTest`, `RouterServiceLifecycleTest` | instrumentation |
| `TorrentSafStorageTest`, `TorrentSafCancellationWireTest`, `TorrentSafProviderCancellationTest`, `TorrentSafFixtureProvider` | instrumentation/provider |
| `LocaleResourceTest`, `check-android-locales.pl` | static/device |
| Java 25 guard / JNI link / APK | Android CI job |

## CI expectations

- `check-config.sh` enforces JDK/toolchain/minSdk constraints.
- `:i2pd:testDebugUnitTest`, `lintDebug`, arm64 JNI link and debug APK.
- Instrumentation requires an emulator/device; keep it separate and manual.
- No local Gradle/NDK/APK execution; record pending until the manual job passes.

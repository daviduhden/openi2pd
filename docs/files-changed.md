# Files created, modified and removed

This list covers the refactoring described in the other documents. No commit
was created.

## Created

### Runtime and IPC

- `daemon/TuiServer.h` — local UNIX socket server declaration.
- `daemon/TuiServer.cpp` — bounded read-only IPC, peer credentials, snapshots.
- `libi2pd/TuiProtocol.h` — versioned request/response parser and framing.
- `tui/TuiClient.h`, `tui/TuiClient.cpp` — socket client and option/config
  resolution.
- `tui/main.cpp` — ncurses views, refresh loop, key handling.

### Tests and fuzzing

- `tests/test-tui-protocol.cpp` — protocol and framing regressions.
- `tests/fuzz/fuzz-tui-protocol.cpp` — libFuzzer target.
- `tests/fuzz/fuzz-http-parser.cpp` — libFuzzer target.

### Android

- `android/` — Gradle project and Java sources imported from
  `PurpleI2P/i2pd-android`.
- `android/app/src/main/cpp/` — JNI sources and CMake wrapper; replaces
  `ndk-build` and the submodule.
- `android/app/src/main/AndroidManifest.xml` — app-specific storage, no WebView
  console, no broad storage permission.
- `android/README.md`, `docs/devs/building/android.md` — integrated build
  instructions.

### Privilege separation

- `privsep/Protocol.h`, `Channel.{h,cpp}`, `LogWorker.{h,cpp}`,
  `Worker.{h,cpp}`, `RunWorker.{h,cpp}` — IPC, log worker and child lifecycle.
- `daemon/i2pd.cpp`, `daemon/Daemon.cpp`, `libi2pd/Log.*` — worker entrypoint,
  spawn/configure/shutdown and external log sink.
- `tests/test-privsep-protocol.cpp`, `tests/test-privsep-log.cpp` — framing/bounds/channel and worker regression.
- `docs/security/privilege-separation.md`, `ipc-protocol.md`,
  `process-capabilities.md`, `threat-model.md`.

### Documentation

- `docs/architecture.md`
- `docs/interop-matrix.md`
- `docs/dependencies.md`
- `docs/cpp23-migration.md`
- `docs/build-system.md`
- `docs/tui-ipc.md`
- `docs/security-audit.md`
- `docs/testing.md`
- `docs/files-changed.md` (this file)
- `docs/attribution-audit.md`
- `docs/upstream-sync.md`
- `docs/INTEROPERABILITY.md`
- `docs/android-modernization.md`
- `docs/REBRANDING.md`
- `TODO.md`, `ROADMAP.md`, `PROGRESS.md`, `AGENTS.md`
- `docs/architecture/README.md`
- `contrib/artwork/open-i2pd-icon.svg`
- `docs/tools/` (per-tool documentation)
- `docs/developer-guide/`, `docs/protocols/`, `docs/security/` (integrated
  documentation indexes)
- `contrib/upstream/sync.sh`, `contrib/upstream/*-files.txt` (pinned provenance
  and manual comparison)

## Modified — functionality

| File | Change |
|---|---|
| `libi2pd/Base.cpp`, `Base.h` | constexpr strict base64, `std::span`, empty base32 fix |
| `libi2pd/HTTP.cpp` | header OWS/token validation, strict lengths, chunk extensions, URL port, vector buffers |
| `libi2pd/Reseed.cpp` | checked SU3 reads, bounded allocations/signature, ZIP header initialization |
| `libi2pd/NetDb.cpp`, `NetDb.hpp` | locked counters, atomic reseed state and serialization |
| `libi2pd/Transports.cpp`, `Transports.h` | peer count/snapshot with lock, transport bitmask, C++23 `contains` |
| `libi2pd/Log.cpp`, `Log.h` | bounded recent-log ring and thread-safe snapshot accessor |
| `libi2pd/Crypto.cpp`, `Garlic.cpp` | constant-time integrity comparisons |
| `libi2pd/Config.cpp` | removed `[http]` options; added `[tui]`; Android default off |
| `daemon/Daemon.cpp`, `Daemon.h` | removed web console lifecycle; start/stop TUI IPC; OpenBSD path handling |
| `daemon/UnixDaemon.cpp` | unveil TUI socket path (`rwc`) without new pledge promises |
| `daemon/I2PControlHandlers.cpp` | locked peer count |
| `Win32/Win32App.cpp` | removed console menu and URL handler |
| `tests/test-base-64.cpp` | strict decode, padding, span regressions |
| `tests/test-http-req.cpp` | header OWS/token and Content-Length regressions |
| `tests/test-http-merge_chunked.cpp` | extension and malformed-size regressions |
| `tests/test-http-url.cpp` | port validation regressions |

## Modified — C++23 cleanup

The following files lost unreachable C++20 fallback branches or stale Boost
version branches; comment-only changes are included:

`libi2pd/Destination.cpp`, `Destination.h`, `RouterContext.cpp`,
`RouterContext.h`, `RouterInfo.h`, `Tag.h`, `Timestamp.cpp`, `Timestamp.h`,
`Transports.h`, `util.cpp`, `Tunnel.h`, `TunnelPool.h`, `TransitTunnel.h`,
`Streaming.h`, `LeaseSet.h`, `ECIESX25519AEADRatchetSession.h`, `Garlic.h`,
`NTCP2.cpp`, `libi2pd_client/AddressBook.cpp`, `ClientContext.cpp`,
`HTTPProxy.cpp`, `I2CP.cpp`, `I2PService.h`, `SAM.cpp`, `SOCKS.cpp`,
`SOCKS.h`, `i18n/I18N.h`.

## Modified — build, packaging, CI

| File | Change |
|---|---|
| `build/CMakeLists.txt` | rewritten: C++23 hard requirement, explicit sources, target usage requirements, options, install, TUI, CTest, sanitizers |
| `tests/CMakeLists.txt` | test helper function, all tests, TUI test, fuzz option |
| `build/cmake/i2pdConfig.cmake` | imported targets and `find_dependency` |
| `build/cmake_modules/CheckAtomic.cmake` | C++23 probe |
| `Makefile` | POSIX-only CMake/Ninja front-end |
| `tests/Makefile` | POSIX CTest wrapper |
| `build/build_mingw.cmd` | CMake/Ninja, no `make`, no XP path |
| `android/app/build.gradle` | Gradle 9.8.1/AGP 9.4.1, JDK 25 toolchain, CMake JNI, no hard-coded signing keys |
| `android/.../DaemonWrapper.java` | app-specific storage, new package and data directory |
| `android/.../I2PDActivity.java`, `MainPreferenceActivity.java` | removed web console and storage-permission gate |
| `android/.../i2pd_android.cpp` | removed web-console URL; added status summary |
| `build/win_installer.iss` | removed webconsole files |
| `debian/rules`, `debian/control` | CMake/Ninja build, new deps, no patches |
| `contrib/rpm/i2pd.spec`, `i2pd-git.spec` | `cmake --build`, no make/ninja invocation |
| `contrib/i2pd.conf`, `contrib/openbsd/i2pd.conf` | `[http]` → `[tui]` |
| `contrib/docker/Dockerfile`, `i2pd-docker.conf` | removed console port/config |
| `contrib/openbsd/README.md` | TUI socket and sandbox notes |
| `README.md`, `docs/README.md` | C++23/CMake/TUI/manual-CI documentation |
| `.github/workflows/build.yml`, `build-openbsd.yml`, `build-windows.yml` | manual-only triggers, Ninja, TUI test targets |
| `tests/test-sam-version.sh` | POSIX loop instead of `seq` |

## Removed

| Path | Reason |
|---|---|
| `daemon/HTTPServer.cpp`, `HTTPServer.h`, `HTTPServerResources.h` | HTTP administrative console |
| `contrib/webconsole/` | Console CSS assets |
| `filelist.mk`, `Makefile.linux`, `Makefile.openbsd`, `Makefile.mingw` | GNU make/build fragments replaced by CMake |
| `debian/patches/` | Patches applied to the deleted GNU make build |
| `contrib/debian/` | Packaging for EOL Ubuntu releases using the deleted build |
| `[http]` options in configs and `Config.cpp` | Console no longer exists |
| Win32 `ID_CONSOLE` menu/handler | Console no longer exists |

## Preserved deliberately

- `libi2pd_client/HTTPProxy.*`, HTTP tunnel code and `[httpproxy]`.
- Reseed code, HTTPS/SU3 handling and reseed options.
- `daemon/I2PControl.*` and its certificate/token handling.
- `libi2pd_wrapper/` and public library headers.
- Torrents web assets under `contrib/torrents/` (client feature, not the
  administrative console).
- Historical `ChangeLog` references to the web console.

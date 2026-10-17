# Progress log

Chronological, append-only record. New sessions should read this file first,
then `TODO.md` and `ROADMAP.md`, then verify the code state.

## Session 1 — initial audit and refactoring (this conversation)

- Audited repository: `libi2pd`, `libi2pd_client`, `daemon`, `i18n`, builds,
  tests, packaging, CI, OpenBSD sandbox.
- Baseline Linux/GCC 16 build passed (101 steps).
- Removed HTTP admin console, resources, `[http]` options, Win32 menu entry and
  Docker port.
- Added local TUI: `libi2pd/TuiProtocol.h`, `daemon/TuiServer.*`, `tui/*`.
- C++23 hard requirement, explicit CMake sources, CTest, POSIX make front-end,
  removal of GNU make fragments and C++20 fallbacks.
- Security fixes: base64 race/strictness, HTTP parser, SU3 checked reads,
  locked netDb counters, peer snapshots, constant-time integrity checks.
- Imported i2pd_docs_en (commit `c5d86a8d...`) and i2pd-tools
  (`ca7ece8d...`); created `tools/`, docs tree, upstream manifests and sync
  script.
- Imported Android app (upstream `f8633c6e...`) under `android/`; replaced
  ndk-build with CMake/JNI, removed WebView console and broad storage
  permission, added status summary.
- Attribution cleanup: removed obsolete author/maintainer lines and personal
  upstream install links; preserved copyright/license notices and historical
  changelogs.
- Live validation: router started with temporary datadir, TUI socket mode 0600,
  all IPC commands and malformed requests tested, TUI connected and quit,
  SIGTERM removed socket/pidfile.
- Tests: 16/16 passed at that point (before firewall test).

## Session 2 — firewall and Android modernization

- Added `tools/firewall.hpp` endpoint model and four renderers.
- Refactored `routerinfo`: `-f` legacy output preserved; explicit `iptables`,
  `nftables`, `windows`, `pf`; `--nft-family/table/chain`; multi-file
  deduplication; strict address/port validation.
- Added `tests/test-routerinfo-firewall.cpp` and CLI smoke checks.
- Verified `routerinfo` and the firewall unit test compile on Linux/GCC 16.
  Test execution pending (see below).
- Android Gradle upgraded to wrapper 9.8.1 (checksum pinned) and AGP 9.4.1.
- JDK minimum 25 enforced at Gradle evaluation; Java toolchain 25; source
  language remains 17 because that is the supported D8/R8 level.
- Removed old WebView preferences, permission gate activities, hard-coded
  signing credentials and cleartext WebView flag.
- Gradle configuration now reaches the expected missing-SDK check; no APK was
  built.

## Current known incomplete/blocked items

- `test-routerinfo-firewall` compiled but was not yet run.
- Final full build/test after all recent edits not yet repeated.
- No I2P Java stable reference cloned/audited yet.
- Android SDK/NDK absent; no APK/JNI build/lint.
- ASan/UBSan/TSan/fuzzers not run.
- OpenBSD, Windows, Android, live Java interop not validated.
- `TODO.md` contains the full unresolved inventory.

## Immediate next steps

1. Run full CMake build + CTest, including firewall test and tools smoke.
2. Run `bmake` front-end after changes.
3. Clone latest stable I2P Java in `/tmp` and start `docs/INTEROPERABILITY.md`.
4. Finish Java refactor and validate Gradle when SDK is available.
5. Continue tool security audit and docs link check.

## Session 3 — Java reference, firewall validation, Android toolchain

- Cloned I2P Java stable tag `i2p-2.13.0` (commit
  `9134f808337b401e8e53c73734c81fab04280c9d`, released 2026-07-24) to
  `/tmp/opencode/i2p-java`; confirmed no newer GitHub stable release.
- Created `docs/INTEROPERABILITY.md` with Java class/function references and
  status vocabulary.
- Found and fixed RI-001: C++ RouterInfo cap 3072 → 4096 to match Java
  `RouterInfo.MAX_UNCOMPRESSED_SIZE`; static validation only.
- Refactored `routerinfo` firewall backends and added
  `test-routerinfo-firewall`; full CTest 17/17 passed on Linux/GCC 16,
  including tools smoke.
- Android: Gradle 9.8.1/AGP 9.4.1, JDK 25 minimum check and toolchain, SDK 37,
  no signing secrets; Gradle configuration reaches missing-SDK error (expected
  because no Android SDK is installed).
- Created `TODO.md`, `ROADMAP.md`, `PROGRESS.md` as persistent state.
- Next: full POSIX-make re-run, Java matrix deep-dive, Android SDK/NDK build,
  sanitizer/fuzzer execution.

## Session 4 — rebranding

- Audited brand references and created `docs/REBRANDING.md`.
- Android applicationId/namespace changed to `org.openi2pd.router`; Java
  packages, manifest and JNI symbols updated. This is a breaking install
  identity change, documented for migration.
- Added neutral placeholder icon (`contrib/artwork/open-i2pd-icon.svg`),
  regenerated Android mipmaps, Windows icon, docs logo/favicon; removed old
  screenshots and WebView visuals.
- Removed personal/upstream install/donation/team references from Android and
  install docs; kept provenance and legal notices.
- Added systemd `Alias=open-i2pd.service` and updated service descriptions;
  binary/config/persistence paths intentionally retained pending migration.
- Updated Dockerfile to CMake/Ninja (no GNU make).
- `bmake` full build and `bmake test` passed (17/17) after the firewall,
  Android and rebranding changes.
- Android Gradle config parsed to the missing-SDK check; no APK built.
- Next: build/validate Android package, add config/data migration, decide
  executable rename.

## Session 5 — privilege separation foundation

- Audited monolithic capabilities and created `docs/security/` reports.
- Added `privsep/`: versioned bounded IPC, AF_UNIX/SOCK_SEQPACKET channel,
  peer-UID check, CLOEXEC, pidfd-aware child wrapper, log worker.
- Integrated `privsep.enabled`/`privsep.log`: router now spawns an exec'ed log
  worker before logger start when enabled; core does not open the configured
  log file.
- Live Linux verification: core had no log descriptor; worker had the only
  write descriptor; router logged through IPC.
- Added `test-privsep-protocol` and `test-privsep-log`; full CTest 19/19 passed on Linux/GCC 16.
- OpenBSD worker policy: unveil(log file) + pledge without inet/dns. Core
  policy and network/storage/core workers remain pending.
- This is an intermediate stage, not the requested final multiprocess
  architecture. See TODO PRIVSEP-005..014.
- `tools/vain`: found/hash/FoundNonce/fKeyId made atomic, duplicate
  per-thread key index fixed, live one-character search wrote a valid key.
- `bmake test` also passed 19/19 after these changes.

## Session 6 — parallel subagents and consolidation

- Coordinator launched independent subagents for Android Java, tools
  hardening (`famtool`, `keyinfo`, `autoconf_i2pd`), and OpenBSD SSU2/MTU
  review. Scopes were disjoint; no commits allowed.
- Added `AGENTS.md` (subagent rules/ownership) and
  `docs/architecture/README.md` (ADRs and current runtime diagram).
- Continued coordinator work on IPC/logging foundation, `vain` concurrency
  hardening, README/TODO/ROADMAP/PROGRESS linking.
- All 19 CTest tests and `bmake test` passed before/after the logging changes;
  Android and OpenBSD runtime validation remain blocked.
- Multiprocess architecture remains deferred by explicit instruction; the log
  worker is the only implemented separation and is not a claim of full
  privilege separation.
- Subagent scopes in progress: Android Java/lifecycle, tools
  (famtool/keyinfo/autoconf), OpenBSD UnixDaemon/SSU2/transports. Coordinator
  will review their diffs and fix cross-component build failures before
  declaring anything integrated.

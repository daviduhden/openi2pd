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
  an external read-only clone; confirmed no newer GitHub stable release.
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

### Subagent results integrated

- Android: Java 17 records/switch/lifecycle/concurrency/config hardening;
  static javac-with-stubs and XML checks only. Device/Gradle validation pending.
- Tools: `famtool` RAII/OpenSSL error paths/family signature checks;
  `keyinfo` b33 bounds; `autoconf_i2pd` bounded input and file error handling.
  Manual smoke tests reported; full Android/OpenBSD builds pending.
- OpenBSD: pidfile O_CLOEXEC/O_NOFOLLOW/regular-file/permission checks,
  rlimit error handling, signal error handling, SSU2/Transport minor type fixes.
- Coordinator fixed a follow-up `keyinfo` b33 header bounds check and rebuilt:
  CMake 149/149 targets and 19/19 CTest passed on Linux/GCC 16.

## Session 7 — Android Java 25/UI, native torrent TUI, Perl migration

- New mandatory objectives: Java 25 as the real source/target language (not
  only the build JDK), full Material 3 GUI redesign, native C++23 torrent
  administration in ncursesw, and replacement of project-maintained Python
  scripts with portable Perl.
- Android agents launched with disjoint ownership: Java 25/toolchain,
  Material UI/UX, service/MVVM architecture, JNI status/logs. Coordinator
  prepared OpenJDK 25, Android SDK 37, build-tools 36 and NDK 27.2 in `/tmp`
  for static/Gradle validation; no APK/device test claimed.
- Torrent inventory: existing C++ engine (`libi2pd_client/Torrents*`),
  Transmission-compatible JSON-RPC, and a browser console under
  `contrib/torrents/web/` (`index.html`, `main.js`, `rpc.js`). The JS behavior
  covers listing, details, add file/magnet, start/stop/reannounce/delete,
  session/DHT stats and formatting. It will be mapped to typed C++ snapshots
  and the local TUI IPC, not reimplemented as a browser bridge.
- Torrent agents launched: control/IPC model, ncurses TUI, security/web/RPC
  removal, Perl migration, and build integration. No agent may commit.
- Python inventory: only `tools/scripts/i2pdctl` was found as project-
  maintained Python (no other `.py` or Python shebang). It is being converted
  to Perl with `JSON::PP`, `Getopt::Long`, core utilities and a documented
  TLS fallback; consumers/docs/install are being migrated.
- Coordinator added TORRENT-*, TORRENT-TUI-*, TORRENT-IPC-*, TORRENT-SEC-*,
  PERL-*, PERL-SEC-*, BUILD-011..013 and TEST-011..013 to `TODO.md`, updated
  the roadmap and subagent ownership.
- Current tree includes uncommitted Android and torrent/Perl work from
  parallel agents. Nothing is marked validated until the coordinator reviews
  the diffs, builds and runs the prepared tests. Full network/storage worker
  separation for torrents remains unimplemented and is explicitly blocked.

### Session 7 integration checkpoint

- Android Java 25 source/target is configured and `:i2pd:compileDebugJavaWithJavac`
  completed successfully with Temurin JDK 25 before the environment was frozen.
  XHTML/XML resources parse; a JVM parser test was added. APK/native/Lint/device
  validation was not completed and is deferred to CI.
- Material 3 UI/architecture source is present: hosted fragments, adaptive
  navigation, `RouterService`/`RouterRepository`/`RouterViewModel`, bounded
  JNI snapshots and logs. No WebView/Kotlin/Compose.
- Native torrent migration: web HTML/CSS/JS deleted; typed C++ control
  snapshots/commands, local TUI IPC, ncurses Torrents/details views, parser
  hardening, path containment and optional loopback-only RPC are implemented.
  Bencode and TUI protocol tests passed (20/20 CTest before the freeze).
- Perl migration: only `tools/scripts/i2pdctl` was in scope. It is now Perl 5;
  `perl -c` and 54 Test::More tests passed on Linux. Docs/install/CMake updated.
- CI: zizmor 1.31.1 reports no findings after pinning action SHAs and setting
  `persist-credentials: false`; yamllint 1.38.0 is clean with `.yamllint.yml`.
  Triggers remain `workflow_dispatch` only.
- User instruction after this checkpoint: **do not compile/build/test in this
  environment; leave all builds and dynamic tests to CI.** Generated build
  directories/caches were removed. Remaining work is static review and CI
  execution, not local validation.

- OpenBSD packaging: copied the `wip-openbsd-ports` `i2pd.rc` and `i2pd.login`
  into `contrib/openbsd/` and documented `rcctl` usage. No compile or service
  run was performed in this environment.

### Session 8 — resumed full-program coordination

- Re-read the tracked state and real diffs; preserved all uncommitted work.
- Local compilation/build/test remains frozen by user instruction. Only static
  checks were performed: `git diff --check`, XML parsing, Markdown link check,
  zizmor and yamllint.
- CI hardened: actions SHA-pinned, checkout credentials disabled, workflow
  document starts added, `.yamllint.yml` added; zizmor 1.31.1 reports no
  findings and yamllint 1.38.0 is clean. Linux CI now checks the Perl client.
- OpenBSD `wip-openbsd-ports` `i2pd.rc` and `i2pd.login` copied byte-for-byte
  to `contrib/openbsd/`; `rcctl` instructions added.
- Static-review subagents were launched with disjoint scopes: Android UI/Java,
  Torrent/TUI, IPC/supervisor, I2P interoperability, Perl security, OpenBSD
  service hardening, plus Tools/firewall. Torrent/TUI, I2P, Perl, Tools/firewall, IPC, OpenBSD and Android
  completed static passes. All were instructed not to compile and not to
  commit; no build or dynamic test was run.
- Pending coordinator work: integrate their diffs, resolve any shared-interface
  conflicts, update this TODO/roadmap, and hand the frozen build/test matrix to
  CI.

### Torrent/TUI static-review result

- Rejected invalid metainfo before insertion; required a non-empty root name.
- Startup `.torrent` loading now uses bounded `ReadTorrentFileBounded()`
  (regular, non-symlink, <=32 MiB) and validates metadata.
- TUI now resets session/DHT state on reconnect/tunnel change, keys tunnel
  identity by index+name, and cancels pending removal if the tunnel/torrent
  changed (critical for remove-with-files).
- Tunnel list is capped; protocol docs match field names.
- Added magnet hash validation for 40-char hex and 32-char base32, rejecting
  partial hashes, and marshalled RPC metainfo/magnet additions onto the
  torrents service thread with bounded waits.
- Tests extended for bencode/name/path/bounded-file/TUI protocol cases.
  No compile or execution was performed; residual TOCTOU, RPC `.wait()` paths
  and live TUI/engine tests remain.

### I2P protocol-bounds static-review result

- Fixed LeaseSet expiration pre-scan off-by-one (exact-fit truncated buffer).
- Bound RouterInfo peer lists and version parsing; accept exact 4096-byte RIs.
- Bound I2NP DatabaseLookup/SearchReply/DatabaseStore serialization; reject
  non-fitting LeaseSets; fix one-byte search-reply count.
- Fixed NTCP2 truncated block-header read and SSU2 PathResponse/partial-message
  bounds; added a 128-entry incomplete-message cap.
- Added `test-protocol-bounds` and `fuzz-protocol-bounds`, registered in
  `tests/CMakeLists.txt`. They were not built or executed.
- Added residual tasks NET-014 (NetDb save iteration lock) and SEC-014
  (malformed address/property parsing, SSU2 family mismatch). Java differential
  validation remains unavailable in this environment.

### Perl security static-review result

- URL/host/port/path validation, 1 MiB request/response bounds and byte-length
  calculation hardened.
- Chunked framing now requires CRLF after each chunk, checks per-chunk and
  cumulative size, and rejects unsupported transfer encodings.
- TLS fallback is shell-free, uses `-ign_eof`, flushes stdin safely, brackets
  IPv6, sanitizes stderr, and never downgrades to cleartext. Added
  `I2PDCTL_TLS_BACKEND=auto|openssl` for diagnostics/tests.
- Whole-exchange alarms, SIGPIPE handling and bounded child reaping added.
- Terminal output escapes C0/DEL/C1 control characters.
- Tests expanded with URL/transport/chunk/size/TLS-shim cases. Not executed.

### Tools/firewall static-review result

- `vain` rewritten to RAII key buffers with cleansing, `std::jthread` joins,
  checked key layout/serialization, fixed option parsing, bounded prefixes,
  signed-overflow/nonce sentinel/thread-count/regex/write failures, and safer
  output collision checks.
- Firewall endpoint validation now restricts numeric address grammar and
  rejects scoped/bracketed/v4-mapped/broadcast forms; Windows rule tags are
  strictly validated; nft options require the nft backend.
- Extended `test-routerinfo-firewall` and `tools-smoke` negative checks.
- Residual: final `vain` output open is not `O_NOFOLLOW`; no dynamic build,
  sanitizer, key-generation or native firewall syntax test was run.

### IPC/log-worker static-review result

- Fixed EOF-as-timeout busy loop, CLOEXEC failure handling, short-send errors,
  MSG_TRUNC detection, peer-close semantics and closed-channel UID checks.
- Reworked post-fork worker setup to async-signal-safe operations; worker fd is
  now an argv argument and must be an inherited `SOCK_SEQPACKET` socket.
- Added configure-once enforcement, duplicate/NUL rejection, write/flush error
  propagation, stdout-only pledge and localtime unveil for the OpenBSD worker.
- Added an in-process bounded restart monitor for the log worker (backoff,
  attempt cap, stable-period reset, shutdown-aware). A separate supervisor
  process and network/storage separation remain unimplemented.
- Prepared expanded privsep protocol/log tests, including sink reattach.
- No compiler/CTest/sanitizer run; `PRIVSEP-005` and `DEF-010` remain partially
  implemented.

### OpenBSD service static-review result

- PID-file cleanup now removes only the locked regular inode, including at
  shutdown; parent after `fork()` uses `_exit`; startup rollback stops the
  logger thread before exit.
- Unveil paths narrowed to effective log/file/TUI/address-book destinations;
  `/dev/log` is unveiled for syslog; URLs are skipped during path setup.
- `privsep.log` is refused when OpenBSD pledge/unveil is active because the
  worker is spawned after the sandbox and would need `proc`/`exec`.
- Added `docs/security/openbsd-validation.md` and documented residual custom
  pledge, readiness, SIGUSR1 and TUI-socket TOCTOU issues. The reseed URL and
  OpenBSD TSTP option were fixed statically after the audit.
- `AbsolutePath()` now preserves URL values, and `unix.handle_sigtstp` is
  registered on OpenBSD; dynamic checks remain pending.
- rc.d/login copies remain byte-identical to `wip-openbsd-ports`.

### Android static-review result

- Lifecycle/service: first-launch-only auto-start, explicit stop persistence,
  restart guard, unconditional daemon shutdown on service destruction,
  Android 15 `onTimeout` fail-closed path.
- FGS: `specialUse` on API 34+ with `dataSync` only on API 29..33; manifest
  permission/subtype property added. This needs device/Play Console validation.
- Settings/config I/O moved to a single background executor; stale view
  callbacks are generation-checked; autostart marker writes are off-main.
- Removed the broken session-only transit switch, dead connectivity receiver,
  unused dependencies/strings/resources and stale WebView ProGuard text.
- Added pure-JVM tests for parser, state formatting, records and config values.
- No compiler/Gradle/AAPT2/Lint/device test was run; the latest lifecycle,
  settings, manifest and test edits postdate the earlier Java compile.

### Session 8 static-review closure

- All seven specialized static-review passes are integrated: Android,
  Torrent/TUI, IPC/log worker, I2P, Perl, OpenBSD and tools/firewall.
- Coordinator additionally fixed magnet hash validation, RPC service-thread
  marshalling, RPC map locking, `vain` ENOENT output handling, OpenBSD reseed
  URL handling and OpenBSD TSTP option registration.
- Master tracking and docs are updated; no active subagent remains.
- No compiler, CTest, Gradle/AAPT2, APK, Lint, sanitizer, fuzz, OpenBSD or
  device test was run. CI/user must execute the prepared commands.


### Session 9 — openi2pd visual identity

- Supplied `logo.png` (1254×1254 RGBA) preserved unchanged as
  `assets/branding/logo-original.png`.
- Reconstructed the symbol and lockup as real vector paths (no embedded image,
  no runtime font): full, symbol-only, horizontal, monochrome, light and dark
  variants; generated `logo.png` and `symbol.png` from the SVG masters.
- Added canonical palette/contrast data in `assets/branding/colors.md` and
  identity documentation under `docs/branding/`.
- Launched disjoint Android UI, TUI, Documentation and Packaging branding
  passes. No commit, push, Gradle, native build or visual/device test has been
  performed for this branding work.

### CI follow-up (uncommitted)

- First manual run: Linux/Android, Windows and OpenBSD failed.
- Second run after portable `Expected`/`std::thread` fixes: Android still linked
  native code against ARM vcpkg libraries; OpenBSD hit OpenSSL's `U64` macro
  collision; Windows tools lacked `mswsock`; Perl transport tests exposed a
  B/s formatting bug and ANSI residue.
- Local fixes: NDK chainload + `boost-json`/`boost-url`, `U64BE` codec names,
  `mswsock` propagation, raw rate formatting and CSI stripping.
- Current instruction forbids commit/push, so these fixes and the CI rerun are
  pending. No local build/test was run.

### Branding subagent results (static)

- **Android:** official symbol vector in launcher/notification/About; adaptive
  icon API 26/33, legacy PNG mipmaps, Android 12 splash, semantic light/dark
  Material 3 palette, `openi2pd` app label and About license text. AAPT2,
  Lint, APK/device and Java compile still pending.
- **Documentation:** README/docs index/nav use `openi2pd` and relative branding
  links; repository link checks and MkDocs nav path checks passed statically.
- **Packaging:** Windows multi-size ICO, desktop entry, favicon/icon, CMake
  installs, Windows metadata and Debian description updated. Windows resource,
  CMake install, desktop and package builds not run.
- **TUI:** centralized theme module with semantic roles and 8/16/256/direct/
  monochrome fallbacks; no raw ANSI or bitmap logo; terminal matrix pending.

### License headers (uncommitted)

- Added `Copyright (c) 2013-2026, The PurpleI2P Project` and
  `Copyright (c) 2026, David Uhden Collado` with
  `SPDX-License-Identifier: BSD-3-Clause` to project C/C++/Java/Python-free
  scripts, CMake, Makefiles, Android XML/Gradle, desktop/service/Windows
  resources and workflow/config sources.
- Updated the existing `BSD3` wording to `BSD-3-Clause`; root, Android and
  tools license files and Debian copyright metadata were aligned.
- Third-party files (`build/cmake_modules/FindCheck`, `GetGitRevisionDescription`,
  Windows SDK `winres.h`, Gradle wrapper Apache-2.0, OpenBSD ports copies) were
  left under their existing notices; documentation remains CC-BY-SA-4.0.
- No license scanner or compiler was run; the static header inventory reports
  only expected third-party/no-SPDX exceptions.

### Branding review and license pass

- Security/review findings were addressed statically: Read-the-Docs CSS
  selectors, docs asset mirror with strict MkDocs build, documented Android
  semantic tokens, genuine navy `logo-dark`, softened-white `logo-light`
  (`#FFFFFF`), TUI disabled/warning contrast, Windows copyright wording and
  explicit artwork license metadata.
- `mkdocs build --strict` completed with only informational unused-page notes.
- Added LICENSE copyright lines and `SPDX-License-Identifier: BSD-3-Clause` to
  project code/config; third-party Apache/BSL/Windows-SDK/OpenBSD-port files
  retain their notices; documentation stays CC-BY-SA-4.0.
- No compiler, Android AAPT2/Lint/device, Windows resource, or visual/terminal
  test was executed; replacement assets remain uncommitted and HEAD is
  therefore not a releaseable branding state.

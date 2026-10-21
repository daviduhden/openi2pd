<!--
See the LICENSE file at the top of the project tree for copyright
and license details.

openi2pd: PROGRESS.

Documents project documentation.

-->

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

### Formatting pass (uncommitted)

- Added `.clang-format` with a C++ profile matching the existing tab/Allman/space
  conventions and a separate 4-space Java profile.
- Ran clang-format 23.1.3 over 245 C/C++/Java source files (generated JNI header
  excluded). The pass is whitespace-only but very large; it was not followed by
  a compiler, Gradle, CTest, or Lint run.
- One space-before-tab diff-check artifact was normalized; `git diff --check`
  is clean.

### POSIX manual pages

- Added mdoc pages under `man/` for `i2pd`, `i2pd-tui` and `i2pdctl`, covering
  options, files, signals, security boundaries, examples and exit status.
- CMake installs `man/*.1`; Debian packaging uses the canonical pages and keeps
  `debian/i2pd.1` as a compatibility symlink.
- Validated with mandoc 1.14-style linting; temporary manpath cross-reference
  checks are recorded below. No Linux/OpenBSD installation or live command
  rendering was run.

### Session 10 - deep Java 2.13.0 interoperability (static, in progress)

- Verified the read-only Java baseline: tag `i2p-2.13.0`, commit
  `9134f808337b401e8e53c73734c81fab04280c9d`, commit date 2026-07-20, release
  2026-07-24. GitHub latest release remains 2.13.0.
- Re-read the existing matrix, harness, checker and all modified files from the
  previous static pass before changing anything.
- Started five disjoint implementation workstreams and one test-harness
  workstream: netDb/MetaLS/DatabaseLookup/DatabaseSearchReply/RouterInfo
  freshness; SAM/I2CP/Streaming/Datagram; tunnel-build windows; SSU2 relay and
  timeout alignment; incoming PQ type iteration; plus differential fixtures.
- Previous static corrections remain in the working tree: SSU2 PQ lists/relay
  version/fragments/ACK/netID, LeaseSet store-key/replay/lease validation,
  ratchet NS freshness/PQ NSR/multi-key, Datagram2 minimum, SAM version and
  connection wait, I2CP HostLookup, Streaming RESET, and Elligator boundary.
- No compiler, CTest, sanitizer, fuzz, Gradle/Lint, OpenBSD or Java/C++ dynamic
  test was run. No commit, tag, push or release was created.
- `TODO.md`, `ROADMAP.md` and `PROGRESS.md` are synchronized with stable
  I2P-001..I2P-013 IDs and explicit static/dynamic distinctions.

### Session 10 results (static)

- Pinned Java reference verified again: `i2p-2.13.0`, commit
  `9134f808337b401e8e53c73734c81fab04280c9d`, read-only; no newer stable.
- Static implementations integrated for I2P-001..I2P-012: Meta LS2,
  DatabaseLookup `0x02/0x10/0x12`, DSRM sender binding, tunnel-build windows,
  floodfill RouterInfo freshness, SAM 3.2/3.3 handling, I2CP BlindingInfo,
  Datagram3 first contact, Datagram2 offline signatures, incoming PQ candidate
  iteration, SSU2 relay validation and NTCP2/SSU2 timeout alignment.
- I2P-013 GOST remains blocked: Java 2.13 reserves signature types 9/10 and
  provides no usable peer verifier.
- An independent security review produced static fixes for: Java OBGW
  records with receive tunnel ID 0 (must be accepted), first SSU2 data packet
  reordering, late SessionConfirmed fragment-count changes, dead HolePunch
  DateTime skew validation, Meta LS2 published-time freshness, and SAM invalid
  `SEND` desynchronization.
- `tests/interop/` now has a static fixture validator and 24 passing static
  cases plus one documented I2NP checksum gap; `check-vectors.pl` syntax and
  reference verification pass. No differential binary was run.
- `TODO.md`, `ROADMAP.md` and `PROGRESS.md` are synchronized. No build, CTest,
  sanitizer, fuzz, Gradle/Lint, OpenBSD or Java/C++ dynamic test was executed;
  no commit, tag, push or release was created.

Static checks run: `git diff --check`, `perl -c tests/interop/check-vectors.pl`,
`tests/interop/check-reference.sh`, and `check-vectors.pl` (24 static/vector
pass, 1 known checksum gap). No compiler, CTest, sanitizer, fuzz, Gradle/Lint,
OpenBSD or Java/C++ dynamic test was run.

### Session 11 — parallel modernization continuation (static)

- Re-read `AGENTS.md`, `TODO.md`, `ROADMAP.md`, `PROGRESS.md` and the clean
  `800c8627` baseline before changing files.
- Four independent subagent workstreams were used with disjoint file scopes:
  OpenBSD/logging hardening, RouterInfo/SSU2 parser bounds, Torrents RPC
  bounded waits, and atomic TUI status reads.
- OpenBSD: custom `openbsd.pledge_file` policies are now additive to the fixed
  startup and runtime baselines; the self-daemonizing parent waits for a
  bounded close-on-exec readiness byte after pidfile/sandbox/logger/start;
  log destination/stream replacement is synchronized and `SIGUSR1` rotation
  has a stress procedure. All OpenBSD paths remain statically validated only.
- Security: RouterInfo address/property option parsing is bounded by the
  declared option section, addresses are capped at the Java limit of 16,
  duplicate keys and malformed string/parameter encodings are rejected, peer
  counts are overflow-checked, and SSU2 cross-family Address blocks are
  ignored before they can overwrite the wrong external address.
- Concurrency: TUI-visible `RouterContext` status/testing/error/netID/bandwidth/
  floodfill/accept/hidden fields and tunnel creation time use `std::atomic`
  loads/stores; `TuiServerCommon` uses accessors only.
- Torrent RPC: the four blocking `.wait()` service calls use bounded packaged
  tasks with self-contained captures; timeouts return a JSON-RPC error and a
  late completion cannot touch request stack storage.
- Added `tests/test-su3-bounds.cpp` for empty/truncated/oversized SU3 headers
  and registered it in CTest. No test was compiled or executed.
- No commit, tag or push was created. No compiler, CTest, sanitizer, fuzz,
  Gradle/Lint, OpenBSD or Windows run was executed.

### Session 12 — C++23 ownership and interface pass (static)

- Continued from the clean `800c8627` baseline plus the Session 11 work;
  no previous changes were reverted and no commit/tag/push was created.
- Split the modernization into disjoint file scopes for libraries,
  transports, clients and the daemon, with a coordinator review of every
  ownership change.
- Streaming/Gzip: `SendBuffer` now owns its payload with `unique_ptr`;
  copy/move are deleted to prevent aliasing; pointer APIs remain and
  `std::span<const uint8_t>` send/async-send overloads were added; gzip
  temporary buffers use `std::array`.
- LeaseSet/Identity: lease-set and identity heap storage uses explicit
  `unique_ptr` owners; public `GetBuffer` views, copy/assignment and
  signature/crypto paths are preserved; local identity temporaries use
  vectors/unique owners.
- NTCP2/Transports: handshake, receive/send and staged-frame buffers are
  explicitly owned, with ownership moved into async completion handlers;
  transport service, work guard, timers, servers and thread use
  `unique_ptr` and `make_unique`.
- Client protocols: SAM asynchronous immediate writes retain shared
  buffer ownership; I2CP and ClientContext temporary host/identity/key
  buffers use `std::vector`.
- Torrents DHT: bucket-chain links are `unique_ptr`; traversal and
  exploratory cursors are documented non-owning observers.
- Daemon/libraries: the `Daemon_Singleton` pimpl is owned by
  `unique_ptr`; log/NetDb/tunnel/transit/NTP/UPnP threads and reseeder
  use explicit owners and `make_unique`.
- Added `tests/test-streaming-buffer.cpp` for the owning `SendBuffer`
  copy semantics and span constructor; registered it in `tests/CMakeLists.txt`.
- Static checks: `git diff --check`, clang-format dry-runs and source
  inspection only. No compiler, CTest, sanitizer, fuzz, Android, Windows
  or OpenBSD execution was performed.

### Session 13 — memory and concurrency security audit (static)

- Five independent subagents audited SSU2/RouterInfo, crypto/reseed, the
  BitTorrent engine, TUI/privsep/JNI, and daemon/library lifecycle with
  disjoint file scopes. Existing uncommitted work was preserved.
- Transport/parser fixes: SSU2 short PeerTest reads, bounded token/peer-test
  caches, dropped-packet queue cap, per-session reordered-packet cap,
  proxy relay endpoint lifetime, and a copy-returning session snapshot API.
  RouterInfo option/property sections now reject truncation, duplicates,
  malformed strings, stale family state and trailing bytes; signature
  subtraction happens exactly once.
- Crypto/reseed fixes: checked BIGNUM conversion bounds, X25519/ElGamal
  failure checks, private-key cleansing, strict b33 parsing, ECIES key
  validation, bounded Garlic clove capacities, strict deflate completion,
  bounded HTTP reseed response and certificate/public-key error paths. The
  I2CP Data path now rejects payloads larger than the I2NP buffer and fails
  closed if Garlic wrapping rejects an oversized message.
- BitTorrent fixes: locked torrent snapshots for timer/RPC paths, bounded
  DHT state, validated peer block ranges/indices, bounded ut_metadata
  arithmetic, resume-file size/range checks, checked file I/O, and
  service-thread aggregation for RPC statistics.
- Lifecycle fixes: atomic NetDb/Tunnel/UPnP/Log run flags, join-before-clear
  ordering, idempotent start/stop, UPnP io_context restart, RouterContext
  timer/service teardown, pidfile identity cleanup, and teardown of helper
  threads on daemon start failure.
- IPC fixes: Windows named-pipe instance reuse to avoid name-squatting gaps,
  privsep child fd closure up to RLIMIT_NOFILE, and exception containment
  in TUI dispatch. JNI review found no structural fix; OOM/UTF-8 behavior
  remains a dynamic Android check.
- Added regression tests: SSU2/RouterInfo signed bounds, crypto bounds,
  strict b33, reseed ZIP bounds, Torrents security, Log lifecycle, TUI IPC,
  plus fuzz targets for Base/b33, log records, privsep framing and torrent
  parsers. No compiler, CTest, ASan/UBSan/TSan, libFuzzer, Windows, JNI or
  OpenBSD execution was performed.

### Session 14 — dependency-floor and compatibility cleanup (static)

- Audited CMake, Boost, OpenSSL/LibreSSL, zlib and ncurses compatibility
  branches. C++23 remains the baseline; no old I2P protocol or wire format
  was removed.
- At that time CMake enforced OpenSSL 3.0 or LibreSSL 3.5 and zlib 1.2.11,
  kept Boost 1.74 for program_options and Boost 1.81 for JSON/URL where
  torrents are enabled, and dropped Boost.Atomic and Boost.Filesystem.
  Session 30 later raised the floors (see below).
- Removed the `STD_FILESYSTEM`/Boost.Filesystem fallback, the
  `__cpp_lib_atomic_shared_ptr` Boost.Atomic fallback and the Torrents
  `std::atomic_ref` fallback. `std::filesystem`, `std::atomic<std::shared_ptr>`
  and `std::atomic_ref` are now unconditional C++23 usage.
- Simplified OpenSSL 3.0-only preprocessor branches in Crypto, Signature,
  Reseed, Family and I2PControl while retaining LibreSSL, SipHash, PQ and
  Ed25519ph capability checks.
- Removed the `WITH_TUI` and `I2PD_BUILD_TESTS` compatibility aliases;
  `I2PD_BUILD_TUI` and `BUILD_TESTING` are canonical, and documentation
  was updated.
- Retained only documented library-gap fallbacks: the `i2p::expected` shim,
  `std::move_only_function` fallback and `__cpp_lib_chrono` fallback for
  the OpenBSD base libc++ toolchain.
- Added TODO `CPP-008`; updated `BUILD-002`, `docs/dependencies.md` and
  build/TUI documentation. A CMake configure probe was attempted but stopped
  before dependency checks because the available Clang 23 linker could not
  find `libstdc++`; no build, CTest, OpenBSD/LibreSSL/Windows/Android or
  dependency-matrix run was completed.

### Session 15 — I2P Java stable-reference recheck and fixes (static + local units)

- Checked official GitHub releases and tags on 2026-10-18: the latest stable
  release remains `i2p-2.13.0`, commit
  `9134f808337b401e8e53c73734c81fab04280c9d`; no newer stable tag exists.
- The pinned read-only reference and all checked-in interop fixtures passed
  the static harness: 25 cases, 24 static/vector OK and one documented
  checksum gap; no differential binary or Java peer was executed.
- Five independent audits completed against the pinned tag and normative
  specifications: NTCP2, SSU2/RouterInfo, LeaseSet/netDb/tunnel, Streaming/
  Datagram/SAM/I2CP, and crypto/ratchet/Garlic.
- NTCP2: added a bounded handshake replay cache, strict SessionConfirmed
  block ordering, `m3p2len` cap, data-phase ordering/length checks, exact
  DateTime handling and post-AEAD termination counting.
- SSU2: one-time tokens, exact netID/version/ConnID validation, bounded token,
  peer-test and reorder state, stricter PeerTest and handshake block parsing,
  PQ/version Retry selection, MTU-aware `v`/`pq` metadata, last-fragment
  ordering and ACK-bearing Termination.
- Data/netDb/tunnel: LS1/LS2 future-lease bounds, unpublished/blinded flags,
  encrypted-LS2 freshness, EC_P256 wire keys, DatabaseStore type masking and
  reply blocks, DatabaseLookup exclusion cap and truncated tunnel checks.
- Clients: SAM 3.x UDP first-line/version parsing, 3.2 port/`HEADER` framing,
  component-wise HELLO negotiation, invalid signature-type errors, RAW
  protocol 0, Datagram2 zero-length options, Datagram3 first contact, strict
  Streaming packet/options bounds/ACK/PING flags and I2CP SessionConfig
  date/offline-signature and HostReply options.
- Crypto/ratchet/Garlic: GOST signature/point validation, ECIES NEXT_KEY reuse
  and handshake ACK handling, ElGamal/AES 200-tag/rekey bounds, strict b33
  flags/blinded type, key-certificate length and gated Ed25519ph support.
- Added regression tests for all five areas; corrected stale SSU2 test wire
  encoding, the privsep `U64BE` calls and the Elligator boundary expectation.
- Executed a local Linux/GCC 16.2.0/OpenSSL 4.0.3 configure and build (196
  targets) and CTest: **39/39 passed**. No Java peer, handshake, network,
  ASan/UBSan/TSan, libFuzzer, OpenBSD, LibreSSL, Windows, Android or
  multi-UID run exists, so interoperability is not claimed verified.
- No I2P protocol version or wire format was removed. No public I2P network
  or live Java peer is used by the static harness.

### Session 16 — NTCP2/SSU2 deep transport audit (static + local units)

- Re-audited NTCP2 handshake/state and SSU2 handshake, ACK, fragmentation,
  retransmission, congestion/window, MTU and control-plane paths against Java
  `i2p-2.13.0` and the normative specs.
- NTCP2: completion-driven Termination/close, queued RouterInfo/termination
  control frames, X/Y/static-key/IV validation, Alice-side SessionCreated replay
  cache, pending-handshake release/global cap, extra-pending-byte rejection,
  per-version hybrid padding limits and HTTP proxy buffer lifetime.
- SSU2 core: MTU-sized multi-fragment SessionConfirmed (up to 15) with unchanged
  retransmission, `SendData` bool/out-parameter (packet 0 no longer an error),
  NACK-only ACK ranges and ACK/`acnt` validation, duplicate/reassembly fixes,
  RFC 6298 RTO/deviation and exponential backoff, PQ SessionCreated MTU clamp,
  fragment minimums/overflow limits and post-AEAD migration handling.
- SSU2 control plane: independent per-family non-published addresses, MTU
  publication/refresh, unpublish introducer cleanup, PeerTest source/address
  validation and fixed version-2 HolePunch; transport teardown/restart ordering
  now joins before releasing servers/timers.
- Added `test-ntcp2-handshake-bounds`, `test-ssu2-session-fragment-bounds` and
  `test-ssu2-control-plane`, and registered them in CTest.
- Local Linux/GCC 16.2.0/OpenSSL 4.0.3 build passed and CTest **42/42 passed**.
  No Java peer, live relay/PeerTest/MTU, sanitizer, fuzzer, LibreSSL/OpenBSD,
  Windows or Android validation was run; interoperability is not claimed.

### Session 17 — LeaseSet/RouterInfo/netDb Java 2.13 audit (static + local units)

- Re-audited LeaseSet/LS2/Encrypted/Meta, RouterInfo and netDb against Java
  2.13.0 and the normative specs.
- LeaseSet: exact offline-signature bounds, safe store-type prefix verification,
  standard LS2 earliest/latest lease freshness, encrypted auth/ciphertext and
  outer/inner metadata bounds, type-5 revalidation, 16-entry Meta limits,
  known key lengths/preference, strict property mappings and local writer caps.
- RouterInfo: transactional parsing/update, optional `iexp`, NTCP2 `i`/`v`
  requirements, per-family local dual-stack split, initialized fields,
  bandwidth-tier ranking, dotted versions, family-signature ordering, hidden
  identity certificates, address expiry endianness, introducer limits,
  signature verification on file load, reachability masks and atomic state.
- netDb: candidate-first RouterInfo/LeaseSet replacement, LS1 earliest-lease
  freshness, strict DatabaseLookup reply-key handling (0x12 fail-closed because
  proposal 154's derived KDF is TBD), DatabaseSearchReply parser/sender binding,
  duplicate/replay suppression, bounded pending requests, locked lifecycle/
  visitor snapshots, persistence-future rejection and no cleartext reply fallback.
- Added `test-leaseset2-java213-regression`, `test-routerinfo-java213-parsing`,
  `test-routerinfo-update-transaction`, `test-netdb-lookup-wire-bounds`,
  `test-netdb-dsrm-correlation` and `test-netdb-store-lookup-hardening`.
- Local Linux/GCC 16.2.0/OpenSSL 4.0.3 build passed and CTest **48/48 passed**.
  No Java peer, live floodfill, ASan/UBSan/TSan, fuzzer, LibreSSL/OpenBSD,
  Windows or Android validation was run; interoperability is not claimed.

### Session 18 — static-only development policy and pending CI baseline

- Added the permanent policy to `AGENTS.md`: no local compilation, build
  configuration, test execution, sanitizer, fuzzer, Android build or project
  binary run. Dynamic validation is exclusively manual GitHub Actions CI.
- Local work is limited to static analysis, implementation, test preparation,
  CMake/workflow updates and documentation. Risky architectural refactors may
  leave temporary integration breakage for CI follow-up; security guarantees
  and existing user changes remain protected.
- Recorded status categories for pending CI compilation/tests and corrected
  workflow instructions in `TODO.md`, `ROADMAP.md` and this log.
- Historical local build/CTest results remain recorded as historical evidence,
  not as validation of the current uncommitted crypto, client, IPC or
  privilege-separation changes. No new local build or test was run.

### Session 19 — netDb cross-type LeaseSet freshness (static)

- Added `LeaseSet::GetEarliestLeaseDate()` and populated it for LS1 and
  standard LS2, including reused `Update()` objects. Encrypted and Meta LS2
  have no lease list and use the zero sentinel.
- `NetDb::AddLeaseSet()` and `AddLeaseSet2()` now use a single Java-compatible
  `isNewer()` rule: published timestamp only for LS2/LS2; earliest lease date
  whenever either side is LS1. A valid stored entry is still not evicted by an
  invalid, expired, older or replayed candidate.
- Extended `test-netdb-store-lookup-hardening` with LS1→LS2 and LS2→LS1
  replacement plus an older-LS2 rejection vector.
- `docs/INTEROPERABILITY.md` DB-110 updated. All current changes are
  implemented by static analysis and pending manual CI compilation/tests; no
  local build or test was run.

### Session 20 — CMake and POSIX Make portability (static)

- Audited the root and tests Makefile front-ends: removed non-POSIX `?=`
  defaults, quoted build paths and retained the Ninja generator default. No
  GNU make syntax or platform fragments remain.
- Replaced hard-coded MiniUPnPc search paths with normal CMake discovery and
  an imported target; corrected the installed package config to request only
  the Boost components the tree uses.
- Removed hard-coded MSYS2/Inno Setup paths and automatic `git pull` from the
  Windows helper.
- Added a compile-free `build-system-static` CTest check covering POSIX make
  syntax, download rules, hard-coded paths and canonical C++23/component
  options.
- Updated `docs/build-system.md`, `docs/files-changed.md` and tracking status.
- No local configure/build/install/test was executed; all changes are pending
  manual CI compilation and tests.

### Session 21 — native TUI navigation and statistics (static)

- Added an eighth main view, Destinations, backed by the existing bounded
  `DESTINATIONS` snapshot; NetDb now requests its own `NETDB` fields instead
  of sharing the destination list.
- Expanded the Status panel with transport online state, 15-second bandwidth,
  data/build tunnel queues and the existing creation-success/netDb counters.
- Updated hotkeys/help (`1`–`8`; Torrents is `6`) and kept the independent
  ncursesw/Windows-console client model, semantic brand theme roles and bounded
  local IPC.
- Extended `test-tui-protocol` with NETDB/DESTINATIONS parsing vectors.
- Updated `docs/tui-ipc.md`, `docs/torrent/tui.md`, `docs/TUI-WINDOWS.md` and
  `docs/files-changed.md`.
- No local configure/build/test was run; all changes are pending manual CI.

### Session 22 — native torrent control and containment (static)

- Confirmed the removed browser console has no maintained HTML/CSS/JS assets;
  the native C++23 engine and ncurses TUI provide listing, magnet/file add,
  detail files/peers/trackers, progress, rates, pause/resume, reannounce,
  removal and session/DHT statistics over the bounded local IPC.
- Added `SetTorrentPathWithinRoot` so a torrent's sanitized root name is
  containment-checked before it enters the session map; startup, magnet
  metadata, save and delete paths retain the existing symlink/recheck policy.
- Routed `TORRENT-ADD-FILE` reads through the dedicated disk service with a
  bounded wait, keeping file I/O off the torrent network service thread.
  Process-level network/storage separation remains open (`TORRENT-IPC-003`).
- Kept `.i2p` tracker enforcement in `AddTracker` and `RequestTracker`; peers
  and DHT remain I2P-only, with no clearnet fallback.
- Updated `docs/torrent/security.md`, `TODO.md` and `docs/files-changed.md`.
  No local configure/build/test was run; every change is pending manual CI.

### Session 23 — portable native tools (static)

- Completed the maintained tool inventory: all 15 native executables remain
  CMake targets linked against the in-tree libraries; no tool submodule,
  external router checkout or GNU make file is present.
- Added `tools/common/getopt.{h,cpp}` for MSVC and removed unconditional
  POSIX `getopt`/`unistd` dependencies from `keyinfo`, `famtool`, `routerinfo`
  and `vain`; Unix still uses the system implementation.
- Refactored `i2pbase64` to C++ streams with binary stdio on Windows, removing
  POSIX fd/open/read/write assumptions while preserving its CLI.
- Added `tools-inventory` CTest and retained `tools-smoke`; both are
  compile-free and pending manual CI.
- Updated `TODO.md`, `docs/tools/README.md` and `docs/files-changed.md`.
  No local build or test was executed.

### Session 24 — Perl migration completion (static)

- Re-inventoried maintained scripts: no project-owned `.py`/`.pyw` or Python
  shebang remains; `i2pdctl` is the only converted project client.
- Added a Test::More wrapper for `validate-branding.pl` and CMake/CTest entries
  for Perl syntax, i2pdctl prove tests and branding validation.
- Added an optional `.perlcriticrc` profile and documented the non-CPAN,
  base-Perl/OpenBSD constraints.
- Updated CI Perl steps and migration/tool documentation.
- No local compiler/test execution; all checks are prepared for manual CI.

### Session 25 — manual CI/test-matrix integration (static)

- Completed CTest registration for the checked-in C++ suite and added
  `interop-static` (26 checked-in Java interoperability fixtures) plus
  `perl-doc-links`. Optional `interop-reference`/`interop-reference-vectors`
  targets use a read-only pinned Java checkout supplied at configure time.
- Added CTest labels for i2p, security, memory, IPC, TUI, torrent, tools, Perl,
  interoperability, platform, Android, static and smoke groups. The default
  full CTest run still executes every registered target.
- Registered Linux-only sandbox/IPC tests only on Linux and OpenBSD
  policy/imsg tests only on OpenBSD. Added `android-config-static` and
  `android-english-only` CTest checks and an optional one-input `ctest -L fuzz`
  inventory when `WITH_FUZZERS=ON`.
- Expanded manual-only CI: Linux Clang/GCC full CTest with label inventory,
  full Windows MSYS2/Clang64 CTest, Android unit/Lint/arm64 JNI/APK, pinned-Java
  interoperability fixtures, ASan+UBSan and TSan CTest, and a separate
  bounded eight-target libFuzzer workflow. The MSVC job remains focused on the
  TUI/API and install checks.
- Fixed OpenBSD/LibreSSL codec-name collisions by renaming the private IPC
  payload methods to `WriteU8`/`ReadU8` (and wider variants); the wire format is
  unchanged. Added explicit Windows `mswsock`/`ws2_32` links for static tool and
  test consumers.
- Dispatched and monitored the published baseline workflows with `gh` on
  `main@800c8627`: Linux and Android run `39160538632`, Windows run
  `39160540886`, OpenBSD run `39160543186`. Linux/Android failed on the old
  test `U64` call and Android's `UnixDaemon`/`DaemonAndroid` graph; OpenBSD
  failed on undeclared `getexecname()`. The working tree contains the
  corresponding fixes, but these are uncommitted and are not represented in
  the published runs. Windows run `39160540886`'s MSYS2 job failed because the
  old selected-target build omitted `routerinfo` (tools smoke) and the
  named-pipe DACL test used an `OW` ACE that failed before the round trip. The
  working tree builds every target and derives the current process SID for the
  DACL. The Windows MSVC job then failed before configure because
  `windows-latest` could not find the Visual Studio 2022 generator; the job is
  pinned to `windows-2022`. No local Windows run exists.
- Static checks after the integration: zizmor (no findings, 10 suppressed),
  yamllint clean, actionlint clean, `check-doc-links.pl` (98 Markdown files) and
  `git diff --check` clean. No compiler, CTest, sanitizer, fuzzer, Gradle,
  OpenBSD or Windows run was performed locally, and no commit or push was made.

### Session 26 — memory-safety and concurrency audit (static)

- Completed a focused static audit of SSU2/NTCP2, Ratchet/Garlic,
  LeaseSet/netDb, I2CP/SAM/Streaming/Datagram/HTTP proxy, BitTorrent,
  privilege-separated IPC and Android JNI. Six read-only subagents reviewed
  disjoint subsystems without editing or compiling.
- Implemented confirmed fixes: PeerTest payload block filtering and
  uninitialized data-key guards; NTCP2 executor-owned shutdown; Ratchet
  NEXT_KEY/tag budgets, tag-map iterator safety and padding bounds; RouterInfo
  DatabaseStore buffer snapshots; copy-on-write remote LeaseSet publication;
  NetDb startup locking; `router.keys` validation; TunnelTest length checks;
  I2CP null-destination rejection; Datagram/HTTP proxy owning callbacks; SAM
  shared-destination lifecycle; BitTorrent exact-block/zero-buffer/loading-flag/
  block-budget/DHT-iterator fixes; SCM credential UID/GID validation and
  ancillary alignment; fdopen ownership; daemon PID initialization; Android
  JNI exception boundaries and null-safe client Stop/Reload.
- Added/updated static regressions in `test-ssu2-control-plane`,
  `test-routerinfo-update-transaction` and `test-torrents-security`.
- Documented causes, severity, corrections, regression plan and residual risks
  in `docs/security/memory-safety-2026.md`; linked it from
  `docs/security/README.md` and `docs/security-audit.md`. Added
  `TODO.md` MEM-001..MEM-018 and a `ROADMAP.md` memory-safety section.
- Static checks only: `build-system-static`, branding validator, documentation
  links (99 files), CTest/CMake source registration inspection, yamllint,
  actionlint, zizmor and `git diff --check`. No compiler, CTest, ASan/UBSan,
  TSan, fuzzer, Gradle/Android, OpenBSD, Windows or live-peer execution was
  performed. No commit or push was made.
- Residual, explicitly not claimed fixed: SSU2 session address/callback
  cross-thread access and some Asio timer/socket ownership, RouterContext
  address/property mutation locking, I2CP/SAM output sequencing, full
  BitTorrent piece data ownership and secret-lifetime cleanliness.

### Session 27 — RouterContext, torrent containment, TUI config and regression coverage (static)

- Continued the integral modernization with five disjoint implementation
  subagents: RouterContext mutator locking, read-only TUI configuration
  snapshot, torrent filesystem containment, per-tool regressions and privilege-
  separated IPC regressions. The coordinator reviewed every diff and kept the
  existing uncommitted work intact.
- `RouterContext` now serializes address/property/caps/floodfill/MTU/bandwidth
  mutations with signed-buffer rebuilds through `m_RouterInfoMutex` and
  `*Locked` helpers. Added `GetRouterInfoProperty()` for TUI readers and
  `test-routercontext-update-race`. The live `GetRouterInfo()` accessor remains
  documented as a compatibility surface; transport readers, Asio timer/socket
  ownership and the full immutability refactor remain pending TSan.
- Torrent filesystem operations now use lexical containment, anchored
  component-wise `O_NOFOLLOW` traversal (`openat`/`mkdirat`), exclusive
  creation with descriptor-based reservation, exact bounded `pread`/`pwrite`
  I/O, bounded resume reads and symlink-resistant rename/delete checks.
  Corrected the anchored-open leaf return and added overflow/short-read guards.
  Windows pre-open TOCTOU, hard links and rename races remain documented
  residual risks.
- Added `I2PDTUI/1 CONFIG`, a bounded typed allowlist snapshot with no writer,
  secret, arbitrary path or URL. Restored the original `1`-`7` TUI tab
  bindings, appended Destinations `8` and Config `9`, fixed Destinations scroll
  direction and a raw-array `.size()` compile error in the help view.
- Added `tools-regressions` (CMake-only, no network/Perl/Python) covering key
  generation/inspection, b33/offline keys, registration/verification, famtool,
  i2pbase64 and vain bounds. Fixed `b33offlinekeys` to return a failure code
  when its output cannot be written.
- Expanded `test-privsep-linux-ipc` and `test-privsep-log` for malformed frames,
  SCM credentials, descriptor ownership, duplicate/oversized ancillary data,
  queue pressure, close/reuse and pre-opened log descriptors. Cross-UID
  rejection still requires a second account or root and TSan/ASan remain
  CI-only.
- Added `MEM-019..MEM-022`, updated `TUI-008`, `TOOL-006`, `PRIVSEP-013` and
  `TORRENT-SEC-002`, and extended `docs/security/memory-safety-2026.md`,
  `ROADMAP.md` and `docs/files-changed.md`.
- Static checks only: source inspection, `git diff --check`, copyright/SPDX
  preservation review and documentation-link inspection. No compiler, CTest,
  ASan/UBSan, TSan, fuzzer, Gradle/Android, OpenBSD, Windows or live-peer run
  was performed for this session. No commit or push was made.

### Session 28 — C++23 ownership/RAII continuation (static)

- Ran six disjoint implementation subagents over library ownership, daemon
  resources, clients, tools/span usage and OpenSSL/signature ownership. The
  coordinator reviewed all diffs, registered the new tests and kept the
  existing uncommitted tree intact.
- Kademlia `DHTNode` children and `DHTTable::m_Root` now use `std::unique_ptr`;
  insertion/split/promotion/cleanup keep the original traversal and filtering
  semantics. Added deterministic `tests/test-kad-dht.cpp`.
- Garlic `UnconfirmedTags` now holds `std::vector<SessionTag>` and moves a
  `std::unique_ptr` into the confirmation map only when the DeliveryStatus
  clove is actually written; `m_PayloadBuffer` is a `unique_ptr<uint8_t[]>`.
  Reseed compressed/uncompressed buffers use vectors, with zlib teardown before
  CRC/netDb work. Extended `test-reseed-zip-bounds`.
- `UnixDaemon` readiness pipes and startup pidfile use a local move-only
  descriptor guard; early `setsid`/`chdir`/`freopen` failures can notify the
  parent. `I2PControlService::CreateCertificate` uses scoped OpenSSL/`FILE*`
  owners and checks previously ignored ASN.1/PEM/signature returns.
- `ClientContext` SAM/BOB/I2CP servers and `AddressBook` storage use
  `unique_ptr`; client key file buffers and address lookup payloads use
  vectors. `LoadPrivateKeys` now validates file size and short reads. Added
  `tests/test-addressbook.cpp` for in-memory parsing.
- Added `ByteStreamToBase64(std::span<const uint8_t>)`, a `std::span` write
  helper for tools, and span-based x25519/i2pbase64 paths; x25519 OpenSSL
  handles and key arrays are scoped, and tool write/read failures are checked.
- `EDDSA25519Verifier`/`EDDSA25519Signer` and `EVP_MD_CTX` uses now have
  scoped ownership; GOST curve construction and temporary `BIGNUM` parameters
  are RAII; `ElggPow` uses a cleansed vector. DSA/ECDSA random-key helpers now
  report failure. Added a provider-fallback Ed25519 regression.
- Coordinator fixes during review: corrected an ECIES coordinate buffer that
  was too small for P-521, removed an invalid `continue` introduced in
  `ClientContext::LoadPrivateKeys`, validated the keyfile size/read, restored
  the original TUI tab bindings, and added `test-kad-dht`/`test-addressbook`
  to `tests/CMakeLists.txt`.
- Static checks only: source/API review and `git diff --check`; documentation
  link validation remains 99 Markdown files. No compiler, CTest, ASan/UBSan,
  TSan, fuzzer, Gradle/Android, OpenBSD, Windows or live-peer execution was
  performed for this session. No commit or push was made.
- Residuals: Streaming still stores pool-owned `Packet*` in multiple raw
  containers; `GOSTR3411_2012_CTX` buffers the whole message; DSA/ECDSA raw
  `EVP_PKEY` and global crypto precomputation lifetimes remain; `GetEd25519`
  and GOST curve caches are not synchronized; `GetRouterInfo()` live readers,
  SSU2 executor ownership and I2CP/SAM output sequencing remain separate work.

### Session 29 — memory/concurrency correction pass

- Six independent read-only audits covered SSU2/NTCP2, crypto/parsers,
  BitTorrent, IPC/privsep, JNI/async clients and library lifecycle. The
  coordinator then split implementation across disjoint SSU2, crypto,
  BitTorrent, JNI/HTTP, I2CP/SAM/ClientContext, lifecycle and IPC scopes.
- SSU2 now applies a per-state payload-block whitelist before dispatch,
  requires established/identity context for PeerTest/Address/I2NP, bounds
  token/recently-connected/PeerTest caches, supports multi-fragment
  SessionConfirmed, serializes `m_OnEstablished`, isolates the SOCKS proxy
  scratch buffer, uses an exception-safe packet batch guard, checks X25519
  agreement results and returns `bool` plus a packet number from `SendData`.
- ECDSA P-521 DER encoding now uses the exact encoder length (the old
  `keyLen + 8` allocation could be one byte short and overflow). AES wrappers
  are checked/fail-closed, ECIES coordinate storage supports P-521 and rejects
  invalid points/null inputs, X25519 rejects low-order all-zero shared
  secrets, DSA/ECDSA key generation reports failure, and lazy Ed25519/GOST
  caches no longer use unsynchronised check/reset.
- Torrent pieces now have a per-piece mutex for data, block map, loading,
  sending and verification state; hash failures/reset rebuild Missing blocks;
  complete corruption cannot stay complete; resume bitfields do not imply
  verification; frame prefixes are checked before length storage; teardown is
  marshalled to the service and the disk worker is joined.
- I2CP reserves session IDs before insertion and marshals termination/socket
  ownership to its service; SAM serializes accept queues and socket writes;
  ClientContext uses locked snapshots; `RunnableI2CPDestination` defers its
  own join. JNI entries share a try/catch boundary and status uses an immutable
  RouterInfo buffer snapshot. HTTP proxy handlers capture `shared_from_this`,
  enforce a 1 MiB header bound and stop/close handlers deterministically.
- TunnelPool creates replacement tunnels outside its locks, returns snapshots,
  uses atomic counters/latency/TCSR/RNG, and clears runtime/exploratory state
  on restart. Timestamp/NTP, RouterContext service/static-key publication,
  NetDb Start/Stop, Logger lifecycle and UPnP address reads were hardened.
  IPC sends/receives use absolute deadlines; TUI connections and child reaping
  are bounded.
- Registered `test-ssu2-preauth`, `test-torrents-concurrency`,
  `test-http-proxy-limits`, `test-i2cp-session-lifecycle`,
  `test-clientcontext-registry`, `test-sam-stream-lifecycle`,
  `test-tunnelpool-zero-hop`, `test-tunnelpool-detach-race`,
  `test-tunnels-restart`, `test-timestamp-offset`,
  `test-ntp-timesync-lifecycle`, `test-routercontext-service-lifecycle`,
  `test-routercontext-static-keys` and `fuzz-signature-der`; prepared
  `fuzz-torrents-parsers`/`fuzz-protocol-bounds` updates. Tests are not
  executed locally.
- Static checks only: source/diff review, `git diff --check`, CMake test-source
  registration inspection and documentation-link inspection. No compiler,
  CTest, ASan/UBSan, TSan, fuzzer, Gradle/Android, OpenBSD, Windows or live-peer
  run was performed for this session. No commit or push was made.
- Residuals remain explicit: `GetRouterInfo()` live-accessor readers, SSU2
  shared sockets/executor and timer callbacks, Streaming raw packet containers,
  BitTorrent multi-thread piece handoff (mutexed but not single-owner),
  I2CP/SAM output sequencing, `RunnableService` self-join contract,
  `GOSTR3411_2012_CTX` whole-message buffering, Windows reparse/hard-link
  windows, Android emulator/device tests and all dynamic CI validation.

### Session 30 — dependency-floor and compatibility cleanup (static)

- Raised the enforced floors to OpenSSL 3.0.2, LibreSSL 4.0 and MiniUPnPc
  2.2.6/API 17. Removed the OpenSSL 3.0.0 SipHash exception, the pre-LibreSSL
  4.0 in-place ChaCha20-Poly1305 copy and the MiniUPnPc API<14/10/8
  branches; API<17 is rejected and the API 18 external-address signature
  remains capability-gated.
- Removed the Windows XP `inet_pton_xp`/`inet_ntop_xp` and `WINVER == 0x0501`
  paths, MSVC-only `sprintf_s` branches, the Android non-Clang and API<24
  polyfills, the legacy LLVM atomic probe, and the `JSON_SUPPORTED`/old-Boost
  and `BOOST_ASIO_HAS_STD_ARRAY`/Bind-placeholder fallbacks.
- Replaced legacy `EVP_MD_CTX_create`/`destroy` aliases with `_new`/`free`
  and removed `OPENSSL_SUPPRESS_DEPRECATED`. Windows endian conversion now
  uses C++23 `std::endian`/`std::byteswap`; Unix/Android use the platform
  `endian(3)` header. The old `LittleBigEndian` fallback was deleted.
- Android keeps UPnP enabled by default. The JNI graph builds the common
  daemon lifecycle and `UPnP.cpp`, links MiniUPnPc, and removes only the
  POSIX privilege-separated supervisor path via `I2PD_NO_PRIVSEP`. The app
  declares the Android 17 local-network permission, requests it at first
  launch and holds a Wi-Fi multicast lock for SSDP. Redundant
  Boost.Atomic/Filesystem/JSON/URL packages remain removed from CI, Debian,
  Docker and the Android vcpkg manifest.
- `build-system-static` now asserts the new floors and the absence of the
  removed branches. No compiler, CTest, sanitizer, fuzzer, Gradle/Android,
  OpenBSD, Windows or live-peer run was performed; all changes remain pending
  manual CI compilation and tests.

### Session 31 — Java 2.13.0 third static interoperability pass

- Rechecked the official stable release on 2026-10-19: `i2p-2.13.0` at
  `9134f808337b401e8e53c73734c81fab04280c9d` remains the latest stable tag and
  GitHub release. The Java tree was not vendored or executed.
- Independent read-only audits covered NTCP2/SSU2, LeaseSet/netDb,
  SAM/I2CP/Datagram/Streaming and crypto/ratchet/Garlic, plus the differential
  harness. Findings were compared with the pinned Java sources and the
  normative specifications.
- Fixed the SSU2 inbound receive baseline (Alice data starts at packet 1 while
  Bob consumed SessionConfirmed as packet 0), removed cleanup code that
  fabricated ACKs across unreceived gaps, ignored ordinary Address blocks for
  external-address publication, and made invalid NTCP2 proxy configuration
  disable only the proxy instead of dereferencing a null server.
- Fixed encrypted-inner `PUBLISHED_ENCRYPTED`, Meta earliest lease dates,
  Meta future-bound semantics and the local inner `UNPUBLISHED` flag. The client
  DatabaseStore path now masks type bits; the client DSRM path uses the strict
  parser and mandatory sender. SU3 now checks file version, exact signature
  length, non-empty signer/content and trailing bytes. Out-of-session
  PeerTest/HolePunch padding is clamped to the 1280-byte SSU2 floor.
- SAM 3.3 ADD/REMOVE now returns `ID=` responses, keeps the primary socket
  open on subsession errors and accepts `STYLE=PRIMARY`; exact DATAGRAM/2/3
  style tokens replace prefix matching. Empty gzip Datagram2/3 payloads are
  delivered. Streaming clamps peer MTU and caps pong payload. ElGamal/AES
  padding is randomized and router incoming-N has a bounded duplicate-ephemeral
  cache.
- Prepared `sam-hello-33-only` and marked the proposal-154 `0x12` fixture as a
  known gap; corrected stale FIXTURES/tunnel-template and residual statements.
  No compiler, CTest, sanitizer, fuzzer, Java peer, SAM client, OpenBSD,
  Windows, Android or live-network run was performed. All changes are static
  and pending CI compilation/tests.

### Session 32 — crypto/ratchet/PQ static audit

- Fixed a compile-time type mismatch in the ECIES tag-message path
  (`ReceiveRatchetTagSetPtr` instead of a derived-type `shared_ptr`).
- NSR and established-session payloads are now structurally validated before
  cloves, ACKs, NEXT_KEY or termination are applied; an invalid DateTime,
  block order/length or malformed tail rejects the whole payload. New Session
  validation requires at least one Garlic clove and a valid DateTime.
- The New Session ephemeral replay cache now stores timestamps, expires after
  five minutes and holds 4096 entries instead of a 256-entry unbounded FIFO.
- Ratchet tag generation accepts the full Java 16-bit range (0..65534) and the
  incoming tag budget is checked before the tag chain advances.
- PQ publication/selection is gated by actual ML-KEM support: LibreSSL no
  longer publishes SSU2 `pq=3`, no-PQ builds ignore peer PQ hints, and
  unsupported hybrid encryption types are not advertised in LocalLeaseSet2.
- SigType 8 now uses the Java-compatible plain Ed25519 primitive over supplied
  bytes; RedDSA private scalars are wiped on destruction.
- Extended static regressions for NS/NSR payload validation, the tag-index
  boundary and Elligator2 boundary-1/high-bit handling. These tests are
  registered but were not executed; no Java vector or dynamic validation
  exists for the current tree.

### Session 33 — client protocol static pass

- SAM large-frame handling: control buffer raised to 49 KiB and receive
  buffer to 64 KiB, covering valid 31/32 KiB DATAGRAM/RAW payloads without a
  zero-length read loop.
- SILENT STREAM CONNECT now starts the I2P->client direction independently of
  the status write, with one outstanding read.
- STREAM ACCEPT on a 3.3 STREAM subsession arms the destination for
  `LISTEN_PORT` (falling back to `FROM_PORT`), including queued-acceptor
  promotion; the C++-only 3-second queued-accept expiry was removed.
- SAM signature types now reject unsupported numeric values, accept
  case-insensitive names and expose `EdDSA_SHA512_Ed25519ph`.
- I2CP b33 association now uses the unblinded signing key carried by the b33;
  TYPE_KEY no longer overwrites TYPE_DEST identity/auth/secret/expiry.
- HostReply types 2-4 now return standard LeaseSet2 properties instead of an
  always-empty mapping; mixed-case `.B32.I2P` is recognized.
- Datagram1 no longer dereferences a null session when the table is full;
  inbound Datagram2/3 no longer mutates an existing outgoing version;
  Datagram3 is hash-only and prefers a fresh LeaseSet.
- Streaming accepts a first contact without a cached LeaseSet by requesting it
  asynchronously, applies inbound SYN MTU, authenticates offline delegation
  even when a LeaseSet is cached, and requires a signed CLOSE/RESET when no
  LeaseSet is available.
- Static regression assertions were extended. No compiler, CTest, Java peer,
  sanitizer, fuzzer or live SAM/I2CP run was performed; all changes remain
  pending CI compilation and tests.

### Session 34 — supervisor process for the logging boundary

- Added a real exec'ed supervisor process (`--privsep-worker supervisor <fd>`).
  The core starts the supervisor; the supervisor owns the log worker, forwards
  `ConfigureLog`/`LogRecord`, and restarts the log child.
- The supervisor never opens the log file. The log worker retains the
  filesystem capability; the supervisor has no network and no direct
  persistent-write capability.
- Generalized worker spawning to carry an explicit role, added
  `SpawnSupervisor`, and kept the core's bounded monitor only for replacing a
  dead supervisor.
- Added proxy configure/reattach paths so neither the core nor the supervisor
  opens the log descriptor in the normal production path; the legacy
  `ConfigureLogFd` descriptor test path remains.
- Updated the privilege-separation, IPC, process-capability, threat-model and
  architecture documents. Storage, network and crypto workers, router-core
  confinement and full negative OS-capability validation remain pending.

### Session 35 — Android English-only interface

- Removed all 16 language-qualified Android resource directories
  (`values-cs` … `values-zh-rCN`) and the non-English Fastlane listings.
  Non-language `values-night`, `values-sw600dp` and `values-v31` resources were
  preserved.
- Added `res/xml/locales_config.xml` with only `en`, referenced it from
  `AndroidManifest.xml`, and added `androidResources.localeFilters += ["en"]`
  to the app Gradle configuration.
- Removed the dead Java `appLocale`/JNI `setLanguage` path and the Android
  native language setter. `UiFormat` now uses English number formatting and
  log case-folding uses `Locale.ROOT`; no process-wide `Locale.setDefault()`
  change was introduced.
- Added `EnglishOnlyResourceTest` for non-English `Configuration` contexts and
  the compile-free `android/tools/check-english-only.pl` guard; the manual
  Android CI job runs the guard before Gradle.
- Updated Android documentation, `TODO.md`, `ROADMAP.md` and this progress log.
  No Gradle, AAPT2, Lint, unit/device test, APK build or native build was run.

### Session 36 — OpenBSD native privilege separation

- Added `privsep/OpenBSDSandbox.{h,cpp}` with compiled per-role policies for
  supervisor, log, storage, network, crypto and admin. Policies are inspected
  by capability predicates and applied with `unveil` before `pledge`; no
  `execpromises` and no reliance on sandbox state surviving `execve`.
- Supervisor/log workers are now spawned in `DaemonUnix::start()` before the
  runtime OpenBSD pledge/unveil. The startup pledge keeps `proc`/`exec` only
  for that window; the runtime policy omits `exec` and retains `proc` only for
  bounded supervisor reaping. The core no longer refuses
  `privsep.log` when the sandbox is enabled, but also no longer forks a
  replacement supervisor after the sandbox is installed. The supervisor owns
  bounded log-worker replacement.
- The supervisor pre-resolves its executable, then installs
  `pledge("stdio unix proc exec")`; it never opens the log path. The log worker
  installs its own file/syslog/stdout policy after `execve`, including
  `/dev/log` for syslog and no log-path rule for pre-opened descriptors.
- The core no longer unveils the logfile or `/dev/log` when the log worker owns
  the sink. Runtime `openbsd.pledge_file` is additive but may not add `proc` or
  `exec`. Worker entrypoints refuse euid 0.
- Added `privsep/OpenBSDImsg.{h,cpp}`: an OpenBSD `imsg` adapter carrying the
  versioned frame, bounded poll deadlines, `getpeereid` peer identity and one
  `SCM_RIGHTS` descriptor.
- Added `tests/test-privsep-openbsd-policy.cpp` (storage cannot open AF_INET;
  network cannot create a persistent file; policy separation; pre-opened
  descriptor validation) and `tests/test-privsep-openbsd-imsg.cpp`. Both are
  registered in CTest for the manual OpenBSD job.
- Updated OpenBSD/security documentation, packaging comments, TODO, ROADMAP and
  this log. No compiler, CTest, sanitizer, OpenBSD or live-service run was
  performed; storage/network/crypto/admin backends remain unimplemented in the
  core.

### Session 37 — Linux IPC/sandbox backend continuation

- `LinuxSandbox` now applies to the Linux log worker and supervisor. The log
  worker opens its configured path before sandboxing and adopts the descriptor,
  so Landlock can restrict writes to that single path without granting the
  parent directory.
- Added role-specific socket policy: storage denies all new sockets; log and
  supervisor deny AF_INET/AF_INET6 while allowing AF_UNIX/socketpair; network
  retains sockets but has no persistent-write allowance.
- Capability reduction now clears effective/permitted sets, ambient
  capabilities and the bounding set.
- Added optional `unshare(CLONE_NEWNET)` with non-fatal fallback when the
  kernel or caller lacks CAP_SYS_ADMIN; seccomp remains the storage/log network
  denial.
- Added cgroup v2 `memory.max`/`pids.max`/`cgroup.procs` support for explicitly
  delegated cgroup2 paths, with NUL/absolute-path validation, `O_NOFOLLOW` and
  filesystem-type checks.
- `ChildProcess::Stop` uses `pidfd_send_signal` when available, falling back to
  `kill` for the pidfd-unsupported path.
- Expanded `test-privsep-linux-sandbox` with storage socketpair denial,
  effective-capability checks, log AF_INET-deny/AF_UNIX-allow and supervisor
  socketpair checks. No local compiler/CTest/Landlock/seccomp run was made.
- memfd sealing was intentionally not added: bounded versioned
  `SOCK_SEQPACKET` records and one `SCM_RIGHTS` descriptor already provide the
  required transfer without a new shared-memory lifecycle.

### Session 38 — Windows worker IPC and process policy

- Added `privsep/WindowsIpc.cpp` for the Windows worker transport: local
  message-mode `CreateNamedPipeW` with `PIPE_REJECT_REMOTE_CLIENTS`,
  `FILE_FLAG_FIRST_PIPE_INSTANCE`, bounded overlapped I/O and the common
  versioned `Message` frame. Remote UNC pipe names are rejected.
- Added an SDDL/protected DACL (`D:P`) granting only the process user and local
  SYSTEM. No Everyone, Authenticated Users or anonymous ACE is used. The
  daemon-side peer check opens the client process token and requires the same
  user SID; PID/session/elevation are exposed for diagnostics.
- Added `WindowsAllowsOperation` role allowlisting, restricted-token creation
  (`CreateRestrictedToken`/`DISABLE_MAX_PRIVILEGE`), Job Object limits
  (`KILL_ON_JOB_CLOSE`, process/memory), and suspended spawning with mitigation
  attributes, `bInheritHandles=FALSE`, and no handle duplication.
- AppContainer/LPAC is capability-probed through documented `userenv.dll`
  exports but is explicitly fail-closed if requested until token/profile/ACL
  setup is validated. No undocumented ALPC API is used; `ncalrpc` remains an
  evaluated alternative.
- Added `tests/test-privsep-windows-ipc.cpp` and the Windows-only CMake target:
  DACL inspection, local/remote pipe names, message round trip, peer SID/PID,
  unknown-type rejection, recovery, restricted token and Job Object creation.
- Updated Windows security/architecture/IPC/TODO/ROADMAP documentation. No
  compiler, CTest, service-account, cross-user or AppContainer/LPAC run was
  performed; storage/network worker backends remain unwired.

### Session 39 — CMake/POSIX Make component audit

- Split native IPC/supervisor/sandbox sources out of the daemon source list into
  the `i2pd_privsep` CMake component; `i2pd` links it and platform link
  interfaces (OpenBSD `util`, Windows `advapi32`) are target-scoped.
- Completed the root POSIX Make front-end with `configure`, `build`, `all`,
  `router`, `tui`, `tools`, `test`, `check`, `install`, `clean` and `dist`.
  `tests/Makefile` now configures with `BUILD_TESTING=ON`, builds and runs
  CTest instead of assuming an existing build tree.
- Added `I2PD_INSTALL_DOCS`, explicit Android-only JNI validation and docs
  installation gating. Added platform component/dependency documentation.
- Extended `build-system-static` to scan root/tests Makefiles, top/tools/tests/
  Android CMake, generator defaults, required POSIX targets, C++23, component
  markers, source globs, download rules and hard-coded installation paths.
- Repaired `test-privsep-log` to compile the Linux/OpenBSD sandbox source it
  now calls.
- Static validation: `cmake -P tests/check-build-system.cmake` passed; `git
  diff --check` and documentation-link validation passed. No local CMake
  configure, build, install, CTest, compiler or generator was executed.

### Session 40 — Native TUI navigation and administrative IPC

- Removed the remaining functional gap against the former web console without
  restoring any browser surface: the TUI now has dedicated Connections,
  router-directory and LeaseSet-directory views, log text search and a
  bandwidth history panel.
- Added bounded `CONNECTIONS`, `NETDB-ROUTERS` and `NETDB-LEASESETS` protocol
  requests. Connections are filtered from a transport snapshot that contains
  only peers with live sessions; netDb lists use bounded container snapshots
  and expose no keys, addresses or raw records.
- Added the only two router-wide administrative writes in protocol version 1:
  `ADMIN TRANSIT ON|OFF` (atomic future-transit acceptance) and
  `ADMIN LOGLEVEL` (atomic logger threshold). Unknown actions, values and
  trailing arguments are rejected; generic configuration, reload, reseed,
  shutdown, file and command operations remain unavailable.
- Updated the ncursesw and Win32 shared client: inactive header compression on
  narrow terminals, `0` for Connections, `t` confirmed transit toggle, `n` for
  netDb directories, `/`/`c`/`L` for logs, transport names for peers, and a
  60-sample 15-second traffic history. The Win32 console/VT backend and local
  named-pipe client remain the Windows frontend; no ncurses dependency was
  added there.
- Audit result: no web admin console, HTML, JavaScript or web-console assets
  remain. `docs/stylesheets/brand.css` is MkDocs documentation styling and the
  HTTP proxy error page is an I2P protocol service, so neither was removed.
- Prepared protocol vectors in `test-tui-protocol` and live-socket dispatch in
  `test-tui-ipc-regression`. No local compiler, CTest, ncurses/Win32 console,
  socket, ACL or terminal run was executed.

### Session 41 — Torrent parity, storage domain and I2P-only audit

- Added explicit `TORRENT-PAUSE`/`TORRENT-RESUME` protocol commands and
  `Pause`/`Resume` control kinds. The engine maps them to the existing
  stop/start paths, which close/announce peers and rejoin trackers; `p` in the
  TUI now reports pause/resume rather than start/stop.
- Moved startup `.torrent` enumeration and bounded reads off the torrent
  network/control service onto the dedicated storage service. Parsing and
  session insertion are posted back to the network service; the storage domain
  never opens peer streams.
- Replaced network-thread filesystem containment with the purely lexical
  `IsPathLexicallyWithinRoot`. Storage revalidates with `IsPathWithinRoot` and
  anchored no-follow opens before create/read/write/rename/delete. Torrent
  display paths use lexical relative paths.
- Added `IsI2PTrackerHost`, used by the tunnel constructor, `AddTracker` and
  `RequestTracker`; it rejects non-`.i2p`, malformed, port-bearing, credential
  and trailing-dot hosts. Peers and datagram trackers still resolve only
  AddressBook I2P destinations.
- Added protocol vectors for pause/resume and torrent-security tests for
  tracker host validation and lexical root containment.
- Documentation and TODO/ROADMAP distinguish the implemented in-process
  storage execution domain from the still-pending OS process separation.
- No compiler, CTest, traffic capture, sanitizer, fuzz, live peer, TUI or RPC
  execution was performed.

### Session 42 — i2pd-tools inventory, memory hardening and packaging

- Confirmed the pinned upstream inventory (`PurpleI2P/i2pd-tools`
  `ca7ece8da86aa80b58fce6fa64375bfd75cbf843`): 15 native tools plus the Perl
  I2PControl client, no submodule and no tool Makefile.
- Added `docs/tools/provenance.md` with the upstream commit, imported/local-only
  file split, BSD-3-Clause notices, per-executable source/test mapping and the
  no-download/no-submodule policy. Linked it from the tool indexes and MkDocs.
- Extended `tools-inventory` to check each executable, its source, shared helper
  files and every path in `contrib/upstream/tools-files.txt`.
- Added a deterministic scripted `autoconf_i2pd` transcript and oversized-stdin
  coverage for `b33address` to `tools-regressions`.
- Bounded `b33address` stdin and `autoconf_i2pd` output-filename input.
- Hardened native key output: POSIX `WriteFile` now uses `O_NOFOLLOW`, creates
  owner-only files and tightens existing files; Windows uses best-effort owner
  read/write permissions. `famtool` restricts its unencrypted PEM private key.
- Wiped `b33offlinekeys` private buffers on success and failure, cleansed
  `x25519` private keys and temporary base64, and added guards for sensitive
  buffers.
- Updated Debian (`usr/bin/`, correct TUI/tool options, ncurses/Perl deps),
  RPM specs (tools/TUI/i2pdctl installation and files), and the legacy Windows
  build/Inno helper to build and package the TUI and native tools.
- Updated tool/security/build docs and `TODO.md`/`ROADMAP.md`. No local build,
  CTest, package build, Windows run, sanitizer or fuzz execution was performed.

### Session 43 — Perl portability, proxy isolation and install-layout checks

- Re-ran the maintained-script inventory: no project-owned `.py`, `.pyw`,
  `.pyi` or Python shebang remains. `i2pdctl` is the only converted client.
- Added `tools/tests/perl-portability.t`, a Test::More guard that scans files
  without extensions for Python shebangs, checks the maintained Perl inventory,
  rejects non-core modules and unsafe process/temp/chmod patterns, and verifies
  that maintained build consumers contain no Python invocation.
- Hardened `i2pdctl` proxy isolation: `HTTP::Tiny` now runs with proxy
  environment variables removed and explicit proxy options disabled, so the
  management password and token cannot be sent to an inherited proxy. Added a
  regression that points all proxy variables at an unreachable endpoint.
- Restricted `openssl` discovery to absolute `PATH` entries and added Windows
  path-separator/`.exe` handling; relative current-directory helpers are no
  longer executed.
- Added repeated-request idempotence coverage to `tools/tests/i2pdctl.t`.
- Added CMake syntax checks for all maintained Perl programs/tests, a private
  prefix `perl-install-layout` test for the installed `i2pdctl` shebang, mode
  and `perl -c` syntax, and an opt-in `I2PD_PERL_CRITIC` CTest target.
- Updated Perl migration/client/testing/CI documentation and status files.
  No local `perl -c`, `prove`, CTest, build, package or OpenBSD execution was
  performed; CI remains the dynamic validator.

### Session 44 — Android toolchain refresh and service lifecycle

- Rechecked the current stable toolchain against official release data:
  Gradle 9.8.1, AGP 9.4.1, compile/target SDK 37, Build Tools 36.0.0, NDK
  r30 LTS `30.0.16248370`, Java source/target 25 and the pinned AndroidX
  versions (all current stable on 2026-10-19).
- Updated the app NDK pin, README, build guide, Java 25 rationale, CI SDK
  installation and the static toolchain guard. The Android JNI CMake graph
  continues to build only `i2pd_jni`, with `WITH_BINARY=OFF`, no torrents and
  no `i2pd_privsep`; no Android process separation is claimed.
- Replaced `DaemonWrapper`'s `java.util.Timer` graceful-stop thread with a
  `RouterService`-owned deadline. The service schedules the deadline on its
  main handler, enqueues the native stop on its single worker, and
  `DaemonWrapper` now publishes an explicit `stopping` state before native
  teardown. This aligns the Java lifecycle with `DaemonAndroid.h` and the
  native `NativeCallGuard`.
- Removed the timer-thread JNI path, cancelled the deadline on explicit stop,
  timeout, restart and service destruction, and made the stop state visible to
  the UI and notification.
- Modernized remaining legacy `switch` statements in the status/config
  parsers and added the `stopping` presentation assertion.
- Updated tests/guards: `android/tools/java25-probe/check-config.sh` now checks
  Build Tools, NDK r30, the absence of `java.util.Timer` and the service
  deadline/stopping contract; `UiFormatTest` covers the new state. The
  English-only guard now ignores explanatory comments instead of treating a
  documented `Locale.setDefault` non-use as a runtime override.
- No Gradle, javac, D8/R8, ART, APK, Lint, CTest, sanitizer or device test was
  executed. No commit or push was made.

### Session 45 — Android GUI, accessibility and control-state second pass

- Coordinated read-only UI, architecture and security audits. The audits
  identified a `ClassCastException` in the Logs empty state, a white toolbar
  logo on the light theme, stale zero counters during `stopping`, inconsistent
  control enablement and missing tablet content constraints.
- Fixed the Logs empty-state container/text split, added a live empty message,
  search debounce, result summary and screen-reader hiding of the empty list.
- Tinted the toolbar mark with the theme `colorOnSurface`, added a global
  service-disconnected banner with Retry, navigation dividers and the
  `Connections` destination label.
- Added lifecycle-based `canStart`/`canStop`/`canRestart`/`canGraceful`
  predicates, `RouterSnapshot.hasStatus()`/`hasLiveMetrics()` and
  `UiFormat.setTextIfChanged`; a started router with a failed status snapshot
  remains stoppable, while `stopping` and status-less snapshots no longer show
  zero counters as real data.
- Added a capped, centred tablet content container (`MaxWidthLinearLayout`,
  840dp), explicit disabled initial controls, section headings, graceful-stop
  progress, 15-second rate presentation, a dashed chart series and chart-cursor
  restoration.
- Added pure-JVM coverage for live-metric/unknown-status/error-stop behavior
  and a device smoke-test assertion for the Logs empty state.
- Static checks passed: all Android XML parsed with `xmllint`, the English-only
  guard, Java 25 config guard, string/id reference checks,
  `git diff --check` and documentation link checking. No Gradle, javac, D8/R8,
  ART, APK, Lint, CTest, sanitizer, TalkBack, tablet or device test was
  executed. No commit or push was made.

### Session 46 — Upstream documentation integration and coherence pass

- Rechecked `PurpleI2P/i2pd_docs_en` at `30ddc4c7c74e055fc858ecff3ae6f69d1a73ab5e`;
  it is still the current `master` commit. The tree has 46 files, no HTML pages
  and seven images; the only inline HTML was the torrent-RPC anchor form.
- Added `contrib/upstream/docs-path-map.txt` for the two renamed BitTorrent
  pages and extended `sync.sh --docs` to verify upstream coverage, missing
  local targets and HTML sources before diffing. The helper remains read-only.
- Removed the duplicate `docs/interop-matrix.md`; `INTEROPERABILITY.md` is now
  the single current Java interoperability matrix. The old flat torrent guide
  remains removed in favour of the nested fork page.
- Aligned the installation guide with the supported CMake platforms, corrected
  the transit-tunnel default and enabled the Android UPnP default, replaced
  stale C++11/GNU-make
  contribution rules, and linked the C++23, IPC, TUI, BitTorrent, Android,
  CMake, Perl and OpenBSD guides from the main indexes.
- Extended the offline documentation checker to validate local heading and
  `{#anchor}` fragments. `sync.sh --docs` reports `Coverage OK`; the link/brand
  checks pass.
- No MkDocs build/publication, compiler, CTest, Android or device run was
  performed. No commit or push was made.

### Session 47 — Firewall generator hardening and documentation alignment

- Kept the existing common `Endpoint` model and independent renderers for
  legacy `-f` fragments, full iptables/ip6tables commands, nftables, Windows
  Defender PowerShell and OpenBSD PF. No renderer executes a firewall command.
- Added `IsValidEndpoint` at the rendering boundary. Manually assembled
  endpoints, non-canonical addresses, invalid transports and IPv4/IPv6 family
  mismatches are rejected even when a caller bypasses `MakeEndpoint`.
  Address validation now also rejects IPv4 private/CGNAT/`0/8`/`240/4` and
  IPv6 ULA/site-local ranges; introducer-only SSU2 addresses are skipped rather
  than treated as malformed.
- Added invalid-transport and rendering-boundary regressions, lower/upper port
  boundary coverage and per-backend malformed-RouterInfo CLI checks.
- Clarified that `-6` selects published IPv6 addresses in every output mode,
  while `-y` separately selects the Yggdrasil address; updated the tool README
  and complete `routerinfo` guide examples accordingly.
- Static checks only: no compiler, CTest, `iptables-restore`, `nft`, `pfctl`,
  PowerShell `-WhatIf` or firewall appliance was executed. No commit or push.

### Session 48 — Android UPnP default-on

- Re-enabled `WITH_UPNP` for the Android JNI graph and added the common
  `Daemon.cpp` lifecycle, `UPnP.cpp`, I2PControl and local TUI server sources
  to `i2pd_jni`; MiniUPnPc is linked and the Android vcpkg manifest includes
  the port.
- Added `I2PD_NO_PRIVSEP` guards so the Android build omits the POSIX
  privilege-separated log supervisor without changing desktop Unix, OpenBSD
  or Windows behavior.
- Set `upnp.enabled = true` and `upnp.name = openi2pd` in the bundled Android
  configuration and restored the Settings toggle/forward-name controls.
- Declared `CHANGE_WIFI_MULTICAST_STATE` and Android 17's
  `ACCESS_LOCAL_NETWORK`, request the runtime permission before the first
  auto-start, and hold a Wi-Fi multicast lock while the router runs.
- Static checks only: branding, documentation links, build-system policy,
  Android Java 25 configuration and English-only resources pass. No JNI link,
  Gradle/APK/Lint, permission-flow/device or gateway-discovery run was
  performed. No commit or push was made.

### Session 49 — CTest subsystem matrix and manual CI dispatch audit

- Registered Linux sandbox/IPC tests only on Linux and OpenBSD policy/imsg
  tests only on OpenBSD, avoiding unconditional Linux headers and tests in the
  OpenBSD graph. Added i2p/security/memory/IPC/TUI/torrent/tools/Perl/interop/
  platform/Android CTest labels, compile-free Android CTest checks and optional
  one-input `ctest -L fuzz` entries for `WITH_FUZZERS=ON`.
- Extended `build-system-static` to require every `tests/test-*.cpp` and
  `tests/fuzz/*.cpp` source to be named in `tests/CMakeLists.txt`.
- Added a Linux CI per-label CTest inventory step; all workflows remain
  `workflow_dispatch`-only. ASan+UBSan, TSan and the manual fuzzing workflow
  remain prepared but unpublished.
- Dispatched the published `main@800c8627` workflows with `gh` and collected
  logs. `39189489953` (Linux and Android) failed in the old revision at
  `PayloadWriter::U64` and the Android `UnixDaemon` graph; `39189492769`
  (Windows) failed MSYS2 at missing `routerinfo`/the pipe DACL assertion and
  MSVC at the unavailable `Visual Studio 17 2022` generator; `39189495752`
  (OpenBSD) failed at undeclared `getexecname()`. These results belong to the
  published revision, not to the local uncommitted corrections.
- A corrected-revision rerun is blocked because no commit or push was made.
  The fuzzing workflow and sanitizer matrix are not present on published
  `main`, so they cannot be dispatched remotely yet.
- Static checks: documentation links, branding, build-system policy,
  **actionlint**, **yamllint** and **zizmor** pass. No local compiler, CTest,
  sanitizer, fuzzer, Gradle/Android or platform run was performed; no commit
  or push.

## 2026-10-19 — native I2PSnark parity increment (static-only)

- Initial `git status`/diff were clean at actual HEAD `c8f2eba1`; the historical
  baseline described earlier in AGENTS is not the current checkout HEAD.
- Split engine, DHT/PEX, comments, Android, TUI and static review work between
  specialized subagent assignments; coordinator integrated control/CMake/CI
  and documentation. No overlapping file edits were authorized.
- The complete verified Java clone was absent. Inspected explicit read-only
  source excerpts at pinned stable commit `9134f808337b401e8e53c73734c81fab04280c9d`;
  remote annotated tag was peeled and checked. Master at inspection was
  `8416c8771d9c62c24ac753706cd1746ad24d7ff3`, 197 commits ahead; selected DHT,
  metadata and v2-related deltas reviewed, not an exhaustive post-tag audit.
- Added multi-tracker/strict magnet parsing, BEP 12 preservation, independent
  v1 metadata builder, paused tracker add/remove IPC/TUI controls and immutable
  metadata snapshots for queued writes. Explicit tracker lists override common
  tracker fallback. Grouping persists; full tier failover remains pending.
- DHT now keeps opaque bounded foreign tokens, uses CSPRNG transactions/tokens,
  validates reply identity binding before consuming requests, and rejects
  malformed packets/ports. PEX/DHT extension parsing requires full dictionaries.
  Private metadata closes pre-metadata peers and suppresses later DHT/PEX;
  late tracker results recheck current authorized membership.
- Added independent bounded ut_comment codec/store (disabled by default,
  unadvertised). No GPL Java implementation copied; file-specific public-domain
  notices and the directory GPLv2 reference are recorded in the matrix.
- Enabled native torrents in Android/CMake and Android dependency manifests;
  added service/JNI bounded controls and Material list/magnet/share/details/
  pause/resume/remove/DHT panel. Uses private storage, not SAF. This is still
  same-process networking/storage; OS capability separation is not complete.
- Prepared four new CTest targets, TUI/Android protocol regressions and expanded
  parser fuzz input coverage; registered sources and an explicit manual CI
  compatibility step. No workflow triggers changed.
- Static checks only: `git diff --check`, source/interface review, JSON/XML
  parsing. No compiler/build/configure/CTest/Gradle/test binary, sanitizer,
  fuzzer or live peer/device execution. No commits or pushes.
- Status: implemented by static analysis for the scoped changes; pending CI
  compilation; pending CI tests. Creation from disk, advanced file selection,
  limits, comment exchange/UI, SAF, true worker separation and all other open
  modernization obligations remain open under SNARK-001..SNARK-008.

## 2026-10-19 — native init services and Debian packaging (static-only)

- Reconnaissance subagents audited the existing `contrib/` service samples,
  `debian/` packaging, CMake install graph, daemon lifecycle/signals and
  privilege-separation boundaries. The daemon already defaults to foreground;
  systemd readiness and full worker separation are not implemented.
- Replaced the forking systemd unit with a native `Type=exec` service using the
  `i2pd` account, State/RuntimeDirectory, journald, bounded restart, SIGINT
  graceful stop and IPC-compatible hardening. Added the `--foreground`
  override and pass it from every native definition so a local
  `daemon=true` cannot detach a supervised process. Added native GNU Shepherd,
  OpenRC, runit (with a separate `svlogd` service), s6/s6-rc and LSB SysVinit
  definitions. Existing dinit was modernized to foreground supervision; the
  OpenBSD rc.d path remains a documented self-backgrounding legacy exception.
- Added `I2PD_SERVICE_MANAGER`, `I2PD_SERVICE_DIR`, `I2PD_SERVICE_CONFDIR` and
  the `Service` CMake component. Exactly one manager is installed; `none` is
  the default. `tests/check-services.cmake` validates the static service
  contract and is registered as `services-static`.
- Modernized Debian packaging for debhelper 13: corrected the CMake TUI option,
  installed into `debian/tmp`, split `i2pd` and `i2pd-tui`, added explicit
  install/docs/manpage manifests, debhelper systemd/SysV/tmpfiles integration,
  a real tmpfiles file, autopkgtest smoke checks, DEP-5 metadata and NEWS.
- Added a manual `Packaging and service validation` workflow. It runs the
  service contracts and shell syntax checks, builds both Debian packages,
  runs lintian and executes the DEP-8 smoke test in an unshare testbed.
- Static checks only: `cmake -P tests/check-services.cmake`,
  `cmake -P tests/check-build-system.cmake`, `sh -n`, shellcheck,
  `systemd-analyze verify` (host-only; `/usr/bin/i2pd` absent), Perl syntax and
  `git diff --check`. `systemd-analyze` and all service/package lifecycle
  checks remain pending CI. No build, configure, package build, lintian,
  autopkgtest, service start or daemon execution was performed locally. No
  commit or push.

## 2026-10-19 — selective upstream openssl synchronization (static-only)

- Reviewed the complete 2026-10-18/19 UTC window on
  `PurpleI2P/i2pd:openssl` at tip `472ef88b`, including author and commit dates,
  parents, files and patches. The history is recorded in
  `docs/upstream-sync.md`; this was a review record, not a bulk merge.
- SSU2 RelayRequest/RelayIntro/RelayResponse/PeerTest guards were already
  present and stronger in the fork. Added the explicit established-only
  PathChallenge dispatch helper and test, and documented the required
  `Introduced` RelayResponse exception.
- Applied the remaining I2PTunnel `std::stringstream::view()` response/IRC
  conversions from upstream `e3856a38`, preserving the earlier request-side
  header limits. Compacted incomplete HTTP header lines so the retained buffer
  measures only the partial line instead of re-appending the parsed prefix.
- Applied the NetDb general peer floor change to 0.9.57 for Debian 12
  interoperability while retaining the 0.9.62 high-bandwidth floor; added
  compile-time policy assertions.
- Applied stopped-torrent piece rejection through `CanAcceptPieceBlock`, and
  changed completed-piece persistence to a weak capture with an authoritative
  `TorrentsTunnel::FindTorrent` membership check. The removal-after-check race
  and `.openi2pd-state` cleanup remain open.
- Omitted upstream `942d6907` because it modifies the deliberately removed
  browser torrent console. Omitted upstream `ec19bfb5` because copying its
  3.0.0 release metadata would replace the fork's release identity and package
  history; the fork remains 2.61.0/0.9.70 pending a separate release decision.
- No local compiler, CMake configure/build, CTest, sanitizer, fuzzer, package,
  service, Android or live-peer run was performed. New tests and changes are
  pending manual CI compilation and tests. No commit or push.

## 2026-10-19 — intensive I2PSnark native implementation continuation

- Recovered a clean checkout at actual HEAD `0cf32519`. Earlier-session work
  had already entered this baseline, including disk creation, priorities,
  comment negotiation/persistence and Android admin controls. No commit or
  push was made by this continuation. Recognition identified stale tracking
  text; existing correct engine behavior was retained.
- Used separate reconnaissance/core, desktop TUI and Android assignments,
  followed by disjoint implementation/review scopes. No local compilation,
  configuration, compiler syntax checks, CTest, Gradle, test binaries,
  sanitizer, fuzzer, Java peer or device execution occurred.
- Disk creation now selects automatic or explicit bounded power-of-two piece
  sizes, carries private status and tracker tiers through typed IPC, reports
  byte/file progress and accepts cancellation. TUI n remains basic; N is
  advanced, K cancels. Existing safe streaming traversal/hash logic is reused.
- Desktop Files now exposes indices and priorities with f. A Comments tab
  supports c ON/OFF, C explicit rating/pseudonym/text and H local moderation.
  Prompts recheck torrent ID, infohash and session identity; no payload access
  or engine/network handles enter the TUI.
- Added Android SAF creation from a document or tree: bounded enumeration,
  incremental cross-file hashes, explicit descriptor cancellation, private/
  tracker/size options, native CreateV1 metainfo and CreateDocument export.
  Service work uses a separate executor; ViewModel retains results across
  rotation. URI-backed downloads/seeding and process-death recovery are not
  implemented by this export feature. Instrumented JNI regressions prepared.
- Added a real exec-ed payload storage worker and connected it to TorrentFile
  IO via `isolatedstorage=true`. Linux uses authenticated inherited IPC,
  one delegated root FD, bounded numeric handles, read-only/writable modes,
  <=16KiB operations, timeouts, durable writes and denied external networking.
  Linux also confines reads to the root and denies x32/socket, io_uring and
  descriptor-stealing bypasses. Unsupported requested backends fail closed;
  the default and remaining parent IO are explicitly not globally isolated.
- Fixed bencode constructors to encode empty strings as 0:, order keys and
  reject duplicate constructed dictionary keys. This is necessary for empty
  foreign DHT tokens and anonymous/rating-only ut_comment entries. Existing
  imported info dictionaries remain byte-preserved.
- Added stopped-service/admin-load/paused-announce guards, preserved existing
  removed-torrent persistence guards, and reviewed storage-root identity for
  multi-file torrents, shutdown draining and fallback behavior.
- Integrated worker source/client/test and library export dependencies in
  CMake. Existing manual workflows retain their triggers; Android now also
  builds the instrumentation APK. Creator/IPC/comment/storage regressions and
  local-state/request fuzz input coverage are prepared, not executed.
- Updated compatibility/protocol matrices, TODO, ROADMAP and AGENTS; added
  local-state and storage-worker format/boundary documentation. Remaining
  essential work remains open under SNARK-001..SNARK-010.
- Static checks: `git diff --check` and affected source/interface inspection.
  Current implementation is pending CI compilation and pending CI tests;
  nothing in this session is claimed validated by CI.

- Final static review corrected OpenBSD worker startup: reject before fork/exec
  because runtime pledge has removed those promises. Pre-pledge supervisor
  delegation remains pending, without widening pledge. Worker limits (1 TiB per
  file, 4096 handles, tunnel-wide root) are documented compatibility boundaries.

### Native payload continuation — 2026-10-20

- Reused existing uncommitted creator/priorities/comments/TUI/Android changes
  after parallel recognition; actual HEAD is `0cf32519`. No commits or push.
- Added shared TorrentPayloadStorage and adapted native worker/TorrentFile.
  Exact IO is chunked to 16 KiB; failed writes invalidate pieces, withhold HAVE
  and resume updates, and require reacquiring authority. Saving pieces cannot
  be served/published/promoted or lose their pending buffer.
- Connected Android persistent per-torrent SAF trees through JNI and a private
  separate-process AIDL broker. Safe descendants, bounded handles/ranges, regular
  seekable descriptors, partial IO loops, fsync and grant/existence checks are
  implemented. Startup hashes one piece at a time; no POSIX payload allocation,
  .part promotion or native payload deletion applies to provider documents.
- Added fail-closed broker seccomp TSYNC setup. Unsupported seek has no staging
  fallback. Binder death/timeout invalidates the application-lifetime connection;
  full app restart restores grants. Root bindings are frozen until router
  restart, preventing mixed destinations; stale capabilities are not replayed.
- Added OpenBSD pre-pledge supervisor with immutable datadir authority, bounded
  children/launches, ownership/identity checks, own worker sandbox and socket
  handoff only after initialization ACK. Corrected the directory-SCM restriction
  by inheriting root fd4 across exec; core only adds recvfd, no proc/exec/sendfd.
  Worker replacement is explicit, broker failure requires daemon restart.
- Moved isolated native file allocation to storage-worker registration. Parent
  metadata/promotion/router writes remain; full capability separation is pending.
- Registered payload/OpenBSD regressions in CMake; Android path/range fixtures
  and existing manual instrumentation APK build are prepared. Extended manual
  torrent regression selection; no workflow trigger changed. Added SAF/IPC
  architecture and refreshed tracking/matrices. Provider fixtures, cancellation,
  OS-negative runtime checks and all dynamic interoperability remain pending.
- No configure, compiler, build, CTest, Gradle, test binary, sanitizer, fuzzer or
  device run was performed. Current increment is implemented by static analysis,
  pending CI compilation and pending CI tests; no CI validation is claimed.

- Final static integration corrected the private protocol regression boundary
  for message types7..11, and keeps candidates hidden during SAF startup
  verification as well as pending writes. Android manifest XML parsed and
  `git diff --check` passed; neither is dynamic validation.
- JNI bridge initialization now installs a denying backend first and must
  succeed before every native start/restart. Missing context or JNI setup
  failure cannot silently select direct native payload IO.

### Binder recovery and persistent authority continuation

- Implemented stable local SAF root/file identities with immutable authority,
  lazy remote remapping per Binder generation, stale-reply rejection and bounded
  exponential rebind. Lifecycle loss retires a live broker and waits for actual
  death before replacement. No automatic replay of ambiguous writes.
- Added operation tokens, CancellationSignal for query/open, oneway cancel/retire
  independent of data locks, max8 active operations and watchdog retirement.
  One bounded executor and a total5s client deadline prevent thread/queue growth.
  Dead-generation handle cleanup returns without connection waits.
- Prepared AIDL cancellation-wire and DocumentsProvider fixtures for cooperative
  cancellation, ignored cancellation, nonseekable pipes and revoked documents.
  Actual cross-process grant/death/rebind tests remain pending.
- Added worker promotion/removal and bounded chunked atomic metadata transaction
  operations; connected isolated tunnel allocation/promotion/delete, metainfo,
  resume and settings writes. No fallback to parent IO after failure. Metadata
  commit fsyncs file and parent;8MiB cap/16KiB chunks and30s transaction budget.
  Fixed8 staging slots cap SIGKILL remnants to64MiB per metadata directory;
  explicit cleanup is still needed after exhaustion.
- Queued metadata saves recheck session membership before IO. Removed isolated
  torrent settings are deleted through the worker. Failed/corrupted seed pieces
  now clear torrent-wide completion so recovery does not leave false seeding.
- Registered test-torrent-storage-lifecycle in existing manual CI CTest graph;
  extended payload regression for torrent completion invalidation. Updated
  architecture/capability matrix and tracking. Router/DHT writes remain in the
  network-capable parent; global privilege separation is NOT complete.
- No build/configure/compiler/test/Gradle/device invocation, commits or push.
  Implemented by static analysis only, pending CI compilation and CI tests.

- Final static checks: main/instrumentation manifest XML parsing and
  `git diff --check` passed. These checks do not validate Android, Binder,
  seccomp or filesystem behavior; manual CI/device validation remains pending.

### DHT/router persistence and SAF administrative completion

- Implemented isolated DHT persistence callbacks with exact54-byte binary
  records,8MiB limit, pinned read-only worker reads and explicit bounded legacy
  flat-name authorization. Tunnel startup binds callbacks before DHT start;
  backend failure unwinds before starting torrent services.
- Added PersistentStorage core callback and fixed router-state worker. Connected
  daemon privsep.routerstorage before context.Init, covering initial identity
  creation and router.info/NTCP2/SSU2 state without fallback. OpenBSD detached
  bootstrap inherits root4; role installs own sandbox. Startup rollback and
  shutdown close capabilities. Four-name/32KiB authority and serialization
  preserved; failed key persistence aborts instead of ephemeral identity use.
- Configured OpenBSD storage workers now drop unix/recvfd promises after setup,
  retaining only inherited-channel IO plus their root filesystem policy.
- Connected SAF delete-files removal to exact-document capabilities, with
  fresh descriptor identity checks; no recursive deletion. Export now executes
  in the bounded broker with cancellation; interrupted provider output can
  remain partial and is not replayed. Added native Remove wiring and status UI.
- Added/registers test-router-storage and test-torrents-dht-persistence plus
  OpenBSD policy and Android document replacement regressions. Protocol framing
  now accepts private message types12..14 with15 as invalid boundary.
- Static-only review and diff checks; no configure/compiler/build/CTest/Gradle/
  test binary/fuzzer/device run, commits or push. Pending CI compilation/tests.
  Global separation is not complete: other persistent authorities and genuine
  network/router/verifier workers remain implementation tasks.

- Final source checks: Android main/test manifest XML parsed and
  `git diff --check` passed. These are static checks only; no current change
  is described as compiled, tested or validated by CI.

### Session 43 — comprehensive Java feature-parity continuation (2026-10-20)

- Verified the reference again: stable `i2p-2.13.0` at
  `9134f808337b401e8e53c73734c81fab04280c9d`; 2.14.0 was a late-October target,
  not a released stable tag. The read-only clone remains external and is not
  vendored or built.
- Seven independent read-only recognition passes covered router/transports/
  netDb, crypto/identities, client services, I2PSnark, administration/tools,
  TUI/Android and security/privilege architecture. The combined inventory is
  `docs/FEATURE-PARITY.md`; wire status remains in `docs/INTEROPERABILITY.md`.
- Implemented by static analysis: SSU2 bounded migration retries/deadline with
  old-endpoint retention; runtime ML-KEM provider probing; RSA raw signature
  verification and RSA/Ed25519 SU3 paths with `reseed.verify=true`; SAM
  persistent bounded framing/pipelining and one-word controls; I2CP
  identity-hash routing-session keys; case-preserving address-book matching;
  torrent resume/final-name re-verification, tracker form/limits, private
  announce, PEX and peer/dial caps; and a typed daemon-owned TUI admin queue.
- Added focused regressions for SSU2 migration timing, ML-KEM provider gating,
  RSA identity layout, SU3 default verification, SAM framing/control, I2CP
  routing keys, address-book case and torrent verification/tracker behavior.
  Registered the new tests in `tests/CMakeLists.txt`.
- Android reseed/runtime log-level parity is implemented by static analysis:
  confirmed Reseed NetDb action, service-worker JNI, closed log-level set,
  persistence plus live apply, and a static instrumentation consistency test.
  Remaining implementation gaps are tracked as PARITY-007: native
  tunnel/address-book editing, selected-file promotion, durable queue/bandwidth
  controls, secret-blinded I2CP, Android SAF recovery and genuine
  network/router/verifier separation.
- No configure, compiler, build, CTest, Gradle, sanitizer, fuzzer, device,
  Java peer or live-network run was performed. Static checks only
  (`git diff --check`, source/interface inspection). All changes are pending
  manual CI compilation and pending CI tests; no CI validation is claimed.

### CI correction after the first parity runs (2026-10-20)

- Corrected `Reseed.cpp`'s invalid `[[maybe_unused]] using` declaration and
  replaced the local `BnPtr` alias with a scoped `std::unique_ptr<BIGNUM>`.
  This was the compilation failure in Linux, Android, OpenBSD, Fuzzing,
  packaging, MSVC and MinGW runs.
- Bounded the RSA excess-key allocation in `Identity.cpp` and explicitly
  checked the derived extended-identity length, addressing GCC's
  `-Wstringop-overflow` and `-Walloc-size-larger-than` warnings.
- Avoided the virtual `GetBuffer()`/`GetBufferLen()` dispatch on the concrete
  LS1 object in `Destination.cpp`, addressing GCC's false
  `-Warray-bounds` warning.
- Removed the unused `${perl:Depends}` substvar from the Debian package and
  declared the required `perl` runtime dependency explicitly.
- Corrected the resume-persistence callback call in `Torrents.cpp`: pass an
  explicit `std::span<const uint8_t>` and convert the tribool completion state
  to a definite boolean before invoking the `std::function`.
- No workflow was dispatched. The fixes are static-only, pending CI
  compilation and tests; no pass is claimed.

## Agent Skills system (2026-10-20)

- Added `.agents/skills/` using the open Agent Skills layout: a discovery
  index plus 16 subsystem skills (`cpp23`, `memory-security`, `i2p-interop`,
  `cryptography`, `privsep`, `openbsd`, `linux-windows`, `torrent`, `tui`,
  `android`, `build`, `packaging`, `tooling`, `upstream`, `ci`,
  `documentation`).
- Each `SKILL.md` has YAML frontmatter with a directory-matching `name` and a
  `description` that states when to load it. Each skill documents activation,
  files to inspect, authoritative references, reconnaissance, parallel work,
  conventions, prohibited changes, CI preparation and tracking.
- Added `references/` checklists for ownership, security surfaces, wire parity,
  cryptographic boundaries, worker/IPC lifecycle, OpenBSD/Linux/Windows
  policies, torrent storage, TUI protocol, Android service/SAF, build matrices,
  packaging/services, tools/firewall, upstream sync, CI jobs and docs/tracking.
- Integrated discovery into `AGENTS.md` without copying the skills. Permanent
  static-only, security, CI-only and licensing policy remains in `AGENTS.md`;
  skills link to it. Subagents report proposed tracking edits to the coordinator.
- Added `tests/check-agent-skills.cmake` and registered compile-free
  `agent-skills-static` in CTest (`ci;static`). It validates expected skill
  directories, frontmatter names, required sections, `AGENTS.md` references and
  `references/*.md` links. It was prepared but not executed locally; its CI run
  remains pending.
- Reconnaissance was performed by read-only subagents covering policy/tracking,
  architecture-to-skill mapping, technical references/CI workflows and
  duplicate/gap analysis. No implementation subagent was allowed to compile,
  run tests or edit concurrently.
- A second read-only review pass corrected stale packaging paths, CTest
  names/labels, `docs/mkdocs.yml`, the OpenBSD storage reference and the
  privilege-separation status wording. It also separated build-side test
  registration from CI workflow ownership and clarified Android SAF, torrent
  storage, man-page, Perl-script and shared-file boundaries.
- No compile, CTest, Gradle, sanitizer, fuzzer, workflow dispatch, link checker
  or branding validator was executed. Verification is static inspection only;
  documentation-link and branding checks remain pending manual CI. The skill
  set is not claimed dynamically validated.

## 2026-10-20 — selective upstream openssl 19/20 continuation (static-only)

- Fetched `PurpleI2P/i2pd:openssl` to `upstream/openssl` without changing the
  worktree. The reviewed tip is
  `de32c8c3f580c2c43ca09154ecfe6a20db80d19f`; the 18/19/20 October window is
  grouped by UTC and documented in `docs/upstream-sync.md`, including author
  versus committer timezone shifts and merge-only commits.
- Verified `452da43b`, `54068be9` and `472ef88b`: the RelayRequest,
  RelayIntro/RelayResponse/PeerTest and PathChallenge state guards are already
  present and stronger in the fork. The required `Introduced` RelayResponse
  exception remains intentional.
- Applied `37090fe0`: added Intel AMT ports 16992-16995 to
  `libi2pd/util.cpp`'s reserved-port table and added
  `tests/test-routercontext-port-range.cpp` to pin the complete table and its
  boundaries.
- Adapted `21ad7d43`: updated the reseed URLs/built-in-list wording in
  `contrib/i2pd.conf`, `contrib/openbsd/i2pd.conf` and
  `docs/user-guide/configuration.md`. Rejected the upstream
  `reseed.verify=false` comment because this fork is fail-closed and tests
  `reseed.verify=true`.
- Adapted `db761a54`, `2598a94e` and `86dd211a` in the RPM specs: guarded
  Fedora Boost.JSON/URL package requirements. `tests/check-build-system.cmake`
  now rejects direct make/ninja macros and checks the Fedora guard. The RHEL 9
  `-DWITH_TORRENTS=OFF` branch is omitted because its default GCC 11 cannot
  build the C++23 tree and no validated `gcc-toolset-13` RPM path exists.
- Recorded `38cf542b`, `f8a12d9d`, `357fcc14`, `a8915cd1`, `e34db4f7`,
  `233e5111` and `de32c8c3` as not applicable, merge-only, or obsolete for the
  fork's C++23/CMake/Ninja, Debian and RPM architecture. No upstream release
  metadata, browser console, GNU-make patch or EOL Jammy packaging was
  restored.
- No compiler, CMake configure/build, CTest, sanitizer, fuzzer, package build,
  rpmbuild, lintian, service or live-peer run was performed. The new
  `test-routercontext-port-range` and `build-system-static` checks remain
  pending manual CI compilation/tests; no validation is claimed.

## 2026-10-21 — parallel client/persistence hardening (static-only)

- Started from clean `main@14283908`. Read-only reconnaissance reviewed
  AGENTS/TODO/ROADMAP/PROGRESS, current diffs and the feature-parity,
  memory-safety and torrent state/security reports. Three agents worked on
  disjoint HTTP tunnel, torrent persistence and address-book files;
  coordinator review integrated shared CMake, CI and tracking changes.
- UPSTREAM-003: extracted the production HTTP tunnel header transformer to
  `I2PTunnelHTTP.h`. Independent request/response state fixes fragmented
  Connection handling and response output loss; stable owned output preserves
  async socket lifetimes. Requests retain their line-count policy; all modes
  enforce aggregate input/output header limits. Prepared
  `test-i2ptunnel-http` and extended `fuzz-http-parser` for segmentation
  comparisons, retaining the existing header rewriting and body semantics.
- UPSTREAM-004: added irreversible torrent instance tombstones and an
  infohash reservation until disk cleanup and its service callback succeed.
  In-flight disk writes finish before cleanup; stale jobs cannot mutate a
  replacement. Both storage modes delete `.openi2pd-state` even when retaining
  payload. Direct POSIX metadata deletion uses anchored no-follow parents,
  exact unlink and directory sync. Failed cleanup retains the reservation
  until daemon restart. RemoveAll admin-state cleanup now runs on the service
  domain, and completion callbacks verify current instance authority.
- Corrected shutdown final snapshots being skipped after membership clearing;
  the exception requires a stopping tunnel and a nonremoved instance. This
  correction remains without a dedicated shutdown regression. Prepared
  `test-torrents-security` cases for revocation, removal during disk IO,
  queued cleanup/re-add, retained payload and failed metadata cleanup.
- SEC-015: address-book lookups now bind nonces to the authenticated resolver
  and normalized name, cap pending requests at 256 and expire them after 120
  monotonic seconds. Wrong senders/ports do not consume valid requests;
  failed RNG cannot send a request. Prepared production-registry regressions
  in `test-addressbook-case` cover replay, collision, capacity and expiry.
- Registered HTTP tests and security/memory labels, added an explicit manual
  Linux regression selection and updated owning skill references. Cross-agent
  static review corrected an unsigned capacity subtraction and identified
  residual Windows deletion races. No wire or persisted format was changed.
- Local verification was source/interface inspection and `git diff --check`
  only. No configure, compiler, build, CTest, sanitizer, fuzzer, Gradle,
  project validator, workflow dispatch, commit or push was run. Everything
  remains implemented by static analysis, pending CI compilation and pending
  CI tests. Full privilege separation, broader lifecycle/admin/scheduling work
  and live Java/device/platform validation are not claimed complete.

## 2026-10-21 — update the Java comparison target to 2.14.0

- Adopted the user's current comparison target, I2P Java `i2p-2.14.0`.
  `git ls-remote` against the official GitHub mirror returned annotated tag
  object `9db233ed7241599eba76f29b8c334eaaf3db2e87` and peeled commit
  `3c98503c5f14efe7bdd783528ba7f621f12d8081`. No release date or local
  checkout availability is inferred from those refs.
- Updated the current reference in AGENTS, skills, interoperability/functional/
  Snark documentation and tracking. Historical 2.13.0 findings, vectors,
  template provenance and earlier progress entries retain their original
  attribution. NET-026 is in implementation: source delta review and migration
  of comparison evidence to 2.14.0 remain pending.
- `check-reference.sh` now expects the 2.14.0 tag and commit. The existing
  vector checker retains its 2.13.0 pin and now accepts the dedicated
  `I2P_JAVA_FIXTURE_REFERENCE` environment variable; `--reference` remains
  the explicit historical corpus option. CMake separates current
  `I2PD_INTEROP_REFERENCE` from `I2PD_INTEROP_FIXTURE_REFERENCE`.
- Prepared the existing manual interop CI job to check two external read-only
  checkouts: current target identity and historical fixture provenance. The
  workflow remains manual-only; no fixture bytes, expectations or Java source
  were rewritten. Prior client/torrent implementation changes were preserved.
- Local verification: remote ref inspection, source/diff review and
  `git diff --check` only. No compiler, build, Java run, test, project validator,
  workflow dispatch, commit or push. Reference-checker/CI changes are
  implemented by static analysis and pending CI tests; no 2.14.0 parity or
  interoperability validation is claimed.

## 2026-10-21 — string safety and libc compatibility (static-only)

- Audited first-party formatting and concatenation. The tree had one remaining
  unbounded call (`strncpy` for an interface name) and no first-party
  `sprintf`/`strcat`/`strcpy`; Boost.Asio 1.90 still compiled an unsafe POSIX
  scope-ID `sprintf`/`strcat` path in `inet_ntop`.
- Added `AddressFormat.h` with bounded IPv4/IPv6/scope-ID and endpoint
  formatting using `inet_ntop` and explicit scope suffixes. Added `LogPrint`
  overloads and converted direct address formatting so Asio's unsafe formatter
  is not instantiated; selected `BOOST_ASIO_HAS_SNPRINTF=1` as an upstream
  bounded fallback for any remaining Asio use.
- Added `CopyString` and replaced the interface-name `strncpy`. Hardened
  `I18N` formatting with a checked vector buffer, checked `Family` certificate
  name extraction, bounded `Log` timestamp fallback, torrent hex output, and
  SOCKS domain-name length checks (including zero-length and capacity overflow).
- Prepared `test-address-format`, `test-util-copy-string`, `test-i18n-format`
  and `test-socks-address`; registered them in CTest and added static
  `build-system-static` checks for the unsafe call family and the Asio macro.
- No compiler, CMake configure/build, CTest, sanitizer, fuzzer, package,
  Android/Gradle or live run was performed. All fixes and tests are static-only,
  pending manual CI compilation and tests.

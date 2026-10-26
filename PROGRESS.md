<!--
See the LICENSE file at the top of the project tree for copyright
and license details.

openi2pd: PROGRESS.

Documents project documentation.

-->

# Progress log

Chronological, append-only record. New sessions should read this file first,
then `TODO.md` and `ROADMAP.md`, then verify the code state.

## Session 1 — initial audit and refactoring (historical)

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
- JDK minimum 25 enforced at Gradle evaluation; Java toolchain 25. The
  historical source level 17 was later superseded by Java 25 source/target;
  see `docs/android-java25.md`.
- Removed old WebView preferences, permission gate activities, hard-coded
  signing credentials and cleartext WebView flag.
- Gradle configuration now reaches the expected missing-SDK check; no APK was
  built.

## Session 2 historical blockers (superseded)

This block records the Session-2 state only. Local build/test instructions in it
are superseded by the permanent static-only policy; use `TODO.md` and manual CI
for current work.

- `test-routerinfo-firewall` compiled but was not run at that time.
- The subsequent full build/test was not performed under the current policy.
- The Java reference was later pinned at 2.14.0; see `docs/INTEROPERABILITY.md`.
- Android SDK/NDK, APK/JNI/Lint, sanitizers, fuzzing and OpenBSD/Windows/device
  validation remain separate manual-CI items.
- `TODO.md` contains the full unresolved inventory.

## Session 2 historical next steps (do not execute locally)

1. Dispatch manual CI for CMake/CTest, tools smoke and the firewall test.
2. Validate the POSIX Make front-end in manual CI.
3. Use the pinned 2.14.0 Java reference in a read-only external checkout.
4. Validate Gradle in the manual Android job when the SDK is available.
5. Continue the tool security audit and documentation-link checks.

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
- Added a temporary systemd `Alias=open-i2pd.service` and updated service
  descriptions; the alias was later removed because no packaged upgrade path
  owned it. Binary/config/persistence paths are intentionally retained pending
  migration.
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

### Session 35 — Android English-only interface (historical; superseded by I18N-001/002)

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

## 2026-10-21 — functional parity continuation (static-only)

- Used read-only reconnaissance for router/protocol, client-service/proxy,
  administration/TUI/Android and I2PSnark gaps against the pinned I2P Java
  2.14.0 sources. No Java tree is vendored or built.
- Added bounded native address-book list/add/replace/remove APIs with
  Java-compatible `.i2p` validation, shared-identity reference safety and an
  empty-index persistence path. TUI Destinations and a native Android Material
  screen use local IPC/app-internal JNI; only full base64 destinations are
  accepted.
- Exposed I2PSnark force recheck through `TORRENT-RECHECK`, TUI `V` and Android;
  the torrent must be paused with complete metadata.
- Added Java-compatible rejection of token-bearing DatabaseStore messages that
  arrive through a router tunnel, before DeliveryStatus construction.
- Prepared `test-addressbook-admin`, `test-tunnel-dsm-policy`, protocol parsing
  and Android recheck regressions; updated the parity matrix, TUI/torrent docs,
  TODO, ROADMAP, AGENTS and testing/files-changed records.
- No compiler, CTest, Gradle/device, sanitizer, fuzzer, validator, live
  lookup/tracker/peer or workflow dispatch was performed. Tunnel creation/
  editing, selected-file promotion, queue/bandwidth policy, deleted-host
  tombstones and subscription editing remain open implementation work.

## 2026-10-21 — upstream openssl continuation (static-only)

- Fetched `PurpleI2P/i2pd:openssl` without worktree writes. The branch tip is
  now `9725b8b227293a796c069ac270fa6b5b47cf9a13`; six commits follow the
  previous `de32c8c3` review, including the merge-only `8710fd89`.
- Adapted `35e25db2`: proxy-generated errors now use typed 400/431/500/502/504
  statuses, including the fork-only request-header limit path. The existing
  nil-socket and bounded-buffer guards remain; `HTTPCodeToStatus(431)` and
  status-line regressions were added.
- Adapted `9725b8b2`: a torrent whose hash is in our own DHT bucket reuses
  local rendezvous peers and publishes our destination with the correct seed
  bit, while preserving private/stopped suppression, `MAX_DHT_TORRENTS` and
  peer caps.
- `032b8a61` was already superseded by `I2PTunnelHTTP.h`; mixed-case HTTP
  tunnel header regressions were added. `137dde2f` is partially
  integrated/obsolete, `8710fd89` is merge-only, and `c5e8e10a` is incompatible
  because it packages the deliberately removed web console.
- Prepared `test-http-res`, `test-torrents-dht-interop` and `test-i2ptunnel-http`
  extensions; updated `docs/upstream-sync.md`, TODO, ROADMAP, AGENTS and the
  torrent/proxy documentation. No local build, CTest, sanitizer, fuzzer,
  package, proxy/DHT/peer run, commit or push was performed.

## 2026-10-21 — OpenSSL 4 and C++23 ownership continuation (static-only)

- Modernized OpenSSL/LibreSSL discovery: CMake now reads the selected
  `opensslv.h`, rejects mixed prefixes, enforces OpenSSL 3.0.2+/LibreSSL 4.0+,
  exposes `I2PD_OPENSSL_EXPECT*` CI guards and runs compile-only probes for
  `EVP_PKEY_is_a`, group/BN params, `EVP_MD/CIPHER/KDF_fetch`, `EVP_EC_gen`,
  `ASN1_STRING` accessors and `TLS_method`. OpenSSL must provide the modern
  surface; LibreSSL retains small feature-selected fallbacks.
- Added a runtime EVP capability probe in `Crypto` and switched family/reseed
  key inspection to provider APIs where available. Ported `famtool` to checked
  EVP signing, `EVP_EC_gen`/PKCS#8 output and strict DER parsing; no deprecation
  suppression remains. TLS contexts now use explicit client/server roles and
  I2PControl signs new certificates with SHA-256. PQ publication and session
  selection use `CanPublishPQ`/`HighestSupportedPQVersion`, so a provider that
  lacks a parameter set cannot be advertised.
- Continued C++23 ownership work: `TunnelConfig` uses an owned
  `vector<unique_ptr<TunnelHopConfig>>`; `GarlicDestination` no longer shares a
  mutable payload buffer; `Piece::m_Data` is `unique_ptr`; `UDPTunnel` uses
  `optional<thread>` and `atomic<bool>`; `Families` owns `EVP_PKEY` and checks
  signing/verification results. Address-book index writes use a same-directory
  temporary and rename, empty indexes are not created on clean shutdown, and
  the Android recheck/tracker parsers reject the engine's `0` sentinel.
- Prepared `test-openssl-capabilities`, `test-tunnelconfig-ownership`, address
  book save-count checks and the Android recheck expectation; updated
  dependencies/famtool/testing documentation and TODO/ROADMAP/AGENTS. No
  compiler, CMake configure/build, CTest, sanitizer, fuzzer, package,
  Android/Gradle/device, live TLS/PQ or workflow dispatch was performed. All
  changes are implemented by static analysis, pending manual CI compilation
  and tests; no commit or push was authorized.

## 2026-10-21 — memory-safety audit continuation (static-only)

- Ran independent static audits for SSU2/NTCP2, ratchet/Garlic, netDb/
  RouterInfo/LeaseSet, BitTorrent, IPC/privsep and Android JNI. Findings were
  correlated against the current uncommitted diff; no source was compiled or
  executed.
- Fixed the family-signature fail-open, router garlic/DeliveryStatus length
  checks, Garlic clove pointer bounds, NTCP2 SipHash setup failure handling,
  torrent piece/DHT empty-vector bounds, offline-key chaining, truncated SU3
  file names, HTTP control-character headers, SAF/JNI pending-exception
  ordering, native Android start re-entry, and TUI/admin/comment bounds.
- Prepared/extended `test-http-res` and `test-tui-protocol` malformed-input
  cases. Added tracking for the remaining SSU2 pool/address/fragment races,
  RouterInfo container mutation, ratchet/destination crypto-context sharing,
  datagram receiver lifetime and residual JNI/IPC contracts (`SEC-025`).
- No compiler, CMake configure/build, CTest, ASan/UBSan/TSan, fuzzer, Gradle/
  device, Java peer or workflow dispatch was performed. All changes are static
  analysis only, pending manual CI compilation/tests; no commit or push.

## 2026-10-21 — concurrency and lifecycle continuation (static-only)

- Six read-only audits covered callbacks/timers, SSU2/NTCP2 sessions, netDb,
  BitTorrent, IPC/storage workers and Android JNI.
- Fixed the `RunnableService` self-stop/join path, made `Transports` lifecycle
  and shutdown ordering atomic/idempotent, restarted `I2PControl` correctly,
  made daemon shutdown flags atomic, and removed the shared
  `RouterContext::m_CurrentNoiseState` scratch by returning caller-owned Noise
  state. `Families` now protects its key map with a shared mutex.
- Added a terminated latch to SSU2 sessions so late handshake packets cannot
  resurrect a terminated session, and terminated expired pending sessions
  instead of silently erasing them.
- Prepared `test-runnableservice-lifecycle`; documented remaining raw
  `this`/receiver/RouterInfo/Profile/BitTorrent/IPC/JNI lifetime work as
  `CONC-005`.
- No compiler, CTest, ASan/UBSan/TSan, fuzzer, Android/device, OpenBSD or live
  peer run was performed; no commit or push.

## 2026-10-21 — build/dependency consolidation (static-only)

- Made the POSIX make front-end real: `.PHONY`, `all` default, build-before-install,
  `DESTDIR`/`PREFIX` forwarding, safe clean guards, working `tui`/`tools`.
- Added `build/CMakePresets.json` (Ninja), rejected `Unix Makefiles` on OpenBSD, set
  CMake 3.24, fixed install components (`Development`), made the exported
  package relocatable with `configure_package_config_file`/`@PACKAGE_INIT@`,
  moved systemd units to an architecture-independent path, installed docs
  outside `WITH_BINARY`, required ncursesw explicitly by default
  (`I2PD_TUI_REQUIRED`), and modelled Boost.JSON/URL as header-only.
- Removed unused CMake modules, fixed MSVC static suffixes, consolidated Boost
  discovery, moved `WITH_FUZZERS` to the top level, pinned RPM CMake/Ninja/Boost
  requirements, replaced old Fedora/Mageia build-directory branches with
  `%{_vpath_builddir}`, removed the obsolete ENGINE requirement and dangling
  i2pd-git certificate link, corrected the static package tmpfiles path, and
  updated Docker to install zlib without header-only Boost packages.
- Added `packaging-static` and manual install-component/POSIX-make CI paths;
  OpenBSD now installs ncurses and asserts `i2pd-tui`; the fuzz workflow
  instruments linked libraries.
- No configure, build, test, package, workflow dispatch, commit or push was
  performed.

## 2026-10-21 — functional parity continuation (static-only)

- Read-only subagents compared router/netDb/reseed, client services/proxies,
  administration/TUI/Android, address book and I2PSnark against the pinned I2P
  Java 2.14.0 tree (`3c98503c5f14efe7bdd783528ba7f621f12d8081`).
- Implemented native deleted-host tombstones: bounded normalized patterns,
  anchored wildcard matching, atomic `deleted.txt` persistence, subscription
  merge skip and explicit-add clearing. TUI exposes `ADDRESSBOOK-BLOCKED`,
  `ADMIN ADDRESS-BLOCK/UNBLOCK`, Destinations `b`/`u`; Android exposes block,
  unblock and blocked-list dialogs through the service worker/JNI.
- Implemented I2PSnark metainfo fidelity and editing: unknown top-level bencode
  keys and creation date/url-list/encoding survive tracker/metadata rewrites;
  comment and created-by editing require a paused torrent and use bounded TUI
  IPC/Android controls. Android now supports remove-with-data through the
  existing `TORRENT-REMOVE ... files` path.
- Recorded remaining 2.14.0 gaps in `docs/FEATURE-PARITY.md` §10 and
  `PARITY-014`: tunnel filters, Streaming limits/actions, SAM protocol subsessions,
  proxy authentication, NetDb admission/refresh, reseed netid multi-source,
  operator blocklist, admin inventory/per-tunnel control and selected-file
  promotion/bandwidth scheduling.
- Prepared/extended regressions: `test-addressbook-admin` (tombstones),
  `test-torrents-engine-interop` (metainfo preservation/edit), TUI protocol
  vectors and Android `TorrentControlTest`.
- No compiler, CTest, sanitizer, fuzzer, Gradle/device, Java peer or workflow
  run was performed; no commit or push.

## 2026-10-21 — upstream openssl re-verification and integration gap closure (static)

- Re-verified `PurpleI2P/i2pd:openssl` with read-only `git ls-remote` and a fresh
  clone. The branch tip remains `9725b8b227293a796c069ac270fa6b5b47cf9a13`;
  `git rev-list --all --not 9725b8b2` is empty, so there is no later commit to
  classify. The six-commit `de32c8c3..9725b8b2` continuation is fully recorded.
- Closed integration gaps exposed by the already-audited commits:
  - DHT rendezvous now unannounces our identity on stop/remove, reserves one
    local slot when remote announcers fill the per-torrent cap, rejects removed
    torrents and matches upstream's empty-bucket predicate fallback.
  - Proxy error status constants moved to `HTTPProxy.h` with a constexpr
    failure mapping; regression tests pin 400/431/500/502/504.
  - `I2PTunnelHTTP` rejects obs-fold leading SP/HTAB header continuations;
    mixed-case `Proxy-Connection` is pinned.
- Corrected upstream-sync packaging wording: `137dde2f` contributes no
  applicable hunk; its `0%{?rhel} <= 9` condition would be true on non-RHEL.
- Updated `docs/upstream-sync.md`, `TODO.md` (`UPSTREAM-006/007`), `ROADMAP.md`,
  `docs/testing.md`, `docs/files-changed.md` and `AGENTS.md`.
- No compiler, CTest, sanitizer, fuzzer, live proxy/DHT/peer or workflow run was
  performed; no commit or push.

## 2026-10-21 — SSU2/NTCP2 handshake and control-plane continuation (static)

- Four read-only audits compared SSU2 handshake/ACK/retransmission, fragmentation,
  NAT/introducers/PeerTest/relay/MTU/IPv6 and NTCP2 framing/options with the
  pinned Java 2.14.0 tree.
- SSU2: late packet 0 no longer rewinds the receive point or ACK; wrong-type
  short packets no longer trigger unbounded SessionConfirmed retransmission;
  session removal is owner-checked; failed SessionCreated resets Noise state;
  the resend throttle predicate was corrected; late `Established` callbacks are
  posted to the service thread; PeerTest msg 1 checks authenticated source
  family/address/target; PeerTest/RelayIntro private/reserved targets are
  rejected; PeerTest timestamps are bounded to ±120 s; RelayIntro HolePunches
  have per-target/rate admission; banned NTCP2 termination marks the peer
  unreachable.
- NTCP2: msg3p2 bounds aligned to Java 2.14.0 (`459..4048`) in both directions;
  static assertions and handshake-bound tests updated.
- Prepared/updated regressions cover malformed, reordered and loss cases in
  `test-ssu2-*`, `test-ntcp2-*`, proxy status and address-book/torrent work.
- No compiler, CTest, sanitizer, fuzzer, live relay/PeerTest/MTU/IPv6 or
  workflow run was performed; no commit or push.

## 2026-10-21 — cryptography audit continuation (static)

- Independent audits covered Ratchet/ECIES NS/NSR, DateTime/replay, AEAD and key
  lifetime, signatures/Elligator2/blinding, and ML-KEM/OpenSSL-LibreSSL
  capability handling.
- Implemented CSPRNG seeds and fail-closed peer ordering, fail-closed
  multi-buffer AEAD, zeroization, ratchet tag boundary/eviction/NEXT_KEY
  amplification limits, an NSR/ACK fallback for all client destinations, LS2
  RSA transient policy, Ed25519 point validation, GOST RFC 7091 normalization,
  SSU2 KEM/version fallback, fail-closed leaseSetEncType and I2CP key-length
  validation.
- Added `test-rng-seed`, extended AEAD/GOST/Ratchet regressions and updated
  `docs/INTEROPERABILITY.md`, `docs/security-audit.md`, `TODO.md` (`NET-164`)
  and test/file tracking.
- Residual: NS empty-clove policy is deliberately stricter than Java; tag
  eviction can still churn under sustained abuse; provider PQ transcripts,
  type-5/7 NSR vectors, multi-key 7/4 sessions and all dynamic validation
  remain pending. No compile, CTest, sanitizer, fuzzer, Java peer or workflow
  run; no commit or push.

## 2026-10-21 — netDb/RouterInfo/LeaseSet continuation (static)

- Clove-delivered I2NP now preserves its untrusted origin; token DSM/DLM cannot
  masquerade as direct messages.
- Client LeaseSet DSRMs are bound to queried peers, deduplicated and capped.
- Per-sender lookup and per-key flood throttles bound amplification while still
  storing and acknowledging entries.
- Fresh zero-address RouterInfos are storable but not selectable; persistence
  uses an atomic updated snapshot and temp+rename file replacement.
- Reseed preserves the local RouterInfo/floodfill entry and is fenced by Stop;
  Start uses locked container counts.
- Encrypted LS2 rejects RSA blinded types; LS2 PopulateLeases is a no-op; Meta
  lifetime is bounded by its latest entry; LS1 buffers are canonicalized.
- Reply-key 0x12 remains fail-closed because Java 2.14.0 has no normative sender
  or KDF.
- Added/updated DSMPolicy, LS1 canonical, LS2 PopulateLeases, Meta expiry and
  encrypted-RSA header regressions; no compile/CTest/TSan/live run exists.

## 2026-10-21 — tunnel-build/I2NP/Garlic continuation (static)

- Duplicate tunnel peers are rejected and zero transit tunnel IDs are dropped.
- Zero-hop creation follows the selected path; replacement outbound tunnels are
  published correctly; varied-length pools retain pre-expiry recreation.
- Build-record shuffles advance the shared RNG.
- OBEP LOCAL delivery and non-whitelisted clove types are dropped.
- TunnelGateway reserves its header, rejects oversized bodies and checks inner
  I2NP expiration.
- Per-tunnel IV replay filtering and bounded reassembly tables were added.
- Clove lengths/freshness are validated, encryption is fallible and router
  garlic wrapping null-checks its session.
- Custom peer selectors are owner-bound/unregistered and peer configuration is
  copied under a mutex.
- No compiler, CTest, sanitizer, fuzzer, live tunnel or Java peer was run; no
  commit or push.

## 2026-10-21 — SAM/I2CP/Streaming/Datagram continuation (static)

- SAM per-port acceptors are disarmed and dequeued; closed sockets cannot adopt
  streams; subsession PORT/duplicate checks, SESSION STATUS errors, HELLO
  whitespace parity and balanced shutdown were added.
- I2CP b33 replies validate the blinded store key, terminal MessageStatus is
  single-owner, and CreateLeaseSet2 validates/stages keys before publishing.
- Streaming saved-packet/reassembly tables are bounded, stream lookup is locked,
  ping/pong ports are preserved, invalid SYNs are rejected and terminated sends
  cancel queued handlers.
- Datagram3 hash-only delivery no longer creates/acks a session without
  authentication; wildcard receivers replace; Datagram1/2 refresh sender identity.
- No compiler, CTest, sanitizer, fuzzer, Java peer or client run; no commit/push.

## 2026-10-21 — user-services continuation (static)

- HTTP/SOCKS now classify `.i2p` case-insensitively, accept `.i2p.alt`, reject
  loopback/private clearnet outproxy targets, and SOCKS accepts IPv4/IPv6
  literals with port-0/private rules.
- Address book adds `.i2p.alt` lookup aliases, `www.` fallback, bounded
  subscription bodies/entries and tombstone enforcement on lookup/jump inserts.
- Reseed now verifies TLS chain/hostname, bounds SU3/HTTP/ZIP/entry/inflated
  sizes, rejects stale network bundles and normalizes default hosts.
- Remaining user-service work: HTTP/SOCKS authentication, full response-header
  limits, server-tunnel TLS configuration, reload of changed tunnel sections,
  tunnel inventory/per-tunnel admin and Android replace UI.
- No compiler, CTest, sanitizer, fuzzer, Java peer, live proxy/reseed or device
  run; no commit or push.

## 2026-10-21 — multiprocess trust-boundary continuation (static)

- Six read-only reconnaissance passes covered supervisor lifecycle, Linux
  worker bootstrap, storage roots, router-core capabilities, network/socket
  ownership, crypto/key custody and log-worker recovery.
- The Linux supervisor no longer installs the write-deny Landlock and
  AF_INET-deny seccomp domains it inherited into child workers. It now applies
  `ApplyLinuxSupervisorSandbox` (no-new-privs plus capability/bounding-set drop)
  and each exec'ed worker installs its own role profile. This corrects the
  default path that would otherwise prevent storage writes and network sockets
  in every child.
- Log-worker restart is bounded by 250 ms exponential backoff capped at 30 s,
  eight consecutive attempts and a 60 s stable-period reset; exhaustion exits
  the supervisor. The logger queue is capped at 8192 records with 8192-byte
  record truncation and a dropped-record counter.
- The OpenBSD torrent-storage broker now receives its validated data root as
  inherited fd 4 opened before spawn instead of resolving the path after
  pledge. It uses `pledge("stdio unix proc exec sendfd recvfd")` without
  `rpath`; the immutable fd, not the configured path string, is the authority.
- The OpenBSD startup pledge always carries `exec` for the startup worker
  window, including storage brokers when `privsep.log` is disabled; the runtime
  policy still drops `exec`.
- Router storage distinguishes a worker-level write rejection from a broken
  channel, so one failed atomic replacement no longer poisons later requests.
- `RunWorker` now requires its channel at descriptor 3, matching every spawn
  path and preventing a manually invoked worker from aliasing inherited root
  descriptor 4.
- Prepared tests: supervisor-bootstrap delegation in
  `test-privsep-linux-sandbox`, bounded-queue/drop accounting in
  `test-log-lifecycle`, and transient-rejection recovery in
  `test-router-storage`.
- Residual: the core still owns transport sockets, most netDb/profile/
  address-book/destination-key writes and long-term keys. Network and crypto
  workers remain policy-builder-only; no single supervisor owns all workers.
  No compiler, CTest, sanitizer, fuzzer, OpenBSD/Linux/Windows runtime,
  Android device, package, live-peer or workflow run exists. No commit or push.

## 2026-10-21 — IPC protocol consolidation (static)

- Five read-only audits covered serialization, worker contracts, authorization,
  buffers/backpressure and error/recovery. No compiler, CTest, sanitizer,
  fuzzer, OpenBSD/Linux/Windows runtime or workflow run was performed.
- Protocol v2 adds the typed `Error` reply (offending type, bounded
  `ErrorCode`, bounded detail) and the shared `EndpointRole`/`AllowsOperation`
  matrix; v1 frames remain readable and new senders emit v2.
- Path/error encoding is fallible (`WritePath`, `WriteStringBounded`,
  `MAX_PATH_SIZE=4096`), so an over-long or NUL-bearing path can no longer be
  silently truncated to a different filesystem object.
- Log configuration now performs a bounded configure/readiness `Ping`/`Pong`
  handshake in both direct and supervisor paths. Startup fails if the worker
  did not apply the configuration; the supervisor distinguishes protocol
  violations (exit 1) from clean shutdown (exit 0). The sink counts dropped
  records after close/send failure.
- All POSIX workers enforce the shared operation allowlist; Ping/Shutdown must
  have empty payloads; storage request IDs must be strictly increasing, so a
  replay cannot re-execute Register/Metadata/Promote/Remove mutations.
- The OpenBSD storage broker sends typed `Error` replies for capacity, unsafe
  path and replaced-root failures and keeps existing children alive; adopted
  worker channels are revalidated as seqpacket sockets with the expected peer
  UID and CLOEXEC.
- `SOCK_SEQPACKET` zero-length records are consumed as records, not EOF.
  `imsgbuf_read` buffered messages are drained before polling again. Windows
  oversized records close the pipe, and `AcceptBound` binds the peer PID/session
  to the spawned worker.
- Tests prepared: protocol v2/v1 and Error/allowlist/path cases, empty-record
  and adopted-fd rejection, configure handshake/drop counting, buffered imsg
  frames, oversized Windows pipes and broker-rejection recovery.
- Residual: no automatic storage-client reconnect/generation fencing, explicit
  transactional cancellation IDs, full typed cross-process error taxonomy,
  Windows daemon wiring or dynamic validation. No commit or push.

## 2026-10-21 — OpenBSD supervisor/storage continuation (static)

- Four read-only audits covered Linux storage parity, OpenBSD pledge/unveil
  semantics, IPC channels and per-process/network policies. No OpenBSD run,
  compiler, CTest, sanitizer, fuzzer or workflow was executed.
- Fixed the critical broker start path: the pre-pledge storage broker no longer
  calls `openat()` after pledge and no longer needs `rpath`. It passes its
  immutable data root at fd4; the worker walks the delegated relative path,
  verifies ownership/dev/inode and the configured pathname, then replaces fd4
  with the validated subdirectory before applying pledge/unveil.
- Forwarded storage workers are disowned after the broker hands their channel
  to the core, so broker shutdown cannot SIGTERM/SIGKILL an in-flight `fsync`.
  The broker reaps detached children non-blockingly and uses a replenished
  one-minute launch window instead of a permanent launch counter.
- A broker timeout is no longer treated as a dead broker; late replies are
  drained by request id before a new reply is matched.
- Storage policy requires a non-empty absolute unveil root and no longer grants
  unused `fattr`/`flock`.
- `ImsgChannel::AdoptValidated` checks socket type, CLOEXEC and `getpeereid`
  before `imsgbuf_allow_fdpass`; oversized sends retain caller descriptor
  ownership and a send timeout poisons the channel.
- Network policy gains explicit `socketHandoff` (`recvfd`), `routeInfo`
  (`route`) and optional `dns` flags; `ValidatePreopenedSocket` checks type,
  CLOEXEC and `SO_ACCEPTCONN`. No transport backend is claimed.
- Prepared tests cover empty storage roots, pre-opened socket validation,
  imsg validated adoption/descriptor ownership, supervised storage path
  replacement and broker-survives-rejection.
- Residual: networking/crypto backends and core `inet` removal remain
  unimplemented; router storage is not yet under the storage broker; log-worker
  pathname privileges and full cross-process recovery remain open. No commit or
  push.

## 2026-10-21 — Linux multiprocess hardening continuation (static)

- Five read-only audits covered AF_UNIX/SOCK_SEQPACKET, credentials, pidfd,
  descriptor transfer and the BitTorrent-connected storage worker. No Linux
  runtime, compiler, CTest, sanitizer, fuzzer or workflow was executed.
- Channel hardening: adopted channels now require AF_UNIX/seqpacket where
  `SO_DOMAIN` exists, reject listening sockets, and enable `SO_PASSCRED` on
  Linux. Empty records must pass the same `SCM_CREDENTIALS` check and are
  bounded to eight per receive call.
- Worker spawn sets `PR_SET_NO_NEW_PRIVS` before exec, so setuid/file-capability
  executables cannot raise privilege before the worker policy is installed.
- Seccomp now denies `process_vm_readv/writev`, `kcmp`, `pidfd_open`,
  `pidfd_send_signal`, `open_by_handle_at`, `name_to_handle_at`,
  `userfaultfd`, `bpf` and `perf_event_open` in addition to the existing
  ptrace/io_uring/socket rules.
- Descriptor hygiene: the pre-5.9 `close()` fallback uses the actual soft
  `RLIMIT_NOFILE` and a documented 1,048,576 cap instead of 1024; supervisor
  and log worker close inherited stdio descriptors that are sockets.
- Linux storage roots carry dev/inode identity and the worker compares it
  before applying Landlock. Both storage clients keep `O_RDONLY` directory
  descriptors because the worker fsyncs parent directories.
  The OpenBSD-only `*-inherited` roles are rejected on Linux.
- Kernel floors and fallbacks are documented in
  `docs/security/process-capabilities.md`.
- Residual: pidfd deferred-reap registry, total stop budget, cgroup v2/kill,
  kill/tgkill self-filtering, full descriptor classification and actual
  network-worker separation remain pending. No commit or push.

## 2026-10-21 — Windows multiprocess hardening continuation (static)

- Five read-only audits covered Named Pipes, ncalrpc/RPC, restricted
  tokens/AppContainer/LPAC, Job Objects/mitigations and shared-protocol parity.
  Decision: keep documented message-mode Named Pipes as the Windows worker
  transport; ncalrpc/MIDL is not migrated now.
- Overlapped I/O: `CancelIoEx` is followed by `GetOverlappedResult(..., TRUE)`
  so buffers/OVERLAPPED are not released while the kernel still owns them.
  Send/receive timeouts and malformed frames close the channel; short writes
  are terminal.
- Accept/connect: rejected peer identities re-arm the one-instance listener
  instead of destroying it; `ConnectBound` authenticates the expected server
  PID/session; `MakeRandomPipeName` uses BCryptGenRandom for per-spawn names;
  `bcrypt` is linked by the Windows privsep target.
- Token/Job: every parent privilege is deleted from the restricted token and
  `LUA_TOKEN` is used for elevated parents; Job Objects now include
  `DIE_ON_UNHANDLED_EXCEPTION`, process/job memory flags and basic UI
  restrictions.
- Windows operation matrix now rejects `ConfigureLogFd` (no descriptor channel)
  and exposes the typed storage request types.
- Documentation records the remaining hard gap: no Windows `--privsep-worker`
  entry/supervisor/daemon wiring, no AppContainer/LPAC token construction,
  no role ACL/WFP enforcement or Windows runtime tests.
- No Windows compiler, CTest, service/cross-user test, AppContainer/LPAC run,
  workflow dispatch, commit or push.

## 2026-10-21 — Android multiprocess/SAF continuation (static)

- Five read-only audits covered services/lifecycle, JNI, Binder/AIDL, SAF and the
  native C++23 engine. No Gradle, NDK, Lint, emulator/device, workflow, commit or
  push was executed.
- The embedded router remains running when the Activity is destroyed
  (`stopWithTask=false`, persisted `should_run`); this is preserved. Android
  process separation is documented honestly: a second `android:process` shares
  the UID, `isolatedProcess` loses persisted SAF grants, and INTERNET is per-UID.
- Android SAF: tree selection validates tree/read-write grants, releases a
  replaced grant and supports detach; export persists/releases its target grant;
  close/release no longer retire the broker on a client timeout; broker descriptor
  close is queued through a reaper; cancellation is operation-scoped instead of
  process-wide; created document names are checked; export starts empty.
- `validPath` now allows legal Android display-name characters `:` and `\`.
- JNI inbound Java strings are decoded from UTF-16 to UTF-8, rejecting NUL and
  unpaired surrogates; native start diagnostics are bounded.
- First-launch pending autostart is now persisted across process/Activity death;
  `MY_PACKAGE_REPLACED` resumes only a router whose start was already requested.
- `TorrentControl.comment` rejects torrent id 0.
- Residual: no router AIDL/process split, no SAF session-scoped AIDL capability,
  no moving import/create document reads into the broker, no minimal SAF seccomp
  library, and no Android device/CI evidence.

## 2026-10-21 — ncursesw TUI panel/error continuation (static)

- Read-only audits covered the existing TUI administrative surface, the pinned
  I2P Java 2.14.0 console/CLI and the local `I2PDTUI/1` operations. No compiler,
  CTest, terminal session, sanitizer, fuzzer, Java peer or workflow was run.
- Added the bounded read-only `SERVICES` request and an eleventh `Services`
  view (`v`). It lists client/server TCP and UDP tunnels, HTTP/SOCKS proxies and
  the SAM/BOB/I2CP servers from thread-safe `ClientContext` snapshots; only
  public b32 destinations are exposed, never keys, listener objects or paths.
- Improved NetDb: LeaseSet encryption type, aged timestamps and an explicit raw
  caps mask; Status now shows net ID and transit bandwidth/bytes; Peers shows
  its bounded snapshot size. The router remains headless and independent of the
  TUI, and no web administration surface was reintroduced.
- Fixed the admin queue contract: a command whose IPC waiter timed out is still
  executed exactly once and only its result is discarded. The client preserves
  the typed remote `ErrorCode`; the TUI shows both code and text. Destructive
  prompts are suspended below the minimum terminal size instead of committing
  invisibly, and a daemon restart resets the log cursor.
- Bounded the destination/address-book client view, matched the new LeaseSet
  field count and added protocol/service/queue/error-code regressions.
- Validation remains pending: no ncurses session, CTest, ASan/UBSan/TSan, fuzz,
  Windows console, live daemon/socket or service-inventory run exists.

## 2026-10-21 — I2PSnark piece/resume/selection continuation (static)

- Five read-only audits covered the native engine/control core, disk creator,
  magnet/tracker/priority paths, peer download/upload/storage and the pinned
  I2P Java 2.14.0 I2PSnark sources. No compiler, CTest, sanitizer, fuzzer,
  Java peer, Android device, live tracker/peer or workflow run was executed.
- Resume/integrity: `ApplyBitfield` now requires exactly
  `ceil(pieces/8)` bytes with zero spare bits and rejects without mutating
  pieces. Native resume replacement uses the atomic no-follow helper; a stale
  resume file is removed when no piece or every piece is complete. Startup
  verifies every piece fully backed by an existing final or `.part` file, so a
  mixed final/missing restart does not discard intact data. Explicit recheck
  hashes all pieces, including an incomplete `.part` with a lost bitmap.
  `GetPieceFileFragments` no longer derives staging from torrent completion,
  fixing recheck of a promoted torrent reading nonexistent `.part` names.
- Download/selection: accepted blocks refresh piece activity, preventing the
  22-second inactivity reset during a slow piece. Completion, tracker `left`
  and promotion are selected-file based; selected `.part` files are promoted
  individually, skipped files are left untouched, and `GetFullLeft()` keeps
  whole-torrent diagnostics. `TorrentInfo`/IPC expose `wanted` and `private`;
  the TUI and Android detail/list show selected size/privacy.
- Trackers/creation: a primary `announce` outside a non-empty `announce-list`
  is normalized into tier 0 without changing the info hash. Tracker snapshots
  expose tiers; private torrents reject tracker administration. Public creation
  with no trackers inherits tunnel common endpoints; private creation without
  a tracker fails before hashing. TUI blocks private tracker prompts and
  Android shows the fixed-list policy.
- Prepared tests: exact resume padding/short/oversized rejection, mixed
  final/`.part` verification, selected completion and full-left accounting,
  primary-plus-list normalization, public tracker inheritance and
  private-without-tracker rejection. All remain unexecuted.
- Remaining: endgame, corrupt-peer accounting, request-ledger/choke policy,
  BEP 12 tier failover, tracker registry reclamation, durable unresolved
  magnets, nested-root creation, creation metadata/source and Java/live
  interoperability. Do not claim dynamic validation.

## 2026-10-21 — decentralized peer discovery continuation (static)

- Five read-only audits covered native i2p_dht, i2p_pex, tracker discovery,
  DHT persistence/bounds and the pinned I2P Java 2.14.0 Snark classes. No
  compiler, CTest, sanitizer, fuzzer, Java peer, live DHT/tracker/peer or
  workflow run was executed.
- DHT: valid queries from nodes the routing table cannot store now receive
  replies; response ingestion is capped at k=8 nodes and 64 fresh peer values;
  query lifetime is 60 s; authenticated responses clear prior failure state;
  incoming announce tokens are bound to the queried infohash, refreshed on
  legitimate re-announce and evicted instead of causing a silent non-reply.
  DHT state names are validated as a single safe leaf, loads are record-capped,
  state is loaded before the receiver is published and non-delegated saves use
  atomic no-follow replacement.
- PEX/messages: receive budget is two messages/100 candidates per 60 s, zero
  destinations are dropped, outgoing PEX advertises only established non-seed
  peers, extension IDs must be positive and are write-once, private torrents
  with metadata neither advertise nor serve ut_metadata, and i2p_dht hints are
  port-checked, write-once and limited to one per minute per connection.
- Trackers: malformed bodies and peer-list errors are failures with bounded
  text; transport errors and non-200 responses double the interval; success
  without an interval re-bases it; advertised minimum intervals are honored;
  tracker peer lists expire; datagram transaction IDs avoid collisions and
  responses validate the local reply port. Datagram tracker errors also update
  backoff state.
- Prepared regressions cover bounded DHT peer values, safe state filenames,
  malformed tracker bodies, exponential failure backoff, success reset and
  tracker peer expiry. All remain unexecuted and no network/interoperability
  claim is made.
- Remaining: live adversarial churn, per-node blacklist/trim admission,
  global/adaptive DHT peer capacity, token-refresh get_peers, tracker tier
  failover/registry reclamation, single-flight tracker deadlines, complete DHT
  persistence retry/worker-role isolation and differential Java runs.

## 2026-10-21 — I2PSnark comments/ratings continuation (static)

- Four read-only audits covered the Java 2.14.0 ut_comment protocol/codec,
  native codec/store/persistence, TUI/Android surfaces and test/fuzz coverage.
  No compiler, CTest, sanitizer, fuzzer, Java peer, live swarm or workflow run
  was executed.
- Wire interoperability: `ut_comment` is omitted when disabled/private instead
  of advertising ID 0; incoming replies accept Java's up-to-256 entries;
  malformed entries are skipped without discarding valid ones; CR/LF normalize
  to spaces; ratings/negative ages clamp Java-style; encoder publishes
  newest-first and trims entries until the 16482-byte Java extension limit is
  met. A local add triggers an immediate bounded handshake/request.
- Store/spam/privacy: content dedupe ignores rating, per-pseudonym visible
  entries are capped, capacity pressure evicts old foreign entries instead of
  becoming permanently full, aggregate text accounting uses UTF-16 units,
  local hide/unhide is supported, and private torrents remain fully suppressed.
- UI: TUI comments render numeric order, show unrated state and shown/total/
  rating summary, use UTF-16 unit limits, expose hide/unhide, and refuse
  enable/add on private or disabled torrents. Android labels the add action as
  local, shows private policy, renders unrated comments without `0/5`, lists
  locally hidden rows and offers a bounded unhide action. The JNI request
  allowlist and command builder include COMMENT-UNHIDE.
- Prepared regressions cover malformed-entry isolation, newest-first replies,
  rating dedupe, per-owner limits, capacity eviction, unhide, state round-trip
  and comment command grammar. No dynamic or Java differential validation is
  claimed.


## 2026-10-21 — Android SAF payload continuation (static)

- Read-only audits covered the C++23 payload abstraction/piece mapping, the
  Linux isolated storage worker contract, the SAF/JNI creation/export path and
  the SAF test/CI registration. No compiler, Gradle/NDK, CTest, provider,
  emulator/device or workflow run was executed.
- Export: the broker now authorizes progressive growth up to the declared
  metadata length and requires exact final size on `finishExport`; failed or
  cancelled exports truncate to zero through the same operation. This fixes the
  previous strict-size authorization that rejected the first 16 KiB chunk.
- Document safety/recovery: read/write revalidate the persisted grant, regular
  size and document ID captured at open, so truncation and same-length
  replacement fail closed. Write mode may extend a shorter existing document;
  existing bytes remain unverified. Shorter-document resume no longer becomes
  permanently unwritable.
- Tree authority: persisted grants are reference-counted; replacing or detaching
  one torrent releases the Android permission only when no other key uses it.
  `SafPayload` re-resolves the selected root on each open, closes cached logical
  handles when the tree generation changes, and drops failed handles. A plain
  torrent removal detaches after the engine no longer needs payload authority.
- Startup/seeding: SAF initialization no longer blocks the single disk worker on
  a whole-tree inline rehash. It uses the shared one-piece-per-disk-job
  verification chain and reads the private resume bitfield; every restored piece
  is still hashed before publication. Zero-length files produce no fragment open.
- Presentation: Android now reads the actual `torrent.tunnel.N.name` and
  `torrent.tunnel.name` snapshot fields, so the storage action no longer aborts
  with an empty tunnel name.
- Prepared tests: JVM `TorrentSafPolicyTest` and `TorrentSafGrantsTest` execute in
  `testDebugUnitTest`; C++ payload coverage adds zero-length multi-file planning.
  Provider/broker/device fault tests remain pending. No dynamic validation is
  claimed.

## 2026-10-21 — Android Java 25/toolchain and lifecycle continuation (static)

- Four read-only audits covered Java/Gradle/AGP/D8/R8, ART lifecycle and
  concurrency, NDK/JNI/CMake, and Android resources/manual CI. No Gradle,
  javac, D8/R8, Lint, NDK/CMake, emulator/device or workflow run was executed.
- Toolchain: moved `ndkVersion` to the Android extension (its previous
  `defaultConfig` placement is not a valid DSL member), switched
  `buildToolsVersion` to assignment form, corrected the AGP-resolved Kotlin
  version to 2.2.10, added release `debugSymbolLevel FULL`, and enabled Lint
  text/XML reports.
- Java/D8 evidence: retained Java 25 source/target and exact JDK 25 toolchain.
  Corrected documentation so the historical standalone R8 9.4.28/`--min-api 24`
  transcript is not presented as current APG D8/R8 or `minSdk 29` proof. The
  AGP-resolved D8/R8 version remains unpinned and class-file 69 → DEX support
  remains a manual-CI obligation.
- Packaging: the universal APK now receives the highest version code instead of
  being a downgrade over every ABI split. Release APK/Lint are added to the
  manual Android job; AAB packaging is deferred because ABI splits require an
  explicit policy.
- JNI/native: `i2pd_jni` now links project warnings/hardening, hides non-JNI
  symbols, keeps static-library internals out of the dynamic table, and includes
  its own filesystem/span/set headers. The SAF bridge fails closed under
  `NO_TORRENTS`; a failed install restores the ordinary backend while Java
  refuses startup when grants exist. SAF JNI calls use a local-reference frame,
  metadata output checks `SetByteArrayRegion`, and status polls return a
  `Starting` snapshot instead of blocking behind start.
- Lifecycle/concurrency: `RouterService.onDestroy` preserves queued command
  completions; a log epoch prevents an in-flight poll from undoing `clearLogs`;
  `requestStop` no longer refuses starting/startFailed states; SAF broker bind
  retries are capped with a terminal status. SAF grants are now reference-counted
  and detached on plain removal.
- Resources/API floor: the API-29 instrumentation test no longer uses
  `String.repeat` (API 33). Hardcoded Java strings, edge-to-edge insets,
  release R8, baseline profiles, ABI/API device matrices and a process-wide
  daemon owner remain open and are recorded in TODO.
- Manual CI: the Android job now asserts class-file major 69, builds unsigned
  release/lintRelease, checks packaged `lib/arm64-v8a/libi2pd.so`, and prints
  Lint reports from their report paths. The ABI/API/device matrix remains
  documented but not executed.

## 2026-10-21 — Android native GUI continuation (static)

- Four read-only audits covered screens/navigation, state architecture,
  accessibility/Material 3 and UI/resource/CI coverage. No Gradle/Lint,
  instrumentation, emulator/device or workflow run was executed.
- Dashboard: native status name and error are shown as a bounded detail line;
  Reseed is available as a connected/not-reseeding-gated action.
- Settings: the listening port is editable with the existing port validator and
  restart apply mode; a Notifications row opens Android app notification settings
  and reports enabled/disabled state.
- Statistics: instantaneous download/upload rates are shown alongside the 15 s
  averages and the chart exposes its retained-window peak.
- Logs: chips now apply minimum-severity semantics (Warn includes errors),
  filtering is a pure JVM helper, filters wrap instead of hiding behind a
  horizontal scroll, the summary is a polite live region and each log row is one
  screen-reader stop.
- I2PSnark: validated Start all/Stop all/Reannounce all commands were added to
  the rich per-torrent panel, and the selected session name is shown in the list
  status. SAF selection/export strings were moved into the English catalogue.
- Accessibility/Material: address-book toolbar title survives recreation, the
  fragment container gets an accessibility pane title, the graceful-stop
  one-second text is no longer a live region, and the chart reports an ImageView
  accessibility role.
- Prepared `UiContractTest` layout/accessibility checks and JVM tests for router
  detail, reseed gating, log severity/query and bulk commands. Device execution,
  per-tunnel inventory/live transit control, SAF source seeding, queue/bandwidth
  policy and the full ABI/API device matrix remain pending.

## 2026-10-21 — i2pd-tools modernization continuation (static)

- Four read-only audits covered the 15 native tools/Perl scripts, upstream
  provenance/submodules/dependencies, CLI/key-format/firewall behavior and
  CMake/CTest/install/docs. No compiler, CMake, CTest, Perl/prove, tool,
  network or workflow run was executed.
- Configuration: `autoconf_i2pd` no longer emits the removed `[http] lang`
  section and the transcript now asserts its absence. Key-type names use one
  table with P521/EdDSA/GOST aliases; keygen refuses unavailable/fallback
  generators instead of writing a mislabelled key. Offline-key tools reject
  non-offline fallback output.
- Shared C++: added an RAII crypto guard and exact-consumption
  `LoadPrivateKeys`; keygen/offline-key tools use them. `routerinfo` now reads
  through the bounded common reader. `x25519`, `b33address` and `verifyhost`
  reject unknown/extra CLI input; `b33address` reports failures non-zero.
- CMake/install: the `Tools` component installs `LICENSE.i2pd-tools`;
  `perl-install-layout` checks every declared binary plus the notice; the
  inventory test fails on a missing upstream manifest or an unregistered
  top-level tool source. Windows ZIP/installer payloads include the tool
  licence.
- Prepared regressions: keygen `--help`/unsupported RSA, x25519/b33address
  option surfaces and autoconf absence checks. Dynamic CTest, Windows CLI,
  firewall syntax validation, fuzzing and packaging runs remain pending.

## 2026-10-21 — Python-free Perl hardening continuation (static)

- Four read-only audits covered Python/shebang/consumer inventory, `i2pdctl`
  quality, Test::More/CMake/CI integration and portable-Perl security. No Perl,
  prove, CMake, CTest, network, tool or workflow run was executed.
- Inventory: confirmed no maintained Python source or shebang remains; removed
  the obsolete Python block from `.gitignore`. The portability guard now covers
  versioned `python3`/`pip`/`PYTHONPATH`, Cython/notebook/template forms, BOM
  and line-2 shebangs, `require` imports, pipe/temp/chmod hazards, Gradle,
  Debian/RPM/static/Docker/Windows consumers, missing consumer files and
  inventory drift.
- `i2pdctl`: only absolute `PATH` entries are accepted for `openssl`, which is
  resolved with `Config`/`File::Spec`; proxy variables are deleted in a
  localized `%ENV`; UTF-8 URL/password values are decoded at the environment
  boundary; non-ASCII/format/control server and CLI values are escaped; duplicate
  `raw_info` methods collapse to one JSON key.
- Tests: added authentication-failure, UTF-8-password and duplicate-`raw_info`
  cases; hardened branding/vector scripts (NUL/`..` link rejection, script-
  relative branding root, absolute/PATH-checked git and validated provenance
  paths/lines). Added component-install assertions for all tools, `i2pdctl` and
  `LICENSE.i2pd-tools`, and an optional Perl::Critic lane that skips cleanly
  when unavailable.
- Docs/TODO: updated the Perl migration, CI/testing/provenance notes and
  PERL-007/008. Dynamic `prove`, CTest, OpenBSD/Windows, live TLS and package
  runs remain pending.

## 2026-10-21 — routerinfo firewall generator continuation (static)

- Four read-only audits covered legacy iptables/ip6tables, nftables, Windows
  Defender Firewall and OpenBSD PF plus the shared endpoint model. No compiler,
  CTest, routerinfo, iptables/nft/pf/PowerShell or workflow run was executed.
- Common validation is locale-independent ASCII; nft refusals are diagnosed
  instead of silently dropped; duplicate endpoints are deduplicated before the
  4096 budget and overflow is terminal. nft `ip6` and Yggdrasil firewall modes
  now require `-6`, and `-f` conflicts are checked before file operands.
- nft rules carry a validated `comment "openi2pd <transport> <tag>"`; PF rules
  carry the equivalent validated `label`; Windows rules use a stable `-Name`,
  `-Group openi2pd`, an address-family token and `-ErrorAction Stop` while
  preserving the existing remote outbound rule. Legacy `-f` fragment text is
  unchanged.
- Rule headers are emitted lazily only when a file contributes at least one
  rule. Updated C++ goldens and CLI negative cases for the new behavior.
- Docs/TODO: routerinfo backend examples, provenance/testing notes and
  FIREWALL-006/010 plus TOOL-012 tracking. Manual syntax validation
  (`iptables-restore --test`, `nft -c`, `pfctl -nf`, PowerShell `-WhatIf`)
  remains pending; no firewall was modified.

## 2026-10-21 — Documentation, identity and resource consolidation (static)

- Four read-only audits covered Markdown/navigation, inherited i2pd-docs,
  branding/light-dark identity and documentation/branding CI checks. No
  MkDocs, link/branding validator, CMake/CTest, Android/Gradle, TUI or workflow
  run was executed.
- Documentation facts corrected for CMake 3.24, Android torrent/UPnP support,
  I2CP default, Yggdrasil SSU removal, AppArmor/IRC paths, FAQ torrent support,
  family certificate path, `io_context`, Windows/proxy branding, and the
  documented BSD-3-Clause imported-doc license. Added substantive history/API
  index pages and reduced the duplicate docs README to a pointer.
- Navigation: removed unsupported upstream iOS/Qt/Haiku pages from the
  published nav while retaining source/license files as excluded provenance;
  added file-header policy and Linux/OpenBSD build labels; added
  `docs-nav-static` for nav targets, theme assets, unlisted Markdown and HTML.
- Branding: proxy information page now has dark/error tokens, night Android
  resources include the launcher plate, palette labels and binary-validation
  wording are corrected, and validator mirror checks now cover SVGs, PNGs and
  `colors.md` fail-closed.
- Link checker now accepts repository-contained parent links, handles setext and
  duplicate headings, rejects NUL/escape targets and fails on empty input.
- Prepared/updated `DOCS-011`, `BRAND-012`, docs/testing/CI notes and the manual
  check list. Dynamic documentation, MkDocs, branding, Android/device and TUI
  validation remains pending.

## 2026-10-22 — native service supervision continuation (static)

- Four read-only audits covered the daemon CLI/signal/path contract, the
  supervisor and worker process tree, every `contrib/` service definition and
  the CMake/service/CI install graph. No compiler, CMake configure/build,
  CTest, service manager, package tool, `systemd-analyze`, daemon, device or
  workflow was run; shellcheck and YAML parsing were used as static checks
  only.
- `Daemon.init()` failure now exits nonzero instead of falling through to
  `EXIT_SUCCESS`; this lets `Restart=on-failure`, OpenRC respawn, s6/runit and
  Shepherd react to a refused storage worker or other initialisation failure.
- systemd: `TimeoutStopSec=660` covers the daemon's 600-second drain plus
  shutdown margin, `SyslogIdentifier=open-i2pd` matches the supervisor prefix,
  the unowned `open-i2pd.service` alias was removed, and `sysusers`/`tmpfiles`
  sources let package managers create the account, state, runtime and log
  paths. The unit still uses `Type=exec`, `KillMode=mixed`, no readiness claim
  and no privilege-separation claim.
- OpenRC: `stopsig` now selects the stop signal and a matching retry budget
  (`SIGINT/660` for graceful stop, `SIGTERM/30` otherwise); the private umask
  is applied; the pidfile parent is derived from `I2PD_PIDFILE` instead of
  assuming `I2PD_RUNDIR`.
- runit adds a private creation mask to the foreground run script. s6 and
  s6-rc gain native `down-signal=INT` and `timeout-kill=660000`, so
  `s6-svc -d` requests the daemon's graceful path rather than an immediate
  `SIGTERM` escalation. SysVinit keeps the daemon-owned, identity-checked
  pidfile instead of unlinking it after stop, and uses a 660-second stop
  budget. Shepherd uses a pidfile under the data directory and a 700-second
  stop grace period; its process-group signalling caveat is documented.
- OpenBSD rc.d now runs `--foreground --service --pidfile=...` under
  `rc_bg=YES`, raises the login-class descriptor limit to 8192 and applies a
  private umask. Documentation no longer describes the path as a
  self-daemonising legacy exception. The unselected legacy Upstart sample was
  changed to foreground supervision with a 660-second SIGINT stop, and the
  dinit sample received the same stop budget.
- CMake installs exact `PROGRAMS`/`FILES` entries instead of
  `USE_SOURCE_PERMISSIONS` directory copies for runit, s6 and s6-rc, and
  installs the systemd sysusers/tmpfiles files. `I2PD_SERVICE_CONFDIR` defaults
  to the manager path only after an explicit override is checked.
- `tests/check-services.cmake` now pins the stop budgets, sysusers/tmpfiles,
  s6 down-signal/timeout files, s6/s6-rc run-script identity and OpenRC
  signal/umask/pidfile-parent rules. `tests/check-build-system.cmake` counts
  each explicit install source. The manual packaging workflow additionally
  syntax-checks the OpenRC and runit shell scripts and installs the static
  validators; native start/stop/reload, `s6-rc-compile`, Shepherd load,
  `systemd-sysusers`/`systemd-tmpfiles`, package lifecycle and sandbox checks
  remain pending CI.
- Static validation only: source inspection, `git diff --check`, YAML parsing
  and shellcheck on the shell service scripts. No service was started, no
  package built and no workflow dispatched.

## 2026-10-22 — Debian and OpenBSD packaging hardening (static)

- Read-only audits covered Debian manifests/metadata/maintainer scripts, the
  CMake daemon/TUI/tools/worker install graph, OpenBSD service/port material
  and the manual packaging workflow. No CMake configure/build, package build,
  lintian, autopkgtest, `dpkg-deb`, `pkg_create`/`pkg_add`, `rcctl`, service
  manager or workflow was run.
- Debian hardening now uses the dpkg-buildflags values despite the direct
  CMake invocation; `perl` is obtained through
  `${perl:Depends}` and optional OpenSSL moved to Recommends. The daemon
  package claims `LICENSE.i2pd-tools` and a CC-BY-SA notice, while the
  `i2pd-tui` package owns the desktop launcher and icons. The launcher opens
  `i2pd-tui`, never a second router.
- `debian/postinst` creates only missing state/log/runtime paths and no longer
  resets administrator ownership/modes on upgrade. Logs use the private
  0700/0600 policy across the main, static and tmpfiles definitions.
- DEP-5 now separates BSD-3-Clause, GPL-2+ historical packaging files,
  CC-BY-SA first-party documentation and Apache-2.0 Android wrapper files.
  The lintian override now uses the binary-package filename; uscan accepts
  GitHub `archive/refs/tags` URLs.
- CMake adds a `Tui` component for `i2pd-tui` and its desktop assets, installs
  only man pages belonging to enabled targets, includes the documentation
  licence in `Documentation`, and rejects `Service` selection without
  `WITH_BINARY=ON`.
- Static Debian packaging uses a native version without `-1`, current control
  fields, one debhelper service action and the documentation notice. The
  manual packaging workflow now runs `check-packaging`, installed-unit
  verification, manifest/split checks, conffile reinstall preservation,
  `autopkgtest`, and remove/purge checks.
- OpenBSD canonical files now use `tunnels.conf.d`, the read-only
  `${TRUEPREFIX}/share/i2pd/certificates`, `--log=syslog`, `daemon_logger`,
  `daemon_timeout=660`, the 8192/umask login class and executable rc.d mode.
  `contrib/openbsd/port/` adds a merge template with Makefile, PLIST
  (`@newgroup`/`@newuser`/`@rcscript`/`@sample`), package README and man pages.
  It is not selected by CMake and is not validated by a live port build.
- No dynamic validation: Debian binary packages, lintian, autopkgtest,
  upgrade/remove/purge, CMake component installs, RPM/static bundles, OpenBSD
  ports/PLIST/pkg_add, rcctl and service lifecycle remain pending CI.
  Statically checked only: the `services-static`, `packaging-static` and
  `build-system-static` CMake source contracts, shell syntax, shellcheck on
  maintained scripts, YAML parsing and `git diff --check`.
  The project-wide privilege-separation boundary is unchanged; network,
  crypto and core workers are not separated by these service definitions.


## 2026-10-21/22 — validation infrastructure and remote dispatch (static + partial remote)

- Read-only audits covered CTest registration/labels, all manual workflows,
  interoperability/IPC/torrent/Android distribution, sanitizer/fuzzer wiring
  and packaging CI. No local compile, CTest, sanitizer, fuzzer, Gradle,
  package or validator was run.
- CTest: added per-test timeouts, fixed Windows `bcrypt` linkage, disabled
  torrent targets when `WITH_TORRENTS=OFF`, corrected security/memory/i2p/
  interop labels, made the Landlock test report SKIP (77) instead of a false
  pass, and made `i2pd-version` assert its output.
- CMake: `WITH_FUZZERS` now requires ASan+UBSan instrumentation, UBSan recovery
  is disabled, unsupported Windows sanitizer requests fail closed, and the
  fuzzer workflow inventory is part of `build-system-static`.
- Workflows: added pinned `upload-artifact` evidence for Linux CTest,
  install-component trees, Android reports/APKs, sanitizer logs, Windows
  CTest/package lists, OpenBSD logs, Debian packages/lintian logs and fuzz
  corpora; manual concurrency no longer cancels an earlier validation.
- Fuzzing now runs `ctest -L fuzz`, continues through all targets after a
  crash, and uploads corpora/crash inputs. Sanitizers use `Debug` and
  `-fno-sanitize-recover=all`. MSYS2 is pinned to `windows-2022` and asserts
  `i2pd-tui`; Android sets `WITH_HARDENING` in the Gradle CMake arguments and
  checks JVM test results.
- Dispatched the published `main@284544a5256043755b197f61e42aee56dbf65032`
  with `gh`; these runs do not include the local uncommitted tree:
  Linux/Android `39688898520`, Windows `39688906891`, OpenBSD `39688913964`,
  Fuzzing `39688921868`, Packaging `39688929467`. The interop job and service
  contracts passed; Linux GCC/Clang, Android, ASan/UBSan, TSan, OpenBSD,
  Windows MSYS2, Fuzzing and the Debian package build failed at the published
  `libi2pd/Transports.cpp:693` parse error. The local worktree has a different
  uncommitted `ConnectToPeer` implementation, so these failures are not
  attributed to local fixes. Windows MSVC later failed with the same published
  `Transports.cpp` parse error.
- No commit, push, tag or release was created.

## 2026-10-22 — Agent Skills coherence continuation (static)

- Four read-only audits covered every `SKILL.md`/reference, AGENTS.md policy
  mapping, reusable documentation/procedures and the Agent Skills checker/CI
  wiring. No skill was executed and no compiler, CTest, Gradle, workflow,
  `gh` call or commit/push was performed.
- All 16 skills now require at least one read-only reconnaissance subagent
  before editing and state the no-local-execution fallback. Their `## Do not`
  sections link the AGENTS.md static-only, no-download, license/provenance,
  worktree/commit and manual-workflow rules; tracking language now proposes
  rows to the coordinator and records the exact AGENTS.md state.
- Descriptions were tightened for memory/C++23, Linux/Windows vs
  privsep/TUI, documentation vs tracking and tooling/Perl boundaries.
  `openi2pd-tooling` gained `references/perl-inventory.md` and a Perl section;
  no separate Perl or IPC skill was added because it would duplicate existing
  tooling/privsep/platform ownership.
- Updated the discovery README selection path, IPC protocol ownership
  arbitration, maintenance checklist and references. Packaging/build/CI/
  documentation/OpenBSD/torrent/tooling references now reflect the `Tui`
  component, systemd sysusers/tmpfiles, the OpenBSD `port/` template, current
  CTest labels, `check-doc-nav.pl`, OpenSSL expectations and torrent/TUI IPC
  documents.
- `tests/check-agent-skills.cmake` now enforces exact directory/README
  inventory, LICENSE headers, unique bounded single-line descriptions, required
  sections including `Do not`/`References`, linked and licensed references and
  no unexpected skill directory. It passed locally as a compile-free source
  check; manual CI and docs-link/branding validation remain pending.
- `TODO.md` GOV-007, `AGENTS.md`, `docs/testing.md` and the skills index were
  updated. No commit or push was created.

## 2026-10-22 — Modern C++23 / Java 25 utilization continuation (static)

- AGENTS.md now contains a permanent `Modern language utilization policy`:
  modern facilities and OOP/generic abstractions are a goal even without a
  measurable performance gain, bounded by explicit compiler/library/API
  availability and the existing protocol/security constraints.
- C++: the X25519 key-supplier worker now uses `std::jthread`, a stop token and
  `condition_variable_any`; the TUI IPC server also uses `std::jthread` and
  stop-token cancellation; outbound NTCP2/SSU2/Yggdrasil selection moved behind
  `ITransportConnector` strategy classes; Identity's signing-key wire-size
  switch was replaced by a `constexpr` registry (with `Modern.h` utilities), the
  logger level parser uses a static range table, and transport/Modern
  regressions were prepared.
- Android: Java 25 source features are exercised through sealed
  `RouterCommand` records with exhaustive pattern matching and a typed
  `LogLevel` enum; the service dispatcher and log filtering consumers were
  updated. No unsupported Java library API (virtual threads/scoped values) was
  introduced. Prepared regressions are `test-modern`,
  `test-transport-connector`, `RouterCommandTest` and `LogFilterTest`.
- Updated `docs/cpp23-migration.md`, `docs/android-java25.md`, the C++23 and
  Android Agent Skills, `TODO.md` (CPP-015/ANDROID-016) and `ROADMAP.md`.
- No compiler, CTest, Gradle/JNI, sanitizer, fuzzer, OpenBSD/Windows, device or
  live-peer validation exists for this increment. No commit or push was made.

## 2026-10-22 — Agent knowledge maintenance continuation (static)

- Added the permanent `Agent Knowledge Maintenance` section to `AGENTS.md`.
  Every significant change must now review AGENTS, the owning skill/references,
  TODO/ROADMAP/PROGRESS and the related technical documentation; the review is
  mandatory, cosmetic edits are not, and the coordinator owns shared files.
- Corrected stale policy statements: manual workflows are dispatch-only with
  no `workflow_call`, the TODO status vocabulary is referenced, shared
  interfaces include Modern.h/TransportConnector.h and the Android/tests
  boundaries, OpenSSL 3.0.2–4.0.x/LibreSSL 4.0+, Windows TUI, the four
  official UI languages, Markdown-only docs and Perl scope are explicit, and the current Java target is
  2.14.0 (2.13.0 remains historical).
- Added `Known issues and decisions` to all 16 skills with subsystem-specific
  pitfalls: Modern.h/Strategy contracts, Transports shutdown ordering,
  jthread/TUI cleanup, Android RouterCommand/LogLevel invariants, SAF
  cancellation, torrent storage/SNARK state, OpenBSD/privsep limits, packaging
  and CI evidence rules. Updated the agent-skills checker and README
  maintenance contract to require the section.
- Added `GOV-008`, updated `CPP-015`/`ANDROID-016`, and corrected ROADMAP/PROGRESS
  historical local-build claims and 2.13.0 target drift. TUI/transport
  lifecycle and seam regressions remain prepared, and the compile-free CMake
  contracts are the only executed checks; no compiler, CTest, Gradle, device or
  workflow was run.

## 2026-10-22 — UI internationalization continuation (static)

- Official UI languages are now English (source/fallback), Spanish, Russian
  and Simplified Chinese, with Traditional Chinese allowed only after verified
  translation. The previous English-only Android constraint is superseded.
- Native catalogs are selected by a process-local registry (`--lang`,
  `[tui] language`/`lang`, then `LC_ALL`/`LC_MESSAGES`/`LANG`) independent of
  the router client. The TUI links `libi2pdlang`, translates exact render lines,
  preserves leading layout spaces, pins `LC_NUMERIC=C` and treats `Put` as a
  column budget. HTTP proxy messages use the same en/es/ru/zh-Hans catalogs.
- Android has complete `values-es`, `values-ru` and `values-zh-rCN` catalogs,
  synchronized locale metadata/filters, a Settings per-app language selector,
  active-UI-locale number formatting and a replacement locale-parity guard.
  `LocaleResourceTest` is prepared for the manual device lane.
- Added the 17th skill `openi2pd-i18n`, `test-i18n-locale`, `i18n-static`,
  `android-locale-policy` and `check-android-locales.pl`; updated AGENTS,
  README/docs, TODO (I18N-001..003), ROADMAP and the compile-free workflow.
- No compiler, CTest, Gradle, AAPT2, Lint, terminal, device or workflow run was
  performed. The detailed TUI help/tool catalog expansion remains tracked as
  I18N-003.

## 2026-10-22 — Signature-name normalization correction (static)

- Status: Implemented / static; Pending CI compilation; Pending CI tests.
- Replaced the dangling `ToUpper` call in `tools/common/key.hpp` with a local
  `std::ranges::transform`, preserving case-insensitive signature aliases and
  unsigned-character conversion before `std::toupper`.
- Prepared a `tools-regressions` mixed-case/underscore keygen case and updated
  the tooling skill decision. Existing AGENTS, TODO/ROADMAP and tool CI wiring
  remain applicable. No compiler, tool executable or test was run locally.

## 2026-10-22 — Publication review and commit organization (static)

- Reviewed modified, deleted and untracked paths with independent read-only
  native, platform and support reviews. Grouped source changes, paired
  regressions, packaging, build/CI integration and knowledge tracking into
  sequential subsystem commits under explicit user authorization.
- Preserved existing work and the configured Git author, committer and signing
  requirements. Ignored `audit/` content remains outside the commit series;
  inspected untracked files are maintained source, configuration or licenses.
- Static checks: per-commit `git diff --cached --check`, XML/JSON parsing and
  credential/artifact inspection. The OpenBSD port README's seven-character
  Setext underline under "Logging" triggers a reviewed conflict-marker false
  positive; its surrounding prose has no merge conflict and is preserved.
  Existing documentation key examples retain
  their historical provenance. No local build, test, project validator or
  manual workflow was executed; compilation and dynamic tests remain pending CI.
- Updated AGENTS to distinguish the pre-publication work inventory from the
  resulting history. TODO/ROADMAP validation states remain unchanged. Final
  push and remote synchronization results are reported in the user handoff.

## 2026-10-22 — Static compilation and linkage audit

- Baseline: clean published `e669765d`. Status: Implemented / static;
  Pending CI compilation; Pending CI tests. Tasks BUILD-018/TEST-023; separate
  runtime follow-up ANDROID-017. No commits or push made.
- Six specialized subagents reviewed C++23 core/header seams; transport,
  cryptography/netDb/client protocols; IPC/supervisor/platforms; native
  torrent/TUI/tools; Android Java/JNI/resources; and CMake/dependency/export
  integration. A second pass inspected recently modified native test sources.
- Corrected demonstrated blockers: missing compiled Boost.JSON/URL linkage and
  exported discovery; qualified translation helpers defined only globally;
  torrent fixture `info` used before declaration; newer Landlock UAPI
  fields/constants assumed in older headers; ELF hardening flags applied to
  MinGW PE links.
- SB-004 is Dropped (pinned API supports overload): final primary-source
  verification showed Material 1.14.0 accepts `setHint(@StringRes int)`.
  Removed the audit's four unnecessary edits; Android source stays unchanged.
- Removed transitive declaration/usage assumptions with direct POSIX `renameat`
  and `shared_ptr` includes, PUBLIC Windows SDK macros and explicit shell32
  import linkage. Corrected a contradictory outer-expected assertion to check
  the intended empty inner optional in the Linux IPC regression.
- Prepared `test-boost-linkage`, extended locale helper/header coverage and
  existing fixture regressions; the manual installed consumer links client
  exports and calls JSON/URL implementations. MSYS2 enables hardening. Updated
  build/packaging contracts, Docker runtime dependencies, technical docs,
  AGENTS and owning skills. No workflow triggers changed.
- Source/interface reviews found no further demonstrated symbol mismatch in
  the inspected encryption/offline signature, Noise-state, tunnel ownership,
  transport connector, JNI or SAF contracts. OpenSSL legacy EC code is guarded
  for LibreSSL, not an unconditional OpenSSL 4 API dependency.
- `docs/static-build-audit.md` records findings, proof, reviewed boundaries and
  residuals. Android native Starting-state re-entry is a separate runtime
  concern, left open for an owner-consistent lifecycle fix and device test.
- No compiler, CMake configure, Make/Ninja, Gradle, CTest, test executable,
  project validator, linter, dependency install or manual CI dispatch ran.
  Static source/diff and text parsing checks do not establish compilation.

## 2026-10-22 — Independent profile, Android lifecycle and firewall continuation

- Baseline: clean published `91ce7075`. Three read-only reconnaissance owners
  reviewed AGENTS, the skills index and owning skills, TODO/ROADMAP/PROGRESS,
  production callers, prepared tests and manual CI consumers before receiving
  independent implementation scopes. Coordinator owns CMake, CI, instructions
  and shared tracking; cross-review does not authorize overlapping edits.
- CONC-009 (slice of CONC-005): synchronized RouterProfile state/RNG and
  canonical first publication, returned endpoint copies, serialized per-profile
  Save/Load, claimed persistence atomically and retained dirty state after
  failed writes/loads. Cache eviction requires map-only shared ownership;
  postponed queue inspection is locked. Cross-review caught the expiry-to-erase
  owner gap and failed-load dirty clearing; both were corrected statically.
- ANDROID-017: moved native start-transition ownership to the portable
  `DaemonLifecycle.h` seam; the adapter invokes the actual daemon start while
  Starting is published. JNI NativeCallGuard serialization remains required.
  Returned/thrown start failure after successful init retains teardown for
  crypto/logger resources. Read-only inspection found the common subsystem
  stop methods guard/reset state; the transport-start failure comment alone
  does not prove repeated stop unsafe. Failed partial initialization is still
  a separate concrete gap, recorded as ANDROID-018 rather than claimed fixed.
- TOOL-012: preserved exact IPv4 legacy firewall fragments and selected
  `ip6tables` explicitly for IPv6. A BUILD_TESTING-only, non-installed helper
  creates a signed public RouterInfo through production APIs in the CI work
  directory, with ephemeral signing keys kept in memory. No binary fixture,
  private key or generated output was added to the repository.
- Prepared `test-router-profile-race`, `test-android-daemon-lifecycle`, expanded
  `test-routerinfo-firewall` and positive `tools-smoke` CLI cases. Registered
  native targets/labels and added a focused manual Linux workflow step; full
  CTest and sanitizer jobs also consume them. Triggers and action pins remain
  unchanged. Actual JNI/device restart and native firewall syntax are pending.
- Updated owning skills/references, security/Android/testing/tool docs, AGENTS,
  TODO and ROADMAP. Global profile storage/delete coordination, crash-safe INI
  replacement, SSU2/RouterInfo/torrent callback lifetimes, Java owner epochs and
  global process separation remain implementation work, not test-only tasks.
- Status: Implemented / static; Pending CI compilation; Pending CI tests.
  Only source/interface review and `git diff --check` were performed. No local
  build, test, project validator, fixture generation, workflow dispatch, commit
  or push occurred. Local uncommitted changes cannot be validated by a workflow
  running a previously published revision.

## 2026-10-22 — Profile storage and failed-init continuation

- Reactivated three reconnaissance/review owners; independent profile and
  daemon implementations shared no files. Coordinator reviewed CMake/CI and
  maintained shared instructions, skills and tracking. Preserved all prior work.
- CONC-010: immutable initialized canonical profile root, process-local
  Save/Load/expiry serialization and flushed same-directory replacement. POSIX
  temp creation/rename use an opened immediate-parent dirfd; Windows flushes
  the file before replace-existing MoveFileExW. Shutdown drains owned futures
  and postponed updates before final save after profile producers stop.
- Review corrected Boost stream serialization, a path declaration ambiguity,
  Windows test descriptor lifetime and startup temporary-file deletion that
  incorrectly assumed cross-platform exclusive ownership. No arbitrary PID
  probe or wider OpenBSD pledge is introduced. Crash-left staging cleanup,
  cross-process enforcement, hostile ancestors/reparse changes and arbitrary
  retained mutators remain open. Directory fsync failure after rename is
  explicitly ambiguous and retains dirty state.
- ANDROID-018: typed fixed-capacity daemon initialization ledger registers
  logging/crypto/storage/context before acquisition and unwinds false/throw
  paths in reverse. Context persistence drains before storage revocation;
  isolation failure retains a failing writer. Stopped logger destination reset
  releases stream/sink resources and restores stdout. Durable keys and
  irreversible configuration/filesystem/pledge effects are not reverted.
- Prepared storage and rollback regressions plus log-lifecycle extensions;
  registered both targets/labels and the manual Linux focused lane. Synthetic
  ledger tests do not invoke production callbacks. Full CI compilation,
  platform durability/sanitizer tests and Android JNI/device evidence remain
  pending. Reviewed language matrices: existing RAII/generic facilities suffice;
  no new language/runtime availability claim was introduced.
- Status: Implemented / static; Pending CI compilation; Pending CI tests.
  Source/interface review and `git diff --check` only. No local build, test,
  project validator, workflow dispatch, commit or push occurred.

## 2026-10-22 — Static receiver, restart and maintenance hardening

- Preserved all previous local work. Three owners recognized tooling, Datagram
  teardown and Android callbacks from instructions/skills/tracking and code.
  Separate read-only passes reviewed profile cleanup, Queue callers, NetDb
  admission and final interfaces. Agent capacity prevented nested reconnaissance
  while all slots were occupied; coordinator/subagent cross-review supplied
  independent inspection, without replacing it with execution.
- CONC-011: registration identity tokens, in-flight slots and external drain
  replace Datagram port-only reset; migrated UDP tunnels, address-book/resolver,
  SAM and torrent DHT/raw receiver owners. Shared-owned destination registration
  is enforced. Review corrected SAM member ownership, header closure, callback
  destruction under registry locks and same/cross-slot reset wait cycles.
  Callback-context reset deactivates but returns false without a lifetime
  barrier. No wire or persisted format changed.
- CONC-012/DEF-026: tunnel/netDb Post admission shares a short mutex with stop
  close, lifecycle transitions serialize separately, workers check stop after
  wake, and joins precede queue drain. NetDb clears lookup/flood throttles under
  their mutex; thread-construction failure keeps admission closed. NetDb's
  regression fixture opens admission with a paused worker, avoiding network,
  storage or reseed startup. Producer quiescence across restart is still needed.
- CONC-010 follow-up: expiry checks canonical hash encoding and expected shard
  path, then non-symlink regular leaf status. Unknown/staging/backup/mis-sharded
  old files remain; hostile concurrent filesystem changes remain unsupported.
- ANDROID-019: stale listener snapshots no longer update retired service state.
  Every service worker submission uses an owner executor on a process FIFO;
  retirement inserts mandatory native stop before new-owner work. Accepted
  work drains before stop; stale submissions/retirement cannot affect a newer
  owner. JNI/commands run outside the owner monitor. No NativeCallGuard bypass.
- TOOL-013: reported Perl/ShellCheck corrections were already in HEAD. Fixed
  remaining CDPATH root capture and SAM mismatch exit status. Manual packaging
  CI now ShellChecks the six maintenance scripts and checks failure with a fake
  nc, without a live router. Triggers/action pins/credentials remain unchanged.
- Prepared receiver, NetDb, expanded tunnel/profile and JVM owner regressions;
  registered host targets/labels and the manual focused Linux selection.
  Updated skills/references, AGENTS, technical docs and shared tracking.
- Status: Implemented / static; Pending CI compilation; Pending CI tests.
  Static source/interface review and git diff --check only. No build, test,
  project validator, ShellCheck/Perl invocation, workflow dispatch, commit or
  push ran. Full platform/JNI/service teardown and sanitizer evidence is pending.

## 2026-10-22 — Publication review

- Explicit user authorization supersedes the earlier no-commit/no-push task
  boundary for this publication. Existing Git identity and signing settings
  remain unchanged; dynamic validation remains manual-CI-only.
- Independent native, Android/daemon and tooling/integration reviews inspected
  all pending source, tests, workflows, instructions, tracking and new files.
  Datagram review identified shared mutable callback-target invocation as a
  regression from the previous per-dispatch std::function copy. Restored the
  callback snapshot before incrementing the in-flight count and prepared a
  mutable-value-capture regression; no tests were executed.
- Corrected the SAM mock diagnostic matcher to include the existing same-line
  HELLO prompt prefix; the manual CI fixture must not expect the diagnostic at
  the beginning of the output line.
- Changes are grouped into dependency-ordered subsystem commits, including
  selective test-registration and NetDb profile-shutdown hunks. Shared status
  documentation records static implementation, not compilation/test evidence.
- Source/diff review, artifact inspection and git diff --check only; no build,
  test, project validator or workflow dispatch ran during publication review.

## 2026-10-24 — Static BOB and I2PControl security hardening

- SEC-026 removes BOB `setkeys` and I2PControl password values from logs.
  BOB tunnel callbacks now use weak derived ownership, registered pending
  receivers and atomic stopped/admission gates across accept, read, LeaseSet
  lookup and stream delivery; late streams close instead of reviving stopped
  tunnels or publishing handlers after drain.
- I2PControl caps active TLS connections, tokens and successful authentication
  issuance; purges expiry before use; rejects empty passwords and the public
  default password on non-loopback TCP listeners. TLS handshake, bounded HTTP
  header/body reads and response writes now carry cancellable deadlines and no
  blocking read remains on the event-loop thread.
- Certificate/key PEM is serialized in memory and committed with
  `AtomicWritePrivate`: POSIX uses mode 0600/no-follow and Windows uses a
  protected user/SYSTEM DACL plus reparse-target rejection, eliminating
  chmod-then-reopen. Win32 hostile-parent traversal and two-file pair
  transactionality remain residual. Added the compile-free
  `control-security-static` contract and updated AGENTS/skills with reusable
  secret-redaction, callback-lifetime, admission/deadline and private-file rules.
- Status: Implemented / static; Pending CI compilation; Pending CI tests. No
  compiler, test runner, project validator, workflow dispatch, commit or push
  was used. Manual security/memory, ASan/UBSan/TSan, slow-client, BOB teardown
  and POSIX/Windows file-authority validation remains required.

## 2026-10-24 — Manual CI failure triage at `2aeb8135`

- Inspected the five manual `workflow_dispatch` runs started at
  `2026-10-24T17:31Z` with `gh run view --log-failed`. The failures are
  compile/configuration defects, not peer or runtime evidence.
- `privsep/Channel.cpp`: the empty-record retry used `continue` after the
  receive retry loop had already closed, so every C++ target failed to parse
  the file. The record handling now stays inside the loop, preserving the
  zero-length-record budget, credential check and hangup distinction.
- `libi2pd/LeaseSet.h` and `libi2pd_client/I2PService.h` use `LogPrint` in
  inline code without including `Log.h`; translation units that did not include
  the logger first failed on Linux, Android and Windows. Both headers now
  include the logger directly.
- `tests/check-agent-skills.cmake` ran as a CMake script without CMP0057, and
  the README inventory loop iterated a `file(READ)` string whose semicolons
  split it into fragments. The policy is set explicitly and the loop reads
  real lines with `file(STRINGS ...)`.
- OpenBSD configuration failed in `FindCurses` because CMake's wide-character
  search only accepts the `ncursesw/` header layout while base OpenBSD
  ncurses installs `ncurses.h`; the nonexistent `ncurses` package was also
  requested by `pkg_add`. Configuration now retries the plain layout and
  requires a `wadd_wch` entry point, and the workflow installs only existing
  packages. The TUI contract documentation records the fallback.
- The packaging service-contract job requested `openrc`, which Ubuntu does not
  publish; the job is static and only needs `shellcheck`, so the unused
  package requests were removed.
- Status: Implemented / static; Pending CI compilation; Pending CI tests. No
  compiler, test runner, project validator or workflow dispatch was used.
  Publishing this correction does not validate it; only a later manual run
  can.

## 2026-10-25 — Second manual CI triage at `d5d0bd6f`

- The five runs started at `2026-10-24T17:45Z` all reached the same new
  compile error in `libi2pd_client/TorrentsDHT.cpp`: the ping-expiry branch
  passed the queried peer identity (`IdentHash`) to `m_HeardFrom.contains`,
  which is a `std::map<NodeID, ...>`. `std::less<NodeID>` is not transparent
  and no derived-key conversion exists, so every platform failed to compile
  the file.
- The timeout record only stores the peer. `RoutingTable::FindNodeByPeer`
  resolves that peer to the routing-table node, and the heard-from guard now
  compares against the node ID before `RemoveNodeByPeer` runs. This preserves
  the intended "do not evict a node we recently heard from" rule instead of
  comparing the peer hash with node IDs.
- `test-torrents-dht-interop` now checks peer-to-node resolution, the absent
  peer case and peer-bound removal.
- Earlier corrections are confirmed by these runs: the Agent Skills static
  contract and the packaging service contract passed, OpenBSD configuration
  passed the ncurses/TUI requirement, and Android, Windows, OpenBSD, sanitizer,
  fuzzing and Debian jobs compiled past the previous `Channel.cpp`,
  `LeaseSet.h`/`I2PService.h` and `openrc` failures.
- Status: Implemented / static; Pending CI compilation; Pending CI tests. No
  compiler, test runner or project validator was used for this correction.

## 2026-10-25 — Multi-subsystem static compile/link audit

- Six read-only reconnaissance agents inspected core C++23 libraries,
  transports/crypto/netDb/client protocols, privsep/daemon/IPC, torrent/TUI/
  tools, Android Java/JNI/resources and CMake/linking. No compiler,
  configurator, test runner, Gradle, validator or workflow was executed.
- Confirmed and corrected the first blocker from the cancelled manual runs:
  `daemon/I2PControl.cpp` had a stray brace that closed `namespace client`
  before the handler definitions, and two `steady_timer::cancel(error_code)`
  calls that Boost.Asio no longer provides. The file now keeps its namespace
  scope, uses the no-argument cancel overloads, includes `<cstdio>`,
  `Crypto.h`, `<openssl/bn.h>` and `<openssl/rsa.h>` directly, avoids the
  throwing `remote_endpoint()` log path and separates shutdown from handshake
  errors. `ip::port_type` became `unsigned short`, restoring the documented
  Boost 1.74 floor.
- Removed dead declarations that would become undefined references on first
  use: `NetDb::ManageRequests` (live logic is `NetDbRequests`) and the
  undefined client `I2PUDPClientTunnel` cleanup-timer pair.
- Warnings from the last completed run are gone: unused `str_rmatch`, the
  PQ-only `token` declaration, the unused `MAX_RESEED_ENTRIES` constant (now a
  real 400-entry SU3 bound), the redundant `deny` lambda capture, and the
  deprecated `SSL_set1_host` branch selected by `SSL_set1_dnsname` when the
  capability probe succeeds. The Windows/Android lane also showed an unused
  rollback-lambda `this` capture, a Windows-only unused `closeSyslog`, and a
  signed `jsize`/`size_t` comparison in the JNI request-length check; all three
  are corrected.
- Build graph: `Win32/Resource.rc` is compiled when `CheckLanguage` finds a
resource compiler (Windows platform modules usually enable RC already), with
  `version.h` on the include path; the ncurses fallback probes
  `CURSES_LIBRARY`; `I2PD_HAVE_SSL_SET1_DNSNAME` and
  `I2PD_HAVE_OPENSSL_MLKEM_H` have `Crypto.h` defaults and
  `build-system-static` markers; LibreSSL ML-KEM additionally requires the
  `mlkem.h` include probe; `I2PD_INSTALL_CONFIG=OFF` lets the OpenBSD port ship
  only its own examples; `WITH_HARDENING` warns instead of silently doing
  nothing on MSVC; the Android lane asserts packaged reseed certificates.
- Torrent/TUI: administrative start/stop/reannounce closures take
  `weak_from_this().lock()` instead of raw `this`; the no-destination shutdown
  path stops owned storage workers; `CreateTorrentFileContent` stores generated
  values in `std::deque` for stable views; the TUI retries a UTF-8 `LC_CTYPE`
  and undefines every mapped `KEY_Fn` macro.
- The full-equivalence program is now ordered in `docs/FEATURE-PARITY.md`
  §12 with Java 2.14.0 reference paths and acceptance evidence. Streaming
  admission (`limitAction`), proxy authentication, tunnel filters, operator
  blocklist, NetDb admission, per-tunnel administration, I2PSnark scheduling
  and process separation remain open; none is claimed equivalent without the
  manual differential harness.
- Streaming admission (workstream 1, partial): `StreamingConnectionThrottle`
  follows Java `ConnectionManager` check order and per-peer/total
  minute/hour/day limits with a fixed-window counter (constant memory), shared
  per `ClientDestination`; `ClientDestination` parses the six I2CP limits and
  tunnel configuration forwards them. It keys on the ratchet static key, not
  the Java destination hash, and `limitAction` reset/HTTP/custom remains the
  other half of the workstream. `test-streaming-throttle` covers zero limits,
  per-peer/total windows, day/hour ordering, retry values, saturation and
  cleanup.
- Java parity (PARITY-014): reseed URLs now carry Proposal 147's `netid`
  parameter built by a testable helper, and server selection follows Java
  2.14.0's two-server/100-router-info acceptance threshold instead of stopping
  at the first successful server. `test-reseed-zip-bounds` pins fresh-query,
  existing-query and already-present-netid composition.
- Parallel wave (static): SAM 3.3 `LISTEN_PROTOCOL`, protocol-aware RAW
  dispatch, `(type,protocol,port)` duplicate keys and exact receiver reset are
  implemented with `tests/test-sam-subsessions.cpp`; local HTTP Basic (407)
  and SOCKS5 RFC 1929 authentication are implemented with fail-closed
  configuration, constant-time comparison, credential stripping and
  `test-proxy-auth` (HTTP Digest remains pending reference verification);
  `i2pd_privsep` now gets
  the libi2pd capability macros; a `CheckLanguage(RC)` probe adds the Windows
  resource only when a resource compiler exists; the OpenBSD port
  lists `boost_json-mt`, `boost_url-mt` and `util`, with packaging contracts
  for those libraries and every certificate-to-PLIST entry.
  `I2PControlService::Stop()` now stops and joins the io_context before
  closing sockets and drains cancelled handlers; aborted accept/handshake
  completions are not logged as errors, a cancelled shutdown timer no longer
  stops the daemon, and a write failure no longer emits a second error body.
  The Android manual lane probes D8/R8 and asserts `classes.dex` magic and both
  certificate trees.
- Static-audit corrections: the Streaming throttle is mutex-protected and the
  `Tag` byte access in its regression uses the call operator; SAM
  `LISTEN_PROTOCOL` also applies to single RAW `SESSION CREATE`; the
  build-system capability marker escapes `${I2PD_CAPABILITY_DEFINITIONS}` and
  the success message runs after the last contract; proxy credential options
  are rejected on the command line and the request buffer is cleansed after
  authentication; the Android APK check selects universal/arm64 APKs and the
  D8/R8 probe uses the AGP-resolved R8 jar. `I2PControlService::Stop()` now
  cancels per-connection deadlines before draining cancelled handlers.
- C++ modernization continuation: the ad-hoc `Defer`/`SaveStateHelper` helpers
  are removed in favour of `modern::ScopeExit` (AddressBook and the ECIES
  ratchet); `i2p::expected` gains `value_or`/`and_then` with host tests; memory
  pools use typed nested deleters instead of `std::function`/`std::bind`,
  removing the NetDb member-pointer workaround; and the I2NP clove allowlist is
  a `constexpr` membership table. No wire/persisted format or log token changed.
- Residual, not changed: `TorrentsTunnel::InitTorrentFiles` still mutates
  session objects from the disk worker; the OpenBSD port's
  `CMAKE_INSTALL_SYSCONFDIR` still needs a real port build; Android D8/R8
  dexing/device evidence and the remaining PARITY-014 feature backlog (HTTP Digest proxy
authentication, Streaming `limitAction`, server-tunnel filters, blocklist,
NetDb admission, per-tunnel administration, selected-file promotion/bandwidth)
remain open.
- Status: Implemented / static; Pending CI compilation; Pending CI tests.
  Source/interface inspection and `git diff --check` only.

## 2026-10-25 — Memory and concurrency audit continuation

Six static audits (transports, cryptography, netDb, BitTorrent, IPC/daemon and
Android JNI) reviewed shutdown, restart, cancellation and cross-thread
handler paths. No compiler, CTest, Gradle, sanitizer, fuzzer, device, workflow
dispatch, commit or push occurred; every statement below is source inspection.

- Streaming lifetime: `Stream` now owns a `shared_ptr<StreamingDestination>`
  (keeping packet pools and the owner reachable from a pending timer) and
  `StreamingDestination::Stop()` marshals teardown to the destination's
  io_context with a bounded wait. The acceptor function is copied under
  `m_AcceptorMutex`, and `AcceptOnce` re-arms a weak-owner callback instead of
  storing a raw `this` bind.
- Destination registry: `ClientDestination` guards the default and per-port
  streaming destinations with `m_StreamingDestinationsMutex`; callers take a
  `shared_ptr` copy and invoke the destination after dropping the lock. This
  closes SAM `Create`/`Remove` versus the I2P data-message dispatch and
  `GetAllStreams`/`DeleteStream` iteration.
- SSU2 introducers: `RouterInfo::SSUExt` owns a mutex and a snapshot accessor;
  `ConnectThroughIntroducer`, HolePunch signature verification, local
  add/remove/update and `RouterContext` clears take it. A vector clear or
  resize can no longer invalidate the indices/iterators used by the transport
  thread.
- Torrent file authority: `TorrentFile` serializes `Save`, `Load`, `Open`,
  `Close`, `Complete`, path/root updates and payload/storage replacement with
  `m_Mutex`, while `m_LastAccessTime` is atomic for the inactivity sweeper.
  `GetFullFilePath()` returns a copy. This removes the double-close and wrong
  handle/read race between the disk worker and the torrent service thread.
- Proxy handshakes: SOCKS stashes pipelined bytes after a method or RFC 1929
  reply and resumes parsing only from the completed write handler, so replies
  never overlap on `m_response` or the socket. HTTP and SOCKS arm a 30-second
  handshake deadline, and `ServiceAcceptor` rejects connections above 1024
  concurrent handlers.
- Control and JNI: `I2PControlService::Stop()` detaches rather than joining
  when invoked from its own io thread and skips re-entrant `run()`;
  `I2PTunnelInfoHandler` uses the locked tunnel snapshots. The Android SAF
  `SafEnv` clears the `OutOfMemoryError` left by a failed `PushLocalFrame` and
  fails `Good()` explicitly.
- Static regression: `tests/check-control-security.cmake` now pins the
  streaming lookup/reference ownership, introducer snapshot accessor,
  `TorrentFile` locked open/close pair, deferred SOCKS input, handshake
  timers, handler cap, JNI exception clear and control self-join guard. The
  existing `ci;security;memory` static test and the manual ASan+UBSan/TSan
  jobs execute it once CI is dispatched.
- SSU2 reassembly: per-incomplete-message out-of-sequence fragments now have
  a 512-fragment and `I2NP_MAX_MESSAGE_SIZE` byte budget, and the counters are
  charged/released with each queued/consumed fragment.
  `test-ssu2-session-fragment-bounds` now exercises the over-count rejection,
  the released counters after in-order consumption, the byte ceiling and a
  concurrent `SSUExt` snapshot reader against a locked writer for TSan.
- Residual SEC-025: SSU2 pool lifetime across deferred teardown, cross-thread
  address/endpoint publication, `RouterInfo` address-slot copy-on-write,
  ratchet crypto-context serialization and raw datagram receiver captures
  remain open. `TorrentsTunnel::InitTorrentFiles`
  still performs discovery/mutation on the disk worker, although per-file
  handle operations are now serialized.
- Status: Implemented / static; Pending CI compilation; Pending CI tests.
  `git diff --check` and compile-free source checks only.

## 2026-10-25 — CMake, install and dependency modernization

Five read-only audits covered the CMake graph/install/export, dependencies and
platforms, daemon/IPC/TUI/tool/test target composition, packaging/CI/docs, and
the Android Gradle/native graph. No compiler, CMake configure/build, make,
Ninja, CTest, Gradle, package manager, linter, validator, workflow dispatch,
commit or push was performed.

- Target/dialect graph: `i2pd_apply_dialect()` is the single compile contract
  for `I2PD_CAPABILITY_DEFINITIONS`, `BOOST_ASIO_HAS_SNPRINTF` and the Windows
  SDK macros. `libi2pd` publishes it; `i2pd_privsep` and `i2pd-tui` consume it
  privately. The duplicated Windows import blocks became one PUBLIC list
  (`ws2_32 mswsock iphlpapi shell32 advapi32`); `i2pd` adds only `user32`,
  `ole32`, `uuid` and `WIN32_APP`. `wsock32`, `crypt32`, `gdi32` and the
  duplicated per-tool WSA lists were removed.
- Daemon/Android composition: `DAEMON_COMMON_SOURCES` is shared by `i2pd` and
  `i2pd_jni`; the JNI target no longer compiles `daemon/TuiServer.cpp`, and
  `Daemon.cpp` omits the AF_UNIX listener and server member on Android while
  retaining the shared request dispatcher used by `i2pd_android.cpp`.
- Dependency detection: `CheckAtomic` reports `I2PD_ATOMIC_LIBRARIES` and
  restores `CMAKE_REQUIRED_LIBRARIES`; `WITH_STATIC` rejects sanitizer/JNI
  combinations and restores the normal library suffixes after discovery;
  Boost.Container is an optional component for static Boost.JSON, mirrored in
  the installed package config; Curses cache state is cleared between the wide
  and plain layouts and `wadd_wch` is probed on `CURSES_LIBRARIES`.
- Install/export: `i2pd_privsep` installs `include/privsep/` and exposes a
  PUBLIC/build-install include interface; `i2pdConfig.cmake` requests both
  OpenSSL components, keeps the zlib 1.2.11 and Boost floors, reports
  `Libraries`/`Development` components and degrades to a normal not-found
  result when only Libraries are staged. The Android JNI install follows
  `WITH_LIBRARY`.
- Tests and presets: torrent engine tests are in a conditional list instead of
  being compiled and disabled; `tests/Makefile` forwards to the root target;
  duplicate security/memory label lists were merged; the test inventory check
  uses complete-token matching; `i2pd-version` trusts the exit status; missing
  Perl is announced and `I2PD_REQUIRE_PERL` can fail configure. Presets use
  schema 5 with `portable`, `ci-linux` and install presets.
- Android: default ABI is arm64-v8a (`-Pi2pd.abis` overrides), Gradle passes
  `-DANDROID_STL=c++_shared`, the wrapper enables `CMAKE_FIND_ROOT_PATH_MODE_*`
  BOTH, Gradle heap/worker bounds and unit-test defaults are explicit, and the
  version name/code are checked against `libi2pd/version.h`. The Android CI job
  caches Gradle, resolves the installed SDK platform, finds any APK, and
  asserts c++_shared, JNI symbols, config assets and the exact certificate
  count.
- Packaging/CI: OpenBSD port gains `pkg/DESCR` and TUI desktop/icon entries and
  drops the obsolete Boost.Atomic/Filesystem `WANTLIB`; Docker builder installs
  compiled Boost program_options/JSON/URL/Container; RPM uses
  `systemd-rpm-macros`, an unconditional Fedora Boost.JSON/URL guard and
  `man/i2pd.1`; the static spec drops the CentOS-7 payload overrides. The
  install job stages the `Service` component, a `DESTDIR` tree and a consumer
  requesting `Libraries`/`Development`; packaging gains manual static-musl and
  container-image jobs.
- Residual: no RPM/static/OpenBSD-port/container build, Gradle/APK/device,
  MSVC/MinGW link or dependency-floor lane was executed. The imported
  `privsep/` headers remain an internal, version-unstable API surface.
- Status: Implemented / static; Pending CI compilation; Pending CI tests.
  `git diff --check` and compile-free source inspection only.

## 2026-10-25 — OpenSSL 4.0 provider, X.509 and signature continuation

Five read-only audits covered EVP/providers/KDF/ciphers, ASN.1/X.509/TLS/reseed,
signatures/identity/PQ, CMake OpenSSL detection and I2P protocol capability
advertisement. No compiler, CMake configure/build, CTest, OpenSSL tool,
Gradle, sanitizer, fuzzer, provider lane, workflow dispatch, commit or push was
performed.

- HKDF: the modern path uses `EVP_KDF_fetch("HKDF")`, `EVP_KDF_CTX_new` and
  `OSSL_KDF_PARAM_*` when the probe succeeds, preserving expand-only PRK
  semantics for zero-length keys; the `EVP_PKEY_HKDF` bridge remains for
  LibreSSL/non-probed backends. The CMake HKDF probe now requires
  `OSSL_KDF_PARAM_MODE`.
- Probe integrity: `I2PD_OPENSSL_PROBE_STAMP` clears cached `I2PD_HAVE_*`
  answers when the include directory/backend/version changes. A combined
  executable link probe (full modern surface for OpenSSL, TLS/ASN.1 for
  LibreSSL) detects header/library split. Both `SSL_set1_dnsname` and
  `SSL_set1_host` are probed; configure fails when neither exists. The
  duplicated required-capability loops were merged.
- Provider capability: SipHash output is pinned to 16 bytes with
  `OSSL_MAC_PARAM_SIZE` in both the peer-ordering path and the probe; the
  probe performs a real sign. ML-KEM availability performs one
  encapsulate/decapsulate round trip before advertising a hybrid version.
  `InitCrypto` returns false for missing required algorithms (including
  Ed25519); the daemon init transaction and `api::InitI2P` fail closed.
- Signatures and wrapping: DSA verification and family verification size DER
  from `i2d_*`; RSA rejects a modulus that does not fill the fixed wire field
  and bounds the digest by PKCS#1 padding; Ed25519 checks digest/BN operations
  and zeroes the signature on failure; `PrivateKeys::Sign` rejects negative or
  null/positive-length input; `ECIESEncrypt`/`ElGamalEncrypt` return failure and
  wipe key/IV/plaintext buffers.
- X.509/TLS: reseed issuer CN extraction uses the bounded
  `X509_NAME_get_text_by_NID`; reseed/I2PControl/server-tunnel contexts set an
  explicit TLS 1.2 floor; SNI failure is diagnosed; I2PControl checks
  `SSL_CTX_check_private_key` and recreates a mismatched pair.
- LeaseSet2/I2CP: reserved EC types 1/2/3 and unsupported hybrid sets are never
  selectable; the destination preference follows the keys actually installed;
  I2CP rejects unavailable types before publishing and verifies every staged
  key. `test-leaseset2-enc-types` pins the policy and PQ selection bounds.
- Tooling: `famtool` encodes key/certificate in memory and writes the private
  key through `AtomicWritePrivate`; the certificate uses the atomic public
  writer.
- Known residual: metadata/API usage for Java type-8 Ed25519ph, blinding and
  NTCP2 `pq` list/Retry semantics remains unverified against the pinned Java
  source; low-level `EC_KEY`/`DSA` fallbacks lack capability probes; reseed and
  family still parse PEM via a throwaway SSL object; no OpenSSL 3.0.2,
  restricted-provider/PQ-denied or Linux-LibreSSL lane exists.
- Status: Implemented / static; Pending CI compilation; Pending CI tests.
  `git diff --check`, JSON/YAML/shell parsing and source inspection only.

## 2026-10-25 — Upstream openssl continuation `9725b8b2..5bcf6101`

Read-only upstream inspection confirmed that `PurpleI2P/i2pd:openssl` moved from
`9725b8b2` to `5bcf6101a456dc991e44d3d79cb69994c7bae484` (fifteen commits,
seven files, +128/-43). Four parallel reconnaissance passes covered the torrent
commits, the single crypto commit, the build/UPnP commits and the
already-present-equivalent audit, and a follow-up static review checked the
integrated diff for compile-blocking and lifetime defects.

Adapted to this architecture:

- `PeerConnection::HandleCancelMsg` implements BEP 3 message id 8 with an exact
  12-byte payload, removes only a still-queued request and leaves an in-flight
  send untouched. The BEP 6 reject reply stays gated on `m_IsFast`, unlike
  upstream's unconditional send, because the fork never synthesises fast-extension
  messages to a peer that did not negotiate them.
- `Piece::BlockReceived` now returns whether the block was newly applied;
  `HandlePieceMsg` gates completion, hashing, disk persistence, HAVE publication
  and download accounting on it. Duplicate or unsolicited blocks on a complete
  piece can no longer force a re-hash or an `InvalidatePiece`.
- `PeerConnection::TorrentComplete()` closes seed connections, resets stats,
  releases outgoing requests and sends *not interested* to remaining peers. The
  tunnel's completion closure calls it instead of branching on the remote
  bitfield. `GetNextBlockToRequest` keeps interest when the peer still
  advertises wanted pieces.
- `eTorrentErrorFilesystem` was added and `Torrent::m_Error` is now atomic,
  because `InitTorrentFiles` runs on the disk worker while status readers run on
  the service thread. A failed part-file/directory creation publishes the error
  instead of silently continuing; no fallback to core filesystem writes is
  introduced. The initial tracker announce was moved into the same disk job and
  now posts back to the service thread only when no error was published and the
  torrent is still registered, running and unremoved; the periodic, start and
  reannounce paths skip a torrent with a non-zero error as well, so the failure
  is not merely cosmetic. The disk job and its continuation capture
  `weak_from_this().lock()` instead of raw `this`. A verified piece write clears
  `eTorrentErrorFilesystem`, so a transient preparation failure does not silence
  the torrent for the process lifetime. A freshly created empty part file still
  does not mark its file `present`, preserving the previous verification
  contract.
- BEP 15's default datagram-tracker port (`DATAGRAM_TRACKER_DEFAULT_PORT`, 6969)
  is applied at the send/connect use sites. The stored tracker URL is not
  rewritten so `IsTorrentTracker`'s exact `URL::to_string()` identity keeps
  working.
- `SendPieceMsg` builds a 13-byte header in `std::array`, passes header and
  payload as a buffer pair to `i2p::stream::SendBuffer`, and hands the owning
  buffer to `Stream::Send`, removing the intermediate `std::vector` copy while
  preserving the existing completion/abort handler.
- `StopTorrent`'s disk post closes every `TorrentFile` before writing the resume
  file, so paused torrents release payload/SAF handles immediately instead of
  waiting for the inactivity sweep.
- Choke and remote-not-interested handlers reset the corresponding rate, and the
  piece/duplicate regression in `test-torrents-priorities` now pins the new
  `BlockReceived` contract.

Already equivalent, no hunk applied: `b31431ba` (DSA signing null/failure
checks, including the LibreSSL branch) and `5bcf6101` (piece invalidation on a
failed save). Omitted with reasons: `14261bb7` (GNU `Makefile.linux`; the fork
uses CMake with GCC 13/Clang 17 floors) and `b1a30e99` (removed Debian quilt
series; CMake `WITH_UPNP` already exceeds the legacy patch). Merge commits
`b77073bf`, `20e30320` and `597ef7a5` carry no independent tree change.

`docs/upstream-sync.md` gained the full disposition table, `TODO.md` gained
UPSTREAM-008, and the torrent/upstream skills record the fork divergences.
Status: Implemented / static; Pending CI compilation; Pending CI tests. Only
`git diff --check` and source inspection were run — no compiler, CTest,
sanitizer, fuzzer, live peer/tracker, workflow dispatch, commit or push.

## 2026-10-25 — 2.14.0 feature-parity audit: router, services, admin, Snark

Four read-only audits covered router/transports/netDb/reseed, client
services/proxies/tunnels, the Java console versus the native TUI/Android
surfaces, and I2PSnark, using fetched 2.14.0 raw sources at
`3c98503c5f14efe7bdd783528ba7f621f12d8081`. The resulting matrix and the
deferred list are in `docs/FEATURE-PARITY.md` §13.

Implemented by static analysis:

- **Operator blocklist** (`libi2pd/Blocklist.{h,cpp}`): bounded Java-compatible
  parser (IPv4 address/range/CIDR 3..32, IPv6 literal with `;`, 44-char base64
  router hash, `#` comments, optional reason), sorted/merged ranges, exact
  IPv6/hash sets, one immutable snapshot published atomically before
  `NetDb::Start()` loads cached RouterInfos. Hostnames are skipped (no DNS),
  IPv6 ranges/GeoIP/country blocking and reload watching are deliberately
  absent. Enforced at SSU2/NTCP2 inbound admission, outbound peer creation,
  netDb store and router/floodfill selection, and `IsRouterBanned`. Runtime
  bans are in-memory only (1024 entries, cleared by restart). Config keys
  `blocklist.enable/file/detail`; OpenBSD unveil covers `blocklist.file`.
- **NetDb lookup admission**: floodfill 14 / non-floodfill 3 replies per 120 s
  reply tunnel, and a non-floodfill drops EXPL and ANY before charging the
  throttle. `test-netdb-stop-admission` now derives the non-floodfill budget
  from the shared constant.
- **Address-book integrity**: automatic subscription merges keep any
  destination that existed before the merge and no longer re-point curated
  names (Java `Daemon.java`); conflicts are counted with bounded warnings and
  no new persisted format. Source administration was added
  (`GetSubscriptions`/`AddSubscription`/`RemoveSubscription`,
  `IsValidSubscriptionURL`, atomic `subscriptions.txt` rewrite, io_context
  re-arm with a stop token).
- **Administration**: read verbs `ADDRESSBOOK-SUBSCRIPTIONS [limit]` and
  `BLOCKLIST [limit]`, and typed writes `ADMIN ADDRESS-SUB-ADD/REMOVE` and
  `ADMIN NETDB-BAN/UNBAN` on the daemon-owned queue. TUI Destinations `s`/`x`
  edit subscriptions, NetDb `b`/`u` ban/unban a router hash, and the NetDb page
  shows blocklist counters. The Android JNI admin allowlist stays closed.
- **I2PSnark tracker parity**: whole-torrent `left` and `event=completed` only
  after every piece is verified (local Seeding/promotion remain
  selection-based), a 15-minute floor on tracker-advertised intervals raised by
  a larger `min interval`, and a 30-minute cap on the internal failure ladder.

Prepared regressions: `test-blocklist-parse`, `test-blocklist-enforce`,
`test-addressbook-merge`, extended `test-addressbook-admin`,
`test-torrents-tracker-schedule`, extended `test-torrents-priorities` and
`test-torrents-verify-parity`, `test-tui-protocol` verb coverage, and new
`control-security-static` pins.

Deferred with reasons (tracked as PARITY-017..020): server-tunnel
`filterDefinition` and the pre-accept reject seam shared with Streaming
`limitAction`; HTTP Digest; `targetForPort`/`spoofedHost`/UA rejection and
per-tunnel start/stop; go-away RouterInfo/store-cap/stale-RI netDb work; Snark
session rate limits, uploader slots and durable magnets; Android typed
bridges for the new admin verbs.

Status: Implemented / static; Pending CI compilation; Pending CI tests. No
compiler, CMake configure, CTest, sanitizer, fuzzer, Android/Gradle/device,
live peer/tracker, workflow dispatch, commit or push occurred.

## 2026-10-26 — NTCP2/SSU2 hardening continuation

Four read-only audits covered NTCP2 handshake/state/timeout/netID/PQ, the SSU2
core (handshake/ACK/retransmission/fragmentation/MTU/IPv6/PQ), SSU2
PeerTest/relay/NAT, and cross-cutting memory/concurrency. Findings were
compared with the specs and the pinned Java 2.14.0 tree and recorded in
`docs/INTEROPERABILITY.md`.

Implemented by static analysis:

- **NTCP2**: corrected SessionCreated padding limits for ML-KEM-768/1024
  (1168/1648) with a 3296-byte buffer equality check; a bounded data-phase AEAD
  failure drain (18..1041 bytes, 10 s) before termination reason 4 and a 30 s
  intra-frame deadline with reason 14; clock-skew handling that flushes
  SessionCreated and terminates with reason 7 after SessionConfirmed; msg1
  netID bans and a signed foreign-netId signal answered with reason 17,
  unreachable and a ban; the RouterInfo request-flood flag honoured under 3 KiB;
  wrong-size DateTime/Termination blocks made fatal.
- **SSU2 security**: HolePunch/Introduced block whitelist (DateTime, Address,
  RelayResponse, Padding, Termination); no external-address publication from
  msg 5 and corroborated-only msg 7 host observation; msg 4→6 Charlie endpoint
  validation; Charlie insertion-result checks, Bob capacity rejection instead of
  eviction, a bounded per-prefix PeerTest limiter and a global peer-test session
  cap; inbound handshake admission cap with reason 19; router ban/blocklist
  checks at relay/peer-test decisions.
- **SSU2 conformance/memory**: ACK 0 required before `Established()`; 120 s
  handshake skew with RTT/2; PeerTest/HolePunch `ver` 2..4 acceptance;
  single-fragment RouterInfo byte required; reachable PQ version fallback via a
  token request; `m_Address` mutex; slab-charged fragment budget with a 128 KiB
  per-message cap, 64 fragments per message and 32 incomplete messages per
  session; non-zero first-wins relay tags with Bob/Alice roles and expiry;
  `Testing` set only after signature verification; tagless/expired introducers
  rejected with session-family selection; IPv6 MTU capped at 1488.

Prepared regressions: `test-ntcp2-failstate`, `test-ntcp2-netid-signal`,
updated `test-ntcp2-handshake-bounds`, `test-ssu2-holepunch-whitelist`,
`test-ssu2-peertest-policy`, `test-ssu2-admission`, extended
`test-ssu2-control-plane` and `test-ssu2-session-fragment-bounds`, and new
`control-security-static` pins for the transport invariants.

Residual and tracked: NTCP2 inbound admission cap, 12-byte options negotiation,
establisher-thread socket I/O and the undeadlined length read (NET-172); SSU2
socket sharing, memory-pool lifetime after deferred teardown, proximity checks,
and 72 s cleanup granularity. Status: Implemented / static; Pending CI
compilation; Pending CI tests. No compiler, CTest, sanitizer, fuzzer, Java
peer, packet capture, workflow dispatch, commit or push occurred.

## 2026-10-26 — Cryptography modernization continuation

Four read-only audits covered the ECIES-X25519-AEAD-Ratchet protocol,
Elligator2/X25519 ephemeral keys, ML-KEM/provider negotiation and
signatures/identities/LeaseSet2 against the pinned Java 2.14.0 tree and the
ECIES/hybrid specs. Findings and dispositions are in
`docs/INTEROPERABILITY.md`.

Implemented by static analysis:

- **Ratchet**: New Session Reply splits are kept pending and the outbound
  tagset is promoted by the first inbound tag (Java `firstTagConsumed`), so a
  lost latest NSR no longer breaks Bob→Alice traffic; delayed NSRs cannot
  rewind an established tagset; tag exhaustion restarts the session instead of
  removing it; NSR payloads are limited to DateTime/Garlic/Options/Padding;
  `inboundTags` is clamped to 320; the anonymous one-time NS path is removed;
  derived secrets are cleansed.
- **X25519**: key generation failure is propagated end-to-end; zero private or
  public material cannot be persisted or published; the key pool and static-key
  paths retry or fail closed; `X25519Keys` is non-copyable and derives/compares
  the caller's public key.
- **Ed25519/blinding**: secret scalars and nonces use `BN_clear_free`;
  blinding returns bool and fails closed; the hand-rolled verifier rejects
  non-canonical `S` with a constant-time comparison; off-curve points are
  rejected; the two-byte b33 signature-type flag is rejected like Java.
- **RouterInfo/LeaseSet2**: RSA router identities are rejected on the netDb
  path; `i2cp.leaseSetEncType` uses a strict decimal parser with Java's
  hybrid/ElGamal combination rules; unknown LS2 key sections are retained inert;
  shipped defaults changed from `6,4,0` to `6,4`.
- **ML-KEM/capabilities**: the LibreSSL probe performs a full KEM round trip;
  hybrid Noise initialisers return bool and callers fail closed; SSU2 binds any
  Retry version change to the signed RouterInfo advertisement and fails instead
  of silently downgrading; NTCP2's 1-in-4 random classic choice was removed.

Prepared regressions: extended `test-eciesx25519-ratchet-session`,
`test-crypto-x25519-noise-elligator-mlkem-audit`, `test-eddsa`,
`test-blinding-b33-hardening`, `test-leaseset2-enc-types`, `test-ssu2-preauth`
and `test-ntcp2-handshake-bounds`, plus the new
`test-routerinfo-rsa-reject`.

Residual and tracked: no Java-generated type-5/7 NSR vectors (CRY-206..214);
`Signer::Sign` remains void and LocalLeaseSet2 is not self-verified (CRY-236);
secret containers cleanse only `size()` (CRY-237); adaptive ratchet/ElGamal
ordering and the 200 ms multi-NS gate are unchanged (CRY-238); provider-free
Ed25519 signing is variable-time; LibreSSL ML-KEM and restricted-provider lanes
are unverified.

Status: Implemented / static; Pending CI compilation; Pending CI tests. No
compiler, CTest, sanitizer, fuzzer, provider matrix, Java peer, workflow
dispatch, commit or push occurred.

## 2026-10-26 — netDb, RouterInfo and LeaseSet continuation

Four read-only audits covered the LeaseSet family (MetaLeaseSet first),
DatabaseLookup reply keys/DatabaseSearchReply, RouterInfo freshness/persistence
and netDb concurrency/floodfill against Java 2.14.0. Findings and residuals are
in `docs/INTEROPERABILITY.md`; the matrix was updated in
`docs/FEATURE-PARITY.md`.

Implemented by static analysis:

- **Garlic origin (P0)**: a new `fromTunnel` marker distinguishes a clove of a
  directly received garlic from one that crossed an I2NP tunnel. DatabaseLookup
  is allowed in the clove list and answered for direct/direct-garlic origin
  (tunnel-origin still dropped). Token-bearing DatabaseStore keeps its token
  for Direct/DirectGarlic (store + DeliveryStatus) and is stored without
  token/ack/relay for TunnelGarlic.
- **Flood/admission**: transport request-flood RIs are forwarded only by a
  floodfill and within the per-key FloodThrottler; the inbound queue is bounded
  (1024) and new RouterInfos are capped (1000/4000, updates and reseed exempt).
- **Persistence**: RouterInfo saves use `fs::AtomicWrite` (no-follow, unique
  temp, fsync file+dir); load accepts only `routerInfo-<base64>.dat` whose stem
  matches the parsed identity and removes staging/mismatched files; `Stop`
  performs a final bounded persist; replacement installs the validated
  candidate instead of mutating in place.
- **Locks/profile I/O**: selection filters and store validation use a
  non-creating profile lookup, so no disk I/O or profile creation happens under
  `m_RouterInfosMutex`/`m_FloodfillsMutex`.
- **MetaLeaseSet**: Java re-serialises a MetaLS2 with header `expires` = latest
  entry and revocation count 0 before verifying; local now rejects any other
  header or a non-zero revocation, and the test fixture is rebuilt
  Java-canonically with negative vectors.
- **Encrypted LS freshness**: a type-5 client reply is installed only when its
  signed outer published time is strictly newer; an authentic older reply keeps
  the cached snapshot.
- **Reply-key 0x12**: remains fail-closed. Java 2.14.0 parses it but has no
  sender/KDF and answers cleartext; no non-normative KDF or cleartext fallback
  was added.
- **RouterInfo freshness**: added the 54-minute `U`-cap/introducer and
  75-minute SSU-only/no-NTCP predicates.

Prepared regressions: `test-leaseset-encrypted-freshness`,
`test-netdb-clove-origin`, `test-netdb-router-admission`,
`test-netdb-persistence`, rebuilt Meta vectors, extended
`test-netdb-store-lookup-hardening`, `test-netdb-stop-admission` and
`test-tunnel-dsm-policy`.

Residual and tracked: client-side Meta resolution (I2P-016), exploratory-pool
router garlic forwarding, encrypted replies for our own RI/EXPL lookups,
probabilistic store pdrop/go-away, per-use stale-RI expiry, throttle eviction,
`m_AcceptUpdates` memory ordering, asynchronous reseed, local RI address COW
(SEC-025) and netDb pool destruction order. Status: Implemented / static;
Pending CI compilation; Pending CI tests. No compiler, CTest, sanitizer,
fuzzer, Java peer, live floodfill, workflow dispatch, commit or push occurred.

## 2026-10-26 tunnel-build / I2NP / garlic continuity (static)

Four read-only reconnaissance subagents (tunnel construction; tunnel
pools/selection/timers; I2NP framing/dispatch; garlic/ratchet plus multiprocess
integration) compared the worktree with I2P Java 2.14.0
(`3c98503c5f14efe7bdd783528ba7f621f12d8081`) and the tunnel-creation,
tunnel-alt-creation and i2np specifications. Two implementation subagents then
worked on disjoint files (tunnel/pool/config/transit versus
I2NP/garlic/session/destination); the coordinator registered the tests,
integrated the seams and repaired the residual findings below.

- **P0 build OOB** (`TU-107`): `Tunnel::Build()` indexed a 4/8-entry shuffled
  `recordIndicies[]` once per hop with an unbounded `numHops`. A
  `PlanTunnelBuildRecords()` authority now rejects an unrepresentable path
  before any write, and `ClampTunnelLength()` bounds the constructor,
  `Reconfigure()` and selection (inbound reserves the phony record).
- **P0 selector UAF** (`TU-112`): the raw custom selector pointer was used
  after the owner could be released. `TunnelPeerSelectorHandle` stores the
  pointer plus a weak owner and pins it for the whole call outside the pool
  mutex.
- **Fail-closed records** (`TU-108`): the record writers return `bool`, cleanse
  the shared secret, zero the record on provider/agreement/AEAD failure and
  abort the build; the creator requires the exact advertised reply count and
  length.
- **Transit build hardening** (`TU-109`): exact payload sizes, a bounded
  8-minute/4096-entry decaying replay filter keyed on the derived reply key
  (never logged), 25 s relay and 60 s reply expirations, and no relay/reply on
  a failed allocation.
- **Admission parity** (`TU-110`): one `IsRouterUsableForTunnel()` predicate
  for fresh selection, explicit peers and reuse; failed or test-struck paths
  are never reused; `ValidatePeers()` is direction-aware.
- **Pool accounting/tests** (`TU-111`): in-flight builds count against the
  target, consecutive failures apply a 3→2→1 ladder with a 10–240 s delay,
  tests persist with a 7.5–15 s per-path deadline and fail only on the third
  strike, recent inbound traffic skips a test, and `TestFailed` tunnels are
  never published.
- **Queue/fragments** (`TU-112`): the inbound tunnel queue is bounded
  (8192, drop counter), the endpoint/transit handlers require exactly 1028
  bytes, incomplete reassembly lives 45 s and a fragment below
  `nextFragmentNum` is dropped instead of stored.
- **I2NP dispatcher** (`I2NP-011`): the Java 90 s past / 240 s future window is
  applied before the type switch (`TunnelData` exempt), followed by a bounded
  two-generation duplicate filter keyed by message ID, expiration second and a
  router salt; cloves use their own destination-salted filter. Drops are
  counted and throttled.
- **Exact lengths** (`I2NP-012`): `CreateI2NPMessage()` returns null on
  overflow and every network caller checks it; DeliveryStatus is exactly 12
  bytes; a DataMessage must declare its whole payload.
- **Garlic continuity** (`GAR-005`, `CONC-014`): per-session state lock, a
  per-thread provider `HKDFContext` and a per-thread destination RNG; `CleanUp()` swaps maps and destroys
  outside locks; 256-entry bounded clove queue; 1..32 cloves; stale/foreign
  cloves skipped with the walk continuing; per-destination clove replay filter;
  origin-aware clove allowlist; budget-full ElGamal still decrypts; destination
  hash checked for `DESTINATION` cloves; ROUTER-mode ratchet cloves rejected;
  session map iteration no longer holds `m_SessionsMutex` across a state-locking
  session call (the ABBA inversion the new lock would otherwise create).

Coordinator corrections after the subagent reports: the failure ladder and
backoff cap were changed so the pinned regression expectations and the
documented 4/8 thresholds agree (10→240 s is reachable); `EncryptECIES()` now
checks the X25519 agreement result and cleanses on failure; an inbound config
without a router identity uses an explicitly zeroed hash instead of relying on
`Tag<>` value-initialisation; `I2NPRouterDuplicateFilter` no longer records
cloves (their per-destination filter is authoritative, so a router-wide entry
could suppress another destination's clove).

New tests: `test-tunnel-build-records`, `test-tunnel-peer-selector-lifetime`,
`test-tunnelpool-build-backoff`, `test-tunnelpool-test-timing`,
`test-tunnelpool-reuse-policy`, `test-transit-tunnel-build-replay`,
`test-tunnel-endpoint-fragments`, `test-tunnels-inbound-queue`,
`test-i2np-dispatch-policy`, `test-garlic-clove-policy`; extended
`test-tunnel-dsm-policy` and `test-tunnelpool-zero-hop`; new fuzzer
`fuzz-i2np-policy` (registered in `tests/CMakeLists.txt` and the manual Fuzzing
workflow) and extended `control-security-static` pins for the record table,
selector lifetime, replay filter, dispatch window, clove bounds and session
locks.

Status: Implemented / static; Pending CI compilation; Pending CI tests. No
compiler, CMake configure, CTest, sanitizer, fuzzer, Java peer, live tunnel,
workflow dispatch, commit or push occurred.

## 2026-10-26 SAM / I2CP / streaming / datagram continuity (static)

Four read-only reconnaissance subagents (SAM bridge, I2CP client, streaming
state machine, Datagram1/2/3) compared the worktree with I2P Java 2.14.0
(`3c98503c5f14efe7bdd783528ba7f621f12d8081`) and the SAM v3, I2CP, streaming
and datagram references. Four implementation subagents then worked on disjoint
files (SAM, streaming, datagram, I2CP/AddressBook); the coordinator added the
`Destination` encrypted-LeaseSet auth plumbing, the config keys, the test
registration, the static pins and repaired the control-plane findings below.

- **SAM receive port** (`SAM-122`): a single session used `PORT` as the I2CP
  receive port, so `RAW RECEIVED`/`DATAGRAM RECEIVED` never fired for a peer
  sending to PORT_ANY. `PORT` now only builds the local UDP forward endpoint
  and `LISTEN_PORT` (default 0) is the receive port.
- **SAM control plane** (`SAM-123`): 64-connection admission with exactly-once
  release, a 60 s HELLO deadline, and no idle disconnection after HELLO (Java
  `setSoTimeout(0)`); only a partially received command line is bounded. The
  SESSION CREATE line (private keys), the HELLO line (credentials) and datagram
  payloads are no longer logged.
- **SAM semantics** (`SAM-124`, `SAM-125`, `SAM-126`): ACCEPT and FORWARD are
  mutually exclusive per port (`ALREADY_ACCEPTING`), FORWARD is port-scoped,
  Java's error codes replace INVALID_ID/CANT_REACH_PEER/silent closes, duplicate
  parameters close, bare tokens mean true, subsessions are keyed by
  `(STYLE, 17/19/20, LISTEN_PORT)`, receivers are protocol-keyed, RAW rejects
  6/17/19/20 and a queued ACCEPT is re-armed after the armed one is torn down.
- **b33 and BlindingInfo** (`I2CP-112`, `I2CP-113`, `I2CP-114`):
  `AddressBook::GetAddress` resolves explicit `.b33.i2p` and implicit
  long-label names with canonical/length/type validation; the type-42 registry
  gained a bounded reader and the HostLookup path passes the 32-byte DH/PSK
  material to the new encrypted-LeaseSet overload; HostReply now uses Java's
  1/2/3/4/5/6 result codes.
- **I2CP control plane** (`I2CP-115`, `I2CP-116`): `i2cp.maxconnections`/
  `i2cp.maxsessions` with clean refusal and exactly-once release, cancellable
  async handshake/header/body deadlines (the blocking `available()`/`read()`
  path is gone), CLS1 key-length/signature/destination validation (DSA-only and
  fail closed), CLS2 Meta/preference/key matching, and send statuses 14/10/11
  instead of silent drops. `i2cp.maxconnections`/`i2cp.maxsessions` are declared
  in `Config.cpp`.
- **Streaming accept** (`STR-113`): `AcceptStream(timeout)` used stack
  references inside an acceptor that stayed installed after timeout; it now
  uses heap sync state and unregisters. Acceptors are explicit registrations
  with `RemoveAcceptor(token)`; `ResetAcceptor()` completes every waiter.
- **Streaming SYN policy** (`STR-114`, `STR-115`): a 128-entry/120 s
  recently-closed set drops a retransmitted SYN (no resurrection or second
  application connection) and answers with one signed RESET; over-limit,
  throttled and backlog-full SYNs are RESET instead of silent; no data is sent
  after CLOSE/RESET; the pong handler is snapshotted under the acceptor mutex;
  a duplicate SYN keeps the routing path.
- **Datagram dispatch** (`DG-111`, `DG-112`, `DG-113`): per-send version with
  `eDatagramDefault`, `(protocol, port)` receiver keying for 17/19/20 with no
  port-to-wildcard promotion, raw reserved-protocol rejection, 0xff→0
  normalisation, per-call options, mutexed (in)deflater, a thread-safe message
  pool, a session mutex, and zero-signature/offline-section rejection.

Coordinator corrections after the subagent reports: the SAM idle deadline was
replaced by a partial-command deadline (closing an idle session control socket
would break every long-lived client and diverges from Java), unparsable HELLO
version bounds now return NOVERSION instead of I2P_ERROR, `SAM.h` includes
`Destination.h` for the `PROTOCOL_TYPE_*` constants it uses in inline helpers,
`tests/test-sam-version.sh` and `test-sam-control-framing.cpp` were updated to
the Java behaviour, `SAM_CONTROL_IDLE_TIMEOUT` was renamed, and the
`RequestDestinationWithEncryptedLeaseSet` overload plus the `Config.cpp` option
declarations were added to complete the cross-agent contract.

New tests: `test-datagram-dispatch`, `test-datagram-version-dispatch`,
`test-i2cp-b33-lookup`, `test-i2cp-blinding-info`,
`test-streaming-accept-timeout`, `test-streaming-accept-race`,
`test-streaming-recently-closed`, `test-streaming-overlimit-reset`,
`test-streaming-sendstate`; extended `test-sam-control-framing`,
`test-sam-subsessions`, `test-sam-datagram-framing`,
`test-datagram-receiver-lifecycle`, `test-i2cp-session-lifecycle` and
`tests/test-sam-version.sh`; extended `control-security-static` pins for
redaction, admission/deadlines, b33/BlindingInfo, accept tokens/replay,
per-send framing and protocol-keyed receivers.

Status: Implemented / static; Pending CI compilation; Pending CI tests. No
compiler, CMake configure, CTest, sanitizer, fuzzer, SAM client, Java peer,
workflow dispatch, commit or push occurred.

## 2026-10-26 application-services continuation (static)

Four read-only reconnaissance subagents (HTTP/SOCKS proxies, client/server
tunnels, reseed/naming/address book, auxiliary services and administration)
compared the worktree with I2P Java 2.14.0
(`3c98503c5f14efe7bdd783528ba7f621f12d8081`). Six implementation subagents then
worked on disjoint files; the coordinator added the build/test registration,
configuration keys, static pins, docs and tracking.

- **HTTP/SOCKS proxies**: generated pages escape every interpolated value and
  never echo the configured outproxy URL/credentials; only `http` and
  CONNECT-`https` are proxied (403 otherwise, before resolution); proxied
  responses get a new client-response mode that strips hop-by-hop/proxy-only
  and disclosure headers; relative-form requests default to 80/443 and IPv6
  CONNECT parses; opt-in RFC 7616 Digest (MD5/SHA-256, bounded nonce cache,
  `nc` replay, stale) with constant-time compare and a failure delay; Basic
  remains default. Upstream resolve/connect/response-header deadlines close the
  client with 504. SOCKS RESOLVE is a bounded 256-entry/10-minute mutex-guarded
  cache keyed by SipHash into 255/8, and the CONNECT/UDP success replies are the
  Java loopback forms.
- **Client/server tunnels**: `I2PServiceHandler::Terminate()` is virtual and
  `ClearHandlers` snapshots/terminates outside the lock, so `Stop()` now closes
  live streams and sockets (previously `m_Dead` was poisoned and connections
  leaked until the 1 h idle timeout); `Stop(bool forced)` and restartability
  follow Java. A bounded `filterDefinition` parser/evaluator (`default`,
  `explicit`, `file`, `record`, first match wins, mtime-cached 1 MiB file,
  4096 counters) is wired to a `StreamingDestination` admission predicate that
  rejects before any stream or SYNACK, with `limitAction=drop|reset`.
  `targetForPort.<port>` (443/22 reset without a mapping), `spoofedHost.<port>`,
  `rejectInproxy`/`rejectReferer`/`rejectUserAgents`, POST 429, `enable`/
  `startOnLoad`, stable `name=`, validated `accesslist`, async target
  resolution, `weak_from_this` callbacks, default-acceptor isolation and
  `GetTunnelServices`/`StartTunnel`/`StopTunnel` complete the lifecycle.
- **Reseed**: the stale-bundle `ResetRemoteRouterInfos()` path is gone (stale
  entries are skipped and counted); reseed runs as an owned `std::jthread` with
  stop-token cancellation and per-phase/per-host deadlines; `RequestReseed()`
  is non-blocking/single-flight and `ReseedState` exposes phase/enabled/
  attempts/last error/timestamps; `reseed.disable`/`i2p.noReseed` are honoured,
  proxy credentials are redacted everywhere, and `netid` URL handling matches
  a real query parameter. Bootstrap still waits synchronously, tracked.
- **Address book**: all artifacts (index, hosts dump, identities, etags,
  `deleted.txt`, a new bounded `entry-sources.txt` sidecar) use the atomic
  no-follow writer; `AddAddressChecked` distinguishes `Exists` from replace;
  `#!action=delete` retracts only names recorded as coming from that
  subscription; destination (516–616) and host-label bounds match Java; HTTP
  304 keeps the ETag/cadence; a 32-entry/60 s negative cache avoids repeat
  lookups (b33 never cached).
- **Auxiliary services**: BOB is loopback/same-UID only, caps sessions at 16,
  bounds command lines at 4 KiB, parses ports with `from_chars` and gates
  `getkeys` behind `bob.allowgetkeys`; no handler logs operands. The
  I2PControl AF_UNIX path validates type/owner before replacement, is `0600`,
  is removed only while it is still this process's socket, and requires a
  same-UID peer where available; JSON ids/strings are escaped, `API` is
  validated, the Java error objects are emitted and bandwidth is strictly
  parsed per direction. NTP validates source/mode/leap/stratum/root
  delay/dispersion/origin and requires three agreeing servers for a large jump.
  UPnP uses a 3 h renewable lease, replaces superseded mappings and refuses a
  same-host IGD.
- **Administration**: new typed verbs (`ADMIN TUNNEL-START/TUNNEL-STOP`,
  `ADMIN ADDRESS-REPLACE`, non-blocking `ADMIN RESEED`), `tunnel.service` and
  `netdb.reseed.*` payload fields, TUI actions for services/netdb/destinations,
  and Android JNI + Material screens for tunnel control, reseed status,
  add-vs-replace, subscriptions and NetDb bans. Writes remain a closed typed
  set; no config writer and no web console.

Coordinator integration: added `I2PTunnelAccessFilter.cpp` to
`LIBI2PD_CLIENT_SOURCES`, registered the 16 new tests in `tests/CMakeLists.txt`
(plus the `security;memory` label group), declared `bob.allowgetkeys`,
`reseed.disable`, `i2p.noReseed` and the three proxy auth keys in
`Config.cpp`, extended `control-security-static` pins (proxy escaping/scheme/
sanitization, SOCKS resolve, access filter/admission, reseed state/no-reset,
address-book atomic/checked-add, I2PControl socket/JSON, UPnP lease, NTP
agreement, typed TUI verbs), fixed a `std::ifstream`/`std::ofstream` flag typo
in the address book, and documented the new keys and TUI verbs.

Status: Implemented / static; Pending CI compilation; Pending CI tests. No
compiler, CMake configure, CTest, sanitizer, fuzzer, Gradle/device, live
proxy/tunnel/reseed/IGD/NTP/BOB/Java peer, workflow dispatch, commit or push
occurred.

## 2026-10-26 privilege-separation continuation (static)

Five read-only audits (supervisor/lifecycle, storage workers, network path,
crypto/log workers, and the core's write-authority inventory) confirmed that
all worker roles are network-free but that the router core still combines
external sockets with persistent writes. Two implementation subagents and one
hardening subagent worked on disjoint files; the coordinator registered tests,
pins, config and docs.

- **Spawn-bound identity**: `SpawnWorker*` writes `[role-id | 16-byte CSPRNG
  nonce]` into an anonymous pipe before `fork()`; the child reads fd 5, closes
  it, and dispatches on the validated role before any frame or sandbox. The
  record never reaches `argv`/`environ`/logs, so a same-UID process cannot
  adopt a role with its own socketpair.
- **Data-storage worker**: protocol v3 adds `ConfigureDataStorage`/
  `DataStorageRequest`/`DataStorageReply` and `EndpointRole::DataStorage`;
  `DataStorageClient`/`RunDataStorageWorker` own the netDb RouterInfo cache,
  peer profiles and destination private keys under a validated root with
  canonical-name validation, 8 staging slots, an atomic no-follow replace and
  0600 key files. `privsep.artifacts` makes the worker the only writer; the
  core's `PersistentStorage` abstraction never falls back after an installed
  writer failure and fails closed when the worker is unavailable.
- **Core writer migration**: `NetDb` persistence, `Profiling` profile saves and
  `ClientDestination` key saves route through `WriteArtifact`; the
  `CreatePrivateFile`+`ofstream` key path is gone and the
  `i2cp.leaseSetPrivKey` value is no longer logged. `RouterContext` retains the
  last `router.info`/fixed-file buffer on failure instead of dropping it.
- **Recovery/fencing**: `RouterStorageClient` and `DataStorageClient` restart
  with bounded backoff, fence requests/replies per generation, drop stale
  replies, return `Busy` at request-id exhaustion and never replay an ambiguous
  write. The supervisor publishes a bounded `SupervisorStatus`, uses one
  documented restart budget (250 ms→30 s, 8 attempts/60 s, 60 s stable reset),
  and rejects a future torrent-broker request id as a typed `BadField` without
  tearing down the channel.
- **Capability invariant**: `privsep/Capabilities.h` declares every role's
  capabilities and forbids `ExternalNetwork`+`PersistentWrite` on target rows;
  the core's legacy row keeps the write bit so the remaining gap is explicit.
  The log worker now sanitises C1/invalid UTF-8, redacts known secret keys,
  caps the file sink at 64 MiB with one notice and no rotation, and sends from
  a channel snapshot outside the logging mutex.
- **Coordinator integration**: added `privsep/DataStorage.cpp` to
  `PRIVSEP_SOURCES`, registered `test-privsep-worker-identity`,
  `test-datastorage-{protocol,worker,client}`, `test-artifact-storage`,
  `test-privsep-capabilities` and `test-privsep-supervisor` (with the
  `i2pd_privsep` link and platform placement), declared `privsep.artifacts`,
  reconciled `MAX_ARTIFACT_SIZE` with the 16 KiB worker frame cap, and added
  `control-security-static` pins for the identity handshake, data-storage
  contract, no-fallback writer, capability matrix and log hardening.

Status: Implemented / static; Pending CI compilation; Pending CI tests. No
compiler, CMake configure, CTest, sanitizer, fuzzer, OpenBSD/Windows runtime,
TUI/Android device, live peer, workflow dispatch, commit or push occurred.

## 2026-10-26 IPC protocol consolidation (static)

Four read-only audits covered serialization/registry, the channel and recovery,
per-role authorization and the platform backends plus fuzz coverage. Their
common findings were a duplicated role policy, terminal decode failures,
implicit reply direction, a Windows receive path that closed an idle pipe, an
unbound pre-opened log descriptor and a fuzz target that reached only three
decoders.

- **Registry**: `MessageSpec` rows in `privsep/Protocol.h` now hold
  `{minVersion, direction, receivers, maxPayload, descriptorPolicy}` for every
  type. `Encode`, `Decode`, `AllowsOperation`, `DescriptorPolicyFor` and
  `DescriptorPresenceAllowed` derive from it, the hand-written role switch was
  removed and `ValidateMessageRegistry()` is a static assertion. `Pong`,
  `Error` and `TorrentStorageSupervisorReply` receivers were narrowed to the
  endpoints that actually issue requests.
- **Versioning/limits**: `PROTOCOL_VERSION = 4` adds `Cancel`; a type in a
  header older than its `minVersion` is `UnsupportedVersion`. Per-type payload
  ceilings replace the global-only check (empty `Ping`, bounded storage
  frames), so a small frame cannot force a large decode.
- **Channel enforcement**: `EnforcementPolicy::{Transport,Endpoint}`; serving
  endpoints enforce descriptor presence/kind, answer attributable frame faults
  with a typed `Error` correlated by request id, close the rejected descriptor
  and tolerate at most eight faults. `InspectFrame` only allows a reply when
  magic, version and declared length agree. Credential/ancillary failures stay
  fatal.
- **Workers**: router/torrent storage, the log worker, the supervisor and the
  broker now answer wrong-role/malformed frames with a bounded typed refusal
  instead of exiting; a duplicate `ConfigureLog` is a refusal, not a teardown.
  Storage workers accept the advisory `Cancel` (suppress a not-yet-run matching
  request) and `DataStorageClient::Write/Remove` take a `std::stop_token`
  (cancel before send does nothing; after send the worker is told, the reply is
  drained and the call reports failure without replay).
- **Windows/log**: a receive timeout returns empty and keeps the pipe;
  `WindowsAllowsOperation` delegates to the registry (keeping the
  "no descriptor channel" limitation); the Windows decoder mirrors the
  attributable-fault rule with its own budget. A pre-opened log descriptor is
  bound to the configured path by dev/inode, owner and `st_nlink == 1`.
- **Verification**: `fuzz-privsep-protocol` now reaches the frame inspector and
  every payload decoder and asserts encoder/decoder round-trip agreement;
  `test-privsep-authorization` pins registry completeness, role×direction,
  descriptor presence, per-type version/ceiling rejection and the Cancel codec.
  Registered in `tests/CMakeLists.txt`; `control-security-static` gained pins
  for the registry, endpoint enforcement, Windows timeout parity and log
  identity binding.

Status: Implemented / static; Pending CI compilation; Pending CI tests. No
compiler, CMake configure, CTest, sanitizer, fuzzer, OpenBSD/Windows runtime,
live peer, workflow dispatch, commit or push occurred.

## 2026-10-26 OpenBSD supervised-start continuation (static)

Four read-only audits (storage worker/broker, startup and credentials, IPC,
pledge/unveil) produced a ranked plan. The P0 findings were: the core startup
pledge lacked `recvfd` (pledge can only remove promises, so the runtime
`recvfd` was ignored and the first supervised handoff would abort), the
supervisor pledged `exec` with unveil disabled (an exec'ed image starts
unsandboxed), there was no credential-drop code anywhere, and `ExecutablePath`
searched `$PATH` with a `/proc/self/exe` fallback that does not exist on
OpenBSD.

- **Supervisor/broker**: both pre-resolve the executable, `unveil(exe,"x")` +
  lock, then pledge `stdio proc exec` / `stdio proc exec sendfd`. `unix`,
  path read/write and descriptor reception are gone. `ReadLogConfig` runs after
  the policy; `PeerUid()` before it. The broker pins 0/1/2 to `/dev/null`,
  charges every spawn attempt to a replenished window, stops a half-configured
  child and keeps per-request typed errors.
- **Pledge window**: one table builds the startup and runtime sets; startup
  adds `recvfd` and `id` (when a worker identity exists); runtime must be a
  strict subset and never gains `exec`/`id`/`chown`/`unveil`. The pledge file is
  a bounded subset selector with comments/blank lines rejected and dangerous
  runtime promises refused. `unveil_file` requires an absolute, non-`/` path.
- **Credentials/identity**: `privsep.user`/`privsep.group` are resolved before
  the sandbox; uid 0 is rejected; the child clears groups, drops gid/uid and
  verifies real/effective ids. The hello is `[role|nonce16|uid|gid]` written
  after the drop; `RunWorker` checks role, identity, exact length and closes
  fd5. The executable is frozen from `argv[0]`/`realpath` (no PATH, no
  `/proc`). Descriptor contract: 3/4/5, stdio `/dev/null`, >=6 closed; fd4 is
  closed for roles without a root.
- **Root core delegation**: clients require `privsep.user` for a root core and
  compare the delegated root owner against the worker uid; workers accept uid 0
  only as their spawner; `AdoptValidated` uses the worker uid. The data tree
  must be owned by the configured identity.
- **Coordinator integration**: `privsep/Credentials.cpp` added to
  `PRIVSEP_SOURCES`; `test-privsep-credentials` registered with the
  privsep link and OpenBSD placement; a shared `tests/privsep-test-executable.h`
  freezes the test binary for the seven tests that spawn themselves;
  `control-security-static` pins the credential drop, frozen executable,
  null-execpromises, pledge-window subset, supervisor unveil and root gate;
  configs/docs/TODO/ROADMAP/AGENTS and the OpenBSD/privsep skills updated.

Status: Implemented / static; Pending CI compilation; Pending CI tests. No
compiler, CMake, CTest, sanitizer, fuzzer, OpenBSD runtime, workflow dispatch,
commit or push occurred.

## 2026-10-26 Linux backend hardening (static)

Four read-only audits (storage worker, AF_UNIX/SOCK_SEQPACKET IPC, credentials
and descriptors, supervision/namespaces) produced a ranked plan. The P0
findings were a per-frame credential check against the socket creator's
`SO_PEERCRED` (which rejects every worker reply after a root core drops the
worker to `privsep.user`) and a capability teardown executed after the drop,
where `capset`/`PR_CAPBSET_DROP` return EPERM and were tolerated. The storage
seccomp filter also denied only `socket`/`socketpair`, and the network
namespace was a silent no-op.

- **Peer expectation:** `Channel::PeerExpectation` is set by `CreatePair`,
  `Adopt`, `AdoptValidated` and both spawn paths. The core requires the worker
  uid/gid and the direct child pid; a worker requires its own uid or uid 0 for
  the root delegating core; an adopted broker socket requires the worker uid
  and rejects a pre-queued record. The legacy creator comparison remains only
  for raw tests.
- **Capabilities:** the forked child clears ambient/bounding capabilities,
  drops gid/uid, then `capset(0)` and verifies with `capget`, then execs; `RunWorker`
  re-verifies `no_new_privs` and an empty set before any frame. Signal
  dispositions are reset to `SIG_DFL`.
- **Seccomp/Landlock:** `denyAll` now denies `connect/bind/listen/accept*/
  sendto/execve/execveat/fork/vfork/clone/clone3`; Landlock handles `EXECUTE`
  without granting it, `Storage` restricts reads as well as writes to the root,
  and the socket/capability lockdown is installed before the configure frame.
  A best-effort empty network namespace is created pre-exec.
- **Executable/root pairing:** the frozen image's dev/inode/size are re-checked
  immediately before `execve` on Linux; only `*-inherited` roles and the broker
  accept a root, which must be owned by the worker uid and not group/world
  writable; fd 4 is closed for every other role.
- **Coordinator integration:** `test-privsep-credentials` is now Unix-wide
  (Linux + OpenBSD) instead of OpenBSD-only; `test-privsep-linux-sandbox` gained
  exec/fork/inherited-connect/outside-read/privilege probes and the
  `ipc;security;platform` label; `test-privsep-linux-ipc` gained expectation,
  pid-mismatch and pending-record adoption cases; `control-security-static`
  pins the expectation, capability ordering, seccomp denies, Landlock EXECUTE,
  early lockdown and root pairing; docs/TODO/ROADMAP/AGENTS and the
  Linux/privsep/memory-security skills were updated.

Status: Implemented / static; Pending CI compilation; Pending CI tests. No
compiler, CMake, CTest, sanitizer, fuzzer, Linux runtime, workflow dispatch,
commit or push occurred.

## 2026-10-26 Windows multiprocess continuation (static)

Four read-only audits (Named Pipes, local RPC/ALPC, tokens/AppContainer, Job
Objects/supervision) produced a ranked plan. The P0 findings were: no Windows
worker entry point (a spawn would have started a second router), spawn identity
on `argv` with no nonce, per-operation authorization dead code, no
network/filesystem boundary (a restricted token without restricting SIDs), no
supervisor/restart owner, and a service that could report STOPPED while the
router ran.

- **Transport/auth:** message-mode Named Pipes retained. `AcceptBound`
  impersonates the client, checks user SID, PID/session, AppContainer SID and
  the `WorkerHello` nonce (protocol v5), then binds the role; `WindowsChannel`
  enforces the shared registry and typed `Error` budget. The pipe DACL includes
  the exact container SID; the receive path delivers a completed-during-cancel
  record instead of dropping it.
- **Isolation:** `WindowsIsolation.cpp` role table (AppContainer/LPAC for
  storage/log/crypto/admin, network capability set, restricted-token
  supervisor), profile/SID/capability creation, delegated-root grant, and a
  fail-closed spawn gate. No role can have both `internetClient` and a write
  root.
- **Spawn/supervision:** suspended create with NUL std handles, empty handle
  list, Job-List attribute, security capabilities + LPAC opt-out, Job/mitigation
  limits, `TerminateJobObject`; `WindowsWorkerSupervisor` with generation
  fencing, restart ladder, status publication and bounded stop; service stop
  drains `Daemon.stop()` with non-zero wait hints.
- **Worker entry:** `--privsep-worker` connects bound, sends the nonce hello,
  binds the role and never initialises the router; data frames without a role
  backend are refused, not routed.
- **Coordinator integration:** `WindowsSupervisor.cpp` added to
  `PRIVSEP_SOURCES`; `test-privsep-windows-isolation` and
  `test-privsep-windows-supervisor` registered with the `i2pd_privsep` link,
  Windows placement and `ipc;security;platform` labels; `WorkerHello` added to
  the authorization test exception; `WindowsIsolation` factory cleaned up to a
  friend constructor (no thread-local handoff); docs/TODO/ROADMAP/AGENTS and
  the Windows/privsep skills updated.

Status: Implemented / static; Pending CI compilation; Pending CI tests. No
Windows compiler, CTest, service run, AppContainer execution, cross-user test,
workflow dispatch, commit or push occurred.

## 2026-10-26 Android multiprocess and lifecycle continuation (static)

Four read-only audits (services/lifecycle, Binder/AIDL, JNI, SAF/storage)
produced a ranked plan. The P1 findings were: a graceful stop kept its deadline
only in memory, so process death during the drain resurrected the router; the
SAF broker trusted same-UID and exposed an unscoped `openExport` truncate plus a
generationless `retire()`; `NewString(nullptr,0)` was reachable for every empty
result; `stopDaemon` could not cancel a start behind the Java monitor; and the
storage-broker process could start a second router.

- **Run intent:** `RouterRunState` (phase/deadline/generation, legacy
  `should_run` mirror) is persisted in `router_state`; `onCreate`/`Restore`
  stop an expired drain, re-arm a live drain after `startedOkay`, clamp corrupt
  deadlines and publish `restoreDeferred` on failure. `onTaskRemoved` keeps the
  daemon; `onDestroy` purges all main-handler callbacks; the notification
  channel is verified in `ensureForeground()`.
- **SAF broker v2:** `handshake`/`apiVersion`, `prepareExport`/`openExport(token)`,
  `retire(generation)`, owner `(uid,pid)` binding, owner-scoped
  cancel/close/revoke, typed admission, 15 s cancel / 30 s process fence,
  `deleteByRelative`/`removeEmptyDirectory`; client handshake/version check,
  bounded retirement recovery and typed busy/timeout/unavailable outcomes.
- **JNI:** one-shot process role with fail-closed `startDaemon`/SAF bridge and
  `StorageBroker` claim in `restrictSafStorageProcess`; safe empty-string
  `NewString`; exception clearing and `ScopedLocalRef`; native stop token
  published before the monitor with `start-canceled`; SAF payloads detached on
  stop and no JNI in bridge/payload destructors.
- **Topology:** storage remains `:torrent_storage` (same-UID containment);
  networking cannot be a separate Android permission domain; the router remains
  in the main process with an Activity-independent foreground service until the
  `IRouterControl` AIDL split (`ANDROID-026`).
- **Coordinator integration:** fixed `TorrentSafCancellationWireTest` for
  `retire(long)`; `RouterRunStateTest`/`TorrentSafProtocolTest`/instrumentation
  contract test added; `test-android-daemon-lifecycle` extended; static pins in
  `control-security-static` (AIDL v2 members, no descriptor export, `(uid,pid)`
  caller binding, two-stage watchdog, persisted run-state keys,
  `onTaskRemoved`/callback purge and `stopWithTask="false"`); docs/TODO/ROADMAP/
  AGENTS and the Android/privsep skills updated. The compile-free
  `build-system-static` lane also exposed a pre-existing split
  `Expected.h lives in libi2pd` marker in `build/CMakeLists.txt`; the comment
  was rewrapped so the pinned phrase is contiguous again.

Status: Implemented / static; Pending CI compilation; Pending CI tests. No
Gradle/NDK, javac, AIDL, CTest, adb, device/emulator, lint, workflow dispatch,
commit or push occurred.

## 2026-10-26 I2PSnark scheduler, tracker and privacy continuation (static)

Four read-only audits (engine download/seeding, creation/magnets/metadata,
trackers/DHT/PEX, piece/resume/persistence/IPC) produced ranked gaps against the
documented I2PSnark reference. The implemented correction slices are:

- **Engine:** outbound requests expire after 60 s with an epoch-safe release
  before the 95 s blanket keep-alive release; peer/torrent counters use the
  actual block length; idle peer rates decay instead of keeping the last burst;
  per-file progress requires complete+verified+not-saving; a failed payload
  write publishes `eTorrentErrorFilesystem` and is cleared only by a later
  verified write; hash mismatches charge a bounded per-torrent peer table and
  disconnect at three strikes (best-effort, in-memory only).
- **Trackers:** the periodic dispatch now preserves the failure ladder, HTTP
  announces are single-flight per `(torrent, slot)` with a 16-request cap and a
  shared 8 s write/response deadline (weak owner, cancel/stop aware), and
  registry slots are reference counted and reused without renumbering; common
  slots are never recycled.
- **DHT privacy:** `MarkPrivate`/`Forget` purge served peers, our rendezvous
  identity, issued tokens and outstanding get_peers queries; a bounded
  private-hash set refuses later inbound get_peers/announce_peer, and a removed
  private hash stays unservable.
- **Control:** `TORRENT-REMOVE-ALL <tunnel> [files]` (TUI `Ctrl-X`/`Ctrl-D` with
  confirmation, Android session removal), restored `TORRENT-COMMENT-UNHIDE`
  parsing, Android single-torrent start/stop and a creation-scoped tracker
  validator; the Android JNI allowlist now admits start/stop/reannounce, their
  bulk variants and remove-all.

Prepared tests: `test-torrents-scheduler`, `test-torrents-tracker-dispatch`
(new, registered in the torrent list) and the extended `test-tui-protocol`;
`SNARK-012` records durable magnets, BEP 12 tiers, endgame/CANCEL,
unchoke/upload-slot policy, bandwidth limits and per-file recheck as open.

Status: Implemented / static; Pending CI compilation; Pending CI tests. No
compiler, CMake configure/build, CTest, sanitizer, fuzzer, Gradle, adb, lint,
workflow dispatch, commit or push occurred.

## 2026-10-26 BitTorrent DHT/PEX hardening continuation (static)

Three read-only audits covered KRPC/routing/lookup/announce, PEX/BEP 10/DHT
hints/peer discovery and the test/fuzz/differential surface. The implemented
slices stay I2P-only and change no peer-wire layout or persisted format:

- **DHT lifecycle:** repliable datagrams are copied into a bounded 64-entry
  queue and parsed on the tunnel service thread; raw datagrams are marshalled
  from the network thread as well. A periodic 15-minute save runs on the disk
  domain (single-flight, delegated mode never falls back), the port is clamped
  to 1..65534 so `rport` cannot wrap, and the destructor/Stop cancel every
  timer and clear queued frames.
- **Tokens/routing/abuse:** tokens use a bounded FIFO store with a 16-per-node
  quota and O(1) eviction; the routing table caps a destination at two node
  IDs; raw sender-less frames get a global 1024/60 s budget and malformed
  frames are never answered; errored nodes are marked failed before advancing a
  lookup; a reply carrying both `values` and `nodes` consumes both.
- **KRPC:** a side-effect-free `DecodeDHTFrame` rejects duplicate/unknown keys,
  truncated/mixed envelopes and oversized transactions, tolerates BEP 5 `v`,
  and is shared by both receive paths; compact node/peer and state-record
  decoders are bounded and fuzz-ready.
- **PEX/BEP 10:** repeated handshakes are rejected instead of re-triggering
  PEX/DHT/metadata, `reqq` is clamped as signed, `v` is capped, a valid private
  torrent omits `metadata_size`, and PEX/DHT advertisement is interval-gated.
  Seed classification now requires an actually received non-empty remote
  bitfield. An `i2p_dht` hint requires an established authenticated identity
  and tolerates an unrelated `rport`; `added` decoding is one pure bounded
  helper.
- **Discovery:** PEX/DHT candidates enter a 256-entry per-torrent table with
  dedupe, PEX 30 min / DHT 3 h TTL, 30 s→900 s backoff, four failures and
  100/60 s per-origin admission; the tunnel drains due candidates from the
  status and reconnect ticks and records attempt/success/failure. The table is
  purged on stop/remove/private transition. Tracker dialing remains unbudgeted.
- **Tests/fuzzing:** new `test-torrents-dht-protocol` and `test-torrents-pex`,
  new `fuzz-torrents-krpc` and `fuzz-torrents-extensions`; both fuzzers are in
  the `fuzz.yml` target lists and both tests are in the focused torrent CTest
  selection. The per-peer routing cap required distinct destinations in the
  existing DHT interop and security fixtures.

Status: Implemented / static; Pending CI compilation; Pending CI tests. No
compiler, CMake configure/build, CTest, fuzzer, sanitizer, Gradle, adb, lint,
workflow dispatch, commit or push occurred. BEP 5 announce `port`/
`implied_port`, BEP 12 tiers, Java differential exchanges and live churn remain
open; no public DHT/DNS/clearnet fallback exists.

## 2026-10-26 I2PSnark comments/ratings continuation (static)

Three read-only audits (Java protocol/codec, store/moderation/persistence and
TUI/Android surfaces) produced the following static corrections; no wire
format was invented.

- **Wire/codec:** a `filter` hint of 0..64 bytes is accepted and ignored; TAB
  is normalized to a space like CR/LF while other C0/DEL/C1 remain rejected.
  Dedupe uses a normalized owner/text key (trim/collapse ASCII whitespace,
  drop U+200B/200C/200D/FEFF) without rewriting the stored text; ratings remain
  immutable for the same normalized pair. Anonymous entries get a separate
  16-entry visible quota.
- **Admission/publish:** a connection may admit 40 new entries/60 s and a
  torrent 80/min, independent of reconnects; duplicates are not charged. A
  local add/hide changes the store revision and may refresh after a 10 s floor;
  the 600 s periodic cadence remains. Hide/trim content changes bump the
  revision so a coalesced state write is scheduled.
- **Persistence:** a future comment timestamp is clamped to the local clock
  instead of rejecting the entire `.state` (which previously lost priorities
  and the stopped flag); structural errors and duplicate IDs still reject
  atomically.
- **Visibility/moderation:** `TORRENT-DETAIL <tunnel> <id> [offset] [limit]`
  pages the retained store (0..255 / 1..16) with
  `comments.offset/count/total/visible/hidden/local/truncated`; the default is
  the newest 16 records. TUI adds `[`/`]` paging, `TimestampAged`, local/hidden
  previews, a persistence-error line, bidi control stripping and confirmed
  hide/unhide. Android pages with offset/count and age/local markers, shows the
  truncation state and reports a busy refusal instead of dropping the action;
  four new strings ship in all catalogs.
- **Tests:** extended `test-torrents-comments` (filter/TAB/control, normalized
  dedupe, anonymous cap, revision matrix, admission windows, counts),
  `test-torrents-state` (future clamp, rejection cases) and
  `test-tui-protocol` (detail paging parse and `SliceCommentPage`), plus
  `TorrentControlTest` paging bounds.

Status: Implemented / static; Pending CI compilation; Pending CI tests. No
compiler, CMake, CTest, fuzzer, sanitizer, Gradle, adb, lint, device or
workflow run occurred. Java `CommentSet` dedupe/rating parity, TUI catalog
translations (`I18N-004`) and device accessibility remain open; codec tests
are not an interoperability claim.

## 2026-10-26 Android SAF payload completion continuation (static)

Three read-only audits covered the C++ payload abstraction/piece flow, the
Linux/OpenBSD storage worker IPC and the Android SAF/JNI/ContentResolver
backend. The implemented slices keep every file operation behind the existing
authorized backend and never add a POSIX path fallback.

- **Existence and resume:** `TorrentFile::ProbePayload` opens the exact final
  name through the payload backend, closes it and reports absence without
  logging (a new torrent's documents legitimately do not exist yet). The SAF
  startup scan now marks only present exact-size documents; the bounded
  verification chain hashes pieces fully backed by existing files and leaves
  pieces spanning a deleted or revoked document downloadable. Zero-length
  documents are still never opened.
- **POSIX write recovery:** a final-name file opened read-only for seeding is
  closed and reopened writable when a later hash failure forces a re-download,
  instead of permanently failing writes through a read-only descriptor.
- **Broker ordering/recovery:** the SAF watchdog (15 s cancel, 30 s fence) is
  armed only after the operation owns `ioLock`, so a queued operation is not
  cancelled or fenced for a stalled sibling; admission and owner checks are
  unchanged. The client bind budget clears its exhausted latch on success and
  restores attempts only after a 30 s stable binding; `initialize`, selection
  and status refresh provide a bounded explicit re-arm.
- **Fixtures/tests:** the instrumentation provider now exposes a nested
  multi-file tree (including a zero-length document) and modes for deleted,
  revoked, same-length-replaced, no-seek/socket, read-only write failure and
  broker retire/rebind. Instrumentation tests cover nested exact-range IO,
  short final block, deletion, replacement, no-seek rejection without staging,
  revocation and rebind; JVM tests pin bind-budget and watchdog-ordering
  predicates plus path/size policy.
- **Documentation:** `docs/torrent/saf-storage.md` records the no-staging
  no-seek contract, startup probing, watchdog ordering, bind recovery and the
  prepared-but-unexecuted fixtures.

Status: Implemented / static; Pending CI compilation; Pending CI tests. No
compiler, CMake, CTest, Gradle, AIDL, adb, lint, device or workflow run
occurred. Device death/revocation/ENOSPC tests and a resume bitfield trusted by
per-document identity remain open; the current design still rehashes every
present document before publishing.

## 2026-10-26 native TUI continuation (static)

Three read-only audits covered TUI panels/navigation/Unicode, router/admin
parity and the Windows console frontend. The implemented slices keep the daemon
independent of any terminal and do not reintroduce a web console.

- **Panels/navigation:** Status now scrolls its fixed fields (Up/Down/PgUp/PgDn,
  Home/End) and keeps the bandwidth history on the first page; it also shows
  the protocol version and the log-drop counter. Every cached panel marks
  itself `[stale]` after disconnect instead of presenting rows as live, and
  empty panels distinguish "waiting for the first snapshot" from a loaded
  empty result. `q` inside a torrent detail returns to the table (Esc does the
  same); only table-mode `q`/`Q` exits. Footer/help text now names the actual
  keys, including Destinations, torrent detail/back, NetDb reseed/ban and
  Logs reopen.
- **Async/errors:** Destinations rotates one address-book auxiliary request per
  6 s instead of issuing up to four blocking requests in one tick; IPC remains
  synchronous with bounded deadlines. Unix and Windows TUI server sessions are
  recycled after 30 minutes instead of 5, reducing forced reconnects. A
  disconnected client keeps snapshots but marks them stale.
- **Unicode:** `SanitizeText` now strictly decodes UTF-8, replaces invalid
  bytes/C1 and C0/DEL with `?`, and strips bidi controls for every untrusted
  field; comments reuse the same path. `DecodeUtf8` rejects overlong, surrogate
  and out-of-range sequences, so ncursesw never receives a raw invalid byte.
- **Router operations:** typed `ADMIN ONLINE ON|OFF` (Transports::SetOnline,
  applied state returned) and `ADMIN LOG-REOPEN`; Status `o` confirms the
  online/offline transition and Logs `O` reopens the daemon log. `log.dropped`
  and `protocol` are exposed in STATE. The stray `std::cout` during bandwidth
  parsing was removed.
- **Windows:** a console control handler stops the UI on Ctrl-C/Break, close,
  logoff and shutdown through the normal restore path; `VK_CANCEL` maps to the
  Ctrl-C path. The pipe is documented as byte-mode.

Prepared/updated tests: `test-tui-protocol` covers the new admin verbs;
`check-control-security` pins the TUI Unicode/staleness/console and 30-minute
server contracts, plus the no-stdout daemon rule. No compiler, CTest, terminal,
Windows runner or workflow was executed.

Status: Implemented / static; Pending CI compilation; Pending CI tests. Open:
NetDb/Destinations row selection and search, Config filtering/writing,
Services secondary-table scrolling, a Windows product IPC test, terminal
locale/ASCII fallback, and TUI catalog translations (I18N-004).

## 2026-10-26 Android Java 25/ART/JNI modernization continuation (static)

Four read-only audits covered Gradle/AGP/D8/R8, Java source modernization, the
JNI/NDK boundary and ART/API 29 limits. The implemented slices preserve Java 25
as a real source/class-file target and do not lower it.

- **Build/D8:** the probe now dexes every nested record/sealed class, requires
  DEX 039 at `--min-api 29`, and keeps the DEX 037 transcript only as
  provenance. `check-config.sh` rejects Java 8/11/17/21/23/24, pins the
  built-in-Kotlin workaround, checks minSdk/probe parity and denies API-30+/33+
  collections/String/stream/virtual-thread symbols in app/instrumentation
  source. The Android job asserts packaged DEX 039 and the absence of
  `java/lang/Record`, `ObjectMethods`, `SwitchBootstraps` and `MatchException`.
- **R8/JNI:** keep rules now cover the JNI-looked-up `TorrentSafStorage` class
  and SAF broker/AIDL names; a new Perl checker verifies Java native
  declarations against the checked-in header and C++ definitions (43 symbols).
- **JNI ownership:** `noexcept` matches header/definitions; UTF-16 buffers use
  an RAII owner; `setDataDir`/`setRuntimeLogLevel` bound input before conversion;
  failed `NewByteArray` clears OOM; the SAF bridge reuses one global class ref
  instead of leaking per restart; `SafEnv.failed` is checked; the dead native
  restart bypass was removed; docs no longer claim no global refs.
- **Java 25:** `RuntimeAdminPolicy` returns a `ReseedStatus` record with an
  explicit `UNKNOWN`; `DaemonWrapper.State.isStartedOkay` is exhaustive;
  `LogLevel` spacing fixed; the docs no longer claim streams/collectors are in
  use. The sealed `RouterCommand` exhaustive dispatch remains the reference.

Status: Implemented / static; Pending CI compilation; Pending CI tests. No
Gradle, javac, D8/R8, Lint, adb, device, workflow dispatch, commit or push
occurred. Real AGP-selected D8/R8, Lint class-file-69 handling, platform-37
resolution, API-29 execution and R8-minified behaviour remain manual evidence.

## 2026-10-26 Native GUI Material 3 continuation (static)

- Four read-only audits covered screens/M3/tablet, state ownership and
  Activity independence, accessibility/notifications/i18n and the
  JNI/SAF/torrent UI path.
- **Rows/tablet:** `UiFactory` centralizes 48dp targets, logical padding, a
  centred `MaxWidthLinearLayout` column and one-utterance semantics; the three
  programmatic screens use it. `content_max_width` is 1000dp at `w600dp`.
- **I2PSnark:** new `TorrentRow` record decoder (bounded fields, rejected id,
  dropped bad hash/magnet, clamped counters) with a titled actions dialog and
  combined TalkBack label; `TorrentRowTest` covers it on the JVM.
- **Accessibility:** address rows announce name/destination/blinded
  (`address_book_blinded` translated into es/ru/zh-CN), the graceful countdown
  is no longer a live region and the notification action follows the drain
  state (`setOnlyAlertOnce`).
- **SAF threading:** select/detach now run on `RouterService`'s SAF worker with
  typed main-thread results; the picker callback no longer performs a
  `commit()` or provider round trip. Fragment generation checks drop late
  results.
- **State/navigation:** torrent session, import tunnel and SAF identity restore
  from instance state; the session picker no longer reopens on rotation;
  secondary entries hide while a detail is open and every fragment tag is
  hidden on destination change.
- **CI:** an opt-in `android_ui` input prepares an x86_64 API 29 emulator lane
  running `connectedDebugAndroidTest`.

Status: Implemented / static; Pending CI compilation; Pending CI tests. No
Gradle, D8/R8, Lint, adb, emulator, device, workflow dispatch, commit or push
occurred. The UI lane is Prepared / not executed.

## 2026-10-26 Complete native and Android internationalization (static)

Four read-only audits covered TUI strings, CLI/daemon output, Android resources
and the quality of the existing translations.

- **TUI:** status/NetDb/config/destinations/services/logs/torrents/detail and
  footer text now comes from the catalogs; `TorrentStatusText` localizes wire
  status tokens only; `BuildDetailGeneral` uses display columns; prompts and
  status messages translate at their boundary; `IsAffirmative` accepts the
  advertised s/sí/д/да tokens.
- **Catalog:** 414 keys with identical es/ru/zh key sets; multi-line HTTP
  address-helper keys were rebuilt as full arguments; 10 dead fragment keys
  removed; 14 plural groups added (es 2, ru 3, zh 1 forms).
- **Checker:** new `i18n/check-i18n-catalogs.pl` validates parity, placeholders,
  trailing spaces, UTF-8, plural arity, identifier exclusion and source-literal
  coverage; `tests/check-i18n.cmake` runs it and pins the boundary helpers.
- **Tools:** `autoconf_i2pd` drops its private ru/en map, offers en/es/ru/zh,
  uses the shared catalogs and reads a token for multibyte yes/no; the language
  registry resolves the environment on first use for standalone tools.
- **Android:** three count strings became `<plurals>` in all locales; typed
  `UserMessageException` replaces hardcoded creation/validation errors; log row,
  notification channel and tracker terminology are translated; the guard now
  checks plural arity/placeholders and test references; `LocaleResourceTest`
  asserts zh-TW fallback and Russian one/few/many.

Status: Implemented / static; Pending CI compilation; Pending CI tests. No
compiler, CTest, terminal, Gradle/AAPT2, device, workflow dispatch, commit or
push occurred. Help bodies and `--help` prose remain English (`I18N-007`).

## 2026-10-26 i2pd-tools integration continuation (static)

Four read-only audits covered CMake/inventory, duplicated core helpers,
upstream provenance and CLI/error contracts.

- **Key I/O:** new `tools/common/keys.hpp` with exact-consumption loading,
  `SecureBuffer`, `fs::AtomicWritePrivate` storage and anonymous same-file
  refusal; all key-writing tools migrated. `autoconf_i2pd` is atomic and
  NUL-safe.
- **Parsers/CLI:** shared `ParseBounded`; `keygen` help/mode contract;
  `keyinfo -b` returns 3 on a rejected destination; `b33address` caps input and
  rejects NUL/control; base64 padding terminates across chunks; `regaddr*`
  validate names; `regaddr_3ld` emits one LF-only record; `famtool` validates
  the family alphabet/mode and never lets a throwing `RouterInfo` escape.
- **Core reuse:** five `CryptoGuard` clones removed, `x25519` uses
  `i2p::crypto::X25519Keys`.
- **CMake/tests:** no `libi2pdclient`, `libi2pdlang` only for `autoconf_i2pd`,
  Windows-only getopt source, aggregate install, cross-compile guard, new
  `tools-getopt`/`tools-cli`, compile-free `tools-inventory-static` across
  CMake/docs/Debian/RPM/CI.
- **Provenance/docs:** `scripts/i2pdctl` removed from the upstream manifest and
  documented as a Perl rewrite; `keys.hpp`/`check-doc-nav.pl`/the RouterInfo
  fixture added to the fork-only list; licence anomalies recorded; stale
  linkage/famtool/GNU-make claims corrected.

Status: Implemented / static; Pending CI compilation; Pending CI tests. No
compiler, CMake configure, CTest, package, workflow dispatch, commit or push
occurred. MSVC tools, man pages and RPM component install remain open.

## 2026-10-26 Python-free guard and Perl quality continuation (static)

The audit confirmed zero project-maintained Python sources, shebangs or build
consumers, and found the remaining migration gaps were in the guards and
registration:

- `perl-portability.t` failed statically: `android/tools/check-android-jni-symbols.pl`
  was discovered but not listed, `i18n/check-i18n-catalogs.pl` was never
  scanned, and `tools/scripts/check-doc-nav.pl` was not executable. The
  inventory, `i18n/*.pl` glob, executable rule, module allowlist and consumer
  list (`.gitignore`, `.dockerignore`, `tests/check-i18n.cmake`,
  `tests/check-tools-inventory.cmake`) are corrected; stale PyDev/Sphinx
  ignore entries are removed.
- `check-android-jni-symbols.pl` and `check-i18n-catalogs.pl` are registered for
  `perl -c`, Perl::Critic and the workflow shell lane; the JNI checker has a
  CTest entry.
- New `tools/tests/maintenance-checkers.t` creates temporary docs/nav/JNI
  trees and asserts each checker rejects a broken input and accepts the repair.
- Every maintained Perl script now declares `use 5.016`; `check-android-locales.pl`
  uses `File::Spec` and writes failures to stderr; the doc-link checker no
  longer reports the exact repository root as an escape; the vector harness
  input is containment-checked and its output capped; the JNI checker ignores
  commented-out declarations.
- `i18n-static` no longer makes Perl mandatory; a manual `perl_quality` dispatch
  input installs the distribution Perl::Critic package, runs `prove` on the
  four Test::More suites and the critic profile. No CPAN or runtime download.

Status: Implemented / static; Pending CI tests. No Perl test runner, `prove`,
`perlcritic`, compiler, workflow dispatch, commit or push ran.

## 2026-10-26 Firewall generator hardening (static)

Four read-only audits covered iptables/ip6tables, nftables, Windows Defender
and OpenBSD PF.

- **Common model:** endpoint revalidation moved into the optional-returning
  renderers; special-purpose IPv4/IPv6 ranges rejected; documentation ranges
  kept for the signed fixture.
- **nftables:** keyword/leading-`_`/hook-name rejection, fail-closed family
  selection, documented `inet` version dependency, base-chain and rerun
  semantics.
- **Windows:** `~` preserved in `-Name`, CR/LF stripped by the escaper, exact
  line pinned, idempotence and Block precedence documented.
- **PF:** label length bounded to 64 bytes including NUL; `quick`/`lo0`/default
  pass/`flags S/SA` semantics documented.
- **CLI:** usage errors to stderr, `--help` on stdout, buffered all-or-nothing
  output, budget after family selection, no-selection diagnostic.
- **Tests:** boundary/injection/family/tag/label unit cases; positive
  nftables/Windows/PF smoke output; usage-stream and mixed valid/malformed
  cases in `tools-cli`.

Status: Implemented / static; Pending CI compilation; Pending CI tests. No
compiler, CTest, firewall command or workflow ran.

## 2026-10-26 Documentation and identity consolidation (static)

Four read-only audits covered docs/Markdown, branding identity, technical
guides and legal/metadata policy.

- **IPC/C++23/torrent/Android/OpenBSD:** protocol v5, registry ceilings,
  WorkerHello, TUI timeouts and full torrent verbs; C++23 probes/expected and
  jthread remainder; JNI page marked historical with the 43-symbol checker as
  authority; OpenBSD `pledge_file` subset semantics.
- **Legal:** new `NOTICE`; restored Windows `LegalCopyright`; SPDX lines;
  corrected BSD-3/CC-BY-SA scope and historical validation claims; install
  parity in CMake/Debian/RPM; `check-legal-metadata.cmake`.
- **Language:** `docs/i18n.md` canonical four-language policy; README/docs/
  configuration links; i18n pinning; removed web-console wording.
- **Branding/packaging:** contrast table completeness, provenance wording,
  legacy `open-i2pd` note, RPM desktop/icons; Debian docs install reconciled.
- **Structure/links:** heading fixes, See-also sections, upstream-pin checker.

Status: Implemented / static; Pending CI tests. No MkDocs, link/branding
checker, package build, device run, workflow dispatch, commit or push occurred.

## 2026-10-26 Service-definition consolidation (static)

Four read-only audits covered the daemon CLI/signals/worker lifecycle, the
systemd/SysV/OpenRC definitions, runit/s6/s6-rc/dinit/Upstart, and
Shepherd/OpenBSD/packaging.

- **Correctness:** `SIGABRT` no longer becomes a clean exit; unhandled
  exceptions exit 1 without printing operands. systemd gains `LogsDirectory`
  and documents main-process-only stop; `SyslogIdentifier=open-i2pd` is frozen.
- **Managers:** OpenRC retry/ulimit/checkpath/required paths; runit `finish`,
  `log/config` and `chpst -o`, with no unsupported `down-signal`; s6/s6-rc
  `softlimit`, `finish`, `timeout-finish` and identical argv; dinit/Upstart
  sample corrections; Shepherd 660 s and its process-group caveat.
- **SysV:** real `force-reload`, identity-checked PID, verified stop, safe
  `DAEMON_OPTS` splitting, `Should-Start: $syslog`, LSB fallbacks and
  normalized enablement.
- **OpenBSD:** `rc_stop_signal=INT`/`daemon_timeout=660`, `daemon_execdir`,
  non-readiness wording, mirrored `@sample` trees, private `/var/i2pd`,
  root-owned config and licence/NOTICE installation.
- **Contracts/CI:** expanded `check-services` (exact signal/timeout files,
  argv parity, docs matrix), `check-build-system` and `check-packaging`;
  `service-install-matrix` installs each `Service` component without building;
  `build.yml` runs `check-services`; `docs/service-management.md` gains a
  signal/exit-code contract and manager matrix.

Status: Implemented / static; Pending CI compilation; Pending CI tests. No
service manager, package, daemon, compiler, CTest, VM/device run, workflow
dispatch, commit or push occurred.

## 2026-10-26 Packaging consolidation (static)

Four read-only audits covered Debian packaging, the CMake install graph,
OpenBSD port/rc.d/LibreSSL, and packaging/autopkgtest CI.

- **CMake:** `Documentation` now installs `ChangeLog`, `LICENSE`, `NOTICE`,
  `README.md`, `LICENSE.i2pd-tools`, `CC-BY-SA-4.0`, filtered docs and
  branding; man pages moved to `Runtime`/`Tui`/`Tools`; a service manager with
  `I2PD_INSTALL_CONFIG=OFF` is rejected.
- **Debian:** manifests use the real `debian/tmp` docs/man/certificate paths;
  `rules` pins config/OpenSSL expectations, parallel jobs and `dh_apparmor`;
  `control` adds AppArmor/sysusers addons and `i2pd-tui`
  `Breaks`/`Replaces`; `preinst` migrates a certificate directory; `postinst`
  repairs only root-owned empty paths; `postinst`/`postrm` keep purge-only
  removal.
- **AppArmor/autopkgtest:** profile allows worker re-exec, sockets and logs;
  smoke asserts DEP-5, docs/man ownership, profile parsing, desktop entry,
  tmpfiles modes and package split.
- **OpenBSD:** separate `obj` build tree, LibreSSL expectation, CMake man
  pages, subscriptions sample, login-class `@newuser`, `pexp`, root-owned
  config and the documented `doas -u _i2pd` TUI path.
- **RPM/static/container:** renamed tools licences/READMEs to avoid `%doc`
  collisions, added desktop/icons to `%files`, shipped CC-BY-SA/NOTICE/docs,
  removed the static config/tunnel links, and made the container certificate
  link idempotent.

Status: Implemented / static; Pending CI tests. No `dpkg-buildpackage`,
`dh_missing`, lintian, autopkgtest, RPM/static/container build, OpenBSD
`pkg_create`/`pkg_add`, workflow dispatch, commit or push occurred.

## 2026-10-26 Test integration and manual CI (static)

Four read-only audits covered CTest registration, workflow/matrix selection,
protocol/crypto coverage, and IPC/BitTorrent/Android/tools/packaging coverage.

- **CTest:** explicit 120 s/300 s sanitizer budgets, `RUN_SERIAL` for forked
  sandbox/storage tests, a shared fixed-`/tmp` resource lock, corrected labels,
  `ci` on compile-free checks, `-UNDEBUG` for tools/fuzz, Perl timeouts and a
  Windows skip-77 stub.
- **Regression:** added `test-x25519-rfc7748` (RFC 7748 section 6.1 KAT)
  through the production `X25519Keys` API.
- **Workflows:** every executing CTest call now has `-j`, `--no-tests=error` and
  `--output-junit`; JUnit/Testing evidence is archived; Linux/sanitizer builds
  require Perl; MSVC builds/runs both Windows privsep targets; OpenBSD archives
  `configure.log`; Android `javap`/DEX checks are SIGPIPE-safe; `adb
  wait-for-device` is bounded; the duplicate optional `perlcritic` block is
  removed.
- **Guard:** `tools/scripts/check-workflows.pl` enforces manual-only triggers,
  no `continue-on-error` and the CTest flags, and `docs/testing.md` must
  document every fuzzer target.
- **Docs/Skills:** updated `docs/ci.md`, `docs/testing.md`,
  `docs/development/perl-migration.md`, `AGENTS.md`, the `openi2pd-build` and
  `openi2pd-memory-security` skills.

Status: Implemented / static; Pending CI compilation; Pending CI tests. No
configure, build, CTest, sanitizer, fuzzer, Gradle/device, workflow dispatch,
commit or push occurred.

## 2026-10-26 Security fuzzing and negative coverage (static)

Three read-only audits covered fuzz/sanitizer infrastructure, SSU2/NTCP2/
Ratchet/netDb/I2NP negative paths, and SAM/I2CP/BitTorrent/JNI/IPC boundaries.

- **Corpus:** added checked-in seeds for 15 fuzzers, per-target `-max_len` and
  artifact prefixes; CMake smoke passes the seed directory; the static checker
  enforces corpus/target/seed bounds and dual build/run registration.
- **Harnesses:** added `fuzz-ntcp2-blocks`, `fuzz-sam-control`,
  `fuzz-i2cp-message` and `fuzz-i2np-database`; fuzz targets now use
  `-fno-sanitize-recover=all` and the workflow sets ASan/UBSan options at job
  level with per-target logs/summary and seeded corpora.
- **Negative tests:** added `test-ssu2-payload-blocks` for the pure SSU2
  block-framing validator.
- **Sanitizers:** added a UBSan-only matrix lane; sanitizer CTest remains
  serial with 300 s per-test budgets; MSVC no longer overrides the per-test
  timeout.
- **Documentation/Skills:** updated `docs/testing.md`, `docs/ci.md`,
  `docs/security/memory-safety-2026.md`, `TODO.md` (TEST-032..036) and the
  `openi2pd-ci` reference matrix.

Status: Implemented / static; Pending CI compilation; Pending CI tests. No
fuzzer, sanitizer, compiler, CTest, workflow dispatch, commit or push occurred.

## 2026-10-26 Agent knowledge synchronization (static)

Six read-only audits covered the skills inventory, C++23/crypto/interoperability,
IPC/platform, BitTorrent/Android/i18n, and build/CI/tooling/packaging. No audit
compiled, executed a test, dispatched a workflow or modified the tree.

- **AGENTS.md:** added the permanent non-duplication rule (link the single
  policy owner; remove stale copies in the same increment) and the mandatory
  knowledge-review record; corrected `Pong` receiver semantics, per-type
  protocol v5 versioning, the CTest registration scope, the `skills-static`
  job wording and the 15-fuzzer/corpus/sanitizer-matrix inventory.
- **Skills:** corrected the log-fd sink and `WorkerHello` description
  (`privsep`), NTCP2's two-column Request/Created padding table and
  `limitAction=drop|reset` status (`i2p-interop`), SSU2 per-message versus
  per-incomplete-message caps and the global `fuzz.yml` `-max_len` override
  (`memory-security`), OpenSSL probe stamping and `Signer::Sign` fallibility
  (`cryptography`), `jthread`/profile-ownership boundaries (`cpp23`), the
  Android runtime-admin bridge/SAF caps/API-denylist reality (`android`),
  native/Android plural arity (`i18n`), torrent ADD-FILE/fuzz-corpus rules,
  the four-way fuzzer contract, `UBSan` lane and real label inventory
  (`build`/`ci`), service stop budgets and packaging payload rules
  (`packaging`), plus the TUI, OpenBSD, Linux/Windows, tooling, upstream and
  documentation slices.
- **Checker:** `tests/check-agent-skills.cmake` now pins the knowledge-review
  and non-duplication phrases in `AGENTS.md`.
- **Tracking:** `TODO.md` corrects SEC-009, GOV-007 (17 skills) and TEST-030
  and adds GOV-009; `ROADMAP.md` corrects the fuzzer count.

Status: Implemented / static; Pending CI tests. Only compile-free checks ran
(`check-agent-skills.cmake`, `check-language-modernization.cmake`,
`check-i18n-catalogs.pl`, source/link inspection). No compiler, CTest, Gradle,
device, package, sanitizer, fuzzer, workflow dispatch, commit or push occurred.

## 2026-10-26 Compile/link seam audit (static)

Six read-only subagents covered core C++23/headers, transports/crypto/netDb/
client protocols, IPC/privsep, torrent/TUI/tools, Android/JNI and
CMake/linkage. They were instructed not to compile, configure, run tests or
edit files.

- **Confirmed and corrected:** `TunnelPool::TunnelExpired` used the removed
  `std::pair` shape; `Streaming.cpp` referenced
  `i2p::client::STOP_ON_SERVICE_TIMEOUT` unqualified; `WindowsChannel`
  declared `m_Role`/`m_RoleSet` twice and lacked the
  `IsCurrentProcessInJobObject()` definition; `Daemon.cpp` rollback used
  `_ANDROID__`; TUI title/status builders concatenated `std::string_view`
  translations with `std::string`; `Crypto.cpp`, `Log.cpp`,
  `SSU2Session.h` and several public headers relied on transitive includes;
  the Windows IPC test narrowed the wide worker nonce; the AF_UNIX control
  test did not share the production guard.
- **Build graph:** added `<openssl/bn.h>` to the combined linked OpenSSL
  probe, raised the preset schema floor to CMake 3.25, propagated
  `${I2PD_EXTRA_LIBS}` to privsep/TUI, published the `i2pd_privsep` →
  `libi2pd` export edge, linked the header-only privsep test suites to the
  archive and made `tools/LICENSE` install from one component.
- **Checks/skills/docs:** `check-i18n.cmake` pins the materialised
  translation concatenation; the C++23/TUI/i18n/Linux-Windows/privsep/build
  skills and `docs/static-build-audit.md` record the decisions; BUILD-020 and
  the roadmap continuation track the residual.

Status: Implemented / static; Pending CI compilation; Pending CI tests. Only
compile-free checks ran. No compiler, configure, CTest, Gradle, device,
package, workflow dispatch, commit or push occurred.

## 2026-10-26 Project documentation BSD-3-Clause scope (static)

Per the requested license consolidation, the project-authored text in
`docs/file-headers.md` and `docs/torrent/snark-compatibility.md` now declares
BSD-3-Clause. Their original attribution and technical references remain; the
Snark inventory describes independently implemented behavior and does not
include Java source text. Debian copyright, RPM and container license
metadata, `NOTICE`, documentation policy, and the legal metadata contract now
describe BSD-3-Clause for all project-authored documentation. Third-party
licenses and upstream provenance remain intact.

Status: Implemented / static; Pending CI compilation; Pending CI tests. Static
review only; no license scanner, package build, workflow, or legal review was
run.

## 2026-10-26 Compile/link audit continuation (static)

Parallel subsystem reviews found two definite router-storage compile errors:
the local descriptor owner lacked the `Release()` called by `Start()`, and
`Exchange()` accessed descriptor/message fields after calling the message-only
`Receive()` API. `RouterStorage.cpp` now transfers descriptor ownership with
`std::exchange` and receives a `ReceivedMessage` through `ReceiveWithFd()`.

The same review made public and inline headers self-contained for the standard
types they expose (`string`, `map`, `hash`, `pair`, `move`, `mutex` and
`memcpy`) across crypto, NTCP2/SSU2, streaming, tunnel endpoints and torrents;
the affected torrent/TUI translation units include `<utility>` directly.
Android's 43 Java/JNI declarations, AIDL v2 overrides and resource references
were coherent by inspection and needed no edit. The explicit CMake source and
link graph was also coherent in the reviewed configurations; its Windows
documentation now records the existing `userenv` edge.

Status: Implemented / static; Pending CI compilation; Pending CI tests. The
only local validation was source/interface inspection and `git diff --check`.
No compiler, CMake configure, CTest, Gradle, project validator, workflow,
commit or push ran.

## 2026-10-26 Latest workflow failure triage (static)

Read-only GitHub CLI review of the latest batch on `a2b8dc481b322db6d5997f586650766948772217`:
Windows `40381344546`, packaging/services `40381324823`, OpenBSD `40381304032`
and fuzzing `40381278483` failed; Linux/Android `40381250464` ended cancelled
after its completed jobs reported failures. No workflow was dispatched or
rerun for this triage.

- **C++ compile corrections:** `Destination.cpp` omitted the final
  `RequestLeaseSet` argument when binding the member function; `Garlic.cpp`
  widened its cleanup snapshot to a base pointer that has no
  `CleanupExpiredTags()` method; `SSU2Session.cpp` lacked the direct
  `Blocklist.h` include; `TunnelPool.cpp` used an unqualified netDb constant;
  `Streaming::IsRecentlyClosed()` and `SocksResolveCache::Lookup()` erase from
  const cache views; and local I2PControl peer authentication called Asio's
  non-const `native_handle()` through a const socket. The common C++ issues
  stopped Windows, OpenBSD, Android, fuzz, Linux and package consumer builds;
  MSVC also reported the garlic/SSU2/cache/constant errors.
- **Packaging and static gates:** Debian stopped before building because the
  workflow omitted `dh-apparmor`; the service-contract gate found the legal
  notice source missing. The standalone `CC-BY-SA-4.0` and
  `LICENSE.i2pd-docs-en` files have now been removed from the repository and
  their standalone package-install entries removed. Existing CC SPDX
  declarations and license metadata for the two documents remain. The i18n gate
  falsely reported missing translations because its parser accepted only one
  C++ literal per catalog key/value; it now joins adjacent literals. The
  fuzzer inventory check used an invalid CMake string escape and now escapes
  the dot correctly.
- **Static review:** `git diff --check` only. No compiler, CMake configure,
  CTest, Gradle, package build, project validator or workflow run was performed
  against these corrections. CI compilation and test states remain
  `Pending CI compilation` and `Pending CI tests`.

## 2026-10-26 Latest workflow follow-up and static compile/link audit (static)

Read-only GitHub CLI triage covered the latest manual dispatches on
`b9cbdaa8abea9d7efa511aa02b90c7e8ef06869a` (`40389733799`, `40389743711`,
`40389738898`, `40389749162`, `40389753745`). The run was cancelled after
several jobs had already failed; the failure logs are evidence for those jobs
only, not for the cancelled remainder. No workflow was dispatched or rerun.

- `libi2pd_client/BOB.cpp`: `IsTrustedCommandPeer` called Asio's non-const
  `remote_endpoint(error_code)` and `native_handle()` through a `const
  tcp::socket &`. The parameter is now non-const; the only caller already
  passes the session's mutable socket.
- `privsep/WindowsIpc.cpp`: the local `TokenAppContainerSid` and
  `TokenIntegrityLevel` helpers shadowed the equally named
  `TOKEN_INFORMATION_CLASS` enumerators, so `GetTokenInformation` could not
  resolve its second argument under MSYS2 clang. The calls now use the
  global-qualified enumerators. The move constructor initializer order was
  aligned with the declared member order.
- `libi2pd_client/TorrentsDHT.cpp`: `ParseList`/`ParseDictionary` return the
  consumed `size_t`, not a pair; `GetEncodedValueLength` no longer applies
  `.second`.
- `libi2pd/util.h`: `Mapping` declared a move constructor without an explicit
  assignment operator, which deleted copy assignment. Both copy and move
  assignment are explicitly defaulted; `TorrentsDHT` copies the optional
  datagram options this way.
- `i18n/check-i18n-catalogs.pl`: the nested match that extracts the key
  fragments reset the `$1`/`$2` captures before the value run was read. Both
  literal runs are now snapshotted first; adjacent C++ literals remain one
  catalog key/value.
- `tests/check-packaging.cmake`: the container-entrypoint assertion used the
  invalid CMake escape `\[`, so the expected text was never matched. Brackets
  need no escaping inside a quoted CMake argument.
- `contrib/static/build-musl.sh`: Alpine no longer publishes a
  `miniupnpc-static` package; `miniupnpc-dev` already ships
  `/usr/lib/libminiupnpc.a`, which the static finder selects.
- `tools/tests/i2pdctl.t`: the Perl client's escaped diagnostic intentionally
  prints `invalid choice: bogus` without the Python-argparse quotes; the test
  now matches the actual, terminal-safe message.

The six parallel read-only reviews also confirmed:

- `privsep/OpenBSDImsg.cpp`/`build/CMakeLists.txt`: OpenBSD 7.7–7.9 expose
  `imsg_get()` while 8.0 replaced it with `imsgbuf_get()`. A `libutil`
  `check_symbol_exists` probe defines `I2PD_HAVE_IMSGBUF_GET`; the adapter
  keeps both branches and `check-build-system.cmake` pins them.
- `privsep/Channel.cpp` defines `_GNU_SOURCE` before consuming `struct ucred`,
  `SO_PASSCRED` and `SCM_CREDENTIALS`; `privsep/Supervisor.cpp` no longer names
  a local RAII object `unexpected`.
- `tools/test-perl-install.cmake` installs the `Documentation` component when
  docs are enabled, because that component owns `LICENSE.i2pd-tools`;
  `tests/check-legal-metadata.cmake` uses the real `2013-2026` holder.
- `tools/test-tools-inventory.cmake` and `tools/test-tools-cli.cmake` used the
  invalid CMake escapes `\.` and `\0`; the base64 `AAAA` payload still decodes
  to the NUL bytes the CLI regression needs.
- `.github/workflows/packaging.yml` uploads `static-file.txt` from the
  workspace instead of an unsupported `/tmp` path.
- Direct standard-library includes were added to `Socks5.h`,
  `NetDbRequests.h`, `Transports.h`, `TransportSession.h`, `Blinding.h`,
  `MatchedDestination.h`, `TorrentPayloadStorage.h`, `Queue.h`, `Gost.h`,
  `I2PControlHandlers.h` and `IdentMetrics.cpp`; `Config.cpp` and
  `ClientContext.cpp` include `Crypto.h` before testing `OPENSSL_PQ`, and
  `Crypto.h` now defines `OPENSSL_PQ 0` when it is unavailable.
- `libi2pd_client/SOCKS.cpp` dropped the unused, undefined
  `AsyncUpstreamSockRead()` declaration.
- `NTCP2.cpp` pins the 8-byte SipHash tag with `OSSL_MAC_PARAM_SIZE` through
  `EVP_PKEY_CTX_set_params`; `SSU2Session.cpp` rejects a PeerTest msg 2 with no
  peer identity instead of dereferencing null.
- Android: a missing `statusCode` defaults to `-1` (not an observed status),
  missing internal storage throws an explicit `IllegalStateException`, a
  reseed error cannot inject a row separator/control, and `content_max_width`
  now follows `w600dp` as well as `sw600dp`. A pure-JVM regression covers the
  missing-status contract.

Residual static findings not corrected in this pass: the Android SAF grant
status call on the UI thread, JNI state calls under the state monitor, unused
data-directory JNI helpers, the `TorrentsComments.h` `NO_TORRENTS` guard
asymmetry, the latent `AcceptLocalControlPeer` template overload hazard, the
unaudited provider/ABI branches and the `TorrentsComments`/TUI documentation
mismatch. The CMake review's `CURSES_INCLUDE_DIRS` concern was checked against
the documented module variable (available since CMake 3.1) and required no
change.

Status: Implemented / static; Pending CI compilation; Pending CI tests. The
only local validation was source/interface inspection and `git diff --check`.
No compiler, CMake configure, CTest, Gradle, package build, project validator,
workflow, commit or push ran. CI still owns all dynamic validation.

## 2026-10-26 Source documentation pass (static)

Nine subagents received disjoint scopes and edited comments only: core
transports/protocols; crypto/netDb/persistence; IPC/daemon/Win32; torrent
engine; client services; TUI/tools; Android Java/JNI/AIDL/SAF; Perl; and
service/packaging shell. Each was told to document contracts and "why", not to
narrate code, and not to compile, generate docs or run tests.

- **C++:** ownership/lifetime, mutex-to-field mapping, thread affinity, bounded
  buffers, tunnel/transport state machines, crypto zeroization and provider
  gates, netDb snapshot/disk-I/O rules, IPC descriptor/role contracts,
  cancellation and generation fencing, torrent piece/tracker/DHT/SAF rules,
  proxy sanitization and SAM/I2CP framing.
- **Android:** service/run-state lifecycle, executor/main-thread ownership,
  SAF AIDL v2 owner/generation/cancel rules, JNI reference/exception rules,
  permissions and Material 3/i18n boundaries. The misplaced broker comment in
  `AndroidManifest.xml` was attached to `RouterService` and `DaemonAndroid.h`
  duplicate Javadoc was merged.
- **Perl:** POD headers for purpose, CLI, I/O, exit codes, core-module
  dependencies and OpenBSD portability; inline comments for complex regex,
  JNI mangling, PNG/ICO parsing and reaping. **Shell:** POSIX contracts for
  service signals/stop budgets, identity, cleanup, maintainer-script phases,
  upgrade/purge safety and static packaging.
- **Documentation assets:** added `docs/source-documentation.md`, linked it
  from the developer guide and MkDocs, and extended the documentation, C++23,
  Android, TUI, tooling and packaging skills. `AGENTS.md` already carries the
  permanent source documentation policy.
- **Comment defects fixed:** `HTTPProxy.cpp` had a literal `*/` inside prose
  that closed the block early; `FS.h` `@param` names and the unnamed
  `Log::Append` parameter were corrected. Checker drift exposed by the pass
  was corrected: `check-build-system` now applies Crypto.h fallback checks only
  to `I2PD_OPENSSL_PROBES`, `check-control-security` matches the multiline
  I2CP call, the Android/tools licences carry the expected BSD-3-Clause SPDX
  identifier, and a Shepherd comment avoids the checker's forbidden wait-loop
  words. One clear bounded defect uncovered while documenting
  `Streaming.cpp` was fixed: the NACK accumulator wrapped in `uint8_t` before
  the `>= 256` guard, so it now uses `size_t` and casts only at the one-byte
  wire write (SEC-033); a regression is prepared but not executed. The OpenBSD
  imsg send path also now closes the channel on every post-enqueue failure, so
  a partially queued mutation cannot be replayed (PRIVSEP-032). `DOCS-015` records the fixes;
  CONC-028, SEC-032, TOOL-018, PKG-028 and WIN-014 record findings that need
  design/runtime work and were not changed here.

Status: comments and documentation Implemented / static; the fixes are
Pending CI compilation and the generated API lane is Pending CI. Static checks
(`check-agent-skills`, `check-doc-links`, `check-doc-nav`,
`check-build-system`, `check-i18n`, `git diff --check`) passed. No Doxygen,
Javadoc, compiler, CTest, Gradle, package, validator, workflow, commit or push
was performed.

## 2026-10-26 CI-driven compile fixes (static)

The manual workflows dispatched for `88ac1e92` were inspected with `gh`
(`run view`, `--log-failed`, `--job --log`). All five batches failed; the
Windows run was read after its jobs completed. The
diagnostics were grouped by first error, not by cascading message:

- **CE-001:** `HTTPProxy.cpp` used `I2PTunnelHTTPHeader` unqualified inside
  `i2p::proxy`; qualified the member and four `Result` comparisons with
  `i2p::client::`.
- **CE-002:** `ClientContext.cpp` called the non-existent
  `ip::address::to_string(error_code&)`; now uses
  `i2p::util::net::FormatAddress`, which also avoids Asio's POSIX scope-ID
  formatter.
- **CE-007:** MSVC failed on `pid_t` in the public `privsep/Worker.h`; guarded
  `<sys/types.h>` and added a `worker_pid_t` alias (`int` on Windows) for the
  POSIX-only `ChildProcess` declarations.
- **CE-008:** `basic_waitable_timer` has no `cancel(error_code&)`; `I2CP` and
  `SAM` now call `cancel()` directly.
- **CE-003:** the source-docs workflow now creates the gitignored
  `docs/generated/source-api` before Doxygen; pinned in
  `check-build-system.cmake`.
- **CE-004:** the static link probe passed `libcrypto.a` before `libssl.a`.
  Fixed the order in both probe stages, restored the saved
  `CMAKE_REQUIRED_*`/try-compile state correctly, and pinned the order.
- **CE-005:** the unused SysV `log_success_msg` LSB shim tripped ShellCheck
  SC2317/SC2329; documented `# shellcheck disable=SC2317,SC2329` and pinned
  the shim and directive.
- **Documentation:** added `docs/ci/COMPILATION-ERRORS.md` with run IDs,
  commit SHA, first diagnostics, causes, corrections and validation states;
  linked it from the developer navigation.

Static verification: all `check-*.cmake` contracts, `shellcheck -s sh
contrib/sysvinit/i2pd`, `sh -n`/`ksh -n`, documentation links/navigation and
`git diff --check` pass. No compiler, CMake, CTest, Gradle, Doxygen, package
or workflow was run; every correction remains Pending CI compilation/tests.

## 2026-10-26 CI warning cleanup (static)

The same `88ac1e92` logs were filtered by warning category. The first errors
had hidden several warning classes; the following were corrected locally:

- **Cross-platform:** a `/*` inside block-comment prose in
  `I2PTunnelAccessFilter` and `Daemon.cpp`; the unused `this` capture in
  `SSU2Session`; `HTTPProxyClientConnection::Start` hiding the base
  `Start()`; the OpenBSD-only unused `RoleWantsNetworkNamespace`.
- **OpenSSL 4:** replaced deprecated `X509_NAME_get_text_by_NID` in
  `Family.cpp`/`Reseed.cpp` with a bounded
  `i2p::crypto::GetX509NameTextByNID` built on the stable index/entry/ASN1
  accessors.
- **MSVC:** replaced `getenv` with `_dupenv_s`, `strerror` with
  `std::generic_category().message()`, and `_wopen` with `_wsopen_s`; made
  size_t/int, int64/uint8 and double/float conversions explicit where the
  bounds are already enforced; renamed shadowing locals (`context`, `l`,
  `ind`); removed unused formal names/lambda captures; widened the DHT bit
  index before the `< 32*8` guard so it cannot wrap through `uint8_t`.

Static checks and `shellcheck` pass; no compiler, CTest, Gradle, Doxygen or
workflow was run. These warning fixes are Pending CI compilation and are not
claimed as a clean warning-free build.

## 2026-10-27 Source documentation continuation (static)

Seven subagents documented the remaining implementation-heavy areas with
disjoint file scopes:

- **Core runtime:** garlic envelope/clove walk, destination lease publication,
  datagram reliability/registration, transport peer/key management, SSU2
  receive/service split and relay dispatch, tunnel build/peeling, endpoint
  fragments and gateway framing.
- **Crypto/persistence:** provider/fallback zeroization, X25519/ElGamal/ECIES
  envelopes, HKDF split, identity/RouterInfo/LeaseSet validation, atomic FS
  writes, logger bounds/redaction boundary, startup-only config and profile
  lock order.
- **IPC/platform:** Windows peer/token/AppContainer/Job/Object ordering,
  OpenBSD unveil/pledge, credential drop and storage/log restart contracts.
- **TUI:** terminal cell/column model, ncurses/Win32 differences, theme
  degradation, IPC deadlines and `TuiProtocol` framing.
- **Android:** remaining fragments, snapshot/status parsing, run state,
  SAF creation/grants and BOOT_COMPLETED receiver contracts.
- **Shell/packaging:** systemd/runit/s6/OpenBSD rc.d/port and Debian
  maintainer-script upgrade/purge behavior.
- **C++ tools:** remaining CLI/exit-code, key-file and registration-grammar
  contracts.

All edits were comments only; a diff audit found no non-comment added or
removed code lines. Defects found during the pass are tracked as CONC-029,
SEC-034, TUI-014, ANDROID-036, TOOL-019 and PKG-029; DOCS-013 records the
remaining coverage. Static checks (`check-*`, docs links/nav/branding,
shellcheck, `git diff --check`) pass. No compiler, CMake, CTest, Gradle,
Doxygen, package, workflow, commit or push was performed; generated docs and
all behavioral findings remain Pending CI.

## 2026-10-27 Second CI-driven compile pass (static)

The manual `b8c6473e` dispatch was read with `gh`: Linux/Android 40416816523,
OpenBSD 40416823112, Fuzzing 40416827033 and Packaging 40416830825 failed;
Windows 40416819852 had MinGW failed and MSVC still running. The first errors
after the previous fixes were:

- **CE-009:** the new X509 helper used mutable `X509_NAME_ENTRY`/
  `ASN1_STRING` locals, but OpenSSL 4/LibreSSL return const pointers; changed
  to const accessors.
- **CE-010:** `b33offlinekeys` called `LoadPrivateKeys` unqualified; now
  `i2p::tools::LoadPrivateKeys`.
- **CE-011:** Android JNI copy-initialized `std::string` from a
  `std::string_view`; now direct-initializes.
- **CE-012:** Doxygen parsed placeholder tags (`<expected>`, `<COMMAND>`,
  `<port>`, etc.) as XML and aborted under `WARN_AS_ERROR`; comment
  placeholders are escaped as `&lt;...&gt;`, and a documentation-pass rule now
  requires this audit.
- **CE-013:** SOCKS missing `override` and unused fields; DHT signed
  comparison; unused local-control overload; OpenBSD Boost.Asio endpoint
  formatting reached `strcat`. The generic `LogPrint` is constrained so Asio
  address/endpoint types use the safe `FormatAddress`/`FormatEndpoint`
  overloads.

Static checks (all `check-*`, docs links/nav/branding, shellcheck and
`git diff --check`) pass. No compiler, CMake, CTest, Gradle, Doxygen or
package was run; the fixes remain Pending CI compilation and the Windows MSVC
lane was still in progress when the diagnostics were collected.

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

<!--
See the LICENSE file at the top of the project tree for copyright
and license details.

openi2pd: ROADMAP.

Documents project documentation.

-->

# Roadmap

This roadmap coordinates parallel workstreams. It is subordinate to the
requirements and current code; `TODO.md` is the authoritative task list.

## Current baseline

Permanent policy: no agent compiles, configures a build or executes tests
locally. Builds and dynamic validation are CI-only through manual
`workflow_dispatch` workflows. The results below are historical evidence from
earlier revisions, not validation of current uncommitted changes.

- Historical Linux/GCC 16: CMake configure and build passed on 2026-10-18 with
  GCC 16.2.0/OpenSSL 4.0.3 (196 targets, including `i2pd` and `i2pd-tui`).
- Historical CTest: 48/48 passed on 2026-10-18, including the new NTCP2/SSU2
  transport, interop/security and torrent/TUI regressions.
- TUI: live UNIX-socket session on Linux passed historically (state, peers,
  tunnels, netDb, logs, malformed requests, quit, cleanup). The current tree
  adds Connections, router/LeaseSet directories, log search, bandwidth
  history, transit toggle and daemon log-level control; none has been compiled
  or run.
- Tools: all 15 built; keygen/keyinfo/offlinekeys/i2pbase64/regaddr/verifyhost/
  x25519 smoke-tested.
- Android: Java 25 source/target compiles with JDK 25/AGP 9.4.1; Material 3
  UI/architecture source is present. APK/JNI/Lint/device tests are deferred to
  CI by user instruction.
- Tools: all 15 native i2pd-tools executables and the Perl I2PControl client
  are in the CMake graph with CTest inventory/smoke/regression, Perl
  portability and install-layout coverage. CMake, Debian, RPM and the Windows
  packaging helper install the router, TUI and tools; the pinned upstream
  commit and licence provenance are recorded in `docs/tools/provenance.md`.
  No local tool build or CTest run validates the current tree.
- Torrent: web console removed; native C++ control + ncurses TUI + local IPC
  and Perl `i2pdctl` are implemented. Pause/resume are explicit typed
  operations, startup scanning and file I/O run on the storage execution
  domain, and `.i2p` tracker validation is shared by every tracker entry
  point. The daemon remains monolithic, so the OS-level network/storage
  process invariant is still pending. Dynamic TUI/engine/RPC/TLS validation
  is deferred to CI.
- Java I2P: latest stable `2.13.0` rechecked on 2026-10-19 and pinned
  (`9134f808337b401e8e53c73734c81fab04280c9d`); the five-subsystem static
  pass, compatibility matrix and local unit regressions are complete, but no
  Java peer, handshake or network interoperability has been executed.
- Java I2P remaining work is tracked as `NET-016..NET-020` and the older
  I2P-001..I2P-013 evidence rows in `TODO.md`; dynamic tests remain blocked.
- Windows TUI/IPC: shared terminal abstraction, native Win32/VT backend,
  secure local Named Pipes, CMake selection, tests and manual CI/documentation
  are implemented statically at baseline `800c8627`. No Windows console,
  DACL, cross-user or MSVC/Clang runtime validation exists yet.
- C++23 ownership pass is in progress: Streaming/Gzip, LeaseSet/Identity,
  NTCP2/Transports, client temporaries, TorrentsDHT and daemon/library thread
  owners have explicit RAII; Kademlia, Garlic tags/payloads, Reseed buffers,
  Signature/OpenSSL, daemon descriptors and client server owners are now
  scoped too. Remaining raw packet/crypto buffers are tracked.
- A static memory/concurrency audit now covers SSU2/RouterInfo, crypto/reseed,
  BitTorrent, TUI/privsep/JNI and daemon lifecycle. Confirmed structural fixes
  and regression/fuzz sources are present; ASan/UBSan/TSan and fuzz execution
  remain mandatory before claiming the audit complete.
- Dependency floors are now explicit: OpenSSL 3.0.2 / LibreSSL 4.0,
  zlib 1.2.11, Boost 1.74 core / 1.81 JSON+URL, MiniUPnPc 2.2.6/API 17,
  C++23; Boost.Atomic, Boost.Filesystem, XP/MSVC-only and old-API fallbacks
  are gone. OpenBSD libc++ gaps remain isolated and documented. No
  dependency-matrix build has been executed.
- CMake owns the build graph and now exposes `i2pd_privsep` as a separate
  component; POSIX Make targets cover configure/build/router/TUI/tools/tests/
  install, default to Ninja, and no maintained front-end uses GNU-only syntax.
  The static CMake/Make audit (`build-system-static`) passed; configure, build,
  install and CTest remain pending manual CI.
- Static-review passes for Torrent/TUI, I2P, Perl, tools/firewall, IPC,
  OpenBSD and Android are integrated. No compiler, CTest, APK/Lint, sanitizer,
  fuzz, OpenBSD or device test was run in this environment.

## Workstreams

### 1. Correctness and security (critical path)

Order:

1. Finish builds/tests after current edits (C++ CMake, tools, firewall test).
2. Run sanitizers and fuzz targets; fix findings.
3. Audit untrusted parsers (NTCP2/SSU2/I2NP/RouterInfo/LeaseSet/SU3).
4. Resolve TUI status data races, torrent control snapshots and Android JNI
   lifetime issues.

Exit criteria: all prepared tests pass, sanitizer findings triaged, no known
memory-safety defect left unfixed.

### 2. I2P Java interoperability (independent after clone)

1. [done/static] Pin I2P Java 2.13.0 at commit
   `9134f808337b401e8e53c73734c81fab04280c9d` in a read-only external tree.
2. [static recheck complete 2026-10-19] Maintain the `INTEROPERABILITY.md` matrix with Java and C++
   file/function references, differences, priorities and validation status.
3. [static implementation complete] I2P-001..I2P-012 have static C++
   implementations; I2P-013 is blocked on Java/dependency evidence. The
   2026-10-19 third pass added NET-155..NET-161, LS-119..LS-121,
   DB-113..DB-114, RS-101, SAM-110, DG-105, STR-104 and CRY-202..CRY-203;
   residual items remain listed in `docs/INTEROPERABILITY.md`. Do not copy Java
   architecture or accidental behavior.
4. [static fixtures expanded] Expand `tests/interop/` with deterministic valid/malformed
   vectors and controlled loopback procedures.
5. [pending] Run controlled Java/C++ integration tests, sanitizers and fuzzers;
   the local Linux/GCC 16 build and 48/48 unit CTest are executed, but no Java
   peer, ASan/UBSan/TSan or libFuzzer run exists.

Exit criteria: every announced protocol row has a Java reference, a C++
reference, a difference verdict and validation status; dynamic evidence is
recorded separately from static review. No "compatible" claim without evidence.

### 3. Android/Java 25 (CI build and device validation pending)

1. Use CI JDK 25, Android SDK 37 and NDK r30 LTS+ (local builds are frozen). The
   Android UI is English-only: language resource catalogs are removed and
   `locales_config.xml`/`localeFilters` declare only `en`.
2. Validate Gradle 9.8.1/AGP 9.4.1 and toolchain 25 in CI.
3. Build APK/DEX; run Android Lint, unit tests and JNI/device tests.
4. Continue Java 25 source refactor (records, sealed types, pattern matching,
   switch expressions, lifecycle separation); D8/R8 class-version support and
   ART API constraints are documented separately.
5. Test rotation, background service, restart and permissions.
6. Validate the second UI pass on device: Logs empty state, service banner and
   Retry, Connections navigation, lifecycle-based controls, tablet content
   width, graceful progress, TalkBack/font scaling and light/dark rendering.

Exit criteria: reproducible APK build with JDK 25 + documented toolchain; no
hard-coded credentials; JNI shutdown/restart tests pass.

### 4. Tools and firewall

1. Run the routerinfo firewall unit test and smoke test in manual CI,
   including rendering-boundary and per-backend malformed-input coverage.
2. Validate each backend syntax with native tools:
   `iptables-restore --test`, `nft -c`, `pfctl -nf`, Windows test host.
3. Continue tool memory/concurrency audit, especially `vain` and `famtool`.
4. Keep the `-6`/`-y` selection rules and the outbound-only/no-apply contract
   synchronized with `tools/routerinfo.cpp` and the tool README.

Exit criteria: all backend regression tests pass; legacy `-f` output is
byte-stable; no generated rule permits `any`.

### 5. Rebranding and identity

1. Keep protocol/ABI/persisted identifiers where compatibility requires it.
2. Rename Android package/applicationId and visible resources (implemented).
3. Decide executable/service/config/data-path migration with aliases.
4. Rename packaging identities with upgrade paths.
5. Replace placeholder icon once a design is supplied.

Exit criteria: user-visible identity is Open I2Pd consistently; every retained
`i2pd`/PurpleI2P reference is classified as legal, technical, provenance or
pending migration in `docs/REBRANDING.md`.

### 6. Documentation and provenance

1. Validate Markdown links, local anchors and the optional MkDocs navigation.
2. Keep `INTEROPERABILITY.md` synchronized with code changes; the duplicate
   `interop-matrix.md` has been removed.
3. Update Java/Android documentation after build validation.
4. Keep `docs/attribution-audit.md`, `docs/upstream-sync.md` and this TODO set
   current. `sync.sh --docs` must report `Coverage OK` for the pinned upstream
   tree, including the two mapped BitTorrent pages.
5. Keep the fork guide index (`docs/README.md`, `docs/index.md`,
   `docs/developer-guide/README.md`) linked to C++23, IPC, TUI, BitTorrent,
   Android, CMake, Perl and OpenBSD sources of truth.

Exit criteria: documentation builds without a server; all upstream commits
pinned and classified; no HTML import, duplicate matrix or stale
web-console/GNU-make instructions.

### 7. Privilege separation

1. IPC/channel/log worker foundation (implemented).
2. Supervisor process and pidfd/restart policy.
3. Storage worker and typed persistence migration.
4. Network worker and router-core event boundary.
5. Crypto worker/key-handle evaluation.
6. OpenBSD per-role policies, startup-time worker spawning, imsg adapter;
   Linux seccomp/Landlock/capability/net-namespace profiles; Windows
   message-mode Named Pipes, protected DACL, restricted tokens and Job Objects.
7. Negative capability tests and dynamic validation (storage/no-network and
   network/no-persistent-write are prepared for OpenBSD/Linux; Windows tests
   cover DACL, malformed IPC, peer identity and recovery).

Exit criteria: the four invariants in the privilege-separation request hold for
every supported platform, with evidence from OS-level capability tests. Until
then the monolithic core remains a documented residual risk.

### 9. Native BitTorrent console and TUI

1. Inventory every JS/HTML/CSS/RPC torrent action and map it to the existing
   C++ engine; do not rewrite working engine logic.
2. Add typed C++23 snapshot/control interfaces; build snapshots on the engine
   service thread with bounded waits.
3. Extend the local TUI IPC with bounded torrent list/detail/action messages.
4. Implement the ncursesw Torrents tab, details, search/sort, confirmations,
   session/DHT stats and error states. The shared client also provides bounded
   Connections, netDb router/LeaseSet directories, log search, traffic history
   and the two atomic router administrative operations.
5. Remove the web console only after parity is mapped; audit JSON-RPC as an
   optional local-only compatibility surface.
6. Harden parser, paths, symlinks, deletion, concurrency, anonymity and RPC
   limits; add deterministic tests.
7. Stage network/storage worker separation; until then the daemon remains
   monolithic and this must be stated explicitly.
8. Do not run local builds/tests in this environment; CI executes them.

Exit criteria: no torrent HTML/JS/CSS; TUI can administer the engine through
typed IPC; I2P-only operation; destructive actions confirmed; parser/path/RPC
tests prepared; full worker separation validated before claiming it.

### 10. Perl migration

1. Inventory all maintained Python and consumers.
2. Convert each script with core Perl modules; preserve CLI/output/exit codes.
3. Provide a safe HTTPS path with no silent cleartext downgrade and no CPAN
   runtime downloads.
4. Add `perl -c`, Test::More and fake-server tests.
5. Update CMake install, docs and packaging; remove Python only after migration.

Exit criteria: no maintained Python script, all consumers migrated, Perl tests
pass locally and on OpenBSD base Perl, and no Perl runtime dependency is added
to the daemon.

### 11. Native Windows TUI and IPC

1. Keep the shared `main.cpp`/`Theme.h` presentation logic and isolate only
   terminal and transport backends.
2. Preserve the ncursesw Unix backend and OpenBSD pledge/unveil behavior.
3. Validate the Win32 console/VT backend on Windows Terminal and conhost.
4. Validate Named Pipe DACLs, service/interactive account separation, remote
   client rejection, cross-user denial, reconnect and partial I/O.
5. Keep CI manual-only; native MSVC and MSYS2/Clang64 jobs remain unexecuted.

Exit criteria: console lifecycle, Unicode/resize, pipe authorization, negative
credential tests and packaging are recorded on real Windows hosts; no
compatibility claim from static inspection alone.

### 8. OpenBSD and CI

1. Validate CMake/Ninja, LibreSSL, pledge/unveil, MTU and custom datadir.
2. Compile and run the OpenBSD role-policy and imsg tests, including the
   storage/no-external-network and network/no-persistent-write negative cases.
3. Run manual GitHub workflows on all platforms.
4. Keep workflows manual-only.

Exit criteria: OpenBSD build/runtime evidence and manual CI results recorded.

## Parallelization rules

- Java audit and Android refactor are independent of C++ parser fixes but share
  the protocol matrix; update the matrix once per finding.
- Firewall backends are independent of the daemon/TUI after the endpoint model
  is stable.
- Do not edit the same file from two workstreams simultaneously.
- Documentation updates lag code changes by one verified commit-equivalent
  diff.

## Risks and blockers

| Risk | Impact | Mitigation |
|---|---|---|
| No Java router / Android SDK in this environment | Cannot validate interoperability or APK | Prepare harnesses and mark blocked |
| Non-atomic status reads | Data race reports | Atomic/snapshot refactor with TSan |
| JNI lifecycle/shutdown races | Crash on restart | RAII + lifecycle tests |
| Java 25 language vs ART API gap | APK incompatibility | Java 25 source/target, D8/R8 probe, Android API constraints documented |
| Firewall syntax differences | Broken rules | Native syntax checks, no execution |
| Upstream docs drift | Stale instructions | Pinned commits + manual sync script |

## Milestones

- **M1:** Current Linux/GCC 16 build and 48/48 unit tests green; sanitizer/fuzz/Windows/OpenBSD/Java runs remain separate.
- **M2:** Security sanitizer baseline clean or triaged.
- **M3:** Java 2.13.0 static matrix complete with confirmed fixes; dynamic Java/peer evidence remains pending.
- **M4:** Android APK builds with JDK 25, Java 25 source/target and passes lint/JNI tests.
- **M7:** Torrent TUI parity and Perl migration complete without web/JS/Python;
  CI validates the deferred builds/tests.
- **M5:** OpenBSD and manual CI evidence recorded.
- **M8:** openi2pd visual identity is consistent across Android, TUI, docs and packaging; SVG/contrast/device checks are recorded.
- **M6:** Documentation and TODO set closed with no unclassified items.

## Change control

No commits, tags or pushes are created by this work. Automatic CI stays
disabled. Any change to protocol or persisted formats requires a specification
reference and a regression test before it is considered complete.

## Native service and Debian packaging modernization (static, 2026-10-19)

Status: implemented by static analysis; pending manual CI compilation, package
build and service validation. Native definitions cover systemd, GNU Shepherd,
OpenRC, runit, s6/s6-rc and SysVinit, with exactly one manager selectable at
CMake install time. Debian packages split the router/tools (`i2pd`) from the
interactive ncurses client (`i2pd-tui`), use debhelper-managed systemd/SysV/
tmpfiles integration and provide DEP-8 smoke tests.

Exit criteria: manual CI builds both binary packages, `lintian` and
autopkgtest pass, `systemd-analyze verify` accepts the unit, each manager's
start/stop/restart/status/reload and signal behavior is checked on a disposable
host, and no TUI service or duplicate router instance is created. Until then,
do not claim runtime service, privilege-separation or package validation.

## Memory-safety corrections (static, 2026-10-18)

Status: **implemented by static analysis, pending compilation and dynamic
validation**. The detailed finding matrix is in
`docs/security/memory-safety-2026.md`; task tracking is `MEM-001..MEM-032` in
`TODO.md`.

Delivered corrections cover PeerTest block filtering, SSU2/NTCP2 shutdown
serialization, the SSU2 state-by-block policy and pre-auth guards, Ratchet
tag/NEXT_KEY budgets and iterator safety, RouterInfo buffer snapshots,
copy-on-write remote LeaseSets, NetDb startup locking, corrupt `router.keys`
rejection, TunnelTest bounds, I2CP null-session handling, Datagram/HTTP proxy
callback ownership, SAM shared-destination lifecycle, BitTorrent partial-block,
memory-budget, piece-mutex and filesystem-containment hardening, DHT iterator
extraction, RouterContext mutator serialization, bounded read-only TUI
configuration snapshots, IPC credential/alignment/fd ownership, absolute IPC
deadlines, TunnelPool zero-hop deadlock/restart recovery, timestamp/NTP and
service lifecycle serialization, and Android JNI exception/lifecycle
boundaries.

Not yet validated dynamically:

- ASan+UBSan and TSan over the full CTest suite.
- libFuzzer smoke passes and malformed protocol corpora.
- Live Java/I2P peer, SSU2 relay/PeerTest, SAM/I2CP streaming and BitTorrent peer tests.
- OpenBSD/LibreSSL, Windows, Android unit/Lint/APK/instrumentation runs.

Residual ownership/concurrency work remains for SSU2 `m_Address`/callbacks
and shared socket/executor ownership, the live
`RouterContext::GetRouterInfo()` readers, I2CP/SAM output sequencing,
BitTorrent multi-thread piece handoff (mutexed but not single-owner),
`RunnableService` self-join and timer ownership, Streaming raw packet
containers, Windows pre-open filesystem TOCTOU, hard links and rename races.
These are not claimed fixed.

## Native I2PSnark parity increment (2026-10-19)

Status: implemented by static analysis for the scoped primitives and native
controls; pending CI compilation; pending CI tests. See SNARK-001..SNARK-008 in
`TODO.md` and the functional/protocol matrices in
`docs/torrent/snark-compatibility.md`.

Delivered source changes cover bounded multi-tracker magnets, preserved BEP 12
metadata and v1 construction from ordered files/precomputed hashes, paused
tracker administration over TUI IPC, private-swarm discovery guards, opaque
I2P DHT tokens/cryptographic transactions and stricter PEX/DHT messages. The
independent comments codec/store is opt-in locally and remains unadvertised.
Android now includes the native engine and a Material torrent panel through
the service/JNI boundary, with app-private storage.

Next unlocked work: storage-domain enumeration/hashing and complete create
commands; precise BEP 12 failover and tracker registry reclamation; DHT routing,
seed filtering and abuse budgets; negotiated comment exchange/persistence and
native controls; file priorities and bandwidth controls; Android SAF and
advanced operations. Research v2/hybrid support against exact specifications
and compatible peers before enabling it. Implement OS workers and negative
capability tests before claiming network/storage separation. All dynamic
verification remains exclusively in manually dispatched CI; no current result
validates this local increment.

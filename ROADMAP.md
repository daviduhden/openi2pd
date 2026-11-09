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

At 176aec32, native compilation succeeds and prior host/checker/privacy
corrections pass their executed regressions. History reaches 382 runs.
TF-100's negative fixture uses unsupported Markdown grammar and fails nine
lanes; the local inline-link correction is Pending CI tests. Full
matrix cleanliness still requires Windows GUI attribution, remaining host
coverage, fourteen earlier Lint warnings, native documentation coverage,
CONC-028 timer authority and PRIVSEP-017 deferred owner retirement.

The 80a487d5 retrospective reaches 362 runs on 63 SHAs. Native sanitizer,
OpenSSL, OpenBSD and selected MSVC regressions pass; Windows MSYS2 Unicode
and GUI-version failures remain. Local CONT-055/056 add idle-timer lifetime
and deadline controls, explicit host-interpreter selection, exact GUI-image
retention and capability reasons. These are Implemented / static, Pending
CI compilation / Pending CI tests. Native MSWin32 paths, fourteen Lint
warnings, skipped optional lanes and source-documentation coverage after
fb974bbe retain separate evidence requirements.

At 9f2934f8, CE-088/089 are Validated by CI in executed lanes. The retained
inventory reaches 352 runs on 62 SHAs; logger fixture and Perl allowlist
failures now replace the compile blockers. Local CONT-053/054 add absolute
POSIX child phase/startup deadlines, authoritative torrent availability and
adapted upstream sampling. These changes are Implemented / static, Pending
CI compilation / Pending CI tests. Windows Unicode/version, fourteen Lint
warnings and skipped capability/device coverage remain unresolved.

The 70f66977 retrospective now covers 342 runs on 61 SHAs. CE-088/089
block the native runtime matrix before execution. Portable timer teardown
and monotonic supervisor admission are Implemented / static, Pending CI
compilation and Pending CI tests. Torrent interest reconciliation adds a
prepared regression; prior runtime and platform gaps remain open.

The 0b7f0ab4 retrospective covers 322 retained runs with logs for 320.
CE-085/086/087 and TF-091 corrections are Implemented / static; common/native
and explicit mutex ownership tests, Android overlay guards, document-smoke
checks and new Javadoc fixtures are prepared for manual CI. RT-060/061/062
add retirement, Tag bounds and Windows absolute-deadline protections.
Compilation/runtime validation is pending. Remaining analyzer, Windows
startup/Unicode/version, Android Lint and runner capability debt stay open.
The compile failures do not revalidate TF-089/090 or RT-058/059 from the
earlier c3f5df53 review; see the six inventories under `docs/ci/`.

The i18n comment-readability follow-up is Implemented / static: natural
paragraphs and the 72-character limit preserve the existing implementation.
Static source comparison and documentation contracts cover this increment;
generated Doxygen/Javadoc rendering remains pending manual CI.

The subsequent source-documentation increment covers IPC/storage authority,
Android lifecycle and validation boundaries, Perl inspection scope, Shell
orchestration, CMake package discovery, workflow artifacts and service/package
ownership. Behavior is preserved by snapshot comparison; generated API
documentation remains Pending CI tests under DOCS-013.

The static compilation/linkage audit of published `e669765d` is tracked by
`BUILD-018` and `TEST-023`: Boost implementation linkage, translation helper
namespaces, fixture ordering, Landlock UAPI
portability and MinGW linker flags are corrected by source inspection only.
Windows usage requirements and direct header declarations are explicit.
The Material resource-ID hypothesis was disproved against pinned 1.14.0;
Android source remains unchanged by this audit.
See [the audit matrix](docs/static-build-audit.md) for reviewed scope and
remaining configurations. All compilation/tests remain pending manual CI;
`ANDROID-017` now has a separate static native-start ownership correction and
host regression; JNI/device evidence is pending. `ANDROID-018` adds phase-aware initialization unwind by static analysis;
production rollback, JNI/device and restart evidence remain pending manual CI.

The fourth static CI-driven audit is recorded as BUILD-025 and CE-022..CE-027
in `TODO.md` and `docs/ci/COMPILATION-ERRORS.md`. Exact-SHA runs on `cec24388`
found three independent compile blockers (protected lifecycle-test access,
zero-argument format instantiation under hardening, and a non-constant fuzzer
initializer-list). Those are corrected statically, as are Android AGP R8
resolution ordering and the Debian compressed-changelog smoke expectation;
each correction still awaits an appropriate manual CI run. The generated
Debian postrm Lintian duplicate-updaterc finding remains under investigation.
Static reconnaissance found no new CMake link-closure or transport/crypto
source defect; incomplete jobs do not establish downstream targets as passing.

The next exact-SHA batch, `b25d0c3a` (runs 40532383635, 40532389857,
40532395786, 40532402554 and 40532412220), found three independent compile or
package blockers: an unqualified `i2p::SHORT_TUNNEL_BUILD_RECORD_SIZE`, an
ambiguous Android SAF fixture overload, and Debian's compressed `README.md.gz`
path. The static-package artifact also confirmed that its custom postrm and
`dh_installinit` both called `update-rc.d`; the explicit duplicate is removed.
These corrections are implemented by source inspection only, pending a newer
manual CI run. Fuzzing passed on `b25d0c3a`; Linux, OpenBSD, Windows and
packaging did not. The Android native/Java production compile advanced to the
instrumentation Java test compile before its overload error; this does not
validate the APK/D8 step.

Permanent policy: no agent compiles, configures a build or executes tests
locally. Build/test/package workflows and dynamic validation are CI-only
through manual `workflow_dispatch`; the compile-free agent-maintenance gate is
the sole pull-request workflow. Every repository change synchronizes relevant
agent/skill knowledge and passes the structural maintenance gate. The results below are historical evidence from
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
- Android: Java 25 source/target is configured with JDK 25/AGP 9.4.1 and the
  Material 3 UI/architecture is present. No current compile/APK evidence is
  claimed; APK/JNI/Lint/device tests are deferred to manual CI.
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
- Java I2P: comparison target `2.14.0`, pinned to
  `3c98503c5f14efe7bdd783528ba7f621f12d8081` on 2026-10-21. The earlier
  five-subsystem static pass and matrices were based on 2.13.0; they require
  a new delta review under NET-026. Historical fixtures keep their 2.13.0
  provenance. No Java peer/handshake/network interoperability is claimed.
- Java I2P remaining work is tracked by `NET-026` (2.14.0 delta review),
  `NET-162..NET-167`, `PARITY-015` and the older I2P-001..I2P-013 evidence
  rows in `TODO.md`; dynamic tests remain blocked.
- Windows TUI/IPC: shared terminal abstraction, native Win32/VT backend,
  secure local Named Pipes, CMake selection, tests and manual CI/documentation
  are implemented statically at baseline `800c8627`. No Windows console,
  DACL, cross-user or MSVC/Clang runtime validation exists yet.
- C++23 ownership pass is in progress: Streaming/Gzip, LeaseSet/Identity,
  NTCP2/Transports, client temporaries, TorrentsDHT and daemon/library thread
  owners have explicit RAII; Kademlia, Garlic tags/payloads, Reseed buffers,
  Signature/OpenSSL, daemon descriptors and client server owners are now
  scoped too. The current continuation converts the tunnel-hop chain, removes
  the Garlic shared payload scratch buffer, moves `Piece` storage and the UDP
  resolver thread to RAII/atomics, and bounds address-book persistence writes.
  Remaining raw packet/crypto buffers are tracked.
- A static memory/concurrency audit now covers SSU2/RouterInfo, crypto/reseed,
  BitTorrent, TUI/privsep/JNI and daemon lifecycle. Confirmed structural fixes
  and regression/fuzz sources are present; ASan/UBSan/TSan and fuzz execution
  remain mandatory before claiming the audit complete.
- BOB and I2PControl control-plane hardening now redacts credential operands,
  closes raw-callback lifetime gaps, bounds TLS connections/tokens, adds
  cancellable asynchronous HTTP deadlines, rejects unsafe default-password
  exposure and atomically replaces PEM files. Static contracts are prepared;
  slow-client, teardown, sanitizer and platform filesystem CI remain pending.
- Dependency floors are now explicit: OpenSSL 3.0.2 through OpenSSL 4.0.x /
  LibreSSL 4.0, zlib 1.2.11, Boost 1.74 core / 1.81 JSON+URL, MiniUPnPc
  2.2.6/API 17, C++23; Boost.Atomic, Boost.Filesystem, XP/MSVC-only and
  old-API fallbacks are gone. CMake reads the selected `opensslv.h` and runs
  compile-only feature probes; `I2PD_OPENSSL_EXPECT*` guards the manual CI
  lanes. OpenSSL 4 removed TLS/custom-method/ASN.1 APIs are avoided, and
  runtime ML-KEM capability gates publication. OpenBSD libc++ gaps remain
  isolated and documented. No dependency-matrix build has been executed.
- CMake owns the build graph and now exposes `i2pd_privsep` as a separate
  component; POSIX Make targets cover configure/build/router/TUI/tools/tests/
  install, default to Ninja, and no maintained front-end uses GNU-only syntax.
  The manual Linux matrix is two branch workflows (latest Homebrew OpenSSL
  3.x and 4.x bottles, gcc and clang); the OpenBSD job asserts LibreSSL. The static
  CMake/Make audit (`build-system-static`) passed; configure, build, install
  and CTest remain pending manual CI.
- Static-review passes for Torrent/TUI, I2P, Perl, tools/firewall, IPC,
  OpenBSD and Android are integrated. A new independent memory-safety pass
  covers SSU2/NTCP2, ratchet/Garlic, netDb, torrent, IPC and JNI; it fixed
  peer-controlled family/garlic/clove/NTCP2/torrent/JNI bounds and records
  SSU2 pool/address races, fragment budgets, RouterInfo copy-on-write and
  ratchet/destination context sharing as `SEC-025`. A concurrent lifecycle
  pass fixed `RunnableService` self-stop, `Transports` shutdown/start
  ordering, `I2PControl` restart, atomic daemon shutdown flags, shared tunnel
  build scratch and SSU2 terminal resurrection; the remaining callback,
  datagram, profile, disk-domain, SSU2 publication and JNI owner-epoch work
  is `CONC-005`. CONC-009 now synchronizes individual RouterProfiles and
  canonical publication; CONC-010 coordinates process-local storage and final
  postponed-update draining. Cross-process authority, abandoned staging cleanup
  and external producer quiescence remain open.
  The POSIX make front-end, install component split, Ninja
  presets, OpenBSD generator guard, RPM/Docker packaging and exported
  package-config paths are consolidated under `BUILD-014..BUILD-017`. No
  compiler, CTest,
  APK/Lint, sanitizer, fuzz, OpenBSD or device test was run in this
  environment.

## Workstreams

### 1. Correctness and security (critical path)

Order:

1. Prepare and manually dispatch CI on the first publishable revision (C++
   CMake, tools, firewall test); do not build locally.
2. Dispatch sanitizer and fuzz jobs; fix findings from CI.
3. Audit untrusted parsers (NTCP2/SSU2/I2NP/RouterInfo/LeaseSet/SU3).
4. Resolve TUI status data races, torrent control snapshots and Android JNI
   lifetime issues.

Exit criteria: all prepared tests pass, sanitizer findings triaged, no known
memory-safety defect left unfixed.

### 2. I2P Java interoperability (independent after clone)

1. [done/static] Pin I2P Java 2.14.0 at peeled commit
   `3c98503c5f14efe7bdd783528ba7f621f12d8081` in a read-only external tree;
   preserve 2.13.0 fixtures as historical evidence.
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
5. [pending] Dispatch controlled Java/C++ integration tests, sanitizers and
   fuzzers. Historical Linux/GCC 16 build and 48/48 unit CTest results predate
   the static-only policy and do not validate the current tree.

Exit criteria: every announced protocol row has a Java reference, a C++
reference, a difference verdict and validation status; dynamic evidence is
recorded separately from static review. No "compatible" claim without evidence.

### 3. Android/Java 25 (CI build and device validation pending)

1. Use CI JDK 25, Android SDK 37 and NDK r30 LTS+ (local builds are frozen). The
   Android UI ships en/es/ru/zh-CN; `locales_config.xml`/`localeFilters` and the
   four resource catalogs must stay synchronized. Android plural categories
   are one/other for en/es, one/few/many/other for ru, and other only for zh-CN.
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
   assert a protected DACL, malformed IPC and peer identity, while live
   cross-user ACL denial and recovery remain pending).

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
pass in manual Linux/OpenBSD CI, and no Perl runtime dependency is added to the
daemon.

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
4. Keep build/test/package workflows manual-only; the compile-free
   agent-maintenance gate is the only pull-request exception.

Exit criteria: OpenBSD build/runtime evidence and manual CI results recorded.

### 12. Reusable Agent Skills

1. Keep `.agents/skills/README.md` as the compact discovery index.
2. Maintain one `SKILL.md` per subsystem with valid `name`/`description`
   frontmatter and a `references/` checklist for file maps, invariants and CI
   lanes.
3. Keep `AGENTS.md` as the permanent-policy source; skills link to it rather
   than duplicating the static-only, security and tracking rules.
4. Update skills whenever component paths, shared interfaces, tests or workflow
   ownership change.
5. Require coordinator review for shared interfaces and concurrent tracking
   edits.

Exit criteria: every skill name matches its directory, descriptions state
activation conditions, references resolve, no skill contradicts `AGENTS.md`,
and `agent-skills-static`, documentation-link and branding checks pass in
manual CI. No agent may claim the skill set dynamically validated before that
run.

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

- **M1:** The first manual CI run on the current revision compiles and passes CTest; sanitizer/fuzz/Windows/OpenBSD/Java runs remain separate.
- **M2:** Security sanitizer baseline clean or triaged.
- **M3:** Java 2.14.0 delta review (NET-026) complete; 2.13.0 matrices stay historical, and dynamic Java/peer evidence remains pending.
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
CMake install time. The 2026-10-22 continuation adds daemon-init failure exit
status, stop budgets aligned with the graceful drain, s6 down-signal/timeout
control files, systemd sysusers/tmpfiles policy, foreground OpenBSD rc.subr
supervision and exact service install modes; these remain static-only. The
latest packaging pass also separates TUI desktop assets, applies
dpkg-buildflags hardening, preserves administrator modes on upgrade, corrects
DEP-5/lintian/uscan metadata and adds an OpenBSD port/PLIST template. Debian
packages split the router/tools (`i2pd`) from the
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

The 2026-10-25 build continuation centralizes the native dialect contract,
shares the daemon source list with Android, installs the exported privsep
headers, makes static Boost.Container optional, installs component-aware
package configs and modernizes the presets/Android/package manifests. It
remains static-only: no configure, build, package, Gradle or device run.

The 2026-10-25 continuation closes the Streaming destination/Stream teardown
race and per-port registry race, the SSU2 introducer snapshot race, the
`TorrentFile` handle/authority race, SOCKS reply/input overlap, proxy handshake
and acceptor admission bounds, the I2PControl self-join path and the SAF
local-frame exception leak. Residual ownership/concurrency work remains for
SSU2 `m_Address`/callback publication and shared socket/executor ownership, the
live `RouterContext::GetRouterInfo()` readers, I2CP/SAM output sequencing,
BitTorrent `InitTorrentFiles` service-thread ownership, `RunnableService`
self-join and timer ownership, Streaming raw packet containers, Windows
pre-open filesystem TOCTOU, hard links and rename races. These are not claimed
fixed or dynamically validated.

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

## Selective upstream openssl synchronization (static, 2026-10-19/20)

Reviewed `PurpleI2P/i2pd:openssl` through
`9725b8b227293a796c069ac270fa6b5b47cf9a13` for commits dated 2026-10-18,
2026-10-19 and 2026-10-20 UTC (first-pass mark `de32c8c3`). The workstream applies the compatible SSU2,
I2PTunnel/HTTP, NetDb peer-policy, torrent-persistence and Intel AMT
reserved-port corrections, adapts the reseed sample wording without weakening
verification, and carries the applicable RPM Boost guards while preserving
C++23, privilege separation, native TUI/Android architecture and fork release
identity.

The follow-up continuation reviews `de32c8c3..9725b8b2` (six commits) and
re-verified on 2026-10-21 that `9725b8b2` is still the branch tip with no
later commits. It adapts the proxy error status classification with the
fork-only 431 header path and moves its mapping into a test-visible header;
ports the DHT rendezvous self-announce with a reserved local peer slot,
unannounce-on-stop, empty-bucket fallback and fork peer/seed/private guards;
and pins mixed-case/obs-fold HTTP tunnel header handling. The RPM release-spec
change is partially integrated/obsolete, the merge commit is merge-only, and
the Windows web-console packaging change is incompatible with the removed
browser UI.

Exit criteria: manual CI compiles the changed paths, `test-ssu2-preauth`,
`test-routercontext-port-range`, `test-netdb-store-lookup-hardening`,
`test-torrents-security`, `test-http-res`, `test-torrents-dht-interop`,
`test-i2ptunnel-http` and `build-system-static` pass, and the I2PTunnel
response/IRC lifetime, proxy status and torrent DHT rendezvous receive dynamic
coverage. An RPM lane must be added and validate the Fedora guards on a
disposable host; no RPM lane exists yet. Upstream 3.0.0 release metadata,
browser-console changes, obsolete GNU-make/Jammy packaging and the unvalidated
RHEL 9 toolset path remain deliberately excluded. Until then this is static-only
work.

## Selective upstream openssl synchronization (static, 2026-10-31)

Reviewed all 40 objects in `5bcf6101..ba38945e` against local baseline
`46738ddf`; per-SHA decisions and merge treatment are in
`docs/upstream-sync.md`. Three adaptations address pool destruction order,
stream reorder packet lifetime and proxy Referer privacy. Other torrent, DHT,
reseed, I2CP and lifecycle changes are already present or superseded by stricter
fork checks; Haiku, browser UI, legacy Makefiles and newer optional Boost
optimizations are not imported. Source review and diff checks only; manual CI
compilation and focused tests remain pending.

## I2P Java 2.14.0 functional parity continuation (static, 2026-10-31)

Read-only subagent reviews covered router/transports/netDb, client services,
administration, address-book/reseed and I2PSnark/Android against the pinned
official source and relevant specifications. `docs/FEATURE-PARITY.md` §18 and
`tests/interop/FIXTURES.md` §10b record exact source links and regression
scenarios. Corrected stale claims: subscription source edits and per-tunnel
start/stop already have typed TUI and Android surfaces; selected-file torrent
promotion is implemented statically.

Remaining substantive gaps are Proposal 112 feed `sig`/`oldsig` verification,
Java's `router.floodfillParticipant=auto` monitor, netDb go-away/pdrop/stale-RI
semantics, NTCP2 caps/options/socket ownership, manual URL/file reseed input,
durable Snark magnets and uploader/rate scheduling. Keep arbitrary reseed
paths/URLs and unsigned feed actions out of generic admin APIs. Android live
transit/bandwidth/share controls are implemented statically through dedicated
service-worker/JNI methods and the existing typed queue. No local build, Java
execution, test, device run or CI validation was performed; manual CI remains
pending.

## netDb and LeaseSet continuation (static, 2026-10-26)

A four-way audit of the LeaseSet family, DatabaseLookup reply keys and
DatabaseSearchReply, RouterInfo freshness/persistence and netDb
concurrency/floodfill closed the defects recorded in
`docs/INTEROPERABILITY.md`: direct-garlic DatabaseLookup admission and the
three-state DatabaseStore origin, transport-RI flood admission, bounded queues
and new-RI admission, durable identity-bound RouterInfo persistence with a
shutdown flush, generation-based replacement, non-creating profile selection,
MetaLeaseSet canonical-form rejection and strict-newer encrypted-LeaseSet
replies. Reply-key 0x12 stays fail-closed because Java 2.14.0 has no sender/KDF
and the spec derived form is TBD.

Deferred with tracking: client-side MetaLeaseSet resolution (I2P-016),
exploratory-pool router garlic forwarding, encrypted replies for our own
RI/EXPL lookups, probabilistic store pdrop/go-away RI, per-use stale-RI
expiry, throttle-map LRU eviction, `m_AcceptUpdates` ordering, asynchronous
reseed, local RouterInfo address COW (SEC-025) and netDb pool destruction
order.

Exit criteria: manual CI compiles the netDb/LeaseSet changes and the new
`test-leaseset-encrypted-freshness`, `test-netdb-clove-origin`,
`test-netdb-router-admission`, `test-netdb-persistence`, rebuilt Meta vectors
and extended netDb regressions pass; a controlled Java floodfill differential
then exercises DLM/DLM-garlic, DSM token policy, MetaLS2 serving and encrypted
LS replay. No dynamic equivalence is claimed before that run.

## Tunnel, I2NP and garlic continuation (static, 2026-10-26)

Four read-only audits of tunnel construction, tunnel pools/selection/timers,
I2NP framing/dispatch and garlic/ratchet clove handling closed the defects in
`docs/INTEROPERABILITY.md`: the unbounded hop count that wrote past the build
record table (a heap out-of-bounds write), fail-closed record encryption with
exact reply counts, transit build replay filtering with Java build expirations,
one peer-admission predicate for selection and reuse, pool build accounting and
a failure ladder, persisted tunnel tests with three strikes, the custom-selector
use-after-free, real far-end transport masks, a bounded tunnel queue with a
45 s fragment window, the Java dispatcher expiry/duplicate window, exact
DeliveryStatus/DataMessage lengths, and the garlic clove grammar, bounded
destination queue, per-session state serialization and the destination-session
lock inversion that the new state lock would otherwise create.

Garlic keys, decryption and tag state stay in the core process; no IPC verb was
added. Java B1/B2/B3 payload-size normalisation remains deferred rather than
approximated.

Deferred with tracking: NTCP2 established-session cap, 12-byte options and
establisher-thread socket ownership (NET-172), SSU2 socket sharing and
memory-pool lifetime after deferred teardown (SEC-025), local RouterInfo
address COW, `TorrentsTunnel::InitTorrentFiles` disk-domain mutation, and the
netDb residuals above.

Exit criteria: manual CI compiles the tunnel/I2NP/garlic changes and the ten new
regressions (`test-tunnel-build-records`,
`test-tunnel-peer-selector-lifetime`, `test-tunnelpool-build-backoff`,
`test-tunnelpool-test-timing`, `test-tunnelpool-reuse-policy`,
`test-transit-tunnel-build-replay`, `test-tunnel-endpoint-fragments`,
`test-tunnels-inbound-queue`, `test-i2np-dispatch-policy`,
`test-garlic-clove-policy`) plus the extended `test-tunnel-dsm-policy` and
`test-tunnelpool-zero-hop` pass, the `fuzz-i2np-policy` target builds and its
one-input smoke entry passes, and a controlled Java 2.14.0 differential then
exercises short-build creation, transit relaying, tunnel tests and garlic clove
dispatch. No dynamic equivalence is claimed before that run.

## Android multiprocess and lifecycle continuation (static, 2026-10-26)

Four audits (services/lifecycle, Binder/AIDL, JNI, SAF/storage) found a
graceful stop that could be lost on process death, a same-UID SAF broker that
exposed an unscoped export truncate and a generationless `retire()`, an empty
`NewString(nullptr,0)` path, a stop that could not cancel an in-flight start,
and no guard against a second router in the storage-broker process.

- `RouterRunState` persists phase/deadline/generation in `router_state` and
  reconciles on `onCreate`/`Restore`: an expired drain is terminal, a live drain
  re-arms the remaining wall-clock delay only after start, and corrupt deadlines
  clamp or fail closed. `onTaskRemoved` keeps the daemon; `onDestroy` removes
  callbacks already queued. SAF import/create/export progress and completion
  now carry the accepting service epoch and re-check it at main-handler delivery,
  including provider posts after the purge (`ANDROID-035`, Implemented / static,
  Pending CI). Provider cleanup can continue after retirement. Persisted
  transitions still need stale-write fencing (`ANDROID-033`). The notification
  channel is verified before foregrounding.
- AIDL v2: `handshake()` generation, `prepareExport`/`openExport(token)`,
  `retire(generation)`, owner `(uid,pid)` on every root/handle/operation/token,
  owner-scoped cancel/close/revoke, typed admission (no exceptions) and a
  two-stage watchdog (cancel at 15 s, process fence only at 30 s). The client
  handshakes/version-checks, recovers a stuck retire in 5 s and distinguishes
  busy/timeout/unavailable from authoritative failure. No descriptor crosses
  Binder.
- JNI: one-shot process role (`Router`/`StorageBroker`), non-null empty-string
  `NewString`, exception clearing, `ScopedLocalRef` in `buildTorrentMetadata`,
  native stop token published before any monitor with a distinct
  `start-canceled`, and no JNI from SAF bridge/payload destructors.
- Topology: storage already runs in `:torrent_storage` (same-UID containment,
  not privilege separation); networking cannot be split (INTERNET per UID);
  `isolatedProcess` loses INTERNET and SAF grants. The router/presentation split
  (`:i2pd_router` + `IRouterControl` AIDL) remains `ANDROID-026`.

Exit criteria: manual Android CI compiles the AIDL/JNI/Java changes and runs
`RouterRunStateTest`, `TorrentSafProtocolTest`, `test-android-daemon-lifecycle`
and the instrumentation broker test on a device/emulator, including
activity/process death, boot restart, broker death/rebind and stop-during-start.
No global Android separation is claimed.

## Windows multiprocess continuation (static, 2026-10-26)

Four audits (Named Pipes, local RPC/ALPC, tokens/AppContainer, Job
Objects/supervision) found that the Windows primitives had no production
consumer and no real network/filesystem boundary: the restricted token had no
restricting SIDs, the policy booleans were unused, per-operation authorization
was dead code, spawn identity was on `argv` with no nonce, and the service
could report STOPPED while the router ran.

- A Windows `--privsep-worker` entry connects with `ConnectBound`, sends a
  32-byte CSPRNG nonce in the new `WorkerHello` (protocol v5), and binds its
  role only after the server impersonates the client and checks user SID,
  PID/session, AppContainer SID and nonce. `WindowsChannel` enforces the shared
  registry per bound role and answers typed `Error`s under the violation
  budget. A completed-during-cancel record is delivered, never discarded.
- `WindowsIsolation.cpp` is the single role authority. Storage/log/crypto/admin
  are low-privilege AppContainers with no `internetClient`; network has
  `internetClient`/`privateNetworkClientServer` and no write root; the supervisor
  uses a restricted token. A role with an Internet capability and a write root
  is rejected. The pipe DACL includes only the service user, SYSTEM and the
  exact container SID; the delegated root is granted to the container SID.
- Spawn uses `CREATE_SUSPENDED|CREATE_NO_WINDOW|EXTENDED_STARTUPINFO_PRESENT`,
  `bInheritHandles=FALSE`, an empty handle list, `PROC_THREAD_ATTRIBUTE_JOB_LIST`
  with a suspended fallback, `SECURITY_CAPABILITIES` + the LPAC opt-out for
  containers, Job Object limits/mitigations, and `TerminateJobObject` on stop.
- `WindowsWorkerSupervisor` owns one role, authenticates each generation,
  restarts under the shared budget with a monotonic generation and publishes
  `SupervisorStatus`. The service drains `Daemon.stop()` with non-zero wait
  hints instead of reporting STOPPED early.
- `ncalrpc` is documented as rejected for now: the worker→core direction would
  require a second core-hosted interface, MIDL/stub generation on two lanes and
  a second cancellation model, while pipes already enforce the shared frames.
  No undocumented ALPC is used.

Deferred (`WIN-013`): start the supervisor from the daemon/service, pass the
delegated root, surface status in TUI/Android, and implement the real Windows
log/storage/network role loops. Exit criteria: manual Windows CI compiles the
new code and passes `test-privsep-windows-ipc`,
`test-privsep-windows-isolation` and `test-privsep-windows-supervisor`, and a
service-account lane proves the stop budget and AppContainer denial. No global
separation is claimed.

## Linux backend hardening continuation (static, 2026-10-26)

Four audits of the storage worker, AF_UNIX/seqpacket IPC, credentials and
descriptors, and supervision/namespaces found two P0 defects: the per-frame
`SCM_CREDENTIALS` check compared against the socket creator's `SO_PEERCRED`,
so a root core that dropped a worker to `privsep.user` rejected every reply;
and the capability teardown ran inside the already-unprivileged worker, making
it a silent no-op. The storage seccomp filter also allowed `connect`/`execve`/
`fork`, and the network namespace was never installed.

- Linux channels carry an explicit `PeerExpectation` (uid/gid/pid, root-parent
  allowance) set by the spawner. The core binds the direct child pid; a worker
  accepts its own uid or uid 0; an adopted broker socket is uid-bound and must
  have an empty receive queue.
- The spawn child drops ambient/bounding capabilities while privileged, drops
  gid/uid, then calls `capset(0)` and verifies with `capget` before `execve`;
  the worker re-asserts
  `no_new_privs` and an empty capability set. Ignored signals are reset.
- Storage seccomp denies the connection lifecycle, `sendto`, `execve*` and
  process creation; Landlock handles `EXECUTE` without granting it, restricts
  reads and writes to the delegated root, and the socket/capability lockdown is
  installed before the configure frame. A best-effort empty network namespace
  is created pre-exec while the core still has `CAP_SYS_ADMIN`.
- The frozen executable is re-checked by dev/inode/size before `execve`; only
  `*-inherited` roles and the broker accept a root, and fd 4 is closed
  otherwise.

Deferred with tracking (`PRIVSEP-031`): cgroup-v2 per-worker limits, a global
admission cap, `SupervisorStatus` over IPC, unified restart/stop budgets,
broker reaping, a deny-all log socket profile and worker-side core-pid binding.
Exit criteria: manual Linux CI compiles the changes and passes the extended
`test-privsep-linux-ipc` and `test-privsep-linux-sandbox`, plus
`test-privsep-worker-identity`, `test-privsep-credentials` and the storage
clients. No global privilege separation is claimed.

## OpenBSD supervised-start continuation (static, 2026-10-26)

Four audits of the storage worker/broker, process startup and credentials, IPC
and pledge/unveil found four P0 defects: the startup pledge lacked `recvfd` (so
the runtime promise was silently ignored), the supervisor could `exec` without
unveil, no credential drop existed, and the executable was resolved through
`$PATH` with a `/proc` fallback. This increment closes them.

- The supervisor and storage broker pre-resolve their own executable, unveil
  exactly that path with `x`, lock the list and pledge `stdio proc exec` /
  `stdio proc exec sendfd`: no `unix`, no path read/write, no descriptor
  reception and no network. `execve` reaches only the frozen image; the new
  image installs its own policy after exec (no `execpromises`, no inherited
  unveil assumption).
- The core startup/runtime pledge sets come from one table; startup adds
  `recvfd` and `id` when needed, the runtime set must be a strict subset, and
  the pledge file is a bounded subset selector (comments/blank lines and
  dangerous runtime promises rejected).
- `privsep.user`/`privsep.group` select the worker identity. The child clears
  supplementary groups, drops gid/uid and verifies the result before exec; a
  root core with isolation and no identity fails startup. The 25-byte hello
  binds role, nonce and post-drop uid/gid; the executable path is frozen from
  `argv[0]`; descriptors 0/1/2 are `/dev/null`, 3 channel, 4 root, 5 hello,
  >=6 closed.
- Peer and root checks understand a root core delegating an unprivileged
  worker: the root must be owned by the worker uid, and a worker accepts uid 0
  only as its spawner. The clients reject a root core without a configured
  identity instead of degrading silently.

Deferred with tracking (`OPENBSD-SBX-11`): a generalized broker for
router/data respawn with new protocol types, and moving the TUI/I2PControl
sockets out of any storage-unveiled root. Exit criteria: manual OpenBSD CI
compiles the changes and passes `test-privsep-openbsd-policy`,
`test-privsep-credentials`, `test-privsep-worker-identity`,
`test-privsep-openbsd-imsg`, the storage tests and the Linux regressions, plus
the fork-based negative probes (no socket, no fork, no `execve("/bin/sh")`,
unveil lock, denied path). No global privilege separation is claimed.

## IPC consolidation continuation (static, 2026-10-26)

Four read-only audits of the internal protocol (serialization and registry,
channel/recovery, per-role authorization, platform backends and fuzz coverage)
drove this increment. The protocol keeps `SOCK_SEQPACKET` and the native
platform mechanisms; it removes the duplicated policy and makes the contract
table-driven.

- One `MessageSpec` registry in `privsep/Protocol.h` is now the only authority
  for a type's minimum version, direction, receiving roles, payload ceiling and
  descriptor policy. The hand-written role switch is gone; `AllowsOperation`,
  encoder and decoder all read the table, and a compile-time check requires a
  contiguous, bounded table.
- Reply direction is explicit (`EndpointRole::Core`): a worker cannot receive a
  reply and the requesting side cannot receive a request. `Error`, `Pong` and
  `TorrentStorageSupervisorReply` are the only replies a non-core endpoint may
  receive, because the supervisor/broker issue their own requests.
- Version 4 adds the advisory `Cancel`; versioning is per type, so a newer type
  in an older header is refused rather than reinterpreted. Per-type ceilings
  replace the single 64 KiB check.
- Serving endpoints enforce the descriptor policy (presence and kind), answer
  an attributable frame fault with a typed `Error` correlated by request id and
  tolerate at most eight such faults before exit. A wrong-role or malformed
  frame no longer kills a worker; credential/ancillary failures remain fatal.
- Windows receive no longer closes the pipe on an idle timeout, applies the
  same attributable-fault rule and delegates role policy to the shared
  registry. A pre-opened log descriptor is bound to the configured path by
  identity, owner and link count.
- `Cancel` is advisory: synchronous storage workers cannot roll back an
  applied mutation, so the client drains the reply and reports failure instead
  of replaying; a not-yet-run matching request is suppressed.

Exit criteria: manual CI compiles the protocol/channel/worker changes and the
new or extended regressions (`test-privsep-authorization`,
`test-privsep-protocol`, `test-datastorage-protocol`, `test-privsep-log`,
`test-privsep-supervisor`, `test-privsep-linux-ipc`, OpenBSD imsg and Windows
pipe tests) pass, and the manual fuzz workflow runs `fuzz-privsep-protocol`
against the consolidated decoders. Deferred with tracking (`PRIVSEP-023..026`):
an explicit version-negotiation handshake, imsg chunking above 16 KiB, the
`ConfigureTorrentStorage` dual schema, a per-request cancel table, and Windows
worker wiring.

## Privilege-separation continuation (static, 2026-10-26)

Audits of the supervisor, storage workers, network path, crypto/log workers and
the core's write authority showed that every worker is already network-free but
that the router core is the only process still combining external sockets with
persistent writes. This increment makes the invariant explicit and enforceable
and moves the first hash-named runtime artifacts out of the core.

- `privsep/Capabilities.h` declares the role×capability matrix and the rule that
  no role may combine external network with persistent writes, with the target
  core row (no write) and the explicit legacy row (write) so the gap is visible.
- A worker role is now bound to its spawner by a CSPRNG nonce delivered on a
  dedicated pipe (fd 5), never on `argv`, so a same-UID process cannot adopt a
  worker role with its own socketpair.
- `privsep.artifacts` delegates the netDb RouterInfo cache, peer profiles and
  destination private keys to a data-storage worker with no network capability,
  canonical-name validation, staging slots and an atomic no-follow replace. The
  installed writer is the only path; failures never fall back to the filesystem
  and an unavailable worker fails closed.
- Storage clients are restartable and generation-fenced, ambiguous writes are
  never replayed, `router.info`/fixed-file save failures retain the last buffer,
  and the supervisor publishes a bounded status with a single restart budget.
- The log worker sanitises C1/invalid UTF-8, redacts known secrets, caps its
  file sink at 64 MiB without rotation and no longer holds the logging mutex
  across a send.

Deferred with tracking: the network worker (listener/connect handoff then full
transport), the crypto signing worker (fallible `Signer::Sign` first), the
direct startup/read paths, garlic tags/address book/torrent/bootstrap keys, a
core write-denying Landlock profile, OpenBSD post-pledge respawn, Windows
`--privsep-worker` and `WSADuplicateSocketW`, and broker subtree registration.

Exit criteria: manual CI compiles the privsep changes and the new regressions
(`test-privsep-worker-identity`, `test-datastorage-{protocol,worker,client}`,
`test-artifact-storage`, `test-privsep-capabilities`,
`test-privsep-supervisor`, extended `test-privsep-log`,
`test-netdb-persistence`, `test-router-profile-storage`) passes, the Linux and
OpenBSD negative capability tests run, and a kill/restart of the data worker
leaves no partial artifact and bumps the generation. No global privilege
separation or dynamic capability claim is made before that run.

## Application-services continuation (static, 2026-10-26)

Four read-only audits of the HTTP/SOCKS proxies, client/server tunnels,
reseeding/naming/address book and the auxiliary services closed the defects in
`docs/INTEROPERABILITY.md`: reflected Host and outproxy-credential disclosure in
proxy pages, non-http scheme forwarding, unsanitized proxied responses, missing
Digest and upstream deadlines, unbounded SOCKS RESOLVE, server-tunnel
connection leaks on stop, the absent `filterDefinition` and pre-accept
rejection seam, missing `targetForPort`/`spoofedHost`/`reject*`/POST/startOnLoad
and per-tunnel control, the reseed peer-wipe/blocking/no-deadline lifecycle,
the non-atomic address book with ignored feed retractions, and the
BOB/I2PControl/NTP/UPnP trust gaps.

Administration stays native and typed: `ADMIN TUNNEL-START/TUNNEL-STOP`,
`ADMIN ADDRESS-REPLACE` (plain add answers `exists`), a non-blocking
`ADMIN RESEED` with phase/last-error fields, `tunnel.service` rows, TUI
screens/actions and Android JNI/screens for tunnels, reseed, add-vs-replace,
subscriptions and NetDb bans. No web console, no generic config writer.

Deferred with tracking: HTTP keep-alive/pipelining, IRC `ircclient`/DCC,
UDP/torrent tunnels in the start/stop registry, Streaming `limitAction`
`http`/custom, synchronous bootstrap reseed, UPnP status in the UIs, Windows
local-socket peer credentials, and the es/ru/zh-Hans entries for the new TUI
strings (`I18N-004`).

Exit criteria: manual CI compiles the application-service changes and the new
or extended regressions (`test-http-proxy-limits`, `test-proxy-auth`,
`test-socks-resolve`, `test-i2ptunnel-{stop,filter,targets}`,
`test-streaming-limitaction`, the four `test-reseed-*` additions,
`test-addressbook-{actions,cache,persistence}`, `test-i2pcontrol-{json,socket}`,
`test-bob-command`, `test-upnp-mapping`, extended `test-ntp-timesync-lifecycle`
and `test-tui-protocol`, `RuntimeAdminPolicyTest`/`RuntimeAdminParityTest`)
passes, and a controlled Java 2.14.0 run then exercises Digest/Basic proxy auth,
scheme rejection, server-tunnel filters and `targetForPort`, reseed status,
address feed retraction, I2PControl error objects and NTP/UPnP behaviour. No
dynamic equivalence is claimed before that run.

## SAM, I2CP, streaming and datagram continuation (static, 2026-10-26)

Four read-only audits of the SAM bridge, I2CP client, streaming state machine
and Datagram1/2/3 closed the defects in `docs/INTEROPERABILITY.md`: the SAM
receive-port conflation that made `RAW RECEIVED`/`DATAGRAM RECEIVED` silent,
private-key/credential logging, the missing control-plane bounds and HELLO
deadline, ACCEPT-during-FORWARD hangs, the 2.14.0 per-style subsession
protocol, b33 resolution, BlindingInfo consumption with Java HostReply codes,
I2CP admission and async read deadlines, LeaseSet create/send validation, the
`AcceptStream(timeout)` use-after-free, duplicate-SYN resurrection and silent
SYN drops, per-send datagram framing, receiver protocol isolation and the
datagram parser/session races.

Key interoperability decisions: `PORT` is only the local forward socket and the
I2CP receive port is `LISTEN_PORT` (default PORT_ANY); an established SAM
control connection is never closed for idleness (Java disables `SO_TIMEOUT`
after HELLO); datagram framing is chosen per send; receivers are keyed by
`(protocol, port)` with no port-to-wildcard promotion; BlindingInfo auth
material is a strict 32-byte secret used only for encrypted-LeaseSet
decryption.

Deferred with tracking: streaming TIME-WAIT/FIN retention,
SYNACK-before-LeaseSet ordering, `limitAction` responses, CLS1 non-DSA key
installation, day-scoped BlindingInfo matching, 33–61 KiB datagrams versus the
local 32 KiB bound, and `TorrentsTunnel`'s shared session-version mutation.

Exit criteria: manual CI compiles the client-service changes and the new or
extended regressions (`test-sam-control-framing`, `test-sam-subsessions`,
`test-sam-version.sh`, `test-i2cp-b33-lookup`, `test-i2cp-blinding-info`,
`test-i2cp-session-lifecycle`, the five `test-streaming-*` additions,
`test-datagram-dispatch`, `test-datagram-version-dispatch`,
`test-datagram-receiver-lifecycle`, `test-sam-datagram-framing`) passes, and a
controlled Java 2.14.0 run then exercises SAM RAW/DATAGRAM receive ports,
concurrent ACCEPT/FORWARD, b33 with and without BlindingInfo, HostReply codes,
streaming close/SYN replay and mixed DATAGRAM/DATAGRAM2/DATAGRAM3 subsessions.
No dynamic equivalence is claimed before that run.

## Cryptography modernization continuation (static, 2026-10-26)

A four-way audit of the ratchet, Elligator2/X25519, ML-KEM/provider
negotiation and signatures/LeaseSet2 against Java 2.14.0 closed the defects in
`docs/INTEROPERABILITY.md`: NSR split promotion by the first inbound tag,
tag-exhaustion restart, strict NSR payloads, the 320-tag window, X25519
key-generation failure propagation and ownership, Ed25519 secret cleansing and
canonical-`S` verification, b33 two-byte rejection, RSA RouterInfo rejection,
strict `leaseSetEncType` and inert unknown LS2 keys, a real LibreSSL ML-KEM
round trip, fallible hybrid Noise initialisers, and PQ downgrade resistance in
SSU2/NTCP2.

Deferred with tracking: Java type-5/7 NSR vectors (CRY-206..214), fallible
`Signer::Sign` and LeaseSet2 self-verification (CRY-236), capacity-wide secret
cleansing (CRY-237), adaptive ratchet/ElGamal ordering and multi-NS admission
(CRY-238), variable-time provider-free Ed25519 signing (documented), and
LibreSSL/restricted-provider lanes.

Exit criteria: manual CI compiles the crypto changes and the extended
`test-eciesx25519-*`, `test-crypto-*`, `test-eddsa`, `test-blinding-*`,
`test-leaseset2-*`, `test-routerinfo-rsa-reject`, `test-ssu2-preauth` and
`test-ntcp2-*` regressions pass; then the controlled Java differential harness
exchanges NS/NSR/ES for type 4 and 5/7 with fixed keys. No dynamic equivalence
is claimed before that run.

## NTCP2/SSU2 hardening continuation (static, 2026-10-26)

A four-way audit against the NTCP2/SSU2 specifications and Java 2.14.0 closed
the transport security and conformance defects recorded in
`docs/INTEROPERABILITY.md`: the ntcp2-hybrid SessionCreated padding table, the
data-phase AEAD oracle drain and intra-frame deadline, clock-skew sequencing,
netID bans/reason 17, the RouterInfo flood flag, a HolePunch block whitelist,
PeerTest address provenance and msg-4 target validation, PeerTest/inbound
admission caps, ACK-0 completion, 120 s handshake skew, out-of-session version
acceptance, single-fragment RouterInfo, a reachable PQ version fallback,
mutex-guarded session addresses, slab-charged fragment budgets, relay
tag/role/expiry invariants and the 1488-byte IPv6 MTU cap.

Deferred with tracking: NTCP2 established-session admission, 12-byte options
negotiation, establisher-thread socket ownership and an undeadlined length read
(NET-172); SSU2 socket sharing, memory-pool lifetime after deferred teardown,
peer-test proximity checks and finer cleanup timers.

Exit criteria: manual CI compiles both transports and the new
`test-ntcp2-*`/`test-ssu2-*` regressions pass; the controlled differential
harness then exercises session establishment with and without ML-KEM,
HolePunch/PeerTest/relay sequences, ACK/reassembly boundaries and MTU/IPv6
paths. No dynamic equivalence is claimed before that run.

## Feature-parity audit continuation (static, 2026-10-25)

A four-way read-only audit against Java 2.14.0 closed the largest router
security gap and the administrative gaps that were still reachable only through
the removed web console.

The operator blocklist is now a bounded, immutable startup snapshot enforcing
IPv4 ranges/CIDR and router hashes at SSU2/NTCP2 admission, outbound peer
creation, netDb store/selection and `IsRouterBanned`; runtime bans are
memory-only and bounded. NetDb answers only three lookups per reply tunnel when
not a floodfill and drops EXPL/ANY there before charging the throttle.
Address-book subscription merges can no longer re-point a locally curated name,
and subscription sources can be listed and edited through typed TUI verbs.

I2PSnark now reports whole-torrent tracker `left` and only announces
`event=completed` after every piece is verified, floors tracker-advertised
announce intervals at 15 minutes and caps the internal failure ladder at 30.

Deferred with explicit reasons: server-tunnel `filterDefinition` (needs a
streaming-level pre-accept reject seam shared with `limitAction`), HTTP Digest,
`targetForPort`/`spoofedHost`/user-agent rejection, per-tunnel runtime
go-away RouterInfo/store-cap/stale-RI
netDb work, and Snark session rate limits/uploader slots/durable magnets.

Exit criteria: manual CI compiles the new blocklist, netDb, address-book, TUI
and torrent paths; `test-blocklist-parse`, `test-blocklist-enforce`,
`test-addressbook-merge`, `test-addressbook-admin`, `test-torrents-tracker-schedule`,
`test-torrents-priorities`, `test-torrents-verify-parity`, `test-tui-protocol`,
`test-netdb-stop-admission` and `control-security-static` pass; a live
floodfill/peer and tracker differential follows. No web console, JVM, plugin or
generic configuration writer returns. Until then this is static-only work.

## Upstream openssl continuation 9725b8b2..5bcf6101 (static, 2026-10-24)

`PurpleI2P/i2pd:openssl` moved from `9725b8b2` to
`5bcf6101a456dc991e44d3d79cb69994c7bae484`; fifteen commits are in the window
and only seven files change (+128/-43): `Makefile.linux`, a Debian UPnP quilt
patch, `libi2pd/Signature.cpp` and the `libi2pd_client/Torrents*` sources. No
SSU2/NTCP2/I2NP/netDb/Garlic, dependency-floor or wire-format change is
involved.

Integrated torrent corrections: BEP 3 `cancel` handling that removes only a
queued request and keeps the fork's BEP 6 reject gate; *not interested* on
torrent completion and only when the peer still advertises wanted pieces;
per-block `applied` gating so duplicate/unsolicited blocks cannot trigger a
re-hash, disk rewrite, invalidation or download accounting; a filesystem error
state published atomically from the disk worker when part files cannot be
created; BEP 15's default datagram-tracker port 6969 applied at the use site
so the stored announce URL identity is preserved; `SendBuffer`-based piece
sending that removes the extra payload copy; files closed on stop before the
resume snapshot; and rate resets on choke/remote-not-interested.

Already equivalent and deliberately not re-applied: `b31431ba` (the fork checks
DSA signing results and zeroes the signature on failure, and guards the
LibreSSL `DSA_do_sign` NULL path) and `5bcf6101` (`Piece::Dump` returns failure
and the caller invalidates the piece). Omitted: `14261bb7` (GNU make; the fork
has no `Makefile.linux`) and `b1a30e99` (the Debian quilt series was removed
when packaging moved to CMake components). Merge commits carry no independent
tree change.

Exit criteria: manual CI compiles the torrent paths and `test-torrents-*`,
`test-torrent-bencode` and `fuzz-torrents-parsers` pass; a live peer must
exercise cancel, completion and tracker defaults. `Signer::Sign` remains a
`void` interface, so all-zero signature publication is still planned rather
than fixed. No commit, push or workflow dispatch happened for this window.

## Intensive native torrent continuation (2026-10-19)

Status: implemented by static analysis for scoped interfaces and operations;
pending CI compilation; pending CI tests. The starting checkout was clean at
`0cf32519`. Recognition reused the existing disk creator, priorities, comment
exchange and native Android controls rather than reimplementing them.

New source behavior includes advanced disk creation with automatic/explicit
piece sizes, tracker tiers/privacy, cancellation and progress; complete desktop
priority/comment controls; SAF document/tree creation and export through native
metadata construction; and opt-in exec-ed Linux payload storage with
bounded typed IPC and negative capability regressions. Empty bencode strings
and canonical dictionary ordering were corrected, preserving imported info
bytes. Native settings loading and paused announces gained additional guards.

The next essential blocks remain URI-backed SAF download/seeding, durable
provider permission/operation recovery, individual selected-file promotion,
complete queue/bandwidth controls, BEP 12 failover/registry cleanup, Windows and
Android storage processes, and network/session/verifier/supervisor integration.
Parent-side persistent operations must be removed before the global capability
invariant can be claimed. None of these obligations is closed by source review
or by earlier-revision CI evidence. See SNARK-001..SNARK-010 and the updated
functional/protocol matrices for exact boundaries.

### Native payload continuation — 2026-10-20

Implemented by static analysis; pending CI compilation and CI tests: common
C++23 payload capability interface, SAF Binder/JNI piece IO and final-name
provider storage, startup rehash, persistent tree selection, bounded fail-closed
provider access, and pending-write publication guard. Native isolated allocation
is delegated to storage. OpenBSD adds pre-pledge supervision, inherited root
and socket-only handoff without runtime proc/exec. Prepared native/OpenBSD and
Android regressions need manual CI/device execution.

Next: provider cancellation/fixtures, transparent broker recovery, detailed
storage errors, SAF deletion, Windows backend, parent metadata/promotion/router
writes, and genuine network/core/verification separation. Keep all unfinished
SNARK and non-torrent modernization tasks open.

### Binder recovery and native persistent authority

Implemented by static analysis; pending CI compilation/tests: stable SAF local
capabilities, generation-aware reauthorization, actual-death fencing, bounded
reconnect/backoff, independent cancellation and retirement of blocked brokers.
Prepared bounded provider fault fixtures. Writes with uncertain results are
invalidated rather than replayed. Isolated native torrent promotion/removal and
metainfo/resume/settings writes now execute in storage workers via typed bounded
operations. Global separation remains in implementation: router/DHT writes and
network/router/verifier process integration are still required.

### DHT/router state and SAF completion increment

Implemented by static analysis: isolated DHT state callbacks and worker reads,
legacy-name authorization, optional fixed-router persistence worker initialized
before key generation, SAF exact-document removal and cancelable brokered
export. CMake/manual CI sources and regressions are prepared. Network/router/
verifier isolation and other persistent authorities remain implementation work;
no global completion or validation claim is made.

## Comprehensive Java parity continuation (2026-10-20)

Historical reference for that pass: `i2p-2.13.0` at
`9134f808337b401e8e53c73734c81fab04280c9d`. Current target is 2.14.0;
NET-026 tracks the pending review. The full functional/administrative matrix is
[`docs/FEATURE-PARITY.md`](docs/FEATURE-PARITY.md); wire evidence remains in
[`docs/INTEROPERABILITY.md`](docs/INTEROPERABILITY.md).

Status: implemented by static analysis for the scoped increment; pending CI
compilation; pending CI tests. No local build/test, Java peer, Android device,
sanitizer/fuzzer, OpenBSD/Windows or live-network run exists.

Implemented source changes: SSU2 migration retransmission/deadline and old
endpoint retention; runtime ML-KEM provider probing; RSA/Ed25519 SU3 and RSA
identity verification with a fail-closed reseed default; SAM persistent bounded
framing/pipelining and control grammar; I2CP identity-hash routing keys;
case-preserving address-book normalization; torrent resume/final-name
verification, tracker forms/limits, private announce, PEX and connection caps;
and a typed daemon-owned TUI admin queue for reseed, shutdown/cancel, tunnel
reload, bandwidth, share, transit and log level. Android reseed and runtime
log-level parity is implemented by static analysis with service-worker JNI,
Material 3 confirmation UI and a static instrumentation consistency test.

Remaining work is not relabelled as only testing: native tunnel/address-book
editing, selected-file torrent promotion, durable queue/bandwidth scheduling,
secret-blinded I2CP, Android SAF recovery and genuine network/router/verifier
separation remain implementation tasks. Every changed path needs manual CI,
differential Java, device and platform validation before a parity or
privilege-separation claim is made.

## Client and persistence hardening (2026-10-21)

Implemented by static analysis; pending CI compilation and pending CI tests:
UPSTREAM-003 extracts and integrates bounded incremental HTTP header parsing
with independent output ownership; UPSTREAM-004 revokes removed torrent
instances, orders cleanup before replacement and deletes native settings in
both storage modes; SEC-015 binds datagram naming responses to the expected
authenticated resolver and bounds pending requests.

Prepared tests cover fragmentation/byte limits, resolver identity/replay/
expiry, removal during disk IO, failed cleanup and re-add ordering. The
existing HTTP fuzzer now compares whole versus fragmented tunnel delivery.
The manual Linux job selects these regressions explicitly; existing full
CTest, sanitizer, OpenBSD and Windows jobs consume their registrations.

Exit criteria remain manual CI compilation/tests, sanitizer execution,
controlled HTTP/datagram exchanges and real shutdown/crash storage tests.
The shutdown-final-snapshot correction has no dedicated regression yet.
Windows reparse-point races, direct payload deletion TOCTOU, full native
administration, durable scheduling, SAF/device recovery and actual network/
router/verifier process separation remain independent implementation work.

## Java 2.14.0 comparison transition (2026-10-21)

Current reference: `i2p-2.14.0`, peeled commit
`3c98503c5f14efe7bdd783528ba7f621f12d8081`. Reference docs, skills and
CI checker configuration are implemented by static analysis, pending CI tests.
The fixture corpus remains explicitly pinned to historical 2.13.0 provenance,
with a separate optional CMake checkout and manual CI check.

NET-026 remains in implementation. Exit criteria: read-only review of the
2.13.0-to-2.14.0 source delta for router/crypto/client/Snark behavior, updated
matrices and properly sourced vectors, justified native corrections, manual
CI compilation/tests and controlled 2.14.0 peer exchanges. Existing 2.13.0
audit evidence must not be presented as a completed 2.14.0 comparison.

## String safety and libc compatibility (static, 2026-10-21)

The first-party C++23 tree no longer calls `sprintf`, `strcat`, `strcpy`,
`strncpy` or their unbounded variants. Address and endpoint diagnostics use a
project-owned bounded formatter rather than Boost.Asio's scope-ID
`sprintf`/`strcat` path; `BOOST_ASIO_HAS_SNPRINTF=1` selects Asio's bounded
`snprintf` fallback for scope IDs while project formatting avoids the
remaining `strcat` path. Fixed platform buffers use the
internal `CopyString` helper. The supported platforms guarantee `snprintf`,
`inet_ntop` and `if_indextoname`, so no `strlcpy`/`strlcat` libc shim is
introduced.

Prepared regressions cover IPv4/IPv6/scope-ID/endpoint formatting, small and
zero-capacity buffers, embedded NUL bytes, i18n formatting, and SOCKS domain
length/overflow boundaries. `build-system-static` rejects the unsafe call
family and requires the Asio definition.

Exit criteria: manual CI compilation on Linux/OpenBSD/Windows/Android, full
CTest, ASan+UBSan/TSan, fuzzing and the new regressions pass. Until then this
is static-only work; no diagnostic removal or validation is claimed.

## Functional parity continuation (static, 2026-10-21)

A scoped 2.14.0 read-only review covered router/tunnel policy, address book,
client services, administration, proxies and I2PSnark. The implemented native
surface now includes:

- address-book list/add/replace/remove/block/unblock through the bounded TUI
  IPC and an app-internal Android screen, with deleted-host tombstones and
  full base64 identities only;
- I2PSnark force recheck, metainfo comment/created-by editing, tracker/metadata
  preservation and Android remove-with-data through the bounded control path;
- Java-compatible rejection of token-bearing DatabaseStore messages that arrive
  through an exploratory tunnel, before any DeliveryStatus reply is built.

Prepared regressions: `test-addressbook-admin` (tombstones),
`test-torrents-engine-interop` (metainfo preservation/edit),
`test-tunnel-dsm-policy`, `TORRENT-RECHECK`/`TORRENT-META-*` protocol parsing
and Android `TorrentControl` command tests. Tunnel creation/editing,
subscription editing, selected-file promotion, uploader/bandwidth scheduling,
the §10 router/client/proxy candidates, live Java lookup and
device/instrumentation execution remain implementation or validation work.

Exit criteria: manual CI compilation/tests on Linux/OpenBSD/Windows/Android,
live Java 2.14.0 lookup/tracker/tunnel checks and device tests pass. No parity
or dynamic interoperability is claimed before then.


## Agent Skills system (static)

Status: implemented by static analysis. The 2026-10-26 continuation (GOV-009)
re-synchronized all 17 skills with the tree after six read-only audits: protocol
v5/`Pong` semantics and the log-fd sink, `WorkerHello`, NTCP2 Request/Created
padding, SSU2 fragment caps, the 15-fuzzer/corpus/sanitizer inventory,
CTest/workflow registration, service stop budgets, packaging payload rules,
Android bridge/SAF facts, plural arity, torrent verb boundaries and upstream
pin scope. Manual CI, documentation and branding validators remain pending.

## Modern language utilization policy (2026-10-22, static)

Status: implemented by static analysis. Modern C++23/Java 25 facilities and
well-scoped OOP/generic abstractions are now a permanent project objective in
`AGENTS.md`, with availability limits for GCC 13/Clang 17/OpenBSD libc++ and
Android minSdk 29. Current increments are a compile-time Identity registry,
`std::jthread`/stop-token X25519 ownership, an outbound transport Strategy
interface, Java sealed router commands/pattern switches and a typed log level.
Compilation, CTest, Gradle, sanitizer, OpenBSD/Windows and device validation
remain pending manual CI.

## Agent knowledge maintenance (static)

AGENTS.md now requires every significant change to review and, when needed,
update the owning Agent Skills and technical/tracking documents. It also makes
non-duplication permanent: a skill links the single policy owner instead of
restating a normative rule, another skill's value or a generated list. `GOV-008`
owns the policy/checker/doc coupling and `GOV-009` the 2026-10-26 audit pass;
`agent-skills-static` enforces the `Known issues and decisions` section and now
pins the knowledge-review/non-duplication phrases, while
`language-modernization-static` pins the policy, language levels and
modernization seams. Dynamic validation remains pending manual CI.

## UI localization (en/es/ru/zh-Hans, 2026-10-22, static)

Native and Android UI catalogs are implemented by static analysis. Exit
criteria: four complete catalogs, locale/fallback selection, per-app Android
language, placeholder/plural parity checks, UTF-8/column-width tests and
manual Linux/OpenBSD/Windows/Android evidence. Full TUI help/tool translation
coverage and dynamic validation remain pending; see `I18N-001..I18N-003`.

The static lifecycle increment implements Datagram receiver registration
identity and draining (CONC-011), stopped tunnel/netDb queue admission and
throttle reset (CONC-012), and RouterService owner fencing (ANDROID-019).
TOOL-013 adds maintenance-shell coverage to manual packaging CI and fails SAM
version checks on mismatches. Compilation/tests, real owner teardown and
platform restarts remain pending; no local execution or publication occurred.

## Static compile/link audit (2026-10-25, static)

Exit criteria for TEST-024: every confirmed compile/link defect found by the
six read-only audits is corrected or explicitly recorded; the warning set from
the last completed manual run is removed; CMake capability probes and the
Windows RC/TUI/OpenBSD paths agree with the source; and the residual
cross-thread, packaging, Android dexing and PARITY-014 items are tracked with
their required dynamic evidence. Compilation and tests remain manual-CI-only;
no local toolchain invocation is evidence for this increment.

## Full Java 2.14.0 equivalence program (2026-10-25, static)

The ordered workstreams are recorded in `docs/FEATURE-PARITY.md` §12:
Streaming admission and `limitAction`; SAM 3.3 subsessions; local proxy
authentication; server-tunnel filters; operator blocklist; NetDb admission and
stale refresh; per-tunnel administration; I2PSnark promotion/queue/bandwidth;
and process separation. Each workstream is completed as a whole (including its
wire response and prepared regression) and is not called equivalent until the
manual Java/native differential harness passes. Reseed `?netid=` and
two-server/100-RI acceptance and the Streaming per-peer/total
minute/hour/day counters are already implemented statically; Streaming
`limitAction`, rejection packet bytes and all other workstreams remain open.

## Native I2PSnark scheduler, tracker and privacy continuation (2026-10-26, static)

Four read-only audits covered engine download/seeding, creation/magnets/
metadata, trackers/DHT/PEX and piece planning/resume/persistence/IPC. The
implemented slices keep the native engine I2P-only and change no peer-wire or
metainfo format:

- transfer scheduler: 60 s per-request expiry before the 95 s blanket release,
  real block-length accounting, idle peer-rate decay, verified-only per-file
  progress, `eTorrentErrorFilesystem` on a failed write (cleared by a later
  verified write) and bounded best-effort corrupt-peer charging;
- trackers: dispatch no longer overwrites the failure ladder, HTTP announces
  are single-flight per `(torrent, slot)` with a shared 8 s write/read deadline,
  and registry slots are reference counted/reused without renumbering;
- DHT privacy: metadata that reveals a private swarm purges peers, our
  rendezvous identity, tokens and outstanding queries, and later inbound
  queries for that hash are refused;
- control: typed `TORRENT-REMOVE-ALL`, a restored `COMMENT-UNHIDE` parse branch,
  TUI confirmations, Android session removal and single-torrent start/stop, and
  the Android creation-tracker validator no longer requires an engine id.

Exit criteria: manual torrent CI registers and passes `test-torrents-scheduler`,
`test-torrents-tracker-dispatch`, the extended `test-tui-protocol` and the
existing torrent suite; Android CI compiles the JNI allowlist and Java actions;
a controlled Java 2.14.0 peer/tracker run covers the remaining differential
questions. Durable magnets, BEP 12 tiers, endgame/CANCEL, unchoke/upload-slot
policy, bandwidth limits and per-file recheck remain open and are tracked by
`SNARK-012`.

## BitTorrent DHT/PEX hardening continuation (2026-10-26, static)

The DHT/PEX audits produced three implementation slices, all I2P-only and
without a peer-wire or persisted-format change (except the stricter KRPC
envelope):

- DHT: repliable datagrams are queued and parsed on the service thread; raw
  frames are copied/posted and limited to 1024/60 s; tokens use a bounded FIFO
  with a 16-per-node quota; routing admits two node IDs per destination; ports
  are clamped to 1..65534; errored nodes are marked failed; periodic state
  saves are single-flight and delegated-safe; a side-effect-free KRPC envelope
  decoder rejects malformed/duplicate/mixed frames.
- PEX/BEP 10: repeated handshakes are rejected, `reqq`/`v` are bounded, private
  `metadata_size` is omitted, seed classification requires a received bitfield,
  `i2p_dht` tolerates an unrelated `rport`, and PEX/DHT candidates use a
  bounded per-torrent TTL/backoff table with per-origin admission.
- CI: `test-torrents-dht-protocol`, `test-torrents-pex`,
  `fuzz-torrents-krpc` and `fuzz-torrents-extensions` are registered in the
  manual torrent/fuzz jobs. Differential Tier A parser vectors and a loopback
  Java 2.14.0 Tier B harness are designed but not authored.

Exit criteria: manual CI compiles and runs the new tests/fuzzers, the focused
torrent lane and the fuzzing workflow; then a loopback Java/native exchange
answers the open `announce_peer` `port`/`implied_port`, closed-envelope,
`noseed`, `rport` and metadata-size questions. BEP 12 tiers, live churn and
OS-separated DHT persistence remain open.

## I2PSnark comments/ratings continuation (2026-10-26, static)

The comment audits found a whole-state rejection on future timestamps, a
per-owner cap that anonymous entries bypassed, per-connection ingress that
allowed 160 new entries/minute, dedupe bypass by whitespace/zero-width padding,
a strict `filter` that dropped valid requests, and only 16 of up to 256
retained records reachable from either UI.

Implemented: future time clamps instead of rejecting the state; TAB and
tolerant `filter` handling; normalized dedupe with a separate anonymous quota;
per-connection 40/min and per-torrent 80/min admitted-entry windows;
revision-aware immediate refresh after local changes; revision bumps for
hide/trim persistence; `TORRENT-DETAIL` comment paging with
visible/hidden/local counts; TUI paging, aged timestamps, bidi stripping and
confirmed moderation; Android paging with age/local markers and four-catalog
strings. Prepared regressions are the extended `test-torrents-comments`,
`test-torrents-state`, `test-tui-protocol` and `TorrentControlTest`.

Exit criteria: manual torrent/TUI/Android CI compiles and runs the new cases;
a loopback Java 2.14.0 `ut_comment` exchange confirms dedupe/rating and
timestamp semantics. Open: `I18N-004` TUI catalogs, normalized rating updates,
whitespace policy confirmation and device moderation accessibility.

## Android SAF payload completion continuation (2026-10-26, static)

The SAF audits found that document storage marked every file present (forcing a
full rehash on every restart), that a POSIX seeding handle could not be
reopened writable after invalidation, that the broker watchdog charged queued
operations for a stalled sibling, and that a transient broker crash disabled
SAF until app restart.

Implemented: `TorrentFile::ProbePayload` startup probing with present-only
verification and missing/revoked files left downloadable; read-only-to-writable
POSIX reopen; watchdog arming after `ioLock`; bounded bind recovery with a 30 s
stability window; read-only tree selection for seeding; and instrumentation
fixtures for nested multi-file trees, zero-length documents, deleted/revoked/
same-length-replaced documents, no-seek/socket, write failure and post-retire
rebind. Prepared tests extend C++ payload, JVM protocol/policy and Android
instrumentation suites.

Exit criteria: manual Android CI compiles the JNI/AIDL/Java and instrumentation
APK; device runs cover multi-file SAF download/seeding, deletion, revocation,
no-seek, ENOSPC, broker death/rebind and restart. Trusting a resume bitfield by
per-document identity, a debug provider reachable by the app UID, and external
router mode remain open.

## Native TUI continuation (2026-10-26, static)

The audits found a dead Status scroll path, silent stale snapshots, a
four-request Destinations tick, invalid/C1/bidi bytes reaching ncursesw, a 5
minute forced reconnect, `q` exiting from a torrent detail, missing transport
online/log-reopen verbs, a stray daemon stdout write, and no Windows console
control handler.

Implemented: Status scrolling plus protocol/log-drop fields; `[stale]` and
loading distinctions on every cached panel; one Destinations auxiliary request
per 6 s; strict UTF-8/C1/bidi sanitisation; 30 minute TUI sessions; `q`-back
semantics and corrected help/footer; `ADMIN ONLINE` and `ADMIN LOG-REOPEN` with
Status `o`/Logs `O`; daemon stdout removal; Windows Ctrl-handler and VK_CANCEL;
and compile-free pins for these contracts. Prepared tests extend
`test-tui-protocol` and `control-security-static`.

Exit criteria: manual Unix/Windows CI compiles and runs the TUI protocol,
terminal, IPC regression and Windows pipe tests; a real terminal checks resize,
locale fallback, Unicode widths and the console restore path. NetDb/Destinations
selection, Config filtering/editing, Services secondary scrolling and a Windows
product IPC test remain open. No web console is reintroduced.

## Android Java 25/ART/JNI modernization continuation (2026-10-26, static)

The audits found that the Java 25 probe dexed only the outer class, expected
DEX 037, and did not assert the AGP-selected D8/R8; that the packaged APK DEX
was never checked for record/type-switch bootstraps; that `noexcept` disagreed
between three JNI definitions and the header; that `GetStringChars` could leak
on allocation failure; that the SAF bridge global class ref leaked on every
restart; and that several Java paths still used stringly results.

Implemented: all-class DEX-039 probing, expanded Java/API pins, packaged-DEX
bootstrap assertions, JNI symbol parity, corrected JNI exception/reference/
ownership paths, process-lifetime SAF bridge reuse, dead native restart removal,
a typed `ReseedStatus` record, an exhaustive state switch and documentation of
the D8-vs-ART and API-29 library limits. Prepared tests/gates are
`check-config.sh`, `verify-java25.sh`, `check-android-jni-symbols.pl`, the
Android workflow DEX step and `RuntimeAdminPolicyTest`.

Exit criteria: the manual Android job installs JDK 25/platform 37, compiles and
dexes the app and tests, runs Lint and unit tests, assembles both APKs with the
DEX assertions, and a device/API-29 run exercises records, sealed switches and
the JNI/SAF boundaries. Main-thread SAF preference work, a generic admin reply
facade and transient-provider failure semantics remain open.

## Native GUI Material 3 continuation (2026-10-26, static)

The GUI audit found three classes of defect: programmatic screens bypassed the
XML touch-target/width contract and had no tablet cap; SAF grant mutation ran
`SharedPreferences.commit()` plus provider calls on the main thread from a
picker callback; and detail navigation could stack or hide the wrong fragment.

Implemented: a shared `UiFactory` row/column contract with 48dp targets,
`MaxWidthLinearLayout` centring and one-utterance accessibility semantics; a
bounded `TorrentRow` record decoder with a titled actions dialog; translated
blindable marker; non-live graceful countdown; state-aware notification
Graceful/Cancel action; off-main SAF select/detach through the service worker
with typed results; restored torrent session state; coherent secondary-entry
visibility; and a night-theme/surface assertion in `UiContractTest`.

Exit criteria: the opt-in `android-ui` dispatch builds x86_64 API 29, boots an
emulator, passes `connectedDebugAndroidTest` including `UiContractTest`,
`MainActivityNavigationTest` and the SAF/broker suites, and a TalkBack pass
confirms row grouping and the notification actions. Real device, tablet and
visual evidence remains required.

## Complete native and Android internationalization (2026-10-26, static)

The audits found that the es/ru/zh catalogs covered only about a third of the
TUI, that `SetStatus`/`BeginPrompt` were unreachable through the `PutLine`
fallback, that the advertised `[s/N]`/`[д/Н]` affirmatives were rejected by the
parser, that multi-line catalog keys had been recorded as their first fragment
(so HTTP address-helper pages never matched), that no plural table existed and
that Android had no `<plurals>` at all.

Implemented: 455-key es/ru/zh parity; status/NetDb/config/destinations/logs/
torrents/detail/footer translation; localized wire status rendering; display-
column wrapping; localized confirmation tokens; plural tables and `ntr` usage;
a compile-free catalog checker wired into `i18n-static`; `autoconf_i2pd` moved
to the shared catalogs; environment-based standalone-tool selection; Android
plurals, typed user-message resources, notification channel description, log
row description, zh-TW fallback test and terminology unification.

Exit criteria: `test-i18n-locale`, `i18n-static`, the Perl catalog and Android
guards pass in manual CI; a terminal run shows no clipped label or mojibake in
es/ru/zh-Hans; an API 29/current device resolves the four catalogs, falls back
to English for zh-TW and selects the Russian plural forms. Help and `--help`
prose remain English pending re-wrapping.

## i2pd-tools integration continuation (2026-10-26, static)

The audit found eight divergent key loaders, five crypto-guard clones, four
secret-buffer idioms, `O_TRUNC` key writes, a `libi2pdclient` link that no tool
uses, a Windows getopt shim that stops at the first operand, a `keyinfo -b`
success exit after a rejection, a CRLF step-file signing mismatch, an
`autoconf_i2pd` NUL/truncate bug and four unsynchronised tool lists.

Implemented: `common/keys.hpp` (exact load, atomic private store, same-file
refusal, `SecureBuffer`), `ParseBounded`, shared `CryptoGuard`, core
`X25519Keys`, padding-terminal base64, bounded/NUL-safe `b33address`,
single-line `regaddr_3ld`, family-name/mode validation, per-tool CMake linkage,
one aggregate install, a Windows-only getopt source with a host test, a
compile-free inventory parity check across docs/packaging/CI, and a `tools-cli`
regression for exact exit codes and streams.

Exit criteria: manual CI builds and runs `tools-smoke`, `tools-regressions`,
`tools-cli`, `tools-getopt`, `tools-inventory` and `tools-inventory-static`;
Debian/RPM install the same 15 tools; the OpenBSD and MSYS2 lanes pass; a
Windows run proves `vain pattern -t N` no longer ignores options. Format
compatibility is unchanged and pinned by the existing record/key regressions.

## Firewall generator hardening (2026-10-26, static)

The four backend audits found: usage errors on stdout, partial stdout after a
later malformed file, a 4096 budget charged before family filtering, accepted
special-purpose IPv6/IPv4 ranges, nft keywords and leading `_` accepted, PF
labels over the 64-byte kernel limit, Windows `-Name` rewriting a base64 `~`,
and no positive nft/Windows/PF CLI coverage.

Implemented a shared endpoint model with renderer-local revalidation, stricter
address/identifier/tag/label checks, stderr usage, all-or-nothing buffered
output, post-selection budget accounting, exact backend unit pins and positive
CLI tests for nftables, Windows and PF plus mixed valid/malformed input.

Exit criteria: manual CI builds and runs `test-routerinfo-firewall`,
`tools-smoke`, `tools-cli` and `tools-regressions`; dedicated hosts validate
generated text with `iptables-restore --test`, `nft -c`, `pfctl -nf` and
PowerShell `-WhatIf`. No rule is ever executed by the tool.

## Documentation and identity consolidation (2026-10-26, static)

The docs audit found stale IPC versions, a reversed worker-hello description, an
incomplete torrent/JNI inventory, missing C++23 probe documentation, a
pledge_file contradiction, undefined documentation licensing, no NOTICE, a
stale Windows copyright, and a four-language policy split across AGENTS/docs.

Implemented: protocol v5 corrections and registry tables; worker-identity and
OpenBSD subset-selector fixes; canonical torrent/TUI verb and timeout tables;
JNI historical marker; C++23 probes; build-component/uninstall/RPM icon parity;
`docs/i18n.md` and its checks; `NOTICE` with CMake/DEB/RPM install;
`check-legal-metadata.cmake` and `check-upstream-pins.cmake`; heading and
cross-link repairs; branding provenance/contrast updates.

Exit criteria: manual docs CI runs the link/nav/branding/i18n/legal/pin checks
and MkDocs builds; manual packaging installs NOTICE/man pages/icons; a device
run confirms the four Android locales and zh-TW fallback.

## Service-definition consolidation (2026-10-26, static)

Static audits of the daemon CLI/signals/workers and of every manager found
inconsistent stop budgets, a reversed worker-hello description outside this
scope, a missing systemd log directory, an inert OpenBSD drain, a runit
`down-signal` that stock runsv does not support, broken OpenBSD `@sample`
paths, an unreachable SysV `force-reload`, and a crash path that exited 0.

Implemented: `LogsDirectory` and documented `KillMode=mixed`; a 660-second
stop budget with a 60-second margin everywhere it is expressible; runit
`SIGTERM` plus `finish`/`log/config`; s6/s6-rc `softlimit`, `finish`,
`timeout-finish` and argv parity; OpenRC retry/ulimit/checkpath fixes; SysV
identity-checked stop/reload and real `force-reload`; OpenBSD
`rc_stop_signal=INT` plus corrected port samples/notices; Shepherd's 660-second
group-stop documented; `SIGABRT` restored to a crash; and a compile-free
contract plus a manual `Service` install matrix for all seven managers.

Exit criteria: the manual packaging workflow installs each manager, the static
contracts pass, and a VM/container lane starts/stops/reloads systemd, OpenRC,
runit, s6/s6-rc, Shepherd, SysVinit, dinit, Upstart and OpenBSD rc.d while
checking the pidfile, log sink, worker drain and no-duplicate-instance rules.

## Packaging consolidation (2026-10-26, static)

The packaging audit found Debian manifests that copied the unfiltered source
docs instead of `debian/tmp`, unclaimed man pages and tools licence, a missing
`Breaks`/`Replaces` for the moved TUI binary, a directory→symlink upgrade risk
for `/var/lib/i2pd/certificates`, an AppArmor profile that denied the privsep
worker exec, CMake documentation payloads absent from RPM/static/container
images, and an OpenBSD port whose in-source CMake build and `@sample` paths
were inconsistent.

Implemented: CMake-owned docs/licences/man pages, corrected Debian manifests
and rules, `dh_apparmor`/sysusers, certificate migration and root-owned path
repair, the TUI split metadata, a corrected AppArmor profile, OpenBSD
`SEPARATE_BUILD`/LibreSSL/subscriptions/login-class handling, RPM/static/
container attribution, and compile-free contracts for all of it.

Exit criteria: the manual packaging workflow builds and installs both Debian
packages, lintian/autopkgtest pass (including the AppArmor parse and docs/man
assertions), the RPM/static/container lanes inspect their manifests, and the
OpenBSD VM runs `pkg_create`/`pkg_add` with a PLIST match.

## Test integration and manual CI (2026-10-26, static)

The audits found 14 Perl tests on the 1500 s default, assert-based tools/fuzz
targets compiled with `NDEBUG`, a no-op sanitizer `--timeout`, no
`--no-tests=error`/JUnit, serial CTest, unchecked fixed `/tmp` names, label
gaps, SIGPIPE false-pass/false-fail checks in the Android lane, and stale
eight-fuzzer documentation.

Implemented: explicit CTest budgets and parallel-safety properties, `-UNDEBUG`
for tools/fuzz, a provider-independent RFC 7748 regression, `-j`/JUnit/
`--no-tests=error` in every lane, a manual-only workflow guard, MSVC Windows
privsep coverage, OpenBSD configure-log evidence, SIGPIPE-safe Android checks
and documentation for all fifteen fuzzers.

Exit criteria: a manual dispatch compiles and runs the complete CTest matrix on
Linux/OpenSSL variants/Windows/OpenBSD, sanitizer and fuzzer lanes, JUnit/CTest
artifacts are complete, and the Android unit/instrumentation lanes exercise the
documented tests.

## Security fuzzing and negative coverage (2026-10-26, static)

The audit found empty per-run corpora, no `-max_len`, non-fatal UBSan in fuzz
targets, no UBSan-only lane, missing NTCP2/SAM/I2CP/I2NP harnesses, no
checked-in seed contract, and untested SSU2 block-framing negatives.

Implemented: checked-in seeds and bounded corpora, per-target artifacts,
four new harnesses, `-fno-sanitize-recover=all`, job-level sanitizer options,
a UBSan-only lane, `test-ssu2-payload-blocks`, and static corpus/fuzzer
contracts. Remaining high-value negatives are tracked in TEST-036.

Exit criteria: a manual dispatch builds and runs all fifteen fuzzers from the
checked-in corpus, the ASan+UBSan/UBSan/TSan lanes complete with attributed
artifacts, and the new SSU2/NTCP2/Ratchet/netDb/SAM/I2CP/IPC negative tests
pass on the supported platforms.

## Compile/link seam continuation (2026-10-26, static)

Six read-only audits reviewed core C++23, transports/crypto/protocols,
IPC/privsep, torrent/TUI/tools, Android/JNI and CMake/linkage. Confirmed
defects were corrected: stale `TunnelTest` field accesses, an unqualified
cross-namespace timeout constant, duplicate Windows channel members, a missing
Windows supervisor probe definition, the Android rollback guard spelling,
C++23 `std::string`/`std::string_view` concatenations, direct includes,
wide Windows nonce test types, the AF_UNIX test guard, the OpenSSL linked-probe
include, preset schema 3.25, atomic/privsep link propagation and the duplicate
tools-licence install. `GOV-009`/`BUILD-020` track the pass; the translation
concatenation shape is pinned by `check-i18n.cmake`.

Exit criteria: the manual Linux/OpenSSL, Windows, OpenBSD, Android, sanitizer,
fuzzer and packaging lanes compile and link the corrected tree. No compile,
link, CTest, Gradle, device, package or workflow evidence exists yet.

The follow-up `TEST-037` pass corrected two additional Unix/OpenBSD router-
storage compile barriers (`Fd::Release()` and the `ReceiveWithFd()` result
shape) and removed transitive standard-header dependencies across crypto,
transports, streaming, torrents and the TUI. Android JNI/AIDL/resources and
the CMake source/link graph had no additional demonstrable defect in this
pass. Exit criteria remain a manual dispatch of every supported compiler and
platform plus `WITH_TORRENTS=ON/OFF`; static inspection is not compilation.

## Latest workflow and static compile/link follow-up (2026-10-26, static)

Read-only `gh` triage covered the `a2b8dc48` and `b9cbdaa8` manual batches.
The `b9cbdaa8` batch was cancelled after several jobs failed; only the failed
jobs provide evidence. Confirmed corrections include the BOB const-socket peer
check, Win32 token-enumerator shadowing, the `size_t` torrent parser returns,
`Mapping` assignment, the i18n capture reset, the invalid packaging CMake
escapes, the removed Alpine `miniupnpc-static` package, the stale
`i2pdctl` diagnostic expectation, the OpenBSD `imsg_get`/`imsgbuf_get` probe,
`_GNU_SOURCE` for IPC credentials, direct standard/OpenSSL includes, the
`OPENSSL_PQ 0` fallback, the NTCP2 SipHash provider parameter and the Android
status/internal-storage/reseed/dimension contracts. The six parallel subsystem
reviews also recorded residual Android thread/lifetime work, the
`TorrentsComments.h` guard asymmetry and the latent local-control template
overload hazard; the CMake `CURSES_INCLUDE_DIRS` concern was not a defect
(variable available since CMake 3.1).

Exit criteria: a manual dispatch compiles and links the corrected tree on
Linux/OpenSSL, Windows/MSVC+MinGW, OpenBSD/LibreSSL, Android and the sanitizer,
fuzzer, packaging and static lanes. No compiler, configure, CTest, Gradle,
package or workflow evidence exists for these changes.

## Source documentation pass (2026-10-26, static)

Nine read-only/limited-edit subagents documented disjoint source areas:
core transports and protocols; crypto/netDb/persistence; IPC/daemon/Win32;
the torrent engine; client services; TUI and C++ tools; Android
Java/JNI/AIDL/SAF; Perl scripts; and service/packaging shell. Comments explain
contracts, ownership, locking, bounded resources, trust boundaries and wire
formats rather than restating code. `docs/source-documentation.md` records the
Doxygen/Javadoc/POD/POSIX conventions and the manual `source_docs` lane.

The pass also repaired source-comment defects: a malformed `*/` block in
`HTTPProxy.cpp` that turned prose into code, mismatched `@param` names in
`FS.h`/`Log.h`, and a misplaced Android manifest explanation. Confirmed
behavioural/lifetime findings are tracked in DOCS-015, CONC-028, SEC-032,
TOOL-018, PKG-028 and WIN-014; they were not silently changed.

Exit criteria: the manual `source_docs` lane generates Doxygen/Javadoc with
warnings as errors, the remaining unreviewed files receive contracts, and the
tracked findings are either fixed with regressions or explicitly accepted.
No compiler, generator, Gradle, package, validator or workflow ran for this
pass; everything remains static analysis pending manual CI.

## CI-driven compile fixes (2026-10-26, static)

The manually dispatched `88ac1e92` runs were read with `gh`: Linux/Android
40411690766, OpenBSD 40411699490, Windows 40411695544, Fuzzing 40411703834 and
Packaging 40411709057. The first significant diagnostics were grouped into
CE-001..CE-008 in `docs/ci/COMPILATION-ERRORS.md`: the unqualified client HTTP
header type, the non-existent Boost address error-code formatter, the Windows
POSIX `pid_t`, the timer error-code cancel overload, the missing Doxygen output
directory, the static OpenSSL archive order, and the SysV ShellCheck shim.

All eight causes are corrected locally; the linked-probe save/restore bug found
during the same analysis is fixed and pinned. Exit criteria: a new manual
dispatch on the corrected revision compiles Linux/OpenSSL variants, Windows
MSVC/MinGW, OpenBSD, Android/JNI, fuzzing, static-musl, container and Debian,
then passes the CTest, Doxygen/Javadoc and package lanes. No local compiler,
configure, CTest, Gradle, Doxygen, package or workflow dispatch exists for
this revision.

## CI warning cleanup (2026-10-26, static)

The `88ac1e92` warning inventory was reduced across the cross-platform and
MSVC lanes: nested comment markers, unused captures/helpers, a hidden virtual
`Start`, deprecated OpenSSL 4 X509 access, Windows CRT deprecations, narrowing
conversions, shadowed locals and unused formal parameters. The X509 helper is
bounded and keeps the same common-name contract. BUILD-022 tracks the pass.

Exit criteria: a new manual dispatch reports no warning from first-party
sources in Linux/OpenBSD/Windows/Android/OpenSSL 4 and fuzz builds. This is
not yet demonstrated; the changes remain Pending CI compilation.

## Source documentation continuation (2026-10-27, static)

A second documentation pass covered the implementation-level core runtime,
crypto/persistence, Windows/OpenBSD IPC, terminal protocol, remaining Android
screens/receivers, systemd/runit/s6/OpenBSD port/Debian scripts and remaining
C++ tools. Comments state ownership, locking, bounds, trust boundaries and
failure behavior; no code changed. Findings are tracked in CONC-029, SEC-034,
TUI-014, ANDROID-036, TOOL-019 and PKG-029; DOCS-013 remains the coverage
umbrella.

## Source comment audit continuation (2026-10-31, static)

The audit expanded across maintained native, Android, test, tool, platform,
build, service and package files. Comments were inspected against actual
behavior and configuration; useful wording was clarified or wrapped without
changing executable statements or configuration values. Third-party,
generated, binary and strict JSON resources were excluded. The reusable style
policy is now owned by `AGENTS.md` and `docs/source-documentation.md`, with
implementation-specific notes in the documentation, C++23 and Android skills.
DOCS-013 remains open for existing tracked design findings and manual
Doxygen/Javadoc validation; this static pass is not dynamic validation.

Exit criteria: the manual source-docs lane generates Doxygen/Javadoc with
warnings as errors, the remaining files are reviewed, and the tracked findings
are fixed with regressions or explicitly accepted. No local generator,
compiler, CMake, CTest, Gradle or package validation exists for this pass.

## Second CI compile/warning continuation (2026-10-27, static)

The `b8c6473e` dispatch exposed a second layer: const-correct X509 access,
one unqualified tool helper, a `string_view`/`string` copy-initialization in
JNI, Doxygen placeholder tags, SOCKS override/unused-field warnings, a DHT
sign-compare and Boost.Asio endpoint formatting reaching `strcat` on OpenBSD.
All were corrected locally (CE-009..CE-013, BUILD-023); the generic `LogPrint`
now routes Asio addresses/endpoints through the safe formatters.

Exit criteria: the next manual Linux/OpenSSL/Windows/OpenBSD/Android/fuzz/
packaging dispatch compiles and the source-docs lane emits no unsupported-tag
warning. No local compiler, CMake, CTest, Gradle, Doxygen or package evidence
exists for this revision.

## Third CI compile and validation continuation (2026-10-27, static)

Runs 40419430023, 40419433360, 40419436959, 40419440596 and 40419444535 on
published SHA `0da3aed0f255d801cf9e85ab1cf155b7175d24e2` exposed the next
failures: const X.509 issuer helper input, reseed fixture `variables_map`
assignment, two Windows IPC/restart assertions, one Android tunnel-name
expectation, a missing Doxygen parameter tag, ShellCheck SC2015, the static-PIE
`file` wording check and a package-scoped Lintian override. Corrected locally
and recorded as CE-014..CE-021 in `docs/ci/COMPILATION-ERRORS.md` and BUILD-024.
The earlier helper-child-constness, `b33offlinekeys` namespace and JNI
`string_view` diagnostics are obsolete by source changes after SHA `88ac1e92`.
The new evidence contains no independent current NTCP2/SSU2/ratchet/netDb/client
or CMake configure/link root cause; several downstream lanes were stopped
before their later checks.

Exit criteria: manual CI on the corrected source passes Linux GCC/Clang and
OpenSSL 3/4, OpenBSD, Windows MSYS2/MSVC, Android Java/JNI/APK, Doxygen,
sanitizer/fuzzer, static-musl and Debian package/Lintian lanes. No such run was
performed on these local changes; compilation, tests and packaging remain
pending CI.

## CI compile, package and CTest continuation (2026-10-31, static)

The latest read-only GitHub Actions audit collected runs 40594540028,
40594544850, 40594549892, 40594555055 and 40594560248 on published SHA
`b34b2e79546c929f1f5f788d4905b41c9a92d45d`. CE-031..CE-036 record and
correct the common nested-enumerator test compile error, omitted CMake library
component artifacts, Debian's compressed branding palette, the Android APK
output-path assertion and two recurrent Windows CTest failures. Protocol,
Torrent/TUI/tools and platform reconnaissance found no other distinct current
compile/link root; several downstream targets remain unobserved because their
jobs stopped at earlier failures.

The compile-free contract sweep also caught a wrapped `Expected.h` comment
marker in `check-build-system.cmake`; the explanatory CMake comment was
restored and the checker now passes. This was checker drift, not a compiler
failure or broken include interface. Other selected CMake contracts passed;
all dynamic validation remains pending manual CI on a revision containing the
local changes.

All corrections are static only. The test, Windows IPC and CMake package fixes
need a fresh manual CI run; Android package-content/D8/R8/JNI checks and Debian
smoke need to complete after their path assertions. Fuzzing passed on the
uncorrected SHA. Do not treat that success or any earlier SHA as validation of
the local corrections.

Reviewing the comment-only diff also found a local Doxygen delimiter
regression in 15 wrapped comments across eight native test sources. The
comments are repaired and tracked as CE-037/DOCS-015; because no CI run used
this revision, native compilation and the source-doc lane remain pending.

## Latest CI compile, docs and package failure pass (2026-10-31, static)

Read GitHub Actions first with gh: runs 41207325363 (Linux/Android),
41207324984 (Windows), 41207325119 (OpenBSD), 41207325136 (Fuzzing) and
41207325472 (Packaging), all on published main SHA
46738ddf6f0f9b8e01e31d7c28bc6396215f82e8, which matched local HEAD at audit
start. The first Linux/OpenBSD/Windows MSYS2 failure was malformed nested
comments in test-http-req.cpp and test-http-res.cpp; downstream build targets
were not reached. The source API Doxygen run also found unclosed blocks in
OpenBSDSandbox.h. Android native build, JVM tests, Lint and APK assembly passed,
then Javadoc configuration failed on the removed AGP bootClasspath property.
The Perl suite exposed an overly broad Python ban conflicting with the
mandatory shared agent-knowledge validator. MSVC built selected targets but
two Windows IPC/supervisor tests had stale role/channel expectations. The
Debian remove/purge job remains under investigation because the exact run log
does not name which silent assertion or command failed.

Static fixes are in place for the shared HTTP test syntax, Doxygen comments,
AGP Javadoc classpath, Perl allowlist and Windows test expectations. The Debian
package workflow now prints a distinct message for each remove/purge
assertion; the generated postrm was inspected from the exact run artifact and
no hook change was justified. The installed CMake consumer and all four
exported archives passed on the published SHA; Linux GCC reached libi2pd and
libi2pdclient links before test compilation stopped. OpenSSL capability probes
passed, so no platform-specific crypto root was observed before that stop.
These results do not prove downstream targets or current local-only changes.

CE-038..CE-044 and BUILD-028 track the causes and statuses. A new manual CI
revision must validate the corrections and disambiguate the Debian lifecycle
failure. No local build, compiler, CTest, Gradle, Doxygen, Perl tests, package
install, commit or push was performed.

## Compiler and warning prevention pass (2026-10-31, static)

The retained Actions history currently covers 206 runs across 11 workflow
names and 43 SHAs; logs were available for 202 completed runs, while three
expired job logs returned `log not found` and the newest MSVC job was still
running at review time. The prior dispatched platform batch on published SHA
`45f5f739aa47ed87aa7462e9b040c0db601bc667` exposed a shared malformed comment
in the Base64 test on Linux, Windows and OpenBSD, plus repeated `-Wcomment`
warnings in shared headers. `docs/ci/COMPILATION-ERRORS.md` records CE-045;
warning history and remaining findings are in
`docs/ci/COMPILATION-WARNINGS.md`, with semantic search guidance in
`docs/ci/COMPILATION-PATTERNS.md`.

Static changes repair the comment roots and add a Perl lexical regression,
replace a Win32 unsigned countdown check with wrap-safe tick arithmetic and a
focused regression, exercise the previously omitted DataStorage rollback
phase, and construct the LeaseSet signed-data buffer at its final size after
GCC 15 warned in the vector growth path. Android source/resource changes
address 34 of the 51 observed Lint instances by static review. Seventeen
instances remain for ABI/dependency and layout/branding review; Boost.Asio's
OpenBSD `strcat` linker warning remains dependency-owned. Broad unused-
parameter and uninitialized warning suppressions were removed, but global
warnings-as-errors is deferred while these diagnostics remain unresolved.

None of the static edits has run in Actions. The cancelled/failed batch at the
old published SHA does not validate them. Fresh manual Linux, Windows,
OpenBSD, Android, fuzz and packaging runs on the corrected revision must
establish compile/link status and enumerate warnings beyond targets blocked
by the first syntax failure.

The next retained batch ran on `abe09136c84d1a4df487306dad6a7eb8e2034806`.
Linux/OpenBSD/MSYS2 exposed the `i2p` namespace constants in
`test-transit-tunnel-build-replay.cpp`; successful Fuzzing, container and
static-musl builds exposed first-party unused asynchronous parameters.
Windows Clang also reported an unused Win32 callback parameter and POSIX-only
address-book test helpers compiled outside their platform guard. The
Debian package built and passed Lintian/DEP-8 but its remove assertion failed
because it checked an empty package-owned directory; the workflow now seeds a
private marker to test actual state preservation. Debian configure also
reported unset CMake CMP0167. Corrections are recorded as CE-044/046/047 and
CW-006/007; the OpenBSD Boost.Asio linker warning remains an external finding
(CW-004). These local changes are static only and await CI on a revision that
contains them. The earlier Android locale failure is fixed in the current
worktree but likewise remains unvalidated remotely.

## Third CI audit (00929cd1, 2026-10-31, static)

The third batch exposed a shared test namespace error, a Windows IPC
request/typed-Error direction defect, a Windows supervisor fail-closed handle
leak, a Debian remove fixture that did not represent real router state, and an
OpenBSD linker warning from direct Asio address formatting. All are corrected
locally (CE-048..CE-054, BUILD-030); dedicated runtime, test-failure and
regression inventories now accompany the compile/warning inventories.

Exit criteria: the next manual Linux/OpenSSL/Windows/OpenBSD/Android/fuzz/
packaging/source-docs batch compiles, runs the Windows IPC/supervisor tests,
preserves Debian state on remove and emits no first-party warning. No local
compiler, CTest, Gradle, Doxygen or package validation exists for this
revision.

## CI audit: first executed suites (63c42ca4, 2026-10-31, static)

This batch reached CTest on Windows and exposed a shared compile blocker plus
a large first-execution backlog. CE-055..CE-064 record the compile, Doxygen,
Javadoc, checker-portability, man-page, CRLF, CLI, reserved-range and Debian
permission defects. RT-004/RT-005 record the null transports context and the
unguarded checksum-IV suffix; TF-001..TF-035 record the assertions, including
the product fixes for zero-hop tunnels, b33 labels, upgrade tokens, origin
forms, tracker failure scheduling, X25519 zero scalars, IPC direction and the
restart budget.

Exit criteria: a manual batch on the corrected revision must compile every
Linux/OpenSSL/OpenBSD/Windows/Android target, run the previously unexecuted
suites without assertion failures or sanitizer reports, install the man pages
on Windows, pass the static Perl/CMake contracts on a CRLF-free checkout,
preserve Debian state across remove, and pass the bounded `i2pd-version`
driver. Two tests and the Windows launch hang remain `CI failure` with
diagnostics.

## Test diagnostics continuation (2026-10-31, static)

A test process that never starts the logger keeps its records only in the
administrative ring, and an `assert()` abort never unwinds to a dump. Every
`i2pd_add_test()` target now links `tests/log-support.cpp`, which installs a
synchronous stderr sink before `main`; `OPENI2PD_TEST_LOG_LEVEL` selects the
level and fuzz targets are excluded for throughput. `test-log-diagnostic-sink`
and `tests/check-build-system.cmake` pin the contract.

Exit criteria: the next manual batch shows `[test-log ...]` lines for the
product diagnostics of every C++ test, including a deliberately failing
assertion in CI, without changing any test result. No local compiler or CTest
run exists.

## Doxygen parameter guard (2026-10-31, static)

The first source-docs run on the corrected revision failed at
`Elligator2::Encode`: the block named two parameters and omitted the two
pointers, and Doxygen treats partial parameter documentation as fatal. All
17 instances of the same defect, across both `/** */` and `///` blocks, are
corrected, and `doxygen-static` (a compile-free scanner over the
`Doxyfile.source` roots) now catches the class before the manual lane.

Exit criteria: the next source-docs dispatch compiles the API documentation
without a `WARN_AS_ERROR` abort, and the static Perl step keeps
`doxygen-static` green. No local Doxygen or compiler run exists.

## Executed-suite audit and Windows containment (2026-11-01, static)

Fuzzing and Packaging pass on `dc5540ae`. The remaining lanes expose a test
compile blocker, two first-run protocol defects (SSU2 `pq` wire encoding,
ELS2 fixture geometry), a tunnel-endpoint guard that rejected pool cells, a
tracker parser that committed partial peers, stale SAM/digest tool
expectations, Windows process-attribute lifetime and SID allocator defects,
nested-Job UI restrictions, and the GUI `--version` hang. All are corrected
under CE-066/CE-067, RT-007..RT-010 and TF-036..TF-043, and the per-test log
echo is what made the two previously open tests diagnosable.

Exit criteria: the next manual batch compiles every Linux/OpenBSD/sanitizer
target, runs the corrected suites without assertions, MSVC isolation and
supervisor spawn under the runner's existing Job Object without heap
corruption, the Windows version/help probes terminate with the banner, and
the source-docs lane completes without a duplicate-`@param` abort. No local
dynamic validation exists.

## Linux OpenSSL branch workflows (2026-11-01, static)

The five older Linux lanes (two compiler jobs, a distribution OpenSSL 3.x job
and two source-built 4.0.0/4.0.3 jobs) are replaced by two manual workflows:
`build-linux-openssl3.yml` and `build-linux-openssl4.yml`. Each uses the
latest Homebrew bottle for its branch and the precompiled toolchain bottles,
verifies that the installed OpenSSL matches the formula and branch, and runs
the full CTest/label/Perl sequence for GCC and Clang. `build.yml` keeps the
Android, install, interop, sanitizer, documentation and static jobs.

Exit criteria: both branch workflows dispatch cleanly and complete the full
suite with the Homebrew bottles, with no source-built OpenSSL and no
`push`/schedule trigger. No local dynamic validation exists.

## Doxygen reference guard (2026-11-01, static)

The source-docs lane rejects an unresolved `@ref`/`\ref` under
`WARN_AS_ERROR`, and Doxygen only resolves extracted symbols. The two
data-member references are backticks, and the static checker now validates a
reference target against types, namespaces, macros and callables collected
from the scanned tree.

Exit criteria: the next source-docs dispatch completes without an unresolved
reference, and `doxygen-static` stays green with its negative fixtures. No
local Doxygen run exists.

## Doxygen command and parameter-name guard (2026-11-01, static)

After the unresolved-reference fix, the static continuation found Javadoc
inline tags in a C++ comment; `{@code` may open a Doxygen code block that is
never closed. The file uses backticks now, and the checker rejects Javadoc
inline tags, unbalanced `@code`/`@verbatim`, and documented parameter names
absent from the declaration, while accepting array parameters.

Exit criteria: the next source-docs dispatch reaches and passes the Android
JNI translation unit and every later file without a command-balance or
parameter-name abort. No local Doxygen run exists.

## Span, containment and Windows diagnostics (2026-11-01, static)

The `4e776a39` batch surfaces a cross-platform span compile blocker, a Win32
device-name fixture trap, an unreachable live-worker test, product containment
holes and a Windows informational hang that survived the argv rewrite. All are
corrected statically; the MSVC access violations need PageHeap/minidump
evidence (TEST-039) rather than a guessed change.

Exit criteria: Linux/OpenBSD/sanitizer compile and run; MSYS2 passes
tools-regressions, tunnel-endpoint and the version probe; the MSVC lane either
passes or produces a PageHeap/minidump that names the corrupting allocation;
the Windows probe logs subsystem/imports and a bounded `--version` result. No
local dynamic validation exists.

## Preventive subsystem audit (2026-11-01, static)

The audit moves the project from reactive CI repair to invariant ownership:
one receive-buffer contract, one descriptor/type authority, one deferred
handler lifetime rule, one clearnet predicate, one UTF-8 column policy, one
Android run-intent policy and one deployment contract. Each is expressed as a
compile-free checker or regression where the seam allows, and as an explicit
residual where it needs a device, a package build or a live peer.

Exit criteria: the Linux OpenSSL 3/4 lanes, OpenBSD, Windows MSYS2, the
sanitizer matrix and Android instrumentation execute the prepared regressions
(RT-013..RT-021, TEST-040); the MSVC intermittent fault either passes
consistently or yields a PageHeap/minidump; the open SSU2/NTCP2 admission and
socket-sharing items (`NET-172`) close without weakening transport bounds; the
OpenBSD pledge-file bound and RPM `%cmake` question are settled with evidence.
No local dynamic validation exists.

### Signing and session ownership

Closes a signature-length overflow that an offline `router.keys` could reach,
makes LeaseSet2 layout immutable after decryption, and turns SAM/I2CP/tunnel
defects into explicit contracts (no secret-bearing log line, shared name rule,
fail-closed accesslist, mutex-guarded owner and resolver, flush-before-close).
Acceptance requires the prepared regressions to run on the Linux/OpenBSD/
Windows/sanitizer lanes and a decision on the streaming acceptor ownership
token (`CONC-031`).

## Current published CI audit (2026-11-02, static)

Manual runs on published `56f17c0f` show Linux/OpenSSL 3 and 4 builds
completing before multiple CTest failures; OpenBSD and Windows CTest failures;
Android Java compilation errors; and ASan/UBSan/TSan findings. Linux packaging
and install-consumer workflows passed, as did the fuzz-smoke job, but these
partial successes do not establish a clean matrix. See BUILD-037 and the
compile, warning, runtime, test and pattern inventories under `docs/ci/`.

The worktree contains static fixes for Android effectively-final captures,
two test-only borrowed-view lifetime errors, integer-to-enum validation before
conversion, sensitive file creation fallbacks, and scoped LeaseSet secret
cleansing. They remain pending manual CI. EdDSA sanitizer underflow, three
leak reports, lifecycle timeouts, Windows heap corruption and multiple
platform assertions remain open. No local dynamic validation was run.

Follow-up static work also makes `HashedStorage::Path` return an empty path
for an empty identifier and tests that contract; Android snapshot errors now
use generic categories with a marker-based redaction regression. Both remain
pending manual CI.
The failing I2PControl socket fixture now retains the old listener through
replacement cleanup to prevent inode reuse, and the DataStorage configuration
encoder enforces the same absolute-root contract as its decoder. Their focused
regressions remain pending CI.
The storage supervisors also preserve their bounded restart backoff across
replacement handshakes so repeated short-lived worker crashes cannot reset
the retry budget; the existing 250/500 ms client regression covers this
policy, pending manual CI.

The 2026-11-02 lifecycle audit found static I2CPServer lifecycle risks not
represented by a historical CI failure: repeated Start can create parallel
accept chains, concurrent Start/Stop can leave a worker without a listener,
and timeout fallback tears sessions down outside their owning executor.
CONC-032 tracks a serialized lifecycle barrier, safe timeout/drain semantics
and deterministic regressions. The current I2CPServer implementation remains
under review; no runtime claim is made.
The same continuation closed two stale portions of ANDROID-036 by pinning the
existing subscription single-flight gate and the 1000-record retained-log
cap. TUI-014 now covers strict COLORFGBG parsing, visible POSIX NUL handling
and the inclusive 128-byte response-header limit on both clients. TOOL-018
now fails vector checks on unexpected passes and reports subprocess
signals/wait errors distinctly. ANDROID-033 now serializes run-state
read/modify/write transitions and rejects stale precomputed generations. These
are static implementations with regression coverage prepared for manual CI;
remaining items stay open in `TODO.md`.
CMake policy warnings from both build and installed-consumer Boost lookup are
addressed with availability-guarded policy selection and a static contract;
cross-version configure validation remains pending.
The DHT persistence regression now accounts for its tunnel's self record
before routing peers and verifies malformed input leaves the prior snapshot
intact; no persisted format changed.
Three more published CTest failures were traced to fixtures: TF-048 used
concatenated padded base64 values, TF-049 reused a stopped Asio context, and
TF-058 did not synchronize reader execution before registry deletion. Their
fixtures now preserve valid base64 framing, restart only stopped contexts,
and barrier after the required snapshots. CI validation remains pending.
The OpenBSD GOST fixture also lacked the required `BN_CTX_start()` frame
around temporary BIGNUMs; it now balances the frame and remains pending
LibreSSL CI. For the Clang/OpenSSL 4 I2CP lifecycle timeout, destination timer,
post and stored request callbacks now use weak ownership; the session snapshot
future is weak as well. The new regression covers a queued callback stranded
in a stopped private context. Executor-owned request maps are not touched by
external Stop; callbacks finish in destruction. SAM/BOB request consumers and
I2CP server lifecycle posts remain under review. All changes await manual CI.
The OpenBSD IPC recovery fixture now uses a frame one byte larger than the
16-byte header; maximum payload limits remain covered at the codec layer. The
historical socket send error was not captured, so the transport portability
diagnosis and correction await manual OpenBSD CI.
SEC-034 now hardens the direct logger in addition to atomic private state
writes: POSIX opens each directory component without following symlinks and
sets mode 0600 on the validated descriptor; Windows rejects reparse targets
and installs a protected user/SYSTEM DACL on the validated handle. Regressions
cover links, permissions and rotation; all changes await manual CI.

## CI audit continuation — 2026-11-02

The current published SHA has been audited across 247 accessible workflow
runs, including successful sibling jobs. Corrections in the worktree remain
static until a manual run on a published revision. The open priority is to
resolve the documented Linux/OpenBSD/Windows CTest and sanitizer findings,
obtain symbolized EdDSA/LeakSanitizer evidence, and finish Android Java 25
validation after the two Javac corrections. A shared MSVC test-support C4996
warning and the missing MSVC `WITH_HARDENING` equivalent now have static local
corrections, pending Windows CI. Remaining package-quality findings still
need follow-up. See
[`docs/ci/COMPILATION-WARNINGS.md`](docs/ci/COMPILATION-WARNINGS.md) and
[`docs/ci/TEST-FAILURES.md`](docs/ci/TEST-FAILURES.md).

## 2026-11-02 continuation priorities

CONT-001..004 advance bounded torrent RPC, authority-only IPv6 URLs,
EdDSA fixture bounds, destination RNG failure handling and checked tool
crypto initialization. AND-025 now reflects the bounded SAF worker and
callback delivery epoch fence. Implemented portions are **Implemented /
static**, with **Pending CI compilation** and **Pending CI tests**; none
have current-tree dynamic evidence. RT-027/TF-069 remain open until the
corrected 64-byte digest fixture runs on sanitizer and platform lanes.
Global network/storage separation, NET-172, multi-peer torrent metadata,
Windows corruption and unresolved leaks remain independent open work.

## Async input and stable-worker continuation

NET-172 now has async-only NTCP2 body and local-tunnel input paths, while
admission, options and establisher socket ownership remain open. CONC-024
separates final-owner descriptor cleanup from registry notification, including
the equivalent SOCKS destructor path. CONT-005 requires a valid operation
reply from a stable generation before replenishing storage restart attempts.
TOOL-019 now includes vain CLI errors and collision naming. CONT-006 improves
Windows fixture isolation and removes irrelevant CRLF dependence; MSVC heap
corruption remains unresolved. All changes are implemented statically and
remain Pending CI compilation / Pending CI tests.

## Destination publication, I2CP lifecycle and SAF admission

CONC-026 serializes destination factory selection/start and preserves hash
registrations on conflicting replacement. CONT-007 bounds SAF queued work;
CONT-008 connects RPC metainfo fields to engine data; CONT-009 pins portable
non-deprecated timer cancellation. I2CP lifecycle integration is tracked by
CONC-032, including pre-session connections, restart generations and executor
ownership through drain. All implemented changes remain Pending CI
compilation / Pending CI tests; global Stop/reload and provider cancellation
limits remain explicit.

## Shared metadata, deferred restart and fail-closed cleanup

CONT-010 shares BEP 9 metadata across peers with exact response validation,
bounded leases, alternate-size recovery and hash-failure cooldown. AND-024
replaces polling with a cancellable Android restart continuation. CONT-011
checks RedDSA wrapper failures and cleans nonce/scalar storage; fallible point
internals remain open. CONT-012 prepares optional Windows heap traces and
matching symbols, retaining TF-062 unresolved. CONT-013 completes initialized
storage-worker coverage on Unix startup abort. These implementations are
static and Pending CI compilation / Pending CI tests. CONC-023 remains open:
listener generation alone cannot solve reentrant handler startup/Stop safely.

### Trust, SAF and recovery continuation

CONT-014 replaces explicit reseed trust reloads and fixes usable-identity
counts, including the equivalent Family counting pattern. Family revocation
still requires cached FamilyID invalidation. CONT-015 hardens direct IRC and
unique-local entry points; ordinary authenticated Streaming acceptance already
supplies identity. CONT-016 integrates cooperative SAF import cancellation and
serialized mapping/grant retirement. CONT-017 develops durable unresolved
magnet recovery with bounded startup authority and pause ordering. CONT-018
corrects duplicate SAM session registration ownership. These increments
are Implemented / static after integration review and require manual CI
compilation/tests; no new runtime evidence is attributed to them. SNARK-012
remains Partially implemented / static for advanced peer policy. Recovery
withholds resolved admission when pause-policy scanning cannot complete;
fallback retirement requires durable metainfo and administrative state.

## Transport fail-closed, provider capability and torrent continuity

CONT-019/020 close ignored SSU2 AEAD results, parser frame sizing and the
missing X25519 provider gate. CONT-021 bounds routine resume persistence,
exposes swarm availability, makes missing-payload promotion fail closed,
clears stopped datagram queues and replaces TUI status substring matching with
an exact token classifier. CONT-022 closes the remaining verifyhost/Perl/i18n
and static-package contracts. CONT-023 strengthens the Java 25 API-29 guard and
metric label association. CONT-024 records the `e485e138..2b92f2d4` upstream
window. All are Implemented / static; manual CI compilation/tests and the
Android UI/device lane remain the next evidence gates. Open priorities remain
the Transports self-stop detach lifetime (CONC-002/TODO), network-worker
separation, NTCP2 admission/options, Streaming destination cycles and
I2PSnark serving/endgame policy.

## OpenBSD pledge hardening without exec or error

OPENBSD-SBX-13 removes `exec` and `error` from the promise table and every
policy boundary. The supervisor now owns a pre-created active/reserve log
worker pair and pledges only `stdio proc`; it never executes a replacement.
The on-demand torrent-storage broker is disabled because it required exec, and
isolated torrent storage fails closed until a pre-created worker-pool design is
proven. The core spawns every worker while unpledged, installs a first pledge
with `unveil`, then drops to runtime with no `exec`, `error` or `id`.
Implemented / static; OpenBSD compilation, pledge-runtime, broker and service
validation remain pending manual CI.

### Registry and capability continuation

CONT-025..CONT-028 add synchronous peer-slot retirement, owner-checked
teardown, torrent completion authority, NTCP2 pending-slot ownership and SAF
mode-preserving cleanup. Implementation is static; compilation and tests are
pending manual CI. Temporary SAF borrowers now share bounded process-wide
lease accounting; external-process consumers remain outside that boundary.
The OpenBSD promise review preserves the exec/error prohibition and records
per-role evidence gaps; pre-created torrent-storage provisioning remains open.

### Executor, verification and IPC response continuation

CONT-029..CONT-034 address the idle-executor timeout cause, equivalent NTP
startup/restart seam, pending NTCP2 source-prefix exhaustion, torrent recheck
ownership and single-flight data-storage response corruption. CONT-027 now
includes bounded process-wide SAF leases and deferred mapping revocation.
Manual CI retains exact sanitizer reproducers for unresolved leak/lifecycle
attribution. All new implementations and regressions remain static/pending CI;
no global privilege-separation or full NET-172 completion is claimed.

### Startup publication, initial reads and creation bounds

CONT-035 repairs reused logger publication authority; CONT-036 covers only
the first 64 inbound NTCP2 request bytes with a cancellable 10-second deadline.
CONT-038 bounds SAF creation inputs before retention and JNI copying, with
UTF-8 component parity and final hash accounting. CONT-037 advances bounded
cold-piece serving and exact cancellation without claiming upload-slot or
endgame completion. Static implementations and prepared regressions await
manual CI; full handshake ownership and provider behavior remain open.

### Reply correlation, native slots and Android dialog authority

CONT-039..CONT-043 remove a ratchet correlation cycle, add bounded fair native
upload grants, revoke Android form authority across view/session replacement
and correct private registration/RPC diagnostics. Java reciprocity scheduling,
endgame, full worker isolation and exact historical sanitizer attribution
remain open. Implementations and prepared regressions are static; compilation,
sanitizers, JVM/device behavior and live interoperability await manual CI.

### Complete maintenance-language migration

PERL-010/011 replace the shared validator's former Python exception with a
core-only Perl implementation and Test::More contract. The project retains the
shared CLI/report/configuration semantics without requiring Python source
byte identity. Workflows, CMake inventories and developer instructions use
Perl; whole-tree source/dependency guards have no Python allowlist. Source and
security review precede removal; runtime equivalence, performance and platform
coverage remain pending authorized CI.

### Shell source boundaries and declarative Make

SHELL-001..003 replace interpreter source operands with native operations or
named Perl helpers and delegate Make control flow to source-controlled Shell.
The native prerequisite graph, CMake/Ninja incremental authority and Debian/
OpenBSD framework boundaries remain intact. Source-only guards and prepared
mock/interop regressions pin the new organization; actual Make, package and
behavioral execution remain pending manual CI.

### Authorized publication of the accumulated continuation

The 2026-11-05 runtime/compile review extends the evidence inventory to 263
historical runs plus a separately dispatched published Windows diagnostic run.
Local static corrections address CE-076, CW-020/021/022 and RT-034/035/037;
regressions cover stream publication, malformed Windows hello/re-arm, manual
payloads and early sanitizer evidence. RT-032 heap corruption and the complete
historical sanitizer ownership families remain unresolved. Status: Pending CI
compilation / Pending CI tests; no supported-matrix cleanliness claim.

The subsequent compiler-cleanliness review paginated 255 accessible runs and
recovered full logs for 252. CE-074/075 and CW-016/017 have static corrections;
early compiler artifact retention and negative fixtures are prepared. Strict
diagnostics are a project-target opt-in exposed by both manual OpenSSL lanes.
Status: Pending CI compilation / Pending CI tests. Published d7e8491c stopped
early in native compilation; supported-matrix cleanliness, historical runtime
findings and external diagnostics remain unresolved.

The reviewed implementation, regression and maintenance increments are split
into signed subsystem commits under explicit user authorization. Publication
does not promote static work to Validated by CI; supported compiler, sanitizer,
package, live-peer and device lanes still need the published revision.

## 2026-11-07 evidence checkpoint

Implemented / static: RFC nonce, overlapped completion lifetime, DHT persistence
ordering, bencode value validation and CI fixture/artifact corrections. Pending
CI compilation and Pending CI tests apply to all local changes. Prioritize
MSVC PageHeap attribution, worker DLL startup and MinGW version timeout, then
fresh sanitizer evidence for historical ownership findings. The floating
OpenSSL 4 lane currently uses 4.1.0 and does not establish explicit 4.0.x
coverage. See the six docs/ci inventories and TODO audit follow-up rows.

## Post-publication sanitizer checkpoint

The executed 68c16e10 matrix restores sanitizer coverage and validates narrow
crypto/ratchet/DHT paths; it does not pass as a whole. Profile static teardown,
profile recycler authority, fixture completion/draining, IPC test policy and
proxy redaction corrections are Implemented / static, Pending CI tests.
Next validate the process-exit regression on MSVC/PageHeap and all sanitizer
lanes, then classify the provider report using instrumented OpenSSL. Keep
MinGW pre-output startup, worker DLL initialization, Android rendering warnings
and platform capability skips separate from demonstrated corrected causes.

## 2026-11-08 evidence and prevention checkpoint

Validated by CI: the profile process-exit regression on the executed MSVC,
OpenBSD and sanitizer lanes; focused instrumented-provider diagnostic
42422405960 on 9f5320b2. No clean supported-matrix claim follows from those
narrow results. Next run the complete TSan suite with its instrumented
library, the corrected IPC opcode fixture and the prepared typed-parser and
random-source failure regressions. These local corrections are Implemented /
static, Pending CI compilation and Pending CI tests. Preserve the blocked
Windows Unicode/worker-startup investigations and platform capability gaps.

## 2026-11-08 executed c115d386 checkpoint

Full TSan now uses the instrumented OpenSSL/provider dependency and no longer
emits the earlier cache race report in the executed suite. All three
sanitizer configurations still fail the same keygen, tracker fixture and IPC
EOF cases. The next static increment corrects those causes, strengthens IPC
refusal authority/deadlines, Retry state publication, tracker framing and
tunnel-test attempt ownership, and preserves MSVC Release assertions without
D9025. These changes and the wide Android measurement fix await manual CI.
Keep the Windows Unicode/worker startup and device/platform coverage gaps
open rather than inferring cleanliness from the passing sibling jobs.

## 2026-11-08 four-language UI continuation

Implemented / static: native catalogs now cover 628 keys and 14 plural groups,
including trusted TUI help, compact tabs, durations, summaries and known
errors. Language selection handles POSIX encodings and exact Chinese script
intent, retains immutable snapshots during concurrent lookups and preserves
English fallback. Prompt deletion and clipping preserve complete scalars and
trailing combining marks. Android adds 34 resource keys per locale and applies
per-app contexts to notifications/Quick Settings as well as UI labels.

Pending CI compilation / Pending CI tests: native concurrency/format/terminal
regressions, checker fixtures, JVM/resource tests and API 29/device locale
recreation, notification refresh, TalkBack and tablet/terminal layout review.
Standalone tool prose and unknown remote diagnostic bodies are not presented
as fully translated. Protocol tokens and user data remain unchanged.

## 2026-11-08 executed `6390fdea` and backpressure follow-up

Validated by CI: the executed ownership/Tag/tracker and named crypto
regressions, Android arm64 pipeline and Debian documentation smoke correction.
The full matrix still fails on IPC backpressure and Perl inventories. Next
validate RT-063's send-first absolute-deadline behavior on Linux and OpenBSD,
TF-093/094 inventory/source harvesting on POSIX and native Windows, and the
two prepared Android Overdraw corrections. These local changes are
Implemented / static, Pending CI compilation and Pending CI tests.

Keep forwarder publication atomic with client-service shutdown as a required
architectural follow-up for HTTP/SOCKS asynchronous completion writes. Retain
Windows startup/Unicode/version investigations and all skipped optional
quality/device/worker coverage. Explicit OpenSSL 4.0.x and the fourteen
remaining Android Lint findings are not represented as completed.

RT-064 additionally hardens torrent inbound acceptance against callbacks
copied before deregistration. Distinct generations, weak owners and a
synchronous registration/admission barrier now cover stop/restart and owner
release. The existing security target has a focused prepared regression;
its earlier passing result does not validate this new source change.

## Integral continuation: lifecycle, upstream and initial discovery

The next local increment implements CONT-044..049: Activity confirmation
authority, readiness generations, broker transaction deadlines, transient
identity separation, ECIES LeaseSet1 refusal, borrowed alphabet spans and
storage-qualified initial tracker/DHT discovery. All remain Implemented /
static, Pending CI compilation and Pending CI tests. The new native targets
join common CTest and selected MSVC build/filter/PageHeap/artifact inventories;
Android JVM/device source discovery includes the new confirmation controls.

The upstream high-water mark advances to 9702ba83 after eighteen explicit
commit decisions. Java stable 2.14.1 is verified independently; existing
2.14.0/2.13.0 references keep their historical scope. Next complete the shared
handler-admission architecture before converting HTTP/SOCKS success writes,
and migrate typed filesystem interfaces coherently with no-follow storage.
Advanced torrent bandwidth/ranking and live peer parity remain open.

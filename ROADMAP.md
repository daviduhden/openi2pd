# Roadmap

This roadmap coordinates parallel workstreams. It is subordinate to the
requirements and current code; `TODO.md` is the authoritative task list.

## Current baseline (verified in this workspace)

- Linux/GCC 16: main CMake configure and build passed at the Session 7
  checkpoint, before the local build freeze.
- CTest: 20/20 passed at the Session 7 checkpoint before the user froze local
  builds (includes torrent bencode and extended TUI protocol tests).
- TUI: live UNIX-socket session on Linux passed (state, peers, tunnels, netDb,
  logs, malformed requests, quit, cleanup).
- Tools: all 15 built; keygen/keyinfo/offlinekeys/i2pbase64/regaddr/verifyhost/
  x25519 smoke-tested.
- Android: Java 25 source/target compiles with JDK 25/AGP 9.4.1; Material 3
  UI/architecture source is present. APK/JNI/Lint/device tests are deferred to
  CI by user instruction.
- Torrent: web console removed; native C++ control + ncurses TUI + local IPC
  and Perl `i2pdctl` are implemented. Dynamic TUI/engine/RPC/TLS validation is
  deferred to CI.
- Java I2P: reference work remains pending.
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

1. Clone the latest stable Java tag (verify 2.13.0 or later) into a read-only
   temporary directory.
2. Build the `INTEROPERABILITY.md` matrix from Java classes/functions to C++
   implementations.
3. Fix only confirmed external-behavior differences; do not copy architecture.
4. Prepare differential harnesses and malformed-input vectors.
5. Re-run controlled integration tests.

Exit criteria: every announced protocol row has a Java reference, a C++
reference, a difference verdict and a validation status. No "compatible" claim
without evidence.

### 3. Android/Java 25 (CI build and device validation pending)

1. Use CI JDK 25, Android SDK 37 and NDK r27+ (local builds are frozen).
2. Validate Gradle 9.8.1/AGP 9.4.1 and toolchain 25 in CI.
3. Build APK/DEX; run Android Lint, unit tests and JNI/device tests.
4. Continue Java 25 source refactor (records, sealed types, pattern matching,
   switch expressions, lifecycle separation); D8/R8 class-version support and
   ART API constraints are documented separately.
5. Test rotation, background service, restart and permissions.

Exit criteria: reproducible APK build with JDK 25 + documented toolchain; no
hard-coded credentials; JNI shutdown/restart tests pass.

### 4. Tools and firewall

1. Run the new routerinfo firewall unit test and smoke test.
2. Validate each backend syntax with native tools:
   `iptables-restore --test`, `nft -c`, `pfctl -nf`, Windows test host.
3. Continue tool memory/concurrency audit, especially `vain` and `famtool`.

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

1. Validate Markdown links/MkDocs nav.
2. Keep `INTEROPERABILITY.md` synchronized with code changes.
3. Update Java/Android documentation after build validation.
4. Keep `docs/attribution-audit.md`, `docs/upstream-sync.md` and this TODO set
   current.

Exit criteria: documentation builds without a server; all upstream commits
pinned; no stale web-console instructions.

### 7. Privilege separation

1. IPC/channel/log worker foundation (implemented).
2. Supervisor process and pidfd/restart policy.
3. Storage worker and typed persistence migration.
4. Network worker and router-core event boundary.
5. Crypto worker/key-handle evaluation.
6. OpenBSD per-process policies; Linux seccomp/Landlock; Windows backend.
7. Negative capability tests and dynamic validation.

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
   session/DHT stats and error states.
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

### 8. OpenBSD and CI

1. Validate CMake/Ninja, LibreSSL, pledge/unveil, MTU and custom datadir.
2. Run manual GitHub workflows on all platforms.
3. Keep workflows manual-only.

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

- **M1:** Current build/tests green after firewall/Android edits.
- **M2:** Security sanitizer baseline clean or triaged.
- **M3:** Java interoperability matrix complete with confirmed fixes.
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

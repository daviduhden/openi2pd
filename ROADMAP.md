# Roadmap

This roadmap coordinates parallel workstreams. It is subordinate to the
requirements and current code; `TODO.md` is the authoritative task list.

## Current baseline (verified in this workspace)

- Linux/GCC 16: main CMake configure and build passed before the most recent
  Android/firewall edits.
- CTest: 16/16 passed before the firewall test was added; `bmake` full build
  passed earlier.
- TUI: live UNIX-socket session on Linux passed (state, peers, tunnels, netDb,
  logs, malformed requests, quit, cleanup).
- Tools: all 15 built; keygen/keyinfo/offlinekeys/i2pbase64/regaddr/verifyhost/
  x25519 smoke-tested.
- Android: integrated; Gradle 9.8.1 + AGP 9.4.1 configuration reached the
  missing-SDK check. No SDK/NDK, so no APK was built.
- Java I2P: not yet cloned/audited in this session; reference work is pending.

## Workstreams

### 1. Correctness and security (critical path)

Order:

1. Finish builds/tests after current edits (C++ CMake, tools, firewall test).
2. Run sanitizers and fuzz targets; fix findings.
3. Audit untrusted parsers (NTCP2/SSU2/I2NP/RouterInfo/LeaseSet/SU3).
4. Resolve TUI status data races and any Android JNI lifetime issues.

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

### 3. Android/OpenJDK 25 (blocked on SDK/NDK)

1. Install JDK 25, Android SDK 37, NDK r27+ and dependency prefixes.
2. Validate Gradle 9.8.1/AGP 9.4.1 and toolchain 25.
3. Build APK/DEX; run Android Lint and JNI tests.
4. Continue Java refactor (records, sealed types, switch expressions, lifecycle
   separation) within the Java 17 language level supported by the Android
   pipeline.
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
| Java 25 vs ART API gap | APK incompatibility | Toolchain 25, language 17, document |
| Firewall syntax differences | Broken rules | Native syntax checks, no execution |
| Upstream docs drift | Stale instructions | Pinned commits + manual sync script |

## Milestones

- **M1:** Current build/tests green after firewall/Android edits.
- **M2:** Security sanitizer baseline clean or triaged.
- **M3:** Java interoperability matrix complete with confirmed fixes.
- **M4:** Android APK builds with JDK 25 and passes lint/JNI tests.
- **M5:** OpenBSD and manual CI evidence recorded.
- **M6:** Documentation and TODO set closed with no unclassified items.

## Change control

No commits, tags or pushes are created by this work. Automatic CI stays
disabled. Any change to protocol or persisted formats requires a specification
reference and a regression test before it is considered complete.

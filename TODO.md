# Master TODO

Persistent task inventory for the Open I2Pd fork. IDs are stable. A checked
box does **not** replace the status field: dynamic validation is tracked
separately.

Subagent ownership: Architecture=coordinator; C++23=B; Security=C; IPC/M=coordinator+IPC; I2P=A; Build/DEPS=D; TUI=E; Tools/FW=F; Docs=G; Android=H; OpenBSD/I; Branding/LEGAL=J/K; CI=J; Tests=C/G/F.

Area ID map: `CPP`=B, `SEC`=C, `I2P`=A, `DEPS`/`BUILD`=D, `TUI`=E,
`TOOLS`/`FW`=F, `DOCS`=G, `ANDROID`=H, `OBSD`=I, `BRAND`/`LEGAL`=J/K,
`CI`=J, `TEST`=C/G, `PRIVSEP`=M.

Status values: `Pending`, `In progress`, `Blocked`, `Implemented / validation
pending`, `Statically reviewed`, `Validated`, `Dropped (reason)`.

Priority values: `P0` critical, `P1` high, `P2` normal, `P3` low.

## A. I2P Java interoperability

- [x] **[NET-001]** Clone the latest stable I2P Java release outside this tree and record tag/commit/date.
  - Priority P0; Status Implemented; tag `i2p-2.13.0`, commit `9134f808...`, 2026-07-24; clone in `/tmp/opencode/i2p-java`.
  - Files: `/tmp/opencode/i2p.i2p` (read-only reference); `docs/INTEROPERABILITY.md`.
  - Acceptance: tag, commit, version, date recorded; no Java code added to fork.
  - Validation: compare tag against upstream release announcements.
- [ ] **[NET-002]** Audit NTCP2 handshake, obfuscation, options, version negotiation and PQ variants against Java.
  - Priority P0; Status Pending; Depends NET-001.
  - Files: `libi2pd/NTCP2.cpp`, `NTCP2.h`; Java `router/transport/ntcp2`.
  - Acceptance: matrix rows with class/function references and any fix.
  - Validation: differential/session tests pending.
- [ ] **[NET-003]** Audit SSU2 handshake, tokens, relay, peer test, fragmentation, MTU and IPv6 against Java.
  - Priority P0; Status Pending; Depends NET-001.
  - Files: `libi2pd/SSU2*.cpp`; Java `router/transport/ssu2`.
  - Acceptance: matrix rows and fixes; OpenBSD MTU path reviewed.
- [ ] **[NET-004]** Audit I2NP message types, serialization, timestamps and malformed handling.
  - Priority P1; Status Pending; Files `libi2pd/I2NPProtocol.*`.
- [ ] **[NET-005]** Audit RouterInfo/LeaseSet/LeaseSet2/EncryptedLeaseSet parsing and validation.
  - Priority P1; Status Pending; Files `RouterInfo.cpp`, `LeaseSet.cpp`, `Identity.cpp`.
- [ ] **[NET-006]** Audit netDb/floodfill lookup, store, expiration and reseed behavior.
  - Priority P1; Status Pending; Files `NetDb.*`, `Reseed.cpp`.
- [ ] **[NET-007]** Audit tunnel build records, garlic routing and delivery semantics.
  - Priority P1; Status Pending; Files `Tunnel*`, `Garlic.cpp`.
- [ ] **[NET-008]** Audit Streaming, Datagram2/3, SAM and I2CP wire behavior used by Java clients.
  - Priority P1; Status Pending; Files `Streaming.*`, `Datagram.*`, `SAM.cpp`, `I2CP.cpp`.
- [ ] **[NET-009]** Audit ECIES-X25519-AEAD-Ratchet, signatures, key derivation and nonces.
  - Priority P0; Status Pending; Files `ECIES*`, `Crypto*`, `Signature*`, `PostQuantum*`.
- [x] **[NET-010]** Produce `docs/INTEROPERABILITY.md` with Java class/function references.
  - Priority P0; Status Implemented / dynamic validation pending; covers all major areas, not every sub-message.
- [ ] **[NET-011]** Implement justified fixes for confirmed incompatibilities.
  - Priority P0; Status Pending; Depends each finding.
- [x] **[NET-013]** Fix RouterInfo size cap to 4096 to match Java `MAX_UNCOMPRESSED_SIZE`.
  - Priority P0; Status Implemented / dynamic validation pending; `RouterInfo.h` comment references Java 2.13.0.
- [ ] **[NET-012]** Prepare differential tests and controlled Java/C++ integration harness.
  - Priority P1; Status Pending; Depends NET-002..009; must not touch public I2P network.

## B. C++23 modernization

- [x] **[CPP-001]** Require C++23 in CMake; remove C++20 fallback and obsolete preprocessor branches.
  - Status Implemented / validation pending; build passed on GCC 16 Linux.
- [ ] **[CPP-002]** Audit remaining raw owning pointers and manual `new[]`/`delete[]`.
  - Priority P1; Status In progress; Files all `libi2pd*`, `daemon`, `tools`.
- [ ] **[CPP-003]** Replace remaining avoidable C-style buffers and macros in hot paths.
  - Priority P2; Status Pending; preserve packet/crypto performance.
- [ ] **[CPP-004]** Convert tunnel/router status reads used by TUI to safe snapshots/atomics.
  - Priority P1; Status Pending; Files `Tunnel.h`, `RouterContext.h`, `TuiServer.cpp`.
- [ ] **[CPP-005]** Add `std::expected`/`optional` to recoverable configuration/filesystem APIs where safe.
  - Priority P2; Status Pending.
- [ ] **[CPP-006]** Consider namespace/enum cleanup and strongly typed protocol constants without ABI breaks.
  - Priority P3; Status Pending.

## C. Security and concurrency

- [x] **[SEC-001]** Fix base64 lazy-table race and invalid alphabet/padding acceptance.
  - Status Implemented; `tests/test-base-64` passed on Linux.
- [x] **[SEC-002]** Harden SU3 header parsing and bounded allocations in reseed.
  - Status Implemented / validation pending; truncated fixture test still missing.
- [x] **[SEC-003]** Lock netDb counters and reseed state.
  - Status Implemented; TSan pending.
- [x] **[SEC-004]** Add lock-safe peer snapshot and use it in TUI/I2PControl.
  - Status Implemented; TSan pending.
- [x] **[SEC-005]** Strict HTTP header/Content-Length/chunk/URL port parsing.
  - Status Implemented; unit tests passed on Linux.
- [x] **[SEC-006]** Use constant-time comparison for integrity hashes.
  - Status Statically reviewed; no timing test.
- [ ] **[SEC-007]** Add truncated/malformed SU3 regression fixture.
  - Priority P1; Status Pending.
- [ ] **[SEC-008]** Run and triage ASan/UBSan/TSan; fix findings.
  - Priority P0; Status Blocked on user dynamic validation; commands in `docs/testing.md`.
- [ ] **[SEC-009]** Fuzz TUI/HTTP parsers and add NTCP2/SSU2/I2NP/RouterInfo/LeaseSet/SU3 targets.
  - Priority P1; Status Partially implemented (two fuzzers prepared), not run.
- [ ] **[SEC-010]** Audit all JNI local/global references, exceptions and ownership.
  - Priority P0; Status In progress; Files `android/app/src/main/cpp`.
- [ ] **[SEC-011]** Audit Android exported components, intents, permissions and storage.
  - Priority P0; Status In progress; see `docs/android-modernization.md`.
- [ ] **[SEC-012]** Validate TUI IPC peer-UID rejection and malformed-client behavior dynamically.
  - Priority P1; Status Pending; no second UID available.
- [ ] **[SEC-013]** Review cryptographic buffer wipes and secret lifetime in tools and daemon.
  - Priority P1; Status In progress.

## D. Build, dependencies, packaging

- [x] **[BUILD-001]** Remove GNU make dependency; POSIX make front-end delegates to CMake/Ninja.
  - Status Implemented; `bmake` full build passed before later changes.
- [x] **[BUILD-002]** Require C++23 and explicit dependency minimums.
  - Status Implemented; `docs/dependencies.md`.
- [x] **[BUILD-003]** Replace source globs with explicit lists and target-scoped CMake.
  - Status Implemented; CMake configure/build passed on Linux.
- [x] **[BUILD-004]** Integrate tests with CTest and add TUI/tools tests.
  - Status Implemented; 16/16 passed before firewall test; firewall test built.
- [x] **[BUILD-005]** Re-run full CMake and POSIX-make builds after all recent changes.
  - Priority P0; Status Validated on Linux/GCC 16: CMake build + 17/17 CTest; `bmake` full build + 17/17 CTest.
- [ ] **[BUILD-006]** Validate CMake install and package config export.
  - Priority P1; Status Partially validated (install passed earlier); re-check.
- [ ] **[BUILD-007]** Validate Debian/RPM packaging with CMake/Ninja.
  - Priority P2; Status Pending.
- [ ] **[BUILD-008]** Validate OpenBSD CMake/Ninja/LibreSSL build and port integration.
  - Priority P0; Status Blocked on user/OpenBSD environment.
- [ ] **[BUILD-009]** Validate Windows build and toolchain.
  - Priority P1; Status Pending.
- [ ] **[BUILD-010]** Validate Android Gradle/JNI build with SDK/NDK and dependency prefix.
  - Priority P0; Status Blocked on SDK/NDK.

## E. Web console removal and TUI

- [x] **[TUI-001]** Remove HTTP admin console, resources, options, Win32 menu and docs.
  - Status Implemented; source removed and build passed.
- [x] **[TUI-002]** Implement versioned read-only UNIX-socket IPC with bounds/UID checks.
  - Status Implemented; live Linux session tested.
- [x] **[TUI-003]** Implement ncurses client with status/peers/tunnels/netDb/logs.
  - Status Implemented; live session tested on Linux.
- [x] **[TUI-004]** Show local destination b32 addresses in TUI.
  - Status Implemented.
- [ ] **[TUI-005]** Add safe administrative commands via daemon main-loop queue.
  - Priority P2; Status Pending; not required for read-only v1.
- [ ] **[TUI-006]** Test resize, reconnect, signal handling and long-running log streaming.
  - Priority P1; Status Pending.
- [ ] **[TUI-007]** Re-run TUI tests after Android/tools/docs integration.
  - Priority P1; Status Pending.

## F. Tools

- [x] **[TOOL-001]** Import all 15 i2pd-tools and build them against in-tree libraries.
  - Status Implemented; all tools linked; smoke test passed.
- [x] **[TOOL-002]** Remove tool submodule, GNU make, dependency-download script.
  - Status Implemented.
- [x] **[TOOL-003]** Modernize key/base64/file handling and fix verifyhost/regaddr leaks.
  - Status Implemented / validation pending.
- [x] **[TOOL-004]** Add tools smoke test to CTest.
  - Status Implemented; passed on Linux.
- [ ] **[TOOL-005]** Audit all remaining tools (`vain`, `autoconf_i2pd`, `keyinfo`, `famtool`, family/address tools) for memory/concurrency/security.
  - Priority P1; Status In progress.
- [ ] **[TOOL-006]** Add per-tool regression tests beyond smoke coverage.
  - Priority P2; Status Pending.
- [x] **[FIREWALL-001]** Refactor routerinfo into endpoint model and four backends.
  - Status Implemented; unit test passed.
- [x] **[FIREWALL-002]** Preserve legacy `-f` output.
  - Status Implemented; regression test passed.
- [x] **[FIREWALL-003]** Add nftables family/table/chain options and identifier validation.
  - Status Implemented.
- [x] **[FIREWALL-004]** Add Windows PowerShell and PF renderers.
  - Status Implemented.
- [x] **[FIREWALL-005]** Add CLI help, conflict validation and deduplication.
  - Status Implemented.
- [x] **[FIREWALL-006]** Run `test-routerinfo-firewall` and tools smoke test.
  - Priority P0; Status Validated on Linux/GCC 16; 17/17 CTest passed.
- [ ] **[FIREWALL-007]** Dynamic syntax validation with `iptables-restore --test`, `nft -c`, `pfctl -nf`, Windows test host.
  - Priority P1; Status Blocked on user/runtime environment.

## G. Documentation

- [x] **[DOC-001]** Import i2pd_docs_en Markdown, MkDocs config and local images.
  - Status Implemented; no HTML pages existed.
- [x] **[DOC-002]** Adapt configuration/build docs for C++23, CMake/POSIX Make, TUI and no web console.
  - Status In progress; some upstream pages still under review.
- [x] **[DOC-003]** Add per-tool documentation and tool index.
  - Status Implemented.
- [x] **[DOC-004]** Add architecture, dependencies, C++23, build, TUI, security and testing reports.
  - Status Implemented.
- [ ] **[DOC-005]** Validate all internal Markdown links and MkDocs navigation.
  - Priority P2; Status Pending.
- [ ] **[DOC-006]** Complete `docs/INTEROPERABILITY.md` with Java class/function references.
  - Priority P0; Status In progress; Depends NET-001.
- [x] **[DOC-007]** Record upstream docs/tools commits and manual sync procedure.
  - Status Implemented; `docs/upstream-sync.md`, `contrib/upstream/`.
- [ ] **[DOC-008]** Update Android architecture/JDK 25 documentation and validation status.
  - Priority P1; Status In progress.

## H. Android (OpenJDK 25)

- [x] **[AND-001]** Import app-android sources and JNI into `android/`.
  - Status Implemented.
- [x] **[AND-002]** Replace ndk-build with CMake/JNI target; remove submodule.
  - Status Implemented; native build not executed.
- [x] **[AND-003]** Remove WebView admin console, `[http]` options and broad storage permissions.
  - Status Implemented.
- [x] **[AND-004]** Add native status summary and remove web-console JNI method.
  - Status Implemented / validation pending.
- [x] **[AND-005]** Upgrade Gradle wrapper to 9.8.1 and AGP to 9.4.1.
  - Status Implemented; Gradle 9.8.1 executed and configuration reached SDK-location check.
- [x] **[AND-006]** Require JDK 25+ at Gradle evaluation and via Java toolchain.
  - Status Implemented; running JDK 27; toolchain 25 resolution pending.
- [ ] **[AND-007]** Verify Gradle/AGP with OpenJDK 25 installed, `compileSdk 37`, NDK r27+.
  - Priority P0; Status Blocked on SDK/NDK/JDK 25.
- [ ] **[AND-008]** Build APK/DEX and run Android Lint/Error Prone.
  - Priority P0; Status Blocked on SDK.
- [ ] **[AND-009]** Refactor Java sources (records, switch expressions, sealed types, lifecycle separation).
  - Priority P1; Status In progress.
- [ ] **[AND-010]** Audit JNI references, exceptions, threads and shutdown races.
  - Priority P0; Status In progress.
- [ ] **[AND-011]** Test service lifecycle, rotation, background execution and restart.
  - Priority P1; Status Pending.
- [ ] **[AND-012]** Document exact JDK/Gradle/AGP/SDK/NDK matrix and Java 25 vs ART limitations.
  - Priority P1; Status In progress; `docs/android-modernization.md`.

## I. OpenBSD

- [x] **[OBSD-001]** Preserve pledge/unveil and add TUI socket path without new promises.
  - Status Implemented / validation pending.
- [ ] **[OBSD-002]** Validate pledge/unveil with non-default user/datadir and TUI socket.
  - Priority P0; Status Blocked on OpenBSD environment.
- [ ] **[OBSD-003]** Re-test SSU2 MTU discovery and IPv4/IPv6 on OpenBSD.
  - Priority P0; Status Pending.
- [ ] **[OBSD-004]** Validate LibreSSL build (no PQ assumptions) and port recipe.
  - Priority P0; Status Pending.

## J. Attribution, CI and governance

- [x] **[GOV-001]** Remove obsolete personal author/maintainer credits while preserving copyright/license notices.
  - Status Implemented; `docs/attribution-audit.md`.
- [x] **[GOV-002]** Keep all GitHub Actions manual-only (`workflow_dispatch`).
  - Status Implemented; no automatic triggers.
- [ ] **[GOV-003]** Update CI to OpenJDK 25 for Android and add app/tool/docs checks when runnable.
  - Priority P1; Status In progress.
- [ ] **[GOV-004]** Re-run manual CI on Linux/OpenBSD/Windows/Android and record results.
  - Priority P0; Status Blocked on user.
- [ ] **[GOV-005]** Keep `TODO.md`, `ROADMAP.md`, `PROGRESS.md` current after each work cycle.
  - Priority P0; Status In progress.

## K. Rebranding and identity

- [x] **[REBRAND-000]** Audit brand/identity references and create `docs/REBRANDING.md`.
  - Status Implemented; static classification complete.
- [x] **[REBRAND-001]** Rename Android applicationId/namespace to `org.openi2pd.router` and update JNI/package references.
  - Status Implemented / compile validation pending (no SDK).
- [x] **[REBRAND-002]** Replace legacy visual identity with neutral placeholder icons; remove old screenshots/logo references.
  - Status Implemented; PNG/ICO/vector generated; dynamic rendering not checked.
- [x] **[REBRAND-003]** Remove personal upstream install/donation/team links from Android/docs.
  - Status Implemented.
- [x] **[REBRAND-004]** Update systemd description and add `open-i2pd.service` alias.
  - Status Implemented; migration documented.
- [ ] **[REBRAND-005]** Decide and implement executable rename (`i2pd` → `open-i2pd`) with compatibility symlink/wrapper.
  - Priority P1; Status Pending; requires packaging/script migration.
- [ ] **[REBRAND-006]** Add dual-read migration for `i2pd.conf` and data directories.
  - Priority P1; Status Pending; affects daemon FS/config, OpenBSD unveil, Android assets.
- [ ] **[REBRAND-007]** Rename Debian/RPM package identities with Provides/Replaces and migration.
  - Priority P2; Status Pending; packaging tests required.
- [ ] **[REBRAND-008]** Replace placeholder icon with a defined project identity when supplied.
  - Priority P3; Status Blocked on design specification.
- [ ] **[REBRAND-009]** Add Android migration from old applicationId/data directory.
  - Priority P1; Status Pending; requires device test.
- [ ] **[REBRAND-010]** Scan built binaries/APKs for residual brand strings.
  - Priority P2; Status Blocked on builds.

## M. Privilege separation (multiprocess)

- [x] **[PRIVSEP-001]** Audit current process capabilities and document gaps.
  - Status Implemented; `docs/security/privilege-separation.md`, `process-capabilities.md`.
- [x] **[PRIVSEP-002]** Implement versioned, bounded internal IPC framing and channel (AF_UNIX/SOCK_SEQPACKET, peer UID, CLOEXEC).
  - Status Implemented / protocol tests passed on Linux; malformed/fuzz pending.
- [x] **[PRIVSEP-003]** Implement spawned log worker with structured records and OpenBSD pledge/unveil policy.
  - Status Implemented; live Linux test confirmed core had no log fd and worker had the only log write fd.
- [x] **[PRIVSEP-004]** Add `--privsep-worker` exec entry point and parent spawn/configure/shutdown.
  - Status Implemented; no auto-restart yet. `test-privsep-log` passed.
- [ ] **[PRIVSEP-005]** Add dedicated supervisor process with pidfd/waitpid, restart backoff and channel policy.
  - Priority P0; Status Pending.
- [ ] **[PRIVSEP-006]** Implement storage worker with typed allowlisted operations; migrate netDb/profiles/keys persistence.
  - Priority P0; Status Pending; core must lose persistent-write capability.
- [ ] **[PRIVSEP-007]** Implement network worker owning NTCP2/SSU2 sockets; core must lose direct network access.
  - Priority P0; Status Pending; state-machine ownership and nonce safety must be designed first.
- [ ] **[PRIVSEP-008]** Implement crypto worker/key handles for long-term operations, or document the boundary if latency makes it non-viable.
  - Priority P1; Status Pending.
- [ ] **[PRIVSEP-009]** Add per-process OpenBSD pledge/unveil policies for storage/network/core/crypto/supervisor.
  - Priority P0; Status Pending.
- [ ] **[PRIVSEP-010]** Add Linux seccomp/Landlock/namespaces/pidfd/cgroup limits and negative capability tests.
  - Priority P1; Status Pending.
- [ ] **[PRIVSEP-011]** Implement Windows ncalrpc/Named Pipes/Restricted token backend.
  - Priority P2; Status Pending.
- [ ] **[PRIVSEP-012]** Define Android process/UID limitations and migration.
  - Priority P2; Status Pending.
- [ ] **[PRIVSEP-013]** Add IPC fuzzing, queue saturation, malformed message, descriptor-transfer and restart tests.
  - Priority P1; Status Pending.
- [ ] **[PRIVSEP-014]** Enforce no silent fallback on OpenBSD and remove the monolithic path for enabled privsep.
  - Priority P0; Status Pending.

## Known defects / findings not yet fixed

- [ ] **[DEF-001]** Non-atomic tunnel/router status fields read by TUI (data race under C++ memory model).
- [ ] **[DEF-002]** No dynamic tests for SU3 malformed input; add fixture.
- [ ] **[DEF-003]** Android app not compiled; Java refactor incomplete.
- [ ] **[DEF-004]** `routerinfo` firewall backends not yet dynamically syntax-checked.
- [ ] **[DEF-005]** Full Java I2P differential audit not yet performed.
- [ ] **[DEF-006]** Android manifest still allows cleartext? Removed; verify merged manifest.
- [ ] **[DEF-007]** `tools/vain` retains raw buffer arrays; found/hash counters/key selection were made atomic/unique, deeper RAII rewrite pending.
- [ ] **[DEF-008]** `famtool` family certificate/private-key handling needs deeper review.
- [ ] **[DEF-009]** Core process still combines network, filesystem and long-term keys; only logging is separated.
- [ ] **[DEF-010]** Log worker has no automatic restart/backoff and no explicit queue quota.

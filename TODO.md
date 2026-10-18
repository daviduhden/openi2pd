# Master TODO

Persistent task inventory for the Open I2Pd fork. IDs are stable. A checked
box does **not** replace the status field: dynamic validation is tracked
separately.

Subagent ownership: Architecture=coordinator; C++23=B; Security=C; IPC/M=coordinator+IPC; I2P=A; Build/DEPS=D; TUI=E; Torrent/RPC=Torrent; Perl=Perl; Tools/FW=F; Docs=G; Android=H; OpenBSD/I; Branding/LEGAL=J/K; CI=J; Tests=C/G/F/Torrent.

Area ID map: `CPP`=B, `SEC`=C, `I2P`=A, `DEPS`/`BUILD`=D, `TUI`=E,
`TOOLS`/`FW`=F, `DOCS`=G, `ANDROID`=H, `OBSD`=I, `BRAND`/`LEGAL`=J/K,
`CI`=J, `TEST`=C/G/Torrent, `PRIVSEP`=M, `TORRENT`=Torrent, `PERL`=Perl.

Status values: `Pending`, `In progress`, `Blocked`, `Implemented / validation
pending`, `Statically reviewed`, `Validated`, `Dropped (reason)`.

Priority values: `P0` critical, `P1` high, `P2` normal, `P3` low.

Responsible: the coordinator owns integration; each task is executed by the
subsystem owner in the ownership table unless a task says otherwise. The
`Files` field identifies the review boundary.

## A. I2P Java interoperability

- [x] **[NET-001]** Clone the latest stable I2P Java release outside this tree and record tag/commit/date.
  - Priority P0; Status Implemented; tag `i2p-2.13.0`, commit `9134f808...`, 2026-07-24; read-only clone outside the tree (not bundled).
  - Files: external read-only Java reference; `docs/INTEROPERABILITY.md`.
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
- [ ] **[NET-014]** Fix `NetDb::SaveUpdated()` iterating `m_RouterInfos` without the mutex; design snapshot/object locking.
  - Priority P0; Status Pending; `NetDb.cpp`; static finding from I2P audit.
- [ ] **[SEC-014]** Review malformed RouterInfo address/property parameter parsing and SSU2 address-family mismatch.
  - Priority P1; Status Pending; findings documented in `docs/INTEROPERABILITY.md`.
- [ ] **[OPENBSD-SBX-1]** Fix OpenBSD reseed URL canonicalization in `Daemon.cpp`; `AbsolutePath()` currently prefixes non-absolute URLs.
  - Priority P0; Status Implemented / dynamic validation pending; URL is left untouched and the unveil setup skips it.
- [ ] **[OPENBSD-SBX-2]** Register/implement `unix.handle_sigtstp` on OpenBSD or remove the dead TSTP/CONT path.
  - Priority P1; Status Implemented / static; option is now registered for OpenBSD; dynamic signal test pending.
- [ ] **[OPENBSD-SBX-3]** Decide whether a custom `openbsd.pledge_file` may bypass the fixed startup pledge; document or enforce.
  - Priority P1; Status Pending.
- [ ] **[OPENBSD-PID-1]** Implement a readiness check for the daemonized rc.d start; `--daemon` currently exits before pidfile/pledge/transport readiness.
  - Priority P1; Status Pending.
- [ ] **[OPENBSD-LOG-1]** Synchronize `Log::Reopen()` with the logger thread for SIGUSR1 rotation.
  - Priority P1; Status Pending.
- [ ] **[OPENBSD-VAL-1]** Execute `docs/security/openbsd-validation.md` on a real OpenBSD host (pledge/unveil, pidfile, signals, SSU2/MTU).
  - Priority P0; Status Pending; dynamic validation required.

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
  - Priority P0; Status Validated on Linux/GCC 16 at Session 7 checkpoint: CMake build + 20/20 CTest; later builds/tests frozen by user and delegated to CI.
- [ ] **[BUILD-006]** Validate CMake install and package config export.
  - Priority P1; Status Partially validated (install passed earlier); re-check.
- [ ] **[BUILD-007]** Validate Debian/RPM packaging with CMake/Ninja.
  - Priority P2; Status Pending.
- [ ] **[BUILD-008]** Validate OpenBSD CMake/Ninja/LibreSSL build and port integration.
  - Priority P0; Status Blocked on user/OpenBSD environment.
- [ ] **[BUILD-009]** Validate Windows build and toolchain.
  - Priority P1; Status Pending.
- [ ] **[BUILD-010]** Validate Android Gradle/JNI build with SDK/NDK and dependency prefix.
  - Priority P0; Status Partially validated: Java compile passed; APK/native/Lint deferred to CI.

## E. Web console removal and TUI

- [x] **[TUI-001]** Remove HTTP admin console, resources, options, Win32 menu and docs.
  - Status Implemented; source removed and build passed.
- [x] **[TUI-002]** Implement versioned read-only UNIX-socket IPC with bounds/UID checks.
  - Status Implemented; live Linux session tested.
- [x] **[TUI-003]** Implement ncurses client with status/peers/tunnels/netDb/logs.
  - Status Implemented; live session tested on Linux.
- [x] **[TUI-004]** Show local destination b32 addresses in TUI.
  - Status Implemented.
- [ ] **[TUI-005]** Add safe administrative router commands via the daemon main-loop queue.
  - Priority P2; Status Torrent actions implemented over local IPC; router start/stop/config actions and main-loop mediation remain pending.
- [ ] **[TUI-006]** Test resize, reconnect, signal handling and long-running log streaming.
  - Priority P1; Status Pending.
- [ ] **[TUI-008]** Add a bounded, validated configuration view/editor to the TUI without exposing keys or raw files.
  - Priority P2; Status Pending; current settings remain in the Android/native config paths.
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
  - Priority P1; Status Implemented / validation pending; `vain` ownership/parsing/threading hardened; firewall/tool regressions prepared; address tools still pending dynamic tests.
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
- [x] **[DOC-005]** Validate all internal Markdown links and MkDocs navigation.
  - Priority P2; Status Static validation passed: 0 missing relative links; MkDocs not executed.
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
  - Priority P0; Status Java compile validated; APK/native build deferred to CI.
- [ ] **[AND-008]** Build APK/DEX and run Android Lint/Error Prone.
  - Priority P0; Status Deferred to CI.
- [ ] **[AND-009]** Refactor Java sources (records, switch expressions, sealed types, lifecycle separation).
  - Priority P1; Status Implemented / static review; lifecycle/service/settings hardening added; compile/device validation deferred to CI.
- [ ] **[AND-010]** Audit JNI references, exceptions, threads and shutdown races.
  - Priority P0; Status Statically reviewed; JNI status/log methods and host syntax check passed; device/CI tests pending.
- [ ] **[AND-011]** Test service lifecycle, rotation, background execution and restart.
  - Priority P1; Status Pending; pure-JVM parser/format/config tests prepared; FGS/rotation/boot device tests pending.
- [ ] **[AND-012]** Document exact JDK/Gradle/AGP/SDK/NDK matrix and Java 25 vs ART limitations.
  - Priority P1; Status Implemented; Java 25, FGS type, auto-start and validation limitations documented.
- [ ] **[AND-013]** Evaluate/implement an isolated router process with Binder/AIDL or a bounded same-process alternative; do not expose raw JNI or storage to UI.
  - Priority P1; Status Pending; current `RouterService` is same-process LocalBinder and documented as an intermediate stage.
- [ ] **[AND-014]** Validate the `specialUse`/`dataSync` foreground-service split, Android 15 timeout policy, boot restrictions and Play Console declaration.
  - Priority P0; Status In progress; manifest/service changes implemented; real device/Play review pending.

## I. OpenBSD

- [x] **[OBSD-001]** Preserve pledge/unveil and add TUI socket path without new promises.
  - Status Implemented / validation pending.
- [ ] **[OBSD-002]** Validate pledge/unveil with non-default user/datadir and TUI socket.
  - Priority P0; Status Blocked on OpenBSD environment; static UnixDaemon/pidfile hardening implemented.
- [ ] **[OBSD-003]** Re-test SSU2 MTU discovery and IPv4/IPv6 on OpenBSD.
  - Priority P0; Status Pending.
- [ ] **[OBSD-004]** Validate LibreSSL build (no PQ assumptions) and port recipe.
  - Priority P0; Status Pending.
- [x] **[OBSD-005]** Add the OpenBSD rc.d service script and login class from `wip-openbsd-ports` to `contrib/openbsd`.
  - Priority P1; Status Implemented / runtime validation in CI; `i2pd.rc`, `i2pd.login`; `rcctl` instructions documented.

## J. Attribution, CI and governance

- [x] **[GOV-001]** Remove obsolete personal author/maintainer credits while preserving copyright/license notices.
  - Status Implemented; `docs/attribution-audit.md`.
- [x] **[GOV-002]** Keep all GitHub Actions manual-only (`workflow_dispatch`).
  - Status Implemented; no automatic triggers.
- [ ] **[GOV-003]** Update CI to OpenJDK 25 for Android and add app/tool/docs checks when runnable.
  - Priority P1; Status Partially implemented; Java 25 CI present; app/Lint/Perl checks to add.
- [ ] **[GOV-004]** Re-run manual CI on Linux/OpenBSD/Windows/Android and record results.
  - Priority P0; Status Blocked on user.
- [ ] **[GOV-005]** Keep `TODO.md`, `ROADMAP.md`, `PROGRESS.md` current after each work cycle.
  - Priority P0; Status In progress.
- [x] **[GOV-006]** Run zizmor and yamllint on `.github/workflows`; pin actions to commit SHAs, disable checkout credential persistence and add `.yamllint.yml`.
  - Priority P0; Status Implemented; zizmor reports no findings (5 suppressed); yamllint clean. CI triggers remain manual-only.

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
  - Status Implemented / static hardening; expanded malformed/EOF/truncation tests prepared; execution pending.
- [x] **[PRIVSEP-003]** Implement spawned log worker with structured records and OpenBSD pledge/unveil policy.
  - Status Implemented; live Linux test confirmed core had no log fd and worker had the only log write fd; write/flush/duplicate/NUL hardening added.
- [x] **[PRIVSEP-004]** Add `--privsep-worker` exec entry point and parent spawn/configure/shutdown.
  - Status Implemented / static hardening; `test-privsep-log` prepared; bounded in-process restart monitor added.
- [ ] **[PRIVSEP-005]** Add dedicated supervisor process with pidfd/waitpid, restart backoff and channel policy.
  - Priority P0; Status In progress; bounded in-process monitor/backoff implemented for the log worker; a separate supervisor process is still pending.
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
  - Priority P1; Status In progress; malformed/EOF/restart tests prepared; fuzzing/saturation/descriptor transfer pending.
- [ ] **[PRIVSEP-014]** Enforce no silent fallback on OpenBSD and remove the monolithic path for enabled privsep.
  - Priority P0; Status Pending.

## ID registry and aliases

The project keeps the historical stable IDs to avoid losing traceability. The
requested subsystem prefixes map as follows; new work uses the requested prefix
when there is no older ID.

| Requested | Existing / meaning |
|---|---|
| `ARCH-*` | Architecture/coordinator tasks; see `ARCH-001..` below |
| `CPP-*` | existing C++23 tasks |
| `SEC-*` | existing memory/security tasks |
| `I2P-*` | alias of existing `NET-*` |
| `IPC-*` | existing `PRIVSEP-*` plus `TORRENT-IPC-*` |
| `OBSD-*` | existing OpenBSD tasks |
| `LINUX-*` | new Linux-specific tasks (see below) |
| `WIN-*` | new Windows-specific tasks (see below) |
| `BUILD-*` | existing build tasks |
| `TUI-*` | existing TUI/Torrent TUI tasks |
| `TORRENT-*` | existing torrent tasks (`TORRENT-TUI-*`/`TORRENT-IPC-*`/`TORRENT-SEC-*`) |
| `PERL-*` | existing Perl tasks (`PERL-SEC-*`) |
| `ANDROID-*` | alias of existing `AND-*` |
| `TOOLS-*` | alias of existing `TOOL-*` |
| `DOCS-*` | alias of existing `DOC-*` |
| `FW-*` | alias of existing `FIREWALL-*` |
| `BRAND-*` | alias of existing `REBRAND-*` |
| `LEGAL-*` | new license/provenance tasks (see below) |
| `CI-*` | alias of existing `GOV-*` |
| `TEST-*` | existing tests plus new tasks |

## Q. Architecture and cross-platform follow-up

- [ ] **[ARCH-001]** Keep the shared-interface ownership map current for `RouterContext`, transport APIs, crypto APIs, `TuiProtocol`, `privsep/Protocol`, JNI and CMake.
  - Priority P0; Status In progress; coordinator owns integration; no unilateral API changes.
- [ ] **[ARCH-002]** Reconcile documented target architecture with actual process/worker implementation; remove claims of completed separation.
  - Priority P0; Status In progress; `docs/security/privilege-separation.md`, `docs/architecture.md`.
- [ ] **[ARCH-003]** Define typed storage-worker operations and ownership before migrating netDb/profile/keys persistence.
  - Priority P0; Status Pending; depends PRIVSEP-006.
- [ ] **[LINUX-001]** Add seccomp-BPF/Landlock/capability/cgroup policy for workers with negative tests.
  - Priority P0; Status Pending; no local execution.
- [ ] **[LINUX-002]** Validate peer credentials, pidfd, close_range and SOCK_SEQPACKET behavior on current Linux.
  - Priority P1; Status Pending.
- [ ] **[WIN-001]** Implement ncalrpc/Named Pipes worker transport with DACL, restricted token/AppContainer and Job Objects.
  - Priority P1; Status Pending.
- [ ] **[WIN-002]** Add Windows negative IPC/credential and firewall validation.
  - Priority P1; Status Pending.
- [ ] **[TOOLS-007]** Continue tool security/regression audit after `famtool`/`keyinfo`/`autoconf_i2pd`; include address/offline-key tools.
  - Priority P1; Status In progress; `vain` and firewall regressions added statically; address/offline-key tools still pending.
- [ ] **[TOOL-008]** Add a dedicated `test-vain` target if the pure base32/nonce helpers are factored out; current coverage is CLI/negative paths only.
  - Priority P2; Status Pending; requires coordinator CMake change.
- [ ] **[FW-008]** Native syntax validation for iptables/nft/PF/PowerShell backends in CI or dedicated hosts.
  - Priority P1; Status Pending.
- [x] **[DOCS-009]** Validate all Markdown links/MkDocs navigation after torrent/Perl/Android docs.
  - Priority P1; Status Static link check passed; MkDocs rendering deferred to CI.
- [ ] **[BRAND-011]** Complete executable/package/data-path migration decisions without breaking technical compatibility.
  - Priority P1; Status Pending; see REBRAND-005..007.
- [ ] **[LEGAL-001]** Audit new torrent/Perl/Android/CI files for required copyright, license and provenance notices.
  - Priority P0; Status In progress; preserve BSD-3-Clause and upstream notices.
- [ ] **[CI-001]** Keep workflows `workflow_dispatch`-only, actions SHA-pinned, checkout credentials disabled and workflow lint clean.
  - Priority P0; Status Implemented; zizmor/yamllint clean; re-run after workflow edits.

## N. BitTorrent native console and TUI

- [ ] **[TORRENT-001]** Inventory all BitTorrent engine, RPC, web/JS, HTML/CSS, config, docs and tests; map every JS/RPC action to C++ behavior.
  - Priority P0; Status Implemented / validation pending; files `libi2pd_client/Torrents*`, `contrib/torrents/web/**`, `docs/tutorials/Filesharing/**`.
  - Acceptance: functionality matrix with implemented/depends-on-browser/unsupported verdicts; no deletion before mapping.
- [ ] **[TORRENT-002]** Define typed snapshot/control interfaces in C++23, separate engine, sessions, persistence, control API and presentation.
  - Priority P0; Status Implemented / validation pending; files new `libi2pd_client/TorrentsControl.*`, `TorrentsTunnel.*`.
  - Acceptance: no TUI access to `Torrent`/`PeerConnection` internals; bounded service-thread snapshots; `std::expected`/`std::optional` errors.
- [ ] **[TORRENT-003]** Implement torrent administration through the local TUI IPC, not HTTP/JSON-RPC or direct engine calls.
  - Priority P0; Status Implemented / validation pending; files `libi2pd/TuiProtocol.h`, `daemon/TuiServer.*`, `tui/**`.
  - Acceptance: typed messages for list/details/start/stop/reannounce/remove/add/session/DHT; bounded requests; UID-gated socket.
- [ ] **[TORRENT-004]** Add native ncursesw Torrents view with sortable/filterable table, selection, paging, resize and Unicode-safe long names.
  - Priority P0; Status Implemented / validation pending; files `tui/main.cpp`.
  - Acceptance: name/state/size/progress/rates/peers/ratio/ETA/error; empty/disconnected states; no main-thread engine calls.
- [ ] **[TORRENT-005]** Add details views for general/files/peers/trackers/session/DHT/errors.
  - Priority P0; Status Implemented / validation pending; files `tui/main.cpp`.
  - Acceptance: all data available in the engine is shown; unsupported engine features marked unavailable, not invented.
- [ ] **[TORRENT-006]** Add `.torrent` file and magnet entry, start/pause/resume/stop, reannounce, remove, remove-with-files and explicit confirmations.
  - Priority P0; Status Implemented / validation pending; files `tui/main.cpp`, `daemon/TuiServer.cpp`.
  - Acceptance: keyboard-only; destructive delete requires explicit confirmation; no shell; bounded paths/magnets.
- [ ] **[TORRENT-007]** Integrate session/DHT statistics and rate/ETA formatting; update without blocking or unbounded redraw.
  - Priority P1; Status Implemented / validation pending.
- [ ] **[TORRENT-008]** Remove torrent HTML/CSS/JS console and all enabling config/server code after parity mapping.
  - Priority P0; Status Implemented / validation pending; files `contrib/torrents/web/**`, `TorrentsRPC.*`, `ClientContext.cpp`, `TorrentsTunnel.*`.
  - Acceptance: no browser assets, no web route, no `web` option; JSON-RPC kept only as separately audited optional compatibility.
- [ ] **[TORRENT-009]** Audit Transmission-compatible JSON-RPC: optional/off by default, loopback-only, auth, no wildcard CORS, bounded bodies, same validated operations, documented incompatibilities.
  - Priority P0; Status Implemented / validation pending; files `TorrentsRPC.*`, `ClientContext.cpp`.
- [ ] **[TORRENT-010]** Preserve I2P-only torrent operation: no clearnet tracker/peer/DHT fallback, no public-IP advertisement, no unnecessary identifiers.
  - Priority P0; Status Statically reviewed / dynamic capture pending; review DNS, trackers, DHT, magnet, outgoing connections.
- [ ] **[TORRENT-011]** Prepare deterministic tests for control serialization, TUI actions, malformed requests, path/containment, parser bounds and RPC limits.
  - Priority P0; Status Partially implemented; bencode/protocol tests pass on Linux; control/TUI/RPC integration tests pending. CTest/TUI integration; no live I2P network required for unit tests.
- [ ] **[TORRENT-012]** Evaluate advanced operations (file selection/priorities/queueing) against real engine support; implement only justified features.
  - Priority P2; Status Pending; do not advertise unsupported capabilities.
- [ ] **[TORRENT-IPC-001]** Define typed administrative torrent messages for future network/storage/core workers.
  - Priority P0; Status Implemented / validation pending; no HTTP dependency between TUI and engine.
- [ ] **[TORRENT-IPC-002]** Bound control queues and avoid large piece copies; snapshots only, no raw storage paths.
  - Priority P1; Status Partially implemented; per-request limits and bounded waits; queue/backpressure review pending.
- [ ] **[TORRENT-IPC-003]** Enforce network/storage separation for torrent workers (no process both connects externally and writes persistent data).
  - Priority P0; Status Blocked on PRIVSEP-006/007; current daemon remains monolithic; do not claim invariant until implemented and tested.
- [ ] **[TORRENT-IPC-004]** Add Android/Windows/OpenBSD IPC mapping for torrent control without bypassing platform restrictions.
  - Priority P2; Status Pending; Android currently same-process service; document staged path.
- [ ] **[TORRENT-IPC-005]** Remove unbounded `.wait()` calls on the JSON-RPC I/O thread for torrent-get/set/DHT snapshots; use bounded waits/timeouts.
  - Priority P1; Status Pending; add and remove/add paths are marshalled; read/set paths still need review.
- [ ] **[TORRENT-SEC-001]** Harden bencode/torrent parsing: bounds, depth, integer/length overflow, piece/file counts, duplicate/colliding paths and allocation limits.
  - Priority P0; Status Implemented / fuzz pending; files `Torrents.cpp`, tests.
- [ ] **[TORRENT-SEC-002]** Harden filesystem operations: traversal, absolute paths, `..`, symlinks/hardlinks, permissions, delete-local-data containment and TOCTOU.
  - Priority P0; Status Implemented / TOCTOU residual risk documented; files `TorrentsTunnel.cpp`, `Torrents.cpp`.
- [ ] **[TORRENT-SEC-003]** Audit malicious peers, message lengths, fragmentation, piece verification, partial writes, resume integrity and memory/open-file bounds.
  - Priority P0; Status Statically reviewed / sanitizer-fuzz pending; dynamic/fuzz validation pending.
- [ ] **[TORRENT-SEC-004]** Remove browser/clearnet exposure and document RPC threat model and incompatible changes.
  - Priority P0; Status Implemented / dynamic validation pending; `docs/torrent/security.md`.
- [ ] **[TORRENT-SEC-005]** Validate magnet info-hash length/alphabet (hex/base32), reject partial hashes, and bound startup `.torrent` loading.
  - Priority P0; Status Implemented / static review; tests for bounded file reads added; magnet runtime test pending.

## O. Perl migration of project scripts

- [ ] **[PERL-001]** Recursively inventory maintained Python scripts and consumers (shebangs, Make/CMake/Gradle/workflows), excluding third-party/generated content.
  - Priority P0; Status Implemented; current finding: only `tools/scripts/i2pdctl`.
  - Acceptance: table script -> Perl equivalent -> consumers -> validation; no unclassified Python.
- [ ] **[PERL-002]** Rewrite `i2pdctl` in portable Perl 5, preserving CLI, env defaults, JSON-RPC semantics, output and exit codes.
  - Priority P0; Status Implemented / dynamic TLS validation pending; core modules preferred; OpenBSD base Perl compatibility.
- [ ] **[PERL-003]** Update `tools/CMakeLists.txt`, docs, packagers and all invocations to the Perl script; remove Python only after consumer migration.
  - Priority P0; Status Implemented / CI install validation pending.
- [ ] **[PERL-004]** Add `perl -c`, Test::More argument/JSON/HTTP regression tests and local fake-server coverage.
  - Priority P0; Status Implemented / 54 tests passed on an earlier revision; expanded transport/TLS suite not run; TLS fixture pending.
- [ ] **[PERL-005]** Document TLS fallback/dependencies; no CPAN runtime downloads; no Perl runtime dependency for the C++ daemon.
  - Priority P1; Status Implemented / CI validation pending.
- [ ] **[PERL-SEC-001]** Audit untrusted input, shell injection, path traversal, temp files/symlinks, permissions, Unicode, regex safety, dynamic loading and destructive operations.
  - Priority P0; Status Statically reviewed; no `eval STRING` on external data; no shell interpolation.
- [ ] **[PERL-SEC-002]** Preserve self-signed HTTPS behavior with a safe `openssl s_client` fallback when `IO::Socket::SSL` is unavailable; never silently downgrade to cleartext.
  - Priority P0; Status Implemented / TLS fixture pending.
- [ ] **[PERL-SEC-003]** Validate `perl -c`, optional Perl::Critic, Test::More, invalid args, Unicode, filesystem errors and idempotence.
  - Priority P1; Status Pending; dynamic execution required.
- [ ] **[PERL-SEC-004]** Add transport regressions for URL injection, chunk framing, size limits, TLS fallback argv/no-cleartext and C0/C1 output escaping.
  - Priority P0; Status Implemented in `tools/tests/i2pdctl.t`; not executed; real TLS/OpenBSD validation pending.

## P. Torrent/Perl build and tests

- [ ] **[BUILD-011]** Add `TorrentsControl.cpp` to explicit CMake source lists; keep torrents optional and TUI optional.
  - Priority P0; Status Implemented / CI build pending; `build/CMakeLists.txt`.
- [ ] **[BUILD-012]** Ensure daemon/torrent engine builds without ncurses and without Perl runtime; no GNU Make.
  - Priority P0; Status Pending.
- [ ] **[BUILD-013]** Install the Perl `i2pdctl` with correct interpreter/permissions and remove Python installation.
  - Priority P1; Status Implemented / CI install validation pending.
- [ ] **[TEST-011]** Add torrent control/protocol/TUI unit and integration tests to CTest.
  - Priority P0; Status Protocol/bencode tests implemented; control/TUI integration pending.
- [ ] **[TEST-012]** Add malformed/truncated/oversized torrent and path/symlink tests, plus RPC limit/auth tests.
  - Priority P0; Status Bencode/path tests implemented; RPC integration pending.
- [ ] **[TEST-014]** Register `test-protocol-bounds` and `fuzz-protocol-bounds`; cover IdentityEx/LeaseSet truncation, I2NP bounds and NTCP2/SSU2 malformed paths.
  - Priority P0; Status Test/fuzzer sources added and CMake registered; execution deferred to CI.
- [ ] **[TEST-013]** Run Perl regression tests and compare old Python/Perl outputs where a fixture is available.
  - Priority P1; Status 54 tests passed on an earlier revision; expanded suite not run; Python comparison not run because Python is not a target dependency.

## Known defects / findings not yet fixed

- [ ] **[DEF-001]** Non-atomic tunnel/router status fields read by TUI (data race under C++ memory model).
- [ ] **[DEF-002]** No dynamic tests for SU3 malformed input; add fixture.
- [ ] **[DEF-003]** Android Java source compiles with Java 25; APK/JNI/Lint/device tests are deferred to CI.
- [ ] **[DEF-004]** `routerinfo` firewall backends not yet dynamically syntax-checked.
- [ ] **[DEF-005]** Full Java I2P differential audit not yet performed.
- [ ] **[DEF-006]** Android manifest still allows cleartext? Removed; verify merged manifest.
- [ ] **[DEF-007]** `tools/vain` raw key-buffer arrays were replaced with zeroing RAII ownership, jthread joins and checked serialization/error paths.
  - Status Implemented / static only; ASan/TSan and one-character search pending.
- [ ] **[DEF-008]** `famtool` family certificate/private-key handling needs deeper review.
- [ ] **[DEF-009]** Core process still combines network, filesystem and long-term keys; only logging is separated.
- [ ] **[DEF-010]** Log worker now has a bounded in-process restart/backoff monitor; no separate supervisor process and no explicit logger queue quota remain.
- [ ] **[DEF-011]** Torrent control/TUI/web removal is implemented statically; dynamic TUI/engine/RPC validation is pending.
- [ ] **[DEF-012]** Torrent network/storage process separation is not implemented; the monolithic daemon remains a residual risk.
- [ ] **[DEF-013]** Perl `i2pdctl` migration and HTTPS fallback need dynamic validation on OpenBSD/Linux.
- [ ] **[DEF-014]** Android Java 25 source compiled before the latest lifecycle/settings/FGS edits; APK/JNI/resource-link/Lint and device behavior remain unvalidated; see H section.

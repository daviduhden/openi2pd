<!--
See the LICENSE file at the top of the project tree for copyright
and license details.

openi2pd: TODO.

Documents project documentation.

-->

# Master TODO

Persistent task inventory for the openi2pd fork. IDs are stable. A checked
box does **not** replace the status field: dynamic validation is tracked
separately.

Subagent ownership: Architecture=coordinator; C++23=B; Security=C; IPC/M=coordinator+IPC; I2P=A; Build/DEPS=D; TUI=E; Torrent/RPC=Torrent; Perl=Perl; Tools/FW=F; Docs=G; Android=H; OpenBSD/I; Branding/LEGAL=J/K; CI=J; Tests=C/G/F/Torrent.

Area ID map: `CPP`=B, `SEC`=C, `I2P`=A, `DEPS`/`BUILD`=D, `TUI`=E,
`TOOLS`/`FW`=F, `DOCS`=G, `ANDROID`=H, `OBSD`=I, `BRAND`/`LEGAL`=J/K,
`CI`=J, `TEST`=C/G/Torrent, `PRIVSEP`=M, `TORRENT`=Torrent, `PERL`=Perl.

Status values: `Pending`, `In progress`, `Blocked`, `Implemented / static`,
`Pending CI compilation`, `Pending CI tests`, `CI failure`, `Correction
pending`, `Validated by CI`, `Dropped (reason)`.

Permanent development policy: local compilation, build configuration and test
execution are prohibited for all agents and subagents. Dynamic validation is
CI-only through manual `workflow_dispatch` workflows. Local work is static
analysis, implementation, test preparation and CMake/workflow updates. See
`AGENTS.md` “Permanent priority policy: static-only local work”. Historical
local build/test results in this file refer to earlier work and do not validate
current uncommitted changes.

Priority values: `P0` critical, `P1` high, `P2` normal, `P3` low.

Responsible: the coordinator owns integration; each task is executed by the
subsystem owner in the ownership table unless a task says otherwise. The
`Files` field identifies the review boundary.

## A. I2P Java interoperability

- [x] **[NET-001]** Clone the latest stable I2P Java release outside this tree and record tag/commit/date.
  - Priority P0; Status Implemented; tag `i2p-2.13.0`, commit `9134f808337b401e8e53c73734c81fab04280c9d`, released 2026-07-24; read-only clone outside the tree (not bundled).
  - Files: external read-only Java reference; `docs/INTEROPERABILITY.md`, `tests/interop/README.md`.
  - Acceptance: tag, commit, version, date recorded; no Java code added to fork.
  - Validation: GitHub releases/tags rechecked 2026-10-19; `i2p-2.13.0` is still the latest stable release and tag.
- [ ] **[NET-002]** Audit NTCP2 handshake, obfuscation, options, version negotiation and PQ variants against Java.
  - Priority P0; Status Implemented / static; NET-118..NET-123 plus NET-134..NET-140 cover replay, block ordering, termination/control queueing, X/Y/static-key validation, Alice replay, pending-handshake bounds, extra pending bytes, per-version padding and proxy buffer lifetime.
  - Files: `libi2pd/NTCP2.cpp`, `NTCP2.h`; Java `router/transport/ntcp2`; `test-ntcp2-session-confirmed-payload` and `test-ntcp2-handshake-bounds` passed locally.
  - Acceptance: matrix rows with class/function references and any fix.
  - Validation: differential/session/replay/PQ tests pending.
- [ ] **[NET-003]** Audit SSU2 handshake, tokens, relay, peer test, fragmentation, MTU and IPv6 against Java.
  - Priority P0; Status Implemented / static; NET-124..NET-154 cover tokens, headers, PeerTest, PQ/Retry, ACK/NACK, RTO/backoff, multi-fragment SessionConfirmed, MTU, per-family addresses and transport lifecycle.
  - Files: `libi2pd/SSU2*.cpp`, `SSU2Session.h`, `Transports.*`, `RouterInfo.*`, `RouterContext.*`; Java `router/transport/ssu2`; new SSU2 tests passed locally.
  - Acceptance: matrix rows and fixes; OpenBSD live MTU/IPv6 path pending.
- [ ] **[NET-004]** Audit I2NP message types, serialization, timestamps and malformed handling.
  - Priority P1; Status Statically reviewed; bounds fixes in NET-015/I2P-002; dynamic pending.
- [ ] **[NET-005]** Audit RouterInfo/LeaseSet/LeaseSet2/EncryptedLeaseSet parsing and validation.
  - Priority P1; Status Implemented / static; LS-109..LS-118, RI-005..RI-016 and DB-109/DB-110 cover LeaseSet/RouterInfo parsing, freshness, ownership and replacement; dynamic vectors pending.
- [ ] **[NET-006]** Audit netDb/floodfill lookup, store, expiration and reseed behavior.
  - Priority P1; Status Implemented / static; DB-104..DB-112 cover netDb replacement, DLM/DSRM, lifecycle, persistence and lookup bounds; dynamic floodfill/lookup pending.
- [ ] **[NET-007]** Audit tunnel build records, garlic routing and delivery semantics.
  - Priority P1; Status Statically reviewed; tunnel time fix I2P-004; dynamic tunnel test pending.
- [ ] **[NET-008]** Audit Streaming, Datagram2/3, SAM and I2CP wire behavior used by Java clients.
  - Priority P1; Status Statically reviewed; client fixes I2P-006..009; dynamic client matrix pending.
- [ ] **[NET-009]** Audit ECIES-X25519-AEAD-Ratchet, signatures, key derivation and nonces.
  - Priority P0; Status Statically reviewed; ratchet fixes I2P-009/I2P-010 and NET-015; crypto vectors pending.
- [x] **[NET-010]** Produce `docs/INTEROPERABILITY.md` with Java class/function references.
  - Priority P0; Status Implemented / dynamic validation pending; covers all major areas, not every sub-message.
- [ ] **[NET-011]** Implement justified fixes for confirmed incompatibilities.
  - Priority P0; Status Implemented / static for I2P-001..012; I2P-013 remains blocked; all dynamic Java/C++ validation pending.
- [x] **[NET-013]** Fix RouterInfo size cap to 4096 to match Java `MAX_UNCOMPRESSED_SIZE`.
  - Priority P0; Status Implemented / dynamic validation pending; `RouterInfo.h` comment references Java 2.13.0.
- [ ] **[NET-012]** Prepare differential tests and controlled Java/C++ integration harness.
  - Priority P1; Status Harness expanded / no dynamic test executed; `tests/interop/`; never touch public I2P network.
- [ ] **[NET-014]** Fix `NetDb::SaveUpdated()` iterating `m_RouterInfos` without the mutex; design snapshot/object locking.
  - Priority P0; Status Implemented / static; snapshots `m_RouterInfos` under `m_RouterInfosMutex`; TSan/live persistence pending.
- [ ] **[NET-015]** Apply Java 2.13.0 transport/ratchet/LeaseSet/netDb/client static-interoperability corrections.
  - Priority P0/P1; Status Implemented / static; previous audit baseline; dynamic Java/C++ validation, ASan/TSan, fuzz and OpenBSD/LibreSSL builds pending.
- [ ] **[NET-016]** Apply the Java 2.13.0 NTCP2 replay, SessionConfirmed ordering/size, data-phase ordering and termination-count corrections.
  - Priority P1; Status Implemented / static; NET-118..NET-123 and NET-134..NET-140; `test-ntcp2-session-confirmed-payload` and `test-ntcp2-handshake-bounds` passed in the local 42-test CTest run; no live replay/handshake execution.
- [ ] **[NET-017]** Apply the Java 2.13.0 SSU2 token, header-validation, PeerTest, PQ/version, MTU and fragment-ordering corrections.
  - Priority P1; Status Implemented / static; NET-124..NET-154; all SSU2/control regression tests passed in the local 42-test CTest; no live SSU2 relay/PeerTest/MTU matrix.
- [ ] **[NET-018]** Apply the Java 2.13.0 Streaming, Datagram2/3, SAM and I2CP client corrections.
  - Priority P1; Status Implemented / static; SAM-105..SAM-109, DG-102/DG-104, STR-101..STR-103 and I2CP-103/I2CP-104; `test-datagram-options`, `test-sam-datagram-framing` and `test-streaming-buffer` passed locally; dynamic SAM/I2CP/stream tests pending.
- [ ] **[NET-019]** Apply the Java 2.13.0 LeaseSet/netDb/DatabaseStore/tunnel-build corrections.
  - Priority P1; Status Implemented / static; LS-104..LS-108, DB-101..DB-103 and TU-101; `test-leaseset-netdb-2.13-freshness` passed locally; dynamic netDb/tunnel validation pending.
- [ ] **[NET-020]** Apply the Java 2.13.0 crypto/ratchet/Garlic validation corrections.
  - Priority P1; Status Implemented / static; CRY-201, RT-201/RT-202, GAR-201, B33-201, ID-201 and SIG-201; `test-crypto-wire-validation` passed locally; live NS/NSR/Garlic and LibreSSL gates pending.
- [ ] **[NET-021]** Apply the Java 2.13.0 LeaseSet/RouterInfo/netDb validation, freshness, ownership and replacement corrections.
  - Priority P1; Status Implemented by static analysis; LS-109..LS-118, RI-005..RI-016 and DB-104..DB-112; tests prepared but current tree is pending CI compilation/tests; live Java floodfill/handshake tests pending.
- [ ] **[NET-022]** Align cross-type LeaseSet1/LeaseSet2 replacement with Java `isNewer()`.
  - Priority P1; Status Implemented by static analysis; `LeaseSet::GetEarliestLeaseDate` and `NetDb::IsLeaseSetNewer` now compare earliest leases whenever either entry is LS1; LS2/LS2 uses published; encrypted outer layers use zero, and Meta now records its earliest entry. `test-leaseset2-java213-regression` checks Meta earliest; pending CI.
- [ ] **[NET-023]** Apply the 2026-10-19 third static pass against Java 2.13.0 (latest stable).
  - Priority P1; Status Implemented / static, CI pending; NET-155..NET-161, LS-119..LS-121, DB-113/DB-114, RS-101, SAM-110, DG-105, STR-104 and CRY-202..CRY-203 corrected. Residual: SSU2 path-challenge retransmit, RouterInfo flood bit, SAM tokenizer/routing edge cases, per-session datagram version, I2CP TYPE_KEY/secret association and RSA verifier. No Java peer, differential run or sanitizer execution.
- [ ] **[I2P-001]** Implement MetaLeaseSet (DatabaseStore type 7) parsing, signature/identity/lease validation, storage, lookup and malformed-input handling.
  - Priority P1; Status Implemented / static; LS-115 Meta parsing, 16-entry/earliest bounds and netDb store/serve; dynamic Java vector pending. `LeaseSet.*`, `NetDb.cpp`; `test-leaseset2-java213-regression` passed locally.
- [ ] **[NET-024]** Apply the 2026-10-19 crypto/ratchet/PQ static audit.
  - Priority P0/P1; Status Implemented / static, CI pending; CRY-206..CRY-214: NSR/ES payloads are pre-validated before side effects, NS requires a Garlic clove, NS replay cache is 5-minute/4096-entry, tag index 65534 is accepted, tag-budget checks precede chain advancement, LibreSSL/no-PQ publication and selection are gated, unsupported hybrid keys are not advertised, Ed25519ph now uses Java-compatible plain Ed25519, and RedDSA keys are wiped. Residual: runtime ML-KEM provider probe, type-5/type-7 NSR vectors, 7/4 multi-key session matrix, Java-generated NS/NSR vectors and ASan/TSan/CI execution. No dynamic validation.
- [ ] **[NET-025]** Apply the 2026-10-19 client-protocol static pass.
  - Priority P1; Status Implemented / static, CI pending; SAM-112..SAM-116 (large frames, SILENT connect, LISTEN_PORT accept, no 3 s expiry, signature types), I2CP-106..I2CP-108 (direct b33 association, HostReply LS2 options, TYPE_KEY merge), DG-106/DG-107 (session-table null guard, no inbound version mutation, hash-only DG3) and STR-105..STR-107 (async incoming LeaseSet, SYN MTU, offline/close authentication). Residual: SAM tokenizer/pipelining, LISTEN_PROTOCOL and receiver reset, secret-blinded I2CP, per-session datagram version, Datagram3/Streaming vectors and all dynamic validation.
- [ ] **[I2P-002]** Implement DatabaseLookup reply-key flags `0x12` (ECIES ratchet public key without tag) with correct encrypted replies and no cleartext fallback.
  - Priority P1; Status Implemented / static; DB-104/DB-105 strict 0x02/0x10 parsing and 0x12 fail-closed; proposal-154 derived KDF is TBD and Java 2.13 has no sender. `NetDb.cpp`, `test-netdb-store-lookup-hardening` passed locally.
- [ ] **[I2P-003]** Validate DatabaseSearchReply sender/from hash and pending-request association; reject truncated/fabricated replies.
  - Priority P1; Status Implemented / static; DB-106..DB-108 strict DSRM parser, mandatory trailing from, one reply per queried peer and 4096 pending-request cap; `test-netdb-dsrm-correlation`/`test-netdb-lookup-wire-bounds` passed locally.
- [ ] **[I2P-004]** Add short/long tunnel-build request time windows, stale/future/replay handling and correct rejection codes.
  - Priority P1; Status Implemented / static; `TransitTunnel.cpp`; 8-minute age/5-minute future drop semantics and code-30 layer-encryption rejection; duplicate-build Bloom filter and dynamic clock-skew tests pending.
- [ ] **[I2P-005]** Apply Java one-hour RouterInfo freshness to floodfill lookups without affecting LeaseSet/local responses.
  - Priority P1; Status Implemented / static; one-hour RouterInfo freshness plus clock fudge, hidden/self exclusion and DB-112 persistence bounds; local netDb tests passed, live aged-RI lookup pending. `NetDb.cpp`, `test-netdb-store-lookup-hardening`.
- [ ] **[I2P-006]** Complete SAM 3.2 STREAM ACCEPT/RAW RECEIVED port/protocol fields, version gating, defaults, errors and close behavior.
  - Priority P2; Status Implemented / static; SAM 3.2 port/protocol defaults, STREAM CONNECT/ACCEPT, DATAGRAM/RAW framing, version gating and connect wait; 3.0/3.1/3.2 matrix pending. `SAM.cpp/.h`; SAM 3.0/3.1/3.2 matrix pending.
- [ ] **[I2P-007]** Implement I2CP BlindingInfo type 42 parsing/association and b33/encrypted destination resolution.
  - Priority P1; Status Implemented / static; type-42 parser, bounded cache and derived-b33 association; secret-blinded and TYPE_KEY association limitations remain. `I2CP.cpp/.h`; Java client vectors pending.
- [ ] **[I2P-008]** Handle Datagram3 first contact without cached LeaseSet using authenticated ratchet identity and bounded pending lookups.
  - Priority P2; Status Implemented / static; bounded HashReceiver and session table, first-contact Datagram3 delivery; no-cache/concurrency tests pending. `Datagram.cpp/.h`; no-cache/latency tests pending.
- [ ] **[I2P-009]** Implement Datagram2 offline signature emission/verification with delegation, expiry and malformed cases.
  - Priority P2; Status Implemented / static; Datagram2 offline section sign/verify, expiry/delegation checks; Java offline-key vectors pending. `Datagram.cpp/.h`; offline-key vectors pending.
- [ ] **[I2P-010]** Iterate all locally supported PQ encryption types for incoming Ratchet New Session messages with safe fallback.
  - Priority P1; Status Implemented / static; bounded local PQ candidate iteration, per-candidate trial state, replay/DateTime preserved; PQ type matrix pending. `ECIESX25519AEADRatchetSession.*`; PQ type matrix pending.
- [ ] **[I2P-011]** Validate SSU2 RelayRequest signature, timestamp, version, fields, state, replay and resource limits; align RelayResponse/HolePunch validation.
  - Priority P0/P1; Status Implemented / static; RelayRequest/Response/Intro timestamp, version, length, signature, replay and resource checks; HolePunch version fixed to 2 and PeerTest source validation added; forged/replayed relay and live PeerTest tests pending. `SSU2Session.cpp`, `SSU2OutOfSession.cpp`; forged/replayed relay tests pending.
- [ ] **[I2P-012]** Audit and align NTCP2/SSU2 protocol-significant timeouts, resends, idle/close and recovery without blindly copying Java constants.
  - Priority P2; Status Implemented / static; SSU2 25 s establish with 1.25/2.5/5/10 s handshake backoff and RFC 6298 data RTO/backoff; NTCP2 10 s establish/120 s idle and completion-driven termination; lossy/latency and restart tests pending. `NTCP2.*`, `SSU2*`; lossy/latency matrix pending.
- [ ] **[I2P-013]** Investigate GOST signature types 9/10; implement only if a real, safe dependency exists; otherwise document the block and never advertise support.
  - Priority P3; Status Blocked - dependency/protocol evidence; `Identity.h`, crypto backend; no implementation until provable.
- [ ] **[TEST-017]** Execute the Java 2.13.0 differential matrix: serialization, malformed inputs, handshakes, netDb, tunnels, ratchet, Datagram and client protocols.
  - Priority P0; Status Harness expanded / static; 26 checked-in cases reviewed (24 static/vector OK, 2 documented known gaps: I2NP checksum and proposal-154 0x12), `interop-static` is registered in CTest and the manual CI interop job validates the pinned reference. No differential C++ binary, live Java peer or public-network test has executed.
- [ ] **[TEST-018]** Add fuzz/regression cases for every new parser and time/expiry check (MetaLS, DLM 0x12, DSRM, tunnel time, relay, PQ iteration, offline D2).
  - Priority P0; Status Partially implemented; `test-su3-bounds` added; remaining parser/time fuzz and malformed fixtures pending.
- [x] **[TEST-021]** Build and run the registered CTest suite for the Java 2.13.0 static-interoperability changes.
  - Priority P0; Status Validated historically on Linux/GCC 16.2.0/OpenSSL 4.0.3: CTest **48/48** passed; this does not validate the current uncommitted tree. No Java peer, sanitizer, fuzzer, OpenBSD/LibreSSL, Windows or Android execution.
- [ ] **[TEST-022]** Integrate all subsystem tests into CMake/CTest and the manual GitHub Actions matrix.
  - Priority P0; Status Implemented / static; every applicable `test-*.cpp` is registered, `interop-static`, `perl-doc-links`, `android-config-static` and `android-english-only` are CTest targets, and suite labels group i2p/security/memory/IPC/TUI/torrent/tools/Perl/interop/platform/Android. Linux-only sandbox/IPC tests are now registered only on Linux, and OpenBSD policy/imsg tests only on OpenBSD. Linux Clang/GCC, full Windows MSYS2 CTest, OpenBSD, Android unit/Lint/APK, ASan+UBSan, TSan and fuzz smoke jobs are prepared. No current workflow run includes these uncommitted changes; compilation/tests pending CI.
- [ ] **[SEC-014]** Review malformed RouterInfo address/property parameter parsing and SSU2 address-family mismatch.
  - Priority P1; Status Implemented / static; RouterInfo option bounds/duplicates/introducer indices and SSU2 cross-family Address handling fixed; malformed corpus pending.
- [ ] **[OPENBSD-SBX-1]** Fix OpenBSD reseed URL canonicalization in `Daemon.cpp`; `AbsolutePath()` currently prefixes non-absolute URLs.
  - Priority P0; Status Implemented / dynamic validation pending; URL is left untouched and the unveil setup skips it.
- [ ] **[OPENBSD-SBX-2]** Register/implement `unix.handle_sigtstp` on OpenBSD or remove the dead TSTP/CONT path.
  - Priority P1; Status Implemented / static; option is now registered for OpenBSD; dynamic signal test pending.
- [ ] **[OPENBSD-SBX-3]** Decide whether a custom `openbsd.pledge_file` may bypass the fixed startup pledge; document or enforce.
  - Priority P1; Status Implemented / static; custom pledge policies are additive to the fixed startup/runtime baseline, with token validation and duplicate suppression; a runtime custom file may not add `proc`/`exec`, and the core never retains `exec`. Dynamic OpenBSD test pending.
- [ ] **[OPENBSD-SBX-4]** Add native OpenBSD per-role policies, startup-time worker spawning and capability checks.
  - Priority P0; Status Implemented / static, CI pending; `OpenBSDSandbox` builds supervisor/log/storage/network/crypto/admin policies, the supervisor/log pair is spawned before the runtime pledge, workers refuse euid 0, and `test-privsep-openbsd-policy` forks negative storage/network capability cases. No OpenBSD compile/runtime result.
- [ ] **[OPENBSD-SBX-5]** Migrate storage, network, crypto and admin backends under the OpenBSD policies and add typed imsg/SCM_RIGHTS operations.
  - Priority P0; Status Pending; policy profiles and an `imsg` adapter/test exist, but the core still owns persistence and sockets. Requires typed storage/network/crypto/admin workers, pre-opened capability descriptors and per-operation authorization.
- [ ] **[OPENBSD-PID-1]** Implement a readiness check for the daemonized rc.d start; `--daemon` currently exits before pidfile/pledge/transport readiness.
  - Priority P1; Status Implemented / static; daemon parent waits up to 60 s for a close-on-exec readiness byte after pidfile/sandbox/logger/start; timeout kills the child. Dynamic rc.d test pending.
- [ ] **[OPENBSD-LOG-1]** Synchronize `Log::Reopen()` with the logger thread for SIGUSR1 rotation.
  - Priority P1; Status Implemented / static; logger destination/stream replacement and writes are synchronized; rotation stress procedure documented. Dynamic SIGUSR1 test pending.
- [ ] **[OPENBSD-VAL-1]** Execute `docs/security/openbsd-validation.md` on a real OpenBSD host (pledge/unveil, pidfile, signals, SSU2/MTU, worker role policies, imsg/SCM_RIGHTS).
  - Priority P0; Status Pending; dynamic validation required, including `test-privsep-openbsd-policy` and `test-privsep-openbsd-imsg`.

## B. C++23 modernization

- [x] **[CPP-001]** Require C++23 in CMake; remove C++20 fallback and obsolete preprocessor branches.
  - Status Implemented / validation pending; build passed on GCC 16 Linux.
- [ ] **[CPP-002]** Audit remaining raw owning pointers and manual `new[]`/`delete[]`.
  - Priority P1; Status Partially implemented / static; Streaming/Gzip, LeaseSet/Identity, NTCP2/Transports, client temp buffers, TorrentsDHT, daemon pimpl and library thread owners converted. Remaining raw owners are tracked in CPP-002 follow-up; no compile/sanitizer run.
- [ ] **[CPP-003]** Replace remaining avoidable C-style buffers and macros in hot paths.
  - Priority P2; Status Partially implemented / static; Gzip stack buffers, Streaming span APIs, Base64 span output and tool chunk spans added; remaining packet/crypto C buffers intentionally deferred for performance review.
- [ ] **[CPP-004]** Convert tunnel/router status reads used by TUI to safe snapshots/atomics.
  - Priority P1; Status Implemented / static; TUI-visible RouterContext/tunnel creation fields are atomic and TuiServerCommon uses accessors only; TSan matrix pending.
- [ ] **[CPP-005]** Add `std::expected`/`optional` to recoverable configuration/filesystem APIs where safe.
  - Priority P2; Status Partially adopted / static; `expected` is used by TUI/tools, `optional` by tools and `ByteStreamToBase64(std::span)` is available; filesystem/configuration APIs remain a separate compatibility pass.
- [ ] **[CPP-006]** Consider namespace/enum cleanup and strongly typed protocol constants without ABI breaks.
- [ ] **[CPP-007]** Maintain the clang-format profile and format C/C++ sources consistently; verify build after the formatting pass.
  - Priority P1; Status Implemented / static formatting; 245 C/C++ and Java files formatted; compile/CI validation pending.
- [x] **[CPP-008]** Remove obsolete dependency-compatibility branches and enforce justified minimum library versions.
  - Priority P1; Status Implemented / static, CI pending. Floors are now OpenSSL 3.0.2 / LibreSSL 4.0 / zlib 1.2.11, Boost 1.74 core / 1.81 JSON+URL, and MiniUPnPc 2.2.6/API 17. Removed the OpenSSL 3.0.0 SipHash exception, pre-LibreSSL-4.0 in-place AEAD copy, MiniUPnPc API<14/10/8 branches (keeping the API 18 `UPNP_GetValidIGD` signature capability and rejecting API<17), Windows XP `inet_pton_xp`/`inet_ntop_xp` and `WINVER == 0x0501`, MSVC `sprintf_s` branches, Android non-Clang/API<24 polyfills, the legacy LLVM atomic probe, `EVP_MD_CTX_create`/`destroy` aliases, `OPENSSL_SUPPRESS_DEPRECATED`, and the `JSON_SUPPORTED`/old-Boost and `BOOST_ASIO_HAS_STD_ARRAY`/Bind placeholder fallbacks. `I2PEndian` now uses C++23 `std::endian`/`std::byteswap` on Windows and the platform `endian(3)` header on Unix, and redundant Boost.Atomic/Filesystem/JSON/URL packages were removed from CI, Debian, Docker and Android vcpkg where unused; MiniUPnPc remains in the Android vcpkg manifest because UPnP is enabled by default. OpenBSD libc++ `<expected>`/`move_only_function`/`clock_cast` fallbacks remain explicitly documented. No build or dependency-matrix execution.
- [ ] **[CPP-009]** Convert remaining library ownership trees, tag buffers, temporary arrays and OpenSSL handles to RAII.
  - Priority P1; Status Implemented / static; Kademlia `DHTNode`/root use `std::unique_ptr`, Garlic `UnconfirmedTags`/payload buffer, Reseed compressed buffers, Signature ED25519/EVP contexts and GOST curve/temporary `BIGNUM` owners are scoped; `test-kad-dht`, `test-signature-identity-hardening` and extended `test-reseed-zip-bounds` prepared; compile/ASan/TSan pending.
- [ ] **[CPP-010]** Convert daemon descriptor, `FILE*` and OpenSSL ownership to scoped RAII.
  - Priority P1; Status Implemented / static; readiness pipe/pidfile guard and I2PControl certificate/key objects use local owners with checked OpenSSL/PEM returns; Linux/OpenBSD runtime and sanitizer pending.
- [ ] **[CPP-011]** Modernize tool/client lifetime and span/expected boundaries without changing CLIs or wire formats.
  - Priority P2; Status Implemented / static; ClientContext/AddressBook use `unique_ptr`, client key buffers use vectors, x25519/i2pbase64/Base use span/RAII, and `test-addressbook` is prepared; CI pending.
- [ ] **[ANDROID-015]** Format Android Java sources with the clang-format Java profile and re-run Java compile/Lint/unit tests.
  - Priority P1; Status Implemented / static formatting; Java compile/Lint/tests pending.
  - Priority P3; Status Pending.

## C. Security and concurrency

- [x] **[SEC-001]** Fix base64 lazy-table race and invalid alphabet/padding acceptance.
  - Status Implemented; `tests/test-base-64` passed on Linux.
- [x] **[SEC-002]** Harden SU3 header parsing and bounded allocations in reseed.
  - Status Implemented / static; `tests/test-su3-bounds.cpp` provides malformed/truncated/oversized header cases; not compiled/run.
- [x] **[SEC-003]** Lock netDb counters and reseed state.
  - Status Implemented; TSan pending.
- [x] **[SEC-004]** Add lock-safe peer snapshot and use it in TUI/I2PControl.
  - Status Implemented; TSan pending.
- [x] **[SEC-005]** Strict HTTP header/Content-Length/chunk/URL port parsing.
  - Status Implemented; unit tests passed on Linux.
- [x] **[SEC-006]** Use constant-time comparison for integrity hashes.
  - Status Statically reviewed; no timing test.
- [ ] **[SEC-007]** Add truncated/malformed SU3 regression fixture.
  - Priority P1; Status Implemented / static; `tests/test-su3-bounds.cpp` covers malformed/truncated/oversized headers; not compiled/run.
- [ ] **[SEC-008]** Run and triage ASan/UBSan/TSan; fix findings.
  - Priority P0; Status CI jobs prepared / pending dynamic execution; `.github/workflows/build.yml` adds independent ASan+UBSan and TSan jobs that build and run the full CTest suite without suppressions. No sanitizer run exists for the current tree.
- [ ] **[SEC-009]** Fuzz TUI/HTTP parsers and add NTCP2/SSU2/I2NP/RouterInfo/LeaseSet/SU3 targets.
  - Priority P1; Status Partially implemented / static; all eight libFuzzer targets are registered in CMake and the manual `Fuzzing` workflow builds/runs bounded smoke passes. Dedicated NTCP2/SSU2 packet seeds and dynamic fuzz results remain pending; the workflow is not yet published, so it cannot be dispatched with `gh` from this revision.
- [ ] **[SEC-010]** Audit all JNI local/global references, exceptions and ownership.
  - Priority P0; Status Implemented / static, dynamic pending; no global refs, stored Java objects or unbounded local-ref growth found. `NewStringUTF` was replaced by bounded UTF-8-to-UTF-16 decoding, so ordinary and supplementary characters/NUL no longer depend on modified UTF-8. C++ allocation exceptions at outer JNI boundaries remain a device/CI check; no runtime claim.
- [ ] **[SEC-011]** Audit Android exported components, intents, permissions and storage.
  - Priority P0; Status In progress; see `docs/android-modernization.md`.
- [ ] **[SEC-012]** Validate TUI IPC peer-UID rejection and malformed-client behavior dynamically.
  - Priority P1; Status Partially implemented / static; Unix peer UID path exists and Windows DACL policy is implemented; no second UID/account runtime test.
- [ ] **[SEC-013]** Review cryptographic buffer wipes and secret lifetime in tools and daemon.
  - Priority P1; Status Partially implemented / static; checked BIGNUM/X25519/ECIES/key bounds, stricter b33/ECIES parsing, bounded Garlic writes, strict reseed deflate/HTTP limits, and new crypto/reseed regressions. Tool outputs now use owner-only POSIX permissions/no-follow writes, bounded stdin/path input and explicit sensitive-buffer wipes. No ASan/UBSan or secret-lifetime dynamic test executed.

## D. Build, dependencies, packaging

- [x] **[BUILD-001]** Remove GNU make dependency; POSIX make front-end delegates to CMake/Ninja.
  - Status Implemented; `bmake` full build passed before later changes.
- [x] **[BUILD-002]** Require C++23 and explicit dependency minimums.
  - Status Implemented / static, CI pending; `docs/dependencies.md` and CMake enforce compiler C++23, OpenSSL 3.0.2 / LibreSSL 4.0, zlib 1.2.11, Boost 1.74 core, Boost 1.81 JSON/URL where torrents are enabled, and MiniUPnPc 2.2.6/API 17 for UPnP (API 18 used when available). Build/dependency matrix execution pending.
- [x] **[BUILD-003]** Replace source globs with explicit lists and target-scoped CMake.
  - Status Implemented; CMake configure/build passed on Linux.
- [x] **[BUILD-004]** Integrate tests with CTest and add TUI/tools tests.
  - Status Implemented; 16/16 passed before firewall test; firewall test built.
- [x] **[BUILD-005]** Re-run full CMake and POSIX-make builds after all recent changes.
  - Priority P0; Status historical only: Linux/GCC 16 builds and 42/42/48/48 CTest results predate the current static-only policy. The current worktree is pending manual CI compilation, POSIX-make and install checks.
- [x] **[BUILD-010]** Static CMake/POSIX-Make portability audit.
  - Priority P0; Status Implemented / static; removed non-POSIX make syntax, hard-coded MiniUPnPc/MSYS2/Inno paths and automatic `git pull`; split `i2pd_privsep` into its own CMake component; added complete POSIX `configure/build/tools/test/check/install` targets; added `I2PD_INSTALL_DOCS` and Android-only JNI validation; the compile-free `build-system-static` CTest check now scans Makefiles, top/tools/tests/Android CMake, generator default, component/target scope, globs, downloads and absolute paths. `cmake -P tests/check-build-system.cmake` passed locally; no configure/build/install/CTest execution.
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
- [x] **[TUI-002]** Implement versioned local IPC with bounds and peer authorization.
  - Status Implemented / static; bounded snapshots plus typed `ADMIN TRANSIT` and
    `ADMIN LOGLEVEL`; Linux AF_UNIX session tested historically; Windows
    named-pipe transport implemented/static but not runtime-tested.
- [x] **[TUI-003]** Implement shared TUI client with status/peers/connections/tunnels/netDb/logs.
  - Status Implemented / static; ncursesw live session tested historically;
    Win32/VT backend implemented/static.
- [x] **[TUI-004]** Show local destination b32 addresses in TUI.
  - Status Implemented / static; dedicated Destinations view using the bounded
    `DESTINATIONS` snapshot, separated from NetDb. Pending CI.
- [ ] **[TUI-005]** Add safe administrative router commands via the daemon main-loop queue.
  - Priority P1; Status Partially implemented / static: torrent actions are
    mediated by the torrents service; `ADMIN TRANSIT` updates an atomic
    acceptance flag and `ADMIN LOGLEVEL` updates the atomic logger threshold.
    Reload/reseed/shutdown remain pending and require a main-loop queue.
- [ ] **[TUI-006]** Test resize, reconnect, signal handling and long-running log streaming.
  - Priority P1; Status Windows/console/named-pipe tests prepared; no runtime resize/reconnect/signal/log-stream execution.
- [ ] **[TUI-008]** Add a bounded, validated configuration view/editor to the TUI without exposing keys or raw files.
  - Priority P2; Status Implemented / static (read-only view); `I2PDTUI/1 CONFIG` returns a typed allowlist snapshot and no writer exists. Editing still requires a main-loop command queue; runtime/CI pending.
- [ ] **[TUI-007]** Re-run TUI tests after Android/tools/docs integration.
  - Priority P1; Status Test suites extended for Windows/console/pipe contracts; no build or CTest execution.
- [ ] **[TUI-009]** Extract the shared terminal interface and implement the Win32 console/VT backend without ncurses/PDCurses.
  - Priority P1; Status Implemented / static; `Terminal.h`, `TerminalUnix.cpp`, `TerminalWin32.cpp`; Windows runtime validation pending.
- [ ] **[TUI-010]** Implement secure Windows Named Pipes for TUI IPC with DACL, server identity checks, bounded I/O and reconnection.
  - Priority P1; Status Implemented / static; `TuiServerWin32.cpp`, `TuiClientWin32.cpp`, `TuiWin32Ipc.*`; cross-user and service-account validation pending.
- [ ] **[TUI-011]** Integrate the Windows TUI/IPC into CMake, tests, manual Windows CI and documentation.
- [x] **[TUI-012]** Expand the main TUI navigation, Status statistics and NetDb/Destinations panels.
  - Priority P2; Status Implemented / static; tabs now cover Status, Peers,
    Connections, Tunnels, Torrents, NetDb, Logs, Help, Destinations and Config.
    Added live connection filtering, router/LeaseSet directories, log text
    search, daemon log-level control, transit toggle and a 60-sample bandwidth
    history; official semantic theme roles and both terminal backends are
    preserved. Pending CI/terminal resize and narrow-width checks.
  - Priority P1; Status Implemented / static; native MSVC/Clang builds, CTest and DACL runtime checks pending.
- [ ] **[TEST-019]** Execute the Windows TUI/IPC test matrix: console lifecycle, resize, UTF-16, Named Pipe I/O, DACL denial and reconnect.
  - Priority P0; Status Tests registered / not executed; requires a real Windows host and second account.
- [ ] **[TEST-020]** Run ownership/RAII regressions for Streaming, LeaseSet/Identity, NTCP2/Transports and TorrentsDHT.
  - Priority P0; Status `test-streaming-buffer`, `test-crypto-audit-bounds`, `test-blinding-b33-strict`, `test-reseed-zip-bounds`, `test-ssu2-routerinfo-bounds`, `test-torrents-security`, `test-log-lifecycle` and `test-tui-ipc-regression` are registered; none compiled/run.

## F. Tools

- [x] **[TOOL-001]** Import all 15 i2pd-tools and build them against in-tree libraries.
  - Status Implemented / static; all 15 native targets plus the Perl client are in the CMake graph, inventory/source/provenance tests are registered, and packaging paths are updated. Historical local smoke/CTest results predate the current tree.
- [x] **[TOOL-002]** Remove tool submodule, GNU make, dependency-download script.
  - Status Implemented.
- [x] **[TOOL-003]** Modernize key/base64/file handling and fix verifyhost/regaddr leaks.
  - Status Implemented / validation pending.
- [x] **[TOOL-004]** Add tools smoke test to CTest.
  - Status Implemented; passed on Linux.
- [x] **[TOOL-005]** Audit all remaining tools (`vain`, `autoconf_i2pd`, `keyinfo`, `famtool`, family/address tools) for memory/concurrency/security.
  - Priority P1; Status Implemented / static; `vain` ownership/parsing/threading hardened; firewall and key/address validation prepared; dynamic Windows/address tests pending.
- [x] **[TOOL-006]** Add per-tool regression tests beyond smoke coverage.
  - Priority P2; Status Implemented / static; `tools-regressions` adds key/address/registration/family/base64/offline/vanity CLI vectors plus a scripted `autoconf_i2pd` transcript; `tools-inventory` checks every artifact and source and the pinned upstream manifest. CI execution pending.
- [x] **[FIREWALL-001]** Refactor routerinfo into endpoint model and four backends.
  - Status Implemented / static; common `Endpoint` model and independent iptables/ip6tables, nftables, Windows PowerShell and PF renderers. Pending current CI unit test.
- [x] **[FIREWALL-002]** Preserve legacy `-f` output.
  - Status Implemented / static; byte-for-byte legacy fragment regression retained. Pending current CI unit test.
- [x] **[FIREWALL-003]** Add nftables family/table/chain options and identifier validation.
  - Status Implemented / static; family mismatch is filtered, table/chain identifiers are bounded, and standard inbound/forward chain names are rejected.
- [x] **[FIREWALL-004]** Add Windows PowerShell and PF renderers.
  - Status Implemented / static; both emit outbound-only rules and never execute the firewall.
- [x] **[FIREWALL-005]** Add CLI help, conflict validation and deduplication.
  - Status Implemented / static; malformed RouterInfo files now fail before hash/address access.
- [x] **[FIREWALL-006]** Run `test-routerinfo-firewall` and tools smoke test.
  - Priority P0; Status Historical local 17/17 CTest only; current tree is pending manual CI compilation/tests.
- [ ] **[FIREWALL-007]** Dynamic syntax validation with `iptables-restore --test`, `nft -c`, `pfctl -nf`, Windows test host.
  - Priority P1; Status Prepared, not executed; commands are documented in `docs/tools/routerinfo.md`.
- [x] **[FIREWALL-008]** Add link-local/IPv4-compatible rejection, malformed-RouterInfo CLI coverage and outbound-only assertions.
  - Priority P0; Status Implemented / static; `test-routerinfo-firewall` and `tools-smoke` updated; malformed RouterInfo is checked through every backend and legacy `-f`; pending CI execution.
- [x] **[FIREWALL-009]** Revalidate the common endpoint at the rendering boundary and align IPv4/IPv6 CLI documentation.
  - Priority P1; Status Implemented / static; `IsValidEndpoint` rejects manually assembled endpoints, non-canonical addresses, invalid transports and family mismatches before any backend renders; `routerinfo` documentation and `tools/README.md` now state that `-6` selects IPv6 in every mode. Pending CI.

## G. Documentation

- [x] **[DOC-001]** Import i2pd_docs_en Markdown, MkDocs config and local images.
  - Status Implemented / static; current upstream commit `30ddc4c7c74e055fc858ecff3ae6f69d1a73ab5e`, 46 files. The official tree contains no HTML pages, so the HTML-to-Markdown conversion is a verified no-op for this snapshot.
- [x] **[DOC-002]** Adapt configuration/build docs for C++23, CMake/POSIX Make, TUI and no web console.
  - Status Implemented / static; C++23/CMake, TUI, BitTorrent, Android, Perl and OpenBSD guides reviewed. The torrent tunnel settings were restored with the removed web option and optional loopback RPC documented.
- [x] **[DOC-003]** Add per-tool documentation and tool index.
  - Status Implemented.
- [x] **[DOC-004]** Add architecture, dependencies, C++23, build, TUI, security and testing reports.
  - Status Implemented.
- [x] **[DOC-005]** Validate all internal Markdown links and MkDocs navigation.
  - Priority P2; Status Implemented / static; `tools/scripts/check-doc-links.pl docs` passes on 100 Markdown files and validates local heading/anchor fragments. MkDocs publication was not executed.
- [ ] **[DOC-006]** Complete `docs/INTEROPERABILITY.md` with Java class/function references.
  - Priority P0; Status Implemented / static; Java 2.13.0 matrix, I2P evidence table and NET-116..NET-133, LS-104..LS-108, DB-101..DB-103, TU-101, SAM-105..SAM-109, DG-104, STR-101..STR-103, I2CP-103/I2CP-104 and crypto rows updated; dynamic evidence pending.
- [x] **[DOC-007]** Record upstream docs/tools commits and manual sync procedure.
  - Status Implemented / static; `docs/upstream-sync.md`, `contrib/upstream/`, pinned docs commit `30ddc4c7c74e055fc858ecff3ae6f69d1a73ab5e`; manual comparison only, no automatic overwrite. `docs-path-map.txt` records the two renamed BitTorrent pages, and `sync.sh --docs` verifies upstream coverage, missing local targets and HTML sources before printing diffs.
- [x] **[DOC-008]** Update Android architecture/JDK 25 documentation and validation status.
  - Priority P1; Status Implemented / static; `docs/devs/building/android.md`, `docs/android-java25.md`, `docs/android-modernization.md` and `android/README.md` describe OpenJDK 25/toolchain pins, D8/R8/ART limits, JNI changes and pending CI.
- [x] **[DOC-009]** Integrate the current upstream nested torrent guide/RPC reference and remove the flat duplicate.
  - Priority P1; Status Implemented / static; nested `built-in-torrent/` guide and adapted `torrents-rpc.md`; MkDocs nav/README links updated; link checker passes. Dynamic MkDocs/APK/daemon validation pending.
- [x] **[DOC-010]** Audit full upstream documentation coverage, remove duplicate pages and align fork guides with the real code.
  - Priority P1; Status Implemented / static; 46 upstream files are classified as 44 direct imports plus 2 mapped torrent pages; 74 fork-owned local paths (61 Markdown pages plus 13 assets) remain local (76 local-only paths including the two renamed pages); there are no HTML sources at the pinned commit. The inline torrent-RPC `<a id>` anchors were converted to Markdown `{#...}` anchors with `attr_list`; the old flat torrent guide and `interop-matrix.md` duplicate were removed. `sync.sh --docs` reports `Coverage OK`, and the link checker validates local anchors. Installation, configuration, Android JNI and platform guides were corrected against the CMake/source tree. MkDocs publication and CI/device validation remain pending.

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
  - Status Implemented / static; wrapper and AGP pins are current. A historical Gradle 9.8.1 run predates the latest lifecycle/JNI edits; current evaluation is pending CI.
- [x] **[AND-006]** Require JDK 25+ at Gradle evaluation and via Java toolchain.
  - Status Implemented / static; the root build rejects a Gradle JVM below 25 and the app toolchain requests exact JDK 25. Historical JDK 27 execution is not current validation; exact JDK 25 resolution is pending CI.
- [ ] **[AND-007]** Verify Gradle/AGP with OpenJDK 25 installed, `compileSdk 37`, NDK r30 LTS.
  - Priority P0; Status CI job prepared; Gradle unit tests, `lintDebug`, native arm64 JNI link and arm64 debug APK build are wired, and the job installs platform 37, Build Tools 36.0.0 and NDK 30.0.16248370. No current run exists for the uncommitted tree; instrumentation remains device-only.
- [ ] **[AND-008]** Build APK/DEX and run Android Lint/Error Prone.
  - Priority P0; Status CI job prepared / pending execution; no APK or Lint result is claimed.
- [x] **[AND-009]** Refactor Java sources (records, switch expressions, sealed types, lifecycle separation).
  - Priority P1; Status Implemented / static; immutable records, exhaustive switch expressions and modern pattern switches are used; service/UI ownership and the service-owned graceful deadline are documented. Dynamic compile/device validation pending.
- [x] **[AND-010]** Audit JNI references, exceptions, threads and shutdown races.
  - Priority P0; Status Implemented / static; native strings are decoded from ordinary UTF-8 to UTF-16 before `NewString` (no `NewStringUTF` modified-UTF-8 mismatch), data-directory access is mutex-protected, and native start failure propagates. C++ allocation exceptions at outer JNI boundaries remain a device/CI check; no runtime claim.
- [x] **[AND-011]** Test service lifecycle, rotation, background execution and restart.
  - Priority P1; Status Implemented / static; start/stop/restart and restart-flag separation reviewed; the app now stops itself on an explicit stop and on the API-35 timeout. The graceful deadline is owned by `RouterService` and enqueues native stop on its worker; `DaemonWrapper` publishes an explicit `stopping` state and no longer uses a `java.util.Timer`. `RouterServiceLifecycleTest` binds/unbinds the non-exported service without starting the daemon; pure-JVM tests and the instrumentation smoke test are prepared; emulator/device run pending.
- [x] **[AND-012]** Document exact JDK/Gradle/AGP/SDK/NDK matrix and Java 25 vs ART limitations.
  - Priority P1; Status Implemented / static; Java 25, FGS type, auto-start and validation limitations documented.
- [ ] **[AND-013]** Evaluate/implement an isolated router process with Binder/AIDL or a bounded same-process alternative; do not expose raw JNI or storage to UI.
  - Priority P1; Status Pending; current `RouterService` is same-process LocalBinder and deliberately non-exported. The Android CMake graph sets `WITH_BINARY=OFF` and does not link `i2pd_privsep`; the native supervisor/log worker and the planned network/storage/crypto workers do not create an Android process boundary. `PRIVSEP-012` tracks the process/UID migration; no raw JNI or storage is exposed to UI.
- [ ] **[AND-014]** Validate the `specialUse`/`dataSync` foreground-service split, Android 15 timeout policy, boot restrictions and Play Console declaration.
  - Priority P0; Status Pending device/Play review; manifest/service changes and the API-35 timeout callback are implemented and statically reviewed only.
- [ ] **[AND-019]** Enable UPnP by default in the Android app with a working native dependency and permission model.
  - Priority P0; Status Implemented / static, CI/device pending; `WITH_UPNP=ON` in the JNI graph, MiniUPnPc in the Android vcpkg manifest, `upnp.enabled=true` and the Settings toggle/name restored, `ACCESS_LOCAL_NETWORK` requested on API 37+, and a Wi-Fi multicast lock held while the router runs. JNI link, permission flow, gateway discovery and APK/Lint/device behavior remain unvalidated.

- [x] **[AND-018]** Replace the graceful-stop `java.util.Timer` with a service-owned deadline and an explicit `stopping` state.
  - Priority P1; Status Implemented / static; `RouterService` schedules the deadline on its main handler, enqueues the native stop on its worker, and `DaemonWrapper` publishes `State.stopping` before native teardown. `android/tools/java25-probe/check-config.sh` and `UiFormatTest` guard the contract; Gradle/device execution pending.
- [ ] **[AND-017]** Establish English-only Android resources and remove localization plumbing.
  - Priority P1; Status Implemented / static, CI pending; deleted 16 `values-<language>` catalogs and non-English Fastlane listings, added `locales_config.xml` with only `en`, manifest `localeConfig`, Gradle `androidResources.localeFilters`, removed Java/JNI/native language plumbing, pinned `UiFormat` to English and log case-folding to `Locale.ROOT`. Added `EnglishOnlyResourceTest` and `android/tools/check-english-only.pl`; no Gradle/device/Lint run.

- [ ] **[AND-016]** Complete the Material 3 GUI/navigation/accessibility/tablet redesign and lifecycle-efficient polling.
  - Priority P1; Status Implemented / static; branded toolbar/rail, detail Up/back state, dashboard quick actions, foreground-gated service observation and snapshot deduplication, `layout-w600dp` rail, heading/live-region fixes, wrapping navigation and backup/Recents hardening are prepared. The second UI pass adds a tinted toolbar mark, a service-disconnected banner with Retry, `Connections` navigation, lifecycle-based control capabilities, unknown-status guards, a capped/centred tablet content width, a determinate graceful-stop bar, Logs empty-state crash fix/debounce/summary, a dashed upload chart series, 15-second rates and `setTextIfChanged`. `MainActivityNavigationTest` now opens Logs and asserts the empty-state text. Pending lint/unit/device and TalkBack/font-scale/split-screen validation.

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
  - Status Implemented / static; `docs/attribution-audit.md`; personal PPA/donation/team and endorsement references removed. Debian/RPM `Maintainer` and changelog contacts are retained because package metadata requires a responsible address; they are not presented as product credits.
- [x] **[GOV-002]** Keep all GitHub Actions manual-only (`workflow_dispatch`).
  - Status Implemented; no automatic triggers.
- [ ] **[GOV-003]** Update CI to OpenJDK 25 for Android and add app/tool/docs checks when runnable.
  - Priority P1; Status Implemented / static; the Android job now wires the Java 25 guard, `:i2pd:testDebugUnitTest`, `:i2pd:lintDebug`, the native arm64 link and an arm64 debug APK build. Instrumentation remains device-only; current uncommitted changes are not published.
- [ ] **[GOV-004]** Re-run manual CI on Linux/OpenBSD/Windows/Android and record results.
  - Priority P0; Status Baseline recorded / pending rerun; published `main@800c8627` runs `39160538632` (Linux/Android), `39160540886` (Windows) and `39160543186` (OpenBSD) were dispatched and monitored with `gh`. Linux, Android, OpenBSD and the Windows MSYS2 job failed for the baseline reasons recorded in CI-002/CI-005/CI-008; Windows MSVC failed to find the VS 2022 generator; the job is pinned to `windows-2022`. These results validate only the published baseline and must not be attributed to the uncommitted fixes.
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
- [x] **[REBRAND-008]** Replace placeholder icon with a defined project identity when supplied.
  - Priority P3; Status Implemented via BRAND-001..004; visual/device validation pending.
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
  - Priority P0; Status Implemented / static; a separate supervisor process owns the log worker, forwards `ConfigureLog`/`LogRecord`, and restarts it. Outside the OpenBSD runtime sandbox the core can replace a dead supervisor; under that sandbox the core has no `exec` (only `proc` for bounded supervisor reaping) and logging stops after a supervisor death. Fork/restart/sandbox dynamic tests pending; storage/network/crypto workers are not yet under it.
- [ ] **[PRIVSEP-006]** Implement storage worker with typed allowlisted operations; migrate netDb/profiles/keys persistence.
  - Priority P0; Status Pending; core must lose persistent-write capability.
- [ ] **[PRIVSEP-007]** Implement network worker owning NTCP2/SSU2 sockets; core must lose direct network access.
  - Priority P0; Status Pending; state-machine ownership and nonce safety must be designed first.
- [ ] **[PRIVSEP-008]** Implement crypto worker/key handles for long-term operations, or document the boundary if latency makes it non-viable.
  - Priority P1; Status Pending.
- [ ] **[PRIVSEP-009]** Add per-process OpenBSD pledge/unveil policies for storage/network/core/crypto/supervisor.
  - Priority P0; Status Implemented / static for supervisor/log and role policy builder; storage/network/crypto/admin backends and core migration remain pending. See OPENBSD-SBX-4/5.
- [ ] **[PRIVSEP-010]** Add Linux seccomp/Landlock/namespaces/pidfd/cgroup limits and negative capability tests.
  - Priority P1; Status Implemented / static, CI pending; LinuxSandbox applies to log/supervisor with role policies, capability drop, Landlock, AF_INET-vs-AF_UNIX seccomp, optional net namespace, cgroup v2 helper and expanded negative tests. Storage/network backends and runtime validation remain pending.
- [ ] **[PRIVSEP-011]** Implement Windows ncalrpc/Named Pipes/Restricted token backend.
  - Priority P2; Status Implemented / static for worker message-mode pipes, protected DACL, peer SID/session checks, restricted token/Job Object and mitigation creation; AppContainer/LPAC and worker/supervisor daemon integration remain pending. No live Windows credential test.
- [ ] **[PRIVSEP-012]** Define Android process/UID limitations and migration.
  - Priority P2; Status In progress / static; the Android service remains same-process and non-exported, with no raw JNI or storage exposed to UI. The Android JNI CMake graph deliberately does not build `i2pd_privsep`; a separate `android:process`/AIDL router owner requires the native worker split and a new security/lifecycle review; not claimed.
- [ ] **[PRIVSEP-013]** Add IPC fuzzing, queue saturation, malformed message, descriptor-transfer and restart tests.
  - Priority P1; Status Implemented / static; malformed/EOF/restart, descriptor-transfer, missing-credential, ancillary-data and queue-pressure cases added to `test-privsep-linux-ipc`/`test-privsep-log`; Linux sanitizer execution pending.
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

## P. CI follow-up from the first manual runs

- [ ] **[CI-002]** Re-run Linux/Android, Windows and OpenBSD workflows after the portability fixes.
  - Priority P0; Status Baseline runs on published `main@800c8627`: `39160538632` failed in `test-privsep-protocol.cpp` (old `PayloadWriter::U64` name) and Android (`UnixDaemon.cpp` compiled against `DaemonAndroid`); `39160543186` failed in `privsep/Worker.cpp` (`getexecname()` undeclared on OpenBSD). The working tree renames the test codec calls, builds only `i2pd_jni` on Android with `WITH_BINARY=OFF`, and includes OpenBSD `<stdlib.h>`. These corrections are uncommitted and are not represented in the published runs. Windows run `39160540886` MSYS2 failed because the old selected-target build omitted `routerinfo` (tools smoke) and the DACL test used an `OW` ACE; the working tree builds all targets and uses the process SID. Windows MSVC failed to find the VS 2022 generator; the job is now pinned to `windows-2022`. The 2026-10-19 dispatches against the same published revision reproduced the Linux/Android, OpenBSD and Windows MSYS2/MSVC generator failures (`39189489953`, `39189495752`, `39189492769`); corrected-revision reruns remain blocked until publication.
- [ ] **[CI-003]** Android vcpkg/CMake: install `boost-json`/`boost-url`/`miniupnpc`, chainload the NDK CMake toolchain and verify the ARM64 JNI shared-library link with `WITH_UPNP=ON`.
  - Priority P0; Status Implemented / static; current published baseline is being rerun. Gradle APK/unit/Lint wiring is pending a published revision.
- [ ] **[CI-004]** Stabilize `i2pdctl` tests: raw B/s formatting and full ANSI/CSI stripping from OpenSSL diagnostics.
  - Priority P0; Status Implemented / static in the working tree; current published baseline rerun will determine whether the historical failures persist. No local test execution.
- [ ] **[CI-005]** Windows tools link `mswsock`/`ws2_32` through libi2pd for AcceptEx/GetAcceptExSockaddrs.
  - Priority P0; Status Implemented / static; explicit `mswsock`/`ws2_32` links were added to every Windows tool/test consumer and the router, not only the static-library interface. The published baseline's `tools-smoke` failed only because the old selected-target workflow did not build `routerinfo`; the working full build does. Windows CI pending.
- [ ] **[CI-006]** OpenBSD `Protocol.h` U64 name collides with OpenSSL `sha.h` macro; rename the codec method.
  - Priority P0; Status Implemented / static; all codec methods are renamed to `WriteU8/WriteU16/WriteU32/WriteU64BE/WriteString` and `ReadU8/...` to avoid OpenBSD/LibreSSL macro/type collisions. Wire format unchanged. OpenBSD CI pending.
- [ ] **[CI-007]** Add ASan/UBSan/TSan, fuzzing, interoperability and full-platform CTest jobs.
  - Priority P0; Status Implemented / static; Linux GCC+Clang full CTest with a per-label inventory check, full Windows MSYS2 CTest, OpenBSD CTest, Android Gradle unit/Lint/APK plus CTest Android static checks, pinned-Java fixture validation, ASan+UBSan, TSan and a separate manual fuzz workflow are prepared. Linux/OpenBSD platform tests are registered only on their host, and `WITH_FUZZERS=ON` adds optional `ctest -L fuzz` smoke entries. All workflows remain `workflow_dispatch`-only. Dynamic execution pending a published revision.
- [ ] **[CI-008]** Fix Windows named-pipe DACL primitive after the published baseline failed before the round trip.
  - Priority P0; Status Implemented / static; `test-tui-win32-pipe` now derives the current process SID and builds the same protected DACL as `TuiServerWin32` instead of relying on the `OW` owner-rights ACE. Windows CI pending.
- [ ] **[CI-009]** Pin the MSVC job to a runner image that provides the documented Visual Studio 2022 generator.
  - Priority P0; Status Implemented / static; the published `windows-latest` baseline could not find VS 2022, so the job now uses `windows-2022`. MSVC compile/test/install execution pending.

## Q. openi2pd visual identity

- [ ] **[BRAND-001]** Reconstruct the supplied logo as real vector geometry: full lockup, standalone symbol and horizontal lockup; preserve the original PNG as reference.
  - Priority P0; Status Implemented / static; `assets/branding/logo.svg`, `symbol.svg`, `logo-horizontal.svg`; visual review pending.
- [ ] **[BRAND-002]** Produce light, dark and monochrome variants plus PNG rasterizations; verify transparency, no embedded raster and no runtime font dependency.
  - Priority P0; Status Implemented / static; SVG XML parse clean; rendered PNG review pending.
- [ ] **[BRAND-003]** Establish one canonical color source with light/dark semantic tokens, error/warning/success, selection/focus/disabled and contrast checks.
  - Priority P0; Status Implemented / static; `assets/branding/colors.md` is the single source for core and semantic light/dark tokens, with WCAG ratios and explicit Android/TUI/CSS/HTTP-proxy/docs/icon mappings. `docs/assets/branding/colors.md` is an exact mirror. `tools/scripts/validate-branding.pl` rejects platform, SVG and brand-vector hex values absent from the canonical file, and validates derived docs/Android/Windows/packaging icon dimensions. Perl check passes; dynamic rendering checks pending.
- [ ] **[BRAND-004]** Replace Android legacy icons, add adaptive/launcher/splash resources and map Material 3 light/dark theme to the official palette.
  - Priority P0; Status Implemented / static; adaptive foreground/monochrome, legacy mipmaps, in-app marks and splash resources derive from the official symbol; the validator checks their dimensions and brand-vector fills. APK/AAPT2/Lint/device validation pending.
- [ ] **[BRAND-005]** Add a centralized ncurses theme with 8/16/256-color and monochrome fallbacks; no raw ANSI or bitmap logo dependency.
  - Priority P1; Status Implemented / static; TUI agent completed; terminal matrix validation pending.
- [ ] **[BRAND-006]** Integrate logo/naming into README, MkDocs, installation/build/Android/TUI/tools/security docs using relative asset paths.
  - Priority P1; Status Implemented / static; branding/navigation and palette references are integrated; inherited I2PD Browser promotion, personal Monero-node guidance, upstream documentation links in visible tool/config output, obsolete GNU Make cross-compilation instructions and the upstream wiki promotion were removed or replaced. `perl tools/scripts/check-doc-links.pl` reports 100 Markdown files and anchor targets OK, and `perl tools/scripts/validate-branding.pl` passes; MkDocs visual/build pending.
- [ ] **[BRAND-007]** Update desktop, Windows, Android, CMake and package icon/install metadata without changing protocol or data identifiers.
  - Priority P1; Status Implemented / static; desktop icon, Windows ICO/resource metadata, docs header/favicon, 256px packaging icon and Android launcher resources are derived from the official masters; dimensions and exact mirror/copy checks are in `validate-branding.pl`. Windows/CMake/desktop runtime validation pending.
- [ ] **[BRAND-008]** Remove obsolete inherited logos/icons only after replacement; preserve copyright, license, attribution, provenance and protocol diagrams.
  - Priority P0; Status Implemented / static; obsolete logos/icons remain replaced; user-visible inherited names were removed from the TUI torrent hint, Android log defaults, Windows firewall rule display name, AppArmor example comment, `--version`/help output, Windows error titles, the HTTP proxy information page and tool/config links. The Android UPnP default is intentionally product behaviour, not inherited branding: it is enabled by default with MiniUPnPc, a Settings toggle and the Android 17 local-network permission. Protocol names, `i2pd` executable/service/config/data identifiers and upstream-derived copyright/license notices are intentionally preserved. Security/review pass pending.
- [ ] **[BRAND-009]** Validate SVG/XML, no embedded rasters, transparent/light/dark rendering, small-size legibility, color consistency, Android/TUI capability and packaging references.
  - Priority P0; Status Static checks passed (SVG/XML, no raster, exact master/mirror/copy hashes, PNG/ICO/mipmap dimensions, contrast, docs links, palette-to-platform consistency, desktop validation); dynamic visual/device, MkDocs publication, APK/Lint, Windows installer and TUI terminal-matrix checks required.
- [ ] **[BRAND-010]** Legal/provenance audit for the supplied logo and generated assets; do not claim ownership beyond the project’s rights.
  - Priority P0; Status Pending; preserve upstream notices; document source provenance.
- [ ] **[BRAND-011]** Final cross-component consistency audit; keep the integration open until Android, TUI, docs, packaging and legacy cleanup use the same identity.
  - Priority P0; Status In progress / static; the Android, TUI, docs, tool and packaging surfaces use the same openi2pd mark, palette and visible naming. The static palette/link/branding checks pass. Remaining work is dynamic visual/device validation, MkDocs publication and CI compilation; no completion claim before those runs.

## Q. Architecture and cross-platform follow-up

- [ ] **[ARCH-001]** Keep the shared-interface ownership map current for `RouterContext`, transport APIs, crypto APIs, `TuiProtocol`, `privsep/Protocol`, JNI and CMake.
  - Priority P0; Status In progress; coordinator owns integration; no unilateral API changes.
- [ ] **[ARCH-002]** Reconcile documented target architecture with actual process/worker implementation; remove claims of completed separation.
  - Priority P0; Status In progress; `docs/security/privilege-separation.md`, `docs/architecture.md`.
- [ ] **[ARCH-003]** Define typed storage-worker operations and ownership before migrating netDb/profile/keys persistence.
  - Priority P0; Status Pending; depends PRIVSEP-006.
- [ ] **[LINUX-001]** Add seccomp-BPF/Landlock/capability/cgroup policy for workers with negative tests.
  - Priority P0; Status Implemented / static, CI pending; per-role Linux profiles now cover AF_INET/AF_INET6 vs AF_UNIX socket filters, Landlock write paths, capability/ambient/bounding-set drop, optional network namespace and cgroup v2 limits. Log/supervisor apply the sandbox; storage/network backends remain pending. `test-privsep-linux-sandbox` adds storage socketpair/no-capability and log/supervisor AF_UNIX tests. No local Linux execution.
- [ ] **[LINUX-002]** Validate peer credentials, pidfd, close_range and SOCK_SEQPACKET behavior on current Linux.
  - Priority P1; Status Partially implemented / static; `SO_PEERCRED`/`SCM_CREDENTIALS`/`SCM_RIGHTS` tests exist and `pidfd_send_signal` is used when available. Cross-UID and current-kernel execution remain pending.
- [ ] **[WIN-001]** Implement ncalrpc/Named Pipes worker transport with DACL, restricted token/AppContainer and Job Objects.
  - Priority P1; Status Implemented / static, CI pending; `WindowsIpc` adds message-mode local pipes, protected user/SYSTEM DACL, peer token SID/session checks, operation allowlist, restricted token, Job Object and mitigation-attribute spawning, and no handle inheritance. AppContainer/LPAC is probed but fails closed when requested. `test-privsep-windows-ipc` covers DACL/round trip/malformed/recovery/token/job. Daemon worker integration and cross-user/service tests pending.
- [ ] **[WIN-002]** Add Windows negative IPC/credential and firewall validation.
  - Priority P1; Status Pending; requires a second user/service account and a live Windows host.
- [x] **[TOOL-009]** Make the tool CLI and binary I/O portable across POSIX and MSVC.
  - Priority P0; Status Implemented / static; `tools/common/getopt.{h,cpp}` supplies getopt/getopt_long on Windows while Unix keeps the system parser; `i2pbase64` now uses C++ streams with binary stdio on Windows. CLI spellings are unchanged. Pending Windows CI.
- [x] **[TOOLS-007]** Continue tool security/regression audit after `famtool`/`keyinfo`/`autoconf_i2pd`; include address/offline-key tools.
  - Priority P1; Status Implemented / static; stdin/path bounds, private output permissions, secret-buffer wiping, `b33address`/offline-key/autoconf regressions and upstream provenance checks are in place. Dynamic sanitizer/Windows permission runs remain pending.
- [ ] **[TOOL-008]** Add a dedicated `test-vain` target if the pure base32/nonce helpers are factored out; current coverage is CLI/negative paths only.
  - Priority P2; Status Pending; requires coordinator CMake change.
- [ ] **[FW-008]** Native syntax validation for iptables/nft/PF/PowerShell backends in CI or dedicated hosts.
  - Priority P1; Status Prepared / not executed; see `docs/tools/routerinfo.md` and `FIREWALL-007`.
- [x] **[DOCS-009]** Validate all Markdown links/MkDocs navigation after torrent/Perl/Android docs.
  - Priority P1; Status Static link check passed; MkDocs rendering deferred to CI.
- [ ] **[DOCS-010]** Maintain portable mdoc manual pages for `i2pd`, `i2pd-tui` and `i2pdctl`; validate with mandoc on Linux/OpenBSD.
  - Priority P1; Status Implemented / mandoc-validated; packaging/install runtime checks pending.
- [ ] **[BRAND-011]** Complete executable/package/data-path migration decisions without breaking technical compatibility.
  - Priority P1; Status Pending; see REBRAND-005..007.
- [ ] **[LEGAL-001]** Audit new torrent/Perl/Android/CI files for required copyright, license and provenance notices.
  - Priority P0; Status In progress; preserve BSD-3-Clause and upstream notices.
- [x] **[LEGAL-002]** Use short `See the LICENSE...` reference headers in first-party source/config/script files; remove duplicated project copyright/SPDX boilerplate while retaining third-party and CC-BY-SA notices.
  - Priority P0; Status Implemented / static; project headers normalized; legal/license texts, generated files, fixtures and third-party notices retained; no compiler/license-scanner run.
- [ ] **[CI-001]** Keep workflows `workflow_dispatch`-only, actions SHA-pinned, checkout credentials disabled and workflow lint clean.
  - Priority P0; Status Implemented; zizmor/yamllint clean; re-run after workflow edits.

## N. BitTorrent native console and TUI

- [x] **[TORRENT-001]** Inventory all BitTorrent engine, RPC, web/JS, HTML/CSS, config, docs and tests; map every JS/RPC action to C++ behavior.
  - Priority P0; Status Implemented / static; no browser HTML/CSS/JS remains in the maintained tree; action mapping is recorded in `docs/torrent/console-matrix.md`. Pending CI/dynamic validation.
- [x] **[TORRENT-002]** Define typed snapshot/control interfaces in C++23, separate engine, sessions, persistence, control API and presentation.
  - Priority P0; Status Implemented / static; `TorrentsControl.*`, `TorrentsTunnel.*`, bounded service-thread snapshots. Pending CI.
- [x] **[TORRENT-003]** Implement torrent administration through the local TUI IPC, not HTTP/JSON-RPC or direct engine calls.
  - Priority P0; Status Implemented / static; typed `TORRENT-*` commands over the UID-gated local IPC. Pending CI.
- [x] **[TORRENT-004]** Add native ncursesw Torrents view with sortable/filterable table, selection, paging, resize and Unicode-safe long names.
  - Priority P0; Status Implemented / static; terminal resize/Unicode dynamic checks pending.
- [x] **[TORRENT-005]** Add details views for general/files/peers/trackers/session/DHT/errors.
  - Priority P0; Status Implemented / static; all engine-provided fields are bounded and surfaced; pending CI.
- [x] **[TORRENT-006]** Add `.torrent` file and magnet entry, start/pause/resume/stop, reannounce, remove, remove-with-files and explicit confirmations.
  - Priority P0; Status Implemented / static; `TORRENT-PAUSE`/`TORRENT-RESUME`
    are distinct typed commands (implemented by the engine stop/start paths),
    local path reads use the bounded storage service, and destructive delete
    requires typed confirmation.
- [x] **[TORRENT-007]** Integrate session/DHT statistics and rate/ETA formatting; update without blocking or unbounded redraw.
  - Priority P1; Status Implemented / static; pending terminal performance check.
- [x] **[TORRENT-008]** Remove torrent HTML/CSS/JS console and all enabling config/server code after parity mapping.
  - Priority P0; Status Implemented / static; no browser assets or web route remain; optional JSON-RPC retained separately.
- [ ] **[TORRENT-009]** Audit Transmission-compatible JSON-RPC: optional/off by default, loopback-only, auth, no wildcard CORS, bounded bodies, same validated operations, documented incompatibilities.
  - Priority P0; Status Implemented / static; dynamic RPC compatibility and auth tests pending.
- [x] **[TORRENT-010]** Preserve I2P-only torrent operation: no clearnet tracker/peer/DHT fallback, no public-IP advertisement, no unnecessary identifiers.
  - Priority P0; Status Implemented / static; the shared `IsI2PTrackerHost`
    check is applied in the tunnel constructor, `AddTracker` and
    `RequestTracker`; peer streams and datagram trackers resolve only
    AddressBook I2P destinations. Dynamic traffic capture is pending.
- [ ] **[TORRENT-011]** Prepare deterministic tests for control serialization, TUI actions, malformed requests, path/containment, parser bounds and RPC limits.
  - Priority P0; Status Partially implemented / static; bencode/TUI protocol
    (including pause/resume), tracker-host, lexical containment, torrent
    security and concurrency tests are prepared; control-service integration
    and live I2P traffic capture remain pending.
- [ ] **[TORRENT-012]** Evaluate advanced operations (file selection/priorities/queueing) against real engine support; implement only justified features.
  - Priority P2; Status Pending; do not advertise unsupported capabilities.
- [ ] **[TORRENT-IPC-001]** Define typed administrative torrent messages for future network/storage/core workers.
  - Priority P0; Status Implemented / validation pending; no HTTP dependency between TUI and engine.
- [ ] **[TORRENT-IPC-002]** Bound control queues and avoid large piece copies; snapshots only, no raw storage paths.
  - Priority P1; Status Partially implemented; per-request limits and bounded waits; queue/backpressure review pending.
- [ ] **[TORRENT-IPC-003]** Enforce network/storage separation for torrent workers (no process both connects externally and writes persistent data).
  - Priority P0; Status Partially implemented / static and OS-level separation
    pending. Network/control and storage callbacks use distinct execution
    domains; startup scans, reads, creation, resume and deletion run on
    storage, and the network thread performs only lexical path checks. The
    daemon is still monolithic, so the process invariant is not claimed.
- [ ] **[TORRENT-IPC-004]** Add Android/Windows/OpenBSD IPC mapping for torrent control without bypassing platform restrictions.
  - Priority P2; Status Partially implemented / static; Windows TUI uses the named-pipe transport; Android same-process service and OpenBSD runtime mapping remain pending.
- [ ] **[TORRENT-IPC-005]** Remove unbounded `.wait()` calls on the JSON-RPC I/O thread for torrent-get/set/DHT snapshots; use bounded waits/timeouts.
  - Priority P1; Status Implemented / static; all four RPC service waits bounded to CONTROL_DEFAULT_TIMEOUT with self-contained packaged tasks; dynamic timeout test pending.
- [ ] **[TORRENT-SEC-001]** Harden bencode/torrent parsing: bounds, depth, integer/length overflow, piece/file counts, duplicate/colliding paths and allocation limits.
  - Priority P0; Status Implemented / fuzz pending; files `Torrents.cpp`, tests.
- [ ] **[TORRENT-SEC-002]** Harden filesystem operations: traversal, absolute paths, `..`, symlinks/hardlinks, permissions, delete-local-data containment and TOCTOU.
  - Priority P0; Status Implemented / static; anchored no-follow
    open/create/read/write helpers, lexical network-side root checks,
    storage-side revalidation of every path, and bounded resume handling.
    Windows pre-open TOCTOU, hard links and rename races remain documented
    residual risks.
- [ ] **[TORRENT-SEC-003]** Audit malicious peers, message lengths, fragmentation, piece verification, partial writes, resume integrity and memory/open-file bounds.
  - Priority P0; Status Statically reviewed / sanitizer-fuzz pending; dynamic/fuzz validation pending.
- [ ] **[TORRENT-SEC-004]** Remove browser/clearnet exposure and document RPC threat model and incompatible changes.
  - Priority P0; Status Implemented / dynamic validation pending; `docs/torrent/security.md`.
- [ ] **[TORRENT-SEC-005]** Validate magnet info-hash length/alphabet (hex/base32), reject partial hashes, and bound startup `.torrent` loading.
  - Priority P0; Status Implemented / static review; tests for bounded file reads added; magnet runtime test pending.

## O. Perl migration of project scripts

- [x] **[PERL-001]** Recursively inventory maintained Python scripts and consumers (shebangs, Make/CMake/Gradle/workflows), excluding third-party/generated content.
  - Priority P0; Status Implemented / static; no project-maintained `.py`/`.pyw` or Python shebang remains; only `tools/scripts/i2pdctl` was project-maintained and is now Perl.
- [x] **[PERL-002]** Rewrite `i2pdctl` in portable Perl 5, preserving CLI, env defaults, JSON-RPC semantics, output and exit codes.
  - Priority P0; Status Implemented / static; core modules preferred; OpenBSD base Perl compatibility. Dynamic TLS validation pending.
- [x] **[PERL-003]** Update `tools/CMakeLists.txt`, docs, packagers and all invocations to the Perl script; remove Python only after consumer migration.
  - Priority P0; Status Implemented / static; CMake installs the Perl script and registers Perl tests. CI install validation pending.
- [x] **[PERL-004]** Add `perl -c`, Test::More argument/JSON/HTTP regression tests and local fake-server coverage.
  - Priority P0; Status Implemented / static; `i2pdctl.t`, `validate-branding.t` and CMake CTest hooks prepared. Dynamic execution pending CI.
- [x] **[PERL-005]** Document TLS fallback/dependencies; no CPAN runtime downloads; no Perl runtime dependency for the C++ daemon.
  - Priority P1; Status Implemented / static; docs and `.perlcriticrc` updated. Dynamic OpenBSD/Linux validation pending.
- [x] **[PERL-SEC-001]** Audit untrusted input, shell injection, path traversal, temp files/symlinks, permissions, Unicode, regex safety, dynamic loading and destructive operations.
  - Priority P0; Status Implemented / static; no `eval STRING` on external data; no shell interpolation; proxy environment variables are removed, relative `PATH` entries are ignored for `openssl`, and `perl-portability.t` rejects Python remnants, non-core modules and unsafe process/chmod/temp patterns.
- [x] **[PERL-SEC-002]** Preserve self-signed HTTPS behavior with a safe `openssl s_client` fallback when `IO::Socket::SSL` is unavailable; never silently downgrade to cleartext.
  - Priority P0; Status Implemented / static; argument-list `IPC::Open3`, bounded TLS exchange, proxy suppression and no-cleartext refusal; live TLS fixture pending.
- [x] **[PERL-SEC-003]** Validate `perl -c`, optional Perl::Critic, Test::More, invalid args, Unicode, filesystem errors and idempotence.
  - Priority P1; Status Implemented / static; CMake registers syntax, portability, Test::More, install-layout and opt-in `perl-critic` checks; proxy/idempotence regressions added; dynamic execution pending CI.
- [x] **[PERL-SEC-004]** Add transport regressions for URL injection, chunk framing, size limits, TLS fallback argv/no-cleartext and C0/C1 output escaping.
  - Priority P0; Status Implemented / static in `tools/tests/i2pdctl.t`; real TLS/OpenBSD execution pending.
- [x] **[PERL-006]** Add a Test::More wrapper and CTest target for the static branding validator.
  - Priority P2; Status Implemented / static; `tools/tests/validate-branding.t` and `perl-validate-branding` prepared. Pending CI.

## P. Torrent/Perl build and tests

- [ ] **[BUILD-011]** Add `TorrentsControl.cpp` to explicit CMake source lists; keep torrents optional and TUI optional.
  - Priority P0; Status Implemented / CI build pending; `build/CMakeLists.txt`.
- [ ] **[BUILD-012]** Ensure daemon/torrent engine builds without ncurses and without Perl runtime; no GNU Make.
  - Priority P0; Status Pending.
- [x] **[BUILD-013]** Install the Perl `i2pdctl` with correct interpreter/permissions and remove Python installation.
  - Priority P1; Status Implemented / static; `install(PROGRAMS)` preserves the portable shebang and executable mode, `perl-install-layout` installs only the `Tools` component into a private prefix and validates the result; CI execution pending.
- [ ] **[TEST-011]** Add torrent control/protocol/TUI unit and integration tests to CTest.
  - Priority P0; Status Protocol/bencode tests implemented; control/TUI integration pending.
- [ ] **[TEST-012]** Add malformed/truncated/oversized torrent and path/symlink tests, plus RPC limit/auth tests.
  - Priority P0; Status Bencode/path tests implemented; RPC integration pending.
- [ ] **[TEST-014]** Register `test-protocol-bounds` and `fuzz-protocol-bounds`; cover IdentityEx/LeaseSet truncation, I2NP bounds and NTCP2/SSU2 malformed paths.
  - Priority P0; Status Test/fuzzer sources added and CMake registered; execution deferred to CI.
- [ ] **[TEST-013]** Run Perl regression tests and compare old Python/Perl outputs where a fixture is available.
  - Priority P1; Status Prepared / static; the expanded suite now includes proxy/idempotence, portability and install-layout tests. Historical 54-test result predates the current tree; no Python comparison is possible because Python is not a target dependency. CI execution pending.

## Known defects / findings not yet fixed

- [x] **[DEF-001]** Non-atomic tunnel/router status fields read by TUI (data race under C++ memory model).
  - Status Implemented / static; RouterContext/TunnelBase status/creation fields are atomic; TSan stress test still required.
- [x] **[DEF-002]** No dynamic tests for SU3 malformed input; add fixture.
  - Status Implemented / static; `tests/test-su3-bounds.cpp` registered; not compiled/run.
- [ ] **[DEF-003]** Android Java source compiles with Java 25; APK/JNI/Lint/device tests are deferred to CI.
  - Status Pending CI; historical compiles predate the latest JNI/lifecycle edits.
- [ ] **[DEF-004]** `routerinfo` firewall backends not yet dynamically syntax-checked.
- [ ] **[DEF-005]** Full Java I2P differential audit not yet performed.
  - Status: latest stable remains 2.13.0 (rechecked 2026-10-19); third static pass added NET-155..NET-161/LS-119..LS-121/DB-113..DB-114/RS-101/SAM-110/DG-105/STR-104/CRY-202..CRY-203. No dynamic Java/C++ test or build executed.
- [ ] **[DEF-006]** Android manifest still allows cleartext? Removed; verify merged manifest.
- [ ] **[DEF-007]** `tools/vain` raw key-buffer arrays were replaced with zeroing RAII ownership, jthread joins and checked serialization/error paths.
  - Status Implemented / static only; ASan/TSan and one-character search pending.
- [ ] **[DEF-008]** `famtool` family certificate/private-key handling needs deeper review.
- [ ] **[DEF-009]** Core process still combines network, filesystem and long-term keys; only logging is separated.
- [ ] **[DEF-010]** Log worker is owned by a separate supervisor process with bounded restart; the explicit logger queue quota remains pending.
- [ ] **[DEF-011]** Torrent control/TUI/web removal is implemented statically; dynamic TUI/engine/RPC validation is pending.
- [ ] **[DEF-012]** Torrent network/storage process separation is not implemented; the monolithic daemon remains a residual risk.
- [ ] **[DEF-013]** Perl `i2pdctl` migration and HTTPS fallback need dynamic validation on OpenBSD/Linux.
- [ ] **[DEF-014]** Android Java 25 source compiled before the latest JNI/lifecycle/FGS edits; APK/JNI/resource-link/Lint and device behavior remain unvalidated; see H section.
- [x] **[DEF-016]** `Transports::Start()` after `Stop()` does not call `io_context::restart()`; restart may spin.
  - Status Implemented / static; `Start()` restarts a stopped `io_context`; dynamic restart test pending.
- [ ] **[DEF-017]** Torrent `Piece::m_Data` is still shared by the network and disk threads; a per-piece mutex now guards the buffer, block map and flags, but there is no single-owner immutable-buffer handoff yet.
  - Status Partially fixed / static; partial-block rejection, zeroed buffers, loading gate and per-piece locking added. `test-torrents-concurrency` is prepared for TSan; no ASan/TSan run exists.
- [ ] **[DEF-018]** Torrent filesystem paths needed component-wise no-follow handling, and some Windows hard-link/rename TOCTOU windows remain.
  - Status Partially fixed / static; anchored `openat`/`O_NOFOLLOW` helpers, bounded exact I/O and symlink-resistant rename/delete checks added. Windows reparse/hard-link/rename races and service-thread offload remain open.
- [ ] **[DEF-019]** DHT raw-datagram responses are correlated only by a 16-bit transaction ID and are unauthenticated; spoofing can influence routing/announce state.
  - Status Confirmed, unfixed; I2P-only traffic preserved. Needs protocol-level authenticity or stricter correlation.
- [ ] **[DEF-020]** Android JNI C++ allocation exceptions can still cross outer JNI boundaries; ordinary UTF-8 is now converted to UTF-16 instead of being passed to `NewStringUTF`.
  - Status Partially fixed / static; a malformed/4-byte/NUL string test and on-device run remain pending. See SEC-010.
- [ ] **[DEF-021]** Windows pipe name can be pre-created before the daemon starts (TUI availability), and same-UID Windows/Unix clients remain inside the documented trust boundary.
  - Status Confirmed, residual; dynamic Windows cross-user and pipe-squat tests pending.
- [ ] **[DEF-022]** SSU2 retains the legacy `GetSSU2Sessions(SSU2Sessions&)` reference form; the new copy-returning overload is safe, but callers must wait on the legacy future.
  - Status Confirmed, compatibility retained; no in-tree caller of the legacy form. Also verify the blocking `Stop()` future does not deadlock on the service thread.
- [ ] **[DEF-023]** NTCP2 control frames (`RouterInfo`/termination) are dropped if a data write is already in flight; the new guard prevents stream interleaving but has no queued/retry path.
  - Status Fixed / static; `m_PendingRouterInfo` and `m_PendingTerminationReason` queue control work to `HandleNextFrameSent`; termination close is completion-driven. Dynamic interleaving/termination test pending.
- [ ] **[DEF-024]** `RunnableService` destruction/stop from its own worker thread still needs an explicit self-join contract; some service timer/socket callbacks are not executor-owned.
  - Status Partially fixed / static; run flags are atomic, start/stop is serialized and NTP/SSU2 shutdowns are bounded. I2CP defers its own join locally; a generic self-join contract and TSan/restart matrix remain pending.
- [x] **[DEF-025]** `Log::SetTimeFormat`/`m_TimeFormat` were unsynchronized.
  - Status Implemented / static; format access is mutex-protected and timestamps are formatted from a snapshot; CI pending.
- [ ] **[DEF-026]** `Tunnels`/`NetDb`/`RouterContext` restart paths are only partially reinitialized; normal daemon CLI does not restart, but Android/test restart may expose stale state.
  - Status Partially fixed / static; Tunnel/Timestamp/NTP/RouterContext service and NetDb lifecycles now expose explicit cleared/restartable state. Full Android/daemon restart matrix and stale-timer/socket checks remain pending.
- [ ] **[DEF-027]** `TorrentsTunnel` has no complete self-owned teardown; teardown is now marshalled to the service with a bounded wait and the destructor joins the disk worker, but timer/disk handlers still capture raw `this`.
  - Status Partially fixed / static; requires a weak token/full lifecycle guard and CI lifecycle tests.
- [ ] **[DEF-028]** Garlic accepts up to the protocol-sized authenticated tag set per message with no additional cap; a compromised destination can force large tag churn.
  - Status Fixed / static; `MAX_INCOMING_ELGAMAL_TAGS` and `MAX_INCOMING_ECIES_TAGS` plus a four-NEXT_KEY payload cap now bound the work. Dynamic tag-budget test pending.
- [ ] **[DEF-029]** Some OpenSSL AEAD/HKDF/BN wrapper paths still ignore return codes or narrow `size_t` to `int`; calls are currently used with bounded buffers.
  - Status Confirmed, residual; needs compiler/OpenSSL-version matrix and targeted error injection.
- [ ] **[DEF-030]** `SSU2Session::SendData` uses `0` both as failure and as the first valid packet number; a failed send can be mistaken for packet 0.
  - Status Fixed / static; `SendData` returns `bool` and an out-parameter packet number; all send/retransmit call sites only store successful packets. Dynamic loss test pending.
- [ ] **[DEF-031]** SSU2 peer-test messages 5/6/7 rely on nonce-derived connection IDs and the intro key without additional origin authentication.
  - Status Partially mitigated / static; source endpoint, reserved-range and ban checks now gate out-of-session PeerTest; nonce/ConnID binding remains protocol-defined. Dynamic abuse test pending.
- [ ] **[DEF-032]** `SSU2Session::CreateAckBlock` may under-ACK when cached ranges are clamped to a smaller caller buffer; currently safe but not dynamically interop-tested.
  - Status Fixed / static; NACK-only range generation, cache invalidation, bounds and `acnt`/range guards added; Java differential ACK test pending.

## R. Memory-safety and concurrency audit (2026-10-18)

All items below are **implemented by static analysis** and remain pending
compilation, sanitizer, fuzz, platform and integration validation. See
`docs/security/memory-safety-2026.md` for causes, exploit conditions and
residual risks.

- [ ] **[MEM-001]** Restrict out-of-session SSU2 PeerTest payload blocks and reject forged data-phase blocks.
  - Priority P0; Status Implemented / static; `SSU2OutOfSession.*`, `SSU2Session.*`; policy regression in `test-ssu2-control-plane`; live forged packet/MSan pending.
- [ ] **[MEM-002]** Serialize NTCP2 shutdown on the service executor and wait before stopping the IOService.
  - Priority P1; Status Implemented / static; `NTCP2.cpp`; TSan/ASan shutdown stress pending.
- [ ] **[MEM-003]** Bound Ratchet NEXT_KEY work and incoming ECIES/ElGamal tag maps.
  - Priority P0; Status Implemented / static; `ECIESX25519AEADRatchetSession.cpp`, `Garlic.cpp`; tag-budget and ASan tests pending.
- [ ] **[MEM-004]** Remove the ECIES tag-map iterator invalidation across callbacks.
  - Priority P1; Status Implemented / static; `Garlic.cpp`; debug-STL rehash regression pending.
- [ ] **[MEM-005]** Validate padding-clove capacity and abandon oversized batches.
  - Priority P2; Status Implemented / static; `ECIESX25519AEADRatchetSession.cpp`; near-limit ASan test pending.
- [ ] **[MEM-006]** Keep an owning RouterInfo buffer snapshot during DatabaseStore serialization.
  - Priority P0; Status Implemented / static; `I2NPProtocol.cpp`; snapshot assertion in `test-routerinfo-update-transaction`; concurrent TSAN test pending.
- [ ] **[MEM-007]** Publish remote LeaseSet replacements copy-on-write instead of mutating reader-visible objects.
  - Priority P0; Status Implemented / static; `Destination.cpp`; concurrent update/read TSAN and functional replacement tests pending.
- [ ] **[MEM-008]** Lock NetDb own-router/floodfill startup mutations against the request thread.
  - Priority P1; Status Implemented / static; `NetDb.cpp`; Start/Stop/lookup TSAN stress pending.
- [ ] **[MEM-009]** Validate `router.keys` size, read, parse and public identity before use.
  - Priority P1; Status Implemented / static; `RouterContext.cpp`; corrupt/truncated-key fixture pending.
- [ ] **[MEM-010]** Validate TunnelTest clove length before reading msgID/timestamp.
  - Priority P2; Status Implemented / static; `RouterContext.cpp`; truncated clove regression pending.
- [ ] **[MEM-011]** Reject I2CP RECONFIGURE SESSION before the destination exists.
  - Priority P0; Status Implemented / static; `I2CP.cpp`; first-message integration test pending.
- [ ] **[MEM-012]** Keep DatagramSession alive across asynchronous LeaseSet requests.
  - Priority P0; Status Implemented / static; `Datagram.cpp`; destroy-during-request ASan test pending.
- [ ] **[MEM-013]** Use owning callbacks and null checks in HTTP proxy resolver/connect paths.
  - Priority P0/P1; Status Implemented / static; `HTTPProxy.cpp`; reload/stop during DNS ASan test pending.
- [ ] **[MEM-014]** Do not stop/delete a SAM destination still shared by another session or tunnel.
  - Priority P0 impact; Status Implemented / static; `SAM.cpp`; two-session lifecycle test pending.
- [ ] **[MEM-015]** Reject partial/misaligned torrent blocks, zero piece buffers, bound piece block maps, serialize DHT query removal and prevent serving loading pieces.
  - Priority P0/P1; Status Implemented / static; `Torrents.cpp/.h`, `TorrentsDHT.cpp`; partial-block regression in `test-torrents-security`; parser budget/TSAN/live peer tests pending.
- [ ] **[MEM-016]** Fix SCM credential PID comparison, ancillary alignment and fdopen ownership; initialize daemon PID state.
  - Priority P1/P2; Status Implemented / static; `privsep/Channel.cpp`, `LogWorker.cpp`, `daemon/Daemon.h`; Linux IPC/UBSan/fd tests pending.
- [ ] **[MEM-017]** Add exception boundaries to Android JNI entry points and make client Stop/Reload null-safe.
  - Priority P0/P1; Status Implemented / static; `i2pd_android.cpp`, `ClientContext.cpp`, `DaemonAndroid.cpp`; instrumentation/device tests pending.
- [ ] **[MEM-018]** Re-run ASan+UBSan, TSan, fuzzing, OpenBSD/Windows/Android and live-peer tests after the memory-safety corrections.
  - Priority P0; Status Pending CI; no current uncommitted result is claimed.
- [ ] **[MEM-019]** Serialize RouterContext address/property/caps/MTU/floodfill mutators with signed-buffer rebuilds.
  - Priority P1; Status Implemented / static; `RouterContext.cpp/.h`, `GetRouterInfoProperty()`; `test-routercontext-update-race`; TSan and remaining live-object readers pending.
- [ ] **[MEM-020]** Harden torrent filesystem paths against traversal, symlink/reparse points and TOCTOU.
  - Priority P0; Status Implemented / static; `Torrents.cpp/.h`, `TorrentsTunnel.cpp`; `test-torrents-security`; Windows/hard-link/rename residual risks documented.
- [ ] **[MEM-021]** Add a bounded, read-only TUI configuration snapshot with no secret or raw-file exposure.
  - Priority P2; Status Implemented / static; `TuiProtocol.h`, `TuiServerCommon.cpp`, `tui/main.cpp`; protocol vectors prepared; runtime pending.
- [ ] **[MEM-022]** Broaden per-tool and IPC regression coverage without weakening credential checks.
  - Priority P1/P2; Status Implemented / static; `tools-regressions`, `test-privsep-linux-ipc`; CI execution pending.
- [ ] **[MEM-023]** Enforce the SSU2 per-message block whitelist and independent pre-auth guards for PeerTest, Address and I2NP handlers.
  - Priority P0; Status Implemented / static; `SSU2Session.*`, `SSU2OutOfSession.*`; `test-ssu2-preauth`, `fuzz-protocol-bounds`; CI and forged-packet/ASan validation pending.
- [ ] **[MEM-024]** Bound SSU2 token, recently-connected and PeerTest caches; support and validate multi-fragment SessionConfirmed.
  - Priority P1; Status Implemented / static; `SSU2.cpp/.h`, `SSU2Session.cpp`; fragment/loss tests pending.
- [ ] **[MEM-025]** Fix ECDSA P-521 DER sizing, checked AES wrappers, low-order X25519, ECIES coordinate capacity and lazy crypto caches.
  - Priority P0/P1; Status Implemented / static; `Signature.*`, `Crypto.*`, `ECIESX25519AEADRatchetSession.cpp`, `Ed25519.cpp`, `Gost.cpp`; P-521/AES/X25519 regressions and `fuzz-signature-der` prepared; CI pending.
- [ ] **[MEM-026]** Give torrent pieces a mutex-guarded state machine, rebuild corrupt complete pieces and check peer frame prefixes before storing lengths.
  - Priority P0/P1; Status Implemented / static; `Torrents.cpp/.h`; `test-torrents-security`, `test-torrents-concurrency`; TSan/CI pending.
- [ ] **[MEM-027]** Marshal torrent teardown to the service and join the disk worker without blocking the caller indefinitely.
  - Priority P1; Status Implemented / static; `TorrentsTunnel.*`; restart/TSan tests pending.
- [ ] **[MEM-028]** Serialize I2CP session creation/termination, SAM accept/write queues and ClientContext destination snapshots.
  - Priority P0/P1; Status Implemented / static; `I2CP.*`, `SAM.*`, `ClientContext.*`; `test-i2cp-session-lifecycle`, `test-sam-stream-lifecycle`, `test-clientcontext-registry`; CI pending.
- [ ] **[MEM-029]** Add the common JNI exception frontier, serialized daemon calls and immutable RouterInfo status snapshots.
  - Priority P0/P1; Status Implemented / static; `android/app/src/main/cpp/*`; JNI/device validation pending.
- [ ] **[MEM-030]** Remove the zero-hop TunnelPool self-deadlock and serialize tunnel restart/latency/TCSR state.
  - Priority P0; Status Implemented / static; `TunnelPool.*`, `Tunnel.*`, `TransitTunnel.*`; `test-tunnelpool-*`, `test-tunnels-restart`; TSan/CI pending.
- [ ] **[MEM-031]** Serialize timestamp/NTP, RouterContext service/static-key, NetDb and Logger lifecycle transitions.
  - Priority P1; Status Implemented / static; `Timestamp.*`, `util.*`, `RouterContext.*`, `NetDb.*`, `Log.*`; lifecycle regressions prepared; CI pending.
- [ ] **[MEM-032]** Bound IPC sends/TUI clients and child reaping with absolute deadlines.
  - Priority P1; Status Implemented / static; `privsep/Channel.cpp`, `privsep/Worker.*`, `daemon/TuiServer*`; Linux IPC/child tests pending.

## I2PSnark native parity continuation (2026-10-19)

Reference and exhaustive area inventory: `docs/torrent/snark-compatibility.md`.
These tasks supplement, and do not close, TORRENT/PRIVSEP/ANDROID/MEM tasks above.
All current changes are local: **pending CI compilation; pending CI tests**.

- [ ] **[SNARK-001]** Magnet and v1 metadata parity.
  - Priority P0; Status implemented by static analysis; pending CI compilation; pending CI tests.
  - Bounded hex/base32/percent-decoded btih, contradictory-topic rejection, multiple I2P tracker hints; BEP 12 tier roundtrip and pure ordered-file/piece-hash v1 constructor. Private metadata suppresses DHT/PEX and closes pre-metadata connections; late discovery callbacks are guarded.
  - Files: `Torrents*`, `tests/test-torrents-magnet.cpp`, `tests/test-torrents-engine-interop.cpp`.
  - Still pending: storage-domain directory enumeration/streamed hashing and complete create operation via IPC/TUI/Android. v2-only unsupported; hybrid v1 projection is not BEP 52 validation.
- [ ] **[SNARK-002]** I2P tracker administration and exact endpoint membership.
  - Priority P0; Status implemented by static analysis; pending CI compilation; pending CI tests.
  - Paused-torrent add/remove through typed local IPC and TUI t/T; validates I2P HTTP/UDP URLs, preserves infohash, captures immutable metainfo for storage. Explicit empty list persists and suppresses common-tracker fallback. Private swarms never use unrelated common trackers.
  - Still pending: BEP 12 failover/shuffling, reclaim unused global tracker slots, persistence failure acknowledgements/crash atomicity and Android tracker editing. Edit churn safely reaches the bounded registry quota until restart.
- [ ] **[SNARK-003]** I2P DHT and PEX interoperability/security.
  - Priority P0; Status implemented by static analysis for opaque tokens, transaction/NID binding, packet/port validation, private/stopped guards and complete PEX/DHT extension dictionaries; pending CI compilation; pending CI tests.
  - Prepared `test-torrents-dht-interop`; existing parser fuzzer extended. Remaining: Java live trackerless exchange, zero-length foreign tokens, seed/noseed semantics, XOR routing parity, per-origin abuse budgets, raw sender metadata and response-size reconciliation.
- [ ] **[SNARK-004]** Comments/ratings interoperable opt-in subsystem.
  - Priority P1; Status implemented by static analysis for independent `ut_comment` codec and disabled-by-default bounded local store; pending CI compilation; pending CI tests.
  - UTF-8/UTF-16 field bounds, age timestamps, ratings, deduplication and local hiding; regression and fuzz entry prepared. Remaining: negotiated peer exchange, rate budgets, per-torrent persistence, admin IPC, native TUI/Android controls and differential Java fixtures. No comments capability is advertised yet; labels are unauthenticated pseudonyms.
- [ ] **[SNARK-005]** Autonomous native Android torrent client.
  - Priority P0; Status implemented by static analysis for enabled native engine, private-storage session bootstrap, bounded service/JNI administration and Material Views torrent panel; pending CI compilation; pending CI tests.
  - Panel supports magnets, sharing, list/progress, pause/resume/remove, files/peers/trackers details and DHT. Prepared Java protocol regressions; Android vcpkg includes Boost JSON/URL.
  - Remaining: SAF broker and torrent import, creation, selection/priorities, tracker edits, comments, external-router mode, notifications/device battery/service lifecycle validation; first config write crash recovery.
- [ ] **[SNARK-006]** Enforce torrent operating-system capability separation.
  - Priority P0; Status pending. Same-process service/disk threads and JNI are not OS privilege separation. Network, session, storage and verification workers, supervisor, typed descriptor/capability IPC and negative tests are still required on Linux/OpenBSD/Windows/Android. No claim of satisfying the no-network-plus-persistent-write invariant.
- [ ] **[SNARK-007]** Native parity CI and reference provenance.
  - Priority P0; Status implemented by static analysis for test/CMake/manual-workflow registration, functional/protocol matrices and per-file license review notes; pending CI compilation; pending CI tests.
  - Four new CTest targets: magnet, engine-interop, dht-interop, comments; existing TUI protocol regression expanded and torrent fuzzer extended. Java live/differential, sanitizer/fuzzer, OpenBSD/LibreSSL, Windows and Android device validation remain pending.
  - No local build/configure/compiler/test/Gradle/binary execution; no commit/push. Earlier CI results apply only to their recorded revisions.
- [ ] **[SNARK-008]** Complete remaining functional inventory.
  - Priority P1; Status pending. File selection/priorities and piece-boundary scheduling, per-session/per-torrent bandwidth/connection limits, durable pause/queue state and crash recovery, full error/retry parity, advanced TUI/Android settings, creation and comments must remain open until implemented and validated.

## Native init services and Debian packaging (2026-10-19)

Status: implemented by static analysis; pending manual CI service/package
validation. These tasks do not claim a successful build, service start or
package install.

- [ ] **[SERVICE-001]** Provide a native systemd unit with foreground `Type=exec`, restricted user, journald logging, native state/runtime directories, bounded restart and IPC-compatible hardening.
  - Priority P0; Status implemented / static; `contrib/i2pd.service`; explicit `--foreground` overrides a local `daemon=true`, no `Type=notify`, no self-daemonizing mode. `systemd-analyze`, start/stop/status/reload and sandbox/IPC checks pending CI.
- [ ] **[SERVICE-002]** Provide native GNU Shepherd, OpenRC, runit, s6/s6-rc and SysVinit definitions with foreground supervision and secure paths.
  - Priority P0; Status implemented / static; `contrib/shepherd`, `contrib/openrc`, `contrib/runit`, `contrib/s6`, `contrib/s6-rc`, `contrib/sysvinit`; only the log worker is isolated. Manager start/stop/failure/signal tests pending CI.
- [ ] **[SERVICE-003]** Select exactly one init system at CMake install time and install no competing service definitions by default.
  - Priority P1; Status implemented / static; `I2PD_SERVICE_MANAGER`, `I2PD_SERVICE_DIR`, `I2PD_SERVICE_CONFDIR`, `Service` component; `tests/check-build-system.cmake`, `tests/check-services.cmake`; configure/install matrix pending CI.
- [ ] **[DEB-001]** Modernize Debian packaging for C++23, debhelper 13, native systemd/SysV/tmpfiles, split `i2pd`/`i2pd-tui`, DEP-5 metadata and autopkgtest.
  - Priority P0; Status implemented / static; `debian/`; build, `dh_missing`, lintian, autopkgtest, install/upgrade/remove/purge and systemd/SysV coexistence pending CI.
- [ ] **[SERVICE-004]** Add manual CI validation for service contracts and Debian packages.
  - Priority P0; Status implemented / static; `.github/workflows/packaging.yml` is `workflow_dispatch`-only, actions are pinned and checkout credentials are not persisted; dispatch and failure triage pending.
- [ ] **[SERVICE-005]** Document enabling, stopping, restarting, status, logging, hardening and Debian package behavior for every supported manager.
  - Priority P1; Status implemented / static; `docs/service-management.md`, `docs/user-guide/run.md`, `docs/user-guide/install.md`, `docs/build-system.md`, `docs/devs/building/unix.md`; rendered documentation and link checks pending CI.

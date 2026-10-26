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
`CI`=J, `TEST`=C/G/Torrent, `PRIVSEP`=M, `TORRENT`=Torrent, `PERL`=Perl,
`I18N`=H/Docs.

Status values: `Pending`, `In progress`, `Blocked`, `Implemented / static`,
`Partially implemented / static`, `Pending CI compilation`, `Pending CI tests`,
`CI failure`, `Correction pending`, `Validated by CI`, `Dropped (reason)`.
Add a parenthetical qualifier only when needed, for example
`Historical evidence (pre-policy)` or `Prepared / not executed`.

Permanent development policy: local compilation, build configuration and test
execution are prohibited for all agents and subagents. Dynamic validation is
CI-only through manual `workflow_dispatch` workflows. Local work is static
analysis, implementation, test preparation and CMake/workflow updates. Every
significant change also performs the `Agent Knowledge Maintenance` review in
`AGENTS.md` (skills, technical docs and tracking). See
`AGENTS.md` “Permanent priority policy: static-only local work”. Historical
local build/test results in this file refer to earlier work and do not validate
current uncommitted changes.

Priority values: `P0` critical, `P1` high, `P2` normal, `P3` low.

Responsible: the coordinator owns integration; each task is executed by the
subsystem owner in the ownership table unless a task says otherwise. The
`Files` field identifies the review boundary.

## A. I2P Java interoperability

Current comparison target: `i2p-2.14.0`, commit
`3c98503c5f14efe7bdd783528ba7f621f12d8081` (remote annotated tag peeled on
2026-10-21). Existing 2.13.0 findings/vectors retain their original provenance;
they do not establish 2.14.0 compatibility. See NET-026.

- [x] **[NET-001]** Record the original I2P Java 2.13.0 comparison checkout (historical).
  - Priority P0; Status Implemented; tag `i2p-2.13.0`, commit `9134f808337b401e8e53c73734c81fab04280c9d`, released 2026-07-24; read-only clone outside the tree (not bundled).
  - Files: external read-only Java reference; `docs/INTEROPERABILITY.md`, `tests/interop/README.md`.
  - Acceptance: tag, commit, version, date recorded; no Java code added to fork.
  - Validation: original 2.13.0 reference only; superseded as comparison target by 2.14.0 on 2026-10-21. No 2.14.0 review is attributed to this task.
- [ ] **[NET-002]** Audit NTCP2 handshake, obfuscation, options, version negotiation and PQ variants against Java.
  - Priority P0; Status Implemented / static; NET-118..NET-123 plus NET-134..NET-140 cover replay, block ordering, termination/control queueing, X/Y/static-key validation, Alice replay, pending-handshake bounds, extra pending bytes, per-version padding and proxy buffer lifetime.
  - Files: `libi2pd/NTCP2.cpp`, `NTCP2.h`; Java `router/transport/ntcp2`; `test-ntcp2-session-confirmed-payload` and `test-ntcp2-handshake-bounds` passed locally.
  - Acceptance: matrix rows with class/function references and any fix.
  - Validation: differential/session/replay/PQ tests pending.
- [ ] **[NET-003]** Audit SSU2 handshake, tokens, relay, peer test, fragmentation, MTU and IPv6 against Java.
  - Priority P0; Status Implemented / static; NET-124..NET-154 cover tokens, headers, PeerTest, PQ/Retry, ACK/NACK, RTO/backoff, multi-fragment SessionConfirmed, MTU, per-family addresses and transport lifecycle.
  - Files: `libi2pd/SSU2*.cpp`, `SSU2Session.h`, `Transports.*`, `RouterInfo.*`, `RouterContext.*`; Java `router/transport/ssu2`; new SSU2 tests passed locally.
  - Acceptance: matrix rows and fixes; OpenBSD live MTU/IPv6 path pending.
- [ ] **[NET-162]** Close the 2026-10-21 SSU2 handshake/ACK/PeerTest/relay integration gaps.
  - Priority P1; Status implemented by static analysis; pending CI compilation and pending CI tests. Fixes: late packet 0 no longer rewinds the receive point/ACK; wrong-type short packets are dropped instead of triggering handshake retransmission; session removal is owner-checked; failed SessionCreated resets Noise state; resend throttle predicate corrected; PeerTest msg 1 source/family/target validation and ±120 s freshness; private/reserved PeerTest/RelayIntro targets rejected; HolePunch per-target/rate admission; late `Established` callbacks posted to the service thread.
  - Files: `libi2pd/SSU2Session.*`, `libi2pd/SSU2.*`, `libi2pd/SSU2OutOfSession.*`, tests `test-ssu2-*`.
  - Validation: prepared reordered/duplicate/loss, wrong-type flood, PeerTest policy and callback-race regressions; no compiler, CTest, TSan/ASan, live relay/PeerTest/MTU/IPv6 run exists.
- [ ] **[NET-163]** Close the 2026-10-21 NTCP2 msg3p2 and termination parity gaps; track remaining options/delayed-close work.
  - Priority P1/P2; Status implemented by static analysis; pending CI compilation and tests. `m3p2len` now uses Java 2.14.0 bounds 459..4048 (send and receive); `REASON_BANNED` marks the peer unreachable. The delayed-close fail state is now implemented (NET-171); the 12-byte options/padding policy, admission cap and establisher-thread socket ownership remain open (NET-172).
  - Files: `libi2pd/NTCP2.*`, `tests/test-ntcp2-*`, `docs/INTEROPERABILITY.md`.
  - Validation: static asserts prepared for 459/4048; no boundary/session/replay/PQ run.

- [ ] **[NET-168]** SSU2 inbound admission and PeerTest resource bounds.
  - Priority P1; Status Implemented / static; Pending CI compilation; Pending CI tests. `SSU2_MAX_INBOUND_SESSIONS` (40) is checked before constructing an inbound session; a SessionRequest/TokenRequest at the cap receives a Retry carrying termination reason 19, other types are dropped without inserting the temporary session. PeerTest gain a per-source (v4 /32, v6 /64) fixed-window limit of 12 per 600 s (bounded 256-entry map), a global `SSU2_MAX_PEER_TEST_SESSIONS` (64), Bob rejects with code 3 instead of evicting live state, and Charlie checks `AddSession()` before sending msg 5 (code 66 on failure).
  - Files: `libi2pd/SSU2.{h,cpp}`, `libi2pd/SSU2Session.{h,cpp}`, `tests/test-ssu2-admission.cpp`, `tests/test-ssu2-peertest-policy.cpp`.
  - Residual: the cap covers handshake states only; established-session count remains governed by the existing peer/transport bounds. Capacity selection is not adaptive like Java's.
- [ ] **[NET-169]** SSU2 PeerTest/relay trust boundary.
  - Priority P1 (security); Status Implemented / static; Pending CI compilation; Pending CI tests. HolePunch/Introduced payloads are limited to DateTime, Address, RelayResponse, Padding and Termination (no RouterInfo/NewToken/fragments/relay/PeerTest/ACK). PeerTest msg 5 no longer publishes or updates our external address; msg 7 applies only a corroborated host observation from the Charlie learned in msg 4 (IP and port) after endpoint/port/reserved/blocklist checks, and never publishes a port from one test. The msg 4 to msg 6 target must pass `IsValidPeerTestEndpoint`, server family support and the operator blocklist. Router-hash ban/blocklist checks were added at Charlie msg 2, RelayIntro, Alice msg 4 and Bob's Charlie selection.
  - Files: `libi2pd/SSU2Session.{h,cpp}`, `libi2pd/SSU2.{h,cpp}`, `libi2pd/SSU2OutOfSession.{h,cpp}`, `tests/test-ssu2-holepunch-whitelist.cpp`, `tests/test-ssu2-peertest-policy.cpp`.
  - Residual: no "too close"/own-netblock proximity check for msg 6; a mapped NAT port can no longer be published from a single peer test (Java also requires corroboration).
- [ ] **[NET-170]** SSU2 conformance and memory hardening.
  - Priority P2; Status Implemented / static; Pending CI compilation; Pending CI tests. `Established()` in `SessionConfirmedSent` requires an ACK covering packet 0 (`acnt == ackThrough`); handshake clock skew is 120 s with an RTT/2 allowance; PeerTest/HolePunch accept `ver` 2..4 while still sending 2; the RouterInfo block requires the single-fragment byte `0x01`; a version-rejection Retry with token 0 now requests a fresh token instead of failing (PQ fallback no longer dead-ends); `m_Address` is mutex-guarded for the cross-thread transports read; out-of-sequence fragments charge the fixed slab with a dedicated 128 KiB budget and the per-session incomplete-message cap is 32; relay tags are non-zero, first-wins and expire (10 s Bob / 30 s Alice) with an explicit role; `Testing` is set only after the msg 4 signature verifies and cleared on every exit; the introducer predicate rejects tagless/expired entries and selects by session family; published/effective IPv6 MTU is capped at 1488.
  - Files: `libi2pd/SSU2Session.{h,cpp}`, `libi2pd/SSU2.{h,cpp}`, `libi2pd/SSU2OutOfSession.*`, `libi2pd/RouterContext.cpp`, `tests/test-ssu2-control-plane.cpp`, `tests/test-ssu2-session-fragment-bounds.cpp`, `tests/test-ssu2-peertest-policy.cpp`.
  - Residual: `ProcessData` still establishes Alice on any authenticable data packet (deliberate); incomplete-message expiry is observed at the 72 s cleanup pass despite the 10 s timeout; socket sharing between receive and send threads and the memory-pool lifetime across deferred teardown remain SEC-025 items.
- [ ] **[NET-171]** NTCP2 AEAD-failure, padding, skew and netID hardening.
  - Priority P1; Status Implemented / static; Pending CI compilation; Pending CI tests. SessionCreated padding limits are corrected to the ntcp2-hybrid table (v4 1168, v5 1648) with a 3296-byte buffer equality check. A data-phase AEAD failure now enters a bounded drain (18..1041 bytes, 10 s, fixed buffer) and only then sends reason 4; a stalled frame sends reason 14 after 30 s. Clock skew sends SessionCreated normally and terminates with reason 7 after SessionConfirmed; a msg1 netID mismatch bans the source; a signed foreign `netId` is exposed via `RouterInfo::HasForeignNetId()` and answered with reason 17 + unreachable + ban. The RouterInfo request-flood flag is honoured for blocks under 3 KiB; malformed DateTime/Termination block sizes are fatal.
  - Files: `libi2pd/NTCP2.{h,cpp}`, `libi2pd/RouterInfo.{h,cpp}`, `tests/test-ntcp2-handshake-bounds.cpp`, `tests/test-ntcp2-failstate.cpp`, `tests/test-ntcp2-netid-signal.cpp`.
  - Residual: the 2-byte length read has no deadline; the `available()` fast path still performs a blocking read on the event loop; NTCP2 inbound admission cap, 12-byte options negotiation and establisher-thread socket I/O remain open (now tracked as NET-172).
- [ ] **[NET-172]** NTCP2 admission, options and cross-thread socket ownership.
  - Priority P1/P2; Status Missing. Audit findings: no established-session cap and a 512-slot pending map keyed per exact IP (an IPv6 /64 can hold all slots) with a 28-33 s sweep for a 10 s timeout; the establisher thread initiates `async_read`/`async_write` on a socket whose close/send is owned by the NTCP2 service thread; padding Options blocks are parsed for length only, never merged or emitted. Planned: per-prefix pending keying and failure ban, session cap, per-session handshake timer, strand/post ownership for socket I/O, and a 12-byte options value type.
  - Reference: spec/ntcp2 SessionRequest/Options; Java `InboundEstablishState`, `NTCPConnection`, `NTCP2Options`.
- [ ] **[NET-004]** Audit I2NP message types, serialization, timestamps and malformed handling.
  - Priority P1; Status Statically reviewed; bounds fixes in NET-015/I2P-002; dynamic pending.
- [ ] **[NET-005]** Audit RouterInfo/LeaseSet/LeaseSet2/EncryptedLeaseSet parsing and validation.
  - Priority P1; Status Implemented / static; LS-109..LS-125, RI-005..RI-016 and DB-109..DB-125 cover LeaseSet/RouterInfo parsing, freshness, ownership, replacement and persistence; dynamic vectors pending.
- [ ] **[NET-006]** Audit netDb/floodfill lookup, store, expiration and reseed behavior.
  - Priority P1; Status Implemented / static; DB-104..DB-125 cover netDb replacement, DLM/DSRM, clove origin, lookup/flood throttling, lifecycle, persistence and lookup bounds; dynamic floodfill/lookup pending.
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
  - Priority P1; Status Implemented / static, CI pending; NET-155..NET-161, LS-119..LS-121, DB-113/DB-114, RS-101, SAM-110, DG-105, STR-104 and CRY-202..CRY-203 corrected. PARITY-002..PARITY-004 subsequently added SSU2 path/flood retransmission, runtime ML-KEM probing, RSA verification and SAM persistent framing; residual SAM quoted/routing edge cases, per-session datagram version and I2CP TYPE_KEY/secret association remain. No Java peer, differential run or sanitizer execution.
- [ ] **[I2P-001]** Implement MetaLeaseSet (DatabaseStore type 7) parsing, signature/identity/lease validation, storage, lookup and malformed-input handling.
  - Priority P1; Status Implemented / static; LS-115 Meta parsing, 16-entry/earliest bounds, netDb store/serve and Java-canonical header/revocation rejection (I2P-014); dynamic Java vector pending. `LeaseSet.*`, `NetDb.cpp`; `test-leaseset2-java213-regression` fixture rebuilt Java-canonically.
- [ ] **[NET-024]** Apply the 2026-10-19 crypto/ratchet/PQ static audit.
  - Priority P0/P1; Status Implemented / static, CI pending; CRY-206..CRY-214: NSR/ES payloads are pre-validated before side effects, NS uses a deliberate stricter non-empty Garlic policy (Java accepts an empty clove set), NS replay cache is 5-minute/4096-entry, tag index 65535 is accepted, tag-budget checks precede chain advancement and evict expired entries, LibreSSL/no-PQ publication and selection are gated, unsupported hybrid keys are not advertised, Ed25519ph now uses Java-compatible plain Ed25519, and RedDSA keys are wiped. PARITY-002 added the runtime ML-KEM provider probe. Residual: type-5/type-7 NSR vectors, 7/4 multi-key session matrix, Java-generated NS/NSR vectors and ASan/TSan/CI execution. No dynamic validation.
- [ ] **[NET-164]** Apply the 2026-10-21 Ratchet/ECIES/AEAD/signature/Elligator2/PQ crypto audit.
  - Priority P0/P1; Status implemented by static analysis; pending CI compilation and pending CI tests. CRY-215..CRY-225: CSPRNG-derived RNG seeds and fail-closed peer ordering; AEAD multi-buffer failure aborts the NTCP2 write before sequence advance; ratchet/AES/NTCP2 key zeroization; ratchet tag boundary 0..65535 plus expired-tag eviction and one forward/one reverse NEXT_KEY per payload; automatic NSR/ACK fallback timer for every ClientDestination; LS2 rejects RSA offline transient keys while Datagram2/Streaming retain Java policy; Ed25519 blinding validates the decoded point; GOST applies RFC 7091 e=1-if-zero with scalar cleansing; SSU2 resets KEM state on version fallback and consumes IncompatibleVersion Retry; explicit `i2cp.leaseSetEncType` fails closed when unsupported; I2CP LeaseSet2 private-key lengths are validated.
  - Files: `libi2pd/util.cpp`, `IdentMetrics.*`, `Tag.h`, `Crypto.*`, `Ed25519.*`, `Blinding.cpp`, `Gost.cpp`, `Signature.h`, `LeaseSet.*`, `Datagram.cpp`, `Streaming.cpp`, `Destination.*`, `Garlic.*`, `ECIESX25519AEADRatchetSession.*`, `NTCP2.*`, `SSU2Session.cpp`, `libi2pd_client/I2CP.*`; tests `test-rng-seed`, `test-aeadchacha20poly1305`, `test-gost-sig`, `test-eciesx25519-ratchet-tagsets`, `test-ntcp2-*`.
  - Validation: prepared differential/negative vectors; no compiler, CTest, ASan/TSan, provider matrix, Java peer, OpenSSL/LibreSSL, live ratchet or workflow run exists. The NSR timer is implemented; its lifecycle test, the deliberate NS empty-clove policy and ML-KEM NS/NSR transcript vectors remain pending.

- [ ] **[NET-165]** Apply the 2026-10-21 RouterInfo/DatabaseLookup/SearchReply/floodfill/LeaseSet netDb audit.
  - Priority P0/P1; Status implemented by static analysis; pending CI compilation and pending CI tests. DB-115..DB-125 and LS-122..LS-125: clove-delivered I2NP keeps its untrusted origin and cannot bypass direct-only DLM/DSM policy; client LeaseSet DSRMs are bound to queried peers, deduplicated and capped at 16 followed hashes; per-(sender,tunnel) lookup and per-key flood throttles are bounded; fresh zero-address RouterInfos are stored but not selectable; RouterInfo persistence takes one atomic updated snapshot, installs the buffer before publishing the flag, and replaces files via a temporary rename; reseed preserves the local RouterInfo/floodfill entry and is fenced by Stop; Start uses locked counts; encrypted LS2 rejects RSA blinded signature types; LS2 PopulateLeases is a no-op; Meta lifetime is bounded by the latest entry; LS1 buffers are trimmed to the signed body.
  - Files: `libi2pd/I2NPProtocol.*`, `RouterContext.cpp`, `RouterInfo.*`, `NetDb.*`, `NetDbRequests.*`, `Destination.*`, `LeaseSet.*`, `Reseed.cpp`; tests `test-tunnel-dsm-policy`, `test-leaseset2-java213-regression`, `test-netdb-*`.
  - Validation: prepared malformed/replay/throttle/canonical-buffer regressions; no compiler, CTest, ASan/TSan, live floodfill/Java peer or workflow run. Reply-key 0x12 remains intentionally fail-closed because Java 2.14 still has no normative sender/KDF.

- [ ] **[NET-166]** Apply the 2026-10-21 tunnel-build/I2NP/Garlic routing audit.
  - Priority P0/P1; Status implemented by static analysis; pending CI compilation and pending CI tests. TU-102..TU-106: reject duplicate peers in a path, reject zero receive/next tunnel IDs, publish the replacement zero-hop outbound tunnel, choose zero-hop from the selected path (not base length), advance the shared RNG for record shuffles and accept varied lengths for pre-expiry recreation. I2NP-006..I2NP-010: reject LOCAL delivery at an OBEP, whitelist clove-delivered types, reserve the gateway header and fail oversized wrappers, validate the inner I2NP expiration and add bounded per-tunnel IV replay plus reassembly tables. GAR-001..GAR-004: bound local clove lengths, enforce clove freshness, make encryption fallible and null-check router sessions. CONC-006/CONC-007: owner-bind and unregister custom selectors and synchronize explicit/trusted peer configuration.
  - Files: `libi2pd/Tunnel.*`, `TunnelBase.h`, `TunnelPool.*`, `TunnelEndpoint.*`, `TransitTunnel.cpp`, `I2NPProtocol.*`, `RouterContext.cpp`, `Garlic.*`, `TunnelGateway.*`, `CryptoKey.*`, `Identity.h`, `RouterInfo.*`, `LeaseSet.*`, `libi2pd_client/MatchedDestination.*`; tests `test-tunnelpool-zero-hop`, `test-tunnel-dsm-policy`, `test-protocol-bounds`, `test-eciesx25519-ratchet-session`.
  - Validation: prepared peer-uniqueness, zero-ID, clove-origin/expiry, gateway-boundary/expiry, IV replay, selector-lifetime and peer-config race regressions; no compiler, CTest, ASan/TSan, fuzzer, live tunnel or workflow run. No privilege-separation boundary carries tunnel keys; that invariant remains unchanged.

- [ ] **[PARITY-015]** Apply the 2026-10-21 user-services (HTTP/SOCKS/reseed/address-book) audit.
  - Priority P1/P2; Status implemented by static analysis; pending CI compilation/tests. HTTP-101: case-insensitive `.i2p`/`.i2p.alt` classification and blocking of loopback/private clearnet outproxy targets. SOCKS-101: case-insensitive/underflow-safe `.i2p` classification, IP-literal conversion and port-0/private target rejection. RS-102..RS-106: TLS chain/hostname verification, bounded SU3/ZIP/HTTP sizes and entry counts, and network-bundle freshness. AB-101..AB-105: `.i2p.alt` lookup alias, `www.` fallback, 5 MiB subscription/entry caps, and tombstone enforcement on lookup/jump inserts. TUI/Android per-tunnel inventory/replace action remains open.
  - Files: `libi2pd_client/HTTPProxy.*`, `SOCKS.cpp`, `AddressBook.*`, `libi2pd/Reseed.*`, `Config.cpp`, `docs/FEATURE-PARITY.md`, docs.
  - Validation: prepared classifier, subscription bound, reseed cap/freshness and b32/b33 regressions; no compiler, CTest, sanitizer, fuzzer, Java peer, live proxy/reseed or device run. No commit/push.

- [ ] **[NET-167]** Apply the 2026-10-21 SAM/I2CP/Streaming/Datagram audit.
  - Priority P1/P2; Status implemented by static analysis; pending CI compilation/tests. SAM-117..SAM-121: per-port acceptor teardown/queue removal, closed-socket accept guard, PORT/duplicate subsession validation, SESSION STATUS on create errors, whitespace-normalised HELLO and balanced session shutdown. I2CP-109..I2CP-111: b33 HostReply checks the blinded store key, one terminal MessageStatus per nonce, and CreateLeaseSet2 stages/validates keys before installing. STR-108..STR-112: bounded saved packet tables, locked stream lookup, port-preserving ping/pong, SYN authentication/open-stream acceptance and terminated-send guard. DG-108..DG-110: unauthenticated Datagram3 cannot create/ack a session, wildcard receiver replacement and Datagram1/2 identity refresh.
  - Files: `libi2pd_client/SAM.*`, `libi2pd_client/I2CP.cpp`, `libi2pd/Streaming.*`, `libi2pd/Destination.*`, `libi2pd/Datagram.*`, `libi2pd/LeaseSet.*`; tests `test-sam-*`, `test-i2cp-*`, `test-streaming-*`, `test-datagram-*`.
  - Validation: prepared parser/async/replay/port regressions; no compiler, CTest, ASan/TSan, fuzzer, Java peer, SAM/I2CP client or workflow run. I2CP multi-key map publication remains a TSan residual (CONC-008). No commit or push.

- [ ] **[NET-025]** Apply the 2026-10-19 client-protocol static pass.
  - Priority P1; Status Implemented / static, CI pending; SAM-112..SAM-116 (large frames, SILENT connect, LISTEN_PORT accept, no 3 s expiry, signature types), I2CP-106..I2CP-108 (direct b33 association, HostReply LS2 options, TYPE_KEY merge), DG-106/DG-107 (session-table null guard, no inbound version mutation, hash-only DG3) and STR-105..STR-107 (async incoming LeaseSet, SYN MTU, offline/close authentication). Residual: SAM tokenizer/pipelining, secret-blinded I2CP, per-session datagram version, Datagram3/Streaming vectors and all dynamic validation. SAM 3.3 `LISTEN_PROTOCOL` and protocol-aware RAW dispatch/receiver reset are implemented statically (`test-sam-subsessions`).
- [ ] **[I2P-002]** Implement DatabaseLookup reply-key flags `0x12` (ECIES ratchet public key without tag) with correct encrypted replies and no cleartext fallback.
  - Priority P1; Status Implemented / static; DB-104/DB-105 strict 0x02/0x10 parsing and 0x12 fail-closed. Java 2.14.0 parses 0x12 into an unused key, replies cleartext (its wrapper supports only 0x02/0x10) and has no sender/KDF; the spec form is TBD, so no KDF is invented. `NetDb.cpp`, `test-netdb-store-lookup-hardening` matrix prepared.
- [ ] **[I2P-003]** Validate DatabaseSearchReply sender/from hash and pending-request association; reject truncated/fabricated replies.
  - Priority P1; Status Implemented / static; DB-106..DB-108 strict DSRM parser, mandatory trailing from, one reply per queried peer and 4096 pending-request cap; `test-netdb-dsrm-correlation`/`test-netdb-lookup-wire-bounds` passed locally.
- [ ] **[I2P-004]** Add short/long tunnel-build request time windows, stale/future/replay handling and correct rejection codes.
  - Priority P1; Status Implemented / static; `TransitTunnel.cpp`; 8-minute age/5-minute future drop semantics and code-30 layer-encryption rejection; duplicate-build Bloom filter and dynamic clock-skew tests pending.
- [ ] **[I2P-005]** Apply Java one-hour RouterInfo freshness to floodfill lookups without affecting LeaseSet/local responses.
  - Priority P1; Status Implemented / static; one-hour RouterInfo freshness plus clock fudge, hidden/self exclusion, DB-112 persistence bounds and the 54-minute `U`-cap/introducer plus 75-minute SSU-only predicates (NET-176); live aged-RI lookup pending. `NetDb.cpp`, `test-netdb-store-lookup-hardening`.
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
- [ ] **[TEST-021]** Re-run the registered CTest suite in manual CI on the current revision.
  - Priority P0; Status Pending CI tests; historical evidence (pre-policy): Linux/GCC 16.2.0/OpenSSL 4.0.3 CTest **48/48** passed at that time, but this does not validate the current uncommitted tree. No Java peer, sanitizer, fuzzer, OpenBSD/LibreSSL, Windows or Android execution.
- [ ] **[TEST-022]** Integrate all subsystem tests into CMake/CTest and the manual GitHub Actions matrix.
  - Priority P0; Status Implemented / static; every applicable `test-*.cpp` is registered, `interop-static`, `perl-doc-links`, `android-config-static` and `android-locale-policy` are CTest targets, and suite labels group i2p/security/memory/IPC/TUI/torrent/tools/Perl/interop/platform/Android. Linux-only sandbox/IPC tests are now registered only on Linux, and OpenBSD policy/imsg tests only on OpenBSD. Linux Clang/GCC, full Windows MSYS2 CTest, OpenBSD, Android unit/Lint/APK, ASan+UBSan, TSan and fuzz smoke jobs are prepared. No current workflow run includes these uncommitted changes; compilation/tests pending CI.
- [ ] **[SEC-014]** Review malformed RouterInfo address/property parameter parsing and SSU2 address-family mismatch.
  - Priority P1; Status Implemented / static; RouterInfo option bounds/duplicates/introducer indices and SSU2 cross-family Address handling fixed; malformed corpus pending.
- [ ] **[OPENBSD-SBX-1]** Fix OpenBSD reseed URL canonicalization in `Daemon.cpp`; `AbsolutePath()` currently prefixes non-absolute URLs.
  - Priority P0; Status Implemented / dynamic validation pending; URL is left untouched and the unveil setup skips it.
- [ ] **[OPENBSD-SBX-2]** Register/implement `unix.handle_sigtstp` on OpenBSD or remove the dead TSTP/CONT path.
  - Priority P1; Status Implemented / static; option is now registered for OpenBSD; dynamic signal test pending.
- [ ] **[OPENBSD-SBX-3]** Decide whether a custom `openbsd.pledge_file` may bypass the fixed startup pledge; document or enforce.
  - Priority P1; Status Implemented / static; the file is now a **subset selector**, not additive: one bounded parser serves both phases, comments/blank lines are rejected, every token must already be in the phase base set, duplicates are ignored, and the runtime phase rejects `proc exec id chown dpath prot_exec pf bpf vmm tty audio video tape disklabel drm wroute settime`. Startup fails when the runtime set is not a strict subset of the startup set. Dynamic OpenBSD test pending.
- [ ] **[OPENBSD-SBX-4]** Add native OpenBSD per-role policies, startup-time worker spawning and capability checks.
  - Priority P0; Status Implemented / static, CI pending; `OpenBSDSandbox` builds supervisor/log/storage/network/crypto/admin policies, the supervisor/log pair is spawned before the runtime pledge, workers refuse euid 0, and `test-privsep-openbsd-policy` forks negative storage/network capability cases. No OpenBSD compile/runtime result.
- [ ] **[OPENBSD-SBX-5]** Migrate storage, network, crypto and admin backends under the OpenBSD policies and add typed imsg/SCM_RIGHTS operations.
  - Priority P0; Status In progress / static; storage uses a pre-pledge broker, inherited root fd4 and supervised workers; network/crypto/admin and the core network boundary remain pending.
- [ ] **[OPENBSD-SBX-6]** Complete supervised OpenBSD storage startup without post-pledge path walks.
  - Priority P0; Status Implemented / static, CI pending; the broker no longer uses `openat()` after pledge or needs `rpath`. It passes its immutable root at fd4; the worker walks the relative path, verifies dev/inode/ownership and the configured pathname before applying pledge/unveil. Forwarded workers are disowned after channel handoff, brokers return typed per-request errors, and launches use a replenished one-minute window. No OpenBSD runtime result.
- [ ] **[OPENBSD-SBX-7]** Tighten storage and imsg capability policies.
  - Priority P0; Status Implemented / static, CI pending; Storage requires a non-empty absolute unveil root and drops unused `fattr`/`flock`; `ImsgChannel::AdoptValidated` checks socket type, CLOEXEC and `getpeereid` before fdpass, failed sends poison the channel, and descriptor ownership matches `Channel`. Tests prepared; no OpenBSD runtime result.
- [ ] **[OPENBSD-PID-1]** Implement a readiness check for the daemonized rc.d start; `--daemon` currently exits before pidfile/pledge/transport readiness.
  - Priority P1; Status Implemented / static; daemon parent waits up to 60 s for a close-on-exec readiness byte after pidfile/sandbox/logger/start; timeout kills the child. Dynamic rc.d test pending.
- [ ] **[OPENBSD-LOG-1]** Synchronize `Log::Reopen()` with the logger thread for SIGUSR1 rotation.
  - Priority P1; Status Implemented / static; logger destination/stream replacement and writes are synchronized; rotation stress procedure documented. Dynamic SIGUSR1 test pending.
- [ ] **[OPENBSD-VAL-1]** Execute `docs/security/openbsd-validation.md` on a real OpenBSD host (pledge/unveil, pidfile, signals, SSU2/MTU, worker role policies, imsg/SCM_RIGHTS).
  - Priority P0; Status Pending; dynamic validation required, including `test-privsep-openbsd-policy`, `test-privsep-openbsd-imsg`, supervised storage path replacement, broker-survives-rejection and worker-survives-broker-exit cases.

## B. C++23 modernization

- [x] **[CPP-001]** Require C++23 in CMake; remove C++20 fallback and obsolete preprocessor branches.
  - Status Implemented / static; CMake contract and source inspection only. Historical GCC 16 evidence predates the static-only policy and does not validate the current tree.
- [ ] **[CPP-002]** Audit remaining raw owning pointers and manual `new[]`/`delete[]`.
  - Priority P1; Status Partially implemented / static; Streaming/Gzip, LeaseSet/Identity, NTCP2/Transports, client temp buffers, TorrentsDHT, daemon pimpl and library thread owners converted. CPP-013 additionally converts `Piece::m_Data`, `UDPTunnel`'s resolver thread and `TunnelConfig`'s hop chain. Remaining raw owners are the Streaming packet pool and the ElGamal precomputation table; no compile/sanitizer run.
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
- [ ] **[CPP-012]** Add OpenSSL 4.0.x provider/TLS compatibility without breaking OpenSSL 3.x or LibreSSL.
  - Priority P0; Status Implemented / static; pending CI compilation/tests. CMake reads the selected `opensslv.h`, enforces OpenSSL 3.0.2+/LibreSSL 4.0+, exposes `I2PD_OPENSSL_EXPECT*` and compile-only EVP/X.509/TLS feature probes. `Crypto` probes provider digests/ciphers/HKDF/SipHash via `EVP_*_fetch`, `Family`/`Reseed` use provider pkey APIs, family tooling uses checked EVP signing and PKCS#8 keys, TLS contexts use explicit client/server roles, I2PControl uses SHA-256, and PQ publication/selection is runtime-gated with `CanPublishPQ`/`HighestSupportedPQVersion`. No local compiler or test was run.
  - 2026-10-25 continuation: HKDF uses `EVP_KDF`/`OSSL_PARAM` on probed providers (legacy bridge retained for LibreSSL); CMake stores a backend/probe stamp, links one combined modern-API probe, probes both `SSL_set1_dnsname` and `SSL_set1_host`, and fails when neither exists; SipHash pins a 16-byte MAC and the probe signs; ML-KEM availability performs an encapsulate/decapsulate round trip; TLS contexts set an explicit 1.2 floor; I2PControl checks the certificate/key pair; reseed CN extraction is bounded; famtool writes its key atomically/private; DSA/ECDSA DER sizing, RSA modulus/digest bounds, Ed25519 digest/BN checks, ECIES/ElGamal failure propagation and secret cleansing were added; LeaseSet2/I2CP reject unavailable EncTypes and the preferred type follows installed keys.
  - Files: `build/CMakeLists.txt`, `libi2pd/Crypto.*`, `CryptoKey.*`, `IdentMetrics.cpp`, `Ed25519.cpp`, `Signature.*`, `Family.*`, `Reseed.cpp`, `LeaseSet.*`, `Destination.cpp`, `PostQuantum.*`, `RouterInfo.cpp`, `NTCP2.cpp`, `SSU2.cpp`, `SSU2Session.cpp`, `daemon/I2PControl.cpp`, `libi2pd_client/I2PTunnel.cpp`, `libi2pd_client/I2CP.cpp`, `tools/famtool.cpp`, `tests/test-openssl-capabilities.cpp`, `tests/test-leaseset2-enc-types.cpp`, `tests/check-build-system.cmake`.
  - Validation: `test-openssl-capabilities` and `test-leaseset2-enc-types` prepared; OpenSSL 3.0.2, 3.5.x, 4.0.0/4.0.3, LibreSSL and restricted-provider runs remain pending. No live TLS, PQ provider, famtool sign/verify or package run exists.
- [ ] **[CRY-226]** Complete the OpenSSL 4.0 provider/X.509/signature hardening pass.
  - Priority P1; Status Implemented / static; Pending CI compilation; Pending CI tests. Prepared changes cover modern HKDF, probe/link detection, SSL hostname fallback, SipHash size, ML-KEM KEM round-trip, DSA/ECDSA DER sizing, RSA fixed modulus, Ed25519 failure handling, ECIES/ElGamal bool results and cleansing, I2PControl key-pair check, TLS floors, bounded CN extraction, famtool atomic private writes, and LeaseSet2/I2CP EncType gating.
  - Residual: Java 2.14.0 vectors for Ed25519ph/type 8, blinding and NTCP2 `pq` list semantics; low-level EC_KEY/DSA fallback probes; `InitCrypto` fail-closed is wired in the daemon but restricted-provider CI and all backends remain unexecuted; Reseed/Family still parse PEM through a throwaway SSL object.
  - Validation: no compiler, CTest, OpenSSL tool, LibreSSL lane, fuzzer or live peer run for this increment.
- [ ] **[CRY-227]** Fix ECIES-X25519-AEAD-Ratchet NSR split selection and tagset exhaustion.
  - Priority P1; Status Implemented / static; Pending CI compilation; Pending CI tests. Pending `(receiveTagset, sendTagset)` splits are retained (bounded 8, 180 s) instead of overwriting `m_SendTagset`; the outbound tagset is promoted when the matching inbound tagset first consumes a tag (Java `firstTagConsumed`), so a lost latest NSR no longer breaks Bob→Alice traffic. A delayed NSR on an established session cannot rewind the tagset ID/index. Tag-index exhaustion marks the session send-exhausted but readable; the next send opens a new session instead of removing it.
  - Files: `libi2pd/ECIESX25519AEADRatchetSession.{h,cpp}`, `libi2pd/Garlic.{h,cpp}`, `tests/test-eciesx25519-ratchet-session.cpp`.
  - Residual: `m_SendTagset` is still written from the receive path without a lock held by send readers (pre-existing; TSan needed); the 8-entry bound is a local policy.
- [ ] **[CRY-228]** Harden the ratchet payload, tag window and secret handling.
  - Priority P2; Status Implemented / static; Pending CI compilation; Pending CI tests. New Session Reply accepts only DateTime/Garlic/Options/Padding and cannot execute Termination/NEXT_KEY/SessionID/ACK or remove the session; `crypto.ratchet.inboundTags` is clamped to Java's 320; the spec-1c anonymous one-time NS path (flags section Java cannot parse) is removed while router-N stays `32+payload+16`; shared secrets and derived keys in NS/NSR/candidate/NEXT_KEY/router-N paths are cleansed.
  - Files: `libi2pd/ECIESX25519AEADRatchetSession.*`, `libi2pd/Garlic.*`, tests.
  - Residual: adaptive ratchet-vs-ElGamal ordering and the 200 ms per-destination New Session gate are unchanged (CRY-238).
- [ ] **[CRY-230]** Make X25519 key generation and ownership fail closed.
  - Priority P1; Status Implemented / static; Pending CI compilation; Pending CI tests. `X25519Keys::GenerateKeys`, `CreateECIESX25519AEADRatchetRandomKeys` and `LocalEncryptionKey::GenerateKeys` return bool; `PrivateKeys::GenerateCryptoKeyPair` rejects zero private/public material and takes the existing fallback; static-key, pool, NTCP2/SSU2/ratchet ephemeral consumers retry or fail closed; `RouterContext` refuses to persist a zeroed static key. `X25519Keys` is non-copyable with a safe move and derives/compares the caller-supplied public key.
  - Files: `libi2pd/Crypto.{h,cpp}`, `CryptoKey.{h,cpp}`, `Identity.{h,cpp}`, `Transports.cpp`, `RouterContext.cpp`, `ECIESX25519AEADRatchetSession.cpp`, tests.
  - Residual: a test-only keygen fault-injection seam is not available; provider failure paths remain unexercised.
- [ ] **[CRY-232]** Correct Ed25519 secret cleansing, canonical `S` and off-curve decoding.
  - Priority P2; Status Implemented / static; Pending CI compilation; Pending CI tests. Blinded scalars and secret nonces use `BN_clear_free`; `BlindPublicKey`/`BlindPrivateKey` return bool and zero outputs on failure; the hand-rolled verifier rejects `S >= L` and compares with `CRYPTO_memcmp`; `DecodePoint`/`IsValidPublicKey` are fail-closed for off-curve points.
  - Residual: the provider-free Ed25519 signer remains variable-time on secret scalars (documented decision); `Ed25519::Verify` remains unused in production.
- [ ] **[B33-202]** Reject the two-byte b33 signature-type flag.
  - Priority P2; Status Implemented / static; Pending CI compilation; Pending CI tests. Java 2.14.0 throws for `flags & 0x01`; local now rejects it and the hardening test pins rejection instead of canonicalization.
- [ ] **[CRY-233]** Reject RSA RouterInfo identities.
  - Priority P2; Status Implemented / static; Pending CI compilation; Pending CI tests. Java `DatabaseEntry.verifySignature` rejects RSA for netDb entries (DoS policy); `RouterInfo::ReadFromBuffer(bool)`/`Update()` now reject RSA before verification, while the RSA primitive remains for SU3/local use. `test-routerinfo-rsa-reject` covers valid-signature rejection and an Ed25519 control.
- [ ] **[CRY-234]** Strict `leaseSetEncType` and inert unknown LS2 keys.
  - Priority P2; Status Implemented / static; Pending CI compilation; Pending CI tests. Exact decimal tokens only; more than one hybrid or ElGamal+hybrid combinations are rejected like Java; unsupported types fail closed. Unknown LS2 encryption-key sections are retained (type + opaque length) for `GetNumEncryptionKeys`/`IsEncryptionKey` and are never selectable; known-type length mismatches still reject the LS2. Shipped defaults `6,4,0` became `6,4`.
  - Files: `libi2pd/Destination.{h,cpp}`, `libi2pd/LeaseSet.{h,cpp}`, `libi2pd/Config.cpp`, `tests/test-leaseset2-enc-types.cpp`.
- [ ] **[CRY-235]** ML-KEM provider probing, fallible hybrid init and PQ downgrade resistance.
  - Priority P1; Status Implemented / static; Pending CI compilation; Pending CI tests. The LibreSSL probe performs generate/marshal/parse/encaps/decaps/compare and cleanses; `InitNoiseIKStateMLKEM`/`InitNoiseXKStateMLKEM`/`InitNoiseXKStateMLKEM1` return bool and callers fail closed; an SSU2 Retry (authenticated only by the public intro key) may change version only when the signed RouterInfo advertises that exact maximum, otherwise the attempt fails; the authenticated `IncompatibleVersion` fallback may only choose an advertised lower version; NTCP2 no longer randomly downgrades 1 in 4 PQ peers.
  - Files: `libi2pd/PostQuantum.{h,cpp}`, `NTCP2.{h,cpp}`, `SSU2Session.{h,cpp}`, tests `test-crypto-x25519-noise-elligator-mlkem-audit`, `test-ssu2-preauth`, `test-ntcp2-handshake-bounds`.
  - Residual: LibreSSL ML-KEM APIs and no-PQ/restricted-provider lanes are unverified; `MLKEMKeys::Encaps/Decaps` still take no buffer length.
- [ ] **[CRY-236]** Fallible signing and LeaseSet2 self-verification.
  - Priority P2; Status Missing. `Signer::Sign` remains `void`, so provider failures can publish an all-zero signature; the legacy Ed25519 compatibility signer re-keys without re-checking and `LocalLeaseSet2`/`LocalEncryptedLeaseSet2` do not verify their own output. Plan: return `i2p::expected`/bool end-to-end, fail the compat signer when the re-derived public key still mismatches, and self-verify before publication.
- [ ] **[CRY-237]** Secret-container capacity cleansing.
  - Priority P2; Status Missing. `PrivateKeys`, `B33OfflineKeys`, `LocalEncryptionKey` and `m_OfflineSignature` cleanse only `size()`; a `SecureBytes` type must cleanse `capacity()` and before resize/overwrite.
- [ ] **[NET-173]** SSU2/NTCP2 PQ downgrade policy.
  - Priority P1; Status Implemented / static; Pending CI compilation; Pending CI tests. SSU2 Retry version changes are bound to the signed RouterInfo advertisement (no silent downgrade from a forged Retry); the authenticated version-rejection fallback only selects an advertised lower version; NTCP2's 1-in-4 random classic choice is removed. Prepared `test-ssu2-preauth`/`test-ntcp2-handshake-bounds` cases.
- [ ] **[NET-174]** Fix garlic-clove netDb origin (DatabaseLookup and DatabaseStore).
  - Priority P0; Status Implemented / static; Pending CI compilation; Pending CI tests. A new `fromTunnel` marker distinguishes a clove of a garlic received directly from a peer from one that crossed an I2NP tunnel; `DatabaseLookup` is in the clove allowlist and is answered only for direct/direct-garlic origin (tunnel-origin still dropped). Token-bearing `DatabaseStore` keeps its token for Direct/DirectGarlic (store + DeliveryStatus) and is stored **without** token/ack/relay for TunnelGarlic. Prepared `test-netdb-clove-origin` and extended `test-tunnel-dsm-policy`.
  - Residual: router garlic delivered through an exploratory tunnel is still dropped in `TunnelPool::ProcessGarlicMessage` (no local destination); the marker is ready for that dispatch fix.
- [ ] **[NET-175]** Transport RouterInfo flood admission and netDb admission bounds.
  - Priority P1; Status Implemented / static; Pending CI compilation; Pending CI tests. `FloodRouterInfoConditional` requires floodfill mode, a non-blocked/banned key and the per-key FloodThrottler; NTCP2/SSU2 request-flood blocks use it. The netDb queue uses `PutIfBelow` (1024) with a dropped counter, and a deterministic 1000/4000 new-RouterInfo cap never drops updates and bypasses while reseeding.
  - Residual: Java's probabilistic `LIMIT_ROUTERS`/pdrop, go-away RouterInfo and `MAX_ROUTERS_INIT` load trimming remain (`DB-127`/`NET-176`); deterministic caps are a fork policy.
- [ ] **[NET-176]** RouterInfo freshness predicate completion.
  - Priority P2; Status Implemented / static; Pending CI compilation; Pending CI tests. `IsRouterInfoStoreValid` adds Java's 54-minute `U`-cap and SSU-introducer rules and the 75-minute SSU-only/no-NTCP rule, gated on one hour of uptime, while keeping the zero-address grace, 2-minute future and 27-hour bounds. Prepared freshness-matrix vectors.
  - Residual: per-use expiry on selection, Java's dynamic `_ds>5000` clamp, probabilistic store admission and go-away RI remain (`DB-127`/`PARITY-020`).
- [ ] **[DB-126]** Durable, identity-bound RouterInfo persistence and shutdown flush.
  - Priority P1; Status Implemented / static; Pending CI compilation; Pending CI tests. Saves use `fs::AtomicWrite` (unique no-follow temp, fsync file+dir, rename); load accepts only `routerInfo-<base64>.dat` whose stem matches the parsed identity, deletes staging leftovers and mismatched files, and `NetDb::Stop` performs one bounded final persist after producers are quiesced.
  - Residual: netDb storage remains in-process (no privsep worker); AtomicWrite makes cache files 0600; a crash-left staging sweep still requires exclusive data-directory ownership.
- [ ] **[CONC-013]** Remove profile disk I/O from netDb selection locks and replace RouterInfos by generation.
  - Priority P1; Status Implemented / static; Pending CI compilation; Pending CI tests. `GetRouterProfileIfExists`/`GetProfileIfExists` never create or load and are used by every selection filter and store validation; `AddRouterInfo` installs the already-validated candidate instead of mutating the stored object and re-verifying under the map lock. Prepared `test-netdb-router-admission` (selection-no-IO, caps) and generation assertions.
  - Residual: local RouterInfo address-slot copy-on-write (SEC-025), `m_AcceptUpdates` acquire/release, throttle-map LRU eviction and asynchronous reseed remain.
- [ ] **[I2P-014]** Enforce MetaLeaseSet canonical form.
  - Priority P1; Status Implemented / static; Pending CI compilation; Pending CI tests. Java re-serialises a MetaLS2 with header `expires` = latest entry and revocation count 0 before verifying, so any other form fails Java signature validation. Local now rejects a header that differs from the latest entry and any revocation; the previous one-way clamp is obsolete.
  - Files: `libi2pd/LeaseSet.cpp`, `tests/test-leaseset2-java213-regression.cpp`.
  - Residual: a future spec that defines revocation processing must revisit the hard rejection; no Java-generated Meta vector is available.
- [ ] **[I2P-015]** Reject older encrypted-LeaseSet replies at the client cache.
  - Priority P2; Status Implemented / static; Pending CI compilation; Pending CI tests. A type-5 reply is installed only if its signed outer published time is strictly newer than the cached entry; equal/older keeps the cached snapshot so an authentic replay cannot roll back the cache. Prepared `test-leaseset-encrypted-freshness`.
  - Residual: the test pins the contract helpers, not the private `HandleDatabaseStoreMessage` branch; a package-private seam would close that gap.
- [ ] **[I2P-016]** Client-side MetaLeaseSet resolution.
  - Priority P2; Status Missing. Floodfills store/serve type 7 but the client switch resolves only types 1/3/5, so Meta-published destinations are unreachable from this client. Plan: follow the selected MetaLease entry (cost/revocation/child type), bind the reply to the queried sub-destination and keep the existing replay/expiry rules. The current drop is documented in `Destination.cpp`; do not turn it into a silent accept.
- [ ] **[CPP-013]** Continue C++23 ownership and RAII in the remaining native structures.
  - Priority P1; Status Implemented / static; pending CI compilation/tests. `TunnelConfig` owns an ordered `vector<unique_ptr<TunnelHopConfig>>`, `GarlicDestination` no longer shares a mutable payload scratch buffer, `Piece::m_Data` is `unique_ptr<uint8_t[]>`, `UDPTunnel` uses `optional<thread>` plus `atomic<bool>`, and `Families` owns `EVP_PKEY` through RAII with checked signing/verification. Remaining residuals are Streaming packet-pool ownership, the ElGamal precomputation table and low-level OpenSSL wrappers in `Signature`/`Gost`.
  - Files: `libi2pd/TunnelConfig.*`, `Garlic.*`, `ECIESX25519AEADRatchetSession.cpp`, `libi2pd_client/Torrents.*`, `UDPTunnel.*`, `libi2pd/Family.*`, `tests/test-tunnelconfig-ownership.cpp`.
  - Validation: `test-tunnelconfig-ownership` prepared; ASan/UBSan/TSan, torrent and tunnel runtime runs pending.
- [ ] **[CPP-014]** Harden address-book persistence and Android recheck validation exposed by the native administration path.
  - Priority P1; Status Implemented / static; pending CI compilation/tests. The filesystem index is written through a same-directory temporary and renamed only after successful index/hosts writes, `Stop()` no longer materialises an empty authoritative index when none existed, oversized/unreadable identity files and non-EOF stream errors are rejected, failed adds remove an unreferenced identity, and the Android recheck/tracker candidates reject the engine's `0` “not found” id consistently.
  - Files: `libi2pd_client/AddressBook.cpp`, `tests/test-addressbook-admin.cpp`, Android `TorrentControl.java`/`TorrentControlTest.java`.
  - Validation: save-count/rollback, tellg/stream and Android command tests prepared; no filesystem fault injection, Gradle unit run or device test executed.
- [ ] **[CPP-015]** Apply the permanent modern-language utilization policy across C++23/OOP and Java 25.
  - Priority P1; Status Implemented / static; pending CI compilation/tests. `test-modern` and `test-transport-connector` are registered in CTest; `language-modernization-static` is registered as a compile-free `ci;docs;static` test and in the `skills-static` workflow job. AGENTS.md and the C++23/Android skills now require deliberate modern facilities and OOP/generic abstractions even without a measurable performance gain. The X25519 pre-generation worker uses `std::jthread`/`std::stop_token`/`condition_variable_any`; the TUI IPC server uses `std::jthread`/stop-token instead of a separate atomic flag; outbound NTCP2/SSU2/Yggdrasil selection now uses an `ITransportConnector` Strategy interface; the signing-key wire-size switch became a `constexpr` array/`optional`/ranges registry; the logger level parser uses a static range table. Android adds sealed `RouterCommand` records with an exhaustive pattern switch and a typed `LogLevel` enum. The 2026-10-25 continuation removes the ad-hoc `Defer`/`SaveStateHelper` vocabulary in favour of `modern::ScopeExit`, adds monadic `value_or`/`and_then` to `i2p::expected`, replaces `std::function`/`std::bind` memory-pool deleters with typed nested deleters (removing the NetDb member-pointer workaround), and turns the I2NP clove allowlist into a `constexpr` table. Feature availability and deliberate limits are documented in `docs/cpp23-migration.md` and `docs/android-java25.md`.
  - Files: `AGENTS.md`, `.agents/skills/openi2pd-cpp23/SKILL.md`, `.agents/skills/openi2pd-android/SKILL.md`, `libi2pd/Transports.*`, `libi2pd/TransportConnector.h`, `libi2pd/Log.cpp`, `daemon/TuiServer.cpp`, `libi2pd/Identity.cpp`, `libi2pd/Modern.h`, `libi2pd/Expected.h`, `libi2pd/util.h`, `libi2pd/NetDb.hpp`, `libi2pd/I2NPProtocol.cpp`, `libi2pd_client/AddressBook.cpp`, `libi2pd/ECIESX25519AEADRatchetSession.cpp`, Android `RouterCommand.java`/`RouterService.java`/`LogLevel.java`/`LogFilter.java`/`LogsFragment.java`, prepared `test-modern`/`test-transport-connector`/`RouterCommandTest`/`LogFilterTest`, `tests/CMakeLists.txt`, `tests/check-language-modernization.cmake`, `.github/workflows/build.yml`, `docs/cpp23-migration.md`, `docs/android-java25.md`.
  - Validation: static source/interface inspection only. No compiler, CTest, Gradle, JNI, OpenBSD, Windows, sanitizer, fuzz, device or live-peer run is attributed to this change.
- [ ] **[ANDROID-015]** Format Android Java sources with the clang-format Java profile and re-run Java compile/Lint/unit tests.
  - Priority P1; Status Implemented / static formatting; Java compile/Lint/tests pending.
- [ ] **[ANDROID-016]** Use Java 25 typed models and exhaustive dispatch in the Android service.
  - Priority P1; Status Implemented / static; pending Gradle/Android compilation and tests. `RouterCommandTest`/`LogFilterTest` are prepared pure-JVM tests but are not wired to a workflow step; no device or D8/R8 evidence exists. `RouterCommand` is a sealed interface with records and an exhaustive pattern switch in `RouterService`; `LogLevel` is a typed enum with switch expressions and `Optional` classification. Public action strings, native call ordering, foreground/startId behavior and the pre-existing typed filter thresholds are preserved.
  - Files: `android/app/src/main/java/org/openi2pd/router/RouterCommand.java`, `RouterService.java`, `LogLevel.java`, `LogFilter.java`, `LogsFragment.java`, `android/app/src/test/java/org/openi2pd/router/RouterCommandTest.java`, `LogFilterTest.java`.
  - Validation: `:i2pd:testDebugUnitTest`, Lint, D8/R8, APK and device tests remain pending manual CI; no Gradle run exists.
- [ ] **[I18N-001]** Add native en/es/ru/zh-Hans UI catalogs and language selection.
  - Priority P1; Status Implemented / static; pending CI compilation/tests. `I18N.cpp` is process-local and independent of the router client; `--lang`, `[tui] language`/`lang` and `LC_ALL`/`LC_MESSAGES`/`LANG` select English, Spanish, Russian or Simplified Chinese, with English fallback for unknown/Traditional tags. TUI `PutLine`/header/status paths and HTTP proxy messages use the catalog; `LC_NUMERIC` is pinned to C.
  - Files: `i18n/I18N.*`, `I18N_langs.h`, `English.cpp`, `Spanish.cpp`, `Russian.cpp`, `ChineseSimplified.cpp`, `tui/main.cpp`, `tui/TuiClient.*`, `daemon/Daemon.cpp`, `libi2pd/Config.cpp`, `build/CMakeLists.txt`, `contrib/i2pd.conf`, `man/i2pd-tui.1`, `docs/user-guide/configuration.md`.
  - Validation: `test-i18n-locale` and `i18n-static` prepared; no compiler, CTest, terminal or OpenBSD/Windows runtime run exists.
- [ ] **[I18N-002]** Ship Android en/es/ru/zh-CN resources and per-app language selection.
  - Priority P1; Status Implemented / static; pending Gradle/Lint/device tests. Complete translated catalogs are present with positional-placeholder parity; `locales_config`/`localeFilters` list the four languages; Settings uses `AppCompatDelegate.setApplicationLocales` without changing the device locale; `check-android-locales.pl` replaces the English-only guard.
  - Files: `android/app/src/main/res/values/strings.xml`, `values-es/strings.xml`, `values-ru/strings.xml`, `values-zh-rCN/strings.xml`, `res/xml/locales_config.xml`, `build.gradle`, `AndroidManifest.xml`, `SettingsFragment.java`, `UiFormat.java`, `LocaleResourceTest.java`, `android/tools/check-android-locales.pl`.
  - Validation: Android resource parity guard passes statically; AAPT2/Lint/unit/device and per-app locale recreation remain pending manual CI.
- [ ] **[I18N-003]** Complete TUI/tool catalog coverage and width/layout validation.
  - Priority P1; Status Partially implemented / static; native core labels, status, prompts and common torrent/error text are translated, but the full 60-line help table, every tool help/error and all composed detail strings still need catalog entries and translated layout review. The TUI `Put` column budget and `DisplayWidth` tab advance are fixed; ncursesw/Windows width, Russian/Chinese truncation and every tool locale remain pending manual runtime tests.
  - Files: `tui/main.cpp`, `tui/TerminalUnix.cpp`, `i18n/*`, `tools/*.cpp`, `tests/test-i18n-locale.cpp`, `tests/check-i18n.cmake`.
  - Validation: static source inspection only; no terminal, tool, OpenBSD/Windows or manual CI run exists.

## C. Security and concurrency

- [ ] **[SEC-026]** Harden BOB and I2PControl secrets, asynchronous lifetime and bounded control-plane admission.
  - Priority P0/P1; Status Implemented / static; BOB secret operands are redacted and tunnel callbacks use weak ownership plus stopped-state gates. I2PControl uses bounded TLS admission, phase deadlines, asynchronous bounded HTTP reads, token/authentication quotas, non-loopback default-password refusal and atomic private PEM replacement.
  - Files: `libi2pd_client/BOB.*`, `daemon/I2PControl.*`, `libi2pd/FS.*`, `build/CMakeLists.txt`, `tests/check-control-security.cmake`, security docs and owning skills.
  - Validation: `control-security-static` prepared; manual CI compilation, security/memory labels, TLS slow-client/fragmentation, BOB STOP/CLEAR ASan/UBSan/TSan and POSIX/Windows file-authority tests pending.

- [ ] **[SEC-027]** Close the 2026-10-25 cross-thread lifetime, handle and handshake findings.
  - Priority P0/P1; Status Implemented / static; pending CI compilation/tests. A `Stream` now holds a `shared_ptr<StreamingDestination>` and `StreamingDestination::Stop()` marshals to the owner io_context (bounded wait) instead of cancelling timers from the SAM thread; `ClientDestination` guards the default/per-port streaming destinations with `m_StreamingDestinationsMutex` and only invokes them after dropping the lock. `RouterInfo::SSUExt` snapshots introducers under a mutex for SSU2 session selection/HolePunch verification while `UpdateIntroducers`/local add/remove/update and `RouterContext` clears take the same lock; out-of-sequence reassembly is bounded to 512 fragments and one `I2NP_MAX_MESSAGE_SIZE` per incomplete message (`test-ssu2-session-fragment-bounds` exercises the caps, counter release and a TSan-oriented `SSUExt` snapshot reader/writer pair). `TorrentFile` serializes `Save`/`Load`/`Open`/`Close`/`Complete`/authority replacement with `m_Mutex`, and its inactivity timestamp is atomic. SOCKS defers pipelined input until the RFC 1929 reply write completes, both proxies bound handshake time, and `ServiceAcceptor` caps concurrent handlers at 1024. `I2PControlService::Stop()` detaches instead of self-joining; the Android SAF `SafEnv` clears a pending `PushLocalFrame` exception.
  - Files: `libi2pd/Streaming.*`, `libi2pd/Destination.*`, `libi2pd/RouterInfo.h`, `libi2pd/RouterInfo.cpp`, `libi2pd/RouterContext.cpp`, `libi2pd/SSU2.cpp`, `libi2pd/SSU2Session.cpp`, `libi2pd_client/Torrents.{h,cpp}`, `libi2pd_client/SOCKS.cpp`, `libi2pd_client/HTTPProxy.{h,cpp}`, `libi2pd_client/I2PService.h`, `daemon/I2PControl.cpp`, `daemon/I2PControlHandlers.cpp`, `android/app/src/main/cpp/i2pd_android.cpp`, `tests/check-control-security.cmake`.
  - Residual: `TorrentsTunnel::InitTorrentFiles` still runs filesystem discovery and object mutation on the disk worker (now serialized per file but not yet split into a disk phase plus service-thread commit); SSU2 pool/address publication, RouterInfo address-slot copy-on-write, ratchet crypto-context serialization and datagram receiver lifetime remain SEC-025.
  - Validation: `control-security-static` pins the new contracts and the existing ASan+UBSan/TSan jobs run it; no compiler, sanitizer, Gradle/JNI, device or live-peer run is attributed to this change.

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
  - Priority P1; Status Partially implemented / static; all fifteen libFuzzer targets are registered in CMake with checked-in corpora, per-target bounds and the manual `Fuzzing` workflow. NTCP2 block/SAM/I2CP/I2NP-database harnesses exist; a packet-level SSU2 harness, dedicated SSU2/NTCP2 socket seeds and dynamic fuzz results remain pending, so this row stays open.
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
  - Priority P1; Status Implemented / static, pending CI compilation/tests.
    The fixed daemon-owned queue now executes reseed, reload, shutdown/cancel,
    bandwidth, share, transit, online/offline transport state, log reopen, log
    level and bounded address-book mutations.
    A command whose IPC wait expires is still executed exactly once; only its
    result is discarded, and the client receives typed error codes.
- [ ] **[TUI-006]** Test resize, reconnect, signal handling and long-running log streaming.
  - Priority P1; Status Windows/console/named-pipe tests prepared. Added prompt
    suspension below the minimum terminal size and log-cursor reset after a
    daemon restart; no runtime resize/reconnect/signal/log-stream execution.
- [ ] **[TUI-008]** Add a bounded, validated configuration view/editor to the TUI without exposing keys or raw files.
  - Priority P2; Status Implemented / static (read-only view); `I2PDTUI/1 CONFIG` returns a typed allowlist snapshot and no writer exists. Editing still requires a main-loop command queue; runtime/CI pending.
- [ ] **[TUI-007]** Re-run TUI tests after Android/tools/docs integration.
  - Priority P1; Status Test suites extended for Windows/console/pipe contracts; no build or CTest execution.
- [ ] **[TUI-009]** Extract the shared terminal interface and implement the Win32 console/VT backend without ncurses/PDCurses.
  - Priority P1; Status Implemented / static; `Terminal.h`, `TerminalUnix.cpp`, `TerminalWin32.cpp`; Windows runtime validation pending.
- [ ] **[TUI-010]** Implement secure Windows Named Pipes for TUI IPC with DACL, server identity checks, bounded I/O and reconnection.
  - Priority P1; Status Implemented / static; `TuiServerWin32.cpp`, `TuiClientWin32.cpp`, `TuiWin32Ipc.*`; cross-user and service-account validation pending.
- [ ] **[TUI-011]** Integrate the Windows TUI/IPC into CMake, tests, manual Windows CI and documentation.
  - Priority P1; Status Implemented / static; native MSVC/Clang builds, CTest and DACL runtime checks pending.
- [x] **[TUI-012]** Expand the main TUI navigation, Status statistics and NetDb/Destinations panels.
  - Priority P2; Status Implemented / static; tabs now cover Status, Peers,
    Connections, Tunnels, Torrents, NetDb, Logs, Services, Help, Destinations
    and Config. Added live connection filtering, router/LeaseSet directories
    with encryption type and aged timestamps, a bounded client-service
    inventory, log text search, daemon log-level control, transit toggle and a
    60-sample bandwidth history; official semantic theme roles and both
    terminal backends are preserved. Pending CI/terminal resize and
    narrow-width checks.
- [ ] **[TUI-013]** Complete bounded TUI panel/error-state hardening.
  - Priority P1; Status Implemented / static, pending CI compilation/tests.
    `SERVICES` is a read-only bounded snapshot of client/server tunnels,
    proxies and protocol servers with no keys or paths. The Torrents view
    preserves session totals and the status/netDb panels expose complete
    bounded fields. Remote error codes are preserved by the client, destructive
    prompts cannot be committed while the terminal is too small, and a daemon
    restart resets the log cursor. The continuation adds Status scrolling over
    the fixed fields, a stale marker for every cached panel after disconnect,
    one address-book auxiliary request per slow pass instead of four blocking
    requests in one tick, invalid/C1/bidi sanitisation for all untrusted text,
    `q`-returns-from-detail semantics, protocol/version and log-drop fields,
    the transport online/offline confirmation and log-reopen keys, and a
    Windows console control handler. Terminal/service/queue regressions are
    prepared; no ncurses session or CTest run exists.
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
- [ ] **[FIREWALL-006]** Run `test-routerinfo-firewall` and tools smoke test.
  - Priority P0; Status Historical local 17/17 CTest only; current tree is pending manual CI compilation/tests. Do not treat the historical result as validation of the current generators.
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

- [ ] **[AND-020]** Harden Android JNI string, diagnostic and native-call boundaries.
  - Priority P0; Status Implemented / static, CI/device pending; inbound Java strings are decoded from UTF-16 to ordinary UTF-8, rejecting embedded NUL and unpaired surrogates; start diagnostics are bounded/sanitized. The SAF bridge install and library load no longer happen on the service main thread.
- [ ] **[AND-021]** Complete SAF grant/export/teardown lifecycle.
  - Priority P0; Status Implemented / static, device pending; tree selection validates tree/read-write grants, reference-counts shared URIs before releasing replaced/detached grants, and detaches a plain torrent removal. Export now allows progressive growth to the declared length and has an explicit finish/abort operation that requires exact final size and truncates failed/cancelled targets. Read/write rechecks the persisted grant, regular-file size and document ID, so same-length replacement and truncation fail closed. Close/release no longer retire the broker on a client timeout, broker close is queued through a reaper, cancellation is operation-scoped, and created-document names are verified. Session-scoped AIDL authorization and moving import/create document reads into the broker remain pending.
- [ ] **[AND-022]** Separate router/document/torrent/presentation Android processes.
  - Priority P0; Status Pending; `android:process` gives crash/lifetime containment but not a new UID; `isolatedProcess` cannot retain persisted SAF grants; INTERNET is per-UID. A typed `IRouterControl` AIDL plus a companion-UID design is required before claiming privilege separation. Activity destruction already does not stop transfers; preserve `stopWithTask=false` and `should_run`.
- [ ] **[AND-023]** Preserve queued command completions across service destruction.
  - Priority P0; Status implemented by static analysis; `RouterService.onDestroy` now removes only the poll and graceful-stop callbacks instead of `removeCallbacksAndMessages(null)`, so an in-flight import/SAF command cannot leave `RouterViewModel.importingTorrent` latched. Pending JVM/instrumentation regression and device lifecycle run.
- [ ] **[AND-024]** Replace the blocking restart busy-wait with callback sequencing.
  - Priority P1; Status Pending; `DaemonWrapper.restartDaemon` sleeps up to 10 s on the shared control worker and can turn a slow restart into `startFailed`. A callback/state-machine sequencer and lifecycle test remain open.
- [ ] **[AND-025]** Move SAF grant persistence off the UI thread.
  - Priority P1; Status Pending; `TorrentSafStorage.select/detach/status` still call `SharedPreferences.commit()` and enumerate persisted URI permissions from main-thread fragment callbacks. Use a bounded background operation with a generation-checked callback.
- [ ] **[AND-026]** Gate and bound the SAF broker process.
  - Priority P1; Status implemented by static analysis; broker binding is lazy on first `root()`/`configured()` use, and retries stop after six attempts with a terminal status instead of spawning/killing `:torrent_storage` forever. Pending no-grant process assertion and device policy-failure test.
- [ ] **[AND-027]** Establish one process-wide native daemon owner.
  - Priority P1; Status Pending; a recreated `RouterService` creates a new `DaemonWrapper`/executor while the old native stop may still run. A static owner or process-wide lock is required; no claim of serialization is made.
- [ ] **[AND-028]** Keep instrumentation tests on the API 29 floor.
  - Priority P0; Status implemented by static analysis; `TorrentSafStorageTest` no longer uses `String.repeat` (API 33) and uses a local builder. API 29 instrumentation execution remains pending.
- [ ] **[AND-029]** Prove Java 25 class-file → D8/R8 → DEX and native packaging in manual CI.
  - Priority P0; Status in implementation; the Android job now asserts class-file major 69, builds unsigned release/lintRelease, checks the APK contains `lib/arm64-v8a/libi2pd.so`, links JNI with project warnings/hardening and hidden visibility, and documents the ABI/API/device matrix. AGP's D8/R8 version is still unpinned; class-file 69 DEX generation, release R8, other ABIs, AAB and device tests remain unvalidated.
- [ ] **[AND-030]** Tighten Android resource/Lint and edge-to-edge lifecycle.
  - Priority P1; Status pending; Lint now requests text/XML reports and the workflow reads the reports path, but hardcoded Java strings, unused resources, release Lint, baseline/profileable debug builds and API 35+ inset handling remain open.
- [ ] **[AND-031]** Dashboard diagnostics and reseed surface.
  - Priority P1; Status implemented by static analysis; the dashboard shows the native router status/error detail and a bounded Reseed action gated by connected/not-reseeding state. Pending device UI regression.
- [ ] **[AND-032]** Configuration and notification remediation.
  - Priority P1; Status implemented by static analysis; listening port is editable with the existing port validator/restart apply mode, and Settings links to the app notification settings with an enabled/disabled summary. Pending Settings UI tests.
- [ ] **[AND-033]** Statistics live/peak values.
  - Priority P1; Status implemented by static analysis; download/upload cards show the instantaneous rate alongside the 15 s average and the chart reports its retained-window peak. Chart history still needs ViewModel ownership across detail navigation.
- [ ] **[AND-034]** Log filtering and accessibility.
  - Priority P1; Status implemented by static analysis; filter chips now mean minimum severity (Warn includes errors), logic is a pure JVM `LogFilter`, log rows are one screen-reader stop, filters wrap, and the summary is a polite live region. Device TalkBack run pending.
- [ ] **[AND-035]** I2PSnark bulk controls and session identity.
  - Priority P1; Status implemented by static analysis; validated Start all/Stop all/Reannounce all commands and the session name in the status line. Device layout/TalkBack execution, queue/bandwidth controls, SAF source seeding and visible per-tunnel inventory remain open.
- [ ] **[AND-036]** Device UI regression lane.
  - Priority P0; Status prepared only; `UiContractTest` checks dashboard/status/reseed, statistics labels, log wrapping/live region and screen-reader row grouping, but the manual Android job still assembles rather than executes instrumentation and no device/API matrix exists.
- [x] **[AND-018]** Replace the graceful-stop `java.util.Timer` with a service-owned deadline and an explicit `stopping` state.
  - Priority P1; Status Implemented / static; `RouterService` schedules the deadline on its main handler, enqueues the native stop on its worker, and `DaemonWrapper` publishes `State.stopping` before native teardown. `android/tools/java25-probe/check-config.sh` and `UiFormatTest` guard the contract; Gradle/device execution pending.
- [x] **[AND-017]** Establish English-only Android resources and remove localization plumbing.
  - Priority P1; Status Dropped (superseded by I18N-001/002); the English-only decision was superseded by `I18N-001`/`I18N-002`. The old English-only catalogs, guard and device test are replaced by en/es/ru/zh-CN resources and the locale policy guard; do not reactivate the old policy.

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
  - Priority P0; Status Baseline recorded / pending rerun; see `CI-010` for the latest published-revision dispatch evidence. Historical `main@800c8627` runs validated only that baseline and do not cover the current uncommitted fixes.
- [ ] **[GOV-005]** Keep `TODO.md`, `ROADMAP.md`, `PROGRESS.md` current after each work cycle.
  - Priority P0; Status In progress.
- [x] **[GOV-006]** Run zizmor and yamllint on `.github/workflows`; pin actions to commit SHAs, disable checkout credential persistence and add `.yamllint.yml`.
  - Priority P0; Status Implemented; zizmor reports no findings (5 suppressed); yamllint clean. CI triggers remain manual-only.
- [ ] **[GOV-007]** Establish reusable Agent Skills for the project's subsystem workflows.
  - Priority P1; Status Implemented / static; `.agents/skills/` contains the README index plus 17 domain skills (`cpp23`, `memory-security`, `i2p-interop`, `cryptography`, `privsep`, `openbsd`, `linux-windows`, `torrent`, `tui`, `android`, `i18n`, `build`, `packaging`, `tooling`, `upstream`, `ci`, `documentation`). Each has single-line frontmatter, precise activation/exclusion descriptions, `Known issues and decisions`, `Do not` policy pointers, coordinator-owned tracking language and at least one licensed `references/` checklist. `openi2pd-tooling` now includes `references/perl-inventory.md`; no separate Perl/IPC skill was added because it would duplicate tooling/privsep/platform ownership.
  - Files: `.agents/skills/**`, `AGENTS.md`, `TODO.md`, `ROADMAP.md`, `PROGRESS.md`, `tests/check-agent-skills.cmake`, `tests/CMakeLists.txt`, `docs/testing.md`.
  - Acceptance: exact README/expected-skill inventory; names match directories; descriptions are unique, bounded and activation-specific; required sections include `Known issues and decisions`, `Do not` and `References`; references are linked and licensed; skills link to global policy instead of duplicating it; no skill authorizes local builds/tests or coordinator-only tracking edits.
  - Validation: `agent-skills-static` passed locally as a compile-free source check; docs-link/branding checks, CTest execution and manual CI remain pending. No skill was executed and no build/test/workflow was run.
- [ ] **[GOV-008]** Keep the permanent agent knowledge base, its technical docs and compile-free checks in lockstep.
  - Priority P1; Status Implemented / static; pending CI. `AGENTS.md` now has a permanent `Agent Knowledge Maintenance` rule requiring every significant change to review AGENTS, the owning skills, TODO/ROADMAP/PROGRESS and technical docs. Every skill has a `Known issues and decisions` section enforced by `agent-skills-static`; `language-modernization-static` pins the policy phrases, language levels, availability matrix and named modernization seams.
  - Files: `AGENTS.md`, `.agents/skills/**`, `tests/check-agent-skills.cmake`, `tests/check-language-modernization.cmake`, `docs/cpp23-migration.md`, `docs/android-java25.md`, `docs/testing.md`, `docs/ci.md`.
  - Acceptance: no task is marked complete without a knowledge-impact review; required skill sections and policy/seam phrases remain coherent; historical local-run evidence is labelled; shared edits stay coordinator-owned.
  - Validation: static source/link inspection and the two compile-free CMake checks only; no compiler, CTest, Gradle, validator or workflow run exists for this row.
- [x] **[GOV-009]** Re-synchronize `AGENTS.md` and every Skill with the 2026-10-26 tree and make non-duplication permanent.
  - Priority P1; Status Implemented / static; Pending CI tests. Six read-only audits covered skills inventory, C++23/crypto/interop, IPC/platform, torrent/Android/i18n and build/CI/tooling/packaging. Corrections cover protocol v5/`Pong` receiver semantics, the log-fd sink, `WorkerHello`, the NTCP2 two-column padding table, SSU2 fragment caps, the 15-fuzzer/corpus inventory, sanitizer matrix, CTest registration, service stop budgets, Debian/OpenBSD payload rules, Android bridge/SAF/API facts, i18n plural arity, torrent/`TORRENT-ADD-FILE` boundary, and upstream pin scope. `AGENTS.md` now states that skills must link the single policy owner instead of duplicating it, and `check-agent-skills.cmake` pins that rule; a stale count in `TODO.md`/`ROADMAP.md` was corrected.
  - Files: `AGENTS.md`, `.agents/skills/**`, `TODO.md`, `ROADMAP.md`, `PROGRESS.md`, `tests/check-agent-skills.cmake`, `docs/testing.md`.
  - Acceptance: no normative rule is restated in a skill; every changed skill fact matches code/tests; counts/pins/status agree across AGENTS, docs and tracking; only compile-free checks run.
  - Validation: `cmake -P tests/check-agent-skills.cmake`, `cmake -P tests/check-language-modernization.cmake`, `perl i18n/check-i18n-catalogs.pl` and `git diff --check` only; no compiler, CTest, Gradle, device, package or workflow dispatch.

## K. Rebranding and identity

- [x] **[REBRAND-000]** Audit brand/identity references and create `docs/REBRANDING.md`.
  - Status Implemented; static classification complete.
- [x] **[REBRAND-001]** Rename Android applicationId/namespace to `org.openi2pd.router` and update JNI/package references.
  - Status Implemented / compile validation pending (no SDK).
- [x] **[REBRAND-002]** Replace legacy visual identity with neutral placeholder icons; remove old screenshots/logo references.
  - Status Implemented; PNG/ICO/vector generated; dynamic rendering not checked.
- [x] **[REBRAND-003]** Remove personal upstream install/donation/team links from Android/docs.
  - Status Implemented.
- [x] **[REBRAND-004]** Update systemd description and evaluate the `open-i2pd.service` alias.
  - Status Implemented; the description uses `openi2pd router`. The temporary alias was removed because no package installed or migrated it; the technical `i2pd.service` filename is retained.
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
  - Status Implemented / static hardening; protocol v2 adds the typed `Error` message and shared endpoint allowlist while still accepting v1 frames. Bindings and framing retest pending CI.
- [x] **[PRIVSEP-003]** Implement spawned log worker with structured records and OpenBSD pledge/unveil policy.
  - Status Implemented; live Linux test confirmed core had no log fd and worker had the only log write fd; write/flush/duplicate/NUL hardening added.
- [x] **[PRIVSEP-004]** Add `--privsep-worker` exec entry point and parent spawn/configure/shutdown.
  - Status Implemented / static hardening; `test-privsep-log` prepared; bounded in-process restart monitor added.
- [ ] **[PRIVSEP-005]** Add dedicated supervisor process with pidfd/waitpid, restart backoff and channel policy.
  - Priority P0; Status Implemented / static, CI pending; a separate supervisor process owns the log worker, forwards `ConfigureLog`/`LogRecord`, and restarts it with 250 ms exponential backoff capped at 30 s, eight consecutive attempts and a 60 s stable-period reset. Exhaustion is fatal and the core's logger queue is bounded. On Linux the supervisor uses capability-only bootstrap (`ApplyLinuxSupervisorSandbox`) because inherited Landlock/seccomp/netns domains would break its exec'ed children; each child applies its role policy. Outside the OpenBSD runtime sandbox the core can replace a dead supervisor; under that sandbox it cannot. The OpenBSD torrent-storage broker is a separate pre-pledge process with an inherited root fd. Router storage and network/crypto workers are not yet under a single supervisor; fork/restart/sandbox dynamic tests pending.
- [ ] **[PRIVSEP-006]** Implement storage worker with typed allowlisted operations; migrate netDb/profiles/keys persistence.
  - Priority P0; Status In progress / static, CI pending; `router-storage` (four fixed router files) and opt-in torrent payload/state workers exist with typed bounded requests and no network capability. netDb/profile/address-book/destination-key writes and the core's persistent-write capability remain; core still combines network and writes.
- [ ] **[PRIVSEP-007]** Implement network worker owning NTCP2/SSU2 sockets; core must lose direct network access.
  - Priority P0; Status Pending; state-machine ownership and nonce safety must be designed first.
- [ ] **[PRIVSEP-008]** Implement crypto worker/key handles for long-term operations, or document the boundary if latency makes it non-viable.
  - Priority P1; Status Pending.
- [ ] **[PRIVSEP-009]** Add per-process OpenBSD pledge/unveil policies for storage/network/core/crypto/supervisor.
  - Priority P0; Status Implemented / static for supervisor/log and role policy builder; storage/network/crypto/admin backends and core migration remain pending. See OPENBSD-SBX-4/5.
- [ ] **[PRIVSEP-010]** Add Linux seccomp/Landlock/namespaces/pidfd/cgroup limits and negative capability tests.
  - Priority P1; Status Implemented / static, CI pending; the log/storage/network role profiles include capability/bounding-set drop, Landlock, AF_INET-vs-AF_UNIX seccomp, optional net namespace and cgroup v2 helpers. The log worker applies its full profile; the supervisor applies only `ApplyLinuxSupervisorSandbox` (no-new-privs + capability drop) so exec'ed children can apply broader delegated roles. `test-privsep-linux-sandbox` now covers the supervisor bootstrap and delegated socket capability. Router/torrent storage workers use the storage profile; netDb/profile/key and network backends and runtime validation remain pending.
- [ ] **[PRIVSEP-011]** Implement Windows ncalrpc/Named Pipes/Restricted token backend.
  - Priority P2; Status Implemented / static for worker message-mode pipes, protected DACL, peer SID/session checks, restricted token/Job Object and mitigation creation; AppContainer/LPAC and worker/supervisor daemon integration remain pending. No live Windows credential test.
- [ ] **[PRIVSEP-012]** Define Android process/UID limitations and migration.
  - Priority P2; Status In progress / static; the Android service remains same-process and non-exported, with no raw JNI or storage exposed to UI. The Android JNI CMake graph deliberately does not build `i2pd_privsep`; a separate `android:process`/AIDL router owner requires the native worker split and a new security/lifecycle review; not claimed.
- [ ] **[PRIVSEP-013]** Add IPC fuzzing, queue saturation, malformed message, descriptor-transfer and restart tests.
  - Priority P1; Status Implemented / static; malformed/EOF/restart, descriptor-transfer, missing-credential, ancillary-data and queue-pressure cases are in `test-privsep-linux-ipc`/`test-privsep-log`; `test-log-lifecycle` checks the bounded 8192-record logger queue/drop counter. `RunWorker` now requires the channel at fd 3 so it cannot alias inherited root fd 4. New `test-privsep-protocol`/`test-privsep-linux-ipc` cases cover typed errors, operation allowlists, empty records and adopted-fd rejection; OpenBSD/Windows cases cover buffered imsg frames and oversized pipes. Linux sanitizer execution pending.
- [ ] **[PRIVSEP-014]** Enforce no silent fallback on OpenBSD and remove the monolithic path for enabled privsep.
  - Priority P0; Status Pending.
- [ ] **[PRIVSEP-015]** Consolidate typed/versioned IPC contracts, authorization, backpressure and recovery.
  - Priority P0; Status Implemented / static, CI pending; protocol v2/minimum v1, typed `Error` replies, shared `AllowsOperation` role allowlist, fallible bounded path encoders, configure/readiness Ping/Pong handshake, monotonic storage request IDs, non-fatal broker capacity/path errors, validated adopted channels, zero-length-record handling, imsg buffered-frame draining, and Windows oversized-message/PID-session binding. Residual: full cross-process channel error taxonomy, automatic client reconnect, explicit transactional cancellation IDs and Windows daemon wiring remain pending.
- [ ] **[PRIVSEP-016]** Harden the Linux AF_UNIX/seqpacket, seccomp and descriptor boundary.
  - Priority P0; Status Implemented / static, CI pending; AF_UNIX domain/listening checks and SO_PASSCRED activation on adopted channels; credential and empty-record flood checks; seccomp denials for process_vm_*, kcmp, pidfd_open/send_signal, open/name_to_handle_at, userfaultfd, bpf and perf_event_open; PR_SET_NO_NEW_PRIVS before exec; wider close() fallback ceiling; inherited stdio socket cleanup; Linux dev/inode storage-root binding; Linux rejection of the OpenBSD-only inherited-root roles. Residual: kill/tgkill self-filtering, pid binding/deferred reap, cgroup.kill and full descriptor classification.
- [ ] **[PRIVSEP-017]** Complete Linux pidfd, cgroup and stop-budget recovery.
  - Priority P1; Status Pending; deferred-reap registry for unreapable children, single-reap tri-state wait, total Stop deadline/Retire, forwarded-worker pidfd/kqueue set, opt-in cgroup v2 limits and cgroup.kill. Kernel floors/fallbacks are documented in `docs/security/process-capabilities.md`; no dynamic evidence.

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
| `CI-*` | CI follow-up tasks (section P); `GOV-*` is governance |
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
- [ ] **[CI-010]** Publish the expanded CTest/workflow matrix and dispatch it without attributing old results.
  - Priority P0; Status remote baseline dispatched on `main@284544a5256043755b197f61e42aee56dbf65032`: Linux/Android `39688898520`, Windows `39688906891`, OpenBSD `39688913964`, Fuzzing `39688921868`, Packaging `39688929467`. Interop and service contracts passed; Linux GCC/Clang, Android, TSan/ASan, OpenBSD, Windows MSYS2/MSVC, Fuzzing and Debian packaging hit the published `libi2pd/Transports.cpp:693` parse error. The local worktree contains a different, uncommitted `ConnectToPeer` implementation and must not be credited with those results. Publish a corrected revision before rerunning.

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

## L. Architecture and cross-platform follow-up

- [ ] **[ARCH-001]** Keep the shared-interface ownership map current for `RouterContext`, transport APIs, crypto APIs, `TuiProtocol`, `privsep/Protocol`, JNI and CMake.
  - Priority P0; Status In progress; coordinator owns integration; no unilateral API changes.
- [ ] **[ARCH-002]** Reconcile documented target architecture with actual process/worker implementation; remove claims of completed separation.
  - Priority P0; Status In progress; `docs/security/privilege-separation.md`, `docs/architecture.md`.
- [ ] **[ARCH-003]** Define typed storage-worker operations and ownership before migrating netDb/profile/keys persistence.
  - Priority P0; Status In progress / static; fixed-name router persistence and opt-in torrent payload/state workers use bounded typed operations. netDb/profile/address-book/destination-key operations and core write-capability removal remain pending; depends PRIVSEP-006.
- [ ] **[LINUX-001]** Add seccomp-BPF/Landlock/capability/cgroup policy for workers with negative tests.
  - Priority P0; Status Implemented / static, CI pending; per-role Linux profiles cover AF_INET/AF_INET6 vs AF_UNIX socket filters, Landlock write paths, capability/ambient/bounding-set drop, optional network namespace and cgroup v2 limits. The log worker applies the full profile; the supervisor uses capability-only bootstrap because inherited restrictions would break exec'ed storage/network children. `test-privsep-linux-sandbox` covers storage socketpair/no-capability, log AF_UNIX and supervisor bootstrap delegation. Router/torrent storage use the storage profile; netDb/profile/key and network backends remain pending. No local Linux execution.
- [ ] **[LINUX-002]** Validate peer credentials, pidfd, close_range and SOCK_SEQPACKET behavior on current Linux.
  - Priority P1; Status Partially implemented / static; `SO_PEERCRED`/`SCM_CREDENTIALS`/`SCM_RIGHTS` tests exist and `pidfd_send_signal` is used when available. Cross-UID and current-kernel execution remain pending.
- [ ] **[WIN-001]** Implement ncalrpc/Named Pipes worker transport with DACL, restricted token/AppContainer and Job Objects.
  - Priority P1; Status Implemented / static, CI pending; `WindowsIpc` adds message-mode local pipes, protected user/SYSTEM DACL, peer token SID/session checks, operation allowlist, restricted token, Job Object and mitigation-attribute spawning, and no handle inheritance. AppContainer/LPAC is probed but fails closed when requested. `test-privsep-windows-ipc` covers DACL/round trip/malformed/recovery/token/job. Daemon worker integration and cross-user/service tests pending.
- [ ] **[WIN-002]** Add Windows negative IPC/credential and firewall validation.
  - Priority P1; Status Pending; requires a second user/service account and a live Windows host.
- [ ] **[WIN-003]** Harden Windows pipe authentication/lifecycle and overlapped I/O.
  - Priority P0; Status Implemented / static, CI pending; canceled overlapped operations are drained before release; timeouts/malformed frames close the channel; rejected peers re-arm the listener; `ConnectBound` authenticates server PID/session; `MakeRandomPipeName` uses BCryptGenRandom; job UI/exception limits added. Tests extended; no Windows run.
- [ ] **[WIN-004]** Complete Windows token/AppContainer/LPAC enforcement.
  - Priority P0; Status In progress / static; all parent privileges are deleted and LUA_TOKEN is used for elevated parents. Restricting SID/integrity level/default DACL, functional `CreateAppContainerToken`/LPAC probing, capability SIDs, pipe mandatory label and role-specific filesystem/network ACLs remain pending.
- [ ] **[WIN-005]** Implement Windows worker entry/supervision and daemon wiring.
  - Priority P0; Status Pending; `--privsep-worker` is not dispatched on Windows, `RunWorker` is an unreachable failing stub, and no supervisor/restart owner exists. AppContainer storage/network roles must not be advertised until this and WIN-004 are complete. No Windows runtime evidence.
- [x] **[TOOL-009]** Make the tool CLI and binary I/O portable across POSIX and MSVC.
  - Priority P0; Status Implemented / static; `tools/common/getopt.{h,cpp}` supplies getopt/getopt_long on Windows while Unix keeps the system parser; `i2pbase64` now uses C++ streams with binary stdio on Windows. CLI spellings are unchanged. Pending Windows CI.
- [x] **[TOOLS-007]** Continue tool security/regression audit after `famtool`/`keyinfo`/`autoconf_i2pd`; include address/offline-key tools.
  - Priority P1; Status Implemented / static; stdin/path bounds, private output permissions, secret-buffer wiping, `b33address`/offline-key/autoconf regressions and upstream provenance checks are in place. Dynamic sanitizer/Windows permission runs remain pending.
- [ ] **[TOOL-008]** Add a dedicated `test-vain` target if the pure base32/nonce helpers are factored out; current coverage is CLI/negative paths only.
  - Priority P2; Status Pending; requires coordinator CMake change.
- [ ] **[TOOL-010]** Modernize tool CLI/key/config contracts and remove duplicated crypto/file handling.
  - Priority P1; Status implemented by static analysis; `autoconf_i2pd` no longer emits the removed `[http] lang` section, keygen refuses unavailable/fallback signature generators instead of writing a mislabelled key, offline-key tools reject non-offline fallback results, `common/crypto.hpp` centralizes RAII, `LoadPrivateKeys` enforces exact consumption and wiping, routerinfo uses the bounded reader, and `x25519`/`b33address`/`verifyhost` reject unknown/invalid CLI surfaces. Pending CTest/Windows/OpenBSD execution.
- [ ] **[TOOL-011]** Install and inventory tool attribution fail-closed.
  - Priority P0; Status implemented by static analysis; the `Tools` component installs `LICENSE.i2pd-tools`, `perl-install-layout` checks all declared binaries and the notice, and `tools-inventory` treats a missing upstream manifest or unregistered top-level tool source as fatal. Pending packaging/install CI.
- [ ] **[TOOL-012]** Resolve legacy `-f` IPv6 family ambiguity and add a positive family/firewall fixture.
  - Priority P1; Status Implemented / static; Pending CI compilation; Pending CI tests. IPv4 legacy fragments stay byte-identical; IPv6 rules explicitly select `ip6tables`. The non-installed `routerinfo-fixture` helper generates a signed public RouterInfo in the CI work directory using ephemeral in-memory keys, avoiding a checked-in binary/private key. `test-routerinfo-firewall` and positive `tools-smoke` checks cover `-6 -p`, legacy and explicit family selection. Native firewall syntax validation remains FW-008.
- [x] **[FIREWALL-010]** Harden backend attribution, validation and CLI failure reporting.
  - Priority P1; Status Implemented / static; Pending CI tests. nft output carries a validated rule comment, PF output a validated label, Windows output stable `-Name`/`-Group`, family and `-ErrorAction Stop`. nft table/chain names reject keywords, a leading `_` and hook names; `IsSelectedByFamily` fails closed on an unknown family; PF labels are limited to 64 bytes including NUL (overlong library tags are refused); Windows `-Name` preserves `~` from a base64 prefix. Special-purpose addresses (NAT64, 6to4, Teredo, discard, `3fff::/20`, benchmark/IETF/6to4-relay IPv4) are rejected while documentation fixtures stay allowed. Usage errors go to stderr, `--help` to stdout; the whole run is buffered and discarded on any input error, so no partial rules reach stdout; the 4096 budget counts emitted rules after family selection; a valid run with no selected endpoint reports it on stderr and exits 0. Unit and smoke tests pin exact Windows/nft/PF output, boundary addresses, injection, family filtering, usage streams and mixed valid/malformed input. Manual `nft -c`, `pfctl -nf`, `iptables-restore --test` and PowerShell `-WhatIf` execution remains pending (`FIREWALL-007`).
- [ ] **[FW-008]** Native syntax validation for iptables/nft/PF/PowerShell backends in CI or dedicated hosts.
  - Priority P1; Status Prepared / not executed; see `docs/tools/routerinfo.md` and `FIREWALL-007`.
- [x] **[DOCS-009]** Validate all Markdown links/MkDocs navigation after torrent/Perl/Android docs.
  - Priority P1; Status Static link check passed; MkDocs rendering deferred to CI.
- [ ] **[DOCS-010]** Maintain portable mdoc manual pages for `i2pd`, `i2pd-tui` and `i2pdctl`; validate with mandoc on Linux/OpenBSD.
  - Priority P1; Status Implemented / mandoc-validated; packaging/install runtime checks pending.
- [ ] **[DOCS-011]** Reconcile inherited docs, navigation, license metadata and build facts.
  - Priority P1; Status implemented by static analysis; CMake floor, Android torrent support, I2CP default, Yggdrasil example, AppArmor/IRC paths, FAQ torrent support, Windows/proxy branding, documentation license (BSD-3-Clause) and unsupported iOS/Qt/Haiku navigation were corrected. Added `docs-nav-static` for nav/theme assets, unlisted Markdown and HTML checks; manual MkDocs/link/branding runs remain pending.
- [ ] **[BRAND-012]** Complete light/dark and mirrored-asset brand parity.
  - Priority P1; Status implemented by static analysis; proxy information page has dark/error tokens, Android night resources include the launcher plate, palette labels and binary-validation wording are corrected, and the branding validator now hashes SVG/PNG/color mirrors fail-closed. Rendered icon/small-size/device checks remain pending.
- [ ] **[BRAND-011]** Complete executable/package/data-path migration decisions without breaking technical compatibility.
  - Priority P1; Status Pending; see REBRAND-005..007.
- [ ] **[LEGAL-001]** Audit new torrent/Perl/Android/CI files for required copyright, license and provenance notices.
  - Priority P0; Status In progress; preserve BSD-3-Clause and upstream notices.
- [x] **[LEGAL-002]** Use short `See the LICENSE...` reference headers in first-party source/config/script files; remove duplicated project copyright/SPDX boilerplate while retaining third-party notices.
  - Priority P0; Status Implemented / static; project headers normalized; legal/license texts, generated files, fixtures and third-party notices retained; no compiler/license-scanner run. Its original CC-marked project docs were subsequently relicensed under LEGAL-003.
- [x] **[LEGAL-003]** License all project-authored documentation under BSD-3-Clause.
  - Priority P0; Status Implemented / static; `docs/file-headers.md` and `docs/torrent/snark-compatibility.md` now declare BSD-3-Clause, Debian/RPM/container metadata agrees, and the project-wide license page, NOTICE, policies and static contract reflect the unified documentation license. The CC notice file remains absent. Pending manual CI checks; no legal scanner or packaging validation was run.
  - Files: `docs/{file-headers.md,torrent/snark-compatibility.md,about/license.md,attribution-audit.md}`, `NOTICE`, `debian/copyright`, package metadata, `AGENTS.md`, documentation/packaging skills and `tests/check-legal-metadata.cmake`.
- [ ] **[CI-001]** Keep workflows `workflow_dispatch`-only, actions SHA-pinned, checkout credentials disabled and workflow lint clean.
  - Priority P0; Status Implemented; zizmor/yamllint clean; re-run after workflow edits.
- [ ] **[CI-002]** Close the latest manual-run compile and packaging failures before the next dispatch.
  - Priority P0; Status Implemented / static; Pending CI compilation; Pending CI tests. Read-only `gh` triage of `a2b8dc48` (`40381250464`, `40381278483`, `40381304032`, `40381324823`, `40381344546`) and `b9cbdaa8` (`40389733799`, `40389743711`, `40389753745`) plus six parallel subsystem reviews found and corrected: the missing `RequestLeaseSet` bind argument, garlic/SSU2/streaming/tunnel-pool/cache const or declaration mismatches, BOB's const socket peer check, Win32 token-enumerator shadowing and move-ctor order, the `size_t` torrent parser returns, `Mapping` assignment, the i18n capture reset, the invalid CMake escapes (`\[`, `\.`, `\0`), the stale documentation holder, the removed Alpine `miniupnpc-static` package, the stale `i2pdctl` diagnostic expectation, the Documentation-owned tools licence in `perl-install-layout`, the OpenBSD `imsg_get`/`imsgbuf_get` probe, `_GNU_SOURCE` for IPC credentials, direct standard/OpenSSL includes, `OPENSSL_PQ 0`, the undefined `AsyncUpstreamSockRead`, the NTCP2 SipHash provider parameter, the SSU2 PeerTest null guard, the `/tmp` artifact path and the Android status/internal-storage/reseed/dimension contracts.
  - Files: `libi2pd/{Crypto,Queue,Gost,Socks5,Transports,TransportSession,NetDbRequests,Blinding,util}.h`, `libi2pd/{Config,IdentMetrics,NTCP2,SSU2Session}.cpp`, `libi2pd_client/{BOB.cpp,TorrentsDHT.cpp,ClientContext.cpp,MatchedDestination.h,TorrentPayloadStorage.h,SOCKS.cpp,SOCKS.h}`, `daemon/I2PControlHandlers.h`, `privsep/{Channel,OpenBSDImsg,Supervisor,WindowsIpc}.cpp`, `i18n/check-i18n-catalogs.pl`, `tests/check-{build-system,legal-metadata,packaging}.cmake`, `tools/{CMakeLists.txt,test-perl-install.cmake,test-tools-cli.cmake,test-tools-inventory.cmake,tests/i2pdctl.t}`, `contrib/static/build-musl.sh`, `.github/workflows/packaging.yml`, `android/app/src/main/{cpp/i2pd_android.cpp,java/org/openi2pd/router/{DaemonWrapper.java,RouterStatusParser.java},res/values-w600dp/dimens.xml}`, `android/app/src/test/java/org/openi2pd/router/RouterStatusParserTest.java`, `docs/{static-build-audit.md,security/openbsd-validation.md}`.
  - Residual: Android SAF status on the UI thread, JNI state calls under the state monitor, unused data-directory JNI helpers, `TorrentsComments.h` guard asymmetry, `AcceptLocalControlPeer` template hazard and unaudited provider/ABI branches. The CMake `CURSES_INCLUDE_DIRS` concern is not a defect (module variable since CMake 3.1).

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
- [x] **[PERL-007]** Make the Python-free/portability guard complete and self-checking.
  - Priority P1; Status Implemented / static; Pending CI tests. The guard detects `python3`/`pip`/`PYTHONPATH`, `.py.in`/Cython/notebook forms, BOM and line-2 shebangs, `require` imports, pipe/temp/chmod hazards, missing consumer files and inventory drift across CMake/Make/Gradle/CI/Debian/RPM/Docker/Windows consumers. The inventory now includes `android/tools/check-android-jni-symbols.pl`, `i18n/check-i18n-catalogs.pl` and `tools/tests/maintenance-checkers.t`; `i18n/*.pl` is discovered; `tools/scripts/check-doc-nav.pl` is executable; `.gitignore`/`.dockerignore`/`tests/check-i18n.cmake`/`tests/check-tools-inventory.cmake` are consumers; and the stale Eclipse-PyDev/Sphinx ignore entries were removed.
- [x] **[PERL-009]** Register the new Perl guards and add negative checker fixtures.
  - Priority P1; Status Implemented / static; Pending CI tests. `check-android-jni-symbols.pl` and `check-i18n-catalogs.pl` are in `I2PD_PERL_SYNTAX_SCRIPTS`, the `perl-critic` list and the workflow shell lane; `android-jni-symbols-static` runs the JNI parity check; `perl-maintenance-checkers` builds temporary trees and asserts the link, navigation and JNI checkers fail on a broken input and pass on the repaired one. `i18n-static` degrades to a status message when Perl is absent instead of making Perl mandatory. A dispatch-only `perl_quality` job installs the distribution `libperl-critic-perl` package, compiles every maintained script, runs the Test::More suites and the critic profile without CPAN.
  - Files: `tools/tests/perl-portability.t`, new `tools/tests/maintenance-checkers.t`, `tools/CMakeLists.txt`, `tests/CMakeLists.txt`, `tests/check-i18n.cmake`, `.github/workflows/build.yml`.
- [ ] **[PERL-008]** Harden `i2pdctl` transport and output paths and verify install attribution.
  - Priority P0; Status implemented by static analysis; `_which` only accepts absolute `PATH` entries and resolves `openssl` absolutely; proxy variables are deleted in a localized `%ENV`; UTF-8 passwords are decoded at the environment boundary; server/CLI values are ASCII/control escaped; duplicate `raw_info` methods collapse. The install lane checks all 15 tools, `i2pdctl` and `LICENSE.i2pd-tools`; Perl::Critic remains optional and non-fatal when unavailable. Live TLS/OpenBSD/Windows tests pending.

## S. Torrent/Perl build and tests

- [ ] **[BUILD-011]** Add `TorrentsControl.cpp` to explicit CMake source lists; keep torrents optional and TUI optional.
  - Priority P0; Status Implemented / CI build pending; `build/CMakeLists.txt`.
- [ ] **[BUILD-012]** Ensure daemon/torrent engine builds without ncurses and without Perl runtime; no GNU Make.
  - Priority P0; Status Pending.
- [x] **[BUILD-013]** Install the Perl `i2pdctl` with correct interpreter/permissions and remove Python installation.
  - Priority P1; Status Implemented / static; `install(PROGRAMS)` preserves the portable shebang and executable mode, `perl-install-layout` installs only the `Tools` component into a private prefix and validates the result; CI execution pending.
- [ ] **[BUILD-014]** Make the POSIX make front-end actually build, install and clean safely.
  - Priority P1; Status implemented by static analysis; pending CI. Root and tests Makefiles declare `.PHONY`, default to `all`, build before install, forward `DESTDIR`/`PREFIX`, guard `BUILD_DIR` on clean, and keep `tools`/`tui` from being treated as existing source directories.
  - Files: `Makefile`, `tests/Makefile`, `tests/check-packaging.cmake`, `tests/check-build-system.cmake`.
  - Validation: Linux `make`/`bmake` and OpenBSD front-end runs pending.
- [ ] **[BUILD-015]** Modernize install components and exported package configuration.
  - Priority P1; Status Implemented / static; Pending CI compilation; Pending CI tests. Headers, `install(EXPORT)` and `i2pdConfig*.cmake` use the `Development` component; systemd units use an architecture-independent path; docs/man install independently of `WITH_BINARY`; `configure_package_config_file`/`@PACKAGE_INIT@` make the exported include paths relocatable and multiarch-safe. BUILD-018 corrects the earlier Boost.JSON/URL model: their compiled libraries are required, and the installed config re-finds enabled components.
  - Files: `build/CMakeLists.txt`, `build/cmake/i2pdConfig.cmake`, `build/CMakePresets.json`, `docs/build-system.md`, `docs/dependencies.md`.
  - Validation: per-component staging, consumer `find_package`, multiarch/OpenBSD install tests pending.
- [ ] **[BUILD-016]** Remove obsolete build/dependency workarounds and pin packaging generators.
  - Priority P1; Status Implemented / static; Pending CI compilation; Pending CI tests. CMake minimum is 3.24, OpenBSD rejects Unix Makefiles, Unix TUI dependency failure is explicit unless `I2PD_TUI_REQUIRED=OFF`, static discovery handles `.lib`, unused CMake modules were removed, RPM requires Ninja/Boost.JSON/URL, uses `%{_vpath_builddir}` instead of old Fedora/Mageia branches and no longer requires ENGINE, i2pd-git certificate links are fixed, static package tmpfiles path is corrected, and the Docker runtime installs zlib plus compiled Boost.JSON/URL libraries (BUILD-018 correction).
  - Files: `build/CMakeLists.txt`, `build/build_mingw.cmd`, `contrib/rpm/*.spec`, `contrib/static/package.sh`, `contrib/docker/Dockerfile`, `.dockerignore`, `debian/control`.
  - Validation: Fedora 43/44, static bundle, Docker, MSVC/MinGW and packaging checks pending.
- [ ] **[BUILD-017]** Add manual CI for install components, POSIX make and package consumption.
  - Priority P1; Status implemented in workflow definitions; pending dispatch. A manual Linux job stages each component and compiles an external consumer against the exported package; the OpenBSD job installs ncurses and asserts the TUI; the fuzz job instruments the linked libraries.
  - Files: `.github/workflows/build.yml`, `build-openbsd.yml`, `fuzz.yml`, `tests/check-packaging.cmake`.
  - Validation: no workflow was dispatched.
- [ ] **[BUILD-018]** Audit and correct static compilation/linkage integration after the publication series.
  - Priority P0; Status Implemented / static; Pending CI compilation; Pending CI tests.
  - Corrections: compiled Boost.JSON/URL linkage and exported discovery/Docker runtime; native translation helper namespace; torrent fixture declaration order; Linux Landlock UAPI portability; MinGW PE hardening. Direct POSIX/memory includes and explicit Windows import/macro contracts remove transitive assumptions. SB-004 is Dropped (pinned API supports overload): Material 1.14.0 accepts resource IDs; no Android source fix was needed.
  - Files/evidence: `docs/static-build-audit.md` (scope, SB-001..006, integration improvements and residuals), `build/CMakeLists.txt`, native/Android sources and paired regressions.
  - Validation: all six subsystem reviews are static; Linux/OpenBSD/Windows/Android, OpenSSL/LibreSSL, no-torrents, 32-/64-bit, old/new UAPI and installed-consumer compilation remain pending manual CI. No local build or test, workflow dispatch, commit or push.
- [ ] **[BUILD-019]** Modernize CMake targets, install/export, dependency detection and manual packaging CI.
  - Priority P1; Status Implemented / static; Pending CI compilation; Pending CI tests. One `i2pd_apply_dialect()` helper now owns capability, `BOOST_ASIO_HAS_SNPRINTF` and Windows SDK definitions for `libi2pd`, `i2pd_privsep` and `i2pd-tui`; `libi2pd` exposes one merged Windows import list; the daemon source list is shared with the Android JNI target, which no longer compiles the AF_UNIX TUI server. `CheckAtomic` reports `I2PD_ATOMIC_LIBRARIES` and restores probe state; static builds reject sanitizer/JNI combinations and restore finder suffixes; Boost.Container is optional for static Boost.JSON. `i2pd_privsep` installs its exported headers; `i2pdConfig.cmake` validates the zlib/Boost/OpenSSL components and supports Libraries-only staging. Torrent engine tests are compiled only with `WITH_TORRENTS=ON`; `tests/Makefile` is a forwarding shim; Perl checks can be made mandatory. Presets use schema 5 with install/CI presets. Packaging: OpenBSD DESCR/TUI assets/WANTLIB, Docker builder Boost packages, RPM Fedora guard/man path, and manual static-musl/container jobs.
  - Files: `build/CMakeLists.txt`, `build/cmake_modules/CheckAtomic.cmake`, `build/cmake/i2pdConfig.cmake`, `build/CMakePresets.json`, `tests/CMakeLists.txt`, `tests/Makefile`, `tests/check-*.cmake`, Android Gradle/CMake/check-config, workflows, packaging files, docs and build skill.
  - Residual: package-manager execution (RPM/static/OpenBSD port), Android Gradle/APK/device, MSVC/MinGW link and OpenSSL/LibreSSL floor lanes remain pending manual CI.
- [ ] **[BUILD-020]** Audit and correct compile/link seams before the next manual CI dispatch.
  - Priority P0; Status Implemented / static; Pending CI compilation; Pending CI tests. Six read-only passes (core C++23, transports/crypto/protocols, IPC/privsep, torrent/TUI/tools, Android/JNI, CMake/linkage) found and corrected: the stale `TunnelTest` `.first`/`.second` accesses; the unqualified `i2p::client::STOP_ON_SERVICE_TIMEOUT`; duplicated `WindowsChannel::m_Role`/`m_RoleSet`; the missing `IsCurrentProcessInJobObject()` definition; `_ANDROID__` versus `__ANDROID__` rollback guards; C++23 `std::string + std::string_view` translations in the TUI; missing `<openssl/hmac.h>`, `<cctype>`, `<cstring>`, `<unordered_map>` and other direct includes; the wide Windows worker-nonce test types; the AF_UNIX test guard; the OpenSSL linked-probe `<openssl/bn.h>`; preset schema 3.25; atomic/`i2pd_privsep` link propagation; and the duplicate tools-licence install. The 2026-10-26 workflows on `a2b8dc4` found additional C++ compile errors in `Destination.cpp`, the garlic session snapshot, `SSU2Session.cpp`, `Streaming.h`, `TunnelPool.cpp`, `SOCKS.h` and `I2PControl.cpp`; these are now corrected locally. The same run found the `dh-apparmor` Debian dependency omission and an i18n checker false positive for adjacent C++ literals. Packaging source licenses now live at the repository root, outside `docs/`; installed paths remain unchanged. `check-i18n.cmake` pins translation concatenation.
  - Latest workflow pass (2026-10-26, published `b9cbdaa8`): `BOB.cpp` used a const socket for Asio's non-const peer operations; the Win32 token helpers shadowed `TOKEN_INFORMATION_CLASS` enumerators; `ParseList`/`ParseDictionary` are `size_t`; `Mapping` lacked assignment after its move constructor; the i18n checker lost `$2` to a nested match; the container contract used an invalid CMake escape; Alpine dropped `miniupnpc-static`; and the Perl `i2pdctl` test expected obsolete argparse quoting. The six-subsystem follow-up also corrected the OpenBSD `imsg_get`/`imsgbuf_get` split with a `libutil` probe, `_GNU_SOURCE` for IPC credentials, direct standard/OpenSSL includes, the `OPENSSL_PQ 0` fallback, the unused `AsyncUpstreamSockRead()` declaration, the NTCP2 SipHash provider parameter, the SSU2 PeerTest null guard, the tools-test CMake escapes and licence component, the workspace artifact path, and the Android status/internal-storage/reseed/dimension contracts. All are corrected by static analysis.
  - Files: `libi2pd/{TunnelPool,Streaming,Crypto,Log,SSU2Session,Queue,Gost,Socks5,Transports,TransportSession,NetDbRequests,Blinding,util,Config,IdentMetrics,NTCP2}.{h,cpp}`, `privsep/{WindowsIpc.cpp,WindowsIpc.h,WindowsSupervisor.cpp,Channel,OpenBSDImsg,Supervisor}.cpp`, `daemon/{Daemon.cpp,I2PControlHandlers.h}`, `tui/main.cpp`, `libi2pd_client/{BOB.cpp,TorrentsDHT.cpp,HTTPProxy.cpp,ClientContext.cpp,MatchedDestination.h,TorrentPayloadStorage.h,SOCKS.cpp,SOCKS.h}`, `tests/{test-privsep-windows-ipc,test-i2pcontrol-socket}.cpp`, `tests/CMakeLists.txt`, `build/{CMakeLists.txt,CMakePresets.json}`, `tools/{CMakeLists.txt,test-perl-install.cmake,test-tools-cli.cmake,test-tools-inventory.cmake,tests/i2pdctl.t}`, `tests/check-{build-system,i18n,packaging,legal-metadata}.cmake`, `i18n/check-i18n-catalogs.pl`, `contrib/static/build-musl.sh`, `.github/workflows/packaging.yml`, Android status/DaemonWrapper/reseed/resources, `docs/static-build-audit.md` and owning skills.
  - Validation: compile-free checks only (`check-build-system`, `check-i18n`, `check-agent-skills`, `check-language-modernization`, `check-services`, `check-legal-metadata`, docs/branding, `git diff --check`). Latest published results are from `a2b8dc4` and the cancelled `b9cbdaa8` batch; they do not validate the corrections in this tree. No compiler, configure, CTest, Gradle, device, package or new workflow run was performed; the fixes are not compiled.
  - Residual: probe-dependent installed-header macros, full Windows/OpenBSD/LibreSSL/32-bit lanes, Android D8/ART and the remaining `TEST-036` negative coverage stay open.
- [ ] **[TEST-023]** Run the static-build audit regressions on the corrected revision.
  - Priority P0; Status Pending CI compilation; Pending CI tests.
  - Prepared: `test-boost-linkage` (torrent-enabled), qualified/global locale helper coverage, corrected torrent engine and Linux IPC assertions; installed consumer calls both compiled Boost APIs; MSYS2 enables hardening. Android toolchain/JNI validation remains pending without a source correction in this pass.
  - No tests executed; old/no-Landlock-header matrix coverage and kernel enforcement evidence remain outstanding.
- [ ] **[ANDROID-017]** Review the separate native Starting-state lifecycle concern.
  - Priority P0; Status Implemented / static; Pending CI compilation; Pending CI tests.
  - Files: `android/app/src/main/cpp/DaemonAndroid.cpp/.h`, `DaemonLifecycle.h`, `tests/test-android-daemon-lifecycle.cpp`. The outer transition owns Starting/Running publication; the adapter invokes the actual daemon start without treating its caller's Starting state as a duplicate. NativeCallGuard serialization stays in place. Prepared host regression covers start invocation, re-entry, rejection and failure/exception rollback; JNI/device restart evidence remains pending. Partial initialization unwind is ANDROID-018.
- [ ] **[ANDROID-018]** Make failed native initialization unwind its completed phases before retry.
  - Priority P0; Status Implemented / static; Pending CI compilation; Pending CI tests. `Daemon::init()` now records logging, crypto, router storage and router-context acquisition before each operation and unwinds false/throw paths in reverse. Context async persistence drains before storage revocation; logging resets to stdout after stopping. Prepared failure-injection and log-lifecycle regressions cover the seams; production callback/JNI/device evidence remains pending. Fail-closed storage selection and native-call serialization are preserved. Files: `daemon/Daemon.*`, `daemon/DaemonInit.h`, `libi2pd/RouterContext.*`, `tests/test-daemon-init-rollback.cpp`. Irreversible sandbox/config/filesystem effects are not rolled back.
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
  - Status: historical 2.13.0 third static pass added NET-155..NET-161/LS-119..LS-121/DB-113..DB-114/RS-101/SAM-110/DG-105/STR-104/CRY-202..CRY-203. Current comparison target is 2.14.0; NET-026 tracks the new review. The 2026-10-20 parity continuation added `docs/FEATURE-PARITY.md` and PARITY-001..PARITY-008; no dynamic Java/C++ test or build executed.
- [ ] **[DEF-006]** Android manifest still allows cleartext? Removed; verify merged manifest.
- [ ] **[DEF-007]** `tools/vain` raw key-buffer arrays were replaced with zeroing RAII ownership, jthread joins and checked serialization/error paths.
  - Status Implemented / static only; ASan/TSan and one-character search pending.
- [ ] **[DEF-008]** `famtool` family certificate/private-key handling needs deeper review.
- [ ] **[DEF-009]** Core process still combines network, filesystem and long-term keys; logging, opt-in router persistence and opt-in torrent payload storage are separated.
- [x] **[DEF-010]** Log worker is owned by a separate supervisor process with bounded restart and an explicit logger queue quota.
  - Status Implemented / static, CI pending; the log queue is capped at 8192 records with 8192-byte record truncation and a dropped-record counter; supervisor restart has exponential backoff, an eight-attempt cap and a stable-period reset. `test-log-lifecycle` is prepared for manual CI.
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
  - Status Partially fixed / static; Tunnel/Timestamp/NTP/RouterContext service and NetDb lifecycles now expose explicit cleared/restartable state. CONC-012 additionally closes tunnel/netDb queue admission and drains stopped-generation traffic; NetDb throttle maps now reset. Full Android/daemon restart matrix and stale-timer/socket checks remain pending.
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

## T. Memory-safety and concurrency audit (2026-10-18)

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
  - Priority P1; Status Implemented / static; Start() now uses locked counts and ResetRemoteRouterInfos is fenced by m_AcceptUpdates; Start/Stop/lookup TSAN stress pending.
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

Reference and full functional/protocol inventory: `docs/torrent/snark-compatibility.md`.
Existing task IDs remain open until their manual CI verification passes. The
current continuation began from clean HEAD `0cf32519`; no current local result
is dynamically validated. No local builds/configuration/tests or commits/push.

- [ ] **[SNARK-001]** Magnet, v1 metadata and full disk creation.
  - Priority P0; Status implemented by static analysis; pending CI compilation; pending CI tests.
  - Preserves multitracker magnets, exact infohashes, BEP 12 tiers and private-swarm guards. `TorrentsCreator` now enumerates/hashes incrementally with no-follow bounds, automatic/explicit piece sizes, cancellation and progress. `TORRENT-CREATE-V1` carries tracker tiers/privacy; TUI n/N/K and native Android root creation are connected.
  - Public creation with no tracker now inherits the tunnel's common endpoints; private creation without a tracker is rejected before hashing. Pending: live Java exchange, filesystem mutation/failure injection, fully empty payload policy, nested-root session persistence and source snapshot semantics. Pure v2 unsupported; hybrid v1 projection is not BEP 52 validation.
- [ ] **[SNARK-002]** I2P tracker administration and exact endpoint membership.
  - Priority P0; Status implemented by static analysis for paused add/remove in TUI and Android; pending CI compilation; pending CI tests.
  - Exact endpoint membership and explicit empty lists survive persistence; private swarms cannot use unrelated common trackers. Announce paths now reject paused/state-loading torrents and late started announces after a pause.
  - A primary `announce` outside a non-empty `announce-list` is normalized into tier 0; private torrents expose a fixed tracker policy and reject administration. Tracker snapshots expose tiers. Malformed/non-200/transport failures now back off with bounded error text; success without an interval re-bases it; tracker peers expire; datagram transaction IDs avoid collisions and validate the local reply port. A dispatched announce preserves the failure ladder instead of overwriting it with the advertised interval; HTTP announces are single-flight per `(torrent, slot)`, capped at 16, and share one 8-second deadline for write and response; registry slots are reference counted, reused lowest-free-first and never renumbered, while common slots are never recycled. Pending: BEP 12 ordered failover/shuffling (do not change the default before a verified Java comparison), persistence acknowledgements and crash/failure testing. Test: `test-torrents-tracker-dispatch` prepared.
- [ ] **[SNARK-003]** I2P DHT and PEX interoperability/security.
  - Priority P0; Status implemented by static analysis; pending CI compilation; pending CI tests.
  - Opaque present-empty tokens, secure transactions/NID binding, raw source-port checks, seed/noseed filtering, global XOR selection, per-origin budgets, enforced retry bounds and immediate correlated error progression are in source. Complete PEX/DHT dictionaries and private/stopped guards preserved.
  - This increment adds 60 s DHT query lifetimes, authenticated-response failure clearing, replies from full foreign buckets, k-node/64-value response caps, infohash-bound and capacity-evicted tokens, safe/atomic DHT state names, capped loads, load-before-receiver ordering, PEX receive budgets/zero filtering, established-only PEX sends, write-once positive extension IDs, private metadata suppression and throttled DHT hints. Metadata that reveals a private swarm now purges served peers, our rendezvous identity, issued tokens and outstanding get_peers queries, and a bounded private-hash set refuses later inbound get_peers/announce_peer; a removed private hash stays unservable. The continuation adds a service-thread queue for repliable datagrams, a bounded FIFO token store with a per-node quota, a two-node-per-destination routing cap, a global raw-frame budget, periodic state save, DHT port clamping, error-reply failure marking, dual nodes/values parsing and a side-effect-free KRPC frame decoder. PEX/BEP 10 now reject repeated handshakes, suppress private metadata_size, track whether a remote bitfield was received before calling it a seed, tolerate an unrelated rport, and enforce per-torrent PEX/DHT budgets; PEX/DHT candidates use a bounded per-torrent table with per-origin TTL and retry backoff. State names/paths are I2P-only and no clearnet/public-DHT fallback exists.
  - Pending: BEP 5 announce `port`/`implied_port` parity, live Java trackerless exchanges, adversarial churn, seed outgoing connection policy and process-separated DHT persistence. Prepared but unexecuted: `test-torrents-dht-protocol`, `test-torrents-pex`, `fuzz-torrents-krpc` and `fuzz-torrents-extensions`.
- [ ] **[SNARK-004]** Opt-in interoperable comments/ratings.
  - Priority P1; Status implemented by static analysis for negotiated exchange, bounded per-peer budgets, store/moderation/statistics and native control persistence; pending CI compilation; pending CI tests.
  - TUI Comments/c/C/H and Android controls connected. Disabled by default; private torrents suppress exchange and omit `ut_comment` entirely. Bencode retains anonymous/rating-only empty fields; labels are never authenticated identities. The codec accepts Java's up-to-256-entry replies, skips malformed entries, publishes newest-first, normalizes CR/LF/TAB, caps wire frames at Java's 16482-byte extension limit, dedupes a normalized owner/text key regardless of rating, bounds per-pseudonym and anonymous visible entries, evicts old foreign entries at capacity, and supports local unhide. The continuation adds tolerant `filter` hints, per-connection 40/min and per-torrent 80/min admitted-entry windows, revision-aware immediate refresh after a local change (10 s floor, 600 s cadence), revision bumps for hide/trim persistence, and future-timestamp clamping on state load. `TORRENT-DETAIL <tunnel> <id> [offset] [limit]` pages the retained comments with visible/hidden/local counts; TUI uses `[`/`]`, aged timestamps, bidi stripping and confirmed hide/unhide, and Android pages with age/local markers. TUI strings remain English (`I18N-004`); Android strings are in all four catalogs.
  - Pending: differential Java peer exchange, consent/renegotiation/lifecycle and crash tests, normalized-dedupe/rating parity against Java `CommentSet`, and whitespace/rating policy confirmation. Do not infer interoperability validation from codec roundtrips.
- [ ] **[SNARK-005]** Autonomous Android native client and SAF.
  - Priority P0; Status implemented by static analysis for existing native panel, bounded SAF torrent import, priorities/comments/trackers, root creation and new document/tree SAF creation/export; pending CI compilation; pending CI tests.
  - SAF hashes incrementally, preserves URI authority, builds metainfo with native CreateV1 and exposes size/privacy/trackers/cancel/progress. Instrumented JNI vectors prepared and instrumentation APK added to manual workflow.
  - Connected SAF tree payload reads/writes, persisted per-torrent grants, startup rehashing and Binder/JNI broker. Startup verification now uses the shared one-piece-per-disk-job chain instead of blocking the disk worker with a whole-tree rehash; private resume files are read and pieces are still hashed before publication. Zero-length files emit no document open. Re-selection is resolved on the next open, and cached native handles are closed when the tree generation changes. Pending: provider fixtures, device/ENOSPC/external-replacement tests, external-router mode and first configuration-write crash recovery.
- [ ] **[SNARK-006]** Operating-system capability separation.
  - Priority P0; Status implemented by static analysis for an opt-in payload storage process and Android Binder backend; pending CI compilation; pending CI tests; global invariant pending.
  - `isolatedstorage=true` connects TorrentFile IO to inherited authenticated bounded IPC, delegated root FD, read-only/writable handles, 16KiB transfers, timeout poisoning and denied external sockets. Read/write path confinement and negative tests prepared. Requested backend failures never fall back to parent IO.
  - Pending: remove parent router/DHT writes, network/session/verification workers and complete supervisor policy; Windows backend; Android Binder recovery validation and end-to-end negative capability validation. No global no-network-plus-write claim.
- [ ] **[SNARK-007]** CI, provenance and regression coverage.
  - Priority P0; Status implemented by static analysis; pending CI compilation; pending CI tests.
  - Retains magnet/engine/DHT/comments/creator/state/priorities targets; adds real storage-worker/TorrentFile regression, option/cancellation/canonical-empty bencode vectors, JNI instrumentation source and local-state/request fuzz coverage. The DHT/PEX continuation adds `test-torrents-dht-protocol`, `test-torrents-pex`, `fuzz-torrents-krpc` and `fuzz-torrents-extensions` to the manual fuzzer list and the focused torrent CTest selection. Differential Java/native fixtures remain to be authored: Tier A is an offline parser-vector corpus with a manifest/generator revision (no real identities); Tier B is a loopback I2CP/SAM exchange against a locally hosted Java 2.14.0 harness on a private router. Neither is executed and neither may be reported as public-network interoperability. Manual workflow compiles both Android APKs; all triggers remain manual.
  - Pending: actual Linux/OpenBSD/LibreSSL/Windows/Android, Java differential/live, sanitizer/fuzzer and device runs. No earlier revision result validates this local tree.
- [ ] **[SNARK-008]** Complete advanced functional inventory.
  - Priority P1; Status in implementation (static analysis where specified).
  - Selection, shared-boundary scheduling and persisted priorities exist. This increment adds selected-file completion/status, selected tracker `left`, per-file promotion from `.part`, `GetFullLeft()` diagnostics and mixed final/`.part` restart verification. Exact resume bitfields are validated and atomically replaced; explicit recheck hashes incomplete pieces. Accepted blocks refresh piece activity instead of hitting the 22 s reset, and each connection releases its own outstanding blocks on choke, keep-alive reset, reject or disconnect. TUI/Android show selected size and privacy; tracker snapshots expose tiers and private edits fail closed. The scheduler continuation adds a 60 s per-request deadline before the 95 s blanket release, real block-length accounting, idle peer-rate decay, complete+verified+not-saving per-file progress, `eTorrentErrorFilesystem` publication on a failed write (cleared by a later verified write) and bounded best-effort corrupt-peer charging (three strikes, 64-entry in-memory table, no persisted ban). TUI `Ctrl-X`/`Ctrl-D` and Android session removal use `TORRENT-REMOVE-ALL`; Android exposes single-torrent start/stop, and `TORRENT-COMMENT-UNHIDE` parses again. Remaining: full bandwidth/connection controls, durable magnet/queue recovery (`SNARK-012`), endgame/CANCEL send, unchoke/upload-slot policy, advanced settings and exact error/retry parity. Keep essential-operation tasks open while any capability or validation remains missing.
- [ ] **[SNARK-009]** Complete SAF payload backend and lifecycle recovery.
  - Priority P0; Status implemented by static analysis for URI-backed tree download/seeding via bounded Binder/JNI and common native payload storage; pending CI compilation; pending CI tests. Grants and priorities persist; provider bytes are rehashed after restart without trusting the resume bitfield. Unsupported seek fails without staging. Implemented statically: bounded Binder generation recovery, cancellation/retirement and provider fault fixtures; exact-document deletion; brokered cancelable export with exact-size commit/abort; document-ID/size validation before every transfer; shorter existing documents are extended only in write mode and remain unverified; grant reference counting; next-open tree re-resolution with native handle teardown; async startup verification and zero-length-file fragment suppression. The continuation probes every document through the payload backend at startup (`TorrentFile::ProbePayload`), verifies only pieces fully backed by present files, leaves missing/revoked documents downloadable, and reopens a POSIX read-only final-name handle writable after invalidation. The broker arms the 15 s/30 s watchdog only after `ioLock`, so a queued operation is not fenced for a stalled sibling, and the client bind budget recovers from a transient broker crash without an app restart. Fixtures now cover a nested multi-file tree, zero-length documents, deleted/revoked documents, same-length replacement, no-seek/socket, read-only write failure and post-retire rebind. Pending: end-to-end device death/grant/provider/ENOSPC tests, SAF resume trusted by document identity, and real-provider validation.
- [ ] **[SNARK-010]** Complete multiprocess platforms and remove remaining parent authority.
  - Priority P0; Status pending. Complete Windows storage backend and Android recovery; isolate networking/session/verification; move metadata, allocation, rename/delete, DHT and router persistent IO out of network-capable processes. Add OS-negative tests and capability-supervisor teardown/restart coverage.


## Native init services and Debian packaging (2026-10-19)

Status: implemented by static analysis; pending manual CI service/package
validation. These tasks do not claim a successful build, service start or
package install.

- [ ] **[SERVICE-001]** Provide a native systemd unit with foreground `Type=exec`, restricted user, journald logging, native state/runtime directories, bounded restart and IPC-compatible hardening.
  - Priority P0; Status implemented / static; `contrib/i2pd.service` uses `Type=exec`, a 660 s stop budget, `KillMode=mixed`, `SyslogIdentifier=open-i2pd`, no alias/readiness claim, and ships sysusers/tmpfiles policy. `systemd-analyze`, start/stop/status/reload and sandbox/IPC checks pending CI.
- [ ] **[SERVICE-002]** Provide native GNU Shepherd, OpenRC, runit, s6/s6-rc and SysVinit definitions with foreground supervision and secure paths.
  - Priority P0; Status implemented / static; native definitions cover Shepherd, OpenRC, runit, s6/s6-rc and SysVinit. OpenRC derives `retry` from `stopsig`, s6 ships `down-signal=INT`/`timeout-kill=660000`, Shepherd uses a data-dir pidfile with a 700 s stop, SysV no longer unlinks an identity-checked pidfile, and OpenBSD uses foreground rc.subr supervision. The log worker and opt-in storage workers remain isolated; network/crypto/core migration is pending. Manager start/stop/failure/signal tests pending CI.
- [ ] **[SERVICE-003]** Select exactly one init system at CMake install time and install no competing service definitions by default.
  - Priority P1; Status implemented / static; `I2PD_SERVICE_MANAGER`, `I2PD_SERVICE_DIR`, `I2PD_SERVICE_CONFDIR`, `Service` component; explicit `install(PROGRAMS/FILES)` modes for runit/s6/s6-rc and systemd sysusers/tmpfiles; `tests/check-build-system.cmake`, `tests/check-services.cmake`; configure/install matrix pending CI.
- [ ] **[DEB-001]** Modernize Debian packaging for C++23, debhelper 13, native systemd/SysV/tmpfiles, split `i2pd`/`i2pd-tui`, DEP-5 metadata and autopkgtest.
  - Priority P0; Status implemented / static; `debian/`; `LICENSE.i2pd-tools`/CC-BY-SA notices, TUI desktop/assets split, dpkg-buildflags hardening, `${perl:Depends}`, private-path preservation on upgrade and native static-package version are in place. Build, `dh_missing`, lintian, autopkgtest, install/upgrade/remove/purge and systemd/SysV coexistence pending CI.
- [ ] **[DEB-002]** Keep Debian conffiles, service ownership and TUI/daemon manifests coherent with CMake components.
  - Priority P0; Status implemented / static; `Runtime`/`Tui`/`Tools`/`Documentation`/`Service` split, desktop `Exec=i2pd-tui`, no TUI service, conffile-preserving reinstall and remove/purge checks are prepared in `debian/tests/smoke` and `.github/workflows/packaging.yml`. Package lifecycle runs remain pending CI.
- [ ] **[DEB-003]** Publish a merge-ready OpenBSD port template with rc.d, PLIST, samples and LibreSSL/paths policy.
  - Priority P1; Status implemented / static; `contrib/openbsd/{i2pd.rc,i2pd.login,i2pd.conf,port/}` uses foreground `rc_bg`, `daemon_logger`, `daemon_timeout=660`, `tunnels.conf.d`, read-only certificates, `@newuser`/`@rcscript`/`@sample` and a 0750/0640 path policy. A real external ports build, `pkg_create`/`pkg_add`, rcctl and LibreSSL runtime checks remain pending.
- [ ] **[SERVICE-004]** Add manual CI validation for service contracts and Debian packages.
  - Priority P0; Status partial / static; `.github/workflows/packaging.yml` is `workflow_dispatch`-only, actions are pinned and checkout credentials are not persisted. Static service contracts now also cover OpenBSD rc.d/login and the OpenBSD port template. The Debian job runs installed-unit `systemd-analyze`, manifest/split checks, conffile-preserving reinstall, autopkgtest and remove/purge checks. Native `s6-rc-compile`, Guile/Shepherd load, OpenRC/runit/s6 starts, RPM/static bundles and package service tests remain pending.
- [ ] **[SERVICE-005]** Document enabling, stopping, restarting, status, logging, hardening and Debian package behavior for every supported manager.
  - Priority P1; Status implemented / static; `docs/service-management.md`, `docs/user-guide/run.md`, `docs/user-guide/install.md`, `docs/build-system.md`, `docs/devs/building/unix.md`; rendered documentation and link checks pending CI.
- [ ] **[SERVICE-006]** Make daemon initialisation failure visible to service managers and align native stop semantics with the graceful drain.
  - Priority P0; Status implemented / static; `daemon/i2pd.cpp` returns `EXIT_FAILURE` when `Daemon.init()` fails. systemd/OpenRC/runit/s6/Shepherd/SysV and the unselected Upstart/dinit samples use foreground supervision and documented INT/TERM stop budgets; s6 ships `down-signal`/`timeout-kill`. Native manager failure injection, `s6-rc-compile`, Shepherd load and `systemd-sysusers`/`tmpfiles` execution remain pending CI.

## Selective upstream openssl synchronization (2026-10-18/19/20)

Reviewed branch `PurpleI2P/i2pd:openssl` through
`9725b8b227293a796c069ac270fa6b5b47cf9a13` (first-pass mark
`de32c8c3f580c2c43ca09154ecfe6a20db80d19f`). The detailed commit table and
omissions are in `docs/upstream-sync.md`.

- [ ] **[UPSTREAM-001]** Import the compatible 18/19 October router-source corrections without reverting fork architecture.
  - Priority P0; Status implemented / static; SSU2 PathChallenge state guard, I2PTunnel `view()` and header compaction, NetDb 0.9.57 floor, torrent stopped/removed persistence guards; pending CI compilation and tests. Upstream 3.0.0 release metadata and browser console changes intentionally omitted.
- [ ] **[UPSTREAM-002]** Validate Debian 12 NetDb peer-floor compatibility and the retained high-bandwidth floor.
  - Priority P0; Status implemented / static; `libi2pd/NetDb.hpp`, `test-netdb-store-lookup-hardening`; live Debian 12 peer and NetDb selection tests pending CI. The 0.9.57 floor is a peer-policy relaxation, not a parser or cryptographic weakening.
- [ ] **[UPSTREAM-003]** Add a stable I2PTunnel HTTP header parser test seam.
  - Priority P1; Status implemented by static analysis; `I2PTunnelHTTP.h` owns independent bounded incremental request/response state and stable transformed output. The production connections use the same parser as `test-i2ptunnel-http`; fragmented Connection/Upgrade, binary body, interleaved directions and byte/header bounds have prepared regressions. Pending CI compilation and pending CI tests; no socket or live HTTP validation is claimed.
- [ ] **[UPSTREAM-004]** Close the remaining torrent removal/persistence race.
  - Priority P1; Status implemented by static analysis; irreversible instance tombstones revoke new writes, and a hash reservation blocks replacement until serialized disk cleanup succeeds. Both native and isolated removal delete `.openi2pd-state` even when payload is retained; cleanup failure keeps the hash blocked until daemon restart. In-flight disk operations may finish before cleanup, never after replacement is admitted. Final shutdown snapshots retain narrowly scoped authority after membership clearing; removed instances never do.
  - Validation: prepared `test-torrents-security` regressions for direct/delegated resume revocation, queued removal/re-add, retained payload, metadata failure and POSIX no-follow removal. Pending CI compilation and pending CI tests, ASan+UBSan/TSan, storage-worker and real shutdown/crash tests. Windows path-check/delete TOCTOU and global process separation remain open.
  - Pending implementation: a dedicated regression for final shutdown snapshots after membership clearing; the source correction exists but is not covered by the deterministic removal harness.
- [ ] **[UPSTREAM-005]** Apply the applicable 2026-10-19/20 `openssl` continuation without reverting the fork architecture.
  - Priority P0/P1; Status implemented / static; `37090fe0` Intel AMT ports 16992-16995 are in `IsPortInReservedRange` with `test-routercontext-port-range`; `21ad7d43` reseed wording is adapted while retaining fail-closed `reseed.verify=true`; `db761a54`/`2598a94e`/`86dd211a` RPM Fedora Boost guards are adapted; `452da43b`/`54068be9`/`472ef88b` remain already implemented/stronger. `38cf542b`, `f8a12d9d`, `357fcc14`, `a8915cd1`, `e34db4f7`, `233e5111` and `de32c8c3` are recorded as not applicable, merge-only or obsolete in `docs/upstream-sync.md`; the RHEL 9 torrent branch is omitted pending a validated GCC 13 toolset path.
  - Files: `libi2pd/util.cpp`, `tests/test-routercontext-port-range.cpp`, `tests/CMakeLists.txt`, `contrib/rpm/i2pd.spec`, `contrib/rpm/i2pd-git.spec`, `contrib/i2pd.conf`, `contrib/openbsd/i2pd.conf`, `docs/upstream-sync.md`, `docs/files-changed.md`, `docs/dependencies.md`, `docs/build-system.md`, `docs/devs/building/unix.md`, `docs/user-guide/configuration.md`, `tests/check-build-system.cmake`, `.agents/skills/openi2pd-upstream/references/sync-checklist.md`.
  - Validation: static source/diff inspection only; `build-system-static`, `test-routercontext-port-range`, Linux/OpenBSD/Windows compilation, CTest, package/RPM checks and manual CI remain pending. No local build or test was run.

- [ ] **[UPSTREAM-006]** Apply the post-`de32c8c3` 2026-10-20 continuation without reverting the fork architecture.
  - Priority P1; Status implemented by static analysis; pending CI compilation and pending CI tests. `35e25db2` proxy error pages now use typed 400/431/500/502/504 statuses while preserving the fork's 1 MiB header bound and nil-socket guard; status constants moved to `HTTPProxy.h` and the mapping is pinned. `9725b8b2` DHT rendezvous self-announce was adapted with `DHTTorrent::GetPeers()`/`RemovePeer()`, a reserved local peer slot, `Unannounce()` on stop/remove, upstream empty-bucket fallback, `IsRemoved` suppression, `MAX_DHT_TORRENTS`, peer caps, private/stopped suppression and the fork's seed bit. `032b8a61` was already superseded by `I2PTunnelHTTP.h`; mixed-case Proxy-Connection and obs-fold rejection regressions were added. `8710fd89` is merge-only; `137dde2f` has no applicable hunk (its `0%{?rhel} <= 9` condition is false-positive when undefined); `c5e8e10a` is incompatible because it packages the removed web console.
  - Files: `libi2pd_client/HTTPProxy.*`, `libi2pd/HTTP.cpp`, `libi2pd_client/I2PTunnelHTTP.h`, `libi2pd_client/TorrentsDHT.*`, `libi2pd_client/TorrentsTunnel.cpp`, `tests/test-http-res.cpp`, `tests/test-http-proxy-limits.cpp`, `tests/test-torrents-dht-interop.cpp`, `tests/test-torrents-security.cpp`, `tests/test-i2ptunnel-http.cpp`, `docs/upstream-sync.md`, `docs/files-changed.md`, `docs/torrent/`, `ROADMAP.md`, `PROGRESS.md`, `AGENTS.md`.
  - Validation: `test-http-res`, `test-torrents-dht-interop`, `test-i2ptunnel-http` and `build-system-static` are prepared; Linux/OpenBSD/Windows/Android compilation, CTest, sanitizers/fuzzers, RPM/package and live proxy/DHT/peer checks remain pending. No local build, test, workflow, commit or push was performed.


- [ ] **[UPSTREAM-007]** Verify the post-9725b8b2 window and close residual integration gaps.
  - Priority P1; Status verified/implemented by static analysis; pending CI compilation and pending CI tests. A fresh `git ls-remote` on 2026-10-21 confirmed `9725b8b2` was the `openssl` tip at that time; the later `5bcf6101` window is covered by UPSTREAM-008. The review closed `35e25db2`/`9725b8b2` fork-side gaps: DHT self-unannounce, reserved local peer slot, `IsRemoved` guard, empty-bucket predicate, proxy status mapping constants/tests and obs-fold rejection.
  - Files: `docs/upstream-sync.md`, `libi2pd_client/HTTPProxy.*`, `libi2pd_client/I2PTunnelHTTP.h`, `libi2pd_client/TorrentsDHT.*`, `libi2pd_client/TorrentsTunnel.cpp`, tests above.
  - Validation: static comparison and prepared regressions only; no compiler, CTest, sanitizer, fuzzer, live proxy/DHT/peer or workflow run exists.

- [ ] **[UPSTREAM-008]** Apply the `9725b8b2..5bcf6101` openssl continuation (15 commits).
  - Priority P1; Status Implemented / static; Pending CI compilation; Pending CI tests. Integrated torrent fixes: optional datagram-tracker port 6969 at the use site, filesystem error state published atomically from the disk worker, BEP 3 cancel handling with the fork's BEP 6 reject guard, not-interested state on completion and when the peer still has wanted pieces, duplicate/block-applied gating before hash/save/accounting, piece send through `SendBuffer` with one synchronous copy, close files on stop before resume persistence, and rate resets. Fork hardening beyond upstream: the add-time disk job and its announce continuation hold `weak_from_this().lock()` instead of raw `this`, all announce paths (initial, periodic, start, reannounce) skip a torrent with a non-zero error, and a verified piece write clears `eTorrentErrorFilesystem`. Already-equivalent: `b31431ba` DSA NULL-signature guard and `5bcf6101` piece invalidation. Omitted: `14261bb7` (GNU make), `b1a30e99` (removed Debian quilt patch), merge-only commits. No web console or legacy build files were restored.
  - Files: `libi2pd_client/Torrents.*`, `libi2pd_client/TorrentsTunnel.*`, `tests/test-torrents-priorities.cpp`, `tests/check-control-security.cmake`, `docs/upstream-sync.md`, `ROADMAP.md`, `PROGRESS.md`, `AGENTS.md`, torrent/upstream skills.
  - Residual: Java type-8/blinding/NTCP2 vectors, `Signer::Sign` fallible-interface migration, torrent peer runtime, OpenBSD/Windows/Android compilation and sanitizers remain pending. No local build, test, workflow, commit or push.
- [ ] SNARK-010: validate OpenBSD pre-pledge supervised storage startup and explicit
  worker replacement. Socket-only handoff and inherited root are implemented
  statically; preserve runtime pledge and validate limits against admission.

## Native payload continuation (2026-10-20)

- [ ] SNARK-006/SNARK-009: common C++ payload capabilities, Android Binder piece
  IO and durable-publication guard implemented by static analysis; pending CI
  compilation and tests. Linux isolated allocation now runs in storage worker.
- [ ] SNARK-010: OpenBSD supervisor bootstrap is implemented by static analysis; pending CI compilation and tests. Respect the
  pledge prohibition on passing directory FDs; inherit authorized root at exec
  and transfer only socket channels after pledge. Keep global network/core/
  router/DHT persistence separation and Windows backend pending.

## Binder recovery and persistent-authority continuation (2026-10-20)

- [ ] SNARK-009: implemented by static analysis: stable local capabilities,
  immutable URI authority, generation/death fencing, bounded rebind/backoff,
  out-of-band cancellation and watchdog retirement. Pending CI compilation,
  CI tests and actual persisted-grant broker-death/remapping instrumentation.
- [ ] SNARK-006/SNARK-010: implemented by static analysis: isolated native
  payload promotion/removal and bounded atomic metainfo/resume/settings writes.
  Lifecycle regression registered, pending CI compilation/tests. Eight exclusive
  staging slots bound crash remnants; exhaustion requires explicit cleanup.
- [ ] SNARK-010: global privilege separation remains in implementation. Router
  keys/config/netDb/DHT persistence still runs in the network-capable parent;
  network/router/verifier worker integration and platform enforcement are not
  implemented by these torrent storage operations. Do not mark global invariant
  complete or validated.

## Persistent authority completion increment

- [ ] SNARK-009: exact SAF document deletion, brokered cancelable metainfo
  export and storage status UI implemented by static analysis; pending CI
  compilation/tests and real provider/device execution.
- [ ] SNARK-006/SNARK-010: DHT load/save delegated in isolated tunnels, with
  preserved54-byte records, bounded metadata reads and explicit legacy-name
  authority. Fixed router state worker connected before core initialization
  through privsep.routerstorage. Implemented by static analysis; pending CI
  compilation/tests. Unsupported requested backends fail closed.
- [ ] SNARK-010: global separation remains in implementation: netDb/profile/
  config/client writes, actual network/router/verifier processes and complete
  Windows/Android authority backends are still missing. These tasks cannot be
  relabelled as only pending tests. Remaining bandwidth/queue/per-file native
  promotion and protocol interoperability tasks above remain open.

## Comprehensive Java functional-parity audit (2026-10-20)

Status: recognition and scoped implementation are static only. That pass used
`i2p-2.13.0` at `9134f808337b401e8e53c73734c81fab04280c9d`. The current
target is 2.14.0; re-auditing these findings is pending under NET-026. Full inventory:
`docs/FEATURE-PARITY.md`; wire findings: `docs/INTEROPERABILITY.md`.

- [ ] **[PARITY-001]** Complete the functional inventory for router, crypto,
  services, I2PSnark, administration/TUI, Android and security.
  - Priority P0; Status implemented by static analysis; reference and status
    categories recorded. No Java source was vendored or executed.
  - Files: `docs/FEATURE-PARITY.md`, `docs/INTEROPERABILITY.md`.
  - Validation: documentation/link and source inspection only.
- [ ] **[PARITY-002]** Implement SSU2 migration, ML-KEM provider probing,
  RSA/SU3 verification and reseed default hardening.
  - Priority P0; Status implemented by static analysis; pending CI compilation
    and pending CI tests.
  - Files: `SSU2Session.*`, `PostQuantum.*`, `Signature.*`, `Identity.cpp`,
    `Reseed.cpp`, `Config.cpp`; focused tests registered.
  - Validation: dynamic loss/provider/RSA/SU3 vectors pending.
- [ ] **[PARITY-003]** Implement SAM pipelining/control grammar, I2CP identity-hash
  routing keys and case-preserving address-book normalization.
  - Priority P0/P1; Status implemented by static analysis; pending CI
    compilation and pending CI tests.
  - Files: `libi2pd_client/SAM.*`, `I2CP.*`, `AddressBook.*`; new tests.
  - Validation: split-frame/pipelined SAM, live I2CP and naming tests pending.
- [ ] **[PARITY-004]** Implement torrent resume/final-name verification, tracker
  parsing/limits, private-swarm announce selection, PEX and connection caps.
  - Priority P0; Status implemented by static analysis; pending CI compilation
    and pending CI tests. No live Java tracker/trackerless exchange or crash
    recovery exists.
  - Files: `Torrents.*`, `TorrentsTunnel.*`, `TorrentsControl.h`; prepared
    parity regression.
- [ ] **[PARITY-005]** Add a typed daemon-owned TUI administrative command queue
  for reseed, shutdown/cancel, tunnel reload, bandwidth, share, transit and log
  level.
  - Priority P0; Status implemented by static analysis; pending CI compilation
    and pending CI tests. No generic config writer was added.
  - Files: `TuiProtocol.h`, `daemon/TuiServerCommon.*`, `daemon/Daemon.*`,
    `tui/main.cpp`; protocol/IPC regressions prepared.
- [ ] **[PARITY-006]** Add Android reseed and runtime log-level parity without
  main-thread JNI, retaining Material 3/Java and the existing service lifecycle.
  - Priority P1; Status implemented by static analysis; pending CI compilation,
  JNI and device tests. Confirmed Reseed action, service-worker JNI, closed
  runtime log-level set, live apply after persistence and a static
  instrumentation consistency test are prepared.
- [ ] **[PARITY-007]** Close remaining functional gaps in native client/server
  tunnel creation/editing, selected-file torrent promotion, durable queue/
  bandwidth scheduling, secret-blinded I2CP and Android SAF recovery.
  - Priority P1; Status pending. These are implementation tasks, not merely
  validation tasks. Address-book administration and force recheck moved to
  PARITY-009/PARITY-010.
- [ ] **[PARITY-009]** Replace the Java address-book web editor with native TUI
  and Android list/add/replace/remove operations.
  - Priority P1; Status implemented by static analysis; pending CI compilation
    and pending CI tests. `AddressBook` exposes bounded list/add/replace/remove,
    Java-compatible name validation, shared-identity reference safety and an
    empty-index removal path. TUI Destinations and an Android Material screen
    use bounded local IPC/app-internal JNI with full base64 identities only.
    Deleted-host tombstones moved to PARITY-012; subscription URL editing and a
    live Java lookup remain open.
  - Files: `libi2pd_client/AddressBook.*`, `libi2pd/TuiProtocol.h`,
    `daemon/TuiServerCommon.*`, `daemon/Daemon.cpp`, `tui/main.cpp`,
    Android address-book/JNI bridge, `tests/test-addressbook-admin.cpp`.
  - Validation: offline name/mutation/reference tests prepared; Android
    device/instrumentation and live lookup pending.
- [ ] **[PARITY-010]** Expose I2PSnark force recheck through TUI and Android.
  - Priority P1; Status implemented by static analysis; pending CI compilation
    and pending CI tests. `TORRENT-RECHECK` is parser/dispatch-validated,
    requires a paused complete torrent, and remains on the torrent service
    thread. TUI `V` and an Android menu action use the same bounded path.
  - Files: `libi2pd/TuiProtocol.h`, `daemon/TuiServerCommon.cpp`,
    `libi2pd_client/TorrentsTunnel.cpp`, `TorrentsControl.h`, `tui/main.cpp`,
    Android `TorrentsFragment`/`TorrentControl`, tests.
  - Validation: protocol and Android command regression prepared; Android
    rejects torrent id 0 (the engine's not-found sentinel), and the engine
    storage-failure fault injection remains a follow-up.
- [ ] **[PARITY-011]** Match Java's exploratory-tunnel DatabaseStore reply-token
  policy.
  - Priority P1; Status implemented by static analysis; pending CI compilation
    and pending CI tests. A token-bearing DatabaseStore received through the
    router's tunnel is dropped before DeliveryStatus construction; direct
    stores, zero-token stores and the `0xFFFFFFFF` no-reply marker remain valid.
  - Files: `libi2pd/NetDb.hpp`, `NetDb.cpp`, `tests/test-tunnel-dsm-policy.cpp`.
  - Validation: pure policy regression prepared; live tunnel injection requires
    manual peer/CI testing.

- [ ] **[PARITY-012]** Add deleted-host tombstones and native block/unblock.
  - Priority P1; Status implemented by static analysis; pending CI compilation
    and pending CI tests. `deleted.txt` is loaded with a 64 KiB/256-pattern
    bound; names are normalized and matched by an anchored bounded glob.
    Removing a name persists an exact tombstone, subscription merges skip a
    matching name, and an explicit add clears matching patterns. TUI
    `ADDRESS-BLOCK`/`ADDRESS-UNBLOCK` plus Destinations keys `b`/`u`; Android
    Address book exposes block, unblock and a blocked-name list. The storage
    mutation is atomic tmp+rename and does not delete key material.
  - Files: `libi2pd_client/AddressBook.*`, `libi2pd/TuiProtocol.h`,
    `daemon/TuiServerCommon.*`, `daemon/Daemon.cpp`, `tui/main.cpp`,
    Android address-book/JNI bridge, `tests/test-addressbook-admin.cpp`,
    `tests/test-tui-protocol.cpp`.
  - Validation: offline matching, persistence, merge-skip, add-clears-block and
    protocol tests prepared; CI/device runs pending.
- [ ] **[PARITY-013]** Preserve and edit I2PSnark metainfo; add Android
  remove-with-data.
  - Priority P1; Status implemented by static analysis; pending CI compilation
    and pending CI tests. `Torrent` retains bounded raw top-level keys other
    than announce/announce-list/info, so tracker and metadata rewrites keep
    comment, created-by, creation date, url-list, encoding and extension data.
    Comment and created-by editing require a paused torrent, validate UTF-8 and
    bounds, and persist through the existing atomic/no-follow torrent writer.
    TUI detail keys `e`/`E`; Android metadata dialog and a separate destructive
    remove-with-data action use the bounded control protocol.
  - Files: `libi2pd_client/Torrents.*`, `TorrentsTunnel.cpp`,
    `TorrentsControl.h`, `libi2pd/TuiProtocol.h`, `daemon/TuiServerCommon.cpp`,
    `tui/main.cpp`, Android `TorrentsFragment`/`TorrentControl`, tests.
  - Validation: metainfo-preservation/round-trip, TUI parser and Android builder
    regressions prepared; CI/device runs pending.
- [ ] **[PARITY-016]** Router security: startup operator blocklist and lookup admission.
  - Priority P1; Status Implemented / static; Pending CI compilation; Pending CI tests. New `libi2pd/Blocklist.{h,cpp}` parses the Java-compatible `blocklist.txt` (IPv4 address/range/CIDR 3..32, IPv6 literal with `;`, 44-char base64 router hash, `#` comments, optional `reason:` prefix) with hard line/file/entry caps, builds sorted/merged IPv4 ranges plus exact IPv6/hash sets, and publishes one immutable snapshot atomically before `NetDb::Start()` loads cached RouterInfos. Hostnames are skipped (no DNS), IPv6 ranges and GeoIP/country blocking are not implemented, and there is no reload watcher. Enforcement: SSU2/NTCP2 inbound admission, outbound peer creation, netDb store and router/floodfill selection, and `IsRouterBanned`. Runtime router bans are in-memory only (1024 entries, cleared on restart). NetDb lookup budget is now floodfill 14 / non-floodfill 3 per 120 s reply tunnel, and a non-floodfill drops both EXPL and ANY before the throttle is charged.
  - Files: `libi2pd/Blocklist.{h,cpp}`, `libi2pd/NetDb.{cpp,hpp}`, `libi2pd/Profiling.{cpp,h}`, `libi2pd/Transports.{cpp,h}`, `libi2pd/SSU2.cpp`, `libi2pd/SSU2OutOfSession.cpp`, `libi2pd/NTCP2.cpp`, `libi2pd/Config.cpp`, `build/CMakeLists.txt`, `daemon/UnixDaemon.cpp`, `contrib/i2pd.conf`, `tests/test-blocklist-{parse,enforce}.cpp`, `tests/test-netdb-stop-admission.cpp`.
  - Residual: go-away RouterInfo replies, floodfill store-count admission, stale-RouterInfo selection refresh and `router.floodfillParticipant` remain (`PARITY-020`); no live floodfill/peer run and no compiler/CTest run exists.
- [ ] **[PARITY-018]** Administration: subscription sources and router bans in the TUI.
  - Priority P1; Status Implemented / static; Pending CI compilation; Pending CI tests. New bounded read verbs `ADDRESSBOOK-SUBSCRIPTIONS [limit]` and `BLOCKLIST [limit]` plus typed writes `ADMIN ADDRESS-SUB-ADD/REMOVE <http(s)-url>` and `ADMIN NETDB-BAN/UNBAN <base64-hash>` on the daemon-owned queue. TUI Destinations keys `s`/`x` edit subscriptions and NetDb keys `b`/`u` ban/unban a router hash; the NetDb page shows blocklist counters. Subscription validation is repeated in the address book and `subscriptions.txt` is rewritten atomically; runtime bans never rewrite the blocklist file. Android has no typed bridge for these yet and its JNI admin allowlist stays closed.
  - Files: `libi2pd/TuiProtocol.h`, `daemon/TuiServerCommon.{h,cpp}`, `daemon/Daemon.cpp`, `tui/main.cpp`, `tests/test-tui-protocol.cpp`, `tests/check-control-security.cmake`, `docs/tui-ipc.md`.
  - Validation: parser/static-contract regressions prepared; no IPC, TUI, Android or CI run exists.
- [ ] **[SNARK-012]** Durable magnet/session queue recovery and advanced peer policy.
  - Priority P1; Status Pending. An unresolved magnet is not persisted: only a resolved `*.torrent` is scanned at startup, so its infohash/name/tracker hints are lost on restart. Required: a bounded, versioned local record with a stated migration/rejection rule (no peer-wire or metainfo change), startup re-add before announce, duplicate-add hint merging and owner-stable pause state. Also open: BEP 12 ordered tiers, unchoke/upload-slot rotation, endgame with CANCEL send, per-torrent/session bandwidth limits, per-file or per-piece recheck, and typed torrent result codes/truncation flags in the IPC snapshot.
  - Files: `TorrentsTunnel.*`, `TorrentsState.*`, `Torrents.*`, `TorrentsControl.*`, `TuiProtocol.h`, Android `TorrentControl`/`TorrentsFragment`.
  - Residual: no compiler/CTest/device/live peer run exists.

- [ ] **[SNARK-011]** Tracker announce parity and scheduling.
  - Priority P1; Status Implemented / static; Pending CI compilation; Pending CI tests. HTTP and BEP 15 announces now send whole-torrent `left` (phony 1 without metadata) and `event=completed` only after every piece is verified, while local Seeding/promotion stay selection-based. A tracker-advertised `interval` is floored at 15 minutes and raised to a larger `min interval`; failures use a short internal retry ladder capped at 30 minutes and a success resets it. Constants `MIN_TRACKER_ANNOUNCE_INTERVAL`/`MAX_TRACKER_FAILURE_BACKOFF`, `TrackerSchedule` and `Torrent::GetAnnounceProgress()`.
  - Files: `libi2pd_client/Torrents.{h,cpp}`, `libi2pd_client/TorrentsTunnel.cpp`, `tests/test-torrents-tracker-schedule.cpp`, `tests/test-torrents-priorities.cpp`, `tests/test-torrents-verify-parity.cpp`.
  - Residual: session up/down rate limits, uploader slots/rotation, durable magnets, persisted/blame-based peer bans and CANCEL sending remain open; no live tracker/peer or CI run exists.
- [x] **[PARITY-019]** Client-service parity: server-tunnel filters, proxy Digest, per-tunnel control.
  - Priority P1; Status Implemented / static; Pending CI compilation; Pending CI tests. The 2026-10-26 continuation added the bounded `filterDefinition` parser/evaluator (`default`, `explicit`, `file`, `record`, first match wins, mtime-cached file, 4096-entry counters), a pre-accept admission predicate on `StreamingDestination` with `limitAction=drop|reset`, `targetForPort.<port>` (443/22 reset without a mapping), `spoofedHost.<port>`, `rejectInproxy`/`rejectReferer`/`rejectUserAgents`, POST throttling (429), `enable`/`startOnLoad`, stable `name=`, per-tunnel `GetTunnelServices`/`StartTunnel`/`StopTunnel`, virtual handler teardown (stopped tunnels now close live connections), default-acceptor isolation and RFC 7616 Basic/Digest proxy authentication (MD5/SHA-256, nonce cache, failure delay). The HTTP proxy also rejects non-http(s) schemes, sanitizes proxied responses, escapes generated pages and never echoes an outproxy URL.
  - Files: `libi2pd_client/I2PTunnelAccessFilter.*`, `I2PTunnel.*`, `I2PService.*`, `ClientContext.*`, `HTTPProxy.*`, `SOCKS.*`, `libi2pd/Streaming.*`, `libi2pd/Config.cpp`, tests `test-i2ptunnel-{stop,filter,targets}`, `test-streaming-limitaction`, `test-socks-resolve`, `test-http-proxy-limits`, `test-proxy-auth`.
  - Residual: HTTP client keep-alive/pipelining, IRC `ircclient` type, UDP/torrent tunnels in the start/stop registry, `advancedsettings` and arbitrary config writers stay unsupported; no live proxy/tunnel/Java peer run exists.
- [ ] **[PARITY-021]** Application-service administration through typed TUI/Android verbs.
  - Priority P1; Status Implemented / static; Pending CI compilation; Pending CI tests. Adds `ADMIN TUNNEL-START/TUNNEL-STOP <name>` (outcome `started|stopped|already-running|already-stopped|unknown`, post-action `running`), `ADMIN ADDRESS-REPLACE` (plain `ADDRESS-ADD` now answers `exists` instead of re-pointing a curated name), a non-blocking `ADMIN RESEED` (`started|already-running|disabled`) with `netdb.reseed.*` phase/error/timestamps, `tunnel.service` rows in `SERVICES`, TUI services/netdb/destinations actions and Android JNI/screens for tunnel control, reseed status, add-vs-replace, subscriptions and NetDb ban/unban. All writes remain a closed typed set; no generic config writer and no web console.
  - Files: `libi2pd/TuiProtocol.h`, `daemon/{TuiServerCommon.*,Daemon.cpp}`, `tui/main.cpp`, `android/app/src/main/**`, `tests/test-tui-protocol.cpp`.
  - Residual: no tunnel restart/enable-disable/create/edit verb, no reseed cancel verb; the new TUI strings still need es/ru/zh-Hans catalog entries (`I18N-004`); no Gradle/device run exists.
- [ ] **[RS-107]** Cancellable reseed lifecycle and status.
  - Priority P0/P1; Status Implemented / static; Pending CI compilation; Pending CI tests. Reseed no longer deletes cached RouterInfos when a downloaded bundle is stale (stale entries are skipped and counted), runs as an owned `std::jthread` with stop-token cancellation and per-phase/per-host I/O deadlines, and exposes `RequestReseed()` (non-blocking, single-flight) plus a bounded `ReseedState` (phase/enabled/attempts/last error/timestamps). `reseed.disable`/`i2p.noReseed` are honoured; proxy credentials are redacted from every log and from the status; `netid` URL composition matches a real query parameter.
  - Files: `libi2pd/Reseed.{h,cpp}`, `libi2pd/NetDb.{hpp,cpp}`, `libi2pd/Config.cpp`, `daemon/{Daemon.cpp,I2PControl.cpp}`, tests `test-reseed-{outdated-bundle,lifecycle,redaction,netid-url}`.
  - Residual: daemon bootstrap still waits synchronously for the first reseed; no `Date`-header clock recovery or non-SU3 directory fallback; no live reseed server run.
- [ ] **[AB-106]** Address-book persistence, feed retraction and validation parity.
  - Priority P1; Status Implemented / static; Pending CI compilation; Pending CI tests. All address-book artifacts (index, hosts dump, identities, etags, deleted.txt, entry-source sidecar) use the atomic no-follow writer; `AddAddressChecked` distinguishes `Exists` from replace and `ADMIN ADDRESS-ADD` no longer re-points; `hosts.txt` `#!action=delete` retracts only names whose recorded origin matches the fetching subscription; destination length (516–616) and host-label rules match Java; HTTP 304 keeps the ETag and cadence; a bounded 32-entry/60 s negative cache avoids repeat lookups (b33 never cached).
  - Files: `libi2pd_client/AddressBook.{h,cpp}`, `daemon/{TuiServerCommon.*,Daemon.cpp}`, `android/app/src/main/**`, tests `test-addressbook-{actions,cache,persistence}`.
  - Residual: feed retraction relies on the `entry-sources.txt` sidecar (deleting it only disables retraction); all artifacts are now mode 0600; no live subscription/resolver run.
- [ ] **[SEC-029]** Auxiliary service hardening (BOB, I2PControl, NTP, UPnP).
  - Priority P0/P1; Status Implemented / static; Pending CI compilation; Pending CI tests. BOB is loopback/same-UID only, caps sessions at 16, bounds command lines at 4 KiB, parses ports with `from_chars` and gates `getkeys` behind `bob.allowgetkeys`; its handlers log no operands. The I2PControl AF_UNIX path is validated by type/owner before replacement, restricted to 0600, removed only while it is still the socket this process created, and requires a same-UID peer where credentials exist; JSON ids/strings are escaped, `API` is validated and the Java error objects are emitted; bandwidth values are strictly parsed and independent. NTP replies are validated (source, mode, leap, stratum, root delay/dispersion, origin) and a large offset needs three agreeing servers. UPnP uses a 3-hour renewable lease, replaces superseded mappings and refuses a same-host IGD.
  - Files: `libi2pd_client/BOB.cpp`, `daemon/I2PControl.{h,cpp}`, `daemon/I2PControlHandlers.{h,cpp}`, `daemon/UPnP.{h,cpp}`, `libi2pd/Timestamp.{h,cpp}`, `libi2pd/Config.cpp`, tests `test-i2pcontrol-{json,socket}`, `test-bob-command`, `test-upnp-mapping`, `test-ntp-timesync-lifecycle`.
  - Residual: BOB is a TCP listener so a per-UID check is only available where the kernel reports INET peer credentials; Windows has no local-socket peer credentials (a non-default password is required); UPnP status is not yet wired to the TUI/Android; no live IGD/NTP/BOB client run.
- [x] **[I18N-004]** Translate the native TUI, prompts, status and BitTorrent panels.
  - Priority P1; Status Implemented / static; Pending CI compilation; Pending CI tests. The three catalogs now carry 455 keys with identical key sets: status/NetDb/config/destinations/services/logs panels, table headers, torrent table and all five detail tabs, footer bars, prompts, status messages, HTTP proxy address-helper/error pages and the `autoconf_i2pd` interactive prompts. English remains the fallback. Help bodies and `--help` prose remain English (documented residual).
  - Files: `i18n/{Spanish,Russian,ChineseSimplified}.cpp`, `tui/main.cpp`, `libi2pd_client/HTTPProxy.cpp`, `tools/autoconf_i2pd.cpp`, `i18n/I18N.cpp`, `tests/test-i18n-locale.cpp`.
- [x] **[I18N-005]** Native catalog contract, plurals and localized confirmation tokens.
  - Priority P1; Status Implemented / static; Pending CI compilation; Pending CI tests. `i18n/check-i18n-catalogs.pl` (run by `i18n-static`) verifies es/ru/zh key parity, printf/placeholder and trailing-space parity, UTF-8/BOM/CRLF hygiene, plural-form arity (es 2, ru 3, zh 1), rejects translated config/protocol identifiers and requires every `tr`/`SetStatus`/`BeginPrompt` literal (including adjacent-literal concatenation) to exist in all catalogs. `SetStatus` and `BeginPrompt` translate at their boundary (a `"> "`-prefixed status was previously unreachable), `IsAffirmative` accepts `s/sí/д/да` to match the advertised `[s/N]`/`[д/Н]` hints, `TorrentStatusText` localizes wire status tokens for display only, and `BuildDetailGeneral` sizes its wrap budget from display columns. Plural tables cover nodes/buckets/torrents/ranges/addresses/records/days/threads/peers/connections/tunnels/attempts/samples/files; `test-i18n-locale` asserts real es/ru/zh values and Russian one/few/many.
  - Files: new `i18n/check-i18n-catalogs.pl`, `i18n/*.cpp`, `tui/main.cpp`, `tests/check-i18n.cmake`, `tests/test-i18n-locale.cpp`.
- [x] **[I18N-006]** Android plural, notification and error-catalog completion.
  - Priority P1; Status Implemented / static; Pending Gradle/AAPT2/device tests. Replaced `logs_summary_format`, `torrent_count` and `runtime_admin_reseed_status` strings with `<plurals>` in all four locales (ru one/few/many/other) and switched the call sites to `getQuantityString`. Hardcoded creation/validation/operation errors now come from `torrent_creation_failed_unknown`, `torrent_create_source_invalid` and `torrent_operation_failed_generic` through a typed `TorrentControl.UserMessageException`; the log row uses `log_row_description`; the notification channel has a translated name and description; the zh tracker label is no longer the English token; Russian router/reseed terminology is unified. `check-android-locales.pl` now validates plural arity/placeholders, rejects non-positional placeholders and walks app/test references; `LocaleResourceTest` asserts zh-TW/zh-Hant English fallback and Russian plural forms.
  - Files: `android/app/src/main/res/values*/strings.xml`, `RouterViewModel.java`, `TorrentControl.java`, `TorrentsFragment.java`, `LogsFragment.java`, `RuntimeAdminFragment.java`, `LogAdapter.java`, `RouterService.java`, `android/tools/check-android-locales.pl`, `LocaleResourceTest.java`.
- [ ] **[I18N-007]** Remaining native help and CLI prose.
  - Priority P2; Status Missing. The TUI Help bodies (81 lines) need re-wrapping before translation; `--help`/usage bodies and machine-readable tool output stay English by policy. `autoconf_i2pd` now uses the shared catalogs.
  - Files: `tui/main.cpp`, `libi2pd/Config.cpp`, `tools/*.cpp`, `i18n/*`.
- [ ] **[PARITY-020]** NetDb propagation parity.
  - Priority P2; Status Missing / partial. Deferred: go-away RouterInfo on store ack and lookup replies, non-floodfill published-LeaseSet answers, floodfill store-count admission, stale-RouterInfo selection refresh, `router.floodfillParticipant` alias, and reseed URL-list reconciliation. Reference `HandleFloodfillDatabaseStoreMessageJob`, `HandleDatabaseLookupMessageJob`, `KademliaNetworkDatabaseFacade`, `Reseeder.java`.
- [ ] **[SEC-028]** Stop subscription merges from re-pointing curated `.i2p` names.
  - Priority P0; Status Implemented / static; Pending CI compilation; Pending CI tests. An automatic merge keeps any destination that existed before the merge (Java `Daemon.java`), counts conflicts with a bounded in-memory counter and bounded warnings, and never writes a conflict file or binary format. Duplicate lines inside one source keep last-wins; explicit add/replace/remove, tombstones, `.i2p.alt`, `www.` fallback, caps and atomic writers are unchanged. Source administration (`GetSubscriptions`/`AddSubscription`/`RemoveSubscription`, `IsValidSubscriptionURL`) is wired to the TUI verbs; Android editing remains pending.
  - Files: `libi2pd_client/AddressBook.{h,cpp}`, `tests/test-addressbook-merge.cpp`, `tests/test-addressbook-admin.cpp`, `libi2pd/TuiProtocol.h`, `daemon/{TuiServerCommon.*,Daemon.cpp}`, `tui/main.cpp`.
  - Residual: signed subscription entries/`#!` actions and a persistent conflict book are not implemented; `https` subscription sources are accepted but the fetch path still uses a plain I2P stream (pre-existing); no CI run exists.
- [ ] **[PARITY-014]** Close the remaining 2.14.0 functional-parity backlog
  identified by the scoped audit.
  - Priority P1; Status Partially implemented / static; Pending CI compilation;
    Pending CI tests. Implemented statically in this increment: reseed
    `?netid=` URL composition and Java's two-server/100-router-info acceptance
    threshold (`AppendReseedNetId`, `MIN_RESEED_SERVERS`,
    `MIN_RESEED_ROUTER_INFOS`; `test-reseed-zip-bounds`), plus Streaming
    per-peer/total minute/hour/day admission windows
    (`StreamingConnectionThrottle`, `test-streaming-throttle`). The 2026-10-25
    audit split the remainder into PARITY-016 (blocklist/lookup admission),
    PARITY-018 (subscription/ban administration), SEC-028 (merge precedence),
    SNARK-011 (tracker parity), PARITY-019 (filters/Digest/per-tunnel control)
    and PARITY-020 (netDb propagation). PARITY-019 is now implemented
    statically: server-tunnel `filterDefinition` with the pre-accept seam and
    drop/reset actions, HTTP Digest, `targetForPort`/`spoofedHost`/`reject*`/
    POST throttling, `startOnLoad` and per-tunnel start/stop (PARITY-021).
    Still pending: Streaming `limitAction` reset/HTTP/custom (drop/reset are
    implemented; HTTP/custom are not); peer profile detail; torrent
    uploader/bandwidth scheduling; OpenBSD port `SYSCONFDIR`; Android D8/R8
    Java 25 dexing evidence; and disk-domain mutation in
    `TorrentsTunnel::InitTorrentFiles`. SAM 3.3 `LISTEN_PROTOCOL`/dispatch,
    OpenBSD `WANTLIB` and `i2pd_privsep` capability parity are corrected
    statically.
  - Ordered full-equivalence workstreams and acceptance criteria are in
    `docs/FEATURE-PARITY.md` §12. Each one is completed as a whole; no
    partial wire action is presented as equivalence.
  - Files: `libi2pd/Reseed.{h,cpp}`, `libi2pd/StreamingThrottle.h`,
    `libi2pd/Streaming.{h,cpp}`, `libi2pd/Destination.{h,cpp}`,
    `libi2pd/Datagram.{h,cpp}`, `libi2pd/Config.cpp`,
    `libi2pd_client/{SAM,HTTPProxy,SOCKS,ClientContext}.*`,
    `tests/test-reseed-zip-bounds.cpp`, `tests/test-streaming-throttle.cpp`,
    `tests/test-sam-subsessions.cpp`, `tests/test-proxy-auth.cpp`,
    `contrib/openbsd/port/Makefile`, `tests/check-packaging.cmake`;
    matrix `docs/FEATURE-PARITY.md` §10 and §12.
  - Validation: source inspection and `git diff --check` only; no candidate
    was compiled or executed.
- [ ] **[PARITY-008]** Validate every changed path through manual CI and
  controlled interoperability/device runs.
  - Priority P0; Status pending CI. Required: Linux/OpenBSD/LibreSSL/Windows
  compilation, CTest, sanitizers/fuzzers, Java 2.14.0 differential harness,
  Android Gradle/JNI/Lint/device and package/service tests.
  - No local build, test, workflow run, commit or push is authorized here.

## Client hardening continuation (2026-10-21)

- [ ] **[NET-026]** Rebase the comparison and interoperability evidence on I2P Java 2.14.0.
  - Priority P0; Status in implementation. Reference configuration/docs are implemented by static analysis: tag `i2p-2.14.0`, peeled commit `3c98503c5f14efe7bdd783528ba7f621f12d8081`, tag object `9db233ed7241599eba76f29b8c334eaaf3db2e87`. Current-target and historical-fixture checks use separate external checkouts; existing 2.13.0 bytes and source citations are preserved.
  - Pending implementation: full 2.13.0-to-2.14.0 subsystem delta review, new or rechecked provenance/vectors and remaining functional corrections. A scoped 2026-10-21 read-only review covered administration, address book, client services, torrent and selected router/tunnel candidates (PARITY-009..014); it does not complete the full matrix. Reference pinning alone does not complete that work.
  - Validation: official remote refs inspected; scripts/CMake/manual workflow edits pending CI tests. No source comparison against 2.14.0, Java execution, compilation or interoperability validation has been performed for this transition.

- [ ] **[SEC-015]** Bind address-book datagram lookup replies to the requested resolver and bound outstanding authority.
  - Priority P0; Status implemented by static analysis; the production lookup registry binds nonce, normalized name, authenticated resolver identity and monotonic creation time. Pending requests are capped at 256, expire after 120 seconds and reject duplicate requests/nonce collisions. Wrong senders and ports cannot consume a legitimate request; RNG failure is fail-closed and shutdown clears the registry.
  - Files: `libi2pd_client/AddressBook.cpp`, `libi2pd_client/AddressBook.h`, `tests/test-addressbook-case.cpp`, `tests/CMakeLists.txt`, `.github/workflows/build.yml`.
  - Validation: prepared resolver-authority, replay, collision, capacity, expiry and shutdown regressions; pending CI compilation and pending CI tests on Linux/OpenBSD/Windows, ASan+UBSan/TSan and controlled datagram exchange. Slow replies beyond the deadline are deliberately rejected. Broader subscription and destination lifecycle work remains open.

- [ ] **[SEC-017]** Fix peer-controlled family-signature fail-open.
  - Priority P0; Status implemented by static analysis; pending CI compilation/tests. A known family with a malformed/non-64-byte base64 signature is now rejected instead of returning true. Unknown families retain the existing “no local policy opinion” result.
  - Files: `libi2pd/Family.cpp`; regression requires a family key-injection seam or generated certificate fixture.
  - Validation: no Java family vector or ASan run yet.
- [ ] **[SEC-018]** Bound router garlic/DeliveryStatus payload reads.
  - Priority P1; Status implemented by static analysis; pending CI compilation/tests. `PostGarlicMessage` checks the declared length against `I2NP` payload length minus the four-byte prefix, requires the minimum clove size, and `PostDeliveryStatusMessage` rejects truncated messages before reading the token.
  - Files: `libi2pd/RouterContext.cpp`.
  - Validation: prepared malformed-length regression not yet added to a runnable test.
- [ ] **[SEC-019]** Harden Garlic clove walking.
  - Priority P1; Status implemented by static analysis; pending CI compilation/tests. Encrypted/destination/tunnel/router pointers are checked against remaining bytes before advancement, I2NP message lengths must include a complete header, allocation failures are handled, and the final clove trailer is bounded before pointer movement.
  - Files: `libi2pd/Garlic.cpp`.
  - Validation: clove-boundary and ASan tests remain pending.
- [ ] **[SEC-020]** Fail NTCP2 closed when SipHash provider setup fails.
  - Priority P1; Status implemented by static analysis; pending CI compilation/tests. `SetSipKeys` is now fallible, checks every EVP operation and avoids installing partially initialized MD contexts; both handshake directions terminate instead of proceeding with invalid contexts.
  - Files: `libi2pd/NTCP2.cpp`, `NTCP2.h`.
  - Validation: provider-failure and ASan tests remain pending.
- [ ] **[SEC-021]** Bound torrent piece/index and DHT empty-vector paths.
  - Priority P1; Status implemented by static analysis; pending CI compilation/tests. Peer `have`/choke/terminate paths check the torrent piece count before `GetPiece`, and empty DHT node vectors no longer construct a `string_view` from a possibly-null pointer.
  - Files: `libi2pd_client/Torrents.cpp`, `TorrentsDHT.cpp`.
  - Validation: fuzz/ASan/TSan torrent regressions remain pending.
- [ ] **[SEC-022]** Harden Android SAF/JNI exception and lifecycle paths.
  - Priority P0/P1; Status partially implemented by static analysis; pending CI compilation/device tests. SAF root/read/remove calls now stop before further JNI calls after a Java exception; `JniScalarCall` clears a pending exception; native daemon start is idempotent/refuses a stopping daemon; the torrent recheck action catches invalid IDs.
  - Files: `android/app/src/main/cpp/i2pd_android.cpp`, `DaemonAndroid.cpp`, `android/app/src/main/java/org/openi2pd/router/TorrentsFragment.java`.
  - Validation: CheckJNI/ASan device tests, service restart and SAF fault injection remain pending.
- [ ] **[SEC-023]** Harden IPC/TUI bounds and lifetime.
  - Priority P1; Status partially implemented by static analysis; pending CI compilation/tests. Torrent-detail comments are capped, admin-result messages carry an explicit length instead of an unbounded `strlen`, and rest-of-line arguments reject control bytes.
  - Files: `daemon/TuiServerCommon.*`, `daemon/Daemon.cpp`, `libi2pd/TuiProtocol.h`, `tests/test-tui-ipc-regression.cpp`, `tests/test-tui-protocol.cpp`.
  - Validation: TUI IPC/fuzz tests remain pending.
- [ ] **[SEC-024]** Fix offline-key chaining and reseed/HTTP truncation.
  - Priority P1; Status implemented by static analysis; pending CI compilation/tests. `CreateOfflineKeys` rejects an already-offline key; file-name reads require exact `gcount`; HTTP header values reject C0/DEL controls; malformed status/header regressions are prepared.
  - Files: `libi2pd/Identity.cpp`, `Reseed.cpp`, `HTTP.cpp`, `tests/test-http-res.cpp`.
  - Validation: ASan/MSan and SU3/HTTP malformed vectors remain pending.
- [ ] **[SEC-025]** Track remaining SSU2/RouterInfo/lifetime races found by the audit.
  - Priority P1; Status partially implemented by static analysis; pending CI compilation/tests. SEC-027 now closes the introducer-snapshot race, the Streaming destination/Stream teardown race and `TorrentFile` handle serialization. Still open: `SSU2Server` memory-pool lifetime across deferred session teardown, cross-thread `SSU2Session::m_Address`/`m_RemoteEndpoint` publication, `RouterInfo` address-slot copy-on-write, ratchet session/destination crypto-context serialization, raw datagram receiver captures, and the remaining SSU2 version-downgrade branch.
  - Files: `libi2pd/SSU2*`, `RouterInfo.*`, `Garlic.*`, `ECIESX25519AEADRatchetSession.*`, `Datagram.cpp`, `util.h`.
  - Validation: TSan/ASan and shutdown/receiver-lifetime tests remain mandatory.
- [ ] **[CONC-001]** Make the base `RunnableService` lifecycle safe from its own handler.
  - Priority P1; Status implemented by static analysis; pending CI compilation/tests. `StopIOService` no longer self-joins; a same-thread request leaves the joinable worker for a later external join, while destruction from the handler detaches instead of terminating. Start is idempotent.
  - Files: `libi2pd/util.*`, `tests/test-runnableservice-lifecycle.cpp`.
  - Validation: prepared self-stop regression; TSan/restart stress pending.
- [ ] **[CONC-002]** Make `Transports` stop/start and shutdown state atomic and ordered.
  - Priority P1; Status implemented by static analysis; pending CI compilation/tests. Running/online/bound flags are atomic, Start is idempotent, timer cancellation happens after joining the owner thread, peer/session refs are dropped while both server contexts are alive, and `m_Peers` is cleared under its mutex.
  - Files: `libi2pd/Transports.*`.
  - Validation: stop/start and TSan stress pending.
- [ ] **[CONC-003]** Restart `I2PControl` and shared daemon shutdown flags safely.
  - Priority P1; Status implemented by static analysis; pending CI compilation/tests. `I2PControlService` restarts its `io_context`, uses an atomic running flag and cannot double-start; daemon `running`, graceful interval and transit-restore state are atomic.
  - Files: `daemon/I2PControl.*`, `daemon/Daemon.h`, `daemon/UnixDaemon.cpp`.
  - Validation: restart/TSan/shutdown-latency tests pending.
- [ ] **[CONC-004]** Remove shared crypto scratch and family-map races.
  - Priority P1; Status implemented by static analysis; pending CI compilation/tests. Tunnel build decryption now returns a caller-owned `NoiseSymmetricState`; `Families` protects its key map with a shared mutex.
  - Files: `libi2pd/RouterContext.*`, `libi2pd/TransitTunnel.cpp`, `libi2pd/Family.*`.
  - Validation: parallel build-record and family reload tests pending.
- [ ] **[CONC-005]** Address the remaining lifecycle/ownership hotspots.
  - Priority P1; Status Partially implemented / static. Per-profile state and canonical publication are addressed by CONC-009. Raw `this` captures in `TorrentsTunnel` timers/posts, datagram callback cross-owner teardown contracts, RouterInfo address/buffer copy-on-write, profile-storage cross-process authority, `Piece`/`TorrentFile` disk-domain ownership, SSU2 address/endpoint publication, IPC worker cancellation and full Android service-owner integration remain open. CONC-011/CONC-012 and ANDROID-019 add bounded receiver, queue and service-owner corrections; runtime verification remains pending.
  - Files: `libi2pd_client/Torrents*`, `Datagram.*`, `libi2pd/RouterInfo.*`, `Profiling.*`, `libi2pd/SSU2*`, `privsep/**`, Android service/SAF.
  - Validation: TSan/ASan shutdown, receiver-lifetime, profile, disk-domain and device tests remain mandatory.

- [ ] **[CONC-009]** Synchronize RouterProfile state and canonical cache publication.
  - Priority P1; Status Implemented / static; Pending CI compilation; Pending CI tests. `Profiling.h/.cpp` synchronize mutable profile state, return endpoint copies and the winning cache instance, serialize per-profile Save/Load, atomically claim persistence and retain dirty state after failed writes. Cache eviction requires map-only shared ownership; postponed queue inspection is locked. `test-router-profile-race` prepares concurrent mutation/publication/queue and persistence-claim checks for the full manual CTest/TSan matrix.
  - Residual: CONC-010 coordinates process-local storage and final queue draining; cross-process authority, crash-left staging cleanup and external producer quiescence remain open.

- [ ] **[CONC-010]** Coordinate profile storage authority, replacement and shutdown persistence.
  - Priority P1; Status Implemented / static; Pending CI compilation; Pending CI tests. Added immutable initialized root authority, serialized profile Save/Load/cleanup, same-directory flushed replacement and draining of postponed updates before final shutdown persistence. Files: `Profiling.*`, `FS.*`, `NetDb.cpp`, `tests/test-router-profile-storage.cpp`. Keep INI names/schema unchanged; prepare isolated temporary-root and failure-path regressions without local execution. Root changes require process restart. Concurrent processes sharing a root are unsupported (Windows/Android ownership enforcement remains open). Cross-process exclusivity, crash-left staging cleanup, kernel/filesystem durability and external producer quiescence remain open; no global separation claim.

- [ ] **[SEC-016]** Remove unsafe C string formatting/concatenation and resolve Boost.Asio scope-ID diagnostics.
  - Priority P1; Status implemented by static analysis; `AddressFormat.h` provides bounded IPv4/IPv6/endpoint formatting with scope IDs, `LogPrint` no longer instantiates Boost's `sprintf`/`strcat` path, `util.cpp` uses bounded `CopyString`, `I18N` uses a checked single-buffer formatter, SOCKS domain buffers reject zero-length/overflow, and `Family`/`Log`/torrent hex formatting handle truncation explicitly. `BOOST_ASIO_HAS_SNPRINTF=1` selects Asio's bounded `snprintf` fallback for scope IDs, while project formatting avoids its remaining `strcat` path; no `strlcpy`/`strlcat` fallback is used because the supported platforms guarantee the selected primitives.
  - Files: `libi2pd/AddressFormat.h`, `libi2pd/Log.*`, `libi2pd/util.*`, `libi2pd/Family.cpp`, `libi2pd/Reseed.cpp`, `i18n/I18N.h`, `libi2pd_client/SOCKS.cpp`, `libi2pd_client/SOCKSAddress.h`, `libi2pd_client/Torrents.cpp`, `build/CMakeLists.txt`, `tests/test-address-format.cpp`, `tests/test-util-copy-string.cpp`, `tests/test-i18n-format.cpp`, `tests/test-socks-address.cpp`, `tests/check-build-system.cmake`, `docs/security-audit.md`, `docs/testing.md`, `docs/dependencies.md`, `docs/build-system.md`, `docs/files-changed.md`.
  - Validation: source/interface inspection and `git diff --check` only; Linux/OpenBSD/Windows/Android compilation, CTest, ASan/UBSan/TSan, fuzzing and manual CI remain pending. No local build, test or validator was run.

## Static lifecycle and maintenance continuation (2026-10-22)

- [ ] **[CONC-011]** Give Datagram receivers identity-bearing teardown and in-flight accounting.
  - Priority P1; Status Implemented / static; Pending CI compilation; Pending CI tests. Replaced port-only reset with registration tokens across Datagram, UDP tunnels, SAM, address book and torrent callers. External reset deactivates and drains in-flight invocations outside registry locks; callback-context reset only deactivates and returns false, so owners must defer destruction. Shared destination ownership is required before registration. Replaced callbacks are released outside registry locks. `test-datagram-receiver-lifecycle` prepares identity/fallback, drain, same/cross-slot reset and destructor-reentry coverage. Wire bytes and historical wildcard fallback remain unchanged; embedding teardown and per-caller sanitizer evidence remain open.
- [ ] **[CONC-012]** Close tunnel/netDb message admission during shutdown and drop stopped-generation queues.
  - Priority P1; Status Implemented / static; Pending CI compilation; Pending CI tests. `Tunnel.*` and `NetDb.*` coordinate admission and enqueue under a short mutex, serialize lifecycle transitions and drain after joining. NetDb also clears both in-memory throttle maps under their mutex. `test-tunnels-restart` and `test-netdb-stop-admission` prepare accepted/rejected posts, concurrent closing/drain and throttle-reset coverage; the NetDb fixture opens admission with the worker paused and does not exercise full Start. Producers must be quiesced across restart; messages do not carry epochs.
- [ ] **[ANDROID-019]** Fence stale RouterService callbacks and order native teardown across owners.
  - Priority P1; Status Implemented / static; Pending CI compilation; Pending CI tests. `ServiceOwnerEpoch` gates all RouterService worker submissions, fences stale listener snapshots and orders accepted old work, mandatory native stop and new-owner work on one process FIFO. Retired submissions reject through existing failure callbacks; stale retirement cannot enqueue a second native stop. JNI stays off the main thread/epoch monitor. `ServiceOwnerEpochTest` prepares latch-based ordering and stale-owner regressions; platform lifecycle/device evidence remains pending.
- [ ] **[TOOL-013]** Preserve maintenance-script error status and path capture.
  - Priority P2; Status Implemented / static; Pending CI tests. Static package root discovery clears CDPATH; SAM version mismatches now return failure. The reported explicit-undef and ShellCheck fixes were already present in HEAD. Manual packaging CI now ShellChecks the six reported maintenance scripts and uses a fake `nc` to check SAM mismatch failure without a live router. No local scripts/linters ran.

- [x] **[TOOL-014]** One destination-key load/store authority.
  - Priority P1; Status Implemented / static; Pending CI compilation; Pending CI tests. New `tools/common/keys.hpp` owns `LoadPrivateKeys` (exact buffer consumption), `SecureBuffer` (wipe on every exit, non-copyable) and `SavePrivateKeys` over the core durable `fs::AtomicWritePrivate` instead of `O_TRUNC`. `keygen`, `offlinekeys`, `b33offlinekeys`, `vain`, `keyinfo`, `regaddr`, `regaddralias`, `regaddr_3ld` and `famtool` use it; the on-disk bytes and 0600 mode are unchanged. `offlinekeys`/`b33offlinekeys` refuse an output that names the input. `autoconf_i2pd` publishes its config through `fs::AtomicWrite` and rejects a NUL in the file name.
  - Files: new `tools/common/keys.hpp`, `tools/{keygen,offlinekeys,b33offlinekeys,vain,keyinfo,regaddr,regaddralias,regaddr_3ld,famtool,autoconf_i2pd}.cpp`.
  - Residual: `famtool` still accepts the historical unused `-o`; per-document/Windows DACL equivalence is not device-tested.
- [x] **[TOOL-015]** Parser, stream and error contracts.
  - Priority P1; Status Implemented / static; Pending CI compilation; Pending CI tests. `NameToSigType`/days parsing share `ParseBounded`; `keygen` help exits 0 and a leading-dash filename is rejected; `keyinfo -b` on a non-blindable destination is exit 3 on stderr (not a successful partial listing); `famtool` requires a mode, validates the family alphabet and turns a throwing `RouterInfo` constructor into a value; `regaddr*` validate the record name and use `CryptoGuard`; `regaddr_3ld` trims CR/LF and rejects a multi-line step file; `b33address` reads with a hard cap and rejects NUL/control, with progress on stderr; `i2pbase64` treats padding as terminal even across a 4 KiB chunk boundary; `x25519` uses the core `X25519Keys`.
  - Files: `tools/common/key.hpp`, `tools/{keygen,keyinfo,famtool,regaddr,regaddralias,regaddr_3ld,b33address,i2pbase64,x25519}.cpp`.
  - Residual: usage/errors are not yet on stderr in every remaining tool; `Signer::Sign` is still `void` (CRY-236); no Windows CLI run.
- [x] **[TOOL-016]** Reuse core crypto and C++23 helpers.
  - Priority P1; Status Implemented / static; Pending CI compilation; Pending CI tests. The five local `CryptoGuard` copies are replaced by `common/crypto.hpp` (including `b33address`); `x25519` drops its EVP keygen/deleter code for `i2p::crypto::X25519Keys`; `famtool`/`routerinfo`/`vain`/`verifyhost` use the shared guard; `SecureBuffer` replaces four wipe idioms; `ParseBounded` replaces three `from_chars` blocks.
  - Files: `tools/common/{crypto.hpp,key.hpp,keys.hpp}`, `tools/{b33address,x25519,famtool,routerinfo,vain,verifyhost}.cpp`.
  - Residual: `firewall.hpp` still has a bespoke reserved-range table because the core policy rejects documentation ranges used by fixtures; `vanity.hpp` macros and the `RouterInfo` throwing constructor remain.
- [x] **[TOOL-017]** CMake inventory, packaging parity and new CTest lanes.
  - Priority P1; Status Implemented / static; Pending CI compilation; Pending CI tests. Tools no longer link `libi2pdclient` (no tool includes a client header); only `autoconf_i2pd` links `libi2pdlang`; the getopt shim is a Windows-only source and has a host regression; install is one aggregate `install(TARGETS ...)`; test registration is skipped when cross-compiling. New `tests/check-tools-inventory.cmake` compares the CMake list with docs navigation, Debian/RPM packaging and the manual install assertion; `tools-cli` checks exact exit codes/streams/same-file refusal/CRLF records; `tools-getopt` exercises the shim on the host.
  - Files: `tools/CMakeLists.txt`, `tools/test-tools-cli.cmake`, `tools/tests/test-tools-getopt.cpp`, `tests/check-tools-inventory.cmake`, `tests/CMakeLists.txt`, `.github/workflows/build.yml`, `contrib/upstream/tools-files.txt`.
  - Residual: no MSVC tools lane; man pages for the 15 tools remain absent; RPM still hand-installs binaries instead of using the component.

## Static compile/link audit (2026-10-25)

- [ ] **[TEST-037]** Continue the exhaustive static compile/link audit after the 2026-10-26 integration wave.
  - Priority P0; Status Implemented / static; Pending CI compilation; Pending CI tests. Parallel reconnaissance covered core C++23, transports/crypto/protocols, privsep, torrents/TUI/tools, Android/JNI/resources and CMake/platform linkage. Confirmed corrections are `RouterStorage.cpp`'s missing RAII `Release()` and `Message`/`ReceivedMessage` mismatch, plus direct standard-library includes in public crypto/transport/streaming/torrent headers and affected translation units. Android's 43 native declarations, AIDL v2 surface and resource references were consistent by inspection and required no source correction. The 2026-10-26 follow-up added the `imsg_get`/`imsgbuf_get` probe, `_GNU_SOURCE`, `OPENSSL_PQ 0`, the SipHash/NTCP2 and SSU2 null guards, the header-include closure, the tools CMake-test escapes and the Android status/reseed/resource fixes.
  - Files: `privsep/RouterStorage.cpp`, `libi2pd/{Crypto,NTCP2,PostQuantum,SSU2,Streaming,Tag,TunnelEndpoint}.h`, `libi2pd_client/{I2PTunnel,Torrents,TorrentsRPC,TorrentsTunnel}.h`, `libi2pd_client/Torrents*.cpp`, `tui/main.cpp`; latest pass: `build/CMakeLists.txt`, `privsep/{Channel,OpenBSDImsg,Supervisor,WindowsIpc}.cpp`, `libi2pd/{Crypto,Queue,Gost,Socks5,Transports,TransportSession,NetDbRequests,Blinding,util}.h`, `libi2pd/{Config,IdentMetrics,NTCP2,SSU2Session}.cpp`, `libi2pd_client/{BOB,TorrentsDHT,SOCKS,ClientContext,MatchedDestination,TorrentPayloadStorage}`, Android status/DaemonWrapper/reseed/resources, `tests/check-build-system.cmake`, `docs/static-build-audit.md`.
  - Validation: source/interface inspection and `git diff --check` only. Linux GCC/Clang, OpenBSD/LibreSSL, OpenSSL 3/4, Windows/MSVC, Android/NDK, `WITH_TORRENTS=ON/OFF`, TUI and storage-worker compilation/tests remain pending manual CI. No compiler, configure, CTest, Gradle, project validator, workflow, commit or push ran.

- [ ] **[TEST-024]** Anticipate compile/link failures across subsystems without local builds.
  - Priority P0; Status Implemented / static; Pending CI compilation; Pending CI tests. Six read-only subagents covered core C++23, protocols/crypto/netDb/client, privsep/daemon/IPC, torrent/TUI/tools, Android Java/JNI/resources and CMake/linking. Confirmed defects corrected: `daemon/I2PControl.cpp` namespace brace and `steady_timer::cancel()` arity, missing direct `<cstdio>`/`Crypto.h`/OpenSSL includes, `ip::port_type` above the Boost 1.74 floor, dead `NetDb::ManageRequests` and UDP client cleanup declarations, Windows `CheckLanguage(RC)`/`version.h` include, `CURSES_LIBRARY` wide-entry probe, `I2PD_HAVE_SSL_SET1_DNSNAME`/`I2PD_HAVE_OPENSSL_MLKEM_H` probes with `Crypto.h` fallbacks, `I2PD_INSTALL_CONFIG`, and APK reseed-certificate evidence.
  - Warning fixes: `str_rmatch` removal, PQ `token` scope, `MAX_RESEED_ENTRIES` entry bound, unused `deny` capture, the deprecated `SSL_set1_host` branch, the Windows rollback-lambda capture, Windows `closeSyslog`, and the JNI `jsize`/`size_t` comparison.
  - Torrent/lifetime fixes: administrative posts capture `weak_from_this().lock()`, the no-destination shutdown stops owned storage workers, and generated metainfo values use a stable `std::deque`. TUI retries UTF-8 `LC_CTYPE` and undefines `KEY_F1..KEY_F12`.
  - Files: `daemon/{I2PControl.cpp,Daemon.cpp,UPnP.h}`, `libi2pd/{Crypto.h,PostQuantum.h,Reseed.cpp,RouterInfo.cpp,NetDb.hpp,Log.cpp}`, `libi2pd_client/{HTTPProxy.cpp,UDPTunnel.h,Torrents.cpp,TorrentsTunnel.cpp}`, `privsep/LinuxSandbox.cpp`, `tui/{main.cpp,TerminalUnix.cpp}`, `android/app/src/main/cpp/i2pd_android.cpp`, `build/CMakeLists.txt`, `tests/{check-build-system.cmake,check-packaging.cmake}`, `contrib/openbsd/port/Makefile`, `.github/workflows/build.yml`, `docs/build-system.md`, `docs/static-build-audit.md`.
  - Residual: `I2PControlService::Stop()` caller-thread socket close (deadline timers now cancelled), `TorrentsTunnel::InitTorrentFiles` disk-domain mutation, OpenBSD port `WANTLIB`/`CMAKE_INSTALL_SYSCONFDIR`, `i2pd_privsep` macro parity, Android D8/R8 dexing and `java25-probe`, and the PARITY-014 feature backlog.
  - Follow-up static corrections: throttle mutex; `Tag` call-operator in the throttle regression; SAM `LISTEN_PROTOCOL` on single RAW sessions; escaped `\${I2PD_CAPABILITY_DEFINITIONS}` marker and late success message in `build-system-static`; command-line rejection and buffer cleansing for proxy credentials; AGP-resolved R8 probe and universal/arm64 APK selection.
  - Validation: source/interface inspection and `git diff --check` only. No compiler, CMake configure, CTest, Gradle, validator, workflow dispatch, commit or push ran.

## Tunnel, I2NP and garlic continuity (2026-10-26, static)

Read-only audits of tunnel construction, tunnel pools/timers, I2NP dispatch and
garlic/ratchet state against I2P Java 2.14.0 preceded these rows. Everything
below is Implemented / static, Pending CI compilation and Pending CI tests; no
compiler, CTest, sanitizer, fuzzer, Java peer, live tunnel or workflow run
exists.

- [ ] **[TU-107]** Bound the tunnel build record table and fail the build closed.
  - Priority P0; Status Implemented / static; Pending CI compilation; Pending CI tests. `PlanTunnelBuildRecords()` is the single authority for the hop/record mapping: a path longer than `MAX_NUM_RECORDS`, or an inbound path that cannot also hold the phony record, fails with `eTunnelStateBuildFailed` before a byte is written (this closed a heap out-of-bounds write indexed by `recordIndicies[]`). `ClampTunnelLength()` clamps the configured hops and variance in the constructor, in `Reconfigure()` and in selection (inbound reserves the phony slot); `CreateBuildRequestRecord()` now takes the record count, validates `recordIndex`, returns `bool` and zeroes the whole record on any failure.
  - Files: `libi2pd/TunnelBase.h`, `libi2pd/Tunnel.{h,cpp}`, `libi2pd/TunnelConfig.{h,cpp}`, `libi2pd/TunnelPool.{h,cpp}`, `tests/test-tunnel-build-records.cpp`.
- [ ] **[TU-108]** Fail-closed build-record encryption and exact build replies.
  - Priority P1; Status Implemented / static; Pending CI compilation; Pending CI tests. `EncryptECIES()`/`CreateBuildRequestRecord()` return failure when the provider cannot produce an ephemeral key, when the X25519 agreement fails or when AEAD encryption fails; the clear-text secret is cleansed and the entire record is zeroed so no partial record is relayed. The phony record also refuses to name a missing router identity or a keyless ephemeral pair. The creator records the advertised record count and rejects a reply with a different count or any trailing/short byte.
  - Files: `libi2pd/TunnelConfig.{h,cpp}`, `libi2pd/Tunnel.{h,cpp}`, `tests/test-tunnel-build-records.cpp`.
- [ ] **[TU-109]** Transit build replay filter, build expirations and exact build sizes.
  - Priority P1; Status Implemented / static; Pending CI compilation; Pending CI tests. Short and variable transit build handlers require an exact payload size, derive the per-request reply key before tunnel creation and reject a repeat inside a bounded 8-minute/4096-entry decaying window (keys are never logged). Relayed requests carry Java's ~25 s expiration and replies ~60 s instead of the generic 8 s. A failed `CreateI2NPMessage()` is no longer relayed or replied to. The filter is per `TransitTunnels` instance and cleared on `Stop()`.
  - Files: `libi2pd/TransitTunnel.{h,cpp}`, `tests/test-transit-tunnel-build-replay.cpp`.
- [ ] **[TU-110]** Peer admission parity for fresh selection and tunnel reuse.
  - Priority P1; Status Implemented / static; Pending CI compilation; Pending CI tests. One `IsRouterUsableForTunnel()` predicate now covers blocklist, runtime bans, revocation/unreachability, missing transports, the bad-profile rule (non-creating lookup), the version floor, congestion, ECIES identity and the endpoint/gateway family rules; fresh selection, explicit peers and reused paths all call it, and a recreated path is refused when the old tunnel failed or already carries a test strike. `ValidatePeers()` takes the direction so the role matches.
  - Files: `libi2pd/TunnelPool.{h,cpp}`, `libi2pd_client/MatchedDestination.cpp`, `tests/test-tunnelpool-reuse-policy.cpp`.
- [ ] **[TU-111]** Tunnel pool build accounting, backoff ladder and timed tests.
  - Priority P1; Status Implemented / static; Pending CI compilation; Pending CI tests. In-flight builds count against the pool target, consecutive failures reduce the next round (3 → 2 at four failures, → 1 at eight) and raise the delay between rounds from 10 s to a 240 s cap, reset on success. Tunnel tests persist across management ticks with a per-hop deadline (7.5–15 s), fail only on the third consecutive strike, keep the strike counter independent of the tunnel state so inbound traffic cannot resurrect a test-failed tunnel, and skip testing an inbound tunnel that carried traffic within 40 s. `TestFailed` tunnels are never advertised in a LeaseSet.
  - Files: `libi2pd/TunnelPool.{h,cpp}`, `libi2pd/Tunnel.{h,cpp}`, `libi2pd/TunnelEndpoint.{h,cpp}`, `tests/test-tunnelpool-build-backoff.cpp`, `tests/test-tunnelpool-test-timing.cpp`, `tests/test-tunnelpool-zero-hop.cpp`.
- [ ] **[TU-112]** Tunnel selector lifetime, far-end masks and bounded queues.
  - Priority P1; Status Implemented / static; Pending CI compilation; Pending CI tests. The custom peer selector is stored as a `TunnelPeerSelectorHandle` (pointer plus weak owner); `TrySelectPeers()` pins the owner for the whole call outside the pool mutex, which removed a use-after-free of a released selector. The three fallback builders pass the peer's real `CompatibleTransports` mask instead of a zero mask from a `false` argument. The inbound tunnel queue is bounded (`PutIfBelow`, 8192) with a drop counter, exact 1028-byte tunnel data messages are required at the endpoint/transit handlers, fragment reassembly keeps Java's 45 s window and a duplicate fragment below `nextFragmentNum` is dropped rather than stored. The latent `GetLowestLatency*Tunnel(exclude)` and first-hop fallback bugs are fixed.
  - Files: `libi2pd/Tunnel.{h,cpp}`, `libi2pd/TunnelPool.{h,cpp}`, `libi2pd/TunnelBase.h`, `libi2pd/TunnelEndpoint.{h,cpp}`, `libi2pd/TransitTunnel.{h,cpp}`, `libi2pd_client/MatchedDestination.{h,cpp}`, `tests/test-tunnel-peer-selector-lifetime.cpp`, `tests/test-tunnels-inbound-queue.cpp`, `tests/test-tunnel-endpoint-fragments.cpp`.
- [ ] **[I2NP-011]** Apply the Java 2.14.0 dispatcher window and duplicate suppression.
  - Priority P1; Status Implemented / static; Pending CI compilation; Pending CI tests. `HandleI2NPMessage()` drops a message whose expiration is older than 90 s or further than 240 s in the future before any handler runs (`TunnelData` keeps its endpoint/IV policy), then applies a bounded two-generation duplicate filter keyed by message ID, expiration second and a router salt (`DecayingHashSet` TIME_MASK form). Cloves are excluded because each destination has its own clove filter with a different salt. Drops are counted and logged at a throttled rate.
  - Files: `libi2pd/I2NPProtocol.{h,cpp}`, `tests/test-i2np-dispatch-policy.cpp`, `tests/fuzz/fuzz-i2np-policy.cpp`.
- [ ] **[I2NP-012]** Strict I2NP allocation, DeliveryStatus and DataMessage lengths.
  - Priority P2; Status Implemented / static; Pending CI compilation; Pending CI tests. `CreateI2NPMessage()` returns `nullptr` on overflow instead of a header-less message with a stale length; every network-derived caller checks it. `DeliveryStatus` requires exactly 12 payload bytes and a `DataMessage` requires its declared length to cover the whole payload (no ignored trailing bytes). Parse-error logging on the tunnel/I2NP/garlic paths is throttled so a hostile peer cannot flood the log.
  - Files: `libi2pd/I2NPProtocol.{h,cpp}`, `libi2pd/Destination.{h,cpp}`, `libi2pd/RouterContext.cpp`, `tests/test-i2np-dispatch-policy.cpp`.
- [ ] **[GAR-005]** Garlic session serialization, bounded clove queue and clove grammar.
  - Priority P1; Status Implemented / static; Pending CI compilation; Pending CI tests. `GarlicRoutingSession` state (`m_StateMutex`) serializes wrapping, lease-set status, tag/ACK state and shared paths; the provider `HKDFContext` is per-thread so concurrent tag generation cannot share derivation state. `GarlicDestination::CleanUp()` swaps every map under its own lock and destroys sessions outside them. The destination garlic queue is bounded (256, oldest dropped and counted). A clove payload must announce 1..32 cloves, a stale or foreign clove is skipped while the walk continues, a replayed clove is dropped by a per-destination filter, the tunnel-only allowlist applies only to `fromTunnel` garlic, the ElGamal tag budget no longer discards the payload, `DESTINATION` cloves are hash-checked and ROUTER-mode ratchet cloves are explicitly rejected. `CleanupExpiredTags()`/`SetLeaseSetUpdated()` no longer call session methods while holding `m_SessionsMutex`.
  - Files: `libi2pd/Garlic.{h,cpp}`, `libi2pd/ECIESX25519AEADRatchetSession.{h,cpp}`, `libi2pd/Destination.{h,cpp}`, `libi2pd/RouterContext.cpp`, `tests/test-garlic-clove-policy.cpp`.
- [ ] **[CONC-014]** Remove the destination-session lock inversion and stop cross-thread raw state reads.
  - Priority P1; Status Implemented / static; Pending CI compilation; Pending CI tests. Session methods take the session state lock; a session that registers itself with its destination takes that state lock before `m_SessionsMutex`. `CleanupExpiredTags()` and `SetLeaseSetUpdated()` therefore snapshot the maps and call the sessions (and erase/terminate) outside the map lock, with an identity re-check before erasing. `GetRemoteStaticKey*`, `IsReadyToSend()` and `IsResponseRequired()` are state-locked. `IsTerminated()`/`IsExhausted()` stay atomic so the tag-budget path does not take the state lock. This is a static analysis; no TSan run exists.
  - Files: `libi2pd/Garlic.cpp`, `libi2pd/ECIESX25519AEADRatchetSession.h`, `tests/test-garlic-clove-policy.cpp`.

## SAM, I2CP, streaming and datagram continuity (2026-10-26, static)

Four read-only audits (SAM bridge, I2CP client, streaming state machine,
Datagram1/2/3) against I2P Java 2.14.0 preceded these rows. Everything below is
Implemented / static, Pending CI compilation and Pending CI tests; no compiler,
CTest, sanitizer, fuzzer, Java peer, SAM client or device run exists.

- [ ] **[SAM-122]** Separate the SAM listen port from the local forward port.
  - Priority P1 (interop); Status Implemented / static. `PORT` now only names the local UDP forward endpoint; the I2CP receive port comes from `LISTEN_PORT` and defaults to 0 (PORT_ANY) for a v3 single session, as Java `SAMv3RawSession/SAMv3DatagramSession` do. Previously `SESSION CREATE STYLE=RAW PORT=7666` registered the receiver on I2CP port 7666, so a peer sending to port 0 was silently dropped (`RAW RECEIVED` never fired).
  - Files: `libi2pd_client/SAM.{h,cpp}`, `tests/test-sam-subsessions.cpp`.
- [ ] **[SAM-123]** Bounded SAM control plane, deadlines and secret redaction.
  - Priority P1 (security). `SAM_MAX_CONTROL_CONNECTIONS` (64) with exactly-once release; 60 s HELLO deadline; after HELLO the socket is not closed for idleness (Java calls `setSoTimeout(0)`), only a partially received command line is bounded (180 s). The SESSION CREATE line (which carries `DESTINATION=<private keys>`), the HELLO line (`USER`/`PASSWORD`) and datagram payloads are never logged; AUTH and malformed HELLO now get `RESULT=I2P_ERROR` replies instead of a silent close.
  - Files: `libi2pd_client/SAM.{h,cpp}`, `tests/test-sam-control-framing.cpp`, `tests/check-control-security.cmake`.
- [ ] **[SAM-124]** SAM 3.2/3.3 command semantics and error codes.
  - Priority P1 (interop). `STREAM ACCEPT`/`STREAM FORWARD` are mutually exclusive per listen port (`RESULT=ALREADY_ACCEPTING`); FORWARD is port-scoped; missing ID is `I2P_ERROR "ID not specified"`; connect timeout is `RESULT=TIMEOUT`; duplicate parameter keys close the connection; bare `SILENT`/`HEADER`/`KEY` mean true; SEND on an unknown/wrong-type session closes like `SAMv1Handler`; a malformed HELLO gets `HELLO REPLY RESULT=I2P_ERROR MESSAGE=…` while an unparsable MIN/MAX bound is `NOVERSION` (Java's `chooseBestVersion` returns null).
  - Files: `libi2pd_client/SAM.{h,cpp}`, `tests/test-sam-control-framing.cpp`, `tests/test-sam-version.sh`.
- [ ] **[SAM-125]** Per-style listen protocol and per-send datagram framing.
  - Priority P1 (interop). The 2.14.0 subsession key is `(STYLE, listenProtocol, LISTEN_PORT)` with 17/19/20 for DATAGRAM/DATAGRAM2/DATAGRAM3, so the three versions may share one port; receivers are registered under that protocol (identity for 17/19, sender-hash for 20). RAW `LISTEN_PROTOCOL` rejects 6/17/19/20. Sends pass the subsession version so a DATAGRAM3 socket never emits Datagram1 framing.
  - Files: `libi2pd_client/SAM.{h,cpp}`, `libi2pd/Datagram.{h,cpp}`, `tests/test-sam-subsessions.cpp`, `tests/test-datagram-version-dispatch.cpp`.
- [ ] **[SAM-126]** Re-arm queued ACCEPTs and protocol-aware teardown.
  - Priority P1 (liveness). Terminating the armed acceptor (or a FORWARD socket) re-arms the oldest queued ACCEPT for the same port instead of stranding it; a queued ACCEPT is never armed on another port's streaming destination.
  - Files: `libi2pd_client/SAM.{h,cpp}`, `libi2pd/Streaming.{h,cpp}`.
- [ ] **[I2CP-112]** Resolve `.b33.i2p` and implicit long-label names.
  - Priority P1 (interop). `AddressBook::GetAddress` now strips an explicit `.b33.i2p`/`.b33.i2p.alt` suffix or treats a final label longer than `B33_ADDRESS_THRESHOLD` (and at most `B33_LABEL_MAX_LENGTH`) as a blinded destination; invalid base32, reserved flags, unblindable types and short labels behind the suffix fail instead of falling through to base32/base64 or the `www.` alias.
  - Files: `libi2pd_client/AddressBook.{h,cpp}`, `tests/test-i2cp-b33-lookup.cpp`.
- [ ] **[I2CP-113]** Consume BlindingInfo (type 42) client-auth material.
  - Priority P1 (interop). The registry now has a bounded, pruned reader keyed by the unblinded signing key and a blinded-key lookup; the HostLookup path passes the 32-byte DH private key or PSK to the new `RequestDestinationWithEncryptedLeaseSet(blinded, complete, secret, len)` overload, which prefers it over the session `i2cp.leaseSetPrivKey`. Keys and secrets are never logged.
  - Files: `libi2pd_client/I2CP.{h,cpp}`, `libi2pd/Destination.{h,cpp}`, `tests/test-i2cp-blinding-info.cpp`.
- [ ] **[I2CP-114]** Java HostReply result codes.
  - Priority P1 (interop). `SendHostReplyMessage` carries a result code: 2/3/4 (secret/key/both required) when the BlindingInfo announces auth but no usable material exists, 5 (decryption failure) on a store-key/decryption mismatch, 6 (leaseset lookup failure) on timeout/no LeaseSet, 1 (failure) on malformed input. Success remains byte-identical.
  - Files: `libi2pd_client/I2CP.{h,cpp}`, `tests/test-i2cp-blinding-info.cpp`.
- [ ] **[I2CP-115]** I2CP control-plane admission and read deadlines.
  - Priority P1 (security). `i2cp.maxconnections` (default 50, hard 2048) and `i2cp.maxsessions` (default 512, hard 4096) with clean refusal; a connection slot is released exactly once; the protocol-byte, header and body reads are asynchronous with cancellable 30 s deadlines, and the synchronous `available()`/`read()` fast path is gone. Idle established connections are not killed.
  - Files: `libi2pd_client/I2CP.{h,cpp}`, `libi2pd/Config.cpp`, `tests/test-i2cp-session-lifecycle.cpp`.
- [ ] **[I2CP-116]** LeaseSet create/reconfigure validation and send statuses.
  - Priority P2. CLS1 derives the signing-key length from the identity, requires a matching destination, validates the embedded LeaseSet signature and the advertised ElGamal key, and remains DSA-only (fail closed, tracked). CLS2 stops reading a key-count byte for a MetaLeaseSet2, follows the `i2cp.leaseSetEncType` preference order and compares derived public keys (X25519 family). `SendMessageExpires` preserves the expiration and returns status 14 (expired) or 10/11 for a bad session/message instead of a silent drop.
  - Files: `libi2pd_client/I2CP.{h,cpp}`, `tests/test-i2cp-blinding-info.cpp`.
- [ ] **[STR-113]** Streaming accept lifetime and acceptor chain.
  - Priority P0/P1 (memory/UX). `AcceptStream(timeout)` no longer captures stack references (a timed-out accept could write through a dangling lambda); it uses shared sync state and unregisters on timeout. Acceptors are an explicit registration list with a token, `RemoveAcceptor(token)` removes one and `ResetAcceptor()` completes every armed waiter with `nullptr` instead of silently dropping the chain.
  - Files: `libi2pd/Streaming.{h,cpp}`, tests `test-streaming-accept-timeout`, `test-streaming-accept-race`.
- [ ] **[STR-114]** Streaming duplicate-SYN and rejection behaviour.
  - Priority P1. A bounded 128-entry/120 s recently-closed set drops a retransmitted SYN for a finished incoming stream (no resurrection, no second application connection) and answers it with a signed RESET. Over-limit, throttled and backlog-full SYNs also get one signed RESET instead of silence.
  - Files: `libi2pd/Streaming.{h,cpp}`, tests `test-streaming-recently-closed`, `test-streaming-overlimit-reset`.
- [ ] **[STR-115]** Streaming post-close send and handler races.
  - Priority P2. No data packet is emitted after CLOSE/RESET (writes fail with `operation_aborted`, queued reads stay readable for half-close); the pong handler is snapshotted under the acceptor mutex; a duplicate SYN no longer resets the routing path; acceptor decision/dequeue/queue insertion happen under one lock so a stream is never orphaned.
  - Files: `libi2pd/Streaming.{h,cpp}`, `tests/test-streaming-sendstate.cpp`.
- [ ] **[DG-111]** Per-send Datagram1/2/3 framing.
  - Priority P1 (interop). `eDatagramDefault` plus a `version` parameter on `SendDatagram`/`SendDatagramTo`; an explicit version overrides the destination-wide default, so a DATAGRAM3 SAM subsession on a destination first created as V1 emits protocol 20/Datagram3 instead of protocol 17.
  - Files: `libi2pd/Datagram.{h,cpp}`, `tests/test-datagram-version-dispatch.cpp`.
- [ ] **[DG-112]** Protocol-aware datagram receivers and no cross-subsession promotion.
  - Priority P1 (privacy). Identity and sender-hash receivers are keyed by `(protocol, port)` with 0 as the catch-all; Datagram1/2/3 dispatch to 17/19/20; a port-specific registration no longer becomes the destination-wide wildcard, so an unregistered port is dropped instead of delivered to another subsession. Raw registration refuses 6/17/19/20 and a 0xff protocol byte is normalised to 0.
  - Files: `libi2pd/Datagram.{h,cpp}`, tests `test-datagram-dispatch`, `test-datagram-receiver-lifecycle`.
- [ ] **[DG-113]** Datagram parser/session state safety.
  - Priority P2. Options are per call (the receiver no longer sees a buffer the next datagram overwrites), the deflator/inflator are mutex-guarded, the message pool is thread-safe for SAM sends, and a datagram session serializes its send queue/routing/LeaseSet state. All-zero signatures and malformed offline sections are dropped instead of published.
  - Files: `libi2pd/Datagram.{h,cpp}`.

## Privilege-separation continuation: capability invariant and data storage (2026-10-26, static)

Read-only audits of the supervisor, storage workers, network path, crypto/log
workers and the core's remaining write authority preceded these rows. The
audits confirmed that every worker is network-free, but that the router core is
the only process still combining external sockets with persistent writes.
Everything below is Implemented / static, Pending CI compilation and Pending CI
tests; no compiler, CTest, sanitizer, fuzzer, OpenBSD/Windows runtime or live
peer run exists.

- [ ] **[PRIVSEP-018]** Bind a worker role to its spawner.
  - Priority P0 (security); Status Implemented / static. `SpawnWorker*` generates a 16-byte CSPRNG nonce and writes `[role-id u8 | nonce 16]` into an anonymous pipe before `fork()`; the child reads it from fd 5, closes fd 5, and validates the role before touching the channel or applying its sandbox. The record never appears in `argv`, `environ`, a log line or the protocol channel, so a same-UID process can no longer invoke `--privsep-worker <role> 3` with its own socketpair and adopt a role. Descriptor contract is 3 = channel, 4 = inherited root (OpenBSD), 5 = one-shot identity, ≥6 closed.
  - Files: `privsep/Worker.{h,cpp}`, `privsep/RunWorker.{h,cpp}`, tests `test-privsep-worker-identity`.
  - Residual: `WorkerHello` is a first-frame record on a pipe, not a kernel-attested credential; a same-UID process that can already ptrace/read another process's fds is out of scope.
- [ ] **[PRIVSEP-019]** Data-storage worker for hash-named runtime artifacts.
  - Priority P0 (invariant); Status Implemented / static. New `EndpointRole::DataStorage` and protocol v3 (still accepting v1/v2) with `ConfigureDataStorage`/`DataStorageRequest`/`DataStorageReply`; `DataStorageClient` + `RunDataStorageWorker` own `netDb/r*/routerInfo-<b64>.dat`, `peerProfiles/p*/profile-<b64>.txt` and `destinations/<b32>.<type>.dat` under a validated root with canonical-name checks, 8 fixed staging slots, an atomic no-follow replace and 0600 for key material. `libi2pd/PersistentStorage` exposes `StorageArtifact`, `WriteArtifact`/`RemoveArtifact` and `InstallArtifactStorage`; `privsep.artifacts=true` makes the worker the only writer (netDb persistence, profiles and destination key saves), with no local fallback after an installed writer failure and a fail-closed path when the worker is unavailable. `RouterContext` retains the last `router.info`/fixed-file save on failure instead of silently dropping it.
  - Files: `privsep/{Protocol.h,DataStorage.*,RouterStorage.cpp}`, `libi2pd/{PersistentStorage.*,NetDb.cpp,RouterInfo.*,Profiling.cpp,Destination.cpp,RouterContext.cpp,Config.cpp}`, `daemon/Daemon.cpp`, `build/CMakeLists.txt`, tests `test-datastorage-{protocol,worker,client}`, `test-artifact-storage`, extended `test-netdb-persistence`/`test-router-profile-storage`.
  - Residual: startup/read paths are still direct in the core; garlic tags, address book, torrent state, `router.keys` bootstrap and config remain core-written; OpenBSD cannot respawn a dead data worker after the runtime pledge (writes fail closed until restart); Windows/Android keep today's behaviour.
- [ ] **[PRIVSEP-020]** Restartable, generation-fenced storage clients.
  - Priority P1 (recovery); Status Implemented / static. `RouterStorageClient` no longer keeps a sticky `ready` flag: a failed send/recv marks the client restartable, a restart bumps the generation, request IDs are fenced per generation, stale replies are discarded, id exhaustion returns `Busy` instead of killing the client, and no ambiguous write is replayed. `DataStorageClient` uses the same rule with bounded backoff (250 ms→30 s, 8 attempts in 60 s, stable reset after 60 s). The supervisor publishes a bounded `SupervisorStatus` (state/generation/restarts/dropped records/reason) and classifies a future torrent-broker request id as a typed `BadField` rejection that keeps the channel.
  - Files: `privsep/RouterStorage.cpp`, `privsep/DataStorage.cpp`, `privsep/Supervisor.{h,cpp}`, tests `test-datastorage-client`, `test-privsep-supervisor`.
  - Residual: the core's own monitor and the supervisor still both track log-worker restarts; the status snapshot is process-local and not yet exposed through TUI/IPC.
- [ ] **[PRIVSEP-021]** Capability matrix and log-worker hardening.
  - Priority P1 (security); Status Implemented / static. `privsep/Capabilities.h` is the single role×capability matrix with the invariant "no role may combine `ExternalNetwork` with `PersistentWrite`", the target core row without the write bit and an explicit legacy row with it; `IsRoleCapabilitySafe`/`ValidateCapabilityMatrix` are compile-time/test-checked. The log worker sanitises UTF-8 and C1 controls, redacts known secret keys, caps the file sink at 64 MiB with one notice and no rotation, and sends from a channel snapshot outside the log mutex; drop counters are exposed.
  - Files: `privsep/Capabilities.h`, `privsep/LogWorker.{h,cpp}`, `privsep/Supervisor.{h,cpp}`, tests `test-privsep-capabilities`, `test-privsep-supervisor`, extended `test-privsep-log`.
  - Residual: no fd-based log sink in production, no log rotation, Windows has no log worker, and the two capability vocabularies (`libi2pd/PersistentStorage.h` and `privsep/Capabilities.h`) must not diverge.
- [ ] **[PRIVSEP-022]** Next phases: network worker, crypto signing worker, core lockdown.
  - Priority P0/P1; Status Missing. Audits rank the network slices as listener-only handoff (~600 LOC, lowest risk), then connect handoff, then a full transport worker (needs the router signing key or a signing oracle, netDb-over-IPC and a bulk data path). The crypto worker's first useful slice is a remote signer (`CryptoSign`/`CryptoSignReply`) with a fallible `Signer::Sign` prerequisite, per-identity handles and no network/write authority; ratchet/tag operations must stay in-process for latency and anonymity. The core can additionally install a write-denying Landlock profile after init (`privsep.core_write_lockdown`) once residual writes are delegated. OpenBSD needs `sendfd` in the network policy and a post-pledge respawn design; Windows needs a `--privsep-worker` entry, a `RunWorker` backend and `WSADuplicateSocketW` handoff; the OpenBSD broker should reject `relative == "."` and a second worker per instance.
  - Reference: `privsep/LinuxSandbox.cpp`, `privsep/OpenBSDSandbox.cpp`, `privsep/WindowsIpc.cpp`, `libi2pd/NTCP2.*`, `libi2pd/SSU2.*`, `libi2pd/Transports.*`, `libi2pd/Signature.h`.

## IPC consolidation continuation (2026-10-26, static)

Four read-only audits (serialization/registry, channel/recovery, per-role
authorization, platform backends plus fuzz coverage) preceded these rows. The
audits confirmed that the frames themselves were bounded but that role policy
was duplicated, decoder failures were terminal, reply direction was implicit,
the Windows receive path killed idle workers, and a pre-opened log descriptor
was not bound to the configured path. Everything below is Implemented / static,
Pending CI compilation and Pending CI tests.

- [ ] **[PRIVSEP-023]** Single message registry and typed per-type limits.
  - Priority P0 (security); Status Implemented / static. `privsep/Protocol.h` now owns one `MessageSpec` row per type with `{minVersion, direction, receiving roles, payload ceiling, descriptor policy}`; `Encode`/`Decode`/`AllowsOperation`/`DescriptorPolicyFor` all derive from it and a `constexpr ValidateMessageRegistry()` static assertion requires a contiguous table with sane ceilings. The hand-written role switch is gone. `EndpointRole::Core` makes reply direction explicit: a worker role receives request-direction types only and the requesting side reply types only. Version 4 adds the advisory `Cancel`; a type carried in a header older than its `minVersion` is `UnsupportedVersion` (no downgrade reinterpretation). Per-type ceilings replace the single 64 KiB check, so `Ping` is empty and a log/config/storage frame has its own bound.
  - Files: `privsep/Protocol.h`, tests `test-privsep-authorization` (new), extended `test-privsep-protocol`/`test-datastorage-protocol`.
  - Residual: a decoded `Message` still owns one payload copy (the decoder returns an owning frame so no view outlives the transport buffer); removing it needs a buffer-owning frame type. There is still no explicit hello/version-negotiation frame; compatibility is per frame. The OpenBSD `imsg` adapter cannot carry a frame above `MAX_IMSGSIZE` (16 KiB), so the 32 KiB `RouterStorageRequest` ceiling is POSIX/Windows-only until chunking exists. `ConfigureTorrentStorage` still has a dual core→broker/worker schema.
- [ ] **[PRIVSEP-024]** Endpoint enforcement, descriptor policy and recoverable faults.
  - Priority P0 (security); Status Implemented / static. `Channel` has `EnforcementPolicy::{Transport,Endpoint}`. A serving endpoint (log, router/data/torrent storage, supervisor, broker) enforces the registry's descriptor presence/kind policy, closes a rejected descriptor, answers an *attributable* frame fault with a typed `Error` correlated by request id, and tolerates at most `MAX_PROTOCOL_VIOLATIONS` (8) such faults before exit. A record is only attributable when magic, version and declared length agree, so a corrupt header cannot choose a request id. Credential and ancillary failures stay fatal. Workers no longer exit on a wrong-role or malformed frame: router/torrent storage, the log worker, the supervisor and the broker all reply with a bounded `NotAuthorized`/`BadField` instead. A duplicate `ConfigureLog` is a bounded refusal, not a teardown.
  - Files: `privsep/Channel.{h,cpp}`, `privsep/{LogWorker,RouterStorage,TorrentStorage,DataStorage,Supervisor}.cpp`, tests extended `test-privsep-log`/`test-privsep-supervisor`.
  - Residual: `Cancel` is advisory only (synchronous single-flight workers cannot roll back an applied mutation); the client drains the reply and reports `Busy` without replay. There is no per-request in-flight table with explicit cancellation acknowledgement. Backpressure is deadline- and credit-based (one reader, single-flight, bounded fault budget, kernel socket buffer); there is no user-space queue or explicit credit window.
- [ ] **[PRIVSEP-025]** Windows parity and log capability binding.
  - Priority P1 (security); Status Implemented / static. A Windows message-mode receive timeout now returns an empty result and keeps the pipe (every polling worker previously exited after one idle period). The Windows decoder applies the same attributable-fault rule with its own bounded budget, and `WindowsAllowsOperation` delegates to the shared registry instead of a second switch (retaining only the "no descriptor channel ⇒ no `ConfigureLogFd`" limitation). A pre-opened log descriptor is bound to the configured path by dev/inode identity, owner and single link count before `fchmod`/sandbox, so the core cannot turn the log worker into an arbitrary same-UID write primitive.
  - Files: `privsep/WindowsIpc.{h,cpp}`, `privsep/LogWorker.cpp`.
  - Residual: Windows daemon wiring/`--privsep-worker` and a bound shell accept path remain pending; Windows has no descriptor equivalent, so `ConfigureLogFd` stays POSIX-only. The log sink is still path-based in production (the supervisor/worker opens it), with no fd-based sink or rotation.
- [ ] **[PRIVSEP-026]** Fuzzing and authorization regressions.
  - Priority P1 (verification); Status Prepared / not executed. `fuzz-privsep-protocol` now exercises `Decode`, `InspectFrame`, every payload decoder (log record/config, error, cancel, data-storage configure/request/reply) and asserts encoder/decoder round-trip agreement for both a data-storage request and an input-derived message type. `test-privsep-authorization` pins registry completeness, role×direction, descriptor presence policy, per-type version/ceiling rejection, Cancel codec bounds and attributable-fault classification.
  - Files: `tests/fuzz/fuzz-privsep-protocol.cpp`, `tests/test-privsep-authorization.cpp`, `tests/CMakeLists.txt`, `tests/check-control-security.cmake`.
  - Residual: the fuzz target is registered in the manual fuzz workflow but no run exists; the authorization test is compile-free and does not exercise a live worker channel (the descriptor-kind path needs `fstat`, covered by the Linux IPC test only for the transport policy).

## OpenBSD supervised-start continuation (2026-10-26, static)

Four read-only audits (storage worker/broker, process startup/credentials, IPC
and pledge/unveil) preceded this increment. They found four P0 defects: the
core startup pledge lacked `recvfd` (so the runtime `recvfd` was silently
ignored and the first supervised handoff would abort), the supervisor had
`exec` with no unveil (an exec'ed image would run unsandboxed), there was no
credential-drop machinery at all, and `ExecutablePath()` trusted `$PATH` with a
`/proc/self/exe` fallback that does not exist on OpenBSD. Everything below is
Implemented / static, Pending CI compilation and Pending CI tests.

- [ ] **[OPENBSD-SBX-8]** Supervisor/broker unveil lockdown and pledge window.
  - Priority P0 (sandbox escape); Status Implemented / static. The log supervisor and the storage broker now pre-resolve their own executable, `unveil(exe,"x")` + lock and pledge `stdio proc exec` / `stdio proc exec sendfd`: no `unix`, no `rpath/wpath/cpath`, no descriptor reception and no network. `execve` can only reach the frozen image; the new image starts without unveils (per `execve`/`unveil`) and installs its own policy. `ApplyOpenBSDPolicy` always passes `OPENBSD_PLEDGE_EXECPROMISES = nullptr`; the core startup set (shared table) adds `recvfd` and `id` when a worker identity is configured, the runtime set is rebuilt from the same table and startup aborts if it is not a strict subset. The pledge-file parser is shared, subset-only and rejects comments/blank lines and dangerous runtime promises. `openbsd.unveil_file` rejects relative/`/` entries. The broker pins 0/1/2 to `/dev/null`, charges every spawn attempt to the launch window, uses per-phase timeouts and stops a half-configured child.
  - Files: `privsep/OpenBSDSandbox.{h,cpp}`, `privsep/Supervisor.{h,cpp}`, `daemon/Daemon.cpp`, `daemon/UnixDaemon.cpp`, `tests/test-privsep-openbsd-policy.cpp`.
  - Residual: the broker keeps `relative == "."` for torrent compatibility (delegates the whole datadir); rejecting it needs a dedicated `<datadir>/torrents` root. The core still holds startup `exec` for the log supervisor and broker but loses it at runtime.
- [ ] **[OPENBSD-SBX-9]** Worker credentials, spawn identity and descriptor hygiene.
  - Priority P0 (root workers); Status Implemented / static. `privsep.user`/`privsep.group` are resolved once with bounded `getpwnam_r`/`getgrnam_r` before the startup pledge; uid 0 is rejected. The forked child calls `setgroups(0,nullptr)`, `setgid`, `setuid` and verifies real/effective ids plus an empty supplementary set before `execve`. A root core with any isolation feature and no configured identity fails startup with an explicit diagnostic. The hello record is now 25 bytes `[role u8 | nonce 16 | uid u32 | gid u32]`, written by the child after the drop; the worker requires role/uid/gid agreement, a non-zero uid and exactly one record. The executable is frozen from `argv[0]`/`realpath` (absolute, regular, owner root/self, no group/world write) with no `$PATH` or `/proc` fallback. Descriptors: 3 channel, 4 inherited root, 5 hello, 0/1/2 `/dev/null`, >=6 closed; a role without a root closes fd 4, and stdio is pinned even for a directly invoked worker. `ForbidProcessCreation()` is a one-way process gate consulted by every spawn helper before the runtime pledge.
  - Files: `privsep/Credentials.{h,cpp}`, `privsep/Worker.{h,cpp}`, `privsep/RunWorker.cpp`, `libi2pd/Config.cpp`, `daemon/i2pd.cpp`, `tests/test-privsep-credentials.cpp`, extended `tests/test-privsep-worker-identity.cpp`.
  - Residual: the core itself is still not chrooted/dropped (workers only); an unprivileged core cannot clear supplementary groups, so such a deployment must start without them; NSS lookup happens before the sandbox by design; there is a narrowed stat/access-to-exec race on the frozen path.
- [ ] **[OPENBSD-SBX-10]** Identity-aware peer and root checks.
  - Priority P0 (root core with unprivileged workers); Status Implemented / static. The core-side storage clients accept a root core only when `privsep.user` is configured and compare the delegated root owner against the **worker** uid (not the core's euid). Workers accept a channel peer with their own uid or, when unprivileged, uid 0 (the delegating root core). `AdoptValidated` for a broker-forwarded worker socket uses `WorkerUid()`. The data directory must be owned by the configured worker identity.
  - Files: `privsep/{RouterStorage,DataStorage,TorrentStorage,LogWorker,Supervisor}.cpp`, `libi2pd_client/TorrentStorageWorker.cpp`, `privsep/Credentials.cpp`.
  - Residual: a root core plus `privsep.user` still requires the administrator to chown the data tree; a non-root core with supplementary groups fails closed at spawn.
- [ ] **[OPENBSD-SBX-11]** Generalize the pre-pledge broker to router/data storage and isolate named control sockets.
  - Priority P1; Status Pending. The broker currently spawns only torrent storage; router and data storage are spawned directly by the core in `init()` and cannot be replaced after the runtime pledge. Extend the broker with typed `SpawnStorageWorker`/`ConfigureSupervisedStorage`/`StorageSupervisorReply` messages (protocol v5), a role byte, per-role readiness and per-role config, so router/data clients adopt a broker channel and restart through it. Separately, the supervisor/storage `unix` removal is complete, but the TUI/I2PControl sockets still live under the datadir; move them out of any storage-unveiled root or prove `connect()` is denied, and add the negative test. Do not reintroduce `exec` in the core for this.
  - Reference: `privsep/Supervisor.cpp` broker, `privsep/RouterStorage.cpp`, `privsep/DataStorage.cpp`, `daemon/Daemon.cpp`, `privsep/Protocol.h`, `docs/security/privilege-separation.md`.

## Linux backend hardening continuation (2026-10-26, static)

Four read-only audits (storage worker, AF_UNIX/seqpacket IPC, credentials and
descriptors, supervision/namespaces) found two P0 defects: the Linux per-frame
`SCM_CREDENTIALS` check compared against `SO_PEERCRED` of the socket creator,
so every worker reply was rejected after a root core dropped the worker to
`privsep.user`; and the capability/bounding-set teardown ran inside the
already-unprivileged exec'ed worker, where `capset`/`PR_CAPBSET_DROP` return
EPERM and are silently tolerated. The storage seccomp filter also denied only
`socket`/`socketpair`, leaving `connect`/`execve`/`fork` reachable, and the
network namespace was a silent no-op. Everything below is Implemented / static,
Pending CI compilation and Pending CI tests.

- [ ] **[PRIVSEP-027]** Explicit per-frame peer expectation on Linux.
  - Priority P0 (isolation was functionally broken); Status Implemented / static. `Channel::PeerExpectation` carries uid/gid/pid with `requireGid`/`requirePid`/`allowRoot`; `CreatePair`, `Adopt`, `AdoptValidated` and every spawn helper install the topology-derived expectation. The core requires the direct child pid; a worker requires its own uid or uid 0 (root delegating core); an adopted broker socket requires the worker uid. A socket with a pre-queued record is rejected at adoption so a broker cannot pre-load an impersonating frame. The legacy `SO_PEERCRED` comparison remains only for raw transport tests (`configured=false`).
  - Files: `privsep/Channel.{h,cpp}`, `privsep/Worker.cpp`, tests `test-privsep-linux-ipc` (`TestPeerExpectation`, `TestAdoptRejectsPendingRecord`).
  - Residual: the worker->core direction is pid-bound but the core->worker direction is uid-only because the worker does not know the core pid; a same-uid sibling that already holds the worker channel can still inject. A second-uid regression requires a `sudo`/`doas` lane.
- [ ] **[PRIVSEP-028]** Capability teardown in the spawn child and post-exec verification.
  - Priority P0 (file capabilities survived into workers); Status Implemented / static. The forked child now sets `PR_SET_NO_NEW_PRIVS`, clears the ambient and bounding sets while still privileged, drops gid/uid, then calls `capset(0)` and verifies with `capget` before `execve`; the exec'ed worker re-asserts `PR_GET_NO_NEW_PRIVS == 1` and an empty capability set before reading a frame. Ignored signal dispositions are reset to `SIG_DFL` in the child.
  - Files: `privsep/LinuxSandbox.{h,cpp}`, `privsep/Worker.cpp`, `privsep/RunWorker.cpp`, tests `test-privsep-linux-sandbox` (`ExpectWorkerPrivileges`), `test-privsep-credentials` (now Unix, not OpenBSD-only).
  - Residual: a root container without `CAP_SETPCAP` fails closed at spawn; the core itself is still not dropped.
- [ ] **[PRIVSEP-029]** Linux storage confinement: connection lifecycle, exec/fork and read restrictions.
  - Priority P0/P1; Status Implemented / static. The `denyAll` seccomp branch denies `connect`, `bind`, `listen`, `accept`, `accept4`, `sendto`, `execve`, `execveat`, `fork`, `vfork`, `clone` and `clone3` in addition to `socket`/`socketpair`; Landlock handles `EXECUTE` without granting it through a writable root; the `Storage` profile sets `restrictFilesystemReads`; the socket/capability lockdown is installed **before** the configure frame and Landlock (which needs the delegated root) follows. An empty network namespace is attempted in the spawn child while it still has `CAP_SYS_ADMIN` (best-effort; seccomp remains the boundary).
  - Files: `privsep/LinuxSandbox.{h,cpp}`, `privsep/{RouterStorage,DataStorage,TorrentStorage,LogWorker}.cpp`, tests `test-privsep-linux-sandbox` (`exec`, `fork`, inherited `connect`, outside read, privileges).
  - Residual: cgroup-v2 limits, a global admission cap, a log-role deny-all socket profile for pre-opened sinks, and `UDP`-specific Landlock behavior remain open.
- [ ] **[PRIVSEP-030]** Executable identity and role/root pairing.
  - Priority P1; Status Implemented / static. The frozen executable is validated as a regular file owned by root/self, without group/world write, with a non-writable parent directory; its dev/inode/size are re-checked immediately before `execve` on Linux. Only `*-inherited` storage roles and the broker accept an inherited root; every other role refuses one, the root must be a directory owned by the worker uid without group/world write, and `RunWorker` closes fd 4 for any other role.
  - Files: `privsep/Worker.{h,cpp}`, `privsep/RunWorker.cpp`.
  - Residual: OpenBSD cannot re-`stat` after its x-only unveil/pledge, so a path replacement there relies on the frozen path and directory ownership; there is no `execveat(AT_EMPTY_PATH)` image binding yet.
- [ ] **[PRIVSEP-031]** Pending supervision/resource work.
  - Priority P1; Status Pending. Wire `ApplyLinuxCgroupV2Limits` per worker (parent-created cgroup, `clone3(CLONE_INTO_CGROUP)` or a start gate, `pids.max`/`memory.max`, `cgroup.kill` on stop); add a process-wide worker-admission cap; expose `SupervisorStatus` over the typed IPC; unify the three restart ladders and the stop budget; reap the broker on every loop turn; add a log-role deny-all socket profile for pre-opened sinks; and bind the worker's expected core pid once the hello carries it. No Linux runtime evidence exists.

## Windows multiprocess continuation (2026-10-26, static)

Four read-only audits (Named Pipes, local RPC/ALPC, tokens/AppContainer,
supervision/Job Objects) found that the transport primitives existed but had no
production consumer; spawn identity was on `argv` with no nonce; per-operation
authorization was dead code; the pipe was single-shot and a completed record
could be discarded on timeout; a restricted token without restricting SIDs was
the only confinement, so storage could use sockets and network could write
files; there was no supervisor/restart owner, no worker entry, and the service
could report STOPPED while the router still ran. Everything below is
Implemented / static, Pending CI compilation and Pending CI tests.

- [ ] **[WIN-006]** Windows worker entry and spawn-bound identity.
  - Priority P0; Status Implemented / static. `--privsep-worker <role> <pipe> <parentPid> <parentSession> <nonce>` is dispatched before `Daemon` and never initialises the router. The worker connects with `ConnectBound` to the spawned parent PID/session, sends a 32-byte CSPRNG nonce in the new `WorkerHello` (protocol v5) frame, and the server consumes it after impersonating the client and checking the user SID, PID/session, AppContainer SID and nonce. The role is bound only after the hello. The nonce is on the command line and is not a secret from a same-user process; the protected DACL and PID/session/container binding remain the primary boundary.
  - Files: `privsep/Protocol.h`, `privsep/WindowsIpc.{h,cpp}`, `daemon/i2pd.cpp`, `tests/test-privsep-windows-ipc.cpp`.
- [ ] **[WIN-007]** Per-operation authorization and authenticated transport.
  - Priority P0; Status Implemented / static. `WindowsChannel::SetRole` plus `Receive` enforce the shared registry after the role is bound: a role may only receive its request-direction types, any `Required*` descriptor type is refused (no descriptor channel), and a refusal is one typed `Error` under the 8-violation budget. `AcceptBound` rejects wrong PID/session/user/container/nonce and re-arms with a bounded backoff (16 per accept). The pipe is message-mode, `PIPE_REJECT_REMOTE_CLIENTS`, protected DACL with the service user, SYSTEM and the exact container SID, `FILE_FLAG_FIRST_PIPE_INSTANCE`, and a 64 KiB+1 receive buffer so oversize is terminal.
  - Files: `privsep/WindowsIpc.{h,cpp}`, tests `test-privsep-windows-ipc.cpp`.
  - Residual: the listener is still one instance per generation; a rejection is bounded but the pipe name is not session-scoped.
- [ ] **[WIN-008]** AppContainer/LPAC role isolation and delegated roots.
  - Priority P0 (network/filesystem separation); Status Implemented / static. `privsep/WindowsIsolation.cpp` is the single role authority: storage/log/crypto/admin are low-privilege AppContainers with no `internetClient`; network is an AppContainer with `internetClient`/`privateNetworkClientServer`; the supervisor uses a restricted token because it spawns children. Profiles are durable `openi2pd.<role>` and capability SIDs are derived; `GrantAppContainerPath` grants only the delegated write root to the container SID; no Everyone/ALL APPLICATION PACKAGES ACE is added. `SpawnWindowsWorker` uses `PROC_THREAD_ATTRIBUTE_SECURITY_CAPABILITIES` + the LPAC all-applications opt-out, refuses a container role without a container, a container for a non-container role, or a persistent-write role without a root, and never silently falls back to a restricted token. The table rejects any role with both an Internet capability and a write root.
  - Files: `privsep/WindowsIsolation.{h,cpp}`, `privsep/WindowsIpc.cpp`, `privsep/Capabilities.h`, `build/CMakeLists.txt`, `tests/test-privsep-windows-isolation.cpp`.
  - Residual: profiles are not deleted automatically; a network AppContainer can write its own profile folder; no live storage/network backend exists yet.
- [ ] **[WIN-009]** Spawn, Job Object and mitigation hardening.
  - Priority P1; Status Implemented / static. The child is created suspended with `CREATE_NO_WINDOW`, `EXTENDED_STARTUPINFO_PRESENT`, `bInheritHandles=FALSE`, NUL std handles, an explicit empty handle list and a computed attribute count; `PROC_THREAD_ATTRIBUTE_JOB_LIST` is used with an `AssignProcessToJobObject` fallback while suspended; every failure terminates the job/process and waits; `TerminateAndWait`/`Stop` terminate the Job Object; per-role memory/process limits and CFG/dynamic-code/extension-point/strict-handle/child-process mitigation bits are applied with SDK guards.
  - Files: `privsep/WindowsIpc.{h,cpp}`.
  - Residual: nested-job topology can still reject assignment; the diagnostic is reported; a restrictive parent job needs an operator change.
- [ ] **[WIN-010]** Windows worker supervisor and recovery.
  - Priority P0; Status Implemented / static. `WindowsWorkerSupervisor` owns one role, creates the per-generation pipe and AppContainer identity, spawns and authenticates the child with the PID/session/role/nonce/container binding, publishes `Starting/Ready/Restarting/Failed` through the shared `SupervisorStatus`, restarts under the shared 250 ms→30 s / 8-in-60 s budget with a monotonic generation, and stops with a bounded `Shutdown`→grace→`TerminateJobObject` sequence. Setup failures are terminal and handle-neutral; `KILL_ON_JOB_CLOSE` is the backstop.
  - Files: `privsep/WindowsSupervisor.{h,cpp}`, `tests/test-privsep-windows-supervisor.cpp`, `Win32/Win32Service.cpp`.
  - Residual: no daemon path instantiates it yet; live storage/network role backends do not exist.
- [ ] **[WIN-011]** Service stop/shutdown recovery.
  - Priority P1; Status Implemented / static. `SERVICE_START/STOP_PENDING` use non-zero wait hints, `Start` failure unwinds `Daemon.start()` and reports a real service code, `OnShutdown` drains `Daemon.stop()` within a bounded budget instead of reporting STOPPED while the router runs, `ServiceMain` no longer throws across the SCM callback, and the stop flag is published atomically.
  - Files: `Win32/Win32Service.cpp`.
  - Residual: no live SCM/service-account test exists.
- [ ] **[WIN-012]** Local RPC decision.
  - Priority P2; Status Dropped (ncalrpc not selected; named pipes retained). Message-mode Named Pipes are retained. `ncalrpc` would add per-call SSPI identity but requires `rpcrt4`/MIDL or checked-in stubs, a second core-hosted interface for worker→core frames (`Pong`/`Error`/supervisor replies), a separate cancellation model, and a `MaxRpcSize` clamp; it does not solve name squatting better than `FILE_FLAG_FIRST_PIPE_INSTANCE` plus a random name. Revisit only for a real cross-principal/AppContainer administrative split, and then migrate the control plane only. No undocumented ALPC (`NtCreatePort`/`NtConnectPort`/`NtRequestWaitReplyPort`, `\RPC Control\`, raw `ntdll` exports) is used anywhere.
  - Files: `docs/security/windows-worker-ipc.md`.
- [ ] **[WIN-013]** Production wiring and role backends.
  - Priority P1; Status Pending. Start `WindowsWorkerSupervisor` from the daemon/service for the log (and then storage/network) roles, pass the delegated root, surface the supervisor status in the TUI/Android admin path, and implement the actual Windows log/storage/network role loops. Until then no Windows worker is started by production.

## Android multiprocess and lifecycle continuation (2026-10-26, static)

Four read-only audits (services/lifecycle, Binder/AIDL, JNI, SAF/storage) found
that the router service was Activity-independent but a graceful stop could be
lost on process death; the SAF broker trusted same-UID as its whole
authorization model and exposed an unscoped export truncate plus a generationless
`retire()`; the JNI string path passed `nullptr` to `NewString` for empty
results and could leave a pending exception; `stopDaemon` could not cancel an
in-flight start; and nothing prevented a second router from starting inside the
storage broker process. Everything below is Implemented / static, Pending CI
compilation and Pending CI tests.

- [ ] **[ANDROID-023]** Durable run intent and task removal.
  - Priority P0 (Activity/process independence); Status Implemented / static. `RouterRunState` persists `run_phase`/`graceful_deadline_epoch_ms`/`run_generation` in the existing `router_state` preferences, mirrors the legacy `should_run` boolean, and is reconciled in `onCreate`/`Restore`: an expired drain is terminal (no resurrection on `START_STICKY`/boot), a live drain re-arms the remaining wall-clock deadline only after `startedOkay`, and corrupt/absurd deadlines are clamped or fail closed. `onTaskRemoved` keeps the daemon (manifest `stopWithTask=false`), republishes the notification and records bounded diagnostics; `onDestroy` purges all pending main-handler callbacks; the notification channel is verified inside `ensureForeground()` and restore failure publishes `restoreDeferred` with one bounded retry.
  - Files: `RouterService.java`, new `RouterRunState.java`, `src/test/.../RouterRunStateTest.java`; `control-security-static` pins the persisted keys, `onTaskRemoved`, the callback purge and `stopWithTask="false"`.
  - Residual: wall-clock jumps, asynchronous `apply()`, OEM force-stop, and a `MY_PACKAGE_REPLACED` during a drain are outside the service's control; no device run.
- [ ] **[ANDROID-024]** SAF broker authorization, admission and recovery (AIDL v2).
  - Priority P0 (capability boundary); Status Implemented / static. AIDL v2 adds `handshake()`/`apiVersion()`, a one-shot `prepareExport` token consumed by `openExport(token)`, `retire(generation)`, `deleteByRelative` and `removeEmptyDirectory`. Every root, handle, operation and token is bound to the exact `(uid,pid)` that created it; `cancel` is owner-scoped; `closeFile`/`revokeTree` require the owner; `retire` requires the handshake caller and the broker generation. Admission returns typed `0/false/null` instead of throwing. The watchdog cancels the provider at 15 s and kills the broker only at 30 s if the same operation is still registered, so one slow read does not drop every torrent handle. The watchdog is armed only after `ioLock`, so a queued operation is not fenced for a stalled sibling; the client bind budget recovers from a transient broker crash (6 attempts, 250 ms→10 s, attempts restored only after a 30 s stable binding) without an application restart. The client performs a bounded handshake/API-version check, uses the export token, recovers a stuck `retiringBinder` within 5 s, and distinguishes `BUSY`/`TIMEOUT`/`UNAVAILABLE` from authoritative failure; no `ParcelFileDescriptor` crosses Binder.
  - Files: `ITorrentSafStorage.aidl`, `TorrentSafStorageService.java`, `TorrentSafStorage.java`, `src/test/.../TorrentSafProtocolTest.java`, `androidTest/.../TorrentSafBrokerContractTest.java`, fixed `TorrentSafCancellationWireTest`; `control-security-static` pins AIDL v2, `(uid,pid)` binding, the two-stage watchdog and the no-descriptor-over-Binder rule.
  - Residual: same-UID remains the hard boundary (foreign-UID denial needs a helper APK); a provider blocking >30 s still fences the broker; a user-chosen single-document export that fails is left zero-length (deleting the user's document would be destructive); native `Busy` retry semantics are caller-driven.
- [ ] **[ANDROID-025]** JNI process role, string boundary and stop cancellation.
  - Priority P0/P1; Status Implemented / static. A one-shot process role (`Router`/`StorageBroker`) is claimed in native memory; `startDaemon`/SAF bridge require `Router`, `restrictSafStorageProcess` claims `StorageBroker`, and a conflict returns a fixed token without starting a router. `NewStringChecked` never passes a null `jchar*` for empty results; `Utf8FromJavaString` clears a pending exception; `buildTorrentMetadata` uses `ScopedLocalRef` and clears exceptions on every failure. `stopDaemon` publishes a native stop token before any monitor, `DaemonAndroid::Start` checks it in the asset loop and around init/start, and returns `start-canceled`; the Java start maps it to stopped with at most one rollback stop. SAF bridge/payload destruction no longer calls JNI from static/stop contexts; payloads are detached on the stop path; `getTransitTunnelsCount` handles `LinkageError`.
  - Files: `i2pd_android.cpp`, `DaemonAndroid.{h,cpp}`, `DaemonWrapper.java`, `I2PD_JNI.java`, `tests/test-android-daemon-lifecycle.cpp`, `control-security-static` pins.
  - Residual: the role guard is process-memory only and does not defend against a compromised process; a payload destroyed on an unattached worker thread leaves its document to the broker/process end; no CheckJNI/device evidence.
- [ ] **[ANDROID-026]** Router/presentation process split and topology.
  - Priority P1; Status Pending. Storage already runs in `:torrent_storage` (same UID; a real heap/crash/seccomp boundary, not a privilege boundary). Networking cannot be split into a separate permission domain: Android grants `INTERNET` per UID and an `isolatedProcess` loses both INTERNET and persisted SAF grants. The remaining split is router vs presentation: move `RouterService`, `DaemonWrapper`, JNI and the SAF client to `:i2pd_router`, add a closed `IRouterControl` AIDL (`RouterSnapshot`/`LogEntry` Parcelable, typed admin verbs), replace `RouterService.isRouterRunning()` for the tile/boot paths, and keep the Activity/ViewModel in the default process. Do not claim this split until the AIDL migration is compiled and device-tested.
  - Reference: `RouterRepository.java`, `RouterService.java`, `I2PdQSTileService.java`, `AndroidManifest.xml`.
- [ ] **[ANDROID-027]** Java 25 build/D8/R8/ART evidence gates.
  - Priority P0; Status Implemented / static; Pending CI compilation; Pending CI tests. The Java 25 probe now dexes every nested record/sealed class (not only the outer class), requires DEX **039** at `--min-api 29`, and keeps a historical DEX 037 transcript only as provenance. `check-config.sh` rejects any older Java release (8/11/17/21/23/24), pins `android.builtInKotlin=false`, checks `--min-api 29` parity, and bans API-30+/33+ collection/String/stream/virtual-thread APIs in app and instrumentation source (unit tests on the build JDK are exempt). The Android job now asserts DEX 039 and the absence of `java/lang/Record`, `ObjectMethods`, `SwitchBootstraps` and `MatchException` in the packaged debug DEX, and runs the JNI symbol parity check. R8 keep rules cover the JNI-looked-up `TorrentSafStorage` class and the SAF broker/AIDL names, although minification remains disabled.
  - Files: `android/tools/java25-probe/{verify-java25.sh,check-config.sh}`, `.github/workflows/build.yml`, `android/app/proguard-project.txt`, `docs/android-java25.md`, `android/tools/check-android-jni-symbols.pl`.
  - Residual: real AGP-selected D8/R8, Lint major-69, API-37 platform resolution, device/API-29 execution and R8-minified behaviour remain manual-CI/device work.
- [ ] **[ANDROID-028]** JNI references, ownership and lifecycle corrections.
  - Priority P0; Status Implemented / static. The three `noexcept` definitions now match the checked-in header; the UTF-16 buffer uses an RAII owner so `ReleaseStringChars` runs even on allocation failure; `setDataDir`/`setRuntimeLogLevel` bound the inbound string before conversion; `NewByteArray` failure clears its pending OOM; the SAF bridge reuses one process-lifetime global class reference instead of leaking one per daemon restart (a class-identity change retains the old bridge for its payloads); `SafEnv.failed` is checked before every provider call; the dead `DaemonAndroidImpl::restart()` bypass was removed; and a compile-free Java↔header↔C++ symbol parity check runs in the Android job.
  - Files: `i2pd_android.cpp`, `DaemonAndroid.{h,cpp}`, `org_openi2pd_router_I2PD_JNI.h`, `android/tools/check-android-jni-symbols.pl`, `.github/workflows/build.yml`, `docs/android-jni-status.md`.
  - Residual: per-call SAF thread attach/detach, transient provider failures still invalidate pieces, stop-path detach latency under a blocked provider, and CheckJNI/device evidence remain open.
- [ ] **[ANDROID-029]** Java 25 code modernization and lifecycle cleanup.
  - Priority P1; Status Implemented / static. `RuntimeAdminPolicy` now returns a typed `ReseedStatus` record (with an explicit `UNKNOWN`) instead of a seven-position `List<String>`; `DaemonWrapper.State.isStartedOkay()` is an exhaustive pattern switch; the `LogLevel` spacing defect is fixed; and the docs no longer claim streams/collectors are used. The existing sealed `RouterCommand` hierarchy and exhaustive service dispatch remain the reference pattern. `TorrentSafStorage`/fragments still perform some main-thread preference/URI work, which is tracked as a residual rather than silently moved.
  - Files: `RuntimeAdminPolicy.java`, `RuntimeAdminFragment.java`, `RuntimeAdminPolicyTest.java`, `DaemonWrapper.java`, `LogLevel.java`, `docs/android-modernization.md`.
  - Residual: main-thread SAF grant persistence, a generic typed admin reply/handoff facade, and owner-scoped result sinks remain follow-ups; no Kotlin/Compose or Java-level reduction.
- [ ] **[ANDROID-030]** Native GUI Material 3, accessibility and tablet completion.
  - Priority P1; Status Implemented / static. `UiFactory` now owns the shared programmatic-screen contract: 48dp minimum touch target, logical padding, a centred `MaxWidthLinearLayout` column (840dp, 1000dp at `w600dp`) and a single-utterance content/state description. Torrents, address book and runtime administration use it. `TorrentRow` is a bounded record decoder for `TORRENTS` (rejects a non-numeric id, drops a malformed hash/magnet, clamps counters/percent, caps text), the actions dialog is titled with the torrent name, and the private/error text joins the TalkBack description. Address rows announce name/destination/blinded; the hard-coded `(blinded)` string is now `address_book_blinded` in all four catalogs. The graceful-stop countdown is no longer a live region. The foreground notification offers Graceful stop or Cancel graceful stop according to the drain state with `setOnlyAlertOnce`. Secondary destinations are hidden while a detail is open and every fragment tag is hidden on destination change. Selecting or detaching a SAF payload tree runs on the service SAF worker with a typed main-thread result instead of `SharedPreferences.commit()` plus provider calls in the picker callback; the torrent session/SAF selection is restored from instance state.
  - Files: `UiFactory.java`, `TorrentRow.java`, `TorrentsFragment.java`, `AddressBookFragment.java`, `RuntimeAdminFragment.java`, `RouterService.java`, `RouterRepository.java`, `RouterViewModel.java`, `MainActivity.java`, `fragment_tunnels.xml`, `values*/strings.xml`, `values-sw600dp/dimens.xml`, `UiContractTest.java`, new `TorrentRowTest.java`.
  - Residual: per-tunnel inventory/accept-transit control, config search/editor beyond the validated subset, two-pane tablet detail layouts, notification live counters, ViewModel-owned traffic history, and no device/TalkBack/visual evidence.
- [ ] **[ANDROID-031]** Manual API 29 emulator UI/accessibility lane.
  - Priority P1; Status Prepared / not executed. `build.yml` gains an opt-in `android_ui` dispatch input and an `android-ui` job that builds x86_64 with vcpkg (API 29), installs `emulator` + `system-images;android-29;google_apis;x86_64` through sdkmanager, creates an AVD, disables animations, runs `connectedDebugAndroidTest`, asserts XML reports exist without `<failure|error>` and uploads reports plus logcat diagnostics. The default dispatch stays arm64-only and no run exists.
  - Files: `.github/workflows/build.yml`.
  - Residual: the workflow has never run; x86_64 vcpkg resolution, emulator boot time, KVM availability and the TalkBack/visual checks are unverified. A current-API lane and SAF fault injection remain future work.

## Documentation and identity consolidation (2026-10-26, static)

- [x] **[DOCS-012]** Consolidate the Markdown technical guides and navigation.
  - Priority P1; Status Implemented / static; Pending manual docs CI. IPC pages now state protocol v5, the per-type registry ceilings, `WorkerHello`/`ConfigureTorrentStorageSupervised`, cancellation/generation fencing, the correct TUI timeouts and the complete torrent verb table. C++23 documents build probes, `i2p::expected` and the `Transports` jthread remainder. The JNI page is marked as a historical snapshot with the 43-symbol checker as authority. OpenBSD `pledge_file` is documented as a fail-closed subset selector. Build-system components, uninstall guidance, Debian docs install and RPM desktop/icon parity are corrected. Cross-links connect IPC, torrent, Android, OpenBSD, packaging and security. Heading structure is fixed in `devs/i2pd-specifics.md`, `tutorials/blinded-addresse.md` and the FAQ.
  - Files: `docs/{security/ipc-protocol.md,tui-ipc.md,torrent/ipc.md,cpp23-migration.md,android-jni-status.md,security/process-capabilities.md,security/openbsd-validation.md,build-system.md,architecture.md,ci.md}`, `contrib/openbsd/README.md`, `debian/{rules,i2pd.install}`, `contrib/rpm/*.spec`.
- [x] **[LEGAL-003]** Add the NOTICE inventory and legal-metadata contract.
  - Priority P0; Status Implemented / static; Pending packaging CI. The initial `NOTICE` audit recorded the project, imported docs, then-CC-marked docs, tools, artwork, Android and bundled-component licences; CMake, Debian and both RPM specs install it. `Win32/Resource.rc2` restores the PurpleI2P + fork `LegalCopyright`; root/Android licenses gain SPDX lines; the then-current AGENTS and docs described the mixed BSD/CC scope. LEGAL-003 supersedes that license split. New `tests/check-legal-metadata.cmake` pins holders, SPDX, NOTICE and install parity.
  - Files: `NOTICE`, `LICENSE`, `android/LICENSE`, `Win32/Resource.rc2`, `build/CMakeLists.txt`, `debian/*`, `contrib/rpm/*`, `docs/about/license.md`, `docs/file-headers.md`, `docs/attribution-audit.md`, `tests/check-legal-metadata.cmake`.
- [x] **[I18N-010]** Publish the four-language policy and pin it.
  - Priority P1; Status Implemented / static; Pending CI. `docs/i18n.md` is the single policy for native `en/es/ru/zh-Hans`, Android `en/es/ru/zh-CN`, English/`zh-TW` fallback, plural arity, translated surface and identifiers; README, docs index and configuration link it. `check-i18n.cmake` pins the tags and links; stale "built-in web/proxy" wording is removed.
  - Files: `docs/i18n.md`, `docs/mkdocs.yml`, `README.md`, `docs/index.md`, `docs/user-guide/configuration.md`, `contrib/i2pd.conf`, `tests/check-i18n.cmake`.
- [x] **[BRAND-013]** Reconcile branding provenance and package visuals.
  - Priority P2; Status Implemented / static; Pending package/device/visual CI. The canonical/mirrored palette gains the full contrast table; `docs/branding/assets.md` stops claiming a separate packaging master; `docs/REBRANDING.md` records `open-i2pd` as a frozen technical spelling; RPM ships the desktop file and hicolor icons like Debian.
  - Files: `assets/branding/colors.md`, `docs/assets/branding/colors.md`, `docs/branding/assets.md`, `docs/REBRANDING.md`, `contrib/rpm/*.spec`.

## Service-definition consolidation (2026-10-26, static)

- [x] **[SERVICE-007]** Harden the packaged systemd unit.
  - Priority P0; Status Implemented / static; Pending CI tests. `LogsDirectory=i2pd`/`LogsDirectoryMode=0700` makes the documented `log = file` opt-in writable under `ProtectSystem=strict`; `KillMode=mixed` is documented as the main-process-only stop signal with cgroup `SIGKILL` only at the deadline; `SyslogIdentifier=open-i2pd` is recorded as a frozen journal-filter identifier. `ExecReload`, `TimeoutStopSec=660`, `Restart=on-failure`, start limits, `UMask=0077`, `LimitNOFILE=8192` and the hardening set are pinned by `check-services`.
  - Files: `contrib/i2pd.service` (with `debian/i2pd.service` symlink), `contrib/tmpfiles.d/i2pd.conf`, `tests/check-services.cmake`.
- [x] **[SERVICE-008]** Reconcile OpenRC, runit, s6/s6-rc, dinit and Upstart.
  - Priority P1; Status Implemented / static; Pending CI tests. OpenRC gains `respawn_period=300`, `rc_ulimit="-n 8192"`, checked `checkpath` calls, required tunnel config/dir and a correct `SIGINT/660` retry form with option-before-name `supervise-daemon` calls. runit gains a bounded `chpst -o 8192`, account/config preconditions, a five-second `finish` and a `log/config` rotation policy; no `down-signal` is shipped because runsv has no per-service timeout. s6/s6-rc gain `s6-softlimit -o 8192`, `finish` and `timeout-finish 10000`, with argv parity enforced. dinit/Upstart sample comments and `console log` are corrected.
  - Files: `contrib/openrc/*`, `contrib/runit/*`, `contrib/s6/*`, `contrib/s6-rc/*`, `contrib/dinit/i2pd`, `contrib/upstart/i2pd.upstart`, `build/CMakeLists.txt`.
- [x] **[SERVICE-009]** Make the SysVinit stop/reload contract robust.
  - Priority P1; Status Implemented / static; Pending CI tests. `force-reload` is now a real stop/start (LSB), `reload` remains `SIGHUP`; the PID is compared with `/proc/<pid>/exe` (stripping ` (deleted)`) before signalling; `stop` re-checks `is_running` after `--oknodo`; `DAEMON_OPTS` splitting disables pathname expansion; `Should-Start: $syslog`, minimal LSB `log_*` fallbacks and `yes|true|1` enablement are added.
  - Files: `contrib/sysvinit/i2pd`, `debian/i2pd.init` symlink, `contrib/sysvinit/README.md`.
- [x] **[SERVICE-010]** Correct Shepherd and OpenBSD stop semantics and the port template.
  - Priority P1; Status Implemented / static; Pending CI tests. Shepherd uses a 660-second grace period and documents its process-group signal. OpenBSD rc.d adds `rc_stop_signal=INT` and `daemon_execdir`, so `rcctl stop` reaches the drain under `daemon_timeout=660`; `rc_bg=YES` is documented as non-readiness. The port installs mirrored `share/examples/etc/...` and `share/examples/var/i2pd`, drops dead certificate samples, keeps config root-owned, creates `/var/i2pd` 0700 and ships `LICENSE`/`NOTICE`/tools licence; DESCR stops claiming unbuilt tools.
  - Files: `contrib/shepherd/i2pd.scm`, `contrib/openbsd/{i2pd.rc,README.md,port/*}`, `docs/security/openbsd-validation.md`.
- [x] **[SERVICE-011]** Preserve crash and exception exit semantics.
  - Priority P0; Status Implemented / static; Pending CI compilation; Pending CI tests. `SIGABRT` is removed from the blocked/dispatched set so an abort keeps its default disposition instead of becoming an orderly exit 0; an exception escaping `init`/`start`/`run`/`stop` is caught in `main`, logged as a non-secret fatal error and returns `EXIT_FAILURE`.
  - Files: `daemon/UnixDaemon.cpp`, `daemon/i2pd.cpp`, `docs/user-guide/run.md`.
- [x] **[SERVICE-012]** Extend the compile-free service contract and manual install matrix.
  - Priority P0; Status Implemented / static; Pending CI tests. `tests/check-services.cmake` now asserts every unit/script token (signals, budgets, users, paths, modes, retry forms, finish/backoff, log policy, exact `down-signal`/`timeout-kill`/`timeout-finish` values), s6/s6-rc argv equality, the `base` dependency marker, Debian/contrib identity, the manager matrix and the documentation caveats. `tests/check-build-system.cmake` and `check-packaging.cmake` pin the new installed files and port samples. The manual packaging workflow gains `service-install-matrix`, which configures each manager, installs only the `Service` component and rejects the dinit/Upstart samples; `build.yml` runs `check-services`.
  - Files: `tests/check-services.cmake`, `tests/check-build-system.cmake`, `tests/check-packaging.cmake`, `.github/workflows/{build,packaging}.yml`, `docs/service-management.md`.
- [ ] **[SERVICE-013]** Remaining service-side validation and policy.
  - Priority P2; Status Pending. No manager start/stop, `systemd-analyze verify`, `rcctl`, `s6-rc-compile`, `herd`, `pkg_create` or device/VM run exists for the current tree. Open decisions: Windows service registration, RPM upgrade-restart policy, Shepherd respawn-rate limiting (no built-in burst limit), an s6 syslog dependency/pipeline, dinit log rotation, hardening additions (`PrivateDevices`, `SystemCallFilter`, `ProtectProc`) only with CI evidence, and a genuine OpenBSD port/`pkg_add` lane.

## Packaging consolidation (2026-10-26, static)

- [x] **[PKG-020]** Align CMake component payload with every packager.
  - Priority P0; Status Implemented / static; Pending CI tests. `Documentation` installs `ChangeLog`, `LICENSE`, `NOTICE`, `README.md`, `LICENSE.i2pd-tools` (renamed) and the filtered docs/branding trees. The standalone CC-BY-SA notice and imported-docs `LICENSE.i2pd-docs-en` file have been removed repository-wide; all project documentation is BSD-3-Clause under LEGAL-003. Man pages moved to `Runtime`/`Tui`/`Tools` so `I2PD_INSTALL_DOCS=OFF` no longer drops them. A service manager combined with `I2PD_INSTALL_CONFIG=OFF` is rejected at configure time.
  - Files: `build/CMakeLists.txt`, `tools/CMakeLists.txt`, `tests/check-build-system.cmake`.
- [x] **[PKG-021]** Correct the Debian manifests, rules and control metadata.
  - Priority P0; Status Implemented / static; Pending CI tests. `i2pd.install` claims the actual `debian/tmp` docs, branding and man pages, plus the tools licence instead of the unfiltered source trees; `i2pd-tui.install` owns its man page and the duplicate source-tree copies were removed. `rules` pins `I2PD_INSTALL_CONFIG=ON`, `I2PD_OPENSSL_EXPECT=OpenSSL`, honors `DEB_BUILD_OPTIONS=parallel=N` and runs `dh_apparmor`; `control` adds `dh-apparmor`, `dh-sequence-installsysusers` and the `Breaks`/`Replaces` split.
  - Files: `debian/{control,rules,i2pd.install,i2pd-tui.install}`, removed `debian/i2pd.1`/`i2pd-tui.manpages`.
- [x] **[PKG-022]** Make upgrades preserve data and avoid root-owned state.
  - Priority P0; Status Implemented / static; Pending CI tests. New `debian/i2pd.preinst` moves a real `/var/lib/i2pd/certificates` directory to `.dpkg-old` before the symlink is unpacked. `postinst` repairs only a root-owned, otherwise empty state/log/runtime directory and a root-owned log file; administrator modes are untouched. The static Debian package drops the removed config/tunnel links and gains the same repair logic.
  - Files: `debian/{i2pd.preinst,postinst}`, `contrib/static/debian/{i2pd.links,i2pd.postinst}`, `debian/NEWS`, `debian/changelog`.
- [x] **[PKG-023]** Load and scope the AppArmor profile.
  - Priority P1; Status Implemented / static; Pending CI tests. The profile gains `flags=(attach_disconnected)`, `mixr` for the worker re-exec, `/var/lib/i2pd/** rwk`, log-directory access and `/run/i2pd/*.sock`; `dh_apparmor` loads/reloads it and the Debian test parses it with `apparmor_parser -Q`. The sysusers policy is installed through `dh_installsysusers`.
  - Files: `contrib/apparmor/usr.bin.i2pd`, `debian/i2pd.apparmor` (removed), `debian/i2pd.sysusers`, `debian/tests/smoke`.
- [x] **[PKG-024]** Modernize the OpenBSD port layout.
  - Priority P1; Status Implemented / static; Pending CI tests. `SEPARATE_BUILD` with `WRKBUILD=${WRKDIST}/obj`, `-DI2PD_OPENSSL_EXPECT=LibreSSL`, CMake-owned man pages, `share/examples/var/i2pd/subscriptions.txt`, `@newuser` login class `i2pd`, `pexp` in rc.d, `daemon_execdir` comment, root-owned configuration and `/var/i2pd` 0700. The port packages preserved license metadata and its READMEs document `doas -u _i2pd i2pd-tui`.
  - Files: `contrib/openbsd/{i2pd.rc,i2pd.conf,README.md,port/*}`.
- [x] **[PKG-025]** Synchronize RPM, static and container resources.
  - Priority P1; Status Implemented / static; Pending CI tests. Both RPM specs rename `LICENSE`/`README` from tools instead of colliding under `%doc`, and list the desktop/icons in the release spec. The static Debian manifest gains `LICENSE` and the CMake man page; the container image ships `LICENSE`/`NOTICE` and no longer fails on a persisted certificate link. Standalone CC-BY-SA notice installation has been removed from all package formats.
  - Files: `contrib/rpm/*.spec`, `contrib/static/i2pd.spec`, `contrib/static/debian/*`, `contrib/docker/*`.
- [x] **[PKG-026]** Extend the packaging contracts.
  - Priority P0; Status Implemented / static; Pending CI tests. `check-packaging` pins the CMake→manifest paths, rules flags, `Breaks`/`Replaces`, preinst migration, sysusers, AppArmor rules, autopkgtest coverage, DEP-5 stanzas, OpenBSD build variables and RPM `%files`. `check-legal-metadata` pins the CMake tools licence rename, absence of standalone CC-BY-SA notice files, static docs and RPM notices. The packaging workflow validates the profile/desktop entry, docs/man payload, tmpfiles modes and split.
  - Files: `tests/check-{packaging,legal-metadata}.cmake`, `.github/workflows/{build,packaging}.yml`, `debian/tests/{control,smoke}`.
- [ ] **[PKG-027]** Remaining packaging validation and decisions.
  - Priority P2; Status Pending. No `dpkg-buildpackage`, `dh_missing`, lintian, autopkgtest, RPM build, static-package install, container runtime, `pkg_create`/`pkg_add` or OpenBSD port build exists for this tree. Open items: decide whether to split `i2pd-tools` from the daemon package, add an RPM and OpenBSD port lane to the manual workflow, and verify the AppArmor profile on an enforcing kernel.

## Test integration and manual CI (2026-10-26, static)

- [x] **[TEST-024]** Give every CTest target a label, timeout and parallel-safety contract.
  - Priority P0; Status Implemented / static; Pending CI tests. `i2pd_add_test()` sets 120 s normally and 300 s under any sanitizer; forked sandbox/credential/storage-worker tests are `RUN_SERIAL`; the fixed-`/tmp` tests share `openi2pd-fixed-tmp`. Label gaps for `privsep`/TUI IPC, tunnel restart/replay, torrent DHT/PEX/scheduler and documentation/i18n tests are corrected, and compile-free checks carry `ci`.
  - Files: `tests/CMakeLists.txt`.
- [x] **[TEST-025]** Restore assert coverage and timeout bounds in tools/fuzz targets.
  - Priority P0; Status Implemented / static; Pending CI tests. `test-tools-getopt` and the libFuzzer harnesses receive `-UNDEBUG`; the opt-in Perl tests no longer inherit the 1500 s default; the Windows AF_UNIX control-socket stub returns 77 with `SKIP_RETURN_CODE`; fuzz smoke entries use the 25 s/30 s budget.
  - Files: `tools/CMakeLists.txt`, `tests/CMakeLists.txt`, `tests/test-i2pcontrol-socket.cpp`.
- [x] **[TEST-026]** Add a reproducible provider-independent X25519 known-answer regression.
  - Priority P1; Status Implemented / static; Pending CI compilation/tests. `test-x25519-rfc7748` pins RFC 7748 section 6.1 public keys and shared secret through the production `X25519Keys` API, with labels `i2p;security;unit`.
  - Files: `tests/test-x25519-rfc7748.cpp`, `tests/CMakeLists.txt`, `docs/testing.md`.
- [x] **[TEST-027]** Make CTest selection and evidence fail closed in every workflow.
  - Priority P0; Status Implemented / static; Pending CI tests. Executing `ctest` calls use `--no-tests=error`, `--output-junit`, and `-j 2` (release/platform) or `-j 1` (sanitizers); JUnit/`Testing` artifacts are uploaded. Linux/sanitizer builds require Perl. The Windows MSVC lane builds/runs both privsep Windows targets, and OpenBSD archives `configure.log`.
  - Files: `.github/workflows/{build,build-windows,build-openbsd,fuzz}.yml`.
- [x] **[TEST-028]** Fix failure-hiding/false-pass shell pipelines in the Android lane.
  - Priority P1; Status Implemented / static; Pending CI tests. `javap` and DEX `strings` checks write to temporary files before `grep`, removing SIGPIPE false failures/passes; the unbounded `adb wait-for-device` has a 180 s timeout; the duplicated optional `perlcritic` block is removed from the Linux job.
  - Files: `.github/workflows/build.yml`.
- [x] **[TEST-029]** Guard manual-only workflows and empty CTest selections statically.
  - Priority P0; Status Implemented / static; Pending CI tests. New `tools/scripts/check-workflows.pl` rejects triggers other than `workflow_dispatch`/`workflow_call`, `continue-on-error`, and executing ctest lines without `--no-tests=error`/`--output-junit`; it runs as CTest `workflow-static` and in `skills-static`, and the fuzzer list must appear in `docs/testing.md`.
  - Files: `tools/scripts/check-workflows.pl`, `tests/CMakeLists.txt`, `tools/CMakeLists.txt`, `tools/tests/perl-portability.t`, `tests/check-build-system.cmake`, `.github/workflows/build.yml`, `docs/{ci,testing}.md`.
- [x] **[TEST-030]** Document the real labels, budgets and fuzzer inventory.
  - Priority P2; Status Implemented / static. `docs/testing.md` now lists `cli`/`integration`/`workflow`/`reference`/`critic`/`binary`, the `RUN_SERIAL`/resource-lock policy, all fifteen fuzzers and the RFC 7748 KAT; `docs/ci.md` documents the workflow guard, `-j`, JUnit, `--no-tests=error` and the corrected `perl_style` input name.
  - Files: `docs/testing.md`, `docs/ci.md`.
- [ ] **[TEST-031]** Remaining coverage and dynamic validation.
  - Priority P2; Status Pending. No configure/build/CTest/sanitizer/fuzzer/workflow dispatch exists for this tree. High-value open regressions: SSU2 long-header/AEAD/clock-skew, NTCP2 data-block ordering, tunnel layer/build-time windows, valid signed SU3, ECIES DateTime window, netDb freshness/clock injection, privsep violation budget/cancellation/router-storage restart, torrent tracker deadline/DHT purge/remove-all, and Android JNI/SAF device tests. Fuzzer library instrumentation and dedicated NTCP2/SSU2 harnesses also remain open.

## Security fuzzing and negative coverage (2026-10-26, static)

- [x] **[TEST-032]** Make fuzzing reproducible with checked-in corpora and per-target bounds.
  - Priority P0; Status Implemented / static; Pending CI tests. `tests/fuzz/corpus/<target>/` holds one small non-secret seed per target (15 targets, ≤64 KiB each); CMake smoke entries pass the seed directory, a per-target `-max_len` and a per-target artifact directory. `check-build-system.cmake` rejects orphan/empty corpora, oversized seeds, credential-like names, a missing corpus and a fuzzer listed only once in `fuzz.yml`.
  - Files: `tests/fuzz/corpus/**`, `tests/CMakeLists.txt`, `tests/check-build-system.cmake`.
- [x] **[TEST-033]** Add NTCP2, SAM, I2CP and I2NP database fuzz harnesses.
  - Priority P1; Status Implemented / static; Pending CI tests. New `fuzz-ntcp2-blocks` (SessionConfirmed/padding), `fuzz-sam-control` (HELLO/params/frame), `fuzz-i2cp-message` (SEND parser) and `fuzz-i2np-database` (DatabaseSearchReply) run under the existing ASan+UBSan fuzzer flags with `-fno-sanitize-recover=all`.
  - Files: `tests/fuzz/fuzz-{ntcp2-blocks,sam-control,i2cp-message,i2np-database}.cpp`, `tests/CMakeLists.txt`, `.github/workflows/fuzz.yml`, `docs/testing.md`.
- [x] **[TEST-034]** Add a pure SSU2 negative block-framing regression.
  - Priority P1; Status Implemented / static; Pending CI compilation/tests. `test-ssu2-payload-blocks` covers DateTime/termination sizes, padding/termination ordering, truncated headers/overruns and per-state block whitelisting through `ValidateSSU2PayloadBlocks`; `test-i2np-database-bounds` pins DatabaseSearchReply truncation/count/trailing-byte behavior.
  - Files: `tests/test-ssu2-payload-blocks.cpp`, `tests/test-i2np-database-bounds.cpp`, `tests/CMakeLists.txt`, `docs/testing.md`.
- [x] **[TEST-035]** Add a UBSan-only lane and fail-closed fuzz environment.
  - Priority P0; Status Implemented / static; Pending CI tests. The sanitizer matrix gains `ubsan`; fuzz jobs set `ASAN_OPTIONS`/`UBSAN_OPTIONS` at job level (leak detection off), per-target logs/steps/summary, per-target artifact prefixes, seeded corpora and explicit `-max_len`. MSVC keeps its CTest per-test timeouts.
  - Files: `.github/workflows/{build,fuzz,build-windows}.yml`.
- [ ] **[TEST-036]** Remaining security coverage and dynamic evidence.
  - Priority P2; Status Pending. High-value open regressions: SSU2 long-header/AEAD-no-reply/ACK/clock-skew/token/receive-window, NTCP2 SessionRequest/data-block decision, Ratchet NEXT_KEY/inbound-tag edges, netDb store-cap accounting/RouterInfo address snapshots, tunnel IV/gateway/1028-byte framing, SAM secret redaction/subsession/write-queue bounds, I2CP CreateLeaseSet2 atomicity/Destroy-ID/key-publication race, torrent tracker deadline/DHT queue policy/PEX window/SAF missing document, JNI host string bounds, privsep violation budget/cancel-no-replay, and Android SAF retire-generation. No fuzzer/sanitizer/workflow/device run exists for this tree.

## Source documentation and audit continuation (2026-10-26, static)

- [ ] **[DOCS-013]** Review and document C++23, Java 25, Perl and shell source contracts progressively.
  - Priority P1; Status Partially implemented / static; Pending CI compilation; Pending CI tests. The permanent source documentation policy is in `AGENTS.md`; conventions, Doxygen/Javadoc/POD/shell rules and the manual generation lane are in `docs/source-documentation.md`. The 2026-10-26 pass documented transports, crypto/netDb, IPC/daemon, torrent engine, client services, TUI/tools, Android service/SAF/JNI, 12 Perl scripts and 18 service/packaging scripts with contract-focused comments. Reviewed interfaces and security boundaries receive source comments; the optional manual `source_docs` lane prepares Doxygen, Android Javadoc/doclint and a TODO candidate artifact. Continue through all unreviewed files and reconcile comments with implementation; external specification links, full API coverage and toolchain integration remain unverified.
- [ ] **[DOCS-014]** Reconcile source TODO/FIXME/XXX markers with tracked work.
  - Priority P2; Status Pending. Static text inspection found existing candidates in transport/streaming, client configuration, torrent RPC placeholders, crypto, SAM and tools. Review each against `TODO.md`, distinguish obsolete notes and string literals from real work, and add stable task references where appropriate. The CI candidate collection is not proof that all markers are tracked.
- [x] **[DOCS-015]** Repair source-comment defects found during the documentation pass.
  - Priority P0; Status Implemented / static; Pending CI compilation. `libi2pd_client/HTTPProxy.cpp` had a literal `*/` inside the `X-Forwarded-*` prose that closed the block early and left the following statements parsed as comment text; the block is restored and the header list documented without `*/`. `libi2pd/FS.h` `@param` names now match `cmdline_datadir`/`cmdline_certsdir`; the unnamed `Log::Append` parameter no longer carries an impossible `@param`; the Android manifest now attaches the router-owner explanation to `RouterService`, not the separate SAF broker.
  - Also corrected checker drift exposed by the pass: `check-build-system` now restricts Crypto.h fallback checks to `I2PD_OPENSSL_PROBES` (the OpenBSD `imsgbuf_get` probe is not a crypto capability), `check-control-security` matches the multiline `RequestDestinationWithEncryptedLeaseSet` call, and `android/LICENSE`/`tools/LICENSE` carry the `BSD-3-Clause` SPDX identifier the legal checker expects; a Shepherd comment was reworded so the forbidden wait-loop scan does not match prose.
  - Files: `libi2pd_client/HTTPProxy.cpp`, `libi2pd/FS.h`, `libi2pd/Log.h`, `android/app/src/main/AndroidManifest.xml`, `android/LICENSE`, `tools/LICENSE`, `tests/check-{build-system,control-security}.cmake`, `docs/source-documentation.md`, `openi2pd-documentation`/C++/Android/TUI/tooling/packaging skills, `tests/check-agent-skills.cmake` (policy phrases).
  - Validation: `git diff --check`, source inspection, `check-agent-skills`, `check-doc-links`, `check-doc-nav`; no Doxygen/Javadoc/compiler run. Documentation generation remains Pending CI.
- [ ] **[CONC-028]** Documentation review lifetime/threading follow-ups.
  - Priority P1; Status Pending. `Streaming::ReadSome()` mutates the receive queue from the caller thread while the rest of `Stream` is service-thread-only (`libi2pd/Streaming.h`); `UDPTunnel` ACK-timer and `TorrentsTunnel` magnet/metadata posts capture raw `this`; `Daemon::StopPrivsepWorkers` does not stop `dataStorage` started by `init()`; `Win32NetState` can double-Release COM pointers and reports online on a disconnected result. Define owner/generation barriers and cleanup before changing callers; add lifetime/restart regressions.
- [ ] **[SEC-032]** Documentation review validation/state follow-ups.
  - Priority P1; Status Pending. `Reseed` counts unparsable certificates as loaded and never revokes a removed trust anchor until restart; `SignRedDSA` ignores `EVP_Digest*` failures; `I2PTunnel` IRC path dereferences a null `m_From` identity; a duplicate SAM `SESSION CREATE` returns a shared session whose temporary acquire is never released and can be torn down by the second socket. Confirm each contract against Java 2.14.0 and add targeted negative regressions before altering behaviour.
- [x] **[SEC-033]** Prevent Streaming NACK-count wraparound before the 8-bit wire field.
  - Priority P1; Status Implemented / static; Pending CI compilation; Pending CI tests. `Streaming.cpp` accumulated `numNacks` in `uint8_t` and then compared it with `>= 256`; the counter wrapped, so the saved-packet cap could be bypassed and `m_SavedPackets` could grow. Both accumulation loops now use `size_t` and cast once at the one-byte serialization point after the `< 256` guard. No wire format change. Prepare a NACK-gap/saved-packet bound regression in `test-streaming-*`; no test was executed.
  - Files: `libi2pd/Streaming.cpp`, pending regression.
- [x] **[PRIVSEP-032]** Close the OpenBSD imsg channel on every failed send after enqueue.
  - Priority P1; Status Implemented / static; Pending OpenBSD CI compilation/tests. `ImsgChannel::Send` previously closed only on timeout; `poll` failure, `POLLNVAL`, `POLLERR`/`POLLHUP` and a hard `imsgbuf_write` error left a partially queued frame and a live channel, so a later send could replay a mutation. All post-enqueue failure paths now `Close()` before returning. The `MAX_IMSGSIZE` ceiling, fd passing and connection-scoped credentials are unchanged. Prepare an OpenBSD imsg send-failure/no-replay regression when the imsg test fixture can simulate poll/write failure.
  - Files: `privsep/OpenBSDImsg.cpp`, `tests/test-privsep-openbsd-imsg.cpp` (pending case).
- [ ] **[TOOL-018]** Documentation review tooling/checker follow-ups.
  - Priority P2; Status Pending. `i2pdctl` documentation says HTTPS-only while the client accepts explicit `http://`; `check-vectors.pl` lists cases before decoding fixtures and does not fail on `UNEXPECTED-PASS`; `check-i18n-catalogs.pl` unescapes `\n` before `\`; `maintenance-checkers.t` maps signal death to exit 0; `famtool` installs certificate and key non-transactionally; `TerminalWin32::Put` compares a byte offset with a column budget. Fix and pin with compile-free/targeted regressions where possible.
- [ ] **[WIN-014]** Correct Win32 tray and network-state defects reported by the documentation pass.
  - Priority P2; Status Pending. `Win32App.cpp` assigns `nid.uFlags` twice (the `notify` branch is dead) and calls `NIM_DELETE` with an uninitialised `NOTIFYICONDATA`; `Win32NetState.cpp` ignores `CoInitialize` failure, does not null released sink pointers and can report the router online from a disconnected result. These are bounded fixes but need a Windows runtime lane; add a reviewer note and keep the AppContainer/service split unchanged.
- [ ] **[PKG-028]** Documentation review service/packaging follow-ups.
  - Priority P2; Status Pending. `contrib/static/debian/i2pd.postinst` returns 0 for an unknown phase (and has no `triggered)` case), leaves `$I2PDUSER` unquoted and only repairs a missing group when the user already exists; `contrib/upstream/sync.sh --docs` is only honoured as the first option; `build-musl.sh` pins neither the Alpine image digest nor a repository snapshot. Review upgrade/purge safety and reproducibility before changing scripts.
- [ ] **[SEC-030]** Reject truncated tunnel fragment and I2NP headers before reading fields.
  - Priority P0; Status Implemented / static; Pending CI compilation; Pending CI tests. `TunnelEndpoint.cpp` checks the fragment header width and assembled I2NP header before reading them; `test-tunnel-endpoint-fragments` prepares truncation cases. Review with ASan/UBSan in manual CI.
- [ ] **[CONC-015]** Audit ratchet remote-static-key snapshot lifetime.
  - Priority P1; Status Pending. `ECIESX25519AEADRatchetSession::GetRemoteStaticKey()` returns an internal pointer after releasing the state mutex while the setter can modify it, and key type and bytes are read separately. Establish whether publication is immutable or return a single locked snapshot; prepare a race regression.
- [ ] **[CONC-016]** Review queue wakeup and timed-wait semantics.
  - Priority P1; Status Pending. `Queue::Wait(sec,usec)` waits without a predicate and can time out with queued items; `NetDb::Run` and `Tunnels::Run` process only after a non-timeout return. The `usec` parameter is used as milliseconds. Define shutdown/wakeup and time-unit contracts before changing callers; prepare lost-notification and spurious-wakeup regressions.
- [ ] **[TORRENT-013]** Reject wrong element types in bencoded string lists.
  - Priority P1; Status Implemented / static; Pending CI compilation; Pending CI tests. `ParseStringList` rejects a complete list when any item is not a byte string rather than silently omitting it. Prepared regressions cover mixed metainfo paths, bencode lists and DHT `values`; compatibility with peers sending invalid mixed lists remains pending CI.
- [ ] **[CONC-017]** Bound and close accepted torrent RPC sessions.
  - Priority P0; Status Pending. The loopback listener in `TorrentsRPC.cpp` has no observed per-session deadline or global admission quota, the accept handler captures raw `this`, and `Stop()` does not explicitly close accepted sessions. Define owner/admission and shutdown barriers; prepare slow-client and stop-with-pending-callback regressions.
- [ ] **[CONC-018]** Fence torrent magnet tracker publication across removal and shutdown.
  - Priority P1; Status Pending. `TorrentsTunnel::AddMagnet()` posts a raw-`this` tracker callback without the stop/membership checks used by other paths. Review owner lifetime and restart generation, and cover an undrained queue and removal before dispatch.
- [ ] **[CONC-019]** Audit I2PControl self-thread shutdown lifetime.
  - Priority P1; Status Pending. `I2PControlService::Stop()` on its service thread detaches and leaves cancellation handlers queued on a stopped context; review raw callback ownership, destruction and restart semantics and add a self-stop regression.
- [ ] **[ANDROID-032]** Bound direct SAF creator operations against non-cooperative providers.
  - Priority P1; Status Pending. `TorrentSafCreator` reads on `safWorker` directly; its cancellation deadline cannot force a provider that ignores cancellation to return, and the broker watchdog does not cover this path. Define bounded process recovery and test a provider that ignores cancellation.
- [ ] **[ANDROID-033]** Make persisted router-state transitions atomic across readers and writers.
  - Priority P1; Status Pending. Some `RouterService` transitions compute a new `RouterRunState` before acquiring `runStateLock`; `save()` does not reject an older generation. Audit the interleavings and prepare a stale-transition regression.
- [ ] **[ANDROID-034]** Review Android exception redaction before publishing snapshots.
  - Priority P1; Status Pending. `RouterService.describe()` includes bounded throwable stack traces and messages in snapshot error text, which may include paths or provider data. Define a safe error mapping and a negative secret/path-redaction regression.
- [ ] **[ANDROID-035]** Fence SAF completion publication after service destruction.
  - Priority P1; Status Pending. An in-flight provider operation can post progress or completion after `onDestroy()` removes messages already queued on the handler. Define a shared generation/admission barrier and prepare a destruction-versus-provider regression.
- [ ] **[CONC-020]** Retain ratchet session authority for reordered streaming packets.
  - Priority P0; Status Pending. `Packet::from` is a borrowed ratchet-session pointer; a packet saved in `Stream::m_SavedPackets` can be processed after the owning Garlic destination expires the session. Audit the FROM/CLOSE/RESET option paths and prepare an out-of-order packet/session-removal regression before changing ownership.
- [ ] **[CONC-021]** Remove synchronous reads from I2P tunnel connection handling.
  - Priority P1; Status Pending. `I2PTunnelConnection::I2PConnect` performs a synchronous `boost::asio::read` after `available()`; the availability check provides no read deadline. Verify executor ownership, replace it with bounded asynchronous I/O and cover a peer that stalls or closes mid-read.
- [ ] **[CONC-022]** Fence SAM and I2CP shutdown callbacks across timeout and restart.
  - Priority P1; Status Pending. `SAMBridge::Stop` and `I2CPServer::Stop` post callbacks capturing raw `this`; their timed-wait fallback can stop the context with those callbacks still queued. Define lifetime and generation authority through shutdown/restart and prepare a queued-callback timeout regression.
- [ ] **[SEC-031]** Reject unsafe SysV pidfile values and executable checks.
  - Priority P0; Status Implemented / static; Pending CI tests. The no-helper fallback rejects zero and non-canonical PID strings before `kill`; when procfs exposes an executable, failure to resolve the configured daemon rejects the match. `check-services.cmake` pins the two failure paths. PID reuse between comparison and signal, and hosts without readable procfs, still need platform-specific review.
- [ ] **[CONC-023]** Establish a shared admission barrier for `ServiceAcceptor` shutdown.
  - Priority P0; Status Pending. `I2PService::AddHandler` inserts under `m_HandlersMutex` without checking `m_Running`, and an already completed accept callback may publish or call `Accept()` after `Stop()` resets the acceptor. Coordinate admission, listener lifetime and restart generation; prepare a stop-versus-accept regression.
- [ ] **[CONC-024]** Audit `SocketsPipe` destruction without prior termination.
  - Priority P1; Status Pending. Its destructor invokes `Terminate()`, which can call `shared_from_this()` when `Kill()` has not already marked the pipe; destruction after the final owner is gone may throw `bad_weak_ptr`. Define explicit teardown ownership and prepare a regression for an unstarted or unterminated pipe.
- [ ] **[CONC-025]** Audit Garlic session owner lifetime across shutdown.
  - Priority P1; Status Pending. `GarlicRoutingSession::m_Owner` is an atomic raw pointer; some call sites load it twice around a null check while cleanup can clear it. Establish destruction and callback barriers, then prepare a shutdown-versus-session-access regression; atomic publication alone does not retain the owner.
- [ ] **[CONC-026]** Make local-destination creation atomic for duplicate keys.
  - Priority P1; Status Pending. `ClientContext::CreateNewLocalDestination(keys)` looks up under the map lock, creates after releasing it, then inserts separately; concurrent callers may return a newly created loser that was not registered or started. Recheck the insertion result under a shared publication contract and prepare a concurrent duplicate-key regression.
- [ ] **[SERVICE-014]** Validate systemd logging selection and precedence.
  - Priority P2; Status Pending. The packaged unit passes `--log=stdout`; the file-log instructions now require replacing `ExecStart` as well as setting `log = file`. Verify command-line/config precedence and the drop-in on a manual systemd run; no manager was started during this review.
- [ ] **[CONC-027]** Retain `ClientDestination` authority through timed shutdown.
  - Priority P0; Status Pending. `ClientDestination::Stop()` posts a callback capturing raw `this` and waits at most ten seconds; timeout can leave it queued after the caller proceeds toward destruction. Define stop/restart ownership and cancellation semantics and prepare a timeout with later queue execution regression.

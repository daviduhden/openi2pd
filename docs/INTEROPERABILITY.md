<!--
See the LICENSE file at the top of the project tree for copyright
and license details.

openi2pd: INTEROPERABILITY.

Documents project documentation.

-->

# I2P Java interoperability audit

> Historical interoperability record. “Local CTest passed” cells describe the capture date and are not current evidence; use the manual CI matrix and `tests/interop/FIXTURES.md` for the active target.

Current comparison target and historical audit provenance:

| Item | Value |
|---|---|
| Repository | https://github.com/i2p/i2p.i2p |
| Current target tag | `i2p-2.14.0` |
| Target commit (peeled tag) | `3c98503c5f14efe7bdd783528ba7f621f12d8081` |
| Tag object | `9db233ed7241599eba76f29b8c334eaaf3db2e87` |
| Pin verification | Official remote tag refs inspected with `git ls-remote`; scoped 2026-10-21 source review completed for selected router/tunnel, admin, address-book, client-service and torrent candidates; full wire re-audit pending |
| Historical audited tag | `i2p-2.13.0` |
| Historical audited commit | `9134f808337b401e8e53c73734c81fab04280c9d` |
| Reference use | External read-only sources; not bundled or built |
| Harness recheck | 2026-10-20: previous 26 cases re-reviewed statically; the new parity regressions are prepared; the pinned checkout and differential runner were not executed |

The matrices, Java method references and findings below record historical
2.13.0 source review unless a section explicitly states otherwise. A scoped
2026-10-21 read-only review checked selected 2.14.0 sources for router/tunnel,
administration, address-book, client-service and torrent candidates, but the
full matrix and wire evidence have not been migrated. Historical claims that
no newer stable tag existed are superseded by the verified 2.14.0 target above.
Existing test names and historical validation records remain tied to their
original revisions; they provide no 2.14.0 validation.

The Java tree is **not** vendored and is not a build/runtime dependency of
openi2pd. No Java code was modified.

Functional and administrative coverage (TUI, Android, services, torrents and
resource limits) is tracked separately in
[`FEATURE-PARITY.md`](FEATURE-PARITY.md). This document remains the
wire-level/interoperability evidence matrix; neither document claims dynamic
validation without a manual CI or device run.

Normative specifications: <https://i2p.net/en/docs/specs>.

Status vocabulary:

- **Compatible (static)** — behavior matches by code/spec inspection.
- **Fixed (validation pending)** — C++ changed to match; dynamic test missing.
- **Optional / not implemented** — feature not required for the protocols this
  fork announces.
- **Needs dynamic validation** — cannot be concluded from source alone.
- **Not evaluated** — outside this audit pass.

## Matrix

| Area | Specification | I2P Java reference | openi2pd reference | Java behavior (static) | openi2pd behavior | Difference | Interoperability consequence | Priority | Change / tests | Validation |
|---|---|---|---|---|---|---|---|---|---|---|
| NTCP2 version negotiation | `spec/ntcp2` | `router/.../transport/ntcp/NTCPTransport.java`, `InboundEstablishState.java`, `OutboundNTCP2State.java` | `libi2pd/NTCP2.cpp` `SetVersion`, SessionRequest/Confirmed | `NTCP2_INT_VERSION=2`, `PQ_INT_VERSION=4`; the address publishes `v=2` plus `pq=N` (N=3..5) when the provider supports ML-KEM, and accepts 2–5 internally | Can accept 3–5, but configured version 3 is clamped to 2; no-PQ builds publish no `pq` | None found; i2pd supports 5 beyond Java's 4 but negotiates down | Compatible when PQ availability matches; otherwise version 2 | P0 | Existing version tests; PQ publication gated | Needs dynamic PQ/non-PQ session test |
| NTCP2 handshake/frames | `spec/ntcp2` | `OutboundNTCP2State`, `InboundEstablishState`, `NTCP2Payload` | `libi2pd/NTCP2.cpp` | X25519/ML-KEM, obfuscation, length bounds, 65516 max message | Same structure; separate implementation; replayed SessionRequest rejection, strict SessionConfirmed block order, 4048-byte msg3p2 cap and data-phase ordering now enforced | Replay/cap/ordering gaps fixed; termination-count and DateTime handling aligned | Compatible (static, validation pending) | P0 | NET-118..NET-123 and NET-134..NET-140; `test-ntcp2-session-confirmed-payload`/`test-ntcp2-handshake-bounds` passed in the local 48-test CTest | Needs dynamic session/replay/PQ test |
| SSU2 version/options | `spec/ssu2` | `router/.../udp/UDPTransport.java`, `PacketBuilder2.java`, `InboundEstablishState2.java` | `libi2pd/SSU2*.cpp` | `SSU2_INT_VERSION=2`; publishes `v=2` plus `pq` for PQ | Publishes `v=2` plus `pq` 3/4; LibreSSL server advertisement now maps unsupported 3 to 2 | C++ could advertise PQ v3 under LibreSSL even though sessions fall back to v2 | Fixed static; no invalid capability advertisement | P0 | `SSU2Server::SetVersion` mapping; validate LibreSSL publication | Fixed (validation pending) |
| SSU2 handshake/tokens/relay | `spec/ssu2` | `OutboundEstablishState2`, `InboundEstablishState2`, `PacketBuilder2` | `libi2pd/SSU2Session.cpp` | Retry/token, relay request/response, peer test blocks | Equivalent blocks and state machine; PathResponse length now includes its 3-byte block header | None found beyond bounds fix | Compatible (static) | P0 | Existing TUI/status only | Needs relay/peer-test dynamic test |
| SSU2 fragmentation | `spec/ssu2` | `InboundMessageState.java`, `OutboundMessageState.java` | `SSU2Session.cpp` `SSU2IncompleteMessage`, fragments | 32 KiB outbound message cap, fragment bitfield, out-of-order retention | Fragment queue and cap; per-session incomplete table now bounded | Unbounded partial-message retention fixed | P1 | Add malformed fragment tests | Pending |
| SSU2 MTU / IPv6 | `spec/ssu2` | `MTU.java`, `PeerState2.java`, `SSU2Payload.java` | `SSU2.cpp`, `RouterContext::SetMTU`, `SSU2Session::HandleAddress` | MTU options, ML-KEM minimums, IPv4/IPv6; Address block length must be 6 or 18 bytes | Similar MTU publication and checks; cross-family Address blocks are now ignored before local address/port/status updates | OpenBSD discovery remains platform-sensitive | Possible path-specific MTU failure; wrong-family external address update fixed | P0 (OpenBSD) | Prior fork fixes retained; family check in `HandleAddress` | Static; needs OpenBSD live test and v4/v6 peer test |
| I2NP header/types/serialization | `spec/i2np` | `core/.../data/i2np/*.java`, `router/.../message/*` | `libi2pd/I2NPProtocol.*` | 16-byte header, type/size/expiration, compression options | Same header and handlers; DatabaseLookup/SearchReply/DatabaseStore now size buffers from bounded counts | Serialization overflow and one-byte count truncation fixed | Compatible (static) | P0 | `test-protocol-bounds` passed in the local 48-test CTest | Needs malformed corpus/fuzz |
| RouterInfo max size | `spec/common-structures` | `RouterInfo.java`: `MAX_UNCOMPRESSED_SIZE = 4*1024` | `libi2pd/RouterInfo.h`: `MAX_RI_BUFFER_SIZE` | Accepts up to 4096 bytes uncompressed | Cap is 4096; DatabaseStore decompression now accepts exactly 4096 too | **Confirmed difference fixed** | Valid 4096-byte Java RouterInfo no longer rejected at the inflate check | P0 | Cap 4096; `uncompressedSize <= MAX_RI_BUFFER_SIZE` | Static; needs large-RI fixture |
| RouterInfo parsing/validation | `spec/common-structures` | `RouterInfo.java`: `MAX_ADDRESSES = 16`, `readBytes`; `RouterAddress.java`: `readBytes`; `DataHelper.readProperties` | `RouterInfo.cpp` `ReadFromBuffer`/`ExtractParam`, `RouterInfo.h` `MAX_RI_ADDRESSES` | At most 16 addresses; exact string lengths; sorted, non-duplicate options; introducer indices 0-2 | Strict parser with atomic commit, bounded options, optional `iexp`, NTCP2 `i`/`v`, per-family split, hidden identity, dotted version and file signature checks | Address option desynchronization fixed; property ordering remains intentionally tolerant | Compatible (static); valid Java RIs still accepted | P0 | Bounded option loops and malformed-RI rejection; malformed corpus pending | Static; needs RouterInfo fuzz/malformed fixtures |
| LeaseSet/LeaseSet2 | `spec/common-structures` | `core/.../data/LeaseSet.java`, `LeaseSet2.java` | `libi2pd/LeaseSet.*` | Standard and LS2 variants, flags, offline signatures | Standard/LS2/encrypted/Meta paths with candidate-only updates, exact key lengths/preference, strict properties and encrypted/auth/expiry bounds | Off-by-one in LeaseSet expiration extraction fixed | Compatible (static) | P1 | `test-leaseset2-java213-regression`, `test-leaseset-netdb-2.13-freshness` and `test-leaseset2-multikey-negotiation-2026` are registered; not run for the current tree | Needs fuzz/live LS corpus |
| EncryptedLeaseSet / b33 | `spec/encrypted-leaseset` | `LeaseSet2.java`, `BlindedPublicKey`/blinding classes | `Blinding.cpp`, `LeaseSet2` | Blinded keys, auth, offline batch | Same scheme; encrypted entries are not wrongly compared to identity hash | None found | Compatible (static) | P1 | b33 tool tests | Pending |
| netDb store/lookup/floodfill | `spec/netdb` | `router/.../networkdb/kademlia/*.java` | `libi2pd/NetDb.*`, `KadDHT.*` | Kademlia lookups, floodfill rules, expiration | Candidate-first replacement, LS1 earliest freshness, strict DLM/DSRM, bounded requests and lifecycle snapshots | Store eviction, replay/retry and lookup-injection gaps fixed | Compatible (static) | P1 | DB-104..DB-112; NetDb tests passed local 48-test CTest | Needs live floodfill lookup |
| Reseed/SU3 | `spec/su3` | `router/.../util/SU3File.java` | `Reseed.cpp` | SU3 header/signature/ZIP | Checked reads and bounds | C++ hardening only | Compatible (static) | P1 | Malformed fixture pending | Pending |
| Tunnel build (short/variable) | `spec/tunnel-creation` | `router/.../tunnel/*.java`, build request records | `TunnelConfig.*`, `Tunnel.*` | Record layouts, reply mapping | Equivalent | None found | Compatible (static) | P1 | Existing tests limited | Needs live tunnels |
| Garlic / tunnel messages | `spec/tunnel-message`, garlic | `router/.../router/...` message handling, garlic | `Garlic.cpp`, `TransitTunnel.cpp` | Layered messages, delivery instructions | Equivalent | None found | Compatible (static) | P1 | None | Needs live tunnel traffic |
| ECIES-X25519-AEAD-Ratchet | `spec/ecies` | `router/.../crypto/ratchet/...` (Java 2.14.0 moved the package) | `ECIESX25519AEADRatchetSession.*` | IK/ratchet, tags, AEAD, nonces | Same external format; NS uses a deliberately stricter non-empty Garlic policy (Java accepts an empty clove set) and valid DateTime; NSR/ES payloads are pre-validated before side effects, NSR splits are promoted by the first inbound tag and tag exhaustion restarts the session | NSR/ES partial application and replay-cache lifetime fixed statically | Implemented (static); tests registered but not executed | P0 | `test-eciesx25519-ratchet-session`, `test-eciesx25519-ratchet-tagsets`; no Java vector | Needs CI/cross-vectors |
| Signatures | `spec/common-structures` | `SigType.java` types 0–8, 11 | `Identity.h`, `Signature.*` | DSA/ECDSA/RSA/Ed25519/Ed25519ph/RedDSA | Same constants plus GOST 9/10; type 8 now uses Java-compatible plain Ed25519 over the supplied bytes | GOST is not in Java stable list; optional | No interoperability impact for common types | P2 | Type-8 vectors still needed | Existing vectors/static review |
| ML-KEM encryption types | proposal 169 / `spec/ecies` | `EncType.java` codes 5–7 | `PostQuantum.*`, `Crypto.h` | MLKEM512/768/1024 hybrid | Same codes when the provider exposes ML-KEM; capability is now probed at runtime and cached, LibreSSL 4 excludes ML-KEM-512, and unsupported key creation fails closed | Provider probe implemented statically; type-5/7 session and Java provider vectors remain missing | PQ sessions only when both support | P1 | Capability checks retained; publication/selection gated | Needs PQ interop |
| Streaming | `spec/streaming` | `router/.../client/streaming/*.java` | `Streaming.*` | SYN/SYN-ACK, windows, resets, close | Equivalent | None found | Compatible (static) | P1 | None | Needs dynamic stream test |
| Datagrams v1/2/3 | `spec/datagrams` | `core/.../client/Datagram*.java` | `Datagram.*` | Repliable/raw, ports, auth | Same | None found | Compatible (static) | P1 | Existing code | Needs client interop |
| SAM | `spec/samv3` | `apps/sam/java/src/net/i2p/sam/*` | `libi2pd_client/SAM.cpp` | Session/handshake/naming/streams, pipelined commands and one-word controls | Same commands plus persistent bounded framing, quoted parameter values, case-insensitive keys and repeated-space command words; SAM 3.3 `LISTEN_PROTOCOL` routing and protocol-aware RAW dispatch are implemented statically | New framing/control fixes are statically implemented; live SAM client and split-frame tests remain | Interoperability improvement for pipelined clients | P1 | `test-sam-control-framing`; `test-sam-version.sh` not run | Pending |
| I2CP | `spec/i2cp` | `core/.../client/I2CP*.java` | `I2CP.cpp` | Message codes, options, LeaseSet lookup | Same | None found | Compatible (static) | P1 | None | Needs dynamic client |
| HTTP client proxy | RFC 9110/9112 | Java apps use HTTP client/proxy separately | `HTTP.cpp`, `HTTPProxy.cpp` | Not a router wire protocol | Header OWS and strict lengths fixed | Interop improvement | Compatible (unit tested) | P2 | Tests passed | Linux unit test |
| Web console | Java router feature | Java console classes | removed | Java-only UI | Removed | Intentional; not protocol | None | — | N/A | N/A |

## Confirmed finding and fix

### RI-001: RouterInfo size cap 3072 vs Java 4096

- **Java:** `router/java/src/net/i2p/data/router/RouterInfo.java`,
  `MAX_UNCOMPRESSED_SIZE = 4*1024`, enforced by `DatabaseStoreMessage`,
  transports and reseed.
- **C++ before:** `libi2pd/RouterInfo.h`, `MAX_RI_BUFFER_SIZE = 3072`.
- **Impact:** a valid Java RouterInfo between 3073 and 4096 bytes could be
  rejected by openi2pd, preventing netDb/floodfill visibility.
- **Fix:** `MAX_RI_BUFFER_SIZE` raised to 4096 with a comment referencing the
  Java constant. NTCP2 handshake-specific limits remain separate.
- **Security:** buffers remain fixed-size and all parsers still enforce the cap;
  no unbounded allocation was introduced.
- **Validation:** static only; a >3072-byte RouterInfo fixture and live Java
  publication test are pending.

> Historical note: the matrix above is the first-pass inventory. Where the
> continuation table below reports a finding, that newer row supersedes the
> older “None found”/“Compatible (static)” wording.

## Current continuation audit (2026-10-18; third pass below, 2026-10-19)

This table records the second static pass against the pinned Java 2.13.0 tree.
`Fixed (static)` means the C++ source was changed; **no dynamic test was run**.

| ID | Area | Specification / Java reference | openi2pd reference | Java behavior (static) | openi2pd behavior | Difference / consequence | Priority | Correction | Tests / validation |
|---|---|---|---|---|---|---|---|---|---|
| NET-101 | SSU2 PQ option list | `UDPTransport.java:145-153, 959-1021`; SSU2 spec | `RouterInfo.cpp` `pq` parser/publisher | Publishes `pq="4,3"` where ML-KEM-512 is available; LibreSSL publishes `4` or omits it; selects only supported variants | Earlier collapsed `4,3`; LibreSSL could publish unsupported `3` | Silent PQ downgrade or failed session | P1 | Parse comma list and select the strongest locally supported value | Fixed (static); needs v4 capture |
| NET-102 | SSU2 RelayRequest version | `SSU2Util.java:231`; `IntroductionManager.java:452-457, 778-783` | `SSU2Session.cpp::Introduce` | Requires relay request `ver=2` | Sent session version 3/4 | Java Bob/Charlie drop relay requests on PQ sessions | P1 | Hard-code relay request version 2 | Fixed (static); relay dynamic test pending |
| NET-103 | SSU2 SessionConfirmed fragments | `PacketBuilder2.java:541-710`; `InboundEstablishState2.java:891-957` | `SSU2Session.cpp::ProcessSessionConfirmed/SendSessionConfirmed` | Reassembles and sends 1..15 fragments | Receive and send paths support 1..15 MTU-sized fragments; retransmit unchanged | Large RouterInfo over low-MTU now has a bounded multi-fragment path | P1 | Bounded 1..15 receive/send and exact retransmit | Fixed (static); `test-ssu2-session-fragment-bounds` passed |
| NET-104 | SSU2 ACK block expansion | `SSU2Payload.java:289-306`; `SSU2Bitfield.java:190-226` | `SSU2Session.cpp::SendQueue` | Parses declared ranges | Expanded ACK size without initializing ranges | Possible false ACK or dropped ACK | P0 | Drop ACK on no space; never extend an uninitialized block | Fixed (static); instrument/fuzz pending |
| NET-105 | SSU2 peer-test buffer | `SSU2Util.java:192-212` | `SSU2Session.cpp::CreatePeerTestBlock` | Allocates signature space for any identity | 96-byte stack buffer; overflow for P-384/P-521/RSA | Memory corruption / peer-test failure with non-Ed25519 identity | P0 | Dynamic vector sized for signature length | Fixed (static); ASan peer-test pending |
| NET-106 | SSU2 Network ID | `PacketHandler.java:250-263, 319-336, 446-483` | `SSU2Session.cpp` long-header handlers | Rejects wrong netID with reason 21 | Did not read `flags[1]` | Cross-network handshake accepted | P1 | Validate netID in SessionRequest/TokenRequest/Retry/HolePunch | Fixed (static); two-netID test pending |
| NET-107 | SSU2 version fallback | `InboundEstablishState2.java:124-133`; `OutboundEstablishState2.java:390-395` | `SSU2Session.cpp::ProcessRetry`, `SelectOutgoingVersion` | Treats version Retry as terminal in Java 2.13 | Retry now persists a lower version; next send does not re-apply the peer PQ version | Retry loop removed for the covered path | P2 | Persist fallback and cap selection | Fixed (static); low-MTU/mock test pending |
| NET-108 | SSU2 ACK0 loss | `PeerState2.java:165-183, 511-522` | `SSU2Session.cpp::ProcessData` | Sends ACK0 once; drops repeated confirmed | Waited only for ACK before Established | Lost ACK0 causes 5 s failure | P2 | Enter Established on first decryptable data packet | Fixed (static); netem ACK0 loss test pending |
| NET-109 | SSU2 packet 0 duplicate | `PeerState2` receive counters | `SSU2Session.cpp::ProcessData` | Initial packet accepted once | `!packetNum` bypassed duplicate check | Duplicate packet 0 possible | P3 | One-time initialization then normal counter | Fixed (static); duplicate packet test pending |
| NET-110 | SSU2 data size bound | SSU2 packet framing | `SSU2Session.cpp::SendData` | Includes header/MAC | Check omitted 16-byte MAC | Latent oversized datagram | P3 | Check `len + 32` | Fixed (static); boundary test pending |
| LS-101 | Encrypted LeaseSet store key | `EncryptedLeaseSet.java:377-388`; proposal 123 | `LeaseSet.cpp`, `NetDb.cpp::AddLeaseSet2` | Store key is SHA256(blinded sig type + blinded key) | Type-5 entry was accepted under any key | Lookup poisoning / wrong encrypted LS served | P0 | Compute store hash while parsing and bind to DatabaseStore key | Fixed (static); cross-key fixture pending |
| LS-102 | LeaseSet replay eviction | `Kademlia...Facade.java:1051-1183` | `NetDb.cpp::AddLeaseSet*` | Validates replacement; leaves old entry on failure | Expired/unpublished LS2 erased a valid entry; LS1 could replace LS2 without freshness | Replayable eviction/stale LeaseSet | P0 | Never erase on invalid replacement; compare LS1 expiry with existing LS2 | Fixed (static); replay test pending |
| LS-103 | Floodfill LS2 lease dates | `Kademlia...Facade.java:993-1035`; `LeaseSet.java:200-261` | `LeaseSet.cpp::ReadStandardLS2TypeSpecificPart` | Rejects all-expired/far-future leases | Floodfill skipped lease dates (`storeLeases=false`) | Serves dead leases; accepts invalid policy | P1 | Always walk leases, validate dates and require one current lease | Fixed (static); expired-LS2 fixture pending |
| RT-101 | Ratchet NS freshness/replay | `ECIESAEADEngine.java:591-625, 1465-1476` | `ECIESX25519AEADRatchetSession.cpp` | Requires DateTime in NS, checks -5/+2 min, a time-bounded replay cache and a deliberately stricter non-empty Garlic policy (Java accepts an empty clove set) | DateTime-only NS could create a session; replay cache was a 256-entry FIFO with no time bound | Captured NS replay after cache eviction | P0 | Generic handshake validation; 5-minute/4096-entry decaying cache; document the stricter NS policy | Fixed (static); replay/time-skew vector pending |
| RT-102 | PQ NSR retransmission | `ECIESAEADEngine.java:784-841, 1257-1319` | `ECIESX25519AEADRatchetSession.cpp::NextNewSessionReplyMessage` | Clones full handshake state per NSR | Restored CK but omitted static-DH MixKey (fixed earlier); payload validation still missing | Second PQ NSR fails authentication | P1 | Re-run `x25519(besk, apk)` MixKey after ML-KEM; validate NSR before commit | Fixed (static); two-NSR/type-7 vector pending |
| RT-103 | Multi-key LeaseSet2 | `LeaseSet2.java:186-205`; `ECIESAEADEngine.java:1133-1148` | `LeaseSet.cpp`, `Datagram.cpp`, `Streaming.cpp`, `Destination.cpp` | Selects acceptable key from LS order; all sections retained and matched by remote key type | One encryptor only in older code | Valid ratchet peer rejected when using another advertised key | P1 | Retain all key sections; repeated preferred type keeps first | Fixed (static); 7/4 vs 6/4 session matrix pending |
| RT-104 | Ratchet payload strictness | `RatchetPayload.java:105-205` | `ECIESX25519AEADRatchetSession.cpp::ValidateRatchetPayload/HandlePayload` | Rejects blocks after padding/termination, bad DateTime/NEXTKEY/ACK lengths, and stages validation before side effects; NS keeps a stricter non-empty Garlic policy | Parser could deliver a prefix before failing late | Partial application/state divergence | P2 | Pre-validate NSR/ES payload; reject invalid DateTime and duplicate forward/reverse NEXT_KEY as a unit | Fixed (static); malformed corpus pending |
| DG-101 | Datagram2 minimum | `Datagram2.java:68`; DSA identity sizes | `Datagram.cpp::HandleDatagram2` | Minimum 429 bytes | Bound now 429 | Valid 429-432-byte DSA datagrams accepted | P2 | Lower bound to 429, then parse identity | Fixed (static); DSA vector pending |
| DG-102 | Datagram3 first contact | `Datagram3.java:117-150`; `SAMv3DatagramSession.java:99-110` | `Datagram.cpp::HandleDatagram3` | Delivers the unauthenticated sender hash without a cached LS | Required a ratchet session; unsolicited first contact was dropped | Valid Datagram3 first contact never reached a HashReceiver | P2 | Deliver to a HashReceiver; prefer a fresh LS; full-identity fallback only with a cached verified LeaseSet for internal consumers | Fixed (static); Java first-contact test pending |
| DG-103 | Datagram2 offline signatures | `Datagram2.java:117-129` | `Datagram.cpp:131-180,299-346` | Emits and verifies the offline section | Implemented with delegation expiry and signed-region verification | No code gap found; Java-generated offline vector still absent | P2 | Keep implementation; add Java offline vector | Implemented (static) | Java receiver/vector pending |
| CR-101 | Elligator boundary | `Elligator2.java:221-224` | `Elligator.cpp` | Rejects `r >= (p-1)/2` | Accepted equality | Accepted-invalid representative | P3 | Strict `<` comparison | Fixed (static); crafted representative test pending |
| SAM-101 | SAM NAMING/HELLO | `SAMv1Handler.java:426-427`; `SAMHandlerFactory.java:133-155` | `SAM.cpp` | KEY_NOT_FOUND; accepts single-digit version | Returned INVALID_KEY; rejected `"3"` | Wrong result/version negotiation | P2 | Use KEY_NOT_FOUND and normalize one-digit version | Fixed (static); SAM 3.1 handshake test pending |
| SAM-102 | SAM 3.2 ports | SAM v3 spec; `SAMv3Handler.java:89-96, 920-948`; `SAMv3StreamSession.java:173-188` | `SAM.cpp` | Ports/protocol/header supported; STREAM subsession uses LISTEN_PORT; 32 KiB SEND/receive buffers; SILENT connect starts both directions | Older 8 KiB control buffer could stall valid large frames; stream subsessions armed the default destination | Large RAW/DATAGRAM SEND and per-port STREAM ACCEPT previously failed | P1 | Complete command/response port plumbing for 3.0-3.2 and 3.3 response shape | Fixed (static) for 3.0-3.3; buffer/subession/routing dynamic test pending |
| SAM-103 | STREAM CONNECT status | `SAMv3StreamSession.java:163-201` | `SAM.cpp::Connect`/timer | Waits for connection then OK/failure; SILENT starts I2P->client too | Returned OK immediately; SILENT was half-duplex | Client starts sending before reachability or never receives replies | P1/P2 | Poll `Stream::IsEstablished`, report TIMEOUT, start I2PReceive independently of reply | Fixed (static); unreachable/SILENT test pending |
| SAM-104 | Streaming RESET | `ConnectionPacketHandler.java:537-629` | `Streaming.cpp::Close/SendReset` | Sends RESET on abort | TODO, no reset | Peer hangs until timeout | P2 | Added signed RESET packet; suppress echo on received reset | Fixed (static); abort/close test pending |
| SAM-105 | SAM UDP first line | `SAMv3DatagramServer.java:140-172`; SAMv3 spec | `SAM.cpp::ParseSAMDatagramHeader` | Accepts `3.x nick destination [options]` | Treated `3.x` as the session ID; standards-compliant sends were dropped | All SAM 3.x UDP datagram/raw sends failed | P1 | Version-token parser; retain legacy no-version form | Fixed (static); `test-sam-datagram-framing` passed in the local 48-test CTest |
| SAM-106 | SAM 3.2 forwarded ports/header | `SAMv3DatagramSession.java:90-123`; `SAMv3RawSession.java:60-121` | `SAM.cpp` forwarding paths | Repliable/raw forwarded datagrams include ports; RAW `HEADER=true` emits PROTOCOL/FROM/TO | Omitted ports and ignored HEADER | Forwarded UDP payloads lacked metadata | P2 | Version-gated header fields and per-session RAW header flag | Fixed (static); live SAM forwarding pending |
| SAM-107 | SAM HELLO version comparison | `SAMHandlerFactory.java:133-153`; `VersionComparator.java` | `SAM.cpp::ParseSAMVersion/ChooseSAMVersion` | Compares version components numerically | Concatenated digits; `MAX=3.2.1` selected 3.3 | Wrong protocol handler selected | P3 | Component-wise numeric comparison and fixed candidate list | Fixed (static); `test-sam-version.sh` and dynamic handshake pending |
| SAM-108 | SAM invalid SIGNATURE_TYPE | `SAMv1Handler.java:336-350`; `SAMv3Handler.java:437-447` | `SAM.cpp::ProcessSessionCreate/ProcessDestGenerate` | Numeric types are validated; names are case-insensitive; type 8 name accepted | Unsupported numeric values silently fell back to DSA | Client received an unexpected destination | P2 | Return `SESSION STATUS RESULT=I2P_ERROR` / `DEST REPLY RESULT=I2P_ERROR` | Fixed (static); invalid-type test pending |
| SAM-109 | SAM RAW protocol zero | `SAMv1Handler.java:540-580`; `SAMRawSession.java:93-94`; SAMv3 spec | `SAM.cpp::ProcessSessionCreate/ProcessSessionAdd` | Java maps SAM RAW `PROTOCOL=0` to I2CP raw protocol 18; local preserves explicit zero | Divergent policy for protocol 0 | A Java RAW receiver may not receive a zero-protocol send | P3 | Preserve explicit zero as documented local behavior; add interop test or normalize at SAM boundary | Intentional difference (static); interop test pending |
| DG-104 | Datagram2/3 zero-length options | `Datagram2.java:174-184`; `Datagram3.java:127-137`; datagrams spec | `Datagram.cpp::ParseDatagramOptions` | A zero-length options Mapping is valid | Treated as malformed/truncated | Valid Datagram2/3 packets rejected | P2 | Parse the 16-bit Mapping length; accept zero and bound the rest | Fixed (static); `test-datagram-options` passed in the local 48-test CTest |
| STR-101 | Streaming option bounds | `Packet.java:619-760`; streaming spec | `Streaming.cpp::ValidatePacket/ProcessOptions` | Validates fixed header and each option | Fixed-part accessors ran before length validation; options could desynchronize | Malformed packet could read stale pool bytes or overrun option parsing | P2 | Validate header/options at both dispatch points; bound each flag block | Fixed (static); malformed corpus pending |
| STR-102 | Initial SYN ACK field | `ConnectionPacketHandler.java:279-285`; streaming spec | `Streaming.cpp::ProcessPacket` | Ignores ackThrough on initial SYN with sendStreamId 0 | Processed ACKs even when NO_ACK was omitted | Spurious ACK state on connection setup | P3 | Skip ACK processing for that case | Fixed (static); SYN/ACK test pending |
| STR-103 | Streaming PING/PONG flags | `ConnectionManager.java:456,914,960` | `Streaming.cpp::SendPing/HandlePing` | PING/PONG set NO_ACK | Did not set NO_ACK | Extra ACK work and Java divergence | P3 | Add `PACKET_FLAG_NO_ACK` to ping and pong frames | Fixed (static); ping test pending |
| I2CP-103 | SessionConfig date/offline signature | `SessionConfig.java:250-318`; `ClientMessageEventListener.java:234-260`; I2CP spec | `I2CP.cpp::ProcessCreateSession` | ±3-minute date window; validates transient delegation and session signature | No date window; offline-signed sessions were rejected/unverified | Replay window missing; offline destinations failed | P2 | Enforce date skew, parse offline options, verify delegation and transient signature | Fixed (static); Java offline-session test pending |
| I2CP-104 | HostReply options Mapping | `HostReplyMessage.java:154-158`; proposal 167 | `I2CP.cpp::SendHostReplyMessage` | Types 2-4 include a Mapping; standard LS2 properties are forwarded | Mapping existed but was always empty | Java lookupDest2/options clients lost LS2 options | P3 | Emit `LeaseSet2::GetProperties()` when available, else `{0,0}` | Fixed (static); options propagation/vector pending |
| I2CP-101 | HostLookup options | `HostLookupMessage.java:189-207`; `I2PSessionImpl.java:232-257`; proposal 167 | `I2CP.cpp::HostLookupMessageHandler` | Types 2/3/4 request LeaseSet options and b33 lookup | Types 0-4 parsed; b33 asynchronous; standard LS2 options forwarded; mixed-case .B32.I2P recognized | Secret/TYPE_KEY association still incomplete | P1/P2 | Parse 2/3/4, resolve blinded addresses, emit LS2 options | Fixed (static); secret/TYPE_KEY and Java vector pending |
| I2CP-102 | BlindingInfo | `I2PSessionImpl.java:2234-2242` | `I2CP.cpp` | Sends/uses type 42 | Type-42 parser, bounded cache, direct unblinded-key association and TYPE_DEST/TYPE_KEY merge | Secret-blinded b33 remains fail-closed; TYPE_KEY has no full auth path | P2 | Parse and bound BlindingInfo; do not claim full secret-blinded support | Fixed (static) for association/merge; Java client test pending |
| NET-111 | MetaLS lookup | `MetaLeaseSet.java:24-25`; `DatabaseEntry.java:52` | `LeaseSet.cpp` type-7 parser | Java stores/serves MetaLS | Parses/validates/stores/serves type 7 | No current publication gap; dynamic Java vector pending | P1 | Meta LS2 parser and netDb path | Fixed (static); Java publication/lookup test pending |
| NET-112 | DLM reply-key 0x12 | `DatabaseLookupMessage.java:390-424`; proposal 154 (KDF TBD) | `NetDb.cpp::ParseDatabaseLookupReplyKey` | 0x12 is a 32-byte ratchet public key with no tags; Java 2.14.0 still has no sender | Strict 0x02/0x10 parsing; 0x12 rejected fail-closed, never cleartext | No non-normative construction or cleartext fallback | P1 | Parse/validate reply-key block; reject derived-key mode | Fixed (static); store test covers parser |
| NET-113 | DSRM sender binding | `IterativeLookupJob.java:38-45`; `SearchMessageSelector.java:79-86` | `NetDbRequests.cpp` | Requires queried sender/from hash | Requires complete trailing from and queried-sender association | Truncated/fabricated replies rejected | P2 | Full from/count/queried-peer checks | Fixed (static); malformed DSRM test pending |
| NET-114 | Tunnel build time | `BuildHandler.java:101-104, 776-812` | `TransitTunnel.cpp` | Enforces 8-minute ECIES age and 5-minute future; drops without reply; code 30 for unsupported short layer encryption | Now validates minute windows after decryption; ignores future-duration expiration as Java does | Stale/future requests dropped; zero-payload/flags/IDs guards added | P2 | Minute-window validation and hostile-record drops | Fixed (static); clock-skew and Bloom-filter replay tests pending |
| NET-115 | Floodfill RI freshness | `HandleDatabaseLookupMessageJob.java:57, 130-140` | `NetDb.cpp::HandleDatabaseLookupMsg` | Serves only current RIs (1 h) | Applies one-hour plus two-minute tolerance to RouterInfo only; self always served | Stale RouterInfo suppressed | P2 | RouterInfo-only freshness check | Fixed (static); aged-RI test pending |
| NET-116 | RouterInfo address option bounds | `RouterInfo.java:88, 589-594`; `RouterAddress.java:285-297`; `DataHelper.java:189-220`; `spec/common-structures` | `RouterInfo.cpp::ReadFromBuffer`/`ExtractParam`, `RouterInfo.h::MAX_RI_ADDRESSES` | `MAX_ADDRESSES=16`; exact string reads; duplicate/unsorted address and RI options rejected | Address loop passed whole remaining RI to option parser; 0-255 addresses; clamped strings; duplicate options overwrote earlier values | Malformed option lengths could consume the next address, peers, properties or signature bytes; conflicting `host`/`port`/`s`/`i`/`v`/`pq` values accepted; no OOB proven | P1 | Bound each option to `size - r`, reject `sz==0`/oversized, cap addresses at 16, reject duplicates, strict `ExtractString`/`ExtractParam`, bound property section and peer count | Fixed (static); malformed RouterInfo corpus pending |
| NET-117 | SSU2 Address block family | `SSU2Payload.java:308-315`; `PeerState2.java:703-706`; `InboundEstablishState2.java:534-539`; `OutboundEstablishState2.java:339-344`; `UDPTransport.java:1354-1382`; SSU2 spec Connection Migration | `SSU2Session.cpp::HandleAddress` | Validates block length 6/18 and does not use it to change local external address | Rejects invalid length and ignores cross-family blocks before local update | Authenticated peer could otherwise overwrite the other family's external address | P1 | Family validation before `RouterContext::UpdateAddress` | Fixed (static); v4/v6 matrix pending |
| NET-118 | NTCP2 SessionRequest replay | `spec/ntcp2` SessionRequest replay note; `NTCPTransport.java:227,855-857` | `NTCP2.cpp::ProcessSessionRequest`, `NTCP2Server::IsHandshakeReplay` | Java keeps a bounded replay filter for encrypted X prefixes | No replay cache; every duplicate was processed | Replayed SessionRequest could force repeated handshake work | P1 | Bounded 8192-entry/600 s mutex-protected replay cache, cleared on stop | Fixed (static); replay/expiry test pending |
| NET-119 | NTCP2 SessionConfirmed part 2 blocks | `spec/ntcp2` Block Ordering Rules; `NTCP2Payload.java:85-160`; `InboundEstablishState.java:558` | `NTCP2.cpp::ValidateNTCP2SessionConfirmedPayload` | Requires RI, optional Options, optional final Padding; no other block | Only the first RI block was checked | Malformed/out-of-order blocks could reach handshake state | P1 | Exact ordered validation with truncation/overrun rejection | Fixed (static); `test-ntcp2-session-confirmed-payload` passed in the local 48-test CTest |
| NET-120 | NTCP2 msg3p2 length cap | `InboundEstablishState.java:84-87,470-473` | `NTCP2.cpp::ProcessSessionRequestMessage` | Caps `m3p2len` at 4048 and floors it at 459 for the reference implementation | Only rejected values below 19; accepted up to 65535 | Per-handshake allocation/read amplification and a SessionConfirmed the reference rejects | P2 | Exact 4048/459 policy bounds and `size_t` local calculation | Fixed (static); boundary test pending |
| NET-121 | NTCP2 data-phase block ordering | `spec/ntcp2` Block Ordering Rules; `NTCP2Payload.java:87-155` | `NTCP2.cpp::ProcessNextFrame` | Rejects truncated/overrunning blocks, data after Padding and non-final Termination | Accepted blocks after Padding and clamped overlong Padding | Misparsed frames and inconsistent termination handling | P1 | Enforce header size, exact block bounds, Padding-last and Termination-last | Fixed (static); malformed-frame corpus pending |
| NET-122 | NTCP2 termination valid-frame count | `spec/ntcp2` Termination; `NTCPConnection.java:881-889,1504-1612` | `NTCP2.cpp::HandleReceived` | Counts only successfully authenticated frames | Incremented receive nonce before AEAD verification | Termination reported one extra valid frame after AEAD failure | P2 | Increment only after successful decryption | Fixed (static); AEAD-failure injection pending |
| NET-123 | NTCP2 DateTime block size | `spec/ntcp2` DateTime; `NTCP2Payload.java:103-109` | `NTCP2.cpp::ProcessNextFrame` | DateTime value is exactly 4 bytes and bad size is non-fatal | Accepted any size >= 4 | Tolerated malformed block instead of Java-like skip/error | P3 | Require exact size 4 and skip/log on mismatch | Fixed (static); malformed corpus pending |
| NET-124 | SSU2 token one-time use | SSU2 spec Tokens; `EstablishmentManager.java:2250-2282,2396-2408` | `SSU2.cpp::FindOutgoingToken/GetIncomingToken`, `SSU2Session.cpp` | A token may be used once and is removed after validation | Outgoing tokens were reusable; incoming tokens stayed cached | Replay of a captured token could trigger extra handshakes | P1 | Erase outgoing on use; remove incoming after successful SessionRequest | Fixed (static); token replay test pending |
| NET-125 | SSU2 long-header validation | `PacketHandler.java:46-47,250-262,470-484`; SSU2 spec Header Validation | `SSU2Session.cpp`, `SSU2OutOfSession.cpp` | Exact netID, version 2-4, source/destination ConnIDs; version 2 for out-of-session PeerTest/HolePunch | netID=0 wildcard and several messages skipped version/ConnID checks | Cross-network or misaddressed packets could reach state machines | P1 | Exact netID/version checks; validate SessionCreated, Retry, TokenRequest, HolePunch and PeerTest IDs | Fixed (static); malformed-header matrix pending |
| NET-126 | SSU2 PeerTest block validation | `SSU2Payload.java:363-387`; `PeerTestManager.java:805-835`; SSU2 spec Peer Test | `SSU2Session.cpp::HandlePeerTest`, `SSU2OutOfSession.cpp` | Version 2, `asz` 0/6/18, message-specific address/status rules, 19-byte IPv4 minimum | Accepted tiny blocks, wrong version/asz, status on msg 1/2, unvalidated out-of-session headers | Malformed peer-test state and possible OOB reads | P1 | Enforce version/asz/status/endpoint bounds in both handlers | Fixed (static); PeerTest 1-7 matrix pending |
| NET-127 | SSU2 handshake block ordering | `SSU2Payload.java:156-187,258-287,317-404`; SSU2 spec | `SSU2Session.cpp::ProcessSessionRequest/ProcessSessionCreated/ProcessTokenRequest` | DateTime must be the first handshake block with exact 4-byte data; invalid/terminating blocks fail the handshake | Handshake payloads could start with other blocks or carry Termination | Malformed handshakes reached Established/SessionCreated | P1 | `HasDateTimeFirstBlock`; `m_ReceivedTermination`; exact block lengths | Fixed (static); malformed handshake corpus pending |
| NET-128 | SSU2 PQ/version Retry | `InboundEstablishState2.java:95-170`; `PacketBuilder2.java:488-516` | `SSU2Session.cpp::SelectOutgoingVersion/ProcessRetry/ProcessSessionRequest` | Never exceed configured max; Retry selects a lower mutual version; incompatible Retry carries the real ConnID | Could advertise above local max/address support; Retry built before headerX decryption | Version mismatch or zero-ConnID Retry | P1 | Cap at local/peer versions, decrypt headerX first, persist fallback, choose 3 then 2 | Fixed (static); v2/v3/v4 retry matrix pending |
| NET-129 | SSU2 address/pq/MTU metadata | `UDPTransport.java:960-1000`; `OutboundEstablishState2.java:150-180`; `UDPAddress.java:102-129`; SSU2 spec MTU | `RouterInfo.cpp/.h`, `SSU2.cpp::Connect` | Parse `v=2,3` lists; retain `pq=4,3`; select MTU-aware 4 or 3; reject SSU2 MTU <1280 | Collapsed `v`/`pq`, ignored MTU for version 4, accepted sub-1280 SSU2 addresses | Wrong PQ selection or unusable session | P1 | Version-list and MTU-aware address selection | Fixed (static); `test-ssu2-address-negotiation` passed in the local 48-test CTest |
| NET-130 | SSU2 SessionRequest ConnIDs | `InboundEstablishState2.java:104-106`; SSU2 spec SessionRequest notes | `SSU2Session.cpp::ProcessSessionRequest/ProcessTokenRequest` | Reject zero or identical source/destination ConnIDs | Identical IDs were accepted | Ambiguous session identity | P2 | Reject zero/equal IDs before Noise | Fixed (static); malformed request test pending |
| NET-131 | SSU2 PeerTest signature type | `PeerTestManager.java`; `SSU2Payload.java` | `SSU2Session.cpp::HandlePeerTest` msg 2 | Incoming signature is Alice's; its length need not match Charlie's identity | Used Charlie's signature length to validate the incoming buffer | Valid cross-signature-type PeerTest rejected | P2 | Validate against Alice's RouterInfo signature length; sign msg 3 with Charlie | Fixed (static); mixed-signature PeerTest pending |
| NET-132 | SSU2 fragment ordering | `InboundMessageState.java:105-145` | `SSU2Session.cpp::AddOutOfSequenceFragment/HandleFollowOnFragment` | Reject fragments at/above a known last fragment; prevent truncation at first last marker | Later fragments could be queued after `isLast` | Silent reassembly truncation | P2 | Track `lastFragmentNum`; reject invalid ordering and duplicates | Fixed (static); `test-ssu2-fragment-ordering` passed in the local 48-test CTest |
| NET-133 | SSU2 ACK0/Termination | `SSU2Payload.java:289-306`; `PacketBuilder2.buildSessionDestroyPacket` | `SSU2Session.cpp::HandleAck/SendTermination` | Validate ACK length before handshake completion; include ACK with Termination | ACK0 could establish before length validation; Termination lacked ACK | Divergent handshake/termination behavior | P2 | Validate ACK first; emit ACK + Termination + padding | Fixed (static); ACK0-loss/termination test pending |
| NET-134 | NTCP2 termination and control-frame queueing | `spec/ntcp2` Termination/RouterInfo; `NTCPConnection.java:881-930` | `NTCP2.cpp::SendTermination/SendTerminationAndTerminate/HandleNextFrameSent` | Termination is queued and close follows the write; control frames are not dropped behind a data frame | Immediate close could race the termination write; RouterInfo/termination were dropped when `m_IsSending` | Peer often saw no termination; control updates lost | P1 | Completion-driven close, pending termination/RouterInfo state | Fixed (static); `test-ntcp2-handshake-bounds` passed local CTest |
| NET-135 | NTCP2 X/Y/static-key validation | `InboundEstablishState.java:318-321`; `OutboundNTCP2State.java:252-256,325-328`; spec/ntcp2 | `NTCP2.cpp` KDF helpers | Reject all-zero X/Y, all-zero DH output, zero/high-bit static key and zero IV | Values were accepted without point/zero checks | Invalid handshake material could reach Noise state | P1 | `IsAllZero`/`AgreeAndValidate`; reject invalid static key/IV | Fixed (static); malformed-key tests pending |
| NET-136 | NTCP2 SessionCreated replay | `spec/ntcp2` SessionCreated replay; Java incoming filter at `NTCPTransport.java:227,855-857` | `NTCP2.cpp::ProcessSessionCreated`, `NTCP2Server::IsHandshakeReplayOutgoing` | Alice caches encrypted Y prefixes for at least `2*D` | Only Bob had a replay cache | Replayed message 2 could force repeated handshake work | P2 | Separate bounded 10-minute outgoing cache | Fixed (static); `test-ntcp2-handshake-bounds` covers cache separation |
| NET-137 | NTCP2 pending inbound bounds | `spec/ntcp2` DoS Mitigation | `NTCP2.cpp::HandleAccept/RemoveNTCP2Session` | Bound pending handshakes and release the slot on termination | A failed handshake kept its per-IP slot until the 28–33 s sweep; no global cap | Immediate legitimate reconnect rejected; pending-slot exhaustion | P2 | Remove on Terminate; 512-entry global cap; iterator-safe stop | Fixed (static); lifecycle test pending |
| NET-138 | NTCP2 extra pending handshake bytes | `InboundEstablishState.java:477-519`; `OutboundNTCP2State.java:390-393`; spec/ntcp2 | `NTCP2.cpp::HasPendingIncomingData` | Reject bytes already queued after message 1/2 | Extra bytes were ignored | Handshake smuggling / probing weakness | P2 | `socket.available()` check before proceeding; probing-resistant close | Fixed (static); live TCP test pending |
| NET-139 | NTCP2 handshake padding limits | `spec/ntcp2-hybrid` Maximum Padding; `InboundEstablishState.java:444-449`; `OutboundNTCP2State.java:342-346` | `NTCP2.cpp` request/created generation and validation | Per-version limits: msg1 880/1264/1648; msg2 848/1168/1648 (the historical 1136/1616 values were wrong) | Shared `m_MaxMsgSize` allowed excessive padding; the SessionCreated receive cap was 32 bytes too small for ML-KEM-768/1024 | Excess allocation and a rejected conforming handshake | P2 | `NTCP2GetMaxHandshakePadding`, `NTCP2IsValidHandshakePadding`, clamped generation, 3296-byte buffer equality | Fixed (static) in NET-171; `test-ntcp2-handshake-bounds` covers limits and limit+1 |
| NET-140 | NTCP2 HTTP proxy buffer lifetime | Asio async buffer lifetime; no Java counterpart | `NTCP2.cpp::HandleProxyConnect` | Response buffer must outlive async read and commit exactly once | Stack `streambuf` retained by async handler; double commit | Use-after-free / corrupted proxy response | P1 | `shared_ptr<streambuf>` capture; remove duplicate commit | Fixed (static); proxy path not dynamically tested |
| NET-141 | SSU2 SessionConfirmed multi-fragment/MTU | `PacketBuilder2.java:541-700`; `InboundEstablishState2.java:862-960`; spec Session Confirmed Fragmentation | `SSU2Session.cpp::SendSessionConfirmed/ResendHandshakePacket/ProcessSessionConfirmed` | Up to 15 fragments sized by MTU, retransmitted unchanged; duplicate fragment 0 keeps original AD | Sender effectively emitted at most 2 and could exceed MTU; only one fragment retransmitted | Large RouterInfo failed; malformed/oversized fragments | P1 | MTU-sized 15-fragment sender, gzip forcing, padding, exact retransmit | Fixed (static); `test-ssu2-session-fragment-bounds` passed local CTest |
| NET-142 | SSU2 SendData failure result | `PeerState2.java:143-155`; spec Packet Numbering | `SSU2Session.cpp::SendData` and callers | Packet 0 is valid; a failed send must not be recorded as packet 0 | `0` was both failure and a valid packet number | Lost packets stored/retransmitted under a false ID | P1 | `bool` + packet-number out parameter; no nonce advance on failure | Fixed (static); call-site/ACK tests passed local CTest |
| NET-143 | SSU2 ACK NACK gap encoding | `SSU2Bitfield.java:139-230`; spec Ack | `SSU2Session.cpp::CreateAckBlock/HandleAck` | NACK-only ranges before a final NACK/ACK pair; reject impossible acnt | Gap >255 was capped while still ACKing unread packets; ACK parsing allowed `acnt > ackThrough` | False ACKs and peer state divergence | P1 | NACK-only ranges, capacity/cached-range guards, `acnt` validation | Fixed (static); `test-ssu2-session-fragment-bounds` passed |
| NET-144 | SSU2 duplicate/reassembly ordering | `PeerState2.java:535`; `InboundMessageState.java:105-145` | `SSU2Session.cpp::UpdateReceivePacketNum/ConcatOutOfSequenceFragments` | Duplicate reordered packets are rejected; completed out-of-order message must call `FromNTCP2` | Duplicate insert ignored return; out-of-order completion skipped header conversion | Duplicate processing / wrong I2NP framing | P2 | Check set insertion; `FromNTCP2` on completion | Fixed (static); fragment/bounds test passed |
| NET-145 | SSU2 RTO/backoff | `PeerState.java:265-268,723-750,843-854`; RFC 6298 | `SSU2Session.cpp::Resend/HandleAckRange` | Initial/min RTO 1000 ms, max 60 s, RTT deviation, exponential backoff, no teardown for one lost packet | 540/100/2500 ms values; no deviation/backoff; session terminated after retry cap | Premature retransmit and session churn | P2 | RFC 6298 RTO/deviation, exponential backoff, drop instead of disconnect | Fixed (static); loss/reorder timing pending |
| NET-146 | SSU2 PQ SessionCreated MTU | `InboundEstablishState2.java:116-140,672-675`; `PeerState2.java:110-115`; spec MTU | `SSU2Session.cpp::SendSessionCreated/AdjustMaxPayloadSize` | Version 4 requires ML-KEM-768 MTU floor; never raise above local/remote capability | Request-derived estimate could exceed capability | Version selected but session too small | P2 | Clamp to local/remote MTU and 1258/MIN_MTU floors | Fixed (static); v4 MTU matrix pending |
| NET-147 | SSU2 fragment minimums/overflow | `SSU2Payload.java:265-280`; spec Follow-on | `SSU2Session.cpp::CreateFirstFragmentBlock/CreateFollowOnFragmentBlock/AttachNextFragment` | FIRSTFRAG data >9, FOLLOWON >5, bounded reassembly | Undersized fragments could be emitted/rejected; reassembly could overflow | Peer rejection / truncated I2NP delivery | P2 | Minimum data checks, 7-bit fragment limit and overflow cap | Fixed (static); `test-ssu2-session-fragment-bounds` covers |
| NET-148 | SSU2 short Data/path migration | `PeerState2.java:500-560`; spec Messages | `SSU2Session.cpp::ProcessData` | Reject <40-byte Data; update endpoint only after AEAD acceptance | Header mask ran before length check; migration triggered on unauthenticated source | Short-packet parsing and spoofed migration | P2 | 40-byte floor; post-AEAD path update | Fixed (static); malformed packet test pending |
| NET-149 | SSU2 per-family address split | `UDPTransport.java:960-1000`; `OutboundEstablishState2.java:150-180` | `RouterInfo.cpp::CopySSU2AddressForFamily`, `RouterContext.cpp` | Separate v4/v6 address state for dual non-published caps | One address object carried both families; MTU/published/introducers could leak | Wrong-family reachability, MTU and PQ state | P1 | Split dual-cap SSU2 entries into independent v4/v6 objects | Fixed (static); `test-ssu2-control-plane` passed |
| NET-150 | SSU2 PeerTest source validation | `PeerTestManager.receiveTest`; `SSU2OutOfSession.cpp` | `IsValidPeerTestEndpoint` and out-of-session handler | Reject invalid/privileged/reserved/banned source before response | Any source could trigger response processing | Amplification / spoofed peer-test work | P2 | Endpoint, reserved-range and ban checks | Fixed (static); `test-ssu2-control-plane` covers |
| NET-151 | SSU2 HolePunch version | `PacketBuilder2.buildHolePunch`; spec Hole Punch | `SSU2OutOfSession.h`, `SendHolePunch` | HolePunch `ver` is always 2, even for a PQ session | Used session/PQ version | Java rejects PQ-version HolePunch | P2 | Fixed `SSU2_HOLE_PUNCH_VERSION=2` | Fixed (static); control test covers constant |
| NET-152 | SSU2 MTU publication | `UDPTransport.java`; `OutboundEstablishState2.java`; spec MTU | `RouterContext.cpp::SetMTU/PublishSSU2Address` | Publish per-family MTU and refresh RI after changes | MTU changed after the last RI build; unpublishing kept stale introducers | Peers saw missing/old MTU; stale reachability | P1 | Persist MTU changes and clear introducers on unpublish | Fixed (static); control test covers publication |
| NET-153 | Transport lifecycle teardown | Local lifecycle requirement; no Java equivalence | `Transports.cpp::Stop/Start` | Join transport thread before releasing servers/timers; restart stopped io_context | Timers/threads could dereference destroyed servers; restart left stopped service | Use-after-free on shutdown/restart | P1 | Stop/join service, release servers/timers, restart service | Fixed (static); restart/ASan test pending |
| NET-154 | SSU2 PeerTest address family | `SSU2Payload.java:308-315`; `PeerTestManager` | `SSU2OutOfSession.cpp::HandleAddress` | Cross-family/unspecified apparent address must not drive local state | Address block could overwrite wrong-family endpoint | NAT/port misdetection | P2 | Validate nonzero/same-family before storing | Fixed (static); v4/v6 PeerTest pending |
| LS-109 | LS2 offline signature boundary | `LeaseSet2.java:556-567,648-683`; proposal 123 | `LeaseSet.cpp::ProcessOfflineSignature` | A final transient signature may end exactly at the buffer boundary | `>=` rejected the valid exact-fit form | Valid offline-signed LS2 rejected | P1 | Exact `>` bounds and trailing-signature consumption | Fixed (static); `test-leaseset2-java213-regression` passed |
| LS-110 | LS2 signature prefix mutation | `LeaseSet2.java:622-641` | `LeaseSet.cpp::VerifySignature` | Store type is signed but not part of the LS2 buffer | Wrote `buf[-1]` for I2CP/inner buffers | Out-of-bounds write | P1 | Verify a temporary `storeType + signed bytes` vector | Fixed (static); regression test |
| LS-111 | Standard LS2 lease freshness | `Kademlia...Facade.java:968-1037` | `LeaseSet.cpp::ReadStandardLS2TypeSpecificPart` | Validate earliest/latest leases; stale `published` may still be current | Rejected stale published and missed earliest >10 min | Valid LS2 rejected / stale earliest accepted | P1/P2 | Lease-based earliest/latest/expiry checks | Fixed (static); regression test |
| LS-112 | Encrypted LS2 auth/length bounds | `EncryptedLeaseSet.java:239-255,607-718` | `LeaseSet.cpp::ReadFromBufferEncrypted` | Reject missing/empty auth keys, bad schemes, truncated layer-1 text | Continued with uninitialized cookie / underflow | Parser/OOB risk and wrong auth state | P1/P2 | Min/max ciphertext, zero-client/wrong-scheme rejection, `Agree()` checks | Fixed (static); regression test |
| LS-113 | Encrypted LS2 Update state | `LeaseSet2.java:430-476`; `EncryptedLeaseSet.java` | `LeaseSet.cpp::LeaseSet2::Update` | Revalidate/reparse type 5 | Only replaced buffer; stale validity/leases | Reused object could stay wrongly valid | P2 | Outer-layer reparse and state reset | Fixed (static); regression test |
| LS-114 | Encrypted outer/inner metadata | Encrypted LS2 spec; `EncryptedLeaseSet.java:301-318` | `LeaseSet.cpp::ReadFromBufferEncrypted` | Outer published/flags/expiry govern retrieval | Inner values overwrote outer and extended expiry | Entry could outlive signed outer layer | P2 | Restore outer metadata; cap expiry at outer minimum | Fixed (static); regression test |
| LS-115 | Meta LS2 limits/freshness | `MetaLeaseSet.java:50-78`; `LeaseSet.java:200-215`; `Kademlia...Facade.java:989-1030` | `LeaseSet.cpp::ReadMetaLS2TypeSpecificPart` | Max 16 entries; earliest entry age bound | >16 entries and stale earliest accepted | Invalid MetaLS accepted | P2 | 16-entry cap and earliest-entry validation | Fixed (static); regression test |
| LS-116 | LS2 key length/preference | `LeaseSet2.java:443-462`; proposal 123 | `LeaseSet.cpp` key sections | Known type wrong length throws; server-preference key selected | Skipped mismatch; chose highest numeric type | Valid key sections discarded / wrong key selected | P2 | Reject known-length mismatch; first supported wire key; P384/P521 lengths | Fixed (static); regression test |
| LS-117 | LS2 property mapping strictness | `DataHelper.java:189-222` | `LeaseSet.cpp::ReadProperties` | Reject malformed/duplicate/overrun mapping | Mapping helper truncated but returned size | Accepted malformed LS2 properties | P2 | Exact key/value/`;` consumption and duplicate rejection | Fixed (static); regression test |
| LS-118 | Local LS2 writer bounds | `LeaseSet2.java:57,437-441,537-554` | `LeaseSet.cpp` local LS2 | Max 8 keys; 16-bit expires; no null buffer deref | Could emit >8 keys/overflow expires/null deref | Invalid locally generated LS2 | P3 | Cap keys, clamp expires, null guards, LS1 earliest rule | Fixed (static); regression test |
| RI-005 | RouterInfo update transaction | `Kademlia...Facade.store/validate`; `RouterInfo.readBytes` | `RouterInfo.cpp::Update/ReadFromBuffer` | Validate complete candidate before replacing | Cleared/mutated cached RI before full validation | A malformed signed update could erase a valid entry | High | Parse into locals and commit only after full validation | Fixed (static); `test-routerinfo-update-transaction` passed |
| RI-006 | Optional introducer expiration | Proposal 133; `UDPAddress`; `OutboundEstablishState2` | `RouterInfo.cpp::UpdateIntroducers` | Missing `iexp` means no expiration | Treated zero as already expired | Valid firewalled SSU2 router unreachable | High | Normalize missing/zero `iexp` to UINT32_MAX | Fixed (static); parsing test |
| RI-007 | NTCP2 published `i`/`v` | `NTCPTransport.getNTCPVersion`; `addNTCP2Options` | `RouterInfo.cpp` address parse | Published NTCP2 needs `v` and `i` | Accepted host+port+`s` without `i` | Unusable published NTCP2 endpoint | High | Require `v`+`i`+static key for published; reject fake outbound form | Fixed (static); parsing test |
| RI-008 | Local dual-stack NTCP2 split | Java separate v4/v6 RouterAddresses | `RouterInfo.cpp::AddNTCP2Address` | v4/v6 state independent | One object shared; host mutation crossed families | One family silently disappeared | High | Split local objects, merge matching wire view | Fixed (static); update test |
| RI-009 | RouterInfo init/bandwidth/version | `RouterInfo.getBandwidthTier`; version parser | `RouterInfo.cpp` | Initialize all fields; rank caps; dotted versions | Indeterminate fields; last cap won; digit concatenation | Wrong caps/version or UB read | Medium | Initialize fields, rank bandwidth caps, dotted version parse | Fixed (static); parsing test |
| RI-010 | Family signature ordering | `FamilyKeyCrypto.verify` | `RouterInfo.cpp` property parse | Verify after full property map | `family.sig` before `family` could disable binding | Family binding order-dependent | Medium | Defer verification until final family name | Fixed (static); parsing test |
| RI-011 | Hidden identity/published state | `RouterInfo.isHidden`; `RouterIdentity.isHidden` | `RouterInfo.h/.cpp` | Hidden cert or H cap means hidden | Certificate ignored | Hidden RI could be published | Medium | Check identity certificate and guard invalid identity | Fixed (static); parsing test |
| RI-012 | Address expiration endianness | `RouterAddress.writeBytes` | `RouterInfo.cpp::WriteToStream` | 8-byte expiration is big-endian | Wrote host-order uint64 | Wrong expiry on serialized local address | Medium | `htobe64(address.date)` | Fixed (static); update test |
| RI-013 | Introducer bounds | `UDPAddress.MAX_INTRODUCERS` | `RouterInfo.cpp::AddSSU2Introducer` etc | Max 3, tag >0, valid `ssu` | Unbounded/unsafe vectors | Invalid local RI / OOB risk | Medium | Limit 3, reject tag 0, null guards | Fixed (static); parsing/update tests |
| RI-014 | Cached RouterInfo file verification | `PersistentDataStore.read` | `RouterInfo.cpp::LoadFile/ReadFromFile` | Verify signature and size on load | Trusted local files; short read possible | Corrupt cache accepted | Medium | Signature verification, size/short-read checks | Fixed (static); update test |
| RI-015 | RouterInfo reachability masks | `UDPAddress`/`UDPTransport` | `RouterInfo.cpp::ComputeAddressTransports` | Published/reachable require concrete endpoint/introducer | Phantom published/reachable states | Bad peer selection | Medium | Shared mask computation, per-family caps, endpoint checks | Fixed (static); parsing/netdb tests |
| RI-016 | RouterInfo concurrency ownership | Immutable RI after read in Java | `RouterInfo.h/.cpp` | Immutable/atomic observable state | Racy scalars/profile/buffer mutation | Data race on live RI update | Medium | Atomic scalars, atomic profile `shared_ptr`, buffer mutex | Fixed (static); no deterministic concurrency test |
| DB-104 | DatabaseLookup reply-key 0x12 | I2NP Reply Encryption table; proposal 154 (derived KDF TBD) | `NetDb.cpp::ParseDatabaseLookupReplyKey` | 0x12 is 32-byte ratchet key, no tags; Java 2.14.0 still has no sender | Earlier treated as ECIES New Session, a non-normative construction | Guessed reply format | P1 | Strict 0x02/0x10 parsing; 0x12 fails closed; never cleartext | Fixed (static); `test-netdb-store-lookup-hardening` passed |
| DB-105 | Direct exploratory lookup | I2NP flags 0x0C; `ExploreJob.java:103-108` | `NetDbRequests.cpp` direct request | Direct exploration still uses EXPL type | Hardcoded RI type | Wrong discovery response | P1 | Pass `m_IsExploratory` in direct path | Fixed (static); `test-netdb-dsrm-correlation` passed |
| DB-106 | DatabaseSearchReply parser | I2NP DSRM; `DatabaseSearchReplyMessage.java:61-91` | `I2NPProtocol.cpp::ParseDatabaseSearchReply` | Key/num/peers/from with mandatory trailing from; 255 max | Ad hoc parsing duplicated bounds | Truncation ambiguity | P3 | Strict parser with exact count/from bounds | Fixed (static); `test-netdb-lookup-wire-bounds` passed |
| DB-107 | DSRM sender binding/replay | I2NP “from is unauthenticated”; Java `SearchReplyJob` | `NetDbRequests.cpp` | Bind to queried peer; one reply per peer | Conditional binding; duplicate replies consumed retries | Lookup injection/retry starvation | P1 | Require requested peer and mark replied peers | Fixed (static); correlation test |
| DB-108 | DSRM pending/filter bounds | Java `SearchJob`/`SingleLookupJob` | `NetDbRequests.*` | Bounded peer following and request map | Fabricated replies could grow work | Resource amplification | P2 | 4096 pending cap, dedupe/self/sender filter, strand-safe drop | Fixed (static); correlation test |
| DB-109 | netDb RouterInfo replacement | `Kademlia...Facade.store/validate` | `NetDb.cpp::AddRouterInfo` | Validate candidate before replacing | Newer invalid RI erased valid entry | Cache poisoning/eviction | High | Candidate validation, old entry retained on failure | Fixed (static); `test-netdb-store-lookup-hardening` passed |
| DB-110 | netDb LeaseSet replacement | `Kademlia...Facade.isNewer/validate` | `NetDb.cpp::AddLeaseSet/AddLeaseSet2`, `LeaseSet::GetEarliestLeaseDate` | Candidate-only, earliest lease for any LS1/LS2 mix, published for LS2/LS2 | Update() mutated cached entry; cross-type used header expiry | Invalid/replayed replacement or wrong winner | High | Earliest-lease comparison for cross-type, candidate build, no mutation | Implemented / static; `test-netdb-store-lookup-hardening` includes cross-type vector |
| DB-111 | netDb containers/visitors | Internal concurrency/ownership | `NetDb.cpp/.hpp` | Clear/visit under safe snapshots; locked counters | Unsynchronized clear; callbacks under locks; empty-map RNG | Deadlock/race/crash | High | Lifecycle mutex, snapshots before visit, locked clear, RNG mutex | Fixed (static); store test |
| DB-112 | netDb persistence future RI | `Kademlia...Facade` freshness | `NetDb.cpp::LoadRouterInfo` | Reject future-dated cached RI | Only old files rejected | Replayable future cache entry | Medium | Two-minute future bound on load | Fixed (static); store test |
| LS-104 | LS1 future lease | `KademliaNetworkDatabaseFacade.java:153,1024` | `LeaseSet.cpp::ReadFromBuffer`, `LeaseSetBufferValidate` | Rejects leases beyond 15 min + clock fudge | No upper bound; year-2100 leases could persist forever | Stale/replayable LS1 entry | P1 | Bound LS1 lease end before mutating stored state | Fixed (static); `test-leaseset-netdb-2.13-freshness` passed in the local 48-test CTest |
| LS-105 | LS2 future lease | `KademliaNetworkDatabaseFacade.java:153,1024` | `LeaseSet.cpp::ReadStandardLS2TypeSpecificPart` | Allows 15 min + 60 s future, not 12 min | 12-minute bound rejected clock-skewed peers | Valid LS2 stores dropped | P2 | Use `MAX_LEASE_FUTURE + CLOCK_FUDGE` | Fixed (static); boundary test passed in local CTest |
| LS-106 | LS2 unpublished/blinded flags | `LeaseSet2.java:51,56,142`; `EncryptedLeaseSet.readHeader` | `LeaseSet.cpp` | `0x04` marks blinded publication; it does not clear `0x02` unpublished; type-5 also honors `0x02` | `0x04` cleared unpublished; type 5 ignored it | Floodfill served an unpublished entry | P2 | Keep `0x02` authoritative for both standard and encrypted LS2 | Fixed (static); `test-leaseset-netdb-2.13-freshness` covers flags; local CTest passed |
| LS-107 | Encrypted LS2 freshness | `KademliaNetworkDatabaseFacade.java:968-1035` | `NetDb.cpp::AddLeaseSet2` | Bounds type-5 published/expires at store time | Only outer signature and `IsExpired()` checked | Stale/far-future encrypted LS2 could remain stored | P2 | Apply published/expiry window before insertion | Fixed (static); encrypted-LS2 boundary test pending |
| LS-108 | EC_P256 LS2 key length | `EncType.java:34`; `LeaseSet2.java:437-462` | `LeaseSet.cpp` | LS2 carries the 64-byte EC point | Used legacy 256-byte key-file size; valid sections discarded | P256 LS2/encryption key interop failure | P2 | LS2-specific 64-byte wire-length helper | Fixed (static); `test-leaseset-netdb-2.13-freshness` covers |
| DB-101 | DatabaseStore type mask | `DatabaseStoreMessage.java:138` | `NetDb.cpp`, `I2NPProtocol.cpp/.h` | Ignores upper four type bits | Dispatched on raw byte; future bits broke stores/signature type | Forward-compatible stores rejected | P3 | Mask `0x0f` in dispatch, flood copy and `IsRouterInfoMsg` | Fixed (static); high-bit type test passed in local CTest |
| DB-102 | DatabaseStore reply block | `DatabaseStoreMessage.java:251-266` | `I2NPProtocol.cpp::CreateDatabaseStoreMsg` | Non-zero token always carries 36-byte reply block; no tunnel means zero ID + writer hash | Token without reply block shifted LeaseSet bytes | Corrupt reply parsing | P2 | Always emit the 36-byte reply block | Fixed (static); reply-token fixture pending |
| DB-103 | DatabaseLookup exclusion cap | `DatabaseLookupMessage.MAX_NUM_PEERS` | `I2NPProtocol.cpp`, `NetDb.cpp` | At most 512 excluded peers | Could serialize/size by message limit | Oversized/divergent lookup | P3 | Cap at 512 on encode and receive | Fixed (static); 512/513 test pending |
| TU-101 | Truncated tunnel inputs | I2NP tunnel-data/build-response layout | `Tunnel.cpp` | Rejects empty/short build responses and truncated tunnel/gateway headers | Dereferenced `msg[0]` or 4/6-byte headers without length checks | OOB read on hostile tunnel messages | P1 | Explicit length guards | Fixed (static); malformed tunnel corpus pending |
| CRY-201 | GOST signature validation | RFC 7091 §6.2; proposal 134 | `Gost.cpp` | Reject `r/s` outside `[1,q-1]`, zero digest/inverse, infinity and off-curve points | All-zero signature could verify; invalid EC operations ignored | Forged GOST signature / invalid point | High | Check every EC operation, range, finite/on-curve point; retry degenerate nonce | Fixed (static); `test-crypto-wire-validation` covers; local CTest passed |
| RT-201 | Ratchet NEXT_KEY reuse | `RatchetSKM.java:1113-1328`; `RatchetTagSet.java:327-338` | `ECIESX25519AEADRatchetSession.cpp`, `Garlic.cpp` | Same-ID reuse generates; new ID reuses pair; every reuse requests reverse key | Reused key without request was ignored; sender omitted request | DH ratchet stalled after second reuse | High | Track key presence/parity; request on reuse; check `Agree` | Fixed (static); repeated NEXT_KEY loopback pending |
| GAR-201 | ElGamal/AES tag handling | `ElGamalAESEngine.java:45,425,451-456,155` | `Garlic.cpp::HandleAESBlock/CreateAESBlock` | Max 200 tags; accept rekey only for flag 0x01; associate tags with announced key | >200 tags accepted; non-zero flag consumed key; new key discarded | Tag-table growth / wrong tag key | High/Medium | Bound tags, exact rekey flag, install after hash, clamp outgoing tags | Fixed (static); 200-tag/rekey test pending |
| RT-202 | Handshake ACK block | `ECIESAEADEngine.java:1500-1506` | `ECIESX25519AEADRatchetSession.cpp` | ACKs in handshake have no established receive tagset and are ignored | Anonymous NS could confirm arbitrary message IDs | Delivery-state manipulation | Medium | Ignore ACKs unless a receive tagset exists | Fixed (static); NS/NSR ACK test pending |
| B33-201 | b33 flags/blinded type | `Blinding.java:236-260,303-308`; proposal 149 | `Blinding.cpp` | Reject unknown flags and non-RedDSA blinded key | Masked unknown flags; accepted wrong blinded type | Store-key mismatch / invalid address accepted | Medium | Reject `flags & 0xF8`; require RedDSA; validate length | Fixed (static); `test-crypto-wire-validation` covers; local CTest passed |
| ID-201 | Key certificate length | `KeyCertificate.java:93-96` | `Identity.cpp::FromBuffer` | KEY certificate needs 4-byte sig/crypto type header | Short payload silently treated as DSA | Identity/type confusion | Medium | Reject payload lengths 0-3 | Fixed (static); crypto wire test covers |
| SIG-201 | Ed25519ph SigType 8 | `SigType.java:63`; `DSAEngine.altSign`; SU3 signature notes | `Signature.h` / `Identity.cpp` | Java generic type-8 signing uses EdDSAEngine over the supplied bytes; SU3 callers prehash | C++ now uses the same plain Ed25519 primitive; no RFC 8032 dom2/prehash provider assumption | Type-8 signature bytes now match the Java primitive contract | Low (offline only; not advertised for normal identities) | Add Java/test-vector coverage; keep callers responsible for any prehash | Implemented (static), not interoperability-verified |

### Corrections applied in this pass (static)

- `RouterInfo.cpp`: SSU2 `pq` list parsing.
- `RouterInfo.cpp/.h`: strict string/option parsing; each address option section bounded by its declared size; 16-address cap; duplicate address/property options rejected; port range and introducer index 0-2 checks; peer count multiplication guarded.
- `SSU2Session.cpp/.h`: relay version 2, 1–15 fragment reassembly, safe ACK handling, peer-test signature buffer, netID checks, PQ fallback flag, ACK0-loss establishment, one-time packet-0 handling, data-size bound, cross-family Address block rejection.
- `LeaseSet.cpp/.h`: encrypted LS2 store hash, all-key retention, floodfill lease-date validation.
- `NetDb.cpp`: store-key binding, no eviction on invalid replacement, LS1/LS2 freshness, `SaveUpdated` snapshot.
- `ECIESX25519AEADRatchetSession.cpp`: NS DateTime/replay validation, PQ NSR static-DH re-mix, strict block ordering/lengths.
- `Datagram.cpp`, `Streaming.cpp`, `Destination.cpp`: multi-key static-key matching; `Streaming.cpp` RESET; Datagram2 minimum.
- `SAM.cpp/.h`: single-digit version, KEY_NOT_FOUND, connect wait, partial 3.2 port defaults.
- `I2CP.cpp`: HostLookup types 2/3/4 and blinded b33.
- `Elligator.cpp`: strict boundary.
- `SSU2.cpp`, `Transports.cpp`, `RouterContext.cpp`: LibreSSL PQ version clamping.

### Stable workstream status (I2P-001..I2P-013)

| ID | Workstream | Current status |
|---|---|---|
| I2P-001 | MetaLeaseSet type 7 | Implemented / static; Meta LS2 parser and netDb storage/serving; dynamic Java vectors pending. |
| I2P-002 | DatabaseLookup reply-key 0x12 | Parsed strictly but rejected fail-closed; proposal 154 derived-key KDF is explicitly TBD and Java 2.14.0 still has no sender. |
| I2P-003 | DatabaseSearchReply sender binding | Implemented / static; strict parser, mandatory trailing from, one accepted reply per queried peer, pending-request cap. |
| I2P-004 | Tunnel-build time windows | Implemented / static; 8-minute age, 5-minute future, Java-style silent drop; code 30 for unsupported layer encryption. Duplicate-build Bloom filter and dynamic tests pending. |
| I2P-005 | Floodfill RouterInfo freshness | Implemented / static; one-hour RouterInfo freshness plus two-minute tolerance, self hidden/exclusion rules and test coverage. |
| I2P-006 | SAM 3.2 | Implemented / static; SAM 3.2 port/protocol defaults, connect/accept, datagram/raw framing and version gating. |
| I2P-007 | I2CP BlindingInfo type 42 | Implemented / static; type-42 parser/cache, direct unblinded-key b33 association and TYPE_DEST/TYPE_KEY merge; secret-blinded submission remains pending. |
| I2P-008 | Datagram3 first contact | Implemented / static; unauthenticated hash receiver, fresh-LS preference and cached-LeaseSet-only internal fallback. |
| I2P-009 | Datagram2 offline signatures | Implemented / static; offline Datagram2 signing/verification and delegation expiry. |
| I2P-010 | Incoming PQ type iteration | Implemented / static; bounded candidate loop over locally supported PQ/EC types with trial state commit. |
| I2P-011 | SSU2 RelayRequest validation | Implemented / static; relay timestamp/version/length/signature/replay/resource validation. |
| I2P-012 | NTCP2/SSU2 timeout alignment | Implemented / static; protocol-significant NTCP2/SSU2 timeout and resend alignment. |
| I2P-013 | GOST 9/10 | Blocked: Java 2.13 lists 9/10 as reserved; no safe advertised implementation without further evidence. |

All statuses remain static until the user runs builds and differential tests. No
row above is a claim of dynamic interoperability.

### I2P-013: GOST 9/10 evidence

- Java 2.13.0 `core/java/src/net/i2p/crypto/SigType.java:66` explicitly
  reserves signature types 9 and 10 for GOST; Java's enum has no usable GOST
  verifier. C++ keeps its local GOST implementation for i2pd/OpenSSL builds,
  but it is not a Java-interoperable advertised capability. No code change is
  safe or useful until Java defines and ships the wire format and verifier.

### I2P-001..I2P-013 evidence table (static)

| ID | Functionality | Specification | Java reference (file/function) | C++ reference | Difference found | Consequence | Priority | Correction applied | Static review | Tests prepared | Dynamic validation |
|---|---|---|---|---|---|---|---|---|---|---|---|
| I2P-001 | Meta LS2 type 7 | proposal 123 | `MetaLeaseSet.readBytes`, `MetaLease.writeBytes/readBytes` | `LeaseSet2::ReadMetaLS2TypeSpecificPart`, `NetDb::AddLeaseSet2` | Type 7 was rejected | Meta LS2 could not be stored/served by i2pd floodfills | P1 | Parser, 40-byte entries, 16-entry/earliest bounds, revocations, expiry/signature/netDb path | Reviewed; bounds/signature and local regression passed | `test-leaseset2-java213-regression` Meta cases | Pending Java-generated vector |
| I2P-002 | DLM reply key 0x12 | proposal 154 (derived KDF TBD) | `DatabaseLookupMessage.readMessage/writeMessageBody` | `NetDb::ParseDatabaseLookupReplyKey`, `HandleDatabaseLookupMsg` | 0x12 is a 32-byte ratchet public key with no tag; Java 2.14.0 still has no sender and the KDF is TBD | A guessed or cleartext reply would be non-interoperable | P1 | Strict 0x02/0x10; 0x12 rejected fail-closed | Reviewed; Java 2.14.0 still has no caller for the public-key form | Store regression covers parser; dynamic 0x12 impossible against stock Java | No interoperability claim |
| I2P-003 | DSRM sender binding | Kademlia lookup | `IterativeLookupJob.runJob`, `SearchMessageSelector.isMatch` | `NetDbRequests::HandleDatabaseSearchReplyMsg` | Truncated/unqueried replies accepted | Lookup injection/stale termination | P2 | Strict trailing-from parser, count bounds, queried-peer binding, duplicate suppression, pending cap | Reviewed; local correlation/wire tests passed | `test-netdb-lookup-wire-bounds`, `test-netdb-dsrm-correlation` | Pending live lookup test |
| I2P-004 | Tunnel build time | tunnel-creation | `BuildHandler.handleReq` | `TransitTunnel.cpp` time helper | No request age/future/replay window | Stale/future builds accepted | P1 | 8-minute ECIES age, 5-minute future, drop semantics; OBGW zero receive ID allowed; flag/short guards | Reviewed | Boundary cases listed in template | Pending clock-skew/live test |
| I2P-005 | Floodfill RI freshness | netDb | `HandleDatabaseLookupMessageJob` `EXPIRE_DELAY` | `NetDb::HandleDatabaseLookupMsg` | Stale RIs served | Dead router data returned | P2 | One-hour + 2-minute tolerance, self/hidden exclusion and persistence future bound | Reviewed; local store test passed | `test-netdb-store-lookup-hardening` | Pending aged-RI/live Java lookup |
| I2P-006 | SAM 3.2/3.3 | SAM v3 spec | `SAMv3Handler.notifyStreamIncomingConnection`, `SAMv1Handler.receiveRaw/Datagram` | `SAM.cpp` stream/datagram paths | 3.2/3.3 ports, framing and ADD/REMOVE response shape implemented | Wrong service/port; clients see wrong metadata | P2 | Session defaults, connect/accept ports, datagram/raw fields, version gating, PRIMARY alias and ID responses | Reviewed; old SAM strings retained | SAM 3.0/3.1/3.2/3.3 cases | Pending terminal matrix |
| I2P-007 | I2CP BlindingInfo 42 | proposal 123 | `BlindingInfoMessage.doReadMessage`, `ClientMessageEventListener.handleBlindingInfo` | `I2CP.cpp` type-42 cache/HostLookup | Type 42 parsed; b33 association works, secret path incomplete | b33 secret/auth setup incomplete | P1 | Parser, bounded cache, direct unblinded-key association | Reviewed; secret/TYPE_KEY limitations documented | I2CP vectors | Pending Java b33 client test |
| I2P-008 | Datagram3 first contact | proposal 163 | `SAMv3DatagramSession` hash delivery | `Datagram::HandleDatagram3`, `HashReceiver` | Required cached LeaseSet | First-contact DGRAM3 dropped | P2 | Unauthenticated hash receiver, bounded sessions, cached-LeaseSet-only fallback | Reviewed | No-cache/concurrency cases | Pending dynamic first-contact |
| I2P-009 | Datagram2 offline | proposal 163 | `Datagram2.make/read` | `DatagramDestination::SendDatagram/HandleDatagram2` | Offline section emitted/validated in current code | No static gap found; Java-generated vector still absent | P2 | Offline flag/section, signed-region verification, delegation expiry | Reviewed; signature coverage traced | Offline-key vectors | Pending Java receiver test |
| I2P-010 | Incoming PQ types | proposal 169/ECIES | `MuxedPQEngine`, `ECIESAEADEngine.decryptNewSession` | `ECIESX25519AEADRatchetSession` candidate loop | Only highest PQ then EC | Valid lower PQ NS rejected | P1 | Fixed bounded 7/6/5/4 candidates, trial state, replay/DateTime | Reviewed; OpenSSL/non-PQ branches inspected | PQ type matrix | Pending cross-matrix |
| I2P-011 | SSU2 relay validation | SSU2 relay | `IntroductionManager` receive/validate | `SSU2Session::HandleRelayRequest/Response/Intro` | No signature/timestamp/version/replay checks | Relay amplification/spoofing | P0/P1 | Timestamp/version/length/signature/replay/resource checks; relay ver=2 | Reviewed; role heuristic noted | Forged/replayed relay cases | Pending live relay/HolePunch |
| I2P-012 | Timeouts/resends | NTCP2/SSU2 | `NTCPTransport`, `EventPumper`, `EstablishmentManager` | `NTCP2.*`, `SSU2Session.*` | Establish/idle values differed | Avoidable failures/churn | P2 | SSU2 25 s + backoff, NTCP2 10 s/120 s | Reviewed; internal policies documented | Lossy/latency matrix | Pending dynamic timing |
| I2P-013 | GOST 9/10 | proposal 134 | `SigType.java:66` reserves 9/10 | `Identity.h` local GOST | Java has no usable GOST wire verifier | Advertising would fail Java peers | P3 | No code change; blocked pending Java/dependency evidence | Evidence reviewed | N/A | Blocked |

### Security review follow-up (static)

- **OBGW tunnel-ID 0:** Java's outbound gateway legitimately uses receive tunnel
  ID 0; the earlier zero-ID guard in `TransitTunnel.cpp` was removed for both
  short and long ECIES records. The next tunnel ID is also not a zero-validity
  test in Java.
- **SSU2 reordered first data packet:** the receive baseline is now packet 0;
  a higher first packet is retained as out-of-sequence and a late packet 0 is
  accepted once. Duplicate packet 0 is still rejected.
- **SessionConfirmed late fragment 0:** the total-fragment count is checked
  before accepting fragment 0, so a late count change is fatal as in Java.
- **HolePunch DateTime:** the out-of-session payload is pre-scanned before
  `HandleRelayResponse()` can call `ConnectAfterIntroduction()`, and invalid
  blocks after parsing are rejected.
- **Meta LS2 published age:** Meta LS2 now rejects a published timestamp older
  than Java's 10-minute `earliest` bound, in addition to expiration and entry
  checks.
- **SAM invalid SEND:** an invalid `SIZE` now terminates the SAM socket instead
  of consuming the declared bytes and parsing payload as commands.

These are static fixes; the corresponding dynamic/negative tests remain
pending.

## Third static pass — historical 2.13.0 recheck 2026-10-19

The reference used for this historical pass was `i2p-2.13.0` at commit
`9134f808337b401e8e53c73734c81fab04280c9d`; the audited C++ tree was compared
against that tag and the normative specs. The following findings were not in
the earlier matrix. **No dynamic test was executed.**

| ID | Area | Java/spec reference | Finding | Correction / status |
|---|---|---|---|---|
| NET-155 | SSU2 receive window | `PeerState2.receivePacket`, `SSU2Bitfield`; SSU2 Packet Numbering | Bob baselined the data phase at packet 0 while Alice's first data packet is 1, so every packet was parked out of sequence and packet 512+ was silently dropped | Fixed (static): inbound first data packet advances the contiguous baseline; packet 0 remains accepted once |
| NET-156 | SSU2 cleanup ACKs | `SSU2Bitfield.fromACKBlock`; `PeerState2.finishMessages` | Cleanup advanced the receive baseline over unreceived packets and ACKed gaps | Fixed (static): no synthetic receipt; ACK encoder keeps NACK-only ranges and retransmission |
| NET-157 | SSU2 Address trust | `PeerState2.gotAddress`; `UDPTransport.externalAddressReceived` | Any established peer could publish a new external IP, including NTCP2, via an Address block | Fixed (static): ordinary Address observations are ignored; only the PeerTest state updates RouterContext |
| NET-158 | SSU2 path challenge | `PeerState2.MAX_PATH_CHALLENGE_SENDS/TIME` | A single lost challenge terminated the session at the next sweep | Fixed (static): bounded 5/10/20 s retransmission, 30 s deadline, same-family check, authenticated PathResponse and old-endpoint retention; dynamic loss test pending |
| NET-159 | Out-of-session MTU | `PacketBuilder2.encryptPeerTest/buildPing`; SSU2 MTU | PeerTest/HolePunch padding used the default 1420/1500 budget, not the peer/spec 1280 path | Fixed (static): out-of-session sessions clamp to `SSU2_MIN_PACKET_SIZE`; MTU capture pending |
| NET-160 | SSU2 flood flag | `PeerState2.gotRI`; `NTCP2Payload` | RouterInfo flag `0x01` is logged but not propagated | Fixed (static): only a newly accepted/replaced RouterInfo is forwarded with `CreateDatabaseStoreMsg` and NetDb floodfill selection, so a replayed old RI cannot trigger repeated flooding; amplification/DoS dynamic test pending |
| NET-161 | NTCP2 proxy startup | `NTCPTransport` proxy configuration | An invalid/unsupported `ntcp2.proxy` left `m_NTCP2Server` null and dereferenced it | Fixed (static): server is created independently; invalid proxy disables only the proxy |
| LS-119 | Encrypted inner flag | `EncryptedLeaseSet.setBlindedWhenPublished`; `LeaseSet2` | Decryption cleared the inner `PUBLISHED_ENCRYPTED` bit, breaking encrypted-LS refresh | Fixed (static): preserve the inner flag; outer public/expiry metadata remains authoritative |
| LS-120 | Meta earliest date | `MetaLeaseSet.addLease`; `LeaseSet.getEarliestLeaseDate` | Meta `GetEarliestLeaseDate()` stayed zero, so LS1↔Meta replacement was inverted | Fixed (static): Meta earliest entry is recorded and used for cross-type freshness |
| LS-121 | Inner unpublished bit | `EncryptedLeaseSet.sign` | Locally written encrypted inner omitted `UNPUBLISHED`, allowing extraction/republication | Fixed (static): emit `UNPUBLISHED | PUBLISHED_ENCRYPTED` |
| LS-122 | Encrypted blinded RSA type | `LeaseSet2.java:649-661`; `EncryptedLeaseSet.java:881-896` | `LeaseSet.cpp::ReadFromBufferEncrypted/ExtractPublishedTimestamp` | RSA blinded/transient signature type was accepted and verified on untrusted type-5 stores | 2048/4096-bit CPU amplification | P1 | Reject RSA before `CreateVerifier` in both parse/order views | Implemented (static); RSA header/negative test pending |
| LS-123 | LS2 PopulateLeases | `LeaseSet2.readBytes` | `LeaseSet::PopulateLeases` | Non-virtual method re-ran the LS1 parser on cached LS2, clearing validity and lease date | Cache invalidation/latent wrong key slot | P2 | Virtual no-op override in `LeaseSet2`; LS1 still reparses | Implemented (static); refresh regression pending |
| LS-124 | Meta effective expiry | `MetaLeaseSet.java:42-78`; `LeaseSet2.addLease` | `LeaseSet.cpp::ReadMetaLS2TypeSpecificPart` | Java re-serialises MetaLS2 with `_expires` = latest entry and zero revocations, so any other header fails its signature check | Local could store/flood/serve entries Java rejects | P1 | Reject non-canonical header expiry and any revocation (I2P-001) | Implemented (static); canonical fixture and negative vectors prepared |
| LS-125 | LS1 trailing bytes | `LeaseSet.java:307-320` | `LeaseSet.cpp::ReadFromBuffer` | LS1 stored/re-served bytes outside the signed body | Unauthenticated cache data / publish verification mismatch | P2 | Trim buffer to signed body + signature after verification | Implemented (static); canonical-buffer test added |

| DB-113 | Client DSRM parser | `DatabaseSearchReplyMessage.readMessage`; `SearchReplyJob` | Destination layer clamped the count and ignored mandatory trailing `from` | Fixed (static): use the strict parser and require a non-zero sender |
| DB-114 | Client DatabaseStore mask | `DatabaseStoreMessage` type mask | Destination layer switched on the raw type byte | Fixed (static): mask `0x0f` before dispatch |
| DB-115 | Clove origin bypass | `HandleGarlicMessageJob.java`, `InboundMessageDistributor.java:250-375` | `I2NPProtocol.h`, `RouterContext.cpp`, `NetDb.cpp` | `fromClove` could not distinguish direct-transport garlic from tunnel garlic, and the DLM allowlist omitted DatabaseLookup | Direct-garlic DLM and token DSM were dropped | P1 | `fromTunnel` marker, DLM allowlist, three-state DSM origin (NETDB-CLOVE-1) | Implemented (static); clove-origin/DSM tests prepared |
| DB-116 | Client DSRM binding | `IterativeLookupJob.java:37-68`; `SearchMessageSelector.java:79-89` | `Destination.cpp::HandleDatabaseSearchReplyMessage` | Any sender matching the public LS hash could inject peers and abort a lookup | Lookup injection, up to 255 follow-ups, replay starvation | P1 | `requestedPeers`/`repliedPeers`, one reply per queried peer, 16-hash cap, self/sender/dedup filter | Implemented (static); unqueried/replay vector pending |
| DB-117 | Floodfill lookup throttle | `LookupThrottler.java:18-60` | `NetDb::ShouldThrottleLookup` | No per-sender/reply-tunnel rate limit | Small DLM amplification/CPU | P2 | Bounded 14/2 min per `(reply hash, tunnel)`, self-key exempt | Implemented (static); 15-request/window test pending |
| DB-118 | DatabaseStore flood throttle | `FloodThrottler.java:18-38` | `NetDb::ShouldThrottleFlood` | Every updated token store flooded | Repeated valid updates amplify DSM floods | P2 | Store/ack unchanged; 3/90 s per key, bounded map | Implemented (static); 4-store/window test pending |
| DB-119 | Fresh zero-address RouterInfo | `RouterInfo.java:589-596`; `Kademlia...validate` | `RouterInfo.cpp`, `NetDb.cpp::IsRouterInfoStoreValid` | Any RI with no supported address was rejected outright | Fresh hidden/unknown-style RI never stored | P2 | Accept signature-valid zero-address RI for 54 min, never select it; persistence uses same bound | Implemented (static); fresh/55-minute fixtures pending |
| DB-120 | Persistence snapshot race | `Kademlia...store`; persistence invariant | `RouterInfo::TakeUpdatedBuffer`, `NetDb::SaveUpdated` | Delete/updated/buffer/flag were separate and could lose a concurrent update | Disk reverted to an older validated RI | P1 | One buffer-mutex snapshot; install body before publishing updated | Implemented (static); race harness pending |
| DB-121 | Reseed self-entry loss | Java persistent netDb keeps local router | `Reseed.cpp` (the former `NetDb::ResetRemoteRouterInfos` is removed) | Reseed cleared self RI/floodfill and could race Stop/Start | Self lookups stopped being served; late reseed wiped restarted netDb | P1 | Preserve local RI/floodfill, no-op after Stop, recheck under map lock | Implemented (static); reseed lifecycle test pending |
| DB-122 | Start count race | NetDb container invariant | `NetDb::Start` | Raw `m_RouterInfos.size()`/floodfill count read without locks | Data race / stale reseed decision | P2 | Use locked count accessors | Implemented (static); TSan test pending |
| DB-123 | RouterInfo file replacement | Java temp+rename persistence | `RouterInfo::SaveToFile` | Truncating write destroyed previous cache on crash/ENOSPC | Cache corruption/loss | P2 | Same-directory `.tmp`, flush, rename, cleanup on failure; failures logged | Implemented (static); fault-injection test pending |

| RS-101 | SU3 header validation | `SU3File.verifyHeader` | File-version byte, exact signature length, non-empty signer/content, and trailing signature data were not all checked | Fixed (static): strict header/signature checks and no trailing bytes |
| SAM-110 | SAM 3.3 ADD/REMOVE | `SAMv3Handler.java:551-566`; `PrimarySession.add/remove` | C++ replied `DESTINATION=` and closed the primary socket on subsession errors | Fixed (static): `SESSION STATUS RESULT=OK ID="…"` / `SESSION STATUS RESULT=I2P_ERROR ID="…" MESSAGE="…"`, socket kept open; `STYLE=PRIMARY` accepted; STREAM subsessions use LISTEN_PORT |
| DG-105 | Empty Datagram2/3 | `Datagram2/3.load`; `I2PSessionMuxedImpl.prepPayload` | A valid zero-output gzip stream was treated as decompression failure | Fixed (static): `InflateChecked` distinguishes valid empty output |
| STR-104 | Streaming MTU/pong | `ConnectionPacketHandler`; `ConnectionManager` | Peer `MAX_PACKET_SIZE` was ignored (including inbound SYN) and pong payload was unbounded | Fixed (static): clamp to peer `>= MIN_MESSAGE_SIZE` on both paths; cap pong at 32 bytes |
| CRY-202 | ElGamal/AES padding | `AESEngine.getPadding` | AES block padding was uninitialized heap data | Fixed (static): randomize padding before encryption |
| CRY-203 | Router N replay | `ECIESAEADEngine.decryptNewSession_N` | Router incoming N had no duplicate-ephemeral cache | Fixed (static): bounded 600 s/256-entry cache after AEAD and payload validation |
| CRY-204 | Ed25519ph primitive | `SigType.java:63`; `DSAEngine.altSign`; SU3 notes | Type 8 used a provider Ed25519ph/dom2 primitive while Java calls plain Ed25519 over the supplied bytes | Fixed (static): both paths use plain Ed25519; callers prehash where required; Java vector still absent |
| CRY-205 | RSA verification | `SigType.java` 4-6; `DSAEngine` | C++ had no RSA verifier for legacy RouterInfo/LS1 | Fixed (static): raw PKCS#1 v1.5 digest verification and certificate layout; validated legacy RouterInfo RSA is accepted, LeaseSet RSA remains rejected like Java, signing/key generation is unavailable and LS2 never advertises RSA. Java-generated key/signature vector pending |
| CRY-206 | Ratchet payload transactionality | `RatchetPayload.java:97-214`; `ECIESAEADEngine.java:591-919` | NSR/ES payload parser applied a valid prefix before a late framing/DateTime error | Fixed (static): `ValidateRatchetPayload` runs before cloves, ACKs, NEXT_KEY and termination |
| CRY-207 | New Session minimum | `ECIESAEADEngine.java:619-630,735-746` | Java 2.14.0 accepts an empty NS clove set, creates no session and sends no NSR | Deliberate stricter C++ policy: handshake validator requires a non-empty Garlic block; no wire compatibility change is claimed |
| CRY-208 | NS replay cache | `RatchetSKM.java:118,213` | 256-entry FIFO without age allowed eviction/replay inside the 5-minute window | Fixed (static): 4096-entry, 5-minute decaying cache |
| CRY-209 | Ratchet tag boundary | `RatchetTagSet.java:487-494` | C++ rejected index 65535 although Java accepts the full 16-bit space | Fixed (static): generate indices 0..65535, then fail without advancing |
| CRY-210 | Incoming tag budget | `Garlic.cpp::AddECIESx25519SessionNextTag` | Tag chain advanced before the map-budget check, skipping a peer tag; a full map permanently denied all sessions | Fixed (static): reserve before advancing; evict expired/terminated entries and then arbitrary oldest entries so a hostile session cannot deny new handshakes |
| CRY-211 | PQ publication | `UDPTransport.addSSU2Options`; SSU2 HYBRID MTU | LibreSSL could publish `pq=3` although ML-KEM-512 is absent; no-PQ builds could parse/select peer PQ hints | Fixed (static): publication and selection require a supported ML-KEM variant |
| CRY-212 | Hybrid key advertisement | `spec/common-structures`; `LeaseSet2` | A non-PQ build could keep a type-5/6/7 identity key in its encryption-key map | Fixed (static): unsupported hybrid types are filtered before LS2 publication |
| CRY-213 | RedDSA key lifetime | Zcash RedDSA key handling | RedDSA signer destructor did not wipe the private scalar | Fixed (static): `OPENSSL_cleanse` on destruction |
| CRY-214 | Inbound session replacement age | `RatchetSKM.SESSION_REPLACE_AGE` | A pending inbound NS had `m_SessionCreatedTimestamp == 0`, so any valid NS could replace it immediately | Fixed (static): set the creation timestamp when the NS is authenticated |
| CRY-215 | Protocol RNG seed | `I2PAppContext.random()` | `util.cpp::GetRngSeed` | Seeds `mt19937` from a 32-bit monotonic clock; `PeerOrdering` read an uninitialized key on CSPRNG failure | Predictable/colliding padding, msgID, tunnel order and peer groups | P1 | CSPRNG-derived 32-bit seed with mixed fallback; zeroize and fail closed in `PeerOrdering` | Implemented (static); distinct-seed/negative tests prepared |
| CRY-216 | AEAD multi-buffer failure | `ChaChaPolyCipherState.encryptWithAd` | `Crypto.cpp`, `NTCP2.cpp` | The multi-buffer encryptor returned `void`, zeroed only the MAC and the caller wrote the frame | Partial ciphertext/plaintext could reach the wire with a zero tag | P0 | Return `bool`, cleanse every output on failure, abort `Terminate()` before sequence increment | Implemented (static); provider-fault test pending |
| CRY-217 | Secret lifetime | Java per-call cipher state destruction | `RatchetTagSet`, AES classes, `NTCP2Session`, session destructor | Tag roots, AES keys and NTCP2 KDF/SipHash keys were freed without wiping | Key material survives freed heap/stack | P2 | `OPENSSL_cleanse`/`Tag::Wipe` in destructors | Implemented (static); placement-new/ASan checks pending |
| CRY-218 | Ratchet tag amplification | `RatchetSKM.java:1493-1494` | `ECIES…::HandlePayload`, `Garlic.cpp` | Up to four NEXT_KEY blocks and a permanently full global tag map | One authenticated payload could allocate several 800-tag windows and deny other sessions | P1 | One forward + one reverse NEXT_KEY per payload; eviction/expiry before budget refusal | Implemented (static); abuse/replay vectors pending |
| CRY-219 | In-process ratchet reply | `ACKTimer.java:23-63`; `ECIESAEADEngine` | `Destination.cpp`, `ECIES…` | Only `I2CPDestination` armed the NSR/ACK fallback; SAM/HTTP/API/datagram destinations had a no-op | Silent applications left peers in a pending session | P1 | Base `ClientDestination` timer, one armed timer per session, re-check `IsResponseRequired()` | Implemented (static); timer/lifecycle test pending |
| CRY-220 | LS2 offline RSA transient | `LeaseSet2.java:649-668` | `LeaseSet.h::ProcessOfflineSignature` | Any RSA transient key was accepted for LS2 though Java rejects it as a DoS vector | Untrusted LS2 could force 4096-bit RSA verification | P1 | `allowRSA=false` for LS2/encrypted LS2; keep Datagram2/Streaming permissive | Implemented (static); RSA/P-521 policy vectors pending |
| CRY-221 | Ed25519 key decoding | `GroupElement.java:339-343` | `Blinding.cpp`, `Ed25519.cpp` | Blinding accepted a 32-byte key whose recovered x is not on the curve | Key/credential/store-hash derived from an invalid point | P1 | `IsValidPublicKey()` curve check before blinding/b33 acceptance; keep Java-valid low-order points | Implemented (static); invalid-point/b33 tests pending |
| CRY-222 | GOST e=0 / scalar lifetime | RFC 7091 §6; BouncyCastle ECGOST3410 | `Gost.cpp`, `Ed25519.cpp`, `Signature.h` | GOST used `e=0` instead of the required `e=1`; several private scalars used `BN_free` | Non-interoperable signatures; key remnants in freed memory | P2 | Normalize e in sign/verify; `BN_clear_free` for private scalars | Implemented (static); e=q/e=0 sign/verify vectors prepared |
| CRY-223 | PQ version fallback | `OutboundEstablishState2.createNewState()` | `SSU2Session.cpp` | A downgrade to SSU2 v2 could reuse a stale ML-KEM key; the `IncompatibleVersion` Retry cascade was dead code | Malformed v2 request or unusable peer after version decline | P1 | Reset KEM state per attempt; consume version termination before abort and retry at 3/2 | Implemented (static); provider/type/Retry matrix pending |
| CRY-224 | Local LS2 encryption policy | `ClientConnectionRunner` leaseSetEncType parsing | `Destination.cpp` | An explicitly configured but unsupported type list was silently replaced by defaults | Operator policy discarded; weaker key set published | P1 | Fail closed when an explicit list yields no supported type; defaults only when omitted | Implemented (static); provider/type tests pending |
| CRY-225 | I2CP private-key lengths | `CreateLeaseSet2Message.readBytes`; `EncType` | `libi2pd_client/I2CP.cpp` | Declared key length was ignored; fixed-size reads over-read or truncated keys, unknown types accepted | Over-read of stale bytes as long-term keys; unusable LS2 key published | P0 | Exact per-type lengths, reject unknown types and mismatches, zero-fill legacy P256 storage | Implemented (static); over-read/type matrix pending |
| SAM-111 | SAM parser grammar | `SAMUtils.parseParams` | Quotes/escapes, repeated spaces and bare PING/PONG are not fully parsed | Fixed (static) for bare PING/PONG/HELP/QUIT, case-insensitive command/key normalization, quoted/escaped values, repeated-space command words, persistent bounded framing and pipelined commands; SAM 3.3 `LISTEN_PROTOCOL`, (type,protocol,port) duplicate keys and protocol-aware RAW dispatch are implemented statically (`test-sam-subsessions`) |
| I2CP-105 | BlindingInfo TYPE_KEY/secret | `ClientMessageEventListener.handleBlindingInfo`; `LookupDestJob` | TYPE_KEY is merged with TYPE_DEST, but secret-blinded/client-auth decryption is absent | Partial (static): routing sessions are now keyed by the parsed full identity hash, closing cross-destination reuse; secret-blinded/client-auth decryption remains residual |

The stale “no newer stable” date, the DG-103 “not implemented” row and the
NTCP2 outgoing-replay residual have been corrected above.

## Client-protocol static pass — 2026-10-19

| ID | Area | Java/spec reference | Correction / status |
|---|---|---|---|
| SAM-112 | Large SEND/RECEIVED frames | `SAMv1Handler.java:538,826-894` | Control buffer raised to 49 KiB and receive buffer to 64 KiB, covering 32 KiB RAW and 31 KiB datagram frames. Static test assertions added; dynamic framing pending. |
| SAM-113 | SILENT STREAM CONNECT | `SAMv3StreamSession.java:163-232` | I2P→client receive now starts independently of the reply write, with a single-outstanding-read guard. |
| SAM-114 | STREAM ACCEPT subsession | `PrimarySession.java:96-109,390-425` | STREAM subsessions use `LISTEN_PORT`; ACCEPT arms the matching per-port `StreamingDestination`, including queued-acceptor promotion. |
| SAM-115 | Accept queue lifetime | `SAMv3StreamSession.java:108-137,236-263` | Three-second expiry removed; FIFO transfer keeps queued acceptors alive. Promotion on armed-socket teardown remains residual. |
| SAM-116 | SIGNATURE_TYPE | `SigType.java:288-325` | Numeric types are validated through `CreateVerifier`, names are case-insensitive, and type 8 is accepted. |
| I2CP-106 | b33 BlindingInfo association | `BlindData.java:224-227`; `LookupDestJob.java:105-107` | The cache is keyed by the b33 unblinded signing key instead of comparing it with a derived blinded key that can never match. |
| I2CP-107 | HostReply LS2 options | `LookupDestJob.java:254-284`; `HostReplyMessage.java:151-158` | Standard LeaseSet2 properties are serialized for lookup types 2-4; empty mapping remains the fallback. |
| I2CP-108 | TYPE_KEY/TYPE_DEST merge | `ClientMessageEventListener.java:875-914` | A TYPE_KEY update no longer overwrites a TYPE_DEST identity/auth/secret/expiry. Secret-blinded decryption remains fail-closed. |
| DG-106 | Datagram1 full session table | `DatagramDestination::ObtainSession` | Null session is rejected instead of dereferenced. |
| DG-107 | Datagram version/hash delivery | `SAMDatagramSession.java:137-208` | Received protocol no longer mutates an existing session's outgoing version; Datagram3 is delivered to a HashReceiver and prefers a fresh LeaseSet; the full-identity path is limited to internal consumers with a cached verified LeaseSet. |
| STR-105 | Incoming stream without cached LeaseSet | `ConnectionManager.java:243-410` | The incoming stream requests the LeaseSet asynchronously and stays pending; it no longer terminates a valid first contact. |
| STR-106 | Incoming SYN MTU | `ConnectionManager.java:371-402` | The SYN `MAX_PACKET_SIZE` is applied to the incoming stream before SYNACK. |
| STR-107 | Streaming offline/close authentication | `Packet.java:797-814`; `ConnectionPacketHandler.java:314-317,593-629` | Offline delegation is always parsed/expiry-checked; unsigned CLOSE/RESET without a cached LeaseSet is rejected. |

### Known remaining incompatibilities (not claimed fixed)

`LISTEN_PROTOCOL` routing is implemented statically; I2CP secret-blinded/TYPE_KEY b33 authorization, per-session outgoing Datagram1/2/3 selection, Datagram2 offline-signature vectors, proposal-154 derived-key reply KDFs, and GOST signature types 9/10 toward Java. The Java 2.14.0 tree has no usable caller for the 0x12 public-key lookup form; that path is fail-closed and marked pending. RelayResponse role disambiguation, type-5/type-7 NSR vectors and the 7/4 multi-key session matrix remain residual. SSU2 Path Challenge retransmission, RouterInfo flood-request propagation, RSA verification/identity-key layout, runtime ML-KEM provider probing, SAM persistent pipelining/bare controls/quoted values and I2CP identity-hash routing keys are now implemented statically, but none has dynamic evidence. Some timeout/keepalive policies need dynamic evidence.

## 2026-10-20 functional-parity continuation

The full feature inventory is in [`FEATURE-PARITY.md`](FEATURE-PARITY.md).
This pass added the following static changes; **no build, CTest, sanitizer,
fuzzer, Java peer, Android device, OpenBSD, Windows or live-network run has
validated them**:

| Area | Java reference | Static change | Remaining evidence |
|---|---|---|---|
| SSU2 migration | `PeerState2.java` | Bounded retries/deadline, authenticated PathResponse and old-endpoint retention | Packet-loss/path migration test |
| ML-KEM | `EncType.java`, ML-KEM provider | Cached runtime provider probe gates publication and key creation | OpenSSL/LibreSSL provider matrix and Java PQ session |
| RSA/SU3 | `SigType.java`, `SU3File.java` | RSA raw verification, `reseed.verify=true` default, Ed25519/RedDSA SU3 paths | Java-generated key/signature and signed-reseed bundle |
| SAM | `SAMv3Handler`, `SAMUtils` | Persistent bounded framing, pipelined commands, bare controls and case-insensitive keys | Split-frame, pipelined and live SAM client tests |
| I2CP | `ClientMessageEventListener`, `LookupDestJob` | Routing-session cache keyed by parsed identity hash | Secret-blinded auth and live Java client |
| Address book | `apps/addressbook`, `NamingService` | ASCII case folding with exact b33/base64 spelling | Live naming lookup and subscription update |
| Torrents | I2PSnark peers/tracker/resume | Re-verification, tracker forms/limits, private announce, PEX, peer IDs and connection caps | Live Java trackerless/tracker exchange and crash recovery |
| TUI administration | Java console operations | Typed daemon queue for reseed, shutdown, reload, bandwidth, share, transit and log level | Manual TUI/daemon lifecycle tests |
| Android administration | Java console operations | Confirmed Reseed action and runtime log-level apply through service-worker JNI and a closed set | Gradle/JNI/device and accessibility tests |

The TUI deliberately exposes a closed command set rather than a generic
configuration writer or shell. The Android application continues to use
Material 3/Java and the shared native engine; no web console or Java runtime is
reintroduced.

## Confirmed findings and fixes (hardening pass)

These follow-up findings come from code inspection and the I2P specifications.
The Java 2.13.0 reference was used for this historical audit; the
findings below remain **Fixed (validation pending)** until the listed
differential test is executed.

The new regression source is `tests/test-protocol-bounds.cpp`; a libFuzzer
harness for the same parsers is `tests/fuzz/fuzz-protocol-bounds.cpp`. Both
are registered in `tests/CMakeLists.txt` (`test-protocol-bounds` in
`I2PD_TESTS`; `fuzz-protocol-bounds` in the `WITH_FUZZERS` loop).

### LS-001: LeaseSet expiration pre-scan read one byte past the buffer

- **C++ before:** `LeaseSet::ExtractExpirationTimestamp()` checked
  `size > len` after adding the encryption and signing key lengths, then read
  `buf[size]`. When `size == len` it read one byte past the end.
- **Impact:** malformed/truncated LeaseSet data could cause an out-of-bounds
  read while netDb compares expiration times (`IsNewer`).
- **Fix:** require `size < len` before reading the lease count.
- **Validation:** regression case in `tests/test-protocol-bounds.cpp`
  (`IsNewer` on a 771-byte truncated LeaseSet). Static only here.

### LS-002 / NETDB-001: DatabaseStore key was not bound to payload identity

- **C++ before:** `NetDb::AddRouterInfo(ident, ...)`, `AddLeaseSet()` and
  `AddLeaseSet2()` validated the signature with the identity embedded in the
  payload, then cached the object under the `ident` taken from the
  DatabaseStore key. Nothing checked that the two identities matched.
- **Impact:** a signed RouterInfo or LeaseSet for destination A could be
  stored/re-published under key B (cache poisoning), and a standard LeaseSet
  update could overwrite an existing entry with leases signed by another
  destination.
- **Spec:** DatabaseStore key is the RouterInfo ident hash, or the LeaseSet
  destination hash; encrypted LeaseSet2 uses the blinded store hash instead.
- **Fix:** compare the announced identity hash with the store key for
  RouterInfo, LeaseSet and non-encrypted LeaseSet2. Encrypted LeaseSet2 is
  exempt because its key is the blinded store hash.
- **Validation:** static; needs a controlled DatabaseStore test with mismatched
  key/identity (must be dropped) and matching key/identity (must be stored).

### RI-002: RouterInfo peer-list and 4096-byte acceptance

- **C++ before:** the peer count was multiplied by 32 and added to `offset`
  before any bound check. The following properties check rejected the packet,
  so no out-of-bounds read was observed, but the parser accepted an invalid
  intermediate offset.
- **Separately:** `NetDb::HandleDatabaseStoreMsg()` decompressed into a
  4096-byte buffer and required `uncompressedSize < MAX_RI_BUFFER_SIZE`,
  rejecting a valid RouterInfo of exactly 4096 bytes; `LocalRouterInfo::
  CreateBuffer()` used the same strict `<` for its own buffer.
- **Fix:** explicit `offset + numPeers*32 <= len` check; accept
  `uncompressedSize <= MAX_RI_BUFFER_SIZE` and generate a local RouterInfo
  that exactly fills the buffer.
- **Validation:** static; the large-RI fixture from RI-001 is still needed.

### RI-003: `router.version` integer overflow on untrusted input

- **C++ before:** `RouterInfo::ReadFromBuffer()` accumulated every decimal
  digit of the `router.version` property into a signed `int` without an
  overflow check. A long numeric property could trigger signed overflow (UB)
  and produce an arbitrary version value.
- **Impact:** malformed RouterInfo properties could produce undefined behavior
  and bypass version-based peer selection checks.
- **Fix:** stop parsing and set the version to 0 when the next digit would
  overflow `int`.
- **Validation:** static; add a malformed `router.version` fixture to the
  RouterInfo fuzz/corpus once registered.

### I2NP-001: serialization bounds for DatabaseLookup/SearchReply/DatabaseStore

- **C++ before:**
  - `CreateDatabaseSearchReply()` wrote `routers.size()` into a one-byte
    field and copied every router into a fixed 4096-byte short message.
  - `CreateDatabaseStoreMsg()` for a LeaseSet copied up to
    `MAX_LS_BUFFER_SIZE` (4096) bytes into a short message with a 37-byte
    DatabaseStore header, overflowing the payload buffer.
  - DatabaseLookup excluded-peer sets were copied without relation to the
    serialized message capacity (the LeaseSet lookup capped at 512, the
    RouterInfo lookup did not).
- **Fix:** select the I2NP buffer from the exact required length, cap the
  one-byte SearchReply count at 255, cap excluded-peer counts to the message
  capacity, and reject LeaseSets that cannot fit.
- **Validation:** static; `test-protocol-bounds` covers the pure length
  helpers. The DatabaseStore overflow cases need the new
  `fuzz-protocol-bounds` ASAN target to be registered and run.

### NTCP2-001: data-phase frame block header

- **C++ before:** `ProcessNextFrame()` entered the loop for any remaining
  byte and read a 3-byte block header.
- **Fix:** require three bytes before reading type/size, matching the existing
  SSU2 `HandlePayload()` loop.
- **Validation:** static; malformed frame corpus pending.

### SSU2-001: PathResponse block length

- **C++ before:** `SendPathResponse()` compared only `payloadSize + len`
  against `m_MaxPayloadSize`, then wrote `len + 3` bytes (block header).
- **Fix:** include the 3-byte header in the bound.
- **Validation:** static; peer-test/path-validation dynamic test pending.

### SSU2-002: fragment reassembly table was unbounded

- **C++ before:** `SSU2Session::HandleFirstFragment()` and
  `HandleFollowOnFragment()` inserted into `m_IncompleteMessages` for every
  new message ID. Entries were only removed by the 30-second cleanup timer, so
  a peer could keep many partial messages (and their growing I2NP buffers)
  alive.
- **Fix:** cap the per-session incomplete-message table at
  `SSU2_MAX_NUM_INCOMPLETE_MESSAGES` (128) and mark the packet invalid when
  full. This is a local resource policy, not a wire-format change.
- **Validation:** static; needs a malformed-fragment rate test.

### SSU2-003: LibreSSL could advertise an unsupported PQ version

- **C++ before:** `SSU2Server::SetVersion()` stored 3 even when built
  against LibreSSL, while `SSU2Session::SetVersion()` mapped 3 back to 2
  because ML-KEM-512 is unavailable. A RouterInfo could therefore advertise
  PQ version 3 while sessions established only version 2.
- **Fix:** the server now maps 3 to 2 under `LIBRESSL_VERSION_NUMBER`, matching
  the session capability and the NTCP2 behavior.
- **Validation:** static only; needs a LibreSSL build plus published-address
  inspection and a Java 2.13.0 PQ/non-PQ connection attempt.

### NETDB-003: `SaveUpdated()` iterated the router map without its mutex

- **C++ before:** `NetDb::SaveUpdated()` iterated `m_RouterInfos` while
  transport threads could insert or erase entries under
  `m_RouterInfosMutex`, risking iterator invalidation and a data race.
- **Fix:** take a shared-pointer snapshot of the map under the mutex and lock
  `m_FloodfillsMutex` while reading the floodfill count. The later erase pass
  continues to take the mutex.
- **Validation:** static only; a TSan save/add/erase test and live netDb
  persistence are pending.

### RI-004: RouterInfo address option sections were not bounded

- **Java:** `router/java/src/net/i2p/data/router/RouterInfo.java:88, 589-594`
  enforces `MAX_ADDRESSES = 16`; `RouterAddress.java:285-297` reads each string
  and the address option mapping; `core/java/src/net/i2p/data/DataHelper.java:189-220`
  rejects truncated strings, bad `=`/`;`, duplicate keys and unsorted keys.
  `RouterInfo.java:97` keeps `MAX_UNCOMPRESSED_SIZE = 4*1024`.
- **C++ before:** `RouterInfo::ReadFromBuffer()` called
  `ExtractParam (buf + offset, len - offset)` for every address option, where
  `len` was the whole remaining RouterInfo rather than the address block's
  declared `size`. A parameter length could therefore cross into the next
  address, the peer list, the property mapping or the signature. Strings were
  clamped instead of rejected, there was no 16-address limit, duplicate
  `host`/`port`/`s`/`i`/`v`/`pq` options overwrote earlier values, and
  introducer indices above 2 were accepted.
- **Impact:** a malformed signed RouterInfo from a peer could desynchronize the
  address parser and be accepted with inconsistent metadata. The outer buffer
  is fixed at 4096 bytes and signature verification still covers the complete
  message, so no out-of-bounds read or signature bypass is demonstrated; the
  defect is parser integrity and wrong address/transport interpretation.
- **Fix:** `ExtractString()` now rejects lengths beyond the supplied view;
  `ExtractParam()` returns a zero size for every malformed key/value/`;`
  sequence. Both the address and property loops pass only their declared
  section remainder, reject `sz == 0` or `sz > remaining`, and cannot advance
  past the section. Address count is capped at `MAX_RI_ADDRESSES` (16), exact
  duplicate option/property keys are rejected, ports must be 0-65535,
  introducer options are limited to indices 0-2, the peer count is checked
  before multiplication, and the 4096-byte cap/signature path is unchanged.
  The parsed body now excludes the trailing signature and must end exactly
  after the property mapping, so properties cannot overlap signature bytes.
  `RouterInfo::Update()` now propagates a parse failure instead of saving a
  malformed but correctly signed buffer.
- **Validation:** static only. Needed: RouterInfo fixtures with an option length
  crossing into the next address, 17 addresses, duplicate `host`/`port`/`s`,
  truncated string, missing `;`, introducer index 9, and a valid 4096-byte
  Java RouterInfo.

### SSU2-004: cross-family Address block could overwrite local reachability

- **Java:** `router/java/src/net/i2p/router/transport/udp/SSU2Payload.java:308-315`
  accepts only 6-byte (IPv4) or 18-byte (IPv6) Address blocks;
  `PeerState2.java:703-706` stores the reported IP/port unvalidated and does
  not call `UDPTransport.externalAddressReceived()`. Java's address update path
  validates family and port (`UDPTransport.java:1354-1382`).
- **Spec:** SSU2 `Address` block reports the recipient's apparent IP/port.
  The Connection Migration section permits treating an address in the other
  family as invalid and does not require IPv4/IPv6 migration.
- **C++ before:** `SSU2Session::HandleAddress()` accepted any valid-length
  Address block and immediately called `RouterContext::UpdateAddress()`, then
  updated NAT error state and could publish an SSU2 address, without comparing
  the block's family with the session's family.
- **Impact:** an authenticated SSU2 peer could make the router overwrite the
  other address family's external IP and reachability/port state, potentially
  publishing an incorrect address and disrupting connectivity. The block is
  inside the AEAD-protected data phase, so this is a malicious-peer issue, not
  an off-path forgery; there is no memory-safety impact.
- **Fix:** compare `ep.address()` with `m_Address->IsV4()`/`IsV6()` (falling
  back to the remote endpoint family if `m_Address` is temporarily unset) and
  ignore a mismatch before `UpdateAddress`, error/port handling or publishing.
  The message is not marked invalid, matching Java's non-fatal handling of an
  Address block. Same-family IPv4/IPv6 peer tests keep their existing path.
  An Address block with a length other than 6 or 18 now marks the packet
  invalid, matching Java's `SSU2Payload` exception.
- **Validation:** static only. Needed: IPv4 session plus IPv6 Address block and
  the reverse (must not update), same-family update, and the existing SSU2
  peer-test/path-validation matrix on both families.

### Residual code-level concerns (not changed in this pass)

- `NetDb::SaveUpdated()` now snapshots `m_RouterInfos` under
  `m_RouterInfosMutex` and locks the floodfill count before iteration.
  TSan and a live lookup/save test are still required.
- `LeaseSet2::ExtractPublishedTimestamp()` and the offline-signature helpers
  use `>=` where an exact-fit buffer is legal. This is stricter than necessary,
  not unsafe; verify against Java before relaxing.

## Residual code-level concerns (not changed in this pass)

- SSU2 `SendSessionConfirmed()` now emits up to 15 MTU-sized fragments; a
  RouterInfo requiring more than the 4-bit fragment space is rejected rather
  than malformed.
- SSU2 `Stop()` synchronously waits on a future posted to the service thread;
  callers must not invoke it from that thread. Shutdown ordering is not
  dynamically tested.
- NTCP2 now checks bytes already queued after message 1/2, but bytes still in
  flight in the kernel are only detectable with a buffered state machine.
- NTCP2 Alice-side message 2 replay caching is implemented by the bounded
  outgoing replay filter; only live kernel-in-flight probing remains untested.
- NTCP2 Termination now closes from the write completion; a write that stalls
  is force-closed on the next timeout sweep.
- I2CP BlindingInfo `TYPE_KEY` entries are merged but not yet used for a full secret/client-auth b33 path; secret-blinded b33 remains fail-closed. Standard LeaseSet2 options are now returned for HostReply types 2-4.
- ElGamal/AES AES-block padding is now randomized; the older zero-filled
  ElGamal plaintext tail remains a non-wire hygiene difference.
- `LeaseSet2::Update()` reparses type 5 and resets validity; the decrypted
  object still changes its store type to the inner type, so a later update
  must be tested explicitly.
- Ed25519ph/type 8 now uses plain Ed25519 over the supplied bytes like Java;
  no RFC 8032 dom2 primitive is assumed. A Java-generated type-8/SU3 vector is
  still required before claiming interoperability.
- Ratchet NSR/ES payloads are structurally pre-validated before side effects;
  the handshake still does not have a Java-generated malformed corpus. The
  runtime ML-KEM provider probe is implemented statically but has no
  OpenSSL/LibreSSL matrix or Java PQ session evidence.
- SAM no longer expires queued STREAM ACCEPTs after three seconds, and STREAM
  subsessions use `LISTEN_PORT`; persistent bounded framing, pipelining and
  quoted parameter values are now implemented statically. `LISTEN_PROTOCOL`
  routing, receiver reset and live large-frame execution remain residual.
- No Java peer, network handshake, ASan/UBSan/TSan, libFuzzer, LibreSSL,
  OpenBSD, Windows, Android or multi-UID validation exists for these changes.


## 2026-10-21 SSU2/NTCP2 continuation (static)

A focused read-only pass compared handshake, ACK, fragmentation, relay,
PeerTest, MTU/IPv6, session lifetime and NTCP2 framing against Java 2.14.0.
Implemented by static analysis; no compiler, sanitizer, fuzzer or live peer
ran.

| ID | Area | Finding | Correction |
|---|---|---|---|
| SSU2-RX-1 | Receive window | A late packet 0 rewound `m_ReceivePacketNum` after reordered packets, allowing duplicates and a regressing ACK. | Late packet 0 is accepted once without moving the contiguous receive point; `ackThrough` remains monotonic. |
| SSU2-DOS-1 | Unauthenticated type | A wrong-type short packet directly retransmitted handshake fragments/ACKs, giving bounded input unbounded output. | Wrong type is logged and dropped; only the bound/backoff timer resends handshakes. |
| SSU2-SESS-1 | Session removal | Removal by endpoint/ConnID could unregister a live duplicate handshake. | Removal is identity-checked with the owning `shared_ptr`; deferred removal uses a weak owner. |
| SSU2-STATE-1 | SessionCreated failure | A failed post-MixHash/MixKey SessionCreated left poisoned Noise state and forced a stale-token Retry. | Failed handshake material is reset; a Retry reinitializes cleanly and stale datagrams fail closed. |
| SSU2-RESEND-1 | Resend throttle | The minimum-interval predicate was inverted and never throttled scans. | Monotonic `last + interval` predicate. |
| SSU2-PT-1 | PeerTest msg 1 | Bob forwarded an unauthenticated, attacker-chosen test endpoint without source/family/reserved/port checks. | Msg 1 target must match the established source address (v4 exact, v6 /64) and pass `IsValidPeerTestEndpoint`. |
| SSU2-PT-2 | PeerTest/Relay target | Charlie could be induced to send msg 5/HolePunch to loopback/private/reserved targets. | Both paths use `IsValidPeerTestEndpoint`; rejected intros do not mint tokens. |
| SSU2-PT-3 | PeerTest freshness | Signed PeerTest 1-7 timestamps were ignored, allowing stale/replayed tests. | ±120 s skew check before state changes in both handlers. |
| SSU2-RLY-1 | HolePunch amplification | RelayIntro had no per-target dedup/rate/capacity limit. | Bounded five-second/per-target and 20-target HolePunch admission. |
| SSU2-CB-1 | Established callback | `Established()` could drain before a transports-thread callback registration, losing PeerTest/introduction work. | Late registration is posted to the SSU2 service thread instead of being dropped. |
| NTCP2-CAP-1 | SessionConfirmed cap | 6000-byte msg3p2 cap was the old Java 2.13 value; Java 2.14 uses 4048/459. | Constants, send/receive checks and static assertions aligned. |
| NTCP2-TERM-1 | Termination reason | `REASON_BANNED` was logged but not applied. | Reason 17 marks the peer unreachable, matching Java's banlist signal. |

Prepared regressions cover reordered SSU2 packet 0/ACK monotonicity, wrong-type
flooding, duplicate-session removal, failed SessionCreated recovery, PeerTest
source/family/timestamp/private-target rejection, HolePunch rate limits,
Established callback races, NTCP2 4048/459 boundaries and banned termination
handling. They remain pending CI compilation/tests and TSan/ASan execution.


## 2026-10-21 tunnel-build/I2NP/Garlic continuation (static)

Read-only audits compared tunnel construction, peer selection, time windows,
I2NP framing/dispatch, garlic cloves and multiprocess boundaries with Java
2.14.0. All following changes are implemented by static analysis only; no
compiler, CTest, sanitizer, fuzzer or live tunnel ran.

| ID | Area | Finding | Correction |
|---|---|---|---|
| TU-102 | Tunnel path uniqueness | A peer could appear twice in one path; Java accumulates every selected peer in the exclude set. | Reject duplicate hashes with bounded selection retries. |
| TU-103 | Transit tunnel IDs | A hostile build record could install receive/next tunnel ID 0, which is also a local/router sentinel. | Drop zero receive or next IDs before creating a transit tunnel. |
| TU-104 | Zero-hop/variance | Zero-hop decisions used the base length, ignoring selected variance, and the replacement outbound tunnel was not published. | Decide from `path.peers`; publish `newTunnel` when established. |
| TU-105 | Build-record shuffle | `GetRngSnapshot()` copied the RNG without advancing it, so independent builds could use identical permutations. | `Tunnels::Shuffle()` advances the shared RNG under its mutex. |
| TU-106 | Pre-expiry recreation | The hop-count gate rejected intentionally varied tunnel lengths. | Accept the configured base/variance range in both directions. |
| I2NP-006 | OBEP LOCAL delivery | A LOCAL delivery at an outbound endpoint was injected as a direct I2NP message. | Drop LOCAL at OBEP; IBEP local delivery remains valid. |
| I2NP-007 | Clove type whitelist | Cloves could carry TunnelData/TunnelGateway/builds into the tunnel worker. | Allow only Garlic, DatabaseStore/SearchReply, DeliveryStatus, Data and build replies. |
| I2NP-008 | Gateway truncation | TunnelGateway allocation omitted its six-byte header and silently truncated at buffer-tier edges. | Reserve the header, reject >65535-byte bodies and fail on `Concat` overflow. |
| I2NP-009 | Inner expiration | Only the gateway wrapper expiration was checked. | Enforce the inner I2NP ±clock-skew window and minimum header length. |
| I2NP-010 | IV replay/reassembly | No per-tunnel IV filter and unbounded incomplete/out-of-sequence tables. | Bounded 10-minute/512-IV filter and 128-message/4096-fragment caps. |
| GAR-001 | Local clove length | `LeaseSetDestination` could use an unvalidated I2NP length in local clove delivery. | Validate length before payload arithmetic and dispatch. |
| GAR-002 | Clove freshness | ElGamal/ECIES clove expirations were ignored, allowing replay and reflection. | Reject cloves outside Java's -90 s/+240 s window. |
| GAR-003 | Encryption failure | A missing encryptor caused a no-op and cleartext heap transmission. | Encryption returns `bool` end-to-end; failed ElGamal/ratchet wrapping returns null. |
| GAR-004 | Null router session | `WrapMessageForRouter` dereferenced a null session for unsupported router crypto types. | Null-check and return; X25519-capable routers accept hybrid types defensively. |
| CONC-006 | Custom selector lifetime | `MatchedTunnelDestination` registered a raw pointer and never unregistered. | Owner `weak_ptr`, unregister on Stop/destructor, activity re-check. |
| CONC-007 | Peer config race | Explicit/trusted peer vectors were swapped while the tunnel worker read them. | Copy under `m_PeersConfigMutex` before selection. |

Reply-key `0x12` remains fail-closed (Java 2.14.0 has no sender/KDF). Tunnel
keys, build records and hop Noise state remain core-only; no storage worker or
IPC snapshot receives them. Residual: adaptive build shaping/backoff,
RouterInfo copy-on-write, RouterProfile synchronization and live
multi-process/tunnel validation.


## 2026-10-21 SAM/I2CP/Streaming/Datagram continuation (static)

Four read-only audits covered SAM 3.2/3.3 framing, I2CP session/key handling,
Streaming packet state and Datagram1/2/3. All changes below are static-only;
no compiler, CTest, sanitizer, fuzzer or Java peer ran.

| ID | Area | Finding | Correction |
|---|---|---|---|
| SAM-117 | Per-port STREAM ACCEPT teardown | `StopAcceptingStreams()` reset only port 0 and closed sockets stayed in the accept queue. | Reset the subsession port, remove the queued socket and reject a stream arriving on a closed acceptor. |
| SAM-118 | Subsessions | DATAGRAM/RAW accepted without PORT and duplicate protocol/port pairs. | Require PORT and reject duplicate `(type, port)` pairs. |
| SAM-119 | SESSION CREATE errors | Parameter failures answered `STREAM STATUS`. | Return `SESSION STATUS RESULT=I2P_ERROR`. |
| SAM-120 | HELLO parsing | Multiple spaces/tabs were rejected, unlike Java tokenisation. | Tokenise `HELLO VERSION` case-insensitively with shared separators. |
| SAM-121 | Shutdown accounting | `CloseSessions` skipped `StopLocalDestination`, unbalancing `Acquire`. | Stop the destination before closing the session. |
| I2CP-109 | b33 HostReply | The unblinded BlindingInfo identity was compared with the blinded LeaseSet, making successful lookups fail. | Validate the blinded LS2 store key actually requested. |
| I2CP-110 | MESSAGE_STATUS | A failed cached route emitted failure and then fallback success for one nonce. | One terminal status owner; `SendMsgTo` owns fallback statuses. |
| I2CP-111 | CREATE_LEASESET2 | Keys were installed before the key list was fully validated. | Stage/validate count/type/trailing bytes, then install and publish. |
| STR-108 | Saved packets | Pre-SYN follow-on packets and per-ID lists were unbounded. | 512 stream IDs and 64 packets per stream. |
| STR-109 | Stream map | Packet dispatch read `m_Streams`/`m_LastStream` without the mutex. | Lock lookup, copy the stream, dispatch outside the lock. |
| STR-110 | Ping/pong ports | Temporary ping/pong streams used port 0. | Preserve FROM/TO ports and expose ping port arguments. |
| STR-111 | Incoming SYN | Failed or FROM-less SYNs were delivered to the acceptor. | Require authenticated FROM/identity and an open stream before acceptance. |
| STR-112 | Terminated send | Queued sends could dereference a destroyed destination. | `SendBuffer` rejects terminated streams and cancels queued handlers. |
| DG-108 | Datagram3 authentication | An unauthenticated hash-only datagram could create and ack a reply session. | Create/refresh/ack only on a matching ratchet session or static key. |
| DG-109 | Wildcard receiver | The first wildcard registration permanently won. | Java-style replacement for a repeated wildcard key. |
| DG-110 | Datagram sender identity | Datagram1/2 cached the old identity after key/offline refresh. | Refresh `m_From` under a mutex and use a snapshot per send. |

Residual: I2CP `m_EncryptionKeys`/owner publication across `i2cp.singlethread=false`
threads remains a TSan item (`CONC-008`); SAM `LISTEN_PROTOCOL`, UDP
SEND_TAGS/EXPIRES and full b33 secret authorization remain open. No
privilege-separation boundary was widened.

## 2026-10-26 NTCP2/SSU2 hardening continuation (static)

Four read-only audits compared handshake, state, ACK, retransmission,
fragmentation, PeerTest, relay, NAT, MTU, IPv6 and PQ negotiation with the
SSU2/NTCP2 specifications and the pinned Java 2.14.0 tree
(`3c98503c5f14efe7bdd783528ba7f621f12d8081`). Everything below is implemented
by static analysis only; no compiler, CTest, sanitizer, fuzzer, Java peer,
packet capture or live relay/PeerTest run exists.

| ID | Area | Finding | Correction |
|---|---|---|---|
| NTCP2-PAD-1 | Message 2 padding | The SessionCreated cap used the "Enc len" column (1136/1616) instead of the spec maximum (1168/1648 for ML-KEM-768/1024), so a conforming peer could be rejected. | Table corrected; both parsers share `NTCP2IsValidHandshakePadding`; the 3296-byte buffer equality is asserted. |
| NTCP2-DRAIN-1 | AEAD oracle | A data-phase AEAD/tag failure answered immediately, violating the spec's probing-resistance requirement; a frame read had no deadline. | Fail state drains 18..1041 random bytes with a 10 s deadline before reason 4; a stalled frame sends reason 14 after 30 s. No write is possible while draining. |
| NTCP2-SKEW-1 | Clock skew | SessionCreated was closed before it flushed and reason 7 was never sent. | Skew is recorded, message 2 is sent normally, and reason 7 is sent after SessionConfirmed. |
| NTCP2-NETID-1 | netID | A foreign msg1 netID was neither banned nor distinguished from a signature failure. | msg1 mismatch bans the source; a signed foreign `netId` sets `HasForeignNetId()` and is answered with reason 17, unreachable and a transport ban. |
| NTCP2-FLOOD-1 | RI propagation | The RouterInfo request-flood flag was defined but ignored. | Honoured for a newly accepted RI block smaller than 3 KiB; otherwise store-only. |
| NTCP2-BLOCK-1 | Malformed blocks | Wrong-size DateTime/Termination blocks were logged but not fatal. | DateTime size != 4 terminates with reason 10; a bad Termination closes without replying. |
| SSU2-HP-1 | HolePunch blocks | HolePunch/Introduced accepted any authenticated block, allowing netDb/token/fragment injection through the public intro key. | Only DateTime, Address, RelayResponse, Padding and Termination are accepted in those states. |
| SSU2-PT-4 | External address poisoning | msg 5 (and uncorroborated msg 7) could publish an attacker-chosen host/port. | msg 5 never publishes; msg 7 applies a host observation only from the Charlie learned in msg 4, after endpoint/port/reserved/blocklist checks, and never publishes a port from one test. |
| SSU2-PT-5 | msg 4 → msg 6 target | The Charlie endpoint from a peer-supplied RouterInfo was used without port/address validation. | `IsValidPeerTestEndpoint` + server family support + operator blocklist are required before msg 6, in both the delayed and the msg-5-first path. |
| SSU2-PT-6 | PeerTest bounds | Charlie ignored `AddSession()` failure and re-sent msg 5; Bob evicted live tests to admit new ones; there was no per-source limit. | Charlie checks the insertion result (code 66); Bob rejects at capacity with code 3; a bounded 12/600 s per-prefix limiter and a global 64-session cap apply. |
| SSU2-ADM-1 | Inbound admission | Inbound half-open sessions were unbounded and created on the single SSU2 thread. | `SSU2_MAX_INBOUND_SESSIONS` (40) is enforced before construction; SessionRequest/TokenRequest at the cap receive a Retry with reason 19, other types are dropped without insertion. |
| SSU2-ACK-1 | ACK 0 | Any ACK in `SessionConfirmedSent` completed the handshake, even without acknowledging SessionConfirmed. | `Established()` requires `acnt == ackThrough` (packet 0 covered); otherwise ACK ranges are processed and retransmission continues. |
| SSU2-SKEW-1 | Clock skew | The handshake window was ±60 s; Java/spec use ±120 s. | Window is 120 s with an RTT/2 allowance. |
| SSU2-VER-1 | Out-of-session version | PeerTest/HolePunch required `ver == 2`; the hybrid spec requires accepting 2–4. | 2..4 accepted; we still send 2. |
| SSU2-RI-1 | RouterInfo fragment byte | A fragment-flagged RouterInfo was parsed as a whole RI. | The single-fragment byte `0x01` is required in SessionConfirmed and the data phase. |
| SSU2-FALLBACK-1 | PQ downgrade | A version-rejection Retry carries token 0, so the downgrade path aborted instead of retrying. | After lowering the version, a zero token requests a fresh token (state reset); the fallback is reachable. |
| SSU2-ADDR-1 | Address publication race | `m_Address` was copied by the transports thread while the service thread replaced it. | `m_AddressMutex` guards the accessors; all assignments use `SetAddress()`. |
| SSU2-FRAG-2 | Reassembly budget | The out-of-sequence budget charged payload length while each fragment owns a 1500-byte slab. | Charges `sizeof(Fragment)` with a 128 KiB per-message budget, 64 fragments per message and a 32-message per-session cap. |
| SSU2-INTRO-1 | Introducer publication | The old predicate published a tagless introducer and family selection used the transport mask. | A zero tag or an expired session is rejected; selection uses the session endpoint family; a null identity is skipped. |
| SSU2-RELAY-1 | Relay invariants | Relay role was inferred from `IsEstablished()`; a zero tag could be minted and later blocks replaced the first; maps never expired. | Explicit Bob/Alice role, non-zero first-wins tag, 10 s/30 s expiry. |
| SSU2-MTU-1 | IPv6 MTU | A discovered IPv6 MTU could be published as 1500. | Published/effective IPv6 MTU is capped at 1488 (1280 floor kept). |
| SSU2-TEST-1 | Testing flag | `Testing` was set before the msg 4 signature verified and not cleared on early exits. | Set after verification with a scoped clear on every exit. |

Previously recorded corrections re-checked against the 2.14.0 tree: SSU2 late
packet 0 monotonicity, wrong-type drop, owner-checked removal, failed
SessionCreated reset, resend throttling, introducer snapshot, fragment budgets
and the PeerTest source/family/timestamp/private-target rules remain correct;
NTCP2 `m3p2len` 459..4048, termination reason 17 and the valid-frame count
remain correct. The 2.13→2.14 window contains no SSU2 delta and only the two
NTCP2 buffer/read fixes already mirrored here.

Prepared regressions: `test-ntcp2-failstate`, `test-ntcp2-netid-signal`,
updated `test-ntcp2-handshake-bounds`, `test-ssu2-holepunch-whitelist`,
`test-ssu2-peertest-policy`, `test-ssu2-admission`, and extensions to
`test-ssu2-control-plane` (version 2–4, fragment byte) and
`test-ssu2-session-fragment-bounds` (slab budget). The session/PeerTest state
sequences still need the controlled differential harness; the current tests
cover pure predicates and helpers, not sockets.

Residual: NTCP2 has no established-session cap, the establisher thread still
initiates socket I/O owned by the service thread, the options block is not
negotiated, and the 2-byte length read has no deadline. SSU2 `ProcessData`
still establishes Alice on any authenticable data packet (deliberate), the
incomplete-message timeout is observed at the 72 s cleanup pass, the socket is
shared between receive and send threads, and the memory-pool lifetime across
deferred teardown remains an open SEC-025 item. No dynamic claim is made.

## 2026-10-26 cryptography modernization continuation (static)

Four read-only audits covered the ECIES-X25519-AEAD-Ratchet protocol, Elligator2
and X25519 ephemeral keys, ML-KEM/provider negotiation, and
signatures/identities/LeaseSet2 against the ECIES specs, the hybrid specs, and
the pinned Java 2.14.0 tree (`3c98503c5f14efe7bdd783528ba7f621f12d8081`; the
ratchet classes live in `router/java/src/net/i2p/router/crypto/ratchet/` in
this release). Everything below is implemented by static analysis; no
compiler, CTest, sanitizer, fuzzer, Java peer, provider matrix or live session
has run.

| ID | Area | Finding | Correction |
|---|---|---|---|
| CRY-227 | Ratchet NSR split | Bob overwrote `m_SendTagset` with the newest New Session Reply split. If that NSR was lost, Alice adopted an older split and every Bob→Alice message was dropped until the 90 s timeout. | Pending `(receiveTagset, sendTagset)` splits (bounded 8, 180 s) are kept; the outbound tagset is promoted when the matching inbound tagset first consumes a tag (Java `firstTagConsumed`); delayed NSRs never rewind an established tagset. |
| CRY-228 | Tagset exhaustion | Exhausting the 16-bit tag index removed the session, losing all queued traffic. | The session is marked exhausted for sends but remains readable; the next send creates a fresh New Session. |
| CRY-229 | NSR payload / tags / cleansing | New Session Reply accepted Termination/NEXT_KEY/SessionID/ACK and could tear down a half-open session; `inboundTags` had no upper clamp; one-time-NS emitted a flags section Java's N decoder cannot parse; stack shared secrets were not wiped. | NSR validation accepts only DateTime/Garlic/Options/Padding; `inboundTags` is clamped to 320; the anonymous one-time path is removed (router-N remains `32+payload+16`); derived secrets are cleansed. |
| CRY-230 | X25519 key generation | A provider/RNG failure produced an all-zero static key that was accepted, persisted and published (unrecoverable identity). | `GenerateKeys`, `CreateECIESX25519AEADRatchetRandomKeys` and `LocalEncryptionKey::GenerateKeys` return bool; `PrivateKeys::GenerateCryptoKeyPair` rejects zero material; static-key and pool consumers fail closed or retry. |
| CRY-231 | X25519 ownership | `X25519Keys` was copyable with owning raw pointers and trusted a caller-supplied public key. | Copy/assignment deleted, safe move added; the public key is derived and compared, failing closed on mismatch. |
| CRY-232 | Ed25519 | Blinded scalars and nonces were freed with `BN_free` (the CRY-222 claim was only partially true); the hand-rolled verifier accepted non-canonical `S`; `DecodePoint` returned off-curve points. | `BN_clear_free` for secret scalars/nonces; `S < L` and `CRYPTO_memcmp` in the unused verifier; off-curve decode returns an empty point and blinding fails closed. |
| B33-202 | b33 flags | Local accepted the two-byte signature-type flag that Java 2.14.0 rejects. | Rejected; the hardening test now pins rejection. |
| CRY-233 | RouterInfo RSA | RouterInfo accepted RSA-signed identities that Java rejects for netDb entries (DoS policy). | RSA identities are rejected before verification; the RSA primitive remains for SU3/local use. |
| CRY-234 | LeaseSet2 keys | `i2cp.leaseSetEncType` accepted trailing garbage and multiple hybrid/ElGamal+hybrid combinations; unknown LS2 key sections were dropped instead of retained inert. | Strict decimal-token parser with Java's combination rules; unknown sections are kept for accounting and never selectable; known-type length mismatches still reject the LS2. |
| CRY-235 | ML-KEM / provider | The LibreSSL probe only created a key (no KEM round trip); the hybrid Noise initialisers could not report an unsupported type; SSU2 adopted any lower version from a Retry authenticated only by the public intro key. | LibreSSL now performs generate/marshal/parse/encaps/decaps/compare; `InitNoise*MLKEM` return bool and callers fail closed; SSU2 accepts a version change only when the signed RouterInfo advertises that exact maximum, otherwise the attempt fails (no silent PQ downgrade). |
| NET-173 | NTCP2 PQ policy | Outbound NTCP2 randomly chose classic X25519 for 1 in 4 PQ-capable peers, unlike Java. | Randomisation removed; the negotiated hybrid version is used. |

Previously recorded corrections re-checked: NS/NSR type-4 layouts, hybrid
type-5/6/7 frame order (`e1`/`ekem1`, nonce reset), tag replay one-time-use,
DateTime ±5 min/2 min, HKDF info strings, LS2 selectable encryption types,
offline-signature validation and the `EdDSAEngine` constant-time 2.14.0 delta
remain correct. The documented CRY-222 “private scalars use BN_clear_free” is
now true for the blinded scalar and nonces as well. The CRY-223 PQ fallback is
now reachable and authorised.

Residual and tracked: no Java-generated type-5/7 NSR vectors (CRY-206..214
remain open); `Signer::Sign` is still `void`, so a provider failure can publish
an all-zero signature and a LocalLeaseSet2 is not self-verified (CRY-236);
secret containers still cleanse only `size()` rather than `capacity()`
(CRY-237); the ratchet/ElGamal adaptive ordering and the 200 ms per-destination
New Session gate were not changed (CRY-238); the provider-free Ed25519 signer
is variable-time on secret scalars and its nonce derivation is
implementation-defined (documented, not rewritten); LibreSSL ML-KEM APIs and a
real PQ provider matrix are unverified.

Prepared regressions: extended `test-eciesx25519-ratchet-session` (both
NSR-loss orders, delayed NSR, NSR block matrix, router-N length, tagset
exhaustion), `test-crypto-x25519-noise-elligator-mlkem-audit` (keygen failure,
ownership, fallible Noise init, provider gating), `test-eddsa` (`S+L`
malleability, off-curve), `test-blinding-b33-hardening`,
`test-leaseset2-enc-types` (strict config, unknown-section retention),
`test-ssu2-preauth` (unauthorised Retry version, ACK-0), `test-ntcp2-handshake-bounds`
(no random downgrade) and the new `test-routerinfo-rsa-reject`. No dynamic
evidence exists for any of them.

## 2026-10-26 netDb/LeaseSet continuation (static)

Four read-only audits covered the LeaseSet family (MetaLeaseSet type 7 first),
DatabaseLookup reply keys and DatabaseSearchReply validation, RouterInfo
freshness/persistence, and netDb concurrency/floodfill behaviour against the
pinned Java 2.14.0 tree. Everything below is implemented by static analysis
only; no compiler, CTest, sanitizer, fuzzer, Java peer or live floodfill ran.

| ID | Area | Finding | Correction |
|---|---|---|---|
| NETDB-CLOVE-1 | Garlic origin | Every garlic clove was marked `fromClove`, and the clove allowlist omitted DatabaseLookup, so all garlic-wrapped DLMs (Java `IterativeSearchJob` and local tunnel lookups) were dropped; a direct-garlic token-bearing DSM was also dropped before store/ack. | A separate `fromTunnel` marker records the enclosing garlic's origin; DLM is allowed from direct garlic and still dropped from tunnel garlic; DSM keeps its token for Direct/DirectGarlic, and TunnelGarlic stores without token/ack/relay. |
| NETDB-FLOOD-1 | Transport RI flood | NTCP2/SSU2 request-flood blocks were re-flooded by non-floodfills and without the per-key FloodThrottler. | `FloodRouterInfoConditional` requires floodfill mode, a non-banned key and the flood budget; the DSM path keeps its own accounting. |
| NETDB-CAP-1 | Admission bounds | The netDb inbound queue was unbounded and there was no store cap. | `Queue::PutIfBelow` with 1024 messages and a deterministic 1000/4000 new-RI cap that never drops updates and bypasses during reseed. Java's probabilistic pdrop is not reproduced. |
| DB-126 | Persistence | RouterInfo saves used a predictable `.tmp` + ofstream + rename (no fsync), leftovers were re-parsed, and the file name was not bound to the identity hash; Stop never flushed accepted RIs. | Saves use `fs::AtomicWrite` (no-follow, unique temp, fsync file+dir); load accepts only `routerInfo-<base64>.dat` whose stem matches the parsed identity and deletes staging/mismatched files; Stop performs one bounded final persist. |
| CONC-013 | Selection locks | Selection filters called `GetProfile()`, which creates a profile and loads it from disk while `m_FloodfillsMutex`/`m_RouterInfosMutex` were held. | Non-creating `GetRouterProfileIfExists`/`GetProfileIfExists` is used by every selection filter and by store validation; profile creation/persistence stays off those locks. |
| DB-127 | Replacement generation | `AddRouterInfo` mutated the stored RouterInfo in place under the map lock, re-verifying the signature there. | The already-validated candidate replaces the map entry; readers hold one generation. |
| I2P-001 | MetaLeaseSet | Java re-serialises a MetaLS2 with `_expires` = latest entry and revocation count 0, so a non-canonical header or any revocation fails Java signature validation. Local accepted both and could flood/serve unverifiable entries. | A MetaLS2 whose header `expires` differs from the latest entry, or that carries revocations, is rejected. The test fixture is rebuilt Java-canonically with negative vectors. |
| LS-127 | Encrypted LS client replay | A type-5 reply was installed without comparing its signed outer published time with the cached entry. | Strictly-newer replacement; an equal/older authentic reply keeps the cached snapshot. |
| I2P-002 | Reply key 0x12 | Java 2.14.0 parses 0x12 into an unused key and answers cleartext (reply wrapping only supports 0x02/0x10); the derived KDF is TBD. | Local remains fail-closed: 0x12 is parsed and rejected with no cleartext reply. No non-normative KDF is invented. Test matrix added. |
| NET-176 | RouterInfo freshness | Only the zero-address 54-minute rule, 2-minute future and 27-hour age were enforced. | Adds the 54-minute `U`-cap and SSU-introducer predicates and the 75-minute SSU-only/no-NTCP rule, gated on one hour of uptime. |

Previously recorded corrections re-checked: DLM/DSRM byte bounds, DSRM
sender/from correlation and one-reply-per-sender, per-(from,tunnel) 14/3 and
per-key 3/90 s throttles, 0x02/0x10 encrypted reply formats, one-time reply-key
erasure, LS1/LS2/encrypted-LS2 store binding and Meta floodfill serving remain
correct. The earlier "Meta expiry clamp" and "no clove DSM token" rows
(LS-124/DB-115) are superseded by the canonical-rejection and three-state origin
rules above.

Residual and tracked: probabilistic store admission/go-away RI, client-side
MetaLeaseSet resolution (type 7 is served but not followed), exploratory-pool
router garlic forwarding, encrypted replies for our own RI/EXPL lookups,
throttle-map eviction by hash order, `m_AcceptUpdates` memory ordering,
asynchronous reseed, local RouterInfo address-slot copy-on-write (SEC-025) and
the netDb memory-pool destruction order. No dynamic evidence exists for any
row.

## 2026-10-26 tunnel-build / I2NP / garlic continuation (static)

Four read-only audits covered tunnel construction (long/short build records,
layered encryption, time windows, replay), tunnel pools/peer selection/timers,
I2NP framing/dispatch/tunnel-message processing, and garlic/ratchet clove
handling with its multiprocess integration, against the pinned Java 2.14.0 tree
and the tunnel-creation, tunnel-alt-creation and i2np specifications. Everything
below is implemented by static analysis only; no compiler, CTest, sanitizer,
fuzzer, Java peer, live tunnel or workflow ran.

| ID | Area | Finding | Correction |
|---|---|---|---|
| TU-107 | Build record table | `Tunnel::Build()` indexed the shuffled `recordIndicies[]` (4 or 8 entries) once per hop while `numHops` was unbounded (config/`Reconfigure`/variance), so an inbound or outbound path above the table wrote past the record map. | `PlanTunnelBuildRecords()` validates hops/phony/record count before any write and fails the build; `ClampTunnelLength()` clamps config, reconfigure and selection (inbound reserves the phony slot); the record writer validates its index. |
| TU-108 | Record encryption | A provider failure or AEAD error left a partially written record that was relayed as if valid; the phony record dereferenced a missing identity/keypair. | `EncryptECIES()`/`CreateBuildRequestRecord()` return `bool`, cleanse the shared secret, zero the whole record on any failure and abort the build; the creator requires the reply to carry exactly the advertised record count and length. |
| TU-109 | Transit build replay | No build-request replay filter; build messages used the generic 8 s expiration; size checks accepted trailing bytes; a failed allocation no longer produced a message but was still relayed. | A bounded 8-minute/4096-entry decaying filter keyed on the derived reply key (never logged) rejects replays before tunnel creation; relayed requests use 25 s and replies 60 s (Java `BuildHandler`); exact payload sizes required; a null message aborts the relay/reply. |
| TU-110 | Reuse admission | `Recreate*Tunnel()` reused peers chosen earlier without re-applying the blocklist, runtime bans, revocation, reachability, version floor, congestion, ECIES or endpoint family rules. | One `IsRouterUsableForTunnel()` predicate shared by fresh selection, explicit peers and reuse; a failed/test-struck path is never reused; `ValidatePeers()` is direction-aware. |
| TU-111 | Pool builds/tests | Pending builds were not counted, so a slow round was oversubscribed; no failure backoff; a test was recreated every tick and failed a tunnel on the first lost reply; inbound traffic resurrected a test-failed tunnel; `TestFailed` tunnels could still be published. | In-flight builds count against the target; Java-like 4/8-failure ladder and 10–240 s delay; tests persist with a 7.5–15 s deadline and fail on the third strike only; strikes are independent of state; 40 s inbound-activity skip; `TestFailed` is filtered from LeaseSets. |
| TU-112 | Selector lifetime/masks | The raw `ITunnelPeerSelector*` was used after the owner could be released (use-after-free), the three fallback builders passed `false` as a `CompatibleTransports` mask (zero), the inbound queue was unbounded, fragment reassembly used the 8 s I2NP lifetime and stored duplicate old fragments, and `GetLowestLatency*Tunnel(exclude)` could return the excluded tunnel. | `TunnelPeerSelectorHandle` pins a weak owner for the whole call outside the pool mutex; real per-direction masks; `PutIfBelow` queue (8192) with a drop counter; exact 1028-byte tunnel data; 45 s reassembly window; stale fragments dropped; exclusion and first-hop fallback fixed. |
| I2NP-011 | Dispatch window | `HandleI2NPMessage()` never checked expiration, so a replay of an old netDb/garlic/status message reached its handler; there was no router-wide duplicate suppression (Java `MessageValidator` + `DecayingHashSet`). | 90 s past / 240 s future window applied before the type switch (TunnelData keeps its endpoint/IV policy) plus a bounded two-generation filter keyed by msgID, expiration second and a router salt; cloves use the destination filter; throttled drop counters. |
| I2NP-012 | Exact lengths | `CreateI2NPMessage()` returned an uninitialised/truncated message on overflow; DeliveryStatus and DataMessage accepted trailing bytes. | Null on overflow with every network caller checking it; DeliveryStatus exactly 12 bytes; DataMessage declared length must cover the whole payload; throttled parse diagnostics. |
| GAR-005 | Clove grammar/session state | Garlic session state (tagsets, ACK list, paths, lease-set status) had no serialization; the provider HKDF context was shared across threads; the destination garlic queue was unbounded; zero/>32-clove payloads, a `fromTunnel`-only allowlist applied to direct garlic, a destination-hash check that ignored the clove type, and a tag-budget full path that discarded the payload. | Per-session recursive `m_StateMutex` plus a per-thread `HKDFContext`; `CleanUp()` swaps maps and destroys outside locks; 256-entry bounded clove queue; 1..32 cloves; stale/foreign cloves skipped and the walk continues; per-destination clove replay filter; origin-aware allowlist; budget-full still decrypts; `DESTINATION` hash check; ROUTER-mode ratchet cloves rejected explicitly. |
| CONC-014 | Lock inversion | With the new session lock, `CleanupExpiredTags()`/`SetLeaseSetUpdated()` held `m_SessionsMutex` while calling session methods that take the state lock, while a self-registering session takes the state lock first — an ABBA deadlock. | Both loops snapshot the map, call the sessions outside the map lock and re-check identity before erasing/terminating; `IsTerminated()`/`IsExhausted()` remain atomic so the tag-budget path stays lock-free. |

Preserved deliberately: existing short/variable record byte layout, HKDF
derivation chain, ChaCha20 slot nonces and AEAD associated data (no wire
change); the tunnel duration/creation constants were not retuned in this
increment; a replay of a build request is rejected rather than answered twice;
an unsigned `ver` fallback still requires the signed RouterInfo maximum; garlic
keys, decryption and tag state remain core-only and were not moved behind the
IPC boundary.

Residual and tracked: probabilistic netDb store admission/go-away, client
MetaLeaseSet resolution, exploratory router garlic forwarding, encrypted
replies for our own RI/EXPL lookups, NTCP2 established-session cap/12-byte
options/establisher-thread socket ownership (NET-172), SSU2 socket sharing and
memory-pool lifetime after deferred teardown (SEC-025), local RouterInfo
address-slot copy-on-write, `TorrentsTunnel::InitTorrentFiles` disk-domain
mutation, and Java's additional B1/B2/B3 payload-size normalisation (deferred
rather than approximated). No dynamic evidence exists for any row above.

## 2026-10-26 SAM / I2CP / streaming / datagram continuation (static)

Four read-only audits compared the native SAM bridge, I2CP client, streaming
state machine and Datagram1/2/3 implementation with the pinned Java 2.14.0 tree
and the SAM v3, I2CP, streaming and datagram specifications. Everything below is
implemented by static analysis only; no compiler, CTest, sanitizer, fuzzer,
Java peer, SAM client or workflow ran.

| ID | Area | Finding | Correction |
|---|---|---|---|
| SAM-122 | Listen vs forward port | A single-session `SESSION CREATE … PORT=7666` used 7666 as the I2CP receive port, so a peer sending to the PORT_ANY the session actually advertised was dropped and `RAW RECEIVED` never fired. | `PORT` only builds the local UDP forward endpoint; the receive port is `LISTEN_PORT` (default 0/PORT_ANY) as in Java `SAMv3RawSession`/`SAMv3DatagramSession`. |
| SAM-123 | Control plane | No connection cap, no HELLO deadline, and the SESSION CREATE/HELLO lines were logged verbatim (`DESTINATION=<private keys>`, `USER`/`PASSWORD`). | 64-connection admission with exactly-once release, 60 s HELLO deadline, no idle disconnection after HELLO (Java sets `SO_TIMEOUT=0`; only a partial command line is bounded), and no secret operand is ever logged. |
| SAM-124 | Command semantics | ACCEPT during FORWARD hung forever, FORWARD armed the default destination, missing ID returned INVALID_ID, connect timeout returned CANT_REACH_PEER, malformed HELLO produced no reply, AUTH closed silently, duplicate parameters and bare tokens were mishandled. | ACCEPT/FORWARD are mutually exclusive per port (`ALREADY_ACCEPTING`), FORWARD is port-scoped, Java's `I2P_ERROR`/`TIMEOUT` codes are emitted, malformed HELLO replies I2P_ERROR while an unparsable MIN/MAX bound is NOVERSION, duplicate keys close, bare tokens mean true. |
| SAM-125 | Subsession identity | Subsession key forced protocol 0 for non-RAW, so DATAGRAM/DATAGRAM2/DATAGRAM3 could not share a listen port, and receivers were protocol-blind. | Key `(STYLE, listenProtocol, LISTEN_PORT)` with 17/19/20, protocol-keyed identity/hash registration, RAW rejects 6/17/19/20, and sends carry the subsession version so framing is never inherited. |
| SAM-126 | Accept re-arm | Terminating the armed acceptor (or a FORWARD socket) reset the chain; queued ACCEPTs waited forever. | A queued ACCEPT for the same port is armed again on the SAM service; port isolation is preserved. |
| I2CP-112 | b33 resolution | `.b33.i2p` and implicit long-label names fell into the address book and never resolved. | Both spellings build a `BlindedPublicKey` (bounded label, canonical base32, blindable type); invalid/reserved/over-long labels and `www.` aliasing fail closed. |
| I2CP-113 | BlindingInfo (42) | Parsed and stored, but no reader existed: DH/PSK client-auth material never reached the encrypted-LeaseSet request. | Bounded reader keyed by the unblinded signing key, blinded-key lookup for a b33 name, and the 32-byte secret is passed to `RequestDestinationWithEncryptedLeaseSet` (preferred over the session key). |
| I2CP-114 | HostReply codes | Every failure was result 1. | Java's 2/3/4 (secret/key/both required), 5 (decryption failure) and 6 (leaseset lookup failure) are emitted; success is byte-identical. |
| I2CP-115 | I2CP admission/deadlines | Unbounded connections and sessions, untimed reads and a synchronous `available()`/`read()` path on the service thread. | `i2cp.maxconnections`/`i2cp.maxsessions` (bounded defaults/caps), exactly-once release, cancellable async handshake/header/body deadlines. |
| I2CP-116 | LeaseSet/send validation | CLS1 assumed a DSA key offset and installed an unvalidated LeaseSet; CLS2 read a key count for a MetaLeaseSet2, chose the highest key type and never compared keys; expired sends were still sent. | CLS1 derives the key length, rejects foreign/invalid LeaseSets and stays DSA-only (fail closed); CLS2 is Meta-correct, preference-ordered and key-matched; status 14/10/11 replace silent drops. |
| STR-113 | Accept lifetime | `AcceptStream(timeout)` left a lambda holding stack references installed after timeout (use-after-free on the next SYN); one acceptor teardown collapsed the whole chain. | Heap sync state, timeout unregisters, explicit acceptor registrations with `RemoveAcceptor(token)`; `ResetAcceptor()` completes every waiter. |
| STR-114 | SYN rejection/replay | A retransmitted SYN re-opened a closed stream and re-invited the application; over-limit/throttled/backlog-full SYNs were dropped in silence. | Bounded recently-closed set (128/120 s) plus one signed RESET per rejected authenticated SYN. |
| STR-115 | Post-close/races | Data could still be emitted after CLOSE/RESET, the pong handler raced, a duplicate SYN reset the routing path, an acceptor reset could orphan a stream. | Data stops after CLOSE/RESET, handler snapshotted under the acceptor mutex, duplicate SYN keeps the path, decision+dequeue+enqueue under one lock. |
| DG-111 | Framing version | The version was frozen per destination, so a DATAGRAM3 subsession emitted Datagram1 framing on protocol 17 and a Java peer dropped it. | Per-send `version` (`eDatagramDefault` keeps the session default) on `SendDatagram`/`SendDatagramTo`. |
| DG-112 | Receiver isolation | Identity/hash receivers were keyed by port only and a port-specific registration was promoted to the wildcard, so a Datagram2 payload could reach a Datagram1 subsession and an unregistered port could be delivered to a random subsession. | `(protocol, port)` keying with 0 as catch-all, exact dispatch 17/19/20, no promotion; raw rejects the typed protocols and 0xff normalises to 0. |
| DG-113 | Parser/session state | One shared options object and inflator, plus unsynchronised session queue/routing state; an all-zero signature could be published. | Per-call options, mutexed (in)deflator, thread-safe pool, one session mutex, zero-signature/offline-section rejection. |

Preserved deliberately: SAM/I2CP wire layouts, datagram framing bytes and the
`MAX_DATAGRAM_SIZE` (32 KiB) inbound bound; streaming window/MTU/RTO constants;
the ratchet/garlic layer that authenticates datagrams and streams. A rejected
streaming SYN is answered with a RESET, never a plaintext forward.

Residual and tracked: streaming TIME-WAIT/FIN retention (FIN is still sent once),
SYNACK-before-LeaseSet ordering, the Java `limitAction` response to an
over-limit stream and uploader/blame parity; CLS1 remains DSA-only; the
BlindingInfo match uses the current day's blinded key; 33–61 KiB Java datagrams
exceed the local 32 KiB bound; `TorrentsTunnel` still mutates a shared session
version; and no dynamic evidence exists for any row above.

## 2026-10-26 application-services continuation (static)

Four read-only audits compared the HTTP/SOCKS proxies, client/server tunnels,
reseeding, `.i2p`/b32/b33 naming with the address book, and the auxiliary
services plus their administration surface with the pinned Java 2.14.0 tree.
Everything below is implemented by static analysis only; no compiler, CTest,
sanitizer, fuzzer, live proxy/tunnel/reseed/IGD/NTP/SAM client, Android device
or workflow ran.

| ID | Area | Finding | Correction |
|---|---|---|---|
| PARITY-019 | HTTP proxy content | Generated error/helper pages interpolated the client `Host` unescaped (reflected HTML/JS), and the 502 page echoed the configured outproxy URL (including `user:pass@`). | Every page value goes through a checked `EscapeHTML`; only a fixed non-secret reason is emitted, and the configured URL/credentials never reach a page or log. |
| PARITY-019 | HTTP proxy protocol | Non-http(s) schemes (`ftp:`, `mailto:`) were rewritten into a plaintext HTTP GET; proxied responses passed hop-by-hop and proxy-only headers through; relative-form requests lost the port default. | `http`/CONNECT-`https` only (403 otherwise, before resolution); a new client-response mode strips `Proxy-*`, `Connection`, `Keep-Alive`, `TE`, `Trailer` and disclosure headers; relative-form defaults to 80/443; CONNECT IPv6 literals parse. |
| PARITY-019 | Proxy auth/limits | Basic only, no failure delay, no upstream resolve/connect/response deadlines. | Opt-in RFC 7616 Digest (MD5/SHA-256, bounded nonce cache, `nc` replay, stale) with constant-time compare and a configurable failure delay; cancellable resolve/connect/response-header deadlines (504 on expiry); Basic remains the default. |
| DG-114 | SOCKS | RESOLVE used an unbounded, unguarded cache with unused timestamps and `std::hash` fake addresses; UDP ASSOCIATE replied with a wildcard and CONNECT with an own-b32/truncated-id trick. | Bounded (256) 10-minute cache under a mutex, keyed SipHash into 255/8; replies are the Java loopback/`127.0.0.1:1` forms. |
| PARITY-019 | Tunnel teardown | `I2PTunnel.h` declared a `Terminate()` that hid the non-virtual base; `ClearHandlers` poisoned `m_Dead`, so stopped server tunnels never closed live streams/sockets (leak until a 1 h idle timeout) and could not restart. | Teardown is virtual, handlers are snapshotted under the mutex and terminated outside it; `Stop(bool forced)` distinguishes forced/in-use; `Start()` restarts. |
| PARITY-017 | Server-tunnel filters | `filterDefinition` was absent and the `accesslist` check ran after accept, so a denied peer still cost a full inbound tunnel and SYNACK. | Bounded parser (`default`, `explicit`, `file`, `record`, first-match-wins, mtime-cached 1 MiB file, 4096 counters) and a `StreamingDestination` admission predicate that rejects before the stream exists, with `limitAction=drop|reset`. |
| PARITY-019 | Server-tunnel options | No `targetForPort.<port>` (HTTPS on 443 was forwarded as plaintext HTTP), `spoofedHost`, `rejectInproxy`/`rejectReferer`/`rejectUserAgents`, POST throttling, `enable`/`startOnLoad` or per-tunnel control. | All are parsed and enforced (443/22 reset without a mapping, 403/429 bodies, bounded POST window); stable names plus `GetTunnelServices`/`StartTunnel`/`StopTunnel`, and only port 0 may own the default acceptor. |
| RS-107 | Reseed lifecycle | A stale-but-signature-valid bundle could call `ResetRemoteRouterInfos()` and erase every cached peer; the synchronous reseed blocked the admin thread/Android worker for minutes and TLS read/write had no deadline; proxy credentials were logged. | The reset path is gone (stale entries are skipped and counted); reseed is an owned, stop-token-cancellable job with per-phase/per-host deadlines, `RequestReseed()`/`ReseedState`, `reseed.disable`/`i2p.noReseed`, redacted proxy logs and strict `netid` query handling. |
| AB-106 | Address book | Index/identity/etag/`deleted.txt` writes were non-atomic and could follow a symlink; add always replaced; feed `#!action=delete` was ignored; destination/host bounds were looser than Java; 304 escalated retries; every miss re-looked-up. | All artifacts use the atomic no-follow writer; `AddAddressChecked` distinguishes `Exists`; feed deletion is restricted to the recorded source origin via a bounded sidecar; Java-compatible length/label rules; 304 keeps the ETag/cadence; bounded negative cache. This does not authenticate feed commands. |
| AB-107 | Address-book feed signatures | Proposal 112 and Java 2.14 require canonical `sig` verification for signed actions and `oldsig` where an existing destination authorizes a change. Native feed handling applies only `action=delete` after source-origin matching and verifies neither signature; other signed actions are absent. | Pending. Add canonical signature preimages, action-by-action authority checks, replay and migration vectors from Proposal 112; preserve the feed-origin restriction. Do not describe source ownership as authenticity. |
| SEC-029 | Auxiliary services | BOB returned private keys to any loopback client, had no session/line cap and threw on bad ports; the I2PControl AF_UNIX path was unlinked blindly, mode depended on umask and had no peer check; JSON ids/strings could inject or break the reply; bandwidth accepted negatives; NTP trusted any source and a single packet; UPnP used permanent mappings. | BOB is loopback/same-UID only, capped (16 sessions, 4 KiB line), `from_chars` ports and `getkeys` behind `bob.allowgetkeys`; I2PControl validates the stale socket, `chmod 0600`, removes only its own socket, requires a same-UID peer, escapes ids/strings, validates `API` and returns Java error objects; NTP validates every field and needs three agreeing servers for a large jump; UPnP uses a 3 h renewable lease, replaces mappings and refuses a same-host IGD. |
| PARITY-021 | Administration | TUI/Android could only read tunnel counters and request a blocking reseed; address add always replaced; Android lacked subscription and ban bridges. | Typed verbs `ADMIN TUNNEL-START/TUNNEL-STOP`, `ADMIN ADDRESS-REPLACE` (plain add answers `exists`), non-blocking `ADMIN RESEED` with `netdb.reseed.*`, `tunnel.service` rows, TUI screens/actions and Android JNI/screens for tunnels, reseed, add-vs-replace, subscriptions and bans. No config writer and no web console. |

Preserved deliberately: proxy/tunnel wire behaviour, the 1 MiB HTTP header cap
and 30 s handshake deadlines, subscription validation (I2P-only, atomic
`subscriptions.txt`), tombstones/merge precedence, SU3/ZIP reseed bounds,
HTTPS chain+hostname verification, the 100-RI/2-server threshold, and the
closed typed admin queue.

Residual and tracked: HTTP client keep-alive/pipelining, IRC `ircclient`,
UDP/torrent tunnels in the start/stop registry, Streaming `limitAction`
`http`/custom responses, daemon bootstrap still waiting synchronously for the
first reseed, day-scoped BlindingInfo matching, 33–61 KiB Java datagrams
versus the local 32 KiB bound, feed retraction depending on the
`entry-sources.txt` sidecar and Proposal 112 signatures still unverified,
UPnP status not yet in TUI/Android, Windows
local-socket peer credentials, and the es/ru/zh-Hans catalog entries for the
new TUI strings (`I18N-004`). No dynamic evidence exists for any row above.

## Open items requiring dynamic evidence

1. Exercise the audited source differences against the pinned 2.14.0 target,
   including NTCP2 session establishment with and without ML-KEM. Historical
   2.13.0 scenarios remain version-specific regression cases.
2. SSU2 relay, peer test and MTU discovery on IPv4/IPv6, especially OpenBSD.
3. Large RouterInfo (3073–4096) publication and lookup.
4. EncryptedLeaseSet/b33 lookup with Java clients.
5. Ratchet/streaming/datagram/SAM/I2CP end-to-end flows.
6. Malformed-message differential behavior.

No claim of "fully compatible" is made until these are executed.

## Non-goals

- Reimplementing Java's console, I2PSnark, SusiMail, plugin system or router
  architecture.
- Copying Java implementation bugs.
- Changing i2pd security validation to accept invalid signatures, tags or
  malformed messages.
- Matching Java thread counts, memory use or internal class structure.

## Controlled differential harness

See [`tests/interop/README.md`](../tests/interop/README.md) and
[`tests/interop/check-reference.sh`](../tests/interop/check-reference.sh).
For new differential runs, the harness configuration must match the pinned
Java 2.14.0 target commit above. Earlier 2.13.0 fixtures and results retain their original provenance.
It requires an isolated loopback network and is a test procedure, not a Java
build/runtime dependency. No result in this document becomes validated until
the dynamic run is recorded.

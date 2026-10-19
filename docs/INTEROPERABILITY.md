# I2P Java interoperability audit

Reference implementation audited statically in this session:

| Item | Value |
|---|---|
| Repository | https://github.com/i2p/i2p.i2p |
| Tag | `i2p-2.13.0` |
| Commit | `9134f808337b401e8e53c73734c81fab04280c9d` |
| Release date | 2026-07-24 (tag commit 2026-07-20) |
| Version | 2.13.0 |
| Local copy | A read-only clone was not present in this environment; Java sources were rechecked against the pinned GitHub tag. The reference is not bundled. |
| Newer stable | None found on 2026-10-19; GitHub latest release and tags still resolve to 2.13.0 |
| Harness recheck | 2026-10-19: 26 checked-in cases reviewed statically (24 static-OK expectations, 2 documented known gaps); the pinned checkout and differential runner were not executed |

The Java tree is **not** vendored and is not a build/runtime dependency of
openi2pd. No Java code was modified.

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
| NTCP2 version negotiation | `spec/ntcp2` | `router/.../transport/ntcp/NTCPTransport.java`, `InboundEstablishState.java`, `OutboundNTCP2State.java` | `libi2pd/NTCP2.cpp` `SetVersion`, SessionRequest/Confirmed | `NTCP2_INT_VERSION=2`, `PQ_INT_VERSION=4`; normal config publishes v2, PQ config publishes 4/5; accepts 2–5 internally when the provider supports it | Can accept 3–5, but configured version 3 is clamped to 2; no-PQ builds publish no `pq` | None found; i2pd supports 5 beyond Java's 4 but negotiates down | Compatible when PQ availability matches; otherwise version 2 | P0 | Existing version tests; PQ publication gated | Needs dynamic PQ/non-PQ session test |
| NTCP2 handshake/frames | `spec/ntcp2` | `OutboundNTCP2State`, `InboundEstablishState`, `NTCP2Payload` | `libi2pd/NTCP2.cpp` | X25519/ML-KEM, obfuscation, length bounds, 65516 max message | Same structure; separate implementation; replayed SessionRequest rejection, strict SessionConfirmed block order, 6000-byte msg3p2 cap and data-phase ordering now enforced | Replay/cap/ordering gaps fixed; termination-count and DateTime handling aligned | Compatible (static, validation pending) | P0 | NET-118..NET-123 and NET-134..NET-140; `test-ntcp2-session-confirmed-payload`/`test-ntcp2-handshake-bounds` passed in the local 48-test CTest | Needs dynamic session/replay/PQ test |
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
| ECIES-X25519-AEAD-Ratchet | `spec/ecies` | `core/.../crypto/ecies/...`, ratchet session classes | `ECIESX25519AEADRatchetSession.*` | IK/ratchet, tags, AEAD, nonces | Same external format; NS requires a Garlic clove and valid DateTime; NSR/ES payloads are pre-validated before side effects | NSR/ES partial application and replay-cache lifetime fixed statically | Implemented (static); tests registered but not executed | P0 | `test-eciesx25519-ratchet-session`, `test-eciesx25519-ratchet-tagsets`; no Java vector | Needs CI/cross-vectors |
| Signatures | `spec/common-structures` | `SigType.java` types 0–8, 11 | `Identity.h`, `Signature.*` | DSA/ECDSA/RSA/Ed25519/Ed25519ph/RedDSA | Same constants plus GOST 9/10; type 8 now uses Java-compatible plain Ed25519 over the supplied bytes | GOST is not in Java stable list; optional | No interoperability impact for common types | P2 | Type-8 vectors still needed | Existing vectors/static review |
| ML-KEM encryption types | proposal 169 / `spec/ecies` | `EncType.java` codes 5–7 | `PostQuantum.*`, `Crypto.h` | MLKEM512/768/1024 hybrid | Same codes when the provider exposes ML-KEM; LibreSSL 4 excludes ML-KEM-512; no-PQ builds no longer select/publish hybrid addresses | Runtime provider probing and type-5/7 session vectors remain missing | PQ sessions only when both support | P1 | Capability checks retained; publication/selection gated | Needs PQ interop |
| Streaming | `spec/streaming` | `router/.../client/streaming/*.java` | `Streaming.*` | SYN/SYN-ACK, windows, resets, close | Equivalent | None found | Compatible (static) | P1 | None | Needs dynamic stream test |
| Datagrams v1/2/3 | `spec/datagrams` | `core/.../client/Datagram*.java` | `Datagram.*` | Repliable/raw, ports, auth | Same | None found | Compatible (static) | P1 | Existing code | Needs client interop |
| SAM | `spec/samv3` | `apps/sam/java/src/net/i2p/sam/*` | `libi2pd_client/SAM.cpp` | Session/handshake/naming/streams | Same commands | None found | Compatible (static) | P1 | `test-sam-version.sh` not run | Pending |
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
| RT-101 | Ratchet NS freshness/replay | `ECIESAEADEngine.java:591-625, 1465-1476` | `ECIESX25519AEADRatchetSession.cpp` | Requires DateTime in NS, checks -5/+2 min, at least one Garlic clove and a time-bounded ephemeral replay cache | DateTime-only NS could create a session; replay cache was a 256-entry FIFO with no time bound | Captured NS replay after cache eviction | P0 | Generic handshake validation; 5-minute/4096-entry decaying cache | Fixed (static); replay/time-skew vector pending |
| RT-102 | PQ NSR retransmission | `ECIESAEADEngine.java:784-841, 1257-1319` | `ECIESX25519AEADRatchetSession.cpp::NextNewSessionReplyMessage` | Clones full handshake state per NSR | Restored CK but omitted static-DH MixKey (fixed earlier); payload validation still missing | Second PQ NSR fails authentication | P1 | Re-run `x25519(besk, apk)` MixKey after ML-KEM; validate NSR before commit | Fixed (static); two-NSR/type-7 vector pending |
| RT-103 | Multi-key LeaseSet2 | `LeaseSet2.java:186-205`; `ECIESAEADEngine.java:1133-1148` | `LeaseSet.cpp`, `Datagram.cpp`, `Streaming.cpp`, `Destination.cpp` | Selects acceptable key from LS order; all sections retained and matched by remote key type | One encryptor only in older code | Valid ratchet peer rejected when using another advertised key | P1 | Retain all key sections; repeated preferred type keeps first | Fixed (static); 7/4 vs 6/4 session matrix pending |
| RT-104 | Ratchet payload strictness | `RatchetPayload.java:105-205` | `ECIESX25519AEADRatchetSession.cpp::ValidateRatchetPayload/HandlePayload` | Rejects blocks after padding/termination, bad DateTime/NEXTKEY/ACK lengths, and stages validation before side effects | Parser could deliver a prefix before failing late | Partial application/state divergence | P2 | Pre-validate NSR/ES payload; NS requires Garlic; reject invalid DateTime as a unit | Fixed (static); malformed corpus pending |
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
| NET-112 | DLM reply-key 0x12 | `DatabaseLookupMessage.java:390-424`; proposal 154 (KDF TBD) | `NetDb.cpp::ParseDatabaseLookupReplyKey` | 0x12 is a 32-byte ratchet public key with no tags; Java 2.13 never sends it | Strict 0x02/0x10 parsing; 0x12 rejected fail-closed, never cleartext | No non-normative construction or cleartext fallback | P1 | Parse/validate reply-key block; reject derived-key mode | Fixed (static); store test covers parser |
| NET-113 | DSRM sender binding | `IterativeLookupJob.java:38-45`; `SearchMessageSelector.java:79-86` | `NetDbRequests.cpp` | Requires queried sender/from hash | Requires complete trailing from and queried-sender association | Truncated/fabricated replies rejected | P2 | Full from/count/queried-peer checks | Fixed (static); malformed DSRM test pending |
| NET-114 | Tunnel build time | `BuildHandler.java:101-104, 776-812` | `TransitTunnel.cpp` | Enforces 8-minute ECIES age and 5-minute future; drops without reply; code 30 for unsupported short layer encryption | Now validates minute windows after decryption; ignores future-duration expiration as Java does | Stale/future requests dropped; zero-payload/flags/IDs guards added | P2 | Minute-window validation and hostile-record drops | Fixed (static); clock-skew and Bloom-filter replay tests pending |
| NET-115 | Floodfill RI freshness | `HandleDatabaseLookupMessageJob.java:57, 130-140` | `NetDb.cpp::HandleDatabaseLookupMsg` | Serves only current RIs (1 h) | Applies one-hour plus two-minute tolerance to RouterInfo only; self always served | Stale RouterInfo suppressed | P2 | RouterInfo-only freshness check | Fixed (static); aged-RI test pending |
| NET-116 | RouterInfo address option bounds | `RouterInfo.java:88, 589-594`; `RouterAddress.java:285-297`; `DataHelper.java:189-220`; `spec/common-structures` | `RouterInfo.cpp::ReadFromBuffer`/`ExtractParam`, `RouterInfo.h::MAX_RI_ADDRESSES` | `MAX_ADDRESSES=16`; exact string reads; duplicate/unsorted address and RI options rejected | Address loop passed whole remaining RI to option parser; 0-255 addresses; clamped strings; duplicate options overwrote earlier values | Malformed option lengths could consume the next address, peers, properties or signature bytes; conflicting `host`/`port`/`s`/`i`/`v`/`pq` values accepted; no OOB proven | P1 | Bound each option to `size - r`, reject `sz==0`/oversized, cap addresses at 16, reject duplicates, strict `ExtractString`/`ExtractParam`, bound property section and peer count | Fixed (static); malformed RouterInfo corpus pending |
| NET-117 | SSU2 Address block family | `SSU2Payload.java:308-315`; `PeerState2.java:703-706`; `InboundEstablishState2.java:534-539`; `OutboundEstablishState2.java:339-344`; `UDPTransport.java:1354-1382`; SSU2 spec Connection Migration | `SSU2Session.cpp::HandleAddress` | Validates block length 6/18 and does not use it to change local external address | Rejects invalid length and ignores cross-family blocks before local update | Authenticated peer could otherwise overwrite the other family's external address | P1 | Family validation before `RouterContext::UpdateAddress` | Fixed (static); v4/v6 matrix pending |
| NET-118 | NTCP2 SessionRequest replay | `spec/ntcp2` SessionRequest replay note; `NTCPTransport.java:227,855-857` | `NTCP2.cpp::ProcessSessionRequest`, `NTCP2Server::IsHandshakeReplay` | Java keeps a bounded replay filter for encrypted X prefixes | No replay cache; every duplicate was processed | Replayed SessionRequest could force repeated handshake work | P1 | Bounded 8192-entry/600 s mutex-protected replay cache, cleared on stop | Fixed (static); replay/expiry test pending |
| NET-119 | NTCP2 SessionConfirmed part 2 blocks | `spec/ntcp2` Block Ordering Rules; `NTCP2Payload.java:85-160`; `InboundEstablishState.java:558` | `NTCP2.cpp::ValidateNTCP2SessionConfirmedPayload` | Requires RI, optional Options, optional final Padding; no other block | Only the first RI block was checked | Malformed/out-of-order blocks could reach handshake state | P1 | Exact ordered validation with truncation/overrun rejection | Fixed (static); `test-ntcp2-session-confirmed-payload` passed in the local 48-test CTest |
| NET-120 | NTCP2 msg3p2 length cap | `InboundEstablishState.java:84-87,470-473` | `NTCP2.cpp::ProcessSessionRequestMessage` | Caps `m3p2len` at 6000 for the reference implementation | Only rejected values below 19; accepted up to 65535 | Per-handshake allocation/read amplification | P2 | Exact 6000-byte policy cap and `size_t` local calculation | Fixed (static); boundary test pending |
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
| NET-139 | NTCP2 handshake padding limits | `spec/ntcp2-hybrid` Maximum Padding; `InboundEstablishState.java:444-449`; `OutboundNTCP2State.java:342-346` | `NTCP2.cpp` request/created generation and validation | Per-version limits: msg1 880/1264/1648; msg2 848/1136/1616 | Shared `m_MaxMsgSize` allowed excessive padding; v4 generation could exceed 1136 | Excess allocation and version-dependent divergence | P2 | `NTCP2GetMaxHandshakePadding` and clamped generation | Fixed (static); `test-ntcp2-handshake-bounds` covers limits |
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
| DB-104 | DatabaseLookup reply-key 0x12 | I2NP Reply Encryption table; proposal 154 (derived KDF TBD) | `NetDb.cpp::ParseDatabaseLookupReplyKey` | 0x12 is 32-byte ratchet key, no tags; Java 2.13 never sends it | Earlier treated as ECIES New Session, a non-normative construction | Guessed reply format | P1 | Strict 0x02/0x10 parsing; 0x12 fails closed; never cleartext | Fixed (static); `test-netdb-store-lookup-hardening` passed |
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
| I2P-002 | DatabaseLookup reply-key 0x12 | Parsed strictly but rejected fail-closed; proposal 154 derived-key KDF is explicitly TBD and Java 2.13 has no sender. |
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
| I2P-002 | DLM reply key 0x12 | proposal 154 (derived KDF TBD) | `DatabaseLookupMessage.readMessage/writeMessageBody` | `NetDb::ParseDatabaseLookupReplyKey`, `HandleDatabaseLookupMsg` | 0x12 is a 32-byte ratchet public key with no tag; Java 2.13 has no sender and the KDF is TBD | A guessed or cleartext reply would be non-interoperable | P1 | Strict 0x02/0x10; 0x12 rejected fail-closed | Reviewed; Java 2.13 has no caller for the public-key form | Store regression covers parser; dynamic 0x12 impossible against stock Java | No interoperability claim |
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

## Third static pass — latest stable recheck 2026-10-19

The latest official stable release is still `i2p-2.13.0` at commit
`9134f808337b401e8e53c73734c81fab04280c9d`; the audited C++ tree was compared
against that tag and the normative specs. The following findings were not in
the earlier matrix. **No dynamic test was executed.**

| ID | Area | Java/spec reference | Finding | Correction / status |
|---|---|---|---|---|
| NET-155 | SSU2 receive window | `PeerState2.receivePacket`, `SSU2Bitfield`; SSU2 Packet Numbering | Bob baselined the data phase at packet 0 while Alice's first data packet is 1, so every packet was parked out of sequence and packet 512+ was silently dropped | Fixed (static): inbound first data packet advances the contiguous baseline; packet 0 remains accepted once |
| NET-156 | SSU2 cleanup ACKs | `SSU2Bitfield.fromACKBlock`; `PeerState2.finishMessages` | Cleanup advanced the receive baseline over unreceived packets and ACKed gaps | Fixed (static): no synthetic receipt; ACK encoder keeps NACK-only ranges and retransmission |
| NET-157 | SSU2 Address trust | `PeerState2.gotAddress`; `UDPTransport.externalAddressReceived` | Any established peer could publish a new external IP, including NTCP2, via an Address block | Fixed (static): ordinary Address observations are ignored; only the PeerTest state updates RouterContext |
| NET-158 | SSU2 path challenge | `PeerState2.MAX_PATH_CHALLENGE_SENDS/TIME` | A single lost challenge terminated the session at the next sweep | Residual; deadline/retransmit parity still needs implementation and dynamic loss testing |
| NET-159 | Out-of-session MTU | `PacketBuilder2.encryptPeerTest/buildPing`; SSU2 MTU | PeerTest/HolePunch padding used the default 1420/1500 budget, not the peer/spec 1280 path | Fixed (static): out-of-session sessions clamp to `SSU2_MIN_PACKET_SIZE`; MTU capture pending |
| NET-160 | SSU2 flood flag | `PeerState2.gotRI`; `NTCP2Payload` | RouterInfo flag `0x01` is logged but not propagated | Residual; honor the flood-request bit |
| NET-161 | NTCP2 proxy startup | `NTCPTransport` proxy configuration | An invalid/unsupported `ntcp2.proxy` left `m_NTCP2Server` null and dereferenced it | Fixed (static): server is created independently; invalid proxy disables only the proxy |
| LS-119 | Encrypted inner flag | `EncryptedLeaseSet.setBlindedWhenPublished`; `LeaseSet2` | Decryption cleared the inner `PUBLISHED_ENCRYPTED` bit, breaking encrypted-LS refresh | Fixed (static): preserve the inner flag; outer public/expiry metadata remains authoritative |
| LS-120 | Meta earliest date | `MetaLeaseSet.addLease`; `LeaseSet.getEarliestLeaseDate` | Meta `GetEarliestLeaseDate()` stayed zero, so LS1↔Meta replacement was inverted | Fixed (static): Meta earliest entry is recorded and used for cross-type freshness |
| LS-121 | Inner unpublished bit | `EncryptedLeaseSet.sign` | Locally written encrypted inner omitted `UNPUBLISHED`, allowing extraction/republication | Fixed (static): emit `UNPUBLISHED | PUBLISHED_ENCRYPTED` |
| DB-113 | Client DSRM parser | `DatabaseSearchReplyMessage.readMessage`; `SearchReplyJob` | Destination layer clamped the count and ignored mandatory trailing `from` | Fixed (static): use the strict parser and require a non-zero sender |
| DB-114 | Client DatabaseStore mask | `DatabaseStoreMessage` type mask | Destination layer switched on the raw type byte | Fixed (static): mask `0x0f` before dispatch |
| RS-101 | SU3 header validation | `SU3File.verifyHeader` | File-version byte, exact signature length, non-empty signer/content, and trailing signature data were not all checked | Fixed (static): strict header/signature checks and no trailing bytes |
| SAM-110 | SAM 3.3 ADD/REMOVE | `SAMv3Handler.java:551-566`; `PrimarySession.add/remove` | C++ replied `DESTINATION=` and closed the primary socket on subsession errors | Fixed (static): `SESSION STATUS RESULT=OK ID="…"` / `SESSION STATUS RESULT=I2P_ERROR ID="…" MESSAGE="…"`, socket kept open; `STYLE=PRIMARY` accepted; STREAM subsessions use LISTEN_PORT |
| DG-105 | Empty Datagram2/3 | `Datagram2/3.load`; `I2PSessionMuxedImpl.prepPayload` | A valid zero-output gzip stream was treated as decompression failure | Fixed (static): `InflateChecked` distinguishes valid empty output |
| STR-104 | Streaming MTU/pong | `ConnectionPacketHandler`; `ConnectionManager` | Peer `MAX_PACKET_SIZE` was ignored (including inbound SYN) and pong payload was unbounded | Fixed (static): clamp to peer `>= MIN_MESSAGE_SIZE` on both paths; cap pong at 32 bytes |
| CRY-202 | ElGamal/AES padding | `AESEngine.getPadding` | AES block padding was uninitialized heap data | Fixed (static): randomize padding before encryption |
| CRY-203 | Router N replay | `ECIESAEADEngine.decryptNewSession_N` | Router incoming N had no duplicate-ephemeral cache | Fixed (static): bounded 600 s/256-entry cache after AEAD and payload validation |
| CRY-204 | Ed25519ph primitive | `SigType.java:63`; `DSAEngine.altSign`; SU3 notes | Type 8 used a provider Ed25519ph/dom2 primitive while Java calls plain Ed25519 over the supplied bytes | Fixed (static): both paths use plain Ed25519; callers prehash where required; Java vector still absent |
| CRY-205 | RSA verification | `SigType.java` 4-6; `DSAEngine` | C++ has no RSA verifier for legacy RouterInfo/LS1 | Residual; optional legacy verifier, never advertise RSA for LS2 |
| CRY-206 | Ratchet payload transactionality | `RatchetPayload.java:97-214`; `ECIESAEADEngine.java:591-919` | NSR/ES payload parser applied a valid prefix before a late framing/DateTime error | Fixed (static): `ValidateRatchetPayload` runs before cloves, ACKs, NEXT_KEY and termination |
| CRY-207 | New Session minimum | `ECIESAEADEngine.java:619-630,735-746` | A DateTime-only NS created a session although Java requires at least one Garlic clove | Fixed (static): handshake validator requires a non-empty Garlic block |
| CRY-208 | NS replay cache | `RatchetSKM.java:118,213` | 256-entry FIFO without age allowed eviction/replay inside the 5-minute window | Fixed (static): 4096-entry, 5-minute decaying cache |
| CRY-209 | Ratchet tag boundary | `RatchetTagSet.java:487-494` | C++ rejected index 65534, one tag before Java’s 16-bit limit | Fixed (static): generate indices 0..65534, then fail without advancing |
| CRY-210 | Incoming tag budget | `Garlic.cpp::AddECIESx25519SessionNextTag` | Tag chain advanced before the map-budget check, skipping a peer tag | Fixed (static): reserve budget before advancing |
| CRY-211 | PQ publication | `UDPTransport.addSSU2Options`; SSU2 HYBRID MTU | LibreSSL could publish `pq=3` although ML-KEM-512 is absent; no-PQ builds could parse/select peer PQ hints | Fixed (static): publication and selection require a supported ML-KEM variant |
| CRY-212 | Hybrid key advertisement | `spec/common-structures`; `LeaseSet2` | A non-PQ build could keep a type-5/6/7 identity key in its encryption-key map | Fixed (static): unsupported hybrid types are filtered before LS2 publication |
| CRY-213 | RedDSA key lifetime | Zcash RedDSA key handling | RedDSA signer destructor did not wipe the private scalar | Fixed (static): `OPENSSL_cleanse` on destruction |
| CRY-214 | Inbound session replacement age | `RatchetSKM.SESSION_REPLACE_AGE` | A pending inbound NS had `m_SessionCreatedTimestamp == 0`, so any valid NS could replace it immediately | Fixed (static): set the creation timestamp when the NS is authenticated |
| SAM-111 | SAM parser grammar | `SAMUtils.parseParams` | Quotes/escapes, repeated spaces and bare PING/PONG are not fully parsed | Residual; tokenizer and response matrix pending |
| I2CP-105 | BlindingInfo TYPE_KEY/secret | `ClientMessageEventListener.handleBlindingInfo`; `LookupDestJob` | TYPE_KEY is merged with TYPE_DEST, but secret-blinded/client-auth decryption is absent | Residual; no secret-blinded interoperability claim |

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

SAM 3.2/3.3 quoted/escaped parser, pipelined commands and `LISTEN_PROTOCOL` routing, I2CP secret-blinded/TYPE_KEY b33 auth, per-session outgoing Datagram1/2/3 selection, Datagram2 offline-signature vectors, proposal-154 derived-key reply KDFs, and GOST signature types 9/10 toward Java. The Java 2.13 tree has no usable caller for the 0x12 public-key lookup form; that path is fail-closed and marked pending. SSU2 Path Challenge retransmission, RouterInfo flood-request propagation, RelayResponse role disambiguation, RSA verification, runtime ML-KEM provider probing, type-5/type-7 NSR vectors and the 7/4 multi-key session matrix also remain residual. Some timeout/keepalive policies need dynamic evidence.

## Confirmed findings and fixes (hardening pass)

These follow-up findings come from code inspection and the I2P specifications.
The Java 2.13.0 reference is now available for the current audit; historical
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
  the handshake still does not have a Java-generated malformed corpus and the
  runtime ML-KEM provider probe is not implemented.
- SAM no longer expires queued STREAM ACCEPTs after three seconds, and STREAM
  subsessions use `LISTEN_PORT`; the quoted/escaped tokenizer, pipelined
  commands, `LISTEN_PROTOCOL` routing, receiver reset and large-frame
  regression execution remain residual.
- No Java peer, network handshake, ASan/UBSan/TSan, libFuzzer, LibreSSL,
  OpenBSD, Windows, Android or multi-UID validation exists for these changes.

## Open items requiring dynamic evidence

1. NTCP2 session establishment against 2.13.0 with and without ML-KEM.
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
The harness pins I2P Java 2.13.0 and requires an isolated loopback network; it
is a test procedure, not a Java build/runtime dependency. No result in this
document becomes validated until the dynamic run is recorded.

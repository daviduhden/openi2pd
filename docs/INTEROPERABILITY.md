# I2P Java interoperability audit

Reference implementation audited statically in this session:

| Item | Value |
|---|---|
| Repository | https://github.com/i2p/i2p.i2p |
| Tag | `i2p-2.13.0` |
| Commit | `9134f808337b401e8e53c73734c81fab04280c9d` |
| Release date | 2026-07-24 (tag commit 2026-07-20) |
| Version | 2.13.0 |
| Local copy | read-only clone outside this fork (not bundled); verified 2026-10-18 |
| Newer stable | None found at audit time (GitHub latest release still 2.13.0) |

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
| NTCP2 version negotiation | `spec/ntcp2` | `router/.../transport/ntcp/NTCPTransport.java`, `InboundEstablishState.java`, `OutboundNTCP2State.java` | `libi2pd/NTCP2.cpp` `SetVersion`, SessionRequest/Confirmed | `NTCP2_INT_VERSION=2`, `PQ_INT_VERSION=4`; publishes `v=2` and `pq=4`; accepts 2–5 internally | Publishes `v=2`, `pq` 3–5; accepts same range with OpenSSL PQ | None found; i2pd supports 3 and 5 beyond Java's 4 but negotiates down | Compatible when PQ availability matches; otherwise version 2 | P0 | Existing version tests; no change | Needs dynamic PQ/non-PQ session test |
| NTCP2 handshake/frames | `spec/ntcp2` | `OutboundNTCP2State`, `InboundEstablishState`, `NTCP2Payload` | `libi2pd/NTCP2.cpp` | X25519/ML-KEM, obfuscation, length bounds, 65516 max message | Same structure; separate implementation; data-phase block headers now require 3 bytes before reading a size | None found | Compatible (static) | P0 | Frame header bound fixed; existing code | Needs dynamic session |
| SSU2 version/options | `spec/ssu2` | `router/.../udp/UDPTransport.java`, `PacketBuilder2.java`, `InboundEstablishState2.java` | `libi2pd/SSU2*.cpp` | `SSU2_INT_VERSION=2`; publishes `v=2` plus `pq` for PQ | Publishes `v=2` plus `pq` 3/4; LibreSSL server advertisement now maps unsupported 3 to 2 | C++ could advertise PQ v3 under LibreSSL even though sessions fall back to v2 | Fixed static; no invalid capability advertisement | P0 | `SSU2Server::SetVersion` mapping; validate LibreSSL publication | Fixed (validation pending) |
| SSU2 handshake/tokens/relay | `spec/ssu2` | `OutboundEstablishState2`, `InboundEstablishState2`, `PacketBuilder2` | `libi2pd/SSU2Session.cpp` | Retry/token, relay request/response, peer test blocks | Equivalent blocks and state machine; PathResponse length now includes its 3-byte block header | None found beyond bounds fix | Compatible (static) | P0 | Existing TUI/status only | Needs relay/peer-test dynamic test |
| SSU2 fragmentation | `spec/ssu2` | `InboundMessageState.java`, `OutboundMessageState.java` | `SSU2Session.cpp` `SSU2IncompleteMessage`, fragments | 32 KiB outbound message cap, fragment bitfield, out-of-order retention | Fragment queue and cap; per-session incomplete table now bounded | Unbounded partial-message retention fixed | P1 | Add malformed fragment tests | Pending |
| SSU2 MTU / IPv6 | `spec/ssu2` | `MTU.java`, `PeerState2.java` | `SSU2.cpp`, `RouterContext::SetMTU` | MTU options, ML-KEM minimums, IPv4/IPv6 | Similar MTU publication and checks | OpenBSD discovery remains platform-sensitive | Possible path-specific MTU failure | P0 (OpenBSD) | Prior fork fixes retained | Needs OpenBSD live test |
| I2NP header/types/serialization | `spec/i2np` | `core/.../data/i2np/*.java`, `router/.../message/*` | `libi2pd/I2NPProtocol.*` | 16-byte header, type/size/expiration, compression options | Same header and handlers; DatabaseLookup/SearchReply/DatabaseStore now size buffers from bounded counts | Serialization overflow and one-byte count truncation fixed | Compatible (static) | P0 | `test-protocol-bounds` registered; not run | Needs malformed corpus/fuzz |
| RouterInfo max size | `spec/common-structures` | `RouterInfo.java`: `MAX_UNCOMPRESSED_SIZE = 4*1024` | `libi2pd/RouterInfo.h`: `MAX_RI_BUFFER_SIZE` | Accepts up to 4096 bytes uncompressed | Cap is 4096; DatabaseStore decompression now accepts exactly 4096 too | **Confirmed difference fixed** | Valid 4096-byte Java RouterInfo no longer rejected at the inflate check | P0 | Cap 4096; `uncompressedSize <= MAX_RI_BUFFER_SIZE` | Static; needs large-RI fixture |
| RouterInfo parsing/validation | `spec/common-structures` | `RouterInfo.java`, `RouterAddress.java` | `RouterInfo.cpp` | Address options, caps, signature, bounds | Similar parser with 4096 cap, explicit peer-list bound and store-key identity check | No other confirmed mismatch | Compatible (static) | P0 | Bounds/key-binding fixes; large/malformed RI tests pending | Pending |
| LeaseSet/LeaseSet2 | `spec/common-structures` | `core/.../data/LeaseSet.java`, `LeaseSet2.java` | `libi2pd/LeaseSet.*` | Standard and LS2 variants, flags, offline signatures | LS2 and encrypted LS2 paths; expiration pre-scan no longer reads past the buffer; DBStore key/identity bound | Off-by-one in LeaseSet expiration extraction fixed | Compatible (static) | P1 | `test-protocol-bounds` registered; not run | Needs cross-parse vectors |
| EncryptedLeaseSet / b33 | `spec/encrypted-leaseset` | `LeaseSet2.java`, `BlindedPublicKey`/blinding classes | `Blinding.cpp`, `LeaseSet2` | Blinded keys, auth, offline batch | Same scheme; encrypted entries are not wrongly compared to identity hash | None found | Compatible (static) | P1 | b33 tool tests | Pending |
| netDb store/lookup/floodfill | `spec/netdb` | `router/.../networkdb/kademlia/*.java` | `libi2pd/NetDb.*`, `KadDHT.*` | Kademlia lookups, floodfill rules, expiration | Equivalent responsibilities; store keys checked against payload identity; ineligible floodfills reset; `SaveUpdated` now snapshots under mutex | Store-key spoofing, stale floodfill flag and iterator race fixed | Compatible (static) | P1 | Snapshot under mutex; TSan/lookup tests pending | Needs dynamic lookup |
| Reseed/SU3 | `spec/su3` | `router/.../util/SU3File.java` | `Reseed.cpp` | SU3 header/signature/ZIP | Checked reads and bounds | C++ hardening only | Compatible (static) | P1 | Malformed fixture pending | Pending |
| Tunnel build (short/variable) | `spec/tunnel-creation` | `router/.../tunnel/*.java`, build request records | `TunnelConfig.*`, `Tunnel.*` | Record layouts, reply mapping | Equivalent | None found | Compatible (static) | P1 | Existing tests limited | Needs live tunnels |
| Garlic / tunnel messages | `spec/tunnel-message`, garlic | `router/.../router/...` message handling, garlic | `Garlic.cpp`, `TransitTunnel.cpp` | Layered messages, delivery instructions | Equivalent | None found | Compatible (static) | P1 | None | Needs live tunnel traffic |
| ECIES-X25519-AEAD-Ratchet | `spec/ecies` | `core/.../crypto/ecies/...`, ratchet session classes | `ECIESX25519AEADRatchetSession.*` | IK/ratchet, tags, AEAD, nonces | Same external format | None found in this pass | Compatible (static) | P0 | Existing AEAD tests only | Needs cross-vectors |
| Signatures | `spec/common-structures` | `SigType.java` types 0–8, 11 | `Identity.h`, `Signature.*` | DSA/ECDSA/RSA/Ed25519/Ed25519ph/RedDSA | Same constants plus GOST 9/10 | GOST is not in Java stable list; optional | No interoperability impact for common types | P2 | No change | Existing vectors |
| ML-KEM encryption types | proposal 169 / `spec/ecies` | `EncType.java` codes 5–7 | `PostQuantum.*`, `Crypto.h` | MLKEM512/768/1024 hybrid | Same codes when OpenSSL PQ available | LibreSSL falls back explicitly | PQ sessions only when both support | P1 | Capability checks retained | Needs PQ interop |
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

## Current continuation audit (2026-10-18)

This table records the second static pass against the pinned Java 2.13.0 tree.
`Fixed (static)` means the C++ source was changed; **no dynamic test was run**.

| ID | Area | Specification / Java reference | openi2pd reference | Java behavior (static) | openi2pd behavior | Difference / consequence | Priority | Correction | Tests / validation |
|---|---|---|---|---|---|---|---|---|---|
| NET-101 | SSU2 PQ option list | `UDPTransport.java:145-153, 959-1021`; SSU2 spec | `RouterInfo.cpp` `pq` parser | Publishes `pq="4,3"`; selects 4/3 | Parsed only one character, so `4,3` left version 2 | Silent PQ downgrade C++ Alice -> Java; masked relay bug | P1 | Parse comma list and choose strongest supported value | Fixed (static); needs v4 capture |
| NET-102 | SSU2 RelayRequest version | `SSU2Util.java:231`; `IntroductionManager.java:452-457, 778-783` | `SSU2Session.cpp::Introduce` | Requires relay request `ver=2` | Sent session version 3/4 | Java Bob/Charlie drop relay requests on PQ sessions | P1 | Hard-code relay request version 2 | Fixed (static); relay dynamic test pending |
| NET-103 | SSU2 SessionConfirmed fragments | `PacketBuilder2.java:541-710`; `InboundEstablishState2.java:891-957` | `SSU2Session.cpp::ProcessSessionConfirmed` | Reassembles 1..15 fragments | Rejected more than 2 | Large Java RouterInfo over low-MTU paths fails | P1 | Generic bounded 1..15 fragment reassembly | Fixed (static); large-RI/IPv6 test pending |
| NET-104 | SSU2 ACK block expansion | `SSU2Payload.java:289-306`; `SSU2Bitfield.java:190-226` | `SSU2Session.cpp::SendQueue` | Parses declared ranges | Expanded ACK size without initializing ranges | Possible false ACK or dropped ACK | P0 | Drop ACK on no space; never extend an uninitialized block | Fixed (static); instrument/fuzz pending |
| NET-105 | SSU2 peer-test buffer | `SSU2Util.java:192-212` | `SSU2Session.cpp::CreatePeerTestBlock` | Allocates signature space for any identity | 96-byte stack buffer; overflow for P-384/P-521/RSA | Memory corruption / peer-test failure with non-Ed25519 identity | P0 | Dynamic vector sized for signature length | Fixed (static); ASan peer-test pending |
| NET-106 | SSU2 Network ID | `PacketHandler.java:250-263, 319-336, 446-483` | `SSU2Session.cpp` long-header handlers | Rejects wrong netID with reason 21 | Did not read `flags[1]` | Cross-network handshake accepted | P1 | Validate netID in SessionRequest/TokenRequest/Retry/HolePunch | Fixed (static); two-netID test pending |
| NET-107 | SSU2 version fallback | `InboundEstablishState2.java:124-133`; `OutboundEstablishState2.java:390-395` | `SSU2Session.cpp::ProcessRetry`, `SendSessionRequest` | Treats version Retry as terminal in Java 2.13 | Retry reset version 2 but resend re-applied peer PQ version | Retry loop until timeout | P2 | Persist fallback flag across resend | Fixed (static); low-MTU/mock test pending |
| NET-108 | SSU2 ACK0 loss | `PeerState2.java:165-183, 511-522` | `SSU2Session.cpp::ProcessData` | Sends ACK0 once; drops repeated confirmed | Waited only for ACK before Established | Lost ACK0 causes 5 s failure | P2 | Enter Established on first decryptable data packet | Fixed (static); netem ACK0 loss test pending |
| NET-109 | SSU2 packet 0 duplicate | `PeerState2` receive counters | `SSU2Session.cpp::ProcessData` | Initial packet accepted once | `!packetNum` bypassed duplicate check | Duplicate packet 0 possible | P3 | One-time initialization then normal counter | Fixed (static); duplicate packet test pending |
| NET-110 | SSU2 data size bound | SSU2 packet framing | `SSU2Session.cpp::SendData` | Includes header/MAC | Check omitted 16-byte MAC | Latent oversized datagram | P3 | Check `len + 32` | Fixed (static); boundary test pending |
| LS-101 | Encrypted LeaseSet store key | `EncryptedLeaseSet.java:377-388`; proposal 123 | `LeaseSet.cpp`, `NetDb.cpp::AddLeaseSet2` | Store key is SHA256(blinded sig type || blinded key) | Type-5 entry was accepted under any key | Lookup poisoning / wrong encrypted LS served | P0 | Compute store hash while parsing and bind to DatabaseStore key | Fixed (static); cross-key fixture pending |
| LS-102 | LeaseSet replay eviction | `Kademlia...Facade.java:1051-1183` | `NetDb.cpp::AddLeaseSet*` | Validates replacement; leaves old entry on failure | Expired/unpublished LS2 erased a valid entry; LS1 could replace LS2 without freshness | Replayable eviction/stale LeaseSet | P0 | Never erase on invalid replacement; compare LS1 expiry with existing LS2 | Fixed (static); replay test pending |
| LS-103 | Floodfill LS2 lease dates | `Kademlia...Facade.java:993-1035`; `LeaseSet.java:200-261` | `LeaseSet.cpp::ReadStandardLS2TypeSpecificPart` | Rejects all-expired/far-future leases | Floodfill skipped lease dates (`storeLeases=false`) | Serves dead leases; accepts invalid policy | P1 | Always walk leases, validate dates and require one current lease | Fixed (static); expired-LS2 fixture pending |
| RT-101 | Ratchet NS freshness/replay | `ECIESAEADEngine.java:591-625, 1465-1476` | `ECIESX25519AEADRatchetSession.cpp` | Requires DateTime in NS and checks duplicate ephemeral | No DateTime requirement/skew or replay cache | Captured NS replay after throttle | P0 | Validate DateTime block (4 bytes, -5/+2 min) and bounded ephemeral replay cache | Fixed (static); replay/time-skew test pending |
| RT-102 | PQ NSR retransmission | `ECIESAEADEngine.java:784-841, 1257-1319` | `ECIESX25519AEADRatchetSession.cpp::NextNewSessionReplyMessage` | Clones full handshake state per NSR | Restored CK but omitted static-DH MixKey | Second PQ NSR fails authentication | P1 | Re-run `x25519(besk, apk)` MixKey after ML-KEM | Fixed (static); two-NSR vector pending |
| RT-103 | Multi-key LeaseSet2 | `LeaseSet2.java:186-205`; `ECIESAEADEngine.java:1133-1148` | `LeaseSet.cpp`, `Datagram.cpp`, `Streaming.cpp`, `Destination.cpp` | Selects acceptable key from LS order | Kept one encryptor and compared only that key | Valid ratchet peer rejected when using another advertised key | P1 | Retain all key sections and match by remote key type | Fixed (static); 7/4 vs 6/4 matrix pending |
| RT-104 | Ratchet payload strictness | `RatchetPayload.java:105-205` | `ECIESX25519AEADRatchetSession.cpp::HandlePayload` | Rejects blocks after padding/termination and bad lengths | More permissive | Accepted-invalid/state divergence | P2 | Add ordering, exact DATETIME/NEXTKEY/ACK lengths | Fixed (static); malformed corpus pending |
| DG-101 | Datagram2 minimum | `Datagram2.java:68`; DSA identity sizes | `Datagram.cpp::HandleDatagram2` | Minimum 429 bytes | Rejected <433 | Valid 429-432-byte DSA datagrams dropped | P2 | Lower bound to 429, then parse identity | Fixed (static); DSA vector pending |
| DG-102 | Datagram3 first contact | `SAMv3DatagramSession.java:99-110` | `Datagram.cpp::HandleDatagram3` | Delivers sender hash without cached LS | Requires cached LS/static key match | Unsolicited DGRAM3 dropped | P2 | Accept authenticated ratchet sender; use LS check only when available | Not implemented; needs identity plumbing | Requires dynamic test |
| DG-103 | Datagram2 offline signatures | `Datagram2.java:117-129` | `Datagram.cpp` | Emits offline section | Not emitted | Offline-signed D2 unverifiable | P2 | Implement offline block or reject offline keys | Not implemented | Requires vector test |
| CR-101 | Elligator boundary | `Elligator2.java:221-224` | `Elligator.cpp` | Rejects `r >= (p-1)/2` | Accepted equality | Accepted-invalid representative | P3 | Strict `<` comparison | Fixed (static); crafted representative test pending |
| SAM-101 | SAM NAMING/HELLO | `SAMv1Handler.java:426-427`; `SAMHandlerFactory.java:133-155` | `SAM.cpp` | KEY_NOT_FOUND; accepts single-digit version | Returned INVALID_KEY; rejected `"3"` | Wrong result/version negotiation | P2 | Use KEY_NOT_FOUND and normalize one-digit version | Fixed (static); SAM 3.1 handshake test pending |
| SAM-102 | SAM 3.2 ports | SAM v3 spec; `SAMv3Handler.java:89-96, 920-948`; `SAMv3StreamSession.java:173-188` | `SAM.cpp` | Ports/protocol/header supported | Session defaults/connect/datagram now parsed; accept/raw framing still partial | Some 3.2 clients routed incorrectly | P1 | Complete command/response port plumbing or stop advertising 3.2+ | Partial; dynamic SAM port test pending |
| SAM-103 | STREAM CONNECT status | `SAMv3StreamSession.java:163-201` | `SAM.cpp::Connect`/timer | Waits for connection then OK/failure | Returned OK immediately | Client starts sending before reachability | P1/P2 | Poll `Stream::IsEstablished` and report failure/timeout | Fixed (static); unreachable destination test pending |
| SAM-104 | Streaming RESET | `ConnectionPacketHandler.java:537-629` | `Streaming.cpp::Close/SendReset` | Sends RESET on abort | TODO, no reset | Peer hangs until timeout | P2 | Added signed RESET packet; suppress echo on received reset | Fixed (static); abort/close test pending |
| I2CP-101 | HostLookup options | `HostLookupMessage.java:189-207`; `I2PSessionImpl.java:232-257` | `I2CP.cpp::HostLookupMessageHandler` | Types 2/3/4 and b33 lookup | Only types 0/1; b33 TODO | Java `lookupDest2`/b33 fails | P1/P2 | Parse 2/3/4 and resolve blinded addresses asynchronously | Fixed (static); Java client test pending |
| I2CP-102 | BlindingInfo | `I2PSessionImpl.java:2234-2242` | `I2CP.cpp` | Sends/uses type 42 | No handler | Encrypted destination setup silently ineffective | P2 | Implement blinding cache or do not advertise 0.9.70 feature set | Not implemented | Requires dynamic test |
| NET-111 | MetaLS lookup | `MetaLeaseSet.java:24-25`; `DatabaseEntry.java:52` | `LeaseSet.cpp` type-7 parser | Java stores/serves MetaLS | Parses/validates/stores/serves type 7 | No current publication gap; dynamic Java vector pending | P1 | Meta LS2 parser and netDb path | Fixed (static); Java publication/lookup test pending |
| NET-112 | DLM reply-key 0x12 | `DatabaseLookupMessage.java:390-421, 517-526` | `NetDb.cpp::HandleDatabaseLookupMsg` | 32-byte ratchet pubkey, no tag | Branches 0x02/0x10/0x12; bounded tags; fail-closed encrypted reply | No cleartext fallback; proposal-154 derived KDF still TBD | P1 | Flag-specific parse/wrap | Fixed (static); 0x12 vector pending |
| NET-113 | DSRM sender binding | `IterativeLookupJob.java:38-45`; `SearchMessageSelector.java:79-86` | `NetDbRequests.cpp` | Requires queried sender/from hash | Requires complete trailing from and queried-sender association | Truncated/fabricated replies rejected | P2 | Full from/count/queried-peer checks | Fixed (static); malformed DSRM test pending |
| NET-114 | Tunnel build time | `BuildHandler.java:101-104, 776-812` | `TransitTunnel.cpp` | Enforces 8-minute ECIES age and 5-minute future; drops without reply; code 30 for unsupported short layer encryption | Now validates minute windows after decryption; ignores future-duration expiration as Java does | Stale/future requests dropped; zero-payload/flags/IDs guards added | P2 | Minute-window validation and hostile-record drops | Fixed (static); clock-skew and Bloom-filter replay tests pending |
| NET-115 | Floodfill RI freshness | `HandleDatabaseLookupMessageJob.java:57, 130-140` | `NetDb.cpp::HandleDatabaseLookupMsg` | Serves only current RIs (1 h) | Applies one-hour plus two-minute tolerance to RouterInfo only; self always served | Stale RouterInfo suppressed | P2 | RouterInfo-only freshness check | Fixed (static); aged-RI test pending |

### Corrections applied in this pass (static)

- `RouterInfo.cpp`: SSU2 `pq` list parsing.
- `SSU2Session.cpp/.h`: relay version 2, 1–15 fragment reassembly, safe ACK handling, peer-test signature buffer, netID checks, PQ fallback flag, ACK0-loss establishment, one-time packet-0 handling, data-size bound.
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
| I2P-002 | DatabaseLookup reply-key 0x12 | Implemented / static; 0x02/0x10/0x12 forms, bounded tags, fail-closed; proposal-154 public-key path not exercised by Java 2.13. |
| I2P-003 | DatabaseSearchReply sender binding | Implemented / static; trailing sender hash, count bounds and queried-sender association. |
| I2P-004 | Tunnel-build time windows | Implemented / static; 8-minute age, 5-minute future, Java-style silent drop; code 30 for unsupported layer encryption. Duplicate-build Bloom filter and dynamic tests pending. |
| I2P-005 | Floodfill RouterInfo freshness | Implemented / static; one-hour RouterInfo freshness in floodfill lookup only. |
| I2P-006 | SAM 3.2 | Implemented / static; SAM 3.2 port/protocol defaults, connect/accept, datagram/raw framing and version gating. |
| I2P-007 | I2CP BlindingInfo type 42 | Implemented / static; type-42 parser/cache and current-date b33 association; secret-blinded/TYPE_KEY association pending. |
| I2P-008 | Datagram3 first contact | Implemented / static; hash receiver and bounded first-contact Datagram3 delivery. |
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
| I2P-001 | Meta LS2 type 7 | proposal 123 | `MetaLeaseSet.readBytes`, `MetaLease.writeBytes/readBytes` | `LeaseSet2::ReadMetaLS2TypeSpecificPart`, `NetDb::AddLeaseSet2` | Type 7 was rejected | Meta LS2 could not be stored/served by i2pd floodfills | P1 | Parser, 40-byte entries, revocations, expiry/signature/netDb path | Reviewed; bounds and signature path traced | `tests/interop/FIXTURES.md` MetaLS cases | Pending Java-generated vector |
| I2P-002 | DLM reply key 0x12 | proposal 154 | `DatabaseLookupMessage.readMessage/writeMessageBody` | `NetDb::HandleDatabaseLookupMsg` | 0x12 was parsed as key+tag and could fall back to cleartext | Lost replies / cleartext risk | P1 | Flag-specific parse, bounded tags, fail-closed encrypted reply | Reviewed; Java 2.13 has no caller for the public-key form | `i2np-dblookup-ratchet-pubkey` vector | Pending 0x12/derived-KDF test |
| I2P-003 | DSRM sender binding | Kademlia lookup | `IterativeLookupJob.runJob`, `SearchMessageSelector.isMatch` | `NetDbRequests::HandleDatabaseSearchReplyMsg` | Truncated/unqueried replies accepted | Lookup injection/stale termination | P2 | Complete trailing from, count bounds, queried-peer set | Reviewed | Malformed DSRM cases | Pending negative/dynamic test |
| I2P-004 | Tunnel build time | tunnel-creation | `BuildHandler.handleReq` | `TransitTunnel.cpp` time helper | No request age/future/replay window | Stale/future builds accepted | P1 | 8-minute ECIES age, 5-minute future, drop semantics; OBGW zero receive ID allowed; flag/short guards | Reviewed | Boundary cases listed in template | Pending clock-skew/live test |
| I2P-005 | Floodfill RI freshness | netDb | `HandleDatabaseLookupMessageJob` `EXPIRE_DELAY` | `NetDb::HandleDatabaseLookupMsg` | Stale RIs served | Dead router data returned | P2 | One-hour + 2-minute tolerance for RouterInfo only | Reviewed | Aged-RI case planned | Pending aged-RI/Java lookup |
| I2P-006 | SAM 3.2 | SAM v3 spec | `SAMv3Handler.notifyStreamIncomingConnection`, `SAMv1Handler.receiveRaw/Datagram` | `SAM.cpp` stream/datagram paths | Ports/protocol/framing missing | Wrong service/port; clients see wrong metadata | P2 | Session defaults, connect/accept ports, datagram/raw fields, version gating | Reviewed; old SAM strings retained | SAM 3.0/3.1/3.2 cases | Pending terminal matrix |
| I2P-007 | I2CP BlindingInfo 42 | proposal 123 | `BlindingInfoMessage.doReadMessage`, `ClientMessageEventListener.handleBlindingInfo` | `I2CP.cpp` type-42 cache/HostLookup | Type 42 ignored | b33 setup ineffective | P1 | Parser, bounded cache, current-date blinded-key association | Reviewed; secret/TYPE_KEY limitations documented | I2CP vectors | Pending Java b33 client test |
| I2P-008 | Datagram3 first contact | proposal 163 | `SAMv3DatagramSession` hash delivery | `Datagram::HandleDatagram3`, `HashReceiver` | Required cached LeaseSet | First-contact DGRAM3 dropped | P2 | Authenticated hash receiver, bounded sessions, optional LS validation | Reviewed | No-cache/concurrency cases | Pending dynamic first-contact |
| I2P-009 | Datagram2 offline | proposal 163 | `Datagram2.make/read` | `DatagramDestination::SendDatagram/HandleDatagram2` | Offline section not emitted/validated | Offline-signed D2 undecodable | P2 | Offline flag/section, signed-region verification, delegation expiry | Reviewed; signature coverage traced | Offline-key vectors | Pending Java receiver test |
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

### Known remaining incompatibilities (not claimed fixed)

SAM 3.2/3.3 edge/error matrix, I2CP secret-blinded/TYPE_KEY BlindingInfo association, Datagram3 without any cached identity for legacy receivers, Datagram2 offline-signature vectors, proposal-154 derived-key reply KDFs, and GOST signature types 9/10 toward Java. The Java 2.13 tree has no usable caller for the 0x12 public-key lookup form; that path is fail-closed and marked pending. SSU2 RelayResponse role disambiguation and some timeout/keepalive policies also need dynamic evidence.

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

### Residual code-level concerns (not changed in this pass)

- `NetDb::SaveUpdated()` now snapshots `m_RouterInfos` under
  `m_RouterInfosMutex` and locks the floodfill count before iteration.
  TSan and a live lookup/save test are still required.
- `RouterInfo::ReadFromBuffer()` passes the whole remaining buffer to the
  address/property parameter parser, so a malformed parameter length can make
  the parser consume bytes belonging to the next field. The signature still
  covers the whole message and the buffer bounds hold, but strict per-field
  bounds should be added with a spec/Java comparison.
- `SSU2Session::HandleAddress()` accepts an Address block whose address family
  differs from the packet transport. No memory-safety impact, but it can
  update the wrong family's external address; Java behavior needs checking.
- `LeaseSet2::ExtractPublishedTimestamp()` and the offline-signature helpers
  use `>=` where an exact-fit buffer is legal. This is stricter than necessary,
  not unsafe; verify against Java before relaxing.

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

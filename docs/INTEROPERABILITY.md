# I2P Java interoperability audit

Reference implementation audited statically in this session:

| Item | Value |
|---|---|
| Repository | https://github.com/i2p/i2p.i2p |
| Tag | `i2p-2.13.0` |
| Commit | `9134f808337b401e8e53c73734c81fab04280c9d` |
| Release date | 2026-07-24 (tag commit 2026-07-20) |
| Version | 2.13.0 |
| Local copy | `/tmp/opencode/i2p-java` (outside this fork, read-only reference) |
| Newer stable | None found at audit time (GitHub latest release still 2.13.0) |

The Java tree is **not** vendored and is not a build/runtime dependency of
Open I2Pd. No Java code was modified.

Status vocabulary:

- **Compatible (static)** — behavior matches by code/spec inspection.
- **Fixed (validation pending)** — C++ changed to match; dynamic test missing.
- **Optional / not implemented** — feature not required for the protocols this
  fork announces.
- **Needs dynamic validation** — cannot be concluded from source alone.
- **Not evaluated** — outside this audit pass.

## Matrix

| Area | Specification | I2P Java reference | Open I2Pd reference | Java behavior (static) | Open I2Pd behavior | Difference | Interoperability consequence | Priority | Change / tests | Validation |
|---|---|---|---|---|---|---|---|---|---|---|
| NTCP2 version negotiation | `spec/ntcp2` | `router/.../transport/ntcp/NTCPTransport.java`, `InboundEstablishState.java`, `OutboundNTCP2State.java` | `libi2pd/NTCP2.cpp` `SetVersion`, SessionRequest/Confirmed | `NTCP2_INT_VERSION=2`, `PQ_INT_VERSION=4`; publishes `v=2` and `pq=4`; accepts 2–5 internally | Publishes `v=2`, `pq` 3–5; accepts same range with OpenSSL PQ | None found; i2pd supports 3 and 5 beyond Java's 4 but negotiates down | Compatible when PQ availability matches; otherwise version 2 | P0 | Existing version tests; no change | Needs dynamic PQ/non-PQ session test |
| NTCP2 handshake/frames | `spec/ntcp2` | `OutboundNTCP2State`, `InboundEstablishState`, `NTCP2Payload` | `libi2pd/NTCP2.cpp` | X25519/ML-KEM, obfuscation, length bounds, 65516 max message | Same structure; separate implementation | None found | Compatible (static) | P0 | Existing code | Needs dynamic session |
| SSU2 version/options | `spec/ssu2` | `router/.../udp/UDPTransport.java`, `PacketBuilder2.java`, `InboundEstablishState2.java` | `libi2pd/SSU2*.cpp` | `SSU2_INT_VERSION=2`; publishes `v=2` plus `pq` for PQ | Publishes `v=2` plus `pq` 3/4; parses both | i2pd's `v` parser historically accepted only exact `"2"`; current Java also publishes exact `"2"` | No current difference; a nonstandard `v=2,4` peer would be rejected | P1 | Keep as-is; documented | Needs peer with legacy combined `v` value |
| SSU2 handshake/tokens/relay | `spec/ssu2` | `OutboundEstablishState2`, `InboundEstablishState2`, `PacketBuilder2` | `libi2pd/SSU2Session.cpp` | Retry/token, relay request/response, peer test blocks | Equivalent blocks and state machine | None found | Compatible (static) | P0 | Existing TUI/status only | Needs relay/peer-test dynamic test |
| SSU2 fragmentation | `spec/ssu2` | `InboundMessageState.java`, `OutboundMessageState.java` | `SSU2Session.cpp` `SSU2IncompleteMessage`, fragments | 32 KiB outbound message cap, fragment bitfield, out-of-order retention | Fragment queue and cap | None found | Compatible (static) | P1 | Add malformed fragment tests | Pending |
| SSU2 MTU / IPv6 | `spec/ssu2` | `MTU.java`, `PeerState2.java` | `SSU2.cpp`, `RouterContext::SetMTU` | MTU options, ML-KEM minimums, IPv4/IPv6 | Similar MTU publication and checks | OpenBSD discovery remains platform-sensitive | Possible path-specific MTU failure | P0 (OpenBSD) | Prior fork fixes retained | Needs OpenBSD live test |
| I2NP header/types/serialization | `spec/i2np` | `core/.../data/i2np/*.java`, `router/.../message/*` | `libi2pd/I2NPProtocol.*` | 16-byte header, type/size/expiration, compression options | Same header and handlers | None found | Compatible (static) | P0 | Existing tests only | Needs malformed corpus/fuzz |
| RouterInfo max size | `spec/common-structures` | `RouterInfo.java`: `MAX_UNCOMPRESSED_SIZE = 4*1024` | `libi2pd/RouterInfo.h`: `MAX_RI_BUFFER_SIZE` | Accepts up to 4096 bytes uncompressed | Was capped at 3072 | **Confirmed difference** | Valid large Java RI could be rejected | P0 | **Fixed**: cap raised to 4096 with Java reference comment | Static; needs large-RI test |
| RouterInfo parsing/validation | `spec/common-structures` | `RouterInfo.java`, `RouterAddress.java` | `RouterInfo.cpp` | Address options, caps, signature, bounds | Similar parser with 4096 cap now | No other confirmed mismatch | Compatible (static) | P0 | Add large/malformed RI tests | Pending |
| LeaseSet/LeaseSet2 | `spec/common-structures` | `core/.../data/LeaseSet.java`, `LeaseSet2.java` | `libi2pd/LeaseSet.*` | Standard and LS2 variants, flags, offline signatures | LS2 and encrypted LS2 paths | None found in this pass | Compatible (static) | P1 | Existing crypto tests | Needs cross-parse vectors |
| EncryptedLeaseSet / b33 | `spec/encrypted-leaseset` | `LeaseSet2.java`, `BlindedPublicKey`/blinding classes | `Blinding.cpp`, `LeaseSet2` | Blinded keys, auth, offline batch | Same scheme | None found | Compatible (static) | P1 | b33 tool tests | Pending |
| netDb store/lookup/floodfill | `spec/netdb` | `router/.../networkdb/kademlia/*.java` | `libi2pd/NetDb.*`, `KadDHT.*` | Kademlia lookups, floodfill rules, expiration | Equivalent responsibilities | None confirmed | Compatible (static) | P1 | Counts/reseed fixes | Needs dynamic lookup |
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
  rejected by Open I2Pd, preventing netDb/floodfill visibility.
- **Fix:** `MAX_RI_BUFFER_SIZE` raised to 4096 with a comment referencing the
  Java constant. NTCP2 handshake-specific limits remain separate.
- **Security:** buffers remain fixed-size and all parsers still enforce the cap;
  no unbounded allocation was introduced.
- **Validation:** static only; a >3072-byte RouterInfo fixture and live Java
  publication test are pending.

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

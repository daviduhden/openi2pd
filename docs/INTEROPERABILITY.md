# I2P Java interoperability audit

Reference implementation audited statically in this session:

| Item | Value |
|---|---|
| Repository | https://github.com/i2p/i2p.i2p |
| Tag | `i2p-2.13.0` |
| Commit | `9134f808337b401e8e53c73734c81fab04280c9d` |
| Release date | 2026-07-24 (tag commit 2026-07-20) |
| Version | 2.13.0 |
| Local copy | read-only clone outside this fork (not bundled) |
| Newer stable | None found at audit time (GitHub latest release still 2.13.0) |

The Java tree is **not** vendored and is not a build/runtime dependency of
openi2pd. No Java code was modified.

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
| SSU2 version/options | `spec/ssu2` | `router/.../udp/UDPTransport.java`, `PacketBuilder2.java`, `InboundEstablishState2.java` | `libi2pd/SSU2*.cpp` | `SSU2_INT_VERSION=2`; publishes `v=2` plus `pq` for PQ | Publishes `v=2` plus `pq` 3/4; parses both | i2pd's `v` parser historically accepted only exact `"2"`; current Java also publishes exact `"2"` | No current difference; a nonstandard `v=2,4` peer would be rejected | P1 | Keep as-is; documented | Needs peer with legacy combined `v` value |
| SSU2 handshake/tokens/relay | `spec/ssu2` | `OutboundEstablishState2`, `InboundEstablishState2`, `PacketBuilder2` | `libi2pd/SSU2Session.cpp` | Retry/token, relay request/response, peer test blocks | Equivalent blocks and state machine; PathResponse length now includes its 3-byte block header | None found beyond bounds fix | Compatible (static) | P0 | Existing TUI/status only | Needs relay/peer-test dynamic test |
| SSU2 fragmentation | `spec/ssu2` | `InboundMessageState.java`, `OutboundMessageState.java` | `SSU2Session.cpp` `SSU2IncompleteMessage`, fragments | 32 KiB outbound message cap, fragment bitfield, out-of-order retention | Fragment queue and cap; per-session incomplete table now bounded | Unbounded partial-message retention fixed | P1 | Add malformed fragment tests | Pending |
| SSU2 MTU / IPv6 | `spec/ssu2` | `MTU.java`, `PeerState2.java` | `SSU2.cpp`, `RouterContext::SetMTU` | MTU options, ML-KEM minimums, IPv4/IPv6 | Similar MTU publication and checks | OpenBSD discovery remains platform-sensitive | Possible path-specific MTU failure | P0 (OpenBSD) | Prior fork fixes retained | Needs OpenBSD live test |
| I2NP header/types/serialization | `spec/i2np` | `core/.../data/i2np/*.java`, `router/.../message/*` | `libi2pd/I2NPProtocol.*` | 16-byte header, type/size/expiration, compression options | Same header and handlers; DatabaseLookup/SearchReply/DatabaseStore now size buffers from bounded counts | Serialization overflow and one-byte count truncation fixed | Compatible (static) | P0 | `test-protocol-bounds` prepared | Needs malformed corpus/fuzz |
| RouterInfo max size | `spec/common-structures` | `RouterInfo.java`: `MAX_UNCOMPRESSED_SIZE = 4*1024` | `libi2pd/RouterInfo.h`: `MAX_RI_BUFFER_SIZE` | Accepts up to 4096 bytes uncompressed | Cap is 4096; DatabaseStore decompression now accepts exactly 4096 too | **Confirmed difference fixed** | Valid 4096-byte Java RouterInfo no longer rejected at the inflate check | P0 | Cap 4096; `uncompressedSize <= MAX_RI_BUFFER_SIZE` | Static; needs large-RI fixture |
| RouterInfo parsing/validation | `spec/common-structures` | `RouterInfo.java`, `RouterAddress.java` | `RouterInfo.cpp` | Address options, caps, signature, bounds | Similar parser with 4096 cap, explicit peer-list bound and store-key identity check | No other confirmed mismatch | Compatible (static) | P0 | Bounds/key-binding fixes; large/malformed RI tests pending | Pending |
| LeaseSet/LeaseSet2 | `spec/common-structures` | `core/.../data/LeaseSet.java`, `LeaseSet2.java` | `libi2pd/LeaseSet.*` | Standard and LS2 variants, flags, offline signatures | LS2 and encrypted LS2 paths; expiration pre-scan no longer reads past the buffer; DBStore key/identity bound | Off-by-one in LeaseSet expiration extraction fixed | Compatible (static) | P1 | `test-protocol-bounds` prepared | Needs cross-parse vectors |
| EncryptedLeaseSet / b33 | `spec/encrypted-leaseset` | `LeaseSet2.java`, `BlindedPublicKey`/blinding classes | `Blinding.cpp`, `LeaseSet2` | Blinded keys, auth, offline batch | Same scheme; encrypted entries are not wrongly compared to identity hash | None found | Compatible (static) | P1 | b33 tool tests | Pending |
| netDb store/lookup/floodfill | `spec/netdb` | `router/.../networkdb/kademlia/*.java` | `libi2pd/NetDb.*`, `KadDHT.*` | Kademlia lookups, floodfill rules, expiration | Equivalent responsibilities; store keys now checked against payload identity; ineligible floodfills are reset to non-floodfill | Store-key spoofing and stale floodfill flag fixed | Compatible (static) | P1 | Counts/reseed fixes; tests pending | Needs dynamic lookup |
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

## Confirmed findings and fixes (hardening pass)

These follow-up findings come from code inspection and the I2P specifications.
The Java 2.13.0 clone was not available during this pass, so each item remains
**Fixed (validation pending)** and still needs the differential test listed.
No wire format was changed.

The new regression source is `tests/test-protocol-bounds.cpp`; a libFuzzer
harness for the same parsers is `tests/fuzz/fuzz-protocol-bounds.cpp`. Neither
is registered yet in `tests/CMakeLists.txt`; the coordinator must add
`test-protocol-bounds` to `I2PD_TESTS` and `fuzz-protocol-bounds` to the
`WITH_FUZZERS` loop before they can run.

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

### Residual code-level concerns (not changed in this pass)

- `NetDb::SaveUpdated()` iterates `m_RouterInfos` without holding
  `m_RouterInfosMutex`, while transport threads may call `AddRouterInfo()`
  (insert/erase). This is an iterator-invalidation/data-race risk that needs an
  object-level locking or snapshot design, not a one-line change.
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

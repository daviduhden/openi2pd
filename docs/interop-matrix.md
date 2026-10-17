> **Superseded:** the current, evidence-oriented matrix is [INTEROPERABILITY.md](INTEROPERABILITY.md). This file is retained as the earlier high-level matrix.

# I2P Java interoperability matrix

Reference specifications: <https://i2p.net/en/docs/specs/>.

Status legend:

- **Implemented**: code exists in this fork and was reviewed during this
  refactoring.
- **Modified**: code or parsing behavior changed in this refactoring.
- **Pending**: no live Java-router interoperability test was executed here.
  Static review is not a compatibility guarantee.

No protocol format was changed in this refactoring. The only wire-visible
change is stricter rejection of malformed HTTP input in the client proxy, plus
acceptance of valid HTTP headers that were previously rejected.

| Area | I2P Java state | This fork before | Difference found | Specification / code reference | Change in this work | Validation |
|---|---|---|---|---|---|---|
| NTCP2 handshake, framing, data phase | Production transport | Implemented | None found in static comparison | `spec/ntcp2`, `libi2pd/NTCP2.cpp`, `NTCP2.h` | Unchanged | Pending Java interop run |
| NTCP2 version negotiation (2) | Production | Implemented | None found | `NTCP2.cpp` SessionRequest options | Unchanged | Pending |
| NTCP2 post-quantum versions 3-5 | Optional/rolling deployment | Implemented with OpenSSL PQ; LibreSSL maps v3 to v2 | Depends on OpenSSL capability, not silently emulated | `NTCP2.cpp:2314-2320`, `PostQuantum.*` | Kept capability detection | Pending; verify against current Java |
| SSU2 handshake, tokens, relay/introduction | Production transport | Implemented | None found | `spec/ssu2`, `SSU2*.cpp` | Unchanged | Pending Java interop run |
| SSU2 fragmentation and reassembly | Production | Implemented, out-of-order queue | None found | `SSU2Session.cpp` fragment blocks | Unchanged | Pending with crafted fragments |
| SSU2 peer testing / relay tags | Production | Implemented | None found | `SSU2Session.cpp` PeerTest/Relay | Unchanged | Pending |
| SSU2 MTU discovery / IPv4/IPv6 | Production; OpenBSD-specific regressions were previously fixed in this fork | Implemented | No new regression found statically | `SSU2.cpp`, `RouterContext::SetMTU`, fork commit `ed5e6885` | No change | Pending OpenBSD live test |
| Connection migration | Specified/uses relay data | Relay/introduction implemented; no separate migration certificate | Not applicable beyond relay handling | SSU2 relay blocks | None | Pending |
| I2NP message types and flooding | Production | Implemented | None found | `spec/i2np`, `I2NPProtocol.*` | Unchanged | Pending |
| Garlic routing / tunnel messages | Production | Implemented | None found | `spec/tunnel-message`, `Garlic.cpp` | Unchanged | Pending |
| Tunnel build (short and variable) | Production | Implemented | None found | `spec/tunnel-creation`, `TunnelConfig.*` | Unchanged | Pending |
| Transit tunnel limits and expiration | Production | Implemented; new TUI exposes counts | None | `TransitTunnel.*`, `Tunnel.*` | Read-only exposure | Pending |
| RouterInfo parsing/serialization | Production | Implemented with size cap | Need malformed-input corpus | `spec/common-structures`, `RouterInfo.cpp` | No format change | Fuzz target still pending |
| LeaseSet / LeaseSet2 / EncryptedLeaseSet | Production | Implemented | None found in static review | `spec/common-structures`, `LeaseSet.cpp` | Unchanged | Pending |
| MetaLeaseSet / blinding | Production | Implemented | None found | `Blinding.cpp`, `LeaseSet2` | Unchanged | Pending |
| netDb store/lookup/floodfill | Production | Implemented | Count getters raced with writers | `spec/netdb`, `NetDb.*` | Locked counters; reseed state | Pending |
| Reseeding (SU3/ZIP, HTTPS, sig verify) | Production | Implemented with size caps | Truncated headers could use uninitialized values | `spec/su3`, `Reseed.cpp` | Checked reads and bounded allocations | Static only |
| ECIES-X25519-AEAD-Ratchet | Production | Implemented | None found | `spec/ecies`, `ECIESX25519AEADRatchetSession.*` | Unchanged | Pending vectors/interop |
| Ed25519 / Ed25519ph / RedDSA | Production | Implemented where OpenSSL supports it | Ed25519ph depends on OpenSSL 3 | `Signature.*`, `Ed25519.*` | Capability checks retained | Existing unit tests |
| ECDSA / RSA / GOST signatures | Production/legacy | Implemented | No removal; still needed for old routers | `Identity.h`, `Signature.*` | Unchanged | Existing GOST tests only |
| Hybrid post-quantum encryption | Optional, versioned | ML-KEM code paths present | LibreSSL fallback is not equivalent to PQ | `PostQuantum.*`, `Crypto.h` | No new PQ claim | Pending Java PQ test |
| Streaming library | Production | Implemented | None found | `spec/streaming`, `Streaming.*` | Unchanged | Pending |
| Datagrams v1/v2/v3, raw/repliable | Production | Implemented | None found | `spec/datagrams`, `Datagram.*` | Unchanged | Pending |
| SAM v3 | Production | Implemented | `tests/test-sam-version.sh` exists but is not wired to CTest | `spec/sam`, `SAM.cpp` | Build now uses CTest; script wiring pending | Pending |
| I2CP | Production | Implemented | None found | `spec/i2cp`, `I2CP.cpp` | Unchanged | Pending |
| BOB | Legacy but specified | Implemented | Not a Java-equivalence requirement | `BOB.cpp` | Kept | Pending |
| HTTP proxy | Not a protocol endpoint of Java router; client-facing | Implemented | Rejected `Header:value` without space; accepted malformed lengths | RFC 9110/9112; `HTTP.cpp` | **Modified** and unit-tested | Unit tests prepared |
| I2PControl | Compatible management protocol | Implemented, retained | Old web console removal does not affect it | `daemon/I2PControl.*` | Unchanged, peer count now locked | Pending |
| Web console | Java has router console | Removed by request | Not an I2P wire protocol | old `daemon/HTTPServer.cpp` | **Removed** | N/A |
| TUI administration | No Java equivalent required | New | Local-only, not a network protocol | `docs/tui-ipc.md` | **New** | Protocol unit tests prepared |
| Capability/version advertisement | Production | Implemented via RouterInfo caps and version | No change | `RouterContext`, `RouterInfo` | TUI shows caps | Pending live inspection |

## Explicit non-goals

- Reproducing the Java router's web UI, plugin format or console endpoints.
- Implementing experimental PQ or transport behavior that is not specified and
  interoperable. Existing version-gated PQ code is retained, not extended.
- Removing NTCP2/SSU2 or legacy signature algorithms without checking current
  network use; no such removal was made.

## Differences that still require a live Java test

1. NTCP2 PQ negotiation against a current Java router with and without OpenSSL
   PQ support.
2. SSU2 peer test, relay and MTU behavior on OpenBSD with non-default
   datadir/user.
3. EncryptedLeaseSet and blinded destination lookups.
4. Streaming flow control and reset behavior under packet loss.
5. SAM/I2CP interoperability for modern client features.

These are marked pending because no Java router was executed in this
environment.

<!--
See the LICENSE file at the top of the project tree for copyright
and license details.

openi2pd I2P wire review reference.
-->
# I2P wire and behavior review

Use with `SKILL.md`. This map is a starting point for static comparison; it is
not a claim of compatibility.

## Reference pin

- Product: I2P Java
- Current target: `i2p-2.14.0`
- Target commit (peeled tag): `3c98503c5f14efe7bdd783528ba7f621f12d8081`
- Tag object: `9db233ed7241599eba76f29b8c334eaaf3db2e87`
- Verification: official remote tag refs inspected with `git ls-remote`.
- Historical audit baseline: `i2p-2.13.0`, commit
  `9134f808337b401e8e53c73734c81fab04280c9d`. Existing matrices and findings
  have not been re-audited against 2.14.0; retain version-specific fixture and
  validation provenance.
- Location: external read-only checkout selected by the coordinator; never
  vendored into this repository or downloaded by a build.
- Source of truth for open work: `docs/INTEROPERABILITY.md` and
  `docs/FEATURE-PARITY.md`.

## Surface map

| Surface | C++ | Java reference area | Fixtures / tests |
|---|---|---|---|
| NTCP2 handshake/data | `libi2pd/NTCP2.*` | `router/transport/ntcp2` | `test-ntcp2-*`; `tests/interop/vectors` |
| SSU2 handshake/data | `libi2pd/SSU2*`, `SSU2Session.h` | `router/transport/ssu2` | `test-ssu2-*` |
| I2NP | `I2NPProtocol.*`; `test-i2np-database-bounds`, `fuzz-i2np-database` | `router/I2NP*` | `test-protocol-bounds`, interop I2NP vectors |
| RouterInfo/LeaseSet | `RouterInfo.*`, `LeaseSet.*` | `data/RouterInfo`, `data/LeaseSet*` | `test-routerinfo-*`, `test-leaseset*` |
| netDb | `NetDb.*`, `NetDbRequests.*`, `KadDHT.*` | `router/database`, `router/netdb` | `test-netdb-*`, `test-kad-dht` |
| SAM | `libi2pd_client/SAM.*` | `client/sam` | `test-sam-*`, SAM vectors |
| I2CP | `libi2pd_client/I2CP.*` | `client/I2CP*` | I2CP vectors, `test-addressbook` |
| Streaming | `libi2pd/Streaming.*` | `client/streaming` | `test-streaming-*` |
| Datagram | `libi2pd/Datagram.*` | `client/datagram` | `test-datagram-*` |
| Tunnels/Garlic | `Tunnel*`, `Garlic.*`, `TransitTunnel.*` | `router/tunnel`, `router/garlic` | tunnel/garlic tests |
| Crypto/ratchet | `ECIESX25519AEADRatchetSession.*`, `Signature.*` | `crypto`, `router/crypto` | crypto/ratchet tests |

## Differential checklist

1. Capture the exact tag/commit and Java method used for comparison.
2. Record the C++ function and line, not only a file name.
3. Compare framing: magic/version/type, length units, endianness, checksums.
4. Compare required and optional fields, defaults and unknown-field behavior.
5. Compare limits: maximum message, field, count, nesting, time window.
6. Compare validation order and side effects before/after validation.
7. Compare signature/identity binding and key-selection rules.
8. Compare retry/replay/expiry/correlation behavior.
9. Compare error categories and whether the peer should close, reject or ignore.
10. Record a verdict: equal, deliberate divergence, unsupported, or pending.

## Fixture rules

- Valid and malformed cases are deterministic and checked in.
- Each case has a `.hex` payload and `.case.json` metadata.
- Provenance states whether a case is hand-built, derived from a specification
  or generated from a reference run.
- Never place a private key, live peer address or public-network capture in a
  fixture.
- A fixture that exercises a parser must assert the exact rejection/acceptance
  boundary, not only a generic failure.

## Dynamic gate

A controlled run needs a Java router on an isolated host/namespace, explicit
peer exchange and no public-network dependency. It should cover both transport
directions, tunnel builds, LeaseSet2/EncryptedLeaseSet, SSU2 peer test/relay,
IPv6/MTU and LibreSSL/no-PQ/PQ combinations where applicable. This run is
manual CI evidence, not something a reconnaissance subagent may execute.

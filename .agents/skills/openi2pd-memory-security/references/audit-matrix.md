<!--
See the LICENSE file at the top of the project tree for copyright
and license details.

openi2pd security audit matrix.
-->
# Memory and security audit matrix

Use with `SKILL.md`. This is a static review map; it does not assert that any
row is currently safe or dynamically validated.

| Surface | Main files | Invariants to check | Prepared regression / lane |
|---|---|---|---|
| NTCP2 | `libi2pd/NTCP2.*` | handshake frame bounds, block ordering, replay windows, pending-handshake limits, padding, data-phase header | `test-ntcp2-handshake-bounds`, `test-ntcp2-session-confirmed-payload`; `memory`, `security` |
| SSU2 | `libi2pd/SSU2*`, `SSU2Session.h` | token/PeerTest parsing, fragment reassembly, ACK/retry state, MTU/address limits, PathChallenge state | `test-ssu2-*`, `test-protocol-bounds`; `memory`, `security` |
| I2NP/netDb | `I2NPProtocol.*`, `NetDb.*`, `NetDbRequests.*` | length-prefixed messages, DatabaseLookup/SearchReply correlation, pending caps, store replacement | `test-netdb-*`, `test-protocol-bounds`; `memory`, `security` |
| RouterInfo/LeaseSet | `RouterInfo.*`, `LeaseSet.*`, `LeaseSet.h` | 4096-byte RI limit, option/address sections, signature scope, lease freshness, Meta/Encrypted outer layers | `test-routerinfo-*`, `test-leaseset*`; `memory`, `security` |
| Router profiles | `Profiling.h/.cpp` | per-instance state lock, canonical publication, endpoint copies, map-only eviction, dirty persistence claims, IO/state lock order; immutable root, process-local IO/cleanup serialization, atomic replacement; cross-process ownership remains open | `test-router-profile-race`, `test-router-profile-storage`; `memory`, `security`, manual TSan |
| Ratchet/Garlic | `ECIESX25519AEADRatchetSession.*`, `Garlic.*`, `Tag.h` | tag/replay budgets, NS/NSR pre-validation, clove requirements, no key/nonce reuse | `test-eciesx25519-*`, `test-crypto-audit-bounds`; `memory`, `security` |
| Signatures/PQ | `Signature.*`, `PostQuantum.*`, `Identity.*` | exact key/certificate lengths, algorithm gating, fail-closed unsupported keys, constant-time verification | `test-signature-*`, `test-crypto-*`; `security` |
| Reseed | `Reseed.*` | SU3/RSA verification default, zip bounds, no unauthenticated install | `test-reseed-zip-bounds`; `security` |
| Client protocols | `libi2pd_client/SAM.*`, `I2CP.*`, `I2PTunnelHTTP.h`, `AddressBook.*`, `libi2pd/Streaming.*`, `libi2pd/Datagram.*`, `HTTPProxy.*` | framing/pipelining, independent HTTP request/response output lifetime, bounded incremental headers, resolver identity/nonce/expiry binding, version gates, buffered-byte limits | `test-sam-*`, `test-datagram-*`, `test-streaming-*`, `test-http-*`, `test-i2ptunnel-http`, `test-addressbook-case`, `fuzz-http-parser`; `security`, client hardening tests also `memory` |
| BOB / I2PControl | `libi2pd_client/BOB.*`, `daemon/I2PControl.*` | no credential operands in logs, weak callback ownership, stopped-state rearm guard, bounded TLS admission/tokens, async header/body deadlines, atomic private PEM replacement | `control-security-static`; `memory`, `security`; manual TLS slow-client and ASan/TSan lifecycle |
| Torrent | `Torrents*`, `TorrentsDHT.*`, `TorrentsComments.*` | bencode nesting/counts, info-hash binding, path containment, peer caps, token/ID binding, privacy suppression | `test-torrent*`, `test-torrents-*`; `memory`, `security`, `torrent` |
| IPC/privsep | `privsep/*`, `daemon/*` | frame size/type/id, peer credentials, descriptor passing, queue pressure, restart/lifecycle | `test-privsep-*`; `ipc`, `security`, `platform` (not every target carries `memory`) |
| Transport lifecycle | `Transports.*`, `TransportConnector.h` | Start/Stop ordering, timer/server/peer lifetime, connector non-owning context, reserved/priority rules | `test-transport-connector`, `test-runnableservice-lifecycle`; `unit` |
| Cooperative cancellation | `daemon/TuiServer.cpp`, `Transports.*` | stop token reaches every bounded wait, `request_stop` before `join`, no detached joinable worker except documented self-stop path | `test-modern`, TUI/transport lifecycle lanes; sanitizer pending |
| Logging | `libi2pd/Log.*` | 8192-byte text cap, 8192-record queue, 512-record recent ring, counted drops, no control-character injection | `test-log-lifecycle`, `fuzz-log-record`; `memory`, `security` |
| TUI protocol | `libi2pd/TuiProtocol.h`, `daemon/TuiServer*`, `tui/*` | bounded requests, typed admin queue, no raw paths/pointers, peer authorization | `test-tui-*`, `fuzz-tui-protocol`; `tui`, `ipc`, `fuzz` |
| Android JNI/SAF | `android/app/src/main/cpp/*`, `I2PD_JNI.java`, SAF broker/AIDL | worker-only JNI, bounded snapshots, URI authority, generation fencing, provider failure, no URI-to-path fallback | Android unit/instrumentation + native tests; CI device lane pending |

## Finding template

Record:

- ID and severity;
- trust boundary and input source;
- exact file/function/line;
- preconditions and failure mode;
- fix or planned fix;
- regression test and sanitizer/fuzz lane;
- residual risk and whether a spec change is required.

## Review sequence

1. Trace input length and ownership from ingress to use.
2. Check arithmetic before allocation/indexing, then check error propagation.
3. Check side effects and partial state before validation.
4. Check concurrent access and shutdown.
5. Check secret/key lifetime and logging.
6. Check the negative path: reject, close, stop, or fail closed.
7. Check tests assert the negative behavior, not only the happy path.

## CI notes

- ASan+UBSan and TSan are separate manual jobs; do not assume one covers the
  other.
- Fuzz smoke is not proof of memory safety; the workflow runs bounded inputs.
- A compile-only result is not a security validation. Keep the task at
  `Pending CI tests` until the relevant lane passes.

CONC-011 uses `test-datagram-receiver-lifecycle` for registration identity,
external drain and reentrant/cross-slot reset. Callback-context reset returns
false and is not a destruction barrier. CONC-012 uses `test-tunnels-restart`
and `test-netdb-stop-admission` for queue closure/drain; the latter fixture
opens admission with a paused worker, without network/storage startup.
ANDROID-019 uses `ServiceOwnerEpochTest` for stale callbacks/submissions,
retirement and accepted-work/stop/new-owner FIFO order. All require manual CI.

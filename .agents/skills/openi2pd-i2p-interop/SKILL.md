---
name: openi2pd-i2p-interop
description: Use when checking or changing I2P wire behavior against the pinned I2P Java 2.14.0 target, including NTCP2, SSU2, I2NP, netDb, RouterInfo, LeaseSet, SAM, I2CP, Streaming and Datagram parity.
---

## Maintenance & Continuous Learning

When this skill guides a repository change, review it after implementation. Update procedures, tools, configurations, assumptions, examples and validation steps when they changed or when reusable knowledge improves future work; correct inaccurate guidance and omit one-off execution details. Report synchronization in the task summary. Follow the normative process and Definition of Done in [AGENTS.md](../../../AGENTS.md#continuous-agent--skills-maintenance).
<!--
See the LICENSE file at the top of the project tree for copyright
and license details.
-->

# openi2pd I2P interoperability

## Activate when

Use this skill for protocol serialization, framing, negotiation, timestamps,
signature scope, replay rules or client behavior that must match I2P Java.
Combine it with the transport/client implementation skill and with
`openi2pd-cryptography` when keys, signatures or ratchets are involved.

Read [`../../../AGENTS.md`](../../../AGENTS.md) first. The current comparison
target is
I2P Java `i2p-2.14.0`, peeled commit
`3c98503c5f14efe7bdd783528ba7f621f12d8081` (tag object
`9db233ed7241599eba76f29b8c334eaaf3db2e87`), verified from the official
remote tag refs. It is external and read-only. Existing audit matrices use
historical 2.13.0 evidence and are pending a 2.14.0 source re-audit; do not
relabel old findings, fixtures or runs as 2.14.0 evidence.

## Inspect first

- The exact C++ parser/serializer and all of its call sites.
- The matching Java class/function in the read-only reference checkout, if it
  is available to the coordinator.
- [`../../../docs/INTEROPERABILITY.md`](../../../docs/INTEROPERABILITY.md) and
  [`../../../docs/FEATURE-PARITY.md`](../../../docs/FEATURE-PARITY.md), including
  residual gaps.
- Deterministic fixtures in `tests/interop/` and their provenance notes.
- The relevant protocol document in
  [`../../../docs/protocols/README.md`](../../../docs/protocols/README.md).

## Reference sources

- [`../../../docs/INTEROPERABILITY.md`](../../../docs/INTEROPERABILITY.md) —
  Java class/function references, confirmed findings and open work.
- [`../../../docs/FEATURE-PARITY.md`](../../../docs/FEATURE-PARITY.md) —
  functional matrices for router, crypto, client services, torrents and
  administration.
- [`../../../tests/interop/README.md`](../../../tests/interop/README.md) and
  [`../../../tests/interop/FIXTURES.md`](../../../tests/interop/FIXTURES.md) —
  harness and fixture rules.
- [`references/wire-review.md`](references/wire-review.md) — protocol matrix
  and differential-test checklist.

## Reconnaissance

Launch at least one read-only `explore` subagent for the C++ path and one for
the matching Java reference and existing fixtures. If subagents are unavailable,
report that limitation; do not substitute local execution. Ask for:

1. the Java source path, tag/commit and relevant method;
2. the C++ parser/serializer, error behavior and callers;
3. a byte-level difference list;
4. existing valid/malformed fixtures and missing cases.

Never vendor Java source, copy GPL implementation text, contact a public I2P
peer or add a hidden external checkout. The reference is for reading only.

## Parallel implementation

- Split by independent protocol/message family. Do not edit the same parser,
  serializer or shared header from two workstreams.
- One owner updates the compatibility matrix per finding; do not let multiple
  agents append conflicting rows. Shared parser/serializer headers,
  `tests/interop/` fixtures and matrix rows require coordinator review.
- Keep the C++ implementation independent. Do not translate Java source
  wholesale; implement from the specification and observed behavior.
- Protocol changes that alter a wire format require a specification reference,
  compatibility rationale and regression before merge.

## Conventions

- Apply the [source documentation policy](../../../AGENTS.md#source-documentation-policy)
  to wire comments; cite the relevant specification and distinguish static
  interpretation from tested interoperability.

- Preserve exact byte layout, field order, endianness and signature scope.
- Reject malformed/truncated input before state mutation or allocation.
- Preserve unknown-field handling and version negotiation deliberately; do not
  silently accept an unsupported version.
- Distinguish “Java-compatible by static comparison” from “dynamically
  interoperable”. The latter requires a controlled peer run.
- Keep test vectors deterministic and provenance-documented; never use live
  network captures as the only evidence.
- Do not copy Java architecture, threading or accidental behavior unless a
  protocol requirement justifies it.

## Known issues and decisions

- LeaseSet2 encryption selection uses `IsSelectableLS2EncryptionKeyType`:
  ElGamal and ECIES-X25519 are universal, EC_P256/P384/P521 are never
  selectable, and hybrid types require the runtime ML-KEM probe. `i2cp.leaseSetEncType`
  and the preferred type must name a key actually installed.
- SSU2 introducer selection and HolePunch verification read
  `SSUExt::GetIntroducers()`; the netDb thread prunes the live vector under the
  same `SSUExt::mutex`, so never iterate or index `address->ssu->introducers`
  directly in transport code. Local add/remove/update and `RouterContext`
  clears take that lock. Wire fields (`itag`/`ih`/`iexp`) are unchanged.
- The comparison target is I2P Java 2.14.0
  (`3c98503c5f14efe7bdd783528ba7f621f12d8081`); 2.13.0 citations and fixtures
  are historical and must keep that provenance.
- Outbound transport priority is a Strategy in `TransportConnector.h`:
  `false` advances to the next transport, but a pending SSU2 endpoint reports
  `true` to preserve Java's one-establishment-state behavior.
- Direct-only DatabaseStore/DatabaseLookup policy depends on preserved clove
  origin; never relabel tunnel-delivered messages as direct.
- `test-transport-connector` checks only static mapping/short-circuit behavior;
  live NTCP2/SSU2, PeerTest, tunnel and Java-peer evidence remain pending.
  Socket-free negative matrices (`test-ssu2-payload-blocks`,
  `test-i2np-database-bounds`, `fuzz-i2np-database`) are preferred over a
  fixture-only check for parser boundaries; they do not prove live
  interoperability.
- Streaming admission follows Java `ConnThrottler`: `StreamingConnectionThrottle`
  keys windows by remote static key and checks day, hour, minute per peer and
  in total. `limitAction=drop` and `limitAction=reset` are implemented
  (`test-streaming-limitaction`); `http`/custom responses and the Java
  differential run remain open. Throttled SYNs otherwise drop, so this is not
  full equivalence. SAM 3.3 `LISTEN_PROTOCOL`
  routing and local HTTP Basic/SOCKS5 RFC 1929 auth are implemented statically;
  HTTP Digest and the Java differential run remain pending.

- Blocklist grammar is the Java text format with documented divergences:
  hostnames are skipped (no DNS), only the `;` IPv6 form is accepted because
  the last `:` separates the reason, and IPv6 ranges/CIDR plus GeoIP countries
  are unsupported. The file is read once before netDb loads cached RouterInfos.
- NetDb lookup budget: 14 replies per 120 s reply tunnel when floodfill, 3
  otherwise; a non-floodfill drops EXPL and ANY before the throttle is charged.
- Tracker accounting matches Java `TrackerClient`: `left` is the whole-torrent
  remainder (phony 1 without metadata) and `event=completed` requires every
  piece to be verified, while local Seeding/promotion remain selection-based.
  Advertised intervals are floored at 15 minutes and raised by a larger
  `min interval`; the internal failure ladder is capped at 30 minutes.
- Address-book merges keep pre-existing destinations (Java `Daemon.java`);
  last-wins applies only to duplicate lines inside one source.
- Address-book feed origin is not command authenticity. Proposal 112 requires
  canonical `sig`/`oldsig` verification for signed actions; the current native
  `action=delete` path checks source ownership but has no signature verifier.
  Keep the local origin restriction and do not add signed actions until their
  exact preimages, authority transitions, replay behavior and Java vectors are
  verified.
- Java floodfill configuration is a runtime policy: `router.floodfillParticipant`
  accepts `true`, `false` and `auto`, defaults to `auto`, and hidden mode
  overrides it. The local false-default boolean is not an equivalent alias;
  implementing the monitor is a separate policy task.
- Durable magnets, eight-slot upload admission and layered torrent bandwidth
  queues are Java I2PSnark behavior (`SnarkManager`, `PeerCoordinator`,
  `BandwidthManager`), not consequences of tracker-wire parity. Track restart,
  admission and fairness separately from protocol serialization.

- NTCP2 padding has **two** columns: SessionRequest 880/1264/1648 and
  SessionCreated 848/1168/1648 for v2/v3/v4/v5; v5 requires
  `1648 + 1648 == 3296`. The old 1136/1616 values were an "Enc len" column
  misread; do not restore them or collapse the two columns.
- NTCP2 data-phase AEAD failure is a probing-resistance contract: drain
  18..1041 random bytes with a 10 s deadline, then reason 4; a stalled frame
  sends reason 14 at 30 s. Clock skew gets SessionCreated first and reason 7
  only after SessionConfirmed. A foreign netID is reason 17 plus a ban, not a
  signature failure.
- SSU2 HolePunch/Introduced accept only DateTime, Address, RelayResponse,
  Padding and Termination. PeerTest msg 5 never publishes an address; msg 7 is
  corroborated against the Charlie learned in msg 4; msg 4→6 targets pass
  endpoint/reserved/blocklist checks. PeerTest/HolePunch accept `ver` 2..4.
- SSU2 `Established()` in `SessionConfirmedSent` requires an ACK covering packet
  0; a version-rejection Retry with token 0 must request a fresh token so the
  PQ fallback stays reachable. RouterInfo blocks require the single-fragment
  byte 0x01.

- Ratchet NSR selection follows Java `RatchetSKM`: the first inbound tagset
  that receives a tag selects the outbound split; a lost latest NSR must not
  break Bob→Alice traffic. NSR payloads carry no Termination/NEXT_KEY/ACK.
- RouterInfo RSA-signed identities are rejected on the netDb path (Java
  `DatabaseEntry` DoS policy); `i2cp.leaseSetEncType` accepts exact decimal
  tokens only and rejects multiple hybrid or ElGamal+hybrid combinations;
  unknown LS2 key sections are retained inert.
- b33 two-byte signature-type flags are rejected like Java 2.14.0. The
  provider-free Ed25519 verifier rejects non-canonical `S`; the live path is
  OpenSSL.
- SSU2 PQ fallback is authorised only by the signed RouterInfo advertisement;
  NTCP2 prefers the negotiated hybrid version without random classic sessions.
- Reseed `netid` (Proposal 147) is appended to SU3/directory URLs and server
  selection uses Java's two-server/100-RouterInfo acceptance threshold;
  `test-reseed-netid-url` pins URL composition. Do not reintroduce a
  single-server acceptance or a cleartext fallback.

- Garlic cloves carry two origin bits: `fromClove` and `fromTunnel`. A clove of
  a directly received garlic may carry a DatabaseLookup and keeps a
  DatabaseStore reply token; a clove of a tunnel-delivered garlic drops
  lookups and stores without token/ack/relay. Never collapse the distinction.
- A MetaLeaseSet is valid only with zero revocations and header `expires` equal
  to the latest entry: Java re-serialises that canonical form before verifying.
  Do not restore the old one-way clamp.
- Reply-key 0x12 is parse-only and fail-closed: Java 2.14.0 has no sender/KDF,
  reads it into an unused key and answers cleartext; the spec derived form is
  TBD. Do not invent a KDF, and do not answer cleartext.
- Transport request-flood RIs are forwarded only by a floodfill and within the
  3/90 s per-key budget. netDb uses deterministic 1024/1000/4000 caps, not
  Java's probabilistic pdrop.
- A RouterInfo cache file is trusted only when `routerInfo-<base64>.dat` matches
  the parsed identity; `.dat.tmp-*` staging and mismatched names are removed.

- `PlanTunnelBuildRecords()` is the only authority for the hop↔record mapping;
  a path it rejects fails the build before a byte is written. Inbound reserves
  the phony slot (`MAX_INBOUND_HOPS`). `CreateBuildRequestRecord()` returns
  `bool` and zeroes the whole record on any failure.
- A transit build request is deduplicated by its derived reply key (bounded
  8-minute/4096-entry decaying table, key never logged). Relayed requests carry
  Java's ~25 s expiration and replies ~60 s, not the generic 8 s; build payload
  sizes are exact.
- The dispatcher applies the Java `MessageValidator` window (90 s past, 240 s
  future) before the type switch and exempts `TunnelData` (endpoint IV filter).
  Duplicate suppression is the `DecayingHashSet` msgID+expiration-second+salt
  form; cloves are excluded because each destination has its own salted filter.
- A clove payload announces 1..32 cloves. A stale or foreign clove is skipped
  and the walk continues; only broken framing stops it. The tunnel-only clove
  allowlist applies to `fromTunnel` garlic; a direct garlic additionally allows
  `TunnelTest`. ROUTER-mode ratchet cloves are rejected, never forwarded in
  cleartext.
- Reuse and fresh selection share `IsRouterUsableForTunnel()`; a failed or
  test-struck path is never recreated. Tunnel tests use a 7.5–15 s per-path
  deadline and three consecutive strikes, and `TestFailed` is never published.
- Java B1/B2/B3 payload-size normalisation is not implemented; only the
  1912-byte target padding exists. Do not approximate the buckets without the
  reference boundaries.

- SAM `PORT` is the local UDP forward socket; the I2CP receive port is
  `LISTEN_PORT` (default 0/PORT_ANY). Use `ResolveSAMSessionListenPort()` and
  `SAMStyleListenProtocol()` (17/19/20 for DATAGRAM/DATAGRAM2/DATAGRAM3) so a
  subsession is keyed by `(STYLE, protocol, LISTEN_PORT)`. Never register a
  receiver on the forward port or key a non-RAW subsession on protocol 0.
- An established SAM control connection is never closed for idleness: Java
  calls `setSoTimeout(0)` after HELLO. Only HELLO (60 s) and a partially
  received command line are bounded. `STREAM ACCEPT` and `STREAM FORWARD` are
  mutually exclusive per port (`ALREADY_ACCEPTING`); a queued ACCEPT must be
  re-armed after the armed one is torn down.
- SAM version bounds are compared numerically; an unparsable/empty MIN/MAX is
  `NOVERSION`, not `I2P_ERROR` (which is reserved for a line that is not
  `HELLO VERSION` or a duplicate parameter).
- `.b33.i2p`/implicit long-label names are blinded destinations
  (`AddressBook::GetAddress`), not address-book keys. A type-42 BlindingInfo is
  keyed by the unblinded signing key and its 32-byte DH private key or PSK is
  passed to `RequestDestinationWithEncryptedLeaseSet`; HostReply uses Java's
  1/2/3/4/5/6 codes. Keys/secrets are never truncated or logged.
- Datagram framing is per send (`eDatagramDefault` keeps the session default);
  receivers are keyed by `(protocol, port)` with 0 as the explicit catch-all
  and no port-to-wildcard promotion. Raw receivers refuse 6/17/19/20 and a
  0xff protocol byte is 0.
- Streaming: a retransmitted SYN for a closed stream is dropped by the bounded
  recently-closed set and answered with one RESET; over-limit/throttled/backlog
  SYNs are RESET; no data after CLOSE/RESET. TIME-WAIT/FIN retention and
  SYNACK-before-LeaseSet ordering remain open divergences.
- CLS1 (CreateLeaseSet) is DSA-only and fail-closed; do not guess per-type
  private-key offsets. CLS2 follows the `i2cp.leaseSetEncType` order and
  compares derived keys.

- HTTP proxy pages/logs never echo a client `Host`/path or the configured
  outproxy URL (it can carry `user:pass@`). Only `http`/CONNECT-`https` are
  proxied; other schemes get 403 before resolution. Proxied responses use the
  client-response mode to strip hop-by-hop and disclosure headers. Digest is
  opt-in MD5/SHA-256 with a bounded nonce cache; Basic remains the default.
- A stopped server tunnel must close its live streams/sockets and be
  restartable (`I2PServiceHandler::Terminate` is virtual; `Stop(bool forced)`).
  `filterDefinition` is evaluated by the streaming admission predicate before a
  stream or SYNACK exists; `drop` is the filter default and `limitAction=reset`
  opts into one signed RESET. `targetForPort.<port>` selects the upstream by
  accepted local port (443/22 without a mapping is reset); only port 0 or
  `defaultacceptor=true` owns the destination-wide acceptor.
- Reseed never deletes cached peers from a bundle: stale entries are skipped
  and counted. Administrative reseed is a cancellable job with per-phase and
  per-host deadlines (`RequestReseed`/`ReseedState`); `reseed.disable`/
  `i2p.noReseed` disable it, and `reseed.proxy` credentials are never logged.
- `ADDRESS-ADD` never re-points an existing name (`ADDRESS-REPLACE` is
  explicit). A subscription `#!action=delete` retracts only names whose
  recorded origin matches that source, via the bounded `entry-sources.txt`
  sidecar. b33 names are never negatively cached. All address-book artifacts
  use the atomic no-follow writer.
- BOB is loopback-only legacy: 16-session cap, 4 KiB command lines,
  `getkeys` requires `bob.allowgetkeys`, no operand logging. The I2PControl
  AF_UNIX path is owner/type-checked, 0600, removed only while it is this
  process's socket and requires a same-UID peer. NTP needs three agreeing
  servers for a large jump; UPnP uses a 3 h renewable lease and refuses a
  same-host IGD.

## Do not

- Do not compile, run CTest, interop scripts, Java peers or project validators
  locally.
- Do not touch the public I2P network from tests or examples.
- Do not change I2P wire/persisted formats without a specification reference
  and migration/compatibility test.
- Do not claim “fully compatible” from a fixture pass or a source comparison.
- Do not create commits, tags, releases, worktrees or temporary commits, and do not push; inspect `git status`/`git diff` before editing.
- Do not install dependencies, fetch/vendor code or perform hidden downloads to enable a local build or test. Preserve LICENSE/NOTICE/provenance and technical identifiers/paths unless an approved migration exists (AGENTS.md).

## Prepare for CI

- Add valid and malformed vectors under `tests/interop/vectors/` with `.hex`
  and `.case.json` provenance.
- Register or extend the `interop-static` check; use the pinned-reference job
  only when a read-only checkout is supplied.
- Add focused C++ tests under `tests/` with `i2p`/`interop`/`security` labels.
- Prepare a controlled Java/C++ differential procedure without public-network
  access; retain its evidence separately from static review.
- Do not mark an interoperability row validated until the manual job passes.

## Tracking

Use the `NET`/A or client area, plus `PARITY-*` rows where applicable. Record
Java reference, C++ files, difference verdict, fix, fixtures and pending
dynamic validation. Propose a dated `PROGRESS.md` entry and any `TODO.md`/`ROADMAP.md` changes to the coordinator; append them only when explicitly assigned. Subagents report
proposed tracking rows to the coordinator.

Record the exact AGENTS.md `Tracking states` value and state which validations were not run; do not collapse them into “pending”.

## References

- [Wire review and differential matrix](references/wire-review.md)
- [Interoperability audit](../../../docs/INTEROPERABILITY.md)
- [Feature parity](../../../docs/FEATURE-PARITY.md)
- [Global policy](../../../AGENTS.md)

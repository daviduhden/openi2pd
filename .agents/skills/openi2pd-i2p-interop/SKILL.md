---
name: openi2pd-i2p-interop
description: Use when checking or changing I2P wire behavior against the pinned I2P Java 2.14.0 target, including NTCP2, SSU2, I2NP, netDb, RouterInfo, LeaseSet, SAM, I2CP, Streaming and Datagram parity.
---
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

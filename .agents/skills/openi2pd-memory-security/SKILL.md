---
name: openi2pd-memory-security
description: Use when auditing or changing untrusted-input parsers, concurrency, object lifetime, resource bounds or security-sensitive behavior in openi2pd, and when preparing sanitizer or fuzz regressions.
---
<!--
See the LICENSE file at the top of the project tree for copyright
and license details.
-->

# openi2pd memory and security

## Activate when

Use this skill for protocol-bounds review, integer/allocation hardening,
concurrency and lifetime analysis, shutdown races, descriptor ownership, secret
wiping or a security regression. It is a review-and-correction skill: when the
fix belongs to another subsystem, prepare the finding and regression there and
coordinate the production edit with that owner.

Read [`../../../AGENTS.md`](../../../AGENTS.md) first. Never weaken a security
control to make a test or build pass.

## Inspect first

- The parser or concurrent component and all of its callers, not only the
  function named in a report.
- `tests/test-*-bounds.cpp`, `tests/test-crypto-audit-bounds.cpp`,
  `tests/test-*-race.cpp`, lifecycle tests and `tests/fuzz/`.
- The relevant section of
  [`../../../docs/security/memory-safety-2026.md`](../../../docs/security/memory-safety-2026.md),
  [`../../../docs/security/threat-model.md`](../../../docs/security/threat-model.md)
  and [`../../../docs/security/README.md`](../../../docs/security/README.md).
- The subsystem's own protocol or storage document before changing a format.

## Reference sources

- [`../../../docs/security/memory-safety-2026.md`](../../../docs/security/memory-safety-2026.md)
  — audited surfaces, confirmed findings and validation status.
- [`../../../docs/security/threat-model.md`](../../../docs/security/threat-model.md)
  — assets, adversaries and out-of-scope limits.
- [`../../../docs/security/privilege-separation.md`](../../../docs/security/privilege-separation.md)
  — capability boundaries that a fix must not bypass.
- [`../../../docs/testing.md`](../../../docs/testing.md) — labels, sanitizer
  lanes and fuzz inventory.
- [`references/audit-matrix.md`](references/audit-matrix.md) — high-risk
  surfaces, invariants and regression names.

## Reconnaissance

Launch at least one read-only `explore` subagent for the target surface and a
second for tests and callers. Ask for:

1. Every path from untrusted bytes, a remote peer, a local IPC peer or a
   provider document to allocation, indexing, arithmetic and persistence.
2. Lock order, ownership, callback capture, thread affinity and shutdown
   ordering.
3. Existing bounds constants, error categories and fail-open/fail-closed
   branches.
4. Tests that already cover the path and gaps that need a focused regression.

Do not let reconnaissance agents edit, compile, execute tests or run scripts.

## Parallel implementation

- Security findings may be split by independent parser or component. Do not
  edit the same file from two workstreams.
- Hand production edits in `libi2pd/`, `libi2pd_client/`, `privsep/` or
  `android/` to the owning skill; this skill can own the corresponding
  regression and review.
- Keep a finding's test, code fix and documentation status in one reviewable
  slice.
- Escalate any change to trust boundaries, key handling, `pledge`/`unveil`,
  seccomp/Landlock, DACLs or authentication to the coordinator.

## Conventions

- Validate lengths, counts and arithmetic before allocation or indexing.
- Reject truncated, contradictory and oversized input explicitly; never treat
  corruption as an empty optional or silently fall back to a weaker path.
- Prefer bounded spans and checked conversions. Avoid narrowing casts that
  discard high bits.
- Use the project's existing constant-time comparison and signature/AEAD
  verification paths; do not invent a replacement.
- Establish and document lock order. Prefer immutable snapshots for data read
  by another thread; do not add unsynchronised container reads.
- Release descriptors, child processes, timers and allocations on every error
  and shutdown path. Wipe secrets with the existing helpers.
- Document a residual risk when a complete fix is not justified; do not claim
  the audit is complete.

## Do not

- Do not compile, run CTest, sanitizers, fuzzers, Gradle or project validators
  locally.
- Do not weaken bounds, signature/AEAD checks, `pledge`/`unveil`, sandbox
  policy, credential checks or resource limits.
- Do not add `continue-on-error`, ignore a sanitizer finding or delete a test
  to obtain a green result.
- Do not ship a new wire or persisted format without a specification,
  migration and compatibility test.
- Do not commit, push, tag or release.

## Prepare for CI

- Add deterministic regressions under `tests/` and register them with
  `memory` and/or `security` labels.
- Extend an existing fuzzer or add one under `tests/fuzz/` when the parser
  surface is not covered; register a one-input smoke target with the `fuzz`
  label.
- Identify the ASan+UBSan, TSan and fuzz jobs required for the changed path.
- Record expected failure behavior (reject, close channel, stop worker) in the
  test name or assertion, not only in prose.
- Distinguish static implementation from pending CI compilation/tests.

## Tracking

Use the security area (`SEC`/C) and the existing `MEM-*`, `NET-*`, `CRY-*`,
`PRIVSEP-*` or subsystem IDs. Record the exact finding, affected files,
regression, sanitizer/fuzz lane and residual risk. Append a dated
`PROGRESS.md` entry. Subagents report proposed tracking rows to the coordinator.

## References

- [Security audit matrix](references/audit-matrix.md)
- [Global policy](../../../AGENTS.md)
- [Memory-safety audit](../../../docs/security/memory-safety-2026.md)

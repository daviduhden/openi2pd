---
name: openi2pd-memory-security
description: Use when auditing untrusted-input parsers, concurrency, object lifetime, resource bounds or security-sensitive behavior in openi2pd, excluding general C++23 ownership refactors and cryptographic primitive selection.
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
second for tests and callers. If subagents are unavailable, report that
limitation; do not substitute local execution. Ask for:

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
- Treat logs and error strings as persistence boundaries. Never include a
  password, private key, seed, token or complete credential-bearing operand;
  prepare a marker-based negative regression for every corrected leak.
- For event-loop services, audit admission count, handshake/header/body/write
  deadlines and every synchronous operation. A byte limit does not prevent a
  slow-client denial of service.
- Persist secrets through a single atomic private-file primitive. Pathname
  chmod followed by reopen is a symlink/TOCTOU defect, not a permission fix.
- Document a residual risk when a complete fix is not justified; do not claim
  the audit is complete.

## Known issues and decisions

- BOB tunnel completions use weak derived ownership plus an atomic stopped
  state, a pending-receiver registry and a short admission mutex shared with
  handler publication; `STOP`/`CLEAR` may otherwise release the last owner or
  publish work after draining. I2PControl uses bounded admission, per-operation
  deadlines, asynchronous bounded HTTP reads, capped tokens/authentication and
  atomic PEM replacement. `control-security-static` pins these seams; runtime
  TLS slow-client and BOB teardown coverage remains pending manual CI.

- CONC-011 registration tokens prevent stale teardown from removing another
  Datagram owner. External reset drains outside registry locks; callback reset
  returns false without waiting to avoid callback cycles. Do not destroy raw
  callback targets on that result. Per-caller ASan/TSan coverage remains pending.


- Queue admission checks must share a lock with enqueue and shutdown close;
  checking an atomic flag before `Put` alone leaves a post-after-drain race.
  Tunnel/netDb lifecycle locks remain separate from the short admission locks.
  Producers must stop across restart; no generation is carried in wire queues.
- Profile expiry only owns canonical encoded-hash filenames at their expected
  shard and regular non-symlink leaves. Preserve unknown/temp/backup leaves;
  process-local locking does not protect a hostile concurrent filesystem owner.


- CONC-010 uses immutable process-local profile-root authority and flushed
  same-directory replacement; storage IO/cleanup is serialized and shutdown
  drains postponed updates after producers stop. Cross-process ownership,
  abandoned staging cleanup and retained external mutators remain open.
  `test-router-profile-storage` is prepared, not executed.


- Profile lock order is map -> instance state, or instance IO -> state;
  instance methods never acquire the map mutex. The RNG mutex is released
  before taking profile state. Cache eviction requires sole map ownership;
  failed persistence must restore dirty state. `test-router-profile-race`
  covers object/publication/queue contracts; storage races use
  `test-router-profile-storage`.
- `Transports::Stop()` ordering is load-bearing: join the worker, cancel
  timers, stop servers, clear peers while both server `io_context`s are alive,
  then reset servers. Cross-thread `IsBound*` reads must use the `m_Has*`
  atomics, never `unique_ptr != nullptr`.
- `jthread` RAII does not close descriptors or unlink paths. `TuiServer` must
  `request_stop()` then `join()` before closing/unlinking, and all waits must
  be bounded; the Windows TUI still uses its own thread/stop event.
- Log text, writer queue and recent ring are independently bounded. Queue drops
  are counted, and the recent ring may contain a record the writer dropped.
- Cooperative cancellation, complete Android/Datagram caller lifecycle
  verification and the `Transports` detach fallback remain residual areas; see
  [`references/audit-matrix.md`](references/audit-matrix.md).
- Prepared seam regressions are `unit`-labelled; do not claim ASan/TSan or
  runtime coverage from them.

## Do not

- Do not compile, run CTest, sanitizers, fuzzers, Gradle or project validators
  locally.
- Do not weaken bounds, signature/AEAD checks, `pledge`/`unveil`, sandbox
  policy, credential checks or resource limits.
- Do not add `continue-on-error`, ignore a sanitizer finding or delete a test
  to obtain a green result.
- Do not ship a new wire or persisted format without a specification,
  migration and compatibility test.
- Do not create commits, tags, releases, worktrees or temporary commits, and do not push; inspect `git status`/`git diff` before editing.
- Do not install dependencies, fetch/vendor code or perform hidden downloads to enable a local build or test. Preserve LICENSE/NOTICE/provenance and technical identifiers/paths unless an approved migration exists (AGENTS.md).

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
regression, sanitizer/fuzz lane and residual risk. Propose a dated `PROGRESS.md` entry and any `TODO.md`/`ROADMAP.md` changes to the coordinator; append them only when explicitly assigned. Subagents report proposed tracking rows to the coordinator.

Record the exact AGENTS.md `Tracking states` value and state which validations were not run; do not collapse them into “pending”.

## References

- [Security audit matrix](references/audit-matrix.md)
- [Global policy](../../../AGENTS.md)
- [Memory-safety audit](../../../docs/security/memory-safety-2026.md)

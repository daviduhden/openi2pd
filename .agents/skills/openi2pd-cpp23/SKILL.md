---
name: openi2pd-cpp23
description: Use when modernizing or refactoring openi2pd native C++23 code, especially ownership, RAII, std::span, std::expected, container lifetimes, internal API breaks or cross-component header changes.
---
<!--
See the LICENSE file at the top of the project tree for copyright
and license details.
-->

# openi2pd C++23 modernization

## Activate when

Use this skill for native C++ changes that affect ownership, object lifetime,
interfaces or resource management. It is the default implementation skill for
`libi2pd/` and `libi2pd_client/`; combine it with the subsystem skill and with
`openi2pd-memory-security` when parsing or concurrency is involved.

Read [`../../../AGENTS.md`](../../../AGENTS.md) first. It is the authoritative
policy. This skill adds a procedure, not an exception to the static-only rule.

## Inspect first

- The real call sites and headers: `libi2pd/`, `libi2pd_client/`, `daemon/`,
  `tui/`, `privsep/`, `tests/`.
- Shared interfaces: `libi2pd/Log.h`, `libi2pd/RouterContext.h`,
  `libi2pd/Expected.h`, `libi2pd/AtomicSharedPtr.h`,
  `libi2pd/TuiProtocol.h` and `privsep/Protocol.h`.
- Existing task rows in `TODO.md` and the current state in `PROGRESS.md`.
- The C++23 rules and applied patterns in
  [`../../../docs/cpp23-migration.md`](../../../docs/cpp23-migration.md) and
  the component map in [`../../../docs/architecture.md`](../../../docs/architecture.md).

## Reference sources

- [`../../../docs/cpp23-migration.md`](../../../docs/cpp23-migration.md) — standard
  floor, applied changes, deliberate limits and remaining candidates.
- [`../../../docs/architecture.md`](../../../docs/architecture.md) — components,
  ownership notes and residual risks.
- [`../../../docs/devs/hacking.md`](../../../docs/devs/hacking.md) — repository
  layout and contribution conventions.
- [`../../../tests/CMakeLists.txt`](../../../tests/CMakeLists.txt) — test
  registration and labels.
- [`references/ownership-checklist.md`](references/ownership-checklist.md) —
  file-level review checklist and reconnaissance prompts.

## Reconnaissance

Launch at least one read-only `explore` subagent. For a broad refactor, launch
two in parallel:

1. Inventory current ownership, raw buffers, callbacks, thread affinity and
   call sites for the target files.
2. Map tests, build-target membership, public headers and documentation that
   must move with the interface.

Give them exact paths and ask for line references. Do not let them edit,
compile, run tests or run project scripts.

## Parallel implementation

- Split by independent translation units or components. Do not let two agents
  edit a shared header, CMake target or the same source file.
- The coordinator owns changes to `Log.h`, `RouterContext`, `Expected.h`,
  `CMakeLists.txt`, `TuiProtocol.h` and cross-library APIs.
- Preserve existing user changes. Use `git status` and `git diff` before
  editing; do not overwrite unrelated modifications.
- Prepare the test and documentation changes in the same workstream, but keep
  the diff reviewable.

## Conventions

- Target C++23 only. Do not add C++20 fallback branches or compiler-specific
  workarounds unless a platform capability check is already established.
- Prefer `std::unique_ptr`/`std::shared_ptr` with explicit ownership over raw
  owning pointers; use `std::span` for borrowed buffers and `std::expected`
  where the codebase already uses it.
- Keep object lifetime and thread affinity explicit. A callback must not retain
  references to dead owners; use existing atomic/snapshot patterns rather than
  adding unsynchronised reads.
- Keep internal API breaks deliberate and update all call sites and tests.
  Deep refactors are allowed; do not preserve a broken interface merely to keep
  a local build looking plausible.
- Do not change I2P wire formats, persisted formats, binary names,
  configuration paths or security checks as part of a mechanical cleanup.

## Do not

- Do not compile, configure, run CTest, sanitizers, fuzzers or project scripts
  locally.
- Do not copy Java architecture or accidental behavior into C++.
- Do not weaken bounds checks, signature/AEAD verification, `pledge`/`unveil`,
  sandbox policy or untrusted-input limits.
- Do not perform repository-wide formatting or unrelated renames.
- Do not commit, push, tag or release.

## Prepare for CI

- Add or update focused tests under `tests/`; register them in
  `tests/CMakeLists.txt` with the correct labels (`unit`, `memory`, `security`
  or the subsystem label).
- For lifetime or concurrency work, identify the ASan/UBSan/TSan and fuzz lanes
  that must execute the path.
- Keep `build-system-static` assertions coherent when source lists or options
  change.
- Record tests as prepared, not passed. Only a manual CI run can move a task to
  `Validated by CI`.

## Tracking

Use the owning area in `TODO.md` (normally `CPP`/B) and the status vocabulary
from `AGENTS.md`. Record files, decisions, static review, tests prepared and
the exact CI lanes still required. Append a dated `PROGRESS.md` entry. As a
subagent, report proposed rows to the coordinator instead of editing shared
tracking files concurrently.

## References

- [Ownership and interface checklist](references/ownership-checklist.md)
- [Repository policy](../../../AGENTS.md)
- [C++23 migration report](../../../docs/cpp23-migration.md)

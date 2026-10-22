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

The permanent `Modern language utilization policy` makes broad use of C++23 and
well-scoped OOP/generic abstractions an explicit objective. Prefer concepts,
ranges, `std::expected`, smart ownership, `jthread`/stop tokens and other
supported facilities even when no measurable performance gain is available.
Do not mass-convert every function into a class; abstractions need a contract,
ownership and a regression path. The availability matrix and deliberate
limits live in
[`../../../docs/cpp23-migration.md`](../../../docs/cpp23-migration.md).

## Inspect first

- The real call sites and headers: `libi2pd/`, `libi2pd_client/`, `daemon/`,
  `tui/`, `privsep/`, `tests/`.
- Reusable vocabulary: `libi2pd/Modern.h` (concepts, `ScopeExit`, overload
  composition and byte-span helpers).
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
two in parallel. If subagents are unavailable, report that limitation; do not
substitute local execution.

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

## Known issues and decisions

- Datagram receiver lifetime uses shared invocation slots and weak owner tokens
  (CONC-011). Drop replaced callbacks outside registry locks: releasing a
  captured owner can reenter registration from its destructor. Preserve a
  per-dispatch callback copy so mutable value captures do not become shared
  across concurrent invocations. Copy before claiming the in-flight count. Reset from a
  receiver callback cannot supply a full drain barrier; inspect its result.


- CONC-010 uses immutable process-local profile-root authority and flushed
  same-directory replacement; storage IO/cleanup is serialized and shutdown
  drains postponed updates after producers stop. Cross-process ownership,
  abandoned staging cleanup and retained external mutators remain open.
  `test-router-profile-storage` is prepared, not executed.


- `RouterProfile` state is guarded per instance; `GetLastEndpoint()` returns
  a value, never a reference escaping its lock. Profile-map eviction requires
  map-only shared ownership. Save/Load take the IO mutex before briefly taking
  the state mutex; no filesystem operation holds the state mutex. Global
  process-local profile storage/delete coordination is CONC-010; external
  producer quiescence remains a caller contract.
- `libi2pd/Modern.h` is shared vocabulary, but adoption is deliberately narrow
  (`Identity.cpp`, `test-modern`). `ScopeExit` cleanup must not throw;
  `AsByteSpan` rejects temporaries and must not be replaced by
  `reinterpret_cast` helpers.
- Signing-key sizes use one `constexpr` registry; unknown types return `false`
  and callers keep documented defaults. Offline signing keys are not chained.
- `ITransportConnector` is a stateless Strategy. `false` means "not
  applicable, try the next priority"; a pending SSU2 endpoint must return
  `true` and must not fall through to NTCP2.
- `jthread` adoption is partial: `TuiServer` and the X25519 supplier use stop
  tokens; `Transports::m_Thread` still has an explicit self-join/detach path.
  `LogLevel` remains a plain enum with `eNumLogLevels`/`g_LogLevelStr`.
- `test-modern` and `test-transport-connector` are registered but unexecuted;
  `language-modernization-static` pins seam names and exact AGENTS/doc phrases.

## Do not

- Do not compile, configure, run CTest, sanitizers, fuzzers or project scripts
  locally.
- Do not copy Java architecture or accidental behavior into C++.
- Do not weaken bounds checks, signature/AEAD verification, `pledge`/`unveil`,
  sandbox policy or untrusted-input limits.
- Do not perform repository-wide formatting or unrelated renames.
- Keep OpenBSD and LibreSSL viable; do not introduce OpenSSL-only APIs
  without the existing feature-selected LibreSSL fallback.
- Do not create commits, tags, releases, worktrees or temporary commits, and do not push; inspect `git status`/`git diff` before editing.
- Do not install dependencies, fetch/vendor code or perform hidden downloads to enable a local build or test. Preserve LICENSE/NOTICE/provenance and technical identifiers/paths unless an approved migration exists (AGENTS.md).

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
the exact CI lanes still required. Propose a dated `PROGRESS.md` entry and any `TODO.md`/`ROADMAP.md` changes to the coordinator; append them only when explicitly assigned. As a
subagent, report proposed rows to the coordinator instead of editing shared
tracking files concurrently.

Record the exact AGENTS.md `Tracking states` value and state which validations were not run; do not collapse them into “pending”.

## Modernization matrix

Use a feature only when its target is understood:

| Facility | Use / guard |
|---|---|
| concepts, folds, structured bindings, `span`/`string_view` | preferred in new interfaces and bounded parsers |
| `constexpr`/`consteval`/`constinit` | static tables and compile-time invariants |
| `std::optional`/`variant`/`any`/`i2p::expected` | recoverable absence/errors; do not replace protocol exceptions blindly |
| ranges/views, generic lambdas, `<=>` | local algorithms and comparisons with explicit lifetimes |
| `chrono`, `jthread`, stop tokens | new timers/workers; audit join and self-stop paths |
| `format`/`print`, `mdspan`, modules, coroutines, executors | not assumed until a compile probe and manual CI prove them |

## References

- [Ownership and interface checklist](references/ownership-checklist.md)
- [Repository policy](../../../AGENTS.md)
- [C++23 migration report](../../../docs/cpp23-migration.md)

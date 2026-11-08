---
name: openi2pd-cpp23
description: Use when modernizing or refactoring openi2pd native C++23 code, especially ownership, RAII, std::span, std::expected, container lifetimes, internal API breaks or cross-component header changes.
---

## Maintenance & Continuous Learning

When this skill guides a repository change, review it after implementation. Update procedures, tools, configurations, assumptions, examples and validation steps when they changed or when reusable knowledge improves future work; correct inaccurate guidance and omit one-off execution details. Report synchronization in the task summary. Follow the normative process and Definition of Done in [AGENTS.md](../../../AGENTS.md#continuous-agent--skills-maintenance).
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
- Shared interfaces require coordinator review. The authoritative list is in
  `AGENTS.md` `## Coordinator / ownership`; link it instead of keeping a
  partial local copy.
- Preserve existing user changes. Use `git status` and `git diff` before
  editing; do not overwrite unrelated modifications.
- Prepare the test and documentation changes in the same workstream, but keep
  the diff reviewable.

## Conventions

- Apply the [source documentation policy](../../../AGENTS.md#source-documentation-policy)
  when reviewing ownership, borrowed views, templates and thread-bound APIs.

- Target C++23 only. Do not add C++20 fallback branches or compiler-specific
  workarounds unless a platform capability check is already established.
- Prefer `std::unique_ptr`/`std::shared_ptr` with explicit ownership over raw
  owning pointers; use `std::span` for borrowed buffers and `std::expected`
  where the codebase already uses it.
- Keep object lifetime and thread affinity explicit. A callback must not retain
  references to dead owners; use existing atomic/snapshot patterns rather than
  adding unsynchronised reads.
- A cancellable operation is not a lifetime barrier. If owner removal can race
  with completion, capture weak ownership and check both lock success and the
  stopped/generation state before rearming or publishing side effects. Avoid
  strong callback cycles in long-lived registrations.
- Keep internal API breaks deliberate and update all call sites and tests.
  Deep refactors are allowed; do not preserve a broken interface merely to keep
  a local build looking plausible.
- Do not change I2P wire formats, persisted formats, binary names,
  configuration paths or security checks as part of a mechanical cleanup.
- A compiler diagnostic can be a cascade from malformed comments, an
  incomplete enum switch or a template instantiation far from its cause. Find
  the first meaningful diagnostic, inspect declarations and consumers, then
  search semantic variants in all platform branches before editing. Keep
  warnings and their historical disposition in
  `docs/ci/COMPILATION-WARNINGS.md`; static fixes remain pending until the
  relevant manual CI lane reruns.

## Known issues and decisions

- When a signed count API meets a size-derived expectation, inspect both
  declarations and use std::cmp_equal where equality is intended. Casting
  either operand can misrepresent negative results. A warning from test
  code still belongs in the exact-run inventory and requires CI compilation
  of the affected translation unit after correction.

- Tunnel test correlation uses a shared, local attempt identity in addition
  to the reusable wire ID. A retained drop callback and unsent rollback
  retire only that identity; after reply, detach or replacement they must
  neither erase the new entry nor penalize its tunnel. Reject live-ID
  collisions instead of overwriting the table. Move retained tunnel owners
  out under the correlation mutex, then run final destructors outside it.
  The timing regression reuses the same ID, pair and deadline explicitly;
  matching only those fields does not distinguish two local attempts.

- Callback ownership migrations can add several weak captures in one
  method. Reuse one weak handle within a lexical scope for drop, timer and
  posted continuations; a second declaration with the same name prevents
  every consumer of that translation unit from compiling. Review sibling
  branch scopes separately, and align the whole lambda body with its braces
  so an early-return guard does not visually own the following statements.
  Nested lambdas expose captured names as closure members under MSVC's
  shadow diagnostics. Give a rearmed timer owner a distinct name, and name
  retired weak-lock owners separately from the function's input owner.
  Renaming must not move final shared-owner destruction into a mutex scope.

- A startup condition predicate belongs to one joined worker generation.
  `Log::Start` re-arms its publication bit before creating a new writer;
  otherwise that writer can use the previous true bit and exit before the
  new running flag and first record are published. Keep reset/launch/publish
  ordering under lifecycle/start locks. A queue wakeup change cannot repair
  this interleaving. The logger source contract pins that ordering; runtime
  restart/sink coverage remains manual CI work.

- `RunnableService` owns an optional per-run executor work guard. Without it,
  an empty `io_context::run()` stops the context before a later post arrives;
  retrying `run()` then spins without dispatching. Start installs the guard,
  Stop releases it, and normal run completion exits the worker. A draining
  Stop must still enter run once if it won the thread-start race. Lifecycle
  callers must serialize Start/Stop; self-destruction/detach is a separate
  unresolved lifetime boundary. The lifecycle regression covers idle gaps,
  restart, queued drain and exception continuation without timing sleeps.
- NTP is a separate executor wrapper: publish initial Sync before launching
  its worker, exit on normal run exhaustion, and drain after join under
  revoked running authority before restart. Its shared test friend can drive
  empty-run exhaustion without contacting an NTP server. UPnP already posts
  initial discovery before launch and exits normally; review independent
  wrappers rather than assuming the base service fix reaches them.

- IRC tunnel forwarding requires a retained peer identity before WEBIRC
  formatting or USER rewriting. Reject missing identities at the factory
  and recheck the connection's identity before writing; never substitute a
  local address or emit a credential first. Linux unique-local source
  mapping likewise requires an identity before opening/mapping the socket.
  Fresh-stream negative fixtures intentionally have no remote identity and
  should verify socket closure and registry removal without an upstream.

- For multi-stage signing, combine owned OpenSSL contexts/scalars with a
  scoped output-commit guard: cleanse secret arrays on every return and
  zero the output unless the final encoding succeeded. Preserve overlapping
  input/output contracts by deferring output writes until hashing is done.
  A checked outer wrapper is not evidence that nested point arithmetic
  propagates allocation failures; record that boundary explicitly.

- ClientContext factories return the registry winner, never their local
  candidate after a failed insertion. The creation mutex serializes factory
  publication and Start; it precedes the registry mutex, which is released
  before Start. This is not a global Stop/reload admission barrier. Preserve
  offline-signature refresh on the winning object. Hash replacement rejects
  occupied keys and inserts the new key before erasing the old one so an
  allocation failure cannot remove the registration.

- Split descriptor cleanup from registry notification in shared handlers.
  `SocketsPipe` destruction closes both endpoints through error-code
  overloads without acquiring shared ownership; active termination notifies
  the owner only after locking its weak self. A destructor calling a normal
  teardown method can otherwise throw `bad_weak_ptr` after the last owner
  disappears. Prepare both unstarted-destruction and repeated-termination
  regressions when changing this seam.
- `std::span<T>` only range-constructs from a contiguous range of the same
  element type; a `char` container passed to `std::span<const uint8_t>` needs a
  borrowing `reinterpret_cast` helper with full-expression lifetime.
- `std::span` parameters that reach a receive loop must not be resized from a
  record length: keep one fixed-capacity storage and decode a `string_view` of
  each record.
- `std::span<T>` only range-constructs from a contiguous range of the same
  element type. Passing `std::string`/`std::string_view` to
  `std::span<const uint8_t>` is ill-formed; use one borrowing
  `reinterpret_cast` helper per test and keep the source alive for the full
  expression (CE-069).

- Legacy BOB tunnels are shared through `I2PService`; do not add a second
  `enable_shared_from_this`. Derive weak tunnel pointers from the inherited
  shared owner, gate every completion/rearm on `m_Stopped`, register and close
  pending receivers, and serialize stop with handler admission. Close a stream
  delivered after stop. Manual ASan/TSan lifecycle validation remains pending.
  Peer-identity checks need a mutable Asio socket because
  `remote_endpoint(error_code)` and `native_handle()` are non-const.

- Datagram receiver lifetime uses shared invocation slots and weak owner tokens
  (CONC-011). Drop replaced callbacks outside registry locks: releasing a
  captured owner can reenter registration from its destructor. Preserve a
  per-dispatch callback copy so mutable value captures do not become shared
  across concurrent invocations. Copy before claiming the in-flight count. Reset from a
  receiver callback cannot supply a full drain barrier; inspect its result.

- Headers with inline `LogPrint`/`eLog*` use must include `Log.h` themselves.
  Do not rely on the including translation unit's order: Android, Windows and
  smaller targets compile `LeaseSet.h`/`I2PService.h` before any logger header
  and fail with undeclared log levels.

- A test subclass does not gain public access to a protected base method just
  by exposing other lifecycle operations. When a lifecycle test needs to read
  protected state such as `RunnableService::IsRunning()`, provide a narrow
  public observer on the test subclass; do not widen the production API only
  to satisfy a test.
- `using namespace i2p::tunnel` does not import declarations from the parent
  `i2p` namespace. Tests using shared protocol constants declared in `i2p`
  should qualify them or add a narrow using-declaration; avoid duplicating
  wire-size literals in fixtures.
- Namespace imports do not expose members nested inside a type in that
  namespace. For example, a test importing `i2p::data` still must qualify the
  transport enumerator as `i2p::data::RouterInfo::eAllTransports`.
- When reviewing comment width, distinguish prose from code continuations,
  preprocessor directives embedded in CMake probes and protocol literals.
  Reflow only complete comment text; preserve adjacent source syntax exactly.


- Profile-root/durability decisions (CONC-010) and their residual risks are
  owned by `openi2pd-memory-security`; do not restate the storage protocol in
  this skill.


- `RouterProfile` state is guarded per instance; `GetLastEndpoint()` returns
  a value, never a reference escaping its lock. Profile-map eviction requires
  map-only shared ownership. Save/Load take the IO mutex before briefly taking
  the state mutex; no filesystem operation holds the state mutex. Global
  process-local profile storage/delete coordination is CONC-010; external
  producer quiescence remains a caller contract.
- `libi2pd/Modern.h` is shared vocabulary. `ScopeExit` replaces the old
  `Defer`/`SaveStateHelper` helpers in `Identity.cpp`, `AddressBook.cpp`, the
  ECIES ratchet, `RouterContext.cpp` and `SSU2Session.cpp`; cleanup must not
  throw. `AsByteSpan` rejects temporaries
  and must not be replaced by `reinterpret_cast` helpers. `i2p::expected` now
  provides `value_or`/`and_then`, and memory pools use typed nested deleters
  instead of `std::function`/`std::bind`.
- Signing-key sizes use one `constexpr` registry; unknown types return `false`
  and callers keep documented defaults. Offline signing keys are not chained.
- `ITransportConnector` is a stateless Strategy. `false` means "not
  applicable, try the next priority"; a pending SSU2 endpoint must return
  `true` and must not fall through to NTCP2.
- `jthread` adoption is partial: `TuiServer` and
  `X25519KeysPairSupplier::m_Thread` use stop tokens; `Transports::m_Thread`
  remains a `std::unique_ptr<std::thread>` with the documented self-detach
  path. Do not describe `Transports` as jthread-owned.
  `LogLevel` remains a plain enum with `eNumLogLevels`/`g_LogLevelStr`.
- `test-modern` and `test-transport-connector` are registered but unexecuted;
  `language-modernization-static` pins seam names and exact AGENTS/doc phrases.


- C++23 has no `operator+(std::string, std::string_view)`. A helper that
  returns `std::string_view` (for example the one-argument `i2p::i18n::tr`)
  must be materialised with `std::string (...)` before a binary `+`, and the
  left operand must never be a `const char*`. `operator+=` and a
  `std::string_view` parameter accept the view directly.
- Qualify constants that live in another namespace instead of relying on
  argument-dependent lookup: `i2p::stream` does not see
  `i2p::client::STOP_ON_SERVICE_TIMEOUT` unqualified.
- When an aggregate or helper type is reshaped (for example an
  `std::pair` replaced by a named struct), expire every structured access
  (`.first`/`.second`) together with the declaration; the compiler cannot
  find hidden stale accesses in a rarely built platform branch.
- Public headers must include what they use. Add `<cstring>` for `memcpy`,
  `<unordered_map>` for its members and the matching standard header for every
  container/utility used in the class body; do not rely on a sibling
  translation unit's include order.

- `GarlicDestination::m_Sessions` stores `ElGamalAESSessionPtr`; keep cleanup
  snapshots at that concrete pointer type because `CleanupExpiredTags()` is
  not a `GarlicRoutingSession` virtual and ratchet sessions live in a separate
  map. `std::bind` also does not apply default arguments: bind every member
  parameter explicitly or use a lambda that calls the method.

- Source comments follow
  [`docs/source-documentation.md`](../../../docs/source-documentation.md):
  Doxygen contracts for public APIs, `//` for non-obvious algorithms, and no
  comment that contradicts the implementation. A documentation-only change
  must not alter code, identifiers or formatting.


- Keep warning-clean MSVC builds: do not narrow `size_t`/`int64_t` into
  `int`/`uint8_t` without an explicit cast and a proven bound, and do not
  shadow a local (`context`, `l`, `ind`) across nested scopes. A wire field
  still gets one cast at serialization, not a narrowed accumulator.

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

- Public headers and standalone `.cpp` files include the standard facilities
  they expose directly (`<chrono>`, `<utility>`, `<cstddef>`, `<cstdint>`,
  `<map>`, `<string_view>`, `<list>`, `<functional>`, `<memory>`, `<vector>`,
  `<openssl/sha.h>`). Do not depend on Boost.Asio, OpenSSL or another project
  header to supply them; `Crypto.h` must keep the `OPENSSL_PQ 0` fallback.
  `SOCKS.cpp` declarations must not survive without a definition.

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

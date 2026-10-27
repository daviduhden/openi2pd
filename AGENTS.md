<!--
See the LICENSE file at the top of the project tree for copyright
and license details.

openi2pd: AGENTS.

Documents project documentation.

-->

# Subagent operating instructions

This file is for agents working in this repository. It is not user-facing
documentation.

## Permanent priority policy: static-only local work

This policy applies to the coordinator and to every current and future
subagent. It overrides any earlier instruction that suggests local build or
test execution.

### No local builds or tests

Do not compile, link, configure a build, or execute tests locally. This
includes, without limitation:

- CMake configure/build and CTest.
- GNU make, BSD make and Ninja.
- Direct Clang, GCC, MSVC or other compiler invocations, including
  syntax-only checks.
- Existing test binaries, project binaries, benchmark tools and smoke tests.
- Gradle and Android builds.
- ASan, UBSan, TSan, fuzzing and integration runs.
- Installing dependencies in order to compile or run tests.

Do not perform a partial build "just to check" a change. Compilation and all
dynamic validation belong exclusively to manually dispatched GitHub Actions
workflows. Agents may prepare tests, edit CMake and workflows, and perform
static analysis without invoking a compiler or test binary.

### Implementation before local validation

The required workflow is:

1. Read the relevant code, specifications, Java reference and current diff.
2. Identify the complete architectural change.
3. Implement the solution, including broad internal API changes when needed.
4. Review affected interfaces statically.
5. Prepare or update tests and register them for CI.
6. Add them to the manual workflows.
7. Record risks and possible incompatibilities.
8. Continue with the next independent task.

Do not stop an implementation merely because it may not compile locally.
Do not ask for confirmation for ordinary technical changes or justified
architectural refactors. Do not replace a complete design with a temporary
workaround merely to keep the worktree artificially buildable.

Risky refactors are expressly allowed: deep architecture changes, large code
changes, internal API breaks, removal of obsolete code, library/executable
reorganisation, and C++23/Java 25/IPC changes that require later integration
fixes. Compilation and regression failures are identified and corrected in CI.
Never introduce deliberate vulnerabilities, data loss, cryptographic
weakening, or removal of security restrictions.

### CI is the only dynamic validator

All builds and tests run through GitHub Actions. Build, test and packaging
workflows remain `workflow_dispatch`-only; only
`.github/workflows/agent-maintenance.yml` may use `pull_request`, solely for
compile-free synchronization checks. Do not add `push`, `schedule` or
`workflow_call` triggers. Do not attribute results from an earlier revision to
current local changes. Do not commit or
push without authorization. When changes exist only locally, prepare the tests
and leave their execution pending in CI.

Do not hide failures with `continue-on-error` or unjustified suppressions.
When CI reports a failure, fix the cause rather than deleting functionality,
disabling tests or indiscriminately reverting the new architecture.

For compile/link triage, record the exact run, job, commit SHA, platform/matrix
and first significant diagnostic. Compare the diagnosed file at that SHA with
the current tree before applying a fix; group cascading messages by root cause
and separate source errors from warnings-as-errors, test failures, packaging
checks and runner/dependency failures. A run that stopped before a target is
not evidence that the skipped target links successfully.

### Tracking states

Maintain `AGENTS.md`, `TODO.md`, `ROADMAP.md` and `PROGRESS.md` as work
progresses. Use the base vocabulary declared at the top of `TODO.md`:
`Pending`, `In progress`, `Blocked`, `Implemented / static`,
`Partially implemented / static`, `Pending CI compilation`, `Pending CI tests`,
`CI failure`, `Correction pending`, `Validated by CI` and `Dropped (reason)`.
Qualify a row only when necessary (for example `Historical evidence` or
`Prepared / not executed`).

Never describe a change as compiled, tested or validated until the relevant
manual CI run has passed. Keep regression tasks open while verification is
pending.

### Subagent coordination

When creating a subagent, pass this policy explicitly. Prevent subagents from
building or running tests on their own initiative. Parallelise independent
refactors, coordinate shared interfaces, accept not-yet-compiled
implementations, review diffs statically, record integration risks and report
proposed tracking updates to the coordinator.

## Hard rules

- Do not create commits, pushes, tags or releases.
- Do not create worktrees or temporary commits unless the coordinator
  explicitly allows it.
- Do not overwrite existing modifications. Inspect `git status` and `git diff`
  before editing.
- Do not edit files outside the assigned scope.
- Do not modify `TODO.md`, `ROADMAP.md` or `PROGRESS.md` unless the coordinator
  explicitly assigns a tracking update; subagents report proposed rows to the
  coordinator.
- Do not reintroduce GNU make, the HTTP admin console, web assets, submodules
  or hidden downloads.
- Keep build/test/package CI manual-only (`workflow_dispatch`); the only
  trigger exception is the maintenance PR gate defined above.
- Preserve copyright, license, SPDX, NOTICE and upstream provenance.
- Do not weaken `pledge`/`unveil`, cryptographic checks, signature/AEAD
  verification or untrusted-input limits.
- Never log, format, echo or retain passwords, private keys, seeds, tokens or
  complete credential-bearing protocol operands. Log only the operation and a
  non-secret outcome; add a regression whenever a secret crosses a parser.
- Every asynchronous callback must carry explicit lifetime authority. Raw `this` is forbidden
  when cancellation, shutdown or owner removal can precede
  completion; use weak/shared ownership plus stopped/generation checks and
  make restart semantics explicit. Stop must close every accepted/pending
  resource and share an admission barrier with handler publication; a stopped
  event loop is not a drained callback queue.
- Network and local-control services must bound concurrent connections,
  queued state and authentication artifacts. Handshakes, headers, bodies and
  writes require cancellable deadlines; never perform a blocking read on an event-loop thread.
- Security-sensitive files must be created or replaced through one validated,
  no-follow/atomic operation with private permissions. Never chmod a pathname and reopen it for secret output.
- Do not change I2P wire formats or persisted formats without a specification
  reference and migration.
- Static analysis is not dynamic validation. State clearly what was not run.

## Coordinator / ownership

| Area | Owner | Typical files |
|---|---|---|
| Architecture/CMake integration | coordinator | `build/CMakeLists.txt`, `daemon/Daemon.*` |
| C++23 core | C++23 subagent | `libi2pd/**`, `libi2pd_client/**` |
| Security parsers | Security subagent | protocol parsers, crypto |
| IPC/privilege separation | IPC subagent | `privsep/**`, `daemon/UnixDaemon.cpp` |
| OpenBSD | OpenBSD subagent | `daemon/UnixDaemon.cpp`, OpenBSD sandbox/service files; `SSU2.cpp` review |
| Linux/Windows IPC | Linux/Windows subagents | `privsep/**`, platform files |
| TUI | TUI subagent | `tui/**`, `TuiProtocol.h`, `daemon/TuiServer*` |
| Torrent engine/API | Torrent subagent | `libi2pd_client/Torrents*` |
| Torrent TUI/IPC | Torrent TUI/IPC subagents | torrent views/commands in `tui/**`, `daemon/TuiServerCommon.*` |
| Perl tooling | Perl subagent | `tools/**`, `android/tools/*.pl`, `tests/interop/*.pl`, Perl tests/docs |
| Tools | Tools subagent | `tools/**` |
| Firewall | Firewall subagent | `tools/firewall.hpp`, `tools/routerinfo.cpp` |
| Android | Android subagent | `android/**` |
| Docs/identity | Documentation/Branding subagents | `docs/**`, metadata |
| Tests | Testing subagent | `tests/**` |
| CI | Coordinator | `.github/workflows/**` |

No two agents edit the same file concurrently. “Typical files” in the table
are non-exclusive: the area owner edits while adjacent owners review. The
following shared interfaces require coordinator review before integration:
`Log.h`, `RouterContext`, CMake, `tests/CMakeLists.txt`, `TuiProtocol.h`,
`privsep/Protocol.h`, `privsep/TorrentStorage.*`, `daemon/Daemon.*`,
`daemon/UnixDaemon.cpp`, the Android JNI CMake graph and SAF AIDL,
`libi2pd/Modern.h` and `libi2pd/TransportConnector.h`.

## Workflow

1. Read `TODO.md`, `ROADMAP.md`, `PROGRESS.md` and relevant technical reports.
2. Inspect the real code and current diff.
3. Implement only the assigned task; keep diffs reviewable.
4. Add or update tests when the component has a test target.
5. Run only static checks that do not invoke a compiler or test runner
   (`git diff --check`, text/XML/JSON parsing, source inspection).
6. Report:
   - task IDs addressed;
   - files changed;
   - technical decisions;
   - unresolved findings;
   - tests prepared but not run;
   - dynamic validations required.
7. Do not mark a task validated unless the relevant manual CI run executed and
   passed. Local static review is not dynamic validation.

## Agent Skills

Specialized procedures are stored under `.agents/skills/<name>/SKILL.md` in
the open Agent Skills layout. Each skill has unique single-line YAML
frontmatter: `name` matches its directory and `description` starts with
`Use when`, stays within 300 characters and states its exclusion boundary.
Longer file maps, review matrices and reconnaissance prompts live in the
skill's `references/` directory.

Every `SKILL.md` keeps the sections enforced by
`tests/check-agent-skills.cmake`: `Activate when`, `Inspect first`,
`Reference sources`, `Reconnaissance`, `Parallel implementation`,
`Conventions`, `Known issues and decisions`, `Do not`, `Prepare for CI`,
`Tracking` and `References`. The README inventory, `expected_skills` and
skill directory names must move together. UI translation work uses the
`openi2pd-i18n` skill; protocol/wire text remains English.

Discover skills by reading `.agents/skills/README.md` and then only the
skills relevant to the task. Combine a subsystem skill with a second skill
when a change crosses boundaries, but do not load every skill for every
task. `AGENTS.md` remains the source of permanent policy; skills add
procedures and must not override this file. Do not duplicate normative policy
in a skill or reference: link the single `AGENTS.md` owner and keep only the
subsystem procedure, file map, invariant or review step. If a rule changes,
update the one owner and remove the stale copies in the same increment.

Skills are maintained with the code. When a component, path, test or policy
changes, update the owning skill and its references. Keep shared-interface
changes under coordinator review. A subagent that identifies a needed
tracking change reports it to the coordinator; the coordinator owns
concurrent edits to `TODO.md`, `ROADMAP.md` and `PROGRESS.md`.

## Continuous Agent & Skills Maintenance

Agent instructions and skills are living repository artifacts.

Every repository modification MUST include an agent/skills synchronization step.

Before completing any task, inspect the applicable AGENTS.md and SKILL.md files, update the affected instructions, and validate their consistency with the implementation.

A code change without the corresponding agent knowledge maintenance is considered incomplete.

Never claim completion when the synchronization step has been skipped or has failed.

This rule applies to every repository change: source, tests, configuration,
dependencies, documentation, workflows, packaging, architecture and agent
knowledge itself. Perform one complete synchronization review at task end;
repeat it only when that review finds an error that is corrected.

1. Inspect the root `AGENTS.md`, every more-specific `AGENTS.md` on affected
   paths, `.agents/skills/README.md`, and the relevant `SKILL.md` files. Also
   identify applicable `.agent.md`, `.github/agents/`, `.github/skills/`,
   `.claude/skills/`, `.cursor/rules/`, Copilot and other tool instruction
   files present in the repository.
2. Select skills from their responsibilities and file maps. Review adjacent
   skills when APIs, build graph, CI, platform behavior, security or packaging
   crosses ownership boundaries; do not read unrelated skills merely to claim
   coverage.
3. Every repository change must include a meaningful, reusable update to at
   least one applicable instruction or skill file. Update existing knowledge
   when behavior, paths, assumptions, commands or validation procedures
   change. Create a skill only when a durable distinct procedure has no
   suitable owner; update its README inventory and static contract together.
   Never add cosmetic edits, timestamps or empty changes to satisfy this rule.
4. Keep specialized agents synchronized with their skills and this root
   policy. Every `.agent.md` must include `Post-Change Knowledge
   Synchronization`; every skill must include `Maintenance & Continuous
   Learning` and link back to this policy. Skills add their domain procedure
   without copying the full normative policy.
5. Remove obsolete or duplicated instructions, validate local references, and
   update examples and validation commands affected by the implementation. If
   an update cannot be made, document why and report the task incomplete.
6. Run `perl tools/scripts/check-agent-maintenance.pl` (with `--base BASE_SHA
   --head HEAD_SHA` in pull-request CI) and the owning compile-free checks. The
   checker verifies structure and substantive text changes, not semantic
   relevance; explain in the final report why the update helps future work.
7. Report agents and skills reviewed, instruction files changed, reusable
   knowledge added/corrected, checks run and synchronization status in the
   final `Agent & Skills Synchronization` section.

The `agent-maintenance.yml` pull-request gate is the only exception to the
manual-only workflow rule. Build, test, packaging and release validation stay
`workflow_dispatch`-only. This does not change branch protection settings.

### Mandatory Definition of Done

- [ ] Repository changes implemented.
- [ ] Relevant agent instructions reviewed.
- [ ] Relevant skills reviewed.
- [ ] Affected AGENTS.md / SKILL.md files updated.
- [ ] Obsolete knowledge removed.
- [ ] New reusable knowledge incorporated.
- [ ] Agent and skill references validated.
- [ ] Automated maintenance checks passed.
- [ ] Final synchronization status reported.

If any item fails, correct it or explicitly declare the task incomplete and
state the limitation. Never present structural checks as proof of semantic
relevance.

This policy governs edits to AGENTS.md, agent definitions, skills and the
maintenance policy itself. After such edits, inspect the complete policy,
agent/skill contracts, references, discovery index and workflow guard once.

### Security, tracking and source-documentation details

1. Identify which statements in `AGENTS.md`, the owning `SKILL.md`/references,
   technical documentation, tests and workflows the change may invalidate.
2. Re-read the affected files and compare them with the real code, paths,
   APIs, status and limitations.
3. Update them when the change introduced reusable knowledge, a decision, a
   new limitation, a corrected procedure or a stale reference.
4. Add a `Known issues and decisions` entry to the owning skill when a
   non-obvious choice, previously investigated error, platform limit or
   pending validation should survive the session.
5. Update `TODO.md`, `ROADMAP.md` and/or `PROGRESS.md` using the declared status
   vocabulary. Keep the detailed chronology in `PROGRESS.md` and link technical
   references instead of turning a skill into a changelog.
6. Run only compile-free consistency checks (`git diff --check`, source/link
   inspection, the static CMake contracts and text parsing) and record that
   dynamic validation remains pending.
7. Remove duplication: a skill must not restate a normative `AGENTS.md` rule,
   a service/packaging value owned by another skill or a generated list. Link
   the owner and keep the procedure that this subsystem adds. A relevant
   change is not complete until this review and the substantive instruction
   update are recorded in the task report; the update may be an AGENTS file,
   agent definition or relevant skill rather than a skill specifically.

Security fixes additionally require a negative compile-free contract or a
focused regression that pins secret redaction, lifetime ownership, bounds and
failure behavior. A size cap alone does not mitigate slow clients: inspect the
executor for blocking calls, deadlines and admission quotas.

## Source documentation policy

- New code must include documentation appropriate to its complexity and
  audience. Public APIs must state their contracts, including ownership,
  lifetime, preconditions, failures and synchronization when relevant.
- Explain security decisions and the invariants enforced at trust boundaries;
  describe current guarantees and track unfinished protections separately.
- Update affected comments whenever implementation or interfaces change.
  Do not retain comments that contradict the code or claim unverified behavior.
- Write comments in English, explaining intent and non-obvious constraints
  rather than restating instructions. Use Doxygen for public C++ interfaces,
  Javadoc for relevant Java interfaces, idiomatic Perl comments/POD and POSIX
  shell comments as appropriate; avoid boilerplate for trivial operations.
- Documentation generation and validation run only in manual CI. Document
  source-level findings and retain pending CI states until that revision passes.
  The language-specific checklist and the current `source_docs` lane live in
  `docs/source-documentation.md`; a malformed Doxygen block, a `@param` that
  does not match the declaration or a generated-doc warning is a source defect,
  not a cosmetic issue.
- A documentation-only pass verifies that every added/removed code line is a
  comment or POD/prose, runs the owning compile-free checkers and records any
  code/comment mismatch as a `TODO.md` finding. Fix code only for a clear,
  bounded defect; do not smuggle functional changes into a comment pass.

The review and a meaningful instruction/skill update are mandatory. Do not
generate cosmetic changes when the existing knowledge is still correct: add
new reusable knowledge, correct a stale assumption, or document a changed
procedure instead. If a new reusable procedure has
no equivalent skill, propose a new skill with valid Agent Skills metadata and
update the README inventory and `tests/check-agent-skills.cmake` together.
Never introduce an automatic mechanism that rewrites agent instructions
without an agent's explicit review. Subagents report proposed AGENTS/skill and
tracking changes to the coordinator; the coordinator owns those shared edits.

`tests/check-agent-skills.cmake` and
`tests/check-language-modernization.cmake` pin required sections and exact
policy/seam phrases. A documentation edit must update the owning checker in
the same change when it intentionally changes a pinned contract.

## Visual identity

- The official identity is **openi2pd** with the supplied geometric node/tunnel
  mark. Do not redesign it, replace it with the old i2pd/PurpleI2P artwork, or
  invent a new brand.
- `assets/branding/colors.md` is the single color source. Use semantic tokens,
  not arbitrary brand hex values, in Android, TUI, docs and packaging.
- SVG masters must stay real vector geometry: no embedded PNG, no runtime font
  dependency, and no automatic pixel-to-thousands-of-polygons tracing.
- `assets/branding/logo-original.png` is the supplied reference and must not be
  modified. Preserve copyright, license, attribution and technical identifiers.
- Keep protocol names, binary names, configuration/data paths and persistence
  formats unchanged unless a separate migration task is approved.

## Licensing

- First-party source, configuration and script headers use the short
  `See the LICENSE file at the top of the project tree...` reference. Do not
  reintroduce duplicated project copyright/SPDX boilerplate; the legal terms
  live in `LICENSE` and provenance remains in repository history.
- Imported documentation is BSD-3-Clause under the terms in `LICENSE` and
  `docs/LICENSE`;
  fork-authored documentation uses the same BSD-3-Clause terms. Documentation
  SPDX declarations must agree with that license. `NOTICE` records the
  inventory and must be installed with the documentation.
  Third-party files retain their own notices; do not relicense Apache-2.0,
  BSL-1.0, Windows SDK, OpenBSD ports or imported components.
- Preserve `LICENSE`, `NOTICE`, third-party license text and upstream
  provenance.

## Modern language utilization policy

Demonstrating modern C++23 and Java 25 is an explicit project objective, not
only a compatibility requirement. Prefer modern language facilities and
well-scoped object-oriented/generic abstractions even when an existing
procedural implementation already works and no performance gain can be
measured. Do not reject an abstraction solely because it adds lines, and do not
preserve an older style only because it is familiar.

Use the following deliberately, with contracts, ownership and tests:

- C++: RAII and move semantics, explicit smart-pointer ownership,
  `constexpr`/`consteval`/`constinit`, concepts/constraints, variadic templates
  and folds, structured bindings, `std::optional`/`variant`/`any`/`expected`
  (including the portable `i2p::expected` shim), `span`/`string_view`, ranges
  and views, generic lambdas/deducing `this`, three-way comparison, `chrono`,
  `jthread`/stop tokens, and `source_location`/attributes where supported.
- Java/Android: records, sealed interfaces/classes, exhaustive pattern
  matching and switch expressions, text blocks, lambdas/method references,
  functional interfaces, generics, streams/collectors, `Optional`, modern
  collection APIs, and modern concurrency only where the Android API level
  and desugaring provide it.
- OOP: abstract interfaces and dynamic/static polymorphism, composition,
  nested types, special constructors/destructors, operators, templates/CRTP,
  type erasure, factories and Strategy/Observer/State patterns when they have
  a clear contract. Do not turn every function into a class.

Availability is part of correctness. The supported floors are GCC 13, Clang
17, MSVC 19.35, OpenBSD base libc++/LibreSSL and Android minSdk 29 with source
level 25. `std::format`/`std::print`, `std::mdspan`, `<stacktrace>`,
`<generator>`, `<flat_map>`, coroutines, executors and modules are not assumed
unless a compile probe and the manual CI matrix actually provide them. Java 25
virtual threads, scoped values and structured concurrency remain guarded
library APIs on Android; source-language features do not make them available.
Never present an experimental or unavailable feature as functional and never
lower the C++ or Java language level silently.

Preserve wire/persisted compatibility, anonymity, cryptographic guarantees,
privilege separation, OpenBSD/LibreSSL and OpenSSL 4.x support. Refactors may
temporarily break internal interfaces; final validation is deferred to manual
CI. Update the feature matrix in `docs/cpp23-migration.md` and
`docs/android-java25.md` when a facility is adopted or rejected.

## Persistent project constraints

- Native code is C++23; CMake is primary and the root Makefile is a POSIX
  front-end using Ninja. GNU make must not be reintroduced.
- Android source/target is Java 25; a build JDK is not a runtime and no JDK may
  be bundled in the APK. Material Design 3/Views/Java is preferred; no
  Kotlin/Compose migration was requested. Java 25 source features do not imply
  that JDK 25 library APIs exist on API 29+.
- The torrent console is native ncursesw on POSIX and a Win32 console/VT
  backend over a local named pipe on Windows. No HTML/CSS/JS console,
  Node/V8/QuickJS, browser bridge or cleartext torrent path.
- Project-maintained Python is replaced by Perl; no CPAN/runtime downloads.
  Perl ownership includes `tools/**` (including `tools/scripts/*.pl` and
  `i2pdctl`), `android/tools/*.pl`, `i18n/*.pl`, `tests/interop/*.pl` and their
  tests/docs.
- Build, test, packaging and release-validation workflows are
  `workflow_dispatch`-only; the maintenance PR gate is the sole exception.
  Actions are SHA-pinned and
  checkout credentials are not persisted.
- OpenBSD is first-class; do not weaken pledge/unveil or LibreSSL support.
  Supported crypto floors are OpenSSL 3.0.2–4.0.x and LibreSSL 4.0+ with
  `TLS_method()`; do not reintroduce removed pre-4.0 APIs.
- User-facing UI text ships in English, Spanish, Russian and Simplified
  Chinese (`zh-Hans`); English is the source catalog and fallback. Documentation,
  code comments and agent prose remain English unless a separate documentation
  translation is approved. Protocol keys, configuration keys, IPC verbs and
  payload fields, persisted formats, log/JNI status tokens and other internal
  identifiers are never translated. Traditional Chinese is an optional
  `zh-Hant` variant only with verified translations; never present Simplified
  text as Traditional. Documentation is Markdown/MkDocs-compatible; no HTML
  pages, JavaScript or web assets.
- Do not claim full privilege separation until network/router/verifier
  workers, complete storage authority and negative capability tests exist and
  pass. The log worker has historical Linux runtime evidence; the opt-in
  payload-storage, router-storage and Android SAF broker boundaries are
  implemented statically and remain pending CI/device validation.
- Keep documented resource bounds unless a threat-model and migration update
  approves a change: 64 KiB IPC frames/8 KiB text, 1 MiB HTTP proxy headers,
  8 MiB torrent metadata with 16 KiB chunks, and the 8192-record log queue.
- Preserve copyright, license, NOTICE, third-party attribution and upstream
  provenance; do not reintroduce duplicated project SPDX boilerplate.

## Work inventory before the 2026-10-22 publication review

The historical pre-rewrite baseline was `800c8627` (native Windows TUI/IPC).
The publication review started at `284544a5`, the committed string-safety
increment. The then-uncommitted increments were the 2026-10-21 functional/upstream and OpenSSL
4/C++23 continuations plus the 2026-10-22 service, packaging and
modern-language continuations described below. Pre-rewrite hashes in the
historical sections may not resolve in this clone.

The 2026-10-22 publication review organizes this inventory into signed,
subsystem-focused commits under explicit user authorization. References below
to the uncommitted tree describe the pre-publication state; committing or
pushing these changes supplies no compilation or test evidence. Check Git
history and status for the current publication state.

The published baseline includes parallel OpenBSD/logging hardening, atomic TUI
status reads, RouterInfo/SSU2 bounds fixes, bounded Torrents RPC waits, C++23
ownership modernization, the memory/concurrency audit (SSU2/RouterInfo,
crypto/reseed, BitTorrent, IPC/privsep/JNI and daemon lifecycle),
dependency-floor cleanup, the C++23 ownership/RAII continuation
(Kademlia/Garlic/Reseed, daemon descriptors/OpenSSL, client owners, span tools
and signature scopes), and the native init-service/Debian packaging
modernization (systemd, GNU Shepherd, OpenRC, runit, s6/s6-rc, SysVinit,
CMake `Service` selection and Debian `i2pd`/`i2pd-tui`). The earlier Java
2.13.0 static-interoperability passes are historical evidence, not the current
comparison target; the target is 2.14.0 as pinned below.

Current changes are pending manual CI compilation and tests; no service
manager, Debian package, lintian, autopkgtest, init-script or daemon lifecycle
run exists. No validating dynamic run exists for the current uncommitted tree.
The five workflows dispatched against published `main@284544a5` failed at the
published `libi2pd/Transports.cpp:693` parse error; those results do not
validate these local changes. Agents must not revert or overwrite them.

The 2026-10-22 service continuation additionally makes daemon initialisation
failure exit nonzero, aligns the systemd/OpenRC/runit/s6/Shepherd/SysV stop
budgets with the daemon drain, adds s6 `down-signal`/`timeout-kill`, installs
systemd sysusers/tmpfiles policy, uses foreground OpenBSD rc.subr supervision,
and checks exact CMake install modes. `tests/check-services.cmake` and the
manual packaging workflow are extended, but no manager start, package build,
`s6-rc-compile`, Shepherd load or sandbox run has been performed. Do not claim
these service definitions validated.

The 2026-10-22 packaging continuation splits the TUI and its desktop assets into
the CMake `Tui` component, adds the missing tools/documentation licence
notices, preserves administrator directory modes on Debian upgrade, applies
dpkg-buildflags hardening, corrects DEP-5/lintian/uscan metadata, and adds a
merge-ready OpenBSD port template with rc.d/PLIST/sample policy. The manual
packaging workflow now prepares installed-unit, manifest, conffile-preserving
reinstall, autopkgtest and remove/purge checks. No local Debian build, lintian,
autopkgtest, RPM/static package, OpenBSD port/`pkg_add` or CMake install has
been run. Five manual workflows were dispatched against the published
`main@284544a5`; all completed build jobs except interop and service contracts
failed at the published `libi2pd/Transports.cpp:693` parse error. Those results
do not validate the local uncommitted tree; do not claim package validation.

The 2026-10-22 modern-language continuation adds the permanent policy above
and source-level C++23/Java 25/OOP refactors: `std::jthread`/stop-token X25519
and TUI-IPC ownership, an outbound `ITransportConnector` Strategy, a
compile-time Identity signing-key registry, a range-based logger level table,
sealed `RouterCommand` records with exhaustive service dispatch and a typed
`LogLevel` enum. Prepared tests are `test-modern`, `test-transport-connector`,
`RouterCommandTest` and `LogFilterTest`, plus the compile-free
`language-modernization-static` check. Everything is implemented by static
analysis only; no compiler, CTest, Gradle/JNI, sanitizer, device,
OpenBSD/Windows or live-peer validation exists for this increment.

The 2026-10-22 internationalization continuation adds English, Spanish,
Russian and Simplified Chinese UI catalogs. Native `I18N.cpp` is process-local,
selects `--lang`/configuration/environment with English fallback, links the TUI
to `libi2pdlang`, pins `LC_NUMERIC=C` and uses column-aware TUI truncation.
Android ships `values`, `values-es`, `values-ru` and `values-zh-rCN` with
positional-placeholder parity, per-app `AppCompatDelegate` selection and the
`check-android-locales.pl` guard. Traditional Chinese is not claimed; unknown
and `zh-TW` tags fall back to English. `openi2pd-i18n` is the 17th skill.
Static only: no compiler, CTest, Gradle, AAPT2, Lint, terminal or device run
exists.

## I2PSnark continuation ownership/state (2026-10-19)

The historical checkout at that increment was `c8f2eba1`, initially clean.
The older baseline description above is historical. Current torrent/native
Android changes are implemented by static analysis only, pending CI compilation
and pending CI tests. Task inventory: SNARK-001..SNARK-010 in TODO; functional
and protocol matrices: `docs/torrent/snark-compatibility.md`.

The native disk creator, persisted file priorities and opt-in negotiated
`ut_comment` exchange are now implemented by static analysis. Keep consent,
private-swarm suppression, rate limits, empty bencode fields and original info
bytes intact. TUI and Android controls exist; do not repeat this work based on
older matrix descriptions. Advanced creation uses automatic/explicit piece
length, privacy, tracker tiers, cancellation and progress.

The real payload storage worker is opt-in (`isolatedstorage=true`) on
Linux and fails closed if requested isolation is unavailable. Root FD,
read-only/writable numeric handles and bounded single-flight IPC are the
capability boundary. Parent metadata/allocation/promotion and router writes
remain; Windows backend and network/session/verifier processes are pending.
OpenBSD supervision and Android SAF payload integration are described below. Do not describe this as global privilege separation.

SAF document/tree creation exports native v1 metainfo with bounded incremental
hashing. URI-backed payload downloads/seeding now use the common backend;
broker/process recovery and provider fixtures now exist statically;
end-to-end device validation remains pending. Preserve URI authority instead of manufacturing filesystem
paths. Full local state schema and storage worker boundaries are documented
in `docs/torrent/local-state.md` and `docs/torrent/storage-worker.md`.

That continuation started from clean `0cf32519`; its changes remained local
and pending CI compilation/tests. Preserve the user-requested descriptive
license-reference headers and all existing notices/provenance. The reference
excerpts outside this repository are not a complete verified clone. No task is
validated without its corresponding manual CI pass.

## Selective upstream openssl integration (2026-10-19/20)

The review covers `PurpleI2P/i2pd:openssl` through
`de32c8c3f580c2c43ca09154ecfe6a20db80d19f` for commits dated 18–20 October
2026 UTC. The exact disposition, timezone/merge rules, omissions and rationale
are in `docs/upstream-sync.md`; task tracking is `UPSTREAM-001..UPSTREAM-005`
in `TODO.md`. The fork keeps its C++23, privilege-separation, native
TUI/Android, CMake and release identity.

The implemented corrections are the explicit SSU2 PathChallenge state guard,
the I2PTunnel response/IRC `view()` and header-compaction fixes, the NetDb
0.9.57 general floor with the 0.9.62 high-bandwidth floor retained, the torrent
stopped/removed persistence guards, the Intel AMT reserved-port table and
regression, the reseed sample wording without weakening fail-closed
verification, and the guarded RPM Boost branches. The browser-console
commit, upstream 3.0.0 release metadata and obsolete GNU make/Jammy packaging
are intentionally omitted. All changes are static-only pending manual CI
compilation and tests; no build, CTest, sanitizer, package or live-peer evidence
exists for this increment.

## Native payload continuation (2026-10-20)

The historical checkout for that increment was `0cf32519`; existing
uncommitted changes were preserved. The native payload abstraction now connects
Android SAF document read/write and seeding through a non-exported
separate-process AIDL/JNI broker.
Persisted grants key tunnel+infohash; final-name document IO skips POSIX payload
allocation/promotion. Startup rehashes provider pieces. No URI-to-path fallback
or unlimited staging is allowed. Unsupported seek fails explicitly. Binder
death/timeout now retires the old process and recovers through generation-fenced
rebind; cancellation and fault-provider fixtures are implemented statically. Earlier SAF-export-only statements
above describe the previous increment.

OpenBSD storage startup now uses a pre-pledge root-authorized supervisor. The
worker inherits root fd4 at exec and installs its own sandbox before requests;
only a UNIX socket is returned via SCM_RIGHTS. Never send a directory FD after
pledge: sendfd/recvfd prohibit it. Core requires recvfd, no additional proc/exec.
Broker failure needs daemon restart; replacement of a worker is explicit.

Pending writes are withheld from HAVE/bitfield/serving/promotion; failed writes
invalidate the piece. Isolated native allocation is delegated. Parent router/DHT persistence and network/core/verifier split remain pending;
this is not global privilege separation. New tests are prepared, not executed.
All this increment is implemented by static analysis, pending CI compilation
and pending CI tests. First-party headers use only the short LICENSE reference.

## Binder recovery and narrowed persistence authority

SAF client IDs now remain local across broker generations; URI/path/length/mode
are immutable authority. Rebinding requires actual death of an old writer;
stale callbacks/results cannot authorize another tree. Cancel/retire is oneway
and independent of IO locks, with CancellationSignal plus bounded process
retirement for ignored cancellation. Do not automatically replay ambiguous
writes. Cleanup of stale handles must not wait/rebind once per file.

Native isolated mode delegates promotion, exact-file deletion, empty-directory
removal and atomic metainfo/resume/settings replacement to storage workers.
Metadata paths are narrow and capped at 8 MiB with 16 KiB chunks; fixed
staging slots cap crash leftovers. Keep DHT/router persistence,
network/router/verifier processes and global enforcement open. No global
separation claim is justified.

All new recovery/lifecycle/provider tests are prepared but unexecuted; current
changes remain implemented by static analysis, pending CI compilation and tests.

## DHT/router persistence and SAF administrative completion

Isolated tunnels now delegate DHT load/save under the original torrents state
root, preserving 54-byte records and explicit legacy flat filename authority.
RouterContext four fixed-file writes use a transport-independent callback;
privsep.routerstorage initializes the network-denied worker before context.Init.
No installed-writer failure can fall back to core filesystem writes. OpenBSD
workers inherit root at exec and drop new UNIX/fd-reception authority after
bootstrap; do not reintroduce directory SCM_RIGHTS or execpromises.

SAF exact document deletion and metainfo export are now brokered, cancelable
operations. Close cached descriptors before deleting exact capabilities, verify
identity/size and never recursively delete a selected tree. Tests are prepared
only. Global netDb/config/profile/client persistence, networking/router/verifier
process separation and cross-platform completion remain implementation tasks;
never relabel all remaining work as pending tests.

## Current Java comparison reference (updated 2026-10-21)

The comparison target is I2P Java `i2p-2.14.0`, peeled commit
`3c98503c5f14efe7bdd783528ba7f621f12d8081` (official annotated tag object
`9db233ed7241599eba76f29b8c334eaaf3db2e87`, verified by remote ref inspection).
Earlier Java 2.13.0 audits, source-line citations and vectors remain historical
evidence; they have not been re-audited or validated against 2.14.0. Never
relabel their provenance. The current-reference check and historical fixture
provenance check use separate external checkouts. NET-026 tracks the 2.14.0
review and evidence migration; no Java checkout/build/test is implied by
verifying remote refs. Do not describe 2.13.0 as the current comparison target.

## Java feature-parity continuation (2026-10-20, historical baseline)

The reference for that earlier pass was `i2p-2.13.0`, commit
`9134f808337b401e8e53c73734c81fab04280c9d`. Seven
read-only recognition passes produced `docs/FEATURE-PARITY.md`. The in-scope
static increment adds SSU2 migration retries, runtime ML-KEM probing, RSA/SU3
verification and a fail-closed reseed default, SAM bounded pipelining/control
grammar, I2CP identity-hash routing keys, case-preserving address-book
normalization, torrent re-verification/tracker/PEX/connection bounds and a
typed daemon-owned TUI admin queue. Android reseed/runtime log-level parity is
implemented statically with service-worker JNI and a static instrumentation
consistency test.

Everything in this increment remains implemented by static analysis only,
pending CI compilation and pending CI tests. No build, CTest, sanitizer,
fuzzer, Java peer, Android device, service, OpenBSD, Windows or live-network
validation exists. Native tunnel/address-book administration, selected-file
promotion, durable queue/bandwidth scheduling, secret-blinded I2CP, Android SAF
recovery and genuine network/router/verifier separation remain implementation
tasks, not test-only work. Do not claim global privilege separation.

## Agent Skills integration status (2026-10-20)

The local `.agents/skills/` collection is implemented by static analysis:
one discovery index and 17 subsystem skills, each with valid frontmatter and
`references/` checklists. `agent-skills-static` validates the exact directory
inventory against the README, frontmatter/name/description bounds, required
sections including `Known issues and decisions` and `Do not`, `AGENTS.md`
references, reference links, orphan references and LICENSE headers without
executing a skill. Adding, renaming or
removing a skill requires updating `tests/check-agent-skills.cmake`
`expected_skills` and `.agents/skills/README.md`. The permanent policies above
remain authoritative; skills link to them and do not authorize local builds,
tests, workflow dispatches, commits or pushes. Documentation-link, branding and
agent-skills checks are pending manual CI; the collection is not dynamically
validated.

## Client and persistence continuation (2026-10-21)

UPSTREAM-003 and UPSTREAM-004 are implemented by static analysis, pending CI
compilation and pending CI tests. HTTP tunnel connections use the bounded
`I2PTunnelHTTPHeader` seam with independent request/response ownership; the
new test and extended HTTP fuzzer are prepared. Torrent removal tombstones
revoke instance authority, reserve the infohash until serialized cleanup
succeeds and delete native settings in both backends even when retaining
payload. Failed cleanup blocks re-add until daemon restart. Final shutdown
snapshots have a narrow stopping/nonremoved exception to live membership;
their dedicated shutdown regression remains pending.

SEC-015 binds pending address-book lookup nonces to authenticated resolver
identities, caps/ages the registry and rejects wrong-port replies. Prepared
HTTP, address-book and torrent regressions are registered in the existing
manual workflows; no workflow was dispatched. Windows path-check/delete
TOCTOU, direct payload deletion races, broader client lifecycle, full Java
parity and global network/router/verifier separation remain open. No local
build, test or project validator was run. No commit or push is authorized for
this continuation.

## String safety continuation (2026-10-21)

SEC-016 is implemented by static analysis, pending CI compilation and pending
CI tests. First-party C++ sources no longer call `sprintf`, `strcat`, `strcpy`,
`strncpy` or their unbounded variants. `AddressFormat.h` provides bounded
IPv4/IPv6/scope-ID and endpoint formatting; `LogPrint` overloads avoid
Boost.Asio's unsafe scope-ID formatter; `BOOST_ASIO_HAS_SNPRINTF=1` selects
Asio's bounded `snprintf` fallback while project formatting avoids its
remaining `strcat` path. `CopyString` replaces the remaining fixed-buffer
copy, `I18N` uses a checked single-buffer formatter, and SOCKS domain buffers
reject zero-length/overflow input. No `strlcpy`/`strlcat` fallback is added
because the supported platform minima guarantee the selected primitives.

Prepared tests cover address/endpoint scope IDs, small/zero-capacity buffers,
embedded NULs, i18n formatting and SOCKS length limits; `build-system-static`
also rejects the unsafe call family. No local build, test, sanitizer, fuzzer
or project validator was run. These changes are not dynamically validated.

## Functional parity continuation (2026-10-21)

PARITY-009..011 are implemented by static analysis, pending CI compilation and
pending CI tests. Read-only subagents reviewed router/tunnel policy, address
book, client services/proxies, administration/TUI/Android and I2PSnark against
the pinned I2P Java 2.14.0 sources; no Java tree is vendored or executed.

Native address-book list/add/replace/remove is exposed through bounded TUI IPC
and an app-internal Android screen. Only full base64 destinations are accepted;
shared identity files are retained while another name references them, and an
explicitly emptied index survives restart. Subscription editing and
deleted-host tombstones remain open.

I2PSnark force recheck is exposed through `TORRENT-RECHECK`, TUI `V` and an
Android action, requiring a paused complete torrent. Token-bearing
DatabaseStore messages that arrive through a router tunnel are dropped before
DeliveryStatus construction, matching Java's exploratory-tunnel policy.

Prepared regressions: `test-addressbook-admin`, `test-tunnel-dsm-policy`,
`TORRENT-RECHECK` protocol parsing and the Android `TorrentControl` recheck
test. No compiler, CTest, Gradle/device, sanitizer, fuzzer, validator, live
lookup/tracker/peer or workflow dispatch was performed. Tunnel creation/editing,
selected-file promotion, queue/bandwidth policy and live Java 2.14.0/device
validation remain open.

## Upstream openssl continuation (2026-10-21)

UPSTREAM-006 is implemented by static analysis, pending CI compilation and
pending CI tests. The reviewed tip is
`9725b8b227293a796c069ac270fa6b5b47cf9a13`, six commits after the previous
`de32c8c3` mark. Proxy-generated errors use typed 400/431/500/502/504 statuses;
the fork-only 1 MiB header limit maps to 431. Torrent DHT rendezvous
self-announce was adapted with `DHTTorrent::GetPeers()`,
`RoutingTable::IsInOurBucket()`, private/stopped suppression, `MAX_DHT_TORRENTS`,
peer caps and the seed bit. Mixed-case HTTP tunnel headers are pinned by tests.
The RPM release-spec change is partially integrated/obsolete, the merge commit
is merge-only, and Windows web-console packaging is incompatible with the
removed browser UI.

No local compiler, CTest, sanitizer, fuzzer, package, proxy/DHT/peer run,
workflow dispatch, commit or push was performed.

## OpenSSL 4 and C++23 ownership continuation (2026-10-21)

CPP-012..CPP-014 are implemented by static analysis, pending CI compilation and
pending CI tests. CMake reads the selected `opensslv.h`, enforces OpenSSL
3.0.2+/LibreSSL 4.0+, exposes `I2PD_OPENSSL_EXPECT*` guards and runs compile-only
provider/X.509/TLS probes. OpenSSL must satisfy the modern probes; LibreSSL keeps
the small feature-selected fallbacks. Runtime EVP capability checks and
`CanPublishPQ`/`HighestSupportedPQVersion` prevent advertising an unavailable
algorithm or ML-KEM parameter set. `famtool` uses checked EVP signing and no
deprecation suppression; TLS contexts use explicit client/server roles.

The C++23 continuation converts `TunnelConfig` hop ownership, removes the
Garlic shared payload buffer, moves `Piece` storage and the UDP resolver thread
to RAII/atomics, and makes `Families` OpenSSL ownership exception-safe. Address
book index replacement is atomic and bounded; Android recheck/tracker parsers
reject the engine's `0` sentinel. No compiler, CMake configure/build, CTest,
sanitizer, fuzzer, package, Android/Gradle/device, live TLS/PQ or workflow run
was performed. Do not describe these changes as validated.

## Memory-safety audit continuation (2026-10-21)

Static audits covered SSU2/NTCP2, ratchet/Garlic, netDb, torrent, IPC and JNI.
Fixed by static analysis: family-signature fail-open, router garlic and
DeliveryStatus bounds, Garlic clove pointer/length checks, NTCP2 SipHash setup
failure handling, torrent piece/DHT empty-vector bounds, offline-key chaining,
truncated reseed names, HTTP control characters, SAF/JNI pending-exception
ordering, Android start re-entry and TUI/IPC bounds. `SEC-017..SEC-024` are
implemented or partially implemented statically pending CI. `SEC-025`
remains open for SSU2 pool lifetime and address/endpoint races, fragment
byte budgets, RouterInfo
copy-on-write, ratchet/destination crypto-context serialization, datagram
receiver lifetime and residual JNI/IPC teardown contracts. No compiler,
sanitizer, fuzzer, device or workflow validation exists; do not claim these
findings dynamically fixed.

## Concurrency and lifecycle continuation (2026-10-21)

A second static pass covered callbacks/timers, SSU2/NTCP2, netDb, BitTorrent,
IPC workers and JNI. `CONC-001..CONC-004` are implemented by static analysis:
`RunnableService` no longer self-joins, `Transports` has atomic/idempotent
lifecycle and ordered timer/server teardown, `I2PControl` restarts its
io_context, daemon shutdown flags are atomic, tunnel-build decrypt uses
caller-owned Noise state, `Families` uses a shared mutex, and SSU2 has a
terminal latch that rejects late handshake packets. `CONC-005` remains open for
raw `this` torrent callbacks, datagram receiver ownership, RouterInfo/profile
copy-on-write, disk-domain file ownership, SSU2 address/endpoint publication and
Android owner epochs. No compiler, TSan/ASan, device or live-peer validation
exists.

## Build/dependency continuation (2026-10-21)

`BUILD-014..BUILD-017` are implemented by static analysis, pending CI:
POSIX make targets are phony, default to `all`, build before install and
forward `DESTDIR`/`PREFIX`; CMake requires 3.24, rejects Unix Makefiles on
OpenBSD, has Ninja presets in `build/`, complete `Development` install components,
relocatable `configure_package_config_file`/`@PACKAGE_INIT@` package paths,
an explicit `I2PD_TUI_REQUIRED` ncursesw requirement, architecture-independent
systemd units and header-only Boost.JSON/URL modelling. Unused CMake modules
and obsolete RPM ENGINE/old build-dir branches/static-package/Docker dependency
paths were removed or corrected. Manual
CI includes per-component install/consumer validation, an OpenBSD TUI
assertion and instrumented fuzz libraries. No configure/build/package/workflow
dispatch exists; do not claim these changes validated.

## Address-book and I2PSnark parity continuation (2026-10-21)

`PARITY-012..PARITY-014` are implemented or recorded by static analysis,
pending CI compilation and pending CI tests. Deleted-host tombstones are
persisted in bounded `deleted.txt`, matched with an anchored wildcard matcher,
applied to subscription merges, cleared by an explicit add and exposed through
TUI `ADDRESSBOOK-BLOCKED`/`ADMIN ADDRESS-BLOCK|UNBLOCK` and Android
block/unblock dialogs. I2PSnark now preserves bounded unknown top-level metainfo
(creation date, url-list, encoding and extension keys) across tracker and
metadata rewrites; comment/created-by editing is exposed on paused torrents via
TUI and Android, and Android supports remove-with-data. Further Java 2.14.0
candidates are tracked in `docs/FEATURE-PARITY.md` §10 and `PARITY-014`.
Reseed `netid`/multi-source acceptance, Streaming admission counters, SAM 3.3
`LISTEN_PROTOCOL` and local HTTP Basic/SOCKS5 authentication are implemented
statically; Streaming `limitAction`, HTTP Digest, server-tunnel filters, NetDb
admission/refresh, operator blocklist, peer/tunnel inventory and selected-file
promotion/bandwidth scheduling remain open. No compiler, CTest, sanitizer,
fuzzer, Android/Gradle/device, Java peer or workflow validation exists.

## Upstream openssl post-tip verification (2026-10-21)

`UPSTREAM-007` is verified/implemented by static analysis, pending CI
compilation and pending CI tests. Read-only remote inspection confirms
`9725b8b227293a796c069ac270fa6b5b47cf9a13` is still the `openssl` branch tip and
there is no later commit. The reviewed `de32c8c3..9725b8b2` six-commit
continuation remains the complete window. Fork-side integration gaps from
`9725b8b2` were closed: self-unannounce on stop/remove, a reserved local DHT
peer slot, removed-torrent suppression, upstream empty-bucket predicate
fallback, and test-visible proxy status constants for 400/431/500/502/504.
The bounded I2PTunnel HTTP parser now rejects obs-fold continuations and pins
mixed-case Proxy-Connection. `137dde2f` is recorded as having no applicable
hunk, including its false-positive RPM `0%{?rhel} <= 9` condition. No compiler,
CTest, sanitizer, fuzzer, live proxy/DHT/peer or workflow validation exists.

## SSU2/NTCP2 continuation (2026-10-21)

`NET-162`/`NET-163` are implemented by static analysis, pending CI compilation
and pending CI tests. SSU2 receive/ACK state is monotonic after late packet 0;
unauthenticated wrong-type packets cannot trigger handshake retransmission;
session removal is identity-checked; failed SessionCreated discards Noise
state; resend throttling is corrected; PeerTest msg 1 validates the authenticated
source address and endpoint; PeerTest/RelayIntro reject private/reserved targets;
PeerTest timestamps use the Java ±120 s window; RelayIntro HolePunches are
rate-limited; and late `Established` callbacks are posted rather than lost.
NTCP2 uses the Java 2.14.0 459..4048 msg3p2 bounds and applies banned
termination to the peer profile. NTCP2 options/padding and delayed-close fail
state remain tracked. No compiler, CTest, TSan/ASan, fuzzer, live peer or
workflow validation exists.

## Cryptography continuation (2026-10-21)

`NET-164`/`CRY-215..CRY-225` are implemented by static analysis, pending CI
compilation and pending CI tests. CSPRNG seeds and fail-closed peer ordering,
multi-buffer AEAD failure handling, ratchet/AES/NTCP2 zeroization, the full
0..65535 tag boundary with eviction and NEXT_KEY limits, automatic NSR/ACK
fallback on every `ClientDestination`, LS2 RSA-transient policy, Ed25519 point
validation, GOST RFC 7091 `e=1` normalization, SSU2 KEM/version fallback,
fail-closed `i2cp.leaseSetEncType`, and I2CP private-key length validation are
in the worktree. The NSR timer lifecycle test, the deliberately stricter empty-clove NS policy,
provider PQ transcripts and all dynamic evidence remain pending. No compiler,
CTest, sanitizer, fuzzer, Java peer, OpenSSL/LibreSSL provider or workflow run
exists; do not describe these paths as validated.

## netDb/LeaseSet continuation (2026-10-21)

`NET-165`/`DB-115..DB-123`/`LS-122..LS-125` are implemented by static analysis,
pending CI compilation and tests. Clove origin is preserved for direct-only
DLM/DSM policy; client DSRMs are bound to queried peers; lookup/flood throttles
are bounded; fresh zero-address RIs are storable but unselectable; RouterInfo
persistence and file replacement are atomic; reseed preserves self and is
fenced by Stop; encrypted LS2 rejects RSA blinded types; LS2 PopulateLeases is
a no-op; Meta expiry follows the latest entry; LS1 buffers are canonicalized.
Reply-key 0x12 remains intentionally fail-closed (no Java 2.14.0 sender/KDF).
No compiler, CTest, sanitizer, fuzzer, live floodfill/Java peer or workflow run
exists; do not describe these paths as validated.

## Tunnel-build/I2NP/Garlic continuation (2026-10-21)

`NET-166`/`TU-102..TU-106`/`I2NP-006..I2NP-010`/`GAR-001..GAR-004`/
`CONC-006`/`CONC-007` are implemented by static analysis, pending CI
compilation and tests. Duplicate peers, zero transit IDs, zero-hop/variance
publication, shared-RNG shuffling, OBEP LOCAL delivery, clove type whitelists,
gateway sizing/inner expiry, IV replay/reassembly caps, clove freshness,
fallible encryption and custom-selector/config ownership were corrected. Tunnel
keys, build records and hop Noise state remain core-only; no multiprocess
boundary was widened. No compiler, CTest, sanitizer, fuzzer, live tunnel/Java
peer or workflow run exists; do not describe these paths as validated.

## SAM/I2CP/Streaming/Datagram continuation (2026-10-21)

`NET-167`/`SAM-117..SAM-121`/`I2CP-109..I2CP-111`/`STR-108..STR-112`/
`DG-108..DG-110` are implemented by static analysis, pending CI compilation and
tests. Per-port accept teardown, subsession validation, SESSION STATUS errors,
HELLO tokenisation, balanced shutdown, blinded HostReply store-key checks,
single terminal I2CP status, staged CreateLeaseSet2 keys, bounded saved packets,
locked stream lookup, ping/pong ports, SYN validation, terminated-send guards,
Datagram3 authentication and identity refresh were corrected. I2CP key/owner
publication under `i2cp.singlethread=false` remains a TSan residual. No
compiler, CTest, sanitizer, fuzzer, Java peer or client run exists; do not
describe these paths as validated.

## User-services continuation (2026-10-21)

`PARITY-015`/`HTTP-101`/`SOCKS-101`/`RS-102..RS-106`/`AB-101..AB-105` are
implemented by static analysis, pending CI compilation/tests. HTTP/SOCKS use
case-insensitive I2P classification and refuse loopback/private clearnet
targets; SOCKS accepts IP literals and enforces port/private rules; address
book adds `.i2p.alt`, `www.` fallback, bounded subscriptions and tombstone
enforcement; reseed verifies TLS chain/hostname and bounds SU3/ZIP/entry sizes
and freshness. HTTP/SOCKS authentication, full response-header limits,
server-tunnel TLS config, changed-section reload, per-tunnel inventory/start-
stop and Android replace UI remain open. No browser/web console was introduced.
No compiler, CTest, sanitizer, fuzzer, Java peer, live proxy/reseed or device
run exists; do not describe these paths as validated.

## Multiprocess trust-boundary continuation (2026-10-21)

The supervisor/storage/logging continuation is implemented by static analysis,
pending CI compilation and tests. The Linux supervisor now uses
`ApplyLinuxSupervisorSandbox` (no-new-privs and capability/bounding-set drop)
rather than installing inherited Landlock/seccomp/network-namespace domains;
each exec'ed worker applies its own role profile so storage and network
children retain their delegated capabilities. Log-worker replacement uses
bounded exponential backoff/cap/stable reset, and the logger queue is bounded
with a dropped-record counter.

The OpenBSD torrent-storage broker receives a validated root as inherited fd4
and uses `pledge("stdio unix proc exec sendfd recvfd")` without `rpath`; the
startup pledge keeps `exec` only for the worker-spawn window. Router storage
treats worker-level rejection separately from transport failure. Worker
channels must be descriptor 3, so they cannot alias inherited root descriptor
4. Prepared regressions cover supervisor bootstrap delegation, queue
saturation and transient storage failure.

Do not claim global privilege separation. The core still owns transport
sockets, most netDb/profile/address-book/destination-key persistence and
long-term keys. Network and crypto workers remain policy-builder-only; no
single supervisor owns all workers. No compiler, CTest, sanitizer, fuzzer,
OpenBSD/Linux/Windows runtime, Android device, package, live-peer or workflow
run exists.

## IPC protocol consolidation (2026-10-21)

`PRIVSEP-002/013/015` are implemented by static analysis, pending CI
compilation and pending CI tests. Protocol v2 (minimum v1) adds typed `Error`
replies and a shared endpoint operation allowlist; path fields use fallible
bounded encoders. Log configuration is acknowledged by a bounded Ping/Pong
handshake. Storage request IDs are strictly increasing and storage operation
failures return typed errors without poisoning the channel. The OpenBSD broker
reports capacity/path/replaced-root rejection without killing live workers and
adopted channels are revalidated. Empty seqpacket records are not EOF;
`imsgbuf_read` buffered frames are drained; Windows oversized records terminate
the pipe and `AcceptBound` binds PID/session.

Residual: no automatic storage-client reconnect/generation fencing, explicit
transaction cancellation IDs, complete Channel error taxonomy, Windows daemon
wiring or dynamic validation. Do not claim the IPC consolidation validated.
No compiler, CTest, sanitizer, fuzzer, OpenBSD/Linux/Windows runtime, device or
workflow run exists.

## OpenBSD supervisor/storage continuation (2026-10-21)

`OPENBSD-SBX-6/7` are implemented by static analysis, pending OpenBSD CI
compilation and tests. The storage broker no longer performs `openat()` after
pledge and needs no `rpath`; it passes the immutable root at fd4 and the worker
walks/validates the relative delegated path before applying pledge/unveil.
Forwarded workers are disowned so broker exit cannot kill in-flight fsyncs;
detached children are reaped and launches use a replenished one-minute window.
A broker timeout is not treated as a dead broker.

Storage policy now requires a non-empty absolute unveil root and drops unused
`fattr`/`flock`. `ImsgChannel::AdoptValidated` validates socket type, CLOEXEC
and `getpeereid` before enabling fdpass, preserves caller descriptor ownership
on compose failure and poisons on send timeout. Network policy has explicit
`socketHandoff`/`routeInfo`/optional DNS flags and `ValidatePreopenedSocket`,
but no transport backend is claimed. Networking/crypto separation, core
`inet` removal and router storage under the broker remain pending. No OpenBSD
runtime, compiler, CTest, sanitizer, fuzzer or workflow run exists.

## Linux multiprocess hardening continuation (2026-10-21)

`PRIVSEP-016` is implemented by static analysis, pending CI compilation/tests;
`PRIVSEP-017` remains pending. Linux adopted channels check AF_UNIX/seqpacket,
reject listeners and enable SO_PASSCRED; empty records require credentials and
are flood-bounded. Worker exec sets `PR_SET_NO_NEW_PRIVS`. Seccomp denies
process-memory, pidfd, handle, userfaultfd, bpf and perf syscalls in addition to
ptrace/io_uring/socket. The close() fallback uses RLIMIT_NOFILE, and supervisor/
log worker close inherited socket stdio. Storage roots carry dev/inode identity; both clients keep O_RDONLY
directory descriptors because the worker fsyncs parents. Linux rejects the
OpenBSD-only inherited-root roles.

Residual: deferred pidfd reaping, total stop budget, cgroup v2/kill,
kill/tgkill self-filtering, descriptor classification and actual network
worker separation. Kernel floors/fallbacks are documented in
`docs/security/process-capabilities.md`. No Linux runtime, compiler, CTest,
sanitizer, fuzzer or workflow evidence exists; do not describe it as validated.

## Windows multiprocess hardening continuation (2026-10-21)

`WIN-003` is implemented by static analysis, pending Windows CI compilation and
tests; `WIN-004/005` remain open. Message-mode Named Pipes remain the Windows
transport; ncalrpc/MIDL is not migrated. Overlapped I/O is cancelled and
drained before release, timeouts/malformed frames close the channel, rejected
peers re-arm the listener, ConnectBound authenticates server PID/session, and
per-spawn pipe names use BCryptGenRandom. Restricted tokens delete all parent
privileges, add LUA_TOKEN for elevated parents, and Job Objects carry
exception/process/job memory and basic UI limits. ConfigureLogFd is rejected on
the descriptor-less Windows transport.

Residual: no Windows `--privsep-worker` entry/supervisor/daemon wiring, no
AppContainer/LPAC token construction, no per-role storage/network ACL or WFP
enforcement, and no Windows runtime/cross-user/AppContainer evidence. Do not
describe Windows separation as validated.

## Android multiprocess/SAF continuation (2026-10-21)

`AND-020/021` are implemented by static analysis, pending manual Android CI and
device tests; `AND-022` remains pending. JNI inbound strings are UTF-16-decoded
with NUL/surrogate rejection; start diagnostics are bounded. SAF tree selection
validates grants and releases replaced ones, export grants are persisted for the
duration of the write, close/release no longer retire the broker on timeout,
broker close is queued, cancellation is operation-scoped, created document names
are checked, and display-name `:`/`\` are accepted. Activity destruction still
does not stop transfers (`stopWithTask=false`, persisted `should_run`), and
`MY_PACKAGE_REPLACED` resumes only an already-requested router.

Do not claim Android UID/process separation: a second `android:process` shares
the UID, `isolatedProcess` cannot retain persisted SAF grants, and INTERNET is
per-UID. Router AIDL, session-scoped SAF authorization, moving document import/
creation into the broker, a minimal SAF seccomp library and device/CI evidence
remain pending. No Gradle/NDK/Lint/device run exists; do not describe it as
validated.

## Static compilation/linkage audit continuation (2026-10-22)

The audit starts from published `e669765d`; BUILD-018 and TEST-023 remain
Implemented / static, Pending CI compilation and Pending CI tests. Six
specialized reconnaissance areas and a second test review cover the recent
C++23/protocol/IPC/torrent/TUI/tools/Android/build seams. The detailed evidence
and configuration limits live in `docs/static-build-audit.md`.

Boost.JSON/URL require compiled targets and matching installed dependency
discovery; they are not supplied by public headers alone. Translation helpers
are canonical `i2p::i18n::tr`/`ntr` with global compatibility aliases. Keep the
Landlock stable UAPI layout/runtime rights and absent-header fail-closed path;
old build headers must not silently remove kernel-supported restrictions.
Windows header SDK/macros are shared target requirements; PE hardening flags
must not be replaced by ELF linker options. Pinned Material 1.14.0 supports
both text and resource-ID hint overloads; verify dependency source before
classifying an API call from memory.

These are static corrections only. No compiler, configurator, test, project
validator, workflow dispatch, commit or push occurred for this increment.
ANDROID-017 subsequently corrects native startup ownership by static analysis;
ANDROID-018 tracks partial-init cleanup. Neither this audit nor source/fixture
fixes establish runtime or global privilege separation.

## Independent development continuation (2026-10-22)

CONC-009 synchronizes individual `RouterProfile` state, returns canonical cache
instances and endpoint copies, serializes per-profile Save/Load and retains
dirty updates on write failure. Map eviction requires sole shared ownership;
callers must retain shared_ptr ownership while accessing a profile. Global
profile-root initialization and delete-versus-save coordination are addressed
by CONC-010 below; do not claim complete persistence isolation.

ANDROID-017 uses `DaemonLifecycle.h` as the sole native start-transition owner
under `NativeCallGuard`; the adapter must invoke `Daemon.start()` while the
owner has published Starting. The helper does not solve generic partial-init
cleanup (ANDROID-018). TOOL-012 keeps IPv4 legacy firewall fragments unchanged
and explicitly prefixes IPv6 with `ip6tables`; positive CLI fixtures are signed
at CI runtime by a non-installed helper, with no checked-in private keys.

All three slices are Implemented / static, Pending CI compilation and Pending
CI tests. Host lifecycle tests do not establish JNI/device behavior. No local
build, test, project validator, workflow dispatch, commit or push was performed.

## Profile storage and failed-init continuation (2026-10-22)

CONC-010 fixes the profile root for the process lifetime and serializes storage
IO/expiry cleanup. Save uses flushed same-directory atomic replacement; POSIX
rename followed by failed directory fsync is an ambiguous durability result and
retains dirty state. NetDb shutdown drains postponed updates after its producers
stop. Embedders must stop their producers and provide exclusive data-directory
ownership. Cross-process enforcement, crash-left staging cleanup, hostile
ancestor/reparse changes and arbitrary retained mutators remain open. Never use
arbitrary-PID liveness probes after OpenBSD drops `proc`, or widen pledge for
staging cleanup. Unknown staging files must not be deleted on an assumption
of exclusivity that the caller has not enforced.

ANDROID-018 adds a common `DaemonInitTransaction` for logging, crypto, router
storage and router context. Register phases before acquisition; unwind context
persistence before revoking storage authority, retain the failing writer on
isolation failure, and reset the stopped logger destination for retry. This
only releases owned resources: durable identity/configuration and irreversible
pledge/unveil effects remain. Synthetic ledger tests do not execute production
rollback or establish cross-configuration retry safety.

Both slices are Implemented / static; Pending CI compilation; Pending CI tests.
No local build, test, project validator, workflow dispatch, commit or push ran.

## Receiver, queue and service-owner continuation (2026-10-22)

CONC-011 replaces Datagram port-only resets with identity-bearing registration
tokens. Keep tokens in their actual receiver owner and reset before releasing
raw callback targets. Registration requires a shared-owned DatagramDestination.
Reset outside receiver callbacks deactivates and drains after registry unlock;
reset from any receiver callback returns false without waiting to avoid
same-slot/cross-slot cycles. Such reset is not a destruction barrier. Preserve
historical port-specific wildcard fallback and release replaced callback
objects outside registry locks. Internal callers are migrated; no wire change.

CONC-012 coordinates tunnel/netDb admission with enqueue and shutdown close,
checks worker stop after waking, and drains stopped-generation messages after
join. NetDb clears both throttle maps under their mutex. Producers must be
quiesced across restart because posts have no generation token. CONC-010 expiry
now recognizes only canonical profile hashes at their exact shard and excludes
symlink leaves, staging, backups and unrelated files. Trusted hierarchy and
exclusive process ownership remain requirements.

ANDROID-019 uses owner-gated Executor views over one process FIFO. Submission
and retirement/barrier insertion share a short monitor. Accepted work drains
ahead of the old owner's mandatory stop, then new-owner work runs; old-owner
submissions reject and duplicate stale retirement cannot stop a newer owner.
Callbacks are epoch-fenced. Never run JNI/commands under the owner monitor,
shut down the process FIFO from a Service, or bypass NativeCallGuard.

TOOL-013 clears CDPATH during static-package root capture and makes SAM reply
mismatches exit nonzero. Manual packaging CI ShellChecks maintenance scripts
and uses a fake nc for error propagation; this does not establish live SAM
interoperability. All slices are Implemented / static; Pending CI compilation
and/or Pending CI tests. No local compiler, test, project validator, workflow
dispatch, commit or push ran.

## Static compile/link audit continuation (2026-10-25)

A read-only, multi-subsystem static audit followed the cancelled manual runs.
Uncommitted corrections close the `daemon/I2PControl.cpp` namespace brace and
`steady_timer::cancel()` errors, add direct `<cstdio>`/`Crypto.h`/OpenSSL
includes, replace `ip::port_type` (Boost 1.76+) with `unsigned short`, remove
dead `NetDb::ManageRequests` and client UDP cleanup declarations, and remove
the warnings from `HTTPProxy`, `RouterInfo`, `Reseed`, `LinuxSandbox` and the
torrent metainfo builder.

Build-graph corrections: a `CheckLanguage(RC)` probe (Windows platform modules
normally enable RC themselves) and the `version.h` include for the Windows
resource script, `CURSES_LIBRARY` (not the optional ncurses name)
for the wide-entry probe, `I2PD_HAVE_SSL_SET1_DNSNAME` and
`I2PD_HAVE_OPENSSL_MLKEM_H` probes with `Crypto.h` fallbacks,
`I2PD_INSTALL_CONFIG` for packagers that ship their own examples, and an APK
assertion for the relocated reseed certificates.

Torrent corrections: administrative start/stop/reannounce posts hold
`weak_from_this().lock()` instead of raw `this`, the no-destination shutdown
path stops owned storage workers, and `CreateTorrentFileContent` keeps
generated values in a `std::deque` so stored views cannot dangle. TUI startup
retries a UTF-8 `LC_CTYPE` and undefines every `KEY_Fn` macro it maps.

Reseed parity: `AppendReseedNetId` adds Proposal 147's `netid` parameter to
SU3/directory URLs, and server selection uses Java 2.14.0's two-server,
100-router-info acceptance threshold. `test-reseed-zip-bounds` covers URL
composition. Streaming admission uses fixed per-peer/total minute/hour/day
windows shared per `ClientDestination`; it keys on the ratchet static key
(Java uses the destination hash) and `limitAction` rejection bytes remain
open. `test-streaming-throttle` covers the counters. SAM 3.3
`LISTEN_PROTOCOL`/protocol-aware RAW dispatch (`test-sam-subsessions`) and
local HTTP Basic/SOCKS5 RFC 1929 authentication (`test-proxy-auth`) are
implemented statically; HTTP Digest remains pending reference verification.

Modernization continuation (static): `modern::ScopeExit` replaces the removed
`Defer`/`SaveStateHelper` helpers, `i2p::expected` provides
`value_or`/`and_then`, memory pools use typed deleters instead of
`std::function`/`std::bind`, and the I2NP clove allowlist is a `constexpr`
table. Wire/persisted formats, IPC/log tokens and security policy are
unchanged. `test-modern` covers the new vocabulary.

Memory/concurrency continuation (static): `Stream` holds a strong
`StreamingDestination` and `Stop()` runs on the owner io_context; the
default/per-port streaming destination registry is mutex-guarded; `SSUExt`
introducers are snapshotted under a mutex for SSU2 readers; `TorrentFile`
serializes handle/authority changes; SOCKS defers pipelined input behind the
RFC 1929 reply and both proxies bound their handshake; `ServiceAcceptor` caps
concurrent handlers; `I2PControlService::Stop()` detaches from its own io
thread; SAF `SafEnv` clears a failed local-frame exception.
SSU2 reassembly also charges a bounded out-of-sequence fragment/byte budget.
`tests/check-control-security.cmake` pins these contracts. These do not make
`TorrentsTunnel::InitTorrentFiles` service-thread-only nor close the residual
SSU2/RouterInfo/ratchet/datagram races tracked by SEC-025.

Build-system modernization (static): `i2pd_apply_dialect()` owns the
capability/Asio/Windows compile contract; `libi2pd` publishes one merged
Windows import list; the daemon common source list is shared with `i2pd_jni`
and Android no longer builds the AF_UNIX TUI server; `CheckAtomic` reports
`I2PD_ATOMIC_LIBRARIES`; static builds reject sanitizer/JNI combinations;
Boost.Container is optional for static Boost.JSON; `i2pd_privsep` installs its
exported headers; `i2pdConfig.cmake` supports component-aware consumption;
torrent tests are conditional; `tests/Makefile` forwards to the root; presets
use schema 5. Android defaults to arm64-v8a with c++_shared and BOTH find-root
modes. Manual packaging now includes static-musl and container-image jobs.
None of these changes has build, package or device evidence.

OpenSSL 4/provider continuation (static): HKDF uses `EVP_KDF`/`OSSL_PARAM`
on probed providers and retains the legacy bridge for LibreSSL; CMake stamps
probe results by backend and runs one linked modern-API probe; both
`SSL_set1_dnsname` and `SSL_set1_host` are probed with a configure failure when
neither exists. SipHash pins a 16-byte MAC, ML-KEM probing encapsulates and
decapsulates, required provider algorithms (including Ed25519) fail daemon
startup, TLS contexts set an explicit 1.2 floor, and I2PControl verifies the
certificate/key pair. DSA/ECDSA DER encoding is sized by the encoder, RSA
rejects a modulus that does not fill the wire field, Ed25519 checks digest/BN
operations, ECIES/ElGamal propagate failure and cleanse secrets, reserved EC
LeaseSet2 types are never installed, and the preferred type follows the keys
actually created. `famtool` writes its private key atomically. None of this has
compile, backend, provider, device or live-peer evidence.

Still open: `TorrentsTunnel::InitTorrentFiles` mutates session objects from
the disk worker, Android D8/R8 Java 25 dexing evidence, and the PARITY-014
feature backlog. OpenBSD `WANTLIB`/libutil and the shared capability macro set
of `i2pd_privsep` are corrected statically. Status: Implemented / static; Pending
CI compilation; Pending CI tests. No compiler, test runner, project validator
or workflow was executed.

## Upstream openssl continuation (2026-10-25, static)

UPSTREAM-008 covers `9725b8b2..5bcf6101` (fifteen commits, seven files). Only
torrent code changed in a way that applies here: BEP 3 cancel that removes a
queued request and keeps the BEP 6 `m_IsFast` reject gate, `BlockReceived`
returning *applied* so duplicates cannot trigger re-hash/persistence/accounting,
`TorrentComplete()`, `eTorrentErrorFilesystem` with an atomic `Torrent::m_Error`
published from the disk worker, BEP 15 default datagram-tracker port applied at
the use site rather than in the stored URL, `SendBuffer` piece sending without
an intermediate vector, files closed on stop before the resume snapshot, and
rate resets. `b31431ba` and `5bcf6101` were already equivalent (the DSA path is
stricter here). GNU `Makefile.linux` and the removed Debian quilt patch stay
omitted, and no web console or legacy build file returns.

`Signer::Sign` is still `void`; all-zero signature publication on a provider
without DSA remains a planned fallible-interface change, not a fix. Status:
Implemented / static; Pending CI compilation; Pending CI tests. No compiler,
test runner, workflow dispatch, commit or push occurred for this window.

## Feature-parity audit continuation (2026-10-25, static)

The operator blocklist is a startup-only input parsed into one immutable
snapshot (`libi2pd/Blocklist.*`) and published before `NetDb::Start()` loads
cached RouterInfos; readers must never lock. Hostnames are skipped, IPv6 uses
the Java `;` form, and country/GeoIP or reload watching do not exist. Runtime
router bans are memory-only (1024, cleared on restart). The NetDb lookup budget
is floodfill 14 / non-floodfill 3 per reply tunnel, and a non-floodfill drops
EXPL/ANY before charging it.

Automatic address-book merges must keep any destination that existed before the
merge; only an explicit operator add/replace may re-point a name. Subscription
sources are edited through the closed typed verbs and rewritten atomically; the
Android JNI admin allowlist stays closed.

Tracker announces separate local selection from swarm accounting: `left` and
`event=completed` are whole-torrent, local Seeding/promotion stay
selection-based, advertised intervals are floored at 15 minutes and failure
retries are capped at 30 minutes.

Deferred and tracked: server-tunnel `filterDefinition` plus the pre-accept
reject seam shared with Streaming `limitAction` (PARITY-017), HTTP Digest and
per-tunnel control (PARITY-019), netDb propagation parity (PARITY-020), Snark
session limits/uploader slots/durable magnets (SNARK-011), and Android bridges
for the new admin verbs (PARITY-018). Status: Implemented / static; Pending CI
compilation; Pending CI tests. No compiler, test runner, device, live peer,
workflow dispatch, commit or push occurred.

## NTCP2/SSU2 hardening continuation (2026-10-26, static)

Transport wire invariants are load-bearing: the NTCP2 SessionCreated padding
table follows spec/ntcp2-hybrid (v4 1168, v5 1648, 3296-byte buffer equality);
a data-phase AEAD failure must drain 18..1041 bytes with a 10 s deadline before
reason 4 and must never write earlier; clock skew terminates with reason 7 only
after SessionConfirmed; a foreign netID is banned/reason 17 and exposed through
`RouterInfo::HasForeignNetId()`. Do not reintroduce an immediate AEAD-failure
reply or widen the padding table.

SSU2 HolePunch/Introduced payloads are limited to DateTime, Address,
RelayResponse, Padding and Termination. PeerTest never publishes an address from
msg 5, msg 7 is corroborated against the Charlie learned in msg 4, msg 4→6
targets pass endpoint/reserved/blocklist checks, and inbound/PeerTest admission
is capped. `Established()` in `SessionConfirmedSent` requires an ACK covering
packet 0. Relay tags are non-zero, first-wins and expiring; `m_Address` is
mutex-guarded for cross-thread readers; fragment reassembly charges the fixed
slab with a 128 KiB per-message budget, 64 fragments per message and 32
incomplete messages per session; published/effective IPv6 MTU is capped at
1488.

Open and tracked: NTCP2 admission/options/establisher-thread socket ownership
and the undeadlined length read (NET-172); SSU2 socket sharing, memory-pool
lifetime after deferred teardown (SEC-025), proximity checks and cleanup
granularity. Status: Implemented / static; Pending CI compilation; Pending CI
tests. No compiler, test runner, sanitizer, Java peer, workflow dispatch,
commit or push occurred.

## Cryptography modernization continuation (2026-10-26, static)

Ratchet splits are selected by the first inbound tag, not by the last New
Session Reply sent; tag exhaustion opens a new session instead of destroying
the readable one; NSR payloads allow only DateTime/Garlic/Options/Padding; the
inbound tag window is capped at 320. Do not restore the overwrite/Remove
behaviour.

X25519 key generation must fail closed: zero private or public material may
never be persisted, published or pooled, and `X25519Keys` stays non-copyable
with public-key derivation. Ed25519 secret scalars/nonces use
`BN_clear_free`, the hand-rolled verifier rejects `S >= L`, and off-curve
points fail closed. The b33 two-byte signature-type flag is rejected like Java;
RSA RouterInfo identities are rejected on the netDb path;
`i2cp.leaseSetEncType` is strict and unknown LS2 key sections stay inert.

ML-KEM availability requires a real round trip on every backend; hybrid Noise
initialisers are fallible. An SSU2 Retry authenticated only by the public intro
key may not change version unless the signed RouterInfo advertises that exact
maximum, and NTCP2 never randomly downgrades a PQ peer.

Open and tracked: type-5/7 NSR vectors (CRY-206..214), fallible `Signer::Sign`
and LS2 self-verification (CRY-236), capacity-wide secret cleansing (CRY-237),
adaptive ratchet/ElGamal ordering and the 200 ms multi-NS gate (CRY-238),
variable-time provider-free Ed25519 signing, and LibreSSL/restricted-provider
lanes. Status: Implemented / static; Pending CI compilation; Pending CI tests.
No compiler, test runner, sanitizer, provider matrix, Java peer, workflow
dispatch, commit or push occurred.

## netDb and LeaseSet continuation (2026-10-26, static)

Garlic origin is two-dimensional: `fromClove` alone is not enough. A clove of a
directly received garlic may carry a DatabaseLookup and keeps a DatabaseStore
reply token; a clove of a tunnel-delivered garlic must be dropped for lookups
and stored without token/ack/relay for stores. Preserve the `fromTunnel`
marker; never relabel tunnel garlic as direct.

Transport RouterInfo request-flood forwarding requires floodfill mode and the
per-key FloodThrottler. netDb keeps a bounded inbound queue and a deterministic
new-RouterInfo cap that never drops updates and bypasses during reseed; Java's
probabilistic pdrop/go-away remain open.

RouterInfo persistence uses `fs::AtomicWrite` and binds the file stem to the
parsed identity hash; staging and mismatched files are removed. Replacement
installs the validated candidate instead of mutating the stored object.
Selection filters and store validation must use the non-creating profile
lookup and never perform disk I/O under netDb locks.

A MetaLeaseSet is accepted only with zero revocations and header `expires`
equal to the latest entry (Java re-serialises and verifies that canonical
form). An encrypted LeaseSet reply replaces the client cache only when its
signed outer published time is strictly newer. Reply-key 0x12 remains
fail-closed: Java has no sender/KDF and answers cleartext; do not invent a KDF
or a cleartext fallback.

Open and tracked: client Meta resolution (I2P-016), exploratory garlic
forwarding, encrypted RI/EXPL replies, probabilistic store admission/go-away,
per-use stale-RI expiry, throttle eviction, `m_AcceptUpdates` ordering,
asynchronous reseed, local RI address COW (SEC-025) and netDb pool lifetime.
Status: Implemented / static; Pending CI compilation; Pending CI tests. No
compiler, test runner, sanitizer, Java peer, live floodfill, workflow dispatch,
commit or push occurred.

## Tunnel, I2NP and garlic continuity (2026-10-26, static)

`PlanTunnelBuildRecords()` is the only authority that maps a configured hop
count onto the 4/8-entry short-build record table. A path it rejects must fail
with `eTunnelStateBuildFailed` before a record byte is written; never index
`recordIndicies[]` or `m_Records` by a hop number that was not planned.
`ClampTunnelLength()` owns the configured/variance bounds (inbound reserves the
phony record) and must run in the constructor, in `Reconfigure()` and in
selection. `CreateBuildRequestRecord()`/`EncryptECIES()` return `bool` and zero
the whole record on any provider, agreement or AEAD failure; a partially
written record must never reach the next hop.

A transit build request is replay-filtered by its derived reply key in a
bounded 8-minute decaying table; the key is a secret and is never logged.
Relayed requests use ~25 s and replies ~60 s (Java `BuildHandler`), and build
payload sizes are exact (no trailing bytes). A failed `CreateI2NPMessage()`
must abort the relay/reply.

Fresh peer selection, explicit peers and tunnel reuse share
`IsRouterUsableForTunnel()`; a failed or test-struck path is never reused. The
custom tunnel selector must stay a `TunnelPeerSelectorHandle` (weak owner
pinned for the whole call) — never a raw selector pointer. The three
one-hop/paired builders must pass the peer's real `CompatibleTransports` mask,
not a boolean.

Pool builds count in-flight requests, apply the 3→2→1 failure ladder with a
10–240 s delay and reset on success. Tunnel tests persist across management
ticks with a 7.5–15 s per-path deadline, fail only on the third consecutive
strike, keep their strike counter separate from the state machine, skip a test
when the inbound tunnel carried traffic within 40 s and never publish a
`TestFailed` tunnel in a LeaseSet.

`HandleI2NPMessage()` applies the Java 90 s past / 240 s future expiration
window before the type switch (`TunnelData` is exempt: the endpoint has its own
IV filter) and then a bounded two-generation duplicate filter keyed by message
ID, expiration second and a router salt. Cloves use their own
destination-salted filter and must not be recorded by the router-wide one.
`DeliveryStatus` is exactly 12 payload bytes and a `DataMessage` must declare
its whole payload.

Garlic sessions serialize their state with `m_StateMutex` and the provider
`HKDFContext` is per-thread. A destination must snapshot `m_Sessions` /
`m_ECIESx25519Sessions` and call session methods outside `m_SessionsMutex`: a
self-registering session takes the state lock first, so iterating the map while
holding it and calling into a session is an ABBA deadlock. The destination
garlic queue is bounded (256). A clove payload announces 1..32 cloves; a stale
or foreign clove is skipped while the walk continues; the tunnel-only allowlist
applies only to `fromTunnel` garlic; a full ElGamal tag budget must not discard
the payload; ROUTER-mode ratchet cloves are rejected rather than forwarded in
cleartext.

Garlic keys/decryption/tag state remain core-only; do not add garlic
operations to the IPC protocol. Open and tracked: NTCP2 established-session cap,
12-byte options and establisher-thread socket ownership (NET-172), SSU2 socket
sharing and memory-pool lifetime after deferred teardown (SEC-025), local
RouterInfo address-slot copy-on-write, `TorrentsTunnel::InitTorrentFiles`
disk-domain mutation, Java B1/B2/B3 payload-size normalisation and the netDb
items above. Status: Implemented / static; Pending CI compilation; Pending CI
tests. No compiler, test runner, sanitizer, Java peer, live tunnel, workflow
dispatch, commit or push occurred.

## SAM, I2CP, streaming and datagram continuity (2026-10-26, static)

In a SAM `SESSION CREATE`, `PORT` is the local UDP forward endpoint and
`LISTEN_PORT` is the I2CP receive port (default 0/PORT_ANY). Never register a
`DatagramDestination` receiver on the forward port; use
`ResolveSAMSessionListenPort()`/`SAMStyleListenProtocol()` so a subsession is
keyed by `(STYLE, 17/19/20/LISTEN_PROTOCOL, LISTEN_PORT)` and receives only its
own datagram style. An established SAM control connection is never closed for
idleness: Java calls `setSoTimeout(0)` after HELLO, so only HELLO and a
partially received command line have deadlines. `SESSION CREATE`
(`DESTINATION=<private keys>`), `HELLO` (`USER`/`PASSWORD`) and datagram
payloads are secrets and must never be logged; add only non-secret outcomes.

`STREAM ACCEPT` and `STREAM FORWARD` are mutually exclusive per listen port and
answer `RESULT=ALREADY_ACCEPTING`; FORWARD is port-scoped. A missing ID is
`I2P_ERROR "ID not specified"`, a connect timeout is `RESULT=TIMEOUT`, duplicate
parameters close the connection, bare `SILENT`/`HEADER`/`KEY` mean true, and an
unparsable MIN/MAX HELLO bound is `NOVERSION` (I2P_ERROR is only for a line that
is not `HELLO VERSION`). When an armed acceptor is torn down, re-arm the oldest
queued ACCEPT for the same port; never arm one port's ACCEPT on another
destination.

`.b33.i2p` and implicit long-label names are blinded destinations, not address
book keys: validate the label (canonical base32, blindable type, bounded
length) and fail closed rather than falling through to base32/base64. A type-42
BlindingInfo is keyed by the unblinded signing key; its 32-byte DH private key
or PSK must be passed to `RequestDestinationWithEncryptedLeaseSet` (preferred
over `i2cp.leaseSetPrivKey`) and never truncated or logged. HostReply uses
Java's result codes 1/2/3/4/5/6, not a bare failure.

The I2CP listener is bounded (`i2cp.maxconnections`/`i2cp.maxsessions`, exactly
one release per admission) and every handshake/header/body read is asynchronous
with a cancellable deadline; never block the service thread with
`available()`/`read()`. CreateLeaseSet (CLS1) is accepted only for a matching
destination with a valid signature and matching ElGamal key, and remains
DSA-only: do not guess per-type private-key offsets.

`StreamingDestination::AcceptStream(timeout)` must use heap sync state and
unregister on timeout (a lambda in `m_Acceptor` must never capture caller stack
references). Acceptors are explicit registrations with tokens;
`RemoveAcceptor(token)` removes one and `ResetAcceptor()` completes every waiter
with `nullptr`. A retransmitted SYN for a closed stream is dropped by the
bounded recently-closed set and answered with one signed RESET; over-limit,
throttled and backlog-full SYNs are RESET, not silently dropped. No data packet
after CLOSE/RESET; duplicate SYNs do not reset the routing path.

Datagram framing is chosen per send (`eDatagramDefault` keeps the session
default); never rely on the destination-wide version for a SAM subsession.
Identity/hash receivers are keyed by `(protocol, port)` with 0 as the explicit
catch-all and are never promoted from a port to the destination wildcard. Raw
receivers refuse 6/17/19/20, a 0xff protocol byte means 0, options are per call
and the (in)deflater plus the datagram session state are guarded.

Open and tracked: streaming TIME-WAIT/FIN retention and SYNACK-before-LeaseSet
ordering, `limitAction` responses, CLS1 non-DSA keys, day-scoped BlindingInfo
matching, 33–61 KiB Java datagrams versus the local 32 KiB bound,
`TorrentsTunnel`'s shared session-version mutation, and all dynamic evidence.
Status: Implemented / static; Pending CI compilation; Pending CI tests. No
compiler, test runner, sanitizer, fuzzer, SAM client, Java peer, workflow
dispatch, commit or push occurred.

## Application-services continuation (2026-10-26, static)

Proxy pages and logs are trust boundaries: never interpolate a client-supplied
`Host`/path or a configured outproxy URL (which may carry `user:pass@`) into
HTML, JSON, an error page or a log. Only `http` and CONNECT-`https` are proxied;
a plain request for another scheme is a 403 before any destination resolution.
Proxied responses use the client-response mode to strip hop-by-hop and
proxy-only headers; a proxied request must never forward credentials,
`X-Forwarded-For`-style identity headers or a full URI in a log. Digest auth is
opt-in (MD5/SHA-256, bounded nonce cache, `nc` replay, stale) and Basic stays
the default; never add `auth-int`/`-sess`/SHA-512-256/userhash half-way.

Server tunnels: `I2PServiceHandler::Terminate()` is virtual and teardown
snapshots handlers under the mutex before terminating outside it; a stopped
tunnel must close its live streams/sockets and be restartable. `filterDefinition`
is parsed into an immutable definition and evaluated by the streaming admission
predicate **before** a stream exists or a SYNACK is sent; filter denials are a
silent drop by default and `limitAction=reset` opts into one signed RESET.
`targetForPort.<port>` selects the upstream by accepted local port (443/22
without a mapping is reset), and only port 0 (or `defaultacceptor=true`) may own
the destination-wide acceptor.

A reseed job is cancellable (`stop_token`) with per-phase and per-host
deadlines; administrative reseed calls `RequestReseed()` and never blocks the
admin thread. Never delete cached RouterInfos because a downloaded bundle is
stale: skip and count stale entries. Never log `reseed.proxy` credentials or
put a URL in `ReseedState.lastError`. The blocking bootstrap reseed is the only
remaining synchronous path.

Address-book artifacts are written only through `fs::AtomicWrite`/
`AtomicWritePrivate` (no `ofstream`, no data taken through a symlink), and
`ADDRESS-ADD` never re-points an existing name: `ADDRESS-REPLACE` is a separate
typed verb. Subscription `#!action=delete` retracts only names recorded as
coming from that source; the `entry-sources.txt` sidecar is bounded and safe to
delete (it only disables retraction). Destination lengths (516–616) and host
label rules follow Java; a miss may be negatively cached for a short, bounded
time but a b33 name never is.

Auxiliary services: BOB is loopback-only legacy, caps sessions/lines, never
logs operands and requires `bob.allowgetkeys` for the private-key reply. The
I2PControl AF_UNIX path is validated by type/owner, `0600`, removed only while
it is still this process's socket, and requires a same-UID peer where the
platform exposes peer credentials; JSON ids and string values are escaped and
Java's error objects are returned. NTP replies are validated field by field
and a large offset needs three agreeing servers. UPnP mappings use a bounded
renewable lease and refuse a same-host IGD.

Administration stays a closed typed set (`ADMIN TUNNEL-START/TUNNEL-STOP`,
`ADMIN ADDRESS-REPLACE`, non-blocking `ADMIN RESEED`, existing subscription and
ban verbs) on the daemon-owned queue, surfaced by the native TUI and Android.
There is no generic config writer, no arbitrary path/URL argument and no web
console. New TUI strings should be added to all four catalogs; English fallback
is a temporary state tracked as `I18N-004`.

Open and tracked: HTTP keep-alive/pipelining, IRC `ircclient`/DCC, UDP/torrent
tunnels in the start/stop registry, Streaming `limitAction` `http`/custom,
synchronous bootstrap reseed, UPnP status in the UIs, Windows local-socket peer
credentials, feed retraction depending on the sidecar, and all dynamic
evidence. Status: Implemented / static; Pending CI compilation; Pending CI
tests. No compiler, test runner, sanitizer, fuzzer, Gradle/device, live proxy/
tunnel/reseed/IGD/NTP/BOB/Java peer, workflow dispatch, commit or push
occurred.

## Privilege-separation continuation (2026-10-26, static)

`privsep/Capabilities.h` is the single role×capability authority and encodes the
project invariant: no process role may set both `ExternalNetwork` and
`PersistentWrite`. The target core row has no write capability; the legacy core
row is recorded with it so the remaining gap is explicit, not assumed. Every OS
backend policy must be reviewable against exactly one row; do not add a role or
a capability outside that header, and do not mark a role "separated" because a
policy builder exists.

A worker role is bound to its spawner: `SpawnWorker*` writes
`[role-id u8 | 16-byte CSPRNG nonce]` into an anonymous pipe before `fork()`.
The child reads fd 5, closes it, and dispatches on the validated role before
touching the channel or applying its policy. Never put the role record on
`argv`, `environ`, a log line, the protocol channel or a file. Descriptors are
3 = channel, 4 = inherited storage root (OpenBSD), 5 = one-shot identity, ≥6
closed. Never reuse one of these numbers for a new purpose.

When `privsep.artifacts` is enabled, the netDb RouterInfo cache, peer profiles
and destination private keys are written only by the data-storage worker
(`netDb/r*/routerInfo-<b64>.dat`, `peerProfiles/p*/profile-<b64>.txt`,
`destinations/<b32>.<type>.dat`). Names are canonical (44-char base64 identity
or `<52-char b32>.<keyType>`) and re-encoded before use; reject separators,
NUL, `..`, drive colons, symlinked/hardlinked/foreign targets. Writes use fixed
staging slots and an atomic no-follow replace (0600 for keys). The installed
writer is the only write path: a writer failure must never fall back to a
local filesystem write, and an enabled-but-unavailable worker must fail closed.
Startup reads remain direct until the read path is split.

Storage channels are generation-fenced. A client restart bumps the generation;
replies from an older generation are dropped, a request-id collision or
exhaustion returns `Busy`, and an ambiguous write is never replayed. Restart
uses 250 ms→30 s backoff with 8 attempts in 60 s and a 60 s stable reset where
an exec'ed worker can be respawned (Linux); OpenBSD has no post-pledge respawn
in the core. A failed `router.info`/fixed-file save retains the last buffer and
retries on the next tick rather than silently dropping the update.

The log worker sanitises UTF-8 (invalid/truncated sequences and C0/DEL/C1
including U+009B), redacts the values of `password`, `privatekey`,
`private key`, `passphrase`, `secret` and `token`, stops appending at 64 MiB
with one non-secret notice (no rotation) and sends from a channel snapshot
outside the logging mutex. Never log credential, key or secret operands on any
path, including failure diagnostics.

Open and tracked (`PRIVSEP-022`): the network worker (listener/connect
handoff first), the crypto signing worker (fallible `Signer::Sign` first),
direct startup/read paths, garlic tags/address book/torrent state/bootstrap
keys, a core write-denying Landlock profile, OpenBSD `sendfd`/post-pledge
respawn, Windows `--privsep-worker`/`WSADuplicateSocketW`, and broker subtree
registration. Do not claim global privilege separation: the core still owns
transport sockets and bootstrap keys. Status: Implemented / static; Pending CI
compilation; Pending CI tests. No compiler, test runner, sanitizer, fuzzer,
OpenBSD/Windows runtime, live peer, workflow dispatch, commit or push occurred.

## IPC consolidation continuation (2026-10-26, static)

`privsep/Protocol.h` owns the single `MessageSpec` registry: per type it
declares the minimum protocol version, direction, receiving roles, payload
ceiling and descriptor policy, and `Encode`/`Decode`/`AllowsOperation`/
`DescriptorPolicyFor` derive from it. Never reintroduce a hand-written role
switch in a worker, backend or test; add or change a row instead. A worker role
may receive request-direction types only and the requesting side reply types
only (`EndpointRole::Core`). `Pong` is the worker-to-core reply; a non-core
endpoint may receive `Error` and `TorrentStorageSupervisorReply`, because the
supervisor and broker issue their own requests. `ValidateMessageRegistry()`
must stay a compile-time assertion over a contiguous table.

Versioning is per type. `PROTOCOL_VERSION = 5`; `DataStorage*` requires 3, the
advisory `Cancel` requires 4, and version 5 adds the Windows `WorkerHello`
spawn proof. A type carried in an older header is `UnsupportedVersion`; never
reinterpret it. Per-type payload ceilings are
normative: an empty `Ping` and a bounded storage frame are not interchangeable
with the 64 KiB frame maximum.

Serving endpoints run `Channel::EnforcementPolicy::Endpoint`: they enforce the
registry's descriptor presence and kind, close a rejected descriptor, answer an
*attributable* frame fault with a typed `Error` correlated by request id, and
tolerate at most `MAX_PROTOCOL_VIOLATIONS` (8) such faults before exit. A frame
is attributable only when magic, version and declared length agree; a corrupt
header must never be able to choose a request id. Credential and ancillary-data
failures stay fatal. A wrong-role or malformed frame is a bounded typed refusal,
never a reason to kill a worker; a duplicate `ConfigureLog` is a refusal too.

`Cancel` is advisory because the storage workers are single-flight and
synchronous. Never treat it as a rollback, never replay a request whose outcome
is unknown, and keep draining the outstanding reply so the channel stays in
sync. `Busy` covers request-id exhaustion and a busy slot without poisoning the
channel. A receiving endpoint must not be killed by an idle timeout: the
Windows message-mode pipe returns empty and keeps the handle.

A pre-opened log descriptor is a capability. Bind it to the configured path by
dev/inode identity, owner and `st_nlink == 1` before `fchmod` or sandboxing;
never accept an arbitrary same-UID writable regular file as the log sink. The
OpenBSD `imsg` adapter cannot carry a frame above `MAX_IMSGSIZE` (16 KiB), so a
larger per-type ceiling is POSIX/Windows-only until chunking exists. No explicit
version-negotiation handshake exists yet; compatibility is per frame. Status:
Implemented / static; Pending CI compilation; Pending CI tests.

## OpenBSD supervised-start continuation (2026-10-26, static)

The OpenBSD supervisor and storage broker are exec authorities only for a
frozen image. Both pre-resolve their own executable before the sandbox,
`unveil(exe,"x")` with exactly one rule, lock the unveil list, and pledge
`stdio proc exec` (supervisor) or `stdio proc exec sendfd` (broker). They must
never regain `unix`, pathname read/write, `recvfd` or network. `execve` beyond
that image must fail; the exec'ed image starts without unveils and installs its
own policy. Never pass an `execpromises` argument (`OPENBSD_PLEDGE_EXECPROMISES`
is `nullptr`) and never assume pledge or unveil survive `execve`.

The core's startup and runtime promise sets come from one table. The startup
set includes `recvfd` (pledge can only remove promises; a runtime-only promise
is ignored and its first use aborts the core) and `id` only when a worker
identity is configured. The runtime set must be a strict subset and must never
contain `exec`, `id`, `chown`, `dpath`, `prot_exec`, `unveil` or `sendfd`.
`openbsd.pledge_file` is a bounded subset selector: comments and blank lines
are rejected, every token must already be in the phase base set, and dangerous
runtime tokens are refused. `openbsd.unveil_file` entries must be absolute,
NUL-free and neither textually nor resolved `/`.

`privsep.user`/`privsep.group` are the worker identity. Resolve them with the
bounded resolver before the startup pledge, reject uid 0, and publish the pair.
The forked child clears supplementary groups, sets gid then uid, and verifies
real/effective ids and an empty supplementary set before `execve`. A root core
with any isolation feature and no configured identity must fail startup with
the explicit identity diagnostic; do not silently skip a worker or broker. The
core itself is not chrooted or dropped by this mechanism. A delegated storage
root must be owned by the worker uid, not by the root core.

A worker is bound to its spawner by the 25-byte hello
`[role u8 | nonce 16 | uid u32 | gid u32]` written by the child after its drop
and read from fd 5. Require role/uid/gid agreement, a non-zero uid, exactly one
record, and close fd 5 before reading a frame. Descriptor contract: 3 channel,
4 inherited storage root (only for `*-inherited` roles), 5 hello, 0/1/2
`/dev/null`, >=6 closed. A role without a root closes fd 4; never let a
terminal, rc.d log pipe or activation socket reach a worker. The executable
path is frozen from `argv[0]`/`realpath` (absolute, regular, owner root/self,
no group/world write); never resolve it through `$PATH` or `/proc`.

A channel peer may have the worker's own uid or, for an unprivileged worker,
uid 0 (the delegating root core); the channel is an anonymous `socketpair`,
never a named socket. `ForbidProcessCreation()` is one-way and must be set
before the runtime pledge; every spawn helper consults it. Remaining
(`OPENBSD-SBX-11`): generalize the broker to router/data respawn with new
typed messages, and move the TUI/I2PControl sockets out of any storage-unveiled
root or prove `connect()` is denied. Do not claim global separation: the core
still owns transport sockets, most persistence and bootstrap keys.

## Linux backend hardening continuation (2026-10-26, static)

Linux `SO_PEERCRED` is the credential captured when the socket was created, not
the live sender. Never compare every frame against it after a worker drops to
`privsep.user`: the channel must carry an explicit `PeerExpectation` set by the
party that knows the topology. The core expects the worker uid/gid and the
direct child pid; a worker expects its own uid or uid 0 (the delegating root
core); an adopted broker socket is uid-bound and must have an empty receive
queue. The legacy creator comparison is only for raw transport tests.

Capability teardown belongs in the forked child. Two stages are required: while
still privileged set `PR_SET_NO_NEW_PRIVS` and clear the ambient and bounding
sets (keeping effective/permitted so `setgroups`/`setgid`/`setuid` still have
CAP_SETGID/CAP_SETUID); after the credential drop call `capset(0)` and verify
with `capget`; then exec. Doing the whole teardown only in the exec'ed worker is
a no-op because the calls return EPERM and must not be tolerated.
`RunWorker` re-asserts `no_new_privs` and an empty capability set before any
role reads a frame. Reset ignored signal dispositions to `SIG_DFL` in the child.

The storage seccomp profile denies the whole connection lifecycle (`connect`,
`bind`, `listen`, `accept*`, `sendto`) and process creation/execution, not only
`socket`/`socketpair`. Landlock handles `EXECUTE` but no worker rule grants it,
and `Storage` restricts reads as well as writes to the delegated root. Install
the socket/capability lockdown **before** the configure frame; Landlock (which
needs the delegated root) follows. An empty network namespace is best-effort in
the spawn child while `CAP_SYS_ADMIN` is available; seccomp is the mandatory
boundary, so never describe a namespace that was not created.

The frozen executable is checked at freeze time (regular, owner root/self, no
group/world write, non-writable parent) and its dev/inode/size are re-checked
immediately before `execve` on Linux. Only `*-inherited` storage roles and the
broker accept an inherited root; the root must be a directory owned by the
worker uid without group/world write, and fd 4 is closed for every other role.

Open (`PRIVSEP-031`): cgroup-v2 per-worker limits, a global admission cap,
`SupervisorStatus` over IPC, unified restart/stop budgets, broker reaping, a
deny-all log socket profile, and worker-side core-pid binding. Do not claim
global separation: the core still owns sockets, persistence and bootstrap keys.
Status: Implemented / static; Pending CI compilation; Pending CI tests.

## Windows multiprocess continuation (2026-10-26, static)

The Windows transport remains a message-mode Named Pipe (`PIPE_REJECT_REMOTE_CLIENTS`,
protected DACL, `FILE_FLAG_FIRST_PIPE_INSTANCE`, per-spawn random name). Do not
migrate it to `ncalrpc`/`ncacn_np`, MIDL stubs or any raw ALPC/`Nt*` port API:
`WIN-012` records the decision and its revisit trigger (a real cross-principal
or AppContainer administrative split, control plane only). No `\RPC Control\`,
`NtCreatePort`, `NtConnectPort`, `NtRequestWaitReplyPort` or undocumented
`ntdll` export may appear.

A Windows worker is bound to its spawner by the `WorkerHello` frame (protocol
v5): `u8 role | u8 nonce length (32) | nonce`, sent after `ConnectBound` to the
spawned parent PID/session. The server impersonates the client, checks user
SID, PID/session, AppContainer SID (when the role uses one) and the nonce, then
binds the role; only then does `WindowsChannel::Receive` apply the shared
registry. The nonce is on the command line and is not a secret from a same-user
process; the protected DACL plus PID/session/container binding are the primary
boundary. `Accept()` is test-only; production uses `AcceptBound`.

`privsep/WindowsIsolation.cpp` is the single role authority. Storage/log/
crypto/admin are low-privilege AppContainers with no `internetClient`; network
has `internetClient`/`privateNetworkClientServer` and no delegated write root;
the supervisor uses a restricted token because it spawns children. A role with
both an Internet capability and a write root is rejected. AppContainer
profiles (`openi2pd.<role>`) are durable and operator-owned; never delete one
automatically. The pipe DACL contains only the service user, SYSTEM and the
exact container SID; grant only the delegated root to the container SID and
never add Everyone, Authenticated Users or ALL APPLICATION PACKAGES. A
container role without a container, a container for a non-container role, or a
persistent-write role without a delegated root must fail closed; never fall
back silently to a restricted token.

Spawn uses a suspended process with `CREATE_NO_WINDOW`, NUL std handles,
`bInheritHandles=FALSE`, an explicit empty handle list, a computed attribute
count, `PROC_THREAD_ATTRIBUTE_JOB_LIST` with a suspended fallback,
`PROC_THREAD_ATTRIBUTE_SECURITY_CAPABILITIES` plus the LPAC all-applications
opt-out, and Job Object memory/process/UI limits. Every failure terminates the
job/process and waits; `TerminateAndWait`/`Stop` terminate the Job Object;
`KILL_ON_JOB_CLOSE` is the backstop. `WindowsWorkerSupervisor` owns one role,
authenticates each generation, restarts under the shared 250 ms→30 s / 8-in-60 s
budget with a monotonic generation, and publishes `SupervisorStatus`. The
service must drain `Daemon.stop()` with non-zero wait hints and never report
STOPPED while the router runs.

A completed-during-cancel overlapped record must be delivered, not discarded;
a timed-out write closes the channel and is never retried on a new one. Open
(`WIN-013`): start the supervisor from the daemon/service, pass the delegated
root, surface status in TUI/Android, and implement the real Windows log/
storage/network role loops. Do not claim global separation: the core still owns
sockets, persistence and bootstrap keys.

## Android multiprocess and lifecycle continuation (2026-10-26, static)

Android grants `INTERNET` and SAF URI permissions per app UID, not per process.
A separate process of the same UID is a heap/crash/network-containment boundary,
not a privilege boundary; `isolatedProcess` loses both `INTERNET` and persisted
SAF grants and therefore cannot host the router or the SAF broker. Never claim
Android privilege separation. The SAF broker remains `:torrent_storage` (same
UID, non-exported, seccomp/no-new-privs); the Activity/ViewModel stay in the
default process; the router/presentation split is `ANDROID-026` and requires a
closed `IRouterControl` AIDL before it may be enabled.

The router is owned by `RouterService`, never by an Activity: `stopWithTask`
is false, the run intent is persisted in `RouterRunState` (`run_phase`,
`graceful_deadline_epoch_ms`, `run_generation`, legacy `should_run` mirror), an
expired drain is terminal across `START_STICKY`/boot, a live drain re-arms the
remaining wall-clock delay only after the daemon reports started, and corrupt
deadlines are clamped or fail closed. `onTaskRemoved` keeps the daemon;
`onDestroy` removes callbacks already queued on its main handler, but SAF
work can still publish later (`ANDROID-035`). Some persisted transitions read
outside the write lock, so stale-state protection remains open (`ANDROID-033`).
The boot/package-replace receiver starts only from the persisted intent.

The SAF AIDL is version 2: `handshake()` returns a CSPRNG generation;
`prepareExport` issues a one-shot owner-bound token consumed by
`openExport(token)`; `retire(generation)` is a one-shot fence; every root,
handle, operation and token is bound to the exact `(uid,pid)` that created it;
`cancel` is owner-scoped and `closeFile`/`revokeTree` require the owner.
Admission returns typed `0/false/null`, never an exception. A data operation
cancels its provider at 15 s; the broker process is fenced only at 30 s if the
same operation is still registered. No `ParcelFileDescriptor` crosses Binder.
Never delete a user-chosen export document automatically.

A native process claims exactly one role (`Router`/`StorageBroker`) before its
first router- or document-capable entry; `startDaemon`/SAF bridge require
`Router`, `restrictSafStorageProcess` claims `StorageBroker` and is
irreversible. A conflict returns `process-role-conflict` and never starts a
second router. The JNI string boundary never passes a null `jchar*` to
`NewString`, clears pending exceptions and deletes local refs on failure;
`stopDaemon` publishes a native stop token before any monitor and a start
returns `start-canceled`; SAF payloads are detached on the stop path and no JNI
may run from bridge/payload destructors. Open: `ANDROID-026` router/presentation
split, helper-APK foreign-UID denial, provider-blocking >30 s and device/CheckJNI
evidence.

## I2PSnark scheduler, tracker and privacy continuation (2026-10-26, static)

Transport invariants are load-bearing: a per-request deadline
(`PEER_REQUEST_TIMEOUT`, 60 s) releases a stalled outbound block before the
95 s blanket keep-alive release, using the request's piece epoch so it cannot
resurrect a reset/recheck block; peer and torrent transfer counters charge the
actual block length (the final block is short); an idle peer rate halves after
one `PEER_RATE_DECAY_INTERVAL` (30 s) and zeroes after two. Per-file progress
counts a piece only when it is complete, verified and not saving, matching
promotion and HAVE/serving withholding. A failed payload write must publish
`eTorrentErrorFilesystem` (the disk worker may set the atomic error) and only a
later verified write clears it; announce/serving stay suppressed meanwhile.
Corrupt-peer charging is best-effort: only the connection whose applied block
completes a mismatching piece is charged, the table is bounded (64) and
in-memory, and three charges disconnect it; never turn this into a persisted
ban or claim Java-equivalent peer blame.

Tracker dispatch must never overwrite the bounded failure ladder with the
advertised interval. HTTP announces are single-flight per `(torrent, slot)`,
capped at 16, and one deadline covers both the write and the response read;
every completion, stop, remove and reannounce retires the admission and closes
the stream. Registry slots are reference counted, reused lowest-free-first and
never renumbered (torrent statistics are slot-keyed); configured common slots
are never recycled. BEP 15 announces stay outside that HTTP registry and keep
their transaction table. BEP 12 ordered tiers/shuffling remain a deliberate
open item: do not change the default announce behavior without a verified Java
comparison.

When metadata reveals a private swarm, the DHT must purge served peers, our
rendezvous identity, issued tokens and outstanding `get_peers` queries, and a
bounded private-hash set must refuse later inbound `get_peers`/`announce_peer`;
a removed private hash stays unservable. Never reintroduce public DHT, DNS,
clearnet trackers or peer fallback.

`TORRENT-REMOVE-ALL <tunnel-index> [files]` is the session-wide removal verb;
the only accepted option is the exact token `files`. `TORRENT-COMMENT-UNHIDE`
must remain in both the request-type and torrent-id parse branches. Android's
JNI torrent-command allowlist must admit every typed verb the Java UI can
build (including start/stop/reannounce and their bulk variants and remove-all);
a closed allowlist that silently rejects a UI verb is a defect. The Android
creation-tracker validator is creation-scoped: it must not require an engine
torrent id, and it must keep the I2P-only URL/control-character rules.

Prepared regressions are `test-torrents-scheduler`,
`test-torrents-tracker-dispatch` and the extended `test-tui-protocol`; none is
executed. Durable magnet/session recovery, BEP 12 tiers, endgame/CANCEL,
unchoke/upload-slot policy, session/torrent bandwidth limits, per-file recheck
and typed torrent results/truncation flags remain open (`SNARK-012`).

## BitTorrent DHT/PEX hardening continuation (2026-10-26, static)

All BitTorrent discovery remains I2P-only: 32-byte destination hashes,
repliable I2P datagrams and `i2p_pex`/`i2p_dht`; never public UDP Kademlia, DNS
or clearnet trackers/peers.

DHT datagrams arrive on the network thread. Repliable frames must be copied
into the bounded DHT incoming queue and parsed on the tunnel service thread;
raw frames must be copied and posted by `TorrentsTunnel` before
`HandleRawDatagram` runs. Never parse, reply or mutate routing/query/token
state on the network thread. The raw path has no sender identity, so it is
protected by a global fixed-window budget (1024/60 s) and must never answer a
malformed frame; the repliable path keeps its per-origin limiter.

Issued tokens are eight CSPRNG bytes in a bounded FIFO store with a 16-token
per-node quota, bound to node ID, destination, info hash, query port and a
600 s lifetime. Never reintroduce a global linear eviction that lets one
destination flush every honest token. The routing table admits at most two
node IDs per destination, because only six node-ID bytes are bound to the peer
and port. A DHT port is always 1..65534 so `rport` cannot wrap. Error replies
mark the target node failed before advancing a lookup; a reply carrying both
`values` and `nodes` consumes both. Periodic state saves are single-flight and
must use the delegated worker without a parent-IO fallback when enabled.

The KRPC envelope decoder is side-effect free and shared by both receive paths:
exactly one consumed top-level dictionary, unique keys, `y` of `q`/`r`/`e`,
`t` 1..16 bytes, exactly one `a`/`r`, closed envelope except optional BEP 5
`v`. A repeated BEP 10 handshake is rejected, extension IDs stay write-once,
`reqq` is clamped as a signed value, `v` is bounded, and a valid private
torrent omits `metadata_size` entirely. Do not call a peer a seed on an empty
or unreceived bitfield: require the bitfield-received flag. `i2p_dht` hints
require an established authenticated identity; a present-but-unrelated `rport`
is tolerated for Java interop while `port` stays bounded.

PEX `added` is concatenated 32-byte destination hashes with no `added.f` or
`dropped`; decoding is bounded and pure. PEX/DHT candidates enter a bounded
per-torrent table (256 entries; PEX 30 min / DHT 3 h TTL; 30 s→900 s retry
backoff; four failures; 100 candidates/60 s per origin) and are drained on the
service tick. The table is purged on stop/remove/private transition and a
connected candidate leaves the table. Tracker-origin peers are not subject to
that budget.

Prepared: `test-torrents-dht-protocol`, `test-torrents-pex`,
`fuzz-torrents-krpc`, `fuzz-torrents-extensions`. Open: BEP 5 announce
`port`/`implied_port` (needs a Java reference check), BEP 12 ordered tiers,
Java/native differential fixtures, live churn, persisted-peer bans and
OS-separated DHT persistence. Do not claim Java interoperability from parser
vectors.

## I2PSnark comments/ratings continuation (2026-10-26, static)

The `ut_comment` wire stays exactly as documented: `msg_type` 0 request with
optional `num` and a `filter` hint, `msg_type` 1 reply with a `comments` list
of `owner`/`text`/`like`/`timestamp`, where timestamp is a nonnegative age in
seconds. Never invent fields or treat the author label as authenticated. A
`filter` is an ignored hint of 0..64 bytes; TAB is normalized to a space; other
C0/DEL/C1 controls remain rejected for terminal safety. A private torrent never
advertises, requests, replies or persists exchange, and comments stay disabled
by default.

Dedupe uses a normalized key over owner and text: trim/collapse ASCII
whitespace and remove U+200B/U+200C/U+200D/U+FEFF, while storing the original
text. Ratings are immutable for a normalized pair; never merge or replace a
stored rating on replay. Anonymous entries have a separate visible quota and
must not bypass the per-pseudonym cap. Only entries not already stored are
charged to the per-connection (40/min) and per-torrent (80/min) admitted-entry
windows; local operator adds bypass those peer budgets but remain bounded by
the store.

A local add/hide changes the store revision and may refresh a connection after
a 10 s floor; the periodic cadence remains 600 s. Every content-changing hide,
unhide or hidden-trim must bump the revision so the coalesced admin-state
writer schedules a save; a no-op hide must not. A future persisted timestamp is
clamped to the local clock instead of rejecting the whole state (which would
lose priorities and the stopped flag); negative/structurally invalid state is
still rejected atomically.

Comment moderation is local-only and hide-based; hidden text is retained for
dedupe and shown in the admin UI. `TORRENT-DETAIL <tunnel> <id> [offset]
[limit]` pages the retained store (offset 0..255, limit 1..16) and reports
`comments.offset/count/total/visible/hidden/local/truncated`; the default
response is the newest 16 records. TUI uses `[`/`]`, aged timestamps, bidi
control stripping (U+202A..U+202E, U+2066..U+2069) and `[y/N]` confirmation
before hide/unhide. Android pages with age/local markers and must not silently
drop an action while busy. Do not log comment text, owner labels, keys or
identities; labels are user-supplied pseudonyms.

Prepared regressions: extended `test-torrents-comments`,
`test-torrents-state`, `test-tui-protocol` and `TorrentControlTest`. Open:
Java differential `CommentSet`/dedupe parity, normalized rating updates,
whitespace policy confirmation, TUI catalog translations (`I18N-004`) and
device moderation tests. No interoperability claim follows from codec tests.

## Android SAF payload completion continuation (2026-10-26, static)

Every payload operation stays behind the selected `TorrentPayloadStorage`
capability. Never fall back to an app-private POSIX path after SAF or worker
selection, and never let a peer-supplied name grant document authority. Native
relative-path validation is necessary but not sufficient: the Java broker keeps
the final traversal/display-name/identity check.

At startup, SAF documents are probed by opening the exact final name with
`create=false` and closing it (`TorrentFile::ProbePayload`); an absent, deleted
or revoked document is reported as missing without an error log. Only files
that answered the exact-size probe are marked present, and the bounded
verification chain hashes pieces fully backed by them. Zero-length documents
are never opened. A piece spanning a missing document stays downloadable; do
not mark every SAF file present or trust the persisted resume bitfield for
document storage. A POSIX final-name file opened read-only for seeding must be
closed and reopened writable if invalidation forces a re-download.

The SAF broker watchdog is armed only after an operation owns `ioLock`: a
queued operation must not be cancelled or process-fenced because a sibling
stalled inside a provider. Admission, owner `(uid,pid)` binding, the 15 s
cancel followed by the 30 s process fence for the same registered operation,
and generation-fenced retirement are unchanged. Client bind retries are
bounded (6 attempts, 250 ms→10 s); a successful connection clears the exhausted
latch, but the attempt budget is restored only after a 30 s stable binding.
`initialize`, an explicit tree selection and a status refresh may re-arm at
most once per stability window; a permanently broken policy must not spawn and
kill a broker forever.

No-seek/pipe providers and short writes fail closed without staging. Deleted
documents, same-length replacement, revoked grants and broker death are
recovery inputs, not reasons to close the whole broker for unrelated handles.
Instrumentation fixtures are bounded, app-private and in-process: nested
multi-file tree, zero-length document, deleted/revoked/same-length-replaced,
no-seek/socket, read-only write failure and post-retire rebind. Device runs
remain the evidence for provider behavior; do not claim them from static tests.

## Android Java 25/ART/JNI modernization continuation (2026-10-26, static)

Java 25 is the real source and class-file target (`sourceCompatibility`/
`targetCompatibility` 25, exact JDK 25 toolchain). Never lower it to 17/21 to
appease a tool. Source-language features (records, sealed types, type-pattern
switches, text blocks) are compiled to class-file 69; **D8 must desugar** the
record superclass/`ObjectMethods`, `SwitchBootstraps.typeSwitch` and
`MatchException` for minSdk 29. A successful `javac` is not ART evidence, and a
dex target is not an API check.

The Android classpath still resolves JDK 25 platform classes. Keep API-30+/33+
`java.util`/`java.lang`/`Stream`/`String` conveniences and every virtual-thread,
structured-concurrency, scoped-value, FFM, class-file or PEM/KDF API out of
`src/main` and `androidTest` (unit tests under `src/test` run on the build JDK
and are exempt). `android.builtInKotlin=false` is the current AGP 9.4 workaround
because Kotlin's highest JVM target is 24; document the AGP 10 replacement
instead of lowering Java. Core-library desugaring stays disabled; it cannot
provide Java 25 platform APIs.

Every Java `native` declaration must have a matching checked-in header
declaration and `Java_*` definition; `android/tools/check-android-jni-symbols.pl`
runs in the manual Android job. JNI entry points must use `noexcept` consistently
in header and definition, release `GetStringChars` through an RAII owner, clear
pending exceptions on every fallback, and never retain an Activity/Listener.
The SAF bridge is one process-lifetime global class reference; reuse it across
daemon restarts and free a candidate global ref on failed installation, while
keeping an old bridge alive for payloads if the class identity changes.

Prepared gates: all nested probe classes dexed at `--min-api 29`, DEX 039,
absence of record/type-switch bootstraps in the packaged DEX, JNI symbol parity,
exact Java-25 pins and an API denylist. Real AGP-selected D8/R8, Lint major-69,
platform-37 resolution, API-29 device execution and R8-minified behaviour remain
manual CI/device evidence. Do not claim Java 25 runtime compatibility from the
source build alone.

## Android native GUI continuation (2026-10-26, static)

The native GUI is Material 3 Views/XML plus three programmatic screens
(torrents, address book, runtime administration). Never create a screen-local
`MaterialButton` or a hard-coded dp inset: `UiFactory.button` provides the 48dp
minimum target and logical padding, `UiFactory.scrollingColumn` provides the
centred `MaxWidthLinearLayout` column (840dp, 1000dp at `w600dp`) and
`UiFactory.applyRowSemantics` provides one-utterance content/state semantics.
A new programmatic screen must use them and must not reintroduce a fixed
`16 * density` inset.

I2PSnark snapshots are decoded through the bounded `TorrentRow` record. A
non-numeric torrent id is rejected, a malformed hash or magnet is dropped and
counters/percent are clamped. Do not concatenate raw snapshot map lookups in a
fragment, and keep the actions dialog titled with the torrent name so a verb
cannot be applied to the wrong row.

SAF grant authority belongs to `RouterService`:
`selectPayloadTree`/`detachPayload` run on `safWorker` and deliver a typed
boolean on the main thread. Never call `TorrentSafStorage.select`/`detach` from
an ActivityResult or row callback; both commit the grant map and may round-trip
to the provider. A fragment must ignore a late result through its generation
check.

Navigation stays single-activity and tab-coherent: secondary destinations
(logs, about, I2PSnark, address book, runtime administration) are entries, not
tabs, and are hidden while a detail is open. `showDestination` must hide every
known fragment tag. The foreground notification offers exactly one drain action
chosen from the lifecycle state (Graceful stop vs Cancel graceful stop) with
`setOnlyAlertOnce`; the graceful countdown is not an accessibility live region.

The manual API 29 UI lane is the opt-in `android_ui` workflow_dispatch input
(`android-ui` job): x86_64 vcpkg/NDK build, sdkmanager/avdmanager emulator,
`connectedDebugAndroidTest`, report assertion and logcat capture. It is
Prepared / not executed. A default dispatch stays arm64-only. Do not claim
device, TalkBack, tablet or visual validation until that lane or a real device
run passes.

## Internationalization continuation (2026-10-26, static)

English is the source catalog and the fallback; Spanish, Russian and Simplified
Chinese (`zh-Hans`) ship as complete translations. The native catalogs live in
`i18n/{English,Spanish,Russian,ChineseSimplified}.cpp` and are keyed by the
exact English source string (including trailing spaces and printf
conversions). Android uses `values`, `values-es`, `values-ru` and
`values-zh-rCN`. Traditional Chinese is not shipped and must fall back to
English rather than display Simplified text.

`i18n/check-i18n-catalogs.pl` is the native contract and runs from
`i18n-static`. It enforces es/ru/zh key-set parity, placeholder and
trailing-space parity, UTF-8/BOM/CRLF hygiene, plural-form arity (es two, ru
three, zh one), rejects translated configuration/protocol identifiers and
requires every `tr`, `SetStatus` and `BeginPrompt` literal to exist. C++
concatenates adjacent literals before lookup, so a multi-line argument is one
key, never its first fragment. Add the full argument when translating.

Translate at the owning boundary: `SetStatus` and `BeginPrompt` translate their
input, and `PutLine` is only a last-resort whole-line fallback (it never sees
the `"> "`/`"! "` status prefix). A confirmation hint must match the parser:
the catalogs advertise `[s/N]` and `[д/Н]`, so `IsAffirmative` accepts
`s/sí/д/да` as well as `y/yes`. Measure labels with `DisplayWidth` (wcwidth
columns), never byte length; fit the status/NetDb label column to 26 cells.

Plural lookup is keyed by the English plural text (`ntr("node", "nodes", n)`)
and the catalogs supply the language forms. Never concatenate a count with an
English plural in a translated sentence; use `ntr` (native) or `<plurals>`/
`getQuantityString` (Android). Android placeholders are positional
(`%1$s`), and every plural category the language needs must exist.

Never translate protocol verbs, IPC verbs, field names, config keys, log
levels/tokens, torrent/lease-set status wire values, peer flags, `NTCP2`/
`SSU2`/`SAM`/`I2CP`/`I2PSnark` identifiers, hashes, URLs or magnet links.
Torrent status may be localized for display only through a mapping that leaves
the wire token untouched. CLI machine-readable output and `--help`/usage prose
stay English; `autoconf_i2pd` and interactive user-facing prompts use the
shared catalogs. Standalone tools resolve `LC_ALL`/`LC_MESSAGES`/`LANG` on
first translation.

`android/tools/check-android-locales.pl` additionally enforces plural arity
and placeholders, positional formatting, catalog parity and test-source
references. User-visible validation messages use resource ids (for example
`TorrentControl.UserMessageException`), never hardcoded English. Android has no
virtual threads or JDK 25 APIs in translatable paths.

## Auxiliary tool integration continuation (2026-10-26, static)

Destination key files are written only through `tools/common/keys.hpp`
(`SecureBuffer` + `SavePrivateKeys` over `fs::AtomicWritePrivate`); never open
a key path with `O_TRUNC` or `ofstream`. Loading requires the parser to consume
every byte. A tool whose output may name its own input must call `SameFile`
and refuse. Key bytes, 0600 mode and the I2P base64/key layout are unchanged.

One `CryptoGuard` from `tools/common/crypto.hpp` owns the crypto lifetime; do
not add another local guard or manual `InitCrypto`/`TerminateCrypto` pair.
Key generation uses the core `i2p::crypto::X25519Keys`, which fails closed on
all-zero material. Numeric options use `ParseBounded` rather than a local
`from_chars` block.

Tool linkage is `libi2pd` only; `autoconf_i2pd` additionally links
`libi2pdlang`. No tool includes a `libi2pdclient` header, so the client graph
(including Boost.JSON/URL) must not be linked into the Tools component. The
getopt compatibility source is Windows-only and has a host regression built
with `I2PD_TOOLS_GETOPT_TEST`.

The tool inventory is one contract: `tools/CMakeLists.txt`, `docs/mkdocs.yml`,
`debian/i2pd.install`, both RPM specs and the manual workflow loop must name the
same executables. `tests/check-tools-inventory.cmake` is compile-free and runs
in the static job; update all five lists and the per-tool page together. The
imported manifest is `contrib/upstream/tools-files.txt`; fork-only files
(including the Perl `i2pdctl` rewrite) are documented in
`docs/tools/provenance.md`, not listed as upstream imports.

CLI contracts are tested: explicit help exits 0 on stdout, invalid usage and
runtime errors go to stderr with a documented non-zero code, protocol/data
output stays on stdout, and a step file is exactly one LF-terminated record.
`tools-cli` and `tools-getopt` are the focused regressions; `tools-smoke` and
`tools-regressions` remain the broad lanes. Never translate protocol verbs,
config keys, machine output or registration records.

Interoperable formats stay pinned: destination key layout, I2P base64, the
`name=destination#!sig=` registration grammar, RouterInfo rendering and
firewall rule text. The root POSIX Make front-end only invokes CMake/Ninja; no
GNU make is required, and no submodule or configure-time download is
reintroduced.

## Documentation and identity consolidation (2026-10-26, static)

Licensing is explicit: project code and imported documentation are
BSD-3-Clause (`LICENSE`, `android/LICENSE`, `docs/LICENSE`);
`docs/file-headers.md` and `docs/torrent/snark-compatibility.md` are also
BSD-3-Clause; `tools/LICENSE` preserves the upstream BSD text and its empty
holder/`i2pd-reseed` clause. `NOTICE` is the single inventory and must be
installed by CMake (`Documentation`), Debian and both RPM specs. The Windows
resource `LegalCopyright` must name both PurpleI2P and the fork holder.
`tests/check-legal-metadata.cmake` pins the holders, SPDX lines, NOTICE,
BSD scope and install parity; never drop the inherited holders.

`docs/i18n.md` is the single four-language policy: native/config
`en`/`es`/`ru`/`zh-Hans`, Android `en`/`es`/`ru`/`zh-CN`, English fallback,
`zh-TW`/`zh-Hant` English fallback, plural arity (es 2, ru 3, native zh 1;
Android requires `one`+`other`), the translated/identifier boundary and the
English-only documentation/store-metadata rule. README, `docs/index.md` and the
configuration table must link it. `check-i18n.cmake` pins the tags and links.

The official identity is `openi2pd` with the canonical `assets/branding/` mark
and palette; `docs/assets/branding/` is a byte-identical MkDocs mirror and must
move with it. `open-i2pd` is a frozen technical spelling (syslog identity,
Android data directory, Windows installer identity, legacy artwork filename),
not a product name. Do not replace the mark or palette; keep technical
identifiers (`i2pd`, `i2pd-tui`, `i2pdctl`, paths, service/package names,
Android application ID) unchanged.

Documentation is Markdown/MkDocs-compatible and English-only. The IPC pages
must state protocol v5, per-type registry ceilings, descriptor 3/4/5, the
25-byte child-written `WorkerHello`, cancellation/generation fencing and the
canonical TUI/torrent verbs. Keep one H1 per page, tag code fences, link the
IPC/C++23/I2PSnark/Android/OpenBSD/packaging guides, and run
`check-doc-links.pl`/`check-doc-nav.pl`/`validate-branding.pl` plus
`check-legal-metadata.cmake`, `check-upstream-pins.cmake` and
`check-i18n.cmake` in the manual static job. Upstream pins live in
`contrib/upstream/sync.sh`, `docs/upstream-sync.md`, `docs/tools/provenance.md`
and `tests/interop/*`; keep them equal.

## Service-definition consolidation (2026-10-26, static)

Service managers signal the main router process; the daemon performs the
ordered stop (close listeners, stop client/router/transit, flush NetDb, join
IPC workers). systemd must keep `KillMode=mixed` so `SIGINT` reaches only the
main PID and `SIGKILL` reaches the cgroup at the deadline. Do not switch to a
cgroup-wide stop signal: it bypasses the worker ordering and can interrupt a
storage write. Shepherd's `make-kill-destructor` necessarily signals the
process group; document that limitation instead of claiming a main-only drain.

The stop budget is 600 s of transit draining plus a 60 s shutdown margin:
`TimeoutStopSec=660` (systemd), `retry=SIGINT/660` (OpenRC with
`stopsig=SIGINT`), `timeout-kill 660000`/`timeout-finish 10000` (s6/s6-rc),
`daemon_timeout=660` with `rc_stop_signal=INT` (OpenBSD rc.d) and
`stop-timeout = 660` (dinit/Upstart/Shepherd). `SIGINT` drains and then stops;
`SIGTERM` is the prompt ordered stop. runit has no per-service timeout or
`down-signal`; keep `SIGTERM` there and never ship a `down-signal` file for
runsv. Do not claim readiness: no definition uses `sd_notify`, `READY=1` or a
`notification-fd` file, and `rc_bg=YES`/`sysvinit`/Shepherd start actions do
not prove router readiness.

`SyslogIdentifier=open-i2pd` is a frozen journal-filter identifier, not the
product name (`openi2pd`). The systemd unit owns `StateDirectory`,
`RuntimeDirectory` and `LogsDirectory`; the packaged `ExecStart` passes
`--log=stdout`, so file logging also requires an `ExecStart` override.
`LogsDirectory` makes the default file-log path writable under
`ProtectSystem=strict`; tmpfiles mirrors the same modes. Debian
`i2pd.service`/`i2pd.init` are symlinks to the canonical `contrib/` files.
OpenBSD rc.d sets `rc_stop_signal=INT` and `daemon_timeout=660`; its port
template must install `share/examples/etc/...` and `share/examples/var/i2pd`
because `@sample <absolute path>` resolves under `share/examples/`, keep
service configuration root-owned, and ship `LICENSE`/`NOTICE`/tools licence.

`SIGABRT` must keep its default disposition: an abort is a crash (exit
`128+SIGABRT`) and must never be converted into a clean `SIGINT`/`SIGTERM`
shutdown. An unhandled exception from `init`/`start`/`run`/`stop` exits `1`
without printing configuration operands. `tests/check-services.cmake` pins the
unit/script contracts, argv parity between s6 and s6-rc, exact `down-signal`/
`timeout-kill`/`timeout-finish` values and the documentation matrix; the manual
packaging workflow installs every selected `Service` component without
building the router. All of this remains static until that workflow passes.

## Packaging consolidation (2026-10-26, static)

CMake owns the distribution payload. `Documentation` installs `ChangeLog`,
`LICENSE`, `NOTICE`, `README.md`, `LICENSE.i2pd-tools` (renamed from
`tools/LICENSE`) and the filtered `docs/` and `assets/branding` trees. The
standalone `CC-BY-SA-4.0` notice file is not part of the repository or
distribution; project documentation is BSD-3-Clause and SPDX declarations
match that license.
Man pages belong to the binary component:
`i2pd.1` to `Runtime`, `i2pd-tui.1` to `Tui`, `i2pdctl.1` to `Tools`; they must
not disappear when `I2PD_INSTALL_DOCS=OFF`. A service manager with
`I2PD_INSTALL_CONFIG=OFF` is a configure error because every packaged unit
reads `/etc/i2pd/i2pd.conf`.

Debian `*.install` entries are resolved in the current directory before
`debian/tmp`. Never list `docs` or `assets/branding`; list the installed
`usr/share/doc/i2pd/...` and `usr/share/man/...` paths so CMake filtering is
preserved. `dh_missing --fail-missing` is the guard. `i2pd-tui` declares
`Breaks`/`Replaces: i2pd (<< 2.61.0-3)` for the moved binary. Debian loads the
AppArmor profile through `dh_apparmor` (the profile needs `mixr` for the
privsep worker re-exec, `flags=(attach_disconnected)`, log and socket rules),
installs `debian/i2pd.sysusers`, and runs `debian/i2pd.preinst` to move a real
`/var/lib/i2pd/certificates` directory to `.dpkg-old` before the compatibility
symlink is unpacked. `postinst` repairs only a root-owned, otherwise empty
state/log directory created by that link; it must never reset administrator
modes, and `postrm` removes data only on purge.

The OpenBSD port uses `WRKSRC=${WRKDIST}/build` with a separate `WRKBUILD`,
pins `-DI2PD_OPENSSL_EXPECT=LibreSSL`, ships `share/examples/var/i2pd` with
`subscriptions.txt`, binds `_i2pd` to login class `i2pd` in `@newuser`, and
relies on CMake for man pages. `@sample <absolute path>` resolves under
`share/examples/<absolute path>`; keep the mirrored trees exact. RPM `%doc`
copies by basename, so install `LICENSE.i2pd-tools` and `README.i2pd-tools`
explicitly; every installed desktop/icon path must appear in `%files`. The
static Debian package keeps only the certificate compatibility link. Do not
create a separate tools package without a migration decision: the tools share
the router libraries and `i2pdctl` is the primary administrative client. All
of this remains static until the manual packaging workflow passes.

## Test integration (2026-10-26, static)

Every C++ test built by `tests/CMakeLists.txt` is registered through
`i2pd_add_test()` with a label and an explicit `TIMEOUT`: 120 s normally and
300 s when any sanitizer option is enabled. The `tools/tests` host checks and
the opt-in `fuzz-smoke-*` entries use direct `add_test()` calls and register
their own timeout/labels. Forked sandbox/credential/storage-worker tests are `RUN_SERIAL`;
the remaining fixed-`/tmp` tests share the `openi2pd-fixed-tmp` resource lock so
`ctest -j` is safe. A new test must state its label, timeout and
parallel-safety decision. `-UNDEBUG` is mandatory for every assert-based
executable, including `test-tools-getopt` and libFuzzer harnesses built with
`RelWithDebInfo`.

Every executing CTest call in a workflow uses `-j` where safe,
`--no-tests=error` and `--output-junit`; the JUnit/`Testing` evidence is
uploaded. A skipped stub returns 77 with a registered `SKIP_RETURN_CODE`; it
must never print `skip:` and return 0. Do not use `continue-on-error` to hide a
failing test, and do not treat an empty CTest selection as success.

Build/test/package workflows remain `workflow_dispatch`-only;
`agent-maintenance.yml` is the sole `pull_request` gate. `workflow_call` may be
added only with an actual reusable consumer and an explicit policy update.
`tools/scripts/check-workflows.pl` is the compile-free guard (CTest
`workflow-static` and the `skills-static` job): it rejects other triggers,
`continue-on-error`, and any executing ctest line without
`--no-tests=error`/`--output-junit`.

The fuzzer inventory is a four-way contract: `tests/fuzz/*.cpp`,
`tests/CMakeLists.txt`, `.github/workflows/fuzz.yml` and `docs/testing.md`
(enforced by `check-build-system.cmake`). Fifteen targets are current; each has
checked-in `tests/fuzz/corpus/<target>/` seeds (<=64 KiB, non-secret), a
per-target `-max_len`/`-artifact_prefix` in CTest and a global manual-workflow
cap. The sanitizer matrix is ASan+UBSan, UBSan-only and TSan. Android
Gradle/JVM and instrumentation tests remain outside CTest; the manual
`android_ui` lane is the only device executor. Static review never proves a
workflow, sanitizer, fuzzer, Gradle or device result for unpublished local
changes.

## Static compile/link audit continuation (2026-10-26)

The `TEST-037` static pass corrected two `privsep/RouterStorage.cpp` compile
barriers: its local descriptor owner now implements the invoked `Release()`
operation, and the request path uses `ReceiveWithFd()` before inspecting the
`ReceivedMessage` descriptor and nested message. Public and inline headers
must continue to include their own standard declarations; this pass added the
missing direct includes for crypto, NTCP2/SSU2, streaming, tunnel endpoints,
torrents and TUI consumers. Android JNI/AIDL/resources and the explicit CMake
graph had no additional demonstrated source defect. All compilation and tests
remain pending manual CI; this entry records static inspection only.

## Latest workflow and compile/link follow-up (2026-10-26, static)

Read-only `gh` triage of the `a2b8dc48` and `b9cbdaa8` manual batches is
complete. The `b9cbdaa8` batch was cancelled after several failures; cancelled
jobs are not evidence for the rest of the matrix. Confirmed corrections are
recorded in `CI-002` and `PROGRESS.md`: the BOB const-socket peer check, Win32
token-enumerator shadowing in `privsep/WindowsIpc.cpp`, the `size_t`
`ParseList`/`ParseDictionary` returns, explicit `Mapping` assignment, the i18n
checker capture reset, the invalid CMake escapes in the static tests, the
removed Alpine `miniupnpc-static` package, the stale `i2pdctl` diagnostic
expectation, the OpenBSD `imsg_get`/`imsgbuf_get` probe, `_GNU_SOURCE` for IPC
credentials, direct standard/OpenSSL includes, the `OPENSSL_PQ 0` fallback,
the NTCP2 SipHash provider parameter, the SSU2 PeerTest null guard and the
Android status/internal-storage/reseed/dimension contracts. Keep those static
contracts in the owning checkers; they remain Pending CI compilation and
Pending CI tests until a manual run passes. No compiler, configure, CTest,
Gradle, package, validator, workflow, commit or push was performed for this
follow-up.

## CI-driven compile fixes (`88ac1e92`, 2026-10-26, static)

The first manual batches for `88ac1e92` were read with `gh` (runs 40411690766,
40411699490, 40411695544, 40411703834 and 40411709057). The detailed inventory
is `docs/ci/COMPILATION-ERRORS.md` (CE-001..CE-008); `BUILD-021` tracks the
corrections. The load-bearing lessons are:

- `libi2pd_client/HTTPProxy.cpp` is `i2p::proxy`; client types such as
  `I2PTunnelHTTPHeader` need explicit `i2p::client::` qualification. A
  compile-free checker now pins the form.
- Boost.Asio has no `ip::address::to_string(error_code&)` and
  `basic_waitable_timer` has no `cancel(error_code&)` on the supported range.
  Use `i2p::util::net::FormatAddress`/`FormatEndpoint` and `cancel()`.
- `privsep/Worker.h` is parsed by the Windows daemon. `ChildProcess` is
  POSIX-only; use `worker_pid_t` (`int` on Windows) and guard `<sys/types.h>`.
- A static link probe must order dependent archives (`libssl` before
  `libcrypto`), restore `CMAKE_REQUIRED_*`/`CMAKE_TRY_COMPILE_TARGET_TYPE`,
  and not leave later checks in archive-only mode.
- The source-docs lane must create the gitignored
  `docs/generated/source-api` before Doxygen; bare `shellcheck -s sh` treats a
  compatibility helper's SC2317/SC2329 as failure.

The `b8c6473e` follow-up (`BUILD-023`, CE-009..CE-013) adds: the X509 helper
must use const `X509_NAME_ENTRY`/`ASN1_STRING` accessors on OpenSSL 4/LibreSSL;
`LoadPrivateKeys` is `i2p::tools::LoadPrivateKeys`; a `std::string_view`
returned by `ReseedPhaseToken` needs direct `std::string` initialization;
Doxygen comment placeholders are escaped as `&lt;...&gt;` under
`WARN_AS_ERROR`; and the generic `LogPrint` excludes Asio address/endpoint
types so they use `FormatAddress`/`FormatEndpoint` instead of Boost's POSIX
`strcat` scope-ID path.

`BUILD-022` also removes the warning classes visible in the same logs:
nested comment openers, unused captures/helpers, a hidden virtual `Start`, the
OpenSSL 4 `X509_NAME_get_text_by_NID` deprecation (bounded
`i2p::crypto::GetX509NameTextByNID`), Windows CRT deprecations
(`_dupenv_s`/`_wsopen_s`/`generic_category`), and MSVC narrowing/shadowing/
unused-parameter diagnostics. Do not suppress the CRT warning family with
`_CRT_SECURE_NO_WARNINGS`; fix the call or bound the conversion.

These are static corrections only. Do not claim CI validation or a
warning-free build until a new manual dispatch on the corrected revision
passes; no compiler, configure, CTest, Gradle, Doxygen, package, workflow,
commit or push was performed for this note.

## Third CI compile and validation pass (`0da3aed0`, 2026-10-27, static)

The current published SHA was reviewed first through GitHub CLI: runs
40419430023 (Linux/Android), 40419433360 (Windows), 40419436959 (OpenBSD),
40419440596 (Fuzzing) and 40419444535 (Packaging/service validation). The
run-by-run evidence and obsolete diagnostics are in
`docs/ci/COMPILATION-ERRORS.md` CE-014..CE-021. Corrected locally: read-only
X.509 helper constness; test-fixture insertion into `variables_map`; Windows
IPC request/reply direction and restart-uptime assertions; Android opaque
tunnel-name parity; Doxygen parameter docs; ShellCheck SC2015; static-PIE
verification wording; Debian package-specific Lintian override ownership.
Earlier `Crypto.h` child-constness, b33offlinekeys namespace and JNI
`string_view` errors are obsolete by later source revisions. No current
protocol-specific or CMake configure/link root failure was observed; early
compile stops leave later Linux targets unverified. State: Implemented / static;
Pending CI compilation; Pending CI tests. No compile, link, configure, test,
Gradle, Doxygen, ShellCheck, Lintian or packaging command was run locally.

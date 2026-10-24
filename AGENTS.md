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

All builds and tests run through GitHub Actions. Workflows remain
`workflow_dispatch`-only; do not add `push`, `pull_request`, `schedule` or
`workflow_call` triggers. Do not attribute results from an earlier revision to
current local changes. Do not commit or
push without authorization. When changes exist only locally, prepare the tests
and leave their execution pending in CI.

Do not hide failures with `continue-on-error` or unjustified suppressions.
When CI reports a failure, fix the cause rather than deleting functionality,
disabling tests or indiscriminately reverting the new architecture.

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
- Keep CI manual-only (`workflow_dispatch`); do not edit triggers.
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
procedures and must not override this file.

Skills are maintained with the code. When a component, path, test or policy
changes, update the owning skill and its references. Keep shared-interface
changes under coordinator review. A subagent that identifies a needed
tracking change reports it to the coordinator; the coordinator owns
concurrent edits to `TODO.md`, `ROADMAP.md` and `PROGRESS.md`.

## Agent Knowledge Maintenance

This section is permanent and applies to the coordinator and every subagent. A
development task is not complete until the agent has reviewed the instructions
and knowledge affected by the change:

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

Security fixes additionally require a negative compile-free contract or a
focused regression that pins secret redaction, lifetime ownership, bounds and
failure behavior. A size cap alone does not mitigate slow clients: inspect the
executor for blocking calls, deadlines and admission quotas.

The review is mandatory; file edits are not. Do not generate cosmetic changes
when the existing knowledge is still correct. If a new reusable procedure has
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
- Documentation remains CC-BY-SA-4.0 as declared in its license file.
  Third-party files retain their own notices; do not relicense Apache-2.0,
  BSL-1.0, Windows SDK, OpenBSD ports or imported components.
- Preserve `LICENSE`, `NOTICE`-equivalent attribution, third-party license
  text and upstream provenance.

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
  `i2pdctl`), `android/tools/*.pl`, `tests/interop/*.pl` and their tests/docs.
- Every workflow is `workflow_dispatch`-only, actions are SHA-pinned and
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
candidates are tracked in `docs/FEATURE-PARITY.md` §10 and `PARITY-014`:
server-tunnel filters, Streaming limit actions, SAM protocol subsessions, local
proxy authentication, NetDb admission/refresh, reseed `netid` multi-source,
operator blocklist, peer/tunnel inventory and selected-file promotion/bandwidth
scheduling. No compiler, CTest, sanitizer, fuzzer, Android/Gradle/device, Java
peer or workflow validation exists.

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

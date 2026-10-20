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
`workflow_dispatch`-only, with `workflow_call` where needed. Do not attribute
results from an earlier revision to current local changes. Do not commit or
push without authorization. When changes exist only locally, prepare the tests
and leave their execution pending in CI.

Do not hide failures with `continue-on-error` or unjustified suppressions.
When CI reports a failure, fix the cause rather than deleting functionality,
disabling tests or indiscriminately reverting the new architecture.

### Tracking states

Maintain `AGENTS.md`, `TODO.md`, `ROADMAP.md` and `PROGRESS.md` as work
progresses. Distinguish exactly between:

- pending;
- in implementation;
- implemented by static analysis;
- pending CI compilation;
- pending CI tests;
- CI failure detected;
- correction pending;
- validated by CI.

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
| OpenBSD | OpenBSD subagent | `daemon/UnixDaemon.cpp`, `SSU2.cpp` |
| Linux/Windows IPC | Linux/Windows subagents | `privsep/**`, platform files |
| TUI | TUI subagent | `tui/**`, `libi2pd/TuiProtocol.h` |
| Torrent engine/API | Torrent subagent | `libi2pd_client/Torrents*` |
| Torrent TUI/IPC | Torrent TUI/IPC subagents | `tui/**`, `daemon/TuiServer.*`, `libi2pd/TuiProtocol.h` |
| Perl tooling | Perl subagent | `tools/scripts/**`, Perl tests/docs |
| Tools | Tools subagent | `tools/**` |
| Firewall | Firewall subagent | `tools/firewall.hpp`, `tools/routerinfo.cpp` |
| Android | Android subagent | `android/**` |
| Docs/identity | Documentation/Branding subagents | `docs/**`, metadata |
| Tests | Testing subagent | `tests/**` |
| CI | Coordinator | `.github/workflows/**` |

No two agents edit the same file concurrently. Shared interfaces (`Log.h`,
`RouterContext`, CMake, `TuiProtocol.h`) require coordinator review before
integration.

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
the open Agent Skills layout. Each skill has YAML frontmatter with a unique
`name` matching its directory and a `description` saying when to use it.
Longer file maps, review matrices and reconnaissance prompts live in the
skill's `references/` directory.

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

## Persistent project constraints

- Native code is C++23; CMake is primary and the root Makefile is a POSIX
  front-end using Ninja. GNU make must not be reintroduced.
- Android source/target is Java 25; a build JDK is not a runtime and no JDK may
  be bundled in the APK. Material 3/Views/Java is preferred; no Kotlin/Compose
  migration was requested.
- The torrent console is native ncursesw + local IPC. No HTML/CSS/JS console,
  Node/V8/QuickJS, browser bridge or cleartext torrent path.
- Project-maintained Python is replaced by Perl; no CPAN/runtime downloads.
- Every workflow is `workflow_dispatch`-only, actions are SHA-pinned and
  checkout credentials are not persisted.
- OpenBSD is first-class; do not weaken pledge/unveil or LibreSSL support.
- Do not claim full privilege separation until network/router/verifier
  workers, complete storage authority and negative capability tests exist and
  pass. The log worker has historical Linux runtime evidence; the opt-in
  payload-storage, router-storage and Android SAF broker boundaries are
  implemented statically and remain pending CI/device validation.
- Preserve copyright, license, NOTICE, third-party attribution and upstream
  provenance; do not reintroduce duplicated project SPDX boilerplate.

## Current uncommitted work

The historical baseline is `800c8627` (native Windows TUI/IPC); the current
published baseline is `300da64b`. The current uncommitted increment is the
selective 2026-10-18/19 `openssl` upstream review described at the end of this
file. The published baseline includes parallel OpenBSD/logging hardening, atomic TUI status
reads, RouterInfo/SSU2 bounds fixes, bounded Torrents RPC waits, C++23 ownership
modernization, the memory/concurrency audit (SSU2/RouterInfo, crypto/reseed,
BitTorrent, IPC/privsep/JNI and daemon lifecycle), dependency-floor cleanup,
the five-subsystem Java 2.13.0 static-interoperability pass, and the
LeaseSet/RouterInfo/netDb Java 2.13.0 pass, static-only crypto/client/IPC
work, the RouterContext/torrent-containment/read-only TUI-config pass with
per-tool and IPC regressions, the C++23 ownership/RAII continuation
(Kademlia/Garlic/Reseed, daemon descriptors/OpenSSL, client owners, span tools
and signature scopes), and the native init-service/Debian packaging
modernization (systemd, GNU Shepherd, OpenRC, runit, s6/s6-rc, SysVinit,
CMake `Service` selection and Debian `i2pd`/`i2pd-tui`). Historical local
Linux/GCC 16 builds and CTest runs predate the current static-only policy and
do not validate the present uncommitted tree.
Current changes are pending manual CI compilation and tests; no service
manager, Debian package, lintian, autopkgtest, init-script or daemon lifecycle
run exists. No Java peer, live floodfill, sanitizer, fuzzer, Windows, Android
or OpenBSD dynamic run exists. Agents must not revert or overwrite those
changes.
Torrent TUI control must use the bounded local IPC; it must not access engine
internals, sockets, download files or JSON-RPC. Python is not an allowed
runtime dependency for project-maintained scripts once the Perl replacement
is installed.

## I2PSnark continuation ownership/state (2026-10-19)

The actual checkout HEAD at this increment was `c8f2eba1`, initially clean.
The older baseline description above is historical. Current torrent/native
Android changes are implemented by static analysis only, pending CI compilation
and pending CI tests. Task inventory: SNARK-001..SNARK-008 in TODO; functional
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

Current continuation started from clean `0cf32519`; all new changes remain
local and pending CI compilation/tests. Preserve the user-requested descriptive
license-reference headers and all existing notices/provenance. The reference
excerpts outside this repository are not a complete verified clone. No task is
validated without its corresponding manual CI pass.

## Selective upstream openssl integration (2026-10-19)

The current increment reviews `PurpleI2P/i2pd:openssl` through
`472ef88b` for commits dated 18–19 October 2026 UTC. The exact disposition,
omissions and rationale are in `docs/upstream-sync.md`; task tracking is
`UPSTREAM-001..UPSTREAM-004` in `TODO.md`. The fork keeps its C++23,
privilege-separation, native TUI/Android, CMake and release identity.

The implemented corrections are the explicit SSU2 PathChallenge state guard,
the I2PTunnel response/IRC `view()` and header-compaction fixes, the NetDb
0.9.57 general floor with the 0.9.62 high-bandwidth floor retained, and the
torrent stopped/removed persistence guards. The browser-console commit and the
upstream 3.0.0 release metadata are intentionally omitted. All changes are
static-only pending manual CI compilation and tests; no build, CTest,
sanitizer, package or live-peer evidence exists for this increment.

## Native payload continuation (2026-10-20)

Actual checkout HEAD remains `0cf32519`; existing uncommitted changes were
preserved. The native payload abstraction now connects Android SAF document
read/write and seeding through a non-exported separate-process AIDL/JNI broker.
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
Metadata paths are narrow and capped8MiB, chunks16KiB; fixed staging slots cap
crash leftovers. Keep DHT/router persistence, network/router/verifier processes
and global enforcement open. No global separation claim is justified.

All new recovery/lifecycle/provider tests are prepared but unexecuted; current
changes remain implemented by static analysis, pending CI compilation and tests.

## DHT/router persistence and SAF administrative completion

Isolated tunnels now delegate DHT load/save under the original torrents state
root, preserving54-byte records and explicit legacy flat filename authority.
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

## Java feature-parity continuation (2026-10-20)

The latest stable reference was rechecked as `i2p-2.13.0`, commit
`9134f808337b401e8e53c73734c81fab04280c9d`; 2.14.0 had not been released. Seven
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
one discovery index and 16 subsystem skills, each with valid frontmatter and a
`references/` checklist. `agent-skills-static` checks the frontmatter, required
sections and reference links without executing a skill. The permanent policies
above remain authoritative; skills link to them and do not authorize local
builds, tests, workflow dispatches, commits or pushes. Documentation-link,
branding and agent-skills checks are pending manual CI; the collection is not
dynamically validated.

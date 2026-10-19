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
implementations, review diffs statically, record integration risks and update
the tracking files.

## Hard rules

- Do not create commits, pushes, tags or releases.
- Do not create worktrees or temporary commits unless the coordinator
  explicitly allows it.
- Do not overwrite existing modifications. Inspect `git status` and `git diff`
  before editing.
- Do not edit files outside the assigned scope.
- Do not modify `TODO.md`, `ROADMAP.md` or `PROGRESS.md`; report changes to the
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

- Upstream-derived project code/config files retain the two `LICENSE` copyright
  lines and `SPDX-License-Identifier: BSD-3-Clause`. New files written for
  openi2pd that were not copied from an i2pd repository carry only
  `Copyright (c) 2026, David Uhden Collado` and the SPDX identifier.
- Documentation remains CC-BY-SA-4.0 as declared in its license file.
  Third-party files retain their own SPDX/license notices; do not relicense
  Apache-2.0, BSL-1.0, Windows SDK, OpenBSD ports or imported components.
- Preserve `LICENSE`, `NOTICE`-equivalent attribution, copyright and SPDX text.

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
- Do not claim full privilege separation until network/storage workers,
  supervisor and negative capability tests actually exist. The log worker is
  only the first isolated component.
- Preserve copyright, license, SPDX, NOTICE and upstream provenance.

## Current uncommitted work

The committed baseline is `800c8627` (native Windows TUI/IPC). The current
uncommitted work contains parallel OpenBSD/logging hardening, atomic TUI status
reads, RouterInfo/SSU2 bounds fixes, bounded Torrents RPC waits, C++23 ownership
modernization, the memory/concurrency audit (SSU2/RouterInfo, crypto/reseed,
BitTorrent, IPC/privsep/JNI and daemon lifecycle), dependency-floor cleanup,
the five-subsystem Java 2.13.0 static-interoperability pass, and the
LeaseSet/RouterInfo/netDb Java 2.13.0 pass, static-only crypto/client/IPC
work, the RouterContext/torrent-containment/read-only TUI-config pass with
per-tool and IPC regressions, and the C++23 ownership/RAII continuation
(Kademlia/Garlic/Reseed, daemon descriptors/OpenSSL, client owners, span tools
and signature scopes). Historical local Linux/GCC 16 builds and CTest
runs predate the current static-only policy and do not validate the present
uncommitted tree.
Current changes are pending manual CI compilation and tests; no Java peer, live
floodfill, sanitizer, fuzzer, Windows, Android or OpenBSD dynamic run exists.
Agents must not revert or overwrite those changes.
Torrent TUI control must use the bounded local IPC; it must not access engine
internals, sockets, download files or JSON-RPC. Python is not an allowed
runtime dependency for project-maintained scripts once the Perl replacement
is installed.

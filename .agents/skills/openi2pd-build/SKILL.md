---
name: openi2pd-build
description: Use when changing the openi2pd CMake graph, POSIX Make front-end, dependency floors, compiler/platform options, test registration or sanitizer configuration.
---
<!--
See the LICENSE file at the top of the project tree for copyright
and license details.
-->

# openi2pd build system

## Activate when

Use this skill for `build/CMakeLists.txt`, the root `Makefile`,
`tests/CMakeLists.txt`, `tools/CMakeLists.txt`, Android JNI CMake, dependency
floors, component/install graph, sanitizer options or test labels. Use
`openi2pd-ci` for workflow dispatch and job triage; use
`openi2pd-packaging` for distro package scripts.

Read [`../../../AGENTS.md`](../../../AGENTS.md) first. CMake remains the single
source of build rules; the root Makefile is a POSIX front-end using Ninja.

## Inspect first

- `build/CMakeLists.txt`, root `CMakeLists.txt` if present, root `Makefile`,
  `tests/Makefile`, `tests/CMakeLists.txt`, `tools/CMakeLists.txt`.
- `android/app/src/main/cpp/CMakeLists.txt` and the Android vcpkg triplet when
  the native graph is shared.
- Relevant workflow configure lines in `.github/workflows/`.
- [`../../../docs/build-system.md`](../../../docs/build-system.md),
  [`../../../docs/dependencies.md`](../../../docs/dependencies.md) and
  [`../../../docs/testing.md`](../../../docs/testing.md).

## Reference sources

- [`../../../docs/build-system.md`](../../../docs/build-system.md) — components,
  options, generator compatibility, service selection and validation status.
- [`../../../docs/dependencies.md`](../../../docs/dependencies.md) — dependency
  floors, removed branches and OpenBSD notes.
- [`../../../docs/testing.md`](../../../docs/testing.md) — CTest labels,
  sanitizer/fuzz lanes and manual CI matrix.
- [`../../../docs/ci.md`](../../../docs/ci.md) — manual workflow policy.
- [`references/build-matrix.md`](references/build-matrix.md) — targets, options
  and platform lanes.

## Reconnaissance

Launch at least one read-only `explore` subagent before editing, split across
independent areas when useful. If subagents are unavailable, report that
limitation; do not substitute local execution. Ask for:

1. target/component membership and explicit source lists;
2. option defaults, dependency checks and platform conditionals;
3. test registration/labels and platform-only tests;
4. install components, service selection and packaging consumers;
5. workflow configure/build commands that would consume the change.

Ask for exact target names and line references. Do not let them configure or
build.

## Parallel implementation

- The coordinator owns `build/CMakeLists.txt` and shared target interfaces.
  This skill owns CMake-side test registration; `openi2pd-ci` owns workflow
  wiring and label-gate verification.
- Split independent component/option changes; do not edit the same CMake
  target from two workstreams.
- Keep test registration changes with the test source change, but do not
  duplicate labels or register a platform-only test on the wrong host.
- Packaging and CI follow-up edits belong to their skills; build owns the
  graph they consume.

## Conventions

- Keep sources explicit; do not reintroduce `file(GLOB ...)`.
- Preserve target boundaries: `libi2pd`, `libi2pdclient`, `libi2pdlang`,
  `i2pd_privsep`, `i2pd`, `i2pd-tui`, tools, tests, Android JNI.
- Defaults: `WITH_TORRENTS=ON`, `WITH_UPNP=ON`, `I2PD_BUILD_TUI=ON`,
  `I2PD_BUILD_TOOLS=ON`, `BUILD_TESTING=OFF`; allow
  `-DWITH_TORRENTS=OFF` on Linux, OpenBSD and Windows without breaking the
  graph.
- Keep `I2PD_BUILD_JNI=OFF` outside Android; reject unsupported combinations
  clearly.
- The POSIX Make front-end must use only portable constructs and Ninja; no GNU
  make fragments, `?=`, `wildcard`, `shell`, `patsubst`, `ifeq` or includes.
- Do not add configure-time downloads, submodules, in-tree globs, hidden
  checkouts or absolute install prefixes.
- Preserve dependency floors and fail with a clear message when a required
  version is absent. No silent fallback to an old API.
- Install exactly one service manager through the `Service` component; do not
  let packaging install competing definitions.

## Known issues and decisions

- Windows `libi2pd` exposes `advapi32` transitively because `FS.cpp`
  constructs protected DACLs for `AtomicWritePrivate`; static tools/tests must
  resolve those imports. `build-system-static` pins the dependency, while
  MSYS2/MSVC link validation remains pending manual CI.

- `test-datagram-receiver-lifecycle` and `test-netdb-stop-admission` join the
  full manual CTest/sanitizer inventory with i2p/security/memory labels. The
  NetDb fixture opens admission with no worker, avoiding reseed/network startup;
  it does not establish full daemon restart correctness.


- CONC-010 uses immutable process-local profile-root authority and flushed
  same-directory replacement; storage IO/cleanup is serialized and shutdown
  drains postponed updates after producers stop. Cross-process ownership,
  abandoned staging cleanup and retained external mutators remain open.
  `test-router-profile-storage` is prepared, not executed.


- `routerinfo-fixture` is a BUILD_TESTING-only helper outside the installed
  tools inventory. Its production serializer signs only public test data in
  the CI work directory. Pass its exact target path to `tools-smoke`; keep
  `test-router-profile-race` and `test-android-daemon-lifecycle` in host CTest.
- Boost.JSON and Boost.URL require compiled implementations: torrent builds
  link `Boost::json`/`Boost::url`, and the installed config re-finds the same
  enabled components. Header discovery alone cannot establish link closure.
  `test-boost-linkage` and the manual installed-consumer lane cover both paths.
- Windows SDK macros are PUBLIC core usage requirements; `WIN32_APP` remains
  PRIVATE. MinGW hardening uses PE flags, while Unix uses ELF `-z` flags.
- `language-modernization-static` is a compile-free but literal contract: it
  pins C++23/Java 25 levels, `minSdk 29`, the availability matrix and named
  seams (`Modern.h`, `ITransportConnector`, `jthread`/`stop_token`,
  `GetSigningKeySizes`, `RouterCommand`, `LogLevel`, `switch (command)`).
  Renaming or moving a seam requires updating the check in the same change.
- Keep the `Tui`/`Runtime`/`Libraries`/`Development`/`Tools`/`Documentation`
  components and exclusive `Service` selection coherent with packaging.
- CMake is primary and Ninja is the default; the root Makefile remains a POSIX
  front-end. Do not reintroduce GNU make or hidden source globs/downloads.
- `i18n-static` pins native catalog registration, TUI `libi2pdlang` linkage and
  Android locale metadata; a new language/catalog must update it and its CTest
  registration in the same change.
- Configure/build/CTest, component consumers and OpenSSL/LibreSSL lanes remain
  pending manual CI.

## Do not

- Do not configure, build, run CTest, `make`, Ninja, Gradle, sanitizers,
  fuzzers or project validators locally.
- Do not reintroduce GNU make, the HTTP console, web assets, submodules or
  hidden downloads.
- Do not weaken hardening/sanitizer checks or suppress warnings to force a
  build.
- Do not make an option silently change a wire/persisted format or a security
  boundary.
- Keep every workflow `workflow_dispatch`-only; do not add `push`, `pull_request`, `schedule` or automatic `workflow_call` triggers.
- Do not create commits, tags, releases, worktrees or temporary commits, and do not push; inspect `git status`/`git diff` before editing.
- Do not install dependencies, fetch/vendor code or perform hidden downloads to enable a local build or test. Preserve LICENSE/NOTICE/provenance and technical identifiers/paths unless an approved migration exists (AGENTS.md).

## Prepare for CI

- Keep `build-system-static` assertions coherent for source lists, dependency
  floors, removed fallbacks, test inventory, install components and options.
  Keep `agent-skills-static` coherent when a test or workflow path is referenced
  by a skill, and `language-modernization-static` coherent when a modernization
  seam/path is renamed or moved.
- Register tests with the correct labels and platform conditions.
- Verify configure lines by inspection for Linux/Clang, Linux/GCC, OpenBSD,
  Windows/MSYS2, MSVC, Android and sanitizer jobs.
- Add a CTest smoke or static check when the graph change is not exercised by
  an existing target.
- Record compilation/tests as pending until the manual workflows pass.

## Tracking

Use `BUILD`/`DEPS`/D or the architecture area. Record target/option changes,
consumers, test registration and platform lanes. Propose a dated `PROGRESS.md` entry and any `TODO.md`/`ROADMAP.md` changes to the coordinator; append them only when explicitly assigned; subagents report proposed rows to the coordinator.

Record the exact AGENTS.md `Tracking states` value and state which validations were not run; do not collapse them into “pending”.

## References

- [Build matrix](references/build-matrix.md)
- [Build system](../../../docs/build-system.md)
- [Dependencies](../../../docs/dependencies.md)
- [Global policy](../../../AGENTS.md)

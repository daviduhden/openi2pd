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

Launch read-only `explore` subagents for:

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

## Do not

- Do not configure, build, run CTest, `make`, Ninja, Gradle, sanitizers,
  fuzzers or project validators locally.
- Do not reintroduce GNU make, the HTTP console, web assets, submodules or
  hidden downloads.
- Do not weaken hardening/sanitizer checks or suppress warnings to force a
  build.
- Do not make an option silently change a wire/persisted format or a security
  boundary.
- Do not commit, push, tag or release.

## Prepare for CI

- Keep `build-system-static` assertions coherent for source lists, dependency
  floors, removed fallbacks, test inventory and options.
- Register tests with the correct labels and platform conditions.
- Verify configure lines by inspection for Linux/Clang, Linux/GCC, OpenBSD,
  Windows/MSYS2, MSVC, Android and sanitizer jobs.
- Add a CTest smoke or static check when the graph change is not exercised by
  an existing target.
- Record compilation/tests as pending until the manual workflows pass.

## Tracking

Use `BUILD`/`DEPS`/D or the architecture area. Record target/option changes,
consumers, test registration and platform lanes. Append a dated `PROGRESS.md`
entry; subagents report proposed rows to the coordinator.

## References

- [Build matrix](references/build-matrix.md)
- [Build system](../../../docs/build-system.md)
- [Dependencies](../../../docs/dependencies.md)
- [Global policy](../../../AGENTS.md)

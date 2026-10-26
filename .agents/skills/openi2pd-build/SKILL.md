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
  the `tests/Makefile` forwarding shim, `tests/CMakeLists.txt`,
  `tools/CMakeLists.txt` and `build/CMakePresets.json`.
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
- The root POSIX Make front-end must use only portable constructs and Ninja;
  no GNU make fragments, `?=`, `wildcard`, `shell`, `patsubst`, `ifeq` or
  includes. `tests/Makefile` only forwards to the root file; keep a single
  CMake configuration and build directory.
- Do not add configure-time downloads, submodules, in-tree globs, hidden
  checkouts or absolute install prefixes.
- Preserve dependency floors and fail with a clear message when a required
  version is absent. No silent fallback to an old API.
- Install exactly one service manager through the `Service` component; do not
  let packaging install competing definitions.

## Known issues and decisions

- `i2pd_apply_dialect()` is the single compile contract for capability macros,
  `BOOST_ASIO_HAS_SNPRINTF` and the Windows SDK macros. `libi2pd` publishes it,
  `i2pd_privsep`/`i2pd-tui` consume it privately. `libi2pd` exposes one merged
  Windows import list (`ws2_32 mswsock iphlpapi shell32 advapi32`); `i2pd` adds
  only `user32 ole32 uuid` and `WIN32_APP`. Do not reintroduce `wsock32`,
  `crypt32` or per-tool copies of the WSA libraries.
- `WITH_STATIC` is rejected with sanitizer options and the Android JNI library.
  The static library-suffix override is restored after dependency discovery so
  tools/tests/TUI keep their normal search suffixes. `CheckAtomic` reports
  `I2PD_ATOMIC_LIBRARIES` and restores `CMAKE_REQUIRED_LIBRARIES`.
- Boost.Container is an optional component of the torrent graph: static
  Boost.JSON needs its compiled `pmr` symbols, shared builds normally resolve
  it transitively. Keep it optional in both `build/CMakeLists.txt` and the
  installed `i2pdConfig.cmake`.
- `i2pd_privsep` is exported because static `libi2pdclient` records a
  `LINK_ONLY` edge. Its headers are installed under `include/privsep/` and its
  include interface is PUBLIC; do not drop the export without replacing that
  static-link edge.
- `i2pdConfig.cmake` requests the `Libraries`/`Development` package components,
  keeps the zlib and Boost floors, requests both OpenSSL components and treats
  a missing Development component as a normal not-found result instead of a
  fatal `set_and_check` abort.
- The `default`/`portable`/`ci-linux` presets use schema 5 (CMake 3.24) and
  expose install presets; `portable` disables tests. Keep presets in sync with
  the documented option defaults.

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
- OpenBSD base ncurses is wide-character capable but installs `ncurses.h`
  directly and exposes `libncursesw` as a link to `libcurses`; CMake's
  `CURSES_NEED_WIDE` search only accepts the `ncursesw/` header layout. The
  build clears the cached Curses finder results before the retry, probes the
  first resolved `CURSES_LIBRARIES` entry for `wadd_wch`, and unsets
  `CURSES_NEED_*` afterwards.
  The OpenBSD workflow must not `pkg_add ncurses` (no such package).
- Windows platform modules normally enable RC themselves. The explicit
  `include(CheckLanguage)`/`check_language(RC)` probe is defensive because
  `enable_language(... OPTIONAL)` is only a placeholder in CMake and still
  errors when no resource compiler exists; the resource is added only when
  `CMAKE_RC_COMPILER` is set, and `${LIBI2PD_DIR}` lets `Resource.rc2` include
  `version.h`.
- OpenSSL probe results are cache entries. `I2PD_OPENSSL_PROBE_STAMP` records
  the selected include dir/kind/version and clears every `I2PD_HAVE_*` when it
  changes. One combined link probe checks the actual headers+libraries; do not
  turn all probes back into compile-only checks. Both `SSL_set1_dnsname` and
  `SSL_set1_host` are probed; neither is optional at configure time.
- Every `I2PD_HAVE_*` OpenSSL capability macro has an `#ifndef … #define 0`
  fallback in `Crypto.h`. Keep `I2PD_HAVE_SSL_SET1_DNSNAME` and
  `I2PD_HAVE_OPENSSL_MLKEM_H` there: the former selects OpenSSL 4's
  replacement for the deprecated `SSL_set1_host`, the latter gates LibreSSL
  ML-KEM on the separate header.
- `language-modernization-static` is a compile-free but literal contract: it
  pins C++23/Java 25 levels, `minSdk 29`, the availability matrix and named
  seams (`Modern.h`, `ITransportConnector`, `jthread`/`stop_token`,
  `GetSigningKeySizes`, `RouterCommand`, `LogLevel`, `switch (command)`).
  Renaming or moving a seam requires updating the check in the same change.
- Keep the `Tui`/`Runtime`/`Libraries`/`Development`/`Tools`/`Documentation`
  components and exclusive `Service` selection coherent with packaging.
- CMake is primary and Ninja is the default; the root Makefile remains a POSIX
  front-end. Do not reintroduce GNU make or hidden source globs/downloads.
- Missing Perl announces that the static interop/doc/i2pdctl checks are not
  registered; `I2PD_REQUIRE_PERL=ON` makes that a configure error. Likewise
  `I2PD_PERL_CRITIC=ON` now fails when `perlcritic` is unavailable instead of
  silently skipping the requested check.
- `i18n-static` pins native catalog registration, TUI `libi2pdlang` linkage and
  Android locale metadata; a new language/catalog must update it and its CTest
  registration in the same change.
- Android defaults to the arm64-v8a ABI because only that vcpkg prefix is
  maintained; `-Pi2pd.abis=` overrides it. Gradle passes
  `-DANDROID_STL=c++_shared` and the wrapper sets
  `CMAKE_FIND_ROOT_PATH_MODE_{LIBRARY,INCLUDE,PACKAGE}=BOTH`; the JNI target no
  longer compiles the AF_UNIX TUI server.
- Configure/build/CTest, component consumers and OpenSSL/LibreSSL lanes remain
  pending manual CI.

- Android Java 25: `sourceCompatibility`/`targetCompatibility` 25 and the exact
  JDK 25 toolchain are pinned. The Java probe must dex **all** classes and
  require DEX 039 at `--min-api 29`; the packaged DEX must not contain
  `java/lang/Record`, `ObjectMethods`, `SwitchBootstraps` or `MatchException`.
  AGP's D8/R8 artifact is unpinned and remains a manual-CI assumption, as do
  Lint class-file-69 handling and platform-37 resolution. Never lower Java to
  make Kotlin/AGP pass; use the documented built-in-Kotlin disablement.

- CTest registration goes through `i2pd_add_test()`; every C++ test gets
  `TIMEOUT ${I2PD_TEST_TIMEOUT}` (120 s, 300 s with any sanitizer). Tests that
  fork a sandbox/privileged worker or own a storage worker are
  `RUN_SERIAL`; the remaining fixed `/tmp` tests share the
  `openi2pd-fixed-tmp` resource lock so `ctest -j` is safe. Do not add a new
  test without a label, a timeout and a parallel-safety decision.
- Every executing CTest invocation in a workflow uses `-j` (where safe),
  `--no-tests=error` and `--output-junit`; JUnit/CTest logs are uploaded as
  evidence. `tools/scripts/check-workflows.pl` (CTest `workflow-static`)
  rejects other triggers, `continue-on-error` and any ctest command missing
  those flags.
- `-UNDEBUG` is mandatory for every assert-based executable, including
  `test-tools-getopt` and the libFuzzer harnesses built with
  `RelWithDebInfo`. Use `i2pd_enable_sanitizers()` for the sanitizer flags;
  do not hand-roll a second sanitizer matrix.
- `test-x25519-rfc7748` is the provider-independent RFC 7748 KAT. Keep
  known-answer tests free of generated identities and wall-clock/network
  dependencies; a random fixture cannot pin a wire vector. The pure
  `ValidateSSU2PayloadBlocks`/`ParseDatabaseSearchReply` seams are the model
  for socket-free negative matrices; do not require a router for a parser
  contract.
- The sanitizer matrix is ASan+UBSan, UBSan-only and TSan; sanitizer CTest is
  serial (`-j 1`) with 300 s per-test budgets, release/platform lanes may use
  `-j 2` only with `RUN_SERIAL`/`RESOURCE_LOCK`. `tools-regressions` has its
  own 600 s budget and `tools-inventory` 30 s, so the 120 s default is not a
  universal cap.
- The fuzzer inventory is a four-way contract: `tests/fuzz/*.cpp`,
  `tests/CMakeLists.txt`, `.github/workflows/fuzz.yml` and the target list in
  `docs/testing.md` (enforced by `check-build-system.cmake`). Fifteen targets
  are current; adding one requires all four places plus a CTest smoke entry,
  a checked-in `tests/fuzz/corpus/<target>/` seed (<=64 KiB, non-secret), a
  per-target `-max_len`/`-artifact_prefix` and a `fuzz.yml` run-loop entry.
  The manual workflow applies one global `-max_len=65536`; do not treat it as
  a substitute for the per-target CTest bound (notably `fuzz-signature-der`).


- Preset schema 5 uses `installPresets`, which requires CMake 3.25 even though
  the project configures at the 3.24 floor. Keep
  `CMakePresets.cmakeMinimumRequired.minor` at 25 and document the split.
- A compile-only OpenSSL probe cannot see a missing header for a type used
  later in the same snippet. The combined linked probe must include every
  header it calls into (`<openssl/bn.h>` for `BIGNUM`/`BN_free`,
  `<openssl/kdf.h>` for the KDF surface) or every OpenSSL lane fails
  configure with a misleading "no linked modern API" error.
- `libi2pd` owns `${I2PD_EXTRA_LIBS}` (libatomic when the host needs it);
  `i2pd_privsep` and both TUI backends use atomics too and must inherit it.
  The exported `i2pd_privsep` archive also needs its PUBLIC `libi2pd` edge so
  an installed consumer links the core symbols.
- Install each file from exactly one component. `tools/LICENSE` is installed
  as `LICENSE.i2pd-tools` by `Documentation` when docs are enabled and by
  `Tools` only when `I2PD_INSTALL_DOCS=OFF`; two rules for one destination
  make per-component installs race.

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

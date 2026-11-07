---
name: openi2pd-build
description: Use when changing the openi2pd CMake graph, POSIX Make front-end, dependency floors, compiler/platform options, test registration or sanitizer configuration.
---

## Maintenance & Continuous Learning

When this skill guides a repository change, review it after implementation. Update procedures, tools, configurations, assumptions, examples and validation steps when they changed or when reusable knowledge improves future work; correct inaccurate guidance and omit one-off execution details. Report synchronization in the task summary. Follow the normative process and Definition of Done in [AGENTS.md](../../../AGENTS.md#continuous-agent--skills-maintenance).
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
- Keep each target recipe within checkmake's five-line `maxbodylength`
  default. For shell guards, compact related branches into one POSIX shell
  command per recipe line without changing the guarded operation or failure
  status; avoid blanket linter-rule suppression.
- Do not add configure-time downloads, submodules, in-tree globs, hidden
  checkouts or absolute install prefixes.
- Preserve dependency floors and fail with a clear message when a required
  version is absent. No silent fallback to an old API.
- When a toolchain warning names an inlined standard-library operation,
  inspect the originating project call and its size/lifetime invariants before
  classifying it as a toolchain false positive. For a warning-policy change,
  remove suppressions only by diagnosing newly exposed warnings; do not add a
  global warnings-as-errors gate while supported first-party lanes still have
  unresolved warnings. Keep the run/SHA and disposition in
  `docs/ci/COMPILATION-WARNINGS.md` and follow the repo-wide search procedure
  in `docs/ci/COMPILATION-PATTERNS.md`.
- Install exactly one service manager through the `Service` component; do not
  let packaging install competing definitions.

## Known issues and decisions

- Perl-enabled test builds require Git 2.28+ for isolated validator
  fixtures. CMake resolves Git before registering that contract and adds
  its directory to the test PATH; OpenBSD's VM must install Git explicitly.
  Runtime-only builds do not acquire this dependency. Reconcile installed
  filename assertions with exact install artifacts after payload renames;
  the consumer lane checks lowercase changelog, matching CMake's rename.
- Platform feature macros can already come from dependency headers or
  compiler definitions. Guard local NOMINMAX/Windows lean declarations
  with ifndef instead of changing their value or suppressing redefinition
  warnings; inspect sibling platform test translation units as well.

- Native tool man pages and their shared reference belong to Tools, even
  with I2PD_INSTALL_DOCS=OFF. The install regression checks each command's
  alias and its shared target on all host platforms. Install the upstream
  ChangeLog under the canonical documentation name changelog; Debian smoke
  expects the compressed name after debhelper processes the staged file.
  When one install statement lists shared manuals, inspect its actual closing
  delimiter rather than a fixed-size lookahead that can miss its component.

- `I2PD_WARNINGS_AS_ERRORS` defaults to `OFF`. Explicit manual matrix
  configurations may enable it to add `/WX` or `-Werror` through
  `i2pd_warnings`; do not set global compiler flags or export this policy
  to installed consumers. Imported dependencies are not rebuilt with this
  option, but their headers can still produce diagnostics in project
  translation units. Keep opt-in matrix validation pending until executed.
- Standalone recursive inventory contracts explicitly select CMP0009 NEW
  so directory symlinks cannot expand the inspected source tree. A script
  run with `cmake -P` does not inherit the build's minimum-version policies.
- The POSIX Make front-end delegates to `build/make-driver.sh`; preserve the
  prerequisite graph and pass expanded CMake flags as trailing argv, never
  reparse an environment string with eval. Keep cleanup refusal and prefix/
  DESTDIR handling in the helper. Debian and OpenBSD ports retain their own
  framework dialects; they are not evidence that the native build needs GNU
  make. Mock-driver regressions must verify argv and ordering without a build.

- Validator syntax/critic registration explicitly lists the entry point, its
  core module tree and the Test::More contract. Compare the module list with
  recursive portability discovery; do not introduce CMake source globs.
  Keep the workflow lane aligned; `.pm` files need strict/warnings
  and a Perl floor, not an executable shebang. CTest registration must provide
  a direct Perl fallback when `prove` is unavailable on a base system.

- The MSVC CI lane retains optimized Release symbols using CMP0141 NEW,
  ProgramDatabase and /DEBUG:FULL with /OPT:REF and /OPT:ICF. That CI
  symbol property needs CMake 3.25+, independently of the project's 3.24
  floor. Keep exact test EXE/PDB artifacts with heap reproduction logs;
  a PDB from a different build is not evidence for a crash address.
- When dependency-policy warnings appear only in installed consumers, review
  both the project `find_package()` and exported `find_dependency()` calls.
  Guard policy selection with `if(POLICY ...)` for the supported minimum and
  keep package-config policy changes local to the included config scope.
- A checker run with `cmake -P` has no `cmake_minimum_required()`. With
  CMP0012 OLD, `while(TRUE)`/`if(TRUE)` treats `TRUE` as an undefined
  variable and a TRUE-valued variable as an undefined name; use
  `while(1)`/`if(1)` and declare `cmake_policy(SET CMP0012 NEW)` before any
  boolean-valued branch. A Debian CMake 3.x packaging job can fail or
  silently weaken a checker that passes under a newer local CMake (CE-071).
- `tests/check-build-system.cmake` anchors each `add_test(NAME)` to its own
  `set_tests_properties` call, detects the two-statement
  `temp_directory_path()` form, accepts `RUN_SERIAL` as well as the shared
  resource lock, and pins `I2PD_OPENSSL_EXPECT_MAJOR`. `string(FIND)` has no
  start offset; slice the suffix and convert the relative match.
- The manual Linux platform lanes are two branch workflows: latest Homebrew
  `openssl@3` and `openssl@4`, each with GCC and Clang. They consume bottled
  dependencies and verify the installed OpenSSL matches the formula; there is
  no pinned source-built OpenSSL lane anymore.

- Man pages belong to their component on every non-Android platform. The
  install rules are guarded by `NOT ANDROID`, not `UNIX`, and
  `tests/check-build-system.cmake` rejects a UNIX-only guard.

- In `install(TARGETS ...)`, assign named components on each `ARCHIVE`,
  `LIBRARY` and `RUNTIME` artifact group when consumers install components
  separately. The exported target can otherwise refer to a static archive
  that was omitted from the selected component; the installed-consumer lane
  must inspect both the staged files and the imported target closure.
- `tests/check-build-system.cmake` also pins the `Expected.h` include-interface
  rationale in `build/CMakeLists.txt`. When reflowing that comment, preserve
  the recognizable `Expected.h lives in libi2pd` marker or update the checker
  in the same change; a failed marker check is not itself a compiler error.
- Keep platform-specific test fixtures and helper functions inside the same
  preprocessor branch as their consumers. Windows Clang warns on POSIX-only
  symlink helpers compiled outside `_WIN32` guards even when those helpers are
  never called on Windows.
- CMake 3.30+ warns when CMP0167 is unset at `find_package(Boost)`. Set the
  policy to NEW only when `POLICY CMP0167` exists; this selects Boost's config
  package on newer CMake while preserving the bundled `FindBoost` path on the
  supported CMake 3.24 floor. Keep Boost component and target closure checks.

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
  PRIVATE. MinGW hardening uses PE flags, MSVC uses `/GS`, `/sdl`, `/guard:cf`,
  `/DYNAMICBASE`, `/NXCOMPAT` and 64-bit `/HIGHENTROPYVA`, while Unix uses ELF
  `-z` flags. Keep the supported compiler floor and static build-system
  contract aligned when changing this matrix.
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
  `tests/CMakeLists.txt`, `.github/workflows/validation.yml` and the target list in
  `docs/testing.md` (enforced by `check-build-system.cmake`). Fifteen targets
  are current; adding one requires all four places plus a CTest smoke entry,
  a checked-in `tests/fuzz/corpus/<target>/` seed (<=64 KiB, non-secret), a
  per-target `-max_len`/`-artifact_prefix` and a `validation.yml` run-loop entry.
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


- Static archives are single-pass: list the dependent `libssl` before
  `libcrypto` in `CMAKE_REQUIRED_LIBRARIES`, and keep the order in both probe
  stages. Shared builds hide the defect; only the Alpine `WITH_STATIC` lane
  exposes it (`CE-004`).
- Save and restore `CMAKE_REQUIRED_INCLUDES`, `CMAKE_REQUIRED_LIBRARIES`,
  `CMAKE_REQUIRED_LINK_OPTIONS` and the original
  `CMAKE_TRY_COMPILE_TARGET_TYPE` around the linked probe. Do not re-read
  variables after unsetting them; later `check_library_exists` must still use
  an executable probe.
- Boost.Asio API floor: use the project's address-format helpers instead
  of depending on deprecated `to_string(error_code&)` overloads.
  For `basic_waitable_timer`, use the
  non-deprecated `cancel()`/`cancel_one()` API: Boost 1.74 documented a
  deprecated error-code overload, but newer releases remove it. Use
  `i2p::util::net::FormatAddress`/`FormatEndpoint` and `cancel()` directly
  (`CE-002`, `CE-008`).
  Compare the [1.74 timer reference](https://www.boost.org/doc/libs/1_74_0/doc/html/boost_asio/reference/basic_waitable_timer/cancel.html)
  with the [current timer reference](https://www.boost.org/doc/libs/latest/doc/html/boost_asio/reference/basic_waitable_timer/cancel.html)
  when reviewing compatibility. Socket `cancel(error_code&)` remains a
  separate API; do not remove its nonthrowing error path indiscriminately.
- The manual source-docs job must create the gitignored
  `docs/generated/source-api` before Doxygen. `WARN_AS_ERROR` turns a missing
  output directory into a job failure (`CE-003`).


- Doxygen treats `<word>` in comments as an XML/HTML tag under
  `WARN_AS_ERROR`. Escape placeholders as `&lt;...&gt;` (or avoid angle
  brackets) in headers/protocol prose; comments-only edits can otherwise fail
  the source-docs lane (CE-012).

## Do not

- Do not configure, build, run CTest, `make`, Ninja, Gradle, sanitizers,
  fuzzers or dynamic project tests locally. Compile-free structural checks
  required by `AGENTS.md` may run locally.
- Do not reintroduce GNU make, the HTTP console, web assets, submodules or
  hidden downloads.
- Do not weaken hardening/sanitizer checks or suppress warnings to force a
  build.
- Do not make an option silently change a wire/persisted format or a security
  boundary.
- Keep build/test/package workflows `workflow_dispatch`-only. The sole
  pull-request exception is the compile-free `agent-maintenance.yml` gate;
  use `openi2pd-ci` for workflow trigger policy and validation.
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

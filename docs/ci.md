<!--
See the LICENSE file at the top of the project tree for copyright
and license details.

openi2pd: ci.

Documents project documentation.

-->

# CI policy and workflow linting

Every workflow is **manual-only** (`workflow_dispatch`), including
`.github/workflows/agent-maintenance.yml`, which runs compile-free agent/skill
synchronization checks on demand. No workflow runs automatically; `push`,
`pull_request`, `schedule` and `repository_dispatch` triggers are not allowed.

The Linux build matrix is two workflows, one per OpenSSL branch:
`build-linux-openssl3.yml` and `build-linux-openssl4.yml`. Each installs the
latest Homebrew `openssl@3`/`openssl@4` bottle plus the precompiled GCC, LLVM,
CMake, Ninja, Boost, zlib, MiniUPnPc and ncurses bottles, verifies that the
installed keg reports the same version as the formula, and then builds and
tests with both GCC and Clang. Keep source-built dependencies out of those
branch lanes. The separate `crypto-tsan.yml` diagnostic builds a pinned
OpenSSL revision with TSan to investigate synchronization inside providers;
an uninstrumented bottle cannot supply that evidence. It retains the same
Clang across library and project builds, checks linked paths and uploads
matching images/logs. Run 42422405960 passed the focused diagnostic on
9f5320b2409003cf05df3b1d8968a7a3ebe6949e. This does not validate the full
sanitizer matrix or subsequent local changes. The shared TSan dependency
action and compiler-macro patch are prepared for both the diagnostic and
the full TSan row; ASan/UBSan and the branch bottle lanes remain separate.

The remaining manual lanes are split by concern so a dispatch does not pay
for unrelated jobs:

- `build-linux.yml` runs the component install/consumer check and the pinned
  Java interop-reference validation.
- `build-android.yml` runs the arm64 unit/Lint/APK lane and, opt-in through
  `android_ui`, the API 29 emulator UI/accessibility lane.
- `validation.yml` merges C++ source documentation, the compile-free agent
  skills, opt-in Perl quality, the ASan+UBSan/UBSan/TSan matrix and the
  bounded libFuzzer smoke pass. `check-workflows.pl` requires each job to
  stay in its owning workflow and refuses a reintroduced aggregate
  `build.yml` or `fuzz.yml`.

`validation.yml` always generates C++ API HTML with `docs/Doxyfile.source`;
Android Javadoc runs with the Android lane, which already provides the debug
Java compiler's Android SDK, dependency and generated-class paths, JDK 25 and
package-private APIs. Doxygen checks
documented parameter and reference syntax, and Javadoc runs doclint with
warnings treated as errors. Missing comments are not counted as successful
coverage: the documentation audit remains open until each subsystem is
reviewed. The C++ job also uploads a list of TODO/FIXME/XXX candidates for
human comparison with `TODO.md`; a textual match can include a string literal
and does not establish that a marker is tracked. External specification URLs
still require review. Generated reports are CI artifacts, not installed files.
Source documentation passed in Validation run
[42056754181](https://github.com/daviduhden/openi2pd/actions/runs/42056754181)
at `01960093aeff480d24ed8507878355a643da168f`; that result does not establish
complete documentation coverage or validate later local changes. The
language-specific comment conventions, Doxygen/Javadoc/POD rules and the
review checklist are in [source documentation](source-documentation.md).

## Historical diagnostics and regression patterns

The source inventories preserve observed evidence separately from static
risks and unresolved causes:

- [Compilation errors](ci/COMPILATION-ERRORS.md)
- [Compiler and tool warnings](ci/COMPILATION-WARNINGS.md)
- [Runtime and sanitizer findings](ci/RUNTIME-ERRORS.md)
- [Test and integration failures](ci/TEST-FAILURES.md)
- [Compilation patterns](ci/COMPILATION-PATTERNS.md)
- [Regression patterns](ci/REGRESSION-PATTERNS.md)

Each record identifies the published revision and lane, preserves the
diagnostic, distinguishes cause from hypothesis, and states whether a
correction has static review or CI evidence. Search semantically related
callers and platform branches before closing a finding.

Configure and build evidence must survive a failure before CTest starts.
The OpenSSL composite and sanitizer jobs capture merged stderr/stdout with
pipefail, then always upload the named logs, CMake cache and configure-probe
report. Negative source fixtures independently pin capture, failure
propagation and upload; no missing test binary is interpreted as a clean run.

## Hardening applied

- All third-party actions are pinned to commit SHAs with a version comment.
- Every `actions/checkout` uses `persist-credentials: false`, preventing the
  job token from being left in the workspace for later steps/artifacts.
- Workflow files start with `---` and pass `yamllint`.
- `.yamllint.yml` allows the `on` key used by GitHub Actions and a bounded
  line length for long shell commands.
- Linux CI is a manual Clang/GCC matrix. Test jobs configure with
  `BUILD_TESTING=ON` and `I2PD_BUILD_TUI=ON`, build the complete target graph,
  run the full CTest suite and validate `i2pd --version`; the install job
  deliberately uses `BUILD_TESTING=OFF` and validates components and the
  exported consumer package. The Perl syntax,
  Python-free portability, branding, documentation-link, `i2pdctl`,
  JNI-symbol, native-catalog, install-layout and static
  interoperability-fixture checks run in the same jobs. The Homebrew toolchain action installs the latest Homebrew Perl and
  exposes both `perl` and `prove` ahead of the runner Perl, so CMake's
  `find_package(Perl)` and the shell checks use one interpreter. A
  `ctest -N -L` inventory also verifies that the docs, i2p, security, memory,
  IPC, TUI, torrent, tools, Perl, interop, platform and Android groups are
  registered. `.perlcriticrc` is prepared for an opt-in style pass
  (`-DI2PD_PERL_CRITIC=ON`); Perl::Critic is not a required dependency, and the
CMake target is registered only when `perlcritic` is already installed. A
dispatch-only `perl_style` input installs the distribution
`libperl-critic-perl` package, compiles every maintained script, runs the four
Test::More suites and the critic profile without CPAN. The
Linux lane also runs the optional style pass when the executable is present;
the component-install lane checks every tool binary, `i2pdctl` and
`LICENSE.i2pd-tools`.
- Android CI runs the compile-free Java 25 toolchain guard, Gradle unit tests,
  `lintDebug`, the native arm64 JNI link, the D8/R8 `--min-api 29` probe and an
  arm64 debug APK build. The APK step asserts `lib/arm64-v8a/libi2pd.so`,
  `libc++_shared.so`, exported JNI symbols, `classes.dex` magic, the bundled
  configuration and the exact reseed/family certificate count. The APK uses
  the vcpkg-installed arm64 prefix, c++_shared and the runner's NDK; no
  dependency is downloaded into the source tree. Instrumentation tests remain
  a device/emulator-only manual step.
- The `interop` job checks the current comparison target `i2p-2.14.0` with
  `check-reference.sh`. A separate read-only `i2p-2.13.0` checkout validates
  the historical vectors' source-line provenance using `check-vectors.pl`.
  Both checkouts are temporary and external to the source tree. Neither
  reference verification nor historical fixture checks establish 2.14.0
  interoperability. The job never contacts a live I2P peer or publishes.
- The sanitizer matrix builds ASan+UBSan, UBSan-only and TSan configurations
  separately and runs the full CTest suite serially with 300 s per-test
  budgets. Sanitizer failures are not suppressed.
- A compile-free, independent `skills-static` job (no `needs:` relation to the
  build matrix) runs `tests/check-agent-skills.cmake`,
  `tests/check-language-modernization.cmake`, `tests/check-i18n.cmake`,
  `tests/check-services.cmake` and `tests/check-build-system.cmake`, so Agent
  Skills, language-policy, translation-catalog or service-definition drift
  fails without a compiler or service. Its failure still fails the workflow but does not cancel
  or block parallel build jobs.
- The `Validation` workflow's fuzzing job builds all fifteen libFuzzer targets, runs
  the one-input `ctest -L fuzz` entries, then runs a bounded per-target pass
  (dispatch input `fuzz_seconds`, default 30) from the checked-in corpus. Every executing CTest call uses `-j` where safe, `--no-tests=error`
  and `--output-junit`, and uploads the JUnit/CTest logs as evidence. Fuzzers are not part of the default build; the generated corpora and
  crash reproducers are uploaded as short-retention artifacts. The workflow
  does not claim a long-running corpus campaign.
- A compile-free `workflow-static` check (`tools/scripts/check-workflows.pl`)
  requires `workflow_dispatch` for every workflow, rejects `push`,
  `pull_request`, `schedule`, `repository_dispatch`, `workflow_call` and any
  `continue-on-error`, and rejects any executing `ctest` invocation without
  `--no-tests=error` and `--output-junit`. It runs in `skills-static` and as a
  CTest target.
- The manual `Packaging and service validation` workflow runs the service and
  packaging static contracts, OpenRC/runit shell checks, `ksh -n` for the
  OpenBSD rc.d template and `shellcheck` (no service manager is installed or
  started). A separate `service-install-matrix` job configures each selectable
  manager, installs only the `Service` component into a staging prefix, checks
  the documented files (including `finish`, `log/config`, `timeout-finish` and
  `dependencies.d/base`), asserts no `notification-fd`, and rejects the
  dinit/Upstart samples. It never starts the router or the manager. The
  Debian lane installs `apparmor`, `apparmor-utils` and `desktop-file-utils`,
  validates the profile with `apparmor_parser -Q`, checks the desktop entry,
  the docs/branding/man-page payload, the sysusers/tmpfiles modes and the
  `Breaks`/`Replaces` split, then builds both
  Debian binary packages, runs lintian, verifies the installed unit and
  manifests, preserves a conffile across reinstall, runs `autopkgtest`, and
  checks remove/purge data preservation. The same workflow now builds a
  fully static musl router, packages it as a Debian artifact and builds the
  multi-stage container image. RPM specs and the OpenBSD port remain separate
  pending targets; a source-only service contract never claims package
  validation.
- The Windows workflow has two manual jobs: MSYS2/Clang64 builds the complete
  target graph and runs all registered Windows tests; the native MSVC job
  remains TUI/API-focused, builds the TUI tests and verifies installation.
  Dependencies are build-time only; the installed product downloads nothing.
- Build/test/package and maintenance workflows are all
  `workflow_dispatch`-only; the maintenance gate runs no build or test. The
  static guard rejects every other trigger. Manual runs do not cancel an
  earlier validation (`cancel-in-progress: false`), and CTest logs, package
  outputs, Android reports and fuzz corpora are archived as short-retention
  artifacts.

## Validation commands

```sh
perl -c tools/scripts/check-workflows.pl
perl tools/scripts/check-workflows.pl
zizmor --offline --persona=regular .
zizmor --offline --persona=pedantic .
zizmor --offline --persona=auditor .
yamllint -f parsable .github/workflows .github/actions
actionlint .github/workflows/*.yml
```

Manual dispatch and log collection use the published revision only:

```sh
gh workflow run "Linux OpenSSL 3.x" --ref main
gh workflow run "Linux OpenSSL 4.x" --ref main
gh workflow run "Linux" --ref main
gh workflow run "Android" --ref main
gh workflow run "Validation" --ref main
gh workflow run "Windows" --ref main
gh workflow run "OpenBSD" --ref main
gh run watch <run-id> --compact --exit-status
gh run view <run-id> --log > /tmp/openi2pd-ci.log
```

The workflows are published on `main`; dispatches always run the remote
revision, never the local uncommitted worktree.

At the 2026-11-07 zizmor 1.32.0 checkpoint, regular, pedantic and auditor
modes report no findings. The only auditor-level items are inline,
rule-specific and justified: `self-repository` for the eight local
`./.github/actions/*` calls that actionlint 1.7.12 cannot express with
GitHub's `$/` syntax; `github-env` for the Homebrew composite action, which
writes only a validated formula and trusted prefix paths from manual
workflows; and `misfeature` for the custom `setup-msys2` and
`vmactions/openbsd-vm` shells those actions require. `yamllint` and
`actionlint` are clean. These are static checks; they do not replace a GitHub
Actions run.

## 2026-10-19 dispatch audit

These runs were dispatched with `gh` against published `main@800c8627`; they
do not exercise the local uncommitted tree:

| Run | Revision | Result | Failure observed |
|---|---|---|---|
| `39189489953` Linux and Android | `800c8627` | failure | Linux: `PayloadWriter::U64` in `test-privsep-protocol`; Android: `UnixDaemon.cpp` compiled against `DaemonAndroid` |
| `39189492769` Windows | `800c8627` | failure | MSYS2: `tools-smoke` could not find `routerinfo`; `test-tui-win32-pipe` assertion/DACL. MSVC: `Visual Studio 17 2022` generator not found on `windows-latest` |
| `39189495752` OpenBSD | `800c8627` | failure | `privsep/Worker.cpp` uses undeclared `getexecname()`; OpenBSD VM exited with code 1 |

Logs were downloaded for triage; corrected-revision reruns remain blocked by
the no-commit/no-push constraint. The local fixes for these old-revision
failures are recorded in `TODO.md` and `PROGRESS.md`, not attributed to the
published runs.

## 2026-10-21 dispatch against current `main`

These runs were started with `gh` against the remote `main` revision
`284544a5256043755b197f61e42aee56dbf65032`; they do not include the local
uncommitted tree. Logs were downloaded for the completed jobs.

| Workflow | Run id | Revision | State |
|---|---|---|---|
| Linux and Android | `39688898520` | `284544a5` | failure: `libi2pd/Transports.cpp:693` parse error in Linux GCC/Clang, Android and both sanitizer jobs; interop success |
| Windows | `39688906891` | `284544a5` | failure: both MSYS2 and MSVC failed at the same `Transports.cpp:693` parse error |
| OpenBSD | `39688913964` | `284544a5` | failure: same `Transports.cpp:693` parse error |
| Fuzzing | `39688921868` | `284544a5` | failure: same `Transports.cpp:693` parse error before fuzz execution |
| Packaging and service validation | `39688929467` | `284544a5` | Debian package build failed at `Transports.cpp:693`; service contracts success |

## Pending

- Run Android instrumentation/lifecycle tests on a device or emulator.
- Publish a new revision and dispatch the updated workflows before claiming
  the local CTest/workflow changes validated.
- Keep the action SHAs updated deliberately; do not switch back to mutable
  tags.

## See also

- [testing](testing.md)
- [build system](build-system.md)
- [Developer guide](developer-guide/README.md)

## Targeted Windows heap evidence

The manual Windows workflow accepts `heap_diagnostics=true`. After ordinary
CTest, its MSVC lane can reproduce three affected test images under installed
x64 CDB/GFlags with per-image PageHeap and bounded debugger processes. No SDK
or debugger download is performed; missing tools fail this optional diagnostic
step explicitly. The ordinary CTest result remains authoritative.

The CLANG64 startup diagnostic also retains the router image and its DLL
closure. Its closed CDB command list keeps plain `k` return addresses and
records `@$ra` at MessageBoxA/W entry: `kc` discards the raw addresses needed
to resolve application callers when MinGW symbols are unavailable. Resolve
code PCs against that exact image; a pre-prolog stack can be incomplete.
Arguments, dialog text, locals, memory dumps and child tracing remain outside
the diagnostic boundary. The prepared command change is Pending CI tests;
earlier modal markers do not establish the source cause of Windows startup.

MSVC Release now retains EXE/PDB artifacts through CMP0141/ProgramDatabase
(CMake 3.25+ in this CI lane) and optimized `/DEBUG:FULL /OPT:REF /OPT:ICF`
linking. The project floor remains CMake 3.24. The debugger command allowlist
prints exception events and code locations only, with no memory dumps,
argument or local-variable commands. The maintenance regression rejects such
commands. PageHeap is disabled in `finally`; evidence remains pending until a
published revision actually runs this option.

Procedures follow [Microsoft's PageHeap documentation](https://learn.microsoft.com/en-us/windows-hardware/drivers/debugger/gflags-and-pageheap),
[CDB command-line reference](https://learn.microsoft.com/en-us/windows-hardware/drivers/debugger/cdb-command-line-options)
and [CMake's symbol-format reference](https://cmake.org/cmake/help/latest/variable/CMAKE_MSVC_DEBUG_INFORMATION_FORMAT.html).

### Exact sanitizer reproduction artifacts

The manual sanitizer lane retains the EdDSA, tunnel stop/targets, ratchet and
RunnableService/I2CP lifecycle executables with its logs. A manifest records
the tested Git revision and SHA-256 of each available image; an explicit list
identifies images not produced by a failed build. Use those exact images for
raw-PC symbolization, never a locally rebuilt replacement. No memory dump or
credential-bearing runtime snapshot is collected. This artifact extension is
prepared statically and awaits a published manual run.

The agent-maintenance gate now uses the system Perl interpreter, the shared
Test::More validator contract and `--scope range` with the PR base/head SHAs.
There is no setup-python action. The manual Validation skills lane runs the
same contract plus a worktree structural scan; CMake also registers
`perl-agent-knowledge-contract` when Perl is available. Syntax and optional
Critic inventories include the validator's modules. A clean checkout scan
alone does not exercise changed-file gating; the black-box fixtures do that.
The Perl migration remains Pending CI tests until a published revision runs.

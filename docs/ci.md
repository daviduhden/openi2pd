<!--
See the LICENSE file at the top of the project tree for copyright
and license details.

openi2pd: ci.

Documents project documentation.

-->

# CI policy and workflow linting

Workflows remain **manual-only** (`workflow_dispatch`); no `push`,
`pull_request`, schedule or automatic trigger is allowed.

## Hardening applied

- All third-party actions are pinned to commit SHAs with a version comment.
- Every `actions/checkout` uses `persist-credentials: false`, preventing the
  job token from being left in the workspace for later steps/artifacts.
- Workflow files start with `---` and pass `yamllint`.
- `.yamllint.yml` allows the `on` key used by GitHub Actions and a bounded
  line length for long shell commands.
- Linux CI is a manual Clang/GCC matrix. Each job configures with
  `BUILD_TESTING=ON` and `I2PD_BUILD_TUI=ON`, builds the complete target graph,
  runs the full CTest suite and validates `i2pd --version`. The Perl syntax,
  Python-free portability, branding, documentation-link, `i2pdctl`,
  install-layout and static interoperability-fixture checks run in the same
  jobs. The Homebrew toolchain action installs the latest Homebrew Perl and
  exposes both `perl` and `prove` ahead of the runner Perl, so CMake's
  `find_package(Perl)` and the shell checks use one interpreter. A
  `ctest -N -L` inventory also verifies that the docs, i2p, security, memory,
  IPC, TUI, torrent, tools, Perl, interop, platform and Android groups are
  registered. `.perlcriticrc` is prepared for an opt-in style pass
  (`-DI2PD_PERL_CRITIC=ON`); Perl::Critic is not a required dependency, and the
CMake target is registered only when `perlcritic` is already installed. The
Linux lane also runs the optional style pass when the executable is present;
the component-install lane checks every tool binary, `i2pdctl` and
`LICENSE.i2pd-tools`.
- Android CI runs the compile-free Java 25 toolchain guard, Gradle unit tests,
  `lintDebug`, the native arm64 JNI link and an arm64 debug APK build. The
  APK step uses the vcpkg-installed Android prefix and the runner's NDK; no
  dependency is downloaded into the source tree. Instrumentation tests remain
  a device/emulator-only manual step.
- The `interop` job checks the current comparison target `i2p-2.14.0` with
  `check-reference.sh`. A separate read-only `i2p-2.13.0` checkout validates
  the historical vectors' source-line provenance using `check-vectors.pl`.
  Both checkouts are temporary and external to the source tree. Neither
  reference verification nor historical fixture checks establish 2.14.0
  interoperability. The job never contacts a live I2P peer or publishes.
- The sanitizer matrix builds ASan+UBSan and TSan configurations separately and
  runs the full CTest suite. Sanitizer failures are not suppressed.
- A compile-free, independent `skills-static` job (no `needs:` relation to the
  build matrix) runs `tests/check-agent-skills.cmake`,
  `tests/check-language-modernization.cmake` and `tests/check-i18n.cmake`, so
  Agent Skills, language-policy or translation-catalog drift fails without a
  compiler or service. Its failure still fails the workflow but does not cancel
  or block parallel build jobs.
- The separate `Fuzzing` workflow builds all eight libFuzzer targets, runs the
  one-input `ctest -L fuzz` entries, then runs a bounded 30-second pass per
  target. Fuzzers are not part of the default build; the generated corpora and
  crash reproducers are uploaded as short-retention artifacts. The workflow
  does not claim a long-running corpus campaign.
- The manual `Packaging and service validation` workflow runs the service and
  packaging static contracts, OpenRC/runit shell checks, then builds both
  Debian binary packages, runs lintian, verifies the installed unit and
  manifests, preserves a conffile across reinstall, runs `autopkgtest`, and
  checks remove/purge data preservation. RPM/static bundles and OpenBSD port
  builds remain separate pending targets; a source-only service contract
  never claims package validation.
- The Windows workflow has two manual jobs: MSYS2/Clang64 builds the complete
  target graph and runs all registered Windows tests; the native MSVC job
  remains TUI/API-focused, builds the TUI tests and verifies installation.
  Dependencies are build-time only; the installed product downloads nothing.
- Every workflow is `workflow_dispatch`-only; no push, pull-request, schedule,
  `workflow_call` or automatic trigger exists. Manual runs do not cancel an
  earlier validation (`cancel-in-progress: false`), and CTest logs, package
  outputs, Android reports and fuzz corpora are archived as short-retention
  artifacts.

## Validation commands

```sh
zizmor --format plain .github/workflows
yamllint -f parsable .github/workflows
actionlint .github/workflows/*.yml
```

Manual dispatch and log collection use the published revision only:

```sh
gh workflow run "Linux and Android" --ref main
gh workflow run "Windows" --ref main
gh workflow run "OpenBSD" --ref main
gh run watch <run-id> --compact --exit-status
gh run view <run-id> --log > /tmp/openi2pd-ci.log
```

The workflows are published on `main`; dispatches always run the remote
revision, never the local uncommitted worktree.

At the 2026-10-19 static checkpoint after adding the label inventory,
platform-specific test registration and fuzz CTest entries: zizmor 1.31.1
reported no findings (10 suppressed baseline items), yamllint was clean and
actionlint reported no errors. After the 2026-10-21 workflow/artifact edits,
`actionlint` and `yamllint` are clean; zizmor reports five low-confidence
self-repository `./` local-action hints and no medium/high findings. These are
static checks; they do not replace a GitHub Actions run.

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

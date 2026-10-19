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
  jobs. A `ctest -N -L` inventory also verifies that the i2p, security, memory,
  IPC, TUI, torrent, tools, Perl, interop, platform and Android groups are
  registered. `.perlcriticrc` is prepared for an opt-in style pass
  (`-DI2PD_PERL_CRITIC=ON`); Perl::Critic is not a required dependency.
- Android CI runs the compile-free Java 25 toolchain guard, Gradle unit tests,
  `lintDebug`, the native arm64 JNI link and an arm64 debug APK build. The
  APK step uses the vcpkg-installed Android prefix and the runner's NDK; no
  dependency is downloaded into the source tree. Instrumentation tests remain
  a device/emulator-only manual step.
- The `interop` job clones the pinned `i2p-2.13.0` reference into a temporary
  read-only directory and validates `check-reference.sh` plus checked-in
  vectors/provenance. It never contacts a live I2P peer and never publishes.
- The sanitizer matrix builds ASan+UBSan and TSan configurations separately and
  runs the full CTest suite. Sanitizer failures are not suppressed.
- The separate `Fuzzing` workflow builds all eight libFuzzer targets and runs a
  bounded smoke pass for each. Fuzzers are not part of the default build;
  `-DWITH_FUZZERS=ON` also registers one-input `ctest -L fuzz` smoke entries
  for local/CI inventory, while the workflow performs the longer corpus run.
- The Windows workflow has two manual jobs: MSYS2/Clang64 builds the complete
  target graph and runs all registered Windows tests; the native MSVC job
  remains TUI/API-focused, builds the TUI tests and verifies installation.
  Dependencies are build-time only; the installed product downloads nothing.
- Every workflow is `workflow_dispatch`-only; no push, pull-request, schedule,
  `workflow_call` or automatic trigger exists.

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

The `Fuzzing` workflow cannot be dispatched until that workflow file is
published. Local changes are never treated as the source of a remote run.

At the 2026-10-19 static checkpoint after adding the label inventory,
platform-specific test registration and fuzz CTest entries: zizmor 1.31.1
reported no findings (10 suppressed baseline items), yamllint was clean and
actionlint reported no errors. These are static checks; they do not replace a
GitHub Actions run.

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

## Pending

- Run the expanded Linux/Android, Windows, OpenBSD, interop, sanitizer and
  fuzzing workflows on a published revision and triage every failure. Local
  uncommitted corrections cannot be attributed to those runs.
- The local `Fuzzing` workflow is not present on published `main`; it cannot be
  dispatched with `gh` until the workflow revision is published. The same
  applies to the ASan/UBSan/TSan jobs and expanded CTest matrix added locally.
- Run Android instrumentation/lifecycle tests on a device or emulator.
- Keep the action SHAs updated deliberately; do not switch back to mutable
  tags.

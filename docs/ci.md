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
- Linux CI runs `perl -c tools/scripts/i2pdctl` and
  `prove -v tools/tests/i2pdctl.t`.
- No new action, package download or automatic trigger was introduced.
- The Windows workflow has two manual jobs: MSYS2/Clang64 for the full
  dependency set and a native MSVC job that uses the runner's vcpkg to obtain
  build dependencies. The MSVC job builds `i2pd`, `i2pd-tui`,
  `test-tui-protocol`, `test-tui-terminal`, `test-tui-pipe-name` and
  `test-tui-win32-pipe`, runs those four tests and verifies that
  `i2pd-tui.exe` installs. Dependencies are build-time only; the installed
  product downloads nothing.
- No workflow was triggered for the Windows TUI integration. Both jobs remain
  `workflow_dispatch`-only.

## Validation commands

```sh
zizmor --format plain .github/workflows
yamllint -f parsable .github/workflows
actionlint .github/workflows/*.yml
```

At the last static checkpoint: zizmor 1.31.1 reported no findings
(5 suppressed baseline items), yamllint 1.38.0 was clean and actionlint
reported no errors. These are static
checks; they do not replace a GitHub Actions run.

## Pending

- Add Android app/JNI/Lint and full CTest matrix execution on CI runners with
  the required SDK/NDK/dependency prefixes.
- Keep the action SHAs updated deliberately; do not switch back to mutable
  tags.

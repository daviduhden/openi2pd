<!--
See the LICENSE file at the top of the project tree for copyright
and license details.

openi2pd CI job and triage matrix.
-->
# CI job and triage matrix

Use with `SKILL.md`. All jobs are manual; no result here validates a revision.

## Workflows

| Workflow | Jobs | Main coverage |
|---|---|---|
| `build.yml` | Linux Clang/GCC, Android, interop, sanitizers | full graph/CTest; Java 25/JNI/APK; pinned Java reference; ASan+UBSan/TSan |
| `build-openbsd.yml` | OpenBSD | LibreSSL build, full CTest, platform tests |
| `build-windows.yml` | MSYS2/Clang64, MSVC | full Windows graph/CTest; TUI/API/install |
| `fuzz.yml` | fuzzing | eight libFuzzer targets + bounded smoke |
| `packaging.yml` | Debian/static/service checks | packages, lintian/autopkgtest, service validation |

All use `workflow_dispatch` only. Actions are SHA-pinned and checkout
credentials are not persisted.

## CTest labels

`unit`, `i2p`, `security`, `memory`, `ipc`, `tui`, `torrent`, `tools`,
`perl`, `interop`, `platform`, `android`, `fuzz`, `ci`, `static`, `smoke`,
`services`, `reference`, `install`, `integration`; `critic` is optional.

The Linux label gate enumerates `i2p security memory ipc tui torrent tools perl
interop platform android ci static smoke`; add any new gate-relevant label to
that explicit loop. Some local/static labels (`unit`, `reference`, `install`,
`integration`, `services`, `critic`) are not currently enumerated there.

Platform-only tests register only on their host:
`test-privsep-openbsd-*`, `test-torrent-storage-openbsd`,
`test-privsep-linux-*`, `test-privsep-windows-ipc`, `test-tui-win32-pipe`.

## Before dispatch

- Workflow is present on the published revision.
- The tree is committed/pushed only if the coordinator explicitly authorized
  it; local changes are not remotely tested.
- Required tools/dependencies are pinned and available.
- Trigger is still manual; no `continue-on-error`; no suppressed failure.
- Expected test labels/jobs are documented.

## Triage sequence

1. Record workflow, job, revision, run id and command.
2. Find the first failing step, not the last cascade.
3. Classify: toolchain/config, compile, link, test assertion, timeout,
   dependency download, environment, or packaging/device setup.
4. Compare with the current tree; do not attribute an old-revision failure to
   local changes.
5. Fix the cause in the owning subsystem; add a regression or static check.
6. Re-run only after explicit authorization.
7. Record result and evidence; update status only when the relevant job passes.

## Prohibited

- Local compile/CTest/sanitizer/fuzz/Gradle execution.
- Dispatched workflows without explicit authorization.
- Hidden downloads, mutable action tags, persisted checkout credentials,
  cleartext secrets or full log dumps in tracked files.
- Claiming “CI green” from a subset of jobs or an older revision.

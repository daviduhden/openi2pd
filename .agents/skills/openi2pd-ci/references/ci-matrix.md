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
| `build.yml` | skills-static, perl-quality (opt-in), Linux Clang/GCC, Linux OpenSSL 3.x, pinned OpenSSL, install-components, Android (arm64), android-ui (opt-in), interop, sanitizers | compile-free skills/workflow/service/packaging/legal/upstream checks; optional Perl::Critic; full graph/CTest; component staging/consumer; Java 25/JNI/APK; pinned Java reference; ASan+UBSan, UBSan-only and TSan |
| `build-openbsd.yml` | OpenBSD | LibreSSL build, full CTest, platform tests |
| `build-windows.yml` | MSYS2/Clang64, MSVC | full Windows graph/CTest; TUI/API/install |
| `fuzz.yml` | fuzzing | fifteen libFuzzer targets + bounded smoke; `--no-tests=error`/JUnit smoke |
| `packaging.yml` | service-contracts, service-install-matrix, static-musl, container-image, Debian | static contracts; seven-manager Service staging; static Debian/RPM-style package; container image; Debian lintian/autopkgtest, conffile reinstall, remove/purge |

All use `workflow_dispatch` only. Actions are SHA-pinned and checkout
credentials are not persisted.

The interop job verifies the current Java 2.14.0 reference separately from
historical 2.13.0 fixture provenance. Use `I2P_JAVA_REFERENCE` for the current
target checker and `I2P_JAVA_FIXTURE_REFERENCE` for the existing corpus
checker. The corresponding CMake options are `I2PD_INTEROP_REFERENCE` and
`I2PD_INTEROP_FIXTURE_REFERENCE`. A reference pin check is not a source audit
or a dynamic compatibility result.

## CTest labels

`unit`, `i2p`, `security`, `memory`, `ipc`, `tui`, `torrent`, `tools`,
`docs`, `perl`, `interop`, `platform`, `android`, `fuzz`, `ci`, `static`,
`smoke`, `packaging`, `install`, `services`, `reference`, `cli`,
`integration`, `binary`, `workflow`; `critic` is optional.

The Linux label gate enumerates `i2p security memory ipc tui torrent tools perl
docs interop platform android ci static packaging services unit install smoke
cli integration`.
`fuzz`, `reference` and `critic` remain outside the normal-build gate. Add a
new gate-relevant label to that explicit loop; `agent-skills-static` is part of
the `ci;static` gate.

Platform-only tests register only on their host:
`test-privsep-openbsd-*`, `test-torrent-storage-openbsd`,
`test-privsep-linux-*`, `test-privsep-windows-ipc`, `test-tui-win32-pipe`.

## Before dispatch

The manual Linux job also explicitly selects `test-i2ptunnel-http`,
`test-addressbook-case` and `test-torrents-security` for the 2026-10-21
client/persistence continuation. These remain in the full CTest and sanitizer
suites; `fuzz-http-parser` also feeds the production HTTP tunnel transformer
and compares whole-buffer versus fragmented delivery. Registration does not
mean these checks have run.

- Workflow is present on the published revision.
- The tree is committed/pushed only if the coordinator explicitly authorized
  it; local changes are not remotely tested.
- Required tools/dependencies are pinned and available.
- Trigger is still manual; no `continue-on-error`; no suppressed failure.
- Artifacts are staged inside the workspace and uploaded with short retention.
- A manual dispatch targets the published revision; never attribute it to the
  local uncommitted worktree.
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

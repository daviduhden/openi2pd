---
name: openi2pd-ci
description: Use when changing openi2pd manual GitHub Actions workflows, CTest label integration, sanitizer or fuzzer jobs, or triaging CI logs with GitHub CLI after explicit authorization.
---

## Maintenance & Continuous Learning

When this skill guides a repository change, review it after implementation. Update procedures, tools, configurations, assumptions, examples and validation steps when they changed or when reusable knowledge improves future work; correct inaccurate guidance and omit one-off execution details. Report synchronization in the task summary. Follow the normative process and Definition of Done in [AGENTS.md](../../../AGENTS.md#continuous-agent--skills-maintenance).
<!--
See the LICENSE file at the top of the project tree for copyright
and license details.
-->

# openi2pd manual CI

## Activate when

Use this skill for `.github/workflows/**`, CTest label/workflow integration,
CI-only checks, sanitizer/fuzzer configuration or `gh` triage. Dynamic
execution is always manual and requires explicit authorization from the
coordinator.

Read [`../../../AGENTS.md`](../../../AGENTS.md) first. The static-only policy
applies: an implementation agent may edit workflows and prepare tests, but may
not dispatch, compile, run CTest, invoke sanitizers/fuzzers or call
`gh workflow run` locally or remotely without explicit authorization.

## Inspect first

- `.github/workflows/build-linux.yml`, `build-android.yml`,
  `validation.yml`, `build-linux-openssl3.yml`, `build-linux-openssl4.yml`,
  `build-openbsd.yml`, `build-windows.yml`, `packaging.yml`; the local
  composite actions under `.github/actions/`.
- `tests/CMakeLists.txt` as a read-only input for registration/labels;
  `openi2pd-build` owns CMake-side edits. Also inspect `tests/fuzz/`,
  `tests/check-*.cmake`, `.agents/skills/README.md`, `tools/*test*.cmake`,
  `tools/tests/` and `android/tools/`.
- [`../../../docs/ci.md`](../../../docs/ci.md),
  [`../../../docs/testing.md`](../../../docs/testing.md) and
  [`../../../docs/build-system.md`](../../../docs/build-system.md).
- Current `CI-*`/`GOV-*` rows in `TODO.md` and the last `PROGRESS.md` entry.

## Reference sources

- [`../../../docs/ci.md`](../../../docs/ci.md) — manual-only policy, hardening,
  validation commands, dispatch audit and pending work.
- [`../../../docs/testing.md`](../../../docs/testing.md) — test labels, manual
  matrix, sanitizer/fuzz and evidence rules.
- [`references/ci-matrix.md`](references/ci-matrix.md) — jobs, labels and triage
  checklist.

## Reconnaissance

Launch at least one read-only `explore` subagent before editing, split across
independent areas when useful. If subagents are unavailable, report that
limitation; do not substitute local execution. Ask for:

1. every workflow trigger, permission, action pin and checkout credential;
2. the configure/build/test commands and platform/toolchain assumptions;
3. CTest label inventory and platform-only registration;
4. packaging/service/Android steps and their dependencies;
5. existing run IDs/logs and whether they predate the current revision.

Ask for line references and exact job names. Do not let them dispatch a
workflow, run `gh`, build or test.

## Parallel implementation

- The coordinator owns workflow files. CMake-side test registration/labels
  belong to `openi2pd-build`; CI owns workflow job wiring and verifying the
  registered label inventory.
- Split Android, packaging and platform jobs only with explicit file
  ownership. Do not edit the same workflow from two workstreams.
- Keep a fix to one failure class per change; do not hide multiple failures in
  one broad rewrite.
- Build/packaging/Android owners provide the commands; CI owns the job wiring.

## Conventions

- Source-documentation generation and checks follow the
  [source documentation policy](../../../AGENTS.md#source-documentation-policy)
  and run in manual CI; report generator coverage and unresolved warnings.

- Build, test and packaging workflows remain `workflow_dispatch`-only. The
  compile-free `agent-maintenance.yml` PR gate is the sole pull-request
  exception; it runs the compile-free Perl validator contract and structural
  checks, never native/JVM builds or product tests. `check-workflows.pl` enforces this
  exact exception, so any additional automatic trigger needs an explicit
  policy and checker update.
- Pin third-party actions to commit SHAs with a version comment; set
  `persist-credentials: false` on checkout.
- Do not add `continue-on-error`, ignore a failing test or suppress warnings to
  make a job green.
- Do not attribute a run to a revision it did not build. Local uncommitted
  changes are not validated by an earlier remote run.
- For every compiler or linker warning, inspect successful and failed job
  logs, trace inlining/template output to the owning source or dependency, and
  search semantically equivalent code across targets and platform branches.
  Keep first-party, dependency, toolchain and runner messages distinct in the
  compilation warning inventory; preserve corrected historical findings.
- A warning fix is pending until the same supported toolchain lane runs on a
  revision containing the change. A job stopped before a target cannot certify
  that target as warning-free.
- Keep jobs reproducible: pinned toolchains/dependencies where practical, no
  hidden downloads into the source tree, bounded timeouts.
- Add negative/failure-path tests, not only smoke checks.
- Keep `Fuzzing` and sanitizer jobs manual and separate; do not fold them into a
  push-triggered job.
- Keep `agent-skills-static` (frontmatter, required sections, links, reference
  closure and policy pointers), `language-modernization-static` (AGENTS.md
  policy, C++23/Java 25 levels, availability matrix and seam inventory) and
  `i18n-static` (native/Android catalog wiring) in the compile-free
  `ci;docs;static` gate; do not execute a skill or compile to validate them.
- The maintenance PR checker compares the base/head commit range and rejects
  repository changes without a substantive instruction update. It validates
  structure and local paths, not semantic relevance; the final review must
  record the reason the knowledge update is reusable.

## Authorization and triage

- Do not run `gh workflow run`, `gh run watch` or any remote action without
  explicit coordinator authorization.
- When authorized, dispatch only the published revision and record workflow
  name, ref, run id and revision.
- Download logs to a private temporary path; classify failures by job/step and
  first error. Do not paste secrets or full logs into tracking files.
- For each relevant run, capture the full SHA and job IDs with `gh run view
  RUN_ID --json headSha,headBranch,createdAt,conclusion,jobs,url`; use
  `gh run view RUN_ID --job JOB_ID --log` for one job and
  `gh run download RUN_ID -D /tmp/openi2pd-ci/RUN_ID` for artifacts. Compare a
  repeated diagnostic with earlier SHAs, and inspect install/package artifacts
  before changing CMake component assignments or Debian file expectations.
  Keep generated-output assertions tied to discovered artifacts rather than
  assuming a build-tool output directory.
- Fix causes, not symptoms. Re-run after the correction only when authorized.
- Use `gh` for reading runs/logs only after authorization; never for commits,
  releases or secrets.
- A CTest job that reaches the end can still report sanitizer defects in
  failed tests. Read every named failure and its adjacent stack before
  classifying the suite; keep assertion failures, ASan/UBSan/TSan findings,
  test-fixture lifetime bugs, and runner/tool warnings as separate records.
  The 2026-11-02 SHA `56f17c0f` run demonstrated this: ASan/TSan identified
  borrowed-view test inputs, while other sanitizer cases remained unresolved.
- When a warning repeats once per test executable, check whether a shared test
  support translation unit is linked into every target before filing duplicate
  findings. MSVC's C4996 on the test logger showed that fixing the shared
  environment lookup once removes a matrix-wide first-party warning; keep
  intentional assertion-enabling D9025 separate from source warnings.
- Read successful packaging and consumer logs for Lintian, CMake policy,
  action-runtime and runner-capability warnings. A successful package job can
  still expose package-quality debt, and Landlock ABI below the requested
  policy is a coverage limitation rather than proof of full sandboxing.

## Known issues and decisions

- The pinned OpenBSD action's run input throws before final copyback on
  failure. Prepare its rsync VM, then run builds in the named OpenBSD
  custom shell: that wrapper pulls artifacts after a failed command and
  returns the original status. Keep action setup, custom-shell execution
  and always-run host upload as separate source-guard boundaries. Do not
  replace failure propagation with a successful VM exit or retry.

- A manual heap-diagnostic run can be cancelled before its target exists.
  Inspect the build step and exact artifact before interpreting a later
  missing-binary helper failure. Preserve cancellation as an evidence gap;
  never infer a bad output path, a clean test or an allocator cause from it.

- Preserve configure and build streams separately from CTest artifacts.
  An early native compile error can leave no Testing directory: the Linux
  OpenSSL composite retains configure.log, build.log and CMake configure
  evidence under its always-run upload. Keep stderr merged and pipefail
  explicit when tee captures diagnostics; never turn a failed build green.
  The two manual OpenSSL workflows expose warnings_as_errors (default false)
  through the shared action's validated ON/OFF input. Keep it opt-in until
  the selected compiler lane is clean; do not infer cleanliness from the
  existence of the switch or from a build stopped at an earlier error.
  The workflow checker and negative fixtures pin the complete input,
  forwarding, environment and CMake option chain so a visible UI switch
  cannot silently stop reaching the compiler policy.
- Successful compile-free steps can emit CMake developer warnings before a
  later step fails. Script-mode checks do not inherit project policies:
  inspect recursive inventories for CMP0009 and quoted discriminator names
  for CMP0054. Select NEW behavior explicitly, rather than suppressing dev
  warnings, and review symlink traversal and literal comparisons together.
- Interpreter metadata queries and file-based syntax checks do not need
  embedded programs. The maintenance gate uses the native version banner;
  its suite and validator still enforce the Perl floor. Source scans cover
  workflow run blocks and composite actions as well as standalone Shell.
  Register the shell-policy check separately from its negative Test::More
  fixtures so structural inspection never launches tests or package builds.

- Agent-maintenance uses the repository Perl validator and Test::More suite,
  not setup-python or an interpreter bootstrap. Keep the same suite in the
  manual validation lane; syntax/critic inventories include `scripts/lib/`
  modules and the contract fixture copies that complete tree. A clean
  checkout's worktree scan checks structure, while fixture/range cases cover
  change gating. Do not mistake the former for evidence of the latter.

- Raw sanitizer PCs cannot be attributed using a later rebuild. Preserve the
  exact failing executable with the run SHA and its SHA-256 digest. The
  sanitizer artifact includes the EdDSA, tunnel stop/targets, ratchet and
  service/I2CP lifecycle reproducers; missing binaries are listed explicitly
  when a build stopped early. These are code artifacts, not memory dumps or
  credentials. Keep this inventory in the workflow/checker pair synchronized
  when a new unsymbolized reproducer is investigated.

- Windows `heap_diagnostics` is an opt-in reproduction after ordinary
  CTest, using installed x64 CDB/GFlags only. Missing SDK debuggers are
  infrastructure limitations, not a clean heap result. PageHeap is per
  allowlisted test image and disabled in `finally`; each debugger process
  has a deadline. Preserve Release EXE/PDB and code-only traces in the
  workspace artifact. Do not collect process memory, arguments or locals;
  `check-workflows.pl` pins the command-file allowlist. A diagnostic rerun
  never replaces the ordinary CTest result or resolves an unsymbolized
  historical report by itself.
- The branch verifier must not call plain `brew`: `brew-toolchain` evaluates
  `brew shellenv` only in its own shell. Resolve
  `/home/linuxbrew/.linuxbrew/bin/brew` with a PATH fallback and fail
  explicitly; a suppressed stderr turns a PATH miss into a false "formula
  not installed" and skips the whole build (CI-003).
- The `4e776a39` batch leaves Linux/OpenBSD/sanitizers at CE-069, MSYS2 at
  tools-regressions/tunnel-endpoint/i2pd-version, and MSVC at intermittent
  access violations (the same-SHA rerun passed). A preventive audit then added
  RT-013..RT-021; none has CI evidence yet.
- Manual CI is split by concern: `build-linux.yml` (install/consumer and
  interop), `build-android.yml` (arm64 and opt-in API 29 UI), and
  `validation.yml` (Doxygen, skills-static, opt-in Perl quality, sanitizers,
  fuzzing). The Linux compiler/CTest matrix stays in
  `build-linux-openssl3.yml`/`build-linux-openssl4.yml`, each passing
  `I2PD_OPENSSL_EXPECT_MAJOR`. `check-workflows.pl` requires each job to stay
  in its owning workflow, refuses a reintroduced aggregate `build.yml`/
  `fuzz.yml`, scans composite actions and rejects source-built OpenSSL.
- Latest batch `4e776a39`: Fuzzing/Packaging pass; Linux/OpenBSD stop at
  CE-069; Windows MSYS2 leaves tools-regressions (Win32 `nul.*` fixture),
  tunnel-endpoint count and i2pd-version; MSVC leaves TUI/privsep access
  violations. The Windows workflow now prints subsystem/imports/ldd and a
  pre-CTest version probe before CTest.
- Linux builds are split into `build-linux-openssl3.yml` and
  `build-linux-openssl4.yml` (GCC/Clang matrix), sharing
  `.github/actions/linux-openssl-build`. They install the latest Homebrew
  bottle via `brew-toolchain`'s `openssl` input, verify formula == binary and
  the branch, and must not source-build OpenSSL; `check-workflows.pl` pins
  both files and rejects the old pinned-download markers.
- The per-test stderr echo (`tests/log-support.cpp`) is what named the
  SSU2 `pq` writer and ELS2 fixture defects. Keep it; an aborting assert
  otherwise leaves only the signal.
- `doxygen-static` now also rejects duplicate `@param` from a member's
  declaration plus definition; the source-docs lane aborts on the first such
  warning.
- `doxygen-static` (`tools/scripts/check-doxygen-params.pl`) runs in the
  static Perl block and the workflow Perl step. It catches the recurring
  partial-`@param` source-docs failure without invoking Doxygen; the manual
  `source_docs` lane remains the only dynamic evidence.
- A failed `assert()` aborts without unwinding, so a ring-based log dump at
  the end of a test is never reached. Every `i2pd_add_test()` target therefore
  links `tests/log-support.cpp`, which installs a synchronous stderr echo on
  the product logger (`SetSynchronousSink`) before `main`. Keep the sink
  non-throwing and lock-free with respect to logger mutexes, and keep fuzz
  targets out of it. `test-log-diagnostic-sink` pins the contract and
  `tests/check-build-system.cmake` pins the linkage; the level is selected by
  `OPENI2PD_TEST_LOG_LEVEL` (default `info`).

- The first batch that actually executed the new suites is more informative
  than any earlier green build: see `docs/ci/TEST-FAILURES.md` TF-001..TF-035.
  A lane that stopped at compile time proves nothing about its CTest targets.
- A Windows GUI-subsystem binary launched by headless CTest can block with no
  output. Use `tests/check-binary-version.cmake` (bounded wait, captured
  output, exit status authoritative) instead of `add_test(... i2pd --version)`.
- Child-output capture on Windows must merge stderr into one stream; Win32
  `select()` accepts sockets only. Repository-relative path comparisons need
  one `/`-normalising helper (case-fold on Windows), and `.gitattributes` must
  pin LF or a runner's `core.autocrlf` turns the catalogs into CRLF.
- An existence check inside a 0700 service-owned directory needs `sudo`;
  permission denied is not "file deleted".

- Retained run logs may outlive neither the run metadata nor its artifacts.
  Record inaccessible job IDs and artifact retention explicitly; missing
  evidence does not establish a clean target. The current multi-run coverage
  and missing-log IDs are summarized in `docs/ci/COMPILATION-WARNINGS.md`.
- For package Lintian failures in generated maintainer scripts, download the
  exact-run package artifact and inspect its `control.tar.*` script before
  changing source hooks. The regular and static Debian package trees have
  distinct postrm files; `dh_installinit` can add its own `update-rc.d` purge
  call at `#DEBHELPER#` expansion.
- A `set -e` workflow step with several silent `test` assertions can report
  only exit 1, not which lifecycle condition failed. Label destructive package
  phases and emit a specific failure message for each preservation/removal
  assertion before changing hooks based on ambiguous logs.

- The manual packaging service-contract job ShellChecks maintenance scripts
  as well as init scripts. Its fake-`nc` SAM regression checks mismatch exit
  status without a live bridge; it establishes no live SAM compatibility.
- Ubuntu does not publish an `openrc` binary package, so the static service
  job installs `shellcheck` and `ksh`; it validates OpenRC/runit files with
  `sh -n`, the OpenBSD rc.d template with `ksh -n` and ShellCheck. Never start
  a service manager in that job.


- CONC-010 uses immutable process-local profile-root authority and flushed
  same-directory replacement; storage IO/cleanup is serialized and shutdown
  drains postponed updates after producers stop. Cross-process ownership,
  abandoned staging cleanup and retained external mutators remain open.
  `test-router-profile-storage` is prepared, not executed.


- The Linux focused profile/Android-lifecycle/firewall step runs the same
  registered host targets as full CTest. The full sanitizer matrix includes
  profile concurrency. Host Android transition results do not substitute for
  JNI linking or actual device startup/restart evidence.
- The installed-consumer lane links `i2pd::libi2pdclient` and calls compiled
  Boost.JSON/URL APIs; merely including a core header misses dependency
  closure failures. MSYS2 also enables `WITH_HARDENING` to exercise PE flags.
- Manual dispatch must target the published revision. Remote results never
  validate the local uncommitted worktree; record revision, run ID and outcome.
- `skills-static` runs the Agent Skills and language-modernization checks
  without a compiler, has no `needs:` relationship to builds, and manual runs
  use `cancel-in-progress: false` so older evidence is preserved.
- `language-modernization-static` pins literal policy phrases and seam names;
  a wording or file move can fail the job even when code is unchanged. Update
  the checker and the owning skill/docs together.
- No `continue-on-error`, deleted tests or suppressed failures. Dynamic
  validation remains pending until the relevant manual job passes.

- Every executing CTest step uses `-j` (2 for release/platform, 1 for
  sanitizers), `--no-tests=error` and `--output-junit`; JUnit/`Testing` logs
  are uploaded. `tools/scripts/check-workflows.pl` (CTest `workflow-static` and the
  `skills-static` job) rejects other triggers, `continue-on-error` and any ctest
  line without those flags.
- Sanitizer diagnostics with raw PCs need a symbolizer and the exact test
  executable with debug information. The validation lane sets
  `ASAN_SYMBOLIZER_PATH` when `llvm-symbolizer` exists and uploads the EdDSA
  reproducer binary alongside XML/CTest logs; if symbolization is unavailable,
  preserve that limitation and do not guess a source-level cause.
- Linux and sanitizer test builds pass `-DI2PD_REQUIRE_PERL=ON`; a missing
  interpreter must unregister tests loudly, not silently turn a lane green.
  OpenBSD base Perl satisfies the same requirement.
- The fuzzer inventory is fifteen targets across `tests/fuzz/*.cpp`,
  `tests/CMakeLists.txt`, `validation.yml` and `docs/testing.md`; the doc list,
  checked-in corpus, per-target cap and dual build/run registration are
  enforced by `check-build-system.cmake`. The sanitizer matrix is ASan+UBSan,
  UBSan-only and TSan; the fuzz job runs from `tests/fuzz/corpus/<target>/`
  with a per-target log and artifact directory.
- SIGPIPE under `pipefail` can turn a matching `grep -q` pipeline into status
  141 (false pass) or a failing check (false fail). Capture `javap`,
  `strings`, `file` or `nm` output to a temporary file before grepping.
- Build/test/package workflows are `workflow_dispatch`-only; the compile-free
  agent-maintenance gate is the sole pull-request workflow. `workflow_call` is
  allowed only for a real reusable consumer. Manual results never validate an
  unpublished local tree.


- Start a compile-failure triage with the real manual runs:
  `gh run list`, `gh run view RUN_ID`, `gh run view RUN_ID --log-failed`,
  `gh run view --job JOB_ID --log`. Record the run ID, SHA and first
  diagnostic in `docs/ci/COMPILATION-ERRORS.md`; do not count cancelled jobs or
  cascaded messages as independent causes.
- The source-docs lane runs Doxygen with warnings as errors and needs
  `mkdir -p docs/generated/source-api` first. The service-contract job runs
  bare `shellcheck -s sh`; a compatibility helper may need an explicit
  `SC2317`/`SC2329` disable with a reason.


- After fixing first errors, re-filter the same logs by warning class; a
  failed build hides warnings from translation units that never compiled.
  Keep the warning inventory in `docs/ci/COMPILATION-ERRORS.md` and do not
  call a build warning-free until a new manual run on the corrected revision
  reports it.
- `boost::program_options::variables_map::operator[]` exposes a const mapped
  value; fixture setup for an option that may not exist must use
  `insert_or_assign`, not assignment through `operator[]`.
- Windows worker tests must preserve message direction: the core sends
  requests and the worker sends replies. Restart backoff sums are not worker
  uptime; compare each generation's ready interval with `STABLE_PERIOD`.
- ShellCheck SC2015 can reject `[ A ] && [ B ] || fail` under the manual
  service-contract lane; express multi-field validation with an explicit
  `if`. Static-musl verification must recognize `file`'s `static-pie linked`
  wording without dropping the static-link requirement.


- Windows MSVC fail-fast exits (`0xc0000409`) from assertions are test
  failures, not crashes to suppress: read the assertion line and the exact
  run/job, then fix the direction/fixture/handle contract. Debian remove
  fixtures must seed realistic state in a non-package-owned subdirectory
  (`netDb/...`), not an unowned root-level file that dpkg may prune with an
  empty package-owned directory.

## Do not

- Do not compile, configure, run CTest, sanitizers, fuzzers, Gradle or dynamic
  project tests locally. Compile-free structural checks required by
  `AGENTS.md` may run locally.
- Do not dispatch or watch a workflow, read remote logs with `gh`, or otherwise
  act on GitHub without explicit coordinator authorization.
- Do not add `push`/`schedule` triggers or additional `pull_request` triggers,
  mutable action tags, persisted checkout credentials or `continue-on-error`.
- Do not hide a failure by deleting a test, weakening an assertion or
  suppressing warnings.
- Do not create commits, tags, releases, worktrees or temporary commits, and do not push; inspect `git status`/`git diff` before editing.
- Do not install dependencies, fetch/vendor code or perform hidden downloads to enable a local build or test. Preserve LICENSE/NOTICE/provenance and technical identifiers/paths unless an approved migration exists (AGENTS.md).

## Prepare for CI

- Verify tests registered by `openi2pd-build` carry the correct labels; keep
  the workflow label inventory complete. Add a new gate-relevant label to the
  explicit workflow loop.
- Add the job to the manual workflow and document the required toolchain.
- Include platform-specific registration and a negative capability test.
- Prepare evidence retention: run id, revision, commands, tool versions and
  failure logs.
- Record status as pending CI compilation/tests until the authorized run passes.

- Keep uploaded evidence inside `GITHUB_WORKSPACE`; `upload-artifact` v4 does
  not follow absolute paths outside it (for example `/tmp/static-file`).
- Compile-free test scripts are still CMake programs: an invalid escape such
  as `\.`, `\[` or `\0` in `string(REGEX REPLACE)` or `file(WRITE)` can fail
  the whole CTest lane even when no compiler runs.

## Tracking

Early compiler evidence must survive in every job family, not only the
ordinary Linux action. Sanitizer lanes retain separate configure/build
stderr logs, CMake cache and probe YAML even when tests never start.
Review capture with pipefail and unconditional artifact publication as
separate boundaries; negative fixtures must remove upload entries while
leaving tee intact. Missing binaries or logs before their phase starts
are evidence gaps, not clean runtime results. The workflow source guard
and `tools/tests/maintenance-checkers.t` cover this cross-job seam.

Use `CI`/J and `GOV-*`. Record workflow/job, revision, run id, result, first
failure and correction. Keep historical runs clearly separated from current
changes. Propose a dated `PROGRESS.md` entry and any `TODO.md`/`ROADMAP.md` changes to the coordinator; append them only when explicitly assigned; subagents report proposed rows to
the coordinator.

Record the exact AGENTS.md `Tracking states` value and state which validations were not run; do not collapse them into “pending”.

## References

- [CI job and triage matrix](references/ci-matrix.md)
- [CI policy](../../../docs/ci.md)
- [Testing plan](../../../docs/testing.md)
- [Global policy](../../../AGENTS.md)

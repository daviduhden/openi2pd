---
name: openi2pd-ci
description: Use when changing openi2pd manual GitHub Actions workflows, CTest label integration, sanitizer or fuzzer jobs, or triaging CI logs with GitHub CLI after explicit authorization.
---
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

- `.github/workflows/build.yml`, `build-openbsd.yml`, `build-windows.yml`,
  `fuzz.yml`, `packaging.yml`; the local composite actions under
  `.github/actions/`.
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

- Every workflow remains `workflow_dispatch`-only; no `push`,
  `pull_request`, `schedule` or automatic `workflow_call` unless the policy is
  deliberately changed by the coordinator.
- Pin third-party actions to commit SHAs with a version comment; set
  `persist-credentials: false` on checkout.
- Do not add `continue-on-error`, ignore a failing test or suppress warnings to
  make a job green.
- Do not attribute a run to a revision it did not build. Local uncommitted
  changes are not validated by an earlier remote run.
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

## Authorization and triage

- Do not run `gh workflow run`, `gh run watch` or any remote action without
  explicit coordinator authorization.
- When authorized, dispatch only the published revision and record workflow
  name, ref, run id and revision.
- Download logs to a private temporary path; classify failures by job/step and
  first error. Do not paste secrets or full logs into tracking files.
- Fix causes, not symptoms. Re-run after the correction only when authorized.
- Use `gh` for reading runs/logs only after authorization; never for commits,
  releases or secrets.

## Known issues and decisions

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
- Linux and sanitizer test builds pass `-DI2PD_REQUIRE_PERL=ON`; a missing
  interpreter must unregister tests loudly, not silently turn a lane green.
  OpenBSD base Perl satisfies the same requirement.
- The fuzzer inventory is fifteen targets across `tests/fuzz/*.cpp`,
  `tests/CMakeLists.txt`, `fuzz.yml` and `docs/testing.md`; the doc list,
  checked-in corpus, per-target cap and dual build/run registration are
  enforced by `check-build-system.cmake`. The sanitizer matrix is ASan+UBSan,
  UBSan-only and TSan; the fuzz job runs from `tests/fuzz/corpus/<target>/`
  with a per-target log and artifact directory.
- SIGPIPE under `pipefail` can turn a matching `grep -q` pipeline into status
  141 (false pass) or a failing check (false fail). Capture `javap`,
  `strings`, `file` or `nm` output to a temporary file before grepping.
- Workflows are `workflow_dispatch`-only; `workflow_call` is allowed only for
  a real reusable consumer. Manual results never validate an unpublished
  local tree.

## Do not

- Do not compile, configure, run CTest, sanitizers, fuzzers, Gradle or project
  validators locally.
- Do not dispatch or watch a workflow, read remote logs with `gh`, or otherwise
  act on GitHub without explicit coordinator authorization.
- Do not add `push`/`pull_request`/`schedule` triggers, mutable action tags,
  persisted checkout credentials or `continue-on-error`.
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

## Tracking

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

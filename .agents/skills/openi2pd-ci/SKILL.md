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
  `tools/*test*.cmake`, `tools/tests/` and `android/tools/`.
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

Launch read-only `explore` subagents for:

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

## Do not

- Do not compile, configure, run CTest, sanitizers, fuzzers, Gradle or project
  validators locally.
- Do not dispatch or watch a workflow, read remote logs with `gh`, or otherwise
  act on GitHub without explicit coordinator authorization.
- Do not add `push`/`pull_request`/`schedule` triggers, mutable action tags,
  persisted checkout credentials or `continue-on-error`.
- Do not hide a failure by deleting a test, weakening an assertion or
  suppressing warnings.
- Do not commit, push, tag or release.

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
changes. Append a dated `PROGRESS.md` entry; subagents report proposed rows to
the coordinator.

## References

- [CI job and triage matrix](references/ci-matrix.md)
- [CI policy](../../../docs/ci.md)
- [Testing plan](../../../docs/testing.md)
- [Global policy](../../../AGENTS.md)

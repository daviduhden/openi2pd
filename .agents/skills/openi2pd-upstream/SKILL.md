---
name: openi2pd-upstream
description: Use when reviewing or applying selective upstream i2pd, i2pd-docs or i2pd-tools changes, updating provenance, synchronization manifests or documenting an omitted upstream change.
---

## Maintenance & Continuous Learning

When this skill guides a repository change, review it after implementation. Update procedures, tools, configurations, assumptions, examples and validation steps when they changed or when reusable knowledge improves future work; correct inaccurate guidance and omit one-off execution details. Report synchronization in the task summary. Follow the normative process and Definition of Done in [AGENTS.md](../../../AGENTS.md#continuous-agent--skills-maintenance).
<!--
See the LICENSE file at the top of the project tree for copyright
and license details.
-->

# openi2pd upstream synchronization

## Activate when

Use this skill for a bounded upstream review, cherry-picking behavior into the
C++23/privsep/native-TUI architecture, updating provenance manifests or
recording why an upstream commit was omitted. It is not for ordinary feature
development.

Read [`../../../AGENTS.md`](../../../AGENTS.md) first. The fork keeps its
C++23, privilege-separation, native TUI/Android, CMake and release identity;
upstream synchronization is selective.

## Inspect first

- The exact upstream repository, branch and commit range under review.
- [`../../../docs/upstream-sync.md`](../../../docs/upstream-sync.md),
  [`../../../docs/UPSTREAM-README.md`](../../../docs/UPSTREAM-README.md) and
  [`../../../docs/files-changed.md`](../../../docs/files-changed.md).
- `contrib/upstream/sync.sh` and its path/file manifests; the script is for
  comparison/import workflow, not local execution by this skill.
- The local files that already implement the same behavior; avoid duplicating
  an earlier partial sync.
- License/provenance at
  [`../../../docs/attribution-audit.md`](../../../docs/attribution-audit.md).

## Reference sources

- [`../../../docs/upstream-sync.md`](../../../docs/upstream-sync.md) — imported
  sources, reviewed commits, rules and reproducible comparison.
- [`../../../docs/UPSTREAM-README.md`](../../../docs/UPSTREAM-README.md) —
  upstream documentation mapping.
- [`../../../contrib/upstream/sync.sh`](../../../contrib/upstream/sync.sh) and
  `contrib/upstream/*.txt` — manifests and path mapping.
- [`references/sync-checklist.md`](references/sync-checklist.md) — review and
  omission template.

## Reconnaissance

Launch at least one read-only `explore` subagent for upstream and one for the
local implementation. If subagents are unavailable, report that limitation; do
not substitute local execution. Ask for:

1. exact upstream commit/date/files and the behavior change;
2. whether the local tree already implements or rejects it;
3. architecture conflicts (C++20 fallback, browser console, GNU make, release
   metadata, vendored code, privilege assumptions);
4. required local tests/docs/provenance updates;
5. license/provenance risk.

Do not let a subagent execute `sync.sh`, clone into the source tree, build or
run tests.

## Parallel implementation

- Split by independent upstream commit/behavior after the conflict analysis.
  Do not parallel-edit the same local file.
- One owner maintains the disposition table and omission rationale.
- The coordinator approves architecture-changing imports and any new
  dependency/license.
- Keep a single local commit-equivalent change set per behavior; do not replay
  unrelated upstream formatting.

## Conventions

- Review commits individually; do not import a branch wholesale.
- Preserve local C++23, CMake, native TUI/Android, privilege separation and
  release identity. Omit upstream browser-console, GNU make, vendored
  submodule/download and release-metadata changes unless a separate approved
  task says otherwise.
- Reimplement behavior independently when direct code import conflicts with
  license/provenance or architecture. Record the decision.
- Keep I2P wire/persisted formats compatible; add a specification reference and
  regression for any change.
- Update `docs/upstream-sync.md` with commit, date, disposition (applied,
  already present, adapted, omitted, deferred) and reason.
- Preserve upstream notices and first-party license headers; do not relicense
  imported files.

## Known issues and decisions

- The current Java comparison target is 2.14.0; earlier 2.13.0 work is
  historical evidence. Upstream OpenSSL review windows use UTC timestamps,
  `%aI %cI`, and merge commits do not carry independent tree changes.
- Never use a moving `--since`/`--until` window that hides the pinned tip.
  Record removed/obsolete upstream changes explicitly instead of silently
  dropping them.
- Browser-console, obsolete GNU make and incompatible packaging changes remain
  intentionally omitted.
- The reviewed `openssl` high-water mark is
  `5bcf6101a456dc991e44d3d79cb69994c7bae484` (`9725b8b2..5bcf6101`, fifteen
  commits, seven files). Re-run `git ls-remote` before claiming it is still the
  tip; the window added no transport, netDb, crypto-primitive, dependency or
  wire-format change.
- Upstream torrent `cancel` replies are unconditional; keep the fork's
  `m_IsFast` gate. Upstream `Makefile.linux` and the Debian quilt series have no
  local target (CMake components and no GNU make), so their fixes are recorded
  as omitted rather than ported.
- Check optional Boost container optimizations against the exact declared
  floor before adapting them: `unordered_flat_map` needs the relevant newer
  Boost API, and `dynamic_bitset::find_first_off` is a Boost 1.90 facility.
  Do not raise the supported dependency floor for an optimization without a
  separately justified compatibility decision.
- Upstream OpenRC `command_args_background` assumes daemonizing command
  semantics. The local service uses foreground mode with `supervise-daemon`,
  and helper commands rely on `${name}`; assess both before porting init-file
  edits.
- Clearing `Packet::from` is required before retaining a reordered streaming
  packet: it is borrowed ratchet-session state, not packet-owned lifetime.
  The stream reorder queue currently lacks an isolated regression seam.
- No upstream change is validated until the corresponding manual CI lane and
  provenance/vectors are updated.

## Do not

- Do not run `sync.sh`, clone/fetch into the tree, build, test, fuzz or run
  dynamic project tests locally. Compile-free structural checks required by
  `AGENTS.md` may run locally.
- Do not merge an upstream branch, copy a whole directory or import code with
  unclear licensing.
- Do not reintroduce GNU make, the HTTP console, web assets, submodules or
  hidden downloads.
- Do not adopt upstream release/version metadata that conflicts with openi2pd
  identity.
- Do not create commits, tags, releases, worktrees or temporary commits, and do not push; inspect `git status`/`git diff` before editing.
- Do not install dependencies, fetch/vendor code or perform hidden downloads to enable a local build or test. Preserve LICENSE/NOTICE/provenance and technical identifiers/paths unless an approved migration exists (AGENTS.md).

## Prepare for CI

- Update static provenance/link checks and the relevant subsystem tests.
- Register focused regressions for each applied behavior; use the owning
  skill's labels.
- Check that documentation-link, branding and build-system-static checks still
  cover the changed manifests.
- Record upstream review as static until the manual CI build/tests pass. Do not
  attribute an upstream test result to the local tree.

## Tracking

Use `UPSTREAM-*` in `TODO.md` and the upstream sync document. Record the exact
commit range, disposition per commit, local files, tests and omitted changes.
Propose a dated `PROGRESS.md` entry and any `TODO.md`/`ROADMAP.md` changes to the coordinator; append them only when explicitly assigned; subagents report proposed rows to the
coordinator.

Record the exact AGENTS.md `Tracking states` value and state which validations were not run; do not collapse them into “pending”.

## References

- [Upstream synchronization checklist](references/sync-checklist.md)
- [Upstream sync policy](../../../docs/upstream-sync.md)
- [Attribution audit](../../../docs/attribution-audit.md)
- [Global policy](../../../AGENTS.md)

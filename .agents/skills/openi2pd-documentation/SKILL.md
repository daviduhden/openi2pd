---
name: openi2pd-documentation
description: Use when changing openi2pd documentation, source comments/Doxygen/Javadoc/POD/shell comments, licensing and attribution, branding metadata, manual pages or navigation, excluding subsystem design claims and coordinator-owned tracking edits unless assigned.
---

## Maintenance & Continuous Learning

When this skill guides a repository change, review it after implementation. Update procedures, tools, configurations, assumptions, examples and validation steps when they changed or when reusable knowledge improves future work; correct inaccurate guidance and omit one-off execution details. Report synchronization in the task summary. Follow the normative process and Definition of Done in [AGENTS.md](../../../AGENTS.md#continuous-agent--skills-maintenance).
<!--
See the LICENSE file at the top of the project tree for copyright
and license details.
-->

# openi2pd documentation and tracking

## Activate when

Use this skill for `docs/`, `README.md`, manuals, licensing/provenance,
branding metadata, in-source documentation conventions (C++ Doxygen, Android
Javadoc, Perl POD, POSIX shell), or a coordinated update of `TODO.md`,
`ROADMAP.md` and `PROGRESS.md`. Use the technical owner skill for design claims; this skill
owns coherence, attribution and tracking format.

Read [`../../../AGENTS.md`](../../../AGENTS.md) first. It remains the source of
permanent policy; do not copy it wholesale into docs or skills.

## Inspect first

- The changed document and every inbound/outbound link in the relevant section.
- `docs/source-documentation.md`, `docs/Doxyfile.source` and the manual
  `source_docs` lane when source comments or generated API docs change.
- `docs/file-headers.md`, `docs/attribution-audit.md`,
  `docs/files-changed.md`, `docs/REBRANDING.md`, `docs/branding/*`.
- `assets/branding/colors.md` and the SVG masters; do not redesign the mark.
- `docs/mkdocs.yml`, `man/*.1`, `README.md`, `AGENTS.md`. Documentation owns man-page
  *content*; `openi2pd-packaging` owns install manifests and service files.
- The current status vocabulary and task IDs in `TODO.md`; the historical
  chronology in `PROGRESS.md`; the workstreams in `ROADMAP.md`.

## Reference sources

- [`../../../docs/source-documentation.md`](../../../docs/source-documentation.md)
  — Doxygen/Javadoc/POD/shell comment conventions, contracts and the manual
  generation lane.
- [`../../../docs/file-headers.md`](../../../docs/file-headers.md) — short
  license-header rule and binary/no-header exceptions.
- [`../../../docs/attribution-audit.md`](../../../docs/attribution-audit.md) —
  copyright, provenance and third-party attribution.
- [`../../../docs/REBRANDING.md`](../../../docs/REBRANDING.md) and
  [`../../../docs/branding/assets.md`](../../../docs/branding/assets.md) —
  identity and asset sources.
- [`../../../docs/testing.md`](../../../docs/testing.md) — `perl-doc-links`,
  `docs-nav-static`, `android-locale-policy` and branding static checks.
- [`references/docs-tracking.md`](references/docs-tracking.md) — writing,
  licensing and tracking checklist.

## Reconnaissance

Launch at least one read-only `explore` subagent before editing, split across
independent areas when useful. If subagents are unavailable, report that
limitation; do not substitute local execution. Ask for:

1. authoritative code/docs for every factual claim;
2. inbound links, anchors and duplicated instructions;
3. license/provenance headers and third-party notices;
4. branding/color references and forbidden legacy artwork;
5. tracking rows and status words that need a coordinated update.

Ask for exact paths and contradictions. Do not let them run link checkers,
branding validators or a build.

## Parallel implementation

- Split independent docs/sections; do not let two agents edit the same file or
  tracking section.
- Technical owners supply facts; documentation owns wording, links and
  attribution. `openi2pd-tooling` owns the Perl check scripts
  (`check-doc-links.pl`, `validate-branding.pl`); documentation consumes their
  results and updates the docs, not the checker implementation, unless
  explicitly assigned. Do not invent a status or validation result.
- `AGENTS.md` is shared policy: coordinator review required. Keep it concise;
  link to skills rather than duplicating procedures.
- Tracking-file edits are coordinator-owned. Subagents report proposed rows and
  a dated progress entry; they do not edit `TODO.md`/`ROADMAP.md`/
  `PROGRESS.md` concurrently.

## Conventions

- Coordinate source comments and optional API documentation with the owning
  subsystem under the [source documentation policy](../../../AGENTS.md#source-documentation-policy).
- Inspect the implementation before writing source comments. Prefer concise
  technical American English and ASCII; keep comment lines within 72 columns
  unless the source format or documentation tool requires otherwise. Review
  existing comments for correctness as well as additions.

- Use the short project license reference for first-party source/config/script
  headers:
  `See the LICENSE file at the top of the project tree for copyright and
  license details.` Do not reintroduce duplicated SPDX/copyright boilerplate.
- Project documentation uses BSD-3-Clause, including
  `docs/file-headers.md` and `docs/torrent/snark-compatibility.md`.
  Third-party text/assets retain their own notices; never relicense Apache-2.0,
  BSL-1.0, Windows SDK, OpenBSD ports or imported components.
- Preserve `LICENSE`, NOTICE-equivalent attribution, upstream provenance and
  technical identifiers.
- Keep branding semantic: `assets/branding/colors.md` is the single color
  source. Do not redesign the official geometric mark, replace it with old
  i2pd/PurpleI2P artwork or modify `logo-original.png`.
- Keep SVG masters real vector geometry; no embedded PNG, runtime font
  dependency or automatic pixel tracing.
- Use relative links that resolve from the document's directory. Update anchors
  when a heading changes.
- Distinguish implemented-by-static-analysis, pending CI compilation, pending
  CI tests and validated-by-CI. Never claim dynamic evidence that does not
  exist.
- Keep historical entries append-only; correct current-state statements rather
  than rewriting history without cause.

## Known issues and decisions

- Reflowing a one-line Doxygen block can remove its closing delimiter if the
  wrapper treats `/** ... */` as ordinary prose. Preserve one opener, `*`
  continuation lines and one closer; scan changed C/C++ sources for repeated
  block openers before review.

- Claims must distinguish static implementation, pending CI compilation,
  pending CI tests and validated-by-CI. Historical local-run rows in
  `docs/testing.md` are not current evidence.
- Documentation is Markdown/MkDocs-compatible and English; preserve each
  document's declared license. UI localization is owned by `openi2pd-i18n` and
  does not translate docs.
  do not add HTML/JS/web assets or duplicate AGENTS policy.
- `check-doc-links.pl`, `check-doc-nav.pl`, `agent-skills-static` and
  `language-modernization-static` are the relevant compile-free checks; the
  latter two pin exact phrases and can break on cosmetic rewording.
- Link/nav/branding and CI validation remain pending manual Linux runs.

- Licensing is unified for project-authored docs: project code and documentation are
  BSD-3-Clause (`LICENSE`, `android/LICENSE`, `docs/LICENSE`);
  `docs/file-headers.md` and `docs/torrent/snark-compatibility.md` declare
  BSD-3-Clause; `tools/LICENSE` is the upstream BSD text. `NOTICE` is the
  inventory and is installed by CMake, Debian and both RPM specs. There is no
  standalone CC-BY-SA notice file or CC-licensed project documentation.
  `tests/check-legal-metadata.cmake` pins holders, SPDX, NOTICE and the Windows
  `LegalCopyright`; do not relicense or drop the upstream holders.
- `docs/i18n.md` is the single four-language policy: native
  `en/es/ru/zh-Hans`, Android `en/es/ru/zh-CN`, English fallback, `zh-TW`/
  `zh-Hant` fallback, plural arity and the translated/identifier boundary.
  README, the docs index and the configuration table must link it.
- `docs/FEATURE-PARITY.md` compares behavior, not UI architecture. Pin every
  Java source claim to the official revision and pair wire claims with the
  normative spec. Reconcile current-state rows when later native work closes
  an older gap; preserve dated history as historical evidence. Source review
  is not a Java/native differential result.
- Documentation stays Markdown/MkDocs-compatible and English-only. The
  `assets/branding/` tree is mirrored at `docs/assets/branding/`; update both
  and let `validate-branding.pl` compare them. Do not replace the official
  mark, palette or `openi2pd` name; `open-i2pd` is a frozen technical token.
- Upstream pins live in `contrib/upstream/sync.sh`, `docs/upstream-sync.md`,
  `docs/tools/provenance.md`, `tests/interop/check-reference.sh` and
  `FIXTURES.md`; `tests/check-upstream-pins.cmake` keeps them equal.

- Doxygen runs with `WARN_AS_ERROR`; a `@param` name that does not exist, a
  `@param` on an unnamed parameter or a reference to a removed symbol fails
  the manual lane. Match the declaration and only document verified behavior.
- A literal `*/` inside `//`/`/* */` prose closes the block early. Write
  `X-Forwarded-* and *-IP`; a malformed block can turn the remaining prose
  into code (`libi2pd_client/HTTPProxy.cpp` had this defect).
- Source comments are English, contracts and "why", not a narration of the
  next statement. Track unfinished guarantees in `TODO.md`; never describe a
  planned protection as implemented.
- Code-oriented checkers also scan comments. A prose word such as "until " in a
  Shepherd file can trip a forbidden-wait-loop rule; reword the comment,
  never weaken the checker. Run the owning compile-free checker after a
  comment pass.

## Do not

- Do not compile, run CTest, link/branding validators, `mkdocs`, man-page
  formatters or project scripts locally.
- Do not claim CI, package, device, interoperability or sanitizer validation
  from documentation/source inspection.
- Do not change protocol names, binary names, config/data paths or persisted
  formats in docs without a migration.
- Do not add personal credits, donation links or outdated upstream install
  instructions.
- Build/test/package workflows remain `workflow_dispatch`-only; the
  compile-free agent-maintenance PR gate is the only pull-request exception.
  See `openi2pd-ci` for workflow policy.
- Do not create commits, tags, releases, worktrees or temporary commits, and do not push; inspect `git status`/`git diff` before editing.
- Do not install dependencies, fetch/vendor code or perform hidden downloads to enable a local build or test. Preserve LICENSE/NOTICE/provenance and technical identifiers/paths unless an approved migration exists (AGENTS.md).

## Prepare for CI

- Keep `perl tools/scripts/check-doc-links.pl docs` and the branding validator
  in the Linux CI job; prepare them but do not run locally.
- Check Markdown anchors, relative paths and `docs/mkdocs.yml` navigation by
  inspection.
- Keep `docs/file-headers.md` exceptions current for new binary/generated
  files.
- Propose the `TODO.md` (stable ID/status/files/validation), `ROADMAP.md`
  (workstream/exit criteria) and `PROGRESS.md` (dated, append-only) rows to the
  coordinator; write them only when explicitly assigned.
- Keep `agent-skills-static` coherent and record the exact AGENTS.md tracking
  state; documentation/link checks remain pending until the manual Linux job
  runs.
- Doxygen warnings are errors in the source API lane. Every documented public
  parameter must have a matching `@param`, even when the surrounding prose
  describes the same bound; check added signatures against each tag.

## Tracking

Use `DOCS`/G, `BRAND`/`LEGAL`/J/K or the owning area. Record the document,
claim/source, links, license/branding impact and pending checks. Propose a dated `PROGRESS.md` entry and any `TODO.md`/`ROADMAP.md` changes to the coordinator; append them only when explicitly assigned. The coordinator integrates tracking changes; subagents
report proposed rows instead of concurrent edits.

Record the exact AGENTS.md `Tracking states` value and state which validations were not run; do not collapse them into “pending”.

## References

- [Documentation, licensing and tracking checklist](references/docs-tracking.md)
- [File headers](../../../docs/file-headers.md)
- [Attribution audit](../../../docs/attribution-audit.md)
- [Global policy](../../../AGENTS.md)

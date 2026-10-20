---
name: openi2pd-documentation
description: Use when changing openi2pd documentation, licensing and attribution, branding metadata, manual pages, documentation links or TODO/ROADMAP/PROGRESS tracking.
---
<!--
See the LICENSE file at the top of the project tree for copyright
and license details.
-->

# openi2pd documentation and tracking

## Activate when

Use this skill for `docs/`, `README.md`, manuals, licensing/provenance,
branding metadata, or a coordinated update of `TODO.md`, `ROADMAP.md` and
`PROGRESS.md`. Use the technical owner skill for design claims; this skill
owns coherence, attribution and tracking format.

Read [`../../../AGENTS.md`](../../../AGENTS.md) first. It remains the source of
permanent policy; do not copy it wholesale into docs or skills.

## Inspect first

- The changed document and every inbound/outbound link in the relevant section.
- `docs/file-headers.md`, `docs/attribution-audit.md`,
  `docs/files-changed.md`, `docs/REBRANDING.md`, `docs/branding/*`.
- `assets/branding/colors.md` and the SVG masters; do not redesign the mark.
- `docs/mkdocs.yml`, `man/*.1`, `README.md`, `AGENTS.md`. Documentation owns man-page
  *content*; `openi2pd-packaging` owns install manifests and service files.
- The current status vocabulary and task IDs in `TODO.md`; the historical
  chronology in `PROGRESS.md`; the workstreams in `ROADMAP.md`.

## Reference sources

- [`../../../docs/file-headers.md`](../../../docs/file-headers.md) — short
  license-header rule and binary/no-header exceptions.
- [`../../../docs/attribution-audit.md`](../../../docs/attribution-audit.md) —
  copyright, provenance and third-party attribution.
- [`../../../docs/REBRANDING.md`](../../../docs/REBRANDING.md) and
  [`../../../docs/branding/assets.md`](../../../docs/branding/assets.md) —
  identity and asset sources.
- [`../../../docs/testing.md`](../../../docs/testing.md) — docs-links/branding
  static checks.
- [`references/docs-tracking.md`](references/docs-tracking.md) — writing,
  licensing and tracking checklist.

## Reconnaissance

Launch read-only `explore` subagents for:

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

- Use the short project license reference for first-party source/config/script
  headers:
  `See the LICENSE file at the top of the project tree for copyright and
  license details.` Do not reintroduce duplicated SPDX/copyright boilerplate.
- Documentation remains CC-BY-SA-4.0 as declared in its license file.
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

## Do not

- Do not compile, run CTest, link/branding validators, `mkdocs`, man-page
  formatters or project scripts locally.
- Do not claim CI, package, device, interoperability or sanitizer validation
  from documentation/source inspection.
- Do not change protocol names, binary names, config/data paths or persisted
  formats in docs without a migration.
- Do not add personal credits, donation links or outdated upstream install
  instructions.
- Do not commit, push, tag or release.

## Prepare for CI

- Keep `perl tools/scripts/check-doc-links.pl docs` and the branding validator
  in the Linux CI job; prepare them but do not run locally.
- Check Markdown anchors, relative paths and `docs/mkdocs.yml` navigation by
  inspection.
- Keep `docs/file-headers.md` exceptions current for new binary/generated
  files.
- Update `TODO.md` (stable ID/status/files/validation), `ROADMAP.md` (workstream
  or exit criteria) and `PROGRESS.md` (dated, append-only) together.
- Record documentation/link checks as pending until the manual Linux job runs.

## Tracking

Use `DOCS`/G, `BRAND`/`LEGAL`/J/K or the owning area. Record the document,
claim/source, links, license/branding impact and pending checks. Append a dated
`PROGRESS.md` entry. The coordinator integrates tracking changes; subagents
report proposed rows instead of concurrent edits.

## References

- [Documentation, licensing and tracking checklist](references/docs-tracking.md)
- [File headers](../../../docs/file-headers.md)
- [Attribution audit](../../../docs/attribution-audit.md)
- [Global policy](../../../AGENTS.md)

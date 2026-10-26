<!--
See the LICENSE file at the top of the project tree for copyright
and license details.

openi2pd documentation and tracking checklist.
-->
# Documentation, licensing and tracking

Use with `SKILL.md`. This is a writing and review checklist, not a doc build.

## Claim hygiene

| Claim | Required evidence |
|---|---|
| Implemented | code + tests prepared; state `Implemented / static` |
| Compiles | manual CI compilation job passed for that revision |
| Tests pass | manual CI test job passed; name the lane |
| Secure/audited | findings recorded, fixes and sanitizer/fuzz evidence |
| Full privilege separation | workers + supervisor + negative capability tests exist |
| Package/service works | package build + service lifecycle run on a disposable host |
| Java-compatible | controlled Java/C++ run, not only source comparison |

Do not convert “source exists” into “validated”.

## Link and structure review

- Relative link resolves from the current document directory.
- Anchor matches the current heading text.
- `docs/mkdocs.yml` navigation includes new pages.
- No duplicate normative paragraph that will drift; link to the canonical doc.
- No stale “pending” statement contradicted by a later verified result.
- Man pages and `--help` output stay consistent.
- Commands use the real binary/path and do not require a public I2P network.

## License review

- First-party header uses the short LICENSE reference.
- First-party documentation uses BSD-3-Clause; its SPDX identifier, when
  present, must match that license.
- Third-party file retains its original notice; do not relicense.
- Imported code/assets have provenance and license text where required.
- Binary assets cannot carry a text header; list/exception is documented.
- `LICENSE`, `NOTICE`-equivalent attribution and `docs/attribution-audit.md`
  remain coherent.

## Branding review

- `assets/branding/colors.md` is the color source; use semantic tokens.
- Official mark comes from `assets/branding/symbol.svg` and approved masters.
- Do not modify `assets/branding/logo-original.png`.
- SVG is vector geometry; no embedded raster/font tracing.
- Android adaptive/themed icon, TUI palette, docs and packaging use the same
  identity.
- No old i2pd/PurpleI2P artwork or personal endorsement links.

## Tracking update

`TODO.md` row:

```text
- [ ] **[AREA-NNN]** Short task.
  - Priority P?; Status <vocabulary>; files and decision.
  - Files: ...
  - Validation: prepared checks, exact CI lane, what is still pending.
```

`ROADMAP.md`: update the owning workstream or add a concise dated section with
scope, exit criteria and remaining blockers.

`PROGRESS.md`: append a dated entry with scope, files, decisions, tests
prepared and validations *not* run. Never rewrite earlier chronology to make
the current state look better.

## Static checks prepared for CI

- `perl-doc-links`: `tools/scripts/check-doc-links.pl docs`
- `docs-nav-static`: `tools/scripts/check-doc-nav.pl`
- `android-locale-policy`, branding validator and palette/docs copy checks
- `agent-skills-static` when `.agents/skills/` is touched
- Markdown/XML/YAML inspection where applicable
- no local execution by the writing agent

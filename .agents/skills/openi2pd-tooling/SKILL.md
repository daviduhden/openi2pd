---
name: openi2pd-tooling
description: Use when changing openi2pd native tools, the firewall rule generator, Perl maintenance scripts, i2pdctl or tool install/inventory tests.
---
<!--
See the LICENSE file at the top of the project tree for copyright
and license details.
-->

# openi2pd tools and Perl

## Activate when

Use this skill for `tools/**`, `tools/scripts/**`, `tools/tests/**`, the
`i2pdctl` client, `routerinfo`/firewall generation, branding/doc-link helpers or
the Python-to-Perl migration. Use `openi2pd-packaging` for install manifests.

Read [`../../../AGENTS.md`](../../../AGENTS.md) first. Project-maintained
Python is replaced by Perl; do not add Python, CPAN or runtime downloads.

## Inspect first

- `tools/CMakeLists.txt`, `tools/*.cpp`, `tools/common/`,
  `tools/firewall.hpp`, `tools/routerinfo.cpp`.
- `tools/scripts/` (`i2pdctl`, `validate-branding.pl`,
  `check-doc-links.pl`) and `tools/tests/`. Tooling owns these scripts;
  documentation consumes the link/branding results and owns docs content.
- `tests/test-routerinfo-firewall.cpp`, tool inventory/smoke/regression CMake
  checks and `tools/README.md`.
- Relevant docs under `docs/tools/`, especially the changed tool and
  `docs/tools/provenance.md`.

## Reference sources

- [`../../../docs/tools/README.md`](../../../docs/tools/README.md) — tool
  inventory and usage.
- [`../../../tools/README.md`](../../../tools/README.md) — source-level tool
  conventions.
- [`../../../docs/tools/provenance.md`](../../../docs/tools/provenance.md) —
  imported upstream provenance and license boundaries.
- [`../../../docs/development/perl-migration.md`](../../../docs/development/perl-migration.md)
  — Perl replacement rules.
- [`../../../docs/torrent/security.md`](../../../docs/torrent/security.md) for
  torrent-related CLI/control boundaries.
- [`references/tooling-checklist.md`](references/tooling-checklist.md) — tool,
  firewall and Perl map.

## Reconnaissance

Launch read-only `explore` subagents for:

1. CLI parsing, input validation and output formats;
2. firewall endpoint/rule generation and platform backends;
3. Perl scripts, their runtime dependencies and tests;
4. CMake inventory/install and packaging consumers;
5. docs/provenance claimed by the tool.

Ask for exact command names/options and test names. Do not let them run a tool,
Perl test, shell test or network command.

## Parallel implementation

- Split native tools, firewall renderers and Perl scripts by independent files.
  Do not edit `tools/CMakeLists.txt` or shared `tools/common/` concurrently.
- The firewall endpoint model is shared by `firewall.hpp` and `routerinfo.cpp`;
  coordinate changes.
- Packaging manifests are owned by `openi2pd-packaging`.
- Keep docs/provenance with the tool change; do not copy upstream code without
  a license review.

## Conventions

- Native tools stay portable C++23 with the existing `tools/common` helpers;
  avoid GNU-only assumptions and platform-specific includes outside guards.
- Validate addresses, ports, file sizes and option combinations before
  generating output or touching files.
- Firewall output must be deterministic, quote/escape correctly and remain
  scoped to the selected family/table/chain; never execute rules.
- Legacy `routerinfo -f` output remains compatible; new explicit backends are
  additive.
- Perl uses core modules only unless an approved dependency already exists;
  no CPAN, Python, network install or hidden download.
- `i2pdctl` remains a bounded local client; do not add a cleartext remote admin
  path.
- Preserve upstream provenance/license notices for imported tools.
- Keep tool names, CLI behavior and install paths unless a migration is
  approved.

## Do not

- Do not compile, run tools, Perl/prove/shell tests, firewall commands,
  sanitizers or validators locally.
- Do not add Python, CPAN, Node, a runtime download or a hidden checkout.
- Do not add an HTTP/web admin console or cleartext torrent/RPC path.
- Do not execute generated firewall rules or require root in a test.
- Do not commit, push, tag or release.

## Prepare for CI

- Keep `tools-inventory`, `tools-smoke`, `tools-regressions`,
  `perl-install-layout`, `test-routerinfo-firewall` and `tools/tests/*.t`
  coherent.
- Register labels `tools`/`perl`/`security` as appropriate.
- Add malformed CLI/input cases for address, port, file and option parsing.
- Prepare packaging install-layout checks when a tool is added/renamed.
- Record tool execution as pending until the manual Linux/Windows/OpenBSD job
  runs; static help/version inspection is not execution.

## Tracking

Use `TOOLS`/`FW`/F and `PERL`/Perl. Record command/tool, input boundary, output
format, tests and install consumers. Append a dated `PROGRESS.md` entry;
subagents report proposed rows to the coordinator.

## References

- [Tooling and firewall checklist](references/tooling-checklist.md)
- [Tools documentation](../../../docs/tools/README.md)
- [Perl migration](../../../docs/development/perl-migration.md)
- [Global policy](../../../AGENTS.md)

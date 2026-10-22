---
name: openi2pd-tooling
description: Use when changing openi2pd native tools, the firewall rule generator, Perl maintenance scripts, i2pdctl, or tool install/inventory/provenance tests.
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
  `check-doc-links.pl`, `check-doc-nav.pl`) and `tools/tests/`. Tooling owns
  these scripts;
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
- [`references/perl-inventory.md`](references/perl-inventory.md) — script,
  test-name, label and CI-consumer matrix.

## Reconnaissance

Launch at least one read-only `explore` subagent before editing, split across
independent areas when useful. If subagents are unavailable, report that
limitation; do not substitute local execution. Ask for:

1. CLI parsing, input validation and output formats;
2. firewall endpoint/rule generation and platform backends;
3. Perl scripts, their runtime dependencies and tests;
4. CMake inventory/install and packaging consumers;
5. docs/provenance claimed by the tool.

Ask for exact command names/options and test names. Do not let them run a tool,
Perl test, shell test or network command.

## Parallel implementation

- Split native tools, firewall renderers and Perl scripts by independent files.
  `tools/CMakeLists.txt` and `tools/common/` are shared interfaces; obtain
  coordinator review before changing them and do not edit them concurrently.
- The firewall endpoint model is shared by `firewall.hpp` and `routerinfo.cpp`;
  coordinate changes.
- Packaging manifests are owned by `openi2pd-packaging`.
- Keep docs/provenance with the tool change; do not copy upstream code without
  a license review.

## Perl

Read [`references/perl-inventory.md`](references/perl-inventory.md) before
changing a `tools/scripts/*.pl`, `tools/tests/*.t`, `android/tools/*.pl` or
`tests/interop/check-vectors.pl` path. Keep core modules unless the dependency
is already approved, use the same interpreter as CMake/CI, and keep failures
non-zero and visible. `i2pdctl` remains an I2PControl JSON-RPC client; it is not
a TUI client.

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
- `i2pdctl` remains an HTTPS-only I2PControl JSON-RPC client with a loopback
  default; do not add cleartext or a browser console.
- Preserve upstream provenance/license notices for imported tools.
- Keep tool names, CLI behavior and install paths unless a migration is
  approved.

## Known issues and decisions

- Maintenance scripts that capture a directory must clear `CDPATH` before
  `cd`; diagnostic output must not contaminate the path. SAM version mismatches
  must fail the script, not only print a diagnostic. The manual packaging CI
  fake-`nc` lane checks failure propagation; live bridge tests remain pending.


- Signature-name normalization in `tools/common/key.hpp` uses a local ranges
  transform with unsigned-character input to `std::toupper`; keep mixed-case
  and underscore aliases covered by `tools-regressions` when editing the table.
- `i2pdctl` is an I2PControl JSON-RPC client, not a TUI client. Perl scripts
  use core modules or already-approved runtime modules; no CPAN/Python/Node
  downloads or hidden installs.
- Firewall tools render text only (`iptables`, `nftables`, `pf`, Windows) and
  never execute rules. Preserve address/port validation and quoting.
- Legacy `routerinfo -f` preserves IPv4 fragments but prefixes IPv6 rules with
  `ip6tables` to make family selection explicit (TOOL-012). The non-installed
  `routerinfo-fixture` helper creates a signed public RouterInfo only in CI;
  `tools-smoke` exercises the production CLI without checked-in private keys.
- `references/perl-inventory.md` maps scripts, tests, labels and consumers;
  update it when a script, interpreter requirement or CI consumer changes.
- Perl/tool install, portability and live firewall tests remain pending manual
  CI.

## Do not

- Do not compile, run tools, Perl/prove/shell tests, firewall commands,
  sanitizers or validators locally.
- Do not add Python, CPAN, Node, a runtime download or a hidden checkout.
- Do not add an HTTP/web admin console or cleartext torrent/RPC path.
- Do not execute generated firewall rules or require root in a test.
- Do not create commits, tags, releases, worktrees or temporary commits, and do not push; inspect `git status`/`git diff` before editing.
- Do not install dependencies, fetch/vendor code or perform hidden downloads to enable a local build or test. Preserve LICENSE/NOTICE/provenance and technical identifiers/paths unless an approved migration exists (AGENTS.md).

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
format, tests and install consumers. Propose a dated `PROGRESS.md` entry and any `TODO.md`/`ROADMAP.md` changes to the coordinator; append them only when explicitly assigned;
subagents report proposed rows to the coordinator.

Record the exact AGENTS.md `Tracking states` value and state which validations were not run; do not collapse them into “pending”.

## References

- [Tooling and firewall checklist](references/tooling-checklist.md)
- [Perl inventory](references/perl-inventory.md)
- [Tools documentation](../../../docs/tools/README.md)
- [Perl migration](../../../docs/development/perl-migration.md)
- [Global policy](../../../AGENTS.md)

---
name: openi2pd-tooling
description: Use when changing openi2pd native tools, the firewall rule generator, Perl maintenance scripts, i2pdctl, or tool install/inventory/provenance tests.
---

## Maintenance & Continuous Learning

When this skill guides a repository change, review it after implementation. Update procedures, tools, configurations, assumptions, examples and validation steps when they changed or when reusable knowledge improves future work; correct inaccurate guidance and omit one-off execution details. Report synchronization in the task summary. Follow the normative process and Definition of Done in [AGENTS.md](../../../AGENTS.md#continuous-agent--skills-maintenance).
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
product and maintenance scripts use Perl; do not add Python, CPAN or runtime
downloads. The shared agent-knowledge validator and its byte-identical
contract tests are the narrow Python exception required by `AGENTS.md`.
`tools/tests/perl-portability.t` exempts only those two exact paths and rejects
other maintained Python sources.

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

- Apply the [source documentation policy](../../../AGENTS.md#source-documentation-policy)
  to tool input/output, exit behavior and non-obvious Perl transformations.

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
- `i2pdctl` defaults to HTTPS with a loopback endpoint; an explicit `http://`
  URL is a documented loopback-only cleartext opt-in, not a general cleartext
  mode. Do not add a browser console or a network listener.
- Preserve upstream provenance/license notices for imported tools.
- Keep tool names, CLI behavior and install paths unless a migration is
  approved.
- `scripts/validate-agent-knowledge.py` and
  `tests/agent_knowledge_validator/test_validator.py` are the only Python
  files exempted by `perl-portability.t`; keep this shared validator contract
  byte-identical across adopters. Local mode inspects staged/unstaged plus
  untracked paths; PR mode accepts explicit `--base`/`--head` commit SHAs. Its
  substantive-text heuristic and link/section checks are structural only; a
  reviewer must still assess whether the update records useful knowledge.
  Never let it execute repository code.

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
- Firewall output is all-or-nothing: `routerinfo` buffers a run and writes
  stdout only after every input parsed, so a later malformed file cannot leave
  a partial rule set for a pipeline to apply. Usage errors go to stderr;
  `--help` stays on stdout. The 4096 budget counts emitted rules after family
  selection, not skipped endpoints.
- Special-purpose addresses are not public peers: NAT64 `64:ff9b::/96`, 6to4
  `2002::/16`, Teredo `2001::/32`, discard `100::/64`, `3fff::/20`, benchmark
  `198.18/15`, IETF `192.0.0/24` and 6to4 relay `192.88.99/24` are rejected.
  The `192.0.2/24`, `198.51.100/24`, `203.0.113/24` and `2001:db8::/32`
  documentation ranges stay allowed because the signed fixture uses them.
- nft table/chain identifiers reject keywords, a leading `_` and hook names;
  `IsSelectedByFamily` fails closed on an unknown family. PF labels are limited
  to 64 bytes including the NUL, so a library tag longer than 48 bytes is
  refused. Windows `-Name` keeps `~` from a base64 prefix and is deterministic
  but not idempotent: a second apply fails and must be treated as an operator
  error or preceded by an explicit removal.
- Renderers revalidate the endpoint at the optional return boundary; a direct
  `Windows`/`Pf`/`Nftables` call cannot emit a hand-built invalid aggregate.
  `EscapePowerShell` strips CR/LF as defense in depth.
- `references/perl-inventory.md` maps scripts, tests, labels and consumers;
  update it when a script, interpreter requirement or CI consumer changes.
- Perl/tool install, portability and live firewall tests remain pending manual
  CI.

- `autoconf_i2pd` is the interactive tool that uses the shared `i2p::i18n`
  catalogs (en/es/ru/zh-Hans) and reads a token for the yes/no answer so
  multibyte `s`/`д` work. Other CLI output, usage and machine-readable
  (`host=dest#!action=`) lines stay English; standalone tools resolve the
  environment on first translation.
- Never translate protocol/config identifiers or data labels that scripts
  parse. The i18n checker rejects such keys and `i2pdctl.t` pins its English
  argparse text.

- Key I/O belongs to `tools/common/keys.hpp`: exact-consumption
  `LoadPrivateKeys`, `SecureBuffer`, and `SavePrivateKeys` over
  `fs::AtomicWritePrivate`. Never `O_TRUNC`/`ofstream` a key path. Tools that
  can name their own input call `SameFile` and refuse; `autoconf_i2pd` uses
  `fs::AtomicWrite` and rejects a NUL file name.
- One `CryptoGuard` from `common/crypto.hpp`; no local clone or manual
  `InitCrypto`/`TerminateCrypto`. `x25519` uses core `X25519Keys`; numeric
  options use `ParseBounded`.
- Link `libi2pd` only; `autoconf_i2pd` alone links `libi2pdlang`. The getopt
  shim is Windows-only (`I2PD_TOOLS_GETOPT_TEST` builds the host test).
- Inventory parity: the CMake list, docs nav, Debian/RPM specs and workflow
  loop are one contract, checked by `tests/check-tools-inventory.cmake`. Add a
  per-tool doc page when adding a target. `contrib/upstream/tools-files.txt`
  lists upstream imports only; the Perl `i2pdctl` rewrite is fork-owned.
- CLI: explicit help is exit 0 on stdout; usage/runtime errors are stderr with
  a documented code; data/machine output stays on stdout; `regaddr_3ld` emits
  one LF-only record. `tools-cli`/`tools-getopt` are the focused tests.

- Perl helpers use a short POD header (purpose, options, I/O, exit codes,
  dependencies, portability) and inline comments only for non-obvious regex,
  transforms, retries or atomic writes. Shell helpers follow the POSIX rules in
  [`docs/source-documentation.md`](../../../docs/source-documentation.md).


- `TOOL-019` records that `tools/common/crypto.hpp` ignores `InitCrypto`
  failure and that `vain`/`regaddr_3ld` usage/error streams and `vain`'s
  default output name are inconsistent with the CLI contract. Tools must fail
  closed on provider initialization and keep usage on stderr with a non-zero
  status.


- `LoadPrivateKeys` lives in `i2p::tools` (`tools/common/keys.hpp`); a tool
  outside that namespace must qualify the call. The `b33offlinekeys` target
  exposed this only after the earlier build blockers were removed (CE-010).

## Do not

- Do not compile, run tools, Perl/prove/shell tests, firewall commands,
  sanitizers or dynamic project tests locally. Compile-free structural checks
  required by `AGENTS.md` may run locally.
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

- `i2pdctl` diagnostics are terminal-safe: `_display()` escapes control and
  non-ASCII bytes and the invalid-choice message does not add Python-argparse
  quotes. Tests must match the actual unquoted wording rather than the former
  argparse output.

- CMake test scripts must use valid quoted-argument escapes: `\.` and `\0`
  are invalid (and CMake cannot store NUL). Use `\\.` for a literal regex dot
  and let base64 decode an `AAAA` payload to NUL bytes instead of embedding
  one in the script.
- `perl-install-layout` covers both component ownership cases: `Tools` owns
  the licence only when documentation is disabled; otherwise install
  `Documentation` into the same private prefix before asserting it.

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

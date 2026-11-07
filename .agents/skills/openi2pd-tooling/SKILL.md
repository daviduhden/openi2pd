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
general-purpose auxiliary scripts use Perl, with native Shell orchestration
where suitable; do not add Python, CPAN or runtime
downloads. The agent-knowledge validator and its Test::More contract suite
are Perl-owned too. The portability inventory has no Python-source exception;
include auxiliary modules under `scripts/lib/` without requiring a shebang.

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
- `scripts/validate-agent-knowledge.pl` and
  `tests/agent_knowledge_validator/test_validator.t` implement the shared
  validator contract with core Perl dependencies. Local mode inspects staged/unstaged plus
  untracked paths; PR mode accepts explicit `--base`/`--head` commit SHAs. Its
  substantive-text heuristic and link/section checks are structural only; a
  reviewer must still assess whether the update records useful knowledge.
  Never let it execute repository code.

## Known issues and decisions

- Decode child results from Perl's documented wait word, not POSIX::WIF*
  helpers: native Win32 Perl can expose those names as throwing stubs.
  Preserve waitpid/spawn failure and POSIX signal outcomes separately from
  shifted exit values; do not interpret Win32 low bits as POSIX signals or
  let an abnormal nonzero status become exit zero. Keep synthetic normal
  exit/error cases and real POSIX signal fixtures in the runner regressions.
  A child can emit matching JSON before dying: validate the full terminal
  status before accepting output, and require successful pipe closure.

The lexical Doxygen checker distinguishes abstract parameter declarators
from named ones: standalone typedefs, qualified types, cv-qualified types
and builtin type combinations are unnamed but still count toward arity.
Keep negative named-operand and function-pointer fixtures alongside JNI
unnamed-class examples; never exempt a specific JNI type name to silence
a false positive. This checker does not resolve C++ symbol tables.
- Audit interpreter execution by operand role, not by a literal `-e` search:
  combined Perl flags, absolute/env interpreters, stdin/heredocs, generated
  fixtures and Shell documentation examples need review. `perl -c file`
  remains file-based validation. Keep executable test doubles in checked-in
  fixtures and pass paths as data instead of constructing source literals.
  The shell-policy checker is compile-free; its negative fixtures and helper
  behavior tests execute only in authorized CI.
- Cross-language validator reviews must cover Git NUL-delimited paths,
  staged/unstaged/range merge-base behavior, UTF-8 decoding, JSON null versus
  boolean/string/number types, TOML quoted path keys and safe child IO. Keep
  AK001-AK006, AKX001, schema and exit codes stable. Fixtures copy the complete
  validator module tree. CLI syntax/help wording can identify Perl, but policy
  results must preserve the contract. Do not restore a Python bootstrap.
- Registration commands share `ValidateRecordName`'s typed grammar failures
  and fixed `RecordNameErrorMessage` suffixes. Keep DEL, backslash and the
  255-byte limit visible in rejection diagnostics without echoing operands;
  validation precedes key/step-file reads. The boolean compatibility helper
  delegates to the same classifier. CLI regressions cover every registration
  consumer and the accepted maximum-length boundary; do not conflate this
  record grammar with DNS-label policy.

- The workflow checker reads the literal Windows debugger command file
  and allows only event description and code-location commands. Its
  maintenance regression copies workflow/action inputs into a temporary
  tree and rejects argument, local-variable and dump commands. Keep the
  checker offline; preparing the fixture is distinct from running the
  debugger or its regression suite.
- Perl::Critic's CI output must include the source path, line, column and
  policy. Numeric verbosity alone omitted filenames in the 2026-11-02 lane;
  `.perlcriticrc` now uses the named format fields so policy findings can be
  corrected by source without suppressing broad categories.
- A Perl/CMake format string never interpolates a caller value into the
  template; use `sprintf('%.*f', $precision, $size)`. Closed Win32 device
  names (`NUL`, `CON`, `COM1`..) are the device in every directory, including
  test fixtures.
- Win32 resolves `NUL`, `CON`, `AUX`, `PRN`, `COM1-9` and `LPT1-9` (any
  extension) as devices in every directory. A fixture named `nul.b64` reads
  EOF, the tool exits 0 and writes nothing, and the test can pass vacuously.
  Use compound stems, pin fixture bytes, and keep binary stdout in a file.
- `i2pbase64` decodes one quartet at a time so `=` terminates the stream and
  `data after padding` stays distinct across reads; unwritable key output is
  `Can't create <path>: the path must be a writable regular file` on stderr
  with each tool's documented exit status.
- A cross-read base64-padding fixture must use valid unpadded quartets in its
  prefix and one terminal padded quartet. Repeating `qg==` concatenates
  already-terminated values, so the decoder correctly reports data after
  padding; use a full unpadded quartet such as `qqqq` when sizing a decoded
  prefix at three output bytes per quartet.
- Never assert an `argv[0]`-bearing usage line; assert `usage: ` plus a
  synopsis token.

- `keyinfo` validates every section before the first stdout byte: an optional
  b33 offline-key batch is a successful summary with a stderr note, while
  structural corruption still fails. `-h` is stdout/0; usage and runtime
  errors are stderr/non-zero. The same split applies to `famtool`,
  `i2pbase64`, `regaddr_3ld`, `b33address` and `b33offlinekeys`; `verifyhost`'s
  verdict and `vain`'s prefix diagnostic are documented stdout exceptions.
- `routerinfo` disables the transports reserved-range admission for its own
  process (it never dials) and reports published addresses; firewall mode
  skips a well-formed non-public endpoint and fails only on a malformed one.
  `tools/common/key.hpp` owns `IsBlindableSigningKeyType()`.

- Maintenance scripts that capture a directory must clear `CDPATH` before
  `cd`; diagnostic output must not contaminate the path. SAM version mismatches
  must fail the script, not only print a diagnostic. The manual packaging CI
  fake-`nc` lane checks failure propagation; live bridge tests remain pending.
- Static interoperability vector validation treats `UNEXPECTED-PASS` as a
  non-zero result so stale `known_gap` markers cannot silently persist.
  Child-process test helpers must decode `waitpid` status with exit/signal
  predicates, retry `EINTR`, and fail on wait errors; an exit-byte shift alone
  can turn a signal death into success. POSIX signal/executable fixtures are
  skipped on Win32 when that process behavior is not portable.


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


- `CryptoGuard` retains the provider initialization outcome and exposes an
  explicit boolean check. Every consumer must reject a false guard before
  constructing keys, loading credentials or emitting results; help paths
  remain independent of provider availability. The guard still owns cleanup
  after failure. `tools-inventory` pins the guard and every consumer's early
  rejection without requiring a broken provider on the runner. A real
  missing-provider invocation remains pending manual CI.
- `vain` writes explicit help to stdout/0 and rejected invocations to
  stderr/1, including missing patterns and unsupported signatures. Keep
  `tools-cli` stream assertions and the `tools-regressions` no-operand status
  aligned; neither regression may start a probabilistic vanity search.
  Default collision names retain the winning address before their numeric
  suffix. Candidate checks do not reserve names: the shared private writer
  still requires a trusted parent directory, and collision publication needs
  manual CI coverage separately from deterministic CLI validation.


- `LoadPrivateKeys` lives in `i2p::tools` (`tools/common/keys.hpp`); a tool
  outside that namespace must qualify the call. The `b33offlinekeys` target
  exposed this only after the earlier build blockers were removed (CE-010).

- Every `CryptoGuard` declaration is a contract, regardless of variable
  name: the inventory checker derives the identifier and requires the fixed
  stderr rejection plus exit 1. `verifyhost` keeps `Valid destination` on
  stdout and every rejection on stderr; empty stdout means "not verified".
- The Perl syntax sweep and critic target are inventories: every maintained
  `.pl`/`.t` except `i2pdctl` (dedicated syntax target) must appear in both.
  `perl-cpp-comment-integrity` is registered by `tools/CMakeLists.txt`.
- The catalog unescape is one left-to-right pass for `\n`, `\t`, `\"` and
  `\\`; never apply ordered substitutions that reinterpret a literal
  backslash. Static Debian scripts follow the main package's phase policy,
  including a `triggered)` branch and non-zero unknown phase.

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
- Compile-free lexical Perl checks may protect source syntax across platforms;
  fixtures must cover comment tokens inside ordinary, escaped and raw strings
  so delimiters are recognized in lexical context rather than by grep alone.
  Keep maintained-source traversal explicit and prune vendored/generated
  trees; wire the check into the manual static CI job without executing it
  locally under the repository's no-test policy.
- `perl-install-layout` covers both component ownership cases: `Tools` owns
  the licence only when documentation is disabled; otherwise install
  `Documentation` into the same private prefix before asserting it.

## Tracking

When extracting Make recipes into command drivers, audit source contracts
for literals that moved with the implementation. Check delegation in the
Makefile and destinations in the helper separately; joining their texts
can let an unused helper conceal a missing call. OpenBSD's installation
contract covers all three phases (`dirs`, `data`, `script`) in
`tests/check-services.cmake`; mock argv coverage lives in
`tools/tests/make-drivers.t`. A passing source contract does not prove
installation succeeded on OpenBSD.

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

<!--
See the LICENSE file at the top of the project tree for copyright
and license details.

openi2pd: perl migration.

Documents project documentation.

-->

# Python-to-Perl migration

Status: **Implemented / static; Pending CI tests.** The current migration
includes the formerly exempt agent-knowledge validator and its black-box
suite. Source review and prepared regressions do not prove dynamic equivalence;
syntax, Test::More, portability and optional Critic runs remain CI work.

## Inventory

The recursive audit covered `*.py`/`*.pyw` files, Python shebangs (including
files without a `.py` extension), and references from Make/CMake/Gradle/CI/docs.
Third-party, generated, virtualenv and illustrative documentation snippets were
excluded.

| Original | Consumer(s) | Perl replacement | Install/validation |
|---|---|---|---|
| `tools/scripts/i2pdctl` (historical Python 3, shebang) | `tools/CMakeLists.txt`; `docs/tools/README.md`; `docs/tools/i2pdctl.md` | `tools/scripts/i2pdctl` (Perl 5, already implemented before this increment) | Existing CLI/transport/install regressions retained; no duplicate rewrite. |
| `scripts/validate-agent-knowledge.py` | Maintenance PR gate, AGENTS and developer procedures | `scripts/validate-agent-knowledge.pl` and core-only modules under `scripts/lib/` | Preserve worktree/range CLI, TOML settings, AK001-AK006/AKX001, UTF-8 text/JSON and exit 0/1/2; syntax, black-box and manual validation lanes prepared. |
| `tests/agent_knowledge_validator/test_validator.py` | Maintenance contract suite | `tests/agent_knowledge_validator/test_validator.t` | Test::More fixtures preserve original cases and add configuration/path/process boundary controls; no Python comparison runtime retained. |

No other project-maintained Python source, extensionless Python shebang,
embedded executor, dependency lockfile or Python build consumer was found in
the repeated independent audit. Generic
references in `docs/Doxyfile` describe optional Doxygen language support and
are not executable project scripts. `tools/tests/perl-portability.t` repeats
this inventory at test time and fails if a `.py`, Python shebang or Python
invocation returns to a maintained build consumer.

## Current Perl inventory

| Script | Role | Validation |
|---|---|---|
| `tools/scripts/i2pdctl` | I2PControl client | syntax, `prove`, install layout, portability |
| `tools/scripts/check-doc-links.pl` | offline Markdown link checker | syntax, `perl-doc-links`, portability, negative fixture |
| `tools/scripts/check-workflows.pl` | manual-only workflow and empty-CTest guard | syntax, `workflow-static`, `skills-static` |
| `scripts/validate-agent-knowledge.pl` and `scripts/lib/` | substantive instruction synchronization, section and local-link gate; TOML configuration | syntax, Test::More, `agent-maintenance.yml` PR gate and manual validation |
| `tests/agent_knowledge_validator/test_validator.t` | black-box validator contract and Git fixtures | syntax, `prove`, portability and manual validation |
| `tools/scripts/check-doc-nav.pl` | MkDocs navigation/unlisted-page checker | syntax, `docs-nav-static`, portability |
| `tools/scripts/check-doxygen-params.pl` | compile-free `@param`, `@ref` and command-balance guard: partial blocks, repeated names, header/definition duplicates, unresolved reference targets and Javadoc inline tags in C/C++ comments under Doxygen's `WARN_IF_DOC_ERROR` | syntax, `doxygen-static`, negative fixtures |
| `tools/scripts/validate-branding.pl` | static branding/palette validator | syntax, `prove` wrapper, portability |
| `tools/tests/cpp-comment-integrity.t` | nested/unterminated C/C++ block-comment scan | syntax, `perl-cpp-comment-integrity` |
| `tools/tests/i2pdctl.t` | Test::More client/transport regressions | `perl-i2pdctl` |
| `tools/tests/validate-branding.t` | Test::More branding wrapper | `perl-validate-branding` |
| `tools/tests/perl-portability.t` | Python-free/core-module/permissions guard | `perl-portability` |
| `tools/tests/maintenance-checkers.t` | negative fixtures for the link/nav/JNI/Javadoc/Doxygen checkers | `perl-maintenance-checkers` |
| `tests/interop/check-vectors.pl` | static interoperability fixtures | syntax, `interop-static` |
| `android/tools/check-android-locales.pl` | compile-free four-locale parity guard | syntax, `android-locale-policy` |
| `android/tools/check-android-javadoc.pl` | compile-free doclint comment guard (unregistered tags, raw placeholders, `@param` names) | syntax, `android-javadoc-static`, negative fixture |
| `android/tools/check-android-jni-symbols.pl` | Java/header/C++ JNI parity guard | syntax, `android-jni-symbols-static`, negative fixture |
| `i18n/check-i18n-catalogs.pl` | native catalog key/placeholder/plural contract | syntax, `i18n-static` |

All required imports are core modules; `IO::Socket::SSL` is an optional runtime
enhancement for `i2pdctl` and the documented `openssl s_client` fallback is
used when it is absent. The portability test also rejects `system`, `exec`,
`qx`, string `eval`, insecure temporary-name helpers and a world-writable
`chmod` in maintained Perl files. The scripts declare a Perl 5.16 floor; the
OpenBSD base interpreter is newer.

## Behaviour preserved

- `i2pdctl [-h] {exec,raw_info,info} ...`
- `I2PCONTROL_URL` default `https://127.0.0.1:7650/`.
- `I2PCONTROL_PASSWORD` default `itoopie`.
- `exec METHOD JSON`, `-j/--json-output`, `raw_info METHOD...`, `info`.
- Human-readable sizes/times, status names, interface/tunnel output (including
  the historical `B/s` bandwidth suffix) and the historical error/exit-code
  behavior.
- Self-signed TLS compatibility and local-only usage warning.

## Deliberate security/portability changes

- No shell invocation: external commands use `IPC::Open3` argument lists. The
  fallback flushes and closes the child's stdin (with `-ign_eof` so the TLS
  connection stays open) and reads stdout/stderr under a deadline.
- HTTPS never silently falls back to cleartext; without
  `IO::Socket::SSL`, a bounded `openssl s_client` exchange is used or the
  command fails clearly. `I2PDCTL_TLS_BACKEND=openssl` forces the fallback for
  diagnostics and tests; there is no cleartext value.
- Both transports bound the whole exchange, not just individual operations:
  the fallback has a 30 s read deadline and a 35 s alarm, and the `HTTP::Tiny`
  path adds a 35 s alarm around the request to stop slow-trickle peers.
  `SIGPIPE` is ignored while writing so a closed peer surfaces as an error
  instead of killing the client.
- Request/response sizes, URL syntax, JSON depth and chunked bodies are bounded.
  The fallback parser rejects malformed status lines, non-numeric
  `Content-Length`, unsupported `Transfer-Encoding` and missing chunk
  delimiters, and checks chunk sizes before allocating.
- IPv6 literals are bracketed for both the `Host` header and `-connect`.
  Option-like hosts are rejected; URLs with credentials, whitespace or control
  characters are refused.
- Server-provided strings printed by `info` have C0/DEL and C1 control
  characters escaped, so an attacker controlling the endpoint cannot inject
  terminal escapes.
- Proxy environment variables are removed inside a localized environment
  before `HTTP::Tiny` is created, and explicit `proxy`/`http_proxy`/
  `https_proxy` options disable inherited configuration. Credentials and
  tokens are therefore not forwarded to an unintended proxy. The regression
  test sets `http_proxy`, `https_proxy` and `all_proxy` to an unreachable
  endpoint and verifies the direct loopback request still succeeds.
- `openssl` is located only through absolute `PATH` entries; relative entries
  are ignored so a current-directory helper cannot be executed accidentally.
- Temporary handles are closed and child processes are reaped with a bounded
  grace period.
- Perl 5 core modules only; no CPAN download at build or run time.
- The daemon has no Perl runtime dependency.

## Validation commands

These are CI procedures. Under the repository's static-only local policy,
prepare syntax, suite and style execution without running them locally;
read-only consistency checkers remain subject to AGENTS.md.

```sh
perl -c tools/scripts/i2pdctl
perl -c scripts/validate-agent-knowledge.pl
perl -c scripts/lib/Openi2pd/AgentKnowledge/TOML.pm
perl -c tests/agent_knowledge_validator/test_validator.t
perl -c tools/scripts/check-doc-links.pl
perl -c tools/scripts/check-doc-nav.pl
perl -c tools/scripts/check-doxygen-params.pl
perl -c tools/scripts/validate-branding.pl
perl -c tools/scripts/check-workflows.pl
perl -c tests/interop/check-vectors.pl
perl -c android/tools/check-android-locales.pl
perl -c android/tools/check-android-javadoc.pl
perl -c android/tools/check-android-jni-symbols.pl
perl -c i18n/check-i18n-catalogs.pl
prove -v tools/tests/i2pdctl.t
prove -v tools/tests/maintenance-checkers.t
prove -v tools/tests/cpp-comment-integrity.t
I2PDCTL_TLS_BACKEND=openssl prove -v tools/tests/i2pdctl.t
prove -v tools/tests/validate-branding.t
prove -v tools/tests/perl-portability.t
prove -v tests/agent_knowledge_validator/test_validator.t
git diff --check
perlcritic --profile .perlcriticrc tools/scripts/i2pdctl  # optional, if installed
```

The test file runs a local fake HTTP JSON-RPC server and covers CLI errors,
authentication, `raw_info`, `exec`/`-j`, `info`, HTTP failures and malformed
URLs. A temporary `openssl` shim is placed first on `PATH` and
`I2PDCTL_TLS_BACKEND=openssl` forces the fallback, so the `s_client` argv
(SNI and bracketed IPv6), chunked and `Content-Length` framing, malformed
status, truncated/oversized bodies, unsupported transfer encodings, sanitized
stderr and the no-cleartext refusal are covered deterministically without a
network. A live TLS exchange against a real i2pd I2PControl endpoint (Linux and
OpenBSD/LibreSSL) remains the required dynamic validation.

CMake registers the Perl syntax, portability, branding, static maintenance,
comment-integrity and install-layout checks when Perl is available and
`I2PD_BUILD_TOOLS=ON` (the default). A `BUILD_TESTING` configuration without Perl keeps the native
tests but skips these optional static guards. `perl-i2pdctl` runs through `prove` when that
wrapper exists and directly runs the TAP file otherwise.
The registered names include `perl-i2pdctl-syntax`, the other
`perl-syntax-*` targets for every maintained script except `i2pdctl`,
`perl-i2pdctl`, `perl-portability`, `perl-validate-branding`,
`perl-maintenance-checkers`, `perl-cpp-comment-integrity`,
`perl-install-layout`, `perl-doc-links`, `docs-nav-static` and
`android-locale-policy`; the optional `perl-critic` target names the full
inventory when `-DI2PD_PERL_CRITIC=ON` is set.
`tools/tests/validate-branding.t` wraps the static asset validator with
Test::More. `perl-install-layout` installs only the `Tools` component into a
private prefix and verifies every declared native tool, the executable mode,
portable shebang and `perl -c` syntax of the installed `i2pdctl`, and the
installed `LICENSE.i2pd-tools` notice.
The OpenBSD port builds with `I2PD_BUILD_TOOLS=OFF` and therefore ships neither
the native tools nor `i2pdctl`; install the client from the Debian/RPM package
or run it from a checkout there.

`.perlcriticrc` is a non-blocking optional profile. Set
`-DI2PD_PERL_CRITIC=ON` to register the opt-in `perl-critic` CTest target when
`perlcritic` is installed; it is not a required dependency and no CPAN module
is required by the build or runtime.

## Complete validator migration: review baseline and dependencies

The originals were reviewed at repository revision
`38068e8d4ced79aad04035f80492ba5837382048` with these SHA-256 digests:

| Historical source | SHA-256 |
|---|---|
| `scripts/validate-agent-knowledge.py` | `c953aa5c2a367da25fdd90420966771e230a88a7a681596c30cf52d0a1de3187` |
| `tests/agent_knowledge_validator/test_validator.py` | `a1f315c07840a6da5aaac82e64fd5eca1c499b560e264a435b3affdb06ea4ff5` |

Both were maintained contract code, not generated or third-party files. Their
only imports were standard-library modules. The implementation read Git state,
configuration and instruction files and printed diagnostics; it did not modify
project files. The suite created private temporary Git repositories. The
former setup-python action and Python 3.11 floor belonged only to that gate;
no native, Android, package or installed daemon runtime requires them.

Review equivalence in these groups before accepting manual CI:

- CLI scope/format/revision constraints, help, stdout/stderr and exit 0/1/2.
- Staged/unstaged/untracked/deleted paths and NUL-delimited Git output;
  range mode compares the merge base with the explicit head revision.
- Instruction discovery, effective changed text, heading/frontmatter checks,
  local links, path containment, duplicate/opposed instruction warnings.
- Optional TOML tables and exact setting types, quoted repository path keys,
  arrays, booleans and bounded integer thresholds; invalid settings fail.
- Stable JSON schema with real booleans, null remediation and empty arrays;
  UTF-8 diagnostics and deterministic path/result ordering.
- Argument-list Git subprocesses without shell interpolation, portable separate
  output capture and deterministic child cleanup. Configuration never executes
  code. A private temporary diagnostic file avoids Win32 pipe-select limits.

The new suite is the retained executable regression authority; historical
sources remain recoverable from Git for review, without shipping a Python
shim or fetching an interpreter. CLI help identifies the Perl implementation;
parser-specific usage/configuration diagnostic wording is not a promise of
byte-identical interpreter messages. Rule outcomes, JSON shape, accepted
contract settings and documented exit behavior remain compatibility targets.
Other adopting repositories need their own coordinated migration; this work
makes no claim to have changed them.

The review also corrects documented-contract defects rather than preserving
them as accidental interpreter behavior: a boolean is not the integer
`minimum_changed_lines`, and `lf_only` must inspect original decoded line
endings before universal-newline normalization. TOML value nesting is capped
at 64; valid validator settings need only shallow typed arrays/tables. Git
uses literal path operands, refuses option-like revisions and disables
textconv/external diff and fsmonitor hooks; reading instructions must not
execute a repository-configured helper or refresh persistent index state.
These intentional hardening differences have focused regressions prepared.
No runtime or performance equivalence measurement has been executed locally.

Remaining mentions of Python in historical progress/CI evidence, imported
Doxygen language descriptions, i2pdctl compatibility comments and Markdown
slug naming describe provenance or tool behavior. They do not execute Python.
Do not delete attribution or falsify old CI evidence merely to remove a word.

MkDocs is an optional third-party publication tool, not a maintained project
Python program or a required build/test/installation dependency. Its existing
YAML navigation and imported Doxygen language-support descriptions are kept.
Use the Markdown files directly and the core-Perl link/navigation checkers for
Python-free development; replacing an optional external publisher is outside
source migration. No vendored third-party Python runtime/source was found.

The validator retains its historical generated-cache path exclusions as
change-gate data for contract compatibility. They do not exempt sources from
the independent portability inventory, invoke an interpreter, or require a
bytecode cache. The obsolete project ignore block was removed separately.

## Shell embedding and declarative Make continuation

The whole-tree follow-up inspected Shell shebangs (including extensionless
services), the five maintained Makefiles/rules, workflows/composite actions,
CMake/Gradle/package configuration and Shell documentation fences. It found
three active inline calls: a CI version probe and two install-mode checks.
Three documented commands also embedded Perl: corpus decoding, a SAM sender
and a redundant Test::More availability probe. No Python inline execution was
found. Named-file syntax checks are retained.

| Previous operation | Replacement | Interface and distribution |
|---|---|---|
| CI Perl version expression | `perl --version` | Uses the standard banner instead of the custom one-line log; the suite and CLI still enforce Perl 5.16. No helper is needed. |
| CMake Perl executable-bit expressions | POSIX `test -x` on UNIX | Discovered executable, argument-list invocation, same failure decision; installed Perl syntax validation remains separate. |
| Hex corpus command | `tests/interop/hex-to-binary.pl` | Named file to exact binary stdout, diagnostics on stderr. Malformed/odd hex fails before publication; input capped at 16 MiB. Source-checkout diagnostic, not installed daemon data. |
| SAM inline client | `tests/interop/sam-stream-send.pl` | Host/port/file operands and visible HELLO result/version retained. Loopback-only, 10 s total deadline, 64 KiB request and 8 KiB reply bounds. Recognized replies, including NOVERSION, mean successful exchange; unexpected fields/content fail without echoing them. |
| Test::More availability expression | Existing named suite | Removed redundant invocation; the suite already imports the module. |
| Generated constant Perl test doubles | Checked-in fixture files | Copied to owned temporary directories; no paths interpolated into program source. Negative scanner strings remain test data and are never executed. |

The SAM diagnostic normalizes accepted CRLF/LF transport replies to one LF
line and never prints arbitrary response fields.

The root Makefile now contains variables, dependencies and brief driver calls.
Its original phony ordering is retained, including configure before build and
build before install. Expanded `CMAKE_FLAGS` arrive as argv, not a string to
reparse. Cleanup checks physical root/source/ancestor aliases plus the cache
marker; this does not provide descriptor-based protection against a concurrent
pathname replacement. CMake/Ninja still decide incremental compilation.

| Makefile | Procedural owner |
|---|---|
| `Makefile` | `build/make-driver.sh` |
| `tests/Makefile` | Unchanged brief forwarding shim |
| `debian/rules` | `debian/build-driver.sh` for flags, configure/build/clean/install; Debhelper retains its service/package authority |
| `contrib/static/debian/rules` | `install-payload.sh`, copied with the complete static Debian source directory |
| `contrib/openbsd/port/Makefile` | `install-examples.sh`, merged beside the external Makefile and called through `${.CURDIR}` with actual `INSTALL_*` argv |

Debian flag-query failures and invalid/duplicate parallel counts fail before
CMake instead of being hidden by Make expansion. Valid flags, staging paths,
parallel overrides and framework install arguments are preserved. Debian's
GNU Make and OpenBSD's BSD ports dialects remain packaging-framework inputs;
the native front-end stays POSIX. Source helpers are not runtime package
commands. The existing `dist` target still archives HEAD, so unpublished
worktree additions are not represented as released source artifacts.

`check-shell-policy.pl` is a compile-free lexical guard, with explicit failure
on unreadable/oversized sources. It covers combined interpreter switches,
stdin/heredocs, common wrappers, Make variables, multiline configuration and
executable documentation fences. It is not a complete Shell/configuration
parser: dynamically constructed command names require manual review. Reports
name location/reason without printing source operands.

Syntax checks, ShellCheck, structural package/build/workflow checks and the
source inventory are separate from the prepared Test::More cases. Those cases
exercise argv/environment propagation, failures, cleanup refusal, static and
ports staging, binary conversion and loopback HELLO behavior with private
fixtures. No real build, package installation, Make execution or test suite was
run locally. Full behavioral and package validation remains Pending CI tests.

<!--
See the LICENSE file at the top of the project tree for copyright
and license details.

openi2pd: perl migration.

Documents project documentation.

-->

# Python-to-Perl migration

Status: **source migration complete for the currently inventoried script;
dynamic validation is CI/user work.** No maintained Python script remains after
the replacement below.

## Inventory

The recursive audit covered `*.py`/`*.pyw` files, Python shebangs (including
files without a `.py` extension), and references from Make/CMake/Gradle/CI/docs.
Third-party, generated, virtualenv and illustrative documentation snippets were
excluded.

| Original | Consumer(s) | Perl replacement | Install/validation |
|---|---|---|---|
| `tools/scripts/i2pdctl` (Python 3, shebang) | `tools/CMakeLists.txt`; `docs/tools/README.md`; `docs/tools/i2pdctl.md` | `tools/scripts/i2pdctl` (Perl 5) | `tools/CMakeLists.txt` installs the Perl file; `perl -c` and `tools/tests/i2pdctl.t` prepare regression coverage. The Perl rewrite is fork-owned and was removed from `contrib/upstream/tools-files.txt`. |

No other project-maintained `.py` file or Python shebang was found. Generic
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
| `tools/scripts/check-agent-maintenance.pl` | substantive instruction synchronization, section and local-link gate | syntax and `agent-maintenance.yml` PR gate |
| `tools/scripts/check-doc-nav.pl` | MkDocs navigation/unlisted-page checker | syntax, `docs-nav-static`, portability |
| `tools/scripts/validate-branding.pl` | static branding/palette validator | syntax, `prove` wrapper, portability |
| `tools/tests/i2pdctl.t` | Test::More client/transport regressions | `perl-i2pdctl` |
| `tools/tests/validate-branding.t` | Test::More branding wrapper | `perl-validate-branding` |
| `tools/tests/perl-portability.t` | Python-free/core-module/permissions guard | `perl-portability` |
| `tools/tests/maintenance-checkers.t` | negative fixtures for the link/nav/JNI checkers | `perl-maintenance-checkers` |
| `tests/interop/check-vectors.pl` | static interoperability fixtures | syntax, `interop-static` |
| `android/tools/check-android-locales.pl` | compile-free four-locale parity guard | syntax, `android-locale-policy` |
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

```sh
perl -c tools/scripts/i2pdctl
perl -c tools/scripts/check-doc-links.pl
perl -c tools/scripts/check-doc-nav.pl
perl -c tools/scripts/validate-branding.pl
perl -c tests/interop/check-vectors.pl
perl -c android/tools/check-android-locales.pl
perl -c android/tools/check-android-jni-symbols.pl
perl -c i18n/check-i18n-catalogs.pl
prove -v tools/tests/i2pdctl.t
prove -v tools/tests/maintenance-checkers.t
I2PDCTL_TLS_BACKEND=openssl prove -v tools/tests/i2pdctl.t
prove -v tools/tests/validate-branding.t
prove -v tools/tests/perl-portability.t
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

CMake registers the Perl syntax, portability, branding, maintenance-checker
and install-layout checks when Perl is available and `I2PD_BUILD_TOOLS=ON`
(the default). A `BUILD_TESTING` configuration without Perl keeps the native
tests but skips these optional static guards. `perl-i2pdctl` runs through `prove` when that
wrapper exists and directly runs the TAP file otherwise.
The registered names include `perl-i2pdctl-syntax`, the other
`perl-syntax-*` targets, `perl-i2pdctl`, `perl-portability`,
`perl-validate-branding`, `perl-install-layout`, `perl-doc-links`,
`docs-nav-static` and `android-locale-policy`.
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

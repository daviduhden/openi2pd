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
| `tools/scripts/i2pdctl` (Python 3, shebang) | `tools/CMakeLists.txt`; `docs/tools/README.md`; `docs/tools/i2pdctl.md`; `contrib/upstream/tools-files.txt` | `tools/scripts/i2pdctl` (Perl 5) | `tools/CMakeLists.txt` installs the Perl file; `perl -c` and `tools/tests/i2pdctl.t` prepare regression coverage |

No other project-maintained `.py` file or Python shebang was found. Generic
references in `docs/Doxyfile` describe optional Doxygen language support and
are not executable project scripts.

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
- Proxy environment variables are ignored by `HTTP::Tiny`, so credentials and
  tokens are not forwarded to an unintended proxy.
- Temporary handles are closed and child processes are reaped with a bounded
  grace period.
- Perl 5 core modules only; no CPAN download at build or run time.
- The daemon has no Perl runtime dependency.

## Validation commands

```sh
perl -c tools/scripts/i2pdctl
prove -v tools/tests/i2pdctl.t
I2PDCTL_TLS_BACKEND=openssl prove -v tools/tests/i2pdctl.t
git diff --check
perlcritic tools/scripts/i2pdctl   # optional, if installed
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


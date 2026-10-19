# i2pdctl

Perl 5 client for the retained I2PControl HTTPS service. It preserves the CLI
of the former project-maintained Python script and uses only Perl core modules
(`Config`, `File::Spec`, `Getopt::Long`, `JSON::PP`, `HTTP::Tiny`, `IPC::Open3`,
`IO::Select`, `POSIX`, `Symbol`) plus the system `openssl` binary for the
self-signed HTTPS fallback when `IO::Socket::SSL` is unavailable.

## Syntax

```sh
i2pdctl [-h] {exec,raw_info,info} ...
```

## Environment

- `I2PCONTROL_URL` - default `https://127.0.0.1:7650/`. Only `http://` and
  `https://` are accepted.
- `I2PCONTROL_PASSWORD` - default `itoopie`; change it in the router
  configuration.
- `I2PDCTL_TLS_BACKEND` - `auto` (default) or `openssl`. `auto` prefers
  `IO::Socket::SSL` through `HTTP::Tiny` and falls back to the shell-free
  `openssl s_client` transport. `openssl` forces the fallback and exists for
  diagnostics and the regression tests. There is deliberately no cleartext
  option, and an invalid value exits with status 1.

## Examples

```sh
i2pdctl info
i2pdctl raw_info RouterInfo ClientServicesInfo
i2pdctl exec RouterManager '{"Shutdown":""}'
```

## Security

i2pd generates a self-signed management certificate. The client intentionally
does not verify it, matching the previous script's
`ssl._create_unverified_context` behavior. When `IO::Socket::SSL` is absent it
uses a shell-free `openssl s_client` fallback and refuses to downgrade to
cleartext. Only use it on a trusted local link; do not expose I2PControl to an
untrusted network. Change the default password before enabling the service.

Because the fallback does not verify the peer certificate, `https://` protects
against passive observers but not against an active man-in-the-middle. Use
`http://` only for a loopback service bound to localhost: the password is then
sent in cleartext. Do not expose I2PControl to an untrusted network in either
mode.

The client bounds requests and responses to 1 MiB each, limits JSON nesting
(128 levels decoded, 64 encoded), validates the URL scheme, host, port and
path, and rejects credentials, whitespace and control characters in URLs.
Whole exchanges are time-bounded (30 s per socket operation plus a 35 s
wall-clock alarm) so a slow or stalled peer cannot hang the command.
The fallback HTTP parser enforces the status-line syntax, a numeric
`Content-Length`, chunked framing with per-chunk and cumulative size checks,
and rejects unsupported `Transfer-Encoding` values. Request bodies are counted
in bytes. Values supplied by the server that `info` prints to the terminal are
escaped for C0/DEL and C1 control characters, so a spoofed endpoint cannot
inject ANSI escapes or forge output lines.

External commands use an argument list (`IPC::Open3`), never a shell; the
validated host, port and path are passed as separate arguments, and no
`eval STRING` is applied to network data. Proxy environment variables are
removed from a localized environment before `HTTP::Tiny` is created, and the
explicit proxy options are disabled, so the password and token are not
forwarded to an unintended proxy. The `openssl` fallback is found only through
absolute `PATH` entries; relative entries are ignored. The self-signed TLS
exception is documented as a compatibility choice, not a general
recommendation.

## Compatibility

Targets the I2PControl JSON-RPC methods implemented by `daemon/I2PControl*`.
It is installed by `tools/CMakeLists.txt` as `i2pdctl`; it is not part of the
daemon's runtime dependency graph. No CPAN installation or automatic module
download is required. See `docs/development/perl-migration.md` for the
inventory and validation commands.

## Validation

```sh
perl -c tools/scripts/i2pdctl
perl -c tools/scripts/validate-branding.pl
prove -v tools/tests/i2pdctl.t
prove -v tools/tests/validate-branding.t
perlcritic --profile .perlcriticrc tools/scripts/i2pdctl   # optional
git diff --check
```

CMake registers `perl-i2pdctl-syntax`, `perl-i2pdctl`, `perl-portability`,
`perl-validate-branding` and `perl-install-layout` when Perl is available.
`perl-install-layout` installs only the `Tools` component into a private
prefix and checks the installed file mode, shebang and `perl -c` syntax.
Test::More is used by the client and branding tests; Perl::Critic remains an
optional static style check (`-DI2PD_PERL_CRITIC=ON`) and is not a build or
runtime dependency.

The regression test runs a local fake JSON-RPC server for the plain HTTP path
and a temporary `openssl` shim with `I2PDCTL_TLS_BACKEND=openssl` for the
fallback path. The shim covers chunked and `Content-Length` framing, malformed
status lines, truncated and oversized bodies, unsupported transfer encodings,
hostname SNI, bracketed IPv6 arguments, sanitized `openssl` diagnostics and
the refusal to continue without TLS. These are static/regression checks; a
live TLS exchange with a real i2pd router is still required for dynamic
validation.

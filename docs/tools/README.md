<!--
See the LICENSE file at the top of the project tree for copyright
and license details.

openi2pd: README.

Documents project documentation.

-->

# Auxiliary tools

The tools in `../../tools/` are imported from
[PurpleI2P/i2pd-tools](https://github.com/PurpleI2P/i2pd-tools) and built
against the in-tree `libi2pd`/`libi2pdclient`. They are independent
executables; there is no submodule and no external i2pd checkout.

Build everything (tools are enabled by default):

```sh
cmake -S build -B build-output -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-output --parallel
```

Disable them with `-DI2PD_BUILD_TOOLS=OFF`. Executables are written to
`build-output/tools/` and installed to the configured `bindir`.

## Security

Several tools read or write private destination/router keys. Treat those files
as secrets:

- Use a private directory and do not commit key files. Native key outputs are
  created with owner-only permissions on POSIX and best-effort owner read/write
  on Windows.
- `keyinfo -p`, `x25519`, and `offlinekeys` can print or copy secret material.
- `famtool -g` writes an unencrypted EC private key PEM. Protect it and keep it
  offline.
- Tools never contact the I2P network or download code. `autoconf_i2pd` and
  `i2pdctl` are interactive/network clients, not build steps.

## Formats

Key files use the I2P destination key format (`PrivateKeys::ToBuffer`) unless a
tool documents otherwise. Base64 follows the I2P alphabet (`-~`, padding with
`=`); see [i2pbase64](i2pbase64.md). No imported tool changes the persisted
format.

## Tool index

| Tool | Purpose |
|---|---|
| [autoconf_i2pd](autoconf_i2pd.md) | Interactive configuration generator |
| [b33address](b33address.md) | Derive a b33 address from a destination |
| [b33offlinekeys](b33offlinekeys.md) | Create per-day offline keys for encrypted LeaseSets |
| [famtool](famtool.md) | Create/sign/verify router family certificates |
| [i2pbase64](i2pbase64.md) | Encode/decode I2P base64 |
| [keygen](keygen.md) | Generate a destination key pair |
| [keyinfo](keyinfo.md) | Inspect a destination key file |
| [offlinekeys](offlinekeys.md) | Create an offline signing key batch |
| [regaddr](regaddr.md) | Sign a host registration record |
| [regaddr_3ld](regaddr_3ld.md) | Sign a three-level subdomain record |
| [regaddralias](regaddralias.md) | Sign an alias/old-destination record |
| [routerinfo](routerinfo.md) | Print addresses and outbound firewall rules from RouterInfo files (iptables/ip6tables, nftables, Windows Defender, OpenBSD PF) |
| [vain](vain.md) | Vanity address generator |
| [verifyhost](verifyhost.md) | Verify a host registration signature |
| [x25519](x25519.md) | Generate an X25519 key pair for encrypted LeaseSets |
| [i2pdctl](i2pdctl.md) | Perl client for I2PControl |

## Tests

CTest registers three native-tool tests:

- `tools-smoke`: key generation, key inspection, base64, registration and
  verification, offline keys, X25519 and CLI validation.
- `tools-regressions`: per-tool regressions beyond the smoke test, including a
  scripted non-interactive `autoconf_i2pd` transcript. It covers
  `keyinfo` with Ed25519, RedDSA and P-256 keys and every output option;
  `b33address` and `b33offlinekeys` including invalid inputs and the default
  day count; `regaddr`, `regaddr_3ld` and `regaddralias` cross-checked with
  `verifyhost`, including tamper rejection; `famtool` generation, the family
  name boundary and negative argument paths; `i2pbase64` text, UTF-8, control
  bytes, embedded NULs, whitespace and malformed input; `offlinekeys` defaults,
  limits and output parseability; and `vain` thread, pattern, signature and
  regex bounds. It never starts a vanity search or contacts the network.
- `tools-inventory`: every native tool target and its source are present, and
  the pinned upstream manifest is checked for missing files.

The regression test generates ephemeral keys below the build tree, restricts
them to the owner where the platform supports it, and removes the directory on
success and on failure. It uses only CMake, has a timeout per tool invocation,
and does not depend on Perl or Python. The positive `famtool` sign/verify path
still needs a RouterInfo fixture and is not covered yet. All workflows remain
manual; dynamic validation of this test is pending the next manual CI run. No
fuzzing has been run on the imported parsers, and dynamic Windows CLI testing
also remains pending.

The Perl `i2pdctl` client is covered separately by `perl-i2pdctl-syntax`,
`perl-portability`, `perl-install-layout` and the Test::More suites; see
[i2pdctl](i2pdctl.md).

The maintained native tools use C++23 and the in-tree `libi2pd`/`libi2pdclient`
libraries. `tools/common/getopt.*` provides the small getopt/getopt_long
compatibility used by MSVC while Unix keeps the system implementation.

See [provenance and inventory](provenance.md) for the pinned upstream commit,
upstream and fork-only files, licence notices and per-executable test coverage.

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

- Use a private directory (`umask 077`) and do not commit key files.
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
| [routerinfo](routerinfo.md) | Print addresses from a RouterInfo file |
| [vain](vain.md) | Vanity address generator |
| [verifyhost](verifyhost.md) | Verify a host registration signature |
| [x25519](x25519.md) | Generate an X25519 key pair for encrypted LeaseSets |
| [i2pdctl](i2pdctl.md) | Python client for I2PControl |

## Tests

The tools are compiled by CI. Functional smoke tests are described in
`../testing.md`; the key-generation/registration round trip was executed
manually during integration. No fuzzing has been run on the imported parsers.

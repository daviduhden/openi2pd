# keygen

Generate an I2P destination private key file.

## Syntax

```sh
keygen <output-file> [<signature-type>]
```

`<signature-type>` accepts a number or a name. The default is
`EDDSA-SHA512-ED25519` (7). Supported names include `DSA-SHA1`,
`ECDSA-SHA256-P256`, `ECDSA-SHA384-P384`, `ECDSA-SHA512-P521`,
`RSA-SHA256-2048`, `RSA-SHA384-3072`, `RSA-SHA512-4096`,
`EDDSA-SHA512-ED25519`, `RED25519-SHA512`,
`GOSTR3410-A-GOSTR3411-256`, and `GOSTR3410-TC26-A-GOSTR3411-512`.

## Example

```sh
keygen service.keys ED25519-SHA512
keyinfo -v service.keys
```

## Output

The output file is the binary `PrivateKeys` serialization used by i2pd
(`keys =` in `tunnels.conf`). It contains the private key.

## Security

On POSIX the key file is created with owner-only permissions (`0600`) and an
existing regular file is tightened; Windows applies a best-effort owner
read/write mode. The tool wipes its temporary buffer after writing, but the
file itself remains until deleted.

## Compatibility

Signature and crypto types are the I2P protocol constants from
`libi2pd/Identity.h`. Ed25519ph/type 8 uses plain Ed25519; other OpenSSL-dependent types require a
library that supports them.

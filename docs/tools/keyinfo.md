<!--
See the LICENSE file at the top of the project tree for copyright
and license details.

openi2pd: keyinfo.

Documents project documentation.

-->

# keyinfo

Inspect an I2P destination private key file.

## Syntax

```sh
keyinfo [-v] [-d] [-p] [-b] <privatekey.dat>
```

| Option | Output |
|---|---|
| (none) | b32 address only |
| `-d` | destination public key in base64 |
| `-p` | private destination key in base64 |
| `-v` | human-readable identity details |
| `-b` | b33 blinded address and offline-key batch summary |

## Example

```sh
keyinfo -v service.keys
keyinfo -b service.keys
```

## Output and formats

Reads the binary `PrivateKeys` format. `-d` prints the public destination,
`-p` prints secret material, and `-v` includes the signature/encryption types,
creation fields and optional offline signature. `-v` selects the readable
listing and takes precedence over `-d`/`-p`; `-b` adds its summary to either
form.

`-b` requires a blindable signing key (ED25519-SHA512 or RED25519-SHA512) and
is refused for any other type. The b33 offline-key batch is optional: a key
file without one is summarized successfully and the absence is reported on
stderr, while a structurally invalid or truncated batch is an error. A key
file carrying the batch reports its day count and covered date range on
stdout.

## Exit status and streams

`0` on success, `2` for a usage or file-read error, and `3` when a key file
or option combination cannot be interpreted (unsupported format, unblindable
`-b` destination, invalid offline signature or batch). Data and `-h` help text
go to stdout; usage and runtime diagnostics go to stderr. Every section is
validated before the first stdout byte, so a failing invocation leaves stdout
empty; `keyinfo -p -b` therefore never prints private key material for a
request it is about to reject.

## Security

`-p` writes the private key to stdout. Redirect carefully. The tool reads the
whole file with a 16 MiB cap and wipes its buffer after parsing.

## Compatibility

Works with destination keys produced by i2pd and the original i2pd-tools. Router
keys are not accepted; use `famtool`/`routerinfo` for RouterInfo data.

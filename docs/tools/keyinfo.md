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
creation fields and optional offline signature.

## Security

`-p` writes the private key to stdout. Redirect carefully. The tool reads the
whole file with a 16 MiB cap and wipes its buffer after parsing.

## Compatibility

Works with destination keys produced by i2pd and the original i2pd-tools. Router
keys are not accepted; use `famtool`/`routerinfo` for RouterInfo data.

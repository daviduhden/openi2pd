<!--
See the LICENSE file at the top of the project tree for copyright
and license details.

openi2pd: verifyhost.

Documents project documentation.

-->

# verifyhost

Verify a signed I2P host registration record.

## Syntax

```sh
verifyhost '<host record>'
```

The record is the form produced by `regaddr`, `regaddr_3ld`, or
`regaddralias`, e.g. `name.i2p=<destination>#!sig=<signature>`.

## Example

```sh
verifyhost "$(cat hosts.txt)"
```

## Output

Prints `Valid destination`, `Invalid destination signature.`, or a structure
error. Exit status is 0 for a valid record and non-zero otherwise.

## Security

Verification uses the destination signature type and OpenSSL-backed
`IdentityEx::Verify`. The signature is base64-decoded with strict validation;
malformed or truncated signatures are rejected. The tool does not trust the
hostname itself; it only verifies that the record was signed by the included
destination.

## Compatibility

Supports current destinations and the legacy `#olddest=`/`#oldsig=` alias
fields. It does not fetch the address book or contact the network.

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

The verdict `Valid destination` is the only line written to stdout. All
rejection diagnostics (`Invalid destination record.`, `Invalid destination
base64.`, `Destination signature not found.`, `Invalid destination signature
encoding.`, `Invalid destination signature.`, `Invalid old destination
base64.`, `Old destination signature not found.`, `Invalid old destination
signature.`), the provider failure `Crypto initialization failed` and the
usage line are written to stderr. Exit status is 0 for a valid record and
non-zero otherwise, so a caller may treat empty stdout as "not verified"
without reading the status.

## Security

Verification uses the destination signature type and OpenSSL-backed
`IdentityEx::Verify`. The signature is base64-decoded with strict validation;
malformed or truncated signatures are rejected. The tool does not trust the
hostname itself; it only verifies that the record was signed by the included
destination.

## Compatibility

Supports current destinations and the legacy `#olddest=`/`#oldsig=` alias
fields. It does not fetch the address book or contact the network.

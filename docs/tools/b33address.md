<!--
See the LICENSE file at the top of the project tree for copyright
and license details.

openi2pd: b33address.

Documents project documentation.

-->

# b33address

Derive a b33 (blinded) address from a destination.

## Syntax

```sh
b33address
```

Reads one base64 destination (public key) from stdin and prints the b33 address
and today's blinded store hash.

## Example

```sh
keyinfo -d service.keys | b33address
```

## Output

`<b33>.b32.i2p` and the current store hash in base64. The address rotates by
day and requires an Ed25519 or RedDSA destination.

## Security

The input is a public destination, not a private key. The tool hashes/blinds
locally and performs no network access.

## Compatibility

Uses `BlindedPublicKey` from `libi2pd`, matching the EncryptedLeaseSet/b33
scheme used by current I2P implementations.

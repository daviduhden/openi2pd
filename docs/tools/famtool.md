# famtool

Create a router family certificate, sign a RouterInfo, or verify a signed
family membership.

## Syntax

```sh
famtool [-v] [-g -n family -c family.crt -k family.pem]
famtool [-v] -s -n family -k family.pem -i router.keys -f router.info
famtool [-v] -V -c family.crt -f router.info
```

## Examples

```sh
umask 077
famtool -g -n i2pfam -c myfam.crt -k myfam.pem
famtool -s -n i2pfam -k myfam.pem -i router.keys -f router.info
famtool -V -c myfam.crt -f router.info
```

## Security

- `-g` writes an **unencrypted** EC private key PEM. Keep it offline and never
  publish `myfam.pem`.
- The family name is limited so it fits the 50-byte signed family blob.
- Certificate parsing uses OpenSSL; the tool now uses `TLS_method()` and checks
  allocations instead of the removed `TLSv1_method()`.
- Signatures are decoded with strict base64 before verification.

## Compatibility

Produces the family certificate and `family`/`family.sig` RouterInfo properties
used by i2pd and I2P Java. Curve: P-256, SHA-256.

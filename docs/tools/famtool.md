<!--
See the LICENSE file at the top of the project tree for copyright
and license details.

openi2pd: famtool.

Documents project documentation.

-->

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
famtool -g -n i2pfam -c myfam.crt -k myfam.pem
famtool -s -n i2pfam -k myfam.pem -i router.keys -f router.info
famtool -V -c myfam.crt -f router.info
```

## Security

- `-g` writes an **unencrypted** private key PEM. OpenSSL builds use the
  provider `EVP_EC_gen("P-256")` path and PKCS#8 `BEGIN PRIVATE KEY` output;
  LibreSSL uses its EC fallback. On POSIX the file is explicitly restricted to
  owner read/write and a failed `fchmod` aborts generation. Keep it offline and
  never publish `myfam.pem`. Windows uses the process ACL and should be
  checked manually.
- The family name is limited so it fits the 50-byte signed family blob.
- Certificate parsing uses OpenSSL's `TLS_method()` and checked EVP ownership;
  no removed fixed-version TLS method or deprecation-suppression macro is used.
- Family signatures use `EVP_DigestSignUpdate`/`EVP_DigestSignFinal`, strict DER
  decoding and the fixed 32-byte `r || s` I2P wire representation. Signatures
  are decoded with strict base64 before verification.

## Compatibility

Produces the family certificate and `family`/`family.sig` RouterInfo properties
used by i2pd and I2P Java. Curve: P-256, SHA-256.

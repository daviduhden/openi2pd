# openi2pd tools

Auxiliary tools imported from
[PurpleI2P/i2pd-tools](https://github.com/PurpleI2P/i2pd-tools) at commit
`ca7ece8da86aa80b58fce6fa64375bfd75cbf843` and integrated into the main
openi2pd tree.

There is no Git submodule and no external i2pd checkout. The tools link the
in-tree `libi2pd`/`libi2pdclient` and are built by the main CMake project.
See [docs/tools](../docs/tools/README.md) for the maintained documentation
and [provenance](../docs/tools/provenance.md) for the pinned upstream files,
licence notices and per-executable test coverage.

## Building

```sh
cmake -S build -B build-output -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-output --parallel
```

Disable with `-DI2PD_BUILD_TOOLS=OFF`. Binaries are in `build-output/tools/`.
GNU Make is not used.

## Security

Some tools read or write private keys. Protect key files with `umask 077`,
never commit them, and review the per-tool warnings in `docs/tools/`.

## Tools included

### verifyhost
#### Example usage
```bash
# Valid destination with old destination signature
./verifyhost 'test12356.i2p=7qYyu4ZkY5ixxLTUn6RF-LvX7YlFZGcpGQPJITq7NieLKW-pwJuyxz7Ga06HzNwieLackwmBVsbizzPHffFXiwqwTcsTbjZvYdRs4oEyCg1V1qoEMaQXHZkwhcIZ0DVFEkscytDy327C9-x5gH0d2lNdrVf2k2dmWKU3hUMSqYRHHOHgJyGIHF8bv4nPCaUjVu9gL6g1Nzfxf5mEwx1-vIsV3NW83Pe9fpXaoqQ~zOXKZ-rOz8h4yfHJ7WKARxEFaUNALa6Kkm4~FOctB6aV05Meh~OW7GzM~ZUalguaDpGkqbOr~mK8b6o8gzOJTWUOku4jHmVeD9doePudAxygPhahZt5j5bCNPSezCCa6DVbLCqJLQr6uK0izlJnrsYnYFqFm3mPlsI09J7MIJroNVssKoktCvq4rSLOUmeuxidhCkv18Y-WwjT0nswgmug1WywqiS0K-ritIs5SZ67GJ2Oshc-W7rUv86yoFksGfsXaeBzGiKX9i0R~UqcxTsjRjBQAEAAcAAA==#!action=adddest#olddest=GcxIh28qJRLIBfZ3AO~-hQdZhJhYM93zeODGYXyzPBE~gQmIYTbw84d~kwQXzzWDvqgpTmwYlqejUiGWOGXTrfPlByoWpKwUf-1Oe~kARvSwWLCpyuAPDNAMm~xTxexsjZ4Ry7Ad7IlxCrlQgBVus6l2t7ZznHa1OPIJh-zMzXoX9DNQpvSVdVYMXXKguQr4HFSa2LVQkPXrVrqXLMj1QDmFrXEwZAhWujntVstCoC8u00B6xhQSqTsvHhca63ZYkge78SNILbpWQQKb7XbjJKkQS99tLFbG-KSERm5YrP61PdF25TIUsiFN7i07HkseUTGMbuwHsfFp6-uXYMK2gz9Vyv0kXYu0ahoTEUJmC0DbYsLGpkF7UI1hWp43eWmWP1XK~SRdi7RqGhMRQmYLQNtiwsamQXtQjWFanjd5aZYwl8r9JF2LtGoaExFCZgtA22LCxqZBe1CNYVqeN3lplr8ep7m~ZwjgWrnjuxEVY52zLnp8oiVo1Hstf~F8w22KBQAEAAcAAA=='
Valid destination

# Destination with missing old signature
./verifyhost 'test123456.i2p=GcxIh28qJRLIBfZ3AO~-hQdZhJhYM93zeODGYXyzPBE~gQmIYTbw84d~kwQXzzWDvqgpTmwYlqejUiGWOGXTrfPlByoWpKwUf-1Oe~kARvSwWLCpyuAPDNAMm~xTxexsjZ4Ry7Ad7IlxCrlQgBVus6l2t7ZznHa1OPIJh-zMzXoX9DNQpvSVdVYMXXKguQr4HFSa2LVQkPXrVrqXLMj1QDmFrXEwZAhWujntVstCoC8u00B6xhQSqTsvHhca63ZYkge78SNILbpWQQKb7XbjJKkQS99tLFbG-KSERm5YrP61PdF25TIUsiFN7i07HkseUTGMbuwHsfFp6-uXYMK2gz9Vyv0kXYu0ahoTEUJmC0DbYsLGpkF7UI1hWp43eWmWP1XK~SRdi7RqGhMRQmYLQNtiwsamQXtQjWFanjd5aZYwl8r9JF2LtGoaExFCZgtA22LCxqZBe1CNYVqeN3lplr8ep7m~ZwjgWrnjuxEVY52zLnp8oiVo1Hstf~F8w22KBQAEAAcAAA==#!action=addsubdomain'
Destination signature not found.

# Invalid signature
./verifyhost 'test.i2p=GcxIh28qJRLIBfZ3AO~-hQdZhJhYM93zeODGYXyzPBE~gQmIYTbw84d~kwQXzzWDvqgpTmwYlqejUiGWOGXTrfPlByoWpKwUf-1Oe~kARvSwWLCpyuAPDNAMm~xTxexsjZ4Ry7Ad7IlxCrlQgBVus6l2t7ZznHa1OPIJh-zMzXoX9DNQpvSVdVYMXXKguQr4HFSa2LVQkPXrVrqXLMj1QDmFrXEwZAhWujntVstCoC8u00B6xhQSqTsvHhca63ZYkge78SNILbpWQQKb7XbjJKkQS99tLFbG-KSERm5YrP61PdF25TIUsiFN7i07HkseUTGMbuwHsfFp6-uXYMK2gz9Vyv0kXYu0ahoTEUJmC0DbYsLGpkF7UI1hWp43eWmWP1XK~SRdi7RqGhMRQmYLQNtiwsamQXtQjWFanjd5aZYwl8r9JF2LtGoaExFCZgtA22LCxqZBe1CNYVqeN3lplr8ep7m~ZwjgWrnjuxEVY52zLnp8oiVo1Hstf~F8w22KBQAEAAcAAA==#!sig=0CiP4rB3x2EVmTMzlXBseUUnjENO900OLaw5Db5ylQwXYcnbmSbVoiR2nxNrjBqrdMOTDF8G-Ps2Wuv-TZ5yDA2='
Invalid destination signature
```

### i2pbase64

encode/decode b64 string
example usage:
```
user@computer:~/openi2pd/tools$ ./i2pbase64
encode
ZW5jb2RlCg==^C
user@computer:~/openi2pd/tools$ ./i2pbase64 -d
ZW5jb2RlCg==
encode
```
U also can use file

### routerinfo

Print addresses and derive **outbound-only** firewall rules from one or more
RouterInfo files. The tool never executes a firewall command or edits a system
firewall configuration.

```sh
# Address listing (IPv4 only unless -6 is given)
./routerinfo -p ~/.i2pd/netDb/r6/routerInfo-blah.dat

# Legacy -f fragments (kept for compatibility)
./routerinfo -6 -f ~/.i2pd/netDb/r6/routerInfo-blah.dat

# Explicit backend: iptables/ip6tables commands
./routerinfo -6 --firewall=iptables ~/.i2pd/netDb/r6/routerInfo-blah.dat

# nftables; the family, table and chain are explicit
./routerinfo -6 --firewall=nftables --nft-family=inet \
  --nft-table=filter --nft-chain=output ~/.i2pd/netDb/r6/routerInfo-blah.dat

# Windows Defender Firewall commands for an elevated PowerShell session
./routerinfo -6 --firewall=windows ~/.i2pd/netDb/r6/routerInfo-blah.dat

# OpenBSD pf.conf rules (review and validate before loading)
./routerinfo -6 --firewall=pf ~/.i2pd/netDb/r6/routerInfo-blah.dat
```

`-6` controls IPv6 selection in all modes. Rules cover NTCP2/TCP and SSU2/UDP;
private, CGNAT, loopback, link-local, multicast and reserved addresses are
rejected. See [the complete reference](../docs/tools/routerinfo.md) for
validation, backend-specific syntax checks and the no-apply policy.

### keygen

Generate an I2P private key

#### Usage

Make a EDDSA-SHA512-ED25519 destination key

    ./keygen privkey.dat

Make an destination key with a certain key type

    ./keygen privkey.dat <number>

or

    ./keygen privkey.dat <key name>


| key name                             | number |
| ------------------------------------ | ------ |
| DSA-SHA1                             | 0      |
| ECDSA-SHA256-P256                    | 1      |
| ECDSA-SHA384-P384                    | 2      |
| ECDSA-SHA512-P521                    | 3      |
| RSA-SHA256-2048                      | 4      |
| RSA-SHA384-3072                      | 5      |
| RSA-SHA512-4096                      | 6      |
| EDDSA-SHA512-ED25519                 | 7      |
| GOSTR3410_CRYPTO_PRO_A-GOSTR3411-256 | 9      |
| GOSTR3410_TC26_A_512-GOSTR3411-512   | 10     |
| RED25519-SHA512                      | 11     |

For more information on the types of signatures, see the
[local tunnel documentation](../docs/user-guide/tunnels.md#signature-types).
### vain

Vanity generation address.

#### Usage

./vain --help

#### Time to Generate on a 2.70GHz Processor
| characters| time to generate (approx.) |
| -------------------- | --------------- |
|         1 	       | ~0.082s	     |
|         2	           | ~0.075s	     |
|         3	           | ~0.100s	     |
|         4	           | ~0.394s	     |
|         5	           | ~6.343s	     |
|         6	           | ~1m-5m	 	     |
|         7	           | ~30m	 	     |

### keyinfo

Prints information about an I2P private key

#### Usage

Print just the b32 address for this key

     ./keyinfo privatekey.dat

... just the base64 address

    ./keyinfo -d privatekey.dat

Print all info about the public key

    ./keyinfo -v privatekey.dat

### regaddr

Generate authentication string to register a domain on reg.i2p and stats.i2p

    ./regaddr domain.dat domain.i2p > auth_string.txt
    cat auth_string.txt

Send output of auth_string to http://reg.i2p/add and http://stats.i2p/i2p/addkey.html

To register a subdomain, use [`regaddr_3ld`](#regaddr_3ld)

### regaddr_3ld

Generate authentication string to register a subdomain on reg.i2p and stats.i2p in 3 steps

    ./regaddr_3ld step1 sub_domain.dat sub.domain.i2p > step1.txt
    ./regaddr_3ld step2 step1.txt domain.dat domain.i2p > step2.txt
    ./regaddr_3ld step3 step2.txt sub_domain.dat > step3.txt
    cat step3.txt

Send output of auth_string to http://reg.i2p/add and http://stats.i2p/i2p/addkey.html

### regaddralias

Generate authentication string to register an alias address for existing domain on reg.i2p and stats.i2p

    ./regaddralias domain-oldkeys.dat domain-newkeys.i2p domain > auth_string.txt
    cat auth_string.txt

Send output of auth_string to http://reg.i2p/add and http://stats.i2p/i2p/addkey.html

### x25519

Generate a key pair with output in base64 encoding. The x25519 keys are used
for authentication with an encrypted LeaseSet; see the
[local encrypted LeaseSet guide](../docs/tutorials/blinded-addresse.md).

### famtool
Program for working with a family of routers in the I2P network. See the
[local family documentation](../docs/user-guide/family.md).

usage: ```./famtool [-h] [-v] [-g -n family -c family.crt -k family.pem] [-s -n family -k family.pem -i router.keys -f router.info] [-V -c family.crt -f router.info]```

generate a new family signing key for family called ``i2pfam''
```./famtool -g -n i2pfam -c myfam.crt -k myfam.pem```

sign a router info with family signing key
```./famtool -s -n i2pfam -k myfam.pem -i router.keys -f router.info```

verify signed router.info
```./famtool -V -n i2pfam -c myfam.pem -f router.info```
#### Example of usage
    $ ./famtool -g -n i2pfam -c myfam.crt -k myfam.pem
    family i2pfam made
    $ cat myfam.crt
    -----BEGIN CERTIFICATE-----
    MIIB3TCCAYOgAwIBAgIBADAKBggqhkjOPQQDAjB4MQswCQYDVQQGEwJYWDELMAkG
    A1UECAwCWFgxCzAJBgNVBAcMAlhYMR4wHAYDVQQKDBVJMlAgQW5vbnltb3VzIE5l
    dHdvcmsxDzANBgNVBAsMBmZhbWlseTEeMBwGA1UEAwwVaTJwZmFtLmZhbWlseS5p
    MnAubmV0MB4XDTIzMDczMTE5MjQ1MFoXDTMzMDcyODE5MjQ1MFoweDELMAkGA1UE
    BhMCWFgxCzAJBgNVBAgMAlhYMQswCQYDVQQHDAJYWDEeMBwGA1UECgwVSTJQIEFu
    b255bW91cyBOZXR3b3JrMQ8wDQYDVQQLDAZmYW1pbHkxHjAcBgNVBAMMFWkycGZh
    bS5mYW1pbHkuaTJwLm5ldDBZMBMGByqGSM49AgEGCCqGSM49AwEHA0IABLejNp1Y
    1tnMGFaUIuzNpNL8B9KvaeSSh+OWhRcHn2x1D8TPCVA4IMS0jiMIodCcdyTmz0Qg
    SXsWDqT2CbBMENQwCgYIKoZIzj0EAwIDSAAwRQIgU58CeHXlluPZNxpmxb7HSHsJ
    STCA8C946oas+8uLU+MCIQDe6Km/h8w+oCh+j6UHobN0EAVtQPQGwkq2XXs1jqX2
    bQ==
    -----END CERTIFICATE-----
    $ cat myfam.pem
    -----BEGIN EC PRIVATE KEY-----
    MHcCAQEEIAFA82vZzZN8nZIOVnadSS73G2NNc5pUsh4qmpK2M0nsoAoGCCqGSM49
    AwEHoUQDQgAEt6M2nVjW2cwYVpQi7M2k0vwH0q9p5JKH45aFFwefbHUPxM8JUDgg
    xLSOIwih0Jx3JObPRCBJexYOpPYJsEwQ1A==
    -----END EC PRIVATE KEY-----

### b33offlinekeys

Per-day keys that let a router publish an encrypted LeaseSet (a b33 address) without
holding the destination's signing key, read by i2pd since 2.62.0. For every day the tool
blinds the destination's key, authorizes a transient key with an offline signature, and
appends the transients and their signatures to the keys file. The destination must be
Ed25519 or RedDSA, and the address stops being published when the batch runs out.

#### Usage

    ./b33offlinekeys b33batch.dat privkey.dat 365

Then, in `tunnels.conf`:

    [my-service]
    type = server
    keys = b33batch.dat
    i2cp.leaseSetType = 5

### AutoConf

A program to help create the openi2pd configuration file

For a now a better way to manual write the config file

For usage just run ./AutoConf or AutoConf.exe

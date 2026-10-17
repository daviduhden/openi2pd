# offlinekeys

Create a destination key file with an offline signature and transient keys.

## Syntax

```sh
offlinekeys <output-file> <keys-file> [<signature-type>] [<days>]
```

Defaults: `EDDSA-SHA512-ED25519`, 365 days. Days must be in `1..3650`.

## Example

```sh
offlinekeys service-offline.keys service.keys ED25519-SHA512 30
```

Use the output as `keys =` in a tunnel/server configuration. The router can sign
with the transient key while the main destination key stays offline until the
expiry.

## Security

The input and output files both contain private material. The tool wipes
temporary buffers; protect the files themselves. An offline batch stops being
valid after the expiry date; the router then cannot publish that destination
until a new batch is supplied.

## Compatibility

Uses the standard I2P offline signature format understood by current i2pd and
I2P Java. The generated key types must be supported by the library used to load
them.

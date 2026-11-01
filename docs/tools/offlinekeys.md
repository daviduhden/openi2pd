<!--
See the LICENSE file at the top of the project tree for copyright
and license details.

openi2pd: offlinekeys.

Documents project documentation.

-->

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

## Output

On success the destination's base32 hash, the signature type and the day count
are printed on stdout. Usage errors, an unreadable or structurally invalid key
file, an unsupported signature type or an out-of-range day count are written to
stderr and never touch stdout.

The batch is installed through the owner-only atomic writer, so the output path
must be a regular file that can be created or replaced. A directory, a
device, a path below a missing directory or another non-writable target is
refused with `Can't create <path>: the path must be a writable regular file`
on stderr and status 3; the input key file and any existing output file are
left unchanged.

Exit status: 0 success, 1 usage or an output path that resolves to the input,
2 an unreadable key file or a bad signature-type/day argument, 3 an invalid key
file or a failed write, 4 the core cannot create offline keys for the requested
type.

## Security

The input and output files both contain private material. Native output is
created with owner-only permissions on POSIX and best-effort owner read/write
on Windows; the tool also wipes temporary buffers. Protect the files
themselves. An offline batch stops being valid after the expiry date; the
router then cannot publish that destination
until a new batch is supplied.

## Compatibility

Uses the standard I2P offline signature format understood by current i2pd and
I2P Java. The generated key types must be supported by the library used to load
them.

# b33offlinekeys

Create per-day offline keys that let a router publish an encrypted (b33)
LeaseSet without holding the destination signing key.

## Syntax

```sh
b33offlinekeys <router-keys-file> <destination-keys-file> [<days>]
```

The destination must be Ed25519 or RedDSA. Days default to 365 and are bounded
by the tool's `kMaxDays` constant (1..365).

## Example

```sh
b33offlinekeys b33batch.dat service.keys 30
```

Tunnel configuration:

```ini
[my-service]
type = server
keys = b33batch.dat
i2cp.leaseSetType = 5
```

## Security

Reads the destination private key and writes a new private key batch. The
output is created with owner-only permissions on POSIX (best-effort owner
read/write on Windows). Keep both offline. The batch expires; publication
stops when the days run out.

## Compatibility

The format is read by i2pd 2.62+ and follows the encrypted LeaseSet offline
signature scheme. The router keys file is accepted for compatibility with the
original tool; the function currently uses the destination keys and the
requested day count.

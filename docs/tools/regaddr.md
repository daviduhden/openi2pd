# regaddr

Create a signed host registration record for reg.i2p/stats.i2p.

## Syntax

```sh
regaddr <private-key-file> <hostname> > record.txt
```

## Example

```sh
regaddr service.keys example.i2p > example.record
verifyhost "$(cat example.record)"
```

Submit the record to the address-book registration service you trust.

## Security

The command signs using the destination private key. Do not send the key file;
only the resulting signed record is public. Signatures are verified by
`verifyhost` and by the registration service.

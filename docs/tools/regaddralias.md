# regaddralias

Create a signed alias record linking an old destination to a new destination.

## Syntax

```sh
regaddralias <old-keys> <new-keys> <hostname> > record.txt
```

## Example

```sh
regaddralias old.keys new.keys alias.i2p > alias.record
verifyhost "$(cat alias.record)"
```

## Security

Both key files are private. The resulting record contains the public
destinations and two signatures (`oldsig`, `sig`); it is safe to publish.

## Compatibility

Uses the legacy `#olddest=` alias fields accepted by the registration services
and by `verifyhost`.

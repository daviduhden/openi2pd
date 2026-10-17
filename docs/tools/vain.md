# vain

Generate Ed25519 vanity destinations matching a prefix.

## Syntax

```sh
vain [options] <pattern>
```

Options (see `vain --help`): `-t/--threads`, `-s/--signature`,
`-o/--output`, `-r/--reg`, `-m/--multiplymode`, `-h/--help`.

## Example

```sh
vain -t 4 -o vanity.keys example
```

## Performance

Generation is CPU-bound and scales with prefix length. The upstream table gives
approximate times on a 2.7 GHz CPU: 4 chars ~0.4 s, 5 chars ~6 s, 6 chars
1-5 min, 7 chars ~30 min. Results depend on hardware and luck.

## Security

Writes a private destination key file. Use a private directory and keep the
output secret. The search uses threads; cancellation with Ctrl-C may leave the
last in-memory candidate unwritten. Verify the result with `keyinfo`.

## Compatibility

Ed25519 destinations and standard key-file format. No network access.

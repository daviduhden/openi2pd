# vain

Generate Ed25519 vanity destinations matching a prefix.

## Syntax

```sh
vain [options] <pattern>
vain <pattern> [options]
```

Options (see `vain --help`):

| Option | Meaning |
|---|---|
| `-t`, `--threads N` | worker threads, 1 to 1024 (default: online CPUs) |
| `-s`, `--signature TYPE` | signature type; only `EDDSA-SHA512-ED25519` is implemented |
| `-o`, `--output PREFIX` | output path prefix |
| `-r`, `--reg` | treat the pattern as a regular expression |
| `-m`, `--multiplymode` | keep searching after the first match |
| `-h`, `--help` | usage |

The positional pattern may be given before or after the options. Extra
positional arguments are ignored, as before.

## Patterns

Without `-r`, the pattern is an I2P base32 prefix: one to 52 characters from
`a-z` and `2-7`. Other characters can never occur in a destination hash and
are rejected instead of starting a search that cannot succeed. With `-r`, the
expression is matched against the generated base32 text. Historically the
number of generated characters equals the length of the expression text, so
`-r '^abc.*'` (6 characters) is matched against a 6-character address prefix;
this behaviour is preserved. The length is capped at 52 characters, which is
the full address length and also fixes the former buffer overflow for longer
expressions. An expression that requires more than 52 characters cannot match.

Regular expressions are compiled with the C++ standard library. A malformed
expression is reported and exits with status 1.

## Output

With no `-o`, a match is written to `<base32-address>.dat` in the current
directory. With `-o PREFIX`, the historical naming is retained: the first
candidate is `PREFIX0.dat`, and an existing file makes the tool try
`PREFIX1.dat`, `PREFIX2.dat`, and so on. The file is an ordinary I2P
destination key file (`PrivateKeys::ToBuffer` format), **not encrypted**.
Protect it with a private directory and `umask 077`, and verify it with
`keyinfo`.

The generated key format and the hash/nonce search are unchanged from the
upstream tool.

## Example

```sh
vain -t 4 -o vanity.keys example
```

## Performance

Generation is CPU-bound and scales with prefix length. The upstream table gives
approximate times on a 2.7 GHz CPU: 4 chars ~0.4 s, 5 chars ~6 s, 6 chars
1-5 min, 7 chars ~30 min. Results depend on hardware and luck.

## Security and resource handling

- Key material is held in an RAII buffer that is wiped with
  `OPENSSL_cleanse` when the search or a failed write ends. No raw owning
  `new[]`/`delete[]` arrays remain.
- Each worker searches its own key buffer; a winning worker publishes its
  nonce and key index through atomic state and a mutex. Worker threads are
  `std::jthread` objects that are joined before a key file is written, so an
  error while starting threads cannot leave a joinable thread behind.
- The hashed identity prefix is checked to be the 391-byte Ed25519 layout
  before any worker starts, including the certificate/key-type bytes embedded
  in the precomputed final SHA-256 block. The complete generated key file
  (larger than the hashed prefix) is written, with only the four nonce bytes
  patched in. Unsupported signature types are refused instead of being
  searched with an out-of-bounds nonce offset.
- Candidate output paths are checked with `symlink_status()`, so a dangling
  symlink is treated as an existing file instead of being followed. The final
  open is not `O_NOFOLLOW`; run the tool in a directory that other users cannot
  write to.
- A failure to write the output is reported and exits with status 1; it is no
  longer reported as success.
- `-s` is honoured. Previously it was parsed and then silently overwritten
  with Ed25519; other types now print `For a while only ED25519-SHA512` and
  exit with status 1.
- Ctrl-C is not trapped. The default signal action terminates the process
  without running destructors, so key buffers are not wiped in that case and
  the candidate is not written. The writer checks the stream state; an I/O
  error can still leave a truncated file, which should be removed after a
  reported `Can't create file` failure.

## Compatibility

Ed25519 destinations and standard key-file format. No network access.

## Tests

`tools-smoke` checks `vain --help` and the deterministic argument-validation
error paths (thread count, regex syntax, non-base32 prefix, unsupported
signature). No search or key generation is executed. Static checks in this
change were `git diff --check` and documentation review. Dynamic validation
still needed: build on Linux/OpenBSD/Windows, one-character search with a
generated key, `keyinfo` inspection of the result, and a sanitizer run for the
worker path.

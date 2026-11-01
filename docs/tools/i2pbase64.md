<!--
See the LICENSE file at the top of the project tree for copyright
and license details.

openi2pd: i2pbase64.

Documents project documentation.

-->

# i2pbase64

Encode or decode I2P base64 from files or stdin.

## Syntax

```sh
i2pbase64 [-d] [filename]
```

Without `-d`, reads binary and writes base64. With `-d`, reads base64
(ignoring ASCII whitespace) and writes binary. The Windows build switches
stdin/stdout to binary mode so CRLF translation cannot corrupt decoded data.
If no filename is given, stdin is
used; output is always stdout.

`-h` prints the usage synopsis on stdout with status 0. A usage error (unknown
option or more than one filename) prints it on stderr with status 1, so a
failing invocation leaves stdout empty; decode and read/write diagnostics also
go to stderr.

## Example

```sh
printf 'hello\n' | i2pbase64
printf 'aGVsbG8K' | i2pbase64 -d
i2pbase64 keys.dat > keys.b64
```

## Format

The alphabet is I2P's (`A-Z a-z 0-9 - ~`) with `=` padding. Decoding rejects
non-alphabet characters and truncated quartets rather than silently producing
corrupt bytes.

`=` terminates the encoded value: after the padded quartet only ASCII
whitespace may follow. A second encoded value appended to the same stream is
rejected with `invalid base64 input: data after padding` on stderr and status
1, even when the padding and the extra characters arrive in different reads.
A partial quartet that is not preceded by padding is `truncated base64 input`;
any other malformed quartet is `invalid base64 input`. Decoded bytes already
written before a later chunk is rejected stay on stdout, so always check the
exit status.

## Security

Do not paste private keys into shell history or terminal logs. Base64 is an
encoding, not encryption.

## Compatibility

Matches `ByteStreamToBase64`/`Base64ToByteStream` in `libi2pd`; the streaming
decoder buffers quartets across read boundaries.

On Windows the filename is resolved by Win32 rules, where a base name that
is a DOS device name is the device in every directory and with any
extension: `nul`, `con`, `aux`, `prn`, `com1`..`com9` and `lpt1`..`lpt9`.
`i2pbase64 -d nul.b64` therefore reads an empty stream from the null device
and exits 0 with no output, even if a file of that name exists. Pass a
`\\?\`-prefixed absolute path, or a name that is not a device name, when
the input really is such a file. The same rule applies to file redirects in
the tool's regressions, so their fixtures use `nul-byte.b64` and
`nul-byte.bin`.

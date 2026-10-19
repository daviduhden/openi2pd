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

## Security

Do not paste private keys into shell history or terminal logs. Base64 is an
encoding, not encryption.

## Compatibility

Matches `ByteStreamToBase64`/`Base64ToByteStream` in `libi2pd`; the streaming
decoder buffers quartets across read boundaries.

<!--
See the LICENSE file at the top of the project tree for copyright
and license details.

openi2pd: autoconf i2pd.

Documents project documentation.

-->

# autoconf_i2pd

Interactive generator for an `i2pd.conf` configuration file.

## Syntax

```sh
autoconf_i2pd
```

Follow the prompts (language, connectivity mode, paths). The generated file is
printed and can be saved under a chosen name. It is a convenience tool; it does
not start or modify a running router.

## Security

Do not run it as root. Review the generated file before installing it, and do
not include secrets in shared output. The tool only writes the file you name.

## Compatibility

The generated file targets the current option set. The old web-console options
are not generated; local administration uses `[tui]`.

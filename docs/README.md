# Open I2Pd documentation

See the [README](../README.md) for supported platforms and build instructions.
The binary and configuration options retain the `i2pd` name.

- [Router configuration](../contrib/i2pd.conf)
- [Client tunnels](../contrib/tunnels.conf)
- [Tunnel examples](../contrib/tunnels.d)
- [OpenBSD service configuration](../contrib/openbsd)

Use `i2pd --help` to list configuration options. The web console is available
at <http://127.0.0.1:7070/> by default.

For a user process, data is stored in `~/.i2pd` on Linux and OpenBSD. With
`--service`, OpenBSD uses `/var/i2pd`, while Linux uses `/var/lib/i2pd`.
`--datadir` overrides these defaults. Windows uses the application data folder;
Android retains its native application's data directory handling.

The [upstream English manual](https://i2pd.readthedocs.io/en/latest/) covers
shared I2P protocols and configuration. Platform and language support in this
fork follows this repository's README.

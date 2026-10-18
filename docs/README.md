# openi2pd documentation

See the [documentation home](index.md) for supported platforms and build instructions.
`openi2pd` is the product identity; the binary and configuration options retain
the `i2pd` name for compatibility.

## Brand

- [Visual identity overview](branding/README.md)
- [Logo usage](branding/logo-usage.md)
- [Brand colors](branding/colors.md) — canonical palette in
  [assets/branding/colors.md](assets/branding/colors.md)
- [Asset inventory](branding/assets.md)

## Documentation

- [Router configuration](../contrib/i2pd.conf)
- [Client tunnels](../contrib/tunnels.conf)
- [Tunnel examples](../contrib/tunnels.d)
- [OpenBSD service configuration](../contrib/openbsd)
- [Architecture and refactoring report](architecture.md)
- [Architecture decisions (ADR)](architecture/README.md)
- [I2P Java interoperability matrix](INTEROPERABILITY.md)
- [Dependency and minimum-version inventory](dependencies.md)
- [C++23 migration](cpp23-migration.md)
- [CMake and POSIX Make migration](build-system.md)
- [TUI and IPC design](tui-ipc.md)
- [TUI on Windows](TUI-WINDOWS.md)
- [Native torrent console](torrent/architecture.md)
- [Torrent functionality matrix](torrent/console-matrix.md)
- [Torrent security](torrent/security.md)
- [Python-to-Perl migration](development/perl-migration.md)
- [Security audit](security-audit.md)
- [OpenBSD sandbox validation](security/openbsd-validation.md)
- [Privilege separation](security/privilege-separation.md)
- [Internal IPC protocol](security/ipc-protocol.md)
- [Threat model](security/threat-model.md)
- [Process capabilities](security/process-capabilities.md)
- [Android modernization](android-modernization.md)
- [Test and validation plan](testing.md)
- [CI policy and workflow linting](ci.md)
- [Files created, modified and removed](files-changed.md)
- [Attribution and personal-reference audit](attribution-audit.md)
- [Rebranding and inherited identity](REBRANDING.md)
- [Upstream provenance and synchronization](upstream-sync.md)

Use `i2pd --help` to list configuration options. The HTTP administrative
console has been removed. Router status and authorized torrent administration
are available through the local `i2pd-tui` client (ncursesw on Unix, Win32/VT
on Windows); the I2P HTTP proxy on port 4444 is a separate service and remains
available.

For a user process, data is stored in `~/.i2pd` on Linux and OpenBSD. With
`--service`, OpenBSD uses `/var/i2pd`, while Linux uses `/var/lib/i2pd`.
`--datadir` overrides these defaults. Windows uses the application data folder;
The Android app uses an app-specific `open-i2pd` directory.

Automatic CI is suspended during the refactoring; see the README. The
[upstream English manual](https://i2pd.readthedocs.io/en/latest/) covers shared
I2P protocols and configuration. Platform and language support in this fork
follows this repository's README.

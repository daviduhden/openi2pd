# Security documentation

- [Full security audit](../security-audit.md): memory safety, concurrency,
  untrusted input, cryptography, findings and fixes.
- [TUI/IPC threat model](../tui-ipc.md)
- [OpenBSD configuration and sandbox](../../contrib/openbsd/README.md)
- [Testing plan](../testing.md), including sanitizer and fuzz commands.

OpenBSD remains a first-class platform. `pledge`/`unveil` are not disabled by
the TUI integration; the socket path is added to unveil with `rwc` and no new
promise is required.

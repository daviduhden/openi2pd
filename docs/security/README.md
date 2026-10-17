# Security documentation

- [Full security audit](../security-audit.md): memory safety, concurrency,
  untrusted input, cryptography, findings and fixes.
- [Privilege separation](../security/privilege-separation.md)
- [IPC protocol](../security/ipc-protocol.md)
- [Process capabilities](../security/process-capabilities.md)
- [Threat model](../security/threat-model.md)
- [TUI/IPC threat model](../tui-ipc.md)
- [OpenBSD configuration and sandbox](../../contrib/openbsd/README.md)
- [Testing plan](../testing.md), including sanitizer and fuzz commands.

OpenBSD remains a first-class platform. `pledge`/`unveil` are not disabled by
the TUI or log-worker integration; the socket/log paths are added to unveil
with the minimum permissions required.

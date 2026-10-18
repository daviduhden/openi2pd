# Security documentation

- [Full security audit](../security-audit.md): memory safety, concurrency,
  untrusted input, cryptography, findings and fixes.
- [Privilege separation](../security/privilege-separation.md)
- [IPC protocol](../security/ipc-protocol.md)
- [Process capabilities](../security/process-capabilities.md)
- [Threat model](../security/threat-model.md)
- [TUI/IPC threat model](../tui-ipc.md)
- [OpenBSD configuration and sandbox](https://github.com/daviduhden/openi2pd/blob/master/contrib/openbsd/README.md)
- [OpenBSD dynamic validation checklist](openbsd-validation.md)
- [Testing plan](../testing.md), including sanitizer and fuzz commands.

OpenBSD remains a first-class platform. `pledge`/`unveil` are not disabled by
the TUI integration, and the TUI/syslog paths are added to unveil with the
minimum permissions required. The log worker currently cannot be spawned while
the parent sandbox is active; that combination is rejected at startup instead
of being silently weakened. See the validation checklist for what still has to
be proven on a real host.

<!--
See the LICENSE file at the top of the project tree for copyright
and license details.

openi2pd: README.

Documents project documentation.

-->

# Security documentation

- [Full security audit](../security-audit.md): memory safety, concurrency,
  untrusted input, cryptography, findings and fixes.
- [Memory-safety and concurrency audit — 2026-10-18](memory-safety-2026.md):
  confirmed findings, implemented corrections, regressions and residual risks.
- [Privilege separation](../security/privilege-separation.md)
- [IPC protocol](../security/ipc-protocol.md)
- [Process capabilities](../security/process-capabilities.md)
- [Threat model](../security/threat-model.md)
- [TUI/IPC threat model](../tui-ipc.md)
- [OpenBSD configuration and sandbox](https://github.com/daviduhden/openi2pd/blob/main/contrib/openbsd/README.md)
- [OpenBSD dynamic validation checklist](openbsd-validation.md)
- [Windows worker IPC and process policy](windows-worker-ipc.md)
- [Testing plan](../testing.md), including sanitizer and fuzz commands.

OpenBSD remains a first-class platform. `pledge`/`unveil` are not disabled by
the TUI integration, and the TUI/syslog paths are added to unveil with the
minimum permissions required. The supervisor/log pair is spawned before the
runtime sandbox and applies its own policy after `execve`; workers refuse to
run as root. See the validation checklist for what still has to be proven on a
real host; storage/network/crypto/admin backends and full core confinement are
not yet implemented.

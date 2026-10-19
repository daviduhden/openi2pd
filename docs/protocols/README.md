# Protocol documentation

The wire specifications are maintained by the I2P project:

- [I2P specifications](https://i2p.net/en/docs/specs/)
- [Common structures](https://i2p.net/en/docs/specs/common-structures/)
- [I2NP](https://i2p.net/en/docs/specs/i2np/)
- [NTCP2](https://i2p.net/en/docs/specs/ntcp2/)
- [SSU2](https://i2p.net/en/docs/specs/ssu2/)
- [Tunnel creation](https://i2p.net/en/docs/specs/tunnel-creation/)
- [Tunnel message](https://i2p.net/en/docs/specs/tunnel-message/)
- [ECIES-X25519-AEAD-Ratchet](https://i2p.net/en/docs/specs/ecies/)
- [Streaming](https://i2p.net/en/docs/specs/streaming/)
- [Datagrams](https://i2p.net/en/docs/specs/datagrams/)
- [SAM](https://i2p.net/en/docs/specs/samv3/)
- [I2CP](https://i2p.net/en/docs/specs/i2cp/)
- [SU3](https://i2p.net/en/docs/specs/su3/)

The fork-specific comparison with I2P Java, including known gaps and pending
live tests, is in the [I2P Java interoperability audit](../INTEROPERABILITY.md).
The earlier high-level `interop-matrix.md` was a duplicate and was removed.

Protocol formats were not changed during the refactoring. The local TUI
protocol is an administrative channel, not an I2P wire protocol; see
[TUI/IPC](../tui-ipc.md).

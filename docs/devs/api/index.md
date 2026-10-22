<!--
See the LICENSE file at the top of the project tree for copyright
and license details.

openi2pd: index.

Documents project documentation.

-->

# Choosing an API

openi2pd provides the same I2P client-facing protocols as the C++ router while
keeping all administration local and bounded.

| API | Use it for | Reference |
|---|---|---|
| SAM | Language-neutral stream and datagram clients | [SAM](SAM.md) |
| BOB | Legacy bridge API; prefer SAM for new clients | [BOB](BOB.md) |
| I2CP | Java I2P-compatible destination/session control | [I2CP](I2CP.md) |
| I2PControl | Local JSON-RPC administration client | [I2PControl](I2PControl.md) |
| libi2pd | In-process C++23 libraries | [libi2pd](libi2pd.md) |

SAM, BOB, I2CP and I2PControl are optional services. Enable only the services
you need and keep their listeners on loopback unless a separate, reviewed
network boundary exists. The HTTP/SOCKS proxies and torrent client are I2P-only;
there is no browser administration console.

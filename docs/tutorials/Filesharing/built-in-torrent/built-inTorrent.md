<!--
See the LICENSE file at the top of the project tree for copyright
and license details.

openi2pd: built inTorrent.

Documents project documentation.

-->

# openi2pd Built-in Torrent Client

> **Requirements:** compiled in by default unless built with `NO_TORRENTS`.
> The optional Transmission-compatible JSON-RPC surface requires Boost ≥ 1.81
> and is not available on Android. The native ncurses TUI has no browser,
> HTML, CSS or JavaScript dependency.

## 1. Configuration

Add a `torrents` tunnel section to `tunnels.conf`:

```ini
[torrents]
type = torrents
torrentsdir = /path/to/torrents
keys = torrents-keys.dat
trackers = http://atia.i2p/announce,udp://opentracker.dg2.i2p:6969,udp://tracker.insulaocculta.i2p:6969,http://opentracker.skank.i2p/a
dhtfile = torrents.dht
signaturetype = 7
```

The web console is gone. Do not set `web`; that option and the bundled
`torrents/web` assets are no longer part of the project.

### Parameters

| Parameter         | Description |
|-------------------|-------------|
| `torrentsdir`     | Directory for downloads. `.torrent` files placed here are loaded automatically at startup; downloads, `.part` and `.resume` files are written here. The directory must already exist. |
| `keys`            | Destination key file. `transient` by default — set a persistent filename so your peer identity survives restarts. |
| `trackers`        | Comma-separated I2P tracker announce URLs only. Clearnet trackers are not supported. |
| `dht`             | Enable the built-in I2P DHT. Default `true`. |
| `dhtfile`         | File used to persist the DHT routing table. |
| `rpcport`         | Optional local Transmission-compatible JSON-RPC port. Disabled by default (`0`). |
| `rpcaddress`      | Loopback bind address for the optional RPC server. Non-loopback addresses are refused. |
| `rpcpath`         | URL path prefix for the optional RPC endpoint. |
| `rpcauth`         | Enable Basic authentication for the optional RPC endpoint. Configure `rpcuser`/`rpcpass` and keep the listener loopback-only. |

## 2. Managing torrents from the native TUI

Use `i2pd-tui` and open the **Torrents** section. The TUI talks to the daemon
over the local `I2PDTUI/1` UNIX socket; it does not use HTTP, JSON-RPC or a
browser. It can list torrents, show details (files, peers, trackers, session
and DHT state), add magnets or local `.torrent` files, pause/resume,
reannounce, and remove torrents. Removing local data requires an explicit
confirmation.

The TUI never opens peer connections or writes downloads itself; validated
administrative operations are performed by the daemon. The torrent engine
remains I2P-only: trackers, peers, metadata and DHT traffic must use I2P
destinations/streams, with no clearnet fallback or DNS lookup.

You can also drop `.torrent` files into `torrentsdir`; they are picked up at
startup. Only regular `.torrent` files at the top level are scanned.

## 3. Optional Transmission-compatible RPC

The RPC endpoint is intended for compatibility with local Transmission clients,
not as a web administration interface. It is disabled unless `rpcport` is set:

```
http://127.0.0.1:<rpcport>/[<rpcpath>/]rpc
```

- The listener must use a loopback address; non-loopback bind requests are
  rejected.
- Do not expose the RPC port through a public interface or proxy.
- Use `rpcauth` with a non-default password when the local environment is not
  fully trusted. Credentials are not stored in the repository.
- CORS is not a supported administration mechanism; browser cross-origin
  access is not required and is not enabled by default.
- Request bodies and responses are bounded. The same validated engine
  operations are used as the TUI.

Supported methods include `torrent-add` (`metainfo` or `filename`),
`torrent-remove`, `torrent-get`, `torrent-start`, `torrent-stop`,
`torrent-set`, `torrent-reannounce`, `session-get`, `session-stats` and
`dht-stats`. The generated parameter/response reference is in
[Torrents JSON-RPC API](torrents-rpc.md). See
[torrent security](../../../torrent/security.md) for the threat model and any
compatibility limitations.

## 4. Security notes

- Torrent traffic must use I2P. There is no direct TCP/UDP, DNS or clearnet
  tracker fallback.
- `.torrent` paths are sanitized and constrained to `torrentsdir`; symlink and
  path-traversal attacks are rejected.
- The TUI socket is local, permission-restricted and peer-UID checked.
- Do not expose the RPC port to an untrusted network. The web console assets
  were removed intentionally.

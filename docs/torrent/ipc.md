# Torrent control IPC

Status: **source implementation complete; dynamic validation pending.** The
protocol is local, versioned and independent of HTTP/JSON-RPC. This document is the contract for the daemon
`TuiServer` and the `i2pd-tui` client; exact parser/code details are in
`libi2pd/TuiProtocol.h` and `daemon/TuiServer.cpp`.

## Transport and authorization

- UNIX-domain stream socket in the router data directory.
- Socket permissions `0600`, socket path identity checked on shutdown.
- Linux `SO_PEERCRED` / BSD `getpeereid`; peer must have the daemon's effective
  UID.
- Bounded receive/send timeouts; one request at a time per connection.
- Requests are line framed; response payload lengths are explicit and bounded.
- No TCP listener, no browser origin, no wildcard CORS, no remote admin by
  default.

## Framing

```
I2PDTUI/1 <COMMAND> [args]\n
I2PDTUI/1 OK <payload-length>\n<payload>
I2PDTUI/1 ERR <code> <message-length>\n<message>
```

Payload fields are `key=escaped-value\n`; escaping covers backslash, newline
and carriage return. Numeric fields are parsed with `from_chars` and bounded.
Lists have per-request limits. The protocol does not carry file descriptors,
raw pointers or piece payloads.

## Torrent commands

```
TORRENT-TUNNELS
TORRENTS <tunnel-index> [limit]
TORRENT-DETAIL <tunnel-index> <torrent-id>
TORRENT-START <tunnel-index> <torrent-id>
TORRENT-STOP <tunnel-index> <torrent-id>
TORRENT-REANNOUNCE <tunnel-index> <torrent-id>
TORRENT-REMOVE <tunnel-index> <torrent-id> [files]
TORRENT-START-ALL <tunnel-index>
TORRENT-STOP-ALL <tunnel-index>
TORRENT-REANNOUNCE-ALL <tunnel-index>
TORRENT-ADD-MAGNET <tunnel-index> <magnet>
TORRENT-ADD-FILE <tunnel-index> <path>
TORRENT-SESSION <tunnel-index>
TORRENT-DHT <tunnel-index>
```

`magnet` and `path` are the bounded rest of the request line (maximum 2048
bytes), not shell fragments. `tunnel-index` refers to a sorted snapshot of
configured torrents tunnels; IDs are per tunnel. All IDs/indexes must resolve;
unknown values return an error rather than creating state.

## Response fields

- Tunnel list: `torrent.tunnels` (count) and
  `torrent.tunnel.<n>.name/torrents/index`.
- Torrent list: `torrent.<n>.id/name/status/error/hash/magnet/total/have/
  downloaded/uploaded/downRate/upRate/peers/seeders/leechers/percent/eta`.
- Details: `detail.*`, `file.<n>.*`, `peer.<n>.*`, `tracker.<n>.*`.
- Session: `session.downloaded/uploaded/active/paused`.
- DHT: `dht.supported/numNodes/numBuckets/numTorrents/numHeardFrom/
  numFailedPeers`.
- Commands: `result=ok|error`, `message`, `id`, `tunnel`.

No field contains private keys, identity secrets, raw engine structures or
arbitrary filesystem paths. The TUI displays only what the daemon explicitly
publishes.

## Engine-thread boundary

Snapshots and commands are executed on the torrents tunnel's Asio service
thread. The IPC thread waits with a short timeout; it never iterates mutable
`Torrent` or `PeerConnection` containers directly. A timeout returns a bounded
error to the TUI and leaves the engine running.

## Queue and backpressure

Control requests are serialized per client and bounded by the socket timeout.
Snapshots are immutable copies with capped torrent/file/peer/tracker counts.
Large piece data is not sent over this channel. Future worker IPC must add
bounded queues and explicit rejection/backpressure rather than unbounded
buffering.

## Worker separation

The control channel is ready to be mediated by a future network/storage worker
split, but the current daemon remains monolithic. In particular, it does not
yet satisfy the invariant that no process both connects externally and writes
persistent storage. That remains `TORRENT-IPC-003` / `PRIVSEP-006/007` and must
not be claimed as completed until implemented and validated on each platform.

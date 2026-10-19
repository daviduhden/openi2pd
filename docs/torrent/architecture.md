# Native torrent architecture

Status: **source implementation complete; dynamic validation pending.** This
document describes the boundaries and current implementation. It is not a
claim that the multi-process separation is complete.

## Scope

The existing C++23 BitTorrent engine is retained. The former browser console
(`contrib/torrents/web/index.html`, `main.js`, `rpc.js` and its CSS) has been
removed. The native control path is:

```
BitTorrent engine (libi2pd_client/Torrents*)
        │  public typed snapshot/command facade
        ▼
TorrentsTunnel control facade (service-thread snapshots)
        │
        ▼
daemon TuiServer (bounded local UNIX IPC, peer UID check)
        │  I2PDTUI/1 typed line messages
        ▼
i2pd-tui (ncursesw presentation model)
```

There is no JavaScript interpreter, Node/V8/QuickJS dependency, generated
JavaScript, browser renderer or web administration page.

## Layers

| Layer | Responsibility | Do not do |
|---|---|---|
| Engine | Bencode, torrent state, pieces, peers, trackers, DHT, I2P streams | Expose raw pointers/internal structs to the TUI |
| Control facade | Immutable snapshots, validated commands, bounded waits | Read mutable engine state from the IPC thread |
| Persistence | `.torrent`, `.part`, `.resume`, configured torrent directory | Accept arbitrary paths from the TUI |
| TUI IPC | Framed requests/responses, limits, local UID authorization | Dependency on HTTP/JSON-RPC |
| Presentation | ncursesw list/details/dialogs/formatting | Open peer connections or files directly |

The TUI receives only bounded scalar/record data. It never receives keys,
private identity material, file descriptors, raw storage paths, or a pointer
into the engine.

## Engine reuse

The following existing behavior is preserved rather than reimplemented:

- v1/hybrid torrent metadata and info-hash validation.
- Piece scheduling, block requests, SHA-1 verification, `.part` files and
  resume bitfields.
- I2P stream peer connections, I2P PEX/DHT extensions and UDP tracker
  datagrams over I2P.
- Tracker request scheduling, DHT peer discovery and session statistics.
- Transmission-compatible JSON-RPC operations (kept separately optional).

The browser console's presentation logic has been mapped to native
operations: table refresh, progress/status formatting, details, add
file/magnet, pause/resume/reannounce/delete, session stats and DHT stats. The
engine already performs the actual transfers; only browser-specific state and
DOM rendering are discarded.

## Snapshot model

The control facade defines immutable values for torrents, files, peers,
trackers, session counters and DHT counters (see
`libi2pd_client/TorrentsControl.h`). A snapshot is built on the torrents
service thread and returned through a bounded wait so the IPC thread does not
race mutable engine fields. Commands are queued to the same thread and return
an accepted/error result; completion is observed by the next snapshot.

## Process separation (staged)

The current daemon still combines network, storage and control in one process.
The torrent TUI therefore uses a typed control API. Network/control and storage
callbacks run on distinct Asio execution domains, startup scans and all file
access are posted to storage, and the network thread performs only lexical path
checks. However, the requested OS-level invariant ("no process can both connect
to peers and write persistent storage") is **not yet achieved** for torrents.
It depends on the pending process/worker work (`PRIVSEP-006`/`007` and
`TORRENT-IPC-003`).

The intended split is:

1. Network worker: I2P streams/datagrams, peer protocol, piece receipt.
2. Storage worker: allowlisted read/write/rename/delete operations under the
   configured download root, never arbitrary paths.
3. Router core/control worker: torrent session state and validated commands.
4. Administrative endpoint: TUI/IPC only, with bounded queues and backpressure.

Until those workers exist and are tested, the monolithic path must be stated
as a residual risk rather than presented as privilege-separated.

## Non-goals

- Reimplementing the transfer engine from scratch.
- Browser UI compatibility or HTML/JS export.
- Clearnet tracker/peer/DHT fallback.
- TUI direct filesystem or peer access.
- HTTP/JSON-RPC as the TUI transport.

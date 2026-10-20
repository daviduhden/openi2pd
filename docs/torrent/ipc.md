<!--
See the LICENSE file at the top of the project tree for copyright
and license details.

openi2pd: ipc.

Documents project documentation.

-->

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
TORRENT-PAUSE <tunnel-index> <torrent-id>
TORRENT-RESUME <tunnel-index> <torrent-id>
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

`PAUSE` maps to the engine stop path (it announces the stop event and closes
peer connections), while `RESUME` maps to the start path (it clears the stopped
flag and announces the start event). `START`/`STOP` remain available for
compatibility. `magnet` and `path` are the bounded rest of the request line
(maximum 2048 bytes), not shell fragments. `tunnel-index` refers to a sorted snapshot of
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

Network/control callbacks and storage callbacks already use separate Asio
execution domains: startup enumeration, reads, creation, resume and deletion
run on the storage service, while the network thread performs only lexical path
checks and never opens the torrent directory. This is **not** an OS process
boundary. The daemon remains monolithic and does not yet satisfy the invariant
that no process both connects to peers and writes persistent storage. That
remains `TORRENT-IPC-003` / `PRIVSEP-006/007` and must not be claimed as
completed until implemented and validated on each platform.

### Paused torrent tracker administration

`TORRENT-TRACKER-ADD <tunnel> <id> <URL>` and
`TORRENT-TRACKER-REMOVE <tunnel> <id> <URL>` use the same authenticated local
transport and bounded argument parser. The service validates I2P HTTP/UDP
URLs, complete metadata, paused state and quotas. Explicit tracker membership
replaces fallback to common trackers. An empty explicit list remains empty on
reload. Success means the update and immutable persistence job were queued;
it is not a durability acknowledgement. These commands do not create a new
wire or persisted torrent format (outer BEP 12 dictionary only).

### Creation options, progress and cancellation

`TORRENT-CREATE-V1 <tunnel> <pieceBytes> <public|private> <trackerCount>
<tier:URL>... <source>` adds explicit creation options. `pieceBytes=0` selects
automatically; otherwise it must be a supported power of two. Tiers start at
zero, are contiguous and remain bounded along with URL count and line length.
Sources remain immediate children of the authorized root. URL authority is
validated by the engine as I2P HTTP/UDP only. `TORRENT-CREATE-CANCEL <tunnel>`
requests cooperative cancellation of the one outstanding job. Session fields
`creation.bytesDone/totalBytes/filesDone/totalFiles` expose bounded progress.

File-priority and comment operations are `TORRENT-FILE-PRIORITY tunnel id
index priority`, `TORRENT-COMMENTS tunnel id ON|OFF`, `TORRENT-COMMENT-ADD
tunnel id rating owner|text` and `TORRENT-COMMENT-HIDE tunnel id localId`.
Administrative success queues immutable persistence; it is not a durability
acknowledgement. The [worker protocol](storage-worker.md) is a separate private
capability channel, never exposed directly to the terminal or peers.

## Android payload capabilities

The app-private AIDL service accepts only authorized tree capabilities, validated
relative names and numeric file handles. Positional IO is exact and limited to
16 KiB per call, with bounded client waits and no raw descriptor export. JNI
implements the common native payload backend; the native TUI continues to use
only administrative IPC. See [SAF storage](saf-storage.md) for authority, failure
and restart boundaries. All paths are pending manual CI and device validation.

SAF IO now carries operation tokens; oneway cancel/retire bypass the provider IO
lock. Stable local capabilities remap only after actual Binder death. Replies
from stale generations are discarded and writes with uncertain results are not
replayed. Native storage IPC now includes narrow metadata transactions and
nonrecursive promotion/removal operations, documented in storage-worker.md.

Private envelope message types12/13/14 configure/request/reply for fixed router
persistence. The request carries only a fixed file enum plus <=32KiB bytes.
Torrent storage adds read-only bounded metadata reads and explicit exact-name
DHT authorization. These are local additive IPC changes; I2P wire formats and
state serialization remain unchanged.

# Torrents JSON-RPC API (openi2pd)

> **openi2pd fork:** the former browser console and its static assets were
> removed. This page documents the optional, loopback-only,
> Transmission-compatible JSON-RPC surface. The native `i2pd-tui` Torrents view
> is the supported administration path. The RPC is not a web interface.

This document describes the JSON-RPC API implemented by
`libi2pd_client/TorrentsRPC.cpp` (`i2p::torrents::JSONRPCHandler`). It is
compatible in shape with the Transmission RPC, adapted to the torrents tunnel.

## 1. Endpoint

The RPC is attached to a **torrents tunnel**. When configured with a non-zero
`rpcport`, the server listens on `rpcaddress:rpcport` (default
`127.0.0.1:0`, i.e. disabled). Only loopback addresses are accepted.

Routes:

- `POST /<path>/rpc` and `POST /<path>/rpc/` — JSON-RPC calls (paths registered
  per tunnel; `<path>` is the configured `rpcpath`, empty by default).

Transport:

- HTTP/1.1, POST only. `GET`, `HEAD`, `OPTIONS` and other methods return
  `405 Method Not Allowed`.
- `Content-Type: application/json` (parameters such as `charset` may follow the
  media type). Form-encoded bodies and browser CORS preflight are not supported.
- Responses for JSON calls: `application/json; charset=UTF-8`, HTTP 200 on
  parse success (an application error is reported inside the JSON body, not via
  HTTP status). Invalid or oversized bodies are rejected with an HTTP error.
- If basic authentication is configured for a path, POST requests missing a
  valid `Authorization` header get HTTP `401 Unauthorized` with
  `WWW-Authenticate: Basic realm="i2pd torrents RPC"`.

## 2. Request / response envelope

Request (JSON object):

```json
{
  "method": "torrent-get",
  "arguments": { ... },
  "tag": 666
}
```

- `method` (string, required): one of the methods listed [in §5](#methods).
- `arguments` (object, required in practice): method-specific arguments. A missing/invalid
  body yields a `Parse error`.
- `tag` (int64, optional): echoed back unchanged in the response.

Response (JSON object):

```json
{
  "result": "success",
  "arguments": { ... },
  "tag": 666
}
```

- `result` is the string `"success"` on success, otherwise a human-readable error message.
- `arguments` is method-dependent (often an object or empty object).
- `tag` is present only if it was sent and non-zero.
- On parse failure: `{ "result": "Parse error", "arguments": {}, ... }`.

## 3. IDs and "all torrents"

- Torrent ids are integers (`int64`).
- For `torrent-stop`, `torrent-start`, `torrent-reannounce`: if `arguments.ids` is absent or empty,
  the action applies to **all** torrents (server calls `StopAllTorrents()`, `StartAllTorrents()`,
  `ReannounceAllTorrents()` respectively).
- For `torrent-remove`: if `ids` is absent or empty, **all** torrents are removed.
- `ids` may be a single integer or an array of integers.

## 4. Authentication (Basic Auth) {#auth}

Tunnel RPC paths can be configured with HTTP Basic Authentication. `AddTunnel` accepts
optional `auth`, `user`, and `pass` parameters; when `auth` is true, the server stores a
basic authorization string for that path.

- POST requests to an authenticated path must include the `Authorization: Basic <credentials>`
  header; otherwise the server responds with HTTP `401 Unauthorized` and
  `WWW-Authenticate: Basic realm="i2pd torrents RPC"`.
- `GetAuth(path)` returns `(required, authString)` so the session can check credentials.
- The listener remains loopback-only; do not expose the RPC port through a
  proxy or public interface.

## 5. Methods {#methods}

### 5.1 `torrent-add`

Add a torrent from a base64-encoded `.torrent` file or a magnet link.

Arguments:

- `metainfo` (string): base64-encoded torrent file content. Must have a length
  that is a multiple of 4 (standard base64) and is bounded. Exactly one of
  `metainfo` or `filename` must be present; supplying both or neither is an
  error.
- `filename` (string): a bounded `magnet:` URI. `AddMagnet` validates the
  info-hash and uses only I2P tracker/peer destinations; there is no clearnet
  fallback. Exactly one of `metainfo` or `filename` must be present.

Response `arguments`:

- `torrent-added` (object, on new torrent): `{ "id", "hashString", "name" }`
- `torrent-duplicate` (object, if already present): same fields.

Errors: `"invalid or corrupt torrent file"` if the torrent cannot be parsed / added.

### 5.2 `torrent-remove`

Arguments:

- `ids` (int or int[]): torrent ids to remove. If absent/empty → remove **all**.
- `delete-local-data` (bool, default false): also delete local files.

Response `arguments`: empty object.

### 5.3 `torrent-get`

Retrieve torrent data.

Arguments:

- `fields` (array of strings, **required**): list of field names to include. `id` is
  always added by the server regardless of this list.
- `ids` (int or int[], optional): subset of torrents. If absent, or if `ids` is not an
  integer/array (e.g. a string), **all** torrents are returned.
- `format` (string, optional): `"table"` to request tabular output
  (`torrents` is an array whose first element is the header array `["id", ...fields...]`
  and remaining elements are row arrays). Default format is an array of objects.

Response `arguments.torrents`: array of objects (default format) or array of arrays (table).

See [§6 Torrent fields](#torrent-fields).

### 5.4 `torrent-stop`

Arguments:

- `ids` (int or int[], optional): torrents to stop. If absent or empty → stop **all** torrents.

Response `arguments`: empty object.

### 5.5 `torrent-start`

Arguments:

- `ids` (int or int[], optional): torrents to start. If absent or empty → start **all** torrents.

Response `arguments`: empty object.

### 5.6 `torrent-set`

Limited implementation (Transmission-compatible names, but only a subset is handled).

Arguments (currently supported):

- `trackerAdd` (array of strings): tracker announce URLs to add as **common** trackers
  (they apply to all torrents; `AddTracker(announce, true)`). Non-I2P, malformed,
  port-bearing, credential-bearing or duplicate-invalid hosts are rejected with
  an error response; they are never contacted.

Other Transmission keys (`downloadDir`, speed limits, files-wanted, priorities, …) are
ignored by the handler (no error is raised, but nothing changes).

Response `arguments`: empty object.

### 5.7 `torrent-reannounce`

Arguments:

- `ids` (int or int[], optional): torrents to reannounce. If absent or empty → reannounce **all** torrents.

Response `arguments`: empty object. Triggers an immediate tracker announce for the given torrents (or all torrents when `ids` is omitted).

### 5.8 `session-get`

Returns daemon/session metadata. No arguments required (the request must still carry an
`arguments` object, possibly `{}`). `ids`/`fields` are not used.

Response `arguments` is an object with the following keys (values are defaults/"TODO"
where noted):

| Key | Type | Value / notes |
|---|---|---|
| `alt-speed-down` | int | 50 |
| `alt-speed-enabled` | bool | false |
| `alt-speed-time-begin` | int | 540 |
| `alt-speed-time-day` | int | 127 |
| `alt-speed-time-enabled` | bool | false |
| `alt-speed-time-end` | int | 1020 |
| `alt-speed-up` | int | 50 |
| `anti-brute-force-enabled` | bool | false |
| `anti-brute-force-threshold` | int | 100 |
| `blocklist-enabled` | bool | false |
| `blocklist-size` | int | 0 |
| `blocklist-url` | string | `""` |
| `cache-size-mb` | int | 4 |
| `config-dir` | string | i2pd data dir (`i2p::fs::GetUTF8DataDir()`) |
| `default-trackers` | string | `""` |
| `dht-enabled` | bool | `m_Tunnel->SupportsDHT()` |
| `download-dir` | string | torrents directory of the tunnel |
| `download-dir-free-space` | int | 9999999999 (placeholder) |
| `download-queue-enabled` | bool | true (placeholder) |
| `download-queue-size` | int | 5 (placeholder) |
| `encryption` | string | `"required"` |
| `idle-seeding-limit` | int | 30 |
| `idle-seeding-limit-enabled` | bool | false |
| `incomplete-dir` | string | torrents directory |
| `incomplete-dir-enabled` | bool | false |
| `lpd-enabled` | bool | false |
| `peer-limit-global` | int | 200 (placeholder) |
| `peer-limit-per-torrent` | int | 50 (placeholder) |
| `peer-port` | int | 6881 |
| `peer-port-random-on-start` | bool | false |
| `pex-enabled` | bool | true |
| `port-forwarding-enabled` | bool | false |
| `queue-stalled-enabled` | bool | false |
| `queue-stalled-minutes` | int | 30 |
| `rename-partial-files` | bool | true |
| `rpc-version` | int | 17 |
| `rpc-version-minimum` | int | 14 |
| `rpc-version-semver` | string | `"5.3.0"` |
| `script-torrent-added-enabled` | bool | false |
| `script-torrent-added-filename` | string | `""` |
| `script-torrent-done-enabled` | bool | false |
| `script-torrent-done-filename` | string | `""` |
| `script-torrent-done-seeding-enabled` | bool | false |
| `script-torrent-done-seeding-filename` | string | `""` |
| `seed-queue-enabled` | bool | false (placeholder) |
| `seed-queue-size` | int | 10 (placeholder) |
| `seedRatioLimit` | int/float | 2 |
| `seedRatioLimited` | bool | false |
| `speed-limit-down` | int | 100 |
| `speed-limit-down-enabled` | bool | false |
| `speed-limit-up` | int | 100 |
| `speed-limit-up-enabled` | bool | false |
| `start-added-torrents` | bool | true |
| `tcp-enabled` | bool | true |
| `trash-original-torrent-files` | bool | false |
| `units` | object | see [Units](#units) |
| `utp-enabled` | bool | true |
| `version` | string | `"4.0.0 (280ace12f8)"` |

### 5.9 `session-stats`

Aggregate session statistics. No arguments.

Response `arguments`:

| Key | Type | Notes |
|---|---|---|
| `torrentCount` | int | total torrents |
| `pausedTorrentCount` | int | stopped torrents |
| `activeTorrentCount` | int | `torrentCount - pausedTorrentCount` |
| `current-stats` | object | see below |
| `cumulative-stats` | object | same object instance/values (per implementation) |

Stats object:

| Key | Type |
|---|---|
| `downloadedBytes` | size (uint64) summed over torrents: `Torrent::GetDownloaded()` |
| `uploadedBytes` | size (uint64): `GetUploaded()` |
| `downloadSpeed` | size/s (uint64): summed `GetDownloadRate()` |
| `uploadSpeed` | size/s (uint64): summed `GetUploadRate()` |
| `sessionCount` | int (1) |
| `filesAdded` | int (0, TODO) |
| `secondsActive` | int (0, TODO) |

### 5.10 `dht-stats`

Arguments: none.

Response `arguments`:

| Key | Type | Notes |
|---|---|---|
| `supported` | bool | `m_Tunnel->SupportsDHT()` |
| `numNodes` | int | (only if supported) |
| `numBuckets` | int | |
| `numTorrents` | int | |
| `numHeardFrom` | int | |
| `numFailedPeers` | int | |

When DHT is not supported only `supported: false` is returned.

## 6. Torrent fields (torrent-get) {#torrent-fields}

Pass these names in `arguments.fields`. Each is added to the torrent object only if the
server returns a non-null value. `id` is always present. Unknown/invalid fields yield
`null` (skipped).

### Core / progress

| Field | Type | Description |
|---|---|---|
| `id` | int | (always added by server) |
| `name` | string | torrent name |
| `hashString` | string | hex info-hash (SHA-1) |
| `magnetLink` | string | generated magnet link (`magnet:?xt=urn:btih:<hex>&dn=...&tr=...`) |
| `status` | int | see enum below |
| `isFinished` | bool | `Torrent::IsComplete()` |
| `sizeWhenDone` | size | `GetLength()` (same as `totalSize`) |
| `totalSize` | size | `GetLength()` |
| `leftUntilDone` | size | `GetLeft()` |
| `haveValid` | size | `GetLength() - GetLeft()` |
| `percentDone` | float (0..1) | `(length-left)/length`, or 0 if length=0 |
| `metadataPercentComplete` | float | 1.0 if length>0 else 0.0 |
| `downloadedEver` | size | `GetDownloaded()` |
| `uploadedEver` | size | `GetUploaded()` |
| `uploadRatio` | float | uploaded/downloaded (1.0 if nothing downloaded) |
| `rateDownload` | size/s | download rate |
| `rateUpload` | size/s | upload rate |
| `eta` | int (seconds) | estimated seconds remaining, or -2 if not downloading |
| `error` | int | 3 if a local error (`GetError()!=0`) else 0 |
| `errorString` | string | error description (first mapping gives `GetErrorStr()`; a second later entry overwrites duplicate key with `""` — effectively empty in the returned JSON) |
| `isStalled` | bool | false (TODO) |
| `recheckProgress` | float | 0 (TODO) |
| `queuePosition` | int | 1 (TODO) |
| `labels` | array | empty (TODO) |
| `seedRatioMode` | int | 0 (follow global) (TODO) |
| `seedRatioLimit` | int | 0 (TODO) |
| `downloadDir` | string | `""` (TODO → torrents dir) |
| `comment` | string | `""` (TODO) |
| `creator` | string | `""` (TODO) |
| `webseedsSendingToUs` | int | 0 (i2p has no web seeds) |

Status enum (`TorrentStatus`):

| Value | Name |
|---|---|
| 0 | STOPPED |
| 1 | QUEUED TO VERIFY |
| 2 | VERIFYING |
| 3 | QUEUED TO DOWNLOAD |
| 4 | DOWNLOADING |
| 5 | QUEUED TO SEED |
| 6 | SEEDING |

### Peers

| Field | Type | Description |
|---|---|---|
| `peers` | array of objects | see [Peer object](#peer-object) |
| `peersConnected` | int | `GetConnections().size()` |
| `peersGettingFromUs` | int | `GetNumUploadingToPeers()` |
| `peersSendingToUs` | int | `GetNumDownloadingFromPeers()` |

#### §6.1 Peer object (from `GetPeers`) {#peer-object}

| Key | Type | Notes |
|---|---|---|
| `address` | string | first 4 chars of the remote ident hash (base64) |
| `port` | int | 6881 (`TORRENT_PORT`) |
| `identHash` | string | full remote destination ident hash (base64) |
| `clientName` | string | remote torrent client name, or recognized name from peer id |
| `isDownloadingFrom` | bool | server passes `it->IsDownloading()` |
| `isUploadingTo` | bool | `IsUploading()` |
| `rateToClient` | size/s | rate peer→us |
| `rateToPeer` | size/s | rate us→peer |
| `isIncoming` | bool | whether the underlying stream is incoming |
| `clientIsChoked` | bool | `IsChoked()` (we choked the remote) |
| `peerIsChoked` | bool | `IsRemoteChoked()` |
| `clientIsInterested` | bool | `IsInterested()` |
| `peerIsInterested` | bool | `IsRemoteInterested()` |
| `flagStr` | string | a Transmission-style flag string built from various states (D/d/K, U/u/?, I/X/H) |
| `progress` | float (0..1) | ratio of pieces the remote has |

Only established connections are included.

### Pieces

| Field | Type | Description |
|---|---|---|
| `pieces` | string | base64-encoded bitfield (`CreateBitfield()`). Size = number of pieces / 8 bytes (rounded). |
| `pieceCount` | int | `GetNumPieces()` |
| `pieceSize` | size | `GetPieceLength()` |
| `wanted` | array of int | array of `1` with `numFiles` (or 1) entries |
| `priorities` | array of int | array of `0` (numFiles/1 entries) |

### Files

| Field | Type | Description |
|---|---|---|
| `files` | array of objects | see [File object](#file-object) |
| `fileStats` | array of objects | see [File stats object](#file-stats-object) (only includes entries with bytesCompleted; length may differ from `files`) |

#### §6.2 File object {#file-object}

| Key | Type | Notes |
|---|---|---|
| `name` | string | path relative to the torrents directory (`std::filesystem::relative`) |
| `length` | size | file size |
| `bytesCompleted` | size | bytes completed (from `GetFilesCompleted()`) |

#### §6.3 File stats object {#file-stats-object}

| Key | Type |
|---|---|
| `bytesCompleted` | size |
| `priority` | int (0) |
| `wanted` | bool (true) |

### Trackers

| Field | Type | Description |
|---|---|---|
| `trackers` | array of objects | see [Tracker entry](#tracker-entry) (global trackers known to the tunnel where this torrent is registered) |
| `trackerStats` | array of objects | see [Tracker stats entry](#tracker-stats-entry) |

#### §6.4 Tracker entry (quick) {#tracker-entry}

| Key | Type | Notes |
|---|---|---|
| `id` | int | tracker index |
| `announce` | string | announce URL |
| `tier` | int | 0 (i2pd does not model tiers) |

Only trackers for which `torrent->GetNextTrackerRequestTime(i)` is non-zero are included.

#### §6.5 Tracker stats entry {#tracker-stats-entry}

| Key | Type | Notes |
|---|---|---|
| `id` | int | tracker index |
| `announce` | string | announce URL |
| `host` | string | parsed host of the announce URL |
| `announceState` | int | 1 (placeholder) |
| `scrapeState` | int | 0 (placeholder: no scrape) |
| `hasAnnounced` | bool | true (placeholder) |
| `hasScraped` | bool | false |
| `isBackup` | bool | false |
| `downloadCount` | int | -1 |
| `tier` | int | 0 |
| `seederCount` | int | `GetNumSeeders(trackerID)` |
| `leecherCount` | int | `GetNumLeechers(trackerID)`; may be -1/sentinel-ish upstream value |
| `lastAnnouncePeerCount` | int | `GetNumPeers(trackerID)` |
| `lastAnnounceResult` | string | `"Success"` or `GetTrackerError()` |
| `lastAnnounceSucceeded` | bool | true if `GetTrackerError()` empty |
| `lastAnnounceTimedOut` | bool | false (TODO) |
| `nextAnnounceTime` | int (unix seconds) | estimated next announce time (rounded down to seconds) |
| `lastAnnounceTime` | int (seconds since epoch, **not** multiplied) | `GetLastTrackerUpdateTime()` — note the unit mismatch vs. `nextAnnounceTime` |
| `lastAnnounceStartTime` | int | (same as `lastAnnounceTime`, TODO) |
| `lastScrapeTime` | int | 0 |
| `lastScrapeStartTime` | int | 0 |
| `nextScrapeTime` | int | 0 |
| `lastScrapeResult` | string | `""` |
| `lastScrapeTimedOut` | bool | false |
| `lastScrapeSucceeded` | bool | true |

## 7. Units (`session-get` → `units`) {#units}

```json
{
  "memory-bytes": 1024,
  "memory-units": ["KiB","MiB","GiB","TiB"],
  "size-bytes":   1000,
  "size-units":   ["kB","MB","GB","TB"],
  "speed-bytes":  1000,
  "speed-units":  ["kB/s","MB/s","GB/s","TB/s"]
}
```

Use 1000 as the base for sizes and speeds, 1024 for memory.

## 8. Errors and edge cases

- Unknown `method` → `result: "Method not found"`, HTTP 200.
- Invalid JSON → `result: "Parse error"`.
- Missing/wrong `Authorization` on an authenticated path → HTTP `401 Unauthorized`.
- `torrent-add` failures → `result: "invalid or corrupt torrent file"`, HTTP 200.
- `torrent-remove` with no ids removes **all** torrents.
- `torrent-get` with a non-array/non-integer `ids` value falls back to **all** torrents.
- `torrent-stop`/`torrent-start`/`torrent-reannounce` with no `ids` act on **all** torrents.
- `torrent-set` only honors `trackerAdd`; unknown keys are ignored.
- Field names are case-sensitive. Duplicate field keys in the server map: `errorString`
  appears twice; `std::map` keeps the first insertion, so the value is `GetErrorStr()` in practice.
  Rely on `errorString` only as a best-effort message.
- Tracker *list* (`trackers`) is the same for every torrent of a tunnel (global trackers).

## 9. Fork differences from the upstream web RPC

- The browser UI, static HTML/CSS/JavaScript assets and `GET /web/...` route
  are removed. Do not configure a `web` tunnel option; it is not read.
- No wildcard CORS headers and no `OPTIONS` preflight. A browser cannot use
  this endpoint as a cross-origin administration API.
- The listener is loopback-only and the body/metadata sizes are bounded.
- `i2pd-tui` uses the local `I2PDTUI/1` socket, not this HTTP endpoint.
- The same validated engine operations are used by both surfaces.

## 10. Example

```bash
curl -s -X POST http://127.0.0.1:PORT/PATH/rpc \
  -H 'Content-Type: application/json' \
  -d '{"method":"torrent-get","arguments":{"fields":["id","name","status","percentDone","rateDownload","rateUpload"]},"tag":1}'

curl -s -X POST http://127.0.0.1:PORT/PATH/rpc \
  -H 'Content-Type: application/json' \
  -d '{"method":"torrent-add","arguments":{"filename":"magnet:?xt=urn:btih:..."},"tag":2}'

curl -s -X POST http://127.0.0.1:PORT/PATH/rpc \
  -H 'Content-Type: application/json' \
  -d '{"method":"session-stats","arguments":{},"tag":3}'

# With basic auth (when the tunnel RPC path is configured with auth):
curl -s -X POST http://127.0.0.1:PORT/PATH/rpc \
  -H 'Content-Type: application/json' \
  -H 'Authorization: Basic dXNlcjpwYXNz' \
  -d '{"method":"torrent-get","arguments":{"fields":["id","name","status"]},"tag":4}'
```

<!--
See the LICENSE file at the top of the project tree for copyright
and license details.

openi2pd torrent engine checklist.
-->
# Torrent engine and storage checklist

Use with `SKILL.md`. This is a source map, not a parity or isolation claim.

## Layers

| Layer | Files | Rule |
|---|---|---|
| Engine | `Torrents.*`, `TorrentsTunnel.*` | I2P streams/datagrams, trackers, DHT, PEX, pieces |
| Control | `TorrentsControl.*`, `TorrentsTunnel` facade | immutable snapshots, validated commands, service thread |
| Creator | `TorrentsCreator.*` | bounded traversal, streaming SHA-1, canonical v1 info |
| Comments | `TorrentsComments.*` | negotiated, bounded, pseudonymous, private suppression |
| State | `TorrentsState.*` | version 1, atomic no-follow replacement |
| Storage | `TorrentPayloadStorage.h` (implementation in `Torrents.cpp`) | no-follow containment, storage-domain I/O |
| Isolated storage | `TorrentStorageWorker.*`, `privsep/TorrentStorage.*` | numeric handles, bounded IPC, fail closed |
| SAF | `android/app/src/main/java/org/openi2pd/router/TorrentSaf*`, AIDL, JNI | URI authority, exact documents/tree, generation fencing |
| RPC | `TorrentsRPC.*` | optional, separate from TUI; no browser surface |

## Invariants

1. Infohash is the SHA-1 of the original encoded `info` dictionary; do not
   re-encode it for identity.
2. Unknown/duplicate bencode keys and excessive nesting are rejected.
3. Metadata paths are normalized and contained; no absolute path, `..`,
   symlink or hardlink escape.
4. A piece is usable only after verified write; failed write invalidates it and
   publishes the torrent's filesystem error (cleared by a later verified
   write). A stalled request expires at 60 s before the 95 s blanket release;
   progress counts complete+verified+not-saving pieces only.
   `BlockReceived` must report *applied* so duplicates cannot re-hash, rewrite
   or invalidate a complete piece, and download accounting only counts an
   applied block.
5. Private torrents suppress DHT/PEX/comments and public tracker behavior.
6. DHT node records are exactly 54 bytes; tokens/transaction IDs are bounded
   opaque bytes; replies bind NID/destination/port.
7. TUI/Android never receive raw engine pointers, keys, arbitrary paths or
   file descriptors.
8. `isolatedstorage=true` means no parent payload/metadata/DHT write fallback.
9. SAF uses provider URI authority; never manufacture a filesystem path.
10. Deletion is exact-file/empty-directory only; never recursive tree deletion.
11. Removal tombstones revoke an instance permanently. Reserve its infohash
    until serialized disk cleanup succeeds; stale jobs must not mutate a
    replacement. Delete native settings even when retaining payload. Cleanup
    failures retain the reservation until daemon restart; see
    [`local state`](../../../docs/torrent/local-state.md) for backend limits.

## Test map

- Parsing/security: `test-torrent-bencode`, `test-torrents-security`,
  `test-torrents-magnet`, `test-torrents-verify-parity`.
  `test-torrents-security` also prepares removal/re-add queue ordering,
  irreversible persistence revocation and metadata cleanup failure cases.
- Engine/control: `test-torrents-engine-interop`, `test-torrents-concurrency`,
  `test-torrents-priorities`, `test-torrents-comments`,
  `test-torrents-scheduler`, `test-torrents-tracker-dispatch`.
- State/creator: `test-torrents-state`, `test-torrents-creator`,
  `test-torrents-dht-persistence`.
- Storage: `test-torrent-payload-storage`, `test-torrent-storage-worker`,
  `test-torrent-storage-lifecycle`, `test-torrent-storage-openbsd`.
- DHT: `test-torrents-dht-interop`, `test-torrents-dht-protocol`,
  `test-torrents-dht-persistence`, `test-kad-dht`.
- PEX/extensions: `test-torrents-pex`.
- Fuzz: `tests/fuzz/fuzz-torrents-parsers.cpp`, `fuzz-torrents-krpc`,
  `fuzz-torrents-extensions`.
- Android: JVM tests under `android/app/src/test`, instrumentation under
  `android/app/src/androidTest`, native JNI link/APK in CI.

## Compatibility boundaries

- v1 supported; pure v2 unsupported; hybrid only via valid v1.
- I2P PEX uses 32-byte destination hashes, not compact IP peers.
- `ut_comment`: negotiated; age seconds on the wire, local epoch in state;
  ratings 0..5; no authentication implication.
- Trackers must be I2P destinations; no public UDP DHT/DNS fallback.
- Java I2PSnark state format is not adopted; native state is versioned
  separately and original metainfo bytes are preserved.

## CI gates

Linux/OpenBSD/Windows build + torrent CTest, OpenBSD storage tests, Android
Gradle/JNI/APK and device instrumentation, fuzz smoke, and a controlled Java
I2PSnark exchange. All are manual; none is implied by a static review.

## Fuzz seeds

Each torrent fuzzer needs one checked-in seed under
`tests/fuzz/corpus/<target>/` (<=64 KiB, no key/credential material), a
per-target `-max_len`/`-artifact_prefix`, and registration in
`tests/CMakeLists.txt`, `fuzz.yml` and `docs/testing.md`. The manual workflow
runs a global `-max_len=65536`; do not rely on that as the target bound.

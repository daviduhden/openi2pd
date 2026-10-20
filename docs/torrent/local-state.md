<!--
See the LICENSE file at the top of the project tree for copyright
and license details.

openi2pd local torrent administration state.

Specifies the native-only versioned state and bounded persistence boundary.
-->
# Local administration state version 1

Status: implemented by static analysis; pending CI compilation and tests.
This format is local to openi2pd. It is not Java I2PSnark's saved-state format,
a peer message, or a modification to BEP 3 metainfo or the existing resume
bitfield. Original imported `info` bytes and infohashes are preserved.

The file is `.openi2pd-state/<40-lowercase-hex-infohash>.state` below the
configured torrent root. The root-level payload name `.openi2pd-state` is
reserved. Every component is accessed through the existing no-follow helpers;
writes use immutable service-thread snapshots, exclusive staging, file sync,
atomic replacement and parent sync. A parent-sync failure reports uncertain
durability even if the replacement already exists; no truncate fallback is used.

The canonical bencode dictionary contains exactly these fields:

| Key | Type and interpretation |
|---|---|
| `version` | Integer `1`; unsupported versions are rejected. |
| `stopped` | Integer `0` or `1`, the explicit administrative pause state. |
| `priorities` | Byte string with one byte per metainfo file: `0` skip, `1` normal, `2` high. An empty value is encoded as `0:`. |
| `comments` | Nested comment-state dictionary described below. |

The maximum file size is 256 KiB. The decoder builds a candidate and replaces
state only after complete type, length, version and value validation. Priorities
must match the torrent's file count when applied. Peer announces are gated
until loading finishes. Missing state uses defaults; invalid or unreadable state
keeps the torrent paused until explicit administration. Commands issued while
state is loading must not silently replace unread settings.

The nested comment-state dictionary contains `version=1`, `enabled` as an
integer boolean, and a `comments` list. Each entry contains `id` (positive
local integer), `like` (0..5), `local` and `hidden` (integer booleans), `owner`
and `text` (validated UTF-8), and `time` (nonnegative local epoch seconds, not
a wire age). IDs are unique within the store. Size, Unicode and aggregate text
limits are enforced by `TorrentsComments.*`. Invalid future timestamps are
rejected. The peer-wire `timestamp` instead carries an age in seconds.

Comment arrivals update a revision counter. Persistence coalesces writes,
keeps at most one ordinary settings write outstanding per torrent and retries
failed writes at the bounded status cadence. Shutdown queues a final immutable
snapshot before disk-worker draining. Visibility/moderation does not authenticate
an author: labels remain forgeable pseudonyms. Private torrents suppress
comment exchange even if stored settings enabled it previously.

This is the initial native schema, so no earlier native-state migration exists.
Future incompatible versions must specify a migration or explicit rejection.
Full crash recovery, power-loss behavior, callback races and Java/native live
interoperability remain manual CI obligations.

In isolated native storage mode, metainfo, resume and settings mutations use
bounded storage-worker transactions. Removing a torrent deletes its native
settings file in this mode. Disk saves recheck session membership before IO;
queued stale snapshots cannot resurrect settings after the serialized removal.
The version-1 data encoding and torrent infohash remain unchanged.

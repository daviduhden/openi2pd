<!--
See the LICENSE file at the top of the project tree for copyright
and license details.

openi2pd local torrent administration state.

Specifies the native-only versioned state and bounded persistence boundary.
-->
# Local administration state version 1

## Unresolved magnet recovery version 1

The separate `.openi2pd-state/<40-lowercase-hex-infohash>.magnet` record
recovers an unresolved magnet without turning its display name into a payload
path. Its canonical bencode dictionary has exactly five keys:

| Key | Type and interpretation |
|---|---|
| `version` | Integer `1`; other versions are rejected. |
| `hash` | Byte string containing the 40 lowercase hexadecimal infohash characters; it must match the filename. |
| `name` | Nonempty bounded UTF-8 display name without control characters. |
| `stopped` | Integer `0` or `1`, preserving administrative pause. |
| `trackers` | Ordered list of bounded, distinct I2P HTTP/UDP tracker hints without userinfo. |

The record limit is 16 KiB, using the existing control argument and tracker
count limits. Decoding validates a complete candidate before publication;
unknown keys, trailing bytes, invalid types, unsupported versions and unsafe
hints are rejected. This is a new native local format: there is no automatic
import of Java saved state and no change to magnet URIs, BEP 3 metainfo,
resume bitfields or peer messages. Older binaries ignore these records;
downgrading loses unresolved-magnet recovery until a supporting binary reads
them again. Unsupported records are retained for operator recovery.

Startup recovery has a shared budget of 256 loaded records, 4096 directory
entries visited and 64 MiB read across metainfo and magnet records. Records
beyond that budget stay on disk with a warning; directory iteration does not
define a stable admission order. This is a recovery budget, not the IPC
snapshot page size or an engine-wide torrent limit. Existing resolved
metainfo takes precedence over duplicate unresolved hints, while pause
authority must survive a crash during the transition to resolved state.
The policy directory is scanned first. If it is unsafe, unreadable or cannot
be completely scanned within the budget, resolved automatic recovery is
withheld rather than assuming omitted magnets were running. Already admitted
magnet records retain their own validated pause policy.

Writes use immutable owner snapshots and the existing no-follow atomic
replacement boundary. A magnet recovery record may be removed only after
both resolved metainfo and its administrative state are durably written.
When metainfo exists but administrative state is missing, retained magnet
pause policy supplies the fallback; malformed hash-bound recovery policy
must fail closed to paused. Ordinary removal deletes only hash-bound state
for an unresolved magnet, never files derived from its display name.

Duplicate unresolved additions merge tracker hints while preserving an
existing operator pause. The isolated worker accepts only exact hash-bound
`.magnet` paths below the reserved state directory. Android keeps these
metadata records in app-private storage; SAF remains payload authority and
does not convert tree URIs into filesystem paths. Implementation review and
prepared regressions remain static, pending manual CI/device validation.

## Resolved administration state

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
limits are enforced by `TorrentsComments.*`. A timestamp ahead of the local
clock is clamped to `now` instead of rejecting the state (a restored backup or
backwards clock must not lose priorities and the stopped flag); negative or
structurally invalid entries still reject the file atomically. The peer-wire
`timestamp` instead carries an age in seconds.

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
settings file in both isolated and direct modes, including when payload is
retained. The version-1 data encoding and torrent infohash remain unchanged.

Removal irreversibly revokes the old instance through an atomic tombstone.
The tunnel reserves its infohash before removing membership and refuses a new
instance until cleanup finishes on the single disk queue. Already-running IO
may complete before that cleanup; later stale save/promotion jobs are rejected.
Successful cleanup releases the reservation on the service domain. A failed
cleanup logs an error and retains the reservation until daemon restart;
removal acknowledgement is queue acceptance, not a durability acknowledgement.
No network/control callback waits for disk IO to revoke the instance.

Direct metadata deletion walks POSIX parents from the trusted root using
no-follow directory descriptors, unlinks only the exact leaf and synchronizes
its containing directory. A missing file is success; directories at metadata
filenames are rejected. Windows retains the existing path-check/delete race
against a same-UID reparse-point attacker. Isolated failures never fall back to
direct filesystem writes. These controls do not complete global privilege
separation or eliminate the documented payload deletion TOCTOU risks.

Shutdown final snapshots may finish after membership is cleared only while
the tunnel is stopping and the instance has not been removed. Ordinary saves
still require matching live membership. Deterministic queue/removal cases in
`test-torrents-security` are prepared; real shutdown, crash and power-loss
validation remains pending manual CI.

### Initial discovery and durable authority

A newly inserted torrent now has service-owned initial-discovery authority
bound weakly to that exact instance. Metainfo persistence, file preparation,
loaded pause policy and resume verification precede its first tracker/DHT
submission. A newly supplied magnet instead waits for the latest successful
immutable magnet-record write; an older coalesced write cannot authorize a
newer name/hint/pause generation. Validated recovery records retain their
existing startup policy gate. No persisted format changes.

Periodic tracker/DHT work and explicit reannounce use the same initial gate.
The one-shot is consumed before dispatch, with lifetime/stop/removal/error
checks and a private-swarm recheck before DHT. Existing advertisements after
initial admission remain governed by the normal torrent state machine;
this does not impose a new durability wait on every later UI mutation.
Policy, private-swarm and two-generation write controls are prepared in the
existing security target. Compilation, sanitizer and live peer validation
remain pending manual CI on a revision containing the changes.

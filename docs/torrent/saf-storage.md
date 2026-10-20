<!--
See the LICENSE file at the top of the project tree for copyright
and license details.

openi2pd: Android document payload storage.
Explains native piece IO, bounded Binder capabilities and remaining recovery work.
-->
# Android SAF payload storage

Status: implemented by static analysis; pending CI compilation and CI tests.

The torrent details action selects a persistent read/write document tree for a
specific tunnel name and v1 infohash. Restart the router to attach the selection;
existing live handles keep their original authority. The URI stays in Android
preferences and ContentResolver, never becomes a native filesystem path. A
selected but revoked or unavailable tree returns storage errors instead of
falling back to the daemon's internal directory.

`TorrentPayloadStorage` supplies the same bounded Open/Read/Write/Close contract
used by the native payload worker. The JNI adapter calls the app-private
`ITorrentSafStorage` Binder service in `:torrent_storage`. The broker checks the
calling UID and persisted document permissions, creates validated descendants,
and owns all document descriptors. Peers receive neither URIs nor raw file
handles. Metainfo and administrative priorities remain in the existing internal
state directory; the provider holds payload files under their final names.

| Operation | Bounds and authority |
|---|---|
| Authorize tree | Persisted content tree permission; at most 128 roots. |
| Open | Root capability plus relative path; at most 32 components, 8192 characters and 255 characters per component; no traversal, controls, colon or backslash; at most 4096 handles and 1 TiB per file. |
| Read/write | Numeric handle, nonnegative offset and at most 16 KiB; range checked by subtraction. Exact IO loops; writes sync before success. |
| Close/revoke | Release a handle or all handles belonging to one root. |

The backend requires a regular, seekable provider descriptor and positional IO.
Pipes, nonseekable streams and providers refusing direct writes are rejected;
there is no unbounded temporary-copy fallback. Providers remain a trust boundary:
a cloud provider can upload data according to its own behavior. Native BitTorrent
traffic still uses I2P only.

Startup rehashes provider pieces one at a time, including fragments crossing
files, without trusting a cached resume bitfield. Invalid or unreadable pieces
become missing. Subsequent seeding reads rehash bytes before transmission. Pending
writes are excluded from HAVE/bitfield publication, serving and completion;
failed writes invalidate the piece and reacquire a capability on a later attempt.
The document provider, rather than native filesystem rename, controls publication
and deletion. Explicit delete-files removal now closes cached handles and requests deletion
of each exact document capability. The broker checks current permission,
regular-file type, nominal size and document descriptor identity before delete.
It never recursively deletes a selected tree; unrelated files remain.

Before publishing Binder, the broker requests a process-wide TSYNC seccomp filter,
closes inherited Internet sockets and denies socket creation/connections and
selected descriptor-recovery bypasses. Failure refuses binding. This policy needs
Android device validation; the service shares the application UID and is not an
Android isolatedProcess. The protocol/router process still retains persistent
write access: this is not global privilege separation.

Binder waits share one five-second deadline across connection, queue and IO. The
client retains bounded local root/file identities containing immutable URI,
relative path, nominal length and access mode. Remote capabilities are re-created
lazily after a verified broker generation change; stale replies cannot update
these mappings or acknowledge an operation. Cleanup of a dead generation drops
local entries immediately rather than waiting for a connection per file.

Death recipients and lifecycle callbacks fence the old Binder. A disconnected
but still-live broker is retired before rebind. Retries use capped exponential
backoff with one call executor and one queued request; hung calls never create
replacement pools. Bind failure and revoked permission remain errors, with no
native-path fallback. Native handles are now logical capabilities across broker
restart, not raw remote IDs.

AIDL IO includes operation tokens. `cancel` and `retire` are oneway control
transactions independent of the IO lock. Tokens register before acquiring the
IO lock, with at most eight active operations. Provider queries and descriptor
opens use CancellationSignal. Cancellation schedules retirement after 100 ms;
operations also have a 15-second independent watchdog. A provider may ignore
cancellation or block pread/pwrite/fsync; broker process death, rather than
cross-thread descriptor close, fences its local writers. The client waits for
actual death before recovering. It never repeats a write whose result is
ambiguous: the engine invalidates the piece and verifies/re-downloads on a later
attempt. A failed seed read also clears the torrent's seeding completion state,
so it can recover instead of remaining falsely complete.

External document providers remain outside this fence: their remote operations
may finish after client death. Immutable destination authority, exact offsets
and subsequent piece hashes remain necessary; no atomic provider rollback is
claimed. Restoring revoked document permission is still an explicit user action.
Selection and grants survive app death; provider process handles do not.

Prepared instrumentation includes cancellation wire checks and a bounded fault
DocumentsProvider for cooperative cancellation, ignored signals, nonseekable
pipes and revoked documents. Full persisted-grant plus broker death/remapping
runs, ENOSPC/corruption, detailed storage errors and service-negative capability
checks remain pending manual CI/device validation. No Android runtime behavior
is claimed validated.

SAF metainfo export now runs in the same broker. A transient URI write grant
authorizes an exact output descriptor, transferred in16KiB chunks with operation
cancellation and the same deadlines. RouterService does not open the output
document itself. Unsupported random access fails explicitly. Interrupted export
may leave a partial provider document and is never automatically replayed. The
UI exposes connection, cancellation and revoked-permission status.

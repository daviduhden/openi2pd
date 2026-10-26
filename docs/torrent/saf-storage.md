<!--
See the LICENSE file at the top of the project tree for copyright
and license details.

openi2pd: Android document payload storage.
Explains native piece IO, bounded Binder capabilities and remaining recovery work.
-->
# Android SAF payload storage

Status: implemented by static analysis; pending CI compilation and CI tests.

The torrent details action selects a persistent read/write document tree for a
specific tunnel name and v1 infohash. A new selection is resolved on the next
document open; a cached handle that still points at the previous tree fails
closed and is reopened against the new selection. The URI stays in Android
preferences and ContentResolver, never becomes a native filesystem path. Grants
are reference-counted by URI, so removing one torrent does not revoke a tree
still used by another. A selected but revoked or unavailable tree returns storage
errors instead of falling back to the daemon's internal directory.

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
| Open | Root capability plus relative display-name path; at most 32 components, 8192 characters and 255 characters per component; no traversal or controls; `:` and `\` are legal display-name characters; at most 4096 handles and 1 TiB per file. |
| Read/write | Numeric handle, nonnegative offset and at most 16 KiB; range checked by subtraction. Every transfer rechecks the persisted grant, regular-file size and the document ID captured at open; replacement or truncation fails closed. Exact IO loops; writes sync before success. |
| Close/revoke | Release a handle or all handles belonging to one root. |

The backend requires a regular, seekable provider descriptor and positional IO.
A provider that returns a pipe, socket or any other sequential descriptor fails
closed at the initial `lseek`/`fstat` probe before a single byte is transferred:
there is no staging file, temporary copy, read-into-memory fallback or
"sequentially copy then rename" path, so such a document can never become
torrent payload storage. Providers remain a trust boundary: a cloud provider
can upload data according to its own behavior. Native BitTorrent traffic still
uses I2P only.

Startup schedules the same bounded local-verification chain used by POSIX, one
piece per disk job, including fragments crossing files. Zero-length files emit
no fragment and never require a provider document. A valid resume bitfield is
read from private state and still marked pending for hashing; invalid or
unreadable pieces become missing. Initialization does not block the single disk
A persisted tree grant may be read-only when the provider denies write. The
client then stores only the read permission, the broker records the root as
non-writable, and seeding existing documents works while any create/write
operation fails closed at the broker; the status line reports the read-only
mode. Re-selecting a writable tree restores download capability.

worker on a whole-tree rehash. Subsequent seeding reads rehash bytes before transmission. Pending
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

Rebinding is bounded but never permanently disabled by a transient fault. The
client keeps one attempt budget (six attempts with 250 ms→10 s backoff) and a
30 s stability window. A successful `onServiceConnected` always clears the
exhausted latch; the attempt budget itself is restored only when the previous
binding lived for the whole window, so a broker that connects and immediately
dies still exhausts the budget instead of spawning and killing a process
forever. An explicit re-arm (`retryBinding()`, called by daemon start, by an
explicit tree selection and by the status refresh the UI performs) clears the
latch at most once per window. A selection or daemon start begins a fresh
episode. The status text distinguishes an exhausted budget ("reselect the
directory or restart the app") from a retry in progress ("reconnecting"), and a
refused bind or revoked permission never falls back to a native path.

AIDL IO includes operation tokens. `cancel` is scoped to one operation and does
not kill the broker; `retire` remains the explicit process-fence operation.
Tokens register before acquiring the IO lock, with at most eight active
operations. Provider queries and descriptor opens use CancellationSignal.
The AIDL is version 2: `handshake()` returns a CSPRNG broker generation,
`prepareExport` issues a one-shot owner-bound export token consumed by
`openExport(token)`, `retire(generation)` is a one-shot fence, and every root,
handle, operation and token is bound to the caller `(uid, pid)`. Admission
returns typed `0/false/null` instead of throwing; `cancel` is owner-scoped and
`closeFile`/`revokeTree` require the owner. `deleteByRelative` and
`removeEmptyDirectory` cover failed/partial documents. No `ParcelFileDescriptor`
is ever returned over Binder.

Operations also have a two-stage watchdog: at 15 seconds the broker cancels the
provider `CancellationSignal`; only if the same operation is still registered at
30 seconds does it retire the broker process. The deadlines are armed only after
the operation owns the IO lock, i.e. immediately before it can query, open,
read, write, sync or delete; an operation that is still queued behind another
one has not touched a provider, so it is never charged, aborted or fenced. A
single stalled document therefore cannot abort or kill the broker for the
operations that never reached it. `end()` cancels both stages on every
completion and exception path, and the 15 s→30 s ordering, owner `(uid, pid)`
checks and one-shot generation fence are unchanged. A provider may ignore
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

Prepared JVM tests cover export growth/commit size, range and chunk bounds,
document identity, grant reference counting, the nested path policy
(control characters, length limits, `..` and absolute paths), the
`canExtendExisting`/exact-size rules, the watchdog arming predicate and the
framework-free bind-retry budget (exhaustion, stability-gated reset, one
explicit re-arm per window). Prepared C++ tests cover cross-file and zero-length
piece planning plus final-name verification. Prepared instrumentation uses a
bounded fault `DocumentsProvider` with a nested multi-file catalog
(`torrent/part-00001.bin`, `torrent/sub/part-00002.bin`, a zero-length
`torrent/empty.bin`, and a legacy single `payload.bin`) and explicit modes for
cooperative cancellation, ignored signals, a nonseekable pipe, a sequential
socket, revoked permissions, revocation after the first open, deletion after the
first open, same-length replacement (new inode, different bytes) and a refused
write. The cases cover listing and exact-range read/write of nested documents,
the short final block, the zero-length skip, a deleted document failing closed,
explicit document deletion leaving sibling files in place,
replacement detection through device/inode identity, no-seek rejection without
staging, a failed write never being acknowledged as partial success, provider
cancellation wire checks, and the full typed broker surface after a retirement
and rebind. Documents live only in the test app's cache directory and the
catalog is bounded.

Broker-level payload IO across a rebind still needs a persisted tree grant from
a provider both the broker and the test process can reach; the in-tree fixture
is declared by the instrumentation APK under `MANAGE_DOCUMENTS`, so that
sequence, a real slow-provider 15 s/30 s escalation, persisted-grant revocation
in the middle of a transfer, ENOSPC on a real filesystem, detailed storage
errors and service-negative capability checks remain pending manual CI/device
validation. None of these tests has been executed and no Android runtime
behavior is claimed validated.

SAF metainfo export now runs in the same broker. The router persists the
picker's write grant for the duration of the export and releases it afterwards;
the exact output descriptor is transferred in 16 KiB chunks with operation
cancellation and the same deadlines. RouterService does not open the output
document itself. The broker truncates the target to zero before the first write
and allows progressive growth only up to the declared metadata length. A
successful `finishExport` requires exact size; a failed or cancelled export is
truncated back to zero. Unsupported random access fails explicitly. A broker
death or timeout during the final commit is ambiguous and may leave a partial
document; it is never automatically replayed. The UI exposes connection,
cancellation and revoked-permission status.

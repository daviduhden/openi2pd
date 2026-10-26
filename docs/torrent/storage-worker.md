<!--
See the LICENSE file at the top of the project tree for copyright
and license details.

openi2pd isolated torrent payload storage.

Describes the implemented process, capability IPC and remaining isolation work.
-->
# Isolated payload storage

Status: implemented by static analysis; pending CI compilation and tests.

A torrent tunnel can request `isolatedstorage=true` on Linux or OpenBSD. Failure
to start, authorize or sandbox the requested worker prevents that torrent
service from starting. Payload IO failures never fall back to direct filesystem
writes. Windows currently rejects this backend. Android uses the separate
[SAF Binder backend](saf-storage.md), rather than this exec-ed backend.

The root must exist, be owned by the unprivileged service identity and not be
writable by group or others. Linux delegates a directory descriptor over the
private inherited channel and confines reads/writes with Landlock and seccomp.
OpenBSD follows the supervisor bootstrap below. Workers close inherited standard
descriptors and apply their own restrictions before processing payload requests.
These guarantees still require the prepared negative CI tests.

## OpenBSD supervised startup

The daemon prepares a dedicated storage supervisor before runtime pledge. Its
trusted bootstrap specifies the canonical datadir and executable; the supervisor
opens the data root itself and installs an executable-only unveil before its
pledge policy.
No execpromises mechanism or inherited sandbox assumption is used. The core
needs only the additional `recvfd` promise for a returned UNIX socket, retaining
its existing runtime restrictions on process creation.

A launch request carries a validated relative directory and expected device/inode
identity. The supervisor forks/execs a worker with the channel at fd3 and the
immutable broker root at fd4. The worker walks the relative path with
`openat/O_NOFOLLOW` **before** applying its policy, verifies ownership and the
expected device/inode, checks that the configured absolute path still resolves
to that capability, then replaces fd4 with the delegated subdirectory and
applies its storage-only pledge/unveil. Only after that ACK does the supervisor
return the socket channel to the daemon. The broker itself does not call
`openat()` after pledge and does not need `rpath`.

The supervisor has no Internet or persistent-write authority. It enforces a
replenished one-minute launch window (32 launches); live workers belong to the
core/channel lifetime after handoff. It disowns a worker after handing its
channel to the core, so a broker exit cannot SIGTERM/SIGKILL an in-flight
`fsync`; worker replacement is an explicit
Start after Stop/failure, without replaying writes. Broker failure requires a
daemon restart. Roots outside the initially authorized datadir are rejected;
malformed or unauthorized requests terminate the broker and its children. The
existing imsg adapter remains separate; this path uses typed UNIX seqpacket IPC.

## Typed operations

The `IP2P` envelope is version 5 (older frames remain readable and are rejected per-type when their minimum version is newer) and uses
typed local message types: `ConfigureTorrentStorage=7`,
`TorrentStorageRequest=8`, `TorrentStorageReply=9`,
`SpawnTorrentStorage=10`, `TorrentStorageSupervisorReply=11`,
`ConfigureTorrentStorageSupervised=16` and the shared typed `Error=15`. These are not I2P wire messages. Linux configuration carries
one root descriptor and its bounded administrative path; OpenBSD inherits the
root at exec and uses descriptor-free configuration. Later messages must
not carry descriptors. Request IDs are monotonically increasing; the worker
rejects a duplicate/out-of-order request without executing it, and responses
must match the expected type and ID. Operation failures use a typed `Error`
reply with a bounded detail string; success uses the operation-specific reply.

| Operation | Authority and bounds |
|---|---|
| Register | Validated root-relative path, nominal length <=1 TiB, explicit create flag. At most 4096 open handles. |
| Read | Numeric handle, offset and length <=16 KiB; exact length required. Read registration creates nothing. |
| Write | Writable numeric handle, offset and <=16 KiB bytes; bounded by registered file length, durable acknowledgement after sync. |
| Release | Closes one numeric handle. |
| Promote | Root-relative staging/final paths and exact nominal length; regular owned files, no-clobber link/unlink with same-inode interruption recovery. |
| Remove | Exact-length owned regular file, or an empty directory; no recursive deletion. |
| Metadata begin/chunk/commit/abort | At most 8 MiB; sequential <=16 KiB chunks, fsync/atomic replace/parent sync. Only root `.torrent`/`.resume` or `.openi2pd-state/<40-lowercase-hex>.state`. |
| Metadata remove | Same narrow administrative namespace, missing files are idempotent success. |
| Ping / Shutdown | Empty payload only. Ping proves liveness; Shutdown is the bounded stop request. `MetadataAbort` is the explicit cancellation verb for an in-progress metadata transaction. |

Registration walks components without following symlinks and rejects special
files, hardlinks and ownership violations. A read-only registration requires
an existing exact-length file and cannot create directories, resize or write.
Create/write registration preserves existing bytes and can extend a sparse
payload. Peer-originated messages never contain these administrative paths.
`TorrentFile` retains the storage-root identity separately from its per-torrent
root so multi-file offsets cannot resolve into a different torrent directory.

One client transaction is in flight per worker, with five-second bounded send
and receive waits. Timeout or malformed framing poisons the channel and stops
the child. Full piece buffers remain in the engine; transfers are chunked into
bounded IPC blocks. Existing SHA-1 piece verification is unchanged.

## Explicit remaining boundary

This is a real payload IO process, not complete privilege separation. Payload allocation/promotion/removal, metainfo, resume and administrative state
writes now use the worker in isolated mode. Router and DHT persistence still have
parent-side operations. Native reads and path inspection also remain in the parent. The daemon retains persistent-write authority. Networking/session
and verification workers, platform-complete backends and removal of all remaining
parent writes are required before claiming that no process combines networking
with persistent writes. The default in-process mode remains available while
this integration is pending CI; it is not described as isolated.

The delegated root is per tunnel, not per torrent. The opt-in worker limits
files to 1 TiB and open handles to 4096; larger engine-supported torrents can
fail registration and must not be described as supported by this backend.

Metadata transactions have a 30-second client budget checked between bounded
exchanges; failure aborts staging and never switches to parent writes. Eight
exclusive temporary slots in each metadata directory bound crash-orphan space
to 64 MiB per directory. Normal abort/disconnect cleans staging; repeated
SIGKILL can exhaust the slots and requires explicit orphan cleanup. Existing
unknown slots are never overwritten. Payload deletion iterates only metainfo
files then removes empty directories, preserving unrelated contents.

## DHT persistence

Isolated tunnels now attach DHT load/save callbacks to a worker authorized for
the existing datadir/torrents root. Flat54-byte NodeInfo records and filenames
are preserved, with an8MiB bound and rejection of truncated records before state
mutation. ReadMetadata pins a read-only regular-file handle and reads16KiB
blocks with a transaction budget. Default `.dht` names and up to16 exact
administratively authorized legacy flat names are supported. Authorization
never grants directories or internal staging/state names. Delegated failures
have no parent writer fallback. Nested or external administrative DHT paths
remain outside this flat capability and are rejected explicitly.

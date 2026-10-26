<!--
See the LICENSE file at the top of the project tree for copyright
and license details.

openi2pd: ipc protocol.

Documents project documentation.

-->

# Internal IPC protocol

## Transport

- Anonymous `socketpair(AF_UNIX, SOCK_SEQPACKET)` between related processes.
- `SOCK_CLOEXEC` is applied; if the kernel rejects the flag combination, the
  code retries without it and sets `FD_CLOEXEC` explicitly. A failure to set
  `FD_CLOEXEC` is fatal rather than leaking a descriptor across `exec`.
- No TCP, no globally named UNIX socket for internal worker IPC.
- The core starts a supervisor process (`--privsep-worker supervisor <fd>`),
  which owns the log worker and forwards log records. The log worker itself is
  still started as `--privsep-worker log <fd>`.
- Each process receives its channel descriptor as argument `3`, not through an
  environment variable. It verifies that the descriptor is an inherited
  `SOCK_SEQPACKET` socket (`fstat`/`S_ISSOCK` and `SO_TYPE`) and rejects any
  other descriptor number, so a manual invocation cannot downgrade the
  transport to a stream/regular file or alias the inherited root at fd 4.
- Peer identity is checked with `getpeereid` (OpenBSD/BSD) or, on Linux, with
  a per-message `SCM_CREDENTIALS` expectation. `SO_PEERCRED` reports the
  credentials captured when the socket was **created**; after a root core drops
  a worker to `privsep.user`, comparing every frame against that record rejects
  the worker's own replies. Production channels therefore carry an explicit
  `PeerExpectation`: the core requires the worker uid/gid and the direct child
  pid, a worker requires its own uid (or uid 0 for the delegating root core),
  and an adopted broker socket requires the worker uid. The caller that created
  the socketpair and forked the child is the only party that knows the
  topology, so it sets the expectation. A record with a mismatched or missing
  credential is rejected. Cross-UID execution is not yet exercised by an
  automated test (it requires a second UID); the root-gated case is prepared.
- Every operation has a timeout and bounded message size.
- A spawned worker is bound to its spawner by a one-shot **25-byte identity
  record** on descriptor 5: `[role u8 | nonce 16 | uid u32 BE | gid u32 BE]`.
  The forked child writes it after its credential drop. The worker requires the
  record's role to match `argv`, its uid/gid to match `geteuid()/getegid()`,
  a non-zero uid and exactly one record (a trailing byte or a still-open writer
  is rejected), then closes fd 5 before reading any frame. The nonce is never
  logged or formatted.
- The Windows message-mode pipe has no descriptor channel, so the spawn proof
  is a typed `WorkerHello` frame (version 5): `u8 role | u8 nonce length (32) |
  nonce`. The worker connects with `ConnectBound` to the spawned parent
  PID/session, sends `WorkerHello`, and the server consumes it with
  `AcceptBound` after impersonating the client and checking the user SID,
  PID/session, AppContainer SID (when the role uses one) and the nonce. Only
  then is the channel's role bound and the registry applied.
- Descriptor contract for every worker: 3 = channel, 4 = inherited storage root
  (only for `*-inherited` roles), 5 = one-shot identity record, 0/1/2 =
  `/dev/null`, >=6 closed. A role without a root closes fd 4 explicitly; stdio
  is pinned even for a directly invoked worker so a terminal, rc.d log pipe or
  socket-activation descriptor is never writable.
- A channel peer must have the worker's own uid or, for an unprivileged worker,
  uid 0 (the root core that delegates the role). The channel is an anonymous
  `socketpair`, never a pathname socket, so a same-uid sibling cannot connect
  to it; the hello nonce is the spawner binding and `getpeereid` is the
  identity check.

## Frame

```
0                4        6        8                 12              16
+----------------+--------+--------+-----------------+---------------+
| magic "IP2P"   | ver=5  | type   | request-id      | payload-len    |
+----------------+--------+--------+-----------------+---------------+
| payload (0..65536-16 bytes)                                      |
+-------------------------------------------------------------------+
```

All integers are network byte order. The header is encoded field-by-field; no
C++ object memory is transmitted. `Channel::Send` builds the 16-byte header and
passes the caller-owned payload as a second iovec, so the hot path does not
concatenate a second copy of every frame. `Encode()` remains the contiguous
form used by tests and the `imsg` adapter.

Limits:

- Header: 16 bytes.
- Total frame: 64 KiB (65536 bytes); payload at most 65520 bytes.
- Text field: 8 KiB (8192 bytes). Path/error fields have explicit smaller
  bounds (4096 bytes / 256 bytes).
- Log text is truncated at the text bound because it is a diagnostic string.
  Path fields use a fallible encoder: an over-long path or embedded NUL is
  rejected, never silently shortened to a different filesystem object.
  Decoding a declared text length above the bound, or beyond the remaining
  payload, fails with `BadField`.
- A declared frame length that does not exactly match the `SOCK_SEQPACKET`
  record fails with `Malformed`; a declared length above the payload limit
  fails with `TooLarge`. Truncated datagrams (`MSG_TRUNC`) are rejected.
- Log text is additionally sanitized (strict UTF-8 walk; C0/DEL/C1 including
  U+009B neutralised; invalid sequences replaced) by the worker.

## Message registry

`privsep/Protocol.h` owns one `MessageSpec` row per type:
`{type, minVersion, direction, receiving roles, payload ceiling, descriptor
policy}`. Encoders, decoders and `Channel` all read it, and a compile-time
`ValidateMessageRegistry()` check requires a contiguous 1..N table with a
per-type ceiling below the frame maximum and a version inside the supported
range. Adding a type without a deliberate role/limit/descriptor decision does
not compile.

- `Encode` refuses an unknown type and a payload above that type's ceiling.
- `Decode` refuses an unknown type, a type carried in a version older than its
  `minVersion` (no downgrade reinterpretation), and a payload above the
  per-type ceiling.
- `AllowsOperation(role, type)` is derived from the same table: a worker role
  can only receive request-direction types and the requesting side can only
  receive reply types. The hand-written allowlist no longer exists.
- `DescriptorPolicyFor` is the only authority for whether a frame may carry a
  descriptor and of which kind.

## Message types

| Type | Direction | Payload |
|---|---|---|
| `ConfigureLog` | core → supervisor → log worker | path mode: `u8 syslog`, `u32 path-len + path`, `u32 tag-len + tag` |
| `ConfigureLogFd` | direct test/legacy path only | same payload, plus exactly one `SCM_RIGHTS` file descriptor for a file sink |
| `LogRecord` | core → supervisor → log worker | `u8 level`, `u64 timestamp`, `u32 text-len + text` |
| `Shutdown` | parent → worker | empty; request-id echoed |
| `Ping` / `Pong` | either | empty; request-id echoed. Used as the bounded configure/readiness handshake. |
| `Error` | worker/broker → caller | `u16 offending-type`, `u8 ErrorCode`, bounded detail string; no descriptor |
| `ConfigureTorrentStorage`, `SpawnTorrentStorage`, `ConfigureRouterStorage` | typed storage setup | see `docs/torrent/storage-worker.md`; no generic path/write operation |
| `TorrentStorageRequest` / `Reply`, `RouterStorageRequest` / `Reply` | typed storage operations | bounded handle/path request and status reply |
| `ConfigureDataStorage`, `DataStorageRequest`, `DataStorageReply` | core → data worker | canonical artifact name + bounded bytes; see `docs/security/process-capabilities.md` |
| `ConfigureTorrentStorageSupervised` | core → torrent-storage supervisor | typed supervised-storage setup; see `docs/torrent/storage-worker.md` |
| `WorkerHello` | worker → supervisor/server (v5) | `u8 role`, `u8 nonce-len`, nonce (32 bytes on Windows); spawn proof |
| `Cancel` | core → storage worker (v4) | `u32 request-id`; advisory, no reply, never a rollback |

Per-type payload ceilings come from the single `MessageSpec` registry
(`privsep/Protocol.h`), not from ad-hoc checks: the 64 KiB frame limit is an
upper bound, while frames such as `ConfigureLog`, `LogRecord`, data-storage and
router/torrent storage requests have smaller declared ceilings. A frame above
its type ceiling is `TooLarge` even when it fits the 64 KiB transport bound.
Descriptor policy per type: `None` for every control/log/storage-request frame;
`RequiredFile` for `ConfigureLogFd`; `OptionalDirectory` for the three configure
frames that may carry a delegated root; `OptionalSocket` for
`TorrentStorageSupervisorReply` (the broker acknowledges without a descriptor,
the supervisor hands one accepted worker socket to the core, which still
requires it before adoption). A received descriptor is `FD_CLOEXEC` and is
closed on every rejection path.

The production supervisor path forwards `ConfigureLog`; the **log worker**
owns the file capability and opens the configured path with
`O_CLOEXEC|O_NOFOLLOW` and `0600`. The supervisor never opens the log file, so
it has neither network nor persistent-write capability. `ConfigureLogFd`
remains for direct/legacy tests: the sender pre-opens the file, transfers one
descriptor with `SCM_RIGHTS`, and retains no copy. The supervisor rejects an
unexpected descriptor.

A log worker rejects `LogRecord` before `ConfigureLog`, rejects unknown types
and malformed/oversized fields. `Ping` and `Shutdown` are accepted before
configuration so a supervisor can probe or stop a freshly spawned worker.
`ConfigureLog` is accepted exactly once; a duplicate is a bounded typed
`BadField` refusal, not a teardown, because the original sink still belongs to
its owner. `path` and `tag` containing an embedded NUL are rejected so the
worker cannot open a different path than the parent encoded. A pre-opened log
descriptor is additionally bound to the configured path by dev/inode identity,
owner and link count, so a sender cannot substitute an arbitrary same-UID
writable file for the log capability.

After sending configuration, the core/supervisor sends a bounded `Ping` and
waits for the matching `Pong`. Configuration success therefore means the
worker decoded the frame, applied its policy, attached the sink and is live,
not merely that a datagram was queued. A failed handshake closes the channel
and fails startup; it never falls back to a local log file.

The production protocol carries logging plus the opt-in router-storage and
torrent payload/state operations. There is no network worker protocol and no
generic file-write API: storage requests are fixed-name or bounded relative-path
operations under a delegated root. Descriptor passing is operation-specific
(`ConfigureLogFd`, the Linux torrent root, and the OpenBSD inherited root fd).
The OpenBSD/Linux network and crypto role policies remain preparation, not a
claim that those backends are migrated.

## Enforcement and recoverability

`Channel` has two explicit policies:

- `Transport` (default, used by request-issuing clients and the raw transport
  tests): framing, size, credentials and the single-descriptor rule only.
- `Endpoint` (every serving worker and the supervisor/broker): additionally
  enforces the per-type descriptor policy and answers an *attributable* frame
  fault with a typed `Error` correlated by request id, closing any descriptor,
  instead of dropping the connection. A record is only attributable when its
  magic, version and declared length are consistent, so a corrupt header can
  never choose a request id. The tolerated faults are bounded
  (`MAX_PROTOCOL_VIOLATIONS = 8`); exceeding the budget is terminal.
  Credential and ancillary-data failures stay fatal in both modes.

Timeouts are absolute deadlines in both directions. A receive timeout returns
an empty result and leaves the channel open on every backend; only a broken
pipe, an oversize record, a corrupt/credential fault or an exhausted violation
budget closes it. Linux/OpenBSD `Channel` and the Windows message-mode pipe
implement the same contract (the Windows path previously closed the pipe on
`ERROR_TIMEOUT`, which terminated any polling worker after one idle period).

Backpressure is credit- and deadline-based rather than an unbounded queue: one
reader per endpoint, single-flight storage requests, a bounded fault budget and
the kernel's `SOCK_SEQPACKET` buffer are the only buffering. A `Send` that
cannot queue the record before its absolute deadline fails (`send timeout`) and
the caller must not replay a mutation; there is no user-space backlog that a
peer can grow without bound.

`Cancel` is advisory because the storage workers are single-flight and
synchronous: a cancel observed after the mutation started cannot roll it back.
The client never replays such a request, drains the outstanding reply to keep
the channel in sync and reports the operation as failed (`Busy`). The worker
suppresses a matching request that has not run yet. `Busy` also covers
request-id exhaustion and a busy single-flight slot; neither poisons the
channel.

## Authorization

- Every server dispatch checks `AllowsOperation(EndpointRole, MessageType)`
  before its switch. The answer comes from the message registry; a type valid
  for one endpoint (for example `LogRecord`) is a bounded typed
  `NotAuthorized` refusal on every other endpoint, never an accepted frame.
- A wrong-direction frame cannot be injected: worker roles are only listed as
  receivers of request-direction types and the requesting side only of reply
  types. `Error`, `Pong` and `TorrentStorageSupervisorReply` are the only
  replies a non-core endpoint may receive (supervisor and broker), because
  they issue their own requests.
- Log worker only accepts log/configure/shutdown/ping operations. Router,
  torrent and data storage accept their own setup/request/shutdown/ping types
  plus the advisory `Cancel`. The storage broker only accepts
  configure/spawn/shutdown/ping.
- `Shutdown` and `Ping` payloads must be empty. A descriptor on `Ping`,
  `Shutdown` or a log record is rejected.
- Router storage accepts four fixed filenames; torrent storage accepts only
  bounded paths beneath its delegated root. No generic `write_file(path,data)`
  API, secret-read operation or command execution operation exists.
- Storage requests must use strictly increasing request IDs. A duplicate,
  replayed or out-of-order request is rejected without re-executing a mutation.
  The OpenBSD broker similarly drops stale spawn IDs and reports per-request
  path/capacity failures as typed `Error` replies without killing live workers.
- Descriptor passing is operation-specific and registry-declared. An endpoint
  in `Endpoint` enforcement mode closes and answers with `BadField` any
  descriptor on a type whose policy is `None`, any descriptor of the wrong
  kind, and any missing descriptor a `Required*` policy demands. `ConfigureLogFd`
  accepts exactly one regular file and binds it to the configured path by
  identity; torrent storage accepts one validated root descriptor (or inherits
  it at fd 4 on OpenBSD). Adopted worker channels are
  re-validated as `AF_UNIX/SOCK_SEQPACKET` (where `SO_DOMAIN` exists), not
  listening, with the expected peer UID and `FD_CLOEXEC`; Linux enables
  `SO_PASSCRED` before use. There is no generic descriptor-passing API.
- `WindowsServer::AcceptBound()` binds the accepted pipe to the spawned
  worker PID/session; SID equality alone remains only for legacy/tests.
  Windows daemon wiring of the bound accept path is still pending.

## Failure behavior

- `send`/`recv` failure closes the channel. A peer close is reported as an
  error, distinct from a receive timeout, so the worker exits instead of
  polling a closed socket forever.
- The router drops log records if the worker channel closes; it never falls
  back to opening the log file in the core process.
- A serving endpoint answers an attributable frame/descriptor fault with a
  typed `Error` and continues; after `MAX_PROTOCOL_VIOLATIONS` (8) tolerated
  faults, or on any credential/ancillary failure, the worker exits with
  failure.
- The supervisor process restarts a dead log worker with 250 ms exponential
  backoff capped at 30 s, eight consecutive attempts and a 60 s stable-period
  reset. The previous child is reaped before a replacement is spawned, so
  there is at most one configured log worker per supervisor.
- When the supervisor restart limit is reached, it exits. The core keeps a
  bounded 8192-record logger queue and drops records after that; it never opens
  the log file. Opt-in router and torrent storage workers exist, but the core
  still combines network and most persistent writes; global separation is not
  claimed.
- Liveness detection is exit-, handshake- and channel-based. Configuration is
  acknowledged by the bounded `Ping`/`Pong` exchange; steady-state log traffic
  does not interleave a watchdog on the same channel. A worker that is alive
  but wedged (for example blocked in a filesystem write) is not killed or
  restarted; the sink send timeout bounds the logger stall and then the
  channel is closed and subsequent records are counted as dropped.
- A zero-length `SOCK_SEQPACKET` record is consumed and is not treated as
  peer EOF, but it must satisfy the same Linux `SCM_CREDENTIALS` expectation as
  a data frame. A socket handed over by a broker is rejected at adoption if a
  record is already queued, so the broker cannot pre-load a frame whose kernel
  credentials would impersonate the worker. More than eight consecutive empty records in one receive call
  is a flood error. Only a hangup/error poll event is terminal.
- The storage broker returns `Error{Busy}`/`Error{NotAuthorized}`/`Error{
  Internal}` for capacity, path-replacement and worker-spawn failures. These
  do not destroy already-granted storage channels. Malformed frames,
  unauthenticated peers and descriptor misuse remain fatal.

## Versioning

`PROTOCOL_VERSION = 5`; `MIN_PROTOCOL_VERSION = 1`. Version 2 adds the typed
`Error` message and the shared endpoint allowlist, version 3 the data-storage
frame family, version 4 the message registry plus the advisory `Cancel`, and
version 5 the Windows `WorkerHello` spawn proof.
Versioning is per type: a frame carrying a type whose `minVersion` is newer
than its header is rejected with `UnsupportedVersion` instead of being
interpreted with older semantics, and a header outside `1..5` is rejected.
There is no downgrade or "best effort" mode. New senders emit version 5.
The OpenBSD `imsg` adapter cannot carry a frame above `MAX_IMSGSIZE`
(16 KiB), so the 32 KiB `RouterStorageRequest` ceiling is a POSIX
`SOCK_SEQPACKET`/Windows capability; a future imsg backend must chunk or the
operation must stay below that bound.

## OpenBSD native adapter

`privsep/OpenBSDImsg.{h,cpp}` wraps the OpenBSD `imsg`/`imsgbuf` API
(`libutil`) and carries the same versioned frame as a payload. It enables
`imsgbuf_allow_fdpass` for exactly one `SCM_RIGHTS` descriptor, applies
bounded poll deadlines and validates peer identity with `getpeereid(2)`.
`ImsgChannel::AdoptValidated()` checks `S_ISSOCK`, the stream/seqpacket type,
`FD_CLOEXEC` and `getpeereid(2)` **before** `imsgbuf_allow_fdpass()`; the
adapter is not production-wired yet. `MAX_IMSGSIZE` is 16 KiB on OpenBSD; the
adapter rejects larger frames, while the existing 8 KiB log-text bound fits
with room to spare. Bulk storage blocks therefore remain on the portable
SOCK_SEQPACKET channel. A send timeout closes the imsg channel instead of
leaving a queued mutation to be retried as a second frame. The receive loop
drains every complete message already buffered by `imsgbuf_read()` before it
polls again, so back-to-back frames cannot be stranded. The adapter is
exercised by `test-privsep-openbsd-imsg` in the manual OpenBSD job. The production supervisor/log path continues to use the portable
`SOCK_SEQPACKET` channel so Linux and OpenBSD share the same framing while
the remaining backends are migrated; the adapter is not a weaker fallback.

## Platform mechanism comparison

| Requirement | OpenBSD | Linux | Windows |
|---|---|---|---|
| Local transport | UNIX `SOCK_SEQPACKET` (implemented); native `imsg` adapter prepared/tested in CI | UNIX `SOCK_SEQPACKET` (implemented) | Local message-mode Named Pipe (`PIPE_TYPE_MESSAGE`, `PIPE_REJECT_REMOTE_CLIENTS`) implemented as primitives; random per-spawn names, server-authenticated connect, listener re-arm and cancel/drain hardening; worker integration pending |
| Peer identity | `getpeereid(3)` at adoption (implemented) | `SO_PEERCRED` at adoption plus a spawner-set per-frame `SCM_CREDENTIALS` expectation (uid/gid/pid, root-parent allowance) | Client PID/session + token user SID via `GetNamedPipeClient*` (implemented); no impersonation |
| Message boundaries | `SOCK_SEQPACKET`; imsg adapter preserves versioned frame | `SOCK_SEQPACKET` | One message-mode pipe message per versioned frame |
| Descriptor transfer | `SCM_RIGHTS` for `ConfigureLogFd`; torrent-storage broker receives its root as inherited fd 4 (directories are not sent through `sendfd` after pledge) | `SCM_RIGHTS` for `ConfigureLogFd`; Linux router storage passes a validated root descriptor | No inheritance/`DuplicateHandle`; worker reconnects by local pipe name |
| Process supervision | `waitpid`, signals; separate supervisor process for log worker (implemented) | eager `pidfd` preferred, `waitpid` fallback; separate supervisor process (implemented) | Restricted token + Job Object + mitigation attributes implemented as primitives; supervisor integration pending |
| OS capability policy | `unveil` + `pledge` per role in `OpenBSDSandbox` (log/supervisor/torrent-storage broker implemented statically; network/crypto/admin backends pending) | `LinuxSandbox`: seccomp socket filters, Landlock write paths, capability drop, optional net namespace and cgroup v2 limits. The log worker applies its full profile; the supervisor uses capability-only bootstrap so exec'ed children can apply delegated roles. Router/torrent storage workers are implemented statically; netDb/profile/key and network backends remain pending | Privileges are deleted from the restricted token and Job UI/exception limits are applied; AppContainer/LPAC and per-role storage/network ACLs remain fail-closed/pending |
| Backpressure | Bounded send timeout (1 s); close and drop on failure; no file fallback; logger queue capped and counted | Same | Overlapped I/O deadlines; oversized message closes the pipe before a tail can become a second frame |
| Fallback to insecure IPC | None; failure is fatal | None; failure is fatal | None; failure is fatal |

The public Windows APIs are preferred over undocumented ALPC. No worker IPC
uses TCP. The UNIX implementation deliberately fails if `SOCK_SEQPACKET` is
unavailable instead of silently switching to a weaker channel; Windows uses a
local message-mode Named Pipe with an explicit DACL.

## See also

- [process capabilities](process-capabilities.md)
- [privilege separation](privilege-separation.md)
- [windows worker ipc](windows-worker-ipc.md)
- [storage worker](../torrent/storage-worker.md)
- [tui ipc](../tui-ipc.md)

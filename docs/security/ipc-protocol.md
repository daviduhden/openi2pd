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
- Peer identity is checked with `SO_PEERCRED` (Linux) or `getpeereid`
  (OpenBSD/BSD). The worker rejects a peer whose UID is not its effective UID.
  The parent does not need a separate check because it created the socketpair
  and execs the child itself. Cross-UID rejection is not yet exercised by an
  automated test (it requires a second UID).
- Every operation has a timeout and bounded message size.

## Frame

```
0                4        6        8                 12              16
+----------------+--------+--------+-----------------+---------------+
| magic "IP2P"   | ver=2  | type   | request-id      | payload-len    |
+----------------+--------+--------+-----------------+---------------+
| payload (0..65536-16 bytes)                                      |
+-------------------------------------------------------------------+
```

All integers are network byte order. The header is encoded field-by-field; no
C++ object memory is transmitted.

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
- Log text is additionally sanitized (control characters, including DEL,
  flattened) by the worker.

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

The production supervisor path forwards `ConfigureLog`; the **log worker**
owns the file capability and opens the configured path with
`O_CLOEXEC|O_NOFOLLOW` and `0600`. The supervisor never opens the log file, so
it has neither network nor persistent-write capability. `ConfigureLogFd`
remains for direct/legacy tests: the sender pre-opens the file, transfers one
descriptor with `SCM_RIGHTS`, and retains no copy. The supervisor rejects an
unexpected descriptor.

A log worker rejects `LogRecord` before `ConfigureLog`, rejects unknown types,
rejects malformed/oversized fields and exits on protocol error. `Ping` and
`Shutdown` are accepted before configuration so a supervisor can probe or stop
a freshly spawned worker. `ConfigureLog` is accepted exactly once; a duplicate
is a fatal protocol error. `path` and `tag` containing an embedded NUL are
rejected so the worker cannot open a different path than the parent encoded.

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

## Authorization

- Every server dispatch checks `AllowsOperation(EndpointRole, MessageType)`
  before its switch. A type valid for one endpoint (for example
  `LogRecord`) is a protocol failure on every other endpoint.
- Log worker only accepts log/configure/shutdown/ping operations. Router and
  torrent storage only accept their own setup/request/shutdown/ping types.
  The storage broker only accepts configure/spawn/shutdown/ping.
- `Shutdown` and `Ping` payloads must be empty. A descriptor on `Ping`,
  `Shutdown` or a log record is rejected.
- Router storage accepts four fixed filenames; torrent storage accepts only
  bounded paths beneath its delegated root. No generic `write_file(path,data)`
  API, secret-read operation or command execution operation exists.
- Storage requests must use strictly increasing request IDs. A duplicate,
  replayed or out-of-order request is rejected without re-executing a mutation.
  The OpenBSD broker similarly drops stale spawn IDs and reports per-request
  path/capacity failures as typed `Error` replies without killing live workers.
- Descriptor passing is operation-specific. `ConfigureLogFd` accepts exactly
  one regular-file descriptor; torrent storage accepts one validated root
  descriptor (or inherits it at fd 4 on OpenBSD). Adopted worker channels are
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
- Worker protocol violation exits the worker with failure.
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
  peer EOF, but it must satisfy the same Linux `SCM_CREDENTIALS` check as a
  data frame. More than eight consecutive empty records in one receive call
  is a flood error. Only a hangup/error poll event is terminal.
- The storage broker returns `Error{Busy}`/`Error{NotAuthorized}`/`Error{
  Internal}` for capacity, path-replacement and worker-spawn failures. These
  do not destroy already-granted storage channels. Malformed frames,
  unauthenticated peers and descriptor misuse remain fatal.

## Versioning

`PROTOCOL_VERSION = 2`; `MIN_PROTOCOL_VERSION = 1`. Version 2 adds the typed
`Error` message and the shared endpoint allowlist. Version-1 frames remain
readable by the same binary for upgrade compatibility, but new senders always
emit version 2. A version outside `1..2` is rejected; there is no downgrade or
"best effort" mode. Future changes must either bump the version or add a new
message type with explicit negotiation.

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
| Peer identity | `getpeereid(3)` (implemented) | `SO_PEERCRED` (implemented) | Client PID/session + token user SID via `GetNamedPipeClient*` (implemented); no impersonation |
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

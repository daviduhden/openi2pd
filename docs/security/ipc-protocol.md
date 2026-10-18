# Internal IPC protocol

## Transport

- Anonymous `socketpair(AF_UNIX, SOCK_SEQPACKET)` between related processes.
- `SOCK_CLOEXEC` is applied; if the kernel rejects the flag combination, the
  code retries without it and sets `FD_CLOEXEC` explicitly. A failure to set
  `FD_CLOEXEC` is fatal rather than leaking a descriptor across `exec`.
- No TCP, no globally named UNIX socket for internal worker IPC.
- The worker receives the channel descriptor as `--privsep-worker log <fd>`,
  not through an environment variable. It verifies that the descriptor is an
  inherited `SOCK_SEQPACKET` socket (`fstat`/`S_ISSOCK` and `SO_TYPE`) and
  rejects invalid descriptors, so a manual invocation cannot downgrade the
  transport to a stream or a regular file.
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
| magic "IP2P"   | ver=1  | type   | request-id      | payload-len    |
+----------------+--------+--------+-----------------+---------------+
| payload (0..65536-16 bytes)                                      |
+-------------------------------------------------------------------+
```

All integers are network byte order. The header is encoded field-by-field; no
C++ object memory is transmitted.

Limits:

- Header: 16 bytes.
- Total frame: 64 KiB (65536 bytes); payload at most 65520 bytes.
- Text field: 8 KiB (8192 bytes).
- Encoding a longer text field truncates it at the bound; decoding a declared
  text length above the bound, or beyond the remaining payload, fails with
  `BadField`.
- A declared frame length that does not exactly match the `SOCK_SEQPACKET`
  record fails with `Malformed`; a declared length above the payload limit
  fails with `TooLarge`. Truncated datagrams (`MSG_TRUNC`) are rejected.
- Log text is additionally sanitized (control characters, including DEL,
  flattened) by the worker.

## Message types

| Type | Direction | Payload |
|---|---|---|
| `ConfigureLog` | parent → log worker | `u8 syslog`, `u32 path-len + path`, `u32 tag-len + tag` |
| `LogRecord` | parent → log worker | `u8 level`, `u64 timestamp`, `u32 text-len + text` |
| `Shutdown` | parent → worker | empty |
| `Ping` / `Pong` | either | empty; request-id echoed |

A log worker rejects `LogRecord` before `ConfigureLog`, rejects unknown types,
rejects malformed/oversized fields and exits on protocol error. `Ping` and
`Shutdown` are accepted before configuration so a supervisor can probe or stop
a freshly spawned worker. `ConfigureLog` is accepted exactly once; a duplicate
is a fatal protocol error. `path` and `tag` containing an embedded NUL are
rejected so the worker cannot open a different path than the parent encoded.

The protocol is currently used only for logging. There is no network or storage
worker protocol, and no generic file-write or descriptor-passing operation.

## Authorization

- Log worker only accepts log/configure/shutdown/ping operations.
- It has no operation that can name an arbitrary path other than the
  single configured log file.
- The storage worker protocol is not implemented yet; no generic
  `write_file(path,data)` API is exposed.
- Descriptor passing (`SCM_RIGHTS`) is not implemented for the log channel.
  If added, it must be operation-specific and rights-minimized.

## Failure behavior

- `send`/`recv` failure closes the channel. A peer close is reported as an
  error, distinct from a receive timeout, so the worker exits instead of
  polling a closed socket forever.
- The router drops log records if the worker channel closes; it never falls
  back to opening the log file in the core process.
- Worker protocol violation exits the worker with failure.
- A single in-process monitor restarts a dead log worker with bounded
  exponential backoff (500 ms, doubling to 30 s; at most eight consecutive
  attempts; reset after 60 s of stable operation). The previous child is reaped
  before the replacement is spawned, so there is at most one configured worker.
  The monitor is shutdown-aware and runs as one joined thread, not an unbounded
  thread pool.
- When the attempt limit is reached, the worker stays stopped and log records
  are dropped. This is a restart foundation, not a separate supervisor process;
  network and storage separation remain unimplemented.
- Liveness detection is exit- and channel-based. A worker that is alive but
  wedged (for example blocked in a filesystem write) is not killed or restarted;
  the sink send timeout bounds the logger stall and then the channel is closed.
  A `Ping` watchdog is not implemented because the parent would have to consume
  `Pong` replies on the same channel that carries log records.

## Versioning

`PROTOCOL_VERSION = 1`. A version mismatch is rejected; there is no
downgrade or "best effort" mode. Future changes must either bump the version or
add a new message type with explicit negotiation.

## Platform mechanism comparison

| Requirement | OpenBSD | Linux | Windows |
|---|---|---|---|
| Local transport | UNIX `SOCK_SEQPACKET` (implemented) | UNIX `SOCK_SEQPACKET` (implemented) | Microsoft RPC `ncalrpc` or Named Pipes (planned) |
| Peer identity | `getpeereid(3)` (implemented) | `SO_PEERCRED` (implemented) | RPC auth / client PID + impersonation (planned) |
| Message boundaries | `SOCK_SEQPACKET` | `SOCK_SEQPACKET` | `PIPE_TYPE_MESSAGE`/RPC (planned) |
| Descriptor transfer | `SCM_RIGHTS`, only for authorized ops (not used yet) | `SCM_RIGHTS` (not used yet) | `DuplicateHandle`, rights-limited (planned) |
| Process supervision | `waitpid`, signals; in-process log restart monitor (implemented) | eager `pidfd` preferred, `waitpid` fallback (implemented); in-process monitor | Job Objects + process handles (planned) |
| OS capability policy | `unveil` + `pledge` per worker (log implemented) | seccomp/Landlock/namespaces/caps (pending) | Restricted token/AppContainer/LPAC/ACL (planned) |
| Backpressure | Bounded send timeout (1 s); close and drop on failure; no file fallback | Same | Overlapped I/O/COP pending |
| Fallback to insecure IPC | None; failure is fatal | None; failure is fatal | None; failure is fatal |

The public Windows APIs are preferred over undocumented ALPC. No worker IPC
uses TCP. The current implementation deliberately fails if `SOCK_SEQPACKET` is
unavailable instead of silently switching to a weaker channel.

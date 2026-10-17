# Internal IPC protocol

## Transport

- Anonymous `socketpair(AF_UNIX, SOCK_SEQPACKET)` between related processes.
- `SOCK_CLOEXEC` is applied; if the kernel rejects the flag combination, the
  code retries without it and sets `FD_CLOEXEC` explicitly.
- No TCP, no globally named UNIX socket for internal worker IPC.
- Peer identity is checked with `SO_PEERCRED` (Linux) or `getpeereid`
  (OpenBSD/BSD). A mismatched UID is rejected.
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
- Total frame: 64 KiB.
- Text field: 8 KiB.
- Log text is additionally sanitized (control characters flattened) by the
  worker.

## Message types

| Type | Direction | Payload |
|---|---|---|
| `ConfigureLog` | parent → log worker | `u8 syslog`, `u32 path-len + path`, `u32 tag-len + tag` |
| `LogRecord` | parent → log worker | `u8 level`, `u64 timestamp`, `u32 text-len + text` |
| `Shutdown` | parent → worker | empty |
| `Ping` / `Pong` | either | empty; request-id echoed |

A log worker rejects `LogRecord` before `ConfigureLog`, rejects unknown types,
rejects malformed/oversized fields and exits on protocol error. Configuration is
sent once.

## Authorization

- Log worker only accepts log/configure/shutdown/ping operations.
- It has no operation that can name an arbitrary path other than the
  single configured log file.
- The storage worker protocol is not implemented yet; no generic
  `write_file(path,data)` API is exposed.
- Descriptor passing (`SCM_RIGHTS`) is not implemented for the log channel.
  If added, it must be operation-specific and rights-minimized.

## Failure behavior

- `send`/`recv` failure closes the channel.
- The router drops log records if the worker channel closes; it never falls
  back to opening the log file in the core process.
- Worker protocol violation exits the worker with failure.
- Parent process currently does not auto-restart the log worker; this is a
  pending supervisor item. A dead worker therefore means lost logs, not
  regained file access in core.

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
| Process supervision | `waitpid`, signals | `pidfd` preferred, `waitpid` fallback (implemented) | Job Objects + process handles (planned) |
| OS capability policy | `unveil` + `pledge` per worker (log implemented) | seccomp/Landlock/namespaces/caps (pending) | Restricted token/AppContainer/LPAC/ACL (planned) |
| Backpressure | Worker channel timeouts; parent queue pending | Same | Overlapped I/O/COP pending |
| Fallback to insecure IPC | None; failure is fatal | None; failure is fatal | None; failure is fatal |

The public Windows APIs are preferred over undocumented ALPC. No worker IPC
uses TCP. The current implementation deliberately fails if `SOCK_SEQPACKET` is
unavailable instead of silently switching to a weaker channel.

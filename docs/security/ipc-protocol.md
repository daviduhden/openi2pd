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
- Each process receives its channel descriptor as an argument, not through an
  environment variable. It verifies that the descriptor is an inherited
  `SOCK_SEQPACKET` socket (`fstat`/`S_ISSOCK` and `SO_TYPE`) and rejects invalid
  descriptors, so a manual invocation cannot downgrade the transport to a
  stream or a regular file.
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
| `ConfigureLog` | core → supervisor → log worker | path mode: `u8 syslog`, `u32 path-len + path`, `u32 tag-len + tag` |
| `ConfigureLogFd` | direct test/legacy path only | same payload, plus exactly one `SCM_RIGHTS` file descriptor for a file sink |
| `LogRecord` | core → supervisor → log worker | `u8 level`, `u64 timestamp`, `u32 text-len + text` |
| `Shutdown` | parent → worker | empty |
| `Ping` / `Pong` | either | empty; request-id echoed |

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

The production protocol is currently used only for logging. There is no
network or storage worker protocol and no generic file-write API;
`ConfigureLogFd` is the sole descriptor-passing operation. The OpenBSD
`OpenBSDSandbox` role policies and `OpenBSDImsg` adapter are preparation for
those backends, not a claim that they are already migrated.

## Authorization

- Log worker only accepts log/configure/shutdown/ping operations.
- It has no operation that can name an arbitrary path other than the
  single configured log file.
- The storage worker protocol is not implemented yet; no generic
  `write_file(path,data)` API is exposed.
- Descriptor passing is limited to the `ConfigureLogFd` operation, where
  exactly one `SCM_RIGHTS` descriptor is accepted. There is no generic
  descriptor-passing API; any future operation must be operation-specific and
  rights-minimized.

## Failure behavior

- `send`/`recv` failure closes the channel. A peer close is reported as an
  error, distinct from a receive timeout, so the worker exits instead of
  polling a closed socket forever.
- The router drops log records if the worker channel closes; it never falls
  back to opening the log file in the core process.
- Worker protocol violation exits the worker with failure.
- The supervisor process restarts a dead log worker. The core has a bounded
  monitor/backoff for replacing a dead supervisor; the supervisor itself owns
  the log child. The previous child is reaped before a replacement is spawned,
  so there is at most one configured log worker per supervisor.
- When the core's supervisor restart limit is reached, logging stays stopped
  and records are dropped. Network and storage separation remain
  unimplemented and are not claimed.
- Liveness detection is exit- and channel-based. A worker that is alive but
  wedged (for example blocked in a filesystem write) is not killed or restarted;
  the sink send timeout bounds the logger stall and then the channel is closed.
  A `Ping` watchdog is not implemented because the parent would have to consume
  `Pong` replies on the same channel that carries log records.

## Versioning

`PROTOCOL_VERSION = 1`. A version mismatch is rejected; there is no
downgrade or "best effort" mode. Future changes must either bump the version or
add a new message type with explicit negotiation.

## OpenBSD native adapter

`privsep/OpenBSDImsg.{h,cpp}` wraps the OpenBSD `imsg`/`imsgbuf` API
(`libutil`) and carries the same versioned frame as a payload. It enables
`imsgbuf_allow_fdpass` for exactly one `SCM_RIGHTS` descriptor, applies
bounded poll deadlines and validates peer identity with `getpeereid(2)`.
`MAX_IMSGSIZE` is 16 KiB on OpenBSD; the adapter rejects larger frames, while
the existing 8 KiB log-text bound fits with room to spare. The adapter is
exercised by `test-privsep-openbsd-imsg` in the manual OpenBSD job. The production supervisor/log path continues to use the portable
`SOCK_SEQPACKET` channel so Linux and OpenBSD share the same framing while
the remaining backends are migrated; the adapter is not a weaker fallback.

## Platform mechanism comparison

| Requirement | OpenBSD | Linux | Windows |
|---|---|---|---|
| Local transport | UNIX `SOCK_SEQPACKET` (implemented); native `imsg` adapter prepared/tested in CI | UNIX `SOCK_SEQPACKET` (implemented) | Local message-mode Named Pipe (`PIPE_TYPE_MESSAGE`, `PIPE_REJECT_REMOTE_CLIENTS`) implemented as primitives; worker integration pending |
| Peer identity | `getpeereid(3)` (implemented) | `SO_PEERCRED` (implemented) | Client PID/session + token user SID via `GetNamedPipeClient*` (implemented); no impersonation |
| Message boundaries | `SOCK_SEQPACKET`; imsg adapter preserves versioned frame | `SOCK_SEQPACKET` | One message-mode pipe message per versioned frame |
| Descriptor transfer | `SCM_RIGHTS` for `ConfigureLogFd`; pre-opened storage roots validated | `SCM_RIGHTS` for `ConfigureLogFd` only | No inheritance/`DuplicateHandle`; worker reconnects by local pipe name |
| Process supervision | `waitpid`, signals; separate supervisor process for log worker (implemented) | eager `pidfd` preferred, `waitpid` fallback; separate supervisor process (implemented) | Restricted token + Job Object + mitigation attributes implemented as primitives; supervisor integration pending |
| OS capability policy | `unveil` + `pledge` per role in `OpenBSDSandbox` (log/supervisor implemented; storage/network/crypto/admin backends pending) | `LinuxSandbox`: seccomp socket filters, Landlock write paths, capability drop, optional net namespace and cgroup v2 limits (log/supervisor applied; storage/network backends pending) | Restricted token/Job Object profiles; AppContainer/LPAC probed and fail-closed; full storage/network enforcement pending |
| Backpressure | Bounded send timeout (1 s); close and drop on failure; no file fallback | Same | Overlapped I/O/COP pending |
| Fallback to insecure IPC | None; failure is fatal | None; failure is fatal | None; failure is fatal |

The public Windows APIs are preferred over undocumented ALPC. No worker IPC
uses TCP. The UNIX implementation deliberately fails if `SOCK_SEQPACKET` is
unavailable instead of silently switching to a weaker channel; Windows uses a
local message-mode Named Pipe with an explicit DACL.

# Process capabilities and OS policies

## Current implementation

| Process | OpenBSD pledge/unveil | Linux | Windows |
|---|---|---|---|
| Router core | Existing broad policy (`stdio rpath wpath cpath fattr inet dns unix flock`, optional `mcast`; `/dev/log` `w` when logging to syslog) | No seccomp/Landlock; normal process | Normal process |
| Log worker | `unveil(logfile, rwc)` then `pledge("stdio rpath wpath cpath fattr")`; syslog: `pledge("stdio unix")`; stdout: `pledge("stdio")`. Requires the parent sandbox to be disabled because the spawn needs `proc`/`exec`; otherwise startup is refused | `PR_SET_NO_NEW_PRIVS`; same UID as core; parent death via channel EOF; no seccomp/Landlock yet | Not implemented |
| Supervisor | In-process restart monitor thread (log worker only); not a separate process or a sandbox boundary | Same | Same |
| Network worker | Not implemented | Not implemented | Not implemented |
| Storage worker | Not implemented | Not implemented | Not implemented |
| Crypto worker | Not implemented | Not implemented | Not implemented |
| TUI | Separate client; local UNIX socket 0600 | Same | Not available |

The OpenBSD runtime pledge is applied before the log worker is spawned. The
worker therefore cannot be used while `openbsd.pledge_enabled` or
`openbsd.unveil_enabled` is active: `fork`/`execv` need `proc`/`exec`, which the
runtime policy deliberately omits. `i2pd` refuses to start for that
combination rather than silently disabling either the worker or the sandbox.
The syslog destination of the core unveils `/dev/log` with `w`; the `unix`
promise alone does not cover the pathname lookup done by `connect(2)`.

## Target policies

### Log worker

- File: `stdio rpath wpath cpath fattr`; `unveil` the configured log path plus
  read-only `/etc/localtime` for timestamp formatting; no `inet`, `dns`, `unix`
  (file case).
- Syslog: `stdio unix`; no filesystem write.
- Foreground stdout: `stdio`; no filesystem access.
- Configuration is accepted exactly once, before the restrictive policy is
  applied. A worker that never receives configuration remains under the
  inherited promises; it never opens a log path without configuration.
- No config/keys/netDb paths.

### Storage worker (planned)

- `unveil` only data/certificate/config directories and explicit files.
- `pledge("stdio rpath wpath cpath fattr flock")`; **no** `inet`, `dns`.
- Typed operations only (`ReadRouterInfo(hash)`, `WriteRouterInfo`,
  `ReadProfile`, `WriteProfile`, `ReadKey(role)` etc.), never arbitrary paths.
- chroot optional; not a substitute for pledge/unveil.

### Network worker (planned)

- `pledge("stdio inet dns unix")` plus only required `mcast` for UPnP if
  applicable; **no** `wpath`, `cpath`, `fattr`.
- No key/config/log file descriptors; only the IPC channel and network sockets.
- Key operations delegated to crypto worker or pre-derived session material.

### Router core (planned)

- `pledge("stdio unix")` plus timer/queue syscalls covered by stdio.
- No `inet`, `dns`, `wpath`, `cpath`, `rpath` (except an explicitly needed
  immutable resource, if unavoidable and documented).
- All persistence and network via IPC.

### Crypto worker (planned)

- No network; minimal `stdio unix`.
- Key files read during initialization, then `unveil` locked/closed.
- Operation-specific key handles, no generic "sign arbitrary bytes with any
  key" API without caller/operation authorization.

### Supervisor

- **Current:** a single in-process thread monitors and restarts only the log
  worker. It is not a security boundary and does not satisfy the target
  architecture.
- **Planned:** separate process. Startup: fork/exec, channel setup, privilege
  drop. Runtime: `stdio unix` only; no router packet processing, no network, no
  persistent writes. Uses pidfd on Linux where available; waitpid on OpenBSD.
  Bounded restart backoff and single-owner channel policy move with it.

## Linux mechanisms

Currently: `SOCK_SEQPACKET`, `SO_PEERCRED`, `SOCK_CLOEXEC`,
`PR_SET_NO_NEW_PRIVS`, eager pidfd supervision where available (waitpid
fallback), async-signal-safe post-fork setup, explicit reaping before a
restart, and EOF-based parent-death detection.

Pending: seccomp-BPF allowlists, Landlock path rules, user/mount/net
namespaces where useful, reduced capabilities, cgroup v2 limits. Network and
filesystem restrictions must be tested together; Landlock does not cover all
socket operations.

Descriptor hygiene is best-effort on old Linux kernels: `close_range(2)` is
used when available; otherwise the child closes descriptors 4..1023. A router
with `limits.openfiles` above 1024 could therefore leak higher descriptors into
the same-UID worker. This does not cross a privilege boundary but is a residual
for the "no unnecessary inherited descriptors" invariant.

## Windows mechanisms

No worker IPC implementation yet. Target: local `ncalrpc` with
`RPC_C_AUTHN_WINNT`, or Named Pipes with explicit DACL,
`PIPE_REJECT_REMOTE_CLIENTS`, `PIPE_TYPE_MESSAGE`, overlapped I/O, client PID
plus impersonation checks, restricted tokens/AppContainer/LPAC and Job Objects.
Do not rely on PID alone or default pipe ACLs.

## Android

The app remains a single ART process hosting JNI. Android process/UID
isolation, SELinux domains and service lifecycle make a desktop-style worker
split non-trivial. Current app changes remove broad storage permissions and the
WebView console; full worker separation is a documented platform limitation, not
a claimed guarantee.

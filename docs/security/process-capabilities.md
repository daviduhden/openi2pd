# Process capabilities and OS policies

## Current implementation

| Process | OpenBSD pledge/unveil | Linux | Windows |
|---|---|---|---|
| Router core | Existing broad policy (`stdio rpath wpath cpath fattr inet dns unix flock`, optional `mcast/unveil`) | No seccomp/Landlock; normal process | Normal process |
| Log worker | `unveil(logfile, rwc)` then `pledge("stdio rpath wpath cpath fattr")`; syslog: `pledge("stdio unix")` | `PR_SET_NO_NEW_PRIVS`; no seccomp/Landlock yet | Not implemented |
| Supervisor | Not separate | Not separate | Not separate |
| Network worker | Not implemented | Not implemented | Not implemented |
| Storage worker | Not implemented | Not implemented | Not implemented |
| Crypto worker | Not implemented | Not implemented | Not implemented |
| TUI | Separate client; local UNIX socket 0600 | Same | Not available |

## Target policies

### Log worker

- File: `stdio rpath wpath cpath fattr`; `unveil` only the configured log path;
  no `inet`, `dns`, `unix` (file case).
- Syslog: `stdio unix`; no filesystem write.
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

### Supervisor (planned)

- Startup: fork/exec, channel setup, privilege drop.
- Runtime: `stdio unix` only; no router packet processing, no network, no
  persistent writes.
- Uses pidfd on Linux where available; waitpid on OpenBSD.

## Linux mechanisms

Currently: `SOCK_SEQPACKET`, `SO_PEERCRED`, `SOCK_CLOEXEC`,
`PR_SET_NO_NEW_PRIVS`, optional pidfd supervision.

Pending: seccomp-BPF allowlists, Landlock path rules, user/mount/net
namespaces where useful, reduced capabilities, cgroup v2 limits. Network and
filesystem restrictions must be tested together; Landlock does not cover all
socket operations.

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

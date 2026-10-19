<!--
See the LICENSE file at the top of the project tree for copyright
and license details.

openi2pd: process capabilities.

Documents project documentation.

-->

# Process capabilities and OS policies

## Current implementation

| Process | OpenBSD pledge/unveil | Linux | Windows |
|---|---|---|---|
| Router core | Existing broad policy (`stdio rpath wpath cpath fattr inet dns unix flock`, optional `mcast`; `/dev/log` `w` only when the core owns syslog). When `privsep.log` owns the sink, `proc` is retained for bounded supervisor reaping/signalling; `exec` is absent | No seccomp/Landlock; normal process | Normal process |
| Log worker | `OpenBSDSandbox` policy: direct file `unveil(/etc/localtime,r)` + `unveil(logfile,rwc)` + `pledge("stdio unix rpath wpath cpath fattr")`; pre-opened descriptor `pledge("stdio unix rpath")` with no log path rule; syslog `unveil(/dev/log,w)` + `pledge("stdio unix")`; stdout `pledge("stdio unix")`. The worker is spawned before the parent runtime sandbox | `LinuxSandbox`: no-new-privs, capset/bounding-set drop, Landlock write restriction to the log file, seccomp denies AF_INET/AF_INET6 while allowing AF_UNIX; `requireLandlock=false` for pre-opened/stdout compatibility | Message-mode pipe primitives, protected DACL, peer SID check, restricted token and Job Object helpers implemented; daemon/log integration pending |
| Supervisor | Separate process; owns log worker/channel; pre-resolves the executable, then `pledge("stdio unix proc exec")`; no network and no path write/read promise; it never opens the log file | `LinuxSandbox`: no-new-privs, capability drop, optional empty net namespace, Landlock write deny, seccomp AF_INET/AF_INET6 deny while allowing AF_UNIX socketpairs; fork/exec retained | Named-pipe primitives/Job Object policy implemented; supervisor integration pending |
| Network worker | `OpenBSDSandbox` policy builder: `stdio unix inet dns` (+ `mcast` only with UPnP), no `wpath/cpath/fattr`; backend migration pending | `LinuxSandbox` profile builder: sockets allowed, Landlock persistent-write deny, no seccomp socket deny; backend not migrated | `WindowsWorkerPolicy` network profile; AppContainer/LPAC fail closed; backend not migrated |
| Storage worker | `OpenBSDSandbox` policy builder: `stdio unix rpath wpath cpath fattr flock recvfd`, unveils only explicit data/certificate roots; no `inet/dns`; backend migration pending | `LinuxSandbox` profile builder: no new sockets, optional empty net namespace, Landlock writes only to explicit paths; backend not migrated | `WindowsWorkerPolicy` storage profile; restricted token + Job Object; AppContainer/LPAC fail closed; backend not migrated |
| Crypto worker | `OpenBSDSandbox` policy builder: `stdio unix`, no network/path write; key handles and backend pending | `LinuxSandbox` profile builder: no sockets, capability drop, Landlock write deny; backend not migrated | `WindowsWorkerPolicy` crypto profile; backend not migrated |
| TUI | Separate client; local UNIX socket 0600 | Same | Not available |

The supervisor is spawned from the startup phase, while the OpenBSD startup
pledge still contains `proc`/`exec`. The parent then installs its runtime
policy without `exec`; `proc` is retained only while `privsep.log` is active so
the core can reap/signal the supervisor. No worker relies on pledge or unveil
state surviving `execve`. The supervisor resolves the executable path before
it applies its own policy. The supervisor owns bounded log-worker replacement;
a dead supervisor is not replaced by the core when the runtime sandbox is
active.

The core no longer unveils a log file or `/dev/log` when the log worker owns
the sink. The log worker installs its own `/dev/log` rule before pledge; this
does not depend on the core's unveil state. Workers refuse to run with effective
UID 0; the packaged service is expected to run as `_i2pd`.

`privsep/OpenBSDSandbox.{h,cpp}` contains per-role policy construction and
runtime application. `BuildOpenBSDPolicy` is used by the log worker and
supervisor and is prepared for storage, network, crypto and admin. The
storage/network/crypto/admin backends are not yet wired into the core; their
existence in the policy builder is not a capability claim.

`privsep/OpenBSDImsg.{h,cpp}` provides an OpenBSD `imsg` adapter that carries
the same versioned frame and supports `imsgbuf_allow_fdpass`/one descriptor.
It is exercised by the manual OpenBSD CI test; the current supervisor/log path
continues to use the portable `SOCK_SEQPACKET` channel until all backends are
migrated.

## Target policies

### Log worker

- File: `stdio unix rpath wpath cpath fattr`; `unveil` the configured log path
  plus read-only `/etc/localtime` for timestamp formatting; no `inet`, `dns`.
- Pre-opened file descriptor: `stdio unix rpath`; no `wpath/cpath/fattr` and no
  rule for the log path. The descriptor must be close-on-exec and a regular
  file.
- Syslog: `stdio unix` with an explicit `/dev/log` `w` rule; no filesystem write
  except that local socket.
- Foreground stdout: `stdio unix`; no filesystem access.
- Configuration is accepted exactly once, before the restrictive policy is
  applied. The worker installs its own policy after `execve`; it does not rely
  on the parent's pledge/unveil state.
- No config/keys/netDb paths.

### Storage worker (policy implemented; backend pending)

- `Unveil` only explicit absolute data/certificate roots and explicit absolute
  extra paths; no implicit current directory or whole-filesystem rule. A
  relative path is rejected rather than resolved under the worker's cwd.
- `pledge("stdio unix rpath wpath cpath fattr flock recvfd")`; **no** `inet`,
  `dns` or `sendfd`.
- Typed operations only (`ReadRouterInfo(hash)`, `WriteRouterInfo`,
  `ReadProfile`, `WriteProfile`, `ReadKey(role)` etc.), never arbitrary paths.
- Pre-opened roots are validated as close-on-exec directories before use;
  `SCM_RIGHTS` accepts at most one descriptor per operation.
- chroot optional; not a substitute for pledge/unveil.

### Network worker (policy implemented; backend pending)

- `pledge("stdio unix inet dns")` plus only required `mcast` for UPnP if
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

- **Current:** a separate supervisor process owns the log worker and forwards
  records. On OpenBSD it pre-resolves the executable and applies
  `pledge("stdio unix proc exec")`; it has no network and never opens the log
  file. It retains fork/exec only to restart the log child, which is why no
  seccomp/Landlock filter is installed yet. The core does not retain `exec`; it
  keeps `proc` only for bounded supervisor reaping/signalling and cannot replace
  a dead supervisor.
- **Planned:** move storage/network/crypto workers and core replacement under
  the same supervisor with typed channels and per-role policies. The supervisor
  must never combine network and persistent-write capabilities.

## Linux mechanisms

Current transport and lifecycle: `SOCK_SEQPACKET`, `SO_PEERCRED`,
`SO_PASSCRED`/mandatory `SCM_CREDENTIALS` per frame, one `SCM_RIGHTS` descriptor
with `FD_CLOEXEC`, `PR_SET_NO_NEW_PRIVS`, eager pidfd supervision where
available (waitpid fallback), `pidfd_send_signal` where available, async-
signal-safe post-fork setup, explicit reaping before a restart, and EOF-based
parent-death detection.

Current sandbox: `LinuxSandbox` now applies the log worker and supervisor
profiles. It drops effective/permitted/ambient capabilities and the capability
bounding set, optionally creates an empty network namespace, installs Landlock
filesystem write restrictions with explicit writable paths, and installs a
seccomp-BPF socket filter. Storage denies every new socket; log/supervisor deny
AF_INET/AF_INET6 while retaining AF_UNIX; networking keeps sockets but has no
persistent-write rule. `LinuxCgroupV2Limits` can apply `memory.max`/`pids.max`
to an explicitly delegated cgroup v2 and move the current process there.

Limits: Landlock network mediation starts at ABI 4 (TCP) and ABI 10 (UDP);
older kernels rely on seccomp for storage/log/supervisor and are rejected for
network/storage profiles because Landlock is required. User namespaces are not
created automatically; `unshare(CLONE_NEWNET)` is optional and falls back to
seccomp when EPERM. cgroup delegation is deployment-specific. The sandbox does
not protect against same-UID sibling processes or a compromised core, and
network/storage backends are still pending. memfd sealing is not used for IPC:
the versioned `SOCK_SEQPACKET` frame and one `SCM_RIGHTS` descriptor are already
bounded, so memfd would add descriptor lifecycle and no isolation benefit.

Descriptor hygiene is best-effort on old Linux kernels: `close_range(2)` is
used when available; otherwise the child closes descriptors 4..1023. A router
with `limits.openfiles` above 1024 could therefore leak higher descriptors into
the same-UID worker. This does not cross a privilege boundary but is a residual
for the "no unnecessary inherited descriptors" invariant.

## Windows mechanisms

`privsep/WindowsIpc.{h,cpp}` implements the local transport as a message-mode
Named Pipe (`PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE`), with
`PIPE_REJECT_REMOTE_CLIENTS`, `FILE_FLAG_FIRST_PIPE_INSTANCE`, one instance,
overlapped I/O deadlines and the common versioned frame. Pipe names must start
with `\\.\pipe\`; a remote UNC name is rejected. The protected DACL is built
from SDDL and grants only the process user and local `SYSTEM`; no Everyone,
Authenticated Users or anonymous ACE, and no inherited ACEs.

`SO_PEERCRED` has no Windows equivalent for a pipe; the server obtains the
client PID/session with `GetNamedPipeClientProcessId`/`SessionId`, opens the
client token, requires the same user SID, and exposes PID/SID/session/elevation
through `WindowsPeer`. PID alone is never treated as authorization. Operation
allowlisting is role-specific (`WindowsAllowsOperation`).

Workers are spawned with a `CreateRestrictedToken` token (no privilege
inheritance), an optional Job Object (`KILL_ON_JOB_CLOSE`, process/memory
limits) and mitigation attributes; `bInheritHandles=FALSE` and the pipe name is
passed on the command line, so no handle is inherited or duplicated.
AppContainer/LPAC is probed through documented `userenv.dll` exports but is not
silently substituted; requesting it fails closed. No undocumented ALPC API is
used. `ncalrpc` remains an evaluated alternative, not a second implementation.

The storage/network worker backends and typed operations remain pending. A
restricted token and Job Object are resource/process controls, not a complete
network/filesystem sandbox; AppContainer/LPAC or an equivalent enforced
boundary is required before claiming effective Windows separation. See
[windows-worker-ipc.md](windows-worker-ipc.md).

## Android

The app remains a single ART process hosting JNI. Android process/UID
isolation, SELinux domains and service lifecycle make a desktop-style worker
split non-trivial. Current app changes remove broad storage permissions and the
WebView console; full worker separation is a documented platform limitation, not
a claimed guarantee.

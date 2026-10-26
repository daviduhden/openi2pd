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
| Router core | Existing broad policy (`stdio rpath wpath cpath fattr inet dns unix flock`, optional `mcast`; `/dev/log` `w` only when the core owns syslog). When `privsep.log` owns the sink, `proc` is retained for bounded supervisor reaping/signalling; `exec` is absent | No seccomp/Landlock; normal process. With `privsep.artifacts`, netDb/profile/destination-key writes go to the data-storage worker; the core keeps its direct write capability for the read path and remaining artifacts | Normal process |
| Log worker | `OpenBSDSandbox` policy: direct file `unveil(/etc/localtime,r)` + `unveil(logfile,rwc)` + `pledge("stdio unix rpath wpath cpath fattr")`; pre-opened descriptor `pledge("stdio unix rpath")` with no log path rule; syslog `unveil(/dev/log,w)` + `pledge("stdio unix")`; stdout `pledge("stdio unix")`. The worker is spawned before the parent runtime sandbox | `LinuxSandbox`: no-new-privs and capability teardown happen in the spawn child before `execve`; Landlock write restriction to the log file and no `EXECUTE`; seccomp AF_INET/AF_INET6 deny (AF_UNIX kept for syslog) is installed **before** the configuration frame; `requireLandlock=false` for pre-opened/stdout compatibility | Low-privilege AppContainer (no `internetClient`) with a delegated log root, protected pipe DACL, PID/session/container/nonce-bound accept and role-bound per-operation IPC; no Windows log backend yet |
| Supervisor and storage broker | Separate processes; the supervisor owns the log worker, the broker owns storage-worker spawning. Both pre-resolve their own executable, `unveil(exe,"x")` + lock (exactly one `x` rule) and `pledge("stdio proc exec")` / `("stdio proc exec sendfd")`: no `unix`, no `rpath/wpath/cpath`, no descriptor reception, no network, and no way to exec anything except the frozen image. The log worker and every exec'ed storage worker install their own unveil+pledge after `execve` | `ApplyLinuxSupervisorSandbox`: no-new-privs and capability/bounding-set drop only. It deliberately installs no Landlock/seccomp/netns domain because those are inherited and cannot be relaxed for exec'ed storage/network children; each child applies its full role policy | Named-pipe primitives/Job Object policy implemented; supervisor integration pending |
| Network worker | `OpenBSDSandbox` policy builder: `stdio unix inet`, optional `dns`/`recvfd`/`route` (+ `mcast` only with UPnP), no `wpath/cpath/fattr`; backend migration pending | `LinuxSandbox` profile builder: sockets allowed, Landlock persistent-write deny, no seccomp socket deny; backend not migrated | AppContainer with `internetClient` + `privateNetworkClientServer`, no delegated write root and no persistent-write ACL; backend not migrated |
| Storage workers | Router-storage owns the four fixed router files; the data-storage worker owns `netDb/r*/routerInfo-<b64>.dat`, `peerProfiles/p*/profile-<b64>.txt` and `destinations/<b32>.<type>.dat`. Both use `OpenBSDSandbox`: `stdio rpath wpath cpath` (plus `unix`/`recvfd` only for the non-preopened path), unveiling only the delegated root; no `inet/dns`; torrent storage keeps its broker | Same roles under `LinuxSandbox`: capabilities are emptied in the spawn child; seccomp denies `socket`/`socketpair` **and** `connect`/`bind`/`listen`/`accept*`/`sendto`/`execve*`/`fork`/`clone`, installed before the configure frame; Landlock limits reads and writes to the delegated root and denies `EXECUTE`; an empty network namespace is attempted pre-exec when the core has `CAP_SYS_ADMIN` (best-effort; seccomp is the mandatory boundary); cgroup-v2 limits remain unwired | Low-privilege AppContainer (no `internetClient`) with one delegated write root granted to the container SID; protected pipe DACL; Job Object/mitigations; backend not migrated |
| Crypto worker | `OpenBSDSandbox` policy builder: `stdio unix`, no network/path write; key handles and backend pending | `LinuxSandbox` profile builder: no sockets, capability drop, Landlock write deny; backend not migrated | `WindowsWorkerPolicy` crypto profile; backend not migrated |
| TUI | Separate client; local UNIX socket 0600 | Same | Not available |

The supervisor is spawned from the startup phase, while the OpenBSD startup
pledge still contains `proc`/`exec`. The startup policy always carries `exec`
in this window so router/torrent storage workers can be spawned even when
`privsep.log` is disabled. The parent then installs its runtime policy without
`exec`; `proc` is retained only while `privsep.log` is active so the core can
reap/signal the supervisor. No worker relies on pledge or unveil state
surviving `execve`. The supervisor resolves the executable path before it
applies its own policy and owns bounded log-worker replacement with exponential
backoff and an eight-attempt cap. A dead supervisor is not replaced by the core
when the runtime sandbox is active.

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

### Capability invariant and worker identity (2026-10-26)

`privsep/Capabilities.h` declares every process role and a small capability
set (`ExternalNetwork`, `PersistentWrite`, `FilesystemRead`, `Signal`, `Exec`,
`FdPassSend`, `FdPassReceive`, `Ipc`). The contract is: **no role may set both
`ExternalNetwork` and `PersistentWrite`**. The table records the current core
row explicitly with the write bit (`CORE_LEGACY_CAPABILITIES`) so the remaining
gap is visible, and `CORE_TARGET_CAPABILITIES` is the row the network/crypto
split must reach. `IsRoleCapabilitySafe()` and `ValidateCapabilityMatrix()` are
compile-time/test-checked; every OS backend policy must be reviewable against
exactly one row.

A worker role is bound to its spawner: `SpawnWorker*` generates 16 bytes from
the platform CSPRNG and writes `[role-id u8 | nonce 16]` into an anonymous pipe
before `fork()`. The child reads it from fd 5, closes fd 5, and only then
touches the channel or applies its policy. The record never appears in `argv`,
`environ`, a log line or the protocol channel, so a same-UID process cannot
invoke `--privsep-worker <role> 3` with its own socketpair and adopt the role.
Descriptor contract: 3 = channel, 4 = inherited storage root (OpenBSD),
5 = one-shot identity record, ≥6 closed.

### Data-storage worker (implemented, opt-in)

With `privsep.artifacts=true` the core installs a `DataStorageClient` as the
only writer for three hash-named artifact kinds: the netDb RouterInfo cache,
peer profiles and destination private keys. The worker validates the delegated
root and creates the three `0700` directories before applying the storage
policy; it has no network capability. Names are canonical (44-character I2P
base64 identity hash, or `<52-char b32>.<keyType>`), re-encoded and compared
before use; separators, NUL, `..`, drive colons, symlinked/hardlinked or
foreign-owned targets are rejected, and writes use fixed staging slots plus an
atomic no-follow replace. Startup reads remain direct in this phase.
`PersistentStorage` never falls back to a local write after an installed
writer fails, and an enabled-but-unavailable worker fails closed. Channels are
generation-fenced; a client death/restart bumps the generation, stale replies
are dropped, ambiguous writes are not replayed, and restart uses 250 ms→30 s
backoff (8 attempts in 60 s, stable reset after 60 s) where an exec'ed worker
can be respawned (Linux). OpenBSD has no post-pledge respawn in the core.

### Log worker hardening

The log record text is sanitised with a strict UTF-8 walk (C0 except TAB, DEL
and the C1 block including U+009B become spaces; invalid/truncated sequences
become one `?`; a cut at the 8 KiB bound never splits a sequence and appends a
truncation marker). Known secret keys (`password`, `privatekey`, `private key`,
`passphrase`, `secret`, `token`) have their value replaced with `<redacted>`
before the record is framed. The file sink stops appending at 64 MiB
(`MAX_LOG_SINK_BYTES`) with one stderr notice and no rotation. The client sends
from a snapshot of the channel outside the log mutex, so a wedged worker cannot
hold the router's logging lock for a full send timeout; drop counters remain
bounded and monotonic. A pre-opened log descriptor (`ConfigureLogFd`) is bound
to the configured path by dev/inode identity, owner and `st_nlink == 1` before
`fchmod` or sandboxing, so it cannot be an arbitrary same-UID writable file.
Frames are checked against the shared message registry: a serving endpoint
enforces the per-type descriptor policy and answers an attributable fault with
a typed `Error` (at most eight), and an idle receive timeout never closes the
Windows pipe.

### Windows AppContainer and supervisor (2026-10-26)

Windows has no per-process network/filesystem switch analogous to Landlock.
The documented boundary is an AppContainer: storage/log/crypto/admin run in a
low-privilege container with no `internetClient`, network runs in a container
with `internetClient`/`privateNetworkClientServer`, and only the delegated
write root is granted to the container SID. The supervisor uses a restricted
token because it must spawn children. `WindowsIsolation.cpp` is the single
role authority and rejects any role that combines an Internet capability with a
write root; `SpawnWindowsWorker` refuses a container role without a container,
a container for a non-container role, or a persistent-write role without a
delegated root.

The pipe is message-mode, `PIPE_REJECT_REMOTE_CLIENTS`, protected DACL (service
user, SYSTEM and the exact container SID), `FILE_FLAG_FIRST_PIPE_INSTANCE`, and
one frame per record. `AcceptBound` impersonates the client to read
`TokenUser`/`TokenIsAppContainer`/`TokenAppContainerSid`/`TokenIntegrityLevel`,
requires the spawned PID/session and, for a container role, the exact container
SID, then consumes one `WorkerHello` carrying the role and the 32-byte CSPRNG
nonce before binding the role. `WindowsChannel::Receive` enforces the shared
registry per role and answers one typed `Error` under the eight-violation
budget. A completed-during-cancel record is delivered rather than discarded.
The worker entry (`--privsep-worker`) connects bound to the parent PID/session,
sends the hello, binds its role and never initialises the router.

`WindowsWorkerSupervisor` owns one role, creates the per-generation pipe and
AppContainer identity, spawns and authenticates the child, restarts it under
the shared 250 ms→30 s / 8-in-60 s budget with a monotonic generation, and
terminates the Job Object within a bounded stop. The service drains
`Daemon.stop()` within a non-zero wait hint instead of reporting STOPPED while
the router runs. No daemon path instantiates the supervisor yet, and the
storage/network engines remain in the core; no live Windows worker exists.

### Linux backend hardening (2026-10-26)

- **Peer authentication:** Linux `SO_PEERCRED` is the **socket creator**, so it
  becomes stale when a root core drops a worker to `privsep.user`. Every
  production channel now carries an explicit `PeerExpectation`: the core
  requires the worker uid/gid and the direct child pid, a worker requires its
  own uid (or uid 0 for the delegating root core), and an adopted broker socket
  requires the worker uid. A socket with a pre-queued record is refused at
  adoption. `configured=false` keeps the creator comparison only for raw
  transport tests.
- **Capabilities:** the spawn child sets `PR_SET_NO_NEW_PRIVS` and clears the
  ambient and bounding sets while still privileged (effective/permitted are
  kept so `setgroups`/`setgid`/`setuid` still have CAP_SETGID/CAP_SETUID); after
  the credential drop it calls `capset(0)` and verifies with `capget`, then
  execs. Doing the whole teardown only in the exec'ed worker was a no-op
  (EPERM). `RunWorker` re-asserts `PR_GET_NO_NEW_PRIVS == 1` and an empty
  capability set before any role reads a frame. Ignored signal dispositions are
  reset to `SIG_DFL` in the child.
- **Seccomp/Landlock:** storage denies the whole connection lifecycle and
  process creation, not only `socket`; Landlock handles `EXECUTE` without
  granting it through a writable root; `Storage` restricts reads as well as
  writes to the delegated root. The socket/capability lockdown is installed
  before the configure frame; Landlock (which needs the root) follows.
- **Executable identity:** the frozen path is checked at freeze time (regular,
  owner root/self, no group/world write, non-writable parent directory) and its
  dev/inode/size are re-checked immediately before `execve` on Linux.
- **Role/root pairing:** only `*-inherited` storage roles and the broker accept
  an inherited root; every other role refuses one, and the root must be a
  directory owned by the worker uid without group/world write. `RunWorker`
  closes fd 4 for any other role.
- **Residual:** cgroup-v2 limits and a global worker-admission cap are not
  wired; the empty network namespace is best-effort because it needs
  `CAP_SYS_ADMIN`; a same-uid sibling that already has the worker channel can
  still inject frames in the worker->core direction (the core->worker direction
  is pid-bound); the core itself is not sandboxed.

### Supervised start, worker identity and descriptors (2026-10-26)

The core keeps a wide **startup** pledge only until every allowed worker is
running: `stdio rpath wpath cpath fattr inet dns unix flock recvfd proc exec`
plus `unveil`/`mcast`/`id` when configured. `recvfd` is present in the startup
set because pledge can only remove promises; without it the runtime `recvfd`
would be silently ignored and the first supervised worker handoff would abort
the core. The **runtime** set is rebuilt from the same table and startup fails
if it is not a strict subset. It never contains `exec`, `id`, `chown` or
`unveil`.

`privsep.user`/`privsep.group` select the worker identity. Names are resolved
once before the startup pledge (bounded `getpwnam_r`/`getgrnam_r`), uid 0 is
rejected, and the pair is published process-wide. The forked child calls
`setgroups(0,nullptr)`, `setgid`, `setuid` before `execve` and verifies the
real/effective ids and an empty supplementary set; a root core with any
isolation feature enabled and no configured identity fails startup with an
explicit diagnostic. The core itself is not chrooted or dropped.

A worker is bound to its spawner by a 25-byte hello
`[role u8 | nonce 16 | uid u32 | gid u32]` on fd 5, written by the child after
the drop. `RunWorker` requires the role to match `argv`, the uid/gid to match
`geteuid()/getegid()`, a non-zero uid and one exact record (a trailing byte or
an open writer is rejected), then closes fd 5. A channel peer may be the same
uid or, for an unprivileged worker, uid 0 (the root core that delegates the
role); the channel is an anonymous `socketpair`, never a named socket.

Descriptor contract: 3 = channel, 4 = inherited storage root (only for
`*-inherited` roles), 5 = one-shot hello, 0/1/2 = `/dev/null`, >=6 closed. A
role without a root closes fd 4 explicitly; stdio is pinned even for a
directly invoked worker, so a terminal, rc.d log pipe or activation socket is
never writable. The executable path is frozen from `argv[0]`/`realpath` before
the sandbox (absolute, regular, owner root/self, no group/world write) and is
never resolved through `$PATH` or `/proc`.

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
- `pledge("stdio rpath wpath cpath")` with `unix`/`recvfd` only when the
  platform delivers a descriptor after exec; **no** `inet`, `dns`, `fattr`,
  `flock` or `sendfd`.
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
  records. On OpenBSD it pre-resolves the executable, unveils exactly that path
  with `x`, locks the list and applies `pledge("stdio proc exec")`; it has no
  `unix`, no pathname read/write and never opens the log file. The storage
  broker uses `pledge("stdio proc exec sendfd")` with the same single-`x`
  unveil. It retains fork/exec only to restart the log child, which is why no
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

Current sandbox: `LinuxSandbox` implements full log/storage/network role
profiles. A full profile drops effective/permitted/ambient capabilities and the
capability bounding set, optionally creates an empty network namespace,
installs Landlock filesystem write restrictions with explicit writable paths,
and installs a seccomp-BPF socket filter. Storage denies every new socket; log
denies AF_INET/AF_INET6 while retaining AF_UNIX; networking keeps sockets but
has no persistent-write rule. The log worker applies its full profile. The supervisor uses only `ApplyLinuxSupervisorSandbox`
(no-new-privs and capability drop) because inherited Landlock/seccomp/netns
domains cannot be relaxed for children; each child applies its own role
profile. `LinuxCgroupV2Limits` can apply `memory.max`/`pids.max` to an
explicitly delegated cgroup v2 and move the current process there.

### Linux kernel floors and failure modes

| Mechanism | Minimum / condition | Behaviour on an older or restricted kernel |
|---|---|---|
| `SOCK_SEQPACKET` / `SO_PASSCRED` | Linux 2.6.x, `CONFIG_UNIX` | `CreatePair` fails; there is no stream fallback |
| `SOCK_CLOEXEC`, `MSG_CMSG_CLOEXEC` | 2.6.27 / 2.6.23 | Explicit `FD_CLOEXEC` pass runs; a failure is fatal |
| `close_range` | 5.9 | Bounded `close()` fallback; soft RLIMIT_NOFILE is used, with a documented 1,048,576 cap when it is `RLIM_INFINITY` |
| `seccomp` filter mode | 3.5, `CONFIG_SECCOMP_FILTER`; project filters x86_64/aarch64 | Unsupported architecture is a fatal storage/log sandbox error |
| `pidfd_open` / `pidfd_send_signal` | 5.3 / 5.5 | `waitpid`/`kill` fallback; no pidfd guarantee |
| Landlock filesystem | ABI 1 (5.13) | Storage rejects if Landlock is required; no path-write fallback |
| Landlock network TCP | ABI 4 | Seccomp still filters AF_INET/AF_INET6 for non-network roles |
| Landlock network UDP | ABI 10 | Network-role UDP is not Landlock-confined; this is a documented residual, not a silent claim |
| `cgroup.kill` | 5.14 | Not currently wired; PID-scoped stop only |
| `SO_PEERPIDFD` | 6.5 | Not assumed; peer credentials remain uid/gid-bound |

Landlock network mediation starts at ABI 4 (TCP) and ABI 10 (UDP); older
kernels rely on seccomp for storage/log and are rejected for storage profiles
because Landlock is required. User namespaces are not created automatically;
`unshare(CLONE_NEWNET)` is optional and falls back to seccomp when EPERM.
cgroup delegation is deployment-specific and no call site currently applies the
cgroup helper. The sandbox does not protect against same-UID sibling processes
or a compromised core, and network/storage backends are still pending. memfd
sealing is not used for IPC: the versioned `SOCK_SEQPACKET` frame and one
`SCM_RIGHTS` descriptor are already bounded, so memfd would add descriptor
lifecycle and no isolation benefit.

Descriptor hygiene is best-effort on old Linux kernels: `close_range(2)` is
used when available; otherwise the child walks to the soft `RLIMIT_NOFILE`
(or a 1,048,576 cap when the limit is infinite). A descriptor above that
fallback cap could still leak on a pre-5.9 kernel. Pre-opened accepted sockets
or writable files are not mediated by Landlock at the write/open point, so
this is treated as a hardening residual, not only a tidiness issue.

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

Workers are spawned with a `CreateRestrictedToken` token that deletes every
parent privilege and adds `LUA_TOKEN` for elevated parents, an optional Job
Object (`KILL_ON_JOB_CLOSE`, `DIE_ON_UNHANDLED_EXCEPTION`, process/job memory
and basic UI restrictions) and mitigation attributes; `bInheritHandles=FALSE`
and the pipe name is passed on the command line, so no handle is inherited or
duplicated. Canceled overlapped I/O is drained before buffers are released and
timeouts close the channel.
AppContainer/LPAC is probed through documented `userenv.dll` exports but is not
silently substituted; requesting it fails closed. No undocumented ALPC API is
used. `ncalrpc` remains an evaluated alternative, not a second implementation.

The Windows storage/network worker backends and typed operations remain
pending. A restricted token and Job Object are resource/process controls, not a
complete network/filesystem sandbox; AppContainer/LPAC or an equivalent
enforced boundary is required before claiming effective Windows separation. See
[windows-worker-ipc.md](windows-worker-ipc.md).

## Android

The app uses two same-UID processes: the default process hosts the Activity,
ViewModel, service client and (today) the router service with JNI; the
`:torrent_storage` process hosts the SAF payload broker. The broker has no
network capability (seccomp `no_new_privs` plus a socket/connect deny) and owns
all `ParcelFileDescriptor`s; no descriptor crosses Binder. This is a real
heap/crash/network containment boundary, not a privilege boundary: both
processes share the app UID, data directory, keystore and INTERNET permission.
An `isolatedProcess` cannot replace the router or broker because it loses both
INTERNET and persisted SAF grants. A separate `:i2pd_router` process for the
router/JNI is the next slice (`ANDROID-026`) and needs a closed `IRouterControl`
AIDL; networking cannot be split into a separate Android permission domain.
The Activity is not the router owner: `RouterService` is a foreground service
with `stopWithTask=false`, a persisted run intent (`run_phase`/graceful
deadline/generation) and a boot/package-replace restart path. Do not claim
global Android privilege separation.

## Linux build headers and Landlock ABI

The compile-time Linux UAPI version and the running kernel's Landlock ABI are
independent. `LinuxSandbox.cpp` uses the stable two-u64 extensible ruleset
layout, with runtime-gated REFER/TRUNCATE/TCP rights and compile-time assertions
against constants and offsets exposed by installed headers. Missing base
Landlock headers produce an explicit sandbox-installation error. They never
convert requested confinement into success. UDP rights remain conditional on
available build-header declarations. Old/new header and kernel combinations
are pending manual CI; see [the static build audit](../static-build-audit.md).

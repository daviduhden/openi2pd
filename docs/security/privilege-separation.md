# Privilege separation architecture

## Current state (implemented in this tree)

The first production-shaped separation is **logging**:

- The router process prepares the initial supervisor/log worker before
  `Log::Start()` and before transport/netDb threads. On OpenBSD this happens
  while the startup pledge still has `proc`/`exec`; the runtime policy then
  drops both.
- The worker is a separate process created with `fork()` + `execv()` of the
  same executable (`--privsep-worker log <fd>`), connected by an anonymous
  `AF_UNIX/SOCK_SEQPACKET` socketpair.
- The router never opens the configured log file. It sends structured records
  over IPC; the worker opens/writes the file or syslog.
- A separate supervisor process owns and restarts the log worker with a
  bounded monitor. Outside the OpenBSD runtime sandbox the core can replace a
  dead supervisor; under that sandbox it has no `exec` and does not. The
  supervisor owns the log child and forwards structured records. It is a real
  process boundary; network/storage/crypto workers are not yet under it.
- If restart attempts are exhausted, log records are dropped. The core never
  reopens the log file and never falls back to a weaker transport.
- Linux evidence from a live run: the core process had no `router.log`
  descriptor; the worker had the only write descriptor.
- On OpenBSD the supervisor/log pair is spawned while the startup pledge still
  has `proc`/`exec`. The parent then installs its runtime policy without `exec`;
  `proc` is retained only for bounded supervisor reaping/signalling. The
  supervisor applies `pledge("stdio unix proc exec")` after
  receiving configuration and pre-resolving the executable; the log worker
  applies its own policy after `execve` (`unveil` + `pledge`, with no
  dependency on inherited sandbox state). Failure is fatal.
- On Linux the log worker and supervisor apply `LinuxSandbox`: no-new-privs,
  capability/bounding-set drop, Landlock write restrictions, seccomp socket
  filters and optional empty network namespace. Storage/network worker
  backends are still pending. Parent death is detected through EOF on the
  channel rather than `PR_SET_PDEATHSIG`.
- The worker descriptor is passed as `--privsep-worker log <fd>` and the worker
  verifies that the descriptor is an inherited socket with a matching peer UID.
  The post-fork child uses only async-signal-safe calls before `execv`.
- On Windows, `WindowsIpc` implements a message-mode local Named Pipe with a
  protected user/SYSTEM DACL, `PIPE_REJECT_REMOTE_CLIENTS`, peer token
  SID/session checks, restricted-token/Job Object helpers and mitigation
  attributes. The worker is spawned suspended with no inherited handles and
  reconnects by pipe name. AppContainer/LPAC is probed but requested policies
  fail closed. No worker backend is wired into the Windows daemon yet.

## Target architecture

```
supervisor
 ├── network worker      sockets only (none: wpath/cpath)
 ├── storage worker      filesystem only (none: inet/dns)
 ├── router core         computation/IPC only (none: inet/wpath)
 ├── crypto worker       key use only (none: inet/wpath)
 ├── log worker          log file/syslog only (none: inet)
 └── admin IPC/TUI       separate client; bounded reads + two atomic admin writes
```

The target invariants are:

1. Network-capable process cannot modify persistent files.
2. Persistence-capable process cannot open Internet sockets.
3. Supervisor never combines network and persistent-write capabilities.
4. Router core has neither direct network nor persistence.
5. Per-worker identity and minimum OS permissions.
6. No unnecessary inherited descriptors.
7. IPC grants no additional capability implicitly.
8. Administrative operations cannot bypass the policy.
9. Worker failure does not automatically compromise peers.
10. No silent fallback to the monolithic model on OpenBSD.

## Capability matrix — current tree

| Component | Network | Persistent write | Long-term keys | Untrusted input | Status |
|---|---|---|---|---|---|
| `i2pd` core | Yes | Yes | Yes | Yes (packets/I2NP) | **Monolithic; target refactor pending** |
| Log worker | No | Log file/syslog | No | Structured log records | **Implemented / runtime tested on Linux** |
| Supervisor process | No | No | No | Worker lifecycle + log records | **Implemented / static; fork/restart tests pending** |
| TUI client | No (local socket) | No | No | IPC responses | Implemented |
| TUI server thread | No | No | No | Local IPC | In daemon process |
| Storage worker | — | — | — | — | **Protocol/capability pending; core still persists directly** |
| Network worker | — | — | — | — | **Protocol/capability pending; core still owns sockets** |
| Crypto worker | — | — | — | — | **Protocol/capability pending; core still owns keys** |
| Supervisor | No | No | No | Worker exit status/channel state | **Separate process for the log worker; other workers pending** |

This is an **intermediate** stage, not the target architecture. The monolithic
core still performs network I/O, persistence and key operations; only logging
is split. The supervisor process now satisfies the process boundary for the log worker,
but the core still combines network, persistence and key operations. It does
not protect against a core compromise and is not full privilege separation. See `TODO.md` `PRIVSEP-005` and `PRIVSEP-006`/`007`.

## Why not split everything at once

I2P session state, tunnel cryptography, netDb persistence and the event loop are
tightly coupled to the current in-process singletons. Splitting a state machine
between processes without defining ownership and event ordering risks nonce
reuse, lost retransmissions and inconsistent tunnel state. The staged migration
therefore starts with a component whose contract is simple and whose isolation
is independently testable: logging.

## Implemented components

- `privsep/Protocol.h` — versioned, length-prefixed message format, bounded
  fields, log record/config codecs.
- `privsep/Channel.{h,cpp}` — `AF_UNIX/SOCK_SEQPACKET`, `SOCK_CLOEXEC`,
  `SO_PEERCRED`/`getpeereid`, poll timeouts.
- `privsep/Worker.{h,cpp}` — fork/exec, async-signal-safe child setup,
  descriptor closing, eager pidfd on Linux, single-path reaping, bounded
  shutdown with SIGTERM/SIGKILL escalation.
- `privsep/LogWorker.{h,cpp}` — isolated writer, configure-once policy,
  OpenBSD policy from `OpenBSDSandbox`, Linux no-new-privs, sink reattach for
  restarts.
- `privsep/OpenBSDSandbox.{h,cpp}` — per-role pledge/unveil policy builder and
  pre-opened descriptor validation.
- `privsep/OpenBSDImsg.{h,cpp}` — OpenBSD `imsg` adapter over the versioned
  frame (CI-tested; not yet the production log transport).
- `privsep/RunWorker.{h,cpp}` — worker entrypoint with explicit channel fd
  argument, socket-fd validation and euid-0 refusal.
- `daemon/i2pd.cpp` — internal `--privsep-worker log <fd>` entry before daemon
  init.
- `daemon/Daemon.cpp` — spawn/configure/stop log worker when
  `privsep.enabled=true`, plus the shutdown-aware bounded restart monitor
  (joined before the sink is released).
- `libi2pd/Log.*` — external sink path (no local file write).

## Configuration

```ini
[privsep]
# Enable the isolated log worker (default: false while the remaining
# components are migrated).
enabled = true
log = true
```

`privsep.enabled` is intentionally **off by default** until network, storage and
core separation are complete; enabling it is safe for the log component but
does not yet satisfy all ten invariants. Do not describe it as full privilege
separation.

## OpenBSD policy

Log file worker (after config):

```text
unveil("/etc/localtime", "r")   # timestamp formatting only
unveil(logfile, "rwc")
unveil(NULL, NULL)
pledge("stdio unix rpath wpath cpath fattr", NULL)
```

Pre-opened log descriptor worker:

```text
unveil("/etc/localtime", "r")
unveil(NULL, NULL)
pledge("stdio unix rpath", NULL)   # no wpath/cpath/fattr
```

Syslog worker:

```text
unveil("/dev/log", "w")
unveil(NULL, NULL)
pledge("stdio unix", NULL)
```

Foreground stdout worker (empty path, not syslog):

```text
pledge("stdio unix", NULL)
```

Supervisor (executable resolved before this policy):

```text
pledge("stdio unix proc exec", NULL)
```

**Current integration limit:** `DaemonUnix::start()` calls
`PreparePrivsepWorkers()` before applying its runtime sandbox. The startup
pledge contains `proc`/`exec` only for that window; the runtime policy drops
`exec` and retains `proc` only while `privsep.log` is active, for bounded
supervisor reaping/signalling. The core does not exec a replacement supervisor
after the sandbox is installed. The supervisor owns bounded log-worker
replacement, and the log worker installs its own `/dev/log` rule for syslog.

The core policy remains broad because storage, network and crypto migration is
incomplete; this is still not full privilege separation. The parent no longer
unveils the logfile or `/dev/log` when the log worker owns the sink, so the core
does not retain the log write capability. Runtime errors after sandbox lock may
be available only through the structured sink/stderr.

`privsep/OpenBSDSandbox.{h,cpp}` defines the per-role policy profiles; storage,
network, crypto and admin backends are pending. `privsep/OpenBSDImsg.{h,cpp}`
is an OpenBSD `imsg` adapter for the versioned frame and descriptor passing,
tested in the manual OpenBSD job; production worker channels remain
`SOCK_SEQPACKET` until the backends are migrated.

## Remaining implementation stages

1. Storage worker with typed, allowlisted operations; migrate `FS`/netDb/profile
   writes; remove `wpath/cpath` from core.
2. Network worker owning transport sockets; core receives normalized session
   events; no private keys in the network process.
3. Dedicated supervisor process with pidfd/`waitpid`, restart policy and
   channel ownership. The log worker is under it; the OpenBSD supervisor has
   its own policy and pre-resolved executable. Network/storage/crypto workers
   and core replacement on supervisor failure remain pending.
4. Crypto worker for long-term signatures/key agreement if measurements show
   viable latency; otherwise document the boundary.
5. Linux seccomp/Landlock is implemented for log/supervisor and policy-ready
   for storage/network; the worker backends and cgroup/namespace deployment
   remain pending. Windows Named Pipe/token/Job primitives are implemented and
   tested statically; AppContainer/LPAC, the worker backends and daemon
   integration remain pending. Android process constraints also remain pending.
6. Negative capability tests and OpenBSD dynamic validation.

See `docs/security/process-capabilities.md`,
`docs/security/ipc-protocol.md`, `docs/security/threat-model.md`.

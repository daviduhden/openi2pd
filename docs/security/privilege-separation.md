# Privilege separation architecture

## Current state (implemented in this tree)

The first production-shaped separation is **logging**:

- The router process spawns the initial log worker before `Log::Start()` and
  before transport/netDb threads. Restarts fork from the multi-threaded router,
  so the post-fork child restricts itself to async-signal-safe calls before
  `execv()`.
- The worker is a separate process created with `fork()` + `execv()` of the
  same executable (`--privsep-worker log <fd>`), connected by an anonymous
  `AF_UNIX/SOCK_SEQPACKET` socketpair.
- The router never opens the configured log file. It sends structured records
  over IPC; the worker opens/writes the file or syslog.
- A single bounded in-process monitor thread reaps a dead log worker and
  restarts it with exponential backoff (500 ms doubling to 30 s, at most eight
  consecutive attempts, reset after 60 s of stable operation). It is a restart
  foundation, **not** a separate supervisor process and not a general
  multiprocess supervisor.
- If restart attempts are exhausted, log records are dropped. The core never
  reopens the log file and never falls back to a weaker transport.
- Linux evidence from a live run: the core process had no `router.log`
  descriptor; the worker had the only write descriptor.
- On OpenBSD the worker applies `unveil(logfile)` + `pledge("stdio rpath wpath
  cpath fattr")` (or `stdio unix` for syslog, `stdio` for foreground stdout)
  *after* receiving configuration and before writing. Failure is fatal. Because
  the parent applies its own pledge before the spawn, this worker path requires
  the parent sandbox to be explicitly disabled; see "Current integration
  limit" below.
- On Linux the worker sets `PR_SET_NO_NEW_PRIVS`; it still runs with the
  router's UID. seccomp/Landlock are not yet applied. Parent death is detected
  through EOF on the channel rather than `PR_SET_PDEATHSIG`.
- The worker descriptor is passed as `--privsep-worker log <fd>` and the worker
  verifies that the descriptor is an inherited socket with a matching peer UID.
  The post-fork child uses only async-signal-safe calls before `execv`.

## Target architecture

```
supervisor
 ├── network worker      sockets only (none: wpath/cpath)
 ├── storage worker      filesystem only (none: inet/dns)
 ├── router core         computation/IPC only (none: inet/wpath)
 ├── crypto worker       key use only (none: inet/wpath)
 ├── log worker          log file/syslog only (none: inet)
 └── admin IPC/TUI       separate client, read-only/authorized operations
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
| TUI client | No (local socket) | No | No | IPC responses | Implemented |
| TUI server thread | No | No | No | Local IPC | In daemon process |
| Storage worker | — | — | — | — | **Not implemented** |
| Network worker | — | — | — | — | **Not implemented** |
| Crypto worker | — | — | — | — | **Not implemented** |
| Supervisor | No | No | No | Worker exit status/channel state | **In-process log restart monitor only; no separate process** |

This is an **intermediate** stage, not the target architecture. The monolithic
core still performs network I/O, persistence and key operations; only logging
is split. The restart monitor is a thread inside the router, so it does not
satisfy the supervisor-process invariant and cannot protect against a core
process compromise. See `TODO.md` `PRIVSEP-005` and `PRIVSEP-006`/`007`.

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
  OpenBSD pledge/unveil, Linux no-new-privs, sink reattach for restarts.
- `privsep/RunWorker.{h,cpp}` — worker entrypoint with explicit channel fd
  argument and socket-fd validation.
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
pledge("stdio rpath wpath cpath fattr")
```

Syslog worker:

```text
pledge("stdio unix")
```

Foreground stdout worker (empty path, not syslog):

```text
pledge("stdio")
```

**Current integration limit:** the router applies its runtime pledge and
unveil rules in `DaemonUnix::start()` *before* `Daemon_Singleton::start()`
spawns the worker with `fork()` + `execv()`. The runtime policy intentionally
has no `proc` or `exec` promise, so spawning the worker at that point would
abort the process. The router now refuses to start when `privsep.enabled` and
`privsep.log` are both true and either `openbsd.pledge_enabled` or
`openbsd.unveil_enabled` is enabled. It does not silently disable the worker
or the sandbox. Run the worker only with the OpenBSD sandbox explicitly
disabled, or keep `privsep.log = false`.

The parent/core policy is unchanged and still broad; this is a pending item.
The same refusal applies to a syslog log worker, which would otherwise need an
additional unveil rule for `/dev/log` in the child.

The parent-core syslog destination now unveils `/dev/log` with `w`; `unix`
alone permits the socket calls but not the filesystem lookup performed by
`connect(2)`.

## Remaining implementation stages

1. Storage worker with typed, allowlisted operations; migrate `FS`/netDb/profile
   writes; remove `wpath/cpath` from core.
2. Network worker owning transport sockets; core receives normalized session
   events; no private keys in the network process.
3. Dedicated supervisor **process** with pidfd/`waitpid`, restart policy and
   channel ownership. The current in-process thread restarts only the log
   worker; it does not supervise other components or survive a core crash.
4. Crypto worker for long-term signatures/key agreement if measurements show
   viable latency; otherwise document the boundary.
5. Linux seccomp/Landlock, Windows restricted tokens/AppContainer/RPC, Android
   process constraints.
6. Negative capability tests and OpenBSD dynamic validation.

See `docs/security/process-capabilities.md`,
`docs/security/ipc-protocol.md`, `docs/security/threat-model.md`.

# Privilege separation architecture

## Current state (implemented in this tree)

The first production-shaped separation is **logging**:

- The router process spawns a log worker before `Log::Start()` and before
  transport/netDb threads.
- The worker is a separate process created with `fork()` + `execv()` of the
  same executable (`--privsep-worker log`), connected by an anonymous
  `AF_UNIX/SOCK_SEQPACKET` socketpair.
- The router never opens the configured log file. It sends structured records
  over IPC; the worker opens/writes the file or syslog.
- Linux evidence from a live run: the core process had no `router.log`
  descriptor; the worker had the only write descriptor.
- On OpenBSD the worker applies `unveil(logfile)` + `pledge("stdio rpath wpath
  cpath fattr")` (or `stdio unix` for syslog) *after* receiving configuration
  and before writing. Failure is fatal.
- On Linux the worker sets `PR_SET_NO_NEW_PRIVS`; seccomp/Landlock are not yet
  applied.

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
| Supervisor | — | — | — | — | **Not implemented as separate process** |

This is an **intermediate** stage, not the target architecture. The monolithic
core remains a known residual risk; see `TODO.md` `PRIVSEP-*`.

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
- `privsep/Worker.{h,cpp}` — fork/exec, descriptor closing, pidfd supervision
  on Linux, shutdown/termination.
- `privsep/LogWorker.{h,cpp}` — isolated writer, OpenBSD policy, Linux
  no-new-privs.
- `privsep/RunWorker.{h,cpp}` — worker entrypoint.
- `daemon/i2pd.cpp` — internal `--privsep-worker` entry before daemon init.
- `daemon/Daemon.cpp` — spawn/configure/stop log worker when
  `privsep.enabled=true`.
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
unveil(logfile, "rwc")
unveil(NULL, NULL)
pledge("stdio rpath wpath cpath fattr")
```

Syslog worker:

```text
pledge("stdio unix")
```

The parent/core policy is unchanged and still broad; this is a pending item.

## Remaining implementation stages

1. Storage worker with typed, allowlisted operations; migrate `FS`/netDb/profile
   writes; remove `wpath/cpath` from core.
2. Network worker owning transport sockets; core receives normalized session
   events; no private keys in the network process.
3. Supervisor process with pidfd/`waitpid`, restart backoff, channel policy.
4. Crypto worker for long-term signatures/key agreement if measurements show
   viable latency; otherwise document the boundary.
5. Linux seccomp/Landlock, Windows restricted tokens/AppContainer/RPC, Android
   process constraints.
6. Negative capability tests and OpenBSD dynamic validation.

See `docs/security/process-capabilities.md`,
`docs/security/ipc-protocol.md`, `docs/security/threat-model.md`.

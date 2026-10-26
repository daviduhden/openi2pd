---
name: openi2pd-privsep
description: Use when changing openi2pd supervisor/worker architecture, internal IPC framing, capability handoff, process lifecycle or privilege-separation policy, excluding platform-specific sandbox details owned by the OpenBSD and Linux/Windows skills.
---
<!--
See the LICENSE file at the top of the project tree for copyright
and license details.
-->

# openi2pd privilege separation

## Activate when

Use this skill for the supervisor/worker model, typed internal messages,
descriptor/capability handoff, restart and shutdown ordering, or the
capability boundary between core, logging, storage, network and crypto
components. Use `openi2pd-openbsd` or `openi2pd-linux-windows` for the actual
sandbox/IPC primitives.

Read [`../../../AGENTS.md`](../../../AGENTS.md) first. The current tree is
**not** fully privilege-separated. The log worker and supervisor have a real
process boundary with historical Linux runtime evidence. The opt-in torrent
payload-storage worker and the opt-in router-storage worker are separate
processes implemented by static analysis, with negative tests pending. The
Android SAF broker is a separate app-private process, not router-wide
separation. Do not describe the core or Android router service as fully
separated.

## Inspect first

- `privsep/Protocol.h`, `Channel.*`, `Worker.*`, `Supervisor.*`,
  `RunWorker.*`, `LogWorker.*`, `RouterStorage.*`, `TorrentStorage.*`.
- `daemon/i2pd.cpp`, `daemon/Daemon.*`, `daemon/UnixDaemon.cpp` and the
  platform server/worker entry points.
- `libi2pd/Log.*`, `libi2pd_client/TorrentStorageWorker.*` and any callback
  that crosses the process boundary.
- [`../../../docs/security/privilege-separation.md`](../../../docs/security/privilege-separation.md)
  and [`../../../docs/security/ipc-protocol.md`](../../../docs/security/ipc-protocol.md).
- The current `PRIVSEP-*` rows in `TODO.md`; many are still implementation
  tasks, not test-only work.

## Reference sources

- [`../../../docs/security/privilege-separation.md`](../../../docs/security/privilege-separation.md)
  — current capability matrix, target invariants and remaining stages.
- [`../../../docs/security/ipc-protocol.md`](../../../docs/security/ipc-protocol.md)
  — transport, frame, message types, authorization and failure behavior.
- [`../../../docs/security/process-capabilities.md`](../../../docs/security/process-capabilities.md)
  — per-process capability expectations.
- [`../../../docs/security/threat-model.md`](../../../docs/security/threat-model.md)
  — assets and failure scenarios.
- [`references/worker-checklist.md`](references/worker-checklist.md) — protocol
  and lifecycle review checklist.

## Reconnaissance

Launch at least one read-only `explore` subagent for the supervisor/worker
lifecycle and one for the protocol and callers. If subagents are unavailable,
report that limitation; do not substitute local execution. Ask for:

1. who creates, authorizes, descriptors and reaps each child;
2. every message type, direction, request-id rule and bound;
3. inherited descriptors and capability handoff before/after sandboxing;
4. restart, timeout, EOF and partial-write behavior;
5. every fallback to the monolithic path;
6. tests that exercise negative capabilities and cross-UID/peer checks.

## Parallel implementation

- Split by protocol message family or independent worker backend. Do not let
  two agents edit `Protocol.h`, `Channel.*`, `Worker.*`, `Daemon.*`,
  `RouterStorage.*` or `TorrentStorage.*` concurrently.
- Platform mechanisms belong to `openi2pd-openbsd` and
  `openi2pd-linux-windows`; this skill owns the transport-neutral contract.
- The coordinator reviews shared interfaces and any change to the daemon
  startup order.
- Keep storage/network/crypto migration separate from logging; do not bundle
  an unrelated worker into a logging fix.

## Conventions

- Apply the [source documentation policy](../../../AGENTS.md#source-documentation-policy)
  to IPC authority, descriptor ownership, shutdown and worker boundaries.

- Version the protocol; reject unknown versions and types. No downgrade or
  “best effort” mode.
- Bound every frame, field, queue, request id and timeout before allocation.
- Use operation-specific descriptors, never a generic `write_file(path,data)`
  or arbitrary descriptor-passing API.
- Verify inherited descriptor type and peer credentials; reject manual or
  cross-UID invocation.
- Apply the child sandbox before accepting requests. Never send a directory
  descriptor after `pledge`; use the established inherited-root or
  supervisor-bootstrap design.
- No silent fallback to core file/network access when isolation was requested.
  Fail closed and report the failure.
- Preserve shutdown ordering: stop intake, drain bounded work, stop children,
  reap, release descriptors. Do not replay ambiguous writes.
- Document the current capability matrix honestly; implementation of one
  worker is not global separation.

## Known issues and decisions

- Do not claim global privilege separation. The log worker has historical Linux
  evidence; payload storage, router storage and the Android SAF broker are
  static boundaries pending CI/device validation. Network, router and verifier
  workers do not exist.
- Worker channels are descriptor 3; OpenBSD storage inherits the authorized root
  at fd4 and installs its own sandbox. Never send a directory FD after pledge.
- IPC bounds and typed errors are normative in
  `docs/security/ipc-protocol.md`; raising frame/text limits requires a threat
  model and migration review.
- Negative capability, storage failure, broker restart and sanitizer tests
  remain pending; see [`references/worker-checklist.md`](references/worker-checklist.md).
- `Channel::ReceiveWithFd` must keep ancillary parsing, the empty-record
  credential check and the empty-record budget inside the same retry loop as
  `poll`/`recvmsg`; a zero-length SOCK_SEQPACKET record is not terminal unless
  a hangup/error event accompanies it.

- `privsep/Capabilities.h` is the single role×capability authority: no target
  role may set `ExternalNetwork` **and** `PersistentWrite`. The core's legacy
  row keeps the write bit on purpose; never delete it to make the matrix
  "green" before the network/crypto split. A policy builder existing is not a
  backend.
- A worker role is bound to its spawner by `[role-id | 16-byte CSPRNG nonce]`
  on a pre-fork pipe, read from fd 5 and closed before the channel or sandbox.
  Never move that record to argv/environ/logs/the protocol channel. Descriptor
  contract: 3 channel, 4 inherited root (OpenBSD), 5 identity, ≥6 closed.
- `privsep.artifacts` delegates the netDb RouterInfo cache, peer profiles and
  destination private keys to the data-storage worker with canonical names,
  staging slots and an atomic no-follow replace; the installed writer is the
  only path and a failure must never fall back to a local write. Startup reads
  remain direct. Garlic tags, address book, torrent state, bootstrap keys and
  config are still core-written — do not describe the core as write-free.
- Storage clients are generation-fenced and restartable; never replay an
  ambiguous write, and return `Busy` at request-id exhaustion instead of
  killing the client. OpenBSD cannot respawn an exec'ed worker after the
  runtime pledge, so a lost data worker is terminal there until restart.
- Log records are sanitised (UTF-8/C1), secrets are redacted by key name, the
  file sink stops at 64 MiB without rotation, and the client sends outside the
  logging mutex. The pre-opened descriptor is the production file sink:
  `Configure/Reattach` passes it over IPC and `ValidateLogDescriptorIdentity`
  checks dev/inode/owner/`st_nlink == 1` before use. Rotation itself is still
  absent; do not describe the sink as missing.

- `privsep/Protocol.h` is the single `MessageSpec` authority (min version,
  direction, receivers, per-type ceiling, descriptor policy). Do not add a
  second role switch in a worker, backend or test; change the row. A worker
  receives request-direction types only; the requesting side receives replies
  only. `Pong` is the worker-to-core reply; a non-core endpoint may receive
  `Error` and `TorrentStorageSupervisorReply` (the supervisor/broker issue
  their own requests).
- Versioning is per type: `DataStorage*` requires v3, `Cancel` requires v4,
  `PROTOCOL_VERSION = 5` adds the Windows `WorkerHello` spawn proof, and a
  newer type in an older header is `UnsupportedVersion`. Never reinterpret it
  or fall back to the 64 KiB frame maximum for a per-type ceiling.
- Serving endpoints use `EnforcementPolicy::Endpoint`: registry descriptor
  presence/kind, a typed `Error` for an attributable fault, at most 8 tolerated
  faults. A frame is attributable only with magic+version+length agreement;
  credential failures stay fatal. A wrong-role frame is a bounded refusal, not
  a worker exit; a duplicate `ConfigureLog` is a refusal too.
- `Cancel` is advisory. Never treat it as a rollback, never replay an ambiguous
  request, and drain the outstanding reply. `Busy` is not a poison state.
- An idle receive timeout must never close a channel (the Windows pipe used to
  do so). A pre-opened log descriptor must be bound to the configured path by
  dev/inode, owner and single link count before use.
- `imsg` cannot carry a frame above 16 KiB (the OpenBSD adapter owns that
  ceiling); no explicit version-negotiation handshake exists yet. Do not claim
  either as implemented.
- The service manager signals the main router process only; workers are
  stopped inside the daemon's ordered stop. A group/cgroup-wide stop bypasses
  that ordering. The 600 s drain/660 s stop budgets and per-manager values are
  owned by `docs/service-management.md` and `openi2pd-packaging`; do not
  restate them here.
- New tests use `i2pd_add_test()`/the explicit `add_test()` contract: label,
  `TIMEOUT` (120 s, 300 s under sanitizers), `SKIP_RETURN_CODE 77` instead of a
  passing skip, and `RUN_SERIAL` for fork/sandbox/storage workers. Workflow
  guards are owned by `openi2pd-ci`.

- OpenBSD worker identity is resolved once (`privsep.user`/`privsep.group`,
  bounded NSS, uid 0 rejected) and dropped in the forked child with
  `setgroups(0,nullptr)`, `setgid`, `setuid` plus verification. A root core
  with isolation and no configured identity fails startup; it is not silently
  degraded. The core itself is not chrooted or dropped.
- The hello record is 25 bytes `[role|nonce16|uid|gid]` written after the drop
  on fd 5; require exact length, role/uid/gid agreement and uid != 0. Descriptor
  contract: 3 channel, 4 inherited root, 5 hello, 0/1/2 `/dev/null`, >=6 closed;
  close fd4 for roles without a root. The executable is frozen from
  `argv[0]`/`realpath`, never `$PATH` or `/proc`.
- A worker accepts a channel peer with its own uid or, when unprivileged, uid 0
  (the delegating root core). A delegated root must be owned by the worker uid.
  `AdoptValidated` on a broker-forwarded socket uses the worker uid.
- OpenBSD supervisor/broker pledge is `stdio proc exec` / `stdio proc exec
  sendfd` with an x-only unveil of the frozen executable and no `unix`/`recvfd`.
  Keep `OPENBSD_PLEDGE_EXECPROMISES = nullptr`; the startup set must be a
  superset of the runtime set. No execpromises, no inherited unveil assumption.

- Linux channels use a `PeerExpectation` because `SO_PEERCRED` is the creator's
  snapshot. Set it in `CreatePair`/`Adopt`/`AdoptValidated`/spawn: core side
  worker uid/gid + child pid, worker side own uid or root, adopted broker socket
  worker uid with an empty receive queue.
- Drop capabilities in the spawn child before `setuid`/`execve`; verify
  `no_new_privs` and an empty capability set in `RunWorker`. Reset ignored
  signals. Do not reintroduce a post-drop-only `capset`.
- Linux storage seccomp denies connection/exec/fork syscalls and Landlock denies
  `EXECUTE`; install the socket/capability lockdown before the configure frame.
  Only `*-inherited` roles/broker accept a root; fd 4 is closed otherwise.
- cgroup-v2 limits, global admission, supervisor-status IPC, unified stop/restart
  budgets and worker-side core-pid binding remain open (`PRIVSEP-031`).

- Windows worker IPC is a message-mode Named Pipe authenticated at accept by
  PID/session/user SID/AppContainer SID plus a `WorkerHello` nonce; the role is
  bound only after that proof, then `WindowsChannel::Receive` enforces the
  shared registry. `Accept()` is test-only. A completed-during-cancel read is
  delivered, not dropped.
- Windows separation is AppContainer-based: low-privilege containers without
  `internetClient` for storage/log/crypto/admin, an Internet-capable container
  for network, and no role with both an Internet capability and a write root.
  Grant only the delegated root to the container SID. Supervisor uses a
  restricted token; never silently substitute it for a container role.
- `WindowsWorkerSupervisor` owns one role and restarts under the shared budget
  with generation fencing; the service drains `Daemon.stop()` with non-zero
  wait hints. No daemon path instantiates it yet (`WIN-013`).

- Android is a two-process same-UID app: `:torrent_storage` is a SAF capability
  broker (containment, not privilege separation) and the default process hosts
  the router service. INTERNET and SAF grants are per UID; isolatedProcess is
  not usable for either role. Do not claim Android separation.
- The SAF broker authorizes every handle/root/operation/token by `(uid,pid)`,
  uses a handshake generation and one-shot export tokens, and never exports a
  file descriptor. Admission is typed, not exceptional; a slow provider cancels
  at 15 s and fences only at 30 s.
- Native process roles are one-shot (`Router`/`StorageBroker`); a conflict must
  fail closed. The JNI string boundary and stop-token ordering are load-bearing.

- The SAF broker watchdog is armed only after the operation owns the provider
  lock, so a queued operation is not process-fenced for a stalled sibling. The
  15 s cancel / 30 s fence ordering, owner `(uid,pid)` binding and generation
  retirement are unchanged. Client bind recovery is bounded (6 attempts,
  250 ms→10 s; attempts restored only after a 30 s stable binding).

- The daemon must not initialize ncurses, read stdin or require a TTY. The TUI
  is an optional local-IPC client; headless/session-0 operation and service
  mode must remain functional. Never add a browser/web console as an
  alternative administration surface.


- Keep the wide/narrow worker-nonce types straight: `EncodeWindowsWorkerNonce`
  and the spawn/accept APIs use `std::wstring`, while
  `DecodeWindowsWorkerNonce` consumes ASCII `std::string_view`. A narrowed
  nonce cannot be passed back to `AcceptBound`.
- Storage clients that inspect an ancillary descriptor must call
  `ReceiveWithFd()` and use `ReceivedMessage`; `Receive()` returns only the
  nested `Message`. Local descriptor RAII owners need an explicit release
  operation before transferring an authorized root into client state.
- `i2pd_privsep` is an exported archive and now publishes its `libi2pd`
  dependency; keep the link edge when adding a new supervisor/worker unit so
  installed consumers and the Windows tests resolve the core symbols.

## Do not

- Do not compile, run CTest, sanitizers, process tests or project validators
  locally.
- Do not weaken `pledge`/`unveil`, seccomp/Landlock, DACLs, peer checks,
  descriptor closure or worker authorization.
- Do not add a generic privileged RPC surface, an inherited directory fd after
  sandboxing or an unauthenticated restart path.
- Do not claim network/storage/crypto separation until those workers and
  negative tests exist.
- Do not create commits, tags, releases, worktrees or temporary commits, and do not push; inspect `git status`/`git diff` before editing.
- Do not install dependencies, fetch/vendor code or perform hidden downloads to enable a local build or test. Preserve LICENSE/NOTICE/provenance and technical identifiers/paths unless an approved migration exists (AGENTS.md).

## Prepare for CI

- Add protocol tests for malformed frames, oversized fields, wrong request ids,
  wrong types, EOF, timeout, cross-UID rejection and queue pressure.
- Register with `ipc`, `security` and `platform` labels; add a fuzz target for
  new framing (`fuzz-privsep-protocol` already covers the base protocol).
- Prepare negative capability tests for each platform backend; do not mark them
  passed until the Linux/OpenBSD/Windows jobs execute them.
- Include lifecycle tests for double start, restart exhaustion, parent death,
  child death and shutdown with an unresponsive worker.

## Tracking

Use `PRIVSEP`/M and the relevant platform IDs. Record the capability before and
after, protocol version, failure behavior, tests prepared and the exact
platform/CI lane still required. Update
`docs/security/privilege-separation.md` when the matrix changes. Propose a dated `PROGRESS.md` entry and any `TODO.md`/`ROADMAP.md` changes to the coordinator; append them only when explicitly assigned; subagents report proposed rows to the coordinator.

Record the exact AGENTS.md `Tracking states` value and state which validations were not run; do not collapse them into “pending”.

## References

- [Worker/IPC checklist](references/worker-checklist.md)
- [Privilege-separation architecture](../../../docs/security/privilege-separation.md)
- [Internal IPC protocol](../../../docs/security/ipc-protocol.md)
- [Global policy](../../../AGENTS.md)

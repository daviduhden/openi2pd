---
name: openi2pd-privsep
description: Use when changing openi2pd supervisor/worker architecture, internal IPC framing, capability handoff, process lifecycle or privilege-separation policy, excluding platform-specific sandbox details owned by the OpenBSD and Linux/Windows skills.
---

## Maintenance & Continuous Learning

When this skill guides a repository change, review it after implementation. Update procedures, tools, configurations, assumptions, examples and validation steps when they changed or when reusable knowledge improves future work; correct inaccurate guidance and omit one-off execution details. Report synchronization in the task summary. Follow the normative process and Definition of Done in [AGENTS.md](../../../AGENTS.md#continuous-agent--skills-maintenance).
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

- Resource-transfer review includes exceptions after child creation and
  before containment/publication. An owning process handle alone may close
  without terminating a suspended unassigned child. Keep a no-allocation
  rollback guard across metadata/error construction, assignment and resume;
  preserve the original exception and the cleanup outcome's evidence limits.


- A retired RPC channel can still own a child awaiting lifecycle cleanup.
  Diagnostic WorkerPid reports that ownership, not readiness. Fixtures must
  pin retained identity, then verify reaping on replacement and explicit
  Stop. Observe killed-child exit without consuming its wait status before
  issuing one failed RPC; polling by repeated writes can mutate live state
  and multiply the timeout by the retry count.

- Descriptor-bearing RPCs use the same absolute send/receive deadline as
  descriptor-free transactions. Carry metadata's outer deadline through
  begin/open, every chunk and final commit/release/abort. Channel Until APIs
  permit an immediate attempt at expiry, so mutation owners must reject
  expiry before calling them. Retire uncertain/late authority without a
  fresh cleanup RPC or claiming that a commit rolled back. Deferred child
  reaping, mutex admission and process startup are separate lifecycle bounds.

- A scoped descriptor alone does not make root publication transactional.
  Stage allocating path conversion before retiring the previous root, then
  commit the matching path with a nonallocating swap alongside descriptor,
  device and inode. RouterStorage and DataStorage share this boundary;
  subsequent worker configuration remains a separate failure interval.

- Stop's retained PID/pidfd guarantee ends with the owning object. Audit
  failed-reap move assignment and destruction separately: PRIVSEP-017's
  deferred-retirement registry is still required before discarded owners
  can preserve reap authority. Passing phase-budget tests do not cover it.

- Capability fixtures emit a fixed non-secret reason through
  tests/capability-skip.h before return code 77. Distinguish root runners,
  missing account data, absent kernel controls and unimplemented exec-free
  provisioning. A reason improves evidence; it does not validate skipped
  worker handshake, resource transfer or shutdown paths.

- POSIX ChildStopBudget partitions one entry deadline across advisory send,
  graceful exit, TERM and final reap. Keep absolute Until APIs through each
  phase and retain PID/pidfd when final reaping fails. Nonblocking liveness
  probes defer after EINTR; startup hello reads become nonblocking before
  their first byte and share one deadline across partial records/signals.

- Supervisor admission must preserve its steady-clock transaction deadline
  across toolchains. GCC 14 libstdc++ timed_mutex instantiation under TSan
  can reference an unavailable clocklock member. Review the complete clock
  path and use the portable deadline gate, not wall-clock conversion or
  a fresh relative timeout. Keep gate wait and protected work separate.

- Composite broker operations pass one absolute deadline through timed
  admission, send and every stale-reply receive. Do not convert remaining
  time back into a fresh relative receive budget. Channel Until methods
  retain the existing immediate-at-expiry behavior; transaction owners
  check expiry before starting another mutation or draining another reply.

- SOCK_SEQPACKET readiness is conservative: after one record drains, a
  smaller send can succeed while POLLOUT remains false. Try MSG_DONTWAIT
  first and poll only after EAGAIN, preserving the original deadline.
  Pair the zero-budget success case with saturated-queue failure and
  unchanged-record assertions; a pre-send poll must not gate that attempt.

- POSIX refusal sends inherit a steady-clock time point through the private
  SendWithFdUntil path. Do not convert remaining time back into a relative
  public send: preemption can otherwise rebase the original receive budget.
  Keep the 250ms refusal cap and the earlier finite deadline together. The
  source contract pins direct absolute propagation; backpressure execution
  remains a separate runtime regression. Windows now has a separate Until
  path; its kernel-reaping and mutex-admission limits belong to the platform
  skill, not the POSIX deadline guarantee.

- Ping, Pong and Shutdown have zero-byte payload ceilings in the registry.
  A buffered EOF/authentication fixture must send an empty payload; its
  encoded header still supplies a nonempty transport record. Keep a negative
  send control and a valid same-channel send so registry rejection cannot
  prevent the test from reaching the intended EOF or identity check.

- Keep storage documentation precise about authority and failure. Payload
  Read/Write/Release use handles, but namespace and metadata operations can
  carry validated root-relative names. SafeStoragePath is lexical validation,
  not filesystem containment. A failed write or directory fsync can follow
  an actual mutation; a failure reply does not promise rollback. Decode
  validates framing without authenticating the transport peer or authorizing
  the endpoint operation.

- Capability vocabulary and allowed role masks are different contracts.
  Exec remains a named bit so parsers and policy reviews can reject it;
  its presence in the enum never requires granting it to a role. Matrix
  coverage tests must explicitly require zero Exec grants while retaining
  positive coverage for the other capabilities. Never satisfy a generic
  "every bit is used" assertion by broadening a sandbox's target mask.

- A Windows accept timeout alone cannot distinguish a loader or mitigation
  failure from an unauthenticated live worker. Sample the process handle
  with a zero-time wait before terminal cleanup, then retain only a numeric
  exit status or a fixed liveness outcome. Cleanup itself terminates the
  child, so sampling afterwards loses the original evidence. Never collect
  the command line or spawn nonce. A supervisor test skipped at this seam
  supplies no authenticated-worker or restart validation.
- Capability target masks are not universal live platform profiles. The
  exec-free supervisor target is enforced by OpenBSD's pre-created workers;
  Linux still forks/execs replacement log workers. Its implementation must
  be inspected separately before claiming target enforcement. The disabled
  OpenBSD storage-broker row likewise proves no live Linux provisioning
  boundary. Keep role descriptions explicit when changing either backend.

- A single-flight data-storage response must match type, decodable payload,
  request ID, worker generation and artifact kind. Typed Error must also name
  the pending operation. Any mismatch retires the channel before returning:
  a real reply left queued would desynchronize every following request.
  Never replay an ambiguous write/remove. Client Busy is not evidence that
  the channel survived; distinguish it from a correlated wire Busy refusal.
  The negative source contract pins this path; malformed worker reply
  injection remains a dynamic regression gap.

- Unix startup abort calls `StopPrivsepWorkers` after init may already
  have created artifact and router storage workers. Review this abort
  inventory alongside normal stop and DaemonInit rollback. Revoke each
  installed callback before reaping/resetting its client; router writes
  keep a rejecting callback and delegated artifacts fail closed without
  a backend. Stopping only the log worker leaves initialized authority
  and children alive after failed startup.
- A transport-neutral encode helper must enforce semantic path rules at
  construction as well as decoding. `EncodeConfigureDataStorage` rejects
  empty and relative roots before framing, matching its decoder's absolute
  path contract; keep positive and negative producer/consumer tests paired.
- An empty `SOCK_SEQPACKET` record without descriptors is consumed as an
  empty record, not EOF. An empty record carrying `SCM_RIGHTS` is invalid:
  close the received descriptor, return a protocol error, and prove the
  channel remains usable. Do not accept an untyped capability.
- A successful worker configuration handshake does not prove the process is
  stable. Preserve the shared restart backoff across rapid worker deaths;
  `StorageClientRestart::NoteReady()` starts a generation interval without
  replenishing its budget. Only a validated operation reply after 60 seconds
  may call `NoteStable()` to restore it. Failure observation, aggregate uptime
  across generations, setup latency and backoff waits never establish healthy
  service. Eight consecutive unstable failures stay terminal until explicit
  owner `Start()`; `CanAttempt()` is a pure deadline/budget query and cannot
  revive exhaustion. The same clock-only policy runs on every platform;
  Windows worker entry points remain fail-closed stubs. Failed replacement
  setup closes its child/channel before waiting again. Keep deterministic
  slow-crash, delayed-setup, stable-boundary and terminal-budget cases in
  `test-datastorage-protocol`, and real client recovery in
  `test-datastorage-client`; execution remains pending manual CI.
- A receive loop owns one fixed-capacity record buffer: a tolerated refusal
  continues the loop and must decode a `string_view` of the record. Never
  `resize` the storage, or the next frame is truncated and reported as a fatal
  ancillary-data fault. A typed `Error` may only name a type with a registry
  row (POSIX and Windows), otherwise the peer cannot decode its refusal.
- Keep `SOCK_SEQPACKET` recovery tests within portable atomic-record sizes.
  To detect a receive buffer accidentally left at the 16-byte IPC header, a
  17-byte frame is sufficient; maximum payload boundaries belong in codec
  tests, not a kernel-dependent socket send assertion (`TF-060`).
- Derive the frame type from the capability actually attached:
  `ConfigureLogFd` is `RequiredFile`, so a syslog/foreground sink must send the
  descriptor-free `ConfigureLog` or the worker rejects the frame before parsing
  it. The supervisor stale-reply drain is bounded; `Cancel` stays advisory.
- A Windows worker is always inside its Job Object: refuse
  `useJobObject=false` and a non-valid job handle instead of skipping both
  `JOB_LIST` and assignment. Build one mutable command line per
  `CreateProcess` attempt, and release the listener/pipe name with the
  generation.
- A supervisor test must spawn through `RunWindowsWorker`; a hand-written
  Ping loop never sends `WorkerHello`, so `AcceptBound` times out and the
  test degrades to a skip.
- Windows spawn attributes are pointer-valued and must outlive
  `CreateProcess`; a block-local mitigation/capability struct yields
  `ERROR_INVALID_PARAMETER`. Profile SIDs from userenv use `FreeSid`.
  Nested jobs keep every containment/resource limit but cannot carry the UI
  restrictions; report the reduction and never fail a contained worker for it.

- Direction is a compile-time registry property. A request is received by
  workers only (the Windows pre-dispatch `WorkerHello` is the single
  exception); a reply reaches the requesting side and never a pure worker;
  the typed `Error` is receivable by every endpoint so a refused frame can be
  read instead of refused again. Only the core sends `Ping`.
  `ValidateDirectionInvariants()` must iterate the whole table.
- The shared restart ladder's eight waits total 61.75 s (the eighth is the
  30 s cap), so it spans more than one stable period. It is a count budget
  with a terminal state, not a rolling window; `NoteStable()` must not revive
  an exhausted budget.
- When the accepting side rejects a hello on PID/session it disconnects
  before reading, so the client's write may fail. The test tolerates that
  race and asserts only the server-side rejection; keep the early reject.

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
- `privsep/DataStorage.cpp` keeps standalone codecs for artifact names so the
  worker does not link the core library. I2P base64 uses its `-~` alphabet and
  `=` padding; the lower-case base32 encoding emits a zero-filled final
  five-bit symbol without `=` padding. Keep these details aligned with the
  core codec and its parity tests when changing artifact-name handling.
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
- The Windows server consumes the spawn `WorkerHello` inside `AcceptBound()`
  before setting the channel's core role. A later hello is a bounded typed
  refusal, not another identity transition; the core endpoint may send only
  that refusal's typed `Error` in addition to normal requests.
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
- No OpenBSD policy may name `exec` or `error`. The supervisor pledges
  `stdio proc` only: one active and one reserve log worker are created and
  configured before the policy, and the reserve is promoted without exec. The
  on-demand storage broker is disabled because it needed fork/exec after its
  own sandbox; isolated torrent storage fails closed pending an exec-free
  provisioning protocol. Keep `OPENBSD_PLEDGE_EXECPROMISES = nullptr`; the
  first set adds `unveil` and must be a superset of runtime. No execpromises,
  no inherited unveil assumption.

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


- The public `Worker.h` must parse on Windows: `ChildProcess` declarations use
  `worker_pid_t`, not raw POSIX `pid_t`, and `<sys/types.h>` is non-Windows
  only. The Windows supervisor does not adopt a POSIX child, but the daemon
  includes the header for the worker-identity declarations.


- The earlier `CONC-029` Windows test expectation was misoriented:
  `AcceptBound()` returns a core-side channel, which may receive worker replies
  such as `DataStorageReply`; the connecting worker side receives requests
  only after `SetRole()`. Core-side typed `Error` refusals are an explicit
  send exception, not evidence that general core replies are allowed.


- A Windows core-side channel must be able to send the typed `Error` refusal;
  `Send` therefore permits `Error` in either direction. A supervisor
  configuration failure is handle-neutral only if it validates the delegated
  root before creating a profile/token/job.

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

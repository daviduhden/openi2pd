---
name: openi2pd-memory-security
description: Use when auditing untrusted-input parsers, concurrency, object lifetime, resource bounds or security-sensitive behavior in openi2pd, excluding general C++23 ownership refactors and cryptographic primitive selection.
---

## Maintenance & Continuous Learning

When this skill guides a repository change, review it after implementation. Update procedures, tools, configurations, assumptions, examples and validation steps when they changed or when reusable knowledge improves future work; correct inaccurate guidance and omit one-off execution details. Report synchronization in the task summary. Follow the normative process and Definition of Done in [AGENTS.md](../../../AGENTS.md#continuous-agent--skills-maintenance).
<!--
See the LICENSE file at the top of the project tree for copyright
and license details.
-->

# openi2pd memory and security

## Activate when

Use this skill for protocol-bounds review, integer/allocation hardening,
concurrency and lifetime analysis, shutdown races, descriptor ownership, secret
wiping or a security regression. It is a review-and-correction skill: when the
fix belongs to another subsystem, prepare the finding and regression there and
coordinate the production edit with that owner.

Read [`../../../AGENTS.md`](../../../AGENTS.md) first. Never weaken a security
control to make a test or build pass.

## Inspect first

- The parser or concurrent component and all of its callers, not only the
  function named in a report.
- `tests/test-*-bounds.cpp`, `tests/test-crypto-audit-bounds.cpp`,
  `tests/test-*-race.cpp`, lifecycle tests and `tests/fuzz/`.
- The relevant section of
  [`../../../docs/security/memory-safety-2026.md`](../../../docs/security/memory-safety-2026.md),
  [`../../../docs/security/threat-model.md`](../../../docs/security/threat-model.md)
  and [`../../../docs/security/README.md`](../../../docs/security/README.md).
- The subsystem's own protocol or storage document before changing a format.

## Reference sources

- [`../../../docs/security/memory-safety-2026.md`](../../../docs/security/memory-safety-2026.md)
  — audited surfaces, confirmed findings and validation status.
- [`../../../docs/security/threat-model.md`](../../../docs/security/threat-model.md)
  — assets, adversaries and out-of-scope limits.
- [`../../../docs/security/privilege-separation.md`](../../../docs/security/privilege-separation.md)
  — capability boundaries that a fix must not bypass.
- [`../../../docs/testing.md`](../../../docs/testing.md) — labels, sanitizer
  lanes and fuzz inventory.
- [`references/audit-matrix.md`](references/audit-matrix.md) — high-risk
  surfaces, invariants and regression names.

## Reconnaissance

Launch at least one read-only `explore` subagent for the target surface and a
second for tests and callers. If subagents are unavailable, report that
limitation; do not substitute local execution. Ask for:

1. Every path from untrusted bytes, a remote peer, a local IPC peer or a
   provider document to allocation, indexing, arithmetic and persistence.
2. Lock order, ownership, callback capture, thread affinity and shutdown
   ordering.
3. Existing bounds constants, error categories and fail-open/fail-closed
   branches.
4. Tests that already cover the path and gaps that need a focused regression.

Do not let reconnaissance agents edit, compile, execute tests or run scripts.

## Parallel implementation

- Security findings may be split by independent parser or component. Do not
  edit the same file from two workstreams.
- Hand production edits in `libi2pd/`, `libi2pd_client/`, `privsep/` or
  `android/` to the owning skill; this skill can own the corresponding
  regression and review.
- Keep a finding's test, code fix and documentation status in one reviewable
  slice.
- Escalate any change to trust boundaries, key handling, `pledge`/`unveil`,
  seccomp/Landlock, DACLs or authentication to the coordinator.

## Conventions

- Validate lengths, counts and arithmetic before allocation or indexing.
- Reject truncated, contradictory and oversized input explicitly; never treat
  corruption as an empty optional or silently fall back to a weaker path.
- Prefer bounded spans and checked conversions. Avoid narrowing casts that
  discard high bits.
- Use the project's existing constant-time comparison and signature/AEAD
  verification paths; do not invent a replacement.
- Establish and document lock order. Prefer immutable snapshots for data read
  by another thread; do not add unsynchronised container reads.
- Release descriptors, child processes, timers and allocations on every error
  and shutdown path. Wipe secrets with the existing helpers.
- Treat logs and error strings as persistence boundaries. Never include a
  password, private key, seed, token or complete credential-bearing operand;
  prepare a marker-based negative regression for every corrected leak.
- For event-loop services, audit admission count, handshake/header/body/write
  deadlines and every synchronous operation. A byte limit does not prevent a
  slow-client denial of service.
- Persist secrets through a single atomic private-file primitive. Pathname
  chmod followed by reopen is a symlink/TOCTOU defect, not a permission fix.
- Document a residual risk when a complete fix is not justified; do not claim
  the audit is complete.

## Known issues and decisions

- Streaming lifetimes are now two-sided: a `Stream` holds a strong
  `StreamingDestination` (so pending timers still find valid packet pools) and
  `StreamingDestination::Stop()` marshals `StopInternal()` onto the owner's
  io_context with a bounded wait. SAM must not call `Stop()` directly from its
  control thread and `ClientDestination` must keep
  `m_StreamingDestinationsMutex` around every default/per-port lookup. Invoke
  the returned `shared_ptr` only after dropping the lock; callbacks re-enter
  the registry. This deliberately trades a possible owner/stream cycle (broken
  by `StopInternal()` clearing the map) for no dangling reference.
- SSU2 reassembly charges `outOfSequenceBytes`/`numOutOfSequenceFragments`
  when queuing and releases them on consumption. The caps are 64 fragments per
  message, 512 out-of-sequence fragments per incomplete message, 128 KiB per
  message and 32 incomplete messages per session; keep them together and reset
  the counters when a pooled `SSU2IncompleteMessage` starts a new first
  fragment. Do not collapse the per-message and per-session limits.
- `SSU2Server` declares its packet/message pools before `ReceiveService` so
  reverse member destruction stops/destroys the service before releasing the
  storage its handlers may reference. Preserve this order when adding or
  moving members.
- `RouterInfo::SSUExt` owns `mutex`; `GetIntroducers()`/`HasIntroducers()` are
  the only cross-thread readers. Do not iterate or index the live vector from
  SSU2, and do not call `UsesIntroducer()` while already holding the SSUExt
  lock (`UpdateIntroducers(address, …)` is the locked variant). Local
  `AddNativeIntroducer`-style helpers and `RouterContext` clears must take the
  same lock. Address-slot copy-on-write and endpoint publication remain
  SEC-025.
- `TorrentFile` serializes `Save`/`Load` with `OpenLocked`/`CloseLocked`;
  never call the public `Open()`/`Close()` from inside a locked disk path, and
  keep `GetFullFilePath()` by value. `InitTorrentFiles` still runs on the disk
  worker, so its remaining object mutation is only partially mitigated by this
  per-file lock.
- Proxy handshakes are bounded: SOCKS stores pipelined bytes in
  `m_DeferredAuthInput` and resynthesizes the receive path from
  `SentSocksResponse`; do not parse request bytes before the RFC 1929 reply
  write completes. HTTP/SOCKS share the 30 s timeout shape; `ServiceAcceptor`
  caps concurrent handlers at `MAX_ACCEPTED_HANDLERS` and rejects, rather than
  queues, excess sockets.
- `I2PControlService::Stop()` must check
  `running_in_this_thread()` before `join()`; destruction from a control
  handler would self-deadlock. `I2PTunnelInfoHandler` uses lock-copied tunnel
  snapshots, never the legacy reference accessors.
- SAF `SafEnv` treats a failed `PushLocalFrame` as a failed call and clears the
  pending `OutOfMemoryError`; leaving it set corrupts the next JNI call on the
  same thread. Device evidence is still pending.

- BOB tunnel completions use weak derived ownership plus an atomic stopped
  state, a pending-receiver registry and a short admission mutex shared with
  handler publication; `STOP`/`CLEAR` may otherwise release the last owner or
  publish work after draining. I2PControl uses bounded admission, per-operation
  deadlines, asynchronous bounded HTTP reads, capped tokens/authentication and
  atomic PEM replacement. `control-security-static` pins these seams; runtime
  TLS slow-client and BOB teardown coverage remains pending manual CI.

- CONC-011 registration tokens prevent stale teardown from removing another
  Datagram owner. External reset drains outside registry locks; callback reset
  returns false without waiting to avoid callback cycles. Do not destroy raw
  callback targets on that result. Per-caller ASan/TSan coverage remains pending.


- Queue admission checks must share a lock with enqueue and shutdown close;
  checking an atomic flag before `Put` alone leaves a post-after-drain race.
  Tunnel/netDb lifecycle locks remain separate from the short admission locks.
  Producers must stop across restart; no generation is carried in wire queues.
- Profile expiry only owns canonical encoded-hash filenames at their expected
  shard and regular non-symlink leaves. Preserve unknown/temp/backup leaves;
  process-local locking does not protect a hostile concurrent filesystem owner.


- CONC-010 uses immutable process-local profile-root authority and flushed
  same-directory replacement; storage IO/cleanup is serialized and shutdown
  drains postponed updates after producers stop. Cross-process ownership,
  abandoned staging cleanup and retained external mutators remain open.
  `test-router-profile-storage` is prepared, not executed.


- Profile lock order is map -> instance state, or instance IO -> state;
  instance methods never acquire the map mutex. The RNG mutex is released
  before taking profile state. Cache eviction requires sole map ownership;
  failed persistence must restore dirty state. `test-router-profile-race`
  covers object/publication/queue contracts; storage races use
  `test-router-profile-storage`.
- `Transports::Stop()` ordering is load-bearing: join the worker, cancel
  timers, stop servers, clear peers while both server `io_context`s are alive,
  then reset servers. Cross-thread `IsBound*` reads must use the `m_Has*`
  atomics, never `unique_ptr != nullptr`.
- `jthread` RAII does not close descriptors or unlink paths. `TuiServer` must
  `request_stop()` then `join()` before closing/unlinking, and all waits must
  be bounded; the Windows TUI still uses its own thread/stop event.
- Log text, writer queue and recent ring are independently bounded. Queue drops
  are counted, and the recent ring may contain a record the writer dropped.
- Cooperative cancellation, complete Android/Datagram caller lifecycle
  verification and the `Transports` detach fallback remain residual areas; see
  [`references/audit-matrix.md`](references/audit-matrix.md).
- Prepared seam regressions are `unit`-labelled; do not claim ASan/TSan or
  runtime coverage from them.

- The operator blocklist is built once on the startup thread and published as
  an immutable `std::shared_ptr<const OperatorBlocklistSnapshot>`; readers
  (`IsOperatorBlockedAddress/Router`, netDb selection) must stay lock-free and
  must not mutate it. Runtime bans live in a separate bounded mutex-guarded set
  (1024, memory only). Do not add a reload watcher or per-query file access.
- Address-book subscription merges must never re-point a destination that
  existed before the merge; the conflict counter is bounded and saturating and
  no conflict file is written. The subscription list is guarded by its own
  mutex and the io_context re-arm uses a shared stop token; never hold that
  mutex across network IO and never call `RearmSubscriptionsTimer` off the
  destination io_context.

- NTCP2's AEAD-failure drain owns one fixed 1041-byte buffer per session and a
  single `m_ReadTimer`; nothing may be written while draining and the timer is
  cancelled in `Terminate()`. Do not allocate the drain size from input.
- SSU2 `m_Address` is read by the transports thread and written by the SSU2
  service thread; copy it only through the mutex-guarded accessors. Fragment
  reassembly charges `sizeof(Fragment)` (fixed 1500-byte slab), capped at 64
  fragments per message, 128 KiB per message and 32 incomplete messages per
  session; do not revert to payload-length accounting (it under-counts memory)
  or to 128 incomplete messages (12 MiB per session).

- X25519 pooled keys must never be returned after use: the pool regenerates or
  drops a failed pair, and consumers check `HasPrivateKey()`. `X25519Keys` is
  non-copyable (owning `EVP_PKEY`/`EVP_PKEY_CTX`) and derives the public key.
- Ed25519 `BN_free` is only for public values; blinded scalars and nonces use
  `BN_clear_free`. Off-curve point decoding returns an empty point.
- Ratchet NS/NSR stack secrets are cleansed with `OPENSSL_cleanse` on every
  exit. Unknown LS2 encryption-key sections are stored with their opaque wire
  length; `IsEncryptionKey` callers must pass a buffer at least that long.

- netDb selection filters and store validation must use
  `GetProfileIfExists`/`GetRouterProfileIfExists`; `GetProfile()` may create a
  profile and read it from disk and must never run under `m_RouterInfosMutex`,
  `m_FloodfillsMutex` or another netDb container lock.
- `AddRouterInfo` publishes an already-validated candidate generation instead of
  mutating the stored RouterInfo under the map lock; readers may retain an older
  `shared_ptr` snapshot by design.
- The netDb inbound queue is bounded (1024) and new RouterInfos are capped
  (1000/4000, updates/reseed exempt); keep `PutIfBelow` and the drop counter,
  and never add an unbounded `Put` path.
- `NetDb::Stop` runs one bounded final persistence before clearing maps; do not
  remove it without an equivalent flush, and keep the persistence call after
  producers are quiesced.
- `Tunnel::Build` must never index the shuffled build-record map by an unplanned
  hop: `PlanTunnelBuildRecords()` validates first, and `ClampTunnelLength()`
  bounds config/reconfigure/selection (inbound reserves the phony slot).
  `CreateBuildRequestRecord()` zeroes failed records; an unwritten record must
  not be sent.
- The custom tunnel peer selector is a `TunnelPeerSelectorHandle` (pointer plus
  weak owner). `TrySelectPeers()` pins the owner for the whole call outside the
  pool mutex; a raw selector dereference after `expired()` is the old UAF.
- The inbound tunnel queue is bounded (`PutIfBelow`, 8192) with a drop counter,
  the destination garlic queue is bounded (256, oldest dropped), the transit
  build replay table is bounded (4096, 8-minute decay) and the clove replay
  filter is bounded too. Never add an unbounded post path fed by a transport
  thread.
- Garlic session state takes `m_StateMutex`; the destination must snapshot
  `m_Sessions`/`m_ECIESx25519Sessions` and call sessions outside
  `m_SessionsMutex`. Holding the map lock while calling a state-locking session
  is an ABBA deadlock. `IsTerminated()`/`IsExhausted()` stay atomic so the
  tag-budget path needs no state lock.
- The provider `HKDFContext` is not reentrant: use the per-thread
  `GetThreadLocalHKDFContext()` and never share one instance across threads.
- `GarlicDestination::GetRng()` returns a per-thread `std::mt19937`; never
  restore a shared generator member, because the destination service, streaming,
  datagram, torrent and timer threads all request random selection.
- `StreamingDestination::AcceptStream(timeout)` must keep its state in a shared
  heap object and unregister on timeout: an acceptor lambda installed in
  `m_Acceptor` must never capture caller stack references. Acceptors are
  tokenised (`RemoveAcceptor`) and `ResetAcceptor()` completes every armed
  waiter with `nullptr`.
- The SAM and I2CP control planes are bounded and asynchronous: a connection/
  session cap with exactly-once release, cancellable handshake/header/body
  deadlines, and no blocking `available()`/`read()` on the service thread. An
  idle established control connection is not closed (Java disables the socket
  timeout after HELLO).
- Datagram receivers are keyed by `(protocol, port)`; a port-specific
  registration is never promoted to the destination wildcard, because that
  would deliver an unaddressed port to another subsession. Options are per call
  and the (in)deflater plus the datagram session state are mutex-guarded.

- A stopped tunnel is a resource boundary: `I2PServiceHandler::Terminate()` is
  virtual, handler teardown snapshots under the mutex and terminates outside
  it, and `Stop(bool)` must close every accepted stream/socket. The old
  non-virtual call poisoned `m_Dead` and leaked connections for an hour.
- Server-tunnel admission is decided before a stream exists: the
  `StreamingDestination` admission predicate takes a lock-free snapshot and a
  denied SYN is dropped or answered with one signed RESET, never accepted then
  closed.
- Reseed is cancellable (`std::jthread`/`stop_token`) with per-phase and
  per-host I/O deadlines; an admin reseed must never block the admin thread,
  and `Stop()` cancels and joins it. Never delete cached RouterInfos from a
  downloaded bundle.
- Address-book files are written only with `fs::AtomicWrite`/
  `AtomicWritePrivate`; never reopen a pathname to write after checking it.
- BOB and I2PControl are local-control boundaries: bounded sessions/lines,
  same-UID peers where the platform exposes credentials, 0600 local sockets,
  escaped JSON and strict numeric parsing. NTP treats every reply as untrusted
  until source/mode/leap/stratum/origin validate and three servers agree.

- A worker role must be bound to its spawner (nonce on fd 5); a role selected
  only by `argv` can be adopted by any same-UID process with its own
  socketpair. Close fd 5 before parsing frames or applying the sandbox.
- Artifact writes delegated to a worker never fall back to a direct filesystem
  write; a failed installed writer and an unavailable worker both fail closed.
  Keep the last `router.info`/fixed-file buffer on failure instead of clearing
  the save flag and losing the update.
- Storage clients are generation-fenced: drop replies older than the current
  generation, return `Busy` on id exhaustion, and never auto-replay a write
  whose completion is unknown.

- The message registry is the only role/limit/version/descriptor authority; a
  duplicated allowlist is a security drift. A decoding fault must be answered
  only when the header is attributable (magic+version+length), and the fault
  budget is bounded; otherwise close.
- Descriptor policy is per type: a `None` type must reject a descriptor, a
  `Required*` type must reject a missing one, and a rejected descriptor is
  always closed. A pre-opened log fd is bound to the configured path identity,
  not just its kind.
- `Cancel` must never be treated as a rollback or a licence to replay; drain the
  reply and report failure. An idle timeout is not EOF.

- A per-frame credential check must use the declared peer expectation, not
  `SO_PEERCRED` of the socket creator; the latter is stale after a credential
  drop and both rejects legitimate frames and accepts the wrong reference.
- Capability teardown after `setuid` is a silent no-op (EPERM). Drop while
  privileged, verify with `capget`, and re-verify in the worker.
- Storage confinement must deny the connection lifecycle and process creation,
  not only socket creation; a writable Landlock root must not grant `EXECUTE`.

- Torrent DHT input has split threading: repliable datagrams are queued and
  parsed on the tunnel service thread; raw sender-less frames are copied/posted
  by `TorrentsTunnel` and limited to 1024/60 s. Token state is a bounded FIFO
  with a per-node quota; routing caps two node IDs per destination. Treat any
  direct network-thread call into the DHT as a regression.

- Comment ingress is bounded twice: 40 new entries/60 s per connection and 80
  per torrent, independent of reconnect; only non-duplicate entries are
  charged. Anonymous entries have a separate visible quota, and normalization
  (whitespace/zero-width) must not rewrite stored text. Comment text, owner
  labels and keys are never logged.

- Sanitizer/fuzz CTest budgets are 300 s per test (the normal 120 s budget is
  too short under ASan/TSan). Sanitizer jobs run CTest serially (`-j 1`) to
  avoid TSan memory pressure; release/platform jobs may use `-j 2` only with
  `RUN_SERIAL`/`RESOURCE_LOCK` on fork, sandbox, storage and fixed-`/tmp`
  tests.
- A skipped/partial coverage path must not look like a pass. Return 77 with a
  registered `SKIP_RETURN_CODE` for a platform stub, or keep the assertion
  active; never print `skip:` and return 0. Fuzzer harnesses must compile
  their asserts (`-UNDEBUG`) even in RelWithDebInfo.

- Fuzz corpora are checked in under `tests/fuzz/corpus/<target>/`; every
  target has a small non-secret seed (≤64 KiB), a per-target `-max_len` and a
  per-target `-artifact_prefix`. CTest uses 1024 bytes for
  `fuzz-signature-der`, 16384 for `fuzz-(http|tui|sam|i2cp|privsep)` and 65536
  otherwise; the manual `fuzz.yml` loop currently passes one global
  `-max_len=65536`, so the per-target CTest bound is authoritative for the
  smoke pass. The static checker rejects an empty/orphan corpus,
  credential-like seed names and a target missing from either the build or run
  list. Do not rely on an empty `$RUNNER_TEMP` corpus.
- Fifteen fuzzers are current: the original eleven plus `fuzz-ntcp2-blocks`,
  `fuzz-sam-control`, `fuzz-i2cp-message` and `fuzz-i2np-database`. Fuzz
  targets compile with `-fno-sanitize-recover=all`; the fuzz job sets
  `ASAN_OPTIONS`/`UBSAN_OPTIONS` at job level and records a per-target
  pass/fail row and log.
- The sanitizer matrix is ASan+UBSan, UBSan-only and TSan. CTest is serial
  (300 s per test) in all three; `test-ssu2-payload-blocks` is the pure
  negative framing matrix for `ValidateSSU2PayloadBlocks`. TSan findings are
  not suppressed; expected race residuals stay tracked instead of hidden by a
  suppression file.

- Pass `unsigned char` (or an explicit cast) to `<cctype>` functions such as
  `std::tolower`; a negative `char` is undefined behavior. The proxy header
  parser and log sanitizer both pin the cast, so a new byte classifier must
  do the same.

- Do not accumulate a bounded count in the narrow type used by the wire field.
  Use `size_t`, apply the limit guard, and cast once at serialization; a
  `uint8_t` NACK counter wrapped before its `>= 256` guard and could bypass the
  saved-packet cap (`SEC-033`, `libi2pd/Streaming.cpp`).


- Open review items `SEC-034` (FS empty-identity/private-file TOCTOU, LeaseSet
  DH/PSK cleanup) and `CONC-029` (trusted-router vector race, Windows core-side
  error send, blocking console connect) are documented but not fixed. Do not
  treat them as resolved when adding a new caller.

## Do not

- Do not compile, run CTest, sanitizers, fuzzers, Gradle or project validators
  locally.
- Do not weaken bounds, signature/AEAD checks, `pledge`/`unveil`, sandbox
  policy, credential checks or resource limits.
- Do not add `continue-on-error`, ignore a sanitizer finding or delete a test
  to obtain a green result.
- Do not ship a new wire or persisted format without a specification,
  migration and compatibility test.
- Do not create commits, tags, releases, worktrees or temporary commits, and do not push; inspect `git status`/`git diff` before editing.
- Do not install dependencies, fetch/vendor code or perform hidden downloads to enable a local build or test. Preserve LICENSE/NOTICE/provenance and technical identifiers/paths unless an approved migration exists (AGENTS.md).

## Prepare for CI

- Add deterministic regressions under `tests/` and register them with
  `memory` and/or `security` labels.
- Extend an existing fuzzer or add one under `tests/fuzz/` when the parser
  surface is not covered; register a one-input smoke target with the `fuzz`
  label.
- Identify the ASan+UBSan, TSan and fuzz jobs required for the changed path.
- Record expected failure behavior (reject, close channel, stop worker) in the
  test name or assertion, not only in prose.
- Distinguish static implementation from pending CI compilation/tests.

## Tracking

Use the security area (`SEC`/C) and the existing `MEM-*`, `NET-*`, `CRY-*`,
`PRIVSEP-*` or subsystem IDs. Record the exact finding, affected files,
regression, sanitizer/fuzz lane and residual risk. Propose a dated `PROGRESS.md` entry and any `TODO.md`/`ROADMAP.md` changes to the coordinator; append them only when explicitly assigned. Subagents report proposed tracking rows to the coordinator.

Record the exact AGENTS.md `Tracking states` value and state which validations were not run; do not collapse them into “pending”.

## References

- [Security audit matrix](references/audit-matrix.md)
- [Global policy](../../../AGENTS.md)
- [Memory-safety audit](../../../docs/security/memory-safety-2026.md)

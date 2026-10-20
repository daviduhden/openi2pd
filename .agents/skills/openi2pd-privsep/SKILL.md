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
lifecycle and one for the protocol and callers. Ask for:

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

## Do not

- Do not compile, run CTest, sanitizers, process tests or project validators
  locally.
- Do not weaken `pledge`/`unveil`, seccomp/Landlock, DACLs, peer checks,
  descriptor closure or worker authorization.
- Do not add a generic privileged RPC surface, an inherited directory fd after
  sandboxing or an unauthenticated restart path.
- Do not claim network/storage/crypto separation until those workers and
  negative tests exist.
- Do not commit, push, tag or release.

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
`docs/security/privilege-separation.md` when the matrix changes. Append a dated
`PROGRESS.md` entry; subagents report proposed rows to the coordinator.

## References

- [Worker/IPC checklist](references/worker-checklist.md)
- [Privilege-separation architecture](../../../docs/security/privilege-separation.md)
- [Internal IPC protocol](../../../docs/security/ipc-protocol.md)
- [Global policy](../../../AGENTS.md)

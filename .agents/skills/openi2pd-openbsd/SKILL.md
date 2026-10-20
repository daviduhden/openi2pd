---
name: openi2pd-openbsd
description: Use when changing openi2pd OpenBSD pledge/unveil policy, LibreSSL compatibility, imsg transport, rc.d service files, storage-supervisor startup or OpenBSD transport behavior.
---
<!--
See the LICENSE file at the top of the project tree for copyright
and license details.
-->

# openi2pd OpenBSD

## Activate when

Use this skill for OpenBSD-specific sandboxing, service integration, LibreSSL
build behavior, imsg adapters, storage-supervisor startup, SSU2/MTU behavior or
an OpenBSD-only regression. Combine it with `openi2pd-privsep` for the
transport-neutral worker contract and `openi2pd-build` for dependency checks.

Read [`../../../AGENTS.md`](../../../AGENTS.md) first. OpenBSD is first-class;
never weaken `pledge`/`unveil` or LibreSSL support to make another platform
pass.

## Inspect first

- `daemon/UnixDaemon.cpp` and `daemon/i2pd.cpp` startup/pledge order.
- `privsep/OpenBSDSandbox.*`, `OpenBSDImsg.*`, `Supervisor.*`, `Worker.*`,
  `RunWorker.*`, `RouterStorage.*`, `TorrentStorage.*`.
- `libi2pd/SSU2*`, `Transports.*`, `RouterInfo.*` for MTU/IPv6/reachability.
- `contrib/openbsd/i2pd.rc`, `i2pd.login`, `i2pd.conf`, `README.md`.
- [`../../../docs/security/openbsd-validation.md`](../../../docs/security/openbsd-validation.md)
  and [`../../../docs/security/privilege-separation.md`](../../../docs/security/privilege-separation.md).

## Reference sources

- [`../../../docs/security/openbsd-validation.md`](../../../docs/security/openbsd-validation.md)
  — package/rc.d, lifecycle, sandbox, TUI/log worker, SSU2 and evidence checklist.
- [`../../../docs/torrent/storage-worker.md`](../../../docs/torrent/storage-worker.md)
  — OpenBSD storage-supervisor bootstrap, fd4 root and socket-only handoff.
- [`../../../docs/security/privilege-separation.md`](../../../docs/security/privilege-separation.md)
  — OpenBSD supervisor/log policy and the broader capability matrix.
- [`../../../contrib/openbsd/README.md`](../../../contrib/openbsd/README.md) —
  native service layout.
- [`../../../docs/dependencies.md`](../../../docs/dependencies.md) — LibreSSL
  floor and OpenBSD notes.
- [`references/openbsd-policy.md`](references/openbsd-policy.md) — policy and
  validation map.

## Reconnaissance

Launch at least one read-only `explore` subagent for the startup/sandbox order
and one for tests/service files. Ask for:

1. when `pledge`/`unveil` is installed relative to worker spawn and config;
2. every inherited fd and descriptor handoff;
3. which paths are unveiled and whether the configured datadir/socket is
   included;
4. the LibreSSL/no-PQ code path and provider capability check;
5. existing OpenBSD-only tests and the workflow job that runs them.

## Parallel implementation

- Keep OpenBSD policy changes separate from generic worker protocol changes.
- Do not edit `daemon/UnixDaemon.cpp` concurrently with another platform agent;
  coordinate shared startup code.
- `contrib/openbsd/` and OpenBSD-only tests can proceed independently from
  Linux/Windows backends.
- The coordinator reviews any change to the supervisor bootstrap or fd4 root
  contract.

## Conventions

- Configure and spawn workers while startup still has `proc`/`exec`; drop
  `exec` from the runtime policy. Retain `proc` only for bounded
  supervisor reaping/signalling where the design requires it.
- The storage supervisor opens and validates its root before installing its
  own policy; it forks/execs the worker with the channel at fd3 and root at
  fd4. The worker applies its own `unveil`/`pledge` before ACK.
- Never send a directory descriptor with `SCM_RIGHTS` after `pledge`;
  `sendfd`/`recvfd` prohibit it. Return only the UNIX socket.
- Add `recvfd` only where required for the returned socket; do not add
  `proc`/`exec` to the core runtime policy without a demonstrated need.
- Use `O_NOFOLLOW`, ownership/device/inode checks and relative `openat` for
  storage bootstrap.
- Keep the OpenBSD rc.d script usable with a non-default user/datadir and
  preserve foreground/runtime semantics.
- Gate post-quantum advertisement on LibreSSL capability; do not advertise an
  unsupported type.

## Do not

- Do not compile, run CTest, `systemd-analyze`, rc.d, package or validators
  locally.
- Do not add promises or unveiled paths merely to silence a failure.
- Do not replace a failed sandbox with an unsandboxed fallback.
- Do not claim OpenBSD runtime validation without the manual VM/CI evidence.
- Do not commit, push, tag or release.

## Prepare for CI

- Update `test-privsep-openbsd-policy`, `test-privsep-openbsd-imsg`,
  `test-torrent-storage-openbsd` and any transport tests.
- Label platform tests `ipc;security;platform`; they register only on OpenBSD.
- Keep `build-openbsd.yml` manual; do not edit its trigger.
- Prepare the validation checklist: build, full CTest, service lifecycle,
  pidfile, sandbox paths, TUI/log worker, signals/shutdown, SSU2/MTU and
  evidence retention.
- Record LibreSSL/no-PQ and OpenBSD runtime checks as pending until the manual
  job actually passes.

## Tracking

Use `OBSD`/I and `OPENBSD-SBX-*`/`PRIVSEP-*`. Record policy before/after,
unveiled paths, inherited fds, tests and the OpenBSD CI/VM lane. Append a dated
`PROGRESS.md` entry; subagents report proposed rows to the coordinator.

## References

- [OpenBSD policy map](references/openbsd-policy.md)
- [OpenBSD validation checklist](../../../docs/security/openbsd-validation.md)
- [Privilege-separation architecture](../../../docs/security/privilege-separation.md)
- [Global policy](../../../AGENTS.md)

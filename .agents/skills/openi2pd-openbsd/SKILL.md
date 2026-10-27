---
name: openi2pd-openbsd
description: Use when changing openi2pd OpenBSD pledge/unveil policy, LibreSSL compatibility, imsg transport, rc.d service files, storage-supervisor startup or OpenBSD transport behavior.
---

## Maintenance & Continuous Learning

When this skill guides a repository change, review it after implementation. Update procedures, tools, configurations, assumptions, examples and validation steps when they changed or when reusable knowledge improves future work; correct inaccurate guidance and omit one-off execution details. Report synchronization in the task summary. Follow the normative process and Definition of Done in [AGENTS.md](../../../AGENTS.md#continuous-agent--skills-maintenance).
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
- `contrib/openbsd/i2pd.rc`, `i2pd.login`, `i2pd.conf`, `README.md` and the
  `port/` merge template (`Makefile`, `PLIST`, `pkg/README`). The template is
  not selected by CMake and is built only in an external ports tree.
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
and one for tests/service files. If subagents are unavailable, report that
limitation; do not substitute local execution. Ask for:

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

## Known issues and decisions

- Profile staging cleanup must not probe arbitrary PIDs after runtime pledge
  drops `proc`. Process-local mutexes do not prove cross-process data-directory
  ownership; preserve unknown staging files instead of deleting a live writer's
  temporary file. CONC-010 leaves crash leftovers pending. See
  [pledge(2)](https://man.openbsd.org/pledge.2).

- `contrib/openbsd/port/` is an external merge template, not selected by CMake
  and not a validated package. `pkg_create`, `pkg_add` and `rcctl` runs remain
  pending.
- Preserve pledge/unveil ordering, fd4 root handoff, post-pledge `sendfd`/
  `recvfd` restrictions and LibreSSL 4.0+/OpenSSL 4.x compatibility.
- Unsupported sandbox paths must return CTest SKIP (77), never a false pass.
- OpenBSD compilation, service lifecycle, TUI and storage-broker runtime
  evidence remain pending manual CI.

- The supervisor and storage broker are exec authorities: pre-resolve the
  executable, `unveil(exe,"x")` + lock, pledge `stdio proc exec` /
  `stdio proc exec sendfd`. No `unix`, path read/write, `recvfd` or network.
  `OPENBSD_PLEDGE_EXECPROMISES` is `nullptr`; never pass execpromises and never
  assume pledge/unveil survive `execve`. A new image installs its own policy.
- The startup pledge must include `recvfd` (and `id` when a worker identity is
  configured) because pledge only removes promises; the runtime set is a strict
  subset. `openbsd.pledge_file` is subset-only, rejects comments/blank lines
  and dangerous runtime tokens. `unveil_file` entries must be absolute and not
  `/`.
- Worker identity: bounded `privsep.user`/`privsep.group` resolution before the
  sandbox, uid 0 rejected, setgroups/setgid/setuid and verification in the
  forked child, 25-byte role/nonce/uid/gid hello on fd 5. Descriptors 3/4/5,
  stdio `/dev/null`, >=6 closed; fd4 closed when the role has no root. Never
  resolve the executable through `$PATH` or `/proc`.
- A root core with isolation and no `privsep.user` must fail startup; a
  delegated root must be owned by the worker uid. Workers accept uid 0 only as
  the delegating spawner. The core itself is not dropped.
- Remaining: broker respawn for router/data, and TUI/I2PControl sockets inside
  a storage-unveiled datadir are still a named-socket isolation gap.

## Do not

- Do not compile, run CTest, `systemd-analyze`, rc.d, package or validators
  locally.
- Do not add promises or unveiled paths merely to silence a failure.
- Do not replace a failed sandbox with an unsandboxed fallback.
- Do not claim OpenBSD runtime validation without the manual VM/CI evidence.
- Do not create commits, tags, releases, worktrees or temporary commits, and do not push; inspect `git status`/`git diff` before editing.
- Do not install dependencies, fetch/vendor code or perform hidden downloads to enable a local build or test. Preserve LICENSE/NOTICE/provenance and technical identifiers/paths unless an approved migration exists (AGENTS.md).

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

- OpenBSD 7.7–7.9 expose `imsg_get()`; 8.0 replaced it with `imsgbuf_get()`
  (1/0/-1 instead of a total size). `build/CMakeLists.txt` probes
  `imsgbuf_get` against `libutil` and defines `I2PD_HAVE_IMSGBUF_GET`; keep the
  fallback compiling on the documented 7.7 floor rather than raising it.

## Tracking

Use `OBSD`/I and `OPENBSD-SBX-*`/`PRIVSEP-*`. Record policy before/after,
unveiled paths, inherited fds, tests and the OpenBSD CI/VM lane. Propose a dated `PROGRESS.md` entry and any `TODO.md`/`ROADMAP.md` changes to the coordinator; append them only when explicitly assigned; subagents report proposed rows to the coordinator.

Record the exact AGENTS.md `Tracking states` value and state which validations were not run; do not collapse them into “pending”.

## References

- [OpenBSD policy map](references/openbsd-policy.md)
- [OpenBSD validation checklist](../../../docs/security/openbsd-validation.md)
- [Privilege-separation architecture](../../../docs/security/privilege-separation.md)
- [Global policy](../../../AGENTS.md)

---
name: openi2pd-linux-windows
description: Use when changing openi2pd Linux sandboxing or Windows local IPC, including seccomp, Landlock, capabilities, named pipes, DACLs, tokens, Jobs and Win32 console/service adapters, excluding the transport-neutral worker protocol and TUI presentation.
---
<!--
See the LICENSE file at the top of the project tree for copyright
and license details.
-->

# openi2pd Linux and Windows backends

## Activate when

Use this skill for platform-specific IPC, sandbox and process mechanics on
Linux or Windows. Use `openi2pd-privsep` for the transport-neutral supervisor
contract and `openi2pd-tui` for console behavior; this skill owns the platform
adapter that implements them.

Read [`../../../AGENTS.md`](../../../AGENTS.md) first. Never weaken a sandbox,
DACL or credential check to make a cross-platform build pass.

## Inspect first

- Linux: `privsep/LinuxSandbox.*`, `Channel.*`, `Worker.*`, `Supervisor.*`,
  `daemon/UnixDaemon.cpp`.
- Windows: `privsep/WindowsIpc.*`, `Worker.*`, `Supervisor.*`,
  `daemon/TuiServerWin32.cpp`, `tui/TuiWin32Ipc.*`,
  `tui/TerminalWin32.*`, `Win32/*`.
- [`../../../docs/security/windows-worker-ipc.md`](../../../docs/security/windows-worker-ipc.md),
  [`../../../docs/TUI-WINDOWS.md`](../../../docs/TUI-WINDOWS.md) and
  [`../../../docs/security/ipc-protocol.md`](../../../docs/security/ipc-protocol.md).
- The Linux/Windows conditional registration in `tests/CMakeLists.txt`.

## Reference sources

- [`../../../docs/security/windows-worker-ipc.md`](../../../docs/security/windows-worker-ipc.md)
  — transport, DACL/SDDL, peer identity, token/Job/AppContainer and limits.
- [`../../../docs/security/ipc-protocol.md`](../../../docs/security/ipc-protocol.md)
  — shared framing and failure behavior.
- [`../../../docs/TUI-WINDOWS.md`](../../../docs/TUI-WINDOWS.md) — Win32 console,
  named pipe, authorization and service-vs-interactive rules.
- [`../../../docs/security/privilege-separation.md`](../../../docs/security/privilege-separation.md)
  — target capability matrix.
- [`references/platform-matrix.md`](references/platform-matrix.md) — platform
  mechanism and test map.

## Reconnaissance

Launch at least one read-only `explore` subagent per platform before editing.
If subagents are unavailable, report that limitation; do not substitute local
execution. Ask for:

1. the exact kernel/API mechanism and its failure path;
2. credential/peer identity checks and what happens on lookup failure;
3. descriptor/handle inheritance and closure;
4. sandbox application order and fallback behavior;
5. console/pipe lifecycle and cleanup;
6. platform-only tests and the manual workflow that runs them.

## Parallel implementation

- Linux and Windows backends are independent; do not let one agent edit both
  unless the change is a shared interface.
- Shared `privsep/Protocol.h`, `Channel.*`, `Worker.*` or
  `daemon/Daemon.*` changes require coordinator review.
- Keep Win32 console changes separate from service/DACL changes unless the
  same startup path is genuinely affected.
- Do not change a shared CMake condition from only one platform workstream.

## Conventions

### Linux

- Apply no-new-privs, capability/bounding-set drops, Landlock and seccomp in the
  documented order; fail closed if a requested layer cannot be applied.
- Use `pidfd`/waitpid ownership with one reaper; close descriptors on all
  paths.
- Do not add a broad `AF_INET` allowance to a UNIX-only worker.
- Keep namespace use optional and verified; no hidden dependency download.

### Windows

- Use message-mode local named pipes with a protected user/SYSTEM DACL,
  `PIPE_REJECT_REMOTE_CLIENTS`, peer SID/session checks and no inherited
  handles.
- Spawn workers suspended, assign the Job Object/restricted token as designed,
  then resume. Treat AppContainer/LPAC probing as fail-closed.
- Keep pipe names local and derived from configuration; do not expose a
  network endpoint.
- Preserve console/VT behavior and clean up handles on service stop and
  interactive exit.
- Avoid `RemoveDirectoryA` macro collisions; use unambiguous names/APIs.

## Known issues and decisions

- Linux build-header age and runtime Landlock ABI are independent. The sandbox
  uses the stable extensible two-u64 ruleset layout and runtime-gated rights,
  with compile-time checks against available UAPI declarations. Absent base
  Landlock headers reject sandbox installation rather than bypass it.
- Windows has a named-pipe worker transport and restricted-token/Job policy, but
  no `--privsep-worker` daemon wiring, AppContainer/LPAC token construction or
  per-role WFP/ACL enforcement. Do not claim Windows separation.
- Linux storage/log boundaries exist; network/router/verifier workers and
  complete storage authority remain pending. OpenBSD-only inherited-root roles
  are rejected on Linux.
- The Unix TUI now uses `std::jthread`; the Windows TUI still uses its own
  thread and stop event. Keep both backends behaviorally aligned.
- Cross-user, AppContainer, WFP and Windows service runtime tests remain
  pending.

## Do not

- Do not compile, run CTest, sanitizers, console/pipe tests or validators
  locally.
- Do not lower DACL protection, disable peer checks, inherit arbitrary handles
  or fall back to a weaker transport.
- Do not add a service that runs the TUI, a second router instance or an
  auto-start trigger.
- Do not claim Windows/Linux runtime validation from a cross-compile or static
  review.
- Do not create commits, tags, releases, worktrees or temporary commits, and do not push; inspect `git status`/`git diff` before editing.
- Do not install dependencies, fetch/vendor code or perform hidden downloads to enable a local build or test. Preserve LICENSE/NOTICE/provenance and technical identifiers/paths unless an approved migration exists (AGENTS.md).

## Prepare for CI

- Linux: `test-privsep-linux-ipc`, `test-privsep-linux-sandbox`,
  `test-privsep-log`, with `ipc;security;platform` labels; ASan/UBSan and TSan
  where relevant.
- Windows: `test-privsep-windows-ipc`, `test-tui-win32-pipe`,
  `test-tui-terminal`; build both MSYS2/Clang64 and MSVC jobs as applicable.
- Keep platform-only tests registered only on their host.
- Prepare explicit denial cases: wrong SID/session, bad DACL, inherited handle,
  unavailable namespace, seccomp denial and AppContainer failure.
- Record results as pending until the manual Windows/Linux jobs pass.

## Tracking

Use `PRIVSEP`/M plus platform IDs. Record mechanism, sandbox order, credential
checks, fallback behavior, tests and the exact CI lane. Update
`docs/security/windows-worker-ipc.md` or the Linux/IPC document when behavior
changes. Propose a dated `PROGRESS.md` entry and any `TODO.md`/`ROADMAP.md` changes to the coordinator; append them only when explicitly assigned; subagents report proposed rows to
the coordinator.

Record the exact AGENTS.md `Tracking states` value and state which validations were not run; do not collapse them into “pending”.

## References

- [Platform mechanism matrix](references/platform-matrix.md)
- [Windows worker IPC](../../../docs/security/windows-worker-ipc.md)
- [Internal IPC protocol](../../../docs/security/ipc-protocol.md)
- [Global policy](../../../AGENTS.md)

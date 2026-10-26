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
- Private files created by shared filesystem helpers require a protected DACL
  containing only the effective user and SYSTEM, non-inheritable handles and
  reparse-point rejection. Do not treat POSIX mode bits or an inherited
  datadir ACL as the Windows equivalent.

## Known issues and decisions

- `AtomicWritePrivate` gives POSIX staging mode 0600 and Windows staging a
  protected effective-user/SYSTEM DACL, with reparse-target rejection and
  write-through replacement. Win32 pathname traversal retains a hostile-parent
  TOCTOU window; certificate and key replacements are individually atomic, not
  a two-file transaction. Platform CI must exercise DACL/reparse behavior.

- Linux build-header age and runtime Landlock ABI are independent. The sandbox
  uses the stable extensible two-u64 ruleset layout and runtime-gated rights,
  with compile-time checks against available UAPI declarations. Absent base
  Landlock headers reject sandbox installation rather than bypass it.
- Storage sources include `<stdio.h>` for POSIX `renameat`; do not depend on
  `<filesystem>` or string headers to declare it on another libc.
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

- Linux `SO_PEERCRED` is the socket creator, not the live peer. After a root
  core drops a worker to `privsep.user`, per-frame `SCM_CREDENTIALS` must be
  checked against a spawner-set `PeerExpectation` (uid/gid/pid, root-parent
  allowance), never against `SO_PEERCRED`. The core binds the direct child pid;
  a worker accepts its own uid or uid 0; an adopted broker socket is uid-bound
  and must have an empty receive queue.
- Capability teardown has two stages in the forked child: `PR_SET_NO_NEW_PRIVS`
  and ambient/bounding clear while still privileged (keep effective/permitted
  for setgid/setuid), then gid/uid drop, then `capset(0)` + `capget` verify and
  exec. The whole teardown after the drop is an EPERM no-op; never tolerate it
  when euid 0.
- Storage seccomp denies `connect`/`bind`/`listen`/`accept*`/`sendto`/`execve*`/
  `fork`/`clone`, not only `socket`/`socketpair`; Landlock handles `EXECUTE`
  without granting it; `Storage` restricts reads to the delegated root. The
  socket/capability lockdown is installed before the configure frame.
- The network namespace is best-effort pre-exec (`CAP_SYS_ADMIN`) and seccomp is
  the mandatory boundary. cgroup-v2 limits and a global admission cap are not
  wired; do not claim them.

- Windows transport is a message-mode Named Pipe with a protected DACL, a
  per-spawn random name and `FILE_FLAG_FIRST_PIPE_INSTANCE`. `ncalrpc`/MIDL and
  raw ALPC/`Nt*` port APIs are rejected; do not introduce them.
- A worker proves its spawn with `WorkerHello` (protocol v5: role + 32-byte
  nonce) after `ConnectBound`; the server impersonates the client and checks
  user SID, PID/session, AppContainer SID and nonce before binding the role.
  `Accept()` is test-only; the nonce is not a secret on the command line.
- `WindowsIsolation.cpp` is the role authority: storage/log/crypto/admin are
  low-privilege AppContainers with no `internetClient`; network has Internet
  capability and no write root; supervisor uses a restricted token. Never
  grant Everyone/ALL APPLICATION PACKAGES; grant only the delegated root to the
  container SID. Fail closed instead of falling back to a restricted token.
- Spawn is suspended with NUL std handles, no handle inheritance, an explicit
  empty handle list, Job-List + security-capabilities attributes, Job limits
  and mitigations. Stop terminates the Job Object within a budget. A
  completed-during-cancel record must be delivered; a timed-out write closes.
- No daemon path starts the Windows supervisor and no Windows role backend
  exists yet (`WIN-013`); do not describe production separation as wired.

- The Windows TUI installs a console control handler for Ctrl-C/Break, close,
  logoff and shutdown; it only sets the TUI stop flag and the normal shutdown
  restores console modes. The daemon itself must remain independent of any
  terminal; no console allocation or input read belongs in the router process.


- The Windows worker handshake nonce is transported as `std::wstring` hex
  (`EncodeWindowsWorkerNonce`, `SpawnWindowsWorker`, `AcceptBound`). Tests and
  callers must keep the wide value; narrowing it back to `std::string` makes
  `AcceptBound` ill-formed. `DecodeWindowsWorkerNonce` takes the narrow ASCII
  hex, so a test helper may narrow explicitly for decoding only.
- Boost.Asio `basic_socket::native_handle()` is non-const; the local-control
  peer-credential overload must receive a mutable accepted socket even though
  the generic no-op overload can accept a const socket.


- `privsep/Worker.h` is included by the Windows daemon even though
  `ChildProcess` is POSIX-only. Do not declare POSIX-only `pid_t` fields
  unguarded: use the `worker_pid_t` alias (`int` on Windows, `pid_t`
  elsewhere) and guard `<sys/types.h>` (`CE-007`).


- MSVC reports C4996 for `getenv`, `strerror` and `_wopen`, plus C4244/C4267
  for narrowing conversions. Use `_dupenv_s`, `std::generic_category().message`
  and `_wsopen_s` with `_SH_DENYNO`; cast only after the existing bound check.
  Do not add `_CRT_SECURE_NO_WARNINGS` to hide the family wholesale.

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

- Windows token helpers must not share a name with a `TOKEN_INFORMATION_CLASS`
  enumerator (`TokenAppContainerSid`, `TokenIntegrityLevel`). Unqualified
  lookup inside the helper selects the function itself and the
  `GetTokenInformation` call fails to compile; qualify the enumerator with
  `::` or rename the helper.
- Asio's `remote_endpoint(error_code)` and `native_handle()` are non-const.
  A peer-identity helper that calls them must take a mutable socket reference;
  the caller normally already owns a mutable session socket.
- `privsep/Channel.cpp` consumes `struct ucred`, `SO_PASSCRED` and
  `SCM_CREDENTIALS`; define `_GNU_SOURCE` in the translation unit as
  `Worker.cpp` does, rather than relying on libstdc++ to predefine it.

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

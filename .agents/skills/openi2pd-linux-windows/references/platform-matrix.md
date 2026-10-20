<!--
See the LICENSE file at the top of the project tree for copyright
and license details.

openi2pd Linux and Windows platform matrix.
-->
# Linux and Windows platform matrix

Use with `SKILL.md`. This is a static map; no platform runtime is validated by
this file.

## Linux

| Layer | Source | Check |
|---|---|---|
| Descriptor channel | `privsep/Channel.*` | seqpacket, CLOEXEC, peer UID, timeout |
| Process lifecycle | `privsep/Worker.*`, `Supervisor.*` | fork/exec, pidfd, single reap, bounded stop |
| Sandbox | `privsep/LinuxSandbox.*`, `daemon/UnixDaemon.cpp` | no-new-privs, caps, Landlock, seccomp, optional netns |
| Storage | `privsep/TorrentStorage.*`, `libi2pd_client/TorrentStorageWorker.*` | root fd, Landlock, bounded requests |
| Tests | `test-privsep-linux-*`, `test-torrent-storage-*` | `ipc;security;platform` |

Failure rules: if a requested sandbox layer cannot be applied, the worker must
not start. A UNIX-only worker must not receive an `AF_INET` allowance. Parent
death uses channel EOF; do not rely on `PR_SET_PDEATHSIG` as the only check.

## Windows

| Layer | Source | Check |
|---|---|---|
| Transport | `privsep/WindowsIpc.*` | message-mode local pipe, reject remote clients |
| DACL | `privsep/WindowsIpc.*` | protected user/SYSTEM DACL, no broad Everyone access |
| Peer identity | `privsep/WindowsIpc.*` | SID/session match; lookup failure rejects |
| Process policy | `privsep/WindowsIpc.*`, `Worker.*` | suspended spawn, no inherited handles, Job Object, restricted token |
| AppContainer | `privsep/WindowsIpc.*` | probe only; fail closed if unavailable |
| TUI | `tui/TuiWin32Ipc.*`, `TerminalWin32.*`, `daemon/TuiServerWin32.cpp` | local pipe, console/VT lifecycle |
| Tests | `test-privsep-windows-ipc`, `test-tui-win32-pipe`, `test-tui-terminal` | `ipc;tui;platform` |

Failure rules: do not fall back to an unprotected pipe or a global namespace.
Do not start a TUI as a service. Do not inherit handles across worker spawn.

## Shared boundaries

- `privsep/Protocol.h` and `Channel.*` are transport-neutral; platform adapters
  must not invent a second framing.
- `daemon/Daemon.*` owns startup ordering; a platform change must not bypass
  the supervisor contract.
- CMake conditionals must register only the tests that can run on the host.
- The Windows MSYS2 and MSVC jobs exercise different source selection and
  warning levels; keep both viable.

## Manual CI

- Linux Clang/GCC full build and CTest, including `-L ipc`/`-L platform`.
- ASan/UBSan and TSan jobs for sandbox/lifetime changes.
- MSYS2/Clang64 full graph and CTest; MSVC TUI/API build and install check.
- Negative cases for wrong peer, wrong DACL, inherited handle, missing
  capability and unavailable namespace.

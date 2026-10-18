# Subagent operating instructions

This file is for agents working in this repository. It is not user-facing
documentation.

## Hard rules

- Do not create commits, pushes, tags or releases.
- Do not create worktrees or temporary commits unless the coordinator
  explicitly allows it.
- Do not overwrite existing modifications. Inspect `git status` and `git diff`
  before editing.
- Do not edit files outside the assigned scope.
- Do not modify `TODO.md`, `ROADMAP.md` or `PROGRESS.md`; report changes to the
  coordinator.
- Do not reintroduce GNU make, the HTTP admin console, web assets, submodules
  or hidden downloads.
- Keep CI manual-only (`workflow_dispatch`); do not edit triggers.
- Preserve copyright, license, SPDX, NOTICE and upstream provenance.
- Do not weaken `pledge`/`unveil`, cryptographic checks, signature/AEAD
  verification or untrusted-input limits.
- Do not change I2P wire formats or persisted formats without a specification
  reference and migration.
- Static analysis is not dynamic validation. State clearly what was not run.

## Coordinator / ownership

| Area | Owner | Typical files |
|---|---|---|
| Architecture/CMake integration | coordinator | `build/CMakeLists.txt`, `daemon/Daemon.*` |
| C++23 core | C++23 subagent | `libi2pd/**`, `libi2pd_client/**` |
| Security parsers | Security subagent | protocol parsers, crypto |
| IPC/privilege separation | IPC subagent | `privsep/**`, `daemon/UnixDaemon.cpp` |
| OpenBSD | OpenBSD subagent | `daemon/UnixDaemon.cpp`, `SSU2.cpp` |
| Linux/Windows IPC | Linux/Windows subagents | `privsep/**`, platform files |
| TUI | TUI subagent | `tui/**`, `libi2pd/TuiProtocol.h` |
| Torrent engine/API | Torrent subagent | `libi2pd_client/Torrents*` |
| Torrent TUI/IPC | Torrent TUI/IPC subagents | `tui/**`, `daemon/TuiServer.*`, `libi2pd/TuiProtocol.h` |
| Perl tooling | Perl subagent | `tools/scripts/**`, Perl tests/docs |
| Tools | Tools subagent | `tools/**` |
| Firewall | Firewall subagent | `tools/firewall.hpp`, `tools/routerinfo.cpp` |
| Android | Android subagent | `android/**` |
| Docs/identity | Documentation/Branding subagents | `docs/**`, metadata |
| Tests | Testing subagent | `tests/**` |
| CI | Coordinator | `.github/workflows/**` |

No two agents edit the same file concurrently. Shared interfaces (`Log.h`,
`RouterContext`, CMake, `TuiProtocol.h`) require coordinator review before
integration.

## Workflow

1. Read `TODO.md`, `ROADMAP.md`, `PROGRESS.md` and relevant technical reports.
2. Inspect the real code and current diff.
3. Implement only the assigned task; keep diffs reviewable.
4. Add or update tests when the component has a test target.
5. Run static checks possible without the user's dynamic environment
   (`git diff --check`, XML parsing, compile if available).
6. Report:
   - task IDs addressed;
   - files changed;
   - technical decisions;
   - unresolved findings;
   - tests prepared but not run;
   - dynamic validations required.
7. Do not mark a task validated unless it was actually executed.

## Persistent project constraints

- Native code is C++23; CMake is primary and the root Makefile is a POSIX
  front-end using Ninja. GNU make must not be reintroduced.
- Android source/target is Java 25; a build JDK is not a runtime and no JDK may
  be bundled in the APK. Material 3/Views/Java is preferred; no Kotlin/Compose
  migration was requested.
- The torrent console is native ncursesw + local IPC. No HTML/CSS/JS console,
  Node/V8/QuickJS, browser bridge or cleartext torrent path.
- Project-maintained Python is replaced by Perl; no CPAN/runtime downloads.
- Every workflow is `workflow_dispatch`-only, actions are SHA-pinned and
  checkout credentials are not persisted.
- OpenBSD is first-class; do not weaken pledge/unveil or LibreSSL support.
- Do not claim full privilege separation until network/storage workers,
  supervisor and negative capability tests actually exist. The log worker is
  only the first isolated component.
- Preserve copyright, license, SPDX, NOTICE and upstream provenance.

## Current uncommitted work

The working tree contains privilege-separated logging (`privsep/`), Android
Java 25/UI refactoring, torrent control/TUI migration, Perl tooling, security
docs and tool hardening. Agents must not revert or overwrite those changes.
Torrent TUI control must use the bounded local IPC; it must not access engine
internals, sockets, download files or JSON-RPC. Python is not an allowed
runtime dependency for project-maintained scripts once the Perl replacement
is installed.

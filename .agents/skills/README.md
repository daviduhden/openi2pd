<!--
See the LICENSE file at the top of the project tree for copyright
and license details.

openi2pd: Agent Skills index.

Lists the project's reusable, task-specific agent procedures.
-->

# openi2pd Agent Skills

This directory contains reusable Agent Skills in the open
`<name>/SKILL.md` layout. Each frontmatter declares a unique `name` and a
`description` that tells an agent when to load it. The canonical permanent
policy remains [`../../AGENTS.md`](../../AGENTS.md); skills add procedures and
file maps, not a second policy layer.

## Selection

Load the smallest set that covers the change:

1. Start with the subsystem skill (`openi2pd-torrent`, `openi2pd-tui`,
   `openi2pd-android`, and so on).
2. Add `openi2pd-cpp23` for native ownership/API changes.
3. Add `openi2pd-memory-security` when the change parses untrusted input,
   changes concurrency/lifetime, or needs a security review.
4. Add `openi2pd-i2p-interop` and/or `openi2pd-cryptography` when a wire
   format, identity, signature, ratchet, transport or Java parity claim is
   involved.
5. Add `openi2pd-build`, `openi2pd-packaging`, `openi2pd-ci` or
   `openi2pd-documentation` only when the change crosses those boundaries.
6. Use `openi2pd-upstream` for selective synchronization, not for ordinary
   development.

Do not load all skills for every task. Combine them when a change crosses
subsystems, but keep one owner per file. Shared interfaces (`Log.h`,
`RouterContext`, CMake, `tests/CMakeLists.txt`, `TuiProtocol.h`,
`privsep/Protocol.h`, `privsep/TorrentStorage.*`, `daemon/Daemon.*`,
`daemon/UnixDaemon.cpp`, the Android JNI CMake graph and SAF AIDL) require
coordinator review. `openi2pd-build` owns CMake-side test registration;
`openi2pd-ci` owns workflow wiring and label-gate verification.

## Inventory

| Skill | Use for | Primary scope |
|---|---|---|
| `openi2pd-cpp23` | C++23 refactors, RAII, spans, expected, owned lifetimes | `libi2pd/`, `libi2pd_client/` |
| `openi2pd-memory-security` | Parser bounds, concurrency, lifetime and security audits | Protocol/crypto/client parsers |
| `openi2pd-i2p-interop` | Java 2.14.0 comparison target; historical 2.13.0 evidence | Transports, netDb, client protocols |
| `openi2pd-cryptography` | Primitives, signatures, ratchets, PQ and reseed trust | Crypto/identity/reseed |
| `openi2pd-privsep` | Supervisor/worker architecture and capability boundaries | `privsep/`, daemon wiring |
| `openi2pd-openbsd` | pledge/unveil, LibreSSL, imsg and OpenBSD service paths | OpenBSD-specific code |
| `openi2pd-linux-windows` | Linux sandbox and Windows IPC/process backends | Platform adapters |
| `openi2pd-torrent` | Native I2PSnark engine, DHT/PEX, storage and SAF | `Torrents*`, torrent docs/tests |
| `openi2pd-tui` | ncursesw/Win32 console and typed local administration | `tui/`, TUI IPC |
| `openi2pd-android` | Java 25, Material Views, JNI, service and SAF | `android/` |
| `openi2pd-build` | CMake, POSIX Make, dependencies and test registration | Build graph |
| `openi2pd-packaging` | Debian/RPM/static, init systems and install layout | `debian/`, `contrib/`, `man/` |
| `openi2pd-tooling` | i2pd-tools, firewall generator and Perl utilities | `tools/` |
| `openi2pd-upstream` | Selective upstream/i2pd-docs/i2pd-tools sync | Provenance and sync docs |
| `openi2pd-ci` | Manual workflows, CTest labels, sanitizers and `gh` triage | `.github/workflows/` |
| `openi2pd-documentation` | Docs, licensing, branding and tracking coherence | `docs/`, metadata |

## Working contract

- Read the selected `SKILL.md`, then only the referenced material needed for
  the task.
- Launch at least one read-only reconnaissance subagent when subagents are
  available. Split independent implementation work across owners; do not let
  two agents edit the same file.
- Do not compile, configure a build, run CTest, Gradle, sanitizers, fuzzers or
  project validators locally. Dynamic validation is dispatched manually in CI.
- Do not commit, push, tag or release. Preserve existing worktree changes.
- Record code as implemented by static analysis separately from pending CI
  compilation, pending CI tests and validated-by-CI states.
- Keep `TODO.md`, `ROADMAP.md` and `PROGRESS.md` coherent. Subagents report
  proposed tracking rows to the coordinator; the coordinator owns shared
  tracking-file edits.

## Skill maintenance

Before considering a skill change complete, inspect that its `name` matches the
directory, its `description` states activation conditions, every relative link
resolves, every `references/` file exists, and no instruction contradicts
`AGENTS.md`. Update the owning skill when paths, shared interfaces, tests or
workflow ownership change. Documentation-link and branding checks remain
manual CI checks; do not run local validators to claim them.

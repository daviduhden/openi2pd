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
file maps, not a second policy layer. Every skill has a `Maintenance &
Continuous Learning` section that applies this policy after task changes.

## Selection

Load the smallest set that covers the change:

1. Start with the subsystem skill (`openi2pd-torrent`, `openi2pd-tui`,
   `openi2pd-android`, `openi2pd-tooling` for `tools/**`/Perl and
   `openi2pd-privsep` for supervisor/worker framing, plus
   `openi2pd-openbsd`/`openi2pd-linux-windows` for the platform adapter).
2. Add `openi2pd-cpp23` for native ownership/API changes.
3. Add `openi2pd-memory-security` when the change parses untrusted input,
   changes concurrency/lifetime, or needs a security review.
4. Add `openi2pd-i2p-interop` and/or `openi2pd-cryptography` when a wire
   format, identity, signature, ratchet, transport or Java parity claim is
   involved.
5. Add `openi2pd-build`, `openi2pd-packaging`, `openi2pd-ci`,
   `openi2pd-documentation` or `openi2pd-i18n` only when the change crosses
   those boundaries. UI strings always use `openi2pd-i18n`.
6. Use `openi2pd-upstream` for selective synchronization, not for ordinary
   development.

Do not load all skills for every task. Combine them when a change crosses
subsystems, but keep one owner per file. Shared interfaces (`Log.h`,
`RouterContext`, CMake, `tests/CMakeLists.txt`, `TuiProtocol.h`,
`privsep/Protocol.h`, `privsep/TorrentStorage.*`, `daemon/Daemon.*`,
`daemon/UnixDaemon.cpp`, the Android JNI CMake graph and SAF AIDL) require
coordinator review. `openi2pd-build` owns CMake-side test registration;
`openi2pd-ci` owns workflow wiring and label-gate verification. For
`docs/security/ipc-protocol.md` version/type changes, `openi2pd-privsep` drafts
and TUI/OpenBSD/Linux/Windows owners review; `TuiProtocol.h` changes are drafted
by `openi2pd-tui`.

## Inventory

| Skill | Use for | Primary scope |
|---|---|---|
| `openi2pd-cpp23` | C++23 refactors, OOP, Modern.h, RAII, spans, expected, owned lifetimes | `libi2pd/`, `libi2pd_client/` |
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
| `openi2pd-tooling` | i2pd-tools, firewall generator and Perl utilities | `tools/`, Android/Perl helpers, interop checks |
| `openi2pd-upstream` | Selective upstream/i2pd-docs/i2pd-tools sync | Provenance and sync docs |
| `openi2pd-ci` | Manual workflows, CTest labels, sanitizers and `gh` triage | `.github/workflows/` |
| `openi2pd-documentation` | Docs, licensing, branding and tracking coherence | `docs/`, metadata |
| `openi2pd-i18n` | UI translations, locale selection, native/Android catalogs and parity tests | `i18n/`, `values-*`, TUI text |

## Working contract

- Read the selected `SKILL.md`, then only the referenced material needed for
  the task.
- Launch at least one read-only reconnaissance subagent when subagents are
  available. Split independent implementation work across owners; do not let
  two agents edit the same file.
- Every repository change performs the mandatory `Continuous Agent & Skills
  Maintenance` procedure in `AGENTS.md` and includes a substantive update to
  at least one relevant instruction or skill file. The PR maintenance gate
  checks structure, changed paths and local references; semantic relevance
  remains an explicit review obligation.
- Never duplicate normative policy. Link the single `AGENTS.md` owner and keep
  only the subsystem procedure, file map, invariant or review step here. A
  rule change updates the owner and removes stale copies in the same
  increment; generated lists and values owned by another skill are not
  restated.
- Do not compile, configure a build, run CTest, Gradle, sanitizers, fuzzers or
  dynamic project tests locally. Compile-free structural checks required by
  `AGENTS.md` may and should run locally. Other dynamic validation is dispatched
  manually in CI.
- Do not create commits, tags, releases, worktrees or temporary commits, and do not push. Inspect `git status`/`git diff` and preserve existing worktree changes.
- Do not install dependencies or fetch/vendor code locally to enable a build, test or inspection; no hidden downloads.
- Record code as implemented by static analysis separately from pending CI
  compilation, pending CI tests and validated-by-CI states.
- Keep `TODO.md`, `ROADMAP.md` and `PROGRESS.md` coherent. Subagents report
  proposed tracking rows to the coordinator; the coordinator owns shared
  tracking-file edits.
- After changes to a skill, agent definition or this index, validate the
  complete inventory, required sections, cross-links and CI guard together.
  Do not use cosmetic rewrites to satisfy the synchronization requirement.

## Skill maintenance

Before considering a skill change complete:

1. Keep the directory name, `name` frontmatter, this inventory row and
   `tests/check-agent-skills.cmake` `expected_skills` in lockstep.
2. Use a single-line `description: Use when ...`; state the subsystem and its
   exclusion boundary.
3. Resolve every relative Markdown link from the containing file during
   review; `agent-skills-static` enforces `references/` link/orphan closure and
   the LICENSE header. Preserve the LICENSE reference.
4. Keep `## Reconnaissance`, `## Known issues and decisions`, `## Do not` and
   `## Tracking`; require read-only subagents, AGENTS.md limits, reusable
   decisions/pitfalls, and coordinator-owned tracking edits.
5. Run compile-free checks locally only as allowed by `AGENTS.md`.
   `agent-skills-static` and `language-modernization-static` are source
   contracts; dynamic validation remains manual CI work. Never claim a check
   passed unless it was executed.
6. If a pinned policy phrase, seam name or required section changes, update
   `AGENTS.md`, the owning skill/reference, the technical doc and the checker in
   the same increment.

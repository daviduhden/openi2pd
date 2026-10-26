---
name: openi2pd-tui
description: Use when changing the openi2pd native ncursesw or Win32 TUI, terminal abstraction, TUI IPC protocol, daemon-owned administration queue or console presentation and accessibility.
---
<!--
See the LICENSE file at the top of the project tree for copyright
and license details.
-->

# openi2pd TUI and local administration

## Activate when

Use this skill for the interactive router console, terminal backends, typed TUI
protocol, status/detail views and daemon-owned administrative commands. Use
`openi2pd-torrent` for the torrent engine/control facade and
`openi2pd-linux-windows` for Win32 pipe mechanics.

Read [`../../../AGENTS.md`](../../../AGENTS.md) first. The console is native
ncursesw/Win32 and local IPC only; there is no HTTP admin console or browser
bridge.

## Inspect first

- `tui/main.cpp`, `TuiClient.*`, `Terminal.*`, `TerminalUnix.cpp`,
  `TerminalWin32.cpp`, `TuiWin32Ipc.*`, `Theme.h`.
- `libi2pd/TuiProtocol.h`, `daemon/TuiServer.*` (including `TuiServer.h`),
  `TuiServerCommon.*`, `TuiServerWin32.cpp`, `tui/TuiClientWin32.cpp`,
  `daemon/Daemon.*`.
- Torrent control types in `libi2pd_client/TorrentsControl.h` and the tunnel
  facade, but do not read engine internals from the IPC thread.
- [`../../../docs/tui-ipc.md`](../../../docs/tui-ipc.md),
  [`../../../docs/TUI-WINDOWS.md`](../../../docs/TUI-WINDOWS.md) and
  [`../../../docs/torrent/tui.md`](../../../docs/torrent/tui.md).

## Reference sources

- [`../../../docs/tui-ipc.md`](../../../docs/tui-ipc.md) — wire protocol,
  limits, data exposure, security and validation checklist.
- [`../../../docs/TUI-WINDOWS.md`](../../../docs/TUI-WINDOWS.md) — Win32
  console, named pipe, authorization and service/interactive split.
- [`../../../docs/torrent/tui.md`](../../../docs/torrent/tui.md) and
  [`../../../docs/torrent/console-matrix.md`](../../../docs/torrent/console-matrix.md)
  — torrent console behavior and parity.
- [`../../../docs/security/ipc-protocol.md`](../../../docs/security/ipc-protocol.md)
  — shared internal framing/failure rules.
- [`references/tui-checklist.md`](references/tui-checklist.md) — protocol,
  terminal and test map.

## Reconnaissance

Launch at least one read-only `explore` subagent before editing, split across
independent areas when useful. If subagents are unavailable, report that
limitation; do not substitute local execution. Ask for:

1. the presentation model and terminal/backend split;
2. every TUI request/response type, bound and authorization rule;
3. daemon-side handlers and the typed admin queue;
4. torrent/network/netDb data exposure and redaction;
5. tests, fuzzers and manual UI checks.

Ask for exact protocol versions and paths. Do not let them edit, build or run
the console.

## Parallel implementation

- Split presentation, protocol and daemon handlers by file; do not edit
  `TuiProtocol.h` or `TuiServerCommon.*` concurrently without coordinator
  review.
- Torrent owner defines engine commands/snapshots; TUI owner defines display
  and input semantics.
- Win32 terminal/pipe changes belong with `openi2pd-linux-windows`; keep the
  shared abstraction stable.
- Do not add a second transport or a generic config-writer fallback.

## Conventions

- Apply the [source documentation policy](../../../AGENTS.md#source-documentation-policy)
  to terminal ownership, local IPC deadlines and snapshot presentation.

- All administrative operations are typed, allowlisted and daemon-owned. Do
  not expose a raw engine pointer, generic `write_file`, arbitrary path or
  shell command.
- Bound every line/frame, list, field, queue and wait. Enforce one request
  lifecycle and reject malformed/unknown messages without crashing the daemon.
- Unix sockets are local and mode-protected; Windows uses a local named pipe
  with peer token checks. Do not bind a network address.
- Keep the TUI a read-only consumer of immutable snapshots except for explicit
  admin commands; commands complete through the next snapshot/result.
- Torrent control must not access engine internals, peer sockets, download
  files or JSON-RPC directly. Use the bounded local IPC control facade.
- Use semantic color tokens from `assets/branding/colors.md`; no arbitrary
  brand hex in terminal code.
- Preserve keyboard/mouse accessibility, resize behavior and clean terminal
  restoration on quit/error.

## Known issues and decisions

- `TuiServer` owns a `std::jthread` and stop token. `Stop()` = `request_stop()`
  + `join()`, then close the listener and unlink only after device/inode
  verification. Client close must not unwind into the thread.
- All client waits are bounded; `stop_requested()` must be checked in `Run`,
  `HandleClient` and `SendAll`. The Windows TUI uses a separate thread/stop
  event and is not a process boundary.
- The TUI is a local administrative client, not the router. Torrent control
  uses bounded IPC and must not read engine internals or download files.
- UI text is selected through `openi2pd-i18n`; `terminal::Put` takes a column
  budget, tab layout uses `DisplayWidth`, and translated/wide text must not be
  advanced or truncated by byte length.
- POSIX startup retries `LC_CTYPE` with `C.UTF-8`/`en_US.UTF-8` when `MB_CUR_MAX`
  is 1, because the renderer decodes UTF-8 with `mbrtowc`/`wcwidth`. Undefine
  both `KEY_F` and `KEY_F1..KEY_F12` before returning native key codes; curses
  implementations differ on which names are macros.
- TUI IPC regressions are prepared but no manual CI/live console run exists.

- New read verbs `ADDRESSBOOK-SUBSCRIPTIONS [limit]` and `BLOCKLIST [limit]`
  plus typed writes `ADMIN ADDRESS-SUB-ADD/REMOVE` and `ADMIN NETDB-BAN/UNBAN`.
  Destinations uses `s`/`x`, NetDb uses `b`/`u`. The admin queue stays
  fixed-size and allocation-free; the only URL accepted is a validated
  `http(s)` subscription source. Runtime bans never rewrite the blocklist file
  and are cleared on restart.
- The Android JNI bridge deliberately rejects generic `ADMIN` verbs; the new
  operations need a typed JNI method before they appear in the app. Do not
  widen the `torrentCommand`/admin allowlist instead.

- Administrative writes remain a closed typed set. `ADMIN TUNNEL-START`/
  `TUNNEL-STOP <name>` answer `started|stopped|already-running|already-stopped|
  unknown` plus `running=0|1`; `ADMIN RESEED` is non-blocking and returns
  `started|already-running|disabled`, with phase/last error in `netdb.reseed.*`;
  `ADMIN ADDRESS-ADD` never re-points a name (`exists`) and
  `ADMIN ADDRESS-REPLACE` is explicit. `SERVICES` carries
  `tunnel.service=name|kind|running|localEndpoint|destination` plus the
  protocol-server rows. Never add a generic config writer, an arbitrary
  path/URL argument or a web console.
- New TUI strings are added in English through `tr()` and must also be added to
  the es/ru/zh-Hans catalogs; English fallback is a tracked gap, not a
  translation (`I18N-004`).

- Torrent page `Ctrl-X` / `Ctrl-D` are confirmed session-wide removals
  (`TORRENT-REMOVE-ALL`, the latter with `files`); they are bound to the tunnel
  index and re-check the session before sending. Keep the typed verb closed and
  do not add a generic removal path.

- Torrent Comments uses `[`/`]` to page retained records (16 per page), shows
  aged timestamps, a persistence-error line and owner/rating previews for
  hidden entries, and requires `[y/N]` confirmation before hide/unhide. Strip
  bidi controls U+202A..U+202E and U+2066..U+2069 before rendering peer labels
  or text. Comment strings remain English until `I18N-004` catalog entries
  exist; do not present English as a translation.

- Status has more fields than a 24-line terminal: Up/Down/PgUp/PgDn scroll the
  fixed rows, Home/End jump to the first/last field, and the bandwidth history
  stays on the first page. `q` in a torrent detail returns to the table; only
  table-mode `q` (or `Q`) exits the TUI. A disconnected client keeps the last
  snapshots but every panel marks them `[stale]`.
- Destinations rotates one address-book auxiliary request per 6 s instead of
  issuing four blocking requests per tick. All IPC requests remain synchronous
  with bounded deadlines; do not claim a non-blocking loop.
- All untrusted text goes through `SanitizeText`, which replaces invalid
  UTF-8/C1/C0/DEL and strips bidi controls U+202A..U+202E/U+2066..U+2069.
  Locale initialization must provide a multibyte LC_CTYPE before ncursesw.
- Status admin keys include `o` for transport online/offline (confirmed) and
  Logs `O` for daemon log reopen; both use the closed typed admin queue.
- Windows installs a console control handler (Ctrl-C/Break/close/logoff/
  shutdown) that only publishes the stop flag so the normal shutdown restores
  the alternate screen. `VK_CANCEL` maps to the Ctrl-C path.

- TUI text is translated at the rendering boundary: `PutLine` only matches an
  exact full line, while `SetStatus`/`BeginPrompt` translate their input.
  Measure with `DisplayWidth` and wrap/fit in columns, never bytes. Protocol
  tokens (torrent status, peer flags, log levels, IPC verbs) stay English;
  `TorrentStatusText` maps them for display only.
- Confirmation prompts advertise `[s/N]`/`[д/Н]`; the parser accepts
  `y/yes/s/sí/д/да`. A new prompt must update both the catalog and
  `IsAffirmative`.


- `i2p::i18n::tr` with one argument returns `std::string_view`, and C++23 has
  no `std::string + std::string_view` operator. Materialise the translation
  with `std::string (i2p::i18n::tr (...))` before concatenating it. The
  `i18n-static` check rejects the unwrapped form in `tui/main.cpp`; keep both
  the code and that guard.

- Public TUI/IPC declarations receive Doxygen contracts (ownership, thread
  affinity, bounded deadlines); rendering helpers explain column/UTF-8 and
  localization invariants rather than restating code. See
  [`docs/source-documentation.md`](../../../docs/source-documentation.md).

## Do not

- Do not compile, run CTest, the console, sanitizers, fuzzers or validators
  locally.
- Do not reintroduce an HTTP admin console, web assets, HTML/CSS/JS, Node/V8,
  QuickJS or a browser bridge.
- Do not install the TUI as a service, start a second router instance or let it
  open files/peer connections directly.
- Do not leak keys, private identities, raw paths or file descriptors through
  status/log responses.
- Do not create commits, tags, releases, worktrees or temporary commits, and do not push; inspect `git status`/`git diff` before editing.
- Do not install dependencies, fetch/vendor code or perform hidden downloads to enable a local build or test. Preserve LICENSE/NOTICE/provenance and technical identifiers/paths unless an approved migration exists (AGENTS.md).

## Prepare for CI

- Add/extend `test-tui-protocol`, `test-tui-ipc-regression`,
  `test-tui-terminal`, `test-tui-pipe-name`, `test-tui-win32-pipe` as
  applicable; labels `tui`, `ipc`, `platform`.
- Extend `tests/fuzz/fuzz-tui-protocol.cpp` for new parsing surfaces.
- Prepare manual checks for resize, key/mouse navigation, long lists, escaping,
  terminal cleanup and Win32 console/VT behavior.
- Torrent admin changes need `TORRENT-*`/`PARITY-005` regressions and a daemon
  shutdown/reload check.
- Record UI/console runs as pending until the manual Windows/Linux job executes.

## Tracking

Use `TUI`/E, `TORRENT`/N and `PARITY-005`. Record protocol version, request
types, bounds, authorization, tests and the terminal/platform CI lane. Update
the TUI protocol document for any wire change. Propose a dated `PROGRESS.md` entry and any `TODO.md`/`ROADMAP.md` changes to the coordinator; append them only when explicitly assigned; subagents report proposed rows to the coordinator.

Record the exact AGENTS.md `Tracking states` value and state which validations were not run; do not collapse them into “pending”.

## References

- [TUI protocol and terminal checklist](references/tui-checklist.md)
- [TUI IPC design](../../../docs/tui-ipc.md)
- [Windows TUI](../../../docs/TUI-WINDOWS.md)
- [Global policy](../../../AGENTS.md)

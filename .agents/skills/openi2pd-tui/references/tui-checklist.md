<!--
See the LICENSE file at the top of the project tree for copyright
and license details.

openi2pd TUI protocol and terminal checklist.
-->
# TUI protocol and terminal checklist

Use with `SKILL.md`. The protocol document remains authoritative.

## Components

| Component | Files | Boundary |
|---|---|---|
| Presentation model | `tui/TuiClient.*` | no daemon internals |
| Terminal | `tui/Terminal.*`, `TerminalUnix.cpp`, `TerminalWin32.cpp` | ncursesw / Win32 VT |
| Theme | `tui/Theme.h`, `assets/branding/colors.md` | semantic tokens |
| Client transport | `tui/TuiWin32Ipc.*`, client socket code | local only |
| Daemon server | `daemon/TuiServer.*`, `TuiServerCommon.*`, `TuiServerWin32.cpp` | bounded reads, typed handlers |
| Protocol | `libi2pd/TuiProtocol.h` | versioned line/frame messages |
| Admin queue | `daemon/Daemon.*`, daemon-owned handlers | typed commands, no generic writer |
| Torrent facade | `libi2pd_client/TorrentsControl.h`, tunnel | immutable snapshots/results |

## Protocol checklist

- Version is explicit; unknown version/type is rejected.
- Every request has bounded size, field count, list length and timeout.
- Escape/control characters and newlines cannot inject a second message.
- Response data is immutable and redacted: no keys, private identity, raw file
  paths, descriptors or engine pointers.
- Authentication is local peer UID (Unix) or SID/session (Windows); failure
  closes the connection.
- Administrative operations are allowlisted and daemon-owned; the client
  cannot name arbitrary config keys or filesystem paths. Read
  `docs/tui-ipc.md` for the normative limits and closed administrative set, and
  `docs/torrent/ipc.md` for torrent command/response/queue bounds.
- Slow/abusive clients cannot block the service thread; queues are bounded and
  backpressure is explicit.
- Shutdown/restart closes the transport, removes the Unix socket and does not
  leave a stale pid/socket.

## Presentation checklist

- Resize, narrow terminals and wide tables remain usable.
- Long/UTF-8 text is bounded and escaped; no terminal corruption.
- Keyboard and mouse navigation have visible focus and help.
- Destructive actions require confirmation and show the target.
- Error/status text distinguishes pending, accepted, failed and stopped.
- Quit/error restores cursor, screen and terminal modes.
- Win32 console/VT fallback follows `docs/TUI-WINDOWS.md`.

## Test map

- `test-tui-protocol`: codec/grammar/limits.
- `test-tui-ipc-regression`: typed admin queue and daemon behavior.
- `test-tui-terminal`: terminal abstraction.
- `test-tui-pipe-name`: platform pipe naming.
- `test-tui-win32-pipe`: Windows pipe/DACL.
- `tests/fuzz/fuzz-tui-protocol.cpp`: parser fuzzing.

Add labels `tui`, `ipc`, `platform` as appropriate. Manual console checks are
still required for rendering/resize/cleanup and Win32 behavior; a unit test is
not a UI validation.

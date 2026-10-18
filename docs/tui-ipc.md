# TUI and IPC design

## Goals

- The router is a headless daemon. It never needs a terminal, stdin or a
  graphical session.
- `i2pd-tui` is a separate process and links neither `libi2pd` nor
  `libi2pdclient`.
- The channel is local, versioned, bounded and read-only.
- A broken or malicious TUI cannot crash, block indefinitely or exhaust the
  router.

## Components

```
┌─────────────┐   AF_UNIX, 0600   ┌──────────────────────────┐
│  i2pd-tui   │ ────────────────> │ i2p::tui::Server thread  │
│ ncurses UI  │ <──────────────── │ snapshots + recent logs  │
└─────────────┘   framed text     └────────────┬─────────────┘
                                               │ read-only accessors
                                     ┌─────────▼──────────┐
                                     │ router subsystem    │
                                     │ context/transports/ │
                                     │ netdb/tunnels/logs  │
                                     └────────────────────┘
```

Source files:

- `libi2pd/TuiProtocol.h` — protocol constants, parser, framing, escaping.
- `daemon/TuiServer.{h,cpp}` — UNIX socket, peer checks, snapshot collection.
- `tui/TuiClient.{h,cpp}` — options, config lookup, socket client.
- `tui/Theme.h` — semantic ncurses roles, capability detection, colour pairs.
- `tui/main.cpp` — ncurses views, refresh loop, key handling.

## Configuration

```ini
[tui]
# Enable the local socket (default: true, Unix only)
enabled = true
# Default: <datadir>/i2pd-tui.sock
socket = /var/i2pd/i2pd-tui.sock
```

The daemon logs a warning and continues if the socket cannot be created. The
TUI can also be pointed directly at a socket:

```sh
i2pd-tui --socket /run/i2pd/i2pd-tui.sock
i2pd-tui --datadir /var/i2pd
i2pd-tui --conf /etc/i2pd/i2pd.conf
```

## Wire protocol, version 1

One request per line, ASCII, maximum 512 bytes:

```
I2PDTUI/1 STATE
I2PDTUI/1 PEERS [limit]
I2PDTUI/1 TUNNELS
I2PDTUI/1 NETDB
I2PDTUI/1 DESTINATIONS
I2PDTUI/1 LOGS <after-sequence> [limit]
I2PDTUI/1 PING
I2PDTUI/1 QUIT
```

Responses:

```
I2PDTUI/1 OK <payload-length>\n<payload>
I2PDTUI/1 ERR <error-code> <message-length>\n<message>
```

Payloads are fields separated by `\n`, each `name=escaped-value`; `\`, newline
and carriage return are escaped. Unknown fields are ignored, which allows
additive evolution. A version mismatch is rejected explicitly.

Limits:

| Limit | Value |
|---|---|
| Request line | 512 bytes |
| Response payload | 256 KiB |
| Log records/request | 200 |
| Peer records/request | 200 |
| Tunnel records/request | 200 |
| Socket permissions | 0600 |
| Receive/send timeout | 5 s |

The server accepts one client at a time (backlog 4). This is sufficient for a
local console and avoids per-client threads. A client that stalls is closed by
the socket timeout.

## Data exposed

- Public router identity hash, version, caps, net ID, uptime, floodfill flag.
- IPv4/IPv6 status, testing/error flags, NTCP2/SSU2 bind state.
- Bandwidth and total transferred bytes.
- Peer count and a bounded peer snapshot (hash, sessions, attempts, active
  transport bitmask, high bandwidth).
- Tunnel IDs, direction, state, hop count and creation time (bounded).
- netDb router/floodfill/LeaseSet counts, reseed activity and local destination b32 addresses.
- Recent log records with sequence, timestamp, level and sanitized text.

Not exposed: private keys, NTCP2/SSU2 static keys, ratchet session state,
introducer tags, destination private keys, or any data that the TUI does not
need. `router.hash` is the public IdentHash.

## Security properties

1. **Filesystem permissions.** The socket is created under `umask 0077`,
   `chmod 0600`, and a pre-existing path is only replaced if it is a socket
   owned by the daemon's effective UID. `Stop()` unlinks only the exact
   device/inode it created.
2. **Peer credentials.** Linux uses `SO_PEERCRED`; OpenBSD/FreeBSD/NetBSD/macOS
   use `getpeereid()`. A different UID is rejected. Systems without a peer
   credential API still rely on the 0600 socket and private data directory.
3. **Bounded parsing.** Header length, payload length, request length and
   numeric arguments are validated before allocation or dispatch. Response
   sizes above the limit produce an error rather than a truncated snapshot.
4. **No command execution.** Version 1 is deliberately read-only. A future
   administrative command must be mediated by the daemon main loop, not run on
   the IPC thread; no such command is implemented.
5. **Terminal safety.** Log control characters are flattened in the log ring;
   the protocol also escapes newlines/backslashes. The TUI prints with
   `addnstr`, never as a format string.
6. **No privilege inheritance assumption.** The TUI is not launched by the
   daemon. `pledge`/`unveil` restrictions of the parent are not used as a
   security boundary for the child; the socket itself is the boundary.

## UI behavior

- Views: Status, Peers, Tunnels, NetDb, Logs, Help.
- Keys: `1`-`5`, `Tab`, arrows, `?`, `r`, `l` (log level), `q`.
- Refresh: state every 2 s; active peer/tunnel view every 2 s; logs every 1 s
  while visible. `getch` uses a 200 ms timeout, so the UI never blocks on the
  socket or terminal.
- Connection failure is shown in the footer; reconnect is retried every 2 s.
- `SIGWINCH` sets a flag and redraws; `SIGINT`/`SIGTERM` exit cleanly. The
  router is not signalled when the TUI exits.

## Theme and terminal capabilities

`tui/Theme.h` is the only place that calls `start_color()`,
`use_default_colors()`, `init_pair()` or `init_extended_pair()`.  Views ask for
a semantic role and never name a raw `COLOR_*` constant.  The header is
header-only, so the explicit CMake target source list does not change.  The
text identity is the lowercase word `openi2pd`; the bitmap/vector mark is not
rendered in a terminal.

The palette source is `assets/branding/colors.md`.  The TUI uses its semantic
tokens rather than the sampled logo accents, because the sampled teal/blue do
not meet normal-text contrast on a light surface.

| Role | Foreground | Background | Emphasis |
|---|---|---|---|
| `HeaderBar` | `on-surface` | `surface-variant` | — |
| `Title` | `on-surface` | `surface-variant` | bold |
| `StatusBar` | `on-surface-variant` | `surface-variant` | — |
| `SelectedRow` | `on-primary-container` | `selected` | bold |
| `SelectedTab` | `on-primary-container` | `primary-container` | bold |
| `PanelBorder` | `outline` | terminal default | — |
| `Primary` | `primary` | terminal default | bold |
| `Secondary` | `secondary` | terminal default | bold |
| `Success` | `success` | terminal default | bold |
| `Warning` | `warning` | terminal default | bold |
| `Error` | `error` | terminal default | bold |
| `Disabled` | `disabled` | terminal default | dim |
| `Muted` | `on-surface-variant` | terminal default | dim |
| `TorrentProgress` | `success` | terminal default | bold |
| `TorrentDownload` | `primary` | terminal default | bold |
| `TorrentUpload` | `secondary` | terminal default | — |
| `LogDebug` | `on-surface-variant` | terminal default | dim |
| `LogInfo` | terminal default | terminal default | — |
| `LogWarning` | `warning` | terminal default | bold |
| `LogError` | `error` | terminal default | bold |
| `LogCritical` | `background` (dark) / `on-primary` (light) | `error` | bold |

`PanelBorder` is reserved for framed views; the current views do not draw
panel frames.  Light and dark token sets are selected with a `COLORFGBG`
heuristic (falling back to dark when it is absent or unparsable).  A light
terminal that does not export `COLORFGBG` can set it explicitly, for example
`COLORFGBG='0;15'`.  Bars and selected rows always have an explicit
foreground/background pair, so they stay readable even when the heuristic is
wrong.  In monochrome, errors add underlining to bold so warning and error rows
remain distinguishable.

### Fallback order

| Capability | Detection | Rendering |
|---|---|---|
| Direct colour / truecolor | extended colour functions are available and `COLORS >= 0x1000000` | RGB-packed colour indices through `init_extended_pair()`; 8-colour ANSI if pair setup fails |
| 256 colours | `COLORS >= 256` | nearest xterm 6×6×6 cube or grey-ramp entry |
| 16 colours | `COLORS >= 16` | ANSI 0–15, bright variants where useful |
| 8 colours | `COLORS >= 8` | ANSI 0–7 |
| Monochrome | no colour support, `start_color()` failure, or `COLORS < 8` | reverse, bold, underline and dim only |

The 8/16-colour modes use standard ANSI approximations rather than exact brand
hex values; the 256-colour mode selects the nearest xterm cube or grey-ramp
entry.  This keeps the TUI readable on terminals that cannot represent the
brand palette exactly.  The ANSI fallbacks are:

- header/status: white on blue;
- selected row/tab: black on cyan (8 colours) or bright cyan (16 colours);
- primary: cyan (dark) or bright blue (light);
- secondary: bright blue (dark) or cyan (light);
- success/progress: green;
- warning: yellow; error: red;
- download: cyan/blue; upload: blue/magenta;
- muted/disabled: dim white (dark) or dim black (light);
- critical: white on red.

Additional safety rules:

- `use_default_colors()` is attempted after `start_color()`.  `-1`
  foreground/background is used only when it returned `OK`.  Otherwise the
  theme uses an explicit black/white pair selected from the background hint.
- Colour pairs are allocated from 1 up to `min(COLOR_PAIRS - 1, 63)`.  A role
  that cannot obtain a pair falls back to its monochrome attributes, so a
  restricted terminal loses colour but not legibility.
- If direct-colour pair initialisation fails, the theme falls back to the
  8-colour ANSI strategy and then monochrome.  RGB-packed indices are not
  valid 256-colour palette entries.  A build without
  `NCURSES_EXT_COLORS`/`NCURSES_EXT_FUNCS` also uses 8-colour ANSI on a
  direct-colour terminfo.
- `init_extended_color()` is not used to redefine palette slots.  Redefining
  slots would mutate the user's terminal palette; truecolor output uses
  ncurses direct-colour indices instead.  This keeps the no-raw-ANSI rule and
  avoids leaving the terminal in a modified colour state after `endwin()`.
- `Theme.h` is guarded by `NCURSES_EXT_COLORS`/`NCURSES_EXT_FUNCS` for extended
  APIs and uses only ncursesw interfaces available on OpenBSD.  No escape
  sequence is written by the TUI.

### Validation checklist

Static checks (can be run without a terminal):

- `git diff --check`.
- Search `tui/` for `\033`, `\x1b`, `printf`-style colour strings, ad-hoc
  `init_pair()` calls and raw `COLOR_*` uses outside `tui/Theme.h`.
- Confirm `tui/Theme.h` includes `<ncurses.h>` itself and only uses guarded
  extended-colour calls.
- Compile `i2pd-tui` against ncursesw on Linux and OpenBSD (not run in this
  static edit).

Dynamic checks (not run in this environment):

- `TERM=vt100` / a monochrome terminfo: header, selected row and tabs remain
  distinguishable by reverse/bold; log levels remain readable.
- `TERM=xterm` (8 colours): distinguish primary/secondary, success, warning,
  error, progress, download and upload rows.
- `TERM=xterm-16color` and `TERM=xterm-256color`: check selected row/tab and
  log colours against the terminal background.
- `TERM=xterm-direct` on a direct-colour terminal: confirm truecolor is used
  and that a failed pair initialisation falls back to 8-colour ANSI.
- `COLORFGBG=0;15` (light) and `COLORFGBG=15;0` (dark), then unset it: confirm
  the light/dark token choice and explicit bar backgrounds.
- Cycle log levels with `l`, select torrent rows, open torrent details, resize
  the terminal and reconnect the daemon.
- Run with a default-colour-less terminal description to exercise the explicit
  black/white fallback, if one is available locally.

No live TUI session or visual comparison has been executed for this change.

## Known limitations

- Tunnel/router status fields are read through existing accessors that are not
  all atomic. TSan validation is pending; see `security-audit.md`.
- Version 1 has no administrative writes (reload, reseed, shutdown). This is
  intentional until a main-loop command queue exists.
- No authentication beyond same-UID filesystem access. A same-UID process can
  already read the router's private keys, so a stronger local secret would not
  add a meaningful boundary.

## Validation

`tests/test-tui-protocol.cpp` covers request/response parsing, version
rejection, numeric bounds, field escaping and payload round-tripping. It is
compiled and registered with CTest, but no live TUI session has been executed
in this environment.

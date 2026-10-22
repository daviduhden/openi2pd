<!--
See the LICENSE file at the top of the project tree for copyright
and license details.

openi2pd: tui ipc.

Documents project documentation.

-->

# TUI and IPC design

The Unix implementation is described here. The Windows terminal, named-pipe
transport and DACL policy are documented separately in
[TUI on Windows](TUI-WINDOWS.md); the wire format, field set and limits are
shared.

## Goals

- The router is a headless daemon. It never needs a terminal, stdin or a
  graphical session.
- `i2pd-tui` is a separate process and links neither `libi2pd` nor
  `libi2pdclient`.
- The channel is local, versioned and bounded. Router state snapshots are
  read-only. Administrative writes are a closed typed set (transit, log level,
  reseed, shutdown/cancel, tunnel reload, bandwidth, share and bounded
  address-book add/remove/block/unblock) executed by a fixed daemon-owned
  queue; torrent
  actions are explicit, allowlisted commands.
- A broken or malicious TUI cannot crash, block indefinitely or exhaust the
  router.

## Components

```
┌─────────────┐   AF_UNIX, 0600   ┌──────────────────────────┐
│  i2pd-tui   │ ────────────────> │ i2p::tui::Server thread  │
│ ncurses UI  │ <──────────────── │ snapshots + recent logs  │
└─────────────┘   framed text     └────────────┬─────────────┘
                                               │ bounded accessors
                                               │ + allowlisted actions
                                     ┌─────────▼──────────┐
                                     │ router subsystem    │
                                     │ context/transports/ │
                                     │ netdb/tunnels/logs  │
                                     └────────────────────┘
```

Source files:

- `libi2pd/TuiProtocol.h` — protocol constants, parser, framing, escaping.
- `daemon/TuiServerCommon.{h,cpp}` — request dispatch and snapshot collection,
  shared by both transports.
- `daemon/TuiServer.{h,cpp}` — Unix `AF_UNIX` listener and peer checks.
- `daemon/TuiServerWin32.cpp` — Windows named-pipe listener.
- `tui/TuiClient.{h,cpp}` — Unix options, config lookup, socket client.
- `tui/TuiClientWin32.cpp` — Windows options and named-pipe client.
- `tui/TuiWin32Ipc.{h,cpp}` — Windows pipe naming and handle helpers; the
  listener DACL is built in `daemon/TuiServerWin32.cpp`.
- `tui/Terminal.h`, `tui/TerminalUnix.cpp` — shared terminal interface and
  ncursesw backend.
- `tui/TerminalWin32.cpp` — Win32 console/VT backend.
- `tui/Theme.h` — semantic roles, capability detection, colour pairs.
- `tui/main.cpp` — shared views, refresh loop and key handling.

## Configuration

```ini
[tui]
# Enable the local IPC channel (default: true; Android: false)
enabled = true
# Unix: socket path, default <datadir>/i2pd-tui.sock
# Windows: logical endpoint; the named pipe name is derived from it
socket = /var/i2pd/i2pd-tui.sock
```

The daemon logs a warning and continues if the endpoint cannot be created. The
TUI can also be pointed directly at an endpoint:

```sh
i2pd-tui --socket /run/i2pd/i2pd-tui.sock
i2pd-tui --datadir /var/i2pd
i2pd-tui --conf /etc/i2pd/i2pd.conf
```

On Windows the daemon creates a local named pipe instead of a socket; there is
no `tui.socket` filesystem path. The TUI derives the pipe name from the
configured data directory, so start both processes with the same data
directory/configuration. Pipe ACLs and service-account behavior are covered in
[TUI on Windows](TUI-WINDOWS.md).

## Wire protocol, version 1

One request per line, ASCII, maximum 4096 bytes:

```
I2PDTUI/1 STATE
I2PDTUI/1 PEERS [limit]
I2PDTUI/1 CONNECTIONS [limit]
I2PDTUI/1 TUNNELS
I2PDTUI/1 NETDB
I2PDTUI/1 NETDB-ROUTERS [limit]
I2PDTUI/1 NETDB-LEASESETS [limit]
I2PDTUI/1 DESTINATIONS
I2PDTUI/1 SERVICES [limit]
I2PDTUI/1 ADDRESSBOOK [limit]
I2PDTUI/1 ADDRESSBOOK-BLOCKED [limit]
I2PDTUI/1 CONFIG
I2PDTUI/1 LOGS <after-sequence> [limit]
I2PDTUI/1 ADMIN TRANSIT ON|OFF
I2PDTUI/1 ADMIN LOGLEVEL none|critical|error|warn|info|debug
I2PDTUI/1 ADMIN RESEED
I2PDTUI/1 ADMIN SHUTDOWN GRACEFUL|IMMEDIATE|CANCEL
I2PDTUI/1 ADMIN RELOAD TUNNELS
I2PDTUI/1 ADMIN BANDWIDTH K|L|M|N|O|P|X
I2PDTUI/1 ADMIN SHARE 0..100
I2PDTUI/1 ADMIN ADDRESS-ADD <name.i2p> <full-base64-destination>
I2PDTUI/1 ADMIN ADDRESS-REMOVE <name.i2p>
I2PDTUI/1 ADMIN ADDRESS-BLOCK <name-or-wildcard.i2p>
I2PDTUI/1 ADMIN ADDRESS-UNBLOCK <name-or-wildcard.i2p>
I2PDTUI/1 TORRENT-RECHECK <tunnel-index> <torrent-id>
I2PDTUI/1 TORRENT-META-COMMENT <tunnel-index> <torrent-id> <text>
I2PDTUI/1 TORRENT-META-CREATED-BY <tunnel-index> <torrent-id> <text>
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
| Request line | 4096 bytes |
| Response header line | 128 bytes |
| Response payload | 256 KiB |
| Field name | 64 bytes |
| Log records/request | 200 |
| Peer records/request | 200 |
| Connection records/request | 200 |
| netDb directory records/request | 200 |
| Service records/request | 200 |
| Address-book records/request | 200 |
| Tunnel records/request | 200 |
| Administrative argument | 16 bytes |
| Torrent records/request | 200 |
| Torrent argument | 2048 bytes |
| Configuration records/request | 128 |
| Configuration value | 512 bytes |
| Socket permissions (Unix) | 0600 |
| Receive/send timeout | 5 s |

`tests/test-tui-protocol.cpp` checks the inclusive limit and rejects the first
byte over it. The Windows transport applies the same limits; see
[TUI on Windows](TUI-WINDOWS.md).

### Configuration snapshot (`CONFIG`)

`CONFIG` takes no arguments and returns the effective startup configuration for
a fixed, compiled-in allowlist. Every record is typed:

```
I2PDTUI/1 OK 123
config.schema=1
config.readonly=1
config.option.0.name=bandwidth
config.option.0.type=string
config.option.0.value=L
config.option.1.name=share
config.option.1.type=int
config.option.1.value=100
...
config.count=30
```

The snapshot is served from the in-memory, finalized option map; handling the
request never opens or writes a configuration file. `config.schema` versions
the payload layout (currently `1`), `config.count` is the number of options
actually emitted, and each `config.option.N` has a `name`, a `type` (`bool`,
`int`, `uint16`, `uint32` or `string`) and a `value`.
Booleans use `1`/`0`. The allowlist covers router identity/network settings,
NTCP2/SSU2, UPnP, NTP, reseed verification, persistence and the TUI switch. It
deliberately excludes private keys, passwords, credentials, certificate or key
files, arbitrary filesystem paths, URLs, proxy endpoints and any generic
`GetOption(name)` lookup.

String values are bounded to 512 bytes and control characters are replaced by
spaces before the normal field escaping. At most 128 options are returned; a
response is still subject to the 256 KiB payload limit. Unknown fields must be
ignored, so later schemas can add fields without a new protocol version.

Any argument after `CONFIG` is rejected with error code 4 (`BadArgument`).
`CONFIG` is read-only in version 1. There is no `CONFIG-SET` command: it is
rejected as an unknown command (error code 3), exactly like any other
unrecognised command. A safe writer requires validating a named allowlisted
option and applying it on the daemon main-loop command queue, outside the IPC
thread. The fixed daemon-owned admin queue exists for the explicitly typed
operations; there is still no generic configuration writer. Until a typed
operation exists, configuration changes are
made in the daemon configuration and require a restart.

The server accepts one client at a time (backlog 4). This is sufficient for a
local console and avoids per-client threads. A client that stalls is closed by
the socket timeout.

### Connections and netDb directories

`CONNECTIONS [limit]` returns only peers with at least one established
transport session. Each `connection` record has the same bounded fields as
`PEERS`: base64 router hash, session count, attempt count, transport bitmask,
high-bandwidth flag and active transport names. `connections.count` reports
the number returned. The snapshot is taken under the transport peer mutex and
contains no session objects.

`NETDB-ROUTERS [limit]` returns up to 200 bounded router records:
base64 hash, version, raw caps mask, floodfill flag, v4/v6 reachability,
timestamp and reachable flag. `NETDB-LEASESETS [limit]` returns the hash,
store type, encryption type, expiration, earliest lease date, validity and
expiry flags. Both use bounded container snapshots and never expose private
keys, addresses or raw records.

`SERVICES [limit]` returns a bounded, read-only inventory of configured client
and server TCP/UDP tunnels, HTTP/SOCKS proxies and the SAM, BOB and I2CP
servers. Each `service` record is `kind|name|endpoint|destination|enabled`;
only public b32 destinations are included. The snapshot is copied under the
client-context locks and never exposes private keys, listener objects or
filesystem paths.

### Administrative operations

Version 1 exposes a closed set of typed router-wide writes:

```
I2PDTUI/1 ADMIN TRANSIT ON
I2PDTUI/1 ADMIN TRANSIT OFF
I2PDTUI/1 ADMIN LOGLEVEL info
I2PDTUI/1 ADMIN RESEED
I2PDTUI/1 ADMIN SHUTDOWN GRACEFUL
I2PDTUI/1 ADMIN SHUTDOWN IMMEDIATE
I2PDTUI/1 ADMIN SHUTDOWN CANCEL
I2PDTUI/1 ADMIN RELOAD TUNNELS
I2PDTUI/1 ADMIN BANDWIDTH L
I2PDTUI/1 ADMIN SHARE 50
I2PDTUI/1 ADMIN ADDRESS-ADD site.i2p <full-base64-destination>
I2PDTUI/1 ADMIN ADDRESS-REMOVE site.i2p
I2PDTUI/1 ADMIN ADDRESS-BLOCK *spam*.i2p
I2PDTUI/1 ADMIN ADDRESS-UNBLOCK *spam*.i2p
```

`ADDRESSBOOK [limit]` returns bounded `address.count` and
`address=name|b32-or-b33|blinded` rows. `ADDRESSBOOK-BLOCKED [limit]` returns
`blocked.count` and `blocked=<name-or-pattern>` rows from the deleted-host
tombstone file. Address mutations accept only a Java-compatible `.i2p` label
and a full base64 identity; block/unblock accept a bounded `.i2p` name or
`*` wildcard. They never accept a path, URL, key filename or subscription URL.
Removing an address persists an exact tombstone and subscription merges skip
matching names; an explicit add clears matching patterns.

The fixed command queue is owned by the daemon, not the IPC thread. A request
is validated and copied into a fixed-capacity slot (eight slots); the daemon
thread executes it and returns a typed payload containing `admin`, `result`
(`ok`/`accepted`/`error`), `code`, `value`, and `message`. A command that is
still running after the bounded wait is reported as `accepted`; further
submissions are rejected as queue-full when every slot is occupied.

`ADMIN TRANSIT` updates the atomic `RouterContext` acceptance flag only. It
does not create, tear down or modify existing tunnels. `ADMIN LOGLEVEL` updates
the logger's atomic minimum level; valid values are `none`, `critical`,
`error`, `warn`, `info` and `debug`. `ADMIN RESEED` requests the existing
single-flight NetDb reseed. `ADMIN SHUTDOWN` uses the daemon's graceful/immediate
shutdown paths; immediate shutdown requires a separate typed confirmation in
the TUI. `ADMIN RELOAD TUNNELS` reuses the client tunnel reload path. Bandwidth
accepts only the one-letter classes `K/L/M/N/O/P/X`; share accepts `0..100`.

None of these commands writes the configuration file, accepts a path, URL or
shell string, or exposes a generic option name. Changes last for the current
router process and are reported by the next `STATE`/`CONFIG` snapshot. Arguments
are validated before dispatch and trailing tokens are rejected.

## Data exposed

- Public router identity hash, version, caps, net ID, uptime, floodfill flag.
- IPv4/IPv6 status, testing/error flags, NTCP2/SSU2 bind state.
- Bandwidth and total transferred bytes.
- Peer count, connected-peer count and a bounded peer snapshot (hash,
  sessions, attempts, active transport names, high bandwidth).
- A bounded connected-peer snapshot for the Connections view.
- A bounded client-service inventory (`SERVICES`) without keys or paths.
- Tunnel IDs, direction, state, hop count and creation time (bounded).
- netDb router/floodfill/LeaseSet counts, reseed activity, bounded router and
  LeaseSet directories, and a separate local-destination (b32) snapshot.
- A typed, allowlisted configuration snapshot (`CONFIG`) with router,
  transport, service and persistence switches. No secrets or paths.
- Recent log records with sequence, timestamp, level and sanitized text, plus
  the current daemon log threshold.

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
4. **Bounded administrative operations.** Router snapshots are read-only,
   including the `CONFIG` allowlist, which contains no keys, credentials,
   paths, URLs or generic option access. Torrent operations (start, stop,
   reannounce, remove, add) are explicit, allowlisted protocol commands; they
   are mediated by the torrents tunnel's service thread and never execute
   shell commands or expose arbitrary file paths. Router-wide writes are the
   closed `ADMIN` set above; each is a typed enum/range, not a generic option
   name. The IPC thread only submits to the daemon-owned fixed queue and waits
   boundedly; reseed, reload, shutdown, bandwidth, share, transit and logger
   updates execute on the daemon thread. No command performs arbitrary
   configuration writes, accepts a path/URL, or runs unbounded work on the IPC
   thread. Any future command must remain operation-specific and validate its
   complete input.
5. **Terminal safety.** Log control characters are flattened in the log ring;
   the protocol also escapes newlines/backslashes. The TUI prints with
   `addnstr`, never as a format string.
6. **No privilege inheritance assumption.** The TUI is not launched by the
   daemon. `pledge`/`unveil` restrictions of the parent are not used as a
   security boundary for the child; the socket itself is the boundary.

The Windows transport replaces the socket/peer-credential layer with a local
named pipe and an explicit DACL. It keeps the same bounded parser and snapshot
code, but its ACLs and cross-user rejection are pending runtime validation.

## UI behavior

- Views: Status, Peers, Tunnels, Torrents, NetDb, Logs, Services, Help,
  Destinations, Config, Connections; `0` selects Connections, `v` selects
  Services and `1`-`9` keep the original order.
- Keys: `1`-`9`, `v`, `0` (Connections), `Tab`, arrows, `?`, `r`, `q`. The
  original `1`-`9` mappings are unchanged; Connections is `0` and Services is
  `v`. All eleven views are reachable by cycling with `Tab` or the arrow keys.
  Narrow terminals show the active view in the header instead of truncated tab
  labels.
- Status: `t` opens a typed confirmation and toggles transit acceptance;
  `e` confirms a NetDb reseed, `H` reloads tunnels, `s`/`S` begin graceful or
  immediate shutdown, `c` cancels a pending graceful drain, and `b`/`%` set the
  bandwidth class/share ratio. The view includes a 60-sample bandwidth history,
  transport state, 1/15-second rates, tunnel queues, creation success and netDb
  counters.
- Connections: bounded table of established transport sessions, sorted by
  session count; `t` also toggles transit acceptance.
- NetDb: `n` switches between the bounded router and LeaseSet directories;
  Up/Down/PgUp/PgDn/Home/End scroll the selected directory.
- Logs: `/` filters records by text, `c` clears the filter, `l` cycles the
  local display threshold and `L` cycles the daemon log level over secure IPC.
  A daemon uptime decrease (router restart) clears the local log cursors so the
  view resumes streaming instead of waiting forever for a reset sequence.
- Refresh: state every 2 s; active peer/connection/tunnel/netDb/destination/
  service/config view every 2 s; netDb directories every 5 s; logs every 1 s
  while visible. `getch` uses a 200 ms timeout, so the UI never blocks on the socket
  or terminal.
- Destinations is a dedicated bounded view of the local client b32 addresses,
  address-book entries and blocked patterns; it does not share the NetDb panel.
  `a`/`d` add/replace or remove an address; `b`/`u` block or unblock a
  subscription name or wildcard.
- Services is a bounded list of configured client/server tunnels, proxies and
  local protocol servers; `v` opens it and Up/Down/PgUp/PgDn scroll it.
- Config is a read-only table of the allowlisted snapshot, with the option
  name, declared type and value. Up/Down and PgUp/PgDn scroll it. There is no
  edit key in version 1.
- If the terminal is below the minimum drawable size, input prompts are
  suspended rather than committed invisibly; Esc cancels and resizing restores
  the normal view.
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
- Version 1 has the closed typed administrative set described above, executed
  through a fixed daemon-owned queue. Floodfill changes, arbitrary configuration
  writing, paths, URLs and generic command execution remain intentionally
  unavailable. Toggling transit does not modify existing tunnels.
- `CONFIG` is a read-only snapshot. Editing is pending: it needs (a) a named
  allowlist with per-option type/range validation, (b) reuse of the existing
  daemon main-loop command queue so `SetOption()`/subsystem updates never run
  on the IPC thread, and (c) an explicit policy for which settings take effect
  without a restart. `CONFIG-SET` is intentionally not accepted before those
  exist.
- No authentication beyond same-UID filesystem access. A same-UID process can
  already read the router's private keys, so a stronger local secret would not
  add a meaningful boundary.

## Validation

`tests/test-tui-protocol.cpp` covers request/response parsing, version
rejection, numeric bounds, field escaping, framing, inclusive size limits,
payload round-tripping, the `CONFIG` request/allowlist bounds, the
`CONNECTIONS`/`NETDB-*` limits and strict `ADMIN` validation for transit,
loglevel, reseed, shutdown, tunnel reload, bandwidth, share and bounded
address-book mutations. It also checks that `CONFIG-SET` remains an unknown
command and that the largest
`config.option.N` field name fits the wire field-name limit.
`tests/test-tui-ipc-regression.cpp` dispatches the bounded directory and
service views, the typed admin queue and an atomic daemon log-level update over
the real local socket; it also checks that an accepted-but-timed-out command is
still executed and that a remote error code is preserved.
`tests/test-tui-win32-pipe.cpp` (Windows only) covers
the named-pipe/DACL primitives, not console rendering. Both are registered with
CTest; no live Windows TUI session or cross-user ACL test has been executed in
this environment. See [TUI on Windows](TUI-WINDOWS.md#verification).

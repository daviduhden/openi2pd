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

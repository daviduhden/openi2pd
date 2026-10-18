# Torrent TUI

Status: **source implementation complete; dynamic validation pending.** The
console uses ncursesw and the local `I2PDTUI/1` administrative socket. It has no
browser dependency.

## Navigation

The main TUI retains Status, Peers, Tunnels, NetDb, Logs and Help and adds a
Torrents section. Key assignments are implemented in `tui/main.cpp` and mirrored in the help
screen; the model is:

| Key | Action |
|---|---|
| `1`..`7` | switch main view |
| `Tab` / arrows | move between views/fields |
| `Up`/`Down`, PgUp/PgDn, Home/End | select/scroll |
| `Enter` | open details / confirm |
| `a` | add magnet |
| `A` | add `.torrent` by local path |
| `p` | start/pause selected torrent |
| `R` | reannounce selected/all depending on context |
| `d` | remove torrent |
| `D` | remove torrent and local data (explicit confirmation) |
| `x` / `z` | start all / stop all |
| `/` | search/filter by name or info hash |
| `s` | cycle sort column/direction |
| `Esc` | close details/dialog |
| `q` | quit TUI (router keeps running) |

## Torrent table

The table shows name, state, total size, progress, downloaded/uploaded,
download/upload rate, peers (seeders/leechers), ratio/ETA and the relevant
error. Long names are truncated by display width without splitting UTF-8
sequences. States are distinguishable by text/symbol as well as color.

Empty states:

- no torrents tunnel configured;
- torrents tunnel configured but no torrents;
- daemon disconnected or IPC error;
- a torrent whose metadata is still being fetched (magnet).

## Details

The details view is tabbed or sectioned by:

- general: id, name, info hash, magnet, status, error, size, pieces;
- files: path, length, completed, remaining;
- peers: identity, client, flags, progress, rates;
- trackers: announce URL, peers/seeders/leechers, last result/times;
- session and DHT counters when provided by the engine;
- diagnostics/errors.

Unsupported engine capabilities are shown as unavailable, not fabricated.
The details view is refreshed at a bounded interval and does not block drawing.

## Actions and safety

- Add file sends a local path to the daemon; the daemon validates that it is a
  regular, bounded `.torrent` file and reads it itself.
- Add magnet sends the bounded magnet string to the daemon.
- Remove and remove-with-files are distinct commands. Local-data removal
  requires a second explicit confirmation and is bounded to the configured
  torrents directory.
- All commands show a result/error in the footer. No shell commands are
  executed from the TUI.
- The TUI never opens a peer connection, writes a download, or reads private
  keys.

## Refresh and performance

Snapshots are requested at a bounded cadence when the Torrents view is active.
Lists are capped by the protocol. Rendering only iterates the visible rows and
uses ncurses `erase`/targeted writes; there is no unbounded log/torrent
accumulation in the client. A slow or disconnected daemon keeps the last
snapshot and reports the connection error.

## Validation

Prepared tests cover protocol parsing, action serialization, empty/error
states, key handling and long names. Live terminal tests (resize, Unicode,
reconnect, large torrent lists) remain to be run by the user; see
`docs/torrent/architecture.md` and `TODO.md` for status.

<!--
See the LICENSE file at the top of the project tree for copyright
and license details.

openi2pd: tui.

Documents project documentation.

-->

# Torrent TUI

Status: **source implementation complete; dynamic validation pending.** The
console uses ncursesw and the local `I2PDTUI/1` administrative socket. It has no
browser dependency.

## Navigation

The main TUI retains Status, Peers, Tunnels, NetDb, Destinations, Logs,
Config, Connections and Help and adds a Torrents section. Key assignments are
implemented in `tui/main.cpp` and mirrored in the help screen; the model is:

| Key | Action |
|---|---|
| `1`..`9`, `0` | switch main view (Torrents is `4`, Connections is `0`) |
| `Tab` / arrows | move between views/fields |
| `Up`/`Down`, PgUp/PgDn, Home/End | select/scroll |
| `Enter` | open details / confirm |
| `a` | add magnet |
| `A` | add `.torrent` by local path |
| `p` | pause/resume selected torrent |
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

Tracker editing: select a torrent, pause it, then press `t` to add an I2P
tracker URL or `T` to remove one. Complete metadata is required. Resume after
editing. Changes queue persistence; errors are reported through the bounded
local IPC. Editing an endpoint means removing its old URL and adding its new
URL. The infohash stays unchanged.

## Complete native controls in this increment

`n` creates from a file/directory directly below the authorized torrent root.
`N` accepts advanced creation options: `pieceBytes public|private trackerCount
tier:URL... source`. Zero selects automatic piece size; tiers start at zero
and are contiguous. `K` requests cancellation. Session details show hashed
bytes and file counts. Example: `0 private 2 0:http://first.i2p/a
1:http://second.i2p/a payload`.

The Files detail tab shows file indices and priorities. `f` accepts `index
priority`, with 0 skipped, 1 normal and 2 high. Shared pieces use the highest
selected priority; all bytes of those pieces are verified. Selected completion
does not announce a partially selected torrent as a full seeder.

The Comments tab shows local IDs, ratings, labels, time and rating statistics.
`c` switches ON/OFF, `C` submits `rating owner|text` (empty owner is allowed),
and `H` hides a local ID. Prompts remain tied to the captured tunnel, torrent
ID and infohash. All operations use authenticated bounded IPC; the TUI opens
no payloads, peer sockets or engine objects.

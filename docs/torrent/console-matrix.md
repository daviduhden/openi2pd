<!--
See the LICENSE file at the top of the project tree for copyright
and license details.

openi2pd: console matrix.

Documents project documentation.

-->

# Torrent console functionality matrix

Original console: `contrib/torrents/web/` (browser HTML/CSS/JS, removed in the
native migration). Engine: `libi2pd_client/Torrents*`. New console: ncursesw
Torrents view over the local TUI IPC. "Engine" means the operation already
exists in C++; it is not being reimplemented merely because JavaScript drove
it.

| Capability | Original JS console | Engine support | Native TUI | Status/notes |
|---|---|---|---|---|
| List torrents | DOM table, 5 s refresh | `GetTorrents`, IDs | Torrent table, bounded refresh | Implemented / validation pending |
| Name/status/progress | JS formatting, color bar | `GetName/GetStatus/GetLeft` | Text + progress/% + state | Implemented / validation pending |
| Size/have/left | `formatSize` | lengths, completed bytes | Same values, native format | Implemented / validation pending |
| Down/up rates | `rateDownload/rateUpload` | per-connection sums | Table + details | Implemented / validation pending |
| Peers S/L | JS counts peer progress | tracker stats + connections | Table + peer details | Implemented / validation pending |
| Ratio/ETA | JS `formatETA` | rates/left | Native calculation | Implemented / validation pending |
| Errors | `errorString` | `GetErrorStr` | Table + details/footer | Implemented / validation pending |
| Session stats | JS `session-stats` | tunnel counters | Details/session | Implemented / validation pending |
| DHT stats | JS `dht-stats` | DHT getters | Details/DHT | Implemented / validation pending |
| Add `.torrent` | File input, base64 to RPC | `AddTorrent` | Path submitted to daemon; daemon reads bounded file | Implemented / validation pending |
| Add magnet | Prompt, RPC | `AddMagnet` | Native dialog/line input | Implemented / validation pending |
| Start/pause | Buttons, RPC | `Pause` command → `StopTorrent` | `p` pause with status | Implemented / validation pending |
| Resume | start button | `Resume` command → `StartTorrent` | `p` resume with status | Implemented / validation pending |
| Reannounce | Button, RPC | `ReannounceTorrent` | Key per selection/all | Implemented / validation pending |
| Remove torrent | Confirm, RPC | `RemoveTorrent(id,false)` | Explicit key/confirmation | Implemented / validation pending |
| Remove + data | Confirm, RPC | `RemoveTorrent(id,true)` | TUI separate key + second confirmation; Android menu action with destructive dialog | Implemented / validation pending |
| Start/stop/reannounce all | Bottom row buttons | `*AllTorrents` | Keys | Implemented / validation pending |
| General details | Modal/fieldset | public getters | Details general | Implemented / validation pending |
| Files details | Table | files/completed | Details files | Implemented / validation pending |
| Peers details | Table | connections/accessors | Details peers | Implemented / validation pending |
| Trackers details / edit | Table | tracker stats + `SetTrackerTiers` | Details trackers; metadata survives rewrite | Implemented / validation pending |
| Metainfo edit | Edit form | `SetMetaComment`, `SetCreatedBy` | TUI e/E and Android dialog; paused torrent | Implemented / validation pending |
| Sort/filter/search | None | client-side data | Native sort/search | Implemented / validation pending |
| Keyboard-only | Browser focus | n/a | ncurses keys | Implemented / validation pending |
| Multi-tunnel | One mount/path per tunnel | map of tunnels | Tunnel index selector/list | Implemented / validation pending |
| File selection/priorities | None | `SetFilePriority`, persisted priorities | Details tab + `f`; Android priority dialog | Implemented / validation pending |
| Force recheck | Button (stopped torrent) | `RequestLocalVerification(true)` | `V` for a paused torrent; Android menu action | Implemented / validation pending |
| Queue/bandwidth limits | None in console | No queue or token bucket | Not advertised in TUI | Fork-specific resource policy; Java has no queue or per-torrent rate limit |
| Browser HTML/CSS/JS | Required | n/a | Removed | Target: no web assets |
| JSON-RPC compatibility | `rpc.js` consumer | `TorrentsRPC` | Separate optional API | Retained only if audited; not used by TUI |

## Parity rule

A row can only move to "implemented" when the native operation is wired through
the typed control facade and the TUI action is reachable and tested. Engine
operations remain in C++; no engine rewrite is implied by removing the browser
console.

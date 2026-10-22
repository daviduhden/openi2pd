<!--
See the LICENSE file at the top of the project tree for copyright
and license details.

openi2pd: TUI WINDOWS.

Documents project documentation.

-->

# TUI on Windows

Status: **static integration only**. The Windows terminal backend, named-pipe
transport and CMake targets are present in this tree, but no Windows console,
service account, DACL, cross-user rejection or MSVC/MSYS2 run has been executed
for this change. Runtime validation is pending; see
[Verification](#verification).

## Design

`i2pd-tui` remains a separate, optional process. It never links `libi2pd`,
`libi2pdclient` or `libi2pdlang`, and the router never reads a terminal or
depends on a console. Both backends speak the same bounded protocol:

```
┌──────────────────────┐                      ┌──────────────────────────┐
│ i2pd-tui             │  local IPC transport │ i2p::tui server thread   │
│ shared views/keys    │ ───────────────────> │ request validation       │
│ platform terminal    │ <─────────────────── │ snapshots / log ring     │
└──────────────────────┘   framed text        └────────────┬─────────────┘
                                                           │ bounded reads
                                                           │ + typed atomics
                                                 ┌─────────▼──────────┐
                                                 │ router subsystems  │
                                                 └────────────────────┘
```

| Concern | Shared | Unix | Windows |
|---|---|---|---|
| Views, key handling, refresh loop | `tui/main.cpp` | — | — |
| Semantic terminal attributes | `tui/Theme.h` | ncursesw roles | Win32/VT attributes |
| Wire format and limits | `libi2pd/TuiProtocol.h` | same | same |
| Terminal setup/input/output | — | `tui/TerminalUnix.cpp` | `tui/TerminalWin32.cpp` |
| Client transport | — | `tui/TuiClient.cpp` | `tui/TuiClientWin32.cpp` |
| Transport plumbing | — | — | `tui/TuiWin32Ipc.cpp/.h` |
| Snapshot collection and dispatch | `daemon/TuiServerCommon.cpp/.h` | same | same |
| Listener and peer policy | — | `daemon/TuiServer.cpp` | `daemon/TuiServerWin32.cpp` |

The build never selects `TuiServer.cpp`, `TuiClient.cpp` or
`TerminalUnix.cpp` on Windows, and never selects the Win32 files on Unix. The
Windows TUI does not use Curses, PDCurses or an HTML/JavaScript console.

## Unix backend

- `TerminalUnix.cpp` owns ncursesw initialisation, resize handling and input.
- `tui/Theme.h` remains the only place that allocates colour pairs, with
  8/16/256/direct-colour and monochrome fallbacks.
- `daemon/TuiServer.cpp` creates an `AF_UNIX` stream socket, normally
  `<datadir>/i2pd-tui.sock`, mode `0600`.
- Linux checks `SO_PEERCRED`; OpenBSD/FreeBSD/NetBSD/macOS use `getpeereid()`.
  A different effective UID is rejected.
- The socket path is covered by the existing OpenBSD `unveil` rules with
  `rwc`; no additional pledge promise is required.

## Windows backend

### Terminal

`TerminalWin32.cpp` uses the Win32 console API and virtual-terminal (VT)
sequences for the shared views. It enables VT processing on the output handle
when the console supports it and uses wide-character console I/O for text.
`main.cpp` keeps the same layout, key bindings and refresh cadence as the Unix
build; only the terminal primitives differ.

Requirements:

- Windows 10 or newer, or a terminal that provides VT processing.
- A real console or a terminal emulator attached to the process; redirected
  stdin/stdout is not a supported interactive TUI target.
- A font with the glyphs used by the selected view. No bitmap or vector logo is
  rendered in the terminal.

The backend does not write raw ANSI sequences outside `TerminalWin32.cpp`, and
does not assume truecolor. If extended colour is unavailable it falls back to
the same 16/8/monochrome roles as the Unix theme.

### IPC transport

The Windows transport is a **local named pipe**, not a TCP port and not a
filesystem socket:

```
\\.\pipe\openi2pd-tui-<16 lowercase hex digits>
```

Both `i2pd.exe` (server) and `i2pd-tui.exe` (client) derive the name from the
same local endpoint: `[tui] socket` when set, otherwise
`<datadir>/i2pd-tui.sock`. `tui/TuiWin32Ipc.cpp` normalises separators and
ASCII case, hashes the normalised bytes with 64-bit FNV-1a and formats the
16-digit suffix. The derivation is one-way: a caller-supplied
`\\.\pipe\...` value is hashed, never opened verbatim, so `--socket` cannot
select an arbitrary existing pipe. Only local pipe clients are accepted;
remote SMB access is refused with `PIPE_REJECT_REMOTE_CLIENTS`.

Request/response framing, escaping, version rejection and record limits are
identical to Unix and are implemented in `libi2pd/TuiProtocol.h`. The Windows
listener reuses `TuiServerCommon` for snapshot collection, so a platform bug
cannot change the field set or parser bounds.

### Authorization policy

The Windows listener enforces the following; the concrete descriptor and
handle flags live in `TuiServerWin32.cpp` and require review on a Windows host:

1. **Explicit protected DACL.** The daemon reads its own process token and
   grants `FILE_ALL_ACCESS` only to that user SID, local `SYSTEM` and the
   built-in Administrators group. The descriptor sets `SE_DACL_PROTECTED` so
   parent-directory inheritance cannot broaden it. A `NULL` DACL (everyone)
   is never used.
2. **No remote clients.** The pipe is created with
   `PIPE_REJECT_REMOTE_CLIENTS`.
3. **First-instance ownership.** `FILE_FLAG_FIRST_PIPE_INSTANCE` makes
   creation fail if another process already owns the name, preventing a
   squatter from intercepting connections. The daemon re-creates only its own
   single instance between clients.
4. **No network endpoint.** The TUI never listens on a TCP port and never
   exposes the protocol through I2P, a proxy or the HTTP admin console (which
   has been removed).
5. **Bounded I/O.** Request lines, response payloads, numeric arguments and
   record counts are validated before allocation, exactly as on Unix. A stalled
   client must not block the router's network threads.
6. **No impersonation.** The server reads the pipe as itself; it does not call
   `ImpersonateNamedPipeClient` or execute commands on behalf of the client.
7. **Bounded lifetime.** The router owns the listening instance. If the TUI
   exits or crashes, the listener remains available for a later client.

The client also verifies the server before sending a request: it reads the
pipe server process image and requires `i2pd.exe`, then requires the server
user SID to be the TUI's own SID or `LocalSystem`, or requires either token to
be a member of Administrators. A different image or unauthorized user is
refused. This is defence in depth against a same-machine process that wins the
pipe name. Renaming the daemon executable requires updating this check.

A same-account process is already inside the router's trust boundary: it can
read the router keys and data directory. The pipe ACL is therefore a
same-account boundary, not a defence against a compromised user account.

### Service versus interactive TUI

The user-mode default data directory follows the router: `i2pd.conf` next to
the executable selects the executable directory; otherwise `%APPDATA%\i2pd`
is used. `--service` uses `%PROGRAMDATA%\i2pd`. Use `--datadir` or the same
configuration when the daemon and TUI run under different accounts.

A Windows service has no interactive console. Keep the roles separate:

- Run `i2pd.exe` as the service (or interactively for a foreground test).
- Start `i2pd-tui.exe` from the desktop session that is allowed by the pipe
  DACL.
- The TUI is **not** a service and must not be registered as one.
- If the service runs under `LocalSystem` and the pipe DACL only grants
  `LocalSystem`, an interactive user cannot connect. Either run the router
  under the interactive account for local administration, grant the intended
  administrative group explicitly in the service configuration, or use the
  documented service account model. Do not broaden the DACL to `Everyone` as a
  shortcut.

The same data-directory and configuration rules apply to both processes. The
TUI does not read `router.keys`.

## Wire protocol and limits

One request per line, ASCII:

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
I2PDTUI/1 LOGS <after-sequence> [limit]
I2PDTUI/1 ADMIN TRANSIT ON|OFF
I2PDTUI/1 ADMIN LOGLEVEL none|critical|error|warn|info|debug
I2PDTUI/1 ADMIN RESEED
I2PDTUI/1 ADMIN SHUTDOWN GRACEFUL|IMMEDIATE|CANCEL
I2PDTUI/1 ADMIN RELOAD TUNNELS
I2PDTUI/1 ADMIN BANDWIDTH K|L|M|N|O|P|X
I2PDTUI/1 ADMIN SHARE 0..100
I2PDTUI/1 TORRENT-TUNNELS
I2PDTUI/1 TORRENTS <tunnel-index> [limit]
I2PDTUI/1 TORRENT-* ...
I2PDTUI/1 PING
I2PDTUI/1 QUIT
```

Responses use an explicit payload length:

```
I2PDTUI/1 OK <payload-length>\n<payload>
I2PDTUI/1 ERR <error-code> <message-length>\n<message>
```

| Limit | Value |
|---|---|
| Request line | 4096 bytes |
| Response payload | 256 KiB |
| Field name | 64 bytes |
| Peer/connection/netDb/tunnel/service/log/torrent records per request | 200 |
| Address-book records per request | 200 |
| Torrent file records per request | 1000 |
| Torrent argument | 2048 bytes |
| Administrative argument | 16 bytes |
| Response header line | 128 bytes |

Router snapshots are read-only. Torrent actions are explicit protocol commands,
are confirmed in the TUI and are mediated by the daemon's torrent service
thread. `ADMIN` exposes a closed set of typed operations (transit, log level,
reseed, shutdown/cancel, tunnel reload, bandwidth and share); the IPC thread
submits them to a fixed daemon-owned queue and never mutates router, tunnel,
NetDb or logger state itself. They are not persisted to the configuration file,
accept no path/URL/generic option, and never execute a command or open a file on
the IPC thread. The Windows named-pipe DACL remains the authorization boundary
for these operations.

## Build and install

### Unix

```sh
cmake -S build -B build-output -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DI2PD_BUILD_TUI=ON
cmake --build build-output --parallel --target i2pd i2pd-tui
sudo cmake --install build-output --prefix /usr
```

`I2PD_BUILD_TUI=ON` is the default. If ncursesw is not found, CMake emits a warning
and builds without `i2pd-tui`; the router itself is unaffected.

### Windows with MSYS2 / Clang64

The maintained manual workflow uses this path because the MSYS2 packages
provide Boost, OpenSSL, zlib and MiniUPnPc:

```sh
cmake -S build -B build-output -G Ninja \
  -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build-output --parallel --target i2pd i2pd-tui
```

No Curses package is required on Windows.

### Windows with MSVC

Use CMake 3.24 or newer and Visual Studio 2022 17.5 (MSVC 19.35) or newer.
Dependencies may come from vcpkg, a system prefix or a package manager; the
native CI job uses the runner's vcpkg only to build dependencies, not to run
the product:

```powershell
cmake -S build -B build-msvc -G "Visual Studio 17 2022" -A x64 `
  -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" `
  -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build-msvc --config Release --parallel --target i2pd i2pd-tui
ctest --test-dir build-msvc -C Release --output-on-failure
```

Torrents are enabled by default. `-DWITH_TORRENTS=OFF -DWITH_UPNP=OFF` remains
valid for a reduced TUI/API smoke build; the full feature build needs
Boost.JSON/URL and MiniUPnPc.

### Installation

`cmake --install` installs `i2pd-tui` as:

- Unix: `${CMAKE_INSTALL_BINDIR}/i2pd-tui`
- Windows: `${CMAKE_INSTALL_BINDIR}/i2pd-tui.exe`

Runtime DLLs are supplied by the selected toolchain/package manager. The build
does not download anything at install or run time. Configuration and data paths
are unchanged (`i2pd.conf`, `tunnels.conf`, `i2pd-tui.sock` on Unix); the
Windows pipe name is derived from the configured local endpoint, not stored in
the configuration file.

## Supported terminals

- Windows Terminal, including the default Windows 11 console host.
- Windows Console Host (`conhost.exe`) with VT processing enabled.
- Visual Studio/VS Code integrated terminals and other terminals that expose a
  conforming VT console.
- Legacy consoles without VT support, or terminals that strip wide characters,
  are not supported for the interactive TUI. The router can still run headless.

The TUI degrades colour, not layout: 16/8-colour and monochrome terminals keep
selection, warning and error states distinguishable. Exact brand colours are
not required or promised.

## Limitations

- The Windows transport is local-only. There is no TCP, HTTP, WebSocket or
  browser UI for the TUI.
- Cross-user access is intentionally not supported by default. A service that
  runs as `LocalSystem` and a desktop TUI running as a standard user need an
  explicit service-account/DACL decision.
- The Windows backend assumes a 256-colour VT console after VT processing is
  enabled; it does not independently distinguish a 16-colour-only VT host.
- The console backend does not implement mouse input or a GUI file picker.
- Terminal resize, reconnect and long-running log streaming still need
  interactive validation on Windows.
- The TUI does not read private keys, NTCP2/SSU2 static keys or ratchet state.
- Status fields read by the TUI are not all atomic yet; ThreadSanitizer
  validation remains pending. See [Security audit](security-audit.md).
- No Windows firewall, AppContainer, restricted-token or Job Object boundary is
  claimed for the TUI. That work belongs to the general IPC/privilege-
  separation roadmap, not to the console client.

## Troubleshooting

| Symptom | Likely cause | Check |
|---|---|---|
| `Access is denied` when starting the TUI | Pipe DACL does not include the interactive account, or the router is a service under another account | Compare the router service account with the TUI user; review `TuiServerWin32.cpp` DACL construction |
| `The system cannot find the file specified` | Router not running, TUI pipe disabled, or data-directory scope differs | Start `i2pd.exe`, confirm `tui.enabled`, pass the same data directory/configuration |
| `All pipe instances are busy` | A previous TUI instance or a stale client holds the only instance | Close the old TUI; verify the router listener is still alive |
| Garbled box drawing or missing colours | Terminal lacks VT/wide-character support, or the font lacks glyphs | Use Windows Terminal or enable VT in the console; select a Unicode-capable font |
| TUI redraws incorrectly after resize | Console resize event not handled by the terminal backend | Resize once more; record the terminal and Windows build for a bug report |
| Router keeps running after the TUI exits | Expected: the TUI is a separate process | Close the console; the router is not signalled |
| Works on Unix, fails on Windows | A Unix-only source was selected by the build | Inspect the `i2pd-tui` source list in `build-output`; Windows must use `TuiClientWin32.cpp`, `TerminalWin32.cpp` and `TuiWin32Ipc.cpp` |

## Verification

Static checks for this integration (not a runtime claim):

```sh
git diff --check
yamllint -f parsable .github/workflows/build-windows.yml
actionlint .github/workflows/build-windows.yml
```

Tests registered with CTest:

- `test-tui-protocol` — portable framing, request/response parsing, version and
  numeric bounds, field escaping, exact-limit acceptance and one-byte-over
  rejection.
- `test-tui-terminal` — portable checks for the shared `Terminal.h` key
  constants, attribute bits and colour-pair packing. It does not initialise
  ncurses or a Win32 console.
- `test-tui-pipe-name` — portable test of the product
  `NormalizeWin32Endpoint()`/`MakeWin32PipeName()` helpers: separator and case
  normalisation, stable hashing, bounded names and refusal to use a caller
  supplied full pipe path verbatim.
- `test-tui-win32-pipe` — Windows-only; validates the SDK named-pipe/DACL
  primitives and a local byte-mode round trip. It does **not** validate console
  rendering, service accounts or kernel ACL enforcement across users.

Runtime checks still required on a Windows host:

1. Build `i2pd` and `i2pd-tui` with MSVC and with MSYS2/Clang64.
2. Start the router with a temporary data directory and `tui.enabled = true`.
3. Confirm the named pipe exists and its DACL grants only the intended
   account/group (`AccessChk` or `Get-Acl` on the pipe, where available).
4. Start `i2pd-tui.exe --datadir <dir>` in Windows Terminal and in conhost;
   check Status, Peers, Connections, Tunnels, NetDb directories, Destinations,
   Services, Logs, Config and Torrent views.
5. Resize the console, use `n` in NetDb, `/` and `L` in Logs, confirm the
   transit toggle with `t`, disconnect/reconnect and quit with `q`; confirm the
   router remains running.
6. Attempt a connection from a second user account and from a remote machine;
   expect rejection.
7. Send malformed and overlong requests; expect an error frame and a closed
   connection, not a router crash.
8. Run the MSVC and MSYS2 jobs from the workflow manually and record the
   result; no workflow was triggered for this change.

Until those checks are recorded, Windows console rendering, pipe ACLs and
service/interactive behavior are **pending validation**.

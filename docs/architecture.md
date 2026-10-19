# Architecture and refactoring report

This document records the state found during the audit and the architectural
changes applied in this refactoring. It is a working report, not a claim that
every component has been validated dynamically.

## Component map

| Component | Location | Responsibility | External consumers |
|---|---|---|---|
| Core library | `libi2pd/` | Identity, crypto, I2NP, transports (NTCP2/SSU2), tunnels, netDb, reseed, streaming, HTTP parsing | `libi2pd.a`, installed headers |
| Client library | `libi2pd_client/` | HTTP/SOCKS proxies, SAM, BOB, I2CP, client tunnels, torrents | `libi2pdclient.a`, installed headers |
| Language data | `i18n/` | English strings and translation helper | `libi2pdlang.a` |
| Daemon | `daemon/` | Lifecycle, configuration, signals, I2PControl, UPnP, local TUI IPC | `i2pd` executable |
| Diagnostics UI | `tui/` | ncursesw (Unix) or Win32/VT (Windows) client, no router state ownership | `i2pd-tui` executable |
| Tests | `tests/` | Parsing, encoding and crypto vectors | CTest |
| Build | `build/CMakeLists.txt`, `Makefile` | CMake build graph, POSIX make front-end | packagers |

Dependency direction after the change:

```
i2pd-tui ──(line protocol over AF_UNIX or a local named pipe)──> i2pd
i2pd ──> libi2pdclient ──> libi2pd ──> libi2pdlang
libi2pd ──> Boost, OpenSSL/LibreSSL, zlib, Threads
```

`i2pd-tui` deliberately does **not** link `libi2pd`. It consumes a bounded
protocol shared through `libi2pd/TuiProtocol.h`. Unix builds use ncursesw;
Windows builds use the Win32 console/VT backend and no Curses library. The
daemon has no dependency on a terminal and never reads stdin. The shared
snapshot/dispatch code is `daemon/TuiServerCommon.cpp`; only the listener and
peer policy differ per platform. See
[TUI on Windows](TUI-WINDOWS.md).

## Findings from the initial audit

1. The administrative HTTP server was a full HTML/CSS/JavaScript application
   inside `daemon/HTTPServer.cpp` (about 1,700 lines), including its own TCP
   acceptor, basic-auth handling, token map and status rendering. It was the
   only consumer of `http.*` options and `contrib/webconsole`.
2. The build had three GNU make platform fragments, GNU-only wildcard and
   conditional syntax, a C++20 fallback, and CMake conditionals for Boost
   1.46 and CMake 3.7. The `-D_GLIBCXX_USE_NANOSLEEP` workaround dates back to
   GCC 4.7/4.8.
3. CMake used `file(GLOB ...)` for all libraries, so adding a source file did
   not reliably reconfigure the build, and installed headers were tied to
   whatever happened to be in the directory.
4. `libi2pd/Base.cpp` initialized its base64 reverse table lazily with a plain
   `static int`. Two threads decoding base64 for the first time could race.
   Invalid base64 characters were mapped to `-1` and then used as unsigned,
   producing silently corrupted keys instead of rejecting them.
5. `NetDb::GetNumRouters/GetNumFloodfills/GetNumLeaseSets` read containers
   without their mutexes. These getters were used by the old web console and
   I2PControl, so administrative reads could race with netDb insertion and
   cleanup.
6. `Transports::GetPeers()` exposed a reference to the peer map without
   locking; I2PControl called `.size()` through it. The TUI needs a safe
   snapshot instead.
7. `Reseed::ProcessSU3Stream` read several header fields without checking the
   stream state. A truncated SU3 file could feed uninitialized values into
   offsets and allocation sizes; signature and content lengths were not
   bounded tightly.
8. The HTTP header parser required whitespace after `:`. RFC 9110 permits
   zero optional whitespace, so `Host:example.i2p` was rejected. Conversely,
   `Content-Length` used `strtoul`, which accepted signs, whitespace and
   trailing garbage, and chunk lengths used `std::stoi` with the same issue.
9. Translation state and several status getters were written for a single
   in-process HTML renderer. They are retained, but the TUI only reads
   immutable/snapshotted values and does not share rendering code with the
   removed console.
10. OpenBSD already had `pledge`/`unveil` support from earlier fork commits.
    The new socket path is added to path canonicalization and unveil rules;
    no promise was broadened.

## Changes applied

- **Web console removed**: `daemon/HTTPServer.cpp`, `HTTPServer.h`,
  `HTTPServerResources.h`, `contrib/webconsole`, the `[http]` configuration
  block, Win32 menu entry, Docker port 7070 and related documentation/tests.
  `HTTPProxy`, HTTP tunnels, reseed and I2PControl were left untouched.
- **Local administration**: new `i2p::tui::Server` in `daemon/TuiServer.cpp`
  and `i2p::tui` protocol in `libi2pd/TuiProtocol.h`. The server is a separate
  thread with a bounded request accumulator, per-connection timeouts, peer UID
  checks, 0600 socket permissions, bounded read-only snapshots, explicitly
  authorized torrent control operations and two operation-specific router
  writes (transit acceptance and daemon log level). There is still no generic
  configuration writer or command queue.
- **ncurses client**: `tui/main.cpp` and `tui/TuiClient.cpp`, target
  `i2pd-tui`, optional at configure time; includes native torrent status,
  details and administrative actions over the local IPC.
- **Native torrent console**: the browser HTML/CSS/JS console was removed.
  `TorrentsControl`/`TorrentsTunnel` publish bounded service-thread snapshots;
  `TuiProtocol`/`TuiServer` expose list/detail/action messages; the optional
  Transmission JSON-RPC listener is loopback-only. See `docs/torrent/`.
- **Thread-safe snapshots**: `Log::GetRecent`, `NetDb` locked counters and
  reseed flag, `Transports::GetPeersSnapshot`, exposed through the IPC layer.
- **Parser hardening**: strict base64 alphabet/padding, HTTP field-name
  validation, optional OWS after the colon, complete numeric parsing for
  `Content-Length` and chunk sizes, chunk extensions, URL port validation.
- **Reseed hardening**: checked reads, bounded signature length, checked
  allocation arithmetic, `std::vector` ownership instead of manual arrays.
- **Build modernization**: explicit source lists, target-scoped usage
  requirements, C++23 only, CTest, install rules, Ninja-based POSIX make
  front-end, removal of `filelist.mk` and platform make fragments.

## Ownership and lifetime notes

- Router-global singletons (`i2p::context`, `transports`, `netdb`, `tunnels`)
  remain, because they model process-wide protocol state and are referenced by
  the public API. The refactoring does not pretend they are injectable
  services; instead, new code snapshots their state under their existing
  locks and avoids exposing references.
- `TuiServer` owns only its socket, path and thread. It does not own router
  objects and cannot destroy them.
- `LogRecord` and `PeerSnapshot` are values. They never carry `RouterInfo`,
  session or key pointers out of the router.
- No cryptographic material is serialized by the TUI protocol. `router.hash`
  is the public identity hash; private keys, ratchet sessions, torrent keys and
  introducer tags are not exposed. Torrent file paths are reduced to display
  names; administrative add-file paths are read only by the daemon.

## Residual architectural risks

- Many status getters still return plain integers that are written by other
  threads (tunnel state, router status). They were already used by the HTTP
  console and GUI; converting all of them to atomics is a larger change and is
  listed as pending validation with ThreadSanitizer.
- `libi2pd` still contains large translation units (`SSU2Session.cpp`,
  `NTCP2.cpp`, `Streaming.cpp`). Splitting them would create review risk
  without a protocol change and was intentionally deferred.
- Public headers expose global state; ABI stability was preserved rather than
  redesigned.

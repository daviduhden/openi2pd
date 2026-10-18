# Test and validation plan

Nothing in this file is claimed as executed unless it explicitly says so. The
author of this refactoring did not run network tests, sanitizer runs, OpenBSD,
Windows, Android or Java interoperability. Local builds/tests were frozen by
user instruction after the Session 7 checkpoint; the commands below are for
CI or a dedicated host.

## What was executed in this environment

### Baseline before modifications

```sh
cmake -S build -B /tmp/openi2pd-test-baseline -G Ninja \
  -DCMAKE_C_COMPILER=gcc-16 -DCMAKE_CXX_COMPILER=g++-16 \
  -DCMAKE_PREFIX_PATH=/home/linuxbrew/.linuxbrew \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON -DWITH_UPNP=OFF
cmake --build /tmp/openi2pd-test-baseline --parallel 8
```

Result: **pass** (101 build steps, including `i2pd` and the original 13 unit
tests). This establishes that the pre-change tree compiled in this container.

### After modifications

Executed configuration:

```sh
cmake -S build -B /tmp/openi2pd-test -G Ninja \
  -DCMAKE_C_COMPILER=gcc-16 -DCMAKE_CXX_COMPILER=g++-16 \
  -DCMAKE_PREFIX_PATH=/home/linuxbrew/.linuxbrew \
  -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON -DWITH_UPNP=OFF -DWITH_TUI=ON
cmake --build /tmp/openi2pd-test --parallel 8
```

Result: **pass** (93 build steps; `i2pd`, `i2pd-tui`, libraries and tests).
This is a Linux/GCC 16 build only. See the final result section below.

### Unit tests

```sh
ctest --test-dir /tmp/openi2pd-test --output-on-failure
```

Executed at the checkpoint: **20/20 passed**, including `tools-smoke` (`ctest --test-dir
/tmp/openi2pd-test --output-on-failure`), including `test-tui-protocol`
and `i2pd-version`. The HTTP tests include new regressions for:

- optional whitespace after `:`;
- invalid field-name tokens;
- malformed/negative/trailing-garbage `Content-Length`;
- chunk extensions, non-hex sizes and trailing garbage;
- URL ports with trailing characters, zero and out-of-range values;
- strict base64 alphabet/padding and short `std::span` destinations.

### TUI protocol tests

`tests/test-protocol-bounds.cpp` covers IdentityEx/LeaseSet truncation, the
LeaseSet expiration off-by-one and I2NP length boundaries; `fuzz-protocol-bounds`
is prepared for the optional libFuzzer target.

`tests/test-tui-protocol.cpp` covers:

- all request kinds and exact argument counts;
- unsupported version, unknown command, oversized line, zero/oversized limits;
- response headers for OK and ERR, all declared error codes, oversized payloads
  and trailing data;
- field escaping/unescaping, empty/oversized field names and daemon-to-client
  framing;
- exact request-line and payload limits accepted, one byte over rejected.

`tests/test-tui-terminal.cpp` checks the shared `tui/Terminal.h` key values,
attribute bits and colour-pair packing. It is header-only and does not
initialise ncurses or a Win32 console.

`tests/test-tui-pipe-name.cpp` is portable because `tui/TuiWin32Ipc.cpp` uses
only the C++ standard library. It validates endpoint normalisation (trimming,
separator folding/collapsing, ASCII case folding), stable FNV-1a hashing,
bounded pipe names and hashing of a caller-supplied full pipe path instead of
opening it verbatim.

`tests/test-tui-win32-pipe.cpp` is compiled only on Windows. It validates the
SDK named-pipe/DACL primitives, an explicit protected DACL and a local
byte-mode round trip. It does **not** validate console rendering, service
accounts or kernel ACL enforcement across users; those remain runtime checks
on a Windows host.

### Static checks prepared

```sh
# Compilation database (add to the configure command)
-DCMAKE_EXPORT_COMPILE_COMMANDS=ON

clang-tidy -p build-output \
  libi2pd/HTTP.cpp libi2pd/Base.cpp libi2pd/Reseed.cpp \
  libi2pd/NetDb.cpp libi2pd/Transports.cpp \
  daemon/TuiServerCommon.cpp daemon/TuiServer.cpp daemon/TuiServerWin32.cpp \
  tui/main.cpp tui/TuiClient.cpp tui/TuiClientWin32.cpp \
  tui/TerminalUnix.cpp tui/TerminalWin32.cpp tui/TuiWin32Ipc.cpp \
  --checks='-*,clang-analyzer-*,bugprone-*,cert-*'
```

A partial `clang-tidy` run on the baseline `Base.cpp` reported the narrowing
and signed-bitwise issues that motivated the rewrite. The full command above
was **not** executed for the changed tree; no full static-analysis claim is
made.

## Sanitizers

```sh
cmake -S build -B build-asan -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON \
  -DWITH_ADDRSANITIZER=ON -DWITH_UPNP=OFF
cmake --build build-asan --parallel
ctest --test-dir build-asan --output-on-failure

cmake -S build -B build-ubsan -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON \
  -DWITH_UNDEFSANITIZER=ON -DWITH_UPNP=OFF
cmake --build build-ubsan --parallel
ctest --test-dir build-ubsan --output-on-failure

cmake -S build -B build-tsan -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON \
  -DWITH_THREADSANITIZER=ON -DWITH_UPNP=OFF
cmake --build build-tsan --parallel
ctest --test-dir build-tsan --output-on-failure
```

TSan is configured to fail if combined with ASan. TSan on the full router is
expected to flag the non-atomic tunnel/router status reads listed in
`security-audit.md`; triage those results before declaring the tree clean.

## Fuzzing

```sh
cmake -S build -B build-fuzz -G Ninja \
  -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
  -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON \
  -DWITH_FUZZERS=ON -DWITH_UPNP=OFF
cmake --build build-fuzz --parallel --target fuzz-tui-protocol fuzz-http-parser
mkdir -p corpus/tui corpus/http
./build-fuzz/tests/fuzz-tui-protocol corpus/tui
./build-fuzz/tests/fuzz-http-parser corpus/http
```

Targets prepared:

- `fuzz-tui-protocol` — request/response parser, field encoder, framing.
- `fuzz-http-parser` — request/response parser, URL decoder, chunk merger.

Not implemented yet (planned, with network-independent harnesses):
NTCP2/SSU2 packet blocks, I2NP, RouterInfo, LeaseSet2, SU3/ZIP, SAM/I2CP line
protocols. These need protocol corpus seeds and should not call the public I2P
network.

## Regression tests for security findings

| Finding | Regression |
|---|---|
| S-01 base64 race/corruption | `test-base-64`: invalid alphabet, misplaced padding, short span, repeated decode |
| S-02 SU3 truncation | **Pending**: add a generated truncated SU3 fixture that asserts no allocation/crash |
| S-03 netDb counters | **Pending**: concurrent `GetNum*`/insert stress under TSan |
| S-04 peer snapshot | **Pending**: concurrent peer cleanup + snapshot under TSan |
| S-05 HTTP lengths | `test-http-req`, `test-http-merge_chunked`, `test-http-url` |
| S-06 header OWS | `test-http-req` |
| S-07 constant-time compare | Reviewed; no timing test |
| S-08 console removal | Build/link check only |
| S-09 TUI input | `test-tui-protocol`; live malformed-client test pending |
| S-11 OpenBSD sandbox | **Pending** on OpenBSD VM |

## TUI runtime checks

Prerequisites: a configured router with `tui.enabled = true`.

```sh
./build-output/i2pd --datadir /tmp/i2pd-test --conf /dev/null &
ls -l /tmp/i2pd-test/i2pd-tui.sock        # expect srw------- owner-only
./build-output/i2pd-tui --socket /tmp/i2pd-test/i2pd-tui.sock
```

Check:

1. Status values update without blocking the UI.
2. Resize the terminal; the layout redraws.
3. Disconnect the TUI (`q`); the router keeps running.
4. Kill the router; the TUI shows a connection error and retries when it
   returns.
5. `kill -TERM` the TUI; it exits cleanly.
6. Run the router with `--daemon` and no TTY; confirm the daemon stays up and
   the socket exists.
7. Attempt to connect from another UID; expect rejection.
8. Send a malformed overlong request with `socat`; expect an error frame and
   connection close, not a router crash.

## Windows TUI checks (pending)

Build with the manual `Windows` workflow or locally with MSVC/MSYS2, then:

```powershell
cmake -S build -B build-msvc -G "Visual Studio 17 2022" -A x64 `
  -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" `
  -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON `
  -DWITH_UPNP=OFF -DWITH_TORRENTS=OFF
cmake --build build-msvc --config Release `
  --target i2pd i2pd-tui test-tui-protocol test-tui-terminal `
    test-tui-pipe-name test-tui-win32-pipe
ctest --test-dir build-msvc -C Release --output-on-failure `
  -R '^(test-(tui-protocol|tui-terminal|tui-pipe-name|tui-win32-pipe))$'
```

Then, on a Windows host with a real console:

1. Start the router with a temporary data directory and `tui.enabled = true`.
2. Inspect the named-pipe DACL (for example with `AccessChk` or the relevant
   PowerShell ACL API) and confirm only the intended account/group has access.
3. Start `i2pd-tui.exe` in Windows Terminal and in conhost; check every view.
4. Resize, cycle log levels, disconnect/reconnect and quit; the router must
   remain running.
5. Try a second user account and a remote client; both must be rejected.
6. Send malformed/overlong requests and confirm an error frame and connection
   close, not a router crash.

No Windows console, DACL, service-account or cross-user test has been executed
for this change.

```sh
# POSIX make front-end (do not pass -j; use CMAKE_BUILD_PARALLEL_LEVEL)
CMAKE_BUILD_PARALLEL_LEVEL=4 bmake
CMAKE_BUILD_PARALLEL_LEVEL=4 bmake test

# Direct CMake with Ninja
cmake -S build -B build-out -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-out --parallel

# Verify no GNU-only make construct in maintained makefiles
grep -nE 'wildcard|shell|patsubst|ifeq|ifneq|\$\(or |\$\(findstring' Makefile tests/Makefile
# expect no output
```

OpenBSD port build (external port): configure with CMake/Ninja, keep
`pledge`/`unveil`, run the daemon as an unprivileged user with a custom
datadir and verify the TUI socket path appears in unveil. This is pending.

## I2P Java interoperability

No Java router is available in this environment. The matrix in
`interop-matrix.md` marks every protocol row as pending. Recommended setup:

1. Run a current Java I2P router on a separate host or network namespace.
2. Exchange RouterInfos; verify NTCP2 and SSU2 sessions establish in both
   directions.
3. Build tunnels in both directions and carry HTTP/streaming traffic.
4. Publish and resolve a LeaseSet2 and an EncryptedLeaseSet.
5. Exercise SSU2 peer test/relay and IPv6.
6. Repeat with OpenSSL without PQ and with PQ enabled, and with LibreSSL.
7. Repeat on OpenBSD with a non-default user/datadir and MTU autodetection.

## Status summary

| Check | Status |
|---|---|
| Baseline compile (before changes) | Passed |
| Modified compile (Linux/GCC 16) | Passed |
| Unit tests and `tools-smoke` | 20/20 passed at Session 7 checkpoint (Linux/GCC 16) |
| ASan/UBSan/TSan | Prepared, not run |
| Fuzzers | Targets prepared, not run |
| OpenBSD build/runtime | Not run |
| Windows build/runtime | Not run |
| Android build | Not run |
| Java interoperability | Not run |
| TUI live session (Unix) | Passed on Linux: socket 0600, STATE/PEERS/TUNNELS/NETDB/LOGS/PING, malformed requests, TUI connect/quit and socket cleanup |
| Windows TUI/pipe/DACL | Not run: compile, console, service-account and cross-user checks pending |

## Final result of this environment

- **Linux/GCC 16 configure and build:** passed.
- **CTest:** 20/20 passed at the Session 7 checkpoint, including torrent bencode/protocol tests, tools smoke and firewall backend test.
- **CMake install:** passed into a temporary prefix (`i2pd`, `i2pd-tui`,
  libraries, headers, configs, certificates, man page).
- **Privilege-separated logging:** live Linux run with `privsep.enabled=true`;
  the core process had no log-file descriptor and the worker had the only write
  descriptor. OpenBSD pledge/unveil was not dynamically tested.
- **POSIX make front-end:** historical `bmake` build/test passed on Linux/GCC 16
  (17/17 at that time); the later CMake checkpoint reached 20/20. Local builds
  are now frozen and delegated to CI.
- **TUI runtime:** a router was started with a temporary datadir; the socket
  appeared as `srw-------`; `STATE`, `NETDB`, `PEERS`, `TUNNELS`, `LOGS` and
  `PING` returned valid frames; invalid commands/limits returned error frames;
  an `i2pd-tui` session connected, rendered the Status view and exited with
  `q`; SIGTERM removed the socket and pidfile.
- **Integrated tools:** `keygen`, `keyinfo`, `offlinekeys`, `i2pbase64`,
  `regaddr`, `verifyhost`, `x25519` passed a manual round trip and the
  `tools-smoke` CTest.
- **Documentation links:** converted/imported Markdown checked with a recursive
  relative-link script; all internal links resolve. MkDocs build not executed.
- **Not validated:** ASan/UBSan/TSan, fuzzers, OpenBSD, Windows, Android APK/JNI build, live Java interoperability, multi-UID rejection, and the full multiprocess privilege-separation invariants. Only the isolated log worker was exercised.
- **Java reference:** stable tag `i2p-2.13.0` cloned read-only; only static
  inspection performed. No Java router was executed.

A successful Linux build does not upgrade the pending rows to validated.

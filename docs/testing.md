# Test and validation plan

Nothing in this file is claimed as executed unless it explicitly says so. The
author of this refactoring did not run network tests, sanitizer runs, OpenBSD,
Windows, Android or Java interoperability.

## What was executed in this environment

### Baseline before modifications

```sh
cmake -S build -B /tmp/opencode/i2pd-baseline -G Ninja \
  -DCMAKE_C_COMPILER=gcc-16 -DCMAKE_CXX_COMPILER=g++-16 \
  -DCMAKE_PREFIX_PATH=/home/linuxbrew/.linuxbrew \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON -DWITH_UPNP=OFF
cmake --build /tmp/opencode/i2pd-baseline --parallel 8
```

Result: **pass** (101 build steps, including `i2pd` and the original 13 unit
tests). This establishes that the pre-change tree compiled in this container.

### After modifications

Executed configuration:

```sh
cmake -S build -B /tmp/opencode/i2pd-new -G Ninja \
  -DCMAKE_C_COMPILER=gcc-16 -DCMAKE_CXX_COMPILER=g++-16 \
  -DCMAKE_PREFIX_PATH=/home/linuxbrew/.linuxbrew \
  -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON -DWITH_UPNP=OFF -DWITH_TUI=ON
cmake --build /tmp/opencode/i2pd-new --parallel 8
```

Result: **pass** (93 build steps; `i2pd`, `i2pd-tui`, libraries and tests).
This is a Linux/GCC 16 build only. See the final result section below.

### Unit tests

```sh
ctest --test-dir /tmp/opencode/i2pd-new --output-on-failure
```

Executed: **17/17 passed**, including `tools-smoke` (`ctest --test-dir
/tmp/opencode/i2pd-new --output-on-failure`), including `test-tui-protocol`
and `i2pd-version`. The HTTP tests include new regressions for:

- optional whitespace after `:`;
- invalid field-name tokens;
- malformed/negative/trailing-garbage `Content-Length`;
- chunk extensions, non-hex sizes and trailing garbage;
- URL ports with trailing characters, zero and out-of-range values;
- strict base64 alphabet/padding and short `std::span` destinations.

### TUI protocol tests

`tests/test-tui-protocol.cpp` covers:

- all request kinds and exact argument counts;
- unsupported version, unknown command, oversized line, zero/oversized limits;
- response headers for OK and ERR, oversized payloads and trailing data;
- field escaping/unescaping and daemon-to-client framing.

### Static checks prepared

```sh
# Compilation database (add to the configure command)
-DCMAKE_EXPORT_COMPILE_COMMANDS=ON

clang-tidy -p build-output \
  libi2pd/HTTP.cpp libi2pd/Base.cpp libi2pd/Reseed.cpp \
  libi2pd/NetDb.cpp libi2pd/Transports.cpp daemon/TuiServer.cpp \
  tui/main.cpp tui/TuiClient.cpp \
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

## Build-system checks

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
| Unit tests and `tools-smoke` | 17/17 passed (Linux/GCC 16) |
| ASan/UBSan/TSan | Prepared, not run |
| Fuzzers | Targets prepared, not run |
| OpenBSD build/runtime | Not run |
| Windows build/runtime | Not run |
| Android build | Not run |
| Java interoperability | Not run |
| TUI live session | Passed on Linux: socket 0600, STATE/PEERS/TUNNELS/NETDB/LOGS/PING, malformed requests, TUI connect/quit and socket cleanup |

## Final result of this environment

- **Linux/GCC 16 configure and build:** passed.
- **CTest:** 17/17 passed, including the integrated tools smoke test and firewall backend test.
- **CMake install:** passed into a temporary prefix (`i2pd`, `i2pd-tui`,
  libraries, headers, configs, certificates, man page).
- **POSIX make front-end:** full `bmake` build and `bmake test` passed on
  Linux/GCC 16 (17/17 tests, including tools and firewall).
- **TUI runtime:** a router was started with a temporary datadir; the socket
  appeared as `srw-------`; `STATE`, `NETDB`, `PEERS`, `TUNNELS`, `LOGS` and
  `PING` returned valid frames; invalid commands/limits returned error frames;
  an `i2pd-tui` session connected, rendered the Status view and exited with
  `q`; SIGTERM removed the socket and pidfile.
- **Integrated tools:** `keygen`, `keyinfo`, `offlinekeys`, `i2pbase64`,
  `regaddr`, `verifyhost`, `x25519` passed a manual round trip and the
  `tools-smoke` CTest.
- **Documentation links:** converted/imported Markdown checked with `rg` for
  stale web-console paths; MkDocs build not executed.
- **Not validated:** ASan/UBSan/TSan, fuzzers, OpenBSD, Windows, Android APK/JNI build, live Java interoperability, multi-UID rejection.
- **Java reference:** stable tag `i2p-2.13.0` cloned read-only; only static
  inspection performed. No Java router was executed.

A successful Linux build does not upgrade the pending rows to validated.

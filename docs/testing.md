<!--
See the LICENSE file at the top of the project tree for copyright
and license details.

openi2pd: testing.

Documents project documentation.

-->

# Test and validation plan

Permanent policy: agents do not configure, build or execute tests locally.
All dynamic validation is performed by manually dispatched GitHub Actions
workflows. Historical local results below are retained for traceability and do
not validate current uncommitted changes.

Nothing in this file is claimed as executed unless it explicitly says so. The
2026-10-18 interoperability pass ran a local Linux/GCC 16 configure, build and
the registered CTest suite; it did not run network/Java peer tests, sanitizer
runs, fuzzing, OpenBSD, Windows or Android. Commands below remain for CI or a
dedicated host.

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

Executed configuration for the 2026-10-18 interoperability pass:

```sh
cmake -S build -B /tmp/opencode/openi2pd-build -G Ninja \
  -DCMAKE_C_COMPILER=gcc-16 -DCMAKE_CXX_COMPILER=g++-16 \
  -DCMAKE_PREFIX_PATH=/home/linuxbrew/.linuxbrew \
  -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON -DWITH_UPNP=OFF -DI2PD_BUILD_TUI=ON
cmake --build /tmp/opencode/openi2pd-build --parallel 8
```

Result: **pass** with GCC 16.2.0 and OpenSSL 4.0.3 (196 build targets:
`i2pd`, `i2pd-tui`, libraries, tools and tests). This is a Linux/GCC build
only; it is not a Java-interoperability claim.

### Unit tests

```sh
ctest --test-dir /tmp/opencode/openi2pd-build --output-on-failure
```

The compile-free build-policy check can be selected independently in CI:

```sh
ctest --test-dir <build-dir> -R build-system-static --output-on-failure
```

It also checks that every `tests/test-*.cpp` and `tests/fuzz/*.cpp` source is
named in `tests/CMakeLists.txt`, so a new subsystem test or fuzzer cannot be
added without CMake wiring.

Executed on 2026-10-18: **48/48 passed**. This includes the new
interoperability regressions listed below, `tools-smoke`, `test-tui-protocol`
and `i2pd-version`. The HTTP tests include regressions for:

- optional whitespace after `:`;
- invalid field-name tokens;
- malformed/negative/trailing-garbage `Content-Length`;
- chunk extensions, non-hex sizes and trailing garbage;
- URL ports with trailing characters, zero and out-of-range values;
- strict base64 alphabet/padding and short `std::span` destinations.

### CMake/CTest coverage and labels

`tests/CMakeLists.txt` builds every checked-in `test-*.cpp` that applies to the
current platform and registers it with CTest. The suite labels are:

- `unit`: all C++ unit/regression targets;
- `i2p`: core HTTP, crypto, NetDb, LeaseSet, RouterInfo, transport, tunnel,
  client and log regression groups;
- `security`: protocol bounds, crypto, LeaseSet/netDb, NTCP2/SSU2 and RouterInfo;
- `memory`: bounds/audit/hardening/race/lifecycle regressions prioritized for
  ASan/UBSan/TSan lanes;
- `ipc`: privilege-separation, TUI transport and pipe tests;
- `tui`: terminal/protocol tests;
- `torrent`: bencode, torrent security and control-plane tests;
- `tools`: tool inventory, smoke and firewall generator tests;
- `perl`: Perl syntax, portability/Python-free inventory, `i2pdctl`, branding,
  documentation-link and install-layout checks; optional `perl-critic` when
  `-DI2PD_PERL_CRITIC=ON` and the analyzer is installed;
- `interop`: static checked-in Java interoperability vectors;
- `platform`: Linux/OpenBSD sandbox, Windows pipe and platform transport tests;
- `android`: compile-free Java 25 configuration and English-only resource
  policy checks; Gradle/APK/Lint/device execution stays in the Android job;
- `fuzz`: optional one-input libFuzzer smoke entries when
  `-DWITH_FUZZERS=ON`;
- `ci`, `static`, `smoke`: build-policy, version and static checks.

Linux-specific IPC/sandbox tests are registered only on Linux; OpenBSD policy
and imsg tests only on OpenBSD. The source files retain portable stubs, but
they are not built or reported as platform tests on the wrong host.

The static `interop-static` target validates all checked-in fixtures without
Java. `-DI2PD_INTEROP_REFERENCE=/path/to/i2p-java-2.13.0` additionally
registers `interop-reference` and `interop-reference-vectors` for a read-only
pinned checkout. The live SAM shell probe remains manual because it needs a
running router and `nc`.

The compile-free `agent-skills-static` check reads `.agents/skills/*/SKILL.md`
and verifies frontmatter names, required sections, `AGENTS.md` references and
`references/*.md` links. It does not execute a skill, build a target or call an
external validator; its CI execution remains pending the next manual run.

### Manual CI execution matrix

`.github/workflows/build.yml` now runs, on `workflow_dispatch` only:

- Linux Clang and GCC: complete build, full CTest, per-label inventory
  (`i2p`, `security`, `memory`, `ipc`, `tui`, `torrent`, `tools`, `perl`,
  `interop`, `platform`, `android`), version, Perl, branding and static
  interoperability checks. GCC, LLVM, CMake, Ninja, Boost, OpenSSL, zlib,
  MiniUPnPc and ncurses come from Homebrew so the manual runs compile against
  the current releases.
- Android: Java 25 guard, English-only resource/locale static check, Gradle unit tests, Lint, native arm64 JNI link and
  an arm64 debug APK build.
- Interop: pinned Java tag/commit/read-only check plus fixture provenance.
- Sanitizers: separate ASan+UBSan and TSan builds with the full CTest suite,
  using the current Homebrew Clang, Boost, OpenSSL, zlib and MiniUPnPc.

`.github/workflows/fuzz.yml` uses the current Homebrew Clang, Boost, OpenSSL
and zlib, builds all eight libFuzzer targets and runs a bounded smoke pass for
each. `.github/workflows/build-windows.yml` runs the
complete MSYS2/Clang64 target graph and CTest suite; the MSVC job remains
TUI/API-focused. `.github/workflows/build-openbsd.yml` runs the complete
OpenBSD CTest suite.

None of these workflow results may be attributed to uncommitted local work.
The current tree is still pending compilation and dynamic execution.

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
- the read-only, typed `CONFIG` allowlist, argument rejection and absence of a
  `CONFIG-SET` writer;
- bounded `CONNECTIONS` and `NETDB-ROUTERS`/`NETDB-LEASESETS` requests;
- strict `ADMIN` validation for transit, loglevel, reseed, shutdown, tunnel
  reload, bandwidth and share, including rejected actions, values, ranges and
  trailing arguments;
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

`tests/test-privsep-windows-ipc.cpp` is compiled only on Windows. It validates
the worker message-mode pipe, protected user/SYSTEM DACL, peer PID/SID/session,
unknown-frame rejection, close/recovery, restricted-token creation and Job
Object creation. It does not validate cross-user ACL rejection, a service
account, AppContainer/LPAC or a live storage/network worker.

`tests/test-su3-bounds.cpp` constructs empty, truncated and oversized SU3
headers and verifies that `Reseeder::ProcessSU3File()` rejects them without
allocating from unvalidated lengths. It does not replace a real SU3 signature
or archive test.

`tests/test-streaming-buffer.cpp` checks the owning `SendBuffer` copy
semantics, the `std::span<const uint8_t>` constructor and the writable staging
constructor. It does not exercise a live socket or an asynchronous completion.

`tests/test-log-lifecycle.cpp` exercises repeated `Log::Start`/`Stop`, sink
replacement and concurrent signal-style `Reopen` calls without starting the
router. `tests/test-tui-ipc-regression.cpp` is a Unix-only real-socket test for
partial/pipelined TUI requests, malformed input, bounded connection/netDb
directory dispatch, atomic daemon log-level control, `QUIT`, socket permissions
and cleanup. `tests/test-torrents-security.cpp` covers piece block bounds, metadata
reference lifetime, DHT peer/bucket limits and torrent file range I/O.
`tests/test-ssu2-routerinfo-bounds.cpp` signs valid and malformed RouterInfo
bodies to isolate parser checks from signature rejection. The crypto audit
regressions are `test-crypto-audit-bounds`, `test-blinding-b33-strict` and
`test-reseed-zip-bounds`; they do not replace a full protocol handshake or a
real signed reseed archive.

`tests/test-routercontext-update-race.cpp` overlaps family/property mutation,
snapshot copying and transport-flag reads without initialising a router; TSan
is the meaningful validator. `tests/test-kad-dht.cpp` exercises deterministic
Kademlia insertion/removal/promotion/clear/filter-owner paths through
`std::unique_ptr` trees. `tests/test-addressbook.cpp` parses in-memory
hosts/identity forms without starting storage, resolvers or a network.
`tests/test-privsep-linux-ipc.cpp` exercises raw `SOCK_SEQPACKET` framing,
credentials, descriptor ownership, ancillary-data rejection and bounded send
pressure; `tests/test-privsep-linux-sandbox.cpp` checks storage socketpair and
capability denial, log AF_INET-deny/AF_UNIX-allow and supervisor socketpair
behavior; `tests/test-privsep-log.cpp` adds real fork/worker descriptor
policies. On OpenBSD, `test-privsep-openbsd-policy` forks children and requires
a pledge violation for storage's AF_INET socket and network's persistent file
creation; `test-privsep-openbsd-imsg` covers the libutil adapter, peer UID and
one descriptor. Cross-UID rejection still needs a second account/root, and the
queue-pressure case is not a TSan proof.
`tests/test-ssu2-preauth.cpp` exercises the SSU2 state-by-block whitelist and
pre-auth PeerTest/Address/I2NP guards without a socket. The lifecycle set
`test-i2cp-session-lifecycle`, `test-sam-stream-lifecycle`,
`test-clientcontext-registry`, `test-tunnelpool-zero-hop`,
`test-tunnelpool-detach-race`, `test-tunnels-restart`,
`test-timestamp-offset`, `test-ntp-timesync-lifecycle`,
`test-routercontext-service-lifecycle` and `test-routercontext-static-keys`
adds deterministic lifecycle and concurrency probes, while
`test-torrents-concurrency` is a TSan-oriented piece publication target.
`tools-regressions` is a CMake-only CTest script with a 600 s timeout covering
the native tool CLI surface; it must not be confused with the historical
`tools-smoke` run. None of these new tests has executed in the current tree.

The Java 2.13.0 static-interoperability and 2026-10-20 parity regressions are:

- `test-ntcp2-session-confirmed-payload.cpp` — ordered RI/Options/Padding,
  truncation, overrun, unknown and out-of-order NTCP2 handshake blocks.
- `test-datagram-options.cpp` — zero-length/normal Datagram2/3 option
  mappings and truncation.
- `test-sam-datagram-framing.cpp` — SAM 3.x UDP first-line parsing,
  version tokens, sessions and legacy form.
- `test-sam-control-framing.cpp` — persistent bounded SAM framing, pipelined
  commands, split SEND payloads, one-word controls and quoted/normalized
  parameters.
- `test-i2cp-routing-session-key.cpp` — identity-hash routing keys across
  signature types and the former fixed-slice collision.
- `test-addressbook-case.cpp` — ASCII host-name folding, b32/b33 spelling and
  exact base64 destination handling.
- `test-torrents-verify-parity.cpp` — resume/final-name re-verification,
  tracker peer forms/limits, tracker id/min interval, percent-encoded peer id
  and connection-limit relationships.
- `test-signature-identity-hardening.cpp` — Java-compatible raw RSA
  verification and RSA identity certificate layout.
- `test-su3-bounds.cpp` — malformed SU3 bounds plus the fail-closed
  `reseed.verify` default.
- `test-ssu2-preauth.cpp` — SSU2 PathChallenge retry/deadline schedule in
  addition to the existing block whitelist.
- `test-crypto-x25519-noise-elligator-mlkem-audit.cpp` — cached runtime
  ML-KEM provider gating.
- `test-tui-protocol.cpp` / `test-tui-ipc-regression.cpp` — typed TUI admin
  parsing/manual queue and real socket dispatch.
- `test-leaseset-netdb-2.13-freshness.cpp` — LS1/LS2 future lease windows,
  unpublished/blinded flags, EC_P256 wire keys, DatabaseStore type mask.
- `test-ssu2-address-negotiation.cpp` — SSU2 `v` lists, PQ/MTU selection and
  sub-1280 rejection.
- `test-ssu2-fragment-ordering.cpp` — last-fragment ordering/duplicate
  rejection.
- `test-crypto-wire-validation.cpp` — key-certificate lengths, strict b33
  flags, Ed25519ph and GOST signature rejection.
- `test-ntcp2-handshake-bounds.cpp` — per-version hybrid padding, replay-cache
  separation and SessionConfirmed payload bounds.
- `test-ssu2-session-fragment-bounds.cpp` — SessionConfirmed fragment count,
  RTO constants, reassembly ordering and overflow rejection.
- `test-ssu2-control-plane.cpp` — dual-family address split, PQ/MTU
  publication and PeerTest endpoint validation.
- `test-leaseset2-java213-regression.cpp` — LS2 offline/freshness/key/property,
  Meta and encrypted-layer valid/malformed cases.
- `test-routerinfo-java213-parsing.cpp` — addresses/options, NTCP2/SSU2
  publication, introducers, caps/version, hidden identity and truncation.
- `test-routerinfo-update-transaction.cpp` — failed updates preserve the old
  entry; local dual-stack NTCP2, expiration and file verification.
- `test-netdb-lookup-wire-bounds.cpp` — DLM/DSRM layouts, exclusion cap and
  strict parser boundaries.
- `test-netdb-dsrm-correlation.cpp` — direct exploratory flag, queried-peer
  binding, duplicate reply suppression and request bounds.
- `test-netdb-store-lookup-hardening.cpp` — RouterInfo/LS1/LS2 replacement,
  floodfill membership, 0x02/0x10/0x12 reply-key rules and persistence.

`test-elligator.cpp` now asserts the Java/proposal boundary rejection for
`r >= (p-1)/2` instead of accepting the crafted boundary representative.
These are unit/static fixtures, not live Java peer tests.

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

The commands below are the CI contract; they are not local execution results.
The manual workflow runs ASan+UBSan together and TSan separately, then executes
the full CTest suite. Sanitizer findings are not suppressed.

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

The manual `Fuzzing` workflow builds every target below and runs a bounded
`-max_total_time=30` smoke pass. With `-DWITH_FUZZERS=ON`, one-input
`ctest -L fuzz` entries are also registered for inventory and quick local
checks; they do not replace the corpus workflow. The commands remain the
reproducible local or CI recipe; no fuzz run is claimed for the current tree.

```sh
cmake -S build -B build-fuzz -G Ninja \
  -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
  -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON \
  -DWITH_FUZZERS=ON -DWITH_UPNP=OFF
cmake --build build-fuzz --parallel --target fuzz-tui-protocol fuzz-http-parser \
  fuzz-protocol-bounds fuzz-log-record fuzz-privsep-protocol fuzz-torrents-parsers fuzz-base-blinding \
  fuzz-signature-der
mkdir -p corpus/tui corpus/http corpus/protocol corpus/log corpus/privsep corpus/torrents corpus/base corpus/signature
ctest --test-dir build-fuzz -L fuzz --output-on-failure
./build-fuzz/tests/fuzz-tui-protocol corpus/tui
./build-fuzz/tests/fuzz-http-parser corpus/http
./build-fuzz/tests/fuzz-protocol-bounds corpus/protocol
./build-fuzz/tests/fuzz-log-record corpus/log
./build-fuzz/tests/fuzz-privsep-protocol corpus/privsep
./build-fuzz/tests/fuzz-torrents-parsers corpus/torrents
./build-fuzz/tests/fuzz-base-blinding corpus/base
./build-fuzz/tests/fuzz-signature-der corpus/signature
```

Targets prepared:

- `fuzz-tui-protocol` — request/response parser, field encoder, framing.
- `fuzz-http-parser` — request/response parser, URL decoder, chunk merger.
- `fuzz-protocol-bounds` — IdentityEx, LeaseSet, I2NP and RouterInfo parsers.
- `fuzz-log-record` — bounded/sanitized log record snapshot.
- `fuzz-privsep-protocol` — worker frame and log/config payload decoders.
- `fuzz-torrents-parsers` — bencode, metainfo, peer block, DHT node parsers.
- `fuzz-base-blinding` — Base32/Base64 decoders and strict b33 parsing.
- `fuzz-signature-der` — ECDSA P-521 DER sizing and verification with a fixed identity.

Still planned: dedicated NTCP2/SSU2 packet-block and I2NP fuzz harnesses; these
need protocol corpus seeds and must not call the public I2P network.

## Regression tests for security findings

| Finding | Regression |
|---|---|
| S-01 base64 race/corruption | `test-base-64`: invalid alphabet, misplaced padding, short span, repeated decode |
| S-02 SU3 truncation | `test-su3-bounds`: empty/truncated/oversized headers without unvalidated allocation |
| S-03 netDb counters | **Pending**: concurrent `GetNum*`/insert stress under TSan |
| S-04 peer snapshot | **Pending**: concurrent peer cleanup + snapshot under TSan |
| S-05 HTTP lengths | `test-http-req`, `test-http-merge_chunked`, `test-http-url` |
| S-06 header OWS | `test-http-req` |
| S-07 constant-time compare | Reviewed; no timing test |
| S-08 console removal | Build/link check only |
| S-09 TUI input | `test-tui-protocol`, `test-tui-ipc-regression`; live Windows/client stress pending |
| S-11 OpenBSD sandbox | `test-privsep-openbsd-policy` (storage/no-AF_INET, network/no-persistent-write, pre-opened descriptors) and `test-privsep-openbsd-imsg`; **pending** OpenBSD VM execution |
| S-12 SSU2/RouterInfo bounds | `test-ssu2-routerinfo-bounds`, `fuzz-protocol-bounds` |
| S-13 crypto/b33/reseed bounds | `test-crypto-audit-bounds`, `test-blinding-b33-strict`, `test-reseed-zip-bounds`, `fuzz-base-blinding` |
| S-14 torrent state/parsers | `test-torrents-security`, `fuzz-torrents-parsers` |
| S-15 log/privsep/TUI IPC lifecycle | `test-log-lifecycle`, `test-tui-ipc-regression`, `fuzz-log-record`, `fuzz-privsep-protocol` |
| S-16 SSU2 pre-auth/payload policy | `test-ssu2-preauth`, `fuzz-protocol-bounds`; dynamic forged packet pending |
| S-17 crypto DER/AES/X25519 | `test-signature-identity-hardening`, `test-crypto-audit-bounds`, `fuzz-signature-der`; CI pending |
| S-18 client/tunnel lifecycle | `test-i2cp-session-lifecycle`, `test-sam-stream-lifecycle`, `test-clientcontext-registry`, `test-tunnelpool-*`, `test-tunnels-restart`; TSan pending |
| S-19 NTP/RouterContext/NetDb/Log lifecycle | `test-timestamp-offset`, `test-ntp-timesync-lifecycle`, `test-routercontext-*`, `test-log-lifecycle`; CI pending |

## Firewall rule generator tests

`test-routerinfo-firewall` is a deterministic C++ regression test for the
common endpoint model and every renderer: historical `-f` fragments, full
iptables/ip6tables commands, nftables (including `inet`/`ip`/`ip6` family
filtering), Windows `New-NetFirewallRule` and OpenBSD PF. It also checks that
every rule is outbound, rejects loopback/link-local/multicast/broadcast,
IPv4-mapped and IPv4-compatible addresses, validates ports and malformed
scopes/brackets, rejects unsafe nftables table/chain identifiers (including
standard inbound/forward hooks), revalidates the endpoint at the rendering
boundary, rejects unsafe Windows display-name tags and checks the lower/upper
port bounds. `-6` is required for IPv6 in every CLI mode; `-y` separately
selects Yggdrasil when present.

`tools-smoke` checks the CLI contract: help lists all backends, unknown or
conflicting backends fail, nftables options require `--firewall=nftables`, bad
families/tables fail, and a malformed RouterInfo file fails before hash or
address access.

External syntax checks are prepared but are not run by the static suite:
`iptables-restore --test`/`ip6tables-restore --test` for wrapped iptables
output, `nft -c -f` for an nftables table/chain wrapper, `pfctl -nf` for PF and
`-WhatIf` on a Windows host for PowerShell. The tool never applies rules and
never edits a system firewall configuration.

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
  -DWITH_UPNP=OFF
cmake --build build-msvc --config Release `
  --target i2pd i2pd-tui test-tui-protocol test-tui-terminal `
    test-tui-pipe-name test-tui-win32-pipe test-privsep-windows-ipc
ctest --test-dir build-msvc -C Release --output-on-failure `
  -R '^(test-(tui-protocol|tui-terminal|tui-pipe-name|tui-win32-pipe|privsep-windows-ipc))$'
```

Torrents are enabled by default. Pass `-DWITH_TORRENTS=OFF` to configure a
reduced TUI/API build without the Boost.JSON/URL dependency.

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

No Java router is available in this environment. The static matrix in
`docs/INTEROPERABILITY.md` records the 2026-10-18 recheck of the latest stable
`i2p-2.13.0`; it does not claim verified interoperability. Recommended setup:

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
| Modified compile (Linux/GCC 16) | Passed on 2026-10-18 (196 targets) |
| Unit tests and `tools-smoke` | 48/48 passed on 2026-10-18 (Linux/GCC 16) |
| ASan/UBSan/TSan | Separate manual CI jobs prepared; not run for the current tree |
| Build-system static check | `build-system-static` prepared, including dependency-floor, removed-compatibility and test/fuzzer inventory assertions; static check passes, no build execution |
| Agent Skills static check | `agent-skills-static` prepared for frontmatter, required sections and reference-link validation; not executed for the current tree |
| CTest registration and labels | Static inventory prepared; Linux-only and OpenBSD-only tests are registered on their host, with i2p/security/memory/ipc/tui/torrent/tools/perl/interop/platform/android groups; CTest execution pending CI |
| Fuzzers | Eight targets and manual smoke workflow prepared; not run |
| OpenBSD build/runtime | Baseline `39160543186` failed at `getexecname()`; local `<stdlib.h>` fix and full CTest pending. New dispatch `39189495752` reproduced the same old-revision failure |
| Windows build/runtime | Baseline `39160540886` MSYS2 failed at `tools-smoke`/named-pipe DACL; local full-build/SID fixes pending. New dispatch `39189492769` reproduced MSYS2 `routerinfo`/pipe DACL and the MSVC generator failure |
| Android build | Baseline `39160538632` failed at `UnixDaemon`/`DaemonAndroid`; local JNI-only Android graph and Gradle steps pending. New dispatch `39189489953` reproduced the old failure |
| Manual dispatch audit 2026-10-19 | Published `main@800c8627` only: Linux/Android `39189489953` failed (`U64`, Android `UnixDaemon`), OpenBSD `39189495752` failed (`getexecname`), Windows `39189492769` MSYS2 failed. Not attributed to local changes |
| Java interoperability | Pinned `i2p-2.13.0` static reference harness reviewed 26 cases (24 static/vector OK, 2 documented known gaps); `interop-static` CTest and manual pinned-reference job prepared. No live Java peer or differential run |
| TUI live session (Unix) | Passed on Linux: socket 0600, STATE/PEERS/TUNNELS/NETDB/LOGS/PING, malformed requests, TUI connect/quit and socket cleanup |
| Windows TUI/pipe/DACL | Not run: compile, console, service-account and cross-user checks pending |

## Final result of this environment

- **Linux/GCC 16 configure and build:** passed on 2026-10-18 (196 targets).
- **CTest:** 48/48 passed on 2026-10-18, including the new protocol, streaming, SSU2, LeaseSet/netDb, crypto and SAM regressions.
- **CMake install:** passed into a temporary prefix (`i2pd`, `i2pd-tui`,
  libraries, headers, configs, certificates, man page).
- **Android English-only policy:** `android/tools/check-english-only.pl` is a
  compile-free CI guard for English-only resources, `locales_config.xml`,
  Fastlane listings and locale overrides. `EnglishOnlyResourceTest` uses
  `createConfigurationContext` (not `Locale.setDefault`) and is prepared for
  the manual device job.
- **Privilege-separated logging:** live Linux run with `privsep.enabled=true`;
  the core process had no log-file descriptor and the worker had the only write
  descriptor. OpenBSD pledge/unveil was not dynamically tested.
- **POSIX make front-end:** historical `bmake` build/test passed on Linux/GCC 16
  (17/17 at that time); the later CMake checkpoint reached 20/20 and the
  2026-10-18 interop/transport/netDb passes reached 48/48.
- **TUI runtime:** a router was started with a temporary datadir; the socket
  appeared as `srw-------`; `STATE`, `NETDB`, `PEERS`, `TUNNELS`, `LOGS` and
  `PING` returned valid frames; invalid commands/limits returned error frames;
  an `i2pd-tui` session connected, rendered the Status view and exited with
  `q`; SIGTERM removed the socket and pidfile.
- **Integrated tools:** `keygen`, `keyinfo`, `offlinekeys`, `i2pbase64`,
  `regaddr`, `verifyhost`, `x25519` passed a manual round trip and the
  `tools-smoke` CTest.
- **Documentation links and anchors:** `tools/scripts/check-doc-links.pl docs`
  checks relative Markdown/image targets and local heading/anchor fragments
  without network access; the current tree resolves. MkDocs build/publication
  is optional and was not executed.
- **Upstream documentation coverage:** `contrib/upstream/sync.sh --docs`
  compares the pinned i2pd_docs_en tree, maps the two renamed torrent pages,
  rejects unclassified upstream files and HTML sources, and reports
  `Coverage OK`. The helper is read-only; it was executed against the pinned
  commit and reported coverage OK.
- **Not validated:** the current uncommitted tree has no CTest/sanitizer/fuzz/OpenBSD/Windows/Android run. Historical Linux results above predate the current static-only policy. Live Java interoperability, Android instrumentation, multi-UID rejection and the full multiprocess privilege-separation invariants remain pending.
- **Java reference:** stable tag `i2p-2.13.0` cloned read-only; the static
  reference/fixture harness reviewed 26 cases (24 static OK, 2 documented known gaps).
  No Java router was executed and no interoperability is claimed.

A successful Linux build does not upgrade the pending rows to validated.

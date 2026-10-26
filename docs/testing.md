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
named in `tests/CMakeLists.txt`, and that every fuzz source/CMake target is
also wired into `.github/workflows/fuzz.yml`, so a new subsystem test or fuzzer
cannot be added without registration.

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
- `docs`: MkDocs navigation targets, theme assets, unlisted Markdown/HTML
  checks, relative link/anchor validation and internationalization catalog policy;
- `perl`: Perl syntax, expanded portability/Python-free inventory (versioned
  interpreters, consumers, shebang/BOM, `require`, process/temp/chmod and
  inventory drift), `i2pdctl`, branding, documentation-link and install-layout
  checks; optional `perl-critic` when `-DI2PD_PERL_CRITIC=ON` and the analyzer
  is installed;
- `interop`: static checked-in Java interoperability vectors;
- `platform`: Linux/OpenBSD sandbox, Windows pipe and platform transport tests;
- `android`: compile-free Java 25 configuration and four-locale resource/catalog
  policy checks; Gradle/APK/Lint/device execution stays in the Android job;
- `fuzz`: optional one-input libFuzzer smoke entries when
  `-DWITH_FUZZERS=ON`;
- `packaging`/`install`: compile-free POSIX make, install-component,
  exported-package, Debian/static/OpenBSD port template and packaging-file
  checks;
- `docs`: documentation links, navigation, branding and Agent Skills
  contracts, in addition to the documentation checks below;
- `services`: init-system definition contract checks for foreground
  supervision, stop budgets, s6 `down-signal`/`timeout-kill`, OpenRC
  `stopsig`, SysV pidfile ownership, systemd sysusers/tmpfiles, OpenBSD
  rc.d/login/config and the unselected Upstart/dinit samples;
- `ci`, `static`, `workflow`, `binary`, `smoke`: build-policy, manual-only
  workflow contract, version and static checks;
- `cli`, `integration`: native tool exit-code/stdout contracts and the
  scripted tool/firewall suites;
- `reference`: optional read-only Java reference and fixture-provenance checks
  (registered only when the external checkouts are configured);
- `critic`: opt-in Perl::Critic policy (requires `-DI2PD_PERL_CRITIC=ON` and
  the distribution analyzer).

`unit` is the broad regression label and still includes the privileged IPC and
storage suites; use the specific `ipc`, `security`, `platform` or `torrent`
labels for narrower selection. Forked sandbox/storage tests are marked
`RUN_SERIAL`, and the remaining fixed-`/tmp` tests share the
`openi2pd-fixed-tmp` resource lock, so `ctest -j` cannot overlap them.

Linux-specific IPC/sandbox tests are registered only on Linux; OpenBSD policy
and imsg tests only on OpenBSD. The source files retain portable stubs, but
they are not built or reported as platform tests on the wrong host.

The static `interop-static` target validates all checked-in fixtures without
Java. `-DI2PD_INTEROP_REFERENCE=/path/to/i2p-java-2.14.0` additionally
registers `interop-reference` for the current comparison target. Separately,
`-DI2PD_INTEROP_FIXTURE_REFERENCE=/path/to/i2p-java-2.13.0` registers
`interop-reference-vectors` for historical fixture provenance. Neither check
executes Java or proves compatibility. The live SAM shell probe remains manual because it needs a
running router and `nc`.

The compile-free `agent-skills-static` check reads `.agents/skills/README.md`,
every `SKILL.md` and `references/*.md`. It verifies the exact directory
inventory against the README, single-line frontmatter/name/description bounds,
required sections including `Known issues and decisions`, `Do not`, `AGENTS.md`
references, reference links, orphan references and LICENSE headers. It does
not execute a skill, build a target or call an external validator; manual CI
execution remains pending.

The compile-free `control-security-static` contract pins BOB secret redaction,
weak callback ownership, receiver/admission shutdown seams, I2PControl
connection/token/deadline bounds, fragmented HTTP classification and private
atomic PEM replacement (including POSIX mode and Windows DACL/reparse seams).
It also pins the 2026-10-25 lifetime/handshake seams: strong StreamingDestination
ownership plus io-context Stop, the guarded per-port registry, SSU2 introducer
snapshots, TorrentFile locked handle pairs, deferred SOCKS authentication input,
HTTP/SOCKS handshake timers, the 1024-handler acceptor cap, the I2PControl
self-join guard and the SAF local-frame exception clear. It does not replace
TLS slow-client, BOB teardown, streaming/torrent/SOCKS runtime races, sanitizer
or platform filesystem execution in manual CI.

### Manual CI execution matrix

`.github/workflows/build.yml` now runs, on `workflow_dispatch` only:

- Linux Clang and GCC: complete build, full CTest, per-label inventory
  (`i2p`, `security`, `memory`, `ipc`, `tui`, `torrent`, `tools`, `perl`,
  `interop`, `platform`, `android`), version, Perl, branding and static
  interoperability checks. GCC, LLVM, CMake, Ninja, Boost, OpenSSL, zlib,
  MiniUPnPc and ncurses come from Homebrew so the manual runs compile against
  the current releases.
- Android: Java 25 guard, en/es/ru/zh-CN resource/locale static check, Gradle unit tests, debug/release Lint, class-file-69 assertion, native arm64 JNI link with hardening, debug APK packaging assertion and unsigned release APK. ABI/API/device matrices remain documented but not executed.
- Interop: pinned Java tag/commit/read-only check plus fixture provenance.
- Sanitizers: separate ASan+UBSan and TSan builds with the full CTest suite,
  using the current Homebrew Clang, Boost, OpenSSL, zlib and MiniUPnPc.

`.github/workflows/fuzz.yml` uses the current Homebrew Clang, Boost, OpenSSL
and zlib, builds all fifteen libFuzzer targets and runs a bounded smoke pass for
each. `.github/workflows/build-windows.yml` runs the
complete MSYS2/Clang64 target graph and CTest suite; the MSVC job remains
TUI/API-focused. `.github/workflows/build-openbsd.yml` runs the complete
OpenBSD CTest suite.

None of these workflow results may be attributed to uncommitted local work.
The current tree is still pending compilation and dynamic execution.

The 2026-10-22 continuation prepares `test-router-profile-race` for concurrent
profile reads/writes, canonical cache publication, postponed queue flushing
and dirty persistence claims. `test-android-daemon-lifecycle` uses the same
owner transition as Android JNI with injected operations to cover real start
invocation, nested-start rejection and failure/exception rollback. It is a
host seam test; JNI linking and device restart remain separate obligations.
Both are registered in CTest and the manual Linux focused step; the full
sanitizer matrix includes them. `tools-smoke` creates a signed public
RouterInfo via the non-installed `routerinfo-fixture` helper for positive
IPv4/IPv6 CLI checks. No helper, test, build or workflow was executed for
these local changes.

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
- bounded `CONNECTIONS`, `NETDB-ROUTERS`/`NETDB-LEASESETS` and `SERVICES`
  requests, including limit clamping;
- typed error-code names preserved by the client;
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
the message-mode pipe, protected user/SYSTEM (+container) DACL, pipe-name
squatting, peer PID/SID/session/container binding, `WorkerHello` nonce/role
rejection, role-bound per-operation refusal, exact frame ceilings, the
completed-during-cancel delivery and close/reconnect recovery.
`test-privsep-windows-isolation.cpp` checks the role table (no Internet
capability plus write root), AppContainer profile/SID/capability creation and
the delegated-root grant. `test-privsep-windows-supervisor.cpp` checks the
restart ladder/status, Job Object kill, failed-spawn handle neutrality and a
live Ping-only worker over an authenticated pipe. None proves cross-user ACL
denial, a real service account or a live storage/network backend.

`tests/test-su3-bounds.cpp` constructs empty, truncated and oversized SU3
headers and verifies that `Reseeder::ProcessSU3File()` rejects them without
allocating from unvalidated lengths. It does not replace a real SU3 signature
or archive test.

`tests/test-streaming-buffer.cpp` checks the owning `SendBuffer` copy
semantics, the `std::span<const uint8_t>` constructor and the writable staging
constructor. It does not exercise a live socket or an asynchronous completion.
`tests/test-streaming-throttle.cpp` checks the Java-compatible per-peer and
total minute/hour/day admission counters, retry intervals, saturation of the
peer table and periodic cleanup without constructing a streaming destination.
`tests/test-sam-subsessions.cpp` covers SAM 3.3 `LISTEN_PROTOCOL` grammar,
`(type,protocol,port)` duplicate keys and protocol-aware RAW dispatch/fallback;
it does not start a SAM bridge or contact a peer.
`tests/test-proxy-auth.cpp` covers local HTTP Basic parsing/bounds/constant-time
comparison, the 407 challenge, credential stripping, fail-closed configuration
and SOCKS5 RFC 1929 method selection/field bounds; it does not open a listener.
`tests/test-reseed-zip-bounds.cpp` also pins Proposal 147 `netid` URL
composition; neither test executes a live SYN or a reseed download.

`tests/test-log-lifecycle.cpp` exercises repeated `Log::Start`/`Stop`, sink
replacement, concurrent signal-style `Reopen` calls and the bounded
8192-record logger queue with its dropped-record counter without starting the
router. `tests/test-tui-ipc-regression.cpp` is a Unix-only real-socket test for
partial/pipelined TUI requests, malformed input, bounded connection/netDb
directory/service dispatch, atomic daemon log-level control, exact-once
execution of an accepted admin command after its waiter times out, typed remote
error codes, `QUIT`, socket permissions and cleanup. `tests/test-torrents-security.cpp` covers piece block bounds, metadata
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
`tests/test-privsep-protocol.cpp` covers v5/v1 frame negotiation and the v5 `WorkerHello` proof,
typed `Error` replies, the shared endpoint allowlist, fallible path encoders and
text/record bounds. `tests/test-privsep-linux-ipc.cpp` exercises raw
`SOCK_SEQPACKET` framing, credentials, descriptor ownership, validated channel
adoption (including AF_UNIX/SO_PASSCRED and rejection of a pre-queued record),
zero-length records and empty-record flood bounds, ancillary-data rejection,
bounded send pressure, the explicit `PeerExpectation` (wrong uid, pid mismatch,
root-parent allowance); `tests/test-privsep-linux-sandbox.cpp` checks storage
socket/socketpair/connect-on-inherited-socket denial, `execve`/`fork` denial,
outside-root read/write denial, effective-capability denial, process-memory/
pidfd syscall denial, log AF_INET-deny/AF_UNIX-allow, and that the supervisor
bootstrap drops capabilities without installing an inherited Landlock/seccomp
domain that would break exec'ed storage/network children;
`tests/test-privsep-log.cpp` adds real fork/worker descriptor policies.
`tests/test-router-storage.cpp` checks fixed-name atomic writes and that a
worker-level rejection does not poison later requests. On OpenBSD, `test-privsep-openbsd-policy` forks children, requires a pledge
violation for storage's AF_INET socket/`fork()`/file write and network's
persistent file creation, checks the supervisor's `x`-only unveil (`unveil`
after lock is `EPERM`, `execve("/bin/sh")` is denied while the frozen image
runs), the broker's `sendfd`-only descriptor contract, the startup/runtime
promise subset and the single pledge-file parser, and validates pre-opened
socket type/CLOEXEC/listening. `test-privsep-credentials` resolves the
service account with bounded lookups, rejects uid 0/unknown/malformed names and
verifies the forked credential drop; it skips as root. `test-privsep-openbsd-imsg` covers validated
adoption (getpeereid/socket type/CLOEXEC), one descriptor, descriptor ownership
on failed compose and multiple frames buffered by a single `imsgbuf_read`.
`test-torrent-storage-openbsd` exercises nested supervised delegation and a
symlinked/replaced path rejection that leaves the broker alive. The Windows
test covers oversized-message/malformed-channel termination, PID-bound
acceptance, server-PID-bound connect, random per-spawn pipe names, DACL
assertions and restricted-token/Job primitives. Cross-UID rejection still needs a second account/root, and the
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
the native tool CLI surface; `test-routerinfo-firewall` pins the endpoint
grammar, iptables/ip6tables and legacy `-f`, nftables family/tag comments,
Windows stable names/groups/error handling and PF labels. No firewall command is
executed by any test; it must not be confused with the historical
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
- `test-addressbook-admin.cpp` — local address-book name validation,
  add/replace/remove, deleted-host block/unblock, tombstone merge-skip,
  save-count/rollback behavior, shared-identity reference retention and the
  subscription URL validation/atomic-round-trip matrix.
- `test-addressbook-merge.cpp` — automatic subscription merges never re-point a
  known name, intra-source last-wins, conflict counting, tombstone skip and
  `.i2p.alt`/`www.` behaviour.
- `test-blocklist-parse.cpp` — Java-compatible blocklist grammar: address,
  range, CIDR, IPv6 `;` form, base64 hash, comments/reasons, backwards/bad
  input rejection and the line/file/entry caps.
- `test-blocklist-enforce.cpp` — snapshot matching inside/outside ranges,
  router-hash membership and the bounded in-memory runtime ban set.
- `test-tunnel-dsm-policy.cpp` — direct/tunnel DatabaseStore reply-token
  acceptance policy.
- `test-torrents-verify-parity.cpp` — resume/final-name re-verification,
  tracker peer forms/limits, tracker id/min interval, percent-encoded peer id,
  connection-limit relationships and top-level metainfo preservation/edit
  round trips.
- `test-torrents-tracker-schedule.cpp` — pure tracker scheduling arithmetic
  (15-minute advertised floor, larger `min interval`, 30-minute failure cap,
  reset on success) plus whole-torrent `left`/`completed` announce progress
  without sockets, timers or sleeps.
- `test-signature-identity-hardening.cpp` — Java-compatible raw RSA
  verification and RSA identity certificate layout.
- `test-su3-bounds.cpp` — malformed SU3 bounds plus the fail-closed
  `reseed.verify` default.
- `test-ssu2-preauth.cpp` — SSU2 PathChallenge retry/deadline schedule in
  addition to the existing block whitelist.
- `test-crypto-x25519-noise-elligator-mlkem-audit.cpp` — cached runtime
  ML-KEM provider gating.
- `test-x25519-rfc7748.cpp` — RFC 7748 section 6.1 X25519 public-key and
  shared-secret known-answer vectors, independent of the provider version.
- `test-tui-protocol.cpp` / `test-tui-ipc-regression.cpp` — typed TUI admin
  parsing/manual queue and real socket dispatch.
- `test-leaseset-netdb-2.13-freshness.cpp` — LS1/LS2 future lease windows,
  unpublished/blinded flags, EC_P256 wire keys, DatabaseStore type mask.
- `test-ssu2-address-negotiation.cpp` — SSU2 `v` lists, PQ/MTU selection and
  sub-1280 rejection.
- `test-ssu2-payload-blocks.cpp` — negative SSU2 block framing: DateTime and
  termination sizes, padding/termination ordering, truncated headers and the
  per-state block whitelist.
- `test-i2np-database-bounds.cpp` — DatabaseSearchReply key/count/from
  truncation, count overflow and Java-compatible trailing-byte handling.
- `test-ssu2-fragment-ordering.cpp` — last-fragment ordering/duplicate
  rejection.
- `test-crypto-wire-validation.cpp` — key-certificate lengths, strict b33
  flags, Ed25519ph and GOST signature rejection.
- `test-openssl-capabilities.cpp` — provider/algorithm probing for SHA/AES/
  ChaCha20/HKDF/SipHash and the runtime ML-KEM version gate.
- `test-tunnelconfig-ownership.cpp` — owned hop chains, ordering and
  phony-hop add/remove transitions without a router or tunnel pool.
- `test-runnableservice-lifecycle.cpp` — self-stop from a posted handler and a
  bounded lifecycle without self-join; TSan/restart stress remains pending.
- `packaging-static` — compile-free checks for POSIX make semantics, install
  development components, Ninja/OpenSSL guards, RPM generator/certificate
  metadata, static-package paths and Docker runtime dependencies.
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
The manual workflow runs ASan+UBSan together, UBSan-only (to expose undefined
behaviour that the combined run can mask) and TSan separately, then executes
the full CTest suite serially. Sanitizer findings are not suppressed.

```sh
cmake -S build -B build-asan -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON \
  -DWITH_ADDRSANITIZER=ON -DWITH_UPNP=OFF
cmake --build build-asan --parallel
ctest --test-dir build-asan -j 1 --no-tests=error --output-on-failure

cmake -S build -B build-ubsan -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON \
  -DWITH_UNDEFSANITIZER=ON -DWITH_UPNP=OFF
cmake --build build-ubsan --parallel
ctest --test-dir build-ubsan -j 1 --no-tests=error --output-on-failure

cmake -S build -B build-tsan -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON \
  -DWITH_THREADSANITIZER=ON -DWITH_UPNP=OFF
cmake --build build-tsan --parallel
ctest --test-dir build-tsan -j 1 --no-tests=error --output-on-failure
```

TSan is configured to fail if combined with ASan. TSan on the full router is
expected to flag the non-atomic tunnel/router status reads listed in
`security-audit.md`; triage those results before declaring the tree clean.

## Fuzzing

The manual `Fuzzing` workflow builds every target below, runs the one-input
`ctest -L fuzz` entries, and then a bounded `-max_total_time=30` smoke pass.
The fuzz lane sets `detect_leaks=0` because libFuzzer's process lifetime is not
one test-process lifetime; the ASan CTest lane keeps leak detection enabled.
Corpora and crash artifacts are uploaded with short retention. With
`-DWITH_FUZZERS=ON`, one-input `ctest -L fuzz` entries are also registered for
inventory and quick local
checks; they do not replace the corpus workflow. The commands remain the
reproducible local or CI recipe; no fuzz run is claimed for the current tree.

```sh
cmake -S build -B build-fuzz -G Ninja \
  -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo -DBUILD_TESTING=ON \
  -DWITH_FUZZERS=ON -DWITH_UPNP=OFF
cmake --build build-fuzz --parallel --target fuzz-tui-protocol fuzz-http-parser \
  fuzz-protocol-bounds fuzz-log-record fuzz-privsep-protocol fuzz-torrents-parsers \
  fuzz-torrents-krpc fuzz-torrents-extensions fuzz-base-blinding fuzz-signature-der fuzz-i2np-policy \
  fuzz-i2np-database fuzz-ntcp2-blocks fuzz-sam-control fuzz-i2cp-message
mkdir -p corpus/tui corpus/http corpus/protocol corpus/log corpus/privsep corpus/torrents corpus/krpc corpus/extensions corpus/base corpus/signature corpus/i2np corpus/ntcp2 corpus/sam corpus/i2cp corpus/i2np-db
ctest --test-dir build-fuzz -j 2 -L fuzz --no-tests=error --output-on-failure
./build-fuzz/tests/fuzz-tui-protocol corpus/tui
./build-fuzz/tests/fuzz-http-parser corpus/http
./build-fuzz/tests/fuzz-protocol-bounds corpus/protocol
./build-fuzz/tests/fuzz-log-record corpus/log
./build-fuzz/tests/fuzz-privsep-protocol corpus/privsep
./build-fuzz/tests/fuzz-torrents-parsers corpus/torrents
./build-fuzz/tests/fuzz-torrents-krpc corpus/krpc
./build-fuzz/tests/fuzz-torrents-extensions corpus/extensions
./build-fuzz/tests/fuzz-base-blinding corpus/base
./build-fuzz/tests/fuzz-signature-der corpus/signature
./build-fuzz/tests/fuzz-i2np-policy corpus/i2np
./build-fuzz/tests/fuzz-ntcp2-blocks corpus/ntcp2
./build-fuzz/tests/fuzz-sam-control corpus/sam
./build-fuzz/tests/fuzz-i2cp-message corpus/i2cp
./build-fuzz/tests/fuzz-i2np-database corpus/i2np-db
```

Corpus seeds are checked in under `tests/fuzz/corpus/<target>/` (one small,
non-secret seed per target; 64 KiB maximum) and are copied into the runner's
per-target corpus before the bounded pass. The CMake smoke entry passes the
same directory and a per-target `-max_len`, so a finding is reproducible from a
clean clone rather than a one-off temporary corpus. Crash and timeout files
use a per-target `-artifact_prefix`.

Targets prepared:

- `fuzz-tui-protocol` — request/response parser, field encoder, framing.
- `fuzz-http-parser` — request/response parser, URL decoder, chunk merger.
- `fuzz-protocol-bounds` — IdentityEx, LeaseSet, I2NP and RouterInfo parsers.
- `fuzz-log-record` — bounded/sanitized log record snapshot.
- `fuzz-privsep-protocol` — worker frame and log/config payload decoders.
- `fuzz-torrents-parsers` — bencode, metainfo, peer block, DHT node parsers.
- `fuzz-torrents-krpc` — KRPC envelope, token and error decoders.
- `fuzz-torrents-extensions` — PEX and BEP 10 extension payloads.
- `fuzz-i2np-policy` — I2NP dispatch predicates, expiry and clove allowlists.
- `fuzz-ntcp2-blocks` — SessionConfirmed block ordering and handshake padding tables.
- `fuzz-sam-control` — SAM HELLO/parameter/frame parsers.
- `fuzz-i2cp-message` — I2CP SEND MESSAGE wire parser.
- `fuzz-i2np-database` — DatabaseSearchReply body parser.
- `fuzz-base-blinding` — Base32/Base64 decoders and strict b33 parsing.
- `fuzz-signature-der` — ECDSA P-521 DER sizing and verification with a fixed identity.

Still planned: dedicated NTCP2/SSU2 packet-block fuzz harnesses; these need
protocol corpus seeds and must not call the public I2P network.

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
| S-11 OpenBSD sandbox | `test-privsep-openbsd-policy` (storage/no-AF_INET-no-fork, network/no-persistent-write, supervisor x-only unveil, pledge subset, pre-opened descriptors), `test-privsep-credentials`, `test-privsep-worker-identity` and `test-privsep-openbsd-imsg`; **pending** OpenBSD VM execution |
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
2. Resize the terminal; the layout redraws and a prompt below the minimum
   size is suspended until Esc or a resize.
3. Open Services (`v`), NetDb (`5`/`n`) and Logs (`6`) and confirm rows are
   bounded and the view does not block.
4. Disconnect the TUI (`q`); the router keeps running.
5. Kill the router; the TUI shows a connection error and retries when it
   returns. After restart, verify the Logs view resumes instead of freezing.
6. `kill -TERM` the TUI; it exits cleanly.
7. Run the router with `--daemon` and no TTY; confirm the daemon stays up and
   the socket exists.
8. Attempt to connect from another UID; expect rejection.
9. Send a malformed overlong request with `socat`; expect an error frame and
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
grep -nE 'wildcard|shell|patsubst|ifeq|ifneq|\$\(or |\$\(findstring' Makefile
# expect no output
```

OpenBSD port build (external port): configure with CMake/Ninja, keep
`pledge`/`unveil`, run the daemon as an unprivileged user with a custom
datadir and verify the TUI socket path appears in unveil. This is pending.

## I2P Java interoperability

No Java router is available in this environment. The static matrix in
`docs/INTEROPERABILITY.md` records the 2026-10-18 recheck of the latest stable
`i2p-2.13.0`; it does not claim verified interoperability. The current target
is `i2p-2.14.0` at `3c98503c5f14efe7bdd783528ba7f621f12d8081`; reviewing
the Java delta and updating the matrix remains pending under NET-026.
Recommended setup for the current target:

1. Run a current Java I2P router on a separate host or network namespace.
2. Exchange RouterInfos; verify NTCP2 and SSU2 sessions establish in both
   directions.
3. Build tunnels in both directions and carry HTTP/streaming traffic.
4. Publish and resolve a LeaseSet2 and an EncryptedLeaseSet.
5. Exercise SSU2 peer test/relay and IPv6.
6. Repeat with OpenSSL without PQ and with PQ enabled, and with LibreSSL.
7. Repeat on OpenBSD with a non-default user/datadir and MTU autodetection.

## Status summary

Rows marked *historical* predate the static-only policy and do not validate the
current uncommitted tree.

| Check | Status |
|---|---|
| Baseline compile (before changes) | Historical; not current evidence |
| Modified compile (Linux/GCC 16) | Historical: passed on 2026-10-18 (196 targets) |
| Unit tests and `tools-smoke` | Historical: 48/48 on 2026-10-18; no current run |
| ASan/UBSan/TSan | Separate manual CI jobs prepared; not run for the current tree |
| Build-system static check | `build-system-static` prepared, including dependency-floor, removed-compatibility and test/fuzzer inventory assertions; static check passes, no build execution |
| Agent Skills static check | `agent-skills-static` passed locally as a compile-free source check for exact inventory, README, frontmatter/name, required sections including `Known issues and decisions`, `AGENTS.md`, reference links and LICENSE headers; CI execution pending |
| Translation check | `i18n-static` validates native catalog registration, TUI selection/boundaries and Android locale metadata without compiling; CI execution pending |
| Language modernization check | `language-modernization-static` validates the AGENTS.md modernization and Agent Knowledge Maintenance policy, C++23/Java 25 levels, availability matrix, Strategy/`jthread`/registry seams and typed Java models without compiling; CI execution pending |
| CTest registration and labels | Static inventory prepared; Linux-only and OpenBSD-only tests are registered on their host, with i2p/security/memory/ipc/tui/torrent/tools/perl/interop/platform/android groups; CTest execution pending CI |
| Fuzzers | Fifteen targets, checked-in corpus seeds, per-target bounds/artifacts and manual smoke workflow prepared; not run |
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
- **Android locale policy:** `android/tools/check-android-locales.pl` is a
  compile-free CI guard for en/es/ru/zh-CN catalog parity, placeholders,
  `locales_config.xml`, locale filters and the per-app override boundary.
  `LocaleResourceTest` uses `createConfigurationContext` (not
  `Locale.setDefault`) and is prepared for the manual device job.
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

## Prepared client and persistence regressions (2026-10-21)

`test-i2ptunnel-http` exercises the production incremental HTTP transformer
without sockets: fragmentation, independent request/response output, binary
body bytes, header rewriting and byte/line limits. It is registered with
`unit`, `i2p`, `security` and `memory` labels.

`test-addressbook-case` also exercises the production resolver-request
registry: authenticated resolver binding, replay/nonce collision, duplicate
normalized names, capacity, expiry and shutdown clearing. It now carries
`security` and `memory` labels in addition to its existing labels.

The manual Linux workflow explicitly selects `test-i2ptunnel-http`,
`test-addressbook-case` and `test-torrents-security` after the full suite.

Formatting and fixed-buffer regressions are prepared as `test-address-format`
(IPv4/IPv6/scope IDs and endpoints), `test-util-copy-string` (zero capacity,
truncation, embedded NUL), `test-i18n-format` (empty/large/percent input) and
`test-socks-address` (zero-length and 255/256-byte domain names). They are
registered in CTest; execution remains pending manual CI.
The parity continuation adds `test-addressbook-admin` (Java-compatible name
validation, add/replace/remove, deleted-host block/unblock, tombstone merge-skip,
shared-identity reference safety and empty-book persistence through a fake
storage), `test-tunnel-dsm-policy` (token-bearing DatabaseStore rejection only
for tunnel-delivered messages), the `TORRENT-RECHECK`/`TORRENT-META-*` protocol
regressions, the metainfo-preservation round trip and Android `TorrentControl`
recheck/remove/metainfo command tests. The Android address-book screen uses
service-worker JNI; its device/instrumentation run remains pending.

The modern-language continuation prepares `test-modern` (concepts,
`constexpr` enum conversion, overload composition, move-only scope guard and
byte-span views), `test-transport-connector` (static strategy mapping and
null-server short-circuit), `RouterCommandTest` (sealed record action decoding)
and `LogFilterTest` (typed log levels and locale-independent classification).
The compile-free `language-modernization-static` check validates the policy,
language levels and seam inventory.

The i18n continuation adds `test-i18n-locale` (language normalization,
English fallback, UTF-8 catalog values, formatting and plural fallback) and
`i18n-static` (catalog registration, TUI linkage, Android locale metadata and
placeholder/key policy). `android-locale-policy` runs
`check-android-locales.pl`; `LocaleResourceTest` is the prepared device test.
None of these tests has run.

The OpenSSL 4 continuation adds `test-openssl-capabilities` (provider fetch
probing and the runtime PQ version gate) and `test-tunnelconfig-ownership`
(the remaining owned tunnel-hop chain). The manual workflow now has separate
OpenSSL 3.x, pinned OpenSSL 4.0.0/4.0.3 and Homebrew-latest lanes; the OpenBSD
job asserts LibreSSL via `I2PD_OPENSSL_EXPECT=LibreSSL`. The `famtool` positive
sign/verify round trip is still not exercised by `test-tools-regressions`; a
real RouterInfo fixture is required before claiming that path dynamically
tested.

The memory-safety continuation extends `test-http-res` with a bare-LF header
injection case and `test-tui-protocol` with C0/DEL rest-of-line argument
rejection. Garlic clove, RouterContext garlic/DeliveryStatus, SAF/JNI
pending-exception, SSU2 pool/address race, fragment-byte budget and
reseed/offline-key ASan cases remain prepared follow-ups; none has been run.

The upstream continuation extends `test-http-res` with serialized
400/431/500/502/504 status lines and `test-http-proxy-limits` with the
status-mapping constants; `test-torrents-dht-interop` pins multi-bucket
own-bucket membership, and `test-torrents-security` pins the reserved local
DHT peer slot and removal. `test-i2ptunnel-http` covers mixed-case
Host/Connection/Keep-Alive/X-I2P/Proxy-Connection headers and rejects obs-fold
continuations. These are registered in the existing CTest targets; no proxy
listener, DHT peer or live tracker is used.

The existing manual sanitizer, Windows and OpenBSD jobs consume the registered
tests through their full CTest runs. Android still requires JNI/native
compilation and device tests; these offline tests do not demonstrate live
HTTP/datagram interoperability, tracker/peer exchange, address lookup or
crash recovery.

All tests in this continuation are prepared only, pending CI compilation and
pending CI tests. No local build, test or project validator was executed.

The existing `fuzz-http-parser` target now also feeds all three production
tunnel modes, comparing whole-buffer and fragmented delivery including the
untouched body tail. Input for this additional comparison is bounded to
12 KiB. Its existing manual Fuzzing job and one-input CTest smoke registration
are retained; neither was executed for this increment.

`test-torrents-security` adds irreversible removal, direct/delegated resume
revocation, deterministic service/disk queue draining and replacement gating.
It checks that retaining payload still deletes native settings and that a
directory at the settings filename keeps removal fail-closed. POSIX cases
also cover symlink parents/leaves for anchored metadata deletion. No live
router, peer, sleeps or worker thread is required by these added cases.
Real shutdown snapshot draining, worker failures and crash durability still
need dedicated manual CI/device execution.

The SSU2/NTCP2 continuation adds prepared static/unit coverage for late packet
0 and monotonic ACK state, wrong-type packet flooding, owner-checked session
removal, failed SessionCreated Noise reset, PeerTest source/family/timestamp
and private/reserved target rejection, RelayIntro HolePunch rate limiting,
late `Established` callbacks, NTCP2 msg3p2 459/4048 boundaries and banned
termination handling. These remain pending CI compilation and CTest; no
live relay/PeerTest/MTU/IPv6 or TSan/ASan execution exists.

The 2026-10-21 cryptography continuation prepares `test-rng-seed`, extends the
AEAD empty-operation failure check, aligns the Ratchet tag boundary to
0..65535, adds GOST e=q/e=0 sign/verify vectors and Ed25519 on-curve/all-zero boundary
checks, and keeps the existing Elligator2, blinding, ML-KEM and NTCP2/SSU2
regressions. New negative
coverage is planned for invalid Ed25519 points, RSA/P-521 LS2 offline sections,
I2CP private-key length mismatches, AEAD provider faults, ratchet NEXT_KEY
amplification and SSU2 version fallback. None of these tests has been compiled
or executed in this worktree.

The netDb continuation extends `test-tunnel-dsm-policy` for the clove/tunnel
token rule, adds canonical LS1 trailing-byte coverage, LS2 PopulateLeases and
Meta latest-entry expiry checks, and prepares clove-origin, client DSRM binding,
lookup/flood throttle, zero-address RouterInfo, persistence snapshot, reseed
lifecycle and atomic file replacement regressions. None has been compiled or
executed locally.

The tunnel/I2NP/Garlic continuation prepares peer-uniqueness and zero-ID transit
tests, zero-hop replacement/variance tests, shared-RNG shuffle checks, OBEP
LOCAL and clove-type policy cases, gateway boundary/expiry vectors, IV replay
and reassembly caps, clove freshness/underflow cases, encryption-failure
canaries, custom-selector lifetime and peer-config race tests. None has been
compiled or executed locally.

The SAM/I2CP/Streaming/Datagram continuation prepares per-port accept teardown,
subsession PORT/duplicate checks, HELLO whitespace vectors, SESSION STATUS error
prefixes, b33 store-key lookups, single terminal I2CP statuses, staged
CreateLeaseSet2 validation, saved-packet/reassembly caps, stream-map races,
ping/pong ports, SYN authentication, Datagram3 unauthenticated hash delivery,
wildcard replacement and identity-refresh regressions. None has been compiled or
executed locally.

### Android IPC/SAF continuation

Prepared for manual Android CI/device only: JNI UTF-16/NUL/surrogate rejection;
bounded start diagnostics; SAF tree/grant release and detach; export grant scope
and zero-length failure state; close/release not retiring the broker; created
document name verification; display-name `:`/`\`; pending autostart across
Activity death; `MY_PACKAGE_REPLACED` resume; torrent id 0 rejection. No Gradle,
Lint, JNI link, emulator or device run exists.

The Android multiprocess/lifecycle continuation adds JVM-testable contracts:
`RouterRunStateTest` (legacy `should_run` migration, drain expiry and clamping,
corrupt deadlines, generation monotonicity, stopped-never-starts) and
`TorrentSafProtocolTest` (AIDL v2 constants, one-shot export tokens, caller
`(uid,pid)` binding, bounded admission, generation-fenced retirement,
range/path bounds). Normal Gradle `testDebugUnitTest` discovers both. The
instrumentation suite adds `TorrentSafBrokerContractTest` (real broker bind,
versioned handshake, foreign-generation refusal, typed capability fallbacks,
generation-fenced retire and a bounded rebind) and updates
`TorrentSafCancellationWireTest` for `retire(long)`; the manual Android job
assembles the instrumentation APK but no emulator/device lane runs it. The host
`test-android-daemon-lifecycle` adds the one-shot process-role and
stop-during-start contracts, and `control-security-static` pins the JNI
empty-string/exception/role/stop-token and SAF destructor rules. The
`onTaskRemoved`/`onDestroy` service contract needs a device and stays manual. No
Gradle, javac, AIDL, JNI, emulator or device run exists.

## Static build audit regressions

`test-boost-linkage` is registered only with torrents enabled and carries
`unit;ci;torrent` labels. It calls out-of-line JSON/URL APIs through the client
archive's link interface. The installed-consumer manual job checks the same
closure after `find_package(i2pd)`. `test-i18n-locale` includes the locale
accessor header first and covers qualified/global translation helpers;
`test-torrents-engine-interop` and `test-privsep-linux-ipc` retain corrected
fixtures/assertions. The MSYS2 job exercises `WITH_HARDENING=ON` PE flags.
These tests were prepared without execution; see
[the audit report](static-build-audit.md) for outstanding platform, UAPI and
Android toolchain validation.

The CONC-010/ANDROID-018 continuation prepares `test-router-profile-storage`
(isolated immutable root, unknown staging preservation, INI round trip, expiry/save serialization, failed load
and POSIX shard-symlink refusal) and `test-daemon-init-rollback` (reverse phase
unwind, unsupported storage before context, cleanup exception and committed
retry). `test-log-lifecycle` additionally covers stopped destination reset.
These targets are registered in host CTest and the manual Linux focused lane;
full suites/sanitizers consume their labels. Synthetic rollback does not invoke
production daemon cleanup. No compilation or test execution has occurred.

The static lifecycle continuation prepares `test-netdb-stop-admission` with a
paused-worker admission fixture: accepted messages must drain, concurrent
posts cannot repopulate a stopped queue, and lookup/flood throttles reset.
`test-tunnels-restart` adds successful admission, stopped rejection and list
consumption under a Post/Stop race. Neither proves producer generation fencing.
The profile-storage test now preserves expired unknown/staging/wrong-shard
leaves and symlinks while removing an expired canonical profile. Manual
packaging CI adds maintenance ShellCheck and a fake-`nc` mismatch-status check;
the latter is not a live SAM test. All new coverage remains unexecuted.

`test-datagram-receiver-lifecycle` prepares stale registration, wildcard/port
dispatch, external callback drain, same/cross-slot reset and callback-destructor
reentry cases. Its threaded drain/race checks are stress coverage, not proof of
all schedules or caller teardown paths. `ServiceOwnerEpochTest` uses a blocked
process FIFO to check accepted old work, stop barrier, new-owner work and stale
submission/retirement behavior; normal Android JVM CI discovers it. JNI, actual
Service callbacks and per-caller sanitizer runs remain pending.

The 2.14.0 parity audit continuation prepares `test-blocklist-parse`,
`test-blocklist-enforce`, `test-addressbook-merge`, the extended
`test-addressbook-admin`, `test-torrents-tracker-schedule`, the extended
`test-torrents-priorities`/`test-torrents-verify-parity`, new
`test-tui-protocol` cases for `ADDRESSBOOK-SUBSCRIPTIONS`, `BLOCKLIST` and the
typed `ADDRESS-SUB-*`/`NETDB-BAN` verbs, and updated `test-netdb-stop-admission`
expectations for the non-floodfill lookup budget. `control-security-static`
pins the immutable blocklist snapshot, merge precedence, tracker progress and
the closed administrative verb set. None of these has been compiled or
executed; manual CI is the first dynamic check.

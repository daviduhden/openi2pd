<!--
See the LICENSE file at the top of the project tree for copyright
and license details.

openi2pd: cpp23 migration.

Documents project documentation.

-->

# C++23 migration report

## Standard and compiler floor

`CMAKE_CXX_STANDARD` is now 23 with `CXX_STANDARD_REQUIRED ON` and
`CXX_EXTENSIONS OFF`. The configure step rejects GCC < 13, Clang < 17 and
MSVC < 19.35. There is no C++20 fallback.

## Deliberate modernization policy

Modern C++ usage is an explicit project objective, not only a compatibility
requirement. New and refactored code should prefer the strongest supported
facility that keeps ownership, protocol compatibility and error behavior
clear. The project deliberately permits abstractions, templates and OOP
structure even when a procedural implementation would work and no measurable
performance gain is claimed.

The policy is bounded by compiler/library availability. The baseline is GCC
13, Clang 17, MSVC 19.35 and OpenBSD base libc++ with LibreSSL. Features that
are not proven by the build probes or the manual CI matrix remain unavailable.

| Facility | Status on the declared baseline | Project use |
|---|---|---|
| concepts/constraints, folds, structured bindings | available | preferred for new generic seams and bounded parsing |
| `constexpr`/`consteval`/`constinit` | available; already used in Base/Identity-style tables | compile-time tables and invariants |
| `std::optional`/`variant`/`any` | available | optional state and closed alternatives |
| `std::expected` | language feature available; library missing on some OpenBSD libc++ | use the portable `i2p::expected` shim |
| `span`/`string_view` | available | explicit extents and non-owning text |
| ranges/views, generic lambdas, `<=>` | available with libc++/libstdc++ support | local algorithms and comparisons |
| `chrono`, `jthread`, stop tokens | available; already used in TUI | timers and owned workers with explicit joins |
| `std::format`/`std::print` | not assumed on the OpenBSD floor | `snprintf`/existing formatting stays |
| `std::mdspan` | not assumed | no packet-buffer adoption yet |
| coroutines/executors | not assumed with the current Boost.Asio floor | remain rejected |
| modules | not assumed by CMake/toolchain matrix | no module migration |

New feature usage must be covered by a source-level regression where practical;
dynamic availability remains a manual-CI claim, never a local inference.

## Modernization seams and limits

- `libi2pd/Modern.h` is the shared vocabulary for enum/byte-range concepts,
  `ScopeExit`, overload composition and byte-span views. Adoption is currently
  narrow (`Identity.cpp`, `test-modern`); do not duplicate its helpers.
  `ScopeExit` cleanup must not throw and `AsByteSpan` intentionally rejects
  temporaries.
- `libi2pd/TransportConnector.h` owns outbound NTCP2/SSU2/Yggdrasil Strategy
  selection. Its context is non-owning and call-scoped; `TryConnect == false`
  means "next priority", while a pending SSU2 endpoint must return `true`.
- `Identity.cpp` uses a `constexpr` signing-key size registry and returns
  `false` for unknown types. `Log.cpp` uses a static range table; the global
  `LogLevel` enum keeps its `eNumLogLevels` sentinel.
- `TuiServer` and the X25519 supplier use `std::jthread`/stop tokens. The main
  `Transports` worker still has an explicit self-join/detach path, so jthread
  adoption is intentionally partial.
- Android-specific Java 25 source features and API limits are in
  `docs/android-java25.md`; they do not change the C++/Android wire contract.

## Applied changes

| Facility | Where | Why it improves the code |
|---|---|---|
| `std::expected` (portable shim on OpenBSD libc++) | `libi2pd/TuiProtocol.h`, `tui/TuiClient.*` | Parsing and socket operations return typed errors instead of sentinel values or exceptions across the IPC boundary |
| `std::jthread` / `std::stop_token` | `daemon/TuiServer.*` | RAII thread ownership and cooperative cancellation; `Stop()` requests cancellation and joins |
| `std::span` | `libi2pd/Base.*`, `Streaming`, `tools/common/file.hpp` | Base64 encode/decode and tool/file writes receive explicit extents; pointer/length overloads remain |
| `consteval` table construction | `libi2pd/Base.cpp` | Removes lazy, racy initialization of the base64 reverse table |
| `constexpr` `std::array`/`std::optional`/ranges registry | `libi2pd/Identity.cpp` | Signing-key wire sizes are one compile-time table instead of a duplicated switch |
| `std::jthread`/`std::stop_token`/`condition_variable_any` | `libi2pd/Transports.*`, `daemon/TuiServer.cpp` | Workers own their lifetime and cancellation; the TUI listener has no separate stop flag |
| Move-only `ScopeExit` guard | `libi2pd/Identity.cpp` | Private-key parse failure cleanup is one path instead of repeated `fail()` returns |
| Strategy interface + protocol connectors | `libi2pd/TransportConnector.h`, `Transports.cpp` | Outbound NTCP2/SSU2/Yggdrasil selection is polymorphic and isolated from peer policy |
| Concepts / abbreviated constraints | `libi2pd/TuiProtocol.h` field encoder, `libi2pd/Modern.h` | Restricts generic APIs and keeps misuse a compile-time error |
| Ranges / views | `libi2pd/Identity.cpp`, `libi2pd/Transports.cpp`, `libi2pd/Log.cpp` | Replaces hand-written scans/if-chains with constrained algorithms; no lifetime extension |
| `std::string_view` / `starts_with` / `ends_with` / `contains` | New TUI code, HTTP, base64, transport snapshots | Avoids copies and removes C++20 fallback branches |
| `std::chrono` | TUI refresh/retry scheduling; existing daemon loop | Explicit time units for non-blocking refresh and reconnect |
| `std::vector` / `std::array` ownership | `Reseed.cpp`, `HTTP.cpp`, `Base.cpp` | Replaces manual `new[]`/`delete[]` and fixed C buffers in error paths |
| `std::atomic` | `NetDb::m_IsReseeding` | Exposes reseed state without a data race |
| `std::mutex` snapshots | `Log`, `NetDb`, `Transports` | Administrative reads cannot race with router threads |
| `std::unique_ptr` ownership trees | `KadDHT`, `ClientContext`, `AddressBook` | Replaces raw child/server/storage owners and removes manual `new`/`delete` on every error path |
| RAII buffers and vectors | `Garlic`, `Reseed`, `ClientContext`, `x25519` | Tag sets, compressed files, key files and OpenSSL handles have deterministic scope |
| Scoped descriptors / `FILE*` / OpenSSL | `UnixDaemon`, `I2PControl`, `Signature`, `Gost` | Readiness pipes, pidfile guards, certificate objects and EVP handles are released exactly once |
| Endian conversion | `libi2pd/I2PEndian.h` | Windows uses `std::endian`/`std::byteswap`; Unix/Android use the platform `endian(3)` header, removing the legacy fallback and compiler intrinsics |

## Deliberate limits

- **Ownership was not mass-converted to smart pointers.** Router globals,
  tunnel session graphs and `shared_from_this` handlers already have explicit
  lifetimes. Replacing raw pointers mechanically would hide cycles and change
  destruction order in protocol code.
- **No `std::format` in the daemon or TUI.** The supported OpenBSD/libc++
  toolchain is not assumed to provide a complete implementation; `snprintf`
  remains in formatting-only paths.
- **No coroutines or executors.** The networking stack is Boost.Asio-based and
  converting it would be a rewrite without a protocol benefit.
- **No ABI-breaking container changes in public headers.** `Base64ToByteStream`
  keeps its pointer/length overload next to the new span overload.

## Removed legacy preprocessor branches

The following constructs were removed because the new baseline makes them
statically unreachable:

- `#if __cplusplus >= 202002L` / `< 202002L` in `Destination`, `Tag`,
  `Transports`, `AddressBook`, `ClientContext`, `I2CP`, `I2PService`, `SAM`,
  `SOCKS`, `RouterContext`.
- `BOOST_VERSION >= 105500` Asio option branch.
- Boost.Filesystem (`STD_FILESYSTEM`) and Boost.Atomic `shared_ptr` fallbacks; `std::filesystem` and `std::atomic<std::shared_ptr>` are now unconditional.
- `boost/lexical_cast.hpp` Android fallback, `BOOST_ASIO_HAS_STD_ARRAY`, Boost.Bind global-placeholder workaround and the `JSON_SUPPORTED`/old-Boost fallback.
- Torrents `std::atomic_ref` capability fallback; C++23 standard usage is unconditional.
- Legacy Windows endian intrinsics (`_byteswap_*`/`__builtin_bswap*`) and the `LittleBigEndian` fallback; Windows uses C++23 `std::endian`/`std::byteswap`.
- `WITH_TUI` and `I2PD_BUILD_TESTS` aliases; `I2PD_BUILD_TUI` and `BUILD_TESTING` are canonical.
- C++20 comments on `<utility>` includes.
- C++20 fallback in `Base64EncodingBufferSize`.

## Remaining modernization candidates (not applied)

These need separate review because they affect protocol lifetimes or hot
paths:

1. Replace `std::list<std::shared_ptr<...>>` peer/session queues with intrusive
   or deque-based structures after profiling.
2. Add `std::expected` to filesystem/configuration APIs without changing the
   existing exception contract at daemon startup.
3. Evaluate `std::mdspan` only for fixed packet buffers, never as a lifetime
   extension.
4. Finish the remaining raw `Packet*` ownership in Streaming,
   `GOSTR3411_2012_CTX` buffering and the remaining global crypto
   precomputation lifetimes only after a dedicated hot-path and API review.
5. Keep `GetRouterInfo()` compatibility readers, SSU2 shared socket/timer
   executor ownership and I2CP/SAM output sequencing under separate
   concurrency work; do not claim these are fixed by the ownership pass.

TUI-visible router/tunnel status fields now use atomics or bounded accessor
snapshots; that item was moved out of the candidate list.

## Validation status

Historical Linux/GCC 16 build and TUI test results in `testing.md` predate the
current static-only policy and the current uncommitted tree. They are not
attributed to the latest ownership or memory/concurrency changes.
Behavioral compatibility of the modernized code remains subject to manual CI.

## Ownership/RAII continuation validation

The Kademlia, Garlic, Reseed, daemon descriptor/OpenSSL, ClientContext/
AddressBook, span-tool and signature/GOST changes in this continuation are
**static-only**. The new `test-kad-dht`, `test-addressbook`, extended
`test-reseed-zip-bounds` and `test-signature-identity-hardening` cases are
registered but have not been compiled or executed. ASan/UBSan, TSan, Windows,
OpenBSD, Android and live-peer validation remain pending manual CI.

## Memory/concurrency static continuation (Session 29)

The SSU2 payload policy, ECDSA DER/AES/X25519, torrent piece state,
I2CP/SAM/ClientContext lifecycle, TunnelPool restart, timestamp/NTP,
RouterContext service/static-key, NetDb and Logger corrections are
**static-only**. New regressions include `test-ssu2-preauth`,
`test-torrents-concurrency`, `test-http-proxy-limits`,
`test-i2cp-session-lifecycle`, `test-clientcontext-registry`,
`test-sam-stream-lifecycle`, the tunnel-pool/restart tests,
`test-timestamp-offset`, `test-ntp-timesync-lifecycle` and the
RouterContext lifecycle/static-key tests. `fuzz-signature-der` was added to
the optional fuzzer set. None has been compiled, run, sanitized, fuzzed or
validated on OpenBSD/Windows/Android/live peers in the current tree.

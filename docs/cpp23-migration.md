# C++23 migration report

## Standard and compiler floor

`CMAKE_CXX_STANDARD` is now 23 with `CXX_STANDARD_REQUIRED ON` and
`CXX_EXTENSIONS OFF`. The configure step rejects GCC < 13, Clang < 17 and
MSVC < 19.35. There is no C++20 fallback.

## Applied changes

| Facility | Where | Why it improves the code |
|---|---|---|
| `std::expected` (portable shim on OpenBSD libc++) | `libi2pd/TuiProtocol.h`, `tui/TuiClient.*` | Parsing and socket operations return typed errors instead of sentinel values or exceptions across the IPC boundary |
| `std::jthread` / `std::stop_token` | `daemon/TuiServer.*` | RAII thread ownership and cooperative cancellation; `Stop()` requests cancellation and joins |
| `std::span` | `libi2pd/Base.*`, `Streaming`, `tools/common/file.hpp` | Base64 encode/decode and tool/file writes receive explicit extents; pointer/length overloads remain |
| `consteval` table construction | `libi2pd/Base.cpp` | Removes lazy, racy initialization of the base64 reverse table |
| Concepts / abbreviated constraints | `libi2pd/TuiProtocol.h` field encoder | Restricts integer formatting to integral/bool values at compile time |
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

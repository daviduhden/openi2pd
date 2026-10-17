# C++23 migration report

## Standard and compiler floor

`CMAKE_CXX_STANDARD` is now 23 with `CXX_STANDARD_REQUIRED ON` and
`CXX_EXTENSIONS OFF`. The configure step rejects GCC < 13, Clang < 17 and
MSVC < 19.35. There is no C++20 fallback.

## Applied changes

| Facility | Where | Why it improves the code |
|---|---|---|
| `std::expected` | `libi2pd/TuiProtocol.h`, `tui/TuiClient.*` | Parsing and socket operations return typed errors instead of sentinel values or exceptions across the IPC boundary |
| `std::jthread` / `std::stop_token` | `daemon/TuiServer.*` | RAII thread ownership and cooperative cancellation; `Stop()` requests cancellation and joins |
| `std::span` | `libi2pd/Base.*` | Base64 decoding receives an explicit destination extent; short buffers are rejected before any write |
| `consteval` table construction | `libi2pd/Base.cpp` | Removes lazy, racy initialization of the base64 reverse table |
| Concepts / abbreviated constraints | `libi2pd/TuiProtocol.h` field encoder | Restricts integer formatting to integral/bool values at compile time |
| `std::string_view` / `starts_with` / `ends_with` / `contains` | New TUI code, HTTP, base64, transport snapshots | Avoids copies and removes C++20 fallback branches |
| `std::chrono` | TUI refresh/retry scheduling; existing daemon loop | Explicit time units for non-blocking refresh and reconnect |
| `std::vector` / `std::array` ownership | `Reseed.cpp`, `HTTP.cpp`, `Base.cpp` | Replaces manual `new[]`/`delete[]` and fixed C buffers in error paths |
| `std::atomic` | `NetDb::m_IsReseeding` | Exposes reseed state without a data race |
| `std::mutex` snapshots | `Log`, `NetDb`, `Transports` | Administrative reads cannot race with router threads |

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
- C++20 comments on `<utility>` includes.
- C++20 fallback in `Base64EncodingBufferSize`.

## Remaining modernization candidates (not applied)

These need separate review because they affect protocol lifetimes or hot
paths:

1. Convert tunnel/router status fields read by the TUI into atomics or
   accessor snapshots under their own locks.
2. Replace `std::list<std::shared_ptr<...>>` peer/session queues with intrusive
   or deque-based structures after profiling.
3. Add `std::expected` to filesystem/configuration APIs without changing the
   existing exception contract at daemon startup.
4. Evaluate `std::mdspan` only for fixed packet buffers, never as a lifetime
   extension.

## Validation status

- **Compiled:** passed in the Linux/GCC 16 build recorded in `testing.md`.
- **Dynamically validated:** the TUI protocol unit test and a live TUI/IPC
  session passed on Linux. Sanitizers and cross-platform builds remain
  pending.
- **Behavioral compatibility:** protocol formats were not changed.

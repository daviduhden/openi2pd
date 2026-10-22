<!--
See the LICENSE file at the top of the project tree for copyright
and license details.

openi2pd C++23 ownership reference.
-->
# C++23 ownership and interface checklist

Use this reference after reading `SKILL.md`. It is a review aid, not a build
or test procedure.

## Component map

| Area | Main files | Shared boundary |
|---|---|---|
| Core values and containers | `Base.*`, `Identity.*`, `CryptoKey.*`, `LeaseSet.*`, `RouterInfo.*` | Serialization and signature lifetimes |
| Transports | `Transports.*`, `NTCP2.*`, `SSU2*`, `Tunnel*`, `Garlic.*` | `RouterContext`, packet buffers, timers |
| NetDb and routing | `NetDb.*`, `KadDHT.*`, `NetDbRequests.*`, `RouterContext.*` | Locks, snapshots, persistence |
| Cryptography | `Crypto.*`, `Signature.*`, `PostQuantum.*`, `ECIESX25519AEADRatchetSession.*` | Key handles and secret wiping |
| Client protocols | `libi2pd_client/SAM.*`, `I2CP.*`, `Streaming.*`, `Datagram.*`, `HTTPProxy.*`, `SOCKS.*` | Async owners and connection lifetimes |
| Torrent | `libi2pd_client/Torrents*`, `TorrentPayloadStorage.*`, `TorrentStorageWorker.*` | Service/storage/network domains |
| Daemon/TUI/IPC | `daemon/*`, `tui/*`, `privsep/*` | Shared protocol and shutdown ordering |
| Modernization seams | `Modern.h`, `TransportConnector.h`, `Log.*` | `language-modernization-static` pins named symbols/paths |

## Review questions

1. Who owns each allocation, descriptor, thread and callback?
2. Can a callback outlive its owner, session, connection or `RouterContext`?
3. Is every borrowed buffer represented as a span with a lifetime that covers
   the call, or is it stored accidentally?
4. Are reads and writes of shared state under the same lock/snapshot discipline?
5. Does an error path release descriptors, timers, sockets and child processes?
6. Does a new `std::expected`/optional path preserve the original error
   category and avoid silently treating corruption as absence?
7. Are secret buffers wiped on every exit path?
8. Does the change alter a wire or persisted format? If so, stop and require a
   specification reference, migration and compatibility test.
9. Did the change accidentally add a compiler-specific fallback, GNU make use,
   a new runtime download or a hidden external checkout?
10. Are all call sites, tests and documentation references updated?

## Reconnaissance prompt

Use an `explore` subagent with a prompt shaped like:

> Read-only reconnaissance. Do not edit, build, test or run project scripts.
> Inspect `<paths>` and report: owning objects, raw/owning pointers, callback
> capture, thread affinity, lock/snapshot boundaries, destructor ordering,
> external callers, tests and docs. Give file and line references. Identify
> the smallest independent implementation slices and shared headers that need
> coordinator review.

## Implementation slices

- Keep header/API changes in one slice; do not parallelise two edits to the
  same header.
- Keep serialization/lifetime fixes separate from formatting or naming.
- Put regressions in `tests/test-*.cpp`, then register them in
  `tests/CMakeLists.txt` with labels.
- Update `docs/cpp23-migration.md` or the component document only when the
  design or remaining-risk statement changed.

## CI expectations

- Linux Clang and GCC jobs: full build and CTest.
- `-L memory` for lifetime/bounds changes; `-L security` for parser/crypto
  changes.
- Separate ASan+UBSan and TSan jobs when the change touches concurrency,
  callbacks or lifetime.
- `build-system-static` when source lists, options or labels change.
- Windows, OpenBSD and Android only when the touched code is compiled there.

No local compilation or test execution is permitted. A static review is not a
CI pass.

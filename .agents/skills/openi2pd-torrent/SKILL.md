---
name: openi2pd-torrent
description: Use when changing the native openi2pd I2PSnark-compatible BitTorrent engine, torrent creation, DHT/PEX/tracker behavior, priorities, comments, local state or isolated payload storage.
---
<!--
See the LICENSE file at the top of the project tree for copyright
and license details.
-->

# openi2pd torrent engine

## Activate when

Use this skill for `libi2pd_client/Torrents*`, the native torrent control
facade, DHT/PEX/tracker behavior, metainfo creation, file priorities,
comments, resume/state persistence or isolated payload storage. Use
`openi2pd-android` for the Java/AIDL/JNI SAF surface, `openi2pd-privsep` for the
worker/fd/capability contract, and `openi2pd-tui` for console presentation.

Read [`../../../AGENTS.md`](../../../AGENTS.md) first. The torrent client is
I2P-only and must remain free of browser, JavaScript, Node/V8/QuickJS and
clearnet fallback paths.

## Inspect first

- Engine and surfaces: `Torrents.*`, `TorrentsTunnel.*`, `TorrentsControl.*`,
  `TorrentsDHT.*`, `TorrentsCreator.*`, `TorrentsComments.*`,
  `TorrentsState.*`, `TorrentsRPC.*`.
- Storage: `TorrentPayloadStorage.*`, `TorrentStorageWorker.*`,
  `privsep/TorrentStorage.*`, `daemon/Daemon.*` and the torrent tunnel config.
- Android SAF native contract: check `TorrentSafStorage*`,
  `TorrentSafCreator*`, `TorrentSafStorageService.*`, `TorrentDocumentReader.*`
  and AIDL/JNI only to confirm torrent semantics. Android owns the Java,
  AIDL/JNI and process implementation; do not edit it from this skill.
- [`../../../docs/torrent/architecture.md`](../../../docs/torrent/architecture.md)
  and the other torrent documents listed below.
- Current `SNARK-*`, `TORRENT-*` and `PARITY-*` rows in `TODO.md`.

## Reference sources

- [`../../../docs/torrent/architecture.md`](../../../docs/torrent/architecture.md)
  — layers, snapshot model, non-goals and staged process split.
- [`../../../docs/torrent/snark-compatibility.md`](../../../docs/torrent/snark-compatibility.md)
  — Java I2PSnark reference and functional/protocol matrices.
- [`../../../docs/torrent/local-state.md`](../../../docs/torrent/local-state.md)
  — versioned native state and atomic persistence.
- [`../../../docs/torrent/storage-worker.md`](../../../docs/torrent/storage-worker.md)
  — isolated payload/state operations and capability limits.
- [`../../../docs/torrent/saf-storage.md`](../../../docs/torrent/saf-storage.md)
  — Android SAF authority and recovery.
- [`../../../docs/torrent/ipc.md`](../../../docs/torrent/ipc.md) — typed TUI
  commands, response fields, queue/backpressure and engine-thread boundary.
- [`../../../docs/torrent/router-storage.md`](../../../docs/torrent/router-storage.md)
  — opt-in router persistence worker and its four fixed files.
- [`../../../docs/torrent/security.md`](../../../docs/torrent/security.md) and
  [`../../../docs/torrent/testing.md`](../../../docs/torrent/testing.md).
- [`references/engine-checklist.md`](references/engine-checklist.md) — engine,
  storage and test map.

## Reconnaissance

Launch at least one read-only `explore` subagent before editing, split across
independent areas when useful. If subagents are unavailable, report that
limitation; do not substitute local execution. Ask for:

1. engine/control ownership, service-thread snapshots and command queueing;
2. parsing, path containment, piece verification and persistence;
3. DHT/PEX/tracker and private-swarm policy;
4. SAF document/tree authority and Binder recovery;
5. existing torrent regressions and fuzz coverage.

Ask for exact file/function references and independent implementation slices.
Do not let them edit, build or run tests.

## Parallel implementation

- Split engine, storage, DHT/PEX, creator and comments into independent slices;
  do not edit `Torrents.h`, `TorrentsControl.h` or the TUI protocol
  concurrently without coordinator review.
- TUI presentation belongs to `openi2pd-tui`; Android UI, AIDL, JNI and SAF
  broker implementation belong to `openi2pd-android`; process/fd/capability
  semantics belong to `openi2pd-privsep`. The torrent owner defines the typed
  facade, engine semantics and bounds.
- Keep control-plane changes separate from payload I/O changes.
- Do not let two agents update the same compatibility-matrix row.

## Conventions

- Preserve v1 info bytes and infohash; pure v2 is unsupported and a hybrid only
  works through a valid v1 layout. Do not silently reinterpret unknown
  dictionaries.
- Validate bencode nesting/counts/lengths and metadata identity before applying
  paths, allocating pieces or announcing.
- Keep all peer/tracker/DHT transport on I2P. Never add clearnet DNS, UDP DHT,
  HTTP trackers or a browser bridge.
- Enforce no-follow containment and exact authorized roots; peer-originated
  input must never supply administrative paths.
- Run file I/O on the storage execution domain. With `isolatedstorage=true`,
  use the typed worker and fail closed; never fall back to parent writes.
- Preserve pending-write withholding: unflushed pieces must not appear in
  HAVE/bitfield/serving/promotion; a failed write invalidates the piece.
- Preserve private-swarm suppression of DHT/PEX/comments even if generic
  settings are enabled.
- Keep comments unauthenticated pseudonyms; do not claim author identity.
- Keep native control typed and bounded; no raw engine pointers or arbitrary
  filesystem paths cross IPC.

## Known issues and decisions

- `TorrentPayloadStorage` is a header-only interface implemented by
  `Torrents.cpp`; do not look for a separate implementation unit.
- Original info bytes, consent for `ut_comment`, private-swarm suppression and
  stopped/removed persistence guards are load-bearing. Pending writes are
  withheld from HAVE/bitfield/serving/promotion.
- SAF cancellation must not close another thread's descriptor; the worker owns
  the `AssetFileDescriptor`. Broker/process recovery stays generation-fenced.
- Isolated payload/router persistence is opt-in and not global separation.
  `SNARK-001..SNARK-010`, live tracker/DHT/peer and device tests remain pending.
- DHT heard-from state is keyed by `NodeID`, while a pending query stores the
  destination peer `IdentHash`. A ping timeout must resolve the peer through
  `RoutingTable::FindNodeByPeer` before checking `m_HeardFrom`; the comparator
  is not transparent and peer hashes are not node IDs.

## Do not

- Do not compile, run CTest, sanitizers, fuzzers, Gradle or validators locally.
- Do not reintroduce the web console, HTML/CSS/JS, Node/V8/QuickJS, Python or a
  cleartext torrent path.
- Do not weaken bencode/path/piece/identity checks or resource caps.
- Do not claim complete I2PSnark or OS-level privilege separation.
- Do not change Java-compatible wire formats without a specification and
  compatibility test.
- Do not create commits, tags, releases, worktrees or temporary commits, and do not push; inspect `git status`/`git diff` before editing.
- Do not install dependencies, fetch/vendor code or perform hidden downloads to enable a local build or test. Preserve LICENSE/NOTICE/provenance and technical identifiers/paths unless an approved migration exists (AGENTS.md).

## Prepare for CI

- Register focused tests under `tests/` with the `torrent` label; include
  `test-torrents-*`, `test-torrent-*` and SAF-related Android tests.
- Use `fuzz-torrents-parsers` for new parser surfaces.
- Prepare negative cases for path traversal, symlink/hardlink, identity
  mismatch, private-torrent leakage, malformed DHT/PEX, provider denial and
  worker write failure.
- Include Linux/OpenBSD storage-worker tests, Android unit/instrumentation
  lanes and a controlled Java I2PSnark exchange where applicable.
- Record tests as prepared until the manual CI/device run passes.

## Tracking

Use `TORRENT`/N, `SNARK-*` and `PARITY-*`. Record engine vs storage vs SAF
scope, protocol/compatibility impact, tests, and the exact native/Android CI
lane. Update `docs/torrent/snark-compatibility.md` or the relevant design doc
when coverage changes. Propose a dated `PROGRESS.md` entry and any `TODO.md`/`ROADMAP.md` changes to the coordinator; append them only when explicitly assigned; subagents report
proposed rows to the coordinator.

Record the exact AGENTS.md `Tracking states` value and state which validations were not run; do not collapse them into “pending”.

## References

- [Torrent engine and storage checklist](references/engine-checklist.md)
- [Native torrent architecture](../../../docs/torrent/architecture.md)
- [I2PSnark compatibility](../../../docs/torrent/snark-compatibility.md)
- [Global policy](../../../AGENTS.md)

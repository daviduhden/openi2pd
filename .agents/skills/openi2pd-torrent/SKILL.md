---
name: openi2pd-torrent
description: Use when changing the native openi2pd I2PSnark-compatible BitTorrent engine, torrent creation, DHT/PEX/tracker behavior, priorities, comments, local state or isolated payload storage.
---

## Maintenance & Continuous Learning

When this skill guides a repository change, review it after implementation. Update procedures, tools, configurations, assumptions, examples and validation steps when they changed or when reusable knowledge improves future work; correct inaccurate guidance and omit one-off execution details. Report synchronization in the task summary. Follow the normative process and Definition of Done in [AGENTS.md](../../../AGENTS.md#continuous-agent--skills-maintenance).
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
  [`../../../docs/torrent/testing.md`](../../../docs/torrent/testing.md),
  [`../../../docs/torrent/console-matrix.md`](../../../docs/torrent/console-matrix.md)
  and [`../../../docs/torrent/tui.md`](../../../docs/torrent/tui.md).
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

- Apply the [source documentation policy](../../../AGENTS.md#source-documentation-policy)
  to torrent state, bounded payload access and private-swarm behavior.

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

- Tracker scheduling tests must distinguish a valid out-of-range integer
  from a wrong bencode type. The former is clamped; the latter preserves
  previous tracker state and enters the failure ladder. Audit sibling tests
  when strengthening a shared parser contract.
- HTTP compact peer strings and datagram tracker ranges require a multiple
  of the 32-byte destination hash width before updating peers or statistics.
  Bound the public datagram consumer as well as its wire caller; retain
  empty replies, peer caps and zero-hash filtering. Cover truncated tails
  after complete hashes and prior-state preservation in both transports.

- Peer IDs require successful RAND_bytes generation; repeated RNG seeds or
  clock fallback destroy per-torrent unlinkability. Failure clears the ID,
  leaves the torrent stopped, and is sticky through error resets/resume.
  HasPeerID guards HTTP/BEP 15, peer handshakes and DHT publication; magnet
  insertion and IsValid also reject failure. Recreate after provider recovery.
  The private per-object source seam tests partial/unsupported generation
  without replacing the global provider or changing unrelated randomness.
  TorrentError appends the failure value without renumbering earlier errors;
  RPC still maps any local error to its existing code 3 and typed control
  snapshots carry the error text, not the internal enum.

- Bencode callbacks return zero to request generic skipping, not to reject
  a known field. Metainfo handlers must also set the malformed error when
  a recognized field has the wrong type; otherwise a string file length
  can become a valid-looking zero-byte file. Track field presence separately
  from successful extraction for length/files exclusion. Cover valid integer
  zero-byte files and ignored unknown fields alongside negative cases.
  The existing 2048-byte tracker URL cap also applies to the primary
  announce field; this is a local resource policy, not a BEP wire limit.
  Apply the same distinction to BEP 10's `m` dictionary, integer extension
  IDs/metadata_size/reqq and byte-string v, and to typed tracker response
  fields. Record malformation before fallback and commit scratch state only
  after the complete document validates. Rejected handshakes do not consume
  the write-once slot; rejected tracker types retain prior peers/stats and
  advance the failure ladder. Unknown fields, zero extension IDs and unusable
  opaque tracker IDs keep their existing tolerant behavior.

- Metadata-only RPC fixtures can still enqueue disk-domain work through
  AddTorrent. Run or drain that executor as well as the destination service;
  a member executor with an owner-retaining task can prevent destruction.
  Keep both domains available through tunnel Stop and final persistence,
  join disk work before stopping the destination service, and distinguish
  these fixture leaks from ratchet-session ownership reports.

- After a bencode dictionary key is consumed, failure to parse its value
  must return failure immediately. Breaking into delimiter handling can
  mistake an e in value position for the dictionary terminator. Cover
  partially populated dictionaries, nested/list fallback and otherwise
  valid metainfo with dangling ignored keys; failed extraction is empty/0.

- DHT Start creates fresh callback/persistence authority; never clear a
  retired generation's stopping flag. All six timer callbacks lock their
  generation before entering the service. Lifecycle mutations remain on the
  owner executor or a quiescent service; a weak state is not permission to
  destroy routing fields concurrently with a handler.
- A periodic DHT save rechecks revocation after taking its generation's
  persistence mutex. Stop revokes first and takes that barrier before its
  final snapshot, preventing an admitted old writer from winning afterward.
  Normal tunnel shutdown also drains the disk executor before restart; keep
  that separate from direct DHT restart and final-writer ordering. The DHT
  protocol regression drives old and new queued saves through real delegates
  without network or filesystem access. Failed submission releases its
  single-flight claim, and old callbacks release only their own claim.
- A DHT node ID is usable only after successful provider randomness and
  routing-table construction. The tunnel discards an uninitialized DHT,
  leaving its capability and ports absent; Start must reject that state
  before persisted-state loading, receiver publication or timer scheduling.
  A saved self record must not silently bypass an RNG initialization failure.
  Preserve the existing identity format and never substitute fixed entropy.
  Provider-failure injection and no-advertisement validation remain CI work.
- `UploadSlotPolicy` grants eight interested peers and rotates on the first
  maintenance/interest event after 30 seconds. It preserves incumbents while
  filling vacancies, keys fairness by peer identity and resets on stop.
  Apply chokes before grants. Choke revokes disk publication and sends Fast
  rejections once while retaining load reservations until callbacks retire.
  This native fair policy does not reproduce Java's bandwidth/reciprocity
  ranking; keep that parity gap separate from wire compatibility.
  Include snapshot, policy and choke-preparation allocations in failure
  boundaries: an exception must revoke former grants before propagation.
  Fallback teardown walks the registry without allocating a new snapshot.
- Cold upload reads use `UploadLoadLedger` tickets before disk publication.
  Queued requests, loads and sends share peer capacity; canceled/removed
  tickets retain process-wide count/byte reservations until their last
  callback releases them. The budget state is shared with tickets, so its
  mutex outlives queued work. BEP 6 cancellation still sends a rejection;
  legacy requests while choked are ignored and Fast requests rejected because
  no upload Allowed Fast set is advertised. Slot choice uses the policy above.
  Cached-send state uses ScopeExit across allocation and stream submission;
  an exception must not leave a piece permanently marked as sending.
- `Piece::ReadVerifiedBlock` validates complete fragment coverage and hashes
  private scratch before replacing the cache under its mutex and epoch
  checks. Partial/canceled reads must never leave trusted shared bytes. Warm
  the existing cache only after full success to avoid a full-piece reread for
  every wire block. The upload scratch reservation is not a bound on all
  retained piece-cache memory; existing cache eviction remains relevant.
- Local verification uses weak tunnel ownership at every queued disk/service
  boundary. Compute selected completion on the service thread after disk
  persistence, never by reading mutable priorities on the disk worker. A
  canceled chain releases its atomic active-verification flag and leaves
  cursor/pending state available for a later explicitly admitted pass.
- Peer admission retires an idle registry entry before queuing `Close()`.
  Deferred teardown alone does not free capacity and lets bursts exceed the
  peer cap. Removal compares the current shared owner so an old close cannot
  erase a replacement with the same authenticated identity. The existing
  verify-parity regression exercises retirement without a running service.
- Completion snapshots file selection on the service thread and rechecks
  selection, verified pieces and exact torrent-instance membership before
  publishing. A same-hash replacement is a different authority. These checks
  do not cancel a filesystem operation already in progress; queued handlers
  must also avoid retaining the tunnel through its own stopped executors.

- Shared BEP 9 assembly remembers one competing metadata size. After
  progress stalls for a request interval, reserve a bounded opportunity
  for that offer even if the original-size peer renews first. Expire the
  preference if its advertiser disappears. This prevents timer ordering
  from pinning an unresponsive size; it does not authenticate a size
  before final info-hash verification. Keep live wire exchange pending.

- BEP 9 assembly is per torrent (`MetadataDownload`), not per connection.
  Keep the 8 MiB info-byte cap, 16 KiB blocks, one 30-second numeric-owner
  lease per peer, exact response lengths and matching negotiated total size.
  Only a complete SHA-1 match releases bytes for publication; a matching hash
  still requires one whole info dictionary. Disconnect releases that owner's
  lease; stop clears assembly on the service thread. `MarkRemoved()` may run
  on a control thread, so it must not mutate metadata assembly there: queued
  service-thread Stop performs that cleanup. Size disagreements use a bounded
  alternate preference after progress stalls. Hash failures clear
  state and delay another attempt by 30..300 seconds to bound reset abuse.
  The existing engine-interop target prepares multi-owner, exact-size,
  expiry/release, disagreement, hash recovery and stop regressions; peer-wire
  and sanitizer execution remain pending manual CI. Upstream `9b3e9b3` is
  adapted behavior, not an imported shared-buffer implementation.

- RPC `torrent-get` comment/creator fields use `GetMetaComment()` and
  `GetCreatedBy()` from metainfo, not peer comment/rating storage. Copy
  these views into owning JSON values on the torrent service thread.
  Keep missing metainfo fields empty, and exercise escaping and absence
  through the existing loopback RPC security regression.
  Imported bencode byte strings may be invalid display text: share
  `IsValidTorrentMetaText` with editing validation and omit invalid RPC
  text without modifying the original metainfo or infohash.

- `TorrentsRPCServer` callbacks own an internal state instead of borrowing
  the facade. Sessions weakly reference it, admission is capped at 16, and
  read/write operations carry 30-second cancellable deadlines. Stop closes
  admission before joining the service, then closes sockets and drains
  canceled completions before restart; stopping an io_context alone does not
  drain its handlers. Keep lifecycle methods on the control thread. Routes
  replace tunnel and authentication together under one mutex; HTTP dispatch
  must copy both in one snapshot, never in separate lookups. The existing
  `test-torrents-security` target prepares cap, teardown, destructor and
  restart regressions; execution remains pending manual CI. Synchronous
  bounded engine-service waits can still delay timer delivery and shutdown.
- `TorrentsTunnel` inherits `I2PService`; passing a null destination creates
  a local destination through the service base. `TorrentsDHT::BuildStateSnapshot`
  therefore writes the self `NodeInfo` first, then routing-table peers. A
  persistence regression must check both records and malformed-load retention,
  not compare the saved bytes only to the peer fixture.
- Tracker `peers` parsing is transactional: parse into a scratch set and
  commit only after the complete bencode document validates and the response
  carried no failure. A truncated document keeps the last known good list.

- A tracker response that carries both `failure reason` and `interval` keeps
  the failure retry ladder: schedule the next request only in the success tail
  of `ParseTrackerResponse`, whatever the field order.
- Read `CreateBitfield()`'s Boost tribool with `indeterminate()` plus an
  explicit `static_cast<bool>`; `!=`/`!` yield another tribool and hide an
  indeterminate state.

- Asio/Beast completion callbacks must keep the required parameter types and
  lifetime captures, but omit names for values their body does not consume.
  Preserve a transferred-byte count when it is forwarded to another handler;
  removing that name can turn a warning cleanup into an undeclared identifier.

- `TorrentFile` now owns `m_Mutex`: `Save`/`Load`, `Open`/`Close`,
  `Complete`, path/root updates and payload-authority replacement all
  serialize. Keep the locked `OpenLocked`/`CloseLocked` helpers private and
  never call the public wrappers from an already locked path.
  `m_LastAccessTime` is atomic for the inactivity sweeper. The lock is held
  across bounded storage-worker IPC (5 s send + 5 s receive per frame) plus
  the 30 s metadata read/transaction deadline, so service-thread callers such
  as `Complete()`, the control snapshot and the RPC file list can wait behind
  a stuck worker; that is bounded and cycle-free but is a known residual, not
  a guarantee.
  `TorrentsTunnel::InitTorrentFiles` still runs on the disk worker; per-file
  operations are
  serialized, but its remaining Torrent-level mutation is not yet split into a
  disk phase plus service-thread commit.
- `TorrentPayloadStorage` is a header-only interface implemented by
  `Torrents.cpp`; do not look for a separate implementation unit.
- Original info bytes, consent for `ut_comment`, private-swarm suppression and
  stopped/removed persistence guards are load-bearing. Pending writes are
  withheld from HAVE/bitfield/serving/promotion.
- SAF cancellation must not close another thread's descriptor; the worker owns
  the `AssetFileDescriptor`. Broker/process recovery stays generation-fenced.
- Isolated payload/router persistence is opt-in and not global separation.
  `SNARK-011`/`SNARK-012` remain `Implemented / static` or open as marked in
  `TODO.md`; live tracker/DHT/peer and device tests remain pending.
- DHT heard-from state is keyed by `NodeID`, while a pending query stores the
  destination peer `IdentHash`. A ping timeout must resolve the peer through
  `RoutingTable::FindNodeByPeer` before checking `m_HeardFrom`; the comparator
  is not transparent and peer hashes are not node IDs.
- Administrative `TorrentsTunnel` start/stop/reannounce methods post closures
  that capture `weak_from_this().lock()`, not raw `this`. Disk-worker file
  preparation still mutates session objects outside the service thread and
  remains tracked by CONC-005.
- `Piece::BlockReceived` reports whether the block was newly applied. Only an
  applied block may trigger completion, `VerifyHash`, disk persistence, HAVE
  publication or download accounting; duplicate/unsolicited blocks must not
  force a re-hash or `InvalidatePiece`. Keep the return value meaningful when
  adding callers.
- BEP 3 `cancel` (message id 8) removes a request only while it is still in
  `m_IncomingRequestsQueue`; an in-flight `SendPieceMsg` copy is not
  interrupted. The BEP 6 reject reply is gated on `m_IsFast` — a deliberate
  divergence from upstream, which answers unconditionally.
- `Torrent::m_Error` is atomic because `InitTorrentFiles` publishes
  `eTorrentErrorFilesystem` from the disk worker. Do not turn it into a plain
  field or report filesystem failures through a service-thread-only path. Every
  announce path (initial, periodic, start, reannounce) skips a torrent with a
  non-zero error; a later verified piece write clears a filesystem error. The
  add-time disk job and its announce continuation capture
  `weak_from_this().lock()`, never raw `this`.
- `DATAGRAM_TRACKER_DEFAULT_PORT` (6969) is applied when sending/connecting,
  never written back into `m_Trackers`; `IsTorrentTracker` compares the exact
  `URL::to_string()` identity of the stored announce URL.
- Java 2.14.0 persists unresolved magnet state in
  `SnarkManager.saveMagnetStatus()` and owns eight-uploader admission plus
  global/torrent/peer bandwidth queues (`PeerCoordinator`/
  `BandwidthManager`). Native unresolved magnets now use a separate bounded
  v1 `.magnet` record; peer-rate reporting still is not rate enforcement.
  Recovery scans pause authority before resolved metainfo and refuses
  automatic resolved recovery when that policy scan is incomplete. Retire
  fallback only after disk-domain success for both metainfo and settings,
  scoped to the same torrent instance. Never infer durability from a read.
  Keep advanced peer policy and dynamic recovery evidence open (`SNARK-012`);
  do not claim them from tracker `left`/`completed` parity alone.
- `PeerConnection::TorrentComplete()` owns end-of-torrent peer state: close
  seeds, reset stats, release outgoing requests and send *not interested*.
  `GetNextBlockToRequest` keeps interest while the peer still advertises
  wanted pieces, so a single in-flight block elsewhere does not clear it.
- `StopTorrent` closes every `TorrentFile` on the disk worker before
  `SaveTorrentResumeFile`; paused torrents must not hold payload/SAF handles
  until the inactivity sweep.
- Preserve the fork's requested-block admission checks for late torrent data:
  a block must match an active request epoch, aligned offset and exact expected
  length. Do not import upstream behavior that accepts unsolicited late
  blocks. Likewise, local verification remains piece-bounded and DHT
  transaction IDs use `RAND_bytes`, not a weaker convenience RNG.

- Tracker announces separate local selection from swarm accounting: `left` and
  `event=completed` come from `Torrent::GetAnnounceProgress ()` (whole torrent,
  completed only after full verification), while `IsComplete`/status/promotion
  stay selection-based. `MIN_TRACKER_ANNOUNCE_INTERVAL` (900 s) floors
  tracker-advertised intervals; `MAX_TRACKER_FAILURE_BACKOFF` (1800 s) caps the
  internal retry ladder, which is independent of the advertised interval.
  `TrackerStats` carries a ninth private field for the failure ladder.

- A stalled outbound block is released by `PEER_REQUEST_TIMEOUT` (60 s) before
  the 95 s blanket keep-alive release; the release carries the request's piece
  epoch so a recheck/reset block cannot be resurrected. Transfer counters charge
  the real block length (the final block is short), and idle peer rates decay
  (half after 30 s, zero after 60 s).
- Per-file progress uses complete+verified+not-saving, matching promotion and
  HAVE/serving withholding. A failed payload write sets the atomic
  `eTorrentErrorFilesystem` from the disk worker; only a later verified write
  clears it. Corrupt-peer charging is bounded and best-effort (last applied
  contributor, 64-entry in-memory table, three strikes, no persisted ban).
- Tracker dispatch preserves the failure ladder; HTTP announces are
  single-flight per `(torrent, slot)` (cap 16) with one 8 s write/read deadline,
  and registry slots are reference-counted/reused without renumbering. BEP 15
  stays outside the HTTP registry. BEP 12 ordered tiers remain open.
- A private-swarm discovery purges DHT peers, rendezvous state, tokens and
  outstanding queries; a bounded private-hash set refuses later inbound
  get_peers/announce_peer. `TORRENT-REMOVE-ALL` is the session-wide verb and
  `TORRENT-COMMENT-UNHIDE` must stay in both parser branches. Android's JNI
  allowlist must admit every verb its UI builds.
- `SNARK-012` tracks durable magnets, BEP 12, endgame/CANCEL, unchoke slots,
  bandwidth limits, per-file recheck and typed result/truncation work.

- DHT datagrams are network-thread input: repliable frames are queued and
  parsed on the service thread; raw frames are copied/posted by
  `TorrentsTunnel` and protected by a global 1024/60 s budget. Never mutate
  routing/query/token state on the network thread and never reply to malformed
  raw frames.
- Tokens are 8 CSPRNG bytes in a bounded FIFO with a 16-token per-node quota,
  bound to node/destination/info-hash/query-port and 600 s. Routing admits at
  most two node IDs per destination; DHT ports are 1..65534. Error replies mark
  the node failed; dual `values`+`nodes` replies consume both.
- `DecodeDHTFrame` is the single KRPC envelope authority: closed unique-key
  envelope, `t` 1..16, one `a`/`r`, optional BEP 5 `v`. PEX/BEP 10 helpers are
  pure; repeated handshakes are rejected, private torrents omit
  `metadata_size`, and seed classification requires a received bitfield.
- For compile-time ranges in Clang/libFuzzer translation units, use
  `constexpr std::array` for a fixed enum sequence. A `constexpr auto` deduced
  from a braced initializer list can fail because its initializer-list backing
  storage is a temporary subobject; `fuzz-torrents-extensions` exercises both
  PEX and DHT candidate origins this way.
- PEX/DHT candidates use the bounded per-torrent table (TTL/backoff/failures,
  100/60 s per origin) and are purged on stop/remove/private. Tracker peers are
  not budgeted. Registered but not executed: `test-torrents-dht-protocol`,
  `test-torrents-pex`, `test-torrents-tracker-schedule`,
  `fuzz-torrents-krpc`, `fuzz-torrents-extensions`.
- Open: BEP 5 announce `port`/`implied_port` (Java reference check), BEP 12
  tiers, differential Java/native fixtures, live churn and peer bans.

- Comments stay disabled by default and private torrents never exchange them.
  `filter` is an ignored 0..64-byte hint; TAB is normalized to a space, other
  C0/DEL/C1 controls are rejected. Dedupe uses a normalized owner/text key
  (whitespace collapse, zero-width removal) while storing the original text;
  ratings are immutable for a normalized pair. Anonymous entries have a
  separate 16-entry visible quota.
- A connection admits at most 40 new entries/60 s and a torrent 80/min;
  duplicates are free. Local operator adds bypass peer budgets but not store
  caps. A local change may refresh after a 10 s floor; the periodic cadence is
  600 s, and every content-changing hide/unhide/trim bumps the revision so the
  coalesced state writer saves. A future persisted timestamp clamps to the
  local clock instead of rejecting the state.
- `TORRENT-DETAIL <tunnel> <id> [offset] [limit]` pages comments (0..255 /
  1..16) and reports offset/count/total/visible/hidden/local/truncated; the
  default is the newest 16. Hidden text is retained and shown to the admin UI.

- SAF documents are probed at startup (`TorrentFile::ProbePayload`) and only
  exact-size present files are marked present; pieces fully backed by them are
  verified and pieces spanning missing/revoked documents stay downloadable.
  Never set all document files present or trust `.resume` for SAF. A POSIX
  final-name read-only handle must be closed/reopened writable after a hash
  failure forces re-download.
- All payload IO stays behind `TorrentPayloadStorage` (worker or SAF); no path
  fallback after selection. Zero-length files never open a document; no-seek
  providers and short writes fail closed without staging.

- Routine per-piece resume persistence is `UpdateTorrentResumeFile()` (120 s
  monotonic, injectable clock); stop, removal, completion and shutdown keep
  immediate `SaveTorrentResumeFile()`. A missing `.part` promotion sets
  `eTorrentErrorFilesystem`, invalidates covering pieces on the service thread
  and persists the corrected bitmap; never call `Complete()` on missing data.
- `CreateAvailableBitfield()` is a clamped union of established peers'
  advertised bitfields, exposed additively as RPC `piecesAvailable`; it is not
  local completion. A stopped datagram session clears its send queue.
- TUI torrent pause/resume and the footer use the exact `TorrentStatusToken`
  classifier. Never substring-match status; unknown sends no command.
- Do not adopt upstream connection replacement until `RemoveConnection` is
  identity-keyed, and do not port the intrusive receive queue without
  releasing the pooled source packet.

## Do not

- Do not compile, run CTest, sanitizers, fuzzers, Gradle or dynamic project
  tests locally. Compile-free structural checks required by `AGENTS.md` may
  run locally.
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
  `test-torrents-*`, `test-torrent-*` and SAF-related Android tests. The read
  verbs `TORRENT-SESSION`/`TORRENT-DHT` are queries; `TORRENT-ADD-FILE` is
  TUI-only and deliberately absent from the Android command switch because no
  Java caller builds it. Adding it requires a SAF import path, not a local
  path.
- Use `fuzz-torrents-parsers`/`fuzz-torrents-krpc`/`fuzz-torrents-extensions`
  for new parser surfaces. A new fuzzer needs a checked-in
  `tests/fuzz/corpus/<target>/` seed (<=64 KiB, non-secret), a per-target
  `-max_len`/`-artifact_prefix`, and registration in `tests/CMakeLists.txt`,
  `validation.yml` and `docs/testing.md`. The manual workflow uses a global
  `-max_len=65536`; the CTest per-target cap is authoritative for smoke.
- Every new test states its label, timeout (120 s; 300 s under sanitizers) and
  parallel-safety decision; forked sandbox/storage tests use `RUN_SERIAL`,
  fixed-`/tmp` tests the `openi2pd-fixed-tmp` lock, and host skips use
  `SKIP_RETURN_CODE 77`.
- Prepare negative cases for path traversal, symlink/hardlink, identity
  mismatch, private-torrent leakage, malformed DHT/PEX, provider denial and
  worker write failure.
- Include Linux/OpenBSD storage-worker tests, Android unit/instrumentation
  lanes and a controlled Java I2PSnark exchange where applicable.
- Record tests as prepared until the manual CI/device run passes.

- `ParseList` and `ParseDictionary` return the consumed byte count
  (`size_t`); only `ExtractByteString`/`ExtractInteger`/`ExtractDictionary`
  return a pair. Do not apply `.second` to the parser helpers.
- `i2p::util::Mapping` declares a move constructor, so its copy assignment is
  implicitly deleted unless explicitly defaulted. Datagram-option copies rely
  on both assignments; keep them defaulted when changing the class.

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

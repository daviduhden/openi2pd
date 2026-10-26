<!--
See the LICENSE file at the top of the project tree for copyright
and license details.

openi2pd: snark compatibility.

Documents project documentation.

-->

<!-- Copyright (c) 2026, David Uhden Collado -->
<!-- SPDX-License-Identifier: CC-BY-SA-4.0 -->
# I2PSnark compatibility inventory

Status: **implemented by static analysis where specified; pending CI
compilation and pending CI tests**. This is a source inventory, not a claim of
complete parity, live interoperability, or operating-system privilege separation.
No build, test, Java peer, Android device, public-network or sanitizer run
validates this revision.

## Reference and provenance

The current comparison target is official I2P Java `i2p-2.14.0`, peeled
commit `3c98503c5f14efe7bdd783528ba7f621f12d8081` (tag object
`9db233ed7241599eba76f29b8c334eaaf3db2e87`), verified from the official
remote tag refs with `git ls-remote`. Its Snark source area is
[`apps/i2psnark/java/src/org/klomp/snark`](https://github.com/i2p/i2p.i2p/tree/i2p-2.14.0/apps/i2psnark/java/src/org/klomp/snark).
The matrices and source findings below have not been re-audited against
2.14.0; no 2.14.0 compatibility claim follows from the tag pin.

The historical source audit used I2P Java `i2p-2.13.0`, commit
`9134f808337b401e8e53c73734c81fab04280c9d`, under
[`apps/i2psnark/java/src/org/klomp/snark`](https://github.com/i2p/i2p.i2p/tree/i2p-2.13.0/apps/i2psnark/java/src/org/klomp/snark).
`/tmp/i2pjava` initially contained transport/SAM excerpts, not a complete Snark
checkout. Missing reference files were fetched explicitly from that tag for
read-only source inspection. They are not distributed with openi2pd.

On 2026-10-19, master HEAD was `8416c8771d9c62c24ac753706cd1746ad24d7ff3`
(197 commits ahead of the historical 2.13.0 reference). The comparison API also
shows new experimental `v2/` files; their presence is not evidence of complete
BEP 52 support. `master` versions of `dht/KRPC.java`, `dht/NodeInfo.java` and
`MetaInfo.java` were also inspected. This comparison covers these files only;
it is not a full review of every post-release Snark change. NodeInfo's compact
format is unchanged. KRPC changes include sorted outgoing dictionaries,
set-based lookup bookkeeping and Datagram1 compression policy. C++ dictionary
encoding already sorts keys. The inspected 2.13.0 and historical master
MetaInfo parsers both require v1 piece hashes; that master snapshot still has
BEP 52 implementation TODOs.

Directory `COPYING` points to the GPLv2 license. Individual `Comment.java`
and `CommentSet.java` declare public domain; licensing must be checked per file.
Java Snark sources include GPLv2 notices and file-specific provenance, including
relicensed zzzot-derived DHT code. They must not be copied into BSD project
sources. This work uses independent C++ implementations and source-derived
behavioral descriptions. No Java implementation is embedded or translated.
Any future code import requires a separate file-level license/provenance review;
being a protocol reference does not grant a BSD relicensing right.

## Functional matrix

Java paths below are relative to the reference directory. C++ paths are relative
to the repository. “Present” means source exists and still requires manual CI.
“Pending” remains an open implementation obligation.

| Capability | Java reference | C++ / native reference | Current coverage and remaining work |
|---|---|---|---|
| Torrent/session administration | `SnarkManager`, `Snark`, `PeerCoordinator` | `TorrentsTunnel.*`, `TorrentsControl.*` | Present: add, start/stop, explicit pause/resume, remove, bulk operations, snapshots; advanced queue policy pending. |
| `.torrent` import | `MetaInfo`, `SnarkManager` | `Torrent::Parse*`, `ReadTorrentFileBounded`, tunnel AddFile control | Present: bounded input, metadata and file-layout validation; malformed corpus and Java exchange pending. |
| Magnet import/share | `MagnetURI`, `MagnetState`, `ExtensionHandler.handleMetadata` | `ParseTorrentMagnet`, `BuildTorrentMagnet`, torrent metadata extension handlers | Present: btih hex/base32, display name and tracker validation, BEP 9 metadata; complete interoperability pending. |
| Torrent creation | `Storage`, `MetaInfo` construction, manager creation operations | `TorrentsCreator.*`, `Torrent::CreateV1`, service creation jobs | Implemented by static analysis: anchored directory traversal, streaming cross-file SHA-1, canonical v1 metainfo, automatic/explicit piece size, tracker tiers/private flag, cancellation and byte/file progress. TUI n/N/K and Android private-root creation are connected. SAF creates and exports metainfo through native CreateV1; SAF source seeding is still pending. A public creation with no trackers now inherits the tunnel's common endpoints instead of setting an explicit empty list; a private creation without any tracker is rejected before hashing. |
| Trackers | `TrackerClient`, `SnarkManager`, `MetaInfo` | `TorrentsTunnel::AddTracker`, tracker request handlers, tracker snapshots | Multiple I2P trackers and announce lifecycle present; t/T add/remove paused-torrent trackers through typed IPC, preserving infohash and queuing persistence. Explicit lists override common trackers, including an explicitly empty list. A primary `announce` outside a non-empty `announce-list` is normalized into tier 0. Private torrents keep a fixed tracker list and reject admin mutation. Tracker snapshots expose the tier (`-1` common). Malformed, oversized, transport-failed and non-200 responses are failures with bounded error text and a short retry ladder capped at 30 minutes; a tracker-advertised `interval` is floored at 15 minutes and raised to a larger `min interval`, and a success without an interval re-bases at that floor. A dispatched request no longer overwrites the failure ladder with the advertised interval. HTTP announces are single-flight per `(torrent, slot)`, capped at 16 concurrent requests, and a dedicated timer closes a request that exceeds 8 seconds (the write and the response read share the admission). Tracker registry slots are reference counted, reused lowest-free-first and never renumbered, so removed torrents release capacity without moving another torrent's statistics; configured common slots are never recycled. Tracker peer lists expire after max(2*interval, 30 min), capped at two hours. Datagram tracker transactions avoid ID collisions and validate the local reply port. Native announces every eligible tracker, matching Java's practical behavior; BEP 12 failover and tier shuffle remain pending (do not change the default before a verified Java comparison). |
| Download and seed | `PeerState`, `PeerCoordinator`, `Storage` | `Torrent`, `Peer`, `TorrentsTunnel` | Native piece transfer, choke/request handling and seeding present. Accepted blocks refresh piece activity, avoiding the 22-second inactivity reset during a slow multi-megabyte piece, and each connection now releases its own outstanding block claims on choke, keep-alive reset, reject or disconnect instead of leaking them until the inactivity sweep. A per-request 60-second deadline releases a stalled outbound block before the 95-second blanket keep-alive release, so one unresponsive peer cannot pin the pipeline. Peer/torrent transfer counters charge the real block length (including a short final block), and an idle peer's reported rate decays instead of keeping its last burst. A hash mismatch charges the connection that completed the piece; three charges disconnect it (bounded best-effort accounting, not a persisted ban). Selected completion stops peer dialing after the wanted set is verified. Java peer exchange, unchoke/upload-slot policy, endgame/CANCEL send, peer blame and recovery tests remain pending. |
| Files and priorities | `Storage`, `PeerCoordinator` | `Torrent::SetFilePriority`, selected-byte accounting, `TorrentsState.*`, native controls | Implemented by static analysis: skip/normal/high, max selected priority for shared pieces, priority then rarity scheduling, dynamic refresh and persisted preferences. TUI f and Android dialogs connected. Local completion and promotion are selected-file based: a skipped file no longer prevents `Seeding`, selected files with every backing piece verified are promoted individually, and unselected `.part` files remain untouched. Per-file progress now uses the same complete+verified+not-saving rule as promotion, so a downloaded-but-unhashed or still-saving piece cannot report a finished file. Tracker `left` and `event=completed` are deliberately whole-torrent (Java `TrackerClient`): a partially selected torrent reports its missing bytes and never claims completion. `GetFullLeft()` supplies that accounting. Force recheck for a paused torrent is exposed as `TORRENT-RECHECK`/`V`/Android and hashes all pieces, including an incomplete bitmap-lost `.part`. Metainfo comment/created-by editing is exposed through `TORRENT-META-COMMENT`/`TORRENT-META-CREATED-BY` (TUI e/E, Android metadata dialog). A failed payload write now publishes `eTorrentErrorFilesystem` and suppresses announce/serving until a later verified write clears it. |
| Integrity and incomplete resume | `Storage`, `BitField` | torrent piece verification, torrent loading/storage domain | SHA-1 piece validation and existing-data checks present. Resume bitfields now require exactly `ceil(pieces/8)` bytes with zero padding; short/spare-bit files are discarded. Native resume replacement is atomic and removes a stale bitmap when no piece or every piece is complete. Startup hashes every piece fully backed by an existing final or `.part` file, so a mixed final/missing restart does not re-download intact files. Explicit recheck hashes all pieces, including incomplete `.part` ranges. Pending writes remain withheld from HAVE/serving. Crash/failure injection and partial-piece differential tests remain pending. |
| Statistics | `Peer`, `PeerCoordinator`, `Snark` | `TorrentInfo`, `TorrentPeerInfo`, `TorrentSessionInfo` | Torrent/peer progress, rates, transfer totals and state present; exact accounting semantics need differential review. |
| Limits | `BandwidthManager`, utility configuration, peer coordinator | engine/tunnel bounds and control facade | Resource limits exist; equivalent per-session/per-torrent bandwidth and connection configuration with UI persistence pending. |
| Errors and state | `SnarkManager`, `Storage`, configuration persistence | `TorrentsState.*`, admin state loading/writing and bounded control facade | Versioned local stopped/priorities/comments state uses immutable snapshots and atomic no-follow replacement; startup gates announces until settings load. Bulk controls now respect pending state. Crash/failure injection and full magnet/session queue recovery remain pending. |
| DHT | `dht/KRPC`, `DHTNodes`, `DHTTracker`, `NodeInfo`, `Token`, `MsgID` | `TorrentsDHT.*` | Global XOR nearest nodes, empty opaque token presence, seed/noseed filtering, raw reply-port binding, per-origin budgets, bounded attempts and immediate correlated KRPC errors are implemented by static analysis. The rendezvous path reuses local peers and advertises our destination when the torrent hash is in our own k-bucket. Queries from a full foreign bucket still receive replies; responses are capped at k nodes/64 fresh peer values; query timeouts are 60 s and an authenticated response clears prior failure state; incoming announce tokens are bound to the queried infohash and old tokens are evicted at capacity. Tokens now live in a bounded FIFO with a per-node quota (16) so one destination cannot flush every other token, and validation still binds node/port/infohash/age. Inbound repliable datagrams are copied into a bounded queue and parsed on the tunnel service thread; raw (sender-less) frames get a global 1024/60 s budget and malformed frames are never answered. The routing table admits at most two node IDs per destination, binds the persisted DHT port to 1..65534 so `rport` cannot wrap, and marks an errored node failed before advancing a lookup. The KRPC envelope decoder is side-effect free, rejects duplicate/unknown keys and truncated or mixed frames, tolerates BEP 5 `v`, and both `nodes` and `values` are consumed when a reply carries both. State is still loaded before the receiver is published and saved atomically or by the delegated worker, and a periodic 15-minute save was added. Private/stopped/removed suppression and self-unannounce remain. Metadata that reveals a private swarm now purges the served peers, our rendezvous identity, issued tokens and outstanding get_peers queries for that hash, and inbound get_peers/announce_peer for a locally private hash are refused; the bounded private-hash set keeps a removed private hash unservable. DHT state filenames are validated, loads are capped, and non-delegated saves use atomic replacement before the receiver is published. An `announce_peer` `port`/`implied_port` field is not interpreted locally (peer values are destination hashes) and our outgoing announce still omits it: that remains a Java-reference check before any change. Live Java, churn and OS-separated DHT persistence remain pending. |
| Peer exchange | `ExtensionHandler.handlePEX/sendPEX` | `Torrents.h` I2P extension constants, peer extension handlers | I2P 32-byte destination hashes, not public compact IP peers. Inbound PEX is limited to two messages and 100 candidates per 60 s per connection and now also per torrent; zero/duplicate destinations are dropped, `added` must be a multiple of 32 and the whole dictionary must parse. Outgoing PEX announces only established non-seed peers, is rate-limited to 120 s even on the handshake path, and no longer misclassifies a peer that has not sent a bitfield as a seed. Extension IDs are positive and write-once; a repeated BEP 10 handshake is rejected instead of re-triggering metadata/PEX/DHT advertisement, `metadata_size` is omitted for a valid private torrent, and a DHT hint requires an established authenticated identity with a bounded 60 s window (a present-but-unrelated `rport` is tolerated for interop). PEX/DHT candidates now enter a bounded per-torrent table (256 entries, PEX 30 min / DHT 3 h TTL, 30 s to 900 s backoff, four failures) with per-origin 100/60 s admission instead of being dialed statelessly; the table is purged on stop/remove/private transition. Decoders are side-effect free and prepared for fuzzing. Malformed-extension regressions and Java differential runs remain pending CI. |
| Comments/ratings | `ExtensionHandler.handleComment/sendCommentReq/locked_sendComments`, `comments/Comment`, `CommentSet` | `TorrentsComments.*`, `PeerConnection`, admin state and native controls | Opt-in negotiated ut_comment exchange, bounded request/reply/ingress budgets, age timestamps, duplicate suppression, stable local moderation IDs, rating statistics and persistence are implemented by static analysis. The continuation adds a normalized dedupe key (whitespace collapse and zero-width removal), a separate anonymous visible quota, per-connection 40/min and per-torrent 80/min admitted-entry budgets, revision-aware immediate refresh after a local change, TAB-to-space normalization and tolerant `filter` hints, a clamp instead of whole-state rejection for a future timestamp, and revised-on-change hide/trim persistence. Comment pages are exposed through `TORRENT-DETAIL [offset] [limit]` with visible/hidden/local counts, a continuation TUI page with `[`/`]`, confirmed hide/unhide, aged timestamps, bidi stripping and a persistence-error line, and Android paging with age/local markers. TUI commands remain English; Android strings ship in all four catalogs. Java peer/differential validation remains pending. |
| Desktop UI | Java servlet administration is reference behavior only | `tui/main.cpp`, `TuiProtocol.h`, shared daemon dispatcher | Native torrent list/details, creation with options/progress/cancel, force recheck, file priorities, comments/ratings/moderation and tracker administration use only restricted IPC. Detail views show selected size, privacy and tracker tier; private tracker edits are refused before a prompt. Session-wide removal is a confirmed typed verb (`Ctrl-X` keep data, `Ctrl-D` with data). Metainfo comment/created-by editing and Android remove-with-data are connected. Advanced queueing/bandwidth settings and interaction CI remain pending. |
| Android UI | Java desktop servlet is not reused | `TorrentsFragment`, `TorrentSafCreator`, service/ViewModel and JNI | Material administration includes magnets, SAF torrent import, priorities, tracker edits, metainfo comment/created-by editing, remove-with-data, session-wide removal, explicit start/stop for one torrent, comments, private-root creation and SAF document/tree metainfo creation/export with options/cancel/progress. The creation tracker validator no longer requires a not-yet-existing torrent id. The list/detail marks private swarms and uses selected size for completion; private tracker edits are blocked. No URI-to-path conversion. SAF tree payload download/seeding is connected through a bounded separate-process Binder/JNI backend. Existing provider bytes are rehashed; unsupported seek fails closed. Device/provider and lifecycle validation remains pending. |
| Android autonomous router | Router/session integration in Java distribution | Android foreground service and native daemon/JNI | Router/native library packaging and service ownership exist; Activity closure must not stop router/torrents. External-router mode requires an explicit compatible transport design. |
| Storage isolation | Java storage behavior supplies semantic reference only | `TorrentStorageWorker.*`, `privsep/TorrentStorage.*`, native sandbox profiles | A real exec-ed storage process is connected to TorrentFile payload reads/writes when isolatedstorage=true on Linux. OpenBSD uses a pre-pledge supervisor, inherited root descriptor and socket-only capability handoff; see storage-worker.md. Authenticated inherited IPC, delegated root FD, numeric handles, bounded transfers and negative-capability regressions are prepared. Allocation, promotion/removal and metainfo/resume/settings writes are delegated in isolated mode; isolated DHT persistence is delegated and four fixed router files have an opt-in worker; other router persistence remains; network/session/verifier split and Windows backend remain pending; Android SAF uses its own app-private Binder storage process. No global isolation claim. |
| Security and privacy | Protocol and local policy analysis | bencode bounds, I2P tracker validation, contained no-follow storage helpers | No clearnet fallback is authorized. TOCTOU, resource saturation, lifecycle and privacy tests remain open until manual CI verifies them. |

## Protocol interoperability matrix

| Wire surface | Reference / format | Native implementation | Compatibility boundary |
|---|---|---|---|
| BitTorrent v1 | BEP 3; `MetaInfo`, `PeerState` | 20-byte SHA-1 infohash and piece hashes | Supported target. Hash the original encoded info dictionary, preserve interoperable byte sequences. |
| v2 and hybrids | BEP 52; historical 2.13.0/master `MetaInfo` pieces check | v1 parser/creator | Pure v2 is unsupported. A hybrid is eligible only through a valid v1 layout and v1 hash; no claim of SHA-256 Merkle validation or v2 exchange. Unknown dictionaries must not silently become v2 support. |
| Extension handshake | BEP 10; `ExtensionHandler.getHandshake` | Native peer extension maps | Use negotiated remote message IDs, bounded dictionaries and per-torrent privacy restrictions. |
| Magnet metadata | BEP 9; `MagnetState`, `ExtensionHandler.handleMetadata` | Native metadata chunk exchange | Verify assembled metadata against requested v1 infohash before applying paths or allocating pieces. |
| I2P tracker replies | `TrackerClient` | tunnel HTTP/Datagram tracker handling | I2P destinations/hashes only, validate tracker URLs at every ingress; no public UDP DHT or DNS fallback. |
| `i2p_dht` extension | `ExtensionHandler.handleDHT/sendDHT` | native extension handlers and `TorrentsDHT` | Bencoded `port` and `rport`; query port 1..65534, reply port query+1. |
| DHT transport | `KRPC.sendQuery/sendResponse/sendAnnouncePeer` | `SendDatagram`, `SendRawDatagram` | Repliable I2P queries; raw replies and token-authorized raw announce. Not conventional Internet UDP Kademlia. |
| Compact DHT node | `NodeInfo.LENGTH`, constructors, `verify` | `NodeInfo`, `Node::VerifyID` | Exactly 54 bytes: NID20 + destination hash32 + network-order query port2. NID first4 match destination hash, next2 XOR port. |
| DHT transactions | `MsgID` | pending-query map | CSPRNG 8-byte opaque outgoing IDs, no collision replacement, exact echoed length; incoming query IDs bounded at16 bytes. |
| DHT tokens | `Token`, `KRPC.receiveGetPeers/receiveAnnouncePeer` | opaque `GetPeersToken` and incoming token map | Locally issued cryptographic random8; remote present tokens of 0..64 bytes retained exactly; absence is represented separately. |
| DHT peer results | `KRPC.receivePeers`, `DHTTracker` | DHT values parsing | Each peer is a 32-byte destination hash; packet/state bounds apply. Configured Java response sizes need boundary reconciliation. |
| `i2p_pex` | `ExtensionHandler.handlePEX/sendPEX` | native PEX extension | `added` is concatenated32-byte destination hashes. Java does not implement `added.f` or `dropped`; never substitute IPv4/IPv6 compact peers. |
| `ut_comment` | `ExtensionHandler` comment methods | `TorrentsComments.*` codec/store and PeerConnection exchange | Extension negotiation, request/reply dictionaries and unsigned author labels described below; no invented compatible-looking replacement. |

## DHT changes in this revision

The DHT pass replaces integer tokens with opaque bytes, uses cryptographic
randomness for generated tokens and transaction IDs, prevents transaction
collisions from replacing pending requests, and validates reply NID/destination/
query-port binding before consuming a pending request. Lookup replies also
match the selected node ID. Raw packets no longer mark arbitrary claimed node
IDs as heard-from before authorization. Announce tokens are checked for age
and issuing node identity; saturated token state refuses new grants.

Parsers require the entire dictionary, required IDs and transaction bounds,
exact compact-node framing, a required find-node target and bounded packets.
Invalid query ports and invalid persisted local node identities are rejected.
Prepared regression: `tests/test-torrents-dht-interop.cpp` exercises the compact
node vector, secure-ID binding, reserved ports and binary64-byte token retention.
It is not a network interoperability test.

Remaining DHT risks include routing policy under adversarial node churn,
live seed/noseed behavior, adversarial origin churn, persistence within the
current daemon and interoperability at the native 512-entry reply bound. Private
torrents must suppress DHT and PEX regardless of generic session settings.

## Comments and ratings: interoperable design boundary

Java negotiates `ut_comment`. A request has `msg_type=0`, optional `num`
(default20) and an outgoing64-byte `filter` placeholder. A reply has
`msg_type=1` and a `comments` list. Each entry contains `owner`, `text`, `like`
and `timestamp`. Timestamp is **age in seconds**, not Unix time. Java computes
local receive time minus nonnegative age (clamped to its 2005 epoch) and can
return up to 256 entries. Rating is clamped to0..5;0 can represent an unrated
comment. Association is the torrent's peer session, not a separate global
comment identifier.

`Comment` normalizes author labels to32 Java characters and comment text to512,
removes or replaces newlines, clamps future times and ratings, and records
local ownership separately. `CommentSet` bounds foreign entries at256 and
aggregate text at4096 Java characters, deduplicates entries, supports local
hiding/removal and tracks ratings. These are implementation behaviors, not
cryptographic author authentication. UTF-8 byte limits in C++ need an explicit
mapping to Java character bounds without splitting encoded characters.

The independent codec/store is integrated with PeerConnection negotiation,
periodic requests and bounded replies. It remains disabled by default and is
suppressed for private torrents. Duplicate detection normalizes ASCII
whitespace and strips U+200B/U+200C/U+200D/U+FEFF without rewriting the stored
text; ratings stay immutable for a normalized `(owner,text)`, and a separate
anonymous visible quota prevents an empty pseudonym from bypassing the
per-owner cap. `filter` is an ignored hint of 0..64 bytes, TAB is normalized to
a space, and other C0/DEL/C1 controls remain rejected for terminal safety.
Per-connection and per-torrent admitted-entry windows bound new content even
across reconnects, and a store revision allows one immediate refresh after a
local add/hide while retaining the 600 s cadence. A future persisted timestamp
is clamped to the local clock rather than rejecting the whole state file. `ut_comment` is omitted entirely when disabled
or private (never advertised as ID 0). A local add triggers a bounded
handshake/request instead of waiting for the next periodic cycle. Requests are
limited to one per 600 seconds, replies to one per 60 seconds; ingress is
limited to 8 messages/128 KiB per minute. The wire message cap is 16482 bytes
(Java's 16484-byte peer-message bound, excluding the type and extension ID),
and replies drop oldest selected entries until they fit. Replies publish
newest-first, may carry up to 256 entries, and skip a malformed entry without
discarding the rest. Content dedupe ignores the rating. Visible entries are
capped at 8 per non-empty pseudonym, hidden retention at 64 entries, and
capacity pressure evicts the oldest foreign visible entry rather than
permanently filling the store. CR/LF are
normalized to spaces; strict UTF-8/control validation otherwise remains.
Stable local IDs support hiding/unhiding while retaining duplicate suppression.
Native controls use explicit pseudonyms; no router, destination or device
identity is inferred. Local state is persisted separately from Java state and
peer-wire age timestamps.

The bencode encoder now retains empty strings as `0:` and sorts dictionary
keys, with duplicate-key rejection. This fixes rating-only/anonymous comments
and empty DHT tokens; original imported info bytes remain unchanged. Prepared
regressions include independent comment fields, empty owner/text, canonical
constructor output, malformed-entry isolation, newest-first selection, rating
dedupe, per-owner spam bounds, capacity eviction, unhide and TUI command
parsing. Publication, persistence and privacy behavior still require manual
Java/native interoperability validation.

## Required manual validation

Prepare and retain manual-only CI gates for bencode/magnet, creation/layout,
tracker policies, metadata identity, DHT/PEX wire vectors, piece corruption,
resume/crash recovery, comments, IPC authorization/limits, symlink/path attacks,
JNI and Android SAF/service lifecycle. Extend fuzz targets for nested bencode,
metadata extensions and DHT/comment packets; use ASan/UBSan/TSan where supported.

Re-audit Snark against the pinned 2.14.0 source, then run controlled
Java 2.14.0/C++ exchanges with no public-network dependency. Historical
2.13.0 fixtures retain their original provenance. Cover:
trackerless metadata/download, DHT get/announce/retry, PEX-only discovery,
private-torrent suppression, seed behavior, malformed messages and future
comment fixtures. Native platforms require manual Linux/OpenBSD/Windows and
Android checks, including LibreSSL and negative worker-capability tests.
Earlier revision results do not validate these source changes. No task above
is “validated by CI” until a corresponding dispatched run passes.

## Current increment and remaining boundaries

The 2026-10-19 continuation began from clean `0cf32519`. Implementations already
in that checkout were reused rather than repeated. New work remains local,
pending CI compilation and pending CI tests. Earlier matrices understated the
existing creator, selection and comment-exchange code; this inventory now
reflects the real native interfaces.

SAF creation/export remains independent of payload authority. A selected download
tree supplies native piece reads/writes through Binder and JNI with exact bounded
positional IO, multi-file fragments and one-piece-per-disk-job startup
verification. A private resume bitfield is read but every restored piece is
rehashed before publication; large-torrent startup no longer blocks the disk
worker on an inline tree scan. Zero-length files never open a document. Providers denying
seek/write are rejected without staging. Read/write validate the persisted grant,
document ID and exact size before each transfer, so truncation or same-length
replacement fails closed; write mode may extend a shorter existing document
without trusting its bytes. Grant URIs are reference-counted across torrents, a
new selection is resolved on the next open, and cached native handles are closed
when the tree generation changes. Export grows progressively and commits only at
the declared exact size; failed/cancelled exports truncate to zero. Provider
timeouts or Binder death still trigger bounded, generation-fenced recovery.
Full cross-process grant/death/remapping and device provider-fault validation
remain open. See [SAF storage](saf-storage.md).

Per-file native promotion, complete bandwidth/queue controls, BEP 12 failover
and full multiprocess capability separation remain open. Tracker-slot
reclamation is implemented by reference counting; see the tracker row. No current change is described as dynamically validated.

The recovery increment additionally moves isolated native metainfo, resume and
settings writes plus promotion/removal into the storage worker. This reduces
parent authority without changing I2P wire or bencode formats. Router/DHT
persistence and a genuine network/core/verifier split still block the global
no-network-plus-persistent-write invariant.

Current additions: isolated DHT persistence preserves its binary format and
legacy flat names through explicit authorization; fixed router state has an
opt-in storage worker; SAF exact deletion and brokered cancelable export are
connected. Tests for these paths are prepared. Essential global process and
persistent-authority separation still needs implementation, not only CI.

The peer-discovery continuation adds bounded DHT responses, valid-query replies for full buckets, hash-bound/evicting tokens, longer query lifetimes, failure clearing, safe/atomic DHT state names and loads, tracker failure backoff/peer expiry/datagram transaction binding, and PEX/DHT hint receive budgets with write-once extension IDs. It remains I2P-only: no public DHT, clearnet DNS or clearnet peer/DHT fallback is introduced.

The 2026-10-21 piece/resume/selection continuation adds exact resume-bitfield
validation, atomic bitmap replacement, mixed final/`.part` startup verification,
all-piece explicit recheck, selected-file completion/promotion, selected tracker
`left`, per-connection request release, primary `announce` tier normalization,
private tracker immutability and public/private creation tracker policy. It
changes no peer-wire or Java-compatible metainfo format; original `info` bytes
and infohashes remain authoritative. Endgame, persisted/blame-based peer bans, BEP 12
failover, durable magnets and nested-root creation remain open. No dynamic validation is claimed.

The 2026-10-26 scheduler/tracker/privacy continuation changes no peer-wire or
Java-compatible metainfo format. It adds: per-request stall expiry, real block
length accounting, idle rate decay, verified-only per-file progress,
filesystem-error publication on failed writes, and bounded best-effort
corrupt-peer charging; single-flight 8-second HTTP tracker deadlines, a
failure-ladder-preserving dispatch decision and reference-counted tracker
slots; private-hash DHT purging and inbound refusal; and typed
`TORRENT-REMOVE-ALL` with TUI confirmation, Android session removal and
single-torrent start/stop, plus the missing `COMMENT-UNHIDE` parse branch and
the Android creation-tracker validator. Prepared regressions are
`test-torrents-scheduler`, `test-torrents-tracker-dispatch` and the extended
`test-tui-protocol`; none was compiled or executed. Durable magnet/session
recovery, BEP 12 failover, unchoke/upload-slot policy, endgame/CANCEL send,
per-file or per-piece recheck and bandwidth limits remain open. No live Java
peer, tracker, DHT, PEX, device or CI run exists for this increment.


The 2026-10-26 DHT/PEX continuation adds no peer-wire or persisted-format change
except the stricter KRPC envelope (unknown/duplicate top-level keys are
rejected and optional BEP 5 `v` is accepted) and the local candidate table. It
adds the service-thread datagram queue, token store with per-node quota,
per-peer routing cap, raw-frame budget, periodic DHT save, port clamp,
error-reply failure marking, dual `nodes`/`values` parsing, handshake replay
rejection, private `metadata_size` suppression, seed-flag tracking, tolerant
`rport`, per-torrent PEX/DHT budgets and the peer candidate table. Prepared
regressions are `test-torrents-dht-protocol`, `test-torrents-pex`,
`fuzz-torrents-krpc` and `fuzz-torrents-extensions`; none was compiled or
executed. BEP 5 announce `port`/`implied_port`, BEP 12 ordered tiers,
Java/native differential exchanges, live churn and OS-separated DHT persistence
remain open. No public DHT, DNS or clearnet fallback exists.

## Differential validation design (prepared, not executed)

The DHT/PEX changes need two distinct evidence tiers; parser vectors alone are
not an interoperability claim.

- **Tier A — offline parser vectors.** Feed the same bencoded bytes to
  `DecodeDHTFrame`, `DecodeCompactNodes`, `DecodeCompactPeers`,
  `DecodePEXAdded` and `DecodeDHTHint`, and on the Java side to the pinned
  `i2p-2.14.0` `dht/KRPC`, `NodeInfo`/`Token` and `ExtensionHandler` parsers.
  Compare accept/reject plus the extracted transaction, query, node, token and
  peer fields. Fixtures must be synthetic (random 20/32-byte hashes, random
  tokens, `*.i2p` names), stored with a manifest and generator revision, and
  must never contain real identities, keys or destinations.
- **Tier B — loopback I2P exchange.** Run one openi2pd router with a torrents
  tunnel bound to a private destination and attach a Java 2.14.0 harness to the
  same local router through loopback I2CP/SAM. Exchange KRPC get_peers,
  announce_peer, responses and `i2p_pex`/`i2p_dht` extension frames. No public
  DHT, DNS or clearnet socket is involved. Report results only as "observed
  against a local Java 2.14.0 client on a private router", never as
  public-network interoperability.

The open questions this must answer before any wire change: whether Java
requires `port`/`implied_port` in `announce_peer`, whether it tolerates the
closed KRPC envelope and our always-present `noseed`, whether its `rport` in
`i2p_dht` follows `port`, and whether it treats a handshake without
`metadata_size` as valid. Until then, the prepared C++ tests and fuzzers pin
only our parser behavior and must not be described as Java validation.


The 2026-10-26 comment continuation changes no wire format: request/reply
keys, `msg_type`, `num`, `filter`, `comments`, `owner`, `text`, `like` and
`timestamp`-as-age are unchanged. It adds local-only paging fields and
admission/normalization behavior; none of it is a Java interoperability claim.
Prepared regressions are the extended `test-torrents-comments`,
`test-torrents-state` and `test-tui-protocol`; they have not been compiled or
executed.

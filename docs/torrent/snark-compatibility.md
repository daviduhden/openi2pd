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
| Torrent creation | `Storage`, `MetaInfo` construction, manager creation operations | `TorrentsCreator.*`, `Torrent::CreateV1`, service creation jobs | Implemented by static analysis: anchored directory traversal, streaming cross-file SHA-1, canonical v1 metainfo, automatic/explicit piece size, tracker tiers/private flag, cancellation and byte/file progress. TUI n/N/K and Android private-root creation are connected. SAF creates and exports metainfo through native CreateV1; SAF source seeding is still pending. |
| Trackers | `TrackerClient`, `SnarkManager`, `MetaInfo` | `TorrentsTunnel::AddTracker`, tracker request handlers, tracker snapshots | Multiple I2P trackers and announce lifecycle present; t/T add/remove paused-torrent trackers through typed IPC, preserving infohash and queuing persistence. Explicit lists override common trackers, including an explicitly empty list. BEP 12 tier failover policy and global registry quota reclamation remain pending. |
| Download and seed | `PeerState`, `PeerCoordinator`, `Storage` | `Torrent`, `Peer`, `TorrentsTunnel` | Native piece transfer, choke/request handling and seeding present; Java peer exchange and recovery tests pending. |
| Files and priorities | `Storage`, `PeerCoordinator` | `Torrent::SetFilePriority`, selected-byte accounting, `TorrentsState.*`, native controls | Implemented by static analysis: skip/normal/high, max selected priority for shared pieces, priority then rarity scheduling, dynamic refresh and persisted preferences. TUI f and Android dialogs connected. Full payload verification remains mandatory; individually completed selected files may retain .part names until whole-torrent promotion. |
| Integrity and incomplete resume | `Storage`, `BitField` | torrent piece verification, torrent loading/storage domain | SHA-1 piece validation and existing-data checks present; interrupted writes, crash recovery and partial-piece differential tests pending. |
| Statistics | `Peer`, `PeerCoordinator`, `Snark` | `TorrentInfo`, `TorrentPeerInfo`, `TorrentSessionInfo` | Torrent/peer progress, rates, transfer totals and state present; exact accounting semantics need differential review. |
| Limits | `BandwidthManager`, utility configuration, peer coordinator | engine/tunnel bounds and control facade | Resource limits exist; equivalent per-session/per-torrent bandwidth and connection configuration with UI persistence pending. |
| Errors and state | `SnarkManager`, `Storage`, configuration persistence | `TorrentsState.*`, admin state loading/writing and bounded control facade | Versioned local stopped/priorities/comments state uses immutable snapshots and atomic no-follow replacement; startup gates announces until settings load. Bulk controls now respect pending state. Crash/failure injection and full magnet/session queue recovery remain pending. |
| DHT | `dht/KRPC`, `DHTNodes`, `DHTTracker`, `NodeInfo`, `Token`, `MsgID` | `TorrentsDHT.*` | Global XOR nearest nodes, empty opaque token presence, seed/noseed filtering, raw reply-port binding, per-origin budgets, bounded attempts and immediate correlated KRPC errors are implemented by static analysis. Live Java, adversarial churn, seed outgoing connection policy and OS-separated DHT persistence remain pending. |
| Peer exchange | `ExtensionHandler.handlePEX/sendPEX` | `Torrents.h` I2P extension constants, peer extension handlers | I2P 32-byte destination hashes, not public compact IP peers. Peer/private-torrent policy and malformed-extension regressions pending CI. |
| Comments/ratings | `ExtensionHandler.handleComment/sendCommentReq/locked_sendComments`, `comments/Comment`, `CommentSet` | `TorrentsComments.*`, `PeerConnection`, admin state and native controls | Opt-in negotiated ut_comment exchange, bounded request/reply/ingress budgets, age timestamps, duplicate suppression, stable local moderation IDs, rating statistics and persistence are implemented by static analysis. TUI Comments/c/C/H and Android controls connected; private swarms suppress exchange. Java peer/differential validation remains pending. |
| Desktop UI | Java servlet administration is reference behavior only | `tui/main.cpp`, `TuiProtocol.h`, shared daemon dispatcher | Native torrent list/details, creation with options/progress/cancel, file priorities, comments/ratings/moderation and tracker administration use only restricted IPC. Advanced queueing/bandwidth settings and interaction CI remain pending. |
| Android UI | Java desktop servlet is not reused | `TorrentsFragment`, `TorrentSafCreator`, service/ViewModel and JNI | Material administration includes magnets, SAF torrent import, priorities, tracker edits, comments, private-root creation and SAF document/tree metainfo creation/export with options/cancel/progress. No URI-to-path conversion. SAF tree payload download/seeding is connected through a bounded separate-process Binder/JNI backend. Existing provider bytes are rehashed; unsupported seek fails closed. Device/provider and lifecycle validation remains pending. |
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
local receive time minus nonnegative age. Rating is clamped to0..5;0 can
represent an unrated comment. Association is the torrent's peer session,
not a separate global comment identifier.

`Comment` normalizes author labels to32 Java characters and comment text to512,
removes or replaces newlines, clamps future times and ratings, and records
local ownership separately. `CommentSet` bounds foreign entries at256 and
aggregate text at4096 Java characters, deduplicates entries, supports local
hiding/removal and tracks ratings. These are implementation behaviors, not
cryptographic author authentication. UTF-8 byte limits in C++ need an explicit
mapping to Java character bounds without splitting encoded characters.

The independent codec/store is now integrated with PeerConnection negotiation,
periodic requests and bounded replies. It remains disabled by default and is
suppressed for private torrents. Requests are limited to one per 600 seconds,
replies to one per 60 seconds; ingress is limited to 8 messages/128 KiB per
minute and 60 KiB per message. Stable local IDs support hiding while retaining
duplicate suppression. Native controls use explicit pseudonyms; no router,
destination or device identity is inferred. Local state is persisted separately
from Java state and peer-wire age timestamps.

The bencode encoder now retains empty strings as `0:` and sorts dictionary
keys, with duplicate-key rejection. This fixes rating-only/anonymous comments
and empty DHT tokens; original imported info bytes remain unchanged. Prepared
regressions include independent comment fields, empty owner/text and canonical
constructor output. Publication, persistence and privacy behavior still require
manual Java/native interoperability validation.

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
tree now supplies native piece reads/writes through Binder and JNI, with exact
bounded positional IO and startup rehashing. Providers denying seek/write are
rejected without staging; provider timeouts or Binder death now trigger bounded, generation-fenced
recovery without an app restart. Grants and priorities persist, operation handles do not. Delete-files removal now deletes exact authorized provider documents without
recursive tree deletion. Cancellation tokens, independent retirement and provider fixtures are prepared;
full cross-process grant/death/remapping validation remains open. See [SAF storage](saf-storage.md).

Per-file native promotion, complete bandwidth/queue controls, BEP 12 failover,
tracker-slot reclamation and full multiprocess capability separation remain
open. No current change is described as dynamically validated.

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

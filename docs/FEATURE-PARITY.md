<!--
See the LICENSE file at the top of the project tree for copyright
and license details.

openi2pd: FEATURE-PARITY.

Documents project documentation.

-->

# I2P Java functional parity matrix

This document records the functional comparison between the official I2P Java
router and openi2pd. It complements [`INTEROPERABILITY.md`](INTEROPERABILITY.md):
that document records wire-level findings and vectors, while this one records
the broader user-visible and administrative feature set.

## Reference baseline

| Item | Value |
|---|---|
| Repository | <https://github.com/i2p/i2p.i2p> |
| Current target tag | `i2p-2.14.0` |
| Target commit (peeled tag) | `3c98503c5f14efe7bdd783528ba7f621f12d8081` |
| Tag object | `9db233ed7241599eba76f29b8c334eaaf3db2e87` |
| Pin verification | Official remote tag refs inspected; a read-only 2026-10-31 source audit covers router/netDb/transports, client/admin/address book/reseed, I2PSnark and Android; dynamic Java/native differentials remain pending |
| Historical audit baseline | `i2p-2.13.0`, commit `9134f808337b401e8e53c73734c81fab04280c9d` |
| Reference use | Read-only external clone; no Java source is vendored, built or shipped |
| Historical audited C++ tree | `bce8771b` plus the parity increment recorded at that audit |

Older matrices and findings retain their historical Java 2.13.0 evidence.
Read-only 2.14.0 reviews on 2026-10-21 and 2026-10-31 checked the target areas
listed in §18 against pinned source and specifications. Tag existence and
source inspection do not establish feature parity or dynamic interoperability;
unreviewed feature families and all runtime differentials remain tracked under
`NET-026`/`PARITY-008`.

The Java reference is used to understand behavior and specification
compliance, not as an architectural template. No Java code, JVM or Java
runtime dependency is introduced into the daemon, TUI, tools or Android
application.

## Status vocabulary

| Status | Meaning |
|---|---|
| **Compatible, equivalent** | Wire/behavior evidence is the same by source and specification inspection. |
| **Compatible, implementation different** | Different structure or presentation, same interoperable result. |
| **Partial** | The common path exists; a named sub-case is absent or divergent. |
| **Missing** | The feature has no equivalent in openi2pd. |
| **Implemented, not validated** | Source exists, but no current CI/device/differential run has passed. |
| **Architecturally unnecessary** | Java behavior belongs to its web/JVM application model, not to a native router requirement. |
| **Incompatible with fork goals** | Would require a web admin console, Java runtime, global privilege regression or another excluded design. |

`Implemented, not validated` is not a success claim. All rows in the current
increment remain static until the relevant manual CI workflow and, where
applicable, a device or Java-peer run succeeds.

## 1. Router, identity, transports and netDb

| Feature | Java reference | openi2pd implementation | Status | Differences, gaps and interoperability | Files / tests | Priority |
|---|---|---|---|---|---|---|
| Router lifecycle and identity | `router/java/src/net/i2p/router/Router.java`, `RouterContext.java`, `data/RouterIdentity.java` | `daemon/Daemon.*`, `libi2pd/RouterContext.*`, `Identity.*` | Compatible, implementation different | Native lifecycle and lock model; config/state paths intentionally differ. No functional wire divergence found. | Router lifecycle tests; manual daemon tests pending | P0 |
| NTCP2 handshake and data phase | `transport/ntcp2/*`, `NTCP2Payload.java` | `libi2pd/NTCP2.*` | Compatible, implementation different | Replay, SessionConfirmed block ordering, size and termination queueing were aligned in earlier passes. Live PQ/non-PQ handshake still pending. | `test-ntcp2-*`; Java peer test pending | P0 |
| SSU2 handshake, tokens, relay and peer test | `router/java/src/net/i2p/router/transport/udp/*` | `libi2pd/SSU2*.cpp` | Compatible, implementation different | Bounded fragment/receive handling and relay framing exist. OpenBSD live MTU/IPv6 and relay tests remain pending. | SSU2 tests; OpenBSD/peer test pending | P0 |
| SSU2 connection migration | `PeerState2.java` (`MAX_PATH_CHALLENGE_SENDS`, `MAX_PATH_CHALLENGE_TIME`, path state) | `libi2pd/SSU2Session.*` | Implemented, not validated | Added bounded 5/10/20 s retransmission, 30 s deadline, same-family check and old-endpoint retention. A lost challenge no longer terminates the session. | `test-ssu2-preauth`; loss/path test pending | P0 |
| SSU2 flood request flag | `PeerState2.gotRI`, `NTCP2Payload` | `SSU2Session.cpp` RouterInfo handling | Implemented, not validated | A newly accepted/replaced RouterInfo with the request-flood bit is forwarded only by a floodfill and only within the per-key FloodThrottler budget; stale replays are not re-flooded. Amplification and malformed-block tests remain. | SSU2 RouterInfo tests pending | P1 |
| PQ transport variants | `UDPTransport.addSSU2Options`, ML-KEM classes | `PostQuantum.*`, `SSU2*.cpp` | Implemented, not validated | Runtime provider probing now gates publication and key creation instead of a compile-time constant. Cross-provider and Java PQ session vectors are absent. | `test-crypto-x25519-noise-elligator-mlkem-audit`; Java PQ test pending | P0 |
| RouterInfo parsing and validation | `data/router/RouterInfo.java`, `RouterAddress.java` | `libi2pd/RouterInfo.*` | Compatible, implementation different | Strict bounded parser; 4 KiB Java ceiling aligned. Fuzz and large-RI fixture execution pending. | `test-protocol-bounds`; fuzz pending | P0 |
| NetDb store/lookup/floodfill | `networkdb/kademlia/*`, `database/DatabaseStoreMessage.java` | `libi2pd/NetDb.*`, `KadDHT.*` | Partial | Replacement, freshness, DLM/DSRM, clove origin, admission caps and persistence were hardened. Direct-transport garlic lookup is answered while tunnel-delivered lookup is dropped; token-bearing DatabaseStore keeps its token for direct/direct-garlic and is stored without ack for tunnel garlic. Non-floodfills no longer re-flood transport RIs; inbound queues and new-RI maps are capped; cached RouterInfos use durable atomic, filename-bound persistence. Java go-away RouterInfo, pdrop admission, per-use stale-RI refresh, and the `router.floodfillParticipant=auto` policy remain absent; local `floodfill` is an explicit boolean. | `test-tunnel-dsm-policy`; go-away/pdrop/refresh/floodfill-policy vectors pending | P0 |
| Reseeding and SU3 | `util/SU3File.java`, `networkdb/reseed/*` | `Reseed.cpp`, `Config.cpp` | Implemented, not validated | `reseed.verify` now defaults to true; RSA 2048/3072/4096 and Ed25519/RedDSA SU3 verification paths were added with fail-closed unsupported handling. A real signed bundle and Java cross-vector are pending. | `test-su3-bounds`; signed-bundle test pending | P0 |
| LeaseSet, LS2, EncryptedLeaseSet, MetaLeaseSet | `core/.../LeaseSet.java`, `LeaseSet2.java` | `libi2pd/LeaseSet.*`, `Blinding.cpp` | Partial | Parsing, expiry, encrypted store key and Meta precedence exist; MetaLeaseSet now rejects non-canonical header expiry and revocations (Java would fail the signature), and an older encrypted-LS reply cannot roll back the client cache. Secret-blinded authorization, client-side Meta resolution and a Java-generated encrypted-LS corpus remain absent. | LeaseSet tests; Java vector pending | P0 |
| Tunnels and tunnel builds | `router/.../tunnel/*` | `libi2pd/Tunnel*`, `TransitTunnel.*` | Compatible, implementation different | Short/long build and transit paths exist. No live tunnel-build interoperability run has been recorded. | Tunnel tests; live build pending | P1 |
| Garlic and I2NP | `core/.../data/i2np/*`, `router/.../message/*` | `I2NPProtocol.*`, `Garlic.cpp` | Compatible, implementation different | Bounded serialization and NS/NSR validation. Malformed corpus and live Garlic routing remain pending. | `test-protocol-bounds`; fuzz pending | P0 |
| Peer selection and profiles | `peermanager/*`, `Profiling.*` | `libi2pd/Profiling.*`, tunnel pools | Partial | Selection policy differs by design; exact Java profile export/import and peer-ban administration are not user-facing equivalents. | Profile tests; admin tests pending | P2 |

## 2. Cryptography and identities

| Feature | Java reference | openi2pd implementation | Status | Differences, gaps and interoperability | Files / tests | Priority |
|---|---|---|---|---|---|---|
| DSA/ECDSA/EdDSA/RedDSA signatures | `core/.../crypto/SigType.java`, `DSAEngine.java` | `Identity.*`, `Signature.*` | Compatible, implementation different | Types 0–3 and 7/8 have native verifiers/signers. GOST 9/10 is an openi2pd extension not in stable Java. | Signature tests; Java vectors partial | P0 |
| RSA signature verification (types 4/5/6) | `SigType.java` 4–6; `DSAEngine` | `RSAVerifier`, `Identity.cpp`, `RouterInfo.cpp` | Implemented, not validated | Raw PKCS#1 v1.5 digest verification and key-certificate layout are implemented; validated legacy RouterInfo signatures are accepted instead of rejected. RSA signing/key generation is explicitly unavailable; LeaseSet destinations still reject RSA to match Java, and LS2 must never advertise RSA. | `test-signature-identity-hardening`; Java RSA key/RouterInfo vector pending | P1 |
| Ed25519ph/type 8 | `SigType.java`, `DSAEngine.altSign` | `Signature.*` | Implemented, not validated | Plain Ed25519 over supplied bytes is used to match Java; a Java-generated type-8 vector is still required. | Existing EdDSA tests | P1 |
| ECIES-X25519-AEAD-Ratchet | `crypto/ecies/*`, `RatchetSKM.java`, `RatchetTagSet.java` | `ECIESX25519AEADRatchetSession.*`, `Garlic.cpp` | Partial | NS/NSR payload pre-validation, replay cache, tag bounds and multi-key LeaseSet selection exist. Secret-blinded I2CP, NSR retransmission and Java NS/NSR vectors remain open. | Ratchet tests; Java vectors pending | P0 |
| ML-KEM key handling | proposal 169 / `crypto/mlkem` | `PostQuantum.*`, `Crypto.h` | Implemented, not validated | Runtime provider probe caches support for ML-KEM-512/768/1024 and refuses unsupported key creation. No current CI provider matrix or Java PQ session exists. | ML-KEM audit test; CI provider matrix pending | P0 |
| Elligator and blinding | `Elligator2.java`, `BlindedPublicKey.java` | `Elligator.cpp`, `Blinding.cpp` | Compatible, implementation different | Boundary checks and b33 derivation exist. Case-preserving address handling was added in this increment. | Blinding/Elligator tests | P1 |
| Identity/key certificates | `data/Identity.java`, `PrivateKeyFile.java` | `Identity.*`, `Signature.*` | Partial | DSA/ECDSA/Ed25519 and RSA verification layouts are parsed. RSA private-key generation and some legacy certificate combinations remain unsupported. | Identity tests; malformed corpus pending | P1 |

## 3. Client services and application protocols

| Feature | Java reference | openi2pd implementation | Status | Differences, gaps and interoperability | Files / tests | Priority |
|---|---|---|---|---|---|---|
| I2CP | `core/.../client/I2CP*.java` | `libi2pd_client/I2CP.*` | Partial | Message and LeaseSet flows exist; routing-session cache keys now use the parsed identity hash for every signature type. Secret-blinded/type-key authorization and live Java client tests remain. | `test-i2cp-routing-session-key`, lifecycle tests; live I2CP pending | P0 |
| SAM 3.x | `apps/sam/java/src/net/i2p/sam/*` | `libi2pd_client/SAM.*` | Implemented, not validated | Persistent bounded framing, pipelined commands, split SEND payloads, one-word PING/PONG/HELP/QUIT, quoted parameter values, case-insensitive keys and repeated-space command words were added. SAM 3.3 `LISTEN_PROTOCOL` routing, `(type,protocol,port)` duplicate keys and protocol-aware RAW dispatch are implemented statically. | `test-sam-control-framing`, `test-sam-subsessions`, datagram/version tests; live SAM client pending | P1 |
| Streaming | `router/.../client/streaming/*` | `Streaming.*` | Partial | SYN/window/reset and MTU bounds exist. Live stream and malformed-packet interoperability are pending. | Streaming tests; live test pending | P1 |
| Datagram1/2/3 | `core/.../client/Datagram*.java` | `Datagram.*` | Partial | Repliable/raw, ports, auth and zero-length options are implemented. Datagram2 offline-signature Java vectors and per-session version selection remain. | Datagram tests; Java vector pending | P1 |
| Client and server tunnels | `apps/i2ptunnel/java/src/net/i2p/i2ptunnel/*` | `libi2pd_client/ClientContext.*`, `I2PTunnel.*` | Partial | Native client/server tunnel configuration exists; TUI/admin coverage is still narrower than Java's web editing. | TUI reload/admin tests; live tunnel test pending | P1 |
| HTTP proxy | `apps/i2ptunnel/java/src/net/i2p/i2ptunnel/HTTPProxy.java` | `libi2pd_client/HTTPProxy.*` | Compatible, implementation different | Native proxy; header lifetime/compaction fixes were retained. No browser-embedded console. | HTTP tests; live proxy pending | P2 |
| SOCKS proxy | `apps/i2ptunnel/java/src/net/i2p/i2ptunnel/SOCKS*.java` | `libi2pd_client/SOCKS.*` | Compatible, implementation different | Native implementation. Authentication/edge cases need live differential tests. | SOCKS tests pending | P2 |
| Address book, .i2p and b32/b33 | `apps/addressbook/java/src/net/i2p/addressbook/*`; [Proposal 112](https://i2p.net/en/proposals/112-addressbook-subscription-feed-commands/) | `libi2pd_client/AddressBook.*` | Partial | Native TUI and Android support bounded entry and subscription-source administration. Case handling, tombstones and origin-scoped feed deletion exist, but the feed parser handles only `action=delete` and does not verify the required `sig`/`oldsig`; signed add/change/subdomain commands and Java host-entry signature semantics are absent. Preserve origin checks until spec-compliant cryptographic verification and migration tests exist. | `test-addressbook-case`, `test-addressbook-admin`; signed Proposal 112 corpus pending | P0 |
| Naming and destination lookup | `client/NamingService.java`, `LookupDestJob.java` | `AddressBook.*`, `Destination.*` | Partial | Local and remote lookup paths exist; secret-blinded and some HostReply option combinations remain fail-closed. | AddressBook/Destination tests | P1 |
| I2PControl and local APIs | `apps/i2pcontrol/java/net/i2p/i2pcontrol/*` | `daemon/I2PControl*`, TUI IPC | Partial | Native JSON-RPC/control compatibility is retained, but not every Java console action has a TUI/Android equivalent. Generic config writes are deliberately excluded. | Control tests; parity matrix below | P2 |

## 4. I2PSnark and BitTorrent

| Feature | Java reference | openi2pd implementation | Status | Differences, gaps and interoperability | Files / tests | Priority |
|---|---|---|---|---|---|---|
| Metainfo, magnets and creation | `apps/i2psnark/.../Torrent.java`, `CreateTorrent.java` | `TorrentsCreator.*`, `Torrents.*` | Implemented, not validated | Bounded v1 creation, piece hashing, tiers, privacy, cancellation/progress and magnet parsing exist. Pure v2 is unsupported; hybrid validation is not claimed. | Creator/magnet tests; Java torrent exchange pending | P0 |
| Metainfo rewrite and editing | `I2PSnarkServlet.displayTorrentEdit/saveTorrentEdit`, `MetaInfo.java` | `Torrents.*`, `TorrentsTunnel.*`, TUI/Android | Implemented, not validated | Comment, created-by, creation date, url-list, encoding and unknown top-level keys survive tracker/metadata rewrites; comment and created-by are editable on a paused torrent through bounded TUI IPC and Android. Over-bound extras fail the rewrite instead of truncating. | `test-torrents-engine-interop`, TUI/Android command tests; CI pending | P1 |
| Piece storage, .part and resume | `Piece`, `Storage`, `Snark` resume handling | `Torrents.*`, `TorrentPayloadStorage.*` | Implemented, not validated | Resume/final-name data is now re-hashed before availability; pending writes are withheld; failures invalidate the piece. Real restart/crash and SAF provider tests remain. | `test-torrents-verify-parity`, storage lifecycle pending | P0 |
| Trackers, tiers and private swarms | `Snark` tracker classes, BEP 3/12 | `TorrentsTunnel.*`, `Torrents.*` | Implemented, not validated | Both compact and list-of-dicts peer responses, per-entry rejection, peer caps, min-interval/tracker-id, percent-encoded peer id and primary-only private announce were added. Tracker edits preserve top-level metainfo such as comment, created-by, creation date and url-list. Live Java tracker test pending. | `test-torrents-verify-parity`; live tracker pending | P0 |
| DHT and PEX | `apps/i2psnark/.../DHT*`, `PeerCoordinator` | `TorrentsDHT.*`, `Torrents.*` | Partial | DHT KRPC/token/bounds and 32-byte PEX exist; PEX now sends inbound/outbound with bounded tracking. The own-bucket rendezvous path reuses local peers and publishes our destination with seed/private/cap guards. DHT source authenticity and live Java trackerless exchange remain open. | DHT/PEX tests; live test pending | P0 |
| Peer connection limits | Java connection-manager/peer limits | `Torrents.*`, `TorrentsTunnel.*` | Implemented, not validated | Per-torrent/global peer and dial caps, bounded accepts and idle rotation were added. No live exhaustion test exists. | `test-torrents-verify-parity`; stress test pending | P1 |
| Comments, ratings and metadata | `apps/i2psnark/.../Comments*`, `ut_comment` | `TorrentsComments.*` | Implemented, not validated | Opt-in negotiated exchange; empty bencode strings and private suppression retained. Java peer exchange and consent tests remain. | Comment codec tests; Java peer pending | P2 |
| Native TUI/Android control | Java web UI (functional equivalent only) | `TuiProtocol`, `tui/*`, Android torrent fragment | Partial | Native typed control includes force recheck, metainfo comment/created-by editing, selected-file verification/promotion and Android remove-with-data, with commands retained on the torrent service thread. Durable queue recovery, uploader-slot/bandwidth controls and crash recovery remain incomplete. | TUI/IPC/Android tests; device test pending | P0 |

## 5. Administration, TUI and Android

Java console behavior is separated from its JSP/HTML/JavaScript presentation. The
replacement is native terminal and Android UI over bounded local IPC/JNI.

| Operation | Java reference | openi2pd surface | Status | Notes / gaps | Priority |
|---|---|---|---|---|---|
| Router status and diagnostics | `apps/routerconsole/.../RouterConsoleRunner`, status pages | TUI Status/Connections; Android Dashboard/Network/Statistics | Compatible, implementation different | Read-only snapshots are bounded. Advanced Java graph/profile pages are summarized rather than copied. | P1 |
| Logs | `apps/routerconsole/.../LogsHelper`, log pages | TUI Logs; Android Logs/settings | Compatible, implementation different | Native log queue and levels. Android validates the closed set and applies the saved level to the running logger on the service worker; TUI has ADMIN LOGLEVEL. | P1 |
| Transit toggle, bandwidth, share | `RouterConsoleRunner`, `configui` | TUI ADMIN TRANSIT/BANDWIDTH/SHARE; Android Runtime admin | Implemented, not validated | Both surfaces send closed typed operations through the daemon queue; Android uses the service worker and dedicated JNI calls. These operations change live state and do not write preferences/configuration. Dynamic Android and daemon checks pending (`PARITY-022`). | P1 |
| Reseed | Java console reseed action and reseed status | TUI ADMIN RESEED; Android Reseed NetDb action | Implemented, not validated | Single-flight NetDb reseed; no URL/path input. Android calls the service worker and shows a bounded result. Signed bundle/device test pending. | P0 |
| Tunnel reload | `I2PTunnel` config reload | TUI ADMIN RELOAD TUNNELS; Android Reload tunnels | Implemented, not validated | Reuses `ClientContext::ReloadConfig`; no arbitrary file path. | P1 |
| Shutdown/graceful drain | Java console shutdown | TUI ADMIN SHUTDOWN GRACEFUL/IMMEDIATE/CANCEL | Implemented, not validated | Typed modes only; immediate shutdown requires typed confirmation. Signal/IPC paths remain daemon-owned. | P0 |
| Router-wide configuration | Java config UI | TUI read-only snapshot; settings files/Android preferences | Compatible, implementation different | There is deliberately no generic TUI config writer. Android validates and persists a closed set. | P1 |
| Client/server tunnel editing | Java I2PTunnel web editor | TUI Destinations/reload; Android Tunnels | Partial | Existing configuration can be reloaded; full native tunnel creation/editing UI is missing. | P1 |
| Address book editing | Java addressbook UI | TUI Destinations; Android Address book screen | Implemented, not validated | List/add/replace/remove, deleted-host block/unblock and subscription source list/add/remove use bounded local TUI IPC or typed app-internal JNI methods, full base64 identities only, and do not delete key material. Automatic merges never re-point a known name (Java `Daemon.java` precedence). | P1 |
| Peer/netDb browsing and bans | Java netDb/peers pages, `ConfigPeerHandler` | TUI NetDb (`b`/`u`), Android Runtime admin, `BLOCKLIST` | Implemented, not validated | Bounded read-only views plus startup operator blocklist counters and an in-memory runtime router ban set (1024 entries, cleared on restart, enforced through `IsRouterBanned`). TUI and Android expose typed ban/unban actions; the detailed blocklist view and per-router profile diagnostics remain narrower. | P2 |
| Client services | Java I2PTunnel/proxy/SAM status pages | TUI Services (`v`); Android Runtime admin | Compatible, implementation different | Bounded read-only inventory of TCP/UDP client and server tunnels, HTTP/SOCKS proxies and SAM/BOB/I2CP servers; public b32 destinations only, no keys or paths. Typed per-tunnel start/stop is implemented; full create/edit/restart remains absent. | P2 |
| I2PSnark operations | Java Snark UI | TUI Torrents + Android native panel | Partial | Pause/start/recheck/priorities/comments/tracker operations; force recheck requires a paused torrent and hashes all pieces. Selected-file completion drives local Seeding status and per-file `.part` promotion, while tracker `left`/`event=completed` are whole-torrent (Java `TrackerClient`); mixed final/`.part` restarts verify only fully backed pieces and resume bitfields require exact padding. Private swarms are labeled and use a fixed primary tracker. Metainfo comment/created-by editing and Android remove-with-data are exposed. Java's durable unresolved-magnet queue, eight-uploader policy, and global/per-torrent/per-peer bandwidth queues remain missing. | P0 |
| Themes/accessibility | Java web themes | TUI themes; Android Material 3 light/dark | Compatible, implementation different | Native focus, screen-reader labels and responsive layouts exist; device validation pending. | P2 |
| Web console, JSP, HTML, CSS, JS | Java router console | None | Incompatible with fork goals | Intentionally removed. No administrative web technology is reintroduced. | — |

## 6. Security, privilege separation and resource limits

| Requirement | Java reference | openi2pd implementation | Status | Differences, gaps and risks | Files / tests | Priority |
|---|---|---|---|---|---|---|
| Log worker isolation | Java logging model | `privsep/LogWorker.*`, supervisor | Implemented, not validated | Opt-in separate process/supervisor. Queue quota and live failure tests remain. | Privsep tests; CI pending | P1 |
| Torrent storage worker | Java local filesystem | `privsep/StorageWorker.*`, `TorrentStorageWorker.*` | Implemented, not validated | Opt-in isolated payload/metadata operations, bounded IPC, no parent-IO fallback. Windows backend and provider death tests remain. | Storage-worker tests; CI/device pending | P0 |
| Router fixed-file persistence | Java persistence | `privsep/RouterStorage.*`, `PersistentStorage.*` | Implemented, not validated | Four fixed router files delegated; netDb/config/profile/client authorities remain in the network-capable parent. | Router-storage test; CI pending | P0 |
| Network/router/verifier processes | Java separate processes/threads | Not implemented | Missing | The daemon still combines network, long-term keys and most persistence. Do not claim global privilege separation. | Architecture work; negative capability tests pending | P0 |
| pledge/unveil | Java sandbox is JVM-specific | OpenBSD daemon/privsep code | Partial | OpenBSD startup/worker policies exist; router-storage post-pledge spawn path must be revalidated. | OpenBSD CI; policy tests pending | P0 |
| Untrusted-input bounds | Java parsers | All native parsers | Partial | Fixed buffers, caps and fuzz targets exist. Sanitizer/fuzz execution has not run for this tree. | Fuzz/sanitizer CI pending | P0 |
| Anonymity restrictions | Java router policy | Router/tunnel/DHT code | Compatible, implementation different | No web console, no cleartext torrent path and no external proxy fallback introduced. Live anonymity testing is inherently limited. | Security review; live test pending | P0 |
| Android process/SAF boundary | Java Android app | Binder/AIDL broker, JNI | Partial | Separate broker and scoped grants exist; device death/revocation and network separation remain unvalidated. | Instrumentation/device CI pending | P0 |

## 7. What the current increment changed

Implemented by static analysis in this increment; pending CI compilation and
pending CI tests:

- SSU2 migration retransmission/deadline and old-address retention.
- Runtime ML-KEM provider probing and fail-closed key creation.
- Reseed verification enabled by default, RSA/Ed25519 SU3 verification and
  RSA identity verification.
- SAM persistent bounded framing, pipelined commands, one-word control grammar
  and case-insensitive option keys.
- I2CP routing-session keys based on the parsed identity hash.
- Address-book case folding with preserved b33 labels.
- Torrent resume/final-name re-verification, tracker parsing/limits, private
  swarm announce selection, PEX, per-torrent peer IDs and connection caps.
- A typed daemon-owned TUI admin queue for reseed, shutdown, reload, bandwidth,
  share, transit and log level.
- Android Reseed NetDb and runtime log-level apply, using a confirmed menu
  action, the service worker, a closed native set and no main-thread JNI.
- Deleted-host tombstones with bounded wildcard matching, atomic persistence,
  TUI `ADDRESS-BLOCK`/`ADDRESS-UNBLOCK` and Android block/unblock dialogs.
- I2PSnark metainfo preservation and comment/created-by editing through the
  paused-torrent control path, plus Android remove-with-data.

No compiler, CTest, sanitizer, fuzzer, Java peer, service manager, Android
device, OpenBSD, Windows or live-network run has validated these changes.

## 8. Remaining implementation order

1. Validate Android address-book/subscription, tunnel and runtime-admin flows,
   including the newly added live transit/bandwidth/share JNI path, with
   manual Gradle/JNI/Lint/device CI.
2. Close controlled interoperability gaps: Java PQ/non-PQ handshakes, signed
   SU3 bundle, RSA key vector, SAM `LISTEN_PROTOCOL` differential, I2CP
   secret-blinded lookup and Datagram2 offline vector.
3. Finish torrent operations: durable magnet queue, uploader-slot and
   bandwidth scheduling, Windows storage and worker recovery; metainfo edit
   and Android remove-with-data are in the current static increment.
4. Move network/router/verifier responsibilities out of the network-capable
   parent before making any global privilege-separation claim.
5. Add dynamic TUI/admin, Android device, OpenBSD/LibreSSL, Windows and
   sanitizer/fuzz evidence through manual GitHub Actions.

## 9. Non-goals

- Reimplementing JSP/HTML/CSS/JavaScript router-console presentation.
- Embedding a JVM, Java runtime, browser engine or Node process.
- Copying Java internal threading, class structure or accidental bugs.
- Weakening signature, AEAD, path, grant or untrusted-input validation to make a
  test pass.
- Claiming protocol or feature equivalence from source inspection alone.

## 10. Scoped 2.14.0 audit backlog (2026-10-21)

These candidates were identified by read-only comparison with the pinned Java
tree. They are appropriate native work, but none is implemented by the current
increment; they remain `PARITY-014` in `TODO.md`.

| Area | Candidate | Current status | Priority |
|---|---|---|---|
| Router/netDb | Per-requester DatabaseLookup and per-key flood throttles, direct exploratory/ANY rejection from floodfills, bounded stale-RouterInfo refresh | Partially implemented / static: the floodfill 14 / non-floodfill 3 budget, non-floodfill EXPL+ANY drop, transport-RI flood admission and 54/75-minute freshness predicates are implemented; per-use expiry on selection, probabilistic store admission and go-away RI remain | P1 |
| Router/reseed | Proposal 147 `?netid=` on SU3 URLs and multi-source/minimum-RouterInfo acceptance | Implemented / static for `netid` plus the two-server/100-RI threshold; no dynamic validation | P2 |
| Router/security | Read-only startup IP/CIDR and router-hash blocklist with bounded parser and existing transport/netDb enforcement points | Implemented / static: `blocklist.txt` parser, atomic snapshot, SSU2/NTCP2 admission, netDb store/selection and `IsRouterBanned`; in-memory runtime bans; no GeoIP/country blocking and no reload watcher | P1 |
| Client tunnels | Server-tunnel `filterDefinition` parser, thresholds, file lists, recorders and reset rejection; openi2pd currently has only an inline whitelist | Missing | P1 |
| Streaming | Minute/hour/day and total incoming limits, destination-hash counting after signature validation, `limitAction` drop/reset/HTTP/custom | Partially implemented / static: per-peer and total minute/hour/day windows (`StreamingConnectionThrottle`); `limitAction` reset/HTTP/custom still pending | P1 |
| SAM | `LISTEN_PROTOCOL`, protocol+port RAW/DATAGRAM receiver dispatch, per-subsession datagram version, duplicate rejection and receiver reset on `SESSION REMOVE` | Implemented / static: protocol-aware RAW dispatch, `(STYLE,protocol,port)` duplicate keying and reset; the per-subsession datagram version and the 2.14.0 per-style listen protocol are corrected in §16, and the Java differential remains pending | P1 |
| Proxies | Local HTTP 407 Basic/Digest and SOCKS5 username/password authentication with constant-time checks and no credential forwarding | Implemented / static for Basic and SOCKS5 RFC 1929; HTTP Digest remains pending reference verification | P1 |
| Administration | Read-only peer profile diagnostics and client/server tunnel inventory; per-tunnel runtime start/stop; Android live router controls | Partial: subscription-source editing, router ban/unban, per-tunnel start/stop and live Android transit/bandwidth/share controls are implemented statically; profile diagnostics remain narrower | P2 |
| I2PSnark | Selected-file promotion, uploader-slot allocation and per-session/per-torrent bandwidth limits | Partial: selection/promotion and tracker `left`/`completed` and announce scheduling match Java statically; uploader slots and session rate limits remain | P0/P1 |

The Java console's JSP/HTML/JavaScript, console authentication, plugins,
update downloads, generic configuration writer, keyring/family certificate
editing and restart actions remain non-goals or service-manager concerns as
recorded in §9 and §5.

## 11. User-services continuation (2026-10-21, static)

| Area | Candidate | Current status | Tracking |
|---|---|---|---|
| HTTP proxy | Case-insensitive `.i2p`/`.i2p.alt` routing, `.i2p.alt` lookup alias, loopback/private clearnet outproxy refusal | Implemented / static | `HTTP-101`, `AB-101` |
| SOCKS | IP-literal ATYP conversion, case/underflow-safe I2P suffix, port-0/private target rules | Implemented / static | `SOCKS-101` |
| Address book | subscription 5 MiB/entry caps, `www.` fallback, tombstone enforcement on lookup/jump insert | Implemented / static | `AB-102..AB-105` |
| Reseed | TLS chain/hostname verification, 8 MiB SU3 / 1 MiB entry / 400-entry bounds, network bundle freshness | Implemented / static | `RS-102..RS-106` |
| Client/server tunnels | Reload applies changed section policy, server TLS verification, inventory/start-stop, HTTP response parser limits | Missing / Partial | `PARITY-014/015` |
| Administration | Read-only client/SOCKS tunnel inventory, per-tunnel start/stop, Android tunnel/address replace UI | Missing / Partial | `PARITY-014/015` |

These changes do not add a browser/web console. HTTP/SOCKS outproxy traffic,
I2P-only resolution and reseed transport authentication remain the privacy
boundaries; no service silently falls back to clearnet for a name classified as
I2P. All rows remain pending manual CI compilation/tests; no live proxy,
reseed, Android or Java peer evidence exists.

## 12. Full-equivalence program (2026-10-25)

The goal is functional equivalence with the pinned Java 2.14.0 target, not
class/threading identity and not a claim based on source inspection. Each
workstream is implemented only from the referenced Java source, keeps the
native architecture and wire compatibility, and lands with prepared native
tests. Dynamic equivalence requires the manual differential harness and is
never inferred from this document.

Reference root: `i2p/i2p.i2p` tag `i2p-2.14.0`, commit
`3c98503c5f14efe7bdd783528ba7f621f12d8081`. Local Java trees are never
vendored or executed by agents.

| Order | Workstream | Java reference | Native surface | Acceptance evidence |
|---|---|---|---|---|
| 1 | Streaming admission: per-peer/total minute/hour/day counters and `limitAction` reset/drop/HTTP/custom | `apps/streaming/java/.../impl/ConnThrottler.java`, `ConnectionManager.java`, `ConnectionOptions.java` | `libi2pd/Streaming.*`, `Destination.*`, `ClientContext.cpp` | Counters implemented statically (`StreamingConnectionThrottle`); rejection packet bytes for each action and the Java/native differential run remain pending |
| 2 | SAM 3.3 `LISTEN_PROTOCOL`, protocol/port RAW/DATAGRAM dispatch, per-subsession datagram version, duplicate rejection, receiver reset on `SESSION REMOVE` | `apps/sam/java/src/net/i2p/sam/*` | `libi2pd_client/SAM.*`, `libi2pd/Datagram.*` | Protocol-aware RAW/DATAGRAM dispatch, per-style 17/19/20 keying and per-send datagram framing implemented statically with `test-sam-subsessions`/`test-datagram-version-dispatch` (§16); Java differential run pending |
| 3 | Local proxy authentication: HTTP 407 Basic/Digest, SOCKS5 username/password, constant-time comparison, no credential forwarding | `apps/i2ptunnel/java/src/net/i2p/i2ptunnel/I2PTunnelHTTPClient*.java`, SOCKS sources | `libi2pd_client/HTTPProxy.*`, `SOCKS.*` | Basic/SOCKS5 parsers, constant-time checks, fail-closed config and no-header-forward tests implemented (`test-proxy-auth`); Digest and Java differential run pending |
| 4 | Server-tunnel `filterDefinition`: files, explicit rules, thresholds, recorders, reset action | `apps/i2ptunnel/java/src/net/i2p/i2ptunnel/access/*` | `libi2pd_client/ClientContext.*`, `I2PTunnel.*` | Parser/file fixtures, reset/accept vectors, Java tunnel differential run |
| 5 | Operator IP/CIDR and router-hash blocklist with bounded parsing and transport/netDb enforcement | `router/java/.../Router.java`, `Banlist.java`, `NetworkDatabaseFacade.java` | `libi2pd/Config.*`, `Transports.*`, `NetDb.*` | Parser bounds, enforcement-point tests, no reload race; live Java differential run |
| 6 | NetDb per-requester/per-key admission and bounded stale-RouterInfo refresh | `router/java/.../networkdb/*` | `libi2pd/NetDb.*` | Throttle/refresh vectors; floodfill differential run |
| 7 | Per-tunnel inventory/start-stop and Android live controls | `apps/routerconsole/java/...`, `apps/i2ptunnel/java/...` | TUI IPC, `daemon/TuiServer*`, Android JNI/UI | Protocol round-trips, start/stop lifecycle tests, device run |
| 8 | I2PSnark durable magnet queue, uploader slots and bandwidth scheduling | `apps/i2psnark/java/src/org/klomp/snark/*` | `libi2pd_client/Torrents*`, storage worker, TUI/Android | Selected-file verification/promotion is implemented statically; remaining acceptance needs restart/recovery, queue/priority/bandwidth vectors and Java Snark differential runs |
| 9 | Network/router/verifier process separation and remaining persistence authority | JVM-specific; I2P Java is the behavioural reference only | `privsep/**`, `daemon/**` | Negative-capability tests, crash/restart, Linux/OpenBSD/Windows runtime evidence |

Cross-cutting rules:

- A workstream is not "done" with a partial wire action. The Streaming
  counters are tracked as a partial implementation while `limitAction` and its
  rejection bytes remain open; SAM is not complete without receiver reset on
  `SESSION REMOVE`.
- Every workstream adds its native regression before manual CI dispatch; a
  differential harness result is recorded in `PROGRESS.md` with the CI run ID.
- No equivalence claim is made for a workstream until its manual Java/native
  differential run passes. Source comparison is evidence for implementation,
  not for equivalence.
- The PARITY-014 backlog and TEST-024 static-audit residuals remain the
  tracking entry points.

## 13. 2.14.0 administration, router and client-service audit (2026-10-25, static)

Four read-only audits compared the native tree with the pinned Java 2.14.0
sources (raw files only; nothing vendored or executed). The findings below are
implementation evidence, not dynamic equivalence. Every row remains
`Pending CI compilation` and `Pending CI tests`.

| Area | Candidate | Java reference | Native status after this increment | Tracking |
|---|---|---|---|---|
| Router/security | Startup operator blocklist (IPv4 address/range/CIDR, IPv6 literal with `;`, base64 router hash), bounded parser, immutable snapshot, transport/netDb enforcement | `Blocklist.java`; `Router.java` startup order | Implemented / static | `PARITY-016` |
| Router/security | In-memory runtime router ban (until restart) consulted by `IsRouterBanned` | `Banlist.java` soft/forever classes | Implemented / static (subset: permanent bans only) | `PARITY-016` |
| Router/netDb | Floodfill 14 vs non-floodfill 3 lookup budget; drop ANY and EXPL on a non-floodfill before charging the throttle | `LookupThrottler.java`; `FloodfillDatabaseLookupMessageHandler.java` | Implemented / static | `PARITY-016` |
| Router/netDb | Go-away RouterInfo on store ack / lookup replies, probabilistic store admission (`LIMIT_ROUTERS`/pdrop), per-use stale-RouterInfo refresh, `router.floodfillParticipant` alias | `HandleFloodfillDatabaseStoreMessageJob`, `KademliaNetworkDatabaseFacade` | Partial: deterministic caps and the 54/75-minute predicates are implemented; pdrop, go-away and per-use expiry remain | `PARITY-020`/`DB-127`/`NET-176` |
| Client services | Server-tunnel `filterDefinition` access semantics (thresholds, explicit/file elements, recorders, reset/drop) and the pre-accept reject seam shared with Streaming `limitAction` | `access/*`, `ConnectionManager.java` | Implemented / static in §17 (parser/evaluator, admission predicate, drop/reset; `limitAction` http/custom still open) | `PARITY-017` |
| Client services | HTTP proxy Digest authentication | `I2PTunnelHTTPClientBase.java` | Implemented / static in §17 (RFC 7616 MD5/SHA-256, opt-in; Basic remains default) | `PARITY-017` |
| Client services | `targetForPort`, `spoofedHost.<port>`, `rejectInproxy`/`rejectReferer`/`rejectUserAgents`, POST throttling, `startOnLoad`, per-tunnel start/stop | `I2PTunnel*.java` | Implemented / static in §17 (typed TUI/Android start/stop; no create/edit/restart verb) | `PARITY-019`/`PARITY-021` |
| Address book | Subscription merge never re-points an already-known name; bounded conflict accounting | `Daemon.java` merge path | Implemented / static | `SEC-028` |
| Address book | Subscription source list/add/remove with validation and atomic `subscriptions.txt` rewrite | `SubscriptionsBean.java` (functional equivalent) | Implemented / static (typed TUI and Android controls) | `PARITY-018` |
| I2PSnark | Whole-torrent tracker `left`, `event=completed` only after every piece is verified, 15-minute advertised interval floor, 30-minute failure cap | `TrackerClient.java` | Implemented / static | `SNARK-011` |
| I2PSnark | Session up/down rate limits, uploader slots/rotation, durable magnets, peer blame/CANCEL | `BandwidthManager.java`, `PeerCoordinator.java`, `MagnetState.java` | Missing — deferred (engine work) | `SNARK-011` |
| Administration | Bounded `BLOCKLIST`, `ADDRESSBOOK-SUBSCRIPTIONS` reads and typed `NETDB-BAN`/`NETDB-UNBAN`/`ADDRESS-SUB-ADD`/`ADDRESS-SUB-REMOVE` writes on the daemon-owned queue | Router console functional equivalent | Implemented / static (TUI and Android typed JNI); the full `BLOCKLIST` listing remains TUI-only | `PARITY-018` |

Deliberate decisions recorded by this increment:

- The blocklist matches router hashes unconditionally but matches addresses
  only when the published address itself is listed; an RI whose *every*
  published address is blocked is rejected at store time, while an RI with one
  blocked address is still usable through its other transports.
- The blocklist is a startup input, not a live-edited store. Runtime bans are
  memory-only, bounded to 1024 entries and cleared by restart, matching the
  Java "ban until restart" semantics without inventing a persisted format.
- Hostnames in a blocklist file are skipped, never resolved: no DNS at startup
  and no hidden fetch. `:`-separated IPv6 literals are rejected because the
  last `:` is the reason separator; the Java `;` form is accepted. IPv6 ranges
  and country/GeoIP blocking are not implemented.
- Address-book merges keep any destination that existed before the merge
  (Java `Daemon.java`). Duplicate lines inside one source still follow
  last-wins; explicit operator add/replace/remove semantics are unchanged.
- Tracker announces keep two separate notions: local selection drives Seeding,
  promotion and the UI; the swarm is told the whole-torrent remainder and only
  receives `event=completed` after every piece is verified. Failure retries use
  a short internal ladder capped at 30 minutes, while tracker-advertised
  intervals are floored at 15 minutes.
- Administrative writes remain a closed typed set. They never accept a path,
  shell string or generic option; the only URL accepted is a validated
  `http(s)` subscription source. Android has dedicated typed JNI methods for
  subscription edits and router ban/unban; do not widen the torrent command
  allowlist or add a generic `ADMIN` bridge.
- No web console, JSP, plugin, shell service, update download or generic
  configuration writer was introduced or restored.

Deferred with reasons: Java supports URL/file-based manual reseed and reseed
proxy/URL editing, but openi2pd deliberately exposes only configured-source
single-flight reseed until a safe URL/file authority design is specified.
`filterDefinition` pre-accept semantics, remaining Digest vectors and Snark
queue/uploader/rate scheduling remain distinct work. Per-tunnel start/stop is
implemented; create/edit/restart remains absent. None of the deferred behavior
is claimed as equivalent.

Prepared regressions: `test-blocklist-parse`, `test-blocklist-enforce`,
`test-addressbook-merge`, extended `test-addressbook-admin`,
`test-torrents-tracker-schedule`, extended `test-torrents-priorities` and
`test-torrents-verify-parity`, plus `test-tui-protocol` coverage of the new
verbs and the `control-security-static` contract pins. No compiler, CTest,
sanitizer, fuzzer, device, live peer/tracker or workflow run exists for them.

## 14. netDb and LeaseSet continuation (2026-10-26, static)

A four-way audit of MetaLeaseSet/EncryptedLeaseSet/LeaseSet2, DatabaseLookup
reply keys and DatabaseSearchReply, RouterInfo freshness/persistence and netDb
concurrency closed the defects recorded in `docs/INTEROPERABILITY.md`:
direct-garlic DatabaseLookup admission and the three-state DatabaseStore origin,
transport-RI flood admission, bounded queues/new-RI admission, durable and
filename-bound RouterInfo persistence with a shutdown flush, generation-based
replacement, non-creating profile selection, MetaLeaseSet canonicalisation
(header expiry = latest entry, zero revocations) and strict-newer encrypted-LS
replies.

Reply-key `0x12` remains deliberately fail-closed: Java 2.14.0 parses it but has
no sender/KDF and answers cleartext, and the spec marks the derived form TBD.
No non-normative KDF or cleartext fallback is introduced.

Deferred with tracking: client-side MetaLeaseSet resolution, exploratory-pool
router garlic forwarding, encrypted replies for our own RI/EXPL lookups, Java's
probabilistic store pdrop and go-away RI, per-use stale-RI selection refresh,
throttle-map LRU eviction, `m_AcceptUpdates` memory ordering, asynchronous
reseed, local RouterInfo address-slot copy-on-write (SEC-025) and netDb pool
destruction order. Prepared regressions: `test-leaseset-encrypted-freshness`,
the rebuilt Meta vectors, `test-netdb-clove-origin`,
`test-netdb-router-admission`, `test-netdb-persistence`, extended
`test-netdb-store-lookup-hardening`, `test-netdb-stop-admission` and
`test-tunnel-dsm-policy`. No compiler, CTest, sanitizer, fuzzer, Java peer or
live floodfill run exists for them.

## 15. Tunnel, I2NP and garlic continuation (2026-10-26, static)

A four-way audit of tunnel construction, tunnel pools/timers, I2NP dispatch and
garlic routing closed the defects recorded in `docs/INTEROPERABILITY.md`:
the build-record table bound (a real heap out-of-bounds write), fail-closed
record encryption with exact reply counts, transit build-request replay and
Java build expirations, one admission predicate for fresh selection and reuse,
pool build accounting with a failure ladder, persisted tunnel tests with
strikes, the custom-selector use-after-free, real far-end transport masks, a
bounded tunnel queue with a 45 s fragment window, the Java dispatcher expiry and
duplicate window, exact DeliveryStatus/DataMessage lengths, and the garlic
clove grammar, bounded destination queue, per-session state serialization and
destination-session lock inversion.

| Area | Java 2.14.0 reference | Local status | Task |
|---|---|---|---|
| Build record table / phony slot | `BuildRequestRecord.java`, `TunnelCreatorConfig.java` | Implemented / static — plan validated before any write, inbound clamps to `MAX_INBOUND_HOPS` | `TU-107` |
| Record encryption failure | `BuildRequestRecord.java` (`createRequestRecord`) | Implemented / static — `bool` + whole-record zeroing, reply count/length must match | `TU-108` |
| Transit build replay/expiry | `BuildHandler.java` | Implemented / static — 8 min decaying reply-key filter, 25 s relay / 60 s reply | `TU-109` |
| Peer reuse admission | `PeerManager.java`, `TunnelPool.java` | Implemented / static — shared predicate, failed/struck paths never reused | `TU-110` |
| Pool builds/tests | `TunnelPool.java`, `BuildExecutor.java`, `Tunnel.testJob` | Implemented / static — pending counted, 4/8 ladder, 3 strikes, 7.5–15 s deadline, 40 s activity skip | `TU-111` |
| Selector lifetime, masks, queue | `TunnelPool.java` custom selector, `FragmentHandler.java` | Implemented / static — owner pinned, real masks, 8192-entry queue, 45 s reassembly | `TU-112` |
| I2NP dispatch window/duplicates | `MessageValidator.java`, `DecayingHashSet.java` | Implemented / static — 90 s/240 s window, two-generation msgID+second filter | `I2NP-011` |
| Exact message lengths | `DataMessage.java`, `DeliveryStatusMessage.java`, `I2NPMessageImpl.java` | Implemented / static — null on overflow, exact lengths | `I2NP-012` |
| Clove grammar/session state | `GarlicMessageParser.java`, `RatchetSKM.java`, `ElGamalAESEngine.java` | Implemented / static — 1..32 cloves, per-destination replay filter, origin-aware allowlist, per-session state lock | `GAR-005` |
| Destination session locks | `RatchetSKM.java`, `ElGamalAESEngine.java` | Implemented / static — map snapshot outside `m_SessionsMutex` | `CONC-014` |

Deliberate decisions recorded by this increment:

- The build record byte layout, HKDF chain, ChaCha20 slot nonces and AEAD
  associated data are unchanged; only the *bounds* and failure handling moved.
  No wire or persisted format changed.
- A replayed transit build request is dropped, never answered twice, and the
  filter key (the derived reply key) is never logged. The filter lives for the
  lifetime of one `TransitTunnels` instance so a restart starts clean.
- Tunnel tests are timed per path (2.5 s/hop, 7.5 s floor, 15 s cap) rather
  than being recreated on every management tick; one lost test is treated as
  packet loss, three consecutive losses fail the tunnel. The strike counter is
  separate from the state machine so inbound traffic cannot clear it.
- The dispatcher window deliberately exempts `TunnelData` (the endpoint has its
  own IV-replay filter and inner-message expiration) and cloves (each
  destination keeps its own salted filter), and never re-checks the stricter
  per-hop tunnel window.
- ROUTER-mode ratchet cloves are rejected: this destination has no
  authenticated router forwarding path that could re-address and re-encrypt the
  clove, and sending it as cleartext would violate the anonymity contract.
- Garlic keys, decryption and tag state remain core-only. The IPC boundary was
  not widened: no garlic operation was added to `privsep/Protocol.h`.

Deferred with tracking: Java's probabilistic netDb store admission, client
MetaLeaseSet resolution, exploratory router garlic forwarding, encrypted
replies for our own RI/EXPL lookups, NTCP2 established-session cap/12-byte
options/establisher-thread socket ownership (`NET-172`), SSU2 socket sharing and
memory-pool lifetime after deferred teardown (`SEC-025`), local RouterInfo
address-slot copy-on-write, `TorrentsTunnel::InitTorrentFiles` disk-domain
mutation, and Java's B1/B2/B3 payload-size normalisation (deferred rather than
approximated).

Prepared regressions: `test-tunnel-build-records`,
`test-tunnel-peer-selector-lifetime`, `test-tunnelpool-build-backoff`,
`test-tunnelpool-test-timing`, `test-tunnelpool-reuse-policy`,
`test-transit-tunnel-build-replay`, `test-tunnel-endpoint-fragments`,
`test-tunnels-inbound-queue`, `test-i2np-dispatch-policy`,
`test-garlic-clove-policy`, extended `test-tunnel-dsm-policy` and
`test-tunnelpool-zero-hop`, the new `fuzz-i2np-policy` fuzzer (registered in
`tests/CMakeLists.txt` and `.github/workflows/fuzz.yml`) and the extended
`control-security-static` pins. No compiler, CTest, sanitizer, fuzzer, Java peer
or live tunnel run exists for them.

## 16. SAM, I2CP, streaming and datagram continuation (2026-10-26, static)

A four-way audit of the SAM bridge, the I2CP client, the streaming state machine
and Datagram1/2/3 closed the defects recorded in `docs/INTEROPERABILITY.md`:
the SAM receive-port conflation, secret logging and unbounded control plane,
ACCEPT/FORWARD coexistence, 2.14.0 per-style subsession protocol, b33
resolution, BlindingInfo consumption with Java HostReply codes, I2CP admission
and read deadlines, LeaseSet create/send validation, the `AcceptStream`
use-after-free, duplicate-SYN resurrection and silent SYN drops, per-send
datagram framing, receiver protocol isolation and parser/session state races.

| Area | Java 2.14.0 reference | Local status | Task |
|---|---|---|---|
| SESSION CREATE ports | `SAMv3RawSession.java`, `SAMv3DatagramSession.java` | Implemented / static — `PORT` is the forward socket, `LISTEN_PORT` the receive port (default 0) | `SAM-122` |
| Control plane/secrets | `SAMHandlerFactory.java`, `SAMBridge.java` | Implemented / static — 64 sockets, 60 s HELLO, no idle close, no secret logging | `SAM-123` |
| STREAM ACCEPT/FORWARD | `SAMv3StreamSession.java`, `SAMv3Handler.java` | Implemented / static — mutual exclusion, ALREADY_ACCEPTING/TIMEOUT/I2P_ERROR, port scope | `SAM-124` |
| Subsession protocol | `PrimarySession.java` | Implemented / static — `(STYLE, 17/19/20, port)` | `SAM-125` |
| Accept re-arm | `SAMv3StreamSession.java` | Implemented / static — queued ACCEPT re-armed per port | `SAM-126` |
| b33 names | `LookupDestJob.java`, `Blinding.java` | Implemented / static — explicit and implicit b33 | `I2CP-112` |
| BlindingInfo 42 | `ClientMessageEventListener.java` | Implemented / static — stored auth material reaches the encrypted-LeS request | `I2CP-113` |
| HostReply codes | `HostReplyMessage.java` | Implemented / static — 1/2/3/4/5/6 | `I2CP-114` |
| I2CP admission | `ClientManager.java` | Implemented / static — connection/session caps and async deadlines | `I2CP-115` |
| LeaseSet/send validation | `ClientMessageEventListener.java`, `CreateLeaseSet2Message.java` | Partial / static — CLS1 DSA-only fail-closed; CLS2 Meta/preference/key checks; status 14/10/11 | `I2CP-116` |
| Accept lifetime | `ConnectionHandler.java` | Implemented / static — heap sync state, token removal | `STR-113` |
| SYN replay/rejection | `ConnectionManager.java` | Implemented / static — recently-closed set + RESET | `STR-114` |
| Post-close/races | `Connection.java`, `PacketQueue.java` | Implemented / static — no data after close, locked handler/dispatch | `STR-115` |
| Datagram framing | `SAMDatagramSession.java` | Implemented / static — per-send version | `DG-111` |
| Receiver isolation | `I2PSessionDemultiplexer.java`, `PrimarySession.java` | Implemented / static — `(protocol, port)`, no promotion | `DG-112` |
| Parser state | `I2PSessionMuxedImpl.java` | Implemented / static — per-call options, locked (in)deflater/session, zero-signature rejection | `DG-113` |

Deliberate decisions recorded by this increment:

- `PORT` and `LISTEN_PORT` are distinct: a SAM client that only sets `PORT`
  forwards to that local port but advertises PORT_ANY on I2P. This matches Java
  and fixes `RAW RECEIVED`/`DATAGRAM RECEIVED` for the single-session form.
- No idle deadline is applied to an established SAM control connection. Java
  calls `setSoTimeout(0)` after HELLO, and clients legitimately park the session
  socket for hours; only HELLO and a partially received command line are
  bounded.
- SAM version bounds are compared numerically. An unparsable/empty bound yields
  NOVERSION like Java, but a negative bound is rejected instead of matching by
  Java's lexicographic accident; the divergence is pinned by `test-sam-version.sh`
  and the control-framing test.
- Datagram framing is a per-message property; the destination-wide version is
  only a default. A session's learned version is never mutated by an explicit
  send.
- Receiver keys are `(protocol, port)` with protocol 0 as the explicit
  catch-all. A port-specific registration is not promoted to the destination
  wildcard: delivering an unaddressed port to another subsession is a
  cross-subsession disclosure.
- BlindingInfo auth material is used for decryption only when it is exactly 32
  bytes (DH private key or PSK); other lengths are ignored rather than
  truncated, and key/secret bytes are never logged.
- A rejected streaming SYN is answered with one signed RESET (Java
  `limitAction`/`DropOverLimit` shapes are not implemented). TIME-WAIT/FIN
  retention remains open: the FIN is sent once and the stream is removed.

Deferred with tracking: streaming TIME-WAIT/FIN retention and
SYNACK-before-LeaseSet ordering, `limitAction` responses and uploader/blame
parity; CLS1 non-DSA key installation; day-scoped BlindingInfo matching;
33–61 KiB Java datagram payloads (local bound 32 KiB);
`TorrentsTunnel`'s shared session-version mutation; and every dynamic check.

Prepared regressions: `test-sam-control-framing`, `test-sam-subsessions`,
`test-sam-version.sh`, `test-i2cp-b33-lookup`, `test-i2cp-blinding-info`,
`test-i2cp-session-lifecycle`, `test-streaming-accept-timeout`,
`test-streaming-accept-race`, `test-streaming-recently-closed`,
`test-streaming-overlimit-reset`, `test-streaming-sendstate`,
`test-datagram-dispatch`, `test-datagram-version-dispatch`, extended
`test-datagram-receiver-lifecycle`, `test-sam-datagram-framing` and the extended
`control-security-static` pins. No compiler, CTest, sanitizer, fuzzer, SAM
client, Java peer or workflow run exists for them.

## 17. Application-services continuation (2026-10-26, static)

A four-way audit of the HTTP/SOCKS proxies, client/server tunnels, reseeding,
`.i2p`/b32/b33 naming with the address book, and the auxiliary services
(BOB/I2PControl/NTP/UPnP) plus their administration surface closed the defects
recorded in `docs/INTEROPERABILITY.md`: reflected content and outproxy
credential disclosure in proxy pages, non-http scheme forwarding, unsanitized
proxied responses, missing Digest and upstream deadlines, unbounded SOCKS
RESOLVE, server-tunnel connection leaks on stop, the absent `filterDefinition`
and pre-accept rejection seam, missing server-tunnel options and per-tunnel
control, the reseed peer-wipe/blocking/no-deadline lifecycle, the non-atomic
address book with ignored feed retractions, and the BOB/I2PControl/NTP/UPnP
trust gaps.

| Area | Java 2.14.0 reference | Local status | Task |
|---|---|---|---|
| HTTP proxy output/protocol | `I2PTunnelHTTPClient.java`, `I2PTunnelHTTPClientBase.java`, `HTTPResponseOutputStream.java` | Implemented / static — escaped pages, no URL echo, http(s)-only, response sanitization, Digest, deadlines | `PARITY-019` |
| SOCKS resolve/replies | `SOCKS5Server.java` | Implemented / static — bounded SipHash cache, loopback replies | `DG-114` |
| Server-tunnel teardown | `I2PTunnelServer.java` | Implemented / static — virtual teardown, forced/in-use stop, restart | `PARITY-019` |
| Access filters | `access/*`, `ConnectionManager.java` | Implemented / static — parser/evaluator, pre-accept drop/reset | `PARITY-017` |
| Server-tunnel options | `I2PTunnelHTTPServer.java` | Implemented / static — targetForPort/spoofedHost/reject*/POST/startOnLoad | `PARITY-019` |
| Tunnel control/admin | `TunnelController.java` | Implemented / static — name registry, Start/Stop, typed TUI/Android verbs | `PARITY-021` |
| Reseed lifecycle | `Reseeder.java`, `ReseedChecker.java` | Implemented / static — cancellable job, deadlines, no peer wipe, status, disable, redaction | `RS-107` |
| Address book | `AddressBook.java`, `Daemon.java`, `DeletedHosts.java` | Implemented / static — atomic no-follow writes, add/replace, feed retraction, bounds, 304, negative cache | `AB-106` |
| BOB | (not present at the 2.14.0 tag) | Implemented / static — loopback/UID, caps, `getkeys` opt-in, no operand logging | `SEC-029` |
| I2PControl | `AuthToken.java`, `JSONRPC2ExtendedError.java`, `NetworkSettingHandler.java` | Implemented / static — private local socket, JSON escaping, error objects, bounded bandwidth | `SEC-029` |
| NTP/UPnP | `NtpClient.java`, `RouterTimestamper.java`, `UPnP.java` | Implemented / static — reply validation/agreement, 3 h lease, same-host IGD refusal | `SEC-029` |
| TUI/Android admin | console functional equivalents | Implemented / static — typed verbs, screens, JNI bridges | `PARITY-021` |

Deliberate decisions recorded by this increment:

- Proxy pages never echo configured URLs or credentials; unsupported schemes
  are rejected with 403 before any destination resolution, and only
  `http`/CONNECT-`https` is proxied (a proxy cannot negotiate TLS for a plain
  request).
- Digest is opt-in (`httpproxy.auth.type=digest`); Basic stays the default and
  the existing constant-time path. `auth-int`, `-sess`, SHA-512-256 and
  `userhash` are rejected rather than half-implemented.
- `filterDefinition` denials are silent (`drop`) by default so a banned peer
  gets no confirmation; `limitAction=reset` opts into one signed RESET.
  Record counters are memory-only and reset on restart.
- A port-specific server tunnel may not own the destination-wide acceptor
  unless `defaultacceptor=true`; a stopped tunnel now actually closes its
  streams/sockets and can restart.
- Reseed never deletes cached peers from a downloaded bundle: a stale bundle
  imports its fresh entries and reports the stale ones. The blocking
  bootstrap remains for the first start; only administrative reseed is async.
- `ADDRESS-ADD` never re-points a name; `ADDRESS-REPLACE` is explicit. Feed
  `action=delete` only retracts names recorded as coming from that
  subscription, through a bounded sidecar that does not change `addresses.csv`.
- BOB is a legacy loopback-only extra, not a Java 2.14.0 feature; `getkeys`
  requires an explicit operator opt-in and BOB rejects non-loopback peers.
- The I2PControl local socket is treated as a security boundary: owned stale
  socket only, 0600, same-UID peer where the platform exposes credentials, and
  no arbitrary config writer (`AdvancedSettings` is not added).

Deferred with tracking: HTTP client keep-alive/pipelining, IRC `ircclient` and
DCC rewriting, UDP/torrent tunnels in the start/stop registry, Streaming
`limitAction` `http`/custom, reseed `Date`-header/non-SU3 fallbacks, UPnP
status in TUI/Android, Windows local-socket peer credentials, and the
es/ru/zh-Hans catalog entries for the new TUI text (`I18N-004`).

Prepared regressions: `test-http-proxy-limits`, `test-proxy-auth`,
`test-socks-resolve`, `test-i2ptunnel-{stop,filter,targets}`,
`test-streaming-limitaction`, `test-reseed-{outdated-bundle,lifecycle,redaction,netid-url}`,
`test-addressbook-{actions,cache,persistence}`, `test-i2pcontrol-{json,socket}`,
`test-bob-command`, `test-upnp-mapping`, extended
`test-ntp-timesync-lifecycle`, `test-tui-protocol`, `test-sam-version.sh`,
`RuntimeAdminPolicyTest`, `RuntimeAdminParityTest` and the extended
`control-security-static` pins. No compiler, CTest, sanitizer, fuzzer, Gradle,
device, live proxy/tunnel/reseed/IGD/NTP/BOB/Java peer or workflow run exists
for them.

## 18. Full 2.14.0 subsystem re-audit (2026-10-31, static)

The audits below use the official `i2p-2.14.0` source pin
`3c98503c5f14efe7bdd783528ba7f621f12d8081`, inspected read-only with
`gh api`, plus the protocol specifications linked from
[`protocols/README.md`](protocols/README.md). Java sources were not copied,
vendored, built or executed. The comparison establishes source-level behavior,
not runtime interoperability. Each status remains pending manual CI and, where
applicable, controlled differential tests.

| Area | Verifiable Java reference | Local evidence and classification | Required regression / decision |
|---|---|---|---|
| NetDb lookup admission | [`FloodfillDatabaseLookupMessageHandler.createJob()`](https://github.com/i2p/i2p.i2p/blob/3c98503c5f14efe7bdd783528ba7f621f12d8081/router/java/src/net/i2p/router/networkdb/kademlia/FloodfillDatabaseLookupMessageHandler.java#L579-L598) drops EXPL/ANY before throttle charging on non-floodfills; direct EXPL/ANY from known floodfills is also refused. | `NetDb::HandleDatabaseLookupMsg()` (`libi2pd/NetDb.cpp`) has the corresponding ordering and local bounds. Compatible by static source comparison; Java peer differential pending. | Exercise both flag/origin combinations and prove a dropped request does not consume the budget. |
| NetDb go-away and store admission | [`FloodfillDatabaseLookupMessageHandler`](https://github.com/i2p/i2p.i2p/blob/3c98503c5f14efe7bdd783528ba7f621f12d8081/router/java/src/net/i2p/router/networkdb/kademlia/FloodfillDatabaseLookupMessageHandler.java) and [`KademliaNetworkDatabaseFacade`](https://github.com/i2p/i2p.i2p/blob/3c98503c5f14efe7bdd783528ba7f621f12d8081/router/java/src/net/i2p/router/networkdb/kademlia/KademliaNetworkDatabaseFacade.java) send a go-away RI when a node stops being floodfill and use probabilistic `LIMIT_ROUTERS` admission. | Missing/partial: local `NetDb` uses deterministic bounded admission and does not send the matching go-away RI on store acknowledgement/lookup responses. Keep deterministic caps until a source-backed policy preserves existing updates, reseed and hard bounds. | Differentially test demotion replies, store count around the limit, existing-key replacement, and bounded memory under churn. `PARITY-020`/`DB-127`. |
| Floodfill policy | [`FloodfillMonitorJob`](https://github.com/i2p/i2p.i2p/blob/3c98503c5f14efe7bdd783528ba7f621f12d8081/router/java/src/net/i2p/router/networkdb/kademlia/FloodfillMonitorJob.java#L102-L170) and [`FloodfillNetworkDatabaseFacade`](https://github.com/i2p/i2p.i2p/blob/3c98503c5f14efe7bdd783528ba7f621f12d8081/router/java/src/net/i2p/router/networkdb/kademlia/FloodfillNetworkDatabaseFacade.java) define `router.floodfillParticipant` as `true`, `false` or `auto`; hidden mode disables it and the default is automatic. | Missing: local `floodfill` is an explicit boolean defaulting to false. A direct alias would be misleading because Java's `auto` is a substantial runtime policy, not a boolean synonym. | Add a deterministic policy state-machine only as a separately scoped feature; test hidden override, explicit modes, restart/uptime, transport and network-health hysteresis. Do not silently map `auto` to false. |
| NTCP2 resource/ownership policy | Java router transport's established-session admission, 12-byte options and establisher/service socket lifecycle (see `NET-172` and [`INTEROPERABILITY.md`](INTEROPERABILITY.md)). | Partial: C++ lacks the same established-session cap and option negotiation; the exact-IP pending map is bounded but has a coarse expiry sweep, and initiation/close I/O ownership crosses threads. These are implementation/availability differences, not proven wire failures. | Add tests for admission saturation, option negotiation and descriptor close/send races before changing socket ownership. Keep deadlines on every handshake read. |
| Address-book feed actions | [Proposal 112](https://i2p.net/en/proposals/112-addressbook-subscription-feed-commands/) defines `sig`, `oldsig`, canonical signed command forms and actions including `adddest`, `addname`, `addsubdomain`, `changedest`, `update`, `remove` and `removeall`; pinned Java [`Daemon.java`](https://github.com/i2p/i2p.i2p/blob/3c98503c5f14efe7bdd783528ba7f621f12d8081/apps/addressbook/java/src/net/i2p/addressbook/Daemon.java) verifies signatures before mutation. | Partial/security gap: `AddressBook.cpp::ParseSubscriptionAction()` handles `action=delete` with local source-origin matching but does not verify `sig` or `oldsig`; the other signed actions are absent. Do not treat origin equality as feed authenticity. | Build signed valid/invalid vectors from Proposal 112 for every action, old-destination authority, replay, canonicalization and cross-feed origin. Implement only after signature-scope/crypto review; retain the local origin restriction. `AB-107`. |
| Subscription administration | Pinned Java [`SubscriptionList.java`](https://github.com/i2p/i2p.i2p/blob/3c98503c5f14efe7bdd783528ba7f621f12d8081/apps/addressbook/java/src/net/i2p/addressbook/SubscriptionList.java) and [`SubscriptionsBean.java`](https://github.com/i2p/i2p.i2p/blob/3c98503c5f14efe7bdd783528ba7f621f12d8081/apps/susidns/src/java/src/i2p/susi/dns/SubscriptionsBean.java#L97-L159) persist subscription metadata and request a naming-service refresh after edits. | Functionally implemented through bounded TUI requests and Android AddressBook typed JNI/service-worker calls. It intentionally does not provide a generic text-file editor or Java web UI. | Test add/remove/duplicate/invalid URL, persistence failure and refresh semantics; Android policy tests exist, Gradle/device validation remains pending. `PARITY-018`. |
| Manual reseed administration | Java [`ConfigReseedHandler`](https://github.com/i2p/i2p.i2p/blob/3c98503c5f14efe7bdd783528ba7f621f12d8081/apps/routerconsole/java/src/net/i2p/router/web/helpers/ConfigReseedHandler.java#L25-L108) accepts a user URL or uploaded ZIP/SU3; [`Reseeder.java`](https://github.com/i2p/i2p.i2p/blob/3c98503c5f14efe7bdd783528ba7f621f12d8081/router/java/src/net/i2p/router/networkdb/reseed/Reseeder.java#L180-L260) imports bundles. | Partial by design: native administration exposes configured-source, single-flight reseed. User-selected paths/URLs and the Java proxy/URL editor are absent; an unsigned ZIP can inject RouterInfos and arbitrary path/URL authority is not exposed. | Keep the bounded action. Any SAF import or URL-selection feature needs a separate trust/authority design and signed/unsigned fixture policy; do not add arbitrary strings to generic admin IPC. `RS-108`. |
| Tunnel lifecycle administration | Pinned Java [`TunnelController`](https://github.com/i2p/i2p.i2p/blob/3c98503c5f14efe7bdd783528ba7f621f12d8081/apps/i2ptunnel/java/src/net/i2p/i2ptunnel/TunnelController.java#L415-L442) implements start and stop. | Per-tunnel start/stop is implemented statically in daemon-owned TUI and Android typed operations; create/edit/restart remains absent. The prior note that start/stop needed an owner-context contract is obsolete. | Test stopped/in-use/restart and shutdown races through the typed queue; Java `restartTunnel()` remains distinct work if requested. `PARITY-019`/`PARITY-021`. |
| Android live router controls | Java router console config handlers apply bandwidth and share changes to the running router. | Implemented / static: Android Runtime admin now invokes dedicated, bounded JNI methods on the service worker for transit, bandwidth and share; the native daemon validates the same closed inputs and queues the existing typed operations. It does not persist preferences or expose a generic config writer. | Manual Android CI/device validation remains pending for accepted values, rejection, stopped-router behavior and lifecycle races. `PARITY-022`. |
| I2PSnark durable magnets | Pinned Java [`SnarkManager.saveMagnetStatus()`](https://github.com/i2p/i2p.i2p/blob/3c98503c5f14efe7bdd783528ba7f621f12d8081/apps/i2psnark/java/src/org/klomp/snark/SnarkManager.java#L2403-L2445) stores magnet identity, tracker hints, display name and run state per infohash. | Missing: `TorrentsState` stores stop/priorities/comments only; `TorrentsTunnel::AddMagnet` does not persist the unresolved URI/hints, so restart loses unresolved magnets. | Add a bounded versioned local record, migration/rejection semantics, duplicate-hint merge and restart-before-announce vectors. No wire or metainfo change. `SNARK-012`. |
| I2PSnark upload and bandwidth policy | Pinned Java [`PeerCoordinator`](https://github.com/i2p/i2p.i2p/blob/3c98503c5f14efe7bdd783528ba7f621f12d8081/apps/i2psnark/java/src/org/klomp/snark/PeerCoordinator.java#L826-L838) gates unchokes and caps each torrent at eight uploaders; [`BandwidthManager`](https://github.com/i2p/i2p.i2p/blob/3c98503c5f14efe7bdd783528ba7f621f12d8081/apps/i2psnark/java/src/org/klomp/snark/BandwidthManager.java#L45-L68) coordinates global, torrent and peer limits. | Missing: local per-peer rate observations are not admission limits or uploader-slot rotation; Android bandwidth settings are router-wide, not torrent policy. | Add deterministic slot rotation, starvation, pause/private-swarm and layered token-budget regressions before implementation. `SNARK-012`. |

The user-visible administration surfaces remain the ncursesw/Win32 TUI and
Android Material 3/Java 25. Java source is behavioral evidence only: do not
add a desktop JVM, copy JSP/HTML/JavaScript, expose an administrative web
server, or broaden the typed control boundary to arbitrary paths, URLs or
configuration writes.

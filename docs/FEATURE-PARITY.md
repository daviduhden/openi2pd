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
| Pin verification | Official remote tag refs inspected with `git ls-remote`; scoped 2026-10-21 source review completed for admin/client/torrent/router candidates, full matrix re-audit pending |
| Historical audit baseline | `i2p-2.13.0`, commit `9134f808337b401e8e53c73734c81fab04280c9d` |
| Reference use | Read-only external clone; no Java source is vendored, built or shipped |
| Historical audited C++ tree | `bce8771b` plus the parity increment recorded at that audit |

The matrices and findings below retain their historical Java 2.13.0 evidence.
A scoped 2026-10-21 read-only review checked 2.14.0 sources for the
administration, address-book, client-service, torrent and selected
router/tunnel candidates recorded here; the full matrix re-audit remains
`NET-026`. Tag existence and source inspection do not establish feature parity
or dynamic interoperability.

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
| SSU2 flood request flag | `PeerState2.gotRI`, `NTCP2Payload` | `SSU2Session.cpp` RouterInfo handling | Implemented, not validated | A newly accepted/replaced RouterInfo with the request-flood bit is forwarded through NetDb's bounded floodfill selection; stale replays are not re-flooded. Amplification and malformed-block tests remain. | SSU2 RouterInfo tests pending | P1 |
| PQ transport variants | `UDPTransport.addSSU2Options`, ML-KEM classes | `PostQuantum.*`, `SSU2*.cpp` | Implemented, not validated | Runtime provider probing now gates publication and key creation instead of a compile-time constant. Cross-provider and Java PQ session vectors are absent. | `test-crypto-x25519-noise-elligator-mlkem-audit`; Java PQ test pending | P0 |
| RouterInfo parsing and validation | `data/router/RouterInfo.java`, `RouterAddress.java` | `libi2pd/RouterInfo.*` | Compatible, implementation different | Strict bounded parser; 4 KiB Java ceiling aligned. Fuzz and large-RI fixture execution pending. | `test-protocol-bounds`; fuzz pending | P0 |
| NetDb store/lookup/floodfill | `networkdb/kademlia/*`, `database/DatabaseStoreMessage.java` | `libi2pd/NetDb.*`, `KadDHT.*` | Partial | Replacement, freshness, DLM/DSRM and bounded requests were hardened. Token-bearing DatabaseStore messages arriving through a router tunnel are dropped before DeliveryStatus reflection, matching the Java exploratory-tunnel rule. Live Java floodfill lookup, RouterInfo persistence and network routing remain unvalidated. | `test-tunnel-dsm-policy`; live lookup pending | P0 |
| Reseeding and SU3 | `util/SU3File.java`, `networkdb/reseed/*` | `Reseed.cpp`, `Config.cpp` | Implemented, not validated | `reseed.verify` now defaults to true; RSA 2048/3072/4096 and Ed25519/RedDSA SU3 verification paths were added with fail-closed unsupported handling. A real signed bundle and Java cross-vector are pending. | `test-su3-bounds`; signed-bundle test pending | P0 |
| LeaseSet, LS2, EncryptedLeaseSet, MetaLeaseSet | `core/.../LeaseSet.java`, `LeaseSet2.java` | `libi2pd/LeaseSet.*`, `Blinding.cpp` | Partial | Parsing, expiry, encrypted store key and Meta precedence exist. Secret-blinded authorization and a Java-generated encrypted-LS corpus remain absent. | LeaseSet tests; Java vector pending | P0 |
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
| SAM 3.x | `apps/sam/java/src/net/i2p/sam/*` | `libi2pd_client/SAM.*` | Implemented, not validated | Persistent bounded framing, pipelined commands, split SEND payloads, one-word PING/PONG/HELP/QUIT, quoted parameter values, case-insensitive keys and repeated-space command words were added. LISTEN_PROTOCOL routing remains partial. | `test-sam-control-framing`, datagram/version tests; live SAM client pending | P1 |
| Streaming | `router/.../client/streaming/*` | `Streaming.*` | Partial | SYN/window/reset and MTU bounds exist. Live stream and malformed-packet interoperability are pending. | Streaming tests; live test pending | P1 |
| Datagram1/2/3 | `core/.../client/Datagram*.java` | `Datagram.*` | Partial | Repliable/raw, ports, auth and zero-length options are implemented. Datagram2 offline-signature Java vectors and per-session version selection remain. | Datagram tests; Java vector pending | P1 |
| Client and server tunnels | `apps/i2ptunnel/java/src/net/i2p/i2ptunnel/*` | `libi2pd_client/ClientContext.*`, `I2PTunnel.*` | Partial | Native client/server tunnel configuration exists; TUI/admin coverage is still narrower than Java's web editing. | TUI reload/admin tests; live tunnel test pending | P1 |
| HTTP proxy | `apps/i2ptunnel/java/src/net/i2p/i2ptunnel/HTTPProxy.java` | `libi2pd_client/HTTPProxy.*` | Compatible, implementation different | Native proxy; header lifetime/compaction fixes were retained. No browser-embedded console. | HTTP tests; live proxy pending | P2 |
| SOCKS proxy | `apps/i2ptunnel/java/src/net/i2p/i2ptunnel/SOCKS*.java` | `libi2pd_client/SOCKS.*` | Compatible, implementation different | Native implementation. Authentication/edge cases need live differential tests. | SOCKS tests pending | P2 |
| Address book, .i2p and b32/b33 | `apps/addressbook/java/src/net/i2p/addressbook/*` | `libi2pd_client/AddressBook.*` | Implemented, not validated | ASCII case folding, case-preserving b33 labels, mixed-case hosts.txt replacement and exact base64 handling remain. Native TUI Destinations and Android Material screens list, add/replace, remove and block/unblock entries with Java-compatible name validation. Removed names persist as deleted-host tombstones and subscription merges skip them; an explicit add clears the matching block. Subscription URL editing remains open. | `test-addressbook-case`, `test-addressbook-admin`; live .i2p lookup pending | P1 |
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
| Native TUI/Android control | Java web UI (functional equivalent only) | `TuiProtocol`, `tui/*`, Android torrent fragment | Partial | Native typed control includes force recheck, metainfo comment/created-by editing and Android remove-with-data, with commands retained on the torrent service thread. Selected-file promotion, full queue/bandwidth controls and crash recovery remain incomplete. | TUI/IPC/Android tests; device test pending | P0 |

## 5. Administration, TUI and Android

Java console behavior is separated from its JSP/HTML/JavaScript presentation. The
replacement is native terminal and Android UI over bounded local IPC/JNI.

| Operation | Java reference | openi2pd surface | Status | Notes / gaps | Priority |
|---|---|---|---|---|---|
| Router status and diagnostics | `apps/routerconsole/.../RouterConsoleRunner`, status pages | TUI Status/Connections; Android Dashboard/Network/Statistics | Compatible, implementation different | Read-only snapshots are bounded. Advanced Java graph/profile pages are summarized rather than copied. | P1 |
| Logs | `apps/routerconsole/.../LogsHelper`, log pages | TUI Logs; Android Logs/settings | Compatible, implementation different | Native log queue and levels. Android validates the closed set and applies the saved level to the running logger on the service worker; TUI has ADMIN LOGLEVEL. | P1 |
| Transit toggle, bandwidth, share | `RouterConsoleRunner`, `configui` | TUI ADMIN TRANSIT/BANDWIDTH/SHARE | Implemented, not validated | Typed command queue executes on the daemon thread; no generic config writer. Dynamic tests pending. | P0 |
| Reseed | Java console reseed action and reseed status | TUI ADMIN RESEED; Android Reseed NetDb action | Implemented, not validated | Single-flight NetDb reseed; no URL/path input. Android calls the service worker and shows a bounded result. Signed bundle/device test pending. | P0 |
| Tunnel reload | `I2PTunnel` config reload | TUI ADMIN RELOAD TUNNELS; Android Reload tunnels | Implemented, not validated | Reuses `ClientContext::ReloadConfig`; no arbitrary file path. | P1 |
| Shutdown/graceful drain | Java console shutdown | TUI ADMIN SHUTDOWN GRACEFUL/IMMEDIATE/CANCEL | Implemented, not validated | Typed modes only; immediate shutdown requires typed confirmation. Signal/IPC paths remain daemon-owned. | P0 |
| Router-wide configuration | Java config UI | TUI read-only snapshot; settings files/Android preferences | Compatible, implementation different | There is deliberately no generic TUI config writer. Android validates and persists a closed set. | P1 |
| Client/server tunnel editing | Java I2PTunnel web editor | TUI Destinations/reload; Android Tunnels | Partial | Existing configuration can be reloaded; full native tunnel creation/editing UI is missing. | P1 |
| Address book editing | Java addressbook UI | TUI Destinations; Android Address book screen | Implemented, not validated | List/add/replace/remove plus deleted-host block/unblock use bounded local TUI IPC or app-internal JNI, full base64 identities only, and do not delete key material. Subscription URL editing remains open. | P1 |
| Peer/netDb browsing | Java netDb/peers pages | TUI NetDb/Peers/Connections | Compatible, implementation different | Bounded read-only views. LeaseSets expose encryption type and aged timestamps. Advanced floodfill diagnostics and lookups are narrower. | P2 |
| Client services | Java I2PTunnel/proxy/SAM status pages | TUI Services (`v`) | Compatible, implementation different | Bounded read-only inventory of TCP/UDP client and server tunnels, HTTP/SOCKS proxies and SAM/BOB/I2CP servers; public b32 destinations only, no keys or paths. Per-tunnel start/stop/editing remains open. | P2 |
| I2PSnark operations | Java Snark UI | TUI Torrents + Android native panel | Partial | Pause/start/recheck/priorities/comments/tracker operations; force recheck requires a paused torrent and hashes all pieces. Selected-file completion now drives tracker `left`, per-file `.part` promotion and Seeding status; mixed final/`.part` restarts verify only fully backed pieces and resume bitfields require exact padding. Private swarms are labeled and use a fixed primary tracker. Metainfo comment/created-by editing and Android remove-with-data are exposed; queue/bandwidth policy and durable magnets remain incomplete. | P0 |
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

1. Add native Android tunnel/address-book administration and validate the new
   reseed/log-level actions with Gradle/JNI/Lint/device tests.
2. Close controlled interoperability gaps: Java PQ/non-PQ handshakes, signed
   SU3 bundle, RSA key vector, SAM LISTEN_PROTOCOL routing, I2CP secret-blinded
   lookup and Datagram2 offline vector.
3. Finish torrent operations: selected-file promotion, durable queue,
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
| Router/netDb | Per-requester DatabaseLookup and per-key flood throttles, direct exploratory/ANY rejection from floodfills, bounded stale-RouterInfo refresh | Missing / Partial | P1 |
| Router/reseed | Proposal 147 `?netid=` on SU3 URLs and multi-source/minimum-RouterInfo acceptance | Partial | P2 |
| Router/security | Read-only startup IP/CIDR and router-hash blocklist with bounded parser and existing transport/netDb enforcement points | Missing | P2 |
| Client tunnels | Server-tunnel `filterDefinition` parser, thresholds, file lists, recorders and reset rejection; openi2pd currently has only an inline whitelist | Missing | P1 |
| Streaming | Minute/hour/day and total incoming limits, destination-hash counting after signature validation, `limitAction` drop/reset/HTTP/custom | Partial | P1 |
| SAM | `LISTEN_PROTOCOL`, protocol+port RAW/DATAGRAM receiver dispatch, per-subsession datagram version, duplicate rejection and receiver reset on `SESSION REMOVE` | Partial | P1 |
| Proxies | Local HTTP 407 Basic/Digest and SOCKS5 username/password authentication with constant-time checks and no credential forwarding | Missing | P1 |
| Administration | Read-only peer profile diagnostics and client/server tunnel inventory; per-tunnel runtime start/stop; Android live transit toggle | Narrower / Missing | P2 |
| I2PSnark | Selected-file promotion, uploader-slot allocation and per-session/per-torrent bandwidth limits | Partial / Missing | P0/P1 |

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

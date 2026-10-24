<!--
See the LICENSE file at the top of the project tree for copyright
and license details.

openi2pd: memory safety 2026.

Documents project documentation.

-->

# Memory-safety and concurrency audit — 2026-10-18

Status: **static corrections prepared, dynamic validation pending**. No
compiler, ASan/UBSan/TSan, fuzzer, OpenBSD, Windows, Android or live I2P run
was performed for these changes. Severity is based on static reachability and
impact, not on a reproduced exploit.

## BOB and I2PControl follow-up (2026-10-24)

| ID | Severity | Finding and correction | Regression / validation |
|---|---|---|---|
| CONTROL-01 | High | BOB `setkeys` and I2PControl password changes exposed complete secrets in logs. Both now record only the operation/outcome. | `control-security-static` pins absence of the known secret-bearing log forms; marker/sink CI regression pending. |
| CONTROL-02 | High/medium | BOB accept/read/LeaseSet/stream callbacks captured raw tunnel pointers across `STOP`/`CLEAR`. Completions now lock weak derived ownership, pending receivers are registered/closed, and a short admission mutex makes stop atomic with handler publication. | Static seam prepared; BOB teardown under ASan/TSan pending. |
| CONTROL-03 | Medium | I2PControl allowed unlimited TLS work, performed a blocking body read on its event-loop thread and had no operation deadline. Admission is capped, handshake/header/body/write phases carry a 15-second deadline, and bounded header/body reads are asynchronous. | `control-security-static`; fragmented-header, slow-client and timeout integration pending. |
| CONTROL-04 | Medium | Authentication tokens were unbounded and the public default password could protect a non-loopback listener. Tokens and all authentication attempts are capped, expired entries purge before issuance/validation, empty/default runtime password changes are rejected, and the configured default is refused outside loopback. | Policy/static contracts prepared; TCP/UNIX configuration matrix pending. |
| CONTROL-05 | Medium | Certificate-key creation chmodded and reopened a pathname, permitting a symlink/TOCTOU race under a hostile writable parent. PEM data is now serialized in memory and committed through `AtomicWritePrivate`: POSIX staging is mode 0600/no-follow and Windows staging has a protected effective-user/SYSTEM DACL with reparse-target rejection. | Static contract prepared; POSIX symlink and Windows DACL/reparse CI coverage pending. |

Residual: the direct non-HTTP JSON form retains its legacy single-read framing.
TLS slow-client, restart, BOB callback teardown and platform file-authority
behavior require manual CI; this static correction is not validation.
Windows pathname traversal retains a hostile-parent TOCTOU window, and the
certificate and key replacements are individually atomic rather than a
two-file transaction.

## SSU2 / NTCP2 / transports

| ID | Severity | Finding and correction | Regression / validation |
|---|---|---|---|
| SSU2-01 | High | Out-of-session PeerTest messages were decrypted with the public introduction key and then fed to the generic data-phase block dispatcher. A forged `I2NPMessage`, fragment, `PathChallenge` or `Termination` could reach code with no authenticated session. `SSU2PeerTestSession` now permits only `DateTime`, `Address`, `PeerTest` and `Padding`; `HandlePayload()` rejects all other blocks. | Policy assertions added to `test-ssu2-control-plane`; live forged-packet test pending. |
| SSU2-02 | Medium-low | PeerTest sessions do not derive data-phase keys, but a forged path challenge could reach `SendData()` and read uninitialized `m_KeyDataSend`. The arrays are now zero-initialized and `SendPathResponse()` refuses to run outside an established data session. | Covered by the restricted-block regression; under MSan/ASan pending. |
| SSU2-03 | Low | RelayIntro reserved only `len + 32` bytes although the block needs `len + 35`; PeerTest msg4 passed the full payload size to a block written at an advanced pointer. Both capacities are now exact and an insufficient block is rejected rather than silently dropping the relay. | Sizing/relay test pending in SSU2 regression suite. |
| NTCP2-01 | Medium-low | `NTCP2Server::Stop()` terminated sessions from the transports thread while the NTCP2 service could iterate the same maps, and it performed socket operations off the owning executor. Teardown is now posted to the service and awaited before `StopIOService()`, matching the SSU2 shutdown pattern. | Shutdown stress under TSan/ASan pending. |

Residual: `SSU2Session::m_Address`, shared UDP sockets and some Asio
timer/socket calls from non-owner threads remain open; `m_OnEstablished` is
now mutex/exchange protected, but full executor ownership still requires a
dedicated refactor rather than a local patch.

## Ratchet, Garlic and cryptography

| ID | Severity | Finding and correction | Regression / validation |
|---|---|---|---|
| RATCHET-01 | High | An authenticated peer could send many `NEXT_KEY` blocks per payload; each generated hundreds of ECIES tags while old tagsets remained alive. Payloads now reject more than four `NEXT_KEY` blocks and both incoming tag maps have hard per-destination budgets. | Tag-budget test pending; `test-eciesx25519-ratchet-*` under ASan pending. |
| RATCHET-02 | High | ElGamal/AES blocks could add 200 tags each without an aggregate limit. `MAX_INCOMING_ELGAMAL_TAGS` now rejects a block that would exceed 4096 retained tags; ECIES tags are capped at 8192. | Budget regression pending; fuzzing with `fuzz-base-blinding`/protocol corpus pending. |
| RATCHET-03 | Medium | `HandleECIESx25519TagMessage()` held an iterator into `m_ECIESx25519Tags` while a callback could insert tags and rehash the map, then erased the invalid iterator. The entry is now removed before dispatch using copied tagset/index values. | Iterator/rehash regression pending with a debug STL or libc++ hardened build. |
| RATCHET-04 | Low | `CreatePaddingClove()` did not validate capacity; batch padding could write past its buffer for near-maximum payloads. Capacity checks now return zero and callers abandon the oversized batch instead of writing. | ASan near-limit payload test pending. |

Residual: several stack/heap intermediates containing shared secrets are not
explicitly cleansed; full secret-lifetime coverage remains a separate task.
Some OpenSSL wrapper return-value/size conversions also remain to be audited.

## LeaseSet, RouterInfo and netDb

| ID | Severity | Finding and correction | Regression / validation |
|---|---|---|---|
| NETDB-01 | High | `CreateDatabaseStoreMsg()` used `RouterInfo::GetBuffer()` and `GetBufferLen()` as separate unlocked calls and then read the raw pointer after the mutex was released; `DeleteBuffer()` could free/reuse it. Serialization now holds one `GetSharedBuffer()` snapshot for the whole operation. | Snapshot lifetime assertion added to `test-routerinfo-update-transaction`; concurrent TSAN test pending. |
| LEASE-01 | High | Remote LeaseSet updates mutated an object already returned to streaming/datagram readers (`m_Leases`, encryption keys, encryptor). `LeaseSetDestination::HandleDatabaseStoreMessage()` now builds a validated candidate and publishes it under `m_RemoteLeaseSetsMutex`, keeping old snapshots immutable. | Concurrent update/read TSAN test pending; functional replacement tests still required. |
| NETDB-02 | Medium | `NetDb::Start()` mutated `m_RouterInfos`/`m_Floodfills` after starting the request thread without the maps' mutexes. Own-router insertion/removal now holds `m_RouterInfosMutex` then `m_FloodfillsMutex`, matching `ClearRouterInfos()`. | TSAN Start/Stop/lookup stress pending. |
| ROUTER-01 | Medium | A truncated/oversized `router.keys` could produce `tellg() == -1`, an unbounded `new[]`, ignored `FromBuffer()` failure and a null `GetPublic()` dereference. Loading now validates the file size/read/parse and requires a public identity before continuing. | Corrupt/truncated `router.keys` regression pending; requires RouterContext test fixture. |
| ROUTER-02 | Medium-low | `HandleCloveI2NPMessage()` read TunnelTest msgID/timestamp without validating the clove length. It now requires `TUNNEL_TEST_SIZE` bytes. | Clove truncation regression pending. |
| ROUTER-03 | Medium | `RouterContext` mutators for properties, addresses, caps, floodfill state, MTU, family, bandwidth and statistics did not share a lock with signed-buffer rebuilds, and callers read `GetRouterInfo().GetProperty("caps")` across threads. Mutators now hold `m_RouterInfoMutex`, use `*Locked` helpers to avoid re-entrancy, and the TUI uses `GetRouterInfoProperty()`. | `test-routercontext-update-race` prepared; TSan/CI pending. |

Residual: `GetRouterInfo()` still exposes the live object for compatibility;
transport code that reads individual `Address` fields can race with a mutator,
and Asio timer/socket ownership remains outside this lock. The lock converts
the property/address mutation contract into one mutex domain but does not make
the whole RouterInfo immutable. TSan validation is required before claiming
the object race-free.

## I2CP, SAM, Streaming, Datagram and HTTP proxy

| ID | Severity | Finding and correction | Regression / validation |
|---|---|---|---|
| I2CP-01 | High | `RECONFIGURE SESSION` could dereference `m_Destination` before `CREATE SESSION` when `sessionID == 0xFFFF`. The handler now requires a non-null destination. | First-message `0xFFFF` integration test pending. |
| DATAGRAM-01 | High | LeaseSet request callbacks captured raw `DatagramSession::this`; a destroyed SAM session could be completed into freed memory. Both callbacks now capture `shared_from_this()` and keep the session alive until completion. | ASan destroy-during-request test pending. |
| HTTP-01 | High/medium | Outproxy resolver/connect handlers used raw `this` and a `[&]` capture; reload/stop could deliver a completion to a destroyed handler. Callbacks now capture `shared_from_this()`, reject empty resolver results, and check sockets/kill state. | Reload/stop during DNS test pending. |
| SAM-01 | High impact | `~SAMSingleSession()` unconditionally removed/stopped a destination that may be shared by another SAM session or tunnel. It now deletes only when the refcount is zero; the shared session remains usable. | Two-session lifecycle test pending. |
| CLIENT-01 | High | A failed Android daemon start calls `Daemon.stop()`, which called `ClientContext::Stop()` on a null `m_SharedLocalDestination`. `Stop()`/`ReloadConfig()` now tolerate null, so the failure path cannot SIGSEGV. | Android instrumentation with transports disabled pending. |

Residual: `I2CPSession` output fields and `SAMSocket` datagram writes still rely
on cross-thread sequencing; a per-session output queue/executor refactor is
needed. `Streaming` callback ownership was not changed in this pass.

## BitTorrent

| ID | Severity | Finding and correction | Regression / validation |
|---|---|---|---|
| TORRENT-01 | High | A peer could return `offset=1,len=1` for a requested 16 KiB block; `BlockReceived()` marked the whole block available and serving it exposed uninitialized heap. Blocks must now be aligned and exactly the requested range; buffers are zero-initialized. | Partial-block assertion added to `test-torrents-security`; live peer/ASan test pending. |
| TORRENT-02 | High/medium | `Piece` eagerly allocated `numPieces × blocksPerPiece` status entries, allowing a moderate metainfo to reserve tens of GiB. `ParsePieces()` now enforces `MAX_TORRENT_BLOCKS` before constructing pieces. | Parser budget regression pending. |
| TORRENT-03 | Medium | `TorrentsDHT::HandleResponse()` erased a query iterator after handlers could insert a new query and rehash the map. The entry is extracted before dispatch and the mapped tuple stays alive locally. | DHT rehash/iterator regression pending. |
| TORRENT-04 | Medium | `Piece::Load()` published a buffer after the first fragment and could expose partially loaded file data. A loading flag now prevents `SendRequestedBlock()` from serving a piece while disk I/O is incomplete; buffers are still zero-initialized. | Multi-fragment load test pending. |
| TORRENT-05 | High | Torrent paths from metainfo/magnet could reach `std::fstream`, `create_directories`, `resize_file` and rename/delete operations without component-wise containment. `IsSafeRelativePath`, `IsPathWithinRoot`, `OpenNoFollowAt`, anchored create/read/write helpers and `CreateDirectoriesWithinRootNoFollow` now reject absolute/`..` paths, symlink/reparse components and non-regular leaves; resume reads are bounded. | `test-torrents-security` covers traversal, symlink leaves/directories, truncated parts and resume boundedness; live TOCTOU/Windows tests pending. |
| TORRENT-06 | Medium-low | `TorrentFile::Save/Load` used stream `seek`/`read` without exact bounds, and a short read could leave a buffer partially uninitialized. Reads/writes now use `pread`/`pwrite`-style exact operations with overflow-safe range checks and reject truncated files; atomic resume writes use the no-follow helper. | `test-torrents-security` includes out-of-range, wrap and truncated-file cases; ASan/Windows pending. |

Residual: `Piece::m_Data`, `m_Blocks` and `m_LastActivityTimestamp` are now
mutex-guarded, but they still have multiple thread owners; a single-owner/
immutable-buffer handoff is required before claiming TSAN cleanliness. On Windows the reparse check before CRT
`_wopen` retains a small TOCTOU window, hard links are not distinguishable
portably, and the resume writer still anchors only its parent directory (not
every component) when the torrent root is not supplied.

## IPC, shutdown and Android JNI

| ID | Severity | Finding and correction | Regression / validation |
|---|---|---|---|
| IPC-01 | Medium | `SCM_CREDENTIALS.pid` was compared with the fixed `SO_PEERCRED.pid`; after `fork()+exec()` the child PID legitimately differs, rejecting worker replies and causing restart loops. The check now compares stable UID/GID only. | Existing `test-privsep-log` Ping/Pong is the regression; Linux run pending. |
| IPC-02 | Low | `cmsghdr` storage was plain `char[]` with no alignment guarantee. Both ancillary buffers now use `alignas(struct cmsghdr)`. | UBSan alignment check pending. |
| IPC-03 | Low | `fdopen()` failure left the transferred descriptor open because `messageFd` was cleared unconditionally. It is cleared only on success, so the error path closes it. | Linux fd-count regression pending. |
| DAEMON-01 | Low | `DaemonUnix::pidFH`/`gracefulShutdownInterval` were uninitialized until `start()`; an early `stop()` could `fstat`/`close` a garbage descriptor. Both have safe initializers. | Start-failure/stop regression pending. |
| JNI-01 | High/medium | Several `JNIEXPORT` functions let C++ exceptions cross into ART. `startDaemon`, `getDataDir`, `stopDaemon`, `reloadTunnelsConfigs`, data-dir/language setters and `getStatusSummary` now contain a `try/catch` boundary. | Instrumentation fault-injection/static JNI boundary check pending. |

Residual: Android still uses same-process `LocalBinder`; JNI object/global
reference tests remain device/emulator-only. Full TSan coverage of restart and
reload paths is still pending.

## TUI configuration snapshot (read-only)

| ID | Severity | Finding and correction | Regression / validation |
|---|---|---|---|
| TUI-008 | Low/medium | The TUI had no way to inspect effective router options without opening configuration files, while any write path would bypass validation and the main-loop command queue. `I2PDTUI/1 CONFIG` now returns a bounded, typed, server-side allowlist snapshot; it exposes no keys, credentials, paths or URLs and has no `CONFIG-SET` command. | Protocol vectors added to `test-tui-protocol`; no live TUI/socket run yet. |
| TUI-009 | Medium | Administrative writes could have become an arbitrary router control channel. Version 1 exposes a closed typed set (`ADMIN TRANSIT`, `LOGLEVEL`, `RESEED`, `SHUTDOWN`, `RELOAD TUNNELS`, `BANDWIDTH`, `SHARE`) dispatched through a fixed daemon-owned queue; each validates its action/range and rejects trailing arguments. There is still no generic configuration option, path, URL, file or command operation. | Parsing vectors in `test-tui-protocol`, queue/socket dispatch in `test-tui-ipc-regression`; live TUI/Windows validation pending. |

Residual: the snapshot is effective startup configuration, not a live
reconfiguration API. The typed daemon queue now exists for a closed admin set,
but generic configuration editing still requires a separately reviewed,
validated and atomically persisted interface with explicit restart semantics.

## Tooling and IPC regression coverage

| ID | Severity | Finding and correction | Regression / validation |
|---|---|---|---|
| TOOL-006 | Low | Per-tool testing stopped at a smoke path; `b33offlinekeys` also returned success when its output file could not be written. A CMake-only `tools-regressions` test now covers key/address/registration/family/base64/offline/vanity CLI bounds, and the write failure exits non-zero. | `tools-regressions` registered with a 600 s timeout; CI execution pending. |
| TOOL-007 | Medium | Unbounded stdin/path reads and private key outputs with default permissions could expose or exhaust resources. `b33address` and `autoconf_i2pd` now bound input; POSIX `WriteFile` uses `O_NOFOLLOW`, `0600` and mode tightening; `b33offlinekeys` and `x25519` wipe temporary secret buffers; `famtool` restricts its unencrypted PEM key. | `tools-regressions` oversized-stdin/scripted-autoconf vectors; POSIX permission/ASan/Windows ACL execution pending. |
| PRIVSEP-013 | Low | Descriptor transfer, missing `SCM_CREDENTIALS`, oversized/duplicate ancillary data, malformed frames, queue pressure and close/reuse paths were not directly covered. `test-privsep-linux-ipc` and expanded `test-privsep-log` cases now exercise these paths and descriptor counts. | Linux CI/ASan/TSan pending; cross-UID rejection still needs a second account/root. |

## Session 29 — stateful parser, ownership and lifecycle pass (static)

These corrections were prepared after the earlier audit table. They are
**implemented by static analysis only**; no compiler, sanitizer, fuzzer or
platform run has validated them.

| ID | Severity | Finding and correction | Regression / validation |
|---|---|---|---|
| SSU2-04 | Critical | Handshake payloads reached the full data-phase block dispatcher. `ValidateSSU2PayloadBlocks()` now applies a per-state whitelist (TokenRequest, SessionRequest, SessionCreated, Retry, SessionConfirmed and established data phase), rejects bad DateTime/ACK/termination sizes before dispatch, and `HandlePeerTest`, `HandleAddress` and `HandleI2NPMsg` independently require an established session with a known identity. | `test-ssu2-preauth` and `fuzz-protocol-bounds` prepared. |
| SSU2-05 | High | Pre-auth token and PeerTest bookkeeping could grow until periodic cleanup. Incoming/outgoing tokens, recently-connected endpoints and pending PeerTest nonces are bounded; SessionConfirmed supports the protocol's multi-fragment range with duplicate/overlap checks. | `test-ssu2-control-plane`/fragment bounds; ASan/CI pending. |
| SSU2-06 | High | `SendData()` used packet number 0 as both the first valid packet and the failure result. It now returns `bool` plus an out-parameter, refuses packet-number exhaustion, checks every X25519 `Agree()` result (including low-order points), and separates the SOCKS5 handshake buffer from the outgoing UDP header. | Loss/short-write and proxy tests pending. |
| SSU2-07 | Medium | `m_OnEstablished` was assigned and invoked concurrently; callbacks now use a mutex/exchange and append without losing an earlier callback. Packet batches use an exception-safe pool guard and `FlushData()` checks the terminal state. | TSan callback/shutdown tests pending. |
| CRYPTO-01 | Critical | ECDSA DER encoding wrote into `keyLen + 8` bytes without an output capacity; a P-521 signature can require 141 bytes and the old buffer held 140. The encoder now asks OpenSSL for the exact length, checks the write, and the verifier/signer use it consistently. | P-521 DER regression in `test-signature-identity-hardening`; `fuzz-signature-der` prepared. |
| CRYPTO-02 | High | AES ECB/CBC wrappers ignored OpenSSL return values and used an indeterminate output length. Checked variants validate alignment/`INT_MAX`, fail closed on null/misaligned input, and wipe only output whose length is known to be valid. Tunnel encryption/decryption clears the output on failure rather than exposing a partially transformed frame. | `test-crypto-audit-bounds`; ASan pending. |
| CRYPTO-03 | High | X25519 accepted all-zero shared secrets from low-order peers. `Agree()` now clears and rejects them; ECIES P-521 coordinate buffers were enlarged to 128 bytes and ECIES operations reject null/invalid points and failed checked AES. | Crypto audit regression; OpenSSL/LibreSSL CI pending. |
| CRYPTO-04 | Medium | Lazy `GetEd25519()` and GOST curve construction used unsynchronised check/reset. Function-local statics/`call_once` now guarantee a stable object; DSA/ECDSA key generators report failure and critical random-generation failures no longer publish predictable key material. | Provider/error-injection tests pending. |
| TORRENT-07 | High | `Piece` state (data buffer, block map, sending/loading/verified flags) is now guarded by a per-piece mutex. Corrupt complete pieces rebuild all blocks as Missing; resume bitfields no longer imply verification; block requests must match the exact aligned range. | `test-torrents-security`, `test-torrents-concurrency` prepared. |
| TORRENT-08 | Medium | The 256 TiB limit was computed in `size_t` and could wrap on ILP32; peer frames whose 4-byte prefix did not fit the receive buffer could stall a connection. The limit is a `uint64_t` constant and the frame length is checked before storing `m_NextMsgLength`. | Bencode/framing regressions and `fuzz-torrents-parsers` prepared. |
| TORRENT-09 | Medium | Torrent tunnel teardown ran from the caller thread against service-owned timers/DHT/sessions, and the disk worker could be left joinable. Teardown is marshalled to the service with a bounded wait, the destructor joins the disk worker, and torrent maps are snapshotted under their mutex. | Shutdown/restart and TSan tests pending. |
| CLIENT-02 | High | I2CP `CREATE SESSION` overwrote the session ID before collision-free insertion, and service-owned sockets/queues could be terminated from a destination thread. IDs are reserved atomically, `Terminate` is marshalled to the I2CP service, SAM writes/accept queues are serialized, and destination stop uses a self-reference plus deferred join. | `test-i2cp-session-lifecycle`, `test-sam-stream-lifecycle`, `test-clientcontext-registry`; CI pending. |
| JNI-02 | High | Multiple `JNIEXPORT` entries could let C++ exceptions reach ART; status iteration read live RouterInfo addresses. All entries now share a try/catch frontier, daemon lifecycle calls are serialized, and status addresses are copied from the signed buffer snapshot. | JNI boundary/status tests pending; Android instrumentation still required. |
| GLOBAL-01 | Critical | `TunnelPool::TestTunnels` created replacement tunnels while holding the container mutex (zero-hop self-deadlock); tunnel/pool cancellation, RNG, TCSR, latency and restart state were unsynchronised. Creation now happens outside the pool locks, snapshots protect readers, counters are atomic, and `Stop()` clears runtime maps/exploratory state. | `test-tunnelpool-zero-hop`, `test-tunnelpool-detach-race`, `test-tunnels-restart`; TSan/CI pending. |
| GLOBAL-02 | High | NTP time offset, NTP start/stop, RouterContext service publication, static key creation, NetDb lifecycle and Logger start/stop had unsynchronised fields or discarded futures. They now use atomics, `call_once`, owned futures, serialized lifecycle transitions and bounded waits. | `test-timestamp-offset`, `test-ntp-timesync-lifecycle`, `test-routercontext-service-lifecycle`, `test-routercontext-static-keys`, `test-log-lifecycle`; CI pending. |
| IPC-04 | High | Channel sends could block past their timeout on atomic sockets; TUI clients could hold IPC forever; child reaping could block indefinitely after SIGKILL. Sends/receives use absolute deadlines with non-blocking I/O, TUI connections have request/duration/stop budgets, and child waits use pidfd/WNOHANG bounded polling. | `test-privsep-linux-ipc`, `test-privsep-log`, `test-tui-ipc-regression`; Linux/OpenBSD/Windows CI pending. |

Residual after this pass: `GetRouterInfo()` still exposes the live object to
compatibility callers; SSU2 sockets and some Asio timer callbacks remain shared
between service threads; Streaming still holds pool-owned `Packet*` containers;
BitTorrent `m_Data` now has a per-piece mutex but not a single-owner immutable
handoff, so TSan cleanliness is not claimed; I2CP/SAM output ordering and
`RunnableService` destruction from its own worker thread require a dedicated
executor/self-join contract; `GOSTR3411_2012_CTX` and secret cleansing need a
separate lifetime pass; Windows reparse/hard-link/rename races remain.

## Validation status

Prepared but **not executed** for this pass:

- New/updated unit probes from the earlier pass and this pass:
  `test-ssu2-control-plane`, `test-ssu2-preauth`,
  `test-routerinfo-update-transaction`, `test-routercontext-update-race`,
  `test-routercontext-service-lifecycle`, `test-routercontext-static-keys`,
  `test-torrents-security`, `test-torrents-concurrency`, `test-tui-protocol`,
  `test-privsep-linux-ipc`, `test-privsep-log`, `test-http-proxy-limits`,
  `test-i2cp-session-lifecycle`, `test-sam-stream-lifecycle`,
  `test-clientcontext-registry`, `test-tunnelpool-zero-hop`,
  `test-tunnelpool-detach-race`, `test-tunnels-restart`,
  `test-timestamp-offset`, `test-ntp-timesync-lifecycle` and the
  `tools-regressions` CMake test.
- ASan+UBSan, TSan and the eight libFuzzer targets (including
  `fuzz-signature-der`) through the manual `build.yml`/`fuzz.yml` workflows.
- OpenBSD/LibreSSL, Windows, Android Unit/Lint/APK/instrumentation and live
  Java/I2P peer tests.

Static checks performed in the working tree: `git diff --check`,
`build-system-static`, documentation-link validation, workflow linting
(yamllint/actionlint/zizmor) and the static interoperability-fixture validator.
No dynamic result is attributed to the current uncommitted tree.

## 2026-10-21 client hardening continuation

Implemented by static analysis; pending CI compilation and pending CI tests:

- Address-book datagram replies previously consumed a nonce without checking
  the authenticated sender. `AddressLookupRequests`, used by the actual
  lookup path, now binds that nonce to the expected resolver and normalized
  name. Wrong senders leave the legitimate request intact. Replies must use
  resolver/response ports 53/54, and nonce generation fails closed on RNG
  failure. The registry caps outstanding entries at 256, expires them after
  120 monotonic seconds, rejects duplicates/collisions and clears at shutdown.
  `test-addressbook-case` prepares identity, replay, collision, normalization,
  capacity and expiry cases against this production registry. Authentication
  remains the existing Datagram1/Datagram2 signature-verification boundary.
- I2PTunnel HTTP header state is extracted into `I2PTunnelHTTPHeader` and
  used directly by the socket connections. Request and response state and
  output are independent; incomplete lines and Connection/Upgrade state
  survive fragment boundaries. Socket writes borrow stable parser-owned
  output, while stream sends retain their existing synchronous copy contract.
  Responses gain the same bounded incremental processing as requests.
  `test-i2ptunnel-http` prepares split-input, binary-body and bounds cases.

The manual Linux workflow selects these regressions explicitly in addition to
the complete CTest suite; they carry security/memory labels. Manual sanitizer,
OpenBSD/LibreSSL, Windows and Android native compilation and controlled peer/
socket tests remain necessary. No compiler, test, project validator or workflow
was executed for this continuation. These changes do not close global
privilege separation, address-book subscription lifecycle or live Java parity.

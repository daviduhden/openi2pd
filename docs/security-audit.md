<!--
See the LICENSE file at the top of the project tree for copyright
and license details.

openi2pd: security audit.

Documents project documentation.

-->

# Security audit

Scope: all C++ in `libi2pd`, `libi2pd_client`, `daemon`, `i18n`, `tui` and the
maintained build/packaging files. This is a static review, not a proof of
absence of defects and not a substitute for fuzzing or sanitizer runs.

Status legend: **Fixed** = code changed; **Reviewed** = inspected, no change
needed or change deferred with rationale; **Removed** = attack surface deleted.

## Findings

### S-01 — Lazy base64 reverse table race and silent corruption (High, Fixed)

- **Location:** `libi2pd/Base.cpp`, old `iT64Build`/`Base64ToByteStream`.
- **Cause:** `static int isFirstTime` and a mutable 256-byte table were
  initialized on first decode. Concurrent first use was a data race. Bytes
  outside the alphabet mapped to `-1`, then were used as unsigned and combined
  into output bytes.
- **Impact:** Corrupted RouterInfo/LeaseSet keys and undefined behavior during
  startup; malformed remote data was accepted as if valid.
- **Fix:** `consteval` reverse table, strict alphabet and padding validation,
  complete length checks, `std::span` destination. Regression:
  `tests/test-base-64.cpp` (invalid alphabet, misplaced padding, short span).

### S-02 — Truncated SU3 header used uninitialized values (High, Fixed)

- **Location:** `libi2pd/Reseed.cpp`, `ProcessSU3Stream`.
- **Cause:** Header fields were read with unchecked `istream::read`; on a short
  file the local variables were used uninitialized. `tellg()`/content length
  arithmetic could produce a huge allocation.
- **Impact:** Undefined behavior and potential crash/OOM from a malformed
  reseed file.
- **Fix:** Exact-read helper, checked skips, magic comparison, signature length
  cap (8192), signer-ID bound, checked `headerLen + contentLength` arithmetic,
  `std::vector` buffers, seek failure checks. Static only; no malicious SU3
  fixture test yet.

### S-03 — netDb counters raced with writers (Medium, Fixed)

- **Location:** `libi2pd/NetDb.hpp`.
- **Cause:** `GetNumRouters`, `GetNumFloodfills`, `GetNumLeaseSets` read
  `unordered_map`/DHT size without the corresponding mutex. The removed web
  console and I2PControl call these from service threads.
- **Impact:** Data race/undefined behavior; historically an administrative
  read could observe an inconsistent value.
- **Fix:** Each getter takes its container mutex. `IsReseeding()` is atomic and
  reseed requests are serialized with a mutex.

### S-04 — Unlocked peer map exposed to administrative code (Medium, Fixed)

- **Location:** `libi2pd/Transports.h`, `I2PControlHandlers.cpp`.
- **Cause:** `GetPeers()` returned a reference to `m_Peers`; `.size()` was read
  without `m_PeersMutex`.
- **Impact:** Data race with peer cleanup/insertion.
- **Fix:** `GetNumPeers()` and `GetPeersSnapshot()` lock the map and return
  values. I2PControl uses `GetNumPeers()`; the TUI uses the snapshot. The old
  reference accessor remains for API compatibility but is not used by new code.

### S-05 — HTTP numeric parsing accepted malformed lengths (Medium/High, Fixed)

- **Location:** `libi2pd/HTTP.cpp` (`content_length`, chunk merger), URL port.
- **Cause:** `strtoul` without end-pointer, `std::stoi` on chunk size, no range
  check on URL port.
- **Impact:** Request/response splitting or divergent interpretation between
  this proxy and the next hop; malformed chunk sizes could be accepted.
- **Fix:** `std::from_chars` over the complete field, overflow checks, explicit
  maximum for `Content-Length`, hex-only chunk sizes with extensions, maximum
  chunk size, `std::vector` instead of `new[]`, full numeric port validation.
  Regression tests in `test-http-req`, `test-http-merge_chunked`,
  `test-http-url`.

### S-06 — Valid HTTP headers rejected (Interoperability, Fixed)

- **Location:** `libi2pd/HTTP.cpp`, `parse_header_line`.
- **Cause:** Required at least one space after `:`.
- **Impact:** `Host:example.i2p` and similar valid requests failed.
- **Fix:** Optional whitespace, token validation for field names, tests.

### S-07 — Integrity comparisons not constant-time (Low, Fixed)

- **Location:** `libi2pd/Crypto.cpp` (ElGamal/ECIES decrypt integrity hash),
  `libi2pd/Garlic.cpp` (payload hash).
- **Cause:** Plain `memcmp` for verification.
- **Impact:** Timing side channel on equality of integrity digests. No direct
  key recovery was demonstrated; the change is defense in depth.
- **Fix:** `CRYPTO_memcmp` for verification. Public-key / address comparisons
  remain `memcmp` because they compare public values.

### S-08 — Administrative HTML console attack surface (High, Removed)

- **Location:** `daemon/HTTPServer.cpp`, `HTTPServerResources.h`,
  `contrib/webconsole`.
- **Cause:** A network-facing administrative server with authentication,
  tokens, HTML rendering, command endpoints and a large parser surface.
- **Impact:** Any defect in authentication, escaping or command handling was
  remotely reachable on localhost and dangerous if misconfigured on a public
  address.
- **Fix:** Entire console removed, including options and Win32 menu entry. I2P
  HTTP proxy, HTTP tunnels, reseed and I2PControl are separate and retained.

### S-09 — New local IPC surface (Mitigated, new code)

- **Location:** `daemon/TuiServer.cpp`, `libi2pd/TuiProtocol.h`.
- **Controls:** 0600 socket, same-UID peer credentials, refusal to unlink a
  non-owned/non-socket path, bounded request/response sizes, numeric bounds,
  per-connection timeouts, one sequential client, `MSG_NOSIGNAL`, read-only
  snapshots, a closed typed administrative set (`ADMIN TRANSIT`, `LOGLEVEL`,
  `RESEED`, `SHUTDOWN`, `RELOAD TUNNELS`, `BANDWIDTH`, `SHARE`) executed through
  a fixed daemon-owned queue, no secret fields, no command execution and no
  generic configuration writer.
- **Windows transport (static):** `daemon/TuiServerWin32.cpp` uses a hashed
  local named-pipe name, `FILE_FLAG_FIRST_PIPE_INSTANCE` against squatting, a
  protected DACL limited to the daemon user/SYSTEM/Administrators, and
  `PIPE_REJECT_REMOTE_CLIENTS`. `tui/TuiClientWin32.cpp` additionally checks
  that the server image is `i2pd.exe` and that the server user is the client
  user or `LocalSystem`, or that either token is a member of Administrators. `tests/test-tui-pipe-name.cpp`
  covers the name derivation and `tests/test-tui-win32-pipe.cpp` covers the
  pipe/DACL primitives.
- **Residual:** A same-UID process is already inside the trust boundary (it can
  read `router.keys`), and systems without `SO_PEERCRED`/`getpeereid` fall back
  to filesystem permissions. The Windows DACL, service-account behavior and
  cross-user rejection have not been executed on a Windows host. No live
  penetration test was performed.

### S-10 — Tunnel/router status reads are not all atomic (Low/Medium, Reviewed)

- **Location:** `Tunnel::GetState`, creation time, router status fields used by
  the TUI.
- **Cause:** Writers update plain integers; administrative readers take
  snapshots of `shared_ptr` lists but not atomics.
- **Impact:** Technically a data race under the C++ memory model; practical
  impact is a stale status value.
- **Status:** Not changed because converting all protocol state to atomics
  needs a dedicated concurrency pass. ThreadSanitizer validation is pending and
  listed in `testing.md`.

### S-11 — OpenBSD sandbox and TUI path (Reviewed, Fixed integration)

- **Location:** `daemon/Daemon.cpp`, `daemon/UnixDaemon.cpp`.
- **Change:** `tui.socket` is canonicalized with other configured paths and
  added to unveil with `rwc`. No new pledge promise was added; the existing
  `unix`/`stdio` promises cover `AF_UNIX` socket creation.
- **Residual:** A custom socket path outside the data directory and not listed
  in `openbsd.unveil_file` will fail to start; the daemon logs a warning and
  continues headless. This is the intended minimum-privilege behavior.

## Memory-safety review notes

- Replaced manual `new[]`/`delete[]` in the touched HTTP, base64 and SU3 paths.
- No new raw owning pointers were introduced. `TuiServer` owns an `int fd`
  with explicit close in `Stop`/destructor and `std::jthread`.
- `std::span`/`string_view` parameters are used only for the duration of the
  call; no view is stored.
- `AppendField` and the parser enforce field-name length and payload limits
  before `std::string` growth.
- The log ring stores a bounded copy and never retains caller strings.
- Integer growth in the TUI log sequence uses `uint64_t`; record counts are
  bounded to 200 per request and 2,000 retained locally.

## Concurrency review notes

- `Log::Append` copies under `m_RecentMutex`; `GetRecent` copies under the same
  mutex. The existing queue remains the writer path.
- `NetDb::Reseed` uses `try_lock` plus an atomic flag so a second request is
  ignored rather than racing `Reseeder` construction.
- `PeerSnapshot` copies identifiers and counters while holding
  `m_PeersMutex`; no `shared_ptr` escapes.
- `TuiServer::Stop` requests stop and joins before closing the listening fd.
  The accept loop polls with a 200 ms timeout, so cancellation is bounded.

## Cryptography review notes

- No cryptographic primitive was implemented or modified.
- AEAD tags are checked by OpenSSL; signature verification goes through
  `Verifier` implementations.
- `RAND_bytes` remains the randomness source; no fallback RNG was added.
- Secret cleanup uses existing `OPENSSL_cleanse`-based buffers; no new secret
  copies were added. The TUI protocol never receives key material.
- OpenSSL/LibreSSL capability guards were retained; no PQ behavior was invented.

## Not audited dynamically

- ASan/UBSan/TSan runs. The manual CI sanitizer matrix is prepared but has not
  executed the current tree.
- Fuzzing of NTCP2/SSU2/I2NP/RouterInfo/LeaseSet/SU3/TUI inputs. Seven
  libFuzzer targets and a manual smoke workflow are prepared; no fuzz run is
  claimed for the current tree.
- Live Java interoperability.
- OpenBSD pledge/unveil under a non-default user and datadir.
- Windows build and runtime.
- Multi-UID rejection (a same-UID live TUI session and malformed requests were
  exercised; a second UID was not available).

See `testing.md` for exact commands and the honest status of each check.

## Recent findings (post-initial audit)

### S-12 — `vain` found-state and key-selection races (Medium, Fixed)

- **Location:** `tools/vain.cpp`, `tools/vanity.hpp`.
- **Cause:** plain `bool found`, `FoundNonce`, `hashescounter` and `fKeyId`
  were written by all worker threads; the thread-to-key mapping also reused
  key 0 for two worker indices.
- **Impact:** data races, wrong key file association and potential undefined
  behavior during vanity generation.
- **Fix:** atomic found/hash/nonce/key-id state, single-winner
  `exchange` publication, per-thread key index. Remaining raw key-buffer arrays
  are documented as a pending RAII task (`DEF-007`).

### S-13 — Log file write removed from the core (Medium/High mitigation, Implemented)

- **Location:** `privsep/LogWorker.*`, `daemon/Daemon.cpp`, `libi2pd/Log.*`.
- **Cause:** the monolithic core opened and wrote logs while also handling
  network traffic and long-term keys.
- **Fix:** optional isolated log worker; core sends structured records over a
  bounded `AF_UNIX/SOCK_SEQPACKET` channel. Worker file mode is `umask 0077`,
  OpenBSD `unveil`/`pledge` exclude `inet`/`dns`. Failure drops logs rather than
  reopening the file in the core.
- **Residual:** the rest of the process still combines network, persistence and
  keys; this is not full privilege separation.

### S-14 — JNI string handling (Low/Medium, Fixed)

- **Location:** `android/app/src/main/cpp/i2pd_android.cpp`.
- **Cause:** `GetStringUTFChars` results were not checked and not managed by
  RAII.
- **Fix:** `ScopedUtfChars` releases on all paths; null/exception paths return
  without using the pointer. Further JNI lifecycle/restart testing remains
  pending.

## 2026-10-18 memory-safety pass

A focused SSU2/NTCP2, Ratchet/Garlic, LeaseSet/netDb, asynchronous client,
BitTorrent, filesystem, TUI and IPC/JNI review produced confirmed corrections
in [`docs/security/memory-safety-2026.md`](security/memory-safety-2026.md).
The changes are implemented by static analysis only; sanitizer, fuzz, live
peer, OpenBSD, Windows and Android execution remain pending manual CI. The
report explicitly lists residual races in SSU2 session ownership, the remaining
live `RouterContext::GetRouterInfo()` readers and Asio timer/socket ownership,
I2CP/SAM output sequencing, BitTorrent piece ownership and Windows filesystem
TOCTOU rather than claiming a clean dynamic result. The TUI configuration
snapshot surface is read-only and allowlisted; the only writes are the typed,
atomic transit and daemon-log-level operations. No key, credential, arbitrary
path or URL is exposed.

## 2026-10-21 format and buffer-safety pass

Static review confirmed and corrected the remaining first-party unsafe string
patterns. No dynamic result is claimed.

- **S-15 — interface-name copy (Low/Medium, Fixed):** `util.cpp` used an
  uninitialised `ifreq` and `strncpy(..., IFNAMSIZ - 1)`, which could leave the
  name unterminated. It now uses zero-initialised storage and bounded
  `CopyString`.
- **S-16 — Boost.Asio scope-ID formatting (Medium mitigation, Fixed):** Asio's
  POSIX `inet_ntop` path uses `sprintf`/`strcat` for IPv6 scope IDs. The
  project now formats addresses and endpoints through `AddressFormat.h`,
  `LogPrint` overloads, and selects Asio's bounded `snprintf` fallback with
  `BOOST_ASIO_HAS_SNPRINTF=1`; the project path avoids Asio's remaining
  `strcat`. No system header or linker warning suppression was used.
- **S-17 — i18n two-pass formatting (Low/Medium, Fixed):** `tr`/`ntr` now use a
  checked vector-backed formatter with negative/short-write handling and no
  empty-string `front()` access.
- **S-18 — SOCKS domain length (High, Fixed):** a zero-length SOCKS5 domain
  could wrap the `uint8_t` length and write past the 255-byte union member.
  The buffer now rejects zero-length and over-capacity input and is covered by
  a prepared regression.
- **S-19 — certificate/log/hex truncation (Low, Fixed):** certificate-name
  extraction is zero-initialised and terminated, `strftime` failure falls back
  to a bounded timestamp, and torrent info-hash hex formatting uses a fixed
  nibble table without `snprintf`.

- **S-20 — tunnel DatabaseStore reply reflection (Medium, Fixed):** a
  DatabaseStore carrying a nonzero reply token was accepted after arriving
  through the router's exploratory inbound tunnel, allowing the last hop to
  make the router emit DeliveryStatus messages to an arbitrary reply
  hash/tunnel. Direct stores and the `0xFFFFFFFF` no-reply marker remain valid;
  tunnel-delivered token-bearing stores are now dropped before any reply is
  built, matching the Java exploratory-tunnel policy.

The new address, copy-string, i18n, SOCKS and tunnel-DSM tests are prepared for
manual CI; no compiler, CTest, sanitizer, fuzzer or runtime validation was
performed.

## 2026-10-21 OpenSSL 4 and ownership pass

- **S-21 — provider/PQ capability drift (High, Fixed statically):** a
  compile-time `OPENSSL_PQ` build could publish a `pq` address even when the
  active provider could not create that ML-KEM parameter set, causing failed
  sessions. Publication and session selection now use runtime
  `CanPublishPQ()`/`HighestSupportedPQVersion()` gating.
- **S-22 — family trust-path ownership (High, Fixed statically):** the family
  verifier/signing path used unchecked `SSL_CTX_new`, `EVP_DigestSign`,
  `d2i_ECDSA_SIG` and `ECDSA_SIG_get0` results and could dereference null or
  uninitialised data. It now uses scoped OpenSSL owners, checked returns and
  strict DER consumption.
- **S-23 — TLS role/version and control certificate (Medium, Fixed
  statically):** the local SSL server-tunnel context was created as a TLS
  server while the code performs a client handshake. It is now `tls_client`;
  control/admin contexts use explicit roles, TLS 1.2 floors and SHA-256
  certificates. `RAND_bytes` failure now refuses to issue a control token.
- **S-24 — address-book persistence and identity bounds (Medium/High, Fixed
  statically):** index replacement uses a same-directory temporary, unreadable
  or oversized identity files are rejected, stream errors do not loop, and
  failed adds clean up unreferenced identity files. No filesystem fault
  injection or ASan run has been performed.

No compiler, CTest, sanitizer, fuzzer, live TLS/provider or filesystem fault
run was performed; the changes remain pending manual CI.

## 2026-10-21 memory-safety audit (SSU2/NTCP2, ratchet, netDb, torrent, IPC, JNI)

Static subagent audits of the peer-controlled paths found the following. No
dynamic validation exists for any item.

### Fixed statically

- **S-25 — Family signature fail-open (High):** a known family with a malformed
  or non-64-byte signature returned true. It now fails closed; unknown families
  retain the “no local policy opinion” result.
- **S-26 — Router garlic/DeliveryStatus bounds (Medium):** the garlic length
  was checked against the whole I2NP length rather than the payload after the
  four-byte prefix; DeliveryStatus read its token without a minimum length.
  Both now reject truncated messages before tag lookup or token use.
- **S-27 — Garlic clove pointer/length handling (Medium):** encrypted,
  destination, tunnel and router clove fields are validated before pointer
  advancement; the I2NP body must contain a full header; allocation failure is
  handled; the trailing clove fields are bounded before advancing.
- **S-28 — NTCP2 SipHash setup (Medium):** `SetSipKeys` now checks every EVP
  return and refuses a partially initialized MD context; both handshake
  directions terminate instead of using it.
- **S-29 — Torrent piece/DHT bounds (Medium/Low):** peer `have`, choke and
  terminate paths check the piece count before `GetPiece`; empty DHT node
  vectors no longer feed a possibly-null `string_view`.
- **S-30 — Offline-key chaining (Medium, tool path):** `CreateOfflineKeys`
  refuses an already-offline key, preventing a transient signature length from
  overflowing a buffer sized with the destination signature length.
- **S-31 — Reseed/HTTP malformed input (Low):** truncated SU3 file names are
  rejected using `gcount`; HTTP header values reject C0/DEL controls.
- **S-32 — Android SAF/JNI exception ordering (Medium):** SAF root/read/remove
  no longer call JNI with a pending Java exception; scalar JNI calls clear a
  pending exception; native start refuses an already-running/stopping daemon;
  the torrent recheck action catches an invalid torrent id.
- **S-33 — IPC/TUI bounds (Low/Medium):** torrent-detail comments are capped;
  admin-result messages carry an explicit length; rest-of-line arguments reject
  control bytes. A queued admin command whose waiter expired is still executed
  exactly once (only the result is discarded), remote error codes survive the
  client, and destructive prompts cannot be committed while the terminal is too
  small to display them.

### Residual findings not fixed

- `SSU2Server` memory pools can be destroyed before deferred session handlers
  release pooled objects; `SSU2Session::m_Address`/`m_RemoteEndpoint` are read
  and written from different services; the per-session fragment budget is
  count-only; `RouterInfo` introducer/address-slot containers are mutated in
  place while readers copy elements; ratchet session and destination
  AEAD/HKDF/RNG contexts are shared across threads; datagram receivers capture
  raw `this` after being copied out of their lock; several TUI/IPC and JNI
  teardown/contract issues remain.
- These are tracked as `SEC-025` and require TSan/ASan, shutdown stress,
  receiver-lifetime and provider-failure tests in manual CI. No compiler,
  CTest, sanitizer, fuzzer, device or network run was performed.

## 2026-10-21 concurrency and lifecycle audit

Independent static reviews covered callbacks/timers, network sessions, netDb,
BitTorrent, IPC workers and Android JNI.

### Fixed statically

- **C-01 — RunnableService self-join:** `StopIOService` no longer calls
  `join()` on the current service thread. Same-thread requests leave the
  joinable worker for a later external join; destruction from the handler
  detaches instead of terminating. Start remains idempotent.
- **C-02 — Transports shutdown:** running/online/bound state is atomic, `Start`
  is idempotent, timers are cancelled only after joining their owner, peer/session
  references are dropped while both server io_contexts are alive, and the peer
  map is cleared under its mutex.
- **C-03 — I2PControl restart:** the control service now calls `restart()` and
  uses an atomic running flag, eliminating the 100% spin/dead endpoint after a
  daemon restart. Daemon `running`, graceful countdown and transit-restore
  state are atomic.
- **C-04 — shared crypto scratch:** tunnel-build decryption uses a caller-owned
  `NoiseSymmetricState` instead of `RouterContext::m_CurrentNoiseState`;
  `Families` protects its key map with a shared mutex.
- **C-05 — SSU2 session termination:** a separate terminated latch prevents
  `Terminate()` and late `ProcessSessionCreated/Confirmed/Retry/HolePunch`
  paths from resurrecting a session; pending entries are removed and expired
  pending sessions are terminated rather than silently erased.

### Residual

- Raw `this` captures in `TorrentsTunnel` timers/posts, datagram receiver
  first-wins/teardown, `RouterInfo` address/buffer copy-on-write,
  global profile-storage cleanup synchronization, `Piece`/`TorrentFile`
  disk-domain ownership, SSU2 `m_Address`/`m_RemoteEndpoint` publication,
  fragment byte budgets, IPC worker cancellation and Android service-owner
  epochs remain open (`CONC-005`). No dynamic validation exists.

The subsequent CONC-009 static correction adds per-instance RouterProfile
locking, canonical cache publication and endpoint copies. Save/Load serialize
their IO and copy state under a short lock; persistence claims do not clear
later updates, failed writes restore dirty state, and failed loads retain the
previous state. Eviction requires sole map ownership so active shared holders
remain canonical. The postponed queue is inspected only under its mutex.
`test-router-profile-race` prepares concurrent access/publication/flushing and
dirty-claim coverage for manual TSan/CTest. CONC-010 adds immutable initialized
root authority, storage IO/cleanup serialization, flushed atomic replacement
and final postponed-update draining after producers stop. Cross-process
exclusivity, abandoned staging cleanup, hostile ancestor/reparse changes and
arbitrary retained mutators remain open. A directory-fsync error after rename
is ambiguous: the replacement may exist while the profile stays dirty.
`test-router-profile-storage` prepares isolated-root, format, cleanup-race and
failure-path coverage. No dynamic validation is claimed.

## 2026-10-21 cryptography continuation (static)

A read-only subagent pass covered Ratchet/ECIES, AEAD and key lifetime,
signatures/Elligator2/blinding, ML-KEM and OpenSSL/LibreSSL negotiation. No
compiler, sanitizer, provider run, Java peer or workflow was executed.

- **CRY-215** `GetRngSeed()` now sources its 32-bit mt19937 seed from
  `RAND_bytes` and falls back to a mixed monotonic value only on provider
  failure. `PeerOrdering` zeroes its key and fails closed if the CSPRNG or
  SipHash provider is unavailable.
- **CRY-216** multi-buffer ChaCha20-Poly1305 encryption returns `bool`, wipes
  every output on failure and NTCP2 terminates before sending or advancing the
  sequence number.
- **CRY-217** ratchet tag roots, intermediate symmetric keys, AES keys,
  ECIES session secrets and NTCP2 KDF/SipHash keys are cleansed on destruction.
- **CRY-218** a payload may carry at most one forward and one reverse NEXT_KEY;
  the global inbound ECIES tag map evicts expired/terminated entries and then
  arbitrary entries instead of permanently refusing all new tags.
- **CRY-219** every `ClientDestination` (not only I2CP) arms a one-second NSR/ACK
  fallback; one timer per session is enforced with an atomic flag.
- **CRY-220** LeaseSet2/EncryptedLeaseSet2 reject an RSA offline transient key
  (Java `LeaseSet2` policy) while Datagram2/Streaming keep Java's permissive
  policy.
- **CRY-221** Ed25519 blinding checks that the decoded key actually satisfies
  the curve equation; Java-valid low-order/all-zero encodings are not rejected
  merely for being low order.
- **CRY-222** GOST sign/verify normalizes `e = digest mod q` to 1 when zero
  (RFC 7091) and private scalars use `BN_clear_free`.
- **CRY-223** SSU2 regenerates ML-KEM state on each SessionRequest attempt and
  consumes an `IncompatibleVersion` Retry before the generic termination abort.
- **CRY-224** an explicit unsupported `i2cp.leaseSetEncType` list now fails
  closed rather than being replaced by defaults.
- **CRY-225** I2CP CreateLeaseSet2 private-key lengths are checked per EncType;
  unknown types and mismatched lengths are rejected, and the legacy 256-byte
  P256 storage is zero-filled before the 32-byte scalar is copied.

Elligator2 was compared line-by-line and showed no new correctness defect.
The NSR timer is implemented but its lifecycle test, the deliberate NS
empty-clove policy, provider PQ transcripts, type-5/7 NSR vectors, multi-key
7/4 sessions and all dynamic/sanitizer evidence remain pending. Do not describe these paths as compiled or validated.

## 2026-10-21 netDb/RouterInfo/LeaseSet continuation (static)

A second netDb-focused read-only pass covered RouterInfo, DatabaseLookup,
DatabaseSearchReply, floodfill request handling, LeaseSet/LeaseSet2,
EncryptedLeaseSet and MetaLeaseSet. No compiler, CTest, sanitizer, fuzzer,
live floodfill or Java peer was run.

- **DB-115** `I2NPMessage` now records `fromClove`; clove-delivered DSM/DLM no
  longer looks direct. Token-bearing stores from a tunnel/clove are rejected,
  including the legacy `0xFFFFFFFF` marker.
- **DB-116** client LeaseSet search replies are bound to queried peers, accepted
  once, capped at 16 followed hashes and filtered for self/sender/excluded
  duplicates.
- **DB-117/DB-118** bounded lookup (14/2 min per reply hash/tunnel) and flood
  (3/90 s per key) throttles were added; stores and acks are still processed
  when flooding is throttled.
- **DB-119** a fresh signature-valid zero-address RouterInfo is storable for 54
  minutes like Java but remains unselectable; load uses the same bound.
- **DB-120** RouterInfo persistence consumes one buffer-mutex snapshot, updates
  the body before publishing the flag, and cannot erase a concurrently installed
  update.
- **DB-121/DB-122** reseed preserves our RouterInfo/floodfill entry, is fenced
  by `Stop()`, and `Start()` uses locked router/floodfill counts.
- **DB-123** RouterInfo files are written to a same-directory temporary and
  renamed, preserving the previous valid cache on a failed write.
- **LS-122** encrypted LS2 rejects RSA blinded/transient signature types before
  any public-key operation.
- **LS-123** `PopulateLeases()` is virtual; LS2 is a no-op so the LS1 parser
  cannot clear a cached LS2.
- **LS-124** MetaLeaseSet effective expiry is clamped to the latest entry,
  matching Java `addLease()`.
- **LS-125** LS1 buffers are trimmed to the signed body plus signature, so
  trailing unauthenticated bytes are not stored or served.

Reply-key `0x12` remains intentionally fail-closed: Java 2.14.0 still has no
sender and proposal 154's derived-key KDF is TBD. Meta revocations are accepted
for length but not applied; Java peers currently verify only the zero-revocation
form, recorded as an intentional spec/implementation difference. Deep residual:
RouterInfo copy-on-write, shared profile-storage cleanup
and NetDbRequests raw-`this` lifetime. No dynamic evidence exists.

## 2026-10-21 tunnel-build/I2NP/Garlic continuation (static)

Four read-only audits covered tunnel construction/peer selection/time windows/
records, tunnel pool concurrency and recovery, I2NP framing and tunnel dispatch,
and garlic clove routing. No compiler, CTest, sanitizer, fuzzer, live tunnel or
Java peer ran.

- **TU-102..TU-106**: duplicate tunnel peers are rejected; zero receive/next
  transit IDs are dropped; zero-hop creation follows the selected path and
  publishes the replacement outbound tunnel; build-record shuffles advance the
  shared RNG; pre-expiry recreation accepts varied lengths.
- **I2NP-006..I2NP-010**: LOCAL at an OBEP is dropped; clove-delivered types are
  whitelisted; TunnelGateway reserves its header, rejects >65535-byte bodies and
  fails on overflow; inner I2NP expiration is checked; per-tunnel IV replay and
  bounded reassembly tables were added.
- **GAR-001..GAR-004**: local clove lengths are validated; ElGamal/ECIES clove
  freshness uses Java's -90/+240 s window; encryption failure propagates as null
  instead of cleartext; router wrapping null-checks its session.
- **CONC-006/CONC-007**: custom peer selectors are owner-bound and unregistered
  on Stop; explicit/trusted peer vectors are copied under a mutex.

Residual: adaptive build shaping/backoff, RouterInfo copy-on-write and
RouterProfile synchronization, live multi-process/tunnel evidence, and
transport-authenticated previous-hop loop checks for build records. Tunnel keys,
build records and hop Noise state remain core-only; no storage/IPC boundary was
widened.

## 2026-10-21 SAM/I2CP/Streaming/Datagram continuation (static)

A read-only pass covered SAM 3.2/3.3, I2CP session/key handling, Streaming packet
state and Datagram1/2/3. No compiler, CTest, sanitizer, fuzzer, Java peer or
client run was performed.

- **SAM-117..SAM-121**: per-port acceptors are disarmed/removed on teardown;
  closed acceptors no longer adopt a stream; DATAGRAM/RAW subsessions require
  PORT and reject duplicate protocol/port pairs; SESSION CREATE parameter
  failures use SESSION STATUS; HELLO accepts Java whitespace/case variants; and
  CloseSessions balances destination Acquire/Release.
- **I2CP-109..I2CP-111**: b33 HostReply checks the requested blinded store key,
  terminal MessageStatus has one owner, and CreateLeaseSet2 validates/stages all
  private keys before installing or publishing.
- **STR-108..STR-112**: saved pre-SYN tables are bounded, stream lookup is locked,
  ping/pong ports are preserved, invalid/FROM-less SYNs are not accepted, and a
  terminated stream cancels queued sends before touching the destination.
- **DG-108..DG-110**: unauthenticated Datagram3 cannot create/ack a session,
  wildcard receivers use last-wins replacement, and Datagram1/2 refresh their
  cached sender identity after key/offline changes.

Residual: I2CP key/owner publication under `i2cp.singlethread=false` remains a
TSan item; SAM `LISTEN_PROTOCOL`, UDP SEND_TAGS/EXPIRES and full b33 secret
authorization remain open. No dynamic evidence exists.

## 2026-10-21 user-services continuation (static)

HTTP/SOCKS, reseed and address-book audits produced and statically fixed:
case-insensitive `.i2p`/`.i2p.alt` classification, `.i2p.alt` lookup aliases and
`www.` fallback; loopback/private clearnet refusal and SOCKS IP-literal/port-0
policy; 5 MiB subscription bodies/10 000-entry caps and tombstone enforcement;
reseed TLS chain+hostname verification, 8 MiB SU3, 1 MiB per ZIP entry,
32 MiB inflated, 400-entry and 30-day network-bundle freshness bounds. No
compiler, CTest, sanitizer, fuzzer, live proxy/reseed, Android/device or Java
peer run exists. Remaining: HTTP/SOCKS authentication, full HTTP
response-direction limits, server-tunnel TLS CA/config, reload of changed tunnel
sections, per-tunnel admin inventory and `.i2p.alt` subscription semantics.

## 2026-10-21 multiprocess trust-boundary continuation (static)

Six read-only passes covered supervisor lifecycle, Linux worker bootstrap,
storage roots, router-core capabilities, network/socket ownership, crypto/key
custody and log-worker recovery. No compiler, CTest, sanitizer, fuzzer,
OpenBSD/Linux/Windows runtime or live-peer run exists.

- **Supervisor bootstrap**: the Linux supervisor previously installed a
  write-deny Landlock domain and an AF_INET-deny seccomp filter that were
  inherited by every `fork`+`exec` child. That silently broke the log worker's
  file sink and any future storage/network child. The supervisor now applies
  `ApplyLinuxSupervisorSandbox` (no-new-privs plus capability/bounding-set
  drop) and each worker applies its own role profile. This is a delegation
  bootstrap, not a claim that the supervisor has network/write isolation.
- **Restart recovery**: log-worker replacement has 250 ms exponential backoff
  capped at 30 s, eight consecutive attempts and a 60 s stable-period reset;
  exhaustion exits the supervisor. The core logger queue is capped at 8192
  records with 8192-byte truncation and a dropped counter.
- **Storage root handoff**: the OpenBSD torrent-storage broker receives a
  validated data root as inherited fd4 instead of re-opening a path after
  pledge. It uses `pledge("stdio proc exec sendfd")` with an `x`-only unveil of its
  frozen executable (current policy; earlier revisions used
  `stdio unix proc exec sendfd recvfd`); directory descriptors are never sent
  through `SCM_RIGHTS`.
- **Router storage recovery**: a worker-level write rejection (status 1) no
  longer poisons the IPC channel; only malformed frames/request IDs do. This
  prevents one ENOSPC/symlink rejection from disabling later persistence.
  Worker entry now requires the channel at descriptor 3, preventing a manual
  invocation from aliasing inherited root descriptor 4.
- **Startup pledge**: OpenBSD carries `exec` only during the startup worker
  window, including when `privsep.log` is disabled; the runtime policy still
  drops it.

Residual: the core still owns transport sockets, most netDb/profile/
address-book/destination-key writes and long-term keys. Network and crypto
workers remain policy-builder-only, and no single supervisor owns all workers.
Do not claim global privilege separation.

## 2026-10-21 IPC protocol consolidation (static)

Five read-only audits covered serialization, worker contracts, authorization,
buffers/backpressure and disconnect recovery. No compiler, CTest, sanitizer,
fuzzer, OpenBSD/Linux/Windows runtime or workflow run exists.

- **Typed/versioned frames**: protocol v2/min v1, typed `Error` replies with
  bounded detail, shared `EndpointRole` allowlist and fallible path encoders.
  Over-long/NUL paths no longer truncate silently to a different path.
- **Authorization**: every POSIX dispatch checks its role allowlist; Ping and
  Shutdown require empty payloads; storage request IDs must increase
  monotonically, so replay cannot repeat mutations. Adopted channels are
  revalidated for seqpacket type, peer UID and CLOEXEC.
- **Readiness/recovery**: log configuration uses a bounded Ping/Pong handshake,
  so queued configuration is not reported as a working sink. The supervisor
  returns nonzero on protocol violations and distinguishes clean shutdown.
  Broker capacity/path/replaced-root rejections are typed and non-fatal.
- **Bounds/backpressure**: logger sink drops are counted; zero-length
  seqpacket records are consumed rather than treated as EOF; imsg drains its
  buffered message queue; Windows oversized records close the pipe.
- **Residual**: automatic storage-client restart and handle-generation fencing,
  explicit transaction cancellation IDs, typed `Channel` error classes,
  Windows daemon integration and cross-UID/live-platform tests remain pending.

## 2026-10-21 OpenBSD supervisor/storage continuation (static)

Four read-only audits covered Linux storage parity, OpenBSD semantics, IPC
channels and per-process/network policy. No OpenBSD compiler, CTest,
pledge/unveil runtime, imsg run or workflow exists.

- **Broker path bug**: the broker had used `openat()` after a pledge without
  `rpath` and with a locked unveil list. It now passes its immutable root at
  fd4; the worker performs the relative walk and dev/inode/path validation
  before applying its own policy.
- **Ownership/recovery**: forwarded workers are disowned after channel handoff
  and reaped non-blockingly; broker shutdown cannot kill in-flight storage.
  Launch churn uses a replenished one-minute window. A broker timeout retains
  the channel and late replies are drained by request id.
- **Policy**: storage requires an absolute unveil root and drops unused
  `fattr`/`flock`. Network gains explicit socket-handoff/route/DNS flags;
  `ValidatePreopenedSocket` checks type, CLOEXEC and listening state.
- **imsg**: validated adoption checks socket/CLOEXEC/getpeereid before fdpass;
  descriptor ownership on compose failure matches `Channel`; timeouts poison.
- Residual: no network/crypto backend, core `inet` removal, router storage
  broker, log descriptor-only policy or OpenBSD runtime evidence.

## 2026-10-21 Linux multiprocess hardening continuation (static)

Five read-only audits covered AF_UNIX/SOCK_SEQPACKET, credentials, pidfd,
descriptor transfer and the BitTorrent-connected storage worker. No Linux
runtime, compiler, CTest, sanitizer, fuzzer or workflow exists.

- **Channel**: adopted descriptors require AF_UNIX/seqpacket where available,
  reject listening sockets and enable SO_PASSCRED; zero-length records now
  require the same credentials as data and are bounded against floods.
- **Spawn**: `PR_SET_NO_NEW_PRIVS` is installed before exec; inherited standard
  socket descriptors are closed in supervisor/log workers; the close() fallback
  uses the actual RLIMIT_NOFILE with a documented infinite-limit cap.
- **seccomp**: storage/log deny process_vm_readv/writev, kcmp, pidfd_open,
  pidfd_send_signal, open/name_to_handle_at, userfaultfd, bpf and
  perf_event_open in addition to ptrace/io_uring/socket.
- **Descriptors/storage**: both storage clients keep O_RDONLY directory
  descriptors and bind the root by dev/inode before Landlock; Linux rejects the
  OpenBSD-only `*-inherited` role names.
- Residual: kill/tgkill self-filtering, pidfd deferred-reap/total stop budget,
  cgroup v2/kill, descriptor classification and true network-worker separation.
  Kernel-version behaviours and fallbacks are documented, not dynamically
  validated.

## 2026-10-21 Windows multiprocess hardening continuation (static)

Five read-only audits covered Named Pipes, ncalrpc, restricted
tokens/AppContainer/LPAC, Job Objects/mitigations and protocol parity. The
transport decision is to retain documented message-mode Named Pipes; ncalrpc
would require MIDL/generated stubs and does not provide handle transfer.

- **Named Pipe lifecycle**: canceled overlapped operations are drained before
  buffer/OVERLAPPED release; timeout and malformed frames are terminal;
  rejected peers leave the listener reusable; ConnectBound validates the
  expected server PID/session; random per-spawn names reduce squatting.
- **Tokens/Jobs**: all parent privileges are deleted, LUA_TOKEN is used for
  elevated parents, and Jobs apply exception, memory and UI restrictions.
- **Authorization**: ConfigureLogFd is rejected because Windows has no
  descriptor channel; typed storage requests are represented in the Windows
  matrix.
- Residual: no worker entry/supervisor/daemon wiring, no functional
  AppContainer/LPAC token + capability/ACL enforcement, no cross-user or
  Windows runtime negative tests. No Windows build or dispatch exists.

## 2026-10-21 Android multiprocess/SAF continuation (static)

Five read-only audits covered Android services/lifecycle, JNI, Binder/AIDL, SAF
and the embedded C++23 engine. No Gradle, NDK, Lint, emulator/device or workflow
exists.

- **Lifecycle**: Activity destruction still does not stop transfers; first-launch
  autostart is persisted, and `MY_PACKAGE_REPLACED` resumes only an already
  requested router.
- **JNI**: inbound strings are decoded from UTF-16 to UTF-8 with NUL/unpaired
  surrogate rejection; start diagnostics are bounded/sanitized.
- **SAF**: tree grants are validated/released, export grants are scoped, close/
  release no longer retire the broker on a client timeout, broker close is
  queued, cancellation is operation-scoped, created names are checked and export
  starts empty. Display-name `:`/`\` are accepted.
- **Boundary limit**: the router/UI share a UID; `android:process` is not UID
  isolation, `isolatedProcess` cannot keep persisted SAF grants, and INTERNET is
  per-UID. No router AIDL split, session-scoped SAF capability, document-read
  confinement, minimal SAF seccomp library or device evidence is claimed.

CONC-010 requires a single process owning the profile data directory. Unix
daemon PID locking enforces this for its normal entry point; equivalent
Windows/Android enforcement is not established. Concurrent instances sharing
a root are unsupported: process-local locks cannot prevent another process
from replacing a profile between expiry stat and deletion. Unknown staging
files are preserved, since startup cannot assume exclusive authority. Normal
failed writes clean their own staging file; crash leftovers remain pending.

## 2026-10-22 receiver, queue and service-owner continuation (static)

- CONC-011 replaces reset-by-port with weak registration identities and shared
  callback invocation slots throughout Datagram/UDP/SAM/address-book/torrent
  callers. External reset deactivates then waits outside the registry lock;
  retired callbacks are also destroyed outside that lock. Reset inside any
  receiver callback returns false after deactivation, avoiding same/cross-slot
  wait cycles; destruction must be deferred until an external drain. In-tree
  raw-owner reset paths were inspected as external lifecycle operations, not
  dynamically proven. Historical wildcard fallback and protocol bytes stay.
- CONC-012 closes tunnel/netDb admission atomically with enqueue, checks stop
  after worker wake, joins and drains, and resets netDb throttle maps. Producers
  must still be quiesced across restart; queue messages do not carry epochs.
- ANDROID-019 fences stale listener snapshots and all service worker submissions
  through an owner executor over a process FIFO. Retirement appends native
  teardown before new-owner work; accepted work drains ahead of that barrier.
  No JNI runs under the epoch monitor. NativeCallGuard remains authoritative.
- CONC-010 cleanup now owns only canonical profile leaves at their exact shard,
  excludes symlinks/unknown/staging names, and preserves unrelated old data.
  Concurrent hostile filesystem owners remain outside the process-local gate.

Prepared receiver/queue/JVM regressions and manual workflow coverage are not
compilation or runtime evidence. Raw torrent disk/timer captures, RouterInfo
copy-on-write, SSU2 publication, IPC cancellation, full platform lifecycle and
global process separation remain open. No local build/test/validator ran.

Publication review preserves the previous per-dispatch std::function snapshot
for Datagram callbacks. The slot mutex protects copying before the in-flight
claim, and invocation uses the local copy outside locks; mutable value captures
therefore do not become shared callback state. A snapshot regression is
prepared alongside the lifecycle tests, pending manual CI.

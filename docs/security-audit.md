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

The new address, copy-string, i18n and SOCKS tests are prepared for manual CI;
no compiler, CTest, sanitizer, fuzzer or runtime validation was performed.

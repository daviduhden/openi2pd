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
  commands, no secret fields, no command execution.
- **Residual:** A same-UID process is already inside the trust boundary (it can
  read `router.keys`), and systems without `SO_PEERCRED`/`getpeereid` fall back
  to filesystem permissions. No live penetration test was performed.

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

- ASan/UBSan/TSan runs.
- Fuzzing of NTCP2/SSU2/I2NP/RouterInfo/LeaseSet/SU3/TUI inputs.
- Live Java interoperability.
- OpenBSD pledge/unveil under a non-default user and datadir.
- Windows build and runtime.
- Multi-UID rejection (a same-UID live TUI session and malformed requests were
  exercised; a second UID was not available).

See `testing.md` for exact commands and the honest status of each check.

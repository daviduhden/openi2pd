<!--
See the LICENSE file at the top of the project tree for copyright
and license details.

openi2pd: testing.

Documents project documentation.

-->

# Torrent and Perl test plan

Status: prepared; dynamic results must be recorded after execution. A static
review is not a substitute for these checks. **Do not run these commands in
the authoring environment**; execute them in CI or a dedicated test host. The
checkpoint build/test results below were obtained before that freeze.

## C++/CTest

```sh
cmake -S build -B /tmp/openi2pd-build -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug -DWITH_TORRENTS=ON \
  -DI2PD_BUILD_TUI=ON -DBUILD_TESTING=ON
cmake --build /tmp/openi2pd-build --parallel
ctest --test-dir /tmp/openi2pd-build --output-on-failure
```

Checkpoint before the freeze: CTest 20/20 passed on Linux/GCC 16, including
`test-torrent-bencode` and extended `test-tui-protocol`. This is not a claim
that the later CI environment or full engine/TUI runtime was validated.

Prepared coverage:

- `test-torrent-bencode`: valid v1 metadata; truncated/malformed bencode;
  oversized integer/length; excessive depth; bad piece/file counts; missing or
  empty root name; absolute, `..`, separator, control-character and colliding
  paths; bounded `.torrent` input checks (extension, regular file, symlink,
  empty/oversized file).
- `test-tui-protocol`: torrent commands including pause/resume, bounds,
  index/ID validation, request-line limits and field parsing.
- `test-torrents-comments`: request/reply codec, age timestamps, malformed
  entry isolation, newest-first bounded replies, rating/content dedupe,
  per-pseudonym spam limits, capacity eviction, hide/unhide and state
  round-trip; `test-tui-protocol` pins the comment command grammar.
- `test-torrents-security`: tracker-host validation, lexical root containment,
  anchored no-follow file operations, path traversal/symlink rejection and
  resume bounds; per-block request release returns a claim to the scheduler;
  `test-torrents-concurrency` covers piece/torrent publication.
- `test-torrents-verify-parity`: exact resume-bitfield length/padding rejection,
  final-name and mixed final/`.part` startup verification, explicit all-piece
  recheck, bounded pending-HAVE gating, tracker malformed-body rejection,
  exponential failure backoff, success interval reset and tracker-peer expiry.
- `test-torrents-priorities`: skip/normal/high boundary pieces, selected
  completion, selected `GetLeft` versus full `GetFullLeft`, file-complete
  promotion inputs and the selected-seeding status.
- `test-torrents-dht-interop`: secure compact nodes, token presence/expiry,
  XOR nearest selection, request/response caps, worst-case state filenames and
  bounded peer-value replies; `test-torrents-dht-persistence` covers delegated
  flat records and worker names.
- `test-torrents-creator` and `test-torrents-engine-interop`: multifile and
  zero-length entries, public tracker inheritance, private-without-tracker
  rejection, primary `announce` plus `announce-list` tier normalization and
  exact info-hash preservation.
- Control snapshot tests (when the target is added): deterministic formatting,
  empty state, unavailable engine fields, command result mapping.
- `-DNO_TORRENTS` build: daemon/TUI must compile without the engine; torrent UI
  shows no configured tunnel/unsupported state.

## Native TUI manual test

1. Start router with a `[torrents]` tunnel and no web/RPC port.
2. Open `i2pd-tui`; verify Status/Peers/Tunnels/NetDb/Logs still work.
3. Torrents view: empty state; add magnet; add a local `.torrent`; verify
   listing and details (general/files/peers/trackers/session/DHT).
4. Pause/resume/reannounce; verify state changes and errors.
5. Remove torrent; verify confirmation. Remove-with-files: verify second
   confirmation and that only files inside `torrentsdir` are deleted.
6. Resize terminal, use a narrow terminal and long Unicode torrent names;
   verify no multibyte truncation and no stale rows.
7. Disconnect/stop the daemon while in Torrents; verify bounded error and
   reconnect, with no UI hang.
8. Verify no browser assets/routes remain and the TUI never opens download
   files or peer sockets.

## Decentralized discovery differential cases

Manual Java/C++ comparison should feed the same recorded KRPC dictionaries to
both implementations and compare observable I2P-only decisions: get_peers
response size, node/value caps, token acceptance/expiry, seed/noseed filtering,
PEX added-hash handling, DHT hint port validation and tracker failure/backoff.
Negative cases must include zero destinations, oversized/truncated 54-byte
nodes, tokens replayed for another info_hash, unsolicited raw responses, wrong
reply ports, private torrents, stopped/removed torrents and injected clear-net
tracker/DHT URLs. No public DHT or clearnet fallback is permitted.

## Security checks

- Crafted `.torrent`: absolute path, `..`, `/`, `\`, NUL/control characters,
  duplicate paths, file/dir collisions, symlink in root/component, huge file
  count/piece count; verify rejection or containment.
- Delete-local-data with a symlink/crafted path: verify no outside deletion.
- RPC: non-loopback bind refused; no wildcard CORS; body limit; auth failure;
  unknown/malformed JSON; no remote admin by default.
- Confirm no clearnet DNS/TCP/UDP path in tracker/DHT/peer code.

## Perl

```sh
perl -c tools/scripts/i2pdctl
prove -v tools/tests/i2pdctl.t
perl -MTest::More -e 1
```

- The Test::More suite preserves `exec`, `raw_info` and `info` output and exit
  codes using fake I2PControl responses; no Python comparison target remains.
- Invalid arguments/JSON, missing URL/password, HTTP errors, Unicode names,
  self-signed TLS fallback, proxy-environment isolation, repeated-request
  stability and responses larger than the configured bound are covered.
- `perlcritic --profile .perlcriticrc tools/scripts/i2pdctl` is optional; CTest
  registers it only with `-DI2PD_PERL_CRITIC=ON`.
- Verify no Python invocation remains in CMake/Make/docs/CI and no Perl
  runtime dependency is added to the daemon; `perl-portability.t` enforces the
  source/build-consumer side.

## Android cross-check (Torrent IPC design)

The torrent IPC remains local to the daemon; Android's same-process service is
not claimed to provide network/storage separation. Device/Gradle checks remain
in the Android plan. The user instruction after the checkpoint is that all
builds/tests run only in CI.

## I2PSnark parity increment

The manual Linux workflow registers four additional CTest regressions:
`test-torrents-magnet`, `test-torrents-engine-interop`,
`test-torrents-dht-interop` and `test-torrents-comments`. These cover conflicting
magnet topics and escapes, tracker hints, v1 construction/roundtrip/infohash,
explicit empty tracker lists, fixed node/identity lengths and opaque tokens,
and comment age timestamps/Unicode bounds/local moderation. The TUI protocol
test includes paused-tracker command syntax; Android adds `TorrentControlTest`.
The existing torrent parser fuzzer now invokes magnet and comment parsers.
All are prepared only: pending CI compilation and pending CI tests. None
establishes live interoperability with Java or OS capability separation.

The continuation adds automatic-piece-size, creation-option/progress and
canonical/empty bencode regressions; advanced creation/cancellation request
parsing; anonymous rating-only comments; and `test-torrent-storage-worker`.
The latter exercises the real exec-ed backend, TorrentFile integration,
read-only/writable handles, bounds, symlinks/hardlinks and denied network
capabilities on Linux. Code 77 is limited to root or explicitly unsupported
kernel isolation; operational failures on a supported kernel fail the test.
Android `TorrentMetadataJniTest` prepares native single/multi-file hashing and
rejection vectors. The manual Android workflow builds its instrumentation APK;
execution on a device remains pending. The parser fuzzer also covers local
state and advanced administrative requests. None has been run locally.

The new `test-torrent-payload-storage` exercises bounded backend transfers, pieces
crossing files, zero-length-file fragment suppression, failed persistence,
unavailable capabilities and the saving gate. OpenBSD registers
`test-torrent-storage-openbsd` for pre-pledge supervision and replacement without
runtime proc/exec. Android `src/test` now runs framework-free
`TorrentSafPolicyTest` (range wraparound, export growth/commit, document identity,
shorter-document extension) and `TorrentSafGrantsTest` (shared-tree reference
counting) in `testDebugUnitTest`. Instrumentation still includes
`TorrentSafStorageTest` path/range rejection; realistic provider fixtures,
nonseekable/revoked/ENOSPC/corrupted documents, same-length replacement, broker
death and seccomp-negative device runs remain pending. These sources have not
been executed locally.

Prepared (not run): `test-torrent-storage-lifecycle` covers no-clobber promotion,
interrupted same-inode completion, metadata chunking/replacement, traversal,
symlink/hardlink refusal and fixed staging-slot exhaustion. The payload test
also verifies that invalidating a previously seeded piece clears torrent-wide
completion. Android adds cancellation wire and fault-provider fixtures; actual
cross-process death/rebind and persisted-grant exercises remain pending.

Prepared Linux targets `test-router-storage` and
`test-torrents-dht-persistence` cover fixed-name atomic router state, failed
callbacks without fallback, DHT format/callback limits, metadata block reads and
legacy-name authorization. OpenBSD policy regressions cover the configured
storage-worker restriction to preopened IPC. Android adds same-size document
replacement rejection. All are pending manual CI compilation and tests.

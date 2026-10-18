# Torrent security review

Status: **static review and hardening implemented; dynamic/fuzz/device tests
pending.** The torrent engine remains in the daemon process; the network/storage
worker split is not yet implemented, so this document distinguishes implemented
controls from residual risk.

## Removed browser surface

- `contrib/torrents/web/index.html`, `main.js`, `rpc.js` and their CSS were
  deleted.
- The RPC server no longer serves files, HTML, CSS, JavaScript or `OPTIONS`
  preflight responses. Non-POST methods return `405`.
- There is no `web` tunnel option or `HandleWeb` code path.
- The ncurses TUI uses the local `I2PDTUI/1` socket, not HTTP/JSON-RPC.

## Bencode and torrent parsing

`libi2pd_client/Torrents.cpp` now enforces:

- exact byte-string/integer syntax, no leading zeroes, negative zero or
  trailing bytes after the top-level dictionary;
- bounded dictionary depth, key count and list item count;
- exact piece-hash count vs. total length/piece length, piece-count and
  piece-size caps;
- mutual exclusion of `length` and `files`, file-count and total-size caps;
- safe path components: no empty/`.`/`..`, absolute paths, separators, NUL,
  control characters, overlong components or duplicate/colliding file paths;
- valid UTF-8 bytes are preserved; unsafe bytes are rejected or sanitized.

Tests in `tests/test-torrent-bencode.cpp` cover malformed/truncated metadata,
depth/bounds, traversal/absolute paths, duplicate/colliding paths, oversized
values and valid v1 single/multi-file metadata. They are deterministic and do
not need I2P network access.

## Filesystem containment

`TorrentsTunnel::IsContainedPath` checks lexical containment under
`torrentsdir`, rejects symlink metadata and non-directory intermediates, and
rechecks paths after subdirectory creation. Save, resume and delete operations
use the check. `TORRENT-ADD-FILE` and startup loading use
`ReadTorrentFileBounded`, which requires a regular `.torrent` file, rejects
symlinks and bounds the read size. Delete-local-data is a distinct command and
requires explicit confirmation in the TUI.

Residual risk: this is a check-then-use design over `std::filesystem`; a local
attacker able to mutate the download directory concurrently could race the
check and the subsequent open/rename/remove. The daemon should use `openat`/
`O_NOFOLLOW`/directory file descriptors in a future hardening pass. The current
code does not claim immunity to a same-UID TOCTOU attacker.

## I2P-only operation

The torrent engine is the existing I2P implementation:

- peers are I2P stream destinations; tracker HTTP and UDP datagrams go through
  I2P destinations;
- DHT extensions and peer discovery are I2P-specific;
- no clearnet tracker/peer/DNS fallback was added;
- no public host IP address is advertised by the torrent control layer;
- the control snapshot exposes no keys or private identity material.

The static review found no new direct TCP/UDP or DNS path in the torrent code.
This must still be verified dynamically with traffic capture and a hostile
tracker/peer fixture.

## Peer/protocol and transfer integrity

Existing engine checks remain in force: bounded receive buffers, message
lengths, handshake/idle timeouts, piece SHA-1 verification, block request
bounds, resume-bitfield application and bounded connection queues. A malformed
peer message is rejected by the existing parser; the new control layer does not
bypass it. Fuzzing and TSan/ASan runs remain pending.

## Optional JSON-RPC

The Transmission-compatible RPC is retained as a separate compatibility surface,
not as the TUI transport:

- `rpcport` defaults to `0` (disabled);
- `rpcaddress` must be loopback (`127.0.0.1`/`::1`); non-loopback bind attempts
  are rejected before the acceptor is created;
- POST + `application/json` only, bounded body (32 MiB) and metadata (24 MiB);
- no wildcard CORS or browser preflight; `Cache-Control: no-store`;
- Basic authentication if configured, constant-time comparison, bounded
  credentials; authentication is optional only because the listener is local;
- torrent IDs, tracker lists and magnet/metainfo inputs are validated;
- metainfo/magnet additions are marshalled to the torrents service thread
  (`AddTorrentFromRpc`/`AddMagnetFromRpc`) with a bounded wait, instead of
  mutating the session from an HTTP I/O thread;
- the same validated engine operations are used as the native TUI.

Documented incompatibility: a client that relied on the removed web files, on
CORS, on non-loopback binding, or on `application/x-www-form-urlencoded` must
be updated. Transmission-compatible JSON-RPC method names/fields are otherwise
preserved where the engine supports them.

## Privilege separation

The control API is typed and bounded, but the daemon still combines network and
storage. `TORRENT-IPC-003` / `PRIVSEP-006`/`007` remain open. The invariant
"no process both connects externally and writes persistent storage" is **not**
satisfied for torrents yet; do not treat this document as a privilege-separation
completion claim.

## Pending validation

- ASan/UBSan/TSan and fuzzing with malformed bencode/peer messages.
- Symlink/TOCTOU and permission tests on Linux and OpenBSD.
- Traffic-capture test proving no clearnet DNS/TCP/UDP.
- RPC auth/loopback/body-limit integration test and a real Transmission client.
- TUI delete confirmation and path-containment tests.

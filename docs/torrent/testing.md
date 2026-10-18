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
- `test-tui-protocol`: torrent commands, bounds, index/ID validation,
  request-line limits and field parsing.
- Control snapshot tests (when the target is added): deterministic formatting,
  empty state, unavailable engine fields, command result mapping.
- `-DNO_TORRENTS` build: daemon/TUI must compile without the engine; torrent UI
  shows no configured tunnel/unsupported state.

## Native TUI manual test

1. Start router with a `[torrents]` tunnel and no web/RPC port.
2. Open `i2pd-tui`; verify Status/Peers/Tunnels/NetDb/Logs still work.
3. Torrents view: empty state; add magnet; add a local `.torrent`; verify
   listing and details (general/files/peers/trackers/session/DHT).
4. Start/pause/reannounce; verify state changes and errors.
5. Remove torrent; verify confirmation. Remove-with-files: verify second
   confirmation and that only files inside `torrentsdir` are deleted.
6. Resize terminal, use a narrow terminal and long Unicode torrent names;
   verify no multibyte truncation and no stale rows.
7. Disconnect/stop the daemon while in Torrents; verify bounded error and
   reconnect, with no UI hang.
8. Verify no browser assets/routes remain and the TUI never opens download
   files or peer sockets.

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

- Compare `exec`, `raw_info`, `info` output and exit codes against the old
  Python script using the same fake I2PControl responses.
- Invalid arguments/JSON, missing URL/password, HTTP errors, Unicode names,
  self-signed TLS fallback, and a response larger than the configured bound.
- `perlcritic tools/scripts/i2pdctl` if installed; do not treat style warnings
  as functional failures.
- Verify no Python invocation remains in CMake/Make/docs/CI and no Perl
  runtime dependency is added to the daemon.

## Android cross-check (Torrent IPC design)

The torrent IPC remains local to the daemon; Android's same-process service is
not claimed to provide network/storage separation. Device/Gradle checks remain
in the Android plan. The user instruction after the checkpoint is that all
builds/tests run only in CI.

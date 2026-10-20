<!--
See the LICENSE file at the top of the project tree for copyright
and license details.

openi2pd OpenBSD policy map.
-->
# OpenBSD policy and validation map

Use with `SKILL.md`. This is a source-level map; runtime/VM validation remains
manual CI work.

## Policy ownership

| Component | File | Rule |
|---|---|---|
| Startup/runtime | `daemon/UnixDaemon.cpp` | spawn supervisor/log before runtime pledge; drop `exec` after startup |
| Core runtime | `daemon/UnixDaemon.cpp` | retain only required `proc`/`recvfd`; no arbitrary file/network promises |
| Supervisor | `privsep/OpenBSDSandbox.*` | `stdio unix proc exec` for lifecycle; never opens router/log payload |
| Log worker | `privsep/LogWorker.*` | own `unveil` + `pledge` after exec; only configured sink |
| Storage supervisor | `privsep/TorrentStorage.*`, `Supervisor.*` | pre-pledge root authorization; fd4 inherited root; socket-only return |
| Storage worker | `libi2pd_client/TorrentStorageWorker.*` | own storage-only policy before ACK; no network/exec |
| Service | `contrib/openbsd/i2pd.rc`, `i2pd.login` | non-default user/datadir, foreground daemon, no TUI service |

## Non-negotiable rules

- No `SCM_RIGHTS` directory descriptor after `pledge`.
- No `execpromises` assumption for a child that must install its own policy.
- No unsandboxed fallback when isolated storage was requested.
- No generic `proc`/`exec` in the core runtime policy.
- No new unveiled path without a matching configured capability.
- No LibreSSL build that advertises an unavailable PQ type.

## Validation checklist

1. Build with the CMake/Ninja graph on OpenBSD and run full CTest.
2. Verify rc.d start/stop/restart/status/reload and foreground behavior.
3. Check pidfile creation/removal and stale-pid handling.
4. Check configured datadir, log, socket and temporary paths are unveiled.
5. Test TUI socket mode/owner, log worker restart and supervisor death.
6. Send SIGTERM/SIGINT and check shutdown ordering, child reaping and no
   orphan socket/pidfile.
7. Check SSU2 MTU/IPv6/peer-test behavior on a controlled network.
8. Retain command output, run id, revision and environment as evidence.

## Required tests

- `test-privsep-openbsd-policy`
- `test-privsep-openbsd-imsg`
- `test-torrent-storage-openbsd`
- `test-privsep-protocol`, `test-privsep-log` where the shared channel changes
- SSU2/transport regressions when MTU/address behavior changes

All are prepared statically. Only `build-openbsd.yml` on a published revision
can provide dynamic evidence; do not infer it from a Linux run.

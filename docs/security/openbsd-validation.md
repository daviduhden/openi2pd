<!--
See the LICENSE file at the top of the project tree for copyright
and license details.

openi2pd: openbsd validation.

Documents project documentation.

-->

# OpenBSD dynamic validation checklist

**Status: static review only.** None of the commands below were executed while
preparing this checklist, and no OpenBSD runtime claim in the other security
documents should be read as validated because this file exists. Record the
OpenBSD release, package revision, probe output and logs with every run.

The checklist targets the packaged service (`net/i2pd`) using
`contrib/openbsd/i2pd.rc` and `i2pd.login`, as `_i2pd`, with
`/var/i2pd` as the data directory. Run the privileged commands as root on a
disposable host or VM with no production router identity.

## 1. Package and rc.d identity

```sh
getcap -f /etc/login.conf.d/i2pd i2pd
rcctl getdef i2pd
rcctl get i2pd
ls -ld /etc/i2pd /var/i2pd /usr/local/share/i2pd/certificates
ls -l /var/i2pd/i2pd.pid /var/i2pd/i2pd-tui.sock
```

- Confirm the rendered `/etc/rc.d/i2pd` matches the canonical template after
  the ports framework has substituted `TRUEPREFIX`, `LOCALSTATEDIR` and
  `SYSCONFDIR`; the installed file is not byte-identical to the template.
- Confirm `getcap -f /etc/login.conf.d/i2pd i2pd` reports `i2pd`, not the
  fallback `daemon` class, and that `_i2pd` uses it. If `/etc/login.conf.db` exists, confirm it was rebuilt
  after installing `login.conf.d/i2pd`.
- Confirm `/etc/i2pd` is root-owned/not writable by `_i2pd` and `/var/i2pd`
  is owned by `_i2pd` with no group/world write access.
- Confirm the rendered rc.d sets `pexp="${TRUEPREFIX}/bin/i2pd"`, so
  `rcctl check` matches the binary rather than a stale command line.
- Confirm the package sampled `/var/i2pd/subscriptions.txt` and that it is
  writable by `_i2pd`; the daemon updates it in place.
- Confirm `/var/i2pd/certificates` is not a leftover real directory competing
  with the packaged `/usr/local/share/i2pd/certificates` tree.
- Confirm `/usr/local/share/doc/i2pd` contains `LICENSE`, `NOTICE`,
  `LICENSE.i2pd-tools` and `CC-BY-SA-4.0`.

## 2. Service lifecycle

```sh
rcctl -d enable i2pd
rcctl -d start i2pd
rcctl check i2pd
ps -o pid,user,state,pledge,command -p "$(cat /var/i2pd/i2pd.pid)"
fstat -p "$(cat /var/i2pd/i2pd.pid)"
tail -n 50 /var/log/daemon
```

- The packaged rc.d script runs `--foreground` under `rc_bg=YES`; `rcctl start`
  therefore confirms that rc.subr started the supervised process, not that the
  router finished initializing. For a manual `--daemon` start, the parent waits
  up to 5 minutes for the child readiness byte. In daemon mode success means the
  pidfile was locked, pledge/unveil were applied, the logger started and
  `Daemon_Singleton::start()` returned true. Confirm
  the pidfile and TUI socket are present immediately after `rcctl check i2pd`.
- The rc.d path has no readiness pipe: `rcctl start` cannot report a later
  child failure. Prove the failure reporting with a separate manual
  `i2pd --daemon` probe instead: the parent must wait for the readiness byte,
  return nonzero on failure/timeout and print the error; a timed-out child is
  killed with `SIGKILL`, and no stray `i2pd` process may remain. Do not apply
  that expectation to `rcctl start`.
- The process state from `ps -o state` must include `p` (pledged) and `U`
  (unveil locked); the `pledge` column must list the runtime promises. It must
  never contain `exec`; `proc` may appear only when `privsep.log` needs bounded
  supervisor reaping/signalling.
- `fstat -p` should show the pidfile descriptor, the TUI listening socket, and
  a `/dev/log` descriptor when `log = syslog` (or otherwise confirm delivery in
  `/var/log/daemon`).
- `rcctl stop i2pd` sends `SIGINT` (the template sets `rc_stop_signal=INT`)
  and waits up to `daemon_timeout=660` for the ordered stop and transit-tunnel
  drain. With no transit tunnels it must complete promptly, remove the pidfile
  and the TUI socket and leave no `i2pd` process.
- `rcctl reload i2pd` must send `SIGHUP` and reload tunnel configuration
  without restarting the process (PID unchanged).
- Repeat after `reboot`; confirm the service is started only when enabled.

## 3. PID file lifecycle

Default path: `/var/i2pd/i2pd.pid`, mode `0600`, content is the daemon PID.

```sh
stat -f '%Sp %Su %Sg %z %N' /var/i2pd/i2pd.pid
/usr/local/bin/i2pd --daemon --service ...      # second instance
```

- The second instance must fail with `Could not lock pid file`, must not
  unlink the first instance's file, and must leave the first instance running.
- `kill -9` the daemon, then start again: the stale file must be reused, not
  rejected, and rewritten with the new PID. Permissions must remain 0600.
- Stop the daemon, replace the pidfile with a symlink to another writable
  file, and start: startup must fail (`O_NOFOLLOW`) and the target must be
  unchanged. Repeat with a directory at the pidfile path (must fail as
  non-regular) and with a regular file belonging to another user (must fail
  to open or lock without truncating it).
- After a deliberate startup failure (for example an
  `openbsd.unveil_file` entry whose parent directory does not exist), the
  pidfile must be removed only if it is still the locked inode.
- Force a transport bind failure (occupy the configured TCP and UDP ports) and
  confirm the process exits with `EXIT_FAILURE`, removes the pidfile and does
  not crash during static destruction. The rollback path now joins the logger
  thread before exit; verify that `Log::~Log()` no longer sees a joinable
  thread in this case. Run this once in foreground mode and once with
  `--daemon`: the waiting parent must exit nonzero and no child may remain.
- While the daemon is stopping, replace the pidfile path with an unrelated
  regular file; the daemon must not unlink that replacement.
- Confirm the PID lock is still held during data persistence (`stop` keeps it
  until `Daemon_Singleton::stop()` returns) and released at process exit.

## 4. Sandbox paths

Run each case from a fresh configuration copy and record `lastcomm` flags
(`P` for pledge violation, `U` for unveil denial) plus `/var/log/daemon`.

- Default `/etc/i2pd/i2pd.conf` with `log = syslog`:
  - startup log lines appear in `/var/log/daemon` after the sandbox is
    applied (validates the `/dev/log` unveil rule);
  - no `U`/`P` flags are recorded for a normal start, a SIGHUP reload and a
    TUI query.
- `log = file` with a logfile inside the datadir, then with an absolute
  logfile outside the datadir listed nowhere: the second must fail only if
  the path was not unveiled; with `openbsd.unveil_file` it must work.
- Custom `tui.socket` inside the datadir; custom absolute `tui.socket`
  outside; a non-existent parent; a path longer than `sun_path`. A custom
  path must be usable when configured (unveiled with `rwc`) and must not
  prevent the router from running headless when creation fails.
- `reseed.file = https://...` must start normally (a URL must not be passed to
  `realpath`) and must actually reseed. `AbsolutePath()` now preserves URLs, so
  the remaining check is dynamic. A local SU3 file outside the datadir must be
  unveiled read-only; a local file whose parent directory does not exist must
  produce a clear startup error.
- Non-default `datadir`, `conf`, `tunconf`, `tunnelsdir` and `certsdir`
  (all absolute) must work, and relative paths must be resolved before the
  daemon `chdir()`.
- `i2pcontrol.enabled = true` with a file certificate/key and with a UNIX
  socket (port 0); a SOCKS outproxy with no port (UNIX socket) must connect.
- UPnP enabled must not produce a pledge violation (`mcast`);
  `/var/run/minissdpd.sock` must be the only extra socket path used.
- `openbsd.unveil_file` with one extra absolute path.
- `openbsd.pledge_file` is a **subset selector**, never an override or an
  additive escape. One parser serves the startup and runtime phases: the file
  is bounded (64 KiB), tokens are space/tab separated, comments and blank
  lines are rejected (a commented-out promise must never look active), unknown
  tokens abort startup, and every token must already be present in that
  phase's validated base set. Duplicates are ignored. The startup base set is
  `stdio rpath wpath cpath fattr inet dns unix flock recvfd proc exec` plus
  `unveil`, `mcast` (UPnP) and `id` when a worker identity is configured; the
  runtime base set is the same table without process/identity promises, plus
  `proc` only while the log supervisor is owned and `mcast` with UPnP. Startup
  fails if the runtime set is not a strict subset, because pledge can only
  remove promises.
  - Positive case: a custom runtime file containing only `stdio` must start,
    and `ps -o pledge -p "$(cat /var/i2pd/i2pd.pid)"` must still show the
    runtime baseline (and `mcast` with UPnP enabled).
  - Negative case: `stdio;`, `procc`, a comment line, a blank line and a
    runtime `proc`/`exec`/`id`/`chown`/`dpath` token must each fail startup
    with a bounded diagnostic in syslog (daemon stderr is `/dev/null`), remove
    the pidfile and exit nonzero. Repeating a token on several lines must not
    duplicate it.
- `openbsd.unveil_file` entries are trimmed, must be absolute and NUL-free, and
  may not be `/` either textually or after `realpath()`. A configured datadir
  symlink is intentionally resolved before unveil.
- A root-started daemon with `privsep.enabled`, `privsep.routerstorage`,
  `privsep.artifacts` or an isolated tunnel and no `privsep.user` must fail
  startup with the explicit identity diagnostic instead of silently skipping
  the broker. With `privsep.user`, the delegated data directory must be owned
  by that uid.
- Start the daemon with an `argv[0]` that contains `/` (the rc.d unit and
  service managers already use an absolute path). The executable is frozen
  from `argv[0]`/`realpath` and is never resolved through `$PATH` or `/proc`;
  a bare program name leaves it unset and every worker spawn fails closed.
- Worker identity: `test-privsep-credentials` resolves the current
  account, rejects unknown/uid-0/malformed names, verifies the fixed diagnostic
  and the forked drop (real/effective ids, empty supplementary set). The
  spawn-bound hello is covered by `test-privsep-worker-identity`: 25-byte
  layout, uid/gid mismatch, uid 0, trailing byte and open writer, pinned stdio
  and the one-way `ForbidProcessCreation()` barrier.
- Supervisor/broker: `test-privsep-openbsd-policy` requires exactly one `x`
  unveil rule for the frozen executable, no `unix`/`recvfd` in the broker,
  no `proc`/`exec` in storage/log roles, and a runtime promise set that is a
  subset of startup. It probes that a storage worker cannot `socket(AF_INET)`
  or `fork()`, that `unveil` after lock is `EPERM`, and that `execve("/bin/sh")`
  fails while the frozen image is still executable.

## 5. TUI and log worker

- TUI socket is mode 0600 and owned by `_i2pd`; a different local user must be
  rejected by the peer-UID check even if the directory allows connect.
- `i2pd-tui` can read status over the socket; it cannot send administrative
  or write requests.
- With `privsep.enabled = true`, `privsep.log = true` and the OpenBSD sandbox
  enabled, startup must now succeed if the service runs as `_i2pd`. The
  supervisor/log pair is spawned before the runtime pledge; verify both worker
  processes have their own pledge state, the core has no `exec`, and `proc` is
  present only for supervisor reaping/signalling.
- Run `test-privsep-openbsd-policy`, `test-privsep-credentials`,
  `test-privsep-worker-identity` and `test-privsep-openbsd-imsg` from
  CTest. The policy test forks children and requires a pledge violation when
  the storage policy attempts an AF_INET socket and when the network policy
  attempts to create a persistent file; it also checks pre-opened stream/
  datagram validation and rejects an empty storage unveil root. The imsg test
  checks validated adoption (`getpeereid`, socket type, CLOEXEC), a versioned
  frame, buffered back-to-back frames, rejected unknown type and descriptor
  ownership on a failed oversized compose.
- With `isolatedstorage=true`, run `test-torrent-storage-openbsd`. It must
  delegate a nested subdirectory through the pre-pledge broker, reject a
  symlinked/replaced delegated path without killing the broker, and allow a
  later legitimate delegation. Verify the broker has no `rpath`/`wpath` in its
  runtime pledge and that an existing worker survives broker exit.
- Start `i2pd` as root with `privsep.enabled = true`: the worker entrypoint must
  refuse to run with euid 0 and startup must fail. The packaged service must be
  run as `_i2pd`; do not add a root fallback.
- The log worker must own the log file descriptor instead of the router, survive
  router log traffic and restart after a forced log-worker kill according to
  the supervisor policy. If the supervisor itself is killed, the core must not
  attempt a fork under the runtime sandbox; logging stops without local
  fallback. Verify that no log file is recreated by the core.

## 6. Signals and shutdown ordering

```sh
kill -USR1 "$(cat /var/i2pd/i2pd.pid)"   # reopen file log
kill -HUP  "$(cat /var/i2pd/i2pd.pid)"   # reload tunnels
kill -INT  "$(cat /var/i2pd/i2pd.pid)"   # graceful, up to 10 min
kill -TERM "$(cat /var/i2pd/i2pd.pid)"   # ordered immediate shutdown
```

- `SIGUSR1` with a file log must reopen the configured path (rotation test).
  `Log` now snapshots the destination and stream under a mutex, opens the
  replacement outside the lock and swaps it in a short critical section.
  A failed open leaves the previous stream active instead of dropping log
  output. Run the rotation stress procedure below under log load; an in-flight
  record may land in the old file, but no line may be truncated or interleaved
  and the logger must keep writing afterwards.
- `SIGHUP` must not deadlock or race destination threads; the PID must not
  change.
- `SIGINT` must stop accepting transit tunnels; with no transit tunnels the
  process should exit promptly, otherwise within the ten-minute grace period.
  A second `SIGINT` must stop immediately.
- `SIGTERM` must run the full ordered stop (TUI, I2PControl, client, router,
  tunnels, transports, NetDB, logger) and exit before `daemon_timeout`.
- During shutdown, further `SIGTERM`/`SIGINT` must not corrupt state; the PID
  file must disappear only after persistence completes.
- `SIGABRT` (from `abort()` or an external sender) must terminate with the
  default disposition and a nonzero signal status, producing a core if
  configured. It must not be converted into a clean shutdown: the service
  manager has to observe the crash and apply its restart policy.
- `unix.handle_sigtstp` is now registered on OpenBSD and the daemon installs
  TSTP/CONT in its blocked-signal set; verify the main sigwait loop reconnects
  and restores transports on a real host.

### Log rotation stress test

Use `log = file` with `loglevel = debug` and keep normal router traffic (or
repeated TUI status queries) running during the test.

```sh
pid=$(cat /var/i2pd/i2pd.pid)
log=/var/i2pd/i2pd.log
for i in $(seq 1 200); do
	kill -USR1 "$pid"
	sleep 0.01
done
for i in $(seq 1 20); do
	mv "$log" "$log.$i"
	kill -USR1 "$pid"
	sleep 0.05
done
```

- The process must stay alive, `ps -o state,pledge` must not show `P`/`U`, and
  every recreated log must receive new lines after its `SIGUSR1`.
- With the default time format, every line must match
  `^[0-9]{2}:[0-9]{2}:[0-9]{2}@[0-9]{3}/(none|critical|error|warn|info|debug) - `.
  Check all rotated and current files; empty or partial records are failures.
- Repeat a few `SIGUSR1` signals during shutdown (after `SIGTERM` while the
  logger is draining) and confirm there is no deadlock or crash.

## 7. SSU2, MTU and transports

- Start with IPv4 only, then IPv6 only, then dual stack; verify the published
  RouterInfo addresses and `ssu2.mtu4`/`ssu2.mtu6` values with `routerinfo`.
- Set the interface MTU below and above the SSU2 maximum; confirm the
  published value is clamped and survives address updates (the
  `RouterContext::UpdateAddress` path must not overwrite an explicit MTU).
- Verify `getifaddrs()`-based MTU discovery works inside the pledge (no
  `SIOCGIFMTU` ioctl); a pledge violation here is a release blocker.
- Exercise peer tests, relay and hole punching from behind NAT if available.
  Confirm the OpenBSD 2 MiB SSU2 socket-buffer cap does not cause packet loss
  under load.
- Do not change wire formats or transport semantics as part of this
  checklist; record failures for a transport-specific review.

## 8. Evidence to retain

- `uname -a`, package version/revision, `config.status`/CMake cache, compiler
  and LibreSSL/Boost versions.
- `rcctl -d start/stop` output, `ps -o state,pledge`, `fstat -p`, pidfile/socket
  `stat` output.
- Relevant `/var/log/daemon` and `dmesg` lines, `lastcomm` `P`/`U` entries.
- Ktrace of startup if unveil paths need to be enumerated
  (`ktrace -di -f /tmp/i2pd.ktrace /usr/local/sbin/i2pd ...`; inspect the
  `unveil` calls with `kdump`); note that the daemon forks.
- Every negative test with its expected and actual result.

## 9. Build/CI status and known portability correction

The published `main@800c8627` OpenBSD workflow was re-run as run
`39160543186`; it stopped in `privsep/Worker.cpp` because `getexecname()` was
not declared through `<cstdlib>`. The working tree adds the OpenBSD
`<stdlib.h>` declaration and renames the private payload codec methods to
`WriteU8`/`WriteU16`/`WriteU32`/`WriteU64BE`/`WriteString` and `ReadU8`/... to
avoid OpenBSD/LibreSSL macro/type collisions; the wire format is unchanged.
The manual OpenBSD workflow builds the complete target graph and runs the full
CTest suite. No OpenBSD compile or runtime result is claimed for the
uncommitted corrections.

The current tree adds `privsep/OpenBSDSandbox.{h,cpp}`,
`privsep/OpenBSDImsg.{h,cpp}`, `tests/test-privsep-openbsd-policy.cpp` and
`tests/test-privsep-openbsd-imsg.cpp`. The imsg test links `libutil` on
OpenBSD. These files have not been compiled locally; the manual OpenBSD job is
the required first dynamic check. If the 7.9 libutil ABI differs from the
adapter assumptions, correct it from the CI error rather than weakening the
version/type checks.

## 10. Not covered by this checklist

- Storage, network and crypto worker backends do not exist; the router remains
  a monolithic process. Their OpenBSD policy profiles are prepared only.
- The log worker can run while the parent sandbox is enabled, but the core
  cannot replace a dead supervisor under the runtime policy.
- Pledge completeness for long-running workloads (reseeding, many peers,
  tunnel churn) still needs soak testing.
- No comparison against OpenBSD-current kernel changes is implied; rerun on
  each supported release.

## See also

- [OpenBSD port template](../../contrib/openbsd/README.md)
- [service management](../service-management.md)
- [build system](../build-system.md)

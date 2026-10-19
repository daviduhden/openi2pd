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
cmp /etc/rc.d/i2pd /path/to/wip-openbsd-ports/net/i2pd/pkg/i2pd.rc
getcap -f /etc/login.conf.d/i2pd i2pd
id -c _i2pd
rcctl getdef i2pd
rcctl get i2pd
ls -ld /etc/i2pd /var/i2pd /var/i2pd/certificates
ls -l /var/i2pd/i2pd.pid /var/i2pd/i2pd-tui.sock
```

- Confirm `i2pd.rc` is byte-identical to
  `wip-openbsd-ports/net/i2pd/pkg/i2pd.rc` (SHA-256
  `a979840440e7d70ebd7efa0d81e347f9489f8b80145218e06e9df5d0fbb71e54` at the
  time of writing).
- Confirm `getcap` reports `i2pd`, not the fallback `daemon` class, and that
  `_i2pd` uses it. If `/etc/login.conf.db` exists, confirm it was rebuilt
  after installing `login.conf.d/i2pd`.
- Confirm `/etc/i2pd` is root-owned/not writable by `_i2pd` and `/var/i2pd`
  is owned by `_i2pd` with no group/world write access.

## 2. Service lifecycle

```sh
rcctl -d enable i2pd
rcctl -d start i2pd
rcctl check i2pd
ps -o pid,user,state,pledge,command -p "$(cat /var/i2pd/i2pd.pid)"
fstat -p "$(cat /var/i2pd/i2pd.pid)"
tail -n 50 /var/log/daemon
```

- `rcctl start` waits for the self-daemonizing parent, which waits (bounded
  to 60 s) for the child to report readiness through a close-on-exec pipe.
  Success therefore means the pidfile was locked, pledge/unveil were applied,
  the logger started and `Daemon_Singleton::start()` returned true. Confirm
  the pidfile and TUI socket are present immediately after `rcctl check i2pd`.
- If the child fails or times out, `rcctl start` must return a nonzero status
  and the parent must print the readiness error. A timed-out child is killed
  with `SIGKILL`; confirm no stray `i2pd` process remains. Do not rely on the
  old assumption that the parent exits before initialization completes.
- The process state from `ps -o state` must include `p` (pledged) and `U`
  (unveil locked); the `pledge` column must list the runtime promises. It must
  never contain `exec`; `proc` may appear only when `privsep.log` needs bounded
  supervisor reaping/signalling.
- `fstat -p` should show the pidfile descriptor, the TUI listening socket, and
  a `/dev/log` descriptor when `log = syslog` (or otherwise confirm delivery in
  `/var/log/daemon`).
- `rcctl stop i2pd` must complete within the default 30 s timeout, remove the
  pidfile and the TUI socket, and leave no `i2pd` process.
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
- `openbsd.pledge_file` is additive, not an override. Each line is tokenized
  on spaces/tabs/CR, `#` starts a comment, duplicate tokens are ignored and
  unknown tokens abort startup. The runtime policy always contains the fixed
  baseline `stdio rpath wpath cpath fattr inet dns unix flock`, plus `mcast`
  when `upnp.enabled` is true and `proc` when `privsep.log` is active;
  accepted custom tokens are appended in file order. An empty custom file therefore keeps the baseline unchanged. The
  runtime parser rejects `proc` and `exec`; adding them to the core would
  restore process-creation capability after worker preparation. Adding `error`
  remains an explicit enforcement change and should be reviewed.
  - Positive case: a custom file containing only `stdio` must start, and
    `ps -o pledge -p "$(cat /var/i2pd/i2pd.pid)"` must still show the whole
    baseline (and `mcast` with UPnP enabled).
  - Negative case: a custom file containing `stdio;` or `procc` must fail
    startup with `Unknown pledge promise` in syslog (daemon stderr is
    `/dev/null`), remove the pidfile and exit nonzero. Repeating the same token
    on several lines must not duplicate it in the `pledge` column. A runtime
    file adding `proc` or `exec` must fail with the explicit refusal.
- The separate startup-phase pledge in `Daemon.cpp` is also additive: it
  always includes `proc`, `unveil` when enabled, and the filesystem/network
  baseline, then appends custom tokens. A custom file therefore cannot remove
  startup-only promises either.

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
- Run `test-privsep-openbsd-policy` and `test-privsep-openbsd-imsg` from
  CTest. The policy test forks children and requires a pledge violation when
  the storage policy attempts an AF_INET socket and when the network policy
  attempts to create a persistent file. The imsg test checks a versioned frame,
  rejected unknown type, `getpeereid` UID and one `SCM_RIGHTS` descriptor.
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
- `SIGABRT` from an actual `abort()` must still terminate and produce a core
  if configured; an externally sent `SIGABRT` is currently treated as a
  shutdown request.
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

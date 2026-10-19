<!--
See the LICENSE file at the top of the project tree for copyright
and license details.

openi2pd: README.

Documents project documentation.

-->

# OpenBSD configuration

Use `i2pd.conf` as the packaged configuration in `/etc/i2pd`, with
`--service --conf=/etc/i2pd/i2pd.conf`. Tunnel configuration lives below
`/etc/i2pd` (root-managed); service data, including the router keys and
`i2pd.pid`, lives in `/var/i2pd`. The rc.d script also selects
`/var/i2pd/certificates` as `certsdir`; the sample configuration in this tree
uses the same path so foreground and service starts agree (keep the packaged
configuration in sync with the rc.d flags). The example logs to syslog. The
former HTTP console no longer exists; use the read-only `i2pd-tui` client over
the local UNIX socket when an interactive view is needed.

The local TUI socket defaults to `/var/i2pd/i2pd-tui.sock` (mode 0600). A
custom `tui.socket` path is unveiled with `rwc` permissions; no additional
pledge promise is required. If the socket cannot be created, the router logs a
warning and continues headless.

## Sandbox

The sandbox uses the effective configuration paths after daemonization.
Paths set by the command line (as the rc.d script does) win over the file, so
`certsdir`, `tunconf` and `tunnelsdir` in `i2pd.conf` are only fallbacks for
manual starts. `SIGHUP` can reload tunnel definitions, but it cannot add a
filesystem path that was not unveiled at startup; restart after changing a
configured path.

The default runtime policy is:

```text
stdio rpath wpath cpath fattr inet dns unix flock
```

plus `mcast` when UPnP is enabled. There is no `exec`, `id`, `chown`, `route`,
`settime` or descriptor-passing promise. When `privsep.log` is active, the
runtime core retains `proc` only for bounded supervisor reaping/signalling;
`exec` is never retained. Startup uses a temporary superset (`proc`, `exec`
when private workers are enabled, and `unveil` when enabled) for `fork`,
`setsid`, `setrlimit`, PID locking and worker spawning; `exec` is dropped before
any service thread or network socket exists.

Additional notes:

- `unix` permits the TUI and I2PControl local sockets, including
  bind/listen/accept and peer credentials; `stdio` covers poll and
  receive/send, `cpath` covers removing the socket, and `fattr` covers the
  0600 `chmod`.
- With `log = syslog`, `/dev/log` is unveiled with `w`. The `unix` promise
  alone does not authorize the filesystem lookup performed by `connect(2)`.
  Name-service files (`/etc/resolv.conf`, `/etc/hosts`, `/etc/services`,
  `/etc/protocols`) are opened internally by libc under the `dns` promise and
  do not need separate unveil rules.
- `reseed.file` may be an `https://` URL. URLs are left unchanged during
  canonicalization and are not treated as filesystem paths during unveil
  setup; a local path is unveiled read-only. Dynamic reseed remains a
  validation item, not a claimed result.
- `i2pcontrol` certificate/key paths and an I2PControl UNIX socket, when
  configured, are unveiled with `rwc`. A SOCKS outproxy without a port (a
  local socket) is unveiled with `w`.
- `openbsd.unveil_file` lists extra paths, one per line; each is unveiled with
  `rwc`. Use absolute paths. `openbsd.pledge_file` is additive, not an
  override: the fixed startup and runtime baselines are always retained, and
  custom whitespace-separated tokens are appended. `#` starts a comment,
  duplicates are ignored and unknown tokens abort startup. The runtime policy
  rejects `proc` and `exec`; workers are spawned before the runtime sandbox and
  the core must not regain process-creation capability. Keep the file to
  genuinely additional promises. In particular, `rpath`/`cpath` are required
  to remove the pidfile and the TUI socket at shutdown, and `fattr` is required
  when saving keys; the baseline already includes them.
- `privsep.log` can run with the OpenBSD sandbox enabled since 2026: the
  supervisor/log pair is spawned while the startup pledge still has
  `proc`/`exec`, and each process installs its own policy after `execve`.
  Workers refuse to run as root; run the service as `_i2pd`. The supervisor
  owns log-worker replacement. If the supervisor itself dies, the core does not
  fork a replacement under the runtime sandbox; logging stops without a local
  fallback.
- Storage, network, crypto and admin policy profiles exist in
  `privsep/OpenBSDSandbox`, but those worker backends are not yet migrated into
  the core. Do not describe `privsep.enabled` as full privilege separation.

The pidfile defaults to `/var/i2pd/i2pd.pid`. It is opened with
`O_CLOEXEC | O_NOFOLLOW`, must be a regular file, is locked with `lockf(3)`
and is restricted to 0600. A symlink, directory or otherwise replaced path is
rejected. The PID lock is held while router data is saved during shutdown, and
the file is removed only if the path still refers to the locked inode. Keep the
pidfile in a directory that is not writable by other users: `O_NOFOLLOW` and
the inode check reduce, but cannot eliminate, path races in a shared directory.

The former port patch for the UDP cleanup timer is unnecessary on current
upstream: that timer has been removed from ClientContext.

## rc.d service

`i2pd.rc` is the OpenBSD rc.d script used by the `net/i2pd` port and is kept
byte-identical to `wip-openbsd-ports/net/i2pd/pkg/i2pd.rc`. Install it as
`/etc/rc.d/i2pd` (or let the port's PLIST `@rcscript` do so). Install
`i2pd.login` as `/etc/login.conf.d/i2pd`; `rc.subr` selects the `i2pd` login
class automatically when that file exists:

```sh
install -m 0644 i2pd.login /etc/login.conf.d/i2pd
# only needed when /etc/login.conf.db exists
cap_mkdb /etc/login.conf
rcctl enable i2pd
rcctl start i2pd
```

The port creates `_i2pd` with `i2pd` as its login class and `/var/i2pd` as its
home directory. If the class is missing, `rc.subr` falls back to the `daemon`
class and the 4096-descriptor limit is not applied.

The script runs `i2pd --daemon --service` as `_i2pd`, with `/var/i2pd` as the
data directory, `/etc/i2pd` for configuration and `/var/i2pd/certificates` for
certificates. Because the daemon backgrounds itself, a successful `rcctl
start` means the parent fork succeeded, not that the router finished
initializing; use `rcctl check i2pd` and inspect the log or pidfile for startup
errors. `rcctl stop` sends `SIGTERM` (ordered shutdown, no grace period), while
`rcctl reload` sends `SIGHUP` (tunnel configuration reload). A graceful
shutdown that waits for transit tunnels is requested explicitly:

```sh
kill -INT "$(cat /var/i2pd/i2pd.pid)"
```

Log rotation should send `SIGUSR1` after moving the file. Adjust the
port/package variables (`TRUEPREFIX`, `LOCALSTATEDIR`, `SYSCONFDIR`) only
through the ports framework, not by editing the installed copy.

See the [OpenBSD dynamic validation
checklist](../../docs/security/openbsd-validation.md) for the checks that have
not been executed in this tree.

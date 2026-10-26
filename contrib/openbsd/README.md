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
`/usr/local/share/i2pd/certificates` as `certsdir`; the sample configuration in this tree
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
stdio rpath wpath cpath fattr inet dns unix flock recvfd
```

plus `mcast` when UPnP is enabled. `recvfd` is the bounded worker-channel
receive promise; there is no `exec`, `id`, `chown`, `route`, `settime` or
arbitrary descriptor-passing API. When `privsep.log` is active, the
runtime core retains `proc` only for bounded supervisor reaping/signalling;
`exec` is never retained at runtime. Startup uses a temporary superset
(`proc`, `exec`, and `unveil` when enabled) for `fork`, `setsid`, `setrlimit`,
PID locking and worker spawning; `exec` is dropped before any service thread or
network socket exists.

Additional notes:

- `unix` permits the TUI and I2PControl local sockets, including
  bind/listen/accept and peer credentials; `stdio` covers poll and
  receive/send, `cpath` covers removing the socket, and `fattr` covers the
  0600 `chmod`.
- With `log = syslog` and a core-owned sink, `/dev/log` is unveiled with `w` (a delegated log worker owns the sink instead). The `unix` promise
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
  `rwc`. Use absolute paths. `openbsd.pledge_file` is a fail-closed **subset
  selector**, not an additive list: every token must already belong to the
  phase base set, the runtime set is always a strict subset of the startup set,
  and comments, blank lines, unknown tokens or a dangerous runtime token abort
  startup. `rpath`/`cpath` are required to remove the pidfile and the TUI
  socket at shutdown, and `fattr` is required when saving keys; those promises
  are already in the baseline and must not be widened elsewhere.
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

`i2pd.rc` is the canonical OpenBSD rc.d template for the `net/i2pd` port.
`port/` contains a merge template (`Makefile`, `PLIST` and `pkg/README`) for
the external ports tree; it is not installed or built by CMake.
Install it as `/etc/rc.d/i2pd` (or let the port's PLIST `@rcscript` do so).
The ports framework may substitute `TRUEPREFIX`, `LOCALSTATEDIR` and
`SYSCONFDIR`, so compare the rendered file rather than the template. Install
`i2pd.login` as `/etc/login.conf.d/i2pd`; `rc.subr` selects the `i2pd` login
class automatically when that file exists:

```sh
install -m 0644 i2pd.login /etc/login.conf.d/i2pd
# only needed when /etc/login.conf.db exists
cap_mkdb /etc/login.conf
rcctl enable i2pd
rcctl start i2pd
```

The port must create `_i2pd` with login class `i2pd` and install
`login.conf.d/i2pd`; the PLIST `@newuser` line binds the class explicitly.
If the class is missing, `rc.subr` falls back to the `daemon` class and the
8192-descriptor/umask policy is not applied. Verify it with
`getcap -f /etc/login.conf.d/i2pd i2pd`, not `id -c` (the password database has
no login-class field). A minimal manual account setup is:

```sh
useradd -class i2pd -s /sbin/nologin -d /var/i2pd _i2pd
```

The script runs `i2pd --foreground --service` as `_i2pd` with an explicit
pidfile, `/var/i2pd` as the data directory, `/etc/i2pd` for configuration and
`/usr/local/share/i2pd/certificates` for certificates; `rc_bg=YES` lets rc.subr background
the foreground process. A successful `rcctl start` means rc.subr started the
process, not that the router finished initializing; use `rcctl check i2pd` and
inspect the log or pidfile for startup errors. The template sets
`rc_stop_signal=INT` and `daemon_timeout=660`, so `rcctl stop` requests the
daemon's ordered shutdown plus the transit-tunnel drain and waits up to 660
seconds before rc.subr escalates. With no transit tunnels the router exits
promptly. `rcctl reload` sends `SIGHUP` (tunnel configuration reload).

The TUI is a client, not a service. The default socket is mode 0600 inside a
0700 `/var/i2pd`, so run the client as the service account (or configure a
socket path with matching ownership):

```sh
doas -u _i2pd i2pd-tui --datadir=/var/i2pd
```

Log rotation should send `SIGUSR1` after moving the file. Adjust the
port/package variables (`TRUEPREFIX`, `LOCALSTATEDIR`, `SYSCONFDIR`) only
through the ports framework, not by editing the installed copy.

See the [OpenBSD dynamic validation
checklist](../../docs/security/openbsd-validation.md) for the checks that have
not been executed in this tree.

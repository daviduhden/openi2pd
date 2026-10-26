<!--
See the LICENSE file at the top of the project tree for copyright
and license details.

openi2pd: README.

Documents the SysV init integration.

-->

# SysV init integration

`i2pd` is a foreground daemon: `daemon=false` is the code default and
`--daemon` is never passed by this script. The LSB script starts the process
with `start-stop-daemon --background` (or a narrow `su` fallback when
`start-stop-daemon` is absent) as the unprivileged `i2pd` user, with a private
runtime directory and the packaged configuration paths.

This integration targets systems that still use SysV init or its compatibility
layer. It is not a replacement for the native systemd unit.

## Installation

Install the router and its packaged configuration first:

```sh
install -m 0755 /path/to/i2pd /usr/bin/i2pd
install -d -m 0755 /etc/i2pd
install -m 0644 contrib/i2pd.conf /etc/i2pd/i2pd.conf
install -m 0644 contrib/tunnels.conf /etc/i2pd/tunnels.conf
install -d -m 0755 /etc/i2pd/tunnels.conf.d
```

Create the service account and its private directories:

```sh
# Debian/Ubuntu
adduser --system --group --home /var/lib/i2pd --no-create-home \
	--shell /usr/sbin/nologin i2pd

install -d -m 0700 -o i2pd -g i2pd /var/lib/i2pd /var/log/i2pd
```

`/run/i2pd` lives on a volatile filesystem; the init script creates it with
mode 0700 on every start, so it does not need to be persisted.

Install the script and its defaults file:

```sh
install -m 0755 contrib/sysvinit/i2pd /etc/init.d/i2pd
install -m 0644 /dev/null /etc/default/i2pd
```

A minimal `/etc/default/i2pd`:

```sh
I2PD_ENABLED=yes
DAEMON_OPTS=
I2PD_USER=i2pd
I2PD_GROUP=i2pd
I2PD_LOG=file
```

The script also accepts `I2PD_CONF`, `I2PD_TUNCONF`, `I2PD_TUNNELSDIR`,
`I2PD_CERTSDIR`, `I2PD_DATADIR`, `I2PD_PIDFILE`, `I2PD_LOGFILE` and
`I2PD_NOFILE`. Empty values fall back to the packaged defaults rather than
disabling a required path.

Enable the service with the distribution's SysV tool:

```sh
# Debian/Ubuntu
update-rc.d i2pd defaults

# Other SysV implementations
chkconfig --add i2pd
chkconfig i2pd on
```

## Management

```sh
/etc/init.d/i2pd start
/etc/init.d/i2pd status
/etc/init.d/i2pd reload
/etc/init.d/i2pd restart
/etc/init.d/i2pd stop
```

Equivalently, use `service i2pd <action>` on distributions that provide the
`service` wrapper. Supported actions are `start`, `stop`, `restart`,
`force-reload`, `reload` and `status`.

Signals have the same meaning as for a manual foreground run:

- `SIGINT` performs an orderly shutdown and may wait up to ten minutes for
  existing tunnels to expire. The script sends `SIGKILL` only after that grace
  period.
- `SIGHUP` reloads tunnel configuration without restarting the router.
- `SIGUSR1` reopens log files, for example after external rotation.

`start` is idempotent: an already running instance is reported and is not
started a second time. `stop` uses `--oknodo`, so stopping an already stopped
service succeeds; after the stop attempt the script re-checks the process and
returns failure if the router survived `SIGKILL`.

`reload` sends `SIGHUP` and only reloads tunnels. `force-reload` performs a
full stop/start because `i2pd.conf` is read only at startup. `status` returns
0 (running), 1 (dead with a stale pidfile) or 3 (not running).

`start` does not prove router readiness: it waits up to ten seconds for the
pidfile and verifies that the PID belongs to the configured executable where
`/proc` is available, but the router may still be reseeding. Check the log and
the TUI/admin status after starting.

`I2PD_ENABLED` accepts `yes`, `true` and `1` (case-insensitive) as enabled; any
other value disables the service, and `start` then reports success without
launching the router.

## Logging

The SysV default is `I2PD_LOG=file` with
`/var/log/i2pd/i2pd.log` (directory 0700, file 0600). The script creates both
with `install` under `umask 0077` when they are missing. The packaged
`i2pd.logrotate` policy uses `copytruncate`, so no signal is required for
rotation; `kill -USR1 "$(cat /run/i2pd/i2pd.pid)"` is the alternative if the
log is moved and reopened externally.

`I2PD_LOG=syslog` sends messages to syslog and adds a weak `$syslog` ordering
through the LSB `Should-Start`/`Should-Stop` fields. `I2PD_LOG=stdout` leaves
logging on the daemon's standard output, which under SysV is normally the init
system's `/dev/null`; use it only when the script is itself supervised by a
process manager that captures output. Use `I2PD_LOG=file` for an independent
SysV boot.

`I2PD_NOFILE` sets the open-file limit before the daemon starts; 8192 is the
default and is ignored with a warning if it is not numeric or cannot be raised.

## Do not run systemd and SysV together

Install and enable exactly one supervisor. On a systemd host, use
`contrib/i2pd.service` and do not enable `/etc/init.d/i2pd`; the systemd
sysv-generator may also expose an `i2pd.service` name for the script, and
starting both paths creates two routers competing for the same data directory,
listening ports and pidfile. If a SysV setup is converted to systemd:

```sh
update-rc.d i2pd disable
systemctl enable --now i2pd.service
```

On a SysV host, do not install or enable the native unit. Disable the previous
supervisor before switching in either direction.

## Security notes

The daemon runs as `i2pd:i2pd`, keeps `i2pd.conf` and tunnel configuration
root-managed under `/etc/i2pd`, and writes state only below `/var/lib/i2pd`
and `/run/i2pd`. The local TUI/torrent IPC socket is created in the data
directory with mode 0600; it must remain on a local filesystem shared with the
client. Do not point `I2PD_DATADIR` at a world-writable location.

The log worker is the only component currently isolated by the privilege
separation work. Do not describe this init integration, or the router as a
whole, as fully privilege-separated or as ready for untrusted multi-user hosts
on that basis. No new capabilities are granted; binding a privileged port
requires an explicit capability and is not configured here.

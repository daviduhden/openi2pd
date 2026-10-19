<!--
See the LICENSE file at the top of the project tree for copyright
and license details.

openi2pd: README.

Documents the OpenRC service integration for the openi2pd router.

-->

# OpenRC service

`i2pd.openrc` is the native OpenRC runscript. Select it with
`-DI2PD_SERVICE_MANAGER=openrc`. CMake installs it as `/etc/init.d/i2pd`,
installs `conf.d/i2pd` as `/etc/conf.d/i2pd`, and `openrc-run` sources that
file before the runscript. The service runs the
router in the foreground under `supervise-daemon`; it does not use a manual
respawn loop or a self-daemonising mode.

## Requirements

The `i2pd` user and group must exist. The runscript creates only the paths it
owns:

| Path | Mode | Owner | Purpose |
|---|---|---|---|
| `/run/i2pd` | `0700` | `i2pd:i2pd` | runtime directory |
| `/var/lib/i2pd` | `0700` | `i2pd:i2pd` | router data and keys |
| `/var/log/i2pd` | `0750` | `i2pd:i2pd` | log directory (file logging only) |
| `/var/log/i2pd/i2pd.log` | `0640` | `i2pd:i2pd` | log file (file logging only) |

`checkpath` performs the preparation in `start_pre`. The pidfile at
`/run/i2pd/i2pd.pid` is deliberately not created by the runscript: the daemon
opens it with `O_NOFOLLOW`, restricts it to `0600`, locks it, and removes it on
a clean shutdown. OpenRC's supervisor state is kept in a separate pidfile
(`/var/run/supervise-i2pd.pid` by default), so the two roles do not collide.

The packaged configuration is `/etc/i2pd/i2pd.conf`; tunnel definitions are
read from `/etc/i2pd/tunnels.conf` and `/etc/i2pd/tunnels.conf.d`. Both the
configuration and the tunnel directory are root-managed.

## Enable and control

```sh
rc-update add i2pd default
rc-service i2pd start
rc-service i2pd status
rc-service i2pd restart
rc-service i2pd stop
rc-update del i2pd default
```

Enablement is handled only by `rc-update`; there is no duplicate
`I2PD_ENABLED` switch in `conf.d`.

`supervise-daemon` restarts a crashed router after 5 seconds, gives up after 5
restarts within 60 seconds, and exposes the native
`rc-service i2pd start|stop|restart|status` actions. A successful `start` or a
`status` of `started` means that the supervisor and process are running, not
that the router has built tunnels or completed reseeding; there is no readiness
notification.

`rc-update` enablement is enough for normal operation. The runscript also adds
two OpenRC commands:

```sh
rc-service i2pd reload        # SIGHUP: reload tunnel configuration
rc-service i2pd reopenlogs    # SIGUSR1: reopen log files after rotation
```

The default stop signal is `SIGTERM`, which stops the router without the
ten-minute transit-tunnel grace period; OpenRC allows 30 seconds for the
ordered shutdown to save state before escalating. To make
`rc-service i2pd stop` request the graceful `SIGINT` path instead, uncomment
the native `stopsig` and `retry` settings in `/etc/conf.d/i2pd`; the daemon
then has up to ten minutes to drain before OpenRC escalates.

## Configuration variables

`/etc/conf.d/i2pd` is the single place for service paths, the account, logging
and extra options:

| Variable | Default | Notes |
|---|---|---|
| `I2PD_USER`, `I2PD_GROUP` | `i2pd`, `i2pd` | passed to `supervise-daemon` as `command_user` |
| `I2PD_CONF` | `/etc/i2pd/i2pd.conf` | main configuration; must be readable |
| `I2PD_TUNCONF` | `/etc/i2pd/tunnels.conf` | main tunnel configuration |
| `I2PD_TUNNELSDIR` | `/etc/i2pd/tunnels.conf.d` | additional tunnel fragments |
| `I2PD_CERTSDIR` | `/usr/share/i2pd/certificates` | packaged trust certificates |
| `I2PD_DATADIR` | `/var/lib/i2pd` | router data and keys |
| `I2PD_RUNDIR` | `/run/i2pd` | private runtime directory |
| `I2PD_PIDFILE` | `/run/i2pd/i2pd.pid` | daemon-owned pidfile |
| `I2PD_LOG` | `file` | `file`, `syslog` or `stdout` |
| `I2PD_LOGDIR` | `/var/log/i2pd` | dedicated directory, forced to `0750` |
| `I2PD_LOGFILE` | `/var/log/i2pd/i2pd.log` | used by `file` and captured `stdout` |
| `I2PD_OPTS` | empty | extra command-line options |

When `I2PD_LOG=file`, the runscript passes `--log=file --logfile=...` and
prepares the file with mode `0640`. When it is `syslog`, no log directory or
file is touched, `--log=syslog` is passed, and the weak `use logger` dependency
orders the service after the system logger when one is present. With
`I2PD_LOG=stdout`, `supervise-daemon` captures the output into
`I2PD_LOGFILE` with mode `0600`, so logs are not discarded when there is no
attached terminal. Other values are rejected during `start_pre`. Use `/etc/i2pd/i2pd.conf` or `I2PD_OPTS` for the daemon's
own log level; do not set a second `--log` option.

If an external rotator moves the file instead of using `copytruncate`, run
`rc-service i2pd reopenlogs` after the move so the daemon opens the new file.

## Security notes

`/etc/conf.d/i2pd` is root-controlled; extra options are visible in the process
list and must not contain secrets. The router process runs as `i2pd:i2pd` and
the log, data and runtime directories are not shared with other accounts. Only
the optional log worker is isolated by the project's privilege-separation
support; no network or storage workers exist yet, so the service provides only
that single-worker isolation.

The TUI (`i2pd-tui`) is an on-demand client for the local read-only IPC socket.
It is not a service and must not be added to a runlevel.

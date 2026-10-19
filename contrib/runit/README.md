<!--
See the LICENSE file at the top of the project tree for copyright
and license details.

openi2pd: README.

Documents the runit service tree for the openi2pd router.

-->

# runit service

The service tree contains a foreground router service and a separate logger:

```text
contrib/runit/i2pd/
|-- run
`-- log/
    `-- run
```

Select it with `-DI2PD_SERVICE_MANAGER=runit`. CMake installs this tree as
`/etc/sv/i2pd` by default, with the executable bits intact.
`run` prepares the runtime and data directories as root, then uses `chpst` to
drop to `i2pd:i2pd` and replaces the shell with `/usr/bin/i2pd`. The router
stays in the foreground and writes to stdout; `log/run` creates the log
directory and runs `svlogd -tt` as the same account. There is no `finish`
script and no manual restart or PID management.

## Install

The `i2pd` user and group must exist before the service starts.

```sh
# After `cmake --install` has installed /etc/sv/i2pd:
ln -s /etc/sv/i2pd /var/service/i2pd
```

Use the distribution's service-link directory when it is not `/var/service`
(for example `/run/runit/service` or `/etc/service`). The `log` subdirectory is
part of the same tree, so runsv starts the logger automatically when the main
service is linked.

The service uses the packaged paths:

- `/etc/i2pd/i2pd.conf`
- `/etc/i2pd/tunnels.conf`
- `/etc/i2pd/tunnels.conf.d`
- `/var/lib/i2pd` for router data and keys
- `/run/i2pd/i2pd.pid` for the daemon-owned pidfile
- `/var/log/i2pd` for `svlogd`

`run` creates `/run/i2pd` and `/var/lib/i2pd` with mode `0700` and ownership
`i2pd:i2pd`. `log/run` creates `/var/log/i2pd` with mode `0750` and runs
`svlogd` with umask `027`, so `current` is created with mode `0640`. Both
scripts must remain root-owned and must not be writable by the `i2pd` account,
because they perform their preparation before dropping privileges.

## Control

```sh
sv status i2pd
sv start i2pd        # alias for `sv up`
sv stop i2pd         # alias for `sv down`
sv restart i2pd
sv reload i2pd       # SIGHUP: reload tunnel configuration
sv 1 i2pd            # SIGUSR1: reopen daemon log files
sv status i2pd/log   # or /var/service/i2pd/log
```

`sv stop` (or `sv down`) sends `SIGTERM` to the router and stops the supervised
service.
`sv restart` uses runsv's normal term/cont/up sequence. `sv interrupt i2pd`
sends `SIGINT`, which starts the daemon's ten-minute graceful drain; because
runsv restarts an exiting service unless it is down, use that when the router
should remain supervised, and use `sv down` for an ordinary service stop.

runsv supervises the process, not the router's internal readiness. A status of
`run` means the process is running, not that tunnels are built or reseeding has
finished. The pidfile is written and locked by the daemon for operators and
other tools; runsv does not need it, and it must not be pre-created or edited
by the service scripts. The daemon's lock and stale-file handling prevent a
second instance from starting.

The logger is an independent supervised service. `sv status i2pd/log` shows
its state, and `sv restart i2pd/log` restarts only `svlogd`. `svlogd` performs
its own rotation and timestamping; do not add `logrotate` handling for
`/var/log/i2pd` on this installation.

## Security notes

The router runs as `i2pd:i2pd`; the configuration and tunnel files are
root-managed. Only the optional log worker is isolated by the project's
privilege-separation support and no network or storage workers exist yet, so
the service provides only that single-worker isolation.

The TUI (`i2pd-tui`) is a separate on-demand client for the local read-only
IPC socket. It is not a service and must not be added to the service tree or
started by runsv.

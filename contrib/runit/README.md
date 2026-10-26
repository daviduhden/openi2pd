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
|-- finish
`-- log/
    |-- run
    `-- config
```

Select it with `-DI2PD_SERVICE_MANAGER=runit`. CMake installs this tree as
`/etc/sv/i2pd` by default, with the executable bits intact.
`run` prepares the runtime and data directories as root, then uses `chpst` to
drop to `i2pd:i2pd`, applies `RLIMIT_NOFILE=8192` and replaces the shell with
`/usr/bin/i2pd`. The router stays in the foreground and writes to stdout;
`log/run` creates the log directory and runs `svlogd -tt` as the same account,
with rotation controlled by `log/config`. `finish` pauses five seconds and
records the exit status before runsv restarts the router, so a persistent
configuration failure is a slow, visible retry instead of a hot loop.

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
sv status i2pd/log   # or /var/service/i2pd/log
```

`sv stop` (or `sv down`) sends `SIGTERM` to the router and stops the supervised
service. `SIGTERM` performs the router's ordered stop: listeners close, state is
flushed, and the IPC workers are joined. This is the correct default for
runit. `sv restart` uses runsv's normal term/cont/up sequence.

runit has no per-service stop timeout and stock `runsv` has no `down-signal`
or `timeout-kill` file, so this tree deliberately ships no `down-signal`. A
`SIGINT` would start the ten-minute transit-tunnel drain, but runsv restarts an
exiting service unless it is down and offers no configurable window. For a
drain under runit, send `SIGINT` to the main PID, wait for it to exit, then
run `sv down`; do not add a `down-signal` file and expect a longer window.
`sv 1 i2pd` sends `SIGUSR1`, which only reopens a file log; this service uses
`--log=stdout`, so it is not useful here.

runsv supervises the process, not the router's internal readiness. A status of
`run` means the process is running, not that tunnels are built or reseeding has
finished. The pidfile is written and locked by the daemon for operators and
other tools; runsv does not need it, and it must not be pre-created or edited
by the service scripts. The daemon's lock and stale-file handling prevent a
second instance from starting.

The logger is an independent supervised service. `sv status i2pd/log` shows
its state, and `sv restart i2pd/log` restarts only `svlogd`. `svlogd` performs
its own rotation (`log/config`: 1 MiB, ten files) and timestamping; do not add
`logrotate` handling for `/var/log/i2pd` on this installation.

runsv signals only its direct child. The router must therefore stop its own IPC
workers during the ordered stop; the service does not signal them directly.

## Security notes

The router runs as `i2pd:i2pd`; the configuration and tunnel files are
root-managed. Only the optional log worker is isolated by the project's
privilege-separation support and no network or storage workers exist yet, so
the service provides only that single-worker isolation.

The TUI (`i2pd-tui`) is a separate on-demand client for the local read-only
IPC socket. It is not a service and must not be added to the service tree or
started by runsv.

<!--
See the LICENSE file at the top of the project tree for copyright
and license details.

openi2pd: README.

Documents the plain s6 service directory for the openi2pd router.
-->

# openi2pd under s6

`i2pd/run` is a plain [s6](https://skarnet.org/software/s6/) service
directory. It is not an s6-rc definition; the s6-rc source form is in
`contrib/s6-rc/source/i2pd`. Use one or the other, not both for the same
service name.

## Requirements

- A root-owned s6 supervision tree (`s6-svscan`/`s6-supervise`). The run script
  creates directories and changes user, so it must start with root privileges.
- An `i2pd` user and group whose primary group is `i2pd`.
- `/usr/bin/i2pd`, `/etc/i2pd/i2pd.conf`, `/etc/i2pd/tunnels.conf` and the
  `/etc/i2pd/tunnels.conf.d` directory. A missing main configuration file is a
  fatal startup error. Do not set `daemon = true` in the configuration: s6
  needs the router to remain in the foreground, and the packaged file leaves
  that option disabled.
- The certificate directory, normally `/usr/share/i2pd/certificates`,
  populated by the package. The service passes that path explicitly but does
  not create or populate it. A distribution that installs certificates into
  the data directory can override the path in the service definition.
- A syslog implementation reachable through `/dev/log`. The run script passes
  `--log=syslog`; it deliberately uses no s6 logging pipeline and no
  root-owned log file.
- `execline` and the s6 command set in the supervision tree's `PATH`.

The run script runs `/usr/bin/install` to create `/run/i2pd` and
`/var/lib/i2pd` with mode 0700 and ownership `i2pd:i2pd` on every start. It
then uses `s6-setuidgid` and execs the router in the foreground with `--service`,
explicit configuration, data, certificate and PID paths, and `--log=syslog`.
There is no shell wrapper and no `--daemon`; the PID file is written for
administrative tools, not for supervision.

## Install with plain s6

This example keeps the service directory under `/var/s6` and links it into an
existing scan directory. Adapt both paths to the local supervision layout.

```sh
install -d -m 0755 /var/s6/i2pd
install -m 0755 contrib/s6/i2pd/run /var/s6/i2pd/run
ln -s /var/s6/i2pd /run/service/i2pd
s6-svc -u /run/service/i2pd
```

`/run/service` must already be supervised by `s6-svscan`. Recreate the link or
copy at boot if `/run` is a tmpfs. Do not put s6-supervise state on a read-only
filesystem: s6-supervise creates its `supervise/` directory inside the linked
service directory.

## Control and logs

```sh
s6-svstat /run/service/i2pd      # state and PID
s6-svc -l /run/service/i2pd      # SIGHUP: reload tunnels
s6-svc -1 /run/service/i2pd      # SIGUSR1: reopen the log destination
s6-svc -d /run/service/i2pd      # down, no automatic restart
s6-svc -u /run/service/i2pd      # up and restart on death
```

Logs go to syslog with the daemon facility, so no `log/` subdirectory or
`s6-log` service is needed. `SIGUSR1` is still passed through for compatibility,
but the syslog destination is not a file owned by this service.

s6 has no readiness protocol for this daemon. The run script does not create a
`notification-fd` file, and the service is considered started when
s6-supervise execs the run script, not when i2pd has joined the network. The
s6-rc up transition likewise completes without a readiness notification; a
warning from s6-rc about the missing notification is expected.

## Stopping and graceful shutdown

The service ships a `down-signal` file containing `INT`, so a normal down
transition requests the graceful shutdown that waits for transit tunnels (up
to ten minutes). `timeout-kill` is set to 660000 ms, matching that drain plus a
shutdown margin. s6 escalates to `SIGKILL` only after the window; use
`s6-svc -x` only for an intentional immediate teardown.

No `finish` script is installed. The daemon removes its PID file on an orderly
shutdown; after a forced `SIGKILL` the stale file is harmless because the next
start reopens and locks it. A finish script would risk deleting a PID file
belonging to a newer instance.

## Scope

The service does not provide the TUI: `i2pd-tui` is an optional client and the
daemon does not depend on it. It also does not enable or claim full privilege
separation. Only the isolated log worker exists; enabling `[privsep] log` is a
separate configuration decision.

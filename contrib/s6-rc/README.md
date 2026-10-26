<!--
See the LICENSE file at the top of the project tree for copyright
and license details.

openi2pd: README.

Documents the s6-rc source definition for the openi2pd router.
-->

# openi2pd under s6-rc

`source/i2pd` is an s6-rc *source definition*, not a live s6 service
directory. It is compiled into a service database with `s6-rc-compile`; s6-rc
then creates the live service directory and links it into the supervision
scan directory. The standalone form is in `contrib/s6/i2pd/run`; install one or
the other, not both under the same name.

## s6 supervision and s6-rc definitions

The two layers are distinct:

- **s6** supervises processes. `s6-supervise` runs a service directory's `run`
  program, restarts it according to its `down`/`finish` state, and exposes
  control through `s6-svc`. It has no dependency graph, bundle or one-shot
  notion.
- **s6-rc** manages a compiled service database. It resolves dependencies and
  bundles, orders up/down transitions, creates `down` files and asks
  `s6-supervise` to start or stop each longrun. It does not replace
  `s6-supervise`; both are needed.

The `type` file contains `longrun`. The `run` file is copied into the generated
service directory by `s6-rc-compile`, so it must stay executable (0755). Do not
point `s6-svscan` directly at the source tree.

## Prerequisites

- A root-owned s6 supervision tree. The run script creates directories and
  changes user, so it must start with root privileges.
- An `i2pd` user and group whose primary group is `i2pd`.
- `/usr/bin/i2pd`, `/etc/i2pd/i2pd.conf`, `/etc/i2pd/tunnels.conf` and the
  `/etc/i2pd/tunnels.conf.d` directory. A missing main configuration file is a
  fatal startup error. Do not set `daemon = true` in the configuration: s6-rc
  needs the router to remain in the foreground, and the packaged file leaves
  that option disabled.
- The certificate directory, normally `/usr/share/i2pd/certificates`,
  populated by the package. A distribution that installs certificates into
  the data directory can override the path in the service definition.
- A syslog daemon reachable through `/dev/log`.

The run script creates `/run/i2pd` and `/var/lib/i2pd` itself, so no separate
runtime-directory unit is needed.

## Dependencies

`dependencies.d/base` is an empty marker that makes i2pd depend on the local
`base` bundle. `base` is the conventional name for the early system bundle; the
database must actually define it or `s6-rc-compile` will reject the dependency.
No network or syslog service is hard-coded: native s6-rc has no standard names
for them. Add those names to `dependencies.d/` only when the local database
provides them. Without a syslog dependency i2pd may start before the logger and
lose the first messages, but it keeps running.

## Install and compile

```sh
install -d -m 0755 /etc/s6-rc/source/i2pd
cp -a contrib/s6-rc/source/i2pd/. /etc/s6-rc/source/i2pd/
install -d -m 0755 /etc/s6-rc/compiled

db=/etc/s6-rc/compiled/$(date +%Y%m%d%H%M%S)
s6-rc-compile "$db" /etc/s6-rc/source
ln -sfn "$db" /etc/s6-rc/compiled/current
```

`s6-rc-compile` takes the output directory first and one or more source
directories after it. `current` is a symlink to the database that
`s6-rc-init` should use at boot. A compiled database that has become live must
not be modified or deleted; use `s6-rc-update` to replace it.

At boot, `s6-svscan` must already be running on the scan directory (for
example `/run/service`) before s6-rc is initialised. With s6-linux-init this
is normally done by the installed stage 2 script; otherwise call it once from
early boot:

```sh
s6-rc-init /run/service
s6-rc -u change i2pd
```

`s6-rc-init` uses `/etc/s6-rc/compiled/current` by default, stores live state
in `/run/s6-rc`, and adds `down` files so that services do not start during
initialisation. It is an initialisation command, not a per-boot service start.

## Updating a live database

Compile a new database without overwriting the live one, switch with
`s6-rc-update`, and only then move the `current` symlink:

```sh
db=/etc/s6-rc/compiled/$(date +%Y%m%d%H%M%S)
s6-rc-compile "$db" /etc/s6-rc/source
s6-rc-update "$db"
ln -sfn "$db" /etc/s6-rc/compiled/current
```

The live database must not be modified or deleted. `s6-rc-update` is the only
supported way to replace it.

## Control

```sh
s6-rc -u change i2pd       # start, with dependencies
s6-rc -d change i2pd       # stop
s6-rc reload i2pd          # SIGHUP: reload tunnels
s6-rc -a list              # currently active services
s6-svstat /run/service/i2pd
```

The run script creates `/run/i2pd` and `/var/lib/i2pd` with mode 0700 and
ownership `i2pd:i2pd`, applies `s6-softlimit -o 8192`, drops privileges with
`s6-setuidgid`, and execs `/usr/bin/i2pd` in the foreground with `--service`,
explicit configuration, data, certificate and PID paths, and `--log=syslog`.
It has no shell wrapper and never passes `--daemon`. The daemon writes
`/run/i2pd/i2pd.pid`, but s6 supervision uses the process it started, not the
PID file.

Logs are sent to syslog, so there is no s6 logging pipeline, logger service or
`notification-fd`. i2pd has no readiness protocol: s6-rc considers the longrun
up when the run script has been executed, not when the router is ready. s6-rc
does not print a warning for a longrun without `notification-fd`. Do not invent
a notification file.

## Signals and shutdown

`SIGINT` is the graceful stop (up to ten minutes for transit tunnels),
`SIGHUP` reloads tunnels, and `SIGUSR1` reopens a file log destination. The
source ships `down-signal` (`INT`), `timeout-kill` (660000 ms) and
`timeout-finish` (10000 ms), so the normal s6-rc down transition requests the
graceful path, s6 escalates to `SIGKILL` only after that window, and the
five-second `finish` backoff is not cut short. Recompile the source database
after changing any of them.

`finish` never touches the daemon-owned PID file; the next instance reopens and
locks it. s6-supervise signals only the run script's process, so the router
stops its own IPC workers during the ordered stop.

## Scope

The TUI is not a service. `i2pd-tui` is an optional client over the local
socket and the router does not depend on it. Only the isolated log worker
exists behind `[privsep]`; this service does not claim full privilege
separation.

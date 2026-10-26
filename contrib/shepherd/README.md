<!--
See the LICENSE file at the top of the project tree for copyright
and license details.

openi2pd: README.

Documents the native GNU Shepherd service definition for the openi2pd router.
-->

# openi2pd under GNU Shepherd

`i2pd.scm` defines one native Shepherd service with the `service` constructor,
registers it with `register-services`, and lets Shepherd start it as the
`i2pd` account in the foreground. It uses no shell, no `--daemon`, no custom
PID supervision and no readiness protocol.

## Requirements

- GNU Shepherd 0.10.5 or newer with the `(shepherd service)` module. The
  `#:grace-period` keyword and `#:requirement` slot are used by this file;
  verify both on 0.10.5 before merging into an older host. `register-services`
  is called with a list, which is the documented older form; keep it when the
  host requires it.
- Shepherd running as root, so `#:user` and `#:group` can drop privileges.
  Shepherd refuses the change if it is unprivileged.
- An `i2pd` user and group.
- `/usr/bin/i2pd`, `/etc/i2pd/i2pd.conf`, `/etc/i2pd/tunnels.conf` and the
  `/etc/i2pd/tunnels.conf.d` directory. A missing main configuration file is a
  fatal startup error. Do not set `daemon = true` in the configuration:
  Shepherd needs the router to remain in the foreground, and the packaged file
  leaves that option disabled.
- `/var/lib/i2pd`. The service keeps its pidfile there because Shepherd has no
  pre-start tmpfiles hook; create the directory before starting the service:

  ```sh
  install -d -m 0700 -o i2pd -g i2pd /var/lib/i2pd
  ```

- A syslog daemon reachable through `/dev/log`.

## Install and load

Install the file outside the Shepherd configuration itself:

```sh
install -d -m 0755 /etc/shepherd
install -m 0644 contrib/shepherd/i2pd.scm /etc/shepherd/i2pd.scm
```

Then load it from the configuration that the running Shepherd instance reads.
For a site whose configuration is `/etc/shepherd/init.scm`:

```scheme
;; /etc/shepherd/init.scm
(load "/etc/shepherd/i2pd.scm")
(start-in-the-background '(i2pd))
```

Native Shepherd reads `/etc/shepherd.scm` when started as root, so a system
that has not been configured to use `/etc/shepherd/init.scm` should put the
same two forms there instead, or start Shepherd with
`--config=/etc/shepherd/init.scm`.

Loading the file alone only defines and registers the service; it does not
start it. The `start-in-the-background` call is required for boot autostart.

`herd enable` does not provide boot autostart. It sets Shepherd's enabled flag
(for example after a respawn-limit disable); the `start-in-the-background`
call above is what starts the router when Shepherd starts.

## Control

```sh
herd start i2pd
herd stop i2pd
herd restart i2pd
herd status i2pd
herd enable i2pd
herd disable i2pd
```

`herd stop` sends `SIGINT`, the router's graceful shutdown, and Shepherd waits
up to the configured 660-second grace period before escalating to `SIGKILL`.
`make-kill-destructor` signals the **process group**, so the IPC workers also
receive `SIGINT`; the router still performs its ordered stop, but a strict
main-process-only worker drain is better served by systemd (`KillMode=mixed`),
OpenRC, runit or s6. `herd restart` performs a full stop and start.

Shepherd has no built-in reload action for this service. To reload tunnel
configuration without a restart, send `SIGHUP` to the main PID shown by
`herd status i2pd`, or use the daemon's PID file:

```sh
kill -HUP "$(cat /var/lib/i2pd/i2pd.pid)"
```

## Service behavior and scope

The start constructor passes `--service`, explicit configuration, tunnel,
data, certificate and PID paths, and `--log=syslog`. Logging through syslog
avoids `#:log-file`: Shepherd opens that file as root before `#:user` takes
effect, which can leave a root-owned file that the router cannot rotate or
reopen. The PID file is written for administrative tools; Shepherd supervises
the foreground child directly and does not use the PID file for readiness.

## Logging and rotation

Because the service logs through syslog, rotation belongs to the syslog daemon
(for example the distribution's `logrotate` rules for `/var/log/messages` or
`newsyslog.conf`). The shipped `contrib/i2pd.logrotate` fragment targets
`/var/log/i2pd/*.log` and is therefore unused here. It becomes relevant only if
the definition is switched to `--log=file`; in that case also set `logfile` to a
path inside a directory owned by `i2pd`, because a file created by root cannot
be reopened by the service account.

No `#:requirement` is declared. (The dependency keyword in this Shepherd
generation is `#:requirement`; older descriptions sometimes call it
`#:requires`, which current Shepherd does not accept.) Native Shepherd has no
standard service named `networking`, so requiring one by name would prevent
startup on systems that do not define it. If the local configuration does
provide a real prerequisite, add its actual provision name, for example
`#:requirement '(network)`, after verifying it is registered.

The TUI is not a service. `i2pd-tui` is an optional client over the local
socket; the router does not depend on it. The isolated log worker and the
opt-in torrent/router storage workers use separate processes; networking,
crypto and the router core are not separated. This definition makes no claim
of full privilege separation.

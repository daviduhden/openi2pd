<!--
See the LICENSE file at the top of the project tree for copyright
and license details.

openi2pd: service management.

Documents the native init-system definitions, Debian package integration and
the runtime paths, signals and logging used by the router service.

-->

# Service management

This page describes the native service definitions that the CMake `Service`
component can install and the Debian package integration. The supported Linux
managers are systemd, GNU Shepherd, OpenRC, runit, s6/s6-rc and SysVinit; a
package selects one manager. Legacy Upstart and dinit samples are retained in
`contrib/` in foreground form but are not installed by `I2PD_SERVICE_MANAGER`. The router is the only managed service: **there is
no TUI service**. `i2pd-tui` is an interactive client that connects to the
running router over its local IPC socket. It does not start, stop or supervise
the daemon and must not be placed in a service unit.

The commands below describe the installed files and manager behaviour. Runtime
start/stop checks, `systemd-analyze`, `lintian`, `autopkgtest` and equivalent
manager checks are **pending manual CI**; this page is static-only
documentation. See [Validation status](#validation-status).

## Runtime model

There is no readiness protocol in any definition: a start action reports only
that the manager executed the process.

The daemon defaults to foreground operation. `--daemon` is an explicit opt-in
and the native service definitions do **not** pass it: the service manager
stays attached to the process, owns its stdout/stderr and observes its exit.
The packaged systemd unit is `Type=exec`, not `Type=forking`. It does not send
`sd_notify` and there is no readiness protocol, so a successful
`systemctl start` means only that the process was executed. Check the journal
and the router status instead of treating the unit state as a network-ready
signal.

For a foreground definition, do not add `--daemon` to `ExecStart`, the runit
`run` script, the s6 `run` script, the Shepherd command or the OpenRC command.
The native definitions pass `--foreground`, which overrides a local
`daemon = true` configuration; without it, the manager could lose a process
that detaches itself.

systemd, runit, s6 and the foreground OpenRC/Shepherd modes keep the process
attached. SysVinit cannot supervise a foreground child in the same way; its
native script uses `start-stop-daemon --background` (or a narrow `su` fallback)
and a pidfile. The OpenBSD rc.d definition runs the daemon in the foreground
with `rc_bg=YES`, so rc.subr owns the backgrounding. In those two paths the
helper or rc.subr owns the process, and `start` does not prove router
readiness. See [SysVinit](#sysvinit) and [OpenBSD path](#openbsd-path).

### Signals, stop budget and exit codes

A service manager signals the router's main process. The daemon then performs
the ordered stop itself: it stops accepting work, closes the TUI/I2PControl
listeners, stops the client and router services, closes NetDb and transports,
flushes persisted state and stops its IPC log/storage workers. A manager that
signals the whole process group bypasses that ordering for the workers; see
the per-manager notes below.

| Signal | Behaviour |
|---|---|
| `SIGINT` (first) | Stop accepting new transit tunnels and give existing ones up to 600 s to expire, then run the ordered stop. |
| `SIGINT` (second) | Run the ordered stop immediately. |
| `SIGTERM` | Run the ordered stop immediately (prompt, no tunnel drain). |
| `SIGHUP` | Reload tunnel configuration; it does not add a new unveiled path or re-read `i2pd.conf`. |
| `SIGUSR1` | Reopen the configured log file after external rotation. |
| `SIGABRT` | No handler: an abort keeps its default disposition and terminates the process with a signal. |
| `SIGPIPE` | Logged and ignored. |

`SIGABRT` must never be converted into a clean shutdown: the service manager
has to observe a crash and apply its restart policy. The same applies to
`SIGSEGV`, `SIGBUS` and `SIGKILL`.

| Exit status | Meaning |
|---|---|
| `0` | Clean stop, including an orderly `SIGINT`/`SIGTERM` stop. |
| `1` | Configuration, pidfile, sandbox, log or startup failure, or a caught fatal exception. |
| `128+N` | Terminated by signal `N`; for example `137` is `SIGKILL` and `134` is `SIGABRT`. |

Restart-on-failure policies (`Restart=on-failure`, `respawn`, `restart =
on-failure`) must treat `0` as healthy and every non-zero or signal exit as a
failure. The drain budget is 600 s; a 660 s stop timeout leaves a 60 s margin
for the ordered stop body. Do not set a shorter timeout without measuring the
shutdown path.

### Manager matrix

| Manager | Supervision | Default stop signal | Stop budget | Log sink | Readiness |
|---|---|---|---|---|---|
| systemd | `Type=exec`, cgroup | `SIGINT`, main process only (`KillMode=mixed`) | 660 s | journal | none |
| OpenRC | `supervise-daemon` | `SIGTERM` (prompt); `stopsig=SIGINT` for the drain | 30 s default, 660 s with `SIGINT` | `file`, `syslog` or captured stdout | none |
| runit | `runsv` | `SIGTERM` (prompt ordered stop) | no per-service timeout | `svlogd` | none |
| s6 / s6-rc | `s6-supervise` | `SIGINT` (`down-signal`) | 660000 ms (`timeout-kill`) | syslog | none |
| SysVinit | background helper + pidfile | `SIGINT` | 660 s then `SIGKILL` | `file`, `syslog` or stdout | none |
| GNU Shepherd | `make-forkexec-constructor` | `SIGINT` to the process group | 660 s | syslog | none |
| OpenBSD rc.d | `rc.subr` background | `SIGINT` (`rc_stop_signal=INT`) | 660 s (`daemon_timeout`) | syslog | none |
| dinit (sample) | `type = process` | `SIGINT` | 660 s | captured file | none |
| Upstart (sample) | foreground respawn | `SIGINT` | 660 s | syslog + console log | none |

There is no readiness protocol and no `sd_notify`, `s6` notification or
equivalent in any definition. A successful `start` means the manager executed
the process, nothing more.

### Identity and paths

The Linux service definitions use a dedicated unprivileged account. The
defaults below are the packaged-service defaults, not universal constants.
Override them through the manager's native configuration and keep every path
consistent with the daemon command line.

| Purpose | Linux service default | Notes |
|---|---|---|
| User/group | `i2pd:i2pd` | The router does not need root. |
| Data | `/var/lib/i2pd` | Keys, netDb, profiles and torrents. |
| Runtime | `/run/i2pd` | Runtime directory; use `/run`, not `/var/run`. |
| Configuration | `/etc/i2pd` | `i2pd.conf`, `tunnels.conf`, `tunnels.conf.d`. |
| PID file | `/run/i2pd/i2pd.pid` in packaged Linux services | The daemon default is `<datadir>/i2pd.pid`; every packaged service passes `--pidfile` explicitly. |
| Logs | journald (systemd); secure file or syslog elsewhere | `journalctl` on systemd. |
| TUI socket | `i2pd-tui.sock` below the configured data directory, or an explicitly configured path | Mode 0600; not a service. |

A user process uses `~/.i2pd` and `~/.i2pd/i2pd.pid` instead. OpenBSD services
keep the existing `/var/i2pd` layout and are described separately in
[OpenBSD configuration](../contrib/openbsd/README.md). Override paths through
the manager's own configuration: a systemd drop-in, OpenRC's
`/etc/conf.d/i2pd` or `/etc/default/i2pd`, the runit/s6 `run` script, the
Shepherd service definition, or the SysV `/etc/default/i2pd` file.

If you move the data, runtime, configuration or socket paths, update the
service definition and the daemon's arguments together. A write path that is
missing from the service manager's sandbox or permission policy causes startup
or key-saving failures. The TUI socket remains mode 0600, so an interactive
client must run as the service account or use an explicitly configured socket
path with suitable ownership; do not weaken the socket to a world-writable
path.

## systemd

The packaged unit is `i2pd.service`; no second alias is installed. The unit
runs the router as `i2pd` in the foreground with `Type=exec`. It has no
`sd_notify` readiness handshake. The packaged sysusers/tmpfiles files let
`systemd-sysusers`/`systemd-tmpfiles` create the account and persistent/runtime
directories on package installation or boot.

```sh
sudo systemctl start i2pd.service
sudo systemctl stop i2pd.service
sudo systemctl restart i2pd.service
sudo systemctl status i2pd.service
sudo systemctl reload i2pd.service
sudo systemctl enable --now i2pd.service
sudo systemctl disable --now i2pd.service
```

`reload` sends `SIGHUP`, which reloads tunnel configuration. It does not add a
new filesystem path to the sandbox; restart after changing a configured path.
`enable --now` enables boot-time startup and starts the service immediately;
`disable --now` disables it and stops it. The unit restarts on failure with a
bounded start rate.

The packaged unit uses `KillSignal=SIGINT`, `KillMode=mixed` and
`TimeoutStopSec=660`: `systemctl stop` requests the graceful path, stops
accepting transit tunnels and may wait up to 10 minutes (600 s) for existing
tunnels to drain, plus a 60 s margin for the ordered stop. `SendSIGKILL=yes`
escalates after that timeout. `KillMode=mixed` is deliberate: the stop signal
reaches the main process only, while `SIGKILL` reaches the whole cgroup at the
deadline. The daemon owns the shutdown order for its IPC workers; signalling
the cgroup earlier would interrupt a storage write. `systemctl restart` and a
package upgrade therefore wait for the same drain. Passing `--no-block` to
`systemctl stop` returns immediately but does not abort the drain. `SIGTERM` is
the prompt ordered shutdown and is **not** the 10-minute graceful path; select
it in a custom unit or override `KillSignal` when prompt stopping is required.
A second `SIGINT` stops immediately. Inspect the installed policy rather than
assuming:

```sh
systemctl show -p Type -p KillSignal -p TimeoutStopUSec i2pd.service
systemctl cat i2pd.service
```

Logs go to journald by default. The unit sets `LogsDirectory=i2pd` (mode
0700), so the documented `log = file` opt-in with
`logfile = /var/log/i2pd/i2pd.log` also works under `ProtectSystem=strict`.
`SyslogIdentifier=open-i2pd` is a frozen compatibility identifier for journal
filters; the product name remains `openi2pd` and the unit is `i2pd.service`.

```sh
journalctl -u i2pd.service -f
journalctl -u i2pd.service -b
journalctl -t open-i2pd -f
```

Override service policy with a drop-in. For example, for a higher descriptor
limit:

```sh
sudo systemctl edit i2pd.service
```

```ini
[Service]
LimitNOFILE=16384
```

Then apply the change with `sudo systemctl daemon-reload` and
`sudo systemctl restart i2pd.service`. If you change `ExecStart` for a custom
prefix, clear the old value with an empty `ExecStart=` assignment before adding
the full replacement, and update `ReadWritePaths` for the new data, runtime and
log directories.

### Hardening and IPC

The packaged unit applies systemd hardening (`ProtectSystem=strict`,
`ReadWritePaths=/var/lib/i2pd /run/i2pd`, `ProtectHome=yes`,
`PrivateTmp=yes`, `RestrictNamespaces=yes`, an empty capability bounding set
and `NoNewPrivileges=yes`). It does not make `/etc/i2pd` writable. Keep the
data and runtime paths writable for the service account, and add any custom
data, runtime, pidfile or socket directory to `ReadWritePaths`. A custom unit
must stay compatible with:

- local `AF_UNIX` listeners and sockets, including the TUI/IPC socket and an
  I2PControl local socket;
- persistence of keys, netDb, profiles and torrent state under the data
  directory;
- file logging under `/var/log/i2pd` when `log = file` is selected; the
  packaged unit uses journald, so add that directory to `ReadWritePaths` and
  create it with `i2pd` ownership if you opt into file logging;
- the router's outbound network access.

Do not set `PrivateNetwork=true` for a router that must reach the I2P network.
Do not enable `ProtectHome=true` when a configured data directory lives under
`/home`, and account for `PrivateTmp=true` if a configured path lives under
`/tmp`; the packaged unit sets both and keeps the data under `/var/lib/i2pd`.
The packaged unit also sets `RestrictNamespaces=yes`. If the optional log
worker uses an empty network namespace or another worker sandbox needs
namespace creation, that restriction blocks it; relax only the specific
namespace that is required or keep that worker feature disabled. Do not weaken
unrelated restrictions.

These unit restrictions are defence in depth, **not full privilege
separation** and not a promise of full sandboxing. The log worker and the
opt-in torrent/router storage workers use separate processes; networking,
crypto and the router core remain pending. See
[Privilege separation](security/privilege-separation.md#service-manager-implications).

## GNU Shepherd

Shepherd uses `herd` and a Scheme service definition. The installed definition
must be registered in the shepherd configuration used at boot. Commands:

```sh
sudo herd status i2pd
sudo herd start i2pd
sudo herd stop i2pd
sudo herd restart i2pd
sudo herd enable i2pd
sudo herd disable i2pd
```

`herd enable`/`herd disable` set Shepherd's enabled flag (for example after a
respawn-limit disable); they do not by themselves create boot autostart. The
installed definition must still be loaded and started from the Shepherd
configuration with `start-in-the-background`, and `herd disable` does not
necessarily stop a running instance, so use `herd stop i2pd` as well when
stopping is intended.
On Guix System, declare the service in the operating-system configuration
instead of editing the live shepherd by hand, so the definition survives a
reconfiguration.

Shepherd does not guarantee a generic `reload` action. Use
`sudo herd restart i2pd`, or send `SIGHUP` to the configured pidfile when a
full restart is not wanted:

```sh
sudo kill -HUP "$(cat /var/lib/i2pd/i2pd.pid)"
```

The contributed definition runs with `--log=syslog`, uses a pidfile under the
data directory, and stops with `SIGINT` plus a 660-second grace period, so
`herd stop` and `herd restart` can wait for the router's tunnel drain rather
than stopping promptly. Shepherd's `make-kill-destructor` sends the signal to
the process group, not only to the main PID. The router still runs its ordered
stop, but the IPC workers also receive `SIGINT` directly; use the
systemd/OpenRC/runit/s6 definitions when a strict main-process-only worker
drain is required. There is no `#:log-file`: Shepherd opens it before
`#:user`, which can leave a root-owned file, so syslog is used instead. Redirect
stdout/stderr in a custom definition to syslog, a supervised log service or a
file with permissions for `i2pd`; do not rely on the invoking terminal.

## OpenRC

Install the init script and register it in a runlevel:

```sh
sudo rc-service i2pd start
sudo rc-service i2pd stop
sudo rc-service i2pd restart
sudo rc-service i2pd status
sudo rc-service i2pd reload
sudo rc-update add i2pd default
sudo rc-update del i2pd default
```

`reload` sends `SIGHUP` when the installed script defines a reload action;
otherwise restart the service. `rc-update add` enables the service at boot,
`rc-update del` disables it. Check the active runlevel with `rc-status default`.

The native OpenRC runscript uses `supervise-daemon` and keeps the router in the
foreground; its command must not pass `--daemon`. It sets `respawn_delay=5`,
`respawn_max=5` and `respawn_period=300`, matching the systemd start limit.
The retry grammar is `SIGNAL/TIMEOUT` (`SIGINT/660`, `SIGTERM/30`) as used by
`supervise-daemon --retry`. `rc_ulimit="-n 8192"` sets the descriptor budget. `start_pre` checks every `checkpath` and aborts on a missing
account, wrong mode or unwritable path. Some other OpenRC recipes use
`command_background=true` or `start-stop-daemon --background`; in that case the
OpenRC helper backgrounds the process and records its pidfile. Prefer the
supervised foreground form where available.

The native runscript reads `/etc/conf.d/i2pd`; the SysV script reads
`/etc/default/i2pd`. Inspect `/etc/init.d/i2pd` and the corresponding defaults
file before changing variables. Set the data, runtime, pidfile, configuration
and log paths there and keep them aligned with the daemon arguments. Configure
OpenRC's `output_log` and `error_log` in `/etc/conf.d/i2pd`, or use syslog, so
foreground output is not lost.

OpenRC's default stop signal is `SIGTERM`, so `rc-service i2pd stop` is prompt.
Set `stopsig="SIGINT"` in `/etc/conf.d/i2pd` for a graceful transit-tunnel
drain; the runscript derives its retry budget from the selected stop signal.

## runit

runit has no separate enable command: a service is enabled by a symlink in the
active service directory. On Void Linux the directory is normally
`/var/service`; generic installations use a `runsvdir` directory such as
`/etc/runit/runsvdir/default`.

```sh
sudo sv up i2pd
sudo sv down i2pd
sudo sv restart i2pd
sudo sv status i2pd
sudo sv reload i2pd
sudo ln -s /etc/sv/i2pd /var/service/i2pd
sudo rm /var/service/i2pd
```

`sv reload` sends `SIGHUP` (tunnel configuration reload). `sv down` sends
`SIGTERM` and is prompt; `sv interrupt` sends `SIGINT` but runsv restarts the
service unless it is also marked down. `sv restart` performs a stop/start
cycle. Adjust the symlink path to the distribution's `runsvdir` if it is not
`/var/service`. runit has no per-service stop timeout and stock `runsv` has no
`down-signal`/`timeout-kill` file, so the definition deliberately uses
`SIGTERM`: it performs the ordered stop promptly and never leaves a `SIGKILL`
race in the middle of a write. Do not add a `down-signal` file; `sv down`
would still not gain a configurable 600 s window. For the transit-tunnel
drain under runit, send `SIGINT` to the main PID and then stop the service
once it has exited.

The `run` script must execute the daemon in the foreground without `--daemon`.
The contributed service tree includes a companion `log/run` that runs `svlogd`
as `i2pd` under `/var/log/i2pd`, a `log/config` rotation policy (1 MiB, ten
files) and a `finish` script that pauses five seconds before runsv restarts a
failed router. Check the logger with `sv status i2pd/log`. `sv 1 i2pd` sends
`SIGUSR1`, which only reopens a file log; this service uses `--log=stdout`, so
it is a no-op here. `sv down` sends `SIGTERM` and is prompt; `sv interrupt
i2pd` sends `SIGINT` for the graceful tunnel drain, but runsv restarts an
exiting service unless it is also down. Set ownership and permissions on the
log directory for the `i2pd` account.

## s6 and s6-rc

A raw s6 service is a directory with a `run` script. The live service path is
installation-specific; common locations include `/run/service/i2pd` and
`/var/run/s6/services/i2pd`.

```sh
sudo s6-svc -u /run/service/i2pd
sudo s6-svc -d /run/service/i2pd
sudo s6-svc -r /run/service/i2pd
sudo s6-svc -l /run/service/i2pd
sudo s6-svc -1 /run/service/i2pd
sudo s6-svstat /run/service/i2pd
```

`-u` brings the service up, `-d` brings it down, `-r` restarts it, `-l`
requests a reload (`SIGHUP` by default; `-h` sends `SIGHUP` directly), `-1`
sends `SIGUSR1` to reopen a file log, and `s6-svstat` reports state.
The contributed service ships `down-signal` (`INT`), `timeout-kill`
(660000 ms) and `timeout-finish` (10000 ms), so `s6-svc -d` requests the
daemon's graceful drain, s6 escalates only after the shutdown window and the
five-second `finish` backoff is not cut short. The run script raises
`RLIMIT_NOFILE` with `s6-softlimit -o 8192`. Do not use `s6-svc -x` or other
teardown modes for an ordinary stop. The s6-rc source carries the same files.

When the installation uses a compiled s6-rc database, manage the service
through s6-rc instead:

```sh
sudo s6-rc -u change i2pd
sudo s6-rc -d change i2pd
s6-rc -a list
```

Use `s6-svc -r`, `s6-svc -l` and `s6-svstat` on the live service directory for
restart, reload and status; s6-rc performs the dependency-ordered up/down
transitions.

Enabling means including the service in the boot bundle's `contents.d` in the
s6-rc source database and rebuilding it with `s6-rc-compile` and
`s6-rc-update`; disabling means removing it from that bundle and applying the
same update. Use the source database and live directory selected by the
distribution (for example `/etc/s6-rc` and `/run/s6-rc`); do not edit a
compiled database in place. The `run` script must stay in the foreground. The
contributed s6 and s6-rc run scripts use `--log=syslog` and require a syslog
daemon at `/dev/log`; they do not install an `s6-log` service. s6-rc does not
print a readiness warning for a longrun without `notification-fd`; there is no
readiness protocol. Use `s6-log` only if you adapt the run script accordingly.

## SysVinit

SysVinit uses an init script plus runlevel links:

```sh
sudo service i2pd start
sudo service i2pd stop
sudo service i2pd restart
sudo service i2pd status
sudo service i2pd reload
sudo update-rc.d i2pd defaults
sudo update-rc.d i2pd disable
```

On RPM-based SysV systems the enable/disable commands are
`sudo chkconfig i2pd on` and `sudo chkconfig i2pd off`. `service i2pd reload`
sends `SIGHUP` and only reloads tunnels; `service i2pd force-reload` performs a
stop/start because `i2pd.conf` is read only at startup. `status` uses the
identity-checked pidfile and returns 0 (running), 1 (dead with a stale pidfile)
or 3 (not running); it can still report a stale process after an unclean
termination if the PID was recycled. The native script validates the executable
behind the PID where `/proc` is available, verifies a stop actually completed,
and serialises `DAEMON_OPTS` splitting without pathname expansion. Its `stop`
sends `SIGINT` and allows the documented graceful drain.

SysVinit does not supervise a foreground process. The native SysV script starts
it with `start-stop-daemon --background` (or a narrow `su` fallback) as the
`i2pd` account and never passes `--daemon`; the OpenBSD rc.d recipe runs
`--foreground` with `rc_bg=YES`. In both cases the service manager or its helper
owns backgrounding. That makes `start` return before the router has initialised
and prevents the same process supervision as `Type=exec`; neither model proves
router readiness. Redirect stdout/stderr in the script or use syslog; there is
no journald integration. The installed SysV script is the authority for its
exact paths and flags. On a systemd host, do not enable both the init script and
the unit; see [Debian and Ubuntu](#debian-and-ubuntu).

## OpenBSD path

OpenBSD keeps its rc.d recipe and `/var/i2pd` data layout; it is not selected by
`I2PD_SERVICE_MANAGER`. The script runs the daemon in the foreground with
`--foreground --service --pidfile=...`, sets `rc_bg=YES`, and lets
`/etc/rc.d/rc.subr` handle start/stop/status:

```sh
doas rcctl enable i2pd
doas rcctl start i2pd
doas rcctl check i2pd
doas rcctl reload i2pd
doas rcctl stop i2pd
```

`reload` sends `SIGHUP`. The template sets `rc_stop_signal=INT` and
`daemon_timeout=660`, so `rcctl stop` requests the ordered stop and the
transit-tunnel drain and waits up to 660 s; with no transit tunnels it exits
promptly. `rcctl start` returns once rc.subr started the foreground process:
`rc_bg=YES` has no readiness pipe, so a later startup failure is visible only
in the log/pidfile, not in the start exit status. The foreground definition
avoids the legacy self-daemonizing readiness ambiguity. See [OpenBSD configuration](../contrib/openbsd/README.md) and the
[OpenBSD validation checklist](security/openbsd-validation.md) for the
pledge/unveil and runtime evidence that is still pending.

## Debian and Ubuntu

The Debian packaging builds with debhelper, CMake and Ninja. debhelper installs
the binary, configuration and service files, registers the `/etc` files as
conffiles and runs the maintainer scripts. The packaging sets
`I2PD_SERVICE_MANAGER=none` and installs the systemd unit and SysV compatibility
script explicitly, avoiding a duplicate service file. Installation registers
and starts the service once through `dh_installinit`/`invoke-rc.d`;
`dh_installsystemd --no-start` only prevents a second start/stop action. A
package upgrade may restart the router through the standard debhelper/RPM
policy, so the 660-second stop window applies to upgrades as well as to an
explicit `systemctl restart`. The `i2pd` package
contains the router, configuration, service definitions, man pages and native
tools. The optional `i2pd-tui` package contains the interactive ncurses client, its
desktop launcher/icons and **no service**. Install both only when an interactive
local client is wanted:

```sh
sudo apt update
sudo apt install i2pd i2pd-tui
```

Inspect installed files and package-owned conffiles with:

```sh
dpkg -L i2pd
dpkg -L i2pd-tui
dpkg-query -W -f='${Conffiles}\n' i2pd
dpkg -s i2pd
apt policy i2pd
```

The TUI socket is mode 0600 and peer-checked against the daemon account. Run
`sudo -u i2pd i2pd-tui --service` (or use a socket with matching ownership)
rather than starting the router from the desktop launcher.

The package installs the AppArmor profile through `dh_apparmor` (loaded and
reloaded by the maintainer scripts), the `i2pd`/`i2pdctl`/`i2pd-tui` man pages,
the filtered `docs/` and `assets/` trees, `LICENSE.i2pd-tools` and
`CC-BY-SA-4.0` under `/usr/share/doc/i2pd`, and a sysusers policy for the
service account. `i2pd-tui` carries `Breaks`/`Replaces` for the binary that
older `i2pd` releases shipped, so an upgrade does not fail on the moved file.
On upgrade, a real `/var/lib/i2pd/certificates` directory is moved to
`/var/lib/i2pd/certificates.dpkg-old` before the packaged compatibility
symlink is installed; move locally added trust anchors back or configure
`certsdir` explicitly.

The package installs both a systemd unit and a SysV init script for
compatibility. On a systemd host, use `systemctl`; the `service` command is a
compatibility wrapper that reaches the same unit:

```sh
systemctl status i2pd
sudo systemctl restart i2pd
sudo systemctl reload i2pd
service i2pd status
sudo service i2pd restart
sudo service i2pd reload
```

On a SysV-only host, use `service` and `update-rc.d` as shown in
[SysVinit](#sysvinit). The package ships both definitions, but only the host's
init system should own the service. Do not enable the unit and the init script
independently in a way that starts two routers. Check with
`systemctl is-enabled i2pd` where systemd is present.

Configuration files are Debian conffiles: local edits to
`/etc/i2pd/i2pd.conf`, `/etc/i2pd/tunnels.conf` and `/etc/default/i2pd` are
preserved on upgrade. Tunnel fragments under `/etc/i2pd/tunnels.conf.d` are
local configuration as well. The service account is `i2pd`; data lives in
`/var/lib/i2pd`, runtime files in `/run/i2pd`, and logs in
`/var/log/i2pd/i2pd.log` when file logging is configured. systemd logging goes
to journald.

Removing the package is not the same as purging it. `apt remove` stops the
service and removes program files but does not remove the data directory or
run the purge branch. `apt purge` removes `/etc/i2pd`, `/etc/default/i2pd`,
`/var/lib/i2pd`, `/var/log/i2pd` and `/run/i2pd`. Back up router keys and
tunnel configuration before purging:

```sh
sudo apt remove i2pd          # keeps conffiles and router data
sudo apt purge i2pd i2pd-tui  # removes conffiles and router data
sudo dpkg --purge i2pd        # equivalent for a locally installed .deb
```

The `i2pd-tui` package is a client. It does not create a service, timer or
socket of its own. When the TUI endpoint is enabled, the router creates the
mode-0600 socket; run the client from an account that can access it, or
configure a socket path and ownership that allows the intended interactive
user. Management commands in this page never use a TUI service.

Package versions, maintainer metadata and changelog entries are not repeated
here; consult `apt policy`, `dpkg -s` and the package changelog.

## CMake install selection

The `Service` install component selects exactly one native Linux definition.
It does not change the build; it only chooses what `cmake --install` writes.
Service selection is rejected on Windows and Android, which use their native
service host or JNI wrapper instead.

| Variable | Purpose |
|---|---|
| `I2PD_SERVICE_MANAGER` | `none`, `systemd`, `openrc`, `runit`, `s6`, `s6-rc`, `shepherd` or `sysvinit`. `none` installs no service definition. |
| `I2PD_SERVICE_DIR` | Destination directory for the unit, init script or service directory. Manager-specific conventional locations are used when it is not set. |
| `I2PD_SERVICE_CONFDIR` | OpenRC `/etc/conf.d` installation directory (default `${CMAKE_INSTALL_SYSCONFDIR}/conf.d`). It is not the router configuration directory. |

Default destinations follow `GNUInstallDirs`. `CMAKE_INSTALL_SYSCONFDIR` is
prefix-relative (normally `etc`); `CMAKE_INSTALL_FULL_SYSCONFDIR` resolves to
`/etc` only for the `/usr` prefix. System packages that expect `/etc` must pass
`-DCMAKE_INSTALL_SYSCONFDIR=/etc` explicitly:

| Manager | Default destination |
|---|---|
| `systemd` | `${CMAKE_INSTALL_PREFIX}/lib/systemd/system/i2pd.service` plus `lib/sysusers.d/openi2pd.conf` and `lib/tmpfiles.d/openi2pd.conf` (architecture-independent) |
| `openrc` | `${CMAKE_INSTALL_SYSCONFDIR}/init.d/i2pd` plus `${CMAKE_INSTALL_SYSCONFDIR}/conf.d/i2pd` |
| `runit` | `${CMAKE_INSTALL_SYSCONFDIR}/sv/i2pd/` (`run`, `finish`, `log/run` and `log/config`) |
| `s6` | `${CMAKE_INSTALL_SYSCONFDIR}/s6/service/i2pd/` (`run`, `finish`, `down-signal`, `timeout-kill`, `timeout-finish`) |
| `s6-rc` | the s6-rc source tree under `${CMAKE_INSTALL_SYSCONFDIR}/s6-rc/source/i2pd/` plus `run`, `finish`, `type`, `down-signal`, `timeout-kill`, `timeout-finish` and `dependencies.d/base` |
| `shepherd` | `${CMAKE_INSTALL_SYSCONFDIR}/shepherd/i2pd.scm` |
| `sysvinit` | `${CMAKE_INSTALL_SYSCONFDIR}/init.d/i2pd` |

`CMAKE_INSTALL_LIBDIR` may include a multiarch tuple on Debian-derived
systems; use `I2PD_SERVICE_DIR` explicitly when the package layout requires a
fixed directory. Confirm the actual files from the install manifest rather than
assuming these defaults.

Example for a systemd package staging tree:

```sh
cmake -S build -B build-output -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=/usr \
  -DCMAKE_INSTALL_SYSCONFDIR=/etc \
  -DI2PD_SERVICE_MANAGER=systemd \
  -DI2PD_SERVICE_DIR=/usr/lib/systemd/system
cmake --build build-output --parallel
DESTDIR="$PWD/pkg" cmake --install build-output --component Service
DESTDIR="$PWD/pkg" cmake --install build-output
```

Install the router and the `Service` component in the same prefix. Selecting
only `Service` in a staging root does not add the `i2pd` binary. Use
`cmake --install build-output --component Service --verbose` or the package
manifest to confirm the exact installed path; do not assume one distribution's
directory for another manager.

Packaging must select **one** manager per package and must not install several
competing definitions for the same service name. Custom prefixes need
matching absolute paths in the unit/script (`ExecStart`, data, runtime,
pidfile, configuration and log paths); a build prefix alone does not rewrite
a packaged manager's absolute paths. `I2PD_SERVICE_CONFDIR` only relocates
OpenRC's `/etc/conf.d` file; it does not relocate `/etc/i2pd` or the daemon's
configuration. The CMake build and the POSIX make front-end do not require GNU
make, and service selection does not add a runtime build dependency.

## Validation status

This documentation change is static-only. No daemon, service manager or
package was run locally. The following checks remain **pending manual CI**:

- `systemd-analyze verify` and `systemctl start/stop/status/reload/enable`
  for `i2pd.service`;
- start/stop/status/reload checks for OpenRC, runit, s6/s6-rc, GNU Shepherd,
  dinit, Upstart and the SysV init script, including the five-second `finish`
  backoff and the `timeout-finish` limit;
- `rcctl stop`/`rcctl start` on OpenBSD with `rc_stop_signal=INT` and the
  660-second `daemon_timeout`, plus the port's `@sample`/notice layout;
- Debian `lintian`, `autopkgtest`, package install/upgrade/remove/purge and
  `dpkg -L`/conffile checks;
- verification that no TUI service is installed and that the client remains
  an interactive local-IPC consumer;
- runtime confirmation that foreground execution, local sockets, persistence
  and logging work under each manager's sandbox.

No readiness guarantee (`sd_notify` or equivalent) and no full privilege
separation or full sandboxing is claimed by any service definition. The log
worker and opt-in storage workers are separate processes; networking, crypto
and the core are pending.

## See also

- [build system](build-system.md)
- [TUI WINDOWS](TUI-WINDOWS.md)
- [openbsd validation](security/openbsd-validation.md)

<!--
See the LICENSE file at the top of the project tree for copyright
and license details.

openi2pd: run.

Documents project documentation.

-->

Running openi2pd
============

Starting, stopping and reloading configuration
----------------------------------------------

This chapter explains how to start and manage the openi2pd router under Unix-like operating systems.

After you have built openi2pd from source, run the binary:

    ./i2pd

To display all available options:

    ./i2pd --help

openi2pd can be controlled with signals. A user process writes its PID to
`~/.i2pd/i2pd.pid`; packaged Linux services use `/run/i2pd/i2pd.pid`, not the
historical `/var/run` path. The `pidfile` option overrides the location. Send
signals with `kill`:

    kill -HUP $( cat /run/i2pd/i2pd.pid )

openi2pd supports the following signals:

* INT - Graceful shutdown. The router stops accepting transit tunnels, waits up to 10 minutes and then stops. Send a second INT signal to shut it down immediately.
* TERM - Immediate ordered shutdown. This is not the 10-minute graceful path.
* HUP - Reload tunnel configuration files. A sandboxed process cannot add a new filesystem path through HUP; restart after changing a configured path.
* USR1 - Reopen the log file, for example after log rotation.
* TSTP/CONT - Disconnect from/restore the network when `unix.handle_sigtstp` is enabled.

For a managed service, use the service manager instead of signalling the
process directly; see [Service management](../service-management.md).

### Local TUI

`i2pd-tui` is an optional ncurses client. It does not control the daemon and
the daemon does not depend on a terminal:

    i2pd-tui --datadir ~/.i2pd
    i2pd-tui --socket /var/lib/i2pd/i2pd-tui.sock

Keys: `1`-`9` and `0` switch views (Status, Peers, Tunnels, Torrents, NetDb,
Logs, Help, Destinations, Config, Connections), `Tab`/arrows navigate, `t`
toggles transit acceptance after confirmation, `l` cycles the local log
threshold, `L` cycles the daemon log level, `n` switches the NetDb directory
and `/` filters logs or torrents. `r` refreshes and `q` quits. The socket is
mode 0600. Use the normal signals or the service manager to reload or stop the
router.

On Windows run `i2pd-tui.exe` from an interactive terminal (Windows Terminal or
a VT-enabled console) and pass the same data directory/configuration as the
router:

```powershell
i2pd.exe --datadir C:\i2pd
i2pd-tui.exe --datadir C:\i2pd
```

The two processes communicate over a local named pipe, not a TCP port. A
service running as `LocalSystem` and a desktop TUI running as another user need
an explicit service-account/DACL decision; the TUI is not a service. See
[TUI on Windows](../TUI-WINDOWS.md).

The former HTTP console at port 7070 no longer exists.


### Service managers

The CMake `Service` component and the Debian packages install native
definitions for systemd, GNU Shepherd, OpenRC, runit, s6/s6-rc and SysVinit.
The native supervisor definitions run the router in the foreground and pass
`--foreground` to override a local `daemon = true` configuration; do not add
`--daemon` to them. The SysV compatibility script is the documented
backgrounding exception. On systemd:

    sudo systemctl start i2pd.service
    sudo systemctl stop i2pd.service
    sudo systemctl restart i2pd.service
    sudo systemctl status i2pd.service
    sudo systemctl reload i2pd.service
    sudo systemctl enable --now i2pd.service
    sudo systemctl disable --now i2pd.service

`reload` sends `SIGHUP` to reload tunnel configuration. The packaged unit uses
`KillSignal=SIGINT` with a 600-second stop timeout, so `systemctl stop` requests
the graceful transit-tunnel drain and may wait up to 10 minutes. `SIGTERM` is a
prompt ordered shutdown and is **not** the 10-minute path. Inspect the installed
unit with `systemctl show -p KillSignal -p TimeoutStopUSec i2pd.service` and
adjust a drop-in if a different stop policy is wanted.

The packaged unit is `Type=exec` and does not use `sd_notify`; a successful
start means the process was executed, not that the router is ready for
traffic. Logs go to journald by default:

    journalctl -u i2pd.service -f

There is no TUI service. `i2pd-tui` is a separate interactive client that
connects to the running router over its local socket. See
[Service management](../service-management.md#systemd) for drop-ins, hardening,
local-socket compatibility and the equivalent commands for the other managers.
Manager start/stop and package checks are pending manual CI; this documentation
is static-only.


Recommended way to run openi2pd built from source
--------------------------------------------------

The following commands show the recommended way to run openi2pd built from source, without a package manager. This keeps all
openi2pd-related files under `$HOME/dist`. The executable and configuration names remain `i2pd` for compatibility.

    mkdir $HOME/dist
    cp i2pd $HOME/dist
    cp -R contrib/certificates $HOME/dist
    cp contrib/i2pd.conf $HOME/dist
    cd $HOME/dist
    ulimit -n 4096  # only on Linux, increasing open file limit

Then, to run openi2pd, change to the installation directory and run:

    ./i2pd --datadir .

Overriding service parameters
-----

For systemd, use `systemctl edit i2pd.service` to create a drop-in under
`/etc/systemd/system/i2pd.service.d/`:

```
[Service]
LimitNOFILE=16386
LimitCORE=infinity
```

Apply it with `sudo systemctl daemon-reload` and restart the service. The
daemon's `datadir`, `pidfile`, `log` and `logfile` options can also be changed,
but the service definition must use the same paths and grant the service
account access to them. [Service management](../service-management.md)
describes the equivalent native configuration for OpenRC, runit, s6/s6-rc,
GNU Shepherd and SysVinit.

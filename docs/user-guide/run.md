Running openi2pd
============

Starting, stopping and reloading configuration
----------------------------------------------

This chapter explains how to start and manage the openi2pd router under Unix-like operating systems.

After you have built openi2pd from source, run the binary:

    ./i2pd

To display all available options:

    ./i2pd --help

openi2pd can be controlled with signals. The process ID by is written to the file `~/.i2pd/i2pd.pid` or
`/var/run/i2pd/i2pd.pid` by default. You can use `kill` utility to send signals like this:

    kill -INT $( cat /var/run/i2pd/i2pd.pid )

openi2pd supports the following signals:

* INT - Graceful shutdown. The router waits for up to 10 minutes and then stops. Send a second INT signal to shut it down immediately.
* HUP - Reload tunnels configuration files.
* USR1 - Reopen the log file.
* TSTP/CONT - Disconnect from/restore the network when `unix.handle_sigtstp` is enabled.

### Local TUI

`i2pd-tui` is an optional ncurses client. It does not control the daemon and
the daemon does not depend on a terminal:

    i2pd-tui --datadir ~/.i2pd
    i2pd-tui --socket /var/i2pd/i2pd-tui.sock

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


### systemd unit

Some distribution packages include a systemd control unit. Where systemd is used, the service can be managed with it.

To start or stop the service:

    sudo systemctl start i2pd.service
    sudo systemctl stop i2pd.service --no-block

The stop command initiates a graceful shutdown; the router stops after draining transit tunnels (maximum 10 minutes).

To enable/disable autostart of i2pd upon bootup:

    sudo systemctl enable i2pd.service
    sudo systemctl disable i2pd.service


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

Overriding systemd service parameters
-----

For overriding systemd service defaults create `/etc/systemd/system/i2pd.service.d/override.conf` file and place overriden options. Don't forget use option section.

```
mkdir -p /etc/systemd/system/i2pd.service.d/
touch /etc/systemd/system/i2pd.service.d/override.conf
```

Example content with enabled coredump and increased `nofile` limit

```
[Service]
LimitNOFILE=16386
LimitCORE=infinity
```

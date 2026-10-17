# OpenBSD configuration

Use `i2pd.conf` as the packaged configuration in `/etc/i2pd`, with
`--service --conf=/etc/i2pd/i2pd.conf`. Install tunnel configuration and
certificates below `/etc/i2pd`; service data defaults to `/var/i2pd`.
The example logs to syslog. The former HTTP console no longer exists; use the
read-only `i2pd-tui` client over the local UNIX socket when an interactive view
is needed.

The local TUI socket defaults to `/var/i2pd/i2pd-tui.sock` (mode 0600). A
custom `tui.socket` path is unveiled with `rwc` permissions; no additional
pledge promise is required.

The sandbox uses the effective configuration paths after daemonization.
Additional unveil paths can be listed, one per line, in
`openbsd.unveil_file`; they receive read, write and create permissions.
`openbsd.pledge_file` overrides the runtime pledge promises.

The former port patch for the UDP cleanup timer is unnecessary on current
upstream: that timer has been removed from ClientContext.

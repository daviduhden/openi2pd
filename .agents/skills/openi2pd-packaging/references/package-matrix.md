<!--
See the LICENSE file at the top of the project tree for copyright
and license details.

openi2pd package and service matrix.
-->
# Package and service matrix

Use with `SKILL.md`. This is a static inventory; package/service execution is
manual CI or disposable-host work.

## Package surfaces

| Surface | Files | Notes |
|---|---|---|
| Debian source package | `debian/` | debhelper 13; `i2pd` + `i2pd-tui` |
| Static Debian bundle | `contrib/static/debian/` | separate manifests; do not merge blindly |
| RPM | `contrib/rpm/i2pd.spec`, `i2pd-git.spec` | systemd integration |
| Static build | `contrib/static/package.sh`, `contrib/static/build-musl.sh` | no runtime downloads |
| Docker | `contrib/docker/` | non-root/runtime policy; no HTTP console |
| OpenBSD | `contrib/openbsd/`, `contrib/openbsd/port/` | rc.d, login class, config, port/PLIST merge template |
| Man pages | `man/i2pd.1`, `i2pdctl.1`, `i2pd-tui.1` | installed by manifests |
| Desktop/metadata | `contrib/openi2pd.desktop`, `Win32/Resource.*` | identity only |

## Service managers

| Manager | Files | Install selection |
|---|---|---|
| systemd | `contrib/i2pd.service`, `contrib/sysusers.d/i2pd.conf`, `contrib/tmpfiles.d/i2pd.conf`, `debian/i2pd.tmpfiles` | `systemd`; Debian installs its own tmpfiles |
| OpenRC | `contrib/openrc/i2pd.openrc`, `contrib/openrc/conf.d/i2pd` | `openrc` |
| runit | `contrib/runit/i2pd/` | `runit` |
| s6 | `contrib/s6/i2pd/` | `s6` |
| s6-rc | `contrib/s6-rc/source/i2pd/` | `s6-rc` |
| GNU Shepherd | `contrib/shepherd/i2pd.scm` | `shepherd` |
| SysVinit | `contrib/sysvinit/i2pd` | `sysvinit` |
| Upstart/dinit | `contrib/upstart/`, `contrib/dinit/` | documentation/legacy; no competing install |
| OpenBSD | `contrib/openbsd/i2pd.rc` | native package |

Exactly one manager per package. `none` installs no service. The TUI is never
a service. No definition may start a second router instance.

## Paths and identity

Preserve:

- binary: `i2pd`, `i2pd-tui`, `i2pd-tools` names;
- config: `/etc/i2pd/i2pd.conf`, `/etc/i2pd/tunnels.conf`;
- data: `/var/lib/i2pd` on Linux, `/var/i2pd` on OpenBSD;
- runtime/pid/log paths documented by the service manager;
- unprivileged `i2pd` user/group where packaging creates one.

A path change requires migration, compatibility symlinks/dual-read and tests.

## Validation

- Debian: `dpkg-buildpackage`, `lintian`, `autopkgtest`, `dpkg -L`, conffile
  and upgrade/remove/purge checks.
- systemd: `systemd-analyze verify`; start/stop/restart/status/reload.
- OpenRC/runit/s6/s6-rc/Shepherd/SysV: start/stop/restart/status/reload on a
  disposable host; signal/shutdown ordering.
- OpenBSD: rc.d lifecycle, pidfile, sandbox paths, TUI/log worker, and the
  external `port/` merge template (`PLIST`, `@rcscript`, `@sample`).
- No TUI unit/init script; no duplicate daemon; no service auto-start on
  install; helpers may enable it without creating a second action.
- Keep license/copyright/NEWS/changelog provenance.

All dynamic checks are pending unless the packaging/manual CI run actually
passes.

---
name: openi2pd-packaging
description: Use when changing openi2pd Debian or RPM packaging, static bundles, service-manager definitions, install layout, man pages, tmpfiles or desktop integration.
---
<!--
See the LICENSE file at the top of the project tree for copyright
and license details.
-->

# openi2pd packaging and services

## Activate when

Use this skill for `debian/`, `contrib/static/`, `contrib/rpm/`,
`contrib/docker/`, init/service definitions (`systemd`, `OpenRC`, `runit`,
`s6`, `s6-rc`, GNU Shepherd, SysVinit, Upstart, dinit), AppArmor, logrotate,
desktop metadata, man-page installation or package smoke tests.

Read [`../../../AGENTS.md`](../../../AGENTS.md) first. Preserve binary names,
configuration/data paths and persisted formats unless a separate approved
migration exists.

## Inspect first

- `debian/` and the separate `contrib/static/debian/` source; do not assume
  they share manifests.
- `debian/rules`, `debian/control`, install/man/tmpfiles/lintian/autopkgtest
  files, `debian/NEWS`, `debian/changelog`.
- `contrib/i2pd.service` and
  `contrib/{openrc,runit,s6,s6-rc,shepherd,sysvinit,upstart,dinit}/`,
  `contrib/openbsd/`, `contrib/apparmor/`, `contrib/i2pd.logrotate`,
  `contrib/openi2pd.desktop`, `man/`.
- `build/CMakeLists.txt` install components and `I2PD_SERVICE_MANAGER`.
- [`../../../docs/service-management.md`](../../../docs/service-management.md),
  [`../../../docs/user-guide/install.md`](../../../docs/user-guide/install.md) and
  [`../../../docs/build-system.md`](../../../docs/build-system.md).

## Reference sources

- [`../../../docs/service-management.md`](../../../docs/service-management.md) —
  runtime model, identities/paths, hardening and each init backend.
- [`../../../docs/build-system.md`](../../../docs/build-system.md) — Service
  component and install selection.
- [`../../../debian/tests/smoke`](../../../debian/tests/smoke) and
  [`../../../debian/tests/control`](../../../debian/tests/control) — Debian
  package smoke expectations.
- [`references/package-matrix.md`](references/package-matrix.md) — package,
  service and validation map.

## Reconnaissance

Launch read-only `explore` subagents for:

1. package manifests and installed file ownership;
2. each init script's start/stop/reload/user/path semantics;
3. CMake `Service` component and install destinations;
4. Debian/RPM metadata, dependencies, conffiles and tmpfiles;
5. packaging CI, lintian/autopkgtest and service validation steps.

Ask for exact paths and package names. Do not let them run `dpkg-buildpackage`,
`lintian`, service managers, Docker or package scripts.

## Parallel implementation

- Split Debian, RPM/static, and init definitions by independent files. Do not
  edit `build/CMakeLists.txt` service selection or `debian/rules` concurrently
  with another package workstream.
- One owner per service name; do not create competing definitions.
- Keep TUI packaging separate: the interactive client is not a service.
- Packaging owns install manifests/units and man-page *installation*.
  Documentation owns man-page content and `docs/` wording. Do not edit the
  same man page and its install manifest concurrently without coordination.
- Coordinate binary/path changes with `openi2pd-build` and documentation with
  `openi2pd-documentation`.

## Conventions

- Install exactly one service manager per package. `I2PD_SERVICE_MANAGER=none`
  installs no service; do not add competing units/scripts for the same name.
- Preserve the unprivileged service identity, `/etc/i2pd`, `/var/i2pd`,
  runtime/pid/log paths and config names unless a migration is approved.
- systemd uses `Type=exec` foreground semantics, no `sd_notify` readiness claim
  unless implemented; use native hardening and tmpfiles as documented.
- OpenRC/runit/s6/Shepherd/SysV definitions must honor foreground mode, user,
  paths and reload/stop semantics consistently.
- Debian packages split `i2pd` and `i2pd-tui`; use debhelper compat 13, explicit
  manifests, `--no-start`, and preserve non-systemd SysV support.
- Keep autopkgtest bounded and non-destructive; it must not rely on a public
  I2P network or leave a running daemon.
- Keep license/copyright/provenance files intact. Do not relicense third-party
  files.
- Do not add a TUI service, duplicate router instance, automatic start on
  install or a hidden download.

## Do not

- Do not compile, build a package, run `lintian`, `autopkgtest`, `systemctl`,
  `rcctl`, `runit`, `s6`, Docker or project validators locally.
- Do not change binary/config/data paths without a migration and
  compatibility test.
- Do not weaken service hardening, sandbox paths, user separation or file
  permissions.
- Do not claim package/service validation from a manifest inspection.
- Do not commit, push, tag or release.

## Prepare for CI

- Keep `services-static` and `tests/check-services.cmake` coherent.
- Prepare/retain the packaging workflow: Debian binary packages, lintian,
  autopkgtest, RPM/static checks and each service-manager smoke where runnable.
- Check `systemd-analyze verify`, start/stop/restart/status/reload, pidfile,
  tmpfiles, user/group and no-duplicate-daemon behavior.
- Verify package contents with `dpkg -L`/manifest inspection and that
  `i2pd-tui` installs no service or `/etc` policy.
- Record package/service results as pending until the manual job passes.

## Tracking

Use `BUILD`/`DEPS` or packaging-specific IDs and the native-service/packaging
increment rows. Record package names, service manager, paths, tests and the
exact CI/host lane. Append a dated `PROGRESS.md` entry; subagents report
proposed rows to the coordinator.

## References

- [Package and service matrix](references/package-matrix.md)
- [Service management](../../../docs/service-management.md)
- [Build system](../../../docs/build-system.md)
- [Global policy](../../../AGENTS.md)

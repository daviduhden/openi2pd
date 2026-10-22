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
- `contrib/i2pd.service`, `contrib/sysusers.d/`, `contrib/tmpfiles.d/` and
  `contrib/{openrc,runit,s6,s6-rc,shepherd,sysvinit,upstart,dinit}/`,
  `contrib/openbsd/` (including `port/`), `contrib/apparmor/`,
  `contrib/i2pd.logrotate`, `contrib/openi2pd.desktop`, `man/`.
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

Launch at least one read-only `explore` subagent before editing, split across
independent areas when useful. If subagents are unavailable, report that
limitation; do not substitute local execution. Ask for:

1. package manifests and installed file ownership;
2. each init script's start/stop/reload/user/path semantics;
3. CMake `Service` component and install destinations;
4. Debian/RPM metadata, dependencies, conffiles and tmpfiles;
5. packaging CI, lintian/autopkgtest and service validation steps.

Ask for exact paths and package names. Do not let them run `dpkg-buildpackage`,
`lintian`, service managers, Docker or package scripts.

## Parallel implementation

- Split Debian, RPM/static, and init definitions by independent files.
  `build/CMakeLists.txt` service selection is coordinator-owned; obtain review
  before integration. Do not edit `debian/rules` concurrently with another
  package workstream (AGENTS.md `Coordinator / ownership`).
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

## Known issues and decisions

- Torrent-enabled Docker runtimes require `boost-json` and `boost-url` shared
  libraries as well as `boost-program_options`; JSON/URL are compiled
  dependencies, not automatically header-only implementations.
- Debian `i2pd`/`i2pd-tui` ownership, TUI desktop assets, systemd
  sysusers/tmpfiles and OpenBSD `/var/i2pd` vs Linux `/var/lib/i2pd` paths are
  deliberate. Preserve administrator modes/ownership on upgrade.
- `contrib/openbsd/port/` is a merge template only; RPM/static/OpenBSD package
  builds, `lintian`, `autopkgtest`, `pkg_add` and service starts are unrun.
- Upstream HTML/web assets and browser consoles are not package dependencies;
  documentation remains Markdown/CC-BY-SA English; shipped UI locales are
  en/es/ru/zh-Hans, while package names, descriptions, man pages and paths stay
  English.
- Package validation claims require the manual packaging workflow; static file
  inspection is not package validation.

## Do not

- Do not compile, build a package, run `lintian`, `autopkgtest`, `systemctl`,
  `rcctl`, `runit`, `s6`, Docker or project validators locally.
- Do not change binary/config/data paths without a migration and
  compatibility test.
- Do not weaken service hardening, sandbox paths, user separation or file
  permissions.
- Do not claim package/service validation from a manifest inspection.
- Keep every workflow `workflow_dispatch`-only; do not add `push`, `pull_request`, `schedule` or automatic `workflow_call` triggers.
- Do not create commits, tags, releases, worktrees or temporary commits, and do not push; inspect `git status`/`git diff` before editing.
- Do not install dependencies, fetch/vendor code or perform hidden downloads to enable a local build or test. Preserve LICENSE/NOTICE/provenance and technical identifiers/paths unless an approved migration exists (AGENTS.md).

## Prepare for CI

- Keep `services-static`, `packaging-static`, `tests/check-services.cmake` and
  `tests/check-packaging.cmake` coherent.
- Prepare/retain the packaging workflow: Debian binary packages, lintian,
  autopkgtest, RPM/static checks and each service-manager smoke where runnable.
- Check `systemd-analyze verify`, start/stop/restart/status/reload, pidfile,
  tmpfiles, user/group and no-duplicate-daemon behavior.
- Verify package contents with `dpkg -L`/manifest inspection and that
  `i2pd-tui` installs no service or `/etc` policy.
- Record the exact AGENTS.md tracking state and state which validations were
  not run; package/service results remain pending until the manual job passes.

## Tracking

Use `BUILD`/`DEPS` or packaging-specific IDs and the native-service/packaging
increment rows. Record package names, service manager, paths, tests and the
exact CI/host lane. Propose a dated `PROGRESS.md` entry and any `TODO.md`/`ROADMAP.md` changes to the coordinator; append them only when explicitly assigned; subagents report
proposed rows to the coordinator.

Record the exact AGENTS.md `Tracking states` value and state which validations were not run; do not collapse them into “pending”.

## References

- [Package and service matrix](references/package-matrix.md)
- [Service management](../../../docs/service-management.md)
- [Build system](../../../docs/build-system.md)
- [Global policy](../../../AGENTS.md)

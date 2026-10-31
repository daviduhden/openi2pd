---
name: openi2pd-packaging
description: Use when changing openi2pd Debian or RPM packaging, static bundles, service-manager definitions, install layout, man pages, tmpfiles or desktop integration.
---

## Maintenance & Continuous Learning

When this skill guides a repository change, review it after implementation. Update procedures, tools, configurations, assumptions, examples and validation steps when they changed or when reusable knowledge improves future work; correct inaccurate guidance and omit one-off execution details. Report synchronization in the task summary. Follow the normative process and Definition of Done in [AGENTS.md](../../../AGENTS.md#continuous-agent--skills-maintenance).
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

- Apply the [source documentation policy](../../../AGENTS.md#source-documentation-policy)
  to service signals, permissions, script inputs and cleanup contracts.

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
- Do not add a TUI service, a duplicate router instance, a second automatic
  start action on install or a hidden download. Debian starts the service once
  through `dh_installinit`; `dh_installsystemd --no-start` only removes the
  duplicate action.

## Known issues and decisions

- Debian `dh_compress` can compress Markdown documentation under
  `/usr/share/doc` (including nested branding files) to `.md.gz`. Keep source
  install manifests uncompressed, but make installed-package smoke checks and
  file-list assertions match the generated package path confirmed from its
  exact-run `.deb` artifact.
- Package lifecycle workflow steps should identify each remove/purge
  preservation assertion in the log. When an exact run exits after a silent
  `test`, inspect that run's generated maintainer scripts and add labeled
  diagnostics before changing package hooks.

- Torrent-enabled Docker runtimes require `boost-json` and `boost-url` shared
  libraries as well as `boost-program_options`; JSON/URL are compiled
  dependencies, not automatically header-only implementations.
- Debian `i2pd`/`i2pd-tui` ownership, TUI desktop assets, systemd
  sysusers/tmpfiles and OpenBSD `/var/i2pd` vs Linux `/var/lib/i2pd` paths are
  deliberate. Preserve administrator modes/ownership on upgrade.
- `contrib/openbsd/port/` is a merge template only; RPM/static/OpenBSD package
  builds, `lintian`, `autopkgtest`, `pkg_add` and service starts are unrun.
- Upstream HTML/web assets and browser consoles are not package dependencies;
  documentation remains Markdown/BSD-3-Clause English; shipped UI locales are
  en/es/ru/zh-Hans, while package names, descriptions, man pages and paths stay
  English.
- Package validation claims require the manual packaging workflow; static file
  inspection is not package validation.
- Debian's `dh_compress` packages the upstream `ChangeLog` as `ChangeLog.gz`
  and can compress `README.md` to `README.md.gz`; package smoke assertions must
  check the installed archive names while CMake/source manifests keep the
  original source names. The ordinary `debian/` and `contrib/static/debian/`
  postrm files are separate: if `dh_installinit` owns init cleanup, do not add
  a second manual `update-rc.d` purge call beside `#DEBHELPER#`.

- The Debian workflow's dependency install must include every package in
  `debian/control` Build-Depends, including `dh-apparmor`; otherwise
  `dpkg-buildpackage` stops at `dpkg-checkbuilddeps` before starting a build.

- systemd: `Type=exec`, `KillMode=mixed`, `KillSignal=SIGINT`,
  `TimeoutStopSec=660`, `StateDirectory`/`RuntimeDirectory`/`LogsDirectory`
  (all 0700) and `SyslogIdentifier=open-i2pd` (frozen filter tag). The main
  process must receive the stop signal; a cgroup-wide stop bypasses the worker
  drain. tmpfiles mirrors the same three directories.
- s6/s6-rc: `down-signal` `INT`, `timeout-kill` `660000`, `timeout-finish`
  `10000`, a five-second `finish` backoff, `s6-softlimit -o 8192`, no
  `notification-fd`, and identical daemon argv between the two trees.
  `tests/check-services.cmake` compares the argv and the exact-value files.
- runit: stock `runsv` has no per-service stop timeout or `down-signal` file.
  Keep `SIGTERM` (prompt ordered stop), never ship `down-signal`, and provide
  `finish` plus `log/config` (1 MiB, ten files). `chpst -o 8192` sets the
  descriptor budget.
- OpenRC: `supervise-daemon`, `respawn_period=300`, `rc_ulimit="-n 8192"`,
  checked `checkpath` calls and `retry=SIGINT/660` when `stopsig=SIGINT`.
  `supervise-daemon --signal HUP|USR1` keeps the option before the service
  name (musl does not permute arguments).
- SysV: `force-reload` is a real stop/start; `reload` is `SIGHUP` only.
  Validate the PID against `/proc/<pid>/exe` (after stripping ` (deleted)`)
  before signalling, verify that a stop actually completed after `--oknodo`,
  disable pathname expansion around `DAEMON_OPTS`, and keep minimal LSB
  `log_*` fallbacks for hosts without lsb-base.
- OpenBSD rc.d: `rc_stop_signal=INT` plus `daemon_timeout=660` makes
  `rcctl stop` reach the drain; `rc_bg=YES` has no readiness pipe, so
  `rcctl start` does not prove readiness. `daemon_class` is read-only and
  derived from the script name; set `daemon_execdir` instead.
- OpenBSD port: `@sample <absolute path>` resolves to
  `share/examples/<absolute path>`, so install `share/examples/etc/...`,
  `share/examples/var/i2pd` and `share/doc/i2pd/{LICENSE,NOTICE,
  LICENSE.i2pd-tools}`; do not ship `/var/i2pd/certificates` samples and keep
  router configuration root-owned (not writable by `_i2pd`).
- GNU Shepherd: `make-kill-destructor` signals the process group, so workers
  receive `SIGINT` too; `#:grace-period 660`, syslog output and no `#:log-file`
  (Shepherd would open it as root before `#:user`).
- A crash must stay a crash: `SIGABRT` has no handler, and an unhandled
  exception exits `1`. Do not restore the old `SIGABRT`-as-`SIGTERM` path.

- CMake owns the distribution payload: `Documentation` installs
  `ChangeLog`/`LICENSE`/`NOTICE`/`README.md`, `LICENSE.i2pd-tools`,
  the filtered `docs/` tree and `assets/branding`; man pages are
  owned by `Runtime` (`i2pd.1`), `Tui` (`i2pd-tui.1`) and `Tools`
  (`i2pdctl.1`), not by the optional docs block. `I2PD_INSTALL_CONFIG=OFF`
  combined with a service manager is a configure error.
- Debian `*.install` entries are resolved in the current directory first and
  only then in `debian/tmp`; never list `docs` or `assets/branding` (the source
  tree wins and bypasses CMake filtering). List the installed
  `usr/share/doc/i2pd/...` and `usr/share/man/...` paths. `dh_missing
  --fail-missing` is the guard.
- The source manifest installs `ChangeLog` from CMake's Documentation
  component, while debhelper compresses it in the binary package to
  `/usr/share/doc/i2pd/ChangeLog.gz`. Keep `debian/i2pd.install` and the
  compile-free manifest contract on the uncompressed source path; Debian smoke
  checks must expect the installed `.gz` path.
- `i2pd-tui` carries `Breaks`/`Replaces: i2pd (<< 2.61.0-3)` for the moved
  binary. Debian loads the profile with `dh_apparmor`, installs
  `debian/i2pd.sysusers`, and uses `debian/i2pd.preinst` to move a real
  `/var/lib/i2pd/certificates` directory to `.dpkg-old` before the symlink is
  unpacked. `postinst` repairs only a root-owned, otherwise empty state/log
  directory created by that link; it never resets administrator modes.
- The AppArmor profile must allow `mixr` for the privsep worker re-exec,
  `flags=(attach_disconnected)`, `/var/log/i2pd/** rw` and
  `/{var/,}run/i2pd/*.sock rw`. `mr` is insufficient.
- OpenBSD: `WRKSRC=${WRKDIST}/build` with a separate `WRKBUILD` (`obj`),
  `-DI2PD_OPENSSL_EXPECT=LibreSSL`, a `share/examples/var/i2pd` subscriptions
  sample and a PLIST `@newuser` with login class `i2pd`. `@sample <absolute
  path>` resolves under `share/examples/<absolute path>`.
- RPM `%doc` copies by basename; install `tools/LICENSE` as
  `LICENSE.i2pd-tools` and `tools/README.md` as `README.i2pd-tools` instead of
  listing both under `%doc`. The release spec must list every installed
  desktop/icon path in `%files`.
- Project documentation declares BSD-3-Clause; no standalone CC notice file is
  installed. The static Debian package keeps only the certificate
  compatibility link, not the removed config/tunnel links.

- Service/unit and maintainer scripts state the operational contract in
  concise comments: signals/stop budget, identity, sandbox paths, cleanup,
  upgrade/purge safety and platform limits. Follow
  [`docs/source-documentation.md`](../../../docs/source-documentation.md) and
  never claim readiness or validation the unit cannot provide.


- `PKG-029` records that `debian/i2pd.tmpfiles` re-applies 0700 on every boot,
  conflicting with the postinst promise that administrator directory modes
  survive an upgrade. Do not describe modes as preserved until this is
  reconciled and tested.

## Do not

- Do not compile, build a package, run `lintian`, `autopkgtest`, `systemctl`,
  `rcctl`, `runit`, `s6`, Docker or dynamic project tests locally.
  Compile-free structural checks required by `AGENTS.md` may run locally.
- Do not change binary/config/data paths without a migration and
  compatibility test.
- Do not weaken service hardening, sandbox paths, user separation or file
  permissions.
- Do not claim package/service validation from a manifest inspection.
- Keep packaging/build/test workflows `workflow_dispatch`-only. The sole
  pull-request exception is the compile-free `agent-maintenance.yml` gate;
  use `openi2pd-ci` for workflow trigger policy and validation.
- Do not create commits, tags, releases, worktrees or temporary commits, and do not push; inspect `git status`/`git diff` before editing.
- Do not install dependencies, fetch/vendor code or perform hidden downloads to enable a local build or test. Preserve LICENSE/NOTICE/provenance and technical identifiers/paths unless an approved migration exists (AGENTS.md).

## Prepare for CI

- Keep `services-static`, `packaging-static`, `tests/check-services.cmake` and
  `tests/check-packaging.cmake` coherent. The packaging workflow's
  `service-install-matrix` job configures each manager and installs only the
  `Service` component, asserting the documented paths, the absence of
  `notification-fd`, and rejection of the dinit/Upstart samples.
- Prepare/retain the packaging workflow: Debian binary packages, lintian,
  autopkgtest (`apparmor_parser -Q`, `desktop-file-validate`, docs/man and
  split assertions), RPM/static checks and each service-manager smoke where
  runnable.
- Check `systemd-analyze verify`, start/stop/restart/status/reload, pidfile,
  tmpfiles, user/group and no-duplicate-daemon behavior.
- Verify package contents with `dpkg -L`/manifest inspection and that
  `i2pd-tui` installs no service or `/etc` policy.
- Record the exact AGENTS.md tracking state and state which validations were
  not run; package/service results remain pending until the manual job passes.

- Alpine no longer publishes a `miniupnpc-static` package. `miniupnpc-dev`
  already contains `/usr/lib/libminiupnpc.a`, which the `WITH_STATIC` finder
  selects; do not re-add a separate static subpackage.
- Inside a quoted CMake argument, `[`/`]` are literal. Escaping them as `\[`
  is an invalid escape and makes the assertion search for different text;
  only `\"`, `\$` and `\\` need escaping for this kind of contract.

- `tools/test-perl-install.cmake` installs `Tools`; when
  `I2PD_INSTALL_DOCS=ON` the shared `LICENSE.i2pd-tools` belongs to
  `Documentation`, so the test must install that component too before checking
  attribution. Never install two components that both own the same destination
  from separate rules.
- CI artifact uploads must stay inside `GITHUB_WORKSPACE`; `upload-artifact`
  v4 skips absolute paths such as `/tmp/static-file`.
- Debian Lintian overrides are scoped by binary package: an
  `i2pd-tui:` tag belongs in `debian/i2pd-tui.lintian-overrides`, not the
  daemon's `i2pd.lintian-overrides`. `debugedit` may report unknown DWARF
  forms during `dh_strip`; classify the command's exit and later package
  diagnostics before treating that message as the failure cause.

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

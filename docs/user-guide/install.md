<!--
See the LICENSE file at the top of the project tree for copyright
and license details.

openi2pd: install.

Documents project documentation.

-->

Installing openi2pd
===================

openi2pd is built and packaged from this repository. The router executable,
configuration file and data paths retain the `i2pd` name for compatibility;
distribution packages named `i2pd` may still point at the upstream PurpleI2P
project and are not automatically an update path for this fork.

## Supported platforms

| Platform | Supported installation path |
|---|---|
| Linux | Build from source; native service definitions and the maintained `debian/` or `contrib/rpm/` recipes |
| OpenBSD | Build from source or use the local `contrib/openbsd/` service recipe |
| Windows | Build with CMake/MSVC or MSYS2/Clang64, or use a fork release artifact |
| Android | Build the integrated app under `android/` with OpenJDK 25 |
| Docker | Build the local image from `contrib/docker/Dockerfile` |

macOS, FreeBSD and Solaris are **not supported** by the CMake entry point,
which rejects them before configuring targets. Use upstream i2pd for those
platforms; do not treat upstream packages as openi2pd binaries.

## Building from source

Read [build requirements](../devs/building/requirements.md) and then use the
guide for the target:

- [Linux and OpenBSD](../devs/building/unix.md)
- [Windows](../devs/building/windows.md)
- [Android](../devs/building/android.md)
- [Cross compilation](../devs/building/cross.md) (experimental, not a validated
  platform)

The build is CMake/Ninja and requires C++23. The POSIX make front-end delegates
to CMake and does not require GNU make. The daemon, TUI, tools, documentation
and `Service` installation components are selected independently; see
[CMake and POSIX Make](../build-system.md) and
[Service management](../service-management.md#cmake-install-selection).

```sh
cmake -S build -B build-output -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-output --parallel
sudo cmake --install build-output --prefix /usr
```

Release artifacts, when published, are on the
[openi2pd releases page](https://github.com/daviduhden/openi2pd/releases).

## Windows

Build with the [Windows guide](../devs/building/windows.md), or use a fork
release artifact. Two architectures are supported: x86 (32-bit) and x86_64.
The Windows XP build path was removed because the C++23 toolchain and current
dependencies do not support it.

The optional TUI is built as `i2pd-tui.exe` next to `i2pd.exe` and uses a local
named pipe; it does not need ncurses or a network port. See
[TUI on Windows](../TUI-WINDOWS.md) for terminal, service-account and DACL
requirements. Runtime validation on Windows is pending.

## Android

Build the integrated Android app from `android/` with OpenJDK 25, Android SDK
platform 37, Build Tools 36.0.0 and NDK r30 LTS+. See
[Android app](../devs/building/android.md) and
[Android modernization](../android-modernization.md). The upstream F-Droid
package has a different application ID and is not an update path for this fork.

## Linux

Build from source using the [Unix guide](../devs/building/unix.md), or use the
maintained Debian/RPM packaging recipes. Native service definitions are
available for systemd, GNU Shepherd, OpenRC, runit, s6/s6-rc and SysVinit, and
are selected at install time with the CMake `Service` component. See
[Service management](../service-management.md) for enabling, start/stop,
status, reload, logging and hardening notes. The TUI is an interactive client,
not a service.

### Debian and Ubuntu

The maintained `debian/` package set provides `i2pd` (router, configuration,
service definitions, man pages and native tools) and `i2pd-tui` (interactive
ncurses client). It builds with debhelper, CMake and Ninja:

```sh
sudo apt install i2pd i2pd-tui
dpkg -L i2pd
dpkg -L i2pd-tui
systemctl status i2pd
service i2pd status
```

The package installs both a systemd unit and a SysV init script. On a systemd
host the `service` command reaches the same unit; on a SysV-only host it runs
the init script. Do not enable both independently. Configuration files such as
`/etc/i2pd/i2pd.conf`, `/etc/i2pd/tunnels.conf` and `/etc/default/i2pd` are
conffiles, so local edits survive upgrades. `apt remove` keeps data and
conffiles; `apt purge` deletes the configuration, keys and logs. See
[Debian and Ubuntu service management](../service-management.md#debian-and-ubuntu)
for package, conffile, purge, TUI-socket and coexistence details.

Distribution packages named `i2pd` (Arch, Gentoo, Debian/Ubuntu, Void and
others) generally track the upstream PurpleI2P source, not this fork. They may
lack the C++23 changes, local TUI, native torrent console and fork-specific
security work. Use them only if you intentionally want upstream i2pd. The old
third-party Debian repository, Ubuntu PPA, Fedora COPR and Flathub application
are likewise not maintained as openi2pd release channels.

## OpenBSD

Build from source with the [Unix guide](../devs/building/unix.md), or use the
local service files and sandbox notes in `contrib/openbsd/`. The upstream
`net/i2pd` port is separate from this fork. See the
[OpenBSD validation checklist](../security/openbsd-validation.md) for the
pledge/unveil, TUI socket, pidfile and SIGHUP/SIGUSR1 evidence that must be
collected on a real OpenBSD host.

## Docker

Build a local image from `contrib/docker/Dockerfile`; no prebuilt upstream image
is required. The container configuration disables the TUI socket because there
is no terminal. The HTTP administrative console has been removed; use the
native `i2pd-tui` client over its local IPC channel for status and administration.

## Verifying an installation

The binary is still named `i2pd`:

```sh
i2pd --version
i2pd --help
```

For a user process, data is stored in `~/.i2pd` on Linux and OpenBSD. With
`--service`, OpenBSD uses `/var/i2pd`, while Linux uses `/var/lib/i2pd`.
`--datadir` overrides these defaults. Windows uses the application data folder;
the Android app uses an app-specific `open-i2pd` directory. The local TUI is
optional and connects to the router over a mode-0600 local socket; it is not a
daemon dependency.

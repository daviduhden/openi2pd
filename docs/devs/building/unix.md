<!--
See the LICENSE file at the top of the project tree for copyright
and license details.

openi2pd: unix.

Documents project documentation.

-->

# Building on Unix-like systems

This fork supports Linux and OpenBSD. Windows and Android have their own
instructions. macOS, FreeBSD and Solaris are outside the supported platform
set; use upstream i2pd if you need them.

Make sure the dependencies from [requirements](requirements.md) are installed.
The language and ownership work is described in
[C++23 migration](../../cpp23-migration.md); the CMake target/install/test
layout is in [CMake and POSIX Make](../../build-system.md).

## Clone and build

```sh
git clone https://github.com/daviduhden/openi2pd.git
cd openi2pd
cmake -S build -B build-output -G Ninja \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build-output --parallel
```

The resulting binaries are `build-output/i2pd` and, when ncursesw was found,
`build-output/i2pd-tui`. Auxiliary tools are in `build-output/tools/`.

The POSIX make front-end performs the same steps:

```sh
CMAKE_BUILD_PARALLEL_LEVEL=8 make
```

Available front-end targets are `configure`, `build`, `all`, `router`, `tui`,
`tools`, `test`, `check`, `install`, `clean` and `dist`. `tests/Makefile`
configures with `BUILD_TESTING=ON`, builds and runs CTest. Do not pass `-j` to
the front-end; parallelism is controlled by CMake.

## Tests

```sh
cmake -S build -B build-output -G Ninja -DBUILD_TESTING=ON
cmake --build build-output --parallel
ctest --test-dir build-output --output-on-failure
```

Fuzz targets are enabled separately with `-DWITH_FUZZERS=ON` and require
Clang.

## Install

```sh
sudo cmake --install build-output --prefix /usr
```

With `I2PD_BUILD_TOOLS=ON` the install includes all native tools and
`i2pdctl`. The maintained RPM recipes install the router, TUI and tools; the
Debian package set splits the interactive client into `i2pd-tui`. Set
`I2PD_INSTALL_DOCS=OFF` to omit the shared documentation component.

or, after configuring the prefix:

```sh
sudo make install
```

### Service installation

Linux service definitions are installed by the `Service` component, separately
from the binaries. Select one manager at configure time:

```sh
cmake -S build -B build-output -G Ninja \
  -DCMAKE_INSTALL_PREFIX=/usr \
  -DI2PD_SERVICE_MANAGER=systemd \
  -DI2PD_SERVICE_DIR=/usr/lib/systemd/system
cmake --build build-output --parallel
sudo cmake --install build-output --component Service
sudo cmake --install build-output
```

The supported values are `none`, `systemd`, `openrc`, `runit`, `s6`, `s6-rc`,
`shepherd` and `sysvinit`. Install the router and the component in the same
prefix; custom prefixes and service directories must match the unit or script's
absolute configuration, data, runtime and pidfile paths. The supervised native
definitions keep the daemon in the foreground and must not pass `--daemon`; the
SysV compatibility script backgrounds it through its helper, and the OpenBSD
legacy path is separate. The TUI is an interactive client and has no service.
See [Service management](../../service-management.md) for commands, hardening
and the backgrounding exceptions.

Runtime start/stop, `systemd-analyze`, OpenRC/runit/s6/Shepherd checks and
Debian `lintian`/`autopkgtest` remain pending manual CI; they are not performed
locally.

## CMake options

Pass options as `-D<key>=<value>`:

| Option | Default | Meaning |
|---|---|---|
| `CMAKE_BUILD_TYPE` | none | `Debug`, `Release`, `RelWithDebInfo` |
| `I2PD_BUILD_TOOLS` | ON | Build the auxiliary tools |
| `I2PD_BUILD_TUI` | ON | Build `i2pd-tui` if ncurses is found |
| `I2PD_INSTALL_DOCS` | ON | Install documentation, branding assets and manual pages |
| `I2PD_SERVICE_MANAGER` | `none` | Install a service definition for `systemd`, `openrc`, `runit`, `s6`, `s6-rc`, `shepherd` or `sysvinit` |
| `I2PD_SERVICE_DIR` | manager default | Destination for the service unit, script or directory |
| `I2PD_SERVICE_CONFDIR` | `${CMAKE_INSTALL_SYSCONFDIR}/conf.d` | OpenRC `conf.d` installation directory; not the router configuration directory |
| `I2PD_PERL_CRITIC` | OFF | Opt-in Perl::Critic CTest check when `perlcritic` is installed |
| `BUILD_TESTING` | OFF | Build unit and protocol tests |
| `WITH_BINARY` | ON | Build the router |
| `WITH_LIBRARY` | ON | Install libraries and headers |
| `WITH_STATIC` | OFF | Static-link the router where supported |
| `WITH_UPNP` | ON | Require MiniUPnPc |
| `WITH_TORRENTS` | ON | Include the BitTorrent client; pass `-DWITH_TORRENTS=OFF` to disable it on Linux or OpenBSD (also supported on Windows) |
| `WITH_HARDENING` | OFF | Stack protector, FORTIFY, RELRO/NOW |
| `WITH_ADDRSANITIZER` | OFF | AddressSanitizer |
| `WITH_THREADSANITIZER` | OFF | ThreadSanitizer |
| `WITH_UNDEFSANITIZER` | OFF | UndefinedBehaviorSanitizer |

List cached options with `cmake -L -S build -B build-output`.

## Debian/Ubuntu

```sh
sudo apt install build-essential cmake ninja-build \
  libboost-program-options-dev libboost-json-dev libboost-url-dev \
  libssl-dev zlib1g-dev
sudo apt install libminiupnpc-dev  # optional UPnP
```

Build a package with the maintained `debian/rules` (CMake + Ninja) or use the
manual build above. The old Makefile patches and EOL Ubuntu packaging were
removed.

## Arch/Manjaro

```sh
sudo pacman -S --needed base-devel cmake ninja boost openssl zlib
```

## Fedora/RHEL/CentOS Stream

```sh
sudo dnf install cmake ninja-build gcc gcc-c++ boost-devel \
  openssl-devel zlib-devel
```

MiniUPnPc is in `miniupnpc-devel`; static Boost is in `boost-static` if needed.

## OpenBSD

```sh
doas pkg_add cmake ninja boost
doas pkg_add miniupnpc   # optional
```

OpenSSL is provided as LibreSSL in base; openi2pd requires LibreSSL 4.0 or
newer (OpenBSD 7.6+). Build with CMake/Ninja or with the POSIX make front-end
(`bmake`):

```sh
cmake -S build -B build-output -G Ninja \
  -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build-output --parallel
```

The OpenBSD service layout and `pledge`/`unveil` notes are in
[`contrib/openbsd/README.md`](../../../contrib/openbsd/README.md). Do not use
`gmake`: it is not required by this tree. Runtime evidence, including the TUI
socket, pidfile, log worker and SIGHUP/SIGUSR1 checks, is described in
[OpenBSD validation](../../security/openbsd-validation.md). The optional Perl
client is documented in [i2pdctl](../../tools/i2pdctl.md) and
[Perl migration](../../development/perl-migration.md).

## unsupported platforms

The CMake project fails early on macOS, FreeBSD and Solaris. Upstream i2pd may
still support them; this fork does not claim or test that support.

<!--
See the LICENSE file at the top of the project tree for copyright
and license details.

openi2pd: build system.

Documents project documentation.

-->

# CMake and POSIX Make migration

## Result

- CMake is the single source of build rules, tests, installation and options.
- No maintained file requires GNU make.
- The root `Makefile` and `tests/Makefile` use only POSIX make syntax
  (`=`, variable expansion, explicit targets and recipes). They contain no
  `?=`, `wildcard`, `shell`, `patsubst`, `ifeq`, `ifneq`, `include` or
  multiline variables. Command-line overrides still work, for example
  `make BUILD_DIR=build-release CMAKE_FLAGS="-DCMAKE_BUILD_TYPE=Release"`.
- Platform make fragments (`Makefile.linux`, `Makefile.openbsd`,
  `Makefile.mingw`) and `filelist.mk` were removed.

## CMake layout

`build/CMakeLists.txt` remains the entry point because existing instructions
and packaging use `cmake -S build`. The project root is derived explicitly.

Targets:

- `libi2pd` — core protocol/crypto library (static by default, PIC).
- `libi2pdclient` — client protocols, links `libi2pd`.
- `libi2pdlang` — language data.
- `i2pd_privsep` — local IPC, supervisor/worker lifecycle and platform
  sandbox adapters; linked by `i2pd` and kept separate from the core library.
- `i2pd` — router daemon.
- `i2pd-tui` — optional client; does not link the router libraries. Unix uses
  ncursesw, Windows uses the Win32 console/VT backend and a local named pipe.
  See [TUI on Windows](TUI-WINDOWS.md).
- test executables from `tests/CMakeLists.txt`, registered with CTest
  (including the WIN32-only `test-tui-win32-pipe` target and
  `test-privsep-windows-ipc`).

Options:

| Option | Default | Effect |
|---|---|---|
| `WITH_LIBRARY` | ON | Install libraries and headers |
| `WITH_BINARY` | ON | Build the router |
| `I2PD_BUILD_TUI` | ON | Build `i2pd-tui` (ncursesw on Unix; Win32 backend on Windows) |
| `I2PD_BUILD_TOOLS` | ON | Build and install the auxiliary tools |
| `I2PD_BUILD_JNI` | OFF | Android-only JNI shared library; rejected on other platforms |
| `I2PD_INSTALL_DOCS` | ON | Install documentation, branding assets and manual pages |
| `I2PD_PERL_CRITIC` | OFF | Register the opt-in `perl-critic` CTest target when `perlcritic` is installed |
| `WITH_UPNP` | ON | Require MiniUPnPc 2.2.6/API 17 and define `USE_UPNP`; API 18 signature used when available; also used by the Android JNI target |
| `WITH_TORRENTS` | ON | Include the BitTorrent client and require Boost.JSON/URL; pass `-DWITH_TORRENTS=OFF` to omit it on Linux, OpenBSD or Windows |
| `WITH_STATIC` | OFF | Static link the router where supported |
| `WITH_HARDENING` | OFF | Stack protector, FORTIFY, RELRO/NOW |
| `WITH_ADDRSANITIZER` | OFF | ASan |
| `WITH_THREADSANITIZER` | OFF | TSan (mutually exclusive with ASan) |
| `WITH_UNDEFSANITIZER` | OFF | UBSan |
| `BUILD_TESTING` | OFF | Build and register tests |

Sources are explicit lists. This intentionally replaces `file(GLOB ...)`:
glob results are not reliable dependency information and made installs depend
on directory contents.

Component dependencies are target-scoped:

| Component | Required dependencies | Optional dependency |
|---|---|---|
| `libi2pd` | Threads, Boost.program_options, OpenSSL/LibreSSL, zlib, libatomic when probed | — |
| `libi2pdclient` | `libi2pd`, Threads, Boost.JSON/Boost.URL when torrents are enabled | — |
| `i2pd_privsep` | Threads; OpenBSD `util`; Windows `advapi32` | — |
| `i2pd` | `libi2pd`, `libi2pdclient`, `libi2pdlang`, `i2pd_privsep` | MiniUPnPc when `WITH_UPNP=ON` |
| `i2pd_jni` (Android) | `libi2pd`, `libi2pdclient`, `libi2pdlang`, common daemon lifecycle, UPnP, I2PControl, TUI server; no `i2pd_privsep` (`I2PD_NO_PRIVSEP`) | MiniUPnPc when `WITH_UPNP=ON` |
| `i2pd-tui` | Threads; ncursesw on Unix; Win32 libraries on Windows | — |
| tools | in-tree libraries, Threads, portability/getopt source | — |
| tests | in-tree libraries and component sources | Perl/prove for syntax, portability, client and install checks; fuzzers require Clang |

No target uses a configure-time download, an in-tree glob, an absolute
installation prefix or a hidden external checkout.

## Generator compatibility

CMake's `Unix Makefiles` generator produces GNU make syntax. It is therefore
not a portable generator for this project's POSIX Make objective. The POSIX
make front-end defaults to `-G Ninja`:

```sh
make configure
make
make router
make tui
make tools
make test
make install
```

`make BUILD_DIR=build-release CMAKE_FLAGS="-DCMAKE_BUILD_TYPE=Release"` and
`make check` are supported; `tests/Makefile` configures, builds and runs CTest.

Generator portability:

| Generator | Supported use |
|---|---|
| Ninja | Default for POSIX make and all maintained CI jobs; no GNU make |
| Visual Studio 17 2022 | Windows native CI; builds via CMake |
| Unix Makefiles | Only when the user deliberately has GNU make; not used by the POSIX front-end or maintained CI |
| Xcode/other IDE generators | CMake may generate them, but they are outside the validated matrix |

This requires Ninja, which is a declared build dependency. It does not require
GNU make. Users may still invoke CMake directly with any generator that matches
their environment; the CMake project itself does not emit make syntax.

Parallel build jobs are controlled with CMake, not `make -j`:

```sh
CMAKE_BUILD_PARALLEL_LEVEL=8 make
```

## Out-of-tree builds

All documented commands configure a separate build directory. In-tree builds
are not used by the Makefile and are not required.

## Installation

```sh
cmake -S build -B build-output -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build-output --parallel
cmake --install build-output --prefix /usr
```

Installed files include `i2pd`, optional `i2pd-tui`, libraries/headers when
`WITH_LIBRARY=ON`, configuration files under `SYSCONFDIR/i2pd`, certificates
under `DATADIR/i2pd`, the man page, CMake package files and, when selected, a
native service definition. On Windows the
runtime install places `i2pd.exe` and `i2pd-tui.exe` in `CMAKE_INSTALL_BINDIR`;
DLLs are supplied by the selected toolchain/package manager.

### Service install selection

The `Service` component installs one native service definition for Linux. It
is independent of the build and can be omitted entirely. The option is rejected
on Windows and Android, which use their own service host or JNI wrapper.

| Variable | Values | Purpose |
|---|---|---|
| `I2PD_SERVICE_MANAGER` | `none`, `systemd`, `openrc`, `runit`, `s6`, `s6-rc`, `shepherd`, `sysvinit` | Select the service definition to install. `none` installs nothing. |
| `I2PD_SERVICE_DIR` | path | Destination directory for the unit, init script or service directory. Manager-specific conventional locations are used when unset. |
| `I2PD_SERVICE_CONFDIR` | path | OpenRC `/etc/conf.d` installation directory (default `${CMAKE_INSTALL_SYSCONFDIR}/conf.d`); it is not the router configuration directory. |

```sh
cmake -S build -B build-output -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=/usr \
  -DI2PD_SERVICE_MANAGER=systemd \
  -DI2PD_SERVICE_DIR=/usr/lib/systemd/system
cmake --build build-output --parallel
DESTDIR="$PWD/pkg" cmake --install build-output --component Service
DESTDIR="$PWD/pkg" cmake --install build-output
```

Install the `Service` component and the router binary into the same prefix;
installing only `Service` does not add `i2pd`. Packaging must choose exactly
one manager per package and must not install competing definitions for the same
service name. Absolute service paths do not follow `CMAKE_INSTALL_PREFIX`, so a
custom prefix requires matching `ExecStart`, data, runtime, pidfile,
configuration and log paths. Confirm the installed path from the package
manifest or `cmake --install ... --component Service --verbose` rather than
assuming a distribution's directory. The daemon defaults to foreground, the
systemd unit is `Type=exec` without `sd_notify`, and the TUI is never a
service. See [Service management](service-management.md).

### Windows TUI source selection

`I2PD_BUILD_TUI=ON` never requires Curses on Windows. The target uses
`tui/main.cpp`, `tui/TuiClientWin32.cpp`, `tui/TerminalWin32.cpp` and
`tui/TuiWin32Ipc.cpp`, and links only the Win32 libraries it uses. On Unix the
same target uses `tui/TuiClient.cpp` and `tui/TerminalUnix.cpp` with ncursesw.
The daemon always compiles `daemon/TuiServerCommon.cpp`; it adds
`daemon/TuiServer.cpp` only on Unix and `daemon/TuiServerWin32.cpp` plus
`tui/TuiWin32Ipc.cpp` only on Windows. See
[TUI on Windows](TUI-WINDOWS.md).

## OpenBSD ports

The OpenBSD port recipe is maintained outside this repository; the local
service/sandbox files and validation notes are in
[`contrib/openbsd/README.md`](../contrib/openbsd/README.md) and
[OpenBSD validation](security/openbsd-validation.md). The recipe should:

1. Build with CMake and Ninja (or run `bmake` through the POSIX front-end).
2. Use `--service` and the `/etc/i2pd` + `/var/i2pd` layout already
   documented in `contrib/openbsd`.
3. Keep the existing `pledge`/`unveil` policy; the TUI socket is covered by
   `unveil` with `rwc` on its configured path and does not require a new
   promise.
4. Not enable a terminal UI service; `i2pd-tui` is an interactive tool.

## Packaging changes in this tree

- Debian `rules` now configures and builds with CMake/Ninja and installs via
  `cmake --install`. Obsolete Makefile patches and EOL-distribution packaging
  copies were removed. The package set separates the router/service package
  (`i2pd`) from the interactive client (`i2pd-tui`). Debian keeps
  `I2PD_SERVICE_MANAGER=none` and installs the systemd unit and SysV
  compatibility script through debhelper, avoiding duplicate service files.
- RPM specs build with `cmake --build` instead of invoking `make` or `ninja`
  directly. They declare the separate Boost.JSON/URL packages for Fedora
  releases through 43. RHEL 9 needs a separate `gcc-toolset-13` environment
  and `-DWITH_TORRENTS=OFF`; the current specs do not claim that toolchain
  path is validated.
- The Windows helper builds with CMake/Ninja; the Windows XP path was removed.
- CI workflows use Ninja explicitly and remain **manual-only**
  (`workflow_dispatch`) during the refactoring. Linux native, sanitizer and
  fuzz jobs install the latest GCC, LLVM, CMake, Ninja, Boost, OpenSSL, zlib,
  MiniUPnPc and ncurses from Homebrew. The Debian package job deliberately
  retains the distribution toolchain so the produced package is validated
  against its declared build environment.

## Static build-system validation

`tests/check-build-system.cmake` is a compile-free CTest check. It rejects
GNU-only make syntax in the maintained front-ends, configure-time downloads
(`file(DOWNLOAD)`, `ExternalProject`, `FetchContent`), source globs, hard-coded
dependency paths in `FindMiniUPnPc.cmake`, hard-coded MSYS2/Inno paths or
`git pull` in the Windows helper, the removed OpenSSL 3.0.0/LibreSSL <4.0/XP/
MSVC-only compatibility paths, and verifies the canonical C++23/component
options, the `i2pd_privsep` component, the required POSIX Make targets and
dependency floors (OpenSSL 3.0.2, LibreSSL 4.0, MiniUPnPc 2.2.6/API 17), and
the RPM specs' `cmake --build` path, Fedora Boost guard and absence of direct
make/ninja macros. It also scans first-party C++ sources for unbounded
`sprintf`/`strcat`/`strcpy`/`strncpy`-family calls and requires the bounded
`BOOST_ASIO_HAS_SNPRINTF=1` selection. It is registered as
`build-system-static` and is part of the manual CI suite.
`tests/check-services.cmake` separately checks the foreground/service-manager
contract, forbidden readiness claims, ownership and log-directory wiring, and
is registered as `services-static`.

## Validation status

Under the permanent static-only development policy, no local configure,
build, install, CMake generate, CTest or `bmake -n` run is performed by agents.
The checks above are prepared for manual GitHub Actions execution. Historical
Linux results from earlier revisions do not validate the current worktree; no
Windows, OpenBSD or Android configure/build was executed in this environment.
Service definition installation, `systemd-analyze verify`, manager start/stop
smoke tests, Debian `lintian`, `autopkgtest` and package purge checks are also
pending manual CI. See `testing.md`, `AGENTS.md` and
[Service management](service-management.md#validation-status).

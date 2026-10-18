# CMake and POSIX Make migration

## Result

- CMake is the single source of build rules, tests, installation and options.
- No maintained file requires GNU make.
- The root `Makefile` and `tests/Makefile` use only POSIX make syntax
  (`=`/`?=`, variable expansion, explicit targets and recipes). They contain no
  `wildcard`, `shell`, `patsubst`, `ifeq`, `ifneq`, `include` or multiline
  variables.
- Platform make fragments (`Makefile.linux`, `Makefile.openbsd`,
  `Makefile.mingw`) and `filelist.mk` were removed.

## CMake layout

`build/CMakeLists.txt` remains the entry point because existing instructions
and packaging use `cmake -S build`. The project root is derived explicitly.

Targets:

- `libi2pd` — core protocol/crypto library (static by default, PIC).
- `libi2pdclient` — client protocols, links `libi2pd`.
- `libi2pdlang` — language data.
- `i2pd` — router daemon.
- `i2pd-tui` — optional client; does not link the router libraries. Unix uses
  ncursesw, Windows uses the Win32 console/VT backend and a local named pipe.
  See [TUI on Windows](TUI-WINDOWS.md).
- test executables from `tests/CMakeLists.txt`, registered with CTest
  (including the WIN32-only `test-tui-win32-pipe` target).

Options:

| Option | Default | Effect |
|---|---|---|
| `WITH_LIBRARY` | ON | Install libraries and headers |
| `WITH_BINARY` | ON | Build the router |
| `WITH_TUI` | ON | Build `i2pd-tui` (ncursesw on Unix; Win32 backend on Windows) |
| `WITH_UPNP` | ON | Require MiniUPnPc and define `USE_UPNP` |
| `WITH_TORRENTS` | ON | Require Boost.JSON/URL; off on Android |
| `WITH_STATIC` | OFF | Static link the router where supported |
| `WITH_HARDENING` | OFF | Stack protector, FORTIFY, RELRO/NOW |
| `WITH_ADDRSANITIZER` | OFF | ASan |
| `WITH_THREADSANITIZER` | OFF | TSan (mutually exclusive with ASan) |
| `WITH_UNDEFSANITIZER` | OFF | UBSan |
| `BUILD_TESTING` | OFF | Build and register tests |
| `WITH_GIT_VERSION` | OFF | Embed git describe output |

Sources are explicit lists. This intentionally replaces `file(GLOB ...)`:
glob results are not reliable dependency information and made installs depend
on directory contents.

## Generator compatibility

CMake's `Unix Makefiles` generator produces GNU make syntax. It is therefore
not a portable generator for this project's POSIX Make objective. The POSIX
make front-end defaults to `-G Ninja`:

```sh
make
make BUILD_DIR=build-release CMAKE_FLAGS="-DCMAKE_BUILD_TYPE=Release"
make test
```

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
under `DATADIR/i2pd`, the man page and CMake package files. On Windows the
runtime install places `i2pd.exe` and `i2pd-tui.exe` in `CMAKE_INSTALL_BINDIR`;
DLLs are supplied by the selected toolchain/package manager.

### Windows TUI source selection

`WITH_TUI=ON` never requires Curses on Windows. The target uses
`tui/main.cpp`, `tui/TuiClientWin32.cpp`, `tui/TerminalWin32.cpp` and
`tui/TuiWin32Ipc.cpp`, and links only the Win32 libraries it uses. On Unix the
same target uses `tui/TuiClient.cpp` and `tui/TerminalUnix.cpp` with ncursesw.
The daemon always compiles `daemon/TuiServerCommon.cpp`; it adds
`daemon/TuiServer.cpp` only on Unix and `daemon/TuiServerWin32.cpp` plus
`tui/TuiWin32Ipc.cpp` only on Windows. See
[TUI on Windows](TUI-WINDOWS.md).

## OpenBSD ports

The OpenBSD port recipe is maintained outside this repository. It should:

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
  copies were removed.
- RPM specs build with `cmake --build` instead of invoking `make` or `ninja`
  directly.
- The Windows helper builds with CMake/Ninja; the Windows XP path was removed.
- CI workflows use Ninja explicitly and remain **manual-only**
  (`workflow_dispatch`) during the refactoring.

## Not validated here

A Linux/GCC 16 configure and build passed, as did a CMake install into a
temporary prefix and a `bmake -n` parse of the POSIX front-end. No
configure/build was executed for Windows, OpenBSD or Android in this
environment. See `testing.md`.

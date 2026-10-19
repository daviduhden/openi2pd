<!--
See the LICENSE file at the top of the project tree for copyright
and license details.

openi2pd: windows.

Documents project documentation.

-->

Building on Windows
=========================

There are two approaches available to build i2pd on Windows. The best
one depends on your needs and personal preferences. One is to use
msys2 and [unix alike infrastructure](unix.md). Another
one is to use Visual Studio. While there might be no difference for
end users of i2pd daemon, developers, however, shall be wary of
differences in C++ name mangling between the two compilers when making
a choice to be able to link their software against libi2pd.

If you are a stranger to C++ with no development tools installed on
your system and your only goal is to have i2pd up and running from the
most recent source, consider using msys2. Although it relies on
command line operations, it should be straight forward.

In this guide, we will use CMake for both approaches and we will
assume that you typically have your projects in C:\dev\ as your
development location for the sake of convenience. Adjust paths
accordingly if it is not the case. Note that msys uses unix-alike
paths like /c/dev/ for C:\dev\.

> **openi2pd note:** the maintained entry point is CMake; there are no GNU
> make build commands in the supported Windows procedure. Windows 10 or newer
> is required (`WINVER=0x0A00`). The router requires C++23, CMake 3.22+,
> Boost 1.74 (1.81 for torrent JSON/URL), OpenSSL 3.0.2+
> (or LibreSSL 4.0+) and zlib 1.2.11+; MiniUPnPc 2.2.6/API 17 is required
> while `WITH_UPNP=ON` (API 18 is used when available). The manual Windows
> workflow is `workflow_dispatch`-only
> and is not started by a push. See [TUI on Windows](../../TUI-WINDOWS.md) for
> the TUI/IPC details.

## Current CMake build

MSYS2/Clang64 (the toolchain used by the manual workflow):

```sh
cmake -S build -B build-output -G Ninja \
  -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build-output --parallel --target all
ctest --test-dir build-output --output-on-failure
```

Native MSVC 19.35+ with vcpkg-provided dependencies:

```powershell
cmake -S build -B build-msvc -G "Visual Studio 17 2022" -A x64 `
  -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" `
  -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build-msvc --config Release --parallel --target all
ctest --test-dir build-msvc -C Release --output-on-failure
```

Install with `cmake --install build-msvc --config Release --prefix <dir>`;
`i2pd.exe`, `i2pd-tui.exe`, the native tools and `i2pdctl` are placed in the
runtime `bin` directory when `I2PD_BUILD_TOOLS=ON`. The legacy
`build/build_mingw.cmd` packaging path builds all targets and includes the TUI
and tool executables in its ZIP/installer. No dependency is downloaded by the
installed product.

### TUI on Windows

`I2PD_BUILD_TUI=ON` is the default. Windows does **not** use ncurses/PDCurses: the
client uses the Win32 console/VT backend and a local named pipe to the router.
Build `i2pd-tui` together with the router; it still does not link the router
libraries. If the router runs as a service under another account, start
`i2pd-tui.exe` from that account's desktop or adjust the service/DACL model
deliberately. See [TUI on Windows](../../TUI-WINDOWS.md) for DACL,
service-versus-interactive, terminal support and troubleshooting guidance.

> Runtime validation of the Windows TUI, DACL and MSVC build is pending. The
> obsolete upstream MSYS2/GNU-make build instructions were removed from this
> maintained guide; use the CMake commands above.

### Historical upstream notes

Older upstream instructions used MSYS2 `make`, `USE_AESNI` and `USE_UPNP`
switches. The maintained tree has no such build switches: use CMake options
(`WITH_UPNP`, `WITH_HARDENING`, `CMAKE_BUILD_TYPE`) and Ninja. The MSYS2 and
Inno Setup helper discovers installations through `MSYS2_ROOT`, `ISCC` or
`PATH` and does not download or pull the worktree.

Using Visual Studio
-------------------

### Installing dependencies through vcpkg

Requirements:

* [vcpkg](https://vcpkg.io/) (NOTE: Due to Microsoft's telemetry policies, it is strongly advised to opt out of data collection by referring to [this document](https://github.com/microsoft/vcpkg#telemetry).)
* [Visual Studio Community Edition](https://www.visualstudio.com/en-us/products/visual-studio-community-vs.aspx) (tested with VS2022)

This part assumes that you know how to use and integrate vcpkg into Visual Studio.
Read [this](https://learn.microsoft.com/en-us/vcpkg/) if you are unsure.
Navigate to the vcpkg directory, and run(change x64-windows to x86-windows for a 32bit build):

    .\vcpkg install boost-program-options:x64-windows boost-json:x64-windows boost-url:x64-windows openssl:x64-windows zlib:x64-windows miniupnpc:x64-windows

You may now skip directly to the [build process](#creating-visual-studio-project).

### Without vcpkg

Provide the same current dependencies through a local prefix and pass it as
`-DCMAKE_PREFIX_PATH=<prefix>`:

* CMake 3.22 or newer.
* Visual Studio 2022 17.5 (MSVC 19.35) or newer with the C++23 toolset.
* Boost 1.74+ with `program_options`; Boost 1.81+ with `json` and `url` when
  torrents are enabled.
* OpenSSL 3.0.2+ (or LibreSSL 4.0+), zlib 1.2.11+.
* MiniUPnPc 2.2.6+ (API 17) while `WITH_UPNP=ON`; API 18 is preferred when
  available.

The project does not bundle, download or build these libraries. Build them with
their maintained upstream procedures for the selected Windows toolchain, or
use vcpkg/MSYS2.

The maintained Windows build helper is `build_mingw.cmd`; CI workflows are
manual-only.

### Creating Visual Studio project

Start CMake GUI, select `build` as the source directory and a separate output
directory, then configure the desired options.

From the command line:

```powershell
cmake -S build -B build-msvc -G "Visual Studio 17 2022" -A x64 `
  -DCMAKE_PREFIX_PATH=<dependency-prefix> `
  -DWITH_UPNP=ON
```

MiniUPnPc is required when `WITH_UPNP=ON`; configure fails rather than silently
disabling UPnP if the minimum API is unavailable.

### Building i2pd

Open the generated solution in Visual Studio, or build from the command line:

```powershell
cmake --build build-msvc --config Release --parallel
ctest --test-dir build-msvc -C Release --output-on-failure
```

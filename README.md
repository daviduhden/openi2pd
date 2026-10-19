<!--
See the LICENSE file at the top of the project tree for copyright
and license details.

openi2pd: README.

Documents project documentation.

-->

# openi2pd

![openi2pd logo](assets/branding/logo-horizontal.svg)

openi2pd is an independent fork of [i2pd](https://github.com/PurpleI2P/i2pd),
a C++ implementation of the I2P router. I2P provides encrypted, anonymous
communication for applications and services.

`openi2pd` is the product and visual identity. The logo, color tokens and usage
rules are documented in [docs/branding](docs/branding/README.md), with the
canonical palette in [assets/branding/colors.md](assets/branding/colors.md).

The supported platforms are **Windows, Linux, OpenBSD and Android**. macOS,
FreeBSD and Solaris are outside the CMake support matrix; use upstream i2pd
there. The TUI and documentation are in English. OpenSSL and LibreSSL are
supported. The HTTP administrative console has been removed.

The executable remains `i2pd`, and existing configuration files, service names
and data directories remain compatible. OpenBSD services store data in
`/var/i2pd`; Linux services use `/var/lib/i2pd`.

## Build

Install a C++23 compiler (GCC 13+, Clang 17+ or MSVC 19.35+), CMake 3.22+,
Ninja, Boost 1.74+ with program_options (Boost 1.81+ with JSON/URL for the
torrent client), OpenSSL 3.0.2+ or LibreSSL 4.0+, and zlib. UPnP support is
compiled in by default and requires MiniUPnPc 2.2.6+ (API 17). From the
repository root:

```sh
cmake -S build -B build-output -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-output --parallel
```

To omit UPnP support, use `-DWITH_UPNP=OFF`. To build and run the unit tests,
add `-DBUILD_TESTING=ON` and run
`ctest --test-dir build-output --output-on-failure`.

The root `Makefile` is a POSIX make front-end for the same CMake build and
works with BSD make on OpenBSD. It uses the Ninja generator because CMake's
"Unix Makefiles" generator emits GNU make syntax. GNU make is no longer
required or supported by the maintained build files. Targets include
`configure`, `build`, `router`, `tui`, `tools`, `test`, `check` and `install`;
`tests/Makefile` configures, builds and runs CTest. Windows build
instructions are in [build_mingw.cmd](build/build_mingw.cmd).

Android app sources are integrated under [android](android/). The app builds
the in-tree router through CMake/JNI; it no longer depends on a Git submodule or
an external i2pd checkout. Development requires OpenJDK 25+, Java 25
source/target, Gradle 9.8.1, AGP 9.4.1, SDK 37 and NDK r30 LTS+. See
[android/README.md](android/README.md)
and [docs/android-java25.md](docs/android-java25.md).

## Run

Start `build-output/i2pd` for a headless router. If built with `I2PD_BUILD_TUI`
(the default), `i2pd-tui` connects to the local bounded administrative
endpoint (an `AF_UNIX` socket on Unix, `<datadir>/i2pd-tui.sock` by default, or
a local named pipe on Windows); router status is read-only and torrent actions
require explicit confirmation. The router runs without a terminal, stdin or the
TUI. See the [configuration example](contrib/i2pd.conf),
[documentation](docs/README.md), the [fork guide index](docs/index.md) and the
[Windows TUI guide](docs/TUI-WINDOWS.md). OpenBSD configuration is in
[contrib/openbsd](contrib/openbsd). Upstream documentation provenance and the
manual synchronization procedure are recorded in
[docs/upstream-sync.md](docs/upstream-sync.md); MkDocs remains an optional
publication tool and is not a daemon or build dependency.

The former HTTP administrative console has been removed. The I2P HTTP proxy,
HTTP tunnels, reseeding and I2PControl remain available.

Report issues at <https://github.com/daviduhden/openi2pd/issues>.

## License

openi2pd source is distributed under the [BSD 3-Clause license](LICENSE)
(`SPDX-License-Identifier: BSD-3-Clause`). Documentation and imported
third-party components retain their respective licenses.
Copyright and attribution for the original i2pd contributors are preserved.

## Continuous integration

CI is manual-only. The workflows cover Linux (GCC and Clang), Windows
(MSYS2/Clang64 and native MSVC), OpenBSD (Clang and LibreSSL), Android and
optional sanitizer/fuzz jobs. The prepared Android job runs Java 25 unit tests,
Lint, the native arm64 JNI link and an APK build; instrumentation and device
tests remain separate. Current results are recorded in
[docs/testing.md](docs/testing.md) and the CI workflows; no current-tree
dynamic validation is claimed until those manual runs pass. Start a run through
**Actions > Run workflow** (`workflow_dispatch`).

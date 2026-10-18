# Open I2Pd

Open I2Pd is an independent fork of [i2pd](https://github.com/PurpleI2P/i2pd),
a C++ implementation of the I2P router. I2P provides encrypted, anonymous
communication for applications and services.

The supported platforms are **Windows, Linux, OpenBSD and Android**. The TUI
and documentation are in English. OpenSSL and LibreSSL are supported. The HTTP
administrative console has been removed.

The executable remains `i2pd`, and existing configuration files, service names
and data directories remain compatible. OpenBSD services store data in
`/var/i2pd`; Linux services use `/var/lib/i2pd`.

## Build

Install a C++23 compiler (GCC 13+, Clang 17+ or MSVC 19.35+), CMake 3.22+,
Ninja, Boost 1.74+, OpenSSL 3.0+ or LibreSSL 3.5+, and zlib. UPnP support is
compiled in by default and requires MiniUPnPC. From the repository root:

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
required or supported by the maintained build files. Windows build
instructions are in [build_mingw.cmd](build/build_mingw.cmd).

Android app sources are integrated under [android](android/). The app builds
the in-tree router through CMake/JNI; it no longer depends on a Git submodule or
an external i2pd checkout. Development requires OpenJDK 25+, Java 25 source/target, Gradle 9.8.1,
AGP 9.4.1, SDK 37 and NDK r27+. See [android/README.md](android/README.md)
and [docs/android-java25.md](docs/android-java25.md).

## Run

Start `build-output/i2pd` for a headless router. If ncurses was found at
configure time, `build-output/i2pd-tui` connects to the local
bounded administrative socket (`tui.socket`, default `<datadir>/i2pd-tui.sock`);
router status is read-only and torrent actions require explicit confirmation. The
router runs without a terminal, stdin or the TUI. See the
[configuration example](contrib/i2pd.conf) and
[documentation](docs/README.md). OpenBSD configuration is in
[contrib/openbsd](contrib/openbsd).

The former HTTP administrative console has been removed. The I2P HTTP proxy,
HTTP tunnels, reseeding and I2PControl remain available.

Report issues at <https://github.com/daviduhden/openi2pd/issues>.

## License

Open I2Pd is distributed under the [BSD 3-Clause license](LICENSE).
Copyright and attribution for the original i2pd contributors are preserved.

## Continuous integration

Three workflows cover Windows (Clang, UCRT and libc++), Linux and Android on
Ubuntu, and OpenBSD (Clang and LibreSSL). Native builds run five basic HTTP,
encoding and encryption tests once. Android cross-compiles an arm64 binary
with the NDK; no emulator is started. Android dependencies are cached and
built in Release mode only. UPnP support uses the default enabled setting.

Automatic CI runs are suspended during the C++23/CMake/TUI refactoring. The
workflows remain available through **Actions > Run workflow**
(`workflow_dispatch`) and must be started manually until the suspension is
lifted explicitly.

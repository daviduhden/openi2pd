# Open I2Pd

Open I2Pd is an independent fork of [i2pd](https://github.com/PurpleI2P/i2pd),
a C++ implementation of the I2P router. I2P provides encrypted, anonymous
communication for applications and services.

The supported platforms are **Windows, Linux, OpenBSD and Android**. The web
console and documentation are in English. OpenSSL and LibreSSL are supported.

The executable remains `i2pd`, and existing configuration files, service names
and data directories remain compatible. OpenBSD services store data in
`/var/i2pd`; Linux services use `/var/lib/i2pd`.

## Build

Install a C++20 compiler, CMake, Boost, OpenSSL or LibreSSL, and zlib.
UPnP support is compiled in by default and requires MiniUPnPC. From the repository root:

```sh
cmake -S build -B build-output -DCMAKE_BUILD_TYPE=Release
cmake --build build-output --parallel
```

To omit UPnP support, use `-DWITH_UPNP=OFF` with CMake or `USE_UPNP=no`
with GNU Make. To build and run the unit tests, add
`-DBUILD_TESTING=ON` and run `ctest --test-dir build-output --output-on-failure`.
GNU Make is also supported. Windows build instructions are in
[build_mingw.cmd](build/build_mingw.cmd).

Android retains the native integration used by
[i2pd-android](https://github.com/PurpleI2P/i2pd-android); this repository does
not contain an Android app or its packaging.

## Run

Start `build-output/i2pd` and open <http://127.0.0.1:7070/> for the web console.
See the [configuration example](contrib/i2pd.conf) and
[documentation](docs/README.md). OpenBSD configuration is in
[contrib/openbsd](contrib/openbsd).

Report issues at <https://github.com/daviduhden/open-i2pd/issues>.

## License

Open I2Pd is distributed under the [BSD 3-Clause license](LICENSE).
Copyright and attribution for the original i2pd contributors are preserved.

## Continuous integration

Three workflows cover Windows (Clang, UCRT and libc++), Linux and Android on
Ubuntu, and OpenBSD (Clang and LibreSSL). Native builds run five basic HTTP,
encoding and encryption tests once. Android cross-compiles an arm64 binary
with the NDK; no emulator is started. Android dependencies are cached and
built in Release mode only. UPnP support uses the default enabled setting.

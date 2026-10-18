# Build requirements

openi2pd requires:

* A C++23 compiler: GCC 13+, Clang 17+, or MSVC 19.35 (VS 2022 17.5)+.
* CMake 3.22 or newer.
* Ninja 1.10 or newer for the POSIX make front-end. CMake can also be invoked
  directly with a generator supported by the local toolchain.
* Boost 1.74+ with `atomic`, `filesystem`, and `program_options`.
  Boost 1.81+ with `json` and `url` is required when the torrent client is
  enabled (the default outside Android).
* OpenSSL 3.0+ or LibreSSL 3.5+.
* zlib 1.2.11+.

Optional:

* MiniUPnPc 2.1+ for UPnP (`WITH_UPNP=ON`, default ON).
* ncursesw 6.0+ for `i2pd-tui` (`I2PD_BUILD_TUI=ON`, default ON when found).

The POSIX make front-end uses Ninja because CMake's `Unix Makefiles` generator
emits GNU make syntax. GNU make is not required or supported by the maintained
build files.

See [dependencies](../../dependencies.md) for the detailed rationale and the
list of removed compatibility branches.

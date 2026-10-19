# Build requirements

openi2pd supports Linux, OpenBSD, Windows and Android. macOS, FreeBSD and
Solaris are rejected by the top-level CMake entry point; use upstream i2pd for
those platforms.

openi2pd requires:

* A C++23 compiler: GCC 13+, Clang 17+, or MSVC 19.35 (VS 2022 17.5)+. See
  [C++23 migration](../../cpp23-migration.md) for the language rationale and
  remaining ownership work.
* CMake 3.22 or newer.
* Ninja 1.10 or newer for the POSIX make front-end. CMake can also be invoked
  directly with a generator supported by the local toolchain.
* Boost 1.74+ with `program_options`. Boost 1.81+ with `json` and `url` is
  required when the torrent client is enabled (the default outside Android).
* OpenSSL 3.0.2+ or LibreSSL 4.0+.
* zlib 1.2.11+.

Optional:

* MiniUPnPc 2.2.6+ (API 17) for UPnP (`WITH_UPNP=ON`, default ON). API 18 is
  used when available for the external-address form. The Android JNI build also
  links MiniUPnPc because UPnP is enabled by default in the app.
* ncursesw 6.0+ for `i2pd-tui` on Unix (`I2PD_BUILD_TUI=ON`, default ON). Windows
  uses the Win32 console/VT backend and needs no Curses library; see
  [TUI on Windows](../../TUI-WINDOWS.md).

The POSIX make front-end uses Ninja because CMake's `Unix Makefiles` generator
emits GNU make syntax. GNU make is not required or supported by the maintained
build files.

See [dependencies](../../dependencies.md) for the detailed rationale and the
list of removed compatibility branches, and
[CMake and POSIX Make](../../build-system.md) for the target, install and test
model. No GNU make, submodule or download-at-configure step is supported.

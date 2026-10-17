# Dependency and minimum-version inventory

The versions below are the supported minimums for the refactored tree. They
are derived from the APIs actually used, not from upstream's historical
matrix.

| Dependency | Minimum | Why this minimum | Used by |
|---|---|---|---|
| C++ compiler | GCC 13, Clang 17, MSVC 19.35 (VS 2022 17.5) | `std::expected`, `std::jthread`, `std::span`, concepts/short requires, C++23 library facilities | Everything |
| CMake | 3.22 | Modern target/usage requirements, `install(EXPORT)`, `CMakePackageConfigHelpers`, CMP0077 behaviour; available on all supported platforms | Build |
| Ninja | 1.10 | Generator for the POSIX make front-end; CMake's `Unix Makefiles` generator emits GNU make syntax | Build front-end |
| Boost | 1.74 for atomic/filesystem/program_options; 1.81 for JSON/URL | `boost::json` and `boost::url` are required by the torrent RPC and only exist from 1.81. Others are older but 1.74 is a realistic floor for the supported distros. | `libi2pd`, `libi2pd_client` |
| OpenSSL | 3.0 | EVP/KDF and ML-KEM capability macros used by the PQ paths; OpenSSL 3 is also the packaging floor of the Debian control file | `libi2pd`, `libi2pd_client`, daemon |
| LibreSSL | 3.5 (OpenBSD 7.4+) | SHA-1/SipHash/Ed25519 APIs used by the code. PQ code is compiled out unless the library exposes ML-KEM. Capability checks remain in place. | OpenBSD builds |
| zlib | 1.2.11 | `inflateInit2`, raw-deflate SU3 ZIP handling | `libi2pd`, tests |
| ncursesw | 6.0 | Optional TUI; wide-character build preferred, ASCII rendering used | `i2pd-tui` only |
| MiniUPnPc | 2.1 | Optional UPnP client; `upnpDiscover`/`UPNP_GetValidIGD` API | daemon when `WITH_UPNP=ON` |

## Removed compatibility branches

| Removed | Reason |
|---|---|
| C++20 fallback in CMake and make | C++23 is a hard requirement; accepting C++20 silently removed `std::expected` |
| `-D_GLIBCXX_USE_NANOSLEEP` | GCC 4.7/4.8 workaround |
| CMake < 3.22 policy downgrade | Unsupported build environment |
| Boost < 1.55 Asio option branch | Superseded by the declared Boost 1.74 minimum |
| `#if __cplusplus >= 202002L` branches | Always true under the new standard |
| `filelist.mk`, `Makefile.linux`, `Makefile.openbsd`, `Makefile.mingw`, GNU make syntax | Replaced by CMake and a POSIX make front-end |
| CMake < 3.21 `crypt32` workaround | Not reachable with CMake 3.22+ |
| Windows XP build path | C++23 toolchain and current OpenSSL/Boost do not support it |

## Retained capability checks

These are **not** obsolete-version branches. They select between APIs that
really differ between OpenSSL and LibreSSL, or between libraries with and
without ML-KEM:

- `OPENSSL_VERSION_NUMBER` / `LIBRESSL_VERSION_NUMBER` guards in
  `Crypto.*`, `Signature.*`, `PostQuantum.*`, `Reseed.cpp`, `SSU2Session.cpp`,
  `NTCP2.cpp`, `I2PControl.cpp`.
- `OPENSSL_PQ` for ML-KEM availability.
- `BOOST_ASIO_HAS_LOCAL_SOCKETS` for I2PControl's local socket.
- `__linux__`/`__OpenBSD__` guards for peer credentials, `pledge`/`unveil`,
  `IPV6_ADDR_PREFERENCES`, and platform service directories.

No dependency is downloaded during configure, build or run. Android
dependencies continue to be provided by the existing vcpkg manifest used in CI;
the project itself has no hidden fetch step.

## OpenBSD notes

- LibreSSL is used through the same OpenSSL EVP/API surface where available.
  ML-KEM is not assumed; `PostQuantum` and NTCP2/SSU2 version selection fall
  back explicitly.
- `pledge`/`unveil` remain enabled. The new TUI socket adds only
  `rwc` access to its configured path and relies on the existing `unix` and
  `stdio` promises.
- The port integration is external to this repository. The maintained tree no
  longer requires `gmake`; a port should invoke CMake with Ninja or the POSIX
  make front-end.

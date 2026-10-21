<!--
See the LICENSE file at the top of the project tree for copyright
and license details.

openi2pd: dependencies.

Documents project documentation.

-->

# Dependency and minimum-version inventory

The versions below are the supported minimums for the refactored tree. They
are derived from the APIs actually used, not from upstream's historical
matrix.

| Dependency | Minimum | Why this minimum | Used by |
|---|---|---|---|
| C++ compiler | GCC 13, Clang 17, MSVC 19.35 (VS 2022 17.5) | C++23 language/library baseline (`std::jthread`, `std::span`, concepts, `std::atomic_ref`, `std::filesystem`, `std::endian`/`std::byteswap`); the portable `i2p::expected` shim remains only for the OpenBSD base libc++ gap | Everything |
| CMake | 3.22 | Modern target/usage requirements, `install(EXPORT)`, `CMakePackageConfigHelpers`, CMP0077 behaviour; available on all supported platforms | Build |
| Ninja | 1.10 | Generator for the POSIX make front-end; CMake's `Unix Makefiles` generator emits GNU make syntax | Build front-end |
| Boost | 1.74 for program_options; 1.81 for JSON/URL | `boost::program_options` is the only compiled core Boost library now that Boost.Atomic and Boost.Filesystem are no longer used; Asio/Beast/property_tree/algorithm are header-only. `boost::json` and `boost::url` are required by the torrent RPC and only exist from 1.81. | `libi2pd`, `libi2pd_client` |
| OpenSSL | 3.0.2 | EVP/KDF/param-building APIs used by the supported code paths; the 3.0.0-only SipHash regression is no longer supported. Enforced by CMake. Ed25519ph/type 8 uses plain Ed25519 (no OpenSSL 3.2 provider dependency); ML-KEM remains gated at 3.5. | `libi2pd`, `libi2pd_client`, daemon |
| LibreSSL | 4.0 (OpenBSD 7.6+) | SHA-1/SipHash/Ed25519 APIs used by the code; the pre-4.0 in-place ChaCha20-Poly1305 workaround is gone. Enforced by CMake when `OPENSSL_VERSION` identifies LibreSSL. PQ code remains capability-gated. | OpenBSD builds |
| zlib | 1.2.11 | `inflateInit2`, raw-deflate SU3 ZIP handling; enforced by CMake | `libi2pd`, tests |
| ncursesw | 6.0 | Optional TUI; wide-character build preferred, ASCII rendering used | `i2pd-tui` only |
| MiniUPnPc | 2.2.6 / API 17 | Optional UPnP client; 2.2.6 is the current Ubuntu 24.04 package. API 18 adds the external-address `UPNP_GetValidIGD` signature and is selected when present; older API branches are rejected. | daemon and Android JNI when `WITH_UPNP=ON` |
| Perl | OpenBSD base Perl / any Perl 5 with core modules | `i2pdctl` uses `Config`, `File::Spec`, `Getopt::Long`, `JSON::PP`, `HTTP::Tiny`, `IPC::Open3`, `IO::Select`, `POSIX` and `Symbol`; tests use `Test::More`. No CPAN or runtime download is performed. | `i2pdctl` and optional static/CI checks only |
| `openssl` command | Same OpenSSL/LibreSSL base already required | Used only by the shell-free `s_client` fallback when `IO::Socket::SSL` is absent; HTTPS never downgrades to cleartext. | `i2pdctl` HTTPS fallback |

## Removed compatibility branches

| Removed | Reason |
|---|---|
| C++20 fallback in CMake and make | C++23 is a hard requirement; accepting C++20 silently removed `std::expected` |
| `-D_GLIBCXX_USE_NANOSLEEP` | GCC 4.7/4.8 workaround |
| CMake < 3.22 policy downgrade | Unsupported build environment |
| Boost < 1.55 Asio option branch | Superseded by the declared Boost 1.74 minimum |
| `#if __cplusplus >= 202002L` branches | Always true under the new standard |
| Boost.Filesystem fallback (`STD_FILESYSTEM=0`, MinGW path workaround) | C++23 `std::filesystem` is unconditional on supported toolchains |
| Boost.Atomic `shared_ptr` fallback (`__cpp_lib_atomic_shared_ptr`) | C++20 `std::atomic<std::shared_ptr>` is part of the supported library baseline |
| `std::atomic_ref` fallback in Torrents | C++20/C++23 standard library baseline |
| `WITH_TUI` and `I2PD_BUILD_TESTS` aliases | Canonical `I2PD_BUILD_TUI` and `BUILD_TESTING` remain |
| `filelist.mk`, `Makefile.linux`, `Makefile.openbsd`, `Makefile.mingw`, GNU make syntax | Replaced by CMake and a POSIX make front-end |
| CMake < 3.21 `crypt32` workaround | Not reachable with CMake 3.22+ |
| Windows XP build path, `inet_pton_xp`/`inet_ntop_xp` and `WINVER == 0x0501` branches | C++23/MSVC 19.35 support Windows 10+; `InetPtonA`/`InetNtopA` and Network List Manager are available directly |
| `_MSC_VER` `sprintf_s` branches in SAM/Timestamp | MSVC 19.35 provides C99 `snprintf`; checked lengths now use one portable path |
| `JSON_SUPPORTED` / "Your boost version ... is too old" fallback in TorrentsRPC | CMake requires Boost 1.81 JSON/URL whenever torrents are enabled |
| `BOOST_ASIO_HAS_STD_ARRAY` and Boost.Bind placeholder workarounds | C++23 `std::array` and current Boost.Asio need no fallback |
| Android non-Clang `std::to_string`/`std::stoi` polyfill and `__ANDROID_API__ < 24` `ifaddrs.h` branch | Android minSdk is 29 (Android 10) with a Clang NDK; libc++ and `getifaddrs` are available |
| Legacy `LittleBigEndian.h`, `I2PEndian.cpp` fallback and compiler-specific byte-swap intrinsics | Windows uses C++23 `std::endian`/`std::byteswap`; Unix/Android use the platform `endian(3)` header directly |
| OpenSSL 3.0.0 SipHash exception | OpenSSL 3.0.2 is the enforced minimum |
| LibreSSL `< 4.0` in-place ChaCha20-Poly1305 copy | LibreSSL 4.0 is the enforced minimum |
| MiniUPnPc API `< 14`/`< 10`/`< 8` selection branches | MiniUPnPc 2.2.6/API 17 is the enforced minimum (API <17 is rejected); API 18 remains a signature capability check |
| Legacy LLVM atomic-builtin probe in `CheckAtomic.cmake` | C++23 `std::atomic` is unconditional; the probe was unused |
| `EVP_MD_CTX_create`/`EVP_MD_CTX_destroy` aliases and `OPENSSL_SUPPRESS_DEPRECATED` | Replaced with `EVP_MD_CTX_new`/`EVP_MD_CTX_free`; no legacy OpenSSL alias is suppressed |
| Redundant Boost.Atomic/Filesystem CI, Debian, Docker and Android vcpkg packages | The project uses `std::atomic`, `std::filesystem` and only program_options/JSON/URL |

## Retained capability checks

These are **not** obsolete-version branches. They select between APIs that
really differ between OpenSSL and LibreSSL, or between libraries with and
without ML-KEM:

- `OPENSSL_VERSION_NUMBER` / `LIBRESSL_VERSION_NUMBER` guards in
  `Crypto.*`, `Signature.*`, `PostQuantum.*`, `Reseed.cpp`, `SSU2Session.cpp`,
  `NTCP2.cpp`, `I2PControl.cpp`.
- `OPENSSL_PQ` for ML-KEM availability.
- `MINIUPNPC_API_VERSION >= 18` for the external-address `UPNP_GetValidIGD` signature; API 17 uses the older call plus `UPNP_GetExternalIPAddress`.
- `BOOST_ASIO_HAS_LOCAL_SOCKETS` for I2PControl's local socket.
- `__linux__`/`__OpenBSD__` guards for peer credentials, `pledge`/`unveil`,
  `IPV6_ADDR_PREFERENCES`, and platform service directories.
- The `i2p::expected` shim, `std::move_only_function` fallback and
  `__cpp_lib_chrono` fallback are retained only for the OpenBSD base libc++
  gaps; modern libc++/libstdc++/MSVC use the standard facilities.

The RPM specs declare the separate Boost.JSON/URL packages for Fedora
releases through 43. RHEL 9 is not supported by its default GCC 11/Boost 1.75
toolchain: it needs a separate `gcc-toolset-13` environment, and the torrent
client must be disabled with `-DWITH_TORRENTS=OFF` because Boost.URL is not
available. The specs do not claim RHEL support until that toolset path is
validated.

No dependency is downloaded during configure, build or run. Android
dependencies continue to be provided by the existing vcpkg manifest used in CI;
the project itself has no hidden fetch step. CMake discovery relies on the
normal toolchain/prefix search path; MiniUPnPc no longer injects distribution-
specific `/usr`, `/usr/local`, `/opt/local` or source-relative paths. The
compile-free `build-system-static` test rejects download rules and hard-coded
installation paths in the maintained build files.

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

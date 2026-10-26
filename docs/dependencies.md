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
| CMake | 3.24 | Modern target/usage requirements, `install(EXPORT)`, `CMakePackageConfigHelpers` and `CMAKE_TRY_COMPILE_TARGET_TYPE`; available on all supported platforms | Build |
| Ninja | 1.10 | Generator for the POSIX make front-end; CMake's `Unix Makefiles` generator emits GNU make syntax | Build front-end |
| Boost | 1.74 for program_options; 1.81 with torrents | `Boost::program_options` supplies the core compiled dependency; Asio/Beast/property_tree/algorithm are header-only. Torrent RPC additionally links the compiled `Boost::json` and `Boost::url` libraries. `Boost.Container` is linked when the host ships it because a static Boost.JSON needs its out-of-line `pmr` symbols; shared builds normally resolve it transitively, while a static configure fails clearly when it is absent. The joint torrent dependency floor is 1.81; including their public headers alone does not supply implementations. | `libi2pd`, `libi2pd_client` |
| OpenSSL | 3.0.2 through 4.0.x | EVP/provider/KDF/param-building APIs used by the supported code paths. OpenSSL 4.0 removed fixed TLS version methods, opaque ASN.1 strings, engine/custom-method APIs and several deprecated entry points; the tree uses `TLS_method()`/Asio role contexts, ASN.1 accessors and provider fetches. CMake reads the selected headers directly, runs compile-only feature probes and one linked modern-API probe, and caches a backend/probe stamp so a reused build directory cannot mix OpenSSL and LibreSSL answers. Ed25519ph/type 8 uses plain Ed25519; ML-KEM remains a runtime-gated 3.5+ feature. | `libi2pd`, `libi2pd_client`, daemon, tools |
| LibreSSL | 4.0 (OpenBSD 7.7+) | SHA-1/SipHash/Ed25519 APIs used by the code; the pre-4.0 in-place ChaCha20-Poly1305 workaround is gone. CMake reads `LIBRESSL_VERSION_TEXT` from the selected `opensslv.h` rather than treating the frozen compatibility version as an OpenSSL release. Feature probes select legacy EC/RSA accessors only where the provider API is absent; both `SSL_set1_dnsname` and `SSL_set1_host` are probed and a backend with neither fails configure. PQ code remains capability-gated. | OpenBSD builds |
| zlib | 1.2.11 | `inflateInit2`, raw-deflate SU3 ZIP handling; enforced by CMake | `libi2pd`, tests |
| ncursesw | 6.0 | TUI; required when `I2PD_BUILD_TUI=ON` (default) and `I2PD_TUI_REQUIRED=ON`, otherwise optional. Wide-character build preferred; ASCII rendering used. | `i2pd-tui` only |
| MiniUPnPc | 2.2.6 / API 17 | Optional UPnP client; 2.2.6 is the current Ubuntu 24.04 package. API 18 adds the external-address `UPNP_GetValidIGD` signature and is selected when present; older API branches are rejected. | daemon and Android JNI when `WITH_UPNP=ON` |
| Perl | OpenBSD base Perl / any Perl 5 with core modules | `i2pdctl` uses `Config`, `File::Spec`, `Getopt::Long`, `JSON::PP`, `HTTP::Tiny`, `IPC::Open3`, `IO::Select`, `POSIX` and `Symbol`; tests use `Test::More`. No CPAN or runtime download is performed. | `i2pdctl` and optional static/CI checks only |
| `openssl` command | Same OpenSSL/LibreSSL base already required | Used only by the shell-free `s_client` fallback when `IO::Socket::SSL` is absent; HTTPS never downgrades to cleartext. | `i2pdctl` HTTPS fallback |

## Removed compatibility branches

| Removed | Reason |
|---|---|
| C++20 fallback in CMake and make | C++23 is a hard requirement; accepting C++20 silently removed `std::expected` |
| `-D_GLIBCXX_USE_NANOSLEEP` | GCC 4.7/4.8 workaround |
| CMake < 3.24 policy downgrade | Unsupported build environment |
| Boost < 1.55 Asio option branch | Superseded by the declared Boost 1.74 minimum |
| `#if __cplusplus >= 202002L` branches | Always true under the new standard |
| Boost.Filesystem fallback (`STD_FILESYSTEM=0`, MinGW path workaround) | C++23 `std::filesystem` is unconditional on supported toolchains |
| Boost.Atomic `shared_ptr` fallback (`__cpp_lib_atomic_shared_ptr`) | C++20 `std::atomic<std::shared_ptr>` is part of the supported library baseline |
| `std::atomic_ref` fallback in Torrents | C++20/C++23 standard library baseline |
| `WITH_TUI` and `I2PD_BUILD_TESTS` aliases | Canonical `I2PD_BUILD_TUI` and `BUILD_TESTING` remain |
| `filelist.mk`, `Makefile.linux`, `Makefile.openbsd`, `Makefile.mingw`, GNU make syntax | Replaced by CMake and a POSIX make front-end |
| CMake < 3.21 `crypt32` workaround | Not reachable with CMake 3.24+ |
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
| Boost.Atomic/Boost.Filesystem entries in the OpenBSD `WANTLIB` | The tree uses `std::atomic`/`std::filesystem`; only Boost.Container/JSON/URL/ProgramOptions remain compiled dependencies |
| `EVP_MD_CTX_create`/`EVP_MD_CTX_destroy` aliases and `OPENSSL_SUPPRESS_DEPRECATED` | Replaced with `EVP_MD_CTX_new`/`EVP_MD_CTX_free`; no legacy OpenSSL alias is suppressed |
| Redundant Boost.Atomic/Filesystem CI, Debian, Docker and Android vcpkg packages | The project uses `std::atomic`, `std::filesystem` and only program_options/JSON/URL |

## Retained capability checks

These are **not** obsolete-version branches. They select between APIs that
really differ between OpenSSL and LibreSSL, or between libraries with and
without ML-KEM:

- Compile-only CMake probes in `build/CMakeLists.txt` for
  `EVP_PKEY_is_a`, `EVP_PKEY_get_group_name`, `EVP_PKEY_get_bn_param`,
  `EVP_MD_fetch`, `EVP_CIPHER_fetch`, `EVP_KDF_fetch`, `EVP_EC_gen`,
  `ASN1_STRING_get0_data` and `TLS_method`. OpenSSL must provide the modern
  probes; LibreSSL keeps the small legacy fallback branches. The probes are
  compile tests only and never execute a target binary.
- `I2PD_OPENSSL_EXPECT` / `I2PD_OPENSSL_EXPECT_VERSION` are optional CI guards
  that fail configuration when the selected backend is not the intended one.
- `OPENSSL_VERSION_NUMBER` / `LIBRESSL_VERSION_NUMBER` guards remain only where
  the API genuinely differs (for example the LibreSSL ML-KEM header/API).
- `OPENSSL_PQ` for ML-KEM availability, plus the runtime `CanPublishPQ()` /
  `HighestSupportedPQVersion()` gates. A compile-time PQ build never
  advertises a parameter set that the active provider cannot create.
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

String formatting uses `snprintf`/`vsnprintf`, `inet_ntop` and
`if_indextoname`, which are guaranteed by the declared compiler and platform
minima. The project does not depend on `strlcpy` or `strlcat`; fixed C-style
buffers use the internal `CopyString` helper instead, avoiding a libc-specific
symbol or fallback. CMake feature probes are limited to OpenSSL/LibreSSL API
selection and do not probe optional libc formatting functions. The `BOOST_ASIO_HAS_SNPRINTF` definition selects Asio's
bounded `snprintf` fallback for scope IDs; project address/endpoint formatting
avoids Asio's remaining POSIX `strcat` scope path.

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
  longer requires `gmake`; `contrib/openbsd/port/` is a merge template and a
  port should invoke CMake with Ninja or the POSIX make front-end. The default
  TUI adds `devel/ncurses`/`ncursesw` to the port dependencies.

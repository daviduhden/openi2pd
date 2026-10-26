# See the LICENSE file at the top of the project tree for copyright
# and license details.
#
# openi2pd: CMakeLists.txt.
#
# Top-level CMake entry point: it enforces C++23 and the supported platforms,
# builds libi2pd/libi2pdclient/libi2pdlang, the router, TUI, optional Android
# JNI and tools, and installs runtime, config and headers. Dependency floors
# cover Boost, OpenSSL/LibreSSL, zlib and optional MiniUPnPc.
#

cmake_minimum_required(VERSION 3.24)

# The project deliberately keeps its CMake entry point in build/ for
# compatibility with existing package recipes.
set(PROJECT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")
list(PREPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake_modules")

include(Version)
set_version("${PROJECT_ROOT}/libi2pd/version.h" PROJECT_VERSION)

project(
  OpenI2Pd
  VERSION ${PROJECT_VERSION}
  HOMEPAGE_URL "https://github.com/daviduhden/openi2pd"
  LANGUAGES C CXX
)

# Win32/Resource.rc carries the executable icon and version block. Without the
# RC language CMake ignores it as a non-compilable source. enable_language()
# has no working OPTIONAL mode, so probe with CheckLanguage and enable RC only
# when a resource compiler is actually available.
if(WIN32)
  include(CheckLanguage)
  check_language(RC)
  if(CMAKE_RC_COMPILER)
    enable_language(RC)
  else()
    message(WARNING "No Windows resource compiler found: building i2pd without icon/version resources")
  endif()
endif()

if(NOT CMAKE_SYSTEM_NAME MATCHES "^(Windows|Linux|OpenBSD|Android)$")
  message(FATAL_ERROR "openi2pd supports Windows, Linux, OpenBSD and Android")
endif()

# CMake's Unix Makefiles generator emits GNU make syntax. OpenBSD's make
# cannot consume it, so reject that combination instead of producing a
# tree that only appears to be buildable.
if(CMAKE_SYSTEM_NAME STREQUAL "OpenBSD" AND CMAKE_GENERATOR MATCHES "Makefiles$")
  message(FATAL_ERROR
    "The ${CMAKE_GENERATOR} generator emits GNU make syntax; use -G Ninja on OpenBSD")
endif()

# C++23 is a hard requirement. The portable Expected shim provides the
# value-or-error API on supported libc++/libstdc++ versions that lack
# <expected>; this does not lower the language standard.
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 13)
  message(FATAL_ERROR "GCC 13 or newer is required for C++23 (found ${CMAKE_CXX_COMPILER_VERSION})")
elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 17)
  message(FATAL_ERROR "Clang 17 or newer is required for C++23 (found ${CMAKE_CXX_COMPILER_VERSION})")
elseif(MSVC AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 19.35)
  message(FATAL_ERROR "Visual Studio 2022 17.5 (19.35) or newer is required for C++23")
endif()

option(WITH_HARDENING       "Use hardening compiler flags"                 OFF)
option(WITH_LIBRARY         "Install libraries and headers"                ON)
option(WITH_BINARY          "Build the router executable"                  ON)
option(I2PD_BUILD_TUI       "Build the TUI (ncursesw on Unix, Win32 on Windows)" ON)
option(I2PD_TUI_REQUIRED    "Fail configuration when the Unix TUI dependency is missing" ON)
option(I2PD_BUILD_TOOLS     "Build i2pd auxiliary tools"                   ON)
option(I2PD_INSTALL_DOCS   "Install documentation and branding assets"    ON)
option(I2PD_INSTALL_CONFIG "Install the sample router configuration"      ON)
option(I2PD_PERL_CRITIC    "Run optional Perl::Critic checks in CTest"    OFF)
option(I2PD_REQUIRE_PERL    "Fail configuration when the Perl test helper is missing" OFF)
option(I2PD_BUILD_JNI       "Build the Android JNI library"                  OFF)
option(WITH_STATIC          "Link the router executable statically"        OFF)
option(WITH_UPNP            "Include support for the UPnP client"          ON)
option(WITH_TORRENTS        "Include the BitTorrent client (default ON; pass -DWITH_TORRENTS=OFF to disable)" ON)
option(WITH_ADDRSANITIZER   "Build with AddressSanitizer (Unix)"           OFF)
option(WITH_THREADSANITIZER "Build with ThreadSanitizer (Unix)"            OFF)
option(WITH_UNDEFSANITIZER  "Build with UndefinedBehaviorSanitizer (Unix)" OFF)
option(BUILD_TESTING        "Build unit and protocol tests"                OFF)
option(WITH_FUZZERS         "Build libFuzzer targets (Clang, requires tests)" OFF)
if(WITH_FUZZERS AND NOT BUILD_TESTING)
  message(FATAL_ERROR "WITH_FUZZERS requires BUILD_TESTING=ON")
endif()
if(WITH_FUZZERS AND (NOT WITH_ADDRSANITIZER OR NOT WITH_UNDEFSANITIZER))
  message(FATAL_ERROR
    "WITH_FUZZERS requires WITH_ADDRSANITIZER=ON and WITH_UNDEFSANITIZER=ON; "
    "an uninstrumented library archive would make the fuzzers report false success.")
endif()
# A static executable and a sanitizer runtime cannot be combined portably: the
# sanitizer needs its own shared runtime and `-static` conflicts with it. Fail
# instead of silently producing a partially instrumented binary.
if(WITH_STATIC AND (WITH_ADDRSANITIZER OR WITH_THREADSANITIZER OR WITH_UNDEFSANITIZER))
  message(FATAL_ERROR "WITH_STATIC cannot be combined with sanitizer options")
endif()
if(WITH_STATIC AND I2PD_BUILD_JNI)
  message(FATAL_ERROR "WITH_STATIC cannot be combined with the Android JNI library")
endif()

include(GNUInstallDirs)
# GNUInstallDirs 3.30+ derives DOCDIR from PROJECT_NAME (OpenI2Pd here), while
# the package and documentation paths are maintained as "i2pd". Replace only
# that derived default; preserve an explicit user override.
if(NOT DEFINED CMAKE_INSTALL_DOCDIR
    OR CMAKE_INSTALL_DOCDIR STREQUAL "${CMAKE_INSTALL_DATAROOTDIR}/doc/${PROJECT_NAME}")
  set(CMAKE_INSTALL_DOCDIR "${CMAKE_INSTALL_DATAROOTDIR}/doc/i2pd" CACHE PATH
    "Documentation installation directory" FORCE)
  # GNUInstallDirs may have created a normal variable that shadows the cache
  # on the first configure. Remove it so install() and later reconfigures read
  # the selected cache value consistently.
  unset(CMAKE_INSTALL_DOCDIR)
endif()

include(CTest)

# Service-manager selection. Distributions install exactly one init system;
# the default installs none so the historical build/install graph is unchanged.
# Native platform service integration is meaningless on Windows and Android,
# where the daemon is started by the OS service manager or the JNI host.
set(I2PD_SERVICE_MANAGER "none" CACHE STRING
  "Init-system service files to install (none, systemd, openrc, runit, s6, s6-rc, shepherd, sysvinit)")
set_property(CACHE I2PD_SERVICE_MANAGER PROPERTY STRINGS
  none systemd openrc runit s6 s6-rc shepherd sysvinit)

set(I2PD_SERVICE_DIR "" CACHE PATH
  "Override the selected service-manager installation directory (empty uses the manager default)")
set(I2PD_SERVICE_CONFDIR "" CACHE PATH
  "OpenRC conf.d installation directory (empty uses ${CMAKE_INSTALL_SYSCONFDIR}/conf.d)")

set(_i2pd_service_managers
  none systemd openrc runit s6 s6-rc shepherd sysvinit)
if(NOT I2PD_SERVICE_MANAGER IN_LIST _i2pd_service_managers)
  message(FATAL_ERROR
    "Invalid I2PD_SERVICE_MANAGER='${I2PD_SERVICE_MANAGER}'. "
    "Expected one of: ${_i2pd_service_managers}")
endif()
if(NOT I2PD_SERVICE_MANAGER STREQUAL "none"
    AND (ANDROID OR WIN32 OR NOT UNIX))
  message(FATAL_ERROR
    "I2PD_SERVICE_MANAGER=${I2PD_SERVICE_MANAGER} is only supported on "
    "Unix-like systems; Windows and Android are managed by their native "
    "service facilities. Use -DI2PD_SERVICE_MANAGER=none.")
endif()
if(NOT I2PD_SERVICE_MANAGER STREQUAL "none" AND NOT WITH_BINARY)
  message(FATAL_ERROR
    "I2PD_SERVICE_MANAGER=${I2PD_SERVICE_MANAGER} requires WITH_BINARY=ON; "
    "a service definition must not be installed without the router it starts.")
endif()
# The packaged units and init scripts invoke /etc/i2pd/i2pd.conf explicitly.
# Installing one without the sample configuration produces a service that can
# never start, so reject that combination at configure time.
if(NOT I2PD_SERVICE_MANAGER STREQUAL "none" AND NOT I2PD_INSTALL_CONFIG)
  message(FATAL_ERROR
    "I2PD_SERVICE_MANAGER=${I2PD_SERVICE_MANAGER} requires "
    "I2PD_INSTALL_CONFIG=ON; the packaged service reads /etc/i2pd/i2pd.conf.")
endif()

# The destination is relative by default, so DESTINATION is interpreted
# against CMAKE_INSTALL_PREFIX. An absolute user override is passed through.
if(I2PD_SERVICE_DIR)
  set(_i2pd_service_dir "${I2PD_SERVICE_DIR}")
elseif(I2PD_SERVICE_MANAGER STREQUAL "systemd")
  # Units are architecture-independent and systemd does not search the
  # multiarch lib/<triplet> tree.
  set(_i2pd_service_dir "lib/systemd/system")
elseif(I2PD_SERVICE_MANAGER STREQUAL "openrc"
    OR I2PD_SERVICE_MANAGER STREQUAL "sysvinit")
  set(_i2pd_service_dir "${CMAKE_INSTALL_SYSCONFDIR}/init.d")
elseif(I2PD_SERVICE_MANAGER STREQUAL "runit")
  set(_i2pd_service_dir "${CMAKE_INSTALL_SYSCONFDIR}/sv")
elseif(I2PD_SERVICE_MANAGER STREQUAL "s6")
  set(_i2pd_service_dir "${CMAKE_INSTALL_SYSCONFDIR}/s6/service")
elseif(I2PD_SERVICE_MANAGER STREQUAL "s6-rc")
  set(_i2pd_service_dir "${CMAKE_INSTALL_SYSCONFDIR}/s6-rc/source")
elseif(I2PD_SERVICE_MANAGER STREQUAL "shepherd")
  set(_i2pd_service_dir "${CMAKE_INSTALL_SYSCONFDIR}/shepherd")
else()
  set(_i2pd_service_dir "")
endif()
if(I2PD_SERVICE_CONFDIR)
  set(_i2pd_service_confdir "${I2PD_SERVICE_CONFDIR}")
else()
  set(_i2pd_service_confdir "${CMAKE_INSTALL_SYSCONFDIR}/conf.d")
endif()

if(ANDROID)
  set(I2PD_BUILD_TUI OFF)
  # Android uses the JNI shared library; there is no supported standalone
  # Android daemon executable. The app wrapper already forces this off, and
  # the manual native CI build must use the same graph.
  set(WITH_BINARY OFF CACHE BOOL "Build the router executable" FORCE)
endif()

if(I2PD_BUILD_JNI AND NOT ANDROID)
  message(FATAL_ERROR "I2PD_BUILD_JNI is only supported when targeting Android")
endif()

if(WITH_STATIC)
  # Save the normal suffix list. The static hint only governs dependency
  # discovery; tools, tests and the TUI must keep their platform defaults.
  set(_i2pd_saved_find_library_suffixes "${CMAKE_FIND_LIBRARY_SUFFIXES}")
  if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    set(CMAKE_FIND_LIBRARY_SUFFIXES ".lib")
  else()
    set(CMAKE_FIND_LIBRARY_SUFFIXES ".a")
  endif()
  set(Boost_USE_STATIC_LIBS ON)
  set(OPENSSL_USE_STATIC_LIBS ON)
  set(ZLIB_USE_STATIC_LIBS ON)
endif()

include(CheckAtomic)
include(CheckCXXSourceCompiles)
include(CheckSymbolExists)
# CheckAtomic restores its probe flags and reports the library that the host
# actually needs (empty when lock-free atomics are native).
set(I2PD_EXTRA_LIBS "${I2PD_ATOMIC_LIBRARIES}")
# OpenBSD 8.0 replaced imsg_get() with imsgbuf_get() (the older name returns the
# total message size, the new one 1/0/-1; this tree treats any positive result
# as success). Probe the selected base rather than hard-coding a release macro.
if(CMAKE_SYSTEM_NAME STREQUAL "OpenBSD")
  set(_i2pd_saved_required_libraries "${CMAKE_REQUIRED_LIBRARIES}")
  set(CMAKE_REQUIRED_LIBRARIES util)
  check_symbol_exists(imsgbuf_get "imsg.h" I2PD_HAVE_IMSGBUF_GET)
  set(CMAKE_REQUIRED_LIBRARIES "${_i2pd_saved_required_libraries}")
  unset(_i2pd_saved_required_libraries)
endif()
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

if(WITH_TORRENTS)
  # JSON and URL declare out-of-line symbols. We use their compiled libraries,
  # rather than including either library's src.hpp in a project source file.
  # Boost.JSON uses boost::container::pmr. Shared Boost resolves it through
  # its own link closure; a static package needs the compiled Container
  # library, so request it optionally rather than forcing every consumer to
  # install a component that shared builds do not need.
  find_package(Boost 1.81 REQUIRED COMPONENTS program_options json url
    OPTIONAL_COMPONENTS container)
  set(I2PD_BOOST_MIN_VERSION 1.81)
  set(I2PD_BOOST_COMPONENTS program_options json url)
  set(I2PD_BOOST_OPTIONAL_COMPONENTS container)
else()
  find_package(Boost 1.74 REQUIRED COMPONENTS program_options)
  set(I2PD_BOOST_MIN_VERSION 1.74)
  set(I2PD_BOOST_COMPONENTS program_options)
  # Keep the optional component name in the installed package config too:
  # FindBoost creates the target only when the host actually ships it.
  set(I2PD_BOOST_OPTIONAL_COMPONENTS container)
endif()
if(WITH_STATIC AND WITH_TORRENTS AND NOT TARGET Boost::container)
  message(FATAL_ERROR
    "Static Boost.JSON requires the compiled Boost.Container library; "
    "install the Boost container component or disable torrents.")
endif()
find_package(OpenSSL REQUIRED COMPONENTS Crypto SSL)
# LibreSSL deliberately keeps OPENSSL_VERSION_NUMBER frozen at 0x20000000
# for source compatibility, so CMake's FindOpenSSL reports 2.0.0 even for a
# current LibreSSL. Read the selected headers explicitly and never search a
# different include prefix: headers and libraries must come from one install.
set(I2PD_OPENSSL_KIND "")
set(I2PD_OPENSSL_VERSION "")
set(I2PD_LIBRESSL_VERSION "")
set(_i2pd_opensslv_path "${OPENSSL_INCLUDE_DIR}/openssl/opensslv.h")
if(NOT EXISTS "${_i2pd_opensslv_path}")
  message(FATAL_ERROR
    "OpenSSL headers were not found at OPENSSL_INCLUDE_DIR='${OPENSSL_INCLUDE_DIR}'. "
    "Refusing to search another prefix; set OPENSSL_ROOT_DIR consistently.")
endif()
file(READ "${_i2pd_opensslv_path}" _i2pd_opensslv_text)
string(REGEX MATCH "LIBRESSL_VERSION_TEXT[ \t]+\"LibreSSL[ \t]+([0-9]+\\.[0-9]+(\\.[0-9]+)?)\""
  _i2pd_libressl_match "${_i2pd_opensslv_text}")
if(CMAKE_MATCH_1)
  set(I2PD_LIBRESSL_VERSION "${CMAKE_MATCH_1}")
  set(I2PD_OPENSSL_VERSION "${CMAKE_MATCH_1}")
  set(I2PD_OPENSSL_KIND "LibreSSL")
else()
  string(REGEX MATCH "OPENSSL_VERSION_STR[ \t]+\"([0-9]+\\.[0-9]+\\.[0-9]+)\""
    _i2pd_openssl_str_match "${_i2pd_opensslv_text}")
  if(CMAKE_MATCH_1)
    set(I2PD_OPENSSL_VERSION "${CMAKE_MATCH_1}")
  else()
    string(REGEX MATCH "OPENSSL_VERSION_TEXT[ \t]+\"OpenSSL[ \t]+([0-9]+\\.[0-9]+\\.[0-9]+)"
      _i2pd_openssl_text_match "${_i2pd_opensslv_text}")
    if(CMAKE_MATCH_1)
      set(I2PD_OPENSSL_VERSION "${CMAKE_MATCH_1}")
    endif()
  endif()
  if(I2PD_OPENSSL_VERSION)
    set(I2PD_OPENSSL_KIND "OpenSSL")
  endif()
endif()
unset(_i2pd_opensslv_text)
if(NOT I2PD_OPENSSL_KIND)
  message(FATAL_ERROR "Unable to determine OpenSSL/LibreSSL version from '${OPENSSL_INCLUDE_DIR}/openssl/opensslv.h'")
endif()
if(I2PD_OPENSSL_KIND STREQUAL "LibreSSL")
  if(I2PD_OPENSSL_VERSION VERSION_LESS 4.0)
    message(FATAL_ERROR "LibreSSL 4.0 or newer is required (found LibreSSL ${I2PD_OPENSSL_VERSION})")
  endif()
else()
  if(I2PD_OPENSSL_VERSION VERSION_LESS 3.0.2)
    message(FATAL_ERROR "OpenSSL 3.0.2 or newer is required (found ${I2PD_OPENSSL_VERSION})")
  endif()
endif()
set(I2PD_OPENSSL_EXPECT "" CACHE STRING
  "Optional CI guard: require OpenSSL or LibreSSL")
set(I2PD_OPENSSL_EXPECT_VERSION "" CACHE STRING
  "Optional CI guard: require this exact selected version")
if(I2PD_OPENSSL_EXPECT AND NOT I2PD_OPENSSL_KIND STREQUAL I2PD_OPENSSL_EXPECT)
  message(FATAL_ERROR "Expected ${I2PD_OPENSSL_EXPECT}, selected ${I2PD_OPENSSL_KIND}")
endif()
if(I2PD_OPENSSL_EXPECT_VERSION AND NOT I2PD_OPENSSL_VERSION VERSION_EQUAL I2PD_OPENSSL_EXPECT_VERSION)
  message(FATAL_ERROR
    "Expected ${I2PD_OPENSSL_EXPECT_VERSION}, selected ${I2PD_OPENSSL_KIND} ${I2PD_OPENSSL_VERSION}")
endif()
message(STATUS "OpenSSL library    : ${I2PD_OPENSSL_KIND} ${I2PD_OPENSSL_VERSION}")
message(STATUS "OpenSSL headers    : ${OPENSSL_INCLUDE_DIR}")
message(STATUS "OpenSSL libs       : ${OPENSSL_CRYPTO_LIBRARY};${OPENSSL_SSL_LIBRARY}")

# Feature probes, not version-number guesses. These are compile-only and do not
# execute the target platform. LibreSSL keeps the fallback branches in code.
set(_i2pd_saved_required_includes "${CMAKE_REQUIRED_INCLUDES}")
set(_i2pd_saved_required_libraries "${CMAKE_REQUIRED_LIBRARIES}")
set(_i2pd_saved_try_compile_target_type "${CMAKE_TRY_COMPILE_TARGET_TYPE}")
set(CMAKE_REQUIRED_INCLUDES "${OPENSSL_INCLUDE_DIR}")
set(CMAKE_REQUIRED_LIBRARIES "${OPENSSL_CRYPTO_LIBRARY};${OPENSSL_SSL_LIBRARY}")
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# Feature probe answers are cache entries. Reusing a build directory after
# switching OpenSSL/LibreSSL or a prefix must not mix old answers with the new
# selected headers and libraries.
set(I2PD_OPENSSL_PROBES
  I2PD_HAVE_TLS_METHOD
  I2PD_HAVE_ASN1_STRING_GET0_DATA
  I2PD_HAVE_EVP_PKEY_IS_A
  I2PD_HAVE_EVP_PKEY_GROUP_NAME
  I2PD_HAVE_EVP_PKEY_BN_PARAM
  I2PD_HAVE_EVP_MD_FETCH
  I2PD_HAVE_EVP_CIPHER_FETCH
  I2PD_HAVE_EVP_KDF_FETCH
  I2PD_HAVE_EVP_PKEY_SIPHASH
  I2PD_HAVE_EVP_EC_GEN
  I2PD_HAVE_SSL_SET1_DNSNAME
  I2PD_HAVE_SSL_SET1_HOST
  I2PD_HAVE_OPENSSL_MLKEM_H
  I2PD_OPENSSL_LINK_OK)
set(_i2pd_probe_stamp
  "${OPENSSL_INCLUDE_DIR}|${I2PD_OPENSSL_KIND}|${I2PD_OPENSSL_VERSION}")
if(DEFINED I2PD_OPENSSL_PROBE_STAMP
    AND NOT I2PD_OPENSSL_PROBE_STAMP STREQUAL "${_i2pd_probe_stamp}")
  foreach(_i2pd_probe IN LISTS I2PD_OPENSSL_PROBES)
    unset(${_i2pd_probe} CACHE)
  endforeach()
endif()
set(I2PD_OPENSSL_PROBE_STAMP "${_i2pd_probe_stamp}" CACHE INTERNAL
  "Selected OpenSSL backend used for the cached capability probes" FORCE)
unset(_i2pd_probe_stamp)
check_cxx_source_compiles("
#include <openssl/ssl.h>
int main() { const SSL_METHOD *m = TLS_method(); return m ? 0 : 1; }"
  I2PD_HAVE_TLS_METHOD)
check_cxx_source_compiles("
#include <openssl/asn1.h>
int main() { ASN1_STRING *s = ASN1_STRING_new(); const unsigned char *d = ASN1_STRING_get0_data(s);
  int l = ASN1_STRING_length(s); (void)d; (void)l; ASN1_STRING_free(s); return 0; }"
  I2PD_HAVE_ASN1_STRING_GET0_DATA)
check_cxx_source_compiles("
#include <openssl/evp.h>
int main() { EVP_PKEY *p = nullptr; return p && EVP_PKEY_is_a(p, \"EC\") ? 0 : 0; }"
  I2PD_HAVE_EVP_PKEY_IS_A)
check_cxx_source_compiles("
#include <openssl/evp.h>
int main() { EVP_PKEY *p = nullptr; char g[80]; size_t n = 0;
  return p && EVP_PKEY_get_group_name(p, g, sizeof(g), &n) == 1 ? 0 : 0; }"
  I2PD_HAVE_EVP_PKEY_GROUP_NAME)
check_cxx_source_compiles("
#include <openssl/evp.h>
#include <openssl/bn.h>
int main() { EVP_PKEY *p = nullptr; BIGNUM *b = nullptr;
  int r = p ? EVP_PKEY_get_bn_param(p, \"n\", &b) : 0; BN_free(b); return r == 1 ? 0 : 0; }"
  I2PD_HAVE_EVP_PKEY_BN_PARAM)
check_cxx_source_compiles("
#include <openssl/evp.h>
int main() { EVP_MD *m = EVP_MD_fetch(nullptr, \"SHA256\", nullptr);
  EVP_MD_free(m); return 0; }"
  I2PD_HAVE_EVP_MD_FETCH)
check_cxx_source_compiles("
#include <openssl/evp.h>
int main() { EVP_CIPHER *c = EVP_CIPHER_fetch(nullptr, \"AES-256-CBC\", nullptr);
  EVP_CIPHER_free(c); return 0; }"
  I2PD_HAVE_EVP_CIPHER_FETCH)
check_cxx_source_compiles("
#include <openssl/kdf.h>
#include <openssl/core_names.h>
#ifndef OSSL_KDF_PARAM_MODE
#error HKDF parameter names are unavailable
#endif
int main() { EVP_KDF *k = EVP_KDF_fetch(nullptr, \"HKDF\", nullptr);
  const char *mode = OSSL_KDF_PARAM_MODE; (void)mode; EVP_KDF_free(k); return 0; }"
  I2PD_HAVE_EVP_KDF_FETCH)
check_cxx_source_compiles("
#include <openssl/evp.h>
#ifdef OPENSSL_NO_SIPHASH
#error SipHash is disabled in this libcrypto
#endif
int main() { EVP_PKEY *p = EVP_PKEY_new_raw_private_key(EVP_PKEY_SIPHASH, nullptr,
  reinterpret_cast<const unsigned char *>(\"0123456789abcdef\"), 16); EVP_PKEY_free(p); return 0; }"
  I2PD_HAVE_EVP_PKEY_SIPHASH)
check_cxx_source_compiles("
#include <openssl/evp.h>
#include <openssl/ec.h>
int main() { EVP_PKEY *p = EVP_EC_gen(\"P-256\"); EVP_PKEY_free(p); return 0; }"
  I2PD_HAVE_EVP_EC_GEN)
# OpenSSL 4.0 deprecates SSL_set1_host in favour of SSL_set1_dnsname.
# LibreSSL keeps the older API, so select the replacement by capability.
check_cxx_source_compiles("
#include <openssl/ssl.h>
int main() { SSL *s = nullptr; return s ? SSL_set1_dnsname(s, \"example.org\") + 1 : 0; }"
  I2PD_HAVE_SSL_SET1_DNSNAME)
# OpenSSL 3.x and LibreSSL provide SSL_set1_host; a future release that drops
# both hostname-verification setters must fail configure, never build a TLS
# client that cannot verify the reseed host.
check_cxx_source_compiles("
#include <openssl/ssl.h>
int main() { SSL *s = nullptr; return s ? SSL_set1_host(s, \"example.org\") + 1 : 0; }"
  I2PD_HAVE_SSL_SET1_HOST)
# LibreSSL 4.3+ exposes ML-KEM through a separate header. Probe it instead of
# assuming that every 4.3 port ships the header and symbols.
check_cxx_source_compiles("
#include <openssl/mlkem.h>
int main() { return 0; }"
  I2PD_HAVE_OPENSSL_MLKEM_H)
if(_i2pd_saved_try_compile_target_type)
  set(CMAKE_TRY_COMPILE_TARGET_TYPE "${_i2pd_saved_try_compile_target_type}")
else()
  unset(CMAKE_TRY_COMPILE_TARGET_TYPE)
endif()
set(CMAKE_REQUIRED_INCLUDES "${_i2pd_saved_required_includes}")
set(CMAKE_REQUIRED_LIBRARIES "${_i2pd_saved_required_libraries}")
unset(_i2pd_saved_try_compile_target_type)
unset(_i2pd_saved_required_includes)
unset(_i2pd_saved_required_libraries)
# Compile-only probes cannot detect a header/library prefix mismatch. One
# combined link probe references the required surface for the selected
# backend and fails at configure time if the library cannot supply it.
set(CMAKE_REQUIRED_INCLUDES "${OPENSSL_INCLUDE_DIR}")
# Use the exact imported file paths rather than the targets so try_compile
# exercises the same libssl/libcrypto selected for the build.
set(CMAKE_REQUIRED_LIBRARIES "${OPENSSL_CRYPTO_LIBRARY};${OPENSSL_SSL_LIBRARY}")
set(_i2pd_link_saved_try_type "${CMAKE_TRY_COMPILE_TARGET_TYPE}")
set(CMAKE_TRY_COMPILE_TARGET_TYPE EXECUTABLE)
if(I2PD_OPENSSL_KIND STREQUAL "OpenSSL")
  check_cxx_source_compiles("
#include <openssl/ssl.h>
#include <openssl/asn1.h>
#include <openssl/evp.h>
#include <openssl/bn.h>
#include <openssl/kdf.h>
int main() {
  const SSL_METHOD *m = TLS_method(); (void)m;
  ASN1_STRING *s = ASN1_STRING_new(); ASN1_STRING_free(s);
  EVP_PKEY *p = nullptr; char g[80]; size_t n = 0; BIGNUM *b = nullptr;
  (void)EVP_PKEY_is_a(p, \"EC\"); (void)EVP_PKEY_get_group_name(p, g, sizeof(g), &n);
  (void)EVP_PKEY_get_bn_param(p, \"n\", &b); BN_free(b);
  EVP_MD *md = EVP_MD_fetch(nullptr, \"SHA256\", nullptr); EVP_MD_free(md);
  EVP_CIPHER *c = EVP_CIPHER_fetch(nullptr, \"AES-256-CBC\", nullptr); EVP_CIPHER_free(c);
  EVP_KDF *k = EVP_KDF_fetch(nullptr, \"HKDF\", nullptr); EVP_KDF_free(k);
  return 0;
}"
    I2PD_OPENSSL_LINK_OK)
else()
  check_cxx_source_compiles("
#include <openssl/ssl.h>
#include <openssl/asn1.h>
int main() {
  const SSL_METHOD *m = TLS_method(); (void)m;
  ASN1_STRING *s = ASN1_STRING_new(); ASN1_STRING_free(s);
  return 0;
}"
    I2PD_OPENSSL_LINK_OK)
endif()
set(CMAKE_REQUIRED_INCLUDES "${_i2pd_saved_required_includes}")
set(CMAKE_REQUIRED_LIBRARIES "${_i2pd_saved_required_libraries}")
if(_i2pd_link_saved_try_type)
  set(CMAKE_TRY_COMPILE_TARGET_TYPE "${_i2pd_link_saved_try_type}")
else()
  unset(CMAKE_TRY_COMPILE_TARGET_TYPE)
endif()
unset(_i2pd_link_saved_try_type)
if(NOT I2PD_OPENSSL_LINK_OK)
  message(FATAL_ERROR
    "The selected OpenSSL headers and libraries do not provide one linked modern API surface")
endif()
if(NOT I2PD_HAVE_TLS_METHOD)
  message(FATAL_ERROR "TLS_method() is required; fixed TLS/DTLS version methods were removed in OpenSSL 4.0")
endif()
if(NOT I2PD_HAVE_ASN1_STRING_GET0_DATA)
  message(FATAL_ERROR "ASN1_STRING_get0_data()/ASN1_STRING_length() are required")
endif()
if(NOT I2PD_HAVE_SSL_SET1_DNSNAME AND NOT I2PD_HAVE_SSL_SET1_HOST)
  message(FATAL_ERROR
    "Hostname verification requires SSL_set1_dnsname() or SSL_set1_host(); neither is available")
endif()
set(_i2pd_required_openssl_caps
  I2PD_HAVE_EVP_PKEY_IS_A
  I2PD_HAVE_EVP_PKEY_GROUP_NAME
  I2PD_HAVE_EVP_PKEY_BN_PARAM
  I2PD_HAVE_EVP_MD_FETCH
  I2PD_HAVE_EVP_CIPHER_FETCH
  I2PD_HAVE_EVP_KDF_FETCH
  I2PD_HAVE_EVP_EC_GEN)
foreach(_i2pd_required IN LISTS _i2pd_required_openssl_caps)
  if(NOT "${${_i2pd_required}}")
    if(I2PD_OPENSSL_KIND STREQUAL "OpenSSL")
      # OpenSSL 3.0.2+ must provide the provider/API surface used by the
      # modern branches. A false probe must not fall back to deprecated
      # low-level APIs or OpenSSL 4 removed compatibility code.
      message(FATAL_ERROR "Required OpenSSL capability ${_i2pd_required} is unavailable")
    endif()
    message(STATUS "LibreSSL feature probe ${_i2pd_required}: unavailable; compatibility branch enabled")
  endif()
endforeach()
unset(_i2pd_required_openssl_caps)
if(NOT I2PD_HAVE_EVP_PKEY_SIPHASH)
  message(STATUS "Libcrypto SipHash is unavailable; using the portable project fallback")
endif()
unset(_i2pd_required)
find_package(ZLIB 1.2.11 REQUIRED)

if(WITH_UPNP)
  find_package(MiniUPnPc 2.2.6 REQUIRED)
endif()

if(WITH_STATIC)
  # Dependency discovery saw only static archives; restore the normal search
  # suffixes for everything built afterwards (tools, tests, TUI, consumers).
  set(CMAKE_FIND_LIBRARY_SUFFIXES "${_i2pd_saved_find_library_suffixes}")
  unset(_i2pd_saved_find_library_suffixes)
endif()

set(LIBI2PD_DIR "${PROJECT_ROOT}/libi2pd")
set(LIBI2PD_CLIENT_DIR "${PROJECT_ROOT}/libi2pd_client")
set(LANG_DIR "${PROJECT_ROOT}/i18n")
set(DAEMON_DIR "${PROJECT_ROOT}/daemon")
set(TUI_DIR "${PROJECT_ROOT}/tui")

# Explicit source lists: a configure-time glob silently changed the ABI and
# build graph whenever a file was added or removed.
set(LIBI2PD_SOURCES
  "${LIBI2PD_DIR}/api.cpp"
  "${LIBI2PD_DIR}/Base.cpp"
  "${LIBI2PD_DIR}/Blinding.cpp"
  "${LIBI2PD_DIR}/Blocklist.cpp"
  "${LIBI2PD_DIR}/Config.cpp"
  "${LIBI2PD_DIR}/Crypto.cpp"
  "${LIBI2PD_DIR}/CryptoKey.cpp"
  "${LIBI2PD_DIR}/Datagram.cpp"
  "${LIBI2PD_DIR}/Destination.cpp"
  "${LIBI2PD_DIR}/ECIESX25519AEADRatchetSession.cpp"
  "${LIBI2PD_DIR}/Ed25519.cpp"
  "${LIBI2PD_DIR}/Elligator.cpp"
  "${LIBI2PD_DIR}/Family.cpp"
  "${LIBI2PD_DIR}/FS.cpp"
  "${LIBI2PD_DIR}/Garlic.cpp"
  "${LIBI2PD_DIR}/Gost.cpp"
  "${LIBI2PD_DIR}/Gzip.cpp"
  "${LIBI2PD_DIR}/HTTP.cpp"
  "${LIBI2PD_DIR}/I2NPProtocol.cpp"
  "${LIBI2PD_DIR}/Identity.cpp"
  "${LIBI2PD_DIR}/IdentMetrics.cpp"
  "${LIBI2PD_DIR}/KadDHT.cpp"
  "${LIBI2PD_DIR}/LeaseSet.cpp"
  "${LIBI2PD_DIR}/Log.cpp"
  "${LIBI2PD_DIR}/NetDb.cpp"
  "${LIBI2PD_DIR}/NetDbRequests.cpp"
  "${LIBI2PD_DIR}/NTCP2.cpp"
  "${LIBI2PD_DIR}/PostQuantum.cpp"
  "${LIBI2PD_DIR}/Profiling.cpp"
  "${LIBI2PD_DIR}/Reseed.cpp"
  "${LIBI2PD_DIR}/RouterContext.cpp"
  "${LIBI2PD_DIR}/PersistentStorage.cpp"
  "${LIBI2PD_DIR}/RouterInfo.cpp"
  "${LIBI2PD_DIR}/Signature.cpp"
  "${LIBI2PD_DIR}/SSU2.cpp"
  "${LIBI2PD_DIR}/SSU2OutOfSession.cpp"
  "${LIBI2PD_DIR}/SSU2Session.cpp"
  "${LIBI2PD_DIR}/Streaming.cpp"
  "${LIBI2PD_DIR}/Timestamp.cpp"
  "${LIBI2PD_DIR}/TransitTunnel.cpp"
  "${LIBI2PD_DIR}/Transports.cpp"
  "${LIBI2PD_DIR}/TunnelBase.cpp"
  "${LIBI2PD_DIR}/TunnelConfig.cpp"
  "${LIBI2PD_DIR}/Tunnel.cpp"
  "${LIBI2PD_DIR}/TunnelEndpoint.cpp"
  "${LIBI2PD_DIR}/TunnelGateway.cpp"
  "${LIBI2PD_DIR}/TunnelPool.cpp"
  "${LIBI2PD_DIR}/util.cpp"
)

set(LIBI2PD_CLIENT_SOURCES
  "${LIBI2PD_CLIENT_DIR}/AddressBook.cpp"
  "${LIBI2PD_CLIENT_DIR}/BOB.cpp"
  "${LIBI2PD_CLIENT_DIR}/ClientContext.cpp"
  "${LIBI2PD_CLIENT_DIR}/HTTPProxy.cpp"
  "${LIBI2PD_CLIENT_DIR}/I2CP.cpp"
  "${LIBI2PD_CLIENT_DIR}/I2PService.cpp"
  "${LIBI2PD_CLIENT_DIR}/I2PTunnel.cpp"
  "${LIBI2PD_CLIENT_DIR}/I2PTunnelAccessFilter.cpp"
  "${LIBI2PD_CLIENT_DIR}/MatchedDestination.cpp"
  "${LIBI2PD_CLIENT_DIR}/SAM.cpp"
  "${LIBI2PD_CLIENT_DIR}/SOCKS.cpp"
  "${LIBI2PD_CLIENT_DIR}/Torrents.cpp"
  "${LIBI2PD_CLIENT_DIR}/TorrentsControl.cpp"
  "${LIBI2PD_CLIENT_DIR}/TorrentsComments.cpp"
  "${LIBI2PD_CLIENT_DIR}/TorrentsCreator.cpp"
  "${LIBI2PD_CLIENT_DIR}/TorrentStorageWorker.cpp"
  "${LIBI2PD_CLIENT_DIR}/TorrentsState.cpp"
  "${LIBI2PD_CLIENT_DIR}/TorrentsDHT.cpp"
  "${LIBI2PD_CLIENT_DIR}/TorrentsRPC.cpp"
  "${LIBI2PD_CLIENT_DIR}/TorrentsTunnel.cpp"
  "${LIBI2PD_CLIENT_DIR}/UDPTunnel.cpp"
)

set(LANG_SOURCES
  "${LANG_DIR}/I18N.cpp"
  "${LANG_DIR}/English.cpp"
  "${LANG_DIR}/Spanish.cpp"
  "${LANG_DIR}/Russian.cpp"
  "${LANG_DIR}/ChineseSimplified.cpp"
)

# Daemon sources shared by the host executable and the Android JNI library.
# Snapshot collection and request dispatch are shared by both local
# transports; only the listener/peer-authorization layer is platform specific.
set(DAEMON_COMMON_SOURCES
  "${DAEMON_DIR}/Daemon.cpp"
  "${DAEMON_DIR}/I2PControl.cpp"
  "${DAEMON_DIR}/I2PControlHandlers.cpp"
  "${DAEMON_DIR}/UPnP.cpp"
  "${DAEMON_DIR}/TuiServerCommon.cpp"
)
set(DAEMON_SOURCES
  ${DAEMON_COMMON_SOURCES}
  "${DAEMON_DIR}/i2pd.cpp"
)
set(PRIVSEP_SOURCES)

if(UNIX)
  list(APPEND DAEMON_SOURCES
    "${DAEMON_DIR}/TuiServer.cpp"
    "${DAEMON_DIR}/UnixDaemon.cpp"
  )
  list(APPEND PRIVSEP_SOURCES
    "${PROJECT_ROOT}/privsep/Channel.cpp"
    "${PROJECT_ROOT}/privsep/Credentials.cpp"
    "${PROJECT_ROOT}/privsep/DataStorage.cpp"
    "${PROJECT_ROOT}/privsep/LinuxSandbox.cpp"
    "${PROJECT_ROOT}/privsep/OpenBSDImsg.cpp"
    "${PROJECT_ROOT}/privsep/OpenBSDSandbox.cpp"
    "${PROJECT_ROOT}/privsep/LogWorker.cpp"
    "${PROJECT_ROOT}/privsep/RunWorker.cpp"
    "${PROJECT_ROOT}/privsep/TorrentStorage.cpp"
    "${PROJECT_ROOT}/privsep/RouterStorage.cpp"
    "${PROJECT_ROOT}/privsep/Supervisor.cpp"
    "${PROJECT_ROOT}/privsep/Worker.cpp"
  )
endif()

if(WIN32)
  list(APPEND DAEMON_SOURCES
    "${DAEMON_DIR}/TuiServerWin32.cpp"
    "${TUI_DIR}/TuiWin32Ipc.cpp"
    "${PROJECT_ROOT}/Win32/DaemonWin32.cpp"
    "${PROJECT_ROOT}/Win32/Win32App.cpp"
    "${PROJECT_ROOT}/Win32/Win32Service.cpp"
    "${PROJECT_ROOT}/Win32/Win32NetState.cpp"
  )
  list(APPEND PRIVSEP_SOURCES
    "${PROJECT_ROOT}/privsep/WindowsIpc.cpp"
    "${PROJECT_ROOT}/privsep/WindowsIsolation.cpp"
    "${PROJECT_ROOT}/privsep/WindowsSupervisor.cpp"
  )
  if(CMAKE_RC_COMPILER)
    list(APPEND DAEMON_SOURCES "${PROJECT_ROOT}/Win32/Resource.rc")
    set_source_files_properties("${PROJECT_ROOT}/Win32/Resource.rc" PROPERTIES
      OBJECT_DEPENDS "${PROJECT_ROOT}/Win32/openi2pd.ico")
  endif()
endif()

# Common compiler diagnostics. These are not global flags: each target gets
# the same interface plus its own definitions.
add_library(i2pd_warnings INTERFACE)
if(MSVC)
  target_compile_options(i2pd_warnings INTERFACE /W4 /permissive- /Zc:__cplusplus /utf-8)
  if(WITH_HARDENING)
    # The Unix hardening block below is not equivalent on MSVC; do not accept
    # the option silently and leave the binary uninstrumented by accident.
    message(WARNING
      "WITH_HARDENING is not implemented for MSVC; configure without it or add explicit MSVC linker flags")
  endif()
else()
  target_compile_options(i2pd_warnings INTERFACE
    -Wall -Wextra -Wpedantic -Wno-unused-parameter -Wno-uninitialized
  )
  if(WITH_HARDENING)
    target_compile_options(i2pd_warnings INTERFACE
      -fstack-protector-strong -Wformat -Wformat-security -Werror=format-security
    )
    target_compile_definitions(i2pd_warnings INTERFACE _FORTIFY_SOURCE=2)
    if(WIN32)
      # MinGW/Clang64 produce PE images; ELF -z flags are not accepted here.
      target_link_options(i2pd_warnings INTERFACE -Wl,--dynamicbase -Wl,--nxcompat)
    else()
      target_link_options(i2pd_warnings INTERFACE -Wl,-z,relro -Wl,-z,now)
    endif()
  endif()
endif()

function(i2pd_enable_sanitizers target)
  # The options are documented as Unix/GCC/Clang only. MSVC/clang-cl and the
  # MinGW runtimes use different sanitizer support; ignore the flags rather
  # than emitting an invalid or unsupported command line.
  if(WIN32)
    if(WITH_ADDRSANITIZER OR WITH_THREADSANITIZER OR WITH_UNDEFSANITIZER)
      message(FATAL_ERROR
        "Sanitizer options are not supported on this Windows toolchain; "
        "refusing to configure an uninstrumented build.")
    endif()
    return()
  endif()
  if(WITH_ADDRSANITIZER)
    target_compile_options(${target} PRIVATE -fsanitize=address -fno-omit-frame-pointer)
    target_link_options(${target} PRIVATE -fsanitize=address)
  endif()
  if(WITH_THREADSANITIZER)
    if(WITH_ADDRSANITIZER)
      message(FATAL_ERROR "ThreadSanitizer cannot be combined with AddressSanitizer")
    endif()
    target_compile_options(${target} PRIVATE -fsanitize=thread -fno-omit-frame-pointer)
    target_link_options(${target} PRIVATE -fsanitize=thread)
  endif()
  if(WITH_UNDEFSANITIZER)
    target_compile_options(${target} PRIVATE -fsanitize=undefined -fno-sanitize-recover=all)
    target_link_options(${target} PRIVATE -fsanitize=undefined)
  endif()
endfunction()

# Capability macros shared by libi2pd and i2pd_privsep. Define them before the
# privsep target (which is optional/Android-disabled) so libi2pd consumers keep
# the same provider branches on every platform.
set(I2PD_CAPABILITY_DEFINITIONS
    I2PD_HAVE_TLS_METHOD=$<BOOL:${I2PD_HAVE_TLS_METHOD}>
    I2PD_HAVE_ASN1_STRING_GET0_DATA=$<BOOL:${I2PD_HAVE_ASN1_STRING_GET0_DATA}>
    I2PD_HAVE_EVP_PKEY_IS_A=$<BOOL:${I2PD_HAVE_EVP_PKEY_IS_A}>
    I2PD_HAVE_EVP_PKEY_GROUP_NAME=$<BOOL:${I2PD_HAVE_EVP_PKEY_GROUP_NAME}>
    I2PD_HAVE_EVP_PKEY_BN_PARAM=$<BOOL:${I2PD_HAVE_EVP_PKEY_BN_PARAM}>
    I2PD_HAVE_EVP_MD_FETCH=$<BOOL:${I2PD_HAVE_EVP_MD_FETCH}>
    I2PD_HAVE_EVP_CIPHER_FETCH=$<BOOL:${I2PD_HAVE_EVP_CIPHER_FETCH}>
    I2PD_HAVE_EVP_KDF_FETCH=$<BOOL:${I2PD_HAVE_EVP_KDF_FETCH}>
    I2PD_HAVE_EVP_PKEY_SIPHASH=$<BOOL:${I2PD_HAVE_EVP_PKEY_SIPHASH}>
    I2PD_HAVE_EVP_EC_GEN=$<BOOL:${I2PD_HAVE_EVP_EC_GEN}>
    I2PD_HAVE_SSL_SET1_DNSNAME=$<BOOL:${I2PD_HAVE_SSL_SET1_DNSNAME}>
    I2PD_HAVE_SSL_SET1_HOST=$<BOOL:${I2PD_HAVE_SSL_SET1_HOST}>
    I2PD_HAVE_OPENSSL_MLKEM_H=$<BOOL:${I2PD_HAVE_OPENSSL_MLKEM_H}>
    BOOST_ASIO_HAS_SNPRINTF=1
  )

# One compile contract for every native target. Public headers expose the
# capability macros and Boost.Asio, so consumers and the export see exactly
# the same definitions as the libraries.
function(i2pd_apply_dialect target visibility)
  target_compile_definitions(${target} ${visibility} ${I2PD_CAPABILITY_DEFINITIONS})
  if(WIN32)
    target_compile_definitions(${target} ${visibility}
      WIN32_LEAN_AND_MEAN NOMINMAX WINVER=0x0A00 _WIN32_WINNT=0x0A00)
  endif()
endfunction()

if(PRIVSEP_SOURCES AND NOT ANDROID)
  # Keep the native IPC/supervisor code out of the core library and daemon
  # source list. This gives the component one explicit ownership boundary and
  # lets tests or a future supervisor link it independently.
  add_library(i2pd_privsep STATIC ${PRIVSEP_SOURCES})
  set_target_properties(i2pd_privsep PROPERTIES PREFIX "" POSITION_INDEPENDENT_CODE ON)
  target_compile_features(i2pd_privsep PRIVATE cxx_std_23)
  # Capability macro parity with libi2pd: a future privsep header that includes
  # an Asio/OpenSSL/Log header must see the same provider branches.
  i2pd_apply_dialect(i2pd_privsep PRIVATE)
  # The exported archive needs a stable include interface.
  # Expected.h lives in libi2pd and is shared by the IPC targets.
  target_include_directories(i2pd_privsep
    PUBLIC
      $<BUILD_INTERFACE:${PROJECT_ROOT}>
      $<BUILD_INTERFACE:${LIBI2PD_DIR}>
      $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
      $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/libi2pd>
    PRIVATE
      "${DAEMON_DIR}"
      "${LANG_DIR}"
  )
  target_link_libraries(i2pd_privsep PRIVATE Threads::Threads ${I2PD_EXTRA_LIBS} $<BUILD_INTERFACE:i2pd_warnings>)
  if(CMAKE_SYSTEM_NAME STREQUAL "OpenBSD")
    # OpenBSD imsg lives in libutil. 8.0 exposes imsgbuf_get(); older bases use
    # imsg_get(). The configure probe selects the available name.
    target_link_libraries(i2pd_privsep PUBLIC util)
    if(I2PD_HAVE_IMSGBUF_GET)
      target_compile_definitions(i2pd_privsep PRIVATE I2PD_HAVE_IMSGBUF_GET=1)
    endif()
  elseif(WIN32)
    # CreateRestrictedToken/CreateProcessAsUser, pipe/named-object security and
    # the DACL grant APIs (advapi32), AppContainer profile creation and SID
    # derivation (userenv) and the per-spawn pipe-name CSPRNG (bcrypt). The SDK
    # macros come from i2pd_apply_dialect() above.
    target_link_libraries(i2pd_privsep PUBLIC advapi32 userenv bcrypt)
  endif()
  i2pd_enable_sanitizers(i2pd_privsep)
endif()

add_library(libi2pd STATIC ${LIBI2PD_SOURCES})
set_target_properties(libi2pd PROPERTIES PREFIX "" POSITION_INDEPENDENT_CODE ON)
target_include_directories(libi2pd
  PUBLIC
    $<BUILD_INTERFACE:${LIBI2PD_DIR}>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/libi2pd>
  PRIVATE
    ${LANG_DIR}
)
target_compile_features(libi2pd PUBLIC cxx_std_23)
# Feature probes drive the OpenSSL/LibreSSL branches. Numeric versions remain
# only for the documented source-compatibility floor and ML-KEM API presence.
# i2pd_privsep and the TUI receive the same capability definitions.
i2pd_apply_dialect(libi2pd PUBLIC)
target_link_libraries(libi2pd
  PUBLIC
    OpenSSL::SSL OpenSSL::Crypto ZLIB::ZLIB
    Boost::headers Boost::program_options Threads::Threads ${I2PD_EXTRA_LIBS}
)
target_link_libraries(libi2pd PRIVATE $<BUILD_INTERFACE:i2pd_warnings>)
i2pd_enable_sanitizers(libi2pd)
# The supervisor/worker archive is installed for direct consumers and uses the
# core logging/framing helpers, so the dependency must travel with it.
if(TARGET i2pd_privsep)
  target_link_libraries(i2pd_privsep PUBLIC libi2pd)
endif()
if(WITH_UPNP)
  target_compile_definitions(libi2pd PRIVATE USE_UPNP)
endif()
if(WIN32)
  target_compile_definitions(libi2pd PRIVATE WIN32_APP)
  # One Windows import-library contract for the library and every static
  # consumer: Asio/AcceptEx (ws2_32/mswsock), adapter enumeration (iphlpapi),
  # SHGetFolderPathW (shell32) and private-file DACLs (advapi32). The macros
  # themselves come from i2pd_apply_dialect().
  target_link_libraries(libi2pd PUBLIC ws2_32 mswsock iphlpapi shell32 advapi32)
endif()
if(ANDROID)
  target_compile_definitions(libi2pd PUBLIC ANDROID)
endif()

add_library(libi2pdclient STATIC ${LIBI2PD_CLIENT_SOURCES})
set_target_properties(libi2pdclient PROPERTIES PREFIX "" POSITION_INDEPENDENT_CODE ON)
target_include_directories(libi2pdclient
  PUBLIC
    $<BUILD_INTERFACE:${LIBI2PD_CLIENT_DIR}>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/libi2pdclient>
  PRIVATE
    ${LANG_DIR}
)
target_compile_features(libi2pdclient PUBLIC cxx_std_23)
target_link_libraries(libi2pdclient PUBLIC libi2pd Threads::Threads)
if(UNIX AND TARGET i2pd_privsep)
  target_link_libraries(libi2pdclient PRIVATE i2pd_privsep)
endif()
target_link_libraries(libi2pdclient PRIVATE $<BUILD_INTERFACE:i2pd_warnings>)
i2pd_enable_sanitizers(libi2pdclient)
if(NOT WITH_TORRENTS)
  target_compile_definitions(libi2pdclient PUBLIC NO_TORRENTS)
else()
  target_link_libraries(libi2pdclient PRIVATE Boost::json Boost::url)
  if(TARGET Boost::container)
    target_link_libraries(libi2pdclient PRIVATE Boost::container)
  endif()
endif()

add_library(libi2pdlang STATIC ${LANG_SOURCES})
set_target_properties(libi2pdlang PROPERTIES PREFIX "" POSITION_INDEPENDENT_CODE ON)
target_include_directories(libi2pdlang
  PUBLIC
    $<BUILD_INTERFACE:${LANG_DIR}>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/libi2pdlang>
  PRIVATE
    ${LIBI2PD_DIR}
)
target_compile_features(libi2pdlang PUBLIC cxx_std_23)
target_link_libraries(libi2pdlang PRIVATE $<BUILD_INTERFACE:i2pd_warnings>)
i2pd_enable_sanitizers(libi2pdlang)
# The language registry is process-local and intentionally independent of the
# client context, so the TUI and tools can translate without linking the router.
target_link_libraries(libi2pdclient PUBLIC libi2pdlang)

if(I2PD_BUILD_JNI AND ANDROID)
  # The JNI library owns the in-process daemon. Daemon.cpp is the common
  # daemon lifecycle implementation used by DaemonAndroid; UPnP.cpp and the
  # local TUI/I2PControl services are linked here as well. Android deliberately
  # does not use the privilege-separated log supervisor, so I2PD_NO_PRIVSEP
  # removes that POSIX-only path while leaving the rest of the daemon intact.
  add_library(i2pd_jni SHARED
    "${PROJECT_ROOT}/android/app/src/main/cpp/DaemonAndroid.cpp"
    "${PROJECT_ROOT}/android/app/src/main/cpp/i2pd_android.cpp"
    ${DAEMON_COMMON_SOURCES}
  )
  set_target_properties(i2pd_jni PROPERTIES
    OUTPUT_NAME i2pd PREFIX "lib"
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON)
  target_compile_features(i2pd_jni PRIVATE cxx_std_23)
  target_include_directories(i2pd_jni PRIVATE
    "${PROJECT_ROOT}/android/app/src/main/cpp" ${DAEMON_DIR} ${LANG_DIR} ${TUI_DIR})
  target_compile_definitions(i2pd_jni PRIVATE ANDROID I2PD_NO_PRIVSEP)
  if(WITH_UPNP)
    target_compile_definitions(i2pd_jni PRIVATE USE_UPNP)
    target_link_libraries(i2pd_jni PRIVATE MiniUPnPc::MiniUPnPc)
  endif()
  target_link_libraries(i2pd_jni PRIVATE libi2pd libi2pdclient libi2pdlang log i2pd_warnings)
  # Only the Java_* entry points are part of the JNI ABI. Keep static-library
  # internals out of the dynamic symbol table.
  target_link_options(i2pd_jni PRIVATE -Wl,--exclude-libs,ALL)
  i2pd_enable_sanitizers(i2pd_jni)
  if(WITH_LIBRARY)
    install(TARGETS i2pd_jni LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT Libraries)
  endif()
elseif(WITH_BINARY)
  if(WIN32)
    add_executable(i2pd WIN32 ${DAEMON_SOURCES})
  else()
    add_executable(i2pd ${DAEMON_SOURCES})
  endif()
  target_compile_features(i2pd PRIVATE cxx_std_23)
  target_include_directories(i2pd PRIVATE
    ${PROJECT_ROOT} ${LIBI2PD_DIR} ${DAEMON_DIR} ${LANG_DIR} ${TUI_DIR})
  target_link_libraries(i2pd PRIVATE libi2pd libi2pdclient libi2pdlang i2pd_warnings)
  if(TARGET i2pd_privsep)
    target_link_libraries(i2pd PRIVATE i2pd_privsep)
  endif()
  if(WITH_UPNP)
    target_compile_definitions(i2pd PRIVATE USE_UPNP)
    target_link_libraries(i2pd PRIVATE MiniUPnPc::MiniUPnPc)
  endif()
  if(WIN32)
    # WIN32_LEAN_AND_MEAN/NOMINMAX/WINVER come from libi2pd's public dialect
    # contract; only the GUI subsystem switch is private to this executable.
    target_compile_definitions(i2pd PRIVATE WIN32_APP)
    # ws2_32/mswsock/iphlpapi/shell32/advapi32 are PUBLIC on libi2pd. Only
    # the GUI/COM libraries used directly by Win32App/NetState are added here.
    target_link_libraries(i2pd PRIVATE user32 ole32 uuid)
  endif()
  if(WITH_STATIC AND NOT MSVC)
    target_link_options(i2pd PRIVATE -static)
  endif()
  i2pd_enable_sanitizers(i2pd)
endif()

# The TUI is a separate process and never links the router libraries. It links
# only the dependency-free language catalog. Both backends share main.cpp,
# Theme.h and TuiProtocol.h; only the terminal and transport implementations
# differ.
if(I2PD_BUILD_TUI)
  if(UNIX)
    # CMake's wide-character search only accepts the ncursesw/ header layout.
    # OpenBSD's base ncurses provides wide-character support but installs
    # ncurses.h directly, so retry the plain header layout and verify that the
    # discovered library really exports wide-character entry points.
    set(CURSES_NEED_WIDE TRUE)
    find_package(Curses QUIET)
    if(NOT CURSES_FOUND)
      # FindCurses caches the header/library results. A second attempt with a
      # different header layout must not reuse the failed cached values.
      unset(CURSES_INCLUDE_PATH CACHE)
      unset(CURSES_INCLUDE_DIR CACHE)
      unset(CURSES_LIBRARY CACHE)
      unset(CURSES_CURSES_LIBRARY CACHE)
      unset(CURSES_NCURSES_LIBRARY CACHE)
      unset(CURSES_EXTRA_LIBRARY CACHE)
      unset(CURSES_NEED_WIDE)
      set(CURSES_NEED_NCURSES TRUE)
      find_package(Curses QUIET)
      if(CURSES_FOUND)
        include(CheckLibraryExists)
        list(GET CURSES_LIBRARIES 0 _i2pd_curses_probe)
        check_library_exists("${_i2pd_curses_probe}" wadd_wch "" I2PD_CURSES_HAS_WIDE_ENTRY)
        unset(_i2pd_curses_probe)
        if(NOT I2PD_CURSES_HAS_WIDE_ENTRY)
          set(CURSES_FOUND FALSE)
        endif()
      endif()
    endif()
    unset(CURSES_NEED_WIDE)
    unset(CURSES_NEED_NCURSES)
    if(I2PD_TUI_REQUIRED AND NOT CURSES_FOUND)
      message(FATAL_ERROR
        "The TUI requires an ncursesw-capable library; install ncursesw or configure with -DI2PD_TUI_REQUIRED=OFF")
    endif()
    if(CURSES_FOUND)
      add_executable(i2pd-tui
        "${TUI_DIR}/main.cpp"
        "${TUI_DIR}/TuiClient.cpp"
        "${TUI_DIR}/TerminalUnix.cpp"
      )
      target_compile_features(i2pd-tui PRIVATE cxx_std_23)
      target_include_directories(i2pd-tui PRIVATE
        "${LIBI2PD_DIR}" "${LANG_DIR}" "${TUI_DIR}" ${CURSES_INCLUDE_DIRS})
      i2pd_apply_dialect(i2pd-tui PRIVATE)
      target_link_libraries(i2pd-tui PRIVATE
        ${CURSES_LIBRARIES} Boost::headers Threads::Threads ${I2PD_EXTRA_LIBS} libi2pdlang i2pd_warnings)
      i2pd_enable_sanitizers(i2pd-tui)
    else()
      message(WARNING "ncursesw not found: building without i2pd-tui")
    endif()
  elseif(WIN32)
    # Win32 console/VT backend and named-pipe client. There is deliberately no
    # Curses/PDCurses dependency on Windows.
    add_executable(i2pd-tui
      "${TUI_DIR}/main.cpp"
      "${TUI_DIR}/TuiClientWin32.cpp"
      "${TUI_DIR}/TerminalWin32.cpp"
      "${TUI_DIR}/TuiWin32Ipc.cpp"
    )
    target_compile_features(i2pd-tui PRIVATE cxx_std_23)
    target_include_directories(i2pd-tui PRIVATE
      "${LIBI2PD_DIR}" "${LANG_DIR}" "${TUI_DIR}")
    i2pd_apply_dialect(i2pd-tui PRIVATE)
    # advapi32: token/SID inspection used for the named-pipe authorization
    # policy. ws2_32 is required by the inline Boost.Asio address types pulled
    # in through Log.h. No shell or Curses library is required.
    target_link_libraries(i2pd-tui PRIVATE
      Boost::headers advapi32 ws2_32 Threads::Threads ${I2PD_EXTRA_LIBS} libi2pdlang i2pd_warnings)
    i2pd_enable_sanitizers(i2pd-tui)
  endif()
endif()

# Installation
if(WITH_LIBRARY)
  if(UNIX AND TARGET i2pd_privsep)
    install(TARGETS i2pd_privsep EXPORT i2pd-targets
      ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT Libraries)
  endif()
  install(TARGETS libi2pd libi2pdclient libi2pdlang
    EXPORT i2pd-targets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    COMPONENT Libraries
  )
  install(DIRECTORY "${LIBI2PD_DIR}/" DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/libi2pd
    COMPONENT Development
    FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp")
  install(DIRECTORY "${LIBI2PD_CLIENT_DIR}/" DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/libi2pdclient
    COMPONENT Development
    FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp")
  install(DIRECTORY "${LANG_DIR}/" DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/libi2pdlang
    COMPONENT Development
    FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp")
  if(UNIX AND TARGET i2pd_privsep)
    # The archive is part of i2pd-targets because static libi2pdclient records
    # its LINK_ONLY edge. Install the matching headers so consumers can use the
    # exported target; the API remains internal and version-unstable.
    install(DIRECTORY "${PROJECT_ROOT}/privsep/" DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/privsep
      COMPONENT Development
      FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp")
  endif()

  include(CMakePackageConfigHelpers)
  # The installed headers are the internal router libraries, not a stable
  # public ABI; an exact version avoids promising source compatibility that
  # the project does not maintain between minor releases.
  write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/i2pdConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY ExactVersion)
  configure_package_config_file(
    cmake/i2pdConfig.cmake
    "${CMAKE_CURRENT_BINARY_DIR}/i2pdConfig.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/i2pd
    PATH_VARS CMAKE_INSTALL_INCLUDEDIR)
  install(EXPORT i2pd-targets
    FILE i2pd-targets.cmake
    NAMESPACE i2pd::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/i2pd
    COMPONENT Development)
  install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/i2pdConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/i2pdConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/i2pd
    COMPONENT Development)
endif()

if(WITH_BINARY)
  install(TARGETS i2pd RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT Runtime)
  if(I2PD_INSTALL_CONFIG)
    install(FILES
      "${PROJECT_ROOT}/contrib/i2pd.conf"
      "${PROJECT_ROOT}/contrib/subscriptions.txt"
      "${PROJECT_ROOT}/contrib/tunnels.conf"
      DESTINATION ${CMAKE_INSTALL_SYSCONFDIR}/i2pd
      COMPONENT Runtime)
    install(DIRECTORY "${PROJECT_ROOT}/contrib/tunnels.d/"
      DESTINATION ${CMAKE_INSTALL_SYSCONFDIR}/i2pd/tunnels.conf.d
      COMPONENT Runtime)
  endif()
  install(DIRECTORY "${PROJECT_ROOT}/contrib/certificates/"
    DESTINATION ${CMAKE_INSTALL_DATADIR}/i2pd/certificates
    COMPONENT Runtime)
endif()

# The TUI is a client, not a service. Its desktop launcher and icons are
# installed with the Tui component so a packager can ship i2pd-tui separately
# without adding a second router instance or a second service definition.
if(TARGET i2pd-tui AND UNIX)
  install(FILES "${PROJECT_ROOT}/contrib/openi2pd.desktop"
    DESTINATION ${CMAKE_INSTALL_DATADIR}/applications
    COMPONENT Tui)
  install(FILES "${PROJECT_ROOT}/assets/branding/symbol.svg"
    DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps
    RENAME openi2pd.svg
    COMPONENT Tui)
  install(FILES "${PROJECT_ROOT}/contrib/artwork/openi2pd-256.png"
    DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/256x256/apps
    RENAME openi2pd.png
    COMPONENT Tui)
endif()

if(I2PD_INSTALL_DOCS)
  install(FILES
    "${PROJECT_ROOT}/ChangeLog" "${PROJECT_ROOT}/LICENSE" "${PROJECT_ROOT}/README.md"
    "${PROJECT_ROOT}/NOTICE"
    DESTINATION ${CMAKE_INSTALL_DOCDIR}
    COMPONENT Documentation)
  # The tool licence keeps its upstream text and is renamed so it cannot
  # overwrite the router LICENSE in the same directory.
  install(FILES
    "${PROJECT_ROOT}/tools/LICENSE"
    DESTINATION ${CMAKE_INSTALL_DOCDIR}
    RENAME LICENSE.i2pd-tools
    COMPONENT Documentation)
  install(DIRECTORY "${PROJECT_ROOT}/docs/"
    DESTINATION ${CMAKE_INSTALL_DOCDIR}/docs
    COMPONENT Documentation
    FILES_MATCHING PATTERN "*.md" PATTERN "*.png" PATTERN "*.ico" PATTERN "*.css" PATTERN "*.svg")
  install(DIRECTORY "${PROJECT_ROOT}/assets/branding/"
    DESTINATION ${CMAKE_INSTALL_DOCDIR}/assets/branding
    COMPONENT Documentation)
  install(FILES "${PROJECT_ROOT}/docs/LICENSE"
    DESTINATION ${CMAKE_INSTALL_DOCDIR}/docs
    COMPONENT Documentation)
endif()

# Man pages describe the binary that owns them and must not disappear when a
# packager disables the optional documentation tree (for example the static
# build or the OpenBSD port). Keep the component aligned with the executable.
if(UNIX AND NOT ANDROID)
  if(WITH_BINARY)
    install(FILES "${PROJECT_ROOT}/man/i2pd.1"
      DESTINATION ${CMAKE_INSTALL_MANDIR}/man1
      COMPONENT Runtime)
  endif()
  if(TARGET i2pd-tui)
    install(FILES "${PROJECT_ROOT}/man/i2pd-tui.1"
      DESTINATION ${CMAKE_INSTALL_MANDIR}/man1
      COMPONENT Tui)
  endif()
  if(I2PD_BUILD_TOOLS)
    install(FILES "${PROJECT_ROOT}/man/i2pdctl.1"
      DESTINATION ${CMAKE_INSTALL_MANDIR}/man1
      COMPONENT Tools)
  endif()
endif()

# Exactly one service manager is installed. The executable/config/doc/man and
# TUI install rules above are independent of this selection.
if(I2PD_SERVICE_MANAGER STREQUAL "systemd")
  install(FILES "${PROJECT_ROOT}/contrib/i2pd.service"
    DESTINATION "${_i2pd_service_dir}"
    RENAME i2pd.service
    COMPONENT Service)
  install(FILES "${PROJECT_ROOT}/contrib/sysusers.d/i2pd.conf"
    DESTINATION "lib/sysusers.d"
    RENAME openi2pd.conf
    COMPONENT Service)
  install(FILES "${PROJECT_ROOT}/contrib/tmpfiles.d/i2pd.conf"
    DESTINATION "lib/tmpfiles.d"
    RENAME openi2pd.conf
    COMPONENT Service)
elseif(I2PD_SERVICE_MANAGER STREQUAL "sysvinit")
  install(PROGRAMS "${PROJECT_ROOT}/contrib/sysvinit/i2pd"
    DESTINATION "${_i2pd_service_dir}"
    RENAME i2pd
    COMPONENT Service)
elseif(I2PD_SERVICE_MANAGER STREQUAL "openrc")
  install(PROGRAMS "${PROJECT_ROOT}/contrib/openrc/i2pd.openrc"
    DESTINATION "${_i2pd_service_dir}"
    RENAME i2pd
    COMPONENT Service)
  install(FILES "${PROJECT_ROOT}/contrib/openrc/conf.d/i2pd"
    DESTINATION "${_i2pd_service_confdir}"
    COMPONENT Service)
elseif(I2PD_SERVICE_MANAGER STREQUAL "runit")
  install(PROGRAMS "${PROJECT_ROOT}/contrib/runit/i2pd/run"
    "${PROJECT_ROOT}/contrib/runit/i2pd/finish"
    DESTINATION "${_i2pd_service_dir}/i2pd"
    COMPONENT Service)
  install(PROGRAMS "${PROJECT_ROOT}/contrib/runit/i2pd/log/run"
    DESTINATION "${_i2pd_service_dir}/i2pd/log"
    COMPONENT Service)
  install(FILES "${PROJECT_ROOT}/contrib/runit/i2pd/log/config"
    DESTINATION "${_i2pd_service_dir}/i2pd/log"
    COMPONENT Service)
elseif(I2PD_SERVICE_MANAGER STREQUAL "s6")
  install(PROGRAMS "${PROJECT_ROOT}/contrib/s6/i2pd/run"
    "${PROJECT_ROOT}/contrib/s6/i2pd/finish"
    DESTINATION "${_i2pd_service_dir}/i2pd"
    COMPONENT Service)
  install(FILES "${PROJECT_ROOT}/contrib/s6/i2pd/down-signal"
    "${PROJECT_ROOT}/contrib/s6/i2pd/timeout-kill"
    "${PROJECT_ROOT}/contrib/s6/i2pd/timeout-finish"
    DESTINATION "${_i2pd_service_dir}/i2pd"
    COMPONENT Service)
elseif(I2PD_SERVICE_MANAGER STREQUAL "s6-rc")
  install(PROGRAMS "${PROJECT_ROOT}/contrib/s6-rc/source/i2pd/run"
    "${PROJECT_ROOT}/contrib/s6-rc/source/i2pd/finish"
    DESTINATION "${_i2pd_service_dir}/i2pd"
    COMPONENT Service)
  install(FILES "${PROJECT_ROOT}/contrib/s6-rc/source/i2pd/type"
    "${PROJECT_ROOT}/contrib/s6-rc/source/i2pd/down-signal"
    "${PROJECT_ROOT}/contrib/s6-rc/source/i2pd/timeout-kill"
    "${PROJECT_ROOT}/contrib/s6-rc/source/i2pd/timeout-finish"
    DESTINATION "${_i2pd_service_dir}/i2pd"
    COMPONENT Service)
  install(FILES "${PROJECT_ROOT}/contrib/s6-rc/source/i2pd/dependencies.d/base"
    DESTINATION "${_i2pd_service_dir}/i2pd/dependencies.d"
    COMPONENT Service)
elseif(I2PD_SERVICE_MANAGER STREQUAL "shepherd")
  install(FILES "${PROJECT_ROOT}/contrib/shepherd/i2pd.scm"
    DESTINATION "${_i2pd_service_dir}"
    COMPONENT Service)
endif()

if(TARGET i2pd-tui)
  install(TARGETS i2pd-tui RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT Tui)
endif()

if(I2PD_BUILD_TOOLS)
  add_subdirectory("${PROJECT_ROOT}/tools" "${CMAKE_CURRENT_BINARY_DIR}/tools")
endif()

if(BUILD_TESTING)
  add_subdirectory("${PROJECT_ROOT}/tests" "${CMAKE_CURRENT_BINARY_DIR}/tests")
endif()

message(STATUS "---------------------------------------")
message(STATUS "Build type         : ${CMAKE_BUILD_TYPE}")
message(STATUS "Compiler           : ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "C++ standard       : ${CMAKE_CXX_STANDARD}")
message(STATUS "Install prefix     : ${CMAKE_INSTALL_PREFIX}")
message(STATUS "Build router       : ${WITH_BINARY}")
message(STATUS "Build libraries    : ${WITH_LIBRARY}")
message(STATUS "UPnP               : ${WITH_UPNP}")
message(STATUS "Torrents           : ${WITH_TORRENTS}")
message(STATUS "Tools              : ${I2PD_BUILD_TOOLS}")
message(STATUS "Perl::Critic       : ${I2PD_PERL_CRITIC}")
message(STATUS "Documentation      : ${I2PD_INSTALL_DOCS}")
message(STATUS "Android JNI        : ${I2PD_BUILD_JNI}")
message(STATUS "Service manager    : ${I2PD_SERVICE_MANAGER}")
if(NOT I2PD_SERVICE_MANAGER STREQUAL "none")
  message(STATUS "Service directory  : ${_i2pd_service_dir}")
  if(I2PD_SERVICE_MANAGER STREQUAL "openrc")
    message(STATUS "Service conf.d     : ${_i2pd_service_confdir}")
  endif()
endif()
if(TARGET i2pd_privsep)
  message(STATUS "Workers IPC        : i2pd_privsep")
endif()
if(TARGET i2pd-tui)
  message(STATUS "TUI                : ON (i2pd-tui)")
else()
  message(STATUS "TUI                : OFF")
endif()
message(STATUS "Tests              : ${BUILD_TESTING}")
message(STATUS "Sanitizers         : ASan=${WITH_ADDRSANITIZER} TSan=${WITH_THREADSANITIZER} UBSan=${WITH_UNDEFSANITIZER}")
message(STATUS "---------------------------------------")

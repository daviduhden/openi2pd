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

cmake_minimum_required(VERSION 3.22)

# The project deliberately keeps its CMake entry point in build/ for
# compatibility with existing package recipes.
set(PROJECT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")
set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake_modules")

include(Version)
set_version("${PROJECT_ROOT}/libi2pd/version.h" PROJECT_VERSION)

project(
  OpenI2Pd
  VERSION ${PROJECT_VERSION}
  HOMEPAGE_URL "https://github.com/daviduhden/openi2pd"
  LANGUAGES C CXX
)

if(NOT CMAKE_SYSTEM_NAME MATCHES "^(Windows|Linux|OpenBSD|Android)$")
  message(FATAL_ERROR "openi2pd supports Windows, Linux, OpenBSD and Android")
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
option(I2PD_BUILD_TOOLS     "Build i2pd auxiliary tools"                   ON)
option(I2PD_INSTALL_DOCS   "Install documentation and branding assets"    ON)
option(I2PD_PERL_CRITIC    "Run optional Perl::Critic checks in CTest"    OFF)
option(I2PD_BUILD_JNI       "Build the Android JNI library"                  OFF)
option(WITH_STATIC          "Link the router executable statically"        OFF)
option(WITH_UPNP            "Include support for the UPnP client"          ON)
option(WITH_TORRENTS        "Include the BitTorrent client"                ON)
option(WITH_ADDRSANITIZER   "Build with AddressSanitizer (Unix)"           OFF)
option(WITH_THREADSANITIZER "Build with ThreadSanitizer (Unix)"            OFF)
option(WITH_UNDEFSANITIZER  "Build with UndefinedBehaviorSanitizer (Unix)" OFF)
option(BUILD_TESTING        "Build unit and protocol tests"                OFF)

include(GNUInstallDirs)

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
set(I2PD_SERVICE_CONFDIR "${CMAKE_INSTALL_SYSCONFDIR}/conf.d" CACHE PATH
  "OpenRC conf.d installation directory")

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

# The destination is relative by default, so DESTINATION is interpreted
# against CMAKE_INSTALL_PREFIX. An absolute user override is passed through.
if(I2PD_SERVICE_DIR)
  set(_i2pd_service_dir "${I2PD_SERVICE_DIR}")
elseif(I2PD_SERVICE_MANAGER STREQUAL "systemd")
  set(_i2pd_service_dir "${CMAKE_INSTALL_LIBDIR}/systemd/system")
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
  if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  endif()
  set(CMAKE_FIND_LIBRARY_SUFFIXES ".a")
  set(Boost_USE_STATIC_LIBS ON)
  set(OPENSSL_USE_STATIC_LIBS ON)
  set(ZLIB_USE_STATIC_LIBS ON)
  set(BUILD_SHARED_LIBS OFF)
endif()

include(CheckAtomic)
set(I2PD_EXTRA_LIBS ${CMAKE_REQUIRED_LIBRARIES})
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

find_package(Boost 1.74 REQUIRED COMPONENTS program_options)
if(WITH_TORRENTS)
  find_package(Boost 1.81 REQUIRED COMPONENTS json url)
endif()
find_package(OpenSSL REQUIRED)
if(OPENSSL_VERSION MATCHES "^LibreSSL ([0-9]+\\.[0-9]+)")
  if(CMAKE_MATCH_1 VERSION_LESS 4.0)
    message(FATAL_ERROR "LibreSSL 4.0 or newer is required (found ${OPENSSL_VERSION})")
  endif()
elseif(OPENSSL_VERSION MATCHES "^OpenSSL ([0-9]+\\.[0-9]+\\.[0-9]+)")
  if(CMAKE_MATCH_1 VERSION_LESS 3.0.2)
    message(FATAL_ERROR "OpenSSL 3.0.2 or newer is required (found ${OPENSSL_VERSION})")
  endif()
elseif(OPENSSL_VERSION MATCHES "^([0-9]+\\.[0-9]+\\.[0-9]+)")
  if(CMAKE_MATCH_1 VERSION_LESS 3.0.2)
    message(FATAL_ERROR "OpenSSL 3.0.2 or newer is required (found ${OPENSSL_VERSION})")
  endif()
else()
  message(FATAL_ERROR "Unable to determine OpenSSL/LibreSSL version from '${OPENSSL_VERSION}'")
endif()
find_package(ZLIB 1.2.11 REQUIRED)

if(WITH_UPNP)
  find_package(MiniUPnPc 2.2.6 REQUIRED)
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
  "${LIBI2PD_CLIENT_DIR}/MatchedDestination.cpp"
  "${LIBI2PD_CLIENT_DIR}/SAM.cpp"
  "${LIBI2PD_CLIENT_DIR}/SOCKS.cpp"
  "${LIBI2PD_CLIENT_DIR}/Torrents.cpp"
  "${LIBI2PD_CLIENT_DIR}/TorrentsControl.cpp"
  "${LIBI2PD_CLIENT_DIR}/TorrentsComments.cpp"
  "${LIBI2PD_CLIENT_DIR}/TorrentsCreator.cpp"
  "${LIBI2PD_CLIENT_DIR}/TorrentsState.cpp"
  "${LIBI2PD_CLIENT_DIR}/TorrentsDHT.cpp"
  "${LIBI2PD_CLIENT_DIR}/TorrentsRPC.cpp"
  "${LIBI2PD_CLIENT_DIR}/TorrentsTunnel.cpp"
  "${LIBI2PD_CLIENT_DIR}/UDPTunnel.cpp"
  "${LANG_DIR}/I18N.cpp"
)

set(LANG_SOURCES
  "${LANG_DIR}/English.cpp"
)

set(DAEMON_SOURCES
  "${DAEMON_DIR}/Daemon.cpp"
  "${DAEMON_DIR}/I2PControl.cpp"
  "${DAEMON_DIR}/I2PControlHandlers.cpp"
  "${DAEMON_DIR}/i2pd.cpp"
  "${DAEMON_DIR}/UPnP.cpp"
  # Snapshot collection and request dispatch are shared by both local
  # transports. Only the listener/peer-authorization layer is platform
  # specific.
  "${DAEMON_DIR}/TuiServerCommon.cpp"
)
set(PRIVSEP_SOURCES)

if(UNIX)
  list(APPEND DAEMON_SOURCES
    "${DAEMON_DIR}/TuiServer.cpp"
    "${DAEMON_DIR}/UnixDaemon.cpp"
  )
  list(APPEND PRIVSEP_SOURCES
    "${PROJECT_ROOT}/privsep/Channel.cpp"
    "${PROJECT_ROOT}/privsep/LinuxSandbox.cpp"
    "${PROJECT_ROOT}/privsep/OpenBSDImsg.cpp"
    "${PROJECT_ROOT}/privsep/OpenBSDSandbox.cpp"
    "${PROJECT_ROOT}/privsep/LogWorker.cpp"
    "${PROJECT_ROOT}/privsep/RunWorker.cpp"
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
  )
  list(APPEND DAEMON_SOURCES "${PROJECT_ROOT}/Win32/Resource.rc")
  set_source_files_properties("${PROJECT_ROOT}/Win32/Resource.rc" PROPERTIES
    OBJECT_DEPENDS "${PROJECT_ROOT}/Win32/openi2pd.ico")
endif()

# Common compiler diagnostics. These are not global flags: each target gets
# the same interface plus its own definitions.
add_library(i2pd_warnings INTERFACE)
if(MSVC)
  target_compile_options(i2pd_warnings INTERFACE /W4 /permissive- /Zc:__cplusplus)
else()
  target_compile_options(i2pd_warnings INTERFACE
    -Wall -Wextra -Wpedantic -Wno-unused-parameter -Wno-uninitialized
  )
  if(WITH_HARDENING)
    target_compile_options(i2pd_warnings INTERFACE
      -fstack-protector-strong -Wformat -Wformat-security -Werror=format-security
    )
    target_compile_definitions(i2pd_warnings INTERFACE _FORTIFY_SOURCE=2)
    target_link_options(i2pd_warnings INTERFACE -Wl,-z,relro -Wl,-z,now)
  endif()
endif()

function(i2pd_enable_sanitizers target)
  # The options are documented as Unix/GCC/Clang only. MSVC/clang-cl and the
  # MinGW runtimes use different sanitizer support; ignore the flags rather
  # than emitting an invalid or unsupported command line.
  if(WIN32)
    if(WITH_ADDRSANITIZER OR WITH_THREADSANITIZER OR WITH_UNDEFSANITIZER)
      message(WARNING "Sanitizer options are not supported on Windows; ignoring them")
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
    target_compile_options(${target} PRIVATE -fsanitize=undefined -fno-sanitize=vptr -fno-sanitize=enum)
    target_link_options(${target} PRIVATE -fsanitize=undefined)
  endif()
endfunction()

if(PRIVSEP_SOURCES AND NOT ANDROID)
  # Keep the native IPC/supervisor code out of the core library and daemon
  # source list. This gives the component one explicit ownership boundary and
  # lets tests or a future supervisor link it independently.
  add_library(i2pd_privsep STATIC ${PRIVSEP_SOURCES})
  set_target_properties(i2pd_privsep PROPERTIES PREFIX "" POSITION_INDEPENDENT_CODE ON)
  target_compile_features(i2pd_privsep PRIVATE cxx_std_23)
  target_include_directories(i2pd_privsep PRIVATE
    "${PROJECT_ROOT}"
    "${DAEMON_DIR}"
    "${LANG_DIR}"
  )
  target_link_libraries(i2pd_privsep PRIVATE Threads::Threads i2pd_warnings)
  if(CMAKE_SYSTEM_NAME STREQUAL "OpenBSD")
    # OpenBSD imsg lives in libutil.
    target_link_libraries(i2pd_privsep PUBLIC util)
  elseif(WIN32)
    # CreateRestrictedToken/CreateProcessAsUser and pipe security APIs.
    target_compile_definitions(i2pd_privsep PRIVATE
      WIN32_LEAN_AND_MEAN NOMINMAX WINVER=0x0A00 _WIN32_WINNT=0x0A00)
    target_link_libraries(i2pd_privsep PUBLIC advapi32)
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
target_link_libraries(libi2pd
  PUBLIC
    OpenSSL::SSL OpenSSL::Crypto ZLIB::ZLIB
    Boost::program_options Threads::Threads ${I2PD_EXTRA_LIBS}
)
if(WIN32)
  # Transports uses AcceptEx/GetAcceptExSockaddrs; static consumers (tools)
  # also need mswsock to resolve them.
  target_link_libraries(libi2pd PUBLIC mswsock ws2_32)
endif()
target_link_libraries(libi2pd PRIVATE $<BUILD_INTERFACE:i2pd_warnings>)
i2pd_enable_sanitizers(libi2pd)
if(WITH_UPNP)
  target_compile_definitions(libi2pd PRIVATE USE_UPNP)
endif()
if(WIN32)
  target_compile_definitions(libi2pd PRIVATE
    WIN32_APP WIN32_LEAN_AND_MEAN NOMINMAX WINVER=0x0A00 _WIN32_WINNT=0x0A00)
  target_link_libraries(libi2pd PUBLIC ws2_32 iphlpapi)
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
target_link_libraries(libi2pdclient PRIVATE $<BUILD_INTERFACE:i2pd_warnings>)
i2pd_enable_sanitizers(libi2pdclient)
if(NOT WITH_TORRENTS)
  target_compile_definitions(libi2pdclient PUBLIC NO_TORRENTS)
elseif(TARGET Boost::json AND TARGET Boost::url)
  target_link_libraries(libi2pdclient PRIVATE Boost::json Boost::url)
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
# The translation glue uses the active client language and therefore lives in
# libi2pdclient; the language data library stays free of client dependencies.
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
    "${DAEMON_DIR}/Daemon.cpp"
    "${DAEMON_DIR}/I2PControl.cpp"
    "${DAEMON_DIR}/I2PControlHandlers.cpp"
    "${DAEMON_DIR}/UPnP.cpp"
    "${DAEMON_DIR}/TuiServerCommon.cpp"
    "${DAEMON_DIR}/TuiServer.cpp"
  )
  set_target_properties(i2pd_jni PROPERTIES OUTPUT_NAME i2pd PREFIX "lib")
  target_compile_features(i2pd_jni PRIVATE cxx_std_23)
  target_include_directories(i2pd_jni PRIVATE
    "${PROJECT_ROOT}/android/app/src/main/cpp" ${DAEMON_DIR} ${LANG_DIR} ${TUI_DIR})
  target_compile_definitions(i2pd_jni PRIVATE ANDROID I2PD_NO_PRIVSEP)
  if(WITH_UPNP)
    target_compile_definitions(i2pd_jni PRIVATE USE_UPNP)
    target_link_libraries(i2pd_jni PRIVATE MiniUPnPc::MiniUPnPc)
  endif()
  target_link_libraries(i2pd_jni PRIVATE libi2pd libi2pdclient libi2pdlang log)
  i2pd_enable_sanitizers(i2pd_jni)
  install(TARGETS i2pd_jni LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT Runtime)
elseif(WITH_BINARY)
  if(WIN32)
    add_executable(i2pd WIN32 ${DAEMON_SOURCES})
  else()
    add_executable(i2pd ${DAEMON_SOURCES})
  endif()
  target_compile_features(i2pd PRIVATE cxx_std_23)
  target_include_directories(i2pd PRIVATE
    ${PROJECT_ROOT} ${DAEMON_DIR} ${LANG_DIR} ${TUI_DIR})
  target_link_libraries(i2pd PRIVATE libi2pd libi2pdclient libi2pdlang i2pd_warnings)
  if(TARGET i2pd_privsep)
    target_link_libraries(i2pd PRIVATE i2pd_privsep)
  endif()
  if(WITH_UPNP)
    target_compile_definitions(i2pd PRIVATE USE_UPNP)
    target_link_libraries(i2pd PRIVATE MiniUPnPc::MiniUPnPc)
  endif()
  if(WIN32)
    target_compile_definitions(i2pd PRIVATE
      WIN32_APP WIN32_LEAN_AND_MEAN NOMINMAX WINVER=0x0A00 _WIN32_WINNT=0x0A00
    )
    target_link_libraries(i2pd PRIVATE
      wsock32 ws2_32 mswsock iphlpapi crypt32 gdi32 ole32 uuid advapi32)
  endif()
  if(WITH_STATIC AND NOT MSVC)
    target_link_options(i2pd PRIVATE -static)
  endif()
  i2pd_enable_sanitizers(i2pd)
endif()

# The TUI is a separate process and never links the router libraries. Both
# backends share main.cpp, Theme.h and TuiProtocol.h; only the terminal and
# transport implementations differ.
if(I2PD_BUILD_TUI)
  if(UNIX)
    set(CURSES_NEED_WIDE TRUE)
    find_package(Curses)
    if(CURSES_FOUND)
      add_executable(i2pd-tui
        "${TUI_DIR}/main.cpp"
        "${TUI_DIR}/TuiClient.cpp"
        "${TUI_DIR}/TerminalUnix.cpp"
      )
      target_compile_features(i2pd-tui PRIVATE cxx_std_23)
      target_include_directories(i2pd-tui PRIVATE
        "${LIBI2PD_DIR}" "${TUI_DIR}" ${CURSES_INCLUDE_DIRS})
      target_link_libraries(i2pd-tui PRIVATE
        ${CURSES_LIBRARIES} Threads::Threads i2pd_warnings)
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
      "${LIBI2PD_DIR}" "${TUI_DIR}")
    target_compile_definitions(i2pd-tui PRIVATE
      WIN32_LEAN_AND_MEAN NOMINMAX WINVER=0x0A00 _WIN32_WINNT=0x0A00)
    # advapi32: token/SID inspection used for the named-pipe authorization
    # policy. The console backend uses kernel32 only; no Winsock, shell or
    # Curses library is required.
    target_link_libraries(i2pd-tui PRIVATE
      advapi32 Threads::Threads i2pd_warnings)
  endif()
endif()

# Installation
if(WITH_LIBRARY)
  install(TARGETS libi2pd libi2pdclient libi2pdlang
    EXPORT i2pd-targets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    COMPONENT Libraries
  )
  install(DIRECTORY "${LIBI2PD_DIR}/" DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/libi2pd
    FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp")
  install(DIRECTORY "${LIBI2PD_CLIENT_DIR}/" DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/libi2pdclient
    FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp")
  install(DIRECTORY "${LANG_DIR}/" DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/libi2pdlang
    FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp")

  include(CMakePackageConfigHelpers)
  write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/i2pdConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion)
  configure_file(cmake/i2pdConfig.cmake "${CMAKE_CURRENT_BINARY_DIR}/i2pdConfig.cmake" @ONLY)
  install(EXPORT i2pd-targets
    FILE i2pd-targets.cmake
    NAMESPACE i2pd::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/i2pd)
  install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/i2pdConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/i2pdConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/i2pd)
endif()

if(WITH_BINARY)
  install(TARGETS i2pd RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT Runtime)
  install(FILES
    "${PROJECT_ROOT}/contrib/i2pd.conf"
    "${PROJECT_ROOT}/contrib/subscriptions.txt"
    "${PROJECT_ROOT}/contrib/tunnels.conf"
    DESTINATION ${CMAKE_INSTALL_SYSCONFDIR}/i2pd
    COMPONENT Runtime)
  install(DIRECTORY "${PROJECT_ROOT}/contrib/tunnels.d/"
    DESTINATION ${CMAKE_INSTALL_SYSCONFDIR}/i2pd/tunnels.conf.d
    COMPONENT Runtime)
  install(DIRECTORY "${PROJECT_ROOT}/contrib/certificates/"
    DESTINATION ${CMAKE_INSTALL_DATADIR}/i2pd/certificates
    COMPONENT Runtime)
  if(UNIX)
    install(FILES "${PROJECT_ROOT}/contrib/openi2pd.desktop"
      DESTINATION ${CMAKE_INSTALL_DATADIR}/applications
      COMPONENT Runtime)
    install(FILES "${PROJECT_ROOT}/assets/branding/symbol.svg"
      DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps
      RENAME openi2pd.svg
      COMPONENT Runtime)
    install(FILES "${PROJECT_ROOT}/contrib/artwork/openi2pd-256.png"
      DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/256x256/apps
      RENAME openi2pd.png
      COMPONENT Runtime)
  endif()
  if(I2PD_INSTALL_DOCS)
    install(FILES
      "${PROJECT_ROOT}/ChangeLog" "${PROJECT_ROOT}/LICENSE" "${PROJECT_ROOT}/README.md"
      "${PROJECT_ROOT}/TODO.md" "${PROJECT_ROOT}/ROADMAP.md" "${PROJECT_ROOT}/PROGRESS.md"
      DESTINATION ${CMAKE_INSTALL_DOCDIR}
      COMPONENT Documentation)
    install(DIRECTORY "${PROJECT_ROOT}/docs/"
      DESTINATION ${CMAKE_INSTALL_DOCDIR}/docs
      COMPONENT Documentation
      FILES_MATCHING PATTERN "*.md" PATTERN "*.png" PATTERN "*.ico" PATTERN "*.css" PATTERN "*.svg"
      PATTERN "Doxyfile" EXCLUDE)
    install(DIRECTORY "${PROJECT_ROOT}/assets/branding/"
      DESTINATION ${CMAKE_INSTALL_DOCDIR}/assets/branding
      COMPONENT Documentation)
    if(UNIX)
      install(DIRECTORY "${PROJECT_ROOT}/man/"
        DESTINATION ${CMAKE_INSTALL_MANDIR}/man1
        COMPONENT Documentation
        FILES_MATCHING PATTERN "*.1")
    endif()
  endif()
endif()

# Exactly one service manager is installed. The executable/config/doc/man and
# TUI install rules above are independent of this selection.
if(I2PD_SERVICE_MANAGER STREQUAL "systemd")
  install(FILES "${PROJECT_ROOT}/contrib/i2pd.service"
    DESTINATION "${_i2pd_service_dir}"
    RENAME i2pd.service
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
  # The supervised service tree keeps the executable bits of run and log/run.
  install(DIRECTORY "${PROJECT_ROOT}/contrib/runit/i2pd/"
    DESTINATION "${_i2pd_service_dir}/i2pd"
    COMPONENT Service
    USE_SOURCE_PERMISSIONS)
elseif(I2PD_SERVICE_MANAGER STREQUAL "s6")
  install(DIRECTORY "${PROJECT_ROOT}/contrib/s6/i2pd/"
    DESTINATION "${_i2pd_service_dir}/i2pd"
    COMPONENT Service
    USE_SOURCE_PERMISSIONS)
elseif(I2PD_SERVICE_MANAGER STREQUAL "s6-rc")
  # s6-rc-compile consumes the source tree directly; keep its longrun
  # directories and dependency declarations intact.
  install(DIRECTORY "${PROJECT_ROOT}/contrib/s6-rc/source/"
    DESTINATION "${_i2pd_service_dir}"
    COMPONENT Service
    USE_SOURCE_PERMISSIONS)
elseif(I2PD_SERVICE_MANAGER STREQUAL "shepherd")
  install(FILES "${PROJECT_ROOT}/contrib/shepherd/i2pd.scm"
    DESTINATION "${_i2pd_service_dir}"
    COMPONENT Service)
endif()

if(TARGET i2pd-tui)
  install(TARGETS i2pd-tui RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT Runtime)
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

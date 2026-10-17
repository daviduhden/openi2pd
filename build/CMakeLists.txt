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
  message(FATAL_ERROR "Open I2Pd supports Windows, Linux, OpenBSD and Android")
endif()

# C++23 is a hard requirement. There is no fallback: accepting an older
# standard would silently disable std::expected and other guarantees used by
# the administrative IPC code.
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
if(DEFINED WITH_TUI)
  set(_i2pd_tui_default "${WITH_TUI}")
else()
  set(_i2pd_tui_default ON)
endif()
option(I2PD_BUILD_TUI       "Build the ncurses TUI when available"         ${_i2pd_tui_default})
option(I2PD_BUILD_TOOLS     "Build i2pd auxiliary tools"                   ON)
option(I2PD_BUILD_JNI       "Build the Android JNI library"                  OFF)
option(I2PD_BUILD_TESTS     "Build i2pd tests (alias of BUILD_TESTING)"    OFF)
option(WITH_STATIC          "Link the router executable statically"        OFF)
option(WITH_UPNP            "Include support for the UPnP client"          ON)
option(WITH_TORRENTS        "Include the BitTorrent client"                ON)
option(WITH_GIT_VERSION     "Use git commit info as version"               OFF)
option(WITH_ADDRSANITIZER   "Build with AddressSanitizer (Unix)"           OFF)
option(WITH_THREADSANITIZER "Build with ThreadSanitizer (Unix)"            OFF)
option(WITH_UNDEFSANITIZER  "Build with UndefinedBehaviorSanitizer (Unix)" OFF)
option(BUILD_TESTING        "Build unit and protocol tests"                OFF)

include(GNUInstallDirs)

# Keep the historical option names working while exposing the canonical
# I2PD_BUILD_* names requested by the integrated tree.
if(I2PD_BUILD_TESTS)
  set(BUILD_TESTING ON CACHE BOOL "Build unit and protocol tests" FORCE)
endif()
set(WITH_TUI "${I2PD_BUILD_TUI}")
include(CTest)

if(ANDROID)
  set(WITH_TORRENTS OFF)
  set(WITH_TUI OFF)
endif()

if(WITH_STATIC)
  if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  endif()
  set(CMAKE_FIND_LIBRARY_SUFFIXES ".a")
  set(Boost_USE_STATIC_LIBS ON)
  set(OPENSSL_USE_STATIC_LIBS ON)
  set(ZLIB_USE_STATIC_LIBS ON)
  set(MINIUPNPC_USE_STATIC_LIBS ON)
  set(BUILD_SHARED_LIBS OFF)
endif()

include(CheckAtomic)
set(I2PD_EXTRA_LIBS ${CMAKE_REQUIRED_LIBRARIES})
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

find_package(Boost 1.74 REQUIRED COMPONENTS atomic filesystem program_options)
if(WITH_TORRENTS AND NOT ANDROID)
  find_package(Boost 1.81 REQUIRED COMPONENTS json url)
endif()
find_package(OpenSSL REQUIRED)
find_package(ZLIB REQUIRED)

if(WITH_UPNP)
  find_package(MiniUPnPc)
  if(NOT MINIUPNPC_FOUND)
    message(FATAL_ERROR "MiniUPnPc was requested but its headers/library were not found")
  endif()
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
  "${LIBI2PD_DIR}/I2PEndian.cpp"
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
)

if(UNIX)
  list(APPEND DAEMON_SOURCES
    "${DAEMON_DIR}/TuiServer.cpp"
    "${DAEMON_DIR}/UnixDaemon.cpp"
    "${PROJECT_ROOT}/privsep/Channel.cpp"
    "${PROJECT_ROOT}/privsep/LogWorker.cpp"
    "${PROJECT_ROOT}/privsep/RunWorker.cpp"
    "${PROJECT_ROOT}/privsep/Worker.cpp"
  )
endif()

if(WIN32)
  list(APPEND DAEMON_SOURCES
    "${PROJECT_ROOT}/Win32/DaemonWin32.cpp"
    "${PROJECT_ROOT}/Win32/Win32App.cpp"
    "${PROJECT_ROOT}/Win32/Win32Service.cpp"
    "${PROJECT_ROOT}/Win32/Win32NetState.cpp"
  )
  list(APPEND DAEMON_SOURCES "${PROJECT_ROOT}/Win32/Resource.rc")
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
    Boost::atomic Boost::filesystem Boost::program_options
    Threads::Threads ${I2PD_EXTRA_LIBS}
)
target_link_libraries(libi2pd PRIVATE $<BUILD_INTERFACE:i2pd_warnings>)
i2pd_enable_sanitizers(libi2pd)
if(WITH_UPNP)
  target_compile_definitions(libi2pd PRIVATE USE_UPNP)
endif()
if(OPENSSL_VERSION MATCHES "^[0-9]+\\.[0-9]+" AND OPENSSL_VERSION VERSION_GREATER_EQUAL 3.0.0)
  target_compile_definitions(libi2pd PRIVATE OPENSSL_SUPPRESS_DEPRECATED)
endif()
if(WIN32)
  target_compile_definitions(libi2pd PRIVATE WIN32_APP WIN32_LEAN_AND_MEAN NOMINMAX)
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
if(NOT WITH_TORRENTS OR ANDROID)
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
  add_library(i2pd_jni SHARED
    "${PROJECT_ROOT}/android/app/src/main/cpp/DaemonAndroid.cpp"
    "${PROJECT_ROOT}/android/app/src/main/cpp/i2pd_android.cpp"
  )
  set_target_properties(i2pd_jni PROPERTIES OUTPUT_NAME i2pd PREFIX "lib")
  target_compile_features(i2pd_jni PRIVATE cxx_std_23)
  target_include_directories(i2pd_jni PRIVATE
    "${PROJECT_ROOT}/android/app/src/main/cpp" ${DAEMON_DIR} ${LANG_DIR})
  target_compile_definitions(i2pd_jni PRIVATE ANDROID)
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
  target_include_directories(i2pd PRIVATE ${PROJECT_ROOT} ${DAEMON_DIR} ${LANG_DIR})
  target_link_libraries(i2pd PRIVATE libi2pd libi2pdclient libi2pdlang i2pd_warnings)
  if(WITH_UPNP)
    target_compile_definitions(i2pd PRIVATE USE_UPNP)
    target_link_libraries(i2pd PRIVATE ${MINIUPNPC_LIBRARY})
    target_include_directories(i2pd PRIVATE ${MINIUPNPC_INCLUDE_DIR})
  endif()
  if(WIN32)
    target_compile_definitions(i2pd PRIVATE
      WIN32_APP WIN32_LEAN_AND_MEAN NOMINMAX WINVER=0x0A00 _WIN32_WINNT=0x0A00
    )
    target_link_libraries(i2pd PRIVATE wsock32 ws2_32 iphlpapi crypt32 gdi32 ole32 uuid)
  endif()
  if(WITH_STATIC AND NOT MSVC)
    target_link_options(i2pd PRIVATE -static)
  endif()
  i2pd_enable_sanitizers(i2pd)
endif()

if(WITH_TUI AND UNIX)
  set(CURSES_NEED_WIDE TRUE)
  find_package(Curses)
  if(CURSES_FOUND)
    add_executable(i2pd-tui
      "${TUI_DIR}/main.cpp"
      "${TUI_DIR}/TuiClient.cpp"
    )
    target_compile_features(i2pd-tui PRIVATE cxx_std_23)
    target_include_directories(i2pd-tui PRIVATE "${LIBI2PD_DIR}" ${CURSES_INCLUDE_DIRS})
    target_link_libraries(i2pd-tui PRIVATE ${CURSES_LIBRARIES} Threads::Threads i2pd_warnings)
    i2pd_enable_sanitizers(i2pd-tui)
  else()
    message(WARNING "ncurses not found: building without i2pd-tui")
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
  install(FILES
    "${PROJECT_ROOT}/ChangeLog" "${PROJECT_ROOT}/LICENSE" "${PROJECT_ROOT}/README.md"
    "${PROJECT_ROOT}/TODO.md" "${PROJECT_ROOT}/ROADMAP.md" "${PROJECT_ROOT}/PROGRESS.md"
    DESTINATION ${CMAKE_INSTALL_DOCDIR}
    COMPONENT Documentation)
  install(DIRECTORY "${PROJECT_ROOT}/docs/"
    DESTINATION ${CMAKE_INSTALL_DOCDIR}/docs
    COMPONENT Documentation
    FILES_MATCHING PATTERN "*.md" PATTERN "*.png" PATTERN "*.ico"
    PATTERN "Doxyfile" EXCLUDE)
  if(UNIX)
    install(FILES "${PROJECT_ROOT}/debian/i2pd.1"
      DESTINATION ${CMAKE_INSTALL_MANDIR}/man1
      COMPONENT Documentation)
  endif()
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
message(STATUS "Android JNI        : ${I2PD_BUILD_JNI}")
if(TARGET i2pd-tui)
  message(STATUS "TUI                : ON (i2pd-tui)")
else()
  message(STATUS "TUI                : OFF")
endif()
message(STATUS "Tests              : ${BUILD_TESTING}")
message(STATUS "Sanitizers         : ASan=${WITH_ADDRSANITIZER} TSan=${WITH_THREADSANITIZER} UBSan=${WITH_UNDEFSANITIZER}")
message(STATUS "---------------------------------------")

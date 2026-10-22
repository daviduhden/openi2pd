# See the LICENSE file at the top of the project tree for copyright
# and license details.
#
# openi2pd: check build system.
#
# Static CMake check of the build-system contract: POSIX make syntax, Ninja
# defaults, dependency floors and the absence of hidden downloads, globs,
# hardcoded prefixes and removed compatibility branches.
#

if(NOT PROJECT_ROOT)
  message(FATAL_ERROR "PROJECT_ROOT is required")
endif()

function(read_required path out_var)
  if(NOT EXISTS "${path}")
    message(FATAL_ERROR "Missing build-system file: ${path}")
  endif()
  file(READ "${path}" contents)
  set(${out_var} "${contents}" PARENT_SCOPE)
endfunction()

function(assert_absent text needle description)
  string(FIND "${text}" "${needle}" position)
  if(NOT position EQUAL -1)
    message(FATAL_ERROR "${description}: found forbidden '${needle}'")
  endif()
endfunction()

function(assert_present text needle description)
  string(FIND "${text}" "${needle}" position)
  if(position EQUAL -1)
    message(FATAL_ERROR "${description}: missing required '${needle}'")
  endif()
endfunction()

set(root_makefile "${PROJECT_ROOT}/Makefile")
set(test_makefile "${PROJECT_ROOT}/tests/Makefile")
set(top_cmake "${PROJECT_ROOT}/build/CMakeLists.txt")
set(tools_cmake "${PROJECT_ROOT}/tools/CMakeLists.txt")
set(tests_cmake "${PROJECT_ROOT}/tests/CMakeLists.txt")
set(android_cmake "${PROJECT_ROOT}/android/app/src/main/cpp/CMakeLists.txt")
set(mingw_helper "${PROJECT_ROOT}/build/build_mingw.cmd")
set(miniupnpc_module "${PROJECT_ROOT}/build/cmake_modules/FindMiniUPnPc.cmake")
set(check_atomic "${PROJECT_ROOT}/build/cmake_modules/CheckAtomic.cmake")
set(crypto_header "${PROJECT_ROOT}/libi2pd/Crypto.h")
set(crypto_source "${PROJECT_ROOT}/libi2pd/Crypto.cpp")
set(famtool_source "${PROJECT_ROOT}/tools/famtool.cpp")
set(endian_header "${PROJECT_ROOT}/libi2pd/I2PEndian.h")
set(sam_source "${PROJECT_ROOT}/libi2pd_client/SAM.cpp")
set(timestamp_source "${PROJECT_ROOT}/libi2pd/Timestamp.cpp")
set(util_source "${PROJECT_ROOT}/libi2pd/util.cpp")
set(win32_netstate "${PROJECT_ROOT}/Win32/Win32NetState.cpp")
set(upnp_source "${PROJECT_ROOT}/daemon/UPnP.cpp")
set(rpm_spec "${PROJECT_ROOT}/contrib/rpm/i2pd.spec")
set(rpm_git_spec "${PROJECT_ROOT}/contrib/rpm/i2pd-git.spec")
set(presets_file "${PROJECT_ROOT}/build/CMakePresets.json")
set(fuzz_workflow "${PROJECT_ROOT}/.github/workflows/fuzz.yml")

read_required("${root_makefile}" root_make)
read_required("${test_makefile}" test_make)
read_required("${top_cmake}" top_cmake_text)
read_required("${tools_cmake}" tools_cmake_text)
read_required("${tests_cmake}" tests_cmake_text)
read_required("${android_cmake}" android_cmake_text)
read_required("${mingw_helper}" mingw_text)
read_required("${miniupnpc_module}" miniupnpc_text)
read_required("${check_atomic}" check_atomic_text)
read_required("${crypto_header}" crypto_header_text)
read_required("${crypto_source}" crypto_source_text)
read_required("${famtool_source}" famtool_text)
read_required("${endian_header}" endian_header_text)
read_required("${sam_source}" sam_text)
read_required("${timestamp_source}" timestamp_text)
read_required("${util_source}" util_text)
read_required("${win32_netstate}" win32_netstate_text)
read_required("${upnp_source}" upnp_text)
read_required("${rpm_spec}" rpm_text)
read_required("${rpm_git_spec}" rpm_git_text)
read_required("${presets_file}" presets_text)
read_required("${fuzz_workflow}" fuzz_workflow_text)

# POSIX make has no ?= or GNU pattern/functions. The front-end must be usable
# with BSD make on OpenBSD.
foreach(make_text IN ITEMS "${root_make}" "${test_make}")
  assert_absent("${make_text}" "?=" "POSIX makefile uses non-POSIX conditional assignment")
  assert_absent("${make_text}" "wildcard(" "POSIX makefile uses GNU wildcard")
  assert_absent("${make_text}" "$(wildcard" "POSIX makefile uses GNU wildcard")
  assert_absent("${make_text}" "patsubst" "POSIX makefile uses GNU patsubst")
  assert_absent("${make_text}" "$(patsubst" "POSIX makefile uses GNU patsubst")
  assert_absent("${make_text}" "shell(" "POSIX makefile uses GNU shell")
  assert_absent("${make_text}" "$(shell" "POSIX makefile uses GNU shell")
  assert_absent("${make_text}" "ifeq" "POSIX makefile uses GNU ifeq")
  assert_absent("${make_text}" "ifneq" "POSIX makefile uses GNU ifneq")
endforeach()

foreach(target IN ITEMS "configure:" "build:" "test:" "install:" "tools:" "check:")
  string(FIND "${root_make}" "${target}" target_position)
  if(target_position EQUAL -1)
    message(FATAL_ERROR "Root POSIX Makefile is missing target '${target}'")
  endif()
endforeach()
foreach(target IN ITEMS "configure:" "build:" "test:" "all:" "clean:")
  string(FIND "${test_make}" "${target}" target_position)
  if(target_position EQUAL -1)
    message(FATAL_ERROR "tests/Makefile is missing target '${target}'")
  endif()
endforeach()
string(FIND "${root_make}" "CMAKE_GENERATOR = Ninja" root_generator)
string(FIND "${test_make}" "CMAKE_GENERATOR = Ninja" test_generator)
if(root_generator EQUAL -1 OR test_generator EQUAL -1)
  message(FATAL_ERROR "POSIX Makefiles must default to the Ninja CMake generator")
endif()

# No configure-time downloads, globs or hidden source trees in any maintained
# CMake entry point.
foreach(cmake_text IN ITEMS "${top_cmake_text}" "${tools_cmake_text}" "${tests_cmake_text}" "${android_cmake_text}"
    "${miniupnpc_text}")
  assert_absent("${cmake_text}" "file(DOWNLOAD" "CMake fetches files")
  assert_absent("${cmake_text}" "ExternalProject" "CMake uses ExternalProject")
  assert_absent("${cmake_text}" "FetchContent" "CMake uses FetchContent")
  assert_absent("${cmake_text}" "git clone" "CMake clones a repository")
  assert_absent("${cmake_text}" "file(GLOB" "CMake uses a source glob")
endforeach()

# Platform-independent dependency discovery: no injected prefixes.
foreach(path_needle IN ITEMS "/usr/include" "/usr/local/include" "/opt/local/include" "SystemDrive" "C:\\msys64")
  assert_absent("${miniupnpc_text}" "${path_needle}" "MiniUPnPc finder hardcodes an installation path")
  assert_absent("${top_cmake_text}" "${path_needle}" "Top-level CMake hardcodes an installation path")
  assert_absent("${tools_cmake_text}" "${path_needle}" "Tools CMake hardcodes an installation path")
  assert_absent("${tests_cmake_text}" "${path_needle}" "Tests CMake hardcodes an installation path")
  assert_absent("${android_cmake_text}" "${path_needle}" "Android CMake hardcodes an installation path")
endforeach()

# Dependency floors and removed compatibility branches are part of the
# maintained contract. These are compile-free source checks.
foreach(required IN ITEMS "OpenSSL 3.0.2" "LibreSSL 4.0" "cmake_minimum_required(VERSION 3.24)" "I2PD_LIBRESSL_VERSION" "LIBRESSL_VERSION_TEXT"
    "I2PD_OPENSSL_EXPECT" "I2PD_HAVE_EVP_PKEY_IS_A" "I2PD_HAVE_EVP_KDF_FETCH"
    "I2PD_HAVE_EVP_EC_GEN" "Expected.h lives in libi2pd" "MiniUPnPc 2.2.6")
  string(FIND "${top_cmake_text}" "${required}" position)
  if(position EQUAL -1)
    message(FATAL_ERROR "CMake is missing dependency-floor marker '${required}'")
  endif()
endforeach()
foreach(required IN ITEMS "MINIUPNPC_API_VERSION" "LESS 17")
  string(FIND "${miniupnpc_text}" "${required}" position)
  if(position EQUAL -1)
    message(FATAL_ERROR "MiniUPnPc finder is missing API-floor marker '${required}'")
  endif()
endforeach()

assert_present("${root_make}" ".PHONY:" "POSIX Makefile must declare phony targets")
assert_present("${root_make}" "install: build" "POSIX make install must build first")
assert_present("${presets_text}" "\"generator\": \"Ninja\"" "CMake presets must default to Ninja")
assert_present("${top_cmake_text}" "COMPONENT Development" "development installs need a component")
assert_present("${top_cmake_text}" "COMPONENT Tui" "the TUI needs a separate install component")
assert_present("${top_cmake_text}" "COMPONENT Service" "service files need a separate install component")
assert_present("${top_cmake_text}" "configure_package_config_file"
  "the package config must use configure_package_config_file")
assert_present("${top_cmake_text}" "PATH_VARS CMAKE_INSTALL_INCLUDEDIR"
  "the package config must derive the include path from PATH_VARS")
assert_present("${top_cmake_text}" "lib/systemd/system" "systemd units need an architecture-independent path")
assert_absent("${top_cmake_text}" "CMAKE_INSTALL_LIBDIR}/systemd/system"
  "systemd unit destination must not use multiarch LIBDIR")
assert_present("${top_cmake_text}" "find_package(Boost 1.81 REQUIRED COMPONENTS program_options json url)"
  "torrent builds require the compiled Boost.JSON/URL components")
assert_present("${top_cmake_text}" "target_link_libraries(libi2pdclient PRIVATE Boost::json Boost::url)"
  "torrent RPC must link the Boost.JSON/URL implementations")
read_required("${PROJECT_ROOT}/build/cmake/i2pdConfig.cmake" package_config_text)
assert_present("${package_config_text}" "@I2PD_BOOST_MIN_VERSION@ COMPONENTS @I2PD_BOOST_COMPONENTS@"
  "installed consumers must discover the configured Boost components")
assert_absent("${top_cmake_text}" "set(CMAKE_MODULE_PATH"
  "CMake module path must be prepended rather than overwritten")
assert_present("${top_cmake_text}" "CMAKE_GENERATOR MATCHES \"Makefiles"
  "OpenBSD must reject the Unix Makefiles generator")
assert_present("${top_cmake_text}" "BOOST_ASIO_HAS_SNPRINTF=1"
  "Boost.Asio scope-ID formatting must select the bounded snprintf path")
assert_present("${top_cmake_text}" "target_link_libraries(libi2pd PUBLIC ws2_32 iphlpapi shell32)"
  "Windows static consumers need the SHGetFolderPathW import library")
assert_present("${top_cmake_text}" "-Wl,--dynamicbase -Wl,--nxcompat"
  "MinGW hardening must use PE rather than ELF linker options")
assert_present("${tests_cmake_text}" "test-boost-linkage"
  "compiled Boost.JSON/URL dependencies need a link regression")
assert_absent("${top_cmake_text}" "OPENSSL_SUPPRESS_DEPRECATED"
  "CMake still suppresses OpenSSL deprecation diagnostics")
assert_absent("${famtool_text}" "OPENSSL_SUPPRESS_DEPRECATED"
  "famtool still suppresses OpenSSL deprecation diagnostics")
assert_absent("${crypto_header_text}" "OPENSSL_VERSION_NUMBER != 0x030000000"
  "Crypto.h still excludes the OpenSSL 3.0.0 SipHash regression")
assert_absent("${crypto_source_text}" "LIBRESSL_VERSION_NUMBER < 0x4000000fL"
  "Crypto.cpp still carries the pre-LibreSSL-4.0 in-place workaround")
assert_absent("${check_atomic_text}" "LLVM_HAS_ATOMICS"
  "CheckAtomic.cmake still probes legacy LLVM atomics")
assert_absent("${sam_text}" "sprintf_s" "SAM.cpp still uses the MSVC-only sprintf_s path")
assert_absent("${timestamp_text}" "sprintf_s" "Timestamp.cpp still uses the MSVC-only sprintf_s path")
assert_absent("${util_text}" "inet_pton_xp" "util.cpp still contains the Windows XP inet_pton fallback")
assert_absent("${util_text}" "inet_ntop_xp" "util.cpp still contains the Windows XP inet_ntop fallback")
assert_absent("${win32_netstate_text}" "0x0501" "Win32NetState still carries the Windows XP branch")
string(FIND "${upnp_text}" "MINIUPNPC_API_VERSION >= 18" upnp_api18)
if(upnp_api18 EQUAL -1)
  message(FATAL_ERROR "UPnP.cpp lost the API-18 UPNP_GetValidIGD capability check")
endif()
foreach(removed IN ITEMS "MINIUPNPC_API_VERSION >= 14" "MINIUPNPC_API_VERSION >= 10" "MINIUPNPC_API_VERSION >= 8")
  assert_absent("${upnp_text}" "${removed}" "UPnP.cpp still carries an old MiniUPnPc API branch")
endforeach()
foreach(required IN ITEMS "std::endian" "std::byteswap")
  string(FIND "${endian_header_text}" "${required}" position)
  if(position EQUAL -1)
    message(FATAL_ERROR "I2PEndian.h is missing C++23 endian marker '${required}'")
  endif()
endforeach()

# Windows packaging must not assume an installation drive/directory or pull
# the worktree from a remote during a local build.
foreach(mingw_needle IN ITEMS "C:\\msys64" "C:\\PROGRA~2" "git pull")
  assert_absent("${mingw_text}" "${mingw_needle}" "Windows helper hardcodes or downloads")
endforeach()

# The retired administrative web console must not reappear in the source
# graph. HTTP proxy/tunnel and reseed services are separate and retained.
foreach(removed IN ITEMS
    "${PROJECT_ROOT}/contrib/webconsole"
    "${PROJECT_ROOT}/contrib/torrents/web"
    "${PROJECT_ROOT}/daemon/HTTPServer.cpp"
    "${PROJECT_ROOT}/daemon/HTTPServer.h"
    "${PROJECT_ROOT}/daemon/HTTPServerResources.h")
  if(EXISTS "${removed}")
    message(FATAL_ERROR "Retired administrative web console asset is present: ${removed}")
  endif()
endforeach()

# CMake remains the source of truth for C++23, components and tests.
foreach(required IN ITEMS
    "CMAKE_CXX_STANDARD 23"
    "CMAKE_CXX_EXTENSIONS OFF"
    "I2PD_BUILD_TUI"
    "I2PD_BUILD_TOOLS"
    "I2PD_BUILD_JNI"
    "I2PD_INSTALL_DOCS"
    "I2PD_PERL_CRITIC"
    "BUILD_TESTING"
    "i2pd_privsep"
    "WITH_BINARY"
    "WITH_LIBRARY"
    "WITH_TORRENTS"
    "I2PD_SERVICE_MANAGER"
    "I2PD_SERVICE_DIR"
    "I2PD_SERVICE_CONFDIR")
  string(FIND "${top_cmake_text}" "${required}" position)
  if(position EQUAL -1)
    message(FATAL_ERROR "CMake is missing required component marker '${required}'")
  endif()
endforeach()

# Service-manager installation is opt-in and exclusive. Each canonical source
# must occur exactly once in the top-level install graph, so a second
# unconditional install cannot silently ship every init system.
string(FIND "${top_cmake_text}" "set(I2PD_SERVICE_MANAGER \"none\" CACHE STRING" service_default)
if(service_default EQUAL -1)
  message(FATAL_ERROR "I2PD_SERVICE_MANAGER must default to none")
endif()
foreach(manager IN ITEMS none systemd openrc runit s6 s6-rc shepherd sysvinit)
  string(FIND "${top_cmake_text}" "\"${manager}\"" manager_position)
  if(manager_position EQUAL -1)
    message(FATAL_ERROR "I2PD_SERVICE_MANAGER is missing supported value '${manager}'")
  endif()
endforeach()
foreach(service_source IN ITEMS
    "contrib/i2pd.service"
    "contrib/sysvinit/i2pd"
    "contrib/openrc/i2pd.openrc"
    "contrib/openrc/conf.d/i2pd"
    "contrib/runit/i2pd/run"
    "contrib/runit/i2pd/log/run"
    "contrib/s6/i2pd/run"
    "contrib/s6/i2pd/down-signal"
    "contrib/s6/i2pd/timeout-kill"
    "contrib/s6-rc/source/i2pd/run"
    "contrib/s6-rc/source/i2pd/type"
    "contrib/s6-rc/source/i2pd/down-signal"
    "contrib/s6-rc/source/i2pd/timeout-kill"
    "contrib/s6-rc/source/i2pd/dependencies.d/base"
    "contrib/shepherd/i2pd.scm"
    "contrib/sysusers.d/i2pd.conf"
    "contrib/tmpfiles.d/i2pd.conf")
  string(REGEX MATCHALL "${service_source}" service_matches "${top_cmake_text}")
  list(LENGTH service_matches service_match_count)
  if(NOT service_match_count EQUAL 1)
    message(FATAL_ERROR
      "CMake must install ${service_source} from exactly one service-manager "
      "branch (found ${service_match_count} references)")
  endif()
endforeach()

string(FIND "${root_make}" "CMAKE_GENERATOR = Ninja" generator_position)
if(generator_position EQUAL -1)
  message(FATAL_ERROR "The POSIX front-end must default to the Ninja generator")
endif()

# Every checked-in regression source must be named in tests/CMakeLists.txt.
# Platform-specific sources may still be conditionally registered, but a new
# file cannot silently fall out of the suite.
file(GLOB _i2pd_test_sources RELATIVE "${PROJECT_ROOT}/tests" "${PROJECT_ROOT}/tests/test-*.cpp")
foreach(_source IN LISTS _i2pd_test_sources)
  string(REGEX REPLACE "\\.cpp$" "" _test_name "${_source}")
  string(FIND "${tests_cmake_text}" "${_test_name}" _test_position)
  if(_test_position EQUAL -1)
    message(FATAL_ERROR "tests/CMakeLists.txt does not register ${_test_name}")
  endif()
endforeach()

# The fuzzer inventory is also explicit: adding a source without wiring it to
# the manual Fuzzing workflow must fail the static check.
file(GLOB _i2pd_fuzz_sources RELATIVE "${PROJECT_ROOT}/tests/fuzz" "${PROJECT_ROOT}/tests/fuzz/*.cpp")
foreach(_source IN LISTS _i2pd_fuzz_sources)
  string(REGEX REPLACE "\\.cpp$" "" _fuzzer_name "${_source}")
  string(FIND "${tests_cmake_text}" "${_fuzzer_name}" _fuzzer_position)
  if(_fuzzer_position EQUAL -1)
    message(FATAL_ERROR "tests/CMakeLists.txt does not register ${_fuzzer_name}")
  endif()
  string(FIND "${fuzz_workflow_text}" "${_fuzzer_name}" _fuzz_workflow_position)
  if(_fuzz_workflow_position EQUAL -1)
    message(FATAL_ERROR ".github/workflows/fuzz.yml does not build/run ${_fuzzer_name}")
  endif()
endforeach()

# The RPM specs must not invoke GNU make or Ninja directly: they build through
# cmake --build and pin the Ninja generator explicitly. Some Fedora releases
# package the required Boost.JSON/URL libraries separately, so the guarded
# build requirements must keep a defined distro-macro test.
foreach(_rpm_text IN ITEMS "${rpm_text}" "${rpm_git_text}")
  assert_absent("${_rpm_text}" "_smp_mflags" "RPM spec uses make/ninja directly")
  assert_absent("${_rpm_text}" "%make_build" "RPM spec uses %make_build")
  assert_absent("${_rpm_text}" "%ninja_build" "RPM spec uses %ninja_build")
  assert_absent("${_rpm_text}" "%ninja" "RPM spec uses a Ninja macro")
  assert_present("${_rpm_text}" "BuildRequires: cmake >= 3.24"
    "RPM spec lacks the CMake 3.24 floor")
  assert_present("${_rpm_text}" "BuildRequires: ninja-build"
    "RPM spec lacks the Ninja build dependency")
  assert_present("${_rpm_text}" "_cmake_generator Ninja"
    "RPM spec does not pin the Ninja generator")
  assert_present("${_rpm_text}" "0%{?fedora} && 0%{?fedora} <= 43"
    "RPM spec lacks the defined Fedora Boost.JSON/URL guard")
  assert_present("${_rpm_text}" "boost-json"
    "RPM spec lacks the Boost.JSON build dependency")
  assert_present("${_rpm_text}" "boost-url"
    "RPM spec lacks the Boost.URL build dependency")
  assert_present("${_rpm_text}" "cmake --build" "RPM spec lacks cmake --build")
endforeach()

# Keep first-party source free of the unbounded C string functions. The
# token boundary prevents std::snprintf from matching; Boost/OpenSSL/system
# headers are excluded.
file(GLOB_RECURSE _i2pd_format_sources
  "${PROJECT_ROOT}/libi2pd/*.cpp" "${PROJECT_ROOT}/libi2pd/*.h" "${PROJECT_ROOT}/libi2pd/*.hpp"
  "${PROJECT_ROOT}/libi2pd_client/*.cpp" "${PROJECT_ROOT}/libi2pd_client/*.h"
  "${PROJECT_ROOT}/libi2pd_client/*.hpp" "${PROJECT_ROOT}/daemon/*.cpp"
  "${PROJECT_ROOT}/daemon/*.h" "${PROJECT_ROOT}/privsep/*.cpp" "${PROJECT_ROOT}/privsep/*.h"
  "${PROJECT_ROOT}/tui/*.cpp" "${PROJECT_ROOT}/tui/*.h" "${PROJECT_ROOT}/tools/*.cpp"
  "${PROJECT_ROOT}/tools/*.hpp" "${PROJECT_ROOT}/i18n/*.cpp" "${PROJECT_ROOT}/i18n/*.h"
  "${PROJECT_ROOT}/tests/*.cpp" "${PROJECT_ROOT}/tests/*.h"
  "${PROJECT_ROOT}/android/app/src/main/cpp/*.cpp" "${PROJECT_ROOT}/Win32/*.cpp"
  "${PROJECT_ROOT}/Win32/*.h")
foreach(_format_source IN LISTS _i2pd_format_sources)
  file(READ "${_format_source}" _format_text)
  string(REGEX MATCH
    "(^|[^A-Za-z0-9_])(sprintf|vsprintf|strcat|strncat|strcpy|strncpy)[[:space:]]*[(]"
    _unsafe_match "${_format_text}")
  if(_unsafe_match)
    message(FATAL_ERROR "unsafe C string function in ${_format_source}: ${_unsafe_match}")
  endif()
endforeach()

message(STATUS "build-system-static: OK")

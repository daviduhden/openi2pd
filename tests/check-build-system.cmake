# See the LICENSE file at the top of the project tree for copyright and
# license details.
#
# openi2pd: check build system.
#
# Static CMake check of the build-system contract: POSIX make syntax,
# Ninja defaults, dependency floors and the absence of hidden downloads,
# globs, hardcoded prefixes and removed compatibility branches.
#

# Standalone scripts run with `cmake -P` have no
# cmake_minimum_required(). CMP0012 OLD treats TRUE/FALSE as
# variable names, so a boolean-valued condition would silently
# take the wrong branch on a CMake 3.x host.
cmake_policy(SET CMP0012 NEW)
# Recursive source inventories must not follow directory symlinks.
cmake_policy(SET CMP0009 NEW)

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
set(fuzz_workflow "${PROJECT_ROOT}/.github/workflows/validation.yml")
set(openbsd_imsg_source "${PROJECT_ROOT}/privsep/OpenBSDImsg.cpp")

read_required("${root_makefile}" root_make)
read_required("${test_makefile}" test_make)
read_required("${top_cmake}" top_cmake_text)
read_required("${tools_cmake}" tools_cmake_text)
read_required("${tests_cmake}" tests_cmake_text)
read_required("${PROJECT_ROOT}/docs/testing.md" testing_doc_text)
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

# POSIX make has no ?= or GNU pattern/functions. The front-end must be
# usable with BSD make on OpenBSD.
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
foreach(target IN ITEMS "all:" "configure:" "build:" "test:" "check:" "clean:")
  string(FIND "${test_make}" "${target}" target_position)
  if(target_position EQUAL -1)
    message(FATAL_ERROR "tests/Makefile shim is missing target '${target}'")
  endif()
endforeach()
assert_present("${test_make}" "\"\${MAKE}\" -C \"..\" "
  "tests/Makefile must forward to the root front-end instead of duplicating configuration")
assert_absent("${test_make}" "CMAKE_GENERATOR = Ninja"
  "tests/Makefile must not carry a second CMake configuration")
string(FIND "${root_make}" "CMAKE_GENERATOR = Ninja" root_generator)
if(root_generator EQUAL -1)
  message(FATAL_ERROR "POSIX Makefile must default to the Ninja CMake generator")
endif()

# No configure-time downloads, globs or hidden source trees in any
# maintained CMake entry point.
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
    "cmake_policy(SET CMP0144 NEW)" "cmake_policy(SET CMP0167 NEW)"
    "I2PD_OPENSSL_EXPECT" "I2PD_OPENSSL_EXPECT_MAJOR" "I2PD_HAVE_EVP_PKEY_IS_A" "I2PD_HAVE_EVP_KDF_FETCH"
    "I2PD_HAVE_EVP_EC_GEN" "I2PD_HAVE_SSL_SET1_DNSNAME" "I2PD_HAVE_SSL_SET1_HOST"
    "I2PD_HAVE_OPENSSL_MLKEM_H" "I2PD_OPENSSL_PROBE_STAMP" "I2PD_OPENSSL_LINK_OK"
    "check_language(RC)" "enable_language(RC)" "CMAKE_RC_COMPILER"
    "list(GET CURSES_LIBRARIES 0 _i2pd_curses_probe)"
    "check_library_exists(\"\${_i2pd_curses_probe}\" wadd_wch" "I2PD_CURSES_HAS_WIDE_ENTRY"
    "i2pd_apply_dialect(i2pd_privsep PRIVATE)" "i2pd_apply_dialect(libi2pd PUBLIC)"
    "Expected.h lives in libi2pd" "MiniUPnPc 2.2.6" "I2PD_ATOMIC_LIBRARIES"
    "WITH_STATIC cannot be combined" "I2PD_REQUIRE_PERL"
    "OPENSSL_SSL_LIBRARY};\${OPENSSL_CRYPTO_LIBRARY")
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
assert_present("${presets_text}" "\"version\": 5" "CMake presets must use the CMake 3.25 schema")
assert_present("${presets_text}" "\"minor\": 25" "Preset schema 5 requires CMake 3.25 (installPresets)")
assert_present("${presets_text}" "\"installPresets\"" "CMake presets must expose the install path")
assert_present("${top_cmake_text}" "COMPONENT Development" "development installs need a component")
assert_present("${top_cmake_text}"
  "ARCHIVE DESTINATION \${CMAKE_INSTALL_LIBDIR} COMPONENT Libraries"
  "static library archives must be assigned to the Libraries component")
assert_present("${top_cmake_text}"
  "LIBRARY DESTINATION \${CMAKE_INSTALL_LIBDIR} COMPONENT Libraries"
  "shared library artifacts must be assigned to the Libraries component")
assert_present("${top_cmake_text}" "COMPONENT Tui" "the TUI needs a separate install component")
assert_present("${top_cmake_text}" "COMPONENT Service" "service files need a separate install component")
assert_present("${top_cmake_text}" "configure_package_config_file"
  "the package config must use configure_package_config_file")
assert_present("${top_cmake_text}" "DESTINATION \${CMAKE_INSTALL_INCLUDEDIR}/privsep"
  "the exported privsep archive must install matching headers")
assert_present("${top_cmake_text}" "PATH_VARS CMAKE_INSTALL_INCLUDEDIR"
  "the package config must derive the include path from PATH_VARS")
assert_present("${top_cmake_text}" "COMPATIBILITY ExactVersion"
  "the installed internal headers must not promise a stable major ABI")
assert_present("${top_cmake_text}" "lib/systemd/system" "systemd units need an architecture-independent path")
assert_absent("${top_cmake_text}" "CMAKE_INSTALL_LIBDIR}/systemd/system"
  "systemd unit destination must not use multiarch LIBDIR")
assert_present("${top_cmake_text}" "COMPONENTS program_options json url"
  "torrent builds require the compiled Boost.JSON/URL components")
assert_present("${top_cmake_text}" "OPTIONAL_COMPONENTS container"
  "static Boost.JSON needs the optional compiled Container library")
assert_present("${top_cmake_text}" "target_link_libraries(libi2pdclient PRIVATE Boost::json Boost::url)"
  "torrent RPC must link the Boost.JSON/URL implementations")
read_required("${PROJECT_ROOT}/build/cmake/i2pdConfig.cmake" package_config_text)
assert_present("${package_config_text}" "cmake_policy(SET CMP0144 NEW)"
  "installed Boost discovery must honor CMP0144 without warning")
assert_present("${package_config_text}" "cmake_policy(SET CMP0167 NEW)"
  "installed Boost discovery must select package config mode without warning")
read_required("${PROJECT_ROOT}/android/app/src/main/cpp/CMakeLists.txt" android_wrapper_text)
read_required("${PROJECT_ROOT}/android/app/build.gradle" android_gradle_text)
assert_present("${android_wrapper_text}" "CMAKE_FIND_ROOT_PATH_MODE_LIBRARY BOTH"
  "the Android wrapper must search vcpkg prefixes outside the NDK sysroot")
assert_present("${android_gradle_text}" "-DANDROID_STL=c++_shared"
  "the Android JNI build must use the vcpkg c++_shared STL")
assert_present("${android_gradle_text}" "i2pd.abis"
  "the Android ABI list must be property-driven and default to the built prefix")
assert_present("${package_config_text}" "@I2PD_BOOST_MIN_VERSION@"
  "installed consumers must discover the configured Boost floor")
assert_present("${package_config_text}" "COMPONENTS @I2PD_BOOST_COMPONENTS@"
  "installed consumers must discover the configured Boost components")
assert_present("${package_config_text}" "OPTIONAL_COMPONENTS @I2PD_BOOST_OPTIONAL_COMPONENTS@"
  "installed consumers must discover the optional Boost.Container component")
assert_present("${package_config_text}" "find_dependency(OpenSSL COMPONENTS Crypto SSL)"
  "the package config must request both OpenSSL components")
assert_present("${package_config_text}" "find_dependency(ZLIB 1.2.11)"
  "the package config must keep the zlib floor")
assert_absent("${top_cmake_text}" "set(CMAKE_MODULE_PATH"
  "CMake module path must be prepended rather than overwritten")
# OpenBSD 7.7-7.9 expose imsg_get(); 8.0 replaced it with imsgbuf_get().
assert_present("${top_cmake_text}" "check_symbol_exists(imsgbuf_get"
  "the OpenBSD imsg API must be probed instead of hard-coding a release")
read_required("${openbsd_imsg_source}" openbsd_imsg_text)
assert_present("${openbsd_imsg_text}" "imsg_get (m_Buffer"
  "the OpenBSD adapter must keep the pre-8.0 imsg_get() fallback")
assert_present("${openbsd_imsg_text}" "imsgbuf_get (m_Buffer"
  "the OpenBSD adapter must support the 8.0 imsgbuf_get() API")
assert_present("${top_cmake_text}" "CMAKE_GENERATOR MATCHES \"Makefiles"
  "OpenBSD must reject the Unix Makefiles generator")
assert_present("${top_cmake_text}" "BOOST_ASIO_HAS_SNPRINTF=1"
  "Boost.Asio scope-ID formatting must select the bounded snprintf path")
assert_present("${top_cmake_text}" "target_link_libraries(libi2pd PUBLIC ws2_32 mswsock iphlpapi shell32 advapi32)"
  "Windows static consumers need one merged import-library contract")
assert_absent("${top_cmake_text}" "wsock32" "obsolete Winsock 1.1 import library")
assert_absent("${top_cmake_text}" "crypt32" "unused crypt32 import library")
assert_present("${top_cmake_text}" "-Wl,--dynamicbase -Wl,--nxcompat"
  "MinGW hardening must use PE rather than ELF linker options")
assert_present("${top_cmake_text}"
  "option(I2PD_WARNINGS_AS_ERRORS \"Treat project target warnings as errors\"    OFF)"
  "strict diagnostics must remain an explicit opt-in")
assert_present("${top_cmake_text}"
  "if(I2PD_WARNINGS_AS_ERRORS)\n  if(MSVC)\n    target_compile_options(i2pd_warnings INTERFACE /WX)\n  else()\n    target_compile_options(i2pd_warnings INTERFACE -Werror)\n  endif()\nendif()"
  "strict diagnostics must apply through the project warning interface")
assert_present("${top_cmake_text}" "target_compile_options(i2pd_warnings INTERFACE /GS /sdl /guard:cf)"
  "MSVC hardening must enable stack checks, SDL checks and Control Flow Guard")
assert_present("${top_cmake_text}" "target_link_options(i2pd_warnings INTERFACE /guard:cf /DYNAMICBASE /NXCOMPAT)"
  "MSVC hardening must enable Control Flow Guard, ASLR and DEP")
assert_present("${top_cmake_text}" "target_link_options(i2pd_warnings INTERFACE /HIGHENTROPYVA)"
  "64-bit MSVC hardening must enable high-entropy ASLR")
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
assert_present("${check_atomic_text}" "I2PD_ATOMIC_LIBRARIES"
  "CheckAtomic.cmake must report the required atomic library")
assert_present("${check_atomic_text}" "set(CMAKE_REQUIRED_LIBRARIES \"\${_i2pd_saved_required_libraries}\")"
  "CheckAtomic.cmake must restore the probe library state")
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

# Windows packaging must not assume an installation drive/directory or
# pull the worktree from a remote during a local build.
foreach(mingw_needle IN ITEMS "C:\\msys64" "C:\\PROGRA~2" "git pull")
  assert_absent("${mingw_text}" "${mingw_needle}" "Windows helper hardcodes or downloads")
endforeach()

# The retired administrative web console must not reappear in the source
# graph. HTTP proxy/tunnel and reseed services are separate and
# retained.
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
    "I2PD_INSTALL_CONFIG"
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

# Service-manager installation is opt-in and exclusive. Each canonical
# source must occur exactly once in the top-level install graph, so a
# second unconditional install cannot silently ship every init system.
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
# dinit and Upstart are sample definitions, not selectable install
# components.
foreach(sample IN ITEMS dinit upstart)
  string(FIND "${top_cmake_text}" "\"${sample}\"" sample_position)
  if(NOT sample_position EQUAL -1)
    message(FATAL_ERROR
      "I2PD_SERVICE_MANAGER must not offer the uninstalled ${sample} sample")
  endif()
endforeach()
foreach(service_source IN ITEMS
    "contrib/i2pd.service"
    "contrib/sysvinit/i2pd"
    "contrib/openrc/i2pd.openrc"
    "contrib/openrc/conf.d/i2pd"
    "contrib/runit/i2pd/run"
    "contrib/runit/i2pd/finish"
    "contrib/runit/i2pd/log/run"
    "contrib/runit/i2pd/log/config"
    "contrib/s6/i2pd/run"
    "contrib/s6/i2pd/finish"
    "contrib/s6/i2pd/down-signal"
    "contrib/s6/i2pd/timeout-kill"
    "contrib/s6/i2pd/timeout-finish"
    "contrib/s6-rc/source/i2pd/run"
    "contrib/s6-rc/source/i2pd/finish"
    "contrib/s6-rc/source/i2pd/type"
    "contrib/s6-rc/source/i2pd/down-signal"
    "contrib/s6-rc/source/i2pd/timeout-kill"
    "contrib/s6-rc/source/i2pd/timeout-finish"
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

# Distribution payload ownership: documentation, licences and man pages
# are installed by the component that owns the binary, not only when the
# optional documentation tree is enabled.
foreach(payload IN ITEMS
    "RENAME LICENSE.i2pd-tools"
    "man/i2pd.1"
    "man/i2pd-tui.1"
    "man/i2pdctl.1")
  string(FIND "${top_cmake_text}" "${payload}" payload_position)
  if(payload_position EQUAL -1)
    message(FATAL_ERROR "CMake install graph is missing ${payload}")
  endif()
endforeach()

# Every executable component owns its man page on every platform that has
# components, not only on UNIX: the Windows/MSVC install builds the same
# Runtime/Tui/Tools components and tools/test-perl-install.cmake checks
# share/man/man1/i2pdctl.1 in the Tools component there. A UNIX-only guard
# is therefore a defect, and Android (an APK, no man tree) is the single
# exclusion. The component is read from the install rule itself, so a page
# cannot drift to another package.
foreach(page_owner IN ITEMS
    "man/i2pd.1|Runtime"
    "man/i2pd-tui.1|Tui"
    "man/i2pdctl.1|Tools")
  string(REPLACE "|" ";" page_pair "${page_owner}")
  list(GET page_pair 0 page_name)
  list(GET page_pair 1 page_component)
  string(FIND "${top_cmake_text}" "${page_name}" page_position)
  if(page_position EQUAL -1)
    message(FATAL_ERROR "CMake install graph is missing ${page_name}")
  endif()
  # Read to this install statement's closing delimiter. A shared tool
  # manual list may span more than an arbitrary fixed lookahead window.
  string(SUBSTRING "${top_cmake_text}" ${page_position} -1 page_tail)
  string(FIND "${page_tail}" ")" page_rule_end)
  if(page_rule_end EQUAL -1)
    message(FATAL_ERROR "Unterminated man-page install rule: ${page_name}")
  endif()
  string(SUBSTRING "${page_tail}" 0 ${page_rule_end} page_rule)
  string(FIND "${page_rule}" "COMPONENT ${page_component}" page_component_position)
  if(page_component_position EQUAL -1)
    message(FATAL_ERROR
      "${page_name} must be installed by the ${page_component} component")
  endif()
  # The 400 characters before the rule are its guard context: a UNIX-only
  # condition there would silently drop the page from a Windows install.
  set(context_start 0)
  if(page_position GREATER 400)
    math(EXPR context_start "${page_position} - 400")
  endif()
  math(EXPR context_length "${page_position} - ${context_start}")
  string(SUBSTRING "${top_cmake_text}" ${context_start} ${context_length}
    page_context)
  string(FIND "${page_context}" "if(UNIX AND NOT ANDROID)" unix_page_guard)
  if(NOT unix_page_guard EQUAL -1)
    message(FATAL_ERROR
      "${page_name} is installed under a UNIX-only guard; man pages belong to "
      "every platform component and only Android is excluded")
  endif()
endforeach()
# The sample configuration is a hard prerequisite of the packaged
# services.
string(FIND "${top_cmake_text}"
  "I2PD_SERVICE_MANAGER=\${I2PD_SERVICE_MANAGER} requires " config_guard)
if(config_guard EQUAL -1)
  message(FATAL_ERROR "CMake must reject a service manager without I2PD_INSTALL_CONFIG")
endif()

string(FIND "${root_make}" "CMAKE_GENERATOR = Ninja" generator_position)
if(generator_position EQUAL -1)
  message(FATAL_ERROR "The POSIX front-end must default to the Ninja generator")
endif()

# Every checked-in regression source must be named in
# tests/CMakeLists.txt. Platform-specific sources may still be
# conditionally registered, but a new file cannot silently fall out of
# the suite.
file(GLOB _i2pd_test_sources RELATIVE "${PROJECT_ROOT}/tests" "${PROJECT_ROOT}/tests/test-*.cpp")
foreach(_source IN LISTS _i2pd_test_sources)
  string(REGEX REPLACE "\\.cpp$" "" _test_name "${_source}")
  # Match a complete token, not a prefix such as test-addressbook inside
  # test-addressbook-admin. CMake list entries are separated by
  # whitespace.
  string(FIND "${tests_cmake_text}" "\n  ${_test_name}\n" _test_line)
  string(FIND "${tests_cmake_text}" " ${_test_name} " _test_inline)
  string(FIND "${tests_cmake_text}" " ${_test_name}\n" _test_eol)
  string(FIND "${tests_cmake_text}" " ${_test_name})" _test_paren)
  if(_test_line EQUAL -1 AND _test_inline EQUAL -1 AND _test_eol EQUAL -1 AND _test_paren EQUAL -1)
    message(FATAL_ERROR "tests/CMakeLists.txt does not register ${_test_name} as a complete target name")
  endif()
endforeach()

# Every unit test echoes the product's own records to stderr; otherwise a
# failing assertion aborts with its diagnostics still only in the
# administrative ring, invisible to CTest.
file(READ "${PROJECT_ROOT}/libi2pd/Log.h" log_header_text)
file(READ "${PROJECT_ROOT}/libi2pd/Log.cpp" log_source_text)
string(FIND "${tests_cmake_text}" "log-support.cpp" _test_log_support)
string(FIND "${log_header_text}" "SetSynchronousSink" _test_log_api)
string(FIND "${log_source_text}" "m_SynchronousSink" _test_log_append)
string(FIND "${testing_doc_text}" "OPENI2PD_TEST_LOG_LEVEL" _test_log_doc)
if(_test_log_support EQUAL -1 OR _test_log_api EQUAL -1 OR _test_log_append EQUAL -1 OR _test_log_doc EQUAL -1)
  message(FATAL_ERROR
    "unit tests must link tests/log-support.cpp, whose synchronous logger sink "
    "is documented in docs/testing.md")
endif()

# The compile-free Doxygen parameter guard must stay registered: the
# source-docs lane is the only place Doxygen itself runs, so a static lane
# is what catches the recurring partial-@param defect before it.
foreach(_marker IN ITEMS "doxygen-static" "check-doxygen-params.pl")
  string(FIND "${tests_cmake_text}" "${_marker}" _doxygen_marker)
  if(_doxygen_marker EQUAL -1)
    message(FATAL_ERROR "tests/CMakeLists.txt must register the ${_marker} contract")
  endif()
endforeach()

# The duplicate-@param rules (one block, and a definition repeating its
# header declaration) are proven only by the Perl fixture suite. Markers
# avoid a backslash escape for the tag itself in this CMake string.
file(READ "${PROJECT_ROOT}/tools/tests/maintenance-checkers.t" _doxygen_fixtures)
foreach(_marker IN ITEMS "documentation for Load" "in one documentation block"
    "unresolved \\@ref target" "Javadoc inline tag in a C"
    "unbalanced \\@verbatim")
  string(FIND "${_doxygen_fixtures}" "${_marker}" _doxygen_fixture)
  if(_doxygen_fixture EQUAL -1)
    message(FATAL_ERROR
      "tools/tests/maintenance-checkers.t must keep the doxygen duplicate check for '${_marker}'")
  endif()
endforeach()

# The fuzzer inventory is also explicit: adding a source without wiring
# it to the manual Fuzzing workflow must fail the static check.
file(GLOB _i2pd_fuzz_sources RELATIVE "${PROJECT_ROOT}/tests/fuzz" "${PROJECT_ROOT}/tests/fuzz/*.cpp")
foreach(_source IN LISTS _i2pd_fuzz_sources)
  string(REGEX REPLACE "\\.cpp$" "" _fuzzer_name "${_source}")
  string(FIND "${tests_cmake_text}" "\n  ${_fuzzer_name}\n" _fuzzer_line)
  string(FIND "${tests_cmake_text}" " ${_fuzzer_name} " _fuzzer_inline)
  string(FIND "${tests_cmake_text}" " ${_fuzzer_name}\n" _fuzzer_eol)
  string(FIND "${tests_cmake_text}" " ${_fuzzer_name})" _fuzzer_paren)
  if(_fuzzer_line EQUAL -1 AND _fuzzer_inline EQUAL -1 AND _fuzzer_eol EQUAL -1 AND _fuzzer_paren EQUAL -1)
    message(FATAL_ERROR "tests/CMakeLists.txt does not register ${_fuzzer_name}")
  endif()
  string(REGEX MATCHALL "${_fuzzer_name}" _fuzz_workflow_matches "${fuzz_workflow_text}")
  list(LENGTH _fuzz_workflow_matches _fuzz_workflow_count)
  if(_fuzz_workflow_count LESS 2)
    message(FATAL_ERROR
      ".github/workflows/validation.yml must list ${_fuzzer_name} in both the build and run loops")
  endif()
  string(FIND "${testing_doc_text}" "${_fuzzer_name}" _fuzz_doc_position)
  if(_fuzz_doc_position EQUAL -1)
    message(FATAL_ERROR "docs/testing.md does not document ${_fuzzer_name}")
  endif()
endforeach()

# Reproducible corpus contract: each registered fuzzer has at least one
# checked-in seed, no orphan corpus directory exists, every seed is
# small and no seed is named like private key material.
file(GLOB _fuzz_corpus_dirs LIST_DIRECTORIES true
  "${PROJECT_ROOT}/tests/fuzz/corpus/*")
foreach(_corpus_dir IN LISTS _fuzz_corpus_dirs)
  get_filename_component(_corpus_name "${_corpus_dir}" NAME)
  string(FIND "${tests_cmake_text}" " ${_corpus_name}" _corpus_registered)
  if(_corpus_registered EQUAL -1)
    message(FATAL_ERROR "fuzz corpus directory has no registered target: ${_corpus_name}")
  endif()
  file(GLOB _corpus_files "${_corpus_dir}/*")
  list(LENGTH _corpus_files _corpus_count)
  if(_corpus_count EQUAL 0)
    message(FATAL_ERROR "fuzz corpus directory is empty: ${_corpus_name}")
  endif()
  foreach(_seed IN LISTS _corpus_files)
    get_filename_component(_seed_name "${_seed}" NAME)
    string(TOLOWER "${_seed_name}" _seed_lower)
    if(_seed_lower MATCHES "private|secret|\\.key$|\\.dat$|\\.pem$|^id_")
      message(FATAL_ERROR "fuzz corpus seed looks like credential material: ${_seed}")
    endif()
    file(SIZE "${_seed}" _seed_size)
    if(_seed_size GREATER 65536)
      message(FATAL_ERROR "fuzz corpus seed exceeds 64 KiB: ${_seed}")
    endif()
  endforeach()
endforeach()
# Every registered fuzzer must have a seed directory so a clean clone
# starts from a reproducible corpus instead of an empty temporary
# directory.
file(GLOB _registered_fuzzers RELATIVE "${PROJECT_ROOT}/tests/fuzz"
  "${PROJECT_ROOT}/tests/fuzz/fuzz-*.cpp")
foreach(_source IN LISTS _registered_fuzzers)
  string(REGEX REPLACE "\\.cpp$" "" _fuzzer "${_source}")
  if(NOT IS_DIRECTORY "${PROJECT_ROOT}/tests/fuzz/corpus/${_fuzzer}")
    message(FATAL_ERROR "fuzzer has no checked-in corpus directory: ${_fuzzer}")
  endif()
endforeach()

# Test-registration contract. AGENTS.md requires an explicit TIMEOUT for
# every registered test and a parallel-safety decision for every fixture:
# `i2pd_add_test()` owns the shared budget while each direct
# registration states its own. A test that creates a fixed path below the
# system temporary directory (no per-run suffix and no mkdtemp template)
# must also be serialised through RUN_SERIAL or the shared
# openi2pd-fixed-tmp resource lock, otherwise two CTest workers race for
# the same tree.
#
# Each registration is anchored on its own name and on the
# set_tests_properties() call for that name, because a positional window
# would let the next registration's TIMEOUT mask a test that states none.
set(_add_test_prefix "add_test(NAME ")
string(LENGTH "${_add_test_prefix}" _add_test_prefix_length)
set(_properties_prefix "set_tests_properties(")
string(LENGTH "${_properties_prefix}" _properties_prefix_length)
# Offsets: (1) the first character after "add_test(NAME " starts the
# registered name, and (2) string(FIND) has no start offset, so each
# iteration searches the remaining suffix and converts the relative match
# back to an absolute position.
set(_add_test_offset 0)
set(_add_test_search "${tests_cmake_text}")
set(_add_test_count 0)
while(1)
  string(FIND "${_add_test_search}" "${_add_test_prefix}" _add_test_relative)
  if(_add_test_relative EQUAL -1)
    break()
  endif()
  math(EXPR _add_test_position "${_add_test_offset} + ${_add_test_relative}")
  math(EXPR _add_test_name_offset "${_add_test_position} + ${_add_test_prefix_length}")
  # A length of -1 is not "up to the last byte": it returns the remainder
  # of the string, which is exactly the suffix this scan needs.
  string(SUBSTRING "${tests_cmake_text}" ${_add_test_name_offset} -1 _add_test_name_tail)
  string(REGEX MATCH "^[^ \t\r\n)]+" _add_test_name "${_add_test_name_tail}")
  if(_add_test_name STREQUAL "")
    message(FATAL_ERROR
      "tests/CMakeLists.txt has an add_test(NAME) registration without a name")
  endif()
  math(EXPR _add_test_count "${_add_test_count} + 1")
  string(FIND "${tests_cmake_text}" "${_properties_prefix}${_add_test_name}"
    _add_test_properties)
  if(_add_test_properties EQUAL -1)
    message(FATAL_ERROR
      "tests/CMakeLists.txt registers '${_add_test_name}' without "
      "set_tests_properties(); every registration must state its own TIMEOUT")
  endif()
  # The property call ends where the next statement starts; cutting the
  # slice there keeps a neighbouring registration or property call from
  # supplying the TIMEOUT.
  math(EXPR _add_test_properties_body "${_add_test_properties} + ${_properties_prefix_length}")
  string(SUBSTRING "${tests_cmake_text}" ${_add_test_properties_body} -1
    _add_test_properties_tail)
  set(_add_test_properties_length -1)
  foreach(_boundary IN ITEMS "${_properties_prefix}" "${_add_test_prefix}")
    string(FIND "${_add_test_properties_tail}" "${_boundary}" _boundary_offset)
    if(NOT _boundary_offset EQUAL -1)
      if(_add_test_properties_length EQUAL -1 OR _boundary_offset LESS _add_test_properties_length)
        set(_add_test_properties_length ${_boundary_offset})
      endif()
    endif()
  endforeach()
  string(SUBSTRING "${_add_test_properties_tail}" 0 ${_add_test_properties_length}
    _add_test_properties_call)
  string(FIND "${_add_test_properties_call}" "TIMEOUT" _add_test_timeout)
  if(_add_test_timeout EQUAL -1)
    message(FATAL_ERROR
      "tests/CMakeLists.txt registers '${_add_test_name}' without an "
      "explicit TIMEOUT in its set_tests_properties() call; do not fall back "
      "to the CTest default")
  endif()
  math(EXPR _add_test_offset "${_add_test_position} + ${_add_test_prefix_length}")
  string(SUBSTRING "${tests_cmake_text}" ${_add_test_offset} -1 _add_test_search)
endwhile()
if(_add_test_count EQUAL 0)
  message(FATAL_ERROR "tests/CMakeLists.txt registers no tests")
endif()

string(FIND "${tests_cmake_text}" "RESOURCE_LOCK openi2pd-fixed-tmp" _fixed_tmp_lock)
if(_fixed_tmp_lock EQUAL -1)
  message(FATAL_ERROR
    "tests/CMakeLists.txt must keep the openi2pd-fixed-tmp RESOURCE_LOCK assignment")
endif()
# Collect the text around both isolation mechanisms, because either makes a
# fixed-fixture test safe: RUN_SERIAL keeps a test out of every parallel
# slot, and the resource lock excludes the tests that are already known to
# share one. A slice around only the lock would reject a test that chose
# the other mechanism.
set(_serialised_text "")
foreach(_marker IN ITEMS "RESOURCE_LOCK openi2pd-fixed-tmp" "RUN_SERIAL TRUE")
  string(FIND "${tests_cmake_text}" "${_marker}" _marker_position)
  if(_marker_position EQUAL -1)
    continue()
  endif()
  math(EXPR _serialised_begin "${_marker_position} - 1400")
  if(_serialised_begin LESS 0)
    set(_serialised_begin 0)
  endif()
  math(EXPR _serialised_length "${_marker_position} - ${_serialised_begin}")
  string(SUBSTRING "${tests_cmake_text}" ${_serialised_begin} ${_serialised_length} _serialised_slice)
  string(APPEND _serialised_text "\n${_serialised_slice}")
endforeach()

file(GLOB _cpp_test_sources RELATIVE "${PROJECT_ROOT}/tests" "${PROJECT_ROOT}/tests/test-*.cpp")
foreach(_test_source IN LISTS _cpp_test_sources)
  file(READ "${PROJECT_ROOT}/tests/${_test_source}" _test_source_text)
  set(_fixed_fixture FALSE)
  string(REGEX MATCHALL "temp_directory_path \\(\\)[^;]*" _temp_calls "${_test_source_text}")
  foreach(_temp_call IN LISTS _temp_calls)
    if(_temp_call MATCHES "to_string|getpid|GetCurrentProcessId")
      continue()
    endif()
    if(_temp_call MATCHES "\"")
      set(_fixed_fixture TRUE)
    endif()
  endforeach()
  # A bare `auto base = temp_directory_path ();` carries no per-run suffix,
  # so a later `base / "name"` is the same fixed path in a second
  # statement. A computed leaf (`base / ("name" + std::to_string (...))`)
  # is intentionally not matched here.
  #
  # The scan advances manually with REGEX MATCH instead of collecting a
  # MATCHALL list: a match that ends at the statement's semicolon would be
  # split into two list elements, so the "bare statement" test would pass
  # for `root = temp_directory_path () / ("per-run" + ...)` and flag the
  # per-run fixtures.
  set(_temp_base_rest "${_test_source_text}")
  while(1)
    string(REGEX MATCH
      "([A-Za-z_][A-Za-z0-9_]*) = (std::filesystem::|fs::|)temp_directory_path \\(\\)[ \t\r\n]*;"
      _temp_base "${_temp_base_rest}")
    if(_temp_base STREQUAL "")
      break()
    endif()
    string(REGEX REPLACE "^([A-Za-z_][A-Za-z0-9_]*) = .*$" "\\1" _temp_base_name "${_temp_base}")
    if(NOT _temp_base_name STREQUAL "")
      string(FIND "${_test_source_text}" "${_temp_base_name} / \"" _temp_base_leaf)
      if(NOT _temp_base_leaf EQUAL -1)
        set(_fixed_fixture TRUE)
      endif()
    endif()
    string(LENGTH "${_temp_base}" _temp_base_length)
    string(FIND "${_temp_base_rest}" "${_temp_base}" _temp_base_position)
    if(_temp_base_position EQUAL -1)
      break()
    endif()
    math(EXPR _temp_base_next "${_temp_base_position} + ${_temp_base_length}")
    string(SUBSTRING "${_temp_base_rest}" ${_temp_base_next} -1 _temp_base_rest)
  endwhile()
  string(REGEX MATCHALL "char [A-Za-z0-9_]+\\[\\] = \"/tmp/[^\"]*\"" _tmp_buffers "${_test_source_text}")
  foreach(_tmp_buffer IN LISTS _tmp_buffers)
    if(NOT _tmp_buffer MATCHES "XXXXXX")
      set(_fixed_fixture TRUE)
    endif()
  endforeach()
  if(_fixed_fixture)
    string(REGEX REPLACE "\\.cpp$" "" _fixed_test_name "${_test_source}")
    string(FIND "${_serialised_text}" "${_fixed_test_name}" _serialised_test)
    if(_serialised_test EQUAL -1)
      message(FATAL_ERROR
        "${_fixed_test_name} creates a fixed temporary path but is neither RUN_SERIAL "
        "nor locked by openi2pd-fixed-tmp; use a per-run name or add the lock")
    endif()
  endif()
endforeach()

# The RPM specs must not invoke GNU make or Ninja directly: they build
# through cmake --build and pin the Ninja generator explicitly. Some
# Fedora releases package the required Boost.JSON/URL libraries
# separately, so the guarded build requirements must keep a defined
# distro-macro test.
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
  assert_present("${_rpm_text}" "0%{?fedora}"
    "RPM spec lacks the Fedora Boost.JSON/URL requirement guard")
  assert_absent("${_rpm_text}" "0%{?fedora} <= 43"
    "RPM spec still caps the Boost.JSON/URL requirement at Fedora 43")
  assert_present("${_rpm_text}" "boost-json"
    "RPM spec lacks the Boost.JSON build dependency")
  assert_present("${_rpm_text}" "boost-url"
    "RPM spec lacks the Boost.URL build dependency")
  assert_present("${_rpm_text}" "cmake --build" "RPM spec lacks cmake --build")
endforeach()

# Keep first-party source free of the unbounded C string functions. The
# token boundary prevents std::snprintf from matching;
# Boost/OpenSSL/system headers are excluded.
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

# Every OpenSSL/LibreSSL capability probe must be exported as a compile
# definition and have a Crypto.h fallback, so a target that includes the
# header without CMake's definitions cannot silently select a different
# branch.
file(READ "${PROJECT_ROOT}/libi2pd/Crypto.h" crypto_feature_text)
file(READ "${PROJECT_ROOT}/privsep/Worker.h" worker_header_text)
assert_present("${worker_header_text}" "worker_pid_t"
  "public Worker.h must alias the POSIX pid type for Windows parsers")
string(REGEX MATCH "set\\(I2PD_OPENSSL_PROBES[^)]*\\)" openssl_probes_block "${top_cmake_text}")
string(REGEX MATCHALL "I2PD_HAVE_[A-Z0-9_]+" crypto_probe_names "${openssl_probes_block}")
list(REMOVE_DUPLICATES crypto_probe_names)
foreach(probe_name IN LISTS crypto_probe_names)
  string(FIND "${top_cmake_text}" "${probe_name}=$<BOOL:" probe_definition)
  if(probe_definition EQUAL -1)
    message(FATAL_ERROR "CMake probe ${probe_name} is not exported as a compile definition")
  endif()
  string(FIND "${crypto_feature_text}" "#ifndef ${probe_name}" probe_fallback)
  if(probe_fallback EQUAL -1)
    message(FATAL_ERROR "Crypto.h has no fallback for probe ${probe_name}")
  endif()
endforeach()

file(READ "${PROJECT_ROOT}/.github/workflows/validation.yml" build_workflow_text)
assert_present("${build_workflow_text}" "mkdir -p docs/generated/source-api"
  "source-docs job must create the gitignored Doxygen output directory")

# Release assertions must survive without conflicting MSVC command macros.
read_required("${PROJECT_ROOT}/build/cmake/TestAssertions.cmake" assertion_helper)
assert_present("${assertion_helper}" "/FI\${PROJECT_ROOT}/tests/TestAssertions.h"
  "MSVC tests need source-level assertion initialization")
assert_present("${assertion_helper}" "PRIVATE -UNDEBUG"
  "GNU-style test drivers must preserve Release assertions")
read_required("${PROJECT_ROOT}/tests/TestAssertions.h" assertion_header)
assert_present("${assertion_header}" "#undef NDEBUG"
  "MSVC assertion header must revoke the Release macro")
assert_present("${assertion_header}" "#include <cassert>"
  "MSVC assertion header must initialize assertions after undefining NDEBUG")
foreach(consumer tests/CMakeLists.txt tools/CMakeLists.txt)
  read_required("${PROJECT_ROOT}/${consumer}" assertion_consumer)
  assert_present("${assertion_consumer}" "i2pd_enable_test_assertions("
    "Test assertion helper must cover ${consumer}")
endforeach()
read_required("${PROJECT_ROOT}/tests/test-release-assertions.cpp" assertion_regression)
assert_present("${assertion_regression}" "assert (++evaluations == 1);"
  "Release regression must evaluate an assertion operand")
read_required("${PROJECT_ROOT}/.github/workflows/build-windows.yml" assertion_workflow)
assert_present("${assertion_workflow}" "test-profile-static-lifetime test-release-assertions"
  "Selective MSVC builds must produce the assertion regression")
assert_present("${assertion_workflow}" "|release-assertions))$"
  "Selective MSVC CTest must execute the assertion regression")

message(STATUS "build-system-static: OK")

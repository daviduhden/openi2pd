# See the LICENSE file at the top of the project tree for copyright
# and license details.
#
# openi2pd: check packaging.
#
# Compile-free checks for the POSIX front-end and the maintained packaging
# files. This script never configures, builds, installs or runs a package.
#

if(NOT PROJECT_ROOT)
  message(FATAL_ERROR "PROJECT_ROOT is required")
endif()

function(read_file path out_var)
  if(NOT EXISTS "${path}")
    message(FATAL_ERROR "Missing packaging file: ${path}")
  endif()
  file(READ "${path}" contents)
  set(${out_var} "${contents}" PARENT_SCOPE)
endfunction()

function(assert_contains text needle description)
  string(FIND "${text}" "${needle}" position)
  if(position EQUAL -1)
    message(FATAL_ERROR "${description}: missing '${needle}'")
  endif()
endfunction()

function(assert_absent text needle description)
  string(FIND "${text}" "${needle}" position)
  if(NOT position EQUAL -1)
    message(FATAL_ERROR "${description}: forbidden '${needle}'")
  endif()
endfunction()

read_file("${PROJECT_ROOT}/Makefile" root_make)
read_file("${PROJECT_ROOT}/tests/Makefile" test_make)
read_file("${PROJECT_ROOT}/build/CMakeLists.txt" top_cmake)
read_file("${PROJECT_ROOT}/build/cmake/i2pdConfig.cmake" package_config)
read_file("${PROJECT_ROOT}/contrib/rpm/i2pd.spec" rpm)
read_file("${PROJECT_ROOT}/contrib/rpm/i2pd-git.spec" rpm_git)
read_file("${PROJECT_ROOT}/contrib/static/package.sh" static_package)
read_file("${PROJECT_ROOT}/contrib/static/debian/control" static_debian_control)
read_file("${PROJECT_ROOT}/contrib/static/debian/source/format" static_source_format)
read_file("${PROJECT_ROOT}/contrib/openbsd/port/Makefile" openbsd_port_make)
read_file("${PROJECT_ROOT}/contrib/openbsd/port/PLIST" openbsd_plist)
read_file("${PROJECT_ROOT}/debian/control" debian_control)
read_file("${PROJECT_ROOT}/debian/i2pd.install" debian_i2pd_install)
read_file("${PROJECT_ROOT}/debian/i2pd-tui.install" debian_tui_install)
read_file("${PROJECT_ROOT}/debian/watch" debian_watch)
read_file("${PROJECT_ROOT}/contrib/docker/Dockerfile" docker)

foreach(make_text IN ITEMS "${root_make}" "${test_make}")
  assert_contains("${make_text}" ".PHONY:" "POSIX make must declare phony targets")
  assert_contains("${make_text}" "CMAKE_GENERATOR = Ninja" "POSIX make must default to Ninja")
  assert_absent("${make_text}" "CMAKE_GENERATOR = Unix Makefiles"
    "POSIX make must not select Unix Makefiles")
endforeach()
assert_contains("${root_make}" "install: build" "install must build before staging")
assert_contains("${root_make}" "DESTDIR =" "install must expose DESTDIR")
assert_contains("${root_make}" "PREFIX =" "install must expose PREFIX")
assert_contains("${root_make}" "refusing to remove unsafe BUILD_DIR" "clean must guard BUILD_DIR")

assert_contains("${top_cmake}" "COMPONENT Development" "headers/export need a development component")
assert_contains("${top_cmake}" "configure_package_config_file"
  "the exported package config must use configure_package_config_file")
assert_contains("${package_config}" "@PACKAGE_INIT@"
  "the exported package config must initialize a relocatable prefix")
assert_contains("${package_config}" "check_required_components(i2pd)"
  "the exported package config must validate requested components")
assert_contains("${top_cmake}" "lib/systemd/system" "systemd units need a non-multiarch path")
assert_absent("${top_cmake}" "CMAKE_INSTALL_LIBDIR}/systemd/system"
  "systemd units must not use multiarch LIBDIR")

foreach(spec IN ITEMS "${rpm}" "${rpm_git}")
  assert_contains("${spec}" "BuildRequires: cmake >= 3.24" "RPM must require CMake 3.24")
  assert_contains("${spec}" "BuildRequires: ninja-build" "RPM must require Ninja")
  assert_contains("${spec}" "BuildRequires: boost-json" "RPM must require Boost.JSON headers")
  assert_contains("${spec}" "BuildRequires: boost-url" "RPM must require Boost.URL headers")
  assert_absent("${spec}" "openssl-devel-engine" "RPM must not require the removed ENGINE package")
  assert_contains("${spec}" "_cmake_generator Ninja" "RPM must pin the Ninja generator")
endforeach()
assert_contains("${rpm_git}" "%{_datadir}/i2pd/certificates" "i2pd-git certificate link must target i2pd")
assert_absent("${rpm_git}" "%{_datadir}/%{name}/certificates" "i2pd-git must not create a dangling link")

assert_contains("${static_package}" "debian/i2pd.tmpfiles" "static package must copy tmpfiles")
assert_absent("${static_package}" "contrib/debian/i2pd.tmpfile" "static package must not use removed path")
assert_contains("${static_package}" "i2pd (%s)" "static package must use a native version")
assert_absent("${static_package}" "i2pd (%s-1)" "static native package must not add a Debian revision")
assert_contains("${static_source_format}" "3.0 (native)" "static source format must be native")
assert_contains("${static_debian_control}" "Rules-Requires-Root: no"
  "static package must not require root for binary targets")
assert_absent("${static_debian_control}" "Rules-Requires-Root: binary-targets"
  "static package still uses the obsolete binary-targets value")

assert_contains("${debian_control}" "Pre-Depends: \${misc:Pre-Depends}"
  "main package must keep the debhelper init Pre-Depends substitution")
assert_absent("${debian_control}" "Pre-Depends: \${misc:Pre-Depends}, adduser"
  "main package must not put adduser in Pre-Depends")
assert_contains("${debian_control}" "\${perl:Depends}" "main package must use dh_perl substitution")
assert_contains("${debian_i2pd_install}" "LICENSE.i2pd-tools"
  "daemon package must claim the tools licence notice")
assert_contains("${debian_i2pd_install}" "CC-BY-SA-4.0"
  "daemon package must claim the documentation licence notice")
assert_absent("${debian_i2pd_install}" "openi2pd.desktop"
  "daemon package must not own the TUI desktop entry")
assert_contains("${debian_tui_install}" "openi2pd.desktop"
  "TUI package must own the desktop entry")
assert_contains("${debian_watch}" "archive/(?:refs/tags/)?v?"
  "uscan must accept GitHub archive URLs")

assert_contains("${openbsd_port_make}" "-DI2PD_SERVICE_MANAGER=none"
  "OpenBSD port must not install a second service through CMake")
assert_contains("${openbsd_port_make}" "CONFIGURE_STYLE ="
  "OpenBSD port must use the CMake module")
assert_contains("${openbsd_port_make}" "NO_TEST ="
  "OpenBSD port must state that CTest is not a package-build test")
assert_contains("${openbsd_port_make}" "-DWITH_HARDENING=ON"
  "OpenBSD package must enable hardening")
assert_contains("${openbsd_plist}" "@newgroup _i2pd:838"
  "OpenBSD PLIST must reserve the service group")
assert_contains("${openbsd_plist}" "@newuser _i2pd:838:838::i2pd account:/var/i2pd:/sbin/nologin"
  "OpenBSD PLIST must reserve the service account")
assert_contains("${openbsd_plist}" "@rcscript"
  "OpenBSD PLIST must register the rc.d script")
assert_contains("${openbsd_plist}" "RCDIR"
  "OpenBSD PLIST must use the rc.d directory variable")
assert_contains("${openbsd_plist}" "@sample"
  "OpenBSD PLIST must preserve configuration as samples")
assert_contains("${openbsd_plist}" "SYSCONFDIR}/i2pd/i2pd.conf"
  "OpenBSD PLIST must sample the router configuration")
assert_contains("${openbsd_plist}" "share/i2pd/certificates"
  "OpenBSD PLIST must ship the certificate tree")

assert_contains("${docker}" " zlib " "container runtime must install zlib")
assert_absent("${docker}" "boost-json" "container runtime must not install header-only Boost.JSON")
assert_absent("${docker}" "boost-url" "container runtime must not install header-only Boost.URL")

# See the LICENSE file at the top of the project tree for copyright and
# license details.
#
# openi2pd: check packaging.
#
# Compile-free checks for the POSIX front-end and the maintained
# packaging files. This script never configures, builds, installs or
# runs a package.
#

# Repository inventories must not traverse directory symlinks.
cmake_policy(SET CMP0009 NEW)

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
read_file("${PROJECT_ROOT}/build/make-driver.sh" make_driver)
read_file("${PROJECT_ROOT}/debian/build-driver.sh" debian_driver)
read_file("${PROJECT_ROOT}/contrib/static/debian/install-payload.sh" static_install_driver)
read_file("${PROJECT_ROOT}/contrib/static/debian/rules" static_debian_rules)
read_file("${PROJECT_ROOT}/contrib/openbsd/port/install-examples.sh" openbsd_install_driver)
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
read_file("${PROJECT_ROOT}/contrib/openbsd/port/pkg/DESCR" openbsd_descr)
read_file("${PROJECT_ROOT}/contrib/apparmor/usr.bin.i2pd" apparmor_profile)
read_file("${PROJECT_ROOT}/contrib/docker/entrypoint.sh" docker_entrypoint)
read_file("${PROJECT_ROOT}/contrib/static/debian/i2pd.docs" static_debian_docs)
read_file("${PROJECT_ROOT}/contrib/static/debian/i2pd.manpages" static_debian_manpages)
read_file("${PROJECT_ROOT}/contrib/static/debian/i2pd.links" static_debian_links)
read_file("${PROJECT_ROOT}/libi2pd/version.h" version_header)
read_file("${PROJECT_ROOT}/debian/changelog" debian_changelog)
read_file("${PROJECT_ROOT}/debian/control" debian_control)
read_file("${PROJECT_ROOT}/debian/i2pd.install" debian_i2pd_install)
read_file("${PROJECT_ROOT}/debian/i2pd-tui.install" debian_tui_install)
read_file("${PROJECT_ROOT}/debian/i2pd.lintian-overrides" debian_i2pd_lintian)
read_file("${PROJECT_ROOT}/debian/i2pd-tui.lintian-overrides" debian_tui_lintian)
read_file("${PROJECT_ROOT}/.github/workflows/packaging.yml" packaging_workflow)
read_file("${PROJECT_ROOT}/debian/rules" debian_rules)
read_file("${PROJECT_ROOT}/debian/copyright" debian_copyright)
read_file("${PROJECT_ROOT}/debian/i2pd.preinst" debian_preinst)
read_file("${PROJECT_ROOT}/debian/postinst" debian_postinst)
read_file("${PROJECT_ROOT}/debian/postrm" debian_postrm)
read_file("${PROJECT_ROOT}/debian/i2pd.sysusers" debian_sysusers)
read_file("${PROJECT_ROOT}/debian/tests/control" debian_tests_control)
read_file("${PROJECT_ROOT}/debian/tests/smoke" debian_tests_smoke)
read_file("${PROJECT_ROOT}/debian/watch" debian_watch)
read_file("${PROJECT_ROOT}/contrib/docker/Dockerfile" docker)

assert_contains("${root_make}" ".PHONY:" "POSIX make must declare phony targets")
assert_contains("${root_make}" "CMAKE_GENERATOR = Ninja" "POSIX make must default to Ninja")
assert_absent("${root_make}" "CMAKE_GENERATOR = Unix Makefiles"
  "POSIX make must not select Unix Makefiles")
assert_contains("${test_make}" ".PHONY:" "tests/Makefile shim must declare phony targets")
assert_contains("${test_make}" "\"\${MAKE}\" -C \"..\" "
  "tests/Makefile shim must forward to the root front-end")
assert_contains("${root_make}" "install: build" "install must build before staging")
assert_contains("${root_make}" "DESTDIR =" "install must expose DESTDIR")
assert_contains("${root_make}" "PREFIX =" "install must expose PREFIX")
assert_contains("${root_make}" "MAKE_DRIVER = sh build/make-driver.sh" "Make must name the source driver")
assert_contains("${root_make}" "\${MAKE_DRIVER} clean" "clean must delegate to its guarded driver")
assert_contains("${make_driver}" "refusing to remove unsafe BUILD_DIR" "clean must guard BUILD_DIR")
assert_contains("${make_driver}" "refusing to remove source root or its ancestor" "clean must reject physical source aliases")
assert_contains("${root_make}" "\${MAKE_DRIVER} install" "install must delegate prefix decisions")
assert_contains("${debian_rules}" "sh debian/build-driver.sh configure" "Debhelper must delegate configure logic")
assert_contains("${static_debian_rules}" "sh debian/install-payload.sh" "Static packages must delegate payload installation")
assert_contains("${static_install_driver}" "bin/i2pd-$architecture" "Static payload must retain its architecture identity")
foreach(phase IN ITEMS dirs data script)
  assert_contains("${openbsd_port_make}" "install-examples.sh\" ${phase}"
    "OpenBSD port must delegate each framework install phase")
endforeach()
assert_contains("${openbsd_port_make}" "\${.CURDIR}/install-examples.sh"
  "The port helper must travel with the external port, not an older distfile")
assert_absent("${openbsd_port_make}" "\${WRKDIST}/contrib/openbsd/port/install-examples.sh"
  "The existing release distfile must not need a newly added port helper")

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

# A selected service manager must ship the configuration its units read
# and the router binary they start, and it is not offered on Windows or
# Android. CMake rejects the mismatches at configure time; pin those
# guards so a package cannot install a service that never starts.
assert_contains("${top_cmake}"
  "I2PD_INSTALL_CONFIG=ON; the packaged service reads /etc/i2pd/i2pd.conf."
  "service selection must require the installed configuration")
assert_contains("${top_cmake}" "requires WITH_BINARY=ON;"
  "service selection must require the router binary")
assert_contains("${top_cmake}" "Windows and Android are managed by their native"
  "service selection must be rejected on Windows and Android")

foreach(spec IN ITEMS "${rpm}" "${rpm_git}")
  assert_contains("${spec}" "BuildRequires: cmake >= 3.24" "RPM must require CMake 3.24")
  assert_contains("${spec}" "BuildRequires: ninja-build" "RPM must require Ninja")
  assert_contains("${spec}" "BuildRequires: boost-json" "RPM must require Boost.JSON headers")
  assert_contains("${spec}" "BuildRequires: boost-url" "RPM must require Boost.URL headers")
  assert_absent("${spec}" "openssl-devel-engine" "RPM must not require the removed ENGINE package")
  assert_contains("${spec}" "_cmake_generator Ninja" "RPM must pin the Ninja generator")
endforeach()
foreach(spec IN ITEMS "${rpm}" "${rpm_git}")
  assert_contains("${spec}" "LICENSE.i2pd-tools" "RPM specs must install the tools licence")
  assert_absent("${spec}" "CC-BY-SA-4.0" "RPM metadata must not retain the replaced CC license")
  assert_contains("${spec}" "%{_datadir}/applications/openi2pd.desktop"
    "RPM %files must own the desktop entry")
  assert_contains("${spec}" "%{_datadir}/icons/hicolor/scalable/apps/openi2pd.svg"
    "RPM %files must own the scalable icon")
  assert_absent("${spec}" "%doc LICENSE NOTICE README.md tools/LICENSE"
    "RPM %doc must not collide on the tools LICENSE")
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
assert_absent("${debian_i2pd_install}" "CC-BY-SA-4.0"
  "daemon package must not install a standalone CC notice")
assert_absent("${debian_i2pd_install}" "openi2pd.desktop"
  "daemon package must not own the TUI desktop entry")
assert_contains("${debian_tui_install}" "openi2pd.desktop"
  "TUI package must own the desktop entry")
assert_contains("${debian_watch}" "archive/(?:refs/tags/)?v?"
  "uscan must accept GitHub archive URLs")

assert_contains("${openbsd_port_make}" "-DI2PD_SERVICE_MANAGER=none"
  "OpenBSD port must not install a second service through CMake")
assert_contains("${openbsd_port_make}" "-DI2PD_OPENSSL_EXPECT=LibreSSL"
  "OpenBSD port must pin the LibreSSL backend")
assert_contains("${openbsd_port_make}" "SEPARATE_BUILD = Yes"
  "OpenBSD port must use a separate build tree")
assert_contains("${openbsd_port_make}" "WRKBUILD = \${WRKDIST}/obj"
  "OpenBSD port must keep CMake artefacts outside build/")
assert_contains("${openbsd_install_driver}" "share/examples/var/i2pd/subscriptions.txt"
  "OpenBSD port must ship the subscriptions sample")
assert_contains("${openbsd_port_make}" "-DI2PD_INSTALL_CONFIG=OFF"
  "OpenBSD port should ship only its own configuration samples")
assert_contains("${openbsd_port_make}" "boost_container-mt"
  "OpenBSD port must list the Boost.Container library used by static Boost.JSON")
assert_absent("${openbsd_port_make}" "boost_atomic-mt"
  "OpenBSD port still requires the removed Boost.Atomic library")
assert_absent("${openbsd_port_make}" "boost_filesystem-mt"
  "OpenBSD port still requires the removed Boost.Filesystem library")
assert_contains("${openbsd_port_make}" "boost_json-mt"
  "OpenBSD port must list the compiled Boost.JSON library used by torrents")
assert_contains("${openbsd_port_make}" "boost_url-mt"
  "OpenBSD port must list the compiled Boost.URL library used by torrents")
assert_contains("${openbsd_port_make}" "ncursesw ssl util z"
  "OpenBSD port must list libutil used by the imsg worker transport")
assert_contains("${openbsd_port_make}" "CONFIGURE_STYLE ="
  "OpenBSD port must use the CMake module")
assert_contains("${openbsd_port_make}" "NO_TEST ="
  "OpenBSD port must state that CTest is not a package-build test")
assert_contains("${openbsd_port_make}" "-DWITH_HARDENING=ON"
  "OpenBSD package must enable hardening")
assert_contains("${openbsd_plist}" "@newgroup _i2pd:838"
  "OpenBSD PLIST must reserve the service group")
assert_contains("${openbsd_plist}" "@newuser _i2pd:838:838:i2pd:0:0:i2pd account:/var/i2pd:/sbin/nologin"
  "OpenBSD PLIST must reserve the service account with its login class")
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
assert_contains("${openbsd_plist}" "share/applications/openi2pd.desktop"
  "OpenBSD PLIST must ship the TUI desktop launcher")
assert_contains("${openbsd_plist}" "share/icons/hicolor/scalable/apps/openi2pd.svg"
  "OpenBSD PLIST must ship the scalable TUI icon")
assert_contains("${openbsd_plist}" "share/icons/hicolor/256x256/apps/openi2pd.png"
  "OpenBSD PLIST must ship the raster TUI icon")
assert_contains("${openbsd_descr}" "C++ implementation" "OpenBSD pkg/DESCR must describe the package")

# Debian manifests must describe what the CMake components actually
# install. dh_install prefers the current directory before debian/tmp,
# so a source-tree "docs" or "assets/branding" entry silently bypasses
# the CMake filters.
foreach(required IN ITEMS
    "usr/share/doc/i2pd/ChangeLog"
    "usr/share/doc/i2pd/LICENSE"
    "usr/share/doc/i2pd/README.md"
    "usr/share/doc/i2pd/NOTICE"
    "usr/share/doc/i2pd/LICENSE.i2pd-tools"
    "usr/share/doc/i2pd/docs"
    "usr/share/doc/i2pd/assets"
    "usr/share/man/man1/i2pd.1"
    "usr/share/man/man1/i2pdctl.1")
  assert_contains("${debian_i2pd_install}" "${required}"
    "daemon install manifest must claim the CMake payload")
endforeach()
assert_absent("${debian_i2pd_install}" "docs usr/share/doc/i2pd"
  "daemon manifest must not copy the unfiltered source docs tree")
assert_absent("${debian_i2pd_install}" "assets/branding usr/share/doc/i2pd"
  "daemon manifest must not copy the unfiltered source branding tree")
assert_contains("${debian_tui_install}" "usr/share/man/man1/i2pd-tui.1"
  "TUI package must own its man page")
assert_absent("${debian_i2pd_install}" "i2pd-tui.1"
  "daemon manifest must not claim the TUI man page")

# Man pages belong to the binary component that ships the program, not to
# the optional documentation tree, so a package built with
# -DI2PD_INSTALL_DOCS=OFF still installs them.
foreach(man_page IN ITEMS
    "man/i2pd[.]1|Runtime"
    "man/i2pd-tui[.]1|Tui"
    "man/i2pdctl[.]1|Tools")
  string(REPLACE "|" ";" man_parts "${man_page}")
  list(GET man_parts 0 man_path)
  list(GET man_parts 1 man_component)
  string(REGEX MATCH "${man_path}\"[^)]*COMPONENT ${man_component}[)]"
    man_rule "${top_cmake}")
  if(man_rule STREQUAL "")
    message(FATAL_ERROR
      "CMake must install ${man_path} with COMPONENT ${man_component}: "
      "man pages are not part of the optional documentation tree")
  endif()
endforeach()
assert_contains("${debian_i2pd_lintian}" "i2pd: possible-gpl-code-linked-with-openssl"
  "daemon Lintian override must be package-scoped")
assert_absent("${debian_i2pd_lintian}" "i2pd-tui:"
  "daemon Lintian overrides must not contain the TUI package")
assert_contains("${debian_tui_lintian}" "i2pd-tui: possible-gpl-code-linked-with-openssl"
  "TUI Lintian override must be in the TUI package file")
assert_contains("${packaging_workflow}" "grep -Eq 'static(-pie)? linked' static-file.txt"
  "static-musl verification must accept static PIE file descriptions")
foreach(required IN ITEMS
    "Checking that remove preserves administrator data"
    "FAIL: remove deleted /etc/i2pd/i2pd.conf"
    "install -d -o i2pd -g i2pd -m 0700 /var/lib/i2pd/netDb"
    "install -o i2pd -g i2pd -m 0600 /dev/null /var/lib/i2pd/netDb/.ci-remove-preservation"
    "if ! sudo test -f /var/lib/i2pd/netDb/.ci-remove-preservation; then"
    "FAIL: remove deleted persistent netDb state under /var/lib/i2pd"
    "Purging packages and checking owned state cleanup"
    "FAIL: purge retained /etc/i2pd/i2pd.conf"
    "if sudo test -d /var/lib/i2pd; then"
    "FAIL: purge retained /var/lib/i2pd"
    "Checking the dpkg database after purge")
  assert_contains("${packaging_workflow}" "${required}"
    "Debian remove/purge lifecycle diagnostics must identify each failed check")
endforeach()
foreach(absent IN ITEMS
    "${PROJECT_ROOT}/debian/i2pd.1"
    "${PROJECT_ROOT}/debian/i2pd-tui.manpages")
  if(EXISTS "${absent}")
    message(FATAL_ERROR "duplicate source-tree manpage payload still exists: ${absent}")
  endif()
endforeach()

foreach(required IN ITEMS
    "-DI2PD_INSTALL_DOCS=ON"
    "-DI2PD_INSTALL_CONFIG=ON"
    "-DI2PD_OPENSSL_EXPECT=OpenSSL"
    "-DI2PD_SERVICE_MANAGER=none")
  assert_contains("${debian_driver}" "${required}" "Debian build driver")
endforeach()
foreach(required IN ITEMS "dh_apparmor --profile-name=usr.bin.i2pd -pi2pd"
    "dh_missing --fail-missing")
  assert_contains("${debian_rules}" "${required}" "Debhelper authority")
endforeach()
foreach(required IN ITEMS "dpkg-buildflags --get CFLAGS" "dpkg-buildflags --get CPPFLAGS"
    "dpkg-buildflags --get CXXFLAGS" "dpkg-buildflags --get LDFLAGS"
    "DEB_BUILD_OPTIONS" "parallel=*)" "multiple parallel counts")
  assert_contains("${debian_driver}" "${required}" "Debian flag/parallel policy")
endforeach()
foreach(required IN ITEMS
    "dh-apparmor"
    "dh-sequence-installsysusers"
    "Breaks: i2pd (<< 2.61.0-3)"
    "Replaces: i2pd (<< 2.61.0-3)")
  assert_contains("${debian_control}" "${required}" "debian/control")
endforeach()
assert_contains("${debian_preinst}" ".dpkg-old"
  "preinst must preserve an existing certificate directory")
assert_contains("${debian_sysusers}" "u i2pd"
  "Debian sysusers policy must create the service account")

# Maintainer-script contracts. The scripts must create only missing
# paths with the packaged policy, never reset an administrator's
# ownership or mode, and never delete state outside purge.
foreach(required IN ITEMS
    "configure|reconfigure)"
    "abort-upgrade|abort-remove|abort-deconfigure|triggered)"
    "-mindepth 1 -maxdepth 1"
    "! -name certificates"
    "-m 0700"
    "-m 0600")
  assert_contains("${debian_postinst}" "${required}"
    "postinst must create or repair the packaged paths idempotently")
endforeach()
foreach(forbidden IN ITEMS
    "chmod -R"
    "rm -rf"
    "systemctl start"
    "invoke-rc.d")
  assert_absent("${debian_postinst}" "${forbidden}"
    "postinst must not reset directories, delete state or start the service")
endforeach()
assert_contains("${debian_postrm}" "  purge)"
  "postrm must remove state only in the purge branch")
assert_contains("${debian_postrm}" "rm -rf /var/lib/i2pd"
  "postrm must remove the private state directory on purge")
assert_contains("${debian_postrm}" "rm -f /etc/default/i2pd"
  "postrm must remove the SysV defaults file on purge")
assert_absent("${debian_postrm}" "chmod"
  "postrm must not modify permissions of surviving paths")
string(REGEX MATCHALL "rm -rf /var/lib/i2pd" postrm_state_removals "${debian_postrm}")
list(LENGTH postrm_state_removals postrm_state_removal_count)
if(NOT postrm_state_removal_count EQUAL 1)
  message(FATAL_ERROR
    "postrm must remove /var/lib/i2pd exactly once, in the purge branch; "
    "found ${postrm_state_removal_count} occurrences")
endif()
assert_contains("${debian_preinst}" "install|upgrade)"
  "preinst must act on install and upgrade only")
assert_absent("${debian_preinst}" "purge)"
  "preinst must never run the certificate migration during purge")
assert_absent("${debian_preinst}" "chmod"
  "preinst must rename the legacy certificate directory, never chmod it")
foreach(required IN ITEMS
    "dh_installtmpfiles"
    "dh_installsystemd --no-start"
    "dh_installinit")
  assert_contains("${debian_rules}" "${required}"
    "debian/rules must keep the service, tmpfiles and sysusers integration")
endforeach()

assert_contains("${debian_tests_control}" "systemd"
  "autopkgtest must install the unit validator")
foreach(required IN ITEMS
    "LICENSE.i2pd-tools"
    "docs/i18n.md"
    "apparmor_parser"
    "desktop-file-validate"
    "/usr/share/doc/i2pd/ChangeLog.gz"
    "/usr/share/doc/i2pd/README.md.gz"
    "/usr/share/doc/i2pd/assets/branding/colors.md.gz"
    "i2pd (= ")
  assert_contains("${debian_tests_smoke}" "${required}" "autopkgtest smoke")
endforeach()
assert_absent("${debian_tests_smoke}" "require_file /usr/share/doc/i2pd/ChangeLog\n"
  "autopkgtest must expect debhelper's compressed upstream changelog")
assert_absent("${debian_tests_smoke}" "require_file /usr/share/doc/i2pd/README.md\n"
  "autopkgtest must expect debhelper's compressed Markdown documentation")

read_file("${PROJECT_ROOT}/contrib/static/debian/i2pd.postrm" static_debian_postrm)
assert_absent("${static_debian_postrm}" "update-rc.d i2pd remove"
  "static package postrm must delegate init cleanup to dh_installinit")

# Static-package maintainer scripts follow the same phase policy as the
# debhelper package: configure/reconfigure do the work, the abort and
# triggered phases are no-ops, and an unknown phase is a packaging error
# instead of a silent success. Every account and home expansion is
# quoted, which is also the ShellCheck SC2086 contract.
read_file("${PROJECT_ROOT}/contrib/static/debian/i2pd.postinst"
  static_debian_postinst)
read_file("${PROJECT_ROOT}/contrib/static/debian/i2pd.prerm"
  static_debian_prerm)
foreach(required IN ITEMS
    "configure|reconfigure)"
    "abort-upgrade|abort-remove|abort-deconfigure)"
    "triggered)"
    "\"$I2PDUSER\""
    "\"$I2PDHOME\""
    "-mindepth 1 -maxdepth 1"
    "! -name certificates"
    "-m 0700"
    "-m 0600")
  assert_contains("${static_debian_postinst}" "${required}"
    "static postinst must keep the packaged phase and directory policy")
endforeach()
assert_contains("${static_debian_postinst}"
  "unknown argument '$1'\" >&2\n    exit 1"
  "static postinst must reject an unknown phase with a non-zero status")
assert_absent("${static_debian_postinst}" "\${I2PDUSER}"
  "static postinst must not use an unquoted brace expansion")
assert_absent("${static_debian_postinst}" "\${I2PDHOME}"
  "static postinst must not use an unquoted brace expansion")
foreach(unquoted IN ITEMS
    "passwd $I2PDUSER"
    "groupadd -f $I2PDUSER"
    "--home $I2PDHOME $I2PDUSER"
    "-o $I2PDUSER"
    "-g $I2PDUSER"
    "chown $I2PDUSER")
  assert_absent("${static_debian_postinst}" "${unquoted}"
    "static postinst must quote every account and home expansion")
endforeach()
assert_absent("${static_debian_postinst}" "systemctl start"
  "static postinst must not start the service")
assert_absent("${static_debian_postinst}" "invoke-rc.d"
  "static postinst must not invoke a service manager")
assert_contains("${static_debian_postrm}" "if [ \"\$1\" = \"purge\" ]"
  "static postrm must remove state only during purge")
assert_absent("${static_debian_postrm}" "systemctl"
  "static postrm must leave service actions to debhelper")
assert_absent("${static_debian_postrm}" "invoke-rc.d"
  "static postrm must leave service actions to debhelper")
assert_contains("${static_debian_prerm}" "#DEBHELPER#"
  "static prerm must keep the debhelper insertion point")
assert_absent("${static_debian_prerm}" "systemctl"
  "static prerm must leave service actions to debhelper")
assert_absent("${static_debian_prerm}" "invoke-rc.d"
  "static prerm must leave service actions to debhelper")

# DEP-5 structure and the documentation licence split.
assert_contains("${debian_copyright}" "Files: contrib/certificates/*"
  "copyright must describe the bundled trust anchors")
assert_contains("${debian_copyright}" "Files: android/gradle/wrapper/*"
  "copyright must describe the binary Gradle wrapper")
assert_contains("${debian_copyright}" "License: GPL-2"
  "copyright must distinguish the GPL-2 text from GPL-2+ code")
assert_absent("${debian_copyright}" "Files: debian/changelog debian/NEWS"
  "NEWS is first-party and must not be declared GPL-2+")
if(EXISTS "${PROJECT_ROOT}/CC-BY-SA-4.0")
  message(FATAL_ERROR "standalone CC-BY-SA notice file must be absent from the repository")
endif()
if(EXISTS "${PROJECT_ROOT}/docs/CC-BY-SA-4.0")
  message(FATAL_ERROR "standalone CC-BY-SA notice file must be absent from the repository")
endif()

# AppArmor profile: worker re-exec, sockets and log directory.
foreach(required IN ITEMS
    "flags=(attach_disconnected)"
    "mixr"
    "/var/lib/i2pd/** rwk"
    "/var/log/i2pd/** rw"
    "/{var/,}run/i2pd/*.sock rw"
    "include if exists <local/usr.bin.i2pd>")
  assert_contains("${apparmor_profile}" "${required}" "AppArmor profile")
endforeach()

# Static package and container attribution.
assert_contains("${static_debian_docs}" "src/LICENSE" "static Debian docs")
assert_absent("${static_debian_docs}" "CC-BY-SA-4.0" "static Debian docs must not install a standalone CC notice")
assert_contains("${static_debian_manpages}" "src/man/i2pd.1" "static Debian manpage")
assert_absent("${static_debian_links}" "var/lib/i2pd/i2pd.conf"
  "static package must not recreate the removed configuration link")
assert_contains("${docker_entrypoint}" "[ -e \"$DATA_DIR/certificates\" ]"
  "container entrypoint must not fail on a persisted certificate link")
assert_contains("${docker}" "usr/share/licenses/i2pd/NOTICE"
  "container image must ship NOTICE")
assert_contains("${docker}" "org.opencontainers.image.licenses=\"BSD-3-Clause\""
  "container license metadata must match project documentation")
assert_absent("${docker}" "CC-BY-SA-4.0"
  "container license metadata must not retain the replaced CC license")

# OpenBSD @sample targets resolve to share/examples/<absolute path>. The
# port must install the mirrored trees, create /var/i2pd from a sample,
# keep the configuration root-owned and ship the mandatory notices.
assert_contains("${openbsd_install_driver}" "share/examples/etc/i2pd"
  "OpenBSD port must install the /etc sample tree")
assert_contains("${openbsd_install_driver}" "share/examples/var/i2pd"
  "OpenBSD port must install the /var/i2pd sample directory")
assert_contains("${openbsd_install_driver}" "share/doc/i2pd/NOTICE"
  "OpenBSD port must install NOTICE")
assert_absent("${openbsd_install_driver}" "share/doc/i2pd/CC-BY-SA-4.0"
  "OpenBSD port must not install a standalone CC notice")
assert_contains("${openbsd_plist}" "@sample \${LOCALSTATEDIR}/i2pd/"
  "OpenBSD PLIST must sample the private state directory")
assert_contains("${openbsd_plist}" "share/examples/etc/i2pd/i2pd.conf"
  "OpenBSD PLIST must reference the mirrored /etc sample")
assert_contains("${openbsd_plist}" "share/examples/var/i2pd/README"
  "OpenBSD PLIST must package the state-directory sample marker")
assert_contains("${openbsd_plist}" "share/examples/var/i2pd/subscriptions.txt"
  "OpenBSD PLIST must package the subscriptions sample")
assert_contains("${openbsd_plist}" "@sample \${LOCALSTATEDIR}/i2pd/subscriptions.txt"
  "OpenBSD PLIST must sample subscriptions.txt")

# Derived @sample contract: pkg_add resolves every "@sample <absolute
# path>" against share/examples/<absolute path>, so each target must have
# a packaged mirror in the PLIST and in the port's post-install recipe. A
# missing mirror installs no configuration or login class at all, which
# the rc.d script cannot detect at start.
string(REPLACE "\n" ";" openbsd_plist_lines "${openbsd_plist}")
set(openbsd_sample_count 0)
foreach(plist_line IN LISTS openbsd_plist_lines)
  if(NOT plist_line MATCHES "^@sample ")
    continue()
  endif()
  math(EXPR openbsd_sample_count "${openbsd_sample_count} + 1")
  string(REGEX REPLACE "^@sample " "" sample_target "${plist_line}")
  string(STRIP "${sample_target}" sample_target)
  string(REPLACE "\${LOCALSTATEDIR}" "var" sample_path "${sample_target}")
  string(REPLACE "\${SYSCONFDIR}" "etc" sample_path "${sample_path}")
  string(REPLACE "\${TRUEPREFIX}" "local" sample_path "${sample_path}")
  string(REGEX REPLACE "[/]+$" "" sample_path "${sample_path}")
  assert_contains("${openbsd_plist}" "share/examples/${sample_path}"
    "OpenBSD PLIST must package the mirror of @sample ${sample_target}")
  assert_contains("${openbsd_install_driver}" "share/examples/${sample_path}"
    "OpenBSD port must install the mirror of @sample ${sample_target}")
endforeach()
if(openbsd_sample_count LESS 5)
  message(FATAL_ERROR
    "OpenBSD PLIST must sample the state directory, subscriptions, the "
    "router and tunnel configuration, the tunnel README and the login "
    "class; found ${openbsd_sample_count} @sample entries")
endif()
assert_contains("${openbsd_plist}" "@sample \${SYSCONFDIR}/login.conf.d/i2pd"
  "OpenBSD PLIST must sample the i2pd login class")
assert_contains("${openbsd_plist}" "man/man1/i2pd.1"
  "OpenBSD PLIST must claim the router man page")
assert_contains("${openbsd_plist}" "man/man1/i2pd-tui.1"
  "OpenBSD PLIST must claim the TUI man page")
assert_contains("${openbsd_plist}" "@mode 0700"
  "OpenBSD state directory must be private")
assert_contains("${openbsd_plist}" "share/doc/i2pd/NOTICE"
  "OpenBSD PLIST must list NOTICE")
assert_contains("${openbsd_plist}" "share/doc/i2pd/LICENSE.i2pd-tools"
  "OpenBSD PLIST must list the tools licence")
assert_absent("${openbsd_plist}" "@sample \${LOCALSTATEDIR}/i2pd/certificates/"
  "certificates are read-only package files, not /var/i2pd samples")
assert_absent("${openbsd_plist}"
  "@mode 0640\n@owner _i2pd\n@group _i2pd\nshare/examples/etc/i2pd/i2pd.conf"
  "router configuration must not be writable by the service account")
assert_absent("${openbsd_descr}" "native tools"
  "OpenBSD DESCR must not claim tools that the port does not build")
foreach(sample IN ITEMS
    "contrib/openbsd/i2pd.conf"
    "contrib/tunnels.conf"
    "contrib/tunnels.d/README"
    "contrib/openbsd/i2pd.login"
    "contrib/openbsd/port/pkg/var-i2pd.README"
    "LICENSE"
    "NOTICE"
    "tools/LICENSE")
  if(NOT EXISTS "${PROJECT_ROOT}/${sample}")
    message(FATAL_ERROR "OpenBSD port sample source is missing: ${sample}")
  endif()
endforeach()

assert_contains("${docker}" " zlib " "container runtime must install zlib")
assert_contains("${docker}" "boost-json" "container runtime must install the Boost.JSON library")
assert_contains("${docker}" "boost-container" "builder must resolve the compiled Boost.Container dependency")
assert_contains("${docker}" "boost-url" "container runtime must install the Boost.URL library")

string(REGEX MATCH "#define I2PD_VERSION_MAJOR ([0-9]+)" _version_major "${version_header}")
set(_version "${CMAKE_MATCH_1}")
string(REGEX MATCH "#define I2PD_VERSION_MINOR ([0-9]+)" _version_minor "${version_header}")
string(APPEND _version ".${CMAKE_MATCH_1}")
string(REGEX MATCH "#define I2PD_VERSION_MICRO ([0-9]+)" _version_micro "${version_header}")
string(APPEND _version ".${CMAKE_MATCH_1}")
assert_contains("${rpm}" "Version:       ${_version}"
  "RPM spec version must match libi2pd/version.h")
assert_contains("${openbsd_port_make}" "GH_TAGNAME = ${_version}"
  "OpenBSD port tag must match libi2pd/version.h")
assert_contains("${debian_changelog}" "i2pd (${_version}"
  "Debian changelog version must match libi2pd/version.h")
unset(_version)

# The OpenBSD PLIST ships the certificate tree file by file. A new
# certificate must not silently break pkg_create with an unlisted file.
file(GLOB_RECURSE openbsd_certificate_files RELATIVE "${PROJECT_ROOT}/contrib/certificates"
  "${PROJECT_ROOT}/contrib/certificates/*.crt")
foreach(certificate IN LISTS openbsd_certificate_files)
  string(FIND "${openbsd_plist}" "share/i2pd/certificates/${certificate}" certificate_entry)
  if(certificate_entry EQUAL -1)
    message(FATAL_ERROR
      "OpenBSD PLIST is missing share/i2pd/certificates/${certificate}")
  endif()
endforeach()

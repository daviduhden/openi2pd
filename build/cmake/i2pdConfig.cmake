# See the LICENSE file at the top of the project tree for copyright
# and license details.
#
# openi2pd: i2pdConfig.
#
# Installed package configuration that re-finds libi2pd's Boost, OpenSSL,
# Threads and zlib dependencies and loads the exported targets. The
# configure_package_config_file substitutions keep the prefix relocatable and
# handle multiarch LIBDIR/INCLUDEDIR layouts.
#

@PACKAGE_INIT@

include(CMakeFindDependencyMacro)
find_dependency(Threads)
find_dependency(Boost 1.74 COMPONENTS program_options)
find_dependency(OpenSSL)
find_dependency(ZLIB)

include("${CMAKE_CURRENT_LIST_DIR}/i2pd-targets.cmake")

set_and_check(libi2pd_INCLUDE_DIR "@PACKAGE_CMAKE_INSTALL_INCLUDEDIR@/libi2pd")
set_and_check(libi2pdclient_INCLUDE_DIR "@PACKAGE_CMAKE_INSTALL_INCLUDEDIR@/libi2pdclient")
set_and_check(libi2pdlang_INCLUDE_DIR "@PACKAGE_CMAKE_INSTALL_INCLUDEDIR@/libi2pdlang")
set(libi2pd_INCLUDE_DIRS "${libi2pd_INCLUDE_DIR};${libi2pdclient_INCLUDE_DIR};${libi2pdlang_INCLUDE_DIR}")

set(libi2pd_LIBRARIES i2pd::libi2pd)
set(libi2pdclient_LIBRARIES i2pd::libi2pdclient)
set(libi2pdlang_LIBRARIES i2pd::libi2pdlang)

check_required_components(i2pd)

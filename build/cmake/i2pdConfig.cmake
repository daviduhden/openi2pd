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
find_dependency(Boost @I2PD_BOOST_MIN_VERSION@
  COMPONENTS @I2PD_BOOST_COMPONENTS@
  OPTIONAL_COMPONENTS @I2PD_BOOST_OPTIONAL_COMPONENTS@)
find_dependency(OpenSSL COMPONENTS Crypto SSL)
find_dependency(ZLIB 1.2.11)

include("${CMAKE_CURRENT_LIST_DIR}/i2pd-targets.cmake")

# The install components are Runtime, Tui, Libraries, Development, Tools,
# Documentation and Service. Only Libraries+Development provide a CMake
# package; report the other names as unsupported instead of pretending that a
# partially installed tree can satisfy them.
set(i2pd_Libraries_FOUND TRUE)
set(i2pd_Development_FOUND TRUE)

foreach(_i2pd_include libi2pd libi2pdclient libi2pdlang)
  if(EXISTS "@PACKAGE_CMAKE_INSTALL_INCLUDEDIR@/${_i2pd_include}")
    set(${_i2pd_include}_INCLUDE_DIR "@PACKAGE_CMAKE_INSTALL_INCLUDEDIR@/${_i2pd_include}")
  endif()
endforeach()
if(NOT libi2pd_INCLUDE_DIR OR NOT libi2pdclient_INCLUDE_DIR OR NOT libi2pdlang_INCLUDE_DIR)
  # A Libraries-only staging tree is valid. Keep find_package(QUIET) able to
  # report the missing Development component instead of aborting configure.
  set(i2pd_Development_FOUND FALSE)
  set(i2pd_NOT_FOUND_MESSAGE
    "The i2pd Development component is not installed under @PACKAGE_CMAKE_INSTALL_INCLUDEDIR@")
  list(FIND i2pd_FIND_COMPONENTS Development _i2pd_development_index)
  if(NOT _i2pd_development_index EQUAL -1)
    set(i2pd_FOUND FALSE)
  endif()
else()
  set(libi2pd_INCLUDE_DIRS "${libi2pd_INCLUDE_DIR};${libi2pdclient_INCLUDE_DIR};${libi2pdlang_INCLUDE_DIR}")
endif()

set(libi2pd_LIBRARIES i2pd::libi2pd)
set(libi2pdclient_LIBRARIES i2pd::libi2pdclient)
set(libi2pdlang_LIBRARIES i2pd::libi2pdlang)

check_required_components(i2pd)

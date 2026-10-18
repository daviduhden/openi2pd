# Copyright (c) 2013-2026, The PurpleI2P Project
# Copyright (c) 2026, David Uhden Collado
# SPDX-License-Identifier: BSD-3-Clause
include(CMakeFindDependencyMacro)
find_dependency(Threads)
find_dependency(Boost 1.74 COMPONENTS atomic filesystem program_options)
if("@WITH_TORRENTS@" STREQUAL "ON")
  find_dependency(Boost 1.81 COMPONENTS json url)
endif()
find_dependency(OpenSSL)
find_dependency(ZLIB)

get_filename_component(_i2pd_prefix "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)

include("${CMAKE_CURRENT_LIST_DIR}/i2pd-targets.cmake")

set(libi2pd_INCLUDE_DIR "${_i2pd_prefix}/@CMAKE_INSTALL_INCLUDEDIR@")
set(libi2pdclient_INCLUDE_DIR "${_i2pd_prefix}/@CMAKE_INSTALL_INCLUDEDIR@")
set(libi2pdlang_INCLUDE_DIR "${_i2pd_prefix}/@CMAKE_INSTALL_INCLUDEDIR@")

set(libi2pd_LIBRARIES i2pd::libi2pd)
set(libi2pdclient_LIBRARIES i2pd::libi2pdclient)
set(libi2pdlang_LIBRARIES i2pd::libi2pdlang)

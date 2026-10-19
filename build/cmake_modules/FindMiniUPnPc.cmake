# Copyright (c) 2013-2026, The PurpleI2P Project
# Copyright (c) 2026, David Uhden Collado
# SPDX-License-Identifier: BSD-3-Clause
# - Find MiniUPnPc
#
# Uses the normal CMake search path (CMAKE_PREFIX_PATH, toolchain sysroot,
# package-manager prefixes and the platform default). No source-relative,
# absolute or downloaded paths are injected here.
#
# MiniUPnPc 2.2.6 (API 17) is the minimum available in the supported Linux
# distribution set. API 18 adds the external-address form of UPNP_GetValidIGD;
# daemon/UPnP.cpp selects that signature when the header reports it.

include(FindPackageHandleStandardArgs)

find_path(MINIUPNPC_INCLUDE_DIR
  NAMES miniupnpc/miniupnpc.h
)

find_library(MINIUPNPC_LIBRARY
  NAMES miniupnpc
)

set(MiniUPnPc_VERSION "")
set(_miniupnpc_api_version "")
if(MINIUPNPC_INCLUDE_DIR)
  file(READ "${MINIUPNPC_INCLUDE_DIR}/miniupnpc/miniupnpc.h" _miniupnpc_header)
  if(_miniupnpc_header MATCHES "#define[ \t]+MINIUPNPC_VERSION[ \t]+\"([^\"]+)\"")
    set(MiniUPnPc_VERSION "${CMAKE_MATCH_1}")
  endif()
  if(_miniupnpc_header MATCHES "#define[ \t]+MINIUPNPC_API_VERSION[ \t]+([0-9]+)")
    set(_miniupnpc_api_version "${CMAKE_MATCH_1}")
  endif()
endif()

find_package_handle_standard_args(MiniUPnPc
  REQUIRED_VARS MINIUPNPC_INCLUDE_DIR MINIUPNPC_LIBRARY
  VERSION_VAR MiniUPnPc_VERSION
)

if(MiniUPnPc_FOUND)
  if(NOT _miniupnpc_api_version OR _miniupnpc_api_version LESS 17)
    message(FATAL_ERROR
      "MiniUPnPc 2.2.6 or newer (API 17) is required; found ${MiniUPnPc_VERSION} "
      "(API ${_miniupnpc_api_version})")
  endif()
  if(NOT TARGET MiniUPnPc::MiniUPnPc)
    add_library(MiniUPnPc::MiniUPnPc UNKNOWN IMPORTED)
    set_target_properties(MiniUPnPc::MiniUPnPc PROPERTIES
      IMPORTED_LOCATION "${MINIUPNPC_LIBRARY}"
      INTERFACE_INCLUDE_DIRECTORIES "${MINIUPNPC_INCLUDE_DIR}"
    )
  endif()
endif()

set(MINIUPNPC_FOUND "${MiniUPnPc_FOUND}")

mark_as_advanced(MINIUPNPC_INCLUDE_DIR MINIUPNPC_LIBRARY)

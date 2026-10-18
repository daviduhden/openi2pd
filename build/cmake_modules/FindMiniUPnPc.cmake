# Copyright (c) 2013-2026, The PurpleI2P Project
# Copyright (c) 2026, David Uhden Collado
# SPDX-License-Identifier: BSD-3-Clause
# - Find MINIUPNPC

if(MINIUPNPC_INCLUDE_DIR AND MINIUPNPC_LIBRARY)
   set(MINIUPNPC_FOUND TRUE)

else()
  find_path(MINIUPNPC_INCLUDE_DIR miniupnpc/miniupnpc.h
      /usr/include
      /usr/local/include
      /opt/local/include
      $ENV{SystemDrive}
      ${PROJECT_SOURCE_DIR}/../..
      )

  find_library(MINIUPNPC_LIBRARY miniupnpc)

  if(MINIUPNPC_INCLUDE_DIR AND MINIUPNPC_LIBRARY)
    set(MINIUPNPC_FOUND TRUE)
    message(STATUS "Found MiniUPnP headers: ${MINIUPNPC_INCLUDE_DIR}")
    message(STATUS "Found MiniUPnP library: ${MINIUPNPC_LIBRARY}")
  else()
    set(MINIUPNPC_FOUND FALSE)
    message(STATUS "MiniUPnP not found.")
  endif()

  mark_as_advanced(MINIUPNPC_INCLUDE_DIR MINIUPNPC_LIBRARY)

endif()

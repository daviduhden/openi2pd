# See the LICENSE file at the top of the project tree for copyright
# and license details.
#
# openi2pd: test tools inventory.
#
# Compile-free CTest inventory check that every maintained tool executable and
# shared source exists, and optionally that the pinned upstream manifest names
# real local files. It does not execute any tool.
#

if(NOT DEFINED TOOLS_DIR OR NOT DEFINED TOOLS OR NOT DEFINED TOOLS_SOURCE_DIR)
  message(FATAL_ERROR "TOOLS_DIR, TOOLS and TOOLS_SOURCE_DIR are required")
endif()

# The list is passed comma-separated so it survives a single -D argument on
# every CMake version/platform.
string(REPLACE "," ";" TOOLS "${TOOLS}")
set(EXE "${CMAKE_EXECUTABLE_SUFFIX}")
foreach(tool IN LISTS TOOLS)
  if(NOT EXISTS "${TOOLS_DIR}/${tool}${EXE}")
    message(FATAL_ERROR "Missing tool executable: ${TOOLS_DIR}/${tool}${EXE}")
  endif()
  if(NOT EXISTS "${TOOLS_SOURCE_DIR}/${tool}.cpp")
    message(FATAL_ERROR "Missing tool source: ${TOOLS_SOURCE_DIR}/${tool}.cpp")
  endif()
endforeach()

# Shared portability/file helpers and the upstream licence must travel with the
# tool component even when only a subset of binaries is inspected.
set(REQUIRED_SOURCES
  common/file.hpp
  common/getopt.cpp
  common/getopt.h
  common/key.hpp
  firewall.hpp
  LICENSE
)
foreach(source IN LISTS REQUIRED_SOURCES)
  if(NOT EXISTS "${TOOLS_SOURCE_DIR}/${source}")
    message(FATAL_ERROR "Missing shared tool source: ${TOOLS_SOURCE_DIR}/${source}")
  endif()
endforeach()

# Verify the pinned upstream manifest describes real local files. The manifest
# lists sources, not generated/downloaded files, and is used by the manual sync
# helper.
if(DEFINED TOOLS_UPSTREAM_MANIFEST AND EXISTS "${TOOLS_UPSTREAM_MANIFEST}")
  file(STRINGS "${TOOLS_UPSTREAM_MANIFEST}" UPSTREAM_FILES)
  foreach(source IN LISTS UPSTREAM_FILES)
    if(source STREQUAL "")
      continue()
    endif()
    if(NOT EXISTS "${TOOLS_SOURCE_DIR}/${source}")
      message(FATAL_ERROR "Upstream manifest references missing file: ${source}")
    endif()
  endforeach()
endif()

message(STATUS "tools-inventory: ${TOOLS} present, sources and provenance checked")

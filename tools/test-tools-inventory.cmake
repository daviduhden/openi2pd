# See the LICENSE file at the top of the project tree for copyright
# and license details.
#
# openi2pd: test tools inventory.
#
# Build-gated artifact inventory check that every maintained tool
# executable and shared source exists, and that the pinned upstream
# manifest names real local files. The compile-free CMake/docs/packaging
# parity check lives in tests/check-tools-inventory.cmake.
#

if(NOT DEFINED TOOLS_DIR OR NOT DEFINED TOOLS OR NOT DEFINED TOOLS_SOURCE_DIR)
  message(FATAL_ERROR "TOOLS_DIR, TOOLS and TOOLS_SOURCE_DIR are required")
endif()

# The list is passed comma-separated so it survives a single -D argument
# on every CMake version/platform.
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

# Shared portability/file helpers and the upstream licence must travel
# with the tool component even when only a subset of binaries is
# inspected.
set(REQUIRED_SOURCES
  common/file.hpp
  common/getopt.cpp
  common/getopt.h
  common/key.hpp
  common/keys.hpp
  common/crypto.hpp
  firewall.hpp
  tests/maintenance-checkers.t
  LICENSE
)
foreach(source IN LISTS REQUIRED_SOURCES)
  if(NOT EXISTS "${TOOLS_SOURCE_DIR}/${source}")
    message(FATAL_ERROR "Missing shared tool source: ${TOOLS_SOURCE_DIR}/${source}")
  endif()
endforeach()

# The installed Perl client must travel with its manual page.
string(REGEX REPLACE "/contrib/upstream/tools-files\\.txt$" "" PROJECT_ROOT
  "${TOOLS_UPSTREAM_MANIFEST}")
if(NOT EXISTS "${PROJECT_ROOT}/man/i2pdctl.1")
  message(FATAL_ERROR "Missing man page: ${PROJECT_ROOT}/man/i2pdctl.1")
endif()

# Verify the pinned upstream manifest describes real local files. A
# missing manifest is a provenance failure, not a skipped check.
if(NOT DEFINED TOOLS_UPSTREAM_MANIFEST OR NOT EXISTS "${TOOLS_UPSTREAM_MANIFEST}")
  message(FATAL_ERROR "Upstream tools manifest is missing: ${TOOLS_UPSTREAM_MANIFEST}")
endif()
file(STRINGS "${TOOLS_UPSTREAM_MANIFEST}" UPSTREAM_FILES)
foreach(source IN LISTS UPSTREAM_FILES)
  if(source STREQUAL "")
    continue()
  endif()
  if(NOT EXISTS "${TOOLS_SOURCE_DIR}/${source}")
    message(FATAL_ERROR "Upstream manifest references missing file: ${source}")
  endif()
endforeach()

# Every top-level tool source must be declared as an executable. A new
# .cpp that is not wired into CMake is a build/inventory drift error.
file(GLOB TOOL_SOURCES "${TOOLS_SOURCE_DIR}/*.cpp")
foreach(source IN LISTS TOOL_SOURCES)
  get_filename_component(name "${source}" NAME_WE)
  if(NOT "${name}" IN_LIST TOOLS)
    message(FATAL_ERROR "Unregistered tool source: ${source}")
  endif()
endforeach()

message(STATUS "tools-inventory: ${TOOLS} present, sources and provenance checked")

# See the LICENSE file at the top of the project tree for copyright
# and license details.
#
# openi2pd: check tools inventory.
#
# Compile-free parity check for the auxiliary tool inventory. The CMake target
# list, documentation navigation, Debian/RPM packaging and the manual install
# assertion must all name the same executables, and every source/doc/manifest
# entry must exist. It never configures, compiles or runs a tool.
#

if(NOT PROJECT_ROOT)
  message(FATAL_ERROR "PROJECT_ROOT is required")
endif()

function(read_required path out_var)
  if(NOT EXISTS "${path}")
    message(FATAL_ERROR "Missing tool inventory file: ${path}")
  endif()
  file(READ "${path}" contents)
  set(${out_var} "${contents}" PARENT_SCOPE)
endfunction()

function(sorted_unique out_var)
  set(values ${ARGN})
  if(values)
    list(SORT values)
    list(REMOVE_DUPLICATES values)
  endif()
  set(${out_var} "${values}" PARENT_SCOPE)
endfunction()

function(extract_between text start_marker end_marker out_var)
  string(FIND "${text}" "${start_marker}" start)
  if(start EQUAL -1)
    message(FATAL_ERROR "missing marker '${start_marker}'")
  endif()
  string(LENGTH "${start_marker}" marker_len)
  math(EXPR start "${start} + ${marker_len}")
  string(SUBSTRING "${text}" ${start} -1 remainder)
  string(FIND "${remainder}" "${end_marker}" end)
  if(end EQUAL -1)
    message(FATAL_ERROR "missing end marker '${end_marker}'")
  endif()
  string(SUBSTRING "${remainder}" 0 ${end} block)
  set(${out_var} "${block}" PARENT_SCOPE)
endfunction()

set(tools_cmake "${PROJECT_ROOT}/tools/CMakeLists.txt")
read_required("${tools_cmake}" cmake_text)
extract_between("${cmake_text}" "set(I2PD_TOOLS" ")" tools_block)
string(REGEX MATCHALL "[A-Za-z0-9_]+" tools_list "${tools_block}")
set(tools_list ${tools_list})
sorted_unique(tools_list ${tools_list})

# Documentation navigation.
read_required("${PROJECT_ROOT}/docs/mkdocs.yml" nav_text)
string(REGEX MATCHALL "tools/[A-Za-z0-9_]+\\.md" nav_pages "${nav_text}")
set(nav_tools)
foreach(page IN LISTS nav_pages)
  get_filename_component(name "${page}" NAME_WE)
  if(name STREQUAL "README" OR name STREQUAL "provenance" OR name STREQUAL "i2pdctl")
    continue()
  endif()
  list(APPEND nav_tools "${name}")
endforeach()
sorted_unique(nav_tools ${nav_tools})

# Debian install list.
read_required("${PROJECT_ROOT}/debian/i2pd.install" deb_text)
string(REGEX MATCHALL "usr/bin/[A-Za-z0-9_]+" deb_bins "${deb_text}")
set(deb_tools)
foreach(bin IN LISTS deb_bins)
  get_filename_component(name "${bin}" NAME)
  if(name STREQUAL "i2pd" OR name STREQUAL "i2pdctl" OR name STREQUAL "i2pd-tui")
    continue()
  endif()
  list(APPEND deb_tools "${name}")
endforeach()
sorted_unique(deb_tools ${deb_tools})

# Both RPM specs hand-install each tool.
set(rpm_tools)
foreach(spec IN ITEMS "${PROJECT_ROOT}/contrib/rpm/i2pd.spec"
    "${PROJECT_ROOT}/contrib/rpm/i2pd-git.spec")
  if(NOT EXISTS "${spec}")
    continue()
  endif()
  file(READ "${spec}" rpm_text)
  string(REGEX MATCHALL "tools/[A-Za-z0-9_]+[ \t]+%\\{buildroot\\}%\\{_bindir\\}/[A-Za-z0-9_]+"
    rpm_pairs "${rpm_text}")
  foreach(pair IN LISTS rpm_pairs)
    string(REGEX REPLACE "^tools/([A-Za-z0-9_]+).*" "\\1" name "${pair}")
    list(APPEND rpm_tools "${name}")
  endforeach()
endforeach()
sorted_unique(rpm_tools ${rpm_tools})

# The manual install assertion in the workflow.
read_required("${PROJECT_ROOT}/.github/workflows/build.yml" workflow_text)
string(REGEX MATCH "for tool in ([A-Za-z0-9_ ]+); do" workflow_loop "${workflow_text}")
if(workflow_loop STREQUAL "")
  message(FATAL_ERROR "the manual install job has no tool inventory loop")
endif()
separate_arguments(workflow_tools UNIX_COMMAND "${CMAKE_MATCH_1}")
sorted_unique(workflow_tools ${workflow_tools})

foreach(pair IN ITEMS
    "documentation;${nav_tools}"
    "Debian;${deb_tools}"
    "RPM;${rpm_tools}"
    "workflow;${workflow_tools}")
  list(GET pair 0 description)
  list(REMOVE_AT pair 0)
  set(other ${pair})
  sorted_unique(other ${other})
  if(NOT "${tools_list}" STREQUAL "${other}")
    message(FATAL_ERROR
      "${description} tool inventory differs from tools/CMakeLists.txt\n"
      "CMake: ${tools_list}\n${description}: ${other}")
  endif()
endforeach()

foreach(tool IN LISTS tools_list)
  foreach(path IN ITEMS
      "${PROJECT_ROOT}/tools/${tool}.cpp"
      "${PROJECT_ROOT}/docs/tools/${tool}.md")
    if(NOT EXISTS "${path}")
      message(FATAL_ERROR "missing ${path} for tool ${tool}")
    endif()
  endforeach()
  string(FIND "${nav_text}" "tools/${tool}.md" nav_entry)
  if(nav_entry EQUAL -1)
    message(FATAL_ERROR "tools/${tool}.md is not in docs/mkdocs.yml")
  endif()
endforeach()

# Shared helpers must exist and every upstream manifest entry must resolve.
foreach(helper IN ITEMS
    "tools/common/file.hpp"
    "tools/common/key.hpp"
    "tools/common/keys.hpp"
    "tools/common/crypto.hpp"
    "tools/common/getopt.cpp"
    "tools/common/getopt.h")
  if(NOT EXISTS "${PROJECT_ROOT}/${helper}")
    message(FATAL_ERROR "missing shared tool helper ${helper}")
  endif()
endforeach()

foreach(path IN ITEMS "tools/scripts/i2pdctl" "docs/tools/i2pdctl.md")
  if(NOT EXISTS "${PROJECT_ROOT}/${path}")
    message(FATAL_ERROR "missing Perl client ${path}")
  endif()
endforeach()

read_required("${PROJECT_ROOT}/contrib/upstream/tools-files.txt" manifest_text)
string(REPLACE "\r" "" manifest_text "${manifest_text}")
string(REPLACE "\n" ";" manifest_entries "${manifest_text}")
foreach(entry IN LISTS manifest_entries)
  string(STRIP "${entry}" entry)
  if(entry STREQUAL "")
    continue()
  endif()
  if(NOT EXISTS "${PROJECT_ROOT}/tools/${entry}")
    message(FATAL_ERROR "upstream tool manifest entry does not exist: ${entry}")
  endif()
endforeach()

foreach(required IN ITEMS "tools/LICENSE" "tools/CMakeLists.txt")
  if(NOT EXISTS "${PROJECT_ROOT}/${required}")
    message(FATAL_ERROR "missing ${required}")
  endif()
endforeach()

list(LENGTH tools_list tool_count)
message(STATUS "tools-inventory-static: ${tool_count} tools consistent across CMake, docs, packaging and CI")

# See the LICENSE file at the top of the project tree for copyright
# and license details.
#
# openi2pd: check upstream pins.
#
# Compile-free equality check between the synchronization script, the
# documentation tables and the interoperability reference checker. It never
# clones, fetches or executes anything.
#

if(NOT PROJECT_ROOT)
  message(FATAL_ERROR "PROJECT_ROOT is required")
endif()

function(read_required path out_var)
  if(NOT EXISTS "${path}")
    message(FATAL_ERROR "Missing upstream pin file: ${path}")
  endif()
  file(READ "${path}" contents)
  set(${out_var} "${contents}" PARENT_SCOPE)
endfunction()

function(extract text pattern description out_var)
  string(REGEX REPLACE ".*${pattern}.*" "\\1" value "${text}")
  if(value STREQUAL text OR value STREQUAL "")
    message(FATAL_ERROR "cannot extract ${description}")
  endif()
  string(LENGTH "${value}" value_length)
  if(NOT value_length EQUAL 40)
    message(FATAL_ERROR "${description} is not a 40-character commit: ${value}")
  endif()
  set(${out_var} "${value}" PARENT_SCOPE)
endfunction()

read_required("${PROJECT_ROOT}/contrib/upstream/sync.sh" sync)
read_required("${PROJECT_ROOT}/docs/upstream-sync.md" sync_doc)
read_required("${PROJECT_ROOT}/docs/tools/provenance.md" tools_doc)
read_required("${PROJECT_ROOT}/tests/interop/check-reference.sh" interop)
read_required("${PROJECT_ROOT}/tests/interop/FIXTURES.md" fixtures)
read_required("${PROJECT_ROOT}/docs/INTEROPERABILITY.md" interop_doc)

extract("${sync}" "DOCS_COMMIT=([0-9a-f]+)" "DOCS_COMMIT" docs_commit)
extract("${sync}" "TOOLS_COMMIT=([0-9a-f]+)" "TOOLS_COMMIT" tools_commit)
extract("${interop}" "\\$commit\" = \"([0-9a-f]+)\"" "Java commit" java_commit)

foreach(doc IN ITEMS sync_doc tools_doc)
  if(doc STREQUAL "sync_doc")
    set(text "${sync_doc}")
    set(label "upstream-sync.md")
  elseif(doc STREQUAL "interop_doc")
    set(text "${interop_doc}")
    set(label "INTEROPERABILITY.md")
  elseif(doc STREQUAL "fixtures")
    set(text "${fixtures}")
    set(label "tests/interop/FIXTURES.md")
  else()
    set(text "${tools_doc}")
    set(label "docs/tools/provenance.md")
  endif()
  if(label STREQUAL "docs/tools/provenance.md")
    string(FIND "${text}" "${tools_commit}" position)
    if(position EQUAL -1)
      message(FATAL_ERROR "${label} does not pin i2pd-tools ${tools_commit}")
    endif()
  else()
    string(FIND "${text}" "${docs_commit}" position)
    if(position EQUAL -1)
      message(FATAL_ERROR "${label} does not pin i2pd-docs ${docs_commit}")
    endif()
  endif()
endforeach()

string(FIND "${interop_doc}" "i2p-2.14.0" tag_position)
if(tag_position EQUAL -1)
  message(FATAL_ERROR "INTEROPERABILITY.md does not name the Java 2.14.0 target")
endif()
string(FIND "${interop_doc}" "${java_commit}" java_position)
if(java_position EQUAL -1)
  message(FATAL_ERROR "INTEROPERABILITY.md does not pin Java ${java_commit}")
endif()
string(FIND "${fixtures}" "${java_commit}" fixture_position)
if(fixture_position EQUAL -1)
  message(FATAL_ERROR "FIXTURES.md does not record Java ${java_commit}")
endif()

message(STATUS "upstream-pins-static: docs ${docs_commit}, tools ${tools_commit}, Java 2.14.0 ${java_commit}")

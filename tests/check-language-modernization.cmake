# See the LICENSE file at the top of the project tree for copyright
# and license details.
#
# openi2pd: check language modernization.
#
# Compile-free contract check for the permanent modern C++23/Java 25 policy,
# its documented availability limits and the presence of the reusable
# modernization seams. It never invokes a compiler, Gradle, CTest or a project
# binary.
#

if(NOT PROJECT_ROOT)
  message(FATAL_ERROR "PROJECT_ROOT is required")
endif()

function(read_required path out_var)
  if(NOT EXISTS "${path}")
    message(FATAL_ERROR "Missing language-modernization file: ${path}")
  endif()
  file(READ "${path}" contents)
  set(${out_var} "${contents}" PARENT_SCOPE)
endfunction()

function(assert_present text needle description)
  string(FIND "${text}" "${needle}" position)
  if(position EQUAL -1)
    message(FATAL_ERROR "${description}: missing required '${needle}'")
  endif()
endfunction()

set(agents "${PROJECT_ROOT}/AGENTS.md")
set(top_cmake "${PROJECT_ROOT}/build/CMakeLists.txt")
set(android_gradle "${PROJECT_ROOT}/android/app/build.gradle")
set(cpp_doc "${PROJECT_ROOT}/docs/cpp23-migration.md")
set(java_doc "${PROJECT_ROOT}/docs/android-java25.md")
set(cpp_skill "${PROJECT_ROOT}/.agents/skills/openi2pd-cpp23/SKILL.md")
set(android_skill "${PROJECT_ROOT}/.agents/skills/openi2pd-android/SKILL.md")
set(modern_header "${PROJECT_ROOT}/libi2pd/Modern.h")
set(connector_header "${PROJECT_ROOT}/libi2pd/TransportConnector.h")
set(transports_header "${PROJECT_ROOT}/libi2pd/Transports.h")
set(tui_server_source "${PROJECT_ROOT}/daemon/TuiServer.cpp")
set(identity_source "${PROJECT_ROOT}/libi2pd/Identity.cpp")
set(command_java "${PROJECT_ROOT}/android/app/src/main/java/org/openi2pd/router/RouterCommand.java")
set(log_level_java "${PROJECT_ROOT}/android/app/src/main/java/org/openi2pd/router/LogLevel.java")
set(router_service_java "${PROJECT_ROOT}/android/app/src/main/java/org/openi2pd/router/RouterService.java")

foreach(path IN ITEMS
    "${agents}" "${top_cmake}" "${android_gradle}" "${cpp_doc}" "${java_doc}"
    "${cpp_skill}" "${android_skill}" "${modern_header}" "${connector_header}"
    "${transports_header}" "${tui_server_source}" "${identity_source}" "${command_java}" "${log_level_java}"
    "${router_service_java}")
  read_required("${path}" ignored)
endforeach()

read_required("${agents}" agents_text)
read_required("${top_cmake}" cmake_text)
read_required("${android_gradle}" gradle_text)
read_required("${cpp_doc}" cpp_doc_text)
read_required("${java_doc}" java_doc_text)
read_required("${cpp_skill}" cpp_skill_text)
read_required("${android_skill}" android_skill_text)
read_required("${modern_header}" modern_text)
read_required("${connector_header}" connector_text)
read_required("${transports_header}" transports_text)
read_required("${tui_server_source}" tui_server_text)
read_required("${identity_source}" identity_text)
read_required("${command_java}" command_text)
read_required("${log_level_java}" log_level_text)
read_required("${router_service_java}" router_service_text)

assert_present("${agents_text}" "Modern language utilization policy"
  "AGENTS.md must keep the permanent modernization policy")
assert_present("${agents_text}" "Agent Knowledge Maintenance"
  "AGENTS.md must keep the permanent knowledge-maintenance rule")
assert_present("${agents_text}" "even when an existing" "policy must justify non-performance modernization")
assert_present("${agents_text}" "virtual threads" "Java library-availability limits must remain explicit")
assert_present("${cmake_text}" "set(CMAKE_CXX_STANDARD 23)" "C++23 remains mandatory")
assert_present("${gradle_text}" "sourceCompatibility = JavaVersion.VERSION_25"
  "Java 25 source level remains mandatory")
assert_present("${gradle_text}" "targetCompatibility = JavaVersion.VERSION_25"
  "Java 25 target level remains mandatory")
assert_present("${gradle_text}" "minSdk 29" "Android API floor remains documented")
assert_present("${cpp_doc_text}" "Deliberate modernization policy" "C++ policy document")
assert_present("${cpp_doc_text}" "std::format" "C++ availability matrix must name guarded facilities")
assert_present("${java_doc_text}" "Modernization policy" "Java policy document")
assert_present("${java_doc_text}" "Source-language features" "source/library distinction")
assert_present("${cpp_skill_text}" "Modernization matrix" "C++ skill must expose the facility matrix")
assert_present("${android_skill_text}" "virtual threads" "Android skill must retain API limits")

assert_present("${modern_text}" "namespace i2p" "Modern.h namespace")
assert_present("${modern_text}" "ScopeExit" "Modern.h RAII utility")
assert_present("${connector_text}" "class ITransportConnector" "Strategy interface")
assert_present("${connector_text}" "FindTransportConnector" "transport strategy registry")
assert_present("${transports_text}" "std::jthread" "X25519 worker must retain RAII thread ownership")
assert_present("${transports_text}" "std::stop_token" "X25519 worker must retain cooperative cancellation")
assert_present("${tui_server_text}" "std::jthread" "TUI IPC server must retain RAII thread ownership")
assert_present("${tui_server_text}" "std::stop_token" "TUI IPC server must retain cooperative cancellation")
assert_present("${identity_text}" "GetSigningKeySizes" "Identity wire-size registry must remain")
assert_present("${command_text}" "sealed interface" "RouterCommand must be sealed")
assert_present("${command_text}" "record" "RouterCommand variants must use records")
assert_present("${log_level_text}" "enum LogLevel" "typed log level is required")
assert_present("${router_service_text}" "switch (command)" "RouterService must use typed dispatch")

message(STATUS "language-modernization-static: OK")

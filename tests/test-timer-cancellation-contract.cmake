# See the LICENSE file at the top of the project tree for copyright
# and license details.
#
# Negative checker fixtures; execution belongs to manual CI.

if(NOT PROJECT_ROOT OR NOT BINARY_DIR)
  message(FATAL_ERROR "PROJECT_ROOT and BINARY_DIR are required")
endif()
string(RANDOM LENGTH 16 ALPHABET 0123456789abcdef fixture_suffix)
set(fixture "${BINARY_DIR}/timer-cancellation-${fixture_suffix}")
if(EXISTS "${fixture}")
  message(FATAL_ERROR "Timer policy fixture directory already exists")
endif()
file(MAKE_DIRECTORY "${fixture}/libi2pd_client")
set(checker "${PROJECT_ROOT}/tests/check-timer-cancellation.cmake")

function(check_timer_case header source expected)
  file(WRITE "${fixture}/libi2pd_client/Case.h" "${header}")
  file(WRITE "${fixture}/libi2pd_client/Case.cpp" "${source}")
  file(READ "${fixture}/libi2pd_client/Case.h" observed_header)
  file(READ "${fixture}/libi2pd_client/Case.cpp" observed_source)
  if(NOT observed_header STREQUAL header OR NOT observed_source STREQUAL source)
    message(FATAL_ERROR "Timer policy fixture did not preserve its input")
  endif()
  execute_process(COMMAND "${CMAKE_COMMAND}" "-DPROJECT_ROOT=${fixture}"
    -P "${checker}" RESULT_VARIABLE status OUTPUT_VARIABLE output
    ERROR_VARIABLE error TIMEOUT 10)
  if(NOT status STREQUAL expected)
    message(FATAL_ERROR "Unexpected timer policy result: ${status}; ${output}${error}")
  endif()
  if(expected STREQUAL "1" AND NOT "${output}${error}" MATCHES
      "Timer cancel[(]error_code[)] is not portable")
    message(FATAL_ERROR "Timer policy must diagnose operand-bearing cancellation")
  endif()
endfunction()

function(check_timer_mutation header pattern)
  string(FIND "${pattern}" "@OPERAND@" marker)
  if(marker LESS 0)
    message(FATAL_ERROR "Timer mutation lacks its operand marker")
  endif()
  string(REPLACE "@OPERAND@" "" valid "${pattern}")
  string(REPLACE "@OPERAND@" "renamedError" invalid "${pattern}")
  if(valid STREQUAL invalid OR invalid MATCHES "@OPERAND@")
    message(FATAL_ERROR "Timer mutation did not change its operand")
  endif()
  check_timer_case("${header}" "${valid}" "0")
  check_timer_case("${header}" "${invalid}" "1")
endfunction()

foreach(pattern IN ITEMS
    "m_AckTimer.cancel (@OPERAND@);"
    "m_Timer->cancel (@OPERAND@);"
    "deadline . cancel (\n  @OPERAND@\n);")
  check_timer_mutation("" "${pattern}")
endforeach()

# Header-declared pointer members can use names without Timer/Deadline.
check_timer_mutation(
  "std::unique_ptr<boost::asio::steady_timer> deadline, retry;\n"
  "attempt->retry -> cancel (\n @OPERAND@\n);\n")
check_timer_mutation(
  "boost::asio::steady_timer neutral;\n"
  "neutral.cancel (@OPERAND@);\n")
check_timer_mutation(
  "boost::asio::basic_waitable_timer<std::chrono::steady_clock> alarm;\n"
  "alarm.cancel (@OPERAND@);\n")
check_timer_mutation(""
  "std::unique_ptr<boost::asio::steady_timer> wake;\nwake->cancel (@OPERAND@);\n")

# The same identifier in an unrelated source must retain its socket API.
file(WRITE "${fixture}/libi2pd_client/Other.cpp"
  "boost::asio::ip::tcp::socket neutral;\nneutral.cancel (socketError);\n")
check_timer_case("boost::asio::steady_timer neutral;\n"
  "neutral.cancel ();\nIPSocket.cancel (ignored);\nm_Acceptor.cancel (ec);\n" "0")
file(REMOVE_RECURSE "${fixture}")

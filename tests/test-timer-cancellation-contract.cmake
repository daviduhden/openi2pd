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
foreach(bad IN ITEMS
    "m_AckTimer.cancel (ignored);"
    "m_AckTimer.cancel (cancelError);"
    "m_Timer->cancel (otherError);"
    "deadline . cancel (\n  renamedError\n);")
  file(WRITE "${fixture}/libi2pd_client/Case.cpp" "${bad}\n")
  execute_process(COMMAND "${CMAKE_COMMAND}" "-DPROJECT_ROOT=${fixture}"
    -P "${checker}" RESULT_VARIABLE status OUTPUT_VARIABLE output
    ERROR_VARIABLE error TIMEOUT 10)
  if(NOT status STREQUAL "1" OR NOT "${output}${error}" MATCHES
      "Timer cancel[(]error_code[)] is not portable")
    message(FATAL_ERROR "Timer policy must reject operand-bearing cancellation")
  endif()
endforeach()
file(WRITE "${fixture}/libi2pd_client/Case.cpp"
  "m_AckTimer.cancel ();\nIPSocket.cancel (ignored);\nm_Acceptor.cancel (ec);\n")
execute_process(COMMAND "${CMAKE_COMMAND}" "-DPROJECT_ROOT=${fixture}"
  -P "${checker}" RESULT_VARIABLE status OUTPUT_VARIABLE output
  ERROR_VARIABLE error TIMEOUT 10)
file(REMOVE_RECURSE "${fixture}")
if(NOT status STREQUAL "0")
  message(FATAL_ERROR "Portable timer and socket APIs must remain accepted")
endif()

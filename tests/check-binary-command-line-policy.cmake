# See the LICENSE file at the top of the project tree for copyright
# and license details.
#
# End-to-end CLI refusal with a working informational control. Synthetic
# operands are never echoed, including when a regression discloses them.

if(NOT DEFINED BINARY OR NOT EXISTS "${BINARY}")
  message(FATAL_ERROR "BINARY must name the router executable")
endif()
execute_process(COMMAND "${BINARY}" --version RESULT_VARIABLE control_status
  OUTPUT_VARIABLE control_output ERROR_VARIABLE control_error TIMEOUT 10)
if(NOT control_status STREQUAL "0")
  message(FATAL_ERROR "CLI policy informational control failed")
endif()
if("${control_output}${control_error}" MATCHES
    "AddressSanitizer|LeakSanitizer|ThreadSanitizer|UndefinedBehaviorSanitizer|runtime error:")
  message(FATAL_ERROR "CLI policy control reported a sanitizer failure")
endif()
foreach(option IN ITEMS httpproxy.auth.username httpproxy.auth.password
    socksproxy.auth.username socksproxy.auth.password)
  foreach(request IN ITEMS --version --help)
    foreach(order IN ITEMS before after)
      if(order STREQUAL "before")
        set(operands "${request}" "--${option}" "command-line-policy-sentinel")
      else()
        set(operands "--${option}" "command-line-policy-sentinel" "${request}")
      endif()
      execute_process(COMMAND "${BINARY}" ${operands}
        RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE error TIMEOUT 5)
      if(NOT status STREQUAL "1")
        message(FATAL_ERROR "Router must refuse explicit CLI credentials")
      endif()
      string(FIND "${output}${error}" "Proxy credentials are configuration-file only" outcome)
      if(outcome EQUAL -1)
        message(FATAL_ERROR "Router CLI refusal lacks the public outcome")
      endif()
      if("${output}${error}" MATCHES
          "command-line-policy-sentinel|AddressSanitizer|LeakSanitizer|ThreadSanitizer|UndefinedBehaviorSanitizer|runtime error:|openi2pd version|Boost version")
        message(FATAL_ERROR "Router CLI refusal leaked input or bypassed policy")
      endif()
    endforeach()
  endforeach()
endforeach()

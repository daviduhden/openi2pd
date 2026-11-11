# See the LICENSE file at the top of the project tree for copyright
# and license details.
#
# Verify fatal parser refusal in isolated child processes. Exact exit
# status and public diagnostics are required; WILL_FAIL could hide a
# sanitizer failure. Captured operands are never printed on failure.

cmake_policy(SET CMP0012 NEW)

if(NOT DEFINED TEST_BINARY OR NOT EXISTS "${TEST_BINARY}")
  message(FATAL_ERROR "TEST_BINARY must name the policy fixture")
endif()
if(NOT DEFINED BINARY_DIR OR NOT IS_DIRECTORY "${BINARY_DIR}")
  message(FATAL_ERROR "BINARY_DIR must name an existing fixture directory")
endif()
if(NOT DEFINED CHILD_TIMEOUT)
  set(CHILD_TIMEOUT 5)
endif()
if(NOT CHILD_TIMEOUT MATCHES "^[0-9]+$")
  message(FATAL_ERROR "CHILD_TIMEOUT must be an integer")
endif()
if(CHILD_TIMEOUT LESS 1 OR CHILD_TIMEOUT GREATER 10)
  message(FATAL_ERROR "CHILD_TIMEOUT must be between 1 and 10 seconds")
endif()

function(expect_policy_refusal label expected)
  execute_process(
    COMMAND "${TEST_BINARY}" ${ARGN}
    RESULT_VARIABLE child_status
    OUTPUT_VARIABLE child_output
    ERROR_VARIABLE child_error
    TIMEOUT "${CHILD_TIMEOUT}")
  set(captured "${child_output}${child_error}")

  # Treat any sanitizer finding as failure even if its process returned
  # the same nonzero status as an intentional parser refusal.
  if(captured MATCHES
      "AddressSanitizer|LeakSanitizer|ThreadSanitizer|UndefinedBehaviorSanitizer|runtime error:|Sanitizer:DEADLYSIGNAL")
    message(FATAL_ERROR "command-line policy ${label}: sanitizer diagnostic")
  endif()
  if(captured MATCHES
      "command-line-policy-sentinel|second-policy-sentinel|OPENI2PD_CREDENTIAL_SENTINEL")
    message(FATAL_ERROR "command-line policy ${label}: rejected data disclosed")
  endif()
  if(captured MATCHES
      "Unexpected fatal policy diagnostic|unexpectedly continued|openi2pd version|Boost version")
    message(FATAL_ERROR "command-line policy ${label}: refusal was bypassed")
  endif()
  if(NOT child_status STREQUAL "1")
    message(FATAL_ERROR "command-line policy ${label}: expected refusal exit 1")
  endif()
  string(FIND "${captured}" "${expected}" diagnostic)
  if(diagnostic EQUAL -1)
    message(FATAL_ERROR "command-line policy ${label}: missing public outcome")
  endif()
endfunction()

set(credential_error
  "Proxy authentication credentials must be set in the configuration file, not on the command line")
foreach(index RANGE 0 3)
  foreach(kind IN ITEMS empty value)
    foreach(request IN ITEMS normal help version)
      expect_policy_refusal("credential-${index}-${kind}-${request}"
        "${credential_error}" --reject-explicit "${index}" "${kind}" "${request}")
    endforeach()
  endforeach()
endforeach()

foreach(kind IN ITEMS unknown numeric credential-syntax credential-duplicate)
  expect_policy_refusal("malformed-${kind}" "Error while parsing arguments"
    --reject-malformed "${kind}")
endforeach()

# The malformed INI contains only a synthetic marker. Keep parallel
# invocations separate and never write beside the source fixture.
string(RANDOM LENGTH 16 ALPHABET 0123456789abcdef fixture_suffix)
set(fixture_dir "${BINARY_DIR}/command-line-policy-${fixture_suffix}")
if(EXISTS "${fixture_dir}")
  message(FATAL_ERROR "command-line policy: temporary directory already exists")
endif()
file(MAKE_DIRECTORY "${fixture_dir}")
set(malformed_config "${fixture_dir}/malformed.ini")
file(WRITE "${malformed_config}"
  "[httpproxy]\nport=command-line-policy-sentinel\n")
expect_policy_refusal("malformed-config" "Error while parsing config file"
  --reject-malformed config "${malformed_config}")
expect_policy_refusal("missing-config" "Missing or unreadable config file"
  --reject-malformed missing-config
  "${fixture_dir}/command-line-policy-sentinel-missing.ini")
file(REMOVE_RECURSE "${fixture_dir}")

message(STATUS "command-line-policy: explicit and malformed operands refused")

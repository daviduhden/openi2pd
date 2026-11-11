# See the LICENSE file at the top of the project tree for copyright
# and license details.
#
# Inspect native timer cancellation without configuring or compiling.

if(NOT PROJECT_ROOT)
  message(FATAL_ERROR "PROJECT_ROOT is required")
endif()
file(GLOB_RECURSE timer_policy_sources
  "${PROJECT_ROOT}/libi2pd/*.cpp" "${PROJECT_ROOT}/libi2pd/*.h"
  "${PROJECT_ROOT}/libi2pd_client/*.cpp" "${PROJECT_ROOT}/libi2pd_client/*.h"
  "${PROJECT_ROOT}/daemon/*.cpp" "${PROJECT_ROOT}/daemon/*.h"
  "${PROJECT_ROOT}/privsep/*.cpp" "${PROJECT_ROOT}/privsep/*.h"
  "${PROJECT_ROOT}/tui/*.cpp" "${PROJECT_ROOT}/tui/*.h"
  "${PROJECT_ROOT}/tools/*.cpp" "${PROJECT_ROOT}/tools/*.h"
  "${PROJECT_ROOT}/tests/*.cpp" "${PROJECT_ROOT}/tests/*.h"
  "${PROJECT_ROOT}/android/app/src/main/cpp/*.cpp"
  "${PROJECT_ROOT}/android/app/src/main/cpp/*.h")
foreach(timer_policy_source IN LISTS timer_policy_sources)
  file(READ "${timer_policy_source}" timer_policy_text)
  # Name-based coverage includes direct and pointer timer/deadline members.
  # Socket and acceptor overloads are independent APIs and remain valid.
  if(timer_policy_text MATCHES
      "[A-Za-z0-9_]*([Tt]imer|[Dd]eadline)[0-9]*[ \t\r\n]*([.]|->)[ \t\r\n]*cancel[ \t\r\n]*\\([ \t\r\n]*[^ \t\r\n)]")
    message(FATAL_ERROR "Timer cancel(error_code) is not portable: ${timer_policy_source}")
  endif()
endforeach()

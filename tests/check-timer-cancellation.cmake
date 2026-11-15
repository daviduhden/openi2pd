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
  # Keep the existing name heuristic for aliases and inherited members.
  # Socket and acceptor overloads are independent APIs and remain valid.
  if(timer_policy_text MATCHES
      "[A-Za-z0-9_]*([Tt]imer|[Dd]eadline)[0-9]*[ \t\r\n]*([.]|->)[ \t\r\n]*cancel[ \t\r\n]*\\([ \t\r\n]*[^ \t\r\n)]")
    message(FATAL_ERROR "Timer cancel(error_code) is not portable: ${timer_policy_source}")
  endif()

  # Concrete declarations also identify neutral names such as 'retry'.
  # Pair a source with its same-stem header, never all repository headers:
  # unrelated socket identifiers must not acquire another class's type.
  get_filename_component(timer_policy_directory "${timer_policy_source}" DIRECTORY)
  get_filename_component(timer_policy_stem "${timer_policy_source}" NAME_WE)
  set(timer_policy_declarations "${timer_policy_text}")
  foreach(timer_policy_extension IN ITEMS h cpp)
    set(timer_policy_pair "${timer_policy_directory}/${timer_policy_stem}.${timer_policy_extension}")
    if(EXISTS "${timer_policy_pair}" AND NOT timer_policy_pair STREQUAL timer_policy_source)
      file(READ "${timer_policy_pair}" timer_policy_pair_text)
      string(APPEND timer_policy_declarations "\n${timer_policy_pair_text}")
    endif()
  endforeach()
  # Exclude semicolons from matches so declaration lists remain intact.
  # This is a bounded lexical check, not C++ type or scope resolution.
  string(REGEX MATCHALL
    "(boost::)?asio::(steady_timer|deadline_timer|system_timer|high_resolution_timer|basic_waitable_timer<[^;{}]+>)[> \t\r\n*&]+[A-Za-z_][A-Za-z0-9_, \t\r\n]*"
    timer_policy_typed_declarations "${timer_policy_declarations}")
  foreach(timer_policy_declaration IN LISTS timer_policy_typed_declarations)
    string(REGEX REPLACE
      "^(boost::)?asio::(steady_timer|deadline_timer|system_timer|high_resolution_timer|basic_waitable_timer<[^;{}]+>)[> \t\r\n*&]+"
      "" timer_policy_names "${timer_policy_declaration}")
    string(REPLACE "," ";" timer_policy_names "${timer_policy_names}")
    foreach(timer_policy_name IN LISTS timer_policy_names)
      string(STRIP "${timer_policy_name}" timer_policy_name)
      if(timer_policy_name MATCHES "^[A-Za-z_][A-Za-z0-9_]*$" AND timer_policy_text MATCHES
          "(^|[^A-Za-z0-9_])${timer_policy_name}[ \t\r\n]*([.]|->)[ \t\r\n]*cancel[ \t\r\n]*\\([ \t\r\n]*[^ \t\r\n)]")
        message(FATAL_ERROR "Timer cancel(error_code) is not portable: ${timer_policy_source}")
      endif()
    endforeach()
  endforeach()
endforeach()

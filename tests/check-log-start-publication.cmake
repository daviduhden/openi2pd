# See the LICENSE file at the top of the project tree for copyright and
# license details.
#
# openi2pd: compile-free logger generation publication contract.

if(NOT PROJECT_ROOT)
  message(FATAL_ERROR "PROJECT_ROOT is required")
endif()
file(READ "${PROJECT_ROOT}/libi2pd/Log.cpp" logger)
string(FIND "${logger}" "void Log::Start ()" begin)
string(FIND "${logger}" "void Log::Stop ()" end)
if(begin LESS 0 OR end LESS_EQUAL begin)
  message(FATAL_ERROR "Logger start/stop boundaries missing")
endif()
math(EXPR length "${end} - ${begin}")
string(SUBSTRING "${logger}" ${begin} ${length} start)
string(FIND "${start}" "m_StartRequested = false;" reset)
string(FIND "${start}" "m_Thread = std::make_unique<std::thread>" launch)
string(FIND "${start}" "m_StartRequested = true;" publish)
if(reset LESS 0 OR launch LESS_EQUAL reset OR publish LESS_EQUAL launch)
  message(FATAL_ERROR "Logger must re-arm publication before each new writer")
endif()
foreach(required IN ITEMS "m_LifecycleMutex" "m_StartMutex"
    "m_IsRunning.store (true, std::memory_order_release)"
    "m_StartCondition.notify_all ()")
  string(FIND "${start}" "${required}" found)
  if(found LESS 0)
    message(FATAL_ERROR "Logger generation publication missing: ${required}")
  endif()
endforeach()

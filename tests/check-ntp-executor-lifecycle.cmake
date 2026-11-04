# See the LICENSE file at the top of the project tree for copyright and
# license details.
#
# openi2pd: compile-free NTP executor startup and restart contract.

if(NOT PROJECT_ROOT)
  message(FATAL_ERROR "PROJECT_ROOT is required")
endif()

file(READ "${PROJECT_ROOT}/libi2pd/Timestamp.cpp" timestamp)
foreach(method IN ITEMS Start Stop Run)
  string(FIND "${timestamp}" "void NTPTimeSync::${method} ()" ${method}_begin)
  if(${method}_begin LESS 0)
    message(FATAL_ERROR "NTP ${method} implementation boundary missing")
  endif()
endforeach()
string(FIND "${timestamp}" "void NTPTimeSync::Sync ()" Sync_begin)
if(Sync_begin LESS 0)
  message(FATAL_ERROR "NTP Sync implementation boundary missing")
endif()

math(EXPR start_length "${Stop_begin} - ${Start_begin}")
string(SUBSTRING "${timestamp}" ${Start_begin} ${start_length} startup)
string(FIND "${startup}" "boost::asio::post (m_Service" initial_post)
string(FIND "${startup}" "m_Thread = std::make_unique<std::thread>" launch)
if(initial_post LESS 0 OR launch LESS_EQUAL initial_post)
  message(FATAL_ERROR "NTP initial work must precede thread launch")
endif()
string(FIND "${startup}" "m_Service.poll ();" failed_start_drain)
if(failed_start_drain LESS_EQUAL launch)
  message(FATAL_ERROR "Failed NTP thread launch must retire queued work")
endif()

math(EXPR stop_length "${Run_begin} - ${Stop_begin}")
string(SUBSTRING "${timestamp}" ${Stop_begin} ${stop_length} shutdown)
string(FIND "${shutdown}" "m_Thread->join ();" join)
string(FIND "${shutdown}" "m_Service.poll ();" drain)
if(join LESS 0 OR drain LESS_EQUAL join)
  message(FATAL_ERROR "NTP shutdown must drain stale work after joining")
endif()

math(EXPR run_length "${Sync_begin} - ${Run_begin}")
string(SUBSTRING "${timestamp}" ${Run_begin} ${run_length} run)
string(FIND "${run}" "m_Service.run ();" dispatch)
string(FIND "${run}" "break;" exhausted)
string(FIND "${run}" "catch (std::exception" handler_exception)
if(dispatch LESS 0 OR exhausted LESS_EQUAL dispatch OR handler_exception LESS_EQUAL exhausted)
  message(FATAL_ERROR "NTP normal run exhaustion must exit before exception retry")
endif()

# See the LICENSE file at the top of the project tree for copyright
# and license details.
#
# openi2pd: compile-free BOB and I2PControl security contracts.

if(NOT PROJECT_ROOT)
  message(FATAL_ERROR "PROJECT_ROOT is required")
endif()

file(READ "${PROJECT_ROOT}/libi2pd_client/BOB.cpp" bob)
file(READ "${PROJECT_ROOT}/daemon/I2PControl.cpp" control)
file(READ "${PROJECT_ROOT}/daemon/I2PControl.h" control_header)

if(bob MATCHES "setkeys[^\n]*operand")
  message(FATAL_ERROR "BOB setkeys must never log or format its secret operand")
endif()
foreach(required IN ITEMS
    "weak_ptr<BOBI2PInboundTunnel>"
    "weak_ptr<BOBI2POutboundTunnel>"
    "m_Stopped"
    "m_AdmissionMutex"
    "m_Receivers")
  string(FIND "${bob}" "${required}" position)
  if(position EQUAL -1)
    message(FATAL_ERROR "Missing BOB lifetime contract: ${required}")
  endif()
endforeach()

if(control MATCHES "New password=")
  message(FATAL_ERROR "I2PControl must not log administrative password values")
endif()
if(control MATCHES "boost::asio::read[ ]*\\(")
  message(FATAL_ERROR "I2PControl request reads must remain asynchronous")
endif()
foreach(required IN ITEMS
    "ArmControlDeadline"
    "transfer_exactly (4)"
    "async_read_until"
    "I2P_CONTROL_MAX_CONNECTIONS"
    "I2P_CONTROL_MAX_TOKENS"
    "WritePEMAtomically"
    "AtomicWritePrivate"
    "m_Connections"
    "refuses a non-loopback listener with the default password")
  string(FIND "${control}${control_header}" "${required}" position)
  if(position EQUAL -1)
    message(FATAL_ERROR "Missing I2PControl security contract: ${required}")
  endif()
endforeach()

file(READ "${PROJECT_ROOT}/libi2pd/FS.cpp" filesystem)
foreach(required IN ITEMS
    "BuildPrivateFileSecurity"
    "SE_DACL_PROTECTED"
    "FILE_FLAG_OPEN_REPARSE_POINT"
    "fchmod (fd, S_IRUSR | S_IWUSR)")
  string(FIND "${filesystem}" "${required}" position)
  if(position EQUAL -1)
    message(FATAL_ERROR "Missing private atomic-file contract: ${required}")
  endif()
endforeach()

# See the LICENSE file at the top of the project tree for copyright and
# license details.
#
# openi2pd: compile-free registration and RPC diagnostic privacy contract.

if(NOT PROJECT_ROOT)
  message(FATAL_ERROR "PROJECT_ROOT is required")
endif()
file(READ "${PROJECT_ROOT}/tools/common/key.hpp" key)
foreach(required IN ITEMS "ValidateRecordName" "RecordNameErrorMessage"
    "RecordNameError::Control" "RecordNameError::Separator"
    "including DEL" "backslash" "255 bytes")
  string(FIND "${key}" "${required}" found)
  if(found LESS 0)
    message(FATAL_ERROR "Registration diagnostic grammar missing: ${required}")
  endif()
endforeach()
foreach(tool IN ITEMS regaddr regaddralias regaddr_3ld)
  file(READ "${PROJECT_ROOT}/tools/${tool}.cpp" source)
  foreach(required IN ITEMS "ValidateRecordName (argv[" "RecordNameErrorMessage (*error)")
    string(FIND "${source}" "${required}" found)
    if(found LESS 0)
      message(FATAL_ERROR "${tool} must share typed non-secret registration diagnostics")
    endif()
  endforeach()
  if(source MATCHES "std::cerr[^\n]*RecordNameErrorMessage[^\n]*argv")
    message(FATAL_ERROR "Rejected registration operands must never reach diagnostics")
  endif()
endforeach()
file(READ "${PROJECT_ROOT}/libi2pd_client/TorrentsRPC.cpp" rpc)
string(FIND "${rpc}" "LogPrint (eLogWarning, \"TorrentsRPC: Duplicate torrent request\");" diagnostic)
if(diagnostic LESS 0 OR rpc MATCHES "TorrentsRPC: duplicate torrent ")
  message(FATAL_ERROR "Duplicate RPC diagnostics must be fixed, bounded and operand-free")
endif()

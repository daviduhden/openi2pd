# See the LICENSE file at the top of the project tree for copyright and
# license details.
#
# openi2pd: compile-free single-flight storage reply authority contract.

if(NOT PROJECT_ROOT)
  message(FATAL_ERROR "PROJECT_ROOT is required")
endif()

file(READ "${PROJECT_ROOT}/privsep/DataStorage.cpp" storage)
string(FIND "${storage}" "bool Operate (" begin)
string(FIND "${storage}" "DataStorageClient::DataStorageClient ()" end)
if(begin LESS 0 OR end LESS_EQUAL begin)
  message(FATAL_ERROR "Data-storage operation boundaries missing")
endif()
math(EXPR length "${end} - ${begin}")
string(SUBSTRING "${storage}" ${begin} ${length} operation)

# A valid envelope alone cannot correlate a reply: ID, worker generation,
# artifact kind and typed Error's offending operation must all match.
foreach(required IN ITEMS
    "auto error = DecodeError (message.payload);"
    "message.requestId != id || !error || std::get<0> (*error) != MessageType::DataStorageRequest"
    "message.type != MessageType::DataStorageReply || !decoded"
    "std::get<1> (*decoded) != static_cast<uint8_t> (kind)"
    "!IsCurrentDataStorageReply (std::get<2> (*decoded), message.requestId, generation, id)"
    "MarkRestart (\"uncorrelated worker error\");"
    "MarkRestart (\"invalid or uncorrelated worker reply\");")
  string(FIND "${operation}" "${required}" found)
  if(found LESS 0)
    message(FATAL_ERROR "Data-storage reply authority missing: ${required}")
  endif()
endforeach()

# Do not restore the old behavior of returning Busy while leaving the
# real reply queued: every later request would consume its predecessor.
string(FIND "${operation}" "auto decoded = DecodeDataStorageReply (message.payload);" reply_begin)
string(FIND "${operation}" "const auto status = static_cast<DataStorageStatus>" reply_end)
if(reply_begin LESS 0 OR reply_end LESS_EQUAL reply_begin)
  message(FATAL_ERROR "Data-storage reply validation boundaries missing")
endif()
math(EXPR reply_length "${reply_end} - ${reply_begin}")
string(SUBSTRING "${operation}" ${reply_begin} ${reply_length} reply_validation)
string(FIND "${reply_validation}" "MarkRestart (\"invalid or uncorrelated worker reply\");" retired)
string(FIND "${reply_validation}" "return false;" returned)
if(retired LESS 0 OR returned LESS retired)
  message(FATAL_ERROR "Invalid storage reply must retire the channel before returning")
endif()
string(FIND "${reply_validation}" "channel.Send" replay)
if(NOT replay LESS 0)
  message(FATAL_ERROR "Invalid storage reply must not replay the ambiguous operation")
endif()

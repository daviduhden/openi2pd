# See the LICENSE file at the top of the project tree for copyright and
# license details.
#
# openi2pd: compile-free pending ratchet split ownership contract.

if(NOT PROJECT_ROOT)
  message(FATAL_ERROR "PROJECT_ROOT is required")
endif()
file(READ "${PROJECT_ROOT}/libi2pd/ECIESX25519AEADRatchetSession.h" header)
file(READ "${PROJECT_ROOT}/libi2pd/ECIESX25519AEADRatchetSession.cpp" source)
string(FIND "${header}" "std::weak_ptr<ReceiveRatchetTagSet> receive;" weak_pending)
string(FIND "${header}" "std::shared_ptr<ECIESX25519AEADRatchetSession> m_Session;" retained_session)
if(weak_pending LESS 0 OR retained_session LESS 0)
  message(FATAL_ERROR "Pending reply correlation must borrow while inbound tags retain sessions")
endif()
string(FIND "${source}" "void ECIESX25519AEADRatchetSession::AddPendingReplyTagset (" begin)
string(FIND "${source}" "bool ECIESX25519AEADRatchetSession::NewOutgoingSessionMessage (" end)
if(begin LESS 0 OR end LESS_EQUAL begin)
  message(FATAL_ERROR "Pending reply helper boundaries missing")
endif()
math(EXPR length "${end} - ${begin}")
string(SUBSTRING "${source}" ${begin} ${length} pending)
foreach(forbidden IN ITEMS "it.receive->" "it->receive.get ()" "if (it.receive)")
  string(FIND "${pending}" "${forbidden}" found)
  if(NOT found LESS 0)
    message(FATAL_ERROR "Pending receive entries must be locked before access")
  endif()
endforeach()
foreach(required IN ITEMS "if (auto receive = it.receive.lock ()) receive->Expire ();"
    "receive = it->receive.lock ();" "receive && receive == receiveTagset")
  string(FIND "${pending}" "${required}" found)
  if(found LESS 0)
    message(FATAL_ERROR "Missing pending receive lifetime guard: ${required}")
  endif()
endforeach()

# Successful weak locks remain alive beyond the pending mutex scope;
# their final destruction may call back into the session's state lock.
string(FIND "${pending}" "TakeMatchingPendingSendTagset (" begin)
string(SUBSTRING "${pending}" ${begin} -1 matching)
string(FIND "${matching}" "receiveOwners;" captured)
string(FIND "${matching}" "std::lock_guard<std::mutex> l (m_PendingReplyTagsetsMutex);" locked)
if(captured LESS 0 OR locked LESS_EQUAL captured)
  message(FATAL_ERROR "Pending weak locks must outlive the pending mutex")
endif()

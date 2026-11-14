# See the LICENSE file at the top of the project tree for copyright and
# license details.
#
# openi2pd: compile-free native fair upload-slot integration contracts.

if(NOT PROJECT_ROOT)
  message(FATAL_ERROR "PROJECT_ROOT is required")
endif()
file(READ "${PROJECT_ROOT}/libi2pd_client/Torrents.cpp" source)
file(READ "${PROJECT_ROOT}/libi2pd_client/Torrents.h" header)

foreach(required IN ITEMS
    "class UploadSlotPolicy final"
    "MaxSlots = 8;"
    "RotationSeconds = 30;"
    "UploadSlotPolicy m_UploadSlots;"
    "std::vector<RequestedBlock> CancelForChoke ();"
    "friend class Torrent;")
  string(FIND "${header}" "${required}" found)
  if(found LESS 0)
    message(FATAL_ERROR "Missing native upload-slot contract: ${required}")
  endif()
endforeach()
foreach(required IN ITEMS
    "RefreshUploadSlots (ts);"
    "m_Torrent->RefreshUploadSlots ();"
    "m_UploadSlots.Reset ();"
    "connection->IsUploadSlotCandidate ()"
    "m_UploadSlots.Selected (*ident)"
    "std::upper_bound (candidates.begin (), candidates.end (), *m_Cursor)"
    "now - *m_LastRotation >= RotationSeconds"
    "ticket->cancelled.exchange (true, std::memory_order_acq_rel)"
    "m_UploadLoads.CancelForChoke ();"
    "queued.splice (queued.end (), m_IncomingRequestsQueue);"
    "for (const auto & [index, offset, length] : revoked) SendRejectRequestMsg (index, offset, length);")
  string(FIND "${source}" "${required}" found)
  if(found LESS 0)
    message(FATAL_ERROR "Upload-slot ownership integration missing: ${required}")
  endif()
endforeach()

# Rotation must revoke former grants before authorizing new peers.
string(FIND "${source}" "void Torrent::RefreshUploadSlots (" begin)
string(FIND "${source}" "bool Torrent::IsConnectedToPeer (" end)
if(begin LESS 0 OR end LESS_EQUAL begin)
  message(FATAL_ERROR "Upload-slot application boundaries missing")
endif()
math(EXPR length "${end} - ${begin}")
string(SUBSTRING "${source}" ${begin} ${length} apply_slots)
string(FIND "${apply_slots}" "connection->SetUploadAllowed (false)" revoke)
string(FIND "${apply_slots}" "connection->SetUploadAllowed (true)" grant)
if(revoke LESS 0 OR grant LESS revoke)
  message(FATAL_ERROR "Upload slots must choke before granting replacements")
endif()

# INTERESTED cannot bypass the torrent's slot authority.
string(FIND "${source}" "case eMessageTypeInterested:" interested_begin)
string(FIND "${source}" "case eMessageTypeHave:" interested_end)
if(interested_begin LESS 0 OR interested_end LESS_EQUAL interested_begin)
  message(FATAL_ERROR "Interested message dispatch boundaries missing")
endif()
math(EXPR interested_length "${interested_end} - ${interested_begin}")
string(SUBSTRING "${source}" ${interested_begin} ${interested_length} interested)
foreach(forbidden IN ITEMS "SendUnchokeMsg" "m_IsRemoteChoked = false")
  string(FIND "${interested}" "${forbidden}" found)
  if(NOT found LESS 0)
    message(FATAL_ERROR "Interested peers must not unchoke themselves")
  endif()
endforeach()

# BEP 6 pending-request rejections follow the wire choke. Cancellation
# revokes publication while its reservations remain with the callback.
string(FIND "${source}" "void PeerConnection::SetUploadAllowed (" allowed_begin)
string(FIND "${source}" "void PeerConnection::SendChokeMsg (" allowed_end)
if(allowed_begin LESS 0 OR allowed_end LESS_EQUAL allowed_begin)
  message(FATAL_ERROR "Upload slot grant/revocation boundaries missing")
endif()
math(EXPR allowed_length "${allowed_end} - ${allowed_begin}")
string(SUBSTRING "${source}" ${allowed_begin} ${allowed_length} allowed)
string(FIND "${allowed}" "SendChokeMsg ();" choke)
string(FIND "${allowed}" "if (m_IsFast)" fast)
if(choke LESS 0 OR fast LESS choke)
  message(FATAL_ERROR "Fast rejections must follow upload-slot choke")
endif()
string(FIND "${allowed}" "m_UploadLoads.Release" released)
if(NOT released LESS 0)
  message(FATAL_ERROR "Choke must not release quota for pending disk work")
endif()

# Allocation during choke preparation belongs inside the same closing
# failure boundary as wire submission. Otherwise a removed slot retains
# its previous upload grant when CancelForChoke's reserve() throws.
string(FIND "${allowed}" "if (m_IsRemoteChoked) return;" revoke_begin)
if(revoke_begin LESS 0)
  message(FATAL_ERROR "Upload-slot revocation boundary missing")
endif()
string(SUBSTRING "${allowed}" ${revoke_begin} -1 revoke_body)
string(FIND "${revoke_body}" "try" guarded)
string(FIND "${revoke_body}" "m_UploadLoads.CancelForChoke ();" prepare)
string(FIND "${revoke_body}" "catch (...)" failed)
string(FIND "${revoke_body}" "Close ();" closed)
if(guarded LESS 0 OR prepare LESS guarded OR failed LESS prepare OR closed LESS failed)
  message(FATAL_ERROR "Choke preparation failure must close upload authority")
endif()

# Refresh snapshot/policy allocations can fail before revocation too.
# Its handler walks the registry directly, never allocating a fallback
# snapshot, and revokes each peer before queuing asynchronous teardown.
foreach(required IN ITEMS
    "void Torrent::RefreshUploadSlots (uint64_t now)\n\ttry"
    "for (const auto & entry : m_Connections)"
    "if (auto connection = entry.second.lock ())"
    "connection->m_IsRemoteChoked = true;"
    "connection->m_UploadDispatchFailed.store (true, std::memory_order_release);"
    "try\n\t\t\t\t{\n\t\t\t\t\tconnection->Close ();\n\t\t\t\t}")
  string(FIND "${apply_slots}" "${required}" found)
  if(found LESS 0)
    message(FATAL_ERROR "Upload policy failure must revoke registry authority: ${required}")
  endif()
endforeach()

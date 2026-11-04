# See the LICENSE file at the top of the project tree for copyright and
# license details.
#
# openi2pd: compile-free upload admission and cancellation contracts.

if(NOT PROJECT_ROOT)
  message(FATAL_ERROR "PROJECT_ROOT is required")
endif()
file(READ "${PROJECT_ROOT}/libi2pd_client/Torrents.cpp" source)
file(READ "${PROJECT_ROOT}/libi2pd_client/Torrents.h" header)

foreach(required IN ITEMS
    "class UploadLoadLedger final"
    "MaxGlobalLoads = MAX_TOTAL_PEERS * MAX_NUM_PIECES"
    "MaxGlobalBytes = MAX_PIECE_LENGTH"
    "std::shared_ptr<BudgetState> m_Budget;"
    "std::atomic<bool> cancelled { false };"
    "UploadLoadLedger m_UploadLoads;")
  string(FIND "${header}" "${required}" found)
  if(found LESS 0)
    message(FATAL_ERROR "Missing upload reservation contract: ${required}")
  endif()
endforeach()

string(FIND "${source}" "void PeerConnection::HandleRequestMsg (" begin)
string(FIND "${source}" "bool PeerConnection::SendRequestedBlock (" end)
if(begin LESS 0 OR end LESS_EQUAL begin)
  message(FATAL_ERROR "Upload admission/pump boundaries missing")
endif()
math(EXPR length "${end} - ${begin}")
string(SUBSTRING "${source}" ${begin} ${length} upload)
foreach(required IN ITEMS
    "if (m_IsRemoteChoked)"
    "m_IncomingRequestsQueue.size () + m_UploadLoads.Size () + m_NumPieces >= MAX_INCOMING_REQUESTS_QUEUE_SIZE"
    "m_NumPieces + m_UploadLoads.Size () < MAX_NUM_PIECES"
    "m_UploadLoads.Acquire ("
    "const std::weak_ptr<PeerConnection> weakPeer = weak_from_this ();"
    "const std::weak_ptr<TorrentsTunnel> weakTunnel = tunnel;"
    "piece.ReadVerifiedBlock ("
    "m_UploadLoads.Release (ticket)"
    "GetBlockEpoch () == ticket->epoch"
    "torrent->InvalidatePiece (index)"
    "m_UploadDispatchFailed.store (true, std::memory_order_release)")
  string(FIND "${upload}" "${required}" found)
  if(found LESS 0)
    message(FATAL_ERROR "Missing bounded upload contract: ${required}")
  endif()
endforeach()
foreach(forbidden IN ITEMS "[this" "s = shared_from_this ()" "piece.Load (" "piece.SetIsLoading (")
  string(FIND "${upload}" "${forbidden}" found)
  if(NOT found LESS 0)
    message(FATAL_ERROR "Unsafe upload disk path restored: ${forbidden}")
  endif()
endforeach()

# Only service completion may invalidate shared torrent state. The disk
# stage verifies private scratch and posts the result back to the owner.
string(FIND "${upload}" "boost::asio::post (tunnel->GetService ()," completion)
string(FIND "${upload}" "torrent->InvalidatePiece (index)" invalidation)
if(completion LESS 0 OR invalidation LESS completion)
  message(FATAL_ERROR "Upload failure must be applied on the service thread")
endif()

foreach(required IN ITEMS
    "else if (!m_UploadLoads.Cancel (cancelled))"
    "m_UploadLoads.Close ();"
    "m_Closed = true;"
    "pieceBytes > MaxGlobalBytes - budget->outstandingUploadBytes"
    "m_Budget->outstandingUploadBytes -= bytes;"
    "fragment.fragmentOffset != covered"
    "if (covered != m_Size) return false;"
    "if (!SHA1 (scratch.get (), m_Size, digest.data ()) || digest != expected) return false;"
    "m_Data = std::move (scratch);"
    "m_IsVerified = SHA1 (m_Data.get (), m_Size, digest) &&"
    "if (!SHA1 (m_Info.data (), len, m_InfoHash.data ()))")
  string(FIND "${source}" "${required}" found)
  if(found LESS 0)
    message(FATAL_ERROR "Upload cancellation/hash contract missing: ${required}")
  endif()
endforeach()

# A verified cache must be published only after scratch authentication.
string(FIND "${source}" "if (!SHA1 (scratch.get (), m_Size, digest.data ())" hashed)
string(FIND "${source}" "m_Data = std::move (scratch);" published)
if(hashed LESS 0 OR published LESS hashed)
  message(FATAL_ERROR "Upload scratch must authenticate before cache publication")
endif()

string(FIND "${source}" "const auto resetSending = i2p::modern::ScopeExit ([&piece] { piece.SetIsSending (false); });" unwind)
if(unwind LESS 0)
  message(FATAL_ERROR "Cached upload sending state must unwind on allocation/write failure")
endif()

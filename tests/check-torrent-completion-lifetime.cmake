# See the LICENSE file at the top of the project tree for copyright and
# license details.
#
# openi2pd: compile-free torrent completion lifetime contract.

if(NOT PROJECT_ROOT)
  message(FATAL_ERROR "PROJECT_ROOT is required")
endif()

file(READ "${PROJECT_ROOT}/libi2pd_client/TorrentsTunnel.cpp" tunnel)
string(FIND "${tunnel}" "void TorrentsTunnel::CompleteTorrent (" begin)
string(FIND "${tunnel}" "void TorrentsTunnel::ScheduleLocalVerification (" end)
if(begin LESS 0 OR end LESS_EQUAL begin)
  message(FATAL_ERROR "Torrent completion implementation boundaries missing")
endif()
math(EXPR length "${end} - ${begin}")
string(SUBSTRING "${tunnel}" ${begin} ${length} completion)

# Every nested disk/service callback borrows weak ownership and locks it
# during execution. Publication checks the original torrent instance,
# current selection and verification.
string(FIND "${completion}" "[this" raw_capture)
string(FIND "${completion}" "[self," strong_capture)
if(NOT raw_capture LESS 0 OR NOT strong_capture LESS 0)
  message(FATAL_ERROR "Torrent completion callbacks must carry weak ownership")
endif()
foreach(required IN ITEMS
    "weak_from_this ().lock ()"
    "const std::weak_ptr<TorrentsTunnel> weakSelf = self;"
    "auto self = weakSelf.lock ();"
    "const auto priorities = torrent->GetFilePriorities ();"
    "selectedFiles = std::move (selectedFiles)"
    "[weakSelf, torrent, hash, currentFile]"
    "[weakSelf, torrent, hash]"
    "[weakSelf, torrent, priorities]"
    "self->FindTorrent (torrent->GetInfoHash ()) != torrent"
    "priorities != torrent->GetFilePriorities ()"
    "|| !torrent->AreAllSelectedPiecesVerified ())"
    "if (self->m_Stopping || torrent->IsRemoved ()")
  string(FIND "${completion}" "${required}" found)
  if(found LESS 0)
    message(FATAL_ERROR "Torrent completion authority missing: ${required}")
  endif()
endforeach()

# The disk lambda uses the immutable selection, never the mutable priority
# vector. Its per-file verified-piece inspection remains deliberate.
string(FIND "${completion}" "selectedFiles = std::move (selectedFiles)" disk_begin)
string(SUBSTRING "${completion}" ${disk_begin} -1 disk_and_completions)
if(disk_and_completions MATCHES "torrent->GetFilePriority ")
  message(FATAL_ERROR "Disk completion must use the admitted file selection")
endif()

# The preceding verification chain crosses the same executors and must not
# revive a removed/replaced torrent or borrow the tunnel through raw this.
string(FIND "${tunnel}" "void TorrentsTunnel::VerifyNextLocalPiece (" verify_begin)
string(FIND "${tunnel}" "std::shared_ptr<Torrent> TorrentsTunnel::FindTorrent (" verify_end)
if(verify_begin LESS 0 OR verify_end LESS_EQUAL verify_begin)
  message(FATAL_ERROR "Torrent verification implementation boundaries missing")
endif()
math(EXPR verify_length "${verify_end} - ${verify_begin}")
string(SUBSTRING "${tunnel}" ${verify_begin} ${verify_length} verification)
string(FIND "${verification}" "[this" raw_verify_capture)
string(FIND "${verification}" "[self," strong_verify_capture)
if(NOT raw_verify_capture LESS 0 OR NOT strong_verify_capture LESS 0)
  message(FATAL_ERROR "Torrent verification callbacks must carry weak ownership")
endif()
foreach(required IN ITEMS
    "const std::weak_ptr<TorrentsTunnel> weakSelf = self;"
    "[weakSelf, torrent, hash = infoHash, index]"
    "[weakSelf, torrent, hash, index, verified]"
    "[weakSelf, torrent, hash]"
    "if (!self || self->m_Stopping || torrent->IsRemoved () || self->FindTorrent (hash) != torrent)"
    "torrent->EndLocalVerification ();"
    "self->VerifyNextLocalPiece (torrent);"
    "if (!torrent->IsComplete () && torrent->AreAllSelectedPiecesVerified ())")
  string(FIND "${verification}" "${required}" found)
  if(found LESS 0)
    message(FATAL_ERROR "Torrent verification authority missing: ${required}")
  endif()
endforeach()

# Only the service completion may inspect the mutable selection. Keep that
# decision after the disk job posts back, and reject the old disk snapshot.
string(FIND "${verification}" "bool complete = torrent->AreAllSelectedPiecesVerified ();" disk_selection)
if(NOT disk_selection LESS 0)
  message(FATAL_ERROR "Torrent verification must inspect selection on the service thread")
endif()

# See the LICENSE file at the top of the project tree for copyright
# and license details.
#
# Compile-free guards for runtime fixes and their regression wiring.
# These inspect source only and do not validate runtime behavior.

function(require_runtime_source path needle)
  file(READ "${PROJECT_ROOT}/${path}" source)
  string(FIND "${source}" "${needle}" position)
  if(position EQUAL -1)
    message(FATAL_ERROR "Missing runtime contract in ${path}: ${needle}")
  endif()
endfunction()

require_runtime_source("libi2pd/Destination.cpp"
  "streamRequestComplete.wait (l, [&done] { return done; });")
require_runtime_source("libi2pd/Destination.cpp"
  "GetService ().get_executor ().running_in_this_thread ()")
file(READ "${PROJECT_ROOT}/libi2pd/Destination.cpp" destination_sync)
if(destination_sync MATCHES "volatile bool done")
  message(FATAL_ERROR "Volatile cannot synchronize stream completion")
endif()
require_runtime_source("libi2pd_client/AddressBook.h"
  "std::atomic<bool> m_IsLoaded;")
require_runtime_source("libi2pd/Reseed.cpp"
  "cv.wait_for (lock, token, delay, [] { return false; });")
require_runtime_source("tests/CMakeLists.txt" "test-stream-creation-sync")
require_runtime_source("tests/test-stream-creation-sync.cpp"
  "destination->CreateStream (blinded)")
require_runtime_source("tests/test-stream-creation-sync.cpp"
  "destination->CreateStream (remote)")

require_runtime_source("libi2pd_client/TorrentsDHT.cpp"
  "if (RAND_bytes (m_NodeID.data () + 6, static_cast<int> (m_NodeID.size () - 6)) != 1)")
require_runtime_source("libi2pd_client/TorrentsDHT.cpp"
  "if (!IsInitialized ()) return;")
require_runtime_source("libi2pd_client/TorrentsTunnel.cpp"
  "if (candidate->IsInitialized ()) m_DHT = std::move (candidate);")

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

require_runtime_source("privsep/WindowsSupervisor.cpp"
  "WaitForSingleObject (m_Worker->process.Native (), 0)")
require_runtime_source("privsep/WindowsSupervisor.cpp"
  "GetExitCodeProcess (m_Worker->process.Native (), &exitCode)")
require_runtime_source("tests/test-tui-protocol.cpp"
  "std::fflush (stderr);")

file(READ "${PROJECT_ROOT}/privsep/WindowsIpc.cpp" windows_runtime_source)
if(windows_runtime_source MATCHES "WindowsChannel candidate \\(m_Pipe"
    OR windows_runtime_source MATCHES "candidate[.]Release \\(\\)")
  message(FATAL_ERROR "Accepted channels must not share listener ownership")
endif()
require_runtime_source("privsep/WindowsIpc.cpp"
  "WindowsChannel candidate (candidatePipe, true);")
require_runtime_source("privsep/WindowsIpc.cpp"
  "channel.Receive (timeoutMs)")
require_runtime_source("privsep/WindowsIpc.cpp"
  "std::min (WORKER_HELLO_TIMEOUT_MS, RemainingDeadlineMs (deadline))")
require_runtime_source("tests/test-privsep-windows-ipc.cpp"
  "TestMalformedHelloRearmsListener ()")

# Cancellation must reap overlapped I/O even when the event wait fails.
require_runtime_source("privsep/WindowsIpc.cpp"
  "const DWORD waitError = wait == WAIT_FAILED ? GetLastError () : ERROR_SUCCESS;")
require_runtime_source("privsep/WindowsIpc.cpp"
  "GetOverlappedResult (pipe, &overlapped, &transferred, TRUE)")
require_runtime_source("tests/test-router-storage.cpp" "#include \"FS.h\"")
require_runtime_source("tests/test-log-lifecycle.cpp" "#ifndef NOMINMAX")
require_runtime_source("tools/CMakeLists.txt" "find_package(Git 2.28 REQUIRED)")
require_runtime_source("libi2pd/Ed25519.cpp"
  "r.reset (ok ? DecodeBN<64> (digest) : nullptr);")
file(READ "${PROJECT_ROOT}/libi2pd/Ed25519.cpp" eddsa_nonce_source)
if(eddsa_nonce_source MATCHES "DecodeBN<32> \\(digest\\)")
  message(FATAL_ERROR "EdDSA nonce must use the entire SHA-512 output")
endif()
require_runtime_source("tests/test-eddsa.cpp"
  "assert (memcmp (output, sig, sizeof (output)) == 0);")
require_runtime_source("tests/test-torrent-bencode.cpp"
  "TestDictionaryValuesRequired ()")
require_runtime_source("libi2pd_client/TorrentsDHT.h"
  "std::mutex persistenceMutex;")
require_runtime_source("libi2pd_client/TorrentsDHT.cpp"
  "auto nextState = std::make_shared<DHTServiceState> ();")
require_runtime_source("tests/test-torrents-dht-protocol.cpp"
  "TestPersistenceGeneration ()")

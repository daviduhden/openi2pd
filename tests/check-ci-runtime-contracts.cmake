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

# Profile state and recycler authority must survive cross-TU teardown.
require_runtime_source("libi2pd/Profiling.cpp"
  "static ProfilingRuntime runtime;")
foreach(producer NetDb.cpp Transports.cpp Tunnel.cpp RouterContext.cpp)
  require_runtime_source("libi2pd/${producer}" "PrepareProfilesRuntime ();")
endforeach()
require_runtime_source("libi2pd/NetDb.hpp"
  "[pool] (RouterProfile * profile) noexcept { pool->ReleaseMt (profile); }")
require_runtime_source("tests/CMakeLists.txt" "test-profile-static-lifetime")

file(READ "${PROJECT_ROOT}/libi2pd/Transports.cpp" transport_redaction)
if(transport_redaction MATCHES "LogPrint[^;]*(ntcp2proxy|ssu2proxy)")
  message(FATAL_ERROR "Proxy configuration operands must never enter logs")
endif()
foreach(url_source libi2pd/Reseed.cpp libi2pd_client/AddressBook.cpp
    libi2pd_client/HTTPProxy.cpp)
  file(READ "${PROJECT_ROOT}/${url_source}" url_logging)
  if(url_logging MATCHES "LogPrint[^;]*(m_Link|m_OutproxyUrl)")
    message(FATAL_ERROR "Credential-bearing source URL in log: ${url_source}")
  endif()
  if(url_source STREQUAL "libi2pd/Reseed.cpp"
      AND url_logging MATCHES "LogPrint[^;]*,[ \t\r\n]*(address|url)[ \t\r\n]*[),]")
    message(FATAL_ERROR "Raw reseed source operand in log")
  endif()
endforeach()

# Fixture authority must cover deferred completion and final teardown.
require_runtime_source("tests/test-streaming-sendstate.cpp"
  "std::shared_ptr<SendResult> result")
require_runtime_source("tests/test-streaming-sendstate.cpp"
  "assert (pending.expired ());")

require_runtime_source("tests/test-i2ptunnel-stop.cpp"
  "assert (released.expired ());")
require_runtime_source("tests/test-i2ptunnel-targets.cpp"
  "assert (released.expired ());")

require_runtime_source("tests/test-torrents-security.cpp"
  "auto diskWork = boost::asio::make_work_guard (diskService);")

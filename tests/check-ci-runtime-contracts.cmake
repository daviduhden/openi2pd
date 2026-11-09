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
  "channel.ReceiveUntil (deadline)")
require_runtime_source("privsep/WindowsIpc.cpp"
  "std::min (deadline, DeadlineAfter (WORKER_HELLO_TIMEOUT_MS))")
require_runtime_source("privsep/WindowsIpc.cpp"
  "WaitForConnectionUntil (deadline)")
require_runtime_source("privsep/WindowsIpc.cpp"
  "overlapped.hEvent, RemainingDeadlineMs (deadline)")
require_runtime_source("privsep/WindowsIpc.cpp"
  "return TerminateAndWaitUntil (deadline);")
require_runtime_source("privsep/WindowsSupervisor.cpp"
  "m_Channel->SendUntil ({ MessageType::Shutdown, 0, { } }, sendDeadline)")
require_runtime_source("privsep/WindowsSupervisor.cpp"
  "m_Worker->StopUntil (deadline)")
require_runtime_source("tests/test-privsep-windows-ipc.cpp"
  "auto expired = accepted->ReceiveUntil (")
require_runtime_source("tests/test-privsep-windows-ipc.cpp"
  "accepted->ReceiveUntil ((WindowsDeadline::max) ())")
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
require_runtime_source("tests/test-privsep-linux-ipc.cpp"
  ".requireGid = true, .configured = true")
require_runtime_source("tests/test-privsep-linux-ipc.cpp"
  "assert (!pair->second.MatchesPeerExpectation (uid, gid + 1, 0));")

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

# Registry tests derive unknown types from the authoritative boundary.
require_runtime_source("tests/test-privsep-linux-ipc.cpp"
  "static_cast<uint16_t> (MAX_MESSAGE_TYPE + 1)")
require_runtime_source("tests/test-privsep-linux-ipc.cpp"
  "assert (!IsKnownMessageType (type));")
file(READ "${PROJECT_ROOT}/tests/test-privsep-linux-ipc.cpp" ipc_fixture)
if(ipc_fixture MATCHES "BuildHeader \\(7,")
  message(FATAL_ERROR "Registered IPC type 7 is not an unknown-type fixture")
endif()
require_runtime_source("tests/test-torrent-bencode.cpp" "TestKnownFieldTypes ()")
require_runtime_source("libi2pd_client/Torrents.cpp"
  "if (!l || announce.size () > MAX_TRACKER_ANNOUNCE_LENGTH)")

# Provider failure never publishes clock-derived seeds or peer IDs.
require_runtime_source("libi2pd/util.cpp" "return detail::RequireRandomSeed")
require_runtime_source("libi2pd/util.h"
  "throw std::runtime_error (\"CSPRNG seed unavailable\");")
file(READ "${PROJECT_ROOT}/libi2pd/util.cpp" seed_source)
string(FIND "${seed_source}" "uint_fast32_t GetRngSeed ()" seed_start)
string(FIND "${seed_source}" "\n\tnamespace net" seed_end)
if(seed_start LESS 0 OR seed_end LESS seed_start)
  message(FATAL_ERROR "Cannot inspect provider seed boundary")
endif()
math(EXPR seed_length "${seed_end} - ${seed_start}")
string(SUBSTRING "${seed_source}" ${seed_start} ${seed_length} seed_body)
if(seed_body MATCHES "GetMonotonic|GetMillisecondsSinceEpoch|std::chrono")
  message(FATAL_ERROR "Provider seed generation must not fall back to clocks")
endif()
require_runtime_source("tests/test-rng-seed.cpp" "assert (zero == 0 && calls == 1);")
require_runtime_source("tests/test-rng-seed.cpp" "assert (seeds.size () > 1);")
require_runtime_source("tests/test-torrents-verify-parity.cpp" "TestPeerIDProviderFailure ()")
require_runtime_source("libi2pd_client/Torrents.cpp"
  "if (!m_Torrent || !m_Stream || !m_Torrent->HasPeerID ()) return;")
require_runtime_source("libi2pd_client/Torrents.h"
  "m_Error.store (HasPeerID () ? error : eTorrentErrorRandomSource, std::memory_order_release);")
require_runtime_source("libi2pd_client/TorrentsTunnel.cpp"
  "if (!torrent || !torrent->HasPeerID () || !IsTorrentTracker (trackerID, *torrent)) return false;")

# Reject bare calls; conditional continuations need semantic review.
if(POLICY CMP0009)
  cmake_policy(SET CMP0009 NEW)
endif()
file(GLOB_RECURSE provider_sources
  "${PROJECT_ROOT}/libi2pd/*.cpp" "${PROJECT_ROOT}/libi2pd/*.h"
  "${PROJECT_ROOT}/libi2pd/*.hpp" "${PROJECT_ROOT}/libi2pd_client/*.cpp"
  "${PROJECT_ROOT}/libi2pd_client/*.h")
foreach(provider_path IN LISTS provider_sources)
  file(READ "${provider_path}" provider_text)
  if(provider_text MATCHES "(^|\n)[ \t]*RAND_(priv_)?bytes[ \t]*\\([^;\n]*\\)[ \t]*;")
    message(FATAL_ERROR "Unchecked provider statement: ${provider_path}")
  endif()
endforeach()
require_runtime_source("libi2pd/Random.h" "OPENSSL_cleanse (bytes.data (), bytes.size ());")
require_runtime_source("libi2pd/Random.h" "return detail::RandomBelowWith (bound, [] { return RandomUInt32 (); });")
require_runtime_source("libi2pd/TunnelPool.cpp" "auto retireUnsentTest = i2p::modern::ScopeExit")
require_runtime_source("libi2pd/TunnelPool.cpp" "OPENSSL_cleanse (key, sizeof (key));")
require_runtime_source("libi2pd/Tunnel.cpp" "auto abortBuild = i2p::modern::ScopeExit")
require_runtime_source("libi2pd/Tunnel.cpp" "retired = std::move (entry->second);")
require_runtime_source("libi2pd/TunnelConfig.cpp" "m_Hops.push_back (std::move (hop));")
require_runtime_source("libi2pd/NetDb.cpp" "if (!randomIdent.Randomize ())")
require_runtime_source("libi2pd/RouterContext.cpp"
  "OPENSSL_cleanse (keys.get (), sizeof (NTCP2PrivateKeys));")
require_runtime_source("libi2pd/RouterContext.cpp"
  "OPENSSL_cleanse (keys.get (), sizeof (SSU2PrivateKeys));")
require_runtime_source("libi2pd/SSU2Session.cpp"
  "auto discardIncompleteHandshake = i2p::modern::ScopeExit")
require_runtime_source("libi2pd/SSU2Session.cpp"
  "discardIncompleteHandshake.dismiss ();")
file(READ "${PROJECT_ROOT}/libi2pd/RouterContext.cpp" router_randomness)
if(router_randomness MATCHES "LogPrint[^;]*replyToken")
  message(FATAL_ERROR "Router publication tokens must not enter logs")
endif()

function(require_runtime_region path begin end token)
  file(READ "${PROJECT_ROOT}/${path}" region_source)
  string(FIND "${region_source}" "${begin}" begin_at)
  string(FIND "${region_source}" "${end}" end_at)
  if(begin_at LESS 0 OR end_at LESS begin_at)
    message(FATAL_ERROR "Cannot inspect runtime region in ${path}")
  endif()
  math(EXPR region_size "${end_at} - ${begin_at}")
  string(SUBSTRING "${region_source}" ${begin_at} ${region_size} region)
  string(FIND "${region}" "${token}" token_at)
  if(token_at LESS 0)
    message(FATAL_ERROR "Missing bounded runtime region contract in ${path}")
  endif()
endfunction()
require_runtime_region("libi2pd/SSU2Session.cpp"
  "bool SSU2Session::SendSessionRequest (uint64_t token)"
  "bool SSU2Session::ProcessSessionRequest (Header & header"
  "m_SentHandshakePacket.reset ();\n\t\tauto discardIncompleteHandshake")
require_runtime_region("libi2pd/SSU2Session.cpp"
  "bool SSU2Session::SendSessionCreated (const uint8_t * X)"
  "bool SSU2Session::ProcessSessionCreated (uint8_t * buf"
  "m_SentHandshakePacket.reset ();\n\t\tauto discardIncompleteHandshake")
require_runtime_source("libi2pd/SSU2Session.cpp"
  "// Retire the previous attempt only after Retry validation.")
require_runtime_source("libi2pd/Tunnel.cpp"
  "std::shared_ptr<Tunnel> retired;\n\t\t\t{\n\t\t\t\tstd::lock_guard<std::mutex> lock (m_PendingTunnelsMutex);")
require_runtime_source("libi2pd/Tag.h" "[[nodiscard]] bool Randomize ()")
require_runtime_source("libi2pd/Tag.h"
  "if (RAND_bytes (m_Buf, sz) == 1) return true;\n\t\t\tOPENSSL_cleanse (m_Buf, sz);")
require_runtime_source("tests/test-torrents-verify-parity.cpp" "TestExtendedHandshakeFieldTypes ()")
require_runtime_source("tests/test-torrents-verify-parity.cpp" "TestTrackerKnownFieldTypes ()")
require_runtime_source("tests/test-torrents-security.cpp"
  "entry.as_object ().at (\"error\").as_int64 () == (populated ? 3 : 0)")

# Source order guards supplement the behavioral regressions in manual CI.
require_runtime_source(".github/workflows/build-windows.yml"
  "msys_perl=$(cygpath -m /usr/bin/perl.exe)")
require_runtime_source(".github/workflows/build-windows.yml"
  [=[-DPERL_EXECUTABLE="$msys_perl"]=])
require_runtime_source("tools/CMakeLists.txt"
  [=[COMMAND "${PERL_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/i2pdctl.t"]=])
file(READ "${PROJECT_ROOT}/tools/CMakeLists.txt" perl_test_graph)
if(perl_test_graph MATCHES "PROVE_EXECUTABLE")
  message(FATAL_ERROR "TAP test must not select an unrelated prove interpreter")
endif()
function(require_runtime_order path begin end)
  file(READ "${PROJECT_ROOT}/${path}" source)
  string(FIND "${source}" "${begin}" first)
  string(FIND "${source}" "${end}" last)
  if(first LESS 0 OR last LESS first)
    message(FATAL_ERROR "Cannot bound ordered runtime contract: ${path}")
  endif()
  math(EXPR length "${last} - ${first}")
  string(SUBSTRING "${source}" ${first} ${length} remaining)
  foreach(token IN LISTS ARGN)
    string(FIND "${remaining}" "${token}" position)
    if(position LESS 0)
      message(FATAL_ERROR "Runtime ordering broken in ${path}: ${token}")
    endif()
    string(LENGTH "${token}" token_length)
    math(EXPR next "${position} + ${token_length}")
    string(SUBSTRING "${remaining}" ${next} -1 remaining)
  endforeach()
endfunction()

require_runtime_order("privsep/Channel.cpp"
  "if (received == 0)" "if (++emptyRecords > 8)"
  "if (receivedFd >= 0)" "peer closed the channel"
  "missing SCM_CREDENTIALS on an empty record")
require_runtime_source("tests/test-privsep-linux-ipc.cpp"
  "afterLast.error ().find (\"closed\")")
require_runtime_source("tests/test-privsep-linux-ipc.cpp"
  "unauthenticated.error ().find (\"expected peer\")")
require_runtime_order("libi2pd/SSU2Session.cpp"
  "bool SSU2Session::ProcessRetry (" "static bool ValidateHolePunchDateTime"
  "AEADChaCha20Poly1305 (" "HasDateTimeFirstBlock ("
  "ValidateSSU2PayloadBlocks (eSSU2SessionStateTokenReceived"
  "if (versionChanged)"
  "HandlePayload (payload" "if (m_IsInvalidMessage)"
  "if (m_ReceivedTermination)" "if (!token)"
  "m_Server.UpdateOutgoingToken (" "m_SentHandshakePacket.reset ();")
require_runtime_source("tools/keygen.cpp" "catch (const std::exception &)")
require_runtime_order("tools/keygen.cpp" "int main (" "const auto & keys"
  "CreateRandomKeys (type)" "catch (const std::exception &)" "return 2;")
require_runtime_source("tools/test-tools-regressions.cmake"
  "foreach(signature RSA-2048-SHA256 RSA-3072-SHA384 RSA-4096-SHA512 4 5 6)")
require_runtime_source("tests/test-torrents-tracker-schedule.cpp"
  "torrent.GetTrackerError (6) == \"malformed response\"")
require_runtime_source("libi2pd_client/Torrents.cpp"
  "if (!len || hashes.size () % i2p::data::IdentHash::len != 0) return 0;")
require_runtime_order("libi2pd_client/Torrents.cpp"
  "void Torrent::HandleDatagramTrackerResponse (" "void Torrent::SetTrackerError ("
  "if ((!hashes && hashesLen) || hashesLen % i2p::data::IdentHash::len != 0)"
  "ReportTrackerFailure (trackerID, \"malformed response\")"
  "CheckTrackerStatsSize (trackerID)" "peers.clear ();")
require_runtime_source("tests/test-torrents-verify-parity.cpp" "TestCompactTrackerFraming ()")
require_runtime_order("privsep/Channel.cpp"
  "Channel::ReceiveWithFd (int timeoutMs)" "Channel::Credentials () const"
  "SCM_CREDENTIALS do not match the expected peer"
  "auto message = Decode (record);" "RejectFrame (offending,")
require_runtime_source("privsep/Channel.cpp"
  "return infinite ? capped : std::min (deadline, capped);")
require_runtime_source("privsep/Channel.cpp"
  "message.error ().text, refusalDeadline ())) continue;")
require_runtime_source("privsep/Channel.cpp"
  "descriptorFault, refusalDeadline ()))")
require_runtime_source("privsep/Channel.cpp"
  "}, -1, false, deadline)")
require_runtime_order("privsep/Channel.cpp"
  "Channel::SendWithFdUntil (" "bool Channel::RejectFrame ("
  "PollTimeout (infinite, deadline)" "::sendmsg (")
file(READ "${PROJECT_ROOT}/privsep/Channel.cpp" channel_deadline_source)
if(channel_deadline_source MATCHES "refusalTimeout")
  message(FATAL_ERROR "Nested refusal sends must retain absolute deadlines")
endif()
string(FIND "${channel_deadline_source}" "Channel::SendWithFdUntil (" absolute_send_begin)
string(FIND "${channel_deadline_source}" "bool Channel::RejectFrame (" absolute_send_end)
math(EXPR absolute_send_length "${absolute_send_end} - ${absolute_send_begin}")
string(SUBSTRING "${channel_deadline_source}" ${absolute_send_begin}
  ${absolute_send_length} absolute_send_source)
if(absolute_send_source MATCHES "MakeDeadline|timeoutMs")
  message(FATAL_ERROR "Absolute channel sends must not rebase their deadline")
endif()
require_runtime_source("tests/test-privsep-linux-ipc.cpp" "TestAuthenticateBeforeRefusal ()")
require_runtime_source("tests/test-privsep-linux-ipc.cpp" "TestRefusalSharesReceiveDeadline ()")
require_runtime_source("libi2pd/TunnelPool.cpp" "entry->second.authority == authority")
require_runtime_source("libi2pd/TunnelPool.cpp" "m_Tests.emplace (msgID, test).second")
require_runtime_order("libi2pd/TunnelPool.cpp"
  "void TunnelPool::HandleTunnelTestDrop (" "void TunnelPool::ManageTunnels ("
  "RetireTunnelTest (msgID, authority)" "if (!retired || !retired->outbound) return;"
  "retired->outbound->SetState (eTunnelStateFailed);")
require_runtime_source("tests/test-tunnelpool-test-timing.cpp" "TestStaleDropCannotRetireReplacement ()")

# Reject signed negative bit indices before conversion or buffer access.
require_runtime_order("libi2pd/Tag.h"
  "uint8_t GetBit (int i) const" "private:"
  "if (i < 0) return 0;" "const size_t pos = static_cast<size_t> (i) / 8;"
  "if (pos >= sz) return 0;" "return m_Buf[pos] &")
require_runtime_source("tests/test-kad-dht.cpp" "TestTagBitBounds ();")
require_runtime_source("tests/test-kad-dht.cpp"
  "hash.GetBit (std::numeric_limits<int>::min ())")

# Exercise both capability-selected and explicit mutex shared ownership.
require_runtime_source("tests/CMakeLists.txt" "test-atomic-shared-ptr")
require_runtime_source("tests/test-atomic-shared-ptr.cpp"
  "i2p::util::AtomicSharedPtr<T, false>")
require_runtime_source("tests/test-atomic-shared-ptr.cpp"
  "TestStoreReleasesOutsideLock<Storage> ()")
require_runtime_source("tests/test-atomic-shared-ptr.cpp"
  "TestFailedCompareExchangeReleasesOutsideLock<Storage> ()")
require_runtime_source(".github/workflows/build-windows.yml"
  "build-msvc/tests/Release/test-atomic-shared-ptr.exe")
require_runtime_source(".github/workflows/build-windows.yml"
  "|atomic-shared-ptr|kad-dht|release-assertions))$")
require_runtime_source(".github/workflows/build-windows.yml"
  "build-msvc/tests/Release/test-kad-dht.exe")

require_runtime_order("privsep/WindowsIpc.cpp"
  "WindowsChannel::ReceiveUntil (WindowsDeadline deadline)" "WindowsChannel::Peer () const"
  "const auto offending = static_cast<MessageType> (inspection.rawType);"
  "std::min (deadline, DeadlineAfter (250))"
  "EncodeError (decoded->type, ErrorCode::NotAuthorized, refusal)"
  "std::min (deadline, DeadlineAfter (250))")
require_runtime_source("privsep/WindowsSupervisor.cpp"
  "m_Worker->WaitUntil (gracefulDeadline)")
require_runtime_source("privsep/WindowsIpc.cpp"
  "WindowsWorkerProcess::WaitUntil (WindowsDeadline deadline) const")
require_runtime_order("libi2pd/Tag.h"
  "std::string ToBase64 (size_t length = sz) const"
  "std::string ToBase32 (size_t length = sz) const"
  "if (length > sz) throw std::length_error"
  "ByteStreamToBase64 (m_Buf, length)")
require_runtime_order("libi2pd/Tag.h"
  "std::string ToBase32 (size_t length = sz) const"
  "size_t FromBase32 (std::string_view s)"
  "if (length > sz) throw std::length_error"
  "ByteStreamToBase32 (m_Buf, length)")
require_runtime_source("tests/test-kad-dht.cpp" "TestTagEncodingBounds ();")

# A conservative writable threshold must not suppress the first send.
require_runtime_order("privsep/Channel.cpp"
  "bool waitForSpace = false;" "bool Channel::RejectFrame"
  "if (waitForSpace)" "::poll (&pfd" "::sendmsg (m_Fd"
  "if (error == EAGAIN || error == EWOULDBLOCK)"
  "waitForSpace = true;")
require_runtime_order("privsep/OpenBSDImsg.cpp"
  "bool attempted = false;" "ImsgChannel::Receive (int timeoutMs)"
  "if (attempted && !infinite && DeadlineExpired (deadline))"
  "if (imsgbuf_write (m_Buffer) != -1) continue;" "::poll (&pfd")
require_runtime_source("tests/test-privsep-linux-ipc.cpp"
  "MessageType::LogRecord, 1000, resumedPayload }, 0)")
require_runtime_source("tests/test-privsep-linux-ipc.cpp"
  "assert (immediate.error () == \"send timeout\");")
require_runtime_source("tests/test-privsep-openbsd-imsg.cpp"
  "MessageType::Ping, 9, { } }, -1, 0)")

# Copied acceptors must reject a destroyed or restarted torrent owner.
require_runtime_order("libi2pd_client/TorrentsTunnel.cpp"
  "void TorrentsTunnel::Stop ()" "void TorrentsTunnel::StopOnService ()"
  "RevokeInboundAccepts ();" "if (m_Stopping.load")
require_runtime_order("libi2pd_client/TorrentsTunnel.cpp"
  "void TorrentsTunnel::RevokeInboundAccepts ()"
  "void TorrentsTunnel::Accept ()"
  "m_InboundAcceptStopped = true;"
  "if (m_InboundAcceptStopped) return { };"
  "return [owner, generation]" "auto self = owner.lock ();"
  "admission (generation->mutex);" "!generation->active"
  "self->AddHandler (connection);" "connection->ReceiveHandshake ();")
require_runtime_source("libi2pd_client/TorrentsTunnel.h"
  "bool m_InboundAcceptStopped = true;")
require_runtime_source("tests/test-torrents-security.cpp"
  "TestInboundAcceptAuthority ();")
require_runtime_source("tests/test-torrents-security.cpp"
  "assert (released.expired ());")

# One broker deadline covers admission, output and stale-reply waits.
require_runtime_order("privsep/Worker.cpp"
  "void ChildProcess::Stop (int timeoutMs)" "static i2p::expected<ChildProcess"
  "const ChildStopBudget budget" "SendUntil (shutdown, budget.send)"
  "WaitForExitUntil (budget.graceful)" "WaitForExitUntil (budget.terminate)"
  "WaitForExitUntil (budget.reap)")
require_runtime_source("privsep/Worker.h"
  "const int polled = ::poll (&pfd, 1, waitMs);")
require_runtime_order("privsep/Worker.cpp"
  "std::optional<WorkerHelloRecord> ReadWorkerHello" "constexpr size_t MAX_EXECUTABLE_PATH_SIZE"
  "const auto deadline =" "statusFlags | O_NONBLOCK"
  "while (done < buffer.size ())"
  "std::chrono::steady_clock::now () >= deadline" "::read (fd,")
require_runtime_source("tests/CMakeLists.txt" "test-child-stop-budget")
require_runtime_source("tests/test-privsep-log.cpp"
  "const auto sent = filler.Send (queued, 0);")
require_runtime_source("privsep/Supervisor.cpp"
  "DeadlineMutex storageSupervisorMutex;")
require_runtime_source("privsep/DeadlineMutex.h"
  "m_Changed.wait_until (lock, deadline,")
require_runtime_source("tests/CMakeLists.txt" "test-deadline-mutex")
require_runtime_source("tests/test-deadline-mutex.cpp"
  "TestMutualExclusion ();")
require_runtime_source("libi2pd_client/I2PService.cpp"
  "try { previous->timer.cancel (); }")
require_runtime_order("privsep/Supervisor.cpp"
  "SpawnSupervisedTorrentStorage (int rootFd" "auto & reply = *selected;"
  "const auto deadline =" "lock.try_lock_until (deadline)"
  "channel.SendUntil (" "std::chrono::steady_clock::now () >= deadline"
  "channel.ReceiveWithFdUntil (deadline)")
require_runtime_source("tests/test-privsep-linux-ipc.cpp"
  "TestTransactionDeadline ();")
require_runtime_source("libi2pd_client/I2PService.cpp"
  "if (generation->wait != wait) return;")
require_runtime_source("libi2pd_client/I2PService.cpp"
  "selected.swap (generation->callbacks);")
require_runtime_source("tests/CMakeLists.txt" "test-i2p-service-readiness")
require_runtime_source("tests/CMakeLists.txt" "test-tunnel-destination-cache")
require_runtime_source("tests/CMakeLists.txt" "test-destination-leaseset-session")
require_runtime_order("libi2pd/Destination.cpp"
  "const uint8_t storeType =" "auto makeLeaseSet ="
  "NETDB_STORE_TYPE_LEASESET && from" "return;" "switch (storeType)")

# Alphabet views carry their bounds through every shard initializer.
require_runtime_source("libi2pd/Base.h"
  "std::span<const char, 32> GetBase32SubstitutionTable ()")
require_runtime_source("libi2pd/Base.h"
  "std::span<const char, 64> GetBase64SubstitutionTable ()")
require_runtime_source("libi2pd/FS.cpp"
  "bool HashedStorage::Init (std::span<const char> chars)")
require_runtime_source("tests/test-base-64.cpp"
  "decltype (GetBase32SubstitutionTable ())::extent == 32")

# Initial discovery cannot race persistence or reenter twice.
require_runtime_order("libi2pd_client/TorrentsTunnel.cpp"
  "bool TorrentsTunnel::DispatchInitialDiscovery ("
  "void TorrentsTunnel::DiscoverAddedTorrent ("
  "if (!CanDiscoverTorrent (torrent)) return false;"
  "state->second.initialDiscoveryPending = false;"
  "if (trackers) trackers ();"
  "!torrent->IsPrivate () && dht")
require_runtime_source("libi2pd_client/TorrentsTunnel.cpp"
  "state->second.initialDiscoveryOwner.lock () == torrent")
require_runtime_source("libi2pd_client/TorrentsTunnel.cpp"
  "&& !torrent->IsLocalVerificationRequested ()")
require_runtime_source("tests/test-torrents-security.cpp"
  "TestMagnetDiscoveryWaitsForDurability ();")
require_runtime_source("tests/test-torrents-security.cpp"
  "TestInitialDiscoveryAuthority ();")

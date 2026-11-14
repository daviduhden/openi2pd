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
  "accepted->ReceiveUntil ((WindowsDeadline::max)())")
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
require_runtime_source(".github/workflows/build-windows.yml"
  "msys_git=$(cygpath -m /usr/bin/git.exe)")
require_runtime_source(".github/workflows/build-windows.yml"
  [=[-DGIT_EXECUTABLE="$msys_git"]=])
require_runtime_source(".github/workflows/build-windows.yml"
  "MSYS: winsymlinks:nativestrict")
require_runtime_source("tests/agent_knowledge_validator/test_validator.t"
  "my $observed = -l $link ? readlink($link) : undef;")
require_runtime_source(".github/workflows/build-windows.yml"
  "build-output/startup-images")
require_runtime_source("tools/CMakeLists.txt"
  [=[COMMAND "${PERL_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/i2pdctl.t"]=])
file(READ "${PROJECT_ROOT}/tools/CMakeLists.txt" perl_test_graph)
if(perl_test_graph MATCHES "PROVE_EXECUTABLE")
  message(FATAL_ERROR "TAP test must not select an unrelated prove interpreter")
endif()
require_runtime_source("tests/test-privsep-worker-identity.cpp"
  "SkipCapability (\"worker identity requires a non-root runner\")")
require_runtime_source("tests/test-privsep-credentials.cpp"
  "SkipCapability (\"worker credentials require a non-root runner\")")
require_runtime_source("tests/test-torrent-storage-openbsd.cpp"
  "exec-free OpenBSD storage supervisor provisioning is not implemented")
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
  "message.error ().text, refusalDeadline ()))\n\t\t\t\t\t\tcontinue;")
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
  "|atomic-shared-ptr|kad-dht|eddsa|signature-identity-hardening|crypto-audit-bounds|signed-data-bounds|command-line-policy|release-assertions)|command-line-policy-rejections|i2pd-command-line-policy)$")
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
  "try\n\t\t\t{\n\t\t\t\tprevious->timer.cancel ();\n\t\t\t}")
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
require_runtime_source("libi2pd_client/I2PService.cpp"
  "if (ec || !m_Running.load () || !wait || m_IdleWait != wait) return;")
require_runtime_source("libi2pd_client/I2PService.cpp"
  "m_IdleCheckTimer->async_wait (\n\t\t\t[owner, wait]")
require_runtime_source("libi2pd_client/I2PService.cpp"
  "return now > latest ? Timer::time_point::max () : now + delay;")
require_runtime_source("libi2pd/Garlic.h" "std::atomic<bool> m_IsIdling;")
require_runtime_source("tests/test-i2p-service-readiness.cpp"
  "TestIdleWaitRevocationAndRestart ();")
require_runtime_source("tests/test-i2p-service-readiness.cpp"
  "TestIdleTimerDoesNotRetainOwnerOrDestination ();")
require_runtime_source("tests/test-i2p-service-readiness.cpp"
  "TestIdleDeadlineSaturation ();")
require_runtime_source("tests/test-i2p-service-readiness.cpp"
  "TestFreshActivityRearmsIdleAuthority ();")
require_runtime_order("libi2pd_client/I2PService.cpp"
  "void I2PService::UpdateLastActivityTime ()" "void I2PService::Resume ()"
  "std::lock_guard idle (m_IdleMutex);"
  "m_LastActivityTime = i2p::util::GetMonotonicMilliseconds ();"
  "resume = m_LocalDestination->IsIdling ();" "if (resume) Resume ();")
require_runtime_source("tests/test-i2p-service-readiness.cpp"
  "TestDestinationIdleStateAcrossThreads ();")
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

# Correlation keys can be reused while successful callbacks
# remain queued.
foreach(needle IN ITEMS
    "[weakSelf, req, torrent, key, authority, event]"
    "[weakSelf, httpStream, torrent, req, key, authority]"
    "[weakSelf, httpStream, torrent, buf, parser, key, authority]"
    "[weakSelf, key, authority]"
    "if (!wasInFlight) return;")
  require_runtime_source("libi2pd_client/TorrentsTunnel.cpp" "${needle}")
endforeach()
file(READ "${PROJECT_ROOT}/libi2pd_client/TorrentsTunnel.cpp" tracker_callbacks)
foreach(needle IN ITEMS "m_TrackerRequests.Contains (key)"
    "m_TrackerRequests.Find (key)" "m_TrackerRequests.Release (key)")
  string(FIND "${tracker_callbacks}" "${needle}" stale_tracker_key)
  if(NOT stale_tracker_key EQUAL -1)
    message(FATAL_ERROR "Tracker callbacks must match admission authority: ${needle}")
  endif()
endforeach()
require_runtime_source("tests/test-torrents-tracker-dispatch.cpp"
  "TestInflightAuthorityAfterReuse ();")

# Loader and authenticated lifecycle failures differ from
# runner capability limits.
file(READ "${PROJECT_ROOT}/tests/test-privsep-windows-supervisor.cpp" worker_fixture)
string(FIND "${worker_fixture}" "int TestLiveWorker ()" live_begin)
string(FIND "${worker_fixture}" "void TestServiceDrainContract ()" live_end)
if(live_begin LESS 0 OR live_end LESS_EQUAL live_begin)
  message(FATAL_ERROR "Missing live-worker outcome review boundary")
endif()
math(EXPR live_length "${live_end} - ${live_begin}")
string(SUBSTRING "${worker_fixture}" ${live_begin} ${live_length} live_body)
if(live_body MATCHES "return 77|assert *\\(|RequireSnapshot")
  message(FATAL_ERROR "Live-worker failures must drain cleanup and fail, not skip")
endif()
require_runtime_source("tests/test-privsep-windows-supervisor.cpp"
  "ClassifyJobAssignment (0xc0000142u, true) == FixtureOutcome::Failed")
require_runtime_source("tests/test-privsep-windows-supervisor.cpp"
  "const int outcome = FixtureExitCode (killOnClose, terminate, live);")

# Compatibility derivation cannot authorize an unrelated supplied key.
require_runtime_source("libi2pd/Signature.cpp"
  "if (CRYPTO_memcmp (m_PublicKeyEncoded, signingPublicKey,")
require_runtime_source("libi2pd/Signature.cpp"
  "if (fallback->IsValid ()) m_Fallback = std::move (fallback);")
require_runtime_source("libi2pd/Identity.cpp"
  "if (signer->IsValid ())\n\t\t\t\tm_Signer = std::move (signer);")
require_runtime_source("libi2pd/Signature.h"
  "{ return GetPkey () != nullptr; }")
require_runtime_source("tests/test-signature-identity-hardening.cpp"
  "TestEddsaFallbackPairValidation ();")
require_runtime_source(".github/workflows/build-windows.yml"
  "test-eddsa test-signature-identity-hardening")
require_runtime_source(".github/workflows/build-windows.yml"
  "|eddsa|signature-identity-hardening|")

# Outbound run admission must also work without inbound registration.
foreach(needle IN ITEMS "StartPeerDials ();"
    "return [owner, generation, torrent, peer, origin, dial]"
    "pending->second.torrent.lock () != torrent"
    "self->FindTorrent (torrent->GetInfoHash ()) == torrent")
  require_runtime_source("libi2pd_client/TorrentsTunnel.cpp" "${needle}")
endforeach()
require_runtime_source("tests/test-torrents-security.cpp"
  "TestPeerDialAuthority ();")
string(FIND "${tracker_callbacks}" "[this, torrent, peer, origin, dial]" raw_dial)
if(NOT raw_dial EQUAL -1)
  message(FATAL_ERROR "Peer dial completion must retain explicit owner authority")
endif()

# The complete signing hierarchy must express its inherited contracts.
file(READ "${PROJECT_ROOT}/libi2pd/Signature.h" signature_contracts)
if(signature_contracts MATCHES
    "(void Sign|bool Verify|void SetPublicKey|size_t GetPublicKeyLen|size_t GetSignatureLen) [(][^)]*[)]( const)?[ \t\r\n]*[;{]")
  message(FATAL_ERROR "Signature overrides must be annotated in every backend")
endif()
require_runtime_source("tests/test-signature-identity-hardening.cpp"
  "BN_bin2bn (encoded.data (), encodedSize, nullptr)")
require_runtime_source("tests/test-routerinfo-rsa-reject.cpp"
  "constexpr int RSA_BYTES = RSA_BITS / 8;")
require_runtime_source("tests/test-routerinfo-rsa-reject.cpp"
  "BN_bin2bn (key.modulus.data (), RSA_BYTES, nullptr)")
require_runtime_source("libi2pd/Signature.h"
  "static constexpr int componentLen = static_cast<int> (keyLen / 2);")

# Encoding and normalization failures must revoke signing readiness.
require_runtime_source("libi2pd/Ed25519.h"
  "[[nodiscard]] bool EncodePublicKey (")
require_runtime_source("libi2pd/Signature.cpp"
  "if (!GetEd25519 ()->EncodePublicKey (")
require_runtime_source("libi2pd/Signature.h"
  "if (!signer.IsValid ())")
require_runtime_source("libi2pd/Identity.cpp"
  "std::make_unique<i2p::crypto::RedDSA25519Signer> (priv)")
require_runtime_order("libi2pd/Ed25519.cpp"
  "if (!ctx || !p.x || !p.y) return { };" "BN_mod_inverse (normalized.y")
require_runtime_source("tests/test-eddsa.cpp"
  "assert (!ed25519->EncodePublicKey (identity, encoded, nullptr));")

# Exceptions after handle or transaction admission retire authority.
file(READ "${PROJECT_ROOT}/libi2pd_client/TorrentStorageWorker.cpp" storage_exception_source)
function(require_storage_unwind_guard begin end)
  string(FIND "${storage_exception_source}" "${begin}" start)
  if(start LESS 0)
    message(FATAL_ERROR "Missing storage exception review boundary: ${begin}")
  endif()
  string(SUBSTRING "${storage_exception_source}" ${start} -1 tail)
  string(FIND "${tail}" "${end}" finish)
  if(finish LESS 0)
    message(FATAL_ERROR "Missing storage exception review boundary: ${end}")
  endif()
  string(SUBSTRING "${tail}" 0 ${finish} body)
  if(NOT body MATCHES "const (Impl::)?ExceptionRetirement retireOnException")
    message(FATAL_ERROR "Missing storage exception retirement guard: ${begin}")
  endif()
endfunction()
require_storage_unwind_guard("bool Exchange (" "#else")
require_storage_unwind_guard("uint32_t TorrentStorageWorker::Register ("
  "bool TorrentStorageWorker::Read (")
require_storage_unwind_guard("bool TorrentStorageWorker::ReadMetadata ("
  "bool TorrentStorageWorker::ReplaceMetadata (")
require_storage_unwind_guard("bool TorrentStorageWorker::ReplaceMetadata ("
  "bool TorrentStorageWorker::RemoveMetadata (")
require_runtime_order("libi2pd_client/TorrentStorageWorker.cpp"
  "process.ChannelFd ().Close ();" "try\n\t\t{\n\t\t\tprocess.Stop (100);\n\t\t}")
require_runtime_source("libi2pd_client/TorrentStorageWorker.cpp"
  "if (std::uncaught_exceptions () > exceptions)")

# Startup owns a directory capability before path conversion and IPC.
require_storage_unwind_guard("bool TorrentStorageWorker::Start ("
  "uint32_t TorrentStorageWorker::Register (")
require_runtime_order("libi2pd_client/TorrentStorageWorker.cpp"
  "storage worker already running" "const Impl::ExceptionRetirement retireOnException")
require_runtime_order("libi2pd_client/TorrentStorageWorker.cpp"
  "const Impl::ExceptionRetirement retireOnException" "const RootDescriptor rootFd (")
require_runtime_source("libi2pd_client/TorrentStorageWorker.cpp"
  "~RootDescriptor () noexcept\n\t\t{\n\t\t\tif (fd >= 0) ::close (fd);\n\t\t}")
require_runtime_source("libi2pd_client/TorrentStorageWorker.cpp"
  "RootDescriptor (const RootDescriptor &) = delete;")
require_runtime_source("libi2pd_client/TorrentStorageWorker.cpp"
  "RootDescriptor & operator= (const RootDescriptor &) = delete;")
require_runtime_source("libi2pd_client/TorrentStorageWorker.cpp"
  "SpawnSupervisedTorrentStorage (rootFd.fd, absolute.string ())")
require_runtime_source("libi2pd_client/TorrentStorageWorker.cpp"
  "MessageType::ConfigureTorrentStorage, setup.Data (), rootFd.fd, result, error")

# Decode failure must precede every use of the unavailable coordinate.
require_runtime_order("libi2pd/Ed25519.cpp"
  "if (!Bx)" "BN_mod (Bx, Bx, q, ctx)")
require_runtime_order("libi2pd/Ed25519.cpp"
  "if (!buf || !ctx) return { };" "point.y = BN_bin2bn (")
require_runtime_order("libi2pd/Ed25519.cpp"
  "if (!point.x) return { };" "BN_is_bit_set (point.x, 0)")
require_runtime_source("libi2pd/Ed25519.cpp"
  "if (!ctx || !p.x || !p.y) return false;")
require_runtime_source("libi2pd/Ed25519.cpp"
  "throw std::runtime_error (\"Ed25519 base-point recovery failed\");")
require_runtime_source("libi2pd/Ed25519.h" "EDDSAPoint copy {")
require_runtime_source("libi2pd/Ed25519.h" "*this = EDDSAPoint { };")
require_runtime_source("tests/test-eddsa.cpp"
  "ed25519->DecodePublicKey (nullptr, ctx)")
require_runtime_source("tests/test-eddsa.cpp"
  "ed25519->DecodePublicKey (pub, nullptr)")
require_runtime_source("tests/test-eddsa.cpp"
  "copied.x != original.x && copied.y != original.y")

# Invalid suite geometry cannot authorize a read or failure clear.
require_runtime_source("libi2pd/Signature.h"
  "constexpr bool IsSupportedECDSAKeyLength (int curve, size_t keyLen)")
require_runtime_source("libi2pd/Signature.cpp"
  "m_KeyLen (hash && IsSupportedECDSAKeyLength (curve, keyLen) ? keyLen : 0)")
require_runtime_source("libi2pd/Signature.cpp"
  "m_KeyLen (hash && (keyLen == 256 || keyLen == 384 || keyLen == 512) ? keyLen : 0)")
foreach(signature_path IN ITEMS "libi2pd/Signature.cpp" "libi2pd/Signature.h")
  require_runtime_source("${signature_path}"
    "if (!IsSupportedECDSAKeyLength (curve, keyLen)) return false;\n\t\tif (!signingPrivateKey || !signingPublicKey) return false;\n\t\tauto clear = [&] ()")
endforeach()
require_runtime_source("tests/test-signature-identity-hardening.cpp"
  "TestInvalidSignatureParameters ();")
require_runtime_source("tests/test-signature-identity-hardening.cpp"
  "std::numeric_limits<size_t>::max ()")

# Public final scalars can contain secret-dependent intermediate values.
require_runtime_order("libi2pd/Ed25519.cpp"
  "void Ed25519::Sign (" "void Ed25519::SignRedDSA ("
  "SecretBN h (nullptr, BN_clear_free);"
  "SecretBN a (nullptr, BN_clear_free);"
  "BN_mod_mul (h.get (), h.get (), a.get (),"
  "EncodeBN (h.get (), signature +")
file(READ "${PROJECT_ROOT}/libi2pd/Ed25519.cpp" eddsa_cleanup_source)
string(FIND "${eddsa_cleanup_source}" "void Ed25519::Sign (" sign_begin)
string(FIND "${eddsa_cleanup_source}" "void Ed25519::SignRedDSA (" sign_end)
math(EXPR sign_length "${sign_end} - ${sign_begin}")
string(SUBSTRING "${eddsa_cleanup_source}" ${sign_begin} ${sign_length} sign_body)
if(sign_body MATCHES "BN_free [(][ha][)]")
  message(FATAL_ERROR "Signing intermediates must use clear-free ownership")
endif()

# Allocate root metadata before publishing its matching capability.
foreach(storage_path IN ITEMS "privsep/RouterStorage.cpp" "privsep/DataStorage.cpp")
  require_runtime_order("${storage_path}"
    "auto rootPath = absolute.string ();" "m_Impl->rootFd = directory.Release ();")
  require_runtime_source("${storage_path}" "m_Impl->rootPath.swap (rootPath);")
endforeach()

# ACK identity is independent of packet sequence and executor retention.
require_runtime_source("libi2pd_client/UDPTunnel.cpp"
  "m_AckTimer.async_wait (MakeAckHandlerLocked ())")
require_runtime_order("libi2pd_client/UDPTunnel.cpp"
  "auto connection = owner.lock ();" "connection->HandleAckTimeout ();"
  "std::lock_guard lock (admission->mutex);"
  "admission->stopRequested.load ()" "admission->wait != wait")
require_runtime_order("libi2pd_client/UDPTunnel.cpp"
  "void UDPConnection::Stop ()" "void UDPConnection::CancelAckTimer ()"
  "stopRequested.store (true)" "std::lock_guard lock (m_AckAdmission->mutex);"
  "CancelAckTimerLocked ();")
require_runtime_order("libi2pd_client/UDPTunnel.cpp"
  "void I2PUDPClientTunnel::Start ()" "void I2PUDPClientTunnel::Stop ()"
  "UDPConnection::Start ();")
require_runtime_order("libi2pd_client/UDPTunnel.cpp"
  "void I2PUDPClientTunnel::Stop ()" "void I2PUDPClientTunnel::RecvFromLocal ()"
  "UDPConnection::Stop ();" "m_DatagramReceiver.Reset ();")
require_runtime_source("libi2pd_client/UDPTunnel.cpp"
  "if (!m_UnackedDatagrams.empty ()) ScheduleAckTimerLocked (0);")
require_runtime_source("tests/CMakeLists.txt" "test-udp-ack-lifecycle")
require_runtime_source("tests/test-udp-ack-lifecycle.cpp"
  "assert (released.expired ());")
require_runtime_source("tests/test-udp-ack-lifecycle.cpp"
  "connection->WaitForStopRequest ();")
require_runtime_order("tests/test-udp-ack-lifecycle.cpp"
  "dispatch.join ();" "assert (stoppedAfterAction.load ());" "stop.join ();")
require_runtime_order("libi2pd_client/UDPTunnel.cpp"
  "void I2PUDPServerTunnel::ExpireStale (" "void I2PUDPClientTunnel::ExpireStale ("
  "if (m_LastSession == itr->second) m_LastSession.reset ();"
  "itr->second->Stop ();")
require_runtime_order("libi2pd_client/UDPTunnel.cpp"
  "void UDPSession::Stop ()" "void UDPSession::Receive ()"
  "UDPConnection::Stop ();" "m_ReceiveStopped = true;" "IPSocket.close (ignored);")
require_runtime_order("libi2pd_client/UDPTunnel.cpp"
  "void UDPSession::HandleReceived (" "I2PUDPServerTunnel::I2PUDPServerTunnel ("
  "std::lock_guard lock (m_ReceiveMutex);" "if (m_ReceiveStopped) return;")
file(READ "${PROJECT_ROOT}/libi2pd_client/UDPTunnel.cpp" udp_lifecycle_source)
if(udp_lifecycle_source MATCHES "GetDatagramSession [(][)] *->")
  message(FATAL_ERROR "UDP session lookup must be checked before dereference")
endif()
require_runtime_source("libi2pd_client/UDPTunnel.cpp" "if (drainEc) break;")

# Cancellation revokes authority and preserves diagnostics on unwind.
require_runtime_order("libi2pd_client/UDPTunnel.cpp"
  "void UDPConnection::CancelAckTimerLocked () noexcept"
  "void UDPConnection::Acked ("
  "m_AckAdmission->wait.reset ();" "m_AckTimerSeqn = 0;"
  "try\n\t\t{\n\t\t\tm_AckTimer.cancel ();\n\t\t}"
  "m_AckAdmission->cancellationError = error.code ();")
require_runtime_order("libi2pd_client/UDPTunnel.cpp"
  "void UDPConnection::ScheduleAckTimerLocked ("
  "UDPConnection::AckHandler UDPConnection::MakeAckHandlerLocked ()"
  "try" "m_AckTimer.expires_after (" "m_AckTimer.async_wait ("
  "CancelAckTimerLocked ();" "throw;")
require_runtime_source("libi2pd_client/UDPTunnel.cpp"
  "GetAckCancellationError ().value ()")

# Registered defaults are not explicit credentials; empty operands are.
require_runtime_source("libi2pd/Config.cpp"
  "found != options.end () && !found->second.defaulted ()")
require_runtime_order("libi2pd/Config.cpp"
  "if (HasCommandLineCredential (m_Options))"
  "if (!ignoreUnknown && (m_Options.count (\"help\")"
  "std::exit (EXIT_FAILURE);")
file(READ "${PROJECT_ROOT}/libi2pd/Config.cpp" config_policy_source)
if(config_policy_source MATCHES "[.]what [(][)]"
    OR config_policy_source MATCHES "ScanRawCommandLine|ClassifyRawToken|TryPublishRawWindowsInformationalRequest")
  message(FATAL_ERROR "Config diagnostics and informational paths must share safe parsing")
endif()
file(READ "${PROJECT_ROOT}/daemon/i2pd.cpp" entry_policy_source)
string(REGEX MATCHALL "return RunWindowsEntryPoint [(][)]" entry_calls "${entry_policy_source}")
list(LENGTH entry_calls entry_count)
if(NOT entry_count EQUAL 2)
  message(FATAL_ERROR "Both Windows entries must share authoritative conversion")
endif()
require_runtime_order("daemon/i2pd.cpp" "int RunWindowsEntryPoint ()"
  "} // namespace" "WindowsCommandLine commandLine;"
  "RunI2PD (commandLine.argc (), commandLine.argv ())")
require_runtime_source("daemon/i2pd.cpp"
  "WideArguments wide (CommandLineToArgvW (GetCommandLineW (), &count), LocalFree);")
require_runtime_source("daemon/i2pd.cpp" "if (written != length)")
require_runtime_source("tests/CMakeLists.txt" "command-line-policy-rejections")
require_runtime_source("tests/CMakeLists.txt" "timer-cancellation-contract")
require_runtime_source("tests/check-command-line-policy.cmake"
  "if(NOT child_status STREQUAL \"1\")")
require_runtime_source("tests/test-command-line-policy.cpp"
  "assert (found->second.defaulted ());")
foreach(signature_secret_path IN ITEMS "libi2pd/Signature.cpp" "libi2pd/Signature.h")
  file(READ "${PROJECT_ROOT}/${signature_secret_path}" signature_secret_source)
  if(signature_secret_source MATCHES "BN_free [(](priv|privKey)[)]")
    message(FATAL_ERROR "Private signature scalars require clear-free cleanup")
  endif()
endforeach()
require_runtime_source("tests/CMakeLists.txt" "i2pd-command-line-policy")
require_runtime_order("daemon/i2pd.cpp" "int RunI2PD (" "int RunWindowsEntryPoint ()"
  "i2p::config::ParseCmdline (argc, argv);" "Daemon.init (argc, argv)")

# GOST output is transactional; RNG failure is not a degenerate draw.
require_runtime_source("libi2pd/Gost.cpp"
  "if (BN_rand_range (frame.k, q) != 1) return;")
require_runtime_source("libi2pd/Gost.cpp"
  "if (BN_is_zero (frame.k)) continue;")
require_runtime_order("libi2pd/Gost.cpp"
  "void GOSTR3410Curve::Sign (" "bool GOSTR3410Curve::Verify ("
  "BN_mod_mul (stagedS.get (), stagedR.get (), priv, q, frame.ctx) != 1"
  "BN_mod_mul (frame.tmp, frame.k, e, q, frame.ctx) != 1"
  "BN_mod_add (stagedS.get (), stagedS.get (), frame.tmp, q, frame.ctx) != 1"
  "BN_swap (r, stagedR.get ());" "BN_swap (s, stagedS.get ());")
require_runtime_order("libi2pd/Gost.cpp"
  "~SigningFrame ()" "} frame;"
  "if (k) BN_clear (k);" "if (tmp) BN_clear (tmp);"
  "BN_CTX_end (ctx);" "BN_CTX_free (ctx);")
require_runtime_source("tests/test-crypto-audit-bounds.cpp"
  "TestGostSigningRejectsInvalidInputs ();")

# Worker stdio must be transferred, not copied as parent-only values.
require_runtime_source("privsep/WindowsIpc.cpp"
  "standardAttributes.bInheritHandle = TRUE;")
require_runtime_source("privsep/WindowsIpc.cpp"
  "HANDLE standardHandles[3] { };")
require_runtime_source("privsep/WindowsIpc.cpp"
  "PROC_THREAD_ATTRIBUTE_HANDLE_LIST,\n\t\t\t\t\tattributes.standardHandles,")
require_runtime_source("privsep/WindowsIpc.cpp"
  "DWORD attributeCount = 1; // mandatory standard-handle list")
require_runtime_source("privsep/WindowsIpc.cpp"
  "created = CreateProcessW (executable.c_str (), mutableCommandLine.data (), nullptr, nullptr, TRUE,")
require_runtime_source("privsep/WindowsIpc.cpp"
  "nullptr, TRUE, creationFlags, nullptr, nullptr, &startup.StartupInfo, &processInfo);")
require_runtime_order("tests/test-privsep-windows-supervisor.cpp"
  "if (argument == \"--privsep-worker\")" "return RunWindowsWorker ("
  "if (!HasUsableWorkerStandardHandles ()) return 3;")

# Failed EC predicates and arithmetic may not authorize a signature.
require_runtime_order("libi2pd/Gost.cpp"
  "bool GOSTR3410Curve::Verify (" "EC_POINT * GOSTR3410Curve::RecoverPublicKey ("
  "BN_is_negative (digest) || BN_is_negative (r) || BN_is_negative (s)"
  "EC_POINT_is_on_curve (m_Group, pub, nullptr) != 1"
  "BN_mod_mul (z1, s, h, q, ctx) == 1"
  "BN_mod (x, x, q, ctx) == 1 && BN_cmp (x, r) == 0")
require_runtime_source("tests/test-crypto-audit-bounds.cpp"
  "TestGostVerificationAndRecoveryBounds ();")

# Capacity checks must not add an untrusted size before comparing.
require_runtime_source("libi2pd/TransportSession.h"
  "if (len > sz - m_Size) len = sz - m_Size;")
require_runtime_source("libi2pd/I2NPProtocol.h"
  "if (len1 > maxLen - len) len1 = maxLen - len;")
require_runtime_source("libi2pd/I2NPProtocol.h"
  "|| alignment > maxLen - len) return;")
require_runtime_source("tests/CMakeLists.txt" "test-signed-data-bounds")
require_runtime_source("tests/CMakeLists.txt" "android-vector-geometry")

# Composite storage RPCs cannot create fresh relative phase budgets.
function(reject_rebased_storage_rpc path begin_marker end_marker)
  file(READ "${PROJECT_ROOT}/${path}" source)
  string(FIND "${source}" "${begin_marker}" begin)
  if(begin LESS 0)
    message(FATAL_ERROR "Missing storage deadline region in ${path}")
  endif()
  string(SUBSTRING "${source}" ${begin} -1 tail)
  string(FIND "${tail}" "${end_marker}" end)
  if(end LESS 0)
    message(FATAL_ERROR "Missing storage deadline boundary in ${path}")
  endif()
  string(SUBSTRING "${tail}" 0 ${end} region)
  if(region MATCHES "[.](Send|SendWithFd|Receive|ReceiveWithFd)[ \t\r\n]*[(]")
    message(FATAL_ERROR "Storage RPC phases must share Until deadlines: ${path}")
  endif()
endfunction()
reject_rebased_storage_rpc("libi2pd_client/TorrentStorageWorker.cpp"
  "bool Exchange (" "#endif")
reject_rebased_storage_rpc("privsep/RouterStorage.cpp"
  "bool Exchange (MessageType" "\n};")
reject_rebased_storage_rpc("privsep/DataStorage.cpp"
  "bool Operate (" "\n};")
require_runtime_order("libi2pd_client/TorrentStorageWorker.cpp"
  "bool TorrentStorageWorker::ReadMetadata (" "bool TorrentStorageWorker::ReplaceMetadata ("
  "const auto deadline =" "const auto path = relative.generic_string ();"
  "request.Data (), -1, reply, error, deadline)"
  "read.Data (), -1, reply, error, deadline)"
  "release.Data (), -1, reply, error, deadline)")
require_runtime_order("libi2pd_client/TorrentStorageWorker.cpp"
  "bool TorrentStorageWorker::ReplaceMetadata (" "bool TorrentStorageWorker::RemoveMetadata ("
  "const auto deadline =" "std::move (payload), -1, reply, error, deadline)"
  "StorageOperation::MetadataBegin" "StorageOperation::MetadataCommit"
  "StorageOperation::MetadataAbort")
file(READ "${PROJECT_ROOT}/libi2pd_client/TorrentStorageWorker.cpp" storage_until_source)
string(REGEX MATCH "void RetireExpired [(][)] noexcept[^}]*}" retirement "${storage_until_source}")
if(NOT retirement OR retirement MATCHES "[.](Stop|Send|SendUntil)[ \t]*[(]")
  message(FATAL_ERROR "Expired storage authority must retire without a new wait")
endif()
require_runtime_source("privsep/DataStorage.cpp"
  "const auto cancelDeadline\n\t\t\t\t= std::min (deadline,")
require_runtime_source("privsep/Channel.cpp"
  "return SendWithFdUntil (message, fd, false, deadline);")

# A nonsecret probe shares containment, never authentication authority.
require_runtime_source("privsep/WindowsIpc.cpp"
  "enum class WindowsSpawnMode\n\t{\n\t\tWorker,\n\t\tStartupProbe\n\t};")
require_runtime_source("privsep/WindowsIpc.cpp"
  "return i2p::unexpected (\"invalid startup probe authority\");")
require_runtime_order("privsep/WindowsIpc.cpp"
  "SpawnWindowsWorkerStartupProbe (" "bool WindowsWorkerProcess::Wait ("
  "WindowsSpawnMode::StartupProbe" "WindowsWorkerRole::Supervisor, { },\n\t\t0, 0, { }, nullptr,"
  "GetWindowsWorkerPolicy (WindowsWorkerRole::Supervisor)")
require_runtime_order("tests/test-privsep-windows-supervisor.cpp"
  "int main (int argc, char ** argv)" "for (int index = 1; index < argc; ++index)"
  "--diagnose-worker-initialization" "return argc == 2 ? DiagnoseWorkerInitialization () : 2;"
  "--windows-startup-probe" "return argc == 2 && HasUsableWorkerStandardHandles () ? 0 : 3;")
require_runtime_source("tests/test-datastorage-client.cpp" "WEXITED | WNOHANG | WNOWAIT")
require_runtime_source("tests/test-datastorage-client.cpp"
  "assert (client.WorkerPid () == firstWorker);")
require_runtime_source("tests/test-datastorage-client.cpp"
  "assert (::waitpid (static_cast<pid_t> (retiredWorker), nullptr, WNOHANG) == -1);")
file(READ "${PROJECT_ROOT}/tests/test-datastorage-client.cpp" death_fixture)
string(REGEX MATCH "bool WaitForFailure [(][^#]*" death_observer "${death_fixture}")
if(NOT death_observer OR death_observer MATCHES "attempt < 100")
  message(FATAL_ERROR "Worker death observation must not retry mutating RPCs")
endif()
string(REGEX MATCHALL "client[.]Write [(]" death_rpc_calls "${death_observer}")
list(LENGTH death_rpc_calls death_rpc_count)
if(NOT death_rpc_count EQUAL 1)
  message(FATAL_ERROR "Worker death fixture must issue exactly one failure RPC")
endif()
require_runtime_source("libi2pd/I2NPProtocol.cpp"
  "if (len <= I2NP_MAX_SHORT_MESSAGE_SIZE - overhead)")
require_runtime_source("libi2pd/I2NPProtocol.cpp"
  "if (len <= I2NP_MAX_MEDIUM_MESSAGE_SIZE - overhead)")
require_runtime_source("libi2pd/I2NPProtocol.cpp"
  "if (len) memcpy (msg->GetBuffer (), buf, len);")

# Provider digest failure must not become cipher input or plaintext.
require_runtime_order("libi2pd/Crypto.cpp"
  "bool ECIESEncrypt (" "bool ECIESDecrypt ("
  "ok = SHA256 (keyBuf, len, shared) != nullptr;"
  "ok = ok && SHA256 (m + 33, 222, m + 1) != nullptr;"
  "if (ok) encryption.SetKey (shared);"
  "ok = ok && encryption.EncryptChecked (m, 256, iv, encrypted + 258);")
require_runtime_order("libi2pd/Crypto.cpp"
  "bool ECIESDecrypt (" "void GenerateECIESKeyPair ("
  "&& SHA256 (keyBuf, len, shared) != nullptr)"
  "decryption.SetKey (shared);"
  "if (SHA256 (m + 33, 222, hash) != nullptr"
  "&& CRYPTO_memcmp (m + 1, hash, 32) == 0)"
  "memcpy (data, m + 33, 222);")
require_runtime_source("tests/test-crypto-audit-bounds.cpp"
  "TestECIESIntegrityPublication ();")

# Weak state is correlation; owner admission must serialize actual use.
require_runtime_order("libi2pd_client/TorrentsDHT.cpp"
  "TorrentsDHT::TimerCallback TorrentsDHT::MakeTimerCallbackLocked ("
  "void TorrentsDHT::ScheduleTimerLocked ("
  "std::weak_ptr<DHTServiceState> guard = m_ServiceState;"
  "std::lock_guard admission (state->ownerMutex);"
  "|| !state->owner || state->waits[index] != wait)\n\t\t\t\treturn;"
  "state->waits[index].reset ();" "action (*state->owner, error);")
require_runtime_order("libi2pd_client/TorrentsDHT.cpp"
  "TorrentsDHT::~TorrentsDHT ()" "void TorrentsDHT::Start ()"
  "RetireServiceState ();" "m_DatagramReceiver.Reset ();"
  "std::lock_guard persistence (m_ServiceState->persistenceMutex);")
require_runtime_source("tests/test-torrents-dht-protocol.cpp"
  "TestTimerOwnerAdmission ();")

# Created children need retirement before any allocating metadata/error.
require_runtime_order("privsep/WindowsIpc.cpp"
  "if (!created) return i2p::unexpected (lastCreateError);"
  "i2p::expected<WindowsWorkerProcess, std::string> SpawnWindowsWorker ("
  "worker.process.Reset (processInfo.hProcess);"
  "worker.thread.Reset (processInfo.hThread);"
  "auto terminateUnpublished = i2p::modern::ScopeExit (\n\t\t\t[&worker] () noexcept"
  "worker.TerminateAndWait (2000, ERROR_PROCESS_ABORTED)"
  "worker.nonceHex = nonce;" "AssignProcessToJobObject ("
  "ResumeThread (worker.thread.Native ())" "terminateUnpublished.dismiss ();"
  "return worker;")

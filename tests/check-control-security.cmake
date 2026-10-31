# See the LICENSE file at the top of the project tree for copyright and
# license details.
#
# openi2pd: compile-free control, proxy and lifetime security contracts.

if(NOT PROJECT_ROOT)
  message(FATAL_ERROR "PROJECT_ROOT is required")
endif()

file(READ "${PROJECT_ROOT}/libi2pd_client/BOB.cpp" bob)
file(READ "${PROJECT_ROOT}/daemon/I2PControl.cpp" control)
file(READ "${PROJECT_ROOT}/daemon/I2PControl.h" control_header)

if(bob MATCHES "setkeys[^\n]*operand")
  message(FATAL_ERROR "BOB setkeys must never log or format its secret operand")
endif()
foreach(required IN ITEMS
    "weak_ptr<BOBI2PInboundTunnel>"
    "weak_ptr<BOBI2POutboundTunnel>"
    "m_Stopped"
    "m_AdmissionMutex"
    "m_Receivers")
  string(FIND "${bob}" "${required}" position)
  if(position EQUAL -1)
    message(FATAL_ERROR "Missing BOB lifetime contract: ${required}")
  endif()
endforeach()

if(control MATCHES "New password=")
  message(FATAL_ERROR "I2PControl must not log administrative password values")
endif()
if(control MATCHES "boost::asio::read[ ]*\\(")
  message(FATAL_ERROR "I2PControl request reads must remain asynchronous")
endif()
foreach(required IN ITEMS
    "ArmControlDeadline"
    "transfer_exactly (4)"
    "async_read_until"
    "I2P_CONTROL_MAX_CONNECTIONS"
    "I2P_CONTROL_MAX_TOKENS"
    "WritePEMAtomically"
    "AtomicWritePrivate"
    "m_Connections"
    "refuses a non-loopback listener with the default password")
  string(FIND "${control}${control_header}" "${required}" position)
  if(position EQUAL -1)
    message(FATAL_ERROR "Missing I2PControl security contract: ${required}")
  endif()
endforeach()

file(READ "${PROJECT_ROOT}/libi2pd/Streaming.cpp" streaming)
file(READ "${PROJECT_ROOT}/libi2pd/Streaming.h" streaming_header)
file(READ "${PROJECT_ROOT}/libi2pd/Destination.cpp" destination)
file(READ "${PROJECT_ROOT}/libi2pd/Destination.h" destination_header)
file(READ "${PROJECT_ROOT}/libi2pd/RouterInfo.h" router_info_header)
file(READ "${PROJECT_ROOT}/libi2pd/SSU2.cpp" ssu2)
file(READ "${PROJECT_ROOT}/libi2pd/SSU2Session.cpp" ssu2_session)
file(READ "${PROJECT_ROOT}/libi2pd/SSU2Session.h" ssu2_session_header)
file(READ "${PROJECT_ROOT}/libi2pd/SSU2OutOfSession.h" ssu2_out_of_session_header)
file(READ "${PROJECT_ROOT}/libi2pd/SSU2OutOfSession.cpp" ssu2_out_of_session)
file(READ "${PROJECT_ROOT}/libi2pd/NTCP2.h" ntcp2_header)
file(READ "${PROJECT_ROOT}/libi2pd/NTCP2.cpp" ntcp2_source)
file(READ "${PROJECT_ROOT}/libi2pd/ECIESX25519AEADRatchetSession.h" ratchet_header)
file(READ "${PROJECT_ROOT}/libi2pd/ECIESX25519AEADRatchetSession.cpp" ratchet_source)
file(READ "${PROJECT_ROOT}/libi2pd/Garlic.h" garlic_header)
file(READ "${PROJECT_ROOT}/libi2pd/Crypto.h" crypto_header)
file(READ "${PROJECT_ROOT}/libi2pd/Crypto.cpp" crypto_source)
file(READ "${PROJECT_ROOT}/libi2pd/Ed25519.cpp" ed25519_source)
file(READ "${PROJECT_ROOT}/libi2pd/PostQuantum.h" pq_header)
file(READ "${PROJECT_ROOT}/libi2pd/PostQuantum.cpp" pq_source)
file(READ "${PROJECT_ROOT}/libi2pd_client/Torrents.h" torrents_header)
file(READ "${PROJECT_ROOT}/libi2pd_client/Torrents.cpp" torrents)
file(READ "${PROJECT_ROOT}/libi2pd/NetDb.cpp" netdb_source)
file(READ "${PROJECT_ROOT}/libi2pd/NetDb.hpp" netdb_header)
file(READ "${PROJECT_ROOT}/libi2pd/I2NPProtocol.h" i2np_header)
file(READ "${PROJECT_ROOT}/libi2pd/Profiling.h" profiling_header)
file(READ "${PROJECT_ROOT}/libi2pd/LeaseSet.cpp" leaseset_source)
file(READ "${PROJECT_ROOT}/libi2pd/Blocklist.h" blocklist_header)
file(READ "${PROJECT_ROOT}/libi2pd/Blocklist.cpp" blocklist_source)
file(READ "${PROJECT_ROOT}/libi2pd/TuiProtocol.h" tui_protocol)
file(READ "${PROJECT_ROOT}/libi2pd_client/TorrentsTunnel.h" torrents_tunnel_header)
file(READ "${PROJECT_ROOT}/libi2pd_client/TorrentsTunnel.cpp" torrents_tunnel)
file(READ "${PROJECT_ROOT}/libi2pd_client/SOCKS.cpp" socks)
file(READ "${PROJECT_ROOT}/libi2pd_client/HTTPProxy.cpp" http_proxy)
file(READ "${PROJECT_ROOT}/libi2pd_client/I2PService.h" service_header)
file(READ "${PROJECT_ROOT}/android/app/src/main/cpp/i2pd_android.cpp" android_jni)

# Streams keep their destination alive and Stop() runs on the
# destination's io_context, so SAM teardown cannot free a
# StreamingDestination under a pending timer/handler (ASan/TSan exercise
# the resulting code paths).
foreach(required IN ITEMS
    "m_LocalDestinationOwner"
    "m_AcceptorMutex"
    "running_in_this_thread ()"
    "StopInternal (")
  string(FIND "${streaming_header}${streaming}" "${required}" position)
  if(position EQUAL -1)
    message(FATAL_ERROR "Missing streaming lifetime contract: ${required}")
  endif()
endforeach()

# The NACK count is written into a one-byte wire field, but accumulation
# must not wrap before the 256 guard or the saved-packet cap is
# bypassed.
foreach(required IN ITEMS
    "size_t numNacks = 0"
    "if (numNacks + (seqn - nextSeqn) >= 256)"
    "static_cast<uint8_t> (numNacks)")
  string(FIND "${streaming}" "${required}" position)
  if(position EQUAL -1)
    message(FATAL_ERROR "Missing Streaming NACK bound contract: ${required}")
  endif()
endforeach()

# SAM creates/removes per-port streaming destinations from its control
# thread.
foreach(required IN ITEMS
    "m_StreamingDestinationsMutex"
    "GetDefaultStreamingDestination")
  string(FIND "${destination_header}${destination}" "${required}" position)
  if(position EQUAL -1)
    message(FATAL_ERROR "Missing streaming-destination registry contract: ${required}")
  endif()
endforeach()

# SSU2 snapshots introducers instead of iterating a vector the netDb
# thread prunes. The header owns the mutex and the snapshot accessor.
foreach(required IN ITEMS
    "GetIntroducers () const"
    "mutable std::mutex mutex"
    "address->ssu->GetIntroducers ()"
    "SSU2_MAX_OUT_OF_SEQUENCE_FRAGMENTS"
    "outOfSequenceBytes")
  string(FIND "${router_info_header}${ssu2}${ssu2_session}${ssu2_session_header}" "${required}" position)
  if(position EQUAL -1)
    message(FATAL_ERROR "Missing introducer-snapshot contract: ${required}")
  endif()
endforeach()

# A TorrentFile handle is shared by the disk worker and the torrent
# service.
foreach(required IN ITEMS
    "OpenLocked"
    "CloseLocked"
    "Locked (bool create)"
    "std::lock_guard<std::mutex> lock (m_Mutex)")
  string(FIND "${torrents_header}${torrents}" "${required}" position)
  if(position EQUAL -1)
    message(FATAL_ERROR "Missing TorrentFile serialization contract: ${required}")
  endif()
endforeach()

# Peer-wire state changes are gated on the block actually being applied,
# the datagram-tracker default port is applied without rewriting the
# stored URL, and a failed part-file/directory creation publishes a
# filesystem error from the disk worker. BEP 3 cancel must stay a
# queued-request removal with the fork's Fast Extension reject gate
# (upstream replies unconditionally).
foreach(required IN ITEMS
    "bool BlockReceived (const uint8_t * block, size_t len, size_t offset)"
    "applied = piece.BlockReceived (buf + 8, len, offset)"
    "eTorrentErrorFilesystem"
    "std::atomic<TorrentError> m_Error"
    "void TorrentComplete ();"
    "current->SetError (eTorrentErrorNoError)"
    "HandleCancelMsg"
    "if (m_IsFast) SendRejectRequestMsg")
  string(FIND "${torrents_header}${torrents}" "${required}" position)
  if(position EQUAL -1)
    message(FATAL_ERROR "Missing torrent peer-wire/storage contract: ${required}")
  endif()
endforeach()
foreach(required IN ITEMS
    "DATAGRAM_TRACKER_DEFAULT_PORT"
    "reqURL.port ? reqURL.port : DATAGRAM_TRACKER_DEFAULT_PORT"
    "SetError (eTorrentErrorFilesystem)"
    "it->TorrentComplete ();"
    "self->InitTorrentFiles (torrent);"
    "torrent->GetError () != eTorrentErrorNoError")
  string(FIND "${torrents_tunnel_header}${torrents_tunnel}" "${required}" position)
  if(position EQUAL -1)
    message(FATAL_ERROR "Missing torrent tracker/storage contract: ${required}")
  endif()
endforeach()

# The operator blocklist is one immutable snapshot published atomically;
# the runtime ban set is separate, bounded and in-memory only.
# Subscription merges may never re-point an already-known name.
foreach(required IN ITEMS
    "class OperatorBlocklistSnapshot"
    "std::shared_ptr<const OperatorBlocklistSnapshot>"
    "LoadOperatorBlocklist ()"
    "IsOperatorBlockedAddress"
    "IsOperatorBlockedRouter"
    "AddRuntimeRouterBan"
    "RemoveRuntimeRouterBan"
    "GetRuntimeRouterBans"
    "OPERATOR_BLOCKLIST_MAX_RUNTIME_BANS")
  string(FIND "${blocklist_header}${blocklist_source}" "${required}" position)
  if(position EQUAL -1)
    message(FATAL_ERROR "Missing operator blocklist contract: ${required}")
  endif()
endforeach()
# Address-book merge precedence and subscription administration.
file(READ "${PROJECT_ROOT}/libi2pd_client/AddressBook.h" addressbook_header)
file(READ "${PROJECT_ROOT}/libi2pd_client/AddressBook.cpp" addressbook_source)
foreach(required IN ITEMS
    "GetSubscriptions (size_t max) const"
    "AddSubscription (std::string_view url, std::string & error)"
    "RemoveSubscription (std::string_view url, std::string & error)"
    "GetMergeConflicts () const"
    "IsValidSubscriptionURL")
  string(FIND "${addressbook_header}${addressbook_source}" "${required}" position)
  if(position EQUAL -1)
    message(FATAL_ERROR "Missing address-book subscription/merge contract: ${required}")
  endif()
endforeach()

# Tracker announces keep whole-torrent progress separate from the
# selection-based local completion state, and honour Java scheduling
# floors.
foreach(required IN ITEMS
    "MIN_TRACKER_ANNOUNCE_INTERVAL"
    "MAX_TRACKER_FAILURE_BACKOFF"
    "struct AnnounceProgress"
    "GetAnnounceProgress () const")
  string(FIND "${torrents_header}${torrents}" "${required}" position)
  if(position EQUAL -1)
    message(FATAL_ERROR "Missing tracker progress/scheduling contract: ${required}")
  endif()
endforeach()

# Administrative verbs stay a closed typed set; no generic option/path
# writer.
foreach(required IN ITEMS
    "ADDRESSBOOK-SUBSCRIPTIONS"
    "BLOCKLIST"
    "NETDB-BAN"
    "NETDB-UNBAN"
    "ADDRESS-SUB-ADD"
    "ADDRESS-SUB-REMOVE"
    "MAX_SUBSCRIPTION_URL_LENGTH")
  string(FIND "${tui_protocol}" "${required}" position)
  if(position EQUAL -1)
    message(FATAL_ERROR "Missing TUI administrative verb contract: ${required}")
  endif()
endforeach()

# SSU2/ NTCP2 transport hardening: pre-authentication block whitelists,
# PeerTest address provenance, ACK-0 completion, admission caps and the
# NTCP2 AEAD-failure drain are load-bearing wire/security contracts.
foreach(required IN ITEMS
    "IsHolePunchPayloadBlockAllowed"
    "eSSU2SessionStateHolePunch"
    "AllowSSU2PeerTestTarget"
    "AllowPeerTestExternalAddressUpdate"
    "SetCharlieEndpoint"
    "IsSSU2AckCoveringPacketZero"
    "IsSSU2RouterInfoFragmentAcceptable"
    "IsSSU2OutOfSessionVersionAccepted"
    "SSU2_MAX_INBOUND_SESSIONS"
    "SSU2_MAX_PEER_TEST_SESSIONS"
    "SSU2_MAX_OUT_OF_SEQUENCE_BYTES"
    "m_AddressMutex")
  string(FIND "${ssu2}${ssu2_session}${ssu2_session_header}${ssu2_out_of_session}${ssu2_out_of_session_header}"
    "${required}" position)
  if(position EQUAL -1)
    message(FATAL_ERROR "Missing SSU2 transport hardening contract: ${required}")
  endif()
endforeach()
foreach(required IN ITEMS
    "NTCP2FailureDrain"
    "NTCP2GetMaxHandshakePadding"
    "NTCP2IsValidHandshakePadding"
    "NTCP2_AEAD_FAILURE_DRAIN_MAX_SIZE"
    "eNTCP2IntraFrameReadTimeout"
    "NTCP2_ROUTER_INFO_MAX_FLOOD_SIZE")
  string(FIND "${ntcp2_header}${ntcp2_source}" "${required}" position)
  if(position EQUAL -1)
    message(FATAL_ERROR "Missing NTCP2 hardening contract: ${required}")
  endif()
endforeach()
foreach(required IN ITEMS
    "HasForeignNetId")
  string(FIND "${router_info_header}" "${required}" position)
  if(position EQUAL -1)
    message(FATAL_ERROR "Missing RouterInfo netId contract: ${required}")
  endif()
endforeach()

# Ratchet split promotion, NSR payload whitelist, tagset-exhaustion
# restart, fail-closed X25519 generation, Ed25519 secret cleansing and
# PQ downgrade resistance are load-bearing cryptographic contracts.
foreach(required IN ITEMS
    "m_PendingReplyTagsets"
    "MarkFirstTagConsumed"
    "ValidateNewSessionReplyPayload"
    "m_SendTagsetExhausted"
    "ECIESX25519_MAX_INBOUND_TAGS")
  string(FIND "${ratchet_header}${ratchet_source}${garlic_header}" "${required}" position)
  if(position EQUAL -1)
    message(FATAL_ERROR "Missing ratchet security contract: ${required}")
  endif()
endforeach()
foreach(required IN ITEMS
    "bool GenerateKeys ()"
    "HasPrivateKey"
    "X25519Keys (const X25519Keys &) = delete")
  string(FIND "${crypto_header}" "${required}" position)
  if(position EQUAL -1)
    message(FATAL_ERROR "Missing X25519 ownership/generation contract: ${required}")
  endif()
endforeach()
foreach(required IN ITEMS
    "BN_clear_free (r)"
    "BN_clear_free (alpha)")
  string(FIND "${ed25519_source}" "${required}" position)
  if(position EQUAL -1)
    message(FATAL_ERROR "Missing Ed25519 secret-cleansing contract: ${required}")
  endif()
endforeach()
foreach(required IN ITEMS
    "bool InitNoiseIKStateMLKEM"
    "bool InitNoiseXKStateMLKEM1")
  string(FIND "${pq_header}" "${required}" position)
  if(position EQUAL -1)
    message(FATAL_ERROR "Missing fallible ML-KEM initialiser contract: ${required}")
  endif()
endforeach()
foreach(required IN ITEMS
    "ParseLeaseSetEncryptionTypes")
  string(FIND "${destination_header}" "${required}" position)
  if(position EQUAL -1)
    message(FATAL_ERROR "Missing leaseSetEncType contract: ${required}")
  endif()
endforeach()

# netDb/LeaseSet continuation: garlic origin, flood admission, bounded
# netDb admission, identity-bound persistence, non-creating profile
# selection and MetaLeaseSet canonicalisation are load-bearing security
# contracts.
foreach(required IN ITEMS
    "IsDatabaseLookupDispatchAllowed"
    "IsAllowedCloveMessageType"
    "fromTunnel")
  string(FIND "${i2np_header}" "${required}" position)
  if(position EQUAL -1)
    message(FATAL_ERROR "Missing garlic-origin contract: ${required}")
  endif()
endforeach()
foreach(required IN ITEMS
    "GetDatabaseStoreOrigin"
    "FloodRouterInfoConditional"
    "NETDB_MAX_QUEUED_MESSAGES"
    "NETDB_MAX_ROUTER_INFOS"
    "ClassifyRouterInfoFile"
    "PutIfBelow")
  string(FIND "${netdb_header}${netdb_source}" "${required}" position)
  if(position EQUAL -1)
    message(FATAL_ERROR "Missing netDb admission/persistence contract: ${required}")
  endif()
endforeach()
foreach(required IN ITEMS
    "GetRouterProfileIfExists"
    "GetProfileIfExists")
  string(FIND "${profiling_header}${router_info_header}" "${required}" position)
  if(position EQUAL -1)
    message(FATAL_ERROR "Missing non-creating profile contract: ${required}")
  endif()
endforeach()
foreach(required IN ITEMS
    "if (expiration != latestEntry)"
    "numRevocations")
  string(FIND "${leaseset_source}" "${required}" position)
  if(position EQUAL -1)
    message(FATAL_ERROR "Missing MetaLeaseSet canonicalisation contract: ${required}")
  endif()
endforeach()

# SOCKS/HTTP handshakes are bounded and replies are serialized.
foreach(required IN ITEMS
    "m_DeferredAuthInput"
    "SOCKS_HANDSHAKE_TIMEOUT"
    "m_HandshakeTimer.cancel (")
  string(FIND "${socks}" "${required}" position)
  if(position EQUAL -1)
    message(FATAL_ERROR "Missing SOCKS handshake contract: ${required}")
  endif()
endforeach()
foreach(required IN ITEMS
    "HTTP_PROXY_HEADER_TIMEOUT"
    "m_HeaderTimer.cancel ("
    "ArmHeaderDeadline")
  string(FIND "${http_proxy}" "${required}" position)
  if(position EQUAL -1)
    message(FATAL_ERROR "Missing HTTP handshake contract: ${required}")
  endif()
endforeach()
foreach(required IN ITEMS
    "GetNumHandlers () const"
    "MAX_ACCEPTED_HANDLERS" "GetNumHandlers () >= MAX_ACCEPTED_HANDLERS")
  string(FIND "${service_header}" "${required}" position)
  if(position EQUAL -1)
    message(FATAL_ERROR "Missing acceptor admission contract: ${required}")
  endif()
endforeach()

# JNI local-frame failure must not leave a pending exception.
foreach(required IN ITEMS
    "failed = true"
    "env->ExceptionClear (")
  string(FIND "${android_jni}" "${required}" position)
  if(position EQUAL -1)
    message(FATAL_ERROR "Missing JNI local-frame contract: ${required}")
  endif()
endforeach()

# I2PControl Stop must not join its own io_context thread.
foreach(required IN ITEMS
    "running_in_this_thread ()"
    "onServiceThread")
  string(FIND "${control}" "${required}" position)
  if(position EQUAL -1)
    message(FATAL_ERROR "Missing I2PControl self-join contract: ${required}")
  endif()
endforeach()

file(READ "${PROJECT_ROOT}/libi2pd/FS.cpp" filesystem)
foreach(required IN ITEMS
    "BuildPrivateFileSecurity"
    "SE_DACL_PROTECTED"
    "FILE_FLAG_OPEN_REPARSE_POINT"
    "fchmod (fd, S_IRUSR | S_IWUSR)")
  string(FIND "${filesystem}" "${required}" position)
  if(position EQUAL -1)
    message(FATAL_ERROR "Missing private atomic-file contract: ${required}")
  endif()
endforeach()

# Tunnel build / I2NP / garlic hardening: the record table is planned
# before any write, a reused path passes the same admission rules as
# fresh selection, a transit build request is replay-filtered, the
# dispatcher applies the Java expiration/duplicate window and a clove
# payload is bounded.
file(READ "${PROJECT_ROOT}/libi2pd/Tunnel.cpp" tunnel)
file(READ "${PROJECT_ROOT}/libi2pd/TunnelPool.cpp" tunnel_pool)
file(READ "${PROJECT_ROOT}/libi2pd/TunnelConfig.cpp" tunnel_config)
file(READ "${PROJECT_ROOT}/libi2pd/TunnelBase.h" tunnel_base)
file(READ "${PROJECT_ROOT}/libi2pd/TransitTunnel.cpp" transit_tunnel)
file(READ "${PROJECT_ROOT}/libi2pd/I2NPProtocol.cpp" i2np_protocol)
file(READ "${PROJECT_ROOT}/libi2pd/I2NPProtocol.h" i2np_protocol_header)
file(READ "${PROJECT_ROOT}/libi2pd/Garlic.cpp" garlic)
file(READ "${PROJECT_ROOT}/libi2pd/Destination.h" destination_header_garlic)

foreach(required IN ITEMS
    "PlanTunnelBuildRecords (numHops"
    "num != sentRecords"
    "TUNNEL_MESSAGE_QUEUE_MAX_SIZE")
  string(FIND "${tunnel}${tunnel_base}" "${required}" position)
  if(position EQUAL -1)
    message(FATAL_ERROR "Missing tunnel build record contract: ${required}")
  endif()
endforeach()

foreach(required IN ITEMS
    "IsRouterUsableForTunnel"
    "CanReuseTunnelPeers"
    "TunnelPeerSelectorHandle::TrySelectPeers")
  string(FIND "${tunnel_pool}" "${required}" position)
  if(position EQUAL -1)
    message(FATAL_ERROR "Missing tunnel pool admission/lifetime contract: ${required}")
  endif()
endforeach()

foreach(required IN ITEMS
    "IsRecordIndexValid (numRecords)"
    "OPENSSL_cleanse (sharedSecret"
    "memset (record, 0, SHORT_TUNNEL_BUILD_RECORD_SIZE)")
  string(FIND "${tunnel_config}" "${required}" position)
  if(position EQUAL -1)
    message(FATAL_ERROR "Missing fail-closed build record contract: ${required}")
  endif()
endforeach()

foreach(required IN ITEMS
    "m_BuildReplayFilter.CheckAndStore"
    "IsExactTunnelBuildMessageSize (len, num, true)"
    "TUNNEL_BUILD_REPLY_EXPIRATION_MS")
  string(FIND "${transit_tunnel}" "${required}" position)
  if(position EQUAL -1)
    message(FATAL_ERROR "Missing transit build replay/size contract: ${required}")
  endif()
endforeach()

foreach(required IN ITEMS
    "IsI2NPDispatchExpired (*msg, now)"
    "g_RouterDuplicateFilter.CheckAndInsert")
  string(FIND "${i2np_protocol}" "${required}" position)
  if(position EQUAL -1)
    message(FATAL_ERROR "Missing I2NP dispatch admission contract: ${required}")
  endif()
endforeach()
foreach(required IN ITEMS
    "I2NP_DISPATCH_MAX_AGE"
    "I2NPDuplicateFilter"
    "IsAllowedCloveMessageType (uint8_t typeID, bool fromTunnel)")
  string(FIND "${i2np_protocol_header}" "${required}" position)
  if(position EQUAL -1)
    message(FATAL_ERROR "Missing I2NP dispatch policy declaration: ${required}")
  endif()
endforeach()

foreach(required IN ITEMS
    "IsValidCloveCount (numCloves)"
    "m_CloveReplayFilter.CheckAndInsert"
    "GetThreadLocalHKDFContext")
  string(FIND "${garlic}" "${required}" position)
  if(position EQUAL -1)
    message(FATAL_ERROR "Missing garlic clove/session contract: ${required}")
  endif()
endforeach()
foreach(required IN ITEMS
    "MAX_INCOMING_GARLIC_QUEUE"
    "IsValidDataMessageLength")
  string(FIND "${destination_header_garlic}" "${required}" position)
  if(position EQUAL -1)
    message(FATAL_ERROR "Missing garlic queue/length contract: ${required}")
  endif()
endforeach()

# SAM / I2CP / streaming / datagram client-service hardening: client
# secrets are never logged, the SAM control plane is bounded and an idle
# session socket is left open (Java disables SO_TIMEOUT after HELLO),
# b33 names resolve, the BlindingInfo auth material reaches the
# encrypted-LeaseSet request, the datagram framing is selected per send
# and a rejected SYN is answered.
file(READ "${PROJECT_ROOT}/libi2pd_client/SAM.h" sam_header)
file(READ "${PROJECT_ROOT}/libi2pd_client/SAM.cpp" sam_source)
file(READ "${PROJECT_ROOT}/libi2pd/Streaming.h" streaming_header)
file(READ "${PROJECT_ROOT}/libi2pd/Streaming.cpp" streaming_source)
file(READ "${PROJECT_ROOT}/libi2pd/Datagram.h" datagram_header)
file(READ "${PROJECT_ROOT}/libi2pd/Datagram.cpp" datagram_source)
file(READ "${PROJECT_ROOT}/libi2pd_client/AddressBook.cpp" addressbook_source)
file(READ "${PROJECT_ROOT}/libi2pd_client/I2CP.cpp" i2cp_source)
file(READ "${PROJECT_ROOT}/libi2pd_client/I2CP.h" i2cp_header)
file(READ "${PROJECT_ROOT}/libi2pd/Destination.h" destination_header_leaseset)
file(READ "${PROJECT_ROOT}/libi2pd/Destination.cpp" destination_source_leaseset)

foreach(required IN ITEMS
    "SAM_MAX_CONTROL_CONNECTIONS"
    "SAM_CONTROL_HELLO_TIMEOUT"
    "SAM_CONTROL_PARTIAL_TIMEOUT"
    "SAMStyleListenProtocol"
    "ResolveSAMSessionListenPort"
    "SAM_STREAM_STATUS_ALREADY_ACCEPTING")
  string(FIND "${sam_header}" "${required}" position)
  if(position EQUAL -1)
    message(FATAL_ERROR "Missing SAM control/session contract: ${required}")
  endif()
endforeach()
foreach(required IN ITEMS
    "RearmQueuedAccept"
    "ReleaseControlConnection"
    "ArmControlDeadline")
  string(FIND "${sam_source}" "${required}" position)
  if(position EQUAL -1)
    message(FATAL_ERROR "Missing SAM teardown/admission contract: ${required}")
  endif()
endforeach()
# The SESSION CREATE line carries private keys and the HELLO line may
# carry USER/PASSWORD: neither may ever be passed to a log call.
string(REGEX MATCH "LogPrint[^\n]*\"SAM: Session create: \"[^\n]*buf" sam_secret_log "${sam_source}")
if(sam_secret_log)
  message(FATAL_ERROR "SAM logs the SESSION CREATE operand (private keys)")
endif()
string(REGEX MATCH "LogPrint[^\n]*\"SAM: Handshake [^\n]*line" sam_hello_log "${sam_source}")
if(sam_hello_log)
  message(FATAL_ERROR "SAM logs the HELLO line (credentials)")
endif()
string(REGEX MATCH "SAM: Datagram send: \"[^\n]*payload" sam_payload_log "${sam_source}")
if(sam_payload_log)
  message(FATAL_ERROR "SAM logs a datagram payload")
endif()

foreach(required IN ITEMS
    "MAX_RECENTLY_CLOSED_STREAMS"
    "AcceptorToken"
    "RemoveAcceptor"
    "SyncAcceptState")
  string(FIND "${streaming_header}" "${required}" position)
  if(position EQUAL -1)
    message(FATAL_ERROR "Missing streaming accept/replay contract: ${required}")
  endif()
endforeach()
foreach(required IN ITEMS
    "RememberRecentlyClosed"
    "RejectSynWithReset"
    "SendResetOnce")
  string(FIND "${streaming_source}" "${required}" position)
  if(position EQUAL -1)
    message(FATAL_ERROR "Missing streaming rejection contract: ${required}")
  endif()
endforeach()

foreach(required IN ITEMS
    "eDatagramDefault"
    "IsTypedDatagramProtocol"
    "SetReceiver (const Receiver & receiver, uint16_t port = 0, uint8_t protocol = 0)")
  string(FIND "${datagram_header}" "${required}" position)
  if(position EQUAL -1)
    message(FATAL_ERROR "Missing datagram dispatch contract: ${required}")
  endif()
endforeach()
foreach(required IN ITEMS
    "MakeReceiverKey"
    "ResolveDatagramVersion")
  string(FIND "${datagram_source}" "${required}" position)
  if(position EQUAL -1)
    message(FATAL_ERROR "Missing datagram protocol/version contract: ${required}")
  endif()
endforeach()

foreach(required IN ITEMS
    ".b33.i2p"
    "B33_LABEL_MAX_LENGTH")
  string(FIND "${addressbook_source}" "${required}" position)
  if(position EQUAL -1)
    message(FATAL_ERROR "Missing b33 resolution contract: ${required}")
  endif()
endforeach()

# basic_waitable_timer exposes cancel()/cancel_one() only; an error_code
# argument fails MSVC and other template instantiations.
foreach(source IN ITEMS i2cp_source sam_source)
  string(FIND "${${source}}" ".cancel (ec)" timer_cancel_ec)
  if(NOT timer_cancel_ec EQUAL -1)
    message(FATAL_ERROR "Timer cancel(error_code) is not portable in ${source}")
  endif()
endforeach()
foreach(required IN ITEMS
    "GetI2CPBlindingAuthSecret"
    "GetI2CPHostReplyResultForAuth"
    "RequestDestinationWithEncryptedLeaseSet"
    "blinded, complete")
  string(FIND "${i2cp_source}" "${required}" position)
  if(position EQUAL -1)
    message(FATAL_ERROR "Missing I2CP blinding-info contract: ${required}")
  endif()
endforeach()
foreach(required IN ITEMS
    "I2CP_MAX_CONNECTIONS_LIMIT"
    "eI2CPHostReplyDecryptionFailure"
    "FindI2CPBlindingInfoByBlindedKey")
  string(FIND "${i2cp_header}" "${required}" position)
  if(position EQUAL -1)
    message(FATAL_ERROR "Missing I2CP admission/HostReply contract: ${required}")
  endif()
endforeach()

# The encrypted-LeaseSet request must carry the per-destination
# client-auth material instead of silently falling back to an
# unauthenticated lookup.
foreach(required IN ITEMS
    "RequestDestinationWithEncryptedLeaseSet"
    "authSecret")
  string(FIND "${destination_header_leaseset}" "${required}" position)
  if(position EQUAL -1)
    message(FATAL_ERROR "Missing encrypted-LeaseSet auth declaration: ${required}")
  endif()
endforeach()
string(FIND "${destination_source_leaseset}" "request->authSecret" auth_secret_use)
if(auth_secret_use EQUAL -1)
  message(FATAL_ERROR "Encrypted-LeaseSet decryption ignores the BlindingInfo auth material")
endif()

# Application-facing services: proxy output must never reflect a hostile
# Host or a configured outproxy credential, only http(s) is proxied,
# responses are sanitized, the I2PControl local socket is private, NTP
# replies are validated and agreed, reseed is cancellable and never
# deletes peers from a bundle, the address book writes atomically, and
# the new administrative verbs stay typed.
file(READ "${PROJECT_ROOT}/libi2pd_client/HTTPProxy.h" http_proxy_header)
file(READ "${PROJECT_ROOT}/libi2pd_client/HTTPProxy.cpp" http_proxy_source)
file(READ "${PROJECT_ROOT}/libi2pd_client/SOCKS.h" socks_header)
file(READ "${PROJECT_ROOT}/libi2pd_client/I2PTunnelAccessFilter.h" access_filter_header)
file(READ "${PROJECT_ROOT}/libi2pd/Streaming.h" streaming_admission_header)
file(READ "${PROJECT_ROOT}/libi2pd/Reseed.h" reseed_header)
file(READ "${PROJECT_ROOT}/libi2pd/Reseed.cpp" reseed_source)
file(READ "${PROJECT_ROOT}/libi2pd/NetDb.hpp" netdb_header)
file(READ "${PROJECT_ROOT}/libi2pd_client/AddressBook.h" addressbook_header)
file(READ "${PROJECT_ROOT}/libi2pd_client/AddressBook.cpp" addressbook_source2)
file(READ "${PROJECT_ROOT}/daemon/I2PControl.h" i2pcontrol_header)
file(READ "${PROJECT_ROOT}/daemon/I2PControlHandlers.h" i2pcontrol_handlers_header)
file(READ "${PROJECT_ROOT}/daemon/UPnP.h" upnp_header)
file(READ "${PROJECT_ROOT}/libi2pd/Timestamp.cpp" timestamp_source)
file(READ "${PROJECT_ROOT}/libi2pd/TuiProtocol.h" tui_protocol_header)

foreach(required IN ITEMS
    "EscapeHTML"
    "ClassifyRequestTarget"
    "StripHopByHopHeaders"
    "ProxyDigestAuth"
    "HTTP_PROXY_UPSTREAM_TIMEOUT")
  string(FIND "${http_proxy_header}" "${required}" position)
  if(position EQUAL -1)
    message(FATAL_ERROR "Missing proxy hardening contract: ${required}")
  endif()
endforeach()
string(REGEX MATCH "m_ProxyURL[^\n]*to_string[^\n]*(page|response|WriteResponse)" http_proxy_url_leak "${http_proxy_source}")
if(http_proxy_url_leak)
  message(FATAL_ERROR "HTTP proxy echoes its configured outproxy URL to a client")
endif()

foreach(required IN ITEMS
    "SocksResolveCache"
    "SOCKS5UdpAssociateReply"
    "SocksFakeIPv4Address")
  string(FIND "${socks_header}" "${required}" position)
  if(position EQUAL -1)
    message(FATAL_ERROR "Missing SOCKS resolve/reply contract: ${required}")
  endif()
endforeach()

foreach(required IN ITEMS
    "FilterDefinition"
    "TunnelAccessFilter"
    "ParseFilterTarget")
  string(FIND "${access_filter_header}" "${required}" position)
  if(position EQUAL -1)
    message(FATAL_ERROR "Missing server-tunnel access filter contract: ${required}")
  endif()
endforeach()
foreach(required IN ITEMS
    "StreamAdmission"
    "AdmissionPredicate"
    "SetAdmissionPredicate")
  string(FIND "${streaming_admission_header}" "${required}" position)
  if(position EQUAL -1)
    message(FATAL_ERROR "Missing streaming pre-accept admission contract: ${required}")
  endif()
endforeach()

foreach(required IN ITEMS
    "ReseedState"
    "ReseedPhase"
    "RedactReseedProxyForLog")
  string(FIND "${reseed_header}" "${required}" position)
  if(position EQUAL -1)
    message(FATAL_ERROR "Missing reseed state/redaction contract: ${required}")
  endif()
endforeach()
string(FIND "${reseed_source}" "ResetRemoteRouterInfos" reseed_reset)
if(NOT reseed_reset EQUAL -1)
  message(FATAL_ERROR "Reseed still deletes remote RouterInfos from a downloaded bundle")
endif()
foreach(required IN ITEMS
    "RequestReseed"
    "GetReseedState")
  string(FIND "${netdb_header}" "${required}" position)
  if(position EQUAL -1)
    message(FATAL_ERROR "Missing non-blocking reseed contract: ${required}")
  endif()
endforeach()

foreach(required IN ITEMS
    "AddressAddResult"
    "AddAddressChecked"
    "NegativeAddressCache")
  string(FIND "${addressbook_header}" "${required}" position)
  if(position EQUAL -1)
    message(FATAL_ERROR "Missing address-book contract: ${required}")
  endif()
endforeach()
string(REGEX MATCH "std::ofstream[^:]" addressbook_ofstream "${addressbook_source2}")
if(addressbook_ofstream)
  message(FATAL_ERROR "Address book still writes through a non-atomic ofstream")
endif()
string(FIND "${addressbook_source2}" "AtomicWrite" addressbook_atomic)
if(addressbook_atomic EQUAL -1)
  message(FATAL_ERROR "Address book does not use the atomic writer")
endif()

foreach(required IN ITEMS
    "RestrictSocketPath"
    "IsSameUserPeer"
    "HAS_PEER_CREDENTIALS")
  string(FIND "${i2pcontrol_header}" "${required}" position)
  if(position EQUAL -1)
    message(FATAL_ERROR "Missing I2PControl local-socket contract: ${required}")
  endif()
endforeach()
foreach(required IN ITEMS
    "AppendQuoted"
    "I2P_CONTROL_ERROR_INVALID_PASSWORD")
  string(FIND "${i2pcontrol_handlers_header}" "${required}" position)
  if(position EQUAL -1)
    message(FATAL_ERROR "Missing I2PControl JSON/error contract: ${required}")
  endif()
endforeach()
foreach(required IN ITEMS
    "FormatUPnPLeaseDuration"
    "IsSameHostIGD")
  string(FIND "${upnp_header}" "${required}" position)
  if(position EQUAL -1)
    message(FATAL_ERROR "Missing UPnP mapping contract: ${required}")
  endif()
endforeach()
foreach(required IN ITEMS
    "NTP_MAX_ROOT_DELAY_SECONDS"
    "NTPOffsetAgreement")
  string(FIND "${timestamp_source}" "${required}" position)
  if(position EQUAL -1)
    message(FATAL_ERROR "Missing NTP reply validation contract: ${required}")
  endif()
endforeach()

foreach(required IN ITEMS
    "AdminTunnelStart"
    "AdminAddressReplace"
    "TUNNEL-START")
  string(FIND "${tui_protocol_header}" "${required}" position)
  if(position EQUAL -1)
    message(FATAL_ERROR "Missing typed administrative verb: ${required}")
  endif()
endforeach()

# Privilege-separation invariants: a worker role is bound to a
# spawn-time identity, the data-storage worker owns hash-named artifacts
# with no network capability, the core never falls back to a local write
# when delegation is enabled, and the capability matrix forbids a role
# with both external network and persistent-write authority.
file(READ "${PROJECT_ROOT}/privsep/Capabilities.h" privsep_capabilities)
file(READ "${PROJECT_ROOT}/privsep/Worker.cpp" privsep_worker_source)
file(READ "${PROJECT_ROOT}/privsep/RunWorker.cpp" privsep_runworker_source)
file(READ "${PROJECT_ROOT}/privsep/DataStorage.h" datastorage_header)
file(READ "${PROJECT_ROOT}/privsep/DataStorage.cpp" datastorage_source)
file(READ "${PROJECT_ROOT}/libi2pd/PersistentStorage.h" persistent_storage_header)
file(READ "${PROJECT_ROOT}/libi2pd/PersistentStorage.cpp" persistent_storage_source)
file(READ "${PROJECT_ROOT}/privsep/LogWorker.cpp" privsep_log_source)
file(READ "${PROJECT_ROOT}/privsep/LogWorker.h" privsep_log_header)
file(READ "${PROJECT_ROOT}/privsep/Supervisor.h" privsep_supervisor_header)

foreach(required IN ITEMS
    "ExternalNetwork"
    "PersistentWrite"
    "IsRoleCapabilitySafe"
    "ValidateCapabilityMatrix"
    "CORE_TARGET_CAPABILITIES"
    "CORE_LEGACY_CAPABILITIES")
  string(FIND "${privsep_capabilities}" "${required}" position)
  if(position EQUAL -1)
    message(FATAL_ERROR "Missing capability contract: ${required}")
  endif()
endforeach()

foreach(required IN ITEMS
    "CreateWorkerHello"
    "ReadWorkerHello"
    "InstallWorkerHelloFd")
  string(FIND "${privsep_worker_source}${privsep_runworker_source}" "${required}" position)
  if(position EQUAL -1)
    message(FATAL_ERROR "Missing spawn-bound worker identity contract: ${required}")
  endif()
endforeach()

foreach(required IN ITEMS
    "IsCanonicalArtifactName"
    "AcceptsArtifactTarget"
    "DataStorageClient")
  string(FIND "${datastorage_header}" "${required}" position)
  if(position EQUAL -1)
    message(FATAL_ERROR "Missing data-storage contract: ${required}")
  endif()
endforeach()
foreach(required IN ITEMS
    "StorageClientRestart"
    "IsCurrentDataStorageReply"
    "ArtifactRelativePath")
  string(FIND "${datastorage_source}" "${required}" position)
  if(position EQUAL -1)
    message(FATAL_ERROR "Missing data-storage worker contract: ${required}")
  endif()
endforeach()
# The storage worker must never gain external-network capability.
string(REGEX MATCH "AF_INET|SOCK_STREAM|::connect[[:space:]]*\\(" datastorage_network "${datastorage_source}")
if(datastorage_network)
  message(FATAL_ERROR "Data-storage worker references an external-network primitive")
endif()

foreach(required IN ITEMS
    "StorageArtifact"
    "WriteArtifact"
    "RemoveArtifact"
    "IsDirectArtifactFallbackAllowed"
    "AreArtifactWritesDelegated")
  string(FIND "${persistent_storage_header}" "${required}" position)
  if(position EQUAL -1)
    message(FATAL_ERROR "Missing artifact delegation contract: ${required}")
  endif()
endforeach()
string(REGEX MATCH "installed[^\n]*fallback|fallback[^\n]*installed" persistent_storage_fallback "${persistent_storage_source}")
if(persistent_storage_fallback)
  message(FATAL_ERROR "Artifact storage still falls back to a local write after a worker failure")
endif()

foreach(required IN ITEMS
    "RedactLogSecrets"
    "SanitizeLogText")
  string(FIND "${privsep_log_source}" "${required}" position)
  if(position EQUAL -1)
    message(FATAL_ERROR "Missing log hardening contract: ${required}")
  endif()
endforeach()
foreach(required IN ITEMS
    "MAX_LOG_SINK_BYTES"
    "FitsLogSinkBudget")
  string(FIND "${privsep_log_header}" "${required}" position)
  if(position EQUAL -1)
    message(FATAL_ERROR "Missing log sink bound contract: ${required}")
  endif()
endforeach()
foreach(required IN ITEMS
    "SupervisorStatus"
    "LogWorkerRestartBudget"
    "ClassifySupervisorReply")
  string(FIND "${privsep_supervisor_header}" "${required}" position)
  if(position EQUAL -1)
    message(FATAL_ERROR "Missing supervisor lifecycle contract: ${required}")
  endif()
endforeach()

# IPC consolidation: one message registry is the only
# role/limit/version/ descriptor authority, a serving endpoint answers
# attributable frame faults instead of dropping the connection, the
# descriptor policy is enforced, and a worker cannot be killed by an
# idle Windows pipe.
file(READ "${PROJECT_ROOT}/privsep/Protocol.h" protocol_header)
file(READ "${PROJECT_ROOT}/privsep/Channel.h" channel_header)
file(READ "${PROJECT_ROOT}/privsep/Channel.cpp" channel_source)
file(READ "${PROJECT_ROOT}/privsep/WindowsIpc.cpp" windows_ipc_source)
file(READ "${PROJECT_ROOT}/privsep/LogWorker.cpp" log_worker_source)

foreach(required IN ITEMS
    "struct MessageSpec"
    "kMessageSpecs"
    "ValidateMessageRegistry"
    "AllowsOperation"
    "DescriptorPolicyFor"
    "DescriptorPresenceAllowed"
    "IsRequestType"
    "IsReplyType"
    "InspectFrame"
    "IsRecoverableFrameFault"
    "EncodeCancel"
    "DecodeCancel")
  string(FIND "${protocol_header}" "${required}" position)
  if(position EQUAL -1)
    message(FATAL_ERROR "Missing IPC registry contract: ${required}")
  endif()
endforeach()

# The hand-written role switch must not return.
string(REGEX MATCH "switch \\(role\\)[^}]*case EndpointRole::Supervisor" legacy_allowlist "${protocol_header}")
if(legacy_allowlist)
  message(FATAL_ERROR "The per-role allowlist is duplicated instead of derived from the registry")
endif()

foreach(required IN ITEMS
    "EnforcementPolicy"
    "MAX_PROTOCOL_VIOLATIONS"
    "RejectFrame")
  string(FIND "${channel_header}" "${required}" position)
  if(position EQUAL -1)
    message(FATAL_ERROR "Missing endpoint enforcement contract: ${required}")
  endif()
endforeach()
foreach(required IN ITEMS
    "ValidateRegistryDescriptor"
    "IsRecoverableFrameFault"
    "InspectFrame")
  string(FIND "${channel_source}" "${required}" position)
  if(position EQUAL -1)
    message(FATAL_ERROR "Missing channel enforcement implementation: ${required}")
  endif()
endforeach()

# An idle receive timeout must not close the Windows pipe.
string(REGEX MATCH "ERROR_TIMEOUT\\)[^;]*Close \\(\\)" windows_timeout_close "${windows_ipc_source}")
if(windows_timeout_close)
  message(FATAL_ERROR "Windows receive closes the pipe on a receive timeout")
endif()
foreach(required IN ITEMS
    "IsRecoverableFrameFault"
    "MAX_WINDOWS_PROTOCOL_VIOLATIONS"
    "AllowsOperation (EndpointRole::Log" )
  string(FIND "${windows_ipc_source}" "${required}" position)
  if(position EQUAL -1)
    message(FATAL_ERROR "Missing Windows IPC parity contract: ${required}")
  endif()
endforeach()

# A pre-opened log descriptor must be bound to the configured path.
foreach(required IN ITEMS
    "ValidateLogDescriptorIdentity"
    "duplicate log configuration"
    "SetEnforcementPolicy")
  string(FIND "${log_worker_source}" "${required}" position)
  if(position EQUAL -1)
    message(FATAL_ERROR "Missing log capability contract: ${required}")
  endif()
endforeach()

# OpenBSD supervised start: credentials, pre-opened descriptors, the
# frozen executable, startup/runtime pledge consistency and the
# credential gate.
file(READ "${PROJECT_ROOT}/privsep/Credentials.h" credentials_header)
file(READ "${PROJECT_ROOT}/privsep/Credentials.cpp" credentials_source)
file(READ "${PROJECT_ROOT}/privsep/Worker.cpp" worker_source)
file(READ "${PROJECT_ROOT}/privsep/OpenBSDSandbox.cpp" openbsd_sandbox_source)
file(READ "${PROJECT_ROOT}/privsep/OpenBSDSandbox.h" openbsd_sandbox_header)
file(READ "${PROJECT_ROOT}/privsep/Supervisor.cpp" supervisor_source)
file(READ "${PROJECT_ROOT}/daemon/Daemon.cpp" daemon_source)
file(READ "${PROJECT_ROOT}/daemon/UnixDaemon.cpp" unixdaemon_source)

foreach(required IN ITEMS
    "ResolveWorkerCredentials"
    "DropWorkerCredentialsInChild"
    "WorkerUid"
    "IsAllowedParentUid")
  string(FIND "${credentials_header}" "${required}" position)
  if(position EQUAL -1)
    message(FATAL_ERROR "Missing worker credential contract: ${required}")
  endif()
endforeach()
foreach(required IN ITEMS
    "setgroups (0, nullptr)"
    "setgid"
    "setuid"
    "getgroups (0, nullptr)")
  string(FIND "${credentials_source}" "${required}" position)
  if(position EQUAL -1)
    message(FATAL_ERROR "Missing credential-drop verification: ${required}")
  endif()
endforeach()

foreach(required IN ITEMS
    "SetExecutablePath"
    "PinStandardDescriptors"
    "WORKER_HELLO_SIZE"
    "DropWorkerCredentialsInChild")
  string(FIND "${worker_source}" "${required}" position)
  if(position EQUAL -1)
    message(FATAL_ERROR "Missing supervised spawn contract: ${required}")
  endif()
endforeach()
# The executable must never be resolved through PATH again.
string(REGEX MATCH "getenv[^\\n]*PATH|/proc/self/exe" worker_path_lookup "${worker_source}")
if(worker_path_lookup)
  message(FATAL_ERROR "Worker executable resolution still trusts PATH or /proc")
endif()

foreach(required IN ITEMS
    "OPENBSD_PLEDGE_EXECPROMISES"
    "IsPledgePromiseSubset"
    "ParseOpenBSDPledgeFile"
    "OpenBSDPledgePhase")
  string(FIND "${openbsd_sandbox_source}" "${required}" position)
  if(position EQUAL -1)
    message(FATAL_ERROR "Missing OpenBSD pledge-window contract: ${required}")
  endif()
endforeach()
# No execpromises string may ever be supplied: the exec'ed image must
# reapply its own unveil and pledge policy. The only accepted second
# argument is the compile-time null constant declared in the header.
string(REGEX MATCH "pledge \\([^,]+,[[:space:]]*\"" openbsd_execpromises "${openbsd_sandbox_source}")
if(openbsd_execpromises)
  message(FATAL_ERROR "A pledge call passes an execpromises literal")
endif()
string(FIND "${openbsd_sandbox_header}" "OPENBSD_PLEDGE_EXECPROMISES = nullptr" openbsd_execpromises_null)
if(openbsd_execpromises_null EQUAL -1)
  message(FATAL_ERROR "The execpromises constant is no longer a null pointer")
endif()

foreach(required IN ITEMS
    "ForbidProcessCreation"
    "policyConfig.executablePath"
    "IsAllowedParentUid")
  string(FIND "${supervisor_source}" "${required}" position)
  if(position EQUAL -1)
    message(FATAL_ERROR "Missing supervisor start contract: ${required}")
  endif()
endforeach()

foreach(required IN ITEMS
    "PreparePrivsepIdentity"
    "ResolveWorkerCredentials"
    "worker isolation requires an unprivileged identity")
  string(FIND "${daemon_source}" "${required}" position)
  if(position EQUAL -1)
    message(FATAL_ERROR "Missing credential/root gate: ${required}")
  endif()
endforeach()
foreach(required IN ITEMS
    "ForbidProcessCreation ()"
    "IsPledgePromiseSubset")
  string(FIND "${unixdaemon_source}" "${required}" position)
  if(position EQUAL -1)
    message(FATAL_ERROR "Missing runtime pledge gate: ${required}")
  endif()
endforeach()

# Linux backend hardening: explicit per-frame peer expectation
# (SO_PEERCRED is stale after a credential drop), capability teardown
# while still privileged, seccomp connection/exec/process denial,
# Landlock EXECUTE handling, early pre-config lockdown and role/root
# pairing.
file(READ "${PROJECT_ROOT}/privsep/Channel.h" channel_header_linux)
file(READ "${PROJECT_ROOT}/privsep/Channel.cpp" channel_source_linux)
file(READ "${PROJECT_ROOT}/privsep/Worker.cpp" worker_source_linux)
file(READ "${PROJECT_ROOT}/privsep/LinuxSandbox.cpp" linux_sandbox_source)
file(READ "${PROJECT_ROOT}/privsep/LinuxSandbox.h" linux_sandbox_header)

foreach(required IN ITEMS
    "PeerExpectation"
    "ExpectPeer"
    "MatchesPeerExpectation"
    "allowRoot")
  string(FIND "${channel_header_linux}" "${required}" position)
  if(position EQUAL -1)
    message(FATAL_ERROR "Missing Linux peer-expectation contract: ${required}")
  endif()
endforeach()
foreach(required IN ITEMS
    "MatchesPeerExpectation"
    "SCM_CREDENTIALS do not match the expected peer"
    "adopted channel has a pending record")
  string(FIND "${channel_source_linux}" "${required}" position)
  if(position EQUAL -1)
    message(FATAL_ERROR "Missing Linux channel hardening: ${required}")
  endif()
endforeach()

foreach(required IN ITEMS
    "PrepareCapabilitiesBeforeCredentialDrop"
    "ClearCapabilitiesAfterCredentialDrop"
    "WorkerRoleTakesInheritedRoot"
    "frozenStatus.st_ino"
    "SYS_unshare")
  string(FIND "${worker_source_linux}" "${required}" position)
  if(position EQUAL -1)
    message(FATAL_ERROR "Missing Linux spawn hardening: ${required}")
  endif()
endforeach()

foreach(required IN ITEMS
    "PrepareCapabilitiesBeforeCredentialDrop"
    "ClearCapabilitiesAfterCredentialDrop"
    "ApplyLinuxIpcLockdown"
    "VerifyWorkerPrivileges")
  string(FIND "${linux_sandbox_header}" "${required}" position)
  if(position EQUAL -1)
    message(FATAL_ERROR "Missing Linux sandbox helper: ${required}")
  endif()
endforeach()
foreach(required IN ITEMS
    "LANDLOCK_ACCESS_FS_EXECUTE"
    "SYS_connect"
    "SYS_bind"
    "SYS_execve"
    "SYS_clone"
    "restrictFilesystemReads = true"
    "g_SeccompInstalled")
  string(FIND "${linux_sandbox_source}" "${required}" position)
  if(position EQUAL -1)
    message(FATAL_ERROR "Missing Linux confinement contract: ${required}")
  endif()
endforeach()

# Storage must install the socket boundary before parsing its
# configuration.
file(READ "${PROJECT_ROOT}/privsep/DataStorage.cpp" datastorage_source_linux)
string(FIND "${datastorage_source_linux}" "ApplyLinuxIpcLockdown (GetLinuxSandboxProfile (WorkerClass::Storage))" datastorage_early)
if(datastorage_early EQUAL -1)
  message(FATAL_ERROR "Data storage parses the configure frame before the Linux socket lockdown")
endif()

# Windows backend: the shared WorkerHello spawn proof, accept-time
# identity binding, AppContainer role authority, per-role IPC, Job
# Object/supervisor lifecycle, service drain and the no-raw-ALPC rule.
file(READ "${PROJECT_ROOT}/privsep/WindowsIpc.cpp" windows_ipc_source)
file(READ "${PROJECT_ROOT}/privsep/WindowsIpc.h" windows_ipc_header)
file(READ "${PROJECT_ROOT}/privsep/WindowsIsolation.h" windows_isolation_header)
file(READ "${PROJECT_ROOT}/privsep/WindowsSupervisor.h" windows_supervisor_header)
file(READ "${PROJECT_ROOT}/Win32/Win32Service.cpp" windows_service_source)

foreach(required IN ITEMS
    "WorkerHello"
    "ImpersonateNamedPipeClient"
    "TokenIsAppContainer"
    "PROC_THREAD_ATTRIBUTE_SECURITY_CAPABILITIES"
    "GrantAppContainerPath"
    "TerminateJobObject")
  string(FIND "${windows_ipc_source}" "${required}" position)
  if(position EQUAL -1)
    message(FATAL_ERROR "Missing Windows IPC hardening: ${required}")
  endif()
endforeach()
foreach(required IN ITEMS
    "SetRole"
    "SpawnWindowsWorker"
    "AcceptBound"
    "WindowsAppContainer")
  string(FIND "${windows_ipc_header}" "${required}" position)
  if(position EQUAL -1)
    message(FATAL_ERROR "Missing Windows channel/identity contract: ${required}")
  endif()
endforeach()
foreach(required IN ITEMS
    "GetWindowsRoleIsolation"
    "CreateWindowsAppContainer"
    "GrantAppContainerPath"
    "WindowsAppContainer")
  string(FIND "${windows_isolation_header}" "${required}" position)
  if(position EQUAL -1)
    message(FATAL_ERROR "Missing Windows isolation contract: ${required}")
  endif()
endforeach()
foreach(required IN ITEMS
    "WindowsWorkerSupervisor"
    "LogWorkerRestartBudget"
    "SupervisorStatus")
  string(FIND "${windows_supervisor_header}" "${required}" position)
  if(position EQUAL -1)
    message(FATAL_ERROR "Missing Windows supervisor contract: ${required}")
  endif()
endforeach()
foreach(required IN ITEMS
    "WINDOWS_SERVICE_STOP_WAIT_HINT_MS"
    "Daemon.stop")
  string(FIND "${windows_service_source}" "${required}" position)
  if(position EQUAL -1)
    message(FATAL_ERROR "Missing Windows service drain contract: ${required}")
  endif()
endforeach()
# No undocumented ALPC/ntdll port API may enter the internal IPC.
string(REGEX MATCH "NtCreatePort|NtConnectPort|NtRequestWaitReplyPort|RtlCreateBoundaryDescriptor|RPC Control"
	windows_alpc "${windows_ipc_source}${windows_ipc_header}")
if(windows_alpc)
  message(FATAL_ERROR "Windows IPC references a raw ALPC/ntdll port API")
endif()

# Android daemon lifecycle/process-role hardening: exactly one
# irreversible process-role claim per process, a lock-free
# start-cancellation token that a start observes while it owns the
# native guard, a NewString boundary that never receives a null jchar
# pointer, cleared JNI exceptions on the string/array failure paths, and
# SAF destructors that never attach a thread or push a local frame.
file(READ "${PROJECT_ROOT}/android/app/src/main/cpp/DaemonAndroid.h" android_daemon_header)
file(READ "${PROJECT_ROOT}/android/app/src/main/cpp/DaemonAndroid.cpp" android_daemon_source)
file(READ "${PROJECT_ROOT}/android/app/src/main/java/org/openi2pd/router/DaemonWrapper.java" android_daemon_wrapper)
file(READ "${PROJECT_ROOT}/android/app/src/main/java/org/openi2pd/router/I2PD_JNI.java" android_jni_java)

foreach(required IN ITEMS
    "enum class ProcessRole : int"
    "ClaimProcessRole (ProcessRole role) noexcept"
    "PROCESS_ROLE_CONFLICT = \"process-role-conflict\""
    "START_CANCELED_RESULT = \"start-canceled\""
    "void RequestStartCancel () noexcept;")
  string(FIND "${android_daemon_header}" "${required}" position)
  if(position EQUAL -1)
    message(FATAL_ERROR "Missing Android process-role/cancel contract: ${required}")
  endif()
endforeach()

foreach(required IN ITEMS
    "std::atomic<bool> startCancelRequested { false }"
    "startCancelRequested.store (false, std::memory_order_release)"
    "IsStartCancelRequested ()"
    "START_CANCELED_RESULT")
  string(FIND "${android_daemon_source}" "${required}" position)
  if(position EQUAL -1)
    message(FATAL_ERROR "Missing native start-cancellation contract: ${required}")
  endif()
endforeach()

# The daemon start must require the router role, the SAF bridge
# installation must require it as well, and the broker sandbox must
# claim the broker role before installing anything. A conflict never
# starts a router.
string(REGEX MATCH
  "Java_org_openi2pd_router_I2PD_1JNI_startDaemon[^}]*HasProcessRole \\(i2p::android::ProcessRole::Router\\)[^}]*i2p::android::start \\(\\)"
  android_start_role "${android_jni}")
if(android_start_role STREQUAL "")
  message(FATAL_ERROR "startDaemon does not require the router process role before starting")
endif()
string(REGEX MATCH
  "Java_org_openi2pd_router_I2PD_1JNI_installSafStorageBridge[^}]*HasProcessRole \\(i2p::android::ProcessRole::Router\\)"
  android_bridge_role "${android_jni}")
if(android_bridge_role STREQUAL "")
  message(FATAL_ERROR "SAF bridge installation does not require the router process role")
endif()
string(REGEX MATCH
  "Java_org_openi2pd_router_I2PD_1JNI_restrictSafStorageProcess[^}]*ClaimProcessRole \\(i2p::android::ProcessRole::StorageBroker\\)"
  android_broker_role "${android_jni}")
if(android_broker_role STREQUAL "")
  message(FATAL_ERROR "restrictSafStorageProcess does not claim the storage-broker role")
endif()
string(FIND "${android_jni}" "i2p::android::CurrentProcessRole () != i2p::android::ProcessRole::Router"
  android_saf_role_gate)
if(android_saf_role_gate EQUAL -1)
  message(FATAL_ERROR "SAF document callbacks are not gated on the router process role")
endif()

# A zero-length NewString must never receive a null jchar pointer.
string(REGEX MATCH "NewString \\([^)]*nullptr" android_null_newstring "${android_jni}")
if(NOT android_null_newstring STREQUAL "")
  message(FATAL_ERROR "NewString receives a null-capable jchar pointer")
endif()
string(FIND "${android_jni}" "utf16.empty () ? &kEmptyUtf16 : utf16.data ()" android_empty_utf16)
if(android_empty_utf16 EQUAL -1)
  message(FATAL_ERROR "The empty UTF-16 NewString case lost its stable element")
endif()
# The VM-owned UTF-16 buffer is released through an RAII owner on every
# path, and a GetStringChars failure clears the pending exception before
# returning.
string(FIND "${android_jni}" "ScopedJStringChars chars (env, text);" android_string_raii)
if(android_string_raii EQUAL -1)
  message(FATAL_ERROR "Utf8FromJavaString does not use the RAII UTF-16 owner")
endif()
string(REGEX MATCH "if \\(!chars\\)[^{]*\\{[^}]*ExceptionClear" android_string_clear
  "${android_jni}")
if(android_string_clear STREQUAL "")
  message(FATAL_ERROR "Utf8FromJavaString returns a fallback with a pending JNI exception")
endif()

# requestStop sets one atomic and must never wait for the native-call
# guard.
string(REGEX MATCH "Java_org_openi2pd_router_I2PD_1JNI_requestStopNative[^}]*NativeCallGuard"
  android_requeststop_guard "${android_jni}")
if(NOT android_requeststop_guard STREQUAL "")
  message(FATAL_ERROR "requestStop takes the native-call guard, so it cannot cancel a start")
endif()

# Java publishes the stop token before taking any monitor, and maps the
# distinct native cancellation result to the stopped state.
string(FIND "${android_daemon_wrapper}" "public synchronized void stopDaemon" android_stop_synchronized)
if(NOT android_stop_synchronized EQUAL -1)
  message(FATAL_ERROR "DaemonWrapper.stopDaemon is monitor-synchronized behind the native start")
endif()
string(FIND "${android_daemon_wrapper}" "stopRequested.set(true);" android_stop_token)
string(FIND "${android_daemon_wrapper}" "synchronized (stopLock)" android_stop_lock)
if(android_stop_token EQUAL -1)
  message(FATAL_ERROR "DaemonWrapper.stopDaemon does not publish its stop token")
endif()
if(android_stop_lock EQUAL -1)
  message(FATAL_ERROR "DaemonWrapper.stopDaemon does not serialise concurrent stop calls")
endif()
if(android_stop_lock LESS android_stop_token)
  message(FATAL_ERROR "DaemonWrapper.stopDaemon publishes its stop token after taking a monitor")
endif()
foreach(required IN ITEMS
    "I2PD_JNI.requestStop()"
    "I2PD_JNI.claimRouterProcessRole()"
    "I2PD_JNI.START_CANCELED")
  string(FIND "${android_daemon_wrapper}" "${required}" position)
  if(position EQUAL -1)
    message(FATAL_ERROR "Missing Android start/stop ordering contract: ${required}")
  endif()
endforeach()
foreach(required IN ITEMS
    "\"start-canceled\""
    "claimRouterProcessRole"
    "PROCESS_ROLE_CONFLICT")
  string(FIND "${android_jni_java}" "${required}" position)
  if(position EQUAL -1)
    message(FATAL_ERROR "Missing Java process-role/cancel contract: ${required}")
  endif()
endforeach()

# No SAF destructor may attach a thread, push a local frame or delete a
# JNI global reference: the bridge is a deliberate process-lifetime
# object and a payload falls back to an explicit detach on the attached
# JNI thread.
string(FIND "${android_jni}" "~SafBridge () = default;" android_bridge_destructor)
if(android_bridge_destructor EQUAL -1)
  message(FATAL_ERROR "SafBridge is not a process-lifetime object with a trivial destructor")
endif()
string(REGEX MATCH "~SafBridge \\(\\)[^{]*\\{[^}]*DeleteGlobalRef" android_bridge_dtor_jni "${android_jni}")
if(NOT android_bridge_dtor_jni STREQUAL "")
  message(FATAL_ERROR "The SafBridge destructor releases a JNI global reference")
endif()
string(REGEX MATCH "~SafPayload \\(\\) override[^{]*\\{[^}]*SafEnv" android_payload_dtor_jni "${android_jni}")
if(NOT android_payload_dtor_jni STREQUAL "")
  message(FATAL_ERROR "The SafPayload destructor enters a JNI scope")
endif()
string(REGEX MATCH "~SafPayload \\(\\) override[^{]*\\{[^}]*ReleaseWithoutAttach \\(\\);[^}]*\\}" android_payload_dtor
  "${android_jni}")
if(android_payload_dtor STREQUAL "")
  message(FATAL_ERROR "The SafPayload destructor is not delegated to the no-attach release path")
endif()
string(REGEX MATCHALL "AttachCurrentThread" android_attach_calls "${android_jni}")
list(LENGTH android_attach_calls android_attach_count)
if(NOT android_attach_count EQUAL 1)
  message(FATAL_ERROR "JNI thread attachment is duplicated outside the SAF call scope")
endif()
string(FIND "${android_jni}" "DetachSafPayloads ();" android_detach_call)
if(android_detach_call EQUAL -1)
  message(FATAL_ERROR "The daemon stop path does not detach SAF payloads")
endif()

# Android SAF broker protocol v2 and the durable router run intent.
# These are compile-free guards for contracts whose real compilation
# happens in the manual Android job; they catch an accidental
# protocol/state downgrade in the Linux static lane first.
file(READ "${PROJECT_ROOT}/android/app/src/main/aidl/org/openi2pd/router/ITorrentSafStorage.aidl"
  android_saf_aidl)
file(READ "${PROJECT_ROOT}/android/app/src/main/java/org/openi2pd/router/TorrentSafStorageService.java"
  android_saf_broker)
file(READ "${PROJECT_ROOT}/android/app/src/main/java/org/openi2pd/router/RouterRunState.java"
  android_run_state)
file(READ "${PROJECT_ROOT}/android/app/src/main/java/org/openi2pd/router/RouterService.java"
  android_router_service)
file(READ "${PROJECT_ROOT}/android/app/src/main/AndroidManifest.xml" android_manifest)

foreach(required IN ITEMS
    "const int API_VERSION = 2;"
    "long handshake();"
    "long prepareExport(String uri, long length, long operation);"
    "long openExport(long token, long operation);"
    "oneway void retire(long generation);")
  string(FIND "${android_saf_aidl}" "${required}" android_saf_contract)
  if(android_saf_contract EQUAL -1)
    message(FATAL_ERROR "SAF AIDL v2 contract is missing: ${required}")
  endif()
endforeach()
# A descriptor may only be mentioned in a comment, never in a
# declaration.
string(REGEX MATCH "\n[ \t]*[^/ \t][^\n]*ParcelFileDescriptor" android_saf_fd_export
  "${android_saf_aidl}")
if(NOT android_saf_fd_export STREQUAL "")
  message(FATAL_ERROR "The SAF AIDL must never export a file descriptor")
endif()
string(FIND "${android_saf_broker}" "new Caller(uid, Binder.getCallingPid())" android_saf_owner)
if(android_saf_owner EQUAL -1)
  message(FATAL_ERROR "The SAF broker does not bind capabilities to (uid, pid)")
endif()
string(FIND "${android_saf_broker}" "CANCEL_DEADLINE_MS" android_saf_cancel_stage)
string(FIND "${android_saf_broker}" "KILL_DEADLINE_MS" android_saf_kill_stage)
if(android_saf_cancel_stage EQUAL -1 OR android_saf_kill_stage EQUAL -1)
  message(FATAL_ERROR "The SAF broker lost its two-stage operation watchdog")
endif()
string(FIND "${android_saf_broker}" "Process.killProcess" android_saf_kill)
if(android_saf_kill EQUAL -1)
  message(FATAL_ERROR "The SAF broker no longer fences a stuck provider by process death")
endif()

foreach(required IN ITEMS
    "run_phase"
    "graceful_deadline_epoch_ms"
    "run_generation"
    "should_run")
  string(FIND "${android_run_state}" "${required}" android_run_key)
  if(android_run_key EQUAL -1)
    message(FATAL_ERROR "RouterRunState is missing its persisted key: ${required}")
  endif()
endforeach()
foreach(required IN ITEMS
    "reconcilePersistedRunState"
    "pendingDrainResumeMillis"
    "onTaskRemoved"
    "removeCallbacksAndMessages(null)")
  string(FIND "${android_router_service}" "${required}" android_run_service)
  if(android_run_service EQUAL -1)
    message(FATAL_ERROR "RouterService is missing its lifecycle contract: ${required}")
  endif()
endforeach()
string(REGEX MATCH "android:name=\"[^\"]*RouterService\"[^>]*android:stopWithTask=\"false\""
  android_router_stop_with_task "${android_manifest}")
if(android_router_stop_with_task STREQUAL "")
  message(FATAL_ERROR "RouterService must declare android:stopWithTask=\"false\"")
endif()

# Native TUI/daemon separation and console-lifecycle contracts. These
# are compile-free text pins for behavior that otherwise needs a
# terminal/runner.
file(READ "${PROJECT_ROOT}/tui/main.cpp" tui_main)
file(READ "${PROJECT_ROOT}/tui/TerminalWin32.cpp" tui_terminal_win32)
file(READ "${PROJECT_ROOT}/daemon/Daemon.cpp" daemon_main)
file(READ "${PROJECT_ROOT}/daemon/TuiServer.cpp" tui_server_unix)
file(READ "${PROJECT_ROOT}/daemon/TuiServerWin32.cpp" tui_server_win32)
foreach(required IN ITEMS
    "DecodeUtf8"
    "SanitizeText"
    "m_StateLoaded"
    "m_DestinationsAux"
    "HandleConsoleEvent"
    "case 'q':")
  string(FIND "${tui_main}" "${required}" tui_contract)
  if(tui_contract EQUAL -1)
    message(FATAL_ERROR "TUI is missing its navigation/Unicode/console contract: ${required}")
  endif()
endforeach()
string(FIND "${daemon_main}" "std::cout << unit" daemon_stdout_leak)
if(NOT daemon_stdout_leak EQUAL -1)
  message(FATAL_ERROR "Daemon init writes a stray character to stdout")
endif()
string(FIND "${tui_server_unix}" "minutes (30)" tui_server_duration)
if(tui_server_duration EQUAL -1)
  message(FATAL_ERROR "Unix TUI server connection budget is not the documented 30 minutes")
endif()
string(FIND "${tui_server_win32}" "minutes (30)" tui_server_win32_duration)
if(tui_server_win32_duration EQUAL -1)
  message(FATAL_ERROR "Windows TUI server connection budget is not the documented 30 minutes")
endif()
string(FIND "${tui_terminal_win32}" "VK_CANCEL" tui_win32_cancel)
if(tui_win32_cancel EQUAL -1)
  message(FATAL_ERROR "Windows terminal does not map VK_CANCEL to the stop path")
endif()

/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: Streaming.
 *
 * Declares the I2P streaming layer: packet flags and MTU limits, asynchronous
 * send buffers, the per-connection Stream state (sequence/ACK, RTT, window, RTO,
 * resend and pacing) and StreamingDestination for accepting, pooling and
 * dispatching streams. Receive timeouts are capped by MAX_RECEIVE_TIMEOUT.
 *
 */

#ifndef STREAMING_H__
#define STREAMING_H__

#include <inttypes.h>
#include <atomic>
#include <condition_variable>
#include <cstring>
#include <string>
#include <unordered_map>
#include <set>
#include <vector>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <chrono>
#include <list>
#include <utility>
#include <boost/asio.hpp>
#include "Base.h"
#include "Gzip.h"
#include "I2PEndian.h"
#include "Identity.h"
#include "LeaseSet.h"
#include "I2NPProtocol.h"
#include "Garlic.h"
#include "Tunnel.h"
#include "util.h" // MemoryPool
#include "StreamingThrottle.h"
#include "ECIESX25519AEADRatchetSession.h"

namespace i2p
{
namespace client
{
	class ClientDestination;
}
namespace stream
{
	const uint16_t PACKET_FLAG_SYNCHRONIZE = 0x0001;
	const uint16_t PACKET_FLAG_CLOSE = 0x0002;
	const uint16_t PACKET_FLAG_RESET = 0x0004;
	const uint16_t PACKET_FLAG_SIGNATURE_INCLUDED = 0x0008;
	const uint16_t PACKET_FLAG_SIGNATURE_REQUESTED = 0x0010;
	const uint16_t PACKET_FLAG_FROM_INCLUDED = 0x0020;
	const uint16_t PACKET_FLAG_DELAY_REQUESTED = 0x0040;
	const uint16_t PACKET_FLAG_MAX_PACKET_SIZE_INCLUDED = 0x0080;
	const uint16_t PACKET_FLAG_PROFILE_INTERACTIVE = 0x0100;
	const uint16_t PACKET_FLAG_ECHO = 0x0200;
	const uint16_t PACKET_FLAG_NO_ACK = 0x0400;
	const uint16_t PACKET_FLAG_OFFLINE_SIGNATURE = 0x0800;

	const size_t STREAMING_MTU = 1730;
	const size_t STREAMING_MTU_RATCHETS = 1812;
	const size_t STREAMING_MIN_MTU = 512; // Java ConnectionOptions.MIN_MESSAGE_SIZE
	const size_t MAX_PACKET_SIZE = 3072;

	const size_t COMPRESSION_THRESHOLD_SIZE = 66;
	const int MAX_NUM_RESEND_ATTEMPTS = 10;
	const int INITIAL_WINDOW_SIZE = 10;
	const int MIN_WINDOW_SIZE = 3;
	const int MAX_WINDOW_SIZE = 512;
	const int MAX_WINDOW_SIZE_INC_PER_RTT = 512; // TODO: remove
	const double RTT_EWMA_ALPHA = 0.1;
	const double SLOWRTT_EWMA_ALPHA = 0.03;
	const double PREV_SPEED_KEEP_TIME_COEFF
		= 0.2; // 0.1 - 1 // how long will the window size stay around the previous drop level, less is longer
	const int MIN_RTO = 20; // in milliseconds
	const int INITIAL_RTT = 1500; // in milliseconds
	const int INITIAL_RTO = 9000; // in milliseconds
	const int INITIAL_PACING_TIME = 1000 * INITIAL_RTT / INITIAL_WINDOW_SIZE; // in microseconds
	const int MIN_SEND_ACK_TIMEOUT = 2; // in milliseconds
	const int SYN_TIMEOUT = 200; // how long we wait for SYN after follow-on, in milliseconds
	const size_t MAX_PENDING_INCOMING_BACKLOG = 1024;
	const size_t MAX_SAVED_STREAM_IDS = 512;
	const size_t MAX_SAVED_PACKETS_PER_STREAM = 64;
	// Java ConnectionManager keeps a bounded map of closed connections so a
	// retransmitted SYN cannot resurrect a connection the peer has not yet
	// noticed as gone. The window only has to outlive the peer's SYN retry
	// ladder; a bounded, timestamped table is enough.
	const size_t MAX_RECENTLY_CLOSED_STREAMS = 128;
	const uint64_t RECENTLY_CLOSED_TIMEOUT = 120; // in seconds
	const uint64_t SYN_REJECT_LOG_INTERVAL = 1000; // in milliseconds
	const int PENDING_INCOMING_TIMEOUT = 10; // in seconds
	const int MAX_RECEIVE_TIMEOUT = 90; // in seconds
	const uint16_t DELAY_CHOKING = 60000; // in milliseconds
	const uint16_t DELAY_CHOKING_JAVA = 61000; // in milliseconds
	const uint16_t DELAY_CHOKING_2 = 65535; // in milliseconds
	const uint16_t DELAY_CHOKING_3 = 65534; // in milliseconds
	const uint64_t SEND_INTERVAL = 5000; // in microseconds
	const uint64_t SEND_INTERVAL_VARIANCE = 1000; // in microseconds
	const uint64_t REQUEST_IMMEDIATE_ACK_INTERVAL = 7500; // in milliseconds
	const uint64_t REQUEST_IMMEDIATE_ACK_INTERVAL_VARIANCE = 3200; // in milliseconds
	const bool LOSS_BASED_CONTROL_ENABLED = 0; // 0/1
	const uint64_t STREAMING_DESTINATION_POOLS_CLEANUP_INTERVAL = 646; // in seconds
	const uint64_t STREAMING_CONNECTION_CLEANUP_INTERVAL = 60; // in seconds

	/// Buffered streaming wire packet. Header accessors assume the caller has
	/// checked len, the NACK count and option length before accessing fields.
	/// `from` is a borrowed ratchet session pointer. The destination-level queue
	/// clears it for packets saved before SYN; stream-level reordering requires
	/// separate lifetime review because it retains packets without clearing it.
	/// @see https://i2p.net/en/docs/specs/streaming/
	struct Packet
	{
		size_t len, offset;
		uint8_t buf[MAX_PACKET_SIZE];
		uint64_t sendTime;
		bool resent;
		i2p::garlic::ECIESX25519AEADRatchetSession * from;

		Packet ()
			: len (0)
			, offset (0)
			, sendTime (0)
			, resent (false)
			, from (nullptr) { };
		uint8_t * GetBuffer () { return buf + offset; };
		size_t GetLength () const { return len > offset ? len - offset : 0; };

		uint32_t GetSendStreamID () const { return bufbe32toh (buf); };
		uint32_t GetReceiveStreamID () const { return bufbe32toh (buf + 4); };
		uint32_t GetSeqn () const { return bufbe32toh (buf + 8); };
		uint32_t GetAckThrough () const { return bufbe32toh (buf + 12); };
		uint8_t GetNACKCount () const { return buf[16]; };
		uint32_t GetNACK (int i) const { return bufbe32toh (buf + 17 + 4 * i); };
		const uint8_t * GetNACKs () const { return buf + 17; };
		const uint8_t * GetOption () const { return buf + 17 + GetNACKCount () * 4 + 3; }; // 3 = resendDelay + flags
		uint16_t GetFlags () const { return bufbe16toh (GetOption () - 2); };
		uint16_t GetOptionSize () const { return bufbe16toh (GetOption ()); };
		const uint8_t * GetOptionData () const { return GetOption () + 2; };
		const uint8_t * GetPayload () const { return GetOptionData () + GetOptionSize (); };

		bool IsSYN () const { return GetFlags () & PACKET_FLAG_SYNCHRONIZE; };
		bool IsNoAck () const { return GetFlags () & PACKET_FLAG_NO_ACK; };
		bool IsEcho () const { return GetFlags () & PACKET_FLAG_ECHO; };
	};

	struct PacketCmp
	{
		bool operator() (const Packet * p1, const Packet * p2) const { return p1->GetSeqn () < p2->GetSeqn (); };
	};

#ifdef __cpp_lib_move_only_function // with C++23
	typedef std::move_only_function<void (const boost::system::error_code & ecode, size_t bytes_transferred)>
		SendHandler;
#else
	typedef std::function<void (const boost::system::error_code & ecode, size_t bytes_transferred)> SendHandler;
#endif

	/// Owns bytes and their completion handler while queued for streaming.
	/// Destruction invokes a remaining handler with success; an owner discarding
	/// unsent data must call Cancel() first. This reports local queue progress,
	/// not acknowledgment by the remote peer.
	struct SendBuffer
	{
		// Owns the payload for the whole asynchronous write. `buf` is a
		// non-owning view into m_Buffer kept for callers that fill the buffer
		// before queueing it (I2CP).
		std::unique_ptr<uint8_t[]> m_Buffer;
		uint8_t * buf;
		size_t len, offset;
		SendHandler handler;

		SendBuffer (const uint8_t * b, size_t l, SendHandler && h)
			: m_Buffer (std::make_unique_for_overwrite<uint8_t[]> (l))
			, buf (m_Buffer.get ())
			, len (l)
			, offset (0)
			, handler (std::move (h))
		{
			if (len) memcpy (buf, b, len);
		}
		SendBuffer (size_t l)
			: // create empty buffer
			m_Buffer (std::make_unique_for_overwrite<uint8_t[]> (l))
			, buf (m_Buffer.get ())
			, len (l)
			, offset (0)
		{
		}
		SendBuffer (const std::vector<std::pair<const uint8_t *, size_t>> & bufs, size_t totalLen, SendHandler && h);
		SendBuffer (std::span<const uint8_t> b, SendHandler && h)
			: SendBuffer (b.data (), b.size (), std::move (h))
		{
		}
		SendBuffer (const SendBuffer &) = delete;
		SendBuffer & operator= (const SendBuffer &) = delete;
		SendBuffer (SendBuffer &&) = delete;
		SendBuffer & operator= (SendBuffer &&) = delete;
		~SendBuffer ()
		{
			if (handler) handler (boost::system::error_code (), len);
		}
		size_t GetRemainingSize () const { return len - offset; };
		const uint8_t * GetRemaningBuffer () const { return buf + offset; };
		/// Complete the handler with operation_aborted and the consumed byte count.
		/// The handler is cleared so destruction does not report success as well.
		void Cancel ()
		{
			if (handler) handler (boost::asio::error::make_error_code (boost::asio::error::operation_aborted), offset);
			handler = nullptr;
		};
	};

	class SendBufferQueue
	{
	public:
		SendBufferQueue ()
			: m_Size (0) { };
		~SendBufferQueue () { CleanUp (); };

		void Add (std::shared_ptr<SendBuffer> && buf);
		size_t Get (uint8_t * buf, size_t len);
		size_t GetSize () const { return m_Size; };
		bool IsEmpty () const { return m_Buffers.empty (); };
		void CleanUp ();

	private:
		std::list<std::shared_ptr<SendBuffer>> m_Buffers;
		size_t m_Size;
	};

	enum StreamStatus
	{
		eStreamStatusNew = 0,
		eStreamStatusOpen,
		eStreamStatusReset,
		eStreamStatusClosing,
		eStreamStatusClosed,
		eStreamStatusTerminated
	};

	class StreamingDestination;

	// Policy answer for one incoming SYN. The server tunnel's access filter
	// and the streaming connection limits share it so a refused peer is
	// answered before any stream exists and before a SYNACK is sent.
	enum class StreamAdmission
	{
		eAllow,
		eDrop, // silence: no SYNACK, no RESET
		eReset // one signed RESET
	};

	// Identity announced by a SYN, or the ratchet-authenticated destination
	// when the packet carries no FROM option. Empty when the packet does not
	// reveal a peer identity (such a SYN is refused later by the normal
	// authentication path, never by the admission policy).
	std::optional<i2p::data::IdentHash> ExtractSynPeerIdentity (const Packet * packet);

	class Stream;

	// Shared state for the blocking StreamingDestination::AcceptStream helper.
	// The armed acceptor owns this object through a shared_ptr, so a late
	// invocation after AcceptStream returned writes into live heap memory
	// instead of a dead stack frame; Cancel() makes such an invocation a no-op
	// and the caller re-offers the stream instead of losing it.
	class SyncAcceptState
	{
	public:
		// Returns false when the waiter already cancelled: the caller must
		// re-offer the stream rather than drop it.
		bool Deliver (std::shared_ptr<Stream> stream);
		// Marks the wait as abandoned without a delivery. Returns true when the
		// cancellation won, i.e. no stream was delivered.
		bool Cancel ();
		// Waits for a delivery. `timeout` == 0 waits indefinitely, a positive
		// value bounds the wait and a negative one returns immediately. Returns
		// true when the wait was completed (by a stream or by a reset with
		// nullptr).
		bool Wait (int timeout);
		std::shared_ptr<Stream> GetStream () const;

	private:
		mutable std::mutex m_Mutex;
		std::condition_variable m_Changed;
		std::shared_ptr<Stream> m_Stream;
		bool m_Completed = false;
		bool m_Cancelled = false;
	};

	/// One I2P streaming connection: sequence/ACK state, window and pacing,
	/// resend queue and the send/receive buffers.
	///
	/// Every state-machine entry point (`HandleNextPacket`, `SendBuffer`,
	/// `ProcessAck`, `Close`, `Terminate`, the timers) runs on the owning
	/// destination's io_context, so the members are not mutex-guarded; only the
	/// `Async*` helpers may be called from another thread, and they post onto
	/// that context. `Receive()` blocks the calling thread until such a posted
	/// read completes, so it must never run on the service thread itself.
	/// `Packet` objects come from and return to the destination's pool, and
	/// `m_LocalDestinationOwner` keeps the destination alive for handlers that
	/// outlive its map entry.
	class Stream : public std::enable_shared_from_this<Stream>
	{
	public:
		Stream (boost::asio::io_context & service, std::shared_ptr<StreamingDestination> local,
			std::shared_ptr<const i2p::data::LeaseSet> remote, int port = 0); // outgoing
		Stream (boost::asio::io_context & service, std::shared_ptr<StreamingDestination> local); // incoming

		~Stream ();
		uint32_t GetSendStreamID () const { return m_SendStreamID; };
		uint32_t GetRecvStreamID () const { return m_RecvStreamID; };
		std::shared_ptr<const i2p::data::LeaseSet> GetRemoteLeaseSet () const { return m_RemoteLeaseSet; };
		std::shared_ptr<const i2p::data::IdentityEx> GetRemoteIdentity () const { return m_RemoteIdentity; };
		bool IsOpen () const { return m_Status == eStreamStatusOpen; };
		bool IsEstablished () const { return m_SendStreamID; };
		bool IsIncoming () const { return m_IsIncoming; };
		StreamStatus GetStatus () const { return m_Status; };
		// SAM 3.2 stream notifications: remote (peer) port and local port.
		uint16_t GetRemotePort () const { return m_Port; };
		uint16_t GetLocalPort () const { return m_LocalPort; };
		void SetPorts (uint16_t remotePort, uint16_t localPort)
		{
			m_Port = remotePort;
			m_LocalPort = localPort;
		};
		StreamingDestination & GetLocalDestination () { return m_LocalDestination; };
		boost::asio::io_context & GetService () { return m_Service; };
		void ResetRoutingPath ();

		void HandleNextPacket (Packet * packet);
		void HandlePing (Packet * packet);
		size_t Send (const uint8_t * buf, size_t len);
		size_t Send (std::span<const uint8_t> buf);
		void AsyncSend (const uint8_t * buf, size_t len, SendHandler && handler);
		void AsyncSend (std::span<const uint8_t> buf, SendHandler && handler);
		void Send (std::shared_ptr<i2p::stream::SendBuffer> && buf);
		void SendPing ();
		// Resume a send after an asynchronous LeaseSet lookup completed.
		void ResumeSend () { SendBuffer (); }

		template <typename Buffer, typename ReceiveHandler>
		void AsyncReceive (const Buffer & buffer, ReceiveHandler && handler, int timeout = 0, size_t minSize = 0);
		size_t ReadSome (uint8_t * buf, size_t len) { return ConcatenatePackets (buf, len); };
		size_t Receive (uint8_t * buf, size_t len, int timeout);

		void AsyncClose () { boost::asio::post (m_Service, std::bind (&Stream::Close, shared_from_this ())); };

		/** only call close from destination thread, use Stream::AsyncClose for other threads */
		void Close ();
		void Cancel () { m_ReceiveTimer.cancel (); };

		size_t GetNumSentBytes () const { return m_NumSentBytes; };
		size_t GetNumReceivedBytes () const { return m_NumReceivedBytes; };
		size_t GetSendQueueSize () const { return m_SentPackets.size (); };
		size_t GetReceiveQueueSize () const { return m_ReceiveQueue.size (); };
		size_t GetSendBufferSize () const { return m_SendBuffer.GetSize (); };
		int GetWindowSize () const { return static_cast<int> (m_WindowSize); };
		int GetRTT () const { return static_cast<int> (m_RTT); };

		void Terminate (bool deleteFromDestination = true);

		// --- connection termination helpers ----------------------------------
		// One-shot RESET for a connection that is not retained: a policy
		// rejection or a retransmitted SYN for a recently closed stream. Unlike
		// SendReset() it never enters the resend queue (Java sends one RESET).
		// Returns false when the stream has no sendStreamID yet.
		bool SendResetOnce ();
		// Answers a first SYN with a single RESET instead of a SYNACK. The
		// packet is consumed. Returns false when the SYN did not authenticate,
		// so a forged SYN never gets a signed response.
		bool RejectWithReset (Packet * packet);
		// Resets and terminates a stream that was created but cannot be handed
		// to an acceptor: one RESET is sent and the stream leaves the maps.
		void AbortWithReset ();

		// Deterministic test seams: force the wire state of a stream so the
		// post-CLOSE/RESET behaviour can be checked without a live peer.
		void SetStatusForTesting (StreamStatus status) { m_Status = status; }
		void SetStreamIDsForTesting (uint32_t sendStreamID, uint32_t recvStreamID)
		{
			m_SendStreamID = sendStreamID;
			m_RecvStreamID = recvStreamID;
		}

	private:
		void CleanUp ();

		void SendBuffer ();
		void SendQuickAck ();
		void SendClose ();
		void SendReset ();
		Packet * CreateResetPacket ();
		bool SendPacket (Packet * packet);
		void SendPackets (const std::list<Packet *> & packets);
		void SendUpdatedLeaseSet ();

		void SavePacket (Packet * packet);
		void ProcessPacket (Packet * packet);
		bool ProcessOptions (uint16_t flags, Packet * packet);
		void ProcessAck (Packet * packet);
		size_t ConcatenatePackets (uint8_t * buf, size_t len);

		void UpdateCurrentRemoteLease (bool expired = false);

		template <typename Buffer, typename ReceiveHandler>
		void HandleReceiveTimer (
			const boost::system::error_code & ecode, Buffer & buffer, ReceiveHandler && handler, int remainingTimeout);

		void ScheduleSend ();
		void HandleSendTimer (const boost::system::error_code & ecode);
		void ScheduleResend ();
		void HandleResendTimer (const boost::system::error_code & ecode);
		void ResendPacket ();
		void ScheduleAck (int timeout);
		void HandleAckSendTimer (const boost::system::error_code & ecode);

		void UpdatePacingTime ();
		void ProcessWindowDrop ();
		void ResetWindowSize ();
		void CancelRemoteLeaseChange ();

	private:
		boost::asio::io_context & m_Service;
		uint32_t m_SendStreamID, m_RecvStreamID, m_SequenceNumber;
		uint32_t m_DropWindowDelaySequenceNumber;
		uint32_t m_TunnelsChangeSequenceNumber;
		int32_t m_LastReceivedSequenceNumber;
		int32_t m_PreviousReceivedSequenceNumber;
		int32_t m_LastConfirmedReceivedSequenceNumber; // for limit inbound speed
		StreamStatus m_Status;
		bool m_IsIncoming, m_IsAckSendScheduled, m_IsNAcked, m_IsFirstACK, m_IsResendNeeded, m_IsFirstRttSample,
			m_IsSendTime, m_IsWinDropped, m_IsChoking2, m_IsChoking3, m_IsClientChoked, m_IsClientChoked2,
			m_IsTimeOutResend, m_IsImmediateAckRequested, m_IsRemoteLeaseChangeInProgress, m_IsBufferEmpty,
			m_IsJavaClient, m_DontSign, m_IsResetReceived = false, m_IsRequestingLeaseSet = false;
		// Keep the destination alive: timer, receive and send handlers can still
		// run after the owner removed it from its map, and every one of them
		// reaches the destination or its packet pools through the reference below.
		std::shared_ptr<StreamingDestination> m_LocalDestinationOwner;
		StreamingDestination & m_LocalDestination;
		// own reference: the stream may outlive the destination's map entry
		std::shared_ptr<const i2p::data::IdentityEx> m_RemoteIdentity;
		std::shared_ptr<const i2p::crypto::Verifier> m_TransientVerifier; // in case of offline key
		std::shared_ptr<const i2p::data::LeaseSet> m_RemoteLeaseSet;
		std::shared_ptr<i2p::garlic::GarlicRoutingSession> m_RoutingSession;
		std::shared_ptr<const i2p::data::Lease> m_CurrentRemoteLease;
		std::shared_ptr<const i2p::data::Lease> m_NextRemoteLease;
		std::shared_ptr<i2p::tunnel::OutboundTunnel> m_CurrentOutboundTunnel;
		std::list<Packet *> m_ReceiveQueue;
		size_t m_SizeToReceive; // before invoke AsyncReceive handler
		std::set<Packet *, PacketCmp> m_SavedPackets;
		std::list<Packet *> m_SentPackets;
		std::list<Packet *> m_NACKedPackets;
		boost::asio::steady_timer m_ReceiveTimer, m_SendTimer, m_ResendTimer, m_AckSendTimer;
		size_t m_NumSentBytes, m_NumReceivedBytes;
		uint16_t m_Port, m_LocalPort;

		SendBufferQueue m_SendBuffer;
		double m_RTT, m_MinRTT, m_SlowRTT, m_FastRTT;
		float m_WindowSize, m_MaxWindowSize, m_LastWindowDropSize, m_WindowDropTargetSize;
		int m_WindowIncCounter, m_RTO, m_AckDelay, m_PrevRTTSample;
		double m_Jitter;
		uint64_t m_MinPacingTime, m_PacingTime, m_PacingTimeRem, // microseconds
			m_LastSendTime, m_LastACKRecieveTime, m_ACKRecieveInterval, m_RemoteLeaseChangeTime, m_LastWindowIncTime,
			m_LastACKRequestTime; // milliseconds
		uint64_t m_LastACKSendTime, m_PacketACKInterval, m_PacketACKIntervalRem; // for limit inbound speed
		int m_MaxNumResendAttempts, m_NumResendAttempts, m_NumPacketsToSend;
		uint64_t m_JitterAccum;
		int m_JitterDiv;
		size_t m_MTU;
	};

	/// Streaming endpoint for one local port: accepts incoming SYNs, dispatches
	/// data packets to the right `Stream`, and holds the acceptor registrations.
	///
	/// `m_Streams`/`m_IncomingStreams` are guarded by `m_StreamsMutex`; the
	/// acceptor registrations, the pong handler, the admission predicate and the
	/// pending-incoming backlog are guarded by `m_AcceptorMutex` because other
	/// threads (SAM, server tunnels, tests) publish them while the service
	/// thread dispatches. A SYN is refused by the installed admission predicate
	/// before a stream object exists, so a denied peer never receives a SYNACK.
	class StreamingDestination : public std::enable_shared_from_this<StreamingDestination>
	{
	public:
		typedef std::function<void (std::shared_ptr<Stream>)> Acceptor;
		typedef std::function<void (const i2p::data::IdentHash *)> PongHandler;
		// Access-filter seam: consulted for a first SYN before the stream is
		// created, so a denied peer never receives a SYNACK and never reaches
		// an acceptor. `toPort` is the local streaming port.
		typedef std::function<StreamAdmission (const i2p::data::IdentHash & peer, uint16_t toPort)> AdmissionPredicate;
		// Identifies one armed one-shot acceptor so a caller can unlink exactly
		// that registration (AcceptStream's timeout) without disturbing the
		// acceptors other clients armed.
		typedef uint64_t AcceptorToken;

		// Outcome of the SYN admission policy. The packet path uses it to decide
		// between accepting, dropping a duplicate and answering with a RESET.
		enum class SynAdmission
		{
			eAccept,
			eDuplicate, // an incoming stream with this receiveStreamID is live
			eRecentlyClosed, // the peer retransmitted a SYN for a finished stream
			eOverLimit, // concurrent stream cap
			eBacklogFull, // pending incoming backlog is full
			eThrottled // per-peer/total connection throttle
		};

		StreamingDestination (
			std::shared_ptr<i2p::client::ClientDestination> owner, uint16_t localPort = 0, bool gzip = false);
		~StreamingDestination ();

		void Start ();
		void Stop ();

		std::shared_ptr<Stream> CreateNewOutgoingStream (
			std::shared_ptr<const i2p::data::LeaseSet> remote, int port = 0);
		void SendPing (std::shared_ptr<const i2p::data::LeaseSet> remote, uint16_t fromPort = 0, uint16_t toPort = 0);
		void DeleteStream (std::shared_ptr<Stream> stream);
		bool DeleteStream (uint32_t recvStreamID);
		size_t GetNumStreams () const
		{
			std::lock_guard<std::mutex> l (m_StreamsMutex);
			return m_Streams.size ();
		};
		// Replaces the persistent acceptor and returns the previous one, so the
		// caller owns exactly the registration it displaced and cannot silently
		// disable another tunnel's acceptor without knowing about it.
		Acceptor SetAcceptor (const Acceptor & acceptor);
		// Remove every armed acceptor (one-shot registrations first, then the
		// persistent one). Each registration is completed with nullptr so a
		// waiter such as a SAM socket cannot hang forever.
		void ResetAcceptor ();
		bool IsAcceptorSet () const;
		// Installs or clears the SYN admission predicate. The predicate is
		// copied out under the registration lock and invoked without any
		// streaming lock held, so it may take its own (filter) lock safely.
		void SetAdmissionPredicate (const AdmissionPredicate & predicate);
		void ResetAdmissionPredicate ();
		bool IsAdmissionPredicateSet () const;
		// Action for a SYN refused by a limit or by the access filter. Java
		// i2ptunnel's `limitAction` selects `drop` (silence) or `reset` (one
		// signed RESET); the default keeps the historical RESET.
		void SetRejectionAction (StreamAdmission action) { m_RejectionAction.store (action); }
		StreamAdmission GetRejectionAction () const { return m_RejectionAction.load (); }
		// Arms a one-shot acceptor and returns its registration token. The
		// registration owns every object it needs, so a late invocation after
		// the caller gave up cannot touch a dead stack frame.
		AcceptorToken AcceptOnce (const Acceptor & acceptor);
		// Unlinks one armed registration. Returns false when it was already
		// consumed for delivery or removed, so the caller knows that a delivery
		// may be in flight.
		bool RemoveAcceptor (AcceptorToken token);
		// Blocking accept for the C API. On timeout this unlinks exactly its own
		// registration (RemoveAcceptor), so other armed acceptors keep working;
		// the wait state lives on the heap and outlives the caller's frame.
		std::shared_ptr<Stream> AcceptStream (int timeout = 0); // sync
		void SetPongHandler (const PongHandler & handler);
		void ResetPongHandler ();

		std::shared_ptr<i2p::client::ClientDestination> GetOwner () const { return m_Owner; };
		void SetOwner (std::shared_ptr<i2p::client::ClientDestination> owner);
		uint16_t GetLocalPort () const { return m_LocalPort; };

		void HandleDataMessagePayload (
			const uint8_t * buf, size_t len, i2p::garlic::ECIESX25519AEADRatchetSession * from);
		// SAM 3.2 incoming stream port reporting; fromPort is the peer source port,
		// toPort is the destination port selected by the Data message.
		void HandleDataMessagePayload (uint16_t fromPort, uint16_t toPort, const uint8_t * buf, size_t len,
			i2p::garlic::ECIESX25519AEADRatchetSession * from);
		std::shared_ptr<I2NPMessage> CreateDataMessage (
			const uint8_t * payload, size_t len, uint16_t toPort, bool checksum = true, bool gzip = false);

		Packet * NewPacket () { return m_PacketsPool.Acquire (); }
		void DeletePacket (Packet * p) { return m_PacketsPool.Release (p); }
		uint32_t GetRandom ();

	private:
		void HandleNextPacket (Packet * packet);
		std::shared_ptr<Stream> CreateNewIncomingStream (uint32_t receiveStreamID);
		void HandlePendingIncomingTimer (const boost::system::error_code & ecode);
		void StopInternal ();
		void StartInternal ();
		void UpdateConnectionLimits ();
		// Decides and dispatches under one lock so a stream is either handed to
		// an acceptor or queued in m_PendingIncomingStreams, never lost.
		void DispatchPendingIncoming ();
		bool AddPendingIncomingStream (std::shared_ptr<Stream> stream);
		AcceptorToken ArmSyncAccept (const std::shared_ptr<SyncAcceptState> & state);
		// Service thread only.
		SynAdmission DecideSynAdmission (uint32_t receiveStreamID, i2p::garlic::ECIESX25519AEADRatchetSession * from);
		// Consumes `packet`, answers it with one signed RESET when it
		// authenticates, and leaves no stream behind. Returns true when a RESET
		// was built.
		bool RejectSynWithReset (Packet * packet, uint32_t receiveStreamID);
		bool IsRecentlyClosed (uint32_t receiveStreamID) const;
		void RememberRecentlyClosed (uint32_t receiveStreamID);
		void ClearRecentlyClosed ();
		void LogSynRejection (const char * reason, uint32_t receiveStreamID);
		// Consumes a SYN refused by a limit: drops it silently or answers with
		// one signed RESET according to m_RejectionAction.
		void RejectSyn (Packet * packet, uint32_t receiveStreamID, const char * reason);
		// Copies the installed predicate out of the registration lock and
		// invokes it. Returns eAllow when no predicate is installed.
		StreamAdmission EvaluateAdmission (const i2p::data::IdentHash & peer) const;
		void ScheduleConnectionCleanup ();
		void HandleConnectionCleanup (const boost::system::error_code & ecode);

	private:
		i2p::util::MemoryPool<Packet> m_PacketsPool;
		i2p::util::MemoryPool<I2NPMessageBuffer<I2NP_MAX_SHORT_MESSAGE_SIZE>> m_I2NPMsgsPool;

		std::shared_ptr<i2p::client::ClientDestination> m_Owner;
		uint16_t m_LocalPort;
		bool m_Gzip; // gzip compression of data messages
		mutable std::mutex m_StreamsMutex;
		std::unordered_map<uint32_t, std::shared_ptr<Stream>> m_Streams; // sendStreamID->stream
		std::unordered_map<uint32_t, std::shared_ptr<Stream>> m_IncomingStreams; // receiveStreamID->stream
		std::shared_ptr<Stream> m_LastStream;
		uint16_t m_LastFromPort = 0, m_LastToPort = 0;
		// Guards the acceptor registrations, the pong handler and the pending
		// incoming queue; all of them are published or cleared from threads other
		// than the destination service.
		mutable std::mutex m_AcceptorMutex;
		struct AcceptorRegistration
		{
			AcceptorToken id;
			Acceptor acceptor;
		};
		Acceptor m_Acceptor;
		std::shared_ptr<const AdmissionPredicate> m_AdmissionPredicate;
		std::atomic<StreamAdmission> m_RejectionAction { StreamAdmission::eReset };
		std::vector<AcceptorRegistration> m_ArmedAcceptors; // back() is the newest
		std::shared_ptr<const PongHandler> m_PongHandler;
		std::atomic<AcceptorToken> m_NextAcceptorToken { 1 };
		std::list<std::shared_ptr<Stream>> m_PendingIncomingStreams;
		boost::asio::steady_timer m_PendingIncomingTimer;
		boost::asio::steady_timer m_ConnectionCleanupTimer;
		std::atomic<bool> m_Started { false };
		std::unordered_map<uint32_t, std::list<Packet *>>
			m_SavedPackets; // receiveStreamID->packets, arrived before SYN
		mutable std::mutex m_RecentlyClosedMutex;
		mutable std::unordered_map<uint32_t, uint64_t> m_RecentlyClosed; // receiveStreamID->seconds
		std::atomic<size_t> m_NumRejectionResets { 0 };
		uint64_t m_LastSynRejectLogTime = 0; // in milliseconds
		uint32_t m_NumSuppressedSynRejects = 0;

		uint64_t m_LastCleanupTime; // in seconds
		std::shared_ptr<StreamingConnectionThrottle> m_ConnectionThrottle;

	public:
		i2p::data::GzipInflator m_Inflator;
		i2p::data::GzipDeflator m_Deflator;

		// for administrative views
		const decltype (m_Streams) & GetStreams () const { return m_Streams; };

		// copy for other threads, unlike GetStreams which hands out the map itself
		std::vector<std::shared_ptr<const Stream>> GetStreamsList ();

		// --- deterministic test seams -----------------------------------------
		// Run the real admission/policy helpers without constructing a signed
		// streaming packet, so the state transitions stay testable in isolation.
		SynAdmission AdmitSynForTesting (uint32_t receiveStreamID)
		{
			return DecideSynAdmission (receiveStreamID, nullptr);
		}
		void RememberRecentlyClosedForTesting (uint32_t receiveStreamID) { RememberRecentlyClosed (receiveStreamID); }
		size_t GetNumRecentlyClosedForTesting () const;
		size_t GetNumRejectionResetsForTesting () const { return m_NumRejectionResets.load (); }
		size_t GetNumPendingIncomingForTesting () const;
		size_t GetNumArmedAcceptorsForTesting () const;
		// Hands an already-created stream to the acceptor/pending dispatch, as
		// the SYN path does. Returns false when the backlog is full.
		bool OfferIncomingStreamForTesting (std::shared_ptr<Stream> stream)
		{
			return AddPendingIncomingStream (std::move (stream));
		}
		// Arms the same registration AcceptStream uses, without waiting.
		AcceptorToken StartSyncAcceptForTesting (const std::shared_ptr<SyncAcceptState> & state)
		{
			return ArmSyncAccept (state);
		}
		// Removes and returns one armed registration so a test can prove that a
		// late invocation after cancellation has no effect.
		Acceptor TakeArmedAcceptorForTesting (AcceptorToken token);
		// Builds (and counts) the one-shot RESET a rejected SYN receives without
		// requiring a signed SYN packet.
		bool BuildRejectionResetForTesting (uint32_t receiveStreamID);
		// Runs the installed admission predicate exactly as the SYN path does.
		StreamAdmission EvaluateAdmissionForTesting (const i2p::data::IdentHash & peer) const
		{
			return EvaluateAdmission (peer);
		}
		// Applies the denial side effects of the SYN path for a denied peer:
		// a silent drop keeps the stream set and the reset counter untouched,
		// a reset increments the counter and remembers the connection as
		// closed. No stream is ever created or offered to an acceptor.
		StreamAdmission ApplyAdmissionForTesting (uint32_t receiveStreamID, const i2p::data::IdentHash & peer);
		// Applies the configured rejection action for a limit rejection.
		StreamAdmission ApplyRejectionActionForTesting (uint32_t receiveStreamID);
		// Registers an extra local stream so tests can reach the concurrent
		// stream cap without a live peer.
		std::shared_ptr<Stream> CreateStreamForTesting () { return CreateNewIncomingStream (GetRandom ()); }
		// Runs the real teardown on the calling thread so a test can release the
		// pending backlog (and break the destination/stream ownership cycle)
		// without starting an io thread.
		void StopForTesting () { StopInternal (); }
	};

	//-------------------------------------------------

	template <typename Buffer, typename ReceiveHandler>
	void Stream::AsyncReceive (const Buffer & buffer, ReceiveHandler && handler, int timeout, size_t minSize)
	{
		auto s = shared_from_this ();
		boost::asio::post (m_Service,
			[s, buffer, handler = std::move (handler), timeout, minSize] (void) mutable
			{
				if (!s->m_ReceiveQueue.empty ()
					|| (s->m_Status != eStreamStatusNew && s->m_Status != eStreamStatusOpen))
					s->HandleReceiveTimer (boost::asio::error::make_error_code (boost::asio::error::operation_aborted),
						buffer, handler, 0);
				else
				{
					s->m_SizeToReceive = minSize;
					int t = (timeout > MAX_RECEIVE_TIMEOUT) ? MAX_RECEIVE_TIMEOUT : timeout;
					s->m_ReceiveTimer.expires_after (std::chrono::seconds (t));
					int left = timeout - t;
					s->m_ReceiveTimer.async_wait (
						[s, buffer, handler = std::move (handler), left] (const boost::system::error_code & ec) mutable
						{ s->HandleReceiveTimer (ec, buffer, std::move (handler), left); });
				}
			});
	}

	template <typename Buffer, typename ReceiveHandler>
	void Stream::HandleReceiveTimer (
		const boost::system::error_code & ecode, Buffer & buffer, ReceiveHandler && handler, int remainingTimeout)
	{
		if (ecode == boost::asio::error::operation_aborted)
		{
			// timeout not expired
			size_t received = ConcatenatePackets ((uint8_t *)buffer.data (), buffer.size ());
			if (received > 0)
				handler (boost::system::error_code (), received);
			else
			{
				// Half-close semantics: bytes that were already queued above stay
				// readable, and only an empty read reports the connection state.
				// The mapping is deliberate: a CLOSE is an orderly end of stream
				// (eof) and a RESET is connection_reset, exactly like a socket,
				// while operation_aborted means the read itself was cancelled.
				boost::system::error_code retCode;
				switch (m_Status)
				{
				case eStreamStatusClosed:
				case eStreamStatusTerminated:
					retCode = boost::asio::error::make_error_code (boost::asio::error::eof);
					break;
				case eStreamStatusReset:
					retCode = boost::asio::error::make_error_code (boost::asio::error::connection_reset);
					break;
				case eStreamStatusNew:
					retCode = boost::asio::error::make_error_code (boost::asio::error::not_connected);
					break;
				default: retCode = boost::asio::error::make_error_code (boost::asio::error::operation_aborted);
				}
				handler (retCode, 0);
			}
		}
		else
		{
			// timeout expired
			if (remainingTimeout <= 0)
			{
				m_SizeToReceive = 0;
				if (m_Status != eStreamStatusTerminated)
					handler (boost::asio::error::make_error_code (boost::asio::error::timed_out), 0);
			}
			else
			{
				// intermediate interrupt
				SendUpdatedLeaseSet (); // send our leaseset if applicable
				AsyncReceive (buffer, handler, remainingTimeout, m_SizeToReceive);
			}
		}
	}
}
}
#endif

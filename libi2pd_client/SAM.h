/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: SAM.
 *
 * Declares the SAM protocol constants, session and master/subsession objects,
 * the socket state machine and the bridge owning the TCP acceptor and UDP
 * endpoint.
 *
 */

#ifndef SAM_H__
#define SAM_H__

#include <inttypes.h>
#include <string>
#include <string_view>
#include <map>
#include <list>
#include <deque>
#include <set>
#include <vector>
#include <utility>
#include <atomic>
#include <functional>
#include <thread>
#include <mutex>
#include <memory>
#include <boost/asio.hpp>
#include "util.h"
#include "Identity.h"
#include "LeaseSet.h"
#include "Streaming.h"
#include "Datagram.h"
#include "Destination.h"

namespace i2p
{
namespace client
{
	// Command/frame buffer must hold the largest SAM payload (32 KiB) plus a
	// base64 destination and command line. The old 8 KiB buffer caused a
	// zero-length read loop for valid RAW/DATAGRAM SEND frames.
	const size_t SAM_SOCKET_BUFFER_SIZE = 49152;
	const size_t SAM_STREAM_BUFFER_SIZE = 65536;
	const size_t SAM_STREAM_MAX_SEND_BUFFER_SIZE = 65536;
	const int SAM_SOCKET_CONNECTION_MAX_IDLE = 3600; // in seconds
	const int SAM_SESSION_READINESS_CHECK_INTERVAL = 3; // in seconds
	const int SAM_SESSION_CLEANUP_CHECK_INTERVAL = 5; // in seconds
	const size_t SAM_SESSION_MAX_ACCEPT_QUEUE_SIZE = 50;

	const char SAM_HANDSHAKE[] = "HELLO VERSION";
	const char SAM_HANDSHAKE_REPLY[] = "HELLO REPLY RESULT=OK VERSION=%s\n";
	constexpr std::string_view SAM_HANDSHAKE_NOVERSION { "HELLO REPLY RESULT=NOVERSION\n" };
	const char SAM_HANDSHAKE_I2P_ERROR[] = "HELLO REPLY RESULT=I2P_ERROR\n";
	const char SAM_SESSION_CREATE[] = "SESSION CREATE";
	const char SAM_SESSION_CREATE_REPLY_OK[] = "SESSION STATUS RESULT=OK DESTINATION=%s\n";
	const char SAM_SUBSESSION_STATUS_OK[] = "SESSION STATUS RESULT=OK ID=\"%s\"\n";
	const char SAM_SUBSESSION_STATUS_ERROR[] = "SESSION STATUS RESULT=I2P_ERROR ID=\"%s\" MESSAGE=\"%s\"\n";
	constexpr std::string_view SAM_SESSION_CREATE_DUPLICATED_ID { "SESSION STATUS RESULT=DUPLICATED_ID\n" };
	constexpr std::string_view SAM_SESSION_CREATE_DUPLICATED_DEST { "SESSION STATUS RESULT=DUPLICATED_DEST\n" };
	constexpr std::string_view SAM_SESSION_CREATE_INVALID_ID { "SESSION STATUS RESULT=INVALID_ID\n" };
	constexpr std::string_view SAM_SESSION_STATUS_INVALID_KEY { "SESSION STATUS RESULT=INVALID_KEY\n" };
	const char SAM_SESSION_STATUS_I2P_ERROR[] = "SESSION STATUS RESULT=I2P_ERROR MESSAGE=\"%s\"\n";
	const char SAM_SESSION_ADD[] = "SESSION ADD";
	const char SAM_SESSION_REMOVE[] = "SESSION REMOVE";
	const char SAM_STREAM_CONNECT[] = "STREAM CONNECT";
	constexpr std::string_view SAM_STREAM_STATUS_OK { "STREAM STATUS RESULT=OK\n" };
	constexpr std::string_view SAM_STREAM_STATUS_INVALID_ID { "STREAM STATUS RESULT=INVALID_ID\n" };
	constexpr std::string_view SAM_STREAM_STATUS_INVALID_KEY { "STREAM STATUS RESULT=INVALID_KEY\n" };
	const char SAM_STREAM_STATUS_CANT_REACH_PEER[] = "STREAM STATUS RESULT=CANT_REACH_PEER MESSAGE=\"%s\"\n";
	const char SAM_STREAM_STATUS_I2P_ERROR[] = "STREAM STATUS RESULT=I2P_ERROR MESSAGE=\"%s\"\n";
	const char SAM_STREAM_ACCEPT[] = "STREAM ACCEPT";
	const char SAM_STREAM_FORWARD[] = "STREAM FORWARD";
	const char SAM_DATAGRAM_SEND[] = "DATAGRAM SEND";
	const char SAM_RAW_SEND[] = "RAW SEND";
	const char SAM_DEST_GENERATE[] = "DEST GENERATE";
	const char SAM_DEST_REPLY[] = "DEST REPLY PUB=%s PRIV=%s\n";
	const char SAM_DEST_REPLY_I2P_ERROR[] = "DEST REPLY RESULT=I2P_ERROR\n";
	const char SAM_NAMING_LOOKUP[] = "NAMING LOOKUP";
	const char SAM_NAMING_REPLY[] = "NAMING REPLY RESULT=OK NAME=%s VALUE=%s\n";
	// SAM 3.2 adds the port fields. Do not advertise them when the negotiated
	// version is older (SAMv1Handler.receiveDatagramBytes/receiveRawBytes).
	const char SAM_DATAGRAM_RECEIVED[] = "DATAGRAM RECEIVED DESTINATION=%s SIZE=%lu\n";
	const char SAM_DATAGRAM_RECEIVED_PORTS[] = "DATAGRAM RECEIVED DESTINATION=%s SIZE=%lu FROM_PORT=%u TO_PORT=%u\n";
	const char SAM_RAW_RECEIVED[] = "RAW RECEIVED SIZE=%lu\n";
	const char SAM_RAW_RECEIVED_PORTS[] = "RAW RECEIVED SIZE=%lu PROTOCOL=%u FROM_PORT=%u TO_PORT=%u\n";
	const char SAM_NAMING_REPLY_INVALID_KEY[] = "NAMING REPLY RESULT=INVALID_KEY NAME=%s\n";
	const char SAM_NAMING_REPLY_KEY_NOT_FOUND[] = "NAMING REPLY RESULT=KEY_NOT_FOUND NAME=%s\n";
	const char SAM_PARAM_MIN[] = "MIN";
	const char SAM_PARAM_MAX[] = "MAX";
	const char SAM_PARAM_STYLE[] = "STYLE";
	const char SAM_PARAM_ID[] = "ID";
	const char SAM_PARAM_SILENT[] = "SILENT";
	const char SAM_PARAM_DESTINATION[] = "DESTINATION";
	const char SAM_PARAM_NAME[] = "NAME";
	const char SAM_PARAM_SIGNATURE_TYPE[] = "SIGNATURE_TYPE";
	const char SAM_PARAM_CRYPTO_TYPE[] = "CRYPTO_TYPE";
	const char SAM_PARAM_SIZE[] = "SIZE";
	const char SAM_PARAM_HOST[] = "HOST";
	const char SAM_PARAM_LISTEN_PORT[] = "LISTEN_PORT";
	const char SAM_PARAM_PORT[] = "PORT";
	const char SAM_PARAM_FROM_PORT[] = "FROM_PORT";
	const char SAM_PARAM_TO_PORT[] = "TO_PORT";
	const char SAM_PARAM_PROTOCOL[] = "PROTOCOL";
	const char SAM_PARAM_HEADER[] = "HEADER";
	const char SAM_VALUE_TRANSIENT[] = "TRANSIENT";
	const char SAM_VALUE_TRUE[] = "true";
	const char SAM_VALUE_FALSE[] = "false";

	constexpr std::string_view SAM_VALUE_STREAM { "STREAM" };
	constexpr std::string_view SAM_VALUE_DATAGRAM { "DATAGRAM" };
	constexpr std::string_view SAM_VALUE_DATAGRAM2 { "DATAGRAM2" };
	constexpr std::string_view SAM_VALUE_DATAGRAM3 { "DATAGRAM3" };
	constexpr std::string_view SAM_VALUE_RAW { "RAW" };
	constexpr std::string_view SAM_VALUE_MASTER { "MASTER" };
	constexpr std::string_view SAM_VALUE_PRIMARY { "PRIMARY" };

	constexpr std::string_view SAM_PING { "PING" };
	const char SAM_PONG[] = "PONG %s\n";

	constexpr int MAKE_SAM_VERSION_NUMBER (int major, int minor)
	{
		return major * 10 + minor;
	}
	constexpr int MAX_SAM_VERSION_VALUE
		= 99999; // stop parsing digits above that, any such version is out of range anyway
	constexpr int MIN_SAM_VERSION = MAKE_SAM_VERSION_NUMBER (3, 0);
	constexpr int MAX_SAM_VERSION = MAKE_SAM_VERSION_NUMBER (3, 3);
	constexpr int SAM_VERSION_32 = MAKE_SAM_VERSION_NUMBER (3, 2); // SAM 3.2
	constexpr int SAM_VERSION_33 = MAKE_SAM_VERSION_NUMBER (3, 3); // SAM 3.3
	// SAMv3StreamSession.connect(): default I2PSocket connect timeout
	constexpr int SAM_STREAM_CONNECT_TIMEOUT = 60 * 1000; // milliseconds

	enum class SAMSocketType
	{
		eSAMSocketTypeUnknown,
		eSAMSocketTypeSession,
		eSAMSocketTypeStream,
		eSAMSocketTypeAcceptor,
		eSAMSocketTypeForward,
		eSAMSocketTypeTerminated
	};

	class SAMBridge;
	struct SAMSession;
	class SAMSocket : public std::enable_shared_from_this<SAMSocket>
	{
	public:
		typedef boost::asio::ip::tcp::socket Socket_t;
		SAMSocket (SAMBridge & owner);
		~SAMSocket ();

		Socket_t & GetSocket () { return m_Socket; };
		void ReceiveHandshake ();
		void SetSocketType (SAMSocketType socketType) { m_SocketType = socketType; };
		SAMSocketType GetSocketType () const { return m_SocketType; };

		void Terminate (const char * reason);

		bool IsSession (std::string_view id) const;

		static i2p::util::Mapping ExtractParams (std::string_view buf);

	private:
		void SetID (std::string_view id);

	private:
		void TerminateClose () { Terminate (nullptr); }

		void HandleHandshakeReceived (const boost::system::error_code & ecode, std::size_t bytes_transferred);
		void HandleHandshakeReplySent (const boost::system::error_code & ecode, std::size_t bytes_transferred);
		void HandleMessage (const boost::system::error_code & ecode, std::size_t bytes_transferred);
		void SendMessageReply (std::string_view msg, bool close);
		void HandleMessageReplySent (
			const boost::system::error_code & ecode, std::size_t bytes_transferred, bool close);
		void Receive ();
		void HandleReceived (const boost::system::error_code & ecode, std::size_t bytes_transferred);

		void I2PReceive ();
		// Event entry points. These may be invoked from a destination/stream
		// thread; they copy their payload and post the actual work to the SAM
		// service, which is the only owner of m_Socket, m_Stream and the
		// per-socket write queue.
		void HandleI2PReceive (const boost::system::error_code & ecode, std::size_t bytes_transferred);
		void HandleI2PAccept (std::shared_ptr<i2p::stream::Stream> stream);
		void HandleI2PForward (std::shared_ptr<i2p::stream::Stream> stream, boost::asio::ip::tcp::endpoint ep);
		// sessionID is the session the datagram belongs to, a subsession has its own
		void HandleI2PDatagramReceive (std::string sessionID, const i2p::data::IdentityEx & from, uint16_t fromPort,
			uint16_t toPort, const uint8_t * buf, size_t len, const i2p::util::Mapping * options);
		void HandleI2PDatagram3Receive (std::string sessionID, const i2p::data::IdentHash & from, uint16_t fromPort,
			uint16_t toPort, const uint8_t * buf, size_t len, const i2p::util::Mapping * options);
		void HandleI2PRawDatagramReceive (std::string sessionID, uint8_t protocol, uint16_t fromPort, uint16_t toPort,
			const uint8_t * buf, size_t len);

		// SAM-service-only implementations.
		void HandleI2PReceiveOnService (const boost::system::error_code & ecode, std::size_t bytes_transferred);
		void HandleI2PAcceptOnService (std::shared_ptr<i2p::stream::Stream> stream);
		void HandleI2PForwardOnService (std::shared_ptr<i2p::stream::Stream> stream, boost::asio::ip::tcp::endpoint ep);
		void HandleI2PDatagramReceiveOnService (std::string sessionID, const i2p::data::IdentityEx & from,
			uint16_t fromPort, uint16_t toPort, std::vector<uint8_t> payload);
		void HandleI2PDatagram3ReceiveOnService (std::string sessionID, const i2p::data::IdentHash & from,
			uint16_t fromPort, uint16_t toPort, std::vector<uint8_t> payload);
		void HandleI2PRawDatagramReceiveOnService (
			std::string sessionID, uint8_t protocol, uint16_t fromPort, uint16_t toPort, std::vector<uint8_t> payload);

		void ProcessSessionCreate (std::string_view buf);
		void ProcessStreamConnect (char * buf, const char * payload, size_t rem);
		void ProcessStreamAccept (std::string_view buf);
		void ProcessStreamForward (std::string_view buf);
		void ProcessDestGenerate (std::string_view buf);
		void ProcessNamingLookup (std::string_view buf);
		void ProcessSessionAdd (std::string_view buf);
		void ProcessSessionRemove (std::string_view buf);
		void ProcessPing (std::string_view text);
		void SendReplyWithMessage (const char * reply, const std::string & msg, bool close);
		void SendSubsessionStatusOk (std::string_view id);
		void SendSubsessionError (std::string_view id, std::string_view msg);
		void SendSessionI2PError (const std::string & msg);
		void SendStreamI2PError (const std::string & msg);
		void SendStreamCantReachPeer (const std::string & msg);
		size_t ProcessDatagramSend (char * buf, size_t len, const char * data); // from SAM 1.0

		void Connect (std::shared_ptr<const i2p::data::LeaseSet> remote, std::shared_ptr<SAMSession> session = nullptr);
		void HandleConnectLeaseSetRequestComplete (std::shared_ptr<i2p::data::LeaseSet> leaseSet);
		void SendNamingLookupReply (const std::string & name, std::shared_ptr<const i2p::data::IdentityEx> identity);
		void HandleNamingLookupLeaseSetRequestComplete (
			std::shared_ptr<i2p::data::LeaseSet> leaseSet, std::string name);
		void HandleSessionReadinessCheckTimer (const boost::system::error_code & ecode);
		void HandleStreamConnectTimer (const boost::system::error_code & ecode);
		void SendSessionCreateReplyOk ();

		// Single in-flight write queue for data sent to the SAM client. The
		// completion handler keeps the owning buffer alive.
		void WriteI2PData (size_t sz);
		void WriteI2PDataImmediate (std::shared_ptr<uint8_t[]> buff, size_t sz);
		void QueueI2PData (const uint8_t * data, size_t len);
		void QueueI2PData (std::shared_ptr<uint8_t[]> buff, size_t len);
		void QueueI2PWrite (std::shared_ptr<uint8_t[]> buff, size_t len,
			std::function<void (const boost::system::error_code &)> && onSent);
		void StartNextI2PWrite ();
		void HandleI2PWriteCompleted (const boost::system::error_code & ec, std::size_t bytes_transferred);
		void HandleStreamSend (const boost::system::error_code & ec);

	private:
		SAMBridge & m_Owner;
		Socket_t m_Socket;
		boost::asio::steady_timer m_Timer;
		char m_Buffer[SAM_SOCKET_BUFFER_SIZE + 1];
		size_t m_BufferOffset; // for session only
		uint8_t m_StreamBuffer[SAM_STREAM_BUFFER_SIZE];

		struct OutputBuffer
		{
			std::shared_ptr<uint8_t[]> data;
			size_t length;
			std::function<void (const boost::system::error_code &)> onSent;
		};
		std::deque<OutputBuffer> m_WriteQueue; // SAM service thread only
		bool m_IsWriting; // SAM service thread only

		SAMSocketType m_SocketType;
		mutable std::mutex m_IDMutex; // protects m_ID for ListSockets() readers
		std::string m_ID; // nickname
		bool m_IsSilent;
		bool m_IsAccepting; // for eSAMSocketTypeAcceptor only
		bool m_IsReceiving; // for eSAMSocketTypeStream only
		bool m_IsI2PReceiving = false; // one outstanding I2P-side read
		std::shared_ptr<i2p::stream::Stream> m_Stream;
		int m_Version;
		int m_ConnectAttempts;
		uint16_t m_ConnectFromPort, m_ConnectToPort;
	};

	// First line of a datagram sent to the SAM UDP port. SAM 3.x clients send
	// "3.x $nickname $destination [options]" (SAMv3Handler's UDP listener);
	// accept the legacy openi2pd/i2pd form without the version token as well.
	struct SAMDatagramHeader
	{
		std::string_view version; // empty for the legacy form
		std::string_view sessionID;
		std::string_view destination;
		std::string_view options; // may be empty
	};

	bool ParseSAMDatagramHeader (std::string_view line, SAMDatagramHeader & header);

	// Byte offset to preserve in m_Buffer for the next read after processing a
	// DATAGRAM SEND/RAW SEND frame. `paramsOffset` is the start of the
	// parameters inside the socket buffer, `len` the bytes from there to the
	// end of the buffer and `processed` the return value of the frame parser.
	// A partial frame keeps the complete command line plus the bytes received
	// so far; a complete frame keeps only a trailing next-command fragment.
	inline size_t GetSAMSendResumeOffset (size_t paramsOffset, size_t len, size_t processed)
	{
		if (processed >= len) return 0;
		if (processed > 0) return len - processed;
		return paramsOffset + len;
	}

	enum class SAMSessionType
	{
		eSAMSessionTypeUnknown,
		eSAMSessionTypeStream,
		eSAMSessionTypeDatagram,
		eSAMSessionTypeRaw,
		eSAMSessionTypeMaster
	};

	struct SAMSession
	{
		SAMBridge & m_Bridge;
		std::string Name;
		SAMSessionType Type;
		i2p::datagram::DatagramVersion DatagramVersion;

		std::shared_ptr<boost::asio::ip::udp::endpoint> UDPEndpoint; // TODO: move
		// Pending STREAM ACCEPT sockets, guarded by acceptQueueMutex. Use the
		// transfer API below; the list itself is an implementation detail.
		mutable std::mutex acceptQueueMutex;
		std::list<std::pair<std::shared_ptr<SAMSocket>, uint64_t>> acceptQueue; // socket, receive time in seconds
		uint16_t FromPort = 0, ToPort = 0;
		uint8_t Protocol = i2p::datagram::DATAGRAM_RAW_PROTOCOL;
		bool Header = false; // raw UDP forwarding: prepend PROTOCOL/FROM/TO

		SAMSession (SAMBridge & parent, std::string_view name, SAMSessionType type);
		virtual ~SAMSession () { };

		virtual std::shared_ptr<ClientDestination> GetLocalDestination () = 0;
		virtual void StopLocalDestination () = 0;
		virtual void Close () { CloseStreams (); };

		void CloseStreams ();

		// Accept-queue transfer API. All methods are safe to call from the SAM
		// service thread and from destination callbacks.
		bool EnqueueAccept (const std::shared_ptr<SAMSocket> & socket, uint64_t now);
		// Returns the oldest queued acceptor (or nullptr). Acceptors do not
		// expire with age, matching Java; the arguments are retained for
		// source compatibility with the former expiry implementation.
		std::shared_ptr<SAMSocket> TakeNextAccept (uint64_t now, std::list<std::shared_ptr<SAMSocket>> & expired);
		std::size_t GetAcceptQueueSize () const;

		// Resolves a datagram destination - a full base64 destination, a plain b32
		// or a blinded b33 .i2p host - the same way a stream connect does, then
		// sends the payload. A b33 lookup is async, so the payload is copied.
		void SendDatagram (std::string_view destination, const uint8_t * payload, size_t len, uint16_t fromPort,
			uint16_t toPort, uint8_t protocol = 0);
	};

	struct SAMSingleSession : public SAMSession
	{
		std::shared_ptr<ClientDestination> localDestination;

		SAMSingleSession (
			SAMBridge & parent, std::string_view name, SAMSessionType type, std::shared_ptr<ClientDestination> dest);
		~SAMSingleSession ();

		std::shared_ptr<ClientDestination> GetLocalDestination () { return localDestination; };
		void StopLocalDestination ();
	};

	struct SAMMasterSession : public SAMSingleSession
	{
		std::set<std::string, std::less<>> subsessions;
		SAMMasterSession (SAMBridge & parent, std::string_view name, std::shared_ptr<ClientDestination> dest)
			: SAMSingleSession (parent, name, SAMSessionType::eSAMSessionTypeMaster, dest) { };
		void Close ();
	};

	struct SAMSubSession : public SAMSession
	{
		std::shared_ptr<SAMMasterSession> masterSession;
		uint16_t inPort;

		SAMSubSession (
			std::shared_ptr<SAMMasterSession> master, std::string_view name, SAMSessionType type, uint16_t port);
		// implements SAMSession
		std::shared_ptr<ClientDestination> GetLocalDestination ();
		void StopLocalDestination ();
	};

	class SAMBridge : private i2p::util::RunnableService
	{
	public:
		SAMBridge (const std::string & address, uint16_t portTCP, uint16_t portUDP, bool singleThread);
		~SAMBridge ();

		void Start ();
		void Stop ();

		auto & GetService () { return GetIOService (); };
		bool IsInServiceThread () const
		{
			std::lock_guard<std::mutex> l (m_ServiceThreadMutex);
			return m_ServiceThreadID == std::this_thread::get_id ();
		}
		std::shared_ptr<SAMSession> CreateSession (std::string_view id, SAMSessionType type,
			std::string_view destination, // empty string means transient
			const i2p::util::Mapping & params);
		bool AddSession (std::shared_ptr<SAMSession> session);
		void CloseSession (std::string_view id);
		std::shared_ptr<SAMSession> FindSession (std::string_view id) const;

		std::list<std::shared_ptr<SAMSocket>> ListSockets (std::string_view id) const;

		/** send raw data to remote endpoint from our UDP Socket. The buffers are
		 * copied, so the caller may release them immediately. Sends are queued
		 * on the SAM service; only one asynchronous send is in flight. */
		void SendTo (const std::vector<boost::asio::const_buffer> & bufs, const boost::asio::ip::udp::endpoint & ep);

		void AddSocket (std::shared_ptr<SAMSocket> socket);
		void RemoveSocket (const std::shared_ptr<SAMSocket> & socket);

		bool ResolveSignatureType (std::string_view name, i2p::data::SigningKeyType & type) const;

	private:
		void Accept ();
		void HandleAccept (const boost::system::error_code & ecode, std::shared_ptr<SAMSocket> socket);

		void ReceiveDatagram ();
		void HandleReceivedDatagram (const boost::system::error_code & ecode, std::size_t bytes_transferred);
		void CloseSessions ();

		void QueueDatagramSend (std::shared_ptr<std::vector<uint8_t>> data,
			const boost::asio::ip::udp::endpoint & ep); // SAM service thread only
		void StartNextDatagramSend ();
		void HandleDatagramSent (const boost::system::error_code & ecode, std::size_t bytes_transferred);

		void ScheduleSessionCleanupTimer (std::shared_ptr<SAMSession> session);
		void HandleSessionCleanupTimer (const boost::system::error_code & ecode, std::shared_ptr<SAMSession> session,
			std::shared_ptr<boost::asio::steady_timer> timer);

	private:
		bool m_IsSingleThread;
		boost::asio::ip::tcp::acceptor m_Acceptor;
		boost::asio::ip::udp::endpoint m_DatagramEndpoint, m_SenderEndpoint;
		boost::asio::ip::udp::socket m_DatagramSocket;
		mutable std::mutex m_SessionsMutex;
		std::map<std::string, std::shared_ptr<SAMSession>, std::less<>> m_Sessions;
		mutable std::mutex m_OpenSocketsMutex;
		std::list<std::shared_ptr<SAMSocket>> m_OpenSockets;
		uint8_t m_DatagramReceiveBuffer[i2p::datagram::MAX_DATAGRAM_SIZE + 1];

		struct DatagramSendItem
		{
			std::shared_ptr<std::vector<uint8_t>> data;
			boost::asio::ip::udp::endpoint endpoint;
		};
		std::deque<DatagramSendItem> m_DatagramSendQueue; // SAM service thread only
		bool m_IsSendingDatagram = false; // SAM service thread only
		mutable std::mutex m_ServiceThreadMutex;
		std::thread::id m_ServiceThreadID;

		const std::map<std::string_view, i2p::data::SigningKeyType> m_SignatureTypes;

	public:
		// for HTTP; thread-safe snapshot, safe to iterate without holding any lock
		std::vector<std::pair<std::string, std::shared_ptr<SAMSession>>> GetSessionsList () const
		{
			std::unique_lock<std::mutex> l (m_SessionsMutex);
			return std::vector<std::pair<std::string, std::shared_ptr<SAMSession>>> (
				m_Sessions.begin (), m_Sessions.end ());
		}
	};
}
}

#endif

/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: BOB.
 *
 * Declares the legacy BOB command channel, per-nickname destinations and the
 * inbound/outbound I2P tunnel classes they manage. The command channel is
 * default-disabled, accepts same-user local peers, bounds its line reader and
 * its concurrent sessions, and never logs or exposes key material unless
 * `bob.allowgetkeys` is explicitly enabled.
 *
 */

#ifndef BOB_H__
#define BOB_H__

#include <inttypes.h>
#include <array>
#include <atomic>
#include <charconv>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <vector>
#include <boost/asio.hpp>
#include "util.h"
#include "I2PTunnel.h"
#include "I2PService.h"
#include "Identity.h"
#include "LeaseSet.h"
#include "SOCKS.h"
#include "HTTPProxy.h"

namespace i2p
{
namespace client
{
	const size_t BOB_COMMAND_BUFFER_SIZE = 1024;
	const int BOB_PING_TIMEOUT = 8000; // in milliseconds

	// A command line is bounded independently of the socket read size: a peer
	// that never sends a newline is disconnected instead of growing a buffer.
	constexpr size_t BOB_MAX_COMMAND_LINE = 4096;
	// Bound on one queued reply (a `list` of every accepted nickname).
	constexpr size_t BOB_MAX_REPLY_SIZE = 8192;
	// Concurrent command sessions. BOB has no per-request authentication, so
	// an unbounded session count is a local denial of service.
	constexpr size_t BOB_MAX_CONCURRENT_SESSIONS = 16;
	// `getkeys` returns a destination's private keys. It stays disabled unless
	// the operator sets the option explicitly.
	constexpr std::string_view BOB_OPTION_ALLOW_GETKEYS { "bob.allowgetkeys" };
	constexpr bool BOB_DEFAULT_ALLOW_GETKEYS = false;

	constexpr std::string_view BOB_COMMAND_ZAP { "zap" };
	constexpr std::string_view BOB_COMMAND_QUIT { "quit" };
	constexpr std::string_view BOB_COMMAND_START { "start" };
	constexpr std::string_view BOB_COMMAND_STOP { "stop" };
	constexpr std::string_view BOB_COMMAND_SETNICK { "setnick" };
	constexpr std::string_view BOB_COMMAND_GETNICK { "getnick" };
	constexpr std::string_view BOB_COMMAND_NEWKEYS { "newkeys" };
	constexpr std::string_view BOB_COMMAND_GETKEYS { "getkeys" };
	constexpr std::string_view BOB_COMMAND_SETKEYS { "setkeys" };
	constexpr std::string_view BOB_COMMAND_GETDEST { "getdest" };
	constexpr std::string_view BOB_COMMAND_OUTHOST { "outhost" };
	constexpr std::string_view BOB_COMMAND_OUTPORT { "outport" };
	constexpr std::string_view BOB_COMMAND_INHOST { "inhost" };
	constexpr std::string_view BOB_COMMAND_INPORT { "inport" };
	constexpr std::string_view BOB_COMMAND_QUIET { "quiet" };
	constexpr std::string_view BOB_COMMAND_LOOKUP { "lookup" };
	constexpr std::string_view BOB_COMMAND_LOOKUP_LOCAL { "lookuplocal" };
	constexpr std::string_view BOB_COMMAND_PING { "ping" };
	constexpr std::string_view BOB_COMMAND_CLEAR { "clear" };
	constexpr std::string_view BOB_COMMAND_LIST { "list" };
	constexpr std::string_view BOB_COMMAND_OPTION { "option" };
	constexpr std::string_view BOB_COMMAND_STATUS { "status" };
	constexpr std::string_view BOB_COMMAND_HELP { "help" };
	constexpr std::string_view BOB_COMMAND_SETTUNNELTYPE { "settunneltype" };

	constexpr std::string_view BOB_HELP_ZAP { "zap - Shuts down BOB." };
	constexpr std::string_view BOB_HELP_QUIT { "quit - Quits this session with BOB." };
	constexpr std::string_view BOB_HELP_START { "start - Starts the current nicknamed tunnel." };
	constexpr std::string_view BOB_HELP_STOP { "stop - Stops the current nicknamed tunnel." };
	constexpr std::string_view BOB_HELP_SETNICK { "setnick <NICKNAME> - Creates a new nickname." };
	constexpr std::string_view BOB_HELP_GETNICK { "getnick <TUNNELNAME> - Sets the nickname from the database." };
	constexpr std::string_view BOB_HELP_NEWKEYS { "newkeys - Generate a new keypair for the current nickname." };
	constexpr std::string_view BOB_HELP_GETKEYS { "getkeys - Return the keypair for the current nickname." };
	constexpr std::string_view BOB_HELP_SETKEYS {
		"setkeys <BASE64_KEYPAIR> - Sets the keypair for the current nickname."
	};
	constexpr std::string_view BOB_HELP_GETDEST { "getdest - Return the destination for the current nickname." };
	constexpr std::string_view BOB_HELP_OUTHOST { "outhost <HOSTNAME|IP> - Set the outhound hostname or IP." };
	constexpr std::string_view BOB_HELP_OUTPORT {
		"outport <PORT_NUMBER> - Set the outbound port that nickname contacts."
	};
	constexpr std::string_view BOB_HELP_INHOST { "inhost <HOSTNAME|IP> - Set the inbound hostname or IP." };
	constexpr std::string_view BOB_HELP_INPORT {
		"inport <PORT_NUMBER> - Set the inbound port number nickname listens on."
	};
	constexpr std::string_view BOB_HELP_QUIET { "quiet <True|False> - Whether to send the incoming destination." };
	constexpr std::string_view BOB_HELP_LOOKUP { "lookup <I2P_HOSTNAME> - Look up an I2P hostname in the network." };
	constexpr std::string_view BOB_HELP_LOOKUP_LOCAL {
		"lookuplocal <I2P_HOSTNAME> - Look up an I2P hostname in local netDb only."
	};
	constexpr std::string_view BOB_HELP_CLEAR { "clear - Clear the current nickname out of the list." };
	constexpr std::string_view BOB_HELP_LIST { "list - List all tunnels." };
	constexpr std::string_view BOB_HELP_OPTION { "option <KEY>=<VALUE> - Set an option. NOTE: Don't use any spaces." };
	constexpr std::string_view BOB_HELP_STATUS { "status <NICKNAME> - Display status of a nicknamed tunnel." };
	constexpr std::string_view BOB_HELP_SETTUNNELTYPE {
		"settunneltype <socks|httpproxy> - Sets socks or http proxy tunnel type."
	};
	constexpr std::string_view BOB_HELP_PING {
		"ping <I2P_HOSTNAME> - Send ping to I2P Hostname. Responds with pong or timeout."
	};
	constexpr std::string_view BOB_HELP_HELP { "help <COMMAND> - Get help on a command." };

	/*
	 * Bounded line accumulator for the command channel.
	 *
	 * Feed() never stores more than one command line plus its terminator, so a
	 * peer that sends no newline is disconnected instead of growing memory, and
	 * a pipelined burst keeps its remaining complete lines queued.
	 */
	class BOBCommandLineReader
	{
	public:
		enum class Result
		{
			Incomplete = 0,
			Complete,
			Overflow
		};

		Result Feed (std::string_view bytes);
		bool HasCompleteLine () const { return m_Pending.find ('\n') != std::string::npos; }
		// Removes and returns the first complete line without its CR/LF ending.
		std::string TakeLine ();
		void Clear () { m_Pending.clear (); }
		size_t GetPendingSize () const { return m_Pending.size (); }

	private:
		std::string m_Pending;
	};

	enum class BOBPortResult
	{
		Ok = 0,
		Empty,
		Invalid,
		OutOfRange
	};

	// Parses a decimal port operand without throwing: a non-numeric, negative
	// or out-of-range value becomes a protocol ERROR reply.
	BOBPortResult ParseBOBPort (std::string_view operand, uint16_t & port);

	// Shared, non-blocking session admission counter.
	struct BOBAdmissionState
	{
		std::atomic<size_t> active { 0 };
	};

	bool TryAcquireBOBSession (BOBAdmissionState & state);
	void ReleaseBOBSession (BOBAdmissionState & state);

	// Fail closed: only the literal `true` from the configuration enables the
	// private-key reply.
	bool ResolveBOBAllowGetKeys (bool optionFound, bool optionValue);

	class BOBI2PTunnelIncomingConnection : public I2PTunnelConnection
	{
	public:
		BOBI2PTunnelIncomingConnection (std::shared_ptr<I2PService> owner, std::shared_ptr<i2p::stream::Stream> stream,
			const boost::asio::ip::tcp::endpoint & target, bool quiet)
			: I2PTunnelConnection (owner, stream, target)
			, m_IsQuiet (quiet) { };

	protected:
		void Established () override;

	private:
		bool m_IsQuiet; // don't send destination
	};

	class BOBI2PTunnel : public I2PService
	{
	public:
		BOBI2PTunnel (std::shared_ptr<ClientDestination> localDestination)
			: I2PService (localDestination) { };

		virtual void Start () { };
		virtual void Stop () { };
	};

	class BOBI2PInboundTunnel : public BOBI2PTunnel
	{
		struct AddressReceiver
		{
			std::shared_ptr<boost::asio::ip::tcp::socket> socket;
			char buffer[BOB_COMMAND_BUFFER_SIZE + 1]; // for destination base64 address
			uint8_t * data; // pointer to buffer
			size_t dataLen, bufferOffset;

			AddressReceiver ()
				: data (nullptr)
				, dataLen (0)
				, bufferOffset (0) { };
		};

	public:
		BOBI2PInboundTunnel (
			const boost::asio::ip::tcp::endpoint & ep, std::shared_ptr<ClientDestination> localDestination);
		~BOBI2PInboundTunnel ();

		void Start ();
		void Stop ();

	private:
		void Accept ();
		void HandleAccept (const boost::system::error_code & ecode, std::shared_ptr<AddressReceiver> receiver);

		void ReceiveAddress (std::shared_ptr<AddressReceiver> receiver);
		void HandleReceivedAddress (const boost::system::error_code & ecode, std::size_t bytes_transferred,
			std::shared_ptr<AddressReceiver> receiver);

		void HandleDestinationRequestComplete (
			std::shared_ptr<i2p::data::LeaseSet> leaseSet, std::shared_ptr<AddressReceiver> receiver);

		void CreateConnection (
			std::shared_ptr<AddressReceiver> receiver, std::shared_ptr<const i2p::data::LeaseSet> leaseSet);
		void CloseReceiver (const std::shared_ptr<AddressReceiver> & receiver);

	private:
		boost::asio::ip::tcp::acceptor m_Acceptor;
		std::atomic_bool m_Stopped { true };
		std::mutex m_AdmissionMutex;
		std::set<std::shared_ptr<AddressReceiver>, std::owner_less<std::shared_ptr<AddressReceiver>>> m_Receivers;
	};

	class BOBI2POutboundTunnel : public BOBI2PTunnel
	{
	public:
		BOBI2POutboundTunnel (const std::string & outhost, uint16_t port,
			std::shared_ptr<ClientDestination> localDestination, bool quiet);

		void Start ();
		void Stop ();

		void SetQuiet () { m_IsQuiet = true; };

	private:
		void Accept ();
		void HandleAccept (std::shared_ptr<i2p::stream::Stream> stream);

	private:
		boost::asio::ip::tcp::endpoint m_Endpoint;
		bool m_IsQuiet;
		std::atomic_bool m_Stopped { true };
		std::mutex m_AdmissionMutex;
	};

	/// One BOB nickname: a client destination plus the optional inbound/outbound
	/// tunnel pair another process can attach to by connecting locally.
	/// Start()/Stop() own both tunnel objects. A host, port or quiet change is
	/// only applied while the corresponding tunnel object is absent, i.e. after
	/// an explicit stop, so a running tunnel is never mutated underneath its
	/// handlers; StopTunnels() clears both objects and the running flag.
	class BOBDestination
	{
	public:
		BOBDestination (std::shared_ptr<ClientDestination> localDestination, const std::string & nickname,
			const std::string & inhost, const std::string & outhost, const uint16_t inport, const uint16_t outport,
			const bool quiet);
		~BOBDestination ();

		void Start ();
		void Stop ();
		void StopTunnels ();
		void CreateInboundTunnel (uint16_t port, const std::string & inhost);
		void CreateOutboundTunnel (const std::string & outhost, uint16_t port, bool quiet);
		const std::string & GetNickname () const { return m_Nickname; }
		const std::string & GetInHost () const { return m_InHost; }
		const std::string & GetOutHost () const { return m_OutHost; }
		uint16_t GetInPort () const { return m_InPort; }
		uint16_t GetOutPort () const { return m_OutPort; }
		bool GetQuiet () const { return m_Quiet; }
		bool IsRunning () const { return m_IsRunning; }
		const i2p::data::PrivateKeys & GetKeys () const { return m_LocalDestination->GetPrivateKeys (); };
		std::shared_ptr<ClientDestination> GetLocalDestination () const { return m_LocalDestination; };

	private:
		std::shared_ptr<ClientDestination> m_LocalDestination;
		std::shared_ptr<BOBI2POutboundTunnel> m_OutboundTunnel;
		std::shared_ptr<BOBI2PInboundTunnel> m_InboundTunnel;

		std::string m_Nickname;
		std::string m_InHost, m_OutHost;
		uint16_t m_InPort, m_OutPort;
		bool m_Quiet;
		bool m_IsRunning;
	};

	class BOBCommandChannel;
	class BOBCommandSession : public std::enable_shared_from_this<BOBCommandSession>
	{
		enum class TunnelType
		{
			STANDARD = 0,
			SOCKS = 1,
			HTTP_PROXY = 2
		};

	public:
		BOBCommandSession (BOBCommandChannel & owner);
		~BOBCommandSession ();
		void Terminate ();
		// Claims one admission slot. Returns false when the channel is at its
		// concurrent-session limit; the caller then closes the socket.
		bool Admit (const std::shared_ptr<BOBAdmissionState> & admission);

		boost::asio::ip::tcp::socket & GetSocket () { return m_Socket; };
		void SendVersion ();

		// command handlers
		void ZapCommandHandler (const char * operand, size_t len);
		void QuitCommandHandler (const char * operand, size_t len);
		void StartCommandHandler (const char * operand, size_t len);
		void StopCommandHandler (const char * operand, size_t len);
		void SetNickCommandHandler (const char * operand, size_t len);
		void GetNickCommandHandler (const char * operand, size_t len);
		void NewkeysCommandHandler (const char * operand, size_t len);
		void SetkeysCommandHandler (const char * operand, size_t len);
		void GetkeysCommandHandler (const char * operand, size_t len);
		void GetdestCommandHandler (const char * operand, size_t len);
		void OuthostCommandHandler (const char * operand, size_t len);
		void OutportCommandHandler (const char * operand, size_t len);
		void InhostCommandHandler (const char * operand, size_t len);
		void InportCommandHandler (const char * operand, size_t len);
		void QuietCommandHandler (const char * operand, size_t len);
		void LookupCommandHandler (const char * operand, size_t len);
		void LookupLocalCommandHandler (const char * operand, size_t len);
		void PingCommandHandler (const char * operand, size_t len);
		void ClearCommandHandler (const char * operand, size_t len);
		void ListCommandHandler (const char * operand, size_t len);
		void OptionCommandHandler (const char * operand, size_t len);
		void StatusCommandHandler (const char * operand, size_t len);
		void HelpCommandHandler (const char * operand, size_t len);
		void SetTunnelTypeCommandHandler (const char * operand, size_t len);

	private:
		void Receive ();
		void Read ();
		void HandleReceivedBytes (const boost::system::error_code & ecode, std::size_t bytes_transferred);
		void HandleLine ();

		void Send ();
		void HandleSent (const boost::system::error_code & ecode, std::size_t bytes_transferred);
		void SendReplyOK (std::string_view msg);
		void SendReplyOK (const std::vector<std::string_view> & strings);
		void SendReplyError (std::string_view msg);
		void SendRaw (std::string_view data);

		std::string BuildStatusLine (bool currentTunnel, std::shared_ptr<BOBDestination> destination);

		void SendPing (std::shared_ptr<const i2p::data::LeaseSet> ls);

	private:
		BOBCommandChannel & m_Owner;
		boost::asio::ip::tcp::socket m_Socket;
		std::array<char, BOB_COMMAND_BUFFER_SIZE> m_ReadBuffer;
		BOBCommandLineReader m_LineReader;
		boost::asio::streambuf m_SendBuffer;
		std::shared_ptr<BOBAdmissionState> m_Admission;
		bool m_Admitted = false;
		bool m_IsOpen, m_IsQuiet, m_IsActive;
		std::string m_Nickname, m_InHost, m_OutHost;
		uint16_t m_InPort, m_OutPort;
		i2p::data::PrivateKeys m_Keys;
		i2p::util::Mapping m_Options;
		std::shared_ptr<BOBDestination> m_CurrentDestination;
		TunnelType m_TunnelType;
	};
	typedef void (BOBCommandSession::*BOBCommandHandler) (const char * operand, size_t len);

	/// Legacy BOB command channel.
	///
	/// A line-oriented TCP protocol (default bind 127.0.0.1, disabled by
	/// default) whose sessions act on the process-global client context: they
	/// create and start nicknamed destinations and their tunnel direction, and
	/// register the SOCKS/HTTP proxies they start.
	///
	/// The protocol carries no authentication, so it is only safe on a local
	/// loopback socket. The channel therefore bounds what a local peer can
	/// consume instead: one command line (BOB_MAX_COMMAND_LINE), one queued
	/// reply (BOB_MAX_REPLY_SIZE) and a fixed number of concurrent sessions
	/// (BOB_MAX_CONCURRENT_SESSIONS). The private-key reply is denied unless
	/// `bob.allowgetkeys=true` was configured at startup, and no command
	/// operand, key or credential is written to a log.
	///
	/// @warning Do not bind this channel to a non-loopback address: any local
	///   process that can connect can otherwise start and stop tunnels and
	///   enumerate them.
	class BOBCommandChannel : private i2p::util::RunnableService
	{
	public:
		BOBCommandChannel (const std::string & address, uint16_t port);
		~BOBCommandChannel ();

		void Start ();
		void Stop ();

		auto & GetService () { return GetIOService (); };
		void AddDestination (const std::string & name, std::shared_ptr<BOBDestination> dest);
		void DeleteDestination (const std::string & name);
		std::shared_ptr<BOBDestination> FindDestination (const std::string & name);
		void AddProxy (const std::string & name, std::shared_ptr<I2PService> && proxy, std::string_view type);
		std::pair<std::shared_ptr<I2PService>, std::string> GetProxy (const std::string & name) const;
		void RemoveProxy (const std::string & name);
		// True only when `bob.allowgetkeys=true` was configured at startup.
		bool IsGetKeysAllowed () const { return m_AllowGetKeys; }

	private:
		void Accept ();
		void HandleAccept (const boost::system::error_code & ecode, std::shared_ptr<BOBCommandSession> session);

	private:
		boost::asio::ip::tcp::acceptor m_Acceptor;
		std::shared_ptr<BOBAdmissionState> m_Admission { std::make_shared<BOBAdmissionState> () };
		bool m_AllowGetKeys = BOB_DEFAULT_ALLOW_GETKEYS;
		std::map<std::string, std::shared_ptr<BOBDestination>> m_Destinations;
		std::map<std::string_view, BOBCommandHandler> m_CommandHandlers;
		std::map<std::string_view, std::string_view> m_HelpStrings;
		// shared, a proxy takes a reference to itself when it accepts a connection
		std::map<std::string, std::pair<std::shared_ptr<I2PService>, std::string>>
			m_Proxies; // name ->(tunnel, proxy type)

	public:
		const decltype (m_CommandHandlers) & GetCommandHandlers () const { return m_CommandHandlers; };
		const decltype (m_HelpStrings) & GetHelpStrings () const { return m_HelpStrings; };
		const decltype (m_Destinations) & GetDestinations () const { return m_Destinations; };
	};
}
}

#endif

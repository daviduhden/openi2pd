/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: SOCKS.
 *
 * Implements SOCKS4/4a and SOCKS5 CONNECT, torsocks address resolution and
 * UDP ASSOCIATE, resolving .i2p names through the address book and optionally
 * forwarding non-I2P traffic to an upstream SOCKS proxy. Handshakes are parsed
 * incrementally with per-request validation. Optional local RFC 1929
 * username/password authentication is negotiated before any request byte is
 * accepted and rejects SOCKS4, which has no credential exchange.
 *
 */

#include <algorithm>
#include <chrono>
#include <cstring>
#include <cassert>
#include <array>
#include <string>
#include <string_view>
#include <vector>
#include <atomic>
#include <boost/algorithm/string/predicate.hpp>
#include <openssl/crypto.h>
#include <openssl/rand.h>
#include "SOCKS.h"
#include "SOCKSAddress.h"
#include "Identity.h"
#include "Streaming.h"
#include "Destination.h"
#include "ClientContext.h"
#include "I2PEndian.h"
#include "I2PTunnel.h"
#include "I2PService.h"
#include "util.h"
#include "Config.h"
#include "Socks5.h"
#include "UDPTunnel.h"

namespace i2p
{
namespace proxy
{
	namespace
	{
		// A client that opens a SOCKS connection but never finishes the version,
		// method or request exchange must not keep the handler allocated.
		constexpr int SOCKS_HANDSHAKE_TIMEOUT = 30; // seconds
	}
	static const size_t socks_buffer_size = 8192;

	class SOCKSServer;
	class SOCKSHandler : public i2p::client::I2PServiceHandler, public std::enable_shared_from_this<SOCKSHandler>
	{
	private:
		enum state
		{
			GET_SOCKSV,
			GET_COMMAND,
			GET_PORT,
			GET_IPV4,
			GET4_IDENT,
			GET4A_HOST,
			GET5_AUTHNUM,
			GET5_AUTH,
			GET5_REQUESTV,
			GET5_GETRSV,
			GET5_GETADDRTYPE,
			GET5_IPV6,
			GET5_HOST_SIZE,
			GET5_HOST,
			GET5_USERPASSWD,
			GET5_USER_SIZE,
			GET5_USER,
			GET5_PASSWD_SIZE,
			GET5_PASSWD,
			READY,
			UPSTREAM_RESOLVE,
			UPSTREAM_CONNECT,
			UPSTREAM_HANDSHAKE
		};
		enum authMethods
		{
			AUTH_NONE = 0, //No authentication, skip to next step
			AUTH_GSSAPI = 1, //GSSAPI authentication
			AUTH_USERPASSWD = 2, //Username and password
			AUTH_UNACCEPTABLE = 0xff //No acceptable method found
		};
		enum addrTypes
		{
			ADDR_IPV4 = 1, //IPv4 address (4 octets)
			ADDR_DNS = 3, // DNS name (up to 255 octets)
			ADDR_IPV6 = 4 //IPV6 address (16 octets)
		};
		enum errTypes
		{
			SOCKS5_OK = 0, // No error for SOCKS5
			SOCKS5_GEN_FAIL = 1, // General server failure
			SOCKS5_RULE_DENIED = 2, // Connection disallowed by ruleset
			SOCKS5_NET_UNREACH = 3, // Network unreachable
			SOCKS5_HOST_UNREACH = 4, // Host unreachable
			SOCKS5_CONN_REFUSED = 5, // Connection refused by the peer
			SOCKS5_TTL_EXPIRED = 6, // TTL Expired
			SOCKS5_CMD_UNSUP = 7, // Command unsupported
			SOCKS5_ADDR_UNSUP = 8, // Address type unsupported
			SOCKS4_OK = 90, // No error for SOCKS4
			SOCKS4_FAIL = 91, // Failed establishing connecting or not allowed
			SOCKS4_IDENTD_MISSING = 92, // Couldn't connect to the identd server
			SOCKS4_IDENTD_DIFFER = 93 // The ID reported by the application and by identd differ
		};
		enum cmdTypes
		{
			CMD_CONNECT = 1, // TCP Connect
			CMD_BIND = 2, // TCP Bind
			CMD_UDP = 3, // UDP associate
			CMD_RESOLVE = 0xF0 // for torsocks
		};
		enum socksVersions
		{
			SOCKS4 = 4, // SOCKS4
			SOCKS5 = 5 // SOCKS5
		};
		bool IsDeniedSocksTarget (std::string_view host, uint16_t port)
		{
			if (!port) return true;
			std::string value (host);
			if (value.size () >= 2 && value.front () == '[' && value.back () == ']')
				value = value.substr (1, value.size () - 2);
			if (boost::iequals (value, "localhost") || i2p::client::HasSuffixIgnoreCase (value, ".localhost"))
				return true;
			boost::system::error_code ec;
			auto address = boost::asio::ip::make_address (value, ec);
			if (ec) return false;
			if (address.is_v4 ())
			{
				auto b = address.to_v4 ().to_bytes ();
				return b[0] == 0 || b[0] == 10 || b[0] == 127 || (b[0] == 169 && b[1] == 254)
					|| (b[0] == 172 && b[1] >= 16 && b[1] <= 31) || (b[0] == 192 && b[1] == 168)
					|| address.to_v4 ().is_multicast ();
			}
			auto b = address.to_v6 ().to_bytes ();
			return address.to_v6 ().is_loopback () || address.to_v6 ().is_unspecified ()
				|| address.to_v6 ().is_multicast () || (b[0] == 0xfe && (b[1] & 0xc0) == 0x80) || (b[0] & 0xfe) == 0xfc;
		}

		union address
		{
			uint32_t ip;
			SOCKSDnsAddress dns;
			uint8_t ipv6[16];
		};

		void EnterState (state nstate, uint8_t parseleft = 1);
		bool HandleData (uint8_t * sock_buff, std::size_t len);
		bool ValidateSOCKSRequest ();
		void HandleSockRecv (const boost::system::error_code & ecode, std::size_t bytes_transfered);
		void Terminate ();
		void AsyncSockRead ();
		std::shared_ptr<SOCKSServer> GetServer () { return std::static_pointer_cast<SOCKSServer> (GetOwner ()); };
		boost::asio::const_buffer GenerateSOCKS4Response (errTypes error, uint32_t ip, uint16_t port);
		boost::asio::const_buffer GenerateSOCKS5Response (
			errTypes error, addrTypes type, const address & addr, uint16_t port);
		bool Socks5ChooseAuth ();
		authMethods ChooseAuthMethod () const;
		void Socks5UserPasswdResponse (bool success);
		/** Zero the decoded RFC 1929 fields; called after verification and on stop. */
		void WipeCredentialFields () noexcept;
		bool VerifyUserPass ();
		void SocksRequestFailed (errTypes error);
		void SocksRequestSuccess ();
		void SentSocksFailed (const boost::system::error_code & ecode);
		void SentSocksDone (const boost::system::error_code & ecode);
		void SentSocksResponse (const boost::system::error_code & ecode);
		void ArmHandshakeDeadline ();
		void HandleHandshakeDeadline (const boost::system::error_code & ecode);
		void HandleStreamRequestComplete (std::shared_ptr<i2p::stream::Stream> stream);
		void ForwardSOCKS ();

		template <typename Socket> void SocksUpstreamSuccess (std::shared_ptr<Socket> & upstreamSock);
		template <typename Socket> void SendUpstreamRequest (std::shared_ptr<Socket> & upstreamSock);
		void HandleUpstreamConnected (
			const boost::system::error_code & ecode, const boost::asio::ip::tcp::endpoint & ep);
		void HandleUpstreamResolved (
			const boost::system::error_code & ecode, boost::asio::ip::tcp::resolver::results_type endpoints);

		boost::asio::ip::tcp::resolver m_proxy_resolver;
		boost::asio::steady_timer m_HandshakeTimer;
		uint8_t m_sock_buff[socks_buffer_size];
		std::shared_ptr<boost::asio::ip::tcp::socket> m_sock, m_upstreamSock;
#if defined(BOOST_ASIO_HAS_LOCAL_SOCKETS)
		std::shared_ptr<boost::asio::local::stream_protocol::socket> m_upstreamLocalSock;
#endif
		std::shared_ptr<i2p::stream::Stream> m_stream;
		uint8_t * m_remaining_data; //Data left to be sent
		uint8_t * m_remaining_upstream_data; //upstream data left to be forwarded
		uint8_t m_response[7 + SOCKS_MAX_HOSTNAME_SIZE];
		address m_address { }; //Address
		std::size_t m_remaining_data_len; //Size of the data left to be sent
		uint32_t m_4aip; //Used in 4a requests
		uint16_t m_port;
		uint8_t m_command;
		uint8_t m_parseleft; //Octets left to parse
		authMethods m_authchosen; //Authentication chosen
		addrTypes m_addrtype; //Address type chosen
		socksVersions m_socksv; //Socks version
		cmdTypes m_cmd; // Command requested
		state m_state;
		const bool m_UseUpstreamProxy; // do we want to use the upstream proxy for non i2p addresses?
		const std::string m_UpstreamProxyAddress;
		const uint16_t m_UpstreamProxyPort;
		// Local RFC 1929 credential, snapshotted from the acceptor at accept time.
		SOCKSUserPassAuth m_Auth;
		std::array<char, SOCKS_AUTH_BUFFER_SIZE> m_authUser { };
		std::array<char, SOCKS_AUTH_BUFFER_SIZE> m_authPassword { };
		std::size_t m_authUserLen = 0;
		std::size_t m_authPasswordLen = 0;
		bool m_authOfferedNone = false;
		bool m_authOfferedUserPass = false;
		bool m_authVerified = false;
		// Bytes that arrived in the same read as the credentials or the method
		// list. They are parsed only after the corresponding reply write has
		// completed, because every SOCKS reply reuses m_response and Asio does
		// not allow two overlapping async_write operations on one socket.
		std::vector<uint8_t> m_DeferredAuthInput;
		std::shared_ptr<i2p::client::I2PUDPClientTunnel> m_UDPTunnel; // shared, the tunnel takes references to itself

	public:
		SOCKSHandler (std::shared_ptr<i2p::client::I2PService> parent,
			std::shared_ptr<boost::asio::ip::tcp::socket> sock, const std::string & upstreamAddr,
			const uint16_t upstreamPort, const bool useUpstream)
			: I2PServiceHandler (parent)
			, m_proxy_resolver (parent->GetService ())
			, m_HandshakeTimer (parent->GetService ())
			, m_sock (sock)
			, m_stream (nullptr)
			, m_remaining_data (nullptr)
			, m_remaining_upstream_data (nullptr)
			, m_remaining_data_len (0)
			, m_4aip (0)
			, m_port (0)
			, m_command (0)
			, m_parseleft (0)
			, m_authchosen (AUTH_UNACCEPTABLE)
			, m_addrtype (ADDR_IPV4)
			, m_socksv (SOCKS5)
			, m_cmd (CMD_CONNECT)
			, m_state (GET_SOCKSV)
			, m_UseUpstreamProxy (useUpstream)
			, m_UpstreamProxyAddress (upstreamAddr)
			, m_UpstreamProxyPort (upstreamPort)
			, m_Auth (std::static_pointer_cast<SOCKSServer> (parent)->GetAuth ())
		{
			m_address.ip = 0;
			EnterState (GET_SOCKSV);
		}

		~SOCKSHandler () { Terminate (); }
		void Handle () override
		{
			ArmHandshakeDeadline ();
			AsyncSockRead ();
		}
	};

	void SOCKSHandler::ArmHandshakeDeadline ()
	{
		auto weak = weak_from_this ();
		m_HandshakeTimer.expires_after (std::chrono::seconds (SOCKS_HANDSHAKE_TIMEOUT));
		m_HandshakeTimer.async_wait (
			[weak] (const boost::system::error_code & ecode)
			{
				if (ecode) return;
				auto self = weak.lock ();
				if (self) self->HandleHandshakeDeadline (boost::system::error_code ());
			});
	}

	void SOCKSHandler::HandleHandshakeDeadline (const boost::system::error_code &)
	{
		// Terminate() is idempotent and owns the Kill() transition; clearing the
		// flag here first would make it return without closing the socket.
		LogPrint (eLogWarning, "SOCKS: handshake was not completed in ", SOCKS_HANDSHAKE_TIMEOUT,
			" seconds, closing connection");
		Terminate ();
	}

	void SOCKSHandler::AsyncSockRead ()
	{
		LogPrint (eLogDebug, "SOCKS: Async sock read");
		if (m_sock)
		{
			m_sock->async_receive (boost::asio::buffer (m_sock_buff, socks_buffer_size),
				std::bind (
					&SOCKSHandler::HandleSockRecv, shared_from_this (), std::placeholders::_1, std::placeholders::_2));
		}
		else
		{
			LogPrint (eLogError, "SOCKS: No socket for read");
		}
	}

	void SOCKSHandler::Terminate ()
	{
		m_HandshakeTimer.cancel ();
		// Any decoded credential is dropped before the socket state changes.
		WipeCredentialFields ();
		if (!m_DeferredAuthInput.empty ())
		{
			OPENSSL_cleanse (m_DeferredAuthInput.data (), m_DeferredAuthInput.size ());
			m_DeferredAuthInput.clear ();
		}
		if (Kill ()) return;
		if (m_sock)
		{
			LogPrint (eLogDebug, "SOCKS: Closing socket");
			m_sock->close ();
			m_sock = nullptr;
		}
		if (m_upstreamSock)
		{
			LogPrint (eLogDebug, "SOCKS: Closing upstream socket");
			m_upstreamSock->close ();
			m_upstreamSock = nullptr;
		}
#if defined(BOOST_ASIO_HAS_LOCAL_SOCKETS)
		if (m_upstreamLocalSock)
		{
			LogPrint (eLogDebug, "SOCKS: Closing upstream local socket");
			m_upstreamLocalSock->close ();
			m_upstreamLocalSock = nullptr;
		}
#endif
		if (m_stream)
		{
			LogPrint (eLogDebug, "SOCKS: Closing stream");
			m_stream->AsyncClose ();
			m_stream = nullptr;
		}
		if (m_UDPTunnel)
		{
			m_UDPTunnel->Stop ();
			GetServer ()->ReleaseLocalUDPPort (m_UDPTunnel->GetLocalEndpoint ().port ());
			m_UDPTunnel = nullptr;
		}
		Done (shared_from_this ());
	}

	boost::asio::const_buffer SOCKSHandler::GenerateSOCKS4Response (
		SOCKSHandler::errTypes error, uint32_t ip, uint16_t port)
	{
		assert (error >= SOCKS4_OK);
		m_response[0] = '\x00'; // version
		m_response[1] = static_cast<uint8_t> (error); // response code
		htobe16buf (m_response + 2, port); // port
		htobe32buf (m_response + 4, ip); // IP
		return boost::asio::const_buffer (m_response, 8);
	}

	boost::asio::const_buffer SOCKSHandler::GenerateSOCKS5Response (
		SOCKSHandler::errTypes error, SOCKSHandler::addrTypes type, const SOCKSHandler::address & addr, uint16_t port)
	{
		size_t size = 6; // header + port
		assert (error <= SOCKS5_ADDR_UNSUP);
		m_response[0] = '\x05'; // version
		m_response[1] = static_cast<uint8_t> (error); // response code
		m_response[2] = '\x00'; // reserved
		m_response[3] = static_cast<uint8_t> (type); // address type
		switch (type)
		{
		case ADDR_IPV4:
			size += 4;
			htobe32buf (m_response + 4, addr.ip);
			htobe16buf (m_response + size - 2, port);
			break;
		case ADDR_IPV6:
			size += 16;
			memcpy (m_response + 4, addr.ipv6, 16);
			htobe16buf (m_response + size - 2, port);
			break;
		case ADDR_DNS:
			std::string address (addr.dns.value, addr.dns.size);
			if (addr.dns.size >= 4
				&& address.substr (addr.dns.size - 4, 4) == ".i2p") // overwrite if requested address inside I2P
			{
				m_response[3] = ADDR_IPV4;
				size += 4;
				memset (m_response + 4, 0, 6); // six HEX zeros
			}
			else
			{
				size += (1 + addr.dns.size); /* name length + resolved address */
				m_response[4] = addr.dns.size;
				memcpy (m_response + 5, addr.dns.value, addr.dns.size);
				htobe16buf (m_response + size - 2, port);
			}
			break;
		}
		return boost::asio::const_buffer (m_response, size);
	}

	SOCKSHandler::authMethods SOCKSHandler::ChooseAuthMethod () const
	{
		switch (SOCKSChooseAuthMethod (m_Auth.IsEnabled (), m_authOfferedNone, m_authOfferedUserPass))
		{
		case SOCKSAuthMethodChoice::None: return AUTH_NONE;
		case SOCKSAuthMethodChoice::UserPass: return AUTH_USERPASSWD;
		case SOCKSAuthMethodChoice::Unacceptable: break;
		}
		return AUTH_UNACCEPTABLE;
	}

	void SOCKSHandler::WipeCredentialFields () noexcept
	{
		OPENSSL_cleanse (m_authUser.data (), m_authUser.size ());
		OPENSSL_cleanse (m_authPassword.data (), m_authPassword.size ());
		m_authUserLen = 0;
		m_authPasswordLen = 0;
	}

	bool SOCKSHandler::VerifyUserPass ()
	{
		// m_Auth.Verify() is constant-time and fail-closed when the local
		// credential is unusable. Only the non-secret outcome is logged.
		const bool accepted = m_Auth.Verify (std::string_view (m_authUser.data (), m_authUserLen),
			std::string_view (m_authPassword.data (), m_authPasswordLen));
		WipeCredentialFields ();
		if (accepted)
			LogPrint (eLogDebug, "SOCKS: v5 user/password authentication accepted");
		else
			LogPrint (eLogWarning, "SOCKS: v5 user/password authentication failed");
		return accepted;
	}

	bool SOCKSHandler::Socks5ChooseAuth ()
	{
		m_response[0] = '\x05'; // Version
		m_response[1] = static_cast<uint8_t> (m_authchosen); // Response code
		boost::asio::const_buffer response (m_response, 2);
		if (m_authchosen == AUTH_UNACCEPTABLE)
		{
			LogPrint (eLogWarning, "SOCKS: v5 authentication negotiation failed");
			boost::asio::async_write (*m_sock, response,
				std::bind (&SOCKSHandler::SentSocksFailed, shared_from_this (), std::placeholders::_1));
			return false;
		}
		else
		{
			LogPrint (eLogDebug, "SOCKS: v5 choosing authentication method: ", m_authchosen);
			boost::asio::async_write (*m_sock, response,
				std::bind (&SOCKSHandler::SentSocksResponse, shared_from_this (), std::placeholders::_1));
			return true;
		}
	}

	void SOCKSHandler::Socks5UserPasswdResponse (bool success)
	{
		m_response[0] = 1; // Version of the subnegotiation
		m_response[1] = success ? 0 : 1; // RFC 1929 STATUS
		LogPrint (eLogDebug, "SOCKS: v5 user/password response: ", success ? "accepted" : "rejected");
		auto response = boost::asio::const_buffer (m_response, 2);
		if (success)
			// Keep the connection: the request follows in the same stream.
			boost::asio::async_write (*m_sock, response,
				std::bind (&SOCKSHandler::SentSocksResponse, shared_from_this (), std::placeholders::_1));
		else
			// A rejected sub-negotiation always closes the connection, so a
			// pipelined request behind it can never be served.
			boost::asio::async_write (*m_sock, response,
				std::bind (&SOCKSHandler::SentSocksFailed, shared_from_this (), std::placeholders::_1));
	}

	/* All hope is lost beyond this point */
	void SOCKSHandler::SocksRequestFailed (SOCKSHandler::errTypes error)
	{
		boost::asio::const_buffer response (nullptr, 0);
		assert (error != SOCKS4_OK && error != SOCKS5_OK);
		switch (m_socksv)
		{
		case SOCKS4:
			LogPrint (eLogWarning, "SOCKS: v4 request failed: ", error);
			if (error < SOCKS4_OK) error = SOCKS4_FAIL; // Transparently map SOCKS5 errors
			response = GenerateSOCKS4Response (error, m_4aip, m_port);
			break;
		case SOCKS5:
			LogPrint (eLogWarning, "SOCKS: v5 request failed: ", error);
			response = GenerateSOCKS5Response (error, m_addrtype, m_address, m_port);
			break;
		}
		boost::asio::async_write (
			*m_sock, response, std::bind (&SOCKSHandler::SentSocksFailed, shared_from_this (), std::placeholders::_1));
	}

	void SOCKSHandler::SocksRequestSuccess ()
	{
		boost::asio::const_buffer response (nullptr, 0);
		// TODO: this should depend on things like the command type and callbacks may change
		switch (m_socksv)
		{
		case SOCKS4:
			LogPrint (eLogInfo, "SOCKS: v4 connection success");
			response = GenerateSOCKS4Response (SOCKS4_OK, m_4aip, m_port);
			break;
		case SOCKS5:
			LogPrint (eLogInfo, "SOCKS: v5 connection success");
			if (m_cmd == CMD_UDP && m_UDPTunnel)
			{
				// RFC 1928 BND.ADDR/BND.PORT of the UDP relay: the loopback
				// address, never a wildcard, so the client has an address it
				// can actually send to (matches I2P Java handleUDP()).
				const auto reply = SOCKS5UdpAssociateReply (m_UDPTunnel->GetLocalEndpoint ().port ());
				std::copy (reply.begin (), reply.end (), m_response);
				response = boost::asio::const_buffer (m_response, reply.size ());
			}
			else
			{
				// A successful CONNECT has no meaningful bound endpoint on an
				// I2P stream. 127.0.0.1:1 is the I2P Java confirmConnection()
				// reply; the previous own-b32/truncated-stream-ID trick could
				// emit a wildcard address and a meaningless port.
				const auto reply = SOCKS5ConnectSuccessReply ();
				std::copy (reply.begin (), reply.end (), m_response);
				response = boost::asio::const_buffer (m_response, reply.size ());
			}
			break;
		}
		boost::asio::async_write (
			*m_sock, response, std::bind (&SOCKSHandler::SentSocksDone, shared_from_this (), std::placeholders::_1));
	}

	void SOCKSHandler::EnterState (SOCKSHandler::state nstate, uint8_t parseleft)
	{
		switch (nstate)
		{
		case GET_PORT: parseleft = 2; break;
		case GET_IPV4:
			m_addrtype = ADDR_IPV4;
			m_address.ip = 0;
			parseleft = 4;
			break;
		case GET4_IDENT: m_4aip = m_address.ip; break;
		case GET4A_HOST:
		case GET5_HOST:
			m_addrtype = ADDR_DNS;
			m_address.dns.size = 0;
			break;
		case GET5_IPV6:
			m_addrtype = ADDR_IPV6;
			parseleft = 16;
			break;
		default:;
		}
		m_parseleft = parseleft;
		m_state = nstate;
	}

	bool SOCKSHandler::ValidateSOCKSRequest ()
	{
		if (m_cmd != CMD_CONNECT && m_cmd != CMD_UDP && m_cmd != CMD_RESOLVE)
		{
			LogPrint (eLogError, "SOCKS: Unsupported command: ", m_cmd);
			SocksRequestFailed (SOCKS5_CMD_UNSUP);
			return false;
		}
		// Convert IP literals to their textual form. The request path already
		// supports name-based upstream forwarding, and this matches Java's
		// SOCKS4/5 IP-literal handling.
		if (m_addrtype == ADDR_IPV4)
		{
			auto value = boost::asio::ip::address_v4 (m_address.ip).to_string ();
			if (!m_address.dns.SetString (value))
			{
				SocksRequestFailed (SOCKS5_ADDR_UNSUP);
				return false;
			}
			m_addrtype = ADDR_DNS;
		}
		else if (m_addrtype == ADDR_IPV6)
		{
			boost::asio::ip::address_v6::bytes_type bytes;
			memcpy (bytes.data (), m_address.ipv6, bytes.size ());
			auto value = boost::asio::ip::address_v6 (bytes).to_string ();
			if (!m_address.dns.SetString (value))
			{
				SocksRequestFailed (SOCKS5_ADDR_UNSUP);
				return false;
			}
			m_addrtype = ADDR_DNS;
		}
		if (m_addrtype != ADDR_DNS)
		{
			SocksRequestFailed (SOCKS5_ADDR_UNSUP);
			return false;
		}
		const auto host = m_address.dns.GetString ();
		if (m_cmd != CMD_RESOLVE && !i2p::client::IsI2PHost (host) && IsDeniedSocksTarget (host, m_port))
		{
			LogPrint (eLogWarning, "SOCKS: Denied loopback/private target ", host, ":", m_port);
			SocksRequestFailed (SOCKS5_RULE_DENIED);
			return false;
		}
		return true;
	}

	bool SOCKSHandler::HandleData (uint8_t * sock_buff, std::size_t len)
	{
		assert (len); // This should always be called with a least a byte left to parse
		while (len > 0)
		{
			switch (m_state)
			{
			case GET_SOCKSV:
				m_socksv = (SOCKSHandler::socksVersions)*sock_buff;
				switch (*sock_buff)
				{
				case SOCKS4:
					// SOCKS4 has no credential exchange. With local
					// authentication enabled it fails closed instead of
					// passing an unauthenticated client to the request parser.
					if (m_Auth.IsEnabled ())
					{
						LogPrint (eLogWarning, "SOCKS: Rejecting SOCKS4 because local authentication is enabled");
						SocksRequestFailed (SOCKS4_FAIL);
						return false;
					}
					EnterState (GET_COMMAND); //Initialize the parser at the right position
					break;
				case SOCKS5:
					EnterState (GET5_AUTHNUM); //Initialize the parser at the right position
					break;
				default:
					LogPrint (eLogError, "SOCKS: Rejected invalid version: ", ((int)*sock_buff));
					Terminate ();
					return false;
				}
				break;
			case GET5_AUTHNUM:
				// `05 00` (an empty method list) is invalid: without this check
				// m_parseleft would underflow and treat following bytes as
				// offered methods.
				if (!*sock_buff)
				{
					LogPrint (eLogWarning, "SOCKS: v5 empty authentication method list");
					m_authchosen = AUTH_UNACCEPTABLE;
					Socks5ChooseAuth ();
					return false;
				}
				EnterState (GET5_AUTH, *sock_buff);
				break;
			case GET5_AUTH:
				m_parseleft--;
				// Remember every offered method and decide once the list ends,
				// so an early 0x00 cannot override a later 0x02.
				if (*sock_buff == AUTH_NONE)
					m_authOfferedNone = true;
				else if (*sock_buff == AUTH_USERPASSWD)
					m_authOfferedUserPass = true;
				if (m_parseleft == 0)
				{
					m_authchosen = ChooseAuthMethod ();
					if (!Socks5ChooseAuth ()) return false;
					if (m_authchosen == AUTH_USERPASSWD)
						EnterState (GET5_USERPASSWD);
					else
						EnterState (GET5_REQUESTV);
					m_DeferredAuthInput.assign (sock_buff + 1, sock_buff + len);
					return false; // resume in SentSocksResponse
				}
				break;
			case GET_COMMAND:
				switch (*sock_buff)
				{
				case CMD_CONNECT:
				case CMD_BIND:
				case CMD_RESOLVE: break;
				case CMD_UDP:
					if (m_socksv == SOCKS5) break;
					[[fallthrough]];
				default:
					LogPrint (eLogError, "SOCKS: Invalid command: ", ((int)*sock_buff));
					SocksRequestFailed (SOCKS5_GEN_FAIL);
					return false;
				}
				m_cmd = (SOCKSHandler::cmdTypes)*sock_buff;
				switch (m_socksv)
				{
				case SOCKS5: EnterState (GET5_GETRSV); break;
				case SOCKS4: EnterState (GET_PORT); break;
				}
				break;
			case GET_PORT:
				m_port = (m_port << 8) | ((uint16_t)*sock_buff);
				m_parseleft--;
				if (m_parseleft == 0)
				{
					switch (m_socksv)
					{
					case SOCKS5: EnterState (READY); break;
					case SOCKS4: EnterState (GET_IPV4); break;
					}
				}
				break;
			case GET_IPV4:
				m_address.ip = (m_address.ip << 8) | ((uint32_t)*sock_buff);
				m_parseleft--;
				if (m_parseleft == 0)
				{
					switch (m_socksv)
					{
					case SOCKS5: EnterState (GET_PORT); break;
					case SOCKS4:
						EnterState (GET4_IDENT);
						m_4aip = m_address.ip;
						break;
					}
				}
				break;
			case GET4_IDENT:
				if (!*sock_buff)
				{
					if (m_4aip == 0 || m_4aip > 255)
						EnterState (READY);
					else
						EnterState (GET4A_HOST);
				}
				break;
			case GET4A_HOST:
				if (!*sock_buff)
				{
					EnterState (READY);
					break;
				}
				if (m_address.dns.size >= SOCKS_MAX_HOSTNAME_SIZE)
				{
					LogPrint (eLogError, "SOCKS: v4a req failed: destination is too large");
					SocksRequestFailed (SOCKS4_FAIL);
					return false;
				}
				if (!m_address.dns.push_back (*sock_buff))
				{
					LogPrint (eLogError, "SOCKS: v4a req failed: destination is too large");
					SocksRequestFailed (SOCKS4_FAIL);
					return false;
				}
				break;
			case GET5_REQUESTV:
				// Defence in depth: an enabled local credential must have been
				// verified before the request parser is reachable, whatever the
				// method negotiation did.
				if (m_Auth.IsEnabled () && !m_authVerified)
				{
					LogPrint (eLogError, "SOCKS: Rejecting request that skipped local authentication");
					SocksRequestFailed (SOCKS5_RULE_DENIED);
					return false;
				}
				if (*sock_buff != SOCKS5)
				{
					LogPrint (eLogError, "SOCKS: v5 rejected unknown request version: ", ((int)*sock_buff));
					SocksRequestFailed (SOCKS5_GEN_FAIL);
					return false;
				}
				EnterState (GET_COMMAND);
				break;
			case GET5_GETRSV:
				if (*sock_buff != 0)
				{
					LogPrint (eLogError, "SOCKS: v5 unknown reserved field: ", ((int)*sock_buff));
					SocksRequestFailed (SOCKS5_GEN_FAIL);
					return false;
				}
				EnterState (GET5_GETADDRTYPE);
				break;
			case GET5_GETADDRTYPE:
				switch (*sock_buff)
				{
				case ADDR_IPV4: EnterState (GET_IPV4); break;
				case ADDR_IPV6: EnterState (GET5_IPV6); break;
				case ADDR_DNS: EnterState (GET5_HOST_SIZE); break;
				default:
					LogPrint (eLogError, "SOCKS: v5 unknown address type: ", ((int)*sock_buff));
					SocksRequestFailed (SOCKS5_GEN_FAIL);
					return false;
				}
				break;
			case GET5_IPV6:
				m_address.ipv6[16 - m_parseleft] = *sock_buff;
				m_parseleft--;
				if (m_parseleft == 0) EnterState (GET_PORT);
				break;
			case GET5_HOST_SIZE:
				if (*sock_buff == 0)
				{
					LogPrint (eLogError, "SOCKS: v5 empty destination name");
					SocksRequestFailed (SOCKS5_ADDR_UNSUP);
					return false;
				}
				EnterState (GET5_HOST, *sock_buff);
				break;
			case GET5_HOST:
				if (!m_address.dns.push_back (*sock_buff))
				{
					LogPrint (eLogError, "SOCKS: v5 destination name is too large");
					SocksRequestFailed (SOCKS5_ADDR_UNSUP);
					return false;
				}
				m_parseleft--;
				if (m_parseleft == 0) EnterState (GET_PORT);
				break;
			case GET5_USERPASSWD:
				if (*sock_buff != 1)
				{
					LogPrint (eLogError, "SOCKS: v5 rejected invalid username/password subnegotiation");
					Socks5UserPasswdResponse (false);
					return false;
				}
				EnterState (GET5_USER_SIZE);
				break;
			case GET5_USER_SIZE:
				// RFC 1929: ULEN is 1..255, so an empty username is invalid.
				if (!SOCKSUserPassFieldValid (*sock_buff))
				{
					LogPrint (eLogWarning, "SOCKS: v5 rejected empty or oversized username field");
					Socks5UserPasswdResponse (false);
					return false;
				}
				m_authUserLen = 0;
				EnterState (GET5_USER, *sock_buff);
				break;
			case GET5_USER:
				if (m_authUserLen >= SOCKS_AUTH_MAX_FIELD_SIZE)
				{
					LogPrint (eLogWarning, "SOCKS: v5 rejected oversized username");
					Socks5UserPasswdResponse (false);
					return false;
				}
				m_authUser[m_authUserLen++] = static_cast<char> (*sock_buff);
				m_parseleft--;
				if (m_parseleft == 0) EnterState (GET5_PASSWD_SIZE);
				break;
			case GET5_PASSWD_SIZE:
				// RFC 1929: PLEN is 1..255, so an empty password is invalid.
				if (!SOCKSUserPassFieldValid (*sock_buff))
				{
					LogPrint (eLogWarning, "SOCKS: v5 rejected empty or oversized password field");
					Socks5UserPasswdResponse (false);
					return false;
				}
				m_authPasswordLen = 0;
				EnterState (GET5_PASSWD, *sock_buff);
				break;
			case GET5_PASSWD:
				if (m_authPasswordLen >= SOCKS_AUTH_MAX_FIELD_SIZE)
				{
					LogPrint (eLogWarning, "SOCKS: v5 rejected oversized password");
					Socks5UserPasswdResponse (false);
					return false;
				}
				m_authPassword[m_authPasswordLen++] = static_cast<char> (*sock_buff);
				m_parseleft--;
				if (m_parseleft == 0)
				{
					const bool accepted = VerifyUserPass ();
					m_authVerified = accepted;
					Socks5UserPasswdResponse (accepted);
					// Never fall through to the request parser: the write
					// callback closes the connection after reporting failure.
					if (!accepted) return false;
					EnterState (GET5_REQUESTV);
					m_DeferredAuthInput.assign (sock_buff + 1, sock_buff + len);
					return false; // resume in SentSocksResponse
				}
				break;
			default:
				LogPrint (eLogError, "SOCKS: Parse state?? ", m_state);
				Terminate ();
				return false;
			}
			sock_buff++;
			len--;
			if (m_state == READY)
			{
				m_remaining_data_len = len;
				m_remaining_data = sock_buff;
				return ValidateSOCKSRequest ();
			}
		}
		return true;
	}

	void SOCKSHandler::HandleSockRecv (const boost::system::error_code & ecode, std::size_t len)
	{
		LogPrint (eLogDebug, "SOCKS: Received ", len, " bytes");
		if (ecode)
		{
			LogPrint (eLogWarning, "SOCKS: Recv got error: ", ecode);
			Terminate ();
			return;
		}

		if (HandleData (m_sock_buff, len))
		{
			if (m_state == READY)
			{
				m_HandshakeTimer.cancel ();
				if (GetServer ()->HasResolvedAddresses ())
				{
					std::string resolved;
					if (m_addrtype == ADDR_IPV4)
					{
						if ((m_address.ip & 0xFF000000) == 0xFF000000) // 255.x.x.x
							resolved = GetServer ()->GetResolvedAddress (boost::asio::ip::address_v4 (m_address.ip));
					}
					else if (m_addrtype == ADDR_DNS)
					{
						if (m_address.dns.GetString ().substr (0, 4) == "255.")
						{
							// the name only starts like an address: make_address throws on
							// anything else, to_v4 throws on v6, and nothing catches here
							boost::system::error_code addrEcode;
							auto addr = boost::asio::ip::make_address (m_address.dns.GetString (), addrEcode);
							if (!addrEcode && addr.is_v4 ())
								resolved = GetServer ()->GetResolvedAddress (addr.to_v4 ());
						}
					}
					if (!resolved.empty ())
					{
						if (m_addrtype == ADDR_DNS)
							LogPrint (eLogInfo, "SOCKS: Replaced ", m_address.dns.GetString (), " to ", resolved);
						else
							LogPrint (eLogInfo, "SOCKS: Replaced to ", resolved);
						if (!m_address.dns.SetString (resolved))
						{
							LogPrint (eLogError, "SOCKS: resolved destination is too large");
							SocksRequestFailed (SOCKS5_ADDR_UNSUP);
							return;
						}
						m_addrtype = ADDR_DNS;
					}
				}
				if (m_addrtype != ADDR_DNS)
				{
					LogPrint (eLogInfo, "SOCKS: Unexpected address type ", (int)m_addrtype);
					SocksRequestFailed (SOCKS5_ADDR_UNSUP);
					return;
				}

				std::string_view addr = m_address.dns.GetString ();
				if (m_cmd == CMD_RESOLVE)
				{
					// resolve to 255.x.x.x address
					LogPrint (eLogInfo, "SOCKS: Resolve ", addr);
					auto owner = GetOwner ();
					if (!owner)
					{
						Terminate ();
						return;
					}
					// A raw `this` would dangle if the handler is destroyed
					// before the posted task runs, and a string_view into
					// m_address would dangle with it: capture both explicitly.
					boost::asio::post (owner->GetService (),
						[self = shared_from_this (), host = std::string (addr)] (void)
						{
							auto server = self->GetServer ();
							if (!server || !self->m_sock) return;
							address ad;
							ad.ip = server->ResolveAddress (host).to_uint ();
							boost::asio::async_write (*self->m_sock,
								self->GenerateSOCKS5Response (SOCKS5_OK, ADDR_IPV4, ad, self->m_port),
								std::bind (&SOCKSHandler::SentSocksDone, self, std::placeholders::_1));
						});
					return;
				}

				LogPrint (eLogInfo, "SOCKS: Requested ", addr, ":", m_port);
				// Case-insensitive suffix check; an address shorter than the
				// suffix must not underflow into npos and be treated as I2P.
				if (i2p::client::IsI2PHost (addr))
				{
					// yes it does
					switch (m_cmd)
					{
					case CMD_CONNECT:
					{
						//make an i2p session
						auto owner = GetOwner ();
						if (!owner)
						{
							Terminate ();
							return;
						}
						owner->UpdateLastActivityTime ();
						owner->CreateStream (std::bind (&SOCKSHandler::HandleStreamRequestComplete, shared_from_this (),
												 std::placeholders::_1),
							addr, m_port);
					}
					break;
					case CMD_UDP:
					{
						// create UDP client tunnel
						LogPrint (eLogInfo, "SOCKS: New UDP associate connection");
						auto owner = GetOwner ();
						if (!owner)
						{
							Terminate ();
							return;
						}
						m_UDPTunnel = std::make_shared<i2p::client::I2PUDPClientTunnel> ("", addr,
							GetServer ()->GetNextLocalUDPEndpoint (), owner->GetLocalDestination (), m_port, false,
							i2p::datagram::eDatagramV3);
						boost::asio::post (owner->GetService (),
							[self = shared_from_this ()] (void)
							{
								if (self->m_sock) self->SocksRequestSuccess ();
							});
					}
					break;
					default:;
					}
				}
				else if (m_UseUpstreamProxy)
					// forward it to upstream proxy
					ForwardSOCKS ();
				else
					// no upstream proxy
					SocksRequestFailed (SOCKS5_ADDR_UNSUP);
			}
			else
				AsyncSockRead ();
		}
	}

	void SOCKSHandler::SentSocksFailed (const boost::system::error_code & ecode)
	{
		if (ecode) LogPrint (eLogError, "SOCKS: Closing socket after sending failure because: ", ecode.message ());
		Terminate ();
	}

	void SOCKSHandler::SentSocksDone (const boost::system::error_code & ecode)
	{
		if (!ecode)
		{
			if (m_cmd == CMD_CONNECT)
			{
				if (Kill ()) return;
				if (m_sock && m_sock->is_open ())
				{
					LogPrint (eLogInfo, "SOCKS: New I2PTunnel connection");
					auto owner = GetOwner ();
					if (!owner)
					{
						Terminate ();
						return;
					}
					auto connection = std::make_shared<i2p::client::I2PTunnelConnection> (owner, m_sock, m_stream);
					owner->AddHandler (connection);
					connection->I2PConnect (m_remaining_data, m_remaining_data_len);
					Done (shared_from_this ());
				}
				else
					Terminate ();
			}
			else if (m_cmd == CMD_UDP && m_UDPTunnel)
			{
				LogPrint (eLogInfo, "SOCKS: Start UDP tunnel");
				m_UDPTunnel->Start ();
				AsyncSockRead (); // associate socket
			}
		}
		else
		{
			LogPrint (eLogError, "SOCKS: Closing socket after completion reply because: ", ecode.message ());
			Terminate ();
		}
	}

	void SOCKSHandler::SentSocksResponse (const boost::system::error_code & ecode)
	{
		if (ecode)
		{
			LogPrint (eLogError, "SOCKS: Closing socket after sending reply because: ", ecode.message ());
			Terminate ();
			return;
		}
		if (!m_DeferredAuthInput.empty ())
		{
			// The reply write is complete, so the parser may now consume the
			// pipelined bytes and reuse m_response for its own reply.
			const auto size = m_DeferredAuthInput.size ();
			if (size > sizeof (m_sock_buff))
			{
				LogPrint (eLogError, "SOCKS: Pipelined input after authentication is too large");
				Terminate ();
				return;
			}
			std::copy (m_DeferredAuthInput.begin (), m_DeferredAuthInput.end (), m_sock_buff);
			OPENSSL_cleanse (m_DeferredAuthInput.data (), size);
			m_DeferredAuthInput.clear ();
			HandleSockRecv (boost::system::error_code (), size);
			return;
		}
		AsyncSockRead ();
	}

	void SOCKSHandler::HandleStreamRequestComplete (std::shared_ptr<i2p::stream::Stream> stream)
	{
		if (stream)
		{
			if (m_sock && m_sock->is_open ())
			{
				m_stream = stream;
				SocksRequestSuccess ();
			}
			else
				stream->AsyncClose ();
		}
		else
		{
			LogPrint (eLogError, "SOCKS: Error when creating the stream, check the previous warnings for more info");
			SocksRequestFailed (SOCKS5_HOST_UNREACH);
		}
	}

	void SOCKSHandler::ForwardSOCKS ()
	{
		LogPrint (eLogInfo, "SOCKS: Forwarding to upstream");
		if (m_UpstreamProxyPort) // TCP
		{
			EnterState (UPSTREAM_RESOLVE);
			m_proxy_resolver.async_resolve (m_UpstreamProxyAddress, std::to_string (m_UpstreamProxyPort),
				std::bind (&SOCKSHandler::HandleUpstreamResolved, shared_from_this (), std::placeholders::_1,
					std::placeholders::_2));
		}
		else if (!m_UpstreamProxyAddress.empty ()) // local
		{
#if defined(BOOST_ASIO_HAS_LOCAL_SOCKETS)
			EnterState (UPSTREAM_CONNECT);
			auto owner = GetOwner ();
			if (!owner)
			{
				Terminate ();
				return;
			}
			m_upstreamLocalSock = std::make_shared<boost::asio::local::stream_protocol::socket> (owner->GetService ());
			auto s = shared_from_this ();
			m_upstreamLocalSock->async_connect (m_UpstreamProxyAddress,
				[s] (const boost::system::error_code & ecode)
				{
					if (ecode)
					{
						LogPrint (eLogWarning, "SOCKS: Could not connect to local upstream proxy: ", ecode.message ());
						s->SocksRequestFailed (SOCKS5_NET_UNREACH);
						return;
					}
					LogPrint (eLogInfo, "SOCKS: Connected to local upstream proxy");
					s->SendUpstreamRequest (s->m_upstreamLocalSock);
				});
#else
			LogPrint (eLogError, "SOCKS: Local sockets for upstream proxy not supported");
			SocksRequestFailed (SOCKS5_ADDR_UNSUP);
#endif
		}
		else
		{
			LogPrint (eLogError, "SOCKS: Incorrect upstream proxy address");
			SocksRequestFailed (SOCKS5_ADDR_UNSUP);
		}
	}

	template <typename Socket> void SOCKSHandler::SocksUpstreamSuccess (std::shared_ptr<Socket> & upstreamSock)
	{
		if (!m_sock->is_open ())
		{
			// check if the socket got closed during upstream handshake
			LogPrint (eLogWarning, "SOCKS: Can't link to upstream because socket is closed");
			upstreamSock = nullptr;
			m_sock = nullptr;
			Terminate ();
			return;
		}
		LogPrint (eLogInfo, "SOCKS: Upstream success");
		boost::asio::const_buffer response (nullptr, 0);
		switch (m_socksv)
		{
		case SOCKS4:
			LogPrint (eLogInfo, "SOCKS: v4 connection success");
			response = GenerateSOCKS4Response (SOCKS4_OK, m_4aip, m_port);
			break;
		case SOCKS5:
			LogPrint (eLogInfo, "SOCKS: v5 connection success");
			//HACK only 16 bits passed in port as SOCKS5 doesn't allow for more
			response = GenerateSOCKS5Response (SOCKS5_OK, ADDR_DNS, m_address, m_port);
			break;
		}
		m_sock->send (response);
		auto owner = GetOwner ();
		if (!owner)
		{
			Terminate ();
			return;
		}
		auto forwarder = CreateSocketsPipe (owner, m_sock, upstreamSock);
		upstreamSock = nullptr;
		m_sock = nullptr;
		owner->AddHandler (forwarder);
		forwarder->Start ();
		Terminate ();
	}

	template <typename Socket> void SOCKSHandler::SendUpstreamRequest (std::shared_ptr<Socket> & upstreamSock)
	{
		LogPrint (eLogInfo, "SOCKS: Negotiating with upstream proxy");
		EnterState (UPSTREAM_HANDSHAKE);
		if (upstreamSock)
		{
			auto s = shared_from_this ();
			auto onHandshake = [s, &upstreamSock] (const boost::system::error_code & ec)
			{
				if (!ec)
					s->SocksUpstreamSuccess (upstreamSock);
				else
				{
					s->SocksRequestFailed (SOCKS5_NET_UNREACH);
					LogPrint (eLogError, "SOCKS: Upstream proxy failure: ", ec.message ());
				}
			};
			switch (m_addrtype)
			{
			case ADDR_DNS:
				i2p::transport::Socks5Handshake (
					*upstreamSock, std::make_pair (std::string (m_address.dns.GetString ()), m_port), onHandshake);
				break;
			case ADDR_IPV4:
				i2p::transport::Socks5Handshake (*upstreamSock,
					boost::asio::ip::tcp::endpoint (boost::asio::ip::address_v4 (m_address.ip), m_port), onHandshake);
				break;
			case ADDR_IPV6:
			{
				boost::asio::ip::address_v6::bytes_type bytes;
				memcpy (bytes.data (), m_address.ipv6, 16);
				i2p::transport::Socks5Handshake (*upstreamSock,
					boost::asio::ip::tcp::endpoint (boost::asio::ip::address_v6 (bytes), m_port), onHandshake);
				break;
			}
			default: LogPrint (eLogError, "SOCKS: Upstream proxy unknown address type: ", m_addrtype);
			}
		}
		else
			LogPrint (eLogError, "SOCKS: No upstream socket to send handshake to");
	}

	void SOCKSHandler::HandleUpstreamConnected (
		const boost::system::error_code & ecode, const boost::asio::ip::tcp::endpoint &)
	{
		if (ecode)
		{
			LogPrint (eLogWarning, "SOCKS: Could not connect to upstream proxy: ", ecode.message ());
			SocksRequestFailed (SOCKS5_NET_UNREACH);
			return;
		}
		LogPrint (eLogInfo, "SOCKS: Connected to upstream proxy");
		SendUpstreamRequest (m_upstreamSock);
	}

	void SOCKSHandler::HandleUpstreamResolved (
		const boost::system::error_code & ecode, boost::asio::ip::tcp::resolver::results_type endpoints)
	{
		if (ecode)
		{
			// error resolving
			LogPrint (
				eLogWarning, "SOCKS: Upstream proxy", m_UpstreamProxyAddress, " not resolved: ", ecode.message ());
			SocksRequestFailed (SOCKS5_NET_UNREACH);
			return;
		}
		LogPrint (eLogInfo, "SOCKS: Upstream proxy resolved");
		EnterState (UPSTREAM_CONNECT);
		auto owner = GetOwner ();
		if (!owner)
		{
			Terminate ();
			return;
		}
		auto & service = owner->GetService ();
		m_upstreamSock = std::make_shared<boost::asio::ip::tcp::socket> (service);
		boost::asio::async_connect (*m_upstreamSock, endpoints,
			std::bind (&SOCKSHandler::HandleUpstreamConnected, shared_from_this (), std::placeholders::_1,
				std::placeholders::_2));
	}

	SOCKSServer::SOCKSServer (const std::string & name, const std::string & address, uint16_t port, bool outEnable,
		const std::string & outAddress, uint16_t outPort,
		std::shared_ptr<i2p::client::ClientDestination> localDestination)
		: TCPIPAcceptor (
			  address, port, localDestination ? localDestination : i2p::client::context.GetSharedLocalDestination ())
		, m_Name (name)
	{
		m_UseUpstreamProxy = false;
		if (outAddress.length () > 0 && outEnable) SetUpstreamProxy (outAddress, outPort);

		// The torsocks fake-IP key is per process: two runs of the same
		// destination name do not share an address, so a stale client cache
		// cannot point at a different destination after a restart. If the
		// CSPRNG is unavailable the mapping falls back to a time-derived key;
		// that weakens only the unpredictability of a routing hint, never a
		// credential or a signature.
		std::array<uint8_t, 16> resolveKey { };
		if (RAND_bytes (resolveKey.data (), static_cast<int> (resolveKey.size ())) != 1)
		{
			LogPrint (eLogCritical, "SOCKS: CSPRNG unavailable for the RESOLVE cache key");
			const auto seed = static_cast<uint64_t> (std::chrono::steady_clock::now ().time_since_epoch ().count ());
			socks_detail::StoreLe64 (seed, resolveKey.data ());
			socks_detail::StoreLe64 (seed ^ 0x9e3779b97f4a7c15ULL, resolveKey.data () + 8);
		}
		m_ResolveCache.SetKey (resolveKey);

		// Local SOCKS5 authentication. Keys (see libi2pd/Config.cpp):
		//   socksproxy.auth.enabled  - require RFC 1929 credentials
		//   socksproxy.auth.username - accepted username, 1..255 bytes
		//   socksproxy.auth.password - accepted password, 1..255 bytes
		// An enabled listener with an empty or oversized field denies every
		// client (fail closed), and SOCKS4 is rejected while it is enabled.
		bool authEnabled = false;
		i2p::config::GetOption ("socksproxy.auth.enabled", authEnabled);
		std::string authUsername;
		std::string authPassword;
		i2p::config::GetOption ("socksproxy.auth.username", authUsername);
		i2p::config::GetOption ("socksproxy.auth.password", authPassword);
		m_Auth.enabled = authEnabled;
		m_Auth.username = std::move (authUsername);
		m_Auth.password = std::move (authPassword);
		if (m_Auth.enabled)
		{
			if (m_Auth.HasUsableCredentials ())
				LogPrint (eLogInfo, "SOCKS: Local username/password authentication enabled for ", m_Name);
			else
				LogPrint (eLogCritical,
					"SOCKS: socksproxy.auth.enabled is set but socksproxy.auth.username/password are "
					"empty or longer than ",
					SOCKS_AUTH_MAX_FIELD_SIZE, " bytes; every request will be rejected");
		}
	}

	void SOCKSServer::SetAuth (std::string username, std::string password)
	{
		m_Auth.enabled = true;
		m_Auth.username = std::move (username);
		m_Auth.password = std::move (password);
		if (!m_Auth.HasUsableCredentials ())
			LogPrint (eLogCritical,
				"SOCKS: Local authentication configured without usable credentials; every request will be rejected");
	}

	std::shared_ptr<i2p::client::I2PServiceHandler> SOCKSServer::CreateHandler (
		std::shared_ptr<boost::asio::ip::tcp::socket> socket)
	{
		return std::make_shared<SOCKSHandler> (
			shared_from_this (), socket, m_UpstreamProxyAddress, m_UpstreamProxyPort, m_UseUpstreamProxy);
	}

	void SOCKSServer::SetUpstreamProxy (const std::string & addr, const uint16_t port)
	{
		m_UpstreamProxyAddress = addr;
		m_UpstreamProxyPort = port;
		m_UseUpstreamProxy = true;
	}

	boost::asio::ip::udp::endpoint SOCKSServer::GetNextLocalUDPEndpoint ()
	{
		const auto & localEndpoint = GetLocalEndpoint ();
		uint16_t port = localEndpoint.port ();
		while (m_UDPPorts.contains (port)) port++;
		m_UDPPorts.insert (port);

		return boost::asio::ip::udp::endpoint (localEndpoint.address (), port); // use proxy address
	}

	void SOCKSServer::ReleaseLocalUDPPort (uint16_t port)
	{
		m_UDPPorts.erase (port);
	}

	std::string SOCKSServer::GetResolvedAddress (const boost::asio::ip::address_v4 & addr) const
	{
		return m_ResolveCache.Lookup (addr, i2p::util::GetMillisecondsSinceEpoch ());
	}

	boost::asio::ip::address_v4 SOCKSServer::ResolveAddress (std::string_view resolved)
	{
		return m_ResolveCache.Insert (resolved, i2p::util::GetMillisecondsSinceEpoch ());
	}
}
}

/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: I2PTunnel.
 *
 * Implements TCP client and server tunnels that bridge local sockets to I2P
 * streams, including HTTP header rewriting, IRC WEBIRC injection, optional
 * TLS, gzip and destination access lists. Server tunnels resolve destinations
 * before connecting and tie each connection to a handler's lifetime.
 *
 */

#include <algorithm>
#include <cassert>
#include <openssl/ssl.h>
#include <cstring>
#include <boost/algorithm/string.hpp>
#include "Base.h"
#include "Log.h"
#include "Destination.h"
#include "ClientContext.h"
#include "I2PTunnel.h"
#include "util.h"

namespace i2p
{
namespace client
{

	/** set standard socket options */
	static void I2PTunnelSetSocketOptions (std::shared_ptr<boost::asio::ip::tcp::socket> socket)
	{
		if (socket && socket->is_open ())
		{
			boost::asio::socket_base::receive_buffer_size option (I2P_TUNNEL_CONNECTION_BUFFER_SIZE);
			socket->set_option (option);
		}
	}

	I2PTunnelConnection::I2PTunnelConnection (std::shared_ptr<I2PService> owner,
		std::shared_ptr<boost::asio::ip::tcp::socket> socket, std::shared_ptr<const i2p::data::LeaseSet> leaseSet,
		uint16_t port)
		: I2PServiceHandler (owner)
		, m_Socket (socket)
		, m_IsReceiving (false)
	{
		auto service = GetOwner ();
		if (service) m_Stream = service->GetLocalDestination ()->CreateStream (leaseSet, port);
	}

	I2PTunnelConnection::I2PTunnelConnection (std::shared_ptr<I2PService> owner,
		std::shared_ptr<boost::asio::ip::tcp::socket> socket, std::shared_ptr<i2p::stream::Stream> stream)
		: I2PServiceHandler (owner)
		, m_Socket (socket)
		, m_Stream (stream)
		, m_IsReceiving (false)
	{
	}

	I2PTunnelConnection::I2PTunnelConnection (std::shared_ptr<I2PService> owner,
		std::shared_ptr<i2p::stream::Stream> stream, const boost::asio::ip::tcp::endpoint & target,
		std::shared_ptr<boost::asio::ssl::context> sslCtx)
		: I2PServiceHandler (owner)
		, m_Stream (stream)
		, m_RemoteEndpoint (target)
		, m_IsReceiving (false)
	{
		m_Socket = std::make_shared<boost::asio::ip::tcp::socket> (owner->GetService ());
		if (sslCtx)
			m_SSL = std::make_shared<boost::asio::ssl::stream<boost::asio::ip::tcp::socket &>> (*m_Socket, *sslCtx);
	}

	I2PTunnelConnection::~I2PTunnelConnection () { }

	void I2PTunnelConnection::I2PConnect (const uint8_t * msg, size_t len)
	{
		if (m_Stream && m_Socket && m_Socket->is_open ())
		{
			if (msg)
				m_Stream->Send (msg, len); // connect and send
			else
			{
				len = 0;
				if (!m_SSL)
				{
					// check if something available
					boost::system::error_code ec;
					len = m_Socket->available (ec);
					if (!ec && len > 0)
					{
						if (len > i2p::stream::STREAMING_MTU_RATCHETS / 2)
							len = i2p::stream::STREAMING_MTU_RATCHETS / 2;
						len = boost::asio::read (
							*m_Socket, boost::asio::buffer (m_Buffer, len), boost::asio::transfer_all (), ec);
						if (ec) len = 0;
					}
					else
						len = 0;
				}
				m_Stream->Send (m_Buffer, len); // connect and send if available
			}
			StreamReceive ();
			Receive ();
		}
		else
			Terminate ();
	}

	boost::asio::ip::address GetLoopbackAddressFor (const i2p::data::IdentHash & addr)
	{
		boost::asio::ip::address_v4::bytes_type bytes;
		const uint8_t * ident = addr;
		bytes[0] = 127;
		memcpy (bytes.data () + 1, ident, 3);
		boost::asio::ip::address ourIP = boost::asio::ip::address_v4 (bytes);
		return ourIP;
	}

	boost::asio::ip::address GetLoopbackAddress6For (const i2p::data::IdentHash & addr)
	{
		boost::asio::ip::address_v6::bytes_type bytes;
		const uint8_t * ident = addr;
		bytes[0] = 0xfd;
		memcpy (bytes.data () + 1, ident, 15);
		boost::asio::ip::address ourIP = boost::asio::ip::address_v6 (bytes);
		return ourIP;
	}

#ifdef __linux__
	static void MapToLoopback (
		std::shared_ptr<boost::asio::ip::tcp::socket> sock, const i2p::data::IdentHash & addr, bool isV4)
	{
		if (sock)
		{
			// bind to 127.x.x.x address for ipv4
			// where x.x.x are first three bytes from ident
			// bind to fdxx.xxxx.xxxx.xxxx.xxxx.xxxx.xxxx.xxxx address for ipv6
			// where xx.xxxx.xxxx.xxxx.xxxx.xxxx.xxxx.xxxx are first 15 bytes from ident
			auto ourIP = isV4 ? GetLoopbackAddressFor (addr) : GetLoopbackAddress6For (addr);
			boost::system::error_code ec;
			sock->bind (boost::asio::ip::tcp::endpoint (ourIP, 0), ec);
			if (ec)
				LogPrint (eLogError, "I2PTunnel: Can't bind ourIP to ", i2p::util::net::FormatAddress (ourIP), ": ",
					ec.message ());
		}
	}
#endif

	void I2PTunnelConnection::Connect (bool isUniqueLocal)
	{
#ifndef __linux__
		(void)isUniqueLocal; // unique-local loopback mapping is Linux-only
#endif
		if (m_Socket)
		{
			I2PTunnelSetSocketOptions (m_Socket);
#ifdef __linux__
			if (isUniqueLocal && m_RemoteEndpoint.address ().is_loopback ())
			{
				auto ident = m_Stream->GetRemoteIdentity ()->GetIdentHash ();
				if (m_RemoteEndpoint.address ().is_v4 ())
				{
					m_Socket->open (boost::asio::ip::tcp::v4 ());
					MapToLoopback (m_Socket, ident, true);
				}
				else if (m_RemoteEndpoint.address ().is_v6 ())
				{
					m_Socket->open (boost::asio::ip::tcp::v6 ());
					MapToLoopback (m_Socket, ident, false);
				}
			}
#endif
			m_Socket->async_connect (m_RemoteEndpoint,
				std::bind (&I2PTunnelConnection::HandleConnect, shared_from_this (), std::placeholders::_1));
		}
	}

	void I2PTunnelConnection::Connect (const boost::asio::ip::address & localAddress)
	{
		if (m_Socket)
		{
			if (m_RemoteEndpoint.address ().is_v6 ())
				m_Socket->open (boost::asio::ip::tcp::v6 ());
			else
				m_Socket->open (boost::asio::ip::tcp::v4 ());
			boost::system::error_code ec;
			m_Socket->bind (boost::asio::ip::tcp::endpoint (localAddress, 0), ec);
			if (ec)
				LogPrint (eLogError, "I2PTunnel: Can't bind to ", i2p::util::net::FormatAddress (localAddress), ": ",
					ec.message ());
		}
		Connect (false);
	}

	void I2PTunnelConnection::Terminate ()
	{
		if (Kill ()) return;
		if (m_SSL) m_SSL = nullptr;
		if (m_Stream)
		{
			// Close() mutates the stream state and belongs to the destination
			// thread; a stop driven by the administrative inventory or daemon
			// shutdown runs elsewhere, so the close is posted to that thread.
			auto stream = m_Stream;
			m_Stream = nullptr;
			stream->AsyncClose ();
		}
		if (m_Socket && m_Socket->is_open ())
		{
			boost::system::error_code ec;
			m_Socket->shutdown (boost::asio::ip::tcp::socket::shutdown_send, ec); // avoid RST
			m_Socket->close ();
		}
		Done (shared_from_this ());
	}

	void I2PTunnelConnection::Receive ()
	{
		if (m_IsReceiving) return; // already receiving
		size_t bufSize = I2P_TUNNEL_CONNECTION_BUFFER_SIZE;
		size_t unsentSize = m_Stream ? m_Stream->GetSendBufferSize () : 0;
		if (unsentSize)
		{
			if (unsentSize >= I2P_TUNNEL_CONNECTION_STREAM_MAX_SEND_BUFFER_SIZE) return; // buffer is full
			if (unsentSize > I2P_TUNNEL_CONNECTION_STREAM_MAX_SEND_BUFFER_SIZE - I2P_TUNNEL_CONNECTION_BUFFER_SIZE)
				bufSize = I2P_TUNNEL_CONNECTION_STREAM_MAX_SEND_BUFFER_SIZE - unsentSize;
		}
		m_IsReceiving = true;
		if (m_SSL)
			m_SSL->async_read_some (boost::asio::buffer (m_Buffer, bufSize),
				std::bind (&I2PTunnelConnection::HandleReceive, shared_from_this (), std::placeholders::_1,
					std::placeholders::_2));
		else
			m_Socket->async_read_some (boost::asio::buffer (m_Buffer, bufSize),
				std::bind (&I2PTunnelConnection::HandleReceive, shared_from_this (), std::placeholders::_1,
					std::placeholders::_2));
	}

	void I2PTunnelConnection::HandleReceive (const boost::system::error_code & ecode, std::size_t bytes_transferred)
	{
		m_IsReceiving = false;
		if (ecode)
		{
			if (ecode != boost::asio::error::operation_aborted)
			{
				if (ecode != boost::asio::error::bad_descriptor && ecode != boost::asio::error::eof)
					LogPrint (eLogError, "I2PTunnel: Read error: ", ecode.message ());
				Terminate ();
			}
		}
		else
		{
			if (bytes_transferred < I2P_TUNNEL_CONNECTION_BUFFER_SIZE && !m_SSL)
			{
				boost::system::error_code ec;
				size_t moreBytes = m_Socket->available (ec);
				if (!ec && moreBytes && m_Stream)
				{
					// read more data from socket before sending to stream
					if (bytes_transferred + moreBytes > I2P_TUNNEL_CONNECTION_BUFFER_SIZE)
						moreBytes = I2P_TUNNEL_CONNECTION_BUFFER_SIZE - bytes_transferred;
					if (m_Stream->GetSendBufferSize () < I2P_TUNNEL_CONNECTION_STREAM_MAX_SEND_BUFFER_SIZE)
					{
						size_t remaining
							= I2P_TUNNEL_CONNECTION_STREAM_MAX_SEND_BUFFER_SIZE - m_Stream->GetSendBufferSize ();
						if (remaining < moreBytes) moreBytes = remaining;
					}
					else
						moreBytes = 0;
				}
				if (moreBytes)
				{
					moreBytes
						= boost::asio::read (*m_Socket, boost::asio::buffer (m_Buffer + bytes_transferred, moreBytes),
							boost::asio::transfer_all (), ec);
					if (!ec) bytes_transferred += moreBytes;
				}
			}
			WriteToStream (m_Buffer, bytes_transferred);
			Receive (); // try to receive more while being sent to stream
		}
	}

	void I2PTunnelConnection::WriteToStream (const uint8_t * buf, size_t len)
	{
		if (m_Stream)
		{
			m_Stream->AsyncSend (buf, len,
				[s = shared_from_this ()] (const boost::system::error_code & ecode, size_t)
				{
					if (!ecode)
						s->Receive ();
					else
						s->Terminate ();
				});
			auto owner = GetOwner ();
			if (owner) owner->UpdateLastActivityTime ();
		}
	}

	void I2PTunnelConnection::HandleWrite (const boost::system::error_code & ecode)
	{
		if (ecode)
		{
			if (ecode != boost::asio::error::operation_aborted)
			{
				if (ecode != boost::asio::error::bad_descriptor)
					LogPrint (eLogError, "I2PTunnel: Write error: ", ecode.message ());
				Terminate ();
			}
		}
		else
			StreamReceive ();
	}

	void I2PTunnelConnection::StreamReceive ()
	{
		if (m_Stream)
		{
			if (m_Stream->GetStatus () == i2p::stream::eStreamStatusNew
				|| m_Stream->GetStatus () == i2p::stream::eStreamStatusOpen) // regular
			{
				m_Stream->AsyncReceive (boost::asio::buffer (m_StreamBuffer, I2P_TUNNEL_CONNECTION_STREAM_BUFFER_SIZE),
					std::bind (&I2PTunnelConnection::HandleStreamReceive, shared_from_this (), std::placeholders::_1,
						std::placeholders::_2),
					I2P_TUNNEL_CONNECTION_MAX_IDLE);
			}
			else // closed by peer
			{
				// get remaining data
				auto len = m_Stream->ReadSome (m_StreamBuffer, I2P_TUNNEL_CONNECTION_STREAM_BUFFER_SIZE);
				if (len > 0) // still some data
					Write (m_StreamBuffer, len);
				else // no more data
					Terminate ();
			}
		}
	}

	void I2PTunnelConnection::HandleStreamReceive (
		const boost::system::error_code & ecode, std::size_t bytes_transferred)
	{
		if (ecode)
		{
			if (ecode != boost::asio::error::operation_aborted)
			{
				LogPrint (eLogError, "I2PTunnel: Stream read error: ", ecode.message ());
				if (bytes_transferred > 0)
					Write (m_StreamBuffer, bytes_transferred); // postpone termination
				else if (ecode == boost::asio::error::timed_out && m_Stream && m_Stream->IsOpen ())
					StreamReceive ();
				else
					Terminate ();
			}
			else
				Terminate ();
		}
		else
			Write (m_StreamBuffer, bytes_transferred);
	}

	void I2PTunnelConnection::Write (const uint8_t * buf, size_t len)
	{
		if (m_SSL)
			boost::asio::async_write (*m_SSL, boost::asio::buffer (buf, len), boost::asio::transfer_all (),
				std::bind (&I2PTunnelConnection::HandleWrite, shared_from_this (), std::placeholders::_1));
		else
			boost::asio::async_write (*m_Socket, boost::asio::buffer (buf, len), boost::asio::transfer_all (),
				std::bind (&I2PTunnelConnection::HandleWrite, shared_from_this (), std::placeholders::_1));
		auto owner = GetOwner ();
		if (owner) owner->UpdateLastActivityTime ();
	}

	void I2PTunnelConnection::HandleConnect (const boost::system::error_code & ecode)
	{
		if (ecode)
		{
			LogPrint (eLogError, "I2PTunnel: Connect error to ", m_RemoteEndpoint, " : ", ecode.message ());
			Terminate ();
		}
		else
		{
			LogPrint (eLogDebug, "I2PTunnel: Connected to ", m_RemoteEndpoint);
			if (m_SSL)
				m_SSL->async_handshake (boost::asio::ssl::stream_base::client,
					std::bind (&I2PTunnelConnection::HandleHandshake, shared_from_this (), std::placeholders::_1));
			else
				Established ();
		}
	}

	void I2PTunnelConnection::HandleHandshake (const boost::system::error_code & ecode)
	{
		if (ecode)
		{
			LogPrint (eLogError, "I2PTunnel: Handshake error: ", ecode.message ());
			Terminate ();
		}
		else
		{
			LogPrint (eLogDebug, "I2PTunnel: SSL connected");
			Established ();
		}
	}

	void I2PTunnelConnection::Established ()
	{
		StreamReceive ();
		Receive ();
	}

	void I2PClientTunnelConnectionHTTP::Write (const uint8_t * buf, size_t len)
	{
		if (m_RequestHeader.State () == I2PTunnelHTTPHeader::Result::Complete)
		{
			I2PTunnelConnection::Write (buf, len);
			return;
		}
		switch (m_RequestHeader.Feed ({ reinterpret_cast<const char *> (buf), len }))
		{
		case I2PTunnelHTTPHeader::Result::Complete:
		{
			const auto & output = m_RequestHeader.Output ();
			// The parser owns this output until connection destruction. It is
			// never mutated after completion, including by response parsing.
			I2PTunnelConnection::Write (reinterpret_cast<const uint8_t *> (output.data ()), output.size ());
			break;
		}
		case I2PTunnelHTTPHeader::Result::Incomplete: StreamReceive (); break;
		case I2PTunnelHTTPHeader::Result::Rejected:
			LogPrint (eLogError, "I2PTunnel: HTTP request header exceeds bounds");
			Terminate ();
			break;
		}
	}

	I2PServerTunnelConnectionHTTP::I2PServerTunnelConnectionHTTP (std::shared_ptr<I2PService> owner,
		std::shared_ptr<i2p::stream::Stream> stream, const boost::asio::ip::tcp::endpoint & target,
		const std::string & host, const std::string & XI2P, std::shared_ptr<boost::asio::ssl::context> sslCtx,
		std::shared_ptr<I2PServerTunnelHTTPPolicy> policy)
		: I2PTunnelConnection (owner, stream, target, sslCtx)
		, m_RequestHeader (I2PTunnelHTTPHeader::Mode::ServerRequest, host, XI2P)
		, m_ResponseHeader (I2PTunnelHTTPHeader::Mode::ServerResponse)
		, m_Policy (std::move (policy))
	{
		if (sslCtx && SSL_set_tlsext_host_name (GetSSL ()->native_handle (), host.c_str ()) != 1)
			LogPrint (eLogError, "I2PTunnel: Failed to set SNI for local upstream ", host);
	}

	void I2PServerTunnelConnectionHTTP::RejectRequest (int status, std::string_view reason)
	{
		m_RequestRejected = true;
		std::string response = "HTTP/1.1 " + std::to_string (status) + " " + std::string (reason)
			+ "\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
		// Terminate() closes the stream, whose closing state flushes the send
		// buffer before the FIN, so the status line is not lost.
		I2PTunnelConnection::WriteToStream (
			reinterpret_cast<const uint8_t *> (response.data ()), response.size ());
		Terminate ();
	}

	void I2PServerTunnelConnectionHTTP::Write (const uint8_t * buf, size_t len)
	{
		if (m_RequestHeader.State () == I2PTunnelHTTPHeader::Result::Complete)
		{
			I2PTunnelConnection::Write (buf, len);
			return;
		}
		switch (m_RequestHeader.Feed ({ reinterpret_cast<const char *> (buf), len }))
		{
		case I2PTunnelHTTPHeader::Result::Complete:
		{
			const auto & output = m_RequestHeader.Output ();
			if (m_Policy)
			{
				switch (m_Policy->Check (output, i2p::util::GetSecondsSinceEpoch ()))
				{
				case I2PServerTunnelRequestDecision::eForbidden:
					// rejectInproxy/rejectReferer/rejectUserAgents. The header
					// bytes are never logged.
					LogPrint (eLogInfo, "I2PTunnel: server tunnel refused a request (403)");
					RejectRequest (403, "Forbidden");
					return;
				case I2PServerTunnelRequestDecision::eTooManyRequests:
					LogPrint (eLogInfo, "I2PTunnel: server tunnel throttled a request (429)");
					RejectRequest (429, "Too Many Requests");
					return;
				case I2PServerTunnelRequestDecision::eAllow: break;
				}
			}
			// The parser owns this output until connection destruction. It is
			// never mutated after completion, including by response parsing.
			I2PTunnelConnection::Write (reinterpret_cast<const uint8_t *> (output.data ()), output.size ());
			break;
		}
		case I2PTunnelHTTPHeader::Result::Incomplete: StreamReceive (); break;
		case I2PTunnelHTTPHeader::Result::Rejected:
			LogPrint (eLogError, "I2PTunnel: HTTP request header exceeds bounds");
			Terminate ();
			break;
		}
	}

	void I2PServerTunnelConnectionHTTP::WriteToStream (const uint8_t * buf, size_t len)
	{
		if (m_ResponseHeader.State () == I2PTunnelHTTPHeader::Result::Complete)
		{
			I2PTunnelConnection::WriteToStream (buf, len);
			return;
		}
		switch (m_ResponseHeader.Feed ({ reinterpret_cast<const char *> (buf), len }))
		{
		case I2PTunnelHTTPHeader::Result::Complete:
		{
			const auto & output = m_ResponseHeader.Output ();
			I2PTunnelConnection::WriteToStream (reinterpret_cast<const uint8_t *> (output.data ()), output.size ());
			break;
		}
		case I2PTunnelHTTPHeader::Result::Incomplete: Receive (); break;
		case I2PTunnelHTTPHeader::Result::Rejected:
			LogPrint (eLogError, "I2PTunnel: HTTP response header exceeds bounds");
			Terminate ();
			break;
		}
	}

	I2PTunnelConnectionIRC::I2PTunnelConnectionIRC (std::shared_ptr<I2PService> owner,
		std::shared_ptr<i2p::stream::Stream> stream, const boost::asio::ip::tcp::endpoint & target,
		const std::string & webircpass, std::shared_ptr<boost::asio::ssl::context> sslCtx)
		: I2PTunnelConnection (owner, stream, target, sslCtx)
		, m_From (stream->GetRemoteIdentity ())
		, m_NeedsWebIrc (webircpass.length () ? true : false)
		, m_WebircPass (webircpass)
	{
	}

	void I2PTunnelConnectionIRC::Write (const uint8_t * buf, size_t len)
	{
		m_OutPacket.str ("");
		if (m_NeedsWebIrc)
		{
			m_NeedsWebIrc = false;
			m_OutPacket << "WEBIRC " << m_WebircPass << " cgiirc "
						<< context.GetAddressBook ().ToAddress (m_From->GetIdentHash ()) << " "
						<< i2p::util::net::FormatAddress (GetSocket ()->local_endpoint ().address ()) << std::endl;
		}

		m_InPacket.clear ();
		m_InPacket.write ((const char *)buf, len);

		while (!m_InPacket.eof () && !m_InPacket.fail ())
		{
			std::string line;
			std::getline (m_InPacket, line);
			if (line.length () == 0 && m_InPacket.eof ()) m_InPacket.str ("");
			auto pos = line.find ("USER");
			if (!pos) // start of line
			{
				pos = line.find (" ");
				if (pos != std::string::npos) pos = line.find (" ", pos + 1);
				auto nextpos = (pos != std::string::npos) ? line.find (" ", pos + 1) : std::string::npos;
				if (pos == std::string::npos || nextpos == std::string::npos)
					m_OutPacket << line << '\n'; // malformed USER line, pass through unmodified
				else
				{
					m_OutPacket << line.substr (0, pos + 1);
					m_OutPacket << context.GetAddressBook ().ToAddress (m_From->GetIdentHash ());
					m_OutPacket << line.substr (nextpos) << '\n';
				}
			}
			else
				m_OutPacket << line << '\n';
		}
		// view() references m_OutPacket, which remains valid until the asynchronous write completes
		I2PTunnelConnection::Write ((const uint8_t *)m_OutPacket.view ().data (), m_OutPacket.view ().size ());
	}

	/* This handler tries to establish a connection with the desired server and dies if it fails to do so */
	class I2PClientTunnelHandler : public I2PServiceHandler, public std::enable_shared_from_this<I2PClientTunnelHandler>
	{
	public:
		I2PClientTunnelHandler (std::shared_ptr<I2PService> parent, std::shared_ptr<const Address> address,
			uint16_t destinationPort, std::shared_ptr<boost::asio::ip::tcp::socket> socket)
			: I2PServiceHandler (parent)
			, m_Address (address)
			, m_DestinationPort (destinationPort)
			, m_Socket (socket) { };
		void Handle () override;
		void Terminate () override;

	private:
		void HandleStreamRequestComplete (std::shared_ptr<i2p::stream::Stream> stream);
		std::shared_ptr<const Address> m_Address;
		uint16_t m_DestinationPort;
		std::shared_ptr<boost::asio::ip::tcp::socket> m_Socket;
	};

	void I2PClientTunnelHandler::Handle ()
	{
		auto owner = GetOwner ();
		if (!owner) return;
		owner->UpdateLastActivityTime ();
		owner->CreateStream (std::bind (&I2PClientTunnelHandler::HandleStreamRequestComplete, shared_from_this (),
								 std::placeholders::_1),
			m_Address, m_DestinationPort);
	}

	void I2PClientTunnelHandler::HandleStreamRequestComplete (std::shared_ptr<i2p::stream::Stream> stream)
	{
		if (stream)
		{
			if (Kill ()) return;
			if (m_Socket && m_Socket->is_open ())
			{
				LogPrint (eLogDebug, "I2PTunnel: New connection");
				auto owner = GetOwner ();
				if (!owner)
				{
					Done (shared_from_this ());
					return;
				}
				auto connection = std::make_shared<I2PTunnelConnection> (owner, m_Socket, stream);
				owner->AddHandler (connection);
				if (!owner->IsRunning ())
				{
					// The tunnel stopped while the stream request completed; the
					// connection was published after the stop snapshot, so close
					// it here (through the public base declaration).
					I2PServiceHandler * handler = connection.get ();
					handler->Terminate ();
					Done (shared_from_this ());
					return;
				}
				connection->I2PConnect ();
				Done (shared_from_this ());
			}
			else
			{
				stream->AsyncClose ();
				Terminate ();
			}
		}
		else
		{
			LogPrint (eLogError,
				"I2PTunnel: Client Tunnel Issue when creating the stream, check the previous warnings for more info.");
			Terminate ();
		}
	}

	void I2PClientTunnelHandler::Terminate ()
	{
		if (Kill ()) return;
		if (m_Socket)
		{
			m_Socket->close ();
			m_Socket = nullptr;
		}
		Done (shared_from_this ());
	}

	// destination is a comma separated list, so one tunnel can serve several servers
	static std::vector<std::string> SplitDestinations (const std::string & destination)
	{
		std::vector<std::string> destinations;
		boost::split (destinations, destination, boost::is_any_of (","), boost::token_compress_on);
		for (auto & it : destinations) boost::trim (it);
		destinations.erase (std::remove (destinations.begin (), destinations.end (), ""), destinations.end ());
		return destinations;
	}

	I2PClientTunnel::I2PClientTunnel (const std::string & name, const std::string & destination,
		const std::string & address, uint16_t port, std::shared_ptr<ClientDestination> localDestination,
		uint16_t destinationPort)
		: TCPIPAcceptor (address, port, localDestination)
		, m_Name (name)
		, m_Destinations (SplitDestinations (destination))
		, m_Addresses (m_Destinations.size ())
		, m_NextAddress (0)
		, m_DestinationPort (destinationPort)
		, m_KeepAliveInterval (0)
	{
	}

	void I2PClientTunnel::Start ()
	{
		TCPIPAcceptor::Start ();
		GetAddress ();
		if (m_KeepAliveInterval) ScheduleKeepAliveTimer ();
	}

	void I2PClientTunnel::Stop ()
	{
		TCPIPAcceptor::Stop ();
		for (auto & it : m_Addresses) it = nullptr;
		if (m_KeepAliveTimer)
		{
			m_KeepAliveTimer->cancel ();
			m_KeepAliveTimer = nullptr;
		}
	}

	void I2PClientTunnel::SetKeepAliveInterval (uint32_t keepAliveInterval)
	{
		m_KeepAliveInterval = keepAliveInterval;
		if (m_KeepAliveInterval)
			m_KeepAliveTimer.reset (new boost::asio::steady_timer (GetLocalDestination ()->GetService ()));
	}

	/* HACK: maybe we should create a caching IdentHash provider in AddressBook */
	std::shared_ptr<const Address> I2PClientTunnel::GetAddress ()
	{
		// round-robin over the destinations, skipping those we can't resolve,
		// so one dead server doesn't block the tunnel
		for (size_t tried = 0; tried < m_Destinations.size (); tried++)
		{
			auto i = m_NextAddress;
			m_NextAddress = (m_NextAddress + 1) % m_Destinations.size ();
			if (!m_Addresses[i])
			{
				m_Addresses[i] = i2p::client::context.GetAddressBook ().GetAddress (m_Destinations[i]);
				if (!m_Addresses[i])
					LogPrint (eLogWarning, "I2PTunnel: Remote destination ", m_Destinations[i], " not found");
			}
			if (m_Addresses[i]) return m_Addresses[i];
		}
		return nullptr;
	}

	std::shared_ptr<I2PServiceHandler> I2PClientTunnel::CreateHandler (
		std::shared_ptr<boost::asio::ip::tcp::socket> socket)
	{
		auto address = GetAddress ();
		if (address)
			return std::make_shared<I2PClientTunnelHandler> (shared_from_this (), address, m_DestinationPort, socket);
		else
			return nullptr;
	}

	void I2PClientTunnel::ScheduleKeepAliveTimer ()
	{
		if (m_KeepAliveTimer)
		{
			m_KeepAliveTimer->expires_after (std::chrono::seconds (m_KeepAliveInterval));
			// The timer outlives Stop()/destruction of the tunnel, so the
			// completion holds a weak owner instead of raw `this`.
			std::weak_ptr<I2PClientTunnel> weakSelf
				= std::static_pointer_cast<I2PClientTunnel> (weak_from_this ().lock ());
			m_KeepAliveTimer->async_wait (
				[weakSelf] (const boost::system::error_code & ecode)
				{
					auto self = weakSelf.lock ();
					if (!self) return;
					self->HandleKeepAliveTimer (ecode);
				});
		}
	}

	void I2PClientTunnel::HandleKeepAliveTimer (const boost::system::error_code & ecode)
	{
		if (ecode != boost::asio::error::operation_aborted && IsRunning ())
		{
			// ping every resolved destination, otherwise the spare ones go cold
			for (const auto & address : m_Addresses)
			{
				if (!address || !address->IsValid ()) continue;
				if (address->IsIdentHash ())
					GetLocalDestination ()->SendPing (address->identHash);
				else
					GetLocalDestination ()->SendPing (address->blindedPublicKey);
			}
			ScheduleKeepAliveTimer ();
		}
	}

	I2PServerTunnel::I2PServerTunnel (const std::string & name, const std::string & address, uint16_t port,
		std::shared_ptr<ClientDestination> localDestination, uint16_t inport, bool gzip)
		: I2PService (localDestination)
		, m_IsUniqueLocal (true)
		, m_IsDefaultAcceptor (false)
		, m_HasAcceptor (false)
		, m_Name (name)
		, m_Address (address)
		, m_Port (port)
		, m_IsAccessList (false)
		, m_AccessRejectAction (TunnelAccessRejectAction::eDrop)
		, m_LimitActionConfigured (false)
		, m_Generation (0)
	{
		m_PortDestination = localDestination->GetStreamingDestination (inport);
		if (!m_PortDestination) // default destination
			m_PortDestination = localDestination->CreateStreamingDestination (inport, gzip);
	}

	void I2PServerTunnel::Start ()
	{
		// Invalidate callbacks from a previous run so a restart cannot be
		// disturbed by a late resolver or acceptor completion.
		++m_Generation;
		m_Endpoint.port (m_Port);
		boost::system::error_code ec;
		auto addr = boost::asio::ip::make_address (m_Address, ec);
		if (!ec)
			m_Endpoint.address (addr);
		else
			Resolve (nullptr);
		{
			std::lock_guard<std::mutex> lock (m_TargetsMutex);
			m_ResolvedTargets.clear ();
		}
		ResolveTargets ();
		I2PService::Start ();
		Accept ();
	}

	void I2PServerTunnel::DetachAcceptor ()
	{
		if (m_PortDestination && m_HasAcceptor)
		{
			// Only this tunnel's registration is removed. A port-specific tunnel
			// is never installed on the default streaming destination, so
			// stopping it cannot disable another tunnel that shares the
			// destination.
			m_PortDestination->SetAcceptor (nullptr);
			m_PortDestination->ResetAdmissionPredicate ();
		}
		if (m_DefaultAcceptorDestination)
		{
			m_DefaultAcceptorDestination->SetAcceptor (nullptr);
			m_DefaultAcceptorDestination->ResetAdmissionPredicate ();
			m_DefaultAcceptorDestination = nullptr;
		}
		m_HasAcceptor = false;
	}

	void I2PServerTunnel::Stop ()
	{
		Stop (true);
	}

	void I2PServerTunnel::Stop (bool forced)
	{
		// Close the handler admission barrier first: a connection published
		// after this point sees IsRunning()==false and terminates itself.
		I2PService::Stop ();
		++m_Generation; // invalidates every outstanding completion
		DetachAcceptor ();
		if (m_Resolver)
		{
			m_Resolver->cancel ();
			m_Resolver = nullptr;
		}
		for (const auto & resolver : m_TargetResolvers)
			if (resolver) resolver->cancel ();
		m_TargetResolvers.clear ();
		{
			std::lock_guard<std::mutex> lock (m_TargetsMutex);
			m_ResolvedTargets.clear ();
		}
		if (forced || !IsInUse ())
			ClearHandlers ();
		else
			LogPrint (eLogInfo, "I2PTunnel: ", m_Name, " keeps ", GetNumHandlers (),
				" live connection(s) because the stop was not forced");
	}

	bool I2PServerTunnel::Resolve (std::shared_ptr<i2p::stream::Stream> stream)
	{
		if (m_Resolver) return false; // already resolving
		m_Resolver = std::make_shared<boost::asio::ip::tcp::resolver> (GetService ());
		std::weak_ptr<I2PServerTunnel> weakSelf
			= std::static_pointer_cast<I2PServerTunnel> (weak_from_this ().lock ());
		const auto generation = m_Generation.load ();
		m_Resolver->async_resolve (m_Address, "",
			[weakSelf, generation, stream] (const boost::system::error_code & ecode,
				boost::asio::ip::tcp::resolver::results_type endpoints)
			{
				auto self = weakSelf.lock ();
				if (!self || self->m_Generation.load () != generation) return;
				self->HandleResolve (ecode, std::move (endpoints), stream);
			});
		return true;
	}

	void I2PServerTunnel::HandleResolve (const boost::system::error_code & ecode,
		boost::asio::ip::tcp::resolver::results_type endpoints, std::shared_ptr<i2p::stream::Stream> stream)
	{
		m_Resolver = nullptr;
		if (!ecode)
		{
			bool found = false;
			boost::asio::ip::tcp::endpoint ep;
			if (m_LocalAddress)
			{
				for (const auto & it : endpoints)
				{
					ep = it;
					if (!ep.address ().is_unspecified ())
					{
						if (ep.address ().is_v4 ())
						{
							if (m_LocalAddress->is_v4 ()) found = true;
						}
						else if (ep.address ().is_v6 ())
						{
							if (i2p::util::net::IsYggdrasilAddress (ep.address ()))
							{
								if (i2p::util::net::IsYggdrasilAddress (*m_LocalAddress)) found = true;
							}
							else if (m_LocalAddress->is_v6 ())
								found = true;
						}
					}
					if (found) break;
				}
			}
			else
			{
				found = true;
				ep = *endpoints.begin (); // first available
			}
			if (!found)
			{
				LogPrint (eLogError, "I2PTunnel: Unable to resolve ", m_Address, " to compatible address");
				if (stream) stream->AbortWithReset ();
				return;
			}

			auto addr = ep.address ();
			LogPrint (eLogInfo, "I2PTunnel: Server tunnel ", (*endpoints.begin ()).host_name (),
				" has been resolved to ", addr);
			m_Endpoint.address (addr);
			if (stream) Connect (stream, m_Endpoint);
		}
		else
		{
			LogPrint (
				eLogError, "I2PTunnel: Unable to resolve server tunnel address ", m_Address, ": ", ecode.message ());
			if (stream) stream->AbortWithReset ();
		}
	}

	void I2PServerTunnel::ResolveTargets ()
	{
		for (const auto & [localPort, entry] : m_Targets.Entries ())
		{
			boost::system::error_code ec;
			auto address = boost::asio::ip::make_address (entry.host, ec);
			if (!ec)
			{
				std::lock_guard<std::mutex> lock (m_TargetsMutex);
				m_ResolvedTargets[localPort] = boost::asio::ip::tcp::endpoint (address, entry.port);
				continue;
			}
			// A hostname target is resolved asynchronously; a stream arriving
			// before the answer exists is reset instead of blocking the service.
			auto resolver = std::make_shared<boost::asio::ip::tcp::resolver> (GetService ());
			m_TargetResolvers.push_back (resolver);
			std::weak_ptr<I2PServerTunnel> weakSelf
				= std::static_pointer_cast<I2PServerTunnel> (weak_from_this ().lock ());
			const auto generation = m_Generation.load ();
			resolver->async_resolve (entry.host, std::to_string (entry.port),
				[weakSelf, generation, localPort] (const boost::system::error_code & ecode,
					boost::asio::ip::tcp::resolver::results_type endpoints)
				{
					auto self = weakSelf.lock ();
					if (!self || self->m_Generation.load () != generation) return;
					self->HandleTargetResolve (ecode, std::move (endpoints), localPort);
				});
		}
	}

	void I2PServerTunnel::HandleTargetResolve (const boost::system::error_code & ecode,
		boost::asio::ip::tcp::resolver::results_type endpoints, uint16_t localPort)
	{
		if (ecode || endpoints.empty ())
		{
			LogPrint (eLogWarning, "I2PTunnel: targetForPort ", localPort,
				" of ", m_Name, " cannot be resolved; streams on that port are reset");
			return;
		}
		std::lock_guard<std::mutex> lock (m_TargetsMutex);
		m_ResolvedTargets[localPort] = *endpoints.begin ();
	}

	bool I2PServerTunnel::SelectTarget (uint16_t localPort, boost::asio::ip::tcp::endpoint & target) const
	{
		auto selection = m_Targets.Select (localPort);
		switch (selection.status)
		{
		case I2PServerTunnelTargetMap::Selection::Status::eTarget:
		{
			std::lock_guard<std::mutex> lock (m_TargetsMutex);
			auto it = m_ResolvedTargets.find (localPort);
			if (it == m_ResolvedTargets.end ()) return false; // not resolved yet
			target = it->second;
			return true;
		}
		case I2PServerTunnelTargetMap::Selection::Status::eReset:
			// Java requires an explicit mapping for the reserved service ports.
			return false;
		case I2PServerTunnelTargetMap::Selection::Status::eDefault: break;
		}
		target = m_Endpoint;
		return true;
	}

	void I2PServerTunnel::SetTargets (const I2PServerTunnelTargetMap & targets)
	{
		m_Targets = targets;
	}

	void I2PServerTunnel::SetAccessList (const std::set<i2p::data::IdentHash> & accessList)
	{
		m_AccessList = accessList;
		m_IsAccessList = true;
	}

	void I2PServerTunnel::SetAccessFilter (
		std::shared_ptr<const FilterDefinition> definition, std::string_view limitAction)
	{
		m_AccessFilterDefinition = std::move (definition);
		if (m_AccessFilterDefinition)
			m_AccessFilter = std::make_shared<TunnelAccessFilter> (m_AccessFilterDefinition);
		else
			m_AccessFilter = nullptr;
		SetLimitAction (limitAction);
	}

	void I2PServerTunnel::SetLimitAction (std::string_view limitAction)
	{
		if (limitAction.empty ())
		{
			// Filter denials are silent by default; streaming limits keep the
			// historical signed RESET.
			m_AccessRejectAction = TunnelAccessRejectAction::eDrop;
			m_LimitActionConfigured = false;
			return;
		}
		m_LimitActionConfigured = true;
		if (limitAction == "reset")
			m_AccessRejectAction = TunnelAccessRejectAction::eReset;
		else if (limitAction == "drop")
			m_AccessRejectAction = TunnelAccessRejectAction::eDrop;
		else
		{
			LogPrint (eLogWarning, "I2PTunnel: unsupported limitAction for ", m_Name,
				"; using the default reset action for limits and drop for filter denials");
			m_AccessRejectAction = TunnelAccessRejectAction::eDrop;
			m_LimitActionConfigured = false;
		}
	}

	void I2PServerTunnel::SetLocalAddress (const std::string & localAddress)
	{
		boost::system::error_code ec;
		auto addr = boost::asio::ip::make_address (localAddress, ec);
		if (!ec)
			m_LocalAddress.reset (new boost::asio::ip::address (addr));
		else
			LogPrint (eLogError, "I2PTunnel: Can't set local address ", localAddress);
	}

	void I2PServerTunnel::SetSSL (bool ssl)
	{
		if (ssl)
		{
			// This context is used for the client handshake to the local SSL
			// upstream server (I2PTunnelConnection::HandleConnect uses
			// stream_base::client), not for accepting TLS.
			m_SSLCtx = std::make_shared<boost::asio::ssl::context> (boost::asio::ssl::context::tls_client);
			m_SSLCtx->set_options (boost::asio::ssl::context::default_workarounds | boost::asio::ssl::context::no_sslv2
				| boost::asio::ssl::context::no_sslv3 | boost::asio::ssl::context::no_tlsv1
				| boost::asio::ssl::context::no_tlsv1_1);
			// Keep an explicit floor independent of the Asio option bits.
			if (SSL_CTX_set_min_proto_version (m_SSLCtx->native_handle (), TLS1_2_VERSION) != 1)
				LogPrint (eLogError, "I2PTunnel: Can't set the upstream TLS 1.2 minimum version");
			m_SSLCtx->set_verify_mode (boost::asio::ssl::context::verify_none);
		}
		else
			m_SSLCtx = nullptr;
	}

	void I2PServerTunnel::Accept ()
	{
		if (!m_PortDestination)
		{
			LogPrint (eLogError, "I2PTunnel: Local destination not set for server tunnel");
			return;
		}
		const bool isPortSpecific = m_PortDestination->GetLocalPort () != 0;
		if (isPortSpecific)
		{
			// A port-specific tunnel owns only its own streaming port. It used
			// to become the default acceptor when the destination had none and
			// then received every unregistered port.
			InstallAcceptor (m_PortDestination);
			if (m_IsDefaultAcceptor)
			{
				// Explicit `defaultacceptor=true` also claims the default
				// streaming destination, taking it over deterministically.
				auto localDestination = GetLocalDestination ();
				auto defaultDestination
					= localDestination ? localDestination->GetDefaultStreamingDestination () : nullptr;
				if (defaultDestination && defaultDestination != m_PortDestination)
				{
					InstallAcceptor (defaultDestination);
					m_DefaultAcceptorDestination = defaultDestination;
				}
			}
			return;
		}
		if (!m_HasAcceptor && !m_IsDefaultAcceptor && m_PortDestination->IsAcceptorSet ())
		{
			LogPrint (eLogWarning, "I2PTunnel: ", m_Name,
				" does not take over an acceptor that another tunnel already owns");
			return;
		}
		InstallAcceptor (m_PortDestination);
	}

	void I2PServerTunnel::InstallAcceptor (
		const std::shared_ptr<i2p::stream::StreamingDestination> & destination)
	{
		if (!destination) return;
		// The acceptor outlives an unforced stop, so it holds a weak owner and
		// the generation it was installed in. A late invocation for a stopped
		// tunnel answers with a RESET instead of touching freed memory.
		std::weak_ptr<I2PServerTunnel> weakSelf
			= std::static_pointer_cast<I2PServerTunnel> (weak_from_this ().lock ());
		const auto generation = m_Generation.load ();
		auto replaced = destination->SetAcceptor (
			[weakSelf, generation] (std::shared_ptr<i2p::stream::Stream> stream)
			{
				auto self = weakSelf.lock ();
				if (!self || self->m_Generation.load () != generation)
				{
					if (stream) stream->AbortWithReset ();
					return;
				}
				self->HandleAccept (stream);
			});
		if (replaced)
			LogPrint (eLogDebug, "I2PTunnel: ", m_Name, " took over an existing acceptor on port ",
				destination->GetLocalPort ());
		m_HasAcceptor = true;
		InstallAdmissionPolicy (destination);
	}

	void I2PServerTunnel::InstallAdmissionPolicy (
		const std::shared_ptr<i2p::stream::StreamingDestination> & destination)
	{
		if (!destination || !m_AccessFilter) return;
		auto filter = m_AccessFilter;
		const auto filterAction = m_AccessRejectAction == TunnelAccessRejectAction::eReset
			? i2p::stream::StreamAdmission::eReset
			: i2p::stream::StreamAdmission::eDrop;
		destination->SetAdmissionPredicate (
			[filter, filterAction] (
				const i2p::data::IdentHash & peer, uint16_t) -> i2p::stream::StreamAdmission
			{
				// Only the outcome is visible to the log; the definition, the
				// file entries and the peer identity stay out of it.
				auto decision = filter->Evaluate (peer, i2p::util::GetSecondsSinceEpoch ());
				return decision.allow ? i2p::stream::StreamAdmission::eAllow : filterAction;
			});
		if (m_LimitActionConfigured) destination->SetRejectionAction (filterAction);
	}

	void I2PServerTunnel::HandleAccept (std::shared_ptr<i2p::stream::Stream> stream)
	{
		if (!stream) return;
		if (!IsRunning ())
		{
			// The tunnel was stopped between the acceptor dispatch and this
			// call; refuse the stream instead of leaving it unowned.
			stream->AbortWithReset ();
			return;
		}
		if (m_IsAccessList && !m_AccessFilter)
		{
			// Legacy inline accesslist/whitelist: a secondary check for the
			// case where no filterDefinition replaced it.
			auto identity = stream->GetRemoteIdentity ();
			if (!identity || !m_AccessList.count (identity->GetIdentHash ()))
			{
				LogPrint (eLogWarning, "I2PTunnel: An incoming address is not in the white list; connection reset");
				stream->AbortWithReset ();
				return;
			}
		}
		boost::asio::ip::tcp::endpoint target;
		if (!SelectTarget (stream->GetLocalPort (), target))
		{
			LogPrint (eLogWarning, "I2PTunnel: ", m_Name, " has no local target for accepted port ",
				stream->GetLocalPort (), "; connection reset");
			stream->AbortWithReset ();
			return;
		}
		if (!target.address ().is_unspecified ())
			Connect (stream, target);
		else if (!Resolve (stream))
		{
			LogPrint (
				eLogWarning, "I2PTunnel: Address ", m_Address, " can't be resolved. Incoming connection dropped");
			stream->AbortWithReset ();
		}
	}

	void I2PServerTunnel::Connect (
		std::shared_ptr<i2p::stream::Stream> stream, const boost::asio::ip::tcp::endpoint & target)
	{
		// new connection
		auto conn = CreateI2PConnection (stream, target);
		if (!conn)
		{
			if (stream) stream->AbortWithReset ();
			return;
		}
		AddHandler (conn);
		if (!IsRunning ())
		{
			// Stop() ran between the admission check and the publication: the
			// handler was registered after the stop snapshot, so this path owns
			// closing it instead of leaving an unowned connection behind.
			// Terminate() is reached through the public base declaration, the
			// virtual dispatch still runs the connection teardown.
			I2PServiceHandler * handler = conn.get ();
			handler->Terminate ();
			return;
		}
		if (m_LocalAddress)
			conn->Connect (*m_LocalAddress);
		else
			conn->Connect (m_IsUniqueLocal);
	}

	std::shared_ptr<I2PTunnelConnection> I2PServerTunnel::CreateI2PConnection (
		std::shared_ptr<i2p::stream::Stream> stream, const boost::asio::ip::tcp::endpoint & target)
	{
		return std::make_shared<I2PTunnelConnection> (shared_from_this (), stream, target, m_SSLCtx);
	}

	I2PServerTunnelHTTP::I2PServerTunnelHTTP (const std::string & name, const std::string & address, uint16_t port,
		std::shared_ptr<ClientDestination> localDestination, const std::string & host, uint16_t inport, bool gzip,
		bool i2pheaders)
		: I2PServerTunnel (name, address, port, localDestination, inport, gzip)
		, m_Host (host)
		, m_I2PHeaders (i2pheaders)
		, m_Policy (std::make_shared<I2PServerTunnelHTTPPolicy> ())
	{
		m_Policy->SetHostOverride (host);
	}

	void I2PServerTunnelHTTP::SetHostOverride (const std::string & host)
	{
		m_Host = host;
		m_Policy->SetHostOverride (host);
	}

	bool I2PServerTunnelHTTP::SetSpoofedHost (uint16_t port, const std::string & host, std::string & error)
	{
		return m_Policy->SetSpoofedHost (port, host, error);
	}

	void I2PServerTunnelHTTP::SetRejectInproxy (bool reject)
	{
		m_Policy->SetRejectInproxy (reject);
	}

	void I2PServerTunnelHTTP::SetRejectReferer (bool reject)
	{
		m_Policy->SetRejectReferer (reject);
	}

	bool I2PServerTunnelHTTP::SetRejectUserAgents (const std::string & list, std::string & error)
	{
		return m_Policy->SetRejectUserAgents (list, error);
	}

	void I2PServerTunnelHTTP::SetPostLimit (int perWindow)
	{
		m_Policy->SetPostLimit (perWindow);
	}

	std::shared_ptr<I2PTunnelConnection> I2PServerTunnelHTTP::CreateI2PConnection (
		std::shared_ptr<i2p::stream::Stream> stream, const boost::asio::ip::tcp::endpoint & target)
	{
		if (m_I2PHeaders && (m_XI2P.empty () || stream->GetRemoteIdentity () != m_From.lock ()))
		{
			auto from = stream->GetRemoteIdentity ();
			if (!from) return nullptr;
			m_From = from;
			std::stringstream ss;
			ss << X_I2P_DEST_B32 << ": " << context.GetAddressBook ().ToAddress (from->GetIdentHash ()) << "\r\n";
			ss << X_I2P_DEST_HASH << ": " << from->GetIdentHash ().ToBase64 () << "\r\n";
			ss << X_I2P_DEST_B64 << ": " << from->ToBase64 () << "\r\n";
			m_XI2P = ss.str ();
		}
		// `spoofedhost.<port>` wins over the `hostoverride` fallback for the
		// port the client connected to.
		const auto host = m_Policy->HostFor (stream->GetLocalPort ());
		return std::make_shared<I2PServerTunnelConnectionHTTP> (
			shared_from_this (), stream, target, host, m_XI2P, GetSSLCtx (), m_Policy);
	}

	I2PServerTunnelIRC::I2PServerTunnelIRC (const std::string & name, const std::string & address, uint16_t port,
		std::shared_ptr<ClientDestination> localDestination, const std::string & webircpass, uint16_t inport, bool gzip)
		: I2PServerTunnel (name, address, port, localDestination, inport, gzip)
		, m_WebircPass (webircpass)
	{
	}

	std::shared_ptr<I2PTunnelConnection> I2PServerTunnelIRC::CreateI2PConnection (
		std::shared_ptr<i2p::stream::Stream> stream, const boost::asio::ip::tcp::endpoint & target)
	{
		return std::make_shared<I2PTunnelConnectionIRC> (
			shared_from_this (), stream, target, m_WebircPass, GetSSLCtx ());
	}

	// ---------------------------------------------------------------
	// targetForPort map
	// ---------------------------------------------------------------

	bool I2PServerTunnelTargetMap::Add (uint16_t localPort, std::string host, uint16_t port, std::string & error)
	{
		error.clear ();
		if (localPort == 0)
		{
			error = "local port must not be zero";
			return false;
		}
		if (host.empty () || host.size () > FilterDefinition::MAX_TARGET_LENGTH)
		{
			error = "invalid target host";
			return false;
		}
		if (!port)
		{
			error = "target port must not be zero";
			return false;
		}
		if (m_Targets.size () >= I2P_SERVER_TUNNEL_MAX_PORT_TARGETS && !m_Targets.contains (localPort))
		{
			error = "too many targetForPort entries";
			return false;
		}
		m_Targets[localPort] = Entry { std::move (host), port };
		return true;
	}

	bool I2PServerTunnelTargetMap::AddSpec (
		uint16_t localPort, std::string_view specification, std::string & error)
	{
		if (specification.empty () || specification.size () > 2 * FilterDefinition::MAX_TARGET_LENGTH)
		{
			error = "invalid targetForPort value";
			return false;
		}
		std::string host;
		std::string_view portText;
		if (specification.front () == '[')
		{
			auto bracket = specification.find (']');
			if (bracket == std::string_view::npos || bracket + 1 >= specification.size ()
				|| specification[bracket + 1] != ':')
			{
				error = "invalid targetForPort value";
				return false;
			}
			host.assign (specification.substr (1, bracket - 1));
			portText = specification.substr (bracket + 2);
		}
		else
		{
			auto colon = specification.rfind (':');
			if (colon == std::string_view::npos)
			{
				error = "invalid targetForPort value";
				return false;
			}
			host.assign (specification.substr (0, colon));
			portText = specification.substr (colon + 1);
		}
		if (portText.empty () || portText.size () > 5)
		{
			error = "invalid targetForPort value";
			return false;
		}
		uint32_t port = 0;
		for (char ch : portText)
		{
			if (ch < '0' || ch > '9')
			{
				error = "invalid targetForPort value";
				return false;
			}
			port = port * 10 + static_cast<uint32_t> (ch - '0');
		}
		if (!port || port > 65535)
		{
			error = "invalid targetForPort value";
			return false;
		}
		return Add (localPort, std::move (host), static_cast<uint16_t> (port), error);
	}

	I2PServerTunnelTargetMap::Selection I2PServerTunnelTargetMap::Select (uint16_t acceptedLocalPort) const
	{
		Selection selection;
		auto it = m_Targets.find (acceptedLocalPort);
		if (it != m_Targets.end ())
		{
			selection.status = Selection::Status::eTarget;
			selection.host = it->second.host;
			selection.port = it->second.port;
			return selection;
		}
		if (IsReservedLocalPort (acceptedLocalPort))
		{
			selection.status = Selection::Status::eReset;
			return selection;
		}
		selection.status = Selection::Status::eDefault;
		return selection;
	}

	// ---------------------------------------------------------------
	// HTTP request policy
	// ---------------------------------------------------------------

	namespace
	{
		char ToLowerAscii (char ch) { return ch >= 'A' && ch <= 'Z' ? static_cast<char> (ch + ('a' - 'A')) : ch; }

		bool HeaderNameEquals (std::string_view name, std::string_view expected)
		{
			if (name.size () != expected.size ()) return false;
			for (size_t i = 0; i < name.size (); ++i)
				if (ToLowerAscii (name[i]) != ToLowerAscii (expected[i])) return false;
			return true;
		}

		bool IsPostMethod (std::string_view header)
		{
			auto end = header.find_first_of (" \t\r\n");
			auto method = header.substr (0, end == std::string_view::npos ? header.size () : end);
			return HeaderNameEquals (method, "POST") || HeaderNameEquals (method, "PUT");
		}
	}

	bool I2PServerTunnelHTTPPolicy::SetSpoofedHost (uint16_t port, std::string host, std::string & error)
	{
		error.clear ();
		if (!port)
		{
			error = "spoofedHost port must not be zero";
			return false;
		}
		if (host.empty () || host.size () > MAX_PATTERN_LENGTH
			|| host.find_first_of ("\r\n\t ") != std::string::npos)
		{
			error = "invalid spoofedHost value";
			return false;
		}
		std::lock_guard<std::mutex> lock (m_Mutex);
		if (m_SpoofedHosts.size () >= MAX_SPOOFED_HOSTS && !m_SpoofedHosts.contains (port))
		{
			error = "too many spoofedHost entries";
			return false;
		}
		m_SpoofedHosts[port] = std::move (host);
		return true;
	}

	std::string I2PServerTunnelHTTPPolicy::HostFor (uint16_t port) const
	{
		std::lock_guard<std::mutex> lock (m_Mutex);
		auto it = m_SpoofedHosts.find (port);
		if (it != m_SpoofedHosts.end ()) return it->second;
		return m_HostOverride;
	}

	bool I2PServerTunnelHTTPPolicy::SetRejectUserAgents (std::string_view list, std::string & error)
	{
		error.clear ();
		std::vector<std::string> patterns;
		size_t start = 0;
		while (start <= list.size ())
		{
			auto comma = list.find (',', start);
			auto item = list.substr (start, comma == std::string_view::npos ? std::string_view::npos : comma - start);
			while (!item.empty () && (item.front () == ' ' || item.front () == '\t')) item.remove_prefix (1);
			while (!item.empty () && (item.back () == ' ' || item.back () == '\t')) item.remove_suffix (1);
			if (!item.empty ())
			{
				if (item.size () > MAX_PATTERN_LENGTH)
				{
					error = "rejectUserAgents pattern too long";
					return false;
				}
				if (patterns.size () >= MAX_REJECT_USER_AGENTS)
				{
					error = "too many rejectUserAgents patterns";
					return false;
				}
				std::string lower;
				lower.reserve (item.size ());
				for (char ch : item) lower.push_back (ToLowerAscii (ch));
				patterns.push_back (std::move (lower));
			}
			if (comma == std::string_view::npos) break;
			start = comma + 1;
		}
		std::lock_guard<std::mutex> lock (m_Mutex);
		m_RejectUserAgents = std::move (patterns);
		return true;
	}

	void I2PServerTunnelHTTPPolicy::SetPostLimit (int perWindow)
	{
		if (perWindow < 0) perWindow = 0;
		std::lock_guard<std::mutex> lock (m_Mutex);
		m_PostLimit = perWindow;
	}

	I2PServerTunnelRequestDecision I2PServerTunnelHTTPPolicy::Check (
		std::string_view header, uint64_t nowSeconds)
	{
		const bool isPost = IsPostMethod (header);
		std::lock_guard<std::mutex> lock (m_Mutex);
		size_t pos = 0;
		bool requestLine = true;
		while (pos < header.size ())
		{
			auto end = header.find ('\n', pos);
			auto line = header.substr (pos, end == std::string_view::npos ? std::string_view::npos : end - pos);
			if (!line.empty () && line.back () == '\r') line.remove_suffix (1);
			pos = end == std::string_view::npos ? header.size () : end + 1;
			if (requestLine)
			{
				requestLine = false;
				continue;
			}
			if (line.empty ()) break;
			auto colon = line.find (':');
			if (colon == std::string_view::npos) continue;
			auto name = line.substr (0, colon);
			auto value = line.substr (colon + 1);
			if (m_RejectInproxy
				&& (HeaderNameEquals (name, "X-Forwarded-For") || HeaderNameEquals (name, "Via")
					|| HeaderNameEquals (name, "Forwarded")))
				return I2PServerTunnelRequestDecision::eForbidden;
			if (m_RejectReferer && HeaderNameEquals (name, "Referer"))
				return I2PServerTunnelRequestDecision::eForbidden;
			if (!m_RejectUserAgents.empty () && HeaderNameEquals (name, "User-Agent"))
			{
				std::string lower;
				lower.reserve (value.size ());
				for (char ch : value) lower.push_back (ToLowerAscii (ch));
				for (const auto & pattern : m_RejectUserAgents)
					if (lower.find (pattern) != std::string::npos) return I2PServerTunnelRequestDecision::eForbidden;
			}
		}
		if (isPost && m_PostLimit > 0)
		{
			if (!m_PostWindowStart || nowSeconds >= m_PostWindowStart + I2P_SERVER_TUNNEL_POST_WINDOW)
			{
				m_PostWindowStart = nowSeconds;
				m_PostCount = 0;
			}
			if (++m_PostCount > static_cast<uint32_t> (m_PostLimit))
				return I2PServerTunnelRequestDecision::eTooManyRequests;
		}
		return I2PServerTunnelRequestDecision::eAllow;
	}
}
}

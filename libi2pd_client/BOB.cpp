/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: BOB.
 *
 * Implements the line-oriented BOB command protocol for creating nicknamed
 * destinations and their TCP-to-I2P tunnels, including key, lookup, ping and
 * proxy commands. Operands are parsed with fixed-length bounds on the command
 * channel's service thread.
 *
 */

#include <string.h>
#include <algorithm>
#include <charconv>
#include <vector>
#include "Log.h"
#include "ClientContext.h"
#include "Config.h"
#include "util.h"
#include "BOB.h"

#ifndef _WIN32
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace i2p
{
namespace client
{
	BOBCommandLineReader::Result BOBCommandLineReader::Feed (std::string_view bytes)
	{
		// One bounded line plus its terminator is all that is ever stored.
		constexpr size_t capacity = BOB_MAX_COMMAND_LINE + 1;
		if (m_Pending.size () < capacity)
		{
			const size_t room = capacity - m_Pending.size ();
			m_Pending.append (bytes.substr (0, std::min (room, bytes.size ())));
		}
		const auto newline = m_Pending.find ('\n');
		if (newline != std::string::npos)
		{
			// A line longer than the limit is rejected even if it does end.
			if (newline > BOB_MAX_COMMAND_LINE) return Result::Overflow;
			return Result::Complete;
		}
		if (m_Pending.size () >= capacity) return Result::Overflow;
		return Result::Incomplete;
	}

	std::string BOBCommandLineReader::TakeLine ()
	{
		const auto newline = m_Pending.find ('\n');
		if (newline == std::string::npos) return { };
		std::string line = m_Pending.substr (0, newline);
		m_Pending.erase (0, newline + 1);
		// Strip the optional CR of a CRLF peer (Transmission does this).
		if (!line.empty () && line.back () == '\r') line.pop_back ();
		return line;
	}

	BOBPortResult ParseBOBPort (std::string_view operand, uint16_t & port)
	{
		port = 0;
		if (operand.empty ()) return BOBPortResult::Empty;
		if (operand.size () > 5) return BOBPortResult::OutOfRange;
		unsigned value = 0;
		const auto * const first = operand.data ();
		const auto * const last = first + operand.size ();
		const auto result = std::from_chars (first, last, value);
		if (result.ec == std::errc::result_out_of_range) return BOBPortResult::OutOfRange;
		if (result.ec != std::errc () || result.ptr != last) return BOBPortResult::Invalid;
		if (value > 65535) return BOBPortResult::OutOfRange;
		port = static_cast<uint16_t> (value);
		return BOBPortResult::Ok;
	}

	bool TryAcquireBOBSession (BOBAdmissionState & state)
	{
		size_t current = state.active.load (std::memory_order_relaxed);
		do
		{
			if (current >= BOB_MAX_CONCURRENT_SESSIONS) return false;
		} while (!state.active.compare_exchange_weak (
			current, current + 1, std::memory_order_acq_rel, std::memory_order_relaxed));
		return true;
	}

	void ReleaseBOBSession (BOBAdmissionState & state)
	{
		size_t current = state.active.load (std::memory_order_relaxed);
		while (current > 0
			&& !state.active.compare_exchange_weak (
				current, current - 1, std::memory_order_acq_rel, std::memory_order_relaxed))
		{
		}
	}

	bool ResolveBOBAllowGetKeys (bool optionFound, bool optionValue)
	{
		return optionFound && optionValue;
	}

	namespace
	{
		// Strict small unsigned parse for the optional key-type operands.
		bool ParseSmallUnsigned (std::string_view text, unsigned & value)
		{
			value = 0;
			if (text.empty () || text.size () > 9) return false;
			unsigned parsed = 0;
			const auto * const first = text.data ();
			const auto * const last = first + text.size ();
			const auto result = std::from_chars (first, last, parsed);
			if (result.ec != std::errc () || result.ptr != last) return false;
			value = parsed;
			return true;
		}

		// The command listener is an INET socket, so a per-user peer credential
		// is not universally available. Where the platform exposes one it is
		// enforced; otherwise the peer must at least be local and the operator
		// must opt in explicitly to the private-key reply.
		bool IsTrustedCommandPeer (const boost::asio::ip::tcp::socket & socket)
		{
			boost::system::error_code ec;
			const auto peer = socket.remote_endpoint (ec);
			if (ec || !peer.address ().is_loopback ()) return false;
#if defined(__linux__)
			struct ucred credentials { };
			socklen_t length = sizeof (credentials);
			if (getsockopt (socket.native_handle (), SOL_SOCKET, SO_PEERCRED, &credentials, &length) == 0)
			{
				if (credentials.uid != geteuid ()) return false;
			}
			else
			{
				// EOPNOTSUPP/ENOPROTOOPT: this socket family does not expose a
				// peer credential here. The loopback check above is then the
				// only admission rule, which is why the private-key reply also
				// needs an explicit operator opt-in.
			}
#endif
			return true;
		}
	}

	void BOBI2PTunnelIncomingConnection::Established ()
	{
		if (m_IsQuiet)
			StreamReceive ();
		else
		{
			// send destination first like received from I2P
			std::string dest = GetStream ()->GetRemoteIdentity ()->ToBase64 ();
			dest += "\n";
			if (dest.size () <= I2P_TUNNEL_CONNECTION_BUFFER_SIZE)
				memcpy (GetStreamBuffer (), dest.c_str (), dest.size ());
			else
				memset (GetStreamBuffer (), 0, I2P_TUNNEL_CONNECTION_BUFFER_SIZE);
			HandleStreamReceive (boost::system::error_code (), dest.size ());
		}
		Receive ();
	}

	BOBI2PInboundTunnel::BOBI2PInboundTunnel (
		const boost::asio::ip::tcp::endpoint & ep, std::shared_ptr<ClientDestination> localDestination)
		: BOBI2PTunnel (localDestination)
		, m_Acceptor (localDestination->GetService (), ep)
	{
	}

	BOBI2PInboundTunnel::~BOBI2PInboundTunnel ()
	{
		Stop ();
	}

	void BOBI2PInboundTunnel::Start ()
	{
		if (!m_Stopped.exchange (false, std::memory_order_acq_rel)) return;
		m_Acceptor.listen ();
		Accept ();
	}

	void BOBI2PInboundTunnel::Stop ()
	{
		std::vector<std::shared_ptr<AddressReceiver>> receivers;
		{
			std::lock_guard lock (m_AdmissionMutex);
			if (m_Stopped.exchange (true, std::memory_order_acq_rel)) return;
			receivers.assign (m_Receivers.begin (), m_Receivers.end ());
			m_Receivers.clear ();
		}
		boost::system::error_code ec;
		m_Acceptor.cancel (ec);
		m_Acceptor.close (ec);
		for (const auto & receiver : receivers)
			if (receiver && receiver->socket) receiver->socket->close (ec);
		ClearHandlers ();
	}

	void BOBI2PInboundTunnel::Accept ()
	{
		if (m_Stopped.load (std::memory_order_acquire)) return;
		auto receiver = std::make_shared<AddressReceiver> ();
		receiver->socket = std::make_shared<boost::asio::ip::tcp::socket> (GetService ());
		{
			std::lock_guard lock (m_AdmissionMutex);
			if (m_Stopped.load (std::memory_order_acquire)) return;
			m_Receivers.insert (receiver);
		}
		auto weak = std::weak_ptr<BOBI2PInboundTunnel> (
			std::static_pointer_cast<BOBI2PInboundTunnel> (I2PService::shared_from_this ()));
		m_Acceptor.async_accept (*receiver->socket,
			[weak, receiver] (const boost::system::error_code & ecode)
			{
				if (auto self = weak.lock ()) self->HandleAccept (ecode, receiver);
			});
	}

	void BOBI2PInboundTunnel::HandleAccept (
		const boost::system::error_code & ecode, std::shared_ptr<AddressReceiver> receiver)
	{
		if (!ecode && !m_Stopped.load (std::memory_order_acquire))
		{
			Accept ();
			ReceiveAddress (receiver);
		}
		else
			CloseReceiver (receiver);
	}

	void BOBI2PInboundTunnel::ReceiveAddress (std::shared_ptr<AddressReceiver> receiver)
	{
		if (m_Stopped.load (std::memory_order_acquire)) return;
		auto weak = std::weak_ptr<BOBI2PInboundTunnel> (
			std::static_pointer_cast<BOBI2PInboundTunnel> (I2PService::shared_from_this ()));
		receiver->socket->async_read_some (boost::asio::buffer (receiver->buffer + receiver->bufferOffset,
											   BOB_COMMAND_BUFFER_SIZE - receiver->bufferOffset),
			[weak, receiver] (const boost::system::error_code & ecode, std::size_t transferred)
			{
				if (auto self = weak.lock ()) self->HandleReceivedAddress (ecode, transferred, receiver);
			});
	}

	void BOBI2PInboundTunnel::HandleReceivedAddress (const boost::system::error_code & ecode,
		std::size_t bytes_transferred, std::shared_ptr<AddressReceiver> receiver)
	{
		if (m_Stopped.load (std::memory_order_acquire)) return;
		if (ecode)
		{
			LogPrint (eLogError, "BOB: Inbound tunnel read error: ", ecode.message ());
			CloseReceiver (receiver);
		}
		else
		{
			receiver->bufferOffset += bytes_transferred;
			receiver->buffer[receiver->bufferOffset] = 0;
			char * eol = strchr (receiver->buffer, '\n');
			if (eol)
			{
				*eol = 0;
				if (eol != receiver->buffer && eol[-1] == '\r')
					eol[-1] = 0; // workaround for Transmission, it sends '\r\n' terminated address
				receiver->data = (uint8_t *)eol + 1;
				receiver->dataLen = receiver->bufferOffset - (eol - receiver->buffer + 1);
				auto addr = context.GetAddressBook ().GetAddress (receiver->buffer);
				if (!addr)
				{
					LogPrint (eLogError, "BOB: Address ", receiver->buffer, " not found");
					CloseReceiver (receiver);
					return;
				}
				if (addr->IsIdentHash ())
				{
					auto leaseSet = GetLocalDestination ()->FindLeaseSet (addr->identHash);
					if (leaseSet)
						CreateConnection (receiver, leaseSet);
					else
					{
						auto weak = std::weak_ptr<BOBI2PInboundTunnel> (
							std::static_pointer_cast<BOBI2PInboundTunnel> (I2PService::shared_from_this ()));
						GetLocalDestination ()->RequestDestination (addr->identHash,
							[weak, receiver] (std::shared_ptr<i2p::data::LeaseSet> result)
							{
								if (auto self = weak.lock ())
									self->HandleDestinationRequestComplete (std::move (result), receiver);
							});
					}
				}
				else
				{
					auto weak = std::weak_ptr<BOBI2PInboundTunnel> (
						std::static_pointer_cast<BOBI2PInboundTunnel> (I2PService::shared_from_this ()));
					GetLocalDestination ()->RequestDestinationWithEncryptedLeaseSet (addr->blindedPublicKey,
						[weak, receiver] (std::shared_ptr<i2p::data::LeaseSet> result)
						{
							if (auto self = weak.lock ())
								self->HandleDestinationRequestComplete (std::move (result), receiver);
						});
				}
			}
			else
			{
				if (receiver->bufferOffset < BOB_COMMAND_BUFFER_SIZE)
					ReceiveAddress (receiver);
				else
				{
					LogPrint (eLogError, "BOB: Missing inbound address");
					CloseReceiver (receiver);
				}
			}
		}
	}

	void BOBI2PInboundTunnel::HandleDestinationRequestComplete (
		std::shared_ptr<i2p::data::LeaseSet> leaseSet, std::shared_ptr<AddressReceiver> receiver)
	{
		if (m_Stopped.load (std::memory_order_acquire)) return;
		if (leaseSet)
			CreateConnection (receiver, leaseSet);
		else
		{
			LogPrint (eLogError, "BOB: LeaseSet for inbound destination not found");
			CloseReceiver (receiver);
		}
	}

	void BOBI2PInboundTunnel::CreateConnection (
		std::shared_ptr<AddressReceiver> receiver, std::shared_ptr<const i2p::data::LeaseSet> leaseSet)
	{
		std::shared_ptr<I2PTunnelConnection> connection;
		{
			std::lock_guard lock (m_AdmissionMutex);
			if (!m_Stopped.load (std::memory_order_acquire))
			{
				LogPrint (eLogDebug, "BOB: New inbound connection");
				connection = std::make_shared<I2PTunnelConnection> (shared_from_this (), receiver->socket, leaseSet);
				AddHandler (connection);
			}
			m_Receivers.erase (receiver);
		}
		if (connection)
			connection->I2PConnect (receiver->data, receiver->dataLen);
		else
		{
			boost::system::error_code ec;
			receiver->socket->close (ec);
		}
	}

	void BOBI2PInboundTunnel::CloseReceiver (const std::shared_ptr<AddressReceiver> & receiver)
	{
		if (!receiver) return;
		{
			std::lock_guard lock (m_AdmissionMutex);
			m_Receivers.erase (receiver);
		}
		if (receiver->socket)
		{
			boost::system::error_code ec;
			receiver->socket->close (ec);
		}
	}

	BOBI2POutboundTunnel::BOBI2POutboundTunnel (
		const std::string & outhost, uint16_t port, std::shared_ptr<ClientDestination> localDestination, bool quiet)
		: BOBI2PTunnel (localDestination)
		, m_Endpoint (boost::asio::ip::make_address (outhost), port)
		, m_IsQuiet (quiet)
	{
	}

	void BOBI2POutboundTunnel::Start ()
	{
		if (!m_Stopped.exchange (false, std::memory_order_acq_rel)) return;
		Accept ();
	}

	void BOBI2POutboundTunnel::Stop ()
	{
		{
			std::lock_guard lock (m_AdmissionMutex);
			if (m_Stopped.exchange (true, std::memory_order_acq_rel)) return;
		}
		auto localDestination = GetLocalDestination ();
		if (localDestination) localDestination->StopAcceptingStreams ();
		ClearHandlers ();
	}

	void BOBI2POutboundTunnel::Accept ()
	{
		if (m_Stopped.load (std::memory_order_acquire)) return;
		auto localDestination = GetLocalDestination ();
		if (localDestination)
		{
			auto weak = std::weak_ptr<BOBI2POutboundTunnel> (
				std::static_pointer_cast<BOBI2POutboundTunnel> (I2PService::shared_from_this ()));
			localDestination->AcceptStreams ([weak] (std::shared_ptr<i2p::stream::Stream> stream)
				{
					if (auto self = weak.lock ()) self->HandleAccept (std::move (stream));
					else if (stream) stream->AsyncClose ();
				});
		}
		else
			LogPrint (eLogError, "BOB: Local destination not set for server tunnel");
	}

	void BOBI2POutboundTunnel::HandleAccept (std::shared_ptr<i2p::stream::Stream> stream)
	{
		std::shared_ptr<BOBI2PTunnelIncomingConnection> conn;
		{
			std::lock_guard lock (m_AdmissionMutex);
			if (!m_Stopped.load (std::memory_order_acquire) && stream)
			{
				conn = std::make_shared<BOBI2PTunnelIncomingConnection> (
					shared_from_this (), stream, m_Endpoint, m_IsQuiet);
				AddHandler (conn);
			}
		}
		if (conn)
			conn->Connect ();
		else if (stream)
			stream->AsyncClose ();
	}

	BOBDestination::BOBDestination (std::shared_ptr<ClientDestination> localDestination, const std::string & nickname,
		const std::string & inhost, const std::string & outhost, const uint16_t inport, const uint16_t outport,
		const bool quiet)
		: m_LocalDestination (localDestination)
		, m_OutboundTunnel (nullptr)
		, m_InboundTunnel (nullptr)
		, m_Nickname (nickname)
		, m_InHost (inhost)
		, m_OutHost (outhost)
		, m_InPort (inport)
		, m_OutPort (outport)
		, m_Quiet (quiet)
		, m_IsRunning (false)
	{
	}

	BOBDestination::~BOBDestination ()
	{
		i2p::client::context.DeleteLocalDestination (m_LocalDestination);
	}

	void BOBDestination::Start ()
	{
		if (m_OutboundTunnel) m_OutboundTunnel->Start ();
		if (m_InboundTunnel) m_InboundTunnel->Start ();
		m_IsRunning = true;
	}

	void BOBDestination::Stop ()
	{
		StopTunnels ();
		m_LocalDestination->Stop ();
	}

	void BOBDestination::StopTunnels ()
	{
		m_IsRunning = false;
		if (m_OutboundTunnel)
		{
			m_OutboundTunnel->Stop ();
			m_OutboundTunnel = nullptr;
		}
		if (m_InboundTunnel)
		{
			m_InboundTunnel->Stop ();
			m_InboundTunnel = nullptr;
		}
	}

	void BOBDestination::CreateInboundTunnel (uint16_t port, const std::string & inhost)
	{
		if (!m_InboundTunnel)
		{
			// update inport and inhost (user can stop tunnel and change)
			m_InPort = port;
			m_InHost = inhost;
			boost::asio::ip::tcp::endpoint ep (boost::asio::ip::tcp::v4 (), port);
			if (!inhost.empty ())
			{
				boost::system::error_code ec;
				auto addr = boost::asio::ip::make_address (inhost, ec);
				if (!ec)
					ep.address (addr);
				else
					LogPrint (eLogError, "BOB: ", ec.message ());
			}
			m_InboundTunnel = std::make_shared<BOBI2PInboundTunnel> (ep, m_LocalDestination);
		}
	}

	void BOBDestination::CreateOutboundTunnel (const std::string & outhost, uint16_t port, bool quiet)
	{
		if (!m_OutboundTunnel)
		{
			// update outport and outhost (user can stop tunnel and change)
			m_OutPort = port;
			m_OutHost = outhost;
			m_OutboundTunnel = std::make_shared<BOBI2POutboundTunnel> (outhost, port, m_LocalDestination, quiet);
		}
	}

	BOBCommandSession::BOBCommandSession (BOBCommandChannel & owner)
		: m_Owner (owner)
		, m_Socket (m_Owner.GetService ())
		, m_ReadBuffer { }
		, m_SendBuffer (BOB_MAX_REPLY_SIZE)
		, m_IsOpen (true)
		, m_IsQuiet (false)
		, m_IsActive (false)
		, m_InPort (0)
		, m_OutPort (0)
		, m_CurrentDestination (nullptr)
		, m_TunnelType (TunnelType::STANDARD)
	{
	}

	BOBCommandSession::~BOBCommandSession ()
	{
		if (m_Admitted && m_Admission) ReleaseBOBSession (*m_Admission);
	}

	bool BOBCommandSession::Admit (const std::shared_ptr<BOBAdmissionState> & admission)
	{
		if (!admission) return false;
		if (!TryAcquireBOBSession (*admission)) return false;
		m_Admission = admission;
		m_Admitted = true;
		return true;
	}

	void BOBCommandSession::Terminate ()
	{
		boost::system::error_code ignored;
		m_Socket.close (ignored);
		m_IsOpen = false;
	}

	void BOBCommandSession::Receive ()
	{
		// A pipelined command that already sits in the reader is executed
		// before another socket read, so the accumulator cannot grow past one
		// bounded line while the session is waiting for a reply to be sent.
		if (m_LineReader.HasCompleteLine ())
		{
			HandleLine ();
			return;
		}
		Read ();
	}

	void BOBCommandSession::Read ()
	{
		m_Socket.async_read_some (boost::asio::buffer (m_ReadBuffer),
			std::bind (&BOBCommandSession::HandleReceivedBytes, shared_from_this (), std::placeholders::_1,
				std::placeholders::_2));
	}

	void BOBCommandSession::HandleReceivedBytes (const boost::system::error_code & ecode, std::size_t bytes_transferred)
	{
		if (ecode)
		{
			if (ecode != boost::asio::error::operation_aborted)
				LogPrint (eLogDebug, "BOB: Command channel read closed");
			if (ecode != boost::asio::error::operation_aborted) Terminate ();
			return;
		}
		const auto result = m_LineReader.Feed (
			std::string_view (m_ReadBuffer.data (), bytes_transferred));
		if (result == BOBCommandLineReader::Result::Overflow)
		{
			// A newline-free flood: close instead of buffering more input.
			LogPrint (eLogWarning, "BOB: Command line limit exceeded, closing session");
			Terminate ();
			return;
		}
		Receive ();
	}

	void BOBCommandSession::HandleLine ()
	{
		std::string line = m_LineReader.TakeLine ();

		std::string command, operand;
		std::istringstream iss (line);
		iss >> command >> operand;

		// process command
		auto & handlers = m_Owner.GetCommandHandlers ();
		auto it = handlers.find (command);
		if (it != handlers.end ())
		{
			(this->*(it->second)) (operand.c_str (), operand.length ());
		}
		else
		{
			// The command word is not a secret; the operand may be (a keypair
			// or a password), so it is never logged.
			LogPrint (eLogError, "BOB: Unknown command ", command.c_str ());
			SendReplyError ("unknown command");
		}
	}

	void BOBCommandSession::Send ()
	{
		boost::asio::async_write (m_Socket, m_SendBuffer, boost::asio::transfer_all (),
			std::bind (
				&BOBCommandSession::HandleSent, shared_from_this (), std::placeholders::_1, std::placeholders::_2));
	}

	void BOBCommandSession::HandleSent (const boost::system::error_code & ecode, std::size_t)
	{
		if (ecode)
		{
			LogPrint (eLogError, "BOB: Command channel send error: ", ecode.message ());
			if (ecode != boost::asio::error::operation_aborted) Terminate ();
		}
		else
		{
			if (m_IsOpen)
				Receive ();
			else
				Terminate ();
		}
	}

	void BOBCommandSession::SendReplyOK (std::string_view msg)
	{
		std::ostream os (&m_SendBuffer);
		os << "OK";
		if (!msg.empty ()) os << " " << msg;
		os << std::endl;
		Send ();
	}

	void BOBCommandSession::SendReplyOK (const std::vector<std::string_view> & strings)
	{
		std::ostream os (&m_SendBuffer);
		os << "OK";
		if (!strings.empty ()) os << " ";
		for (auto & it : strings) os << it;
		os << std::endl;
		Send ();
	}

	void BOBCommandSession::SendReplyError (std::string_view msg)
	{
		std::ostream os (&m_SendBuffer);
		os << "ERROR " << msg << std::endl;
		Send ();
	}

	void BOBCommandSession::SendVersion ()
	{
		std::ostream os (&m_SendBuffer);
		os << "BOB 00.00.10" << std::endl;
		SendReplyOK (std::string_view ()); // empty string
	}

	void BOBCommandSession::SendRaw (std::string_view data)
	{
		std::ostream os (&m_SendBuffer);
		os << data << std::endl;
	}

	std::string BOBCommandSession::BuildStatusLine (bool currentTunnel, std::shared_ptr<BOBDestination> dest)
	{
		// helper lambdas
		constexpr auto issetStr
			= [] (const std::string & str) { return !str.empty () ? str : "not_set"; }; // for inhost, outhost
		const auto issetNum = [] (int p) { return p ? std::to_string (p) : "not_set"; }; // for inport, outport

		// tunnel info
		auto nickname = currentTunnel ? m_Nickname : (dest ? dest->GetNickname () : "");
		bool quiet = currentTunnel ? m_IsQuiet : (dest ? dest->GetQuiet () : true);
		auto inhost = issetStr (currentTunnel ? m_InHost : (dest ? dest->GetInHost () : ""));
		auto outhost = issetStr (currentTunnel ? m_OutHost : (dest ? dest->GetOutHost () : ""));
		auto inport = issetNum (currentTunnel ? m_InPort : (dest ? dest->GetInPort () : 0));
		auto outport = issetNum (currentTunnel ? m_OutPort : (dest ? dest->GetOutPort () : 0));
		bool keys = (bool)dest; // key must exist when destination is created
		bool starting = dest && !dest->IsRunning ();
		bool running = dest && dest->IsRunning ();

		auto [proxy, proxyType] = m_Owner.GetProxy (nickname);
		// build line
		std::stringstream ss;
		ss << "DATA "
		   << "NICKNAME: " << nickname << " " << "STARTING: " << std::boolalpha << starting << " "
		   << "RUNNING: " << std::boolalpha << running << " " << "STOPPING: " << "false "
		   << "KEYS: " << std::boolalpha << keys << " " << "QUIET: " << std::boolalpha << quiet << " "
		   << "INPORT: " << inport << " " << "INHOST: " << inhost << " "
		   << "OUTPORT: " << outport << " " << "OUTHOST: " << outhost << " "
		   << "PROXYTYPE: " << proxyType << " " << "PROXYSTART: " << std::boolalpha << (bool)proxy;
		return ss.str ();
	}

	void BOBCommandSession::ZapCommandHandler (const char *, size_t)
	{
		LogPrint (eLogDebug, "BOB: zap");
		Terminate ();
	}

	void BOBCommandSession::QuitCommandHandler (const char *, size_t)
	{
		LogPrint (eLogDebug, "BOB: quit");
		m_IsOpen = false;
		SendReplyOK ("Bye!");
	}

	void BOBCommandSession::StartCommandHandler (const char *, size_t)
	{
		LogPrint (eLogDebug, "BOB: start ", m_Nickname);
		if (m_IsActive)
		{
			SendReplyError ("tunnel is active");
			return;
		}
		if (!m_Keys.GetPublic ()) // keys are set ?
		{
			SendReplyError ("Keys must be set.");
			return;
		}
		if (m_InPort == 0 && m_OutHost.empty () && m_OutPort == 0)
		{
			SendReplyError ("(inhost):inport or outhost:outport must be set.");
			return;
		}
		if (!m_InHost.empty ())
		{
			// TODO: FIXME: temporary validation, until hostname support is added
			boost::system::error_code ec;
			boost::asio::ip::make_address (m_InHost, ec);
			if (ec)
			{
				SendReplyError ("inhost must be a valid IPv4 address.");
				return;
			}
		}
		if (!m_OutHost.empty ())
		{
			// TODO: FIXME: temporary validation, until hostname support is added
			boost::system::error_code ec;
			boost::asio::ip::make_address (m_OutHost, ec);
			if (ec)
			{
				SendReplyError ("outhost must be a IPv4 address.");
				return;
			}
		}

		if (!m_CurrentDestination)
		{
			m_CurrentDestination = std::make_shared<BOBDestination> (
				i2p::client::context.CreateNewLocalDestination (m_Keys, true, &m_Options), // deleted in clear command
				m_Nickname, m_InHost, m_OutHost, m_InPort, m_OutPort, m_IsQuiet);
			m_Owner.AddDestination (m_Nickname, m_CurrentDestination);
		}
		switch (m_TunnelType)
		{
		case TunnelType::STANDARD:
		{
			if (m_InPort) m_CurrentDestination->CreateInboundTunnel (m_InPort, m_InHost);
			if (m_OutPort && !m_OutHost.empty ())
				m_CurrentDestination->CreateOutboundTunnel (m_OutHost, m_OutPort, m_IsQuiet);
			m_CurrentDestination->Start ();
			break;
		}
		case TunnelType::SOCKS:
		{
			try
			{
				auto socksProxy = std::make_shared<i2p::proxy::SOCKSProxy> (m_Nickname, m_InHost, m_InPort, false,
					m_OutHost, m_OutPort, m_CurrentDestination->GetLocalDestination ());
				socksProxy->Start ();
				m_Owner.AddProxy (m_Nickname, std::move (socksProxy), "SOCKS");
			}
			catch (std::exception & e)
			{
				LogPrint (eLogCritical, "Clients: Exception in SOCKS Proxy: ", e.what ());
				ThrowFatal ("Unable to start SOCKS Proxy at ", m_InHost, ":", m_InPort, ": ", e.what ());
			}
			break;
		}
		case TunnelType::HTTP_PROXY:
		{
			try
			{
				auto httpProxy = std::make_shared<i2p::proxy::HTTPProxy> (m_Nickname, m_InHost, m_InPort, m_OutHost,
					true, true, m_CurrentDestination->GetLocalDestination ());
				httpProxy->Start ();
				m_Owner.AddProxy (m_Nickname, std::move (httpProxy), "HTTPPROXY");
			}
			catch (std::exception & e)
			{
				LogPrint (eLogCritical, "Clients: Exception in HTTP Proxy: ", e.what ());
				ThrowFatal ("Unable to start HTTP Proxy at ", m_InHost, ":", m_InPort, ": ", e.what ());
			}
			break;
		}
		default: SendReplyError ("Unsupported tunnel type."); return;
		}
		SendReplyOK ("Tunnel starting");
		m_IsActive = true;
	}

	void BOBCommandSession::StopCommandHandler (const char *, size_t)
	{
		LogPrint (eLogDebug, "BOB: stop ", m_Nickname);
		if (!m_IsActive)
		{
			SendReplyError ("tunnel is inactive");
			return;
		}
		auto dest = m_Owner.FindDestination (m_Nickname);
		if (dest)
		{
			dest->StopTunnels ();
			SendReplyOK ("Tunnel stopping");
			auto [proxy, proxyType] = m_Owner.GetProxy (m_Nickname);
			if (proxy) m_Owner.RemoveProxy (m_Nickname);
		}
		else
			SendReplyError ("tunnel not found");
		m_IsActive = false;
	}

	void BOBCommandSession::SetNickCommandHandler (const char * operand, size_t)
	{
		LogPrint (eLogDebug, "BOB: setnick");
		if (*operand)
		{
			auto dest = m_Owner.FindDestination (operand);
			if (!dest)
			{
				m_Nickname = operand;
				SendReplyOK ({ "Nickname set to ", m_Nickname });
			}
			else
				SendReplyError ("tunnel is active");
		}
		else
			SendReplyError ("no nickname has been set");
	}

	void BOBCommandSession::GetNickCommandHandler (const char * operand, size_t)
	{
		LogPrint (eLogDebug, "BOB: getnick");
		if (*operand)
		{
			m_CurrentDestination = m_Owner.FindDestination (operand);
			if (m_CurrentDestination)
			{
				m_Keys = m_CurrentDestination->GetKeys ();
				m_IsActive = m_CurrentDestination->IsRunning ();
				if (!m_IsActive)
				{
					auto [proxy, proxyType] = m_Owner.GetProxy (operand);
					if (proxy) m_IsActive = true;
				}
				m_Nickname = operand;
			}
			if (m_Nickname == operand)
				SendReplyOK ({ "Nickname set to ", m_Nickname });
			else
				SendReplyError ("no nickname has been set");
		}
		else
			SendReplyError ("no nickname has been set");
	}

	void BOBCommandSession::NewkeysCommandHandler (const char * operand, size_t)
	{
		LogPrint (eLogDebug, "BOB: newkeys");
		i2p::data::SigningKeyType signatureType = i2p::data::SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519;
		i2p::data::CryptoKeyType cryptoType = i2p::data::CRYPTO_KEY_TYPE_ELGAMAL;
		if (*operand)
		{
			// Parse without throwing: the previous std::stoi could raise
			// std::out_of_range out of a command handler.
			std::string_view types (operand);
			const auto separator = types.find (' ');
			unsigned signature = 0;
			if (!ParseSmallUnsigned (types.substr (0, separator), signature))
			{
				SendReplyError ("invalid signature type");
				return;
			}
			unsigned crypto = 0;
			if (separator != std::string_view::npos
				&& !ParseSmallUnsigned (types.substr (separator + 1), crypto))
			{
				SendReplyError ("invalid crypto type");
				return;
			}
			signatureType = static_cast<i2p::data::SigningKeyType> (signature);
			if (separator != std::string_view::npos)
				cryptoType = static_cast<i2p::data::CryptoKeyType> (crypto);
		}

		m_Keys = i2p::data::PrivateKeys::CreateRandomKeys (signatureType, cryptoType, true);
		SendReplyOK (m_Keys.GetPublic ()->ToBase64 ());
	}

	void BOBCommandSession::SetkeysCommandHandler (const char * operand, size_t)
	{
		LogPrint (eLogDebug, "BOB: setkeys");
		if (*operand && m_Keys.FromBase64 (operand))
			SendReplyOK (m_Keys.GetPublic ()->ToBase64 ());
		else
			SendReplyError ("invalid keys");
	}

	void BOBCommandSession::GetkeysCommandHandler (const char *, size_t)
	{
		LogPrint (eLogDebug, "BOB: getkeys request");
		// The reply carries a destination's private keys. It is refused unless
		// the operator explicitly enabled the option: BOB itself authenticates
		// nothing beyond the local peer.
		if (!m_Owner.IsGetKeysAllowed ())
		{
			LogPrint (eLogWarning, "BOB: getkeys is disabled; set bob.allowgetkeys=true to enable it");
			SendReplyError ("getkeys is disabled");
			return;
		}
		if (m_Keys.GetPublic ()) // keys are set ?
			SendReplyOK (m_Keys.ToBase64 ());
		else
			SendReplyError ("keys are not set");
	}

	void BOBCommandSession::GetdestCommandHandler (const char *, size_t)
	{
		LogPrint (eLogDebug, "BOB: getdest");
		if (m_Keys.GetPublic ()) // keys are set ?
			SendReplyOK (m_Keys.GetPublic ()->ToBase64 ());
		else
			SendReplyError ("keys are not set");
	}

	void BOBCommandSession::OuthostCommandHandler (const char * operand, size_t)
	{
		LogPrint (eLogDebug, "BOB: outhost");
		if (*operand)
		{
			m_OutHost = operand;
			SendReplyOK ("outhost set");
		}
		else
			SendReplyError ("empty outhost");
	}

	void BOBCommandSession::OutportCommandHandler (const char * operand, size_t)
	{
		LogPrint (eLogDebug, "BOB: outport");
		uint16_t port = 0;
		switch (ParseBOBPort (std::string_view (operand), port))
		{
		case BOBPortResult::Ok:
			m_OutPort = port;
			SendReplyOK ("outbound port set");
			return;
		case BOBPortResult::Empty: SendReplyError ("empty outport"); return;
		case BOBPortResult::Invalid: SendReplyError ("invalid port"); return;
		case BOBPortResult::OutOfRange:
		default: SendReplyError ("port out of range"); return;
		}
	}

	void BOBCommandSession::InhostCommandHandler (const char * operand, size_t)
	{
		LogPrint (eLogDebug, "BOB: inhost");
		if (*operand)
		{
			m_InHost = operand;
			SendReplyOK ("inhost set");
		}
		else
			SendReplyError ("empty inhost");
	}

	void BOBCommandSession::InportCommandHandler (const char * operand, size_t)
	{
		LogPrint (eLogDebug, "BOB: inport");
		uint16_t port = 0;
		switch (ParseBOBPort (std::string_view (operand), port))
		{
		case BOBPortResult::Ok:
			m_InPort = port;
			SendReplyOK ("inbound port set");
			return;
		case BOBPortResult::Empty: SendReplyError ("empty inport"); return;
		case BOBPortResult::Invalid: SendReplyError ("invalid port"); return;
		case BOBPortResult::OutOfRange:
		default: SendReplyError ("port out of range"); return;
		}
	}

	void BOBCommandSession::QuietCommandHandler (const char *, size_t)
	{
		LogPrint (eLogDebug, "BOB: quiet");
		if (m_Nickname.length () > 0)
		{
			if (!m_IsActive)
			{
				m_IsQuiet = true;
				SendReplyOK ("Quiet set");
			}
			else
				SendReplyError ("tunnel is active");
		}
		else
			SendReplyError ("no nickname has been set");
	}

	void BOBCommandSession::LookupCommandHandler (const char * operand, size_t)
	{
		LogPrint (eLogDebug, "BOB: lookup");
		if (*operand)
		{
			auto addr = context.GetAddressBook ().GetAddress (operand);
			if (!addr)
			{
				SendReplyError ("Address Not found");
				return;
			}
			auto localDestination = (m_CurrentDestination && m_CurrentDestination->IsRunning ())
				? m_CurrentDestination->GetLocalDestination ()
				: i2p::client::context.GetSharedLocalDestination ();
			if (!localDestination)
			{
				SendReplyError ("No local destination");
				return;
			}
			if (addr->IsIdentHash ())
			{
				// we might have leaseset already
				auto leaseSet = localDestination->FindLeaseSet (addr->identHash);
				if (leaseSet)
				{
					SendReplyOK (leaseSet->GetIdentity ()->ToBase64 ());
					return;
				}
			}
			// trying to request
			auto s = shared_from_this ();
			auto requstCallback = [s] (std::shared_ptr<i2p::data::LeaseSet> ls)
			{
				if (ls)
					s->SendReplyOK (ls->GetIdentity ()->ToBase64 ());
				else
					s->SendReplyError ("LeaseSet Not found");
			};
			if (addr->IsIdentHash ())
				localDestination->RequestDestination (addr->identHash, requstCallback);
			else
				localDestination->RequestDestinationWithEncryptedLeaseSet (addr->blindedPublicKey, requstCallback);
		}
		else
			SendReplyError ("empty lookup address");
	}

	void BOBCommandSession::LookupLocalCommandHandler (const char * operand, size_t)
	{
		LogPrint (eLogDebug, "BOB: lookup local");
		if (*operand)
		{
			auto addr = context.GetAddressBook ().GetAddress (operand);
			if (!addr)
			{
				SendReplyError ("Address Not found");
				return;
			}
			auto ls = i2p::data::netdb.FindLeaseSet (addr->identHash);
			if (ls)
				SendReplyOK (ls->GetIdentity ()->ToBase64 ());
			else
				SendReplyError ("Local LeaseSet Not found");
		}
		else
			SendReplyError ("empty lookup address");
	}

	void BOBCommandSession::PingCommandHandler (const char * operand, size_t)
	{
		LogPrint (eLogDebug, "BOB: ping");
		if (*operand)
		{
			auto addr = context.GetAddressBook ().GetAddress (operand);
			if (!addr)
			{
				SendReplyError ("Address Not found");
				return;
			}
			auto localDestination = (m_CurrentDestination && m_CurrentDestination->IsRunning ())
				? m_CurrentDestination->GetLocalDestination ()
				: i2p::client::context.GetSharedLocalDestination ();
			if (!localDestination)
			{
				SendReplyError ("No local destination");
				return;
			}
			if (addr->IsIdentHash ())
			{
				// we might have leaseset already
				auto leaseSet = localDestination->FindLeaseSet (addr->identHash);
				if (leaseSet)
				{
					boost::asio::post (localDestination->GetService (),
						std::bind (&BOBCommandSession::SendPing, shared_from_this (), leaseSet));
					return;
				}
			}
			// request LeaseSet
			if (addr->IsIdentHash ())
				localDestination->RequestDestination (addr->identHash,
					std::bind (&BOBCommandSession::SendPing, shared_from_this (), std::placeholders::_1));
			else
				localDestination->RequestDestinationWithEncryptedLeaseSet (addr->blindedPublicKey,
					std::bind (&BOBCommandSession::SendPing, shared_from_this (), std::placeholders::_1));
		}
		else
			SendReplyError ("empty ping address");
	}

	void BOBCommandSession::SendPing (std::shared_ptr<const i2p::data::LeaseSet> ls)
	{
		if (!ls)
		{
			SendReplyError ("LeaseSet Not found");
			return;
		}
		auto localDestination = (m_CurrentDestination && m_CurrentDestination->IsRunning ())
			? m_CurrentDestination->GetLocalDestination ()
			: i2p::client::context.GetSharedLocalDestination ();
		if (!localDestination)
		{
			SendReplyError ("No local destination");
			return;
		}
		auto streamingDestination = localDestination->GetStreamingDestination ();
		if (!streamingDestination)
		{
			SendReplyError ("No streaming destination");
			return;
		}
		auto timer = std::make_shared<boost::asio::steady_timer> (localDestination->GetService ());
		timer->expires_after (std::chrono::milliseconds (BOB_PING_TIMEOUT));
		timer->async_wait (
			[streamingDestination, s = shared_from_this ()] (const boost::system::error_code & ecode)
			{
				if (ecode != boost::asio::error::operation_aborted)
				{
					LogPrint (eLogDebug, "BOB: Pong not received after ", BOB_PING_TIMEOUT, " millliseconds");
					streamingDestination->ResetPongHandler ();
					s->SendReplyError ("timeout");
				}
			});
		auto ts = i2p::util::GetMillisecondsSinceEpoch ();
		streamingDestination->SetPongHandler (
			[streamingDestination, timer, ts, s = shared_from_this ()] (const i2p::data::IdentHash * from)
			{
				int t = static_cast<int> (i2p::util::GetMillisecondsSinceEpoch () - ts);
				if (t < 0) t = 0;
				streamingDestination->ResetPongHandler ();
				timer->cancel ();
				if (from)
					s->SendReplyOK (
						{ "pong ", "from ", from->ToBase32 (), ".b32.i2p: time=", std::to_string (t), " ms" });
				else
					s->SendReplyOK ({ "pong: time=", std::to_string (t), " ms" });
			});
		streamingDestination->SendPing (ls);
	}

	void BOBCommandSession::ClearCommandHandler (const char *, size_t)
	{
		LogPrint (eLogDebug, "BOB: clear");
		m_Owner.DeleteDestination (m_Nickname);
		m_Nickname = "";
		SendReplyOK ("cleared");
	}

	void BOBCommandSession::ListCommandHandler (const char *, size_t)
	{
		LogPrint (eLogDebug, "BOB: list");
		bool sentCurrent = false;
		const auto & destinations = m_Owner.GetDestinations ();
		for (const auto & it : destinations)
		{
			SendRaw (BuildStatusLine (false, it.second));
			if (m_Nickname.compare (it.second->GetNickname ()) == 0) sentCurrent = true;
		}
		if (!sentCurrent && !m_Nickname.empty ())
			// add the current tunnel to the list.
			// this is for the incomplete tunnel which has not been started yet.
			SendRaw (BuildStatusLine (true, m_CurrentDestination));

		SendReplyOK ("Listing done");
	}

	void BOBCommandSession::OptionCommandHandler (const char * operand, size_t)
	{
		LogPrint (eLogDebug, "BOB: option");
		const char * value = strchr (operand, '=');
		if (value)
		{
			*(const_cast<char *> (value)) = 0;
			m_Options.Insert (operand, value + 1);
			SendReplyOK ({ "option ", operand, " set to ", value + 1 });
			*(const_cast<char *> (value)) = '=';
		}
		else
			SendReplyError ("malformed");
	}

	void BOBCommandSession::StatusCommandHandler (const char * operand, size_t)
	{
		LogPrint (eLogDebug, "BOB: status");
		const std::string name = operand;

		// always prefer destination
		auto dest = m_Owner.FindDestination (name);
		if (dest)
			// tunnel destination exists
			SendReplyOK (BuildStatusLine (false, dest));
		else
		{
			if (m_Nickname == name && !name.empty ())
				// tunnel is incomplete / has not been started yet
				SendReplyOK (BuildStatusLine (true, nullptr));
			else
				SendReplyError ("no nickname has been set");
		}
	}
	void BOBCommandSession::HelpCommandHandler (const char * operand, size_t)
	{
		auto helpStrings = m_Owner.GetHelpStrings ();
		if (!*operand)
		{
			std::stringstream ss;
			ss << "COMMANDS:";
			for (auto const & x : helpStrings)
			{
				ss << " " << x.first;
			}
			SendReplyOK (ss.str ());
		}
		else
		{
			auto it = helpStrings.find (operand);
			if (it != helpStrings.end ())
			{
				SendReplyOK (it->second);
				return;
			}
			SendReplyError ("No such command");
		}
	}

	void BOBCommandSession::SetTunnelTypeCommandHandler (const char * operand, size_t len)
	{
		std::string_view sv (operand, len);
		LogPrint (eLogDebug, "BOB: settunneltype");
		if (sv == "socks")
		{
			m_TunnelType = TunnelType::SOCKS;
			SendReplyOK ("tunnel type set to SOCKS");
		}
		else if (sv == "httpproxy")
		{
			m_TunnelType = TunnelType::HTTP_PROXY;
			SendReplyOK ("tunnel type set to HTTP proxy");
		}
		else
		{
			m_TunnelType = TunnelType::STANDARD;
			SendReplyError ("no tunnel type has been set");
		}
	}

	BOBCommandChannel::BOBCommandChannel (const std::string & address, uint16_t port)
		: RunnableService ("BOB")
		, m_Acceptor (GetIOService (), boost::asio::ip::tcp::endpoint (boost::asio::ip::make_address (address), port))
	{
		// command -> handler
		m_CommandHandlers[BOB_COMMAND_ZAP] = &BOBCommandSession::ZapCommandHandler;
		m_CommandHandlers[BOB_COMMAND_QUIT] = &BOBCommandSession::QuitCommandHandler;
		m_CommandHandlers[BOB_COMMAND_START] = &BOBCommandSession::StartCommandHandler;
		m_CommandHandlers[BOB_COMMAND_STOP] = &BOBCommandSession::StopCommandHandler;
		m_CommandHandlers[BOB_COMMAND_SETNICK] = &BOBCommandSession::SetNickCommandHandler;
		m_CommandHandlers[BOB_COMMAND_GETNICK] = &BOBCommandSession::GetNickCommandHandler;
		m_CommandHandlers[BOB_COMMAND_NEWKEYS] = &BOBCommandSession::NewkeysCommandHandler;
		m_CommandHandlers[BOB_COMMAND_GETKEYS] = &BOBCommandSession::GetkeysCommandHandler;
		m_CommandHandlers[BOB_COMMAND_SETKEYS] = &BOBCommandSession::SetkeysCommandHandler;
		m_CommandHandlers[BOB_COMMAND_GETDEST] = &BOBCommandSession::GetdestCommandHandler;
		m_CommandHandlers[BOB_COMMAND_OUTHOST] = &BOBCommandSession::OuthostCommandHandler;
		m_CommandHandlers[BOB_COMMAND_OUTPORT] = &BOBCommandSession::OutportCommandHandler;
		m_CommandHandlers[BOB_COMMAND_INHOST] = &BOBCommandSession::InhostCommandHandler;
		m_CommandHandlers[BOB_COMMAND_INPORT] = &BOBCommandSession::InportCommandHandler;
		m_CommandHandlers[BOB_COMMAND_QUIET] = &BOBCommandSession::QuietCommandHandler;
		m_CommandHandlers[BOB_COMMAND_LOOKUP] = &BOBCommandSession::LookupCommandHandler;
		m_CommandHandlers[BOB_COMMAND_LOOKUP_LOCAL] = &BOBCommandSession::LookupLocalCommandHandler;
		m_CommandHandlers[BOB_COMMAND_PING] = &BOBCommandSession::PingCommandHandler;
		m_CommandHandlers[BOB_COMMAND_CLEAR] = &BOBCommandSession::ClearCommandHandler;
		m_CommandHandlers[BOB_COMMAND_LIST] = &BOBCommandSession::ListCommandHandler;
		m_CommandHandlers[BOB_COMMAND_OPTION] = &BOBCommandSession::OptionCommandHandler;
		m_CommandHandlers[BOB_COMMAND_STATUS] = &BOBCommandSession::StatusCommandHandler;
		m_CommandHandlers[BOB_COMMAND_HELP] = &BOBCommandSession::HelpCommandHandler;
		m_CommandHandlers[BOB_COMMAND_SETTUNNELTYPE] = &BOBCommandSession::SetTunnelTypeCommandHandler;
		// command -> help string
		m_HelpStrings[BOB_COMMAND_ZAP] = BOB_HELP_ZAP;
		m_HelpStrings[BOB_COMMAND_QUIT] = BOB_HELP_QUIT;
		m_HelpStrings[BOB_COMMAND_START] = BOB_HELP_START;
		m_HelpStrings[BOB_COMMAND_STOP] = BOB_HELP_STOP;
		m_HelpStrings[BOB_COMMAND_SETNICK] = BOB_HELP_SETNICK;
		m_HelpStrings[BOB_COMMAND_GETNICK] = BOB_HELP_GETNICK;
		m_HelpStrings[BOB_COMMAND_NEWKEYS] = BOB_HELP_NEWKEYS;
		m_HelpStrings[BOB_COMMAND_GETKEYS] = BOB_HELP_GETKEYS;
		m_HelpStrings[BOB_COMMAND_SETKEYS] = BOB_HELP_SETKEYS;
		m_HelpStrings[BOB_COMMAND_GETDEST] = BOB_HELP_GETDEST;
		m_HelpStrings[BOB_COMMAND_OUTHOST] = BOB_HELP_OUTHOST;
		m_HelpStrings[BOB_COMMAND_OUTPORT] = BOB_HELP_OUTPORT;
		m_HelpStrings[BOB_COMMAND_INHOST] = BOB_HELP_INHOST;
		m_HelpStrings[BOB_COMMAND_INPORT] = BOB_HELP_INPORT;
		m_HelpStrings[BOB_COMMAND_QUIET] = BOB_HELP_QUIET;
		m_HelpStrings[BOB_COMMAND_LOOKUP] = BOB_HELP_LOOKUP;
		m_HelpStrings[BOB_COMMAND_LOOKUP_LOCAL] = BOB_HELP_LOOKUP_LOCAL;
		m_HelpStrings[BOB_COMMAND_CLEAR] = BOB_HELP_CLEAR;
		m_HelpStrings[BOB_COMMAND_LIST] = BOB_HELP_LIST;
		m_HelpStrings[BOB_COMMAND_OPTION] = BOB_HELP_OPTION;
		m_HelpStrings[BOB_COMMAND_STATUS] = BOB_HELP_STATUS;
		m_HelpStrings[BOB_COMMAND_SETTUNNELTYPE] = BOB_HELP_SETTUNNELTYPE;
		m_HelpStrings[BOB_COMMAND_PING] = BOB_HELP_PING;
		m_HelpStrings[BOB_COMMAND_HELP] = BOB_HELP_HELP;
		// Fail closed: an absent or false option leaves the private-key reply
		// disabled, and reading an unregistered option is safe here.
		bool allowGetKeys = BOB_DEFAULT_ALLOW_GETKEYS;
		const bool found = i2p::config::GetOption (std::string (BOB_OPTION_ALLOW_GETKEYS), allowGetKeys);
		m_AllowGetKeys = ResolveBOBAllowGetKeys (found, allowGetKeys);
		if (m_AllowGetKeys) LogPrint (eLogWarning, "BOB: getkeys is enabled by configuration");
	}

	BOBCommandChannel::~BOBCommandChannel ()
	{
		if (IsRunning ()) Stop ();
	}

	void BOBCommandChannel::Start ()
	{
		Accept ();
		StartIOService ();
	}

	void BOBCommandChannel::Stop ()
	{
		for (auto & it : m_Destinations) it.second->Stop ();
		m_Acceptor.cancel ();
		StopIOService ();
	}

	void BOBCommandChannel::AddDestination (const std::string & name, std::shared_ptr<BOBDestination> dest)
	{
		m_Destinations.emplace (name, dest);
	}

	void BOBCommandChannel::DeleteDestination (const std::string & name)
	{
		auto it = m_Destinations.find (name);
		if (it != m_Destinations.end ())
		{
			it->second->Stop ();
			m_Destinations.erase (it);
		}
	}

	std::shared_ptr<BOBDestination> BOBCommandChannel::FindDestination (const std::string & name)
	{
		auto it = m_Destinations.find (name);
		if (it != m_Destinations.end ()) return it->second;
		return nullptr;
	}

	void BOBCommandChannel::AddProxy (
		const std::string & name, std::shared_ptr<I2PService> && proxy, std::string_view type)
	{
		m_Proxies.emplace (name, std::make_pair (std::move (proxy), type));
	}

	std::pair<std::shared_ptr<I2PService>, std::string> BOBCommandChannel::GetProxy (const std::string & name) const
	{
		auto it = m_Proxies.find (name);
		if (it != m_Proxies.end () && it->second.first) return it->second;
		return { nullptr, "" };
	}

	void BOBCommandChannel::RemoveProxy (const std::string & name)
	{
		m_Proxies.erase (name);
	}

	void BOBCommandChannel::Accept ()
	{
		auto newSession = std::make_shared<BOBCommandSession> (*this);
		m_Acceptor.async_accept (newSession->GetSocket (),
			std::bind (&BOBCommandChannel::HandleAccept, this, std::placeholders::_1, newSession));
	}

	void BOBCommandChannel::HandleAccept (
		const boost::system::error_code & ecode, std::shared_ptr<BOBCommandSession> session)
	{
		if (ecode != boost::asio::error::operation_aborted) Accept ();

		if (ecode)
		{
			LogPrint (eLogError, "BOB: Accept error: ", ecode.message ());
			return;
		}
		if (!IsTrustedCommandPeer (session->GetSocket ()))
		{
			// Never log the rejected operand: this path only reports the rule.
			LogPrint (eLogWarning, "BOB: Rejected a command connection that is not a local same-user peer");
			session->Terminate ();
			return;
		}
		if (!session->Admit (m_Admission))
		{
			LogPrint (eLogWarning, "BOB: Concurrent command session limit reached");
			session->Terminate ();
			return;
		}
		LogPrint (eLogInfo, "BOB: New command connection");
		session->SendVersion ();
	}
}
}

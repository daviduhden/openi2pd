/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: SAM.
 *
 * Implements the SAM bridge over TCP and UDP: version negotiation,
 * session/subsession management, stream connect/accept/forward, datagram and
 * raw send/receive, destination generation and naming lookups. A persistent
 * bounded input buffer frames pipelined commands, preserves split lines and
 * SEND payloads across reads, and serialises replies so protocol order is
 * unchanged. Version-gated port fields and bounded accept/session tracking
 * keep the protocol replies compatible and the peer-controlled state limited.
 * Control connections are admission-capped and carry a cancellable HELLO and
 * inter-command deadline; private-key and credential operands are never logged.
 *
 */

#include <string.h>
#include <cstdio>
#include <cstdlib>
#include <cctype>
#include <algorithm>
#include <charconv>
#include <chrono>
#include <future>
#include <vector>
#include "Base.h"
#include "Identity.h"
#include "Log.h"
#include "Destination.h"
#include "ClientContext.h"
#include "util.h"
#include "SAM.h"

namespace i2p
{
namespace client
{
	SAMSocket::SAMSocket (SAMBridge & owner)
		: m_Owner (owner)
		, m_Socket (owner.GetService ())
		, m_Timer (m_Owner.GetService ())
		, m_ControlDeadline (m_Owner.GetService ())
		, m_BufferOffset (0)
		, m_InputSize (0)
		, m_IsWriting (false)
		, m_SocketType (SAMSocketType::eSAMSocketTypeUnknown)
		, m_IsSilent (false)
		, m_IsAccepting (false)
		, m_IsReceiving (false)
		, m_Version (MIN_SAM_VERSION)
		, m_ConnectAttempts (0)
		, m_ConnectFromPort (0)
		, m_ConnectToPort (0)
	{
	}

	SAMSocket::~SAMSocket ()
	{
		m_Stream = nullptr;
		// Terminate() normally released the slot; a socket dropped without it
		// (io_context already stopped) must not leak the admission count.
		ReleaseControlConnection ();
	}

	void SAMSocket::Terminate (const char *)
	{
		// Terminate is idempotent and may be re-entered by CloseSession() ->
		// CloseStreams() -> Terminate() for the same socket.
		auto previousType = m_SocketType;
		if (previousType == SAMSocketType::eSAMSocketTypeTerminated) return;
		m_SocketType = SAMSocketType::eSAMSocketTypeTerminated;
		ResetAllDatagramReceivers ();
		CancelControlDeadline ();
		ReleaseControlConnection ();

		if (m_Stream)
		{
			m_Stream->AsyncClose ();
			m_Stream = nullptr;
		}
		switch (previousType)
		{
		case SAMSocketType::eSAMSocketTypeSession: m_Owner.CloseSession (m_ID); break;
		case SAMSocketType::eSAMSocketTypeStream: break;
		case SAMSocketType::eSAMSocketTypeAcceptor:
		case SAMSocketType::eSAMSocketTypeForward:
		{
			auto session = m_Owner.FindSession (m_ID);
			if (session)
			{
				session->RemoveAccept (shared_from_this ());
				if (previousType == SAMSocketType::eSAMSocketTypeForward) session->RemoveForwardAcceptor (m_ListenPort);
				if (m_IsAccepting && session->GetLocalDestination ())
					session->GetLocalDestination ()->StopAcceptingStreams (m_ListenPort);
				// The armed acceptor is gone: a queued ACCEPT for the same port
				// must be armed again or it would wait forever. The queued
				// sockets are separate control connections, so their own
				// deadlines are unaffected.
				RearmQueuedAccept (m_ListenPort);
			}
			break;
		}
		default:;
		}
		m_WriteQueue.clear ();
		m_IsWriting = false;
		if (m_Socket.is_open ())
		{
			boost::system::error_code ec;
			m_Socket.shutdown (boost::asio::ip::tcp::socket::shutdown_both, ec);
			m_Socket.close (ec);
		}
		m_Owner.RemoveSocket (shared_from_this ());
	}

	void SAMSocket::TrackDatagramReceiver (
		std::string_view sessionID, i2p::datagram::DatagramDestination::ReceiverRegistration registration)
	{
		m_DatagramReceivers[std::string (sessionID)].push_back (std::move (registration));
	}

	void SAMSocket::ResetDatagramReceivers (std::string_view sessionID)
	{
		auto it = m_DatagramReceivers.find (sessionID);
		if (it == m_DatagramReceivers.end ()) return;
		auto registrations = std::move (it->second);
		m_DatagramReceivers.erase (it);
		for (const auto & registration : registrations) registration.Reset ();
	}

	void SAMSocket::ResetAllDatagramReceivers ()
	{
		auto registrations = std::move (m_DatagramReceivers);
		m_DatagramReceivers.clear ();
		for (const auto & entry : registrations)
			for (const auto & registration : entry.second) registration.Reset ();
	}

	void SAMSocket::ReceiveHandshake ()
	{
		// The HELLO line is framed from the same bounded control input as the
		// commands that may be pipelined behind it in the same read.
		Receive ();
	}

	void SAMSocket::ArmControlDeadline (int seconds, const char * reason)
	{
		if (!m_Socket.is_open ()) return;
		m_ControlDeadlineReason = reason;
		m_ControlDeadline.expires_after (std::chrono::seconds (seconds));
		m_ControlDeadline.async_wait (
			std::bind (&SAMSocket::HandleControlDeadline, shared_from_this (), std::placeholders::_1));
	}

	void SAMSocket::CancelControlDeadline ()
	{
		// cancel() only aborts the wait; the handler still runs and returns on
		// the aborted error code, so a closed/reused timer cannot fire. The
		// supported Boost range has no error_code overload on this timer.
		m_ControlDeadline.cancel ();
	}

	void SAMSocket::HandleControlDeadline (const boost::system::error_code & ecode)
	{
		if (ecode == boost::asio::error::operation_aborted) return;
		if (m_SocketType == SAMSocketType::eSAMSocketTypeTerminated) return;
		LogPrint (eLogInfo, "SAM: Control connection closed: ", m_ControlDeadlineReason ? m_ControlDeadlineReason : "");
		Terminate (m_ControlDeadlineReason);
	}

	void SAMSocket::ReleaseControlConnection ()
	{
		// Exactly once per admitted socket, whatever terminates it. The counter
		// is shared, so a socket that outlives the bridge stays safe.
		if (!m_IsControlConnectionCounted.exchange (false)) return;
		if (m_ControlConnectionCounter) m_ControlConnectionCounter->fetch_sub (1);
	}

	bool SAMSocket::GetCommandParams (std::string_view buf, i2p::util::Mapping & params)
	{
		auto parsed = ParseSAMParams (buf);
		params = std::move (parsed.values);
		if (!parsed.duplicate) return true;
		// Java SAMUtils.parseParams() throws on a duplicate key, which closes
		// the control connection. There is no SAM error reply for this.
		LogPrint (eLogError, "SAM: Duplicate parameter key in command");
		Terminate ("SAM: duplicate parameter");
		return false;
	}

	static std::string CreateVersion (int ver)
	{
		auto d = std::div (ver, 10);
		return std::to_string (d.quot) + "." + std::to_string (d.rem);
	}

	static bool ParseSAMPort (const i2p::util::Mapping & params, std::string_view name, uint16_t & value)
	{
		if (!params.Contains (name)) return true; // absent, keep default
		return ParseSAMNumber (params[name], value);
	}

	static bool ParseSAMProtocol (const i2p::util::Mapping & params, std::string_view name, uint8_t & value)
	{
		if (!params.Contains (name)) return true; // absent, keep default
		return ParseSAMNumber (params[name], value);
	}

	namespace
	{
		std::string_view SkipSpaces (std::string_view str)
		{
			while (!str.empty () && str.front () == ' ') str.remove_prefix (1);
			return str;
		}

		bool NextToken (std::string_view & str, std::string_view & token)
		{
			str = SkipSpaces (str);
			if (str.empty ()) return false;
			auto pos = str.find (' ');
			if (pos == std::string_view::npos)
			{
				token = str;
				str = { };
			}
			else
			{
				token = str.substr (0, pos);
				str = str.substr (pos);
			}
			return !token.empty ();
		}

		bool IsSAMVersionToken (std::string_view token)
		{
			// SAMv3DatagramServer only accepts first-line versions beginning
			// with "3."; accept exactly that prefix here.
			return token.size () >= 2 && token[0] == '3' && token[1] == '.';
		}
	} // namespace

	bool ParseSAMDatagramHeader (std::string_view line, SAMDatagramHeader & header)
	{
		header = { };
		std::string_view rest = line;
		std::string_view first;
		if (!NextToken (rest, first)) return false;
		if (IsSAMVersionToken (first))
		{
			header.version = first;
			if (!NextToken (rest, header.sessionID)) return false;
		}
		else
			header.sessionID = first;
		if (!NextToken (rest, header.destination)) return false;
		header.options = SkipSpaces (rest);
		return true;
	}

	void SAMSocket::ConsumeInput (size_t len)
	{
		if (len >= m_InputSize)
		{
			m_InputSize = 0;
			return;
		}
		memmove (m_InputBuffer, m_InputBuffer + len, m_InputSize - len);
		m_InputSize -= len;
	}

	size_t SAMSocket::GetSAMSendLimit () const
	{
		// SAMv1Handler.checkDatagramSize()/checkSize(): a DATAGRAM SEND frame
		// carries at most 31744 bytes, a RAW SEND frame 32768. 0 tells the
		// framer that this socket has no SEND-capable session, so the line is
		// handed to the handler immediately instead of waiting for a payload
		// the client may never send.
		auto session = m_Owner.FindSession (m_ID);
		if (session && session->Type == SAMSessionType::eSAMSessionTypeRaw) return SAM_RAW_SEND_MAX_SIZE;
		if (session && session->Type == SAMSessionType::eSAMSessionTypeDatagram) return SAM_DATAGRAM_SEND_MAX_SIZE;
		return 0;
	}

	void SAMSocket::ProcessHandshakeLine (std::string_view line)
	{
		// The HELLO line can carry SAM credentials (AUTH/USER/PASSWORD style
		// clients) and is therefore never logged: only the negotiated version
		// and the outcome are.
		if (line.size () >= SAM_SOCKET_BUFFER_SIZE)
		{
			SendReplyWithMessage (SAM_HANDSHAKE_I2P_ERROR_MESSAGE, "HELLO line too long", true);
			return;
		}
		int selected = MIN_SAM_VERSION;
		const auto result = NegotiateSAMHandshake (line, selected);
		if (result == SAMHandshakeResult::eSAMHandshakeInvalid)
		{
			LogPrint (eLogError, "SAM: Malformed handshake");
			SendReplyWithMessage (SAM_HANDSHAKE_I2P_ERROR_MESSAGE, "Malformed HELLO VERSION", true);
			return;
		}
		if (result == SAMHandshakeResult::eSAMHandshakeNoVersion)
		{
			LogPrint (eLogError, "SAM: Handshake version mismatch");
			SendMessageReply (SAM_HANDSHAKE_NOVERSION, true);
			return;
		}
		m_Version = selected;
		m_IsHandshakeDone = true;
		LogPrint (eLogInfo, "SAM: Handshake done, version ", CreateVersion (m_Version));
		// The HELLO deadline is replaced by the inter-command deadline as soon
		// as the next control read starts.
		SendReplyWithMessage (SAM_HANDSHAKE_REPLY, CreateVersion (m_Version), false);
	}

	void SAMSocket::ContinueProcessing ()
	{
		if (m_SocketType == SAMSocketType::eSAMSocketTypeStream)
		{
			// Stream mode forwards every further client byte to the I2P stream.
			Receive ();
			return;
		}
		ProcessBufferedCommands ();
	}

	void SAMSocket::ProcessBufferedCommands ()
	{
		if (m_IsDispatching)
		{
			m_IsContinueRequested = true;
			return;
		}
		m_IsDispatching = true;
		for (;;)
		{
			m_IsContinueRequested = false;
			while (ProcessOneBufferedFrame ())
			{
			}
			if (!m_IsContinueRequested) break;
		}
		m_IsDispatching = false;
	}

	bool SAMSocket::ProcessOneBufferedFrame ()
	{
		// A queued reply or a command awaiting an asynchronous reply keeps
		// protocol order: later commands must not produce replies first.
		if (m_IsReplyQueued || m_IsCommandPending) return false;
		// Only a control socket (before or after SESSION CREATE) parses
		// commands. Acceptors and forwarders stay idle until a stream arrives.
		if (m_SocketType != SAMSocketType::eSAMSocketTypeUnknown
			&& m_SocketType != SAMSocketType::eSAMSocketTypeSession)
			return false;

		SAMFrame frame;
		auto status = GetSAMNextFrame ({ m_InputBuffer, m_InputSize }, GetSAMSendLimit (), frame);
		if (status == SAMFrameStatus::eSAMFrameNeedMore)
		{
			Receive ();
			return false;
		}
		if (status == SAMFrameStatus::eSAMFrameTooLong)
		{
			LogPrint (eLogError, "SAM: Command frame exceeds the control buffer ", m_InputSize);
			Terminate ("SAM: command frame too long");
			return false;
		}

		std::string_view line (m_InputBuffer, frame.lineLength);
		line.remove_suffix (1); // '\n'
		if (!line.empty () && line.back () == '\r') line.remove_suffix (1);

		if (!SAMCanDispatchCommands (m_IsHandshakeDone))
		{
			ProcessHandshakeLine (line);
			ConsumeInput (frame.lineLength);
			return false; // the HELLO REPLY must precede every command reply
		}

		ProcessControlLine (line, frame.lineLength);
		return !m_IsReplyQueued && !m_IsCommandPending && m_SocketType != SAMSocketType::eSAMSocketTypeTerminated
			&& m_SocketType != SAMSocketType::eSAMSocketTypeStream;
	}

	void SAMSocket::ProcessControlLine (std::string_view line, size_t lineLength)
	{
		while (!line.empty () && line.front () == ' ') line.remove_prefix (1);
		if (line.empty ())
		{
			// SAMv3Handler ignores empty lines.
			ConsumeInput (lineLength);
			return;
		}

		std::string_view argument;
		auto control = ParseSAMControlCommand (line, argument);
		if (control != SAMControlCommand::eSAMControlCommandNone)
		{
			auto space = line.find (' ');
			auto word = space == std::string_view::npos ? line : line.substr (0, space);
			auto reply = MakeSAMControlReply (control, word, argument);
			// Only the command word is logged: the argument of AUTH (and of a
			// PING echo) may carry credentials and is never written to the log.
			LogPrint (eLogDebug, "SAM: Control command ", word);
			ConsumeInput (lineLength);
			switch (control)
			{
			case SAMControlCommand::eSAMControlCommandPong: return; // answer to our PING
			case SAMControlCommand::eSAMControlCommandQuit:
			case SAMControlCommand::eSAMControlCommandStop:
			case SAMControlCommand::eSAMControlCommandExit:
				LogPrint (eLogInfo, "SAM: Client requested ", word);
				SendMessageReply (reply, true);
				return;
			case SAMControlCommand::eSAMControlCommandPing:
			case SAMControlCommand::eSAMControlCommandHelp: SendMessageReply (reply, false); return;
			case SAMControlCommand::eSAMControlCommandAuth:
				// No authenticator exists: refuse without dropping the control
				// connection, so the client can continue unauthenticated.
				LogPrint (eLogWarning, "SAM: AUTH is not supported");
				SendMessageReply (reply, false);
				return;
			case SAMControlCommand::eSAMControlCommandUnknown:
				LogPrint (eLogError, "SAM: Unknown command ", word);
				SendMessageReply (reply, false);
				return;
			default: return;
			}
		}

		// Two-word command: normalize the separator between the words so a
		// repeated space cannot turn "SESSION  CREATE" into a one-word command.
		std::string commandStorage;
		std::string_view command;
		std::string_view params;
		auto firstSpace = line.find (' ');
		if (firstSpace == std::string_view::npos)
			command = line;
		else
		{
			size_t secondStart = firstSpace + 1;
			while (secondStart < line.size () && line[secondStart] == ' ') ++secondStart;
			auto secondSpace = secondStart < line.size () ? line.find (' ', secondStart) : std::string_view::npos;
			commandStorage.reserve (firstSpace + 1
				+ (secondSpace == std::string_view::npos ? line.size () - secondStart : secondSpace - secondStart));
			commandStorage.append (line.substr (0, firstSpace));
			commandStorage.push_back (' ');
			if (secondStart < line.size ())
				commandStorage.append (line.substr (secondStart,
					secondSpace == std::string_view::npos ? std::string_view::npos : secondSpace - secondStart));
			command = commandStorage;
			if (secondSpace != std::string_view::npos) params = line.substr (secondSpace + 1);
		}

		if (SAMCommandEquals (command, SAM_SESSION_CREATE))
		{
			ProcessSessionCreate (params);
			ConsumeInput (lineLength);
		}
		else if (SAMCommandEquals (command, SAM_STREAM_CONNECT))
		{
			// Bytes behind the command line are stream payload, as in
			// SAMv3Handler; copy them into the pre-connect buffer.
			ProcessStreamConnect (params, m_InputBuffer + lineLength, m_InputSize - lineLength);
			ConsumeInput (m_InputSize);
		}
		else if (SAMCommandEquals (command, SAM_STREAM_ACCEPT))
		{
			ProcessStreamAccept (params);
			ConsumeInput (lineLength);
		}
		else if (SAMCommandEquals (command, SAM_STREAM_FORWARD))
		{
			ProcessStreamForward (params);
			ConsumeInput (lineLength);
		}
		else if (SAMCommandEquals (command, SAM_DEST_GENERATE))
		{
			ProcessDestGenerate (params);
			ConsumeInput (lineLength);
		}
		else if (SAMCommandEquals (command, SAM_NAMING_LOOKUP))
		{
			ProcessNamingLookup (params);
			ConsumeInput (lineLength);
		}
		else if (SAMCommandEquals (command, SAM_SESSION_ADD))
		{
			ProcessSessionAdd (params);
			ConsumeInput (lineLength);
		}
		else if (SAMCommandEquals (command, SAM_SESSION_REMOVE))
		{
			ProcessSessionRemove (params);
			ConsumeInput (lineLength);
		}
		else if (SAMCommandEquals (command, SAM_DATAGRAM_SEND) || SAMCommandEquals (command, SAM_RAW_SEND))
		{
			size_t available = m_InputSize - lineLength;
			size_t consumed = ProcessDatagramSend (params, m_InputBuffer + lineLength, available);
			ConsumeInput (lineLength + consumed);
		}
		else
		{
			LogPrint (eLogError, "SAM: Unexpected message ", line);
			Terminate ("SAM: unexpected message");
		}
	}

	bool SAMSocket::IsSession (std::string_view id) const
	{
		std::lock_guard<std::mutex> l (m_IDMutex);
		return id == m_ID;
	}

	void SAMSocket::SetID (std::string_view id)
	{
		std::lock_guard<std::mutex> l (m_IDMutex);
		m_ID.assign (id);
	}

	void SAMSocket::SendMessageReply (std::string_view msg, bool close)
	{
		// The reply body may carry secret operands: SESSION STATUS RESULT=OK
		// contains DESTINATION=<private keys> and DEST REPLY contains PRIV=.
		// Log only the size and the close decision, never the bytes.
		LogPrint (eLogDebug, "SAMSocket::SendMessageReply, close=", close ? "true" : "false", ", bytes=", msg.size ());

		// The reply (or the decision not to send one) settles any deferred
		// command: further commands may now be parsed once the write is done.
		m_IsCommandPending = false;
		if ((!m_IsSilent || m_SocketType == SAMSocketType::eSAMSocketTypeForward) && !msg.empty ())
		{
			auto reply = std::make_shared<uint8_t[]> (msg.size ());
			memcpy (reply.get (), msg.data (), msg.size ());
			m_IsReplyQueued = true;
			auto s = shared_from_this ();
			QueueI2PWrite (std::move (reply), msg.size (),
				[s, close] (const boost::system::error_code & ecode) { s->HandleMessageReplySent (ecode, 0, close); });
		}
		else
		{
			if (close)
				Terminate ("SAMSocket::SendMessageReply(close=true)");
			else
				ContinueProcessing ();
		}
	}

	void SAMSocket::HandleMessageReplySent (const boost::system::error_code & ecode, std::size_t, bool close)
	{
		m_IsReplyQueued = false;
		if (ecode)
		{
			LogPrint (eLogError, "SAM: Reply send error: ", ecode.message ());
			if (ecode != boost::asio::error::operation_aborted) Terminate ("SAM: reply send error");
		}
		else
		{
			if (close)
				Terminate ("SAMSocket::HandleMessageReplySent(close=true)");
			else
			{
				// STREAM CONNECT STATUS=OK must reach the client before any payload
				// from the I2P side, so start both directions only after the reply.
				if (m_SocketType == SAMSocketType::eSAMSocketTypeStream && m_Stream) I2PReceive ();
				ContinueProcessing ();
			}
		}
	}

	void SAMSocket::HandleMessage (const boost::system::error_code & ecode, std::size_t bytes_transferred)
	{
		m_IsReading = false;
		if (ecode)
		{
			LogPrint (eLogError, "SAM: Read error: ", ecode.message ());
			if (ecode != boost::asio::error::operation_aborted) Terminate ("SAM: read error");
		}
		else if (m_SocketType == SAMSocketType::eSAMSocketTypeStream)
		{
			// A control read may complete just after the socket switched to
			// stream mode; those bytes are payload, not commands. The read
			// started at m_InputSize, which cannot change while it is pending.
			if (!bytes_transferred) return Receive ();
			if (!m_Stream)
			{
				Terminate ("No Stream Remaining");
				return;
			}
			m_Stream->AsyncSend (reinterpret_cast<uint8_t *> (m_InputBuffer + m_ControlReadOffset), bytes_transferred,
				std::bind (&SAMSocket::HandleStreamSend, shared_from_this (), std::placeholders::_1));
		}
		else
		{
			m_InputSize += bytes_transferred;
			ProcessBufferedCommands ();
		}
	}

	static bool IsAcceptableSessionName (std::string_view str)
	{
		auto itr = str.begin ();
		while (itr != str.end ())
		{
			char ch = *itr;
			++itr;
			if (ch == '<' || ch == '>' || ch == '"' || ch == '\'' || ch == '/') return false;
		}
		return true;
	}

	// .i2p host names are case-insensitive, unlike base64 destinations. Java
	// lowercases the name before its ".b32.i2p" check and the naming service
	// matches case-insensitively.
	static bool IsI2PHostName (std::string_view address)
	{
		return address.size () > 4 && SAMCommandEquals (address.substr (address.size () - 4), ".I2P");
	}

	void SAMSocket::ProcessSessionCreate (std::string_view buf)
	{
		// SESSION STATUS may be delayed until the destination is ready; no
		// later command may be dispatched (or answered) before it.
		m_IsCommandPending = true;
		// DESTINATION carries the base64 private keys. The command line is
		// never logged; only the canonical style and whether an ID was supplied
		// are (MakeSAMSessionCreateLogLine).
		i2p::util::Mapping params;
		if (!GetCommandParams (buf, params)) return;
		std::string_view style = params[SAM_PARAM_STYLE];
		std::string_view id = params[SAM_PARAM_ID];
		std::string_view destination = params[SAM_PARAM_DESTINATION];
		LogPrint (eLogDebug, MakeSAMSessionCreateLogLine (style, !id.empty ()));

		if (id.empty () || !IsAcceptableSessionName (id))
		{
			// Java SAMv3Handler requires the ID property.
			SendMessageReply (SAM_SESSION_CREATE_INVALID_ID, true);
			return;
		}
		SetID (id);
		if (m_Owner.FindSession (id))
		{
			// session exists
			SendMessageReply (SAM_SESSION_CREATE_DUPLICATED_ID, true);
			return;
		}

		SAMSessionType type = SAMSessionType::eSAMSessionTypeUnknown;
		i2p::datagram::DatagramVersion datagramVersion = i2p::datagram::eDatagramV1;
		if (SAMCommandEquals (style, SAM_VALUE_STREAM))
			type = SAMSessionType::eSAMSessionTypeStream;
		else if (SAMCommandEquals (style, SAM_VALUE_DATAGRAM) || SAMCommandEquals (style, SAM_VALUE_DATAGRAM2)
			|| SAMCommandEquals (style, SAM_VALUE_DATAGRAM3))
		{
			type = SAMSessionType::eSAMSessionTypeDatagram;
			if (SAMCommandEquals (style, SAM_VALUE_DATAGRAM2))
				datagramVersion = i2p::datagram::eDatagramV2;
			else if (SAMCommandEquals (style, SAM_VALUE_DATAGRAM3))
				datagramVersion = i2p::datagram::eDatagramV3;
		}
		else if (SAMCommandEquals (style, SAM_VALUE_RAW))
			type = SAMSessionType::eSAMSessionTypeRaw;
		else if (SAMCommandEquals (style, SAM_VALUE_MASTER) || SAMCommandEquals (style, SAM_VALUE_PRIMARY))
		{
			if (m_Version < SAM_VERSION_33) // < SAM 3.3
			{
				SendSessionI2PError ("PRIMARY session is not supported");
				return;
			}
			type = SAMSessionType::eSAMSessionTypeMaster;
		}
		if (type == SAMSessionType::eSAMSessionTypeUnknown)
		{
			// unknown style
			SendSessionI2PError ("Unknown STYLE");
			return;
		}

		// SAM 3.2 session defaults. They apply to later SEND/CONNECT commands.
		uint16_t fromPort = 0, toPort = 0;
		uint8_t protocol = i2p::datagram::DATAGRAM_RAW_PROTOCOL; // RAW default, SAMv3 spec
		if (!ParseSAMPort (params, SAM_PARAM_FROM_PORT, fromPort))
		{
			SendSessionI2PError ("Invalid FROM_PORT");
			return;
		}
		if (!ParseSAMPort (params, SAM_PARAM_TO_PORT, toPort))
		{
			SendSessionI2PError ("Invalid TO_PORT");
			return;
		}
		// PROTOCOL is only meaningful for RAW sessions; other styles ignore it.
		if (type == SAMSessionType::eSAMSessionTypeRaw && !ParseSAMProtocol (params, SAM_PARAM_PROTOCOL, protocol))
		{
			SendSessionI2PError ("Invalid PROTOCOL");
			return;
		}
		// SAM 3.3 LISTEN_PROTOCOL selects the effective receive protocol. It is
		// interpreted for RAW, validated and ignored for DATAGRAM (the style
		// selects 17/19/20) and rejected for STREAM/PRIMARY. `PORT` below is
		// only the local UDP forward endpoint (see SAMv3Handler/SAM 3.x), so it
		// can never become the I2CP receive port.
		uint8_t listenProtocol = protocol;
		if (!ResolveSAMListenProtocol (params, type, protocol, listenProtocol))
		{
			SendSessionI2PError ("Invalid LISTEN_PROTOCOL");
			return;
		}
		const uint8_t receiveProtocol = SAMStyleListenProtocol (type, datagramVersion, listenProtocol);
		// I2CP receive port: LISTEN_PORT, default 0 (PORT_ANY). Java uses 0 for
		// a single session unless LISTEN_PORT is given.
		uint16_t listenPort = 0;
		if (!ResolveSAMSessionListenPort (params, listenPort))
		{
			SendSessionI2PError ("Invalid LISTEN_PORT");
			return;
		}
		// HEADER=true makes forwarded raw datagrams carry a PROTOCOL/FROM_PORT/
		// TO_PORT line, as in SAMv3RawSession (SAM 3.2+). A bare HEADER token
		// means true (SAMUtils.getBool).
		bool header = m_Version >= SAM_VERSION_32 && type == SAMSessionType::eSAMSessionTypeRaw
			&& SAMIsParamTrue (params, SAM_PARAM_HEADER);

		std::shared_ptr<boost::asio::ip::udp::endpoint> forward = nullptr;
		if ((type == SAMSessionType::eSAMSessionTypeDatagram || type == SAMSessionType::eSAMSessionTypeRaw)
			&& params.Contains (SAM_PARAM_PORT))
		{
			// udp forward selected. Like SAMv3RawSession.getSocketAddress(), HOST
			// defaults to the control connection's remote address. SAM v3
			// (`SESSION CREATE`) defines PORT as this local forwarding endpoint
			// and LISTEN_PORT as the I2CP receive port, so PORT never selects
			// the receiver registration below.
			uint16_t port = 0;
			if (!ParseSAMPort (params, SAM_PARAM_PORT, port))
			{
				SendSessionI2PError ("Invalid port");
				return;
			}
			boost::system::error_code e;
			boost::asio::ip::address addr;
			if (params.Contains (SAM_PARAM_HOST))
			{
				// TODO: support hostnames in udp forward
				addr = boost::asio::ip::make_address (params[SAM_PARAM_HOST], e);
				if (e)
				{
					// not an ip address
					SendSessionI2PError ("Invalid IP Address in HOST");
					return;
				}
			}
			else
			{
				auto ep = m_Socket.remote_endpoint (e);
				if (e)
				{
					SendSessionI2PError ("Socket error: cannot get remote endpoint");
					return;
				}
				addr = ep.address ();
			}
			forward = std::make_shared<boost::asio::ip::udp::endpoint> (addr, port);
		}

		//ensure we actually received a destination
		if (destination.empty ())
		{
			SendMessageReply (SAM_SESSION_STATUS_INVALID_KEY, true);
			return;
		}

		if (!SAMCommandEquals (destination, SAM_VALUE_TRANSIENT))
		{
			//ensure it's a base64 string
			i2p::data::PrivateKeys keys;
			if (!keys.FromBase64 (destination))
			{
				SendMessageReply (SAM_SESSION_STATUS_INVALID_KEY, true);
				return;
			}
		}
		else if (params.Contains (SAM_PARAM_SIGNATURE_TYPE))
		{
			// SAMv1Handler/SAMv3Handler reject an unsupported SIGNATURE_TYPE
			// instead of silently falling back to DSA-SHA1.
			i2p::data::SigningKeyType signatureType;
			if (!m_Owner.ResolveSignatureType (params[SAM_PARAM_SIGNATURE_TYPE], signatureType))
			{
				SendSessionI2PError (
					"SIGNATURE_TYPE " + std::string (params[SAM_PARAM_SIGNATURE_TYPE]) + " unsupported");
				return;
			}
		}

		// create destination
		auto session = m_Owner.CreateSession (
			id, type, SAMCommandEquals (destination, SAM_VALUE_TRANSIENT) ? "" : destination, params);
		if (session)
		{
			m_SocketType = SAMSocketType::eSAMSocketTypeSession;
			session->FromPort = fromPort;
			session->ToPort = toPort;
			session->Protocol = protocol;
			session->Header = header;
			session->ListenPort = listenPort;
			if (type == SAMSessionType::eSAMSessionTypeDatagram || type == SAMSessionType::eSAMSessionTypeRaw)
			{
				session->DatagramVersion = datagramVersion;
				session->UDPEndpoint = forward;
				auto dest = session->GetLocalDestination ()->CreateDatagramDestination (true, datagramVersion);
				if (type == SAMSessionType::eSAMSessionTypeDatagram)
				{
					// DATAGRAM/DATAGRAM2/DATAGRAM3 register identity receivers
					// under their own I2CP protocol (17/19) and DATAGRAM3 under
					// the sender-hash protocol 20, so the styles can share a
					// listen port.
					if (datagramVersion == i2p::datagram::eDatagramV3)
						TrackDatagramReceiver (m_ID,
							dest->SetHashReceiver (
								std::bind (&SAMSocket::HandleI2PDatagram3Receive, shared_from_this (), m_ID,
									std::placeholders::_1, std::placeholders::_2, std::placeholders::_3,
									std::placeholders::_4, std::placeholders::_5, std::placeholders::_6),
								listenPort, receiveProtocol));
					else
						TrackDatagramReceiver (m_ID,
							dest->SetReceiver (
								std::bind (&SAMSocket::HandleI2PDatagramReceive, shared_from_this (), m_ID,
									std::placeholders::_1, std::placeholders::_2, std::placeholders::_3,
									std::placeholders::_4, std::placeholders::_5, std::placeholders::_6),
								listenPort, receiveProtocol));
				}
				else // raw
					TrackDatagramReceiver (m_ID,
						dest->SetRawReceiver (std::bind (&SAMSocket::HandleI2PRawDatagramReceive, shared_from_this (),
												  m_ID, std::placeholders::_1, std::placeholders::_2,
												  std::placeholders::_3, std::placeholders::_4, std::placeholders::_5),
							listenPort, receiveProtocol));
			}

			if (session->GetLocalDestination ()->IsReady ())
				SendSessionCreateReplyOk ();
			else
			{
				m_Timer.expires_after (std::chrono::seconds (SAM_SESSION_READINESS_CHECK_INTERVAL));
				m_Timer.async_wait (std::bind (
					&SAMSocket::HandleSessionReadinessCheckTimer, shared_from_this (), std::placeholders::_1));
			}
		}
		else
			SendMessageReply (SAM_SESSION_CREATE_DUPLICATED_DEST, true);
	}

	void SAMSocket::HandleSessionReadinessCheckTimer (const boost::system::error_code & ecode)
	{
		if (ecode != boost::asio::error::operation_aborted)
		{
			if (m_Socket.is_open ())
			{
				auto session = m_Owner.FindSession (m_ID);
				if (session)
				{
					if (session->GetLocalDestination ()->IsReady ())
						SendSessionCreateReplyOk ();
					else
					{
						m_Timer.expires_after (std::chrono::seconds (SAM_SESSION_READINESS_CHECK_INTERVAL));
						m_Timer.async_wait (std::bind (
							&SAMSocket::HandleSessionReadinessCheckTimer, shared_from_this (), std::placeholders::_1));
					}
				}
			}
			else
				Terminate ("SAM: session socket closed");
		}
	}

	void SAMSocket::SendSessionCreateReplyOk ()
	{
		auto session = m_Owner.FindSession (m_ID);
		if (session)
		{
			std::string priv = session->GetLocalDestination ()->GetPrivateKeys ().ToBase64 ();
			const int formatted
				= std::snprintf (m_Buffer, SAM_SOCKET_BUFFER_SIZE, SAM_SESSION_CREATE_REPLY_OK, priv.c_str ());
			const size_t l2
				= formatted <= 0 ? 0 : std::min<size_t> (static_cast<size_t> (formatted), SAM_SOCKET_BUFFER_SIZE - 1);
			SendMessageReply ({ m_Buffer, l2 }, false);
		}
	}

	void SAMSocket::ProcessStreamConnect (std::string_view buf, const char * payload, size_t rem)
	{
		// The reply arrives after the connect completes, so no further command
		// may be parsed (or replied to) in the meantime.
		m_IsCommandPending = true;
		// CONNECT carries only a public destination and ports, so the parameter
		// list is safe to log (unlike SESSION CREATE and HELLO).
		LogPrint (eLogDebug, "SAM: Stream connect: ", buf);
		if (m_SocketType != SAMSocketType::eSAMSocketTypeUnknown)
		{
			SendStreamI2PError ("Socket already in use");
			return;
		}
		i2p::util::Mapping params;
		if (!GetCommandParams (buf, params)) return;
		std::string_view id = params[SAM_PARAM_ID];
		std::string_view destination = params[SAM_PARAM_DESTINATION];
		// SAMv3Handler/SAMv1Handler report a missing ID as I2P_ERROR, not as an
		// unknown session.
		if (id.empty ())
		{
			SendStreamI2PError ("ID not specified");
			return;
		}
		if (SAMIsParamTrue (params, SAM_PARAM_SILENT)) m_IsSilent = true;
		SetID (id);
		auto session = m_Owner.FindSession (id);
		m_ConnectFromPort = session ? session->FromPort : 0;
		m_ConnectToPort = session ? session->ToPort : 0;
		if (!ParseSAMPort (params, SAM_PARAM_FROM_PORT, m_ConnectFromPort))
		{
			SendStreamI2PError ("Invalid FROM_PORT");
			return;
		}
		if (!ParseSAMPort (params, SAM_PARAM_TO_PORT, m_ConnectToPort))
		{
			SendStreamI2PError ("Invalid TO_PORT");
			return;
		}
		if (session)
		{
			if (session->Type != SAMSessionType::eSAMSessionTypeStream)
			{
				SendStreamI2PError ("Specified ID is not a STREAM session");
				return;
			}
			if (rem > 0) // handle follow on data
			{
				// payload points past the command's line terminator (LF or CRLF).
				memmove (m_Buffer, payload, rem);
				m_BufferOffset = rem;
			}
			else
				m_BufferOffset = 0;

			std::shared_ptr<const Address> addr;
			if (IsI2PHostName (destination))
				addr = context.GetAddressBook ().GetAddress (std::string (destination));
			else
			{
				auto dest = std::make_shared<i2p::data::IdentityEx> ();
				size_t l = dest->FromBase64 (destination);
				if (l > 0)
				{
					context.GetAddressBook ().InsertFullAddress (dest);
					addr = std::make_shared<Address> (dest->GetIdentHash ());
				}
			}

			if (addr && addr->IsValid ())
			{
				if (addr->IsIdentHash ())
				{
					if (session->GetLocalDestination ()->GetIdentHash () != addr->identHash)
					{
						auto leaseSet = session->GetLocalDestination ()->FindLeaseSet (addr->identHash);
						if (leaseSet)
						{
							// SAMSocket state (stream pointer, timer, buffers) is owned
							// by the SAM service. Only the network operation inside
							// CreateNewOutgoingStream is handed to the destination.
							auto s = shared_from_this ();
							boost::asio::post (
								m_Owner.GetService (), [s, leaseSet, session] () { s->Connect (leaseSet, session); });
						}
						else
						{
							session->GetLocalDestination ()->RequestDestination (addr->identHash,
								std::bind (&SAMSocket::HandleConnectLeaseSetRequestComplete, shared_from_this (),
									std::placeholders::_1));
						}
					}
					else
						SendStreamCantReachPeer ("Can't connect to myself");
				}
				else // B33
					session->GetLocalDestination ()->RequestDestinationWithEncryptedLeaseSet (addr->blindedPublicKey,
						std::bind (&SAMSocket::HandleConnectLeaseSetRequestComplete, shared_from_this (),
							std::placeholders::_1));
			}
			else
				SendMessageReply (SAM_STREAM_STATUS_INVALID_KEY, true);
		}
		else
			SendMessageReply (SAM_STREAM_STATUS_INVALID_ID, true);
	}

	void SAMSocket::Connect (std::shared_ptr<const i2p::data::LeaseSet> remote, std::shared_ptr<SAMSession> session)
	{
		if (m_SocketType == SAMSocketType::eSAMSocketTypeTerminated) return;
		if (!session) session = m_Owner.FindSession (m_ID);
		if (session)
		{
			if (session->GetLocalDestination ()->SupportsEncryptionType (remote->GetEncryptionType ()))
			{
				m_SocketType = SAMSocketType::eSAMSocketTypeStream;
				// The control phase is over; stream payload has its own idle
				// handling.
				CancelControlDeadline ();
				auto localDest = session->GetLocalDestination ();
				uint16_t fromPort = m_ConnectFromPort ? m_ConnectFromPort : session->FromPort;
				auto streamingDest = localDest->GetStreamingDestination (fromPort);
				if (!streamingDest && fromPort) streamingDest = localDest->CreateStreamingDestination (fromPort);
				m_Stream = streamingDest ? streamingDest->CreateNewOutgoingStream (remote, m_ConnectToPort)
										 : localDest->CreateStream (remote, m_ConnectToPort);
				if (m_Stream)
				{
					m_Stream->Send ((uint8_t *)m_Buffer, m_BufferOffset); // connect and send
					m_BufferOffset = 0;
					m_ConnectAttempts = 0;
					m_Timer.expires_after (std::chrono::milliseconds (100));
					m_Timer.async_wait (
						std::bind (&SAMSocket::HandleStreamConnectTimer, shared_from_this (), std::placeholders::_1));
				}
				else
					SendMessageReply (SAM_STREAM_STATUS_INVALID_ID, true);
			}
			else
				SendStreamCantReachPeer ("Incompatible crypto");
		}
		else
			SendMessageReply (SAM_STREAM_STATUS_INVALID_ID, true);
	}

	void SAMSocket::HandleStreamConnectTimer (const boost::system::error_code & ecode)
	{
		if (ecode == boost::asio::error::operation_aborted || !m_Stream) return;
		if (m_Stream->IsEstablished ())
		{
			// SILENT=true has no reply completion handler, so start the
			// I2P->client direction here. For the non-silent case the reply
			// completion does it; I2PReceive is guarded against double reads.
			SendMessageReply (SAM_STREAM_STATUS_OK, false);
			if (m_IsSilent) I2PReceive ();
			return;
		}
		auto status = m_Stream->GetStatus ();
		if (status == i2p::stream::eStreamStatusReset || status == i2p::stream::eStreamStatusClosed
			|| status == i2p::stream::eStreamStatusTerminated)
		{
			SendStreamCantReachPeer ("Connection failed");
			return;
		}
		if (++m_ConnectAttempts * 100 >= SAM_STREAM_CONNECT_TIMEOUT)
		{
			m_Stream->AsyncClose (); // timer runs on the SAM service, not the stream's
			// An expired connect is TIMEOUT; CANT_REACH_PEER is reserved for a
			// reachable-but-failed attempt (no LeaseSet, incompatible crypto).
			SendMessageReply (SAM_STREAM_STATUS_TIMEOUT, true);
			return;
		}
		m_Timer.expires_after (std::chrono::milliseconds (100));
		m_Timer.async_wait (
			std::bind (&SAMSocket::HandleStreamConnectTimer, shared_from_this (), std::placeholders::_1));
	}

	void SAMSocket::HandleConnectLeaseSetRequestComplete (std::shared_ptr<i2p::data::LeaseSet> leaseSet)
	{
		auto s = shared_from_this ();
		boost::asio::post (m_Owner.GetService (),
			[s, leaseSet] ()
			{
				if (leaseSet)
					s->Connect (leaseSet);
				else
				{
					LogPrint (eLogError, "SAM: Destination to connect not found");
					s->SendStreamCantReachPeer ("LeaseSet not found");
				}
			});
	}

	void SAMSocket::ProcessStreamAccept (std::string_view buf)
	{
		LogPrint (eLogDebug, "SAM: Stream accept: ", buf);
		if (m_SocketType != SAMSocketType::eSAMSocketTypeUnknown)
		{
			SendStreamI2PError ("Socket already in use");
			return;
		}
		i2p::util::Mapping params;
		if (!GetCommandParams (buf, params)) return;
		std::string_view id = params[SAM_PARAM_ID];
		if (id.empty ())
		{
			SendStreamI2PError ("ID not specified");
			return;
		}
		if (SAMIsParamTrue (params, SAM_PARAM_SILENT)) m_IsSilent = true;
		SetID (id);
		auto session = m_Owner.FindSession (id);
		if (session)
		{
			if (session->Type != SAMSessionType::eSAMSessionTypeStream)
			{
				SendStreamI2PError ("Specified ID is not a STREAM session");
				return;
			}
			// A STREAM FORWARD acceptor owns this session's listen port: a
			// concurrent ACCEPT cannot be armed or queued behind it.
			const uint16_t listenPort = session->ListenPort;
			if (session->IsForwarding (listenPort))
			{
				LogPrint (eLogInfo, "SAM: Session ", m_ID, " is forwarding on port ", listenPort);
				SendMessageReply (SAM_STREAM_STATUS_ALREADY_ACCEPTING, false);
				return;
			}
			m_SocketType = SAMSocketType::eSAMSocketTypeAcceptor;
			// The socket now waits for an incoming stream, not for commands: the
			// inter-command deadline no longer applies.
			CancelControlDeadline ();
			m_ListenPort = listenPort;
			auto localDest = session->GetLocalDestination ();
			auto streamDest = localDest ? localDest->GetStreamingDestination (listenPort) : nullptr;
			if (!streamDest && localDest && listenPort)
			{
				streamDest = localDest->CreateStreamingDestination (listenPort);
				if (streamDest) streamDest->Start ();
			}
			if (!streamDest)
			{
				SendStreamI2PError ("Streaming destination is not available");
				return;
			}
			if (!streamDest->IsAcceptorSet ())
			{
				m_IsAccepting = true;
				SendMessageReply (SAM_STREAM_STATUS_OK, false);
				streamDest->AcceptOnce (
					std::bind (&SAMSocket::HandleI2PAccept, shared_from_this (), std::placeholders::_1));
			}
			else
			{
				auto ts = i2p::util::GetSecondsSinceEpoch ();
				if (session->EnqueueAccept (shared_from_this (), ts))
				{
					// already accepting, queue up
					SendMessageReply (SAM_STREAM_STATUS_OK, false);
				}
				else
				{
					LogPrint (
						eLogInfo, "SAM: Session ", m_ID, " accept queue is full ", session->GetAcceptQueueSize ());
					SendStreamI2PError ("Already accepting");
				}
			}
		}
		else
			SendMessageReply (SAM_STREAM_STATUS_INVALID_ID, true);
	}

	void SAMSocket::ProcessStreamForward (std::string_view buf)
	{
		LogPrint (eLogDebug, "SAM: Stream forward: ", buf);

		i2p::util::Mapping params;
		if (!GetCommandParams (buf, params)) return;
		auto id = params[SAM_PARAM_ID];
		if (id.empty ())
		{
			SendStreamI2PError ("ID not specified");
			return;
		}

		auto session = m_Owner.FindSession (id);
		if (!session)
		{
			SendMessageReply (SAM_STREAM_STATUS_INVALID_ID, true);
			return;
		}
		if (session->Type != SAMSessionType::eSAMSessionTypeStream)
		{
			SendStreamI2PError ("Specified ID is not a STREAM session");
			return;
		}
		// Per-port forwarding: only the port-scoped streaming destination of
		// the session is used, so a port-specific forwarder never accepts the
		// traffic of another port. An ACCEPT armed on the same port wins.
		const uint16_t listenPort = session->ListenPort;
		auto localDest = session->GetLocalDestination ();
		auto streamDest = localDest ? localDest->GetStreamingDestination (listenPort) : nullptr;
		if (!streamDest && localDest && listenPort)
		{
			streamDest = localDest->CreateStreamingDestination (listenPort);
			if (streamDest) streamDest->Start ();
		}
		if (!streamDest)
		{
			SendStreamI2PError ("Streaming destination is not available");
			return;
		}
		if (streamDest->IsAcceptorSet () || session->IsForwarding (listenPort))
		{
			SendMessageReply (SAM_STREAM_STATUS_ALREADY_ACCEPTING, false);
			return;
		}

		auto portStr = params[SAM_PARAM_PORT];
		if (portStr.empty ())
		{
			SendStreamI2PError ("PORT is missing");
			return;
		}

		if (!std::all_of (portStr.begin (), portStr.end (), [] (char c) { return c >= '0' && c <= '9'; }))
		{
			SendStreamI2PError ("Port must be numeric");
			return;
		}

		uint16_t port = 0;
		auto res = std::from_chars (portStr.data (), portStr.data () + portStr.size (), port);
		if (res.ec != std::errc ())
		{
			SendStreamI2PError ("Invalid port");
			return;
		}

		boost::asio::ip::tcp::endpoint ep;
		auto host = params[SAM_PARAM_HOST];

		if (!host.empty ())
		{
			boost::system::error_code ec;
			auto addr = boost::asio::ip::make_address (host, ec);
			if (ec)
			{
				SendStreamI2PError ("Invalid IP Address in HOST");
				return;
			}
			ep = boost::asio::ip::tcp::endpoint (addr, port);
		}
		else
		{
			boost::system::error_code ec;
			ep = m_Socket.remote_endpoint (ec);
			if (ec)
			{
				SendStreamI2PError ("Socket error: cannot get remote endpoint");
				return;
			}
			ep.port (port);
		}

		m_SocketType = SAMSocketType::eSAMSocketTypeForward;
		m_ListenPort = listenPort;
		// A forwarder waits for incoming streams instead of commands.
		CancelControlDeadline ();
		SetID (id);
		m_IsAccepting = true;

		if (SAMIsParamTrue (params, SAM_PARAM_SILENT)) m_IsSilent = true;

		session->AddForwardAcceptor (listenPort);
		streamDest->SetAcceptor (
			std::bind (&SAMSocket::HandleI2PForward, shared_from_this (), std::placeholders::_1, ep));

		SendMessageReply (SAM_STREAM_STATUS_OK, false);
	}

	size_t SAMSocket::ProcessDatagramSend (std::string_view line, const char * payload, size_t available)
	{
		// The SEND parameters name the public peer destination, ports and size;
		// the payload itself is never logged.
		LogPrint (eLogDebug, "SAM: Datagram send: ", line);
		i2p::util::Mapping params;
		if (!GetCommandParams (line, params)) return available;
		// A SEND has no ID parameter: it belongs to the session created on this
		// control connection. Java SAMv1Handler reports a missing/unknown
		// session as an error and closes the connection instead of silently
		// consuming the command and its payload.
		if (m_ID.empty ())
		{
			LogPrint (eLogError, "SAM: DATAGRAM SEND without a session, ID not specified");
			Terminate ("SAM: ID not specified");
			return available;
		}
		auto session = m_Owner.FindSession (m_ID);
		if (!session)
		{
			LogPrint (eLogError, "SAM: DATAGRAM SEND on an unknown session");
			Terminate ("SAM: unknown session");
			return available;
		}
		if (session->Type != SAMSessionType::eSAMSessionTypeRaw
			&& session->Type != SAMSessionType::eSAMSessionTypeDatagram)
		{
			LogPrint (eLogError, "SAM: SEND on a non-datagram session ", session->Name);
			Terminate ("SAM: SEND on a non-datagram session");
			return available;
		}
		bool isRaw = session->Type == SAMSessionType::eSAMSessionTypeRaw;
		size_t size = 0;
		// SAMRawSession/SAMDatagramSession limits
		if (!ParseSAMNumber (params[SAM_PARAM_SIZE], size) || !size
			|| size > (isRaw ? SAM_RAW_SEND_MAX_SIZE : SAM_DATAGRAM_SEND_MAX_SIZE))
		{
			LogPrint (eLogWarning, "SAM: Invalid SEND size ", params[SAM_PARAM_SIZE]);
			Terminate ("SAM: invalid SEND size");
			return available;
		}
		if (size > available)
		{
			LogPrint (eLogWarning, "SAM: Sent datagram size ", size, " exceeds buffered ", available);
			return 0; // try to receive more
		}
		uint16_t fromPort = session->FromPort, toPort = session->ToPort;
		uint8_t protocol = session->Protocol;
		if (!ParseSAMPort (params, SAM_PARAM_FROM_PORT, fromPort))
		{
			LogPrint (eLogWarning, "SAM: Invalid SEND FROM_PORT ", params[SAM_PARAM_FROM_PORT], ", using default");
			fromPort = session->FromPort;
		}
		if (!ParseSAMPort (params, SAM_PARAM_TO_PORT, toPort))
		{
			LogPrint (eLogWarning, "SAM: Invalid SEND TO_PORT ", params[SAM_PARAM_TO_PORT], ", using default");
			toPort = session->ToPort;
		}
		if (isRaw && !ParseSAMProtocol (params, SAM_PARAM_PROTOCOL, protocol))
		{
			LogPrint (eLogWarning, "SAM: Invalid SEND PROTOCOL ", params[SAM_PARAM_PROTOCOL], ", using default");
			protocol = session->Protocol;
		}
		if (params[SAM_PARAM_DESTINATION].empty ())
		{
			LogPrint (eLogError, "SAM: Missing SEND DESTINATION");
			Terminate ("SAM: SEND DESTINATION not specified");
			return available;
		}
		session->SendDatagram (
			params[SAM_PARAM_DESTINATION], (const uint8_t *)payload, size, fromPort, toPort, protocol);
		return size;
	}

	void SAMSocket::ProcessDestGenerate (std::string_view buf)
	{
		LogPrint (eLogDebug, "SAM: Dest generate");
		i2p::util::Mapping params;
		if (!GetCommandParams (buf, params)) return;
		// extract signature type
		i2p::data::SigningKeyType signatureType = i2p::data::SIGNING_KEY_TYPE_DSA_SHA1;
		i2p::data::CryptoKeyType cryptoType = i2p::data::CRYPTO_KEY_TYPE_ELGAMAL;
		auto signatureTypeStr = params[SAM_PARAM_SIGNATURE_TYPE];
		if (!signatureTypeStr.empty () && !m_Owner.ResolveSignatureType (signatureTypeStr, signatureType))
		{
			// SAMv1Handler.execDestMessage() returns I2P_ERROR for an
			// unsupported SIGNATURE_TYPE rather than generating a DSA key.
			SendMessageReply (SAM_DEST_REPLY_I2P_ERROR, true);
			return;
		}
		params.Get (SAM_PARAM_CRYPTO_TYPE, cryptoType);
		auto keys = i2p::data::PrivateKeys::CreateRandomKeys (signatureType, cryptoType, true);
		const int l = std::snprintf (m_Buffer, SAM_SOCKET_BUFFER_SIZE, SAM_DEST_REPLY,
			keys.GetPublic ()->ToBase64 ().c_str (), keys.ToBase64 ().c_str ());
		if (l <= 0 || static_cast<size_t> (l) >= SAM_SOCKET_BUFFER_SIZE)
		{
			LogPrint (eLogError, "SAM: Destination reply exceeds buffer");
			SendMessageReply (SAM_DEST_REPLY_I2P_ERROR, true);
			return;
		}
		SendMessageReply ({ m_Buffer, static_cast<size_t> (l) }, false);
	}

	void SAMSocket::ProcessNamingLookup (std::string_view buf)
	{
		// A lease set request can defer the NAMING REPLY; keep later commands
		// from producing replies first.
		m_IsCommandPending = true;
		LogPrint (eLogDebug, "SAM: Naming lookup: ", buf);
		i2p::util::Mapping params;
		if (!GetCommandParams (buf, params)) return;
		std::string name (params[SAM_PARAM_NAME]);
		std::shared_ptr<const i2p::data::IdentityEx> identity;
		std::shared_ptr<const Address> addr;
		auto session = m_Owner.FindSession (m_ID);
		auto dest = session == nullptr ? context.GetSharedLocalDestination () : session->GetLocalDestination ();
		if (name == "ME")
			SendNamingLookupReply (name, dest->GetIdentity ());
		else if ((identity = context.GetAddressBook ().GetFullAddress (name)) != nullptr)
			SendNamingLookupReply (name, identity);
		else if ((addr = context.GetAddressBook ().GetAddress (name)))
		{
			if (addr->IsIdentHash ())
			{
				auto leaseSet = dest->FindLeaseSet (addr->identHash);
				if (leaseSet)
					SendNamingLookupReply (name, leaseSet->GetIdentity ());
				else
					dest->RequestDestination (addr->identHash,
						std::bind (&SAMSocket::HandleNamingLookupLeaseSetRequestComplete, shared_from_this (),
							std::placeholders::_1, name));
			}
			else
				dest->RequestDestinationWithEncryptedLeaseSet (addr->blindedPublicKey,
					std::bind (&SAMSocket::HandleNamingLookupLeaseSetRequestComplete, shared_from_this (),
						std::placeholders::_1, name));
		}
		else
		{
			LogPrint (eLogError, "SAM: Naming failed, unknown address ", name);
			const int formatted
				= std::snprintf (m_Buffer, SAM_SOCKET_BUFFER_SIZE, SAM_NAMING_REPLY_KEY_NOT_FOUND, name.c_str ());
			const size_t len
				= formatted <= 0 ? 0 : std::min<size_t> (static_cast<size_t> (formatted), SAM_SOCKET_BUFFER_SIZE - 1);
			SendMessageReply ({ m_Buffer, len }, false);
		}
	}

	void SAMSocket::ProcessSessionAdd (std::string_view buf)
	{
		i2p::util::Mapping params;
		if (!GetCommandParams (buf, params)) return;
		std::string_view id = params[SAM_PARAM_ID];
		if (m_Version < SAM_VERSION_33) // < SAM 3.3
		{
			SendSubsessionError (id, "SESSION ADD is not supported");
			return;
		}
		auto session = m_Owner.FindSession (m_ID);
		if (session && session->Type == SAMSessionType::eSAMSessionTypeMaster)
		{
			LogPrint (eLogDebug, "SAM: Subsession add: ", buf);
			auto masterSession = std::static_pointer_cast<SAMMasterSession> (session);
			if (id.empty ())
			{
				SendSubsessionError (id, "ID not specified");
				return;
			}
			if (params.Contains (SAM_PARAM_DESTINATION))
			{
				SendSubsessionError (id, "SESSION ADD may not contain DESTINATION");
				return;
			}
			if (masterSession->subsessions.count (id) > 0)
			{
				SendSubsessionError (id, "Duplicate ID " + std::string (id));
				return;
			}
			std::string_view style = params[SAM_PARAM_STYLE];
			SAMSessionType type = SAMSessionType::eSAMSessionTypeUnknown;
			i2p::datagram::DatagramVersion datagramVersion = i2p::datagram::eDatagramV1;
			if (SAMCommandEquals (style, SAM_VALUE_STREAM))
				type = SAMSessionType::eSAMSessionTypeStream;
			else if (SAMCommandEquals (style, SAM_VALUE_DATAGRAM) || SAMCommandEquals (style, SAM_VALUE_DATAGRAM2)
				|| SAMCommandEquals (style, SAM_VALUE_DATAGRAM3))
			{
				type = SAMSessionType::eSAMSessionTypeDatagram;
				if (SAMCommandEquals (style, SAM_VALUE_DATAGRAM2))
					datagramVersion = i2p::datagram::eDatagramV2;
				else if (SAMCommandEquals (style, SAM_VALUE_DATAGRAM3))
					datagramVersion = i2p::datagram::eDatagramV3;
			}
			else if (SAMCommandEquals (style, SAM_VALUE_RAW))
				type = SAMSessionType::eSAMSessionTypeRaw;
			if (type == SAMSessionType::eSAMSessionTypeUnknown)
			{
				SendSubsessionError (id, "Unrecognized SESSION STYLE");
				return;
			}

			uint16_t fromPort = 0, toPort = 0;
			uint8_t protocol = i2p::datagram::DATAGRAM_RAW_PROTOCOL; // RAW default, SAMv3 spec
			if (!ParseSAMPort (params, SAM_PARAM_FROM_PORT, fromPort))
			{
				SendSubsessionError (id, "Invalid FROM_PORT");
				return;
			}
			if (!ParseSAMPort (params, SAM_PARAM_TO_PORT, toPort))
			{
				SendSubsessionError (id, "Invalid TO_PORT");
				return;
			}
			// PROTOCOL is only meaningful for RAW subsessions.
			if (type == SAMSessionType::eSAMSessionTypeRaw && !ParseSAMProtocol (params, SAM_PARAM_PROTOCOL, protocol))
			{
				SendSubsessionError (id, "Invalid PROTOCOL");
				return;
			}
			// SAM 3.3 LISTEN_PROTOCOL: interpreted for RAW (defaults to
			// PROTOCOL, 0 is the catch-all, streaming/datagram protocols are
			// rejected), validated and ignored for DATAGRAM and rejected for
			// STREAM. The effective protocol of the style then selects the
			// receiver key below.
			uint8_t listenProtocol = protocol;
			if (!ResolveSAMListenProtocol (params, type, protocol, listenProtocol))
			{
				SendSubsessionError (id, "Invalid LISTEN_PROTOCOL");
				return;
			}
			const uint8_t receiveProtocol = SAMStyleListenProtocol (type, datagramVersion, listenProtocol);
			bool header = m_Version >= SAM_VERSION_32 && type == SAMSessionType::eSAMSessionTypeRaw
				&& SAMIsParamTrue (params, SAM_PARAM_HEADER);

			std::shared_ptr<boost::asio::ip::udp::endpoint> forward = nullptr;
			if ((type == SAMSessionType::eSAMSessionTypeDatagram || type == SAMSessionType::eSAMSessionTypeRaw)
				&& params.Contains (SAM_PARAM_PORT))
			{
				// udp forward selected; HOST defaults to the control peer
				uint16_t port = 0;
				if (!ParseSAMPort (params, SAM_PARAM_PORT, port))
				{
					SendSubsessionError (id, "Invalid port");
					return;
				}
				boost::system::error_code e;
				boost::asio::ip::address addr;
				if (params.Contains (SAM_PARAM_HOST))
				{
					addr = boost::asio::ip::make_address (params[SAM_PARAM_HOST], e);
					if (e)
					{
						SendSubsessionError (id, "Invalid IP Address in HOST");
						return;
					}
				}
				else
				{
					auto ep = m_Socket.remote_endpoint (e);
					if (e)
					{
						SendSubsessionError (id, "Socket error: cannot get remote endpoint");
						return;
					}
					addr = ep.address ();
				}
				forward = std::make_shared<boost::asio::ip::udp::endpoint> (addr, port);
			}

			// traffic from I2P is routed by port, LISTEN_PORT defaults to FROM_PORT
			uint16_t listenPort = fromPort;
			if (!ParseSAMPort (params, SAM_PARAM_LISTEN_PORT, listenPort))
			{
				SendSubsessionError (id, "Invalid LISTEN_PORT");
				return;
			}
			// Java PrimarySession requires PORT for RAW/DATAGRAM subsessions and
			// rejects a duplicate (protocol, port) pair.
			if ((type == SAMSessionType::eSAMSessionTypeDatagram || type == SAMSessionType::eSAMSessionTypeRaw)
				&& !params.Contains (SAM_PARAM_PORT))
			{
				SendSubsessionError (id, "DATAGRAM/RAW subsession must specify PORT");
				return;
			}
			// Java PrimarySession keys a subsession on (STYLE, effective
			// receive protocol, LISTEN_PORT): two RAW subsessions may share a
			// port on different protocols, and DATAGRAM/DATAGRAM2/DATAGRAM3 may
			// share a port because their styles select 17/19/20.
			const auto candidateKey = MakeSAMSubsessionKey (type, receiveProtocol, listenPort);
			for (const auto & existingId : masterSession->subsessions)
			{
				auto existing = m_Owner.FindSession (existingId);
				auto sub = std::dynamic_pointer_cast<SAMSubSession> (existing);
				if (sub && MakeSAMSubsessionKey (sub->Type, sub->inProtocol, sub->inPort) == candidateKey)
				{
					SendSubsessionError (id, "Duplicate protocol and port");
					return;
				}
			}

			// STREAM subsessions route incoming connections by LISTEN_PORT;
			// FROM_PORT remains the outgoing source port.
			auto subsession = std::make_shared<SAMSubSession> (masterSession, id, type, listenPort, receiveProtocol);
			if (m_Owner.AddSession (subsession))
			{
				subsession->FromPort = fromPort;
				subsession->ToPort = toPort;
				subsession->Protocol = protocol;
				subsession->Header = header;
				if (type == SAMSessionType::eSAMSessionTypeDatagram || type == SAMSessionType::eSAMSessionTypeRaw)
				{
					subsession->DatagramVersion = datagramVersion;
					subsession->UDPEndpoint = forward;
					auto dest
						= masterSession->GetLocalDestination ()->CreateDatagramDestination (true, datagramVersion);
					if (type == SAMSessionType::eSAMSessionTypeDatagram)
					{
						// Identity receivers use the style protocol (17/19), the
						// DATAGRAM3 sender-hash receiver protocol 20.
						if (datagramVersion == i2p::datagram::eDatagramV3)
							TrackDatagramReceiver (id,
								dest->SetHashReceiver (
									std::bind (&SAMSocket::HandleI2PDatagram3Receive, shared_from_this (),
										std::string (id), std::placeholders::_1, std::placeholders::_2,
										std::placeholders::_3, std::placeholders::_4, std::placeholders::_5,
										std::placeholders::_6),
									listenPort, receiveProtocol));
						else
							TrackDatagramReceiver (id,
								dest->SetReceiver (std::bind (&SAMSocket::HandleI2PDatagramReceive, shared_from_this (),
													   std::string (id), std::placeholders::_1, std::placeholders::_2,
													   std::placeholders::_3, std::placeholders::_4,
													   std::placeholders::_5, std::placeholders::_6),
									listenPort, receiveProtocol));
					}
					else
						TrackDatagramReceiver (id,
							dest->SetRawReceiver (
								std::bind (&SAMSocket::HandleI2PRawDatagramReceive, shared_from_this (),
									std::string (id), std::placeholders::_1, std::placeholders::_2,
									std::placeholders::_3, std::placeholders::_4, std::placeholders::_5),
								listenPort, receiveProtocol));
				}
				masterSession->subsessions.insert (std::string (id));
				SendSubsessionStatusOk (id);
			}
			else
				SendSubsessionError (id, "Duplicate ID " + std::string (id));
		}
		else
			SendSubsessionError (id, "Wrong session type");
	}

	void SAMSocket::ProcessSessionRemove (std::string_view buf)
	{
		i2p::util::Mapping params;
		if (!GetCommandParams (buf, params)) return;
		std::string id (params[SAM_PARAM_ID]);
		if (m_Version < SAM_VERSION_33) // < SAM 3.3
		{
			SendSubsessionError (id, "SESSION REMOVE is not supported");
			return;
		}
		auto session = m_Owner.FindSession (m_ID);
		if (session && session->Type == SAMSessionType::eSAMSessionTypeMaster)
		{
			LogPrint (eLogDebug, "SAM: Subsession remove: ", buf);
			auto masterSession = std::static_pointer_cast<SAMMasterSession> (session);
			if (id.empty () || !masterSession->subsessions.erase (id))
			{
				SendSubsessionError (id, "ID " + id + " not found");
				return;
			}
			ResetDatagramReceivers (id);
			m_Owner.CloseSession (id);
			SendSubsessionStatusOk (id);
		}
		else
			SendSubsessionError (id, "Not a PRIMARY session");
	}

	void SAMSocket::ProcessPing (std::string_view text)
	{
		LogPrint (eLogDebug, "SAM: Ping ", text);
		SendMessageReply (MakeSAMControlReply (SAMControlCommand::eSAMControlCommandPing, SAM_PING, text), false);
	}

	void SAMSocket::SendReplyWithMessage (const char * reply, const std::string & msg, bool close)
	{
		const int formatted = std::snprintf (m_Buffer, SAM_SOCKET_BUFFER_SIZE, reply, msg.c_str ());
		const size_t len
			= formatted <= 0 ? 0 : std::min<size_t> (static_cast<size_t> (formatted), SAM_SOCKET_BUFFER_SIZE - 1);
		SendMessageReply ({ m_Buffer, len }, close);
	}

	void SAMSocket::SendSubsessionStatusOk (std::string_view id)
	{
		const std::string value (id);
		const int formatted
			= std::snprintf (m_Buffer, SAM_SOCKET_BUFFER_SIZE, SAM_SUBSESSION_STATUS_OK, value.c_str ());
		const size_t len
			= formatted <= 0 ? 0 : std::min<size_t> (static_cast<size_t> (formatted), SAM_SOCKET_BUFFER_SIZE - 1);
		SendMessageReply ({ m_Buffer, len }, false);
	}

	void SAMSocket::SendSubsessionError (std::string_view id, std::string_view msg)
	{
		const std::string idValue (id), message (msg);
		const int formatted = std::snprintf (
			m_Buffer, SAM_SOCKET_BUFFER_SIZE, SAM_SUBSESSION_STATUS_ERROR, idValue.c_str (), message.c_str ());
		const size_t len
			= formatted <= 0 ? 0 : std::min<size_t> (static_cast<size_t> (formatted), SAM_SOCKET_BUFFER_SIZE - 1);
		SendMessageReply ({ m_Buffer, len }, false);
	}

	void SAMSocket::SendSessionI2PError (const std::string & msg)
	{
		LogPrint (eLogError, "SAM: Session I2P error: ", msg);
		SendReplyWithMessage (SAM_SESSION_STATUS_I2P_ERROR, msg, true);
	}

	void SAMSocket::SendStreamI2PError (const std::string & msg)
	{
		LogPrint (eLogError, "SAM: Stream I2P error: ", msg);
		SendReplyWithMessage (SAM_STREAM_STATUS_I2P_ERROR, msg, true);
	}

	void SAMSocket::SendStreamCantReachPeer (const std::string & msg)
	{
		SendReplyWithMessage (SAM_STREAM_STATUS_CANT_REACH_PEER, msg, true);
	}

	void SAMSocket::HandleNamingLookupLeaseSetRequestComplete (
		std::shared_ptr<i2p::data::LeaseSet> leaseSet, std::string name)
	{
		auto s = shared_from_this ();
		boost::asio::post (m_Owner.GetService (),
			[s, leaseSet, name = std::move (name)] ()
			{
				if (leaseSet)
				{
					context.GetAddressBook ().InsertFullAddress (leaseSet->GetIdentity ());
					s->SendNamingLookupReply (name, leaseSet->GetIdentity ());
				}
				else
				{
					LogPrint (eLogError, "SAM: Naming lookup failed. LeaseSet for ", name, " not found");
					const int formatted = std::snprintf (
						s->m_Buffer, SAM_SOCKET_BUFFER_SIZE, SAM_NAMING_REPLY_KEY_NOT_FOUND, name.c_str ());
					const size_t len = formatted <= 0
						? 0
						: std::min<size_t> (static_cast<size_t> (formatted), SAM_SOCKET_BUFFER_SIZE - 1);
					s->SendMessageReply ({ s->m_Buffer, len }, false);
				}
			});
	}

	void SAMSocket::SendNamingLookupReply (
		const std::string & name, std::shared_ptr<const i2p::data::IdentityEx> identity)
	{
		auto base64 = identity->ToBase64 ();
		const int formatted
			= std::snprintf (m_Buffer, SAM_SOCKET_BUFFER_SIZE, SAM_NAMING_REPLY, name.c_str (), base64.c_str ());
		const size_t l
			= formatted <= 0 ? 0 : std::min<size_t> (static_cast<size_t> (formatted), SAM_SOCKET_BUFFER_SIZE - 1);
		SendMessageReply ({ m_Buffer, l }, false);
	}

	i2p::util::Mapping SAMSocket::ExtractParams (std::string_view buf)
	{
		// Shared grammar: ParseSAMParams() also reports duplicate keys and
		// treats a bare token as true, which SAMSocket::GetCommandParams()
		// consumes. This static form stays available to the UDP first-line
		// options and to tests.
		return ParseSAMParams (buf).values;
	}

	void SAMSocket::Receive ()
	{
		if (m_SocketType == SAMSocketType::eSAMSocketTypeStream)
		{
			// A stream socket carries payload, not commands: the control
			// deadlines only apply to the control phases.
			CancelControlDeadline ();
			if (m_IsReceiving) return;
			size_t bufSize = SAM_SOCKET_BUFFER_SIZE;
			size_t unsentSize = m_Stream ? m_Stream->GetSendBufferSize () : 0;
			if (unsentSize)
			{
				if (unsentSize >= SAM_STREAM_MAX_SEND_BUFFER_SIZE) return; // buffer is full
				if (unsentSize > SAM_STREAM_MAX_SEND_BUFFER_SIZE - SAM_SOCKET_BUFFER_SIZE)
					bufSize = SAM_STREAM_MAX_SEND_BUFFER_SIZE - unsentSize;
			}
			m_IsReceiving = true;
			m_Socket.async_read_some (boost::asio::buffer (m_Buffer, bufSize),
				std::bind (
					&SAMSocket::HandleReceived, shared_from_this (), std::placeholders::_1, std::placeholders::_2));
		}
		else
		{
			if (m_IsReading || m_IsReplyQueued || m_IsCommandPending) return;
			if (m_SocketType != SAMSocketType::eSAMSocketTypeUnknown
				&& m_SocketType != SAMSocketType::eSAMSocketTypeSession)
				return; // acceptors/forwarders only start reading when a stream arrives
			size_t capacity = SAM_SOCKET_BUFFER_SIZE - m_InputSize;
			if (!capacity)
			{
				LogPrint (eLogError, "SAM: Control input buffer is full");
				Terminate ("SAM: control input overflow");
				return;
			}
			// Before HELLO the client has 60 s (SAMHandlerFactory.HELLO_TIMEOUT).
			// After HELLO Java calls setSoTimeout(0): a session control socket may
			// stay idle for hours, so only a *partially* received command line is
			// bounded, and an idle connection is left to the peer.
			if (!m_IsHandshakeDone)
				ArmControlDeadline (SAM_CONTROL_HELLO_TIMEOUT, "HELLO timeout");
			else if (m_InputSize)
				ArmControlDeadline (SAM_CONTROL_PARTIAL_TIMEOUT, "incomplete command");
			else
				CancelControlDeadline ();
			m_IsReading = true;
			m_ControlReadOffset = m_InputSize;
			m_Socket.async_read_some (boost::asio::buffer (m_InputBuffer + m_ControlReadOffset, capacity),
				std::bind (
					&SAMSocket::HandleMessage, shared_from_this (), std::placeholders::_1, std::placeholders::_2));
		}
	}

	void SAMSocket::HandleReceived (const boost::system::error_code & ecode, std::size_t bytes_transferred)
	{
		m_IsReceiving = false;
		if (ecode)
		{
			LogPrint (eLogError, "SAM: Read error: ", ecode.message ());
			if (ecode != boost::asio::error::operation_aborted) Terminate ("read error");
		}
		else
		{
			if (m_Stream)
			{
				m_Stream->AsyncSend ((uint8_t *)m_Buffer, bytes_transferred,
					std::bind (&SAMSocket::HandleStreamSend, shared_from_this (), std::placeholders::_1));
				Receive ();
			}
			else
				Terminate ("No Stream Remaining");
		}
	}

	void SAMSocket::I2PReceive ()
	{
		if (m_Stream && !m_IsI2PReceiving)
		{
			if (m_Stream->GetStatus () == i2p::stream::eStreamStatusNew
				|| m_Stream->GetStatus () == i2p::stream::eStreamStatusOpen) // regular
			{
				m_IsI2PReceiving = true;
				m_Stream->AsyncReceive (boost::asio::buffer (m_StreamBuffer, SAM_STREAM_BUFFER_SIZE),
					std::bind (&SAMSocket::HandleI2PReceive, shared_from_this (), std::placeholders::_1,
						std::placeholders::_2),
					SAM_SOCKET_CONNECTION_MAX_IDLE);
			}
			else // closed by peer
			{
				auto buff = std::make_shared<uint8_t[]> (SAM_STREAM_BUFFER_SIZE);
				// get remaining data
				auto len = m_Stream->ReadSome (buff.get (), SAM_STREAM_BUFFER_SIZE);
				if (len > 0) // still some data
				{
					WriteI2PDataImmediate (std::move (buff), len);
				}
				else // no more data
					Terminate ("no more data");
			}
		}
	}

	void SAMSocket::WriteI2PDataImmediate (std::shared_ptr<uint8_t[]> buff, size_t sz)
	{
		QueueI2PData (std::move (buff), sz);
	}

	void SAMSocket::WriteI2PData (size_t sz)
	{
		QueueI2PData (m_StreamBuffer, sz);
	}

	void SAMSocket::QueueI2PData (const uint8_t * data, size_t len)
	{
		if (!data || !len) return;
		auto buff = std::make_shared<uint8_t[]> (len);
		memcpy (buff.get (), data, len);
		QueueI2PData (std::move (buff), len);
	}

	void SAMSocket::QueueI2PData (std::shared_ptr<uint8_t[]> buff, size_t len)
	{
		QueueI2PWrite (std::move (buff), len, std::function<void (const boost::system::error_code &)> ());
	}

	void SAMSocket::QueueI2PWrite (
		std::shared_ptr<uint8_t[]> buff, size_t len, std::function<void (const boost::system::error_code &)> && onSent)
	{
		if (!buff || !len) return;
		if (m_SocketType == SAMSocketType::eSAMSocketTypeTerminated) return;
		m_WriteQueue.push_back (OutputBuffer { std::move (buff), len, std::move (onSent) });
		if (!m_IsWriting) StartNextI2PWrite ();
	}

	void SAMSocket::StartNextI2PWrite ()
	{
		if (m_IsWriting || m_WriteQueue.empty ()) return;
		if (m_SocketType == SAMSocketType::eSAMSocketTypeTerminated)
		{
			m_WriteQueue.clear ();
			return;
		}
		OutputBuffer buffer = m_WriteQueue.front ();
		m_IsWriting = true;
		boost::asio::async_write (m_Socket, boost::asio::buffer (buffer.data.get (), buffer.length),
			boost::asio::transfer_all (),
			[s = shared_from_this (), buffer] (const boost::system::error_code & ec, std::size_t bytes_transferred)
			{
				(void)buffer; // keep the bytes alive until the completion handler runs
				s->HandleI2PWriteCompleted (ec, bytes_transferred);
			});
	}

	void SAMSocket::HandleI2PWriteCompleted (const boost::system::error_code & ec, std::size_t)
	{
		std::function<void (const boost::system::error_code &)> onSent;
		if (!m_WriteQueue.empty ())
		{
			onSent = std::move (m_WriteQueue.front ().onSent);
			m_WriteQueue.pop_front ();
		}
		m_IsWriting = false;
		if (ec)
		{
			m_WriteQueue.clear ();
			if (onSent)
				onSent (ec);
			else if (ec != boost::asio::error::operation_aborted)
			{
				LogPrint (eLogError, "SAM: Socket write error: ", ec.message ());
				Terminate ("socket write error");
			}
			return;
		}
		if (onSent) onSent (ec);
		StartNextI2PWrite ();
	}

	void SAMSocket::HandleI2PReceive (const boost::system::error_code & ecode, std::size_t bytes_transferred)
	{
		auto s = shared_from_this ();
		boost::asio::post (m_Owner.GetService (),
			[s, ecode, bytes_transferred] () { s->HandleI2PReceiveOnService (ecode, bytes_transferred); });
	}

	void SAMSocket::HandleI2PReceiveOnService (const boost::system::error_code & ecode, std::size_t bytes_transferred)
	{
		m_IsI2PReceiving = false;
		if (ecode)
		{
			LogPrint (eLogError, "SAM: Stream read error: ", ecode.message ());
			if (ecode != boost::asio::error::operation_aborted)
			{
				if (bytes_transferred > 0)
				{
					WriteI2PData (bytes_transferred);
				}
				else
				{
					auto s = shared_from_this ();
					boost::asio::post (m_Owner.GetService (), [s] { s->Terminate ("stream read error"); });
				}
			}
			else
			{
				auto s = shared_from_this ();
				boost::asio::post (m_Owner.GetService (), [s] { s->Terminate ("stream read error (op aborted)"); });
			}
		}
		else
		{
			if (m_SocketType != SAMSocketType::eSAMSocketTypeTerminated)
			{
				if (bytes_transferred > 0) WriteI2PData (bytes_transferred);
				// The write queue owns the copy, so the next read can start
				// immediately and output order is preserved.
				I2PReceive ();
			}
		}
	}

	void SAMSocket::HandleI2PAccept (std::shared_ptr<i2p::stream::Stream> stream)
	{
		auto s = shared_from_this ();
		boost::asio::post (m_Owner.GetService (),
			[s, stream = std::move (stream)] () mutable { s->HandleI2PAcceptOnService (std::move (stream)); });
	}

	bool SAMSocket::ArmAcceptorOnService (
		const std::shared_ptr<SAMSession> & session, const std::shared_ptr<SAMSocket> & socket)
	{
		if (!session || !socket) return false;
		auto localDest = session->GetLocalDestination ();
		if (!localDest) return false;
		const uint16_t listenPort = socket->m_ListenPort;
		auto streamDest = localDest->GetStreamingDestination (listenPort);
		if (!streamDest && listenPort)
		{
			streamDest = localDest->CreateStreamingDestination (listenPort);
			if (streamDest) streamDest->Start ();
		}
		if (!streamDest) return false;
		socket->m_IsAccepting = true;
		streamDest->AcceptOnce (std::bind (&SAMSocket::HandleI2PAccept, socket, std::placeholders::_1));
		return true;
	}

	void SAMSocket::RearmQueuedAccept (uint16_t listenPort)
	{
		auto session = m_Owner.FindSession (m_ID);
		if (!session) return;
		auto next = session->TakeNextAcceptOnPort (listenPort);
		if (!next) return;
		LogPrint (eLogDebug, "SAM: Re-arming a queued ACCEPT on port ", listenPort);
		// Arm on the SAM service: Terminate can run inside a destination
		// callback, where the streaming destination must not be touched.
		boost::asio::post (m_Owner.GetService (),
			[session, next] ()
			{
				if (next->GetSocketType () != SAMSocketType::eSAMSocketTypeAcceptor
					|| !ArmAcceptorOnService (session, next))
					next->Terminate (nullptr);
			});
	}

	void SAMSocket::HandleI2PAcceptOnService (std::shared_ptr<i2p::stream::Stream> stream)
	{
		if (stream && (m_SocketType == SAMSocketType::eSAMSocketTypeTerminated || !m_Socket.is_open ()))
		{
			LogPrint (eLogWarning, "SAM: Incoming stream arrived for a closed acceptor");
			stream->Close ();
			stream.reset ();
		}
		if (stream)
		{
			LogPrint (eLogDebug, "SAM: Incoming I2P connection for session ", m_ID);
			m_SocketType = SAMSocketType::eSAMSocketTypeStream;
			CancelControlDeadline ();
			m_IsAccepting = false;
			m_Stream = stream;
			context.GetAddressBook ().InsertFullAddress (stream->GetRemoteIdentity ());
			auto session = m_Owner.FindSession (m_ID);
			if (session)
			{
				// Re-arm the next pending ACCEPT of *this* listen port. Queued
				// acceptors of other ports stay queued; a subsession's ACCEPT
				// must never be armed on another port's destination.
				auto socket = session->TakeNextAcceptOnPort (m_ListenPort);
				if (socket)
				{
					if (socket->GetSocketType () != SAMSocketType::eSAMSocketTypeAcceptor
						|| !ArmAcceptorOnService (session, socket))
						socket->Terminate (nullptr);
				}
			}
			if (!m_IsSilent)
			{
				if (m_SocketType != SAMSocketType::eSAMSocketTypeTerminated)
				{
					// get remote peer address
					std::string ident = stream->GetRemoteIdentity ()->ToBase64 ();
					if (m_Version >= SAM_VERSION_32)
					{
						// SAMv3Handler.notifyStreamIncomingConnection(): peer port then local port
						ident.append (" FROM_PORT=");
						ident.append (std::to_string (stream->GetRemotePort ()));
						ident.append (" TO_PORT=");
						ident.append (std::to_string (stream->GetLocalPort ()));
					}
					ident.push_back ('\n');
					// send remote peer address back to client like received from stream
					QueueI2PData (reinterpret_cast<const uint8_t *> (ident.data ()), ident.size ());
				}
			}
			// The socket was an acceptor and did not read commands; start the
			// client->I2P direction now that it carries stream payload.
			if (m_SocketType != SAMSocketType::eSAMSocketTypeTerminated) Receive ();
			I2PReceive ();
		}
		else
			LogPrint (eLogWarning, "SAM: I2P acceptor has been reset");
	}

	void SAMSocket::HandleI2PForward (std::shared_ptr<i2p::stream::Stream> stream, boost::asio::ip::tcp::endpoint ep)
	{
		auto s = shared_from_this ();
		boost::asio::post (m_Owner.GetService (),
			[s, stream = std::move (stream), ep] () mutable { s->HandleI2PForwardOnService (std::move (stream), ep); });
	}

	void SAMSocket::HandleI2PForwardOnService (
		std::shared_ptr<i2p::stream::Stream> stream, boost::asio::ip::tcp::endpoint ep)
	{
		if (stream)
		{
			LogPrint (eLogDebug, "SAM: Incoming forward I2P connection for session ", m_ID);
			auto newSocket = std::make_shared<SAMSocket> (m_Owner);
			newSocket->SetSocketType (SAMSocketType::eSAMSocketTypeStream);
			auto s = shared_from_this ();
			newSocket->GetSocket ().async_connect (ep,
				[s, newSocket, stream] (const boost::system::error_code & ecode)
				{
					if (!ecode)
					{
						s->m_Owner.AddSocket (newSocket);
						newSocket->Receive ();
						newSocket->m_Stream = stream;
						newSocket->SetID (s->m_ID);
						if (!s->m_IsSilent)
						{
							// get remote peer address
							auto dest = stream->GetRemoteIdentity ()->ToBase64 ();
							if (s->m_Version >= SAM_VERSION_32)
							{
								dest.append (" FROM_PORT=");
								dest.append (std::to_string (stream->GetRemotePort ()));
								dest.append (" TO_PORT=");
								dest.append (std::to_string (stream->GetLocalPort ()));
							}
							if (dest.length () + 1 > SAM_STREAM_BUFFER_SIZE)
							{
								LogPrint (eLogError, "SAM: Remote identity is too long ", dest.length ());
								newSocket->TerminateClose ();
							}
							else
							{
								dest.push_back ('\n');
								// send identity like it has been received from stream
								newSocket->QueueI2PData (
									reinterpret_cast<const uint8_t *> (dest.data ()), dest.size ());
								newSocket->I2PReceive ();
							}
						}
						else
							newSocket->I2PReceive ();
					}
					else
						stream->AsyncClose ();
				});
		}
		else
			LogPrint (eLogWarning, "SAM: I2P forward acceptor has been reset");
	}

	void SAMSocket::HandleI2PDatagramReceive (std::string sessionID, const i2p::data::IdentityEx & from,
		uint16_t fromPort, uint16_t toPort, const uint8_t * buf, size_t len, const i2p::util::Mapping *)
	{
		// This callback may run on the destination's service. Copy the payload
		// before it is reused by the next datagram, then handle it on SAM.
		std::vector<uint8_t> payload;
		if (buf && len) payload.assign (buf, buf + len);
		auto s = shared_from_this ();
		boost::asio::post (m_Owner.GetService (),
			[s, sessionID = std::move (sessionID), from, fromPort, toPort, payload = std::move (payload)] () mutable
			{
				s->HandleI2PDatagramReceiveOnService (
					std::move (sessionID), from, fromPort, toPort, std::move (payload));
			});
	}

	void SAMSocket::HandleI2PDatagramReceiveOnService (std::string sessionID, const i2p::data::IdentityEx & from,
		uint16_t fromPort, uint16_t toPort, std::vector<uint8_t> payload)
	{
		size_t len = payload.size ();
		const uint8_t * buf = payload.empty () ? nullptr : payload.data ();
		LogPrint (eLogDebug, "SAM: Datagram received ", len);
		auto session = m_Owner.FindSession (sessionID);
		if (session)
		{
			auto base64 = (session->DatagramVersion == i2p::datagram::eDatagramV3) ? from.GetIdentHash ().ToBase64 ()
																				   : from.ToBase64 ();
			auto ep = session->UDPEndpoint;
			if (ep)
			{
				// udp forward enabled: destination [FROM_PORT TO_PORT] LF payload
				// (SAMv3DatagramSession.receiveDatagramBytes; SAM 3.2+ ports).
				std::string header = base64;
				if (m_Version >= SAM_VERSION_32)
				{
					header += " FROM_PORT=";
					header += std::to_string (fromPort);
					header += " TO_PORT=";
					header += std::to_string (toPort);
				}
				header.push_back ('\n');
				m_Owner.SendTo ({ { (const uint8_t *)header.data (), header.size () }, { buf, len } }, *ep);
			}
			else
			{
				int l;
				if (m_Version >= SAM_VERSION_32)
				{
					l = std::snprintf ((char *)m_StreamBuffer, SAM_STREAM_BUFFER_SIZE, SAM_DATAGRAM_RECEIVED_PORTS,
						base64.c_str (), (long unsigned int)len, (unsigned)fromPort, (unsigned)toPort);
				}
				else
				{
					l = std::snprintf ((char *)m_StreamBuffer, SAM_STREAM_BUFFER_SIZE, SAM_DATAGRAM_RECEIVED,
						base64.c_str (), (long unsigned int)len);
				}
				if (l < 0 || (size_t)l >= SAM_STREAM_BUFFER_SIZE)
					LogPrint (eLogWarning, "SAM: Datagram header exceeds buffer, dropped");
				else if (len <= SAM_STREAM_BUFFER_SIZE - (size_t)l)
				{
					if (len) memcpy (m_StreamBuffer + l, buf, len);
					WriteI2PData (len + l);
				}
				else
					LogPrint (eLogWarning, "SAM: Received datagram size ", len, " exceeds buffer");
			}
		}
	}

	void SAMSocket::HandleI2PDatagram3Receive (std::string sessionID, const i2p::data::IdentHash & from,
		uint16_t fromPort, uint16_t toPort, const uint8_t * buf, size_t len, const i2p::util::Mapping *)
	{
		std::vector<uint8_t> payload;
		if (buf && len) payload.assign (buf, buf + len);
		auto s = shared_from_this ();
		boost::asio::post (m_Owner.GetService (),
			[s, sessionID = std::move (sessionID), from, fromPort, toPort, payload = std::move (payload)] () mutable
			{
				s->HandleI2PDatagram3ReceiveOnService (
					std::move (sessionID), from, fromPort, toPort, std::move (payload));
			});
	}

	void SAMSocket::HandleI2PDatagram3ReceiveOnService (std::string sessionID, const i2p::data::IdentHash & from,
		uint16_t fromPort, uint16_t toPort, std::vector<uint8_t> payload)
	{
		size_t len = payload.size ();
		const uint8_t * buf = payload.empty () ? nullptr : payload.data ();
		// Datagram3 has only the sender hash (SAMv3DatagramSession.receiveDatagramBytes(Hash,...)).
		LogPrint (eLogDebug, "SAM: Datagram3 received ", len);
		auto session = m_Owner.FindSession (sessionID);
		if (session)
		{
			auto base64 = from.ToBase64 ();
			auto ep = session->UDPEndpoint;
			if (ep)
			{
				// send to remote endpoint: sender hash [FROM_PORT TO_PORT] LF payload
				std::string header = base64;
				if (m_Version >= SAM_VERSION_32)
				{
					header += " FROM_PORT=";
					header += std::to_string (fromPort);
					header += " TO_PORT=";
					header += std::to_string (toPort);
				}
				header.push_back ('\n');
				m_Owner.SendTo ({ { (const uint8_t *)header.data (), header.size () }, { buf, len } }, *ep);
			}
			else
			{
				int l;
				if (m_Version >= SAM_VERSION_32)
				{
					l = std::snprintf ((char *)m_StreamBuffer, SAM_STREAM_BUFFER_SIZE, SAM_DATAGRAM_RECEIVED_PORTS,
						base64.c_str (), (long unsigned int)len, (unsigned)fromPort, (unsigned)toPort);
				}
				else
				{
					l = std::snprintf ((char *)m_StreamBuffer, SAM_STREAM_BUFFER_SIZE, SAM_DATAGRAM_RECEIVED,
						base64.c_str (), (long unsigned int)len);
				}
				if (l < 0 || (size_t)l >= SAM_STREAM_BUFFER_SIZE)
					LogPrint (eLogWarning, "SAM: Datagram3 header exceeds buffer, dropped");
				else if (len <= SAM_STREAM_BUFFER_SIZE - (size_t)l)
				{
					if (len) memcpy (m_StreamBuffer + l, buf, len);
					WriteI2PData (len + l);
				}
				else
					LogPrint (eLogWarning, "SAM: Received datagram3 size ", len, " exceeds buffer");
			}
		}
	}

	void SAMSocket::HandleI2PRawDatagramReceive (
		std::string sessionID, uint8_t protocol, uint16_t fromPort, uint16_t toPort, const uint8_t * buf, size_t len)
	{
		std::vector<uint8_t> payload;
		if (buf && len) payload.assign (buf, buf + len);
		auto s = shared_from_this ();
		boost::asio::post (m_Owner.GetService (),
			[s, sessionID = std::move (sessionID), protocol, fromPort, toPort, payload = std::move (payload)] () mutable
			{
				s->HandleI2PRawDatagramReceiveOnService (
					std::move (sessionID), protocol, fromPort, toPort, std::move (payload));
			});
	}

	void SAMSocket::HandleI2PRawDatagramReceiveOnService (
		std::string sessionID, uint8_t protocol, uint16_t fromPort, uint16_t toPort, std::vector<uint8_t> payload)
	{
		size_t len = payload.size ();
		const uint8_t * buf = payload.empty () ? nullptr : payload.data ();
		LogPrint (eLogDebug, "SAM: Raw datagram received ", len);
		auto session = m_Owner.FindSession (sessionID);
		if (session)
		{
			auto ep = session->UDPEndpoint;
			if (ep)
			{
				// HEADER=true (SAM 3.2+, RAW only): prepend the I2CP metadata
				// line, matching SAMv3RawSession.receiveRawBytes.
				if (session->Header)
				{
					char header[64];
					int l = std::snprintf (header, sizeof (header), "PROTOCOL=%u FROM_PORT=%u TO_PORT=%u\n",
						(unsigned)protocol, (unsigned)fromPort, (unsigned)toPort);
					if (l > 0 && (size_t)l < sizeof (header))
						m_Owner.SendTo ({ { (const uint8_t *)header, (size_t)l }, { buf, len } }, *ep);
					else
						LogPrint (eLogWarning, "SAM: Raw datagram header too long");
				}
				else
					m_Owner.SendTo ({ { buf, len } }, *ep);
			}
			else
			{
				int l;
				if (m_Version >= SAM_VERSION_32)
				{
					l = std::snprintf ((char *)m_StreamBuffer, SAM_STREAM_BUFFER_SIZE, SAM_RAW_RECEIVED_PORTS,
						(long unsigned int)len, (unsigned)protocol, (unsigned)fromPort, (unsigned)toPort);
				}
				else
				{
					l = std::snprintf (
						(char *)m_StreamBuffer, SAM_STREAM_BUFFER_SIZE, SAM_RAW_RECEIVED, (long unsigned int)len);
				}
				if (l < 0 || (size_t)l >= SAM_STREAM_BUFFER_SIZE)
					LogPrint (eLogWarning, "SAM: Raw received header exceeds buffer, dropped");
				else if (len <= SAM_STREAM_BUFFER_SIZE - (size_t)l)
				{
					if (len) memcpy (m_StreamBuffer + l, buf, len);
					WriteI2PData (len + l);
				}
				else
					LogPrint (eLogWarning, "SAM: Received raw datagram size ", len, " exceeds buffer");
			}
		}
	}

	void SAMSocket::HandleStreamSend (const boost::system::error_code & ec)
	{
		boost::asio::post (m_Owner.GetService (),
			std::bind (!ec ? &SAMSocket::Receive : &SAMSocket::TerminateClose, shared_from_this ()));
	}

	SAMSession::SAMSession (SAMBridge & parent, std::string_view id, SAMSessionType type)
		: m_Bridge (parent)
		, Name (id)
		, Type (type)
		, DatagramVersion (i2p::datagram::eDatagramV1)
	{
	}

	bool SAMSession::EnqueueAccept (const std::shared_ptr<SAMSocket> & socket, uint64_t now)
	{
		if (!socket) return false;
		std::lock_guard<std::mutex> l (acceptQueueMutex);
		if (acceptQueue.size () >= SAM_SESSION_MAX_ACCEPT_QUEUE_SIZE) return false;
		acceptQueue.emplace_back (socket, now);
		return true;
	}

	void SAMSession::RemoveAccept (const std::shared_ptr<SAMSocket> & socket)
	{
		if (!socket) return;
		std::lock_guard<std::mutex> l (acceptQueueMutex);
		acceptQueue.remove_if ([&socket] (const auto & item) { return item.first == socket; });
	}

	std::shared_ptr<SAMSocket> SAMSession::TakeNextAccept (
		uint64_t /*now*/, std::list<std::shared_ptr<SAMSocket>> & /*expired*/)
	{
		std::lock_guard<std::mutex> l (acceptQueueMutex);
		if (acceptQueue.empty ()) return nullptr;
		auto socket = acceptQueue.front ().first;
		acceptQueue.pop_front ();
		return socket;
	}

	std::size_t SAMSession::GetAcceptQueueSize () const
	{
		std::lock_guard<std::mutex> l (acceptQueueMutex);
		return acceptQueue.size ();
	}

	std::shared_ptr<SAMSocket> SAMSession::TakeNextAcceptOnPort (uint16_t port)
	{
		std::lock_guard<std::mutex> l (acceptQueueMutex);
		for (auto it = acceptQueue.begin (); it != acceptQueue.end (); ++it)
		{
			if (it->first && it->first->GetAcceptListenPort () == port)
			{
				auto socket = it->first;
				acceptQueue.erase (it);
				return socket;
			}
		}
		return nullptr;
	}

	bool SAMSession::IsForwarding (uint16_t port) const
	{
		std::lock_guard<std::mutex> l (acceptQueueMutex);
		return forwardAcceptors.find (port) != forwardAcceptors.end ();
	}

	bool SAMSession::IsForwardingAnyPort () const
	{
		std::lock_guard<std::mutex> l (acceptQueueMutex);
		return !forwardAcceptors.empty ();
	}

	void SAMSession::AddForwardAcceptor (uint16_t port)
	{
		std::lock_guard<std::mutex> l (acceptQueueMutex);
		++forwardAcceptors[port];
	}

	void SAMSession::RemoveForwardAcceptor (uint16_t port)
	{
		std::lock_guard<std::mutex> l (acceptQueueMutex);
		auto it = forwardAcceptors.find (port);
		if (it == forwardAcceptors.end ()) return;
		if (--it->second == 0) forwardAcceptors.erase (it);
	}

	void SAMSession::CloseStreams ()
	{
		for (const auto & itr : m_Bridge.ListSockets (Name))
		{
			itr->Terminate (nullptr);
		}
	}

	void SAMSession::SendDatagram (std::string_view destination, const uint8_t * payload, size_t len, uint16_t fromPort,
		uint16_t toPort, uint8_t protocol)
	{
		auto localDest = GetLocalDestination ();
		auto datagramDest = localDest ? localDest->GetDatagramDestination () : nullptr;
		if (!datagramDest)
		{
			LogPrint (eLogError, "SAM: Missing datagram destination for session ", Name);
			return;
		}
		// Resolve like a stream connect: a full base64 destination, or a .i2p host
		// that is either a plain b32 (ident hash) or a blinded b33 (encrypted leaseset).
		std::shared_ptr<const Address> addr;
		if (IsI2PHostName (destination))
			addr = context.GetAddressBook ().GetAddress (std::string (destination));
		else
		{
			auto dest = std::make_shared<i2p::data::IdentityEx> ();
			if (dest->FromBase64 (std::string (destination)) > 0)
			{
				context.GetAddressBook ().InsertFullAddress (dest);
				addr = std::make_shared<Address> (dest->GetIdentHash ());
			}
		}
		if (!addr || !addr->IsValid ())
		{
			LogPrint (eLogError, "SAM: Datagram invalid destination ", destination);
			return;
		}
		auto sendTo = [datagramDest, type = Type, version = DatagramVersion, fromPort, toPort, protocol] (
						  const uint8_t * p, size_t l, const i2p::data::IdentHash & ident)
		{
			if (type == SAMSessionType::eSAMSessionTypeDatagram)
			{
				// The session's SAM style selects the I2CP datagram framing:
				// DATAGRAM3 must never emit Datagram1/Datagram2 payloads.
				datagramDest->SendDatagramTo (p, l, ident, fromPort, toPort, version);
			}
			else if (type == SAMSessionType::eSAMSessionTypeRaw)
				datagramDest->SendRawDatagramTo (p, l, ident, fromPort, toPort, protocol);
			else
				LogPrint (eLogError, "SAM: Unexpected session type ", (int)type, " for datagram send");
		};
		if (addr->IsIdentHash ())
			sendTo (payload, len, addr->identHash); // plain b32 / full dest: send straight away
		else
		{
			// b33: the leaseset lookup is async and the receive buffer is reused, so copy
			// the payload, then send to the resolved ident hash once the leaseset arrives.
			auto data = std::make_shared<std::vector<uint8_t>> (payload, payload + len);
			localDest->RequestDestinationWithEncryptedLeaseSet (addr->blindedPublicKey,
				[sendTo, data] (std::shared_ptr<i2p::data::LeaseSet> ls)
				{
					if (ls) sendTo (data->data (), data->size (), ls->GetIdentHash ());
				});
		}
	}

	SAMSingleSession::SAMSingleSession (
		SAMBridge & parent, std::string_view name, SAMSessionType type, std::shared_ptr<ClientDestination> dest)
		: SAMSession (parent, name, type)
		, localDestination (dest)
	{
	}

	SAMSingleSession::~SAMSingleSession ()
	{
		// Do not remove/stop a destination still referenced by another SAM
		// session or tunnel. CleanupDestinations() removes an orphan after the
		// last owner releases it.
		if (localDestination && localDestination->GetRefCounter () <= 0)
			i2p::client::context.DeleteLocalDestination (localDestination);
	}

	void SAMSingleSession::StopLocalDestination ()
	{
		if (!localDestination) return;
		// Destination is ref-counted and may be shared by sessions whose SESSION
		// CREATEs resolve to the same identity hash; only its last user may stop it.
		if (localDestination->Release () > 0) return;
		// stop accepting new streams
		localDestination->StopAcceptingStreams ();
		// terminate existing streams
		auto s = localDestination->GetStreamingDestination (); // TODO: take care about datagrams
		if (s) s->Stop ();
	}

	void SAMMasterSession::Close ()
	{
		SAMSingleSession::Close ();
		for (const auto & it : subsessions) m_Bridge.CloseSession (it);
		subsessions.clear ();
	}

	SAMSubSession::SAMSubSession (std::shared_ptr<SAMMasterSession> master, std::string_view name, SAMSessionType type,
		uint16_t port, uint8_t listenProtocol)
		: SAMSession (master->m_Bridge, name, type)
		, masterSession (master)
		, inPort (port)
		, inProtocol (listenProtocol)
	{
		// Everything that selects receivers or the port-scoped streaming
		// destination reads the base ListenPort.
		ListenPort = port;
		if (Type == SAMSessionType::eSAMSessionTypeStream && port)
		{
			// additional streaming destination, use default if port is 0
			auto d = masterSession->GetLocalDestination ()->CreateStreamingDestination (inPort);
			if (d) d->Start ();
		}
	}

	std::shared_ptr<ClientDestination> SAMSubSession::GetLocalDestination ()
	{
		return masterSession ? masterSession->GetLocalDestination () : nullptr;
	}

	void SAMSubSession::StopLocalDestination ()
	{
		auto dest = GetLocalDestination ();
		if (dest && Type == SAMSessionType::eSAMSessionTypeStream)
		{
			auto d = dest->RemoveStreamingDestination (inPort);
			if (d) d->Stop ();
		}
		// Datagram receivers are protocol/port registrations tracked by the
		// owning SAMSocket; SESSION REMOVE and socket teardown reset them before
		// this subsession (and the callback's owner) is released.
	}

	SAMBridge::SAMBridge (const std::string & address, uint16_t portTCP, uint16_t portUDP, bool singleThread)
		: RunnableService ("SAM")
		, m_IsSingleThread (singleThread)
		, m_Acceptor (
			  GetIOService (), boost::asio::ip::tcp::endpoint (boost::asio::ip::make_address (address), portTCP))
		, m_DatagramEndpoint (
			  boost::asio::ip::make_address (address), (!portUDP) ? (portTCP ? (uint16_t)(portTCP - 1) : 0) : portUDP)
		, m_DatagramSocket (GetIOService (), m_DatagramEndpoint)
		, m_SignatureTypes {
			{ "DSA_SHA1", i2p::data::SIGNING_KEY_TYPE_DSA_SHA1 },
			{ "ECDSA_SHA256_P256", i2p::data::SIGNING_KEY_TYPE_ECDSA_SHA256_P256 },
			{ "ECDSA_SHA384_P384", i2p::data::SIGNING_KEY_TYPE_ECDSA_SHA384_P384 },
			{ "ECDSA_SHA512_P521", i2p::data::SIGNING_KEY_TYPE_ECDSA_SHA512_P521 },
			{ "EdDSA_SHA512_Ed25519", i2p::data::SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519 },
			{ "EdDSA_SHA512_Ed25519ph", i2p::data::SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519ph },
			{ "GOST_GOSTR3411256_GOSTR3410CRYPTOPROA",
				i2p::data::SIGNING_KEY_TYPE_GOSTR3410_CRYPTO_PRO_A_GOSTR3411_256 },
			{ "GOST_GOSTR3411512_GOSTR3410TC26A512", i2p::data::SIGNING_KEY_TYPE_GOSTR3410_TC26_A_512_GOSTR3411_512 },
			{ "RedDSA_SHA512_Ed25519", i2p::data::SIGNING_KEY_TYPE_REDDSA_SHA512_ED25519 },
		}
	{
	}

	SAMBridge::~SAMBridge ()
	{
		if (IsRunning ()) Stop ();
	}

	void SAMBridge::Start ()
	{
		boost::asio::post (GetIOService (),
			[this] ()
			{
				std::lock_guard<std::mutex> l (m_ServiceThreadMutex);
				m_ServiceThreadID = std::this_thread::get_id ();
			});
		Accept ();
		ReceiveDatagram ();
		StartIOService ();
	}

	void SAMBridge::CloseSessions ()
	{
		decltype (m_Sessions) sessions;
		{
			std::unique_lock<std::mutex> l (m_SessionsMutex);
			m_Sessions.swap (sessions);
		}
		for (auto & it : sessions)
		{
			// Close() alone never releases the Acquire() taken by CreateSession;
			// the map was already cleared, so the normal Terminate path cannot
			// find the session and would leave the reference unbalanced.
			if (it.second) it.second->StopLocalDestination ();
			if (it.second) it.second->Close ();
		}
	}

	void SAMBridge::Stop ()
	{
		try
		{
			m_Acceptor.cancel ();
		}
		catch (const std::exception & ex)
		{
			LogPrint (eLogError, "SAM: Runtime exception: ", ex.what ());
		}

		// Sockets and sessions are owned by the SAM service. Close them there,
		// then join from this thread. A bridge that never started has no owner
		// thread to marshal to.
		if (IsRunning () && !IsInServiceThread ())
		{
			auto done = std::make_shared<std::promise<void>> ();
			auto future = done->get_future ();
			boost::asio::post (GetIOService (),
				[this, done] ()
				{
					CloseSessions ();
					done->set_value ();
				});
			if (future.wait_for (std::chrono::seconds (STOP_DESTINATIONS_TIMEOUT)) != std::future_status::ready)
			{
				LogPrint (eLogError, "SAM: Service didn't close sessions in time");
				CloseSessions ();
			}
		}
		else
			CloseSessions ();

		StopIOService ();
	}

	void SAMBridge::Accept ()
	{
		auto newSocket = std::make_shared<SAMSocket> (*this);
		m_Acceptor.async_accept (
			newSocket->GetSocket (), std::bind (&SAMBridge::HandleAccept, this, std::placeholders::_1, newSocket));
	}

	void SAMBridge::AddSocket (std::shared_ptr<SAMSocket> socket)
	{
		std::unique_lock<std::mutex> lock (m_OpenSocketsMutex);
		m_OpenSockets.push_back (socket);
	}

	void SAMBridge::RemoveSocket (const std::shared_ptr<SAMSocket> & socket)
	{
		std::unique_lock<std::mutex> lock (m_OpenSocketsMutex);
		m_OpenSockets.remove_if ([socket] (const std::shared_ptr<SAMSocket> & item) -> bool { return item == socket; });
	}

	bool SAMBridge::TryAddControlConnection ()
	{
		auto current = m_ControlConnections->load ();
		while (current < SAM_MAX_CONTROL_CONNECTIONS)
		{
			if (m_ControlConnections->compare_exchange_weak (current, current + 1)) return true;
		}
		return false;
	}

	void SAMBridge::HandleAccept (const boost::system::error_code & ecode, std::shared_ptr<SAMSocket> socket)
	{
		if (!ecode)
		{
			boost::system::error_code ec;
			auto ep = socket->GetSocket ().remote_endpoint (ec);
			if (!ec)
			{
				// Bounded admission: a control connection that would exceed
				// SAM_MAX_CONTROL_CONNECTIONS is closed immediately and never
				// counted, so Terminate has nothing to release.
				if (!TryAddControlConnection ())
				{
					LogPrint (eLogWarning, "SAM: Too many control connections (", SAM_MAX_CONTROL_CONNECTIONS,
						"), rejecting ", ep);
					boost::system::error_code ignored;
					socket->GetSocket ().shutdown (boost::asio::ip::tcp::socket::shutdown_both, ignored);
					socket->GetSocket ().close (ignored);
				}
				else
				{
					socket->MarkControlConnectionCounted (m_ControlConnections);
					LogPrint (eLogDebug, "SAM: New connection from ", ep);
					AddSocket (socket);
					socket->ReceiveHandshake ();
				}
			}
			else
				LogPrint (eLogError, "SAM: Incoming connection error: ", ec.message ());
		}
		else
			LogPrint (eLogError, "SAM: Accept error: ", ecode.message ());

		if (ecode != boost::asio::error::operation_aborted) Accept ();
	}

	std::shared_ptr<SAMSession> SAMBridge::CreateSession (
		std::string_view id, SAMSessionType type, std::string_view destination, const i2p::util::Mapping & params)
	{
		std::shared_ptr<ClientDestination> localDestination = nullptr;
		if (destination != "")
		{
			i2p::data::PrivateKeys keys;
			if (!keys.FromBase64 (destination)) return nullptr;
			localDestination = i2p::client::context.FindLocalDestination (keys.GetPublic ()->GetIdentHash ());
			if (localDestination && localDestination->GetRefCounter () <= 0)
			{
				LogPrint (eLogError, "SAM: Orphaned destination ", keys.GetPublic ()->GetIdentHash ().ToBase32 (),
					".  Try later");
				return nullptr;
			}
			if (!localDestination)
				localDestination = m_IsSingleThread
					? i2p::client::context.CreateNewLocalDestination (GetIOService (), keys, true, &params)
					: i2p::client::context.CreateNewLocalDestination (keys, true, &params);
		}
		else // transient
		{
			// extract signature type
			i2p::data::SigningKeyType signatureType = i2p::data::SIGNING_KEY_TYPE_DSA_SHA1;
			i2p::data::CryptoKeyType cryptoType = i2p::data::CRYPTO_KEY_TYPE_ELGAMAL;
			if (!params.IsEmpty ())
			{
				auto signatureTypeStr = params[SAM_PARAM_SIGNATURE_TYPE];
				if (!signatureTypeStr.empty ())
				{
					if (!ResolveSignatureType (signatureTypeStr, signatureType))
						LogPrint (eLogWarning, "SAM: ", SAM_PARAM_SIGNATURE_TYPE, " is invalid ", signatureTypeStr);
				}
				params.Get (SAM_PARAM_CRYPTO_TYPE, cryptoType);
			}
			localDestination = m_IsSingleThread
				? i2p::client::context.CreateNewLocalDestination (
					  GetIOService (), true, signatureType, cryptoType, &params)
				: i2p::client::context.CreateNewLocalDestination (true, signatureType, cryptoType, &params);
		}
		if (localDestination)
		{
			localDestination->Acquire ();
			auto session = (type == SAMSessionType::eSAMSessionTypeMaster)
				? std::make_shared<SAMMasterSession> (*this, id, localDestination)
				: std::make_shared<SAMSingleSession> (*this, id, type, localDestination);
			std::unique_lock<std::mutex> l (m_SessionsMutex);
			auto ret = m_Sessions.emplace (id, session);
			if (!ret.second) LogPrint (eLogWarning, "SAM: Session ", id, " already exists");
			return ret.first->second;
		}
		return nullptr;
	}

	bool SAMBridge::AddSession (std::shared_ptr<SAMSession> session)
	{
		if (!session) return false;
		std::unique_lock<std::mutex> l (m_SessionsMutex);
		auto ret = m_Sessions.emplace (session->Name, session);
		return ret.second;
	}

	void SAMBridge::CloseSession (std::string_view id)
	{
		std::shared_ptr<SAMSession> session;
		{
			std::unique_lock<std::mutex> l (m_SessionsMutex);
			auto it = m_Sessions.find (id);
			if (it != m_Sessions.end ())
			{
				session = it->second;
				m_Sessions.erase (it);
			}
		}
		if (session)
		{
			session->StopLocalDestination ();
			session->Close ();
			if (m_IsSingleThread) ScheduleSessionCleanupTimer (session); // let all session's streams close
		}
	}

	void SAMBridge::ScheduleSessionCleanupTimer (std::shared_ptr<SAMSession> session)
	{
		auto timer = std::make_shared<boost::asio::steady_timer> (GetService ());
		timer->expires_after (
			std::chrono::seconds (SAM_SESSION_CLEANUP_CHECK_INTERVAL)); // postpone destination cleanup
		timer->async_wait (
			std::bind (&SAMBridge::HandleSessionCleanupTimer, this, std::placeholders::_1, session, timer));
	}

	void SAMBridge::HandleSessionCleanupTimer (const boost::system::error_code & ecode,
		std::shared_ptr<SAMSession> session, std::shared_ptr<boost::asio::steady_timer>)
	{
		if (ecode != boost::asio::error::operation_aborted && session)
		{
			auto dest = session->GetLocalDestination ();
			if (dest)
			{
				auto streamingDest = dest->GetStreamingDestination ();
				if (streamingDest)
				{
					auto numStreams = streamingDest->GetNumStreams ();
					if (numStreams > 0)
					{
						LogPrint (eLogInfo, "SAM: Session ", session->Name, " still has ", numStreams, " streams");
						ScheduleSessionCleanupTimer (session);
					}
					else
						LogPrint (eLogDebug, "SAM: Session ", session->Name, " terminated");
				}
			}
		}
		// session's destructor is called here unless rescheduled
	}

	std::shared_ptr<SAMSession> SAMBridge::FindSession (std::string_view id) const
	{
		std::unique_lock<std::mutex> l (m_SessionsMutex);
		auto it = m_Sessions.find (id);
		if (it != m_Sessions.end ()) return it->second;
		return nullptr;
	}

	std::list<std::shared_ptr<SAMSocket>> SAMBridge::ListSockets (std::string_view id) const
	{
		std::list<std::shared_ptr<SAMSocket>> list;
		{
			std::unique_lock<std::mutex> l (m_OpenSocketsMutex);
			for (const auto & itr : m_OpenSockets)
				if (itr->IsSession (id)) list.push_back (itr);
		}
		return list;
	}

	void SAMBridge::SendTo (
		const std::vector<boost::asio::const_buffer> & bufs, const boost::asio::ip::udp::endpoint & ep)
	{
		size_t total = 0;
		for (const auto & buf : bufs) total += buf.size ();
		if (!total) return;
		// Copy now: the caller may reuse m_StreamBuffer or a local header as
		// soon as this returns. The service thread owns the send queue and
		// keeps exactly one asynchronous send in flight.
		auto data = std::make_shared<std::vector<uint8_t>> ();
		data->reserve (total);
		for (const auto & buf : bufs)
		{
			if (!buf.size ()) continue;
			const uint8_t * begin = static_cast<const uint8_t *> (buf.data ());
			data->insert (data->end (), begin, begin + buf.size ());
		}
		boost::asio::post (
			GetIOService (), [this, data = std::move (data), ep] () { QueueDatagramSend (std::move (data), ep); });
	}

	void SAMBridge::QueueDatagramSend (
		std::shared_ptr<std::vector<uint8_t>> data, const boost::asio::ip::udp::endpoint & ep)
	{
		if (!data || data->empty ()) return;
		m_DatagramSendQueue.push_back (DatagramSendItem { std::move (data), ep });
		StartNextDatagramSend ();
	}

	void SAMBridge::StartNextDatagramSend ()
	{
		if (m_IsSendingDatagram || m_DatagramSendQueue.empty ()) return;
		m_IsSendingDatagram = true;
		auto data = m_DatagramSendQueue.front ().data;
		auto ep = m_DatagramSendQueue.front ().endpoint;
		m_DatagramSocket.async_send_to (boost::asio::buffer (*data), ep,
			[this, data] (const boost::system::error_code & ecode, std::size_t bytes_transferred)
			{
				(void)data; // keep the bytes alive until the completion handler runs
				HandleDatagramSent (ecode, bytes_transferred);
			});
	}

	void SAMBridge::HandleDatagramSent (const boost::system::error_code & ecode, std::size_t)
	{
		if (!m_DatagramSendQueue.empty ()) m_DatagramSendQueue.pop_front ();
		m_IsSendingDatagram = false;
		if (ecode)
		{
			if (ecode != boost::asio::error::operation_aborted)
				LogPrint (eLogError, "SAM: Datagram send error: ", ecode.message ());
		}
		StartNextDatagramSend ();
	}

	void SAMBridge::ReceiveDatagram ()
	{
		m_DatagramSocket.async_receive_from (
			boost::asio::buffer (m_DatagramReceiveBuffer, i2p::datagram::MAX_DATAGRAM_SIZE), m_SenderEndpoint,
			std::bind (&SAMBridge::HandleReceivedDatagram, this, std::placeholders::_1, std::placeholders::_2));
	}

	void SAMBridge::HandleReceivedDatagram (const boost::system::error_code & ecode, std::size_t bytes_transferred)
	{
		if (!ecode)
		{
			m_DatagramReceiveBuffer[bytes_transferred] = 0;
			char * eol = strchr ((char *)m_DatagramReceiveBuffer, '\n');
			if (eol)
			{
				*eol = 0;
				eol++;
				size_t payloadLen = bytes_transferred - ((uint8_t *)eol - m_DatagramReceiveBuffer);
				LogPrint (eLogDebug, "SAM: Datagram received ", m_DatagramReceiveBuffer, " size=", payloadLen);
				SAMDatagramHeader header;
				if (ParseSAMDatagramHeader ({ (const char *)m_DatagramReceiveBuffer,
												(size_t)((char *)eol - 1 - (char *)m_DatagramReceiveBuffer) },
						header))
				{
					auto session = FindSession (header.sessionID);
					if (session)
					{
						// Per-datagram options override the session defaults.
						uint16_t fromPort = session->FromPort, toPort = session->ToPort;
						uint8_t protocol = session->Protocol;
						if (!header.options.empty ())
						{
							auto params = SAMSocket::ExtractParams (header.options);
							uint16_t value = 0;
							if (params.Contains (SAM_PARAM_FROM_PORT))
							{
								if (ParseSAMNumber (params[SAM_PARAM_FROM_PORT], value))
									fromPort = value;
								else
									LogPrint (eLogWarning, "SAM: Invalid datagram FROM_PORT ",
										params[SAM_PARAM_FROM_PORT], ", using session default");
							}
							if (params.Contains (SAM_PARAM_TO_PORT))
							{
								if (ParseSAMNumber (params[SAM_PARAM_TO_PORT], value))
									toPort = value;
								else
									LogPrint (eLogWarning, "SAM: Invalid datagram TO_PORT ", params[SAM_PARAM_TO_PORT],
										", using session default");
							}
							if (session->Type == SAMSessionType::eSAMSessionTypeRaw
								&& params.Contains (SAM_PARAM_PROTOCOL))
							{
								uint8_t proto = 0;
								if (ParseSAMNumber (params[SAM_PARAM_PROTOCOL], proto))
									protocol = proto;
								else
									LogPrint (eLogWarning, "SAM: Invalid datagram PROTOCOL ",
										params[SAM_PARAM_PROTOCOL], ", using session default");
							}
							LogPrint (eLogInfo, "SAM: Datagram params are FROM_PORT=", fromPort, " TO_PORT=", toPort,
								" PROTOCOL=", (unsigned)protocol);
						}
						session->SendDatagram (
							header.destination, (const uint8_t *)eol, payloadLen, fromPort, toPort, protocol);
					}
					else
						LogPrint (eLogError, "SAM: Session ", header.sessionID, " not found");
				}
				else
					LogPrint (eLogError, "SAM: Invalid datagram");
			}
			else
				LogPrint (eLogError, "SAM: Invalid datagram");
			ReceiveDatagram ();
		}
		else
			LogPrint (eLogError, "SAM: Datagram receive error: ", ecode.message ());
	}

	bool SAMBridge::ResolveSignatureType (std::string_view name, i2p::data::SigningKeyType & type) const
	{
		auto res = std::from_chars (name.data (), name.data () + name.size (), type);
		if (res.ec == std::errc ())
		{
			if (res.ptr != name.data () + name.size ()) return false;
		}
		else if (res.ec == std::errc::invalid_argument)
		{
			// Java SigType names are case-insensitive.
			auto equalsIgnoreCase = [] (std::string_view a, std::string_view b)
			{
				if (a.size () != b.size ()) return false;
				for (size_t i = 0; i < a.size (); ++i)
					if (std::tolower (static_cast<unsigned char> (a[i]))
						!= std::tolower (static_cast<unsigned char> (b[i])))
						return false;
				return true;
			};
			bool found = false;
			for (const auto & it : m_SignatureTypes)
				if (equalsIgnoreCase (name, it.first))
				{
					type = it.second;
					found = true;
					break;
				}
			if (!found) return false;
		}
		else
			return false;
		// Do not silently let PrivateKeys::CreateRandomKeys fall back to DSA
		// for an unsupported numeric type.
		std::unique_ptr<i2p::crypto::Verifier> verifier (i2p::data::IdentityEx::CreateVerifier (type));
		return verifier != nullptr;
	}
}
}

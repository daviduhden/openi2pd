/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: SAM.
 *
 * Declares the SAM protocol constants, session and master/subsession
 * objects, the socket state machine, the bounded control-input framer
 * and the bridge owning the TCP acceptor and UDP endpoint. The header
 * also holds the pure grammar helpers: parameter parsing (duplicate
 * keys, bare boolean tokens), HELLO VERSION negotiation,
 * LISTEN_PORT/LISTEN_PROTOCOL resolution and the per-style effective
 * receive protocol.
 *
 */

#ifndef SAM_H__
#define SAM_H__

#include <inttypes.h>
#include <charconv>
#include <concepts>
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
#include <algorithm>
#include <boost/asio.hpp>
#include "util.h"
#include "Identity.h"
#include "LeaseSet.h"
#include "Streaming.h"
#include "Destination.h" // PROTOCOL_TYPE_* for the SAM 3.3 style listen protocol
#include "Datagram.h"
#include "Destination.h"

namespace i2p
{
namespace client
{
	// Command/frame buffer must hold the largest SAM payload (32 KiB) plus
	// a base64 destination and command line. The old 8 KiB buffer caused a
	// zero-length read loop for valid RAW/DATAGRAM SEND frames.
	const size_t SAM_SOCKET_BUFFER_SIZE = 49152;
	const size_t SAM_STREAM_BUFFER_SIZE = 65536;
	const size_t SAM_STREAM_MAX_SEND_BUFFER_SIZE = 65536;
	const int SAM_SOCKET_CONNECTION_MAX_IDLE = 3600; // in seconds
	const int SAM_SESSION_READINESS_CHECK_INTERVAL = 3; // in seconds
	const int SAM_SESSION_CLEANUP_CHECK_INTERVAL = 5; // in seconds
	const size_t SAM_SESSION_MAX_ACCEPT_QUEUE_SIZE = 50;
	// Bound on concurrent TCP control connections. A SAM client needs one
	// control connection per session, accept and forward socket; the cap
	// is admission-only and further connections are closed immediately.
	const size_t SAM_MAX_CONTROL_CONNECTIONS = 64;
	// Control-connection deadlines. SAMHandlerFactory closes a connection
	// that stays silent before HELLO (HELLO_TIMEOUT 60 s) and then
	// disables the socket timeout, so an idle *session* control connection
	// must never be closed. The second deadline only bounds a partially
	// received command line, so a slow or stalled sender cannot hold the
	// parser indefinitely.
	const int SAM_CONTROL_HELLO_TIMEOUT = 60; // in seconds
	const int SAM_CONTROL_PARTIAL_TIMEOUT = 180; // in seconds

	const char SAM_HANDSHAKE[] = "HELLO VERSION";
	const char SAM_HANDSHAKE_REPLY[] = "HELLO REPLY RESULT=OK VERSION=%s\n";
	constexpr std::string_view SAM_HANDSHAKE_NOVERSION { "HELLO REPLY RESULT=NOVERSION\n" };
	const char SAM_HANDSHAKE_I2P_ERROR[] = "HELLO REPLY RESULT=I2P_ERROR\n";
	// A malformed HELLO (missing fields or unparsable version numbers) is
	// answered before the connection is closed.
	const char SAM_HANDSHAKE_I2P_ERROR_MESSAGE[] = "HELLO REPLY RESULT=I2P_ERROR MESSAGE=\"%s\"\n";
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
	// SAM v3 distinguishes an expired connect from an unreachable peer and
	// a second ACCEPT/FORWARD from a protocol error.
	constexpr std::string_view SAM_STREAM_STATUS_TIMEOUT { "STREAM STATUS RESULT=TIMEOUT\n" };
	constexpr std::string_view SAM_STREAM_STATUS_ALREADY_ACCEPTING { "STREAM STATUS RESULT=ALREADY_ACCEPTING\n" };
	// openi2pd has no SAM authenticator, so AUTH is refused without
	// closing the control connection (SAMv3Handler keeps it for a failed
	// AUTH).
	constexpr std::string_view SAM_AUTH_STATUS_I2P_ERROR {
		"AUTH STATUS RESULT=I2P_ERROR MESSAGE=\"AUTH is not supported\"\n"
	};
	const char SAM_STREAM_ACCEPT[] = "STREAM ACCEPT";
	const char SAM_STREAM_FORWARD[] = "STREAM FORWARD";
	const char SAM_DATAGRAM_SEND[] = "DATAGRAM SEND";
	const char SAM_RAW_SEND[] = "RAW SEND";
	const char SAM_DEST_GENERATE[] = "DEST GENERATE";
	const char SAM_DEST_REPLY[] = "DEST REPLY PUB=%s PRIV=%s\n";
	const char SAM_DEST_REPLY_I2P_ERROR[] = "DEST REPLY RESULT=I2P_ERROR\n";
	const char SAM_NAMING_LOOKUP[] = "NAMING LOOKUP";
	const char SAM_NAMING_REPLY[] = "NAMING REPLY RESULT=OK NAME=%s VALUE=%s\n";
	// SAM 3.2 adds the port fields. Do not advertise them when the
	// negotiated version is older
	// (SAMv1Handler.receiveDatagramBytes/receiveRawBytes).
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
	const char SAM_PARAM_LISTEN_PROTOCOL[] = "LISTEN_PROTOCOL";
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

	// SAM 3.3 LISTEN_PROTOCOL. A RAW subsession receives an exact I2CP
	// payload protocol; 0 is the catch-all and 6 is reserved for
	// streaming, which is not a raw payload protocol (SAMv3 PrimarySession
	// rejects it).
	constexpr uint8_t SAM_LISTEN_PROTOCOL_ANY = 0;
	constexpr uint8_t SAM_LISTEN_PROTOCOL_STREAMING = 6;

	constexpr std::string_view SAM_PING { "PING" };
	constexpr std::string_view SAM_PONG { "PONG\n" };
	// SAMv3Handler.handle(): HELP is answered and the session is kept
	// open.
	constexpr std::string_view SAM_HELP_REPLY { "HELP STATUS RESULT=OK MESSAGE=https://i2p.net/en/docs/api/samv3\n" };

	// Largest payload accepted by DATAGRAM SEND and RAW SEND, matching
	// SAMv1Handler.checkDatagramSize()/checkSize().
	constexpr size_t SAM_DATAGRAM_SEND_MAX_SIZE = 31744;
	constexpr size_t SAM_RAW_SEND_MAX_SIZE = 32768;

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

	// One-word control commands accepted by SAMv3Handler.handle() before
	// the two-word SAM dispatch. Java's SAMUtils.parseParams() upper-cases
	// the COMMAND token, so the classification is ASCII case-insensitive.
	enum class SAMControlCommand
	{
		eSAMControlCommandNone, // a two-word SAM command
		eSAMControlCommandUnknown, // a single word that is not a control command
		eSAMControlCommandPing,
		eSAMControlCommandPong,
		eSAMControlCommandHelp,
		eSAMControlCommandAuth,
		eSAMControlCommandQuit,
		eSAMControlCommandStop,
		eSAMControlCommandExit
	};

	inline bool SAMCommandEquals (std::string_view word, std::string_view expected)
	{
		if (word.size () != expected.size ()) return false;
		for (size_t i = 0; i < word.size (); i++)
		{
			auto ch = word[i];
			auto expectedCh = expected[i];
			if (ch >= 'a' && ch <= 'z') ch = static_cast<char> (ch - 'a' + 'A');
			if (expectedCh >= 'a' && expectedCh <= 'z') expectedCh = static_cast<char> (expectedCh - 'a' + 'A');
			if (ch != expectedCh) return false;
		}
		return true;
	}

	inline std::string ToUpperSAMCommand (std::string_view word)
	{
		std::string upper (word);
		for (auto & ch : upper)
			if (ch >= 'a' && ch <= 'z') ch = static_cast<char> (ch - 'a' + 'A');
		return upper;
	}

	// Classifies the first token of a control line. `argument` receives
	// the remainder after the first space; SAMv3Handler keeps the rest of
	// a PING/PONG line verbatim instead of parsing key=value fields. AUTH
	// is a one-word command whose argument may carry credentials, so
	// callers must not log it.
	inline SAMControlCommand ParseSAMControlCommand (std::string_view line, std::string_view & argument)
	{
		argument = { };
		auto space = line.find (' ');
		auto word = space == std::string_view::npos ? line : line.substr (0, space);
		if (space != std::string_view::npos) argument = line.substr (space + 1);
		if (SAMCommandEquals (word, "PING")) return SAMControlCommand::eSAMControlCommandPing;
		if (SAMCommandEquals (word, "PONG")) return SAMControlCommand::eSAMControlCommandPong;
		if (SAMCommandEquals (word, "HELP")) return SAMControlCommand::eSAMControlCommandHelp;
		if (SAMCommandEquals (word, "AUTH")) return SAMControlCommand::eSAMControlCommandAuth;
		if (SAMCommandEquals (word, "QUIT")) return SAMControlCommand::eSAMControlCommandQuit;
		if (SAMCommandEquals (word, "STOP")) return SAMControlCommand::eSAMControlCommandStop;
		if (SAMCommandEquals (word, "EXIT")) return SAMControlCommand::eSAMControlCommandExit;
		if (space == std::string_view::npos) return SAMControlCommand::eSAMControlCommandUnknown;
		return SAMControlCommand::eSAMControlCommandNone;
	}

	// Builds the reply line for a one-word control command. Returns an
	// empty string when no reply must be written (PONG, ordinary
	// commands). `word` is the token as received; replies use the
	// upper-case spelling that Java derives from parseParams().
	inline std::string MakeSAMControlReply (SAMControlCommand command, std::string_view word, std::string_view argument)
	{
		std::string reply;
		switch (command)
		{
		case SAMControlCommand::eSAMControlCommandPing:
			if (argument.empty ())
			{
				reply = SAM_PONG;
				break;
			}
			reply = "PONG ";
			reply.append (argument);
			reply.push_back ('\n');
			break;
		case SAMControlCommand::eSAMControlCommandHelp: reply = SAM_HELP_REPLY; break;
		case SAMControlCommand::eSAMControlCommandAuth: reply = SAM_AUTH_STATUS_I2P_ERROR; break;
		case SAMControlCommand::eSAMControlCommandQuit:
		case SAMControlCommand::eSAMControlCommandStop:
		case SAMControlCommand::eSAMControlCommandExit:
			reply = ToUpperSAMCommand (word);
			reply.append (" STATUS RESULT=OK MESSAGE=bye\n");
			break;
		case SAMControlCommand::eSAMControlCommandUnknown:
			reply = ToUpperSAMCommand (word);
			reply.append (" STATUS RESULT=I2P_ERROR MESSAGE=\"unknown command or missing subcommand, "
						  "enter HELP for help\"\n");
			break;
		default: break;
		}
		return reply;
	}

	enum class SAMFrameStatus
	{
		eSAMFrameComplete,
		eSAMFrameNeedMore,
		eSAMFrameTooLong // the bounded control buffer cannot hold the frame
	};

	/// Offsets in the bounded control input: a newline-terminated command
	/// can be followed immediately by SIZE bytes for DATAGRAM/RAW SEND.
	/// The payload is not another command, even when it contains a
	/// newline.
	struct SAMFrame
	{
		size_t lineLength = 0; // command line including the '\n' terminator
		size_t frameLength = 0; // command line plus SEND payload
		size_t payloadLength = 0; // SEND payload (DATAGRAM SEND/RAW SEND only)
	};

	inline bool IsSAMSendCommand (std::string_view line)
	{
		auto firstSpace = line.find (' ');
		if (firstSpace == std::string_view::npos) return false;
		size_t secondStart = firstSpace + 1;
		while (secondStart < line.size () && line[secondStart] == ' ') ++secondStart;
		auto secondSpace = secondStart < line.size () ? line.find (' ', secondStart) : std::string_view::npos;
		auto second = line.substr (
			secondStart, secondSpace == std::string_view::npos ? std::string_view::npos : secondSpace - secondStart);
		return (SAMCommandEquals (line.substr (0, firstSpace), "DATAGRAM")
				   || SAMCommandEquals (line.substr (0, firstSpace), "RAW"))
			&& SAMCommandEquals (second, "SEND");
	}

	// Returns the value of a `NAME=value` field in a SAM parameter list.
	// Like Java's parseParams(), only whole space-delimited fields match,
	// so bytes of a SEND payload can never be mistaken for a parameter.
	inline std::string_view FindSAMParam (std::string_view params, std::string_view name)
	{
		size_t pos = 0;
		while (pos < params.size ())
		{
			auto end = params.find (' ', pos);
			if (end == std::string_view::npos) end = params.size ();
			auto field = params.substr (pos, end - pos);
			if (field.size () > name.size () && field[name.size ()] == '='
				&& SAMCommandEquals (field.substr (0, name.size ()), name))
				return field.substr (name.size () + 1);
			pos = end + 1;
		}
		return { };
	}

	// SAM 3.2 port, protocol and SIZE fields must be complete decimal
	// numbers. Unlike Mapping::Get, this rejects trailing garbage instead
	// of silently truncating it.
	template <std::integral T> bool ParseSAMNumber (std::string_view s, T & value)
	{
		if (s.empty ()) return false;
		auto res = std::from_chars (s.data (), s.data () + s.size (), value);
		return res.ec == std::errc () && res.ptr == s.data () + s.size ();
	}

	// A parsed SAM parameter list. Java's SAMUtils.parseParams() throws
	// when the same key appears twice, which terminates the control
	// connection, so the duplicate condition is reported to the SAM
	// command handlers instead of silently keeping the first or last
	// value.
	struct SAMParams
	{
		i2p::util::Mapping values;
		bool duplicate = false;
	};

	// Parses a SAM parameter list. Keys are normalized to upper case,
	// quoted values honour backslash escapes and a bare token is a present
	// parameter with the value "true", because SAMUtils.getBool() treats
	// `SILENT`, `HEADER` and `KEY` without `=value` as true. Spaces, tabs
	// and form feeds separate fields, like SAMUtils.parseParams().
	inline SAMParams ParseSAMParams (std::string_view buf)
	{
		SAMParams result;
		auto isSeparator = [] (char ch) { return ch == ' ' || ch == '\t' || ch == '\f'; };
		size_t pos = 0;
		auto skipSeparators = [&buf, &pos, &isSeparator] ()
		{
			while (pos < buf.size () && isSeparator (buf[pos])) ++pos;
		};
		while (pos < buf.size ())
		{
			skipSeparators ();
			if (pos >= buf.size ()) break;

			const size_t keyStart = pos;
			while (pos < buf.size () && buf[pos] != '=' && !isSeparator (buf[pos])) ++pos;
			if (pos >= buf.size () || buf[pos] != '=')
			{
				// A bare token is a parameter without a value: skip it without
				// desynchronising the rest of the line, but record it as true.
				auto bare = ToUpperSAMCommand (buf.substr (keyStart, pos - keyStart));
				while (pos < buf.size () && !isSeparator (buf[pos])) ++pos;
				if (!bare.empty () && !result.values.Insert (bare, SAM_VALUE_TRUE)) result.duplicate = true;
				continue;
			}
			auto key = ToUpperSAMCommand (buf.substr (keyStart, pos - keyStart));
			++pos;
			std::string value;
			if (pos < buf.size () && buf[pos] == '"')
			{
				// SAMUtils.parseParams() supports quoted values. A backslash
				// escapes the next byte; an unterminated quote consumes the
				// remainder as a bounded value.
				++pos;
				bool escaped = false;
				while (pos < buf.size ())
				{
					const char ch = buf[pos++];
					if (escaped)
					{
						value.push_back (ch);
						escaped = false;
					}
					else if (ch == '\\')
						escaped = true;
					else if (ch == '"')
						break;
					else
						value.push_back (ch);
				}
				if (escaped) value.push_back ('\\');
			}
			else
			{
				const size_t valueStart = pos;
				while (pos < buf.size () && !isSeparator (buf[pos])) ++pos;
				value.assign (buf.substr (valueStart, pos - valueStart));
			}
			if (!key.empty () && !result.values.Insert (key, value)) result.duplicate = true;
		}
		return result;
	}

	// Java SAMUtils.getBool(): a present parameter is true when it carries
	// no value (`SILENT`) or the value is "true" (case-insensitive). An
	// explicit `SILENT=false` therefore stays false.
	inline bool SAMIsParamTrue (const i2p::util::Mapping & params, std::string_view name)
	{
		if (!params.Contains (name)) return false;
		auto value = params[name];
		return value.empty () || SAMCommandEquals (value, SAM_VALUE_TRUE);
	}

	// SAM version components are compared numerically, component by
	// component, like Java's VersionComparator. Concatenating the digits
	// (the previous approach) mis-orders versions such as "3.2.1".
	inline bool ParseSAMVersionComponents (std::string_view ver, std::vector<uint64_t> & components)
	{
		components.clear ();
		if (ver.empty ()) return false;
		size_t pos = 0;
		while (pos <= ver.size ())
		{
			auto dot = ver.find ('.', pos);
			auto end = dot == std::string_view::npos ? ver.size () : dot;
			if (end == pos) return false;
			uint64_t value = 0;
			for (size_t i = pos; i < end; i++)
			{
				auto ch = ver[i];
				if (ch < '0' || ch > '9') return false;
				if (value > MAX_SAM_VERSION_VALUE) return false; // absurd component
				value = value * 10 + (ch - '0');
			}
			components.push_back (value);
			if (dot == std::string_view::npos) break;
			pos = dot + 1;
		}
		return !components.empty ();
	}

	inline int CompareSAMVersion (int candidate, const std::vector<uint64_t> & other)
	{
		// Missing trailing components are zero, so "3", "3.0" and "3.0.0"
		// select the same 3.0 handler.
		uint64_t parts[2] = { (uint64_t)(candidate / 10), (uint64_t)(candidate % 10) };
		size_t n = std::max<size_t> (2, other.size ());
		for (size_t i = 0; i < n; i++)
		{
			uint64_t a = i < 2 ? parts[i] : 0;
			uint64_t b = i < other.size () ? other[i] : 0;
			if (a < b) return -1;
			if (a > b) return 1;
		}
		return 0;
	}

	inline int ChooseSAMVersion (
		bool hasMin, const std::vector<uint64_t> & minVer, bool hasMax, const std::vector<uint64_t> & maxVer)
	{
		static constexpr int candidates[] = { MAKE_SAM_VERSION_NUMBER (3, 3), MAKE_SAM_VERSION_NUMBER (3, 2),
			MAKE_SAM_VERSION_NUMBER (3, 1), MAKE_SAM_VERSION_NUMBER (3, 0) };
		for (auto candidate : candidates)
		{
			if (hasMin && CompareSAMVersion (candidate, minVer) < 0) continue;
			if (hasMax && CompareSAMVersion (candidate, maxVer) > 0) continue;
			return candidate;
		}
		return -1;
	}

	enum class SAMHandshakeResult
	{
		eSAMHandshakeOk,
		// Malformed HELLO: HELLO REPLY RESULT=I2P_ERROR MESSAGE=... then
		// close.
		eSAMHandshakeInvalid,
		// Well-formed but unsupported range: HELLO REPLY RESULT=NOVERSION.
		eSAMHandshakeNoVersion
	};

	// Parses "HELLO VERSION [MIN=x.y] [MAX=x.y]" and selects the version
	// to answer with. The first line of a control connection is always the
	// HELLO line, so a malformed line can never be dispatched as a SAM
	// command.
	inline SAMHandshakeResult NegotiateSAMHandshake (std::string_view line, int & version)
	{
		// SAMUtils.parseParams treats spaces, tabs and form feeds as
		// separators and normalises command case. Tokenise the handshake the
		// same way instead of requiring exactly one literal space.
		auto nextToken = [] (std::string_view & rest, std::string_view & token)
		{
			while (!rest.empty () && (rest.front () == ' ' || rest.front () == '\t' || rest.front () == '\f'))
				rest.remove_prefix (1);
			if (rest.empty ()) return false;
			auto pos = rest.find_first_of (" \t\f");
			if (pos == std::string_view::npos)
			{
				token = rest;
				rest = { };
			}
			else
			{
				token = rest.substr (0, pos);
				rest.remove_prefix (pos);
			}
			return !token.empty ();
		};

		std::string_view rest = line, hello, versionWord;
		if (!nextToken (rest, hello) || !nextToken (rest, versionWord) || !SAMCommandEquals (hello, "HELLO")
			|| !SAMCommandEquals (versionWord, "VERSION"))
			return SAMHandshakeResult::eSAMHandshakeInvalid;

		auto parsed = ParseSAMParams (rest);
		if (parsed.duplicate) return SAMHandshakeResult::eSAMHandshakeInvalid;
		const auto & params = parsed.values;

		bool hasMin = false, hasMax = false;
		std::vector<uint64_t> minVer, maxVer;
		if (params.Contains (SAM_PARAM_MAX))
		{
			hasMax = true;
			// SAMHandlerFactory does not validate the bound syntax: an empty or
			// unparsable bound simply matches no supported version and the
			// client receives NOVERSION (not I2P_ERROR, which is reserved for a
			// handshake that is not HELLO VERSION at all).
			if (!ParseSAMVersionComponents (params[SAM_PARAM_MAX], maxVer))
				return SAMHandshakeResult::eSAMHandshakeNoVersion;
		}
		if (params.Contains (SAM_PARAM_MIN))
		{
			hasMin = true;
			if (!ParseSAMVersionComponents (params[SAM_PARAM_MIN], minVer))
				return SAMHandshakeResult::eSAMHandshakeNoVersion;
		}
		// A min > max range simply selects nothing (NOVERSION).
		auto selected = ChooseSAMVersion (hasMin, minVer, hasMax, maxVer);
		if (selected < 0) return SAMHandshakeResult::eSAMHandshakeNoVersion;
		version = selected;
		return SAMHandshakeResult::eSAMHandshakeOk;
	}

	// A control socket executes no SAM command before a successful HELLO
	// VERSION exchange, so a command pipelined behind HELLO cannot run
	// first.
	inline bool SAMCanDispatchCommands (bool handshakeDone)
	{
		return handshakeDone;
	}

	// I2CP receive port of a single session. SAM v3 (SESSION CREATE)
	// defines PORT as the local UDP forward endpoint of a RAW/DATAGRAM
	// session and LISTEN_PORT as the receive port, which defaults to 0
	// (PORT_ANY). Using PORT here made a session with PORT= unable to
	// receive anything.
	inline bool ResolveSAMSessionListenPort (const i2p::util::Mapping & params, uint16_t & listenPort)
	{
		listenPort = 0;
		if (!params.Contains (SAM_PARAM_LISTEN_PORT)) return true;
		return ParseSAMNumber (params[SAM_PARAM_LISTEN_PORT], listenPort);
	}

	// Non-secret summary of SESSION CREATE for the log: the canonical
	// style name and the ID presence only. DESTINATION is a base64
	// private-key blob and never reaches the log; an unrecognized STYLE is
	// reported as "unknown" so a client cannot inject arbitrary log
	// content either.
	inline std::string MakeSAMSessionCreateLogLine (std::string_view style, bool hasId)
	{
		std::string line ("SAM: Session create, STYLE=");
		if (SAMCommandEquals (style, SAM_VALUE_STREAM))
			line.append (SAM_VALUE_STREAM);
		else if (SAMCommandEquals (style, SAM_VALUE_DATAGRAM))
			line.append (SAM_VALUE_DATAGRAM);
		else if (SAMCommandEquals (style, SAM_VALUE_DATAGRAM2))
			line.append (SAM_VALUE_DATAGRAM2);
		else if (SAMCommandEquals (style, SAM_VALUE_DATAGRAM3))
			line.append (SAM_VALUE_DATAGRAM3);
		else if (SAMCommandEquals (style, SAM_VALUE_RAW))
			line.append (SAM_VALUE_RAW);
		else if (SAMCommandEquals (style, SAM_VALUE_MASTER))
			line.append (SAM_VALUE_MASTER);
		else if (SAMCommandEquals (style, SAM_VALUE_PRIMARY))
			line.append (SAM_VALUE_PRIMARY);
		else
			line.append ("unknown");
		line.append (hasId ? ", id present" : ", id missing");
		return line;
	}

	/// Frame the bounded SAM control input. DATAGRAM/RAW SEND adds SIZE
	/// bytes after the line; a split payload needs more input rather than
	/// becoming a second command. A zero @a sendLimit or invalid SIZE
	/// completes the line for the handler to reject, without waiting for
	/// an unusable payload.
	/// @see https://i2p.net/en/docs/specs/samv3/
	inline SAMFrameStatus GetSAMNextFrame (std::string_view input, size_t sendLimit, SAMFrame & frame)
	{
		frame = { };
		auto newline = input.find ('\n');
		if (newline == std::string_view::npos)
			return input.size () >= SAM_SOCKET_BUFFER_SIZE ? SAMFrameStatus::eSAMFrameTooLong
														   : SAMFrameStatus::eSAMFrameNeedMore;
		frame.lineLength = newline + 1;
		frame.frameLength = frame.lineLength;
		auto line = input.substr (0, newline);
		if (!line.empty () && line.back () == '\r') line.remove_suffix (1);
		if (!IsSAMSendCommand (line)) return SAMFrameStatus::eSAMFrameComplete;
		if (!sendLimit) return SAMFrameStatus::eSAMFrameComplete; // no SEND-capable session
		// Locate the end of the second command word; repeated spaces are
		// allowed between the words. A SEND without a parameter list is
		// complete immediately so the handler can report the missing SIZE.
		auto firstSpace = line.find (' ');
		size_t secondStart = firstSpace + 1;
		while (secondStart < line.size () && line[secondStart] == ' ') ++secondStart;
		auto secondSpace = secondStart < line.size () ? line.find (' ', secondStart) : std::string_view::npos;
		if (secondSpace == std::string_view::npos) return SAMFrameStatus::eSAMFrameComplete;
		size_t size = 0;
		if (!ParseSAMNumber (FindSAMParam (line.substr (secondSpace + 1), SAM_PARAM_SIZE), size) || !size
			|| size > sendLimit)
			return SAMFrameStatus::eSAMFrameComplete; // the handler validates and reports it
		if (input.size () - frame.lineLength < size)
			return input.size () >= SAM_SOCKET_BUFFER_SIZE ? SAMFrameStatus::eSAMFrameTooLong
														   : SAMFrameStatus::eSAMFrameNeedMore;
		frame.payloadLength = size;
		frame.frameLength = frame.lineLength + size;
		return SAMFrameStatus::eSAMFrameComplete;
	}

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
	/// One SAM TCP control or stream socket. The bridge is borrowed and
	/// must outlive this socket and its callbacks; shared handlers retain
	/// the socket. Terminate releases its admission slot, receivers,
	/// stream and socket once.
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
		// Listen port an ACCEPT/FORWARD socket is armed on. Used to re-arm
		// the next queued ACCEPT of that port and to reset only its acceptor.
		uint16_t GetAcceptListenPort () const { return m_ListenPort; };
		// Set once the bridge admitted this socket against
		// SAM_MAX_CONTROL_CONNECTIONS; Terminate() or the destructor releases
		// the slot through the bridge's shared counter.
		void MarkControlConnectionCounted (const std::shared_ptr<std::atomic<std::size_t>> & counter)
		{
			m_ControlConnectionCounter = counter;
			m_IsControlConnectionCounted = true;
		};

		void Terminate (const char * reason);

		bool IsSession (std::string_view id) const;

		static i2p::util::Mapping ExtractParams (std::string_view buf);

	private:
		void SetID (std::string_view id);

	private:
		void TerminateClose () { Terminate (nullptr); }

		// Control-protocol parsing. The SAM service thread is the only owner
		// of the input buffer; every entry point appends at most
		// SAM_SOCKET_BUFFER_SIZE bytes and then drains complete frames.
		void HandleMessage (const boost::system::error_code & ecode, std::size_t bytes_transferred);
		void ProcessBufferedCommands ();
		bool ProcessOneBufferedFrame ();
		void ProcessControlLine (std::string_view line, size_t lineLength);
		void ProcessHandshakeLine (std::string_view line);
		// Parses the parameters of a command. A duplicate key terminates the
		// control connection (SAMUtils.parseParams throws); returns false
		// then.
		bool GetCommandParams (std::string_view buf, i2p::util::Mapping & params);
		void ConsumeInput (size_t len);
		void ContinueProcessing ();
		size_t GetSAMSendLimit () const;

		// Cancellable deadline for HELLO and for a partially received
		// command. An established idle session may remain open between
		// commands.
		void ArmControlDeadline (int seconds, const char * reason);
		void CancelControlDeadline ();
		void HandleControlDeadline (const boost::system::error_code & ecode);
		void ReleaseControlConnection ();

		// Arms one pending ACCEPT on the session's port-scoped streaming
		// destination. Must run on the SAM service.
		static bool ArmAcceptorOnService (
			const std::shared_ptr<SAMSession> & session, const std::shared_ptr<SAMSocket> & socket);
		void RearmQueuedAccept (uint16_t listenPort);

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
		// sessionID is the session the datagram belongs to, a subsession has
		// its own
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
		void ProcessStreamConnect (std::string_view buf, const char * payload, size_t rem);
		void ProcessStreamAccept (std::string_view buf);
		void ProcessStreamForward (std::string_view buf);
		void ProcessDestGenerate (std::string_view buf);
		void ProcessNamingLookup (std::string_view buf);
		void ProcessSessionAdd (std::string_view buf);
		void ProcessSessionRemove (std::string_view buf);
		void TrackDatagramReceiver (
			std::string_view sessionID, i2p::datagram::DatagramDestination::ReceiverRegistration registration);
		void ResetDatagramReceivers (std::string_view sessionID);
		void ResetAllDatagramReceivers ();
		void ProcessPing (std::string_view text);
		void SendReplyWithMessage (const char * reply, const std::string & msg, bool close);
		void SendSubsessionStatusOk (std::string_view id);
		void SendSubsessionError (std::string_view id, std::string_view msg);
		void SendSessionI2PError (const std::string & msg);
		void SendStreamI2PError (const std::string & msg);
		void SendStreamCantReachPeer (const std::string & msg);
		// Returns the payload bytes consumed (0 when the payload is not
		// buffered yet). `line` is the parameter list after the command
		// words, `payload` the bytes following the line terminator.
		size_t ProcessDatagramSend (std::string_view line, const char * payload, size_t available);

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
		// Separate timer for the HELLO/inter-command deadline: m_Timer is
		// used by the connect poll, the readiness check and the stream
		// lifecycle, and must not be overwritten by a control deadline.
		boost::asio::steady_timer m_ControlDeadline;
		const char * m_ControlDeadlineReason = nullptr;
		// Scratch buffer for replies and for the STREAM CONNECT pre-connect
		// payload (m_BufferOffset bytes).
		char m_Buffer[SAM_SOCKET_BUFFER_SIZE + 1];
		size_t m_BufferOffset; // for session only
		// Bounded control input, retained across reads so that pipelined
		// commands and split lines are parsed without dropping bytes.
		char m_InputBuffer[SAM_SOCKET_BUFFER_SIZE + 1];
		size_t m_InputSize; // pending bytes at the start of m_InputBuffer
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
		bool m_IsReading = false; // one outstanding control read
		// Offset used by the control read that may complete after STREAM
		// CONNECT switched the socket to stream mode. m_InputSize is consumed
		// by the command parser while the read is still outstanding, so the
		// completion handler must not recompute this offset.
		size_t m_ControlReadOffset = 0;
		bool m_IsHandshakeDone = false;
		bool m_IsDispatching = false; // inside ProcessBufferedCommands
		bool m_IsContinueRequested = false;
		bool m_IsReplyQueued = false; // a control reply is being written
		bool m_IsCommandPending = false; // a command with a deferred reply is in flight
		// Listen port this socket accepts/forwards on (0 is the default
		// streaming destination), recorded when ACCEPT/FORWARD is armed.
		uint16_t m_ListenPort = 0;
		// Admission slot of this control connection: a shared counter so the
		// release is safe even if the bridge is already gone, and an atomic
		// flag so the slot is released exactly once.
		std::shared_ptr<std::atomic<std::size_t>> m_ControlConnectionCounter;
		std::atomic<bool> m_IsControlConnectionCounted { false };
		std::map<std::string, std::vector<i2p::datagram::DatagramDestination::ReceiverRegistration>, std::less<>>
			m_DatagramReceivers;
		std::shared_ptr<i2p::stream::Stream> m_Stream;
		int m_Version;
		int m_ConnectAttempts;
		uint16_t m_ConnectFromPort, m_ConnectToPort;
	};

	// First line of a datagram sent to the SAM UDP port. SAM 3.x clients
	// send "3.x $nickname $destination [options]" (SAMv3Handler's UDP
	// listener); accept the legacy openi2pd/i2pd form without the version
	// token as well.
	struct SAMDatagramHeader
	{
		std::string_view version; // empty for the legacy form
		std::string_view sessionID;
		std::string_view destination;
		std::string_view options; // may be empty
	};

	bool ParseSAMDatagramHeader (std::string_view line, SAMDatagramHeader & header);

	// Byte offset the former single-buffer parser preserved between reads
	// for a DATAGRAM SEND/RAW SEND frame. The socket now keeps an explicit
	// bounded input buffer (SAMFrame), so this helper is retained only for
	// the datagram-framing regression vector. `paramsOffset` is the start
	// of the parameters inside the old socket buffer, `len` the bytes from
	// there to the end of the buffer and `processed` the former frame
	// parser result.
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

	// SAM 3.3 primary-subsession identity. Two subsessions collide when
	// their STYLE, listen port and listen protocol are equal (Java 2.14.0
	// PrimarySession). The protocol is the *effective* receive protocol:
	// RAW uses LISTEN_PROTOCOL, DATAGRAM/DATAGRAM2/DATAGRAM3 use the I2CP
	// datagram protocols 17/19/20 and STREAM has none (0). Keeping the
	// datagram protocol in the key lets the three datagram styles share
	// one listen port and still stay distinct subsessions.
	struct SAMSubsessionKey
	{
		SAMSessionType type = SAMSessionType::eSAMSessionTypeUnknown;
		uint8_t protocol = 0;
		uint16_t port = 0;
	};

	inline bool operator== (const SAMSubsessionKey & lhs, const SAMSubsessionKey & rhs)
	{
		return lhs.type == rhs.type && lhs.protocol == rhs.protocol && lhs.port == rhs.port;
	}

	inline bool operator!= (const SAMSubsessionKey & lhs, const SAMSubsessionKey & rhs)
	{
		return !(lhs == rhs);
	}

	// `listenProtocol` is the effective receive protocol from
	// SAMStyleListenProtocol(), not the raw LISTEN_PROTOCOL option.
	inline SAMSubsessionKey MakeSAMSubsessionKey (SAMSessionType type, uint8_t listenProtocol, uint16_t listenPort)
	{
		SAMSubsessionKey key;
		key.type = type;
		key.protocol = listenProtocol;
		key.port = listenPort;
		return key;
	}

	// Effective SAM 3.3 receive protocol of a session style (Java 2.14.0
	// PrimarySession): DATAGRAM/DATAGRAM2/DATAGRAM3 subsessions are
	// selected by the I2CP datagram protocols 17/19/20, RAW by
	// LISTEN_PROTOCOL and STREAM by nothing. `rawListenProtocol` is
	// ignored for the other styles.
	inline uint8_t SAMStyleListenProtocol (
		SAMSessionType type, i2p::datagram::DatagramVersion version, uint8_t rawListenProtocol)
	{
		switch (type)
		{
		case SAMSessionType::eSAMSessionTypeDatagram:
			if (version == i2p::datagram::eDatagramV3) return PROTOCOL_TYPE_DATAGRAM3;
			if (version == i2p::datagram::eDatagramV2) return PROTOCOL_TYPE_DATAGRAM2;
			return PROTOCOL_TYPE_DATAGRAM;
		case SAMSessionType::eSAMSessionTypeRaw: return rawListenProtocol;
		default: return 0;
		}
	}

	// A RAW receive key can never name streaming (6) or a datagram style
	// protocol (17/19/20): DatagramDestination routes those protocols to
	// the datagram handlers, so such a registration could never receive
	// anything (SAMv3 PrimarySession).
	inline bool IsSAMReservedRawListenProtocol (uint8_t protocol)
	{
		return protocol == SAM_LISTEN_PROTOCOL_STREAMING || protocol == PROTOCOL_TYPE_DATAGRAM
			|| protocol == PROTOCOL_TYPE_DATAGRAM2 || protocol == PROTOCOL_TYPE_DATAGRAM3;
	}

	// Resolves LISTEN_PROTOCOL. On RAW sessions and subsessions the option
	// selects the receive protocol (default: PROTOCOL, 0 is the catch-all)
	// and is rejected when it names streaming or a datagram protocol. Java
	// only interprets LISTEN_PROTOCOL for RAW, but a DATAGRAM subsession
	// must accept (validate and ignore) it because the style itself
	// selects 17/19/20; STREAM and PRIMARY reject the option.
	// `sendProtocol` is the already parsed/defaulted PROTOCOL value. On a
	// RAW session `listenProtocol` receives the effective receive key; a
	// DATAGRAM session ignores it because SAMStyleListenProtocol()
	// supplies 17/19/20.
	inline bool ResolveSAMListenProtocol (
		const i2p::util::Mapping & params, SAMSessionType type, uint8_t sendProtocol, uint8_t & listenProtocol)
	{
		listenProtocol = sendProtocol; // SAM 3.3: LISTEN_PROTOCOL defaults to PROTOCOL
		const bool present = params.Contains (SAM_PARAM_LISTEN_PROTOCOL);
		if (type != SAMSessionType::eSAMSessionTypeRaw)
		{
			if (!present) return true;
			uint8_t ignored = 0;
			if (!ParseSAMNumber (params[SAM_PARAM_LISTEN_PROTOCOL], ignored)) return false;
			return type == SAMSessionType::eSAMSessionTypeDatagram;
		}
		if (!present) return !IsSAMReservedRawListenProtocol (listenProtocol);
		uint8_t value = 0;
		if (!ParseSAMNumber (params[SAM_PARAM_LISTEN_PROTOCOL], value)) return false;
		if (IsSAMReservedRawListenProtocol (value)) return false;
		listenProtocol = value;
		return true;
	}

	/// Session state shared by the bridge and admitted sockets. The bridge
	/// is a borrowed owner; the accept queue and forwarder registry share
	/// one mutex.
	struct SAMSession
	{
		SAMBridge & m_Bridge;
		std::string Name;
		SAMSessionType Type;
		i2p::datagram::DatagramVersion DatagramVersion;

		std::shared_ptr<boost::asio::ip::udp::endpoint> UDPEndpoint; // TODO: move
		// Effective listen port selecting the I2CP receivers and the
		// port-scoped streaming destination: LISTEN_PORT for a single session
		// (default 0, PORT only names the local UDP forward endpoint) and
		// inPort for a subsession.
		uint16_t ListenPort = 0;
		// Pending STREAM ACCEPT sockets, guarded by acceptQueueMutex. Use the
		// transfer API below; the list itself is an implementation detail.
		mutable std::mutex acceptQueueMutex;
		std::list<std::pair<std::shared_ptr<SAMSocket>, uint64_t>> acceptQueue; // socket, receive time in seconds
		// Installed STREAM FORWARD acceptors per listen port, guarded by
		// acceptQueueMutex. A port carries at most one forwarder and refuses
		// a concurrent ACCEPT for the same port (RESULT=ALREADY_ACCEPTING).
		std::map<uint16_t, std::size_t> forwardAcceptors;
		uint16_t FromPort = 0, ToPort = 0;
		uint8_t Protocol = i2p::datagram::DATAGRAM_RAW_PROTOCOL;
		bool Header = false; // raw UDP forwarding: prepend PROTOCOL/FROM/TO

		SAMSession (SAMBridge & parent, std::string_view name, SAMSessionType type);
		virtual ~SAMSession () { };

		virtual std::shared_ptr<ClientDestination> GetLocalDestination () = 0;
		virtual void StopLocalDestination () = 0;
		virtual void Close () { CloseStreams (); };

		void CloseStreams ();

		// Accept-queue transfer API. All methods are safe to call from the
		// SAM service thread and from destination callbacks.
		bool EnqueueAccept (const std::shared_ptr<SAMSocket> & socket, uint64_t now);
		void RemoveAccept (const std::shared_ptr<SAMSocket> & socket);
		// Returns the oldest queued acceptor (or nullptr). Acceptors do not
		// expire with age, matching Java; the arguments are retained for
		// source compatibility with the former expiry implementation.
		std::shared_ptr<SAMSocket> TakeNextAccept (uint64_t now, std::list<std::shared_ptr<SAMSocket>> & expired);
		// Returns the oldest queued acceptor whose ACCEPT targets `port`.
		std::shared_ptr<SAMSocket> TakeNextAcceptOnPort (uint16_t port);
		std::size_t GetAcceptQueueSize () const;

		// STREAM FORWARD tracking, guarded like the accept queue.
		bool IsForwarding (uint16_t port) const;
		bool IsForwardingAnyPort () const;
		void AddForwardAcceptor (uint16_t port);
		void RemoveForwardAcceptor (uint16_t port);

		// Resolves a datagram destination - a full base64 destination, a
		// plain b32 or a blinded b33 .i2p host - the same way a stream
		// connect does, then sends the payload. A b33 lookup is async, so the
		// payload is copied.
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
		// Effective SAM 3.3 receive protocol of the subsession:
		// LISTEN_PROTOCOL for RAW (0 is the catch-all), 17/19/20 for
		// DATAGRAM/DATAGRAM2/DATAGRAM3 and 0 for STREAM
		// (SAMStyleListenProtocol).
		uint8_t inProtocol = 0;

		SAMSubSession (std::shared_ptr<SAMMasterSession> master, std::string_view name, SAMSessionType type,
			uint16_t port, uint8_t listenProtocol = 0);
		// implements SAMSession
		std::shared_ptr<ClientDestination> GetLocalDestination ();
		void StopLocalDestination ();
	};

	/// Owns the SAM listener, datagram endpoint and session/socket
	/// registries. Destination callbacks marshal socket work onto the SAM
	/// service thread.
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
		/// Build a session for a validated SESSION CREATE.
		///
		/// @param id          client-chosen session name (unique per bridge)
		/// @param type validated STYLE; MASTER creates a master session
		/// @param destination base64 private keys, or empty for a transient
		///   identity created from SIGNATURE_TYPE/CRYPTO_TYPE
		/// @param params other options, applied only to a new destination
		/// @return nullptr when the key material does not parse or names an
		///   orphaned destination; the caller answers SESSION STATUS instead.
		///
		/// The destination reference of the returned session is acquired here
		/// and released by SAMSession::StopLocalDestination(). Key material
		/// and the options mapping are never logged.
		std::shared_ptr<SAMSession> CreateSession (std::string_view id, SAMSessionType type,
			std::string_view destination, // empty string means transient
			const i2p::util::Mapping & params);
		/// Insert a session under its ID and report whether the ID was free.
		/// Used for SESSION ADD subsessions after the caller has checked the
		/// (style, receive protocol, listen port) key; an already used ID
		/// fails and the caller answers SESSION STATUS instead of sharing
		/// state.
		bool AddSession (std::shared_ptr<SAMSession> session);
		void CloseSession (std::string_view id);
		std::shared_ptr<SAMSession> FindSession (std::string_view id) const;

		std::list<std::shared_ptr<SAMSocket>> ListSockets (std::string_view id) const;

		/** Send raw data to a remote endpoint from UDP. Buffers are
		 * copied, so callers may release them. Sends are queued on
		 * the SAM service; only one asynchronous send is in flight.
		 */
		void SendTo (const std::vector<boost::asio::const_buffer> & bufs, const boost::asio::ip::udp::endpoint & ep);

		void AddSocket (std::shared_ptr<SAMSocket> socket);
		void RemoveSocket (const std::shared_ptr<SAMSocket> & socket);

		// Bounded TCP admission. Each admitted socket holds the counter and
		// releases its slot once, from Terminate() or destruction.
		bool TryAddControlConnection ();
		std::size_t GetControlConnectionCount () const { return m_ControlConnections->load (); };

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
		// Declared before the socket list and shared with every admitted
		// SAMSocket, so a late handler may destroy a socket after the bridge
		// without accessing bridge state.
		std::shared_ptr<std::atomic<std::size_t>> m_ControlConnections = std::make_shared<std::atomic<std::size_t>> (0);
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
		// HTTP snapshot; returned storage is independent of the session lock.
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

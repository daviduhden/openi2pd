/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: test sam control framing.
 *
 * Exercises the bounded SAM control framer and the one-word command grammar:
 * pipelined commands, split lines and SEND payloads across reads, CRLF,
 * buffer limits, PING/PONG/HELP/AUTH/QUIT classification, the exact reply lines
 * of SAMv3Handler, the HELLO VERSION negotiation, duplicate-parameter
 * rejection, bare boolean tokens and the non-secret SESSION CREATE log summary.
 * No socket, router or session is created.
 *
 */

#include <cassert>
#include <string>
#include <string_view>

#include "SAM.h"

namespace
{
using namespace i2p::client;

SAMFrameStatus Frame (std::string_view input, SAMFrame & frame, size_t sendLimit = SAM_RAW_SEND_MAX_SIZE)
{
	return GetSAMNextFrame (input, sendLimit, frame);
}

void TestSingleAndPipelinedLines ()
{
	SAMFrame frame;

	// One complete line.
	assert (Frame ("PING\n", frame) == SAMFrameStatus::eSAMFrameComplete);
	assert (frame.lineLength == 5);
	assert (frame.frameLength == 5);
	assert (frame.payloadLength == 0);

	// A partial line is preserved by the caller, not parsed.
	assert (Frame ("PIN", frame) == SAMFrameStatus::eSAMFrameNeedMore);
	assert (Frame ("", frame) == SAMFrameStatus::eSAMFrameNeedMore);

	// CRLF counts the '\r' in the consumed line.
	assert (Frame ("HELP\r\n", frame) == SAMFrameStatus::eSAMFrameComplete);
	assert (frame.lineLength == 6);
	assert (frame.frameLength == 6);

	// Two pipelined commands in one read: the first frame stops at its '\n',
	// and the second one is framed from the remainder.
	const std::string two = "PING\nDEST GENERATE\n";
	assert (Frame (two, frame) == SAMFrameStatus::eSAMFrameComplete);
	assert (frame.lineLength == 5);
	const auto second = std::string_view (two).substr (frame.frameLength);
	assert (Frame (second, frame) == SAMFrameStatus::eSAMFrameComplete);
	assert (frame.lineLength == second.size ());
	assert (second == "DEST GENERATE\n");

	// HELLO and a command in the same read: the HELLO line is one frame and
	// the command survives in the remaining input.
	const std::string hello = "HELLO VERSION MIN=3.0 MAX=3.3\nPING\n";
	assert (Frame (hello, frame) == SAMFrameStatus::eSAMFrameComplete);
	assert (frame.lineLength == std::string_view ("HELLO VERSION MIN=3.0 MAX=3.3\n").size ());
	const auto afterHello = std::string_view (hello).substr (frame.frameLength);
	assert (Frame (afterHello, frame) == SAMFrameStatus::eSAMFrameComplete);
	assert (afterHello == "PING\n");

	// An empty line is a complete line (the parser ignores it).
	assert (Frame ("\n", frame) == SAMFrameStatus::eSAMFrameComplete);
	assert (frame.lineLength == 1);
}

void TestSendFraming ()
{
	SAMFrame frame;

	// A complete DATAGRAM SEND: line plus exactly SIZE payload bytes.
	const std::string send = "DATAGRAM SEND DESTINATION=abc SIZE=3\nxyz";
	assert (Frame (send, frame) == SAMFrameStatus::eSAMFrameComplete);
	assert (frame.payloadLength == 3);
	assert (frame.frameLength == send.size ());
	assert (frame.lineLength == send.size () - 3);
	assert (std::string_view (send).substr (frame.lineLength) == "xyz");

	// Repeated spaces between the command words still identify the payload
	// boundary and cannot make the parser treat binary bytes as commands.
	const std::string spaced = "DATAGRAM  SEND DESTINATION=abc SIZE=3\nxyz";
	assert (Frame (spaced, frame) == SAMFrameStatus::eSAMFrameComplete);
	assert (frame.payloadLength == 3);
	assert (frame.frameLength == spaced.size ());

	// A split payload is incomplete: it must not be parsed as commands.
	assert (Frame (send.substr (0, send.size () - 1), frame) == SAMFrameStatus::eSAMFrameNeedMore);
	assert (Frame ("RAW SEND DESTINATION=abc SIZE=3\n", frame) == SAMFrameStatus::eSAMFrameNeedMore);

	// A complete frame followed by the next pipelined command: only the frame
	// is consumed and the trailing command stays framed.
	const std::string pipelined = send + "PING\n";
	assert (Frame (pipelined, frame) == SAMFrameStatus::eSAMFrameComplete);
	assert (frame.frameLength == send.size ());
	assert (
		Frame (std::string_view (pipelined).substr (frame.frameLength), frame) == SAMFrameStatus::eSAMFrameComplete);
	assert (frame.lineLength == 5);

	// Binary payloads may contain newlines and spaces without changing the
	// frame, and cannot spoof SIZE (only whole parameter fields match).
	const std::string binary = "RAW SEND DESTINATION=abc SIZE=5\nPING\n";
	assert (Frame (binary, frame) == SAMFrameStatus::eSAMFrameComplete);
	assert (frame.payloadLength == 5);
	assert (frame.frameLength == binary.size ());
	assert (frame.lineLength == std::string_view ("RAW SEND DESTINATION=abc SIZE=5\n").size ());

	// An invalid or oversized SIZE completes the line so the SEND handler
	// reports it and closes instead of buffering attacker-chosen lengths.
	assert (Frame ("DATAGRAM SEND DESTINATION=abc SIZE=0\n", frame) == SAMFrameStatus::eSAMFrameComplete);
	assert (frame.payloadLength == 0);
	assert (Frame ("DATAGRAM SEND DESTINATION=abc SIZE=abc\n", frame) == SAMFrameStatus::eSAMFrameComplete);
	assert (Frame ("DATAGRAM SEND DESTINATION=abc SIZE=31745\n", frame, SAM_DATAGRAM_SEND_MAX_SIZE)
		== SAMFrameStatus::eSAMFrameComplete);
	assert (Frame ("RAW SEND DESTINATION=abc SIZE=32769\n", frame) == SAMFrameStatus::eSAMFrameComplete);

	// A socket without a DATAGRAM/RAW session (sendLimit 0) completes the
	// line without waiting for a payload the client may never send.
	assert (Frame ("DATAGRAM SEND DESTINATION=abc SIZE=3\n", frame, 0) == SAMFrameStatus::eSAMFrameComplete);
	assert (frame.payloadLength == 0);
	assert (frame.frameLength == frame.lineLength);

	// The raw limit accepts a full-sized payload and the datagram limit
	// rejects one byte more.
	std::string raw = "RAW SEND DESTINATION=abc SIZE=32768\n";
	raw.append (SAM_RAW_SEND_MAX_SIZE, 'x');
	assert (Frame (raw, frame) == SAMFrameStatus::eSAMFrameComplete);
	assert (frame.payloadLength == SAM_RAW_SEND_MAX_SIZE);
	assert (frame.frameLength == raw.size ());
	std::string datagram = "DATAGRAM SEND DESTINATION=abc SIZE=31745\n";
	datagram.append (31745, 'x');
	assert (Frame (datagram, frame, SAM_DATAGRAM_SEND_MAX_SIZE) == SAMFrameStatus::eSAMFrameComplete);
	assert (frame.payloadLength == 0);
}

void TestBufferLimits ()
{
	SAMFrame frame;

	// No newline and a full buffer cannot become a valid command line.
	const std::string full (SAM_SOCKET_BUFFER_SIZE, 'a');
	assert (Frame (full, frame) == SAMFrameStatus::eSAMFrameTooLong);

	// A line that fits exactly (content plus '\n') is still accepted.
	std::string maxLine (SAM_SOCKET_BUFFER_SIZE - 1, 'a');
	maxLine.push_back ('\n');
	assert (Frame (maxLine, frame) == SAMFrameStatus::eSAMFrameComplete);
	assert (frame.lineLength == SAM_SOCKET_BUFFER_SIZE);

	// An incomplete SEND payload in a full buffer is rejected instead of
	// looping on a zero-length read. The line is long enough that the buffer
	// cannot also hold the declared 32 KiB payload.
	std::string longLine = "RAW SEND DESTINATION=";
	longLine.append (SAM_SOCKET_BUFFER_SIZE - longLine.size () - 32, 'a');
	longLine.append (" SIZE=32768\n");
	assert (longLine.size () < SAM_SOCKET_BUFFER_SIZE);
	assert (Frame (longLine, frame) == SAMFrameStatus::eSAMFrameNeedMore);
	std::string raw = longLine;
	raw.append (SAM_SOCKET_BUFFER_SIZE - raw.size (), 'x');
	assert (raw.size () == SAM_SOCKET_BUFFER_SIZE);
	assert (Frame (raw, frame) == SAMFrameStatus::eSAMFrameTooLong);

	// A partial command line is still incomplete, not too long, while the
	// buffer has room.
	assert (Frame (raw.substr (0, SAM_SOCKET_BUFFER_SIZE / 2), frame) == SAMFrameStatus::eSAMFrameNeedMore);
}

void TestParameterLookup ()
{
	// Whole fields only: a payload cannot inject or override parameters.
	assert (FindSAMParam ("DESTINATION=abc SIZE=3", "SIZE") == "3");
	assert (FindSAMParam ("XSIZE=3", "SIZE").empty ());
	assert (FindSAMParam ("SIZE=1 SIZE=2", "SIZE") == "1");
	assert (FindSAMParam (" DESTINATION=abc", "DESTINATION") == "abc");
	assert (FindSAMParam ("", "SIZE").empty ());
	assert (FindSAMParam ("SIZE=", "SIZE").empty ());

	// SAMUtils.parseParams() tokenizes quoted values and normalizes keys.
	auto params = SAMSocket::ExtractParams ("id=MySession name=\"two words\" path=\"a\\\"b\" bare");
	assert (params["ID"] == "MySession");
	assert (params["NAME"] == "two words");
	assert (params["PATH"] == "a\"b");
	// A bare token is a present parameter with the value "true"
	// (SAMUtils.getBool treats KEY/SILENT/HEADER without '=' as true).
	assert (params.Contains ("BARE"));
	assert (params["BARE"] == SAM_VALUE_TRUE);

	uint16_t port = 0;
	assert (ParseSAMNumber ("1234", port) && port == 1234);
	assert (!ParseSAMNumber ("12x", port));
	assert (!ParseSAMNumber ("", port));
	size_t size = 0;
	assert (ParseSAMNumber ("31744", size) && size == 31744);
}

void TestBooleanParameters ()
{
	// SAMUtils.getBool(): a bare key is true, "true" (any case) is true, an
	// explicit "false" and an absent key are false.
	auto enabled = ParseSAMParams ("SILENT HEADER=true key=TrUe off=false").values;
	assert (SAMIsParamTrue (enabled, SAM_PARAM_SILENT));
	assert (SAMIsParamTrue (enabled, SAM_PARAM_HEADER));
	assert (SAMIsParamTrue (enabled, "KEY"));
	assert (!SAMIsParamTrue (enabled, "OFF"));
	assert (!SAMIsParamTrue (enabled, "MISSING"));
	assert (!SAMIsParamTrue (ParseSAMParams ("").values, SAM_PARAM_SILENT));

	// A bare key and an explicit key with an empty value are both true.
	assert (SAMIsParamTrue (ParseSAMParams ("SILENT=").values, SAM_PARAM_SILENT));
	// Any other value is not a boolean true.
	assert (!SAMIsParamTrue (ParseSAMParams ("SILENT=yes").values, SAM_PARAM_SILENT));
}

void TestDuplicateParameter ()
{
	// A repeated key is reported, whatever the value form is (Java
	// SAMUtils.parseParams throws, which closes the control connection).
	assert (!ParseSAMParams ("ID=a ID=b").duplicate);
	assert (ParseSAMParams ("ID=a id=b").duplicate);
	assert (ParseSAMParams ("STYLE=RAW STYLE=STREAM").duplicate);
	assert (ParseSAMParams ("SILENT SILENT").duplicate);
	assert (ParseSAMParams ("SILENT SILENT=true").duplicate);
	assert (ParseSAMParams ("SIZE=1").duplicate == false);

	// The duplicate is not silently merged away: the first value is kept so a
	// diagnostic can name the key.
	auto parsed = ParseSAMParams ("ID=first ID=second");
	assert (parsed.duplicate);
	assert (parsed.values["ID"] == "first");

	// The UDP first-line option parser keeps its historical last-wins-free
	// behaviour; only the duplicate flag is additive.
	assert (!SAMSocket::ExtractParams ("FROM_PORT=1 TO_PORT=2").Contains ("ID"));
}

void TestHandshakeNegotiation ()
{
	int version = 0;

	// A normal 3.x client selects the newest version in its range.
	assert (NegotiateSAMHandshake ("HELLO VERSION MIN=3.0 MAX=3.3", version) == SAMHandshakeResult::eSAMHandshakeOk);
	assert (version == MAX_SAM_VERSION);
	assert (NegotiateSAMHandshake ("HELLO VERSION MIN=3.0 MAX=3.1", version) == SAMHandshakeResult::eSAMHandshakeOk);
	assert (version == MAKE_SAM_VERSION_NUMBER (3, 1));
	assert (NegotiateSAMHandshake ("HELLO VERSION MIN=3.0 MAX=9.9", version) == SAMHandshakeResult::eSAMHandshakeOk);
	assert (version == MAX_SAM_VERSION);

	// MIN only, MAX only, case-insensitive words and tab separators.
	assert (NegotiateSAMHandshake ("HELLO VERSION MIN=3.2", version) == SAMHandshakeResult::eSAMHandshakeOk);
	assert (version == MAX_SAM_VERSION);
	assert (NegotiateSAMHandshake ("hello version max=3.0", version) == SAMHandshakeResult::eSAMHandshakeOk);
	assert (version == MAKE_SAM_VERSION_NUMBER (3, 0));
	assert (NegotiateSAMHandshake ("HELLO\tVERSION\tMIN=3.0\tMAX=3.2", version) == SAMHandshakeResult::eSAMHandshakeOk);
	assert (version == SAM_VERSION_32);

	// A well-formed range that selects nothing is NOVERSION.
	assert (NegotiateSAMHandshake ("HELLO VERSION MIN=3.4 MAX=3.9", version)
		== SAMHandshakeResult::eSAMHandshakeNoVersion);
	assert (NegotiateSAMHandshake ("HELLO VERSION MIN=3.3 MAX=3.0", version)
		== SAMHandshakeResult::eSAMHandshakeNoVersion);
	assert (NegotiateSAMHandshake ("HELLO VERSION MIN=2.0 MAX=2.9", version)
		== SAMHandshakeResult::eSAMHandshakeNoVersion);
	// A component above 3.3.x is still a well-formed range, not a syntax error.
	assert (NegotiateSAMHandshake ("HELLO VERSION MIN=3.0.0.0.0.0.0.0.0.0.0.0.1", version)
		== SAMHandshakeResult::eSAMHandshakeNoVersion);

	// Malformed HELLO lines (not a HELLO VERSION command at all, or a
	// duplicate parameter key) get I2P_ERROR, as SAMHandlerFactory throws on
	// them.
	for (const char * line : { "", "HELLO", "HELLO VERSIONX", "VERSION MIN=3.0",
			 "HELLO VERSION MIN=3.0 MIN=3.1" })
	{
		version = 0;
		assert (NegotiateSAMHandshake (line, version) == SAMHandshakeResult::eSAMHandshakeInvalid);
	}
	// A present but unparsable/empty bound matches no supported version.
	// SAMHandlerFactory does not validate the syntax either; it answers
	// NOVERSION rather than I2P_ERROR. The numeric comparison here is stricter
	// than Java's lexicographic VersionComparator (which happens to accept a
	// negative bound), a deliberate and documented divergence.
	for (const char * line : { "HELLO VERSION MIN=x", "HELLO VERSION MAX=3.x", "HELLO VERSION MIN= 3.0",
			 "HELLO VERSION MIN=-1", "HELLO VERSION MAX=" })
	{
		version = 0;
		assert (NegotiateSAMHandshake (line, version) == SAMHandshakeResult::eSAMHandshakeNoVersion);
	}

	// A bare HELLO VERSION (no MIN/MAX) still selects the newest version, as
	// the previous implementation did.
	assert (NegotiateSAMHandshake ("HELLO VERSION", version) == SAMHandshakeResult::eSAMHandshakeOk);
	assert (version == MAX_SAM_VERSION);

	// Trailing components beyond the 3.x pair still compare numerically.
	assert (NegotiateSAMHandshake ("HELLO VERSION MIN=3.0.0 MAX=3.1.5", version) == SAMHandshakeResult::eSAMHandshakeOk);
	assert (version == SAM_VERSION_32);
}

void TestHandshakePrecedesCommands ()
{
	// The first complete frame of a control connection is consumed as HELLO
	// (SAMSocket::ProcessOneBufferedFrame), so no SESSION/STREAM/DEST command
	// can execute before a successful HELLO VERSION exchange.
	assert (!SAMCanDispatchCommands (false));
	assert (SAMCanDispatchCommands (true));

	// A command line is not a HELLO line: the handshake parser refuses it, so a
	// pipelined command cannot be mistaken for the handshake.
	int version = 0;
	for (const char * line : { "SESSION CREATE STYLE=STREAM SILENT=true", "DEST GENERATE", "STREAM ACCEPT ID=x",
			 "AUTH", "PING" })
	{
		assert (NegotiateSAMHandshake (line, version) == SAMHandshakeResult::eSAMHandshakeInvalid);
	}
}

void TestSecretRedaction ()
{
	// The SESSION CREATE log line never carries DESTINATION (base64 private
	// keys) or any other operand value, and an unrecognized STYLE cannot inject
	// log content either.
	const std::string raw = MakeSAMSessionCreateLogLine ("RAW", true);
	assert (raw == "SAM: Session create, STYLE=RAW, id present");
	assert (raw.find ("DESTINATION") == std::string::npos);
	assert (raw.find ("PRIV") == std::string::npos);
	assert (MakeSAMSessionCreateLogLine ("datagram3", false) == "SAM: Session create, STYLE=DATAGRAM3, id missing");
	assert (MakeSAMSessionCreateLogLine ("primary", true) == "SAM: Session create, STYLE=PRIMARY, id present");
	assert (MakeSAMSessionCreateLogLine ("Master", true) == "SAM: Session create, STYLE=MASTER, id present");
	const std::string injected = MakeSAMSessionCreateLogLine ("RAW DESTINATION=secret\nPASSWORD=x", true);
	assert (injected == "SAM: Session create, STYLE=unknown, id present");
	assert (injected.find ("secret") == std::string::npos);
	assert (injected.find ('\n') == std::string::npos);

	// The reply builders never include a secret operand in the fixed part; the
	// DESTINATION reply is formatted separately and is not logged.
	assert (std::string_view (SAM_AUTH_STATUS_I2P_ERROR).find ("PASSWORD") == std::string_view::npos);
	assert (std::string_view (SAM_HANDSHAKE_NOVERSION) == "HELLO REPLY RESULT=NOVERSION\n");
}

void TestControlGrammar ()
{
	using Command = SAMControlCommand;
	std::string_view argument;

	assert (ParseSAMControlCommand ("PING", argument) == Command::eSAMControlCommandPing);
	assert (argument.empty ());
	assert (ParseSAMControlCommand ("PING 123", argument) == Command::eSAMControlCommandPing);
	assert (argument == "123");
	assert (ParseSAMControlCommand ("ping 123", argument) == Command::eSAMControlCommandPing);
	assert (argument == "123");
	assert (ParseSAMControlCommand ("PiNg  1 2", argument) == Command::eSAMControlCommandPing);
	assert (argument == " 1 2");

	assert (ParseSAMControlCommand ("PONG", argument) == Command::eSAMControlCommandPong);
	assert (argument.empty ());
	assert (ParseSAMControlCommand ("pong 42", argument) == Command::eSAMControlCommandPong);
	assert (argument == "42");

	assert (ParseSAMControlCommand ("HELP", argument) == Command::eSAMControlCommandHelp);
	assert (ParseSAMControlCommand ("help me", argument) == Command::eSAMControlCommandHelp);
	assert (ParseSAMControlCommand ("QUIT", argument) == Command::eSAMControlCommandQuit);
	assert (ParseSAMControlCommand ("quit now", argument) == Command::eSAMControlCommandQuit);
	assert (ParseSAMControlCommand ("STOP", argument) == Command::eSAMControlCommandStop);
	assert (ParseSAMControlCommand ("EXIT", argument) == Command::eSAMControlCommandExit);
	assert (ParseSAMControlCommand ("exit", argument) == Command::eSAMControlCommandExit);

	// AUTH is a one-word control command, with or without a credential
	// argument. The argument is never logged.
	assert (ParseSAMControlCommand ("AUTH", argument) == Command::eSAMControlCommandAuth);
	assert (argument.empty ());
	assert (ParseSAMControlCommand ("auth USER=x PASSWORD=y", argument) == Command::eSAMControlCommandAuth);
	assert (argument == "USER=x PASSWORD=y");

	// Two-word SAM commands are not control commands, and a lone unknown word
	// is reported without dropping the session.
	assert (ParseSAMControlCommand ("SESSION CREATE STYLE=STREAM", argument) == Command::eSAMControlCommandNone);
	assert (ParseSAMControlCommand ("STREAM CONNECT ID=x", argument) == Command::eSAMControlCommandNone);
	assert (ParseSAMControlCommand ("PINGX", argument) == Command::eSAMControlCommandUnknown);
	assert (ParseSAMControlCommand ("", argument) == Command::eSAMControlCommandUnknown);
}

void TestControlReplies ()
{
	using Command = SAMControlCommand;

	// Bare PING works and has no trailing space, like execPingMessage(null).
	assert (MakeSAMControlReply (Command::eSAMControlCommandPing, "PING", "") == "PONG\n");
	assert (MakeSAMControlReply (Command::eSAMControlCommandPing, "ping", "123") == "PONG 123\n");
	assert (MakeSAMControlReply (Command::eSAMControlCommandPing, "PING", " a b") == "PONG  a b\n");

	// PONG is silent and must not close the session.
	assert (MakeSAMControlReply (Command::eSAMControlCommandPong, "PONG", "123").empty ());
	assert (MakeSAMControlReply (Command::eSAMControlCommandNone, "SESSION", "").empty ());

	// HELP keeps the session open.
	assert (MakeSAMControlReply (Command::eSAMControlCommandHelp, "HELP", "")
		== "HELP STATUS RESULT=OK MESSAGE=https://i2p.net/en/docs/api/samv3\n");

	// QUIT/STOP/EXIT are acknowledged and then close the control socket.
	assert (MakeSAMControlReply (Command::eSAMControlCommandQuit, "quit", "") == "QUIT STATUS RESULT=OK MESSAGE=bye\n");
	assert (MakeSAMControlReply (Command::eSAMControlCommandStop, "STOP", "") == "STOP STATUS RESULT=OK MESSAGE=bye\n");
	assert (MakeSAMControlReply (Command::eSAMControlCommandExit, "exit", "") == "EXIT STATUS RESULT=OK MESSAGE=bye\n");

	// A single unknown word is answered with an error but keeps the session.
	assert (MakeSAMControlReply (Command::eSAMControlCommandUnknown, "foo", "")
		== "FOO STATUS RESULT=I2P_ERROR MESSAGE=\"unknown command or missing subcommand, enter HELP "
		   "for help\"\n");

	// AUTH is refused without closing the connection, and the reply never
	// echoes the credential argument.
	assert (MakeSAMControlReply (Command::eSAMControlCommandAuth, "AUTH", "USER=x PASSWORD=y")
		== std::string (SAM_AUTH_STATUS_I2P_ERROR));
	assert (MakeSAMControlReply (Command::eSAMControlCommandAuth, "auth", "").find ("PASSWORD")
		== std::string::npos);
}

void TestSendCommandDetection ()
{
	assert (IsSAMSendCommand ("DATAGRAM SEND DESTINATION=abc SIZE=1"));
	assert (IsSAMSendCommand ("RAW SEND DESTINATION=abc SIZE=1"));
	assert (IsSAMSendCommand ("DATAGRAM SEND"));
	assert (IsSAMSendCommand ("DATAGRAM  SEND SIZE=1"));
	assert (!IsSAMSendCommand ("DATAGRAM SENDX SIZE=1"));
	assert (!IsSAMSendCommand ("DATAGRAM SENDS SIZE=1"));
	assert (!IsSAMSendCommand ("SESSION CREATE STYLE=RAW"));
}
} // namespace

int main ()
{
	TestSingleAndPipelinedLines ();
	TestSendFraming ();
	TestBufferLimits ();
	TestParameterLookup ();
	TestBooleanParameters ();
	TestDuplicateParameter ();
	TestHandshakeNegotiation ();
	TestHandshakePrecedesCommands ();
	TestSecretRedaction ();
	TestControlGrammar ();
	TestControlReplies ();
	TestSendCommandDetection ();
	return 0;
}

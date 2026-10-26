/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: test sam datagram framing.
 *
 * Parses SAM 3.x datagram first lines, supporting versioned and legacy forms, per-datagram ports, raw protocol and repeated spaces while rejecting missing nickname/destination and computing resume offsets for split sends.
 *
 */

#include <cassert>
#include <string_view>

#include "SAM.h"
#include "Datagram.h"

int main ()
{
	using namespace i2p::client;

	SAMDatagramHeader header;
	assert (ParseSAMDatagramHeader (
		"3.2 session123 "
		"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA",
		header));
	assert (header.version == "3.2");
	assert (header.sessionID == "session123");
	assert (header.options.empty ());
	assert (header.destination.size () > 32);

	// SAM 3.2 per-datagram ports and raw protocol.
	assert (ParseSAMDatagramHeader ("3.2 nick dest FROM_PORT=1234 TO_PORT=5678 PROTOCOL=18", header));
	assert (header.version == "3.2");
	assert (header.sessionID == "nick");
	assert (header.destination == "dest");
	assert (header.options == "FROM_PORT=1234 TO_PORT=5678 PROTOCOL=18");

	// Any 3.x version is accepted on the first line.
	assert (ParseSAMDatagramHeader ("3.0 nick dest", header));
	assert (header.version == "3.0");
	assert (ParseSAMDatagramHeader ("3.99 nick dest", header));
	assert (header.version == "3.99");

	// Legacy form retained for older local clients.
	assert (ParseSAMDatagramHeader ("nick dest FROM_PORT=1", header));
	assert (header.version.empty ());
	assert (header.sessionID == "nick");
	assert (header.destination == "dest");
	assert (header.options == "FROM_PORT=1");

	// SAM 3.2 allows repeated spaces between fields.
	assert (ParseSAMDatagramHeader ("3.2   nick    dest   TO_PORT=7", header));
	assert (header.sessionID == "nick");
	assert (header.destination == "dest");
	assert (header.options == "TO_PORT=7");

	// Missing nickname or destination is not a usable header.
	assert (!ParseSAMDatagramHeader ("3.2 nick", header));
	assert (!ParseSAMDatagramHeader ("nick", header));
	assert (!ParseSAMDatagramHeader ("", header));

	// Resume offset for a DATAGRAM SEND split across reads. A partial payload
	// must keep the complete command line (offset inside the socket buffer),
	// not only the bytes after the parameters. A complete frame with trailing
	// bytes keeps just the trailing fragment.
	const size_t paramsOffset = 14; // "DATAGRAM SEND "
	assert (GetSAMSendResumeOffset (paramsOffset, 100, 0) == paramsOffset + 100);
	assert (GetSAMSendResumeOffset (paramsOffset, 100, 40) == 60);
	assert (GetSAMSendResumeOffset (paramsOffset, 100, 100) == 0);
	assert (GetSAMSendResumeOffset (paramsOffset, 0, 0) == 0);

	// The SAM 3.3 LISTEN_PROTOCOL contract and the DatagramDestination dispatch
	// contract must agree: streaming (6) and the typed datagram protocols
	// (17/19/20) are consumed by their own handlers and are never RAW payloads,
	// while PROTO_UNSPECIFIED (0) and ordinary payload protocols are.
	for (const uint8_t reserved : { SAM_LISTEN_PROTOCOL_STREAMING, PROTOCOL_TYPE_DATAGRAM, PROTOCOL_TYPE_DATAGRAM2,
			 PROTOCOL_TYPE_DATAGRAM3 })
	{
		assert (IsSAMReservedRawListenProtocol (reserved));
		assert (i2p::datagram::IsReservedRawProtocol (reserved));
		assert (i2p::datagram::IsTypedDatagramProtocol (reserved) == (reserved != SAM_LISTEN_PROTOCOL_STREAMING));
	}
	for (const uint8_t allowed : { (uint8_t) 0, (uint8_t) PROTOCOL_TYPE_RAW, (uint8_t) 33 })
	{
		assert (!IsSAMReservedRawListenProtocol (allowed));
		assert (!i2p::datagram::IsReservedRawProtocol (allowed));
	}
	assert (i2p::datagram::DATAGRAM_RAW_PROTOCOL == PROTOCOL_TYPE_RAW);

	return 0;
}

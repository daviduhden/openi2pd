/*
 * Copyright (c) 2026, David Uhden Collado
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Regression checks for the first line of a SAM datagram sent to the UDP
 * port. SAM 3.x clients send "3.x nickname destination [options]"; the older
 * openi2pd/i2pd form omitted the version token. This does not open a socket.
 */

#include <cassert>
#include <string_view>

#include "SAM.h"

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

	return 0;
}

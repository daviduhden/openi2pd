/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: test SAM subsessions.
 *
 * Exercises the SAM 3.3 LISTEN_PROTOCOL grammar, LISTEN_PORT selection, the
 * per-style effective receive protocol (17/19/20 for DATAGRAM/DATAGRAM2/
 * DATAGRAM3), the primary-subsession duplicate key and protocol-aware RAW
 * receiver dispatch (exact match, protocol-0 catch-all, streaming exclusion and
 * registration reset). No socket, router or SAM session is created; the
 * dispatch cases use a local DatagramDestination fixture.
 *
 */

#include <cassert>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <boost/asio.hpp>

#include "SAM.h"
#include "Datagram.h"
#include "Destination.h"

namespace
{
using namespace i2p::client;

i2p::util::Mapping MakeParams (std::initializer_list<std::pair<const std::string, std::string>> values)
{
	return i2p::util::Mapping (values);
}

std::vector<uint8_t> MakeEmptyStoredGzipMember ()
{
	// DatagramDestination inflates every raw incoming payload. This minimal
	// stored empty gzip member is the same fixture the receiver-lifecycle test
	// uses; it produces a valid zero-length payload.
	return { 0x1f, 0x8b, 0x08, 0, 0, 0, 0, 0, 0, 0xff, 0x01, 0, 0, 0xff, 0xff, 0, 0, 0, 0, 0, 0, 0, 0 };
}

std::shared_ptr<i2p::datagram::DatagramDestination> MakeDatagramDestination ()
{
	// Create the destination directly: storing it through
	// ClientDestination::CreateDatagramDestination would form the owner's
	// intentional destination ownership cycle in this isolated fixture.
	auto keys = i2p::data::PrivateKeys::CreateRandomKeys (
		i2p::data::SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519, i2p::data::CRYPTO_KEY_TYPE_ECIES_X25519_AEAD, true);
	static boost::asio::io_context service;
	auto owner = std::make_shared<i2p::client::ClientDestination> (service, keys, false);
	return std::make_shared<i2p::datagram::DatagramDestination> (owner, false, i2p::datagram::eDatagramV1);
}

// Delivers a raw payload with the given I2CP protocol number through the
// production dispatch path, so the protocol-6 guard and receiver selection are
// both exercised.
void DispatchRaw (
	const std::shared_ptr<i2p::datagram::DatagramDestination> & destination, uint16_t port, uint8_t protocol)
{
	auto payload = MakeEmptyStoredGzipMember ();
	destination->HandleDataMessagePayload (0, port, payload.data (), payload.size (), protocol, nullptr);
}

void TestListenProtocolGrammar ()
{
	uint8_t listen = 0;

	// Absent: LISTEN_PROTOCOL defaults to PROTOCOL (SAM 3.3). A single RAW
	// session still defaults its PROTOCOL to 18.
	auto absent = MakeParams ({ });
	assert (ResolveSAMListenProtocol (absent, SAMSessionType::eSAMSessionTypeRaw, 18, listen));
	assert (listen == 18);
	assert (ResolveSAMListenProtocol (absent, SAMSessionType::eSAMSessionTypeRaw, 33, listen));
	assert (listen == 33);

	// The PROTOCOL default obeys the same rule: a RAW receive key can never be
	// streaming (6) or one of the datagram style protocols (17/19/20), because
	// DatagramDestination routes those to the datagram handlers.
	assert (!ResolveSAMListenProtocol (absent, SAMSessionType::eSAMSessionTypeRaw, 6, listen));
	assert (!ResolveSAMListenProtocol (absent, SAMSessionType::eSAMSessionTypeRaw, 17, listen));
	assert (!ResolveSAMListenProtocol (absent, SAMSessionType::eSAMSessionTypeRaw, 19, listen));
	assert (!ResolveSAMListenProtocol (absent, SAMSessionType::eSAMSessionTypeRaw, 20, listen));
	assert (ResolveSAMListenProtocol (absent, SAMSessionType::eSAMSessionTypeDatagram, 6, listen));

	// Explicit 0 is the catch-all, whatever PROTOCOL says.
	auto any = MakeParams ({ { SAM_PARAM_LISTEN_PROTOCOL, "0" } });
	assert (ResolveSAMListenProtocol (any, SAMSessionType::eSAMSessionTypeRaw, 18, listen));
	assert (listen == SAM_LISTEN_PROTOCOL_ANY);

	// An explicit valid protocol number wins over PROTOCOL.
	auto raw33 = MakeParams ({ { SAM_PARAM_LISTEN_PROTOCOL, "33" } });
	assert (ResolveSAMListenProtocol (raw33, SAMSessionType::eSAMSessionTypeRaw, 18, listen));
	assert (listen == 33);
	assert (ResolveSAMListenProtocol (raw33, SAMSessionType::eSAMSessionTypeRaw, 0, listen));
	assert (listen == 33);

	// 255 is the largest valid I2CP protocol byte.
	auto raw255 = MakeParams ({ { SAM_PARAM_LISTEN_PROTOCOL, "255" } });
	assert (ResolveSAMListenProtocol (raw255, SAMSessionType::eSAMSessionTypeRaw, 18, listen));
	assert (listen == 255);

	// Protocol 6 is streaming and 17/19/20 belong to the datagram styles; a RAW
	// receiver must reject them instead of registering an unreachable key.
	for (const char * value : { "6", "17", "19", "20" })
	{
		auto reserved = MakeParams ({ { SAM_PARAM_LISTEN_PROTOCOL, value } });
		assert (!ResolveSAMListenProtocol (reserved, SAMSessionType::eSAMSessionTypeRaw, 18, listen));
	}

	// Malformed values are rejected: no digits, trailing garbage, signs and a
	// value beyond the uint8_t range.
	for (const char * value : { "", "x", "18x", "-1", "+18", "256", "0x12" })
	{
		auto bad = MakeParams ({ { SAM_PARAM_LISTEN_PROTOCOL, value } });
		assert (!ResolveSAMListenProtocol (bad, SAMSessionType::eSAMSessionTypeRaw, 18, listen));
	}

	// Java only interprets the option for RAW, but a DATAGRAM subsession must
	// accept (validate and ignore) it: the style itself selects 17/19/20. An
	// invalid value is still refused.
	auto validValue = MakeParams ({ { SAM_PARAM_LISTEN_PROTOCOL, "17" } });
	assert (ResolveSAMListenProtocol (validValue, SAMSessionType::eSAMSessionTypeDatagram, 17, listen));
	assert (ResolveSAMListenProtocol (validValue, SAMSessionType::eSAMSessionTypeDatagram, 0, listen));
	auto anyValue = MakeParams ({ { SAM_PARAM_LISTEN_PROTOCOL, "0" } });
	assert (ResolveSAMListenProtocol (anyValue, SAMSessionType::eSAMSessionTypeDatagram, 17, listen));
	auto badValue = MakeParams ({ { SAM_PARAM_LISTEN_PROTOCOL, "x" } });
	assert (!ResolveSAMListenProtocol (badValue, SAMSessionType::eSAMSessionTypeDatagram, 17, listen));

	// STREAM, PRIMARY and unknown styles reject the option even when its value
	// is valid.
	assert (!ResolveSAMListenProtocol (validValue, SAMSessionType::eSAMSessionTypeStream, 0, listen));
	assert (!ResolveSAMListenProtocol (validValue, SAMSessionType::eSAMSessionTypeMaster, 0, listen));
	assert (!ResolveSAMListenProtocol (validValue, SAMSessionType::eSAMSessionTypeUnknown, 0, listen));
	assert (ResolveSAMListenProtocol (absent, SAMSessionType::eSAMSessionTypeStream, 0, listen));
	assert (ResolveSAMListenProtocol (absent, SAMSessionType::eSAMSessionTypeMaster, 0, listen));
}

void TestStyleListenProtocol ()
{
	using Version = i2p::datagram::DatagramVersion;

	// RAW keeps LISTEN_PROTOCOL; the three datagram styles map onto their I2CP
	// protocols so they can share a listen port; STREAM has no receive
	// protocol.
	assert (SAMStyleListenProtocol (SAMSessionType::eSAMSessionTypeRaw, i2p::datagram::eDatagramV1, 33) == 33);
	assert (SAMStyleListenProtocol (SAMSessionType::eSAMSessionTypeRaw, i2p::datagram::eDatagramV3, 0)
		== SAM_LISTEN_PROTOCOL_ANY);
	assert (SAMStyleListenProtocol (SAMSessionType::eSAMSessionTypeDatagram, i2p::datagram::eDatagramV1, 0)
		== PROTOCOL_TYPE_DATAGRAM);
	assert (SAMStyleListenProtocol (SAMSessionType::eSAMSessionTypeDatagram, i2p::datagram::eDatagramV2, 0)
		== PROTOCOL_TYPE_DATAGRAM2);
	assert (SAMStyleListenProtocol (SAMSessionType::eSAMSessionTypeDatagram, i2p::datagram::eDatagramV3, 0)
		== PROTOCOL_TYPE_DATAGRAM3);
	assert (PROTOCOL_TYPE_DATAGRAM == 17);
	assert (PROTOCOL_TYPE_DATAGRAM2 == 19);
	assert (PROTOCOL_TYPE_DATAGRAM3 == 20);
	assert (SAMStyleListenProtocol (SAMSessionType::eSAMSessionTypeStream, Version::eDatagramV3, 33) == 0);
	assert (SAMStyleListenProtocol (SAMSessionType::eSAMSessionTypeMaster, Version::eDatagramV3, 33) == 0);
	assert (SAMStyleListenProtocol (SAMSessionType::eSAMSessionTypeUnknown, Version::eDatagramV1, 33) == 0);
}

void TestSubsessionDuplicateKey ()
{
	using Version = i2p::datagram::DatagramVersion;
	const auto raw18 = MakeSAMSubsessionKey (SAMSessionType::eSAMSessionTypeRaw, 18, 9000);

	// The same (STYLE, protocol, port) triple collides.
	assert (raw18 == MakeSAMSubsessionKey (SAMSessionType::eSAMSessionTypeRaw, 18, 9000));

	// Different RAW protocols on the same port are distinct subsessions.
	assert (raw18 != MakeSAMSubsessionKey (SAMSessionType::eSAMSessionTypeRaw, 33, 9000));
	assert (raw18 != MakeSAMSubsessionKey (SAMSessionType::eSAMSessionTypeRaw, SAM_LISTEN_PROTOCOL_ANY, 9000));
	assert (MakeSAMSubsessionKey (SAMSessionType::eSAMSessionTypeRaw, 33, 9000)
		!= MakeSAMSubsessionKey (SAMSessionType::eSAMSessionTypeRaw, 0, 9000));

	// Port and style still separate subsessions.
	assert (raw18 != MakeSAMSubsessionKey (SAMSessionType::eSAMSessionTypeRaw, 18, 9001));
	assert (raw18 != MakeSAMSubsessionKey (SAMSessionType::eSAMSessionTypeDatagram, 18, 9000));
	assert (raw18 != MakeSAMSubsessionKey (SAMSessionType::eSAMSessionTypeStream, 18, 9000));

	// DATAGRAM, DATAGRAM2 and DATAGRAM3 on the same LISTEN_PORT are three
	// distinct subsessions because their effective protocols differ, and a
	// repeated style/protocol/port still collides.
	const auto datagram1 = MakeSAMSubsessionKey (SAMSessionType::eSAMSessionTypeDatagram,
		SAMStyleListenProtocol (SAMSessionType::eSAMSessionTypeDatagram, Version::eDatagramV1, 0), 9000);
	const auto datagram2 = MakeSAMSubsessionKey (SAMSessionType::eSAMSessionTypeDatagram,
		SAMStyleListenProtocol (SAMSessionType::eSAMSessionTypeDatagram, Version::eDatagramV2, 0), 9000);
	const auto datagram3 = MakeSAMSubsessionKey (SAMSessionType::eSAMSessionTypeDatagram,
		SAMStyleListenProtocol (SAMSessionType::eSAMSessionTypeDatagram, Version::eDatagramV3, 0), 9000);
	assert (datagram1 != datagram2);
	assert (datagram1 != datagram3);
	assert (datagram2 != datagram3);
	assert (datagram1
		== MakeSAMSubsessionKey (SAMSessionType::eSAMSessionTypeDatagram,
			SAMStyleListenProtocol (SAMSessionType::eSAMSessionTypeDatagram, Version::eDatagramV1, 0), 9000));
	assert (datagram1 != MakeSAMSubsessionKey (SAMSessionType::eSAMSessionTypeDatagram, PROTOCOL_TYPE_DATAGRAM, 9001));
	assert (datagram1 != raw18);

	// STREAM subsessions have no receive protocol (SAMStyleListenProtocol is 0),
	// so they may share a port with a RAW subsession.
	assert (SAMStyleListenProtocol (SAMSessionType::eSAMSessionTypeStream, Version::eDatagramV1, 6) == 0);
	assert (SAMStyleListenProtocol (SAMSessionType::eSAMSessionTypeMaster, Version::eDatagramV1, 6) == 0);
	assert (MakeSAMSubsessionKey (SAMSessionType::eSAMSessionTypeStream,
				SAMStyleListenProtocol (SAMSessionType::eSAMSessionTypeStream, Version::eDatagramV1, 6), 9000)
		== MakeSAMSubsessionKey (SAMSessionType::eSAMSessionTypeStream, 0, 9000));
}

void TestSessionListenPort ()
{
	// SAM v3: LISTEN_PORT is the I2CP receive port and defaults to 0
	// (PORT_ANY); PORT is only the local UDP forward endpoint and must never
	// become the receive port.
	uint16_t listenPort = 1234;
	assert (ResolveSAMSessionListenPort (MakeParams ({ }), listenPort));
	assert (listenPort == 0);

	assert (ResolveSAMSessionListenPort (MakeParams ({ { SAM_PARAM_LISTEN_PORT, "5000" } }), listenPort));
	assert (listenPort == 5000);
	assert (ResolveSAMSessionListenPort (MakeParams ({ { SAM_PARAM_LISTEN_PORT, "0" } }), listenPort));
	assert (listenPort == 0);
	assert (ResolveSAMSessionListenPort (MakeParams ({ { SAM_PARAM_PORT, "4000" } }), listenPort));
	assert (listenPort == 0);
	assert (ResolveSAMSessionListenPort (
		MakeParams ({ { SAM_PARAM_PORT, "4000" }, { SAM_PARAM_LISTEN_PORT, "6000" } }), listenPort));
	assert (listenPort == 6000);

	for (const char * value : { "", "x", "5000x", "-1", "70000" })
	{
		listenPort = 4321;
		assert (!ResolveSAMSessionListenPort (MakeParams ({ { SAM_PARAM_LISTEN_PORT, value } }), listenPort));
	}
}

void TestProtocolAwareRawDispatch ()
{
	auto destination = MakeDatagramDestination ();
	int catchAllCalls = 0, exactCalls = 0, wildcardPortCalls = 0, streamingCalls = 0;
	auto catchAll = destination->SetRawReceiver (
		[&] (uint8_t, uint16_t, uint16_t, const uint8_t *, size_t) { ++catchAllCalls; }, 9000);
	auto exact = destination->SetRawReceiver (
		[&] (uint8_t, uint16_t, uint16_t, const uint8_t *, size_t) { ++exactCalls; }, 9000, 33);
	auto wildcardPort = destination->SetRawReceiver (
		[&] (uint8_t, uint16_t, uint16_t, const uint8_t *, size_t) { ++wildcardPortCalls; }, 0, 33);

	// Exact protocol match wins over the protocol-0 catch-all on that port.
	DispatchRaw (destination, 9000, 33);
	assert (exactCalls == 1);
	assert (catchAllCalls == 0);

	// Any other protocol on the port falls back to the catch-all.
	DispatchRaw (destination, 9000, 18);
	assert (catchAllCalls == 1);
	assert (exactCalls == 1);

	// An incoming protocol 0 selects the port catch-all as well.
	DispatchRaw (destination, 9000, 0);
	assert (catchAllCalls == 2);

	// A protocol-specific registration on another port reaches the port
	// wildcard with the same protocol.
	DispatchRaw (destination, 9001, 33);
	assert (wildcardPortCalls == 1);
	assert (catchAllCalls == 2);
	assert (exactCalls == 1);

	// A protocol-specific registration must not become the destination-wide
	// fallback for unrelated raw traffic.
	DispatchRaw (destination, 9002, 18);
	assert (catchAllCalls == 3);
	assert (wildcardPortCalls == 1);

	// Protocol 6 (streaming) is never delivered to a raw receiver, including a
	// protocol-0 catch-all.
	DispatchRaw (destination, 9000, 6);
	DispatchRaw (destination, 9000, SAM_LISTEN_PROTOCOL_STREAMING);
	assert (catchAllCalls == 3);
	assert (exactCalls == 1);
	assert (wildcardPortCalls == 1);

	// Registering protocol 6 as a raw receiver is refused outright.
	auto rejected = destination->SetRawReceiver (
		[&] (uint8_t, uint16_t, uint16_t, const uint8_t *, size_t) { ++streamingCalls; }, 9000, 6);
	assert (!rejected);
	DispatchRaw (destination, 9000, 6);
	assert (streamingCalls == 0);

	// Reset receivers before the destination fixture is released.
	assert (exact.Reset ());
	assert (wildcardPort.Reset ());
	assert (catchAll.Reset ());
}

void TestProtocolAwareReset ()
{
	auto destination = MakeDatagramDestination ();
	int catchAllCalls = 0, exactCalls = 0, replacementCalls = 0;
	auto catchAll = destination->SetRawReceiver (
		[&] (uint8_t, uint16_t, uint16_t, const uint8_t *, size_t) { ++catchAllCalls; }, 9100);
	auto exact = destination->SetRawReceiver (
		[&] (uint8_t, uint16_t, uint16_t, const uint8_t *, size_t) { ++exactCalls; }, 9100, 33);

	DispatchRaw (destination, 9100, 33);
	assert (exactCalls == 1);

	// SESSION REMOVE-style reset of the exact protocol registration leaves the
	// catch-all as the remaining match, not a stale protocol-33 entry.
	assert (exact.Reset ());
	DispatchRaw (destination, 9100, 33);
	assert (catchAllCalls == 1);
	exactCalls = 0;

	// Re-registering the same protocol key after a reset succeeds and the new
	// receiver receives the traffic.
	auto replacement = destination->SetRawReceiver (
		[&] (uint8_t, uint16_t, uint16_t, const uint8_t *, size_t) { ++replacementCalls; }, 9100, 33);
	DispatchRaw (destination, 9100, 33);
	assert (replacementCalls == 1);
	assert (catchAllCalls == 1);
	assert (exactCalls == 0);

	assert (replacement.Reset ());
	DispatchRaw (destination, 9100, 33);
	assert (catchAllCalls == 2);
	assert (replacementCalls == 1);

	// Resetting the catch-all removes the destination-wide fallback too.
	assert (catchAll.Reset ());
	DispatchRaw (destination, 9100, 33);
	DispatchRaw (destination, 9999, 18);
	assert (catchAllCalls == 2);
}
}

int main ()
{
	TestListenProtocolGrammar ();
	TestStyleListenProtocol ();
	TestSubsessionDuplicateKey ();
	TestSessionListenPort ();
	TestProtocolAwareRawDispatch ();
	TestProtocolAwareReset ();
	return 0;
}

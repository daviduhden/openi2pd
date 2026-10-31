/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: test datagram receiver dispatch.
 *
 * Covers (protocol, port) keying of identity and hash receivers for
 * Datagram1 (17), Datagram2 (19) and Datagram3 (20), the explicit
 * catch-all fallback, the absence of port-to-wildcard promotion, the
 * raw protocol registration refusals and the 0xff to PROTO_UNSPECIFIED
 * normalization. No socket, routing path or SAM control connection is
 * involved.
 *
 */

#include <cassert>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

#include <boost/asio.hpp>

#include "Datagram.h"
#include "Destination.h"
#include "Gzip.h"
#include "Identity.h"

namespace
{
struct Fixture
{
	i2p::data::PrivateKeys keys;
	std::shared_ptr<i2p::client::ClientDestination> owner;
	std::shared_ptr<i2p::datagram::DatagramDestination> destination;
};

Fixture MakeFixture ()
{
	// Create the destination directly: storing it through
	// ClientDestination::CreateDatagramDestination would form the owner's
	// intentional destination ownership cycle in this isolated fixture.
	static boost::asio::io_context service;
	Fixture fixture;
	fixture.keys = i2p::data::PrivateKeys::CreateRandomKeys (
		i2p::data::SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519, i2p::data::CRYPTO_KEY_TYPE_ECIES_X25519_AEAD, true);
	fixture.owner = std::make_shared<i2p::client::ClientDestination> (service, fixture.keys, false);
	fixture.destination
		= std::make_shared<i2p::datagram::DatagramDestination> (fixture.owner, false, i2p::datagram::eDatagramV1);
	return fixture;
}

struct Sender
{
	i2p::data::PrivateKeys keys;
	std::shared_ptr<const i2p::data::IdentityEx> identity;
	std::vector<uint8_t> identityBuffer;
};

Sender MakeSender ()
{
	Sender sender;
	sender.keys = i2p::data::PrivateKeys::CreateRandomKeys (
		i2p::data::SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519, i2p::data::CRYPTO_KEY_TYPE_ECIES_X25519_AEAD, true);
	sender.identity = sender.keys.GetPublic ();
	sender.identityBuffer.resize (sender.identity->GetFullLen ());
	sender.identity->ToBuffer (sender.identityBuffer.data (), sender.identityBuffer.size ());
	return sender;
}

// Datagram1 wire form: identity || signature || payload
// (I2PDatagramMaker.java).
std::vector<uint8_t> MakeDatagram1 (const Sender & sender, const std::vector<uint8_t> & payload)
{
	std::vector<uint8_t> signature (sender.keys.GetSignatureLen ());
	assert (signature.size () == sender.identity->GetSignatureLen ());
	sender.keys.Sign (payload.data (), static_cast<int> (payload.size ()), signature.data ());
	std::vector<uint8_t> frame = sender.identityBuffer;
	frame.insert (frame.end (), signature.begin (), signature.end ());
	frame.insert (frame.end (), payload.begin (), payload.end ());
	return frame;
}

// Datagram2 wire form: identity || flags || payload || signature, where
// the signature covers toHash || flags || payload (Datagram2.java
// make()).
std::vector<uint8_t> MakeDatagram2 (
	const Sender & sender, const i2p::data::IdentHash & to, const std::vector<uint8_t> & payload)
{
	const uint8_t flags[] = { 0x00, 0x02 };
	std::vector<uint8_t> signedData (to.data (), to.data () + 32);
	signedData.insert (signedData.end (), flags, flags + 2);
	signedData.insert (signedData.end (), payload.begin (), payload.end ());
	std::vector<uint8_t> signature (sender.keys.GetSignatureLen ());
	sender.keys.Sign (signedData.data (), static_cast<int> (signedData.size ()), signature.data ());
	std::vector<uint8_t> frame = sender.identityBuffer;
	frame.insert (frame.end (), flags, flags + 2);
	frame.insert (frame.end (), payload.begin (), payload.end ());
	frame.insert (frame.end (), signature.begin (), signature.end ());
	return frame;
}

// Datagram3 wire form: senderHash || flags || payload (Datagram3.java).
std::vector<uint8_t> MakeDatagram3 (const i2p::data::IdentHash & from, const std::vector<uint8_t> & payload)
{
	const uint8_t flags[] = { 0x00, 0x03 };
	std::vector<uint8_t> frame (from.data (), from.data () + 32);
	frame.insert (frame.end (), flags, flags + 2);
	frame.insert (frame.end (), payload.begin (), payload.end ());
	return frame;
}

// DatagramDestination inflates every inbound protocol. The fixture is
// built without compression, so a stored (uncompressed) gzip member is
// what the destination would have produced itself.
std::vector<uint8_t> Store (const std::vector<uint8_t> & frame)
{
	std::vector<uint8_t> out (frame.size () + 23);
	const auto len
		= i2p::data::GzipNoCompression (frame.data (), static_cast<uint16_t> (frame.size ()), out.data (), out.size ());
	assert (len == out.size ());
	return out;
}

void Dispatch (Fixture & fixture, uint16_t toPort, uint8_t protocol, const std::vector<uint8_t> & frame)
{
	auto compressed = Store (frame);
	fixture.destination->HandleDataMessagePayload (
		0, toPort, compressed.data (), compressed.size (), protocol, nullptr);
}

const std::vector<uint8_t> PAYLOAD { 0xde, 0xad, 0xbe, 0xef };

void TestProtocolKeyedDispatch ()
{
	auto fixture = MakeFixture ();
	auto sender = MakeSender ();
	int datagram1Calls = 0, datagram2Calls = 0, hashCalls = 0;
	auto datagram1 = fixture.destination->SetReceiver (
		[&] (const i2p::data::IdentityEx &, uint16_t, uint16_t, const uint8_t * buf, size_t len,
			const i2p::util::Mapping *)
		{
			++datagram1Calls;
			assert (len == PAYLOAD.size ());
			assert (std::memcmp (buf, PAYLOAD.data (), PAYLOAD.size ()) == 0);
		},
		9001, i2p::client::PROTOCOL_TYPE_DATAGRAM);
	auto datagram2 = fixture.destination->SetReceiver (
		[&] (const i2p::data::IdentityEx &, uint16_t, uint16_t, const uint8_t * buf, size_t len,
			const i2p::util::Mapping *)
		{
			++datagram2Calls;
			assert (len == PAYLOAD.size ());
			assert (std::memcmp (buf, PAYLOAD.data (), PAYLOAD.size ()) == 0);
		},
		9001, i2p::client::PROTOCOL_TYPE_DATAGRAM2);
	auto datagram3 = fixture.destination->SetHashReceiver (
		[&] (const i2p::data::IdentHash &, uint16_t, uint16_t, const uint8_t * buf, size_t len,
			const i2p::util::Mapping *)
		{
			++hashCalls;
			assert (len == PAYLOAD.size ());
			assert (std::memcmp (buf, PAYLOAD.data (), PAYLOAD.size ()) == 0);
		},
		9001, i2p::client::PROTOCOL_TYPE_DATAGRAM3);

	Dispatch (fixture, 9001, i2p::client::PROTOCOL_TYPE_DATAGRAM, MakeDatagram1 (sender, PAYLOAD));
	assert (datagram1Calls == 1 && datagram2Calls == 0 && hashCalls == 0);

	Dispatch (fixture, 9001, i2p::client::PROTOCOL_TYPE_DATAGRAM2,
		MakeDatagram2 (sender, fixture.keys.GetPublic ()->GetIdentHash (), PAYLOAD));
	assert (datagram1Calls == 1 && datagram2Calls == 1 && hashCalls == 0);

	Dispatch (
		fixture, 9001, i2p::client::PROTOCOL_TYPE_DATAGRAM3, MakeDatagram3 (sender.identity->GetIdentHash (), PAYLOAD));
	assert (datagram1Calls == 1 && datagram2Calls == 1 && hashCalls == 1);

	assert (datagram1.Reset ());
	assert (datagram2.Reset ());
	assert (datagram3.Reset ());
}

void TestStyleIsolation ()
{
	auto fixture = MakeFixture ();
	auto sender = MakeSender ();
	int datagram2Calls = 0, hashCalls = 0;
	auto only2 = fixture.destination->SetReceiver (
		[&] (const i2p::data::IdentityEx &, uint16_t, uint16_t, const uint8_t *, size_t, const i2p::util::Mapping *)
		{ ++datagram2Calls; }, 9100, i2p::client::PROTOCOL_TYPE_DATAGRAM2);
	auto only3 = fixture.destination->SetHashReceiver (
		[&] (const i2p::data::IdentHash &, uint16_t, uint16_t, const uint8_t *, size_t, const i2p::util::Mapping *)
		{ ++hashCalls; }, 9101, i2p::client::PROTOCOL_TYPE_DATAGRAM3);

	// A DATAGRAM2 subsession never receives Datagram1 payloads, and a
	// DATAGRAM3 subsession never receives Datagram2 payloads.
	Dispatch (fixture, 9100, i2p::client::PROTOCOL_TYPE_DATAGRAM, MakeDatagram1 (sender, PAYLOAD));
	assert (datagram2Calls == 0);
	Dispatch (fixture, 9101, i2p::client::PROTOCOL_TYPE_DATAGRAM2,
		MakeDatagram2 (sender, fixture.keys.GetPublic ()->GetIdentHash (), PAYLOAD));
	assert (hashCalls == 0);

	Dispatch (fixture, 9100, i2p::client::PROTOCOL_TYPE_DATAGRAM2,
		MakeDatagram2 (sender, fixture.keys.GetPublic ()->GetIdentHash (), PAYLOAD));
	assert (datagram2Calls == 1);
	Dispatch (
		fixture, 9101, i2p::client::PROTOCOL_TYPE_DATAGRAM3, MakeDatagram3 (sender.identity->GetIdentHash (), PAYLOAD));
	assert (hashCalls == 1);

	assert (only2.Reset ());
	assert (only3.Reset ());
}

void TestCatchAllFallback ()
{
	auto fixture = MakeFixture ();
	auto sender = MakeSender ();
	int identityCalls = 0, protocolWildcardCalls = 0;
	// Explicit (PROTO_ANY, PORT_ANY) listener: Java's only
	// destination-wide
	// fallback.
	auto catchAll = fixture.destination->SetReceiver (
		[&] (const i2p::data::IdentityEx &, uint16_t, uint16_t, const uint8_t *, size_t, const i2p::util::Mapping *)
		{ ++identityCalls; });
	// Protocol wildcard on any port.
	auto protocolWildcard = fixture.destination->SetReceiver (
		[&] (const i2p::data::IdentityEx &, uint16_t, uint16_t, const uint8_t *, size_t, const i2p::util::Mapping *)
		{ ++protocolWildcardCalls; }, 0, i2p::client::PROTOCOL_TYPE_DATAGRAM);

	Dispatch (fixture, 9050, i2p::client::PROTOCOL_TYPE_DATAGRAM, MakeDatagram1 (sender, PAYLOAD));
	assert (protocolWildcardCalls == 1);
	assert (identityCalls == 0);

	// Datagram2 has no protocol wildcard, so it reaches the (0, 0)
	// listener.
	Dispatch (fixture, 9051, i2p::client::PROTOCOL_TYPE_DATAGRAM2,
		MakeDatagram2 (sender, fixture.keys.GetPublic ()->GetIdentHash (), PAYLOAD));
	assert (identityCalls == 1);

	// Datagram3 without a hash receiver and without a cached LeaseSet is
	// never promoted to the identity receiver.
	Dispatch (
		fixture, 9051, i2p::client::PROTOCOL_TYPE_DATAGRAM3, MakeDatagram3 (sender.identity->GetIdentHash (), PAYLOAD));
	assert (identityCalls == 1);
	assert (protocolWildcardCalls == 1);

	assert (catchAll.Reset ());
	assert (protocolWildcard.Reset ());
}

void TestNoPortPromotion ()
{
	auto fixture = MakeFixture ();
	auto sender = MakeSender ();
	int portCalls = 0, hashCalls = 0;
	auto receiver = fixture.destination->SetReceiver (
		[&] (const i2p::data::IdentityEx &, uint16_t, uint16_t, const uint8_t *, size_t, const i2p::util::Mapping *)
		{ ++portCalls; }, 9001);
	auto hashReceiver = fixture.destination->SetHashReceiver (
		[&] (const i2p::data::IdentHash &, uint16_t, uint16_t, const uint8_t *, size_t, const i2p::util::Mapping *)
		{ ++hashCalls; }, 9001);

	// A port-specific registration is not promoted to the destination-wide
	// fallback: another port (and the default port) stays undelivered.
	Dispatch (fixture, 9003, i2p::client::PROTOCOL_TYPE_DATAGRAM, MakeDatagram1 (sender, PAYLOAD));
	assert (portCalls == 0);
	Dispatch (fixture, 0, i2p::client::PROTOCOL_TYPE_DATAGRAM, MakeDatagram1 (sender, PAYLOAD));
	assert (portCalls == 0);
	Dispatch (
		fixture, 9003, i2p::client::PROTOCOL_TYPE_DATAGRAM3, MakeDatagram3 (sender.identity->GetIdentHash (), PAYLOAD));
	assert (hashCalls == 0);

	// The addressed port still delivers.
	Dispatch (fixture, 9001, i2p::client::PROTOCOL_TYPE_DATAGRAM, MakeDatagram1 (sender, PAYLOAD));
	assert (portCalls == 1);
	Dispatch (
		fixture, 9001, i2p::client::PROTOCOL_TYPE_DATAGRAM3, MakeDatagram3 (sender.identity->GetIdentHash (), PAYLOAD));
	assert (hashCalls == 1);

	assert (receiver.Reset ());
	assert (hashReceiver.Reset ());
}

void TestRawProtocolRefusal ()
{
	auto fixture = MakeFixture ();
	int calls = 0;
	auto receiver = [&] (uint8_t, uint16_t, uint16_t, const uint8_t *, size_t) { ++calls; };
	// Streaming and the typed datagram protocols are consumed by their own
	// handlers, so a raw registration for them is refused.
	assert (!fixture.destination->SetRawReceiver (receiver, 9000, i2p::client::PROTOCOL_TYPE_STREAMING));
	assert (!fixture.destination->SetRawReceiver (receiver, 9000, i2p::client::PROTOCOL_TYPE_DATAGRAM));
	assert (!fixture.destination->SetRawReceiver (receiver, 9000, i2p::client::PROTOCOL_TYPE_DATAGRAM2));
	assert (!fixture.destination->SetRawReceiver (receiver, 9000, i2p::client::PROTOCOL_TYPE_DATAGRAM3));

	// The payload protocols remain registrable.
	auto raw = fixture.destination->SetRawReceiver (receiver, 9000, i2p::client::PROTOCOL_TYPE_RAW);
	auto arbitrary = fixture.destination->SetRawReceiver (receiver, 9000, 33);
	assert (raw && arbitrary);
	assert (raw.Reset ());
	assert (arbitrary.Reset ());
	assert (calls == 0);
}

void TestUnspecifiedProtocolNormalization ()
{
	auto fixture = MakeFixture ();
	uint8_t observed = 0xaa;
	int calls = 0;
	auto receiver = fixture.destination->SetRawReceiver (
		[&] (uint8_t protocol, uint16_t, uint16_t, const uint8_t * buf, size_t len)
		{
			observed = protocol;
			++calls;
			assert (len == PAYLOAD.size ());
			assert (std::memcmp (buf, PAYLOAD.data (), PAYLOAD.size ()) == 0);
		},
		9000);

	// The OS-level "unspecified" value 0xff is normalized to
	// PROTO_UNSPECIFIED (0) before the raw receiver is selected.
	Dispatch (fixture, 9000, 0xff, PAYLOAD);
	assert (calls == 1);
	assert (observed == 0);
	assert (receiver.Reset ());

	// A registration on the literal 0xff value can never be reached, so
	// the normalization does not create a second catch-all.
	auto literal = MakeFixture ();
	int literalCalls = 0;
	auto literalReceiver = literal.destination->SetRawReceiver (
		[&] (uint8_t, uint16_t, uint16_t, const uint8_t *, size_t) { ++literalCalls; }, 9001, 0xff);
	Dispatch (literal, 9001, 0xff, PAYLOAD);
	assert (literalCalls == 0);
	assert (literalReceiver.Reset ());
}
}

int main ()
{
	TestProtocolKeyedDispatch ();
	TestStyleIsolation ();
	TestCatchAllFallback ();
	TestNoPortPromotion ();
	TestRawProtocolRefusal ();
	TestUnspecifiedProtocolNormalization ();
	return 0;
}

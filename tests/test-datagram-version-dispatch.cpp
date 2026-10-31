/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: test per-send datagram version dispatch.
 *
 * Verifies that the framing version is chosen per send: a destination
 * created for Datagram1 must emit Datagram3 framing when the caller
 * asks for it, an unset version keeps the session's version, and the
 * session version is not frozen by an explicit request. The built I2NP
 * Data message is inspected through the DatagramSession send observer,
 * so no routing path, LeaseSet or socket is involved.
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
#include "I2PEndian.h"
#include "Identity.h"
#include "I2NPProtocol.h"

namespace
{
struct FramedMessage
{
	uint16_t fromPort = 0;
	uint16_t toPort = 0;
	uint8_t protocol = 0;
	std::vector<uint8_t> content;
	bool valid = false;
};

// Decode one I2NP Data message built by
// DatagramDestination::CreateDataMessage for a destination created
// without compression: 4-byte length, then a stored gzip member whose
// MTIME/OS bytes carry the ports and protocol and whose stored block
// starts at offset 15.
FramedMessage Decode (const i2p::I2NPMessage & msg)
{
	FramedMessage framed;
	if (msg.GetTypeID () != i2p::eI2NPData) return framed;
	const uint8_t * payload = msg.GetPayload ();
	const size_t len = msg.GetPayloadLength ();
	if (len < 4 + 23) return framed;
	const size_t dataLen = bufbe32toh (payload);
	if (dataLen != len - 4) return framed;
	const uint8_t * member = payload + 4;
	if (member[10] != 0x01) return framed; // stored block only
	framed.fromPort = bufbe16toh (member + 4);
	framed.toPort = bufbe16toh (member + 6);
	framed.protocol = member[9];
	const size_t contentLen = bufle16toh (member + 11);
	if (contentLen + 23 > dataLen) return framed;
	framed.content.assign (member + 15, member + 15 + contentLen);
	framed.valid = true;
	return framed;
}

struct Fixture
{
	i2p::data::PrivateKeys keys;
	std::shared_ptr<i2p::client::ClientDestination> owner;
	std::shared_ptr<i2p::datagram::DatagramDestination> destination;
	i2p::data::IdentHash remote;
	std::shared_ptr<i2p::datagram::DatagramSession> session;
	std::vector<FramedMessage> sent;
};

Fixture MakeFixture ()
{
	static boost::asio::io_context service;
	Fixture fixture;
	fixture.keys = i2p::data::PrivateKeys::CreateRandomKeys (
		i2p::data::SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519, i2p::data::CRYPTO_KEY_TYPE_ECIES_X25519_AEAD, true);
	fixture.owner = std::make_shared<i2p::client::ClientDestination> (service, fixture.keys, false);
	// The destination is created for Datagram1, exactly like the UDP
	// tunnel
	// and the torrent code do.
	fixture.destination
		= std::make_shared<i2p::datagram::DatagramDestination> (fixture.owner, false, i2p::datagram::eDatagramV1);
	auto remoteKeys = i2p::data::PrivateKeys::CreateRandomKeys (
		i2p::data::SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519, i2p::data::CRYPTO_KEY_TYPE_ECIES_X25519_AEAD, true);
	fixture.remote = remoteKeys.GetPublic ()->GetIdentHash ();
	fixture.session = fixture.destination->GetSession (fixture.remote);
	return fixture;
}

void Observe (Fixture & fixture)
{
	// Capture the fixture that outlives this call; the observer is removed
	// before the fixture is released.
	fixture.session->SetSendObserverForTesting (
		[&fixture] (const i2p::I2NPMessage & msg) { fixture.sent.push_back (Decode (msg)); });
}

void TestPerSendVersion ()
{
	auto fixture = MakeFixture ();
	Observe (fixture);
	assert (fixture.session);
	assert (fixture.session->GetVersion () == i2p::datagram::eDatagramV1);

	const std::vector<uint8_t> payload { 0x01, 0x02, 0x03, 0x04, 0x05 };
	const auto remote = fixture.remote;
	const auto * identityHash = fixture.keys.GetPublic ()->GetIdentHash ().data ();

	// A DATAGRAM3 SAM subsession must send Datagram3 framing even though
	// the shared destination was created for Datagram1.
	fixture.destination->SendDatagramTo (
		payload.data (), payload.size (), remote, 111, 222, i2p::datagram::eDatagramV3);
	assert (fixture.sent.size () == 1);
	assert (fixture.sent[0].valid);
	assert (fixture.sent[0].protocol == i2p::client::PROTOCOL_TYPE_DATAGRAM3);
	assert (fixture.sent[0].fromPort == 111 && fixture.sent[0].toPort == 222);
	assert (fixture.sent[0].content.size () == 32 + 2 + payload.size ());
	assert (std::memcmp (fixture.sent[0].content.data (), identityHash, 32) == 0);
	assert (fixture.sent[0].content[32] == 0x00 && fixture.sent[0].content[33] == 0x03);
	assert (std::memcmp (fixture.sent[0].content.data () + 34, payload.data (), payload.size ()) == 0);
	// An explicit request must not change the session's version.
	assert (fixture.session->GetVersion () == i2p::datagram::eDatagramV1);

	// eDatagramDefault keeps the session's version (Datagram1 here):
	// identity || signature || payload.
	fixture.destination->SendDatagramTo (payload.data (), payload.size (), remote, 7, 8);
	assert (fixture.sent.size () == 2);
	const size_t identityLen = fixture.keys.GetPublic ()->GetFullLen ();
	const size_t signatureLen = fixture.keys.GetSignatureLen ();
	std::vector<uint8_t> identityBuffer (identityLen);
	const auto identityBufferLen = fixture.keys.GetPublic ()->ToBuffer (identityBuffer.data (), identityBuffer.size ());
	assert (identityBufferLen == identityLen);
	assert (fixture.sent[1].valid);
	assert (fixture.sent[1].protocol == i2p::client::PROTOCOL_TYPE_DATAGRAM);
	assert (fixture.sent[1].fromPort == 7 && fixture.sent[1].toPort == 8);
	assert (fixture.sent[1].content.size () == identityLen + signatureLen + payload.size ());
	assert (std::memcmp (fixture.sent[1].content.data (), identityBuffer.data (), identityLen) == 0);
	assert (std::memcmp (fixture.sent[1].content.data () + identityLen + signatureLen, payload.data (), payload.size ())
		== 0);
	// The published signature is not the signer's all-zero failure value.
	bool nonZeroSignature = false;
	for (size_t i = 0; i < signatureLen; ++i)
		nonZeroSignature = nonZeroSignature || fixture.sent[1].content[identityLen + i] != 0;
	assert (nonZeroSignature);

	// An explicit Datagram2 request wins over the V1 default.
	fixture.destination->SendDatagramTo (payload.data (), payload.size (), remote, 0, 0, i2p::datagram::eDatagramV2);
	assert (fixture.sent.size () == 3);
	assert (fixture.sent[2].valid);
	assert (fixture.sent[2].protocol == i2p::client::PROTOCOL_TYPE_DATAGRAM2);
	assert (fixture.sent[2].content.size () == identityLen + 2 + payload.size () + signatureLen);
	assert (fixture.sent[2].content[identityLen] == 0x00 && fixture.sent[2].content[identityLen + 1] == 0x02);
	assert (std::memcmp (fixture.sent[2].content.data () + identityLen + 2, payload.data (), payload.size ()) == 0);

	// A later default send is Datagram1 again: the request is per message.
	fixture.destination->SendDatagramTo (payload.data (), payload.size (), remote);
	assert (fixture.sent.size () == 4);
	assert (fixture.sent[3].protocol == i2p::client::PROTOCOL_TYPE_DATAGRAM);

	// An unsupported explicit version is logged and dropped, not framed.
	fixture.destination->SendDatagramTo (
		payload.data (), payload.size (), remote, 0, 0, static_cast<i2p::datagram::DatagramVersion> (9));
	assert (fixture.sent.size () == 4);

	fixture.session->SetSendObserverForTesting (nullptr);
}

void TestSessionVersionSwitch ()
{
	auto fixture = MakeFixture ();
	Observe (fixture);
	const std::vector<uint8_t> payload { 0x2a };
	const auto remote = fixture.remote;

	fixture.session->SetVersion (i2p::datagram::eDatagramV3);
	fixture.destination->SendDatagramTo (payload.data (), payload.size (), remote);
	assert (fixture.sent.size () == 1);
	assert (fixture.sent[0].protocol == i2p::client::PROTOCOL_TYPE_DATAGRAM3);

	// An explicit Datagram1 request overrides the session's V3 version.
	fixture.destination->SendDatagramTo (payload.data (), payload.size (), remote, 0, 0, i2p::datagram::eDatagramV1);
	assert (fixture.sent.size () == 2);
	assert (fixture.sent[1].protocol == i2p::client::PROTOCOL_TYPE_DATAGRAM);

	fixture.session->SetSendObserverForTesting (nullptr);
}

void TestVersionHelpers ()
{
	using namespace i2p::datagram;
	// An unset request keeps the session's version.
	assert (ResolveDatagramVersion (eDatagramDefault, eDatagramV1) == eDatagramV1);
	assert (ResolveDatagramVersion (eDatagramDefault, eDatagramV2) == eDatagramV2);
	assert (ResolveDatagramVersion (eDatagramDefault, eDatagramV3) == eDatagramV3);
	// An explicit request wins.
	assert (ResolveDatagramVersion (eDatagramV3, eDatagramV1) == eDatagramV3);
	assert (ResolveDatagramVersion (eDatagramV1, eDatagramV3) == eDatagramV1);

	assert (IsDatagramVersionSupported (eDatagramV1));
	assert (IsDatagramVersionSupported (eDatagramV2));
	assert (IsDatagramVersionSupported (eDatagramV3));
	assert (!IsDatagramVersionSupported (eDatagramDefault));
	assert (!IsDatagramVersionSupported (static_cast<DatagramVersion> (4)));
}
}

int main ()
{
	TestPerSendVersion ();
	TestSessionVersionSwitch ();
	TestVersionHelpers ();
	return 0;
}

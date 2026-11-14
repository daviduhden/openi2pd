/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * Pins LeaseSet1 session provenance before client cache publication.
 */

#include <cassert>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

#include <boost/asio.hpp>

#include "Destination.h"
#include "ECIESX25519AEADRatchetSession.h"
#include "I2NPProtocol.h"
#include "I2PEndian.h"
#include "Identity.h"
#include "LeaseSet.h"
#include "Timestamp.h"

namespace
{
class DestinationFixture : public i2p::client::ClientDestination
{
public:
	using ClientDestination::ClientDestination;
	void Store (const std::vector<uint8_t> & payload, i2p::garlic::ECIESX25519AEADRatchetSession * session)
	{
		assert (HandleCloveI2NPMessage (i2p::eI2NPDatabaseStore, payload.data (), payload.size (), 1, session));
	}
};

std::vector<uint8_t> MakeStore (const i2p::data::PrivateKeys & keys, uint64_t expiration)
{
	auto identity = keys.GetPublic ();
	std::vector<uint8_t> leaseSet (identity->GetFullLen ());
	assert (identity->ToBuffer (leaseSet.data (), leaseSet.size ()) == leaseSet.size ());
	const auto * encryptionKey = identity->GetEncryptionPublicKey ();
	leaseSet.insert (leaseSet.end (), encryptionKey, encryptionKey + 256);
	leaseSet.resize (leaseSet.size () + identity->GetSigningPublicKeyLen (), 0);
	leaseSet.push_back (1);
	leaseSet.resize (leaseSet.size () + i2p::data::IdentHash::len, 0x42);
	auto offset = leaseSet.size ();
	leaseSet.resize (offset + 12);
	htobe32buf (leaseSet.data () + offset, 37);
	htobe64buf (leaseSet.data () + offset + 4, expiration);
	std::vector<uint8_t> signature (keys.GetSignatureLen ());
	keys.Sign (leaseSet.data (), static_cast<int> (leaseSet.size ()), signature.data ());
	leaseSet.insert (leaseSet.end (), signature.begin (), signature.end ());
	i2p::data::LeaseSet parsed (leaseSet.data (), leaseSet.size ());
	assert (parsed.IsValid ());

	std::vector<uint8_t> payload (i2p::DATABASE_STORE_HEADER_SIZE, 0);
	std::memcpy (
		payload.data () + i2p::DATABASE_STORE_KEY_OFFSET, identity->GetIdentHash ().data (), i2p::data::IdentHash::len);
	payload[i2p::DATABASE_STORE_TYPE_OFFSET] = i2p::data::NETDB_STORE_TYPE_LEASESET;
	payload.insert (payload.end (), leaseSet.begin (), leaseSet.end ());
	return payload;
}
}

int main ()
{
	using namespace i2p::data;
	boost::asio::io_context service;
	auto localKeys
		= PrivateKeys::CreateRandomKeys (SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519, CRYPTO_KEY_TYPE_ELGAMAL, true);
	auto remoteKeys
		= PrivateKeys::CreateRandomKeys (SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519, CRYPTO_KEY_TYPE_ELGAMAL, true);
	auto destination = std::make_shared<DestinationFixture> (service, localKeys, false);
	auto session = std::make_shared<i2p::garlic::ECIESX25519AEADRatchetSession> (destination.get (), false);
	const auto & remote = remoteKeys.GetPublic ()->GetIdentHash ();
	const auto expires = i2p::util::GetMillisecondsSinceEpoch () + 300000;
	auto payload = MakeStore (remoteKeys, expires);

	// Even a signature-valid LS1 cannot identify an ECIES session peer.
	destination->Store (payload, session.get ());
	assert (!destination->FindLeaseSet (remote));
	assert (!session->GetDestinationPtr ());

	// Ordinary LS1 delivery remains accepted for an ElGamal destination.
	destination->Store (payload, nullptr);
	auto cached = destination->FindLeaseSet (remote);
	assert (cached && cached->IsValid () && !cached->IsIncompatibleCrypto ());

	// A newer authenticated-session advertisement cannot replace it,
	// including when future DatabaseStore option bits are set.
	auto newer = MakeStore (remoteKeys, expires + 1000);
	newer[i2p::DATABASE_STORE_TYPE_OFFSET] |= 0x80;
	destination->Store (newer, session.get ());
	assert (destination->FindLeaseSet (remote) == cached);
	assert (!session->GetDestinationPtr ());
	return 0;
}

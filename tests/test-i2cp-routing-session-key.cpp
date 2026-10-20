/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: test i2cp routing session key.
 *
 * Verifies that the I2CP garlic routing-session cache is keyed by the parsed
 * wire identity hash for every signature/crypto type, and documents that the
 * former fixed signing-key slice could be reproduced by a crafted identity so
 * that another destination's cached session was selected. No router or network
 * is started.
 *
 */

#include <cassert>
#include <vector>

#include "I2CP.h"
#include "Identity.h"

namespace
{
using namespace i2p::client;

std::shared_ptr<const i2p::data::IdentityEx> MakeIdentity (
	i2p::data::SigningKeyType signing, i2p::data::CryptoKeyType crypto)
{
	auto keys = i2p::data::PrivateKeys::CreateRandomKeys (signing, crypto, true);
	return keys.GetPublic ();
}

std::vector<uint8_t> WireIdentity (const std::shared_ptr<const i2p::data::IdentityEx> & identity)
{
	std::vector<uint8_t> wire (identity->GetFullLen ());
	assert (wire.size () >= i2p::data::DEFAULT_IDENTITY_SIZE);
	assert (identity->ToBuffer (wire.data (), wire.size ()) == wire.size ());
	return wire;
}

void TestKeyMatchesIdentityHash ()
{
	auto identity
		= MakeIdentity (i2p::data::SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519, i2p::data::CRYPTO_KEY_TYPE_ECIES_X25519_AEAD);
	auto wire = WireIdentity (identity);
	auto key = GetI2CPRoutingSessionKey (wire.data (), wire.size ());
	// Same value IdentityEx derives (and LeaseSet::GetIdentHash() exposes), so
	// inserting after a send and looking up by the wire identity agree.
	assert (key == identity->GetIdentHash ());

	// Defensive: no identity bytes yields the zero key, never the previous
	// peer's session.
	auto empty = GetI2CPRoutingSessionKey (nullptr, 0);
	assert (empty.IsZero ());
	assert (empty != key);
}

void TestFormerSliceIsCraftable ()
{
	auto identity
		= MakeIdentity (i2p::data::SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519, i2p::data::CRYPTO_KEY_TYPE_ECIES_X25519_AEAD);
	auto wire = WireIdentity (identity);
	auto key = GetI2CPRoutingSessionKey (wire.data (), wire.size ());
	assert (key == identity->GetIdentHash ());

	// The former insert key was signingKey + 96 and the former lookup key was
	// wire identity + DEFAULT_IDENTITY_SIZE - 35; both are identity offset 352,
	// i.e. the last 32 bytes of the fixed 128-byte signing-key slot. That is
	// not the identity hash for any signature type.
	i2p::data::IdentHash formerInsert (identity->GetStandardIdentity ().signingKey + 96);
	i2p::data::IdentHash formerLookup (wire.data () + i2p::data::DEFAULT_IDENTITY_SIZE - 35);
	assert (formerInsert == formerLookup);
	assert (formerInsert != key);

	// A different, still-parseable destination that preserves those 32 bytes
	// (only a public-key byte changes) reproduces the old cache key while
	// having its own identity hash: the former lookup would have selected the
	// original destination's routing session.
	auto forged = wire;
	forged[0] ^= 0x01; // encryption public key byte, outside the old slice
	i2p::data::IdentityEx forgedIdentity (forged.data (), forged.size ());
	assert (forgedIdentity.IsValid ());
	assert (forgedIdentity.GetFullLen () == wire.size ());
	assert (forgedIdentity.GetIdentHash () != identity->GetIdentHash ());

	i2p::data::IdentHash forgedFormer (forged.data () + i2p::data::DEFAULT_IDENTITY_SIZE - 35);
	assert (forgedFormer == formerInsert);

	// The identity-hash key keeps the forged and the original destination
	// apart, so no session can be reused across them.
	auto forgedKey = GetI2CPRoutingSessionKey (forged.data (), forged.size ());
	assert (forgedKey == forgedIdentity.GetIdentHash ());
	assert (forgedKey != key);
}

void TestOtherSignatureTypes ()
{
	// DSA and ECDSA destinations also key by the full identity hash; the
	// former slice is a key fragment rather than the identity hash.
	auto dsa = MakeIdentity (i2p::data::SIGNING_KEY_TYPE_DSA_SHA1, i2p::data::CRYPTO_KEY_TYPE_ELGAMAL);
	auto dsaWire = WireIdentity (dsa);
	auto dsaKey = GetI2CPRoutingSessionKey (dsaWire.data (), dsaWire.size ());
	assert (dsaKey == dsa->GetIdentHash ());
	assert (i2p::data::IdentHash (dsa->GetStandardIdentity ().signingKey + 96) != dsaKey);

	auto ecdsa
		= MakeIdentity (i2p::data::SIGNING_KEY_TYPE_ECDSA_SHA256_P256, i2p::data::CRYPTO_KEY_TYPE_ECIES_X25519_AEAD);
	auto ecdsaWire = WireIdentity (ecdsa);
	auto ecdsaKey = GetI2CPRoutingSessionKey (ecdsaWire.data (), ecdsaWire.size ());
	assert (ecdsaKey == ecdsa->GetIdentHash ());
	assert (i2p::data::IdentHash (ecdsa->GetStandardIdentity ().signingKey + 96) != ecdsaKey);

	// A truncated identity must not silently produce another peer's key.
	auto truncated = GetI2CPRoutingSessionKey (ecdsaWire.data (), ecdsaWire.size () / 2);
	assert (truncated != ecdsaKey);
}
} // namespace

int main ()
{
	TestKeyMatchesIdentityHash ();
	TestFormerSliceIsCraftable ();
	TestOtherSignatureTypes ();
	return 0;
}

/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: test blinding.
 *
 * Round-trips Ed25519 and RedDSA destination blinding: the public key
 * derived from the blinded private key must match GenerateAlpha(), and
 * a blinded signature must verify.
 *
 */

#include <cassert>
#include <memory>
#include <string.h>
#include "Blinding.h"
#include "Identity.h"
#include "Timestamp.h"

using namespace i2p::data;
using namespace i2p::util;
using namespace i2p::crypto;

void BlindTest (SigningKeyType sigType)
{
	auto keys = PrivateKeys::CreateRandomKeys (sigType);
	BlindedPublicKey blindedKey (keys.GetPublic ());
	auto timestamp = GetSecondsSinceEpoch ();
	char date[9];
	GetDateString (timestamp, date);
	uint8_t blindedPriv[32], blindedPub[32];
	auto publicKeyLen = blindedKey.BlindPrivateKey (keys.GetSigningPrivateKey (), date, blindedPriv, blindedPub);
	uint8_t blindedPub1[32];
	blindedKey.GetBlindedKey (date, blindedPub1);
	// check if public key produced from private blinded key matches blided
	// public key
	assert (!memcmp (blindedPub, blindedPub1, publicKeyLen));
	// try to sign and verify
	std::unique_ptr<Signer> blindedSigner (PrivateKeys::CreateSigner (blindedKey.GetBlindedSigType (), blindedPriv));
	uint8_t buf[100], signature[64];
	memset (buf, 1, 100);
	blindedSigner->Sign (buf, 100, signature);
	std::unique_ptr<Verifier> blindedVerifier (IdentityEx::CreateVerifier (blindedKey.GetBlindedSigType ()));
	blindedVerifier->SetPublicKey (blindedPub);
	assert (blindedVerifier->Verify (buf, 100, signature));
}

int main ()
{
	// EdDSA test
	BlindTest (SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519);
	// RedDSA test
	BlindTest (SIGNING_KEY_TYPE_REDDSA_SHA512_ED25519);
}

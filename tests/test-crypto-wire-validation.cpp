/*
 * Copyright (c) 2026, David Uhden Collado
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Regression checks for untrusted key/signature encodings:
 *   - Key certificates shorter than the four-byte header are rejected.
 *   - b33 flags outside proposal 149 and a non-RedDSA blinded type are rejected.
 *   - The GOST R 34.10 verifier rejects the all-zero signature and points that
 *     are not on the curve.
 *
 * These are pure in-process checks; no network, key files or router state are
 * required.
 */

#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

#include "Base.h"
#include "Blinding.h"
#include "Gost.h"
#include "I2PEndian.h"
#include "Identity.h"
#include "Signature.h"

namespace
{
uint32_t Crc32 (const uint8_t * data, size_t len)
{
	uint32_t crc = 0xFFFFFFFFu;
	for (size_t i = 0; i < len; ++i)
	{
		crc ^= data[i];
		for (int bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
	}
	return ~crc;
}

void TestKeyCertificateLength ()
{
	using namespace i2p::data;

	std::array<uint8_t, DEFAULT_IDENTITY_SIZE + 4> buffer { };
	buffer[DEFAULT_IDENTITY_SIZE - 3] = CERTIFICATE_TYPE_KEY;

	// A KEY certificate always contains the 2-byte signature type and the
	// 2-byte crypto type. Java's KeyCertificate rejects shorter payloads.
	for (uint16_t length : { 0, 1, 2, 3 })
	{
		htobe16buf (buffer.data () + DEFAULT_IDENTITY_SIZE - 2, length);
		IdentityEx identity;
		assert (identity.FromBuffer (buffer.data (), buffer.size ()) == 0);
	}

	// A four-byte key certificate is valid (DSA-SHA1, ElGamal here).
	htobe16buf (buffer.data () + DEFAULT_IDENTITY_SIZE - 2, 4);
	IdentityEx dsa;
	assert (dsa.FromBuffer (buffer.data (), buffer.size ()) == DEFAULT_IDENTITY_SIZE + 4);
	assert (dsa.GetSigningKeyType () == SIGNING_KEY_TYPE_DSA_SHA1);
	assert (dsa.GetCryptoKeyType () == CRYPTO_KEY_TYPE_ELGAMAL);
}

std::string ReencodeB33 (const std::string & b33, const std::function<void (uint8_t *)> & mutate)
{
	std::array<uint8_t, 64> raw { };
	size_t len = i2p::data::Base32ToByteStream (b33, raw.data (), raw.size ());
	assert (len == 35);

	uint32_t checksum = Crc32 (raw.data () + 3, len - 3);
	raw[0] ^= checksum;
	raw[1] ^= checksum >> 8;
	raw[2] ^= checksum >> 16;

	mutate (raw.data ());

	checksum = Crc32 (raw.data () + 3, len - 3);
	raw[0] ^= checksum;
	raw[1] ^= checksum >> 8;
	raw[2] ^= checksum >> 16;
	return i2p::data::ByteStreamToBase32 (raw.data (), len);
}

void TestB33FlagsAndBlindedType ()
{
	using namespace i2p::data;

	auto keys = PrivateKeys::CreateRandomKeys (SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519);
	BlindedPublicKey original (keys.GetPublic ());
	assert (original.IsValid ());
	const std::string b33 = original.ToB33 ();
	assert (!b33.empty ());

	BlindedPublicKey unchanged (b33);
	assert (unchanged.IsValid ());

	// Proposal 149 only defines flags 0x01, 0x02 and 0x04. Java rejects any
	// bit in 0xf8 instead of silently using a different representation.
	const std::string unknownFlag = ReencodeB33 (b33, [] (uint8_t * raw) { raw[0] |= 0x08; });
	BlindedPublicKey unknown (unknownFlag);
	assert (!unknown.IsValid ());
	assert (unknown.ToB33 ().empty ());

	// Proposal 149 requires a RedDSA (11) blinded key. Accepting another type
	// produced a store hash that no Java peer would look up.
	const std::string wrongBlindedType = ReencodeB33 (b33, [] (uint8_t * raw) { raw[2] = 7; });
	BlindedPublicKey wrongType (wrongBlindedType);
	assert (!wrongType.IsValid ());
	assert (wrongType.ToB33 ().empty ());
}

void TestEd25519phGate ()
{
	using namespace i2p::data;

	auto keys = PrivateKeys::CreateRandomKeys (SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519ph);
	assert (keys.GetPublic ());
	assert (keys.GetPublic ()->GetSigningKeyType () == SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519ph);
	assert (keys.GetPublic ()->GetSigningPublicKeyLen () == i2p::crypto::EDDSA25519_PUBLIC_KEY_LENGTH);
	assert (keys.GetPublic ()->GetSigningPrivateKeyLen () == i2p::crypto::EDDSA25519_PRIVATE_KEY_LENGTH);
	assert (keys.GetPublic ()->GetSignatureLen () == i2p::crypto::EDDSA25519_SIGNATURE_LENGTH);

	uint8_t message[32];
	for (size_t i = 0; i < sizeof (message); ++i) message[i] = (uint8_t)i;
	uint8_t signature[i2p::crypto::EDDSA25519_SIGNATURE_LENGTH] = { };
	keys.Sign (message, sizeof (message), signature);
	assert (keys.GetPublic ()->Verify (message, sizeof (message), signature));

	// A changed message must not verify under the same Ed25519ph signature.
	message[0] ^= 1;
	assert (!keys.GetPublic ()->Verify (message, sizeof (message), signature));
}

void TestGostSignatureValidation ()
{
	using namespace i2p::crypto;

	const uint8_t message[32] = { 0x54, 0x65, 0x73, 0x74, 0x20, 0x47, 0x4F, 0x53, 0x54, 0x20, 0x33, 0x34, 0x2E, 0x31,
		0x30, 0x20, 0x73, 0x69, 0x67, 0x6E, 0x61, 0x74, 0x75, 0x72, 0x65, 0x20, 0x63, 0x68, 0x65, 0x63, 0x6B, 0x21 };

	uint8_t priv[64], pub[128], signature[128];
	CreateGOSTR3410RandomKeys (eGOSTR3410TC26A512, priv, pub);
	GOSTR3410_512_Signer signer (eGOSTR3410TC26A512, priv);
	signer.Sign (message, sizeof (message), signature);

	GOSTR3410_512_Verifier verifier (eGOSTR3410TC26A512);
	verifier.SetPublicKey (pub);
	assert (verifier.Verify (message, sizeof (message), signature));

	// r = s = 0 yields the point at infinity. The old verifier compared an
	// uninitialised/zero x coordinate against zero and accepted it.
	uint8_t zero[128] = { };
	assert (!verifier.Verify (message, sizeof (message), zero));

	uint8_t rZero[128];
	memcpy (rZero, signature, sizeof (rZero));
	memset (rZero, 0, 64);
	assert (!verifier.Verify (message, sizeof (message), rZero));

	uint8_t sZero[128];
	memcpy (sZero, signature, sizeof (sZero));
	memset (sZero + 64, 0, 64);
	assert (!verifier.Verify (message, sizeof (message), sZero));

	// A public key that is not a finite point on the curve must not verify.
	GOSTR3410_512_Verifier invalidVerifier (eGOSTR3410TC26A512);
	uint8_t invalidPublicKey[128] = { };
	invalidVerifier.SetPublicKey (invalidPublicKey);
	assert (!invalidVerifier.Verify (message, sizeof (message), signature));
}
}

int main ()
{
	TestKeyCertificateLength ();
	TestB33FlagsAndBlindedType ();
	TestEd25519phGate ();
	TestGostSignatureValidation ();
	return 0;
}

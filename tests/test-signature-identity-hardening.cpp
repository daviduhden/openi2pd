/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: test signature identity hardening.
 *
 * Checks identity and signature encodings: P-521 certificate sizes and DER bounds, Ed25519 excess-data rejection, malformed identities, RSA wire lengths and private-key round trips, all by in-process verification.
 *
 */

#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <vector>

#include "I2PEndian.h"
#include "Identity.h"
#include "Signature.h"

namespace
{
const uint8_t TEST_MESSAGE[37] = { 0x49, 0x32, 0x50, 0x20, 0x69, 0x64, 0x65, 0x6e, 0x74, 0x69, 0x74, 0x79, 0x20, 0x68,
	0x61, 0x72, 0x64, 0x65, 0x6e, 0x69, 0x6e, 0x67, 0x20, 0x76, 0x65, 0x63, 0x74, 0x6f, 0x72, 0x20, 0x62, 0x79, 0x74,
	0x65, 0x73, 0x21, 0x21 };

void PutCertificate (uint8_t * buf, uint8_t type, uint16_t payloadLen)
{
	buf[i2p::data::DEFAULT_IDENTITY_SIZE - 3] = type;
	htobe16buf (buf + i2p::data::DEFAULT_IDENTITY_SIZE - 2, payloadLen);
}

void TestP521ExcessSigningKey ()
{
	using namespace i2p::data;
	using namespace i2p::crypto;

	uint8_t priv[ECDSAP521_KEY_LENGTH / 2];
	uint8_t pub[ECDSAP521_KEY_LENGTH];
	assert (CreateECDSAP521RandomKeys (priv, pub));

	// 12-byte key certificate: 4-byte header, 4 bytes of P-521 excess
	// signing key and 4 unused crypto-key bytes. The extended part therefore
	// lives in m_ExtendedBufferPtr rather than the 8-byte inline buffer.
	std::array<uint8_t, DEFAULT_IDENTITY_SIZE + 12> buffer { };
	memcpy (buffer.data () + 256, pub, 128);
	PutCertificate (buffer.data (), CERTIFICATE_TYPE_KEY, 12);
	htobe16buf (buffer.data () + DEFAULT_IDENTITY_SIZE, SIGNING_KEY_TYPE_ECDSA_SHA512_P521);
	htobe16buf (buffer.data () + DEFAULT_IDENTITY_SIZE + 2, CRYPTO_KEY_TYPE_ECIES_X25519_AEAD);
	memcpy (buffer.data () + DEFAULT_IDENTITY_SIZE + 4, pub + 128, 4);

	IdentityEx identity (buffer.data (), buffer.size ());
	assert (identity.IsValid ());
	assert (identity.GetFullLen () == buffer.size ());
	assert (identity.GetSigningKeyType () == SIGNING_KEY_TYPE_ECDSA_SHA512_P521);
	assert (identity.GetCryptoKeyType () == CRYPTO_KEY_TYPE_ECIES_X25519_AEAD);
	assert (identity.GetSigningPublicKeyLen () == ECDSAP521_KEY_LENGTH);
	assert (identity.GetSignatureLen () == ECDSAP521_KEY_LENGTH);

	uint8_t signature[ECDSAP521_KEY_LENGTH];
	ECDSAP521Signer signer (priv);
	signer.Sign (TEST_MESSAGE, sizeof (TEST_MESSAGE), signature);
	assert (identity.Verify (TEST_MESSAGE, sizeof (TEST_MESSAGE), signature));

	// The old reader accepted five-byte P-521 certificates and copied four
	// excess bytes from an uninitialised part of the inline buffer.
	std::array<uint8_t, DEFAULT_IDENTITY_SIZE + 5> truncated { };
	memcpy (truncated.data () + 256, pub, 128);
	PutCertificate (truncated.data (), CERTIFICATE_TYPE_KEY, 5);
	htobe16buf (truncated.data () + DEFAULT_IDENTITY_SIZE, SIGNING_KEY_TYPE_ECDSA_SHA512_P521);
	htobe16buf (truncated.data () + DEFAULT_IDENTITY_SIZE + 2, CRYPTO_KEY_TYPE_ELGAMAL);
	htobe16buf (truncated.data () + DEFAULT_IDENTITY_SIZE + 4, pub[128]);

	IdentityEx incomplete (truncated.data (), truncated.size ());
	assert (!incomplete.IsValid ());
	assert (incomplete.GetFullLen () == 0);
	assert (!incomplete.Verify (TEST_MESSAGE, sizeof (TEST_MESSAGE), signature));
}

void TestP521DerSignatureBounds ()
{
	using namespace i2p::crypto;

	uint8_t priv[ECDSAP521_KEY_LENGTH / 2];
	uint8_t pub[ECDSAP521_KEY_LENGTH];
	assert (CreateECDSAP521RandomKeys (priv, pub));

	ECDSAP521Verifier verifier;
	verifier.SetPublicKey (pub);

	// Two high-bit 66-byte integers encode to a 141-byte DER SEQUENCE.
	// The old fixed keyLen + 8 (= 140) buffer was written past its end.
	uint8_t oversized[ECDSAP521_KEY_LENGTH];
	memset (oversized, 0xFF, sizeof (oversized));
	assert (!verifier.Verify (TEST_MESSAGE, sizeof (TEST_MESSAGE), oversized));

	// The exact-size encoder must still accept a valid signature.
	ECDSAP521Signer signer (priv);
	uint8_t signature[ECDSAP521_KEY_LENGTH] = { };
	signer.Sign (TEST_MESSAGE, sizeof (TEST_MESSAGE), signature);
	assert (verifier.Verify (TEST_MESSAGE, sizeof (TEST_MESSAGE), signature));
	assert (!verifier.Verify (TEST_MESSAGE, sizeof (TEST_MESSAGE) - 1, signature));
}

void TestKeyCertificateExcessData ()
{
	using namespace i2p::data;

	// Ed25519 requires no excess signing data. Java's
	// SigningPublicKey.toTypedKey() rejects excess data when the crypto type
	// is ElGamal; accepting it would make two different identities hash the
	// same key layout ambiguously.
	std::array<uint8_t, DEFAULT_IDENTITY_SIZE + 5> excess { };
	PutCertificate (excess.data (), CERTIFICATE_TYPE_KEY, 5);
	htobe16buf (excess.data () + DEFAULT_IDENTITY_SIZE, SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519);
	htobe16buf (excess.data () + DEFAULT_IDENTITY_SIZE + 2, CRYPTO_KEY_TYPE_ELGAMAL);

	IdentityEx rejected (excess.data (), excess.size ());
	assert (!rejected.IsValid ());
	assert (rejected.GetFullLen () == 0);

	// The canonical four-byte certificate remains accepted.
	std::array<uint8_t, DEFAULT_IDENTITY_SIZE + 4> canonical { };
	PutCertificate (canonical.data (), CERTIFICATE_TYPE_KEY, 4);
	htobe16buf (canonical.data () + DEFAULT_IDENTITY_SIZE, SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519);
	htobe16buf (canonical.data () + DEFAULT_IDENTITY_SIZE + 2, CRYPTO_KEY_TYPE_ELGAMAL);

	IdentityEx accepted (canonical.data (), canonical.size ());
	assert (accepted.IsValid ());
	assert (accepted.GetFullLen () == canonical.size ());
	assert (accepted.GetSigningKeyType () == SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519);
	assert (accepted.GetSigningPublicKeyLen () == i2p::crypto::EDDSA25519_PUBLIC_KEY_LENGTH);
	assert (accepted.GetSigningPrivateKeyLen () == i2p::crypto::EDDSA25519_PRIVATE_KEY_LENGTH);
	assert (accepted.GetSignatureLen () == i2p::crypto::EDDSA25519_SIGNATURE_LENGTH);
}

void TestMalformedIdentityIsNotValid ()
{
	using namespace i2p::data;

	std::array<uint8_t, DEFAULT_IDENTITY_SIZE - 1> shortBuffer { };
	IdentityEx tooShort (shortBuffer.data (), shortBuffer.size ());
	assert (!tooShort.IsValid ());
	assert (tooShort.GetFullLen () == 0);

	std::array<uint8_t, DEFAULT_IDENTITY_SIZE> nullCertificate { };
	IdentityEx dsa (nullCertificate.data (), nullCertificate.size ());
	assert (dsa.IsValid ());
	assert (dsa.GetFullLen () == DEFAULT_IDENTITY_SIZE);
	assert (dsa.GetSigningKeyType () == SIGNING_KEY_TYPE_DSA_SHA1);
	assert (dsa.GetCryptoKeyType () == CRYPTO_KEY_TYPE_ELGAMAL);
}

void TestRsaWireLengths ()
{
	using namespace i2p::data;

	std::array<uint8_t, DEFAULT_IDENTITY_SIZE + 132> buffer { };
	PutCertificate (buffer.data (), CERTIFICATE_TYPE_KEY, 132);
	htobe16buf (buffer.data () + DEFAULT_IDENTITY_SIZE, SIGNING_KEY_TYPE_RSA_SHA256_2048);
	htobe16buf (buffer.data () + DEFAULT_IDENTITY_SIZE + 2, CRYPTO_KEY_TYPE_ELGAMAL);

	IdentityEx identity (buffer.data (), buffer.size ());
	assert (identity.IsValid ());
	assert (identity.GetFullLen () == buffer.size ());
	assert (identity.IsRSA ());
	assert (identity.GetSigningPublicKeyLen () == 256);
	assert (identity.GetSigningPrivateKeyLen () == 512);
	assert (identity.GetSignatureLen () == 256);
	// No RSA verifier is advertised in this build.
	assert (!identity.Verify (TEST_MESSAGE, sizeof (TEST_MESSAGE), buffer.data ()));

	uint8_t priv[512] { };
	uint8_t pub[256] { };
	assert (!PrivateKeys::GenerateSigningKeyPair (SIGNING_KEY_TYPE_RSA_SHA256_2048, priv, pub));
}

void TestPrivateKeysRoundTrip ()
{
	using namespace i2p::data;

	auto keys = PrivateKeys::CreateRandomKeys (SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519);
	assert (keys.GetPublic ());
	std::vector<uint8_t> buffer (keys.GetFullLen ());
	assert (buffer.size () > DEFAULT_IDENTITY_SIZE);
	assert (keys.ToBuffer (buffer.data (), buffer.size ()) == buffer.size ());

	PrivateKeys parsed;
	assert (parsed.FromBuffer (buffer.data (), buffer.size ()) == buffer.size ());
	assert (parsed.GetPublic ()->GetIdentHash () == keys.GetPublic ()->GetIdentHash ());

	uint8_t signature[i2p::crypto::EDDSA25519_SIGNATURE_LENGTH];
	parsed.Sign (TEST_MESSAGE, sizeof (TEST_MESSAGE), signature);
	assert (parsed.GetPublic ()->Verify (TEST_MESSAGE, sizeof (TEST_MESSAGE), signature));

	// Truncating the signing private key must fail the parse and leave no
	// half-initialised public identity behind.
	assert (parsed.FromBuffer (buffer.data (), buffer.size () - 1) == 0);
	assert (!parsed.GetPublic ());
}

void TestEddsaFallbackMatchesCompat ()
{
	using namespace i2p::crypto;

	uint8_t priv[EDDSA25519_PRIVATE_KEY_LENGTH];
	uint8_t pub[EDDSA25519_PUBLIC_KEY_LENGTH];
	assert (CreateEDDSA25519RandomKeys (priv, pub));

	// Give the provider-backed signer a public key that cannot match the one
	// derived from the private key. Both paths must therefore use the
	// compatibility signer and produce the same deterministic signature.
	uint8_t mismatched[EDDSA25519_PUBLIC_KEY_LENGTH];
	memcpy (mismatched, pub, sizeof (mismatched));
	mismatched[0] ^= 0x01;

	EDDSA25519SignerCompat compat (priv, mismatched);
	EDDSA25519Signer signer (priv, mismatched);

	uint8_t compatSignature[EDDSA25519_SIGNATURE_LENGTH] = { };
	uint8_t signerSignature[EDDSA25519_SIGNATURE_LENGTH] = { };
	compat.Sign (TEST_MESSAGE, sizeof (TEST_MESSAGE), compatSignature);
	signer.Sign (TEST_MESSAGE, sizeof (TEST_MESSAGE), signerSignature);
	assert (!memcmp (compatSignature, signerSignature, sizeof (compatSignature)));

	EDDSA25519Verifier verifier;
	verifier.SetPublicKey (compat.GetPublicKey ());
	assert (verifier.Verify (TEST_MESSAGE, sizeof (TEST_MESSAGE), signerSignature));
	assert (!verifier.Verify (TEST_MESSAGE, sizeof (TEST_MESSAGE) - 1, signerSignature));
}
} // namespace

int main ()
{
	TestP521ExcessSigningKey ();
	TestP521DerSignatureBounds ();
	TestKeyCertificateExcessData ();
	TestMalformedIdentityIsNotValid ();
	TestRsaWireLengths ();
	TestPrivateKeysRoundTrip ();
	TestEddsaFallbackMatchesCompat ();
	return 0;
}

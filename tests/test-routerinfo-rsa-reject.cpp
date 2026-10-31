/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: test routerinfo rsa reject.
 *
 * Pins the Java DatabaseEntry.verifySignature() policy that a
 * RouterInfo may not carry an RSA router identity. The RSA verifier
 * itself stays available for SU3 files and local use, so this test
 * signs the RouterInfo with a real RSA-2048 key, checks that the
 * signature verifies through the identity, and then asserts that
 * RouterInfo parsing (and update) still rejects it. A body signed with
 * an Ed25519 identity is accepted, proving the rejection is the
 * identity's signature type and not the RouterInfo body.
 *
 */

#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#include <openssl/opensslv.h>
#if !defined(LIBRESSL_VERSION_NUMBER)
#include <openssl/core_names.h>
#endif
#include <openssl/bn.h>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/rsa.h>
#include <openssl/sha.h>

#include "I2PEndian.h"
#include "Identity.h"
#include "RouterContext.h"
#include "RouterInfo.h"

namespace
{
using namespace i2p::data;

constexpr size_t RSA_BITS = 2048;
constexpr size_t RSA_BYTES = RSA_BITS / 8;
// DEFAULT_IDENTITY_SIZE carries 128 signing-key bytes; an RSA-2048
// modulus
// needs a key certificate with the remaining 128 bytes.
constexpr size_t RSA_EXCESS = RSA_BYTES - 128;

// Owns the RSA private exponent used for the raw PKCS#1 v1.5 signature.
struct RsaKey
{
	std::array<uint8_t, RSA_BYTES> modulus { };
	BIGNUM * d = nullptr;
	~RsaKey ()
	{
		if (d) BN_clear_free (d);
	}
};

void AppendParam (std::string & out, std::string_view key, std::string_view value)
{
	out.push_back (static_cast<char> (key.size ()));
	out.append (key);
	out.push_back ('=');
	out.push_back (static_cast<char> (value.size ()));
	out.append (value);
	out.push_back (';');
}

// Minimal but complete RouterInfo body: timestamp, no addresses, no
// peers and the two properties the parser requires (netId and
// router.version).
std::vector<uint8_t> BuildBody ()
{
	std::vector<uint8_t> body;
	uint8_t encoded[8];
	htobe64buf (encoded, 1700000000000ULL);
	body.insert (body.end (), encoded, encoded + sizeof (encoded));
	body.push_back (0); // number of addresses
	body.push_back (0); // peer size
	std::string properties;
	AppendParam (properties, "netId", std::to_string (i2p::context.GetNetID ()));
	AppendParam (properties, "router.version", "2.50.0");
	uint8_t size[2];
	htobe16buf (size, static_cast<uint16_t> (properties.size ()));
	body.insert (body.end (), size, size + sizeof (size));
	body.insert (body.end (), properties.begin (), properties.end ());
	return body;
}

void GenerateRsaKey (RsaKey & key)
{
#ifndef LIBRESSL_VERSION_NUMBER
	EVP_PKEY_CTX * ctx = EVP_PKEY_CTX_new_from_name (nullptr, "RSA", nullptr);
	assert (ctx);
	EVP_PKEY * pkey = nullptr;
	assert (EVP_PKEY_keygen_init (ctx) > 0);
	assert (EVP_PKEY_CTX_set_rsa_keygen_bits (ctx, static_cast<int> (RSA_BITS)) > 0);
	assert (EVP_PKEY_keygen (ctx, &pkey) > 0);
	EVP_PKEY_CTX_free (ctx);
	BIGNUM * n = nullptr;
	assert (EVP_PKEY_get_bn_param (pkey, OSSL_PKEY_PARAM_RSA_N, &n) == 1);
	assert (EVP_PKEY_get_bn_param (pkey, OSSL_PKEY_PARAM_RSA_D, &key.d) == 1);
	assert (i2p::crypto::bn2buf (n, key.modulus.data (), key.modulus.size ()));
	BN_free (n);
	EVP_PKEY_free (pkey);
#else
	RSA * rsa = RSA_new ();
	assert (rsa);
	BIGNUM * e = BN_new ();
	assert (e && BN_set_word (e, 65537) == 1);
	assert (RSA_generate_key_ex (rsa, static_cast<int> (RSA_BITS), e, nullptr) == 1);
	BN_free (e);
	const BIGNUM *n = nullptr, *pub = nullptr, *priv = nullptr;
	RSA_get0_key (rsa, &n, &pub, &priv);
	assert (n && priv);
	assert (i2p::crypto::bn2buf (n, key.modulus.data (), key.modulus.size ()));
	key.d = BN_dup (priv);
	assert (key.d);
	RSA_free (rsa);
#endif
}

// Raw PKCS#1 v1.5 signature over SHA-256(message) without a DigestInfo
// wrapper, matching the I2P RSA verifier.
void SignRawRsa (const RsaKey & key, const uint8_t * message, size_t len, std::array<uint8_t, RSA_BYTES> & signature)
{
	uint8_t digest[32];
	SHA256 (message, len, digest);
	std::array<uint8_t, RSA_BYTES> encoded { };
	encoded[0] = 0;
	encoded[1] = 1; // PKCS#1 v1.5 block type 1
	memset (encoded.data () + 2, 0xFF, encoded.size () - 2 - 1 - sizeof (digest));
	encoded[encoded.size () - 1 - sizeof (digest)] = 0;
	memcpy (encoded.data () + encoded.size () - sizeof (digest), digest, sizeof (digest));

	BN_CTX * ctx = BN_CTX_new ();
	BIGNUM * m = BN_bin2bn (encoded.data (), encoded.size (), nullptr);
	BIGNUM * n = BN_bin2bn (key.modulus.data (), key.modulus.size (), nullptr);
	BIGNUM * raw = BN_new ();
	assert (ctx && m && n && raw);
	assert (BN_mod_exp (raw, m, key.d, n, ctx) == 1);
	assert (i2p::crypto::bn2buf (raw, signature.data (), signature.size ()));
	BN_free (raw);
	BN_free (n);
	BN_free (m);
	BN_CTX_free (ctx);
	OPENSSL_cleanse (digest, sizeof (digest));
}

std::vector<uint8_t> BuildRsaIdentity (const RsaKey & key)
{
	std::vector<uint8_t> identity (DEFAULT_IDENTITY_SIZE + 4 + RSA_EXCESS, 0);
	// Key certificate: signing type, crypto type and the excess modulus
	// bytes.
	identity[DEFAULT_IDENTITY_SIZE - 3] = CERTIFICATE_TYPE_KEY;
	htobe16buf (identity.data () + DEFAULT_IDENTITY_SIZE - 2, static_cast<uint16_t> (4 + RSA_EXCESS));
	htobe16buf (identity.data () + DEFAULT_IDENTITY_SIZE, SIGNING_KEY_TYPE_RSA_SHA256_2048);
	htobe16buf (identity.data () + DEFAULT_IDENTITY_SIZE + 2, CRYPTO_KEY_TYPE_ELGAMAL);
	for (size_t i = 0; i < 256; ++i) identity[i] = static_cast<uint8_t> (i | 1); // crypto public key
	memcpy (identity.data () + 256, key.modulus.data (), 128);
	memcpy (identity.data () + DEFAULT_IDENTITY_SIZE + 4, key.modulus.data () + 128, RSA_EXCESS);
	return identity;
}

std::vector<uint8_t> BuildSignedRouterInfo (const std::vector<uint8_t> & identity, const RsaKey & key)
{
	const auto body = BuildBody ();
	std::vector<uint8_t> buffer = identity;
	buffer.insert (buffer.end (), body.begin (), body.end ());
	std::array<uint8_t, RSA_BYTES> signature { };
	SignRawRsa (key, buffer.data (), buffer.size (), signature);
	buffer.insert (buffer.end (), signature.begin (), signature.end ());
	return buffer;
}

} // namespace

int main ()
{
	RsaKey key;
	GenerateRsaKey (key);

	const auto identityBuffer = BuildRsaIdentity (key);
	IdentityEx identity (identityBuffer.data (), identityBuffer.size ());
	assert (identity.IsValid ());
	assert (identity.IsRSA ());
	assert (identity.GetSignatureLen () == RSA_BYTES);

	const auto buffer = BuildSignedRouterInfo (identityBuffer, key);
	const size_t signedLen = buffer.size () - RSA_BYTES;
	// The RSA signature itself is valid, so the verifier must stay
	// available
	// for SU3/local use and RouterInfo rejection is a policy decision.
	assert (identity.Verify (buffer.data (), signedLen, buffer.data () + signedLen));

	{
		RouterInfo router (buffer.data (), buffer.size ());
		assert (router.IsUnreachable ());
		// The identity is parsed (and retained for diagnostics) before the
		// policy check, but the body must never be treated as valid. An
		// update re-uses it and must be rejected as well.
		assert (router.Update (buffer.data (), buffer.size ()) == false);
	}

	// The same RouterInfo body signed with an Ed25519 identity is
	// accepted, so the rejection above is the RSA identity type and not
	// the body.
	{
		auto keys = PrivateKeys::CreateRandomKeys (SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519, CRYPTO_KEY_TYPE_ELGAMAL);
		assert (keys.GetPublic ());
		const auto edIdentity = keys.GetPublic ();
		std::vector<uint8_t> edIdentityBuffer (edIdentity->GetFullLen ());
		assert (edIdentity->ToBuffer (edIdentityBuffer.data (), edIdentityBuffer.size ()) == edIdentityBuffer.size ());

		const auto body = BuildBody ();
		std::vector<uint8_t> accepted = edIdentityBuffer;
		accepted.insert (accepted.end (), body.begin (), body.end ());
		std::vector<uint8_t> signature (edIdentity->GetSignatureLen ());
		keys.Sign (accepted.data (), static_cast<int> (accepted.size ()), signature.data ());
		accepted.insert (accepted.end (), signature.begin (), signature.end ());

		RouterInfo router (accepted.data (), accepted.size ());
		assert (!router.IsUnreachable ());
	}

	return 0;
}

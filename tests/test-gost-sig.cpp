/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: test gost sig.
 *
 * Generates GOST R 34.10-2012 256- and 512-bit keys and checks that
 * each signature verifies with the matching verifier and parameter set.
 *
 */

#include <cassert>
#include <inttypes.h>
#include <string.h>

#include "Gost.h"
#include "Signature.h"

const uint8_t example2[72] = { 0xfb, 0xe2, 0xe5, 0xf0, 0xee, 0xe3, 0xc8, 0x20, 0xfb, 0xea, 0xfa, 0xeb, 0xef, 0x20, 0xff,
	0xfb, 0xf0, 0xe1, 0xe0, 0xf0, 0xf5, 0x20, 0xe0, 0xed, 0x20, 0xe8, 0xec, 0xe0, 0xeb, 0xe5, 0xf0, 0xf2, 0xf1, 0x20,
	0xff, 0xf0, 0xee, 0xec, 0x20, 0xf1, 0x20, 0xfa, 0xf2, 0xfe, 0xe5, 0xe2, 0x20, 0x2c, 0xe8, 0xf6, 0xf3, 0xed, 0xe2,
	0x20, 0xe8, 0xe6, 0xee, 0xe1, 0xe8, 0xf0, 0xf2, 0xd1, 0x20, 0x2c, 0xe8, 0xf0, 0xf2, 0xe5, 0xe2, 0x20, 0xe5, 0xd1 };

int main ()
{
	uint8_t priv[64], pub[128], signature[128];
	i2p::crypto::CreateGOSTR3410RandomKeys (i2p::crypto::eGOSTR3410TC26A512, priv, pub);
	i2p::crypto::GOSTR3410_512_Signer signer (i2p::crypto::eGOSTR3410TC26A512, priv);
	signer.Sign (example2, 72, signature);
	i2p::crypto::GOSTR3410_512_Verifier verifier (i2p::crypto::eGOSTR3410TC26A512);
	verifier.SetPublicKey (pub);
	assert (verifier.Verify (example2, 72, signature));

	i2p::crypto::CreateGOSTR3410RandomKeys (i2p::crypto::eGOSTR3410CryptoProA, priv, pub);
	i2p::crypto::GOSTR3410_256_Signer signer1 (i2p::crypto::eGOSTR3410CryptoProA, priv);
	signer1.Sign (example2, 72, signature);
	i2p::crypto::GOSTR3410_256_Verifier verifier1 (i2p::crypto::eGOSTR3410CryptoProA);
	verifier1.SetPublicKey (pub);
	assert (verifier1.Verify (example2, 72, signature));

	// RFC 7091 section 6: e = digest mod q, and e = 1 when the residue is
	// zero. Both directions must agree on that degenerate digest.
	{
		auto & curve = *i2p::crypto::GetGOSTR3410Curve (i2p::crypto::eGOSTR3410CryptoProA);
		BN_CTX * ctx = BN_CTX_new ();
		assert (ctx);
		BIGNUM *q = BN_CTX_get (ctx), *privBN = BN_CTX_get (ctx);
		BIGNUM *digest = BN_CTX_get (ctx), *r = BN_CTX_get (ctx), *s = BN_CTX_get (ctx);
		assert (q && privBN && digest && r && s && EC_GROUP_get_order (curve.GetGroup (), q, ctx) == 1);
		assert (BN_one (privBN) == 1);
		EC_POINT * pubPoint = curve.MulP (privBN);
		assert (pubPoint);
		for (int zero = 0; zero < 2; ++zero)
		{
			if (zero)
				BN_zero (digest);
			else
				assert (BN_copy (digest, q) != nullptr);
			curve.Sign (privBN, digest, r, s);
			assert (!BN_is_zero (r) && !BN_is_zero (s));
			assert (curve.Verify (pubPoint, digest, r, s));
		}
		EC_POINT_free (pubPoint);
		BN_CTX_free (ctx);
	}
}

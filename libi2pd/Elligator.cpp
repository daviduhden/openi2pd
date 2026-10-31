/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: Elligator.
 *
 * Implements Elligator2 encoding and decoding of X25519 public keys
 * into 32-byte strings for the ECIES ratchet's oblivious new-session
 * field. Encoding selects the random alternative branch and high bits,
 * while decoding reconstructs and validates a curve point, rejecting
 * non-canonical or unmappable inputs; shared curve constants are
 * initialised thread-safely.
 *
 */

#include <openssl/rand.h>
#include "Crypto.h"
#include "Elligator.h"

namespace i2p
{
namespace crypto
{

	Elligator2::Elligator2 ()
	{
		// TODO: share with Ed22519
		p = BN_new ();
		// 2^255-19
		BN_set_bit (p, 255); // 2^255
		BN_sub_word (p, 19);
		p38 = BN_dup (p);
		BN_add_word (p38, 3);
		BN_div_word (p38, 8); // (p+3)/8
		p12 = BN_dup (p);
		BN_sub_word (p12, 1);
		BN_div_word (p12, 2); // (p-1)/2
		p14 = BN_dup (p);
		BN_sub_word (p14, 1);
		BN_div_word (p14, 4); // (p-1)/4

		A = BN_new ();
		BN_set_word (A, 486662);
		nA = BN_new ();
		BN_sub (nA, p, A);

		BN_CTX * ctx = BN_CTX_new ();
		// calculate sqrt(-1)
		sqrtn1 = BN_new ();
		BN_set_word (sqrtn1, 2);
		BN_mod_exp (sqrtn1, sqrtn1, p14, p, ctx); // 2^((p-1)/4

		u = BN_new ();
		BN_set_word (u, 2);
		iu = BN_new ();
		BN_mod_inverse (iu, u, p, ctx);

		BN_CTX_free (ctx);
	}

	Elligator2::~Elligator2 ()
	{
		BN_free (p);
		BN_free (p38);
		BN_free (p12);
		BN_free (p14);
		BN_free (sqrtn1);
		BN_free (A);
		BN_free (nA);
		BN_free (u);
		BN_free (iu);
	}

	bool Elligator2::Encode (const uint8_t * key, uint8_t * encoded, bool highY, bool random) const
	{
		if (!key || !encoded) return false;
		BN_CTX * ctx = BN_CTX_new ();
		if (!ctx) return false;
		BN_CTX_start (ctx);
		bool ret = true;

		uint8_t key1[32];
		for (size_t i = 0; i < 16; i++) // from Little Endian
		{
			key1[i] = key[31 - i];
			key1[31 - i] = key[i];
		}

		BIGNUM * x = BN_CTX_get (ctx);
		BIGNUM * xA = BN_CTX_get (ctx);
		BIGNUM * uxxA = BN_CTX_get (ctx);
		BIGNUM * r = BN_CTX_get (ctx);
		if (!x || !xA || !uxxA || !r)
		{
			BN_CTX_end (ctx);
			BN_CTX_free (ctx);
			return false;
		}
		// Java operates on the field element, so non-canonical input is
		// reduced before the multiplication below.
		ret = BN_bin2bn (key1, 32, x) != nullptr && BN_nnmod (x, x, p, ctx) == 1;
		if (ret)
		{
			// Java forces the non-alternative branch for x = 0 instead of
			// attempting to invert zero.
			if (BN_is_zero (x)) highY = false;
			ret = BN_mod_add (xA, x, A, p, ctx) == 1; // x + A (mod p)
			if (ret) ret = BN_sub (xA, p, xA) == 1; // -(x + A) (mod p)
			ret = ret && BN_mod_mul (uxxA, u, x, p, ctx) == 1 && BN_mod_mul (uxxA, uxxA, xA, p, ctx) == 1;
		}

		if (ret && Legendre (uxxA, ctx) != -1)
		{
			uint8_t randByte = 0; // random highest bits and high y
			if (random)
			{
				if (RAND_bytes (&randByte, 1) != 1) ret = false;
				// Java: alternative = (rand & 0x01) == 0. highY is the
				// C++ name for that same "alternative" branch.
				else
					highY = (randByte & 0x01) == 0;
			}

			if (ret)
			{
				// r = (highY ? x : x+A)^-1 * (highY ? x+A : x) / u
				BIGNUM * numerator = highY ? xA : x;
				BIGNUM * denominator = highY ? x : xA;
				ret = BN_mod_inverse (r, denominator, p, ctx) != nullptr && BN_mod_mul (r, r, numerator, p, ctx) == 1
					&& BN_mod_mul (r, r, iu, p, ctx) == 1;
				if (ret)
				{
					SquareRoot (r, r, ctx);
					ret = bn2buf (r, encoded, 32);
				}

				if (ret)
				{
					if (random) encoded[0] |= (randByte & 0xC0); // copy two highest bits from randByte
					for (size_t i = 0; i < 16; i++) // To Little Endian
					{
						uint8_t tmp = encoded[i];
						encoded[i] = encoded[31 - i];
						encoded[31 - i] = tmp;
					}
				}
			}
		}
		else
			ret = false;

		BN_CTX_end (ctx);
		BN_CTX_free (ctx);
		return ret;
	}

	bool Elligator2::Decode (const uint8_t * encoded, uint8_t * key) const
	{
		if (!encoded || !key) return false;
		BN_CTX * ctx = BN_CTX_new ();
		if (!ctx) return false;
		BN_CTX_start (ctx);
		bool ret = true;

		uint8_t encoded1[32];
		for (size_t i = 0; i < 16; i++) // from Little Endian
		{
			encoded1[i] = encoded[31 - i];
			encoded1[31 - i] = encoded[i];
		}
		encoded1[0] &= 0x3F; // drop two highest bits

		BIGNUM * r = BN_CTX_get (ctx);
		BIGNUM * v = BN_CTX_get (ctx);
		BIGNUM * vpA = BN_CTX_get (ctx);
		BIGNUM * t = BN_CTX_get (ctx);
		BIGNUM * x = BN_CTX_get (ctx);
		if (!r || !v || !vpA || !t || !x)
		{
			BN_CTX_end (ctx);
			BN_CTX_free (ctx);
			return false;
		}
		ret = BN_bin2bn (encoded1, 32, r) != nullptr;

		if (ret && BN_cmp (r, p12) < 0) // r < (p-1)/2
		{
			// v = -A/(1+u*r^2)
			ret = BN_mod_sqr (v, r, p, ctx) == 1 && BN_mod_mul (v, v, u, p, ctx) == 1 && BN_add_word (v, 1) == 1
				&& BN_mod_inverse (v, v, p, ctx) != nullptr && BN_mod_mul (v, v, nA, p, ctx) == 1;
			if (ret)
			{
				ret = BN_mod_add (vpA, v, A, p, ctx) == 1; // v + A (mod p)
				// t = v^3+A*v^2+v = v^2*(v+A)+v
				ret = ret && BN_mod_sqr (t, v, p, ctx) == 1 && BN_mod_mul (t, t, vpA, p, ctx) == 1
					&& BN_mod_add (t, t, v, p, ctx) == 1;
			}
			if (ret)
			{
				int legendre = Legendre (t, ctx);
				if (legendre == 1)
					ret = BN_copy (x, v) != nullptr;
				else
				{
					ret = BN_sub (x, p, v) == 1 && BN_mod_sub (x, x, A, p, ctx) == 1;
				}
			}
			if (ret) ret = bn2buf (x, key, 32);
			if (ret)
			{
				for (size_t i = 0; i < 16; i++) // To Little Endian
				{
					uint8_t tmp = key[i];
					key[i] = key[31 - i];
					key[31 - i] = tmp;
				}
			}
		}
		else
			ret = false;

		BN_CTX_end (ctx);
		BN_CTX_free (ctx);

		return ret;
	}

	void Elligator2::SquareRoot (const BIGNUM * x, BIGNUM * r, BN_CTX * ctx) const
	{
		BIGNUM * t = BN_CTX_get (ctx);
		BN_mod_exp (t, x, p14, p, ctx); // t = x^((p-1)/4)
		BN_mod_exp (r, x, p38, p, ctx); // r = x^((p+3)/8)
		BN_add_word (t, 1);

		if (!BN_cmp (t, p)) BN_mod_mul (r, r, sqrtn1, p, ctx);

		if (BN_cmp (r, p12) > 0) // r > (p-1)/2
			BN_sub (r, p, r);
	}

	int Elligator2::Legendre (const BIGNUM * a, BN_CTX * ctx) const
	{
		// assume a < p, so don't check for a % p = 0, but a = 0 only
		if (BN_is_zero (a)) return 0;
		BIGNUM * r = BN_CTX_get (ctx);
		BN_mod_exp (r, a, p12, p, ctx); // r = a^((p-1)/2) mod p
		if (BN_is_word (r, 1))
			return 1;
		else if (BN_is_zero (r))
			return 0;
		return -1;
	}

	std::unique_ptr<Elligator2> & GetElligator ()
	{
		// Function-local static initialisation is thread-safe in C++11 and
		// later. The old check-then-create pattern could construct two
		// Elligator2 instances concurrently and race on the global pointer.
		static std::unique_ptr<Elligator2> g_Elligator = std::make_unique<Elligator2> ();
		return g_Elligator;
	}
}
}

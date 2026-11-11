/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: Ed25519.
 *
 * Implements Ed25519/EdDSA signing and verification plus RedDSA signing
 * and public/private key blinding over OpenSSL BIGNUM and EC
 * arithmetic. Point decoding recovers x from y, checks the curve
 * equation and preserves the sign bit, while signing uses deterministic
 * EdDSA nonces or a fresh random RedDSA nonce.
 *
 */

#include <openssl/evp.h>
#include <openssl/crypto.h>
#include <cstring>
#include <memory>
#include <stdexcept>
#include "Log.h"
#include "Crypto.h"
#include "Ed25519.h"
#include "Modern.h"

namespace i2p
{
namespace crypto
{
	Ed25519::Ed25519 ()
	{
		BN_CTX * ctx = BN_CTX_new ();
		BIGNUM * tmp = BN_new ();

		q = BN_new ();
		// 2^255-19
		BN_set_bit (q, 255); // 2^255
		BN_sub_word (q, 19);

		l = BN_new ();
		// 2^252 + 27742317777372353535851937790883648493
		BN_set_bit (l, 252);
		two_252_2 = BN_dup (l);
		BN_dec2bn (&tmp, "27742317777372353535851937790883648493");
		BN_add (l, l, tmp);
		BN_sub_word (two_252_2, 2); // 2^252 - 2

		// -121665*inv(121666)
		d = BN_new ();
		BN_set_word (tmp, 121666);
		BN_mod_inverse (tmp, tmp, q, ctx);
		BN_set_word (d, 121665);
		BN_set_negative (d, 1);
		BN_mod_mul (d, d, tmp, q, ctx);

		// 2^((q-1)/4)
		I = BN_new ();
		BN_free (tmp);
		tmp = BN_dup (q);
		BN_sub_word (tmp, 1);
		BN_div_word (tmp, 4);
		BN_set_word (I, 2);
		BN_mod_exp (I, I, tmp, q, ctx);
		BN_free (tmp);

		// 4*inv(5)
		BIGNUM * By = BN_new ();
		BN_set_word (By, 5);
		BN_mod_inverse (By, By, q, ctx);
		BN_mul_word (By, 4);
		BIGNUM * Bx = RecoverX (By, ctx);
		if (!Bx)
		{
			// A provider failure cannot publish a missing base point.
			// A failed constructor does not run this class's destructor.
			BN_free (By);
			BN_CTX_free (ctx);
			BN_free (q);
			BN_free (l);
			BN_free (d);
			BN_free (I);
			BN_free (two_252_2);
			throw std::runtime_error ("Ed25519 base-point recovery failed");
		}
		BN_mod (Bx, Bx, q, ctx); // % q
		BN_mod (By, By, q, ctx); // % q

		// precalculate Bi256 table
		Bi256Carry = { Bx, By }; // B
		for (int i = 0; i < 32; i++)
		{
			Bi256[i][0] = Bi256Carry; // first point
			for (int j = 1; j < 128; j++) Bi256[i][j] = Sum (Bi256[i][j - 1], Bi256[i][0], ctx); // (256+j+1)^i*B
			Bi256Carry = Bi256[i][127];
			for (int j = 0; j < 128; j++) // add first point 128 more times
				Bi256Carry = Sum (Bi256Carry, Bi256[i][0], ctx);
		}

		BN_CTX_free (ctx);
	}

	Ed25519::Ed25519 (const Ed25519 & other)
		: q (BN_dup (other.q))
		, l (BN_dup (other.l))
		, d (BN_dup (other.d))
		, I (BN_dup (other.I))
		, two_252_2 (BN_dup (other.two_252_2))
		, Bi256Carry (other.Bi256Carry)
	{
		for (int i = 0; i < 32; i++)
			for (int j = 0; j < 128; j++) Bi256[i][j] = other.Bi256[i][j];
	}

	Ed25519::~Ed25519 ()
	{
		BN_free (q);
		BN_free (l);
		BN_free (d);
		BN_free (I);
		BN_free (two_252_2);
	}

	EDDSAPoint Ed25519::GeneratePublicKey (const uint8_t * expandedPrivateKey, BN_CTX * ctx) const
	{
		return MulB (expandedPrivateKey, ctx); // left half of expanded key, considered as Little Endian
	}

	EDDSAPoint Ed25519::DecodePublicKey (const uint8_t * buf, BN_CTX * ctx) const
	{
		return DecodePoint (buf, ctx);
	}

	bool Ed25519::EncodePublicKey (const EDDSAPoint & publicKey, uint8_t * buf, BN_CTX * ctx) const
	{
		if (!buf) return false;
		if (EncodePoint (Normalize (publicKey, ctx), buf)) return true;
		memset (buf, 0, EDDSA25519_PUBLIC_KEY_LENGTH);
		return false;
	}

	bool Ed25519::IsValidPublicKey (const uint8_t * buf) const
	{
		if (!buf) return false;
		BN_CTX * ctx = BN_CTX_new ();
		if (!ctx) return false;
		const auto p = DecodePoint (buf, ctx);
		// DecodePoint() returns an empty point when the recovered x does not
		// satisfy the curve equation, so presence is the on-curve result.
		const bool ok = p.x && p.y;
		BN_CTX_free (ctx);
		return ok;
	}

	bool Ed25519::Verify (const EDDSAPoint & publicKey, const uint8_t * digest, const uint8_t * signature) const
	{
		if (!digest || !signature) return false;
		if (!publicKey.x || !publicKey.y)
		{
			LogPrint (eLogError, "25519 signature verification with an invalid public key");
			return false;
		}
		BN_CTX * ctx = BN_CTX_new ();
		if (!ctx) return false;
		BIGNUM * h = DecodeBN<64> (digest);
		// signature 0..31 - R, 32..63 - S
		// B*S = R + PK*h => R = B*S - PK*h
		// RFC 8032 section 5.1.7 requires 0 <= S < L. Without this check a
		// malleated signature with S + L verifies, because MulB() reduces the
		// scalar modulo the group order.
		BIGNUM * s = DecodeBN<32> (signature + EDDSA25519_SIGNATURE_LENGTH / 2);
		bool passed = h && s && BN_cmp (s, l) < 0;
		if (s) BN_free (s); // public value
		if (passed)
		{
			// we don't decode R, but encode (B*S - PK*h)
			auto Bs = MulB (signature + EDDSA25519_SIGNATURE_LENGTH / 2, ctx); // B*S;
			BN_mod (h, h, l, ctx); // public key is multiple of B, but B%l = 0
			auto PKh = Mul (publicKey, h, ctx); // PK*h
			// A failed encoder leaves the buffer zero-filled
			// and fails the comparison instead of being read
			// uninitialised.
			uint8_t diff[32] = { };
			const bool encoded
				= EncodePoint (Normalize (Sum (Bs, -PKh, ctx), ctx), diff); // Bs - PKh encoded
			// Constant-time comparison: the expected value must not leak
			// through an early-exit memcmp().
			passed = encoded && CRYPTO_memcmp (signature, diff, 32) == 0; // R
		}
		if (h) BN_free (h);
		BN_CTX_free (ctx);
		if (!passed) LogPrint (eLogError, "25519 signature verification failed");
		return passed;
	}

	void Ed25519::Sign (const uint8_t * expandedPrivateKey, const uint8_t * publicKeyEncoded, const uint8_t * buf,
		size_t len, uint8_t * signature) const
	{
		if (!signature) return;
		// Every failure path zeroes the complete signature.
		// Published records (RouterInfo, LeaseSet1) are signed
		// through this call, so a partial R or a missing S must
		// never look like a signature.
		if (!expandedPrivateKey || !publicKeyEncoded || (!buf && len))
		{
			memset (signature, 0, EDDSA25519_SIGNATURE_LENGTH);
			LogPrint (eLogError, "Ed25519: signing failed");
			return;
		}
		BN_CTX * bnCtx = BN_CTX_new ();
		EVP_MD_CTX * ctx = nullptr;
		using SecretBN = std::unique_ptr<BIGNUM, decltype (&BN_clear_free)>;
		SecretBN r (nullptr, BN_clear_free);
		BIGNUM *h = nullptr, *a = nullptr;
		uint8_t digest[64] = { };
		unsigned int dl = sizeof (digest);
		bool ok = bnCtx && (ctx = EVP_MD_CTX_new ()) != nullptr && EVP_DigestInit_ex (ctx, EVP_sha512 (), nullptr) == 1
			&& EVP_DigestUpdate (ctx, expandedPrivateKey + EDDSA25519_PRIVATE_KEY_LENGTH, EDDSA25519_PRIVATE_KEY_LENGTH)
				== 1
			&& EVP_DigestUpdate (ctx, buf, len) == 1 && EVP_DigestFinal_ex (ctx, digest, &dl) == 1
			&& dl == sizeof (digest);
		if (ctx)
		{
			EVP_MD_CTX_free (ctx);
			ctx = nullptr;
		}
		// RFC 8032 section 5.1.6 reduces the entire SHA-512 nonce modulo L.
		// MulB consumes 32 little-endian bytes, so encode the reduced scalar
		// rather than discarding the upper half of the hash.
		r.reset (ok ? DecodeBN<64> (digest) : nullptr);
		if (r) BN_set_flags (r.get (), BN_FLG_CONSTTIME);
		ok = ok && r && BN_mod (r.get (), r.get (), l, bnCtx) == 1 && EncodeBN (r.get (), digest, 32);
		// calculate R
		uint8_t R[EDDSA25519_SIGNATURE_LENGTH / 2] = { }; // signature might overlap buf
		// The encoder result is part of the success flag: a point that
		// cannot be encoded would otherwise be published as a signature
		// with an all-zero R half.
		if (ok && r)
			ok = EncodePoint (Normalize (MulB (digest, bnCtx), bnCtx), R);
		// calculate S
		ctx = ok ? EVP_MD_CTX_new () : nullptr;
		ok = ok && ctx && EVP_DigestInit_ex (ctx, EVP_sha512 (), nullptr) == 1
			&& EVP_DigestUpdate (ctx, R, EDDSA25519_SIGNATURE_LENGTH / 2) == 1
			&& EVP_DigestUpdate (ctx, publicKeyEncoded, EDDSA25519_PUBLIC_KEY_LENGTH) == 1
			&& EVP_DigestUpdate (ctx, buf, len) == 1 && EVP_DigestFinal_ex (ctx, digest, &dl) == 1
			&& dl == sizeof (digest);
		if (ctx) EVP_MD_CTX_free (ctx);
		h = ok ? DecodeBN<64> (digest) : nullptr;
		a = ok ? DecodeBN<EDDSA25519_PRIVATE_KEY_LENGTH> (expandedPrivateKey) : nullptr; // left half
		ok = ok && r && h && a && BN_mod_mul (h, h, a, l, bnCtx) == 1 && BN_mod_add (h, h, r.get (), l, bnCtx) == 1;
		if (ok)
		{
			memcpy (signature, R, EDDSA25519_SIGNATURE_LENGTH / 2);
			// Fold the S encoder result in as well, so a failed second
			// half zeroes the R half that was already copied.
			ok = EncodeBN (h, signature + EDDSA25519_SIGNATURE_LENGTH / 2, EDDSA25519_SIGNATURE_LENGTH / 2);
		}
		if (!ok)
		{
			memset (signature, 0, EDDSA25519_SIGNATURE_LENGTH);
			LogPrint (eLogError, "Ed25519: signing failed");
		}
		if (h) BN_free (h); // public signature scalar
		if (a) BN_clear_free (a);

		OPENSSL_cleanse (digest, sizeof (digest));
		OPENSSL_cleanse (R, sizeof (R));
		if (bnCtx) BN_CTX_free (bnCtx);
	}

	void Ed25519::SignRedDSA (const uint8_t * privateKey, const uint8_t * publicKeyEncoded, const uint8_t * buf,
		size_t len, uint8_t * signature) const
	{
		if (!signature) return;
		bool complete = false;
		uint8_t T[80] {}, digest[64] {}, R[EDDSA25519_SIGNATURE_LENGTH / 2] {};
		i2p::modern::ScopeExit cleanup ([&]
			{
				OPENSSL_cleanse (T, sizeof (T));
				OPENSSL_cleanse (digest, sizeof (digest));
				OPENSSL_cleanse (R, sizeof (R));
				if (!complete)
				{
					memset (signature, 0, EDDSA25519_SIGNATURE_LENGTH);
					LogPrint (eLogError, "RedDSA: signing failed");
				}
			});
		if (!privateKey || !publicKeyEncoded || (!buf && len)) return;
		std::unique_ptr<BN_CTX, decltype (&BN_CTX_free)> bnCtx (BN_CTX_new (), BN_CTX_free);
		std::unique_ptr<EVP_MD_CTX, decltype (&EVP_MD_CTX_free)> ctx (EVP_MD_CTX_new (), EVP_MD_CTX_free);
		if (!bnCtx || !ctx || RAND_bytes (T, sizeof (T)) != 1) return;
		unsigned int dl = sizeof (digest);
		// Proposal 123: r = H*(T || public key || message).
		if (EVP_DigestInit_ex (ctx.get (), EVP_sha512 (), nullptr) != 1
			|| EVP_DigestUpdate (ctx.get (), T, sizeof (T)) != 1
			|| EVP_DigestUpdate (ctx.get (), publicKeyEncoded, EDDSA25519_PUBLIC_KEY_LENGTH) != 1
			|| (len && EVP_DigestUpdate (ctx.get (), buf, len) != 1)
			|| EVP_DigestFinal_ex (ctx.get (), digest, &dl) != 1 || dl != sizeof (digest)) return;
		using SecretBN = std::unique_ptr<BIGNUM, decltype (&BN_clear_free)>;
		SecretBN r (DecodeBN<64> (digest), BN_clear_free);
		if (r) BN_set_flags (r.get (), BN_FLG_CONSTTIME);
		if (!r || BN_mod (r.get (), r.get (), l, bnCtx.get ()) != 1) return;
		if (!EncodeBN (r.get (), digest, 32)) return;
		// Keep R separate: the signature is allowed to overlap the message.
		if (!EncodePoint (Normalize (MulB (digest, bnCtx.get ()), bnCtx.get ()), R)) return;
		dl = sizeof (digest);
		if (EVP_DigestInit_ex (ctx.get (), EVP_sha512 (), nullptr) != 1
			|| EVP_DigestUpdate (ctx.get (), R, sizeof (R)) != 1
			|| EVP_DigestUpdate (ctx.get (), publicKeyEncoded, EDDSA25519_PUBLIC_KEY_LENGTH) != 1
			|| (len && EVP_DigestUpdate (ctx.get (), buf, len) != 1)
			|| EVP_DigestFinal_ex (ctx.get (), digest, &dl) != 1 || dl != sizeof (digest)) return;
		SecretBN h (DecodeBN<64> (digest), BN_clear_free);
		SecretBN a (DecodeBN<EDDSA25519_PRIVATE_KEY_LENGTH> (privateKey), BN_clear_free);
		if (!h || !a || BN_mod_mul (h.get (), h.get (), a.get (), l, bnCtx.get ()) != 1
			|| BN_mod_add (h.get (), h.get (), r.get (), l, bnCtx.get ()) != 1) return;
		memcpy (signature, R, sizeof (R));
		if (!EncodeBN (h.get (), signature + sizeof (R), sizeof (R))) return;
		complete = true;
	}

	EDDSAPoint Ed25519::Sum (const EDDSAPoint & p1, const EDDSAPoint & p2, BN_CTX * ctx) const
	{
		// x3 = (x1*y2+y1*x2)*(z1*z2-d*t1*t2)
		// y3 = (y1*y2+x1*x2)*(z1*z2+d*t1*t2)
		// z3 = (z1*z2-d*t1*t2)*(z1*z2+d*t1*t2)
		// t3 = (y1*y2+x1*x2)*(x1*y2+y1*x2)
		BIGNUM *x3 = BN_new (), *y3 = BN_new (), *z3 = BN_new (), *t3 = BN_new ();

		BN_mul (x3, p1.x, p2.x, ctx); // A = x1*x2
		BN_mul (y3, p1.y, p2.y, ctx); // B = y1*y2

		BN_CTX_start (ctx);
		BIGNUM *t1 = p1.t, *t2 = p2.t;
		if (!t1)
		{
			t1 = BN_CTX_get (ctx);
			BN_mul (t1, p1.x, p1.y, ctx);
		}
		if (!t2)
		{
			t2 = BN_CTX_get (ctx);
			BN_mul (t2, p2.x, p2.y, ctx);
		}
		BN_mul (t3, t1, t2, ctx);
		BN_mul (t3, t3, d, ctx); // C = d*t1*t2

		if (p1.z)
		{
			if (p2.z)
				BN_mul (z3, p1.z, p2.z, ctx); // D = z1*z2
			else
				BN_copy (z3, p1.z); // D = z1
		}
		else
		{
			if (p2.z)
				BN_copy (z3, p2.z); // D = z2
			else
				BN_one (z3); // D = 1
		}

		BIGNUM *E = BN_CTX_get (ctx), *F = BN_CTX_get (ctx), *G = BN_CTX_get (ctx), *H = BN_CTX_get (ctx);
		BN_add (E, p1.x, p1.y);
		BN_add (F, p2.x, p2.y);
		BN_mul (E, E, F, ctx); // (x1 + y1)*(x2 + y2)
		BN_sub (E, E, x3);
		BN_sub (E, E, y3); // E = (x1 + y1)*(x2 + y2) - A - B
		BN_sub (F, z3, t3); // F = D - C
		BN_add (G, z3, t3); // G = D + C
		BN_add (H, y3, x3); // H = B + A

		BN_mod_mul (x3, E, F, q, ctx); // x3 = E*F
		BN_mod_mul (y3, G, H, q, ctx); // y3 = G*H
		BN_mod_mul (z3, F, G, q, ctx); // z3 = F*G
		BN_mod_mul (t3, E, H, q, ctx); // t3 = E*H

		BN_CTX_end (ctx);

		return EDDSAPoint { x3, y3, z3, t3 };
	}

	void Ed25519::Double (EDDSAPoint & p, BN_CTX * ctx) const
	{
		BN_CTX_start (ctx);
		BIGNUM *x2 = BN_CTX_get (ctx), *y2 = BN_CTX_get (ctx), *z2 = BN_CTX_get (ctx), *t2 = BN_CTX_get (ctx);

		BN_sqr (x2, p.x, ctx); // x2 = A = x^2
		BN_sqr (y2, p.y, ctx); // y2 = B = y^2
		if (p.t)
			BN_sqr (t2, p.t, ctx); // t2 = t^2
		else
		{
			BN_mul (t2, p.x, p.y, ctx); // t = x*y
			BN_sqr (t2, t2, ctx); // t2 = t^2
		}
		BN_mul (t2, t2, d, ctx); // t2 = C = d*t^2
		if (p.z)
			BN_sqr (z2, p.z, ctx); // z2 = D = z^2
		else
			BN_one (z2); // z2 = 1

		BIGNUM *E = BN_CTX_get (ctx), *F = BN_CTX_get (ctx), *G = BN_CTX_get (ctx), *H = BN_CTX_get (ctx);
		// E = (x+y)*(x+y)-A-B = x^2+y^2+2xy-A-B = 2xy
		BN_mul (E, p.x, p.y, ctx);
		BN_lshift1 (E, E); // E =2*x*y
		BN_sub (F, z2, t2); // F = D - C
		BN_add (G, z2, t2); // G = D + C
		BN_add (H, y2, x2); // H = B + A

		BN_mod_mul (p.x, E, F, q, ctx); // x2 = E*F
		BN_mod_mul (p.y, G, H, q, ctx); // y2 = G*H
		if (!p.z) p.z = BN_new ();
		BN_mod_mul (p.z, F, G, q, ctx); // z2 = F*G
		if (!p.t) p.t = BN_new ();
		BN_mod_mul (p.t, E, H, q, ctx); // t2 = E*H

		BN_CTX_end (ctx);
	}

	EDDSAPoint Ed25519::Mul (const EDDSAPoint & p, const BIGNUM * e, BN_CTX * ctx) const
	{
		BIGNUM *zero = BN_new (), *one = BN_new ();
		BN_zero (zero);
		BN_one (one);
		EDDSAPoint res { zero, one };
		if (!BN_is_zero (e))
		{
			int bitCount = BN_num_bits (e);
			for (int i = bitCount - 1; i >= 0; i--)
			{
				Double (res, ctx);
				if (BN_is_bit_set (e, i)) res = Sum (res, p, ctx);
			}
		}
		return res;
	}

	EDDSAPoint Ed25519::MulB (const uint8_t * e, BN_CTX * ctx) const // B*e, e is 32 bytes Little Endian
	{
		BIGNUM *zero = BN_new (), *one = BN_new ();
		BN_zero (zero);
		BN_one (one);
		EDDSAPoint res { zero, one };
		bool carry = false;
		for (int i = 0; i < 32; i++)
		{
			uint8_t x = e[i];
			if (carry)
			{
				if (x < 255)
				{
					x++;
					carry = false;
				}
				else
					x = 0;
			}
			if (x > 0)
			{
				if (x <= 128)
					res = Sum (res, Bi256[i][x - 1], ctx);
				else
				{
					res = Sum (res, -Bi256[i][255 - x], ctx); // -Bi[256-x]
					carry = true;
				}
			}
		}
		if (carry) res = Sum (res, Bi256Carry, ctx);
		return res;
	}

	EDDSAPoint Ed25519::Normalize (const EDDSAPoint & p, BN_CTX * ctx) const
	{
		if (!ctx || !p.x || !p.y) return {};
		EDDSAPoint normalized;
		if (p.z)
		{
			normalized.x = BN_new ();
			normalized.y = BN_new ();
			if (!normalized.x || !normalized.y
				|| !BN_mod_inverse (normalized.y, p.z, q, ctx)
				|| BN_mod_mul (normalized.x, p.x, normalized.y, q, ctx) != 1
				|| BN_mod_mul (normalized.y, p.y, normalized.y, q, ctx) != 1)
				return {};
		}
		else
		{
			normalized.x = BN_dup (p.x);
			normalized.y = BN_dup (p.y);
			if (!normalized.x || !normalized.y) return {};
		}
		return normalized;
	}

	bool Ed25519::IsOnCurve (const EDDSAPoint & p, BN_CTX * ctx) const
	{
		// Empty or incomplete points never authorize arithmetic.
		if (!ctx || !p.x || !p.y) return false;
		BN_CTX_start (ctx);
		auto finish = [ctx] (bool valid)
		{
			BN_CTX_end (ctx);
			return valid;
		};
		BIGNUM *x2 = BN_CTX_get (ctx), *y2 = BN_CTX_get (ctx);
		BIGNUM *tmp = BN_CTX_get (ctx);
		// y^2 - x^2 - 1 - d*x^2*y^2 must vanish modulo q.
		if (!x2 || !y2 || !tmp
			|| BN_sqr (x2, p.x, ctx) != 1
			|| BN_sqr (y2, p.y, ctx) != 1
			|| BN_mul (tmp, d, x2, ctx) != 1
			|| BN_mul (tmp, tmp, y2, ctx) != 1
			|| BN_sub (tmp, y2, tmp) != 1
			|| BN_sub (tmp, tmp, x2) != 1
			|| BN_sub_word (tmp, 1) != 1
			|| BN_mod (tmp, tmp, q, ctx) != 1)
			return finish (false);
		return finish (BN_is_zero (tmp));
	}

	BIGNUM * Ed25519::RecoverX (const BIGNUM * y, BN_CTX * ctx) const
	{
		if (!y || !ctx) return nullptr;
		BN_CTX_start (ctx);
		auto finish = [ctx] (BIGNUM * result)
		{
			BN_CTX_end (ctx);
			return result;
		};
		BIGNUM *y2 = BN_CTX_get (ctx), *xx = BN_CTX_get (ctx);
		// xx = (y^2 - 1) / (d*y^2 + 1), modulo q.
		if (!y2 || !xx || BN_sqr (y2, y, ctx) != 1
			|| BN_mul (xx, d, y2, ctx) != 1
			|| BN_add_word (xx, 1) != 1
			|| !BN_mod_inverse (xx, xx, q, ctx)
			|| BN_sub_word (y2, 1) != 1
			|| BN_mul (xx, y2, xx, ctx) != 1)
			return finish (nullptr);
		std::unique_ptr<BIGNUM, decltype (&BN_free)> x (BN_new (), BN_free);
		if (!x || BN_mod_exp (x.get (), xx, two_252_2, q, ctx) != 1
			|| BN_sqr (y2, x.get (), ctx) != 1
			|| BN_mod_sub (y2, y2, xx, q, ctx) != 1)
			return finish (nullptr);
		if (!BN_is_zero (y2)
			&& BN_mod_mul (x.get (), x.get (), I, q, ctx) != 1)
			return finish (nullptr);
		if (BN_is_odd (x.get ()) && BN_sub (x.get (), q, x.get ()) != 1)
			return finish (nullptr);
		return finish (x.release ());
	}

	EDDSAPoint Ed25519::DecodePoint (const uint8_t * buf, BN_CTX * ctx) const
	{
		if (!buf || !ctx) return {};
		// buf is 32 little-endian bytes; the high bit carries x parity.
		uint8_t buf1[EDDSA25519_PUBLIC_KEY_LENGTH];
		for (size_t i = 0; i < EDDSA25519_PUBLIC_KEY_LENGTH; ++i)
			buf1[i] = buf[EDDSA25519_PUBLIC_KEY_LENGTH - 1 - i];
		const bool isHighestBitSet = buf1[0] & 0x80;
		buf1[0] &= 0x7f;
		EDDSAPoint point;
		point.y = BN_bin2bn (buf1, EDDSA25519_PUBLIC_KEY_LENGTH, nullptr);
		if (!point.y) return {};
		point.x = RecoverX (point.y, ctx);
		if (!point.x) return {};
		if (static_cast<bool> (BN_is_bit_set (point.x, 0)) != isHighestBitSet
			&& BN_sub (point.x, q, point.x) != 1)
			return {};
		point.z = BN_new ();
		point.t = BN_new ();
		if (!point.z || !point.t || BN_one (point.z) != 1
			|| BN_mod_mul (point.t, point.x, point.y, q, ctx) != 1)
			return {};
		if (!IsOnCurve (point, ctx))
		{
			LogPrint (eLogError, "Decoded point is not on 25519");
			return {};
		}
		return point;
	}

	bool Ed25519::EncodePoint (const EDDSAPoint & p, uint8_t * buf) const
	{
		if (!p.x || !p.y || !EncodeBN (p.y, buf, EDDSA25519_PUBLIC_KEY_LENGTH)) return false;
		if (BN_is_bit_set (p.x, 0)) // highest bit
			buf[EDDSA25519_PUBLIC_KEY_LENGTH - 1] |= 0x80; // set highest bit
		return true;
	}

	template <int len> BIGNUM * Ed25519::DecodeBN (const uint8_t * buf) const
	{
		// buf is Little Endian convert it to Big Endian
		uint8_t buf1[len];
		for (size_t i = 0; i < len / 2; i++) // invert bytes
		{
			buf1[i] = buf[len - 1 - i];
			buf1[len - 1 - i] = buf[i];
		}
		BIGNUM * res = BN_bin2bn (buf1, len, nullptr);
		OPENSSL_cleanse (buf1, len);
		return res;
	}

	bool Ed25519::EncodeBN (const BIGNUM * bn, uint8_t * buf, size_t len) const
	{
		if (!bn2buf (bn, buf, len)) return false;
		// To Little Endian
		for (size_t i = 0; i < len / 2; i++) // invert bytes
		{
			uint8_t tmp = buf[i];
			buf[i] = buf[len - 1 - i];
			buf[len - 1 - i] = tmp;
		}
		return true;
	}

	bool Ed25519::BlindPublicKey (const uint8_t * pub, const uint8_t * seed, uint8_t * blinded)
	{
		if (!blinded) return false;
		memset (blinded, 0, EDDSA25519_PUBLIC_KEY_LENGTH);
		if (!pub || !seed) return false;
		BN_CTX * ctx = BN_CTX_new ();
		if (!ctx) return false;
		// calculate alpha = seed mod l
		BIGNUM * alpha = DecodeBN<64> (seed); // seed is in Little Endian
		if (!alpha)
		{
			BN_CTX_free (ctx);
			return false;
		}
		BN_mod (alpha, alpha, l, ctx); // % l
		uint8_t priv[32] = { };
		// A failed scalar encoding would leave a zero alpha and blind
		// with the identity element, so it is a failure, not a
		// degraded result.
		if (!EncodeBN (alpha, priv, 32)) // back to Little Endian
		{
			OPENSSL_cleanse (priv, sizeof (priv));
			BN_clear_free (alpha);
			BN_CTX_free (ctx);
			LogPrint (eLogError, "Ed25519: blinding scalar encoding failed");
			return false;
		}
		// alpha is a secret blinding scalar: wipe it, do not just free it.
		BN_clear_free (alpha);
		// A' = BLIND_PUBKEY(A, alpha) = A + DERIVE_PUBLIC(alpha)
		auto A = DecodePublicKey (pub, ctx);
		if (!A.x || !A.y)
		{
			OPENSSL_cleanse (priv, sizeof (priv));
			BN_CTX_free (ctx);
			return false;
		}
		auto A1 = Sum (A, MulB (priv, ctx), ctx); // pub + B*alpha
		// An unencodable point must fail the blinding instead of
		// returning a zero store key with a success result.
		// EncodePoint() writes into blinded only on success, so
		// zero the buffer on failure.
		if (!EncodePoint (Normalize (A1, ctx), blinded))
		{
			memset (blinded, 0, EDDSA25519_PUBLIC_KEY_LENGTH);
			OPENSSL_cleanse (priv, sizeof (priv));
			BN_CTX_free (ctx);
			LogPrint (eLogError, "Ed25519: blinded public key encoding failed");
			return false;
		}
		OPENSSL_cleanse (priv, sizeof (priv));
		BN_CTX_free (ctx);
		return true;
	}

	bool Ed25519::BlindPrivateKey (
		const uint8_t * priv, const uint8_t * seed, uint8_t * blindedPriv, uint8_t * blindedPub)
	{
		if (blindedPriv) memset (blindedPriv, 0, EDDSA25519_PRIVATE_KEY_LENGTH);
		if (blindedPub) memset (blindedPub, 0, EDDSA25519_PUBLIC_KEY_LENGTH);
		if (!priv || !seed || !blindedPriv || !blindedPub) return false;
		BN_CTX * ctx = BN_CTX_new ();
		if (!ctx) return false;
		// calculate alpha = seed mod l
		BIGNUM * alpha = DecodeBN<64> (seed); // seed is in Little Endian
		BIGNUM * p = DecodeBN<32> (priv); // priv is in Little Endian
		if (!alpha || !p)
		{
			BN_clear_free (alpha);
			BN_clear_free (p);
			BN_CTX_free (ctx);
			return false;
		}
		BN_mod (alpha, alpha, l, ctx); // % l
		BN_add (alpha, alpha, p); // alpha = alpha + priv
		// a' = BLIND_PRIVKEY(a, alpha) = (a + alpha) mod L
		BN_mod (alpha, alpha, l, ctx); // % l
		// Both encoders are part of the result: a failed
		// private-scalar or public-key encoding must not report
		// success with zero material, because the caller would
		// derive a store key from it.
		if (!EncodeBN (alpha, blindedPriv, 32))
		{
			OPENSSL_cleanse (blindedPriv, EDDSA25519_PRIVATE_KEY_LENGTH);
			OPENSSL_cleanse (blindedPub, EDDSA25519_PUBLIC_KEY_LENGTH);
			BN_clear_free (alpha);
			BN_clear_free (p);
			BN_CTX_free (ctx);
			LogPrint (eLogError, "Ed25519: blinded private key encoding failed");
			return false;
		}
		// A' = DERIVE_PUBLIC(a')
		auto A1 = MulB (blindedPriv, ctx);
		if (!EncodePoint (Normalize (A1, ctx), blindedPub))
		{
			OPENSSL_cleanse (blindedPriv, EDDSA25519_PRIVATE_KEY_LENGTH);
			OPENSSL_cleanse (blindedPub, EDDSA25519_PUBLIC_KEY_LENGTH);
			BN_clear_free (alpha);
			BN_clear_free (p);
			BN_CTX_free (ctx);
			LogPrint (eLogError, "Ed25519: blinded public key encoding failed");
			return false;
		}
		BN_clear_free (alpha);
		BN_clear_free (p);
		BN_CTX_free (ctx);
		return true;
	}

	bool Ed25519::ExpandPrivateKey (const uint8_t * key, uint8_t * expandedKey)
	{
		if (!expandedKey) return false;
		if (!key || !SHA512 (key, EDDSA25519_PRIVATE_KEY_LENGTH, expandedKey))
		{
			OPENSSL_cleanse (expandedKey, 2 * EDDSA25519_PRIVATE_KEY_LENGTH);
			return false;
		}
		expandedKey[0] &= 0xF8; // drop last 3 bits
		expandedKey[EDDSA25519_PRIVATE_KEY_LENGTH - 1] &= 0x3F; // drop first 2 bits
		expandedKey[EDDSA25519_PRIVATE_KEY_LENGTH - 1] |= 0x40; // set second bit
		return true;
	}

	bool Ed25519::CreateRedDSAPrivateKey (uint8_t * priv)
	{
		if (!priv) return false;
		uint8_t seed[32];
		if (RAND_bytes (seed, sizeof (seed)) != 1)
		{
			OPENSSL_cleanse (seed, sizeof (seed));
			memset (priv, 0, EDDSA25519_PRIVATE_KEY_LENGTH);
			return false;
		}
		BIGNUM * p = DecodeBN<32> (seed);
		BN_CTX * ctx = BN_CTX_new ();
		bool ok = p && ctx && BN_mod (p, p, l, ctx) == 1;
		if (ok)
			EncodeBN (p, priv, EDDSA25519_PRIVATE_KEY_LENGTH);
		else
			memset (priv, 0, EDDSA25519_PRIVATE_KEY_LENGTH);
		if (ctx) BN_CTX_free (ctx);
		BN_clear_free (p);
		OPENSSL_cleanse (seed, sizeof (seed));
		return ok;
	}

	std::unique_ptr<Ed25519> & GetEd25519 ()
	{
		// Function-local static initialization is thread-safe. A caller's
		// reference therefore points to an object that no other thread can
		// replace while it is in use.
		static std::unique_ptr<Ed25519> instance = std::make_unique<Ed25519> ();
		return instance;
	}
}
}

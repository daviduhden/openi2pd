/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: Ed25519.
 *
 * Implements Ed25519 and RedDSA point arithmetic, signing and verification on
 * top of OpenSSL BIGNUM, using projective coordinates and a precomputed
 * base-point table. Includes the key-blinding operations needed for encrypted
 * LeaseSet2.
 *
 */

#ifndef ED25519_H__
#define ED25519_H__

#include <memory>
#include <openssl/bn.h>
#include "Crypto.h"

namespace i2p
{
namespace crypto
{
	struct EDDSAPoint
	{
		BIGNUM * x { nullptr };
		BIGNUM * y { nullptr };
		BIGNUM * z { nullptr };
		BIGNUM * t { nullptr }; // projective coordinates

		EDDSAPoint () { }
		EDDSAPoint (const EDDSAPoint & other) { *this = other; }
		EDDSAPoint (EDDSAPoint && other) { *this = std::move (other); }
		EDDSAPoint (BIGNUM * x1, BIGNUM * y1, BIGNUM * z1 = nullptr, BIGNUM * t1 = nullptr)
			: x (x1)
			, y (y1)
			, z (z1)
			, t (t1)
		{
		}
		~EDDSAPoint ()
		{
			BN_free (x);
			BN_free (y);
			BN_free (z);
			BN_free (t);
		}

		EDDSAPoint & operator= (EDDSAPoint && other)
		{
			if (this != &other)
			{
				BN_free (x);
				x = other.x;
				other.x = nullptr;
				BN_free (y);
				y = other.y;
				other.y = nullptr;
				BN_free (z);
				z = other.z;
				other.z = nullptr;
				BN_free (t);
				t = other.t;
				other.t = nullptr;
			}
			return *this;
		}

		EDDSAPoint & operator= (const EDDSAPoint & other)
		{
			if (this != &other)
			{
				BN_free (x);
				x = other.x ? BN_dup (other.x) : nullptr;
				BN_free (y);
				y = other.y ? BN_dup (other.y) : nullptr;
				BN_free (z);
				z = other.z ? BN_dup (other.z) : nullptr;
				BN_free (t);
				t = other.t ? BN_dup (other.t) : nullptr;
			}
			return *this;
		}

		EDDSAPoint operator- () const
		{
			BIGNUM *x1 = NULL, *y1 = NULL, *z1 = NULL, *t1 = NULL;
			if (x)
			{
				x1 = BN_dup (x);
				BN_set_negative (x1, !BN_is_negative (x));
			};
			if (y) y1 = BN_dup (y);
			if (z) z1 = BN_dup (z);
			if (t)
			{
				t1 = BN_dup (t);
				BN_set_negative (t1, !BN_is_negative (t));
			};
			return EDDSAPoint { x1, y1, z1, t1 };
		}
	};

	const size_t EDDSA25519_PUBLIC_KEY_LENGTH = 32;
	const size_t EDDSA25519_SIGNATURE_LENGTH = 64;
	const size_t EDDSA25519_PRIVATE_KEY_LENGTH = 32;
	class Ed25519
	{
	public:
		Ed25519 ();
		Ed25519 (const Ed25519 & other);
		~Ed25519 ();

		EDDSAPoint GeneratePublicKey (const uint8_t * expandedPrivateKey, BN_CTX * ctx) const;
		EDDSAPoint DecodePublicKey (const uint8_t * buf, BN_CTX * ctx) const;
		void EncodePublicKey (const EDDSAPoint & publicKey, uint8_t * buf, BN_CTX * ctx) const;
		// Java GroupElement decoding rejects a y encoding whose recovered x
		// does not satisfy the curve equation. Callers must not blind or hash
		// through such a key. A rejected encoding decodes to an empty point.
		bool IsValidPublicKey (const uint8_t * buf) const;

		// Both return false when the input point is not on the curve or the
		// provider fails; the output buffers are then all-zero and the caller
		// must not derive a blinded store key from them.
		bool BlindPublicKey (const uint8_t * pub, const uint8_t * seed,
			uint8_t * blinded); // for encrypted LeaseSet2, pub - 32, seed - 64, blinded - 32
		bool BlindPrivateKey (const uint8_t * priv, const uint8_t * seed, uint8_t * blindedPriv,
			uint8_t * blindedPub); // for encrypted LeaseSet2, pub - 32, seed - 64, blinded - 32

		bool Verify (const EDDSAPoint & publicKey, const uint8_t * digest, const uint8_t * signature) const;
		/// Produce an Ed25519 signature over the message in buf.
		/// The nonce is derived deterministically from the expanded key prefix
		/// and the message, so the expanded key is secret material that the
		/// caller must cleanse. A missing input or a provider/digest failure
		/// zeroes the whole signature instead of returning a partial one.
		void Sign (const uint8_t * expandedPrivateKey, const uint8_t * publicKeyEncoded, const uint8_t * buf,
			size_t len, uint8_t * signature) const;
		/// RedDSA variant (Proposal 123) used by LeaseSet2: the nonce is
		/// randomized (80 CSPRNG bytes per RFC-style construction) because a
		/// predictable nonce would reveal the private scalar. A failed nonce draw
		/// zeroes the signature rather than falling back to a deterministic nonce.
		void SignRedDSA (const uint8_t * privateKey, const uint8_t * publicKeyEncoded, const uint8_t * buf, size_t len,
			uint8_t * signature) const;

		/// Expand a 32-byte seed into the 64-byte secret scalar and prefix:
		/// SHA-512 followed by the RFC 8032 clamping. The output is secret and
		/// must be cleansed by the caller when no longer needed.
		static void ExpandPrivateKey (
			const uint8_t * key, uint8_t * expandedKey); // key - 32 bytes, expandedKey - 64 bytes
		/// Generate a RedDSA private scalar reduced modulo the group order.
		/// @return False when the CSPRNG fails or no usable scalar was drawn; a
		/// false result must never be used as a key.
		bool CreateRedDSAPrivateKey (uint8_t * priv); // priv is 32 bytes

	private:
		EDDSAPoint Sum (const EDDSAPoint & p1, const EDDSAPoint & p2, BN_CTX * ctx) const;
		void Double (EDDSAPoint & p, BN_CTX * ctx) const;
		EDDSAPoint Mul (const EDDSAPoint & p, const BIGNUM * e, BN_CTX * ctx) const;
		EDDSAPoint MulB (const uint8_t * e, BN_CTX * ctx) const; // B*e, e is 32 bytes Little Endian
		EDDSAPoint Normalize (const EDDSAPoint & p, BN_CTX * ctx) const;

		bool IsOnCurve (const EDDSAPoint & p, BN_CTX * ctx) const;
		BIGNUM * RecoverX (const BIGNUM * y, BN_CTX * ctx) const;
		EDDSAPoint DecodePoint (const uint8_t * buf, BN_CTX * ctx) const;
		void EncodePoint (const EDDSAPoint & p, uint8_t * buf) const;

		template <int len> BIGNUM * DecodeBN (const uint8_t * buf) const;
		void EncodeBN (const BIGNUM * bn, uint8_t * buf, size_t len) const;

	private:
		BIGNUM *q, *l, *d, *I;
		// transient values
		BIGNUM * two_252_2; // 2^252-2
		EDDSAPoint Bi256[32][128]; // per byte, Bi256[i][j] = (256+j+1)^i*B, we don't store zeroes
		// if j > 128 we use 256 - j and carry 1 to next byte
		// Bi256[0][0] = B, base point
		EDDSAPoint Bi256Carry; // Bi256[32][0]
	};

	std::unique_ptr<Ed25519> & GetEd25519 ();

}
}

#endif

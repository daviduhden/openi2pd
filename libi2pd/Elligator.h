/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: Elligator.
 *
 * Declares Elligator2 encoding and decoding, which make X25519 public
 * keys indistinguishable from random bytes. The shared instance holds
 * the curve constants and square-root helpers required by the mapping.
 *
 */

#ifndef ELLIGATOR_H__
#define ELLIGATOR_H__

#include <inttypes.h>
#include <memory>
#include <openssl/bn.h>

namespace i2p
{
namespace crypto
{

	/// Elligator2 mapping for X25519 public keys, making an encoded key
	/// indistinguishable from random bytes to a passive observer.
	class Elligator2
	{
	public:
		Elligator2 ();
		~Elligator2 ();

		/// Map a 32-byte X25519 public key to a 32-byte representative.
		/// @param highY Selects which of the two curve points with the same
		/// x-coordinate the receiver should use.
		/// @param random Draw the remaining ambiguity from the CSPRNG instead
		///               of deriving it deterministically.
		/// @return False when the key has no Elligator representation
		///         (roughly half of all keys) or on provider failure. The
		///         caller must then send the plain key, which remains a valid
		///         handshake encoding; see
		///         X25519Keys::SetElligatorIneligible().
		bool Encode (const uint8_t * key, uint8_t * encoded, bool highY = false, bool random = true) const;
		/// Recover the public key from a representative.
		/// @return False when the input is not a canonical
		///         representative, so a failed decode must abort the
		///         handshake and never be retried as if it were a plain key.
		bool Decode (const uint8_t * encoded, uint8_t * key) const;

	private:
		void SquareRoot (const BIGNUM * x, BIGNUM * r, BN_CTX * ctx) const;
		int Legendre (const BIGNUM * a, BN_CTX * ctx) const; // a/p

	private:
		BIGNUM *p, *p38, *p12, *p14, *sqrtn1, *A, *nA, *u, *iu;
	};

	std::unique_ptr<Elligator2> & GetElligator ();
}
}

#endif

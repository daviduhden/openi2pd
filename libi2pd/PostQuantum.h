/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: PostQuantum.
 *
 * ML-KEM (FIPS 203) key encapsulation wrappers for the hybrid I2P
 * crypto types, with key/ciphertext sizes selected by CryptoKeyType and
 * OpenSSL/LibreSSL feature detection. Provides the Noise IK, XK and XK1
 * state initialization used by ratchets, NTCP2 and SSU2; every
 * initialization reports failure so a handshake aborts instead of
 * continuing with partial or zeroed hash state.
 *
 */

#ifndef POST_QUANTUM_H__
#define POST_QUANTUM_H__

#include <memory>
#include <string>
#include <string_view>
#include <array>
#include <tuple>
#include "Crypto.h"
#include "Identity.h"
#if OPENSSL_PQ && defined(LIBRESSL_VERSION_NUMBER) && I2PD_HAVE_OPENSSL_MLKEM_H
#include <openssl/mlkem.h>
#endif

#if OPENSSL_PQ

namespace i2p
{
namespace crypto
{
	enum MLKEMTypes
	{
		eMLKEM512 = 0,
		eMLKEM768,
		eMLKEM1024
	};

	constexpr size_t MLKEM512_KEY_LENGTH = 800;
	constexpr size_t MLKEM512_CIPHER_TEXT_LENGTH = 768;
	constexpr size_t MLKEM768_KEY_LENGTH = 1184;
	constexpr size_t MLKEM768_CIPHER_TEXT_LENGTH = 1088;
	constexpr size_t MLKEM1024_KEY_LENGTH = 1568;
	constexpr size_t MLKEM1024_CIPHER_TEXT_LENGTH = 1568;

	constexpr std::array MLKEMS = { std::make_tuple ("ML-KEM-512", MLKEM512_KEY_LENGTH, MLKEM512_CIPHER_TEXT_LENGTH),
		std::make_tuple ("ML-KEM-768", MLKEM768_KEY_LENGTH, MLKEM768_CIPHER_TEXT_LENGTH),
		std::make_tuple ("ML-KEM-1024", MLKEM1024_KEY_LENGTH, MLKEM1024_CIPHER_TEXT_LENGTH) };

	constexpr size_t GetMLKEMPublicKeyLen (i2p::data::CryptoKeyType type)
	{
		if (type <= i2p::data::CRYPTO_KEY_TYPE_ECIES_X25519_AEAD
			|| type - i2p::data::CRYPTO_KEY_TYPE_ECIES_X25519_AEAD > (int)MLKEMS.size ())
			return 0;
		return std::get<1> (MLKEMS[type - i2p::data::CRYPTO_KEY_TYPE_ECIES_X25519_AEAD - 1]);
	}

	constexpr size_t GetMLKEMCipherTextLen (i2p::data::CryptoKeyType type)
	{
		if (type <= i2p::data::CRYPTO_KEY_TYPE_ECIES_X25519_AEAD
			|| type - i2p::data::CRYPTO_KEY_TYPE_ECIES_X25519_AEAD > (int)MLKEMS.size ())
			return 0;
		return std::get<2> (MLKEMS[type - i2p::data::CRYPTO_KEY_TYPE_ECIES_X25519_AEAD - 1]);
	}

	/// Probe the active crypto backend once per parameter set. The probe
	/// checks wire sizes and an encapsulation/decapsulation round trip.
	/// The cached result gates advertisement and session selection.
	/// Unknown and unsupported types return false.
	bool IsMLKEMSupported (i2p::data::CryptoKeyType type);

	// True only when the active provider can perform the ML-KEM parameter
	// set selected by an NTCP2/SSU2 hybrid version. Callers use this
	// before writing a "pq" address option or choosing a hybrid session
	// version.
	bool CanPublishPQ (int version);

	// Highest configured/requested hybrid version at or below maximum that
	// the active provider supports. Returns 0 when no hybrid version
	// works.
	int HighestSupportedPQVersion (int requested, int maximum);

	/// Owns backend key material for one ML-KEM parameter set.
	/// Successful key generation enables both operations. A successfully
	/// imported public key supports encapsulation only.
	class MLKEMKeys
	{
	public:
		MLKEMKeys (MLKEMTypes type);
		~MLKEMKeys ();

		MLKEMKeys (const MLKEMKeys &) = delete;
		MLKEMKeys & operator= (const MLKEMKeys &) = delete;

		// Buffers must hold GetKeyLen(), GetCTLen(), or 32 bytes as
		// appropriate. On false, callers must abort the handshake and discard
		// outputs.
		[[nodiscard]] bool GenerateKeys ();
		[[nodiscard]] bool GetPublicKey (uint8_t * pub) const;
		[[nodiscard]] bool SetPublicKey (const uint8_t * pub);
		size_t GetKeyLen () const { return m_KeyLen; };
		size_t GetCTLen () const { return m_CTLen; };
		[[nodiscard]] bool Encaps (uint8_t * ciphertext, uint8_t * shared);
		/// Produce the candidate shared secret for the Noise handshake.
		/// Success can include implicit rejection. The subsequent AEAD
		/// authentication still has to succeed.
		[[nodiscard]] bool Decaps (const uint8_t * ciphertext, uint8_t * shared);

	private:
		void FreeKeys ();

	private:
		const std::string m_Name;
		const size_t m_KeyLen, m_CTLen;
#ifndef LIBRESSL_VERSION_NUMBER
		EVP_PKEY * m_Pkey;
#else
		MLKEM_private_key * m_Pkey;
		MLKEM_public_key * m_PublicKey;
		const int m_Rank;
#endif
	};

	std::unique_ptr<MLKEMKeys> CreateMLKEMKeys (i2p::data::CryptoKeyType type);

	// These return false for an unsupported type or a null peer key, after
	// zeroing the state. A caller that cannot use the returned state must
	// abort the handshake instead of continuing with a zero/partially
	// mixed hash.
	bool InitNoiseIKStateMLKEM (NoiseSymmetricState & state, i2p::data::CryptoKeyType type,
		const uint8_t * pub); // Noise_IK (ratchets) PQ ML-KEM
	bool InitNoiseXKStateMLKEM (
		NoiseSymmetricState & state, i2p::data::CryptoKeyType type, const uint8_t * pub); // Noise_XK (NTCP2) PQ ML-KEM
	bool InitNoiseXKStateMLKEM1 (
		NoiseSymmetricState & state, i2p::data::CryptoKeyType type, const uint8_t * pub); // Noise_XK1 (SSU2) PQ ML-KEM
}
}

#endif

#endif

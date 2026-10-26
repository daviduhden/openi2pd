/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: NTCP2.
 *
 * Implements the NTCP2 TCP transport: Noise XK handshakes with optional ML-KEM
 * hybrid key exchange, AEAD-ChaCha20-Poly1305 data framing, SipHash length
 * obfuscation, and padding/options/termination blocks. Handshakes validate
 * timestamps, low-order X25519 outputs and block sizes, while Boost.Asio
 * session state machines enforce handshake, replay, pending-connection and
 * send-queue bounds.
 *
 */

#include <openssl/rand.h>
#include <openssl/sha.h>
#include <openssl/hmac.h>
#include <stdlib.h>
#include <exception>
#include <vector>
#include <chrono>
#include <limits>
#include <cstring>
#include "Log.h"
#include "I2PEndian.h"
#include "Crypto.h"
#if OPENSSL_SIPHASH
#include <openssl/core_names.h>
#endif
#include "Siphash.h"
#include "RouterContext.h"
#include "Transports.h"
#include "NetDb.hpp"
#include "HTTP.h"
#include "Blocklist.h"
#include "util.h"
#include "Socks5.h"
#include "NTCP2.h"

#if defined(__linux__) && !defined(_NETINET_IN_H)
#include <linux/in6.h>
#endif

namespace i2p
{
namespace transport
{
	namespace
	{
		bool IsAllZero (const uint8_t * buf, size_t len)
		{
			for (size_t i = 0; i < len; ++i)
				if (buf[i]) return false;
			return true;
		}

		// RFC 7748: an all-zero X25519 output means the peer supplied a
		// low-order point and the result must not be used. OpenSSL reports some
		// of these as errors; check the output as well.
		bool AgreeAndValidate (i2p::crypto::X25519Keys & keys, const uint8_t * pub, uint8_t * shared)
		{
			return keys.Agree (pub, shared) && !IsAllZero (shared, 32);
		}
	}

	NTCP2Establisher::NTCP2Establisher ()
		: m_BufferLen (0)
		, m_IsLongPadding (false)
	{
		SetVersion (2);
	}

	bool ValidateNTCP2SessionConfirmedPayload (const uint8_t * payload, size_t len, size_t & routerInfoDataSize)
	{
		routerInfoDataSize = 0;
		if (!payload || len < 4) return false; // block header + at least the flag byte
		if (payload[0] != eNTCP2BlkRouterInfo) return false;
		size_t size = bufbe16toh (payload + 1);
		if (size < 1 || size > i2p::data::MAX_RI_BUFFER_SIZE + 1 || size > len - 3) return false;
		size_t offset = 3 + size;
		bool gotOptions = false, gotPadding = false;
		while (offset < len)
		{
			if (len - offset < 3) return false;
			uint8_t blk = payload[offset];
			size_t blockSize = bufbe16toh (payload + offset + 1);
			offset += 3;
			if (blockSize > len - offset) return false;
			if (blk == eNTCP2BlkOptions)
			{
				// Options may only appear once, before padding.
				if (gotOptions || gotPadding) return false;
				gotOptions = true;
			}
			else if (blk == eNTCP2BlkPadding)
			{
				// Padding is optional, may appear only once, and must be last.
				if (gotPadding || offset + blockSize != len) return false;
				gotPadding = true;
			}
			else
				return false; // I2NP, DateTime, Termination and unknown blocks are illegal here
			offset += blockSize;
		}
		routerInfoDataSize = size;
		return true;
	}

	void NTCP2Establisher::SetVersion (int version)
	{
#if OPENSSL_PQ
		if (version >= 3 && version <= 5 && i2p::crypto::CanPublishPQ (version))
		{
			switch (version)
			{
			case 3: m_CryptoType = i2p::data::CRYPTO_KEY_TYPE_ECIES_MLKEM512_X25519_AEAD; break;
			case 4: m_CryptoType = i2p::data::CRYPTO_KEY_TYPE_ECIES_MLKEM768_X25519_AEAD; break;
			case 5: m_CryptoType = i2p::data::CRYPTO_KEY_TYPE_ECIES_MLKEM1024_X25519_AEAD; break;
			default: break;
			}
			return;
		}
		m_CryptoType = i2p::data::CRYPTO_KEY_TYPE_ECIES_X25519_AEAD;
		m_IsLongPadding = false;
#else
		(void)version;
		m_CryptoType = i2p::data::CRYPTO_KEY_TYPE_ECIES_X25519_AEAD;
#endif
	}

	bool NTCP2Establisher::KeyDerivationFunction1 (
		const uint8_t * pub, i2p::crypto::X25519Keys & priv, const uint8_t * rs, const uint8_t * epub)
	{
#if OPENSSL_PQ
		if (m_CryptoType == i2p::data::CRYPTO_KEY_TYPE_ECIES_X25519_AEAD)
			i2p::crypto::InitNoiseXKState (*this, rs);
		else if (!i2p::crypto::InitNoiseXKStateMLKEM (*this, m_CryptoType, rs))
		{
			// An unsupported hybrid parameter set leaves the Noise state zeroed;
			// continue only with a state that was actually initialised.
			LogPrint (eLogError, "NTCP2: unsupported hybrid handshake crypto type ", (int)m_CryptoType);
			return false;
		}
#else
		i2p::crypto::InitNoiseXKState (*this, rs);
#endif
		// h = SHA256(h || epub)
		MixHash (epub, 32);
		// x25519 between pub and priv
		uint8_t inputKeyMaterial[32];
		if (!AgreeAndValidate (priv, pub, inputKeyMaterial)) return false;
		MixKey (inputKeyMaterial);
		return true;
	}

	bool NTCP2Establisher::KDF1Alice ()
	{
		return KeyDerivationFunction1 (m_RemoteStaticKey, *m_EphemeralKeys, m_RemoteStaticKey, GetPub ());
	}

	bool NTCP2Establisher::KDF1Bob ()
	{
		return KeyDerivationFunction1 (GetRemotePub (), i2p::context.GetNTCP2StaticKeys (),
			i2p::context.GetNTCP2StaticPublicKey (), GetRemotePub ());
	}

	bool NTCP2Establisher::KeyDerivationFunction2 (const uint8_t * epub)
	{
		MixHash (epub, 32);

		// x25519 between remote pub and ephemaral priv
		uint8_t inputKeyMaterial[32];
		if (!AgreeAndValidate (*m_EphemeralKeys, GetRemotePub (), inputKeyMaterial)) return false;
		MixKey (inputKeyMaterial);
		return true;
	}

	bool NTCP2Establisher::KDF2Alice ()
	{
		return KeyDerivationFunction2 (GetRemotePub ());
	}

	bool NTCP2Establisher::KDF2Bob ()
	{
		return KeyDerivationFunction2 (GetPub ());
	}

	bool NTCP2Establisher::KDF3Alice ()
	{
		uint8_t inputKeyMaterial[32];
		if (!AgreeAndValidate (i2p::context.GetNTCP2StaticKeys (), GetRemotePub (), inputKeyMaterial)) return false;
		MixKey (inputKeyMaterial);
		return true;
	}

	bool NTCP2Establisher::KDF3Bob ()
	{
		uint8_t inputKeyMaterial[32];
		if (!AgreeAndValidate (*m_EphemeralKeys, m_RemoteStaticKey, inputKeyMaterial)) return false;
		MixKey (inputKeyMaterial);
		return true;
	}

	void NTCP2Establisher::CreateEphemeralKey ()
	{
		m_EphemeralKeys = i2p::transport::transports.GetNextX25519KeysPair ();
		if (m_EphemeralKeys && !m_EphemeralKeys->HasPrivateKey ())
		{
			// A pooled pair whose generation failed must never be used: the
			// SessionRequest/SessionCreated would carry an all-zero X/Y.
			LogPrint (eLogWarning, "NTCP2: ephemeral key generation failed, retrying");
			if (!m_EphemeralKeys->GenerateKeys ())
				LogPrint (eLogError, "NTCP2: no usable ephemeral key; the handshake will abort");
		}
	}

	void NTCP2Establisher::ApplyPadding (uint8_t * padding, size_t paddingLength)
	{
		MixHash (padding, paddingLength);
	}

	bool NTCP2Establisher::CreateSessionRequestMessage (std::mt19937 & rng)
	{
		if (!HasUsableEphemeralKey ())
		{
			LogPrint (eLogError, "NTCP2: no usable ephemeral key for SessionRequest");
			return false;
		}
		// spec/ntcp2 Published Addresses: Alice must validate Bob's static key
		// and IV before connecting. A zero value is not a valid X25519 point;
		// the high bit is reserved for the hybrid PQ marker and must be clear
		// on the published static key (Java OutboundNTCP2State:252-256).
		if (IsAllZero (m_RemoteStaticKey, 32) || (m_RemoteStaticKey[31] & 0x80))
		{
			LogPrint (eLogWarning, "NTCP2: Invalid remote static key");
			return false;
		}
		if (IsAllZero (m_IV, 16))
		{
			LogPrint (eLogWarning, "NTCP2: Invalid remote IV");
			return false;
		}
		size_t offset = 0;
		// encrypt X
		i2p::crypto::CBCEncryption encryption;
		encryption.SetKey (m_RemoteIdentHash);
#if OPENSSL_PQ
		if (m_CryptoType > i2p::data::CRYPTO_KEY_TYPE_ECIES_X25519_AEAD)
		{
			uint8_t pub[32];
			memcpy (pub, GetPub (), 32);
			pub[31] |= 0x80; // set highest bit
			encryption.Encrypt (pub, 32, m_IV, m_Buffer); // X
			//  ML-KEM encap_key
			m_PQKeys = i2p::crypto::CreateMLKEMKeys (m_CryptoType);
			if (!m_PQKeys || !m_PQKeys->GenerateKeys ()) return false;
		}
		else
			encryption.Encrypt (GetPub (), 32, m_IV, m_Buffer); // X
#else
		encryption.Encrypt (GetPub (), 32, m_IV, m_Buffer); // X
#endif
		memcpy (m_IV, m_Buffer + 16, 16); // save last block as IV for SessionCreated
		offset += 32;
		// encryption key for next block
		if (!KDF1Alice ()) return false;
		size_t maxPaddingLength
			= m_IsLongPadding ? NTCP2_SESSION_HANDSHAKE_LONG_MAX_SIZE : NTCP2_SESSION_HANDSHAKE_MAX_SIZE;
		maxPaddingLength -= 64;
		size_t maxMsgSize = m_MaxMsgSize;
#if OPENSSL_PQ
		if (m_PQKeys)
		{
			// ML-KEM frame
			auto keyLen = i2p::crypto::GetMLKEMPublicKeyLen (m_CryptoType);
			std::vector<uint8_t> encapsKey (keyLen);
			if (!m_PQKeys || !m_PQKeys->GetPublicKey (encapsKey.data ())) return false;
			// encrypt encapsKey
			if (!Encrypt (encapsKey.data (), m_Buffer + offset, keyLen))
			{
				LogPrint (eLogWarning, "NTCP2: SessionRequest ML-KEM encap_key frame AEAD encryption failed ");
				return false;
			}
			MixHash (m_Buffer + offset, keyLen + 16); // h = SHA256(h || ciphertext)
			offset += keyLen + 16;
			maxPaddingLength = offset + 32; // 32 bytes following options block size
			// adjust max msg size because we might send smaller message that we can receive
			maxMsgSize = NTCP2_SESSION_HANDSHAKE_LONG_MAX_SIZE + i2p::crypto::MLKEM1024_KEY_LENGTH + 16;
			if (maxMsgSize > m_MaxMsgSize) maxMsgSize = m_MaxMsgSize;
		}
#endif
		// Keep the generated padding within the per-version maximum. The local
		// generation policy may be smaller, but must never emit a value that a
		// conforming peer is required to reject (spec/ntcp2-hybrid).
		const size_t versionMaxPadding = NTCP2GetMaxHandshakePadding (GetVersion (), false);
		if (maxPaddingLength > versionMaxPadding) maxPaddingLength = versionMaxPadding;
		// calculate padding length
		if (offset + 32 + maxPaddingLength > maxMsgSize) maxPaddingLength = maxMsgSize - offset - 32;
		auto paddingLength = maxPaddingLength ? rng () % maxPaddingLength : 0;
		// fill options
		uint8_t options[32]; // actual options size is 16 bytes
		memset (options, 0, 16);
		options[0] = static_cast<uint8_t> (i2p::context.GetNetID ()); // network ID
		options[1] = 2; // ver, always 2 regardless actual version
		htobe16buf (options + 2, static_cast<uint16_t> (paddingLength)); // padLen
		// calculate m3p2Len
		auto riBuffer = i2p::context.CopyRouterInfoBuffer ();
		auto bufLen = riBuffer->GetBufferLen ();
		size_t part2Len = bufLen + 4 + 16; // (RI header + RI + MAC for now) TODO: implement options
		if (part2Len > NTCP2_SESSION_CONFIRMED_PART2_MAX_SIZE || part2Len > std::numeric_limits<uint16_t>::max ())
		{
			LogPrint (eLogError, "NTCP2: Local RouterInfo is too large for SessionConfirmed (", part2Len, " bytes)");
			return false;
		}
		m3p2Len = static_cast<uint16_t> (part2Len);
		htobe16buf (options + 4, m3p2Len);
		// 2 bytes reserved
		htobe32buf (options + 8,
			static_cast<uint32_t> ((i2p::util::GetMillisecondsSinceEpoch () + 500) / 1000)); // tsA, rounded to seconds
		// 4 bytes reserved
		// encrypt options
		if (!Encrypt (options, m_Buffer + offset, 16))
		{
			LogPrint (eLogWarning, "NTCP2: SessionRequest options frame AEAD encryption failed");
			return false;
		}
		MixHash (m_Buffer + offset, 32);
		offset += 32;
		// padding
		if (paddingLength)
		{
			RAND_bytes (m_Buffer + offset, static_cast<int> (paddingLength));
			MixHash (m_Buffer + offset, paddingLength);
		}
		m_BufferLen = offset + paddingLength;
		// create m3p2 payload (RouterInfo block) for SessionConfirmed
		m_SessionConfirmedBuffer = std::make_unique_for_overwrite<uint8_t[]> (m3p2Len + 48); // m3p1 is 48 bytes
		uint8_t * m3p2 = m_SessionConfirmedBuffer.get () + 48;
		m3p2[0] = eNTCP2BlkRouterInfo; // block
		htobe16buf (m3p2 + 1, static_cast<uint16_t> (bufLen + 1)); // flag + RI
		m3p2[3] = 0; // flag
		memcpy (m3p2 + 4, riBuffer->data (), bufLen); // TODO: eliminate extra copy

		return true;
	}

	bool NTCP2Establisher::CreateSessionCreatedMessage (std::mt19937 & rng)
	{
		if (!HasUsableEphemeralKey ())
		{
			LogPrint (eLogError, "NTCP2: no usable ephemeral key for SessionCreated");
			return false;
		}
		size_t offset = 0;
		// encrypt Y
		i2p::crypto::CBCEncryption encryption;
		encryption.SetKey (i2p::context.GetIdentHash ());
		encryption.Encrypt (GetPub (), 32, m_IV, m_Buffer); // Y
		offset += 32;
		// encryption key for next block (m_K)
		if (!KDF2Bob ()) return false;
		size_t maxPaddingLength
			= m_IsLongPadding ? NTCP2_SESSION_HANDSHAKE_LONG_MAX_SIZE : NTCP2_SESSION_HANDSHAKE_MAX_SIZE;
		maxPaddingLength -= 64;
		size_t maxMsgSize = m_MaxMsgSize;
#if OPENSSL_PQ
		if (m_CryptoType > i2p::data::CRYPTO_KEY_TYPE_ECIES_X25519_AEAD)
		{
			if (!m_PQKeys) return false;
			size_t cipherTextLen = i2p::crypto::GetMLKEMCipherTextLen (m_CryptoType);
			std::vector<uint8_t> kemCiphertext (cipherTextLen);
			uint8_t sharedSecret[32];
			if (!m_PQKeys || !m_PQKeys->Encaps (kemCiphertext.data (), sharedSecret)) return false;
			if (!Encrypt (kemCiphertext.data (), m_Buffer + offset, cipherTextLen))
			{
				LogPrint (eLogWarning, "NTCP2: SessionCreated ML-KEM ciphertext section AEAD encryption failed");
				return false;
			}
			MixHash (m_Buffer + offset, cipherTextLen + 16); // encrypt ML-KEM frame
			MixKey (sharedSecret);
			offset += cipherTextLen + 16;
			maxPaddingLength = offset + 32; // 32 bytes following options block size
			// adjust max msg size because we might send smaller message that we can receive
			maxMsgSize = NTCP2_SESSION_HANDSHAKE_LONG_MAX_SIZE + i2p::crypto::MLKEM1024_KEY_LENGTH + 16;
			if (maxMsgSize > m_MaxMsgSize) maxMsgSize = m_MaxMsgSize;
		}
#endif
		// Per-version SessionCreated padding limit (spec/ntcp2-hybrid).
		const size_t versionMaxPadding = NTCP2GetMaxHandshakePadding (GetVersion (), true);
		if (maxPaddingLength > versionMaxPadding) maxPaddingLength = versionMaxPadding;
		// calculate padding length
		if (offset + 32 + maxPaddingLength > maxMsgSize) maxPaddingLength = maxMsgSize - offset - 32;
		auto paddingLength = maxPaddingLength ? rng () % maxPaddingLength : 0;
		uint8_t options[16];
		memset (options, 0, 16);
		htobe16buf (options + 2, static_cast<uint16_t> (paddingLength)); // padLen
		htobe32buf (options + 8,
			static_cast<uint32_t> ((i2p::util::GetMillisecondsSinceEpoch () + 500) / 1000)); // tsB, rounded to seconds
		// encrypt options
		if (!Encrypt (options, m_Buffer + offset, 16))
		{
			LogPrint (eLogWarning, "NTCP2: SessionCreated failed to encrypt options");
			return false;
		}
		MixHash (m_Buffer + offset, 32); // encrypted options
		offset += 32;
		// padding
		if (paddingLength)
		{
			RAND_bytes (m_Buffer + offset, static_cast<int> (paddingLength));
			MixHash (m_Buffer + offset, paddingLength);
		}
		m_BufferLen = offset + paddingLength;
		return true;
	}

	bool NTCP2Establisher::CreateSessionConfirmedMessagePart1 ()
	{
		// part1 48 bytes, n = 1
		if (!Encrypt (i2p::context.GetNTCP2StaticPublicKey (), m_SessionConfirmedBuffer.get (), 32))
		{
			LogPrint (eLogWarning, "NTCP2: SessionConfirmed failed to encrypt part1");
			return false;
		}
		return true;
	}

	bool NTCP2Establisher::CreateSessionConfirmedMessagePart2 ()
	{
		// part 2
		// update AD again
		MixHash (m_SessionConfirmedBuffer.get (), 48);
		// encrypt m3p2, it must be filled in SessionRequest
		if (!KDF3Alice ()) return false; // MixKey, n = 0
		uint8_t * m3p2 = m_SessionConfirmedBuffer.get () + 48;
		if (!Encrypt (m3p2, m3p2, m3p2Len - 16))
		{
			LogPrint (eLogWarning, "NTCP2: SessionConfirmed failed to encrypt part2");
			return false;
		}
		// update h again
		MixHash (m3p2, m3p2Len); //h = SHA256(h || ciphertext)
		return true;
	}

	bool NTCP2Establisher::ProcessSessionRequestMessage (
		uint16_t & paddingLen, bool & clockSkew, bool & netIdMismatch, bool & pq, bool decryptX)
	{
		clockSkew = false;
		netIdMismatch = false;
		pq = false;
		size_t offset = 0;
		if (decryptX)
		{
			// decrypt X
			auto x = GetRemotePub ();
			i2p::crypto::CBCDecryption decryption;
			decryption.SetKey (i2p::context.GetIdentHash ());
			decryption.Decrypt (m_Buffer, 32, i2p::context.GetNTCP2IV (), x);
			memcpy (m_IV, m_Buffer + 16, 16); // save last block as IV for SessionCreated
			if (x[31] & 0x80)
			{
#if OPENSSL_PQ
				if (m_CryptoType > i2p::data::CRYPTO_KEY_TYPE_ECIES_X25519_AEAD)
				{
					pq = true;
					x[31] &= 0x7F;
				}
#endif
				if (!pq)
				{
					LogPrint (eLogWarning, "NTCP2: SessionRequest ML-KEM requested but not supported");
					return false;
				}
			}
			else
				SetVersion (2); // regular x25519 requested
			// Java InboundEstablishState:318-321 and spec/ntcp2 require a valid
			// X25519 point. All-zero is not a point and makes the DH result
			// attacker-known.
			if (IsAllZero (x, 32))
			{
				LogPrint (eLogWarning, "NTCP2: SessionRequest X is zero");
				return false;
			}
			// decryption key for next block
			if (!KDF1Bob ())
			{
				LogPrint (eLogWarning, "NTCP2: SessionRequest KDF failed");
				return false;
			}
		}
		offset += 32;
#if OPENSSL_PQ
		if (pq) return true; // we need to read extra ML-KEM block first
		if (m_CryptoType > i2p::data::CRYPTO_KEY_TYPE_ECIES_X25519_AEAD)
		{
			auto keyLen = i2p::crypto::GetMLKEMPublicKeyLen (m_CryptoType);
			if (!keyLen) return false;
			std::vector<uint8_t> encapsKey (keyLen);
			if (!Decrypt (m_Buffer + offset, encapsKey.data (), keyLen))
			{
				LogPrint (eLogWarning, "NTCP2: SessionRequest ML-KEM encap_key frame AEAD decryption failed");
				return false;
			}
			MixHash (m_Buffer + offset, keyLen + 16);
			offset += keyLen + 16;
			m_PQKeys = i2p::crypto::CreateMLKEMKeys (m_CryptoType);
			if (!m_PQKeys || !m_PQKeys->SetPublicKey (encapsKey.data ())) return false;
		}
#endif
		// verify MAC and decrypt options block (32 bytes)
		uint8_t options[16];
		if (Decrypt (m_Buffer + offset, options, 16))
		{
			MixHash (m_Buffer + offset, 32);
			offset += 32;
			// options
			if (options[0] && options[0] != i2p::context.GetNetID ())
			{
				// A netID in the options block is authoritative: the peer belongs
				// to a different I2P network. The caller applies the transport ban
				// and logs once; only netId=0 (unspecified) is accepted.
				netIdMismatch = true;
				return false;
			}
			if (options[1] == 2) // ver is always 2
			{
				paddingLen = bufbe16toh (options + 2);
				// spec/ntcp2-hybrid Maximum Padding: never accept a padLen above
				// the per-version table (NTCP2GetMaxHandshakePadding).
				if (!NTCP2IsValidHandshakePadding (GetVersion (), false, paddingLen))
				{
					LogPrint (eLogWarning, "NTCP2: SessionRequest padding length ", (int)paddingLen, " exceeds ",
						NTCP2GetMaxHandshakePadding (GetVersion (), false));
					return false;
				}
				m_BufferLen = paddingLen + offset;
				// actual padding is not known yet, apply MixHash later
				if (m_BufferLen > NTCP2_SESSION_HANDSHAKE_MAX_SIZE) m_IsLongPadding = true;
				m3p2Len = bufbe16toh (options + 4);
				if (m3p2Len < NTCP2_SESSION_CONFIRMED_PART2_MIN_SIZE)
				{
					LogPrint (eLogWarning, "NTCP2: SessionRequest m3p2len=", m3p2Len, " is too short");
					return false;
				}
				if (m3p2Len > NTCP2_SESSION_CONFIRMED_PART2_MAX_SIZE)
				{
					LogPrint (eLogWarning, "NTCP2: SessionRequest m3p2len=", m3p2Len, " is too long");
					return false;
				}
				// check timestamp
				auto ts = i2p::util::GetSecondsSinceEpoch ();
				uint32_t tsA = bufbe32toh (options + 8);
				if (tsA > std::numeric_limits<uint32_t>::max () - NTCP2_CLOCK_SKEW)
				{
					LogPrint (eLogError, "NTCP2: SessionRequest tsA is too high ", tsA);
					clockSkew = true;
				}
				else if (tsA < ts - NTCP2_CLOCK_SKEW || tsA > ts + NTCP2_CLOCK_SKEW)
				{
					LogPrint (eLogWarning, "NTCP2: SessionRequest time difference ", (int)ts - (int)tsA,
						" exceeds clock skew");
					clockSkew = true;
					// we send SessionCreate to let Alice know our time and then close session
				}
			}
			else
			{
				LogPrint (eLogWarning, "NTCP2: SessionRequest version mismatch ", (int)options[1]);
				return false;
			}
		}
		else
		{
			LogPrint (eLogWarning, "NTCP2: SessionRequest AEAD verification failed ");
			return false;
		}
		return true;
	}

	bool NTCP2Establisher::ProcessSessionCreatedMessage (uint16_t & paddingLen)
	{
		size_t offset = 0;
		// decrypt Y
		i2p::crypto::CBCDecryption decryption;
		decryption.SetKey (m_RemoteIdentHash);
		decryption.Decrypt (m_Buffer + offset, 32, m_IV, GetRemotePub ());
		// Java OutboundNTCP2State:325-328 and spec/ntcp2 require a valid
		// X25519 point; reject an all-zero Y before using it in the DH.
		if (IsAllZero (GetRemotePub (), 32))
		{
			LogPrint (eLogWarning, "NTCP2: SessionCreated Y is zero");
			return false;
		}
		offset = 32;
		// decryption key for next block (m_K)
		if (!KDF2Alice ())
		{
			LogPrint (eLogWarning, "NTCP2: SessionCreated KDF failed");
			return false;
		}
#if OPENSSL_PQ
		if (m_CryptoType > i2p::data::CRYPTO_KEY_TYPE_ECIES_X25519_AEAD)
		{
			if (!m_PQKeys) return false;
			// decrypt kem_ciphertext  frame
			size_t cipherTextLen = i2p::crypto::GetMLKEMCipherTextLen (m_CryptoType);
			std::vector<uint8_t> kemCiphertext (cipherTextLen);
			if (!Decrypt (m_Buffer + offset, kemCiphertext.data (), cipherTextLen))
			{
				LogPrint (eLogWarning, "NTCP2: SessionCreated ML-KEM ciphertext section AEAD decryption failed");
				return false;
			}

			MixHash (m_Buffer + offset, cipherTextLen + 16);
			offset += cipherTextLen + 16;
			// decaps
			uint8_t sharedSecret[32];
			if (!m_PQKeys || !m_PQKeys->Decaps (kemCiphertext.data (), sharedSecret)) return false;
			MixKey (sharedSecret);
		}
#endif
		// decrypt options and verify MAC
		uint8_t options[16];
		if (Decrypt (m_Buffer + offset, options, 16))
		{
			MixHash (m_Buffer + offset, 32); // encrypted options
			// options
			paddingLen = bufbe16toh (options + 2);
			if (!NTCP2IsValidHandshakePadding (GetVersion (), true, paddingLen))
			{
				LogPrint (eLogWarning, "NTCP2: SessionCreated padding length ", (int)paddingLen, " exceeds ",
					NTCP2GetMaxHandshakePadding (GetVersion (), true));
				return false;
			}
			// actual padding is not known yet, apply MixHash later
			// check timestamp
			auto ts = i2p::util::GetSecondsSinceEpoch ();
			uint32_t tsB = bufbe32toh (options + 8);
			if (tsB > std::numeric_limits<uint32_t>::max () - NTCP2_CLOCK_SKEW)
			{
				LogPrint (eLogError, "NTCP2: SessionCreated tsB is too high ", tsB);
				return false;
			}
			if (tsB < ts - NTCP2_CLOCK_SKEW || tsB > ts + NTCP2_CLOCK_SKEW)
			{
				LogPrint (
					eLogWarning, "NTCP2: SessionCreated time difference ", (int)ts - (int)tsB, " exceeds clock skew");
				return false;
			}
			offset += 32;
		}
		else
		{
			LogPrint (eLogWarning, "NTCP2: SessionCreated AEAD verification failed ");
			return false;
		}
		m_BufferLen = offset;
		return true;
	}

	bool NTCP2Establisher::ProcessSessionConfirmedMessagePart1 ()
	{
		// decrypt S, n = 1
		if (!Decrypt (m_SessionConfirmedBuffer.get (), m_RemoteStaticKey, 32))
		{
			LogPrint (eLogWarning, "NTCP2: SessionConfirmed Part1 AEAD verification failed ");
			return false;
		}
		MixHash (m_SessionConfirmedBuffer.get (), 48);
		return true;
	}

	bool NTCP2Establisher::ProcessSessionConfirmedMessagePart2 (uint8_t * m3p2Buf)
	{
		if (!KDF3Bob ()) // MixKey, n = 0
		{
			LogPrint (eLogWarning, "NTCP2: SessionConfirmed Part2 KDF failed");
			return false;
		}
		if (Decrypt (m_SessionConfirmedBuffer.get () + 48, m3p2Buf, m3p2Len - 16))
			// calculate new h again for KDF data
			MixHash (m_SessionConfirmedBuffer.get () + 48, m3p2Len); // h = SHA256(h || ciphertext)
		else
		{
			LogPrint (eLogWarning, "NTCP2: SessionConfirmed Part2 AEAD verification failed ");
			return false;
		}
		return true;
	}

	NTCP2Session::NTCP2Session (NTCP2Server & server, std::shared_ptr<const i2p::data::RouterInfo> in_RemoteRouter,
		std::shared_ptr<const i2p::data::RouterInfo::Address> addr)
		: TransportSession (in_RemoteRouter, NTCP2_ESTABLISH_TIMEOUT)
		, m_Server (server)
		, m_Socket (m_Server.GetService ())
		, m_ReadTimer (m_Server.GetService ())
		, m_IsEstablished (false)
		, m_IsTerminated (false)
		, m_IsClockSkew (false)
		, m_Establisher (new NTCP2Establisher)
		, m_SendKey (nullptr)
		, m_ReceiveKey (nullptr)
		,
#if OPENSSL_SIPHASH
		m_SendMDCtx (nullptr)
		, m_ReceiveMDCtx (nullptr)
		,
#else
		m_SendSipKey (nullptr)
		, m_ReceiveSipKey (nullptr)
		,
#endif
		m_NextReceivedLen (0)
		, m_NextReceivedBufferSize (0)
		, m_ReceiveSequenceNumber (0)
		, m_SendSequenceNumber (0)
		, m_IsSending (false)
		, m_IsReceiving (false)
		, m_IsTerminating (false)
		, m_PendingTerminationReason (-1)
		, m_PendingRouterInfo (false)
		, m_NextRouterInfoResendTime (0)
		, m_NextPaddingSize (16)
	{
		if (in_RemoteRouter) // Alice
		{
			m_Establisher->m_RemoteIdentHash = GetRemoteIdentity ()->GetIdentHash ();
			if (addr)
			{
				memcpy (m_Establisher->m_RemoteStaticKey, addr->s, 32);
				memcpy (m_Establisher->m_IV, addr->i, 16);
				m_RemoteEndpoint = boost::asio::ip::tcp::endpoint (addr->host, static_cast<uint16_t> (addr->port));
#if OPENSSL_PQ
				if (m_Server.GetVersion () > 2) // we support post quantum in config
					m_Establisher->SetVersion (addr->v);
#endif
				// Long padding is a local generation policy, not a wire
				// requirement: it widens the random padding range only for a
				// peer that publishes a hybrid version and is at least 0.9.69.
				// Every emitted value is still clamped to the per-version
				// maximum (NTCP2GetMaxHandshakePadding), so a conforming peer
				// is never required to reject our handshake.
				if (addr->v > 2 && in_RemoteRouter->GetVersion () >= MAKE_VERSION_NUMBER (0, 9, 69)) // 0.9.69
					m_Establisher->m_IsLongPadding = true;
			}
			else
				LogPrint (eLogWarning, "NTCP2: Missing NTCP2 address");
		}
	}

	NTCP2Session::~NTCP2Session ()
	{
#if OPENSSL_SIPHASH
		if (m_SendMDCtx) EVP_MD_CTX_free (m_SendMDCtx);
		if (m_ReceiveMDCtx) EVP_MD_CTX_free (m_ReceiveMDCtx);
#endif
		OPENSSL_cleanse (m_Kab, sizeof (m_Kab));
		OPENSSL_cleanse (m_Kba, sizeof (m_Kba));
		OPENSSL_cleanse (m_Sipkeysab, sizeof (m_Sipkeysab));
		OPENSSL_cleanse (m_Sipkeysba, sizeof (m_Sipkeysba));
	}

	void NTCP2Session::Terminate ()
	{
		bool isTerminated = m_IsTerminated.exchange (true);
		if (!isTerminated)
		{
			m_IsEstablished = false;
			// A data-phase failure drain and the per-frame read deadline must not
			// outlive the session; the handlers are idempotent but the timer keeps
			// a shared reference to this session.
			m_FailureDrain.Reset ();
			m_ReadTimer.cancel ();
			boost::system::error_code ec;
			m_Socket.shutdown (boost::asio::ip::tcp::socket::shutdown_both, ec);
			if (ec) LogPrint (eLogDebug, "NTCP2: Couldn't shutdown socket: ", ec.message ());
			m_Socket.close ();
			transports.PeerDisconnected (shared_from_this ());
			m_Server.RemoveNTCP2Session (shared_from_this ());
			{
				std::lock_guard<std::mutex> l (m_IntermediateQueueMutex);
				if (!m_IntermediateQueue.empty ()) m_SendQueue.splice (m_SendQueue.end (), m_IntermediateQueue);
			}
			for (auto & it : m_SendQueue) it->Drop ();
			m_SendQueue.clear ();
			SetSendQueueSize (0);
			auto remoteIdentity = GetRemoteIdentity ();
			if (remoteIdentity)
			{
				LogPrint (eLogDebug, "NTCP2: Session with ", GetRemoteEndpoint (), " (",
					i2p::data::GetIdentHashAbbreviation (remoteIdentity->GetIdentHash ()), ") terminated");
			}
			else
			{
				LogPrint (eLogDebug, "NTCP2: Session with ", GetRemoteEndpoint (), " terminated");
			}
		}
	}

	void NTCP2Session::Close ()
	{
		m_FailureDrain.Reset ();
		m_ReadTimer.cancel ();
		m_Socket.close ();
	}

	void NTCP2Session::TerminateByTimeout ()
	{
		// The failure drain owns the close while it is pending; its own deadline
		// ends it within NTCP2_AEAD_FAILURE_DRAIN_TIMEOUT seconds.
		if (m_FailureDrain.IsDraining ()) return;
		// If a termination frame is already pending on a stalled write, force
		// the close on the next timeout sweep instead of waiting forever.
		if (m_IsTerminating || m_PendingTerminationReason >= 0)
		{
			// Defer the close: the termination timer may currently be iterating
			// m_NTCP2Sessions, which Terminate() modifies.
			boost::asio::post (m_Server.GetService (), std::bind (&NTCP2Session::Terminate, shared_from_this ()));
			return;
		}
		SendTerminationAndTerminate (eNTCP2IdleTimeout);
	}

	void NTCP2Session::Done ()
	{
		boost::asio::post (m_Server.GetService (), std::bind (&NTCP2Session::Terminate, shared_from_this ()));
	}

	void NTCP2Session::Established ()
	{
		if (IsTerminated ()) return;
		m_IsEstablished = true;
		if (IsTerminated ())
		{
			m_IsEstablished = false;
			return;
		}
		m_Version = (uint8_t)m_Establisher->m_CryptoType - 2;
		m_Establisher.reset (nullptr);
		SetTerminationTimeout (NTCP2_TERMINATION_TIMEOUT + m_Server.GetRng () () % NTCP2_TERMINATION_TIMEOUT_VARIANCE);
		m_NextRouterInfoResendTime = i2p::util::GetSecondsSinceEpoch () + NTCP2_ROUTERINFO_RESEND_INTERVAL
			+ m_Server.GetRng () () % NTCP2_ROUTERINFO_RESEND_INTERVAL_VARIANCE;
		SendQueue ();
		transports.PeerConnected (shared_from_this ());
	}

	// The AEAD nonce is the 64-bit frame sequence number in big-endian order
	// with a zeroed 4-byte prefix (spec/ntcp2 framing). The counter is
	// per-direction and only increments after the AEAD operation succeeded, so a
	// failed frame never advances the expected nonce.
	void NTCP2Session::CreateNonce (uint64_t seqn, uint8_t * nonce)
	{
		memset (nonce, 0, 4);
		htole64buf (nonce + 4, seqn);
	}

	void NTCP2Session::CreateNextReceivedBuffer (size_t size)
	{
		if (m_NextReceivedBuffer && size <= m_NextReceivedBufferSize) return; // buffer is good, do nothing
		m_NextReceivedBuffer = std::make_unique_for_overwrite<uint8_t[]> (size);
		m_NextReceivedBufferSize = size;
	}

	void NTCP2Session::DeleteNextReceiveBuffer (uint64_t ts)
	{
		if (m_NextReceivedBuffer && !m_IsReceiving
			&& ts > GetLastActivityTimestamp () + NTCP2_RECEIVE_BUFFER_DELETION_TIMEOUT)
		{
			m_NextReceivedBuffer.reset ();
			m_NextReceivedBufferSize = 0;
		}
	}

	/**
	 * @brief Derive the data-phase AEAD and SipHash keys from the final Noise chaining key.
	 *
	 * The chain is normative (spec/ntcp2, data phase key derivation):
	 * k_ab and k_ba come from HKDF(ck), ask_master from HKDF(ck, info="ask")
	 * and sip_master from HKDF(ask_master, h || "siphash"). Bob sends with k_ba
	 * and receives with k_ab; Alice does the reverse. Never derive one
	 * direction from the other and never reuse a chaining key after this call.
	 *
	 * @warning Runs on the session thread exactly once, after the final
	 *          handshake message was authenticated; the derived arrays are
	 *          cleansed in the destructor.
	 */
	void NTCP2Session::KeyDerivationFunctionDataPhase ()
	{
		uint8_t k[64];
		i2p::crypto::HKDF (m_Establisher->GetCK (), nullptr, 0, "", k); // k_ab, k_ba = HKDF(ck, zerolen)
		memcpy (m_Kab, k, 32);
		memcpy (m_Kba, k + 32, 32);
		uint8_t master[32];
		i2p::crypto::HKDF (
			m_Establisher->GetCK (), nullptr, 0, "ask", master, 32); // ask_master = HKDF(ck, zerolen, info="ask")
		uint8_t h[39];
		memcpy (h, m_Establisher->GetH (), 32);
		memcpy (h + 32, "siphash", 7);
		i2p::crypto::HKDF (master, h, 39, "", master, 32); // sip_master = HKDF(ask_master, h || "siphash")
		i2p::crypto::HKDF (master, nullptr, 0, "", k); // sipkeys_ab, sipkeys_ba = HKDF(sip_master, zerolen)
		memcpy (m_Sipkeysab, k, 32);
		memcpy (m_Sipkeysba, k + 32, 32);
	}

	void NTCP2Session::SendSessionRequest ()
	{
		if (!m_Establisher->CreateSessionRequestMessage (m_Server.GetEstablisherRng ()))
		{
			LogPrint (eLogWarning, "NTCP2: Send SessionRequest KDF failed");
			boost::asio::post (m_Server.GetService (), std::bind (&NTCP2Session::Terminate, shared_from_this ()));
			return;
		}
		// send message
		m_HandshakeInterval = i2p::util::GetMillisecondsSinceEpoch ();
		boost::asio::async_write (m_Socket, boost::asio::buffer (m_Establisher->m_Buffer, m_Establisher->m_BufferLen),
			boost::asio::transfer_all (),
			std::bind (&NTCP2Session::HandleSessionRequestSent, shared_from_this (), std::placeholders::_1,
				std::placeholders::_2));
	}

	void NTCP2Session::HandleSessionRequestSent (const boost::system::error_code & ecode, std::size_t bytes_transferred)
	{
		(void)bytes_transferred;
		if (ecode)
		{
			LogPrint (eLogWarning, "NTCP2: Couldn't send SessionRequest message: ", ecode.message ());
			Terminate ();
		}
		else
		{
			// we receive first 64 bytes (32 Y, and 32 ChaCha/Poly frame) first and ML-KEM frame if post quantum
			size_t len = 64;
#if OPENSSL_PQ
			if (m_Establisher->m_CryptoType > i2p::data::CRYPTO_KEY_TYPE_ECIES_X25519_AEAD)
				len += i2p::crypto::GetMLKEMCipherTextLen (m_Establisher->m_CryptoType) + 16;
#endif
			boost::asio::async_read (m_Socket, boost::asio::buffer (m_Establisher->m_Buffer, len),
				boost::asio::transfer_all (),
				std::bind (&NTCP2Session::HandleSessionCreatedReceived, shared_from_this (), std::placeholders::_1,
					std::placeholders::_2));
		}
	}

	void NTCP2Session::HandleSessionRequestReceived (
		const boost::system::error_code & ecode, std::size_t bytes_transferred)
	{
		if (ecode)
		{
			LogPrint (eLogWarning, "NTCP2: SessionRequest read error: ", ecode.message ());
			Terminate ();
		}
		else
		{
			m_Establisher->CreateEphemeralKey ();
			boost::asio::post (m_Server.GetEstablisherService (),
				[s = shared_from_this (), bytes_transferred] () { s->ProcessSessionRequest (bytes_transferred); });
		}
	}

	void NTCP2Session::ProcessSessionRequest (size_t len, bool first)
	{
		LogPrint (eLogDebug, "NTCP2: SessionRequest ", first ? "received " : "updated ", len);
		if (first && m_Server.IsHandshakeReplay (m_Establisher->m_Buffer))
		{
			LogPrint (eLogWarning, "NTCP2: Replayed SessionRequest from ", m_RemoteEndpoint);
			ReadSomethingAndTerminate ();
			return;
		}
		uint16_t paddingLen = 0;
		bool clockSkew = false, netIdMismatch = false, pq = false;
		if (m_Establisher->ProcessSessionRequestMessage (paddingLen, clockSkew, netIdMismatch, pq, first))
		{
			// spec/ntcp2 SessionRequest: a peer whose tsA is outside ±60 s still
			// gets SessionCreated, so Alice learns our time, but the session must
			// not be established. Record it and continue the normal message 2
			// exchange; EstablishSessionAfterSessionConfirmed() terminates with
			// reason 7 once SessionConfirmed arrived (bounded by
			// NTCP2_ESTABLISH_TIMEOUT) and the message 2 write has completed.
			// Closing here instead could drop SessionCreated before it flushes.
			if (clockSkew)
			{
				m_IsClockSkew = true;
				LogPrint (eLogWarning, "NTCP2: Clock skew from ", m_RemoteEndpoint,
					"; answering SessionRequest and terminating after SessionConfirmed");
			}
#if OPENSSL_PQ
			if (pq)
			{
				auto keyLen = i2p::crypto::GetMLKEMPublicKeyLen (m_Establisher->m_CryptoType);
				boost::asio::async_read (m_Socket, boost::asio::buffer (m_Establisher->m_Buffer + 64, keyLen + 16),
					boost::asio::transfer_all (),
					std::bind (&NTCP2Session::HandleSessionRequestMLKEMReceived, shared_from_this (),
						std::placeholders::_1, std::placeholders::_2));
				return;
			}
#endif
			if (paddingLen > 0)
			{
				if (len + paddingLen <= m_Establisher->m_MaxMsgSize)
				{
					boost::asio::async_read (m_Socket, boost::asio::buffer (m_Establisher->m_Buffer + len, paddingLen),
						boost::asio::transfer_all (),
						std::bind (&NTCP2Session::HandleSessionRequestPaddingReceived, shared_from_this (),
							std::placeholders::_1, std::placeholders::_2));
				}
				else
				{
					LogPrint (eLogWarning, "NTCP2: SessionRequest padding length ", (int)paddingLen, " is too long");
					boost::asio::post (
						m_Server.GetService (), std::bind (&NTCP2Session::Terminate, shared_from_this ()));
				}
			}
			else if (HasPendingIncomingData ())
			{
				// spec/ntcp2 SessionRequest: Bob must fail if any incoming data
				// remains after validating message 1 and its padding.
				LogPrint (eLogWarning, "NTCP2: Extra data after SessionRequest from ", m_RemoteEndpoint);
				ReadSomethingAndTerminate ();
			}
			else
				SendSessionCreated ();
		}
		else
		{
			if (netIdMismatch && !m_RemoteEndpoint.address ().is_unspecified ())
			{
				// A SessionRequest netID that is neither 0 nor ours means the peer
				// is on another I2P network; ban the source at the transport level
				// so it cannot keep occupying handshake slots. Logged once here
				// instead of once per message, the parser does not log it at all.
				LogPrint (eLogWarning, "NTCP2: SessionRequest network ID mismatch from ", m_RemoteEndpoint,
					". Expected ", i2p::context.GetNetID (), "; banning address");
				i2p::transport::transports.AddBan (m_RemoteEndpoint.address ());
			}
			ReadSomethingAndTerminate (); // probing resistance
		}
	}

	void NTCP2Session::HandleSessionRequestPaddingReceived (
		const boost::system::error_code & ecode, std::size_t bytes_transferred)
	{
		if (ecode)
		{
			LogPrint (eLogWarning, "NTCP2: SessionRequest padding read error: ", ecode.message ());
			Terminate ();
		}
		else
		{
			boost::asio::post (m_Server.GetEstablisherService (),
				[s = shared_from_this (), paddingLength = bytes_transferred] ()
				{
					if (paddingLength < s->m_Establisher->m_BufferLen)
						s->m_Establisher->ApplyPadding (
							s->m_Establisher->m_Buffer + s->m_Establisher->m_BufferLen - paddingLength, paddingLength);
					if (s->HasPendingIncomingData ())
					{
						LogPrint (eLogWarning, "NTCP2: Extra data after SessionRequest padding from ",
							s->GetRemoteEndpoint ());
						boost::asio::post (s->m_Server.GetService (), std::bind (&NTCP2Session::Terminate, s));
					}
					else
						s->SendSessionCreated ();
				});
		}
	}

#if OPENSSL_PQ
	void NTCP2Session::HandleSessionRequestMLKEMReceived (
		const boost::system::error_code & ecode, std::size_t bytes_transferred)
	{
		if (ecode)
		{
			LogPrint (eLogWarning, "NTCP2: SessionRequest ML-KEM read error: ", ecode.message ());
			Terminate ();
		}
		else
		{
			boost::asio::post (m_Server.GetEstablisherService (), [s = shared_from_this (), bytes_transferred] ()
				{ s->ProcessSessionRequest (bytes_transferred + 64, false); });
		}
	}
#endif

	void NTCP2Session::SendSessionCreated ()
	{
		if (!m_Establisher->CreateSessionCreatedMessage (m_Server.GetEstablisherRng ()))
		{
			LogPrint (eLogWarning, "NTCP2: Send SessionCreated KDF failed");
			boost::asio::post (m_Server.GetService (), std::bind (&NTCP2Session::Terminate, shared_from_this ()));
			return;
		}
		// send message
		m_HandshakeInterval = i2p::util::GetMillisecondsSinceEpoch ();
		boost::asio::async_write (m_Socket, boost::asio::buffer (m_Establisher->m_Buffer, m_Establisher->m_BufferLen),
			boost::asio::transfer_all (),
			std::bind (&NTCP2Session::HandleSessionCreatedSent, shared_from_this (), std::placeholders::_1,
				std::placeholders::_2));
	}

	void NTCP2Session::HandleSessionCreatedReceived (
		const boost::system::error_code & ecode, std::size_t bytes_transferred)
	{
		if (ecode)
		{
			LogPrint (eLogWarning, "NTCP2: SessionCreated read error: ", ecode.message ());
			Terminate ();
		}
		else
		{
			m_HandshakeInterval = i2p::util::GetMillisecondsSinceEpoch () - m_HandshakeInterval;
			boost::asio::post (m_Server.GetEstablisherService (),
				[s = shared_from_this (), bytes_transferred] () { s->ProcessSessionCreated (bytes_transferred); });
		}
	}

	void NTCP2Session::ProcessSessionCreated (size_t len)
	{
		LogPrint (eLogDebug, "NTCP2: SessionCreated received ", len);
		if (m_Server.IsHandshakeReplayOutgoing (m_Establisher->m_Buffer))
		{
			LogPrint (eLogWarning, "NTCP2: Replayed SessionCreated from ", m_RemoteEndpoint);
			boost::asio::post (m_Server.GetService (), std::bind (&NTCP2Session::Terminate, shared_from_this ()));
			return;
		}
		uint16_t paddingLen = 0;
		if (m_Establisher->ProcessSessionCreatedMessage (paddingLen))
		{
			if (paddingLen > 0)
			{
				if (m_Establisher->m_BufferLen + paddingLen <= m_Establisher->m_MaxMsgSize)
				{
					boost::asio::async_read (m_Socket,
						boost::asio::buffer (m_Establisher->m_Buffer + m_Establisher->m_BufferLen, paddingLen),
						boost::asio::transfer_all (),
						std::bind (&NTCP2Session::HandleSessionCreatedPaddingReceived, shared_from_this (),
							std::placeholders::_1, std::placeholders::_2));
				}
				else
				{
					LogPrint (eLogWarning, "NTCP2: SessionCreated padding length ", (int)paddingLen, " is too long");
					boost::asio::post (
						m_Server.GetService (), std::bind (&NTCP2Session::Terminate, shared_from_this ()));
				}
			}
			else if (HasPendingIncomingData ())
			{
				// spec/ntcp2 SessionCreated: Alice must fail if any incoming data
				// remains after validating message 2 and its padding.
				LogPrint (eLogWarning, "NTCP2: Extra data after SessionCreated from ", m_RemoteEndpoint);
				boost::asio::post (m_Server.GetService (), std::bind (&NTCP2Session::Terminate, shared_from_this ()));
			}
			else
				SendSessionConfirmed ();
		}
		else
		{
			if (GetRemoteIdentity ())
				i2p::data::netdb.SetUnreachable (GetRemoteIdentity ()->GetIdentHash (), true); // assume wrong s key
			boost::asio::post (m_Server.GetService (), std::bind (&NTCP2Session::Terminate, shared_from_this ()));
		}
	}

	void NTCP2Session::HandleSessionCreatedPaddingReceived (
		const boost::system::error_code & ecode, std::size_t bytes_transferred)
	{
		if (ecode)
		{
			LogPrint (eLogWarning, "NTCP2: SessionCreated padding read error: ", ecode.message ());
			Terminate ();
		}
		else
		{
			boost::asio::post (m_Server.GetEstablisherService (),
				[s = shared_from_this (), paddingLength = bytes_transferred] ()
				{
					s->m_Establisher->ApplyPadding (
						s->m_Establisher->m_Buffer + s->m_Establisher->m_BufferLen, paddingLength);
					s->m_Establisher->m_BufferLen += paddingLength;
					if (s->HasPendingIncomingData ())
					{
						LogPrint (eLogWarning, "NTCP2: Extra data after SessionCreated padding from ",
							s->GetRemoteEndpoint ());
						boost::asio::post (s->m_Server.GetService (), std::bind (&NTCP2Session::Terminate, s));
					}
					else
						s->SendSessionConfirmed ();
				});
		}
	}

	void NTCP2Session::SendSessionConfirmed ()
	{
		if (!m_Establisher->CreateSessionConfirmedMessagePart1 ())
		{
			boost::asio::post (m_Server.GetService (), std::bind (&NTCP2Session::Terminate, shared_from_this ()));
			return;
		}
		if (!m_Establisher->CreateSessionConfirmedMessagePart2 ())
		{
			LogPrint (eLogWarning, "NTCP2: Send SessionConfirmed Part2 KDF failed");
			boost::asio::post (m_Server.GetService (), std::bind (&NTCP2Session::Terminate, shared_from_this ()));
			return;
		}
		// send message
		boost::asio::async_write (m_Socket,
			boost::asio::buffer (m_Establisher->m_SessionConfirmedBuffer.get (), m_Establisher->m3p2Len + 48),
			boost::asio::transfer_all (),
			std::bind (&NTCP2Session::HandleSessionConfirmedSent, shared_from_this (), std::placeholders::_1,
				std::placeholders::_2));
	}

	void NTCP2Session::HandleSessionConfirmedSent (
		const boost::system::error_code & ecode, std::size_t bytes_transferred)
	{
		(void)bytes_transferred;
		if (ecode)
		{
			LogPrint (eLogWarning, "NTCP2: Couldn't send SessionConfirmed message: ", ecode.message ());
			Terminate ();
		}
		else
		{
			LogPrint (eLogDebug, "NTCP2: SessionConfirmed sent");
			KeyDerivationFunctionDataPhase ();
			// Alice data phase keys
			m_SendKey = m_Kab;
			m_ReceiveKey = m_Kba;
			if (!SetSipKeys (m_Sipkeysab, m_Sipkeysba))
			{
				LogPrint (eLogError, "NTCP2: SipHash provider is unavailable");
				Terminate ();
				return;
			}
			memcpy (m_ReceiveIV.buf, m_Sipkeysba + 16, 8);
			memcpy (m_SendIV.buf, m_Sipkeysab + 16, 8);
			Established ();
			ReceiveLength ();

			// TODO: remove
			// m_SendQueue.push_back (CreateDeliveryStatusMsg (1));
			// SendQueue ();
		}
	}

	void NTCP2Session::HandleSessionCreatedSent (const boost::system::error_code & ecode, std::size_t bytes_transferred)
	{
		(void)bytes_transferred;
		if (ecode)
		{
			LogPrint (eLogWarning, "NTCP2: Couldn't send SessionCreated message: ", ecode.message ());
			Terminate ();
		}
		else
		{
			LogPrint (eLogDebug, "NTCP2: SessionCreated sent");
			m_Establisher->m_SessionConfirmedBuffer
				= std::make_unique_for_overwrite<uint8_t[]> (m_Establisher->m3p2Len + 48);
			boost::asio::async_read (m_Socket,
				boost::asio::buffer (m_Establisher->m_SessionConfirmedBuffer.get (), m_Establisher->m3p2Len + 48),
				boost::asio::transfer_all (),
				std::bind (&NTCP2Session::HandleSessionConfirmedReceived, shared_from_this (), std::placeholders::_1,
					std::placeholders::_2));
		}
	}

	void NTCP2Session::HandleSessionConfirmedReceived (
		const boost::system::error_code & ecode, std::size_t bytes_transferred)
	{
		(void)bytes_transferred;
		if (ecode)
		{
			LogPrint (eLogWarning, "NTCP2: SessionConfirmed read error: ", ecode.message ());
			Terminate ();
		}
		else
		{
			m_HandshakeInterval = i2p::util::GetMillisecondsSinceEpoch () - m_HandshakeInterval;
			boost::asio::post (
				m_Server.GetEstablisherService (), [s = shared_from_this ()] () { s->ProcessSessionConfirmed (); });
		}
	}

	void NTCP2Session::ProcessSessionConfirmed ()
	{
		// run on establisher thread
		LogPrint (eLogDebug, "NTCP2: SessionConfirmed received");
		// part 1
		if (m_Establisher->ProcessSessionConfirmedMessagePart1 ())
		{
			// part 2
			auto buf = std::make_shared<std::vector<uint8_t>> (m_Establisher->m3p2Len - 16); // -MAC
			if (m_Establisher->ProcessSessionConfirmedMessagePart2 (buf->data ())) // TODO:handle in establisher thread
			{
				// payload
				// RI block must be first, then optional Options and (last) Padding.
				size_t size = 0;
				if (!ValidateNTCP2SessionConfirmedPayload (buf->data (), buf->size (), size))
				{
					LogPrint (eLogWarning, "NTCP2: Malformed SessionConfirmed payload");
					boost::asio::post (
						m_Server.GetService (), std::bind (&NTCP2Session::Terminate, shared_from_this ()));
					return;
				}
				boost::asio::post (m_Server.GetService (),
					[s = shared_from_this (), buf, size] () { s->EstablishSessionAfterSessionConfirmed (buf, size); });
			}
			else
				boost::asio::post (m_Server.GetService (), std::bind (&NTCP2Session::Terminate, shared_from_this ()));
		}
		else
			boost::asio::post (m_Server.GetService (), std::bind (&NTCP2Session::Terminate, shared_from_this ()));
	}

	void NTCP2Session::EstablishSessionAfterSessionConfirmed (std::shared_ptr<std::vector<uint8_t>> buf, size_t size)
	{
		// run on main NTCP2 thread
		KeyDerivationFunctionDataPhase ();
		// Bob data phase keys
		m_SendKey = m_Kba;
		m_ReceiveKey = m_Kab;
		if (!SetSipKeys (m_Sipkeysba, m_Sipkeysab))
		{
			LogPrint (eLogError, "NTCP2: SipHash provider is unavailable");
			Terminate ();
			return;
		}
		memcpy (m_ReceiveIV.buf, m_Sipkeysab + 16, 8);
		memcpy (m_SendIV.buf, m_Sipkeysba + 16, 8);
		// we need to set keys for SendTerminationAndTerminate
		// TODO: check flag
		i2p::data::RouterInfo ri (buf->data () + 4, size - 1); // 1 byte block type + 2 bytes size + 1 byte flag
		if (ri.HasForeignNetId ())
		{
			// The signed properties carry a non-zero netId that is not ours, so
			// this RouterInfo belongs to another I2P network. It is rejected like
			// any malformed RI (IsUnreachable stays set), but the reason is
			// distinct and the source is banned at the transport level.
			LogPrint (
				eLogError, "NTCP2: RouterInfo with foreign netId in SessionConfirmed from ", GetRemoteEndpoint ());
			auto ident = ri.GetIdentHash ();
			i2p::data::netdb.SetUnreachable (ident, true);
			if (!m_RemoteEndpoint.address ().is_unspecified ())
				i2p::transport::transports.AddBan (m_RemoteEndpoint.address ());
			SendTerminationAndTerminate (eNTCP2Banned);
			return;
		}
		if (ri.IsUnreachable ())
		{
			LogPrint (
				eLogError, "NTCP2: RouterInfo verification failed in SessionConfirmed from ", GetRemoteEndpoint ());
			SendTerminationAndTerminate (eNTCP2RouterInfoSignatureVerificationFail);
			return;
		}
		LogPrint (eLogDebug, "NTCP2: SessionConfirmed from ", GetRemoteEndpoint (), " (",
			i2p::data::GetIdentHashAbbreviation (ri.GetIdentHash ()), ")");
		// SessionRequest was outside the ±NTCP2_CLOCK_SKEW window. SessionCreated
		// was sent (so Alice could correct her clock) and has been flushed: message
		// 3 was only read after its write completed. Now terminate with reason 7
		// and record the peer in its profile so retries are not immediate.
		// RouterProfile has no dedicated skew counter, so the existing
		// unreachable marking is reused; it expires with the profile.
		if (m_IsClockSkew)
		{
			LogPrint (
				eLogWarning, "NTCP2: Clock skew with ", GetRemoteEndpoint (), "; terminating after SessionConfirmed");
			i2p::data::UpdateRouterProfile (ri.GetIdentHash (),
				[] (std::shared_ptr<i2p::data::RouterProfile> profile)
				{
					if (profile) profile->Unreachable (true);
				});
			SendTerminationAndTerminate (eNTCP2ClockSkew);
			return;
		}
		auto ts = i2p::util::GetMillisecondsSinceEpoch ();
		if (ts > ri.GetTimestamp () + i2p::data::NETDB_MIN_EXPIRATION_TIMEOUT * 1000LL) // 90 minutes
		{
			LogPrint (eLogError, "NTCP2: RouterInfo is too old in SessionConfirmed for ",
				(ts - ri.GetTimestamp ()) / 1000LL, " seconds");
			SendTerminationAndTerminate (eNTCP2Message3Error);
			return;
		}
		if (ts + i2p::data::NETDB_EXPIRATION_TIMEOUT_THRESHOLD * 1000LL < ri.GetTimestamp ()) // 2 minutes
		{
			LogPrint (
				eLogError, "NTCP2: RouterInfo is from future for ", (ri.GetTimestamp () - ts) / 1000LL, " seconds");
			SendTerminationAndTerminate (eNTCP2Message3Error);
			return;
		}
		if (ri.GetVersion () < i2p::data::NETDB_MIN_ALLOWED_VERSION && !ri.IsHighBandwidth ())
		{
			LogPrint (eLogInfo, "NTCP2: Router version ", ri.GetVersion (), " is too old in SessionConfirmed");
			SendTerminationAndTerminate (eNTCP2Banned);
			return;
		}
		// update RouterInfo in netdb
		auto ri1 = i2p::data::netdb.AddRouterInfo (
			ri.GetBuffer (), static_cast<int> (ri.GetBufferLen ())); // ri1 points to one from netdb now
		if (!ri1)
		{
			LogPrint (eLogError, "NTCP2: Couldn't update RouterInfo from SessionConfirmed in netdb");
			Terminate ();
			return;
		}

		bool isOlder = false;
		if (ri.GetTimestamp () + i2p::data::NETDB_EXPIRATION_TIMEOUT_THRESHOLD * 1000LL < ri1->GetTimestamp ())
		{
			// received RouterInfo is older than one in netdb
			isOlder = true;
			if (ri1->HasProfile ())
			{
				auto profile = i2p::data::GetRouterProfile (ri1->GetIdentHash ()); // retrieve profile
				if (profile && profile->IsDuplicated ())
				{
					SendTerminationAndTerminate (eNTCP2Banned);
					return;
				}
			}
		}

		auto addr = m_RemoteEndpoint.address ().is_v4 ()
			? ri1->GetNTCP2V4Address ()
			: (i2p::util::net::IsYggdrasilAddress (m_RemoteEndpoint.address ()) ? ri1->GetYggdrasilAddress ()
																				: ri1->GetNTCP2V6Address ());
		if (!addr)
		{
			LogPrint (eLogError, "NTCP2: Address not found in SessionConfirmed");
			Terminate ();
			return;
		}
		if (addr->IsPublishedNTCP2 () && m_RemoteEndpoint.address () != addr->host
			&& (!m_RemoteEndpoint.address ().is_v6 ()
				|| (i2p::util::net::IsYggdrasilAddress (m_RemoteEndpoint.address ())
						? memcmp (m_RemoteEndpoint.address ().to_v6 ().to_bytes ().data () + 1,
							  addr->host.to_v6 ().to_bytes ().data () + 1, 7)
						: // from the same yggdrasil subnet
						memcmp (m_RemoteEndpoint.address ().to_v6 ().to_bytes ().data (),
							addr->host.to_v6 ().to_bytes ().data (), 8)))) // temporary address
		{
			if (isOlder) // older router?
				i2p::data::UpdateRouterProfile (ri1->GetIdentHash (),
					[] (std::shared_ptr<i2p::data::RouterProfile> profile)
					{
						if (profile) profile->Duplicated (); // mark router as duplicated in profile
					});
			else
				LogPrint (eLogInfo, "NTCP2: Host mismatch between published address ", addr->host,
					" and actual endpoint ", m_RemoteEndpoint.address ());
			SendTerminationAndTerminate (eNTCP2Banned);
			return;
		}
		if (memcmp (m_Establisher->m_RemoteStaticKey, addr->s, 32))
		{
			LogPrint (eLogError, "NTCP2: Wrong static key in SessionConfirmed");
			if (addr->IsPublishedNTCP2 ()) i2p::transport::transports.AddBan (m_RemoteEndpoint.address ());
			Terminate ();
			return;
		}
		// TODO: process options block

		// ready to communicate
		SetRemoteIdentity (ri1->GetRouterIdentity ());
		if (m_Server.AddNTCP2Session (shared_from_this (), true))
		{
			Established ();
			if (ri1->GetCongestion () == i2p::data::RouterInfo::eRejectAll)
			{
				auto terminationTimeout = GetTerminationTimeout () / 2;
				if (terminationTimeout < NTCP2_ESTABLISH_TIMEOUT) terminationTimeout = NTCP2_ESTABLISH_TIMEOUT;
				SetTerminationTimeout (terminationTimeout);
			}
			ReceiveLength ();
		}
		else
			Terminate ();
	}

	bool NTCP2Session::SetSipKeys (const uint8_t * sendSipKey, const uint8_t * receiveSipKey)
	{
		if (!sendSipKey || !receiveSipKey) return false;
#if OPENSSL_SIPHASH
		auto makeContext = [] (const uint8_t * key) -> EVP_MD_CTX *
		{
			EVP_PKEY * sipKey = EVP_PKEY_new_raw_private_key (EVP_PKEY_SIPHASH, nullptr, key, 16);
			if (!sipKey) return nullptr;
			EVP_MD_CTX * md = EVP_MD_CTX_new ();
			EVP_PKEY_CTX * pkeyCtx = nullptr;
			size_t macSize = 8;
			OSSL_PARAM params[2]
				= { OSSL_PARAM_construct_size_t (OSSL_MAC_PARAM_SIZE, &macSize), OSSL_PARAM_construct_end () };
			const bool ok = md && EVP_DigestSignInit (md, &pkeyCtx, nullptr, nullptr, sipKey) == 1 && pkeyCtx
				&& EVP_PKEY_CTX_set_params (pkeyCtx, params) == 1;
			EVP_PKEY_free (sipKey);
			if (!ok)
			{
				EVP_MD_CTX_free (md);
				return nullptr;
			}
			return md;
		};
		EVP_MD_CTX * send = makeContext (sendSipKey);
		if (!send) return false;
		EVP_MD_CTX * receive = makeContext (receiveSipKey);
		if (!receive)
		{
			EVP_MD_CTX_free (send);
			return false;
		}
		if (m_SendMDCtx) EVP_MD_CTX_free (m_SendMDCtx);
		if (m_ReceiveMDCtx) EVP_MD_CTX_free (m_ReceiveMDCtx);
		m_SendMDCtx = send;
		m_ReceiveMDCtx = receive;
#else
		m_SendSipKey = sendSipKey;
		m_ReceiveSipKey = receiveSipKey;
#endif
		return true;
	}

	void NTCP2Session::ClientLogin ()
	{
		// Start the establishment budget after the TCP connect (or proxy
		// handshake) completes, matching Java NTCPTransport.ESTABLISH_TIMEOUT
		// rather than counting connect time against the handshake.
		SetTerminationTimeout (NTCP2_ESTABLISH_TIMEOUT);
		SetLastActivityTimestamp (i2p::util::GetSecondsSinceEpoch ());
		// Outbound NTCP2 always uses the version negotiated from the peer's
		// RouterInfo. The former one-in-four random downgrade to x25519 let a
		// passive observer weaken the handshake of a hybrid peer and diverged
		// from Java 2.14.0, which never downgrades.
		m_Establisher->SetVersion (NTCP2SelectOutboundVersion (m_Establisher->GetVersion ()));
		m_Establisher->CreateEphemeralKey ();
		boost::asio::post (
			m_Server.GetEstablisherService (), [s = shared_from_this ()] () { s->SendSessionRequest (); });
	}

	void NTCP2Session::ServerLogin (int version)
	{
		if (m_Establisher) m_Establisher->SetVersion (version);
		SetTerminationTimeout (NTCP2_ESTABLISH_TIMEOUT);
		SetLastActivityTimestamp (i2p::util::GetSecondsSinceEpoch ());
		boost::asio::async_read (m_Socket, boost::asio::buffer (m_Establisher->m_Buffer, 64),
			boost::asio::transfer_all (),
			std::bind (&NTCP2Session::HandleSessionRequestReceived, shared_from_this (), std::placeholders::_1,
				std::placeholders::_2));
	}

	bool NTCP2Session::HasPendingIncomingData ()
	{
		boost::system::error_code ec;
		const auto available = m_Socket.available (ec);
		return !ec && available > 0;
	}

	void NTCP2Session::ReceiveLength ()
	{
		if (IsTerminated ()) return;
#ifdef __linux__
		const int one = 1;
		setsockopt (m_Socket.native_handle (), IPPROTO_TCP, TCP_QUICKACK, &one, sizeof (one));
#endif
		boost::asio::async_read (m_Socket, boost::asio::buffer (&m_NextReceivedLen, 2), boost::asio::transfer_all (),
			std::bind (&NTCP2Session::HandleReceivedLength, shared_from_this (), std::placeholders::_1,
				std::placeholders::_2));
	}

	void NTCP2Session::HandleReceivedLength (
		const boost::system::error_code & ecode, std::size_t /* bytes_transferred */)
	{
		if (ecode)
		{
			if (ecode != boost::asio::error::operation_aborted)
				LogPrint (eLogWarning, "NTCP2: Receive length read error: ", ecode.message ());
			Terminate ();
		}
		else
		{
#if OPENSSL_SIPHASH
			EVP_DigestSignInit (m_ReceiveMDCtx, nullptr, nullptr, nullptr, nullptr);
			EVP_DigestSignUpdate (m_ReceiveMDCtx, m_ReceiveIV.buf, 8);
			size_t l = 8;
			EVP_DigestSignFinal (m_ReceiveMDCtx, m_ReceiveIV.buf, &l);
#else
			i2p::crypto::Siphash<8> (m_ReceiveIV.buf, m_ReceiveIV.buf, 8, m_ReceiveSipKey);
#endif
			// m_NextReceivedLen comes from the network in BigEndian
			m_NextReceivedLen = be16toh (m_NextReceivedLen) ^ le16toh (m_ReceiveIV.key);
			LogPrint (eLogDebug, "NTCP2: Received length ", m_NextReceivedLen);
			if (m_NextReceivedLen >= 16)
			{
				CreateNextReceivedBuffer (m_NextReceivedLen);
				boost::system::error_code ec;
				size_t moreBytes = m_Socket.available (ec);
				if (!ec)
				{
					if (moreBytes >= m_NextReceivedLen)
					{
						// read and process message immediately if available
						moreBytes = boost::asio::read (m_Socket,
							boost::asio::buffer (m_NextReceivedBuffer.get (), m_NextReceivedLen),
							boost::asio::transfer_all (), ec);
						HandleReceived (ec, moreBytes);
					}
					else
						Receive ();
				}
				else
					LogPrint (eLogWarning, "NTCP2: Socket error: ", ec.message ());
			}
			else
			{
				// An obfuscated length that decodes below the AEAD tag size means
				// the peer is not using the session keys. Treat it like a tag
				// failure: draining first keeps this path from becoming an
				// immediate-reset oracle (spec/ntcp2 "AEAD Error Handling").
				LogPrint (
					eLogError, "NTCP2: Received length ", m_NextReceivedLen, " is too short from ", m_RemoteEndpoint);
				EnterFailureDrain ();
			}
		}
	}

	void NTCP2Session::Receive ()
	{
		if (IsTerminated ()) return;
#ifdef __linux__
		const int one = 1;
		setsockopt (m_Socket.native_handle (), IPPROTO_TCP, TCP_QUICKACK, &one, sizeof (one));
#endif
		m_IsReceiving = true;
		// The obfuscated length is known, so a stalled body is a peer that stopped
		// in the middle of a frame: bound it with reason 14. The length read
		// itself stays unbounded so an idle session is not closed early; the 120 s
		// idle termination covers a peer that never completes it.
		ScheduleFrameReadDeadline ();
		boost::asio::async_read (m_Socket, boost::asio::buffer (m_NextReceivedBuffer.get (), m_NextReceivedLen),
			boost::asio::transfer_all (),
			std::bind (
				&NTCP2Session::HandleReceived, shared_from_this (), std::placeholders::_1, std::placeholders::_2));
	}

	void NTCP2Session::ScheduleFrameReadDeadline ()
	{
		// One timer serves the intra-frame deadline and the failure drain.
		// expires_after() cancels a pending wait; that handler then observes
		// operation_aborted and does nothing.
		m_ReadTimer.expires_after (std::chrono::seconds (NTCP2_INTRA_FRAME_READ_TIMEOUT));
		m_ReadTimer.async_wait ([s = shared_from_this ()] (const boost::system::error_code & ecode)
			{ s->HandleReadDeadlineTimer (ecode); });
	}

	void NTCP2Session::HandleReadDeadlineTimer (const boost::system::error_code & ecode)
	{
		if (ecode == boost::asio::error::operation_aborted) return; // rescheduled or closed
		if (ecode) LogPrint (eLogWarning, "NTCP2: Read deadline timer error: ", ecode.message ());
		if (IsTerminated ()) return;
		if (m_FailureDrain.IsDraining ())
		{
			// A peer that stops sending during the drain is answered exactly like
			// one that sent the full amount: the reply must not reveal how much
			// ciphertext was drained.
			if (!m_FailureDrain.Expire ()) return;
			LogPrint (eLogWarning, "NTCP2: Data phase AEAD failure drain timed out from ", m_RemoteEndpoint);
			FinishFailureDrain ();
			return;
		}
		if (!IsEstablished ()) return;
		LogPrint (eLogWarning, "NTCP2: Intra-frame read timeout from ", m_RemoteEndpoint);
		SendTerminationAndTerminate (eNTCP2IntraFrameReadTimeout);
	}

	void NTCP2Session::EnterFailureDrain ()
	{
		if (IsTerminated () || m_FailureDrain.IsDraining ()) return;
		// The drain policy only applies to the data phase; before establishment
		// there are no session keys, so an immediate close cannot be an oracle.
		if (!IsEstablished ())
		{
			Terminate ();
			return;
		}
		const auto drainSize = NTCP2GetAEADFailureDrainSize (m_Server.GetRng () ());
		if (!m_FailureDrain.Begin (drainSize))
		{
			// The helper only rejects out-of-range sizes; stay fail-closed.
			SendTerminationAndTerminate (eNTCP2DataPhaseAEADFailure);
			return;
		}
		m_IsReceiving = false;
		LogPrint (eLogWarning, "NTCP2: Data phase AEAD failure from ", m_RemoteEndpoint, "; draining ", drainSize,
			" bytes before terminating");
		// Bounded size and a fixed buffer: no allocation depends on the input.
		// Nothing may be written until this drain finishes; SendQueue(),
		// SendRouterInfo() and EncryptAndSendNextBuffer() check CanWrite().
		boost::asio::async_read (m_Socket, boost::asio::buffer (m_FailureDrainBuffer, drainSize),
			boost::asio::transfer_all (),
			[s = shared_from_this ()] (const boost::system::error_code & ecode, std::size_t bytes_transferred)
			{ s->HandleFailureDrainReceived (ecode, bytes_transferred); });
		m_ReadTimer.expires_after (std::chrono::seconds (NTCP2_AEAD_FAILURE_DRAIN_TIMEOUT));
		m_ReadTimer.async_wait ([s = shared_from_this ()] (const boost::system::error_code & ecode)
			{ s->HandleReadDeadlineTimer (ecode); });
	}

	void NTCP2Session::HandleFailureDrainReceived (
		const boost::system::error_code & ecode, std::size_t bytes_transferred)
	{
		if (!m_FailureDrain.IsDraining ()) return; // timed out, or the session closed first
		m_ReadTimer.cancel ();
		if (ecode)
		{
			// The peer closed or the drain read failed, so no Termination can be
			// delivered; close without one.
			LogPrint (eLogDebug, "NTCP2: Data phase AEAD failure drain ended: ", ecode.message ());
			m_FailureDrain.Reset ();
			Terminate ();
			return;
		}
		m_FailureDrain.Consume (bytes_transferred);
		FinishFailureDrain ();
	}

	void NTCP2Session::FinishFailureDrain ()
	{
		if (IsTerminated () || m_FailureDrain.IsDraining ()) return;
		LogPrint (eLogDebug, "NTCP2: Data phase AEAD failure drain complete from ", m_RemoteEndpoint);
		SendTerminationAndTerminate (eNTCP2DataPhaseAEADFailure);
	}

	void NTCP2Session::HandleReceived (const boost::system::error_code & ecode, std::size_t bytes_transferred)
	{
		// The frame body is complete (or failed); the intra-frame deadline must not
		// run while the next frame is awaited.
		m_ReadTimer.cancel ();
		// A read timeout or a pending termination may have closed the session
		// while this body was still arriving; do not process late frames.
		if (IsTerminated ()) return;
		if (ecode)
		{
			if (ecode != boost::asio::error::operation_aborted)
				LogPrint (eLogWarning, "NTCP2: Receive read error: ", ecode.message ());
			Terminate ();
		}
		else
		{
			UpdateNumReceivedBytes (bytes_transferred + 2);
			i2p::transport::transports.UpdateReceivedBytes (bytes_transferred + 2);
			uint8_t nonce[12];
			CreateNonce (m_ReceiveSequenceNumber, nonce);
			if (m_Server.AEADChaCha20Poly1305Decrypt (m_NextReceivedBuffer.get (), m_NextReceivedLen - 16, nullptr, 0,
					m_ReceiveKey, nonce, m_NextReceivedBuffer.get (), m_NextReceivedLen))
			{
				// Count only authenticated frames. SendTermination() reports
				// this value as "valid data frames received"; incrementing
				// before the AEAD check made an AEAD failure report one frame
				// too many.
				m_ReceiveSequenceNumber++;
				LogPrint (eLogDebug, "NTCP2: Received message decrypted");
				ProcessNextFrame (m_NextReceivedBuffer.get (), m_NextReceivedLen - 16);
				m_IsReceiving = false;
				ReceiveLength ();
			}
			else
			{
				// spec/ntcp2 "AEAD Error Handling": do not answer immediately.
				// m_ReceiveSequenceNumber stays unchanged: the failed frame is not
				// a valid frame.
				LogPrint (eLogWarning, "NTCP2: Received AEAD verification failed from ", m_RemoteEndpoint);
				EnterFailureDrain ();
			}
		}
	}

	void NTCP2Session::ProcessNextFrame (const uint8_t * frame, size_t len)
	{
		size_t offset = 0;
		bool gotPadding = false;
		// A block header is 3 bytes (type + 16-bit size). A truncated header or
		// a block that claims more bytes than the frame contains is malformed;
		// stop processing instead of reading into the MAC or the next block.
		// Padding must be last and termination must be the last non-padding
		// block (spec/ntcp2 "Block Ordering Rules"), matching Java's
		// NTCP2Payload.processPayload().
		while (offset < len)
		{
			if (len - offset < 3)
			{
				LogPrint (eLogError, "NTCP2: Truncated block header at offset ", offset, " of ", len);
				break;
			}
			if (gotPadding)
			{
				LogPrint (eLogError, "NTCP2: Block after padding at offset ", offset);
				break;
			}
			uint8_t blk = frame[offset];
			offset++;
			auto size = bufbe16toh (frame + offset);
			offset += 2;
			LogPrint (eLogDebug, "NTCP2: Block type ", (int)blk, " of size ", size);
			if (size > len - offset)
			{
				LogPrint (eLogError, "NTCP2: Unexpected block length ", size, " of type ", (int)blk);
				break;
			}
			switch (blk)
			{
			case eNTCP2BlkDateTime:
			{
				LogPrint (eLogDebug, "NTCP2: Datetime");
				// Java NTCP2Payload.processPayload() rejects a DateTime block that
				// is not exactly 4 bytes (DataFormatException), which terminates
				// the session. A warning here would leave Alice and Bob with
				// different views of the connection.
				if (size != 4)
				{
					LogPrint (eLogError, "NTCP2: Datetime block has invalid size ", size);
					SendTerminationAndTerminate (eNTCP2PayloadFormatError);
					m_Handler.Flush ();
					return;
				}
				if (m_IsEstablished)
				{
					uint64_t ts = i2p::util::GetSecondsSinceEpoch ();
					uint64_t tsA = bufbe32toh (frame + offset);
					if (tsA < ts - NTCP2_CLOCK_SKEW || tsA > ts + NTCP2_CLOCK_SKEW)
					{
						LogPrint (eLogWarning, "NTCP2: Established session time difference ", (int)ts - (int)tsA,
							" exceeds clock skew");
						SendTerminationAndTerminate (eNTCP2ClockSkew);
						m_Handler.Flush ();
						return;
					}
				}
				break;
			}
			case eNTCP2BlkOptions:
				if (size < 12) LogPrint (eLogWarning, "NTCP2: Options block is too short ", size);
				LogPrint (eLogDebug, "NTCP2: Options");
				break;
			case eNTCP2BlkRouterInfo:
			{
				if (size < 1)
				{
					LogPrint (eLogWarning, "NTCP2: RouterInfo block is too short");
					break;
				}
				const uint8_t flag = frame[offset];
				LogPrint (eLogDebug, "NTCP2: RouterInfo flag=", (int)flag);
				if (size <= i2p::data::MAX_RI_BUFFER_SIZE + 1)
				{
					bool updated = false;
					auto newRi = i2p::data::netdb.AddRouterInfo (frame + offset + 1, size - 1, updated);
					if (newRi)
					{
						// PeerState.gotRI honors the request-flood bit: only a
						// newly accepted or replaced RouterInfo is forwarded, and
						// Java additionally requires the RI block to be smaller
						// than 3 KiB, floodfillEnabled() and a spare per-key
						// FloodThrottler budget. NetDb owns those checks and the
						// actual flood routing.
						if ((flag & NTCP2_ROUTER_INFO_FLAG_REQUEST_FLOOD) && updated
							&& size < NTCP2_ROUTER_INFO_MAX_FLOOD_SIZE)
							i2p::data::netdb.FloodRouterInfoConditional (newRi);
						auto remoteIdentity = GetRemoteIdentity ();
						if (remoteIdentity && remoteIdentity->GetIdentHash () == newRi->GetIdentHash ())
							// peer's RouterInfo update
							SetRemoteIdentity (newRi->GetIdentity ());
						i2p::transport::transports.UpdatePeerParams (newRi);
					}
				}
				else
					LogPrint (eLogInfo, "NTCP2: RouterInfo block is too long ", size);
				break;
			}
			case eNTCP2BlkI2NPMessage:
			{
				LogPrint (eLogDebug, "NTCP2: I2NP");
				if (size > I2NP_MAX_MESSAGE_SIZE)
				{
					LogPrint (eLogError, "NTCP2: I2NP block is too long ", size);
					break;
				}
				if (size < 9)
				{
					LogPrint (eLogError, "NTCP2: I2NP block is too short ", size);
					break;
				}
				auto nextMsg = (frame[offset] == eI2NPTunnelData) ? NewI2NPTunnelMessage (true) : NewI2NPMessage (size);
				nextMsg->len = nextMsg->offset + size + 7; // 7 more bytes for full I2NP header
				if (nextMsg->len <= nextMsg->maxLen)
				{
					memcpy (nextMsg->GetNTCP2Header (), frame + offset, size);
					nextMsg->FromNTCP2 ();
					m_Handler.PutNextMessage (std::move (nextMsg));
				}
				else
					LogPrint (eLogError, "NTCP2: I2NP block is too long for I2NP message");
				break;
			}
			case eNTCP2BlkTermination:
				// Java NTCP2Payload: the Termination block is exactly 9 bytes
				// (8-byte receive count + reason). A malformed termination is
				// fatal, but it is never answered with another Termination: the
				// peer is already closing and a reply could loop.
				if (size != 9)
				{
					LogPrint (eLogError, "NTCP2: Unexpected termination block size ", size, " from ", m_RemoteEndpoint);
					Terminate ();
					m_Handler.Flush ();
					return;
				}
				{
					const auto reason = static_cast<NTCP2TerminationReason> (frame[offset + 8]);
					LogPrint (eLogDebug, "NTCP2: Termination. reason=", (int)reason);
					// Java NTCPConnection.gotTermination applies a per-hash
					// unreachable signal for REASON_BANNED so the next
					// ConnectToPeer does not retry a router that banned us.
					if (reason == eNTCP2Banned && GetRemoteIdentity ())
						i2p::data::netdb.SetUnreachable (GetRemoteIdentity ()->GetIdentHash (), true);
					Terminate ();
				}
				// Termination must be the last non-padding block; the session
				// is closed either way, so do not process anything after it.
				m_Handler.Flush ();
				return;
			case eNTCP2BlkPadding:
				LogPrint (eLogDebug, "NTCP2: Padding");
				gotPadding = true;
				break;
			default: LogPrint (eLogWarning, "NTCP2: Unknown block type ", (int)blk);
			}
			offset += size;
		}
		m_Handler.Flush ();
	}

	void NTCP2Session::SetNextSentFrameLength (size_t frameLen, uint8_t * lengthBuf)
	{
#if OPENSSL_SIPHASH
		EVP_DigestSignInit (m_SendMDCtx, nullptr, nullptr, nullptr, nullptr);
		EVP_DigestSignUpdate (m_SendMDCtx, m_SendIV.buf, 8);
		size_t l = 8;
		EVP_DigestSignFinal (m_SendMDCtx, m_SendIV.buf, &l);
#else
		i2p::crypto::Siphash<8> (m_SendIV.buf, m_SendIV.buf, 8, m_SendSipKey);
#endif
		// length must be in BigEndian
		htobe16buf (lengthBuf, static_cast<uint16_t> (frameLen ^ le16toh (m_SendIV.key)));
		LogPrint (eLogDebug, "NTCP2: Sent length ", frameLen);
	}

	void NTCP2Session::SendI2NPMsgs (std::vector<std::shared_ptr<I2NPMessage>> & msgs)
	{
		if (msgs.empty () || IsTerminated ()) return;

		size_t totalLen = 0;
		std::vector<std::pair<uint8_t *, size_t>> encryptBufs;
		std::vector<boost::asio::const_buffer> bufs;
		std::shared_ptr<I2NPMessage> first;
		uint8_t * macBuf = nullptr;
		for (auto & it : msgs)
		{
			it->ToNTCP2 ();
			auto buf = it->GetNTCP2Header ();
			auto len = it->GetNTCP2Length ();
			// block header
			buf -= 3;
			buf[0] = eNTCP2BlkI2NPMessage; // blk
			htobe16buf (buf + 1, static_cast<uint16_t> (len)); // size
			len += 3;
			totalLen += len;
			encryptBufs.push_back ({ buf, len });
			if (&it == &msgs.front ()) // first message
			{
				// allocate two bytes for length
				buf -= 2;
				len += 2;
				first = it;
			}
			if (&it == &msgs.back () && it->len + 16 < it->maxLen) // last message
			{
				// if it's long enough we add padding and MAC to it
				// create padding block
				auto paddingLen = CreatePaddingBlock (totalLen, buf + len, it->maxLen - it->len - 16);
				if (paddingLen)
				{
					encryptBufs.push_back ({ buf + len, paddingLen });
					len += paddingLen;
					totalLen += paddingLen;
				}
				macBuf = buf + len;
				// allocate 16 bytes for MAC
				len += 16;
			}

			bufs.push_back (boost::asio::buffer (buf, len));
		}

		if (!macBuf) // last block was not enough for MAC
		{
			// allocate send buffer
			// can be any size > 16, we just allocate 287 frequently
			m_NextSendBuffer = std::make_unique_for_overwrite<uint8_t[]> (287);
			// create padding block
			auto paddingLen = CreatePaddingBlock (totalLen, m_NextSendBuffer.get (), 287 - 16);
			// and padding block to encrypt and send
			if (paddingLen) encryptBufs.push_back ({ m_NextSendBuffer.get (), paddingLen });
			bufs.push_back (boost::asio::buffer (m_NextSendBuffer.get (), paddingLen + 16));
			macBuf = m_NextSendBuffer.get () + paddingLen;
			totalLen += paddingLen;
		}
		if (totalLen > NTCP2_UNENCRYPTED_FRAME_MAX_SIZE)
		{
			LogPrint (eLogError, "NTCP2: Frame to send is too long ", totalLen);
			return;
		}
		uint8_t nonce[12];
		CreateNonce (m_SendSequenceNumber, nonce);
		if (!m_Server.AEADChaCha20Poly1305Encrypt (encryptBufs, m_SendKey, nonce, macBuf))
		{
			LogPrint (eLogError, "NTCP2: AEAD encryption failed; dropping frame");
			m_NextSendBuffer.reset ();
			Terminate ();
			return;
		}
		m_SendSequenceNumber++;
		SetNextSentFrameLength (totalLen + 16, first->GetNTCP2Header () - 5); // frame length right before first block

		// send buffers, the staged buffer is moved into the handler so that it outlives the write
		m_IsSending = true;
		std::unique_ptr<uint8_t[]> sendBuffer = std::move (m_NextSendBuffer);
		boost::asio::async_write (m_Socket, bufs, boost::asio::transfer_all (),
			[s = shared_from_this (), msgs = std::move (msgs), sendBuffer = std::move (sendBuffer)] (
				const boost::system::error_code & ecode, std::size_t bytes_transferred) mutable
			{ s->HandleI2NPMsgsSent (ecode, bytes_transferred, std::move (msgs), std::move (sendBuffer)); });
	}

	void NTCP2Session::HandleI2NPMsgsSent (const boost::system::error_code & ecode, std::size_t bytes_transferred,
		std::vector<std::shared_ptr<I2NPMessage>> /* msgs */, std::unique_ptr<uint8_t[]> sendBuffer)
	{
		HandleNextFrameSent (ecode, bytes_transferred, std::move (sendBuffer));
		// msgs get destroyed here
	}

	bool NTCP2Session::EncryptAndSendNextBuffer (size_t payloadLen)
	{
		if (IsTerminated ())
		{
			m_NextSendBuffer.reset ();
			return false;
		}
		// A data-phase failure drain must complete first: answering earlier would
		// turn the AEAD failure into a decryption oracle (spec/ntcp2 "AEAD Error
		// Handling"). The drain itself calls this function only after it is over.
		if (!m_FailureDrain.CanWrite ())
		{
			m_NextSendBuffer.reset ();
			return false;
		}
		// Never start a second async_write on the same TCP stream. Callers that
		// own a control frame remember the request and retry from the write
		// completion handler.
		if (m_IsSending)
		{
			m_NextSendBuffer.reset ();
			return false;
		}
		if (payloadLen > NTCP2_UNENCRYPTED_FRAME_MAX_SIZE)
		{
			LogPrint (eLogError, "NTCP2: Buffer to send is too long ", payloadLen);
			m_NextSendBuffer.reset ();
			return false;
		}
		// encrypt
		uint8_t nonce[12];
		CreateNonce (m_SendSequenceNumber, nonce);
		if (!m_Server.AEADChaCha20Poly1305Encrypt ({ { m_NextSendBuffer.get () + 2, payloadLen } }, m_SendKey, nonce,
				m_NextSendBuffer.get () + payloadLen + 2))
		{
			LogPrint (eLogError, "NTCP2: AEAD encryption failed; dropping frame");
			m_NextSendBuffer.reset ();
			Terminate ();
			return false;
		}
		m_SendSequenceNumber++;
		SetNextSentFrameLength (payloadLen + 16, m_NextSendBuffer.get ());
		// send, the buffer is moved into the handler so that it outlives the write
		m_IsSending = true;
		std::unique_ptr<uint8_t[]> sendBuffer = std::move (m_NextSendBuffer);
		// Build the buffer view before moving its owner into the completion handler;
		// argument evaluation order is otherwise unspecified.
		auto writeBuffer = boost::asio::buffer (sendBuffer.get (), payloadLen + 16 + 2);
		boost::asio::async_write (m_Socket, writeBuffer, boost::asio::transfer_all (),
			[s = shared_from_this (), sendBuffer = std::move (sendBuffer)] (
				const boost::system::error_code & ecode, std::size_t bytes_transferred) mutable
			{ s->HandleNextFrameSent (ecode, bytes_transferred, std::move (sendBuffer)); });
		return true;
	}

	void NTCP2Session::HandleNextFrameSent (const boost::system::error_code & ecode, std::size_t bytes_transferred,
		std::unique_ptr<uint8_t[]> /* sendBuffer */)
	{
		// sendBuffer is released when this handler returns
		m_IsSending = false;

		if (ecode)
		{
			if (ecode != boost::asio::error::operation_aborted)
				LogPrint (eLogWarning, "NTCP2: Couldn't send frame ", ecode.message ());
			Terminate ();
			return;
		}

		UpdateNumSentBytes (bytes_transferred);
		i2p::transport::transports.UpdateSentBytes (bytes_transferred);
		LogPrint (eLogDebug, "NTCP2: Next frame sent ", bytes_transferred);
		if (m_IsTerminating)
		{
			// The termination frame has been flushed; now close.
			Terminate ();
			return;
		}
		if (m_PendingTerminationReason >= 0)
		{
			// A data or RouterInfo write was in flight when termination was
			// requested. Send it now and close after its completion.
			auto reason = (NTCP2TerminationReason)m_PendingTerminationReason;
			m_PendingTerminationReason = -1;
			if (SendTermination (reason))
			{
				m_IsTerminating = true;
				return;
			}
			Terminate ();
			return;
		}
		if (m_PendingRouterInfo)
		{
			m_PendingRouterInfo = false;
			SendRouterInfo ();
			if (m_IsSending) return;
		}
		if (GetLastActivityTimestamp () > m_NextRouterInfoResendTime && m_NextRouterInfoResendTime)
		{
			m_NextRouterInfoResendTime
				+= NTCP2_ROUTERINFO_RESEND_INTERVAL + m_Server.GetRng () () % NTCP2_ROUTERINFO_RESEND_INTERVAL_VARIANCE;
			SendRouterInfo ();
		}
		else
		{
			SendQueue ();
			SetSendQueueSize (m_SendQueue.size ());
		}
	}

	void NTCP2Session::SendQueue ()
	{
		if (m_IsSending || m_IsTerminating || m_PendingTerminationReason >= 0) return;
		// No data may be written while the data-phase failure drain is pending.
		if (!m_FailureDrain.CanWrite ()) return;
		if (!m_SendQueue.empty () && m_IsEstablished)
		{
			std::vector<std::shared_ptr<I2NPMessage>> msgs;
			auto ts = i2p::util::GetMillisecondsSinceEpoch ();
			size_t s = 0;
			while (!m_SendQueue.empty ())
			{
				auto msg = m_SendQueue.front ();
				if (!msg || msg->IsExpired (ts))
				{
					// drop null or expired message
					if (msg) msg->Drop ();
					m_SendQueue.pop_front ();
					continue;
				}
				size_t len = msg->GetNTCP2Length ();
				if (s + len + 3 <= NTCP2_UNENCRYPTED_FRAME_MAX_SIZE) // 3 bytes block header
				{
					msgs.push_back (msg);
					s += (len + 3);
					m_SendQueue.pop_front ();
					if (s >= NTCP2_SEND_AFTER_FRAME_SIZE) break; // send frame right a way
				}
				else if (len + 3 > NTCP2_UNENCRYPTED_FRAME_MAX_SIZE)
				{
					LogPrint (eLogError, "NTCP2: I2NP message of size ", len, " can't be sent. Dropped");
					msg->Drop ();
					m_SendQueue.pop_front ();
				}
				else
					break;
			}
			SendI2NPMsgs (msgs);
		}
	}

	void NTCP2Session::MoveSendQueue (std::shared_ptr<NTCP2Session> other)
	{
		if (!other || m_SendQueue.empty ()) return;
		std::list<std::shared_ptr<I2NPMessage>> msgs;
		auto ts = i2p::util::GetMillisecondsSinceEpoch ();
		for (auto it : m_SendQueue)
			if (!it->IsExpired (ts))
				msgs.push_back (it);
			else
				it->Drop ();
		m_SendQueue.clear ();
		if (!msgs.empty ()) other->SendI2NPMessages (msgs);
	}

	// Data-phase padding policy. The block grows with the payload up to
	// NTCP2_MAX_PADDING_RATIO percent and the emitted length is drawn from a
	// batch of random bytes instead of being recomputed per frame, so two frames
	// of equal size do not share a padding length. A short frame is measured as
	// 256 bytes so the block is not always zero. The result is clamped to the
	// caller-supplied capacity and to NTCP2_UNENCRYPTED_FRAME_MAX_SIZE, so no
	// input can produce a frame the encryptor must reject.
	size_t NTCP2Session::CreatePaddingBlock (size_t msgLen, uint8_t * buf, size_t len)
	{
		if (len < 3) return 0;
		len -= 3;
		if (msgLen < 256) msgLen = 256; // for short message padding should not be always zero
		size_t paddingSize = (msgLen * NTCP2_MAX_PADDING_RATIO) / 100;
		if (msgLen + paddingSize + 3 > NTCP2_UNENCRYPTED_FRAME_MAX_SIZE)
		{
			int l = static_cast<int> (NTCP2_UNENCRYPTED_FRAME_MAX_SIZE - msgLen - 3);
			if (l <= 0) return 0;
			paddingSize = l;
		}
		if (paddingSize > len) paddingSize = len;
		if (paddingSize)
		{
			if (m_NextPaddingSize >= 16)
			{
				RAND_bytes ((uint8_t *)m_PaddingSizes, sizeof (m_PaddingSizes));
				m_NextPaddingSize = 0;
			}
			paddingSize = m_PaddingSizes[m_NextPaddingSize++] % (paddingSize + 1);
		}
		buf[0] = eNTCP2BlkPadding; // blk
		htobe16buf (buf + 1, static_cast<uint16_t> (paddingSize)); // size
		memset (buf + 3, 0, paddingSize);
		return paddingSize + 3;
	}

	void NTCP2Session::SendRouterInfo ()
	{
		if (!IsEstablished () || m_IsTerminating || m_PendingTerminationReason >= 0) return;
		// Do not queue a control frame behind a data-phase failure drain.
		if (!m_FailureDrain.CanWrite ()) return;
		if (m_IsSending)
		{
			m_PendingRouterInfo = true;
			return;
		}
		auto riBuffer = i2p::context.CopyRouterInfoBuffer ();
		auto riLen = riBuffer->GetBufferLen ();
		size_t payloadLen = riLen + 3 + 1 + 7; // 3 bytes block header + 1 byte RI flag + 7 bytes DateTime
		// up to 64 bytes padding
		m_NextSendBuffer = std::make_unique_for_overwrite<uint8_t[]> (payloadLen + 16 + 2 + 64);
		// DateTime	block
		m_NextSendBuffer[2] = eNTCP2BlkDateTime;
		htobe16buf (m_NextSendBuffer.get () + 3, 4);
		htobe32buf (m_NextSendBuffer.get () + 5,
			static_cast<uint32_t> ((i2p::util::GetMillisecondsSinceEpoch () + 500) / 1000));
		// RouterInfo block
		m_NextSendBuffer[9] = eNTCP2BlkRouterInfo;
		htobe16buf (m_NextSendBuffer.get () + 10, static_cast<uint16_t> (riLen + 1)); // size
		m_NextSendBuffer[12] = 0; // flag
		memcpy (m_NextSendBuffer.get () + 13, riBuffer->data (), riLen); // TODO: eliminate extra copy
		// padding block
		auto paddingSize = CreatePaddingBlock (payloadLen, m_NextSendBuffer.get () + 2 + payloadLen, 64);
		payloadLen += paddingSize;
		// encrypt and send
		if (!EncryptAndSendNextBuffer (payloadLen) && IsEstablished ()) m_PendingRouterInfo = true;
	}

	bool NTCP2Session::SendTermination (NTCP2TerminationReason reason)
	{
		if (!m_SendKey ||
#if OPENSSL_SIPHASH
			!m_SendMDCtx
#else
			!m_SendSipKey
#endif
		)
			return false;
		if (m_IsSending)
		{
			// Complete the in-flight write first; HandleNextFrameSent() sends
			// this termination frame and then closes the session.
			m_PendingTerminationReason = (int)reason;
			return false;
		}
		// 49 = 12 bytes message + 16 bytes MAC + 2 bytes size + up to 19 padding block
		m_NextSendBuffer = std::make_unique_for_overwrite<uint8_t[]> (49);
		// termination block
		m_NextSendBuffer[2] = eNTCP2BlkTermination;
		m_NextSendBuffer[3] = 0;
		m_NextSendBuffer[4] = 9; // 9 bytes block size
		htobe64buf (m_NextSendBuffer.get () + 5, m_ReceiveSequenceNumber);
		m_NextSendBuffer[13] = (uint8_t)reason;
		// padding block
		auto paddingSize = CreatePaddingBlock (12, m_NextSendBuffer.get () + 14, 19);
		// encrypt and send
		return EncryptAndSendNextBuffer (paddingSize + 12);
	}

	void NTCP2Session::SendTerminationAndTerminate (NTCP2TerminationReason reason)
	{
		if (IsTerminated () || m_IsTerminating) return;
		if (m_FailureDrain.IsDraining ())
		{
			// The pending AEAD-failure drain owns the close: writing any frame
			// before it finishes would recreate the decryption oracle the drain
			// exists to prevent. The drain's own 10 s completion/timeout sends
			// reason 4 and closes.
			return;
		}
		if (m_PendingTerminationReason < 0) m_PendingTerminationReason = (int)reason;
		if (m_IsSending) return; // HandleNextFrameSent() will send and close
		if (SendTermination (reason))
		{
			m_IsTerminating = true; // close from the write completion handler
			return;
		}
		m_PendingTerminationReason = -1;
		boost::asio::post (m_Server.GetService (), std::bind (&NTCP2Session::Terminate, shared_from_this ()));
	}

	// Probing resistance for a rejected handshake (spec/ntcp2 "AEAD Error
	// Handling" for the handshake path): consume a random amount of pending
	// input before closing, so neither the close timing nor the reset describes
	// how much of the rejected message was parsed. The length comes from the
	// server RNG and is bounded by the handshake maximum; nothing is written and
	// the completion handler ignores both the result and the error.
	void NTCP2Session::ReadSomethingAndTerminate ()
	{
		size_t len = m_Server.GetRng () () % NTCP2_SESSION_HANDSHAKE_MAX_SIZE;
		if (len > 0 && m_Establisher)
			boost::asio::async_read (m_Socket, boost::asio::buffer (m_Establisher->m_Buffer, len),
				boost::asio::transfer_all (),
				[s = shared_from_this ()] (const boost::system::error_code &, size_t) { s->Terminate (); });
		else
			boost::asio::post (m_Server.GetService (), std::bind (&NTCP2Session::Terminate, shared_from_this ()));
	}

	void NTCP2Session::SendI2NPMessages (std::list<std::shared_ptr<I2NPMessage>> & msgs)
	{
		if (m_IsTerminated || msgs.empty ())
		{
			msgs.clear ();
			return;
		}
		bool empty = false;
		{
			std::lock_guard<std::mutex> l (m_IntermediateQueueMutex);
			empty = m_IntermediateQueue.empty ();
			m_IntermediateQueue.splice (m_IntermediateQueue.end (), msgs);
		}
		if (empty)
			boost::asio::post (
				m_Server.GetService (), std::bind (&NTCP2Session::PostI2NPMessages, shared_from_this ()));
	}

	// Queue admission runs on the session thread; m_IntermediateQueue is filled
	// from any thread and swapped here under its mutex. Above half of
	// NTCP2_MAX_OUTGOING_QUEUE_SIZE, messages that carry an onDrop callback are
	// dropped instead of queued because their producer can retransmit them, and
	// a queue above the hard cap terminates the session instead of growing
	// without bound.
	void NTCP2Session::PostI2NPMessages ()
	{
		if (m_IsTerminated) return;
		std::list<std::shared_ptr<I2NPMessage>> msgs;
		{
			std::lock_guard<std::mutex> l (m_IntermediateQueueMutex);
			m_IntermediateQueue.swap (msgs);
		}
		bool isSemiFull = m_SendQueue.size () > NTCP2_MAX_OUTGOING_QUEUE_SIZE / 2;
		if (isSemiFull)
		{
			for (auto it : msgs)
				if (it->onDrop)
					it->Drop (); // drop earlier because we can handle it
				else
					m_SendQueue.push_back (std::move (it));
		}
		else
			m_SendQueue.splice (m_SendQueue.end (), msgs);

		if (!m_IsSending && m_IsEstablished)
			SendQueue ();
		else if (m_SendQueue.size () > NTCP2_MAX_OUTGOING_QUEUE_SIZE)
		{
			LogPrint (eLogWarning, "NTCP2: Outgoing messages queue size to ", GetIdentHashBase64 (), " exceeds ",
				NTCP2_MAX_OUTGOING_QUEUE_SIZE);
			Terminate ();
		}
		SetSendQueueSize (m_SendQueue.size ());
	}

	void NTCP2Session::SendLocalRouterInfo (bool update)
	{
		if (update || !IsOutgoing ()) // we send it in SessionConfirmed for outgoing session
			boost::asio::post (m_Server.GetService (), std::bind (&NTCP2Session::SendRouterInfo, shared_from_this ()));
	}

	i2p::data::RouterInfo::SupportedTransports NTCP2Session::GetTransportType () const
	{
		if (m_RemoteEndpoint.address ().is_v4 ()) return i2p::data::RouterInfo::eNTCP2V4;
		return i2p::util::net::IsYggdrasilAddress (m_RemoteEndpoint.address ()) ? i2p::data::RouterInfo::eNTCP2V6Mesh
																				: i2p::data::RouterInfo::eNTCP2V6;
	}

	NTCP2Server::NTCP2Server ()
		: RunnableServiceWithWork ("NTCP2")
		, m_TerminationTimer (GetService ())
		, m_ProxyType (eNoProxy)
		, m_Resolver (GetService ())
		, m_Rng (i2p::util::GetRngSeed ())
		, m_EstablisherService (m_Rng ())
		, m_Version (2)
		, m_IsReady (false)
	{
	}

	NTCP2Server::~NTCP2Server ()
	{
		Stop ();
	}

	void NTCP2Server::Start ()
	{
		m_EstablisherService.Start ();
		if (!IsRunning ())
		{
			StartIOService ();
			if (UsingProxy ())
			{
				LogPrint (eLogInfo, "NTCP2: Using proxy to connect to peers");
				// TODO: resolve proxy until it is resolved
				boost::system::error_code e;
				auto itr = m_Resolver.resolve (m_ProxyAddress, std::to_string (m_ProxyPort), e);
				if (e)
					LogPrint (eLogCritical, "NTCP2: Failed to resolve proxy ", e.message ());
				else
				{
					m_ProxyEndpoint.reset (new boost::asio::ip::tcp::endpoint (*itr.begin ()));
					if (m_ProxyEndpoint) LogPrint (eLogDebug, "NTCP2: m_ProxyEndpoint ", *m_ProxyEndpoint);
				}
			}
			else
				LogPrint (eLogInfo, "NTCP2: Proxy is not used");
			// start acceptors
			auto addresses = context.GetRouterInfo ().GetAddresses ();
			if (!addresses) return;
			for (const auto & address : *addresses)
			{
				if (!address) continue;
				if (address->IsPublishedNTCP2 () && address->port)
				{
					if (address->IsV4 ())
					{
						try
						{
							auto ep = m_Address4 ? boost::asio::ip::tcp::endpoint (
													   m_Address4->address (), static_cast<uint16_t> (address->port))
												 : boost::asio::ip::tcp::endpoint (boost::asio::ip::tcp::v4 (),
													   static_cast<uint16_t> (address->port));
							m_NTCP2Acceptor.reset (new boost::asio::ip::tcp::acceptor (GetService (), ep));
						}
						catch (std::exception & ex)
						{
							LogPrint (eLogCritical, "NTCP2: Failed to bind to v4 port ", address->port, ex.what ());
							ThrowFatal (
								"Unable to start IPv4 NTCP2 transport at port ", address->port, ": ", ex.what ());
							continue;
						}

						LogPrint (eLogInfo, "NTCP2: Start listening v4 TCP port ", address->port);
						auto conn = std::make_shared<NTCP2Session> (*this);
						m_NTCP2Acceptor->async_accept (conn->GetSocket (),
							std::bind (&NTCP2Server::HandleAccept, this, conn, std::placeholders::_1));
					}
					else if (address->IsV6 () && (context.SupportsV6 () || context.SupportsMesh ()))
					{
						m_NTCP2V6Acceptor.reset (new boost::asio::ip::tcp::acceptor (GetService ()));
						try
						{
							m_NTCP2V6Acceptor->open (boost::asio::ip::tcp::v6 ());
							m_NTCP2V6Acceptor->set_option (boost::asio::ip::v6_only (true));
							m_NTCP2V6Acceptor->set_option (boost::asio::socket_base::reuse_address (true));
#if defined(__linux__) && !defined(_NETINET_IN_H)
							if (!m_Address6 && !m_YggdrasilAddress) // only if not binded to address
							{
								// Set preference to use public IPv6 address -- tested on linux, not works on windows, and not tested on others
								typedef boost::asio::detail::socket_option::integer<BOOST_ASIO_OS_DEF (IPPROTO_IPV6),
									IPV6_ADDR_PREFERENCES>
									ipv6PreferAddr;
								m_NTCP2V6Acceptor->set_option (ipv6PreferAddr (
									IPV6_PREFER_SRC_PUBLIC | IPV6_PREFER_SRC_HOME | IPV6_PREFER_SRC_NONCGA));
							}
#endif
							auto ep = boost::asio::ip::tcp::endpoint (
								boost::asio::ip::tcp::v6 (), static_cast<uint16_t> (address->port));
							if (m_Address6 && !context.SupportsMesh ())
								ep = boost::asio::ip::tcp::endpoint (
									m_Address6->address (), static_cast<uint16_t> (address->port));
							else if (m_YggdrasilAddress && !context.SupportsV6 ())
								ep = boost::asio::ip::tcp::endpoint (
									m_YggdrasilAddress->address (), static_cast<uint16_t> (address->port));
							m_NTCP2V6Acceptor->bind (ep);
							m_NTCP2V6Acceptor->listen ();

							LogPrint (eLogInfo, "NTCP2: Start listening v6 TCP port ", address->port);
							auto conn = std::make_shared<NTCP2Session> (*this);
							m_NTCP2V6Acceptor->async_accept (conn->GetSocket (),
								std::bind (&NTCP2Server::HandleAcceptV6, this, conn, std::placeholders::_1));
						}
						catch (std::exception & ex)
						{
							LogPrint (
								eLogCritical, "NTCP2: Failed to bind to v6 port ", address->port, ": ", ex.what ());
							ThrowFatal (
								"Unable to start IPv6 NTCP2 transport at port ", address->port, ": ", ex.what ());
							continue;
						}
					}
				}
			}
			ScheduleTermination ();
		}
		m_IsReady = true;
	}

	void NTCP2Server::Stop ()
	{
		m_IsReady = false;
		m_EstablisherService.Stop ();
		if (IsRunning ())
		{
			// Stop() is called from the transports thread. Terminate() mutates
			// m_NTCP2Sessions/m_PendingIncomingSessions, while the NTCP2 service
			// may be iterating them in HandleTerminationTimer. Serialize the
			// teardown on the service thread and wait before stopping it.
			try
			{
				boost::asio::post (GetService (),
					boost::asio::use_future (
						[this] ()
						{
							auto ntcpSessions = m_NTCP2Sessions;
							auto pendingSessions = m_PendingIncomingSessions;
							for (auto & it : ntcpSessions) it.second->Terminate ();
							for (auto & it : pendingSessions) it.second->Terminate ();
							m_NTCP2Sessions.clear ();
							m_PendingIncomingSessions.clear ();
						}))
					.get ();
			}
			catch (const std::exception & ex)
			{
				LogPrint (eLogError, "NTCP2: shutdown task failed: ", ex.what ());
			}
			m_TerminationTimer.cancel ();
			m_ProxyEndpoint = nullptr;
		}
		else
		{
			m_NTCP2Sessions.clear ();
			m_PendingIncomingSessions.clear ();
		}
		{
			std::lock_guard<std::mutex> l (m_HandshakeReplayMutex);
			m_HandshakeReplayCache.clear ();
			m_HandshakeReplayCacheOutgoing.clear ();
		}
		StopIOService ();
	}

	bool NTCP2Server::IsHandshakeReplay (const uint8_t * encrypted, std::unordered_map<uint64_t, uint64_t> & cache)
	{
		if (!encrypted) return false;
		uint64_t key = 0;
		memcpy (&key, encrypted, sizeof (key)); // opaque first 8 bytes of the encrypted value
		auto now = i2p::util::GetSecondsSinceEpoch ();
		std::lock_guard<std::mutex> l (m_HandshakeReplayMutex);
		// Drop expired entries first so the cache reflects the current window.
		for (auto it = cache.begin (); it != cache.end ();)
			if (it->second <= now)
				it = cache.erase (it);
			else
				++it;
		auto it = cache.find (key);
		if (it != cache.end ()) return true;
		if (cache.size () >= NTCP2_MAX_HANDSHAKE_REPLAY_ENTRIES)
		{
			// Bounded cache: evict an arbitrary entry rather than grow without
			// limit. This can only weaken replay detection under a flood, never
			// accept a value that is still represented in the cache.
			cache.erase (cache.begin ());
		}
		cache.emplace (key, now + NTCP2_HANDSHAKE_REPLAY_TIMEOUT);
		return false;
	}

	bool NTCP2Server::IsHandshakeReplay (const uint8_t * encryptedX)
	{
		return IsHandshakeReplay (encryptedX, m_HandshakeReplayCache);
	}

	bool NTCP2Server::IsHandshakeReplayOutgoing (const uint8_t * encryptedY)
	{
		return IsHandshakeReplay (encryptedY, m_HandshakeReplayCacheOutgoing);
	}

	bool NTCP2Server::AddNTCP2Session (std::shared_ptr<NTCP2Session> session, bool incoming)
	{
		if (!session || !m_IsReady || session->IsTerminated ()) return false;
		if (incoming) m_PendingIncomingSessions.erase (session->GetRemoteEndpoint ().address ());
		if (!session->GetRemoteIdentity ())
		{
			LogPrint (eLogWarning, "NTCP2: Unknown identity for ", session->GetRemoteEndpoint ());
			session->Terminate ();
			return false;
		}
		auto & ident = session->GetRemoteIdentity ()->GetIdentHash ();
		auto it = m_NTCP2Sessions.find (ident);
		if (it != m_NTCP2Sessions.end ())
		{
			LogPrint (eLogWarning, "NTCP2: Session with ", ident.ToBase64 (), " already exists. ",
				incoming ? "Replaced" : "Dropped");
			if (incoming)
			{
				// replace by new session
				auto s = it->second;
				s->MoveSendQueue (session);
				m_NTCP2Sessions.erase (it);
				s->Terminate ();
			}
			else
			{
				session->Terminate ();
				return false;
			}
		}
		m_NTCP2Sessions.emplace (ident, session);
		return true;
	}

	void NTCP2Server::RemoveNTCP2Session (std::shared_ptr<NTCP2Session> session)
	{
		if (!session) return;
		// Release a per-address pending handshake slot immediately, so a peer
		// that failed or closed a handshake can reconnect without waiting for
		// the 28-33 second termination sweep.
		const auto & address = session->GetRemoteEndpoint ().address ();
		if (!address.is_unspecified ())
		{
			auto pending = m_PendingIncomingSessions.find (address);
			if (pending != m_PendingIncomingSessions.end () && pending->second == session)
				m_PendingIncomingSessions.erase (pending);
		}
		if (session->GetRemoteIdentity ())
		{
			auto it = m_NTCP2Sessions.find (session->GetRemoteIdentity ()->GetIdentHash ());
			if (it != m_NTCP2Sessions.end () && it->second == session) m_NTCP2Sessions.erase (it);
		}
	}

	std::shared_ptr<NTCP2Session> NTCP2Server::FindNTCP2Session (const i2p::data::IdentHash & ident)
	{
		auto it = m_NTCP2Sessions.find (ident);
		if (it != m_NTCP2Sessions.end ()) return it->second;
		return nullptr;
	}

	void NTCP2Server::Connect (std::shared_ptr<NTCP2Session> conn)
	{
		if (!conn || conn->GetRemoteEndpoint ().address ().is_unspecified ())
		{
			LogPrint (eLogError, "NTCP2: Can't connect to unspecified address");
			return;
		}
		LogPrint (eLogDebug, "NTCP2: Connecting to ", conn->GetRemoteEndpoint (), " (",
			i2p::data::GetIdentHashAbbreviation (conn->GetRemoteIdentity ()->GetIdentHash ()), ")");
		boost::asio::post (GetService (),
			[this, conn] ()
			{
				if (!m_IsReady) return;
				if (AddNTCP2Session (conn))
				{
					auto timer = std::make_shared<boost::asio::steady_timer> (GetService ());
					auto timeout = NTCP2_CONNECT_TIMEOUT; // Java NTCPTransport.ESTABLISH_TIMEOUT
					conn->SetTerminationTimeout (timeout);
					timer->expires_after (std::chrono::seconds (timeout));
					timer->async_wait (
						[conn, timeout] (const boost::system::error_code & ecode)
						{
							if (ecode != boost::asio::error::operation_aborted)
							{
								LogPrint (eLogInfo, "NTCP2: Not connected in ", timeout, " seconds");
								conn->Terminate ();
							}
						});
					// bind to local address
					std::shared_ptr<boost::asio::ip::tcp::endpoint> localAddress;
					if (conn->GetRemoteEndpoint ().address ().is_v6 ())
					{
						if (i2p::util::net::IsYggdrasilAddress (conn->GetRemoteEndpoint ().address ()))
							localAddress = m_YggdrasilAddress;
						else
							localAddress = m_Address6;
						conn->GetSocket ().open (boost::asio::ip::tcp::v6 ());
					}
					else
					{
						localAddress = m_Address4;
						conn->GetSocket ().open (boost::asio::ip::tcp::v4 ());
					}
					if (localAddress)
					{
						boost::system::error_code ec;
						conn->GetSocket ().bind (*localAddress, ec);
						if (ec)
							LogPrint (eLogError, "NTCP2: Can't bind to ",
								i2p::util::net::FormatAddress (localAddress->address ()), ": ", ec.message ());
					}
					conn->GetSocket ().async_connect (conn->GetRemoteEndpoint (),
						std::bind (&NTCP2Server::HandleConnect, this, std::placeholders::_1, conn, timer));
				}
				else
					conn->Terminate ();
			});
	}

	void NTCP2Server::HandleConnect (const boost::system::error_code & ecode, std::shared_ptr<NTCP2Session> conn,
		std::shared_ptr<boost::asio::steady_timer> timer)
	{
		timer->cancel ();
		if (ecode)
		{
			LogPrint (eLogInfo, "NTCP2: Connect error ", ecode.message ());
			conn->Terminate ();
		}
		else
		{
			LogPrint (eLogDebug, "NTCP2: Connected to ", conn->GetRemoteEndpoint (), " (",
				i2p::data::GetIdentHashAbbreviation (conn->GetRemoteIdentity ()->GetIdentHash ()), ")");
			conn->ClientLogin ();
		}
	}

	void NTCP2Server::HandleAccept (std::shared_ptr<NTCP2Session> conn, const boost::system::error_code & error)
	{
		if (!error && conn && m_IsReady)
		{
			boost::system::error_code ec;
			auto ep = conn->GetSocket ().remote_endpoint (ec);
			if (!ec)
			{
				LogPrint (eLogDebug, "NTCP2: Connected from ", ep);
				if (!i2p::transport::transports.IsInReservedRange (ep.address ())
					&& !i2p::data::IsOperatorBlockedAddress (ep.address ())
					&& !i2p::transport::transports.IsBanned (ep.address ()))
				{
					if (m_PendingIncomingSessions.size () >= NTCP2_MAX_PENDING_INCOMING_SESSIONS)
						LogPrint (eLogWarning, "NTCP2: Too many pending incoming sessions");
					else if (m_PendingIncomingSessions.emplace (ep.address (), conn).second)
					{
						conn->SetRemoteEndpoint (ep);
						conn->ServerLogin (m_Version);
						conn = nullptr;
					}
					else
						LogPrint (eLogInfo, "NTCP2: Incoming session from ", ep.address (), " is already pending");
				}
				else
					LogPrint (eLogError, "NTCP2: Incoming connection from invalid or banned IP ", ep.address ());
			}
			else
				LogPrint (eLogError, "NTCP2: Connected from error ", ec.message ());
		}
		else
		{
			LogPrint (eLogError, "NTCP2: Accept error ", error.message ());
			if (error == boost::asio::error::no_descriptors)
			{
				i2p::context.SetError (eRouterErrorNoDescriptors);
				return;
			}
		}

		if (error != boost::asio::error::operation_aborted)
		{
			if (!conn) // connection is used, create new one
				conn = std::make_shared<NTCP2Session> (*this);
			else // reuse failed
				conn->Close ();
			m_NTCP2Acceptor->async_accept (
				conn->GetSocket (), std::bind (&NTCP2Server::HandleAccept, this, conn, std::placeholders::_1));
		}
	}

	void NTCP2Server::HandleAcceptV6 (std::shared_ptr<NTCP2Session> conn, const boost::system::error_code & error)
	{
		if (!error && conn)
		{
			boost::system::error_code ec;
			auto ep = conn->GetSocket ().remote_endpoint (ec);
			if (!ec)
			{
				LogPrint (eLogDebug, "NTCP2: Connected from ", ep);
				if ((!i2p::transport::transports.IsInReservedRange (ep.address ())
						|| i2p::util::net::IsYggdrasilAddress (ep.address ()))
					&& !i2p::data::IsOperatorBlockedAddress (ep.address ())
					&& !i2p::transport::transports.IsBanned (ep.address ()))
				{
					if (m_PendingIncomingSessions.size () >= NTCP2_MAX_PENDING_INCOMING_SESSIONS)
						LogPrint (eLogWarning, "NTCP2: Too many pending incoming sessions");
					else if (m_PendingIncomingSessions.emplace (ep.address (), conn).second)
					{
						conn->SetRemoteEndpoint (ep);
						conn->ServerLogin (i2p::util::net::IsYggdrasilAddress (ep.address ())
								? 2
								: m_Version); // yggdrasil is always 2 for now
						conn = nullptr;
					}
					else
						LogPrint (eLogInfo, "NTCP2: Incoming session from ", ep.address (), " is already pending");
				}
				else
					LogPrint (eLogError, "NTCP2: Incoming connection from invalid or banned IP ", ep.address ());
			}
			else
				LogPrint (eLogError, "NTCP2: Connected from error ", ec.message ());
		}
		else
		{
			LogPrint (eLogError, "NTCP2: Accept ipv6 error ", error.message ());
			if (error == boost::asio::error::no_descriptors)
			{
				i2p::context.SetErrorV6 (eRouterErrorNoDescriptors);
				return;
			}
		}

		if (error != boost::asio::error::operation_aborted)
		{
			if (!conn) // connection is used, create new one
				conn = std::make_shared<NTCP2Session> (*this);
			else // reuse failed
				conn->Close ();
			m_NTCP2V6Acceptor->async_accept (
				conn->GetSocket (), std::bind (&NTCP2Server::HandleAcceptV6, this, conn, std::placeholders::_1));
		}
	}

	void NTCP2Server::ScheduleTermination ()
	{
		m_TerminationTimer.expires_after (std::chrono::seconds (
			NTCP2_TERMINATION_CHECK_TIMEOUT + m_Rng () % NTCP2_TERMINATION_CHECK_TIMEOUT_VARIANCE));
		m_TerminationTimer.async_wait (std::bind (&NTCP2Server::HandleTerminationTimer, this, std::placeholders::_1));
	}

	void NTCP2Server::HandleTerminationTimer (const boost::system::error_code & ecode)
	{
		if (ecode != boost::asio::error::operation_aborted)
		{
			auto ts = i2p::util::GetSecondsSinceEpoch ();
			// established
			for (auto & it : m_NTCP2Sessions)
				if (it.second->IsTerminationTimeoutExpired (ts))
				{
					auto session = it.second;
					LogPrint (eLogDebug, "NTCP2: No activity for ", session->GetTerminationTimeout (), " seconds");
					session->TerminateByTimeout (); // it doesn't change m_NTCP2Session right a way
				}
				else
					it.second->DeleteNextReceiveBuffer (ts);
			// pending
			for (auto it = m_PendingIncomingSessions.begin (); it != m_PendingIncomingSessions.end ();)
			{
				const auto address = it->first;
				auto session = it->second;
				++it;
				if (session->IsEstablished () || session->IsTerminationTimeoutExpired (ts) || session->IsTerminated ())
				{
					// Terminate() may remove the pending entry itself, so erase
					// by key here instead of using an iterator it invalidated.
					session->Terminate ();
					m_PendingIncomingSessions.erase (address);
				}
			}
			ScheduleTermination ();

			// try to restart acceptors if no description
			// we do it after timer to let timer take descriptor first
			if (i2p::context.GetError () == eRouterErrorNoDescriptors)
			{
				i2p::context.SetError (eRouterErrorNone);
				auto conn = std::make_shared<NTCP2Session> (*this);
				m_NTCP2Acceptor->async_accept (
					conn->GetSocket (), std::bind (&NTCP2Server::HandleAccept, this, conn, std::placeholders::_1));
			}
			if (i2p::context.GetErrorV6 () == eRouterErrorNoDescriptors)
			{
				i2p::context.SetErrorV6 (eRouterErrorNone);
				auto conn = std::make_shared<NTCP2Session> (*this);
				m_NTCP2V6Acceptor->async_accept (
					conn->GetSocket (), std::bind (&NTCP2Server::HandleAcceptV6, this, conn, std::placeholders::_1));
			}
		}
	}

	void NTCP2Server::ConnectWithProxy (std::shared_ptr<NTCP2Session> conn)
	{
		if (!m_ProxyEndpoint) return;
		if (!conn || conn->GetRemoteEndpoint ().address ().is_unspecified ())
		{
			LogPrint (eLogError, "NTCP2: Can't connect to unspecified address");
			return;
		}
		boost::asio::post (GetService (),
			[this, conn] ()
			{
				if (this->AddNTCP2Session (conn))
				{
					auto timer = std::make_shared<boost::asio::steady_timer> (GetService ());
					auto timeout = NTCP2_CONNECT_TIMEOUT; // Java NTCPTransport.ESTABLISH_TIMEOUT
					conn->SetTerminationTimeout (timeout);
					timer->expires_after (std::chrono::seconds (timeout));
					timer->async_wait (
						[conn, timeout] (const boost::system::error_code & ecode)
						{
							if (ecode != boost::asio::error::operation_aborted)
							{
								LogPrint (eLogInfo, "NTCP2: Not connected in ", timeout, " seconds");
								conn->Terminate ();
							}
						});
					conn->GetSocket ().async_connect (*m_ProxyEndpoint,
						std::bind (&NTCP2Server::HandleProxyConnect, this, std::placeholders::_1, conn, timer));
				}
			});
	}

	void NTCP2Server::UseProxy (ProxyType proxytype, const std::string & addr, uint16_t port, const std::string & user,
		const std::string & pass)
	{
		m_ProxyType = proxytype;
		m_ProxyAddress = addr;
		m_ProxyPort = port;
		if (m_ProxyType == eHTTPProxy) m_ProxyAuthorization = i2p::http::CreateBasicAuthorizationString (user, pass);
	}

	void NTCP2Server::HandleProxyConnect (const boost::system::error_code & ecode, std::shared_ptr<NTCP2Session> conn,
		std::shared_ptr<boost::asio::steady_timer> timer)
	{
		if (ecode)
		{
			LogPrint (eLogWarning, "NTCP2: Failed to connect to proxy ", ecode.message ());
			timer->cancel ();
			conn->Terminate ();
			return;
		}
		switch (m_ProxyType)
		{
		case eSocksProxy:
		{
			// TODO: support username/password auth etc
			Socks5Handshake (conn->GetSocket (), conn->GetRemoteEndpoint (),
				[conn, timer] (const boost::system::error_code & ec)
				{
					timer->cancel ();
					if (!ec)
						conn->ClientLogin ();
					else
					{
						LogPrint (eLogError, "NTCP2: SOCKS proxy handshake error ", ec.message ());
						conn->Terminate ();
					}
				});
			break;
		}
		case eHTTPProxy:
		{
			auto & ep = conn->GetRemoteEndpoint ();
			i2p::http::HTTPReq req;
			req.method = "CONNECT";
			req.version = "HTTP/1.1";
			req.uri = i2p::util::net::FormatEndpoint (ep);
			if (!m_ProxyAuthorization.empty ()) req.AddHeader ("Proxy-Authorization", m_ProxyAuthorization);

			// The buffer must outlive the async_write. Keep a shared owner and
			// capture it in the completion handler.
			auto writebuff = std::make_shared<boost::asio::streambuf> ();
			std::ostream out (writebuff.get ());
			out << req.to_string ();

			boost::asio::async_write (conn->GetSocket (), writebuff->data (), boost::asio::transfer_all (),
				[writebuff] (const boost::system::error_code & ec, std::size_t transferred)
				{
					(void)transferred;
					if (ec) LogPrint (eLogError, "NTCP2: HTTP proxy write error ", ec.message ());
				});

			auto readbuff = std::make_shared<boost::asio::streambuf> ();
			boost::asio::async_read_until (conn->GetSocket (), *readbuff, "\r\n\r\n",
				[readbuff, timer, conn] (const boost::system::error_code & ec, std::size_t transferred)
				{
					(void)transferred;
					if (ec)
					{
						LogPrint (eLogError, "NTCP2: HTTP proxy read error ", ec.message ());
						timer->cancel ();
						conn->Terminate ();
					}
					else
					{
						// async_read_until already committed the bytes into the
						// streambuf; committing again duplicates/corrupts them.
						i2p::http::HTTPRes res;
						if (res.parse (std::string { boost::asio::buffers_begin (readbuff->data ()),
								boost::asio::buffers_begin (readbuff->data ()) + readbuff->size () })
							> 0)
						{
							if (res.code == 200)
							{
								timer->cancel ();
								conn->ClientLogin ();
								return;
							}
							else
								LogPrint (eLogError, "NTCP2: HTTP proxy rejected request ", res.code);
						}
						else
							LogPrint (eLogError, "NTCP2: HTTP proxy gave malformed response");
						timer->cancel ();
						conn->Terminate ();
					}
				});
			break;
		}
		default: LogPrint (eLogError, "NTCP2: Unknown proxy type, invalid state");
		}
	}

	void NTCP2Server::SetLocalAddress (const boost::asio::ip::address & localAddress)
	{
		auto addr = std::make_shared<boost::asio::ip::tcp::endpoint> (boost::asio::ip::tcp::endpoint (localAddress, 0));
		if (localAddress.is_v6 ())
		{
			if (i2p::util::net::IsYggdrasilAddress (localAddress))
				m_YggdrasilAddress = addr;
			else
				m_Address6 = addr;
		}
		else
			m_Address4 = addr;
	}

	bool NTCP2Server::AEADChaCha20Poly1305Encrypt (const std::vector<std::pair<uint8_t *, size_t>> & bufs,
		const uint8_t * key, const uint8_t * nonce, uint8_t * mac)
	{
		return m_Encryptor.Encrypt (bufs, key, nonce, mac);
	}

	bool NTCP2Server::AEADChaCha20Poly1305Decrypt (const uint8_t * msg, size_t msgLen, const uint8_t * ad, size_t adLen,
		const uint8_t * key, const uint8_t * nonce, uint8_t * buf, size_t len)
	{
		return m_Decryptor.Decrypt (msg, msgLen, ad, adLen, key, nonce, buf, len);
	}

	void NTCP2Server::SetVersion (int version)
	{
#if OPENSSL_PQ
		const int selected = i2p::crypto::HighestSupportedPQVersion (version, 5);
		m_Version = selected ? selected : 2;
#else
		(void)version;
		m_Version = 2;
#endif
	}
}
}

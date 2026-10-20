/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: LeaseSet.
 *
 * Implements LeaseSet1 and LeaseSet2 parsing, validation, signature checking,
 * lease expiry/selection and store-key derivation, including encrypted LS2
 * with client authentication and per-client keys. Local lease sets are
 * assembled and re-signed with EdDSA/RedDSA, and peer-supplied sections,
 * properties and lengths are bounds-checked before use.
 *
 */

#include <string.h>
#include <algorithm>
#include "I2PEndian.h"
#include "Crypto.h"
#include "PostQuantum.h"
#include "Log.h"
#include "Tag.h"
#include "Timestamp.h"
#include "NetDb.hpp"
#include "Tunnel.h"
#include "CryptoKey.h"
#include "Signature.h"
#include "LeaseSet.h"

namespace i2p
{
namespace data
{
	namespace
	{
		// Encryption key types that i2pd can actually use for a LeaseSet2.
		// Java marks EC_P256/P384/P521 unavailable (EncType.isAvailable() is
		// false for every EncAlgo::EC placeholder), and i2pd routes every
		// non-ratchet type through ElGamal/AES. Selecting a reserved EC type
		// would therefore create a session whose 514-byte ElGamal block is
		// produced by a P256 or unknown encryptor. Keep the sections for
		// IsEncryptionKey(), but never make one the active encryptor.
		bool IsSelectableLS2EncryptionKeyType (CryptoKeyType keyType)
		{
			switch (keyType)
			{
			case CRYPTO_KEY_TYPE_ELGAMAL:
			case CRYPTO_KEY_TYPE_ECIES_X25519_AEAD: return true;
#if OPENSSL_PQ
			case CRYPTO_KEY_TYPE_ECIES_MLKEM512_X25519_AEAD:
			case CRYPTO_KEY_TYPE_ECIES_MLKEM768_X25519_AEAD:
			case CRYPTO_KEY_TYPE_ECIES_MLKEM1024_X25519_AEAD: return i2p::crypto::IsMLKEMSupported (keyType);
#endif
			default: return false;
			}
		}

		// Java EncType.EC_P256 (code 1) carries a 64-byte uncompressed point
		// on the wire. GetCryptoPublicKeyLen() reports 256 for this type only
		// to keep the legacy 256-byte key file layout; LS2 uses the wire size.
		size_t GetLS2PublicKeyLen (CryptoKeyType keyType)
		{
			switch (keyType)
			{
			case CRYPTO_KEY_TYPE_ECIES_P256_SHA256_AES256CBC: return 64;
			// Proposal 145 / common structures: P384 and P521 are reserved
			// wire types with explicit lengths even though they have no
			// encryptor here. Without these cases a section of that type is
			// stored with a zero length and cannot be compared by
			// IsEncryptionKey().
			case 2: return 96; // EC_P384
			case 3: return 132; // EC_P521
			default: return i2p::crypto::GetCryptoPublicKeyLen (keyType);
			}
		}

		// Mapping::FromBuffer() tolerates a malformed or duplicate entry and
		// returns the declared size anyway. Java's DataHelper.readProperties()
		// throws DataFormatException for both, so validate before populating.
		bool ReadProperties (const uint8_t * buf, size_t len, uint16_t size, i2p::util::Mapping & mapping)
		{
			if (!size) return true;
			if (len < (size_t)size + 2) return false;
			size_t offset = 2, end = (size_t)size + 2;
			while (offset < end)
			{
				uint8_t keyLen = buf[offset++];
				if (keyLen > end - offset) return false;
				auto key = std::string_view (reinterpret_cast<const char *> (buf + offset), keyLen);
				offset += keyLen;
				if (offset >= end || buf[offset] != '=') return false;
				offset++;
				if (offset >= end) return false;
				uint8_t valueLen = buf[offset++];
				if (valueLen > end - offset) return false;
				auto value = std::string_view (reinterpret_cast<const char *> (buf + offset), valueLen);
				offset += valueLen;
				if (offset >= end || buf[offset] != ';') return false;
				offset++;
				if (!mapping.Insert (key, value)) return false; // duplicate key
			}
			return offset == end;
		}
	}

	LeaseSet::LeaseSet (bool storeLeases)
		: m_IsValid (false)
		, m_IsIncompatibleCrypto (false)
		, m_StoreLeases (storeLeases)
		, m_ExpirationTime (0)
		, m_BufferLen (0)
	{
	}

	LeaseSet::LeaseSet (const uint8_t * buf, size_t len, bool storeLeases)
		: m_IsValid (true)
		, m_IsIncompatibleCrypto (false)
		, m_StoreLeases (storeLeases)
		, m_ExpirationTime (0)
	{
		m_Buffer = std::make_unique<uint8_t[]> (len);
		if (len) memcpy (m_Buffer.get (), buf, len);
		m_BufferLen = len;
		ReadFromBuffer ();
	}

	void LeaseSet::Update (
		const uint8_t * buf, size_t len, std::shared_ptr<LocalDestination> dest, bool verifySignature)
	{
		SetBuffer (buf, len);
		ReadFromBuffer (false, verifySignature);
	}

	void LeaseSet::PopulateLeases ()
	{
		m_StoreLeases = true;
		ReadFromBuffer (false);
	}

	void LeaseSet::ReadFromBuffer (bool readIdentity, bool verifySignature)
	{
		// Reused objects must not keep a previous valid flag or lease date if
		// this buffer is malformed; every successful path sets them again.
		m_IsValid = false;
		m_EarliestLeaseDate = 0;
		if (readIdentity || !m_Identity) m_Identity = netdb.NewIdentity (m_Buffer.get (), m_BufferLen);
		if (!m_Identity)
		{
			LogPrint (eLogError, "LeaseSet: Can't read identity");
			return;
		}
		size_t size = m_Identity->GetFullLen ();
		if (size + 256 > m_BufferLen)
		{
			LogPrint (eLogError, "LeaseSet: Identity length ", int (size), " exceeds buffer size ", int (m_BufferLen));
			m_IsValid = false;
			return;
		}
		if (m_StoreLeases)
		{
			if (!m_EncryptionKey) m_EncryptionKey = std::make_unique<uint8_t[]> (256);
			memcpy (m_EncryptionKey.get (), m_Buffer.get () + size, 256);
		}
		size += 256; // encryption key
		size += m_Identity->GetSigningPublicKeyLen (); // unused signing key
		if (size + 1 > m_BufferLen)
		{
			LogPrint (eLogError, "LeaseSet: ", int (size), " exceeds buffer size ", int (m_BufferLen));
			m_IsValid = false;
			return;
		}
		uint8_t num = m_Buffer[size];
		size++; // num
		LogPrint (eLogDebug, "LeaseSet: Read num=", (int)num);
		if (!num || num > MAX_NUM_LEASES)
		{
			LogPrint (eLogError, "LeaseSet: Incorrect number of leases", (int)num);
			m_IsValid = false;
			return;
		}
		if (size + num * LEASE_SIZE > m_BufferLen)
		{
			LogPrint (eLogError, "LeaseSet: ", int (size), " exceeds buffer size ", int (m_BufferLen));
			m_IsValid = false;
			return;
		}

		// KademliaNetworkDatabaseFacade.validate() rejects a lease set whose
		// earliest lease is more than 10 minutes old and whose latest lease is
		// more than CLOCK_FUDGE + MAX_LEASE_FUTURE ahead. Do this before
		// changing any stored leases so a forged far-future LS1 cannot occupy
		// a netDb entry indefinitely.
		auto ts = i2p::util::GetMillisecondsSinceEpoch ();
		const uint8_t * leases = m_Buffer.get () + size;
		uint64_t earliestLease = UINT64_MAX;
		for (int i = 0; i < num; i++)
		{
			auto endDate = bufbe64toh (leases + i * LEASE_SIZE + 36);
			if (endDate > ts + LEASESET_MAX_LEASE_FUTURE + LEASESET2_CLOCK_FUDGE)
			{
				LogPrint (eLogWarning, "LeaseSet: Lease end date is too far in the future ", endDate);
				m_IsValid = false;
				return;
			}
			if (endDate < earliestLease) earliestLease = endDate;
		}
		if (earliestLease + LEASESET_MAX_LEASE_AGE <= ts)
		{
			LogPrint (eLogWarning, "LeaseSet: Earliest lease is too old ", earliestLease);
			m_IsValid = false;
			return;
		}
		m_EarliestLeaseDate = earliestLease;

		UpdateLeasesBegin ();
		// process leases
		m_ExpirationTime = 0;
		for (int i = 0; i < num; i++)
		{
			Lease lease;
			lease.tunnelGateway = leases;
			leases += 32; // gateway
			lease.tunnelID = bufbe32toh (leases);
			leases += 4; // tunnel ID
			lease.endDate = bufbe64toh (leases);
			leases += 8; // end date
			UpdateLease (lease, ts);
		}
		if (!m_ExpirationTime)
		{
			LogPrint (eLogWarning, "LeaseSet: All leases are expired. Dropped");
			m_IsValid = false;
			return;
		}
		m_ExpirationTime += LEASE_ENDDATE_THRESHOLD;
		UpdateLeasesEnd ();
		m_IsValid = true;

		// verify
		if (verifySignature)
		{
			auto signedSize = leases - m_Buffer.get ();
			if (signedSize + m_Identity->GetSignatureLen () > m_BufferLen)
			{
				LogPrint (eLogError, "LeaseSet: Signature exceeds buffer size ", int (m_BufferLen));
				m_IsValid = false;
			}
			else if (!m_Identity->Verify (m_Buffer.get (), signedSize, leases))
			{
				LogPrint (eLogWarning, "LeaseSet: Verification failed");
				m_IsValid = false;
			}
		}
	}

	void LeaseSet::UpdateLeasesBegin ()
	{
		// reset existing leases
		if (m_StoreLeases)
			for (auto & it : m_Leases) it->isUpdated = false;
		else
			m_Leases.clear ();
	}

	void LeaseSet::UpdateLeasesEnd ()
	{
		// delete old leases
		if (m_StoreLeases)
		{
			for (auto it = m_Leases.begin (); it != m_Leases.end ();)
			{
				if (!(*it)->isUpdated)
				{
					(*it)->endDate = 0; // somebody might still hold it
					m_Leases.erase (it++);
				}
				else
					++it;
			}
		}
	}

	void LeaseSet::UpdateLease (const Lease & lease, uint64_t ts)
	{
		if (ts < lease.endDate + LEASE_ENDDATE_THRESHOLD)
		{
			if (lease.endDate > m_ExpirationTime) m_ExpirationTime = lease.endDate;
			if (m_StoreLeases)
			{
				auto ret = m_Leases.insert (i2p::data::netdb.NewLease (lease));
				if (!ret.second) (*ret.first)->endDate = lease.endDate; // update existing
				(*ret.first)->isUpdated = true;
			}
		}
		else
			LogPrint (eLogWarning, "LeaseSet: Lease is expired already");
	}

	uint64_t LeaseSet::ExtractExpirationTimestamp (const uint8_t * buf, size_t len) const
	{
		if (!buf || !len || !m_Identity) return 0;
		size_t size = m_Identity->GetFullLen ();
		if (size > len) return 0;
		size += 256; // encryption key
		size += m_Identity->GetSigningPublicKeyLen (); // unused signing key
		if (size >= len) return 0; // need at least the num leases byte
		uint8_t num = buf[size];
		size++; // num
		if (size + num * LEASE_SIZE > len) return 0;
		uint64_t timestamp = 0;
		for (int i = 0; i < num; i++)
		{
			size += 36; // gateway (32) + tunnelId(4)
			auto endDate = bufbe64toh (buf + size);
			size += 8; // end date
			if (!timestamp || endDate < timestamp) timestamp = endDate;
		}
		return timestamp;
	}

	bool LeaseSet::IsNewer (const uint8_t * buf, size_t len) const
	{
		return ExtractExpirationTimestamp (buf, len) > ExtractExpirationTimestamp (m_Buffer.get (), m_BufferLen);
	}

	bool LeaseSet::ExpiresSoon (const uint64_t dlt, const uint64_t fudge) const
	{
		auto now = i2p::util::GetMillisecondsSinceEpoch ();
		if (fudge) now += i2p::crypto::RandomBelow (static_cast<uint32_t> (fudge));
		if (now >= m_ExpirationTime) return true;
		return m_ExpirationTime - now <= dlt;
	}

	const std::vector<std::shared_ptr<const Lease>> LeaseSet::GetNonExpiredLeases (bool withThreshold) const
	{
		return GetNonExpiredLeasesExcluding ([] (const Lease & l) -> bool { return false; }, withThreshold);
	}

	const std::vector<std::shared_ptr<const Lease>> LeaseSet::GetNonExpiredLeasesExcluding (
		LeaseInspectFunc exclude, bool withThreshold) const
	{
		auto ts = i2p::util::GetMillisecondsSinceEpoch ();
		std::vector<std::shared_ptr<const Lease>> leases;
		for (const auto & it : m_Leases)
		{
			auto endDate = it->endDate;
			if (withThreshold)
				endDate += LEASE_ENDDATE_THRESHOLD;
			else
				endDate -= LEASE_ENDDATE_THRESHOLD;
			if (ts < endDate && !exclude (*it)) leases.push_back (it);
		}
		return leases;
	}

	bool LeaseSet::HasExpiredLeases () const
	{
		auto ts = i2p::util::GetMillisecondsSinceEpoch ();
		for (const auto & it : m_Leases)
			if (ts >= it->endDate) return true;
		return false;
	}

	bool LeaseSet::IsExpired () const
	{
		// A Meta LS2 has no tunnel leases; its header expiration is
		// authoritative (proposal 123). Do not treat it as an empty LS1.
		if (m_StoreLeases && IsEmpty () && GetStoreType () != NETDB_STORE_TYPE_META_LEASESET2) return true;
		auto ts = i2p::util::GetMillisecondsSinceEpoch ();
		return ts > m_ExpirationTime;
	}

	void LeaseSet::Encrypt (const uint8_t * data, uint8_t * encrypted) const
	{
		if (!m_Identity || !m_EncryptionKey || !data || !encrypted) return;
		auto encryptor = m_Identity->CreateEncryptor (m_EncryptionKey.get ());
		if (encryptor) encryptor->Encrypt (data, encrypted);
	}

	void LeaseSet::SetBuffer (const uint8_t * buf, size_t len)
	{
		if (len > MAX_LS_BUFFER_SIZE)
		{
			LogPrint (eLogError, "LeaseSet: Buffer is too long ", len);
			len = MAX_LS_BUFFER_SIZE;
		}
		if (m_Buffer && len > m_BufferLen)
		{
			m_Buffer.reset ();
		}
		if (!m_Buffer) m_Buffer = std::make_unique<uint8_t[]> (len);
		m_BufferLen = len;
		if (len) memcpy (m_Buffer.get (), buf, len);
	}

	void LeaseSet::SetBufferLen (size_t len)
	{
		if (len <= m_BufferLen)
			m_BufferLen = len;
		else
			LogPrint (eLogError, "LeaseSet2: Actual buffer size ", int (len), " exceeds full buffer size ",
				int (m_BufferLen));
	}

	LeaseSet2::LeaseSet2 (uint8_t storeType, const uint8_t * buf, size_t len, bool storeLeases,
		std::shared_ptr<LocalDestination> dest, CryptoKeyType preferredCrypto)
		: LeaseSet (storeLeases)
		, m_StoreType (storeType)
		, m_EncryptionType (0)
		, m_PreferredEncryptionType (preferredCrypto)
	{
		SetBuffer (buf, len);
		if (storeType == NETDB_STORE_TYPE_ENCRYPTED_LEASESET2)
			ReadFromBufferEncrypted (GetBuffer (), GetBufferLen (), nullptr, dest, nullptr);
		else
			ReadFromBuffer (GetBuffer (), GetBufferLen (), dest);
	}

	LeaseSet2::LeaseSet2 (const uint8_t * buf, size_t len, std::shared_ptr<const BlindedPublicKey> key,
		std::shared_ptr<LocalDestination> dest, const uint8_t * secret, CryptoKeyType preferredCrypto)
		: LeaseSet (true)
		, m_StoreType (NETDB_STORE_TYPE_ENCRYPTED_LEASESET2)
		, m_EncryptionType (0)
		, m_PreferredEncryptionType (preferredCrypto)
	{
		SetBuffer (buf, len);
		ReadFromBufferEncrypted (GetBuffer (), GetBufferLen (), key, dest, secret);
	}

	void LeaseSet2::Update (
		const uint8_t * buf, size_t len, std::shared_ptr<LocalDestination> dest, bool verifySignature)
	{
		SetBuffer (buf, len);
		if (GetStoreType () != NETDB_STORE_TYPE_ENCRYPTED_LEASESET2)
			ReadFromBuffer (GetBuffer (), GetBufferLen (), dest, false, verifySignature);
		else
			// A floodfill cannot decrypt type 5 and has no secret/blinded key
			// here. Validate and keep the outer layer instead of leaving the
			// previous buffer's validity and leases in place.
			ReadFromBufferEncrypted (GetBuffer (), GetBufferLen (), nullptr, dest, nullptr);
	}

	bool LeaseSet2::IsNewer (const uint8_t * buf, size_t len) const
	{
		uint64_t expiration;
		return ExtractPublishedTimestamp (buf, len, expiration) > m_PublishedTimestamp;
	}

	void LeaseSet2::ReadFromBuffer (const uint8_t * buf, size_t len, std::shared_ptr<LocalDestination> dest,
		bool readIdentity, bool verifySignature)
	{
		// The same object is reused by Update(). Do not inherit validity,
		// offline verifier, flags or encryption keys from the old buffer.
		SetIsValid (false);
		m_TransientVerifier = nullptr;
		m_IsPublic = true;
		m_IsPublishedEncrypted = false;
		m_EncryptionType = 0;
		m_Encryptor = nullptr;
		SetIsIncompatibleCrypto (false);
		SetEarliestLeaseDate (0);
		m_EncryptionKeys.clear ();
		m_Properties.CleanUp ();

		// standard LS2 header
		std::shared_ptr<const IdentityEx> identity;
		if (readIdentity || !GetIdentity ())
		{
			identity = netdb.NewIdentity (buf, len);
			if (!identity || !identity->GetFullLen ()) return;
			SetIdentity (identity);
		}
		else
			identity = GetIdentity ();
		size_t offset = identity->GetFullLen ();
		if (offset + 8 > len) return;
		m_PublishedTimestamp = bufbe32toh (buf + offset);
		offset += 4; // published timestamp (seconds)
		uint16_t expires = bufbe16toh (buf + offset);
		offset += 2; // expires (seconds)
		SetExpirationTime ((static_cast<uint64_t> (m_PublishedTimestamp) + expires) * 1000LL); // in milliseconds
		uint16_t flags = bufbe16toh (buf + offset);
		offset += 2; // flags
		if (flags & LEASESET2_FLAG_OFFLINE_KEYS)
		{
			// transient key
			m_TransientVerifier = ProcessOfflineSignature (identity, buf, len, offset);
			if (!m_TransientVerifier)
			{
				LogPrint (eLogError, "LeaseSet2: Offline signature failed");
				return;
			}
		}
		if (flags & LEASESET2_FLAG_UNPUBLISHED_LEASESET) m_IsPublic = false;
		// 0x04 (Java FLAG_BLINDED) only says that a future publication of
		// this unencrypted LS2 will be blinded/encrypted. It must not clear
		// the UNPUBLISHED flag: Java's getReceivedAsPublished() is
		// super && !isUnpublished(), so a 0x06 LS2 stays unpublished.
		if (flags & LEASESET2_FLAG_PUBLISHED_ENCRYPTED) m_IsPublishedEncrypted = true;
		// type specific part. Type 7 (Meta LS2) shares the standard LS2
		// header but has MetaLease entries (proposal 123, MetaLeaseSet.java).
		size_t s = 0;
		switch (m_StoreType)
		{
		case NETDB_STORE_TYPE_STANDARD_LEASESET2:
			s = ReadStandardLS2TypeSpecificPart (buf + offset, len - offset, dest);
			break;
		case NETDB_STORE_TYPE_META_LEASESET2: s = ReadMetaLS2TypeSpecificPart (buf + offset, len - offset); break;
		default: LogPrint (eLogWarning, "LeaseSet2: Store type ", (int)m_StoreType, " is not supported"); return;
		}
		if (!s) return;
		offset += s;
		// The signature is part of the structure even when the caller already
		// verified it (Update/PopulateLeases). Reject a truncated trailing
		// signature before marking the object valid.
		auto signatureLen
			= m_TransientVerifier ? m_TransientVerifier->GetSignatureLen () : identity->GetSignatureLen ();
		if (offset > len || signatureLen > len - offset)
		{
			LogPrint (eLogWarning, "LeaseSet2: short buffer: wanted ", int (offset + signatureLen), " bytes, have ",
				int (len));
			return;
		}
		if (verifySignature || m_TransientVerifier)
		{
			// verify signature
			bool verified = m_TransientVerifier ? VerifySignature (m_TransientVerifier, buf, len, offset)
												: VerifySignature (identity, buf, len, offset);
			SetIsValid (verified);
		}
		else
			SetIsValid (true);
		offset += signatureLen;
		SetBufferLen (offset);
	}

	template <typename Verifier>
	bool LeaseSet2::VerifySignature (Verifier & verifier, const uint8_t * buf, size_t len, size_t signatureOffset)
	{
		auto signatureLen = verifier->GetSignatureLen ();
		if (signatureOffset > len || signatureLen > len - signatureOffset) return false;
		// The signed data is the canonical store type followed by the buffer.
		// Do not write buf[-1]: callers may pass a buffer that has no preceding
		// byte (for example the inner layer extracted from I2CP) and the
		// previous implementation performed an out-of-bounds store.
		std::vector<uint8_t> signedData;
		signedData.reserve (signatureOffset + 1);
		signedData.push_back (m_StoreType);
		signedData.insert (signedData.end (), buf, buf + signatureOffset);
		bool verified = verifier->Verify (signedData.data (), signedData.size (), buf + signatureOffset);
		if (!verified) LogPrint (eLogWarning, "LeaseSet2: Verification failed");
		return verified;
	}

	size_t LeaseSet2::ReadStandardLS2TypeSpecificPart (
		const uint8_t * buf, size_t len, std::shared_ptr<LocalDestination> dest)
	{
		size_t offset = 0;
		// properties
		if (offset + 2 > len) return 0;
		uint16_t propertiesLen = bufbe16toh (buf + offset);
		if (propertiesLen > len - offset - 2) return 0;
		if (!ReadProperties (buf + offset, len - offset, propertiesLen, m_Properties)) return 0;
		offset += propertiesLen + 2;
		// key sections (LeaseSet2.java rejects 0 or more than MAX_KEYS)
		CryptoKeyType newEncryptionType = 0;
		std::shared_ptr<i2p::crypto::CryptoKeyEncryptor> newEncryptor;
		bool preferredKeyFound = false;
		if (offset + 1 > len) return 0;
		int numKeySections = buf[offset];
		offset++;
		if (numKeySections <= 0 || numKeySections > MAX_NUM_ENCRYPTION_KEYS)
		{
			LogPrint (eLogWarning, "LeaseSet2: Bad key count ", numKeySections);
			return 0;
		}
		m_EncryptionKeys.reserve (numKeySections);
		for (int i = 0; i < numKeySections; i++)
		{
			if (offset + 4 > len) return 0;
			uint16_t keyType = bufbe16toh (buf + offset);
			offset += 2; // encryption key type
			uint16_t encryptionKeyLen = bufbe16toh (buf + offset);
			offset += 2;
			if (offset + encryptionKeyLen > len) return 0;
			// LeaseSet2.java carries the explicit encLen on the wire. Use the
			// LS2 wire length (EC_P256 is 64 bytes), not the legacy key-file
			// size, so valid EC_P256 sections are not discarded.
			auto expectedKeyLen = GetLS2PublicKeyLen (keyType);
			if (expectedKeyLen && expectedKeyLen != encryptionKeyLen)
			{
				// Java PublicKey(type, data) rejects a known key type whose
				// explicit length does not match the type. Silently skipping
				// it used to make a malformed section part of a valid LS2.
				LogPrint (eLogWarning, "LeaseSet2: Key type ", keyType, " has length ", encryptionKeyLen, ", expected ",
					expectedKeyLen);
				return 0;
			}
			if (expectedKeyLen)
			{
				// Preserve every section in wire order. Java keeps a List and
				// does not reject a repeated type; overwriting an earlier entry
				// would make IsEncryptionKey() reject a key the publisher (and
				// the peer's ratchet) actually advertised.
				m_EncryptionKeys.push_back (
					{ keyType, std::vector<uint8_t> (buf + offset, buf + offset + encryptionKeyLen) });
			}
			if (IsStoreLeases () && !preferredKeyFound && expectedKeyLen && // create encryptor with leases only
				IsSelectableLS2EncryptionKeyType (keyType) && (!dest || dest->SupportsEncryptionType (keyType)))
			{
				// Prefer the locally configured type (or the type the peer's
				// ratchet session selected), otherwise keep the first
				// supported key in the publisher's preference order without
				// advancing (proposal 123, "Encryption Key Preference").
				if (!newEncryptor || keyType == m_PreferredEncryptionType)
				{
					auto encryptor = i2p::data::IdentityEx::CreateEncryptor (keyType, buf + offset);
					if (encryptor)
					{
						newEncryptor = encryptor;
						newEncryptionType = keyType;
						preferredKeyFound = keyType == m_PreferredEncryptionType;
					}
				}
			}
			offset += encryptionKeyLen;
		}
		if (newEncryptor)
		{
			m_EncryptionType = newEncryptionType;
			m_Encryptor = newEncryptor; // TODO: atomic
			SetIsIncompatibleCrypto (false);
		}
		else
		{
			m_EncryptionType = 0;
			m_Encryptor = nullptr;
			SetIsIncompatibleCrypto (true);
		}

		// leases
		if (offset + 1 > len) return 0;
		int numLeases = buf[offset];
		offset++;
		if (numLeases <= 0 || numLeases > MAX_NUM_LEASES)
		{
			LogPrint (eLogWarning, "LeaseSet2: Bad lease count ", numLeases);
			return 0;
		}
		if ((size_t)numLeases * LEASE2_SIZE > len - offset) return 0;
		auto ts = i2p::util::GetMillisecondsSinceEpoch ();
		// KademliaNetworkDatabaseFacade.MAX_LEASE_FUTURE plus clock fudge.
		// The old 12-minute check rejected valid LS2s from peers whose clock
		// was a few minutes fast.
		if (GetExpirationTime () > ts + LEASESET_MAX_LEASE_FUTURE + LEASESET2_CLOCK_FUDGE)
		{
			LogPrint (eLogWarning, "LeaseSet2: Expiration time is from future ", GetExpirationTime () / 1000LL);
			return 0;
		}
		if (GetExpirationTime () + LEASESET2_CLOCK_FUDGE <= ts)
		{
			// LeaseSet expiration is authoritative (KademliaNetworkDatabaseFacade.validate)
			LogPrint (eLogWarning, "LeaseSet2: Expiration time is in the past ", GetExpirationTime () / 1000LL);
			return 0;
		}
		// Java validates the individual leases, not the published timestamp,
		// for a standard LS2: earliest > now - 10 min, latest > now - fudge,
		// latest <= now + CLOCK_FUDGE + MAX_LEASE_FUTURE. A stale published
		// timestamp with current leases is therefore still valid.
		uint64_t earliestLease = UINT64_MAX, latestLease = 0;
		for (int i = 0; i < numLeases; i++)
		{
			uint64_t endDate = static_cast<uint64_t> (bufbe32toh (buf + offset + i * LEASE2_SIZE + 36)) * 1000LL;
			if (endDate > ts + LEASESET_MAX_LEASE_FUTURE + LEASESET2_CLOCK_FUDGE)
			{
				LogPrint (eLogWarning, "LeaseSet2: Lease end date is from future ", endDate);
				return 0;
			}
			if (endDate < earliestLease) earliestLease = endDate;
			if (endDate > latestLease) latestLease = endDate;
		}
		if (earliestLease + LEASESET_MAX_LEASE_AGE <= ts)
		{
			LogPrint (eLogWarning, "LeaseSet2: Earliest lease is too old ", earliestLease);
			return 0;
		}
		if (latestLease + LEASESET2_CLOCK_FUDGE <= ts)
		{
			LogPrint (eLogWarning, "LeaseSet2: All leases are expired");
			return 0;
		}
		SetEarliestLeaseDate (earliestLease);
		if (IsStoreLeases ()) UpdateLeasesBegin ();
		for (int i = 0; i < numLeases; i++)
		{
			Lease lease;
			lease.tunnelGateway = buf + offset;
			offset += 32; // gateway
			lease.tunnelID = bufbe32toh (buf + offset);
			offset += 4; // tunnel ID
			lease.endDate = bufbe32toh (buf + offset) * 1000LL;
			offset += 4; // end date
			if (IsStoreLeases ()) UpdateLease (lease, ts);
		}
		if (IsStoreLeases ()) UpdateLeasesEnd ();

		return (offset > len ? 0 : offset);
	}

	size_t LeaseSet2::ReadMetaLS2TypeSpecificPart (const uint8_t * buf, size_t len)
	{
		// Proposal 123, Meta LS2. Same layout as MetaLeaseSet.java: properties,
		// one byte entry count, MetaLease entries (40 bytes), one byte
		// revocation count, 32-byte revocations, then the standard signature.
		size_t offset = 0;
		// properties
		if (offset + 2 > len) return 0;
		uint16_t propertiesLen = bufbe16toh (buf + offset);
		if (propertiesLen > len - offset - 2) return 0;
		if (!ReadProperties (buf + offset, len - offset, propertiesLen, m_Properties)) return 0;
		offset += propertiesLen + 2;
		// entries
		if (offset + 1 > len) return 0;
		uint8_t numEntries = buf[offset++];
		// MetaLeaseSet.readBytes() adds every entry through LeaseSet.addLease(),
		// which enforces MAX_LEASES (16); a 17th entry throws.
		if (numEntries > MAX_NUM_LEASES)
		{
			LogPrint (eLogWarning, "LeaseSet2: Too many Meta leases ", (int)numEntries);
			return 0;
		}
		if ((size_t)numEntries * META_LEASE_SIZE > len - offset) return 0;
		auto ts = i2p::util::GetMillisecondsSinceEpoch ();
		uint64_t expiration = GetExpirationTime ();
		// Meta LS2 may live for hours (proposal 123: max 18.2 hours).
		if (expiration + LEASESET2_CLOCK_FUDGE <= ts
			|| expiration > ts + LEASESET2_META_MAX_FUTURE + LEASESET2_CLOCK_FUDGE)
		{
			LogPrint (eLogWarning, "LeaseSet2: Meta expiration time is invalid ", expiration / 1000LL);
			return 0;
		}
		if (m_PublishedTimestamp * 1000LL + LEASESET_MAX_LEASE_AGE <= ts)
		{
			LogPrint (eLogWarning, "LeaseSet2: Meta published time is too old ", m_PublishedTimestamp);
			return 0;
		}
		if (numEntries == 0)
		{
			LogPrint (eLogWarning, "LeaseSet2: Meta LeaseSet has no entries");
			return 0;
		}
		uint64_t earliestEntry = UINT64_MAX, latestEntry = 0;
		bool hasCurrentEntry = false;
		for (int i = 0; i < numEntries; i++)
		{
			const uint8_t * entry = buf + offset;
			// flags at entry[32..33], type at entry[34], cost at entry[35],
			// 4-byte expiration at entry[36..39].
			uint64_t endDate = bufbe32toh (entry + 36) * 1000LL;
			if (endDate < earliestEntry) earliestEntry = endDate;
			if (endDate > latestEntry) latestEntry = endDate;
			if (endDate + LEASESET2_CLOCK_FUDGE > ts) hasCurrentEntry = true;
			offset += META_LEASE_SIZE;
		}
		// Java's validate() uses min(earliest entry, published) for the age
		// check and min(latest entry, header expiration) for freshness. A
		// child entry may legitimately outlive the header; only the minimum
		// bounds the MetaLeaseSet itself.
		if (std::min (latestEntry, expiration) > ts + LEASESET2_META_MAX_FUTURE + LEASESET2_CLOCK_FUDGE)
		{
			LogPrint (eLogWarning, "LeaseSet2: Meta latest lease/expiration is from future ",
				std::min (latestEntry, expiration));
			return 0;
		}
		if (earliestEntry + LEASESET_MAX_LEASE_AGE <= ts)
		{
			LogPrint (eLogWarning, "LeaseSet2: Earliest Meta lease is too old ", earliestEntry);
			return 0;
		}
		if (!hasCurrentEntry)
		{
			LogPrint (eLogWarning, "LeaseSet2: All Meta leases are expired");
			return 0;
		}
		// Java's MetaLeaseSet derives getEarliestLeaseDate() from its entries,
		// so cross-type LS1/Meta replacement must compare this value rather
		// than the zero sentinel used for encrypted outer layers.
		SetEarliestLeaseDate (earliestEntry);
		// revocations (validated for length; revocation processing is not
		// implemented, as in MetaLeaseSet.java where it is also a TODO)
		if (offset + 1 > len) return 0;
		uint8_t numRevocations = buf[offset++];
		if ((size_t)numRevocations * 32 > len - offset) return 0;
		offset += (size_t)numRevocations * 32;
		return offset;
	}

	void LeaseSet2::ReadFromBufferEncrypted (const uint8_t * buf, size_t len,
		std::shared_ptr<const BlindedPublicKey> key, std::shared_ptr<LocalDestination> dest, const uint8_t * secret)
	{
		// Reused objects (Update) must not keep the previous outer validity,
		// leases, keys or flags while parsing a new encrypted buffer.
		SetIsValid (false);
		UpdateLeasesBegin ();
		UpdateLeasesEnd ();
		SetExpirationTime (0);
		SetIdentity (nullptr); // an encrypted outer layer has no Destination
		m_StoreType = NETDB_STORE_TYPE_ENCRYPTED_LEASESET2;
		m_TransientVerifier = nullptr;
		m_HasStoreHash = false;
		m_IsPublic = true;
		m_IsPublishedEncrypted = false;
		m_EncryptionType = 0;
		m_Encryptor = nullptr;
		SetIsIncompatibleCrypto (false);
		SetEarliestLeaseDate (0);
		m_EncryptionKeys.clear ();
		m_Properties.CleanUp ();

		size_t offset = 0;
		// blinded key
		if (len < 2) return;
		const uint8_t * stA1 = buf + offset; // stA1 = blinded signature type, 2 bytes big endian
		uint16_t blindedKeyType = bufbe16toh (stA1);
		offset += 2;
		std::unique_ptr<i2p::crypto::Verifier> blindedVerifier (i2p::data::IdentityEx::CreateVerifier (blindedKeyType));
		if (!blindedVerifier) return;
		auto blindedKeyLen = blindedVerifier->GetPublicKeyLen ();
		if (offset + blindedKeyLen > len) return;
		const uint8_t * blindedPublicKey = buf + offset;
		blindedVerifier->SetPublicKey (blindedPublicKey);
		{
			// Proposal 123: store key = SHA256(blinded sig type || blinded public key).
			std::vector<uint8_t> hashInput (2 + blindedKeyLen);
			memcpy (hashInput.data (), stA1, 2);
			memcpy (hashInput.data () + 2, blindedPublicKey, blindedKeyLen);
			uint8_t hash[32];
			SHA256 (hashInput.data (), hashInput.size (), hash);
			memcpy (m_StoreHash, hash, 32);
			m_HasStoreHash = true;
		}
		offset += blindedKeyLen;
		// expiration
		if (offset + 8 > len) return;
		const uint8_t * publishedTimestamp = buf + offset;
		m_PublishedTimestamp = bufbe32toh (publishedTimestamp);
		offset += 4; // published timestamp (seconds)
		uint16_t expires = bufbe16toh (buf + offset);
		offset += 2; // expires (seconds)
		SetExpirationTime ((static_cast<uint64_t> (m_PublishedTimestamp) + expires) * 1000LL); // in milliseconds
		uint16_t flags = bufbe16toh (buf + offset);
		offset += 2; // flags
		// Java's getReceivedAsPublished() excludes unpublished entries for
		// every LS2 type, including encrypted LS2.
		if (flags & LEASESET2_FLAG_UNPUBLISHED_LEASESET) m_IsPublic = false;
		if (flags & LEASESET2_FLAG_OFFLINE_KEYS)
		{
			// transient key
			m_TransientVerifier = ProcessOfflineSignature (blindedVerifier, buf, len, offset);
			if (!m_TransientVerifier)
			{
				LogPrint (eLogError, "LeaseSet2: Offline signature failed");
				return;
			}
		}
		// outer ciphertext
		if (offset + 2 > len) return;
		uint16_t lenOuterCiphertext = bufbe16toh (buf + offset);
		offset += 2;
		// EncryptedLeaseSet.MIN_ENCRYPTED_SIZE is 8 + 16 = 24. The layer 1
		// plaintext still needs a salt and a flag byte, but that is checked
		// below once the outer signature has been authenticated.
		if (lenOuterCiphertext < 24 || lenOuterCiphertext > len - offset) return;
		const uint8_t * outerCiphertext = buf + offset;
		offset += lenOuterCiphertext;
		// verify signature
		bool verified = m_TransientVerifier ? VerifySignature (m_TransientVerifier, buf, len, offset)
											: VerifySignature (blindedVerifier, buf, len, offset);
		SetIsValid (verified);
		// handle ciphertext
		if (verified && key)
		{
			// We must verify the inner LeaseSet again in Layer 2. Once a key
			// is supplied a too-short ciphertext is malformed, not merely an
			// outer-only entry that a floodfill would store.
			SetIsValid (false);
			if (lenOuterCiphertext < 33) // outerSalt(32) + layer 1 flags(1)
			{
				LogPrint (eLogError, "LeaseSet2: Encrypted payload is too short ", lenOuterCiphertext);
				return;
			}
			if (blindedKeyType == key->GetBlindedSigType ())
			{
				// verify blinding
				char date[9];
				i2p::util::GetDateString (m_PublishedTimestamp, date);
				std::vector<uint8_t> blinded (blindedKeyLen);
				key->GetBlindedKey (date, blinded.data ());
				if (memcmp (blindedPublicKey, blinded.data (), blindedKeyLen))
				{
					LogPrint (eLogError, "LeaseSet2: Blinded public key doesn't match");
					return;
				}
			}
			else
			{
				LogPrint (eLogError, "LeaseSet2: Unexpected blinded key type ", blindedKeyType, " instead ",
					key->GetBlindedSigType ());
				return;
			}
			// Remember the outer metadata. The inner layer must not make the
			// object appear newer or valid for longer than the signed outer
			// expiry that the netDb store rules validated.
			const uint32_t outerPublishedTimestamp = m_PublishedTimestamp;
			const uint64_t outerExpirationTime = GetExpirationTime ();
			const bool outerIsPublic = m_IsPublic;
			// outer key
			// outerInput = subcredential || publishedTimestamp
			uint8_t subcredential[36];
			key->GetSubcredential (blindedPublicKey, blindedKeyLen, subcredential);
			memcpy (subcredential + 32, publishedTimestamp, 4);
			// outerSalt = outerCiphertext[0:32]
			// keys = HKDF(outerSalt, outerInput, "ELS2_L1K", 44)
			uint8_t keys[64]; // 44 bytes actual data
			i2p::crypto::HKDF (outerCiphertext, subcredential, 36, "ELS2_L1K", keys);
			// decrypt Layer 1
			// outerKey = keys[0:31]
			// outerIV = keys[32:43]
			size_t lenOuterPlaintext = lenOuterCiphertext - 32;
			std::vector<uint8_t> outerPlainText (lenOuterPlaintext);
			i2p::crypto::ChaCha20 (outerCiphertext + 32, lenOuterPlaintext, keys, keys + 32, outerPlainText.data ());
			// inner key
			// innerInput = authCookie || subcredential || publishedTimestamp
			// innerSalt = innerCiphertext[0:32]
			// keys = HKDF(innerSalt, innerInput, "ELS2_L2K", 44)
			uint8_t innerInput[68];
			size_t authDataLen
				= ExtractClientAuthData (outerPlainText.data (), lenOuterPlaintext, secret, subcredential, innerInput);
			if (authDataLen == static_cast<size_t> (-1))
			{
				LogPrint (eLogError, "LeaseSet2: Invalid or undecryptable client auth data");
				return;
			}
			if (lenOuterPlaintext
				< authDataLen + 34) // flag + authData + inner salt + at least one byte of inner plaintext
			{
				LogPrint (eLogError, "LeaseSet2: Outer plaintext is too short ", lenOuterPlaintext);
				return;
			}
			if (authDataLen > 0)
			{
				memcpy (innerInput + 32, subcredential, 36);
				i2p::crypto::HKDF (outerPlainText.data () + 1 + authDataLen, innerInput, 68, "ELS2_L2K", keys);
			}
			else
				// no authData presented, innerInput = subcredential || publishedTimestamp
				// skip 1 byte flags
				i2p::crypto::HKDF (outerPlainText.data () + 1, subcredential, 36, "ELS2_L2K", keys); // no authCookie
			// decrypt Layer 2
			// innerKey = keys[0:31]
			// innerIV = keys[32:43]
			size_t lenInnerPlaintext = lenOuterPlaintext - 32 - 1 - authDataLen;
			std::vector<uint8_t> innerPlainText (lenInnerPlaintext);
			i2p::crypto::ChaCha20 (outerPlainText.data () + 32 + 1 + authDataLen, lenInnerPlaintext, keys, keys + 32,
				innerPlainText.data ());
			if (innerPlainText[0] == NETDB_STORE_TYPE_STANDARD_LEASESET2
				|| innerPlainText[0] == NETDB_STORE_TYPE_META_LEASESET2)
			{
				// override store type and buffer
				m_StoreType = innerPlainText[0];
				SetBuffer (innerPlainText.data () + 1, lenInnerPlaintext - 1);
				m_TransientVerifier = nullptr; // Layer 2 has a signer of its own
				// parse and verify Layer 2
				ReadFromBuffer (innerPlainText.data () + 1, lenInnerPlaintext - 1, dest);
				// Keep the outer timestamp/flags for IsNewer/IsPublic and do
				// not extend the lifetime beyond the signed outer expiry.
				m_PublishedTimestamp = outerPublishedTimestamp;
				if (GetExpirationTime () > outerExpirationTime) SetExpirationTime (outerExpirationTime);
				m_IsPublic = outerIsPublic;
				// Preserve the inner PUBLISHED_ENCRYPTED bit. Streaming uses
				// IsPublishedEncrypted() to decide whether a refresh must ask
				// for the blinded LeaseSet after the decrypted leases expire.
				// Only the outer public/expiry metadata is authoritative.
			}
			else
				LogPrint (eLogError, "LeaseSet2: Unexpected LeaseSet type ", (int)innerPlainText[0],
					" inside encrypted LeaseSet");
		}
		else
		{
			// we set actual length of encrypted buffer
			offset
				+= m_TransientVerifier ? m_TransientVerifier->GetSignatureLen () : blindedVerifier->GetSignatureLen ();
			if (offset > len) return;
			SetBufferLen (offset);
		}
	}

	// helper for ExtractClientAuthData
	static inline bool GetAuthCookie (
		const uint8_t * authClients, int numClients, const uint8_t * okm, uint8_t * authCookie)
	{
		// try to find clientCookie_i for clientID_i = okm[44:51]
		for (int i = 0; i < numClients; i++)
		{
			if (!memcmp (okm + 44, authClients + i * 40, 8)) // clientID_i
			{
				// clientKey_i = okm[0:31]
				// clientIV_i = okm[32:43]
				i2p::crypto::ChaCha20 (authClients + i * 40 + 8, 32, okm, okm + 32, authCookie); // clientCookie_i
				return true;
			}
		}
		return false;
	}

	size_t LeaseSet2::ExtractClientAuthData (const uint8_t * buf, size_t len, const uint8_t * secret,
		const uint8_t * subcredential, uint8_t * authCookie) const
	{
		static constexpr size_t AUTH_DATA_FAILED = static_cast<size_t> (-1);
		if (len < 1) return AUTH_DATA_FAILED;
		size_t offset = 0;
		uint8_t flag = buf[offset];
		offset++; // flag
		// Bits 3-1 describe the auth scheme only when bit 0 is set.
		uint8_t scheme = flag & 0x0E;
		if (!(flag & 0x01)) // no per-client auth
			return scheme ? AUTH_DATA_FAILED : 0;
		if (scheme == 0x00) // DH
		{
			if (offset + 34 > len) // ephemeralPublicKey(32) + numClients(2)
			{
				LogPrint (eLogError, "LeaseSet2: Buffer too short for DH auth data header");
				return AUTH_DATA_FAILED;
			}
			const uint8_t * ephemeralPublicKey = buf + offset;
			offset += 32; // ephemeralPublicKey
			uint16_t numClients = bufbe16toh (buf + offset);
			offset += 2; // clients
			const uint8_t * authClients = buf + offset;
			offset += (size_t)numClients * 40; // authClients
			if (offset > len)
			{
				LogPrint (eLogError, "LeaseSet2: Too many clients ", numClients, " in DH auth data");
				return AUTH_DATA_FAILED;
			}
			if (!numClients)
			{
				LogPrint (eLogError, "LeaseSet2: DH auth data has no clients");
				return AUTH_DATA_FAILED;
			}
			if (!secret)
			{
				// Java throws DataFormatException rather than continuing with
				// an uninitialised authCookie.
				LogPrint (eLogError, "LeaseSet2: Can't calculate authCookie: csk_i is not provided");
				return AUTH_DATA_FAILED;
			}
			i2p::crypto::X25519Keys ck (secret, nullptr); // derive cpk_i from csk_i
			uint8_t authInput[100];
			if (!ck.Agree (ephemeralPublicKey, authInput)) // sharedSecret is first 32 bytes of authInput
			{
				LogPrint (eLogError, "LeaseSet2: DH auth key agreement failed");
				return AUTH_DATA_FAILED;
			}
			memcpy (authInput + 32, ck.GetPublicKey (), 32); // cpk_i
			memcpy (authInput + 64, subcredential, 36);
			uint8_t okm[64]; // 52 actual data
			i2p::crypto::HKDF (ephemeralPublicKey, authInput, 100, "ELS2_XCA", okm);
			if (!GetAuthCookie (authClients, numClients, okm, authCookie))
			{
				LogPrint (eLogError, "LeaseSet2: Client cookie DH not found");
				return AUTH_DATA_FAILED;
			}
		}
		else if (scheme == 0x02) // PSK
		{
			if (offset + 34 > len) // authSalt(32) + numClients(2)
			{
				LogPrint (eLogError, "LeaseSet2: Buffer too short for PSK auth data header");
				return AUTH_DATA_FAILED;
			}
			const uint8_t * authSalt = buf + offset;
			offset += 32; // authSalt
			uint16_t numClients = bufbe16toh (buf + offset);
			offset += 2; // clients
			const uint8_t * authClients = buf + offset;
			offset += (size_t)numClients * 40; // authClients
			if (offset > len)
			{
				LogPrint (eLogError, "LeaseSet2: Too many clients ", numClients, " in PSK auth data");
				return AUTH_DATA_FAILED;
			}
			if (!numClients)
			{
				LogPrint (eLogError, "LeaseSet2: PSK auth data has no clients");
				return AUTH_DATA_FAILED;
			}
			if (!secret)
			{
				LogPrint (eLogError, "LeaseSet2: Can't calculate authCookie: psk_i is not provided");
				return AUTH_DATA_FAILED;
			}
			uint8_t authInput[68];
			memcpy (authInput, secret, 32);
			memcpy (authInput + 32, subcredential, 36);
			uint8_t okm[64]; // 52 actual data
			i2p::crypto::HKDF (authSalt, authInput, 68, "ELS2PSKA", okm);
			if (!GetAuthCookie (authClients, numClients, okm, authCookie))
			{
				LogPrint (eLogError, "LeaseSet2: Client cookie PSK not found");
				return AUTH_DATA_FAILED;
			}
		}
		else
		{
			LogPrint (eLogError, "LeaseSet2: Unknown client auth type ", (int)flag);
			return AUTH_DATA_FAILED;
		}
		return offset - 1;
	}

	void LeaseSet2::Encrypt (const uint8_t * data, uint8_t * encrypted) const
	{
		if (!encrypted) return;
		auto encryptor = m_Encryptor; // TODO: atomic
		if (encryptor) encryptor->Encrypt (data, encrypted);
	}

	bool LeaseSet2::IsEncryptionKey (CryptoKeyType type, const uint8_t * key) const
	{
		if (!key) return false;
		// Do not assume X25519: ML-KEM and future key types have other
		// lengths. Compare against the stored LS2 wire key length.
		auto keyLen = GetLS2PublicKeyLen (type);
		if (!keyLen) return false;
		// Every advertised section is kept, including a repeated type. Java's
		// List may contain the same type twice and either key can be the one
		// the peer's ratchet selected, so match any section rather than only
		// the last one.
		for (const auto & section : m_EncryptionKeys)
			if (section.type == type && section.key.size () == keyLen && !memcmp (section.key.data (), key, keyLen))
				return true;
		return false;
	}

	bool LeaseSet2::IsStoreKey (const IdentHash & key) const
	{
		if (m_HasStoreHash) return m_StoreHash == key;
		auto identity = GetIdentity ();
		return identity && identity->GetIdentHash () == key;
	}

	uint64_t LeaseSet2::ExtractExpirationTimestamp (const uint8_t * buf, size_t len) const
	{
		uint64_t expiration = 0;
		ExtractPublishedTimestamp (buf, len, expiration);
		return expiration;
	}

	uint64_t LeaseSet2::ExtractPublishedTimestamp (const uint8_t * buf, size_t len, uint64_t & expiration) const
	{
		if (!buf || len < 8) return 0;
		if (m_StoreType == NETDB_STORE_TYPE_ENCRYPTED_LEASESET2)
		{
			// encrypted LS2
			size_t offset = 0;
			uint16_t blindedKeyType = bufbe16toh (buf + offset);
			offset += 2;
			std::unique_ptr<i2p::crypto::Verifier> blindedVerifier (
				i2p::data::IdentityEx::CreateVerifier (blindedKeyType));
			if (!blindedVerifier) return 0;
			auto blindedKeyLen = blindedVerifier->GetPublicKeyLen ();
			if (offset + blindedKeyLen + 6 > len) return 0;
			offset += blindedKeyLen;
			uint32_t timestamp = bufbe32toh (buf + offset);
			offset += 4;
			uint16_t expires = bufbe16toh (buf + offset);
			offset += 2;
			expiration = (static_cast<uint64_t> (timestamp) + expires) * 1000LL;
			return timestamp;
		}
		else
		{
			auto identity = GetIdentity ();
			if (!identity) return 0;
			size_t offset = identity->GetFullLen ();
			if (offset + 6 > len) return 0;
			uint32_t timestamp = bufbe32toh (buf + offset);
			offset += 4;
			uint16_t expires = bufbe16toh (buf + offset);
			offset += 2;
			expiration = (static_cast<uint64_t> (timestamp) + expires) * 1000LL;
			return timestamp;
		}
	}

	LocalLeaseSet::LocalLeaseSet (std::shared_ptr<const IdentityEx> identity, const uint8_t * encryptionPublicKey,
		std::vector<std::shared_ptr<i2p::tunnel::InboundTunnel>> tunnels)
		: m_ExpirationTime (0)
		, m_Identity (identity)
	{
		int num = tunnels.size ();
		if (num > MAX_NUM_LEASES) num = MAX_NUM_LEASES;
		// identity
		auto signingKeyLen = m_Identity->GetSigningPublicKeyLen ();
		m_BufferLen
			= m_Identity->GetFullLen () + 256 + signingKeyLen + 1 + num * LEASE_SIZE + m_Identity->GetSignatureLen ();
		m_Buffer = std::make_unique<uint8_t[]> (m_BufferLen);
		auto offset = m_Identity->ToBuffer (m_Buffer.get (), m_BufferLen);
		memcpy (m_Buffer.get () + offset, encryptionPublicKey, 256);
		offset += 256;
		memset (m_Buffer.get () + offset, 0, signingKeyLen);
		offset += signingKeyLen;
		// num leases
		auto numLeasesPos = offset;
		m_Buffer[offset] = num;
		offset++;
		// leases
		m_Leases = m_Buffer.get () + offset;
		auto currentTime = i2p::util::GetMillisecondsSinceEpoch ();
		int skipped = 0;
		for (int i = 0; i < num; i++)
		{
			uint64_t ts = tunnels[i]->GetCreationTime () + i2p::tunnel::TUNNEL_EXPIRATION_TIMEOUT
				- i2p::tunnel::TUNNEL_EXPIRATION_THRESHOLD; // 1 minute before expiration
			ts *= 1000; // in milliseconds
			if (ts <= currentTime)
			{
				// already expired, skip
				skipped++;
				continue;
			}
			if (ts > m_ExpirationTime) m_ExpirationTime = ts;
			// make sure leaseset is newer than previous, but adding some time to expiration date
			ts += (currentTime - tunnels[i]->GetCreationTime () * 1000LL) * 2
				/ i2p::tunnel::TUNNEL_EXPIRATION_TIMEOUT; // up to 2 secs
			memcpy (m_Buffer.get () + offset, tunnels[i]->GetNextIdentHash (), 32);
			offset += 32; // gateway id
			htobe32buf (m_Buffer.get () + offset, tunnels[i]->GetNextTunnelID ());
			offset += 4; // tunnel id
			htobe64buf (m_Buffer.get () + offset, ts);
			offset += 8; // end date
		}
		if (skipped > 0)
		{
			// adjust num leases
			if (skipped > num) skipped = num;
			num -= skipped;
			m_BufferLen -= skipped * LEASE_SIZE;
			m_Buffer[numLeasesPos] = num;
		}
		// we don't sign it yet. must be signed later on
	}

	LocalLeaseSet::LocalLeaseSet (std::shared_ptr<const IdentityEx> identity, const uint8_t * buf, size_t len)
		: m_ExpirationTime (0)
		, m_Identity (identity)
	{
		if (buf)
		{
			m_BufferLen = len;
			m_Buffer = std::make_unique<uint8_t[]> (m_BufferLen);
			if (len) memcpy (m_Buffer.get (), buf, len);
		}
		else
		{
			m_Buffer.reset ();
			m_BufferLen = 0;
		}
	}

	bool LocalLeaseSet::IsExpired () const
	{
		auto ts = i2p::util::GetMillisecondsSinceEpoch ();
		return ts > m_ExpirationTime;
	}

	bool LeaseSetBufferValidate (const uint8_t * ptr, size_t sz, uint64_t & expires)
	{
		expires = 0;
		if (!ptr || !sz) return false;
		IdentityEx ident (ptr, sz);
		size_t size = ident.GetFullLen ();
		if (!size || size > sz)
		{
			LogPrint (eLogError, "LeaseSet: Identity length ", size, " exceeds buffer size ", sz);
			return false;
		}
		// encryption key
		size += 256;
		// signing key (unused)
		size += ident.GetSigningPublicKeyLen ();
		if (size + 1 > sz)
		{
			LogPrint (eLogError, "LeaseSet: ", size, " exceeds buffer size ", sz);
			return false;
		}
		uint8_t numLeases = ptr[size];
		++size;
		if (!numLeases || numLeases > MAX_NUM_LEASES)
		{
			LogPrint (eLogError, "LeaseSet: Incorrect number of leases", (int)numLeases);
			return false;
		}
		if (size + numLeases * LEASE_SIZE > sz)
		{
			LogPrint (eLogError, "LeaseSet: ", size, " exceeds buffer size ", sz);
			return false;
		}
		const uint8_t * leases = ptr + size;
		uint64_t earliest = UINT64_MAX;
		/** find the earliest and latest lease expiration timestamps */
		for (int i = 0; i < numLeases; i++)
		{
			leases += 36; // gateway + tunnel ID
			uint64_t endDate = bufbe64toh (leases);
			leases += 8; // end date
			if (endDate > expires) expires = endDate;
			if (endDate < earliest) earliest = endDate;
		}
		auto now = i2p::util::GetMillisecondsSinceEpoch ();
		// Same bounds as Java's validate(): the latest lease must not be
		// years into the future and the earliest must not be more than
		// 10 minutes old.
		if (expires > now + LEASESET_MAX_LEASE_FUTURE + LEASESET2_CLOCK_FUDGE)
		{
			LogPrint (eLogWarning, "LeaseSet: Lease end date is too far in the future ", expires);
			return false;
		}
		if (earliest + LEASESET_MAX_LEASE_AGE <= now)
		{
			LogPrint (eLogWarning, "LeaseSet: Earliest lease is too old ", earliest);
			return false;
		}
		if ((size_t)(leases - ptr) + ident.GetSignatureLen () > sz)
		{
			LogPrint (eLogError, "LeaseSet: Signature exceeds buffer size ", sz);
			return false;
		}
		return ident.Verify (ptr, leases - ptr, leases);
	}

	LocalLeaseSet2::LocalLeaseSet2 (uint8_t storeType, const i2p::data::PrivateKeys & keys,
		const EncryptionKeys & encryptionKeys, const std::vector<std::shared_ptr<i2p::tunnel::InboundTunnel>> & tunnels,
		bool isPublic, uint64_t publishedTimestamp, bool isPublishedEncrypted)
		: LocalLeaseSet (keys.GetPublic (), nullptr, 0)
	{
		auto identity = keys.GetPublic ();
		// assume standard LS2
		int num = tunnels.size ();
		if (num > MAX_NUM_LEASES) num = MAX_NUM_LEASES;
		// Build the key section list once. GetLS2PublicKeyLen() is the wire
		// length (EC_P256 is 64 bytes, legacy EC_P256 key files are 256);
		// skipping a key whose stored public material is shorter prevents
		// publishing a section that every peer must reject as malformed.
		struct KeySection
		{
			const i2p::crypto::LocalEncryptionKey * key;
			size_t len;
		};
		std::vector<KeySection> keySections;
		keySections.reserve (std::min<size_t> (encryptionKeys.size (), MAX_NUM_ENCRYPTION_KEYS));
		for (const auto & it : encryptionKeys)
		{
			if (keySections.size () >= MAX_NUM_ENCRYPTION_KEYS)
			{
				LogPrint (eLogWarning, "LeaseSet2: Truncating ", encryptionKeys.size (), " encryption keys to ",
					MAX_NUM_ENCRYPTION_KEYS);
				break;
			}
			auto keyLen = GetLS2PublicKeyLen (it->keyType);
			if (!keyLen || it->pub.size () < keyLen)
			{
				LogPrint (eLogWarning, "LeaseSet2: Skipping unusable local encryption key type ", (int)it->keyType,
					" with ", it->pub.size (), " public bytes, expected ", keyLen);
				continue;
			}
			keySections.push_back ({ it.get (), keyLen });
		}
		if (keySections.empty ())
		{
			// LeaseSet2.java rejects numKeys == 0.
			LogPrint (eLogError, "LeaseSet2: No usable local encryption keys");
			return;
		}
		size_t keySectionsLen = 0;
		for (const auto & section : keySections) keySectionsLen += 2 /*key type*/ + 2 /*key len*/ + section.len /*key*/;
		auto numKeys = keySections.size ();
		m_BufferLen = identity->GetFullLen ()
			+ 4 /*published*/ + 2 /*expires*/ + 2 /*flag*/ + 2 /*properties len*/ + 1 /*num keys*/ + keySectionsLen
			+ 1 /*num leases*/ + num * LEASE2_SIZE + keys.GetSignatureLen ();
		uint16_t flags = 0;
		if (keys.IsOfflineSignature ())
		{
			flags |= LEASESET2_FLAG_OFFLINE_KEYS;
			m_BufferLen += keys.GetOfflineSignature ().size ();
		}
		if (isPublishedEncrypted)
		{
			// The inner LeaseSet of an encrypted publication is never served
			// directly. Java writes UNPUBLISHED | PUBLISHED_ENCRYPTED (0x06),
			// which keeps a floodfill from serving the extracted destination.
			flags |= LEASESET2_FLAG_PUBLISHED_ENCRYPTED | LEASESET2_FLAG_UNPUBLISHED_LEASESET;
		}
		else if (!isPublic)
			flags |= LEASESET2_FLAG_UNPUBLISHED_LEASESET;

		m_Buffer = std::make_unique<uint8_t[]> (m_BufferLen + 1);
		m_Buffer[0] = storeType;
		// LS2 header
		auto offset = identity->ToBuffer (m_Buffer.get () + 1, m_BufferLen) + 1;
		htobe32buf (m_Buffer.get () + offset, publishedTimestamp);
		offset += 4; // published timestamp (seconds)
		uint8_t * expiresBuf = m_Buffer.get () + offset;
		offset += 2; // expires, fill later
		htobe16buf (m_Buffer.get () + offset, flags);
		offset += 2; // flags
		if (keys.IsOfflineSignature ())
		{
			// offline signature
			const auto & offlineSignature = keys.GetOfflineSignature ();
			memcpy (m_Buffer.get () + offset, offlineSignature.data (), offlineSignature.size ());
			offset += offlineSignature.size ();
		}
		htobe16buf (m_Buffer.get () + offset, 0);
		offset += 2; // properties len
		// keys
		m_Buffer[offset] = (uint8_t)numKeys;
		offset++; // 1 key
		for (const auto & section : keySections)
		{
			htobe16buf (m_Buffer.get () + offset, section.key->keyType);
			offset += 2; // key type
			htobe16buf (m_Buffer.get () + offset, section.len);
			offset += 2; // key len
			memcpy (m_Buffer.get () + offset, section.key->pub.data (), section.len);
			offset += section.len; // key
		}
		// leases
		uint32_t expirationTime = 0; // in seconds
		int skipped = 0;
		auto numLeasesPos = offset;
		m_Buffer[offset] = num;
		offset++; // num leases
		for (int i = 0; i < num; i++)
		{
			auto ts = tunnels[i]->GetCreationTime () + i2p::tunnel::TUNNEL_EXPIRATION_TIMEOUT
				- i2p::tunnel::TUNNEL_EXPIRATION_THRESHOLD; // in seconds, 1 minute before expiration
			if (ts <= publishedTimestamp)
			{
				// already expired, skip
				skipped++;
				continue;
			}
			if (ts > expirationTime) expirationTime = ts;
			memcpy (m_Buffer.get () + offset, tunnels[i]->GetNextIdentHash (), 32);
			offset += 32; // gateway id
			htobe32buf (m_Buffer.get () + offset, tunnels[i]->GetNextTunnelID ());
			offset += 4; // tunnel id
			htobe32buf (m_Buffer.get () + offset, ts);
			offset += 4; // end date
		}
		if (skipped > 0)
		{
			// adjust num leases
			if (skipped > num) skipped = num;
			num -= skipped;
			m_BufferLen -= skipped * LEASE2_SIZE;
			m_Buffer[numLeasesPos] = num;
		}
		// update expiration
		if (expirationTime)
		{
			SetExpirationTime (expirationTime * 1000LL);
			auto expires = expirationTime > publishedTimestamp ? expirationTime - publishedTimestamp : 0;
			if (expires > 0xFFFF) expires = 0xFFFF; // 16-bit wire field, 18.2 hours
			htobe16buf (expiresBuf, expires);
		}
		else
		{
			// no tunnels or withdraw
			SetExpirationTime (publishedTimestamp * 1000LL);
			memset (expiresBuf, 0, 2); // expires immediately
		}
		// sign
		keys.Sign (m_Buffer.get (), offset, m_Buffer.get () + offset); // LS + leading store type
	}

	LocalLeaseSet2::LocalLeaseSet2 (
		uint8_t storeType, std::shared_ptr<const IdentityEx> identity, const uint8_t * buf, size_t len)
		: LocalLeaseSet (identity, nullptr, 0)
	{
		if (!buf) len = 0;
		m_BufferLen = len;
		m_Buffer = std::make_unique<uint8_t[]> (m_BufferLen + 1);
		if (len) memcpy (m_Buffer.get () + 1, buf, len);
		m_Buffer[0] = storeType;
	}

	LocalEncryptedLeaseSet2::LocalEncryptedLeaseSet2 (std::shared_ptr<const LocalLeaseSet2> ls,
		const i2p::data::PrivateKeys & keys, int authType, std::shared_ptr<std::vector<AuthPublicKey>> authKeys)
		: LocalLeaseSet2 (ls ? ls->GetIdentity () : nullptr)
		, m_InnerLeaseSet (ls)
	{
		if (!ls)
		{
			LogPrint (eLogError, "LeaseSet2: Can't encrypt a null inner LeaseSet");
			return;
		}
		if (!ls->GetBuffer () || !ls->GetBufferLen ())
		{
			LogPrint (eLogError, "LeaseSet2: Can't encrypt an uninitialised inner LeaseSet");
			return;
		}
		// Java rejects per-client auth without client keys. Silently falling
		// back to AUTH_NONE publishes an unauthenticated LeaseSet even though
		// the operator requested restricted access.
		if (authType != ENCRYPTED_LEASESET_AUTH_TYPE_NONE)
		{
			if (authType != ENCRYPTED_LEASESET_AUTH_TYPE_DH && authType != ENCRYPTED_LEASESET_AUTH_TYPE_PSK)
			{
				LogPrint (eLogError, "LeaseSet2: Unknown client auth type ", authType);
				return;
			}
			if (!authKeys || authKeys->empty ())
			{
				LogPrint (eLogError, "LeaseSet2: Client auth type ", authType, " requires client keys");
				return;
			}
			if (authKeys->size () > 0xFFFF)
			{
				LogPrint (eLogError, "LeaseSet2: Too many client auth keys ", authKeys->size ());
				return;
			}
		}
		size_t lenInnerPlaintext = ls->GetBufferLen () + 1, lenOuterPlaintext = lenInnerPlaintext + 32 + 1;
		uint8_t layer1Flags = 0;
		if (authKeys && !authKeys->empty ())
		{
			if (authType == ENCRYPTED_LEASESET_AUTH_TYPE_DH)
				layer1Flags |= 0x01; // DH, authentication scheme 0, auth bit 1
			else if (authType == ENCRYPTED_LEASESET_AUTH_TYPE_PSK)
				layer1Flags |= 0x03; // PSK, authentication scheme 1, auth bit 1
			if (layer1Flags) lenOuterPlaintext += 32 + 2 + authKeys->size () * 40; // auth data len
		}
		size_t lenOuterCiphertext = lenOuterPlaintext + 32;
		// lenOuterCiphertext is a 16-bit wire field and Java caps the
		// encrypted payload at 4096 bytes.
		if (lenOuterCiphertext > MAX_LS_BUFFER_SIZE)
		{
			LogPrint (eLogError, "LeaseSet2: Encrypted LeaseSet is too large ", lenOuterCiphertext);
			return;
		}

		BlindedPublicKey blindedKey (ls->GetIdentity ());
		auto timestamp = i2p::util::GetSecondsSinceEpoch ();
		char date[9];
		i2p::util::GetDateString (timestamp, date);
		OfflinePrivateKeys offlineKeys (keys, date);
		uint8_t blindedPriv[i2p::crypto::EDDSA25519_PRIVATE_KEY_LENGTH],
			blindedPub[i2p::crypto::EDDSA25519_PUBLIC_KEY_LENGTH]; // 32 and 32 max
		std::unique_ptr<i2p::crypto::Signer> blindedSigner;
		size_t publicKeyLen = 0;
		if (offlineKeys.IsOfflineSignature ())
			publicKeyLen
				= blindedKey.GetBlindedKey (date, blindedPub); // the transient is authorized by it, not blinded from it
		else if (!keys.IsOfflineSignature ())
		{
			publicKeyLen = blindedKey.BlindPrivateKey (keys.GetSigningPrivateKey (), date, blindedPriv, blindedPub);
			blindedSigner.reset (i2p::data::PrivateKeys::CreateSigner (blindedKey.GetBlindedSigType (), blindedPriv));
			memset (blindedPriv, 0, sizeof (blindedPriv)); // it would give the destination's key away
		}
		if (!publicKeyLen || (!blindedSigner && !offlineKeys.IsOfflineSignature ()))
		{
			LogPrint (eLogError, "LeaseSet2: Can't sign an encrypted LeaseSet for ", date);
			return;
		}
		const auto & offlineSignature = offlineKeys.GetOfflineSignature ();
		m_BufferLen = 2 /*blinded sig type*/ + publicKeyLen
			+ 4 /*published*/ + 2 /*expires*/ + 2 /*flags*/ + offlineSignature.size ()
			+ 2 /*lenOuterCiphertext*/ + lenOuterCiphertext
			+ (blindedSigner ? 64 /*signature*/ : offlineKeys.GetSignatureLen ());
		if (m_BufferLen + 1 > MAX_LS_BUFFER_SIZE)
		{
			LogPrint (eLogError, "LeaseSet2: Encrypted LeaseSet buffer is too large ", m_BufferLen + 1);
			m_BufferLen = 0;
			return;
		}
		m_Buffer = std::make_unique<uint8_t[]> (m_BufferLen + 1);
		m_Buffer[0] = NETDB_STORE_TYPE_ENCRYPTED_LEASESET2;
		auto offset = 1;
		htobe16buf (m_Buffer.get () + offset, blindedKey.GetBlindedSigType ());
		offset += 2; // Blinded Public Key Sig Type
		memcpy (m_Buffer.get () + offset, blindedPub, publicKeyLen);
		offset += publicKeyLen; // Blinded Public Key
		htobe32buf (m_Buffer.get () + offset, timestamp);
		offset += 4; // published timestamp (seconds)
		auto nextMidnight = (timestamp / 86400LL + 1) * 86400LL; // 86400 = 24*3600 seconds
		auto expirationTime = ls->GetExpirationTime () / 1000LL;
		if (expirationTime > nextMidnight) expirationTime = nextMidnight;
		SetExpirationTime (expirationTime * 1000LL);
		htobe16buf (m_Buffer.get () + offset, expirationTime > timestamp ? expirationTime - timestamp : 0);
		offset += 2; // expires
		uint16_t flags = offlineKeys.IsOfflineSignature () ? LEASESET2_FLAG_OFFLINE_KEYS : 0;
		htobe16buf (m_Buffer.get () + offset, flags);
		offset += 2; // flags
		if (flags)
		{
			memcpy (m_Buffer.get () + offset, offlineSignature.data (), offlineSignature.size ());
			offset += offlineSignature.size ();
		}
		htobe16buf (m_Buffer.get () + offset, lenOuterCiphertext);
		offset += 2; // lenOuterCiphertext
		// outerChipherText
		// Layer 1
		uint8_t subcredential[36];
		blindedKey.GetSubcredential (blindedPub, 32, subcredential);
		htobe32buf (subcredential + 32, timestamp); // outerInput = subcredential || publishedTimestamp
		// keys = HKDF(outerSalt, outerInput, "ELS2_L1K", 44)
		uint8_t keys1[64]; // 44 bytes actual data
		if (RAND_bytes (m_Buffer.get () + offset, 32) != 1) // outerSalt = CSRNG(32)
		{
			LogPrint (eLogError, "LeaseSet2: Can't generate the outer salt");
			m_BufferLen = 0;
			m_Buffer.reset ();
			return;
		}
		i2p::crypto::HKDF (m_Buffer.get () + offset, subcredential, 36, "ELS2_L1K", keys1);
		offset += 32; // outerSalt
		uint8_t * outerPlainText = m_Buffer.get () + offset;
		m_Buffer[offset] = layer1Flags;
		offset++; // layer 1 flags
		// auth data
		uint8_t innerInput[68]; // authCookie || subcredential || publishedTimestamp
		if (layer1Flags)
		{
			if (RAND_bytes (innerInput, 32) != 1) // authCookie
			{
				LogPrint (eLogError, "LeaseSet2: Can't generate the auth cookie");
				m_BufferLen = 0;
				m_Buffer.reset ();
				return;
			}
			if (!CreateClientAuthData (subcredential, authType, authKeys, innerInput, m_Buffer.get () + offset))
			{
				LogPrint (eLogError, "LeaseSet2: Can't create the client auth data");
				m_BufferLen = 0;
				m_Buffer.reset ();
				return;
			}
			offset += 32 + 2 + authKeys->size () * 40; // auth clients
		}
		// Layer 2
		// keys = HKDF(outerSalt, outerInput, "ELS2_L2K", 44)
		uint8_t keys2[64]; // 44 bytes actual data
		if (RAND_bytes (m_Buffer.get () + offset, 32) != 1) // innerSalt = CSRNG(32)
		{
			LogPrint (eLogError, "LeaseSet2: Can't generate the inner salt");
			m_BufferLen = 0;
			m_Buffer.reset ();
			return;
		}
		if (layer1Flags)
		{
			memcpy (innerInput + 32, subcredential, 36); // + subcredential || publishedTimestamp
			i2p::crypto::HKDF (m_Buffer.get () + offset, innerInput, 68, "ELS2_L2K", keys2);
		}
		else
			i2p::crypto::HKDF (m_Buffer.get () + offset, subcredential, 36, "ELS2_L2K", keys2); // no authCookie
		offset += 32; // innerSalt
		m_Buffer[offset] = ls->GetStoreType ();
		memcpy (m_Buffer.get () + offset + 1, ls->GetBuffer (), ls->GetBufferLen ());
		i2p::crypto::ChaCha20 (m_Buffer.get () + offset, lenInnerPlaintext, keys2, keys2 + 32,
			m_Buffer.get () + offset); // encrypt Layer 2
		offset += lenInnerPlaintext;
		i2p::crypto::ChaCha20 (outerPlainText, lenOuterPlaintext, keys1, keys1 + 32, outerPlainText); // encrypt Layer 1
		// signature
		if (blindedSigner)
			blindedSigner->Sign (m_Buffer.get (), offset, m_Buffer.get () + offset);
		else
			offlineKeys.Sign (m_Buffer.get (), offset, m_Buffer.get () + offset);
		// store hash
		m_StoreHash = blindedKey.GetStoreHash (date);
	}

	LocalEncryptedLeaseSet2::LocalEncryptedLeaseSet2 (
		std::shared_ptr<const IdentityEx> identity, const uint8_t * buf, size_t len)
		: LocalLeaseSet2 (NETDB_STORE_TYPE_ENCRYPTED_LEASESET2, identity, buf, len)
	{
		// fill inner LeaseSet2
		auto blindedKey = std::make_shared<BlindedPublicKey> (identity);
		i2p::data::LeaseSet2 ls (buf, len, blindedKey); // inner layer
		if (ls.IsValid ())
		{
			m_InnerLeaseSet
				= std::make_shared<LocalLeaseSet2> (ls.GetStoreType (), identity, ls.GetBuffer (), ls.GetBufferLen ());
			m_StoreHash = blindedKey->GetStoreHash ();
		}
		else
			LogPrint (eLogError, "LeaseSet2: Couldn't extract inner layer");
	}

	bool LocalEncryptedLeaseSet2::CreateClientAuthData (const uint8_t * subcredential, int authType,
		std::shared_ptr<std::vector<AuthPublicKey>> authKeys, const uint8_t * authCookie, uint8_t * authData) const
	{
		if (!subcredential || !authCookie || !authData || !authKeys || authKeys->empty ()) return false;
		if (authType == ENCRYPTED_LEASESET_AUTH_TYPE_DH)
		{
			i2p::crypto::X25519Keys ek;
			ek.GenerateKeys (); // esk and epk
			if (i2p::data::Tag<32> (ek.GetPublicKey ()).IsZero ()) return false;
			memcpy (authData, ek.GetPublicKey (), 32);
			authData += 32; // epk
			htobe16buf (authData, authKeys->size ());
			authData += 2; // num clients
			uint8_t authInput[100]; // sharedSecret || cpk_i || subcredential || publishedTimestamp
			memcpy (authInput + 64, subcredential, 36);
			for (auto & it : *authKeys)
			{
				if (!ek.Agree (it, authInput)) return false; // sharedSecret = DH(esk, cpk_i)
				memcpy (authInput + 32, it, 32);
				uint8_t okm[64]; // 52 actual data
				i2p::crypto::HKDF (ek.GetPublicKey (), authInput, 100, "ELS2_XCA", okm);
				memcpy (authData, okm + 44, 8);
				authData += 8; // clientID_i
				i2p::crypto::ChaCha20 (authCookie, 32, okm, okm + 32, authData);
				authData += 32; // clientCookie_i
			}
		}
		else // assume PSK
		{
			uint8_t authSalt[32];
			if (RAND_bytes (authSalt, sizeof (authSalt)) != 1) return false;
			memcpy (authData, authSalt, 32);
			authData += 32; // authSalt
			htobe16buf (authData, authKeys->size ());
			authData += 2; // num clients
			uint8_t authInput[68]; // authInput = psk_i || subcredential || publishedTimestamp
			memcpy (authInput + 32, subcredential, 36);
			for (auto & it : *authKeys)
			{
				memcpy (authInput, it, 32);
				uint8_t okm[64]; // 52 actual data
				i2p::crypto::HKDF (authSalt, authInput, 68, "ELS2PSKA", okm);
				memcpy (authData, okm + 44, 8);
				authData += 8; // clientID_i
				i2p::crypto::ChaCha20 (authCookie, 32, okm, okm + 32, authData);
				authData += 32; // clientCookie_i
			}
		}
		return true;
	}
}
}

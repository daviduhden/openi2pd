/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: LeaseSet.
 *
 * LeaseSet v1 and LeaseSet2 parsing, signature verification and expiry policy for standard,
 * meta and encrypted store types, including offline signatures and per-type encryption-key
 * selection. Local builders assemble the inbound-tunnel leases that the netDb publishes, with
 * Java-compatible lease-age and clock-fudge bounds.
 *
 */

#ifndef LEASE_SET_H__
#define LEASE_SET_H__

#include <inttypes.h>
#include <string.h>
#include <vector>
#include <list>
#include <set>
#include <map>
#include <memory>
#include "Identity.h"
#include "Timestamp.h"
#include "I2PEndian.h"
#include "Blinding.h"
#include "Crypto.h"
#include "CryptoKey.h"
#include "util.h"

namespace i2p
{

namespace tunnel
{
	class InboundTunnel;
}

namespace data
{
	const int LEASE_ENDDATE_THRESHOLD = 51000; // in milliseconds
	struct Lease
	{
		IdentHash tunnelGateway;
		uint32_t tunnelID;
		uint64_t endDate; // 0 means invalid
		bool isUpdated; // transient
		/* return true if this lease expires within t millisecond + fudge factor */
		bool ExpiresWithin (const uint64_t t, const uint64_t fudge = 1000) const
		{
			auto expire = i2p::util::GetMillisecondsSinceEpoch ();
			if (fudge) expire += i2p::crypto::RandomBelow (static_cast<uint32_t> (fudge));
			if (endDate < expire) return true;
			return (endDate - expire) < t;
		}
	};

	struct LeaseCmp
	{
		bool operator() (std::shared_ptr<const Lease> l1, std::shared_ptr<const Lease> l2) const
		{
			if (l1->tunnelID != l2->tunnelID)
				return l1->tunnelID < l2->tunnelID;
			else
				return l1->tunnelGateway < l2->tunnelGateway;
		};
	};

	typedef std::function<bool (const Lease & l)> LeaseInspectFunc;

	const size_t MAX_LS_BUFFER_SIZE = 4096;
	const size_t LEASE_SIZE = 44; // 32 + 4 + 8
	const size_t LEASE2_SIZE = 40; // 32 + 4 + 4
	const size_t META_LEASE_SIZE = 40; // hash (32) + flags (2) + type (1) + cost (1) + expires (4)
	const uint8_t MAX_NUM_LEASES = 16;
	const uint8_t MAX_NUM_ENCRYPTION_KEYS = 8; // LeaseSet2.MAX_KEYS
	const uint64_t LEASESET_EXPIRATION_TIME_THRESHOLD = 12 * 60 * 1000; // in milliseconds
	const uint64_t LEASESET2_CLOCK_FUDGE = 60 * 1000; // Router.CLOCK_FUDGE_FACTOR
	// KademliaNetworkDatabaseFacade.validate() rejects an entry whose earliest
	// lease is more than 10 minutes old. This applies to LS1 and LS2.
	const uint64_t LEASESET_MAX_LEASE_AGE = 10 * 60 * 1000; // in milliseconds
	// KademliaNetworkDatabaseFacade.MAX_LEASE_FUTURE. This applies to LS1
	// and LS2 lease dates. Clock fudge is added separately.
	const uint64_t LEASESET_MAX_LEASE_FUTURE = 15 * 60 * 1000; // in milliseconds
	// KademliaNetworkDatabaseFacade.MAX_META_LEASE_FUTURE. Standard LS2
	// expiration is capped by the 16-bit expires field (65535 seconds).
	const uint64_t LEASESET2_META_MAX_FUTURE = 65535 * 1000LL; // in milliseconds
	const uint64_t LEASESET2_META_MAX_PUBLISHED_AGE = 10 * 60 * 1000; // Java earliest-lease bound

	const uint8_t NETDB_STORE_TYPE_LEASESET = 1;
	class LeaseSet : public RoutingDestination
	{
	public:
		LeaseSet (const uint8_t * buf, size_t len, bool storeLeases = true);
		virtual ~LeaseSet () = default;
		virtual void Update (
			const uint8_t * buf, size_t len, std::shared_ptr<LocalDestination> dest, bool verifySignature);
		virtual bool IsNewer (const uint8_t * buf, size_t len) const;
		void PopulateLeases (); // from buffer

		const uint8_t * GetBuffer () const { return m_Buffer.get (); };
		size_t GetBufferLen () const { return m_BufferLen; };
		bool IsValid () const { return m_IsValid; };
		bool IsIncompatibleCrypto () const { return m_IsIncompatibleCrypto; };
		void SetIsIncompatibleCrypto (bool isIncompatibleCrypto) { m_IsIncompatibleCrypto = isIncompatibleCrypto; };
		const std::vector<std::shared_ptr<const Lease>> GetNonExpiredLeases (bool withThreshold = true) const;
		const std::vector<std::shared_ptr<const Lease>> GetNonExpiredLeasesExcluding (
			LeaseInspectFunc exclude, bool withThreshold = true) const;
		bool HasExpiredLeases () const;
		bool IsExpired () const;
		bool IsEmpty () const { return m_Leases.empty (); };
		uint64_t GetExpirationTime () const { return m_ExpirationTime; };
		// Earliest lease end date in milliseconds. 0 means "no leases",
		// matching Java's -1 sentinel for cross-type isNewer() comparisons
		// without using an unsigned wraparound.
		uint64_t GetEarliestLeaseDate () const { return m_EarliestLeaseDate; };
		bool ExpiresSoon (const uint64_t dlt = 1000 * 5, const uint64_t fudge = 0) const;
		bool operator== (const LeaseSet & other) const
		{
			if (m_BufferLen != other.m_BufferLen) return false;
			return m_BufferLen == 0 || !memcmp (m_Buffer.get (), other.m_Buffer.get (), m_BufferLen);
		};
		virtual uint8_t GetStoreType () const { return NETDB_STORE_TYPE_LEASESET; };
		virtual uint32_t GetPublishedTimestamp () const { return 0; }; // should be set for LeaseSet2 only
		virtual bool IsEncryptionKey (CryptoKeyType /* type */, const uint8_t * /* key */) const { return false; };
		virtual std::shared_ptr<const i2p::crypto::Verifier> GetTransientVerifier () const { return nullptr; };
		virtual bool IsPublishedEncrypted () const { return false; };

		// implements RoutingDestination
		std::shared_ptr<const IdentityEx> GetIdentity () const { return m_Identity; };
		void Encrypt (const uint8_t * data, uint8_t * encrypted) const;
		bool IsDestination () const { return true; };

		// used by administrative views
		void ExpireLease () { m_ExpirationTime = i2p::util::GetSecondsSinceEpoch (); };

	protected:
		void UpdateLeasesBegin ();
		void UpdateLeasesEnd ();
		void UpdateLease (const Lease & lease, uint64_t ts);

		// called from LeaseSet2
		LeaseSet (bool storeLeases);
		void SetBuffer (const uint8_t * buf, size_t len);
		void SetBufferLen (size_t len);
		void SetIdentity (std::shared_ptr<const IdentityEx> identity) { m_Identity = identity; };
		void SetExpirationTime (uint64_t t) { m_ExpirationTime = t; };
		void SetEarliestLeaseDate (uint64_t t) { m_EarliestLeaseDate = t; };
		void SetIsValid (bool isValid) { m_IsValid = isValid; };
		bool IsStoreLeases () const { return m_StoreLeases; };

	private:
		void ReadFromBuffer (bool readIdentity = true, bool verifySignature = true);
		virtual uint64_t ExtractExpirationTimestamp (
			const uint8_t * buf, size_t len) const; // LS1: earliest lease end date

	private:
		bool m_IsValid, m_IsIncompatibleCrypto, m_StoreLeases; // we don't need to store leases for floodfill
		std::set<std::shared_ptr<Lease>, LeaseCmp> m_Leases;
		uint64_t m_ExpirationTime; // in milliseconds
		uint64_t m_EarliestLeaseDate = 0; // in milliseconds; 0 = no leases
		std::shared_ptr<const IdentityEx> m_Identity;
		std::unique_ptr<uint8_t[]> m_EncryptionKey;
		std::unique_ptr<uint8_t[]> m_Buffer;
		size_t m_BufferLen;
	};

	/**
	 * validate lease set buffer signature and extract expiration timestamp
	 * @returns true if the leaseset is well formed and signature is valid
	 */
	bool LeaseSetBufferValidate (const uint8_t * ptr, size_t sz, uint64_t & expires);

	const uint8_t NETDB_STORE_TYPE_STANDARD_LEASESET2 = 3;
	const uint8_t NETDB_STORE_TYPE_ENCRYPTED_LEASESET2 = 5;
	const uint8_t NETDB_STORE_TYPE_META_LEASESET2 = 7;

	const uint16_t LEASESET2_FLAG_OFFLINE_KEYS = 0x0001;
	const uint16_t LEASESET2_FLAG_UNPUBLISHED_LEASESET = 0x0002;
	const uint16_t LEASESET2_FLAG_PUBLISHED_ENCRYPTED = 0x0004;

	class LeaseSet2 : public LeaseSet
	{
	public:
		LeaseSet2 (uint8_t storeType)
			: LeaseSet (true)
			, m_StoreType (storeType)
			, m_EncryptionType (0)
			, m_PreferredEncryptionType (0) { }; // for administrative views only
		LeaseSet2 (uint8_t storeType, const uint8_t * buf, size_t len, bool storeLeases = true,
			std::shared_ptr<LocalDestination> dest = nullptr,
			CryptoKeyType preferredCrypto = CRYPTO_KEY_TYPE_ECIES_X25519_AEAD);
		LeaseSet2 (const uint8_t * buf, size_t len, std::shared_ptr<const BlindedPublicKey> key,
			std::shared_ptr<LocalDestination> dest = nullptr, const uint8_t * secret = nullptr,
			CryptoKeyType preferredCrypto
			= CRYPTO_KEY_TYPE_ECIES_X25519_AEAD); // store type 5, called from local netdb only
		uint8_t GetStoreType () const override { return m_StoreType; };
		uint32_t GetPublishedTimestamp () const override { return m_PublishedTimestamp; };
		bool IsPublic () const { return m_IsPublic; };
		// LeaseSet2 options mapping (proposal 167 HostReply types 2-4).
		const i2p::util::Mapping & GetProperties () const { return m_Properties; };
		bool IsPublishedEncrypted () const override { return m_IsPublishedEncrypted; };
		bool IsEncryptionKey (CryptoKeyType type, const uint8_t * key) const override;
		bool IsStoreKey (const IdentHash & key) const;
		std::shared_ptr<const i2p::crypto::Verifier> GetTransientVerifier () const override
		{
			return m_TransientVerifier;
		};
		void Update (
			const uint8_t * buf, size_t len, std::shared_ptr<LocalDestination> dest, bool verifySignature) override;
		bool IsNewer (const uint8_t * buf, size_t len) const override;

		// implements RoutingDestination
		void Encrypt (const uint8_t * data, uint8_t * encrypted) const override;
		CryptoKeyType GetEncryptionType () const override { return m_EncryptionType; };

	private:
		void ReadFromBuffer (const uint8_t * buf, size_t len, std::shared_ptr<LocalDestination> dest,
			bool readIdentity = true, bool verifySignature = true);
		void ReadFromBufferEncrypted (const uint8_t * buf, size_t len, std::shared_ptr<const BlindedPublicKey> key,
			std::shared_ptr<LocalDestination> dest, const uint8_t * secret);
		size_t ReadStandardLS2TypeSpecificPart (
			const uint8_t * buf, size_t len, std::shared_ptr<LocalDestination> dest);
		size_t ReadMetaLS2TypeSpecificPart (const uint8_t * buf, size_t len);

		template <typename Verifier>
		bool VerifySignature (Verifier & verifier, const uint8_t * buf, size_t len, size_t signatureOffset);

		uint64_t ExtractExpirationTimestamp (const uint8_t * buf, size_t len) const override;
		uint64_t ExtractPublishedTimestamp (const uint8_t * buf, size_t len, uint64_t & expiration) const;
		size_t ExtractClientAuthData (const uint8_t * buf, size_t len, const uint8_t * secret,
			const uint8_t * subcredential, uint8_t * authCookie)
			const; // subcredential is subcredential + timestamp, return length of autData without flag

	private:
		// One wire key section, kept in the order the publisher advertised it.
		// A std::map keyed by type would silently drop an earlier section when
		// the same type is advertised twice; Java keeps every section in a List
		// and only getEncryptionKey()/IsEncryptionKey() collapse them by type.
		struct EncryptionKeySection
		{
			CryptoKeyType type;
			std::vector<uint8_t> key;
		};

		uint8_t m_StoreType;
		uint32_t m_PublishedTimestamp = 0; // seconds
		bool m_IsPublic = true, m_IsPublishedEncrypted = false;
		std::shared_ptr<i2p::crypto::Verifier> m_TransientVerifier;
		CryptoKeyType m_EncryptionType;
		const CryptoKeyType m_PreferredEncryptionType;
		std::shared_ptr<i2p::crypto::CryptoKeyEncryptor> m_Encryptor; // for standardLS2
		std::vector<EncryptionKeySection> m_EncryptionKeys; // all advertised key sections, wire order
		IdentHash m_StoreHash;
		bool m_HasStoreHash = false;
		i2p::util::Mapping m_Properties;
	};

	// also called from Streaming.cpp
	template <typename Verifier>
	std::shared_ptr<i2p::crypto::Verifier> ProcessOfflineSignature (
		const Verifier & verifier, const uint8_t * buf, size_t len, size_t & offset)
	{
		// The offline section may end exactly at the end of the buffer (the
		// LeaseSet2 signature is the last field). Use strict greater-than
		// checks; ">=" rejected every offline-signed LeaseSet2.
		if (offset + 6 > len) return nullptr;
		const uint8_t * signedData = buf + offset;
		uint32_t expiresTimestamp = bufbe32toh (buf + offset);
		offset += 4; // expires timestamp
		if (expiresTimestamp < i2p::util::GetSecondsSinceEpoch ()) return nullptr;
		uint16_t keyType = bufbe16toh (buf + offset);
		offset += 2;
		std::shared_ptr<i2p::crypto::Verifier> transientVerifier (i2p::data::IdentityEx::CreateVerifier (keyType));
		if (!transientVerifier) return nullptr;
		auto keyLen = transientVerifier->GetPublicKeyLen ();
		if (offset + keyLen > len) return nullptr;
		transientVerifier->SetPublicKey (buf + offset);
		offset += keyLen;
		auto signatureLen = verifier->GetSignatureLen ();
		if (offset + signatureLen > len) return nullptr;
		if (!verifier->Verify (signedData, keyLen + 6, buf + offset)) return nullptr;
		offset += signatureLen;
		return transientVerifier;
	}

	//------------------------------------------------------------------------------------
	class LocalLeaseSet
	{
	public:
		LocalLeaseSet (std::shared_ptr<const IdentityEx> identity, const uint8_t * encryptionPublicKey,
			std::vector<std::shared_ptr<i2p::tunnel::InboundTunnel>> tunnels);
		LocalLeaseSet (std::shared_ptr<const IdentityEx> identity, const uint8_t * buf, size_t len);
		virtual ~LocalLeaseSet () = default;

		virtual uint8_t * GetBuffer () const { return m_Buffer.get (); };
		uint8_t * GetSignature () { return GetBuffer () + GetBufferLen () - GetSignatureLen (); };
		virtual size_t GetBufferLen () const { return m_BufferLen; };
		size_t GetSignatureLen () const { return m_Identity->GetSignatureLen (); };
		uint8_t * GetLeases () { return m_Leases; };

		const IdentHash & GetIdentHash () const { return m_Identity->GetIdentHash (); };
		std::shared_ptr<const IdentityEx> GetIdentity () const { return m_Identity; };
		bool IsExpired () const;
		uint64_t GetExpirationTime () const { return m_ExpirationTime; };
		void SetExpirationTime (uint64_t expirationTime) { m_ExpirationTime = expirationTime; };
		bool operator== (const LeaseSet & other) const
		{
			if (GetBufferLen () != other.GetBufferLen ()) return false;
			return GetBufferLen () == 0 || !memcmp (GetBuffer (), other.GetBuffer (), GetBufferLen ());
		};

		virtual uint8_t GetStoreType () const { return NETDB_STORE_TYPE_LEASESET; };
		virtual const IdentHash & GetStoreHash () const
		{
			return GetIdentHash ();
		}; // differ from ident hash for encrypted LeaseSet2
		virtual std::shared_ptr<const LocalLeaseSet> GetInnerLeaseSet () const
		{
			return nullptr;
		}; // non-null for encrypted LeaseSet2

	private:
		uint64_t m_ExpirationTime; // in milliseconds
		std::shared_ptr<const IdentityEx> m_Identity;
		std::unique_ptr<uint8_t[]> m_Buffer;
		uint8_t * m_Leases = nullptr;
		size_t m_BufferLen;
	};

	class LocalLeaseSet2 : public LocalLeaseSet
	{
	public:
		typedef std::list<std::shared_ptr<const i2p::crypto::LocalEncryptionKey>> EncryptionKeys;

		LocalLeaseSet2 (uint8_t storeType, const i2p::data::PrivateKeys & keys, const EncryptionKeys & encryptionKeys,
			const std::vector<std::shared_ptr<i2p::tunnel::InboundTunnel>> & tunnels, bool isPublic,
			uint64_t publishedTimestamp, bool isPublishedEncrypted = false);

		LocalLeaseSet2 (uint8_t storeType, std::shared_ptr<const IdentityEx> identity, const uint8_t * buf,
			size_t len); // from I2CP

		virtual ~LocalLeaseSet2 () = default;

		uint8_t * GetBuffer () const { return m_Buffer ? m_Buffer.get () + 1 : nullptr; };
		size_t GetBufferLen () const { return m_BufferLen; };

		uint8_t GetStoreType () const { return m_Buffer ? m_Buffer[0] : NETDB_STORE_TYPE_STANDARD_LEASESET2; };

	protected:
		LocalLeaseSet2 (std::shared_ptr<const IdentityEx> identity)
			: LocalLeaseSet (identity, nullptr, 0)
			, m_BufferLen (0) { }; // called from LocalEncryptedLeaseSet2

	protected:
		std::unique_ptr<uint8_t[]> m_Buffer; // 1 byte store type + actual buffer
		size_t m_BufferLen = 0;
	};

	const int ENCRYPTED_LEASESET_AUTH_TYPE_NONE = 0;
	const int ENCRYPTED_LEASESET_AUTH_TYPE_DH = 1;
	const int ENCRYPTED_LEASESET_AUTH_TYPE_PSK = 2;

	typedef i2p::data::Tag<32> AuthPublicKey;

	class LocalEncryptedLeaseSet2 : public LocalLeaseSet2
	{
	public:
		LocalEncryptedLeaseSet2 (std::shared_ptr<const LocalLeaseSet2> ls, const i2p::data::PrivateKeys & keys,
			int authType = ENCRYPTED_LEASESET_AUTH_TYPE_NONE,
			std::shared_ptr<std::vector<AuthPublicKey>> authKeys = nullptr);

		LocalEncryptedLeaseSet2 (
			std::shared_ptr<const IdentityEx> identity, const uint8_t * buf, size_t len); // from I2CP

		const IdentHash & GetStoreHash () const { return m_StoreHash; };
		std::shared_ptr<const LocalLeaseSet> GetInnerLeaseSet () const { return m_InnerLeaseSet; };

	private:
		bool CreateClientAuthData (const uint8_t * subcredential, int authType,
			std::shared_ptr<std::vector<AuthPublicKey>> authKeys, const uint8_t * authCookie, uint8_t * authData) const;

	private:
		IdentHash m_StoreHash;
		std::shared_ptr<const LocalLeaseSet2> m_InnerLeaseSet;
	};
}
}

#endif

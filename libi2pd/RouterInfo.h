/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: RouterInfo.
 *
 * RouterInfo parsing, serialization and capability model: identity, NTCP2/SSU2 addresses for
 * IPv4, IPv6 and mesh, caps, congestion, introducers, transport bitmasks and family signatures,
 * bounded by Java-compatible size, address and introducer limits. LocalRouterInfo rebuilds and
 * signs the published descriptor; instances are non-copyable, keep their signed buffer behind
 * a mutex and publish transport and profile state atomically for concurrent readers.
 *
 */

#ifndef ROUTER_INFO_H__
#define ROUTER_INFO_H__

#include <inttypes.h>
#include <string>
#include <string_view>
#include <tuple>
#include <map>
#include <vector>
#include <array>
#include <iostream>
#include <memory>
#include <atomic>
#include <mutex>
#include <utility>
#include <boost/asio.hpp>
#include "Identity.h"
#include "Profiling.h"
#include "Family.h"

namespace i2p
{
namespace data
{
	const char ROUTER_INFO_PROPERTY_LEASESETS[] = "netdb.knownLeaseSets";
	const char ROUTER_INFO_PROPERTY_ROUTERS[] = "netdb.knownRouters";
	const char ROUTER_INFO_PROPERTY_NETID[] = "netId";
	const char ROUTER_INFO_PROPERTY_VERSION[] = "router.version";
	const char ROUTER_INFO_PROPERTY_FAMILY[] = "family";
	const char ROUTER_INFO_PROPERTY_FAMILY_SIG[] = "family.sig";

	const char CAPS_FLAG_FLOODFILL = 'f';
	const char CAPS_FLAG_HIDDEN = 'H';
	const char CAPS_FLAG_REACHABLE = 'R';
	const char CAPS_FLAG_UNREACHABLE = 'U';
	/* bandwidth flags */
	const char CAPS_FLAG_LOW_BANDWIDTH1 = 'K'; /*   < 12 KBps */
	const char CAPS_FLAG_LOW_BANDWIDTH2 = 'L'; /*  12-48 KBps */
	const char CAPS_FLAG_LOW_BANDWIDTH3 = 'M'; /*  48-64 KBps */
	const char CAPS_FLAG_LOW_BANDWIDTH4 = 'N'; /*  64-128 KBps */
	const char CAPS_FLAG_HIGH_BANDWIDTH = 'O'; /* 128-256 KBps */
	const char CAPS_FLAG_EXTRA_BANDWIDTH1 = 'P'; /* 256-2048 KBps */
	const char CAPS_FLAG_EXTRA_BANDWIDTH2 = 'X'; /*   > 2048 KBps */
	// bandwidth limits in kBps
	const uint32_t LOW_BANDWIDTH_LIMIT = 48;
	const uint32_t HIGH_BANDWIDTH_LIMIT = 256;
	const uint32_t EXTRA_BANDWIDTH_LIMIT = 2048;
	// congesion flags
	const char CAPS_FLAG_MEDIUM_CONGESTION = 'D';
	const char CAPS_FLAG_HIGH_CONGESTION = 'E';
	const char CAPS_FLAG_REJECT_ALL_CONGESTION = 'G';

	const char CAPS_FLAG_V4 = '4';
	const char CAPS_FLAG_V6 = '6';
	const char CAPS_FLAG_SSU2_TESTING = 'B';
	const char CAPS_FLAG_SSU2_INTRODUCER = 'C';

	const uint8_t COST_NTCP2_PUBLISHED = 3;
	const uint8_t COST_NTCP2_NON_PUBLISHED = 14;
	const uint8_t COST_SSU2_DIRECT = 8;
	const uint8_t COST_SSU2_NON_PUBLISHED = 15;

	// I2P Java RouterInfo.MAX_UNCOMPRESSED_SIZE is 4096 (RouterInfo.java, 2.13.0).
	// Keep the same upper bound so a valid Java RouterInfo with many address
	// properties is not rejected by the C++ parser. NTCP2's on-wire handshake
	// still limits its own RI payload independently.
	const size_t MAX_RI_BUFFER_SIZE = 4096;
	// I2P Java RouterInfo.MAX_ADDRESSES is 16 (RouterInfo.java, 2.13.0).
	// Reject excess address entries before parsing rather than relying on the
	// RouterInfo byte cap to bound the loop.
	const uint8_t MAX_RI_ADDRESSES = 16;
	// SSU2 RouterAddress introducer options are ih[0-2], iexp[0-2] and
	// itag[0-2]. Java UDPAddress.MAX_INTRODUCERS is 3 (2.13.0).
	const uint8_t MAX_RI_INTRODUCERS = 3;
	// SSU2 spec, MTU section: 1280 is the minimum for both IPv4 and IPv6.
	// ML-KEM-768 handshakes need 1318 (IPv4) or 1338 (IPv6); Java rejects a
	// published SSU2 address below those values for pq=4.
	const int MIN_SSU2_MTU = 1280;
	const int MIN_SSU2_MLKEM768_MTU_IPV4 = 1318;
	const int MIN_SSU2_MLKEM768_MTU_IPV6 = 1338;
	const int HIGH_CONGESTION_INTERVAL = 15 * 60; // in seconds, 15 minutes
	const int INTRODUCER_UPDATE_INTERVAL = 20 * 60 * 1000; // in milliseconds, 20 minutes

	class RouterInfo : public RoutingDestination
	{
	public:
		enum SupportedTransportsIdx
		{
			eNTCP2V4Idx = 0,
			eNTCP2V6Idx,
			eSSU2V4Idx,
			eSSU2V6Idx,
			eNTCP2V6MeshIdx,
			eNumTransports
		};

#define TransportBit(tr) e##tr = (1 << e##tr##Idx)

		enum SupportedTransports
		{
			TransportBit (NTCP2V4), // 0x01
			TransportBit (NTCP2V6), // 0x02
			TransportBit (SSU2V4), // 0x04
			TransportBit (SSU2V6), // 0x08
			TransportBit (NTCP2V6Mesh), // 0x10
			eAllTransports = 0xFF
		};
		typedef uint8_t CompatibleTransports;

		enum Caps
		{
			eFloodfill = 0x01,
			eHighBandwidth = 0x02,
			eExtraBandwidth = 0x04,
			eReachable = 0x08,
			eHidden = 0x10,
			eUnreachable = 0x20
		};

		enum Congestion
		{
			eLowCongestion = 0,
			eMediumCongestion,
			eHighCongestion,
			eRejectAll
		};

		enum AddressCaps
		{
			eV4 = 0x01,
			eV6 = 0x02,
			eSSUTesting = 0x04,
			eSSUIntroducer = 0x08
		};

		enum TransportStyle
		{
			eTransportUnknown = 0,
			eTransportNTCP2,
			eTransportSSU2
		};

		struct Introducer
		{
			Introducer ()
				: iTag (0)
				, iExp (0)
			{
				iH.Fill (0);
			};
			IdentHash iH;
			uint32_t iTag;
			uint32_t iExp;
		};

		struct SSUExt
		{
			int mtu;
			std::vector<Introducer> introducers;
		};

		struct Address
		{
			TransportStyle transportStyle;
			boost::asio::ip::address host;
			Tag<32> s { }, i { }; // keys, i is first 16 bytes for NTCP2 and 32 bytes intro key for SSU
			int v = 2; // version
			// Bit N is set when SSU2 version N (3 or 4) was advertised in the
			// comma-separated "pq" address option. The base "v" option must
			// still contain 2; "pq" only selects the hybrid extension.
			uint8_t pqVersions = 0;
			int port;
			uint64_t date;
			uint8_t caps;
			bool published = false;
			std::unique_ptr<SSUExt> ssu; // not null for SSU

			bool SupportsSSU2Version (int version) const
			{
				return version == 2 || (version >= 3 && version <= 4 && (pqVersions & (1u << version)));
			}

			bool IsCompatible (const boost::asio::ip::address & other) const
			{
				return (IsV4 () && other.is_v4 ()) || (IsV6 () && other.is_v6 ());
			}

			bool operator== (const Address & other) const
			{
				return transportStyle == other.transportStyle && host == other.host && port == other.port;
			}

			bool operator!= (const Address & other) const { return !(*this == other); }

			bool IsNTCP2 () const { return transportStyle == eTransportNTCP2; };
			bool IsSSU2 () const { return transportStyle == eTransportSSU2; };
			bool IsPublishedNTCP2 () const { return IsNTCP2 () && published; };
			// Reachability requires either a concrete published endpoint or at
			// least one valid introducer. A "published" flag alone (for example
			// SetReachable() before any address block was received) must not
			// make an address with no host/port look reachable: the serialized
			// RouterInfo contains neither host nor introducers, so remote peers
			// could not use it.
			bool IsReachableSSU () const
			{
				return (bool)ssu && ((published && port && !host.is_unspecified ()) || UsesIntroducer ());
			}
			bool UsesIntroducer () const { return (bool)ssu && !ssu->introducers.empty (); };

			bool IsIntroducer () const { return caps & eSSUIntroducer; };
			bool IsPeerTesting () const { return caps & eSSUTesting; };

			// A concrete host determines the family. Address caps only declare
			// families for non-published/unspecified addresses and must not let an
			// untrusted "caps=6" turn an IPv4 endpoint into an IPv6 one.
			bool IsV4 () const { return !host.is_unspecified () ? host.is_v4 () : ((caps & AddressCaps::eV4) != 0); };
			bool IsV6 () const { return !host.is_unspecified () ? host.is_v6 () : ((caps & AddressCaps::eV6) != 0); };

			bool IsSameSubnet (const Address & other)
			{
				if (host.is_unspecified () || other.host.is_unspecified ()) return false;
				if (host.is_v4 () && other.host.is_v4 ())
					return !std::memcmp (
						host.to_v4 ().to_bytes ().data (), other.host.to_v4 ().to_bytes ().data (), 3); // /24
				if (host.is_v6 () && other.host.is_v6 ())
					return !std::memcmp (
						host.to_v6 ().to_bytes ().data (), other.host.to_v6 ().to_bytes ().data (), 7); // /56
				return false;
			}
		};

		class Buffer : public std::array<uint8_t, MAX_RI_BUFFER_SIZE>
		{
		public:
			Buffer () = default;
			Buffer (const uint8_t * buf, size_t len);
			Buffer (const Buffer & other)
				: Buffer (other.data (), other.m_BufferLen) { };

			size_t GetBufferLen () const { return m_BufferLen; };
			void SetBufferLen (size_t len) { m_BufferLen = len; };

		private:
			size_t m_BufferLen = 0;
		};

		typedef std::array<std::shared_ptr<Address>, eNumTransports> Addresses;
		typedef std::shared_ptr<Addresses> AddressesPtr;
		RouterInfo (const std::string & fullPath);
		RouterInfo (const RouterInfo &) = delete;
		RouterInfo & operator= (const RouterInfo &) = delete;
		RouterInfo (std::shared_ptr<Buffer> && buf, size_t len);
		RouterInfo (const uint8_t * buf, size_t len);
		virtual ~RouterInfo ();

		std::shared_ptr<const IdentityEx> GetRouterIdentity () const { return m_RouterIdentity; };
		void SetRouterIdentity (std::shared_ptr<const IdentityEx> identity);
		std::string GetIdentHashBase64 () const { return GetIdentHash ().ToBase64 (); };
		uint64_t GetTimestamp () const { return m_Timestamp.load (); };
		int GetVersion () const { return m_Version.load (); };
		virtual void SetProperty (std::string_view key, std::string_view value) { };
		virtual void ClearProperties () { };
		AddressesPtr GetAddresses () const; // should be called for local RI only, otherwise must return shared_ptr
		std::shared_ptr<const Address> GetNTCP2V4Address () const;
		std::shared_ptr<const Address> GetNTCP2V6Address () const;
		std::shared_ptr<const Address> GetPublishedNTCP2V4Address () const;
		std::shared_ptr<const Address> GetPublishedNTCP2V6Address () const;
		std::shared_ptr<const Address> GetYggdrasilAddress () const;
		std::shared_ptr<const Address> GetSSU2V4Address () const;
		std::shared_ptr<const Address> GetSSU2V6Address () const;
		std::shared_ptr<const Address> GetSSU2Address (bool v4) const;

		void AddNTCP2Address (const uint8_t * staticKey, const uint8_t * iv, int port, uint8_t caps); // non published
		void AddNTCP2Address (const uint8_t * staticKey, const uint8_t * iv, const boost::asio::ip::address & host,
			int port); // published
		void RemoveNTCP2Address (bool v4);
		void AddSSU2Address (
			const uint8_t * staticKey, const uint8_t * introKey, int port, uint8_t caps); // non published
		void AddSSU2Address (const uint8_t * staticKey, const uint8_t * introKey, const boost::asio::ip::address & host,
			int port); // published
		void RemoveSSU2Address (bool v4);
		void SetUnreachableAddressesTransportCaps (uint8_t transports); // bitmask of AddressCaps
		void UpdateSupportedTransports ();
		void UpdateIntroducers (uint64_t ts); // ts in seconds
		bool IsFloodfill () const { return m_IsFloodfill.load (); };
		void SetFloodfill () { m_IsFloodfill.store (true); };
		void ResetFloodfill () { m_IsFloodfill.store (false); };
		bool IsECIES () const
		{
			auto identity = m_RouterIdentity;
			return identity && identity->GetCryptoKeyType () == i2p::data::CRYPTO_KEY_TYPE_ECIES_X25519_AEAD;
		};
		bool IsNTCP2 (bool v4only = true) const;
		bool IsNTCP2V6 () const { return m_SupportedTransports.load () & eNTCP2V6; };
		bool IsSSU2V4 () const { return m_SupportedTransports.load () & eSSU2V4; };
		bool IsSSU2V6 () const { return m_SupportedTransports.load () & eSSU2V6; };
		bool IsV6 () const { return m_SupportedTransports.load () & (eNTCP2V6 | eSSU2V6); };
		bool IsV4 () const { return m_SupportedTransports.load () & (eNTCP2V4 | eSSU2V4); };
		bool IsMesh () const { return m_SupportedTransports.load () & eNTCP2V6Mesh; };
		void EnableV6 ();
		void DisableV6 ();
		void EnableV4 ();
		void DisableV4 ();
		void EnableMesh ();
		void DisableMesh ();
		bool IsCompatible (const RouterInfo & other) const
		{
			return m_SupportedTransports.load () & other.m_SupportedTransports.load ();
		};
		bool IsSameSubnet (const RouterInfo & other) const;
		bool IsReachableFrom (const RouterInfo & other) const
		{
			return m_ReachableTransports.load () & other.m_SupportedTransports.load ();
		};
		bool IsReachableBy (CompatibleTransports transports) const
		{
			return m_ReachableTransports.load () & transports;
		};
		CompatibleTransports GetCompatibleTransports (bool incoming) const
		{
			return incoming ? m_ReachableTransports.load () : m_SupportedTransports.load ();
		};
		CompatibleTransports GetPublishedTransports () const { return m_PublishedTransports.load (); };
		bool HasValidAddresses () const { return m_SupportedTransports.load () != 0; };
		// Java RouterInfo.isHidden() (2.13.0) checks both the "H" capability
		// and a hidden certificate in the router identity.
		bool IsHidden () const
		{
			if (m_Caps.load () & eHidden) return true;
			// A rejected/truncated identity may not have been fully copied into
			// m_StandardIdentity; don't inspect it before validation succeeded.
			if (m_IsUnreachable.load ()) return false;
			auto identity = m_RouterIdentity;
			return identity && identity->GetStandardIdentity ().certificate[0] == CERTIFICATE_TYPE_HIDDEN;
		};
		bool IsHighBandwidth () const { return m_Caps.load () & RouterInfo::eHighBandwidth; };
		bool IsExtraBandwidth () const { return m_Caps.load () & RouterInfo::eExtraBandwidth; };
		bool IsEligibleFloodfill () const;
		bool IsDeclaredFloodfill () const { return m_Caps.load () & RouterInfo::eFloodfill; };
		bool IsPublished (bool v4) const;
		bool IsPublishedOn (CompatibleTransports transports) const;
		bool IsNAT2NATOnly (const RouterInfo & other) const; // only NAT-to-NAT connection is possible
		bool IsSSU2PeerTesting (bool v4) const;
		bool IsSSU2Introducer (bool v4) const;
		bool IsHighCongestion (bool highBandwidth) const;

		uint8_t GetCaps () const { return m_Caps.load (); };
		char GetBandwidthCap () const { return m_BandwidthCap.load (); };
		void SetCaps (uint8_t caps) { m_Caps.store (caps); };

		Congestion GetCongestion () const { return m_Congestion.load (); };

		void SetUnreachable (bool unreachable) { m_IsUnreachable.store (unreachable); };
		bool IsUnreachable () const { return m_IsUnreachable.load (); };
		void ExcludeReachableTransports (CompatibleTransports transports)
		{
			m_ReachableTransports.fetch_and (static_cast<CompatibleTransports> (~transports));
		};

		const uint8_t * GetBuffer () const
		{
			std::lock_guard<std::mutex> l (m_BufferMutex);
			return m_Buffer ? m_Buffer->data () : nullptr;
		};
		const uint8_t * LoadBuffer (const std::string & fullPath); // load if necessary
		size_t GetBufferLen () const
		{
			std::lock_guard<std::mutex> l (m_BufferMutex);
			return m_Buffer ? m_Buffer->GetBufferLen () : 0;
		};
		void DeleteBuffer ()
		{
			std::lock_guard<std::mutex> l (m_BufferMutex);
			m_Buffer = nullptr;
			m_IsBufferScheduledToDelete.store (false);
		};
		std::shared_ptr<Buffer> GetSharedBuffer () const
		{
			std::lock_guard<std::mutex> l (m_BufferMutex);
			return m_Buffer;
		};
		std::shared_ptr<Buffer> CopyBuffer () const;
		void ScheduleBufferToDelete () { m_IsBufferScheduledToDelete.store (true); };
		void CancelBufferToDelete () { m_IsBufferScheduledToDelete.store (false); };
		bool IsBufferScheduledToDelete () const { return m_IsBufferScheduledToDelete.load (); };

		bool IsUpdated () const { return m_IsUpdated.load (); };
		void SetUpdated (bool updated) { m_IsUpdated.store (updated); };
		bool SaveToFile (const std::string & fullPath);
		static bool SaveToFile (const std::string & fullPath, std::shared_ptr<Buffer> buf);

		std::shared_ptr<RouterProfile> GetProfile () const;
		void DropProfile () { std::atomic_store (&m_Profile, std::shared_ptr<RouterProfile> { }); };
		bool HasProfile () const { return std::atomic_load (&m_Profile) != nullptr; };

		bool Update (const uint8_t * buf, size_t len);
		bool IsNewer (const uint8_t * buf, size_t len) const;

		/** return true if we are in a router family and the signature is valid */
		bool IsFamily (FamilyID famid) const
		{
			auto id = m_FamilyID.load ();
			return id && id == famid;
		};
		bool IsSameFamily (const RouterInfo & other) const { return IsFamily (other.m_FamilyID.load ()); };

		// implements RoutingDestination
		std::shared_ptr<const IdentityEx> GetIdentity () const { return m_RouterIdentity; };
		void Encrypt (const uint8_t * data, uint8_t * encrypted) const;

		bool IsDestination () const { return false; };

	protected:
		RouterInfo ();
		uint8_t * GetBufferPointer (size_t offset = 0)
		{
			std::lock_guard<std::mutex> l (m_BufferMutex);
			return m_Buffer ? m_Buffer->data () + offset : nullptr;
		};
		void UpdateBuffer (const uint8_t * buf, size_t len);
		void SetBufferLen (size_t len)
		{
			std::lock_guard<std::mutex> l (m_BufferMutex);
			if (m_Buffer) m_Buffer->SetBufferLen (len);
		};
		void RefreshTimestamp ();
		void SetTimestamp (uint64_t ts) { m_Timestamp.store (ts); };
		CompatibleTransports GetReachableTransports () const { return m_ReachableTransports.load (); };
		void SetReachableTransports (CompatibleTransports transports) { m_ReachableTransports.store (transports); };
		void SetCongestion (Congestion c) { m_Congestion.store (c); };

	private:
		bool LoadFile (const std::string & fullPath);
		void ReadFromFile (const std::string & fullPath);
		bool ReadFromBuffer (const uint8_t * buf, size_t len); // signed body only; signature already removed
		void ReadFromBuffer (bool verifySignature);
		std::string_view ExtractString (const uint8_t * buf, size_t len, bool & valid) const;
		std::tuple<std::string_view, std::string_view, size_t> ExtractParam (const uint8_t * buf, size_t len) const;
		void ExtractCaps (std::string_view value, uint8_t & caps, char & bandwidthCap, Congestion & congestion) const;
		uint8_t ExtractAddressCaps (std::string_view value) const;
		void UpdateIntroducers (std::shared_ptr<Address> address, uint64_t ts);
		template <typename Filter> std::shared_ptr<const Address> GetAddress (Filter filter) const;
		virtual bool StoresProperties () const { return false; };
		virtual std::shared_ptr<Buffer> NewBuffer () const;
		virtual std::shared_ptr<Address> NewAddress () const;
		virtual AddressesPtr NewAddresses () const;
		virtual std::shared_ptr<IdentityEx> NewIdentity (const uint8_t * buf, size_t len) const;

	private:
		std::atomic<FamilyID> m_FamilyID;
		std::shared_ptr<const IdentityEx> m_RouterIdentity;
		std::shared_ptr<Buffer> m_Buffer;
		std::atomic<uint64_t> m_Timestamp; // in milliseconds
		// libc++ does not provide the C++20 atomic<shared_ptr> specialization
		// used by the other toolchains. Keep these as ordinary shared_ptr
		// objects and use the standard atomic_load/atomic_store free functions
		// at the access points; the mutable marker is required by const readers.
		mutable AddressesPtr m_Addresses;
		std::atomic<bool> m_IsUpdated, m_IsUnreachable, m_IsFloodfill, m_IsBufferScheduledToDelete;
		std::atomic<CompatibleTransports> m_SupportedTransports, m_ReachableTransports, m_PublishedTransports;
		std::atomic<uint8_t> m_Caps;
		std::atomic<char> m_BandwidthCap;
		std::atomic<int> m_Version;
		std::atomic<Congestion> m_Congestion;
		mutable std::shared_ptr<RouterProfile> m_Profile;
		mutable std::mutex m_BufferMutex;

	public:
		static std::string GetTransportName (SupportedTransports tr);
	};

	class LocalRouterInfo : public RouterInfo
	{
	public:
		LocalRouterInfo () = default;
		void CreateBuffer (const PrivateKeys & privateKeys);
		void UpdateCaps (uint8_t caps);
		bool UpdateCongestion (Congestion c); // returns true if updated

		void SetProperty (std::string_view key, std::string_view value) override;
		void DeleteProperty (const std::string & key);
		std::string GetProperty (const std::string & key) const;
		void ClearProperties () override { m_Properties.clear (); };
		void UpdateFloodfillProperty (bool floodfill);

		bool AddSSU2Introducer (const Introducer & introducer, bool v4);
		bool RemoveSSU2Introducer (const IdentHash & h, bool v4);
		bool UpdateSSU2Introducer (const IdentHash & h, bool v4, uint32_t iTag, uint32_t iExp);

	private:
		void WriteToStream (std::ostream & s) const;
		void UpdateCapsProperty ();
		void WriteString (const std::string & str, std::ostream & s) const;
		bool StoresProperties () const override { return true; };
		std::shared_ptr<Buffer> NewBuffer () const override;
		std::shared_ptr<Address> NewAddress () const override;
		RouterInfo::AddressesPtr NewAddresses () const override;
		std::shared_ptr<IdentityEx> NewIdentity (const uint8_t * buf, size_t len) const override;

	private:
		std::map<std::string, std::string> m_Properties;
	};
}
}

#endif

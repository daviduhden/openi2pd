/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: SOCKS.
 *
 * Declares the SOCKS acceptor, upstream-proxy configuration, UDP port pool,
 * the local RFC 1929 username/password authenticator, the fixed success
 * replies and the bounded virtual IPv4 mapping used for torsocks-style
 * resolution.
 *
 */

#ifndef SOCKS_H__
#define SOCKS_H__

#include <array>
#include <cstdint>
#include <memory>
#include <set>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <cstddef>
#include <boost/asio.hpp>
#include <mutex>
#include "I2PService.h"
// ProxySecretEquals() and the shared 255-byte field bound are declared by the
// HTTP proxy header so both local proxy authenticators use one comparison and
// one limit instead of two copies of a security primitive.
#include "HTTPProxy.h"

namespace i2p
{
namespace proxy
{
	/** RFC 1929 ULEN/PLEN ceiling. */
	inline constexpr std::size_t SOCKS_AUTH_MAX_FIELD_SIZE = PROXY_AUTH_MAX_FIELD_SIZE;

	/** Fixed receive buffer for one decoded username or password. */
	inline constexpr std::size_t SOCKS_AUTH_BUFFER_SIZE = SOCKS_AUTH_MAX_FIELD_SIZE + 1;

	/**
	 * Success replies.
	 *
	 * RFC 1928 requires BND.ADDR/BND.PORT in a success reply. The proxy has no
	 * meaningful bound endpoint for an I2P stream or for a UDP relay that is
	 * not a wildcard, so it answers with the loopback address like I2P Java
	 * `SOCKS5Server.confirmConnection()` (127.0.0.1, port 1) and
	 * `handleUDP()` (127.0.0.1, relay port). A wildcard 0.0.0.0 must never be
	 * sent: a client could otherwise try to connect back to its own wildcard.
	 */
	inline constexpr std::uint32_t SOCKS5_REPLY_LOOPBACK_ADDRESS = 0x7F000001; // 127.0.0.1
	inline constexpr std::uint16_t SOCKS5_CONNECT_REPLY_PORT = 1;

	/** `05 00 00 01 7F 00 00 01 00 01` (CONNECT succeeded). */
	inline std::array<std::uint8_t, 10> SOCKS5ConnectSuccessReply () noexcept
	{
		return { 0x05, 0x00, 0x00, 0x01, 0x7f, 0x00, 0x00, 0x01, 0x00, SOCKS5_CONNECT_REPLY_PORT };
	}

	/** `05 00 00 01 7F 00 00 01 <port>` (UDP ASSOCIATE succeeded). */
	inline std::array<std::uint8_t, 10> SOCKS5UdpAssociateReply (std::uint16_t port) noexcept
	{
		return { 0x05, 0x00, 0x00, 0x01, 0x7f, 0x00, 0x00, 0x01, static_cast<std::uint8_t> (port >> 8),
			static_cast<std::uint8_t> (port & 0xff) };
	}

	/** Bounds for the torsocks RESOLVE cache, matching I2P Java LHMCache(256). */
	inline constexpr std::size_t SOCKS_RESOLVE_CACHE_MAX_ENTRIES = 256;
	inline constexpr std::uint64_t SOCKS_RESOLVE_CACHE_TTL_MS = 10 * 60 * 1000;
	/** 255.0.0.0/8 is reserved and can never name a real host. */
	inline constexpr std::uint32_t SOCKS_RESOLVE_FAKE_PREFIX = 0xFF000000;

	namespace socks_detail
	{
		inline std::uint64_t LoadLe64 (const std::uint8_t * data) noexcept
		{
			std::uint64_t value = 0;
			for (unsigned int i = 0; i < 8; ++i) value |= static_cast<std::uint64_t> (data[i]) << (i * 8);
			return value;
		}

		inline void StoreLe64 (std::uint64_t value, std::uint8_t * out) noexcept
		{
			for (unsigned int i = 0; i < 8; ++i) out[i] = static_cast<std::uint8_t> (value >> (i * 8));
		}

		inline std::uint64_t RotateLeft (std::uint64_t value, int bits) noexcept
		{
			return (value << bits) | (value >> (64 - bits));
		}

		inline void SipRound (std::uint64_t & v0, std::uint64_t & v1, std::uint64_t & v2, std::uint64_t & v3) noexcept
		{
			v0 += v1;
			v1 = RotateLeft (v1, 13);
			v1 ^= v0;
			v0 = RotateLeft (v0, 32);
			v2 += v3;
			v3 = RotateLeft (v3, 16);
			v3 ^= v2;
			v0 += v3;
			v3 = RotateLeft (v3, 21);
			v3 ^= v0;
			v2 += v1;
			v1 = RotateLeft (v1, 17);
			v1 ^= v2;
			v2 = RotateLeft (v2, 32);
		}

		/**
		 * SipHash-2-4 over a 128-bit key. The project-owned implementation is
		 * used here rather than the provider-backed one because the fake-IP
		 * derivation must stay identical across OpenSSL and LibreSSL builds;
		 * the output is a routing hint, not a secret.
		 */
		inline std::uint64_t SipHash24 (const std::uint8_t * key, const std::uint8_t * data, std::size_t size) noexcept
		{
			std::uint64_t v0 = 0x736f6d6570736575ULL;
			std::uint64_t v1 = 0x646f72616e646f6dULL;
			std::uint64_t v2 = 0x6c7967656e657261ULL;
			std::uint64_t v3 = 0x7465646279746573ULL;
			const std::uint64_t k0 = LoadLe64 (key);
			const std::uint64_t k1 = LoadLe64 (key + 8);
			v3 ^= k1;
			v2 ^= k0;
			v1 ^= k1;
			v0 ^= k0;

			const std::size_t blocks = size / 8;
			for (std::size_t i = 0; i < blocks; ++i)
			{
				const std::uint64_t message = LoadLe64 (data + i * 8);
				v3 ^= message;
				for (int round = 0; round < 2; ++round) SipRound (v0, v1, v2, v3);
				v0 ^= message;
			}

			std::uint64_t tail = static_cast<std::uint64_t> (size) << 56;
			const std::size_t remaining = size & 7u;
			for (std::size_t i = 0; i < remaining; ++i)
				tail |= static_cast<std::uint64_t> (data[blocks * 8 + i]) << (i * 8);
			v3 ^= tail;
			for (int round = 0; round < 2; ++round) SipRound (v0, v1, v2, v3);
			v0 ^= tail;
			v2 ^= 0xff;
			for (int round = 0; round < 4; ++round) SipRound (v0, v1, v2, v3);
			return v0 ^ v1 ^ v2 ^ v3;
		}
	} // namespace socks_detail

	/**
	 * Deterministic fake IPv4 for a destination: 255.a.b.c derived from a keyed
	 * SipHash of the name. The mapping is stable for the process lifetime and
	 * never overlaps a routable address.
	 */
	inline boost::asio::ip::address_v4 SocksFakeIPv4Address (
		std::string_view destination, const std::array<std::uint8_t, 16> & key) noexcept
	{
		const auto hash = socks_detail::SipHash24 (
			key.data (), reinterpret_cast<const std::uint8_t *> (destination.data ()), destination.size ());
		const auto low = static_cast<std::uint32_t> (hash & 0x00ffffffu);
		return boost::asio::ip::address_v4 (SOCKS_RESOLVE_FAKE_PREFIX | low);
	}

	/**
	 * Bounded, mutex-guarded cache for torsocks RESOLVE.
	 *
	 * A lookup maps a fake IPv4 back to the destination name, and an insert
	 * maps the name to its fake IPv4. Entries expire after
	 * SOCKS_RESOLVE_CACHE_TTL_MS and the cache never holds more than
	 * SOCKS_RESOLVE_CACHE_MAX_ENTRIES entries; the entry that expires first is
	 * evicted when every entry is still live.
	 */
	class SocksResolveCache
	{
	public:
		explicit SocksResolveCache (std::array<std::uint8_t, 16> key = { }) noexcept
			: m_Key (key)
		{
		}

		void SetKey (const std::array<std::uint8_t, 16> & key) noexcept
		{
			std::lock_guard<std::mutex> lock (m_Mutex);
			m_Key = key;
		}

		/** Insert or refresh @a destination and return its fake address. */
		boost::asio::ip::address_v4 Insert (std::string_view destination, std::uint64_t nowMs)
		{
			std::lock_guard<std::mutex> lock (m_Mutex);
			const auto address = SocksFakeIPv4Address (destination, m_Key);
			PurgeExpiredLocked (nowMs);

			const auto key = address.to_uint ();
			auto existing = m_Entries.find (key);
			if (existing != m_Entries.end ())
			{
				// A collision between two different names is possible; the
				// newest RESOLVE wins and the stale mapping is dropped.
				if (std::string_view (existing->second.host) != destination)
					LogPrint (eLogWarning, "SOCKS: resolve cache collision, replacing an entry");
				existing->second.host.assign (destination);
				existing->second.expiresMs = nowMs + SOCKS_RESOLVE_CACHE_TTL_MS;
				return address;
			}
			if (m_Entries.size () >= SOCKS_RESOLVE_CACHE_MAX_ENTRIES)
			{
				auto oldest = m_Entries.begin ();
				for (auto it = m_Entries.begin (); it != m_Entries.end (); ++it)
					if (it->second.expiresMs < oldest->second.expiresMs) oldest = it;
				m_Entries.erase (oldest);
			}
			Entry entry;
			entry.host.assign (destination);
			entry.expiresMs = nowMs + SOCKS_RESOLVE_CACHE_TTL_MS;
			m_Entries.emplace (key, std::move (entry));
			return address;
		}

		/** @return the live destination for @a address, or an empty string. */
		std::string Lookup (const boost::asio::ip::address_v4 & address, std::uint64_t nowMs) const
		{
			std::lock_guard<std::mutex> lock (m_Mutex);
			auto it = m_Entries.find (address.to_uint ());
			if (it == m_Entries.end ()) return { };
			if (it->second.expiresMs <= nowMs)
			{
				m_Entries.erase (it);
				return { };
			}
			return it->second.host;
		}

		std::size_t Size () const
		{
			std::lock_guard<std::mutex> lock (m_Mutex);
			return m_Entries.size ();
		}

		void Clear ()
		{
			std::lock_guard<std::mutex> lock (m_Mutex);
			m_Entries.clear ();
		}

	private:
		struct Entry
		{
			std::string host;
			std::uint64_t expiresMs = 0;
		};

		void PurgeExpiredLocked (std::uint64_t nowMs)
		{
			for (auto it = m_Entries.begin (); it != m_Entries.end ();)
			{
				if (it->second.expiresMs <= nowMs)
					it = m_Entries.erase (it);
				else
					++it;
			}
		}

		mutable std::mutex m_Mutex;
		std::map<std::uint32_t, Entry> m_Entries;
		std::array<std::uint8_t, 16> m_Key { };
	};

	/**
	 * @return true when an RFC 1929 field length (taken from ULEN/PLEN or from
	 * the configured credential) is acceptable: 1..255 bytes.
	 */
	constexpr bool SOCKSUserPassFieldValid (std::size_t length) noexcept
	{
		return length >= 1 && length <= SOCKS_AUTH_MAX_FIELD_SIZE;
	}

	enum class SOCKSAuthMethodChoice
	{
		/** Method 0x00: proceed without a credential exchange. */
		None,
		/** Method 0x02: the RFC 1929 username/password sub-negotiation. */
		UserPass,
		/** Method 0xFF: reply and close; no offered method can be satisfied. */
		Unacceptable
	};

	/**
	 * RFC 1929 method selection.
	 *
	 * With local authentication enabled only a verifiable credential exchange
	 * is acceptable (0x02). Without it, only 0x00 is acceptable: a client that
	 * offers nothing but 0x02 must not be let through, because the proxy has no
	 * credential to compare against and would otherwise accept arbitrary input.
	 */
	constexpr SOCKSAuthMethodChoice SOCKSChooseAuthMethod (
		bool auth_enabled, bool offered_none, bool offered_userpass) noexcept
	{
		if (auth_enabled)
			return offered_userpass ? SOCKSAuthMethodChoice::UserPass : SOCKSAuthMethodChoice::Unacceptable;
		return offered_none ? SOCKSAuthMethodChoice::None : SOCKSAuthMethodChoice::Unacceptable;
	}

	/**
	 * Local SOCKS5 username/password credential. Same fail-closed contract as
	 * ProxyBasicAuth: `enabled` with an empty or oversized field denies every
	 * client instead of accepting arbitrary credentials.
	 */
	struct SOCKSUserPassAuth
	{
		bool enabled = false;
		std::string username;
		std::string password;

		bool IsEnabled () const noexcept { return enabled; }

		bool HasUsableCredentials () const noexcept
		{
			return enabled && SOCKSUserPassFieldValid (username.size ()) && SOCKSUserPassFieldValid (password.size ());
		}

		bool Verify (std::string_view user, std::string_view password_provided) const noexcept
		{
			if (!HasUsableCredentials ()) return false;
			// Both fields are always compared; a mismatch counter keeps the
			// evaluation free of a short-circuit.
			unsigned int mismatches = 0;
			if (!ProxySecretEquals (username, user)) ++mismatches;
			if (!ProxySecretEquals (password, password_provided)) ++mismatches;
			return mismatches == 0;
		}
	};

	class SOCKSServer : public i2p::client::TCPIPAcceptor
	{
	public:
		SOCKSServer (const std::string & name, const std::string & address, uint16_t port, bool outEnable,
			const std::string & outAddress, uint16_t outPort,
			std::shared_ptr<i2p::client::ClientDestination> localDestination = nullptr);
		~SOCKSServer () { };

		void SetUpstreamProxy (const std::string & addr, const uint16_t port);
		boost::asio::ip::udp::endpoint GetNextLocalUDPEndpoint ();
		void ReleaseLocalUDPPort (uint16_t port);

		/**
		 * Torsocks RESOLVE mapping. A fake address is 255/8 and expires after
		 * SOCKS_RESOLVE_CACHE_TTL_MS; the cache holds at most 256 entries.
		 * GetResolvedAddress() returns a copy because the cache is guarded and
		 * may evict the entry at any time.
		 */
		std::string GetResolvedAddress (const boost::asio::ip::address_v4 & addr) const;
		boost::asio::ip::address_v4 ResolveAddress (std::string_view resolved);
		bool HasResolvedAddresses () const { return m_ResolveCache.Size () != 0; }
		std::size_t GetResolvedAddressCount () const { return m_ResolveCache.Size (); }

		/**
		 * Local SOCKS5 authentication state. The constructor reads
		 * `socksproxy.auth.enabled`, `socksproxy.auth.username` and
		 * `socksproxy.auth.password`; handlers snapshot it when accepted.
		 */
		const SOCKSUserPassAuth & GetAuth () const { return m_Auth; }
		void SetAuth (std::string username, std::string password);

	protected:
		// Implements TCPIPAcceptor
		std::shared_ptr<i2p::client::I2PServiceHandler> CreateHandler (
			std::shared_ptr<boost::asio::ip::tcp::socket> socket) override;
		const char * GetName () const override { return m_Name.c_str (); }

	private:
		std::string m_Name;
		std::string m_UpstreamProxyAddress;
		uint16_t m_UpstreamProxyPort;
		bool m_UseUpstreamProxy;
		SOCKSUserPassAuth m_Auth;
		std::set<uint16_t> m_UDPPorts;
		SocksResolveCache m_ResolveCache;
	};

	typedef SOCKSServer SOCKSProxy;
}
}
#endif

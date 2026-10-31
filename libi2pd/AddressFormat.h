/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: address format.
 *
 * Small IPv4/IPv6 and endpoint formatters. POSIX uses inet_ntop and
 * appends scope IDs explicitly, so Boost.Asio's unsafe POSIX scope-ID
 * sprintf/strcat path is not instantiated. Windows uses Asio's
 * WSAAddressToString path and appends a missing scope suffix
 * explicitly.
 */

#ifndef ADDRESS_FORMAT_H
#define ADDRESS_FORMAT_H

#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <boost/asio/ip/address.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ip/udp.hpp>

#if !defined(_WIN32)
#include <arpa/inet.h>
#include <net/if.h>
#endif

namespace i2p
{
namespace util
{
	namespace net
	{
		inline std::string FormatAddress (const boost::asio::ip::address_v4 & address);
		inline std::string FormatAddress (const boost::asio::ip::address_v6 & address);
		inline std::string FormatAddress (const boost::asio::ip::address & address);

		/// Format a raw address plus scope ID without Boost.Asio's POSIX
		/// scope-ID formatter (which uses an unsafe sprintf/strcat path).
		/// @pre source is null or points to 4 (IPv4) / 16 (IPv6) bytes.
		/// @param scopeId IPv6 zone index; for link-local and link-local
		/// multicast addresses an interface name is emitted when one can be
		/// resolved, otherwise the numeric index is appended.
		/// @return Empty string on a null source or a formatting
		///         failure, so a caller must not render an empty address as a
		///         valid endpoint.
		inline std::string FormatAddressBytes (bool ipv6, const void * source, std::uint32_t scopeId)
		{
			if (!source) return { };
#if defined(_WIN32)
			// Asio's Windows path uses WSAAddressToString and does not contain
			// the POSIX sprintf/strcat scope-ID branch.
			if (ipv6)
			{
				boost::asio::ip::address_v6::bytes_type bytes { };
				std::memcpy (bytes.data (), source, bytes.size ());
				return FormatAddress (boost::asio::ip::address_v6 (bytes, scopeId));
			}
			boost::asio::ip::address_v4::bytes_type bytes { };
			std::memcpy (bytes.data (), source, bytes.size ());
			return FormatAddress (boost::asio::ip::address_v4 (bytes));
#else
			std::array<char, INET6_ADDRSTRLEN> buffer { };
			const int family = ipv6 ? AF_INET6 : AF_INET;
			if (!::inet_ntop (family, source, buffer.data (), buffer.size ())) return { };
			std::string result (buffer.data ());
			if (ipv6 && scopeId != 0)
			{
				const auto * bytes = static_cast<const unsigned char *> (source);
				const bool linkLocal = bytes[0] == 0xfe && (bytes[1] & 0xc0) == 0x80;
				const bool multicastLinkLocal = bytes[0] == 0xff && (bytes[1] & 0x0f) == 0x02;
				result.push_back ('%');
				char interfaceName[IF_NAMESIZE] { };
				if ((linkLocal || multicastLinkLocal) && if_indextoname (scopeId, interfaceName) != nullptr)
					result.append (interfaceName);
				else
					result.append (std::to_string (scopeId));
			}
			return result;
#endif
		}

		inline std::string FormatAddress (const boost::asio::ip::address_v4 & address)
		{
#if defined(_WIN32)
			try
			{
				return address.to_string ();
			}
			catch (...)
			{
				return { };
			}
#else
			const auto bytes = address.to_bytes ();
			return FormatAddressBytes (false, bytes.data (), 0);
#endif
		}

		inline std::string FormatAddress (const boost::asio::ip::address_v6 & address)
		{
#if defined(_WIN32)
			try
			{
				auto result = address.to_string ();
				if (address.scope_id () != 0 && result.find ('%') == std::string::npos)
					result += '%' + std::to_string (address.scope_id ());
				return result;
			}
			catch (...)
			{
				return { };
			}
#else
			const auto bytes = address.to_bytes ();
			return FormatAddressBytes (true, bytes.data (), static_cast<std::uint32_t> (address.scope_id ()));
#endif
		}

		inline std::string FormatAddress (const boost::asio::ip::address & address)
		{
			return address.is_v4 () ? FormatAddress (address.to_v4 ()) : FormatAddress (address.to_v6 ());
		}

		inline std::string FormatEndpoint (const boost::asio::ip::tcp::endpoint & endpoint)
		{
			const auto address = FormatAddress (endpoint.address ());
			if (endpoint.address ().is_v6 ()) return '[' + address + "]:" + std::to_string (endpoint.port ());
			return address + ':' + std::to_string (endpoint.port ());
		}

		inline std::string FormatEndpoint (const boost::asio::ip::udp::endpoint & endpoint)
		{
			const auto address = FormatAddress (endpoint.address ());
			if (endpoint.address ().is_v6 ()) return '[' + address + "]:" + std::to_string (endpoint.port ());
			return address + ':' + std::to_string (endpoint.port ());
		}
	}
}
}

#endif // ADDRESS_FORMAT_H

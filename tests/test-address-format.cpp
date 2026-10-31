/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: test address formatting.
 *
 * Pins IPv4, IPv6, link-local scope-ID and endpoint formatting used by
 * logs and configuration. It deliberately does not invoke Boost.Asio's
 * formatter.
 */

#include <iostream>
#include <string>

#include <boost/asio/ip/address.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ip/udp.hpp>

#include "AddressFormat.h"

namespace
{
bool Check (bool condition, const char * message)
{
	if (!condition) std::cerr << "address-format: " << message << std::endl;
	return condition;
}
}

int main ()
{
	using boost::asio::ip::address_v4;
	using boost::asio::ip::address_v6;

	if (!Check (i2p::util::net::FormatAddress (address_v4::loopback ()) == "127.0.0.1", "IPv4 loopback")) return 1;
	if (!Check (i2p::util::net::FormatAddress (address_v4::any ()) == "0.0.0.0", "IPv4 any")) return 1;
	if (!Check (i2p::util::net::FormatAddress (address_v6::loopback ()) == "::1", "IPv6 loopback")) return 1;
	if (!Check (i2p::util::net::FormatAddress (address_v6::any ()) == "::", "IPv6 any")) return 1;

	const auto globalV6 = boost::asio::ip::make_address_v6 ("2001:db8::1");
	if (!Check (i2p::util::net::FormatAddress (globalV6) == "2001:db8::1", "IPv6 canonical form")) return 1;
	if (!Check (i2p::util::net::FormatAddress (globalV6).find ('%') == std::string::npos,
			"global IPv6 has no scope suffix"))
		return 1;

	const auto scopedV6 = address_v6 (globalV6.to_bytes (), 7);
	const auto scoped = i2p::util::net::FormatAddress (scopedV6);
	if (!Check (scoped == "2001:db8::1%7", "non-link-local IPv6 uses numeric scope")) return 1;

	const auto linkLocal = address_v6 (boost::asio::ip::make_address_v6 ("fe80::1").to_bytes (), 7);
	const auto linkLocalText = i2p::util::net::FormatAddress (linkLocal);
	if (!Check (linkLocalText.rfind ("fe80::1%", 0) == 0 && linkLocalText.size () > 8, "IPv6 link-local scope suffix"))
		return 1;

	const boost::asio::ip::tcp::endpoint v4Endpoint (address_v4::loopback (), 8080);
	if (!Check (i2p::util::net::FormatEndpoint (v4Endpoint) == "127.0.0.1:8080", "IPv4 endpoint")) return 1;
	const boost::asio::ip::tcp::endpoint v6Endpoint (address_v6::loopback (), 8080);
	if (!Check (i2p::util::net::FormatEndpoint (v6Endpoint) == "[::1]:8080", "IPv6 endpoint")) return 1;
	const boost::asio::ip::udp::endpoint scopedEndpoint (linkLocal, 9000);
	const auto scopedEndpointText = i2p::util::net::FormatEndpoint (scopedEndpoint);
	if (!Check (scopedEndpointText.rfind ("[fe80::1%", 0) == 0 && scopedEndpointText.size () > 10
				&& scopedEndpointText.compare (scopedEndpointText.size () - 6, 6, "]:9000") == 0,
			"IPv6 scoped UDP endpoint"))
		return 1;

	return 0;
}

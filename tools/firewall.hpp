/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: firewall.
 *
 * Renders outbound allow rules for NTCP2/SSU2 endpoints in iptables, nftables,
 * Windows Firewall and OpenBSD pf syntax. It accepts only validated numeric
 * public addresses and rejects unsafe nft identifiers; nothing is executed.
 *
 */

#ifndef I2PD_TOOLS_FIREWALL_HPP
#define I2PD_TOOLS_FIREWALL_HPP

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include <boost/asio/ip/address.hpp>
#include "AddressFormat.h"

namespace i2p::tools::firewall
{
enum class Backend
{
	IptablesLegacy, // -f: preserve the historical rule fragment
	Iptables, // explicit --firewall=iptables: full commands
	Nftables,
	Windows,
	Pf
};

enum class Transport
{
	NTCP2,
	SSU2
};

struct Endpoint
{
	std::string address; // canonical text, no brackets
	uint16_t port;
	Transport transport;
	bool ipv6;
};

struct RenderOptions
{
	Backend backend = Backend::IptablesLegacy;
	std::string nftFamily = "inet";
	std::string nftTable = "filter";
	std::string nftChain = "output";
	std::string ruleTag; // public router hash prefix for unambiguous names
};

inline const char * TransportName (Transport transport)
{
	return transport == Transport::NTCP2 ? "NTCP2" : "SSU2";
}

inline const char * L4Protocol (Transport transport)
{
	return transport == Transport::NTCP2 ? "tcp" : "udp";
}

/**
	 * Parse and validate an address/port/transport tuple. Numeric addresses
	 * only: RouterInfo files are untrusted, and a hostname would make the
	 * generated rules depend on DNS.
	 */
inline bool IsNumericAddressText (std::string_view text)
{
	if (text.empty ()) return false;
	return std::all_of (text.begin (), text.end (),
		[] (unsigned char ch)
		{
			return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F') || ch == ':'
				|| ch == '.';
		});
}

inline std::optional<Endpoint> MakeEndpoint (std::string_view addressText, int port, Transport transport)
{
	if (transport != Transport::NTCP2 && transport != Transport::SSU2) return std::nullopt;
	if (port < 1 || port > 65535) return std::nullopt;
	if (addressText.empty () || addressText.size () > 64) return std::nullopt;

	// Keep the grammar numeric. This also rejects IPv6 scope identifiers
	// ("fe80::1%eth0"), bracketed literals and whitespace before the
	// address parser can normalise them into rule text.
	if (!IsNumericAddressText (addressText)) return std::nullopt;

	boost::system::error_code ec;
	auto address = boost::asio::ip::make_address (std::string (addressText), ec);
	if (ec || address.is_unspecified () || address.is_multicast () || address.is_loopback ()) return std::nullopt;

	// A v4-mapped v6 address would bypass the IPv4 checks above and is not
	// a valid RouterInfo endpoint. IPv4-compatible (::a.b.c.d) and
	// link-local destinations are also unusable for a public I2P router and
	// would create misleading firewall rules.
	if (address.is_v6 ())
	{
		const auto bytes = address.to_v6 ().to_bytes ();
		if (address.to_v6 ().is_v4_mapped ()) return std::nullopt;
		const bool ipv4Compatible
			= std::all_of (bytes.begin (), bytes.begin () + 12, [] (unsigned char byte) { return byte == 0; });
		if (ipv4Compatible) return std::nullopt;
		if (bytes[0] == 0xfe && (bytes[1] & 0xc0) == 0x80) return std::nullopt; // fe80::/10
		if ((bytes[0] & 0xfe) == 0xfc) return std::nullopt; // fc00::/7 ULA
		if (bytes[0] == 0xfe && (bytes[1] & 0xc0) == 0xc0) return std::nullopt; // fec0::/10 site-local
	}
	else
	{
		const auto bytes = address.to_v4 ().to_bytes ();
		if (bytes[0] == 169 && bytes[1] == 254) return std::nullopt; // 169.254.0.0/16
		// Do not turn a private, CGNAT or reserved address from an untrusted
		// RouterInfo into a firewall rule. Documentation/benchmark ranges are
		// intentionally left to the caller because the unit tests use them.
		const uint32_t value = address.to_v4 ().to_uint ();
		if ((value & 0xff000000u) == 0x0a000000u // 10.0.0.0/8
			|| (value & 0xfff00000u) == 0xac100000u // 172.16.0.0/12
			|| (value & 0xffff0000u) == 0xc0a80000u // 192.168.0.0/16
			|| (value & 0xffc00000u) == 0x64400000u // 100.64.0.0/10 CGNAT
			|| (value & 0xff000000u) == 0x00000000u // 0.0.0.0/8
			|| (value & 0xf0000000u) == 0xf0000000u) // 240.0.0.0/4
			return std::nullopt;
		// The limited broadcast address is neither unicast nor useful as a
		// destination; emitting it would create an over-broad rule.
		if (address.to_v4 () == boost::asio::ip::address_v4::broadcast ()) return std::nullopt;
	}

	return Endpoint { i2p::util::net::FormatAddress (address), static_cast<uint16_t> (port), transport,
		address.is_v6 () };
}

/**
	 * Revalidate an Endpoint at the rendering boundary. Endpoint is deliberately
	 * a small aggregate, so callers should use MakeEndpoint(); this guard keeps a
	 * manually constructed or deserialized endpoint from bypassing its family,
	 * canonical-address and port checks.
	 */
inline bool IsValidEndpoint (const Endpoint & endpoint)
{
	auto parsed = MakeEndpoint (endpoint.address, endpoint.port, endpoint.transport);
	return parsed && parsed->address == endpoint.address && parsed->ipv6 == endpoint.ipv6;
}

inline bool IsAsciiAlpha (unsigned char ch)
{
	return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z');
}

inline bool IsAsciiAlnum (unsigned char ch)
{
	return IsAsciiAlpha (ch) || (ch >= '0' && ch <= '9');
}

inline char AsciiLower (unsigned char ch)
{
	return (ch >= 'A' && ch <= 'Z') ? static_cast<char> (ch - 'A' + 'a') : static_cast<char> (ch);
}

inline bool IsSafeIdentifier (std::string_view value)
{
	if (value.empty () || value.size () > 64) return false;
	if (!(IsAsciiAlpha (static_cast<unsigned char> (value.front ())) || value.front () == '_')) return false;
	return std::all_of (value.begin () + 1, value.end (),
		[] (unsigned char ch) { return IsAsciiAlnum (ch) || ch == '_' || ch == '-' || ch == '.'; });
}

/**
	 * nftables chain names are configured by the caller. Reject the standard
	 * inbound/forward hook names so a typo cannot turn an "outbound" command
	 * into an INPUT/FORWARD rule. Custom output chains remain possible.
	 */
inline bool IsSafeOutputChain (std::string_view value)
{
	if (!IsSafeIdentifier (value)) return false;
	std::string lower (value.begin (), value.end ());
	std::transform (lower.begin (), lower.end (), lower.begin (), [] (unsigned char ch) { return AsciiLower (ch); });
	return lower != "input" && lower != "forward" && lower != "prerouting" && lower != "postrouting";
}

/**
	 * Display-name tag for Windows rules (normally a router hash prefix).
	 * Restricted to printable identifier characters so the generated
	 * PowerShell line cannot acquire a newline, a control character or an
	 * unbalanced quote. Empty is allowed; the address/port still identify the
	 * rule.
	 */
inline bool IsSafeRuleTag (std::string_view value)
{
	if (value.size () > 64) return false;
	return std::all_of (value.begin (), value.end (),
		[] (unsigned char ch)
		{
			return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || ch == '+'
				|| ch == '/' || ch == '=' || ch == '_' || ch == '-' || ch == '~' || ch == '.';
		});
}

inline std::string EscapePowerShell (std::string_view value)
{
	std::string escaped;
	escaped.reserve (value.size ());
	for (char ch : value)
	{
		if (ch == '\'')
			escaped += "''";
		else
			escaped.push_back (ch);
	}
	return escaped;
}

inline std::string IptablesLegacy (const Endpoint & endpoint)
{
	// This exact leading-space fragment is the historical -f output.
	return " -A OUTPUT -p " + std::string (L4Protocol (endpoint.transport)) + " -d " + endpoint.address + " --dport "
		+ std::to_string (endpoint.port) + " -j ACCEPT";
}

inline std::string IptablesCommand (const Endpoint & endpoint)
{
	const char * command = endpoint.ipv6 ? "ip6tables" : "iptables";
	return std::string (command) + " -A OUTPUT -p " + L4Protocol (endpoint.transport) + " -d " + endpoint.address
		+ " --dport " + std::to_string (endpoint.port) + " -j ACCEPT";
}

inline std::optional<std::string> Nftables (const Endpoint & endpoint, const RenderOptions & options)
{
	if (!IsSafeIdentifier (options.nftTable) || !IsSafeOutputChain (options.nftChain)
		|| !IsSafeRuleTag (options.ruleTag))
		return std::nullopt;

	const bool useInet = options.nftFamily == "inet";
	const bool useIp = options.nftFamily == "ip";
	const bool useIp6 = options.nftFamily == "ip6";
	if (!useInet && !useIp && !useIp6) return std::nullopt;
	if ((useIp && endpoint.ipv6) || (useIp6 && !endpoint.ipv6)) return std::nullopt;

	const char * family = endpoint.ipv6 ? "ip6" : "ip";
	std::string rule = "nft add rule " + options.nftFamily + " " + options.nftTable + " " + options.nftChain + " "
		+ family + " daddr " + endpoint.address + " " + L4Protocol (endpoint.transport) + " dport "
		+ std::to_string (endpoint.port) + " accept";
	if (!options.ruleTag.empty ())
		rule
			+= " comment \"openi2pd " + std::string (TransportName (endpoint.transport)) + " " + options.ruleTag + "\"";
	return rule;
}

inline std::optional<std::string> Windows (const Endpoint & endpoint, const RenderOptions & options)
{
	if (!IsSafeRuleTag (options.ruleTag)) return std::nullopt;
	const std::string family = endpoint.ipv6 ? "v6" : "v4";
	std::string addressId = endpoint.address;
	std::replace (addressId.begin (), addressId.end (), ':', '_');
	const std::string name = "openi2pd " + std::string (TransportName (endpoint.transport)) + " " + family + " "
		+ options.ruleTag + " " + endpoint.address + ":" + std::to_string (endpoint.port);
	std::string tagId = options.ruleTag;
	std::replace_if (
		tagId.begin (), tagId.end (),
		[] (unsigned char ch) { return !(IsAsciiAlnum (ch) || ch == '_' || ch == '-' || ch == '.'); }, '_');
	const std::string ruleName = "openi2pd_" + std::string (TransportName (endpoint.transport)) + "_" + family + "_"
		+ tagId + "_" + addressId + "_" + std::to_string (endpoint.port);
	return "New-NetFirewallRule -Name '" + EscapePowerShell (ruleName) + "' -DisplayName '" + EscapePowerShell (name)
		+ "' -Group 'openi2pd' -Direction Outbound -Action Allow -Protocol "
		+ (endpoint.transport == Transport::NTCP2 ? "TCP" : "UDP") + " -RemoteAddress '"
		+ EscapePowerShell (endpoint.address) + "' -RemotePort " + std::to_string (endpoint.port)
		+ " -ErrorAction Stop";
}

inline std::optional<std::string> Pf (const Endpoint & endpoint, const RenderOptions & options)
{
	if (!IsSafeRuleTag (options.ruleTag)) return std::nullopt;
	std::string rule = std::string ("pass out quick ") + (endpoint.ipv6 ? "inet6" : "inet") + " proto "
		+ L4Protocol (endpoint.transport) + " to " + endpoint.address + " port " + std::to_string (endpoint.port)
		+ " keep state";
	if (!options.ruleTag.empty ())
		rule += " label \"openi2pd " + std::string (TransportName (endpoint.transport)) + " " + options.ruleTag + "\"";
	return rule;
}

/** True when the backend's configured family deliberately excludes endpoint. */
inline bool IsSelectedByFamily (const Endpoint & endpoint, const RenderOptions & options)
{
	if (options.backend != Backend::Nftables) return true;
	if (options.nftFamily == "ip") return !endpoint.ipv6;
	if (options.nftFamily == "ip6") return endpoint.ipv6;
	return true;
}

inline std::optional<std::string> Render (const Endpoint & endpoint, const RenderOptions & options)
{
	if (!IsValidEndpoint (endpoint)) return std::nullopt;
	switch (options.backend)
	{
	case Backend::IptablesLegacy: return IptablesLegacy (endpoint);
	case Backend::Iptables: return IptablesCommand (endpoint);
	case Backend::Nftables: return Nftables (endpoint, options);
	case Backend::Windows: return Windows (endpoint, options);
	case Backend::Pf: return Pf (endpoint, options);
	default: return std::nullopt;
	}
}

/** Stable deduplication key across multiple RouterInfo files. */
inline std::string EndpointKey (const Endpoint & endpoint)
{
	return std::string (endpoint.ipv6 ? "6|" : "4|") + std::to_string (static_cast<int> (endpoint.transport)) + "|"
		+ endpoint.address + "|" + std::to_string (endpoint.port);
}
}

#endif

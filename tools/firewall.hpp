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

namespace i2p::tools::firewall
{
	enum class Backend
	{
		IptablesLegacy, // -f: preserve the historical rule fragment
		Iptables,       // explicit --firewall=iptables: full commands
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
		return std::all_of (text.begin (), text.end (), [] (unsigned char ch) {
			return (ch >= '0' && ch <= '9') ||
				(ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F') ||
				ch == ':' || ch == '.';
		});
	}

	inline std::optional<Endpoint> MakeEndpoint (std::string_view addressText, int port,
		Transport transport)
	{
		if (port < 1 || port > 65535) return std::nullopt;
		if (addressText.empty () || addressText.size () > 64) return std::nullopt;

		// Keep the grammar numeric. This also rejects IPv6 scope identifiers
		// ("fe80::1%eth0"), bracketed literals and whitespace before the
		// address parser can normalise them into rule text.
		if (!IsNumericAddressText (addressText)) return std::nullopt;

		boost::system::error_code ec;
		auto address = boost::asio::ip::make_address (std::string (addressText), ec);
		if (ec || address.is_unspecified () || address.is_multicast () || address.is_loopback ())
			return std::nullopt;

		// A v4-mapped v6 address would bypass the IPv4 checks above and is not
		// a valid RouterInfo endpoint.
		if (address.is_v6 () && address.to_v6 ().is_v4_mapped ()) return std::nullopt;

		// The limited broadcast address is neither unicast nor useful as a
		// destination; emitting it would create an over-broad rule.
		if (address.is_v4 () && address.to_v4 () == boost::asio::ip::address_v4::broadcast ())
			return std::nullopt;

		return Endpoint{address.to_string (), static_cast<uint16_t> (port), transport,
			address.is_v6 ()};
	}

	inline bool IsSafeIdentifier (std::string_view value)
	{
		if (value.empty () || value.size () > 64) return false;
		if (!(std::isalpha (static_cast<unsigned char> (value.front ())) || value.front () == '_'))
			return false;
		return std::all_of (value.begin () + 1, value.end (), [] (unsigned char ch) {
			return std::isalnum (ch) || ch == '_' || ch == '-' || ch == '.';
		});
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
		return std::all_of (value.begin (), value.end (), [] (unsigned char ch) {
			return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'z') ||
				(ch >= 'A' && ch <= 'Z') || ch == '+' || ch == '/' || ch == '=' ||
				ch == '_' || ch == '-' || ch == '~' || ch == '.';
		});
	}

	inline std::string EscapePowerShell (std::string_view value)
	{
		std::string escaped;
		escaped.reserve (value.size ());
		for (char ch : value)
		{
			if (ch == '\'') escaped += "''";
			else escaped.push_back (ch);
		}
		return escaped;
	}

	inline std::string IptablesLegacy (const Endpoint& endpoint)
	{
		// This exact leading-space fragment is the historical -f output.
		return " -A OUTPUT -p " + std::string (L4Protocol (endpoint.transport)) +
			" -d " + endpoint.address + " --dport " + std::to_string (endpoint.port) +
			" -j ACCEPT";
	}

	inline std::string IptablesCommand (const Endpoint& endpoint)
	{
		const char * command = endpoint.ipv6 ? "ip6tables" : "iptables";
		return std::string (command) + " -A OUTPUT -p " +
			L4Protocol (endpoint.transport) + " -d " + endpoint.address +
			" --dport " + std::to_string (endpoint.port) + " -j ACCEPT";
	}

	inline std::optional<std::string> Nftables (const Endpoint& endpoint,
		const RenderOptions& options)
	{
		if (!IsSafeIdentifier (options.nftTable) || !IsSafeIdentifier (options.nftChain))
			return std::nullopt;

		const bool useInet = options.nftFamily == "inet";
		const bool useIp = options.nftFamily == "ip";
		const bool useIp6 = options.nftFamily == "ip6";
		if (!useInet && !useIp && !useIp6) return std::nullopt;
		if ((useIp && endpoint.ipv6) || (useIp6 && !endpoint.ipv6)) return std::nullopt;

		const char * family = endpoint.ipv6 ? "ip6" : "ip";
		return "nft add rule " + options.nftFamily + " " + options.nftTable + " " +
			options.nftChain + " " + family + " daddr " + endpoint.address + " " +
			L4Protocol (endpoint.transport) + " dport " + std::to_string (endpoint.port) +
			" accept";
	}

	inline std::optional<std::string> Windows (const Endpoint& endpoint,
		const RenderOptions& options)
	{
		if (!IsSafeRuleTag (options.ruleTag)) return std::nullopt;
		const std::string name = "i2pd " + std::string (TransportName (endpoint.transport)) +
			" " + options.ruleTag + " " + endpoint.address + ":" + std::to_string (endpoint.port);
		return "New-NetFirewallRule -DisplayName '" + EscapePowerShell (name) +
			"' -Direction Outbound -Action Allow -Protocol " +
			(endpoint.transport == Transport::NTCP2 ? "TCP" : "UDP") +
			" -RemoteAddress '" + EscapePowerShell (endpoint.address) +
			"' -RemotePort " + std::to_string (endpoint.port);
	}

	inline std::string Pf (const Endpoint& endpoint)
	{
		return std::string ("pass out quick ") + (endpoint.ipv6 ? "inet6" : "inet") +
			" proto " + L4Protocol (endpoint.transport) + " to " + endpoint.address +
			" port " + std::to_string (endpoint.port) + " keep state";
	}

	inline std::optional<std::string> Render (const Endpoint& endpoint,
		const RenderOptions& options)
	{
		switch (options.backend)
		{
			case Backend::IptablesLegacy: return IptablesLegacy (endpoint);
			case Backend::Iptables: return IptablesCommand (endpoint);
			case Backend::Nftables: return Nftables (endpoint, options);
			case Backend::Windows: return Windows (endpoint, options);
			case Backend::Pf: return Pf (endpoint);
			default: return std::nullopt;
		}
	}

	/** Stable deduplication key across multiple RouterInfo files. */
	inline std::string EndpointKey (const Endpoint& endpoint)
	{
		return std::string (endpoint.ipv6 ? "6|" : "4|") +
			std::to_string (static_cast<int> (endpoint.transport)) + "|" +
			endpoint.address + "|" + std::to_string (endpoint.port);
	}
}

#endif

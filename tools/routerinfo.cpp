/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: routerinfo.
 *
 * Reads RouterInfo files and prints their NTCP2/SSU2 addresses, optionally
 * rendered as outbound firewall rules for iptables, nftables, Windows or pf.
 * It deduplicates endpoints, requires public numeric addresses, and only
 * writes text; no firewall is modified.
 *
 */

#include <cstdint>
#include <cstring>
#include "common/getopt.h"
#include <iostream>
#include <memory>
#include <optional>
#include <ostream>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "Crypto.h"
#include "RouterInfo.h"
#include "common/file.hpp"
#include "common/crypto.hpp"
#include "firewall.hpp"

namespace
{
using i2p::tools::firewall::Backend;
using i2p::tools::firewall::Endpoint;
using i2p::tools::firewall::RenderOptions;
using i2p::tools::firewall::Transport;

constexpr size_t MAX_ENDPOINTS = 4096;

struct Options
{
	bool ipv6 = false;
	bool legacyFirewall = false;
	bool printPort = false;
	bool yggdrasil = false;
	bool help = false;
	std::optional<Backend> backend;
	bool nftOptionsGiven = false;
	RenderOptions render;
};

void Usage (const char * program, std::ostream & out)
{
	out << "usage: " << program
		<< " [-6] [-p] [-y] [--firewall=BACKEND] [NFT OPTIONS] routerinfo.dat...\n"
		   "\n"
		   "Print addresses from one or more RouterInfo files.\n"
		   "\n"
		   "  -6                     include IPv6 addresses\n"
		   "  -p                     print ports in address listing (listing only; ignored with a backend)\n"
		   "  -y                     include Yggdrasil addresses\n"
		   "  -f                     print legacy iptables OUTPUT fragments (default backend)\n"
		   "  --firewall=iptables    print full iptables/ip6tables commands\n"
		   "  --firewall=nftables    print nft(8) commands\n"
		   "  --firewall=windows     print PowerShell New-NetFirewallRule commands\n"
		   "  --firewall=pf          print OpenBSD pf.conf rules\n"
		   "  --nft-family=FAMILY    nftables family: inet (default), ip or ip6\n"
		   "  --nft-table=NAME       nftables table (default: filter)\n"
		   "  --nft-chain=NAME       nftables output chain (default: output)\n"
		   "  -h, --help             this help\n"
		   "\n"
		   "All backends can be generated on any platform. The tool only writes\n"
		   "text and never executes or reloads a firewall.\n";
}

std::optional<Backend> ParseBackend (std::string_view name)
{
	if (name == "iptables") return Backend::Iptables;
	if (name == "nftables") return Backend::Nftables;
	if (name == "windows") return Backend::Windows;
	if (name == "pf") return Backend::Pf;
	return std::nullopt;
}

std::string_view AddressStyle (const i2p::data::RouterInfo::Address & address)
{
	switch (address.transportStyle)
	{
	case i2p::data::RouterInfo::eTransportNTCP2: return "NTCP2";
	case i2p::data::RouterInfo::eTransportSSU2: return "SSU2";
	default: return "???";
	}
}

std::optional<Transport> TransportOf (const i2p::data::RouterInfo::Address & address)
{
	switch (address.transportStyle)
	{
	case i2p::data::RouterInfo::eTransportNTCP2: return Transport::NTCP2;
	case i2p::data::RouterInfo::eTransportSSU2: return Transport::SSU2;
	default: return std::nullopt;
	}
}

void AddAddress (std::vector<std::shared_ptr<const i2p::data::RouterInfo::Address>> & addresses,
	const std::shared_ptr<const i2p::data::RouterInfo::Address> & address)
{
	if (address) addresses.push_back (address);
}

bool PrintRules (const std::string & hash,
	const std::vector<std::shared_ptr<const i2p::data::RouterInfo::Address>> & addresses, const Options & options,
	std::set<std::string> & seen, size_t & total, bool & fatal, std::ostream & out)
{
	bool valid = true;
	bool headerPrinted = false;
	fatal = false;
	for (const auto & address : addresses)
	{
		auto transport = TransportOf (*address);
		if (!transport) continue;
		// SSU2 addresses may be introducer-only: the serialized RouterInfo has
		// neither a host nor a direct port for them. They are valid RouterInfo
		// data but cannot produce a direct outbound rule, so skip them instead of
		// treating the whole file as malformed.
		if (address->host.is_unspecified () || address->port <= 0) continue;

		auto endpoint = i2p::tools::firewall::MakeEndpoint (
			i2p::util::net::FormatAddress (address->host), address->port, *transport);
		if (!endpoint)
		{
			std::cerr << "routerinfo: invalid endpoint for " << hash << std::endl;
			valid = false;
			continue;
		}
		RenderOptions render = options.render;
		render.ruleTag = hash.substr (0, 8);
		if (!i2p::tools::firewall::IsSelectedByFamily (*endpoint, render)) continue;
		auto rule = i2p::tools::firewall::Render (*endpoint, render);
		if (!rule)
		{
			std::cerr << "routerinfo: cannot render endpoint " << endpoint->address << ":" << endpoint->port << " for "
					  << hash << std::endl;
			valid = false;
			continue;
		}
		const auto key = i2p::tools::firewall::EndpointKey (*endpoint);
		if (!seen.insert (key).second) continue;
		if (++total > MAX_ENDPOINTS)
		{
			std::cerr << "routerinfo: too many distinct endpoints (max " << MAX_ENDPOINTS << ")" << std::endl;
			fatal = true;
			return false;
		}
		if (!headerPrinted)
		{
			out << "# " << hash << std::endl;
			headerPrinted = true;
		}
		out << *rule << std::endl;
	}
	return valid;
}
}

/// @brief Render RouterInfo addresses or outbound firewall rules.
///
/// Two modes, chosen by `-f`/`--firewall=`: an address listing (default), or
/// firewall text for iptables (legacy fragment or full command), nftables,
/// Windows PowerShell or OpenBSD pf. Nothing is executed; the tool only writes
/// text.
///
/// The whole run is buffered and written to stdout only after every input file
/// has been parsed. That is deliberate: a generator that leaves a partial rule
/// set on stdout after a later file fails can be piped straight into a shell
/// and half-applied. Errors always go to stderr, so `routerinfo ... > rules`
/// yields a file that is complete or empty.
///
/// Option contracts: `-p` affects the listing only and is ignored with a
/// backend; `-y` needs `-6` when generating rules because Yggdrasil is IPv6;
/// nftables options require `--firewall=nftables`; `-f` cannot be combined with
/// an explicit backend. Duplicate endpoints across files are collapsed by
/// family/transport/address/port.
///
/// @return 0 on success (including an empty, already-diagnosed rule set),
///         1 on usage error, unreadable input, invalid endpoint, conflicting
///         options or more than 4096 distinct endpoints.
int main (int argc, char * argv[])
{
	Options options;
	static const option longOptions[]
		= { { "help", no_argument, nullptr, 'h' }, { "firewall", required_argument, nullptr, 'F' },
			  { "nft-family", required_argument, nullptr, 'A' }, { "nft-table", required_argument, nullptr, 'B' },
			  { "nft-chain", required_argument, nullptr, 'C' }, { nullptr, 0, nullptr, 0 } };

	int opt;
	while ((opt = getopt_long (argc, argv, "6fpyh", longOptions, nullptr)) != -1)
	{
		switch (opt)
		{
		case '6': options.ipv6 = true; break;
		case 'f': options.legacyFirewall = true; break;
		case 'p': options.printPort = true; break;
		case 'y': options.yggdrasil = true; break;
		case 'h': options.help = true; break;
		case 'F':
		{
			auto backend = ParseBackend (optarg);
			if (!backend)
			{
				std::cerr << "routerinfo: unknown firewall backend: " << optarg << std::endl;
				return 1;
			}
			if (options.backend && *options.backend != *backend)
			{
				std::cerr << "routerinfo: conflicting firewall backends" << std::endl;
				return 1;
			}
			options.backend = *backend;
			break;
		}
		case 'A':
			options.render.nftFamily = optarg;
			options.nftOptionsGiven = true;
			break;
		case 'B':
			options.render.nftTable = optarg;
			options.nftOptionsGiven = true;
			break;
		case 'C':
			options.render.nftChain = optarg;
			options.nftOptionsGiven = true;
			break;
		default: Usage (argv[0], std::cerr); return 1;
		}
	}

	if (options.help)
	{
		Usage (argv[0], std::cout);
		return 0;
	}
	if (options.legacyFirewall && options.backend)
	{
		std::cerr << "routerinfo: -f cannot be combined with --firewall" << std::endl;
		return 1;
	}
	if (options.legacyFirewall && options.nftOptionsGiven)
	{
		std::cerr << "routerinfo: nftables options cannot be combined with -f" << std::endl;
		return 1;
	}
	if (optind >= argc)
	{
		Usage (argv[0], std::cerr);
		return 1;
	}
	if (options.legacyFirewall) options.backend = Backend::IptablesLegacy;
	if (!options.backend && options.nftOptionsGiven)
	{
		std::cerr << "routerinfo: nftables options require --firewall=nftables" << std::endl;
		return 1;
	}
	if (options.backend)
	{
		options.render.backend = *options.backend;
		if (*options.backend != Backend::Nftables && options.nftOptionsGiven)
		{
			std::cerr << "routerinfo: nftables options require --firewall=nftables" << std::endl;
			return 1;
		}
		if (*options.backend == Backend::Nftables)
		{
			if (options.render.nftFamily != "inet" && options.render.nftFamily != "ip"
				&& options.render.nftFamily != "ip6")
			{
				std::cerr << "routerinfo: nft family must be inet, ip or ip6" << std::endl;
				return 1;
			}
			if (!i2p::tools::firewall::IsSafeIdentifier (options.render.nftTable)
				|| !i2p::tools::firewall::IsSafeOutputChain (options.render.nftChain))
			{
				std::cerr << "routerinfo: invalid nft table or chain name" << std::endl;
				return 1;
			}
			if (options.render.nftFamily == "ip6" && !options.ipv6)
			{
				std::cerr << "routerinfo: --nft-family=ip6 requires -6" << std::endl;
				return 1;
			}
		}
	}
	if (options.yggdrasil && options.backend && !options.ipv6)
	{
		std::cerr << "routerinfo: -y firewall rules require -6" << std::endl;
		return 1;
	}

	i2p::tools::CryptoGuard cryptoGuard;

	std::set<std::string> seen;
	size_t total = 0;
	bool inputError = false;
	// Buffer the whole run. A generator that leaves partial rules on stdout
	// after a later file fails can be piped into a shell and half-applied.
	std::ostringstream output;
	for (int index = optind; index < argc; ++index)
	{
		const std::string filename (argv[index]);
		try
		{
			auto buffer = i2p::tools::ReadFile (filename);
			if (!buffer)
			{
				std::cerr << "routerinfo: " << buffer.error () << std::endl;
				inputError = true;
				continue;
			}
			i2p::data::RouterInfo router (buffer->data (), buffer->size ());
			if (router.IsUnreachable () || !router.GetRouterIdentity ())
			{
				std::cerr << "routerinfo: malformed or invalid RouterInfo: " << filename << std::endl;
				inputError = true;
				continue;
			}
			const auto hash = router.GetIdentHashBase64 ();

			std::vector<std::shared_ptr<const i2p::data::RouterInfo::Address>> addresses;
			AddAddress (addresses, router.GetPublishedNTCP2V4Address ());
			AddAddress (addresses, router.GetSSU2V4Address ());
			if (options.ipv6)
			{
				AddAddress (addresses, router.GetPublishedNTCP2V6Address ());
				AddAddress (addresses, router.GetSSU2V6Address ());
			}
			if (options.yggdrasil) AddAddress (addresses, router.GetYggdrasilAddress ());

			if (options.backend)
			{
				bool fatal = false;
				if (!PrintRules (hash, addresses, options, seen, total, fatal, output))
				{
					inputError = true;
					if (fatal) return 1;
					continue;
				}
			}
			else
			{
				output << "Router Hash: " << hash << std::endl;
				for (const auto & address : addresses)
				{
					output << AddressStyle (*address) << ": " << i2p::util::net::FormatAddress (address->host);
					if (options.printPort) output << ":" << address->port;
					output << std::endl;
				}
			}
		}
		catch (const std::exception & ex)
		{
			std::cerr << "routerinfo: cannot read " << filename << ": " << ex.what () << std::endl;
			inputError = true;
		}
		catch (...)
		{
			std::cerr << "routerinfo: cannot read " << filename << std::endl;
			inputError = true;
		}
	}

	if (inputError) return 1;
	if (options.backend && total == 0)
		std::cerr << "routerinfo: no endpoint matched the selected backend and family" << std::endl;
	std::cout << output.str ();
	return 0;
}

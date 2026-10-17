#include <cassert>
#include <string>

#include "firewall.hpp"

using namespace i2p::tools::firewall;

static Endpoint Make (const char * address, uint16_t port, Transport transport)
{
	auto endpoint = MakeEndpoint (address, port, transport);
	assert (endpoint.has_value ());
	return *endpoint;
}

int main ()
{
	const auto ntcp4 = Make ("192.0.2.10", 12345, Transport::NTCP2);
	const auto ssu4 = Make ("198.51.100.20", 4567, Transport::SSU2);
	const auto ntcp6 = Make ("2001:db8::10", 12345, Transport::NTCP2);
	const auto ssu6 = Make ("2001:db8::20", 4567, Transport::SSU2);

	// Validation rejects values that would create unsafe or meaningless rules.
	assert (!MakeEndpoint ("192.0.2.10", 0, Transport::NTCP2));
	assert (!MakeEndpoint ("192.0.2.10", 65536, Transport::NTCP2));
	assert (!MakeEndpoint ("not-an-ip", 1234, Transport::NTCP2));
	assert (!MakeEndpoint ("1.2.3.4; reboot", 1234, Transport::NTCP2));
	assert (!MakeEndpoint ("1.2.3.4\n-A INPUT -j ACCEPT", 1234, Transport::NTCP2));
	assert (!MakeEndpoint ("0.0.0.0", 1234, Transport::NTCP2));
	assert (!MakeEndpoint ("224.0.0.1", 1234, Transport::NTCP2));
	assert (!MakeEndpoint ("127.0.0.1", 1234, Transport::NTCP2));
	assert (!MakeEndpoint ("::", 1234, Transport::NTCP2));
	assert (!MakeEndpoint ("ff02::1", 1234, Transport::NTCP2));

	RenderOptions options;
	options.ruleTag = "abcdef12";

	// Historical -f output must stay byte-for-byte stable.
	options.backend = Backend::IptablesLegacy;
	assert (Render (ntcp4, options) ==
		" -A OUTPUT -p tcp -d 192.0.2.10 --dport 12345 -j ACCEPT");
	assert (Render (ssu6, options) ==
		" -A OUTPUT -p udp -d 2001:db8::20 --dport 4567 -j ACCEPT");

	options.backend = Backend::Iptables;
	assert (Render (ntcp4, options) ==
		"iptables -A OUTPUT -p tcp -d 192.0.2.10 --dport 12345 -j ACCEPT");
	assert (Render (ntcp6, options) ==
		"ip6tables -A OUTPUT -p tcp -d 2001:db8::10 --dport 12345 -j ACCEPT");
	assert (Render (ssu6, options) ==
		"ip6tables -A OUTPUT -p udp -d 2001:db8::20 --dport 4567 -j ACCEPT");

	options.backend = Backend::Nftables;
	assert (Render (ntcp4, options) ==
		"nft add rule inet filter output ip daddr 192.0.2.10 tcp dport 12345 accept");
	assert (Render (ssu6, options) ==
		"nft add rule inet filter output ip6 daddr 2001:db8::20 udp dport 4567 accept");

	// Family-specific nftables output never silently widens to the other family.
	options.nftFamily = "ip";
	assert (Render (ntcp6, options) == std::nullopt);
	assert (Render (ntcp4, options).has_value ());
	options.nftFamily = "ip6";
	assert (Render (ntcp4, options) == std::nullopt);
	assert (Render (ssu6, options).has_value ());
	options.nftFamily = "inet";

	// Invalid identifiers are rejected instead of being inserted into a command.
	assert (!IsSafeIdentifier (""));
	assert (!IsSafeIdentifier ("-table"));
	assert (!IsSafeIdentifier ("out;flush ruleset"));
	assert (!IsSafeIdentifier ("out put"));
	assert (IsSafeIdentifier ("i2pd_out-1.2"));

	options.nftTable = "bad;name";
	assert (Render (ntcp4, options) == std::nullopt);
	options.nftTable = "filter";

	options.backend = Backend::Windows;
	auto windows = Render (ntcp4, options);
	assert (windows.has_value ());
	assert (windows->find ("New-NetFirewallRule") == 0);
	assert (windows->find ("-Direction Outbound") != std::string::npos);
	assert (windows->find ("-Protocol TCP") != std::string::npos);
	assert (windows->find ("-RemoteAddress '192.0.2.10'") != std::string::npos);
	assert (windows->find ("-RemotePort 12345") != std::string::npos);
	assert (windows->find ("i2pd NTCP2 abcdef12") != std::string::npos);

	options.backend = Backend::Windows;
	auto windows6 = Render (ssu6, options);
	assert (windows6.has_value ());
	assert (windows6->find ("-Protocol UDP") != std::string::npos);
	assert (windows6->find ("-RemoteAddress '2001:db8::20'") != std::string::npos);

	assert (EscapePowerShell ("a'b") == "a''b");

	options.backend = Backend::Pf;
	assert (Render (ntcp4, options) ==
		"pass out quick inet proto tcp to 192.0.2.10 port 12345 keep state");
	assert (Render (ssu6, options) ==
		"pass out quick inet6 proto udp to 2001:db8::20 port 4567 keep state");

	// Deduplication is independent of the rendering backend.
	assert (EndpointKey (ntcp4) == EndpointKey (Make ("192.0.2.10", 12345, Transport::NTCP2)));
	assert (EndpointKey (ntcp4) != EndpointKey (ssu4));

	return 0;
}

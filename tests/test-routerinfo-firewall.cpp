/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: test routerinfo firewall.
 *
 * Validates firewall-rule endpoint parsing and rendering across iptables, nftables, pf and Windows backends, requiring outbound-only rules, canonical numeric addresses and rejection of private, reserved, scoped or injected values.
 *
 */

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

static void AssertSafeRule (const std::optional<std::string> & rule)
{
	assert (rule.has_value ());
	assert (!rule->empty ());
	assert (rule->find ('\n') == std::string::npos);
	assert (rule->find ('\r') == std::string::npos);
	assert (rule->find ("any") == std::string::npos);
	// Every generated rule must be outbound, never an INPUT/inbound rule.
	assert (rule->find ("INPUT") == std::string::npos);
	assert (rule->find ("pass in") == std::string::npos);
	assert (rule->find ("OUTPUT") != std::string::npos || rule->find ("output") != std::string::npos
		|| rule->find ("Outbound") != std::string::npos || rule->find ("pass out") != std::string::npos);
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
	assert (!MakeEndpoint ("192.0.2.10", -1, Transport::NTCP2));
	assert (!MakeEndpoint ("not-an-ip", 1234, Transport::NTCP2));
	assert (!MakeEndpoint ("1.2.3.4; reboot", 1234, Transport::NTCP2));
	assert (!MakeEndpoint ("1.2.3.4\n-A INPUT -j ACCEPT", 1234, Transport::NTCP2));
	assert (!MakeEndpoint ("0.0.0.0", 1234, Transport::NTCP2));
	assert (!MakeEndpoint ("224.0.0.1", 1234, Transport::NTCP2));
	assert (!MakeEndpoint ("127.0.0.1", 1234, Transport::NTCP2));
	assert (!MakeEndpoint ("255.255.255.255", 1234, Transport::NTCP2));
	assert (!MakeEndpoint ("::", 1234, Transport::NTCP2));
	assert (!MakeEndpoint ("ff02::1", 1234, Transport::NTCP2));
	// Scoped, bracketed, mapped/compatible and non-numeric literals never
	// reach a rule. Link-local addresses are not usable I2P endpoints.
	assert (!MakeEndpoint ("fe80::1%eth0", 1234, Transport::NTCP2));
	assert (!MakeEndpoint ("[2001:db8::10]", 1234, Transport::NTCP2));
	assert (!MakeEndpoint ("2001:db8::10 ", 1234, Transport::NTCP2));
	assert (!MakeEndpoint ("2001:db8::10/64", 1234, Transport::NTCP2));
	assert (!MakeEndpoint ("::ffff:192.0.2.10", 1234, Transport::NTCP2));
	assert (!MakeEndpoint ("::ffff:127.0.0.1", 1234, Transport::NTCP2));
	assert (!MakeEndpoint ("::192.0.2.10", 1234, Transport::NTCP2));
	assert (!MakeEndpoint ("fe80::1", 1234, Transport::NTCP2));
	assert (!MakeEndpoint ("febf::1", 1234, Transport::NTCP2));
	assert (!MakeEndpoint ("fc00::1", 1234, Transport::NTCP2));
	assert (!MakeEndpoint ("fec0::1", 1234, Transport::NTCP2));
	assert (!MakeEndpoint ("169.254.1.2", 1234, Transport::NTCP2));
	assert (!MakeEndpoint ("10.0.0.1", 1234, Transport::NTCP2));
	assert (!MakeEndpoint ("172.16.0.1", 1234, Transport::NTCP2));
	assert (!MakeEndpoint ("192.168.1.1", 1234, Transport::NTCP2));
	assert (!MakeEndpoint ("100.64.0.1", 1234, Transport::NTCP2));
	assert (!MakeEndpoint ("0.1.2.3", 1234, Transport::NTCP2));
	assert (!MakeEndpoint ("240.0.0.1", 1234, Transport::NTCP2));
	assert (!MakeEndpoint ("", 1234, Transport::NTCP2));
	assert (!MakeEndpoint (std::string (65, '1'), 1234, Transport::NTCP2));

	assert (!IsNumericAddressText (""));
	assert (!IsNumericAddressText ("host.example"));
	assert (!IsNumericAddressText ("192.0.2.10 "));
	assert (IsNumericAddressText ("192.0.2.10"));
	assert (IsNumericAddressText ("2001:db8::10"));
	assert (IsNumericAddressText ("2001:DB8::10"));

	// Parser output is canonical and port bounds are inclusive.
	const auto canonical = Make ("2001:0DB8:0000::0010", 65535, Transport::NTCP2);
	assert (canonical.address == "2001:db8::10");
	assert (canonical.ipv6);
	assert (canonical.port == 65535);
	assert (Make ("192.0.2.10", 1, Transport::SSU2).port == 1);
	assert (!MakeEndpoint ("192.0.2.10", 1234, static_cast<Transport> (99)));

	RenderOptions options;
	options.ruleTag = "abcdef12";

	// The public aggregate must not bypass validation at the render boundary.
	assert (!IsValidEndpoint ((Endpoint { "192.0.2.10", 0, Transport::NTCP2, false })));
	assert (!IsValidEndpoint ((Endpoint { "192.0.2.10", 1234, static_cast<Transport> (99), false })));
	assert (!IsValidEndpoint ((Endpoint { "192.0.2.10", 1234, Transport::NTCP2, true })));
	assert (!IsValidEndpoint ((Endpoint { "2001:0DB8::10", 1234, Transport::NTCP2, true })));
	assert (IsValidEndpoint (ntcp4));
	{
		Endpoint invalid { "not-an-ip", 1234, Transport::NTCP2, false };
		assert (Render (invalid, options) == std::nullopt);
		Endpoint wrongFamily { "192.0.2.10", 1234, Transport::NTCP2, true };
		assert (Render (wrongFamily, options) == std::nullopt);
	}

	// Preserve historical IPv4 -f output; IPv6 is explicitly routed through
	// ip6tables so the family-specific command is unambiguous.
	options.backend = Backend::IptablesLegacy;
	assert (Render (ntcp4, options) == " -A OUTPUT -p tcp -d 192.0.2.10 --dport 12345 -j ACCEPT");
	assert (Render (ssu4, options) == " -A OUTPUT -p udp -d 198.51.100.20 --dport 4567 -j ACCEPT");
	assert (Render (ntcp6, options) == "ip6tables -A OUTPUT -p tcp -d 2001:db8::10 --dport 12345 -j ACCEPT");
	assert (Render (ssu6, options) == "ip6tables -A OUTPUT -p udp -d 2001:db8::20 --dport 4567 -j ACCEPT");
	AssertSafeRule (Render (ntcp4, options));

	options.backend = Backend::Iptables;
	assert (Render (ntcp4, options) == "iptables -A OUTPUT -p tcp -d 192.0.2.10 --dport 12345 -j ACCEPT");
	assert (Render (ntcp6, options) == "ip6tables -A OUTPUT -p tcp -d 2001:db8::10 --dport 12345 -j ACCEPT");
	assert (Render (ssu4, options) == "iptables -A OUTPUT -p udp -d 198.51.100.20 --dport 4567 -j ACCEPT");
	assert (Render (ssu6, options) == "ip6tables -A OUTPUT -p udp -d 2001:db8::20 --dport 4567 -j ACCEPT");
	AssertSafeRule (Render (ssu4, options));

	options.backend = Backend::Nftables;
	assert (Render (ntcp4, options)
		== "nft add rule inet filter output ip daddr 192.0.2.10 tcp dport 12345 accept comment \"openi2pd NTCP2 "
		   "abcdef12\"");
	assert (Render (ssu6, options)
		== "nft add rule inet filter output ip6 daddr 2001:db8::20 udp dport 4567 accept comment \"openi2pd SSU2 "
		   "abcdef12\"");

	// Family-specific nftables output never silently widens to the other family.
	options.nftFamily = "ip";
	assert (Render (ntcp6, options) == std::nullopt);
	assert (Render (ntcp4, options).has_value ());
	assert (IsSelectedByFamily (ntcp4, options));
	assert (!IsSelectedByFamily (ntcp6, options));
	options.nftFamily = "ip6";
	assert (Render (ntcp4, options) == std::nullopt);
	assert (Render (ssu6, options).has_value ());
	options.nftFamily = "inet";

	// Invalid identifiers are rejected instead of being inserted into a command.
	assert (!IsSafeIdentifier (""));
	assert (!IsSafeIdentifier ("-table"));
	assert (!IsSafeIdentifier ("out;flush ruleset"));
	assert (!IsSafeIdentifier ("out put"));
	assert (!IsSafeIdentifier ("\xC0table"));
	assert (IsSafeIdentifier ("i2pd_out-1.2"));
	assert (IsSafeOutputChain ("output"));
	assert (IsSafeOutputChain ("i2pd_out-1.2"));
	assert (!IsSafeOutputChain ("input"));
	assert (!IsSafeOutputChain ("forward"));
	assert (!IsSafeOutputChain ("INPUT"));
	assert (!IsSafeOutputChain ("out;flush ruleset"));

	options.nftTable = "bad;name";
	assert (Render (ntcp4, options) == std::nullopt);
	options.nftTable = "filter";

	// Unknown families and unsafe chain names are rejected, not emitted.
	options.nftFamily = "inet6";
	assert (Render (ntcp4, options) == std::nullopt);
	options.nftFamily = "inet";
	options.nftChain = "out;flush ruleset";
	assert (Render (ntcp4, options) == std::nullopt);
	options.nftChain = "input";
	assert (Render (ntcp4, options) == std::nullopt);
	options.nftChain = "output";
	options.nftTable = std::string (65, 'a');
	assert (Render (ntcp4, options) == std::nullopt);
	options.nftTable = "filter";

	AssertSafeRule (Render (ntcp4, options));
	AssertSafeRule (Render (ssu6, options));

	options.backend = Backend::Windows;
	auto windows = Render (ntcp4, options);
	assert (windows.has_value ());
	assert (windows->find ("New-NetFirewallRule") == 0);
	assert (windows->find ("-Name 'openi2pd_NTCP2_v4_abcdef12_192.0.2.10_12345'") != std::string::npos);
	assert (windows->find ("-Group 'openi2pd'") != std::string::npos);
	assert (windows->find ("-Direction Outbound") != std::string::npos);
	assert (windows->find ("-Protocol TCP") != std::string::npos);
	assert (windows->find ("-ErrorAction Stop") != std::string::npos);
	assert (windows->find ("-RemoteAddress '192.0.2.10'") != std::string::npos);
	assert (windows->find ("-RemotePort 12345") != std::string::npos);
	assert (windows->find ("openi2pd NTCP2 v4 abcdef12") != std::string::npos);
	AssertSafeRule (windows);

	options.backend = Backend::Windows;
	auto windows6 = Render (ssu6, options);
	assert (windows6.has_value ());
	assert (windows6->find ("-Protocol UDP") != std::string::npos);
	assert (windows6->find ("-RemoteAddress '2001:db8::20'") != std::string::npos);
	assert (windows6->find ("openi2pd SSU2 v6 abcdef12") != std::string::npos);
	assert (windows6->find ("-Name 'openi2pd_SSU2_v6_abcdef12_2001_db8__20_4567'") != std::string::npos);
	AssertSafeRule (windows6);

	// Display-name tags accept base64/identifier characters and are rejected
	// rather than emitted with quotes, newlines or control characters.
	assert (IsSafeRuleTag (""));
	assert (IsSafeRuleTag ("abcdef12"));
	assert (IsSafeRuleTag ("abc+/=~-._"));
	assert (!IsSafeRuleTag ("bad'tag"));
	assert (!IsSafeRuleTag ("bad\ntag"));
	assert (!IsSafeRuleTag ("bad tag"));
	assert (!IsSafeRuleTag (std::string (65, 'a')));
	options.ruleTag = "abc+/=~-._";
	assert (Render (ntcp4, options).has_value ());
	options.ruleTag = "bad\ntag";
	assert (Render (ntcp4, options) == std::nullopt);
	options.ruleTag = "bad'tag";
	assert (Render (ntcp4, options) == std::nullopt);
	options.backend = Backend::Nftables;
	assert (Render (ntcp4, options) == std::nullopt);
	options.backend = Backend::Windows;
	options.ruleTag = "abcdef12";

	assert (EscapePowerShell ("a'b") == "a''b");

	options.backend = Backend::Pf;
	assert (Render (ntcp4, options)
		== "pass out quick inet proto tcp to 192.0.2.10 port 12345 keep state label \"openi2pd NTCP2 abcdef12\"");
	assert (Render (ssu6, options)
		== "pass out quick inet6 proto udp to 2001:db8::20 port 4567 keep state label \"openi2pd SSU2 abcdef12\"");
	AssertSafeRule (Render (ntcp4, options));

	// Deduplication is independent of the rendering backend.
	assert (EndpointKey (ntcp4) == EndpointKey (Make ("192.0.2.10", 12345, Transport::NTCP2)));
	assert (EndpointKey (ntcp4) != EndpointKey (Make ("192.0.2.10", 12345, Transport::SSU2)));
	assert (EndpointKey (ntcp4) != EndpointKey (Make ("192.0.2.10", 12346, Transport::NTCP2)));

	return 0;
}

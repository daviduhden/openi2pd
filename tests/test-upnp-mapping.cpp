/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: test upnp mapping.
 *
 * Compile-free regression for the UPnP mapping policy helpers: the
 * lease duration handed to the IGD, the same-host IGD refusal and the
 * ownership test used before a superseded mapping is replaced. No IGD,
 * network or MiniUPnPc call is made.
 *
 */

#include <iostream>
#include <string>

#include "../daemon/UPnP.h"

namespace
{
int failures = 0;

void Check (bool condition, const std::string & what)
{
	if (condition) return;
	std::cerr << "FAIL: " << what << std::endl;
	++failures;
}
}

int main ()
{
	using namespace i2p::transport;

	// --- lease duration
	// ---------------------------------------------------
	{
		Check (UPNP_LEASE_DURATION_SECONDS == 3 * 60 * 60, "default lease is three hours");
		Check (FormatUPnPLeaseDuration () == "10800", "default lease is 10800 seconds");
		Check (FormatUPnPLeaseDuration (60) == "60", "explicit lease is honoured");
		Check (FormatUPnPLeaseDuration (0) == "10800", "zero falls back to the default lease");
		Check (FormatUPnPLeaseDuration (UPNP_MAX_LEASE_DURATION_SECONDS + 1000)
				== std::to_string (UPNP_MAX_LEASE_DURATION_SECONDS),
			"lease above the maximum is clamped");
	}

	// --- same-host IGD
	// ----------------------------------------------------
	{
		Check (IsSameHostIGD ("", "192.168.1.10"), "empty external address is refused");
		Check (IsSameHostIGD ("0.0.0.0", "192.168.1.10"), "0.0.0.0 is refused");
		Check (IsSameHostIGD ("127.0.0.1", "192.168.1.10"), "loopback is refused");
		Check (IsSameHostIGD ("127.10.0.2", "192.168.1.10"), "loopback range is refused");
		Check (IsSameHostIGD ("::1", "192.168.1.10"), "IPv6 loopback is refused");
		Check (IsSameHostIGD ("::", "192.168.1.10"), "unspecified IPv6 is refused");
		Check (IsSameHostIGD ("192.168.1.10", "192.168.1.10"), "external equal to the local address is refused");
		Check (IsSameHostIGD (" 192.168.1.10 ", "\t192.168.1.10"), "whitespace does not hide a same-host IGD");
		Check (!IsSameHostIGD ("203.0.113.7", "192.168.1.10"), "an ordinary IGD is accepted");
		Check (!IsSameHostIGD ("2001:db8::1", "192.168.1.10"), "an IPv6 IGD is accepted");
		Check (!IsSameHostIGD ("203.0.113.7", ""), "a missing local address does not refuse an IGD");
	}

	// --- existing mapping ownership
	// ---------------------------------------
	{
		Check (IsOwnUPnPMapping ("192.168.1.10", "28080", "192.168.1.10", "28080"),
			"identical mapping is renewable in place");
		Check (!IsOwnUPnPMapping ("192.168.1.11", "28080", "192.168.1.10", "28080"),
			"mapping owned by another host is not ours");
		Check (!IsOwnUPnPMapping ("192.168.1.10", "28081", "192.168.1.10", "28080"),
			"mapping for another internal port is not ours");
		Check (!IsOwnUPnPMapping ("", "", "192.168.1.10", "28080"), "empty mapping is not ours");
		Check (!IsOwnUPnPMapping ("192.168.1.10", "28080", "", ""), "missing local target is not ours");
	}

	// --- status shape
	// -----------------------------------------------------
	{
		const UPnPStatus status;
		Check (!status.enabled && !status.mapped && status.lastError == 0, "status defaults are inert");
	}

	if (failures)
	{
		std::cerr << failures << " UPnP mapping checks failed" << std::endl;
		return 1;
	}
	std::cout << "UPnP mapping checks passed" << std::endl;
	return 0;
}

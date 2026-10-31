/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: server-tunnel option and lifecycle regression.
 *
 * Covers the pure target/policy helpers of a server tunnel:
 * `targetForPort` parsing and selection, the 443/22 reset rule,
 * `spoofedHost` fallback, the inproxy/referer/user-agent rejects, the
 * bounded POST window, `limitAction` mapping, the administrative name
 * helpers and Start/Stop idempotence. No socket, DNS lookup or timer is
 * used.
 *
 */

#include <cassert>
#include <string>

#include <boost/asio.hpp>

#include "ClientContext.h"
#include "Destination.h"
#include "I2PTunnel.h"
#include "Identity.h"

namespace
{
using i2p::client::I2PServerTunnel;
using i2p::client::I2PServerTunnelHTTPPolicy;
using i2p::client::I2PServerTunnelRequestDecision;
using i2p::client::I2PServerTunnelTargetMap;

struct Fixture
{
	boost::asio::io_context service;
	std::shared_ptr<i2p::client::ClientDestination> destination;

	Fixture ()
	{
		i2p::util::Mapping params;
		auto keys = i2p::data::PrivateKeys::CreateRandomKeys (
			i2p::data::SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519, i2p::data::CRYPTO_KEY_TYPE_ECIES_X25519_AEAD, true);
		destination = std::make_shared<i2p::client::ClientDestination> (service, keys, false, &params);
	}
};

const std::string BasicPost = "POST /upload HTTP/1.1\r\nHost: peer\r\nContent-Length: 0\r\n\r\n";
const std::string BasicGet = "GET / HTTP/1.1\r\nHost: peer\r\n\r\n";
} // namespace

int main ()
{
	// --- targetForPort parsing and selection
	// ------------------------------
	{
		I2PServerTunnelTargetMap targets;
		std::string error;
		assert (targets.AddSpec (80, "127.0.0.1:8080", error));
		assert (targets.AddSpec (443, "[::1]:8443", error));
		assert (targets.Size () == 2);
		assert (targets.Entries ().at (80).host == "127.0.0.1");
		assert (targets.Entries ().at (80).port == 8080);
		assert (targets.Entries ().at (443).host == "::1");

		// Malformed values and out-of-range ports are rejected.
		assert (!targets.AddSpec (81, "127.0.0.1", error) && !error.empty ());
		assert (!targets.AddSpec (81, "127.0.0.1:0", error));
		assert (!targets.AddSpec (81, "127.0.0.1:70000", error));
		assert (!targets.AddSpec (0, "127.0.0.1:80", error));

		// Selection: mapped, reserved without a mapping and plain default.
		auto selection = targets.Select (80);
		assert (selection.status == I2PServerTunnelTargetMap::Selection::Status::eTarget);
		assert (selection.port == 8080);
		selection = targets.Select (443);
		assert (selection.status == I2PServerTunnelTargetMap::Selection::Status::eTarget);
		selection = targets.Select (22); // reserved, no mapping
		assert (selection.status == I2PServerTunnelTargetMap::Selection::Status::eReset);
		selection = targets.Select (8000);
		assert (selection.status == I2PServerTunnelTargetMap::Selection::Status::eDefault);

		// The map is bounded.
		I2PServerTunnelTargetMap bounded;
		for (uint16_t port = 1; port <= i2p::client::I2P_SERVER_TUNNEL_MAX_PORT_TARGETS; ++port)
			assert (bounded.Add (static_cast<uint16_t> (port + 1000), "127.0.0.1", port, error));
		assert (!bounded.Add (9999, "127.0.0.1", 1, error));
		assert (bounded.Size () == i2p::client::I2P_SERVER_TUNNEL_MAX_PORT_TARGETS);
	}

	// --- target selection through a tunnel
	// --------------------------------
	Fixture fixture;
	auto tunnel = std::make_shared<I2PServerTunnel> ("targets", "127.0.0.1", 8080, fixture.destination, 0, false);
	tunnel->Start ();
	assert (tunnel->IsRunning ());
	{
		// The configured address is an IP literal, so Start() needs no DNS.
		boost::asio::ip::tcp::endpoint target;
		assert (tunnel->SelectTarget (8000, target)); // default target
		assert (target.port () == 8080);
		assert (!tunnel->SelectTarget (443, target)); // reserved without a mapping
		assert (!tunnel->SelectTarget (22, target));

		I2PServerTunnelTargetMap targets;
		std::string error;
		assert (targets.AddSpec (443, "127.0.0.1:8443", error));
		assert (targets.AddSpec (80, "localhost:8081", error)); // hostname: async, never blocks
		tunnel->SetTargets (targets);
		assert (tunnel->GetTargets ().Size () == 2);
	}

	// --- spoofedHost and hostoverride fallback
	// ----------------------------
	{
		I2PServerTunnelHTTPPolicy policy;
		policy.SetHostOverride ("override.example");
		std::string error;
		assert (policy.SetSpoofedHost (443, "secure.example", error));
		assert (policy.HostFor (443) == "secure.example");
		assert (policy.HostFor (80) == "override.example");
		assert (!policy.SetSpoofedHost (443, "bad host", error)); // space/CR/LF rejected
		assert (!policy.SetSpoofedHost (0, "zero.example", error));
		assert (policy.HostFor (443) == "secure.example"); // unchanged on failure
	}

	// --- request rejects
	// ---------------------------------------------------
	{
		I2PServerTunnelHTTPPolicy policy;
		policy.SetRejectInproxy (true);
		assert (policy.Check ("GET / HTTP/1.1\r\nX-Forwarded-For: 10.0.0.1\r\n\r\n", 100)
			== I2PServerTunnelRequestDecision::eForbidden);
		assert (policy.Check ("GET / HTTP/1.1\r\nvIa: 1.1 proxy\r\n\r\n", 100)
			== I2PServerTunnelRequestDecision::eForbidden);
		assert (policy.Check ("GET / HTTP/1.1\r\nForwarded: for=1.2.3.4\r\n\r\n", 100)
			== I2PServerTunnelRequestDecision::eForbidden);
		assert (policy.Check (BasicGet, 100) == I2PServerTunnelRequestDecision::eAllow);
	}
	{
		I2PServerTunnelHTTPPolicy policy;
		policy.SetRejectReferer (true);
		assert (policy.Check ("GET / HTTP/1.1\r\nReferer: http://example/\r\n\r\n", 100)
			== I2PServerTunnelRequestDecision::eForbidden);
		assert (policy.Check (BasicGet, 100) == I2PServerTunnelRequestDecision::eAllow);
	}
	{
		I2PServerTunnelHTTPPolicy policy;
		std::string error;
		assert (policy.SetRejectUserAgents (" BadBot , evil-Crawler ", error));
		assert (policy.GetNumRejectUserAgents () == 2);
		assert (policy.Check ("GET / HTTP/1.1\r\nUser-Agent: Mozilla BadBot/1.0\r\n\r\n", 100)
			== I2PServerTunnelRequestDecision::eForbidden);
		assert (policy.Check ("GET / HTTP/1.1\r\nuser-agent: EVIL-CRAWLER\r\n\r\n", 100)
			== I2PServerTunnelRequestDecision::eForbidden);
		assert (policy.Check ("GET / HTTP/1.1\r\nUser-Agent: Mozilla/5.0\r\n\r\n", 100)
			== I2PServerTunnelRequestDecision::eAllow);
		assert (!policy.SetRejectUserAgents (std::string (257, 'a'), error));
	}

	// --- bounded POST/PUT window
	// ------------------------------------------
	{
		I2PServerTunnelHTTPPolicy policy;
		policy.SetPostLimit (2);
		assert (policy.GetPostLimit () == 2);
		const uint64_t now = 5000;
		assert (policy.Check (BasicPost, now) == I2PServerTunnelRequestDecision::eAllow);
		assert (policy.Check (BasicPost, now + 1) == I2PServerTunnelRequestDecision::eAllow);
		assert (policy.Check (BasicPost, now + 2) == I2PServerTunnelRequestDecision::eTooManyRequests);
		// GET requests never consume the POST budget.
		assert (policy.Check (BasicGet, now + 3) == I2PServerTunnelRequestDecision::eAllow);
		// The next window resets the counter.
		assert (policy.Check (BasicPost, now + i2p::client::I2P_SERVER_TUNNEL_POST_WINDOW)
			== I2PServerTunnelRequestDecision::eAllow);

		policy.SetPostLimit (0); // disabled
		for (int i = 0; i < 100; ++i)
			assert (
				policy.Check (BasicPost, 9000 + static_cast<uint64_t> (i)) == I2PServerTunnelRequestDecision::eAllow);
	}

	// --- limitAction mapping
	// ----------------------------------------------
	{
		auto denyAll = i2p::client::FilterDefinition::DenyAll ();
		tunnel->SetAccessFilter (denyAll, ""); // default: filter denials drop
		assert (tunnel->GetAccessRejectAction () == i2p::client::TunnelAccessRejectAction::eDrop);
		tunnel->SetLimitAction ("reset");
		assert (tunnel->GetAccessRejectAction () == i2p::client::TunnelAccessRejectAction::eReset);
		tunnel->SetLimitAction ("drop");
		assert (tunnel->GetAccessRejectAction () == i2p::client::TunnelAccessRejectAction::eDrop);
		tunnel->SetLimitAction ("bogus"); // unsupported value falls back
		assert (tunnel->GetAccessRejectAction () == i2p::client::TunnelAccessRejectAction::eDrop);
	}

	// --- administrative name helpers
	// --------------------------------------
	{
		assert (i2p::client::MakeTunnelServiceName ("admin", "section", "client", "127.0.0.1:80") == "admin");
		assert (i2p::client::MakeTunnelServiceName ("", "section", "client", "127.0.0.1:80") == "section");
		assert (i2p::client::MakeTunnelServiceName ("", "", "server", "peer:8080") == "server://peer:8080");

		assert (i2p::client::IsValidTunnelServiceName ("tunnel one"));
		assert (!i2p::client::IsValidTunnelServiceName (""));
		assert (!i2p::client::IsValidTunnelServiceName (std::string (65, 'a')));
		assert (!i2p::client::IsValidTunnelServiceName ("bad\nname"));

		assert (i2p::client::ShouldStartTunnelOnLoad (true, true));
		assert (!i2p::client::ShouldStartTunnelOnLoad (false, true));
		assert (!i2p::client::ShouldStartTunnelOnLoad (true, false));

		i2p::client::TunnelServiceNameRegistry registry;
		assert (registry.Register ("first"));
		assert (!registry.Register ("first")); // duplicate is a config error
		assert (!registry.Register (""));
		assert (registry.Size () == 1);
		assert (registry.Contains ("first"));
		registry.Clear ();
		assert (registry.Size () == 0);
		assert (registry.Register ("first"));
	}

	// --- Start/Stop idempotence and the running flag
	// ----------------------
	{
		assert (tunnel->IsRunning ());
		tunnel->Start (); // idempotent: no second acceptor failure
		assert (tunnel->IsRunning ());

		// A restart resolves the configured targetForPort literals again; the
		// hostname entry stays unresolved because the io_context never runs,
		// so its port is refused instead of blocking the service thread.
		boost::asio::ip::tcp::endpoint target;
		assert (tunnel->SelectTarget (443, target) && target.port () == 8443);
		assert (!tunnel->SelectTarget (80, target));

		tunnel->Stop ();
		assert (!tunnel->IsRunning ());
		tunnel->Stop (); // idempotent
		assert (!tunnel->IsRunning ());
		tunnel->Start ();
		assert (tunnel->IsRunning ());
		tunnel->Stop (true);
		assert (!tunnel->IsRunning ());
	}

	return 0;
}

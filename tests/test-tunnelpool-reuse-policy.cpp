/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: test tunnel peer reuse policy and far-end masks.
 *
 * Recreating a tunnel reused the old peers without re-applying the blocklist,
 * runtime bans, reachability, version, congestion or endpoint rules, and the
 * fallback builders passed a zero far-end transport mask. This test pins the
 * shared admission predicate, the "never reuse a failed path" decision and the
 * far-end mask of the one-hop configurations. Deterministic: RouterInfos are
 * built in memory, no netdb lookup, socket or thread runs.
 *
 */

#include <cassert>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#include "Blocklist.h"
#include "Identity.h"
#include "I2PEndian.h"
#include "RouterContext.h"
#include "RouterInfo.h"
#include "Tunnel.h"
#include "TunnelPool.h"

namespace i2p
{
namespace tunnel
{
	struct TunnelPoolTestAccess
	{
		static bool CanReusePeers (const TunnelPool & pool, const Tunnel & tunnel)
		{
			return pool.CanReuseTunnelPeers (tunnel);
		}
	};
}
}

namespace
{
using namespace i2p;
using namespace i2p::data;
using namespace i2p::tunnel;

void AppendBe16 (std::vector<uint8_t> & out, uint16_t value)
{
	uint8_t encoded[2];
	htobe16buf (encoded, value);
	out.insert (out.end (), encoded, encoded + sizeof (encoded));
}

void AppendBe64 (std::vector<uint8_t> & out, uint64_t value)
{
	uint8_t encoded[8];
	htobe64buf (encoded, value);
	out.insert (out.end (), encoded, encoded + sizeof (encoded));
}

void AppendString (std::vector<uint8_t> & out, std::string_view value)
{
	out.push_back (static_cast<uint8_t> (value.size ()));
	out.insert (out.end (), value.begin (), value.end ());
}

void AppendParam (std::string & out, std::string_view key, std::string_view value)
{
	out.push_back (static_cast<char> (key.size ()));
	out.append (key);
	out.push_back ('=');
	out.push_back (static_cast<char> (value.size ()));
	out.append (value);
	out.push_back (';');
}

std::vector<uint8_t> BuildRouterInfo (const PrivateKeys & keys, const std::string & version,
	const std::string & caps, bool withAddress)
{
	std::vector<uint8_t> body;
	AppendBe64 (body, 1700000000000ULL);
	body.push_back (withAddress ? 1 : 0);
	if (withAddress)
	{
		body.push_back (8); // cost
		AppendBe64 (body, 0); // date
		AppendString (body, "SSU2");
		std::string options;
		AppendParam (options, "host", "8.8.8.8");
		AppendParam (options, "i", ByteStreamToBase64 (std::vector<uint8_t> (32, 0x22).data (), 32));
		AppendParam (options, "port", "12345");
		AppendParam (options, "s", ByteStreamToBase64 (std::vector<uint8_t> (32, 0x11).data (), 32));
		AppendParam (options, "v", "2");
		AppendBe16 (body, static_cast<uint16_t> (options.size ()));
		body.insert (body.end (), options.begin (), options.end ());
	}
	body.push_back (0); // peers

	std::string properties;
	AppendParam (properties, "caps", caps);
	AppendParam (properties, "netId", std::to_string (i2p::context.GetNetID ()));
	AppendParam (properties, "router.version", version);
	AppendBe16 (body, static_cast<uint16_t> (properties.size ()));
	body.insert (body.end (), properties.begin (), properties.end ());

	std::vector<uint8_t> buffer;
	auto identity = keys.GetPublic ();
	assert (identity);
	buffer.resize (identity->GetFullLen ());
	assert (identity->ToBuffer (buffer.data (), buffer.size ()) == buffer.size ());
	buffer.insert (buffer.end (), body.begin (), body.end ());
	std::vector<uint8_t> signature (identity->GetSignatureLen ());
	keys.Sign (buffer.data (), static_cast<int> (buffer.size ()), signature.data ());
	buffer.insert (buffer.end (), signature.begin (), signature.end ());
	return buffer;
}

std::shared_ptr<RouterInfo> MakeRouter (const PrivateKeys & keys, const std::string & version = "2.14.0",
	const std::string & caps = "O", bool withAddress = true)
{
	auto buffer = BuildRouterInfo (keys, version, caps, withAddress);
	auto router = std::make_shared<RouterInfo> (buffer.data (), buffer.size ());
	assert (!router->GetIdentHash ().IsZero ());
	return router;
}

PrivateKeys MakeEciesKeys ()
{
	return PrivateKeys::CreateRandomKeys (SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519, CRYPTO_KEY_TYPE_ECIES_X25519_AEAD);
}

void TestAdmissionPredicate ()
{
	auto keys = MakeEciesKeys ();
	auto router = MakeRouter (keys);
	assert (router->IsECIES ());
	assert (router->GetCompatibleTransports (true) != 0);
	assert (router->GetCompatibleTransports (false) != 0);
	// An unknown local router (no identity in the test context) skips the
	// peer-to-peer reachability checks instead of guessing them.
	assert (IsRouterUsableForTunnel (*router, nullptr, TunnelPeerRole::Middle, true, false));
	assert (IsRouterUsableForTunnel (*router, nullptr, TunnelPeerRole::Endpoint, false, false));
	assert (IsRouterUsableForTunnel (*router, nullptr, TunnelPeerRole::Gateway, true, false));

	// Runtime bans apply to a reused identity that arrived before the ban.
	assert (AddRuntimeRouterBan (router->GetIdentHash ()));
	assert (!IsRouterUsableForTunnel (*router, nullptr, TunnelPeerRole::Middle, true, false));
	assert (RemoveRuntimeRouterBan (router->GetIdentHash ()));
	assert (IsRouterUsableForTunnel (*router, nullptr, TunnelPeerRole::Middle, true, false));

	// A descriptor without a usable transport can never carry a tunnel hop.
	auto unreachable = MakeRouter (MakeEciesKeys (), "2.14.0", "O", false);
	assert (!unreachable->HasValidAddresses ());
	assert (!IsRouterUsableForTunnel (*unreachable, nullptr, TunnelPeerRole::Middle, true, false));

	// Old routers: the general floor is waived only for a high-bandwidth peer.
	auto old = MakeRouter (MakeEciesKeys (), "0.9.1");
	assert (!old->IsHighBandwidth ());
	assert (!IsRouterUsableForTunnel (*old, nullptr, TunnelPeerRole::Middle, true, false));

	// ElGamal-only identities cannot be tunnel hops in an ECIES network.
	auto elgamalKeys
		= PrivateKeys::CreateRandomKeys (SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519, CRYPTO_KEY_TYPE_ELGAMAL);
	auto elgamal = MakeRouter (elgamalKeys);
	assert (!elgamal->IsECIES ());
	assert (!IsRouterUsableForTunnel (*elgamal, nullptr, TunnelPeerRole::Middle, true, false));

	// A peer that is not IPv4 cannot be an endpoint.
	auto dualKeys = MakeEciesKeys ();
	auto endpoint = MakeRouter (dualKeys, "2.14.0", "O", true);
	assert (IsRouterUsableForTunnel (*endpoint, nullptr, TunnelPeerRole::Endpoint, false, false));
}

void TestRoleAssignment ()
{
	assert (GetTunnelPeerRole (0, 3, true) == TunnelPeerRole::Gateway);
	assert (GetTunnelPeerRole (0, 3, false) == TunnelPeerRole::Middle);
	assert (GetTunnelPeerRole (1, 3, false) == TunnelPeerRole::Middle);
	assert (GetTunnelPeerRole (2, 3, false) == TunnelPeerRole::Endpoint);
	// The last hop of an inbound path is requested as an endpoint by NetDb too.
	assert (GetTunnelPeerRole (2, 3, true) == TunnelPeerRole::Endpoint);
	// A single-hop inbound tunnel is its own gateway.
	assert (GetTunnelPeerRole (0, 1, true) == TunnelPeerRole::Gateway);
	// A single-hop outbound tunnel is the OBEP.
	assert (GetTunnelPeerRole (0, 1, false) == TunnelPeerRole::Endpoint);
}

void TestFarEndTransports ()
{
	auto router = MakeRouter (MakeEciesKeys ());
	const auto inboundMask = router->GetCompatibleTransports (true);
	const auto outboundMask = router->GetCompatibleTransports (false);
	assert (inboundMask != 0);
	assert (outboundMask != 0);

	// The fallback builders must pass the peer's real transport set, not the
	// zero mask a `false` boolean argument used to produce.
	auto inboundConfig = i2p::tunnel::CreateOneHopInboundTunnelConfig (*router);
	assert (inboundConfig);
	assert (inboundConfig->GetNumHops () == 1);
	assert (inboundConfig->IsInbound ());
	assert (inboundConfig->GetFarEndTransports () == inboundMask);
	assert (inboundConfig->GetFarEndTransports () != 0);

	IdentHash replyIdent;
	replyIdent.Fill (0x42);
	auto outboundConfig = i2p::tunnel::CreateOneHopOutboundTunnelConfig (*router, 0x12345678, replyIdent);
	assert (outboundConfig);
	assert (outboundConfig->GetNumHops () == 1);
	assert (outboundConfig->GetFarEndTransports () == outboundMask);
	assert (outboundConfig->GetFarEndTransports () != 0);
	assert (!outboundConfig->IsInbound ());
}

void TestNoReuseOfFailedPath ()
{
	std::vector<std::shared_ptr<const IdentityEx>> peers {
		MakeRouter (MakeEciesKeys ())->GetRouterIdentity (), MakeRouter (MakeEciesKeys ())->GetRouterIdentity ()
	};
	auto config = std::make_shared<i2p::tunnel::TunnelConfig> (peers, 1, IdentHash { });
	auto tunnel = std::make_shared<i2p::tunnel::OutboundTunnel> (config);
	auto pool = std::make_shared<i2p::tunnel::TunnelPool> (2, 2, 1, 1, 0, 0, false);
	using Access = i2p::tunnel::TunnelPoolTestAccess;
	assert (Access::CanReusePeers (*pool, *tunnel));

	tunnel->SetState (i2p::tunnel::eTunnelStateFailed);
	assert (!Access::CanReusePeers (*pool, *tunnel));

	tunnel->SetState (i2p::tunnel::eTunnelStateEstablished);
	assert (Access::CanReusePeers (*pool, *tunnel));
	// A tunnel suspected by a failed test must not have its path rebuilt.
	tunnel->AddTestStrike ();
	assert (!Access::CanReusePeers (*pool, *tunnel));
	tunnel->ResetTestStrikes ();
	assert (Access::CanReusePeers (*pool, *tunnel));

	i2p::tunnel::tunnels.DeleteTunnelPool (pool);
}
}

int main ()
{
	TestAdmissionPredicate ();
	TestRoleAssignment ();
	TestFarEndTransports ();
	TestNoReuseOfFailedPath ();
	return 0;
}

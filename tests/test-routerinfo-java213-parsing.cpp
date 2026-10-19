/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: test routerinfo java213 parsing.
 *
 * Checks Java 2.13 RouterInfo/RouterAddress parsing: address and property limits, duplicate rejection, NTCP2 and SSU2 option rules, bandwidth tiers, hidden certificates and every truncation point.
 *
 */

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "Base.h"
#include "I2PEndian.h"
#include "Identity.h"
#include "RouterContext.h"
#include "RouterInfo.h"

namespace
{
using namespace i2p::data;

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
	assert (value.size () <= 255);
	out.push_back (static_cast<uint8_t> (value.size ()));
	out.insert (out.end (), value.begin (), value.end ());
}

void AppendParam (std::string & out, std::string_view key, std::string_view value)
{
	assert (key.size () <= 255 && value.size () <= 255);
	out.push_back (static_cast<char> (key.size ()));
	out.append (key);
	out.push_back ('=');
	out.push_back (static_cast<char> (value.size ()));
	out.append (value);
	out.push_back (';');
}

using Params = std::vector<std::pair<std::string, std::string>>;

struct AddressSpec
{
	std::string style = "SSU2";
	uint8_t cost = 0;
	uint64_t date = 0;
	Params options;
	bool duplicateFirstOption = false;
	uint16_t sizeOverride = 0;
};

struct RouterSpec
{
	uint64_t timestamp = 1700000000000ULL;
	std::vector<AddressSpec> addresses;
	Params properties;
	uint8_t peerCount = 0;
	uint8_t addressCountOverride = 0;
	bool hasAddressCountOverride = false;
	bool trailingByte = false;
};

std::string EncodeParams (const Params & params, bool duplicateFirst = false)
{
	std::string out;
	for (const auto & [key, value] : params) AppendParam (out, key, value);
	if (duplicateFirst && !params.empty ()) AppendParam (out, params.front ().first, params.front ().second);
	return out;
}

std::vector<uint8_t> BuildBody (const RouterSpec & spec)
{
	std::vector<uint8_t> body;
	AppendBe64 (body, spec.timestamp);
	if (spec.hasAddressCountOverride)
		body.push_back (spec.addressCountOverride);
	else
		body.push_back (static_cast<uint8_t> (spec.addresses.size ()));
	for (const auto & address : spec.addresses)
	{
		body.push_back (address.cost);
		AppendBe64 (body, address.date);
		AppendString (body, address.style);
		const auto options = EncodeParams (address.options, address.duplicateFirstOption);
		AppendBe16 (body, address.sizeOverride ? address.sizeOverride : static_cast<uint16_t> (options.size ()));
		body.insert (body.end (), options.begin (), options.end ());
	}
	body.push_back (spec.peerCount);
	const auto properties = EncodeParams (spec.properties);
	AppendBe16 (body, static_cast<uint16_t> (properties.size ()));
	body.insert (body.end (), properties.begin (), properties.end ());
	if (spec.trailingByte) body.push_back (0);
	return body;
}

std::vector<uint8_t> IdentityBuffer (const PrivateKeys & keys)
{
	auto identity = keys.GetPublic ();
	assert (identity);
	std::vector<uint8_t> buffer (identity->GetFullLen ());
	assert (identity->ToBuffer (buffer.data (), buffer.size ()) == buffer.size ());
	return buffer;
}

std::vector<uint8_t> SignRouterInfo (const PrivateKeys & keys, const std::vector<uint8_t> & body,
	const std::vector<uint8_t> * identityOverride = nullptr)
{
	const auto identity = identityOverride ? *identityOverride : IdentityBuffer (keys);
	std::vector<uint8_t> buffer = identity;
	buffer.insert (buffer.end (), body.begin (), body.end ());
	std::vector<uint8_t> signature (keys.GetPublic ()->GetSignatureLen ());
	keys.Sign (buffer.data (), static_cast<int> (buffer.size ()), signature.data ());
	buffer.insert (buffer.end (), signature.begin (), signature.end ());
	return buffer;
}

std::string Base64Key (uint8_t fill, size_t len)
{
	std::vector<uint8_t> data (len, fill);
	return ByteStreamToBase64 (data.data (), data.size ());
}

Params StandardProperties (std::string version = "2.50.0")
{
	// Sorted the way Java DataHelper.writeProperties() emits them.
	return { { "caps", "O" }, { "netId", std::to_string (i2p::context.GetNetID ()) },
		{ "router.version", std::move (version) } };
}

Params SSU2V4Options (const std::string & mtu = "", const std::string & pq = "", const std::string & caps = "")
{
	Params options;
	if (!caps.empty ()) options.emplace_back ("caps", caps);
	options.emplace_back ("host", "8.8.8.8");
	options.emplace_back ("i", Base64Key (0x22, 32));
	if (!mtu.empty ()) options.emplace_back ("mtu", mtu);
	options.emplace_back ("port", "12345");
	if (!pq.empty ()) options.emplace_back ("pq", pq);
	options.emplace_back ("s", Base64Key (0x11, 32));
	options.emplace_back ("v", "2");
	return options;
}

Params SSU2V6Options (const std::string & mtu = "", const std::string & pq = "")
{
	Params options;
	options.emplace_back ("host", "2606:4700:4700::1111");
	options.emplace_back ("i", Base64Key (0x22, 32));
	if (!mtu.empty ()) options.emplace_back ("mtu", mtu);
	options.emplace_back ("port", "12345");
	if (!pq.empty ()) options.emplace_back ("pq", pq);
	options.emplace_back ("s", Base64Key (0x11, 32));
	options.emplace_back ("v", "2");
	return options;
}

Params NTCP2PublishedOptions (bool includeIV, bool includeVersion)
{
	Params options;
	options.emplace_back ("host", "8.8.8.8");
	if (includeIV) options.emplace_back ("i", Base64Key (0x33, 16));
	options.emplace_back ("port", "12345");
	options.emplace_back ("s", Base64Key (0x11, 32));
	if (includeVersion) options.emplace_back ("v", "2");
	return options;
}

Params SSU2IntroducerOptions (const std::string & tag, bool includeExpiration)
{
	Params options;
	options.emplace_back ("caps", "B4");
	options.emplace_back ("i", Base64Key (0x22, 32));
	if (includeExpiration) options.emplace_back ("iexp0", "1900000000");
	options.emplace_back ("ih0", Base64Key (0x44, 32));
	options.emplace_back ("itag0", tag);
	options.emplace_back ("s", Base64Key (0x11, 32));
	options.emplace_back ("v", "2");
	return options;
}

std::shared_ptr<const RouterInfo::Address> ParseWithAddress (
	const PrivateKeys & keys, AddressSpec address, RouterSpec spec = { })
{
	spec.addresses = { std::move (address) };
	if (spec.properties.empty ()) spec.properties = StandardProperties ();
	const auto buffer = SignRouterInfo (keys, BuildBody (spec));
	auto router = std::make_shared<RouterInfo> (buffer.data (), buffer.size ());
	assert (!router->IsUnreachable ());
	auto addresses = router->GetAddresses ();
	assert (addresses);
	for (const auto & address : *addresses)
		if (address) return std::static_pointer_cast<const RouterInfo::Address> (address);
	assert (false);
	return nullptr;
}

void ExpectUnreachable (const PrivateKeys & keys, const RouterSpec & spec)
{
	const auto buffer = SignRouterInfo (keys, BuildBody (spec));
	RouterInfo router (buffer.data (), buffer.size ());
	assert (router.IsUnreachable ());
}
}

int main ()
{
	auto keys
		= PrivateKeys::CreateRandomKeys (SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519, CRYPTO_KEY_TYPE_ECIES_X25519_AEAD);
	assert (keys.GetPublic ());

	// A baseline published SSU2 v4 RouterInfo parses and is selectable.
	{
		RouterSpec spec;
		spec.addresses = { { "SSU2", 8, 0, SSU2V4Options () } };
		spec.properties = StandardProperties ();
		const auto buffer = SignRouterInfo (keys, BuildBody (spec));
		RouterInfo router (buffer.data (), buffer.size ());
		assert (!router.IsUnreachable ());
		assert (router.GetTimestamp () == spec.timestamp);
		assert (router.GetVersion () == 25000);
		assert (router.IsPublished (true));
		assert (router.IsReachableBy (RouterInfo::eSSU2V4));
		assert (router.GetSSU2V4Address () != nullptr);
		assert (router.GetSSU2V6Address () == nullptr);
		assert (router.GetBandwidthCap () == 'O');
		assert (router.IsHighBandwidth ());
		assert (!router.IsExtraBandwidth ());
	}

	// Java RouterInfo.MAX_ADDRESSES is 16. 16 parse, 17 are rejected before
	// the loop.
	{
		RouterSpec spec;
		spec.addresses.resize (i2p::data::MAX_RI_ADDRESSES, { "SSU2", 8, 0, SSU2V4Options () });
		for (size_t i = 1; i < spec.addresses.size (); i++)
			spec.addresses[i].options = SSU2IntroducerOptions ("1", true);
		spec.properties = StandardProperties ();
		const auto buffer = SignRouterInfo (keys, BuildBody (spec));
		RouterInfo router (buffer.data (), buffer.size ());
		assert (!router.IsUnreachable ());
	}
	{
		RouterSpec spec;
		spec.hasAddressCountOverride = true;
		spec.addressCountOverride = MAX_RI_ADDRESSES + 1;
		ExpectUnreachable (keys, spec);
	}

	// Duplicate address options are rejected (Java readProperties(..., true)).
	{
		AddressSpec address { "SSU2", 8, 0, SSU2V4Options (), true };
		RouterSpec spec;
		spec.addresses = { address };
		spec.properties = StandardProperties ();
		ExpectUnreachable (keys, spec);
	}

	// Duplicate properties are rejected; unsorted (but unique) properties are
	// tolerated because other implementations have shipped unsorted mappings.
	{
		RouterSpec spec;
		spec.addresses = { { "SSU2", 8, 0, SSU2V4Options () } };
		spec.properties = StandardProperties ();
		spec.properties.push_back (spec.properties.front ());
		ExpectUnreachable (keys, spec);
	}
	{
		RouterSpec spec;
		spec.addresses = { { "SSU2", 8, 0, SSU2V4Options () } };
		spec.properties = StandardProperties ();
		std::reverse (spec.properties.begin (), spec.properties.end ());
		const auto buffer = SignRouterInfo (keys, BuildBody (spec));
		RouterInfo router (buffer.data (), buffer.size ());
		assert (!router.IsUnreachable ());
	}

	// NTCP2 published addresses require v containing "2" and the "i" IV
	// (Java NTCPTransport.getNTCP2Version). Without both they must not be
	// advertised as published.
	{
		RouterSpec spec;
		spec.addresses = { { "NTCP2", 3, 0, NTCP2PublishedOptions (true, true) } };
		spec.properties = StandardProperties ();
		const auto buffer = SignRouterInfo (keys, BuildBody (spec));
		RouterInfo router (buffer.data (), buffer.size ());
		assert (!router.IsUnreachable ());
		assert (router.IsPublished (true));
		assert (router.GetPublishedNTCP2V4Address () != nullptr);
	}
	{
		RouterSpec spec;
		spec.addresses = { { "NTCP2", 14, 0, NTCP2PublishedOptions (false, true) } };
		spec.properties = StandardProperties ();
		ExpectUnreachable (keys, spec);
	}
	{
		RouterSpec spec;
		spec.addresses = { { "NTCP2", 14, 0, NTCP2PublishedOptions (true, false) } };
		spec.properties = StandardProperties ();
		ExpectUnreachable (keys, spec);
	}

	// Proposal 133: iexp is optional. A firewalled SSU2 address with a valid
	// itag/ih but no iexp remains reachable and peer-testable.
	{
		RouterSpec spec;
		spec.addresses = { { "SSU2", 15, 0, SSU2IntroducerOptions ("1234", false) } };
		spec.properties = StandardProperties ();
		const auto buffer = SignRouterInfo (keys, BuildBody (spec));
		RouterInfo router (buffer.data (), buffer.size ());
		assert (!router.IsUnreachable ());
		assert (router.GetSSU2V4Address () != nullptr);
		assert (router.GetSSU2V4Address ()->UsesIntroducer ());
		assert (router.IsReachableBy (RouterInfo::eSSU2V4));
		assert (router.IsSSU2PeerTesting (true));
	}
	// itag 0 is not a valid introducer (Java requires tag > 0).
	{
		RouterSpec spec;
		spec.addresses = { { "SSU2", 15, 0, SSU2IntroducerOptions ("0", false) } };
		spec.properties = StandardProperties ();
		const auto buffer = SignRouterInfo (keys, BuildBody (spec));
		RouterInfo router (buffer.data (), buffer.size ());
		assert (!router.IsUnreachable ());
		assert (router.GetSSU2V4Address () != nullptr);
		assert (!router.GetSSU2V4Address ()->UsesIntroducer ());
		assert (!router.IsReachableBy (RouterInfo::eSSU2V4));
		assert (!router.IsSSU2PeerTesting (true));
	}
	// Only ih[0-2]/iexp[0-2]/itag[0-2] exist. An index above 2 is ignored
	// instead of growing the introducer vector.
	{
		auto options = SSU2IntroducerOptions ("1234", true);
		options.emplace_back ("itag3", "999");
		RouterSpec spec;
		spec.addresses = { { "SSU2", 15, 0, options } };
		spec.properties = StandardProperties ();
		const auto buffer = SignRouterInfo (keys, BuildBody (spec));
		RouterInfo router (buffer.data (), buffer.size ());
		assert (!router.IsUnreachable ());
		auto address = router.GetSSU2V4Address ();
		assert (address && address->ssu);
		assert (address->ssu->introducers.size () == 1);
		assert (address->ssu->introducers.front ().iTag == 1234);
	}

	// Multiple RouterAddress entries for one transport are allowed. A later
	// published address must replace an earlier introducer-only entry for the
	// same family (Java keeps both and its transports pick the usable one).
	{
		RouterSpec spec;
		spec.addresses
			= { { "SSU2", 15, 0, SSU2IntroducerOptions ("1234", true) }, { "SSU2", 8, 0, SSU2V4Options () } };
		spec.properties = StandardProperties ();
		const auto buffer = SignRouterInfo (keys, BuildBody (spec));
		RouterInfo router (buffer.data (), buffer.size ());
		assert (!router.IsUnreachable ());
		auto address = router.GetSSU2V4Address ();
		assert (address);
		assert (address->published);
		assert (address->host == boost::asio::ip::make_address ("8.8.8.8"));
		assert (router.IsPublished (true));
	}

	// SSU2 pq/MTU selection follows UDPTransport.getSSUVersion(): pq=4 needs
	// 1318 bytes on IPv4 and 1338 on IPv6; pq=3 is the fallback.
	// openi2pd keeps a stricter policy than Java UDPAddress here: an SSU2
	// address whose published MTU is below the spec minimum is not usable at
	// all (see test-ssu2-address-negotiation), so the RI is rejected.
	{
		RouterSpec spec;
		spec.addresses = { { "SSU2", 8, 0, SSU2V4Options ("1000", "4,3") } };
		spec.properties = StandardProperties ();
		const auto buffer = SignRouterInfo (keys, BuildBody (spec));
		RouterInfo router (buffer.data (), buffer.size ());
		assert (router.IsUnreachable ());
	}
	{
		auto address = ParseWithAddress (keys, { "SSU2", 8, 0, SSU2V4Options ("1317", "4") });
		assert (address->v == 2);
	}
	{
		auto address = ParseWithAddress (keys, { "SSU2", 8, 0, SSU2V4Options ("1318", "4,3") });
#if OPENSSL_PQ
		assert (address->v == 4);
#else
		assert (address->v == 2);
#endif
	}
	{
		auto address = ParseWithAddress (keys, { "SSU2", 8, 0, SSU2V6Options ("1337", "4,3") });
#if OPENSSL_PQ
#if defined(LIBRESSL_VERSION_NUMBER)
		assert (address->v == 2); // no ML-KEM-512 fallback
#else
		assert (address->v == 3);
#endif
#else
		assert (address->v == 2);
#endif
	}
	{
		auto address = ParseWithAddress (keys, { "SSU2", 8, 0, SSU2V6Options ("1338", "4,3") });
#if OPENSSL_PQ
		assert (address->v == 4);
#else
		assert (address->v == 2);
#endif
	}

	// Java RouterInfo.getBandwidthTier() picks the strongest bandwidth cap
	// regardless of its position; "POf" must not degrade to "O" or keep both
	// the extra and high flags inconsistently.
	{
		RouterSpec spec;
		spec.addresses = { { "SSU2", 8, 0, SSU2V4Options () } };
		spec.properties = { { "caps", "POf" }, { "netId", std::to_string (i2p::context.GetNetID ()) },
			{ "router.version", "2.50.0" } };
		const auto buffer = SignRouterInfo (keys, BuildBody (spec));
		RouterInfo router (buffer.data (), buffer.size ());
		assert (!router.IsUnreachable ());
		assert (router.GetBandwidthCap () == 'P');
		assert (router.IsHighBandwidth ());
		assert (router.IsExtraBandwidth ());
		assert (router.IsFloodfill ());
	}
	{
		RouterSpec spec;
		spec.addresses = { { "SSU2", 8, 0, SSU2V4Options () } };
		spec.properties = { { "caps", "OP" }, { "netId", std::to_string (i2p::context.GetNetID ()) },
			{ "router.version", "2.50.0" } };
		const auto buffer = SignRouterInfo (keys, BuildBody (spec));
		RouterInfo router (buffer.data (), buffer.size ());
		assert (router.GetBandwidthCap () == 'P');
		assert (router.IsExtraBandwidth ());
	}
	{
		RouterSpec spec;
		spec.addresses = { { "SSU2", 8, 0, SSU2V4Options () } };
		spec.properties = { { "netId", std::to_string (i2p::context.GetNetID ()) }, { "router.version", "2.50.0" } };
		const auto buffer = SignRouterInfo (keys, BuildBody (spec));
		RouterInfo router (buffer.data (), buffer.size ());
		assert (router.GetBandwidthCap () == 0); // "Unknown", not uninitialized
	}
	// A malformed caps string with several congestion markers resolves to the
	// most severe one regardless of order ("ED" must not mean medium).
	{
		RouterSpec spec;
		spec.addresses = { { "SSU2", 8, 0, SSU2V4Options () } };
		spec.properties = { { "caps", "ED" }, { "netId", std::to_string (i2p::context.GetNetID ()) },
			{ "router.version", "2.50.0" } };
		const auto buffer = SignRouterInfo (keys, BuildBody (spec));
		RouterInfo router (buffer.data (), buffer.size ());
		assert (router.GetCongestion () == RouterInfo::eHighCongestion);
	}

	// RouterInfo.isHidden() includes a hidden identity certificate, not only
	// the "H" capability. A hidden router's published address must not count
	// as published.
	{
		// A hidden certificate carries no key certificate, so the identity is
		// a legacy DSA identity. Use matching DSA keys for the signature.
		auto dsaKeys = PrivateKeys::CreateRandomKeys (SIGNING_KEY_TYPE_DSA_SHA1, CRYPTO_KEY_TYPE_ELGAMAL);
		assert (dsaKeys.GetPublic ());
		auto identity = IdentityBuffer (dsaKeys);
		assert (identity.size () >= 387);
		identity[384] = CERTIFICATE_TYPE_HIDDEN; // publicKey (256) + signingKey (128)
		identity[385] = 0;
		identity[386] = 0;
		RouterSpec spec;
		spec.addresses = { { "SSU2", 8, 0, SSU2V4Options () } };
		spec.properties = StandardProperties ();
		const auto buffer = SignRouterInfo (dsaKeys, BuildBody (spec), &identity);
		RouterInfo router (buffer.data (), buffer.size ());
		assert (!router.IsUnreachable ());
		assert (router.IsHidden ());
		assert (!router.IsPublished (true));
	}

	// router.version is dotted. "0.10.0" must be 1000, not the old
	// digit-concatenation 100 (which is below every 0.9.6x threshold).
	{
		RouterSpec spec;
		spec.addresses = { { "SSU2", 8, 0, SSU2V4Options () } };
		spec.properties = StandardProperties ("0.10.0");
		const auto buffer = SignRouterInfo (keys, BuildBody (spec));
		RouterInfo router (buffer.data (), buffer.size ());
		assert (!router.IsUnreachable ());
		assert (router.GetVersion () == 1000);
	}
	{
		RouterSpec spec;
		spec.addresses = { { "SSU2", 8, 0, SSU2V4Options () } };
		spec.properties = StandardProperties ("0.9.62");
		const auto buffer = SignRouterInfo (keys, BuildBody (spec));
		RouterInfo router (buffer.data (), buffer.size ());
		assert (router.GetVersion () == 962);
	}

	// Wrong netId, oversized address/property blocks and trailing bytes are
	// rejected.
	{
		RouterSpec spec;
		spec.addresses = { { "SSU2", 8, 0, SSU2V4Options () } };
		spec.properties = { { "caps", "O" }, { "netId", std::to_string (i2p::context.GetNetID () + 1) },
			{ "router.version", "2.50.0" } };
		ExpectUnreachable (keys, spec);
	}
	{
		RouterSpec spec;
		spec.addresses = { { "SSU2", 8, 0, SSU2V4Options () } };
		spec.addresses[0].sizeOverride = 0xFFFF;
		spec.properties = StandardProperties ();
		ExpectUnreachable (keys, spec);
	}
	{
		RouterSpec spec;
		spec.addresses = { { "SSU2", 8, 0, SSU2V4Options () } };
		spec.properties = StandardProperties ();
		spec.trailingByte = true;
		ExpectUnreachable (keys, spec);
	}

	// Truncating a valid signed RouterInfo at every offset must never parse as
	// valid and must not read out of bounds.
	{
		RouterSpec spec;
		spec.addresses = { { "SSU2", 8, 0, SSU2V4Options () } };
		spec.properties = StandardProperties ();
		const auto buffer = SignRouterInfo (keys, BuildBody (spec));
		for (size_t len = 0; len < buffer.size (); len++)
		{
			RouterInfo router (buffer.data (), len);
			assert (router.IsUnreachable ());
		}
	}

	return 0;
}

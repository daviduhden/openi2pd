/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: test ssu2 control plane.
 *
 * Checks SSU2 control-plane rules: per-family address splitting, Java-compatible pq publication and MTU downgrade, peer-test reachability, hole-punch version and state-specific block whitelists.
 *
 */

#include <cassert>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "Base.h"
#include "Identity.h"
#include "RouterContext.h"
#include "RouterInfo.h"
#if OPENSSL_PQ
#include "PostQuantum.h"
#endif
#include "SSU2OutOfSession.h"
#include "Transports.h"

namespace
{
void AppendBe16 (std::vector<uint8_t> & out, uint16_t value)
{
	out.push_back (static_cast<uint8_t> (value >> 8));
	out.push_back (static_cast<uint8_t> (value));
}

void AppendBe64 (std::vector<uint8_t> & out, uint64_t value)
{
	for (int shift = 56; shift >= 0; shift -= 8) out.push_back (static_cast<uint8_t> (value >> shift));
}

void AppendString (std::vector<uint8_t> & out, std::string_view value)
{
	assert (value.size () <= 255);
	out.push_back (static_cast<uint8_t> (value.size ()));
	out.insert (out.end (), value.begin (), value.end ());
}

// RouterInfo mappings are sequences of length-prefixed key, '=', value and ';'.
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

std::string NonPublishedSSU2Options (std::string_view caps, bool withIntroducer)
{
	const uint8_t staticKey[32] = { };
	const uint8_t introKey[32] = { };

	std::string options;
	AppendParam (options, "caps", caps);
	AppendParam (options, "s", i2p::data::ByteStreamToBase64 (staticKey, sizeof (staticKey)));
	AppendParam (options, "i", i2p::data::ByteStreamToBase64 (introKey, sizeof (introKey)));
	AppendParam (options, "v", "2");
	if (withIntroducer)
	{
		uint8_t introducer[32];
		memset (introducer, 0x11, sizeof (introducer));
		const auto now = i2p::util::GetSecondsSinceEpoch ();
		AppendParam (options, "itag0", "12345");
		AppendParam (options, "ih0", i2p::data::ByteStreamToBase64 (introducer, sizeof (introducer)));
		AppendParam (options, "iexp0", std::to_string (now + 3600));
	}
	return options;
}

std::vector<uint8_t> BuildBody (const std::string & options)
{
	std::vector<uint8_t> body;
	AppendBe64 (body, 1700000000000ULL); // timestamp in milliseconds

	body.push_back (1); // one address
	body.push_back (0); // cost
	AppendBe64 (body, 0); // date
	AppendString (body, "SSU2");
	AppendBe16 (body, static_cast<uint16_t> (options.size ()));
	body.insert (body.end (), options.begin (), options.end ());

	body.push_back (0); // peers (never parsed)

	std::string properties;
	AppendParam (properties, "caps", "O");
	AppendParam (properties, "netId", std::to_string (i2p::context.GetNetID ()));
	AppendParam (properties, "router.version", "2.50.0");
	AppendBe16 (body, static_cast<uint16_t> (properties.size ()));
	body.insert (body.end (), properties.begin (), properties.end ());
	return body;
}

std::vector<uint8_t> SignRouterInfo (const i2p::data::PrivateKeys & keys, const std::vector<uint8_t> & body)
{
	auto identity = keys.GetPublic ();
	assert (identity);

	std::vector<uint8_t> buffer (identity->GetFullLen ());
	assert (identity->ToBuffer (buffer.data (), buffer.size ()) == buffer.size ());
	buffer.insert (buffer.end (), body.begin (), body.end ());

	std::vector<uint8_t> signature (identity->GetSignatureLen ());
	keys.Sign (buffer.data (), static_cast<int> (buffer.size ()), signature.data ());
	buffer.insert (buffer.end (), signature.begin (), signature.end ());
	return buffer;
}

std::shared_ptr<i2p::data::RouterInfo> Parse (const i2p::data::PrivateKeys & keys, const std::string & options)
{
	const auto buffer = SignRouterInfo (keys, BuildBody (options));
	return std::make_shared<i2p::data::RouterInfo> (buffer.data (), buffer.size ());
}

void TestDualFamilyAddressSplit (const i2p::data::PrivateKeys & keys)
{
	auto router = Parse (keys, NonPublishedSSU2Options ("B46", true));
	assert (!router->IsUnreachable ());
	auto v4 = router->GetSSU2V4Address ();
	auto v6 = router->GetSSU2V6Address ();
	assert (v4 && v6);
	// Java publishes separate per-family RouterAddresses; a single "B46"
	// entry must not make one object impersonate both families.
	assert (v4 != v6);
	assert (v4->IsV4 () && !v4->IsV6 ());
	assert (v6->IsV6 () && !v6->IsV4 ());
	assert (v4->UsesIntroducer () && v6->UsesIntroducer ());
	assert (v4->IsReachableSSU () && v6->IsReachableSSU ());
	assert (v4->IsPeerTesting () && v6->IsPeerTesting ());
	assert (router->IsSSU2PeerTesting (true));
	assert (router->IsSSU2PeerTesting (false));
	assert (!router->IsPublished (true));
	assert (!router->IsPublished (false));
}

void TestUnpublishedAddressWithoutEndpointIsNotReachable ()
{
	i2p::data::RouterInfo::Address addr { };
	addr.transportStyle = i2p::data::RouterInfo::eTransportSSU2;
	addr.caps = i2p::data::RouterInfo::eV4;
	addr.port = 12345;
	addr.ssu = std::make_unique<i2p::data::RouterInfo::SSUExt> ();
	addr.published = true; // e.g. SetReachable() before an Address block
	assert (!addr.IsReachableSSU ());

	addr.host = boost::asio::ip::make_address ("8.8.8.8");
	assert (addr.IsReachableSSU ());

	addr.host = boost::asio::ip::address ();
	i2p::data::RouterInfo::Introducer introducer;
	introducer.iTag = 1;
	introducer.iExp = static_cast<uint32_t> (i2p::util::GetSecondsSinceEpoch () + 60);
	introducer.iH.Fill (0x22);
	addr.ssu->introducers.push_back (introducer);
	assert (addr.IsReachableSSU ());
}

void TestPQPublicationUsesPerFamilyMTU (const i2p::data::PrivateKeys & keys)
{
	i2p::data::LocalRouterInfo local;
	local.SetRouterIdentity (keys.GetPublic ());
	const uint8_t staticKey[32] = { };
	const uint8_t introKey[32] = { };
	local.AddSSU2Address (staticKey, introKey, 12345, i2p::data::RouterInfo::eV4 | i2p::data::RouterInfo::eV6);

	auto addresses = local.GetAddresses ();
	assert (addresses);
	auto v4 = (*addresses)[i2p::data::RouterInfo::eSSU2V4Idx];
	auto v6 = (*addresses)[i2p::data::RouterInfo::eSSU2V6Idx];
	assert (v4 && v6 && v4 != v6);
	assert (v4->IsV4 () && !v4->IsV6 ());
	assert (v6->IsV6 () && !v6->IsV4 ());

	// Simulate PublishSSU2Address(): pq=4 requested for both families, but
	// only the IPv4 interface has a low MTU. ML-KEM-768 needs 1318 bytes on
	// IPv4 (PeerState2.MIN_MLKEM768_IPV4_MTU).
	v4->v = 4;
	v4->ssu->mtu = 1280;
	v4->caps |= i2p::data::RouterInfo::eSSUTesting;
	v6->v = 4;
	v6->ssu->mtu = 0; // unknown, treated as the default link MTU
	v6->caps |= i2p::data::RouterInfo::eSSUTesting;

	local.UpdateCaps (0);
	local.SetProperty ("netId", std::to_string (i2p::context.GetNetID ()));
	local.SetProperty ("router.version", "2.50.0");
	local.CreateBuffer (keys);
	assert (local.GetBuffer () && local.GetBufferLen ());

	i2p::data::RouterInfo parsed (local.GetBuffer (), local.GetBufferLen ());
	assert (!parsed.IsUnreachable ());
	auto pv4 = parsed.GetSSU2V4Address ();
	auto pv6 = parsed.GetSSU2V6Address ();
	assert (pv4 && pv6 && pv4 != pv6);

	// The provider decides which advertised parameter sets can be selected.
	bool has3 = false, has4 = false;
#if OPENSSL_PQ
	has3 = i2p::crypto::CanPublishPQ (3);
	has4 = i2p::crypto::CanPublishPQ (4);
#endif

	// IPv4: published "pq=3". A low MTU prevents ML-KEM-768, so use version 3
	// only when the provider supplies ML-KEM-512.
	assert (pv4->ssu && pv4->ssu->mtu == 1280);
	assert (pv4->v == (has3 ? 3 : 2));
	assert (pv4->SupportsSSU2Version (3) == has3);
	assert (!pv4->SupportsSSU2Version (4));

	// IPv6: unknown MTU keeps the Java-compatible "4,3" list when available.
	assert (pv6->ssu && pv6->ssu->mtu == 0);
	assert (pv6->v == (has4 ? 4 : (has3 ? 3 : 2)));
	assert (pv6->SupportsSSU2Version (4) == has4);
	assert (pv6->SupportsSSU2Version (3) == has3);

	// "B" survives serialization for non-published addresses.
	assert (pv4->IsPeerTesting () && pv6->IsPeerTesting ());
}

void TestStatePayloadBlockMatrix ()
{
	using namespace i2p::transport;

	auto expectOnly = [] (SSU2SessionState state, std::initializer_list<uint8_t> allowedBlocks)
	{
		for (int block = 0; block < 256; block++)
		{
			bool expected = false;
			for (auto allowed : allowedBlocks)
				if (allowed == block)
				{
					expected = true;
					break;
				}
			assert (IsSSU2PayloadBlockAllowedForState (state, static_cast<uint8_t> (block)) == expected);
		}
	};

	expectOnly (eSSU2SessionStateTokenRequestReceived, { eSSU2BlkDateTime, eSSU2BlkPadding, eSSU2BlkTermination });
	expectOnly (eSSU2SessionStateSessionRequestReceived,
		{ eSSU2BlkDateTime, eSSU2BlkPadding, eSSU2BlkRelayTagRequest, eSSU2BlkTermination });
	expectOnly (eSSU2SessionStateSessionCreatedReceived,
		{ eSSU2BlkDateTime, eSSU2BlkAddress, eSSU2BlkNewToken, eSSU2BlkRelayTag, eSSU2BlkPadding });
	expectOnly (eSSU2SessionStateTokenReceived, // Retry
		{ eSSU2BlkDateTime, eSSU2BlkAddress, eSSU2BlkPadding, eSSU2BlkTermination });
	expectOnly (eSSU2SessionStateSessionCreatedSent, // SessionConfirmed remainder
		{ eSSU2BlkRouterInfo, eSSU2BlkOptions, eSSU2BlkPadding });

	// Data-phase states intentionally keep the full block set.
	for (int block = 0; block < 256; block++)
		assert (IsSSU2PayloadBlockAllowedForState (eSSU2SessionStateEstablished, static_cast<uint8_t> (block)));

	// Independent pre-auth guards used by HandlePeerTest/HandleI2NPMsg and
	// HandleAddress.
	assert (IsSSU2EstablishedPayloadContext (eSSU2SessionStateEstablished, true));
	assert (!IsSSU2EstablishedPayloadContext (eSSU2SessionStateEstablished, false));
	for (auto state :
		{ eSSU2SessionStateUnknown, eSSU2SessionStateTokenRequestReceived, eSSU2SessionStateSessionRequestReceived,
			eSSU2SessionStateSessionCreatedSent, eSSU2SessionStateSessionCreatedReceived,
			eSSU2SessionStateTokenReceived, eSSU2SessionStateSessionConfirmedSent, eSSU2SessionStateIntroduced })
		assert (!IsSSU2EstablishedPayloadContext (state, true));

	assert (IsSSU2AddressBlockContextAllowed (eSSU2SessionStateEstablished, true));
	assert (IsSSU2AddressBlockContextAllowed (eSSU2SessionStateSessionCreatedReceived, true));
	assert (IsSSU2AddressBlockContextAllowed (eSSU2SessionStateTokenReceived, true));
	assert (!IsSSU2AddressBlockContextAllowed (eSSU2SessionStateEstablished, false));
	assert (!IsSSU2AddressBlockContextAllowed (eSSU2SessionStateSessionRequestReceived, true));
	assert (!IsSSU2AddressBlockContextAllowed (eSSU2SessionStateTokenRequestReceived, true));
	assert (!IsSSU2AddressBlockContextAllowed (eSSU2SessionStateSessionCreatedSent, true));
}

void TestPayloadBlockValidator ()
{
	using namespace i2p::transport;

	const uint8_t dateTime[] = { eSSU2BlkDateTime, 0, 4, 0, 0, 0, 1 };
	const uint8_t padding[] = { eSSU2BlkPadding, 0, 0 };
	const uint8_t address[] = { eSSU2BlkAddress, 0, 6, 0x1F, 0x90, 8, 8, 8, 8 };
	const uint8_t i2np[] = { eSSU2BlkI2NPMessage, 0, 9, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
	const uint8_t relayTagRequest[] = { eSSU2BlkRelayTagRequest, 0, 0 };
	const uint8_t newToken[] = { eSSU2BlkNewToken, 0, 12, 0, 0, 0, 1, 1, 2, 3, 4, 5, 6, 7, 8 };
	const uint8_t routerInfo[] = { eSSU2BlkRouterInfo, 0, 2, 0, 0 };
	const uint8_t options[] = { eSSU2BlkOptions, 0, 0 };
	const uint8_t termination[] = { eSSU2BlkTermination, 0, 9, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

	assert (ValidateSSU2PayloadBlocks (eSSU2SessionStateTokenRequestReceived, dateTime, sizeof (dateTime)));
	assert (ValidateSSU2PayloadBlocks (eSSU2SessionStateTokenRequestReceived, padding, sizeof (padding)));
	assert (!ValidateSSU2PayloadBlocks (eSSU2SessionStateTokenRequestReceived, address, sizeof (address)));
	assert (!ValidateSSU2PayloadBlocks (eSSU2SessionStateTokenRequestReceived, i2np, sizeof (i2np)));

	assert (
		ValidateSSU2PayloadBlocks (eSSU2SessionStateSessionRequestReceived, relayTagRequest, sizeof (relayTagRequest)));
	assert (ValidateSSU2PayloadBlocks (eSSU2SessionStateSessionRequestReceived, termination, sizeof (termination)));
	assert (!ValidateSSU2PayloadBlocks (eSSU2SessionStateSessionRequestReceived, i2np, sizeof (i2np)));

	assert (ValidateSSU2PayloadBlocks (eSSU2SessionStateSessionCreatedReceived, address, sizeof (address)));
	assert (ValidateSSU2PayloadBlocks (eSSU2SessionStateSessionCreatedReceived, newToken, sizeof (newToken)));
	assert (!ValidateSSU2PayloadBlocks (eSSU2SessionStateSessionCreatedReceived, i2np, sizeof (i2np)));
	assert (!ValidateSSU2PayloadBlocks (eSSU2SessionStateSessionCreatedReceived, termination, sizeof (termination)));

	assert (ValidateSSU2PayloadBlocks (eSSU2SessionStateTokenReceived, address, sizeof (address)));
	assert (ValidateSSU2PayloadBlocks (eSSU2SessionStateTokenReceived, termination, sizeof (termination)));
	assert (!ValidateSSU2PayloadBlocks (eSSU2SessionStateTokenReceived, i2np, sizeof (i2np)));

	assert (ValidateSSU2PayloadBlocks (eSSU2SessionStateSessionCreatedSent, routerInfo, sizeof (routerInfo)));
	assert (ValidateSSU2PayloadBlocks (eSSU2SessionStateSessionCreatedSent, options, sizeof (options)));
	assert (!ValidateSSU2PayloadBlocks (eSSU2SessionStateSessionCreatedSent, i2np, sizeof (i2np)));

	// Truncated framing, content after padding and non-4-byte DateTime.
	const uint8_t truncated[] = { eSSU2BlkPadding, 0 };
	assert (!ValidateSSU2PayloadBlocks (eSSU2SessionStateTokenRequestReceived, truncated, sizeof (truncated)));
	assert (!ValidateSSU2PayloadBlocks (eSSU2SessionStateTokenRequestReceived, nullptr, sizeof (dateTime)));
	const uint8_t badDateTime[] = { eSSU2BlkDateTime, 0, 3, 0, 0, 0 };
	assert (!ValidateSSU2PayloadBlocks (eSSU2SessionStateTokenRequestReceived, badDateTime, sizeof (badDateTime)));
	const uint8_t afterPadding[] = { eSSU2BlkPadding, 0, 0, eSSU2BlkPadding, 0, 0 };
	assert (!ValidateSSU2PayloadBlocks (eSSU2SessionStateTokenRequestReceived, afterPadding, sizeof (afterPadding)));
}

void TestOutOfSessionVersionAndRouterInfoFrag ()
{
	using namespace i2p::transport;

	// PeerTest and HolePunch send version 2 but accept 2..4 on receive, so a
	// peer that already negotiated a post-quantum version is not rejected.
	assert (!IsSSU2OutOfSessionVersionAccepted (1));
	for (uint8_t version = 2; version <= 4; ++version) assert (IsSSU2OutOfSessionVersionAccepted (version));
	assert (!IsSSU2OutOfSessionVersionAccepted (5));
	assert (SSU2_HOLE_PUNCH_VERSION == 2);

	// A single unfragmented RouterInfo is the only accepted frag byte. The
	// SSU2 field is `index << 4 | count`, so 0x00, 0x11 and 0x12 are fragments
	// and must be rejected in the SessionConfirmed leading block and in a
	// data-phase RouterInfo block alike.
	assert (IsSSU2RouterInfoFragmentAcceptable (0x01));
	assert (!IsSSU2RouterInfoFragmentAcceptable (0x00));
	assert (!IsSSU2RouterInfoFragmentAcceptable (0x11));
	assert (!IsSSU2RouterInfoFragmentAcceptable (0x12));
	assert (SSU2_ROUTER_INFO_FRAG_SINGLE == 0x01);
}

void TestPeerTestEndpointValidation ()
{
	using endpoint = boost::asio::ip::udp::endpoint;

	assert (i2p::transport::SSU2_HOLE_PUNCH_VERSION == 2);

	// Public address, plausible SSU2 port.
	assert (i2p::transport::IsValidPeerTestEndpoint (endpoint (boost::asio::ip::make_address ("8.8.8.8"), 9151)));
	assert (i2p::transport::IsValidPeerTestEndpoint (
		endpoint (boost::asio::ip::make_address ("2001:4860:4860::8888"), 9151)));
	// Ports in Java's SSU2 random range are acceptable even when they are on
	// the local port-selection blacklist (Java isValidPort() first clause).
	assert (i2p::transport::IsValidPeerTestEndpoint (endpoint (boost::asio::ip::make_address ("8.8.8.8"), 20000)));

	// Privileged, zero and conventional reserved ports.
	assert (!i2p::transport::IsValidPeerTestEndpoint (endpoint (boost::asio::ip::make_address ("8.8.8.8"), 80)));
	assert (!i2p::transport::IsValidPeerTestEndpoint (endpoint (boost::asio::ip::make_address ("8.8.8.8"), 0)));
	assert (!i2p::transport::IsValidPeerTestEndpoint (
		endpoint (boost::asio::ip::make_address ("8.8.8.8"), 9150))); // Tor browser
	assert (!i2p::transport::IsValidPeerTestEndpoint (
		endpoint (boost::asio::ip::make_address ("8.8.8.8"), 7655))); // standard I2P port
	assert (
		!i2p::transport::IsValidPeerTestEndpoint (endpoint (boost::asio::ip::make_address ("8.8.8.8"), 9001))); // Tor

	// Unspecified, loopback, RFC1918 and documentation ranges.
	assert (!i2p::transport::IsValidPeerTestEndpoint (endpoint ()));
	assert (!i2p::transport::IsValidPeerTestEndpoint (endpoint (boost::asio::ip::make_address ("127.0.0.1"), 9151)));
	assert (!i2p::transport::IsValidPeerTestEndpoint (endpoint (boost::asio::ip::make_address ("10.1.2.3"), 9151)));
	assert (!i2p::transport::IsValidPeerTestEndpoint (endpoint (boost::asio::ip::make_address ("192.168.1.1"), 9151)));
	assert (!i2p::transport::IsValidPeerTestEndpoint (endpoint (boost::asio::ip::make_address ("192.0.2.1"), 9151)));

	// Banned sources are rejected.
	const auto banned = boost::asio::ip::make_address ("8.8.4.4");
	i2p::transport::transports.AddBan (banned);
	assert (!i2p::transport::IsValidPeerTestEndpoint (endpoint (banned, 9151)));

	// A PeerTest payload is decrypted with the public introduction key, so
	// the generic I2NP/fragment/path/termination blocks must never be accepted.
	assert (i2p::transport::IsPeerTestPayloadBlockAllowed (i2p::transport::eSSU2BlkDateTime));
	assert (i2p::transport::IsPeerTestPayloadBlockAllowed (i2p::transport::eSSU2BlkPeerTest));
	assert (i2p::transport::IsPeerTestPayloadBlockAllowed (i2p::transport::eSSU2BlkAddress));
	assert (i2p::transport::IsPeerTestPayloadBlockAllowed (i2p::transport::eSSU2BlkPadding));
	assert (!i2p::transport::IsPeerTestPayloadBlockAllowed (i2p::transport::eSSU2BlkI2NPMessage));
	assert (!i2p::transport::IsPeerTestPayloadBlockAllowed (i2p::transport::eSSU2BlkFirstFragment));
	assert (!i2p::transport::IsPeerTestPayloadBlockAllowed (i2p::transport::eSSU2BlkPathChallenge));
	assert (!i2p::transport::IsPeerTestPayloadBlockAllowed (i2p::transport::eSSU2BlkTermination));
}
} // namespace

int main ()
{
	auto keys = i2p::data::PrivateKeys::CreateRandomKeys (
		i2p::data::SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519, i2p::data::CRYPTO_KEY_TYPE_ECIES_X25519_AEAD);
	assert (keys.GetPublic ());

	TestDualFamilyAddressSplit (keys);
	TestUnpublishedAddressWithoutEndpointIsNotReachable ();
	TestPQPublicationUsesPerFamilyMTU (keys);
	TestStatePayloadBlockMatrix ();
	TestPayloadBlockValidator ();
	TestOutOfSessionVersionAndRouterInfoFrag ();
	TestPeerTestEndpointValidation ();
	return 0;
}

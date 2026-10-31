/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: test ssu2 address negotiation.
 *
 * Checks SSU2 RouterInfo address metadata: version-2 compatibility
 * lists, pq preference selection against published MTU, and rejection
 * of sub-1280 MTUs or malformed values.
 *
 */

#include <cassert>
#include <cstdint>
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

// RouterInfo mappings are sequences of length-prefixed key, '=', value
// and ';'.
// Build the encoded bytes, not a human-readable semicolon list.
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

std::string SSU2Options (const std::string & mtu, const std::string & v, const std::string & pq)
{
	const uint8_t staticKey[32] = { };
	const uint8_t introKey[32] = { };

	std::string options;
	AppendParam (options, "host", "8.8.8.8");
	AppendParam (options, "port", "12345");
	if (!mtu.empty ()) AppendParam (options, "mtu", mtu);
	AppendParam (options, "s", i2p::data::ByteStreamToBase64 (staticKey, sizeof (staticKey)));
	AppendParam (options, "i", i2p::data::ByteStreamToBase64 (introKey, sizeof (introKey)));
	AppendParam (options, "v", v);
	if (!pq.empty ()) AppendParam (options, "pq", pq);
	AppendParam (options, "caps", "BC");
	return options;
}

std::vector<uint8_t> BuildBody (const std::string & mtu, const std::string & v, const std::string & pq)
{
	std::vector<uint8_t> body;
	AppendBe64 (body, 1700000000000ULL); // timestamp in milliseconds

	body.push_back (1); // one address
	body.push_back (0); // cost
	AppendBe64 (body, 0); // date
	AppendString (body, "SSU2");

	const auto addressOptions = SSU2Options (mtu, v, pq);
	AppendBe16 (body, static_cast<uint16_t> (addressOptions.size ()));
	body.insert (body.end (), addressOptions.begin (), addressOptions.end ());

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

std::shared_ptr<i2p::data::RouterInfo> Parse (
	const i2p::data::PrivateKeys & keys, const std::string & mtu, const std::string & v, const std::string & pq)
{
	const auto buffer = SignRouterInfo (keys, BuildBody (mtu, v, pq));
	return std::make_shared<i2p::data::RouterInfo> (buffer.data (), buffer.size ());
}

void ExpectAccepted (const i2p::data::PrivateKeys & keys, const std::string & mtu, const std::string & v,
	const std::string & pq, int expectedVersion, bool expectPq4)
{
	auto router = Parse (keys, mtu, v, pq);
	assert (!router->IsUnreachable ());
	auto address = router->GetSSU2V4Address ();
	assert (address != nullptr);
	assert (address->v == expectedVersion);
	assert (address->SupportsSSU2Version (2));
	assert (address->SupportsSSU2Version (4) == expectPq4);
}

void ExpectRejected (
	const i2p::data::PrivateKeys & keys, const std::string & mtu, const std::string & v, const std::string & pq)
{
	auto router = Parse (keys, mtu, v, pq);
	assert (router->IsUnreachable ());
	assert (router->GetSSU2V4Address () == nullptr);
}

bool HasPQ (int version)
{
#if OPENSSL_PQ
	return i2p::crypto::CanPublishPQ (version);
#else
	(void)version;
	return false;
#endif
}
}

int main ()
{
	auto keys = i2p::data::PrivateKeys::CreateRandomKeys (
		i2p::data::SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519, i2p::data::CRYPTO_KEY_TYPE_ECIES_X25519_AEAD);
	assert (keys.GetPublic ());

	// A comma-separated compatibility list is valid as long as it contains
	// the base version 2 (SSU2 spec, Published Router Info).
	ExpectAccepted (keys, "", "2,3", "", 2, false);
	ExpectAccepted (keys, "", "2,4", "", 2, false);

	// A base version other than 2 is not a valid SSU2 address.
	ExpectRejected (keys, "", "3", "");
	ExpectRejected (keys, "", "4", "");
	ExpectRejected (keys, "", "2,bogus", "");

	// pq=4,3 with the default MTU selects the strongest parameter set that
	// the active provider actually implements.
	const int bestDefaultMTU = HasPQ (4) ? 4 : (HasPQ (3) ? 3 : 2);
	ExpectAccepted (keys, "", "2", "4,3", bestDefaultMTU, true);

	// ML-KEM-768 needs 1318 bytes on IPv4. A lower published MTU must
	// select the ML-KEM-512 version when available, otherwise base SSU2.
	const int bestLowMTU = HasPQ (3) ? 3 : 2;
	ExpectAccepted (keys, "1317", "2", "4,3", bestLowMTU, true);
	ExpectAccepted (keys, "1318", "2", "4,3", HasPQ (4) ? 4 : bestLowMTU, true);

	// pq=4 without a usable lower version cannot select Q4; the base SSU2
	// version remains 2, while the option itself is still recorded.
	ExpectAccepted (keys, "1300", "2", "4", 2, true);

	// The SSU2 minimum MTU is 1280 for both families (SSU2 spec, MTU).
	ExpectRejected (keys, "1279", "2", "");
	ExpectAccepted (keys, "1280", "2", "", 2, false);

	// A malformed MTU is treated as "unspecified" (the default 1500).
	ExpectAccepted (keys, "-1", "2", "", 2, false);

	return 0;
}

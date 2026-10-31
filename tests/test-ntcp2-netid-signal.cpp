/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: test ntcp2 netid signal.
 *
 * Verifies RouterInfo::HasForeignNetId(), the signal the NTCP2
 * SessionConfirmed path uses to answer reason 17 and ban the source
 * instead of reporting a signature failure. A foreign netId keeps the
 * RouterInfo unreachable (it must never be stored or selected) but
 * stays distinguishable from a malformed body. Only signed RouterInfo
 * buffers are built here; the ban/termination itself needs the socket
 * harness.
 *
 */

#include <cassert>
#include <cstdint>
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

// A RouterInfo with no addresses: the parser accepts that (the
// no-address
// policy lives in NetDb), so the test only depends on the property
// block.
std::vector<uint8_t> BuildBody (const std::vector<std::pair<std::string, std::string>> & properties)
{
	std::vector<uint8_t> body;
	AppendBe64 (body, 1700000000000ULL); // timestamp
	body.push_back (0); // address count
	body.push_back (0); // peer count
	std::string encoded;
	for (const auto & [key, value] : properties) AppendParam (encoded, key, value);
	AppendBe16 (body, static_cast<uint16_t> (encoded.size ()));
	body.insert (body.end (), encoded.begin (), encoded.end ());
	return body;
}

std::vector<uint8_t> SignRouterInfo (const PrivateKeys & keys, const std::vector<uint8_t> & body)
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

std::vector<std::pair<std::string, std::string>> Properties (const std::string & netId)
{
	return { { "caps", "O" }, { "netId", netId }, { "router.version", "2.50.0" } };
}
}

int main ()
{
	auto keys
		= PrivateKeys::CreateRandomKeys (SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519, CRYPTO_KEY_TYPE_ECIES_X25519_AEAD);
	assert (keys.GetPublic ());

	const auto ourNetId = i2p::context.GetNetID ();
	// The default build netId is not zero, which lets the test distinguish
	// a foreign network (>= 1) from the unspecified netId=0 property.
	assert (ourNetId != 0);

	// Baseline: our own netId parses and is neither unreachable nor
	// foreign.
	{
		const auto buffer = SignRouterInfo (keys, BuildBody (Properties (std::to_string (ourNetId))));
		RouterInfo router (buffer.data (), buffer.size ());
		assert (!router.IsUnreachable ());
		assert (!router.HasForeignNetId ());
		assert (!router.IsForeignNetId ());
	}

	// A non-zero netId of another network: the RouterInfo stays rejected
	// (it must never be stored or selected) but the reason is queryable,
	// so NTCP2 answers reason 17 and bans the source instead of treating
	// it as a signature failure.
	{
		const auto buffer = SignRouterInfo (keys, BuildBody (Properties (std::to_string (ourNetId + 1))));
		RouterInfo router (buffer.data (), buffer.size ());
		assert (router.IsUnreachable ());
		assert (router.HasForeignNetId ());
		assert (router.IsForeignNetId ());
	}

	// netId=0 is never foreign. It is still rejected because it is not our
	// network ID (unchanged behaviour), but it must not trigger the ban
	// path.
	{
		const auto buffer = SignRouterInfo (keys, BuildBody (Properties ("0")));
		RouterInfo router (buffer.data (), buffer.size ());
		assert (router.IsUnreachable ());
		assert (!router.HasForeignNetId ());
	}

	// A non-numeric netId is malformed, not foreign.
	{
		const auto buffer = SignRouterInfo (keys, BuildBody (Properties ("not-a-number")));
		RouterInfo router (buffer.data (), buffer.size ());
		assert (router.IsUnreachable ());
		assert (!router.HasForeignNetId ());
	}

	// A body with a missing netId property is malformed and not foreign.
	{
		const std::vector<std::pair<std::string, std::string>> properties
			= { { "caps", "O" }, { "router.version", "2.50.0" } };
		const auto buffer = SignRouterInfo (keys, BuildBody (properties));
		RouterInfo router (buffer.data (), buffer.size ());
		assert (router.IsUnreachable ());
		assert (!router.HasForeignNetId ());
	}

	// A truncated buffer never yields the foreign-netId signal: the error
	// is detected before the property block.
	{
		const auto buffer = SignRouterInfo (keys, BuildBody (Properties (std::to_string (ourNetId + 1))));
		for (size_t len = 0; len < buffer.size (); len += 7)
		{
			RouterInfo router (buffer.data (), len);
			assert (router.IsUnreachable ());
			assert (!router.HasForeignNetId ());
		}
	}

	return 0;
}

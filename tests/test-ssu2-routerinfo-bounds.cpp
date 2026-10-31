/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: test ssu2 routerinfo bounds.
 *
 * Signs both valid and malformed RouterInfo bodies with a fresh EdDSA
 * identity, so parsing rather than signature verification must reject
 * overrunning address/property sections and family-mismatched hosts.
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

std::string SSU2Options (bool duplicatePort, bool crossFamilyCaps)
{
	const uint8_t staticKey[32] = { };
	const uint8_t introKey[32] = { };

	std::string options;
	AppendParam (options, "host", "8.8.8.8");
	AppendParam (options, "port", "12345");
	if (duplicatePort) AppendParam (options, "port", "12346");
	AppendParam (options, "s", i2p::data::ByteStreamToBase64 (staticKey, sizeof (staticKey)));
	AppendParam (options, "i", i2p::data::ByteStreamToBase64 (introKey, sizeof (introKey)));
	AppendParam (options, "v", "2");
	AppendParam (options, "caps", crossFamilyCaps ? "B6C" : "BC");
	return options;
}

struct BodyOptions
{
	bool tooManyAddresses = false;
	bool duplicateAddressKey = false;
	bool oversizedAddressBlock = false;
	bool crossFamilyCaps = false;
	bool tooManyPeers = false;
	bool duplicateProperty = false;
	bool trailingBytes = false;
};

std::vector<uint8_t> BuildBody (const BodyOptions & options)
{
	std::vector<uint8_t> body;
	AppendBe64 (body, 1700000000000ULL); // timestamp in milliseconds

	if (options.tooManyAddresses)
	{
		body.push_back (static_cast<uint8_t> (i2p::data::MAX_RI_ADDRESSES + 1));
		return body;
	}

	body.push_back (1); // one address
	body.push_back (0); // cost
	AppendBe64 (body, 0); // date
	AppendString (body, "SSU2");

	const auto addressOptions = SSU2Options (options.duplicateAddressKey, options.crossFamilyCaps);
	AppendBe16 (body, options.oversizedAddressBlock ? 0xFFFF : static_cast<uint16_t> (addressOptions.size ()));
	body.insert (body.end (), addressOptions.begin (), addressOptions.end ());

	body.push_back (
		options.tooManyPeers ? static_cast<uint8_t> (0xFF) : static_cast<uint8_t> (0)); // peers (never parsed)

	std::string properties;
	AppendParam (properties, "caps", "O");
	AppendParam (properties, "netId", std::to_string (i2p::context.GetNetID ()));
	if (options.duplicateProperty) AppendParam (properties, "netId", std::to_string (i2p::context.GetNetID ()));
	AppendParam (properties, "router.version", "2.50.0");
	AppendBe16 (body, static_cast<uint16_t> (properties.size ()));
	body.insert (body.end (), properties.begin (), properties.end ());

	if (options.trailingBytes) body.push_back (0);
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

void ExpectRejected (const i2p::data::PrivateKeys & keys, const BodyOptions & options)
{
	const auto buffer = SignRouterInfo (keys, BuildBody (options));
	i2p::data::RouterInfo router (buffer.data (), buffer.size ());
	assert (router.IsUnreachable ());
}
}

int main ()
{
	auto keys = i2p::data::PrivateKeys::CreateRandomKeys (
		i2p::data::SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519, i2p::data::CRYPTO_KEY_TYPE_ECIES_X25519_AEAD);
	assert (keys.GetPublic ());

	// Regression: ReadFromBuffer() used to subtract the signature length
	// after
	// the caller had already removed it, rejecting every valid RouterInfo.
	{
		const auto buffer = SignRouterInfo (keys, BuildBody ({ }));
		i2p::data::RouterInfo router (buffer.data (), buffer.size ());
		assert (!router.IsUnreachable ());
		assert (router.GetSSU2V4Address () != nullptr);
		assert (router.GetSSU2V6Address () == nullptr);
	}

	// The update path parses the signed body after the identity as well;
	// it had
	// the same second signature-length subtraction.
	{
		const auto first = SignRouterInfo (keys, BuildBody ({ }));
		i2p::data::RouterInfo router (first.data (), first.size ());
		assert (!router.IsUnreachable ());

		const auto second = SignRouterInfo (keys, BuildBody ({ }));
		assert (router.Update (second.data (), second.size ()));
		assert (!router.IsUnreachable ());
		assert (router.GetSSU2V4Address () != nullptr);
	}

	// A published IPv4 host must not be made selectable as IPv6 by an
	// untrusted "caps=6" address property.
	{
		BodyOptions crossFamily;
		crossFamily.crossFamilyCaps = true;
		const auto buffer = SignRouterInfo (keys, BuildBody (crossFamily));
		i2p::data::RouterInfo router (buffer.data (), buffer.size ());
		assert (!router.IsUnreachable ());
		assert (router.GetSSU2V4Address () != nullptr);
		assert (router.GetSSU2V6Address () == nullptr);
		assert (!router.IsSSU2V6 ());
	}

	BodyOptions options;
	options.tooManyAddresses = true;
	ExpectRejected (keys, options);

	options = { };
	options.duplicateAddressKey = true;
	ExpectRejected (keys, options);

	options = { };
	options.oversizedAddressBlock = true;
	ExpectRejected (keys, options);

	options = { };
	options.tooManyPeers = true;
	ExpectRejected (keys, options);

	options = { };
	options.duplicateProperty = true;
	ExpectRejected (keys, options);

	options = { };
	options.trailingBytes = true;
	ExpectRejected (keys, options);

	return 0;
}

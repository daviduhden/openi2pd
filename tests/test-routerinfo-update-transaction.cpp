/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: test routerinfo update transaction.
 *
 * Exercises RouterInfo update atomicity: only a newer, correctly signed, same-identity body replaces state, while malformed, wrong-netId or file-loaded bodies leave the old snapshot intact. Also checks local v4/v6 address serialization.
 *
 */

#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
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
#include "Timestamp.h"

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
	uint8_t cost = 8;
	uint64_t date = 0;
	Params options;
};

struct RouterSpec
{
	uint64_t timestamp = 1700000000000ULL;
	std::vector<AddressSpec> addresses;
	Params properties;
	bool trailingByte = false;
};

std::vector<uint8_t> BuildBody (const RouterSpec & spec)
{
	std::vector<uint8_t> body;
	AppendBe64 (body, spec.timestamp);
	body.push_back (static_cast<uint8_t> (spec.addresses.size ()));
	for (const auto & address : spec.addresses)
	{
		body.push_back (address.cost);
		AppendBe64 (body, address.date);
		AppendString (body, address.style);
		std::string options;
		for (const auto & [key, value] : address.options) AppendParam (options, key, value);
		AppendBe16 (body, static_cast<uint16_t> (options.size ()));
		body.insert (body.end (), options.begin (), options.end ());
	}
	body.push_back (0); // peers
	std::string properties;
	for (const auto & [key, value] : spec.properties) AppendParam (properties, key, value);
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

std::vector<uint8_t> SignRouterInfo (const PrivateKeys & keys, const std::vector<uint8_t> & body)
{
	std::vector<uint8_t> buffer = IdentityBuffer (keys);
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

Params StandardProperties (const std::string & caps = "O")
{
	return { { "caps", caps }, { "netId", std::to_string (i2p::context.GetNetID ()) }, { "router.version", "2.50.0" } };
}

Params SSU2Options (const std::string & port)
{
	return { { "host", "8.8.8.8" }, { "i", Base64Key (0x22, 32) }, { "port", port }, { "s", Base64Key (0x11, 32) },
		{ "v", "2" } };
}

RouterSpec MakeSpec (uint64_t timestamp, const std::string & port, const std::string & caps = "O")
{
	RouterSpec spec;
	spec.timestamp = timestamp;
	spec.addresses = { { "SSU2", 8, 0, SSU2Options (port) } };
	spec.properties = StandardProperties (caps);
	return spec;
}

std::filesystem::path TemporaryFilePath ()
{
	auto name = std::string ("openi2pd-routerinfo-")
		+ std::to_string (std::chrono::steady_clock::now ().time_since_epoch ().count ()) + ".dat";
	return std::filesystem::temp_directory_path () / name;
}

void ExpectSameState (const RouterInfo & router, uint64_t timestamp, uint16_t port, int version)
{
	assert (router.GetTimestamp () == timestamp);
	assert (router.GetVersion () == version);
	auto address = router.GetSSU2V4Address ();
	assert (address);
	assert (address->port == port);
}
}

int main ()
{
	auto keys
		= PrivateKeys::CreateRandomKeys (SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519, CRYPTO_KEY_TYPE_ECIES_X25519_AEAD);
	assert (keys.GetPublic ());

	const uint64_t timestamp = 1700000000000ULL;

	// IsNewer() is a strict timestamp comparison and needs the full identity
	// in the supplied buffer.
	{
		const auto first = SignRouterInfo (keys, BuildBody (MakeSpec (timestamp, "12345")));
		RouterInfo router (first.data (), first.size ());
		assert (!router.IsUnreachable ());

		const auto newer = SignRouterInfo (keys, BuildBody (MakeSpec (timestamp + 1, "12345")));
		assert (router.IsNewer (newer.data (), newer.size ()));
		const auto equal = SignRouterInfo (keys, BuildBody (MakeSpec (timestamp, "12345")));
		assert (!router.IsNewer (equal.data (), equal.size ()));
		const auto older = SignRouterInfo (keys, BuildBody (MakeSpec (timestamp - 1, "12345")));
		assert (!router.IsNewer (older.data (), older.size ()));
		assert (!router.IsNewer (newer.data (), 10));
		assert (!router.IsNewer (nullptr, 0));
	}

	// A valid newer body replaces the state and buffer, and marks the object
	// for persistence.
	{
		const auto first = SignRouterInfo (keys, BuildBody (MakeSpec (timestamp, "12345")));
		RouterInfo router (first.data (), first.size ());
		assert (!router.IsUnreachable ());
		router.SetUpdated (false);

		const auto second = SignRouterInfo (keys, BuildBody (MakeSpec (timestamp + 10, "22222", "OP")));
		assert (router.Update (second.data (), second.size ()));
		assert (!router.IsUnreachable ());
		assert (router.IsUpdated ());
		ExpectSameState (router, timestamp + 10, 22222, 25000);
		assert (router.IsExtraBandwidth ());
		assert (router.GetBufferLen () == second.size ());
		assert (std::memcmp (router.GetBuffer (), second.data (), second.size ()) == 0);

		// A signed body with trailing bytes is malformed. Update() must fail
		// without touching the state, the buffer or the updated flag.
		router.SetUpdated (false);
		auto bad = MakeSpec (timestamp + 20, "33333");
		bad.trailingByte = true;
		const auto malformed = SignRouterInfo (keys, BuildBody (bad));
		assert (!router.Update (malformed.data (), malformed.size ()));
		assert (!router.IsUpdated ());
		ExpectSameState (router, timestamp + 10, 22222, 25000);
		assert (router.GetBufferLen () == second.size ());
		assert (std::memcmp (router.GetBuffer (), second.data (), second.size ()) == 0);

		// Signed body with a wrong netId is syntactically valid but not
		// suitable for this network. It must not evict the cached entry.
		auto wrongNetId = MakeSpec (timestamp + 30, "44444");
		wrongNetId.properties[1].second = std::to_string (i2p::context.GetNetID () + 1);
		const auto netIdBuffer = SignRouterInfo (keys, BuildBody (wrongNetId));
		assert (!router.Update (netIdBuffer.data (), netIdBuffer.size ()));
		ExpectSameState (router, timestamp + 10, 22222, 25000);

		// A body with no usable transport is rejected too.
		RouterSpec empty;
		empty.timestamp = timestamp + 40;
		empty.properties = StandardProperties ();
		const auto emptyBuffer = SignRouterInfo (keys, BuildBody (empty));
		assert (!router.Update (emptyBuffer.data (), emptyBuffer.size ()));
		ExpectSameState (router, timestamp + 10, 22222, 25000);
	}

	// Signature/identity binding: a valid RouterInfo for another identity and
	// an oversized buffer are both refused.
	{
		const auto first = SignRouterInfo (keys, BuildBody (MakeSpec (timestamp, "12345")));
		RouterInfo router (first.data (), first.size ());
		assert (!router.IsUnreachable ());

		auto otherKeys
			= PrivateKeys::CreateRandomKeys (SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519, CRYPTO_KEY_TYPE_ECIES_X25519_AEAD);
		const auto other = SignRouterInfo (otherKeys, BuildBody (MakeSpec (timestamp + 10, "22222")));
		assert (!router.Update (other.data (), other.size ()));
		ExpectSameState (router, timestamp, 12345, 25000);

		std::vector<uint8_t> oversized (MAX_RI_BUFFER_SIZE + 1, 0);
		assert (!router.Update (oversized.data (), oversized.size ()));
		assert (!router.Update (nullptr, 0));
		ExpectSameState (router, timestamp, 12345, 25000);

		// Flip one signature byte: verification fails and the old state stays.
		auto tampered = first;
		tampered.back () ^= 0x01;
		assert (!router.Update (tampered.data (), tampered.size ()));
		ExpectSameState (router, timestamp, 12345, 25000);
	}

	// Buffer ownership: CopyBuffer() is a deep copy, GetSharedBuffer() aliases
	// the internal buffer, DeleteBuffer() releases it, LoadBuffer() reloads the
	// persisted bytes without re-parsing the object.
	{
		const auto buffer = SignRouterInfo (keys, BuildBody (MakeSpec (timestamp, "12345")));
		RouterInfo router (buffer.data (), buffer.size ());
		assert (!router.IsUnreachable ());

		auto shared = router.GetSharedBuffer ();
		assert (shared);
		assert (shared->data () == router.GetBuffer ());

		auto copy = router.CopyBuffer ();
		assert (copy);
		assert (copy != shared);
		assert (copy->GetBufferLen () == router.GetBufferLen ());
		assert (std::memcmp (copy->data (), router.GetBuffer (), router.GetBufferLen ()) == 0);
		copy->data ()[0] ^= 0xFF; // must not alias
		assert (router.GetBuffer ()[0] != copy->data ()[0]);

		const auto path = TemporaryFilePath ();
		assert (router.SaveToFile (path.string ()));

		auto snapshot = router.GetSharedBuffer ();
		assert (snapshot && snapshot->GetBufferLen () == buffer.size ());
		router.DeleteBuffer ();
		assert (router.GetBuffer () == nullptr);
		assert (router.GetBufferLen () == 0);
		// A caller that took GetSharedBuffer() keeps a valid owned snapshot
		// after DeleteBuffer(); this is the invariant used by DatabaseStore
		// serialization.
		assert (snapshot->GetBufferLen () == buffer.size ());
		assert (snapshot->data () != nullptr);
		assert (router.GetSSU2V4Address () != nullptr); // parsed state remains

		assert (router.LoadBuffer (path.string ()) != nullptr);
		assert (router.GetBufferLen () == buffer.size ());
		assert (std::memcmp (router.GetBuffer (), buffer.data (), buffer.size ()) == 0);
		std::filesystem::remove (path);
	}

	// File round-trip: the constructor now verifies the signature on load
	// (Java PersistentDataStore.readBytes(in, true)). A corrupted file must be
	// rejected instead of being inserted into the netDb.
	{
		const auto buffer = SignRouterInfo (keys, BuildBody (MakeSpec (timestamp, "12345")));
		RouterInfo original (buffer.data (), buffer.size ());
		assert (!original.IsUnreachable ());

		const auto path = TemporaryFilePath ();
		assert (original.SaveToFile (path.string ()));

		RouterInfo loaded (path.string ());
		assert (!loaded.IsUnreachable ());
		assert (loaded.GetIdentHash () == original.GetIdentHash ());
		ExpectSameState (loaded, timestamp, 12345, 25000);

		auto corrupted = buffer;
		corrupted.back () ^= 0x80;
		{
			std::ofstream out (path.string (), std::ofstream::binary | std::ofstream::trunc);
			assert (out.is_open ());
			out.write (reinterpret_cast<const char *> (corrupted.data ()), corrupted.size ());
		}
		RouterInfo bad (path.string ());
		assert (bad.IsUnreachable ());

		// Truncated files are rejected by the size check.
		{
			std::ofstream out (path.string (), std::ofstream::binary | std::ofstream::trunc);
			out.write (reinterpret_cast<const char *> (buffer.data ()), 20);
		}
		RouterInfo truncated (path.string ());
		assert (truncated.IsUnreachable ());
		std::filesystem::remove (path);
	}

	// Local generation: a firewalled NTCP2 address with caps "46" must become
	// two independent objects. Mutating the v4 endpoint must not change the v6
	// endpoint. The serialized buffer must still contain both families and the
	// RouterAddress expiration must be big-endian.
	{
		LocalRouterInfo local;
		local.SetRouterIdentity (keys.GetPublic ());
		const uint8_t staticKey[32] = { 0x11 };
		const uint8_t iv[16] = { 0x22 };
		local.AddNTCP2Address (staticKey, iv, 12345, RouterInfo::AddressCaps::eV4 | RouterInfo::AddressCaps::eV6);
		auto addresses = local.GetAddresses ();
		assert (addresses);
		auto v4 = (*addresses)[RouterInfo::eNTCP2V4Idx];
		auto v6 = (*addresses)[RouterInfo::eNTCP2V6Idx];
		assert (v4 && v6);
		assert (v4 != v6); // distinct per-family objects

		// A later v4 host update must not leak into the v6 slot.
		v4->host = boost::asio::ip::make_address ("8.8.4.4");
		assert (v6->host.is_unspecified ());
		assert (v6->IsV6 ());
		assert (!v6->IsV4 ());

		// The local writer emits the RouterAddress expiration as a 64-bit
		// big-endian value.
		const uint64_t date = 0x0102030405060708ULL;
		v4->date = date;

		local.SetProperty ("netId", std::to_string (i2p::context.GetNetID ()));
		local.SetProperty ("router.version", "2.50.0");
		local.SetProperty ("caps", "L");
		local.CreateBuffer (keys);
		auto buffer = local.GetSharedBuffer ();
		assert (buffer && buffer->GetBufferLen () > 0);

		RouterInfo parsed (buffer->data (), buffer->GetBufferLen ());
		assert (!parsed.IsUnreachable ());
		assert (parsed.GetNTCP2V4Address () != nullptr);
		assert (parsed.GetNTCP2V6Address () != nullptr);
		assert (parsed.GetNTCP2V4Address ()->date == date);
		assert (parsed.GetNTCP2V6Address ()->IsV6 ());
		assert (!parsed.GetNTCP2V6Address ()->IsV4 ());
		// Firewalled addresses do not serialize host, but the v4 mutation must
		// not have turned the v6 entry into a v4 one.
		assert (parsed.GetNTCP2V4Address ()->IsV4 ());
		assert (parsed.GetNTCP2V6Address ()->host.is_unspecified ());

		// The two serialized NTCP2 entries are independent; the writer must
		// not drop the v6 address because of pointer equality anymore.
		assert (parsed.GetAddresses ());
	}

	// An unmodified dual-stack firewalled NTCP2 endpoint is still serialized as
	// one RouterAddress with caps "46" (the pre-split bytes), even though the
	// in-memory state uses two independent objects.
	{
		LocalRouterInfo local;
		local.SetRouterIdentity (keys.GetPublic ());
		const uint8_t staticKey[32] = { 0x11 };
		const uint8_t iv[16] = { 0x22 };
		local.AddNTCP2Address (staticKey, iv, 12345, RouterInfo::AddressCaps::eV4 | RouterInfo::AddressCaps::eV6);
		local.SetProperty ("netId", std::to_string (i2p::context.GetNetID ()));
		local.SetProperty ("router.version", "2.50.0");
		local.SetProperty ("caps", "L");
		local.CreateBuffer (keys);
		auto buffer = local.GetSharedBuffer ();
		assert (buffer && buffer->GetBufferLen () > 0);
		size_t ntcp2Entries = 0;
		// Length-prefixed transport-style string ("\5NTCP2"); an unanchored
		// "NTCP2" search could match random identity bytes.
		const char needle[] = "\x05"
							  "NTCP2";
		for (size_t i = 0; i + sizeof (needle) - 1 <= buffer->GetBufferLen (); i++)
			if (std::memcmp (buffer->data () + i, needle, sizeof (needle) - 1) == 0) ntcp2Entries++;
		assert (ntcp2Entries == 1);
		RouterInfo parsed (buffer->data (), buffer->GetBufferLen ());
		assert (!parsed.IsUnreachable ());
		assert (parsed.GetNTCP2V4Address () != nullptr);
		assert (parsed.GetNTCP2V6Address () != nullptr);
	}

	// Local SSU2 introducers: indices are limited to 0..2 and tag 0 is not a
	// valid introducer (Java UDPAddress requires tag > 0).
	{
		LocalRouterInfo local;
		local.SetRouterIdentity (keys.GetPublic ());
		const uint8_t staticKey[32] = { 0x11 };
		const uint8_t introKey[32] = { 0x22 };
		local.AddSSU2Address (staticKey, introKey, 12345, RouterInfo::AddressCaps::eV4);
		assert (local.GetSSU2V4Address () != nullptr);

		RouterInfo::Introducer introducer;
		introducer.iExp = static_cast<uint32_t> (i2p::util::GetSecondsSinceEpoch () + 3600);
		introducer.iTag = 0;
		introducer.iH.Fill (0x10);
		assert (!local.AddSSU2Introducer (introducer, true));

		int accepted = 0;
		for (uint32_t i = 1; i <= MAX_RI_INTRODUCERS + 1; i++)
		{
			introducer.iTag = i;
			introducer.iH.Fill (static_cast<uint8_t> (0x10 + i));
			if (local.AddSSU2Introducer (introducer, true)) accepted++;
		}
		assert (accepted == MAX_RI_INTRODUCERS);
		auto address = local.GetSSU2V4Address ();
		assert (address);
		assert (address->ssu);
		assert (address->ssu->introducers.size () == MAX_RI_INTRODUCERS);
	}

	return 0;
}

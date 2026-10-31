/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: netDb RouterInfo admission bounds.
 *
 * Checks the hard store cap for new unsolicited RouterInfos (an update
 * of an existing key and a reseed insert are still allowed), the
 * transport flood admission (only a floodfill forwards a requested
 * flood), and that router selection never creates or loads a peer
 * profile while holding a container lock. Deterministic, no sockets,
 * threads or sleeps.
 *
 */

#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include "Blocklist.h"
#include "I2PEndian.h"
#include "Identity.h"
#include "NetDb.hpp"
#include "Profiling.h"
#include "RouterContext.h"
#include "RouterInfo.h"
#include "Timestamp.h"

namespace i2p::data
{
// The friend declared by NetDb; the test drives only deterministic
// helpers.
struct NetDbLifecycleTestAccess
{
	// Fill the RouterInfo map with placeholder slots so the hard cap can
	// be reached without signing hundreds of descriptors. A null slot is
	// an ordinary "no cached object" entry for every reader.
	static void FillWithPlaceholders (NetDb & database, size_t total)
	{
		std::lock_guard lock (database.m_RouterInfosMutex);
		uint32_t seed = 0x1000;
		while (database.m_RouterInfos.size () < total)
		{
			IdentHash hash;
			hash.Fill (0);
			htobe32buf (hash, seed++);
			database.m_RouterInfos.emplace (hash, nullptr);
		}
	}
	static void InsertFloodfill (NetDb & database, const std::shared_ptr<RouterInfo> & router)
	{
		std::lock_guard lock (database.m_FloodfillsMutex);
		database.m_Floodfills.Insert (router);
	}
	static void SetReseeding (NetDb & database, bool reseeding)
	{
		database.m_IsReseeding.store (reseeding, std::memory_order_relaxed);
	}
};
}

namespace
{
using namespace i2p;
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

// Minimal signed RouterInfo: no addresses (storable inside the
// 54-minute grace), a valid netId and router.version, and the requested
// caps. The RouterInfo parser requires exactly this shape.
std::vector<uint8_t> BuildRouterInfo (const PrivateKeys & keys, uint64_t published, const char * caps)
{
	std::vector<uint8_t> body;
	AppendBe64 (body, published);
	body.push_back (0); // zero addresses
	body.push_back (0); // zero peer hashes

	std::string properties;
	AppendParam (properties, "caps", caps);
	AppendParam (properties, "netId", std::to_string (i2p::context.GetNetID ()));
	AppendParam (properties, "router.version", "2.14.0");
	AppendBe16 (body, static_cast<uint16_t> (properties.size ()));
	body.insert (body.end (), properties.begin (), properties.end ());

	std::vector<uint8_t> buffer;
	auto identity = keys.GetPublic ();
	assert (identity);
	const auto identityOffset = buffer.size ();
	buffer.resize (identityOffset + identity->GetFullLen ());
	assert (identity->ToBuffer (buffer.data () + identityOffset, identity->GetFullLen ()) == identity->GetFullLen ());
	buffer.insert (buffer.end (), body.begin (), body.end ());

	std::vector<uint8_t> signature (identity->GetSignatureLen ());
	keys.Sign (buffer.data (), static_cast<int> (buffer.size ()), signature.data ());
	buffer.insert (buffer.end (), signature.begin (), signature.end ());
	return buffer;
}

void TestRouterInfoStoreCap ()
{
	auto keys
		= PrivateKeys::CreateRandomKeys (SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519, CRYPTO_KEY_TYPE_ECIES_X25519_AEAD);
	auto ident = keys.GetPublic ()->GetIdentHash ();
	const auto now = i2p::util::GetMillisecondsSinceEpoch ();

	NetDb database;
	auto original = BuildRouterInfo (keys, now - 60 * 1000, "O");
	assert (database.AddRouterInfo (ident, original.data (), original.size ()));
	assert (database.GetNumRouters () == 1);

	// The global test context is not a floodfill, so the ordinary cap
	// applies.
	using Access = i2p::data::NetDbLifecycleTestAccess;
	Access::FillWithPlaceholders (database, NETDB_MAX_ROUTER_INFOS);
	assert (database.GetNumRouters () == static_cast<int> (NETDB_MAX_ROUTER_INFOS));

	auto otherKeys
		= PrivateKeys::CreateRandomKeys (SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519, CRYPTO_KEY_TYPE_ECIES_X25519_AEAD);
	auto other = BuildRouterInfo (otherKeys, now, "O");
	bool updated = false;
	assert (!database.AddRouterInfo (other.data (), other.size (), updated));
	assert (!updated);
	assert (database.GetNumRouters () == static_cast<int> (NETDB_MAX_ROUTER_INFOS));

	// An update of an already stored key is never dropped by the cap.
	auto newer = BuildRouterInfo (keys, now, "O");
	updated = false;
	assert (database.AddRouterInfo (newer.data (), newer.size (), updated));
	assert (updated);

	// A reseed insert bypasses the cap: the verified SU3 payload is
	// already a bounded source and bootstrap must not be starved by a
	// flooded map.
	Access::SetReseeding (database, true);
	updated = false;
	assert (database.AddRouterInfo (other.data (), other.size (), updated));
	assert (updated);
	assert (database.GetNumRouters () == static_cast<int> (NETDB_MAX_ROUTER_INFOS) + 1);
	Access::SetReseeding (database, false);
}

void TestTransportFloodAdmission ()
{
	// Java PeerState.gotRI/PeerState2.gotRI: only a floodfill forwards a
	// received RouterInfo, and never a blocklisted/banned key.
	auto keys
		= PrivateKeys::CreateRandomKeys (SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519, CRYPTO_KEY_TYPE_ECIES_X25519_AEAD);
	auto ident = keys.GetPublic ()->GetIdentHash ();

	assert (!IsRouterInfoFloodAdmitted (false, ident));
	assert (IsRouterInfoFloodAdmitted (true, ident));

	assert (AddRuntimeRouterBan (ident));
	assert (!IsRouterInfoFloodAdmitted (true, ident));
	assert (RemoveRuntimeRouterBan (ident));
	assert (IsRouterInfoFloodAdmitted (true, ident));

	NetDb database;
	const auto now = i2p::util::GetMillisecondsSinceEpoch ();
	auto buffer = BuildRouterInfo (keys, now, "O");
	assert (database.AddRouterInfo (ident, buffer.data (), buffer.size ()));
	auto stored = database.FindRouter (ident);
	assert (stored);

	// The transport path refuses to flood while this router is not a
	// floodfill; the DatabaseStore path keeps its own accounting and is
	// not asserted here.
	assert (!database.FloodRouterInfoConditional (stored));
	assert (!database.FloodRouterInfoConditional (nullptr));
	// A banned key is refused by the unconditional path as well.
	assert (AddRuntimeRouterBan (ident));
	assert (!database.FloodRouterInfo (stored));
	assert (RemoveRuntimeRouterBan (ident));
}

void TestSelectionDoesNotLoadProfiles ()
{
	auto keys
		= PrivateKeys::CreateRandomKeys (SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519, CRYPTO_KEY_TYPE_ECIES_X25519_AEAD);
	auto ident = keys.GetPublic ()->GetIdentHash ();
	const auto now = i2p::util::GetMillisecondsSinceEpoch ();

	NetDb database;
	auto buffer = BuildRouterInfo (keys, now - 60 * 1000, "O");
	assert (database.AddRouterInfo (ident, buffer.data (), buffer.size ()));
	auto stored = database.FindRouter (ident);
	assert (stored);
	assert (!stored->HasProfile ());
	i2p::data::NetDbLifecycleTestAccess::InsertFloodfill (database, stored);
	assert (database.GetNumFloodfills () == 1);

	std::atomic<int> profileLoads { 0 };
	SetRouterProfileLoadObserverForTesting (
		[&profileLoads, &ident] (const IdentHash & hash)
		{
			if (hash == ident) ++profileLoads;
		});

	IdentHash target;
	target.Fill (0x5A);
	{
		std::unordered_set<IdentHash> excluded;
		auto closest = database.GetClosestFloodfill (target, excluded);
		assert (closest == stored);
		assert (profileLoads.load () == 0);
		assert (!stored->HasProfile ());
	}
	{
		std::unordered_set<IdentHash> excluded;
		auto closest = database.GetClosestFloodfills (target, 3, excluded);
		assert (closest.size () == 1);
		assert (closest.front () == ident);
		assert (profileLoads.load () == 0);
		assert (!stored->HasProfile ());
	}
	// Random selection applies the same filter map; it must not create a
	// profile either.
	for (int i = 0; i < 8; ++i) (void)database.GetRandomRouter ();
	assert (profileLoads.load () == 0);
	assert (!stored->HasProfile ());

	// Creating a profile is an explicit operation outside the selection
	// path.
	auto profile = stored->GetProfile ();
	assert (profile);
	assert (profileLoads.load () == 1);
	assert (stored->HasProfile ());
	SetRouterProfileLoadObserverForTesting (nullptr);
}
}

int main ()
{
	TestRouterInfoStoreCap ();
	TestTransportFloodAdmission ();
	TestSelectionDoesNotLoadProfiles ();
	return 0;
}

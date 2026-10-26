/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: test address-book negative cache.
 *
 * Offline checks for the bounded negative cache that suppresses repeated
 * FindAddress/LookupAddress attempts for an unknown name: the 60-second
 * lifetime, the 32-entry bound, the clear on add/remove, the removal on a
 * successful lookup and the rule that a blinded (b33) label is never cached.
 * No storage directory, resolver, destination, socket, thread or timer is
 * started.
 */

#include <cassert>
#include <cstdint>
#include <string>

#include "AddressBook.h"
#include "Blinding.h"
#include "Identity.h"

namespace
{
using Cache = i2p::client::detail::NegativeAddressCache;

i2p::data::PrivateKeys MakeKeys ()
{
	return i2p::data::PrivateKeys::CreateRandomKeys (
		i2p::data::SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519, i2p::data::CRYPTO_KEY_TYPE_ECIES_X25519_AEAD, true);
}

std::string Base64 (const i2p::data::PrivateKeys & keys)
{
	return keys.GetPublic ()->ToBase64 ();
}

std::string MakeB33 (const i2p::data::PrivateKeys & keys)
{
	i2p::data::BlindedPublicKey blinded (keys.GetPublic ());
	assert (blinded.IsValid ());
	const std::string b33 = blinded.ToB33 ();
	assert (b33.size () > i2p::client::B33_ADDRESS_THRESHOLD);
	return b33;
}

class MemoryStorage final : public i2p::client::AddressBookStorage
{
public:
	std::shared_ptr<const i2p::data::IdentityEx> GetAddress (const i2p::data::IdentHash &) override { return nullptr; }
	void AddAddress (std::shared_ptr<const i2p::data::IdentityEx>) override { }
	void RemoveAddress (const i2p::data::IdentHash &) override { }
	void CleanUpCache () override { }
	bool Init () override { return true; }
	int Load (Addresses &) override { return 0; }
	int LoadLocal (Addresses &) override { return 0; }
	int Save (const Addresses & addresses) override { return static_cast<int> (addresses.size ()); }
	void SaveEtag (const i2p::data::IdentHash &, const std::string &, const std::string &) override { }
	bool GetEtag (const i2p::data::IdentHash &, std::string &, std::string &) override { return false; }
	void ResetEtags () override { }
};

void TestHitAndExpiry ()
{
	Cache cache;
	assert (cache.Size (100) == 0);
	assert (!cache.IsCached ("absent.example.i2p", 100));

	cache.Insert ("Absent.Example.I2P", 100);
	// A hit inside the lifetime lets the caller skip the second lookup, and the
	// key is the normalised name.
	assert (cache.Size (100) == 1);
	assert (cache.IsCached ("absent.example.i2p", 100));
	assert (cache.IsCached ("ABSENT.EXAMPLE.I2P", 100));
	assert (cache.IsCached ("absent.example.i2p", 100 + Cache::TIMEOUT_SECONDS - 1));

	// At the deadline the miss may be retried.
	assert (!cache.IsCached ("absent.example.i2p", 100 + Cache::TIMEOUT_SECONDS));
	assert (cache.Size (100 + Cache::TIMEOUT_SECONDS) == 0);

	// A monotonic-clock reset fails closed instead of pinning the entry.
	cache.Insert ("reset.example.i2p", 500);
	assert (!cache.IsCached ("reset.example.i2p", 100));
	assert (cache.Size (100) == 0);
}

void TestBound ()
{
	Cache cache;
	for (size_t i = 0; i < Cache::MAX_ENTRIES; i++) cache.Insert (std::to_string (i) + ".example.i2p", 1000);
	assert (cache.Size (1000) == Cache::MAX_ENTRIES);

	// One more miss evicts exactly one entry instead of growing the table.
	cache.Insert ("extra.example.i2p", 1001);
	assert (cache.Size (1001) == Cache::MAX_ENTRIES);
	assert (cache.IsCached ("extra.example.i2p", 1001));
	size_t remaining = 0;
	for (size_t i = 0; i < Cache::MAX_ENTRIES; i++)
		if (cache.IsCached (std::to_string (i) + ".example.i2p", 1001)) remaining++;
	assert (remaining == Cache::MAX_ENTRIES - 1);

	// Re-inserting a cached name refreshes it without evicting another entry.
	cache.Insert ("extra.example.i2p", 1002);
	assert (cache.Size (1002) == Cache::MAX_ENTRIES);
	assert (cache.IsCached ("extra.example.i2p", 1002 + Cache::TIMEOUT_SECONDS - 1));
}

void TestNeverCacheBlindedLabel ()
{
	auto keys = MakeKeys ();
	const std::string b33 = MakeB33 (keys);

	// A b33 name is decoded locally, so it must never be remembered as a miss.
	assert (!Cache::IsCacheable (b33 + ".i2p"));
	assert (!Cache::IsCacheable (b33 + ".b33.i2p"));
	assert (!Cache::IsCacheable (b33 + ".b33.i2p.alt"));
	assert (Cache::IsCacheable ("example.i2p"));
	assert (!Cache::IsCacheable (""));
	assert (!Cache::IsCacheable (std::string (256, 'a')));

	Cache cache;
	cache.Insert (b33 + ".i2p", 100);
	cache.Insert (b33 + ".b33.i2p", 100);
	assert (cache.Size (100) == 0);
	assert (!cache.IsCached (b33 + ".i2p", 100));
	assert (!cache.IsCached (b33 + ".b33.i2p", 100));
}

void TestEraseAndClear ()
{
	Cache cache;
	cache.Insert ("one.example.i2p", 100);
	cache.Insert ("two.example.i2p", 100);
	assert (cache.Size (100) == 2);
	// A successful lookup answers exactly its own name.
	cache.Erase ("ONE.example.i2p");
	assert (!cache.IsCached ("one.example.i2p", 100));
	assert (cache.IsCached ("two.example.i2p", 100));
	cache.Clear ();
	assert (cache.Size (100) == 0);
}

void TestBookIntegration ()
{
	auto keys = MakeKeys ();
	const auto hash = keys.GetPublic ()->GetIdentHash ();

	auto storage = std::make_unique<MemoryStorage> ();
	i2p::client::AddressBook book (std::move (storage));
	assert (book.GetNegativeCacheSize () == 0);

	// A miss is remembered once: polling the same unknown name does not grow the
	// table (the second call is served by the cache instead of a new lookup).
	assert (!book.GetAddress ("absent.example.i2p"));
	assert (book.GetNegativeCacheSize () == 1);
	assert (!book.GetAddress ("absent.example.i2p"));
	assert (book.GetNegativeCacheSize () == 1);

	// A local add clears the cached miss and the name resolves immediately.
	using Result = i2p::client::AddressAddResult;
	assert (book.AddAddressChecked ("absent.example.i2p", Base64 (keys), false) == Result::Ok);
	assert (book.GetNegativeCacheSize () == 0);
	auto resolved = book.GetAddress ("absent.example.i2p");
	assert (resolved);
	assert (resolved->identHash == hash);
	assert (book.GetNegativeCacheSize () == 0);

	// A removal clears the cache again.
	assert (!book.GetAddress ("other.example.i2p"));
	assert (book.GetNegativeCacheSize () == 1);
	assert (book.RemoveAddress ("absent.example.i2p") == i2p::client::AddressBookMutationResult::Ok);
	assert (book.GetNegativeCacheSize () == 0);

	// A b33 name and a rejected b33 name are never cached: both are decided
	// locally and must stay resolvable.
	const std::string b33 = MakeB33 (keys);
	auto blinded = book.GetAddress (b33 + ".i2p");
	assert (blinded);
	assert (!blinded->IsIdentHash ());
	assert (book.GetNegativeCacheSize () == 0);
	assert (!book.GetAddress ("abcdefghijklmnop.b33.i2p"));
	assert (book.GetNegativeCacheSize () == 0);
}
} // namespace

int main ()
{
	TestHitAndExpiry ();
	TestBound ();
	TestNeverCacheBlindedLabel ();
	TestEraseAndClear ();
	TestBookIntegration ();
	return 0;
}

/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: test address-book add and feed actions.
 *
 * Offline checks for the administrative add result (Exists without
 * replace), the Java destination-length bounds, the name rules that
 * reject the doubled hyphen and the 52-character base32 lookalike, and
 * the subscription "action=delete" retraction: only the feed that
 * supplied a name may retract it, and a locally curated name is never
 * deleted by a feed. No storage directory, resolver, destination,
 * socket, thread or timer is started.
 */

#include <cassert>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "AddressBook.h"
#include "Identity.h"

namespace
{
class FakeStorage final : public i2p::client::AddressBookStorage
{
public:
	std::shared_ptr<const i2p::data::IdentityEx> GetAddress (const i2p::data::IdentHash & ident) override
	{
		auto it = identities.find (ident);
		return it == identities.end () ? nullptr : it->second;
	}

	void AddAddress (std::shared_ptr<const i2p::data::IdentityEx> address) override
	{
		if (address) identities[address->GetIdentHash ()] = std::move (address);
	}

	void RemoveAddress (const i2p::data::IdentHash & ident) override { identities.erase (ident); }
	void CleanUpCache () override { }
	bool Init () override { return true; }
	int Load (Addresses &) override { return 0; }
	int LoadLocal (Addresses &) override { return 0; }
	int Save (const Addresses & addresses) override
	{
		saveCount++;
		return static_cast<int> (addresses.size ());
	}
	void SaveEtag (const i2p::data::IdentHash &, const std::string &, const std::string &) override { }
	bool GetEtag (const i2p::data::IdentHash &, std::string &, std::string &) override { return false; }
	void ResetEtags () override { }

	// Tombstones and entry origins are held in memory so the merge path
	// can be tested without creating a data directory.
	bool LoadDeletedHosts (std::vector<std::string> & patterns) override
	{
		patterns = deleted;
		return true;
	}
	bool SaveDeletedHosts (const std::vector<std::string> & patterns) override
	{
		deleted = patterns;
		return true;
	}
	bool LoadAddressSources (std::map<std::string, std::string, std::less<>> & loaded) override
	{
		loaded = sources;
		return true;
	}
	bool SaveAddressSources (const std::map<std::string, std::string, std::less<>> & saved) override
	{
		sources = saved;
		return true;
	}

	std::map<i2p::data::IdentHash, std::shared_ptr<const i2p::data::IdentityEx>> identities;
	std::vector<std::string> deleted;
	std::map<std::string, std::string, std::less<>> sources;
	int saveCount = 0;
};

i2p::data::PrivateKeys MakeKeys ()
{
	return i2p::data::PrivateKeys::CreateRandomKeys (
		i2p::data::SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519, i2p::data::CRYPTO_KEY_TYPE_ECIES_X25519_AEAD, true);
}

std::string Base64 (const i2p::data::PrivateKeys & keys)
{
	return keys.GetPublic ()->ToBase64 ();
}

i2p::data::IdentHash Hash (const i2p::data::PrivateKeys & keys)
{
	return keys.GetPublic ()->GetIdentHash ();
}

void TestDestinationLengthBounds ()
{
	using i2p::client::IsValidDestinationLength;
	// Java bounds the base64 string before decoding it: 516..616
	// characters.
	assert (!IsValidDestinationLength (""));
	assert (!IsValidDestinationLength (std::string (515, 'A')));
	assert (IsValidDestinationLength (std::string (516, 'A')));
	assert (IsValidDestinationLength (std::string (615, 'A')));
	assert (IsValidDestinationLength (std::string (616, 'A')));
	assert (!IsValidDestinationLength (std::string (617, 'A')));
	// A real public identity serialises inside the bounds.
	auto keys = MakeKeys ();
	const auto encoded = Base64 (keys);
	assert (encoded.size () >= i2p::client::MIN_DESTINATION_BASE64_LENGTH);
	assert (encoded.size () <= i2p::client::MAX_DESTINATION_BASE64_LENGTH);
	assert (IsValidDestinationLength (encoded));
}

void TestNameRules ()
{
	using i2p::client::IsValidHostName;
	// A label may not contain a doubled hyphen (IDNA "xn--" prefix
	// position).
	assert (!IsValidHostName ("foo--bar.i2p"));
	assert (!IsValidHostName ("xn--site.i2p"));
	assert (IsValidHostName ("foo-bar.i2p"));
	// A final label of exactly 52 characters is the base32-hash lookalike.
	assert (!IsValidHostName (std::string (52, 'a') + ".i2p"));
	assert (IsValidHostName (std::string (51, 'a') + ".i2p"));
	// The rest of the Java rules are unchanged.
	assert (IsValidHostName ("sub.example.i2p"));
	assert (IsValidHostName ("host-01.i2p"));
	assert (!IsValidHostName ("proxy.i2p"));
	assert (!IsValidHostName ("a..b.i2p"));
	assert (!IsValidHostName ("a_-b.i2p"));
}

void TestAddAddressChecked ()
{
	auto keyA = MakeKeys ();
	auto keyB = MakeKeys ();
	const auto hashA = Hash (keyA);
	const auto hashB = Hash (keyB);

	auto storage = std::make_unique<FakeStorage> ();
	auto * fake = storage.get ();
	i2p::client::AddressBook book (std::move (storage));

	using Result = i2p::client::AddressAddResult;
	assert (book.AddAddressChecked ("site.i2p", Base64 (keyA), false) == Result::Ok);
	assert (book.FindAddress ("site.i2p")->identHash == hashA);
	const int saves = fake->saveCount;

	// Without replace an existing destination is kept and nothing is
	// written.
	assert (book.AddAddressChecked ("site.i2p", Base64 (keyB), false) == Result::Exists);
	assert (fake->saveCount == saves);
	assert (book.FindAddress ("site.i2p")->identHash == hashA);
	assert (!fake->identities.contains (hashB));
	assert (fake->identities.contains (hashA));

	// With replace the operator re-points the name and the old identity
	// file is no longer referenced.
	assert (book.AddAddressChecked ("site.i2p", Base64 (keyB), true) == Result::Ok);
	assert (book.FindAddress ("site.i2p")->identHash == hashB);
	assert (fake->identities.contains (hashB));
	assert (!fake->identities.contains (hashA));

	// Repeating the stored destination is a no-op, not Exists, for either
	// mode.
	assert (book.AddAddressChecked ("site.i2p", Base64 (keyB), false) == Result::Ok);
	assert (book.AddAddressChecked ("site.i2p", Base64 (keyB), true) == Result::Ok);
	assert (fake->saveCount == saves + 1);

	// Rejections never mutate the entry.
	assert (book.AddAddressChecked ("proxy.i2p", Base64 (keyA), false) == Result::InvalidName);
	assert (book.AddAddressChecked ("bad--name.i2p", Base64 (keyA), false) == Result::InvalidName);
	assert (book.AddAddressChecked ("site.i2p", std::string (515, 'A'), true) == Result::InvalidDestination);
	assert (book.AddAddressChecked ("site.i2p", std::string (617, 'A'), true) == Result::InvalidDestination);
	assert (book.AddAddressChecked ("site.i2p", "", true) == Result::InvalidDestination);
	assert (book.FindAddress ("site.i2p")->identHash == hashB);
	assert (fake->saveCount == saves + 1);

	// The historical AddAddress contract is preserved for the existing
	// callers.
	using Mutate = i2p::client::AddressBookMutationResult;
	assert (book.AddAddress ("site.i2p", Base64 (keyA), false) == Mutate::Exists);
	assert (book.AddAddress ("site.i2p", Base64 (keyA), true) == Mutate::Ok);
	assert (book.AddAddress ("proxy.i2p", Base64 (keyA), true) == Mutate::InvalidName);
	assert (book.AddAddress ("site.i2p", "not-base64", true) == Mutate::InvalidDestination);
}

void TestFeedRetraction ()
{
	auto feedKey = MakeKeys ();
	auto otherKey = MakeKeys ();
	auto localKey = MakeKeys ();
	const auto feedHash = Hash (feedKey);

	auto storage = std::make_unique<FakeStorage> ();
	auto * fake = storage.get ();
	i2p::client::AddressBook book (std::move (storage));

	// A locally curated name and a name supplied by a feed.
	assert (book.AddAddressChecked ("local.i2p", Base64 (localKey), false) == i2p::client::AddressAddResult::Ok);
	std::istringstream first ("fromfeed.i2p=" + Base64 (feedKey) + "\n");
	assert (book.LoadHostsFromStream (std::move (first), true, "stats.i2p"));
	assert (book.FindAddress ("fromfeed.i2p"));
	// Only the feed-supplied name has a recorded origin.
	assert (fake->sources.count ("fromfeed.i2p") == 1);
	assert (fake->sources.at ("fromfeed.i2p") == "stats.i2p");
	assert (!fake->sources.contains ("local.i2p"));

	// A locally added name is never deleted by a feed, even if the feed
	// offers
	// the same destination the operator stored.
	std::istringstream localDelete ("local.i2p=" + Base64 (localKey) + "#!action=delete\n");
	assert (book.LoadHostsFromStream (std::move (localDelete), true, "stats.i2p"));
	assert (book.FindAddress ("local.i2p"));

	// Another feed cannot retract a name it did not supply.
	std::istringstream otherDelete ("fromfeed.i2p=" + Base64 (feedKey) + "#!action=delete\n");
	assert (book.LoadHostsFromStream (std::move (otherDelete), true, "other.i2p"));
	assert (book.FindAddress ("fromfeed.i2p"));

	// "action=update" and unknown keys stay a no-op, and the entry is
	// merged.
	std::istringstream update ("updated.i2p=" + Base64 (feedKey) + "#!action=update,updated=now\n");
	assert (book.LoadHostsFromStream (std::move (update), true, "stats.i2p"));
	assert (book.FindAddress ("updated.i2p"));

	// An over-long metainfo block is treated as comment text: the line is
	// still a normal entry and no action is honoured.
	std::istringstream oversize (
		"oversize.i2p=" + Base64 (feedKey) + "#!action=delete," + std::string (400, 'x') + "\n");
	assert (book.LoadHostsFromStream (std::move (oversize), true, "stats.i2p"));
	assert (book.FindAddress ("oversize.i2p"));

	// The originating feed retracts its own name: the entry and its
	// identity
	// file are gone, and a tombstone keeps another feed from resurrecting
	// it.
	std::istringstream retract ("fromfeed.i2p=" + Base64 (feedKey) + "#!action=delete\n");
	assert (book.LoadHostsFromStream (std::move (retract), true, "stats.i2p"));
	assert (!book.FindAddress ("fromfeed.i2p"));
	assert (!fake->identities.contains (feedHash));
	assert (fake->sources.count ("fromfeed.i2p") == 0);
	auto deleted = book.GetDeletedHosts (10);
	assert (deleted.size () == 1 && deleted[0] == "fromfeed.i2p");

	std::istringstream resurrect ("fromfeed.i2p=" + Base64 (feedKey) + "\n");
	assert (book.LoadHostsFromStream (std::move (resurrect), true, "other.i2p"));
	assert (!book.FindAddress ("fromfeed.i2p"));

	// A destination with the wrong length is rejected without logging it,
	// and the rest of the source is still merged.
	std::istringstream badDestination (
		"short.i2p=" + std::string (515, 'A') + "\n" + "good.i2p=" + Base64 (otherKey) + "\n");
	assert (book.LoadHostsFromStream (std::move (badDestination), true, "stats.i2p"));
	assert (!book.FindAddress ("short.i2p"));
	assert (book.FindAddress ("good.i2p"));
}

void TestRetractionOutsideFeed ()
{
	auto key = MakeKeys ();
	auto storage = std::make_unique<FakeStorage> ();
	auto book = std::make_unique<i2p::client::AddressBook> (std::move (storage));

	// A startup hosts.txt load is not a feed merge, so a retraction
	// request can never be honoured there. The line is skipped instead of
	// being merged as a new destination; every other line of the same file
	// is still applied.
	std::istringstream startup ("plain.i2p=" + Base64 (key) + "#!action=delete\n" + "kept.i2p=" + Base64 (key) + "\n");
	assert (book->LoadHostsFromStream (std::move (startup), false));
	assert (!book->FindAddress ("plain.i2p"));
	assert (book->FindAddress ("kept.i2p"));
}
} // namespace

int main ()
{
	TestDestinationLengthBounds ();
	TestNameRules ();
	TestAddAddressChecked ();
	TestFeedRetraction ();
	TestRetractionOutsideFeed ();
	return 0;
}

/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: test address-book subscription merge precedence.
 *
 * Offline checks that an automatic subscription merge never replaces a
 * name that already had a destination, that duplicate lines inside one
 * source still follow last-wins, that declined entries are counted,
 * that tombstones still block a merge and that the .i2p.alt alias and
 * www. fallback are unaffected. No storage directory, resolver,
 * destination, socket, thread or timer is started; the merge helper is
 * exercised directly.
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
class MemoryStorage final : public i2p::client::AddressBookStorage
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
	// Tombstones are held in memory so the merge path can be tested
	// without creating a data directory.
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

	std::map<i2p::data::IdentHash, std::shared_ptr<const i2p::data::IdentityEx>> identities;
	std::vector<std::string> deleted;
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

void TestKnownNameKeepsLocalDestination ()
{
	auto local = MakeKeys ();
	auto remote = MakeKeys ();
	const auto localHash = Hash (local);
	const auto remoteHash = Hash (remote);
	assert (!(localHash == remoteHash));

	auto storage = std::make_unique<MemoryStorage> ();
	auto * fake = storage.get ();
	i2p::client::AddressBook book (std::move (storage));

	// Curated locally through the explicit add path; the original spelling
	// is normalised, the destination is not rewritten.
	book.InsertAddress ("Curated.Example.I2P", Base64 (local));
	assert (book.FindAddress ("curated.example.i2p"));
	assert (book.FindAddress ("curated.example.i2p")->identHash == localHash);
	assert (fake->identities.contains (localHash));
	assert (book.GetMergeConflicts () == 0);

	std::istringstream offersOverwrite ("curated.example.i2p=" + Base64 (remote) + "\n");
	assert (book.LoadHostsFromStream (std::move (offersOverwrite), true, "stats.i2p"));
	// The subscription did not take the name over and did not persist the
	// rejected identity.
	assert (book.FindAddress ("curated.example.i2p")->identHash == localHash);
	assert (!fake->identities.contains (remoteHash));
	assert (book.GetMergeConflicts () == 1);

	// Repeating the value the book already holds is not a conflict.
	std::istringstream offersSame ("curated.example.i2p=" + Base64 (local) + "\n");
	assert (book.LoadHostsFromStream (std::move (offersSame), true, "stats.i2p"));
	assert (book.FindAddress ("curated.example.i2p")->identHash == localHash);
	assert (book.GetMergeConflicts () == 1);

	// An explicit administrative replace is an operator action and still
	// wins.
	assert (
		book.AddAddress ("curated.example.i2p", Base64 (remote), true) == i2p::client::AddressBookMutationResult::Ok);
	assert (book.FindAddress ("curated.example.i2p")->identHash == remoteHash);
	assert (fake->identities.contains (remoteHash));
	assert (!fake->identities.contains (localHash));
}

void TestSourceAddsAndRefinesItsOwnNames ()
{
	auto first = MakeKeys ();
	auto second = MakeKeys ();
	const auto firstHash = Hash (first);
	const auto secondHash = Hash (second);

	// No storage: the merge only touches the in-memory map, which is the
	// same precedence decision an update from a subscription makes.
	i2p::client::AddressBook book;
	std::istringstream source ("fresh.i2p=" + Base64 (first) + "\n" + "dupe.i2p=" + Base64 (first) + "\n"
		+ "dupe.i2p=" + Base64 (second) + "\n");
	assert (book.LoadHostsFromStream (std::move (source), true, "stats.i2p"));
	assert (book.FindAddress ("fresh.i2p")->identHash == firstHash);
	// Last line wins inside one source (properties-style), and refining
	// its own
	// entry is not counted as a conflict against a locally known name.
	assert (book.FindAddress ("dupe.i2p")->identHash == secondHash);
	assert (book.GetMergeConflicts () == 0);

	// A later visit of the same source can no longer change those names.
	std::istringstream revisit ("dupe.i2p=" + Base64 (first) + "\n");
	assert (book.LoadHostsFromStream (std::move (revisit), true, "stats.i2p"));
	assert (book.FindAddress ("dupe.i2p")->identHash == secondHash);
	assert (book.GetMergeConflicts () == 1);
}

void TestTombstoneBlocksMerge ()
{
	auto key = MakeKeys ();
	const auto hash = Hash (key);

	auto storage = std::make_unique<MemoryStorage> ();
	auto * fake = storage.get ();
	i2p::client::AddressBook book (std::move (storage));

	assert (book.BlockAddress ("blocked.i2p") == i2p::client::AddressBookMutationResult::Ok);
	assert (fake->deleted.size () == 1);

	std::istringstream source ("blocked.i2p=" + Base64 (key) + "\n" + "allowed.i2p=" + Base64 (key) + "\n");
	assert (book.LoadHostsFromStream (std::move (source), true, "stats.i2p"));
	// The tombstone still wins; a skipped tombstone is not a conflict.
	assert (!book.FindAddress ("blocked.i2p"));
	assert (book.FindAddress ("allowed.i2p"));
	assert (book.FindAddress ("allowed.i2p")->identHash == hash);
	assert (book.GetMergeConflicts () == 0);
}

void TestAliasAndWwwFallbackUnaffected ()
{
	auto local = MakeKeys ();
	auto remote = MakeKeys ();
	const auto localHash = Hash (local);

	i2p::client::AddressBook book;
	book.InsertAddress ("Portal.Example.I2P", Base64 (local));

	std::istringstream source ("portal.example.i2p=" + Base64 (remote)
		+ "\n"
		  "other.example.i2p="
		+ Base64 (remote) + "\n");
	assert (book.LoadHostsFromStream (std::move (source), true, "stats.i2p"));
	assert (book.GetMergeConflicts () == 1);

	// RFC 9476 .i2p.alt stays a lookup-only alias of the untouched local
	// name.
	auto alt = book.GetAddress ("PORTAL.Example.I2P.ALT");
	assert (alt);
	assert (alt->identHash == localHash);
	// The Java www. fallback still uses the apex entry.
	auto www = book.GetAddress ("www.portal.example.i2p");
	assert (www);
	assert (www->identHash == localHash);
	// A new name from the same source is added normally.
	assert (book.FindAddress ("other.example.i2p"));
}

void TestConflictCounterIsPerEntry ()
{
	auto local = MakeKeys ();
	auto remote = MakeKeys ();
	const auto localHash = Hash (local);

	i2p::client::AddressBook book;
	book.InsertAddress ("kept.i2p", Base64 (local));
	assert (book.GetMergeConflicts () == 0);

	// Every differing offer for the known name is counted once, no matter
	// how
	// many log lines the bounded diagnostics print.
	std::string body;
	for (size_t i = 0; i < 32; i++) body += "kept.i2p=" + Base64 (remote) + "\n";
	std::istringstream source (body);
	assert (book.LoadHostsFromStream (std::move (source), true, "stats.i2p"));
	assert (book.GetMergeConflicts () == 32);
	assert (book.FindAddress ("kept.i2p")->identHash == localHash);
}
} // namespace

int main ()
{
	TestKnownNameKeepsLocalDestination ();
	TestSourceAddsAndRefinesItsOwnNames ();
	TestTombstoneBlocksMerge ();
	TestAliasAndWwwFallbackUnaffected ();
	TestConflictCounterIsPerEntry ();
	return 0;
}

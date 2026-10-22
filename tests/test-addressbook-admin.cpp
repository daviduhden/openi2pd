/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: test address-book administration.
 *
 * Offline checks for Java-compatible name validation, bounded add/replace/
 * remove semantics and shared-identity reference handling. No filesystem,
 * network, resolver or router is started.
 */

#include <cassert>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <utility>

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
	int Save (const Addresses & addresses) override { saveCount++; return static_cast<int> (addresses.size ()); }
	void SaveEtag (const i2p::data::IdentHash &, const std::string &, const std::string &) override { }
	bool GetEtag (const i2p::data::IdentHash &, std::string &, std::string &) override { return false; }
	void ResetEtags () override { }

	std::map<i2p::data::IdentHash, std::shared_ptr<const i2p::data::IdentityEx>> identities;
	int saveCount = 0;
};

std::string Base64 (const i2p::data::PrivateKeys & keys)
{
	return keys.GetPublic ()->ToBase64 ();
}

void TestNameValidation ()
{
	using i2p::client::IsValidHostName;
	assert (IsValidHostName ("site.i2p"));
	assert (IsValidHostName ("sub.example.i2p"));
	assert (IsValidHostName ("host-01.i2p"));
	assert (IsValidHostName ("MiXeD.Example.I2P"));
	assert (!IsValidHostName ("proxy.i2p"));
	assert (!IsValidHostName ("router.i2p"));
	assert (!IsValidHostName ("console.i2p"));
	assert (!IsValidHostName ("x.proxy.i2p"));
	assert (!IsValidHostName ("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa.b32.i2p"));
	assert (!IsValidHostName (".x.i2p"));
	assert (!IsValidHostName ("-x.i2p"));
	assert (!IsValidHostName ("a..b.i2p"));
	assert (!IsValidHostName ("a.-b.i2p"));
	assert (!IsValidHostName ("a_b.i2p"));
	assert (!IsValidHostName ("caf\303\251.i2p"));
	assert (!IsValidHostName ("x.i2p."));
	assert (!IsValidHostName ("x"));
}

void TestAddReplaceRemove ()
{
	auto keyA = i2p::data::PrivateKeys::CreateRandomKeys (
		i2p::data::SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519, i2p::data::CRYPTO_KEY_TYPE_ECIES_X25519_AEAD, true);
	auto keyB = i2p::data::PrivateKeys::CreateRandomKeys (
		i2p::data::SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519, i2p::data::CRYPTO_KEY_TYPE_ECIES_X25519_AEAD, true);
	const auto hashA = keyA.GetPublic ()->GetIdentHash ();
	const auto hashB = keyB.GetPublic ()->GetIdentHash ();

	auto storage = std::make_unique<FakeStorage> ();
	auto * fake = storage.get ();
	i2p::client::AddressBook book (std::move (storage));

	using Result = i2p::client::AddressBookMutationResult;
	assert (book.AddAddress ("site.i2p", Base64 (keyA), false) == Result::Ok);
	assert (fake->saveCount == 1);
	assert (book.FindAddress ("SITE.I2P")->identHash == hashA);
	assert (fake->identities.contains (hashA));
	auto entries = book.GetEntries (10);
	assert (entries.size () == 1 && entries[0].name == "site.i2p");
	assert (!entries[0].blinded);

	// Same destination is a no-op; a different destination requires replace.
	assert (book.AddAddress ("site.i2p", Base64 (keyA), false) == Result::Ok);
	assert (book.AddAddress ("site.i2p", Base64 (keyB), false) == Result::Exists);
	assert (fake->saveCount == 1);
	assert (book.AddAddress ("site.i2p", Base64 (keyB), true) == Result::Ok);
	assert (fake->saveCount == 2);
	assert (book.FindAddress ("site.i2p")->identHash == hashB);
	assert (!fake->identities.contains (hashA));
	assert (fake->identities.contains (hashB));

	// A shared identity remains available to the other name.
	assert (book.AddAddress ("other.i2p", Base64 (keyB), false) == Result::Ok);
	assert (fake->saveCount == 3);
	assert (book.RemoveAddress ("site.i2p") == Result::Ok);
	assert (fake->saveCount == 4);
	assert (book.FindAddress ("site.i2p") == nullptr);
	assert (book.FindAddress ("other.i2p")->identHash == hashB);
	assert (fake->identities.contains (hashB));

	assert (book.RemoveAddress ("other.i2p") == Result::Ok);
	assert (fake->saveCount == 5);
	assert (!fake->identities.contains (hashB));
	assert (book.RemoveAddress ("other.i2p") == Result::NotFound);

	// Invalid destinations and names never mutate the book.
	assert (book.AddAddress ("bad.i2p", "not-base64", false) == Result::InvalidDestination);
	assert (book.AddAddress ("proxy.i2p", Base64 (keyA), false) == Result::InvalidName);
	assert (book.GetEntries (10).empty ());
}

void TestDeletedHosts ()
{
	auto key = i2p::data::PrivateKeys::CreateRandomKeys (
		i2p::data::SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519, i2p::data::CRYPTO_KEY_TYPE_ECIES_X25519_AEAD, true);
	auto storage = std::make_unique<FakeStorage> ();
	auto book = std::make_unique<i2p::client::AddressBook> (std::move (storage));
	using Result = i2p::client::AddressBookMutationResult;
	assert (book->AddAddress ("site.i2p", Base64 (key), false) == Result::Ok);
	assert (book->RemoveAddress ("site.i2p") == Result::Ok);
	assert (book->GetDeletedHosts (10).size () == 1);
	assert (book->GetDeletedHosts (10)[0] == "site.i2p");

	std::istringstream update ("site.i2p=" + Base64 (key) + "\n");
	assert (book->LoadHostsFromStream (std::move (update), true));
	assert (book->FindAddress ("site.i2p") == nullptr);

	assert (book->UnblockAddress ("site.i2p") == Result::Ok);
	assert (book->GetDeletedHosts (10).empty ());
	std::istringstream updateAgain ("site.i2p=" + Base64 (key) + "\n");
	assert (book->LoadHostsFromStream (std::move (updateAgain), true));
	assert (book->FindAddress ("site.i2p") != nullptr);

	assert (book->BlockAddress ("*.i2p") == Result::Ok);
	assert (book->BlockAddress ("bad pattern") == Result::InvalidName);
	assert (book->GetDeletedHosts (10).size () == 1);
	assert (book->AddAddress ("second.i2p", Base64 (key), false) == Result::Ok);
	// An explicit add clears matching tombstone patterns.
	assert (book->GetDeletedHosts (10).empty ());
}
}

int main ()
{
	TestNameValidation ();
	TestAddReplaceRemove ();
	TestDeletedHosts ();
	return 0;
}

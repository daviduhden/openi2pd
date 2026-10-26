/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: test address-book administration.
 *
 * Offline checks for Java-compatible name validation, bounded add/replace/
 * remove semantics, shared-identity reference handling, subscription-URL
 * validation and the atomic subscriptions.txt round trip. No socket, resolver,
 * destination, timer or thread is started; the only filesystem use is an
 * isolated temporary data directory.
 */

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "AddressBook.h"
#include "FS.h"
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

void TestSubscriptionValidation ()
{
	using i2p::client::IsValidSubscriptionURL;
	auto keys = i2p::data::PrivateKeys::CreateRandomKeys (
		i2p::data::SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519, i2p::data::CRYPTO_KEY_TYPE_ECIES_X25519_AEAD, true);
	const auto b32 = keys.GetPublic ()->GetIdentHash ().ToBase32 ();

	std::string error;
	const char * accepted[] = {
		"http://stats.i2p/hosts",
		"https://sub.home.i2p:8443/hosts?format=hosts",
		"HTTP://STATS.I2P/HOSTS",
	};
	for (const char * url : accepted)
	{
		assert (IsValidSubscriptionURL (url, error));
		assert (error.empty ());
	}
	// A base32 destination host resolves without an address-book entry, so it
	// stays a valid I2P-only source.
	const std::string b32HostSource = "http://" + b32 + ".b32.i2p/hosts";
	assert (IsValidSubscriptionURL (b32HostSource, error));
	assert (error.empty ());

	const char * rejected[] = {
		"",
		"ftp://stats.i2p/hosts",
		"stats.i2p/hosts",
		"http://",
		"http:///hosts",
		"http://user:pass@stats.i2p/hosts",
		"http://stats.i2p:pass@stats.i2p/hosts",
		"http://stats.i2p/hosts#fragment",
		"http://stats.i2p/ho sts",
		"http://stats.i2p/hosts\t",
		"http://stats.i2p/hosts\n",
		// Cleartext hosts are refused: a subscription stays an I2P fetch.
		"http://example.com/hosts",
		"http://127.0.0.1/hosts",
		"http://[::1]/hosts",
		// Port and authority bounds.
		"http://stats.i2p:/hosts",
		"http://stats.i2p:0/hosts",
		"http://stats.i2p:70000/hosts",
		"http://stats.i2p:80x/hosts",
		"http://stats.i2p./hosts",
	};
	for (const char * url : rejected)
	{
		assert (!IsValidSubscriptionURL (url, error));
		assert (!error.empty ());
	}
	assert (!IsValidSubscriptionURL (std::string ("http://stats.i2p/") + std::string (600, 'a'), error));
	assert (!error.empty ());
}

void TestSubscriptionRoundTrip ()
{
	const auto root = std::filesystem::temp_directory_path ()
		/ ("openi2pd-addressbook-subscriptions-"
			+ std::to_string (std::chrono::steady_clock::now ().time_since_epoch ().count ()));
	std::filesystem::create_directories (root);
	// The subscription list lives in the data directory, not in storage.
	i2p::fs::DetectDataDir (root.string (), false);
	const auto file = i2p::fs::DataDirPath ("subscriptions.txt");

	auto storage = std::make_unique<FakeStorage> ();
	i2p::client::AddressBook book (std::move (storage));
	std::string error;

	assert (book.GetSubscriptions (16).empty ());
	assert (book.AddSubscription ("http://stats.i2p/hosts", error));
	assert (error.empty ());
	// Surrounding whitespace is trimmed; the stored value is the clean URL.
	assert (book.AddSubscription ("  https://other.i2p/hosts?format=hosts  ", error));
	assert (error.empty ());

	auto list = book.GetSubscriptions (16);
	assert (list.size () == 2);
	assert (list[0] == "http://stats.i2p/hosts");
	assert (list[1] == "https://other.i2p/hosts?format=hosts");
	assert (book.GetSubscriptions (1).size () == 1);
	assert (book.GetSubscriptions (0).size () == 2); // 0 selects the stored cap

	// Duplicates and invalid sources never reach the list.
	assert (!book.AddSubscription ("http://stats.i2p/hosts", error));
	assert (!error.empty ());
	assert (!book.AddSubscription ("ftp://stats.i2p/hosts", error));
	assert (!book.AddSubscription ("http://user:pass@stats.i2p/hosts", error));
	assert (!book.AddSubscription ("http://stats.i2p/hosts#frag", error));
	assert (book.GetSubscriptions (16).size () == 2);

	std::string contents;
	{
		std::ifstream saved (file);
		assert (saved.is_open ());
		contents.assign (std::istreambuf_iterator<char> (saved), std::istreambuf_iterator<char> ());
	}
	assert (contents.find ("http://stats.i2p/hosts\n") != std::string::npos);
	assert (contents.find ("https://other.i2p/hosts?format=hosts\n") != std::string::npos);
	assert (contents.find ("user:pass") == std::string::npos);
	// The atomic replacement leaves no staging file behind.
	assert (std::filesystem::exists (file));
	size_t staging = 0;
	for (const auto & entry : std::filesystem::directory_iterator (root))
		if (entry.is_regular_file ()
			&& entry.path ().filename ().string ().find (".tmp-") != std::string::npos)
			staging++;
	assert (staging == 0);

	// Remove: validation first, then an exact match.
	assert (!book.RemoveSubscription ("ftp://stats.i2p/hosts", error));
	assert (!error.empty ());
	assert (!book.RemoveSubscription ("http://absent.i2p/hosts", error));
	assert (!error.empty ());
	assert (book.RemoveSubscription ("http://stats.i2p/hosts", error));
	assert (error.empty ());
	list = book.GetSubscriptions (16);
	assert (list.size () == 1 && list[0] == "https://other.i2p/hosts?format=hosts");
	assert (book.RemoveSubscription ("https://other.i2p/hosts?format=hosts", error));
	assert (error.empty ());
	assert (book.GetSubscriptions (16).empty ());
	{
		std::ifstream saved (file);
		assert (saved.is_open ());
		std::string line;
		while (std::getline (saved, line))
			assert (line.empty () || line[0] == '#');
	}

	// The source list is capped; an entry beyond the cap is rejected instead of
	// silently dropped.
	for (size_t i = 0; i < i2p::client::MAX_SUBSCRIPTION_URLS; i++)
		assert (book.AddSubscription ("http://h" + std::to_string (i) + ".i2p/hosts", error));
	assert (book.GetSubscriptions (0).size () == i2p::client::MAX_SUBSCRIPTION_URLS);
	assert (!book.AddSubscription ("http://extra.i2p/hosts", error));
	assert (!error.empty ());
	assert (book.GetSubscriptions (0).size () == i2p::client::MAX_SUBSCRIPTION_URLS);

	std::error_code ignored;
	std::filesystem::remove_all (root, ignored);
}
}

int main ()
{
	TestNameValidation ();
	TestAddReplaceRemove ();
	TestDeletedHosts ();
	TestSubscriptionValidation ();
	TestSubscriptionRoundTrip ();
	return 0;
}

/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: test addressbook case-insensitivity.
 *
 * Offline checks that .i2p and .b32.i2p host names resolve regardless of
 * ASCII case, that mixed-case hosts.txt entries share one lookup key, and
 * that normalisation does not touch base64 destinations. No storage,
 * resolver, destination or network is started.
 *
 */

#include <cassert>
#include <sstream>
#include <string>

#include "AddressBook.h"
#include "Identity.h"

namespace
{
i2p::data::PrivateKeys MakeDestinationKeys ()
{
	return i2p::data::PrivateKeys::CreateRandomKeys (
		i2p::data::SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519, i2p::data::CRYPTO_KEY_TYPE_ECIES_X25519_AEAD, true);
}

std::string Base64 (const i2p::data::PrivateKeys & keys)
{
	return keys.GetPublic ()->ToBase64 ();
}

std::string Upper (std::string value)
{
	for (auto & ch : value)
		if (ch >= 'a' && ch <= 'z') ch = static_cast<char> (ch - 'a' + 'A');
	return value;
}

void TestNormalizeHostName ()
{
	using i2p::client::NormalizeHostName;
	assert (NormalizeHostName ("WWW.Example.I2P") == "www.example.i2p");
	assert (NormalizeHostName ("AbC.b32.i2P") == "abc.b32.i2p");
	assert (NormalizeHostName ("site.i2p") == "site.i2p");
	assert (NormalizeHostName ("") == "");
	// Only ASCII letters are folded, so non-ASCII bytes and digits are kept
	// byte-for-byte.
	assert (NormalizeHostName ("caf\303\251.i2p") == "caf\303\251.i2p");
	assert (NormalizeHostName ("host-01.I2P") == "host-01.i2p");

	// A b33 label is URL-safe base64 and case-sensitive: it must survive
	// verbatim while the suffix and any subdomain prefix are folded.
	const std::string b33 = "AbCdEfGhIjKlMnOpQrStUvWxYz0123456789AbCdEfGhIjKlMnOpQrSt";
	assert (b33.size () > i2p::client::B33_ADDRESS_THRESHOLD);
	assert (NormalizeHostName (b33 + ".I2P") == b33 + ".i2p");
	assert (NormalizeHostName ("SUB." + b33 + ".I2P") == "sub." + b33 + ".i2p");
}

void TestMixedCaseHostsFile ()
{
	auto keys = MakeDestinationKeys ();
	const auto & hash = keys.GetPublic ()->GetIdentHash ();

	i2p::client::AddressBook book;
	std::stringstream hosts;
	hosts << "MiXeD.Example.I2P=" << Base64 (keys) << "\n";
	assert (book.LoadHostsFromStream (std::move (hosts), true));

	// Every spelling resolves to the same entry.
	for (const char * name : { "mixed.example.i2p", "MIXED.EXAMPLE.I2P", "MiXeD.ExAmPlE.i2p" })
	{
		auto addr = book.FindAddress (name);
		assert (addr);
		assert (addr->IsIdentHash ());
		assert (addr->identHash == hash);
	}

	auto resolved = book.GetAddress ("mIxEd.ExAmPlE.i2P");
	assert (resolved);
	assert (resolved->identHash == hash);
	assert (book.RecordExists ("MIXED.EXAMPLE.I2P", Base64 (keys)));
	assert (book.RecordExists ("mixed.example.i2p", hash.ToBase32 () + ".b32.i2p"));

	// Unknown names are still unknown in any spelling.
	assert (!book.FindAddress ("absent.example.i2p"));
	assert (!book.FindAddress ("ABSENT.EXAMPLE.I2P"));
}

void TestMixedCaseReplacement ()
{
	auto keyA = MakeDestinationKeys ();
	auto keyB = MakeDestinationKeys ();
	const auto & hashB = keyB.GetPublic ()->GetIdentHash ();

	i2p::client::AddressBook book;
	std::stringstream hosts;
	hosts << "Site.I2P=" << Base64 (keyA) << "\n"
		  << "site.i2p=" << Base64 (keyB) << "\n";
	assert (book.LoadHostsFromStream (std::move (hosts), true));

	// The second line has to replace the first one instead of creating a
	// second, case-distinct entry.
	auto site = book.FindAddress ("SITE.I2P");
	assert (site);
	assert (site->identHash == hashB);
}

void TestB32Case ()
{
	auto keys = MakeDestinationKeys ();
	const auto & hash = keys.GetPublic ()->GetIdentHash ();
	const std::string b32 = hash.ToBase32 ();

	i2p::client::AddressBook book;

	// .b32.i2p is decoded from the normalised name.
	const std::string lower = b32 + ".b32.i2p";
	const std::string upper = Upper (lower);
	const std::string mixed = b32 + ".B32.I2P";
	for (const auto & name : { lower, upper, mixed })
	{
		auto addr = book.GetAddress (name);
		assert (addr);
		assert (addr->IsIdentHash ());
		assert (addr->identHash == hash);
	}

	// A mixed-case b32 name must not be stored from hosts.txt.
	std::stringstream hosts;
	hosts << b32 << ".B32.I2P=" << Base64 (keys) << "\n";
	assert (book.LoadHostsFromStream (std::move (hosts), true));
	assert (!book.FindAddress (lower));
	assert (!book.FindAddress (upper));
}

void TestInsertAddressCase ()
{
	auto keys = MakeDestinationKeys ();
	const auto & hash = keys.GetPublic ()->GetIdentHash ();
	const std::string b32 = hash.ToBase32 ();

	i2p::client::AddressBook book;

	// insertAddress() normalises the key but keeps accepting both jumps.
	book.InsertAddress ("Jump.Example.I2P", Base64 (keys));
	auto jump = book.FindAddress ("jump.example.i2p");
	assert (jump);
	assert (jump->identHash == hash);

	book.InsertAddress ("B32.Jump.Example.I2P", b32 + ".b32.i2p");
	auto b32jump = book.FindAddress ("b32.jump.example.i2p");
	assert (b32jump);
	assert (b32jump->identHash == hash);
	auto jumped = book.GetAddress ("B32.JUMP.EXAMPLE.I2P");
	assert (jumped);
	assert (jumped->identHash == hash);
}

void TestBase64StaysCaseSensitive ()
{
	auto keys = MakeDestinationKeys ();
	const auto & hash = keys.GetPublic ()->GetIdentHash ();

	i2p::client::AddressBook book;

	// A base64 destination is parsed from its exact spelling and is not
	// lowercased as if it were a host name.
	const std::string base64 = Base64 (keys);
	assert (base64 != Upper (base64)); // the test vector really contains lower-case letters
	auto addr = book.GetAddress (base64);
	assert (addr);
	assert (addr->identHash == hash);
}

void TestLookupReplyAuthority ()
{
	using Requests = i2p::client::detail::AddressLookupRequests;
	Requests requests;
	const auto resolver = MakeDestinationKeys ().GetPublic ()->GetIdentHash ();
	const auto stranger = MakeDestinationKeys ().GetPublic ()->GetIdentHash ();
	assert (requests.Insert (42, "Host.Domain.I2P", resolver, 100));
	// A valid signature from a different destination does not authorize this
	// nonce and must leave the real request available to its resolver.
	assert (!requests.Take (42, stranger, 100));
	assert (!requests.Take (43, resolver, 100));
	assert (!requests.Insert (42, "other.domain.i2p", resolver, 100));
	assert (!requests.Insert (43, "HOST.DOMAIN.I2P", resolver, 100));
	auto name = requests.Take (42, resolver, 101);
	assert (name && *name == "host.domain.i2p");
	assert (!requests.Take (42, resolver, 101)); // replay
	assert (requests.Insert (42, "host.domain.i2p", resolver, 101));
	requests.Clear ();
	assert (!requests.Take (42, resolver, 101)); // stopped lookup service
}

void TestLookupBoundsAndExpiry ()
{
	using Requests = i2p::client::detail::AddressLookupRequests;
	Requests requests;
	const auto resolver = MakeDestinationKeys ().GetPublic ()->GetIdentHash ();
	assert (!requests.Insert (1, "", resolver, 100));
	assert (!requests.Insert (1, std::string (256, 'a'), resolver, 100));
	for (size_t i = 0; i < Requests::MAX_PENDING; ++i)
		assert (requests.Insert (static_cast<uint32_t> (i), std::to_string (i) + ".domain.i2p", resolver, 100));
	assert (!requests.Insert (1000, "overflow.domain.i2p", resolver, 100));
	// Just before the deadline, the original slot still belongs to its name.
	auto name = requests.Take (0, resolver, 100 + Requests::TIMEOUT_SECONDS - 1);
	assert (name && *name == "0.domain.i2p");
	// At the deadline, neither an authenticated late reply nor stale requests
	// may retain authority or prevent a new lookup from acquiring a slot.
	assert (!requests.Take (1, resolver, 100 + Requests::TIMEOUT_SECONDS));
	assert (requests.Insert (1000, "new.domain.i2p", resolver, 100 + Requests::TIMEOUT_SECONDS));
	assert (requests.Take (1000, resolver, 100 + Requests::TIMEOUT_SECONDS));
	assert (requests.Insert (1001, "reset.domain.i2p", resolver, 500));
	assert (!requests.Take (1001, resolver, 499)); // clock reset fails closed
}
} // namespace

int main ()
{
	TestNormalizeHostName ();
	TestMixedCaseHostsFile ();
	TestMixedCaseReplacement ();
	TestB32Case ();
	TestInsertAddressCase ();
	TestBase64StaysCaseSensitive ();
	TestLookupReplyAuthority ();
	TestLookupBoundsAndExpiry ();
	return 0;
}

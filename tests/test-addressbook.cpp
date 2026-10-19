/*
 * Copyright (c) 2026, David Uhden Collado
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * This file is part of openi2pd and licensed under BSD-3-Clause
 *
 * See full license text in LICENSE file at top of project tree
 */

#include <cassert>
#include <sstream>
#include <string>

#include "AddressBook.h"
#include "Blinding.h"
#include "Identity.h"

// Offline regression checks for Address parsing and hosts.txt parsing. No
// router, destination, resolver, network or persisted storage is started: the
// AddressBook instances below are created without Start(), so m_Storage stays
// null and LoadHostsFromStream only updates the in-memory map.

namespace
{
	constexpr std::size_t IDENT_HASH_BASE32_LEN = 52;

	i2p::data::PrivateKeys MakeDestinationKeys ()
	{
		return i2p::data::PrivateKeys::CreateRandomKeys (i2p::data::SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519,
			i2p::data::CRYPTO_KEY_TYPE_ECIES_X25519_AEAD, true);
	}

	std::string Base64 (const i2p::data::PrivateKeys & keys)
	{
		return keys.GetPublic ()->ToBase64 ();
	}

	void TestIdentHashAddress ()
	{
		const std::string b32 (IDENT_HASH_BASE32_LEN, 'a');

		i2p::client::Address fromString (b32);
		assert (fromString.IsValid ());
		assert (fromString.IsIdentHash ());
		assert (!fromString.blindedPublicKey);
		assert (fromString.ToBase32 () == b32);

		i2p::data::IdentHash hash;
		assert (hash.FromBase32 (b32) == i2p::data::IdentHash::len);
		const i2p::client::Address fromHash (hash);
		assert (fromHash.IsValid ());
		assert (fromHash.IsIdentHash ());
		assert (fromHash == fromString);
		assert (fromHash.ToBase32 () == b32);
		assert (i2p::client::GetB32Address (hash) == b32 + ".b32.i2p");

		// Non-base32 characters are rejected and invalid addresses serialise
		// to nothing.
		const i2p::client::Address invalid ("not-a-base32-address");
		assert (!invalid.IsValid ());
		assert (invalid.addressType == i2p::client::Address::eAddressInvalid);
		assert (invalid.ToBase32 ().empty ());
		assert (!(invalid == fromString));
	}

	void TestBlindedAddress ()
	{
		auto keys = MakeDestinationKeys ();
		i2p::data::BlindedPublicKey blinded (keys.GetPublic ());
		assert (blinded.IsValid ());

		const std::string b33 = blinded.ToB33 ();
		assert (!b33.empty ());
		assert (b33.size () > i2p::client::B33_ADDRESS_THRESHOLD);

		i2p::client::Address address (b33);
		assert (address.IsValid ());
		assert (!address.IsIdentHash ());
		assert (address.addressType == i2p::client::Address::eAddressBlindedPublicKey);
		assert (address.blindedPublicKey);
		assert (address.ToBase32 () == b33);

		const i2p::client::Address same (b33);
		assert (address == same);

		// A truncated b33 must not be accepted as a blinded address.
		const i2p::client::Address truncated (b33.substr (0, b33.size () - 1));
		assert (!truncated.IsValid ());
		assert (truncated.ToBase32 ().empty ());

		// Bytes beyond the exact b33 layout are rejected, not silently ignored.
		const i2p::client::Address overlong (b33 + "aa");
		assert (!overlong.IsValid ());
	}

	void TestGetAddress ()
	{
		auto keys = MakeDestinationKeys ();
		const auto & identHash = keys.GetPublic ()->GetIdentHash ();
		const std::string b32 = identHash.ToBase32 ();

		i2p::client::AddressBook book;

		auto b32Address = book.GetAddress (b32 + ".b32.i2p");
		assert (b32Address);
		assert (b32Address->IsIdentHash ());
		assert (b32Address->identHash == identHash);

		auto fullAddress = book.GetAddress (Base64 (keys));
		assert (fullAddress);
		assert (fullAddress->IsIdentHash ());
		assert (fullAddress->identHash == identHash);

		assert (book.GetAddress ("") == nullptr);
	}

	void TestLoadHostsFromStream ()
	{
		auto keyA = MakeDestinationKeys ();
		auto keyB = MakeDestinationKeys ();
		const auto & hashA = keyA.GetPublic ()->GetIdentHash ();
		const auto & hashB = keyB.GetPublic ()->GetIdentHash ();
		assert (!(hashA == hashB));

		i2p::client::AddressBook book;

		std::stringstream hosts;
		hosts << "# comment line\n"
			  << "\n"
			  << "site.i2p=" << Base64 (keyA) << "\n"
			  << "site.i2p=" << Base64 (keyB) << "# replacement\n"
			  << "malformed.i2p=%%%not-base64%%%\n"
			  << "not-an-i2p-name=" << Base64 (keyA) << "\n"
			  << hashA.ToBase32 () << ".b32.i2p=" << Base64 (keyA) << "\n"
			  << "other.i2p=" << Base64 (keyB) << "\n";

		assert (book.LoadHostsFromStream (std::move (hosts), true));

		// The second entry replaces the first one: a newer non-DSA identity
		// wins, and the obsolete hash is no longer reachable.
		auto site = book.FindAddress ("site.i2p");
		assert (site);
		assert (site->IsIdentHash ());
		assert (site->identHash == hashB);
		assert (!book.RecordExists ("site.i2p", hashA.ToBase32 () + ".b32.i2p"));
		assert (book.RecordExists ("site.i2p", Base64 (keyB)));

		auto other = book.FindAddress ("other.i2p");
		assert (other);
		assert (other->identHash == hashB);

		// Rejected lines: malformed base64, a domain that is not .i2p and a
		// b32 name (b32 names must not be stored in the address book).
		assert (!book.FindAddress ("malformed.i2p"));
		assert (!book.FindAddress ("not-an-i2p-name"));
		assert (!book.FindAddress (hashA.ToBase32 () + ".b32.i2p"));

		// Unknown names are not invented by the parser.
		assert (!book.FindAddress ("absent.i2p"));
	}

	void TestLoadHostsFromStreamIncomplete ()
	{
		i2p::client::AddressBook book;

		// A final line without '=' means the stream may have been truncated.
		std::stringstream noEquals ("looks-like-a-host.i2p");
		assert (!book.LoadHostsFromStream (std::move (noEquals), true));

		// A malformed address on the unterminated final line is reported as an
		// incomplete read instead of a silent success.
		std::stringstream badTail ("last.i2p=%%%not-base64%%%");
		assert (!book.LoadHostsFromStream (std::move (badTail), true));
		assert (!book.FindAddress ("last.i2p"));
	}
}

int main ()
{
	TestIdentHashAddress ();
	TestBlindedAddress ();
	TestGetAddress ();
	TestLoadHostsFromStream ();
	TestLoadHostsFromStreamIncomplete ();
	return 0;
}

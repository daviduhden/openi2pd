/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: test eciesx25519 ratchet tagsets.
 *
 * Validates ratchet tag-set KDFs against independent HMAC vectors and checks tag indices, 16-bit tagset IDs, symmetric-key gaps and fail-closed exhaustion of the Java-compatible tag space.
 *
 */

#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <string_view>
#include <vector>

#include <openssl/evp.h>
#include <openssl/hmac.h>

#include "Crypto.h"
#include "ECIESX25519AEADRatchetSession.h"

namespace
{
using Bytes32 = std::array<uint8_t, 32>;

std::array<uint8_t, 32> HmacSha256 (const uint8_t * key, size_t keyLen, const uint8_t * data, size_t len)
{
	std::array<uint8_t, 32> out { };
	unsigned int outLen = out.size ();
	assert (HMAC (EVP_sha256 (), key, static_cast<int> (keyLen), data, len, out.data (), &outLen));
	assert (outLen == out.size ());
	return out;
}

// RFC 5869 Extract-then-Expand with SHA-256 and a 32-byte salt.
std::vector<uint8_t> RefHKDF (
	const uint8_t * salt32, const uint8_t * ikm, size_t ikmLen, std::string_view info, size_t outLen)
{
	const std::array<uint8_t, 32> prk = HmacSha256 (salt32, 32, ikm, ikmLen);
	std::vector<uint8_t> okm;
	std::vector<uint8_t> previous; // T(0) is empty
	uint8_t counter = 1;
	while (okm.size () < outLen)
	{
		std::vector<uint8_t> block = previous;
		block.insert (block.end (), info.begin (), info.end ());
		block.push_back (counter++);
		const auto mac = HmacSha256 (prk.data (), prk.size (), block.data (), block.size ());
		previous.assign (mac.begin (), mac.end ());
		okm.insert (okm.end (), previous.begin (), previous.end ());
	}
	okm.resize (outLen);
	return okm;
}

Bytes32 Fill (uint8_t seed)
{
	Bytes32 out { };
	for (size_t i = 0; i < out.size (); i++) out[i] = static_cast<uint8_t> (seed + 17 * i);
	return out;
}

struct ReferenceTagSet
{
	Bytes32 nextRootKey { };
	Bytes32 sessionTagCK { };
	Bytes32 sessionTagConstant { };
	Bytes32 symmKeyCK { };

	explicit ReferenceTagSet (const Bytes32 & rootKey, const Bytes32 & k)
	{
		const auto step = RefHKDF (rootKey.data (), k.data (), k.size (), "KDFDHRatchetStep", 64);
		std::memcpy (nextRootKey.data (), step.data (), 32);
		const auto tagAndKey = RefHKDF (step.data () + 32, nullptr, 0, "TagAndKeyGenKeys", 64);
		std::memcpy (sessionTagCK.data (), tagAndKey.data (), 32);
		std::memcpy (symmKeyCK.data (), tagAndKey.data () + 32, 32);
		const auto initialized = RefHKDF (sessionTagCK.data (), nullptr, 0, "STInitialization", 64);
		std::memcpy (sessionTagCK.data (), initialized.data (), 32);
		std::memcpy (sessionTagConstant.data (), initialized.data () + 32, 32);
	}

	uint64_t NextTag ()
	{
		const auto data = RefHKDF (
			sessionTagCK.data (), sessionTagConstant.data (), sessionTagConstant.size (), "SessionTagKeyGen", 64);
		std::memcpy (sessionTagCK.data (), data.data (), 32);
		uint64_t tag = 0;
		std::memcpy (&tag, data.data () + 32, sizeof (tag));
		return tag;
	}

	Bytes32 NextKey ()
	{
		const auto data = RefHKDF (symmKeyCK.data (), nullptr, 0, "SymmetricRatchet", 64);
		std::memcpy (symmKeyCK.data (), data.data (), 32);
		Bytes32 key { };
		std::memcpy (key.data (), data.data () + 32, key.size ());
		return key;
	}
};
}

int main ()
{
	const Bytes32 rootKey = Fill (0x11);
	const Bytes32 tagsetKey = Fill (0x5a);

	i2p::garlic::RatchetTagSet tagset;
	tagset.DHInitialize (rootKey.data (), tagsetKey.data ());
	tagset.NextSessionTagRatchet ();
	tagset.SetTagSetID (3);

	ReferenceTagSet reference (rootKey, tagsetKey);
	assert (!std::memcmp (tagset.GetNextRootKey (), reference.nextRootKey.data (), 32));

	// Index 0 is the tag whose N (nonce) is 0: callers read GetNextIndex()
	// before generating the tag. CreatePayload and the tag map rely on this.
	i2p::crypto::HKDFContext hkdf;
	for (int i = 0; i < 8; i++)
	{
		assert (tagset.GetNextIndex () == i);
		const uint64_t expected = reference.NextTag ();
		const uint64_t tag = tagset.GetNextSessionTag (hkdf);
		assert (tag == expected);
		assert (tag != 0);
		assert (tagset.GetNextIndex () == i + 1);
	}
	assert (tagset.GetMsgID () == static_cast<uint32_t> ((3 << 16) + 8));

	// ACKs carry a 16-bit tagset ID. Values outside that range must not be
	// shifted into a wrapped, unconfirmable 32-bit message ID.
	tagset.SetTagSetID (0x10000);
	assert (tagset.GetMsgID () == 0);
	tagset.SetTagSetID (-1);
	assert (tagset.GetMsgID () == 0);
	tagset.SetTagSetID (0xFFFF);
	assert (tagset.GetMsgID () == 0xFFFF0008u);
	tagset.SetTagSetID (3);

	// Symmetric key ratchet: in-order, gap, and a late older key served from
	// the intermediate cache. Expected values come from the independent KDF.
	ReferenceTagSet referenceKeys (rootKey, tagsetKey);
	std::array<Bytes32, 4> expectedKeys { };
	for (auto & key : expectedKeys) key = referenceKeys.NextKey ();

	uint8_t actual[32] = { };
	assert (tagset.GetSymmKey (0, actual));
	assert (!std::memcmp (actual, expectedKeys[0].data (), 32));

	// Walking to index 3 must leave 1 and 2 available.
	assert (tagset.GetSymmKey (3, actual));
	assert (!std::memcmp (actual, expectedKeys[3].data (), 32));
	assert (tagset.GetSymmKey (1, actual));
	assert (!std::memcmp (actual, expectedKeys[1].data (), 32));
	assert (tagset.GetSymmKey (2, actual));
	assert (!std::memcmp (actual, expectedKeys[2].data (), 32));
	// Each intermediate is consumed exactly once.
	assert (!tagset.GetSymmKey (1, actual));

	// Out-of-range and already-consumed indexes fail closed: the caller must
	// not encrypt or decrypt with an uninitialised stack key.
	std::memset (actual, 0xAA, sizeof (actual));
	assert (!tagset.GetSymmKey (-1, actual));
	assert (!tagset.GetSymmKey (65535, actual));
	assert (actual[0] == 0xAA);

	// A fresh tagset starts at index 0.
	{
		i2p::garlic::RatchetTagSet fresh;
		fresh.DHInitialize (rootKey.data (), tagsetKey.data ());
		fresh.NextSessionTagRatchet ();
		ReferenceTagSet freshReference (rootKey, tagsetKey);
		assert (fresh.GetNextIndex () == 0);
		assert (fresh.GetNextSessionTag (hkdf) == freshReference.NextTag ());
		const Bytes32 expected0 = freshReference.NextKey ();
		uint8_t got[32] = { };
		assert (fresh.GetSymmKey (0, got));
		assert (!std::memcmp (got, expected0.data (), 32));
	}

	// Java's 16-bit tag space accepts indices 0..65534 and then fails closed.
	// The generator must not be one tag short, and the failure must not advance
	// the index (which would silently skip a peer tag).
	{
		i2p::garlic::RatchetTagSet boundary;
		boundary.DHInitialize (rootKey.data (), tagsetKey.data ());
		boundary.NextSessionTagRatchet ();
		for (int i = 0; i <= 65534; ++i) assert (boundary.GetNextSessionTag (hkdf) != 0);
		assert (boundary.GetNextIndex () == 65535);
		assert (boundary.GetNextSessionTag (hkdf) == 0);
		assert (boundary.GetNextIndex () == 65535);
	}

	return 0;
}

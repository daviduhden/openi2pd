/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: test leaseset encrypted freshness.
 *
 * Static checks for the encrypted LeaseSet2 client replay guard: the signed
 * outer published timestamp survives the outer-only and the decrypted parse,
 * and only a strictly newer reply may replace the cached entry.
 *
 */

#include <cassert>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

#include "Blinding.h"
#include "Crypto.h"
#include "I2PEndian.h"
#include "Identity.h"
#include "LeaseSet.h"
#include "Timestamp.h"

// No destination, socket or thread is created here. The type 5 branch of
// LeaseSetDestination::HandleDatabaseStoreMessage is private, so the test
// pins the two contracts that branch depends on: the outer timestamp is not
// replaced by the inner header, and the cache accepts only a strictly newer
// candidate.

namespace
{
using namespace i2p::data;

void AppendBe16 (std::vector<uint8_t> & buffer, uint16_t value)
{
	uint8_t encoded[2];
	htobe16buf (encoded, value);
	buffer.insert (buffer.end (), encoded, encoded + sizeof (encoded));
}

void AppendBe32 (std::vector<uint8_t> & buffer, uint32_t value)
{
	uint8_t encoded[4];
	htobe32buf (encoded, value);
	buffer.insert (buffer.end (), encoded, encoded + sizeof (encoded));
}

void AppendIdentity (std::vector<uint8_t> & buffer, const PrivateKeys & keys)
{
	auto identity = keys.GetPublic ();
	assert (identity);
	auto len = identity->GetFullLen ();
	auto offset = buffer.size ();
	buffer.resize (offset + len);
	assert (identity->ToBuffer (buffer.data () + offset, len) == len);
}

void AppendSignature (std::vector<uint8_t> & buffer, const PrivateKeys & keys)
{
	std::vector<uint8_t> signature (keys.GetSignatureLen ());
	assert (!signature.empty ());
	keys.Sign (buffer.data (), static_cast<int> (buffer.size ()), signature.data ());
	buffer.insert (buffer.end (), signature.begin (), signature.end ());
}

// The returned vector starts with the NetDb store type, which is covered by
// the inner LeaseSet2 signature but is not part of LeaseSet2::GetBuffer().
std::vector<uint8_t> BuildStandardLeaseSet2 (
	const PrivateKeys & keys, uint32_t published, uint16_t expires, uint32_t leaseEnd)
{
	std::vector<uint8_t> buffer;
	buffer.push_back (NETDB_STORE_TYPE_STANDARD_LEASESET2);
	AppendIdentity (buffer, keys);
	AppendBe32 (buffer, published);
	AppendBe16 (buffer, expires);
	AppendBe16 (buffer, 0); // flags
	AppendBe16 (buffer, 0); // empty properties
	buffer.push_back (1); // one encryption key
	AppendBe16 (buffer, CRYPTO_KEY_TYPE_ECIES_X25519_AEAD);
	AppendBe16 (buffer, 32);
	buffer.resize (buffer.size () + 32, 0x5A); // X25519 public key
	buffer.push_back (1); // one lease
	buffer.resize (buffer.size () + 32, 0x42); // gateway
	AppendBe32 (buffer, 0x01020304); // tunnel ID
	AppendBe32 (buffer, leaseEnd);
	AppendSignature (buffer, keys);
	return buffer;
}

// Seals an already signed inner standard LS2 exactly like
// LocalEncryptedLeaseSet2 with ENCRYPTED_LEASESET_AUTH_TYPE_NONE, but with an
// explicit published timestamp so the replay order is deterministic instead
// of depending on the wall clock.
std::vector<uint8_t> SealEncryptedLeaseSet2 (
	const PrivateKeys & keys, const std::vector<uint8_t> & inner, uint32_t published, uint16_t expires)
{
	assert (inner.size () > 1);
	auto identity = keys.GetPublic ();
	assert (identity);
	BlindedPublicKey blindedKey (identity);
	char date[9];
	i2p::util::GetDateString (published, date);
	uint8_t blindedPriv[i2p::crypto::EDDSA25519_PRIVATE_KEY_LENGTH],
		blindedPub[i2p::crypto::EDDSA25519_PUBLIC_KEY_LENGTH];
	auto publicKeyLen = blindedKey.BlindPrivateKey (keys.GetSigningPrivateKey (), date, blindedPriv, blindedPub);
	assert (publicKeyLen == i2p::crypto::EDDSA25519_PUBLIC_KEY_LENGTH);
	std::unique_ptr<i2p::crypto::Signer> signer (
		PrivateKeys::CreateSigner (blindedKey.GetBlindedSigType (), blindedPriv));
	memset (blindedPriv, 0, sizeof (blindedPriv)); // never keep the blinded secret around
	assert (signer);

	std::vector<uint8_t> buffer;
	buffer.push_back (NETDB_STORE_TYPE_ENCRYPTED_LEASESET2);
	AppendBe16 (buffer, blindedKey.GetBlindedSigType ());
	buffer.insert (buffer.end (), blindedPub, blindedPub + publicKeyLen);
	AppendBe32 (buffer, published);
	AppendBe16 (buffer, expires);
	AppendBe16 (buffer, 0); // flags
	size_t lenInnerPlaintext = inner.size () + 1;
	size_t lenOuterPlaintext = lenInnerPlaintext + 32 + 1; // inner salt + layer 1 flags
	size_t lenOuterCiphertext = lenOuterPlaintext + 32; // outer salt
	assert (lenOuterCiphertext <= 0xFFFF);
	AppendBe16 (buffer, static_cast<uint16_t> (lenOuterCiphertext));
	size_t outerSaltPos = buffer.size ();
	buffer.resize (buffer.size () + 32);
	assert (RAND_bytes (buffer.data () + outerSaltPos, 32) == 1);
	uint8_t subcredential[36];
	blindedKey.GetSubcredential (blindedPub, publicKeyLen, subcredential);
	htobe32buf (subcredential + 32, published);
	uint8_t outerKeys[64], innerKeys[64];
	i2p::crypto::HKDF (buffer.data () + outerSaltPos, subcredential, 36, "ELS2_L1K", outerKeys);
	size_t outerPlaintextPos = buffer.size ();
	buffer.resize (buffer.size () + lenOuterPlaintext);
	buffer[outerPlaintextPos] = 0; // layer 1 flags: no per-client auth
	size_t innerSaltPos = buffer.size ();
	buffer.resize (buffer.size () + 32);
	assert (RAND_bytes (buffer.data () + innerSaltPos, 32) == 1);
	i2p::crypto::HKDF (buffer.data () + innerSaltPos, subcredential, 36, "ELS2_L2K", innerKeys);
	size_t innerPlaintextPos = buffer.size ();
	buffer.resize (buffer.size () + lenInnerPlaintext);
	buffer[innerPlaintextPos] = inner.front (); // inner store type
	memcpy (buffer.data () + innerPlaintextPos + 1, inner.data () + 1, inner.size () - 1);
	i2p::crypto::ChaCha20 (buffer.data () + innerPlaintextPos, lenInnerPlaintext, innerKeys, innerKeys + 32,
		buffer.data () + innerPlaintextPos);
	i2p::crypto::ChaCha20 (buffer.data () + outerPlaintextPos, lenOuterPlaintext, outerKeys, outerKeys + 32,
		buffer.data () + outerPlaintextPos);
	// The signature covers the store type and everything before it.
	std::vector<uint8_t> signature (i2p::crypto::EDDSA25519_SIGNATURE_LENGTH);
	signer->Sign (buffer.data (), static_cast<int> (buffer.size ()), signature.data ());
	buffer.insert (buffer.end (), signature.begin (), signature.end ());
	return buffer;
}

std::shared_ptr<LeaseSet2> ParseOuterEncrypted (const std::vector<uint8_t> & buffer)
{
	assert (!buffer.empty ());
	return std::make_shared<LeaseSet2> (
		NETDB_STORE_TYPE_ENCRYPTED_LEASESET2, buffer.data () + 1, buffer.size () - 1, false);
}

void TestOuterTimestampSurvivesBothParses ()
{
	auto keys = PrivateKeys::CreateRandomKeys (
		SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519, CRYPTO_KEY_TYPE_ECIES_X25519_AEAD, true);
	auto now = static_cast<uint32_t> (i2p::util::GetMillisecondsSinceEpoch () / 1000);
	// The inner layer is valid on its own: published now - 300 and a
	// 20 minute expires, so its own expiration is now + 900.
	auto inner = BuildStandardLeaseSet2 (keys, now - 300, 1200, now + 600);
	auto olderBuffer = SealEncryptedLeaseSet2 (keys, inner, now - 120, 900);
	auto newerBuffer = SealEncryptedLeaseSet2 (keys, inner, now - 60, 900);

	// A floodfill stores and serves the outer layer only, but it must still
	// record the signed published timestamp that orders the cache.
	auto olderOuter = ParseOuterEncrypted (olderBuffer);
	auto newerOuter = ParseOuterEncrypted (newerBuffer);
	assert (olderOuter->IsValid ());
	assert (newerOuter->IsValid ());
	assert (olderOuter->GetStoreType () == NETDB_STORE_TYPE_ENCRYPTED_LEASESET2);
	assert (olderOuter->GetPublishedTimestamp () == now - 120);
	assert (newerOuter->GetPublishedTimestamp () == now - 60);

	// A client decrypts the inner layer. The inner header must not replace
	// the outer timestamp, and the signed outer expiry stays authoritative.
	auto blindedKey = std::make_shared<BlindedPublicKey> (keys.GetPublic ());
	LeaseSet2 olderDecrypted (olderBuffer.data () + 1, olderBuffer.size () - 1, blindedKey, nullptr, nullptr);
	LeaseSet2 newerDecrypted (newerBuffer.data () + 1, newerBuffer.size () - 1, blindedKey, nullptr, nullptr);
	assert (olderDecrypted.IsValid ());
	assert (newerDecrypted.IsValid ());
	assert (olderDecrypted.GetStoreType () == NETDB_STORE_TYPE_STANDARD_LEASESET2);
	assert (olderDecrypted.GetPublishedTimestamp () == now - 120);
	assert (newerDecrypted.GetPublishedTimestamp () == now - 60);
	// The inner expiration (now + 900) is clamped to the outer one.
	assert (olderDecrypted.GetExpirationTime () == static_cast<uint64_t> (now + 780) * 1000);
	assert (newerDecrypted.GetExpirationTime () == static_cast<uint64_t> (now + 840) * 1000);
	assert (olderDecrypted.GetNonExpiredLeases ().size () == 1);
}

void TestOlderReplyDoesNotReplaceNewer ()
{
	auto keys = PrivateKeys::CreateRandomKeys (
		SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519, CRYPTO_KEY_TYPE_ECIES_X25519_AEAD, true);
	auto now = static_cast<uint32_t> (i2p::util::GetMillisecondsSinceEpoch () / 1000);
	auto inner = BuildStandardLeaseSet2 (keys, now - 300, 1200, now + 600);
	auto olderBuffer = SealEncryptedLeaseSet2 (keys, inner, now - 120, 900);
	auto newerBuffer = SealEncryptedLeaseSet2 (keys, inner, now - 60, 900);

	auto newer = ParseOuterEncrypted (newerBuffer);
	auto older = ParseOuterEncrypted (olderBuffer);
	auto duplicate = ParseOuterEncrypted (newerBuffer);
	// Both replies are authentic, well formed and unexpired, so the published
	// order is the only thing that may decide the replacement.
	assert (older->IsValid () && !older->IsExpired ());
	assert (newer->IsValid () && !newer->IsExpired ());
	assert (duplicate->IsValid () && !duplicate->IsExpired ());
	assert (older->GetPublishedTimestamp () < newer->GetPublishedTimestamp ());

	// Mirror of the type 5 guard in
	// LeaseSetDestination::HandleDatabaseStoreMessage: the cached entry is
	// replaced only by a strictly newer outer published timestamp.
	auto cached = newer;
	auto store = [&cached] (const std::shared_ptr<LeaseSet2> & candidate)
	{
		if (cached->GetPublishedTimestamp () >= candidate->GetPublishedTimestamp ()) return;
		cached = candidate;
	};

	store (older); // a replayed but authentic older LeaseSet2
	assert (cached == newer);
	assert (cached->GetPublishedTimestamp () == now - 60);
	store (duplicate); // an equal timestamp is a duplicate, not a refresh
	assert (cached == newer);

	// The same strict-newer rule through the public comparison also used by
	// the type 1/3 branch.
	assert (older->IsNewer (newerBuffer.data () + 1, newerBuffer.size () - 1));
	assert (!newer->IsNewer (olderBuffer.data () + 1, olderBuffer.size () - 1));
	assert (!newer->IsNewer (newerBuffer.data () + 1, newerBuffer.size () - 1));

	// A genuinely newer reply still replaces the cached entry.
	auto newerStill = SealEncryptedLeaseSet2 (keys, inner, now - 30, 900);
	store (ParseOuterEncrypted (newerStill));
	assert (cached->GetPublishedTimestamp () == now - 30);
}
}

int main ()
{
	TestOuterTimestampSurvivesBothParses ();
	TestOlderReplyDoesNotReplaceNewer ();
	return 0;
}

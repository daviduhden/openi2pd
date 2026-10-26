/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: test data-storage protocol.
 *
 * Checks the version-3 data-storage frame family, the canonical artifact name
 * matrix, payload/name bounds, exact-length framing, the endpoint allowlist,
 * the generation-fenced reply predicate and the restart backoff schedule.
 * No socket, worker process or live router is involved.
 * Prepared for manual CI only; this file must not be executed locally.
 */

#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "Base.h"
#include "PersistentStorage.h"
#include "privsep/DataStorage.h"
#include "privsep/Protocol.h"

using namespace i2p::privsep;
using i2p::data::StorageArtifact;

namespace
{
std::string Header (uint32_t magic, uint16_t version, uint16_t type, uint32_t requestId, uint32_t length)
{
	PayloadWriter writer;
	writer.WriteU32 (magic);
	writer.WriteU16 (version);
	writer.WriteU16 (type);
	writer.WriteU32 (requestId);
	writer.WriteU32 (length);
	return writer.Data ();
}

std::string Base64Name (uint8_t seed)
{
	std::array<uint8_t, 32> identity { };
	for (size_t i = 0; i < identity.size (); ++i) identity[i] = static_cast<uint8_t> (seed + i);
	return i2p::data::ByteStreamToBase64 (identity.data (), identity.size ());
}

std::string Base32Name (uint8_t seed)
{
	std::array<uint8_t, 32> identity { };
	for (size_t i = 0; i < identity.size (); ++i) identity[i] = static_cast<uint8_t> (seed * 3 + i);
	return i2p::data::ByteStreamToBase32 (identity.data (), identity.size ());
}

std::string BuildRequest (uint8_t kind, std::string_view name, std::string_view payload)
{
	PayloadWriter writer;
	writer.WriteU8 (kind);
	writer.WriteU8 (static_cast<uint8_t> (name.size ()));
	writer.WriteRaw (name);
	writer.WriteU32 (static_cast<uint32_t> (payload.size ()));
	writer.WriteRaw (payload);
	return writer.Data ();
}
}

int main ()
{
	// Versioning is per type: the data-storage family only exists from
	// version 3, and a version-1/2 header must not carry it.
	assert (PROTOCOL_VERSION == 5);
	assert (MIN_PROTOCOL_VERSION == 1);
	for (uint16_t version : { uint16_t { 1 }, uint16_t { 2 } })
	{
		auto refused
			= Decode (Header (PROTOCOL_MAGIC, version, static_cast<uint16_t> (MessageType::DataStorageRequest), 1, 0));
		assert (!refused && refused.error ().code == ErrorCode::UnsupportedVersion);
	}
	for (uint16_t version : { uint16_t { 3 }, uint16_t { 4 } })
		assert (Decode (Header (PROTOCOL_MAGIC, version, static_cast<uint16_t> (MessageType::DataStorageRequest), 1, 0))
				.has_value ());
	// Cancel is version 4 only.
	assert (!Decode (Header (PROTOCOL_MAGIC, 3, static_cast<uint16_t> (MessageType::Cancel), 0, 0)).has_value ());
	assert (Decode (Header (PROTOCOL_MAGIC, 4, static_cast<uint16_t> (MessageType::Cancel), 0, 0)).has_value ());
	// The new types are known and the boundary above them is not.
	for (uint16_t type = static_cast<uint16_t> (MessageType::ConfigureTorrentStorageSupervised);
		type <= static_cast<uint16_t> (MessageType::DataStorageReply); ++type)
		assert (Decode (Header (PROTOCOL_MAGIC, PROTOCOL_VERSION, type, 1, 0)).has_value ());
	assert (MAX_MESSAGE_TYPE == static_cast<uint16_t> (MessageType::WorkerHello));
	assert (!Decode (Header (PROTOCOL_MAGIC, PROTOCOL_VERSION, MAX_MESSAGE_TYPE + 1, 1, 0)).has_value ());

	// The data-storage endpoint can neither log nor touch the fixed router
	// files, and the other endpoints cannot submit artifact operations.
	assert (AllowsOperation (EndpointRole::DataStorage, MessageType::ConfigureDataStorage));
	assert (AllowsOperation (EndpointRole::DataStorage, MessageType::DataStorageRequest));
	assert (AllowsOperation (EndpointRole::DataStorage, MessageType::Ping));
	assert (AllowsOperation (EndpointRole::DataStorage, MessageType::Shutdown));
	assert (!AllowsOperation (EndpointRole::DataStorage, MessageType::LogRecord));
	assert (!AllowsOperation (EndpointRole::DataStorage, MessageType::ConfigureLog));
	assert (!AllowsOperation (EndpointRole::DataStorage, MessageType::RouterStorageRequest));
	assert (!AllowsOperation (EndpointRole::DataStorage, MessageType::ConfigureRouterStorage));
	assert (!AllowsOperation (EndpointRole::DataStorage, MessageType::TorrentStorageRequest));
	assert (!AllowsOperation (EndpointRole::DataStorage, MessageType::Error));
	assert (!AllowsOperation (EndpointRole::Log, MessageType::DataStorageRequest));
	assert (!AllowsOperation (EndpointRole::RouterStorage, MessageType::DataStorageRequest));
	assert (!AllowsOperation (EndpointRole::Supervisor, MessageType::ConfigureDataStorage));

	// Artifact kinds are exactly the three hash-named artifacts.
	assert (IsKnownArtifactKind (static_cast<uint8_t> (StorageArtifact::RouterInfoCache)));
	assert (IsKnownArtifactKind (static_cast<uint8_t> (StorageArtifact::PeerProfile)));
	assert (IsKnownArtifactKind (static_cast<uint8_t> (StorageArtifact::DestinationKeys)));
	assert (!IsKnownArtifactKind (0));
	assert (!IsKnownArtifactKind (4));
	assert (!IsKnownArtifactKind (255));
	assert (std::string_view (ArtifactDirectory (StorageArtifact::RouterInfoCache)) == "netDb");
	assert (std::string_view (ArtifactDirectory (StorageArtifact::PeerProfile)) == "peerProfiles");
	assert (std::string_view (ArtifactDirectory (StorageArtifact::DestinationKeys)) == "destinations");

	// Canonical name matrix.
	{
		const auto base64 = Base64Name (1);
		const auto base32 = Base32Name (1);
		assert (base64.size () == 44);
		assert (base32.size () == 52);
		assert (IsCanonicalArtifactName (StorageArtifact::RouterInfoCache, base64));
		assert (IsCanonicalArtifactName (StorageArtifact::PeerProfile, base64));
		// The fields of a canonical name are fixed: a name of the wrong length,
		// an upper-case base32 identity, path syntax, ':' and NUL are refused.
		assert (!IsCanonicalArtifactName (StorageArtifact::RouterInfoCache, base64.substr (0, 43)));
		assert (!IsCanonicalArtifactName (StorageArtifact::RouterInfoCache, base64 + "A"));
		assert (!IsCanonicalArtifactName (StorageArtifact::RouterInfoCache, ""));
		assert (!IsCanonicalArtifactName (StorageArtifact::RouterInfoCache, base64.substr (0, 40) + "AA=="));
		assert (!IsCanonicalArtifactName (StorageArtifact::RouterInfoCache, base64.substr (0, 40) + "A==="));
		assert (!IsCanonicalArtifactName (StorageArtifact::PeerProfile, base32));
		assert (!IsCanonicalArtifactName (StorageArtifact::RouterInfoCache, std::string (base64).replace (1, 1, "/")));
		assert (!IsCanonicalArtifactName (StorageArtifact::RouterInfoCache, std::string (base64).replace (1, 1, "\\")));
		assert (!IsCanonicalArtifactName (StorageArtifact::RouterInfoCache, std::string (base64).replace (1, 1, ":")));
		assert (!IsCanonicalArtifactName (StorageArtifact::RouterInfoCache, std::string (base64).replace (1, 1, ".")));
		assert (!IsCanonicalArtifactName (StorageArtifact::RouterInfoCache, ".." + std::string (42, 'A')));
		assert (!IsCanonicalArtifactName (StorageArtifact::RouterInfoCache, std::string (base64).replace (2, 1, "\0")));
		assert (
			!IsCanonicalArtifactName (StorageArtifact::RouterInfoCache, std::string (MAX_ARTIFACT_NAME_SIZE + 1, 'A')));
		// Destination keys: <52 base32>.<decimal key type>, key type 0 is
		// ElGamal; a leading zero or any non-decimal spelling is refused.
		assert (IsCanonicalArtifactName (StorageArtifact::DestinationKeys, base32 + ".0"));
		assert (IsCanonicalArtifactName (StorageArtifact::DestinationKeys, base32 + ".1"));
		assert (IsCanonicalArtifactName (StorageArtifact::DestinationKeys, base32 + ".3"));
		assert (IsCanonicalArtifactName (StorageArtifact::DestinationKeys, base32 + ".65535"));
		assert (!IsCanonicalArtifactName (StorageArtifact::DestinationKeys, base32 + ".65536"));
		assert (!IsCanonicalArtifactName (StorageArtifact::DestinationKeys, base32 + ".00"));
		assert (!IsCanonicalArtifactName (StorageArtifact::DestinationKeys, base32 + ".01"));
		assert (!IsCanonicalArtifactName (StorageArtifact::DestinationKeys, base32 + "."));
		assert (!IsCanonicalArtifactName (StorageArtifact::DestinationKeys, base32));
		assert (!IsCanonicalArtifactName (StorageArtifact::DestinationKeys, base32 + ".3x"));
		assert (!IsCanonicalArtifactName (StorageArtifact::DestinationKeys, base32 + ".-3"));
		assert (!IsCanonicalArtifactName (StorageArtifact::DestinationKeys, base32 + ". 3"));
		std::string upper = base32;
		for (auto & ch : upper) ch = static_cast<char> (ch - 'a' + 'A');
		assert (!IsCanonicalArtifactName (StorageArtifact::DestinationKeys, upper + ".3"));
		// The derived path stays inside the kind directory.
		assert (ArtifactRelativePath (StorageArtifact::RouterInfoCache, base64)
			== "netDb/r" + std::string (1, base64[0]) + "/routerInfo-" + base64 + ".dat");
		assert (ArtifactRelativePath (StorageArtifact::PeerProfile, base64)
			== "peerProfiles/p" + std::string (1, base64[0]) + "/profile-" + base64 + ".txt");
		assert (ArtifactRelativePath (StorageArtifact::DestinationKeys, base32 + ".3")
			== "destinations/" + base32 + ".3.dat");
		assert (ArtifactRelativePath (StorageArtifact::RouterInfoCache, "../escape").empty ());
	}

	// Target metadata gate: regular, same owner and exactly one link.
	assert (AcceptsArtifactTarget (true, 1000, 1000, 1));
	assert (!AcceptsArtifactTarget (false, 1000, 1000, 1)); // directory/symlink/other
	assert (!AcceptsArtifactTarget (true, 1001, 1000, 1)); // foreign owner
	assert (!AcceptsArtifactTarget (true, 1000, 1000, 2)); // hard link

	// Request codec round trip and bounds.
	{
		const auto base64 = Base64Name (7);
		const std::string payload (32, 'x');
		auto encoded
			= EncodeDataStorageRequest (static_cast<uint8_t> (StorageArtifact::RouterInfoCache), base64, payload);
		assert (encoded.has_value ());
		auto decoded = DecodeDataStorageRequest (*encoded);
		assert (decoded.has_value ());
		assert (decoded->kind == static_cast<uint8_t> (StorageArtifact::RouterInfoCache));
		assert (decoded->name == base64);
		assert (decoded->payload == payload);
		// Zero payload is the removal form.
		auto removal = EncodeDataStorageRequest (static_cast<uint8_t> (StorageArtifact::DestinationKeys), base64, { });
		assert (removal.has_value ());
		auto removed = DecodeDataStorageRequest (*removal);
		assert (removed.has_value () && removed->payload.empty ());
		// Name bounds: empty is refused, the documented maximum is inclusive.
		assert (!EncodeDataStorageRequest (1, "", payload).has_value ());
		assert (EncodeDataStorageRequest (1, std::string (MAX_ARTIFACT_NAME_SIZE, 'a'), payload).has_value ());
		assert (!EncodeDataStorageRequest (1, std::string (MAX_ARTIFACT_NAME_SIZE + 1, 'a'), payload).has_value ());
		assert (!EncodeDataStorageRequest (0, base64, payload).has_value ());
		assert (!EncodeDataStorageRequest (1, std::string ("bad\0name", 8), payload).has_value ());
		// Payload cap: the limit is accepted, one byte more is TooLarge.
		assert (EncodeDataStorageRequest (1, base64, std::string (MAX_ARTIFACT_PAYLOAD_SIZE, 'p')).has_value ());
		auto tooLargePayload = EncodeDataStorageRequest (1, base64, std::string (MAX_ARTIFACT_PAYLOAD_SIZE + 1, 'p'));
		assert (!tooLargePayload.has_value ());
		assert (tooLargePayload.error ().code == ErrorCode::TooLarge);
	}

	// Decoder framing: truncation, trailing bytes, mismatched lengths, zero
	// kind/name and an oversized declared payload are all rejected.
	{
		const auto base64 = Base64Name (9);
		const auto request = BuildRequest (static_cast<uint8_t> (StorageArtifact::PeerProfile), base64, "abc");
		assert (DecodeDataStorageRequest (request).has_value ());
		assert (!DecodeDataStorageRequest ("").has_value ());
		assert (!DecodeDataStorageRequest (request.substr (0, request.size () - 1)).has_value ());
		assert (!DecodeDataStorageRequest (request + "x").has_value ());
		// Trailing bytes after a declared payload that fits are rejected.
		auto trailing = BuildRequest (1, base64, "abc");
		trailing.push_back ('x');
		auto trailingDecoded = DecodeDataStorageRequest (trailing);
		assert (!trailingDecoded.has_value ());
		assert (trailingDecoded.error ().code == ErrorCode::BadField);
		// Declared payload length disagrees with the remaining bytes.
		PayloadWriter mismatched;
		mismatched.WriteU8 (1);
		mismatched.WriteU8 (static_cast<uint8_t> (base64.size ()));
		mismatched.WriteRaw (base64);
		mismatched.WriteU32 (4);
		mismatched.WriteRaw ("abc");
		assert (!DecodeDataStorageRequest (mismatched.Data ()).has_value ());
		// Zero kind and zero name length.
		assert (!DecodeDataStorageRequest (BuildRequest (0, base64, "abc")).has_value ());
		assert (!DecodeDataStorageRequest (BuildRequest (1, "", "abc")).has_value ());
		// Name length one past the bound.
		PayloadWriter overlong;
		overlong.WriteU8 (1);
		overlong.WriteU8 (static_cast<uint8_t> (MAX_ARTIFACT_NAME_SIZE + 1));
		overlong.WriteRaw (std::string (MAX_ARTIFACT_NAME_SIZE + 1, 'a'));
		overlong.WriteU32 (0);
		assert (!DecodeDataStorageRequest (overlong.Data ()).has_value ());
		// Embedded NUL in a name is a field error, not a truncation.
		PayloadWriter nul;
		nul.WriteU8 (1);
		nul.WriteU8 (5);
		nul.WriteRaw (std::string ("a\0bcd", 5));
		nul.WriteU32 (0);
		assert (!DecodeDataStorageRequest (nul.Data ()).has_value ());
		// Declared payload above the cap is TooLarge before allocation.
		PayloadWriter oversized;
		oversized.WriteU8 (1);
		oversized.WriteU8 (1);
		oversized.WriteRaw ("a");
		oversized.WriteU32 (MAX_ARTIFACT_PAYLOAD_SIZE + 1);
		auto oversizedDecoded = DecodeDataStorageRequest (oversized.Data ());
		assert (!oversizedDecoded.has_value ());
		assert (oversizedDecoded.error ().code == ErrorCode::TooLarge);
	}

	// Reply codec: exactly six bytes, known status, non-zero generation.
	{
		const auto reply = EncodeDataStorageReply (static_cast<uint8_t> (DataStorageStatus::Ok), 2, 9);
		auto decoded = DecodeDataStorageReply (reply);
		assert (decoded.has_value ());
		assert (std::get<0> (*decoded) == static_cast<uint8_t> (DataStorageStatus::Ok));
		assert (std::get<1> (*decoded) == 2);
		assert (std::get<2> (*decoded) == 9);
		assert (!DecodeDataStorageReply (reply.substr (0, 5)).has_value ());
		assert (!DecodeDataStorageReply (reply + "x").has_value ());
		assert (!DecodeDataStorageReply (EncodeDataStorageReply (3, 1, 1)).has_value ());
		assert (!DecodeDataStorageReply (EncodeDataStorageReply (0, 1, 0)).has_value ());
	}

	// Configuration codec: absolute path, identity and a non-zero generation.
	{
		auto encoded = EncodeConfigureDataStorage ("/var/lib/openi2pd", 12, 34, 7);
		assert (encoded.has_value ());
		auto decoded = DecodeConfigureDataStorage (*encoded);
		assert (decoded.has_value ());
		assert (std::get<0> (*decoded) == "/var/lib/openi2pd");
		assert (std::get<1> (*decoded) == 12);
		assert (std::get<2> (*decoded) == 34);
		assert (std::get<3> (*decoded) == 7);
		assert (!EncodeConfigureDataStorage ("/var/lib/openi2pd", 12, 34, 0).has_value ());
		assert (!EncodeConfigureDataStorage ("relative", 12, 34, 7).has_value ());
		assert (!DecodeConfigureDataStorage (*encoded + "x").has_value ());
		// A relative or empty root is refused by the decoder as well.
		PayloadWriter relative;
		relative.WriteString ("relative");
		relative.WriteU64BE (1);
		relative.WriteU64BE (2);
		relative.WriteU32 (1);
		assert (!DecodeConfigureDataStorage (relative.Data ()).has_value ());
		PayloadWriter zeroGeneration;
		zeroGeneration.WriteString ("/tmp");
		zeroGeneration.WriteU64BE (1);
		zeroGeneration.WriteU64BE (2);
		zeroGeneration.WriteU32 (0);
		assert (!DecodeConfigureDataStorage (zeroGeneration.Data ()).has_value ());
	}

	// Generation/id fencing: only the in-flight request of the current
	// generation is accepted.
	assert (IsCurrentDataStorageReply (4, 11, 4, 11));
	assert (!IsCurrentDataStorageReply (3, 11, 4, 11)); // stale generation
	assert (!IsCurrentDataStorageReply (5, 11, 4, 11)); // future generation
	assert (!IsCurrentDataStorageReply (4, 10, 4, 11)); // abandoned request
	assert (!IsCurrentDataStorageReply (0, 11, 4, 11));

	// Restart backoff: 250 ms doubling to a 30 s cap, 8 attempts per window.
	{
		const auto start = std::chrono::steady_clock::time_point (std::chrono::seconds (1000));
		StorageClientRestart policy;
		policy.Reset (start);
		assert (policy.Attempts () == 0);
		assert (policy.CanAttempt (start));
		std::chrono::milliseconds delay (250);
		for (unsigned attempt = 1; attempt <= 7; ++attempt)
		{
			const auto now = start + std::chrono::seconds (attempt);
			assert (policy.RecordFailure (now));
			assert (policy.Attempts () == attempt);
			// The recorded failure schedules the retry after the current delay
			// and only then doubles it.
			assert (policy.NextAttempt () == now + delay);
			assert (!policy.CanAttempt (now));
			assert (policy.CanAttempt (now + delay));
			delay = std::min (delay * 2, std::chrono::milliseconds (30000));
			assert (policy.Backoff () == delay);
		}
		// The eighth failure exhausts the window budget: no further attempt is
		// allowed even after the delay has elapsed.
		const auto now = start + std::chrono::seconds (9);
		assert (!policy.RecordFailure (now));
		assert (policy.Attempts () == 8);
		assert (!policy.CanAttempt (now + std::chrono::seconds (31)));
		// The cap never exceeds 30 s.
		assert (policy.Backoff () == std::chrono::milliseconds (30000));
		// A quiet period starts a new window.
		assert (policy.CanAttempt (now + std::chrono::seconds (61)));
		assert (policy.Attempts () == 0);
	}
	return 0;
}

/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: fuzz torrent extension decoders.
 *
 * Fuzzes the pure BEP11 "i2p_pex", i2p_dht and BEP10
 * handshake-dictionary
 * parsers. Every input must be decoded without throwing or crashing,
 * and every
 * output must stay within the documented bounds (at most
 * MAX_PEX_RECEIVE_PEERS
 * destinations, port in 1..65534, no zero destinations).
 */

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "Torrents.h"

#ifndef NO_TORRENTS

extern "C" int LLVMFuzzerTestOneInput (const uint8_t * data, size_t size)
{
	std::string_view input (reinterpret_cast<const char *> (data), size);

	// BEP11 PEX: the decoder is the only authority on "added"; it must
	// drop zero/duplicate destinations, cap the result and report
	// malformed input instead of returning a partial list.
	{
		auto decoded = i2p::torrents::DecodePEXAdded (input);
		assert (decoded.peers.size () <= i2p::torrents::MAX_PEX_RECEIVE_PEERS);
		if (decoded.malformed)
			assert (decoded.peers.empty ());
		else
			for (const auto & peer : decoded.peers) assert (!peer.IsZero ());
		// A well-formed message built from the same bytes can only describe
		// input.size() / 32 destinations and never more. Only whole hashes
		// are used: a truncated tail is *supposed* to be rejected.
		if (size <= i2p::torrents::PEER_CONNECTION_RECEIVE_BUFFER_SIZE)
		{
			const size_t wholeHashes = (size / i2p::data::IdentHash::len) * i2p::data::IdentHash::len;
			const std::string hashBytes (input.substr (0, wholeHashes));
			const std::string message
				= i2p::torrents::CreateDictionary ({ { "added", i2p::torrents::CreateByteString (hashBytes) } });
			auto rebuilt = i2p::torrents::DecodePEXAdded (message);
			assert (!rebuilt.malformed);
			assert (rebuilt.peers.size () <= wholeHashes / i2p::data::IdentHash::len);
			assert (rebuilt.peers.size () <= i2p::torrents::MAX_PEX_RECEIVE_PEERS);
			// Every returned destination is a 32-byte slice of the input.
			for (const auto & peer : rebuilt.peers)
				assert (hashBytes.find (
							std::string (reinterpret_cast<const char *> (peer.data ()), i2p::data::IdentHash::len))
					!= std::string::npos);
		}
	}

	// i2p_dht port hint: an out-of-range or missing port is "no port", and
	// a malformed message never carries a usable port.
	{
		auto decoded = i2p::torrents::DecodeDHTHint (input);
		if (decoded.port) assert (*decoded.port >= 1 && *decoded.port <= 65534);
		if (decoded.malformed) assert (!decoded.port);
		// The port/rport relationship must never wrap: a hint around the
		// 16-bit boundary is either a tolerated mismatch or a plain mismatch.
		const int64_t port = static_cast<int64_t> (size % 65540);
		const std::string message = i2p::torrents::CreateDictionary (
			{ { "port", i2p::torrents::CreateInteger (port) }, { "rport", i2p::torrents::CreateInteger (port + 1) } });
		auto rebuilt = i2p::torrents::DecodeDHTHint (message);
		assert (!rebuilt.malformed);
		if (rebuilt.port) assert (*rebuilt.port >= 1 && *rebuilt.port <= 65534);
	}

	// The BEP10 handshake is a bencode dictionary; ParseDictionary is the
	// reusable, connection-free part of the handshake path.
	{
		size_t parsed = i2p::torrents::ParseDictionary (input);
		assert (parsed <= size);
		(void)parsed;
		// Nested "m" dictionaries use the same entry point with the depth
		// guard, so a deeply nested corpus cannot recurse without a bound.
		(void)i2p::torrents::ParseDictionary (input,
			[] (std::string_view key, std::string_view value) -> size_t
			{
				if (key == "m") return i2p::torrents::ParseDictionary (value, nullptr, 1);
				return 0;
			});
	}

	// Candidate lifetime arithmetic must stay total for arbitrary clock
	// and failure values (no wrap, no unbounded loop).
	{
		uint64_t clock = 0;
		uint32_t failures = 0;
		if (size >= 8)
			for (size_t i = 0; i < 8; ++i) clock = (clock << 8) | data[i];
		if (size >= 12)
			for (size_t i = 8; i < 12; ++i) failures = (failures << 8) | data[i];
		constexpr std::array origins {
			i2p::torrents::ePeerConnectionOriginPEX, i2p::torrents::ePeerConnectionOriginDHT };
		for (auto origin : origins)
		{
			(void)i2p::torrents::PeerCandidatePolicy::IsUsable (1000, 0, failures, clock, origin);
			(void)i2p::torrents::PeerCandidatePolicy::IsUsable (clock, clock, failures, 1000, origin);
		}
		assert (
			i2p::torrents::PeerCandidatePolicy::RetryDelay (failures) <= i2p::torrents::PEER_CANDIDATE_MAX_RETRY_DELAY);
		assert (i2p::torrents::PeerCandidatePolicy::RetryDelay (failures) >= i2p::torrents::PEER_CANDIDATE_RETRY_DELAY);
	}

	return 0;
}

#else
extern "C" int LLVMFuzzerTestOneInput (const uint8_t * data, size_t size)
{
	(void)data;
	(void)size;
	return 0;
}
#endif

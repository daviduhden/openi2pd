/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * Allocation-failure transactions in the tracker and DHT registries.
 * No executor runs and no live peer is required. Checkpoints precede real
 * allocating operations and every fixture witnesses its injected fault.
 */
#include <cassert>
#include <cstring>
#include <initializer_list>
#include <memory>
#include <new>
#include <boost/asio/io_context.hpp>
#include "Destination.h"
#include "TorrentsDHT.h"
#include "TorrentsTunnel.h"

namespace i2p::torrents
{
struct TorrentAdmissionTestAccess
{
	using TokenPoint = DHTTokenStore::AdmissionCheckpoint;
	using HashPoint = PrivateInfoHashSet::AdmissionCheckpoint;
	inline static unsigned target = 0, observed = 0;
	static void Fail ()
	{
		++observed;
		throw std::bad_alloc ();
	}
	static void TokenFault (TokenPoint point)
	{
		if (static_cast<unsigned> (point) == target) Fail ();
	}
	static void HashFault (HashPoint point)
	{
		if (static_cast<unsigned> (point) == target) Fail ();
	}

	static void Trackers ()
	{
		// A null destination invokes the global factory and starts workers.
		// Keep this registry fixture on an explicitly unstarted local owner.
		boost::asio::io_context service;
		auto keys = i2p::data::PrivateKeys::CreateRandomKeys (
			i2p::data::SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519, i2p::data::CRYPTO_KEY_TYPE_ECIES_X25519_AEAD, true);
		auto owner = std::make_shared<i2p::client::ClientDestination> (service, keys, false);
		TorrentsTunnel tunnel ("admission-test", owner, "/unused-authority", "", false);
		assert (tunnel.AddTracker ("http://first.i2p/announce", false) == 0);
		observed = 0;
		tunnel.m_TrackerAppendCheckpointForTesting = &Fail;
		try
		{
			tunnel.AddTracker ("http://second.i2p/announce", false);
			assert (false);
		}
		catch (const std::bad_alloc &)
		{
			assert (observed == 1);
		}
		tunnel.m_TrackerAppendCheckpointForTesting = nullptr;
		assert (tunnel.m_Trackers.size () == 1 && tunnel.m_TrackerSlots.size () == 1);
		assert (tunnel.GetTrackerAnnounce (0) == "http://first.i2p/announce");
		assert (tunnel.AddTracker ("http://second.i2p/announce", false) == 1);
		assert (tunnel.m_Trackers.size () == 2 && tunnel.m_TrackerSlots.size () == 2);
	}

	static Torrent::InfoHash Hash (unsigned value)
	{
		Torrent::InfoHash hash { };
		std::memcpy (hash.data (), &value, sizeof (value));
		return hash;
	}
	static void PrivateHashes ()
	{
		for (unsigned point = 0; point < 2; ++point)
		{
			PrivateInfoHashSet hashes;
			for (unsigned i = 0; i < PrivateInfoHashSet::MaxHashes; ++i) hashes.Insert (Hash (i));
			target = point;
			observed = 0;
			hashes.m_AdmissionCheckpointForTesting = &HashFault;
			try
			{
				hashes.Insert (Hash (PrivateInfoHashSet::MaxHashes));
				assert (false);
			}
			catch (const std::bad_alloc &)
			{
				assert (observed == 1);
			}
			hashes.m_AdmissionCheckpointForTesting = nullptr;
			assert (hashes.Size () == PrivateInfoHashSet::MaxHashes);
			assert (hashes.m_Order.size () == hashes.Size ());
			assert (hashes.Contains (Hash (0)) && !hashes.Contains (Hash (PrivateInfoHashSet::MaxHashes)));
			assert (
				hashes.Insert (Hash (PrivateInfoHashSet::MaxHashes)) == PrivateInfoHashSet::Admission::EvictedOldest);
			assert (!hashes.Contains (Hash (0)));
		}
	}

	static DHTIncomingToken Token (unsigned value, bool otherNode = false)
	{
		DHTIncomingToken token { };
		token.token.assign (8, '\0');
		std::memcpy (token.token.data (), &value, sizeof (value));
		token.nodeID[0] = otherNode ? 2 : 1;
		token.port = 1234;
		token.issued = 1;
		return token;
	}
	static void Tokens ()
	{
		for (bool otherNode : { false, true })
			for (unsigned point = 0; point < 4; ++point)
			{
				DHTTokenStore store;
				for (unsigned i = 0; i < DHTTokenStore::MaxTokensPerNode; ++i) store.Issue (Token (i));
				const auto candidate = Token (100, otherNode);
				target = point;
				observed = 0;
				store.m_AdmissionCheckpointForTesting = &TokenFault;
				try
				{
					store.Issue (candidate);
					assert (false);
				}
				catch (const std::bad_alloc &)
				{
					assert (observed == 1);
				}
				store.m_AdmissionCheckpointForTesting = nullptr;
				assert (store.Size () == DHTTokenStore::MaxTokensPerNode);
				assert (store.m_Order.size () == store.Size ());
				assert (store.m_ByNode.size () == 1);
				assert (store.Contains (Token (0).token) && !store.Contains (candidate.token));
				assert (store.SizeForNode (Token (0).nodeID) == DHTTokenStore::MaxTokensPerNode);
				const auto result = store.Issue (candidate);
				assert (result
					== (otherNode ? DHTTokenStore::Admission::Issued : DHTTokenStore::Admission::ReplacedOwnOldest));
				assert (store.Contains (candidate.token));
				store.Clear ();
				assert (store.m_Order.empty () && store.m_Tokens.empty () && store.m_ByNode.empty ());
			}
	}
};
}

int main ()
{
	i2p::torrents::TorrentAdmissionTestAccess::Trackers ();
	i2p::torrents::TorrentAdmissionTestAccess::PrivateHashes ();
	i2p::torrents::TorrentAdmissionTestAccess::Tokens ();
}

/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * Pre-SYN packet ownership, admission and expiry authority. Fixtures
 * use an unstarted destination; no network or timer wait is required.
 */
#include <cassert>
#include <cstddef>
#include <cstring>
#include <initializer_list>
#include <memory>
#include <new>
#include "Destination.h"
#include "Modern.h"
#include "Streaming.h"

namespace i2p::stream
{
struct StreamingDestinationPacketTestAccess
{
	using Point = StreamingDestination::SavedPacketCheckpoint;
	struct InjectedFailure final : std::bad_alloc
	{
	};
	inline static Point selected = Point::Batch;
	inline static unsigned observed = 0;
	static void Fail (Point point)
	{
		if (point == selected)
		{
			++observed;
			throw InjectedFailure { };
		}
	}
	static std::unique_ptr<Packet> Make (StreamingDestination & destination, uint32_t seq = 0)
	{
		std::unique_ptr<Packet> packet (destination.NewPacket ());
		std::memset (packet->buf, 0, 23);
		packet->len = 23; // A complete fixed header and one payload byte.
		htobe32buf (packet->buf + 8, seq);
		htobe16buf (packet->buf + 18, PACKET_FLAG_NO_ACK);
		return packet;
	}
	static void CheckFailure (StreamingDestination & destination, Point point, bool existing)
	{
		constexpr uint32_t id = 17;
		destination.m_SavedPackets.clear ();
		if (existing) destination.SaveIncomingPacket (id, Make (destination));
		auto prior = existing ? destination.m_SavedPackets.at (id) : nullptr;
		selected = point;
		observed = 0;
		destination.m_SavedPacketCheckpointForTesting = &Fail;
		auto clear = i2p::modern::ScopeExit ([&] { destination.m_SavedPacketCheckpointForTesting = nullptr; });
		bool threw = false;
		try
		{
			destination.SaveIncomingPacket (id, Make (destination, 1));
		}
		catch (const InjectedFailure &)
		{
			threw = true;
		}
		assert (threw && observed == 1);
		const std::size_t expectedSaved = existing ? 1 : 0;
		assert (destination.m_SavedPackets.size () == expectedSaved);
		if (existing)
		{
			assert (destination.m_SavedPackets.at (id) == prior);
			assert (prior->packets.size () == 1);
		}
	}
	static void ConsumerFault (Stream::PacketCheckpoint point)
	{
		if (point == Stream::PacketCheckpoint::ReceivePublished)
		{
			++observed;
			throw InjectedFailure { };
		}
	}
	static void CheckDrain (StreamingDestination & destination, Stream & stream, bool fault)
	{
		constexpr uint32_t id = 19;
		destination.m_SavedPackets.clear ();
		destination.SaveIncomingPacket (id, Make (destination));
		destination.SaveIncomingPacket (id, Make (destination, 1));
		std::weak_ptr<StreamingDestination::SavedPacketBatch> old = destination.m_SavedPackets.at (id);
		stream.m_PacketCheckpointForTesting = fault ? &ConsumerFault : nullptr;
		auto clear = i2p::modern::ScopeExit ([&] { stream.m_PacketCheckpointForTesting = nullptr; });
		observed = 0;
		bool threw = false;
		try
		{
			destination.DrainSavedPackets (id, stream);
		}
		catch (const InjectedFailure &)
		{
			threw = true;
		}
		assert (threw == fault && observed == (fault ? 1u : 0u));
		assert (destination.m_SavedPackets.empty ());
		assert (old.expired ());
		const std::size_t expectedReceived = fault ? 1 : 2;
		assert (stream.m_ReceiveQueue.size () == expectedReceived);
	}
	static void CheckExpiry (StreamingDestination & destination)
	{
		constexpr uint32_t id = 23;
		destination.m_SavedPackets.clear ();
		destination.SaveIncomingPacket (id, Make (destination));
		auto previous = destination.m_SavedPackets.at (id);
		destination.m_SavedPackets.erase (id);
		destination.SaveIncomingPacket (id, Make (destination));
		auto successor = destination.m_SavedPackets.at (id);
		destination.ExpireSavedPackets (id, previous, { });
		assert (destination.m_SavedPackets.at (id) == successor);
		destination.ExpireSavedPackets (id, successor, boost::asio::error::operation_aborted);
		assert (destination.m_SavedPackets.at (id) == successor);
		destination.ExpireSavedPackets (id, successor, { });
		assert (destination.m_SavedPackets.empty ());
	}
	static void CheckBounds (StreamingDestination & destination)
	{
		destination.m_SavedPackets.clear ();
		for (size_t i = 0; i <= MAX_SAVED_PACKETS_PER_STREAM; ++i)
			destination.SaveIncomingPacket (29, Make (destination));
		assert (destination.m_SavedPackets.at (29)->packets.size () == MAX_SAVED_PACKETS_PER_STREAM);
		destination.m_SavedPackets.clear ();
		for (uint32_t i = 1; i <= MAX_SAVED_STREAM_IDS + 1; ++i) destination.SaveIncomingPacket (i, Make (destination));
		assert (destination.m_SavedPackets.size () == MAX_SAVED_STREAM_IDS);
		assert (!destination.m_SavedPackets.contains (MAX_SAVED_STREAM_IDS + 1));
		destination.StopInternal ();
		assert (destination.m_SavedPackets.empty ());
	}
	static void Save (StreamingDestination & destination) { destination.SaveIncomingPacket (31, Make (destination)); }
};
}

int main ()
{
	using namespace i2p::stream;
	auto service = std::make_unique<boost::asio::io_context> ();
	auto keys = i2p::data::PrivateKeys::CreateRandomKeys (
		i2p::data::SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519, i2p::data::CRYPTO_KEY_TYPE_ECIES_X25519_AEAD, true);
	auto owner = std::make_shared<i2p::client::ClientDestination> (*service, keys, false);
	auto destination = std::make_shared<StreamingDestination> (owner);
	using Access = StreamingDestinationPacketTestAccess;
	for (auto point :
		{ Access::Point::Batch, Access::Point::PacketNode, Access::Point::TimerWait, Access::Point::TableNode })
		Access::CheckFailure (*destination, point, false);
	Access::CheckFailure (*destination, Access::Point::PacketNode, true);
	for (bool fault : { false, true })
	{
		auto stream = std::make_shared<Stream> (*service, destination);
		Access::CheckDrain (*destination, *stream, fault);
	}
	Access::CheckExpiry (*destination);
	Access::CheckBounds (*destination);
	Access::Save (*destination);
	// No handler is dispatched, including canceled expiry completions.
	// Neither the queued weak wait nor a packet's deleter retains owners.
	std::weak_ptr<StreamingDestination> weak = destination;
	destination.reset ();
	assert (weak.expired ());
	owner.reset ();
	service.reset ();
}

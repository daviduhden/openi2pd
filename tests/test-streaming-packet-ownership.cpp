/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * Allocation-failure ownership boundaries for streaming packets.
 * Sanitizers also check that failed admission and abandoned control
 * handlers release their packets without a borrowing pool deleter.
 */
#include <cassert>
#include <cstring>
#include <initializer_list>
#include <memory>
#include <new>
#include "Destination.h"
#include "Streaming.h"

namespace i2p::stream
{
struct StreamPacketOwnershipTestAccess
{
	using Point = Stream::PacketCheckpoint;
	inline static Point target = Point::SaveNode;
	inline static unsigned observed = 0;
	static void Check (Point point)
	{
		if (point == target)
		{
			++observed;
			throw std::bad_alloc ();
		}
	}
	static Packet * Make (StreamingDestination & destination)
	{
		auto * packet = destination.NewPacket ();
		std::memset (packet->buf, 0, 23);
		packet->len = 23; // Fixed header plus one payload byte.
		return packet;
	}
	static void Fail (Stream & stream, StreamingDestination & destination, Point point)
	{
		target = point;
		observed = 0;
		stream.m_PacketCheckpointForTesting = &Check;
		bool threw = false;
		try
		{
			if (point == Point::SaveNode)
				stream.SavePacket (Make (destination));
			else if (point == Point::ReceiveNode)
				stream.ProcessPacket (Make (destination));
			else
			{
				stream.m_LastReceivedSequenceNumber = -1;
				stream.SavePacket (Make (destination));
				stream.ProcessSavedPackets ();
			}
		}
		catch (const std::bad_alloc &)
		{
			threw = true;
		}
		stream.m_PacketCheckpointForTesting = nullptr;
		assert (threw && observed == 1);
		assert (stream.m_SavedPackets.empty ());
		assert (stream.m_ReceiveQueue.size () == (point == Point::ReceivePublished ? 1 : 0));
	}
	static void Drain (Stream & stream, StreamingDestination & destination)
	{
		stream.m_LastReceivedSequenceNumber = -1;
		stream.SavePacket (Make (destination));
		stream.ProcessSavedPackets ();
		assert (stream.m_SavedPackets.empty ());
		assert (stream.m_ReceiveQueue.size () == 1);
	}
	static void QueueControl (Stream & stream)
	{
		stream.m_DontSign = true;
		stream.SendClose ();
		stream.SendReset ();
	}
	static void Stop (Stream & stream) { stream.Terminate (false); }
	static bool HasSent (Stream & stream) { return !stream.m_SentPackets.empty (); }
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
	using Access = StreamPacketOwnershipTestAccess;
	for (auto point : { Access::Point::SaveNode, Access::Point::ReceiveNode, Access::Point::ReceivePublished })
	{
		auto stream = std::make_shared<Stream> (*service, destination);
		Access::Fail (*stream, *destination, point);
	}
	{
		auto stream = std::make_shared<Stream> (*service, destination);
		Access::Drain (*stream, *destination);
	}
	{
		auto stream = std::make_shared<Stream> (*service, destination);
		Access::QueueControl (*stream);
		Access::Stop (*stream);
		service->poll ();
		assert (!Access::HasSent (*stream));
		service->restart ();
	}
	// The executor never runs. Queued control packets must not retain
	// the stream or its destination; destroying the executor frees them.
	auto stream = std::make_shared<Stream> (*service, destination);
	std::weak_ptr<Stream> weak = stream;
	Access::QueueControl (*stream);
	stream.reset ();
	assert (weak.expired ());
	destination.reset ();
	owner.reset ();
	service.reset ();
	assert (weak.expired ());
}

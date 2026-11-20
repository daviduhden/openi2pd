/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * Atomic stream registry admission and identity-bound retirement.
 */
#include <cassert>
#include <memory>
#include <new>
#include "Destination.h"
#include "Streaming.h"

namespace i2p::stream
{
struct StreamingRegistrationTestAccess
{
	struct InjectedFailure final : std::bad_alloc
	{
	};
	inline static unsigned observed = 0;
	static void FailSecondIndex ()
	{
		++observed;
		throw InjectedFailure { };
	}
	static std::shared_ptr<Stream> Make (
		const std::shared_ptr<StreamingDestination> & destination, uint32_t local, uint32_t peer)
	{
		auto stream = std::make_shared<Stream> (destination->GetOwner ()->GetService (), destination);
		stream->SetStreamIDsForTesting (peer, local);
		return stream;
	}
	static void Run (const std::shared_ptr<StreamingDestination> & destination)
	{
		auto incumbent = Make (destination, 11, 17);
		destination->RegisterStream (incumbent, 17);
		assert (destination->m_Streams.at (11) == incumbent);
		assert (destination->m_IncomingStreams.at (17) == incumbent);

		// Failure is witnessed at the second insertion boundary, after the
		// first map has acquired ownership, and propagates unchanged.
		auto candidate = Make (destination, 12, 18);
		observed = 0;
		destination->m_RegistrationCheckpointForTesting = &FailSecondIndex;
		bool failed = false;
		try
		{
			destination->RegisterStream (candidate, 18);
		}
		catch (const InjectedFailure &)
		{
			failed = true;
		}
		destination->m_RegistrationCheckpointForTesting = nullptr;
		assert (failed && observed == 1);
		assert (destination->m_Streams.size () == 1);
		assert (destination->m_IncomingStreams.size () == 1);
		std::weak_ptr<Stream> abandoned = candidate;
		candidate.reset ();
		assert (abandoned.expired ());

		// Either occupied index refuses the complete transaction. Deleting
		// its rejected candidate cannot retire the incumbent's authority.
		for (bool localCollision : { false, true })
		{
			candidate = Make (destination, localCollision ? 11 : 12, 17);
			failed = false;
			try
			{
				destination->RegisterStream (candidate, 17);
			}
			catch (const StreamingDestination::RegistrationError &)
			{
				failed = true;
			}
			assert (failed);
			destination->DeleteStream (candidate);
			assert (destination->m_Streams.size () == 1);
			assert (destination->m_IncomingStreams.size () == 1);
			assert (destination->m_Streams.at (11) == incumbent);
			assert (destination->m_IncomingStreams.at (17) == incumbent);
		}
		candidate.reset ();
		// A pre-SYN stream has not acquired its peer ID yet. Rollback must
		// still retire the exact secondary entry, not erase key zero.
		auto preSyn = Make (destination, 13, 0);
		destination->RegisterStream (preSyn, 19);
		assert (destination->UnregisterStream (preSyn));
		assert (!destination->m_IncomingStreams.contains (19));
		preSyn.reset ();

		candidate = Make (destination, 0, 20);
		failed = false;
		try
		{
			destination->RegisterStream (candidate, 20);
		}
		catch (const StreamingDestination::RegistrationError &)
		{
			failed = true;
		}
		assert (failed && destination->m_Streams.size () == 1);
		candidate.reset ();

		// Successful retirement and key reuse preserve the replacement when
		// a stale strong reference requests deletion again.
		destination->DeleteStream (incumbent);
		auto replacement = Make (destination, 11, 17);
		destination->RegisterStream (replacement, 17);
		destination->DeleteStream (incumbent);
		assert (destination->m_Streams.at (11) == replacement);
		assert (destination->m_IncomingStreams.at (17) == replacement);
		destination->DeleteStream (replacement);
		assert (destination->m_Streams.empty ());
		assert (destination->m_IncomingStreams.empty ());

		// Outgoing admission touches only the primary index.
		auto outgoing = Make (destination, 21, 0);
		destination->RegisterStream (outgoing, std::nullopt);
		assert (destination->m_Streams.at (21) == outgoing);
		assert (destination->m_IncomingStreams.empty ());
		destination->DeleteStream (outgoing);

		// An external acceptor may publish effects before it throws. The
		// admission marker must already be set; rollback cannot undeliver.
		auto delivered = Make (destination, 22, 23);
		destination->RegisterStream (delivered, 23);
		unsigned callbacks = 0;
		destination->m_Acceptor = [&] (std::shared_ptr<Stream> stream)
		{
			assert (stream == delivered);
			++callbacks;
			throw InjectedFailure { };
		};
		bool admitted = false;
		failed = false;
		try
		{
			destination->AddPendingIncomingStream (delivered, &admitted);
		}
		catch (const InjectedFailure &)
		{
			failed = true;
		}
		destination->m_Acceptor = nullptr;
		assert (failed && admitted && callbacks == 1);
		assert (destination->m_PendingIncomingStreams.empty ());
		assert (destination->m_Streams.at (22) == delivered);
		destination->DeleteStream (delivered);
	}
};
}

int main ()
{
	auto service = std::make_unique<boost::asio::io_context> ();
	auto keys = i2p::data::PrivateKeys::CreateRandomKeys (
		i2p::data::SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519, i2p::data::CRYPTO_KEY_TYPE_ECIES_X25519_AEAD, true);
	auto owner = std::make_shared<i2p::client::ClientDestination> (*service, keys, false);
	auto destination = std::make_shared<i2p::stream::StreamingDestination> (owner);
	i2p::stream::StreamingRegistrationTestAccess::Run (destination);
	std::weak_ptr<i2p::stream::StreamingDestination> weak = destination;
	destination.reset ();
	assert (weak.expired ());
}

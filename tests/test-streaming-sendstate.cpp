/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: test streaming send state.
 *
 * Checks that no data packet is built once a stream reached CLOSE or RESET and
 * that a write queued in that state fails with operation_aborted instead of
 * being silently accepted. A still-open control stream must keep building
 * packets, so the assertion is not vacuous.
 *
 */

#include <cassert>
#include <cstdint>
#include <memory>

#include <boost/asio.hpp>

#include "Destination.h"
#include "Streaming.h"

namespace
{
struct Fixture
{
	boost::asio::io_context service;
	std::shared_ptr<i2p::client::ClientDestination> owner;
	std::shared_ptr<i2p::stream::StreamingDestination> destination;

	Fixture ()
	{
		auto keys = i2p::data::PrivateKeys::CreateRandomKeys (
			i2p::data::SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519, i2p::data::CRYPTO_KEY_TYPE_ECIES_X25519_AEAD, true);
		owner = std::make_shared<i2p::client::ClientDestination> (service, keys, false);
		destination = std::make_shared<i2p::stream::StreamingDestination> (owner);
	}
};

const uint8_t TEST_PAYLOAD[] = { 1, 2, 3, 4, 5 };

struct SendResult
{
	boost::system::error_code ecode;
	size_t transferred = 0;
	bool called = false;
};

// Queues one write and runs the posted send work deterministically.
void Write (boost::asio::io_context & service, i2p::stream::Stream & stream, SendResult & result)
{
	stream.AsyncSend (TEST_PAYLOAD, sizeof (TEST_PAYLOAD),
		[&result] (const boost::system::error_code & ecode, size_t len)
		{
			result.ecode = ecode;
			result.transferred = len;
			result.called = true;
		});
	service.poll ();
}
} // namespace

int main ()
{
	using i2p::stream::eStreamStatusClosed;
	using i2p::stream::eStreamStatusClosing;
	using i2p::stream::eStreamStatusReset;
	using i2p::stream::Stream;

	Fixture f;

	// Control: a stream that is still open keeps building packets, so the
	// assertions below are not vacuous.
	{
		auto stream = std::make_shared<Stream> (f.service, f.destination);
		SendResult result;
		Write (f.service, *stream, result);
		assert (result.called);
		assert (!result.ecode);
		assert (stream->GetSendQueueSize () == 1);
	}

	// After CLOSE the queued write fails with operation_aborted and no packet
	// is queued.
	{
		auto stream = std::make_shared<Stream> (f.service, f.destination);
		stream->SetStreamIDsForTesting (3, 4);
		stream->SetStatusForTesting (eStreamStatusClosed);
		SendResult result;
		Write (f.service, *stream, result);
		assert (result.called);
		assert (result.ecode == boost::asio::error::operation_aborted);
		assert (result.transferred == 0);
		assert (stream->GetSendQueueSize () == 0);
		assert (stream->GetSendBufferSize () == 0);
	}

	// After RESET the same rule applies.
	{
		auto stream = std::make_shared<Stream> (f.service, f.destination);
		stream->SetStreamIDsForTesting (5, 6);
		stream->SetStatusForTesting (eStreamStatusReset);
		SendResult result;
		Write (f.service, *stream, result);
		assert (result.called);
		assert (result.ecode == boost::asio::error::operation_aborted);
		assert (result.transferred == 0);
		assert (stream->GetSendQueueSize () == 0);
		assert (stream->GetSendBufferSize () == 0);
	}

	// Half-close must keep working: a Closing stream keeps the bytes queued for
	// the FIN flush (or has already sent them); it never aborts them.
	{
		auto stream = std::make_shared<Stream> (f.service, f.destination);
		stream->SetStreamIDsForTesting (7, 8);
		stream->SetStatusForTesting (eStreamStatusClosing);
		SendResult result;
		Write (f.service, *stream, result);
		assert (result.ecode != boost::asio::error::operation_aborted);
		assert (!result.called || !result.ecode);
		assert (stream->GetSendBufferSize () == 0 || stream->GetSendBufferSize () == sizeof (TEST_PAYLOAD));
	}

	return 0;
}

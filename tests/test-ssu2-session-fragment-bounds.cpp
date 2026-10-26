/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: test ssu2 session fragment bounds.
 *
 * Checks SSU2 SessionConfirmed 4-bit and follow-on 7-bit fragment limits, rejecting out-of-range or duplicate last markers and reassembly that would exceed the I2NP message bound.
 *
 */

#include <cassert>
#include <cstdint>
#include <atomic>
#include <memory>
#include <mutex>
#include <thread>

#include "I2NPProtocol.h"
#include "SSU2Session.h"

namespace
{
std::shared_ptr<i2p::transport::SSU2IncompleteMessage::Fragment> MakeFragment (int number, bool isLast, uint8_t value)
{
	return std::make_shared<i2p::transport::SSU2IncompleteMessage::Fragment> (&value, sizeof (value), number, isLast);
}

std::shared_ptr<i2p::transport::SSU2IncompleteMessage::Fragment> MakeSizedFragment (int number, size_t size)
{
	static const uint8_t data[256] = { 0 };
	return std::make_shared<i2p::transport::SSU2IncompleteMessage::Fragment> (data, size, number, false);
}

size_t CountQueuedFragments (const std::shared_ptr<i2p::transport::SSU2IncompleteMessage> & message)
{
	size_t count = 0;
	for (auto fragment = message->outOfSequenceFragments; fragment; fragment = fragment->next) ++count;
	return count;
}
}

int main ()
{
	// SessionConfirmed uses a 4-bit fragment count; FIRST/FOLLOW-ON fragments
	// use the wider 7-bit field. Do not share these limits.
	static_assert (i2p::transport::SSU2_MAX_SESSION_CONFIRMED_FRAGMENTS == 15);
	static_assert (i2p::transport::SSU2_MAX_SESSION_CONFIRMED_FRAGMENTS < i2p::transport::SSU2_MAX_NUM_FRAGMENTS,
		"SessionConfirmed is the smaller 4-bit fragment space");
	static_assert (i2p::transport::SSU2_INITIAL_RTO >= i2p::transport::SSU2_MIN_RTO);
	static_assert (i2p::transport::SSU2_MAX_RTO >= i2p::transport::SSU2_INITIAL_RTO);
	static_assert (i2p::transport::SSU2_HANDSHAKE_RESEND_INTERVAL == 1250);

	auto incomplete = std::make_shared<i2p::transport::SSU2IncompleteMessage> ();
	incomplete->msg = i2p::NewI2NPShortMessage ();
	incomplete->msg->len = incomplete->msg->offset;
	incomplete->nextFragmentNum = 1; // fragment 0 (FIRSTFRAG) was already consumed

	// Receive the last fragment first, then the two missing ones. The
	// reassembled message must be released only when the last marker is
	// actually reached, in fragment-number order.
	incomplete->AddOutOfSequenceFragment (MakeFragment (3, true, 0xA3));
	incomplete->AddOutOfSequenceFragment (MakeFragment (2, false, 0xA2));
	assert (!incomplete->ConcatOutOfSequenceFragments ());
	incomplete->AddOutOfSequenceFragment (MakeFragment (1, false, 0xA1));
	assert (incomplete->ConcatOutOfSequenceFragments ());
	assert (!incomplete->outOfSequenceFragments);

	const auto * data = incomplete->msg->buf + incomplete->msg->offset;
	assert (incomplete->msg->len == incomplete->msg->offset + 3);
	assert (data[0] == 0xA1);
	assert (data[1] == 0xA2);
	assert (data[2] == 0xA3);

	// A fragment above the known last marker, and a second last marker, are
	// rejected rather than truncating or extending the message.
	auto second = std::make_shared<i2p::transport::SSU2IncompleteMessage> ();
	second->msg = i2p::NewI2NPShortMessage ();
	second->msg->len = second->msg->offset;
	second->nextFragmentNum = 0;
	auto last = MakeFragment (5, true, 0x55);
	second->AddOutOfSequenceFragment (last);
	assert (second->lastFragmentNum == 5);
	second->AddOutOfSequenceFragment (MakeFragment (6, false, 0x66));
	second->AddOutOfSequenceFragment (MakeFragment (4, true, 0x44));
	assert (second->lastFragmentNum == 5);
	assert (second->outOfSequenceFragments == last);
	assert (!last->next);

	// An in-sequence follow-on fragment that would push the reassembled I2NP
	// message past its absolute limit is rejected without appending or
	// advancing the fragment number.
	auto overflow = std::make_shared<i2p::transport::SSU2IncompleteMessage> ();
	overflow->msg = i2p::NewI2NPShortMessage ();
	overflow->msg->len = i2p::I2NP_MAX_MESSAGE_SIZE;
	overflow->nextFragmentNum = 1;
	const uint8_t one = 0x01;
	assert (!overflow->AttachNextFragment (&one, sizeof (one)));
	assert (overflow->nextFragmentNum == 1);
	assert (overflow->msg->len == i2p::I2NP_MAX_MESSAGE_SIZE);

	// Out-of-sequence chains are bounded per incomplete message. The byte
	// budget charges the fixed fragment slab (every Fragment owns a full
	// SSU2_MAX_PACKET_SIZE buffer), not the carried payload length, so tiny
	// fragments cannot pin more memory than the budget allows. The cap must
	// reject the excess fragment without disturbing the accepted chain, and
	// consuming the queue must release the accounting again.
	constexpr size_t slab = sizeof (i2p::transport::SSU2IncompleteMessage::Fragment);
	static_assert (slab > i2p::transport::SSU2_MAX_PACKET_SIZE, "the slab includes bookkeeping");
	auto bounded = std::make_shared<i2p::transport::SSU2IncompleteMessage> ();
	bounded->msg = i2p::NewI2NPShortMessage ();
	bounded->msg->len = bounded->msg->offset;
	bounded->nextFragmentNum = 1;
	size_t acceptedFragments = 0;
	for (size_t i = 1; i <= i2p::transport::SSU2_MAX_OUT_OF_SEQUENCE_FRAGMENTS; ++i)
	{
		const size_t before = bounded->numOutOfSequenceFragments;
		bounded->AddOutOfSequenceFragment (MakeFragment (static_cast<int> (i), false, 0x5A));
		if (bounded->numOutOfSequenceFragments == before) break;
		++acceptedFragments;
	}
	assert (acceptedFragments > 0);
	// The per-message fragment count is the primary cap: a full chain must fit
	// inside the fixed slab budget, and the next fragment is always rejected.
	assert (acceptedFragments == i2p::transport::SSU2_MAX_FRAGMENTS_PER_MESSAGE);
	assert (bounded->numOutOfSequenceFragments == acceptedFragments);
	assert (bounded->outOfSequenceBytes == acceptedFragments * slab);
	assert (bounded->outOfSequenceBytes <= i2p::transport::SSU2_MAX_OUT_OF_SEQUENCE_BYTES);
	assert (CountQueuedFragments (bounded) == acceptedFragments);
	bounded->AddOutOfSequenceFragment (
		MakeFragment (static_cast<int> (acceptedFragments + 1), false, 0x5B));
	assert (bounded->numOutOfSequenceFragments == acceptedFragments);
	assert (CountQueuedFragments (bounded) == acceptedFragments);
	assert (!bounded->ConcatOutOfSequenceFragments ()); // no last marker
	assert (!bounded->outOfSequenceFragments);
	assert (bounded->numOutOfSequenceFragments == 0);
	assert (bounded->outOfSequenceBytes == 0);

	// The same accounting holds for larger payloads: the budget follows the
	// slab count, so a 256-byte fragment can no longer queue more entries than
	// a one-byte fragment could.
	auto bytes = std::make_shared<i2p::transport::SSU2IncompleteMessage> ();
	bytes->nextFragmentNum = 1;
	for (int i = 1; i <= static_cast<int> (i2p::transport::SSU2_MAX_OUT_OF_SEQUENCE_FRAGMENTS); ++i)
		bytes->AddOutOfSequenceFragment (MakeSizedFragment (i, 256));
	assert (bytes->outOfSequenceBytes <= i2p::transport::SSU2_MAX_OUT_OF_SEQUENCE_BYTES);
	assert (bytes->outOfSequenceBytes == bytes->numOutOfSequenceFragments * slab);
	assert (bytes->numOutOfSequenceFragments == acceptedFragments);
	assert (CountQueuedFragments (bytes) == bytes->numOutOfSequenceFragments);

	// SSUExt readers must never observe a vector being resized or cleared.
	// The writer uses the same mutex as RouterInfo::UpdateIntroducers and the
	// RouterContext clears; the reader exercises both snapshot accessors. The
	// manual TSan job is the intended race detector for this case.
	i2p::data::RouterInfo::SSUExt ext;
	std::atomic<bool> stop { false };
	std::thread reader (
		[&]
		{
			while (!stop.load (std::memory_order_relaxed))
			{
				const auto snapshot = ext.GetIntroducers ();
				(void)ext.HasIntroducers ();
				(void)snapshot;
			}
		});
	std::thread writer (
		[&]
		{
			for (int i = 1; i <= 20000; ++i)
			{
				std::lock_guard<std::mutex> lock (ext.mutex);
				if ((i % 7) == 0)
					ext.introducers.clear ();
				else
				{
					i2p::data::RouterInfo::Introducer introducer;
					introducer.iTag = static_cast<uint32_t> (i);
					ext.introducers.push_back (introducer);
				}
			}
		});
	writer.join ();
	stop.store (true, std::memory_order_relaxed);
	reader.join ();

	return 0;
}

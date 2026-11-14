/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * Exercise transcript admission with oversized and null source lengths.
 * Each oversized operand still supplies the bytes that fit in the
 * transcript; the declared length must not wrap the capacity check.
 */

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>

#include "I2NPProtocol.h"
#include "TransportSession.h"

int main ()
{
	constexpr size_t capacity = 16;
	const std::array<uint8_t, capacity> source { 1, 2, 3, 4 };
	i2p::transport::SignedData<capacity> transcript;
	assert (transcript.Insert (nullptr, 0) == 0);
	assert (transcript.Insert (nullptr, capacity) == 0);
	assert (transcript.Insert (nullptr,
		std::numeric_limits<size_t>::max ()) == 0);
	assert (transcript.Insert (source.data (), 0) == 0);
	assert (transcript.Insert (source.data (), 1) == 1);
	assert (transcript.Insert (source.data (),
		std::numeric_limits<size_t>::max ()) == capacity - 1);
	assert (transcript.Insert (source.data (), 1) == 0);
	assert (transcript.Insert (source.data (),
		std::numeric_limits<size_t>::max ()) == 0);

	// A copied full transcript retains its independent capacity state.
	auto copied = transcript;
	assert (copied.Insert (source.data (), 1) == 0);
	transcript.Reset ();
	assert (transcript.Insert (source.data (), capacity) == capacity);
	copied.Reset ();
	assert (copied.Insert (source.data (), capacity + 1) == capacity);
	assert (copied.Insert (source.data (), 1) == 0);

	i2p::transport::SignedData<1> singleByte;
	assert (singleByte.Insert (source.data (),
		std::numeric_limits<size_t>::max ()) == 1);
	assert (singleByte.Insert (source.data (), 1) == 0);

	// Message concatenation shares the same bounded append contract.
	i2p::I2NPMessageBuffer<64> message;
	const std::array<uint8_t, 64> payload { 5, 6, 7, 8 };
	const size_t initialLength = message.len;
	assert (message.Concat (nullptr, 0) == 0);
	assert (message.Concat (nullptr,
		std::numeric_limits<size_t>::max ()) == 0);
	assert (message.len == initialLength);
	assert (message.Concat (payload.data (), 0) == 0);
	assert (message.Concat (payload.data (), 1) == 1);
	const size_t remaining = message.maxLen - message.len;
	assert (message.Concat (payload.data (),
		std::numeric_limits<size_t>::max ()) == remaining);
	assert (message.len == message.maxLen);
	assert (message.buf[initialLength] == payload[0]);
	for (size_t index = 0; index < remaining; ++index)
		assert (message.buf[initialLength + 1 + index] == payload[index]);
	assert (message.Concat (payload.data (), 1) == 0);
	message.len = message.maxLen + 1;
	assert (message.Concat (payload.data (), 1) == 0);
	assert (message.len == message.maxLen + 1);
	i2p::I2NPMessage unbacked;
	assert (unbacked.Concat (payload.data (), 1) == 0);

	// Rejected alignment operands must not alter message geometry.
	i2p::I2NPMessageBuffer<64> aligned;
	const size_t initialOffset = aligned.offset;
	const size_t alignedLength = aligned.len;
	aligned.Align (0);
	assert (aligned.offset == initialOffset && aligned.len == alignedLength);
	aligned.Align (std::numeric_limits<size_t>::max ());
	assert (aligned.offset == initialOffset && aligned.len == alignedLength);
	aligned.Align (aligned.maxLen - aligned.len + 1);
	assert (aligned.offset == initialOffset && aligned.len == alignedLength);
	const size_t remainder = reinterpret_cast<std::uintptr_t> (
		aligned.GetBuffer ()) % 12;
	const size_t padding = remainder ? 12 - remainder : 0;
	aligned.Align (12);
	assert (aligned.offset == initialOffset + padding);
	assert (aligned.len == alignedLength + padding);
	assert (reinterpret_cast<std::uintptr_t> (aligned.GetBuffer ()) % 12 == 0);
	assert (aligned.GetLength () == alignedLength - initialOffset);
	const size_t validOffset = aligned.offset;
	aligned.len = aligned.maxLen + 1;
	aligned.Align (1);
	assert (aligned.offset == validOffset && aligned.len == aligned.maxLen + 1);
	aligned.len = alignedLength;
	aligned.offset = aligned.len + 1;
	aligned.Align (1);
	assert (aligned.offset == alignedLength + 1 && aligned.len == alignedLength);
	unbacked.Align (12);
	assert (unbacked.offset == initialOffset && unbacked.len == alignedLength);

	// Factory tier selection must not wrap header overhead into short.
	constexpr size_t overhead = i2p::I2NP_HEADER_SIZE + 2;
	const auto expectTier = [] (size_t requested, size_t expected)
	{
		const auto allocated = i2p::NewI2NPMessage (requested);
		assert (allocated && allocated->maxLen == expected);
	};
	expectTier (0, i2p::I2NP_MAX_SHORT_MESSAGE_SIZE);
	expectTier (i2p::I2NP_MAX_SHORT_MESSAGE_SIZE - overhead,
		i2p::I2NP_MAX_SHORT_MESSAGE_SIZE);
	expectTier (i2p::I2NP_MAX_SHORT_MESSAGE_SIZE - overhead + 1,
		i2p::I2NP_MAX_MEDIUM_MESSAGE_SIZE);
	expectTier (i2p::I2NP_MAX_MEDIUM_MESSAGE_SIZE - overhead,
		i2p::I2NP_MAX_MEDIUM_MESSAGE_SIZE);
	expectTier (i2p::I2NP_MAX_MEDIUM_MESSAGE_SIZE - overhead + 1,
		i2p::I2NP_MAX_MESSAGE_SIZE);
	expectTier (std::numeric_limits<size_t>::max (), i2p::I2NP_MAX_MESSAGE_SIZE);

	// Empty input must not dereference a null source.
	const auto empty = i2p::CreateI2NPMessage (nullptr, 0, nullptr);
	assert (empty && empty->GetLength () == 0);
	assert (!i2p::CreateI2NPMessage (nullptr, 1, nullptr));

	// Destination tier selection includes the preserved source offset.
	const auto copySource = i2p::NewI2NPMessage ();
	for (const size_t start : std::array<size_t, 6> { 0, 2, 4095, 5000,
		i2p::I2NP_MAX_MEDIUM_MESSAGE_SIZE, i2p::I2NP_MAX_MESSAGE_SIZE - 1 })
	{
		copySource->offset = start;
		copySource->len = start + 1;
		copySource->buf[start] = 0xA5;
		copySource->fromClove = true;
		copySource->fromTunnel = true;
		const auto copiedMessage = i2p::CopyI2NPMessage (copySource);
		assert (copiedMessage && copiedMessage->maxLen >= copySource->len);
		assert (copiedMessage->offset == start && copiedMessage->len == start + 1);
		assert (copiedMessage->GetLength () == 1 && copiedMessage->GetBuffer ()[0] == 0xA5);
		assert (copiedMessage->fromClove && copiedMessage->fromTunnel);
		copySource->buf[start] = 0x5A;
		assert (copiedMessage->GetBuffer ()[0] == 0xA5);
	}
	copySource->offset = 5000;
	copySource->len = 5000;
	const auto emptyCopy = i2p::CopyI2NPMessage (copySource);
	assert (emptyCopy && emptyCopy->offset == 5000 && emptyCopy->GetLength () == 0);
	assert (!i2p::CopyI2NPMessage (nullptr));
	assert (!i2p::CopyI2NPMessage (std::make_shared<i2p::I2NPMessage> ()));
	copySource->offset = 1;
	copySource->len = 0;
	assert (!i2p::CopyI2NPMessage (copySource));
	copySource->offset = 0;
	copySource->len = copySource->maxLen + 1;
	assert (!i2p::CopyI2NPMessage (copySource));
	copySource->offset = std::numeric_limits<size_t>::max ();
	copySource->len = std::numeric_limits<size_t>::max ();
	assert (!i2p::CopyI2NPMessage (copySource));
	return 0;
}

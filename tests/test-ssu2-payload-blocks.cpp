/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: test SSU2 payload blocks.
 *
 * Negative framing matrix for the pure SSU2 decrypted-payload validator:
 * truncated headers, DateTime size, termination size/order, padding order and
 * the per-state block whitelist. No socket or session is created.
 *
 */

#include <cassert>
#include <cstdint>
#include <vector>

#include "SSU2Session.h"

namespace
{
std::vector<uint8_t> Block (uint8_t type, const std::vector<uint8_t> & payload)
{
	std::vector<uint8_t> out;
	out.push_back (type);
	out.push_back (static_cast<uint8_t> ((payload.size () >> 8) & 0xFF));
	out.push_back (static_cast<uint8_t> (payload.size () & 0xFF));
	out.insert (out.end (), payload.begin (), payload.end ());
	return out;
}

void Append (std::vector<uint8_t> & out, const std::vector<uint8_t> & block)
{
	out.insert (out.end (), block.begin (), block.end ());
}

bool Validate (i2p::transport::SSU2SessionState state, const std::vector<uint8_t> & payload)
{
	return i2p::transport::ValidateSSU2PayloadBlocks (state, payload.data (), payload.size ());
}
} // namespace

int main ()
{
	using i2p::transport::eSSU2SessionStateEstablished;
	using i2p::transport::eSSU2SessionStateSessionCreatedReceived;
	const std::vector<uint8_t> four (4, 0);

	// DateTime must be exactly four bytes.
	assert (Validate (eSSU2SessionStateEstablished, Block (i2p::transport::eSSU2BlkDateTime, four)));
	assert (!Validate (eSSU2SessionStateEstablished, Block (i2p::transport::eSSU2BlkDateTime, { 0, 0, 0 })));

	// A termination block is at least nine bytes and must be last except for
	// padding.
	assert (!Validate (eSSU2SessionStateEstablished, Block (i2p::transport::eSSU2BlkTermination, std::vector<uint8_t> (8, 0))));
	assert (Validate (eSSU2SessionStateEstablished, Block (i2p::transport::eSSU2BlkTermination, std::vector<uint8_t> (9, 0))));

	std::vector<uint8_t> afterTermination;
	Append (afterTermination, Block (i2p::transport::eSSU2BlkTermination, std::vector<uint8_t> (9, 0)));
	Append (afterTermination, Block (i2p::transport::eSSU2BlkDateTime, four));
	assert (!Validate (eSSU2SessionStateEstablished, afterTermination));

	std::vector<uint8_t> terminationThenPadding;
	Append (terminationThenPadding, Block (i2p::transport::eSSU2BlkTermination, std::vector<uint8_t> (9, 0)));
	Append (terminationThenPadding, Block (i2p::transport::eSSU2BlkPadding, { 0 }));
	assert (Validate (eSSU2SessionStateEstablished, terminationThenPadding));

	// Padding must be the final block; anything after it is rejected.
	std::vector<uint8_t> paddingThenDateTime;
	Append (paddingThenDateTime, Block (i2p::transport::eSSU2BlkPadding, { 0 }));
	Append (paddingThenDateTime, Block (i2p::transport::eSSU2BlkDateTime, four));
	assert (!Validate (eSSU2SessionStateEstablished, paddingThenDateTime));

	// Truncated block header and declared size overrun are rejected.
	assert (!Validate (eSSU2SessionStateEstablished, { i2p::transport::eSSU2BlkPadding }));
	assert (!Validate (eSSU2SessionStateEstablished, { i2p::transport::eSSU2BlkPadding, 0 }));
	std::vector<uint8_t> overrun = { i2p::transport::eSSU2BlkPadding, 0, 4, 0 };
	assert (!Validate (eSSU2SessionStateEstablished, overrun));

	// A block that is not permitted in the SessionCreated state is rejected
	// even when its frame is otherwise well formed (Termination is only valid
	// in the request/retry states).
	assert (!Validate (eSSU2SessionStateSessionCreatedReceived,
		Block (i2p::transport::eSSU2BlkTermination, std::vector<uint8_t> (9, 0))));

	// Empty payload is a valid block list (the caller decides whether a state
	// requires at least one block).
	assert (Validate (eSSU2SessionStateEstablished, {}));

	return 0;
}

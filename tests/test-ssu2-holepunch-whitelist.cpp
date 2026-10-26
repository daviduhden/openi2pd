/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: test ssu2 holepunch whitelist.
 *
 * Enforces the HolePunch/Introduced block whitelist: only DateTime, Address,
 * RelayResponse, Padding and Termination are accepted, and a data-phase block
 * smuggled into an introduction payload invalidates the whole payload.
 *
 */

#include <cassert>
#include <cstdint>
#include <initializer_list>
#include <vector>

#include "SSU2Session.h"

namespace
{
using namespace i2p::transport;

bool IsHolePunchBlockExpected (uint8_t block)
{
	switch (block)
	{
	case eSSU2BlkDateTime:
	case eSSU2BlkAddress:
	case eSSU2BlkRelayResponse:
	case eSSU2BlkPadding:
	case eSSU2BlkTermination: return true;
	default: return false;
	}
}

std::vector<uint8_t> MakeBlock (uint8_t type, size_t size, uint8_t fill = 0)
{
	std::vector<uint8_t> block;
	block.push_back (type);
	block.push_back (static_cast<uint8_t> (size >> 8));
	block.push_back (static_cast<uint8_t> (size));
	block.insert (block.end (), size, fill);
	return block;
}

std::vector<uint8_t> Concat (std::initializer_list<std::vector<uint8_t>> parts)
{
	std::vector<uint8_t> out;
	for (const auto & part : parts) out.insert (out.end (), part.begin (), part.end ());
	return out;
}

void TestBlockMatrix ()
{
	constexpr SSU2SessionState holePunchStates[] = { eSSU2SessionStateHolePunch, eSSU2SessionStateIntroduced };

	// The state table must delegate to the HolePunch predicate for both
	// introduction states and nothing else may be accepted.
	for (auto state : holePunchStates)
	{
		for (int block = 0; block < 256; block++)
			assert (IsSSU2PayloadBlockAllowedForState (state, static_cast<uint8_t> (block))
				== IsHolePunchBlockExpected (static_cast<uint8_t> (block)));
	}

	// Explicitly reject everything that reaches a data-phase handler.
	const uint8_t forbiddenBlocks[] = {
		eSSU2BlkOptions,
		eSSU2BlkRouterInfo,
		eSSU2BlkI2NPMessage,
		eSSU2BlkFirstFragment,
		eSSU2BlkFollowOnFragment,
		eSSU2BlkRelayRequest,
		eSSU2BlkRelayIntro,
		eSSU2BlkPeerTest,
		eSSU2BlkNextNonce,
		eSSU2BlkAck,
		eSSU2BlkIntroKey,
		eSSU2BlkRelayTagRequest,
		eSSU2BlkRelayTag,
		eSSU2BlkNewToken,
		eSSU2BlkPathChallenge,
		eSSU2BlkPathResponse,
		eSSU2BlkFirstPacketNumber
	};
	for (auto block : forbiddenBlocks)
	{
		assert (!IsHolePunchPayloadBlockAllowed (block));
		for (auto state : holePunchStates)
			assert (!IsSSU2PayloadBlockAllowedForState (state, block));
	}

	// The PeerTest whitelist is independent and unchanged: it still accepts
	// the PeerTest block and Termination stays invalid there.
	assert (IsPeerTestPayloadBlockAllowed (eSSU2BlkDateTime));
	assert (IsPeerTestPayloadBlockAllowed (eSSU2BlkPeerTest));
	assert (IsPeerTestPayloadBlockAllowed (eSSU2BlkAddress));
	assert (IsPeerTestPayloadBlockAllowed (eSSU2BlkPadding));
	assert (!IsPeerTestPayloadBlockAllowed (eSSU2BlkTermination));
	assert (!IsPeerTestPayloadBlockAllowed (eSSU2BlkRelayResponse));
	assert (!IsHolePunchPayloadBlockAllowed (eSSU2BlkPeerTest));

	// The established data phase keeps the complete block set; the new
	// whitelist must not narrow it.
	assert (IsSSU2PayloadBlockAllowedForState (eSSU2SessionStateEstablished, eSSU2BlkI2NPMessage));
	assert (IsSSU2PayloadBlockAllowedForState (eSSU2SessionStateEstablished, eSSU2BlkPeerTest));
}

void TestPayloadValidation ()
{
	constexpr SSU2SessionState holePunchStates[] = { eSSU2SessionStateHolePunch, eSSU2SessionStateIntroduced };

	const auto dateTime = MakeBlock (eSSU2BlkDateTime, 4, 1);
	const auto address = MakeBlock (eSSU2BlkAddress, 6);
	const auto relayResponse = MakeBlock (eSSU2BlkRelayResponse, 12);
	const auto padding = MakeBlock (eSSU2BlkPadding, 0);
	const auto termination = MakeBlock (eSSU2BlkTermination, 9);

	const auto introduction = Concat ({ dateTime, address, relayResponse, padding });
	for (auto state : holePunchStates)
		assert (ValidateSSU2PayloadBlocks (state, introduction.data (), introduction.size ()));

	// An introduction may end with Termination followed by Padding.
	const auto withTermination = Concat ({ dateTime, relayResponse, termination, padding });
	for (auto state : holePunchStates)
		assert (ValidateSSU2PayloadBlocks (state, withTermination.data (), withTermination.size ()));

	const auto i2np = MakeBlock (eSSU2BlkI2NPMessage, 9);
	const auto firstFragment = MakeBlock (eSSU2BlkFirstFragment, 10);
	const auto relayRequest = MakeBlock (eSSU2BlkRelayRequest, 20);
	const auto peerTest = MakeBlock (eSSU2BlkPeerTest, 13);
	const auto pathChallenge = MakeBlock (eSSU2BlkPathChallenge, 8);
	const auto ack = MakeBlock (eSSU2BlkAck, 5);
	const auto newToken = MakeBlock (eSSU2BlkNewToken, 12);
	const auto routerInfo = MakeBlock (eSSU2BlkRouterInfo, 2);

	for (const auto * forbidden : { &i2np, &firstFragment, &relayRequest, &peerTest, &pathChallenge, &ack, &newToken,
			 &routerInfo })
	{
		const auto payload = Concat ({ dateTime, *forbidden, padding });
		for (auto state : holePunchStates)
			assert (!ValidateSSU2PayloadBlocks (state, payload.data (), payload.size ()));
	}

	// The same blocks stay valid in the data phase, so the rejection above is
	// specific to the introduction context.
	for (const auto * allowed : { &i2np, &peerTest, &ack, &newToken, &routerInfo })
	{
		const auto payload = Concat ({ dateTime, *allowed, padding });
		assert (ValidateSSU2PayloadBlocks (eSSU2SessionStateEstablished, payload.data (), payload.size ()));
	}
}
} // namespace

int main ()
{
	TestBlockMatrix ();
	TestPayloadValidation ();
	return 0;
}

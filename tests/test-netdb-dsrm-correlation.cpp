/*
* Copyright (c) 2026, David Uhden Collado
* SPDX-License-Identifier: BSD-3-Clause
*
* This file is part of openi2pd and licensed under BSD-3-Clause
*
* See full license text in LICENSE file at top of project tree
*/

#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>

#include "I2NPProtocol.h"
#include "I2PEndian.h"
#include "Identity.h"
#include "NetDbRequests.h"
#include "RouterContext.h"

// Static checks for RequestedDestination lookup/response correlation:
// exploratory lookup type on the direct path, exclusion growth, tracking of
// the peers a lookup was actually sent to, and duplicate-reply suppression.
//
// References:
//   I2NP DatabaseLookup flags bits 3-2 (11 = exploration) and bit 0 (tunnel).
//   I2NP DatabaseSearchReply note: the "from" hash is unauthenticated.
//   Java 2.13.0 ExploreJob.buildMessage() sets Type.EXPL for a real explore
//   even when replyTunnelId is null (direct path).
//   Java 2.13.0 SearchReplyJob/SingleLookupJob treat duplicates and only
//   follow a bounded, deduplicated set of hashes.

namespace
{
using namespace i2p;
using namespace i2p::data;

IdentHash MakeHash (uint32_t value)
{
	IdentHash hash;
	hash.Fill (0);
	htobe32buf (hash, value);
	return hash;
}

void InitializeLocalIdentity ()
{
	auto keys = PrivateKeys::CreateRandomKeys (
		SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519, CRYPTO_KEY_TYPE_ECIES_X25519_AEAD, false);
	context.GetRouterInfo ().SetRouterIdentity (keys.GetPublic ());
}

uint8_t LookupFlags (const std::shared_ptr<I2NPMessage> & msg)
{
	assert (msg);
	assert (msg->GetTypeID () == eI2NPDatabaseLookup);
	return msg->GetPayload ()[64];
}

void TestDirectExploratoryLookupType ()
{
	// Regression: the direct overload used to hardcode exploratory=false, so
	// an exploration sent directly at a connected floodfill was encoded as an
	// RI lookup (0x08) instead of an exploration (0x0c).
	RequestedDestination exploratory (MakeHash (0x1000), true, true);
	const IdentHash firstFloodfill = MakeHash (0x1001);
	auto first = exploratory.CreateRequestMessage (firstFloodfill);
	assert (LookupFlags (first) == DATABASE_LOOKUP_TYPE_EXPLORATORY_LOOKUP);
	assert (bufbe16toh (first->GetPayload () + 65) == 0); // nothing excluded yet
	assert (exploratory.IsExploratory ());
	assert (exploratory.IsRequestedPeer (firstFloodfill));

	// The next direct attempt excludes the peer already queried.
	auto second = exploratory.CreateRequestMessage (MakeHash (0x1002));
	assert (LookupFlags (second) == DATABASE_LOOKUP_TYPE_EXPLORATORY_LOOKUP);
	assert (bufbe16toh (second->GetPayload () + 65) == 1);
	assert (exploratory.IsExcluded (firstFloodfill));
	assert (exploratory.GetNumAttempts () == 2);

	// A regular (non-exploratory) direct lookup keeps the RI type.
	RequestedDestination regular (MakeHash (0x1100), false, true);
	auto ri = regular.CreateRequestMessage (MakeHash (0x1101));
	assert (LookupFlags (ri) == DATABASE_LOOKUP_TYPE_ROUTERINFO_LOOKUP);
}

void TestResponseSenderTracking ()
{
	RequestedDestination request (MakeHash (0x2000), false, true);
	assert (!request.HasRequestedPeers ());
	assert (!request.IsRequestedPeer (MakeHash (0x2001)));
	assert (!request.IsRequestedPeer (MakeHash (0x2002)));

	auto first = request.CreateRequestMessage (MakeHash (0x2001));
	assert (first);
	assert (request.HasRequestedPeers ());
	assert (request.IsRequestedPeer (MakeHash (0x2001)));
	assert (!request.IsRequestedPeer (MakeHash (0x2002)));

	// The first accepted reply from the queried peer is recorded exactly once.
	assert (request.MarkReplyFromPeer (MakeHash (0x2001)));
	assert (!request.MarkReplyFromPeer (MakeHash (0x2001)));

	// A later attempt retains both peers, so a legitimate late reply from the
	// first peer remains correlatable and is still distinguishable from a
	// duplicate of its own earlier reply.
	auto second = request.CreateRequestMessage (MakeHash (0x2002));
	assert (second);
	assert (request.IsRequestedPeer (MakeHash (0x2001)));
	assert (request.IsRequestedPeer (MakeHash (0x2002)));
	assert (request.MarkReplyFromPeer (MakeHash (0x2002)));
	assert (!request.MarkReplyFromPeer (MakeHash (0x2002)));
	assert (request.GetNumAttempts () == 2);
}

void TestSenderOutsideRequestedSet ()
{
	// A spoofed/unsolicited DatabaseSearchReply claims a "from" hash. The
	// handler only consults IsRequestedPeer() for senders, so an unrelated
	// hash must not be correlated with a lookup that was sent to someone else.
	RequestedDestination request (MakeHash (0x3000), false, true);
	auto msg = request.CreateRequestMessage (MakeHash (0x3001));
	assert (msg);
	assert (request.IsRequestedPeer (MakeHash (0x3001)));
	assert (!request.IsRequestedPeer (MakeHash (0x3002)));
	assert (!request.IsRequestedPeer (MakeHash (0x3003)));
}

void TestExcludedAndRequestedPeersAreSeparate ()
{
	// A peer excluded by a previous reply is not automatically a requested
	// peer. Exclusions grow unboundedly only up to the caller's set; requested
	// peers are the transport destinations a reply may legitimately claim.
	RequestedDestination request (MakeHash (0x4000), false, true);
	auto first = request.CreateRequestMessage (MakeHash (0x4001));
	assert (first);
	auto second = request.CreateRequestMessage (MakeHash (0x4002));
	assert (second);
	assert (request.IsExcluded (MakeHash (0x4001)));
	// The new target is inserted after the message is serialized, so the
	// second message excludes only the first peer but the set now has both.
	assert (bufbe16toh (second->GetPayload () + 65) == 1);
	assert (request.IsExcluded (MakeHash (0x4002)));
	assert (request.IsRequestedPeer (MakeHash (0x4002)));
	assert (request.GetNumAttempts () == 2);

	request.ClearExcludedPeers ();
	assert (!request.IsExcluded (MakeHash (0x4001)));
	assert (request.IsRequestedPeer (MakeHash (0x4001)));
}
}

int main ()
{
	InitializeLocalIdentity ();
	TestDirectExploratoryLookupType ();
	TestResponseSenderTracking ();
	TestSenderOutsideRequestedSet ();
	TestExcludedAndRequestedPeersAreSeparate ();
	return 0;
}

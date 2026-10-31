/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: test kad dht.
 *
 * Checks deterministic Kademlia insertion, exact-hash lookup, duplicate
 * replacement, leaf removal with sibling promotion, filtered cleanup
 * and full node release on destruction.
 *
 */

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <functional>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "Identity.h"
#include "KadDHT.h"
#include "RouterInfo.h"

// Deterministic Kademlia ownership and tree-shape checks. The
// identities are built from fixed key bytes rather than generated keys,
// and no network, filesystem or netDb component is used.

namespace
{
using namespace i2p::data;

class TestRouterInfo : public RouterInfo
{
public:
	TestRouterInfo () = default;
};

struct TestNode
{
	std::shared_ptr<RouterInfo> router;
	IdentHash hash;
};

TestNode MakeNode (uint32_t seed)
{
	Identity identity { };
	uint32_t state = seed * 2654435761u + 1u;
	auto next = [&state] ()
	{
		state = state * 1664525u + 1013904223u;
		return static_cast<uint8_t> (state >> 24);
	};
	for (auto & byte : identity.publicKey) byte = next ();
	for (auto & byte : identity.signingKey) byte = next ();
	identity.certificate[0] = 0;
	identity.certificate[1] = 0;
	identity.certificate[2] = 0;

	auto identityEx = std::make_shared<IdentityEx> (identity);
	auto router = std::make_shared<TestRouterInfo> ();
	router->SetRouterIdentity (identityEx);
	auto hash = router->GetIdentHash ();
	return { std::move (router), hash };
}

std::vector<TestNode> MakeNodes (size_t count)
{
	std::vector<TestNode> nodes;
	nodes.reserve (count);
	for (size_t i = 0; i < count; ++i) nodes.push_back (MakeNode (static_cast<uint32_t> (i)));
	return nodes;
}

const TestNode * FindFirstBit (const std::vector<TestNode> & nodes, bool one)
{
	for (const auto & node : nodes)
		if ((node.hash.GetBit (0) != 0) == one) return &node;
	return nullptr;
}

void TestInsertFindAndDuplicates ()
{
	DHTTable table;
	auto probe = MakeNode (0);
	assert (table.GetSize () == 0);
	assert (!table.FindClosest (probe.hash));
	assert (table.FindClosest (probe.hash, 0, nullptr).empty ());

	table.Insert (nullptr);
	assert (table.GetSize () == 0);

	auto nodes = MakeNodes (4);
	for (const auto & node : nodes)
	{
		table.Insert (node.router);
		assert (table.FindClosest (node.hash) == node.router);
	}
	assert (table.GetSize () == nodes.size ());

	// Every inserted identity is reachable at its exact hash.
	for (const auto & node : nodes) assert (table.FindClosest (node.hash) == node.router);

	// A rejected filter must hide an exact match, an accepting one must
	// return it. This is the existing filter contract, not a new one.
	assert (table.FindClosest (nodes[0].hash, [] (const std::shared_ptr<RouterInfo> &) { return false; }) == nullptr);
	assert (table.FindClosest (
				nodes[0].hash, [&nodes] (const std::shared_ptr<RouterInfo> & r) { return r == nodes[0].router; })
		== nodes[0].router);

	auto closest = table.FindClosest (nodes[0].hash, 10, nullptr);
	assert (closest.size () == nodes.size ());
	for (const auto & node : nodes)
		assert (std::find (closest.begin (), closest.end (), node.router) != closest.end ());
	assert (closest.front () == nodes[0].router);

	// A duplicate router replaces the stored object without growing the
	// table. Two RouterInfo instances can share one identity hash.
	auto sameIdentity = MakeNode (1);
	assert (sameIdentity.hash == nodes[1].hash);
	assert (sameIdentity.router != nodes[1].router);
	table.Insert (sameIdentity.router);
	assert (table.GetSize () == nodes.size ());
	assert (table.FindClosest (nodes[1].hash) == sameIdentity.router);
	table.Insert (sameIdentity.router);
	assert (table.GetSize () == nodes.size ());
}

void TestRemoveLeafAndPromotion ()
{
	auto candidates = MakeNodes (16);
	auto zero = FindFirstBit (candidates, false);
	auto one = FindFirstBit (candidates, true);
	assert (zero && one);
	assert (zero->hash.GetBit (0) == 0);
	assert (one->hash.GetBit (0) != 0);

	DHTTable table;
	table.Insert (zero->router);
	table.Insert (one->router);
	assert (table.GetSize () == 2);

	assert (table.Remove (zero->hash));
	assert (table.GetSize () == 1);
	assert (table.FindClosest (zero->hash) == one->router);
	assert (table.FindClosest (one->hash) == one->router);

	// Removing one side of the root split promotes the surviving router to
	// the root, so the printed tree is a single leaf again.
	std::stringstream promoted;
	table.Print (promoted);
	assert (promoted.str () == ">" + GetIdentHashAbbreviation (one->hash) + "\n");

	assert (!table.Remove (zero->hash));
	assert (table.GetSize () == 1);
	assert (table.Remove (one->hash));
	assert (table.GetSize () == 0);
	assert (!table.FindClosest (one->hash));
	assert (!table.Remove (one->hash));
}

void TestClear ()
{
	DHTTable table;
	auto nodes = MakeNodes (6);
	for (const auto & node : nodes) table.Insert (node.router);
	assert (table.GetSize () == nodes.size ());

	table.Clear ();
	assert (table.GetSize () == 0);
	assert (!table.FindClosest (nodes[0].hash));

	// The root remains usable after Clear and repeated Clear is safe.
	table.Insert (nodes[2].router);
	assert (table.GetSize () == 1);
	assert (table.FindClosest (nodes[2].hash) == nodes[2].router);
	table.Clear ();
	table.Clear ();
	assert (table.GetSize () == 0);
	assert (!table.FindClosest (nodes[2].hash));
}

void TestCleanupWithFilter ()
{
	auto nodes = MakeNodes (5);
	DHTTable table;
	for (const auto & node : nodes) table.Insert (node.router);

	// Keep exactly one router and prune every other leaf and branch.
	const auto keep = nodes[2];
	table.Cleanup ([&keep] (const std::shared_ptr<RouterInfo> & r) { return r == keep.router; });
	assert (table.GetSize () == 1);
	assert (table.FindClosest (keep.hash) == keep.router);
	for (const auto & node : nodes)
		if (node.hash != keep.hash) assert (table.FindClosest (node.hash) == keep.router);

	// A filter accepting everything leaves the table unchanged.
	table.Clear ();
	for (const auto & node : nodes) table.Insert (node.router);
	table.Cleanup ([] (const std::shared_ptr<RouterInfo> &) { return true; });
	assert (table.GetSize () == nodes.size ());
	for (const auto & node : nodes) assert (table.FindClosest (node.hash) == node.router);

	// A filter rejecting everything is equivalent to Clear, but the filter
	// path must still prune all nodes and promote nothing.
	table.Cleanup ([] (const std::shared_ptr<RouterInfo> &) { return false; });
	assert (table.GetSize () == 0);
	assert (!table.FindClosest (nodes[0].hash));

	// An empty filter takes the unconditional Clear path.
	std::function<bool (const std::shared_ptr<RouterInfo> &)> emptyFilter;
	for (const auto & node : nodes) table.Insert (node.router);
	table.Cleanup (emptyFilter);
	assert (table.GetSize () == 0);
	assert (!table.FindClosest (nodes[0].hash));

	// Filtering one side of a split must promote the kept sibling.
	auto candidates = MakeNodes (16);
	auto zero = FindFirstBit (candidates, false);
	auto one = FindFirstBit (candidates, true);
	assert (zero && one);

	DHTTable filtered;
	filtered.Insert (zero->router);
	filtered.Insert (one->router);
	filtered.Cleanup ([one] (const std::shared_ptr<RouterInfo> & r) { return r == one->router; });
	assert (filtered.GetSize () == 1);
	std::stringstream promoted;
	filtered.Print (promoted);
	assert (promoted.str () == ">" + GetIdentHashAbbreviation (one->hash) + "\n");
}

void TestDestructionAndRelease ()
{
	auto nodes = MakeNodes (16);

	// Destroying a populated tree must release every node through the
	// unique_ptr chain.
	{
		DHTTable table;
		for (const auto & node : nodes) table.Insert (node.router);
		assert (table.GetSize () == nodes.size ());
	}

	// Clear, rebuild and remove every node. This exercises leaf deletion,
	// branch pruning and promotion before the final destructor runs.
	{
		DHTTable table;
		for (const auto & node : nodes) table.Insert (node.router);
		table.Clear ();
		for (const auto & node : nodes) table.Insert (node.router);
		for (const auto & node : nodes) assert (table.Remove (node.hash));
		assert (table.GetSize () == 0);
	}
}
}

int main ()
{
	TestInsertFindAndDuplicates ();
	TestRemoveLeafAndPromotion ();
	TestClear ();
	TestCleanupWithFilter ();
	TestDestructionAndRelease ();
	return 0;
}

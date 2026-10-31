/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: blocklist enforcement regression.
 *
 * Exercises immutable snapshot matching for IPv4/IPv6/router hashes,
 * the bounded runtime-ban set and its IsRouterBanned() integration. The
 * global one-shot file load is intentionally not invoked: the static
 * contract in NetDb.cpp and Profiling.cpp covers the startup wiring,
 * while these checks stay free of configuration, sockets and filesystem
 * fixtures.
 *
 */

#include <cassert>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <boost/asio/ip/address.hpp>

#include "Blocklist.h"
#include "Profiling.h"

using namespace i2p::data;

namespace
{
std::shared_ptr<const OperatorBlocklistSnapshot> BuildSnapshot (IdentHash & routerHash)
{
	routerHash.Fill (0x77);
	OperatorBlocklistBuilder builder;
	assert (builder.AddLine ("10.0.0.0/8"));
	assert (builder.AddLine ("192.168.1.5"));
	assert (builder.AddLine ("203.0.113.0-203.0.113.15"));
	assert (builder.AddLine ("2001;db8;;1"));
	assert (builder.AddLine (routerHash.ToBase64 ()));
	assert (builder.AddLine ("example.com")); // documented skip
	assert (builder.GetSkippedHostnames () == 1);
	return builder.Build ();
}

IdentHash MakeHash (uint32_t value)
{
	IdentHash hash;
	hash.Fill (0);
	auto * bytes = hash ();
	bytes[0] = static_cast<uint8_t> (value & 0xFF);
	bytes[1] = static_cast<uint8_t> ((value >> 8) & 0xFF);
	bytes[2] = static_cast<uint8_t> ((value >> 16) & 0xFF);
	bytes[3] = static_cast<uint8_t> ((value >> 24) & 0xFF);
	return hash;
}

void TestSnapshotAddresses ()
{
	IdentHash routerHash;
	auto snapshot = BuildSnapshot (routerHash);
	assert (snapshot->GetNumIPv4Ranges () == 3);
	assert (snapshot->GetNumIPv6Addresses () == 1);
	assert (snapshot->GetNumRouterHashes () == 1);

	auto address = [] (const char * value) { return boost::asio::ip::make_address (value); };

	assert (snapshot->IsAddressBlocked (address ("10.0.0.1")));
	assert (snapshot->IsAddressBlocked (address ("10.255.255.255")));
	assert (!snapshot->IsAddressBlocked (address ("9.255.255.255")));
	assert (!snapshot->IsAddressBlocked (address ("11.0.0.0")));

	assert (snapshot->IsAddressBlocked (address ("192.168.1.5")));
	assert (!snapshot->IsAddressBlocked (address ("192.168.1.6")));

	assert (snapshot->IsAddressBlocked (address ("203.0.113.7")));
	assert (!snapshot->IsAddressBlocked (address ("203.0.113.16")));

	assert (snapshot->IsAddressBlocked (address ("2001:db8::1")));
	assert (!snapshot->IsAddressBlocked (address ("2001:db8::2")));
	// An IPv4-mapped IPv6 peer must not bypass an IPv4 range.
	assert (snapshot->IsAddressBlocked (address ("::ffff:10.1.2.3")));

	assert (snapshot->ContainsRouter (routerHash));
	IdentHash other;
	other.Fill (0x11);
	assert (!snapshot->ContainsRouter (other));

	// Empty snapshots block nothing.
	OperatorBlocklistBuilder emptyBuilder;
	auto empty = emptyBuilder.Build ();
	assert (!empty->IsAddressBlocked (address ("10.0.0.1")));
	assert (empty->GetNumIPv4Ranges () == 0);
	assert (empty->GetNumRouterHashes () == 0);
}

void TestRuntimeBans ()
{
	// The test process owns this global state; leave it empty afterwards.
	assert (GetRuntimeRouterBanCount () == 0);
	const auto first = MakeHash (1);
	const auto second = MakeHash (2);
	const auto third = MakeHash (3);

	assert (AddRuntimeRouterBan (first));
	assert (AddRuntimeRouterBan (first)); // idempotent
	assert (AddRuntimeRouterBan (second));
	assert (AddRuntimeRouterBan (third));
	assert (GetRuntimeRouterBanCount () == 3);
	assert (IsRuntimeRouterBanned (first));
	assert (i2p::data::IsRouterBanned (first)); // Profiling integration

	std::vector<IdentHash> out;
	assert (GetRuntimeRouterBans (out, 2) == 2);
	assert (out.size () == 2);
	assert (GetRuntimeRouterBans (out, 10) == 3);
	assert (out.size () == 3);

	assert (RemoveRuntimeRouterBan (second));
	assert (!RemoveRuntimeRouterBan (second));
	assert (GetRuntimeRouterBanCount () == 2);
	assert (!IsRuntimeRouterBanned (second));
	assert (!i2p::data::IsRouterBanned (second)); // empty profile map, no ban

	assert (RemoveRuntimeRouterBan (first));
	assert (RemoveRuntimeRouterBan (third));
	assert (GetRuntimeRouterBanCount () == 0);
	assert (!i2p::data::IsRouterBanned (first));
}

void TestRuntimeBanCap ()
{
	assert (GetRuntimeRouterBanCount () == 0);
	for (size_t i = 0; i < OPERATOR_BLOCKLIST_MAX_RUNTIME_BANS; ++i)
		assert (AddRuntimeRouterBan (MakeHash (static_cast<uint32_t> (i))));
	assert (GetRuntimeRouterBanCount () == OPERATOR_BLOCKLIST_MAX_RUNTIME_BANS);

	// The set refuses rather than evicting an existing administrative ban.
	const auto overflow = MakeHash (0x00FFFFFFu);
	assert (!AddRuntimeRouterBan (overflow));
	assert (!IsRuntimeRouterBanned (overflow));
	assert (GetRuntimeRouterBanCount () == OPERATOR_BLOCKLIST_MAX_RUNTIME_BANS);

	for (size_t i = 0; i < OPERATOR_BLOCKLIST_MAX_RUNTIME_BANS; ++i)
		assert (RemoveRuntimeRouterBan (MakeHash (static_cast<uint32_t> (i))));
	assert (GetRuntimeRouterBanCount () == 0);
}

void TestUnloadedStats ()
{
	// LoadOperatorBlocklist() is not called here, so no file was read and
	// the published table is absent.
	auto stats = GetOperatorBlocklistStats ();
	assert (!stats.loaded);
	assert (stats.ipv4Ranges == 0);
	assert (stats.ipv6Addresses == 0);
	assert (stats.routerHashes == 0);
}
} // namespace

int main ()
{
	// Snapshot membership is pure; runtime bans and the Profiling
	// predicate
	// need no router context, configuration or profile files.
	TestSnapshotAddresses ();
	TestRuntimeBans ();
	TestRuntimeBanCap ();
	TestUnloadedStats ();
	return 0;
}

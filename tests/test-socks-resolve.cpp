/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: test SOCKS resolve cache and success replies.
 *
 * Exercises the bounded, expiring torsocks RESOLVE mapping (fake IPv4
 * derivation, determinism, key isolation, expiry and eviction) and the SOCKS5
 * success replies, which must never carry a wildcard address. No socket,
 * listener or peer is used.
 *
 */

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "SOCKS.h"

namespace
{
using i2p::proxy::SOCKS5ConnectSuccessReply;
using i2p::proxy::SOCKS5UdpAssociateReply;
using i2p::proxy::SOCKS5_CONNECT_REPLY_PORT;
using i2p::proxy::SOCKS5_REPLY_LOOPBACK_ADDRESS;
using i2p::proxy::SOCKS_RESOLVE_CACHE_MAX_ENTRIES;
using i2p::proxy::SOCKS_RESOLVE_CACHE_TTL_MS;
using i2p::proxy::SOCKS_RESOLVE_FAKE_PREFIX;
using i2p::proxy::SocksFakeIPv4Address;
using i2p::proxy::SocksResolveCache;

constexpr std::uint64_t NOW = 1'700'000'000'000;

std::array<std::uint8_t, 16> MakeKey (std::uint8_t seed)
{
	std::array<std::uint8_t, 16> key { };
	for (std::size_t i = 0; i < key.size (); ++i) key[i] = static_cast<std::uint8_t> (seed + i);
	return key;
}

void TestFakeAddressSpace ()
{
	const auto key = MakeKey (0x11);

	// The fake address always lives in the reserved 255/8 block, so it can
	// never collide with a routable clearnet address.
	for (const char * name : { "example.i2p", "a.b32.i2p", "x", "", "very-long-destination.example.i2p", "255.1.2.3" })
	{
		const auto address = SocksFakeIPv4Address (name, key);
		assert ((address.to_uint () & 0xff000000u) == SOCKS_RESOLVE_FAKE_PREFIX);
		assert (!address.is_unspecified ());
	}

	// Determinism: the same name and key always give the same address.
	const auto first = SocksFakeIPv4Address ("example.i2p", key);
	assert (SocksFakeIPv4Address ("example.i2p", key) == first);

	// Key isolation: a different process key remaps the name (checked over
	// several names so a single hash collision cannot fail the assertion).
	const auto otherKey = MakeKey (0x91);
	bool differs = false;
	for (const char * name : { "one.i2p", "two.i2p", "three.i2p", "four.i2p", "five.i2p", "six.i2p" })
		if (SocksFakeIPv4Address (name, key) != SocksFakeIPv4Address (name, otherKey)) differs = true;
	assert (differs);

	// Different destinations map to different addresses in practice.
	bool namesDiffer = false;
	if (SocksFakeIPv4Address ("one.i2p", key) != SocksFakeIPv4Address ("two.i2p", key)) namesDiffer = true;
	assert (namesDiffer);
}

void TestInsertAndLookup ()
{
	SocksResolveCache cache (MakeKey (0x23));
	assert (cache.Size () == 0);
	assert (cache.Lookup (boost::asio::ip::address_v4 (0x7f000001), NOW).empty ());

	const auto fake = cache.Insert ("example.i2p", NOW);
	assert (cache.Size () == 1);
	assert (cache.Lookup (fake, NOW) == "example.i2p");

	// Re-inserting the same name refreshes the existing entry.
	assert (cache.Insert ("example.i2p", NOW + 1000) == fake);
	assert (cache.Size () == 1);

	// An unrelated address is never resolved.
	assert (cache.Lookup (boost::asio::ip::address_v4 (0x0a000001), NOW).empty ());

	cache.Clear ();
	assert (cache.Size () == 0);
	assert (cache.Lookup (fake, NOW).empty ());
}

void TestExpiry ()
{
	SocksResolveCache cache (MakeKey (0x37));
	const auto fake = cache.Insert ("expiring.i2p", NOW);

	// Live just before the deadline, expired at and after it.
	assert (cache.Lookup (fake, NOW + SOCKS_RESOLVE_CACHE_TTL_MS - 1) == "expiring.i2p");
	assert (cache.Lookup (fake, NOW + SOCKS_RESOLVE_CACHE_TTL_MS).empty ());
	// The expired entry was removed, not retained.
	assert (cache.Size () == 0);

	// A re-resolve after expiry restores the same deterministic address.
	cache.Insert ("expiring.i2p", NOW + SOCKS_RESOLVE_CACHE_TTL_MS + 5);
	assert (cache.Lookup (fake, NOW + SOCKS_RESOLVE_CACHE_TTL_MS + 6) == "expiring.i2p");
}

void TestBoundAndEviction ()
{
	SocksResolveCache cache (MakeKey (0x41));
	assert (SOCKS_RESOLVE_CACHE_MAX_ENTRIES == 256);

	for (std::size_t i = 0; i < SOCKS_RESOLVE_CACHE_MAX_ENTRIES + 100; ++i)
	{
		const auto address = cache.Insert ("host" + std::to_string (i) + ".i2p", NOW + i);
		assert ((address.to_uint () & 0xff000000u) == SOCKS_RESOLVE_FAKE_PREFIX);
		// The cache never exceeds its documented bound.
		assert (cache.Size () <= SOCKS_RESOLVE_CACHE_MAX_ENTRIES);
	}
	assert (cache.Size () == SOCKS_RESOLVE_CACHE_MAX_ENTRIES);

	// The most recent insertion is still resolvable.
	const auto latest = SocksFakeIPv4Address (
		"host" + std::to_string (SOCKS_RESOLVE_CACHE_MAX_ENTRIES + 99) + ".i2p", MakeKey (0x41));
	assert (cache.Lookup (latest, NOW + 1000) == "host355.i2p");

	// Expired entries are dropped before eviction, so re-inserting after the
	// lifetime starts from a small cache instead of evicting live entries.
	cache.Insert ("fresh.i2p", NOW + SOCKS_RESOLVE_CACHE_TTL_MS * 4);
	assert (cache.Size () == 1);
}

void TestSuccessReplies ()
{
	// CONNECT: 05 00 00 01 7F 00 00 01 00 01 (I2P Java confirmConnection()).
	const auto connectReply = SOCKS5ConnectSuccessReply ();
	assert (connectReply.size () == 10);
	assert (connectReply[0] == 0x05);
	assert (connectReply[1] == 0x00); // succeeded
	assert (connectReply[2] == 0x00); // reserved
	assert (connectReply[3] == 0x01); // IPv4
	assert (connectReply[4] == 127 && connectReply[5] == 0 && connectReply[6] == 0 && connectReply[7] == 1);
	assert (connectReply[8] == 0x00 && connectReply[9] == SOCKS5_CONNECT_REPLY_PORT);
	assert (SOCKS5_CONNECT_REPLY_PORT == 1);

	// UDP ASSOCIATE: the relay port is carried, the address is loopback.
	for (const std::uint16_t port : { static_cast<std::uint16_t> (0), static_cast<std::uint16_t> (1),
			 static_cast<std::uint16_t> (9050), static_cast<std::uint16_t> (65535) })
	{
		const auto reply = SOCKS5UdpAssociateReply (port);
		assert (reply.size () == 10);
		assert (reply[0] == 0x05 && reply[1] == 0x00 && reply[3] == 0x01);
		assert (reply[4] == 127 && reply[5] == 0 && reply[6] == 0 && reply[7] == 1);
		assert (reply[8] == static_cast<std::uint8_t> (port >> 8));
		assert (reply[9] == static_cast<std::uint8_t> (port & 0xff));
	}

	// Both replies must never carry the wildcard address.
	const auto address = boost::asio::ip::address_v4 (SOCKS5_REPLY_LOOPBACK_ADDRESS);
	assert (address.is_loopback ());
	assert (!address.is_unspecified ());
	assert (SOCKS5_REPLY_LOOPBACK_ADDRESS != 0);
	// 0.0.0.0 must be absent from the encoded replies.
	for (const auto & reply : { SOCKS5ConnectSuccessReply (), SOCKS5UdpAssociateReply (1234) })
		assert (!(reply[4] == 0 && reply[5] == 0 && reply[6] == 0 && reply[7] == 0));
}
}

int main ()
{
	TestFakeAddressSpace ();
	TestInsertAndLookup ();
	TestExpiry ();
	TestBoundAndEviction ();
	TestSuccessReplies ();
	return 0;
}

/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: blocklist parser regression.
 *
 * Exercises the bounded, Java-compatible blocklist grammar: IPv4 singles,
 * ranges and CIDR prefixes, IPv6 semicolon form, router hashes, reason
 * prefixes, comments, malformed input and every parser-level cap. It uses
 * only the pure parser/builder API, so no sockets, threads, configuration or
 * filesystem are involved.
 *
 */

#include <cassert>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>

#include "Blocklist.h"

using namespace i2p::data;

namespace
{
OperatorBlocklistEntry ParseOk (std::string_view line)
{
	auto parsed = ParseOperatorBlocklistLine (line);
	assert (parsed.has_value ());
	return *parsed;
}

void ParseRejected (std::string_view line)
{
	auto parsed = ParseOperatorBlocklistLine (line);
	assert (!parsed.has_value ());
	assert (!parsed.error ().empty ());
}

void TestCommentsAndEmpty ()
{
	assert (ParseOk ("").kind == OperatorBlocklistEntryKind::Empty);
	assert (ParseOk ("\r").kind == OperatorBlocklistEntryKind::Empty);
	assert (ParseOk ("# whole-line comment").kind == OperatorBlocklistEntryKind::Comment);
	assert (ParseOk ("#1.2.3.4").kind == OperatorBlocklistEntryKind::Comment);
	// '#' is only a comment marker in column 1.
	ParseRejected ("1.2.3.4 # trailing comment");
	ParseRejected (" 1.2.3.4");
}

void TestIpv4 ()
{
	auto single = ParseOk ("1.2.3.4");
	assert (single.kind == OperatorBlocklistEntryKind::Ipv4Range);
	assert (single.from == 0x01020304u);
	assert (single.to == single.from);

	auto range = ParseOk ("10.0.0.1-10.0.0.9");
	assert (range.kind == OperatorBlocklistEntryKind::Ipv4Range);
	assert (range.from == 0x0A000001u);
	assert (range.to == 0x0A000009u);

	auto host = ParseOk ("192.168.1.5\r");
	assert (host.kind == OperatorBlocklistEntryKind::Ipv4Range);
	assert (host.from == host.to);

	auto full = ParseOk ("255.255.255.255");
	assert (full.from == 0xFFFFFFFFu);

	auto slash32 = ParseOk ("203.0.113.7/32");
	assert (slash32.from == 0xCB007107u);
	assert (slash32.to == slash32.from);

	auto slash3 = ParseOk ("128.0.0.0/3");
	assert (slash3.from == 0x80000000u);
	assert (slash3.to == 0x9FFFFFFFu);
}

void TestIpv4Rejections ()
{
	ParseRejected ("1.2.3.256");
	ParseRejected ("1.2.3");
	ParseRejected ("1.2.3.4.5");
	ParseRejected ("1.2.3.");
	ParseRejected (".1.2.3.4");
	ParseRejected ("1.2.3.-4");
	ParseRejected ("1.2.3.+4");
	ParseRejected ("10.0.0.9-10.0.0.1"); // backwards
	ParseRejected ("10.0.0.0/2"); // below the Java floor
	ParseRejected ("10.0.0.0/33");
	ParseRejected ("10.0.0.0/");
	ParseRejected ("10.0.0.0/x");
	ParseRejected ("10.0.0.0/3/4");
	// NUL bytes never reach an entry.
	std::string nul ("1.2.3.4");
	nul.push_back ('\0');
	nul += "8";
	ParseRejected (nul);
}

void TestReasons ()
{
	auto entry = ParseOk ("Some network:10.0.0.0/8");
	assert (entry.kind == OperatorBlocklistEntryKind::Ipv4Range);
	assert (entry.from == 0x0A000000u);
	assert (entry.to == 0x0AFFFFFFu);
	assert (entry.reason == "Some network");

	auto chained = ParseOk ("region:notes:1.2.3.4");
	assert (chained.reason == "region:notes");
	assert (chained.from == 0x01020304u);
}

void TestIpv6 ()
{
	auto entry = ParseOk ("2001;db8;;1");
	assert (entry.kind == OperatorBlocklistEntryKind::Ipv6Address);
	assert (entry.ipv6[0] == 0x20);
	assert (entry.ipv6[1] == 0x01);
	assert (entry.ipv6[2] == 0x0D);
	assert (entry.ipv6[3] == 0xB8);
	assert (entry.ipv6[15] == 0x01);

	auto withReason = ParseOk ("lab host:2001;db8;;2");
	assert (withReason.kind == OperatorBlocklistEntryKind::Ipv6Address);
	assert (withReason.ipv6[15] == 0x02);
	assert (withReason.reason == "lab host");

	// The colon form is documented as rejected: the last ':' is the reason
	// separator, so the remainder is not an IPv6 entry.
	ParseRejected ("2001:db8::1");
	ParseRejected ("2001;db8;;1/64"); // no IPv6 ranges or prefixes
	ParseRejected ("fe80;%eth0"); // no zone identifiers
}

void TestHashes ()
{
	IdentHash hash;
	hash.Fill (0x5A);
	auto line = hash.ToBase64 ();
	assert (line.size () == 44);
	auto entry = ParseOk (line);
	assert (entry.kind == OperatorBlocklistEntryKind::RouterHash);
	assert (entry.routerHash == hash);

	auto withReason = ParseOk ("Known bad router:" + line);
	assert (withReason.kind == OperatorBlocklistEntryKind::RouterHash);
	assert (withReason.routerHash == hash);
	assert (withReason.reason == "Known bad router");

	ParseRejected (std::string (43, '1')); // one character short of a hash and not a hostname
	ParseRejected (line + "A"); // 45 characters
	ParseRejected (std::string (44, '!'));
}

void TestHostnameSkip ()
{
	auto entry = ParseOk ("example.com");
	assert (entry.kind == OperatorBlocklistEntryKind::HostnameSkipped);
	assert (ParseOk ("localhost").kind == OperatorBlocklistEntryKind::HostnameSkipped);
	// Still a documented skip, never a DNS lookup.
	assert (ParseOk ("router.example-2.net").kind == OperatorBlocklistEntryKind::HostnameSkipped);
}

void TestLineAndFileBounds ()
{
	assert (IsOperatorBlocklistLineLengthAllowed (0));
	assert (IsOperatorBlocklistLineLengthAllowed (OPERATOR_BLOCKLIST_MAX_LINE_LENGTH));
	assert (!IsOperatorBlocklistLineLengthAllowed (OPERATOR_BLOCKLIST_MAX_LINE_LENGTH + 1));
	ParseRejected (std::string (OPERATOR_BLOCKLIST_MAX_LINE_LENGTH + 1, '1'));

	assert (IsOperatorBlocklistFileSizeAllowed (0));
	assert (IsOperatorBlocklistFileSizeAllowed (OPERATOR_BLOCKLIST_MAX_FILE_SIZE));
	assert (!IsOperatorBlocklistFileSizeAllowed (OPERATOR_BLOCKLIST_MAX_FILE_SIZE + 1));
}

void TestMerge ()
{
	OperatorBlocklistBuilder builder;
	assert (builder.AddLine ("10.0.0.0/24"));
	assert (builder.AddLine ("10.0.0.128-10.0.1.5")); // overlapping
	assert (builder.AddLine ("10.0.1.6-10.0.1.9")); // adjacent
	assert (builder.AddLine ("10.0.1.10")); // adjacent single
	auto snapshot = builder.Build ();
	assert (snapshot);
	assert (snapshot->GetNumIPv4Ranges () == 1);
	assert (snapshot->IsAddressBlocked (0x0A000000u));
	assert (snapshot->IsAddressBlocked (0x0A000109u));
	assert (snapshot->IsAddressBlocked (0x0A00010Au));
	assert (!snapshot->IsAddressBlocked (0x0A00010Bu));
	assert (!snapshot->IsAddressBlocked (0x09FFFFFFu));

	OperatorBlocklistBuilder disjoint;
	assert (disjoint.AddLine ("10.0.0.0-10.0.0.255"));
	assert (disjoint.AddLine ("10.0.2.0-10.0.2.255"));
	auto two = disjoint.Build ();
	assert (two->GetNumIPv4Ranges () == 2);

	// A range may cross the signed 32-bit boundary; this tree sorts unsigned.
	OperatorBlocklistBuilder wide;
	assert (wide.AddLine ("126.0.0.0-130.0.0.0"));
	auto crossing = wide.Build ();
	assert (crossing->GetNumIPv4Ranges () == 1);
	assert (crossing->IsAddressBlocked (0x7FFFFFFFu));
	assert (crossing->IsAddressBlocked (0x82000000u));
}

void TestRangeCap ()
{
	OperatorBlocklistBuilder builder;
	OperatorBlocklistEntry entry;
	entry.kind = OperatorBlocklistEntryKind::Ipv4Range;
	for (size_t i = 0; i < OPERATOR_BLOCKLIST_MAX_IPV4_RANGES; ++i)
	{
		entry.from = entry.to = static_cast<uint32_t> (i * 2);
		assert (builder.AddEntry (entry));
	}
	entry.from = entry.to = 0xFFFFFFFFu;
	assert (!builder.AddEntry (entry));
	assert (builder.GetRangeCapHits () == 1);
	auto snapshot = builder.Build ();
	assert (snapshot->GetNumIPv4Ranges () == OPERATOR_BLOCKLIST_MAX_IPV4_RANGES);
}

void TestHashCap ()
{
	OperatorBlocklistBuilder builder;
	OperatorBlocklistEntry entry;
	entry.kind = OperatorBlocklistEntryKind::RouterHash;
	for (size_t i = 0; i < OPERATOR_BLOCKLIST_MAX_ROUTER_HASHES; ++i)
	{
		entry.routerHash.Fill (0);
		auto * bytes = entry.routerHash ();
		bytes[0] = static_cast<uint8_t> (i & 0xFF);
		bytes[1] = static_cast<uint8_t> ((i >> 8) & 0xFF);
		bytes[2] = static_cast<uint8_t> ((i >> 16) & 0xFF);
		assert (builder.AddEntry (entry));
	}
	entry.routerHash.Fill (0xFF);
	assert (!builder.AddEntry (entry));
	assert (builder.GetHashCapHits () == 1);
	auto snapshot = builder.Build ();
	assert (snapshot->GetNumRouterHashes () == OPERATOR_BLOCKLIST_MAX_ROUTER_HASHES);
}
} // namespace

int main ()
{
	TestCommentsAndEmpty ();
	TestIpv4 ();
	TestIpv4Rejections ();
	TestReasons ();
	TestIpv6 ();
	TestHashes ();
	TestHostnameSkip ();
	TestLineAndFileBounds ();
	TestMerge ();
	TestRangeCap ();
	TestHashCap ();
	return 0;
}

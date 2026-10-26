/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: Blocklist.
 *
 * Implements the one-shot operator blocklist load, the Java-compatible
 * bounded line parser, immutable snapshot queries and the bounded in-memory
 * runtime-ban set.
 *
 * Deliberate omissions (do not add silently):
 *   - no reload watcher: an operator must restart the router to change the
 *     list, so a reader can never observe a half-built table;
 *   - no country/GeoIP blocking and no blocklist.tor/country feeds;
 *   - no hostname resolution: Java resolves hostnames while reading, but this
 *     implementation skips them so startup never depends on DNS and a
 *     resolver failure cannot silently unblock an entry;
 *   - no persisted runtime bans and no automatic banlist write-back.
 *
 */

#include <algorithm>
#include <atomic>
#include <charconv>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>
#include <string>
#include <string_view>
#include <system_error>

#include <boost/system/error_code.hpp>

#include "AtomicSharedPtr.h"
#include "Blocklist.h"
#include "Config.h"
#include "FS.h"
#include "Log.h"

namespace i2p
{
namespace data
{
	namespace
	{
		constexpr std::string_view DEFAULT_BLOCKLIST_FILE = "blocklist.txt";

		std::mutex g_LoadMutex;
		bool g_LoadAttempted = false;
		std::atomic<bool> g_Loaded { false };
		i2p::util::AtomicSharedPtr<const OperatorBlocklistSnapshot> g_Snapshot;

		// Runtime bans are a small administrative escape hatch, not a second
		// blocklist file. The count lets the profile hot path avoid the mutex
		// when the set is empty.
		std::mutex g_RuntimeBansMutex;
		std::unordered_set<IdentHash> g_RuntimeBans;
		std::atomic<size_t> g_RuntimeBanCount { 0 };
		std::atomic<bool> g_RuntimeBanFullLogged { false };

		bool LooksLikeHostname (std::string_view value)
		{
			if (value.empty ()) return false;
			bool hasAlpha = false;
			for (char c : value)
			{
				const auto byte = static_cast<unsigned char> (c);
				if (std::isalpha (byte))
					hasAlpha = true;
				else if (!std::isdigit (byte) && c != '.' && c != '-')
					return false;
			}
			return hasAlpha;
		}

		bool ContainsWhitespace (std::string_view value)
		{
			for (char c : value)
				if (std::isspace (static_cast<unsigned char> (c))) return true;
			return false;
		}

		i2p::expected<uint32_t, std::string_view> ParseIpv4Literal (std::string_view text)
		{
			if (text.empty ()) return i2p::unexpected<std::string_view> ("empty IPv4 address");
			uint32_t result = 0;
			size_t parts = 0;
			size_t offset = 0;
			while (true)
			{
				const auto dot = text.find ('.', offset);
				const auto part
					= text.substr (offset, dot == std::string_view::npos ? std::string_view::npos : dot - offset);
				if (part.empty () || part.size () > 3) return i2p::unexpected<std::string_view> ("invalid IPv4 octet");
				uint32_t octet = 0;
				const auto [end, ec] = std::from_chars (part.data (), part.data () + part.size (), octet);
				if (ec != std::errc () || end != part.data () + part.size () || octet > 255)
					return i2p::unexpected<std::string_view> ("invalid IPv4 octet");
				result = (result << 8) | octet;
				++parts;
				if (dot == std::string_view::npos) break;
				if (parts >= 4) return i2p::unexpected<std::string_view> ("too many IPv4 octets");
				offset = dot + 1;
			}
			if (parts != 4) return i2p::unexpected<std::string_view> ("IPv4 address needs four octets");
			return result;
		}

		i2p::expected<unsigned, std::string_view> ParseCidrMask (std::string_view text)
		{
			if (text.empty () || text.size () > 2) return i2p::unexpected<std::string_view> ("invalid CIDR mask");
			unsigned mask = 0;
			const auto [end, ec] = std::from_chars (text.data (), text.data () + text.size (), mask);
			if (ec != std::errc () || end != text.data () + text.size () || mask < 3 || mask > 32)
				return i2p::unexpected<std::string_view> ("CIDR mask must be between 3 and 32");
			return mask;
		}

		i2p::expected<std::array<uint8_t, 16>, std::string_view> ParseIpv6Literal (std::string_view payload)
		{
			// An on-disk IPv6 entry replaces ':' with ';' because the last ':'
			// separates an optional reason. No ranges, zone identifiers or
			// whitespace are accepted.
			if (payload.find ('/') != std::string_view::npos || payload.find ('-') != std::string_view::npos)
				return i2p::unexpected<std::string_view> ("IPv6 ranges and CIDR prefixes are not supported");
			if (payload.find ('%') != std::string_view::npos)
				return i2p::unexpected<std::string_view> ("IPv6 zone identifiers are not supported");
			std::string text (payload);
			std::replace (text.begin (), text.end (), ';', ':');
			boost::system::error_code ec;
			const auto address = boost::asio::ip::make_address_v6 (text, ec);
			if (ec) return i2p::unexpected<std::string_view> ("invalid IPv6 literal");
			return address.to_bytes ();
		}

		bool IsIpv4Mapped (const std::array<uint8_t, 16> & bytes)
		{
			for (size_t i = 0; i < 10; ++i)
				if (bytes[i]) return false;
			return bytes[10] == 0xFF && bytes[11] == 0xFF;
		}

		// Read one boolean option with an explicit fallback. GetOption() writes
		// a default-constructed value when the key is absent, so the fallback
		// must be restored from its return value rather than the mapped value.
		bool GetBooleanOption (const char * name, bool fallback)
		{
			bool value = fallback;
			if (!i2p::config::GetOption (name, value)) return fallback;
			return value;
		}
	} // namespace

	size_t OperatorBlocklistIPv6Hash::operator() (const std::array<uint8_t, 16> & value) const noexcept
	{
		// FNV-1a over the exact 16-byte address keeps the hash stable across
		// platforms and avoids relying on a compiler-provided byte hash.
		size_t hash = static_cast<size_t> (1469598103934665603ull);
		for (uint8_t byte : value)
		{
			hash ^= byte;
			hash *= static_cast<size_t> (1099511628211ull);
		}
		return hash;
	}

	bool IsOperatorBlocklistLineLengthAllowed (size_t length) noexcept
	{
		return length <= OPERATOR_BLOCKLIST_MAX_LINE_LENGTH;
	}

	bool IsOperatorBlocklistFileSizeAllowed (uint64_t size) noexcept
	{
		return size <= OPERATOR_BLOCKLIST_MAX_FILE_SIZE;
	}

	i2p::expected<OperatorBlocklistEntry, std::string_view> ParseOperatorBlocklistLine (std::string_view line)
	{
		OperatorBlocklistEntry entry;
		if (!line.empty () && line.back () == '\r') line.remove_suffix (1);
		if (!IsOperatorBlocklistLineLengthAllowed (line.size ()))
			return i2p::unexpected<std::string_view> ("blocklist line exceeds 1024 bytes");
		if (line.find ('\0') != std::string_view::npos)
			return i2p::unexpected<std::string_view> ("blocklist line contains a NUL byte");
		if (line.empty ()) return entry; // Empty
		// A '#' is a comment only in column 1. Anywhere else it is ordinary
		// text and the line will fail to parse, matching Java.
		if (line.front () == '#')
		{
			entry.kind = OperatorBlocklistEntryKind::Comment;
			return entry;
		}

		const auto colon = line.rfind (':');
		const auto payload = colon == std::string_view::npos ? line : line.substr (colon + 1);
		if (payload.empty ()) return i2p::unexpected<std::string_view> ("missing entry after reason prefix");
		if (ContainsWhitespace (payload)) return i2p::unexpected<std::string_view> ("whitespace in blocklist entry");
		if (colon != std::string_view::npos) entry.reason = line.substr (0, colon);

		// Semicolon form is checked before the 44-character hash form: ';'
		// cannot occur in Base64, so there is no ambiguity, and an exactly
		// 44-character IPv6 literal is not silently treated as a bad hash.
		if (payload.find (';') != std::string_view::npos)
		{
			auto address = ParseIpv6Literal (payload);
			if (!address) return i2p::unexpected<std::string_view> (address.error ());
			entry.kind = OperatorBlocklistEntryKind::Ipv6Address;
			entry.ipv6 = *address;
			return entry;
		}

		if (payload.size () == 44 && payload.find ('.') == std::string_view::npos)
		{
			IdentHash hash;
			if (hash.FromBase64 (payload) != IdentHash::len)
				return i2p::unexpected<std::string_view> ("invalid 44-character router hash");
			entry.kind = OperatorBlocklistEntryKind::RouterHash;
			entry.routerHash = hash;
			return entry;
		}

		// IPv4 range, CIDR or single address.
		const auto dash = payload.find ('-');
		if (dash != std::string_view::npos)
		{
			auto from = ParseIpv4Literal (payload.substr (0, dash));
			auto to = ParseIpv4Literal (payload.substr (dash + 1));
			if (from && to)
			{
				if (*to < *from) return i2p::unexpected<std::string_view> ("backwards IPv4 range");
				entry.kind = OperatorBlocklistEntryKind::Ipv4Range;
				entry.from = *from;
				entry.to = *to;
				return entry;
			}
			// A hyphenated hostname is a documented skip; only numeric garbage
			// reaches the generic rejection below.
		}
		else
		{
			const auto slash = payload.find ('/');
			if (slash != std::string_view::npos)
			{
				auto address = ParseIpv4Literal (payload.substr (0, slash));
				if (!address) return i2p::unexpected<std::string_view> (address.error ());
				auto mask = ParseCidrMask (payload.substr (slash + 1));
				if (!mask) return i2p::unexpected<std::string_view> (mask.error ());
				// Java Blocklist.java sets the low 32-mask bits of the address.
				const uint32_t hostMask = *mask == 32 ? 0 : (0xFFFFFFFFu >> *mask);
				entry.kind = OperatorBlocklistEntryKind::Ipv4Range;
				entry.from = *address;
				entry.to = *address | hostMask;
				return entry;
			}

			auto address = ParseIpv4Literal (payload);
			if (address)
			{
				entry.kind = OperatorBlocklistEntryKind::Ipv4Range;
				entry.from = entry.to = *address;
				return entry;
			}
		}

		// Java resolves a hostname at list-read time. Never perform DNS here:
		// a deterministic skip cannot be controlled by a hostile resolver.
		if (LooksLikeHostname (payload))
		{
			entry.kind = OperatorBlocklistEntryKind::HostnameSkipped;
			return entry;
		}
		return i2p::unexpected<std::string_view> ("invalid blocklist entry");
	}

	bool OperatorBlocklistSnapshot::IsAddressBlocked (uint32_t ipv4) const
	{
		// The ranges are sorted by `from` and merged, so the candidate range is
		// the last one starting at or before the address.
		const auto it = std::upper_bound (m_Ipv4Ranges.begin (), m_Ipv4Ranges.end (), ipv4,
			[] (uint32_t value, const std::pair<uint32_t, uint32_t> & range) { return value < range.first; });
		if (it == m_Ipv4Ranges.begin ()) return false;
		const auto & range = *std::prev (it);
		return ipv4 <= range.second;
	}

	bool OperatorBlocklistSnapshot::IsAddressBlocked (const std::array<uint8_t, 16> & ipv6) const
	{
		return m_Ipv6Addresses.contains (ipv6);
	}

	bool OperatorBlocklistSnapshot::IsAddressBlocked (const boost::asio::ip::address & address) const
	{
		if (address.is_v4 ()) return IsAddressBlocked (address.to_v4 ().to_uint ());
		if (!address.is_v6 ()) return false;
		const auto bytes = address.to_v6 ().to_bytes ();
		// An IPv4-mapped IPv6 peer must not bypass an IPv4 range entry.
		if (IsIpv4Mapped (bytes))
		{
			const uint32_t ipv4 = (static_cast<uint32_t> (bytes[12]) << 24) | (static_cast<uint32_t> (bytes[13]) << 16)
				| (static_cast<uint32_t> (bytes[14]) << 8) | bytes[15];
			if (IsAddressBlocked (ipv4)) return true;
		}
		return m_Ipv6Addresses.contains (bytes);
	}

	bool OperatorBlocklistSnapshot::ContainsRouter (const IdentHash & routerHash) const
	{
		return m_RouterHashes.contains (routerHash);
	}

	void OperatorBlocklistBuilder::RetainDiagnosticReason (std::string_view reason)
	{
		if (!m_RetainDiagnosticReasons) return;
		if (m_DiagnosticReasons.size () >= OPERATOR_BLOCKLIST_MAX_DIAGNOSTIC_REASONS) return;
		const size_t length = std::min (reason.size (), OPERATOR_BLOCKLIST_MAX_REASON_LENGTH);
		m_DiagnosticReasons.emplace_back (reason.substr (0, length));
	}

	bool OperatorBlocklistBuilder::AddEntry (const OperatorBlocklistEntry & entry)
	{
		switch (entry.kind)
		{
		case OperatorBlocklistEntryKind::Ipv4Range:
			if (m_Ipv4Ranges.size () >= OPERATOR_BLOCKLIST_MAX_IPV4_RANGES)
			{
				++m_RangeCapHits;
				return false;
			}
			m_Ipv4Ranges.emplace_back (entry.from, entry.to);
			return true;
		case OperatorBlocklistEntryKind::Ipv6Address:
			if (m_Ipv6Addresses.size () >= OPERATOR_BLOCKLIST_MAX_IPV6_ADDRESSES
				&& !m_Ipv6Addresses.contains (entry.ipv6))
			{
				++m_AddressCapHits;
				return false;
			}
			m_Ipv6Addresses.insert (entry.ipv6);
			return true;
		case OperatorBlocklistEntryKind::RouterHash:
			if (m_RouterHashes.size () >= OPERATOR_BLOCKLIST_MAX_ROUTER_HASHES
				&& !m_RouterHashes.contains (entry.routerHash))
			{
				++m_HashCapHits;
				return false;
			}
			m_RouterHashes.insert (entry.routerHash);
			return true;
		default: return false;
		}
	}

	bool OperatorBlocklistBuilder::AddLine (std::string_view line)
	{
		auto parsed = ParseOperatorBlocklistLine (line);
		if (!parsed)
		{
			++m_RejectedLines;
			RetainDiagnosticReason (line);
			return false;
		}
		if (parsed->kind == OperatorBlocklistEntryKind::HostnameSkipped)
		{
			++m_SkippedHostnames;
			RetainDiagnosticReason (line);
			return true;
		}
		if (parsed->kind == OperatorBlocklistEntryKind::Empty || parsed->kind == OperatorBlocklistEntryKind::Comment)
			return true;
		if (!AddEntry (*parsed))
		{
			RetainDiagnosticReason (line);
			return false;
		}
		return true;
	}

	std::shared_ptr<const OperatorBlocklistSnapshot> OperatorBlocklistBuilder::Build ()
	{
		std::ranges::sort (m_Ipv4Ranges);
		std::vector<std::pair<uint32_t, uint32_t>> merged;
		merged.reserve (m_Ipv4Ranges.size ());
		for (const auto & range : m_Ipv4Ranges)
		{
			if (!merged.empty ()
				&& static_cast<uint64_t> (range.first) <= static_cast<uint64_t> (merged.back ().second) + 1)
			{
				// Coalesce overlapping and adjacent ranges. The uint64 compare
				// avoids wrapping when the previous range ends at 255.255.255.255.
				if (range.second > merged.back ().second) merged.back ().second = range.second;
			}
			else
				merged.push_back (range);
		}

		auto snapshot = std::make_shared<OperatorBlocklistSnapshot> ();
		snapshot->m_Ipv4Ranges = std::move (merged);
		snapshot->m_Ipv6Addresses = std::move (m_Ipv6Addresses);
		snapshot->m_RouterHashes = std::move (m_RouterHashes);
		return snapshot;
	}

	bool LoadOperatorBlocklist ()
	{
		std::lock_guard<std::mutex> lock (g_LoadMutex);
		if (g_LoadAttempted) return g_Loaded.load (std::memory_order_acquire);
		g_LoadAttempted = true;

		if (!GetBooleanOption ("blocklist.enable", true))
		{
			g_Snapshot.Store (std::make_shared<const OperatorBlocklistSnapshot> ());
			LogPrint (eLogInfo, "Blocklist: Disabled by configuration");
			return true;
		}

		std::string configured;
		i2p::config::GetOption ("blocklist.file", configured);
		const bool configuredByOperator = !configured.empty ();
		if (!configuredByOperator) configured = std::string (DEFAULT_BLOCKLIST_FILE);
		std::filesystem::path path (configured);
		if (path.is_relative ()) path = std::filesystem::path (i2p::fs::GetDataDir ()) / path;

		const bool retainReasons = GetBooleanOption ("blocklist.detail", false);

		std::ifstream stream (path, std::ios::binary);
		if (!stream)
		{
			// A missing default file is the normal state (no operator list is
			// shipped). Only an explicitly configured path that cannot be read
			// is worth a warning.
			LogPrint (configuredByOperator ? eLogWarning : eLogInfo, "Blocklist: File not found or unreadable ",
				path.string ());
			g_Snapshot.Store (std::make_shared<const OperatorBlocklistSnapshot> ());
			return false;
		}
		stream.seekg (0, std::ios::end);
		const auto fileSize = stream.tellg ();
		if (fileSize < 0 || !IsOperatorBlocklistFileSizeAllowed (static_cast<uint64_t> (fileSize)))
		{
			LogPrint (eLogError, "Blocklist: File is not a bounded regular file ", path.string ());
			g_Snapshot.Store (std::make_shared<const OperatorBlocklistSnapshot> ());
			return false;
		}
		std::string contents (static_cast<size_t> (fileSize), '\0');
		stream.seekg (0, std::ios::beg);
		stream.read (contents.data (), static_cast<std::streamsize> (contents.size ()));
		if (!stream || stream.gcount () != static_cast<std::streamsize> (contents.size ()))
		{
			LogPrint (eLogError, "Blocklist: Short read from ", path.string ());
			g_Snapshot.Store (std::make_shared<const OperatorBlocklistSnapshot> ());
			return false;
		}
		if (contents.find ('\0') != std::string::npos)
		{
			LogPrint (eLogError, "Blocklist: File contains a NUL byte ", path.string ());
			g_Snapshot.Store (std::make_shared<const OperatorBlocklistSnapshot> ());
			return false;
		}

		OperatorBlocklistBuilder builder (retainReasons);
		size_t offset = 0;
		while (offset <= contents.size ())
		{
			auto end = contents.find ('\n', offset);
			if (end == std::string::npos) end = contents.size ();
			builder.AddLine (std::string_view (contents).substr (offset, end - offset));
			if (end == contents.size ()) break;
			offset = end + 1;
		}

		auto snapshot = builder.Build ();
		LogPrint (eLogInfo, "Blocklist: ", snapshot->GetNumIPv4Ranges (), " IPv4 ranges, ",
			snapshot->GetNumIPv6Addresses (), " IPv6 addresses, ", snapshot->GetNumRouterHashes (),
			" router hashes loaded from ", path.string ());
		if (builder.GetSkippedHostnames ())
			LogPrint (eLogWarning, "Blocklist: Skipped ", builder.GetSkippedHostnames (),
				" hostname entries; DNS resolution is not performed");
		if (builder.GetRejectedLines ())
			LogPrint (eLogWarning, "Blocklist: Rejected ", builder.GetRejectedLines (), " malformed lines");
		if (builder.GetRangeCapHits () || builder.GetHashCapHits () || builder.GetAddressCapHits ())
			LogPrint (eLogWarning, "Blocklist: Entry caps reached; further entries were ignored (",
				builder.GetRangeCapHits (), " ranges, ", builder.GetAddressCapHits (), " addresses, ",
				builder.GetHashCapHits (), " hashes)");
		if (retainReasons)
			for (size_t i = 0; i < builder.GetDiagnosticReasonCount (); ++i)
				LogPrint (eLogDebug, "Blocklist: Skipped entry: ", builder.GetDiagnosticReason (i));

		g_Snapshot.Store (std::move (snapshot));
		g_Loaded.store (true, std::memory_order_release);
		return true;
	}

	bool IsOperatorBlockedAddress (const boost::asio::ip::address & address)
	{
		auto snapshot = g_Snapshot.Load ();
		return snapshot && snapshot->IsAddressBlocked (address);
	}

	bool IsOperatorBlockedRouter (const IdentHash & hash)
	{
		auto snapshot = g_Snapshot.Load ();
		return snapshot && snapshot->ContainsRouter (hash);
	}

	bool IsRuntimeRouterBanned (const IdentHash & hash)
	{
		if (g_RuntimeBanCount.load (std::memory_order_acquire) == 0) return false;
		std::lock_guard<std::mutex> lock (g_RuntimeBansMutex);
		return g_RuntimeBans.contains (hash);
	}

	bool AddRuntimeRouterBan (const IdentHash & hash)
	{
		std::lock_guard<std::mutex> lock (g_RuntimeBansMutex);
		if (g_RuntimeBans.contains (hash)) return true;
		if (g_RuntimeBans.size () >= OPERATOR_BLOCKLIST_MAX_RUNTIME_BANS)
		{
			if (!g_RuntimeBanFullLogged.exchange (true, std::memory_order_relaxed))
				LogPrint (eLogWarning, "Blocklist: Runtime ban set is full (", OPERATOR_BLOCKLIST_MAX_RUNTIME_BANS,
					" entries); refusing ", hash.ToBase64 ());
			return false;
		}
		g_RuntimeBans.insert (hash);
		g_RuntimeBanCount.store (g_RuntimeBans.size (), std::memory_order_release);
		return true;
	}

	bool RemoveRuntimeRouterBan (const IdentHash & hash)
	{
		std::lock_guard<std::mutex> lock (g_RuntimeBansMutex);
		const bool removed = g_RuntimeBans.erase (hash) > 0;
		if (removed) g_RuntimeBanCount.store (g_RuntimeBans.size (), std::memory_order_release);
		return removed;
	}

	size_t GetRuntimeRouterBans (std::vector<IdentHash> & out, size_t max)
	{
		out.clear ();
		std::lock_guard<std::mutex> lock (g_RuntimeBansMutex);
		out.reserve (std::min (max, g_RuntimeBans.size ()));
		for (const auto & hash : g_RuntimeBans)
		{
			if (out.size () >= max) break;
			out.push_back (hash);
		}
		return out.size ();
	}

	size_t GetRuntimeRouterBanCount ()
	{
		return g_RuntimeBanCount.load (std::memory_order_acquire);
	}

	OperatorBlocklistStats GetOperatorBlocklistStats ()
	{
		OperatorBlocklistStats stats;
		stats.loaded = g_Loaded.load (std::memory_order_acquire);
		auto snapshot = g_Snapshot.Load ();
		if (snapshot)
		{
			stats.ipv4Ranges = snapshot->GetNumIPv4Ranges ();
			stats.ipv6Addresses = snapshot->GetNumIPv6Addresses ();
			stats.routerHashes = snapshot->GetNumRouterHashes ();
		}
		return stats;
	}
} // namespace data
} // namespace i2p

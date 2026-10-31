/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: Blocklist.
 *
 * Declares the operator blocklist: a bounded parser for the
 * Java-compatible blocklist file grammar, an immutable snapshot
 * published through an atomic shared pointer for lock-free reader
 * queries, and a small in-memory runtime-ban set. Deliberately no
 * reload watcher, no country/GeoIP blocking and no persisted runtime
 * bans; see Blocklist.cpp for the documented omissions.
 *
 */

#ifndef BLOCKLIST_H__
#define BLOCKLIST_H__

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#include <boost/asio/ip/address.hpp>

#include "Expected.h"
#include "Identity.h"

namespace i2p
{
namespace data
{
	// Bounds mirror Java Blocklist.java plus the hardening limits of this
	// tree. The line and file caps are checked before any per-entry work
	// so a hostile or corrupt file cannot grow the parser state without
	// limit.
	constexpr size_t OPERATOR_BLOCKLIST_MAX_LINE_LENGTH = 1024;
	constexpr uint64_t OPERATOR_BLOCKLIST_MAX_FILE_SIZE = 16 * 1024 * 1024;
	constexpr size_t OPERATOR_BLOCKLIST_MAX_IPV4_RANGES = 262144;
	constexpr size_t OPERATOR_BLOCKLIST_MAX_IPV6_ADDRESSES = 65536;
	constexpr size_t OPERATOR_BLOCKLIST_MAX_ROUTER_HASHES = 65536;
	constexpr size_t OPERATOR_BLOCKLIST_MAX_RUNTIME_BANS = 1024;
	// Bounded, optional reason sample retained when blocklist.detail is
	// true.
	constexpr size_t OPERATOR_BLOCKLIST_MAX_DIAGNOSTIC_REASONS = 8;
	constexpr size_t OPERATOR_BLOCKLIST_MAX_REASON_LENGTH = 256;

	struct OperatorBlocklistIPv6Hash
	{
		size_t operator() (const std::array<uint8_t, 16> & value) const noexcept;
	};

	enum class OperatorBlocklistEntryKind : uint8_t
	{
		Empty = 0,
		Comment,
		Ipv4Range, // a single IPv4 address is a range with from == to
		Ipv6Address,
		RouterHash,
		HostnameSkipped // Java would resolve it; this implementation never does DNS
	};

	/**
	 * One parsed blocklist line.
	 *
	 * `reason` is the optional `comment:` prefix before the last ':'. It
	 * is a view into the input line, so it is only valid while the caller
	 * keeps the line alive; the builder copies it only when diagnostic
	 * detail is enabled.
	 */
	struct OperatorBlocklistEntry
	{
		OperatorBlocklistEntryKind kind = OperatorBlocklistEntryKind::Empty;
		uint32_t from = 0;
		uint32_t to = 0;
		std::array<uint8_t, 16> ipv6 { };
		IdentHash routerHash { };
		std::string_view reason;
	};

	/**
	 * Parse one blocklist line.
	 *
	 * Grammar (Java Blocklist.java compatible): `#` in column 1 is a
	 * whole-line comment; `comment:...:entry` keeps everything before the
	 * last ':' as reason; `a.b.c.d`, `a.b.c.d-a.b.c.d2`, `a.b.c.d/3..32`;
	 * an IPv6 literal with ';' instead of ':' (no ranges); a 44-character
	 * Base64 router hash (32 decoded bytes). Hostnames are never resolved:
	 * they return HostnameSkipped and the caller logs one aggregate
	 * warning. An IPv6 literal written with ':' is rejected because the
	 * last ':' is the comment separator, matching Java behaviour.
	 * Malformed input is reported as a value, never as an exception; the
	 * error payload is a string_view literal with static lifetime. Only an
	 * allocation failure can throw.
	 */
	i2p::expected<OperatorBlocklistEntry, std::string_view> ParseOperatorBlocklistLine (std::string_view line);

	bool IsOperatorBlocklistLineLengthAllowed (size_t length) noexcept;
	bool IsOperatorBlocklistFileSizeAllowed (uint64_t size) noexcept;

	/**
	 * Immutable parsed blocklist.
	 *
	 * IPv4 ranges are sorted and merged (overlapping and adjacent ranges
	 * are coalesced). IPv6 entries are exact addresses. Queries never
	 * mutate the snapshot and take no lock; publication swaps the whole
	 * object.
	 */
	class OperatorBlocklistSnapshot
	{
	public:
		bool IsAddressBlocked (const boost::asio::ip::address & address) const;
		bool IsAddressBlocked (uint32_t ipv4) const;
		bool IsAddressBlocked (const std::array<uint8_t, 16> & ipv6) const;
		bool ContainsRouter (const IdentHash & routerHash) const;
		size_t GetNumIPv4Ranges () const noexcept { return m_Ipv4Ranges.size (); }
		size_t GetNumIPv6Addresses () const noexcept { return m_Ipv6Addresses.size (); }
		size_t GetNumRouterHashes () const noexcept { return m_RouterHashes.size (); }

	private:
		friend class OperatorBlocklistBuilder;

		std::vector<std::pair<uint32_t, uint32_t>> m_Ipv4Ranges; // inclusive, sorted, merged
		std::unordered_set<std::array<uint8_t, 16>, OperatorBlocklistIPv6Hash> m_Ipv6Addresses;
		std::unordered_set<IdentHash> m_RouterHashes;
	};

	/**
	 * Bounded accumulator used by the startup loader and the parser tests.
	 * AddLine() records skipped hostnames and rejected lines; the loader
	 * logs one message per class after Build() instead of one per line.
	 */
	class OperatorBlocklistBuilder
	{
	public:
		explicit OperatorBlocklistBuilder (bool retainDiagnosticReasons = false)
			: m_RetainDiagnosticReasons (retainDiagnosticReasons)
		{
		}

		// Returns false when the line was rejected or the corresponding entry
		// cap is already full. Hostname lines are accepted as a documented
		// skip.
		bool AddLine (std::string_view line);
		bool AddEntry (const OperatorBlocklistEntry & entry);

		std::shared_ptr<const OperatorBlocklistSnapshot> Build ();

		size_t GetSkippedHostnames () const noexcept { return m_SkippedHostnames; }
		size_t GetRejectedLines () const noexcept { return m_RejectedLines; }
		size_t GetRangeCapHits () const noexcept { return m_RangeCapHits; }
		size_t GetAddressCapHits () const noexcept { return m_AddressCapHits; }
		size_t GetHashCapHits () const noexcept { return m_HashCapHits; }
		size_t GetDiagnosticReasonCount () const noexcept { return m_DiagnosticReasons.size (); }
		const std::string & GetDiagnosticReason (size_t index) const { return m_DiagnosticReasons.at (index); }

	private:
		void RetainDiagnosticReason (std::string_view reason);

	private:
		bool m_RetainDiagnosticReasons;
		size_t m_SkippedHostnames = 0;
		size_t m_RejectedLines = 0;
		size_t m_RangeCapHits = 0;
		size_t m_AddressCapHits = 0;
		size_t m_HashCapHits = 0;
		std::vector<std::pair<uint32_t, uint32_t>> m_Ipv4Ranges;
		std::unordered_set<std::array<uint8_t, 16>, OperatorBlocklistIPv6Hash> m_Ipv6Addresses;
		std::unordered_set<IdentHash> m_RouterHashes;
		std::vector<std::string> m_DiagnosticReasons;
	};

	struct OperatorBlocklistStats
	{
		size_t ipv4Ranges = 0;
		size_t ipv6Addresses = 0;
		size_t routerHashes = 0;
		bool loaded = false;
	};

	/**
	 * One-shot startup load. Reads blocklist.enable/blocklist.file/
	 * blocklist.detail from the configuration, resolves a relative path
	 * against the data directory and publishes one immutable snapshot.
	 * Idempotent: later calls return the first result without touching
	 * disk. The caller must not be an io_context thread (this is the only
	 * file IO in the subsystem and happens before NetDb loads
	 * RouterInfos/reseeds).
	 */
	bool LoadOperatorBlocklist ();

	/**
	 * Query the published snapshot. Cheap and non-blocking on the hot
	 * path.
	 */
	bool IsOperatorBlockedAddress (const boost::asio::ip::address & address);
	bool IsOperatorBlockedRouter (const IdentHash & hash);

	/** In-memory only, bounded, never persisted. */
	bool AddRuntimeRouterBan (const IdentHash & hash);
	bool RemoveRuntimeRouterBan (const IdentHash & hash);
	bool IsRuntimeRouterBanned (const IdentHash & hash);
	// Replaces `out` with at most `max` entries and returns the count
	// written.
	size_t GetRuntimeRouterBans (std::vector<IdentHash> & out, size_t max);
	size_t GetRuntimeRouterBanCount ();

	OperatorBlocklistStats GetOperatorBlocklistStats ();
}
}

#endif

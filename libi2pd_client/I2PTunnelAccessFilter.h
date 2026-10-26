/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: I2PTunnelAccessFilter.
 *
 * Parses and evaluates the server-tunnel `filterDefinition` grammar used by
 * the Java i2ptunnel `access/*` elements: a `default=allow|deny` fallback plus
 * an ordered list of `explicit`, `file` and `record` rules whose first match
 * decides. The definition is immutable; the runtime object caches parsed
 * `file` lists (reloaded when the file's mtime/size changes, bounded to 1 MiB
 * and 100k entries) and the per-destination `record` counters.
 *
 * Evaluation never blocks on the packet path beyond one bounded local file
 * read per changed list and never takes a lock that the streaming packet path
 * holds. No rule value, file path or entry list is ever logged.
 *
 */

#ifndef I2PTUNNEL_ACCESS_FILTER_H__
#define I2PTUNNEL_ACCESS_FILTER_H__

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include "Identity.h"

namespace i2p
{
namespace client
{
	// Action the streaming layer takes for a peer refused by this filter.
	enum class TunnelAccessRejectAction
	{
		eDrop, // silence: no SYNACK and no RESET
		eReset // one signed RESET
	};

	// One parsed `filterDefinition` value. Instances are immutable and shared
	// between the configuration reader and the runtime evaluator.
	class FilterDefinition
	{
	public:
		enum class ElementKind
		{
			eExplicit,
			eFile,
			eRecord
		};

		// A dotted component mask matched against the loopback-mapped IPv4
		// address of a peer (127.<hash byte 0>.<byte 1>.<byte 2>, the same
		// convention the Linux unique-local mapping uses). `-1` is a wildcard.
		struct AddressMask
		{
			std::array<int, 4> octets { -1, -1, -1, -1 };
		};

		struct Element
		{
			ElementKind kind = ElementKind::eExplicit;
			bool allow = true;
			// Explicit and file entries.
			std::vector<i2p::data::IdentHash> destinations;
			std::vector<AddressMask> masks;
			bool matchesAny = false; // the `*` wildcard entry
			// File entries only: absolute path, validated at parse time.
			std::string path;
			// Record entries only: match while the peer's recorded connection
			// count is at most `threshold`. A zero threshold never matches.
			uint32_t threshold = 0;
		};

		static constexpr size_t MAX_DEFINITION_LENGTH = 64 * 1024;
		static constexpr size_t MAX_ELEMENTS = 256;
		static constexpr size_t MAX_FILE_BYTES = 1024 * 1024;
		static constexpr size_t MAX_FILE_ENTRIES = 100000;
		static constexpr size_t MAX_FILE_ELEMENTS = 16;
		static constexpr size_t MAX_TARGET_LENGTH = 256;

		// Parses a `filterDefinition` value (`;` or newline separated). Returns
		// nullptr and sets a non-secret `error` description on invalid grammar.
		static std::shared_ptr<const FilterDefinition> Parse (std::string_view definition, std::string & error);
		// Upgrade path for the legacy inline `accesslist`/`whitelist`: the
		// listed destinations are allowed and everything else is denied.
		static std::shared_ptr<const FilterDefinition> FromAccessList (
			const std::vector<i2p::data::IdentHash> & destinations);
		static std::shared_ptr<const FilterDefinition> AllowAll ();
		static std::shared_ptr<const FilterDefinition> DenyAll ();

		bool IsDefaultAllow () const { return m_DefaultAllow; }
		const std::vector<Element> & GetElements () const { return m_Elements; }
		size_t GetNumElements () const { return m_Elements.size (); }

	private:
		bool m_DefaultAllow = true;
		std::vector<Element> m_Elements;
	};

	// Runtime filter state: parsed `file` lists and `record` counters.
	class TunnelAccessFilter
	{
	public:
		struct Decision
		{
			bool allow = false;
			bool matched = false; // false when the definition default decided
		};

		explicit TunnelAccessFilter (std::shared_ptr<const FilterDefinition> definition);

		// First match wins. `record` elements count this connection, so one
		// call per accepted SYN.
		Decision Evaluate (const i2p::data::IdentHash & peer, uint64_t nowSeconds);
		// Same evaluation without consuming a record count, for a secondary
		// defensive check after the stream already exists.
		Decision Peek (const i2p::data::IdentHash & peer, uint64_t nowSeconds) const;

		// Test and reload hooks. A zero cache interval re-checks the file's
		// mtime/size on every evaluation; the production default bounds the
		// stat rate to once per five seconds.
		void SetFileCacheSeconds (uint64_t seconds) { m_FileCacheSeconds = seconds; }
		void ClearRecords ();

		size_t GetNumRecords () const;
		size_t GetNumFileElements () const;
		const std::shared_ptr<const FilterDefinition> & GetDefinition () const { return m_Definition; }

		static constexpr uint64_t DEFAULT_FILE_CACHE_SECONDS = 5;
		static constexpr size_t MAX_RECORDS = 4096;
		static constexpr uint64_t RECORD_TIMEOUT = 24 * 60 * 60; // in seconds

	private:
		struct FileCache
		{
			bool loaded = false;
			uint64_t mtime = 0;
			uint64_t size = 0;
			uint64_t lastCheck = 0;
			std::vector<i2p::data::IdentHash> destinations;
			std::vector<FilterDefinition::AddressMask> masks;
			bool matchesAny = false;
			size_t rejectedLines = 0;
		};

		struct RecordEntry
		{
			uint32_t count = 0;
			uint64_t lastSeen = 0;
		};

		// All helpers below run with m_Mutex held; EvaluateInternal is the only
		// place that takes it, so no rule evaluation ever nests a filter lock.
		FileCache & GetFileCacheLocked (const std::string & path, uint64_t nowSeconds) const;
		uint32_t NextRecordCountLocked (const i2p::data::IdentHash & peer, uint64_t nowSeconds) const;
		uint32_t PeekRecordCountLocked (const i2p::data::IdentHash & peer) const;
		Decision EvaluateInternal (const i2p::data::IdentHash & peer, uint64_t nowSeconds, bool consumeRecord) const;

	private:
		std::shared_ptr<const FilterDefinition> m_Definition;
		uint64_t m_FileCacheSeconds = DEFAULT_FILE_CACHE_SECONDS;
		mutable std::mutex m_Mutex;
		mutable std::unordered_map<std::string, FileCache> m_Files;
		mutable std::unordered_map<i2p::data::IdentHash, RecordEntry> m_Records;
	};

	// Parses one filter target (`*`, a base32/base64 destination or a dotted
	// address mask). Exposed for the configuration validator and the tests.
	bool ParseFilterTarget (std::string_view target, std::vector<i2p::data::IdentHash> & destinations,
		std::vector<FilterDefinition::AddressMask> & masks, bool & matchesAny, std::string & error);

	// True when `mask` matches the loopback-mapped IPv4 address of `peer`.
	bool MatchesAddressMask (const FilterDefinition::AddressMask & mask, const i2p::data::IdentHash & peer);
}
}

#endif

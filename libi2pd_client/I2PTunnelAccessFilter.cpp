/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: I2PTunnelAccessFilter.
 *
 * Implements the Java i2ptunnel `access` filter grammar: ordered `explicit`,
 * `file` and `record` elements with first-match-wins evaluation, an immutable
 * definition, bounded file caching with mtime/size reload and per-destination
 * record counters. Invalid grammar is rejected at configuration load; entry
 * values and file contents are never logged.
 *
 */

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <system_error>
#include "I2PTunnelAccessFilter.h"
#include "Log.h"

namespace i2p
{
namespace client
{
	namespace
	{
		bool IsSpace (char ch)
		{
			return ch == ' ' || ch == '\t' || ch == '\r';
		}

		std::string_view Trim (std::string_view text)
		{
			while (!text.empty () && IsSpace (text.front ())) text.remove_prefix (1);
			while (!text.empty () && IsSpace (text.back ())) text.remove_suffix (1);
			return text;
		}

		bool EqualsIgnoreCase (std::string_view text, std::string_view expected)
		{
			if (text.size () != expected.size ()) return false;
			for (size_t i = 0; i < text.size (); ++i)
			{
				char a = text[i], b = expected[i];
				if (a >= 'A' && a <= 'Z') a = static_cast<char> (a + ('a' - 'A'));
				if (b >= 'A' && b <= 'Z') b = static_cast<char> (b + ('a' - 'A'));
				if (a != b) return false;
			}
			return true;
		}

		bool ParseOctet (std::string_view text, int & value)
		{
			if (text.empty () || text.size () > 3) return false;
			int result = 0;
			for (char ch : text)
			{
				if (ch < '0' || ch > '9') return false;
				result = result * 10 + (ch - '0');
			}
			if (result > 255) return false;
			value = result;
			return true;
		}

		bool ParseAddressMask (std::string_view text, FilterDefinition::AddressMask & mask)
		{
			std::array<int, 4> octets { -1, -1, -1, -1 };
			size_t index = 0, start = 0;
			while (start <= text.size ())
			{
				auto dot = text.find ('.', start);
				auto component
					= text.substr (start, dot == std::string_view::npos ? std::string_view::npos : dot - start);
				if (index >= octets.size ()) return false;
				if (component == "*")
					octets[index] = -1;
				else if (!ParseOctet (component, octets[index]))
					return false;
				++index;
				if (dot == std::string_view::npos) break;
				start = dot + 1;
			}
			if (index != octets.size ()) return false;
			mask.octets = octets;
			return true;
		}

		bool AppendTarget (std::string_view target, FilterDefinition::Element & element, std::string & error)
		{
			std::vector<i2p::data::IdentHash> destinations;
			std::vector<FilterDefinition::AddressMask> masks;
			bool matchesAny = false;
			if (!ParseFilterTarget (target, destinations, masks, matchesAny, error)) return false;
			element.destinations.insert (element.destinations.end (), destinations.begin (), destinations.end ());
			element.masks.insert (element.masks.end (), masks.begin (), masks.end ());
			element.matchesAny = element.matchesAny || matchesAny;
			return true;
		}

		bool MatchesEntries (const std::vector<i2p::data::IdentHash> & destinations,
			const std::vector<FilterDefinition::AddressMask> & masks, bool matchesAny,
			const i2p::data::IdentHash & peer)
		{
			if (matchesAny) return true;
			for (const auto & destination : destinations)
				if (destination == peer) return true;
			for (const auto & mask : masks)
				if (MatchesAddressMask (mask, peer)) return true;
			return false;
		}

		uint64_t FileMilliseconds (const std::filesystem::file_time_type & time)
		{
			return static_cast<uint64_t> (
				std::chrono::duration_cast<std::chrono::milliseconds> (time.time_since_epoch ()).count ());
		}
	}

	bool ParseFilterTarget (std::string_view target, std::vector<i2p::data::IdentHash> & destinations,
		std::vector<FilterDefinition::AddressMask> & masks, bool & matchesAny, std::string & error)
	{
		matchesAny = false;
		if (target.empty ())
		{
			error = "empty target";
			return false;
		}
		if (target.size () > FilterDefinition::MAX_TARGET_LENGTH)
		{
			error = "target too long";
			return false;
		}
		if (target == "*")
		{
			matchesAny = true;
			return true;
		}
		if (target.find ('.') != std::string_view::npos || target.find ('*') != std::string_view::npos)
		{
			FilterDefinition::AddressMask mask;
			if (!ParseAddressMask (target, mask))
			{
				error = "invalid address mask";
				return false;
			}
			masks.push_back (mask);
			return true;
		}
		i2p::data::IdentHash destination;
		// Base32 of a 32-byte hash is 52 characters, base64 is 44.
		constexpr size_t BASE32_LENGTH = (32 * 8 + 4) / 5;
		if (target.size () == BASE32_LENGTH && destination.FromBase32 (target) == destination.len)
		{
			destinations.push_back (destination);
			return true;
		}
		if (destination.FromBase64 (target) == destination.len)
		{
			destinations.push_back (destination);
			return true;
		}
		// A zero hash would silently match (or refuse) every peer, so an
		// unparsable entry is a configuration error instead.
		error = "unsupported target (expected base32, base64, address mask or *)";
		return false;
	}

	bool MatchesAddressMask (const FilterDefinition::AddressMask & mask, const i2p::data::IdentHash & peer)
	{
		// Same convention as the Linux unique-local mapping: 127 followed by
		// the first three identity bytes.
		const uint8_t * ident = peer;
		const std::array<uint8_t, 4> address { 127, ident[0], ident[1], ident[2] };
		for (size_t i = 0; i < mask.octets.size (); ++i)
		{
			if (mask.octets[i] < 0) continue;
			if (mask.octets[i] != static_cast<int> (address[i])) return false;
		}
		return true;
	}

	std::shared_ptr<const FilterDefinition> FilterDefinition::AllowAll ()
	{
		auto definition = std::make_shared<FilterDefinition> ();
		definition->m_DefaultAllow = true;
		return definition;
	}

	std::shared_ptr<const FilterDefinition> FilterDefinition::DenyAll ()
	{
		auto definition = std::make_shared<FilterDefinition> ();
		definition->m_DefaultAllow = false;
		return definition;
	}

	std::shared_ptr<const FilterDefinition> FilterDefinition::FromAccessList (
		const std::vector<i2p::data::IdentHash> & destinations)
	{
		auto definition = std::make_shared<FilterDefinition> ();
		definition->m_DefaultAllow = false;
		if (destinations.empty ()) return definition;
		Element element;
		element.kind = ElementKind::eExplicit;
		element.allow = true;
		element.destinations = destinations;
		definition->m_Elements.push_back (std::move (element));
		return definition;
	}

	std::shared_ptr<const FilterDefinition> FilterDefinition::Parse (std::string_view definition, std::string & error)
	{
		error.clear ();
		if (definition.size () > MAX_DEFINITION_LENGTH)
		{
			error = "definition too long";
			return nullptr;
		}

		auto parsed = std::make_shared<FilterDefinition> ();
		std::vector<std::string_view> tokens;
		size_t start = 0;
		while (start <= definition.size ())
		{
			auto end = definition.find_first_of (";\n", start);
			auto chunk
				= definition.substr (start, end == std::string_view::npos ? std::string_view::npos : end - start);
			chunk = Trim (chunk);
			if (!chunk.empty () && chunk.front () != '#') tokens.push_back (chunk);
			if (end == std::string_view::npos) break;
			start = end + 1;
		}

		bool defaultSeen = false;
		size_t numFileElements = 0;
		for (const auto & token : tokens)
		{
			auto equal = token.find ('=');
			if (equal != std::string_view::npos)
			{
				auto key = Trim (token.substr (0, equal));
				auto value = Trim (token.substr (equal + 1));
				if (!EqualsIgnoreCase (key, "default"))
				{
					error = "unknown assignment";
					return nullptr;
				}
				if (defaultSeen)
				{
					error = "duplicate default";
					return nullptr;
				}
				if (EqualsIgnoreCase (value, "allow"))
					parsed->m_DefaultAllow = true;
				else if (EqualsIgnoreCase (value, "deny"))
					parsed->m_DefaultAllow = false;
				else
				{
					error = "invalid default value";
					return nullptr;
				}
				defaultSeen = true;
				continue;
			}

			// `&lt;kind&gt; &lt;allow|deny&gt; &lt;value&gt;`
			size_t pos = 0;
			auto next = [&token, &pos] () -> std::string_view
			{
				while (pos < token.size () && IsSpace (token[pos])) ++pos;
				auto begin = pos;
				while (pos < token.size () && !IsSpace (token[pos])) ++pos;
				return token.substr (begin, pos - begin);
			};
			auto kind = next ();
			auto action = next ();
			auto value = Trim (token.substr (pos));

			if (parsed->m_Elements.size () >= MAX_ELEMENTS)
			{
				error = "too many elements";
				return nullptr;
			}
			Element element;
			if (EqualsIgnoreCase (kind, "explicit"))
				element.kind = ElementKind::eExplicit;
			else if (EqualsIgnoreCase (kind, "file"))
			{
				element.kind = ElementKind::eFile;
				if (++numFileElements > MAX_FILE_ELEMENTS)
				{
					error = "too many file elements";
					return nullptr;
				}
			}
			else if (EqualsIgnoreCase (kind, "record"))
				element.kind = ElementKind::eRecord;
			else
			{
				error = "unknown element";
				return nullptr;
			}

			if (EqualsIgnoreCase (action, "allow"))
				element.allow = true;
			else if (EqualsIgnoreCase (action, "deny"))
				element.allow = false;
			else
			{
				error = "invalid element action";
				return nullptr;
			}
			if (value.empty ())
			{
				error = "missing element value";
				return nullptr;
			}

			if (element.kind == ElementKind::eRecord)
			{
				uint64_t threshold = 0;
				if (value.size () > 9)
				{
					error = "invalid record threshold";
					return nullptr;
				}
				for (char ch : value)
				{
					if (ch < '0' || ch > '9')
					{
						error = "invalid record threshold";
						return nullptr;
					}
					threshold = threshold * 10 + static_cast<uint64_t> (ch - '0');
				}
				element.threshold
					= static_cast<uint32_t> (std::min<uint64_t> (threshold, std::numeric_limits<uint32_t>::max ()));
			}
			else if (element.kind == ElementKind::eFile)
			{
				if (value.size () > 4096)
				{
					error = "file element path too long";
					return nullptr;
				}
				if (!std::filesystem::path (std::string (value)).is_absolute ())
				{
					error = "file element path must be absolute";
					return nullptr;
				}
				element.path.assign (value);
			}
			else if (!AppendTarget (value, element, error))
				return nullptr;

			parsed->m_Elements.push_back (std::move (element));
		}

		return parsed;
	}

	TunnelAccessFilter::TunnelAccessFilter (std::shared_ptr<const FilterDefinition> definition)
		: m_Definition (definition ? std::move (definition) : FilterDefinition::AllowAll ())
	{
	}

	TunnelAccessFilter::Decision TunnelAccessFilter::Evaluate (const i2p::data::IdentHash & peer, uint64_t nowSeconds)
	{
		return EvaluateInternal (peer, nowSeconds, true);
	}

	TunnelAccessFilter::Decision TunnelAccessFilter::Peek (const i2p::data::IdentHash & peer, uint64_t nowSeconds) const
	{
		return EvaluateInternal (peer, nowSeconds, false);
	}

	TunnelAccessFilter::Decision TunnelAccessFilter::EvaluateInternal (
		const i2p::data::IdentHash & peer, uint64_t nowSeconds, bool consumeRecord) const
	{
		std::lock_guard<std::mutex> lock (m_Mutex);
		for (const auto & element : m_Definition->GetElements ())
		{
			bool matches = false;
			switch (element.kind)
			{
			case FilterDefinition::ElementKind::eExplicit:
				matches = MatchesEntries (element.destinations, element.masks, element.matchesAny, peer);
				break;
			case FilterDefinition::ElementKind::eFile:
			{
				const auto & cache = GetFileCacheLocked (element.path, nowSeconds);
				matches = MatchesEntries (cache.destinations, cache.masks, cache.matchesAny, peer);
				break;
			}
			case FilterDefinition::ElementKind::eRecord:
				// A `record` element decides while the peer is within the first
				// `threshold` connections it made. An unknown peer (no record
				// yet, Peek path) never matches.
				if (element.threshold)
				{
					const auto count
						= consumeRecord ? NextRecordCountLocked (peer, nowSeconds) : PeekRecordCountLocked (peer);
					matches = count != 0 && count <= element.threshold;
				}
				break;
			}
			if (matches) return Decision { element.allow, true };
		}
		return Decision { m_Definition->IsDefaultAllow (), false };
	}

	TunnelAccessFilter::FileCache & TunnelAccessFilter::GetFileCacheLocked (
		const std::string & path, uint64_t nowSeconds) const
	{
		auto it = m_Files.find (path);
		if (it == m_Files.end ()) it = m_Files.emplace (path, FileCache { }).first;
		auto & cache = it->second;

		// The stat rate is bounded; a changed mtime or size reloads the list.
		if (cache.loaded && m_FileCacheSeconds && nowSeconds < cache.lastCheck + m_FileCacheSeconds) return cache;
		cache.lastCheck = nowSeconds;

		std::error_code ec;
		const std::filesystem::path file (path);
		const auto size = std::filesystem::file_size (file, ec);
		if (ec)
		{
			if (cache.loaded)
				LogPrint (eLogWarning, "I2PTunnel: a filter file became unreadable; its entries are inactive");
			cache = FileCache { }; // inactive, fail closed for `file allow` lists
			cache.lastCheck = nowSeconds;
			return cache;
		}
		const auto writeTime = std::filesystem::last_write_time (file, ec);
		const uint64_t mtime = ec ? 0 : FileMilliseconds (writeTime);
		if (cache.loaded && cache.mtime == mtime && cache.size == size) return cache;

		FileCache reloaded;
		reloaded.lastCheck = nowSeconds;
		reloaded.mtime = mtime;
		reloaded.size = size;
		if (size > FilterDefinition::MAX_FILE_BYTES)
		{
			LogPrint (eLogWarning, "I2PTunnel: a filter file exceeds the size limit; its entries are inactive");
			cache = reloaded;
			return cache;
		}

		std::ifstream input (file, std::ifstream::binary);
		if (!input.is_open ())
		{
			LogPrint (eLogWarning, "I2PTunnel: a filter file could not be opened; its entries are inactive");
			cache = reloaded;
			return cache;
		}
		std::string line;
		size_t entries = 0;
		while (std::getline (input, line) && entries < FilterDefinition::MAX_FILE_ENTRIES)
		{
			auto trimmed = Trim (line);
			if (trimmed.empty () || trimmed.front () == '#') continue;
			std::string error;
			if (!ParseFilterTarget (trimmed, reloaded.destinations, reloaded.masks, reloaded.matchesAny, error))
			{
				++reloaded.rejectedLines;
				continue;
			}
			++entries;
		}
		if (reloaded.rejectedLines)
			LogPrint (eLogWarning, "I2PTunnel: a filter file contains ", reloaded.rejectedLines,
				" invalid entries that were skipped");
		reloaded.loaded = true;
		cache = std::move (reloaded);
		return cache;
	}

	uint32_t TunnelAccessFilter::NextRecordCountLocked (const i2p::data::IdentHash & peer, uint64_t nowSeconds) const
	{
		auto it = m_Records.find (peer);
		if (it == m_Records.end ())
		{
			if (m_Records.size () >= MAX_RECORDS)
			{
				// Displace expired entries first, then the oldest record. A
				// bounded table must not grow with the peer count.
				for (auto record = m_Records.begin (); record != m_Records.end ();)
				{
					if (nowSeconds > record->second.lastSeen + RECORD_TIMEOUT)
						record = m_Records.erase (record);
					else
						++record;
				}
				if (m_Records.size () >= MAX_RECORDS)
				{
					auto oldest = m_Records.begin ();
					for (auto record = m_Records.begin (); record != m_Records.end (); ++record)
						if (record->second.lastSeen < oldest->second.lastSeen) oldest = record;
					if (oldest != m_Records.end ()) m_Records.erase (oldest);
				}
			}
			it = m_Records.emplace (peer, RecordEntry { }).first;
		}
		if (it->second.count != std::numeric_limits<uint32_t>::max ()) ++it->second.count;
		it->second.lastSeen = nowSeconds;
		return it->second.count;
	}

	uint32_t TunnelAccessFilter::PeekRecordCountLocked (const i2p::data::IdentHash & peer) const
	{
		auto it = m_Records.find (peer);
		return it == m_Records.end () ? 0 : it->second.count;
	}

	void TunnelAccessFilter::ClearRecords ()
	{
		std::lock_guard<std::mutex> lock (m_Mutex);
		m_Records.clear ();
	}

	size_t TunnelAccessFilter::GetNumRecords () const
	{
		std::lock_guard<std::mutex> lock (m_Mutex);
		return m_Records.size ();
	}

	size_t TunnelAccessFilter::GetNumFileElements () const
	{
		size_t count = 0;
		for (const auto & element : m_Definition->GetElements ())
			if (element.kind == FilterDefinition::ElementKind::eFile) ++count;
		return count;
	}
}
}

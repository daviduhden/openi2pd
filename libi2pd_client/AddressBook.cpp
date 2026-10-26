/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: AddressBook.
 *
 * Loads hosts.txt, local addresses and HTTP subscriptions, resolves .i2p
 * names locally or through destination lookups, and caches identities plus
 * subscription ETags on disk. Host names are matched ASCII
 * case-insensitively, while b33 labels and base64 destinations keep their
 * exact spelling. A per-destination UDP resolver answers remote name queries
 * on the address resolver ports.
 *
 * Merge precedence: bulk/subscription input never replaces a name that already
 * had a destination when the merge started, so a subscription operator cannot
 * hijack a locally curated .i2p name. Duplicate lines inside one source still
 * follow last-wins (properties-style), and an explicit local add/replace is an
 * operator action that may re-point a name. Declined offers are counted in a
 * saturating in-memory counter and logged at warning level, bounded per merge;
 * no per-conflict file is written.
 *
 * Feed retraction: a subscription may ask for the removal of a name it created
 * with the Java advanced form "<name>=<destination>#!action=delete". The origin
 * of every feed-supplied entry is remembered in memory and in the bounded
 * entry-sources.txt sidecar, so the index keeps its legacy "name,destination" columns
 * and old readers stay compatible. A name without a recorded origin (locally
 * added, loaded from hosts.txt or restored from an index whose sidecar is
 * missing) is never deleted by a feed. Deletion is persistent: it records the
 * same tombstone as an administrative remove.
 *
 * Every persisted artifact (identity files, the address index, the hosts.txt
 * dump, the subscription ETags, deleted.txt and entry-sources.txt) is replaced
 * through i2p::fs::AtomicWrite/AtomicWritePrivate, which never opens the target
 * path for truncation and never follows a symlink placed at it.
 *
 */

#include <string.h>
#include <inttypes.h>
#include <algorithm>
#include <cctype>
#include <limits>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <filesystem>
#include <fstream>
#include <chrono>
#include <future>
#include <utility>
#include <openssl/rand.h>
#include <boost/algorithm/string.hpp>
#include "Base.h"
#include "Modern.h"
#include "Random.h"
#include "util.h"
#include "Timestamp.h"
#include "Identity.h"
#include "FS.h"
#include "Log.h"
#include "Config.h"
#include "HTTP.h"
#include "NetDb.hpp"
#include "ClientContext.h"
#include "BoostStream.h"
#include "AddressBook.h"

#include <filesystem>
namespace fs_lib = std::filesystem;

namespace i2p
{
namespace client
{
	namespace
	{
		constexpr size_t MAX_SUBSCRIPTION_BODY = 5 * 1024 * 1024;
		constexpr size_t MAX_SUBSCRIPTION_ENTRIES = 10000;
		// Conflict diagnostics are bounded: a hostile source may offer a
		// conflicting destination for every entry, and per-entry warnings would
		// flood the log.
		constexpr size_t MAX_CONFLICT_LOG_LINES = 8;
		// subscriptions.txt is a small operator file: cap the bytes and lines
		// read so a corrupt or hostile file cannot force a large allocation.
		constexpr size_t MAX_SUBSCRIPTION_FILE = 64 * 1024;
		constexpr size_t MAX_SUBSCRIPTION_FILE_LINES = 4096;
		// An operator-initiated add/remove should be fetched promptly, but the
		// timer belongs to the destination's io_context and must not be armed
		// with a zero delay from another thread.
		constexpr std::chrono::seconds SUBSCRIPTION_RELOAD_DELAY { 1 };
		// The advanced hosts.txt form appends "#!key=value,key=value" pairs. Only
		// a bounded number of short pairs is honoured; an over-long block is
		// ignored as comment text so a hostile source cannot spend parser time.
		constexpr size_t MAX_ACTION_PAIRS = 8;
		constexpr size_t MAX_ACTION_KEY_LENGTH = 24;
		constexpr size_t MAX_ACTION_VALUE_LENGTH = 128;
		constexpr size_t MAX_ACTION_BLOCK_LENGTH = 256;
		// entry-sources.txt is written by the book itself: one "name<TAB>origin" line
		// per entry that a subscription supplied. The bounds follow the merge
		// entry cap (10000 addresses) with room for the origin column.
		constexpr size_t MAX_ADDRESS_SOURCE_FILE = 1024 * 1024;
		constexpr size_t MAX_ADDRESS_SOURCE_ENTRIES = 20000;

		bool SchemeIs (std::string_view scheme, std::string_view expected)
		{
			if (scheme.size () != expected.size ()) return false;
			for (size_t i = 0; i < scheme.size (); i++)
			{
				char ch = scheme[i];
				if (ch >= 'A' && ch <= 'Z') ch = static_cast<char> (ch - 'A' + 'a');
				if (ch != expected[i]) return false;
			}
			return true;
		}

		// Diagnostic label for a subscription source: scheme, host and port
		// only. Paths and queries are operator-chosen and may carry credentials
		// or tokens, so they never reach a log line or an error string.
		std::string SubscriptionLabel (std::string_view url)
		{
			auto schemeEnd = url.find ("://");
			if (schemeEnd == std::string_view::npos) return "subscription";
			const auto authorityStart = schemeEnd + 3;
			auto authorityEnd = url.find_first_of ("/?#", authorityStart);
			if (authorityEnd == std::string_view::npos) authorityEnd = url.size ();
			return std::string (url.substr (0, authorityEnd));
		}

		bool EqualIgnoreCase (std::string_view left, std::string_view right)
		{
			if (left.size () != right.size ()) return false;
			for (size_t i = 0; i < left.size (); i++)
			{
				char a = left[i];
				char b = right[i];
				if (a >= 'A' && a <= 'Z') a = static_cast<char> (a - 'A' + 'a');
				if (b >= 'A' && b <= 'Z') b = static_cast<char> (b - 'A' + 'a');
				if (a != b) return false;
			}
			return true;
		}

		// A blinded (b33) name is decoded locally and is never resolved through
		// the address book, so a resolver miss for it must not be remembered.
		bool IsBlindedHostName (std::string_view normalized)
		{
			std::string_view host = normalized;
			if (HasSuffixIgnoreCase (host, ".i2p.alt")) host.remove_suffix (4);
			if (!HasSuffixIgnoreCase (host, ".i2p")) return false;
			host.remove_suffix (4);
			if (HasSuffixIgnoreCase (host, ".b33")) return true;
			auto dot = host.rfind ('.');
			auto label = dot == std::string_view::npos ? host : host.substr (dot + 1);
			return label.size () > B33_ADDRESS_THRESHOLD;
		}

		struct SubscriptionLineAction
		{
			bool deleteRequested = false;
		};

		// Parses the "#!..." tail of a hosts.txt line. Unknown keys, malformed
		// pairs and the "action=update" no-op are ignored; only a well-formed
		// "action=delete" has an effect. The result is false when the block
		// exceeds its bounds, in which case the whole block is ignored.
		bool ParseSubscriptionAction (std::string_view block, SubscriptionLineAction & action)
		{
			action = SubscriptionLineAction { };
			if (block.size () > MAX_ACTION_BLOCK_LENGTH) return false;
			size_t pairs = 0;
			size_t start = 0;
			while (start <= block.size ())
			{
				auto end = block.find_first_of (",;", start);
				if (end == std::string_view::npos) end = block.size ();
				auto pair = block.substr (start, end - start);
				start = end + 1;
				while (!pair.empty () && (pair.front () == ' ' || pair.front () == '\t')) pair.remove_prefix (1);
				while (!pair.empty () && (pair.back () == ' ' || pair.back () == '\t')) pair.remove_suffix (1);
				if (pair.empty ()) continue;
				if (++pairs > MAX_ACTION_PAIRS) return false;
				auto equals = pair.find ('=');
				if (equals == std::string_view::npos) continue;
				auto key = pair.substr (0, equals);
				auto value = pair.substr (equals + 1);
				if (key.size () > MAX_ACTION_KEY_LENGTH || value.size () > MAX_ACTION_VALUE_LENGTH) continue;
				if (EqualIgnoreCase (key, "action") && EqualIgnoreCase (value, "delete")) action.deleteRequested = true;
			}
			return true;
		}

		// One persisted "name<TAB>origin" line. Names and origins are host
		// names, so a tab (which neither can contain) is an unambiguous
		// separator.
		bool ParseAddressSourceLine (std::string_view line, std::string & name, std::string & origin)
		{
			auto tab = line.find ('\t');
			if (tab == std::string_view::npos) return false;
			auto left = line.substr (0, tab);
			auto right = line.substr (tab + 1);
			if (left.empty () || left.size () > 255 || right.empty () || right.size () > 255) return false;
			name = NormalizeHostName (left);
			origin.assign (right);
			return true;
		}

		// A persisted artifact is replaced by rename, so a regular file is the
		// only shape it can legitimately have. A pre-placed symlink (or any
		// other non-regular entry) is refused instead of being read through, so
		// an unrelated file can never be imported as address-book state.
		bool IsRegularArtifact (const std::string & path)
		{
			std::error_code error;
			const auto status = std::filesystem::symlink_status (path, error);
			if (error) return false;
			if (std::filesystem::is_regular_file (status)) return true;
			if (std::filesystem::exists (status))
				LogPrint (eLogWarning, "Addressbook: Ignoring non-regular file ", path);
			return false;
		}
	}

	bool HasSuffixIgnoreCase (std::string_view value, std::string_view suffix)
	{
		if (value.size () < suffix.size ()) return false;
		auto tail = value.substr (value.size () - suffix.size ());
		for (size_t i = 0; i < suffix.size (); i++)
		{
			char ch = tail[i];
			char expected = suffix[i];
			if (ch >= 'A' && ch <= 'Z') ch = static_cast<char> (ch - 'A' + 'a');
			if (expected >= 'A' && expected <= 'Z') expected = static_cast<char> (expected - 'A' + 'a');
			if (ch != expected) return false;
		}
		return true;
	}

	bool IsI2PHost (std::string_view host)
	{
		auto normalized = NormalizeHostName (host);
		if (!normalized.empty () && normalized.back () == '.') normalized.pop_back ();
		return HasSuffixIgnoreCase (normalized, ".i2p") || HasSuffixIgnoreCase (normalized, ".i2p.alt");
	}

	std::string NormalizeHostName (std::string_view name)
	{
		// A blinded destination label is URL-safe base64 and therefore
		// case-sensitive. It is either the final label of ".i2p" (the Address
		// parser treats a label longer than B33_ADDRESS_THRESHOLD this way) or
		// the label immediately before an explicit ".b33.i2p" suffix. Preserve
		// that label verbatim and fold the suffix and any subdomain prefix.
		constexpr std::string_view b33Suffix = ".b33.i2p";
		constexpr std::string_view b33AltSuffix = ".b33.i2p.alt";
		bool explicitB33 = HasSuffixIgnoreCase (name, b33AltSuffix) && name.size () > b33AltSuffix.size ();
		if (!explicitB33) explicitB33 = HasSuffixIgnoreCase (name, b33Suffix) && name.size () > b33Suffix.size ();
		size_t labelEnd = name.size ();
		if (explicitB33)
			labelEnd
				= name.size () - (HasSuffixIgnoreCase (name, b33AltSuffix) ? b33AltSuffix.size () : b33Suffix.size ());
		else if (HasSuffixIgnoreCase (name, ".i2p.alt"))
			labelEnd = name.size () - 8;
		else if (HasSuffixIgnoreCase (name, ".i2p"))
			labelEnd = name.size () - 4;

		auto dot = labelEnd ? name.rfind ('.', labelEnd - 1) : std::string_view::npos;
		size_t labelStart = dot == std::string_view::npos ? 0 : dot + 1;
		bool b33 = explicitB33 || (labelEnd > labelStart && labelEnd - labelStart > B33_ADDRESS_THRESHOLD);

		std::string normalized (name);
		for (size_t i = 0; i < normalized.size (); i++)
		{
			if (b33 && i >= labelStart && i < labelEnd) continue;
			auto ch = normalized[i];
			if (ch >= 'A' && ch <= 'Z') normalized[i] = static_cast<char> (ch - 'A' + 'a');
		}
		return normalized;
	}

	bool IsValidDestinationLength (std::string_view base64)
	{
		return base64.size () >= MIN_DESTINATION_BASE64_LENGTH && base64.size () <= MAX_DESTINATION_BASE64_LENGTH;
	}

	bool IsValidHostName (std::string_view name)
	{
		auto normalized = NormalizeHostName (name);
		if (normalized.size () < 5 || normalized.size () > 67) return false;
		if (!normalized.ends_with (".i2p")) return false;
		if (normalized.ends_with (".b32.i2p") || normalized.ends_with (".b33.i2p")) return false;
		auto core = std::string_view (normalized).substr (0, normalized.size () - 4);
		if (core.empty ()) return false;
		size_t labelStart = 0;
		while (true)
		{
			auto labelEnd = core.find ('.', labelStart);
			const bool finalLabel = labelEnd == std::string_view::npos;
			if (finalLabel) labelEnd = core.size ();
			auto label = core.substr (labelStart, labelEnd - labelStart);
			if (label.empty () || label.front () == '-' || label.back () == '-') return false;
			// IDNA reserves the doubled hyphen (the "xn--" prefix position) and no
			// I2P name needs one, so it is refused instead of letting a name
			// imitate a punycode prefix.
			if (label.find ("--") != std::string_view::npos) return false;
			// A final label of exactly the base32-hash length is the
			// "<hash>.i2p" lookalike of "<hash>.b32.i2p": a name like that would
			// never resolve as a destination and can only mislead.
			if (finalLabel)
			{
				if (label.size () == B32_HASH_LABEL_LENGTH) return false;
				break;
			}
			labelStart = labelEnd + 1;
		}
		for (char ch : core)
			if (!((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '.' || ch == '-')) return false;
		for (std::string_view reserved : { "proxy", "router", "console" })
		{
			if (core == reserved) return false;
			std::string suffix (".");
			suffix.append (reserved);
			if (core.ends_with (std::string_view (suffix))) return false;
		}
		return true;
	}

	bool IsValidSubscriptionURL (std::string_view url, std::string & error)
	{
		error.clear ();
		if (url.empty () || url.size () > MAX_SUBSCRIPTION_URL_LENGTH)
		{
			error = "subscription url length is out of range";
			return false;
		}
		// A source URL is one line of subscriptions.txt: whitespace or control
		// bytes would split it or inject a second source.
		for (char ch : url)
		{
			const auto byte = static_cast<unsigned char> (ch);
			if (byte <= 0x20 || byte >= 0x7f)
			{
				error = "subscription url contains whitespace or control characters";
				return false;
			}
		}
		auto schemeEnd = url.find ("://");
		if (schemeEnd == std::string_view::npos || schemeEnd == 0)
		{
			error = "subscription url must be absolute";
			return false;
		}
		auto scheme = url.substr (0, schemeEnd);
		if (!SchemeIs (scheme, "http") && !SchemeIs (scheme, "https"))
		{
			error = "subscription scheme must be http or https";
			return false;
		}
		const auto authorityStart = schemeEnd + 3;
		auto authorityEnd = url.find_first_of ("/?#", authorityStart);
		if (authorityEnd == std::string_view::npos) authorityEnd = url.size ();
		auto authority = url.substr (authorityStart, authorityEnd - authorityStart);
		if (authority.empty ())
		{
			error = "subscription url has no host";
			return false;
		}
		// User information would be sent to a peer-chosen destination and
		// echoed by diagnostics; refuse it instead of persisting a credential.
		if (authority.find ('@') != std::string_view::npos)
		{
			error = "subscription url must not contain user information";
			return false;
		}
		if (url.find ('#') != std::string_view::npos)
		{
			error = "subscription url must not contain a fragment";
			return false;
		}
		auto host = authority;
		auto colon = authority.rfind (':');
		if (colon != std::string_view::npos)
		{
			host = authority.substr (0, colon);
			auto port = authority.substr (colon + 1);
			unsigned value = 0;
			bool valid = !port.empty () && port.size () <= 5;
			for (char ch : port)
			{
				if (ch < '0' || ch > '9')
				{
					valid = false;
					break;
				}
				value = value * 10 + static_cast<unsigned> (ch - '0');
			}
			if (!valid || value == 0 || value > 65535)
			{
				error = "subscription url has an invalid port";
				return false;
			}
		}
		if (host.empty () || host.find ('[') != std::string_view::npos || host.find (']') != std::string_view::npos
			|| host.back () == '.')
		{
			error = "subscription url has an invalid host";
			return false;
		}
		// The fetch authority is the I2P stream client: a clearnet host would
		// fail later or turn into a cleartext request, so it is refused here.
		if (!IsI2PHost (host))
		{
			error = "subscription host must be an .i2p destination";
			return false;
		}
		return true;
	}

	bool AddressBook::IsValidDeletedPattern (std::string_view pattern)
	{
		if (pattern.size () < 5 || pattern.size () > 255 || !pattern.ends_with (".i2p")) return false;
		if (pattern.front () == '.' || pattern.front () == '-') return false;
		if (pattern.find ("..") != std::string_view::npos || pattern.find (".-") != std::string_view::npos
			|| pattern.find ("-.") != std::string_view::npos)
			return false;
		for (char ch : pattern)
		{
			if ((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '.' || ch == '-' || ch == '*' || ch == '_'
				|| ch == '~' || (ch >= 'A' && ch <= 'Z'))
				continue;
			return false;
		}
		return true;
	}

	bool AddressBook::MatchesDeletedPattern (std::string_view pattern, std::string_view name)
	{
		// Anchored glob with a single backtrack point per '*'. The pattern is
		// already bounded at 255 bytes and name at the address-book limit.
		size_t p = 0, n = 0, star = std::string_view::npos, resume = 0;
		while (n < name.size ())
		{
			if (p < pattern.size () && pattern[p] == name[n])
			{
				++p;
				++n;
			}
			else if (p < pattern.size () && pattern[p] == '*')
			{
				star = p++;
				resume = n;
			}
			else if (star != std::string_view::npos)
			{
				p = star + 1;
				n = ++resume;
			}
			else
				return false;
		}
		while (p < pattern.size () && pattern[p] == '*') ++p;
		return p == pattern.size ();
	}

	bool AddressBook::IsDeletedHostLocked (std::string_view name) const
	{
		for (const auto & pattern : m_DeletedHosts)
			if (MatchesDeletedPattern (pattern, name)) return true;
		return false;
	}

	bool AddressBook::RemoveDeletedMatchesLocked (std::string_view name)
	{
		const auto before = m_DeletedHosts.size ();
		std::erase_if (
			m_DeletedHosts, [name] (const std::string & pattern) { return MatchesDeletedPattern (pattern, name); });
		return before != m_DeletedHosts.size ();
	}

	bool AddressBook::SaveDeletedHostsLocked ()
	{
		return m_Storage && m_Storage->SaveDeletedHosts (m_DeletedHosts);
	}

	bool AddressBook::IsFeedOriginLocked (std::string_view name, std::string_view source) const
	{
		if (source.empty ()) return false;
		auto it = m_AddressSources.find (name);
		if (it == m_AddressSources.end ()) return false;
		return EqualIgnoreCase (it->second, source);
	}

	bool AddressBook::SaveAddressSourcesLocked ()
	{
		if (!m_Storage) return false;
		// Only origins of names that still exist are persisted, so a removed
		// entry cannot leave a stale sidecar line behind.
		std::map<std::string, std::string, std::less<>> current;
		for (const auto & [name, origin] : m_AddressSources)
			if (m_Addresses.find (name) != m_Addresses.end ()) current.emplace (name, origin);
		return m_Storage->SaveAddressSources (current);
	}

	void AddressBook::ForgetAddressSourceLocked (std::string_view name)
	{
		auto it = m_AddressSources.find (name);
		if (it == m_AddressSources.end ()) return;
		m_AddressSources.erase (it);
		LogPrint (eLogDebug, "Addressbook: Forgot the subscription origin of ", name);
		// Persisting here keeps a later feed from being able to retract a name
		// after an administrative add took it over.
		if (m_Storage && !SaveAddressSourcesLocked ())
			LogPrint (eLogWarning, "Addressbook: Could not persist entry origins");
	}

	bool AddressBook::ApplyFeedDeleteLocked (std::string_view key, std::string_view name)
	{
		auto it = m_Addresses.find (key);
		if (it == m_Addresses.end ()) return false;
		auto previous = it->second;
		m_Addresses.erase (it);
		if (m_Storage && m_Storage->Save (m_Addresses) < 0)
		{
			m_Addresses[std::string (key)] = previous;
			LogPrint (eLogWarning, "Addressbook: Could not persist the retraction of ", name);
			return false;
		}
		// The retraction is persistent: the same tombstone an administrative
		// remove records keeps another feed from resurrecting the name. A full
		// tombstone list only means the record is not kept; the removal still
		// applies.
		if (!IsDeletedHostLocked (key) && m_DeletedHosts.size () < 256)
		{
			m_DeletedHosts.push_back (std::string (key));
			if (!SaveDeletedHostsLocked ()) m_DeletedHosts.pop_back ();
		}
		ForgetAddressSourceLocked (key);
		m_NegativeCache.Clear ();
		if (previous && previous->IsIdentHash () && m_Storage)
		{
			bool referenced = false;
			for (const auto & [unused, address] : m_Addresses)
				if (address && address->IsIdentHash () && address->identHash == previous->identHash)
				{
					referenced = true;
					break;
				}
			if (!referenced) m_Storage->RemoveAddress (previous->identHash);
		}
		LogPrint (eLogInfo, "Addressbook: Subscription retracted ", name);
		return true;
	}

	void AddressBook::LoadDeletedHosts ()
	{
		if (!m_Storage) return;
		std::vector<std::string> loaded;
		if (!m_Storage->LoadDeletedHosts (loaded)) return;
		std::lock_guard<std::mutex> l (m_AddressBookMutex);
		m_DeletedHosts.clear ();
		for (const auto & raw : loaded)
		{
			auto pattern = NormalizeHostName (raw);
			if (!IsValidDeletedPattern (pattern)) continue;
			if (std::find (m_DeletedHosts.begin (), m_DeletedHosts.end (), pattern) == m_DeletedHosts.end ())
				m_DeletedHosts.push_back (std::move (pattern));
			if (m_DeletedHosts.size () >= 256) break;
		}
		if (!m_DeletedHosts.empty ())
			LogPrint (eLogInfo, "Addressbook: ", m_DeletedHosts.size (), " deleted-host patterns loaded");
	}

	void AddressBook::LoadAddressSources ()
	{
		if (!m_Storage) return;
		std::map<std::string, std::string, std::less<>> loaded;
		if (!m_Storage->LoadAddressSources (loaded)) return;
		std::lock_guard<std::mutex> l (m_AddressBookMutex);
		m_AddressSources.clear ();
		size_t stale = 0;
		for (auto & [name, origin] : loaded)
		{
			if (m_Addresses.find (name) == m_Addresses.end ())
			{
				stale++;
				continue;
			}
			m_AddressSources.emplace (name, std::move (origin));
		}
		if (!m_AddressSources.empty ())
			LogPrint (eLogInfo, "Addressbook: ", m_AddressSources.size (), " subscription origins loaded");
		if (stale) LogPrint (eLogInfo, "Addressbook: ", stale, " stale subscription origins ignored");
	}

	void detail::AddressLookupRequests::Prune (uint64_t now)
	{
		std::erase_if (m_Requests,
			[now] (const auto & entry)
			{
				auto created = entry.second.Created;
				return now < created || now - created >= TIMEOUT_SECONDS;
			});
	}

	bool detail::AddressLookupRequests::Insert (
		uint32_t nonce, std::string_view name, const i2p::data::IdentHash & resolver, uint64_t now)
	{
		if (name.empty () || name.size () > 255) return false;
		auto key = NormalizeHostName (name);
		std::lock_guard<std::mutex> l (m_Mutex);
		Prune (now);
		if (m_Requests.size () >= MAX_PENDING) return false;
		for (const auto & [unused, request] : m_Requests)
			if (request.Name == key && request.Resolver == resolver) return false;
		return m_Requests.emplace (nonce, Request { std::move (key), resolver, now }).second;
	}

	std::optional<std::string> detail::AddressLookupRequests::Take (
		uint32_t nonce, const i2p::data::IdentHash & resolver, uint64_t now)
	{
		std::lock_guard<std::mutex> l (m_Mutex);
		Prune (now);
		auto it = m_Requests.find (nonce);
		// An unrelated authenticated sender must not consume the real request.
		if (it == m_Requests.end () || it->second.Resolver != resolver) return std::nullopt;
		auto name = std::move (it->second.Name);
		m_Requests.erase (it);
		return name;
	}

	void detail::AddressLookupRequests::Clear ()
	{
		std::lock_guard<std::mutex> l (m_Mutex);
		m_Requests.clear ();
	}

	void detail::NegativeAddressCache::Prune (uint64_t now) const
	{
		std::erase_if (m_Entries,
			[now] (const auto & entry)
			{
				const auto expires = entry.second;
				// An entry that lies further in the future than one lifetime can
				// only come from a monotonic-clock reset: drop it as well.
				return now >= expires || expires > now + TIMEOUT_SECONDS;
			});
	}

	bool detail::NegativeAddressCache::IsCacheable (std::string_view normalized)
	{
		if (normalized.empty () || normalized.size () > 255) return false;
		return !IsBlindedHostName (normalized);
	}

	bool detail::NegativeAddressCache::IsCached (std::string_view name, uint64_t now) const
	{
		auto key = NormalizeHostName (name);
		if (!IsCacheable (key)) return false;
		std::lock_guard<std::mutex> l (m_Mutex);
		Prune (now);
		auto it = m_Entries.find (key);
		return it != m_Entries.end () && now < it->second;
	}

	void detail::NegativeAddressCache::Insert (std::string_view name, uint64_t now)
	{
		auto key = NormalizeHostName (name);
		// A b33 label is decoded locally, so caching its "miss" would suppress a
		// later valid decode for a whole lifetime.
		if (!IsCacheable (key)) return;
		std::lock_guard<std::mutex> l (m_Mutex);
		Prune (now);
		if (m_Entries.find (key) == m_Entries.end () && m_Entries.size () >= MAX_ENTRIES)
		{
			// Evict the entry that expires first: the table stays bounded and a
			// burst of misses cannot pin the cache to the oldest names.
			auto oldest = m_Entries.begin ();
			for (auto it = m_Entries.begin (); it != m_Entries.end (); ++it)
				if (it->second < oldest->second) oldest = it;
			if (oldest != m_Entries.end ()) m_Entries.erase (oldest);
		}
		m_Entries[key] = now + TIMEOUT_SECONDS;
	}

	void detail::NegativeAddressCache::Erase (std::string_view name)
	{
		auto key = NormalizeHostName (name);
		std::lock_guard<std::mutex> l (m_Mutex);
		m_Entries.erase (key);
	}

	void detail::NegativeAddressCache::Clear ()
	{
		std::lock_guard<std::mutex> l (m_Mutex);
		m_Entries.clear ();
	}

	size_t detail::NegativeAddressCache::Size (uint64_t now) const
	{
		std::lock_guard<std::mutex> l (m_Mutex);
		Prune (now);
		return m_Entries.size ();
	}

	// TODO: this is actually proxy class
	class AddressBookFilesystemStorage : public AddressBookStorage
	{
	public:
		AddressBookFilesystemStorage ()
			: storage ("addressbook", "b", "", "b32")
		{
			i2p::config::GetOption ("persist.addressbook", m_IsPersist);
			if (m_IsPersist) i2p::config::GetOption ("addressbook.hostsfile", m_HostsFile);
		}
		std::shared_ptr<const i2p::data::IdentityEx> GetAddress (const i2p::data::IdentHash & ident) override;
		void AddAddress (std::shared_ptr<const i2p::data::IdentityEx> address) override;
		void RemoveAddress (const i2p::data::IdentHash & ident) override;
		void CleanUpCache () override;

		bool Init () override;
		int Load (Addresses & addresses) override;
		int LoadLocal (Addresses & addresses) override;
		int Save (const Addresses & addresses) override;

		void SaveEtag (const i2p::data::IdentHash & subscription, const std::string & etag,
			const std::string & lastModified) override;
		bool GetEtag (
			const i2p::data::IdentHash & subscription, std::string & etag, std::string & lastModified) override;
		void ResetEtags () override;
		bool LoadDeletedHosts (std::vector<std::string> & patterns) override;
		bool SaveDeletedHosts (const std::vector<std::string> & patterns) override;
		bool LoadAddressSources (std::map<std::string, std::string, std::less<>> & sources) override;
		bool SaveAddressSources (const std::map<std::string, std::string, std::less<>> & sources) override;

	private:
		int LoadFromFile (const std::string & filename,
			Addresses & addresses); // returns -1 if can't open file, otherwise number of records

	private:
		i2p::fs::HashedStorage storage;
		std::string etagsPath, indexPath, localPath, deletedPath, sourcesPath;
		bool m_IsPersist = false;
		std::string m_HostsFile; // file to dump hosts.txt, empty if not used
		std::unordered_map<i2p::data::IdentHash, std::pair<std::vector<uint8_t>, uint64_t>>
			m_FullAddressCache; // ident hash -> (full ident buffer, last access timestamp)
		std::mutex m_FullAddressCacheMutex;
	};

	bool AddressBookFilesystemStorage::Init ()
	{
		storage.SetPlace (i2p::fs::GetDataDir ());
		// init storage
		if (storage.Init (i2p::data::GetBase32SubstitutionTable (), 32))
		{
			// init ETags
			etagsPath = i2p::fs::StorageRootPath (storage, "etags");
			if (!i2p::fs::Exists (etagsPath)) i2p::fs::CreateDirectory (etagsPath);
			// init address files
			indexPath = i2p::fs::StorageRootPath (storage, "addresses.csv");
			localPath = i2p::fs::StorageRootPath (storage, "local.csv");
			deletedPath = i2p::fs::StorageRootPath (storage, "deleted.txt");
			sourcesPath = i2p::fs::StorageRootPath (storage, "entry-sources.txt");
			return true;
		}
		return false;
	}

	std::shared_ptr<const i2p::data::IdentityEx> AddressBookFilesystemStorage::GetAddress (
		const i2p::data::IdentHash & ident)
	{
		auto ts = i2p::util::GetMonotonicSeconds ();
		{
			std::lock_guard<std::mutex> l (m_FullAddressCacheMutex);
			auto it = m_FullAddressCache.find (ident);
			if (it != m_FullAddressCache.end ())
			{
				it->second.second = ts;
				return std::make_shared<i2p::data::IdentityEx> (it->second.first.data (), it->second.first.size ());
			}
		}

		if (!m_IsPersist)
		{
			LogPrint (eLogDebug, "Addressbook: Persistence is disabled");
			return nullptr;
		}
		std::string filename = storage.Path (ident.ToBase32 ());
		if (!IsRegularArtifact (filename))
		{
			LogPrint (eLogDebug, "Addressbook: Requested, but not found: ", filename);
			return nullptr;
		}
		std::ifstream f (filename, std::ifstream::binary);
		if (!f.is_open ())
		{
			LogPrint (eLogDebug, "Addressbook: Requested, but not found: ", filename);
			return nullptr;
		}

		f.seekg (0, std::ios::end);
		const auto end = f.tellg ();
		if (end == std::streampos (-1))
		{
			LogPrint (eLogError, "Addressbook: File ", filename, " is unreadable");
			return nullptr;
		}
		const auto length = static_cast<size_t> (static_cast<std::streamoff> (end));
		if (length < i2p::data::DEFAULT_IDENTITY_SIZE)
		{
			LogPrint (eLogError, "Addressbook: File ", filename, " is too short: ", length);
			return nullptr;
		}
		if (length > 64 * 1024)
		{
			LogPrint (eLogError, "Addressbook: File ", filename, " is too large: ", length);
			return nullptr;
		}
		f.seekg (0, std::ios::beg);
		std::vector<uint8_t> buf (length);
		f.read (reinterpret_cast<char *> (buf.data ()), static_cast<std::streamsize> (length));
		if (!f)
		{
			LogPrint (eLogError, "Addressbook: Couldn't read ", filename);
			return nullptr;
		}
		auto address = std::make_shared<i2p::data::IdentityEx> (buf.data (), length);
		// The file name is the base32 of this identity hash, so content that
		// belongs to another identity is refused instead of being handed out as
		// the key material of the requested hash.
		if (address->GetFullLen () != length || address->GetIdentHash () != ident)
		{
			LogPrint (eLogError, "Addressbook: Identity file does not match its name: ", filename);
			return nullptr;
		}
		{
			std::lock_guard<std::mutex> l (m_FullAddressCacheMutex);
			m_FullAddressCache.try_emplace (ident, buf, ts);
		}
		return address;
	}

	void AddressBookFilesystemStorage::AddAddress (std::shared_ptr<const i2p::data::IdentityEx> address)
	{
		if (!address) return;
		size_t len = address->GetFullLen ();
		std::vector<uint8_t> buf;
		if (!len) return; // invalid address
		{
			std::lock_guard<std::mutex> l (m_FullAddressCacheMutex);
			auto [it, inserted]
				= m_FullAddressCache.try_emplace (address->GetIdentHash (), len, i2p::util::GetMonotonicSeconds ());
			if (inserted) address->ToBuffer (it->second.first.data (), len);
			if (m_IsPersist) buf = it->second.first;
		}
		if (m_IsPersist && !buf.empty ())
		{
			std::string path = storage.Path (address->GetIdentHash ().ToBase32 ());
			// A same-directory atomic replacement never opens the target path for
			// truncation and never follows a symlink placed at it. The identity is
			// a per-destination file, so it is written with private permissions.
			if (!i2p::fs::AtomicWritePrivate (
					path, std::string_view (reinterpret_cast<const char *> (buf.data ()), len)))
				LogPrint (eLogError, "Addressbook: Could not write identity file ", path);
		}
	}

	void AddressBookFilesystemStorage::RemoveAddress (const i2p::data::IdentHash & ident)
	{
		{
			std::lock_guard<std::mutex> l (m_FullAddressCacheMutex);
			m_FullAddressCache.erase (ident);
		}
		if (!m_IsPersist) return;
		storage.Remove (ident.ToBase32 ());
	}

	int AddressBookFilesystemStorage::LoadFromFile (const std::string & filename, Addresses & addresses)
	{
		int num = 0;
		if (!IsRegularArtifact (filename)) return -1;
		std::ifstream f (filename, std::ifstream::in); // in text mode
		if (!f) return -1;

		addresses.clear ();
		std::string s;
		while (std::getline (f, s))
		{
			if (s.empty ()) continue; // skip empty line

			std::size_t pos = s.find (',');
			if (pos != std::string::npos)
			{
				std::string name = s.substr (0, pos++);
				std::string addr = s.substr (pos);

				addresses[NormalizeHostName (name)] = std::make_shared<Address> (addr);
				num++;
			}
		}
		if (f.bad ())
		{
			LogPrint (eLogError, "Addressbook: I/O error while reading ", filename);
			return -1;
		}
		return num;
	}

	int AddressBookFilesystemStorage::Load (Addresses & addresses)
	{
		int num = LoadFromFile (indexPath, addresses);
		if (num < 0)
		{
			LogPrint (eLogWarning, "Addressbook: Can't open ", indexPath);
			return -1;
		}
		LogPrint (eLogInfo, "Addressbook: Using index file ", indexPath);
		LogPrint (eLogInfo, "Addressbook: ", num, " addresses loaded from storage");

		return num;
	}

	int AddressBookFilesystemStorage::LoadLocal (Addresses & addresses)
	{
		int num = LoadFromFile (localPath, addresses);
		if (num < 0) return 0;
		LogPrint (eLogInfo, "Addressbook: ", num, " local addresses loaded");
		return num;
	}

	int AddressBookFilesystemStorage::Save (const Addresses & addresses)
	{
		int num = 0;
		// Build the complete new index in memory first: a partial write must
		// never reach the live file, and the replacement below is atomic.
		std::string index;
		for (const auto & it : addresses)
		{
			if (it.second->IsValid ())
			{
				index.append (it.first);
				index.push_back (',');
				if (it.second->IsIdentHash ())
					index.append (it.second->identHash.ToBase32 ());
				else
					index.append (it.second->blindedPublicKey->ToB33 ());
				index.push_back ('\n');
				num++;
			}
			else
				LogPrint (eLogWarning, "Addressbook: Invalid address ", it.first);
		}

		if (!m_HostsFile.empty ())
		{
			// dump full hosts.txt
			std::string hosts;
			for (const auto & it : addresses)
			{
				std::shared_ptr<const i2p::data::IdentityEx> addr;
				if (it.second->IsIdentHash ())
				{
					addr = GetAddress (it.second->identHash);
					if (addr)
					{
						hosts.append (it.first);
						hosts.push_back ('=');
						hosts.append (addr->ToBase64 ());
						hosts.push_back ('\n');
					}
				}
			}
			if (!i2p::fs::AtomicWrite (m_HostsFile, hosts))
			{
				LogPrint (eLogWarning, "Addressbook: Could not write ", m_HostsFile);
				return -1;
			}
		}

		if (!i2p::fs::AtomicWrite (indexPath, index))
		{
			LogPrint (eLogWarning, "Addressbook: Could not replace ", indexPath);
			return -1;
		}
		LogPrint (eLogInfo, "Addressbook: ", num, " addresses saved");

		return num;
	}

	void AddressBookFilesystemStorage::SaveEtag (
		const i2p::data::IdentHash & subscription, const std::string & etag, const std::string & lastModified)
	{
		std::string fname = etagsPath + i2p::fs::dirSep + subscription.ToBase32 () + ".txt";
		std::string contents (etag);
		contents.push_back ('\n');
		contents.append (lastModified);
		contents.push_back ('\n');
		if (!i2p::fs::AtomicWrite (fname, contents)) LogPrint (eLogError, "Addressbook: Could not write ", fname);
	}

	bool AddressBookFilesystemStorage::GetEtag (
		const i2p::data::IdentHash & subscription, std::string & etag, std::string & lastModified)
	{
		std::string fname = etagsPath + i2p::fs::dirSep + subscription.ToBase32 () + ".txt";
		if (!IsRegularArtifact (fname)) return false;
		std::ifstream f (fname, std::ifstream::in);
		if (!f || f.eof ()) return false;
		std::getline (f, etag);
		if (f.eof ()) return false;
		std::getline (f, lastModified);
		return true;
	}

	void AddressBookFilesystemStorage::ResetEtags ()
	{
		LogPrint (eLogError, "Addressbook: Resetting eTags");
		for (fs_lib::directory_iterator it (etagsPath); it != fs_lib::directory_iterator (); ++it)
		{
			if (!fs_lib::is_regular_file (it->status ())) continue;
			fs_lib::remove (it->path ());
		}
	}

	bool AddressBookFilesystemStorage::LoadDeletedHosts (std::vector<std::string> & patterns)
	{
		patterns.clear ();
		if (!IsRegularArtifact (deletedPath)) return false;
		std::error_code sizeError;
		const auto fileSize = std::filesystem::file_size (deletedPath, sizeError);
		if (!sizeError && fileSize > 64 * 1024)
		{
			LogPrint (eLogWarning, "Addressbook: ", deletedPath, " exceeds 64 KiB; ignored");
			return false;
		}
		std::ifstream f (deletedPath, std::ifstream::in);
		if (!f.is_open ()) return false;
		std::string line;
		size_t lines = 0;
		while (std::getline (f, line) && lines++ < 4096)
		{
			if (!line.empty () && line.back () == '\r') line.pop_back ();
			auto first = line.find_first_not_of (" \t");
			if (first == std::string::npos || line[first] == '#') continue;
			auto last = line.find_last_not_of (" \t");
			line = line.substr (first, last - first + 1);
			if (line.empty () || line.size () > 255) continue;
			if (std::find (patterns.begin (), patterns.end (), line) == patterns.end ()) patterns.push_back (line);
			if (patterns.size () >= 256) break;
		}
		if (f.bad ())
		{
			LogPrint (eLogWarning, "Addressbook: I/O error while reading ", deletedPath);
			return false;
		}
		return true;
	}

	bool AddressBookFilesystemStorage::SaveDeletedHosts (const std::vector<std::string> & patterns)
	{
		std::string contents ("# Deleted address-book hosts; one name or wildcard pattern per line.\n");
		size_t count = 0;
		for (const auto & pattern : patterns)
		{
			if (count++ >= 256 || pattern.empty () || pattern.size () > 255) continue;
			if (contents.size () + pattern.size () + 1 > 64 * 1024 - 128) break;
			contents.append (pattern);
			contents.push_back ('\n');
		}
		if (!i2p::fs::AtomicWrite (deletedPath, contents))
		{
			LogPrint (eLogWarning, "Addressbook: Could not replace ", deletedPath);
			return false;
		}
		return true;
	}

	bool AddressBookFilesystemStorage::LoadAddressSources (std::map<std::string, std::string, std::less<>> & sources)
	{
		sources.clear ();
		if (!IsRegularArtifact (sourcesPath)) return false;
		std::error_code sizeError;
		const auto fileSize = std::filesystem::file_size (sourcesPath, sizeError);
		if (!sizeError && fileSize > MAX_ADDRESS_SOURCE_FILE)
		{
			LogPrint (eLogWarning, "Addressbook: ", sourcesPath, " exceeds its size bound; entry origins ignored");
			return false;
		}
		std::ifstream f (sourcesPath, std::ifstream::in);
		if (!f.is_open ()) return false;
		std::string line;
		size_t lines = 0;
		while (std::getline (f, line) && lines++ < MAX_ADDRESS_SOURCE_ENTRIES)
		{
			if (!line.empty () && line.back () == '\r') line.pop_back ();
			if (line.empty () || line[0] == '#') continue;
			std::string name, origin;
			if (!ParseAddressSourceLine (line, name, origin)) continue;
			if (!IsValidHostName (name)) continue;
			// The origin is a subscription host label, never a URL or a
			// credential: refuse anything that is not a plain host name.
			bool printable = true;
			for (char ch : origin)
				if (static_cast<unsigned char> (ch) <= 0x20 || static_cast<unsigned char> (ch) >= 0x7f)
				{
					printable = false;
					break;
				}
			if (!printable) continue;
			sources[name] = std::move (origin);
		}
		if (f.bad ())
		{
			LogPrint (eLogWarning, "Addressbook: I/O error while reading ", sourcesPath);
			return false;
		}
		return true;
	}

	bool AddressBookFilesystemStorage::SaveAddressSources (
		const std::map<std::string, std::string, std::less<>> & sources)
	{
		std::string contents ("# Origin of address-book entries supplied by a subscription; name<TAB>origin.\n");
		size_t written = 0;
		size_t truncated = 0;
		for (const auto & [name, origin] : sources)
		{
			if (written >= MAX_ADDRESS_SOURCE_ENTRIES || name.empty () || name.size () > 255 || origin.empty ()
				|| origin.size () > 255)
				continue;
			if (contents.size () + name.size () + origin.size () + 2 > MAX_ADDRESS_SOURCE_FILE)
			{
				truncated++;
				continue;
			}
			contents.append (name);
			contents.push_back ('\t');
			contents.append (origin);
			contents.push_back ('\n');
			written++;
		}
		if (truncated)
			LogPrint (eLogWarning, "Addressbook: ", truncated,
				" entry origins exceed the entry-sources.txt bound; those names are treated as locally added");
		if (!i2p::fs::AtomicWrite (sourcesPath, contents))
		{
			LogPrint (eLogWarning, "Addressbook: Could not replace ", sourcesPath);
			return false;
		}
		return true;
	}

	void AddressBookFilesystemStorage::CleanUpCache ()
	{
		auto ts = i2p::util::GetMonotonicSeconds ();
		std::lock_guard<std::mutex> l (m_FullAddressCacheMutex);
		for (auto it = m_FullAddressCache.begin (); it != m_FullAddressCache.end ();)
		{
			if (ts > it->second.second + ADDRESS_CACHE_EXPIRATION_TIMEOUT)
				it = m_FullAddressCache.erase (it);
			else
				it++;
		}
	}

	//---------------------------------------------------------------------

	Address::Address (std::string_view b32)
		: addressType (eAddressInvalid)
	{
		if (b32.length () <= B33_ADDRESS_THRESHOLD)
		{
			if (identHash.FromBase32 (b32) > 0) addressType = eAddressIndentHash;
		}
		else
		{
			blindedPublicKey = std::make_shared<i2p::data::BlindedPublicKey> (b32);
			if (blindedPublicKey->IsValid ()) addressType = eAddressBlindedPublicKey;
		}
	}

	Address::Address (const i2p::data::IdentHash & hash)
	{
		addressType = eAddressIndentHash;
		identHash = hash;
	}

	std::string Address::ToBase32 () const
	{
		switch (addressType)
		{
		case eAddressIndentHash: return identHash.ToBase32 (); break;
		case eAddressBlindedPublicKey:
			if (blindedPublicKey) return blindedPublicKey->ToB33 ();
			break;
		default:;
		}
		return "";
	}

	bool Address::operator== (const Address & other) const
	{
		if (addressType != other.addressType) return false;
		switch (addressType)
		{
		case eAddressIndentHash: return identHash == other.identHash; break;
		case eAddressBlindedPublicKey:
			if (blindedPublicKey && other.blindedPublicKey
				&& blindedPublicKey->GetPublicKeyLen () == other.blindedPublicKey->GetPublicKeyLen ()
				&& blindedPublicKey->GetSigType () == other.blindedPublicKey->GetSigType ()
				&& blindedPublicKey->GetBlindedSigType () == other.blindedPublicKey->GetBlindedSigType ())
				return !memcmp (blindedPublicKey->GetPublicKey (), other.blindedPublicKey->GetPublicKey (),
					blindedPublicKey->GetPublicKeyLen ());
			break;
		default:;
		}
		return false;
	}

	AddressBook::AddressBook (std::unique_ptr<AddressBookStorage> storage)
		: m_Storage (std::move (storage))
		, m_IsLoaded (false)
		, m_NumRetries (0)
		, m_DefaultSubscription (nullptr)
		, m_ReloadAlive (std::make_shared<std::atomic<bool>> (false))
		, m_MergeConflicts (0)
		, m_SubscriptionsUpdateTimer (nullptr)
		, m_IsEnabled (true)
	{
	}

	AddressBook::~AddressBook ()
	{
		Stop ();
	}

	void AddressBook::Start ()
	{
		i2p::config::GetOption ("addressbook.enabled", m_IsEnabled);
		if (m_IsEnabled)
		{
			{
				std::lock_guard<std::mutex> l (m_SubscriptionsMutex);
				m_ReloadAlive = std::make_shared<std::atomic<bool>> (true);
			}
			if (!m_Storage) m_Storage = std::make_unique<AddressBookFilesystemStorage> ();
			m_Storage->Init ();
			LoadDeletedHosts ();
			LoadHosts (); /* try storage, then hosts.txt, then download */
			StartSubscriptions ();
			StartLookups ();
			ScheduleCacheUpdate ();
		}
	}

	void AddressBook::StartResolvers ()
	{
		LoadLocal ();
	}

	void AddressBook::Stop ()
	{
		// Reject queued subscription re-arms before the timer and the source
		// objects are released; a reload posted by the administration thread
		// must not touch stopped state. The client context is stopped while the
		// router io_context may still run, so the same lock also serializes
		// this teardown against RearmSubscriptionsTimer().
		{
			std::lock_guard<std::mutex> l (m_SubscriptionsMutex);
			if (m_ReloadAlive) m_ReloadAlive->store (false, std::memory_order_release);
			m_ReloadAlive.reset ();
			if (m_SubscriptionsUpdateTimer)
			{
				m_SubscriptionsUpdateTimer->cancel ();
				m_SubscriptionsUpdateTimer = nullptr;
			}
		}
		StopLookups ();
		StopSubscriptions ();
		if (m_AddressCacheUpdateTimer)
		{
			m_AddressCacheUpdateTimer->cancel ();
			m_AddressCacheUpdateTimer = nullptr;
		}
		bool isDownloading = m_Downloading.valid ();
		if (isDownloading)
		{
			if (m_Downloading.wait_for (std::chrono::seconds (0)) == std::future_status::ready)
				isDownloading = false;
			else
			{
				LogPrint (eLogInfo, "Addressbook: Subscriptions are downloading, abort");
				for (int i = 0; i < 30; i++)
				{
					if (m_Downloading.wait_for (std::chrono::seconds (1))
						== std::future_status::ready) // wait for 1 seconds
					{
						isDownloading = false;
						LogPrint (eLogInfo, "Addressbook: Subscriptions download complete");
						break;
					}
				}
			}
			if (!isDownloading)
				m_Downloading.get ();
			else
				LogPrint (eLogError, "Addressbook: Subscription download timeout");
		}
		if (m_Storage)
		{
			// Do not materialise an empty index when no index existed at
			// startup. Otherwise a clean shutdown would make an empty
			// addresses.csv authoritative and permanently mask hosts.txt.
			if (m_IsLoaded) m_Storage->Save (m_Addresses);
			m_Storage.reset ();
		}
		m_DefaultSubscription = nullptr;
		m_NegativeCache.Clear ();
		{
			std::lock_guard<std::mutex> l (m_SubscriptionsMutex);
			m_Subscriptions.clear ();
			// The URL list is reloaded from subscriptions.txt on the next
			// Start(), so a stopped book holds no stale sources.
			m_SubscriptionURLs.clear ();
		}
		{
			std::lock_guard<std::mutex> l (m_AddressBookMutex);
			m_AddressSources.clear ();
		}
	}

	namespace
	{
		// Final label of a host: the last dot-separated component. The b33
		// label of a name is always the final label before ".i2p".
		std::string_view GetFinalLabel (std::string_view host)
		{
			auto dot = host.rfind ('.');
			return dot == std::string_view::npos ? host : host.substr (dot + 1);
		}
	} // namespace

	std::shared_ptr<const Address> AddressBook::GetAddress (std::string_view address)
	{
		// Java lowercases the host before the .b32.i2p check, and the suffix
		// must terminate the address (not merely occur in the middle). Host
		// names are ASCII case-insensitive; base64 destinations are not and
		// are parsed from the original spelling below.
		std::string normalized = NormalizeHostName (address);
		// RFC 9476: .i2p.alt is a lookup-only alias for .i2p.
		if (HasSuffixIgnoreCase (normalized, ".i2p.alt")) normalized.resize (normalized.size () - 4);
		if (normalized.ends_with (".b32.i2p"))
		{
			auto addr = std::make_shared<const Address> (normalized.substr (0, normalized.size () - 8));
			return addr->IsValid () ? addr : nullptr;
		}
		else if (normalized.ends_with (".i2p"))
		{
			// Java's LookupDestJob decodes a blinded destination in both
			// spellings: an explicit ".b33.i2p" suffix and an implicit final
			// label longer than B33_ADDRESS_THRESHOLD. The label is URL-safe
			// base64, so it comes from the case-preserving normalisation above
			// and is never looked up in the address book. A b33 name is also
			// never resolved through the local "www." alias: the alias denotes
			// an unblinded name, and a revoked or renamed b33 must not silently
			// become another destination.
			const std::string_view hostPart (normalized.data (), normalized.size () - 4);
			const auto label = GetFinalLabel (hostPart);
			const bool b33 = normalized.ends_with (".b33.i2p") || label.size () > B33_ADDRESS_THRESHOLD;
			if (b33)
			{
				// A short label behind an explicit ".b33.i2p" suffix is not a
				// base32 destination: Address() would silently decode it as one
				// and truncate the b33. The parser enforces the canonical
				// length, the base32 character set and a blindable signature
				// type, so reserved or unsigned types fail closed here instead
				// of falling through to base64 parsing.
				if (label.size () <= B33_ADDRESS_THRESHOLD || label.size () > B33_LABEL_MAX_LENGTH) return nullptr;
				auto addr = std::make_shared<const Address> (label);
				return addr->IsValid () ? addr : nullptr;
			}
			if (!m_IsEnabled) return nullptr;
			auto addr = FindAddress (normalized);
			if (!addr && normalized.size () > 8 && normalized.compare (0, 4, "www.") == 0)
				addr = FindAddress (normalized.substr (4)); // Java www. fallback, local only
			if (!addr)
			{
				// A miss is remembered for a short, bounded time so that a
				// caller polling an unknown name does not re-run FindAddress and
				// re-send a resolver query on every attempt. The entry is cleared
				// by an add/remove/merge/reload and by a successful lookup; a b33
				// name never reaches this path and the cache refuses one anyway.
				const auto now = i2p::util::GetMonotonicSeconds ();
				if (!m_NegativeCache.IsCached (normalized, now))
				{
					LookupAddress (normalized); // TODO:
					m_NegativeCache.Insert (normalized, now);
				}
			}
			return addr;
		}
		// if not .b32 we assume full base64 address
		i2p::data::IdentityEx dest;
		if (!dest.FromBase64 (address)) return nullptr;
		return std::make_shared<const Address> (dest.GetIdentHash ());
	}

	std::shared_ptr<const Address> AddressBook::FindAddress (std::string_view address)
	{
		// The address book is keyed by the normalised name, so a mixed-case
		// .I2P/.B32.I2P query resolves like its lower-case spelling.
		auto normalized = NormalizeHostName (address);
		std::lock_guard<std::mutex> l (m_AddressBookMutex);
		auto it = m_Addresses.find (normalized);
		if (it != m_Addresses.end ()) return it->second;
		return nullptr;
	}

	bool AddressBook::RecordExists (const std::string & address, const std::string & jump)
	{
		auto addr = FindAddress (address);
		if (!addr) return false;

		auto pos = jump.find (".b32.i2p");
		if (pos != std::string::npos)
		{
			i2p::data::IdentHash identHash;
			if (identHash.FromBase32 (jump.substr (0, pos)) && identHash == addr->identHash) return true;
		}
		else
		{
			i2p::data::IdentityEx ident;
			if (ident.FromBase64 (jump) && ident.GetIdentHash () == addr->identHash) return true;
		}

		return false;
	}

	void AddressBook::InsertAddress (const std::string & address, const std::string & jump)
	{
		// Store under the normalised key; the jump link and the log keep the
		// original spelling.
		auto key = NormalizeHostName (address);
		std::lock_guard<std::mutex> l (m_AddressBookMutex);
		if (IsDeletedHostLocked (key))
		{
			LogPrint (eLogInfo, "Addressbook: Refusing blocked host ", address);
			return;
		}
		auto pos = jump.find (".b32.i2p");
		if (pos != std::string::npos)
		{
			m_Addresses[key] = std::make_shared<Address> (jump.substr (0, pos));
			LogPrint (eLogInfo, "Addressbook: Added ", address, " -> ", jump);
		}
		else
		{
			// assume base64
			if (!IsValidDestinationLength (jump))
			{
				LogPrint (eLogError, "Addressbook: Rejected jump-link destination for ", address, " of length ",
					jump.size (), " (expected ", MIN_DESTINATION_BASE64_LENGTH, "..", MAX_DESTINATION_BASE64_LENGTH,
					")");
				return;
			}
			auto ident = std::make_shared<i2p::data::IdentityEx> ();
			if (ident->FromBase64 (jump))
			{
				if (m_Storage) m_Storage->AddAddress (ident);
				m_Addresses[key] = std::make_shared<Address> (ident->GetIdentHash ());
				LogPrint (eLogInfo, "Addressbook: Added ", address, " -> ", ToAddress (ident->GetIdentHash ()));
			}
			else
				// The jump value is an identity, so it is never logged.
				LogPrint (eLogError, "Addressbook: Malformed jump-link destination for ", address);
		}
		ForgetAddressSourceLocked (key);
		m_NegativeCache.Clear ();
		if (m_Storage) m_Storage->Save (m_Addresses);
	}

	void AddressBook::InsertFullAddress (std::shared_ptr<const i2p::data::IdentityEx> address)
	{
		if (!address) return;
		const auto length = address->GetFullLen ();
		if (length < MIN_IDENTITY_LENGTH || length > MAX_IDENTITY_LENGTH)
		{
			// Public identities only: the caller's object is unusable as an
			// address-book entry and its content is never logged.
			LogPrint (eLogWarning, "Addressbook: Rejected full identity of length ", length, " (expected ",
				MIN_IDENTITY_LENGTH, "..", MAX_IDENTITY_LENGTH, ")");
			return;
		}
		if (m_Storage) m_Storage->AddAddress (address);
	}

	std::vector<AddressBookEntry> AddressBook::GetEntries (size_t limit)
	{
		if (!limit) limit = 200;
		limit = std::min<size_t> (limit, 1000);
		std::lock_guard<std::mutex> l (m_AddressBookMutex);
		std::vector<AddressBookEntry> entries;
		entries.reserve (std::min (limit, m_Addresses.size ()));
		for (const auto & [name, address] : m_Addresses)
		{
			if (entries.size () >= limit) break;
			if (!address || !address->IsValid ()) continue;
			AddressBookEntry entry;
			entry.name = name;
			entry.destination = address->ToBase32 ();
			entry.blinded = !address->IsIdentHash ();
			if (!entry.destination.empty ()) entries.push_back (std::move (entry));
		}
		return entries;
	}

	std::vector<std::string> AddressBook::GetDeletedHosts (size_t limit)
	{
		if (!limit) limit = 200;
		limit = std::min<size_t> (limit, 1000);
		std::lock_guard<std::mutex> l (m_AddressBookMutex);
		std::vector<std::string> result;
		result.reserve (std::min (limit, m_DeletedHosts.size ()));
		for (const auto & pattern : m_DeletedHosts)
		{
			if (result.size () >= limit) break;
			result.push_back (pattern);
		}
		return result;
	}

	AddressBookMutationResult AddressBook::BlockAddress (std::string_view name)
	{
		auto cleaned = boost::algorithm::trim_copy (std::string (name));
		if (cleaned.empty () || cleaned.size () > 255) return AddressBookMutationResult::InvalidName;
		auto key = NormalizeHostName (cleaned);
		if (!IsValidDeletedPattern (key)) return AddressBookMutationResult::InvalidName;
		std::lock_guard<std::mutex> l (m_AddressBookMutex);
		if (!m_Storage) return AddressBookMutationResult::Unavailable;
		if (std::find (m_DeletedHosts.begin (), m_DeletedHosts.end (), key) != m_DeletedHosts.end ())
			return AddressBookMutationResult::Ok;
		// A wildcard may already cover this name. Record the exact pattern too
		// so the blocked list can unblock this specific name without deleting
		// the wildcard.
		if (m_DeletedHosts.size () >= 256) return AddressBookMutationResult::Unavailable;
		m_DeletedHosts.push_back (key);
		if (!SaveDeletedHostsLocked ())
		{
			m_DeletedHosts.pop_back ();
			return AddressBookMutationResult::Unavailable;
		}
		LogPrint (eLogInfo, "Addressbook: Blocked ", key);
		return AddressBookMutationResult::Ok;
	}

	AddressBookMutationResult AddressBook::UnblockAddress (std::string_view name)
	{
		auto cleaned = boost::algorithm::trim_copy (std::string (name));
		if (cleaned.empty () || cleaned.size () > 255) return AddressBookMutationResult::InvalidName;
		auto key = NormalizeHostName (cleaned);
		std::lock_guard<std::mutex> l (m_AddressBookMutex);
		if (!m_Storage) return AddressBookMutationResult::Unavailable;
		auto before = m_DeletedHosts;
		auto it = std::find (m_DeletedHosts.begin (), m_DeletedHosts.end (), key);
		if (it == m_DeletedHosts.end ()) return AddressBookMutationResult::NotFound;
		m_DeletedHosts.erase (it);
		if (!SaveDeletedHostsLocked ())
		{
			m_DeletedHosts = std::move (before);
			return AddressBookMutationResult::Unavailable;
		}
		LogPrint (eLogInfo, "Addressbook: Unblocked ", key);
		return AddressBookMutationResult::Ok;
	}

	AddressAddResult AddressBook::AddAddressChecked (std::string_view name, std::string_view destination, bool replace)
	{
		if (!IsValidHostName (name)) return AddressAddResult::InvalidName;
		// Java bounds the base64 destination string before decoding it. The value
		// is an identity, so a rejection logs the length, never the value.
		if (!IsValidDestinationLength (destination))
		{
			LogPrint (eLogWarning, "Addressbook: Rejected destination for ", name, " of length ", destination.size (),
				" (expected ", MIN_DESTINATION_BASE64_LENGTH, "..", MAX_DESTINATION_BASE64_LENGTH, ")");
			return AddressAddResult::InvalidDestination;
		}
		auto identity = std::make_shared<i2p::data::IdentityEx> ();
		if (!identity->FromBase64 (destination))
		{
			LogPrint (eLogWarning, "Addressbook: Rejected destination for ", name, ": not a base64 identity");
			return AddressAddResult::InvalidDestination;
		}
		const auto length = identity->GetFullLen ();
		if (length < MIN_IDENTITY_LENGTH || length > MAX_IDENTITY_LENGTH)
		{
			LogPrint (eLogWarning, "Addressbook: Rejected destination for ", name, ": identity length ", length,
				" is outside ", MIN_IDENTITY_LENGTH, "..", MAX_IDENTITY_LENGTH);
			return AddressAddResult::InvalidDestination;
		}
		const auto key = NormalizeHostName (name);
		const auto hash = identity->GetIdentHash ();

		std::lock_guard<std::mutex> l (m_AddressBookMutex);
		if (!m_Storage) return AddressAddResult::Unavailable;
		auto it = m_Addresses.find (key);
		if (it != m_Addresses.end () && it->second)
		{
			if (it->second->IsIdentHash () && it->second->identHash == hash)
			{
				if (RemoveDeletedMatchesLocked (key) && !SaveDeletedHostsLocked ())
					LogPrint (eLogWarning, "Addressbook: Could not persist cleared deleted-host pattern for ", key);
				return AddressAddResult::Ok;
			}
			// Without an explicit replace the existing destination is kept and
			// the entry is not touched at all.
			if (!replace) return AddressAddResult::Exists;
		}

		std::shared_ptr<Address> previous = it != m_Addresses.end () ? it->second : nullptr;
		m_Storage->AddAddress (identity);
		m_Addresses[key] = std::make_shared<Address> (hash);
		if (m_Storage->Save (m_Addresses) < 0)
		{
			if (previous)
				m_Addresses[key] = previous;
			else
				m_Addresses.erase (key);
			// Do not leave an identity file orphaned when the index write
			// failed, unless another name already references it.
			bool referenced = false;
			for (const auto & [unused, address] : m_Addresses)
				if (address && address->IsIdentHash () && address->identHash == hash)
				{
					referenced = true;
					break;
				}
			if (!referenced) m_Storage->RemoveAddress (hash);
			return AddressAddResult::Unavailable;
		}
		if (previous && previous->IsIdentHash () && previous->identHash != hash)
		{
			bool referenced = false;
			for (const auto & [unused, address] : m_Addresses)
				if (address && address->IsIdentHash () && address->identHash == previous->identHash)
				{
					referenced = true;
					break;
				}
			if (!referenced) m_Storage->RemoveAddress (previous->identHash);
		}
		// An explicit add is local authority: the name is no longer retractable
		// by the feed that may have supplied the previous destination.
		ForgetAddressSourceLocked (key);
		m_NegativeCache.Clear ();
		if (RemoveDeletedMatchesLocked (key) && !SaveDeletedHostsLocked ())
			LogPrint (eLogWarning, "Addressbook: Could not persist cleared deleted-host pattern for ", key);
		LogPrint (eLogInfo, "Addressbook: Added ", key, " -> ", hash.ToBase32 ());
		return AddressAddResult::Ok;
	}

	AddressBookMutationResult AddressBook::AddAddress (
		std::string_view name, std::string_view destination, bool replace)
	{
		switch (AddAddressChecked (name, destination, replace))
		{
		case AddressAddResult::Ok: return AddressBookMutationResult::Ok;
		case AddressAddResult::InvalidName: return AddressBookMutationResult::InvalidName;
		case AddressAddResult::InvalidDestination: return AddressBookMutationResult::InvalidDestination;
		case AddressAddResult::Exists: return AddressBookMutationResult::Exists;
		case AddressAddResult::Unavailable: return AddressBookMutationResult::Unavailable;
		}
		return AddressBookMutationResult::Unavailable;
	}

	size_t AddressBook::GetNegativeCacheSize () const
	{
		return m_NegativeCache.Size (i2p::util::GetMonotonicSeconds ());
	}

	AddressBookMutationResult AddressBook::RemoveAddress (std::string_view name)
	{
		// Removal must also work for blinded (.b33.i2p) entries that were
		// learned from subscriptions and therefore are not valid ADD targets.
		if (name.empty () || name.size () > 255) return AddressBookMutationResult::InvalidName;
		const auto key = NormalizeHostName (name);
		std::lock_guard<std::mutex> l (m_AddressBookMutex);
		if (!m_Storage) return AddressBookMutationResult::Unavailable;
		auto it = m_Addresses.find (key);
		if (it == m_Addresses.end ()) return AddressBookMutationResult::NotFound;
		auto previous = it->second;
		auto previousDeleted = m_DeletedHosts;
		const bool alreadyBlocked = IsDeletedHostLocked (key);
		if (!alreadyBlocked)
		{
			if (m_DeletedHosts.size () >= 256) return AddressBookMutationResult::Unavailable;
			m_DeletedHosts.push_back (key);
			if (!SaveDeletedHostsLocked ())
			{
				m_DeletedHosts = std::move (previousDeleted);
				return AddressBookMutationResult::Unavailable;
			}
		}
		m_Addresses.erase (it);
		if (m_Storage->Save (m_Addresses) < 0)
		{
			m_Addresses[key] = previous;
			if (!alreadyBlocked)
			{
				m_DeletedHosts = std::move (previousDeleted);
				SaveDeletedHostsLocked ();
			}
			return AddressBookMutationResult::Unavailable;
		}
		if (previous && previous->IsIdentHash ())
		{
			bool referenced = false;
			for (const auto & [unused, address] : m_Addresses)
				if (address && address->IsIdentHash () && address->identHash == previous->identHash)
				{
					referenced = true;
					break;
				}
			if (!referenced) m_Storage->RemoveAddress (previous->identHash);
		}
		ForgetAddressSourceLocked (key);
		m_NegativeCache.Clear ();
		LogPrint (eLogInfo, "Addressbook: Removed ", key);
		return AddressBookMutationResult::Ok;
	}

	std::shared_ptr<const i2p::data::IdentityEx> AddressBook::GetFullAddress (const std::string & address)
	{
		auto addr = GetAddress (address);
		if (!addr || !addr->IsIdentHash ()) return nullptr;
		return m_Storage ? m_Storage->GetAddress (addr->identHash) : nullptr;
	}

	void AddressBook::LoadHosts ()
	{
		if (!m_Storage) return;
		m_NegativeCache.Clear ();
		const int indexed = m_Storage->Load (m_Addresses);
		if (indexed >= 0)
		{
			// An existing empty index is authoritative: the last entry was
			// explicitly removed and must not be resurrected from hosts.txt.
			m_IsLoaded = true;
			LoadAddressSources ();
			return;
		}

		// No index yet: try hosts.txt
		std::ifstream f (i2p::fs::DataDirPath ("hosts.txt"), std::ifstream::in); // in text mode
		if (f.is_open ())
		{
			LoadHostsFromStream (std::move (f), false);
			m_IsLoaded = true;
		}

		// reset eTags, because we don’t know how old hosts.txt is or can't load addressbook
		m_Storage->ResetEtags ();
	}

	bool AddressBook::LoadHostsFromStream (std::istream && f, bool is_update)
	{
		return LoadHostsFromStream (std::move (f), is_update, std::string_view ());
	}

	bool AddressBook::LoadHostsFromStream (std::istream && f, bool is_update, std::string_view source)
	{
		std::unique_lock<std::mutex> l (m_AddressBookMutex);
		int numAddresses = 0;
		size_t deletions = 0;
		bool sourcesChanged = false;
		bool incomplete = false;
		// Precedence: an automatic merge never overwrites a name that already
		// had a destination when the stream started. Duplicate lines inside one
		// source may refine each other (last value wins, like a properties
		// file), because a source that controls both values cannot gain
		// authority over a locally curated name that way.
		std::unordered_set<std::string> batchNames;
		size_t conflicts = 0;
		size_t conflictLogs = 0;
		std::string s;
		while (std::getline (f, s))
		{
			if (s.empty () || s[0] == '#') continue; // skip empty or comment line

			size_t pos = s.find ('=');

			if (pos != std::string::npos)
			{
				if (numAddresses >= (int)MAX_SUBSCRIPTION_ENTRIES)
				{
					LogPrint (eLogWarning, "Addressbook: Subscription entry limit reached");
					break;
				}
				std::string_view name = std::string_view (s).substr (0, pos++);
				std::string_view addr = std::string_view (s).substr (pos);

				// The Java advanced form appends "#!key=value" pairs after the
				// destination; text after a plain '#' is a comment. Only a bounded
				// block is parsed and an over-long one is treated as comment text.
				SubscriptionLineAction action;
				size_t commentPos = addr.find ('#');
				if (commentPos != addr.npos)
				{
					auto tail = addr.substr (commentPos);
					if (tail.size () > 1 && tail[1] == '!') ParseSubscriptionAction (tail.substr (2), action);
					addr = addr.substr (0, commentPos); // remove comments
				}
				if (!IsValidHostName (name))
				{
					LogPrint (eLogWarning, "Addressbook: Invalid subscription host name ", name);
					continue;
				}
				// The map key is the normalised name; the original spelling is
				// kept in diagnostics (and in the jump-link writer's output).
				auto key = NormalizeHostName (name);
				if (action.deleteRequested)
				{
					// Only the feed that supplied a name may retract it, and only
					// while merging a fetched source. A locally added name, a name
					// from another feed and a name without a recorded origin are
					// always kept.
					if (!is_update || source.empty ())
					{
						LogPrint (
							eLogInfo, "Addressbook: Ignoring retraction of ", name, " outside a subscription merge");
						continue;
					}
					if (!IsFeedOriginLocked (key, source))
					{
						LogPrint (eLogWarning, "Addressbook: ", std::string (source), " asked to retract ", name,
							" which it did not supply; keeping it");
						continue;
					}
					if (ApplyFeedDeleteLocked (key, name))
						deletions++;
					else
						ForgetAddressSourceLocked (key); // the entry is already gone
					continue;
				}
				if (is_update && IsDeletedHostLocked (key))
				{
					LogPrint (eLogInfo, "Addressbook: Skipping deleted host ", name);
					continue;
				}
				if (key.ends_with (".b32.i2p"))
				{
					LogPrint (eLogError, "Addressbook: Skipped adding of b32 address: ", name);
					continue;
				}

				if (!key.ends_with (".i2p"))
				{
					LogPrint (eLogError, "Addressbook: Malformed domain: ", name);
					continue;
				}

				// Java bounds the base64 destination before decoding it. The value
				// is an identity, so a rejection never logs it.
				if (!IsValidDestinationLength (addr))
				{
					LogPrint (eLogError, "Addressbook: Rejected destination for ", name, " of length ", addr.size (),
						" (expected ", MIN_DESTINATION_BASE64_LENGTH, "..", MAX_DESTINATION_BASE64_LENGTH, ")");
					incomplete = f.eof ();
					continue;
				}

				auto ident = std::make_shared<i2p::data::IdentityEx> ();
				if (!ident->FromBase64 (addr))
				{
					LogPrint (eLogError, "Addressbook: Malformed destination for ", name);
					incomplete = f.eof ();
					continue;
				}
				numAddresses++;
				auto it = m_Addresses.find (key);
				if (it != m_Addresses.end ()) // already exists ?
				{
					const bool sameDestination
						= it->second->IsIdentHash () && it->second->identHash == ident->GetIdentHash ();
					if (!batchNames.contains (key))
					{
						// The name was known before this merge: keep it. Only an
						// explicit local add/replace may re-point it.
						if (!sameDestination)
						{
							conflicts++;
							RecordMergeConflict ();
							if (conflictLogs++ < MAX_CONFLICT_LOG_LINES)
								LogPrint (eLogWarning,
									"Addressbook: ", source.empty () ? "Bulk source" : std::string (source),
									" offers another destination for ", name, "; keeping the existing entry");
						}
					}
					else if (!sameDestination && it->second->IsIdentHash () && // address changed inside this source?
						ident->GetSigningKeyType () != i2p::data::SIGNING_KEY_TYPE_DSA_SHA1) // don't replace by DSA
					{
						if (m_Storage)
						{
							m_Storage->AddAddress (ident);
							m_Storage->RemoveAddress (it->second->identHash);
						}
						it->second->identHash = ident->GetIdentHash ();
						LogPrint (eLogInfo, "Addressbook: Updated host: ", name);
					}
				}
				else
				{
					batchNames.insert (key);
					m_Addresses.emplace (key, std::make_shared<Address> (ident->GetIdentHash ()));
					if (m_Storage) m_Storage->AddAddress (ident);
					// Remember that this feed supplied the name, so only that feed
					// may retract it later.
					if (!source.empty ())
					{
						m_AddressSources[key] = std::string (source);
						sourcesChanged = true;
					}
					if (is_update) LogPrint (eLogInfo, "Addressbook: Added new host: ", name);
				}
			}
			else
				incomplete = f.eof ();
		}
		if (f.bad ()) incomplete = true;
		if (conflicts > MAX_CONFLICT_LOG_LINES)
			LogPrint (eLogWarning, "Addressbook: ", conflicts, " subscription entries for ",
				source.empty () ? "this source" : std::string (source), " were declined; local destinations kept");
		if (deletions > 0)
			LogPrint (eLogInfo, "Addressbook: ", deletions, " entries retracted by ",
				source.empty () ? "this source" : std::string (source));
		LogPrint (eLogInfo, "Addressbook: ", numAddresses, " addresses processed");
		if (numAddresses > 0 || deletions > 0)
		{
			if (!incomplete) m_IsLoaded = true;
			if (sourcesChanged && m_Storage && !SaveAddressSourcesLocked ())
				LogPrint (eLogWarning, "Addressbook: Could not persist entry origins");
			if (m_Storage) m_Storage->Save (m_Addresses);
			// The merge changed what the book knows, so a cached miss may now be
			// a hit.
			m_NegativeCache.Clear ();
		}
		return !incomplete;
	}

	std::vector<std::string> AddressBook::ReadSubscriptionSources ()
	{
		std::vector<std::string> urls;
		bool limitLogged = false;
		auto AddSource = [&urls, &limitLogged] (std::string_view candidate)
		{
			std::string error;
			if (!IsValidSubscriptionURL (candidate, error))
			{
				LogPrint (eLogWarning, "Addressbook: Ignoring subscription source: ", error);
				return;
			}
			if (urls.size () >= MAX_SUBSCRIPTION_URLS)
			{
				if (!limitLogged)
				{
					LogPrint (eLogWarning, "Addressbook: Subscription source limit of ", MAX_SUBSCRIPTION_URLS,
						" reached; ignoring further sources");
					limitLogged = true;
				}
				return;
			}
			if (std::find (urls.begin (), urls.end (), candidate) != urls.end ()) return;
			urls.emplace_back (candidate);
		};

		const auto subscriptionsPath = i2p::fs::DataDirPath ("subscriptions.txt");
		std::error_code sizeError;
		const auto subscriptionsSize = std::filesystem::file_size (subscriptionsPath, sizeError);
		const bool tooLarge = !sizeError && subscriptionsSize > MAX_SUBSCRIPTION_FILE;
		if (tooLarge)
			LogPrint (eLogError, "Addressbook: subscriptions.txt exceeds the ", MAX_SUBSCRIPTION_FILE,
				"-byte bound; using addressbook.subscriptions");
		std::ifstream f (subscriptionsPath, std::ifstream::in); // in text mode
		if (f.is_open () && !tooLarge)
		{
			std::string s;
			size_t lines = 0;
			size_t consumed = 0;
			while (std::getline (f, s) && lines++ < MAX_SUBSCRIPTION_FILE_LINES && consumed < MAX_SUBSCRIPTION_FILE)
			{
				consumed += s.size () + 1;
				if (!s.empty () && s.back () == '\r') s.pop_back ();
				auto first = s.find_first_not_of (" \t");
				if (first == std::string::npos || s[first] == '#') continue; // skip empty line or comment
				auto last = s.find_last_not_of (" \t");
				if (last - first + 1 > MAX_SUBSCRIPTION_URL_LENGTH)
				{
					LogPrint (eLogWarning, "Addressbook: Ignoring overlong subscription source line");
					continue;
				}
				AddSource (std::string_view (s).substr (first, last - first + 1));
			}
			if (f.bad ()) LogPrint (eLogError, "Addressbook: I/O error while reading subscriptions.txt");
			LogPrint (eLogWarning, "Addressbook: subscriptions.txt usage is deprecated, use config file instead");
		}
		else
		{
			LogPrint (eLogInfo, "Addressbook: Loading subscriptions from config");
			// using config file items
			std::string subscriptionURLs;
			i2p::config::GetOption ("addressbook.subscriptions", subscriptionURLs);
			std::vector<std::string> subsList;
			boost::split (subsList, subscriptionURLs, boost::is_any_of (","), boost::token_compress_on);
			for (const auto & s : subsList)
			{
				auto first = s.find_first_not_of (" \t");
				if (first == std::string::npos) continue;
				auto last = s.find_last_not_of (" \t");
				AddSource (std::string_view (s).substr (first, last - first + 1));
			}
		}
		return urls;
	}

	void AddressBook::RebuildSubscriptionsLocked ()
	{
		m_Subscriptions.clear ();
		m_Subscriptions.reserve (m_SubscriptionURLs.size ());
		for (const auto & url : m_SubscriptionURLs)
			m_Subscriptions.push_back (std::make_shared<AddressBookSubscription> (*this, url));
	}

	bool AddressBook::SaveSubscriptionsLocked (std::string & error)
	{
		std::string contents ("# Address-book subscription URLs; one absolute I2P URL per line.\n");
		for (const auto & url : m_SubscriptionURLs)
		{
			if (contents.size () + url.size () + 1 > MAX_SUBSCRIPTION_FILE)
			{
				error = "subscription list is too large";
				return false;
			}
			contents.append (url);
			contents.push_back ('\n');
		}
		// Same-directory, no-follow replacement from i2p::fs: the list is never
		// written by chmod()ing a pathname and reopening it.
		if (!i2p::fs::AtomicWrite (i2p::fs::DataDirPath ("subscriptions.txt"), contents))
		{
			error = "could not write subscriptions.txt";
			return false;
		}
		return true;
	}

	void AddressBook::LoadSubscriptions ()
	{
		auto urls = ReadSubscriptionSources ();
		std::lock_guard<std::mutex> l (m_SubscriptionsMutex);
		if (!m_SubscriptionURLs.empty ())
		{
			LogPrint (eLogError, "Addressbook: Subscriptions already loaded");
			return;
		}
		m_SubscriptionURLs = std::move (urls);
		RebuildSubscriptionsLocked ();
		LogPrint (eLogInfo, "Addressbook: ", m_Subscriptions.size (), " subscriptions urls loaded");
	}

	std::vector<std::string> AddressBook::GetSubscriptions (size_t max) const
	{
		if (!max) max = MAX_SUBSCRIPTION_URLS;
		max = std::min<size_t> (max, MAX_SUBSCRIPTION_URLS);
		std::lock_guard<std::mutex> l (m_SubscriptionsMutex);
		std::vector<std::string> result;
		result.reserve (std::min (max, m_SubscriptionURLs.size ()));
		for (const auto & url : m_SubscriptionURLs)
		{
			if (result.size () >= max) break;
			result.push_back (url);
		}
		return result;
	}

	bool AddressBook::AddSubscription (std::string_view url, std::string & error)
	{
		error.clear ();
		auto cleaned = boost::algorithm::trim_copy (std::string (url));
		if (!IsValidSubscriptionURL (cleaned, error)) return false;
		{
			std::lock_guard<std::mutex> l (m_SubscriptionsMutex);
			if (std::find (m_SubscriptionURLs.begin (), m_SubscriptionURLs.end (), cleaned)
				!= m_SubscriptionURLs.end ())
			{
				error = "subscription already exists";
				return false;
			}
			if (m_SubscriptionURLs.size () >= MAX_SUBSCRIPTION_URLS)
			{
				error = "subscription limit reached";
				return false;
			}
			auto previous = m_SubscriptionURLs;
			m_SubscriptionURLs.push_back (cleaned);
			// The atomic rewrite is bounded (<= 64 KiB) and the mutex is not held
			// across network IO; the administration thread may wait for it.
			if (!SaveSubscriptionsLocked (error))
			{
				m_SubscriptionURLs = std::move (previous);
				return false;
			}
			RebuildSubscriptionsLocked ();
		}
		LogPrint (eLogInfo, "Addressbook: Added subscription ", SubscriptionLabel (cleaned));
		// Never fetch or block here: the destination's io_context re-arms the
		// subscription timer and picks the new source up from the list.
		PostSubscriptionsRearm (SUBSCRIPTION_RELOAD_DELAY);
		return true;
	}

	bool AddressBook::RemoveSubscription (std::string_view url, std::string & error)
	{
		error.clear ();
		auto cleaned = boost::algorithm::trim_copy (std::string (url));
		if (!IsValidSubscriptionURL (cleaned, error)) return false;
		{
			std::lock_guard<std::mutex> l (m_SubscriptionsMutex);
			auto it = std::find (m_SubscriptionURLs.begin (), m_SubscriptionURLs.end (), cleaned);
			if (it == m_SubscriptionURLs.end ())
			{
				error = "subscription not found";
				return false;
			}
			auto previous = m_SubscriptionURLs;
			m_SubscriptionURLs.erase (it);
			if (!SaveSubscriptionsLocked (error))
			{
				m_SubscriptionURLs = std::move (previous);
				return false;
			}
			RebuildSubscriptionsLocked ();
		}
		LogPrint (eLogInfo, "Addressbook: Removed subscription ", SubscriptionLabel (cleaned));
		PostSubscriptionsRearm (SUBSCRIPTION_RELOAD_DELAY);
		return true;
	}

	void AddressBook::RecordMergeConflict ()
	{
		// Saturating: the counter is a diagnostic and must not overflow after a
		// hostile source offers millions of conflicting names.
		uint64_t current = m_MergeConflicts.load (std::memory_order_relaxed);
		while (current != std::numeric_limits<uint64_t>::max ()
			&& !m_MergeConflicts.compare_exchange_weak (
				current, current + 1, std::memory_order_relaxed, std::memory_order_relaxed))
		{
		}
	}

	std::shared_ptr<std::atomic<bool>> AddressBook::ReloadToken () const
	{
		std::lock_guard<std::mutex> l (m_SubscriptionsMutex);
		return m_ReloadAlive;
	}

	void AddressBook::PostSubscriptionsRearm (std::chrono::seconds delay)
	{
		auto dest = i2p::client::context.GetSharedLocalDestination ();
		if (!dest) return; // Start()/ResetTimers() read the persisted list instead
		auto token = ReloadToken ();
		if (!token) return;
		auto & service = dest->GetService ();
		boost::asio::post (service,
			[this, token, delay]
			{
				// Lifetime authority: Stop() clears the shared token before it
				// releases the subscription timer and the source objects.
				if (!token->load (std::memory_order_acquire)) return;
				RearmSubscriptionsTimer (delay);
			});
	}

	void AddressBook::RearmSubscriptionsTimer (std::chrono::seconds delay)
	{
		auto dest = i2p::client::context.GetSharedLocalDestination ();
		if (!dest)
		{
			LogPrint (eLogWarning, "Addressbook: Subscription list changed without a local destination");
			return;
		}
		// Runs on the destination's io_context. The lock is shared with Stop(),
		// which invalidates the token and releases the timer, so a re-arm that
		// loses the shutdown race cannot touch stopped state.
		std::lock_guard<std::mutex> l (m_SubscriptionsMutex);
		if (!m_ReloadAlive || !m_ReloadAlive->load (std::memory_order_relaxed)) return;
		if (m_Subscriptions.empty () && m_IsLoaded) return;
		if (!m_SubscriptionsUpdateTimer)
			m_SubscriptionsUpdateTimer = std::make_unique<boost::asio::steady_timer> (dest->GetService ());
		else
			m_SubscriptionsUpdateTimer->cancel (); // a pending wait resumes as operation_aborted
		m_SubscriptionsUpdateTimer->expires_after (delay);
		m_SubscriptionsUpdateTimer->async_wait (
			std::bind (&AddressBook::HandleSubscriptionsUpdateTimer, this, std::placeholders::_1));
		LogPrint (eLogDebug, "Addressbook: Subscription update armed in ", delay.count (), " seconds");
	}

	void AddressBook::LoadLocal ()
	{
		if (!m_Storage) return;
		AddressBookStorage::Addresses localAddresses;
		m_Storage->LoadLocal (localAddresses);
		for (const auto & it : localAddresses)
		{
			if (!it.second->IsIdentHash ()) continue; // skip blinded for now
			auto dot = it.first.find ('.');
			if (dot != std::string::npos)
			{
				auto domain = it.first.substr (dot + 1);
				auto it1 = m_Addresses.find (domain); // find domain in our addressbook
				if (it1 != m_Addresses.end () && it1->second->IsIdentHash ())
				{
					auto dest = context.FindLocalDestination (it1->second->identHash);
					if (dest)
					{
						// address is ours
						std::shared_ptr<AddressResolver> resolver;
						auto it2 = m_Resolvers.find (it1->second->identHash);
						if (it2 != m_Resolvers.end ())
							resolver = it2->second; // resolver exists
						else
						{
							// create new resolver
							resolver = std::make_shared<AddressResolver> (dest);
							m_Resolvers.insert (std::make_pair (it1->second->identHash, resolver));
						}
						resolver->AddAddress (it.first, it.second->identHash);
					}
				}
			}
		}
	}

	bool AddressBook::GetEtag (
		const i2p::data::IdentHash & subscription, std::string & etag, std::string & lastModified)
	{
		if (m_Storage)
			return m_Storage->GetEtag (subscription, etag, lastModified);
		else
			return false;
	}

	void AddressBook::DownloadComplete (bool success, const i2p::data::IdentHash & subscription,
		const std::string & etag, const std::string & lastModified)
	{
		DownloadComplete (success ? AddressBookDownloadResult::Updated : AddressBookDownloadResult::Failed,
			subscription, etag, lastModified);
	}

	void AddressBook::DownloadComplete (AddressBookDownloadResult result, const i2p::data::IdentHash & subscription,
		const std::string & etag, const std::string & lastModified)
	{
		const bool succeeded = result != AddressBookDownloadResult::Failed;
		m_NumRetries++;
		int nextUpdateTimeout = m_NumRetries * CONTINIOUS_SUBSCRIPTION_RETRY_TIMEOUT;
		if (m_NumRetries > CONTINIOUS_SUBSCRIPTION_MAX_NUM_RETRIES
			|| nextUpdateTimeout > CONTINIOUS_SUBSCRIPTION_UPDATE_TIMEOUT)
			nextUpdateTimeout = CONTINIOUS_SUBSCRIPTION_UPDATE_TIMEOUT;
		if (succeeded)
		{
			// "Not Modified" is not a failure: the source answered and the cached
			// book is current, so the retry ladder is reset and the normal cadence
			// continues. The stored ETag/Last-Modified are kept as they are.
			m_NumRetries = 0;
			if (m_DefaultSubscription) m_DefaultSubscription = nullptr;
			if (m_IsLoaded)
				nextUpdateTimeout = CONTINIOUS_SUBSCRIPTION_UPDATE_TIMEOUT;
			else
				m_IsLoaded = true;
			if (result == AddressBookDownloadResult::Updated && m_Storage)
				m_Storage->SaveEtag (subscription, etag, lastModified);
		}
		// A download completes on the async worker thread, so the re-arm is
		// handed to the destination's io_context rather than touching its timer
		// (or a reload posted from the administration thread) from here.
		PostSubscriptionsRearm (std::chrono::minutes (nextUpdateTimeout));
	}

	void AddressBook::StartSubscriptions ()
	{
		LoadSubscriptions ();
		bool hasSources = false;
		{
			std::lock_guard<std::mutex> l (m_SubscriptionsMutex);
			hasSources = !m_Subscriptions.empty ();
		}
		if (m_IsLoaded && !hasSources) return;

		auto dest = i2p::client::context.GetSharedLocalDestination ();
		if (dest)
		{
			// The timer is also replaced by the io_context re-arm path and by
			// ResetTimers() on the administration thread; all writers share the
			// subscription mutex.
			std::lock_guard<std::mutex> l (m_SubscriptionsMutex);
			if (!m_ReloadAlive || !m_ReloadAlive->load (std::memory_order_relaxed)) return;
			m_SubscriptionsUpdateTimer = std::make_unique<boost::asio::steady_timer> (dest->GetService ());
			m_SubscriptionsUpdateTimer->expires_after (std::chrono::minutes (INITIAL_SUBSCRIPTION_UPDATE_TIMEOUT));
			m_SubscriptionsUpdateTimer->async_wait (
				std::bind (&AddressBook::HandleSubscriptionsUpdateTimer, this, std::placeholders::_1));
		}
		else
			LogPrint (eLogCritical, "Addressbook: Can't start subscriptions: missing shared local destination");
	}

	void AddressBook::StopSubscriptions ()
	{
		std::lock_guard<std::mutex> l (m_SubscriptionsMutex);
		if (m_SubscriptionsUpdateTimer) m_SubscriptionsUpdateTimer->cancel ();
	}

	void AddressBook::ResetTimers ()
	{
		// both timers run on the shared local destination's service, so they must be
		// created anew when that destination is replaced
		auto dest = i2p::client::context.GetSharedLocalDestination ();
		// A replaced destination invalidates pending lookups, so a cached miss is
		// no longer trustworthy either.
		m_NegativeCache.Clear ();
		{
			// ReloadConfig() runs on the daemon-owned administration thread while
			// the timer handler and RearmSubscriptionsTimer() run on the shared
			// destination's io_context. Serialize the pointer replacement and
			// respect the shutdown token so a reload cannot resurrect a stopped
			// subscription timer.
			std::lock_guard<std::mutex> l (m_SubscriptionsMutex);
			if (m_SubscriptionsUpdateTimer)
			{
				m_SubscriptionsUpdateTimer->cancel ();
				m_SubscriptionsUpdateTimer = nullptr;
				if (dest && m_ReloadAlive && m_ReloadAlive->load (std::memory_order_relaxed))
				{
					m_SubscriptionsUpdateTimer = std::make_unique<boost::asio::steady_timer> (dest->GetService ());
					m_SubscriptionsUpdateTimer->expires_after (
						std::chrono::minutes (INITIAL_SUBSCRIPTION_UPDATE_TIMEOUT));
					m_SubscriptionsUpdateTimer->async_wait (
						std::bind (&AddressBook::HandleSubscriptionsUpdateTimer, this, std::placeholders::_1));
				}
			}
		}
		if (m_AddressCacheUpdateTimer)
		{
			m_AddressCacheUpdateTimer->cancel ();
			m_AddressCacheUpdateTimer = nullptr;
			ScheduleCacheUpdate (); // creates it again on the current destination
		}
	}

	void AddressBook::HandleSubscriptionsUpdateTimer (const boost::system::error_code & ecode)
	{
		if (ecode != boost::asio::error::operation_aborted)
		{
			auto dest = i2p::client::context.GetSharedLocalDestination ();
			if (!dest)
			{
				LogPrint (eLogWarning, "Addressbook: Missing local destination, skip subscription update");
				return;
			}
			bool isDownloading = m_Downloading.valid ();
			if (isDownloading
				&& m_Downloading.wait_for (std::chrono::seconds (0)) == std::future_status::ready) // still active?
			{
				m_Downloading.get ();
				isDownloading = false;
			}
			// Copy the source list so a concurrent administration reload cannot
			// invalidate the chosen object; the entry keeps it alive while the
			// download runs.
			std::vector<std::shared_ptr<AddressBookSubscription>> subscriptions;
			{
				std::lock_guard<std::mutex> l (m_SubscriptionsMutex);
				subscriptions = m_Subscriptions;
			}
			if (!isDownloading && dest->IsReady ())
			{
				if (!m_IsLoaded)
				{
					// download it from default subscription
					LogPrint (eLogInfo, "Addressbook: Trying to download it from default subscription.");
					std::string defaultSubURL;
					i2p::config::GetOption ("addressbook.defaulturl", defaultSubURL);
					if (!m_DefaultSubscription)
						m_DefaultSubscription = std::make_shared<AddressBookSubscription> (*this, defaultSubURL);
					m_Downloading = std::async (
						std::launch::async, std::bind (&AddressBookSubscription::CheckUpdates, m_DefaultSubscription));
				}
				else if (!subscriptions.empty ())
				{
					// pick random subscription from the current snapshot
					auto ind = i2p::crypto::RandomBelow (static_cast<uint32_t> (subscriptions.size ()));
					m_Downloading = std::async (
						std::launch::async, std::bind (&AddressBookSubscription::CheckUpdates, subscriptions[ind]));
				}
			}
			else
			{
				// try it again later. The timer can be replaced by ResetTimers()
				// on the administration thread, so the mutation shares the
				// subscription lock and respects the shutdown token.
				std::lock_guard<std::mutex> l (m_SubscriptionsMutex);
				if (!m_ReloadAlive || !m_ReloadAlive->load (std::memory_order_relaxed)) return;
				if (!m_SubscriptionsUpdateTimer) return;
				m_SubscriptionsUpdateTimer->expires_after (std::chrono::minutes (INITIAL_SUBSCRIPTION_RETRY_TIMEOUT));
				m_SubscriptionsUpdateTimer->async_wait (
					std::bind (&AddressBook::HandleSubscriptionsUpdateTimer, this, std::placeholders::_1));
			}
		}
	}

	void AddressBook::StartLookups ()
	{
		m_LookupReceiver.Reset ();
		auto dest = i2p::client::context.GetSharedLocalDestination ();
		if (dest)
		{
			auto datagram = dest->GetDatagramDestination ();
			if (!datagram) datagram = dest->CreateDatagramDestination ();
			m_LookupReceiver = datagram->SetReceiver (
				std::bind (&AddressBook::HandleLookupResponse, this, std::placeholders::_1, std::placeholders::_2,
					std::placeholders::_3, std::placeholders::_4, std::placeholders::_5),
				ADDRESS_RESPONSE_DATAGRAM_PORT);
		}
	}

	void AddressBook::StopLookups ()
	{
		m_LookupReceiver.Reset ();
		m_Lookups.Clear ();
	}

	void AddressBook::LookupAddress (std::string_view address)
	{
		std::shared_ptr<const Address> addr;
		auto dot = address.find ('.');
		if (dot != std::string::npos) addr = FindAddress (address.substr (dot + 1));
		if (!addr || !addr->IsIdentHash ()) // TODO:
		{
			LogPrint (eLogError, "Addressbook: Can't find domain for ", address);
			return;
		}

		auto dest = i2p::client::context.GetSharedLocalDestination ();
		if (dest)
		{
			auto datagram = dest->GetDatagramDestination ();
			if (datagram)
			{
				// The wire format stores the requested name length in a single
				// byte, so refuse names that cannot be represented instead of
				// silently truncating them.
				if (address.length () > 255)
				{
					LogPrint (eLogError, "Addressbook: Lookup name is too long ", address.length ());
					return;
				}
				uint32_t nonce;
				if (RAND_bytes (reinterpret_cast<unsigned char *> (&nonce), sizeof (nonce)) != 1) return;
				if (!m_Lookups.Insert (nonce, address, addr->identHash, i2p::util::GetMonotonicSeconds ())) return;
				LogPrint (eLogDebug, "Addressbook: Lookup of ", address, " to ", addr->identHash.ToBase32 (),
					" nonce=", nonce);
				// reserved (4) || nonce (4) || name length (1) || name
				std::vector<uint8_t> buf (address.length () + 9, 0);
				htobe32buf (buf.data () + 4, nonce);
				buf[8] = static_cast<uint8_t> (address.length ());
				memcpy (buf.data () + 9, address.data (), address.length ());
				// SendDatagramTo has no span overload; pass the contiguous buffer
				// explicitly. It copies the payload before buf is destroyed.
				datagram->SendDatagramTo (buf.data (), buf.size (), addr->identHash, ADDRESS_RESPONSE_DATAGRAM_PORT,
					ADDRESS_RESOLVER_DATAGRAM_PORT);
			}
		}
	}

	void AddressBook::HandleLookupResponse (
		const i2p::data::IdentityEx & from, uint16_t fromPort, uint16_t toPort, const uint8_t * buf, size_t len)
	{
		if (fromPort != ADDRESS_RESOLVER_DATAGRAM_PORT || toPort != ADDRESS_RESPONSE_DATAGRAM_PORT) return;
		if (len < 44)
		{
			LogPrint (eLogError, "Addressbook: Lookup response is too short ", len);
			return;
		}
		uint32_t nonce = bufbe32toh (buf + 4);
		LogPrint (eLogDebug, "Addressbook: Lookup response received from ", from.GetIdentHash ().ToBase32 (),
			" nonce=", nonce);
		auto address = m_Lookups.Take (nonce, from.GetIdentHash (), i2p::util::GetMonotonicSeconds ());
		if (address)
		{
			i2p::data::IdentHash hash (buf + 8);
			if (!hash.IsZero ())
			{
				auto key = NormalizeHostName (*address);
				std::lock_guard<std::mutex> l (m_AddressBookMutex);
				// A successful lookup answers the recent miss.
				m_NegativeCache.Erase (key);
				if (IsDeletedHostLocked (key))
					LogPrint (eLogInfo, "Addressbook: Ignoring blocked lookup response for ", *address);
				else
					m_Addresses[key] = std::make_shared<Address> (hash);
			}
			else
				LogPrint (eLogInfo, "AddressBook: Lookup response: ", *address, " not found");
		}
	}

	void AddressBook::ScheduleCacheUpdate ()
	{
		if (!m_AddressCacheUpdateTimer)
		{
			auto dest = i2p::client::context.GetSharedLocalDestination ();
			if (dest) m_AddressCacheUpdateTimer = std::make_unique<boost::asio::steady_timer> (dest->GetService ());
		}
		if (m_AddressCacheUpdateTimer)
		{
			m_AddressCacheUpdateTimer->expires_after (std::chrono::seconds (ADDRESS_CACHE_UPDATE_INTERVAL));
			m_AddressCacheUpdateTimer->async_wait (
				[this] (const boost::system::error_code & ecode)
				{
					if (ecode != boost::asio::error::operation_aborted)
					{
						if (m_Storage) m_Storage->CleanUpCache ();
						ScheduleCacheUpdate ();
					}
				});
		}
	}

	AddressBookSubscription::AddressBookSubscription (AddressBook & book, std::string_view link)
		: m_Book (book)
		, m_Link (link)
	{
	}

	void AddressBookSubscription::CheckUpdates ()
	{
		i2p::util::SetThreadName ("Addressbook");

		auto result = MakeRequest ();
		m_Book.DownloadComplete (result, m_Ident, m_Etag, m_LastModified);
	}

	AddressBookDownloadResult AddressBookSubscription::MakeRequest ()
	{
		i2p::http::URL url;
		// must be run in separate thread
		LogPrint (eLogInfo, "Addressbook: Downloading hosts database from ", m_Link);
		if (!url.parse (m_Link))
		{
			LogPrint (eLogError, "Addressbook: Failed to parse url: ", m_Link);
			return AddressBookDownloadResult::Failed;
		}
		auto addr = m_Book.GetAddress (url.host);
		if (!addr || !addr->IsIdentHash ())
		{
			LogPrint (eLogError, "Addressbook: Can't resolve ", url.host);
			return AddressBookDownloadResult::Failed;
		}
		else
			m_Ident = addr->identHash;
		// save url parts for later use
		std::string dest_host = url.host;
		int dest_port = url.port ? url.port : 80;
		// try to create stream to addressbook site
		auto stream = i2p::client::context.GetSharedLocalDestination ()->CreateStream (
			m_Ident, static_cast<uint16_t> (dest_port));
		if (!stream)
		{
			LogPrint (eLogWarning, "Addressbook: LeaseSet for address ", url.host, " not found");
			return AddressBookDownloadResult::Failed;
		}
		i2p::modern::ScopeExit d (
			[stream] () mutable
			{
				// destroy stream in destination's thread on exit
				boost::asio::post (
					i2p::client::context.GetSharedLocalDestination ()->GetService (), [s = std::move (stream)] () { });
			});
		if (m_Etag.empty () && m_LastModified.empty ())
		{
			m_Book.GetEtag (m_Ident, m_Etag, m_LastModified);
			LogPrint (eLogDebug, "Addressbook: Loaded for ", url.host, ": ETag: ", m_Etag,
				", Last-Modified: ", m_LastModified);
		}
		// create http request & send it
		boost::beast::http::request<boost::beast::http::string_body> req (
			boost::beast::http::verb::get, url.to_string (true), 11); // HTTP 1.1
		req.set (boost::beast::http::field::host, dest_host);
		req.set (boost::beast::http::field::user_agent, "Wget/1.11.4");
		req.set (boost::beast::http::field::accept_encoding, "gzip");
		req.set ("X-Accept-Encoding", "x-i2p-gzip;q=1.0, identity;q=0.5, deflate;q=0, gzip;q=0, *;q=0");
		if (!m_Etag.empty ()) req.set (boost::beast::http::field::if_none_match, m_Etag); // If-None-Match
		if (!m_LastModified.empty ())
			req.set (boost::beast::http::field::if_modified_since, m_LastModified); // If-Modified-Since
		req.keep_alive (false); // Connection: close

		i2p::client::BoostAsyncStream httpStream (stream, SUBSCRIPTION_REQUEST_TIMEOUT);
		std::future<size_t> sending = boost::beast::http::async_write (httpStream, req, boost::asio::use_future);
		try
		{
			size_t bytes_transferred = sending.get ();
			if (!bytes_transferred)
			{
				LogPrint (eLogWarning, "Addressbook: HTTP request to ", dest_host, " was not sent");
				return AddressBookDownloadResult::Failed;
			}
		}
		catch (std::exception & ex)
		{
			LogPrint (eLogWarning, "Addressbook: Send HTTP request exception: ", ex.what ());
			return AddressBookDownloadResult::Failed;
		}
		boost::beast::flat_buffer receiveBuffer { MAX_SUBSCRIPTION_BODY };
		boost::beast::http::response<boost::beast::http::string_body> res;
		std::future<size_t> receiving
			= boost::beast::http::async_read (httpStream, receiveBuffer, res, boost::asio::use_future);
		try
		{
			size_t bytes_transferred = receiving.get ();
			if (!bytes_transferred)
			{
				LogPrint (eLogWarning, "Addressbook: HTTP response from ", dest_host, " was not received");
				return AddressBookDownloadResult::Failed;
			}
		}
		catch (std::exception & ex)
		{
			LogPrint (eLogWarning, "Addressbook: Receive HTTP response exception: ", ex.what ());
			return AddressBookDownloadResult::Failed;
		}
		stream->AsyncClose ();
		// check result
		if (res.result () != boost::beast::http::status::ok)
		{
			if (res.result_int () == 304)
			{
				// Not Modified: the cached book is current. This is not a failure,
				// so the ETag/Last-Modified stay as they are and the caller keeps
				// the normal refresh cadence.
				LogPrint (eLogInfo, "Addressbook: No updates from ", dest_host, ", code 304");
				return AddressBookDownloadResult::NotModified;
			}
			LogPrint (eLogWarning, "Addressbook: Response code ", res.result_int (), " from ", dest_host);
			return AddressBookDownloadResult::Failed;
		}
		// update Etag and Last-Modified
		auto eTag = res[boost::beast::http::field::etag];
		if (!eTag.empty ()) m_Etag = std::string (eTag);
		auto lastModified = res[boost::beast::http::field::last_modified];
		if (!lastModified.empty ()) m_LastModified = std::string (lastModified);
		// parse response
		LogPrint (eLogInfo, "Addressbook: Got update from ", dest_host);
		if (boost::iequals (res[boost::beast::http::field::content_encoding], "gzip")
			|| boost::iequals (res[boost::beast::http::field::content_encoding], "x-i2p-gzip"))
		{
			std::vector<uint8_t> unzipped (MAX_SUBSCRIPTION_BODY);
			i2p::data::GzipInflator inflator;
			size_t unzippedLen = 0;
			if (!inflator.InflateChecked ((const uint8_t *)res.body ().data (), res.body ().size (), unzipped.data (),
					unzipped.size (), unzippedLen))
			{
				LogPrint (eLogError, "Addressbook: Can't gunzip http response");
				return AddressBookDownloadResult::Failed;
			}
			m_Book.LoadHostsFromStream (
				std::stringstream (std::string (reinterpret_cast<const char *> (unzipped.data ()), unzippedLen)), true,
				dest_host);
		}
		else
			m_Book.LoadHostsFromStream (std::stringstream (res.body ()), true, dest_host);
		return AddressBookDownloadResult::Updated;
	}

	AddressResolver::AddressResolver (std::shared_ptr<ClientDestination> destination)
		: m_LocalDestination (destination)
	{
		if (m_LocalDestination)
		{
			auto datagram = m_LocalDestination->GetDatagramDestination ();
			if (!datagram) datagram = m_LocalDestination->CreateDatagramDestination ();
			m_RequestReceiver = datagram->SetReceiver (
				std::bind (&AddressResolver::HandleRequest, this, std::placeholders::_1, std::placeholders::_2,
					std::placeholders::_3, std::placeholders::_4, std::placeholders::_5),
				ADDRESS_RESOLVER_DATAGRAM_PORT);
		}
	}

	AddressResolver::~AddressResolver ()
	{
		m_RequestReceiver.Reset ();
	}

	void AddressResolver::HandleRequest (
		const i2p::data::IdentityEx & from, uint16_t fromPort, uint16_t toPort, const uint8_t * buf, size_t len)
	{
		if (len < 9 || len < buf[8] + 9U)
		{
			LogPrint (eLogError, "Addressbook: Address request is too short ", len);
			return;
		}
		// read requested address
		uint8_t l = buf[8];
		char address[256];
		memcpy (address, buf + 9, l);
		address[l] = 0;
		LogPrint (eLogDebug, "Addressbook: Address request ", address);
		// send response
		uint8_t response[44];
		memset (response, 0, 4); // reserved
		memcpy (response + 4, buf + 4, 4); // nonce
		auto it = m_LocalAddresses.find (NormalizeHostName (std::string_view (address, l)));
		if (it != m_LocalAddresses.end ())
			memcpy (response + 8, it->second, 32); // ident
		else
			memset (response + 8, 0, 32); // not found
		memset (response + 40, 0, 4); // set expiration time to zero
		m_LocalDestination->GetDatagramDestination ()->SendDatagramTo (
			response, 44, from.GetIdentHash (), toPort, fromPort);
	}

	void AddressResolver::AddAddress (const std::string & name, const i2p::data::IdentHash & ident)
	{
		m_LocalAddresses[NormalizeHostName (name)] = ident;
	}

}
}

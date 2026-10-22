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
 */

#include <string.h>
#include <inttypes.h>
#include <algorithm>
#include <cctype>
#include <string>
#include <unordered_map>
#include <filesystem>
#include <fstream>
#include <chrono>
#include <future>
#include <utility>
#include <openssl/rand.h>
#include <boost/algorithm/string.hpp>
#include "Base.h"
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
			labelEnd = name.size () - (HasSuffixIgnoreCase (name, b33AltSuffix) ? b33AltSuffix.size () : b33Suffix.size ());
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

	bool IsValidHostName (std::string_view name)
	{
		auto normalized = NormalizeHostName (name);
		if (normalized.size () < 5 || normalized.size () > 67) return false;
		if (!normalized.ends_with (".i2p")) return false;
		if (normalized.ends_with (".b32.i2p") || normalized.ends_with (".b33.i2p")) return false;
		auto core = std::string_view (normalized).substr (0, normalized.size () - 4);
		if (core.empty () || core.front () == '.' || core.front () == '-') return false;
		if (core.find ("..") != std::string_view::npos || core.find (".-") != std::string_view::npos
			|| core.find ("-.") != std::string_view::npos)
			return false;
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

	bool AddressBook::IsValidDeletedPattern (std::string_view pattern)
	{
		if (pattern.size () < 5 || pattern.size () > 255 || !pattern.ends_with (".i2p")) return false;
		if (pattern.front () == '.' || pattern.front () == '-') return false;
		if (pattern.find ("..") != std::string_view::npos || pattern.find (".-") != std::string_view::npos
			|| pattern.find ("-.") != std::string_view::npos)
			return false;
		for (char ch : pattern)
		{
			if ((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '.' || ch == '-'
				|| ch == '*' || ch == '_' || ch == '~' || (ch >= 'A' && ch <= 'Z'))
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
		std::erase_if (m_DeletedHosts, [name] (const std::string & pattern)
			{ return MatchesDeletedPattern (pattern, name); });
		return before != m_DeletedHosts.size ();
	}

	bool AddressBook::SaveDeletedHostsLocked ()
	{
		return m_Storage && m_Storage->SaveDeletedHosts (m_DeletedHosts);
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

	void detail::AddressLookupRequests::Prune (uint64_t now)
	{
		std::erase_if (m_Requests, [now] (const auto & entry)
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

	private:
		int LoadFromFile (const std::string & filename,
			Addresses & addresses); // returns -1 if can't open file, otherwise number of records

	private:
		i2p::fs::HashedStorage storage;
		std::string etagsPath, indexPath, localPath, deletedPath;
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
		{
			std::lock_guard<std::mutex> l (m_FullAddressCacheMutex);
			m_FullAddressCache.try_emplace (ident, buf, ts);
		}
		return std::make_shared<i2p::data::IdentityEx> (buf.data (), length);
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
			std::ofstream f (path, std::ofstream::binary | std::ofstream::out);
			if (!f.is_open ())
			{
				LogPrint (eLogError, "Addressbook: Can't open file ", path);
				return;
			}
			f.write ((const char *)buf.data (), len);
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
		const std::string indexTemp = indexPath + ".tmp";
		{
			// Replace the index only after a complete write. Truncating the
			// live file first could leave an empty index after ENOSPC or a
			// crash, which the loader would then treat as authoritative.
			std::ofstream f (indexTemp, std::ofstream::out | std::ofstream::trunc); // in text mode
			if (!f.is_open ())
			{
				LogPrint (eLogWarning, "Addressbook: Can't open ", indexTemp);
				return -1;
			}
			for (const auto & it : addresses)
			{
				if (it.second->IsValid ())
				{
					f << it.first << ",";
					if (it.second->IsIdentHash ())
						f << it.second->identHash.ToBase32 ();
					else
						f << it.second->blindedPublicKey->ToB33 ();
					f << std::endl;
					num++;
				}
				else
					LogPrint (eLogWarning, "Addressbook: Invalid address ", it.first);
			}
			if (!f)
			{
				LogPrint (eLogWarning, "Addressbook: Could not write ", indexTemp);
				std::error_code ignored;
				std::filesystem::remove (indexTemp, ignored);
				return -1;
			}
		}

		if (!m_HostsFile.empty ())
		{
			// dump full hosts.txt
			std::ofstream f (m_HostsFile, std::ofstream::out); // in text mode
			if (!f.is_open ())
			{
				LogPrint (eLogWarning, "Addressbook: Can't open ", m_HostsFile);
				std::error_code ignored;
				std::filesystem::remove (indexTemp, ignored);
				return -1;
			}
			for (const auto & it : addresses)
			{
				std::shared_ptr<const i2p::data::IdentityEx> addr;
				if (it.second->IsIdentHash ())
				{
					addr = GetAddress (it.second->identHash);
					if (addr) f << it.first << "=" << addr->ToBase64 () << std::endl;
				}
			}
			if (!f)
			{
				LogPrint (eLogWarning, "Addressbook: Could not write ", m_HostsFile);
				std::error_code ignored;
				std::filesystem::remove (indexTemp, ignored);
				return -1;
			}
		}

		std::error_code renameError;
		std::filesystem::rename (indexTemp, indexPath, renameError);
		if (renameError)
		{
			// Windows rename does not replace an existing destination.
			std::error_code ignored;
			std::filesystem::remove (indexPath, ignored);
			renameError.clear ();
			std::filesystem::rename (indexTemp, indexPath, renameError);
		}
		if (renameError)
		{
			LogPrint (eLogWarning, "Addressbook: Could not replace ", indexPath, ": ", renameError.message ());
			std::error_code ignored;
			std::filesystem::remove (indexTemp, ignored);
			return -1;
		}
		LogPrint (eLogInfo, "Addressbook: ", num, " addresses saved");

		return num;
	}

	void AddressBookFilesystemStorage::SaveEtag (
		const i2p::data::IdentHash & subscription, const std::string & etag, const std::string & lastModified)
	{
		std::string fname = etagsPath + i2p::fs::dirSep + subscription.ToBase32 () + ".txt";
		std::ofstream f (fname, std::ofstream::out | std::ofstream::trunc);
		if (f)
		{
			f << etag << std::endl;
			f << lastModified << std::endl;
		}
	}

	bool AddressBookFilesystemStorage::GetEtag (
		const i2p::data::IdentHash & subscription, std::string & etag, std::string & lastModified)
	{
		std::string fname = etagsPath + i2p::fs::dirSep + subscription.ToBase32 () + ".txt";
		std::ifstream f (fname, std::ofstream::in);
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
			if (std::find (patterns.begin (), patterns.end (), line) == patterns.end ())
				patterns.push_back (line);
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
		const std::string temporary = deletedPath + ".tmp";
		{
			std::ofstream f (temporary, std::ofstream::out | std::ofstream::trunc);
			if (!f.is_open ())
			{
				LogPrint (eLogWarning, "Addressbook: Can't open ", temporary);
				return false;
			}
			f << "# Deleted address-book hosts; one name or wildcard pattern per line.\n";
			size_t count = 0;
			size_t written = 0;
			for (const auto & pattern : patterns)
			{
				if (count++ >= 256 || pattern.empty () || pattern.size () > 255) continue;
				if (written + pattern.size () + 1 > 64 * 1024 - 128) break;
				f << pattern << '\n';
				written += pattern.size () + 1;
			}
			if (!f)
			{
				LogPrint (eLogWarning, "Addressbook: Could not write ", temporary);
				std::error_code ignored;
				std::filesystem::remove (temporary, ignored);
				return false;
			}
		}
		std::error_code renameError;
		std::filesystem::rename (temporary, deletedPath, renameError);
		if (renameError)
		{
			std::error_code ignored;
			std::filesystem::remove (deletedPath, ignored);
			renameError.clear ();
			std::filesystem::rename (temporary, deletedPath, renameError);
		}
		if (renameError)
		{
			LogPrint (eLogWarning, "Addressbook: Could not replace ", deletedPath, ": ", renameError.message ());
			std::error_code ignored;
			std::filesystem::remove (temporary, ignored);
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
		StopLookups ();
		StopSubscriptions ();
		if (m_SubscriptionsUpdateTimer)
		{
			m_SubscriptionsUpdateTimer->cancel ();
			m_SubscriptionsUpdateTimer = nullptr;
		}
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
		m_Subscriptions.clear ();
	}

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
			if (!m_IsEnabled) return nullptr;
			auto addr = FindAddress (normalized);
			if (!addr && normalized.size () > 8 && normalized.compare (0, 4, "www.") == 0)
				addr = FindAddress (normalized.substr (4)); // Java www. fallback, local only
			if (!addr) LookupAddress (normalized); // TODO:
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
			auto ident = std::make_shared<i2p::data::IdentityEx> ();
			if (ident->FromBase64 (jump))
			{
				if (m_Storage) m_Storage->AddAddress (ident);
				m_Addresses[key] = std::make_shared<Address> (ident->GetIdentHash ());
				LogPrint (eLogInfo, "Addressbook: Added ", address, " -> ", ToAddress (ident->GetIdentHash ()));
			}
			else
				LogPrint (eLogError, "Addressbook: Malformed address ", jump);
		}
		if (m_Storage) m_Storage->Save (m_Addresses);
	}

	void AddressBook::InsertFullAddress (std::shared_ptr<const i2p::data::IdentityEx> address)
	{
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

	AddressBookMutationResult AddressBook::AddAddress (
		std::string_view name, std::string_view destination, bool replace)
	{
		if (!IsValidHostName (name)) return AddressBookMutationResult::InvalidName;
		if (destination.empty () || destination.size () > 1024)
			return AddressBookMutationResult::InvalidDestination;
		auto identity = std::make_shared<i2p::data::IdentityEx> ();
		if (!identity->FromBase64 (std::string (destination)))
			return AddressBookMutationResult::InvalidDestination;
		const auto key = NormalizeHostName (name);
		const auto hash = identity->GetIdentHash ();

		std::lock_guard<std::mutex> l (m_AddressBookMutex);
		if (!m_Storage) return AddressBookMutationResult::Unavailable;
		auto it = m_Addresses.find (key);
		if (it != m_Addresses.end () && it->second)
		{
			if (it->second->IsIdentHash () && it->second->identHash == hash)
			{
				if (RemoveDeletedMatchesLocked (key) && !SaveDeletedHostsLocked ())
					LogPrint (eLogWarning, "Addressbook: Could not persist cleared deleted-host pattern for ", key);
				return AddressBookMutationResult::Ok;
			}
			if (!replace) return AddressBookMutationResult::Exists;
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
			return AddressBookMutationResult::Unavailable;
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
		if (RemoveDeletedMatchesLocked (key) && !SaveDeletedHostsLocked ())
			LogPrint (eLogWarning, "Addressbook: Could not persist cleared deleted-host pattern for ", key);
		LogPrint (eLogInfo, "Addressbook: Added ", key, " -> ", hash.ToBase32 ());
		return AddressBookMutationResult::Ok;
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
		const int indexed = m_Storage->Load (m_Addresses);
		if (indexed >= 0)
		{
			// An existing empty index is authoritative: the last entry was
			// explicitly removed and must not be resurrected from hosts.txt.
			m_IsLoaded = true;
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
		std::unique_lock<std::mutex> l (m_AddressBookMutex);
		int numAddresses = 0;
		bool incomplete = false;
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

				size_t commentPos = addr.find ('#');
				if (commentPos != addr.npos) addr = addr.substr (0, commentPos); // remove comments
				if (!IsValidHostName (name))
				{
					LogPrint (eLogWarning, "Addressbook: Invalid subscription host name ", name);
					continue;
				}
				// The map key is the normalised name; the original spelling is
				// kept in diagnostics (and in the jump-link writer's output).
				auto key = NormalizeHostName (name);
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

				auto ident = std::make_shared<i2p::data::IdentityEx> ();
				if (!ident->FromBase64 (addr))
				{
					LogPrint (eLogError, "Addressbook: Malformed address ", addr, " for ", name);
					incomplete = f.eof ();
					continue;
				}
				numAddresses++;
				auto it = m_Addresses.find (key);
				if (it != m_Addresses.end ()) // already exists ?
				{
					if (it->second->IsIdentHash () && it->second->identHash != ident->GetIdentHash ()
						&& // address changed?
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
					m_Addresses.emplace (key, std::make_shared<Address> (ident->GetIdentHash ()));
					if (m_Storage) m_Storage->AddAddress (ident);
					if (is_update) LogPrint (eLogInfo, "Addressbook: Added new host: ", name);
				}
			}
			else
				incomplete = f.eof ();
		}
		if (f.bad ()) incomplete = true;
		LogPrint (eLogInfo, "Addressbook: ", numAddresses, " addresses processed");
		if (numAddresses > 0)
		{
			if (!incomplete) m_IsLoaded = true;
			if (m_Storage) m_Storage->Save (m_Addresses);
		}
		return !incomplete;
	}

	void AddressBook::LoadSubscriptions ()
	{
		if (!m_Subscriptions.size ())
		{
			std::ifstream f (i2p::fs::DataDirPath ("subscriptions.txt"), std::ifstream::in); // in text mode
			if (f.is_open ())
			{
				std::string s;
				while (std::getline (f, s))
				{
					if (s.empty () || s[0] == '#') continue; // skip empty line or comment
					m_Subscriptions.push_back (std::make_shared<AddressBookSubscription> (*this, s));
				}
				if (f.bad ()) LogPrint (eLogError, "Addressbook: I/O error while reading subscriptions.txt");
				LogPrint (eLogInfo, "Addressbook: ", m_Subscriptions.size (), " subscriptions urls loaded");
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
					if (!s.empty ()) m_Subscriptions.push_back (std::make_shared<AddressBookSubscription> (*this, s));
				LogPrint (eLogInfo, "Addressbook: ", m_Subscriptions.size (), " subscriptions urls loaded");
			}
		}
		else
			LogPrint (eLogError, "Addressbook: Subscriptions already loaded");
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
		m_NumRetries++;
		int nextUpdateTimeout = m_NumRetries * CONTINIOUS_SUBSCRIPTION_RETRY_TIMEOUT;
		if (m_NumRetries > CONTINIOUS_SUBSCRIPTION_MAX_NUM_RETRIES
			|| nextUpdateTimeout > CONTINIOUS_SUBSCRIPTION_UPDATE_TIMEOUT)
			nextUpdateTimeout = CONTINIOUS_SUBSCRIPTION_UPDATE_TIMEOUT;
		if (success)
		{
			m_NumRetries = 0;
			if (m_DefaultSubscription) m_DefaultSubscription = nullptr;
			if (m_IsLoaded)
				nextUpdateTimeout = CONTINIOUS_SUBSCRIPTION_UPDATE_TIMEOUT;
			else
				m_IsLoaded = true;
			if (m_Storage) m_Storage->SaveEtag (subscription, etag, lastModified);
		}
		if (m_SubscriptionsUpdateTimer)
		{
			m_SubscriptionsUpdateTimer->expires_after (std::chrono::minutes (nextUpdateTimeout));
			m_SubscriptionsUpdateTimer->async_wait (
				std::bind (&AddressBook::HandleSubscriptionsUpdateTimer, this, std::placeholders::_1));
		}
	}

	void AddressBook::StartSubscriptions ()
	{
		LoadSubscriptions ();
		if (m_IsLoaded && m_Subscriptions.empty ()) return;

		auto dest = i2p::client::context.GetSharedLocalDestination ();
		if (dest)
		{
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
		if (m_SubscriptionsUpdateTimer) m_SubscriptionsUpdateTimer->cancel ();
	}

	void AddressBook::ResetTimers ()
	{
		// both timers run on the shared local destination's service, so they must be
		// created anew when that destination is replaced
		auto dest = i2p::client::context.GetSharedLocalDestination ();
		if (m_SubscriptionsUpdateTimer)
		{
			m_SubscriptionsUpdateTimer->cancel ();
			m_SubscriptionsUpdateTimer = nullptr;
			if (dest)
			{
				m_SubscriptionsUpdateTimer = std::make_unique<boost::asio::steady_timer> (dest->GetService ());
				m_SubscriptionsUpdateTimer->expires_after (std::chrono::minutes (INITIAL_SUBSCRIPTION_UPDATE_TIMEOUT));
				m_SubscriptionsUpdateTimer->async_wait (
					std::bind (&AddressBook::HandleSubscriptionsUpdateTimer, this, std::placeholders::_1));
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
				else if (!m_Subscriptions.empty ())
				{
					// pick random subscription
					auto ind = i2p::crypto::RandomBelow (static_cast<uint32_t> (m_Subscriptions.size ()));
					m_Downloading = std::async (
						std::launch::async, std::bind (&AddressBookSubscription::CheckUpdates, m_Subscriptions[ind]));
				}
			}
			else
			{
				// try it again later
				m_SubscriptionsUpdateTimer->expires_after (std::chrono::minutes (INITIAL_SUBSCRIPTION_RETRY_TIMEOUT));
				m_SubscriptionsUpdateTimer->async_wait (
					std::bind (&AddressBook::HandleSubscriptionsUpdateTimer, this, std::placeholders::_1));
			}
		}
	}

	void AddressBook::StartLookups ()
	{
		auto dest = i2p::client::context.GetSharedLocalDestination ();
		if (dest)
		{
			auto datagram = dest->GetDatagramDestination ();
			if (!datagram) datagram = dest->CreateDatagramDestination ();
			datagram->SetReceiver (
				std::bind (&AddressBook::HandleLookupResponse, this, std::placeholders::_1, std::placeholders::_2,
					std::placeholders::_3, std::placeholders::_4, std::placeholders::_5),
				ADDRESS_RESPONSE_DATAGRAM_PORT);
		}
	}

	void AddressBook::StopLookups ()
	{
		m_Lookups.Clear ();
		auto dest = i2p::client::context.GetSharedLocalDestination ();
		if (dest)
		{
			auto datagram = dest->GetDatagramDestination ();
			if (datagram) datagram->ResetReceiver (ADDRESS_RESPONSE_DATAGRAM_PORT);
		}
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

		bool result = MakeRequest ();
		m_Book.DownloadComplete (result, m_Ident, m_Etag, m_LastModified);
	}

	bool AddressBookSubscription::MakeRequest ()
	{
		i2p::http::URL url;
		// must be run in separate thread
		LogPrint (eLogInfo, "Addressbook: Downloading hosts database from ", m_Link);
		if (!url.parse (m_Link))
		{
			LogPrint (eLogError, "Addressbook: Failed to parse url: ", m_Link);
			return false;
		}
		auto addr = m_Book.GetAddress (url.host);
		if (!addr || !addr->IsIdentHash ())
		{
			LogPrint (eLogError, "Addressbook: Can't resolve ", url.host);
			return false;
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
			return false;
		}
		i2p::util::Defer d (
			[stream] ()
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
				return false;
			}
		}
		catch (std::exception & ex)
		{
			LogPrint (eLogWarning, "Addressbook: Send HTTP request exception: ", ex.what ());
			return false;
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
				return false;
			}
		}
		catch (std::exception & ex)
		{
			LogPrint (eLogWarning, "Addressbook: Receive HTTP response exception: ", ex.what ());
			return false;
		}
		stream->AsyncClose ();
		// check result
		if (res.result () != boost::beast::http::status::ok)
		{
			if (res.result_int () == 304)
				LogPrint (eLogInfo, "Addressbook: No updates from ", dest_host, ", code 304");
			else
				LogPrint (eLogWarning, "Addressbook: Response code ", res.result_int (), " from ", dest_host);
			return false;
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
			if (!inflator.InflateChecked ((const uint8_t *)res.body ().data (), res.body ().size (),
					unzipped.data (), unzipped.size (), unzippedLen))
			{
				LogPrint (eLogError, "Addressbook: Can't gunzip http response");
				return false;
			}
			m_Book.LoadHostsFromStream (
				std::stringstream (std::string (reinterpret_cast<const char *> (unzipped.data ()), unzippedLen)), true);
		}
		else
			m_Book.LoadHostsFromStream (std::stringstream (res.body ()), true);
		return true;
	}

	AddressResolver::AddressResolver (std::shared_ptr<ClientDestination> destination)
		: m_LocalDestination (destination)
	{
		if (m_LocalDestination)
		{
			auto datagram = m_LocalDestination->GetDatagramDestination ();
			if (!datagram) datagram = m_LocalDestination->CreateDatagramDestination ();
			datagram->SetReceiver (
				std::bind (&AddressResolver::HandleRequest, this, std::placeholders::_1, std::placeholders::_2,
					std::placeholders::_3, std::placeholders::_4, std::placeholders::_5),
				ADDRESS_RESOLVER_DATAGRAM_PORT);
		}
	}

	AddressResolver::~AddressResolver ()
	{
		if (m_LocalDestination)
		{
			auto datagram = m_LocalDestination->GetDatagramDestination ();
			if (datagram) datagram->ResetReceiver (ADDRESS_RESOLVER_DATAGRAM_PORT);
		}
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

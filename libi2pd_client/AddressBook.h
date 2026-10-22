/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: AddressBook.
 *
 * Declares the hosts.txt/subscription address book, its pluggable storage
 * backend and the per-destination datagram resolver for .i2p names.
 *
 */

#ifndef ADDRESS_BOOK_H__
#define ADDRESS_BOOK_H__

#include <string.h>
#include <cstdint>
#include <string>
#include <string_view>
#include <map>
#include <vector>
#include <iostream>
#include <mutex>
#include <future>
#include <memory>
#include <optional>
#include <boost/asio.hpp>
#include "Base.h"
#include "Identity.h"
#include "Log.h"
#include "Destination.h"
#include "LeaseSet.h"

namespace i2p
{
namespace client
{
	const int INITIAL_SUBSCRIPTION_UPDATE_TIMEOUT = 3; // in minutes
	const int INITIAL_SUBSCRIPTION_RETRY_TIMEOUT = 1; // in minutes
	const int CONTINIOUS_SUBSCRIPTION_UPDATE_TIMEOUT = 720; // in minutes (12 hours)
	const int CONTINIOUS_SUBSCRIPTION_RETRY_TIMEOUT = 5; // in minutes
	const int CONTINIOUS_SUBSCRIPTION_MAX_NUM_RETRIES = 10; // then update timeout
	const int SUBSCRIPTION_REQUEST_TIMEOUT = 120; //in seconds
	const int ADDRESS_CACHE_EXPIRATION_TIMEOUT = 710; // in seconds
	const int ADDRESS_CACHE_UPDATE_INTERVAL = 76; // in seconds

	const uint16_t ADDRESS_RESOLVER_DATAGRAM_PORT = 53;
	const uint16_t ADDRESS_RESPONSE_DATAGRAM_PORT = 54;

	const size_t B33_ADDRESS_THRESHOLD = 52; // characters

	struct Address
	{
		enum
		{
			eAddressIndentHash,
			eAddressBlindedPublicKey,
			eAddressInvalid
		} addressType;
		i2p::data::IdentHash identHash;
		std::shared_ptr<i2p::data::BlindedPublicKey> blindedPublicKey;

		Address (std::string_view b32);
		Address (const i2p::data::IdentHash & hash);
		bool IsIdentHash () const { return addressType == eAddressIndentHash; };
		bool IsValid () const { return addressType != eAddressInvalid; };
		std::string ToBase32 () const;

		bool operator== (const Address & other) const;
	};

	inline std::string GetB32Address (const i2p::data::IdentHash & ident)
	{
		return ident.ToBase32 ().append (".b32.i2p");
	}

	// Host names in the address book are case-insensitive: Java lowercases the
	// name in its naming service and before the ".b32.i2p" check. Returns an
	// ASCII-lowercase copy suitable as a lookup/insertion key. The current
	// administration listing returns that normalized key; diagnostics may keep
	// the original spelling. Only 'A'-'Z' are folded so the result does not
	// depend on the process locale, and a b33 label (URL-safe
	// base64, case-sensitive) is preserved byte-for-byte.
	bool HasSuffixIgnoreCase (std::string_view value, std::string_view suffix);
	bool IsI2PHost (std::string_view host);
	std::string NormalizeHostName (std::string_view name);

	// Local, bounded address-book administration. Names use the ASCII rules
	// expected by the Java address book; destinations are full base64
	// identities, never paths, URLs or key filenames.
	bool IsValidHostName (std::string_view name);

	enum class AddressBookMutationResult : uint8_t
	{
		Ok = 0,
		InvalidName,
		InvalidDestination,
		Exists,
		NotFound,
		Unavailable
	};

	struct AddressBookEntry
	{
		std::string name;
		std::string destination; // b32 or b33 label; display-only
		bool blinded = false;
	};

	namespace detail
	{
		// Correlate authenticated datagram replies with a bounded, short-lived
		// request. The nonce alone never grants authority to change a name.
		class AddressLookupRequests
		{
		public:
			static constexpr size_t MAX_PENDING = 256;
			static constexpr uint64_t TIMEOUT_SECONDS = 120;
			bool Insert (uint32_t nonce, std::string_view name, const i2p::data::IdentHash & resolver, uint64_t now);
			std::optional<std::string> Take (uint32_t nonce, const i2p::data::IdentHash & resolver, uint64_t now);
			void Clear ();

		private:
			struct Request
			{
				std::string Name;
				i2p::data::IdentHash Resolver;
				uint64_t Created;
			};
			void Prune (uint64_t now); // caller holds m_Mutex
			std::mutex m_Mutex;
			std::map<uint32_t, Request> m_Requests;
		};
	}

	class AddressBookStorage // interface for storage
	{
	public:
		typedef std::map<std::string, std::shared_ptr<Address>, std::less<>> Addresses;

		virtual ~AddressBookStorage () { };
		virtual std::shared_ptr<const i2p::data::IdentityEx> GetAddress (const i2p::data::IdentHash & ident) = 0;
		virtual void AddAddress (std::shared_ptr<const i2p::data::IdentityEx> address) = 0;
		virtual void RemoveAddress (const i2p::data::IdentHash & ident) = 0;
		virtual void CleanUpCache () = 0;

		virtual bool Init () = 0;
		virtual int Load (Addresses & addresses) = 0;
		virtual int LoadLocal (Addresses & addresses) = 0;
		virtual int Save (const Addresses & addresses) = 0;

		virtual void SaveEtag (
			const i2p::data::IdentHash & subscription, const std::string & etag, const std::string & lastModified) = 0;
		virtual bool GetEtag (const i2p::data::IdentHash & subscription, std::string & etag, std::string & lastModified)
			= 0;
		virtual void ResetEtags () = 0;

		// Deleted-host tombstones are a small, read-only-at-startup policy
		// list. The default implementation keeps custom storages usable; the
		// filesystem backend persists them in deleted.txt.
		virtual bool LoadDeletedHosts (std::vector<std::string> & patterns) { (void) patterns; return false; }
		virtual bool SaveDeletedHosts (const std::vector<std::string> & patterns)
		{
			(void) patterns;
			return true;
		}
	};

	class AddressBookSubscription;
	class AddressResolver;
	class AddressBook
	{
	public:
		explicit AddressBook (std::unique_ptr<AddressBookStorage> storage = nullptr);
		~AddressBook ();
		void Start ();
		void ResetTimers (); // after the shared local destination is replaced
		void StartResolvers ();
		void Stop ();
		std::shared_ptr<const Address> GetAddress (std::string_view address);
		std::shared_ptr<const i2p::data::IdentityEx> GetFullAddress (const std::string & address);
		std::shared_ptr<const Address> FindAddress (std::string_view address);
		void LookupAddress (std::string_view address);
		void InsertAddress (const std::string & address, const std::string & jump); // for jump links
		void InsertFullAddress (std::shared_ptr<const i2p::data::IdentityEx> address);

		bool RecordExists (const std::string & address, const std::string & jump);

		// Administrative list/mutation helpers. They copy only names and
		// public labels; identity files and storage paths never cross the
		// local administration boundary.
		std::vector<AddressBookEntry> GetEntries (size_t limit);
		AddressBookMutationResult AddAddress (
			std::string_view name, std::string_view destination, bool replace);
		AddressBookMutationResult RemoveAddress (std::string_view name);
		// Deleted-host tombstones: subscription merges must not resurrect a
		// name the user removed. Patterns are bounded, ASCII-only names with
		// optional '*' wildcards; an explicit Add wins and clears a match.
		std::vector<std::string> GetDeletedHosts (size_t limit);
		AddressBookMutationResult BlockAddress (std::string_view name);
		AddressBookMutationResult UnblockAddress (std::string_view name);

		bool LoadHostsFromStream (std::istream && f, bool is_update);
		void DownloadComplete (bool success, const i2p::data::IdentHash & subscription, const std::string & etag,
			const std::string & lastModified);
		//This method returns the ".b32.i2p" address
		std::string ToAddress (const i2p::data::IdentHash & ident) { return GetB32Address (ident); }
		std::string ToAddress (std::shared_ptr<const i2p::data::IdentityEx> ident)
		{
			return ToAddress (ident->GetIdentHash ());
		}

		bool GetEtag (const i2p::data::IdentHash & subscription, std::string & etag, std::string & lastModified);
		bool IsEnabled () const { return m_IsEnabled; }

	private:
		void StartSubscriptions ();
		void StopSubscriptions ();

		void LoadHosts ();
		void LoadDeletedHosts ();
		void LoadSubscriptions ();
		void LoadLocal ();

		static bool IsValidDeletedPattern (std::string_view pattern);
		static bool MatchesDeletedPattern (std::string_view pattern, std::string_view name);
		bool IsDeletedHostLocked (std::string_view name) const;
		bool RemoveDeletedMatchesLocked (std::string_view name);
		bool SaveDeletedHostsLocked ();

		void HandleSubscriptionsUpdateTimer (const boost::system::error_code & ecode);

		void StartLookups ();
		void StopLookups ();
		void HandleLookupResponse (
			const i2p::data::IdentityEx & from, uint16_t fromPort, uint16_t toPort, const uint8_t * buf, size_t len);

		void ScheduleCacheUpdate ();

	private:
		std::mutex m_AddressBookMutex;
		AddressBookStorage::Addresses m_Addresses;
		std::vector<std::string> m_DeletedHosts;
		std::map<i2p::data::IdentHash, std::shared_ptr<AddressResolver>> m_Resolvers; // local destination->resolver
		detail::AddressLookupRequests m_Lookups;
		std::unique_ptr<AddressBookStorage> m_Storage;
		volatile bool m_IsLoaded;
		std::future<void> m_Downloading;
		int m_NumRetries;
		std::vector<std::shared_ptr<AddressBookSubscription>> m_Subscriptions;
		std::shared_ptr<AddressBookSubscription> m_DefaultSubscription; // in case if we don't know any addresses yet
		std::unique_ptr<boost::asio::steady_timer> m_SubscriptionsUpdateTimer, m_AddressCacheUpdateTimer;
		bool m_IsEnabled;
	};

	class AddressBookSubscription
	{
	public:
		AddressBookSubscription (AddressBook & book, std::string_view link);
		void CheckUpdates ();

	private:
		bool MakeRequest ();

	private:
		AddressBook & m_Book;
		std::string m_Link, m_Etag, m_LastModified;
		i2p::data::IdentHash m_Ident;
		// m_Etag must be surrounded by ""
	};

	class AddressResolver
	{
	public:
		AddressResolver (std::shared_ptr<ClientDestination> destination);
		~AddressResolver ();
		void AddAddress (const std::string & name, const i2p::data::IdentHash & ident);

	private:
		void HandleRequest (
			const i2p::data::IdentityEx & from, uint16_t fromPort, uint16_t toPort, const uint8_t * buf, size_t len);

	private:
		std::shared_ptr<ClientDestination> m_LocalDestination;
		std::map<std::string, i2p::data::IdentHash> m_LocalAddresses;
	};
}
}

#endif

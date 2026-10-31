/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: AddressBook.
 *
 * Declares the hosts.txt/subscription address book, its pluggable
 * storage backend and the per-destination datagram resolver for .i2p
 * names.
 *
 * An automatic subscription merge never replaces a name that already
 * had a destination: only an explicit local add/replace may re-point
 * it. Subscription sources are bounded, credential-free I2P URLs that
 * the administration thread may list, add and remove; the destination's
 * io_context reloads them.
 *
 * A source may retract a name it created with the Java advanced
 * hosts.txt form "&lt;name&gt;=&lt;destination&gt;#!action=delete". To
 * honour that without letting a feed delete a locally curated name, the
 * book records which subscription first supplied each entry. The legacy
 * "name,destination" index columns are kept byte-compatible, so the
 * origin is persisted in a separate "entry-sources.txt" sidecar; a
 * missing or unreadable sidecar means no entry has a feed origin and
 * therefore no feed can delete anything.
 *
 */

#ifndef ADDRESS_BOOK_H__
#define ADDRESS_BOOK_H__

#include <string.h>
#include <atomic>
#include <chrono>
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
	// A 32-byte destination hash is 52 base32 characters. A final ".i2p"
	// label of exactly that length is rejected so a name cannot imitate
	// the "&lt;hash&gt;.b32.i2p" form.
	const size_t B32_HASH_LABEL_LENGTH = 52;
	// A b33 label of a 32-byte key is 56 base32 characters. The parser
	// rejects any non-canonical encoding, so this bound only stops an
	// oversized label from reaching it; it is not an accepted alternative
	// spelling.
	const size_t B33_LABEL_MAX_LENGTH = 128; // characters

	// Java bounds the base64 destination string itself (516..616
	// characters) and refuses a value outside that range before decoding
	// it; the decoded identity is then 387..462 bytes. One predicate keeps
	// the administrative, merge and jump-link paths on the same bound.
	const size_t MIN_DESTINATION_BASE64_LENGTH = 516;
	const size_t MAX_DESTINATION_BASE64_LENGTH = 616;
	const size_t MIN_IDENTITY_LENGTH = 387;
	const size_t MAX_IDENTITY_LENGTH = 462;
	bool IsValidDestinationLength (std::string_view base64);

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

	// Host names in the address book are case-insensitive: Java lowercases
	// the name in its naming service and before the ".b32.i2p" check.
	// Returns an ASCII-lowercase copy suitable as a lookup/insertion key.
	// The current administration listing returns that normalized key;
	// diagnostics may keep the original spelling. Only 'A'-'Z' are folded
	// so the result does not depend on the process locale, and a b33 label
	// (URL-safe base64, case-sensitive) is preserved byte-for-byte.
	bool HasSuffixIgnoreCase (std::string_view value, std::string_view suffix);
	bool IsI2PHost (std::string_view host);
	std::string NormalizeHostName (std::string_view name);

	// Local, bounded address-book administration. Names use the ASCII
	// rules expected by the Java address book; destinations are full
	// base64 identities, never paths, URLs or key filenames. Every label
	// is LDH-only, may not contain a doubled hyphen (the IDNA "xn--"
	// prefix position is reserved, and no I2P name needs one) and the
	// final label may not be exactly 52 base32 characters: that is the
	// length of a base32 destination hash and would let a name imitate the
	// "&lt;hash&gt;.b32.i2p" form.
	bool IsValidHostName (std::string_view name);

	// Subscription sources are fetched through the I2P HTTP client, so the
	// host must resolve to an .i2p destination: a subscription can never
	// become a cleartext fetch authority. The list is bounded,
	// credential-free and fragment-free. `error` receives a short,
	// non-secret diagnostic.
	const size_t MAX_SUBSCRIPTION_URLS = 16;
	const size_t MAX_SUBSCRIPTION_URL_LENGTH = 512; // bytes
	bool IsValidSubscriptionURL (std::string_view url, std::string & error);

	enum class AddressBookMutationResult : uint8_t
	{
		Ok = 0,
		InvalidName,
		InvalidDestination,
		Exists,
		NotFound,
		Unavailable
	};

	// Result of an administrative destination add. It is the mutation
	// result without NotFound: an add either stores, declines an existing
	// name or fails. A caller that must distinguish "kept the existing
	// destination" from "re-pointed it" uses AddAddressChecked().
	enum class AddressAddResult : uint8_t
	{
		Ok = 0,
		InvalidName,
		InvalidDestination,
		Exists,
		Unavailable
	};

	// Internal outcome of one subscription fetch. 304 Not Modified is
	// neither a failure nor an update: the stored ETag and the normal
	// refresh cadence are kept. The public DownloadComplete(bool, ...)
	// callback contract is unchanged; this tri-state only exists between
	// the fetch and the book.
	enum class AddressBookDownloadResult : uint8_t
	{
		Failed = 0,
		NotModified,
		Updated
	};

	struct AddressBookEntry
	{
		std::string name;
		std::string destination; // b32 or b33 label; display-only
		bool blinded = false;
	};

	namespace detail
	{
		// Correlate authenticated datagram replies with a bounded,
		// short-lived request. The nonce alone never grants authority to
		// change a name.
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

		// Bounded negative cache for a name that neither the book nor the
		// resolver could satisfy recently. Without it every lookup of an
		// unknown name repeats FindAddress and sends another resolver query.
		// Entries expire after TIMEOUT_SECONDS, the table is capped at
		// MAX_ENTRIES, and a b33/blinded label is never cached: it is decoded
		// locally and is never resolved through the book.
		class NegativeAddressCache
		{
		public:
			static constexpr size_t MAX_ENTRIES = 32;
			static constexpr uint64_t TIMEOUT_SECONDS = 60;

			// Both take the monotonic seconds value of the caller so the tests
			// are deterministic.
			bool IsCached (std::string_view name, uint64_t now) const;
			void Insert (std::string_view name, uint64_t now);
			void Erase (std::string_view name);
			void Clear ();
			size_t Size (uint64_t now) const;
			// A normalised name is cacheable when it is a resolvable host, i.e.
			// not empty, not oversized and not a blinded (b33) label.
			static bool IsCacheable (std::string_view normalized);

		private:
			void Prune (uint64_t now) const; // caller holds m_Mutex
			mutable std::mutex m_Mutex;
			mutable std::map<std::string, uint64_t, std::less<>> m_Entries;
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
		virtual bool LoadDeletedHosts (std::vector<std::string> & patterns)
		{
			(void)patterns;
			return false;
		}
		virtual bool SaveDeletedHosts (const std::vector<std::string> & patterns)
		{
			(void)patterns;
			return true;
		}

		// Per-name origin of an entry learned from a subscription feed, used
		// to decide whether "action=delete" may retract it. The persisted
		// index keeps the legacy "name,destination" columns, so the origin
		// lives in a separate sidecar file. A missing or unreadable sidecar
		// means no entry has a feed origin, and a feed can then delete
		// nothing: fail closed. The default implementation keeps custom
		// storages usable.
		virtual bool LoadAddressSources (std::map<std::string, std::string, std::less<>> & sources)
		{
			(void)sources;
			return false;
		}
		virtual bool SaveAddressSources (const std::map<std::string, std::string, std::less<>> & sources)
		{
			(void)sources;
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
		/// Resolve a name or destination for a local caller (proxy, tunnel or
		/// destination). Three input forms are accepted: a base32
		/// `&lt;hash&gt;.b32.i2p` label, an `.i2p`/`.b33.i2p` host name, and
		/// a full base64 destination. A `.i2p.alt` suffix is treated as a
		/// lookup-only alias for `.i2p`. A b32/b33 result is decoded locally
		/// and succeeds without any network activity; a host name that is not
		/// in the book triggers one bounded, non-blocking resolver query (see
		/// LookupAddress()) and returns `nullptr` for this call, so a caller
		/// must not treat a miss as terminal. A miss is negatively cached for
		/// a short, bounded time; a b33 label is never cached and never
		/// resolved through the local `www.` alias.
		std::shared_ptr<const Address> GetAddress (std::string_view address);
		std::shared_ptr<const i2p::data::IdentityEx> GetFullAddress (const std::string & address);
		/// Local-only lookup under the normalised (lower-case) key. It never
		/// sends a query, so it is the right call for a caller that must not
		/// create network side effects.
		std::shared_ptr<const Address> FindAddress (std::string_view address);
		/// Send one resolver query for an unqualified host name. The name
		/// must have a `.i2p` target already present in the book, its length
		/// must fit the single byte of the wire format, and the reply is
		/// correlated by a CSPRNG nonce that is bound to the queried name and
		/// the authenticated resolver identity: a reply can only inform,
		/// never authorize, a name change.
		void LookupAddress (std::string_view address);
		/// Store a jump-link result. The destination value is an identity,
		/// not a path or URL; an invalid length or a blocked (tombstoned)
		/// name is refused, and the value is never logged.
		void InsertAddress (const std::string & address, const std::string & jump); // for jump links
		void InsertFullAddress (std::shared_ptr<const i2p::data::IdentityEx> address);

		/// True when the stored destination for @a address equals the one in
		/// @a jump (accepted as a b32 label or a full base64 identity). Used
		/// to decide whether a jump link is new information before it is
		/// applied.
		bool RecordExists (const std::string & address, const std::string & jump);

		// Administrative list/mutation helpers. They copy only names and
		// public labels; identity files and storage paths never cross the
		// local administration boundary.
		std::vector<AddressBookEntry> GetEntries (size_t limit);
		// Administrative add. The rejected destination value is never logged;
		// only the reason and the name are. AddAddress() keeps the historical
		// AddressBookMutationResult contract for the existing callers and is
		// equivalent to AddAddressChecked(); replace=false never touches an
		// existing entry and reports Exists instead.
		AddressAddResult AddAddressChecked (std::string_view name, std::string_view destination, bool replace);
		AddressBookMutationResult AddAddress (std::string_view name, std::string_view destination, bool replace);
		AddressBookMutationResult RemoveAddress (std::string_view name);
		// Diagnostics only: number of live negative-cache entries. It never
		// exposes names or destinations.
		size_t GetNegativeCacheSize () const;
		// Deleted-host tombstones: subscription merges must not resurrect a
		// name the user removed. Patterns are bounded, ASCII-only names with
		// optional '*' wildcards; an explicit Add wins and clears a match.
		std::vector<std::string> GetDeletedHosts (size_t limit);
		AddressBookMutationResult BlockAddress (std::string_view name);
		AddressBookMutationResult UnblockAddress (std::string_view name);

		// Subscription administration. These helpers only edit the source
		// list: they never fetch, never block on a download and never stop
		// the address book. They may be called from the daemon administration
		// thread; the shared destination's io_context performs the reload.
		// The list is populated by Start(), and the daemon only serves these
		// commands after startup, so add/remove rewrites the complete list.
		std::vector<std::string> GetSubscriptions (size_t max) const;
		bool AddSubscription (std::string_view url, std::string & error);
		bool RemoveSubscription (std::string_view url, std::string & error);
		// Cumulative, saturating count of subscription offers declined
		// because the name already had a destination. Diagnostics only: no
		// per-conflict file is written.
		uint64_t GetMergeConflicts () const { return m_MergeConflicts.load (std::memory_order_relaxed); }

		bool LoadHostsFromStream (std::istream && f, bool is_update);
		// Source-aware merge used by subscription downloads. `source` is a
		// non-secret diagnostic label (the resolved subscription host). It is
		// recorded as the entry origin, appears in conflict log lines, and
		// only a line whose "action=delete" matches that origin may retract
		// the name.
		bool LoadHostsFromStream (std::istream && f, bool is_update, std::string_view source);
		void DownloadComplete (bool success, const i2p::data::IdentHash & subscription, const std::string & etag,
			const std::string & lastModified);
		// Tri-state variant used by AddressBookSubscription. A 304 Not
		// Modified keeps the stored ETag, is not a failure and does not
		// advance the retry ladder.
		void DownloadComplete (AddressBookDownloadResult result, const i2p::data::IdentHash & subscription,
			const std::string & etag, const std::string & lastModified);
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
		void LoadAddressSources ();
		void LoadSubscriptions ();
		void LoadLocal ();

		// Subscription-source state is guarded by m_SubscriptionsMutex. These
		// helpers must be called with that lock held, except for
		// ReadSubscriptionSources (pure) and the timer handoff below.
		static std::vector<std::string> ReadSubscriptionSources ();
		void RebuildSubscriptionsLocked ();
		bool SaveSubscriptionsLocked (std::string & error);

		// The subscription timer belongs to the shared local destination's
		// io_context. RearmSubscriptionsTimer() must only run there.
		void RearmSubscriptionsTimer (std::chrono::seconds delay);
		// Any thread: hands a re-arm to the io_context. The shared lifetime
		// token is cleared by Stop(), so a queued re-arm cannot touch
		// released timer or subscription state.
		void PostSubscriptionsRearm (std::chrono::seconds delay);
		std::shared_ptr<std::atomic<bool>> ReloadToken () const;

		void RecordMergeConflict ();

		static bool IsValidDeletedPattern (std::string_view pattern);
		static bool MatchesDeletedPattern (std::string_view pattern, std::string_view name);
		bool IsDeletedHostLocked (std::string_view name) const;
		bool RemoveDeletedMatchesLocked (std::string_view name);
		bool SaveDeletedHostsLocked ();

		// Entry origins (name -> subscription label), guarded by
		// m_AddressBookMutex. These helpers require that lock.
		bool IsFeedOriginLocked (std::string_view name, std::string_view source) const;
		bool SaveAddressSourcesLocked ();
		void ForgetAddressSourceLocked (std::string_view name);
		// Retracts an entry that its own feed asked to delete. Requires the
		// lock; returns true when the entry was removed.
		bool ApplyFeedDeleteLocked (std::string_view key, std::string_view name);

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
		// Origin of entries that a subscription supplied; see the file
		// comment.
		std::map<std::string, std::string, std::less<>> m_AddressSources;
		detail::NegativeAddressCache m_NegativeCache;
		std::map<i2p::data::IdentHash, std::shared_ptr<AddressResolver>> m_Resolvers; // local destination->resolver
		i2p::datagram::DatagramDestination::ReceiverRegistration m_LookupReceiver;
		detail::AddressLookupRequests m_Lookups;
		std::unique_ptr<AddressBookStorage> m_Storage;
		volatile bool m_IsLoaded;
		std::future<void> m_Downloading;
		int m_NumRetries;
		// Subscription sources, guarded by m_SubscriptionsMutex. The URL list
		// is the persisted authority; the objects are rebuilt from it and are
		// shared so an in-flight download keeps its source alive.
		mutable std::mutex m_SubscriptionsMutex;
		std::vector<std::string> m_SubscriptionURLs;
		std::vector<std::shared_ptr<AddressBookSubscription>> m_Subscriptions;
		std::shared_ptr<AddressBookSubscription> m_DefaultSubscription; // in case if we don't know any addresses yet
		std::shared_ptr<std::atomic<bool>> m_ReloadAlive; // see PostSubscriptionsRearm()
		std::atomic<uint64_t> m_MergeConflicts;
		std::unique_ptr<boost::asio::steady_timer> m_SubscriptionsUpdateTimer, m_AddressCacheUpdateTimer;
		bool m_IsEnabled;
	};

	class AddressBookSubscription
	{
	public:
		AddressBookSubscription (AddressBook & book, std::string_view link);
		void CheckUpdates ();

	private:
		// Failed, NotModified (HTTP 304) or Updated; the book keeps the
		// normal cadence for the last two.
		AddressBookDownloadResult MakeRequest ();

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
		i2p::datagram::DatagramDestination::ReceiverRegistration m_RequestReceiver;
		std::map<std::string, i2p::data::IdentHash> m_LocalAddresses;
	};
}
}

#endif

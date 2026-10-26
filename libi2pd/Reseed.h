/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: Reseed.
 *
 * Initial RouterInfo bootstrap from signed SU3 reseed bundles or ZIP files fetched over HTTPS
 * or Yggdrasil. Bundles are verified against configured signing keys, redirects are bounded,
 * every I/O phase carries a cancellable deadline, outdated entries are skipped instead of
 * deleting cached peers, and the owned job publishes a bounded administrative state.
 *
 */

#ifndef RESEED_H
#define RESEED_H

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iostream>
#include <map>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>
#include <boost/asio/io_context.hpp>
#include "Identity.h"
#include "Crypto.h"

namespace i2p
{
namespace data
{
	class NetDb;

	constexpr int MAX_NUM_RESEED_ATTEMPTS = 10;
	constexpr int MAX_RESEED_REDIRECTS = 5;
	// Historical per-connect budget. Individual phases are bounded by
	// RESEED_PHASE_TIMEOUT below; this constant is kept for API compatibility.
	constexpr int RESEED_CONNECT_TIMEOUT = 5; // in seconds
	constexpr int RESEED_WAITING_INTERVAL = 30; // in second
	constexpr int RESEED_GIVEUP_TIMEOUT = 180; // in seconds
	// Java 2.14.0 Reseeder: stop only after enough router infos from at least
	// two independent reseed servers.
	constexpr int MIN_RESEED_ROUTER_INFOS = 100;
	constexpr int MIN_RESEED_SERVERS = 2;

	// Per-phase I/O budget (DNS resolve, connect, TLS handshake, request write
	// and each response read) and the total budget for one host. A stalled or
	// slow mirror can therefore never hold a reseed worker for minutes, and a
	// cooperative stop returns after at most one phase.
	constexpr std::chrono::seconds RESEED_PHASE_TIMEOUT { 45 };
	constexpr std::chrono::seconds RESEED_HOST_TIMEOUT { 7 * 60 };

	// An imported RouterInfo older than this is skipped. Java's Reseeder only
	// skips an outdated entry; it never deletes cached peers because a
	// downloaded bundle happens to carry old descriptors. The window matches
	// the historical 10 x NETDB_MAX_EXPIRATION_TIMEOUT check (270 hours).
	constexpr uint64_t RESEED_STALE_ROUTER_TIMEOUT = 10ULL * 27 * 60 * 60 * 1000;

	// Administrative reasons are bounded; they never carry a URL, a proxy or a
	// credential.
	constexpr size_t RESEED_MAX_ERROR_LENGTH = 128;

	enum class ReseedPhase : uint8_t
	{
		Idle,
		Running,
		Succeeded,
		Failed,
		Disabled
	};

	struct ReseedState
	{
		ReseedPhase phase { ReseedPhase::Idle };
		bool enabled { true };
		uint32_t attempts { 0 };
		uint32_t succeeded { 0 };
		uint64_t lastAttemptMs { 0 };
		uint64_t lastSuccessMs { 0 };
		std::string lastError; // bounded to RESEED_MAX_ERROR_LENGTH
	};

	// Result of one import pass. Imported and skipped entries are reported
	// separately so a caller can distinguish "the mirror was stale" from "the
	// bundle was rejected" without touching the cached peer set.
	struct ReseedResult
	{
		int imported { 0 };
		int skippedStale { 0 };
		int servers { 0 };
		bool cancelled { false };
		bool noServers { false }; // no source configured; retrying cannot help
		std::string error; // bounded, non-secret reason
	};

	// True when reseed is allowed. `reseed.disable` is the switch and
	// `i2p.noReseed` is accepted as the historical alias.
	bool IsReseedEnabled ();

	// Proposal 147: append the router network ID to an SU3 reseed URL. Java
	// appends "?netid=" unconditionally; this helper keeps an existing query
	// intact, matches the parameter instead of a raw substring and strips the
	// fragment, which is never sent to the server.
	std::string AppendReseedNetId (std::string url, int netId);

	// Drops userinfo, path, query and fragment from a configured proxy
	// endpoint, so a `user:pass@host` value never reaches a log record or an
	// administrative state. An unparsable value reports "invalid" instead of
	// being echoed.
	std::string RedactReseedProxyForLog (const std::string & proxy);

	// Deterministic seams for the reseed regressions. Production uses the real
	// HTTPS/Yggdrasil transport, the wall clock and the configured sources.
	struct ReseedTestHooks
	{
		std::function<std::string (const std::string & url, const std::stop_token & token)> fetch;
		std::function<uint64_t ()> nowMs;
		std::vector<std::string> sources;
		bool overrideSources { false };
	};

	/// Imports bootstrap RouterInfos from configured local or remote sources.
	/// Remote SU3 bundles are checked against loaded signing certificates;
	/// a local ZIP import is a separate entry point and has no SU3 signature.
	/// One owner performs a pass and controls cancellation through its stop token.
	/// @see https://i2p.net/en/docs/specs/su3/
	class Reseeder
	{
		typedef Tag<512> PublicKey;

	public:
		Reseeder ();
		~Reseeder ();

		// One bootstrap pass: a configured local file/URL when present,
		// otherwise the reseed servers with the historical retry budget. Every
		// wait and every I/O phase observes `token`.
		ReseedResult Bootstrap (const std::stop_token & token = std::stop_token { });
		ReseedResult ReseedFromServers (const std::stop_token & token = std::stop_token { });

		// Local file entry points. The int wrappers keep the historical
		// "number of imported RouterInfos" contract.
		int ProcessSU3File (const char * filename);
		int ProcessZIPFile (const char * filename);
		ReseedResult ReadSU3File (const char * filename);
		ReseedResult ReadZIPFile (const char * filename);

		/// Load the SU3 signing certificates into the in-process trust store.
		/// Only `*.crt` entries are read; unparsable files are logged and skipped.
		/// The store only grows, so a certificate removed from the directory stays
		/// trusted until the process restarts, and a bundle whose signer is absent
		/// from the store is rejected rather than imported unverified.
		void LoadCertificates ();

		void SetFetchHookForTesting (
			std::function<std::string (const std::string & url, const std::stop_token & token)> hook);
		void SetClockForTesting (std::function<uint64_t ()> nowMs);
		void SetSourcesForTesting (std::vector<std::string> sources);
		void SetNetDbForTesting (NetDb * database);

	private:
		ReseedResult ReseedFromSU3Url (const std::string & url, bool isHttps, const std::stop_token & token);
		void LoadCertificate (const std::string & filename);

		ReseedResult ProcessSU3Stream (std::istream & s, const std::stop_token & token, bool requireFresh = false);
		ReseedResult ProcessZIPStream (std::istream & s, uint64_t contentLength, const std::stop_token & token);

		bool FindZipDataDescriptor (std::istream & s);

		std::string HttpsRequest (const std::string & address, int redirects, const std::stop_token & token);
		std::string YggdrasilRequest (const std::string & address, int redirects, const std::stop_token & token);
		template <typename Stream>
		std::string ReseedRequest (Stream & s, boost::asio::io_context & service, const std::string & uri,
			int redirects, const std::stop_token & token, std::chrono::steady_clock::time_point hostDeadline);

		uint64_t NowMilliseconds () const;
		std::chrono::milliseconds PhaseBudget (std::chrono::steady_clock::time_point hostDeadline) const;
		NetDb & TargetNetDb () const;

	private:
		std::map<std::string, PublicKey> m_SigningKeys;
		std::function<std::string (const std::string & url, const std::stop_token & token)> m_FetchHook;
		std::function<uint64_t ()> m_ClockHook;
		std::vector<std::string> m_SourceOverride;
		bool m_HasSourceOverride = false;
		NetDb * m_NetDbOverride = nullptr;
		// Fixed failure reason of the last transport phase. Only written by the
		// thread that owns this reseeder; never contains a URL or credential.
		std::string m_LastTransportError;
	};
}
}

#endif

/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: Reseed.
 *
 * Implements network bootstrap from SU3-signed ZIP bundles, either local files
 * or HTTPS/Yggdrasil reseed servers with optional HTTP or SOCKS proxies and
 * redirects. SU3 headers, content and ZIP entries are length-bounded, RSA and
 * Ed25519 SU3 signatures are checked against loaded certificates (verification
 * defaults to enabled), and accepted RouterInfos are imported with duplicate
 * and expiry filtering.
 *
 */

#include <array>
#include <string.h>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <limits>
#include <mutex>
#include <random>
#include <fstream>
#include <memory>
#include <sstream>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>
#include <boost/asio.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/algorithm/string.hpp>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/objects.h>
#include <openssl/x509.h>
#include <openssl/pem.h>
#include <zlib.h>

#include "Crypto.h"
#if I2PD_HAVE_EVP_PKEY_BN_PARAM && !defined(LIBRESSL_VERSION_NUMBER)
#include <openssl/core_names.h>
#endif
#include "I2PEndian.h"
#include "Reseed.h"
#include "FS.h"
#include "Log.h"
#include "Identity.h"
#include "NetDb.hpp"
#include "HTTP.h"
#include "util.h"
#include "Config.h"
#include "Socks5.h"

namespace i2p
{
namespace data
{
	const uint64_t MAX_RESEED_CONTENT_LENGTH = 8 * 1024 * 1024; // 8 MB signed SU3 content
	const uint32_t MAX_RESEED_FILE_SIZE = 1 * 1024 * 1024; // 1 MB per ZIP entry
	const uint64_t MAX_RESEED_UNCOMPRESSED_TOTAL = 32 * 1024 * 1024; // 32 MB total inflated data
	const size_t MAX_RESEED_ENTRIES = 400; // Java Reseeder local archive entry cap
	// HTTP headers, chunk framing and redirect bodies are small compared with
	// the signed payload. Refuse to buffer more than the SU3 cap plus a small
	// allowance so a malicious or broken reseed server cannot grow the
	// response string without bound.
	const size_t MAX_RESEED_HTTP_RESPONSE_SIZE = static_cast<size_t> (MAX_RESEED_CONTENT_LENGTH) + 1024 * 1024;

	namespace
	{
		// Administrative reasons are fixed tokens. Nothing that can carry a
		// credential (URL, proxy userinfo, configuration value) is ever stored
		// or logged as a reason.
		void SetReseedReason (std::string & target, std::string_view reason)
		{
			if (reason.size () > RESEED_MAX_ERROR_LENGTH) reason = reason.substr (0, RESEED_MAX_ERROR_LENGTH);
			target.assign (reason);
		}

		// A `netid` query parameter is a match only when its name is complete:
		// "?netid=2", "&netid=2" or a bare "netid" token. A raw substring match
		// rejects a URL whose other parameter or value merely mentions it.
		bool HasReseedNetIdParameter (std::string_view url)
		{
			const auto query = url.find ('?');
			if (query == std::string_view::npos) return false;
			std::size_t pos = query + 1;
			while (pos <= url.size ())
			{
				auto end = url.find ('&', pos);
				if (end == std::string_view::npos) end = url.size ();
				const auto param = url.substr (pos, end - pos);
				const auto equals = param.find ('=');
				if ((equals == std::string_view::npos ? param : param.substr (0, equals)) == "netid") return true;
				if (end >= url.size ()) break;
				pos = end + 1;
			}
			return false;
		}

		// Sleeps for `delay` unless the stop token is signalled first. The stop
		// callback wakes the wait immediately, so shutdown never waits for the
		// historical 30-second retry interval.
		bool SleepUnlessStopped (std::chrono::milliseconds delay, const std::stop_token & token)
		{
			if (token.stop_requested ()) return false;
			std::mutex mutex;
			std::condition_variable cv;
			std::stop_callback wake (token, [&cv] { cv.notify_all (); });
			std::unique_lock<std::mutex> lock (mutex);
			return !cv.wait_for (lock, delay, [&token] { return token.stop_requested (); });
		}

		// Runs one asynchronous operation with a hard deadline and cooperative
		// cancellation. `start` posts the operation and `cancel` aborts it.
		// Returns true only when the operation completed; on timeout or stop the
		// operation is aborted and every handler is drained before the operands
		// go out of scope, so the stream must then be discarded.
		template <typename Start, typename Cancel>
		bool RunWithDeadline (boost::asio::io_context & service, std::chrono::milliseconds timeout,
			const std::stop_token & token, Start && start, Cancel && cancel)
		{
			if (token.stop_requested () || timeout <= std::chrono::milliseconds::zero ()) return false;
			boost::asio::steady_timer timer (service);
			bool expired = false;
			timer.expires_after (timeout);
			timer.async_wait (
				[&expired, &cancel] (const boost::system::error_code & ec)
				{
					if (ec) return; // cancelled by the completion path
					expired = true;
					cancel ();
				});
			std::stop_callback onStop (token,
				[&service, cancel]
				{
					// Cancel on the I/O thread: a socket is not a shared object,
					// so a stop request from the admin thread posts the abort.
					boost::asio::post (service, [cancel] { cancel (); });
				});
			start ();
			service.restart ();
			service.run ();
			// Drain the aborted timer handler before the timer is destroyed: it
			// captures locals of this frame.
			timer.cancel ();
			service.restart ();
			service.run ();
			return !expired && !token.stop_requested ();
		}

		// The published date follows the variable-length RouterIdentity. The
		// netDb applies its own store-validity policy; this check exists only to
		// count entries that the bundle carries already far too old, which Java's
		// reseeder skips instead of deleting cached peers.
		bool GetRouterInfoPublishedDate (const uint8_t * buf, int len, uint64_t & published)
		{
			if (!buf || len <= 0) return false;
			IdentityEx identity;
			if (!identity.FromBuffer (buf, len)) return false;
			const size_t offset = identity.GetFullLen ();
			if (offset + 8 > static_cast<size_t> (len)) return false;
			published = bufbe64toh (buf + offset);
			return true;
		}
	} // namespace

	bool IsReseedEnabled ()
	{
		bool disabled = false;
		if (!i2p::config::GetOption ("reseed.disable", disabled)) disabled = false;
		bool noReseed = false;
		if (!i2p::config::GetOption ("i2p.noReseed", noReseed)) noReseed = false;
		return !disabled && !noReseed;
	}

	std::string AppendReseedNetId (std::string url, int netId)
	{
		// The fragment is never sent to the server; appending after it would put
		// the parameter outside the query.
		const auto fragment = url.find ('#');
		if (fragment != std::string::npos) url.erase (fragment);
		if (HasReseedNetIdParameter (url)) return url;
		const bool separatorPresent = !url.empty () && (url.back () == '?' || url.back () == '&');
		if (!separatorPresent) url += url.find ('?') == std::string::npos ? '?' : '&';
		url += "netid=";
		url += std::to_string (netId);
		return url;
	}

	std::string RedactReseedProxyForLog (const std::string & proxy)
	{
		if (proxy.empty ()) return "";
		i2p::http::URL url;
		if (!url.parse (proxy) || url.host.empty ()) return "invalid";
		std::string out;
		if (!url.schema.empty ())
		{
			out = url.schema;
			out += "://";
		}
		out += url.ipv6 ? "[" + url.host + "]" : url.host;
		if (url.port) out += ":" + std::to_string (url.port);
		return out;
	}

	Reseeder::Reseeder () { }

	Reseeder::~Reseeder () { }

	void Reseeder::SetFetchHookForTesting (
		std::function<std::string (const std::string & url, const std::stop_token & token)> hook)
	{
		m_FetchHook = std::move (hook);
	}

	void Reseeder::SetClockForTesting (std::function<uint64_t ()> nowMs)
	{
		m_ClockHook = std::move (nowMs);
	}

	void Reseeder::SetSourcesForTesting (std::vector<std::string> sources)
	{
		m_SourceOverride = std::move (sources);
		m_HasSourceOverride = true;
	}

	void Reseeder::SetNetDbForTesting (NetDb * database)
	{
		m_NetDbOverride = database;
	}

	uint64_t Reseeder::NowMilliseconds () const
	{
		return m_ClockHook ? m_ClockHook () : i2p::util::GetMillisecondsSinceEpoch ();
	}

	std::chrono::milliseconds Reseeder::PhaseBudget (std::chrono::steady_clock::time_point hostDeadline) const
	{
		const auto now = std::chrono::steady_clock::now ();
		if (now >= hostDeadline) return std::chrono::milliseconds::zero ();
		const auto left = std::chrono::duration_cast<std::chrono::milliseconds> (hostDeadline - now);
		const auto phase = std::chrono::duration_cast<std::chrono::milliseconds> (RESEED_PHASE_TIMEOUT);
		return left < phase ? left : phase;
	}

	NetDb & Reseeder::TargetNetDb () const
	{
		return m_NetDbOverride ? *m_NetDbOverride : i2p::data::netdb;
	}

	/**
	 @brief tries to bootstrap into I2P network (from local files and servers, with respect of options)
	 */
	ReseedResult Reseeder::Bootstrap (const std::stop_token & token)
	{
		ReseedResult result;
		if (!IsReseedEnabled ())
		{
			SetReseedReason (result.error, "disabled");
			LogPrint (eLogWarning, "Reseed: Reseed is disabled by configuration");
			return result;
		}

		std::string su3FileName;
		i2p::config::GetOption ("reseed.file", su3FileName);
		std::string zipFileName;
		i2p::config::GetOption ("reseed.zipfile", zipFileName);

		if (su3FileName.length () > 0) // bootstrap from SU3 file or URL
		{
			if (su3FileName.length () > 8 && su3FileName.substr (0, 8) == "https://")
			{
				// Proposal 147 network-ID cross-check for a single SU3 URL.
				result = ReseedFromSU3Url (AppendReseedNetId (su3FileName, i2p::context.GetNetID ()), true, token);
			}
			else
				result = ReadSU3File (su3FileName.c_str ());
			if (result.imported <= 0) LogPrint (eLogWarning, "Reseed: Failed to reseed from ", su3FileName);
			return result;
		}
		if (zipFileName.length () > 0) // bootstrap from ZIP file
		{
			result = ReadZIPFile (zipFileName.c_str ());
			if (result.imported <= 0) LogPrint (eLogWarning, "Reseed: Failed to reseed from ", zipFileName);
			return result;
		}

		// bootstrap from reseed servers
		const auto start = i2p::util::GetMonotonicSeconds ();
		for (;;)
		{
			if (token.stop_requested ())
			{
				result.cancelled = true;
				SetReseedReason (result.error, "cancelled");
				return result;
			}
			auto pass = ReseedFromServers (token);
			result.imported += pass.imported;
			result.skippedStale += pass.skippedStale;
			result.servers += pass.servers;
			if (pass.imported > 0)
			{
				result.cancelled = false;
				result.error.clear ();
				break;
			}
			result.cancelled = pass.cancelled;
			SetReseedReason (result.error, pass.error.empty () ? "no server" : pass.error);
			if (pass.cancelled || pass.noServers) break; // a retry cannot help
			if (i2p::util::GetMonotonicSeconds () >= start + RESEED_GIVEUP_TIMEOUT) break; // give up
			LogPrint (eLogWarning, "Reseed: Failed to reseed from servers. Waiting for ", RESEED_WAITING_INTERVAL,
				" seconds");
			if (!SleepUnlessStopped (std::chrono::seconds (RESEED_WAITING_INTERVAL), token))
			{
				result.cancelled = true;
				SetReseedReason (result.error, "cancelled");
				break;
			}
		}
		if (result.imported <= 0) LogPrint (eLogWarning, "Reseed: Failed to reseed from servers. Give up");
		return result;
	}

	/**
	 * @brief bootstrap from random server, retry 10 times
	 * @return number of entries added to netDb and the skipped/outdated count
	 */
	ReseedResult Reseeder::ReseedFromServers (const std::stop_token & token)
	{
		ReseedResult result;
		std::vector<std::string> httpsReseedHostList;
		std::vector<std::string> yggReseedHostList;
		if (m_HasSourceOverride)
		{
			httpsReseedHostList = m_SourceOverride;
		}
		else
		{
			bool ipv6;
			i2p::config::GetOption ("ipv6", ipv6);
			bool ipv4;
			i2p::config::GetOption ("ipv4", ipv4);
			bool yggdrasil;
			i2p::config::GetOption ("meshnets.yggdrasil", yggdrasil);

			if (ipv4 || ipv6)
			{
				std::string reseedURLs;
				i2p::config::GetOption ("reseed.urls", reseedURLs);
				if (!reseedURLs.empty ())
					boost::split (httpsReseedHostList, reseedURLs, boost::is_any_of (","), boost::token_compress_on);
			}

			if (yggdrasil && !i2p::util::net::GetYggdrasilAddress ().is_unspecified ())
			{
				LogPrint (eLogInfo, "Reseed: Yggdrasil is supported");
				std::string yggReseedURLs;
				i2p::config::GetOption ("reseed.yggurls", yggReseedURLs);
				if (!yggReseedURLs.empty ())
					boost::split (yggReseedHostList, yggReseedURLs, boost::is_any_of (","), boost::token_compress_on);
			}
		}

		if (httpsReseedHostList.empty () && yggReseedHostList.empty ())
		{
			LogPrint (eLogWarning, "Reseed: No reseed servers specified");
			result.noServers = true;
			SetReseedReason (result.error, "no server");
			return result;
		}

		int numReseeds = static_cast<int> (httpsReseedHostList.size () + yggReseedHostList.size ());
		int reseedAttempts = std::min (numReseeds, MAX_NUM_RESEED_ATTEMPTS);
		if (reseedAttempts)
		{
			std::mt19937 rng (i2p::util::GetRngSeed ());
			for (int i = 0; i < reseedAttempts; i++)
			{
				if (token.stop_requested ())
				{
					result.cancelled = true;
					SetReseedReason (result.error, "cancelled");
					return result;
				}
				auto ind = rng () % numReseeds;
				bool isHttps = ind < httpsReseedHostList.size ();
				std::string reseedUrl
					= isHttps ? httpsReseedHostList[ind] : yggReseedHostList[ind - httpsReseedHostList.size ()];
				reseedUrl += "i2pseeds.su3";
				// Proposal 147: tell the reseed server which network we serve.
				reseedUrl = AppendReseedNetId (reseedUrl, i2p::context.GetNetID ());
				auto pass = ReseedFromSU3Url (reseedUrl, isHttps, token);
				result.imported += pass.imported;
				result.skippedStale += pass.skippedStale;
				if (pass.imported > 0)
				{
					// Java keeps downloading until it has enough router infos
					// from at least two independent servers.
					result.servers++;
					if (result.imported >= MIN_RESEED_ROUTER_INFOS && result.servers >= MIN_RESEED_SERVERS)
					{
						LogPrint (
							eLogInfo, "Reseed: ", result.imported, " router infos from ", result.servers, " servers");
						return result;
					}
				}
				else if (!pass.error.empty ())
					SetReseedReason (result.error, pass.error);
				if (pass.cancelled)
				{
					result.cancelled = true;
					SetReseedReason (result.error, "cancelled");
					return result;
				}

				if (isHttps)
					httpsReseedHostList.erase (httpsReseedHostList.begin () + ind);
				else
					yggReseedHostList.erase (yggReseedHostList.begin () + (ind - httpsReseedHostList.size ()));
				numReseeds--;
				if (!numReseeds) break;
			}
		}
		if (result.imported > 0)
		{
			LogPrint (eLogInfo, "Reseed: ", result.imported, " router infos from ", result.servers, " servers");
			return result;
		}
		LogPrint (
			eLogWarning, "Reseed: Failed to reseed from ", numReseeds, " servers after ", reseedAttempts, " attempts");
		if (result.error.empty ()) SetReseedReason (result.error, "no server");
		return result;
	}

	/**
	 * @brief bootstrap from HTTPS URL with SU3 file
	 * @param url
	 * @return imported and skipped entry counts
	 */
	ReseedResult Reseeder::ReseedFromSU3Url (const std::string & url, bool isHttps, const std::stop_token & token)
	{
		ReseedResult result;
		LogPrint (eLogInfo, "Reseed: Downloading SU3 from ", url);
		std::string su3;
		if (m_FetchHook)
			su3 = m_FetchHook (url, token);
		else
			su3 = isHttps ? HttpsRequest (url, 0, token) : YggdrasilRequest (url, 0, token);
		if (su3.empty ())
		{
			if (token.stop_requested ())
			{
				result.cancelled = true;
				SetReseedReason (result.error, "cancelled");
			}
			else
				SetReseedReason (result.error, m_LastTransportError.empty () ? "download" : m_LastTransportError);
			LogPrint (eLogWarning, "Reseed: SU3 download failed");
			return result;
		}
		std::stringstream s (su3);
		return ProcessSU3Stream (s, token, true);
	}

	int Reseeder::ProcessSU3File (const char * filename)
	{
		return ReadSU3File (filename).imported;
	}

	int Reseeder::ProcessZIPFile (const char * filename)
	{
		return ReadZIPFile (filename).imported;
	}

	ReseedResult Reseeder::ReadSU3File (const char * filename)
	{
		ReseedResult result;
		std::ifstream s (filename, std::ifstream::binary);
		if (s.is_open ()) return ProcessSU3Stream (s, std::stop_token { });
		LogPrint (eLogCritical, "Reseed: Can't open file ", filename);
		SetReseedReason (result.error, "file");
		return result;
	}

	ReseedResult Reseeder::ReadZIPFile (const char * filename)
	{
		ReseedResult result;
		std::ifstream s (filename, std::ifstream::binary);
		if (s.is_open ())
		{
			s.seekg (0, std::ios::end);
			auto len = s.tellg ();
			s.seekg (0, std::ios::beg);
			if (len < 0)
			{
				SetReseedReason (result.error, "file");
				return result;
			}
			return ProcessZIPStream (s, static_cast<uint64_t> (len), std::stop_token { });
		}
		LogPrint (eLogCritical, "Reseed: Can't open file ", filename);
		SetReseedReason (result.error, "file");
		return result;
	}

	const char SU3_MAGIC_NUMBER[] = "I2Psu3";

	namespace
	{
		size_t GetSU3SignatureLength (SigningKeyType type)
		{
			switch (type)
			{
			case SIGNING_KEY_TYPE_DSA_SHA1: return 40;
			case SIGNING_KEY_TYPE_ECDSA_SHA256_P256: return 64;
			case SIGNING_KEY_TYPE_ECDSA_SHA384_P384: return 96;
			case SIGNING_KEY_TYPE_ECDSA_SHA512_P521: return 132;
			case SIGNING_KEY_TYPE_RSA_SHA256_2048: return 256;
			case SIGNING_KEY_TYPE_RSA_SHA384_3072: return 384;
			case SIGNING_KEY_TYPE_RSA_SHA512_4096: return 512;
			case SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519:
			case SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519ph:
			case SIGNING_KEY_TYPE_REDDSA_SHA512_ED25519: return 64;
			case SIGNING_KEY_TYPE_GOSTR3410_CRYPTO_PRO_A_GOSTR3411_256: return 64;
			case SIGNING_KEY_TYPE_GOSTR3410_TC26_A_512_GOSTR3411_512: return 128;
			default: return 0;
			}
		}

		// SU3File verifies the digest of the header and content. RSA uses the
		// raw PKCS#1 v1.5 operation over that digest (NONEwithRSA in Java), and
		// EdDSA verifies the digest bytes directly.
		const EVP_MD * GetSU3Digest (SigningKeyType type)
		{
			switch (type)
			{
			case SIGNING_KEY_TYPE_RSA_SHA256_2048: return EVP_sha256 ();
			case SIGNING_KEY_TYPE_RSA_SHA384_3072: return EVP_sha384 ();
			case SIGNING_KEY_TYPE_RSA_SHA512_4096:
			case SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519:
			case SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519ph:
			case SIGNING_KEY_TYPE_REDDSA_SHA512_ED25519: return EVP_sha512 ();
			default: return nullptr;
			}
		}

		bool IsSU3EdDSAType (SigningKeyType type)
		{
			return type == SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519 || type == SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519ph
				|| type == SIGNING_KEY_TYPE_REDDSA_SHA512_ED25519;
		}

		bool IsSU3RSAType (SigningKeyType type)
		{
			return type == SIGNING_KEY_TYPE_RSA_SHA256_2048 || type == SIGNING_KEY_TYPE_RSA_SHA384_3072
				|| type == SIGNING_KEY_TYPE_RSA_SHA512_4096;
		}

		// Check the complete PKCS#1 v1.5 type-1 encoding that Java's
		// NONEwithRSA expects, not only the digest tail: 00 01 FF..FF 00 hash.
		bool IsValidSU3RSAEncoding (
			const uint8_t * encoded, size_t encodedLen, const uint8_t * digest, size_t digestLen)
		{
			if (encodedLen < digestLen + 11) return false;
			if (encoded[0] != 0x00 || encoded[1] != 0x01) return false;
			const size_t separator = encodedLen - digestLen - 1;
			for (size_t i = 2; i < separator; i++)
				if (encoded[i] != 0xFF) return false;
			if (encoded[separator] != 0x00) return false;
			return memcmp (encoded + encodedLen - digestLen, digest, digestLen) == 0;
		}
	} // namespace

	ReseedResult Reseeder::ProcessSU3Stream (std::istream & s, const std::stop_token & token, bool requireFresh)
	{
		auto failure = [] (std::string_view reason)
		{
			ReseedResult result;
			SetReseedReason (result.error, reason);
			return result;
		};
		auto readExact = [&s] (void * buffer, size_t size) -> bool
		{
			s.read (static_cast<char *> (buffer), static_cast<std::streamsize> (size));
			return s.good () && static_cast<size_t> (s.gcount ()) == size;
		};
		auto skip = [&s] (std::streamoff size) -> bool
		{
			s.seekg (size, std::ios::cur);
			return s.good ();
		};

		if (token.stop_requested ()) return failure ("cancelled");

		// SU3File.java: six-byte magic, one unused byte, then the file version.
		constexpr size_t SU3_MAGIC_LENGTH = 6;
		char magicNumber[SU3_MAGIC_LENGTH];
		if (!readExact (magicNumber, sizeof (magicNumber))
			|| memcmp (magicNumber, SU3_MAGIC_NUMBER, sizeof (magicNumber)) != 0)
		{
			LogPrint (eLogError, "Reseed: Unexpected or truncated SU3 header");
			return failure ("protocol");
		}
		if (!skip (1)) return failure ("protocol"); // unused
		uint8_t fileVersion = 0;
		if (!readExact (&fileVersion, sizeof (fileVersion)) || fileVersion != 0)
		{
			LogPrint (eLogError, "Reseed: Unsupported SU3 file version ", (int)fileVersion);
			return failure ("protocol");
		}
		uint16_t signatureTypeRaw = 0, signatureLengthRaw = 0;
		if (!readExact (&signatureTypeRaw, sizeof (signatureTypeRaw))
			|| !readExact (&signatureLengthRaw, sizeof (signatureLengthRaw)))
			return failure ("protocol");
		SigningKeyType signatureType = static_cast<SigningKeyType> (be16toh (signatureTypeRaw));
		uint16_t signatureLength = be16toh (signatureLengthRaw);
		const size_t expectedSignatureLength = GetSU3SignatureLength (signatureType);
		if (!expectedSignatureLength || signatureLength != expectedSignatureLength)
		{
			LogPrint (
				eLogError, "Reseed: Invalid SU3 signature type/length ", (int)signatureType, "/", signatureLength);
			return failure ("protocol");
		}
		if (!skip (1)) return failure ("protocol"); // unused
		uint8_t versionLength = 0;
		if (!readExact (&versionLength, sizeof (versionLength))) return failure ("protocol");
		if (versionLength < 16)
		{
			LogPrint (eLogError, "Reseed: SU3 version field too short: ", (int)versionLength);
			return failure ("protocol");
		}
		if (!skip (1)) return failure ("protocol"); // unused
		uint8_t signerIDLength = 0;
		if (!readExact (&signerIDLength, sizeof (signerIDLength))) return failure ("protocol");
		if (!signerIDLength)
		{
			LogPrint (eLogError, "Reseed: SU3 signer ID is empty");
			return failure ("protocol");
		}
		uint64_t contentLengthRaw = 0;
		if (!readExact (&contentLengthRaw, sizeof (contentLengthRaw))) return failure ("protocol");
		uint64_t contentLength = be64toh (contentLengthRaw);
		if (!contentLength)
		{
			LogPrint (eLogError, "Reseed: SU3 content is empty");
			return failure ("protocol");
		}
		if (contentLength > MAX_RESEED_CONTENT_LENGTH)
		{
			LogPrint (eLogError, "Reseed: SU3 content length too large: ", contentLength);
			return failure ("bounds");
		}
		if (!skip (1)) return failure ("protocol"); // unused
		uint8_t fileType = 0;
		if (!readExact (&fileType, sizeof (fileType))) return failure ("protocol");
		if (fileType != 0x00) // zip file
		{
			LogPrint (eLogError, "Reseed: Can't handle file type ", (int)fileType);
			return failure ("protocol");
		}
		if (!skip (1)) return failure ("protocol"); // unused
		uint8_t contentType = 0;
		if (!readExact (&contentType, sizeof (contentType))) return failure ("protocol");
		if (contentType != 0x03) // reseed data
		{
			LogPrint (eLogError, "Reseed: Unexpected content type ", (int)contentType);
			return failure ("protocol");
		}
		if (!skip (12)) return failure ("protocol"); // unused

		std::array<char, 256> version { };
		if (static_cast<size_t> (versionLength) >= version.size ()) return failure ("protocol");
		if (!readExact (version.data (), versionLength)) return failure ("protocol");
		version[versionLength] = 0;
		if (requireFresh)
		{
			char * end = nullptr;
			const unsigned long long epoch = std::strtoull (version.data (), &end, 10);
			const auto now = NowMilliseconds ();
			constexpr uint64_t MAX_FILE_AGE = 30ULL * 24 * 60 * 60 * 1000;
			if (end == version.data () || *end || epoch < 1400000000ULL
				|| static_cast<uint64_t> (epoch) * 1000ULL + MAX_FILE_AGE < now)
			{
				LogPrint (eLogError, "Reseed: Stale or invalid SU3 version ", version.data ());
				return failure ("stale bundle");
			}
		}
		std::array<char, 256> signerID { };
		if (static_cast<size_t> (signerIDLength) >= signerID.size ()) return failure ("protocol");
		if (!readExact (signerID.data (), signerIDLength)) return failure ("protocol");
		signerID[signerIDLength] = 0;

		// GetOption() clears the value when the option is absent. Keep the
		// fail-closed default even if Finalize() has not populated the option
		// (for example in tools or tests that only read a local SU3 file).
		bool verify = true;
		if (!i2p::config::GetOption ("reseed.verify", verify)) verify = true;
		if (verify)
		{
			auto it = m_SigningKeys.find (signerID.data ());
			if (it != m_SigningKeys.end ())
			{
				const EVP_MD * digestAlgorithm = GetSU3Digest (signatureType);
				const bool eddsa = IsSU3EdDSAType (signatureType);
				if (!digestAlgorithm || (!eddsa && !IsSU3RSAType (signatureType)))
					// Fail closed for signer types this build cannot check.
					LogPrint (eLogWarning, "Reseed: Signature type ", signatureType, " is not supported");
				else
				{
					auto pos = s.tellg ();
					if (pos < 0) return failure ("protocol");
					const size_t headerLen = static_cast<size_t> (pos);
					if (contentLength > MAX_RESEED_CONTENT_LENGTH
						|| headerLen > MAX_RESEED_CONTENT_LENGTH - contentLength)
						return failure ("bounds");
					const size_t tbsLen = headerLen + static_cast<size_t> (contentLength);
					std::vector<uint8_t> tbs (tbsLen);
					std::vector<uint8_t> signature (signatureLength);
					s.seekg (0, std::ios::beg);
					if (!readExact (tbs.data (), tbs.size ()) || !readExact (signature.data (), signature.size ()))
						return failure ("protocol");
					if (s.peek () != std::char_traits<char>::eof ())
					{
						LogPrint (eLogError, "Reseed: SU3 has data after the signature");
						return failure ("protocol");
					}

					std::array<uint8_t, EVP_MAX_MD_SIZE> digest { };
					unsigned int digestLength = 0;
					if (EVP_Digest (tbs.data (), tbsLen, digest.data (), &digestLength, digestAlgorithm, nullptr) != 1
						|| !digestLength)
					{
						LogPrint (eLogError, "Reseed: Can't calculate SU3 digest");
						return failure ("signature");
					}

					if (eddsa)
					{
						// The Ed25519/RedDSA public key is stored right-aligned
						// in the certificate slot.
						i2p::crypto::EDDSA25519Verifier verifier;
						verifier.SetPublicKey (
							it->second.data () + it->second.len - i2p::crypto::EDDSA25519_PUBLIC_KEY_LENGTH);
						if (digestLength != 64 || signature.size () != i2p::crypto::EDDSA25519_SIGNATURE_LENGTH
							|| !verifier.Verify (digest.data (), digestLength, signature.data ()))
							LogPrint (eLogWarning, "Reseed: SU3 EdDSA signature verification failed");
						else
							verify = false; // verified
					}
					else
					{
						// RSA raw PKCS#1 v1.5 over the digest, right-aligned.
						// We can't use RSA_verify because SU3 signs the digest
						// without a DigestInfo wrapper.
						if (signatureLength < digestLength + 11)
						{
							LogPrint (eLogError, "Reseed: SU3 RSA signature is too short for its digest");
							return failure ("signature");
						}
						BN_CTX * bnctx = BN_CTX_new ();
						BIGNUM *sig = BN_new (), *n = BN_new ();
						if (!bnctx || !sig || !n)
						{
							if (bnctx) BN_CTX_free (bnctx);
							if (sig) BN_free (sig);
							if (n) BN_free (n);
							return failure ("signature");
						}
						BN_bin2bn (signature.data (), signatureLength, sig);
						// A certificate slot is at most 512 bytes (Tag<512>).
						BN_bin2bn (it->second.data (), static_cast<int> (it->second.len), n);
						std::vector<uint8_t> enSigBuf (signatureLength);
						if (!BN_is_odd (n) || BN_mod_exp (sig, sig, i2p::crypto::GetRSAE (), n, bnctx) != 1)
							// A malformed or non-RSA certificate slot must fail
							// closed instead of dividing by zero.
							LogPrint (eLogWarning, "Reseed: SU3 RSA key or signature is invalid");
						else if (!i2p::crypto::bn2buf (sig, enSigBuf.data (), signatureLength))
							LogPrint (eLogWarning, "Reseed: SU3 signature does not fit the declared length");
						else if (!IsValidSU3RSAEncoding (
									 enSigBuf.data (), enSigBuf.size (), digest.data (), digestLength))
							LogPrint (eLogWarning, "Reseed: SU3 signature verification failed");
						else
							verify = false; // verified
						BN_free (sig);
						BN_free (n);
						BN_CTX_free (bnctx);
					}
					s.seekg (pos, std::ios::beg);
				}
			}
			else
				LogPrint (eLogWarning, "Reseed: Certificate for ", signerID.data (), " not loaded");
		}

		if (verify) // not verified
		{
			LogPrint (eLogCritical, "Reseed: SU3 verification failed");
			return failure ("signature");
		}

		// handle content
		return ProcessZIPStream (s, contentLength, token);
	}

	const uint32_t ZIP_HEADER_SIGNATURE = 0x04034B50;
	const uint32_t ZIP_CENTRAL_DIRECTORY_HEADER_SIGNATURE = 0x02014B50;
	const uint16_t ZIP_BIT_FLAG_DATA_DESCRIPTOR = 0x0008;
	ReseedResult Reseeder::ProcessZIPStream (std::istream & s, uint64_t contentLength, const std::stop_token & token)
	{
		ReseedResult result;
		size_t processedEntries = 0;
		uint64_t totalUncompressed = 0;
		// Entries that passed the archive/CRC checks and were offered to the
		// netDb store policy (which may still reject an outdated descriptor).
		size_t considered = 0;
		// One clock reading keeps the freshness decision of every entry
		// consistent for the whole archive.
		const uint64_t now = NowMilliseconds ();
		auto stopWith = [&result] (std::string_view reason)
		{
			SetReseedReason (result.error, reason);
			return result;
		};
		// Imported entries are bounded by the verified payload, and an outdated
		// entry is only skipped and counted. Nothing here deletes a cached peer.
		auto addRouterInfo = [&] (const uint8_t * data, size_t size)
		{
			if (!data || !size || size > static_cast<size_t> (std::numeric_limits<int>::max ())) return;
			uint64_t published = 0;
			if (GetRouterInfoPublishedDate (data, static_cast<int> (size), published)
				&& now > published + RESEED_STALE_ROUTER_TIMEOUT)
			{
				++result.skippedStale;
				LogPrint (eLogInfo, "Reseed: Skipping outdated RouterInfo by ", (now - published) / 1000LL / 3600LL,
					" hours");
				return;
			}
			++considered;
			if (TargetNetDb ().AddRouterInfo (data, static_cast<int> (size))) ++result.imported;
		};

		auto contentPosition = s.tellg ();
		if (contentPosition < 0) return stopWith ("protocol");
		size_t contentPos = static_cast<size_t> (contentPosition);
		// not !eof: seekg clears eofbit, so a stream broken in the middle of a
		// header stays not-eof forever and the loop never ends
		while (s.good ())
		{
			if (token.stop_requested ())
			{
				result.cancelled = true;
				return stopWith ("cancelled");
			}
			if (processedEntries >= MAX_RESEED_ENTRIES)
			{
				LogPrint (eLogWarning, "Reseed: Archive entry limit reached");
				break;
			}
			++processedEntries;
			uint32_t signature = 0;
			s.read ((char *)&signature, sizeof (signature));
			if (s.gcount () != static_cast<std::streamsize> (sizeof (signature))) break;
			signature = le32toh (signature);
			if (signature == ZIP_HEADER_SIGNATURE)
			{
				// next local file
				s.seekg (2, std::ios::cur); // version
				uint16_t bitFlag = 0;
				s.read ((char *)&bitFlag, 2);
				bitFlag = le16toh (bitFlag);
				uint16_t compressionMethod = 0;
				s.read ((char *)&compressionMethod, 2);
				compressionMethod = le16toh (compressionMethod);
				s.seekg (4, std::ios::cur); // skip fields we don't care about
				uint32_t compressedSize = 0, uncompressedSize = 0;
				uint32_t crc_32 = 0;
				s.read ((char *)&crc_32, 4);
				crc_32 = le32toh (crc_32);
				s.read ((char *)&compressedSize, 4);
				compressedSize = le32toh (compressedSize);
				s.read ((char *)&uncompressedSize, 4);
				uncompressedSize = le32toh (uncompressedSize);
				uint16_t fileNameLength, extraFieldLength;
				s.read ((char *)&fileNameLength, 2);
				fileNameLength = le16toh (fileNameLength);
				s.read ((char *)&extraFieldLength, 2);
				extraFieldLength = le16toh (extraFieldLength);
				if (!s.good ())
				{
					LogPrint (eLogError, "Reseed: Truncated zip local file header");
					return stopWith ("protocol");
				}
				if (fileNameLength >= 255)
				{
					// too big
					LogPrint (eLogError, "Reseed: SU3 fileNameLength too large: ", fileNameLength);
					return stopWith ("bounds");
				}
				char localFileName[255] { };
				s.read (localFileName, fileNameLength);
				if (!s || s.gcount () != static_cast<std::streamsize> (fileNameLength))
				{
					LogPrint (eLogError, "Reseed: Truncated zip file name");
					return stopWith ("protocol");
				}
				localFileName[fileNameLength] = 0;
				s.seekg (extraFieldLength, std::ios::cur);
				// take care about data descriptor if presented
				if (bitFlag & ZIP_BIT_FLAG_DATA_DESCRIPTOR)
				{
					auto pos = s.tellg ();
					if (!FindZipDataDescriptor (s))
					{
						LogPrint (eLogError, "Reseed: SU3 archive data descriptor not found");
						return stopWith ("protocol");
					}
					s.read ((char *)&crc_32, 4);
					crc_32 = le32toh (crc_32);
					s.read ((char *)&compressedSize, 4);
					compressedSize = le32toh (compressedSize);
					// The data-descriptor signature is consumed as part of the
					// compressed data. Reject values that would wrap uint32_t
					// before the size cap below.
					if (compressedSize > MAX_RESEED_FILE_SIZE - 4)
					{
						LogPrint (eLogError, "Reseed: SU3 compressed size too large: ", compressedSize);
						return stopWith ("bounds");
					}
					compressedSize += 4;
					s.read ((char *)&uncompressedSize, 4);
					uncompressedSize = le32toh (uncompressedSize);

					// now we know compressed and uncompressed size
					s.seekg (pos, std::ios::beg); // back to compressed data
				}

				LogPrint (eLogDebug, "Reseed: Processing file ", localFileName, " ", compressedSize, " bytes");
				if (!compressedSize)
				{
					LogPrint (eLogWarning, "Reseed: Unexpected size 0. Skipped");
					continue;
				}
				const uint64_t entryUncompressed = compressionMethod ? uncompressedSize : compressedSize;
				if (compressedSize > MAX_RESEED_FILE_SIZE || uncompressedSize > MAX_RESEED_FILE_SIZE
					|| entryUncompressed > MAX_RESEED_UNCOMPRESSED_TOTAL - totalUncompressed)
				{
					LogPrint (eLogError, "Reseed: SU3 file size too large, compressed=", compressedSize,
						" uncompressed=", uncompressedSize);
					return stopWith ("bounds");
				}
				totalUncompressed += entryUncompressed;

				// Both sizes were validated against MAX_RESEED_FILE_SIZE above.
				// The buffers are owned by the vectors so every early return
				// (truncated stream, zlib error, CRC failure) releases them.
				std::vector<uint8_t> compressed (compressedSize);
				s.read ((char *)compressed.data (), compressedSize);
				if (!s.good ())
				{
					LogPrint (eLogError, "Reseed: Truncated zip file data");
					return stopWith ("protocol");
				}
				if (compressionMethod) // we assume Deflate
				{
					// Allocate before touching zlib state so an allocation
					// failure cannot leave an initialised stream behind.
					std::vector<uint8_t> uncompressed (uncompressedSize);
					z_stream inflator;
					memset (&inflator, 0, sizeof (inflator));
					inflateInit2 (&inflator, -MAX_WBITS); // no zlib header
					inflator.next_in = compressed.data ();
					inflator.avail_in = compressedSize;
					inflator.next_out = uncompressed.data ();
					inflator.avail_out = uncompressedSize;
					int err;
					// Require the complete deflate stream. Z_OK only means that
					// more input or output is needed, so accepting it could let
					// a truncated file reach CRC32/AddRouterInfo.
					err = inflate (&inflator, Z_SYNC_FLUSH);
					const size_t produced = uncompressedSize - inflator.avail_out;
					// Release the zlib state before CRC/netDb work so a later
					// exception cannot leak it.
					inflateEnd (&inflator);
					if (err == Z_STREAM_END)
					{
						uncompressed.resize (produced);
						// produced is bounded by MAX_RESEED_FILE_SIZE.
						if (crc32 (0, uncompressed.data (), static_cast<uInt> (uncompressed.size ())) == crc_32)
							addRouterInfo (uncompressed.data (), uncompressed.size ());
						else
						{
							LogPrint (eLogError, "Reseed: CRC32 verification failed");
							SetReseedReason (result.error, "crc");
						}
					}
					else
					{
						LogPrint (eLogError, "Reseed: SU3 decompression error ", err);
						SetReseedReason (result.error, "deflate");
					}
				}
				else // no compression
					addRouterInfo (compressed.data (), compressed.size ());
				if (bitFlag & ZIP_BIT_FLAG_DATA_DESCRIPTOR)
					s.seekg (12, std::ios::cur); // skip data descriptor section if presented (12 = 16 - 4)
			}
			else
			{
				if (signature != ZIP_CENTRAL_DIRECTORY_HEADER_SIGNATURE)
					LogPrint (eLogWarning, "Reseed: Missing zip central directory header");
				break; // no more files
			}
			size_t end = s.tellg ();
			if (end - contentPos >= contentLength) break; // we are beyond contentLength
		}
		// A downloaded bundle can add or skip RouterInfos, but it never deletes
		// a cached peer: an ordinary slow or partially stale mirror must not
		// erase a working peer set.
		if (result.imported == 0 && result.error.empty ())
		{
			if (result.skippedStale > 0)
				SetReseedReason (result.error, "stale bundle");
			else if (considered > 0)
				// Every entry reached the netDb and its store policy refused it.
				SetReseedReason (result.error, "rejected");
		}
		return result;
	}

	const uint8_t ZIP_DATA_DESCRIPTOR_SIGNATURE[] = { 0x50, 0x4B, 0x07, 0x08 };
	bool Reseeder::FindZipDataDescriptor (std::istream & s)
	{
		size_t nextInd = 0;
		while (s.good ()) // a broken stream never reaches eof, see ProcessZIPStream
		{
			uint8_t nextByte;
			s.read ((char *)&nextByte, 1);
			if (nextByte == ZIP_DATA_DESCRIPTOR_SIGNATURE[nextInd])
			{
				nextInd++;
				if (nextInd >= sizeof (ZIP_DATA_DESCRIPTOR_SIGNATURE)) return true;
			}
			else
				nextInd = 0;
		}
		return false;
	}

	void Reseeder::LoadCertificate (const std::string & filename)
	{
		using BioPtr = std::unique_ptr<BIO, decltype (&BIO_free)>;
		using X509Ptr = std::unique_ptr<X509, decltype (&X509_free)>;
		using PkeyPtr = std::unique_ptr<EVP_PKEY, decltype (&EVP_PKEY_free)>;

		BioPtr bio (BIO_new_file (filename.c_str (), "r"), BIO_free);
		X509Ptr cert (bio ? PEM_read_bio_X509 (bio.get (), nullptr, nullptr, nullptr) : nullptr, X509_free);
		if (!cert)
		{
			LogPrint (eLogCritical, "Reseed: Can't open certificate file ", filename);
			return;
		}

		// Extract the issuer CN with the bounded X.509 accessor. The legacy
		// X509_NAME_oneline() format truncates long or escaped names and is not
		// a stable parser.
		char name[256] = { };
		const int cnLength
			= X509_NAME_get_text_by_NID (X509_get_issuer_name (cert.get ()), NID_commonName, name, sizeof (name));
		char * cn = cnLength > 0 ? name : nullptr;

		// Extract the signing key. RSA keeps the modulus only (e = 65537).
		// Ed25519 certificates contribute their 32-byte public key.
		PkeyPtr pubKey (X509_get_pubkey (cert.get ()), EVP_PKEY_free);
		bool stored = false;
#if I2PD_HAVE_EVP_PKEY_IS_A
		const bool isEd25519 = pubKey && EVP_PKEY_is_a (pubKey.get (), "ED25519") == 1;
#else
		const bool isEd25519 = pubKey && EVP_PKEY_base_id (pubKey.get ()) == EVP_PKEY_ED25519;
#endif
		if (isEd25519)
		{
			PublicKey value { };
			size_t keyLength = 0;
			if (EVP_PKEY_get_raw_public_key (pubKey.get (), nullptr, &keyLength) == 1
				&& keyLength == i2p::crypto::EDDSA25519_PUBLIC_KEY_LENGTH
				&& EVP_PKEY_get_raw_public_key (pubKey.get (), value () + (sizeof (value) - keyLength), &keyLength)
					== 1)
			{
				if (cn)
				{
					m_SigningKeys[cn] = value;
					stored = true;
				}
				else
					LogPrint (eLogError, "Reseed: Can't find CN field in ", filename);
			}
			else
				LogPrint (eLogError, "Reseed: Can't extract Ed25519 key from ", filename);
		}
		else
		{
#if I2PD_HAVE_EVP_PKEY_BN_PARAM && !defined(LIBRESSL_VERSION_NUMBER)
			std::unique_ptr<BIGNUM, decltype (&BN_free)> n1 (nullptr, BN_free);
			BIGNUM * rawN = nullptr;
			if (pubKey && EVP_PKEY_get_bn_param (pubKey.get (), OSSL_PKEY_PARAM_RSA_N, &rawN) != 1)
				LogPrint (eLogError, "Reseed: Can't extract the RSA modulus from ", filename);
			n1.reset (rawN);
			const BIGNUM * n = n1.get ();
#else
			const RSA * key = pubKey ? EVP_PKEY_get0_RSA (pubKey.get ()) : nullptr;
			const BIGNUM *e = nullptr, *d = nullptr, *n = nullptr;
			if (key) RSA_get0_key (key, &n, &e, &d);
#endif
			if (n)
			{
				PublicKey value { };
				if (i2p::crypto::bn2buf (n, value, sizeof (value)))
				{
					if (cn)
					{
						m_SigningKeys[cn] = value;
						stored = true;
					}
					else
						LogPrint (eLogError, "Reseed: Can't find CN field in ", filename);
				}
				else
					LogPrint (eLogError, "Reseed: RSA key is too large in ", filename);
			}
			else
				LogPrint (eLogError, "Reseed: Can't extract RSA key from ", filename);
		}
		if (!stored && cn)
			LogPrint (eLogError, "Reseed: Certificate ", filename, " did not provide a usable signing key");
	}

	void Reseeder::LoadCertificates ()
	{
		std::string certDir = i2p::fs::GetCertsDir () + i2p::fs::dirSep + "reseed";

		std::vector<std::string> files;
		int numCertificates = 0;

		if (!i2p::fs::ReadDir (certDir, files))
		{
			LogPrint (eLogWarning, "Reseed: Can't load reseed certificates from ", certDir);
			return;
		}

		for (const std::string & file : files)
		{
			// file.size () - 4 underflows for a short name and compare()
			// throws std::out_of_range, terminating the router during startup.
			if (file.size () < 4 || file.compare (file.size () - 4, 4, ".crt") != 0)
			{
				LogPrint (eLogWarning, "Reseed: Ignoring file ", file);
				continue;
			}
			LoadCertificate (file);
			numCertificates++;
		}
		LogPrint (eLogInfo, "Reseed: ", numCertificates, " certificates loaded");
	}

	std::string Reseeder::HttpsRequest (const std::string & address, int redirects, const std::stop_token & token)
	{
		m_LastTransportError.clear ();
		if (token.stop_requested ())
		{
			SetReseedReason (m_LastTransportError, "cancelled");
			return "";
		}

		i2p::http::URL proxyUrl;
		std::string proxy;
		i2p::config::GetOption ("reseed.proxy", proxy);
		// check for proxy url
		if (proxy.size ())
		{
			// parse
			if (proxyUrl.parse (proxy))
			{
				if (proxyUrl.schema == "http" && !proxyUrl.port)
				{
					proxyUrl.port = 80;
				}
				else if (proxyUrl.schema == "socks" && !proxyUrl.port)
				{
					proxyUrl.port = 1080;
				}
				// check for valid proxy url schema
				if (proxyUrl.schema != "http" && proxyUrl.schema != "socks")
				{
					// The configured value may carry userinfo; only the endpoint
					// is ever logged.
					LogPrint (eLogCritical, "Reseed: Bad proxy url: ", RedactReseedProxyForLog (proxy));
					SetReseedReason (m_LastTransportError, "proxy");
					return "";
				}
			}
			else
			{
				LogPrint (eLogCritical, "Reseed: Bad proxy url: ", RedactReseedProxyForLog (proxy));
				SetReseedReason (m_LastTransportError, "proxy");
				return "";
			}
		}
		i2p::http::URL url;
		if (!url.parse (address))
		{
			LogPrint (eLogCritical, "Reseed: Failed to parse url: ", address);
			SetReseedReason (m_LastTransportError, "url");
			return "";
		}
		url.schema = "https";
		if (!url.port) url.port = 443;

		boost::asio::io_context service;
		boost::system::error_code ecode;
		// Every phase below is bounded by this deadline and by the stop token, so
		// a stalled mirror cannot hold the reseed worker for minutes.
		const auto hostDeadline = std::chrono::steady_clock::now () + RESEED_HOST_TIMEOUT;

		boost::asio::ssl::context ctx (boost::asio::ssl::context::tls_client);
		ctx.set_options (boost::asio::ssl::context::default_workarounds | boost::asio::ssl::context::no_sslv2
			| boost::asio::ssl::context::no_sslv3 | boost::asio::ssl::context::no_tlsv1
			| boost::asio::ssl::context::no_tlsv1_1);
		if (SSL_CTX_set_min_proto_version (ctx.native_handle (), TLS1_2_VERSION) != 1)
		{
			LogPrint (eLogError, "Reseed: Can't set the TLS 1.2 minimum version");
			SetReseedReason (m_LastTransportError, "tls");
			return "";
		}
		// Reseed transport must authenticate the server chain and hostname.
		// SU3 protects the bundle contents, but TLS prevents bootstrap
		// fingerprinting and stale/mirror substitution at the transport layer.
		ctx.set_default_verify_paths (ecode);
		if (ecode)
		{
			LogPrint (eLogError, "Reseed: Can't load system trust store: ", ecode.message ());
			SetReseedReason (m_LastTransportError, "tls");
			return "";
		}
		ctx.set_verify_mode (boost::asio::ssl::context::verify_peer);
		boost::asio::ssl::stream<boost::asio::ip::tcp::socket> s (service, ctx);

		if (proxyUrl.schema.size ())
		{
			// proxy connection
			boost::asio::ip::tcp::resolver resolver (service);
			boost::asio::ip::tcp::resolver::results_type endpoints;
			boost::system::error_code resolveError;
			const bool resolved = RunWithDeadline (
				service, PhaseBudget (hostDeadline), token,
				[&]
				{
					resolver.async_resolve (proxyUrl.host, std::to_string (proxyUrl.port),
						[&] (const boost::system::error_code & ec, boost::asio::ip::tcp::resolver::results_type results)
						{
							resolveError = ec;
							endpoints = std::move (results);
						});
				},
				[&] { resolver.cancel (); });
			if (!resolved || resolveError)
			{
				SetReseedReason (
					m_LastTransportError, resolved ? "resolve" : (token.stop_requested () ? "cancelled" : "timeout"));
				LogPrint (eLogError,
					"Reseed: Can't resolve the proxy endpoint: ", resolved ? resolveError.message () : "no answer");
				return "";
			}
			bool connected = false;
			for (const auto & entry : endpoints)
			{
				boost::system::error_code connectError;
				const bool done = RunWithDeadline (
					service, PhaseBudget (hostDeadline), token,
					[&]
					{
						s.lowest_layer ().async_connect (
							entry.endpoint (), [&] (const boost::system::error_code & ec) { connectError = ec; });
					},
					[&] { s.lowest_layer ().cancel (); });
				if (!done)
				{
					SetReseedReason (m_LastTransportError, token.stop_requested () ? "cancelled" : "timeout");
					break;
				}
				if (!connectError)
				{
					connected = true;
					break;
				}
			}
			if (!connected)
			{
				if (m_LastTransportError.empty ()) SetReseedReason (m_LastTransportError, "connect");
				LogPrint (eLogError, "Reseed: Failed to connect to the proxy");
				return "";
			}

			auto & sock = s.next_layer ();
			if (proxyUrl.schema == "http")
			{
				i2p::http::HTTPReq proxyReq;
				i2p::http::HTTPRes proxyRes;
				proxyReq.method = "CONNECT";
				proxyReq.version = "HTTP/1.1";
				proxyReq.uri = url.host + ":" + std::to_string (url.port);
				auto auth = i2p::http::CreateBasicAuthorizationString (proxyUrl.user, proxyUrl.pass);
				if (!auth.empty ()) proxyReq.AddHeader ("Proxy-Authorization", auth);

				const std::string connectRequest = proxyReq.to_string ();
				boost::system::error_code writeError;
				const bool written = RunWithDeadline (
					service, PhaseBudget (hostDeadline), token,
					[&]
					{
						boost::asio::async_write (sock, boost::asio::buffer (connectRequest),
							[&] (const boost::system::error_code & ec, std::size_t) { writeError = ec; });
					},
					[&] { sock.cancel (); });
				if (!written || writeError)
				{
					sock.close ();
					SetReseedReason (
						m_LastTransportError, written ? "proxy" : (token.stop_requested () ? "cancelled" : "timeout"));
					LogPrint (
						eLogError, "Reseed: HTTP CONNECT write error: ", written ? writeError.message () : "no answer");
					return "";
				}
				boost::asio::streambuf readbuf;
				boost::system::error_code readError;
				const bool read = RunWithDeadline (
					service, PhaseBudget (hostDeadline), token,
					[&]
					{
						boost::asio::async_read_until (sock, readbuf, "\r\n\r\n",
							[&] (const boost::system::error_code & ec, std::size_t) { readError = ec; });
					},
					[&] { sock.cancel (); });
				if (!read || readError)
				{
					sock.close ();
					SetReseedReason (
						m_LastTransportError, read ? "proxy" : (token.stop_requested () ? "cancelled" : "timeout"));
					LogPrint (
						eLogError, "Reseed: HTTP CONNECT read error: ", read ? readError.message () : "no answer");
					return "";
				}
				if (proxyRes.parse (std::string { boost::asio::buffers_begin (readbuf.data ()),
						boost::asio::buffers_begin (readbuf.data ()) + readbuf.size () })
					<= 0)
				{
					sock.close ();
					LogPrint (eLogError, "Reseed: HTTP CONNECT malformed reply");
					SetReseedReason (m_LastTransportError, "proxy");
					return "";
				}
				if (proxyRes.code != 200)
				{
					sock.close ();
					LogPrint (eLogError, "Reseed: HTTP CONNECT got bad status: ", proxyRes.code);
					SetReseedReason (m_LastTransportError, "proxy");
					return "";
				}
			}
			else
			{
				// assume socks if not http, is checked before this for other types
				// TODO: support username/password auth etc
				bool success = false;
				boost::system::error_code socksError;
				boost::asio::steady_timer timer (service);
				timer.expires_after (PhaseBudget (hostDeadline));
				timer.async_wait (
					[&] (const boost::system::error_code & ec)
					{
						if (ec) return;
						sock.close ();
					});
				std::stop_callback onStop (token,
					[&service, &sock]
					{
						// Close on the I/O thread; the request frame owns the
						// socket for the whole handshake.
						boost::asio::post (service, [&sock] { sock.close (); });
					});
				i2p::transport::Socks5Handshake (sock, std::make_pair (url.host, url.port),
					[&] (const boost::system::error_code & ec)
					{
						timer.cancel ();
						if (!ec)
							success = true;
						else
							socksError = ec;
					});
				service.restart ();
				service.run (); // execute all async operations
				timer.cancel ();
				service.restart ();
				service.run (); // drain the aborted timer handler
				if (!success)
				{
					sock.close ();
					if (socksError && socksError != boost::asio::error::operation_aborted)
						LogPrint (eLogError, "Reseed: SOCKS handshake failed: ", socksError.message ());
					else
						LogPrint (eLogError, "Reseed: SOCKS handshake did not complete");
					SetReseedReason (m_LastTransportError, token.stop_requested () ? "cancelled" : "timeout");
					return "";
				}
			}
		}
		else
		{
			// direct connection
			boost::asio::ip::tcp::resolver resolver (service);
			boost::asio::ip::tcp::resolver::results_type endpoints;
			boost::system::error_code resolveError;
			const bool resolved = RunWithDeadline (
				service, PhaseBudget (hostDeadline), token,
				[&]
				{
					resolver.async_resolve (url.host, std::to_string (url.port),
						[&] (const boost::system::error_code & ec, boost::asio::ip::tcp::resolver::results_type results)
						{
							resolveError = ec;
							endpoints = std::move (results);
						});
				},
				[&] { resolver.cancel (); });
			if (!resolved || resolveError)
			{
				SetReseedReason (
					m_LastTransportError, resolved ? "resolve" : (token.stop_requested () ? "cancelled" : "timeout"));
				LogPrint (eLogError, "Reseed: Failed to connect to ", url.host);
				return "";
			}
			bool connected = false;
			for (const auto & entry : endpoints)
			{
				boost::asio::ip::tcp::endpoint ep = entry.endpoint ();
				bool supported = false;
				if (!ep.address ().is_unspecified ())
				{
					if (ep.address ().is_v4 ())
						supported = i2p::context.SupportsV4 ();
					else if (ep.address ().is_v6 ())
						supported = i2p::util::net::IsYggdrasilAddress (ep.address ()) ? i2p::context.SupportsMesh ()
																					   : i2p::context.SupportsV6 ();
				}
				if (!supported) continue;
				boost::system::error_code connectError;
				const bool done = RunWithDeadline (
					service, PhaseBudget (hostDeadline), token,
					[&]
					{
						s.lowest_layer ().async_connect (
							ep, [&] (const boost::system::error_code & ec) { connectError = ec; });
					},
					[&] { s.lowest_layer ().cancel (); });
				if (!done)
				{
					SetReseedReason (m_LastTransportError, token.stop_requested () ? "cancelled" : "timeout");
					break;
				}
				if (!connectError)
				{
					LogPrint (eLogDebug, "Reseed: Connected to ", i2p::util::net::FormatAddress (ep.address ()));
					connected = true;
					break;
				}
			}
			if (!connected)
			{
				if (m_LastTransportError.empty ()) SetReseedReason (m_LastTransportError, "connect");
				LogPrint (eLogError, "Reseed: Failed to connect to ", url.host);
				return "";
			}
		}

		if (SSL_set_tlsext_host_name (s.native_handle (), url.host.c_str ()) != 1)
		{
			LogPrint (eLogError, "Reseed: Failed to set SNI for ", url.host);
			SetReseedReason (m_LastTransportError, "tls");
			return "";
		}
#if I2PD_HAVE_SSL_SET1_DNSNAME
		if (SSL_set1_dnsname (s.native_handle (), url.host.c_str ()) != 1)
#elif I2PD_HAVE_SSL_SET1_HOST
		if (SSL_set1_host (s.native_handle (), url.host.c_str ()) != 1)
#else
#error "No TLS hostname verification API is available"
#endif
		{
			LogPrint (eLogError, "Reseed: Failed to install hostname verification for ", url.host);
			SetReseedReason (m_LastTransportError, "tls");
			return "";
		}
		boost::system::error_code handshakeError;
		const bool handshaked = RunWithDeadline (
			service, PhaseBudget (hostDeadline), token,
			[&]
			{
				s.async_handshake (boost::asio::ssl::stream_base::client,
					[&] (const boost::system::error_code & ec) { handshakeError = ec; });
			},
			[&] { s.lowest_layer ().cancel (); });
		if (!handshaked)
		{
			LogPrint (eLogError, "Reseed: SSL handshake timed out for ", url.host);
			SetReseedReason (m_LastTransportError, token.stop_requested () ? "cancelled" : "timeout");
			return "";
		}
		if (handshakeError)
		{
			LogPrint (eLogError, "Reseed: SSL handshake failed: ", handshakeError.message ());
			SetReseedReason (m_LastTransportError, "tls");
			return "";
		}
		LogPrint (eLogDebug, "Reseed: Connected to ", url.host, ":", url.port);
		return ReseedRequest (s, service, url.to_string (), redirects, token, hostDeadline);
	}

	template <typename Stream>
	std::string Reseeder::ReseedRequest (Stream & s, boost::asio::io_context & service, const std::string & uri,
		int redirects, const std::stop_token & token, std::chrono::steady_clock::time_point hostDeadline)
	{
		m_LastTransportError.clear ();
		bool follow;
		i2p::config::GetOption ("reseed.followredirect", follow);
		i2p::http::HTTPReq req;
		i2p::http::HTTPRes res;

		req.uri = uri;
		req.AddHeader ("User-Agent", "Wget/1.11.4");
		req.AddHeader ("Connection", "close");

		const std::string request = req.to_string ();
		boost::system::error_code writeError;
		const bool written = RunWithDeadline (
			service, PhaseBudget (hostDeadline), token,
			[&]
			{
				boost::asio::async_write (s, boost::asio::buffer (request),
					[&] (const boost::system::error_code & ec, std::size_t) { writeError = ec; });
			},
			[&] { s.lowest_layer ().cancel (); });
		if (!written)
		{
			LogPrint (eLogWarning, "Reseed: Request to ", uri, " timed out");
			SetReseedReason (m_LastTransportError, token.stop_requested () ? "cancelled" : "timeout");
			return "";
		}
		if (writeError)
		{
			LogPrint (eLogWarning, "Reseed: Request write to ", uri, " failed: ", writeError.message ());
			SetReseedReason (m_LastTransportError, "connect");
			return "";
		}

		// read response
		std::stringstream rs;
		char recv_buf[1024];
		size_t received = 0;
		for (;;)
		{
			if (token.stop_requested ())
			{
				SetReseedReason (m_LastTransportError, "cancelled");
				return "";
			}
			size_t l = 0;
			boost::system::error_code readError;
			const bool read = RunWithDeadline (
				service, PhaseBudget (hostDeadline), token,
				[&]
				{
					s.async_read_some (boost::asio::buffer (recv_buf, sizeof (recv_buf)),
						[&] (const boost::system::error_code & ec, std::size_t n)
						{
							readError = ec;
							l = n;
						});
				},
				[&] { s.lowest_layer ().cancel (); });
			if (!read)
			{
				LogPrint (eLogWarning, "Reseed: Response from ", uri, " timed out");
				SetReseedReason (m_LastTransportError, token.stop_requested () ? "cancelled" : "timeout");
				return "";
			}
			if (readError) break; // parse what arrived; the HTTP parser decides
			if (!l) break;
			if (l > MAX_RESEED_HTTP_RESPONSE_SIZE - received)
			{
				LogPrint (eLogError, "Reseed: Response from ", uri, " exceeds size limit");
				SetReseedReason (m_LastTransportError, "bounds");
				return "";
			}
			received += l;
			rs.write (recv_buf, l);
		}

		// process response
		std::string data = rs.str ();
		int len = res.parse (data);
		if (len <= 0)
		{
			LogPrint (eLogWarning, "Reseed: Incomplete/broken response from ", uri);
			SetReseedReason (m_LastTransportError, "http");
			return "";
		}

		if ((res.code == 301 || res.code == 302 || res.code == 307) && follow)
		{
			LogPrint (eLogDebug, "Reseed: Received redirect from ", uri);

			std::string location = res.get_header ("Location");
			if (location.length () == 0)
			{
				LogPrint (eLogWarning, "Reseed: Broken redirect from ", uri);
				SetReseedReason (m_LastTransportError, "http");
				return "";
			}
			if (redirects >= MAX_RESEED_REDIRECTS)
			{
				LogPrint (eLogWarning, "Reseed: Too many redirects from ", uri);
				SetReseedReason (m_LastTransportError, "http");
				return "";
			}
			bool isHttps = (location.length () > 8 && location.substr (0, 8) == "https://");
			return (isHttps ? HttpsRequest (location, redirects + 1, token)
							: YggdrasilRequest (location, redirects + 1, token));
		}

		if (res.code != 200)
		{
			LogPrint (eLogError, "Reseed: Failed to reseed from ", uri, ", http code ", res.code);
			SetReseedReason (m_LastTransportError, "http");
			return "";
		}

		data.erase (0, len); /* drop http headers from response */
		LogPrint (eLogDebug, "Reseed: Got ", data.length (), " bytes of data from ", uri);
		if (res.is_chunked ())
		{
			std::stringstream in (data), out;
			if (!i2p::http::MergeChunkedResponse (in, out))
			{
				LogPrint (eLogWarning, "Reseed: Failed to merge chunked response from ", uri);
				SetReseedReason (m_LastTransportError, "http");
				return "";
			}
			LogPrint (eLogDebug, "Reseed: Got ", data.length (), "(", out.tellg (), ") bytes of data from ", uri);
			data = out.str ();
		}
		return data;
	}

	std::string Reseeder::YggdrasilRequest (const std::string & address, int redirects, const std::stop_token & token)
	{
		m_LastTransportError.clear ();
		i2p::http::URL url;
		if (!url.parse (address))
		{
			LogPrint (eLogError, "Reseed: Failed to parse url: ", address);
			SetReseedReason (m_LastTransportError, "url");
			return "";
		}
		url.schema = "http";
		if (!url.port) url.port = 80;

		boost::asio::io_context service;
		const auto hostDeadline = std::chrono::steady_clock::now () + RESEED_HOST_TIMEOUT;
		boost::asio::ip::tcp::socket s (service, boost::asio::ip::tcp::v6 ());

		boost::asio::ip::tcp::resolver resolver (service);
		boost::asio::ip::tcp::resolver::results_type endpoints;
		boost::system::error_code resolveError;
		const bool resolved = RunWithDeadline (
			service, PhaseBudget (hostDeadline), token,
			[&]
			{
				resolver.async_resolve (url.host, std::to_string (url.port),
					[&] (const boost::system::error_code & ec, boost::asio::ip::tcp::resolver::results_type results)
					{
						resolveError = ec;
						endpoints = std::move (results);
					});
			},
			[&] { resolver.cancel (); });
		if (!resolved || resolveError)
		{
			LogPrint (eLogError, "Reseed: Yggdrasil: Couldn't resolve ", url.host, ": ",
				resolved ? resolveError.message () : "no answer");
			SetReseedReason (
				m_LastTransportError, resolved ? "resolve" : (token.stop_requested () ? "cancelled" : "timeout"));
			return "";
		}
		bool connected = false;
		for (const auto & entry : endpoints)
		{
			boost::asio::ip::tcp::endpoint ep = entry.endpoint ();
			if (!i2p::util::net::IsYggdrasilAddress (ep.address ()) || !i2p::context.SupportsMesh ()) continue;
			LogPrint (eLogDebug, "Reseed: Yggdrasil: Resolved to ", i2p::util::net::FormatAddress (ep.address ()));
			boost::system::error_code connectError;
			const bool done = RunWithDeadline (
				service, PhaseBudget (hostDeadline), token,
				[&] { s.async_connect (ep, [&] (const boost::system::error_code & ec) { connectError = ec; }); },
				[&] { s.cancel (); });
			if (!done)
			{
				SetReseedReason (m_LastTransportError, token.stop_requested () ? "cancelled" : "timeout");
				break;
			}
			if (!connectError)
			{
				connected = true;
				break;
			}
		}
		if (!connected)
		{
			if (m_LastTransportError.empty ()) SetReseedReason (m_LastTransportError, "connect");
			LogPrint (eLogError, "Reseed: Yggdrasil: Failed to connect to ", url.host);
			return "";
		}

		LogPrint (eLogDebug, "Reseed: Yggdrasil: Connected to ", url.host, ":", url.port);
		return ReseedRequest (s, service, url.to_string (), redirects, token, hostDeadline);
	}

}
}

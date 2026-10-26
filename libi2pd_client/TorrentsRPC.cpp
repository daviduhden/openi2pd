/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: TorrentsRPC.
 *
 * Implements a Transmission-style JSON-RPC endpoint restricted to loopback
 * HTTP, with optional Basic authentication, strict POST and application/json
 * checks, and bounded bodies, fields and torrent IDs. A subset of the
 * Transmission method set is marshalled to the torrent service thread with a
 * fixed timeout so a stalled engine cannot pin an RPC thread.
 *
 */

#ifndef NO_TORRENTS

#include <cstring>
#include <boost/json.hpp>
#include <boost/url.hpp>
#include <boost/url/encode.hpp>
#include <boost/url/grammar/alnum_chars.hpp>
#include <boost/algorithm/hex.hpp>
#include <vector>
#include <array>
#include <chrono>
#include <functional>
#include <future>
#include <filesystem>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <unordered_set>
#include <utility>
#include "Base.h"
#include "Log.h"
#include "HTTP.h"
#include "Timestamp.h"
#include "Torrents.h"
#include "TorrentsControl.h"
#include "TorrentsTunnel.h"
#include "TorrentsRPC.h"

namespace i2p
{
namespace torrents
{
	namespace
	{
		// A maximum-size v1 metainfo is MAX_NUM_TORRENT_PIECES * SHA_DIGEST_LENGTH
		// bytes of piece hashes plus a small amount of metadata, base64 encoded.
		constexpr size_t MAX_RPC_BODY_SIZE = 32 * 1024 * 1024; // 32 MiB
		constexpr size_t MAX_RPC_METADATA_SIZE = 24 * 1024 * 1024; // 24 MiB
		constexpr size_t MAX_RPC_MAGNET_LENGTH = 2048;
		constexpr size_t MAX_RPC_TRACKERS_PER_REQUEST = 32;
		constexpr size_t MAX_RPC_TRACKER_LENGTH = 2048;
		constexpr size_t MAX_RPC_FIELDS_PER_REQUEST = 64;
		constexpr size_t MAX_RPC_CREDENTIAL_LENGTH = 256;
		// One request must not be able to enqueue an unbounded number of
		// torrent lookups/commands or repeat the same id thousands of times.
		constexpr size_t MAX_RPC_TORRENT_IDS = 1024;

		bool IsValidCredential (std::string_view credential)
		{
			return !credential.empty () && credential.size () <= MAX_RPC_CREDENTIAL_LENGTH
				&& credential.find ('\0') == std::string_view::npos && credential.find ('\r') == std::string_view::npos
				&& credential.find ('\n') == std::string_view::npos;
		}

		bool SecureEquals (std::string_view a, std::string_view b)
		{
			if (a.size () != b.size ()) return false;
			unsigned char difference = 0;
			for (size_t i = 0; i < a.size (); i++) difference |= (unsigned char)a[i] ^ (unsigned char)b[i];
			return difference == 0;
		}

		boost::asio::ip::tcp::endpoint MakeLoopbackEndpoint (std::string_view address, uint16_t port)
		{
			boost::system::error_code ec;
			auto bindAddress = boost::asio::ip::make_address (std::string (address), ec);
			if (ec || !bindAddress.is_loopback ())
				throw std::invalid_argument ("Torrents RPC address must be a loopback address (127.0.0.1 or ::1)");
			return { bindAddress, port };
		}

		// One RPC service round-trip is bounded by the same interval as the
		// control plane. A stalled or stopped tunnel service must never pin an
		// RPC I/O thread indefinitely.
		constexpr std::chrono::milliseconds RPC_SERVICE_TIMEOUT = CONTROL_DEFAULT_TIMEOUT;
		constexpr std::string_view RPC_SERVICE_TIMEOUT_ERROR = "Torrent service timeout";
		constexpr std::string_view RPC_SERVICE_FAILURE_ERROR = "Internal error";

		struct DHTStatistics
		{
			size_t numNodes = 0;
			size_t numBuckets = 0;
			size_t numTorrents = 0;
			size_t numHeardFrom = 0;
			size_t numFailedPeers = 0;
		};

		struct SessionStatistics
		{
			size_t torrentCount = 0;
			size_t downloaded = 0;
			size_t uploaded = 0;
			uint64_t downloadRate = 0;
			uint64_t uploadRate = 0;
			int stopped = 0;
		};

		struct TorrentSummary
		{
			std::string name;
			std::string hexHash;
		};

		struct ServiceTaskVoid
		{
		};

		// Runs one operation on the torrent service and waits for it with a
		// fixed bound. The packaged task owns its result, so a timed-out task
		// can only complete into its own shared state and never writes to
		// caller stack storage.
		template <typename F> auto RunOnTorrentService (const std::shared_ptr<TorrentsTunnel> & tunnel, F && operation)
		{
			using OperationResult = std::invoke_result_t<F &>;
			using Result = std::conditional_t<std::is_void_v<OperationResult>, ServiceTaskVoid, OperationResult>;
			struct Outcome
			{
				bool success = false;
				bool timedOut = false;
				Result value { };
			};

			Outcome outcome;
			if (!tunnel) return outcome;
			std::shared_ptr<std::packaged_task<Result ()>> task;
			try
			{
				task = std::make_shared<std::packaged_task<Result ()>> (
					[operation = std::forward<F> (operation)] () mutable -> Result
					{
						if constexpr (std::is_void_v<OperationResult>)
						{
							operation ();
							return { };
						}
						else
							return operation ();
					});
			}
			catch (const std::exception & ex)
			{
				LogPrint (eLogWarning, "TorrentsRPC: Failed to create service task: ", ex.what ());
				return outcome;
			}
			auto future = task->get_future ();
			try
			{
				boost::asio::post (tunnel->GetService (), [task] () { (*task) (); });
			}
			catch (const std::exception & ex)
			{
				LogPrint (eLogWarning, "TorrentsRPC: Failed to queue service task: ", ex.what ());
				return outcome;
			}
			if (future.wait_for (RPC_SERVICE_TIMEOUT) != std::future_status::ready)
			{
				LogPrint (eLogWarning, "TorrentsRPC: Torrent service task timed out");
				outcome.timedOut = true;
				return outcome;
			}
			try
			{
				outcome.value = future.get ();
				outcome.success = true;
			}
			catch (const std::exception & ex)
			{
				LogPrint (eLogWarning, "TorrentsRPC: Torrent service task failed: ", ex.what ());
			}
			catch (...)
			{
				LogPrint (eLogWarning, "TorrentsRPC: Torrent service task failed");
			}
			return outcome;
		}

	}

	class JSONRPCHandler
	{
	public:
		JSONRPCHandler (std::shared_ptr<TorrentsTunnel> tunnel)
			: m_Tunnel (tunnel) { };

		std::string HandleRequest (std::string_view request);

	private:
		std::string SuccessResponse (int64_t tag, boost::json::object && arguments);
		std::string ErrorResponse (int64_t tag, std::string_view message);
		static int64_t GetTag (boost::json::object & jsonRequest);
		static bool GetTorrentIds (boost::json::object & arguments, std::vector<int> & torrentIds);
		boost::json::value GetFieldValue (std::string_view field, std::shared_ptr<Torrent> torrent) const;
		boost::json::object GetTorrentObject (
			std::shared_ptr<Torrent> torrent, int id, const boost::json::array & fields) const;
		boost::json::array GetTorrentTableRow (std::shared_ptr<Torrent> torrent, int id,
			const boost::json::array & fields, boost::json::array & header) const;
		static boost::json::array GetPeers (std::shared_ptr<Torrent> torrent);
		boost::json::array GetTrackers (std::shared_ptr<Torrent> torrent) const;
		boost::json::array GetTrackerStats (std::shared_ptr<Torrent> torrent) const;
		boost::json::array GetFiles (std::shared_ptr<Torrent> torrent) const;
		static boost::json::array GetFileStats (std::shared_ptr<Torrent> torrent);
		static std::string RecognizeClientByPeerID (const PeerConnection::PeerID & peerID);

		std::string HandleTorrentAdd (boost::json::object && jsonRequest);
		std::string HandleTorrentRemove (boost::json::object && jsonRequest);
		std::string HandleTorrentGet (boost::json::object && jsonRequest);
		std::string HandleTorrentStop (boost::json::object && jsonRequest);
		std::string HandleTorrentStart (boost::json::object && jsonRequest);
		std::string HandleSessionGet (boost::json::object && jsonRequest); // for transmission-rpc library
		std::string HandleSessionStats (boost::json::object && jsonRequest);
		std::string HandleTorrentSet (boost::json::object && jsonRequest);
		std::string HandleTorrentReannounce (boost::json::object && jsonRequest);
		std::string HandleDHTStats (boost::json::object && jsonRequest);

	private:
		std::shared_ptr<TorrentsTunnel> m_Tunnel;
	};

	std::string JSONRPCHandler::SuccessResponse (int64_t tag, boost::json::object && arguments)
	{
		boost::json::object response;
		response["result"] = "success";
		response["arguments"] = arguments;
		if (tag) response["tag"] = tag;
		return boost::json::serialize (response);
	}

	std::string JSONRPCHandler::ErrorResponse (int64_t tag, std::string_view message)
	{
		boost::json::object response, arguments;
		if (tag) response["tag"] = tag;
		response["arguments"] = arguments;
		response["result"] = message;
		return boost::json::serialize (response);
	}

	int64_t JSONRPCHandler::GetTag (boost::json::object & jsonRequest)
	{
		if (jsonRequest.contains ("tag"))
		{
			const auto & tag = jsonRequest.at ("tag");
			if (tag.is_int64 ()) return tag.as_int64 ();
		}
		return 0;
	}

	bool JSONRPCHandler::GetTorrentIds (boost::json::object & arguments, std::vector<int> & torrentIds)
	{
		if (!arguments.contains ("ids")) return true;
		const auto & ids = arguments.at ("ids");
		std::unordered_set<int> seen;
		auto addId = [&torrentIds, &seen] (const boost::json::value & value) -> bool
		{
			if (!value.is_int64 ()) return false;
			auto id = value.as_int64 ();
			if (id <= 0 || id > std::numeric_limits<int>::max ()) return false;
			if (seen.insert ((int)id).second)
			{
				if (torrentIds.size () >= MAX_RPC_TORRENT_IDS) return false;
				torrentIds.push_back ((int)id);
			}
			return true;
		};
		if (ids.is_array ())
		{
			if (ids.as_array ().size () > MAX_RPC_TORRENT_IDS) return false;
			for (const auto & it : ids.as_array ())
				if (!addId (it)) return false;
		}
		else if (!addId (ids))
			return false;
		return true;
	}

	std::string JSONRPCHandler::HandleRequest (std::string_view request)
	{
		const static std::map<std::string_view, std::string (JSONRPCHandler::*) (boost::json::object && jsonRequest)>
			methods = { { "torrent-add", &JSONRPCHandler::HandleTorrentAdd },
				{ "torrent-remove", &JSONRPCHandler::HandleTorrentRemove },
				{ "torrent-get", &JSONRPCHandler::HandleTorrentGet },
				{ "torrent-stop", &JSONRPCHandler::HandleTorrentStop },
				{ "torrent-start", &JSONRPCHandler::HandleTorrentStart },
				{ "session-get", &JSONRPCHandler::HandleSessionGet },
				{ "session-stats", &JSONRPCHandler::HandleSessionStats },
				{ "torrent-set", &JSONRPCHandler::HandleTorrentSet },
				{ "torrent-reannounce", &JSONRPCHandler::HandleTorrentReannounce },
				{ "dht-stats", &JSONRPCHandler::HandleDHTStats } };

		try
		{
			boost::json::parse_options options;
			options.max_depth = 32;
			auto parsed = boost::json::parse (request, { }, options);
			if (!parsed.is_object ()) return ErrorResponse (0, "Invalid Request");
			auto & jsonRequest = parsed.as_object ();
			if (!jsonRequest.contains ("method") || !jsonRequest.at ("method").is_string ())
			{
				LogPrint (eLogInfo, "TorrentsRPC: Missing or invalid method");
				return ErrorResponse (GetTag (jsonRequest), "Invalid Request");
			}
			auto method = jsonRequest.at ("method").as_string ();
			auto it = methods.find (method);
			if (it == methods.end ())
			{
				LogPrint (eLogInfo, "TorrentsRPC: Method not found ", method);
				return ErrorResponse (GetTag (jsonRequest), "Method not found");
			}
			if (jsonRequest.contains ("arguments") && !jsonRequest.at ("arguments").is_object ())
				return ErrorResponse (GetTag (jsonRequest), "Invalid Request");
			return (this->*(it->second)) (std::move (jsonRequest));
		}
		catch (const std::exception & ex)
		{
			LogPrint (eLogInfo, "TorrentsRPC: Failed to parse JSON: ", ex.what ());
			return ErrorResponse (0, "Parse error");
		}
	}

	std::string JSONRPCHandler::HandleSessionGet (boost::json::object && jsonRequest)
	{
		boost::json::object units;
		units["memory-bytes"] = 1024;
		units["memory-units"] = boost::json::array { "KiB", "MiB", "GiB", "TiB" };
		units["size-bytes"] = 1000;
		units["size-units"] = boost::json::array { "kB", "MB", "GB", "TB" };
		units["speed-bytes"] = 1000;
		units["speed-units"] = boost::json::array { "kB/s", "MB/s", "GB/s", "TB/s" };

		boost::json::object response;
		response["alt-speed-down"] = 50;
		response["alt-speed-enabled"] = false;
		response["alt-speed-time-begin"] = 540;
		response["alt-speed-time-day"] = 127;
		response["alt-speed-time-enabled"] = false;
		response["alt-speed-time-end"] = 1020;
		response["alt-speed-up"] = 50;
		response["anti-brute-force-enabled"] = false;
		response["anti-brute-force-threshold"] = 100;
		response["blocklist-enabled"] = false;
		response["blocklist-size"] = 0;
		response["blocklist-url"] = "";
		response["cache-size-mb"] = 4;
		response["config-dir"] = i2p::fs::GetUTF8DataDir (); // TODO:
		response["default-trackers"] = "";

		response["dht-enabled"] = m_Tunnel->SupportsDHT ();
		response["download-dir"] = m_Tunnel->GetTorrentsDir ().string ();
		response["download-dir-free-space"] = 9999999999; // TODO:
		response["download-queue-enabled"] = true; // TODO:
		response["download-queue-size"] = 5; // TODO:
		response["encryption"] = "required";
		response["idle-seeding-limit"] = 30;
		response["idle-seeding-limit-enabled"] = false;
		response["incomplete-dir"] = m_Tunnel->GetTorrentsDir ().string ();
		response["incomplete-dir-enabled"] = false;
		response["lpd-enabled"] = false;
		response["peer-limit-global"] = 200; // TODO:
		response["peer-limit-per-torrent"] = 50; // TODO:
		response["peer-port"] = 6881;
		response["peer-port-random-on-start"] = false;
		response["pex-enabled"] = true;
		response["port-forwarding-enabled"] = false;
		response["queue-stalled-enabled"] = false;
		response["queue-stalled-minutes"] = 30;
		response["rename-partial-files"] = true;
		response["rpc-version"] = 17;
		response["rpc-version-minimum"] = 14;
		response["rpc-version-semver"] = "5.3.0";
		response["script-torrent-added-enabled"] = false;
		response["script-torrent-added-filename"] = "";
		response["script-torrent-done-enabled"] = false;
		response["script-torrent-done-filename"] = "";
		response["script-torrent-done-seeding-enabled"] = false;
		response["script-torrent-done-seeding-filename"] = "";
		response["seed-queue-enabled"] = false; // TODO:
		response["seed-queue-size"] = 10; // TODO:
		response["seedRatioLimit"] = 2;
		response["seedRatioLimited"] = false;
		response["speed-limit-down"] = 100;
		response["speed-limit-down-enabled"] = false;
		response["speed-limit-up"] = 100;
		response["speed-limit-up-enabled"] = false;
		response["start-added-torrents"] = true;
		response["tcp-enabled"] = true;
		response["trash-original-torrent-files"] = false;
		response["units"] = units;
		response["utp-enabled"] = true;
		response["version"] = "4.0.0 (280ace12f8)";
		return SuccessResponse (GetTag (jsonRequest), std::move (response));
	}

	std::string JSONRPCHandler::HandleTorrentAdd (boost::json::object && jsonRequest)
	{
		auto arguments = jsonRequest.at ("arguments").as_object ();
		bool hasMetainfo = arguments.contains ("metainfo"), hasFilename = arguments.contains ("filename");
		if (hasMetainfo == hasFilename) // exactly one source is required
			return ErrorResponse (GetTag (jsonRequest), "Invalid Request");
		std::shared_ptr<Torrent> torrent;
		int id = 0;
		if (hasMetainfo)
		{
			const auto & metainfo = arguments.at ("metainfo");
			if (!metainfo.is_string ()) return ErrorResponse (GetTag (jsonRequest), "invalid or corrupt torrent file");
			const auto & b64string = metainfo.as_string ();
			std::string_view b64torrent (b64string.data (), b64string.size ());
			if (b64torrent.empty () || b64torrent.size () % 4) // base64 must be non-empty and multiple of 4
				return ErrorResponse (GetTag (jsonRequest), "invalid or corrupt torrent file");
			auto decodedSize = boost::beast::detail::base64::decoded_size (b64torrent.size ());
			if (!decodedSize || decodedSize > MAX_RPC_METADATA_SIZE)
				return ErrorResponse (GetTag (jsonRequest), "invalid or corrupt torrent file");
			std::string torrentFileContent (decodedSize, '\0');
			auto [written, consumed] = boost::beast::detail::base64::decode (
				torrentFileContent.data (), b64torrent.data (), b64torrent.size ());
			if (!written || consumed != b64torrent.size ())
				return ErrorResponse (GetTag (jsonRequest), "invalid or corrupt torrent file");
			torrentFileContent.resize (written);
			std::tie (torrent, id) = m_Tunnel->AddTorrentFromRpc (torrentFileContent);
		}
		else
		{
			const auto & filename = arguments.at ("filename");
			if (!filename.is_string ()) return ErrorResponse (GetTag (jsonRequest), "Invalid Request");
			const auto & magnetString = filename.as_string ();
			std::string_view magnet (magnetString.data (), magnetString.size ());
			if (magnet.empty () || magnet.size () > MAX_RPC_MAGNET_LENGTH)
				return ErrorResponse (GetTag (jsonRequest), "Invalid magnet link");
			std::tie (torrent, id) = m_Tunnel->AddMagnetFromRpc (magnet);
		}

		if (torrent)
		{
			boost::json::object response, torrentInfo;
			// Name and hash are owned by the service thread and a magnet can
			// receive its metadata concurrently, so copy them there.
			auto summary = RunOnTorrentService (m_Tunnel,
				[torrent] ()
				{
					TorrentSummary s;
					s.name = std::string (torrent->GetName ());
					boost::algorithm::hex (torrent->GetInfoHash ().begin (), torrent->GetInfoHash ().end (),
						std::back_inserter (s.hexHash));
					return s;
				});
			if (!summary.success)
				return ErrorResponse (
					GetTag (jsonRequest), summary.timedOut ? RPC_SERVICE_TIMEOUT_ERROR : RPC_SERVICE_FAILURE_ERROR);
			torrentInfo["id"] = id;
			torrentInfo["hashString"] = std::move (summary.value.hexHash);
			torrentInfo["name"] = std::move (summary.value.name);
			if (id)
			{
				response["torrent-added"] = torrentInfo;
				LogPrint (eLogDebug, "TorrentsRPC: torrent added ", torrentInfo["name"].as_string ());
			}
			else
			{
				response["torrent-duplicate"] = torrentInfo;
				LogPrint (eLogDebug, "TorrentsRPC: duplicate torrent ", torrentInfo["name"].as_string ());
			}
			return SuccessResponse (GetTag (jsonRequest), std::move (response));
		}
		else
			return ErrorResponse (GetTag (jsonRequest), "invalid or corrupt torrent file");
	}

	std::string JSONRPCHandler::HandleTorrentRemove (boost::json::object && jsonRequest)
	{
		auto arguments = jsonRequest.at ("arguments").as_object ();
		bool deleteFiles = false;
		if (arguments.contains ("delete-local-data"))
		{
			if (!arguments.at ("delete-local-data").is_bool ())
				return ErrorResponse (GetTag (jsonRequest), "Invalid Request");
			deleteFiles = arguments.at ("delete-local-data").as_bool ();
		}
		std::vector<int> torrentIds;
		if (!GetTorrentIds (arguments, torrentIds)) return ErrorResponse (GetTag (jsonRequest), "Invalid torrent ids");
		if (arguments.contains ("ids"))
		{
			// An explicit empty list is a no-op; only a missing ids member means all.
			for (auto id : torrentIds) m_Tunnel->RemoveTorrent (id, deleteFiles);
		}
		else
			m_Tunnel->RemoveAllTorrents (deleteFiles);
		boost::json::object response; // always empty
		return SuccessResponse (GetTag (jsonRequest), std::move (response));
	}

	std::string JSONRPCHandler::HandleTorrentReannounce (boost::json::object && jsonRequest)
	{
		auto arguments = jsonRequest.at ("arguments").as_object ();
		std::vector<int> torrentIds;
		if (!GetTorrentIds (arguments, torrentIds)) return ErrorResponse (GetTag (jsonRequest), "Invalid torrent ids");
		if (arguments.contains ("ids"))
		{
			for (auto id : torrentIds) m_Tunnel->ReannounceTorrent (id);
		}
		else
			m_Tunnel->ReannounceAllTorrents ();
		boost::json::object response; // always empty
		return SuccessResponse (GetTag (jsonRequest), std::move (response));
	}

	std::string JSONRPCHandler::HandleTorrentGet (boost::json::object && jsonRequest)
	{
		auto arguments = jsonRequest.at ("arguments").as_object ();
		std::vector<int> torrentIds;
		if (!GetTorrentIds (arguments, torrentIds)) return ErrorResponse (GetTag (jsonRequest), "Invalid torrent ids");
		if (!arguments.contains ("ids")) torrentIds = m_Tunnel->GetTorrentIDs ();
		if (!arguments.contains ("fields") || !arguments.at ("fields").is_array ())
			return ErrorResponse (GetTag (jsonRequest), "Invalid Request");
		const auto & fields = arguments.at ("fields").as_array ();
		if (fields.size () > MAX_RPC_FIELDS_PER_REQUEST) return ErrorResponse (GetTag (jsonRequest), "Too many fields");
		for (const auto & field : fields)
			if (!field.is_string ()) return ErrorResponse (GetTag (jsonRequest), "Invalid Request");
		bool isTable = false;
		if (arguments.contains ("format"))
		{
			if (!arguments.at ("format").is_string ()) return ErrorResponse (GetTag (jsonRequest), "Invalid Request");
			isTable = arguments.at ("format").as_string () == "table";
		}

		boost::json::object response;
		boost::json::array torrents, header;
		for (auto id : torrentIds)
		{
			auto torrent = m_Tunnel->FindTorrentByID (id);
			if (torrent)
			{
				if (isTable)
				{
					bool isFirst = header.empty ();
					auto outcome = RunOnTorrentService (m_Tunnel,
						[handler = *this, torrent, id, fields] ()
						{
							boost::json::array rowHeader;
							auto row = handler.GetTorrentTableRow (torrent, id, fields, rowHeader);
							return std::pair<boost::json::array, boost::json::array> (
								std::move (row), std::move (rowHeader));
						});
					if (!outcome.success)
						return ErrorResponse (GetTag (jsonRequest),
							outcome.timedOut ? RPC_SERVICE_TIMEOUT_ERROR : RPC_SERVICE_FAILURE_ERROR);
					if (isFirst)
					{
						header = outcome.value.second;
						torrents.push_back (std::move (outcome.value.second)); // add header once
					}
					torrents.push_back (std::move (outcome.value.first));
				}
				else
				{
					auto outcome = RunOnTorrentService (m_Tunnel, [handler = *this, torrent, id, fields] ()
						{ return handler.GetTorrentObject (torrent, id, fields); });
					if (!outcome.success)
						return ErrorResponse (GetTag (jsonRequest),
							outcome.timedOut ? RPC_SERVICE_TIMEOUT_ERROR : RPC_SERVICE_FAILURE_ERROR);
					torrents.push_back (std::move (outcome.value));
				}
			}
		}
		if (isTable && header.empty ()) torrents.push_back (fields);
		response["torrents"] = torrents;
		return SuccessResponse (GetTag (jsonRequest), std::move (response));
	}

	boost::json::object JSONRPCHandler::GetTorrentObject (
		std::shared_ptr<Torrent> torrent, int id, const boost::json::array & fields) const
	{
		boost::json::object t;
		t["id"] = id;
		for (const auto & field : fields)
		{
			auto fieldValue = GetFieldValue (field.as_string (), torrent);
			if (!fieldValue.is_null ()) t[field.as_string ()] = fieldValue;
		}
		return t;
	}

	boost::json::array JSONRPCHandler::GetTorrentTableRow (
		std::shared_ptr<Torrent> torrent, int id, const boost::json::array & fields, boost::json::array & header) const
	{
		bool fillHeader = header.empty ();
		boost::json::array row;
		if (fillHeader) header.push_back ("id");
		row.push_back (id);
		for (const auto & field : fields)
		{
			auto fieldValue = GetFieldValue (field.as_string (), torrent);
			if (!fieldValue.is_null ())
			{
				if (fillHeader) header.push_back (field);
				row.push_back (fieldValue);
			}
		}
		return row;
	}

	boost::json::value JSONRPCHandler::GetFieldValue (std::string_view field, std::shared_ptr<Torrent> torrent) const
	{
		const static std::map<std::string_view, std::function<boost::json::value (std::shared_ptr<Torrent>)>> fields = {
			{ "name", [] (std::shared_ptr<Torrent> torrent) { return boost::json::value (torrent->GetName ()); } },
			{ "status", [] (std::shared_ptr<Torrent> torrent) { return boost::json::value (torrent->GetStatus ()); } },
			{ "isFinished",
				[] (std::shared_ptr<Torrent> torrent) { return boost::json::value (torrent->IsComplete ()); } },
			{ "sizeWhenDone", [] (std::shared_ptr<Torrent> torrent)
				{ return boost::json::value (torrent->GetSelectedWantedBytes ()); } },
			{ "leftUntilDone",
				[] (std::shared_ptr<Torrent> torrent) { return boost::json::value (torrent->GetLeft ()); } },
			{ "rateDownload",
				[] (std::shared_ptr<Torrent> torrent) { return boost::json::value (torrent->GetDownloadRate ()); } },
			{ "rateUpload",
				[] (std::shared_ptr<Torrent> torrent) { return boost::json::value (torrent->GetUploadRate ()); } },
			{ "peersGettingFromUs", [] (std::shared_ptr<Torrent> torrent)
				{ return boost::json::value (torrent->GetNumUploadingToPeers ()); } },
			{ "peersSendingToUs", [] (std::shared_ptr<Torrent> torrent)
				{ return boost::json::value (torrent->GetNumDownloadingFromPeers ()); } },
			{ "pieceCount",
				[] (std::shared_ptr<Torrent> torrent) { return boost::json::value (torrent->GetNumPieces ()); } },
			{ "pieceSize",
				[] (std::shared_ptr<Torrent> torrent) { return boost::json::value (torrent->GetPieceLength ()); } },
			{ "totalSize",
				[] (std::shared_ptr<Torrent> torrent) { return boost::json::value (torrent->GetLength ()); } },
			{ "percentDone",
				[] (std::shared_ptr<Torrent> torrent)
				{
					const auto wanted = torrent->GetSelectedWantedBytes ();
					if (!wanted) return boost::json::value (0.0);
					return boost::json::value ((float)torrent->GetSelectedVerifiedBytes () / (float)wanted);
				} },
			{ "hashString",
				[] (std::shared_ptr<Torrent> torrent)
				{
					std::string hexHash;
					boost::algorithm::hex (
						torrent->GetInfoHash ().begin (), torrent->GetInfoHash ().end (), std::back_inserter (hexHash));
					return boost::json::value (hexHash);
				} },
			{ "magnetLink",
				[] (std::shared_ptr<Torrent> torrent)
				{
					std::string hexHash;
					boost::algorithm::hex (
						torrent->GetInfoHash ().begin (), torrent->GetInfoHash ().end (), std::back_inserter (hexHash));
					std::string magnet ("magnet:?xt=urn:btih:");
					magnet += hexHash;
					if (!torrent->GetName ().empty ())
					{
						magnet += "&dn=";
						boost::urls::encoding_opts opts;
						opts.space_as_plus = true;
						magnet += boost::urls::encode (torrent->GetName (), boost::urls::grammar::alnum_chars, opts);
					}
					if (!torrent->GetAnnounce ().empty ())
					{
						magnet += "&tr=";
						magnet += boost::urls::url (torrent->GetAnnounce ()).buffer ();
					}
					return boost::json::value (magnet);
				} },
			{ "pieces",
				[] (std::shared_ptr<Torrent> torrent)
				{
					auto [bitfield, have] = torrent->CreateBitfield ();
					std::string b64pieces;
					b64pieces.resize (boost::beast::detail::base64::encoded_size (bitfield.size ()));
					boost::beast::detail::base64::encode (b64pieces.data (), bitfield.data (), bitfield.size ());
					return boost::json::value (b64pieces);
				} },
			{ "wanted",
				[] (std::shared_ptr<Torrent> torrent)
				{
					boost::json::array wanted;
					size_t numFiles = torrent->GetFiles ().size ();
					if (!numFiles) numFiles = 1;
					wanted.resize (numFiles);
					for (size_t i = 0; i < torrent->GetFiles ().size (); ++i)
						wanted[i] = torrent->GetFilePriority (i) > 0 ? 1 : 0;
					return wanted;
				} },
			{ "priorities",
				[] (std::shared_ptr<Torrent> torrent)
				{
					boost::json::array priorities;
					size_t numFiles = torrent->GetFiles ().size ();
					if (!numFiles) numFiles = 1;
					priorities.resize (numFiles);
					for (size_t i = 0; i < torrent->GetFiles ().size (); ++i)
						priorities[i] = torrent->GetFilePriority (i);
					return priorities;
				} },
			{ "error", [] (std::shared_ptr<Torrent> torrent)
				{ return boost::json::value (torrent->GetError () ? 3 : 0); } }, // local error or no error
			{ "errorString",
				[] (std::shared_ptr<Torrent> torrent) { return boost::json::value (torrent->GetErrorStr ()); } },
			{ "eta",
				[] (std::shared_ptr<Torrent> torrent)
				{
					auto downloadRate = torrent->GetDownloadRate ();
					return boost::json::value (downloadRate ? (int64_t)(torrent->GetLeft () / downloadRate) : -2);
				} },
			{ "uploadRatio",
				[] (std::shared_ptr<Torrent> torrent)
				{
					float ratio = torrent->GetDownloaded ()
						? ((float)torrent->GetUploaded ()) / ((float)torrent->GetDownloaded ())
						: 1.0;
					return boost::json::value (ratio);
				} },
			{ "metadataPercentComplete", [] (std::shared_ptr<Torrent> torrent)
				{ return boost::json::value (torrent->GetLength () ? 1.0 : 0.0); } },
			{ "haveValid", [] (std::shared_ptr<Torrent> torrent)
				{ return boost::json::value (torrent->GetSelectedVerifiedBytes ()); } },
			{ "uploadedEver",
				[] (std::shared_ptr<Torrent> torrent) { return boost::json::value (torrent->GetUploaded ()); } },
			{ "downloadedEver",
				[] (std::shared_ptr<Torrent> torrent) { return boost::json::value (torrent->GetDownloaded ()); } },
			{ "isStalled", [] (std::shared_ptr<Torrent> torrent) { return boost::json::value (false); } }, // TODO:
			{ "labels", [] (std::shared_ptr<Torrent> torrent) { return boost::json::array (); } },
			{ "queuePosition", [] (std::shared_ptr<Torrent> torrent) { return boost::json::value (1); } },
			{ "recheckProgress", [] (std::shared_ptr<Torrent> torrent) { return boost::json::value (0); } },

			// follow the global settings
			// TR_RATIOLIMIT_GLOBAL = 0,
			// override the global settings, seeding until a certain ratio
			// TR_RATIOLIMIT_SINGLE = 1,
			// override the global settings, seeding regardless of ratio
			// TR_RATIOLIMIT_UNLIMITED = 2
			{ "seedRatioMode", [] (std::shared_ptr<Torrent> torrent) { return boost::json::value (0); } },
			{ "seedRatioLimit", [] (std::shared_ptr<Torrent> torrent) { return boost::json::value (0); } },

			{ "downloadDir",
				[] (std::shared_ptr<Torrent> torrent) { return boost::json::value (""); } }, // TODO: torrentsdir
			{ "webseedsSendingToUs",
				[] (std::shared_ptr<Torrent> torrent) { return boost::json::value (0); } }, // not webseeds in i2p yet
			{ "peers", &JSONRPCHandler::GetPeers },
			{ "peersConnected", [] (std::shared_ptr<Torrent> torrent)
				{ return boost::json::value (torrent->GetConnections ().size ()); } },
			{ "fileStats", &JSONRPCHandler::GetFileStats },
			{ "comment", [] (std::shared_ptr<Torrent> torrent)
				{ return boost::json::value (""); } }, // TODO: extract from "comment" dictionary key
			{ "creator", [] (std::shared_ptr<Torrent> torrent)
				{ return boost::json::value (""); } } // TODO: extract from "created by" dictionary key
		};
		if (torrent)
		{
			if (field == "trackers") return GetTrackers (torrent);
			if (field == "trackerStats") return GetTrackerStats (torrent);
			if (field == "files") return GetFiles (torrent);
			auto it = fields.find (field);
			if (it != fields.end ()) return it->second (torrent);
		}
		return boost::json::value ();
	}

	boost::json::array JSONRPCHandler::GetPeers (std::shared_ptr<Torrent> torrent)
	{
		boost::json::array peers;
		auto conns = torrent->GetConnections ();
		for (const auto & it : conns)
		{
			if (it->IsEstablished ())
			{
				boost::json::object peer;
				std::string flags;
				auto stream = it->GetStream ();
				bool isIncoming = stream ? stream->IsIncoming () : false;
				auto identHashStr = stream ? stream->GetRemoteIdentity ()->GetIdentHash ().ToBase64 () : "";
				peer["address"] = identHashStr.substr (0, 4);
				peer["port"] = TORRENT_PORT;
				peer["identHash"] = identHashStr;
				peer["clientName"] = it->GetRemoteName ().empty () ? RecognizeClientByPeerID (it->GetRemotePeerID ())
																   : it->GetRemoteName ();
				peer["isDownloadingFrom"] = it->IsDownloading ();
				peer["isUploadingTo"] = it->IsUploading ();
				peer["rateToClient"] = it->GetDownloadRate ();
				peer["rateToPeer"] = it->GetUploadRate ();
				peer["isIncoming"] = isIncoming;
				peer["clientIsChoked"] = it->IsChoked ();
				peer["peerIsChoked"] = it->IsRemoteChoked ();
				peer["clientIsInterested"] = it->IsInterested ();
				peer["peerIsInterested"] = it->IsRemoteInterested ();
				if (it->IsDownloading ())
					flags.push_back ('D');
				else if (it->IsInterested ())
					flags.push_back ('d');
				else if (!it->IsChoked ())
					flags.push_back ('K');
				if (it->IsUploading ())
					flags.push_back ('U');
				else if (it->IsRemoteInterested ())
					flags.push_back ('u');
				else if (!it->IsRemoteChoked ())
					flags.push_back ('?');
				switch (it->GetOrigin ())
				{
				case ePeerConnectionOriginIncoming: flags.push_back ('I'); break;
				case ePeerConnectionOriginPEX: flags.push_back ('X'); break;
				case ePeerConnectionOriginDHT: flags.push_back ('H'); break;
				default:;
				};
				peer["flagStr"] = flags;
				const auto & remoteBitfield = it->GetRemoteBitfield ();
				peer["progress"]
					= remoteBitfield.size () ? ((float)remoteBitfield.count ()) / ((float)remoteBitfield.size ()) : 0.0;
				peers.push_back (peer);
			}
		}
		return peers;
	}

	boost::json::array JSONRPCHandler::GetTrackers (std::shared_ptr<Torrent> torrent) const
	{
		boost::json::array trackers;
		for (size_t i = 0; i < m_Tunnel->GetNumTrackers (); i++)
		{
			// A released registry slot keeps its index but reports an empty
			// announce URL; never expose it as a tracker of this torrent.
			const auto announce = m_Tunnel->GetTrackerAnnounce (i);
			if (torrent->GetNextTrackerRequestTime (i) && !announce.empty ())
			{
				boost::json::object tracker;
				tracker["id"] = i;
				tracker["announce"] = announce;
				tracker["tier"] = 0;
				trackers.push_back (tracker);
			}
		}
		return trackers;
	}

	boost::json::array JSONRPCHandler::GetTrackerStats (std::shared_ptr<Torrent> torrent) const
	{
		boost::json::array trackers;
		auto ts = i2p::util::GetMonotonicMilliseconds ();
		auto tsSinceEpoch = i2p::util::GetMillisecondsSinceEpoch ();
		for (size_t i = 0; i < m_Tunnel->GetNumTrackers (); i++)
		{
			const auto announce = m_Tunnel->GetTrackerAnnounce (i);
			if (torrent->GetNextTrackerRequestTime (i) && !announce.empty ())
			{
				boost::json::object tracker;
				tracker["id"] = i;
				tracker["announce"] = announce;
				i2p::http::URL announceURL;
				announceURL.parse (announce);
				tracker["host"] = announceURL.host;
				tracker["announceState"] = 1;
				tracker["scrapeState"] = 0;
				tracker["hasAnnounced"] = true; // TODO:
				tracker["hasScraped"] = false;
				tracker["isBackup"] = false;
				tracker["downloadCount"] = -1;
				tracker["tier"] = 0;
				tracker["seederCount"] = torrent->GetNumSeeders (i);
				tracker["leecherCount"] = torrent->GetNumLeechers (i);
				tracker["lastAnnouncePeerCount"] = torrent->GetNumPeers (i);
				auto trackerError = torrent->GetTrackerError (i);
				if (trackerError.empty ())
				{
					tracker["lastAnnounceResult"] = "Success";
					tracker["lastAnnounceSucceeded"] = true;
				}
				else
				{
					tracker["lastAnnounceResult"] = trackerError;
					tracker["lastAnnounceSucceeded"] = false;
				}
				tracker["lastAnnounceTimedOut"] = false; // TODO:
				auto nextRequestTime = torrent->GetNextTrackerRequestTime (i);
				tracker["nextAnnounceTime"]
					= ((nextRequestTime && nextRequestTime > ts ? nextRequestTime - ts : 0) + tsSinceEpoch) / 1000;
				tracker["lastAnnounceTime"] = torrent->GetLastTrackerUpdateTime (i);
				tracker["lastAnnounceStartTime"] = torrent->GetLastTrackerUpdateTime (i); // TODO:
				tracker["lastScrapeTime"] = 0;
				tracker["lastScrapeStartTime"] = 0;
				tracker["nextScrapeTime"] = 0;
				tracker["lastScrapeResult"] = "";
				tracker["lastScrapeTimedOut"] = false;
				tracker["lastScrapeSucceeded"] = true;
				trackers.push_back (tracker);
			}
		}
		return trackers;
	}

	boost::json::array JSONRPCHandler::GetFiles (std::shared_ptr<Torrent> torrent) const
	{
		boost::json::array files;
		const auto & torrentFiles = torrent->GetFiles ();
		const auto & torrentsDir = m_Tunnel->GetTorrentsDir ();
		auto filesCompleted = torrent->GetFilesCompleted ();
		size_t ind = 0;
		for (const auto & it : torrentFiles)
		{
			boost::json::object file;
			file["name"] = std::filesystem::relative (it->GetFullFilePath (), torrentsDir).string ();
			file["length"] = it->GetFileLength ();
			file["bytesCompleted"] = (ind < filesCompleted.size ()) ? filesCompleted[ind] : 0;
			files.push_back (file);
			ind++;
		}
		return files;
	}

	boost::json::array JSONRPCHandler::GetFileStats (std::shared_ptr<Torrent> torrent)
	{
		boost::json::array files;
		auto filesCompleted = torrent->GetFilesCompleted ();
		for (auto it : filesCompleted)
		{
			boost::json::object file;
			file["bytesCompleted"] = it;
			file["priority"] = 0;
			file["wanted"] = true;
			files.push_back (file);
		}
		return files;
	}

	std::string JSONRPCHandler::RecognizeClientByPeerID (const PeerConnection::PeerID & peerID)
	{
		static constexpr std::array<uint8_t, 12> i2psnark { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03,
			0x03, 0x03 };
		static const std::map<std::string_view, std::string_view> twoChars
			= { { "qB", "qBittorrent" }, { "XD", "XD" }, { "BI", "BiglyBT" }, { "AZ", "Vuze" }, { "LT", "libtorrent" },
				  { "IO", "insulaocculta" }, { "KT", "KTorrent" }, { "ET", "EepTorrent" }, { "TR", "Transmission" },
				  { "DE", "Deluge" }, { "CV", "Clove" } };

		if (peerID.size () >= i2psnark.size () && !memcmp (peerID.data (), i2psnark.data (), i2psnark.size ()))
			return "I2PSnark";

		std::string_view peerIDStr ((const char *)peerID.data (), peerID.size ());
		if (peerIDStr.substr (0, 6) == "-I2PD-") return "i2pd";
		if (peerIDStr[0] == '-')
		{
			auto it = twoChars.find (peerIDStr.substr (1, 2));
			if (it != twoChars.end ()) return std::string (it->second);
		}
		else if (peerIDStr.substr (0, 3) == "TIX")
			return "Tixati";

		std::string unknown ("Unknown (");
		for (auto ch : peerIDStr) unknown.push_back (std::isprint ((uint8_t)ch) ? ch : '?');
		unknown.push_back (')');
		return unknown;
	}

	std::string JSONRPCHandler::HandleTorrentStop (boost::json::object && jsonRequest)
	{
		auto arguments = jsonRequest.at ("arguments").as_object ();
		std::vector<int> torrentIds;
		if (!GetTorrentIds (arguments, torrentIds)) return ErrorResponse (GetTag (jsonRequest), "Invalid torrent ids");
		if (arguments.contains ("ids"))
		{
			for (auto id : torrentIds) m_Tunnel->StopTorrent (id);
		}
		else
			m_Tunnel->StopAllTorrents ();
		boost::json::object response; // always empty
		return SuccessResponse (GetTag (jsonRequest), std::move (response));
	}

	std::string JSONRPCHandler::HandleTorrentStart (boost::json::object && jsonRequest)
	{
		auto arguments = jsonRequest.at ("arguments").as_object ();
		std::vector<int> torrentIds;
		if (!GetTorrentIds (arguments, torrentIds)) return ErrorResponse (GetTag (jsonRequest), "Invalid torrent ids");
		if (arguments.contains ("ids"))
		{
			for (auto id : torrentIds) m_Tunnel->StartTorrent (id);
		}
		else
			m_Tunnel->StartAllTorrents ();
		boost::json::object response; // always empty
		return SuccessResponse (GetTag (jsonRequest), std::move (response));
	}

	std::string JSONRPCHandler::HandleSessionStats (boost::json::object && jsonRequest)
	{
		// Counters and peer maps belong to the tunnel service thread. Aggregate
		// them there instead of walking live connections from the RPC thread.
		auto outcome = RunOnTorrentService (m_Tunnel,
			[tunnel = m_Tunnel] ()
			{
				SessionStatistics statistics;
				auto torrents = tunnel->GetTorrents ();
				statistics.torrentCount = torrents.size ();
				for (auto it : torrents)
				{
					statistics.downloaded += it->GetDownloaded ();
					statistics.uploaded += it->GetUploaded ();
					statistics.downloadRate += it->GetDownloadRate ();
					statistics.uploadRate += it->GetUploadRate ();
					if (it->IsStopped ()) statistics.stopped++;
				}
				return statistics;
			});
		if (!outcome.success)
			return ErrorResponse (
				GetTag (jsonRequest), outcome.timedOut ? RPC_SERVICE_TIMEOUT_ERROR : RPC_SERVICE_FAILURE_ERROR);

		boost::json::object response, stats;
		stats["downloadedBytes"] = outcome.value.downloaded;
		stats["uploadedBytes"] = outcome.value.uploaded;
		stats["downloadSpeed"] = outcome.value.downloadRate;
		stats["uploadSpeed"] = outcome.value.uploadRate;
		stats["sessionCount"] = 1;
		stats["filesAdded"] = 0; // TODO:
		stats["secondsActive"] = 0; // TODO:
		response["torrentCount"] = outcome.value.torrentCount;
		response["pausedTorrentCount"] = outcome.value.stopped;
		response["activeTorrentCount"] = outcome.value.torrentCount - outcome.value.stopped;
		response["current-stats"] = stats;
		response["cumulative-stats"] = stats;
		return SuccessResponse (GetTag (jsonRequest), std::move (response));
	}

	std::string JSONRPCHandler::HandleDHTStats (boost::json::object && jsonRequest)
	{
		boost::json::object response;
		bool isDHTSupported = m_Tunnel->SupportsDHT ();
		response["supported"] = isDHTSupported;
		if (isDHTSupported)
		{
			auto outcome = RunOnTorrentService (m_Tunnel,
				[tunnel = m_Tunnel] ()
				{
					DHTStatistics stats;
					const auto & dht = tunnel->GetDHT ();
					if (dht)
					{
						stats.numNodes = dht->GetNumNodes ();
						stats.numBuckets = dht->GetNumBuckets ();
						stats.numTorrents = dht->GetNumTorrents ();
						stats.numHeardFrom = dht->GetNumHeardFrom ();
						stats.numFailedPeers = dht->GetNumFailedPeers ();
					}
					return stats;
				});
			if (!outcome.success)
				return ErrorResponse (
					GetTag (jsonRequest), outcome.timedOut ? RPC_SERVICE_TIMEOUT_ERROR : RPC_SERVICE_FAILURE_ERROR);
			response["numNodes"] = outcome.value.numNodes;
			response["numBuckets"] = outcome.value.numBuckets;
			response["numTorrents"] = outcome.value.numTorrents;
			response["numHeardFrom"] = outcome.value.numHeardFrom;
			response["numFailedPeers"] = outcome.value.numFailedPeers;
		}
		return SuccessResponse (GetTag (jsonRequest), std::move (response));
	}

	std::string JSONRPCHandler::HandleTorrentSet (boost::json::object && jsonRequest)
	{
		auto arguments = jsonRequest.at ("arguments").as_object ();
		if (arguments.contains ("trackerAdd"))
		{
			if (!arguments.at ("trackerAdd").is_array ())
				return ErrorResponse (GetTag (jsonRequest), "Invalid Request");
			const auto & trackers = arguments.at ("trackerAdd").as_array ();
			if (trackers.size () > MAX_RPC_TRACKERS_PER_REQUEST)
				return ErrorResponse (GetTag (jsonRequest), "Too many trackers");
			for (const auto & it : trackers)
			{
				if (!it.is_string ()) return ErrorResponse (GetTag (jsonRequest), "Invalid Request");
				const auto & announceString = it.as_string ();
				std::string_view announce (announceString.data (), announceString.size ());
				if (announce.empty () || announce.size () > MAX_RPC_TRACKER_LENGTH
					|| announce.find ('\n') != std::string_view::npos)
					return ErrorResponse (GetTag (jsonRequest), "Invalid tracker");
				// A timed-out task is not cancellable: it can still add the
				// tracker, but it owns its announce string and tunnel reference.
				auto outcome = RunOnTorrentService (m_Tunnel, [tunnel = m_Tunnel, announce = std::string (announce)] ()
					{ return tunnel->AddTracker (announce, true); });
				if (!outcome.success)
					return ErrorResponse (
						GetTag (jsonRequest), outcome.timedOut ? RPC_SERVICE_TIMEOUT_ERROR : RPC_SERVICE_FAILURE_ERROR);
				if (outcome.value < 0)
					return ErrorResponse (GetTag (jsonRequest), "Tracker is not a valid I2P announce URL");
			}
		}
		boost::json::object response; // always empty
		return SuccessResponse (GetTag (jsonRequest), std::move (response));
	}

	TorrentsRPCSession::TorrentsRPCSession (TorrentsRPCServer & server, boost::asio::ip::tcp::socket && s)
		: m_Server (server)
		, m_Socket (std::move (s))
	{
		m_RequestParser.body_limit (MAX_RPC_BODY_SIZE);
	}

	void TorrentsRPCSession::ReceiveRequest ()
	{
		boost::beast::http::async_read (m_Socket, m_ReceiveBuffer, m_RequestParser,
			boost::beast::bind_front_handler (&TorrentsRPCSession::HandleRequest, shared_from_this ()));
	}

	void TorrentsRPCSession::HandleRequest (const boost::system::error_code & ecode, size_t bytes_transferred)
	{
		if (ecode)
		{
			if (ecode == boost::beast::http::error::body_limit)
				SendResponse (boost::beast::http::status::payload_too_large);
			else if (ecode != boost::beast::http::error::end_of_stream
				&& ecode != boost::asio::error::operation_aborted)
				LogPrint (eLogDebug, "TorrentsRPC: Failed to read request: ", ecode.message ());
			return;
		}

		const auto & request = m_RequestParser.get ();
		if (request.method () != boost::beast::http::verb::post)
		{
			// GET, HEAD, OPTIONS and every other method are not resources here.
			SendResponse (boost::beast::http::status::method_not_allowed);
			return;
		}

		auto path = request.target ();
		std::string_view pathView (path.data (), path.size ());
		auto tunnel = m_Server.GetTunnel (pathView);
		if (!tunnel)
		{
			LogPrint (eLogDebug, "TorrentRPC ", pathView, " not found");
			SendResponse (boost::beast::http::status::not_found);
			return;
		}

		auto [authRequired, authString] = m_Server.GetAuth (pathView);
		if (authRequired)
		{
			auto auth = request[boost::beast::http::field::authorization];
			std::string_view authView (auth.data (), auth.size ());
			if (authString.empty () || !SecureEquals (authView, authString))
			{
				SendUnauthorizedResponse ();
				return;
			}
		}

		auto contentTypeHeader = request[boost::beast::http::field::content_type];
		std::string_view contentType (contentTypeHeader.data (), contentTypeHeader.size ());
		static constexpr std::string_view appjson { "application/json" };
		if (!contentType.starts_with (appjson))
		{
			SendResponse (boost::beast::http::status::unsupported_media_type);
			return;
		}
		auto parameters = contentType.substr (appjson.size ());
		size_t pos = 0;
		while (pos < parameters.size () && (parameters[pos] == ' ' || parameters[pos] == '\t')) pos++;
		if (pos < parameters.size () && parameters[pos] != ';')
		{
			SendResponse (boost::beast::http::status::unsupported_media_type);
			return;
		}
		if (request.body ().empty ())
		{
			SendResponse (boost::beast::http::status::bad_request);
			return;
		}

		JSONRPCHandler jsonHandler (tunnel);
		auto response = jsonHandler.HandleRequest (request.body ());
		if (!response.empty ()) SendResponse (boost::beast::http::status::ok, response);
	}

	void TorrentsRPCSession::SendResponse (boost::beast::http::status result, std::string_view data)
	{
		m_Response.version (11); // HTTP/1.1
		m_Response.result (result);
		m_Response.keep_alive (false);
		m_Response.set (boost::beast::http::field::server, "i2pd torrents RPC");
		m_Response.set (boost::beast::http::field::cache_control, "no-store");
		if (result == boost::beast::http::status::method_not_allowed)
			m_Response.set (boost::beast::http::field::allow, "POST");
		m_Response.set (boost::beast::http::field::content_type,
			(result == boost::beast::http::status::ok) ? "application/json; charset=UTF-8"
													   : "text/plain; charset=UTF-8");
		m_Response.body () = data;
		m_Response.prepare_payload ();
		boost::beast::http::async_write (m_Socket, m_Response,
			[s = shared_from_this ()] (const boost::system::error_code & ecode, size_t bytes_transferred)
			{
				if (ecode) LogPrint (eLogWarning, "TorrentsRPC: Failed to send response: ", ecode.message ());
			});
	}

	void TorrentsRPCSession::SendUnauthorizedResponse ()
	{
		m_Response.version (11); // HTTP/1.1
		m_Response.result (boost::beast::http::status::unauthorized);
		m_Response.keep_alive (false);
		m_Response.set (boost::beast::http::field::server, "i2pd torrents RPC");
		m_Response.set (boost::beast::http::field::cache_control, "no-store");
		m_Response.set (boost::beast::http::field::www_authenticate, "Basic realm=\"i2pd torrents RPC\"");
		m_Response.set (boost::beast::http::field::content_type, "text/plain; charset=UTF-8");
		m_Response.body () = "Unauthorized";
		m_Response.prepare_payload ();
		boost::beast::http::async_write (m_Socket, m_Response,
			[s = shared_from_this ()] (const boost::system::error_code & ecode, size_t bytes_transferred)
			{
				if (ecode) LogPrint (eLogWarning, "TorrentsRPC: Failed to send response: ", ecode.message ());
			});
	}

	TorrentsRPCServer::TorrentsRPCServer (std::string_view address, uint16_t port)
		: RunnableServiceWithWork ("TRPC")
		, m_Acceptor (GetService (), MakeLoopbackEndpoint (address, port))
	{
	}

	void TorrentsRPCServer::Start ()
	{
		if (!IsRunning ())
		{
			StartIOService ();
			Accept ();
		}
	}

	void TorrentsRPCServer::Stop ()
	{
		if (IsRunning ())
		{
			m_Acceptor.cancel ();
			StopIOService ();
		}
	}

	void TorrentsRPCServer::AddTunnel (std::string_view path, std::shared_ptr<TorrentsTunnel> tunnel, bool auth,
		const std::string & user, const std::string & pass)
	{
		if (auth && (!IsValidCredential (user) || !IsValidCredential (pass)))
		{
			LogPrint (eLogError, "TorrentsRPC: RPC authentication requires non-empty, bounded rpcuser and rpcpass");
			return;
		}
		std::string rpcPath ("/");
		if (!path.empty ()) rpcPath += std::string (path) + "/";
		// insert both pathes rpc and rpc/
		rpcPath += "rpc";
		std::lock_guard<std::mutex> lock (m_TunnelsMutex);
		m_Tunnels.emplace (rpcPath, tunnel);
		if (auth) m_Auth.emplace (rpcPath, i2p::http::CreateBasicAuthorizationString (user, pass));

		rpcPath += "/";
		m_Tunnels.emplace (rpcPath, tunnel);
		if (auth) m_Auth.emplace (rpcPath, i2p::http::CreateBasicAuthorizationString (user, pass));
	}

	std::pair<boost::asio::ip::tcp::endpoint, std::string> TorrentsRPCServer::GetTunnelEndpoint (
		std::shared_ptr<TorrentsTunnel> tunnel) const
	{
		std::lock_guard<std::mutex> lock (m_TunnelsMutex);
		for (const auto & [path, weakTunnel] : m_Tunnels)
		{
			if (auto t = weakTunnel.lock (); t == tunnel)
			{
				return { m_Acceptor.local_endpoint (), path };
			}
		}
		return { };
	}

	std::shared_ptr<TorrentsTunnel> TorrentsRPCServer::GetTunnel (std::string_view path) const
	{
		std::lock_guard<std::mutex> lock (m_TunnelsMutex);
		auto it = m_Tunnels.find (path);
		if (it != m_Tunnels.end ()) return it->second.lock ();
		return nullptr;
	}

	std::pair<bool, std::string> TorrentsRPCServer::GetAuth (std::string_view path) const
	{
		std::lock_guard<std::mutex> lock (m_TunnelsMutex);
		auto it = m_Auth.find (path);
		if (it == m_Auth.end ()) return { false, "" };
		return { true, it->second };
	}

	void TorrentsRPCServer::Accept ()
	{
		m_Acceptor.async_accept (boost::asio::make_strand (GetService ()),
			[this] (const boost::system::error_code & ecode, boost::asio::ip::tcp::socket socket)
			{
				if (!ecode)
				{
					LogPrint (eLogDebug, "TorrentsRPC: Accepted incoming request");
					auto session = std::make_shared<TorrentsRPCSession> (*this, std::move (socket));
					session->ReceiveRequest ();
					Accept ();
				}
			});
	}
}
}

#endif

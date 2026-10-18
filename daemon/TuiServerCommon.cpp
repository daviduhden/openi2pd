/*
* Copyright (c) 2013-2026, The PurpleI2P Project
* Copyright (c) 2026, David Uhden Collado
* SPDX-License-Identifier: BSD-3-Clause
*
* This file is part of openi2pd and licensed under BSD-3-Clause
*
* See full license text in LICENSE file at top of project tree
*/

#include "TuiServerCommon.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ClientContext.h"
#include "Expected.h"
#include "Log.h"
#include "NetDb.hpp"
#include "RouterContext.h"
#include "RouterInfo.h"
#include "Timestamp.h"
#include "TorrentsControl.h"
#include "Transports.h"
#include "Tunnel.h"
#include "TuiProtocol.h"
#include "version.h"

namespace i2p
{
namespace tui
{
	namespace
	{
		std::string_view StatusName (i2p::RouterStatus status)
		{
			switch (status)
			{
			case i2p::eRouterStatusOK: return "OK";
			case i2p::eRouterStatusFirewalled: return "Firewalled";
			case i2p::eRouterStatusUnknown: return "Unknown";
			case i2p::eRouterStatusProxy: return "Proxy";
			case i2p::eRouterStatusMesh: return "Mesh";
			case i2p::eRouterStatusStan: return "Stan";
			default: return "Unknown";
			}
		}

		std::string_view ErrorName (i2p::RouterError error)
		{
			switch (error)
			{
			case i2p::eRouterErrorNone: return "none";
			case i2p::eRouterErrorClockSkew: return "clock-skew";
			case i2p::eRouterErrorOffline: return "offline";
			case i2p::eRouterErrorSymmetricNAT: return "symmetric-nat";
			case i2p::eRouterErrorFullConeNAT: return "full-cone-nat";
			case i2p::eRouterErrorNoDescriptors: return "no-descriptors";
			default: return "unknown";
			}
		}

		std::string_view TunnelStateName (i2p::tunnel::TunnelState state)
		{
			switch (state)
			{
			case i2p::tunnel::eTunnelStatePending: return "pending";
			case i2p::tunnel::eTunnelStateBuildReplyReceived: return "build-reply";
			case i2p::tunnel::eTunnelStateBuildFailed: return "build-failed";
			case i2p::tunnel::eTunnelStateEstablished: return "established";
			case i2p::tunnel::eTunnelStateTestFailed: return "test-failed";
			case i2p::tunnel::eTunnelStateFailed: return "failed";
			case i2p::tunnel::eTunnelStateExpiring: return "expiring";
			default: return "unknown";
			}
		}

		std::string BuildState ()
		{
			std::string payload;
			payload.reserve (2048);
			AppendField (payload, "protocol", PROTOCOL_VERSION);
			AppendField (payload, "router.version", VERSION);
			AppendField (payload, "router.hash", i2p::context.GetRouterInfo ().GetIdentHashBase64 ());
			AppendField (payload, "router.caps", i2p::context.GetRouterInfo ().GetProperty ("caps"));
			AppendField (payload, "router.netid", i2p::context.GetNetID ());
			AppendField (payload, "router.floodfill", i2p::context.IsFloodfill ());
			AppendField (payload, "router.accepts_tunnels", i2p::context.AcceptsTunnels ());
			AppendField (payload, "router.uptime", i2p::context.GetUptime ());
			AppendField (payload, "router.bandwidth_limit", i2p::context.GetBandwidthLimit ());

			AppendField (payload, "network.v4", i2p::context.SupportsV4 ());
			AppendField (payload, "network.v6", i2p::context.SupportsV6 ());
			AppendField (payload, "network.status4", StatusName (i2p::context.GetStatus ()));
			AppendField (payload, "network.status6", StatusName (i2p::context.GetStatusV6 ()));
			AppendField (payload, "network.testing4", i2p::context.GetTesting ());
			AppendField (payload, "network.testing6", i2p::context.GetTestingV6 ());
			AppendField (payload, "network.error4", ErrorName (i2p::context.GetError ()));
			AppendField (payload, "network.error6", ErrorName (i2p::context.GetErrorV6 ()));

			const auto & transports = i2p::transport::transports;
			AppendField (payload, "transport.ntcp2", transports.IsBoundNTCP2 ());
			AppendField (payload, "transport.ssu2", transports.IsBoundSSU2 ());
			AppendField (payload, "transport.online", transports.IsOnline ());
			AppendField (payload, "bandwidth.in", transports.GetInBandwidth ());
			AppendField (payload, "bandwidth.out", transports.GetOutBandwidth ());
			AppendField (payload, "bandwidth.transit", transports.GetTransitBandwidth ());
			AppendField (payload, "bandwidth.in15", transports.GetInBandwidth15s ());
			AppendField (payload, "bandwidth.out15", transports.GetOutBandwidth15s ());
			AppendField (payload, "traffic.received", transports.GetTotalReceivedBytes ());
			AppendField (payload, "traffic.sent", transports.GetTotalSentBytes ());
			AppendField (payload, "traffic.transit", transports.GetTotalTransitTransmittedBytes ());
			AppendField (payload, "peers.count", transports.GetNumPeers ());

			const auto & tunnels = i2p::tunnel::tunnels;
			AppendField (payload, "tunnels.inbound", tunnels.CountInboundTunnels ());
			AppendField (payload, "tunnels.outbound", tunnels.CountOutboundTunnels ());
			AppendField (payload, "tunnels.transit", tunnels.CountTransitTunnels ());
			AppendField (payload, "tunnels.success", tunnels.GetTunnelCreationSuccessRate ());
			AppendField (payload, "tunnels.queue", tunnels.GetQueueSize ());
			AppendField (payload, "tunnels.build_queue", tunnels.GetTBMQueueSize ());

			const auto & netdb = i2p::data::netdb;
			AppendField (payload, "netdb.routers", netdb.GetNumRouters ());
			AppendField (payload, "netdb.floodfills", netdb.GetNumFloodfills ());
			AppendField (payload, "netdb.leasesets", netdb.GetNumLeaseSets ());
			AppendField (payload, "netdb.reseeding", netdb.IsReseeding ());
			return payload;
		}

		std::string BuildPeers (uint32_t limit)
		{
			std::string payload;
			for (const auto & peer : i2p::transport::transports.GetPeersSnapshot (limit))
			{
				std::string value = peer.ident.ToBase64 ();
				value.push_back ('|');
				value.append (std::to_string (peer.numAttempts));
				value.push_back ('|');
				value.append (std::to_string (peer.numSessions));
				value.push_back ('|');
				value.append (std::to_string (peer.transports));
				value.push_back ('|');
				value.push_back (peer.isHighBandwidth ? '1' : '0');
				AppendField (payload, "peer", value);
			}
			return payload;
		}

		std::string BuildTunnels ()
		{
			std::string payload;
			size_t count = 0;
			auto append = [&payload, &count] (std::string_view direction, const auto & tunnel)
			{
				if (count++ >= MAX_TUNNEL_RECORDS_PER_REQUEST) return;
				std::string value = std::to_string (tunnel->GetTunnelID ());
				value.push_back ('|');
				value.append (direction);
				value.push_back ('|');
				value.append (TunnelStateName (tunnel->GetState ()));
				value.push_back ('|');
				value.append (std::to_string (tunnel->GetNumHops ()));
				value.push_back ('|');
				value.append (std::to_string (tunnel->GetCreationTime ()));
				AppendField (payload, "tunnel", value);
			};

			for (const auto & tunnel : i2p::tunnel::tunnels.GetInboundTunnelsList ()) append ("in", tunnel);
			for (const auto & tunnel : i2p::tunnel::tunnels.GetOutboundTunnelsList ()) append ("out", tunnel);
			for (const auto & tunnel : i2p::tunnel::tunnels.GetTransitTunnelsList ())
			{
				if (count++ >= MAX_TUNNEL_RECORDS_PER_REQUEST) break;
				std::string value = std::to_string (tunnel->GetTunnelID ());
				value.append ("|transit|n/a|0|");
				value.append (std::to_string (tunnel->GetCreationTime ()));
				AppendField (payload, "tunnel", value);
			}
			return payload;
		}

		std::string BuildNetDb ()
		{
			std::string payload;
			AppendField (payload, "netdb.routers", i2p::data::netdb.GetNumRouters ());
			AppendField (payload, "netdb.floodfills", i2p::data::netdb.GetNumFloodfills ());
			AppendField (payload, "netdb.leasesets", i2p::data::netdb.GetNumLeaseSets ());
			AppendField (payload, "netdb.reseeding", i2p::data::netdb.IsReseeding ());
			AppendField (payload, "router.floodfill", i2p::context.IsFloodfill ());
			AppendField (payload, "router.hidden", i2p::context.IsHidden ());
			AppendField (payload, "router.family", i2p::context.GetFamily ());
			return payload;
		}

		std::string BuildDestinations ()
		{
			std::string payload;
			size_t count = 0;
			for (const auto & entry : i2p::client::context.GetDestinationsList ())
			{
				if (count++ >= MAX_TUNNEL_RECORDS_PER_REQUEST) break;
				const auto & hash = entry.first;
				std::string value = hash.ToBase32 ();
				value.append (".b32.i2p|");
				value.append (hash.ToBase64 ());
				AppendField (payload, "destination", value);
			}
			return payload;
		}

		std::string BuildLogs (uint64_t after, uint32_t limit)
		{
			std::string payload;
			for (const auto & record : i2p::log::Logger ().GetRecent (after, limit))
			{
				std::string value = std::to_string (record.sequence);
				value.push_back ('|');
				value.append (std::to_string (static_cast<uint64_t> (record.timestamp)));
				value.push_back ('|');
				value.append (std::to_string (static_cast<int> (record.level)));
				value.push_back ('|');
				value.append (record.text);
				AppendField (payload, "log", value);
			}
			return payload;
		}

#ifndef NO_TORRENTS
		using TorrentsTunnelPtr = std::shared_ptr<i2p::torrents::TorrentsTunnel>;

		std::vector<TorrentsTunnelPtr> GetSortedTorrentsTunnels ()
		{
			return i2p::client::context.GetTorrentsTunnelsSnapshot ();
		}

		i2p::expected<i2p::torrents::TorrentsSnapshot, std::string> GetTunnelSnapshot (const TorrentsTunnelPtr & tunnel)
		{
			if (!tunnel) return i2p::unexpected ("torrent tunnel not found");
			auto snapshot = tunnel->GetControlSnapshot (i2p::torrents::CONTROL_DEFAULT_TIMEOUT);
			if (!snapshot) return i2p::unexpected ("torrent snapshot timed out");
			return std::move (*snapshot);
		}

		void AppendTorrentSummary (
			std::string & payload, std::string_view prefix, const i2p::torrents::TorrentInfo & torrent)
		{
			auto field = [prefix] (std::string_view suffix)
			{
				std::string name (prefix);
				name.push_back ('.');
				name.append (suffix);
				return name;
			};
			AppendField (payload, field ("id"), torrent.id);
			AppendField (payload, field ("name"), torrent.name);
			AppendField (payload, field ("status"), torrent.status);
			AppendField (payload, field ("error"), torrent.error);
			AppendField (payload, field ("hash"), torrent.hash);
			AppendField (payload, field ("magnet"), torrent.magnet);
			AppendField (payload, field ("total"), torrent.totalSize);
			AppendField (payload, field ("have"), torrent.have);
			AppendField (payload, field ("downloaded"), torrent.downloaded);
			AppendField (payload, field ("uploaded"), torrent.uploaded);
			AppendField (payload, field ("downRate"), torrent.downRate);
			AppendField (payload, field ("upRate"), torrent.upRate);
			AppendField (payload, field ("peers"), torrent.peers);
			AppendField (payload, field ("seeders"), torrent.seeders);
			AppendField (payload, field ("leechers"), torrent.leechers);
			AppendField (payload, field ("percent"), std::to_string (torrent.percent));
			AppendField (payload, field ("eta"),
				torrent.etaSeconds ? static_cast<int64_t> (*torrent.etaSeconds) : static_cast<int64_t> (-1));
		}

		void AppendTorrentDetail (std::string & payload, const i2p::torrents::TorrentInfo & torrent)
		{
			AppendTorrentSummary (payload, "detail", torrent);
			size_t count = 0;
			for (const auto & file : torrent.files)
			{
				if (count++ >= MAX_TORRENT_FILE_RECORDS_PER_REQUEST) break;
				auto prefix = "file." + std::to_string (count - 1);
				AppendField (payload, prefix + ".name", file.name);
				AppendField (payload, prefix + ".length", file.length);
				AppendField (payload, prefix + ".completed", file.completed);
			}
			count = 0;
			for (const auto & peer : torrent.peerDetails)
			{
				if (count++ >= MAX_TORRENT_PEER_RECORDS_PER_REQUEST) break;
				auto prefix = "peer." + std::to_string (count - 1);
				AppendField (payload, prefix + ".ident", peer.ident);
				AppendField (payload, prefix + ".client", peer.client);
				AppendField (payload, prefix + ".flags", peer.flags);
				AppendField (payload, prefix + ".progress", std::to_string (peer.progress));
				AppendField (payload, prefix + ".downRate", peer.downRate);
				AppendField (payload, prefix + ".upRate", peer.upRate);
			}
			count = 0;
			for (const auto & tracker : torrent.trackers)
			{
				if (count++ >= MAX_TORRENT_TRACKER_RECORDS_PER_REQUEST) break;
				auto prefix = "tracker." + std::to_string (count - 1);
				AppendField (payload, prefix + ".announce", tracker.announce);
				AppendField (payload, prefix + ".peers", tracker.peers);
				AppendField (payload, prefix + ".seeders", tracker.seeders);
				AppendField (payload, prefix + ".leechers", tracker.leechers);
				AppendField (payload, prefix + ".lastResult", tracker.lastResult);
				AppendField (payload, prefix + ".lastAnnounce", tracker.lastAnnounce);
				AppendField (payload, prefix + ".nextAnnounce", tracker.nextAnnounce);
			}
		}

		std::string BuildTorrentTunnelList ()
		{
			std::string payload;
			auto tunnels = GetSortedTorrentsTunnels ();
			size_t count = std::min<size_t> (tunnels.size (), MAX_TORRENT_RECORDS_PER_REQUEST);
			AppendField (payload, "torrent.tunnels", count);
			for (size_t i = 0; i < count; i++)
			{
				auto prefix = "torrent.tunnel." + std::to_string (i);
				AppendField (payload, prefix + ".name", tunnels[i]->GetName ());
				AppendField (payload, prefix + ".torrents", tunnels[i]->GetTorrentIDs ().size ());
				AppendField (payload, prefix + ".index", i);
			}
			return payload;
		}

		i2p::expected<std::string, std::string> BuildTorrentList (uint32_t tunnelIndex, uint32_t limit)
		{
			auto tunnels = GetSortedTorrentsTunnels ();
			if (tunnelIndex >= tunnels.size ()) return i2p::unexpected ("torrent tunnel not found");
			auto snapshot = GetTunnelSnapshot (tunnels[tunnelIndex]);
			if (!snapshot) return i2p::unexpected (snapshot.error ());

			std::string payload;
			AppendField (payload, "torrent.tunnel.index", tunnelIndex);
			AppendField (payload, "torrent.tunnel.name", snapshot->tunnelName);
			size_t count = std::min<size_t> (limit, snapshot->torrents.size ());
			count = std::min<size_t> (count, MAX_TORRENT_RECORDS_PER_REQUEST);
			for (size_t i = 0; i < count; i++)
				AppendTorrentSummary (payload, "torrent." + std::to_string (i), snapshot->torrents[i]);
			AppendField (payload, "torrent.count", count);
			return payload;
		}

		i2p::expected<std::string, std::string> BuildTorrentDetail (uint32_t tunnelIndex, int torrentId)
		{
			auto tunnels = GetSortedTorrentsTunnels ();
			if (tunnelIndex >= tunnels.size ()) return i2p::unexpected ("torrent tunnel not found");
			auto snapshot = GetTunnelSnapshot (tunnels[tunnelIndex]);
			if (!snapshot) return i2p::unexpected (snapshot.error ());

			auto it = std::find_if (snapshot->torrents.begin (), snapshot->torrents.end (),
				[torrentId] (const auto & torrent) { return torrent.id == torrentId; });
			if (it == snapshot->torrents.end ()) return i2p::unexpected ("torrent not found");
			std::string payload;
			AppendTorrentDetail (payload, *it);
			return payload;
		}

		i2p::expected<std::string, std::string> BuildTorrentSession (uint32_t tunnelIndex)
		{
			auto tunnels = GetSortedTorrentsTunnels ();
			if (tunnelIndex >= tunnels.size ()) return i2p::unexpected ("torrent tunnel not found");
			auto snapshot = GetTunnelSnapshot (tunnels[tunnelIndex]);
			if (!snapshot) return i2p::unexpected (snapshot.error ());

			std::string payload;
			AppendField (payload, "session.downloaded", snapshot->session.downloaded);
			AppendField (payload, "session.uploaded", snapshot->session.uploaded);
			AppendField (payload, "session.active", snapshot->session.active);
			AppendField (payload, "session.paused", snapshot->session.paused);
			return payload;
		}

		i2p::expected<std::string, std::string> BuildTorrentDht (uint32_t tunnelIndex)
		{
			auto tunnels = GetSortedTorrentsTunnels ();
			if (tunnelIndex >= tunnels.size ()) return i2p::unexpected ("torrent tunnel not found");
			auto snapshot = GetTunnelSnapshot (tunnels[tunnelIndex]);
			if (!snapshot) return i2p::unexpected (snapshot.error ());

			std::string payload;
			AppendField (payload, "dht.supported", snapshot->dht.supported);
			AppendField (payload, "dht.numNodes", snapshot->dht.numNodes);
			AppendField (payload, "dht.numBuckets", snapshot->dht.numBuckets);
			AppendField (payload, "dht.numTorrents", snapshot->dht.numTorrents);
			AppendField (payload, "dht.numHeardFrom", snapshot->dht.numHeardFrom);
			AppendField (payload, "dht.numFailedPeers", snapshot->dht.numFailedPeers);
			return payload;
		}

		std::string BuildTorrentCommandResult (uint32_t tunnelIndex, const i2p::torrents::TorrentCommand & command)
		{
			i2p::torrents::TorrentCommandResult result { false, "torrent tunnel not found", command.torrentId };
			auto tunnels = GetSortedTorrentsTunnels ();
			if (tunnelIndex < tunnels.size ())
				result = tunnels[tunnelIndex]->ExecuteControlCommand (command, i2p::torrents::CONTROL_DEFAULT_TIMEOUT);

			std::string payload;
			AppendField (payload, "result", result.success ? "ok" : "error");
			AppendField (payload, "message", result.message);
			AppendField (payload, "id", result.id);
			AppendField (payload, "tunnel", tunnelIndex);
			return payload;
		}
#endif
	}

	DispatchResult Dispatch (std::string_view line)
	{
		auto request = ParseRequest (line);
		if (!request) return { MakeErrorResponse (request.error ().code, request.error ().message), false };

		// A response that exceeds the wire limit is rejected rather than
		// truncated: partial snapshots can hide state or mix sections.
		auto makeOk = [] (std::string payload) -> DispatchResult
		{
			if (payload.size () > MAX_RESPONSE_PAYLOAD)
				return { MakeErrorResponse (ErrorCode::ResponseTooLarge, "snapshot too large"), false };
			return { MakeResponse (ResponseStatus::Ok, payload), false };
		};

		auto makeQuery = [] (i2p::expected<std::string, std::string> result) -> DispatchResult
		{
			if (!result) return { MakeErrorResponse (ErrorCode::Internal, result.error ()), false };
			auto payload = std::move (*result);
			if (payload.size () > MAX_RESPONSE_PAYLOAD)
				return { MakeErrorResponse (ErrorCode::ResponseTooLarge, "snapshot too large"), false };
			return { MakeResponse (ResponseStatus::Ok, payload), false };
		};

		switch (request->type)
		{
		case RequestType::State: return makeOk (BuildState ());
		case RequestType::Peers: return makeOk (BuildPeers (request->limit));
		case RequestType::Tunnels: return makeOk (BuildTunnels ());
		case RequestType::NetDb: return makeOk (BuildNetDb ());
		case RequestType::Destinations: return makeOk (BuildDestinations ());
		case RequestType::Logs: return makeOk (BuildLogs (request->after, request->limit));
		case RequestType::Ping:
		{
			std::string payload;
			AppendField (payload, "pong", i2p::util::GetSecondsSinceEpoch ());
			return makeOk (std::move (payload));
		}
		case RequestType::Quit: return { MakeResponse (ResponseStatus::Ok, { }), true };
#ifndef NO_TORRENTS
		case RequestType::TorrentTunnels: return makeOk (BuildTorrentTunnelList ());
		case RequestType::Torrents: return makeQuery (BuildTorrentList (request->tunnelIndex, request->limit));
		case RequestType::TorrentDetail:
			return makeQuery (BuildTorrentDetail (request->tunnelIndex, request->torrentId));
		case RequestType::TorrentSession: return makeQuery (BuildTorrentSession (request->tunnelIndex));
		case RequestType::TorrentDht: return makeQuery (BuildTorrentDht (request->tunnelIndex));
		case RequestType::TorrentStart:
			return makeOk (BuildTorrentCommandResult (
				request->tunnelIndex, { i2p::torrents::TorrentCommandKind::Start, request->torrentId, false, { } }));
		case RequestType::TorrentStop:
			return makeOk (BuildTorrentCommandResult (
				request->tunnelIndex, { i2p::torrents::TorrentCommandKind::Stop, request->torrentId, false, { } }));
		case RequestType::TorrentReannounce:
			return makeOk (BuildTorrentCommandResult (request->tunnelIndex,
				{ i2p::torrents::TorrentCommandKind::Reannounce, request->torrentId, false, { } }));
		case RequestType::TorrentRemove:
			return makeOk (BuildTorrentCommandResult (request->tunnelIndex,
				{ i2p::torrents::TorrentCommandKind::Remove, request->torrentId, request->deleteFiles, { } }));
		case RequestType::TorrentStartAll:
			return makeOk (BuildTorrentCommandResult (
				request->tunnelIndex, { i2p::torrents::TorrentCommandKind::StartAll, 0, false, { } }));
		case RequestType::TorrentStopAll:
			return makeOk (BuildTorrentCommandResult (
				request->tunnelIndex, { i2p::torrents::TorrentCommandKind::StopAll, 0, false, { } }));
		case RequestType::TorrentReannounceAll:
			return makeOk (BuildTorrentCommandResult (
				request->tunnelIndex, { i2p::torrents::TorrentCommandKind::ReannounceAll, 0, false, { } }));
		case RequestType::TorrentAddMagnet:
			return makeOk (BuildTorrentCommandResult (
				request->tunnelIndex, { i2p::torrents::TorrentCommandKind::AddMagnet, 0, false, request->argument }));
		case RequestType::TorrentAddFile:
			return makeOk (BuildTorrentCommandResult (
				request->tunnelIndex, { i2p::torrents::TorrentCommandKind::AddFile, 0, false, request->argument }));
#endif
		default: return { MakeErrorResponse (ErrorCode::UnknownCommand, "unknown command"), false };
		}
	}
}
}

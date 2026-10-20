/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: TuiServerCommon.
 *
 * Implements the shared TUI dispatcher: parses and validates each request
 * line, rejects oversized responses and builds snapshots of router,
 * transport, tunnel, NetDb, destination, log, configuration and torrent
 * state. Exceptions become internal-error frames so a failed snapshot cannot
 * unwind the IPC thread.
 *
 */

#include "TuiServerCommon.h"

#include <algorithm>
#include <cstdint>
#include <exception>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ClientContext.h"
#include "Config.h"
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
			AppendField (payload, "router.caps", i2p::context.GetRouterInfoProperty ("caps"));
			AppendField (payload, "router.netid", i2p::context.GetNetID ());
			AppendField (payload, "router.floodfill", i2p::context.IsFloodfill ());
			AppendField (payload, "router.accepts_tunnels", i2p::context.AcceptsTunnels ());
			AppendField (payload, "router.uptime", i2p::context.GetUptime ());
			AppendField (payload, "router.bandwidth_limit", i2p::context.GetBandwidthLimit ());
			AppendField (payload, "log.level", static_cast<int> (i2p::log::Logger ().GetLogLevel ()));

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
			AppendField (payload, "connections.count", transports.GetNumConnectedPeers ());

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

		std::string TransportNames (uint8_t transports)
		{
			std::string names;
			auto append = [&names] (const char * name)
			{
				if (!names.empty ()) names.push_back (',');
				names.append (name);
			};
			if (transports & i2p::data::RouterInfo::eNTCP2V4) append ("NTCP2v4");
			if (transports & i2p::data::RouterInfo::eNTCP2V6) append ("NTCP2v6");
			if (transports & i2p::data::RouterInfo::eSSU2V4) append ("SSU2v4");
			if (transports & i2p::data::RouterInfo::eSSU2V6) append ("SSU2v6");
			if (transports & i2p::data::RouterInfo::eNTCP2V6Mesh) append ("Mesh");
			return names.empty () ? std::string ("none") : names;
		}

		void AppendPeerSnapshot (
			std::string & payload, std::string_view field, const i2p::transport::PeerSnapshot & peer)
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
			value.push_back ('|');
			value.append (TransportNames (peer.transports));
			AppendField (payload, field, value);
		}

		std::string BuildPeers (uint32_t limit)
		{
			std::string payload;
			for (const auto & peer : i2p::transport::transports.GetPeersSnapshot (limit))
				AppendPeerSnapshot (payload, "peer", peer);
			return payload;
		}

		std::string BuildConnections (uint32_t limit)
		{
			std::string payload;
			const auto peers = i2p::transport::transports.GetConnectedPeersSnapshot (limit);
			AppendField (payload, "connections.count", peers.size ());
			for (const auto & peer : peers) AppendPeerSnapshot (payload, "connection", peer);
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

		std::string BuildNetDbRouters (uint32_t limit)
		{
			std::string payload;
			const auto routers = i2p::data::netdb.GetRouterInfosSnapshot (limit);
			AppendField (payload, "netdb.routers.listed", routers.size ());
			for (const auto & router : routers)
			{
				std::string value = router->GetIdentHash ().ToBase64 ();
				value.push_back ('|');
				value.append (std::to_string (router->GetVersion ()));
				value.push_back ('|');
				value.append (std::to_string (router->GetCaps ()));
				value.push_back ('|');
				value.push_back (router->IsFloodfill () ? '1' : '0');
				value.push_back ('|');
				value.push_back (router->IsV4 () ? '1' : '0');
				value.push_back ('|');
				value.push_back (router->IsV6 () ? '1' : '0');
				value.push_back ('|');
				value.append (std::to_string (router->GetTimestamp ()));
				value.push_back ('|');
				value.push_back (router->IsUnreachable () ? '0' : '1');
				AppendField (payload, "netdb.router", value);
			}
			return payload;
		}

		std::string BuildNetDbLeaseSets (uint32_t limit)
		{
			std::string payload;
			const auto leaseSets = i2p::data::netdb.GetLeaseSetsSnapshot (limit);
			AppendField (payload, "netdb.leasesets.listed", leaseSets.size ());
			for (const auto & [ident, leaseSet] : leaseSets)
			{
				if (!leaseSet) continue;
				std::string value = ident.ToBase64 ();
				value.push_back ('|');
				value.append (std::to_string (leaseSet->GetStoreType ()));
				value.push_back ('|');
				value.append (std::to_string (leaseSet->GetExpirationTime ()));
				value.push_back ('|');
				value.append (std::to_string (leaseSet->GetEarliestLeaseDate ()));
				value.push_back ('|');
				value.push_back (leaseSet->IsValid () ? '1' : '0');
				value.push_back ('|');
				value.push_back (leaseSet->IsExpired () ? '1' : '0');
				AppendField (payload, "netdb.leaseset", value);
			}
			return payload;
		}

		std::string BuildAdminTransit (bool enabled)
		{
			// RouterContext::m_AcceptsTunnels is atomic. Changing it from the
			// IPC thread does not mutate RouterInfo, tunnel pools or NetDb;
			// it only controls whether future transit requests are accepted.
			i2p::context.SetAcceptsTunnels (enabled);
			LogPrint (eLogInfo, "TUI: transit tunnels ", enabled ? "enabled" : "disabled");
			std::string payload;
			AppendField (payload, "admin", "transit");
			AppendField (payload, "result", "ok");
			AppendField (payload, "transit", enabled);
			AppendField (payload, "message", enabled ? "transit tunnels enabled" : "transit tunnels disabled");
			return payload;
		}

		std::string BuildAdminLogLevel (std::string_view level)
		{
			// Log::SetLogLevel only updates an atomic minimum level. No file,
			// sink or logging thread state is mutated on the IPC thread.
			i2p::log::Logger ().SetLogLevel (std::string (level));
			std::string payload;
			AppendField (payload, "admin", "loglevel");
			AppendField (payload, "result", "ok");
			AppendField (payload, "loglevel", level);
			AppendField (payload, "message", "daemon log level set to " + std::string (level));
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

		enum class ConfigValueKind
		{
			Bool,
			Int,
			UInt16,
			UInt32,
			String
		};

		struct ConfigOptionDescriptor
		{
			const char * name;
			ConfigValueKind kind;
		};

		// Deliberately small and explicit. Nothing that can contain a key,
		// credential, arbitrary filesystem path or URL is included, and the
		// snapshot never exposes a generic option lookup. Adding an entry is
		// a protocol change that must stay bounded and documented.
		constexpr ConfigOptionDescriptor CONFIG_OPTIONS[] = {
			{ "bandwidth", ConfigValueKind::String },
			{ "share", ConfigValueKind::Int },
			{ "netid", ConfigValueKind::Int },
			{ "floodfill", ConfigValueKind::Bool },
			{ "notransit", ConfigValueKind::Bool },
			{ "nat", ConfigValueKind::Bool },
			{ "ipv4", ConfigValueKind::Bool },
			{ "ipv6", ConfigValueKind::Bool },
			{ "port", ConfigValueKind::UInt16 },
			{ "loglevel", ConfigValueKind::String },
			{ "logclftime", ConfigValueKind::Bool },
			{ "family", ConfigValueKind::String },
			{ "reservedrange", ConfigValueKind::Bool },
			{ "stan", ConfigValueKind::Bool },
			{ "ntcp2.enabled", ConfigValueKind::Bool },
			{ "ntcp2.published", ConfigValueKind::Bool },
			{ "ntcp2.port", ConfigValueKind::UInt16 },
			{ "ssu2.enabled", ConfigValueKind::Bool },
			{ "ssu2.published", ConfigValueKind::Bool },
			{ "ssu2.port", ConfigValueKind::UInt16 },
			{ "ssu2.firewalled4", ConfigValueKind::Bool },
			{ "ssu2.firewalled6", ConfigValueKind::Bool },
			{ "upnp.enabled", ConfigValueKind::Bool },
			{ "nettime.enabled", ConfigValueKind::Bool },
			{ "reseed.verify", ConfigValueKind::Bool },
			{ "addressbook.enabled", ConfigValueKind::Bool },
			{ "persist.profiles", ConfigValueKind::Bool },
			{ "persist.addressbook", ConfigValueKind::Bool },
			{ "tui.enabled", ConfigValueKind::Bool },
			{ "limits.transittunnels", ConfigValueKind::UInt32 },
		};

		constexpr size_t CONFIG_OPTION_COUNT = sizeof (CONFIG_OPTIONS) / sizeof (CONFIG_OPTIONS[0]);
		static_assert (CONFIG_OPTION_COUNT <= MAX_CONFIG_RECORDS_PER_REQUEST,
			"TUI configuration allowlist exceeds protocol limit");

		std::string_view ConfigTypeName (ConfigValueKind kind)
		{
			switch (kind)
			{
			case ConfigValueKind::Bool: return "bool";
			case ConfigValueKind::Int: return "int";
			case ConfigValueKind::UInt16: return "uint16";
			case ConfigValueKind::UInt32: return "uint32";
			case ConfigValueKind::String: return "string";
			default: return "string";
			}
		}

		std::string SanitizeConfigValue (std::string_view value)
		{
			const auto length = std::min (value.size (), MAX_CONFIG_VALUE_LENGTH);
			std::string result;
			result.reserve (length);
			for (size_t i = 0; i < length; ++i)
			{
				const auto byte = static_cast<unsigned char> (value[i]);
				result.push_back (byte < 0x20 || byte == 0x7F ? ' ' : value[i]);
			}
			return result;
		}

		std::string BuildConfig ()
		{
			std::string payload;
			payload.reserve (4096);
			AppendField (payload, "config.schema", 1);
			// Version 1 can only read the effective startup configuration.
			// Writes need validation and a main-loop command queue.
			AppendField (payload, "config.readonly", true);

			size_t index = 0;
			auto append = [&payload, &index] (const char * name, ConfigValueKind kind, std::string_view value)
			{
				if (index >= MAX_CONFIG_RECORDS_PER_REQUEST) return;
				const auto prefix = "config.option." + std::to_string (index);
				AppendField (payload, prefix + ".name", name);
				AppendField (payload, prefix + ".type", ConfigTypeName (kind));
				AppendField (payload, prefix + ".value", value);
				++index;
			};

			for (const auto & option : CONFIG_OPTIONS)
			{
				switch (option.kind)
				{
				case ConfigValueKind::Bool:
				{
					bool value = false;
					i2p::config::GetOption (option.name, value);
					append (option.name, option.kind, value ? "1" : "0");
					break;
				}
				case ConfigValueKind::Int:
				{
					int value = 0;
					i2p::config::GetOption (option.name, value);
					append (option.name, option.kind, std::to_string (value));
					break;
				}
				case ConfigValueKind::UInt16:
				{
					uint16_t value = 0;
					i2p::config::GetOption (option.name, value);
					append (option.name, option.kind, std::to_string (value));
					break;
				}
				case ConfigValueKind::UInt32:
				{
					uint32_t value = 0;
					i2p::config::GetOption (option.name, value);
					append (option.name, option.kind, std::to_string (value));
					break;
				}
				case ConfigValueKind::String:
				{
					std::string value;
					i2p::config::GetOption (option.name, value);
					append (option.name, option.kind, SanitizeConfigValue (value));
					break;
				}
				}
			}

			// Report what was actually emitted, not the compile-time array
			// size, so a future bound regression stays observable.
			AppendField (payload, "config.count", static_cast<uint64_t> (index));
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
			AppendField (payload, "comments.enabled", torrent.commentsEnabled);
			AppendField (payload, "comments.total", torrent.commentCount);
			AppendField (payload, "comments.count", torrent.comments.size ());
			AppendField (payload, "comments.ratingCount", torrent.ratingCount);
			AppendField (payload, "comments.average", std::to_string (torrent.averageRating));
			AppendField (payload, "comments.persistenceError", torrent.commentPersistenceError);
			for (size_t i = 0; i < torrent.comments.size (); ++i)
			{
				const auto & comment = torrent.comments[i];
				auto prefix = "comment." + std::to_string (i);
				AppendField (payload, prefix + ".id", comment.localId);
				AppendField (payload, prefix + ".owner", comment.owner);
				AppendField (payload, prefix + ".text", comment.text);
				AppendField (payload, prefix + ".rating", comment.rating);
				AppendField (payload, prefix + ".time", comment.time);
				AppendField (payload, prefix + ".hidden", comment.hidden);
				AppendField (payload, prefix + ".local", comment.local);
			}

			size_t count = 0;
			for (const auto & file : torrent.files)
			{
				if (count++ >= MAX_TORRENT_FILE_RECORDS_PER_REQUEST) break;
				auto prefix = "file." + std::to_string (count - 1);
				AppendField (payload, prefix + ".name", file.name);
				AppendField (payload, prefix + ".length", file.length);
				AppendField (payload, prefix + ".completed", file.completed);
				AppendField (payload, prefix + ".priority", file.priority);
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
			AppendField (payload, "creation.job", snapshot->creation.job);
			AppendField (payload, "creation.pending", snapshot->creation.pending);
			AppendField (payload, "creation.success", snapshot->creation.success);
			AppendField (payload, "creation.torrentId", snapshot->creation.torrentId);
			AppendField (payload, "creation.message", snapshot->creation.message);
			AppendField (payload, "creation.bytesDone", snapshot->creation.bytesDone);
			AppendField (payload, "creation.totalBytes", snapshot->creation.totalBytes);
			AppendField (payload, "creation.filesDone", snapshot->creation.filesDone);
			AppendField (payload, "creation.totalFiles", snapshot->creation.totalFiles);
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

	namespace
	{
		DispatchResult DispatchUnchecked (std::string_view line)
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
			case RequestType::Connections: return makeOk (BuildConnections (request->limit));
			case RequestType::Tunnels: return makeOk (BuildTunnels ());
			case RequestType::NetDb: return makeOk (BuildNetDb ());
			case RequestType::NetDbRouters: return makeOk (BuildNetDbRouters (request->limit));
			case RequestType::NetDbLeaseSets: return makeOk (BuildNetDbLeaseSets (request->limit));
			case RequestType::Destinations: return makeOk (BuildDestinations ());
			case RequestType::Logs: return makeOk (BuildLogs (request->after, request->limit));
			case RequestType::Config: return makeOk (BuildConfig ());
			case RequestType::AdminTransit: return makeOk (BuildAdminTransit (request->enabled));
			case RequestType::AdminLogLevel: return makeOk (BuildAdminLogLevel (request->argument));
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
				return makeOk (BuildTorrentCommandResult (request->tunnelIndex,
					{ i2p::torrents::TorrentCommandKind::Start, request->torrentId, false, { } }));
			case RequestType::TorrentStop:
				return makeOk (BuildTorrentCommandResult (
					request->tunnelIndex, { i2p::torrents::TorrentCommandKind::Stop, request->torrentId, false, { } }));
			case RequestType::TorrentPause:
				return makeOk (BuildTorrentCommandResult (request->tunnelIndex,
					{ i2p::torrents::TorrentCommandKind::Pause, request->torrentId, false, { } }));
			case RequestType::TorrentResume:
				return makeOk (BuildTorrentCommandResult (request->tunnelIndex,
					{ i2p::torrents::TorrentCommandKind::Resume, request->torrentId, false, { } }));
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
			case RequestType::TorrentAddTracker:
				return makeOk (BuildTorrentCommandResult (request->tunnelIndex,
					{ i2p::torrents::TorrentCommandKind::AddTracker, request->torrentId, false, request->argument }));
			case RequestType::TorrentRemoveTracker:
				return makeOk (BuildTorrentCommandResult (request->tunnelIndex,
					{ i2p::torrents::TorrentCommandKind::RemoveTracker, request->torrentId, false,
						request->argument }));
			case RequestType::TorrentCommentsEnable:
				return makeOk (BuildTorrentCommandResult (request->tunnelIndex,
					{ i2p::torrents::TorrentCommandKind::CommentsEnable, request->torrentId, false,
						request->argument }));
			case RequestType::TorrentCommentAdd:
				return makeOk (BuildTorrentCommandResult (request->tunnelIndex,
					{ i2p::torrents::TorrentCommandKind::CommentAdd, request->torrentId, false, request->argument }));
			case RequestType::TorrentCommentHide:
				return makeOk (BuildTorrentCommandResult (request->tunnelIndex,
					{ i2p::torrents::TorrentCommandKind::CommentHide, request->torrentId, false, request->argument }));
			case RequestType::TorrentFilePriority:
				return makeOk (BuildTorrentCommandResult (request->tunnelIndex,
					{ i2p::torrents::TorrentCommandKind::FilePriority, request->torrentId, false, request->argument }));
			case RequestType::TorrentCreateV1:
			{
				i2p::torrents::TorrentCommand command { i2p::torrents::TorrentCommandKind::Create, 0, false, request->argument };
				command.createPieceLength = request->createPieceLength;
				command.createPrivate = request->createPrivate;
				command.createTrackers = request->createTrackers;
				return makeOk (BuildTorrentCommandResult (request->tunnelIndex, command));
			}
			case RequestType::TorrentCreateCancel:
				return makeOk (BuildTorrentCommandResult (request->tunnelIndex,
					{ i2p::torrents::TorrentCommandKind::CancelCreate, 0, false, {} }));
			case RequestType::TorrentCreate:
				return makeOk (BuildTorrentCommandResult (
					request->tunnelIndex, { i2p::torrents::TorrentCommandKind::Create, 0, false, request->argument }));
			case RequestType::TorrentAddMagnet:
				return makeOk (BuildTorrentCommandResult (request->tunnelIndex,
					{ i2p::torrents::TorrentCommandKind::AddMagnet, 0, false, request->argument }));
			case RequestType::TorrentAddFile:
				return makeOk (BuildTorrentCommandResult (
					request->tunnelIndex, { i2p::torrents::TorrentCommandKind::AddFile, 0, false, request->argument }));
#endif
			default: return { MakeErrorResponse (ErrorCode::UnknownCommand, "unknown command"), false };
			}
		}
	}

	DispatchResult Dispatch (std::string_view line)
	{
		// Snapshot collection runs on the IPC thread and must never unwind
		// into std::thread: an allocation or engine failure while serving a
		// local request would terminate the whole router.
		try
		{
			return DispatchUnchecked (line);
		}
		catch (const std::exception &)
		{
			return { MakeErrorResponse (ErrorCode::Internal, "internal error"), false };
		}
		catch (...)
		{
			return { MakeErrorResponse (ErrorCode::Internal, "internal error"), false };
		}
	}

	DispatchResult ImportTorrent (uint32_t tunnelIndex, std::span<const uint8_t> content)
	{
		try
		{
#ifndef NO_TORRENTS
			if (content.empty () || content.size () > i2p::torrents::MAX_CONTROL_FILE_BYTES)
				return { MakeErrorResponse (ErrorCode::BadArgument, "invalid metainfo size"), false };
			auto tunnels = GetSortedTorrentsTunnels ();
			if (tunnelIndex >= tunnels.size ())
				return { MakeErrorResponse (ErrorCode::BadArgument, "torrent tunnel not found"), false };
			auto [torrent, id] = tunnels[tunnelIndex]->AddTorrentFromRpc (
				std::string_view (reinterpret_cast<const char *> (content.data ()), content.size ()));
			std::string payload;
			AppendField (payload, "result", torrent && id ? "ok" : "error");
			AppendField (
				payload, "message", torrent && id ? "torrent imported" : "invalid metainfo or import timed out");
			AppendField (payload, "id", id);
			AppendField (payload, "tunnel", tunnelIndex);
			return { MakeResponse (ResponseStatus::Ok, payload), false };
#else
			return { MakeErrorResponse (ErrorCode::UnknownCommand, "torrents unavailable"), false };
#endif
		}
		catch (...)
		{
			return { MakeErrorResponse (ErrorCode::Internal, "import failed"), false };
		}
	}
}
}

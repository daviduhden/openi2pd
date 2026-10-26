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
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "AddressBook.h"
#include "Blocklist.h"
#include "AddressFormat.h"
#include "ClientContext.h"
#include "Config.h"
#include "Expected.h"
#include "I2PService.h"
#include "I2PTunnel.h"
#include "Log.h"
#include "NetDb.hpp"
#include "RouterContext.h"
#include "RouterInfo.h"
#include "Timestamp.h"
#include "TorrentsControl.h"
#include "Transports.h"
#include "Tunnel.h"
#include "UDPTunnel.h"
#include "TuiProtocol.h"
#include "version.h"

namespace i2p
{
namespace tui
{
	// The protocol parser and the address book validate the same URL bound; a
	// change in one without the other would let the IPC side accept a value the
	// book then rejects (or vice versa).
	static_assert (i2p::tui::MAX_SUBSCRIPTION_URL_LENGTH == i2p::client::MAX_SUBSCRIPTION_URL_LENGTH,
		"TUI and address-book subscription URL bounds must match");
#ifndef NO_TORRENTS
	// The comment page size is declared once by the protocol and once by the
	// snapshot helper; a page larger than the wire bound would truncate
	// silently and a smaller one would waste responses.
	static_assert (i2p::torrents::MAX_COMMENT_PAGE == MAX_TORRENT_COMMENT_RECORDS_PER_REQUEST,
		"TUI and torrent comment page bounds must match");
#endif
	namespace
	{
		enum class QueueSlotState : uint8_t
		{
			Free = 0,
			Queued,
			Running,
			Done
		};

		struct QueueSlot
		{
			AdminCommand command { };
			AdminCommandResult result { };
			QueueSlotState state = QueueSlotState::Free;
			// Set by an IPC caller that stopped waiting. The executor then
			// drops the result instead of publishing it to a dead waiter.
			bool discard = false;
		};

		struct AdminQueue
		{
			~AdminQueue ()
			{
				// A joinable thread at static destruction would terminate the
				// process. Normal shutdown stops the queue first; this is the
				// last-resort guard for an abnormal exit path.
				{
					std::lock_guard<std::mutex> lock (mutex);
					stopping.store (true, std::memory_order_relaxed);
				}
				changed.notify_all ();
				if (executorThread.joinable ()) executorThread.join ();
			}

			std::mutex mutex;
			std::condition_variable changed;
			std::array<QueueSlot, ADMIN_COMMAND_QUEUE_CAPACITY> slots { };
			std::thread executorThread;
			std::atomic<bool> stopping { false };
			AdminCommandExecutor executor = nullptr;
			bool running = false;
		};

		AdminQueue & GetAdminQueue ()
		{
			// Function-local so transport-only unit tests can link this
			// translation unit without constructing the daemon singleton.
			static AdminQueue queue;
			return queue;
		}

		bool HasQueuedSlot (const AdminQueue & queue)
		{
			for (const auto & slot : queue.slots)
				if (slot.state == QueueSlotState::Queued) return true;
			return false;
		}

		void SetAdminMessage (AdminCommandResult & result, const char * message)
		{
			const int written = std::snprintf (result.message, sizeof (result.message), "%s", message ? message : "");
			result.messageLength
				= written > 0 ? std::min<size_t> (static_cast<size_t> (written), sizeof (result.message) - 1) : 0;
		}

		void RunAdminQueue (AdminQueue * queue)
		{
			for (;;)
			{
				AdminCommand command { };
				size_t index = 0;
				AdminCommandExecutor executor = nullptr;
				{
					std::unique_lock<std::mutex> lock (queue->mutex);
					queue->changed.wait (lock,
						[queue] { return queue->stopping.load (std::memory_order_relaxed) || HasQueuedSlot (*queue); });
					if (queue->stopping.load (std::memory_order_relaxed))
					{
						// Stop() must not leave a waiter blocked behind a
						// command that was never going to run.
						for (auto & slot : queue->slots)
						{
							if (slot.state == QueueSlotState::Queued) slot.state = QueueSlotState::Free;
							slot.discard = false;
						}
						queue->changed.notify_all ();
						return;
					}

					while (index < queue->slots.size () && queue->slots[index].state != QueueSlotState::Queued) ++index;
					if (index == queue->slots.size ()) continue;

					auto & slot = queue->slots[index];
					// "discard" means the waiter no longer wants the result;
					// it must never mean "do not execute". The command still
					// runs exactly once and its result is dropped at completion.
					slot.state = QueueSlotState::Running;
					command = slot.command;
					executor = queue->executor;
				}

				AdminCommandResult result { };
				if (executor)
				{
					try
					{
						executor (command, result);
					}
					catch (...)
					{
						// A subsystem exception must not kill the single
						// administrative executor and strand every later IPC
						// waiter. The operation is reported as failed.
						result = AdminCommandResult { };
						result.code = AdminResultCode::Failed;
						SetAdminMessage (result, "admin command failed");
					}
				}
				else
				{
					result.code = AdminResultCode::Unavailable;
					SetAdminMessage (result, "admin command executor is unavailable");
				}

				{
					std::lock_guard<std::mutex> lock (queue->mutex);
					auto & slot = queue->slots[index];
					if (slot.discard)
					{
						slot.state = QueueSlotState::Free;
						slot.discard = false;
					}
					else
					{
						slot.result = result;
						slot.state = QueueSlotState::Done;
					}
					queue->changed.notify_all ();
				}
			}
		}

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
			AppendField (payload, "log.dropped", i2p::log::Logger ().GetDroppedCount ());

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

		std::string_view ReseedPhaseName (i2p::data::ReseedPhase phase)
		{
			switch (phase)
			{
			case i2p::data::ReseedPhase::Idle: return "idle";
			case i2p::data::ReseedPhase::Running: return "running";
			case i2p::data::ReseedPhase::Succeeded: return "succeeded";
			case i2p::data::ReseedPhase::Failed: return "failed";
			case i2p::data::ReseedPhase::Disabled: return "disabled";
			default: return "idle";
			}
		}

		std::string BuildNetDb ()
		{
			std::string payload;
			AppendField (payload, "netdb.routers", i2p::data::netdb.GetNumRouters ());
			AppendField (payload, "netdb.floodfills", i2p::data::netdb.GetNumFloodfills ());
			AppendField (payload, "netdb.leasesets", i2p::data::netdb.GetNumLeaseSets ());
			AppendField (payload, "netdb.reseeding", i2p::data::netdb.IsReseeding ());
			// Bounded, non-secret administrative view of the reseed job. It
			// never waits for the job and never carries a URL or a proxy.
			const auto reseed = i2p::data::netdb.GetReseedState ();
			AppendField (payload, "netdb.reseed.phase", ReseedPhaseName (reseed.phase));
			AppendField (payload, "netdb.reseed.enabled", reseed.enabled);
			AppendField (payload, "netdb.reseed.attempts", reseed.attempts);
			AppendField (payload, "netdb.reseed.succeeded", reseed.succeeded);
			AppendField (payload, "netdb.reseed.last_attempt_ms", reseed.lastAttemptMs);
			AppendField (payload, "netdb.reseed.last_success_ms", reseed.lastSuccessMs);
			AppendField (payload, "netdb.reseed.last_error",
				std::string_view (reseed.lastError).substr (0, i2p::data::RESEED_MAX_ERROR_LENGTH));
			AppendField (payload, "router.floodfill", i2p::context.IsFloodfill ());
			AppendField (payload, "router.hidden", i2p::context.IsHidden ());
			AppendField (payload, "router.family", i2p::context.GetFamily ());
			const auto blocklist = i2p::data::GetOperatorBlocklistStats ();
			AppendField (payload, "blocklist.loaded", blocklist.loaded);
			AppendField (payload, "blocklist.ipv4", blocklist.ipv4Ranges);
			AppendField (payload, "blocklist.ipv6", blocklist.ipv6Addresses);
			AppendField (payload, "blocklist.hashes", blocklist.routerHashes);
			AppendField (payload, "blocklist.runtime", i2p::data::GetRuntimeRouterBanCount ());
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
				value.append (std::to_string (leaseSet->GetEncryptionType ()));
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

		std::string BuildAdminCommandResult (std::string_view action, const AdminCommandResult & result)
		{
			// The queue already executed the operation on the daemon-owned
			// administrative thread. This function only serializes the typed
			// outcome; it never touches router, tunnel or logger state.
			std::string payload;
			AppendField (payload, "admin", action);
			std::string_view status = "error";
			if (result.code == AdminResultCode::Ok)
				status = "ok";
			else if (result.code == AdminResultCode::Accepted)
				status = "accepted";
			AppendField (payload, "result", status);
			AppendField (payload, "code", AdminResultCodeName (result.code));
			// Discrete outcome token for the verbs that have one. It is absent
			// for every other verb so an older client sees the same fields as
			// before.
			if (result.outcome != AdminOutcome::None)
				AppendField (payload, "outcome", AdminOutcomeName (result.outcome));
			AppendField (payload, "value", result.value);
			if (action == "transit") AppendField (payload, "transit", result.enabled);
			if (action == "tunnel-start" || action == "tunnel-stop") AppendField (payload, "running", result.enabled);
			AppendField (payload, "message",
				std::string_view (result.message, std::min (result.messageLength, sizeof (result.message) - 1)));
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

		std::string BuildServices (uint32_t limit)
		{
			std::string payload;
			size_t count = 0;
			auto clean = [] (std::string text)
			{
				for (auto & ch : text)
					if (ch == '|' || static_cast<unsigned char> (ch) < 0x20 || ch == 0x7f) ch = ' ';
				return text;
			};
			auto add = [&] (std::string_view kind, std::string_view name, std::string endpoint, std::string address,
						   bool enabled)
			{
				if (count >= limit) return;
				std::string value (kind);
				value.push_back ('|');
				value.append (clean (std::string (name)));
				value.push_back ('|');
				value.append (clean (std::move (endpoint)));
				value.push_back ('|');
				value.append (clean (std::move (address)));
				value.push_back ('|');
				value.push_back (enabled ? '1' : '0');
				AppendField (payload, "service", value);
				++count;
			};
			auto addressOf = [] (const std::shared_ptr<const i2p::client::ClientDestination> & destination)
			{ return destination ? destination->GetIdentHash ().ToBase32 () + ".b32.i2p" : std::string (); };

			for (const auto & [endpoint, service] : i2p::client::context.GetClientTunnelsSnapshot ())
			{
				if (!service) continue;
				add ("tcp-client", service->GetName (), i2p::util::net::FormatEndpoint (endpoint),
					addressOf (service->GetLocalDestination ()), true);
			}
			for (const auto & entry : i2p::client::context.GetServerTunnelsSnapshot ())
			{
				const auto & service = entry.second;
				if (!service) continue;
				add ("tcp-server", service->GetName (),
					service->GetAddress () + ":" + std::to_string (service->GetPort ()),
					addressOf (service->GetLocalDestination ()), true);
			}
			for (const auto & [endpoint, service] : i2p::client::context.GetClientForwardsSnapshot ())
			{
				if (!service) continue;
				add ("udp-client", service->GetName (), i2p::util::net::FormatEndpoint (endpoint),
					addressOf (service->GetLocalDestination ()), true);
			}
			for (const auto & [key, service] : i2p::client::context.GetServerForwardsSnapshot ())
			{
				if (!service) continue;
				add ("udp-server", service->GetName (), "i2p port " + std::to_string (key.second),
					addressOf (service->GetLocalDestination ()), true);
			}
			if (auto proxy = i2p::client::context.GetHttpProxy ())
				add ("http-proxy", proxy->GetName (), "local proxy", addressOf (proxy->GetLocalDestination ()), true);
			if (auto proxy = i2p::client::context.GetSocksProxy ())
				add ("socks-proxy", proxy->GetName (), "local proxy", addressOf (proxy->GetLocalDestination ()), true);
			const auto sam = i2p::client::context.GetSAMBridge ();
			add ("sam", "SAM bridge", "local control", { }, sam != nullptr);
			const auto bob = i2p::client::context.GetBOBCommandChannel ();
			add ("bob", "BOB command channel", "local control", { }, bob != nullptr);
			const auto i2cp = i2p::client::context.GetI2CPServer ();
			add ("i2cp", "I2CP server", "local control", { }, i2cp != nullptr);
			AppendField (payload, "service.count", count);

			// Configured client/server tunnels, each with its unique service
			// name, kind and running state so the console can start or stop
			// exactly one of them. Only public labels are published: no
			// private keys, no identity files and no local filesystem path
			// beyond the already-published listener endpoint. Each field is
			// additionally capped so a hostile configuration value cannot
			// turn one row into a large snapshot.
			auto cap = [] (std::string text)
			{
				constexpr size_t MAX_FIELD = 256;
				if (text.size () > MAX_FIELD) text.resize (MAX_FIELD);
				return text;
			};
			size_t tunnelCount = 0;
			for (const auto & service : i2p::client::context.GetTunnelServices ())
			{
				if (tunnelCount >= limit) break;
				std::string value = cap (clean (service.name));
				value.push_back ('|');
				value.append (cap (clean (service.kind)));
				value.push_back ('|');
				value.push_back (service.running ? '1' : '0');
				value.push_back ('|');
				value.append (cap (clean (service.localEndpoint)));
				value.push_back ('|');
				value.append (cap (clean (service.destination)));
				AppendField (payload, "tunnel.service", value);
				++tunnelCount;
			}
			AppendField (payload, "tunnel.service.count", tunnelCount);
			return payload;
		}

		std::string BuildAddressBook (uint32_t limit)
		{
			std::string payload;
			const auto entries = i2p::client::context.GetAddressBook ().GetEntries (limit);
			AppendField (payload, "address.count", entries.size ());
			for (const auto & entry : entries)
			{
				std::string value = entry.name;
				value.push_back ('|');
				value.append (entry.destination);
				value.push_back ('|');
				value.push_back (entry.blinded ? '1' : '0');
				AppendField (payload, "address", value);
			}
			return payload;
		}

		std::string BuildBlockedAddresses (uint32_t limit)
		{
			std::string payload;
			const auto patterns = i2p::client::context.GetAddressBook ().GetDeletedHosts (limit);
			AppendField (payload, "blocked.count", patterns.size ());
			for (const auto & pattern : patterns) AppendField (payload, "blocked", pattern);
			return payload;
		}

		std::string BuildSubscriptions (uint32_t limit)
		{
			std::string payload;
			const auto urls = i2p::client::context.GetAddressBook ().GetSubscriptions (limit);
			AppendField (payload, "subscription.count", urls.size ());
			for (const auto & url : urls) AppendField (payload, "subscription", url);
			return payload;
		}

		std::string BuildBlocklist (uint32_t limit)
		{
			std::string payload;
			const auto stats = i2p::data::GetOperatorBlocklistStats ();
			AppendField (payload, "blocklist.loaded", stats.loaded);
			AppendField (payload, "blocklist.ipv4", stats.ipv4Ranges);
			AppendField (payload, "blocklist.ipv6", stats.ipv6Addresses);
			AppendField (payload, "blocklist.hashes", stats.routerHashes);
			std::vector<i2p::data::IdentHash> bans;
			const auto count = i2p::data::GetRuntimeRouterBans (bans, limit);
			AppendField (payload, "blocklist.runtime", count);
			for (size_t i = 0; i < bans.size (); ++i)
				AppendField (payload, "blocklist.banned", bans[i].ToBase64 ());
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
			AppendField (payload, field ("wanted"), torrent.wantedSize);
			AppendField (payload, field ("have"), torrent.have);
			AppendField (payload, field ("private"), torrent.privateTorrent);
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

		void AppendTorrentDetail (std::string & payload, const i2p::torrents::TorrentInfo & torrent,
			uint32_t commentOffset, uint32_t commentLimit)
		{
			AppendTorrentSummary (payload, "detail", torrent);
			AppendField (payload, "detail.comment", torrent.metaComment);
			AppendField (payload, "detail.createdBy", torrent.createdBy);
			AppendField (payload, "comments.enabled", torrent.commentsEnabled);
			AppendField (payload, "comments.total", torrent.commentCount);
			// Retained classification over the whole snapshot vector, not only
			// over the emitted page: hidden records are retained for
			// moderation and must stay countable after a page boundary.
			AppendField (payload, "comments.visible", torrent.commentVisibleCount);
			AppendField (payload, "comments.hidden", torrent.commentHiddenCount);
			AppendField (payload, "comments.local", torrent.commentLocalCount);
			// One response emits at most MAX_TORRENT_COMMENT_RECORDS_PER_REQUEST
			// records. Clients page with TORRENT-DETAIL <tunnel> <id> <offset>
			// <limit>; without the page arguments this is the newest page,
			// which is exactly the response earlier clients already parse.
			const auto page
				= i2p::torrents::SliceCommentPage (torrent.comments.size (), commentOffset, commentLimit);
			// comments.offset/count describe the emitted slice; comments.total
			// remains the retained count, so clients must not iterate count
			// against a truncated comments array.
			AppendField (payload, "comments.offset", page.offset);
			AppendField (payload, "comments.count", page.count);
			AppendField (payload, "comments.ratingCount", torrent.ratingCount);
			{
				char average[16];
				std::snprintf (average, sizeof (average), "%.1f", torrent.averageRating);
				AppendField (payload, "comments.average", average);
			}
			AppendField (payload, "comments.persistenceError", torrent.commentPersistenceError);
			for (size_t i = 0; i < page.count; ++i)
			{
				const auto & comment = torrent.comments[page.offset + i];
				auto prefix = "comment." + std::to_string (i);
				AppendField (payload, prefix + ".id", comment.localId);
				AppendField (payload, prefix + ".owner", comment.owner);
				AppendField (payload, prefix + ".text", comment.text);
				AppendField (payload, prefix + ".rating", comment.rating);
				AppendField (payload, prefix + ".time", comment.time);
				AppendField (payload, prefix + ".hidden", comment.hidden);
				AppendField (payload, prefix + ".local", comment.local);
			}
			AppendField (payload, "comments.truncated", page.truncated);

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
				AppendField (payload, prefix + ".tier", tracker.tier);
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

		i2p::expected<std::string, std::string> BuildTorrentDetail (
			uint32_t tunnelIndex, int torrentId, uint32_t commentOffset, uint32_t commentLimit)
		{
			auto tunnels = GetSortedTorrentsTunnels ();
			if (tunnelIndex >= tunnels.size ()) return i2p::unexpected ("torrent tunnel not found");
			auto snapshot = GetTunnelSnapshot (tunnels[tunnelIndex]);
			if (!snapshot) return i2p::unexpected (snapshot.error ());

			auto it = std::find_if (snapshot->torrents.begin (), snapshot->torrents.end (),
				[torrentId] (const auto & torrent) { return torrent.id == torrentId; });
			if (it == snapshot->torrents.end ()) return i2p::unexpected ("torrent not found");
			std::string payload;
			AppendTorrentDetail (payload, *it, commentOffset, commentLimit);
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

	bool StartAdminCommandQueue (AdminCommandExecutor executor)
	{
		if (!executor) return false;
		auto & queue = GetAdminQueue ();
		{
			std::lock_guard<std::mutex> lock (queue.mutex);
			if (queue.running) return false;
			for (auto & slot : queue.slots)
			{
				slot = QueueSlot { };
				slot.state = QueueSlotState::Free;
			}
			queue.executor = executor;
			queue.stopping.store (false, std::memory_order_relaxed);
			queue.running = true;
		}
		try
		{
			queue.executorThread = std::thread ([&queue] { RunAdminQueue (&queue); });
		}
		catch (...)
		{
			std::lock_guard<std::mutex> lock (queue.mutex);
			queue.running = false;
			queue.executor = nullptr;
			return false;
		}
		return true;
	}

	void StopAdminCommandQueue ()
	{
		auto & queue = GetAdminQueue ();
		{
			std::lock_guard<std::mutex> lock (queue.mutex);
			if (!queue.running) return;
			queue.stopping.store (true, std::memory_order_relaxed);
			queue.changed.notify_all ();
		}
		if (queue.executorThread.joinable ()) queue.executorThread.join ();
		{
			std::lock_guard<std::mutex> lock (queue.mutex);
			for (auto & slot : queue.slots)
			{
				slot = QueueSlot { };
				slot.state = QueueSlotState::Free;
			}
			queue.executor = nullptr;
			queue.running = false;
			queue.stopping.store (false, std::memory_order_relaxed);
		}
	}

	AdminCommandResult SubmitAdminCommand (const AdminCommand & command, std::chrono::milliseconds timeout)
	{
		AdminCommandResult unavailable { };
		unavailable.code = AdminResultCode::Unavailable;
		SetAdminMessage (unavailable, "admin command queue is not running");

		auto & queue = GetAdminQueue ();
		std::unique_lock<std::mutex> lock (queue.mutex);
		if (!queue.running || queue.stopping.load (std::memory_order_relaxed) || !queue.executor) return unavailable;

		QueueSlot * selected = nullptr;
		size_t index = 0;
		for (; index < queue.slots.size (); ++index)
		{
			if (queue.slots[index].state == QueueSlotState::Free)
			{
				selected = &queue.slots[index];
				break;
			}
		}
		if (!selected)
		{
			AdminCommandResult full { };
			full.code = AdminResultCode::QueueFull;
			SetAdminMessage (full, "admin command queue is full");
			return full;
		}

		selected->command = command;
		selected->result = AdminCommandResult { };
		selected->discard = false;
		selected->state = QueueSlotState::Queued;
		queue.changed.notify_all ();

		auto & slot = queue.slots[index];
		if (!queue.changed.wait_for (lock, timeout, [&slot] { return slot.state == QueueSlotState::Done; }))
		{
			// Stop() may have retired the queued command while we waited.
			if (slot.state == QueueSlotState::Free) return unavailable;
			// The command is still queued or executing. Do not cancel it:
			// record that the result is no longer wanted and let the daemon
			// thread finish at its own bounded pace.
			if (slot.state != QueueSlotState::Done)
			{
				slot.discard = true;
				AdminCommandResult accepted { };
				accepted.code = AdminResultCode::Accepted;
				SetAdminMessage (accepted, "command accepted; still running");
				return accepted;
			}
		}

		auto result = slot.result;
		slot.state = QueueSlotState::Free;
		slot.discard = false;
		queue.changed.notify_all ();
		return result;
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

			// Router-wide writes are handed to the daemon-owned executor. The
			// IPC thread only submits a fixed-size command and waits for a
			// bounded, typed result; it never mutates router, tunnel, NetDb or
			// logger state itself.
			auto runAdmin = [&makeOk] (std::string_view action, const AdminCommand & command) -> DispatchResult
			{ return makeOk (BuildAdminCommandResult (action, SubmitAdminCommand (command, ADMIN_COMMAND_TIMEOUT))); };

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
			case RequestType::Services: return makeOk (BuildServices (request->limit));
			case RequestType::AddressBook: return makeOk (BuildAddressBook (request->limit));
			case RequestType::AddressBookBlocked: return makeOk (BuildBlockedAddresses (request->limit));
			case RequestType::AddressBookSubscriptions: return makeOk (BuildSubscriptions (request->limit));
			case RequestType::Blocklist: return makeOk (BuildBlocklist (request->limit));
			case RequestType::Logs: return makeOk (BuildLogs (request->after, request->limit));
			case RequestType::Config: return makeOk (BuildConfig ());
			case RequestType::AdminTransit:
				return runAdmin ("transit", { AdminCommandKind::Transit, 0, request->enabled });
			case RequestType::AdminOnline:
				return runAdmin ("online", { AdminCommandKind::Online, 0, request->enabled });
			case RequestType::AdminLogReopen:
				return runAdmin ("log-reopen", { AdminCommandKind::LogReopen, 0, false });
			case RequestType::AdminLogLevel:
				return runAdmin (
					"loglevel", { AdminCommandKind::LogLevel, static_cast<int32_t> (request->adminValue), false });
			case RequestType::AdminReseed: return runAdmin ("reseed", { AdminCommandKind::Reseed, 0, false });
			case RequestType::AdminReloadTunnels:
				return runAdmin ("reload-tunnels", { AdminCommandKind::ReloadTunnels, 0, false });
			case RequestType::AdminBandwidth:
				return runAdmin (
					"bandwidth", { AdminCommandKind::Bandwidth, static_cast<int32_t> (request->adminValue), false });
			case RequestType::AdminShare:
				return runAdmin (
					"share", { AdminCommandKind::Share, static_cast<int32_t> (request->adminValue), false });
			case RequestType::AdminTunnelStart:
			case RequestType::AdminTunnelStop:
			{
				AdminCommand command;
				const bool start = request->type == RequestType::AdminTunnelStart;
				command.kind = start ? AdminCommandKind::TunnelStart : AdminCommandKind::TunnelStop;
				// The parser already bounded the name to printable,
				// whitespace-free text; the fixed buffer copy is the second
				// bound and never depends on the request length.
				std::snprintf (command.name, sizeof (command.name), "%s", request->argument.c_str ());
				return runAdmin (start ? "tunnel-start" : "tunnel-stop", command);
			}
			case RequestType::AdminAddressAdd:
			case RequestType::AdminAddressReplace:
			case RequestType::AdminAddressRemove:
			case RequestType::AdminAddressBlock:
			case RequestType::AdminAddressUnblock:
			{
				AdminCommand command;
				std::string_view action;
				if (request->type == RequestType::AdminAddressAdd || request->type == RequestType::AdminAddressReplace)
				{
					command.kind = AdminCommandKind::AddressAdd;
					// ADDRESS-ADD never re-points an existing name; only the
					// explicit replace verb authorises the overwrite.
					command.enabled = request->type == RequestType::AdminAddressReplace;
					action = command.enabled ? "address-replace" : "address-add";
				}
				else if (request->type == RequestType::AdminAddressRemove)
				{
					command.kind = AdminCommandKind::AddressRemove;
					action = "address-remove";
				}
				else if (request->type == RequestType::AdminAddressBlock)
				{
					command.kind = AdminCommandKind::AddressBlock;
					action = "address-block";
				}
				else
				{
					command.kind = AdminCommandKind::AddressUnblock;
					action = "address-unblock";
				}
				std::snprintf (command.name, sizeof (command.name), "%s", request->argument.c_str ());
				std::snprintf (command.destination, sizeof (command.destination), "%s", request->argument2.c_str ());
				return runAdmin (action, command);
			}
			case RequestType::AdminNetDbBan:
			case RequestType::AdminNetDbUnban:
			{
				AdminCommand command;
				const bool ban = request->type == RequestType::AdminNetDbBan;
				command.kind = ban ? AdminCommandKind::NetDbBan : AdminCommandKind::NetDbUnban;
				std::snprintf (command.name, sizeof (command.name), "%s", request->argument.c_str ());
				return runAdmin (ban ? "netdb-ban" : "netdb-unban", command);
			}
			case RequestType::AdminAddressSubAdd:
			case RequestType::AdminAddressSubRemove:
			{
				AdminCommand command;
				const bool add = request->type == RequestType::AdminAddressSubAdd;
				command.kind = add ? AdminCommandKind::AddressSubAdd : AdminCommandKind::AddressSubRemove;
				std::snprintf (command.destination, sizeof (command.destination), "%s", request->argument.c_str ());
				return runAdmin (add ? "address-sub-add" : "address-sub-remove", command);
			}
			case RequestType::AdminShutdown:
			{
				switch (request->shutdownMode)
				{
				case AdminShutdownMode::Graceful:
					return runAdmin ("shutdown", { AdminCommandKind::ShutdownGraceful, 0, false });
				case AdminShutdownMode::Immediate:
					return runAdmin ("shutdown", { AdminCommandKind::ShutdownImmediate, 0, false });
				case AdminShutdownMode::Cancel:
					return runAdmin ("shutdown", { AdminCommandKind::ShutdownCancel, 0, false });
				}
				return { MakeErrorResponse (ErrorCode::BadArgument, "unknown shutdown mode"), false };
			}
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
				return makeQuery (BuildTorrentDetail (request->tunnelIndex, request->torrentId,
					request->commentOffset, request->commentLimit));
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
			case RequestType::TorrentRecheck:
				return makeOk (BuildTorrentCommandResult (request->tunnelIndex,
					{ i2p::torrents::TorrentCommandKind::Recheck, request->torrentId, false, { } }));
			case RequestType::TorrentRemove:
				return makeOk (BuildTorrentCommandResult (request->tunnelIndex,
					{ i2p::torrents::TorrentCommandKind::Remove, request->torrentId, request->deleteFiles, { } }));
			case RequestType::TorrentRemoveAll:
				// Session-wide removal: no torrent id is involved, and the
				// files flag is the same exact token as TORRENT-REMOVE.
				return makeOk (BuildTorrentCommandResult (request->tunnelIndex,
					{ i2p::torrents::TorrentCommandKind::RemoveAll, 0, request->deleteFiles, { } }));
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
			case RequestType::TorrentCommentUnhide:
				return makeOk (BuildTorrentCommandResult (request->tunnelIndex,
					{ request->type == RequestType::TorrentCommentHide
							? i2p::torrents::TorrentCommandKind::CommentHide
							: i2p::torrents::TorrentCommandKind::CommentUnhide,
						request->torrentId, false, request->argument }));
			case RequestType::TorrentFilePriority:
				return makeOk (BuildTorrentCommandResult (request->tunnelIndex,
					{ i2p::torrents::TorrentCommandKind::FilePriority, request->torrentId, false, request->argument }));
			case RequestType::TorrentSetComment:
				return makeOk (BuildTorrentCommandResult (request->tunnelIndex,
					{ i2p::torrents::TorrentCommandKind::SetMetaComment, request->torrentId, false,
						request->argument }));
			case RequestType::TorrentSetCreatedBy:
				return makeOk (BuildTorrentCommandResult (request->tunnelIndex,
					{ i2p::torrents::TorrentCommandKind::SetCreatedBy, request->torrentId, false, request->argument }));
			case RequestType::TorrentCreateV1:
			{
				i2p::torrents::TorrentCommand command { i2p::torrents::TorrentCommandKind::Create, 0, false,
					request->argument };
				command.createPieceLength = request->createPieceLength;
				command.createPrivate = request->createPrivate;
				command.createTrackers = request->createTrackers;
				return makeOk (BuildTorrentCommandResult (request->tunnelIndex, command));
			}
			case RequestType::TorrentCreateCancel:
				return makeOk (BuildTorrentCommandResult (
					request->tunnelIndex, { i2p::torrents::TorrentCommandKind::CancelCreate, 0, false, { } }));
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
			// Keep the shared signature used by the torrent-enabled build.
			(void)tunnelIndex;
			(void)content;
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

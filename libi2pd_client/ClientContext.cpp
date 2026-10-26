/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: ClientContext.
 *
 * Orchestrates client startup and teardown: it creates the shared local
 * destination, starts the address book, proxies, configured tunnels, SAM,
 * BOB, I2CP and torrent RPC servers, and parses tunnel sections and I2CP
 * options on reload. Destinations and tunnels are tracked so they can be
 * replaced or stopped without dangling references.
 *
 */

#include <fstream>
#include <iostream>
#include <sstream>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/ini_parser.hpp>
#include "Config.h"
#include "FS.h"
#include "Log.h"
#include "Identity.h"
#include "Crypto.h"
#include "util.h"
#include "ClientContext.h"
#include "HTTPProxy.h"
#include "SOCKS.h"
#include "MatchedDestination.h"

namespace i2p
{
namespace client
{
	ClientContext context;

	ClientContext::ClientContext ()
		: m_SharedLocalDestination (nullptr)
	{
	}

	ClientContext::~ClientContext ()
	{
		// Preserve the historical destruction order (SAM, BOB, I2CP). Stop()
		// already resets these; the explicit resets keep the destructor correct
		// when the context is torn down without a preceding Stop().
		m_SamBridge.reset ();
		m_BOBCommandChannel.reset ();
		m_I2CPServer.reset ();
	}

	void ClientContext::Start ()
	{
		// shared local destination
		if (!GetSharedLocalDestination ()) CreateNewSharedLocalDestination ();

		// addressbook
		m_AddressBook.Start ();

		// HTTP proxy
		ReadHttpProxy ();

		// SOCKS proxy
		ReadSocksProxy ();

		// I2P tunnels
		ReadTunnels ();

		// SAM
		bool sam;
		i2p::config::GetOption ("sam.enabled", sam);
		if (sam)
		{
			std::string samAddr;
			i2p::config::GetOption ("sam.address", samAddr);
			uint16_t samPortTCP;
			i2p::config::GetOption ("sam.port", samPortTCP);
			uint16_t samPortUDP;
			i2p::config::GetOption ("sam.portudp", samPortUDP);
			bool singleThread;
			i2p::config::GetOption ("sam.singlethread", singleThread);
			LogPrint (eLogInfo, "Clients: Starting SAM bridge at ", samAddr, ":[", samPortTCP, "|", samPortUDP, "]");
			try
			{
				m_SamBridge = std::make_unique<SAMBridge> (samAddr, samPortTCP, samPortUDP, singleThread);
				m_SamBridge->Start ();
			}
			catch (std::exception & e)
			{
				LogPrint (eLogCritical, "Clients: Exception in SAM bridge: ", e.what ());
				ThrowFatal (
					"Unable to start SAM bridge at ", samAddr, ":[", samPortTCP, "|", samPortUDP, "]: ", e.what ());
			}
		}

		// BOB
		bool bob;
		i2p::config::GetOption ("bob.enabled", bob);
		if (bob)
		{
			std::string bobAddr;
			i2p::config::GetOption ("bob.address", bobAddr);
			uint16_t bobPort;
			i2p::config::GetOption ("bob.port", bobPort);
			LogPrint (eLogInfo, "Clients: Starting BOB command channel at ", bobAddr, ":", bobPort);
			try
			{
				m_BOBCommandChannel = std::make_unique<BOBCommandChannel> (bobAddr, bobPort);
				m_BOBCommandChannel->Start ();
			}
			catch (std::exception & e)
			{
				LogPrint (eLogCritical, "Clients: Exception in BOB bridge: ", e.what ());
				ThrowFatal ("Unable to start BOB bridge at ", bobAddr, ":", bobPort, ": ", e.what ());
			}
		}

		// I2CP
		bool i2cp;
		i2p::config::GetOption ("i2cp.enabled", i2cp);
		if (i2cp)
		{
			std::string i2cpAddr;
			i2p::config::GetOption ("i2cp.address", i2cpAddr);
			uint16_t i2cpPort;
			i2p::config::GetOption ("i2cp.port", i2cpPort);
			bool singleThread;
			i2p::config::GetOption ("i2cp.singlethread", singleThread);
			LogPrint (eLogInfo, "Clients: Starting I2CP at ", i2cpAddr, ":", i2cpPort);
			try
			{
				m_I2CPServer = std::make_unique<I2CPServer> (i2cpAddr, i2cpPort, singleThread);
				m_I2CPServer->Start ();
			}
			catch (std::exception & e)
			{
				LogPrint (eLogCritical, "Clients: Exception in I2CP: ", e.what ());
				ThrowFatal ("Unable to start I2CP at ", i2cpAddr, ":", i2cpPort, ": ", e.what ());
			}
		}

		m_AddressBook.StartResolvers ();

#ifndef NO_TORRENTS
		// start torrents RPC server
		for (const auto & server : GetTorrentsRPCServersSnapshot ()) server->Start ();
#endif
	}

	std::shared_ptr<I2PService> ClientContext::DetachHttpProxy ()
	{
		std::lock_guard<std::mutex> l (m_ProxyMutex);
		auto proxy = m_HttpProxy;
		m_HttpProxy = nullptr;
		return proxy;
	}

	std::shared_ptr<I2PService> ClientContext::DetachSocksProxy ()
	{
		std::lock_guard<std::mutex> l (m_ProxyMutex);
		auto proxy = m_SocksProxy;
		m_SocksProxy = nullptr;
		return proxy;
	}

	// Ordered client teardown. Every container is snapshotted and cleared under
	// its own lock, then the services are stopped outside the lock, because a
	// stop re-enters the registry (Done()/RemoveHandler). The order matters:
	// listeners and proxies first, then the protocol servers that own
	// destinations, then the address book and datagram tunnels, and only then
	// the destinations themselves. Reversing it would let an acceptor publish a
	// handler for a destination that is already gone.
	void ClientContext::Stop ()
	{
		auto httpProxy = DetachHttpProxy ();
		if (httpProxy)
		{
			LogPrint (eLogInfo, "Clients: Stopping HTTP Proxy");
			httpProxy->Stop ();
		}

		auto socksProxy = DetachSocksProxy ();
		if (socksProxy)
		{
			LogPrint (eLogInfo, "Clients: Stopping SOCKS Proxy");
			socksProxy->Stop ();
		}

		std::vector<std::pair<boost::asio::ip::tcp::endpoint, std::shared_ptr<I2PService>>> clientTunnels;
		std::vector<std::pair<std::pair<i2p::data::IdentHash, int>, std::shared_ptr<I2PServerTunnel>>> serverTunnels;
		{
			std::lock_guard<std::mutex> l (m_ProxyMutex);
			clientTunnels.assign (m_ClientTunnels.begin (), m_ClientTunnels.end ());
			serverTunnels.assign (m_ServerTunnels.begin (), m_ServerTunnels.end ());
			m_ClientTunnels.clear ();
			m_ServerTunnels.clear ();
		}
		for (auto & it : clientTunnels)
		{
			LogPrint (eLogInfo, "Clients: Stopping I2P client tunnel on port ", it.first);
			if (it.second) it.second->Stop ();
		}
		for (auto & it : serverTunnels)
		{
			LogPrint (eLogInfo, "Clients: Stopping I2P server tunnel");
			if (it.second) it.second->Stop ();
		}
		// A later Start() rebuilds the inventory from the configuration files.
		m_ServiceNames.Clear ();

		if (m_SamBridge)
		{
			LogPrint (eLogInfo, "Clients: Stopping SAM bridge");
			m_SamBridge->Stop ();
			m_SamBridge.reset ();
		}

		if (m_BOBCommandChannel)
		{
			LogPrint (eLogInfo, "Clients: Stopping BOB command channel");
			m_BOBCommandChannel->Stop ();
			m_BOBCommandChannel.reset ();
		}

		if (m_I2CPServer)
		{
			LogPrint (eLogInfo, "Clients: Stopping I2CP");
			m_I2CPServer->Stop ();
			m_I2CPServer.reset ();
		}

		LogPrint (eLogInfo, "Clients: Stopping AddressBook");
		m_AddressBook.Stop ();

		LogPrint (eLogInfo, "Clients: Stopping UDP Tunnels");
		std::vector<std::shared_ptr<I2PUDPClientTunnel>> clientForwards;
		std::vector<std::shared_ptr<I2PUDPServerTunnel>> serverForwards;
		{
			std::lock_guard<std::mutex> lock (m_ForwardsMutex);
			for (auto & it : m_ClientForwards)
				if (it.second) clientForwards.push_back (it.second);
			for (auto & it : m_ServerForwards)
				if (it.second) serverForwards.push_back (it.second);
			m_ClientForwards.clear ();
			m_ServerForwards.clear ();
		}
		for (const auto & forward : clientForwards) forward->Stop ();
		for (const auto & forward : serverForwards) forward->Stop ();

#ifndef NO_TORRENTS
		// stop torrents RPC servers
		for (const auto & server : GetTorrentsRPCServersSnapshot ()) server->Stop ();

		std::vector<std::shared_ptr<i2p::torrents::TorrentsTunnel>> torrentsTunnelsToStop;
		{
			std::lock_guard<std::mutex> l (m_TorrentsTunnelsMutex);
			torrentsTunnelsToStop.reserve (m_TorrentsTunnels.size ());
			for (const auto & it : m_TorrentsTunnels)
				if (it.second) torrentsTunnelsToStop.push_back (it.second);
			m_TorrentsTunnels.clear ();
		}
		for (const auto & torrentsTunnel : torrentsTunnelsToStop)
		{
			LogPrint (eLogInfo, "Clients: Stopping torrents tunnel");
			torrentsTunnel->Stop ();
		}
		if (!torrentsTunnelsToStop.empty ())
		{
			// let torrent tunnels finish
			LogPrint (eLogInfo, "Clients: Waiting for ", STOP_DESTINATIONS_TIMEOUT, "seconds");
			std::this_thread::sleep_for (std::chrono::seconds (STOP_DESTINATIONS_TIMEOUT));
		}
#endif

		// Snapshot and clear under the lock, then stop/join outside it.
		std::vector<std::shared_ptr<ClientDestination>> destinations;
		std::shared_ptr<ClientDestination> sharedLocalDestination;
		{
			LogPrint (eLogInfo, "Clients: Stopping Destinations");
			std::lock_guard<std::mutex> lock (m_DestinationsMutex);
			destinations.reserve (m_Destinations.size ());
			for (auto & it : m_Destinations)
				if (it.second) destinations.push_back (it.second);
			LogPrint (eLogInfo, "Clients: Stopping Destinations - Clear");
			m_Destinations.clear ();
			sharedLocalDestination = m_SharedLocalDestination;
			m_SharedLocalDestination = nullptr;
		}
		for (const auto & destination : destinations) destination->Stop ();

		LogPrint (eLogInfo, "Clients: Stopping SharedLocalDestination");
		if (sharedLocalDestination) sharedLocalDestination->Release ();
	}

	namespace
	{
		std::string FormatTcpEndpoint (const boost::asio::ip::tcp::endpoint & endpoint)
		{
			boost::system::error_code ec;
			auto address = endpoint.address ().to_string (ec);
			if (ec) address = "?";
			return address + ":" + std::to_string (endpoint.port ());
		}

		std::string JoinDestinations (const std::vector<std::string> & destinations)
		{
			std::string joined;
			for (const auto & destination : destinations)
			{
				if (!joined.empty ()) joined += ",";
				joined += destination;
			}
			return joined;
		}

		char ToLowerAscii (char ch)
		{
			return ch >= 'A' && ch <= 'Z' ? static_cast<char> (ch + ('a' - 'A')) : ch;
		}

		std::string ToLowerCopy (std::string_view text)
		{
			std::string lower;
			lower.reserve (text.size ());
			for (char ch : text) lower.push_back (ToLowerAscii (ch));
			return lower;
		}

		// Bounded decimal port parser for the dotted tunnel option keys.
		bool ParseLocalPort (std::string_view text, uint16_t & port)
		{
			if (text.empty () || text.size () > 5) return false;
			uint32_t value = 0;
			for (char ch : text)
			{
				if (ch < '0' || ch > '9') return false;
				value = value * 10 + static_cast<uint32_t> (ch - '0');
			}
			if (!value || value > 65535) return false;
			port = static_cast<uint16_t> (value);
			return true;
		}
	}

	std::string MakeTunnelServiceName (
		std::string_view configuredName, std::string_view sectionName, std::string_view type, std::string_view endpoint)
	{
		auto candidate = configuredName;
		if (candidate.empty ()) candidate = sectionName;
		if (candidate.empty ())
		{
			std::string fallback (type);
			fallback += "://";
			fallback += endpoint;
			return fallback;
		}
		return std::string (candidate);
	}

	bool IsValidTunnelServiceName (std::string_view name)
	{
		if (name.empty () || name.size () > I2P_TUNNEL_SERVICE_NAME_MAX_LENGTH) return false;
		for (char ch : name)
			if (static_cast<unsigned char> (ch) < 0x20 || ch == 0x7f) return false;
		return true;
	}

	bool ShouldStartTunnelOnLoad (bool enable, bool startOnLoad)
	{
		return enable && startOnLoad;
	}

	void TunnelServiceNameRegistry::Clear ()
	{
		std::lock_guard<std::mutex> lock (m_Mutex);
		m_Names.clear ();
	}

	bool TunnelServiceNameRegistry::Register (std::string_view name)
	{
		if (!IsValidTunnelServiceName (name)) return false;
		std::lock_guard<std::mutex> lock (m_Mutex);
		return m_Names.emplace (name).second;
	}

	size_t TunnelServiceNameRegistry::Size () const
	{
		std::lock_guard<std::mutex> lock (m_Mutex);
		return m_Names.size ();
	}

	bool TunnelServiceNameRegistry::Contains (std::string_view name) const
	{
		std::lock_guard<std::mutex> lock (m_Mutex);
		return m_Names.find (std::string (name)) != m_Names.end ();
	}

	std::vector<TunnelServiceInfo> ClientContext::GetTunnelServices () const
	{
		std::vector<TunnelServiceInfo> services;
		for (const auto & item : GetClientTunnelsSnapshot ())
		{
			const auto & service = item.second;
			if (!service) continue;
			TunnelServiceInfo info;
			info.name = service->GetName ();
			info.localEndpoint = FormatTcpEndpoint (item.first);
			info.running = service->IsRunning ();
			if (auto * acceptor = dynamic_cast<TCPIPAcceptor *> (service.get ()))
				info.localEndpoint = FormatTcpEndpoint (acceptor->GetLocalEndpoint ());
			if (auto * tunnel = dynamic_cast<I2PClientTunnel *> (service.get ()))
			{
				info.kind = "client";
				info.target = JoinDestinations (tunnel->GetDestinations ());
				info.destination = info.target;
			}
			else if (dynamic_cast<i2p::proxy::HTTPProxy *> (service.get ()))
				info.kind = "httpproxy";
			else if (dynamic_cast<i2p::proxy::SOCKSProxy *> (service.get ()))
				info.kind = "socksproxy";
			else
				info.kind = "client";
			services.push_back (std::move (info));
		}

		for (const auto & item : GetServerTunnelsSnapshot ())
		{
			const auto & tunnel = item.second;
			if (!tunnel) continue;
			TunnelServiceInfo info;
			info.name = tunnel->GetName ();
			info.localEndpoint = "i2p:" + std::to_string (tunnel->GetLocalPort ());
			info.target = tunnel->GetAddress () + ":" + std::to_string (tunnel->GetPort ());
			info.running = tunnel->IsRunning ();
			if (auto localDestination = tunnel->GetLocalDestination ())
				info.destination = localDestination->GetIdentHash ().ToBase32 ();
			if (dynamic_cast<I2PServerTunnelHTTP *> (tunnel.get ()))
				info.kind = "http";
			else if (dynamic_cast<I2PServerTunnelIRC *> (tunnel.get ()))
				info.kind = "irc";
			else
				info.kind = "server";
			services.push_back (std::move (info));
		}

		std::sort (services.begin (), services.end (),
			[] (const TunnelServiceInfo & first, const TunnelServiceInfo & second)
			{ return first.name < second.name; });
		return services;
	}

	bool ClientContext::StartTunnel (std::string_view name, std::string & error)
	{
		error.clear ();
		if (!IsValidTunnelServiceName (name))
		{
			error = "invalid tunnel name";
			return false;
		}
		std::shared_ptr<I2PService> service;
		for (const auto & item : GetClientTunnelsSnapshot ())
			if (item.second && name == item.second->GetName ())
			{
				service = item.second;
				break;
			}
		if (!service)
			for (const auto & item : GetServerTunnelsSnapshot ())
				if (item.second && name == item.second->GetName ())
				{
					service = item.second;
					break;
				}
		if (!service)
		{
			error = "unknown tunnel";
			return false;
		}
		// Idempotent: a running tunnel is left alone instead of binding twice.
		if (service->IsRunning ()) return true;
		try
		{
			service->Start ();
		}
		catch (const std::exception & ex)
		{
			error = ex.what ();
			return false;
		}
		if (!service->IsRunning ())
		{
			error = "tunnel did not start";
			return false;
		}
		return true;
	}

	bool ClientContext::StopTunnel (std::string_view name, std::string & error)
	{
		error.clear ();
		if (!IsValidTunnelServiceName (name))
		{
			error = "invalid tunnel name";
			return false;
		}
		std::shared_ptr<I2PService> service;
		for (const auto & item : GetClientTunnelsSnapshot ())
			if (item.second && name == item.second->GetName ())
			{
				service = item.second;
				break;
			}
		if (!service)
			for (const auto & item : GetServerTunnelsSnapshot ())
				if (item.second && name == item.second->GetName ())
				{
					service = item.second;
					break;
				}
		if (!service)
		{
			error = "unknown tunnel";
			return false;
		}
		if (!service->IsRunning ()) return true; // idempotent
		// An administrative stop closes the live connections it owns, but it
		// only detaches this tunnel's acceptor (never another tunnel's).
		try
		{
			if (auto server = std::dynamic_pointer_cast<I2PServerTunnel> (service))
				server->Stop (true);
			else
				service->Stop ();
		}
		catch (const std::exception & ex)
		{
			error = ex.what ();
			return false;
		}
		if (service->IsRunning ())
		{
			error = "tunnel did not stop";
			return false;
		}
		return true;
	}

	// Live reload protocol. The shared local destination is replaced first
	// because the address book timers and every service hold it; then the two
	// proxies are re-created. Server tunnels are handled in three passes:
	// detach every acceptor (incoming streams wait in the streaming queue
	// instead of being refused), reset the transient `isUpdated` flag, re-read
	// the tunnel files (a tunnel that still exists re-installs its own
	// acceptor and sets the flag again), and finally remove the tunnels that
	// nobody re-read. Destinations whose reference count dropped to zero are
	// collected and stopped last, so a tunnel removed in an earlier pass has
	// already released its destination.
	void ClientContext::ReloadConfig ()
	{
		// TODO: handle config changes
		/*std::string config; i2p::config::GetOption("conf", config);
		i2p::config::ParseConfig(config);*/

		// change shared local destination
		std::shared_ptr<ClientDestination> oldSharedLocalDestination;
		{
			std::lock_guard<std::mutex> l (m_DestinationsMutex);
			oldSharedLocalDestination = m_SharedLocalDestination;
			m_SharedLocalDestination = nullptr;
		}
		if (oldSharedLocalDestination) oldSharedLocalDestination->Release ();
		CreateNewSharedLocalDestination ();
		m_AddressBook.ResetTimers (); // its timers are left on a destination about to go

		// recreate HTTP proxy
		auto httpProxy = DetachHttpProxy ();
		if (httpProxy) httpProxy->Stop ();
		ReadHttpProxy ();

		// recreate SOCKS proxy
		auto socksProxy = DetachSocksProxy ();
		if (socksProxy) socksProxy->Stop ();
		ReadSocksProxy ();

		// handle tunnels
		// take the acceptors off first, a tunnel still in the config sets its own back
		// while it is read, incoming streams wait in the queue meanwhile
		std::vector<std::shared_ptr<I2PServerTunnel>> serverTunnels;
		{
			std::lock_guard<std::mutex> l (m_ProxyMutex);
			serverTunnels.reserve (m_ServerTunnels.size ());
			for (const auto & it : m_ServerTunnels)
				if (it.second) serverTunnels.push_back (it.second);
		}
		for (const auto & tunnel : serverTunnels) tunnel->DetachAcceptor ();
		// reset isUpdated for each tunnel
		VisitTunnels (false);
		// reload tunnels
		ReadTunnels ();
		// delete not updated tunnels (not in config anymore)
		VisitTunnels (true);

		// delete unused destinations: collect under the lock, stop outside it
		std::vector<std::shared_ptr<ClientDestination>> unusedDestinations;
		{
			std::lock_guard<std::mutex> l (m_DestinationsMutex);
			for (auto it = m_Destinations.begin (); it != m_Destinations.end ();)
			{
				auto dest = it->second;
				if (!dest || dest->GetRefCounter () > 0)
					++it; // skip
				else
				{
					unusedDestinations.push_back (dest);
					it = m_Destinations.erase (it);
				}
			}
		}
		for (const auto & destination : unusedDestinations) destination->Stop ();
	}

	bool ClientContext::LoadPrivateKeys (i2p::data::PrivateKeys & keys, std::string_view filename,
		i2p::data::SigningKeyType sigType, i2p::data::CryptoKeyType cryptoType)
	{
		if (filename.starts_with ("transient"))
		{
			keys = i2p::data::PrivateKeys::CreateRandomKeys (sigType, cryptoType, true);
			LogPrint (eLogInfo, "Clients: New transient keys address ",
				m_AddressBook.ToAddress (keys.GetPublic ()->GetIdentHash ()), " created");
			return true;
		}

		bool success = true;
		std::string fullPath = i2p::fs::DataDirPath (filename);
		std::ifstream s (fullPath, std::ifstream::binary);
		if (s.is_open ())
		{
			s.seekg (0, std::ios::end);
			const auto end = s.tellg ();
			constexpr std::streamoff MAX_KEYS_FILE_SIZE = 64 * 1024;
			if (end <= 0 || static_cast<std::streamoff> (end) > MAX_KEYS_FILE_SIZE)
			{
				LogPrint (eLogCritical, "Clients: Invalid keyfile size ", filename);
				return false;
			}
			const size_t len = static_cast<size_t> (end);
			s.seekg (0, std::ios::beg);
			std::vector<uint8_t> buf (len);
			s.read ((char *)buf.data (), static_cast<std::streamsize> (len));
			if (!s || static_cast<size_t> (s.gcount ()) != len)
			{
				LogPrint (eLogCritical, "Clients: Truncated keyfile ", filename);
				return false;
			}
			if (!keys.FromBuffer (buf.data (), len))
			{
				LogPrint (eLogCritical, "Clients: Failed to load keyfile ", filename);
				success = false;
			}
			else
				LogPrint (eLogInfo, "Clients: Local address ",
					m_AddressBook.ToAddress (keys.GetPublic ()->GetIdentHash ()), " loaded");
		}
		else
		{
			LogPrint (eLogInfo, "Clients: Can't open file ", fullPath, " Creating new one with signature type ",
				sigType, " crypto type ", cryptoType);
			keys = i2p::data::PrivateKeys::CreateRandomKeys (sigType, cryptoType, true);
			std::ofstream f (i2p::fs::CreatePrivateFile (fullPath), std::ofstream::binary | std::ofstream::out);
			size_t len = keys.GetFullLen ();
			std::vector<uint8_t> buf (len);
			len = keys.ToBuffer (buf.data (), len);
			f.write ((char *)buf.data (), len);

			LogPrint (eLogInfo, "Clients: New private keys file ", fullPath, " for ",
				m_AddressBook.ToAddress (keys.GetPublic ()->GetIdentHash ()), " created");
		}
		return success;
	}

	std::vector<std::shared_ptr<DatagramSessionInfo>> ClientContext::GetForwardInfosFor (
		const i2p::data::IdentHash & destination)
	{
		std::vector<std::shared_ptr<DatagramSessionInfo>> infos;
		std::lock_guard<std::mutex> lock (m_ForwardsMutex);
		for (const auto & c : m_ClientForwards)
		{
			if (c.second->IsLocalDestination (destination))
			{
				for (auto & i : c.second->GetSessions ()) infos.push_back (i);
				break;
			}
		}
		for (const auto & s : m_ServerForwards)
		{
			if (std::get<0> (s.first) == destination)
			{
				for (auto & i : s.second->GetSessions ()) infos.push_back (i);
				break;
			}
		}
		return infos;
	}

	std::shared_ptr<ClientDestination> ClientContext::CreateNewLocalDestination (bool isPublic,
		i2p::data::SigningKeyType sigType, i2p::data::CryptoKeyType cryptoType, const i2p::util::Mapping * params)
	{
		i2p::data::PrivateKeys keys = i2p::data::PrivateKeys::CreateRandomKeys (sigType, cryptoType, true);
		auto localDestination = std::make_shared<RunnableClientDestination> (keys, isPublic, params);
		AddLocalDestination (localDestination);
		return localDestination;
	}

	std::shared_ptr<ClientDestination> ClientContext::CreateNewLocalDestination (boost::asio::io_context & service,
		bool isPublic, i2p::data::SigningKeyType sigType, i2p::data::CryptoKeyType cryptoType,
		const i2p::util::Mapping * params)
	{
		i2p::data::PrivateKeys keys = i2p::data::PrivateKeys::CreateRandomKeys (sigType, cryptoType, true);
		auto localDestination = std::make_shared<ClientDestination> (service, keys, isPublic, params);
		AddLocalDestination (localDestination);
		return localDestination;
	}

	std::shared_ptr<ClientDestination> ClientContext::CreateNewMatchedTunnelDestination (
		const i2p::data::PrivateKeys & keys, const std::string & name, const i2p::util::Mapping * params)
	{
		auto localDestination = std::make_shared<MatchedTunnelDestination> (keys, name, params);
		AddLocalDestination (localDestination);
		return localDestination;
	}

	void ClientContext::AddLocalDestination (std::shared_ptr<ClientDestination> localDestination)
	{
		bool added = false;
		{
			std::unique_lock<std::mutex> l (m_DestinationsMutex);
			added = m_Destinations.emplace (localDestination->GetIdentHash (), localDestination).second;
		}
		if (added) localDestination->Start ();
	}

	void ClientContext::DeleteLocalDestination (std::shared_ptr<ClientDestination> destination)
	{
		if (!destination) return;
		bool removed = false;
		{
			std::unique_lock<std::mutex> l (m_DestinationsMutex);
			auto it = m_Destinations.find (destination->GetIdentHash ());
			if (it != m_Destinations.end () && it->second == destination)
			{
				m_Destinations.erase (it);
				removed = true;
			}
			else
			{
				// The registry key may have been replaced without mutating
				// the destination object (ReplaceLocalDestinationHash). Remove
				// by object identity so the replaced key cannot be orphaned.
				for (it = m_Destinations.begin (); it != m_Destinations.end (); ++it)
					if (it->second == destination)
					{
						m_Destinations.erase (it);
						removed = true;
						break;
					}
			}
		}
		if (removed) destination->Stop ();
	}

	bool ClientContext::ReplaceLocalDestinationHash (
		const i2p::data::IdentHash & oldIdentHash, const i2p::data::IdentHash & newIdentHash)
	{
		std::unique_lock<std::mutex> l (m_DestinationsMutex);
		auto it = m_Destinations.find (oldIdentHash);
		if (it == m_Destinations.end ()) return false;
		auto dest = it->second;
		if (!dest) return false;
		m_Destinations.erase (it);
		m_Destinations.emplace (newIdentHash, dest);
		return true;
	}

	std::shared_ptr<ClientDestination> ClientContext::CreateNewLocalDestination (
		const i2p::data::PrivateKeys & keys, bool isPublic, const i2p::util::Mapping * params)
	{
		std::shared_ptr<ClientDestination> existing;
		{
			std::lock_guard<std::mutex> l (m_DestinationsMutex);
			auto it = m_Destinations.find (keys.GetPublic ()->GetIdentHash ());
			if (it != m_Destinations.end ()) existing = it->second;
		}
		if (existing)
		{
			LogPrint (eLogWarning, "Clients: Local destination ",
				m_AddressBook.ToAddress (keys.GetPublic ()->GetIdentHash ()), " exists");
			if (keys.IsOfflineSignature ()) existing->UpdateOfflineSignature (keys);
			existing->Start (); // make sure to start
			return existing;
		}
		auto localDestination = std::make_shared<RunnableClientDestination> (keys, isPublic, params);
		AddLocalDestination (localDestination);
		return localDestination;
	}

	std::shared_ptr<ClientDestination> ClientContext::CreateNewLocalDestination (boost::asio::io_context & service,
		const i2p::data::PrivateKeys & keys, bool isPublic, const i2p::util::Mapping * params)
	{
		std::shared_ptr<ClientDestination> existing;
		{
			std::lock_guard<std::mutex> l (m_DestinationsMutex);
			auto it = m_Destinations.find (keys.GetPublic ()->GetIdentHash ());
			if (it != m_Destinations.end ()) existing = it->second;
		}
		if (existing)
		{
			LogPrint (eLogWarning, "Clients: Local destination ",
				m_AddressBook.ToAddress (keys.GetPublic ()->GetIdentHash ()), " exists");
			if (keys.IsOfflineSignature ()) existing->UpdateOfflineSignature (keys);
			existing->Start (); // make sure to start
			return existing;
		}
		auto localDestination = std::make_shared<ClientDestination> (service, keys, isPublic, params);
		AddLocalDestination (localDestination);
		return localDestination;
	}

	void ClientContext::CreateNewSharedLocalDestination ()
	{
		i2p::util::Mapping params;
		ReadI2CPOptionsFromConfig ("shareddest.", params);
		params.Insert (I2CP_PARAM_OUTBOUND_NICKNAME, "SharedDest");

		auto localDestination = CreateNewLocalDestination (false, i2p::data::SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519,
			i2p::data::CRYPTO_KEY_TYPE_ELGAMAL,
			&params); // non-public, EDDSA
		{
			std::lock_guard<std::mutex> l (m_DestinationsMutex);
			m_SharedLocalDestination = localDestination;
		}
		localDestination->Acquire ();
	}

	std::shared_ptr<ClientDestination> ClientContext::FindLocalDestination (
		const i2p::data::IdentHash & destination) const
	{
		std::lock_guard<std::mutex> l (m_DestinationsMutex);
		auto it = m_Destinations.find (destination);
		if (it != m_Destinations.end ()) return it->second;
		return nullptr;
	}

	template <typename Section, typename Type>
	std::string ClientContext::GetI2CPOption (
		const Section & section, const std::string & name, const Type & value) const
	{
		return section.second.get (boost::property_tree::ptree::path_type (name, '/'), std::to_string (value));
	}

	template <typename Section>
	std::string ClientContext::GetI2CPStringOption (
		const Section & section, const std::string & name, const std::string & value) const
	{
		return section.second.get (boost::property_tree::ptree::path_type (name, '/'), value);
	}

	template <typename Section>
	void ClientContext::ReadI2CPOptionsGroup (
		const Section & section, const std::string & group, i2p::util::Mapping & options) const
	{
		for (auto it : section.second)
		{
			if (it.first.length () >= group.length () && !it.first.compare (0, group.length (), group))
				options.Insert (it.first, it.second.get_value (""));
		}
	}

	template <typename Section>
	void ClientContext::ReadI2CPOptions (const Section & section, bool isServer, i2p::util::Mapping & options) const
	{
		options.Insert (I2CP_PARAM_INBOUND_TUNNEL_LENGTH,
			GetI2CPOption (section, I2CP_PARAM_INBOUND_TUNNEL_LENGTH, DEFAULT_INBOUND_TUNNEL_LENGTH));
		options.Insert (I2CP_PARAM_OUTBOUND_TUNNEL_LENGTH,
			GetI2CPOption (section, I2CP_PARAM_OUTBOUND_TUNNEL_LENGTH, DEFAULT_OUTBOUND_TUNNEL_LENGTH));
		options.Insert (I2CP_PARAM_INBOUND_TUNNELS_QUANTITY,
			GetI2CPOption (section, I2CP_PARAM_INBOUND_TUNNELS_QUANTITY, DEFAULT_INBOUND_TUNNELS_QUANTITY));
		options.Insert (I2CP_PARAM_OUTBOUND_TUNNELS_QUANTITY,
			GetI2CPOption (section, I2CP_PARAM_OUTBOUND_TUNNELS_QUANTITY, DEFAULT_OUTBOUND_TUNNELS_QUANTITY));
		options.Insert (I2CP_PARAM_INBOUND_TUNNELS_LENGTH_VARIANCE,
			GetI2CPOption (
				section, I2CP_PARAM_INBOUND_TUNNELS_LENGTH_VARIANCE, DEFAULT_INBOUND_TUNNELS_LENGTH_VARIANCE));
		options.Insert (I2CP_PARAM_OUTBOUND_TUNNELS_LENGTH_VARIANCE,
			GetI2CPOption (
				section, I2CP_PARAM_OUTBOUND_TUNNELS_LENGTH_VARIANCE, DEFAULT_OUTBOUND_TUNNELS_LENGTH_VARIANCE));
		options.Insert (
			I2CP_PARAM_TAGS_TO_SEND, GetI2CPOption (section, I2CP_PARAM_TAGS_TO_SEND, DEFAULT_TAGS_TO_SEND));
		options.Insert (I2CP_PARAM_MIN_TUNNEL_LATENCY,
			GetI2CPOption (section, I2CP_PARAM_MIN_TUNNEL_LATENCY, DEFAULT_MIN_TUNNEL_LATENCY));
		options.Insert (I2CP_PARAM_MAX_TUNNEL_LATENCY,
			GetI2CPOption (section, I2CP_PARAM_MAX_TUNNEL_LATENCY, DEFAULT_MAX_TUNNEL_LATENCY));
		options.Insert (I2CP_PARAM_STREAMING_INITIAL_ACK_DELAY,
			GetI2CPOption (section, I2CP_PARAM_STREAMING_INITIAL_ACK_DELAY, DEFAULT_INITIAL_ACK_DELAY));
		options.Insert (I2CP_PARAM_STREAMING_MAX_OUTBOUND_SPEED,
			GetI2CPOption (section, I2CP_PARAM_STREAMING_MAX_OUTBOUND_SPEED, DEFAULT_MAX_OUTBOUND_SPEED));
		options.Insert (I2CP_PARAM_STREAMING_MAX_INBOUND_SPEED,
			GetI2CPOption (section, I2CP_PARAM_STREAMING_MAX_INBOUND_SPEED, DEFAULT_MAX_INBOUND_SPEED));
		options.Insert (I2CP_PARAM_STREAMING_MAX_CONCURRENT_STREAMS,
			GetI2CPOption (section, I2CP_PARAM_STREAMING_MAX_CONCURRENT_STREAMS, DEFAULT_MAX_CONCURRENT_STREAMS));
		options.Insert (I2CP_PARAM_STREAMING_MAX_CONNS_PER_MINUTE,
			GetI2CPOption (section, I2CP_PARAM_STREAMING_MAX_CONNS_PER_MINUTE, DEFAULT_MAX_CONNS_PER_MINUTE));
		options.Insert (I2CP_PARAM_STREAMING_MAX_CONNS_PER_HOUR,
			GetI2CPOption (section, I2CP_PARAM_STREAMING_MAX_CONNS_PER_HOUR, DEFAULT_MAX_CONNS_PER_HOUR));
		options.Insert (I2CP_PARAM_STREAMING_MAX_CONNS_PER_DAY,
			GetI2CPOption (section, I2CP_PARAM_STREAMING_MAX_CONNS_PER_DAY, DEFAULT_MAX_CONNS_PER_DAY));
		options.Insert (I2CP_PARAM_STREAMING_MAX_TOTAL_CONNS_PER_MINUTE,
			GetI2CPOption (
				section, I2CP_PARAM_STREAMING_MAX_TOTAL_CONNS_PER_MINUTE, DEFAULT_MAX_TOTAL_CONNS_PER_MINUTE));
		options.Insert (I2CP_PARAM_STREAMING_MAX_TOTAL_CONNS_PER_HOUR,
			GetI2CPOption (section, I2CP_PARAM_STREAMING_MAX_TOTAL_CONNS_PER_HOUR, DEFAULT_MAX_TOTAL_CONNS_PER_HOUR));
		options.Insert (I2CP_PARAM_STREAMING_MAX_TOTAL_CONNS_PER_DAY,
			GetI2CPOption (section, I2CP_PARAM_STREAMING_MAX_TOTAL_CONNS_PER_DAY, DEFAULT_MAX_TOTAL_CONNS_PER_DAY));
		options.Insert (I2CP_PARAM_STREAMING_ANSWER_PINGS,
			GetI2CPOption (section, I2CP_PARAM_STREAMING_ANSWER_PINGS, isServer ? DEFAULT_ANSWER_PINGS : false));
		options.Insert (
			I2CP_PARAM_STREAMING_DONT_SIGN, GetI2CPOption (section, I2CP_PARAM_STREAMING_DONT_SIGN, DEFAULT_DONT_SIGN));
		options.Insert (I2CP_PARAM_STREAMING_PROFILE,
			GetI2CPOption (section, I2CP_PARAM_STREAMING_PROFILE, DEFAULT_STREAMING_PROFILE));
		options.Insert (I2CP_PARAM_STREAMING_MAX_WINDOW_SIZE,
			GetI2CPOption (section, I2CP_PARAM_STREAMING_MAX_WINDOW_SIZE, i2p::stream::MAX_WINDOW_SIZE));
		options.Insert (
			I2CP_PARAM_LEASESET_TYPE, GetI2CPOption (section, I2CP_PARAM_LEASESET_TYPE, DEFAULT_LEASESET_TYPE));
		options.Insert (I2CP_PARAM_STREAMING_MAX_RESENDS,
			GetI2CPOption (section, I2CP_PARAM_STREAMING_MAX_RESENDS, i2p::stream::MAX_NUM_RESEND_ATTEMPTS));
#if OPENSSL_PQ
		// Hybrid + X25519. ElGamal is deliberately not combined with a hybrid
		// type: the strict parser rejects that combination (same policy as Java).
		std::string encType = GetI2CPStringOption (section, I2CP_PARAM_LEASESET_ENCRYPTION_TYPE, "6,4");
#else
		std::string encType
			= GetI2CPStringOption (section, I2CP_PARAM_LEASESET_ENCRYPTION_TYPE, isServer ? "4" : "4,0");
#endif
		if (encType.length () > 0) options.Insert (I2CP_PARAM_LEASESET_ENCRYPTION_TYPE, encType);
		std::string privKey = GetI2CPStringOption (section, I2CP_PARAM_LEASESET_PRIV_KEY, "");
		if (privKey.length () > 0) options.Insert (I2CP_PARAM_LEASESET_PRIV_KEY, privKey);
		auto authType = GetI2CPOption (section, I2CP_PARAM_LEASESET_AUTH_TYPE, 0);
		if (authType != "0") // auth is set
		{
			options.Insert (I2CP_PARAM_LEASESET_AUTH_TYPE, authType);
			if (authType == "1") // DH
				ReadI2CPOptionsGroup (section, I2CP_PARAM_LEASESET_CLIENT_DH, options);
			else if (authType == "2") // PSK
				ReadI2CPOptionsGroup (section, I2CP_PARAM_LEASESET_CLIENT_PSK, options);
		}
		std::string explicitPeers = GetI2CPStringOption (section, I2CP_PARAM_EXPLICIT_PEERS, "");
		if (explicitPeers.length () > 0) options.Insert (I2CP_PARAM_EXPLICIT_PEERS, explicitPeers);
		std::string trustedRouters = GetI2CPStringOption (section, I2CP_PARAM_TRUSTED_ROUTERS, "");
		if (trustedRouters.length () > 0) options.Insert (I2CP_PARAM_TRUSTED_ROUTERS, trustedRouters);
		std::string ratchetInboundTags = GetI2CPStringOption (section, I2CP_PARAM_RATCHET_INBOUND_TAGS, "");
		if (ratchetInboundTags.length () > 0) options.Insert (I2CP_PARAM_RATCHET_INBOUND_TAGS, ratchetInboundTags);
		std::string inboundRandomKey = GetI2CPStringOption (section, I2CP_PARAM_INBOUND_RANDOM_KEY, "");
		if (inboundRandomKey.length () > 0) options.Insert (I2CP_PARAM_INBOUND_RANDOM_KEY, inboundRandomKey);
		std::string outboundRandomKey = GetI2CPStringOption (section, I2CP_PARAM_OUTBOUND_RANDOM_KEY, "");
		if (outboundRandomKey.length () > 0) options.Insert (I2CP_PARAM_OUTBOUND_RANDOM_KEY, outboundRandomKey);
	}

	void ClientContext::ReadI2CPOptionsFromConfig (const std::string & prefix, i2p::util::Mapping & options) const
	{
		std::string value;
		if (i2p::config::GetOption (prefix + I2CP_PARAM_INBOUND_TUNNEL_LENGTH, value))
			options.Insert (I2CP_PARAM_INBOUND_TUNNEL_LENGTH, value);
		if (i2p::config::GetOption (prefix + I2CP_PARAM_INBOUND_TUNNELS_QUANTITY, value))
			options.Insert (I2CP_PARAM_INBOUND_TUNNELS_QUANTITY, value);
		if (i2p::config::GetOption (prefix + I2CP_PARAM_INBOUND_TUNNELS_LENGTH_VARIANCE, value))
			options.Insert (I2CP_PARAM_INBOUND_TUNNELS_LENGTH_VARIANCE, value);
		if (i2p::config::GetOption (prefix + I2CP_PARAM_OUTBOUND_TUNNEL_LENGTH, value))
			options.Insert (I2CP_PARAM_OUTBOUND_TUNNEL_LENGTH, value);
		if (i2p::config::GetOption (prefix + I2CP_PARAM_OUTBOUND_TUNNELS_QUANTITY, value))
			options.Insert (I2CP_PARAM_OUTBOUND_TUNNELS_QUANTITY, value);
		if (i2p::config::GetOption (prefix + I2CP_PARAM_OUTBOUND_TUNNELS_LENGTH_VARIANCE, value))
			options.Insert (I2CP_PARAM_OUTBOUND_TUNNELS_LENGTH_VARIANCE, value);
		if (i2p::config::GetOption (prefix + I2CP_PARAM_MIN_TUNNEL_LATENCY, value))
			options.Insert (I2CP_PARAM_MIN_TUNNEL_LATENCY, value);
		if (i2p::config::GetOption (prefix + I2CP_PARAM_MAX_TUNNEL_LATENCY, value))
			options.Insert (I2CP_PARAM_MAX_TUNNEL_LATENCY, value);
		if (i2p::config::GetOption (prefix + I2CP_PARAM_LEASESET_TYPE, value))
			options.Insert (I2CP_PARAM_LEASESET_TYPE, value);
		if (i2p::config::GetOption (prefix + I2CP_PARAM_LEASESET_ENCRYPTION_TYPE, value))
			options.Insert (I2CP_PARAM_LEASESET_ENCRYPTION_TYPE, value);
		if (i2p::config::GetOption (prefix + I2CP_PARAM_LEASESET_PRIV_KEY, value) && !value.empty ())
			options.Insert (I2CP_PARAM_LEASESET_PRIV_KEY, value);
		if (i2p::config::GetOption (prefix + I2CP_PARAM_STREAMING_PROFILE, value))
			options.Insert (I2CP_PARAM_STREAMING_PROFILE, value);
		if (i2p::config::GetOption (prefix + I2CP_PARAM_STREAMING_MAX_WINDOW_SIZE, value))
			options.Insert (I2CP_PARAM_STREAMING_MAX_WINDOW_SIZE, value);
		if (i2p::config::GetOption (prefix + I2CP_PARAM_STREAMING_MAX_RESENDS, value))
			options.Insert (I2CP_PARAM_STREAMING_MAX_RESENDS, value);
	}

	static size_t GetUDPTunnelMaxWindow (const boost::property_tree::ptree & section)
	{
		size_t w = section.get<size_t> (UDP_TUNNEL_MAX_WINDOW, I2P_UDP_DEFAULT_MAX_NUM_UNACKED_DATAGRAMS);
		if (w < I2P_UDP_MIN_MAX_NUM_UNACKED_DATAGRAMS) w = I2P_UDP_MIN_MAX_NUM_UNACKED_DATAGRAMS;
		if (w > I2P_UDP_MAX_NUM_UNACKED_DATAGRAMS) w = I2P_UDP_MAX_NUM_UNACKED_DATAGRAMS;
		return w;
	}

	// Reads every tunnel section from the main tunnel file and from
	// `tunnels.d/*.conf`, in that order. Each section must claim a unique
	// administrative name before any object is created, so a duplicate is a
	// configuration error rather than an ambiguous StartTunnel/StopTunnel
	// target; the registry is cleared first, which is what releases the names of
	// sections that disappeared.
	void ClientContext::ReadTunnels ()
	{
		int numClientTunnels = 0, numServerTunnels = 0;
		// Every section of every file must claim a unique administrative name,
		// so a removed/renamed tunnel releases its name on reload.
		m_ServiceNames.Clear ();
		std::string tunConf;
		i2p::config::GetOption ("tunconf", tunConf);
		if (tunConf.empty ()) tunConf = i2p::fs::DataDirPath ("tunnels.conf");

		if (i2p::fs::Exists (tunConf))
		{
			LogPrint (eLogDebug, "Clients: Tunnels config file: ", tunConf);
			ReadTunnels (tunConf, numClientTunnels, numServerTunnels);
		}

		std::string tunDir;
		i2p::config::GetOption ("tunnelsdir", tunDir);
		if (tunDir.empty ()) tunDir = i2p::fs::DataDirPath ("tunnels.d");

		if (i2p::fs::Exists (tunDir))
		{
			std::vector<std::string> files;
			if (i2p::fs::ReadDir (tunDir, files))
			{
				for (auto & it : files)
				{
					if (!it.ends_with (".conf")) continue;
					LogPrint (eLogDebug, "Clients: Tunnels extra config file: ", it);
					ReadTunnels (it, numClientTunnels, numServerTunnels);
				}
			}
		}

		LogPrint (eLogInfo, "Clients: ", numClientTunnels, " I2P client tunnels created");
		LogPrint (eLogInfo, "Clients: ", numServerTunnels, " I2P server tunnels created");
	}

	// Append one INI section that was recovered from a file the parser rejected
	// as a whole. A single malformed line makes read_ini throw and would drop
	// every tunnel in the file, including the correct sections, so the recovery
	// path parses each section independently and skips only the ones that are
	// still unreadable.
	void ClientContext::AddSection (
		boost::property_tree::ptree & pt, const std::string & name, const std::string & chunk, int & numSections)
	{
		if (chunk.empty ()) return;
		boost::property_tree::ptree section;
		try
		{
			std::stringstream ss (chunk);
			boost::property_tree::read_ini (ss, section);
		}
		catch (std::exception & ex)
		{
			LogPrint (
				eLogWarning, "Clients: Skipped ", name.empty () ? "tunnels config header" : name, ": ", ex.what ());
			return;
		}
		for (auto & it : section) pt.push_back (it);
		numSections++;
	}

	// Fallback for a file rejected as a whole: a single malformed line makes read_ini throw
	// and every tunnel is lost, including sections that are correct. Read section by section
	// instead, so that only unreadable ones are skipped
	int ClientContext::ReadTunnelsBySection (const std::string & tunConf, boost::property_tree::ptree & pt)
	{
		std::ifstream f (tunConf);
		if (!f.is_open ()) return 0;
		int numSections = 0;
		std::string line, chunk, name;
		while (std::getline (f, line))
		{
			auto start = line.find_first_not_of (" \t");
			if (start != std::string::npos && line[start] == '[')
			{
				AddSection (pt, name, chunk, numSections);
				chunk.clear ();
				name = line.substr (start);
			}
			chunk += line;
			chunk += "\n";
		}
		AddSection (pt, name, chunk, numSections);
		return numSections;
	}

	void ClientContext::ReadTunnels (const std::string & tunConf, int & numClientTunnels, int & numServerTunnels)
	{
		boost::property_tree::ptree pt;
		try
		{
			boost::property_tree::read_ini (tunConf, pt);
		}
		catch (std::exception & ex)
		{
			LogPrint (eLogWarning, "Clients: Can't read ", tunConf, ": ", ex.what ());
			pt.clear ();
			int numSections = ReadTunnelsBySection (tunConf, pt);
			if (!numSections) return;
			LogPrint (
				eLogWarning, "Clients: ", tunConf, " read section by section, ", numSections, " section(s) accepted");
		}

		std::map<std::string, std::shared_ptr<ClientDestination>> destinations; // keys -> destination
		for (auto & section : pt)
		{
			std::string name = section.first;
			try
			{
				std::string type = section.second.get<std::string> (I2P_TUNNELS_SECTION_TYPE);
				const std::string configuredName = section.second.get<std::string> (I2P_TUNNEL_NAME, "");
				bool enable = ShouldStartTunnelOnLoad (
					section.second.get (I2P_TUNNEL_ENABLE, true), section.second.get (I2P_TUNNEL_START_ON_LOAD, true));
				// Claim the administrative name once the endpoint is known. A
				// duplicate is a configuration error, not a silent alias, so the
				// typed StartTunnel/StopTunnel verbs stay unambiguous.
				auto claimName = [&] (const std::string & endpoint) -> bool
				{
					name = MakeTunnelServiceName (configuredName, section.first, type, endpoint);
					if (m_ServiceNames.Register (name)) return true;
					LogPrint (eLogError, "Clients: Duplicate tunnel service name ", name, " in section ", section.first,
						"; the section is skipped");
					return false;
				};
				if (type == I2P_TUNNELS_SECTION_TYPE_CLIENT || type == I2P_TUNNELS_SECTION_TYPE_SOCKS
					|| type == I2P_TUNNELS_SECTION_TYPE_WEBSOCKS || type == I2P_TUNNELS_SECTION_TYPE_HTTPPROXY
					|| type == I2P_TUNNELS_SECTION_TYPE_UDPCLIENT)
				{
					// mandatory params
					std::string dest;
					if (type == I2P_TUNNELS_SECTION_TYPE_CLIENT || type == I2P_TUNNELS_SECTION_TYPE_UDPCLIENT)
						dest = section.second.get<std::string> (I2P_CLIENT_TUNNEL_DESTINATION);
					uint16_t port = section.second.get<uint16_t> (I2P_CLIENT_TUNNEL_PORT);
					// optional params
					bool matchTunnels = section.second.get (I2P_CLIENT_TUNNEL_MATCH_TUNNELS, false);
					std::string keys = section.second.get<std::string> (I2P_CLIENT_TUNNEL_KEYS, "transient");
					std::string address = section.second.get<std::string> (I2P_CLIENT_TUNNEL_ADDRESS, "127.0.0.1");
					uint16_t destinationPort = section.second.get<uint16_t> (I2P_CLIENT_TUNNEL_DESTINATION_PORT, 0);
					if (!claimName (address + ":" + std::to_string (port))) continue;
					i2p::data::SigningKeyType sigType = section.second.get (
						I2P_CLIENT_TUNNEL_SIGNATURE_TYPE, i2p::data::SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519);
					if (sigType > i2p::data::SIGNING_KEY_TYPE_REDDSA_SHA512_ED25519)
						sigType = i2p::data::SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519;
					i2p::data::CryptoKeyType cryptoType
						= section.second.get (I2P_CLIENT_TUNNEL_CRYPTO_TYPE, i2p::data::CRYPTO_KEY_TYPE_ELGAMAL);
					// I2CP
					i2p::util::Mapping options;
					ReadI2CPOptions (section, false, options);

					// Set I2CP name if not set
					if (!options.Contains (I2CP_PARAM_OUTBOUND_NICKNAME))
						options.Insert (I2CP_PARAM_OUTBOUND_NICKNAME, name);

					std::shared_ptr<ClientDestination> localDestination = nullptr;
					if (keys == "shareddest")
						localDestination = GetSharedLocalDestination ();
					else if (keys.length () > 0)
					{
						auto it = destinations.find (keys);
						if (it != destinations.end ())
							localDestination = it->second;
						else
						{
							i2p::data::PrivateKeys k;
							if (LoadPrivateKeys (k, keys, sigType, cryptoType))
							{
								localDestination = FindLocalDestination (k.GetPublic ()->GetIdentHash ());
								if (!localDestination)
								{
									if (matchTunnels)
										localDestination = CreateNewMatchedTunnelDestination (k, dest, &options);
									else
										localDestination = CreateNewLocalDestination (
											k, type == I2P_TUNNELS_SECTION_TYPE_UDPCLIENT, &options);
									if (keys != "transient") destinations[keys] = localDestination;
								}
								else
								{
									// destination is already running, the pool takes new parameters
									// on the fly, no need to recreate anything
									localDestination->Reconfigure (options);
									if (keys != "transient") destinations[keys] = localDestination;
								}
							}
						}
					}

					if (type == I2P_TUNNELS_SECTION_TYPE_UDPCLIENT)
					{
						// udp client
						// TODO: hostnames
						boost::asio::ip::udp::endpoint end (boost::asio::ip::make_address (address), port);
						if (!localDestination) localDestination = GetSharedLocalDestination ();

						bool gzip = section.second.get (I2P_CLIENT_TUNNEL_GZIP, true);
						int datagramVersion = (i2p::datagram::DatagramVersion)section.second.get (
							UDP_CLIENT_TUNNEL_DATAGRAM_VERSION, (int)i2p::datagram::eDatagramV3);
						auto clientTunnel = std::make_shared<I2PUDPClientTunnel> (name, dest, end, localDestination,
							destinationPort, gzip, (i2p::datagram::DatagramVersion)datagramVersion);
						clientTunnel->SetMaxWindow (GetUDPTunnelMaxWindow (section.second));

						uint32_t keepAlive = section.second.get<uint32_t> (I2P_CLIENT_TUNNEL_KEEP_ALIVE_INTERVAL, 0);
						if (keepAlive)
						{
							clientTunnel->SetKeepAliveInterval (keepAlive);
							LogPrint (eLogInfo, "Clients: UDP Client tunnel keep alive interval set to ", keepAlive);
						}

						bool inserted = false;
						std::shared_ptr<I2PUDPClientTunnel> existing;
						{
							std::lock_guard<std::mutex> lock (m_ForwardsMutex);
							auto ins = m_ClientForwards.insert (std::make_pair (end, clientTunnel));
							inserted = ins.second;
							if (!inserted)
							{
								existing = ins.first->second;
								if (existing) existing->isUpdated = true;
							}
						}
						if (inserted)
						{
							if (enable)
							{
								clientTunnel->Start ();
								numClientTunnels++;
							}
							else
								LogPrint (
									eLogInfo, "Clients: I2P UDP client tunnel ", name, " is disabled (enable=false)");
						}
						else if (existing)
						{
							// TODO: update
							if (!enable)
							{
								LogPrint (eLogInfo, "Clients: I2P UDP client tunnel ", name,
									" is disabled (enable=false); the running forward is left unchanged");
							}
							else if (existing->GetLocalDestination () != clientTunnel->GetLocalDestination ())
							{
								LogPrint (eLogInfo, "Clients: I2P UDP client tunnel destination updated");
								existing->Stop ();
								existing->SetLocalDestination (clientTunnel->GetLocalDestination ());
								existing->Start ();
							}
							LogPrint (eLogError, "Clients: I2P Client forward for endpoint ", end, " already exists");
						}
					}
					else
					{
						boost::asio::ip::tcp::endpoint clientEndpoint;
						std::shared_ptr<I2PService> clientTunnel;
						if (type == I2P_TUNNELS_SECTION_TYPE_SOCKS)
						{
							// socks proxy
							std::string outproxy = section.second.get ("outproxy", "");
							auto tun = std::make_shared<i2p::proxy::SOCKSProxy> (
								name, address, port, !outproxy.empty (), outproxy, destinationPort, localDestination);
							if (section.second.get<bool> ("auth.enabled", false))
								tun->SetAuth (section.second.get<std::string> ("auth.username", ""),
									section.second.get<std::string> ("auth.password", ""));
							clientTunnel = tun;
							clientEndpoint = tun->GetLocalEndpoint ();
						}
						else if (type == I2P_TUNNELS_SECTION_TYPE_HTTPPROXY)
						{
							// http proxy
							std::string outproxy = section.second.get ("outproxy", "");
							bool addresshelper = section.second.get ("addresshelper", true);
							bool senduseragent = section.second.get ("senduseragent", false);
							auto tun = std::make_shared<i2p::proxy::HTTPProxy> (
								name, address, port, outproxy, addresshelper, senduseragent, localDestination);
							if (section.second.get<bool> ("auth.enabled", false))
								tun->SetBasicAuth (section.second.get<std::string> ("auth.username", ""),
									section.second.get<std::string> ("auth.password", ""));
							clientTunnel = tun;
							clientEndpoint = tun->GetLocalEndpoint ();
						}
						else if (type == I2P_TUNNELS_SECTION_TYPE_WEBSOCKS)
						{
							LogPrint (eLogWarning, "Clients: I2P Client tunnel websocks is deprecated, not starting ",
								name, " tunnel");
							continue;
						}
						else
						{
							// tcp client
							auto tun = std::make_shared<I2PClientTunnel> (
								name, dest, address, port, localDestination, destinationPort);
							clientTunnel = tun;
							clientEndpoint = tun->GetLocalEndpoint ();

							uint32_t keepAlive
								= section.second.get<uint32_t> (I2P_CLIENT_TUNNEL_KEEP_ALIVE_INTERVAL, 0);
							if (keepAlive)
							{
								tun->SetKeepAliveInterval (keepAlive);
								LogPrint (
									eLogInfo, "Clients: I2P Client tunnel keep alive interval set to ", keepAlive);
							}
						}

						uint32_t timeout = section.second.get<uint32_t> (I2P_CLIENT_TUNNEL_CONNECT_TIMEOUT, 0);
						if (timeout)
						{
							clientTunnel->SetConnectTimeout (timeout);
							LogPrint (eLogInfo, "Clients: I2P Client tunnel connect timeout set to ", timeout);
						}
						uint64_t closeIdleTime = section.second.get (
							boost::property_tree::ptree::path_type (I2P_CLIENT_TUNNEL_CLOSE_IDLE_TIME, '/'), 0);
						if (closeIdleTime)
						{
							clientTunnel->SetCloseIdleTime (closeIdleTime);
							clientTunnel->SetNewDestOnResume (section.second.get (
								boost::property_tree::ptree::path_type (I2P_CLIENT_TUNNEL_NEW_DEST_ON_RESUME, '/'),
								false));
						}

						bool inserted = false;
						std::shared_ptr<I2PService> existing;
						{
							std::lock_guard<std::mutex> lock (m_ProxyMutex);
							auto ins = m_ClientTunnels.insert (std::make_pair (clientEndpoint, clientTunnel));
							inserted = ins.second;
							if (!inserted)
							{
								existing = ins.first->second;
								if (existing) existing->isUpdated = true;
							}
						}
						if (inserted)
						{
							if (enable)
							{
								clientTunnel->Start ();
								numClientTunnels++;
							}
							else
								LogPrint (eLogInfo, "Clients: I2P client tunnel ", name, " is disabled (enable=false)");
						}
						else if (existing)
						{
							// TODO: update
							if (!enable && existing->IsRunning ())
							{
								LogPrint (eLogInfo, "Clients: stopping disabled I2P client tunnel ", name);
								existing->Stop ();
							}
							else if (enable && !existing->IsRunning ())
							{
								LogPrint (eLogInfo, "Clients: starting newly enabled I2P client tunnel ", name);
								existing->Start ();
							}
							if (existing->IsRunning ()
								&& existing->GetLocalDestination () != clientTunnel->GetLocalDestination ())
							{
								LogPrint (eLogInfo, "Clients: I2P client tunnel destination updated");
								existing->Stop ();
								existing->SetLocalDestination (clientTunnel->GetLocalDestination ());
								existing->Start ();
							}
							LogPrint (eLogInfo, "Clients: I2P client tunnel for endpoint ", clientEndpoint,
								" already exists");
						}
					}
				}

				else if (type == I2P_TUNNELS_SECTION_TYPE_SERVER || type == I2P_TUNNELS_SECTION_TYPE_HTTP
					|| type == I2P_TUNNELS_SECTION_TYPE_IRC || type == I2P_TUNNELS_SECTION_TYPE_UDPSERVER)
				{
					// mandatory params
					std::string host = section.second.get<std::string> (I2P_SERVER_TUNNEL_HOST);
					uint16_t port = section.second.get<uint16_t> (I2P_SERVER_TUNNEL_PORT);
					std::string keys = section.second.get<std::string> (I2P_SERVER_TUNNEL_KEYS);
					// optional params
					uint16_t inPort = section.second.get<uint16_t> (I2P_SERVER_TUNNEL_INPORT, port);
					std::string accessList = section.second.get<std::string> (I2P_SERVER_TUNNEL_ACCESS_LIST, "");
					if (accessList == "")
						accessList = section.second.get<std::string> (I2P_SERVER_TUNNEL_WHITE_LIST, "");
					bool gzip = section.second.get (I2P_SERVER_TUNNEL_GZIP, false);
					i2p::data::SigningKeyType sigType = section.second.get (
						I2P_SERVER_TUNNEL_SIGNATURE_TYPE, i2p::data::SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519);
					if (sigType > i2p::data::SIGNING_KEY_TYPE_REDDSA_SHA512_ED25519)
						sigType = i2p::data::SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519;
					i2p::data::CryptoKeyType cryptoType
						= section.second.get (I2P_CLIENT_TUNNEL_CRYPTO_TYPE, i2p::data::CRYPTO_KEY_TYPE_ELGAMAL);

					std::string address = section.second.get<std::string> (I2P_SERVER_TUNNEL_ADDRESS, "");
					bool isUniqueLocal
						= section.second.get (I2P_SERVER_TUNNEL_ENABLE_UNIQUE_LOCAL, (host == "::1") ? false : true);
					bool ssl = section.second.get (I2P_SERVER_TUNNEL_SSL, false);
					// Claim the name before any destination is created, so a
					// duplicated section does not allocate a second identity.
					if (!claimName (host + ":" + std::to_string (port))) continue;

					// I2CP
					i2p::util::Mapping options;
					ReadI2CPOptions (section, true, options);

					// Set I2CP name if not set
					if (!options.Contains (I2CP_PARAM_INBOUND_NICKNAME))
						options.Insert (I2CP_PARAM_INBOUND_NICKNAME, name);

					std::shared_ptr<ClientDestination> localDestination = nullptr;
					if (keys == "shareddest")
						localDestination = GetSharedLocalDestination ();
					else
					{
						auto it = destinations.find (keys);
						if (it != destinations.end ())
						{
							localDestination = it->second;
							localDestination->SetPublic (true);
						}
						else
						{
							i2p::data::PrivateKeys k;
							if (!LoadPrivateKeys (k, keys, sigType, cryptoType)) continue;
							localDestination = FindLocalDestination (k.GetPublic ()->GetIdentHash ());
							if (!localDestination)
							{
								localDestination = CreateNewLocalDestination (k, true, &options);
								destinations[keys] = localDestination;
							}
							else
								localDestination->SetPublic (true);
						}
					}
					if (type == I2P_TUNNELS_SECTION_TYPE_UDPSERVER)
					{
						// udp server tunnel
						// TODO: hostnames
						boost::asio::ip::udp::endpoint endpoint (boost::asio::ip::make_address (host), port);
						if (address.empty ())
						{
							if (!endpoint.address ().is_unspecified () && endpoint.address ().is_v6 ())
								address = "::1";
							else
								address = "127.0.0.1";
						}
						auto localAddress = boost::asio::ip::make_address (address);
						auto serverTunnel = std::make_shared<I2PUDPServerTunnel> (
							name, localDestination, localAddress, endpoint, inPort, gzip);
						serverTunnel->SetMaxWindow (GetUDPTunnelMaxWindow (section.second));
						if (!isUniqueLocal)
						{
							LogPrint (eLogInfo, "Clients: Disabling loopback address mapping");
							serverTunnel->SetUniqueLocal (isUniqueLocal);
						}
						bool inserted = false;
						std::shared_ptr<I2PUDPServerTunnel> existing;
						{
							std::lock_guard<std::mutex> lock (m_ForwardsMutex);
							auto ins = m_ServerForwards.insert (std::make_pair (
								std::make_pair (localDestination->GetIdentHash (), port), serverTunnel));
							inserted = ins.second;
							if (!inserted)
							{
								existing = ins.first->second;
								if (existing) existing->isUpdated = true;
							}
						}
						if (inserted)
						{
							if (enable)
							{
								serverTunnel->Start ();
								LogPrint (eLogInfo, "Clients: I2P Server Forward created for UDP Endpoint ", host, ":",
									port, " bound on ", address, " for ",
									localDestination->GetIdentHash ().ToBase32 ());
							}
							else
								LogPrint (
									eLogInfo, "Clients: I2P server forward ", name, " is disabled (enable=false)");
						}
						else
						{
							LogPrint (eLogError, "Clients: I2P Server Forward for destination/port ",
								m_AddressBook.ToAddress (localDestination->GetIdentHash ()), "/", port,
								" already exists");
						}

						continue;
					}

					std::shared_ptr<I2PServerTunnel> serverTunnel;
					if (type == I2P_TUNNELS_SECTION_TYPE_HTTP)
					{
						std::string hostOverride
							= section.second.get<std::string> (I2P_SERVER_TUNNEL_HOST_OVERRIDE, "");
						bool i2pheaders = section.second.get (I2P_SERVER_TUNNEL_I2P_HEADERS, true);
						serverTunnel = std::make_shared<I2PServerTunnelHTTP> (
							name, host, port, localDestination, hostOverride, inPort, gzip, i2pheaders);
					}
					else if (type == I2P_TUNNELS_SECTION_TYPE_IRC)
					{
						std::string webircpass
							= section.second.get<std::string> (I2P_SERVER_TUNNEL_WEBIRC_PASSWORD, "");
						serverTunnel = std::make_shared<I2PServerTunnelIRC> (
							name, host, port, localDestination, webircpass, inPort, gzip);
					}
					else // regular server tunnel by default
						serverTunnel
							= std::make_shared<I2PServerTunnel> (name, host, port, localDestination, inPort, gzip);

					if (!address.empty ()) serverTunnel->SetLocalAddress (address);
					if (!isUniqueLocal)
					{
						LogPrint (eLogInfo, "Clients: Disabling loopback address mapping");
						serverTunnel->SetUniqueLocal (isUniqueLocal);
					}
					if (ssl) serverTunnel->SetSSL (true);
					serverTunnel->SetDefaultAcceptor (
						section.second.get<bool> (I2P_SERVER_TUNNEL_DEFAULT_ACCEPTOR, false));
					if (double delayOpen = section.second.get<double> (I2P_SERVER_TUNNEL_DELAY_OPEN, 0.0);
						delayOpen > 0)
						LogPrint (eLogWarning, "Clients: delayOpen is not supported and is ignored for ", name);

					// targetForPort.<port>=host:port (bounded, Java compatible)
					I2PServerTunnelTargetMap targets;
					for (const auto & property : section.second)
					{
						if (!property.second.empty ()) continue; // not a leaf value
						auto lower = ToLowerCopy (property.first);
						if (!lower.starts_with (I2P_SERVER_TUNNEL_TARGET_FOR_PORT)) continue;
						uint16_t localPort = 0;
						std::string error;
						if (!ParseLocalPort (
								lower.substr (std::string_view (I2P_SERVER_TUNNEL_TARGET_FOR_PORT).size ()), localPort)
							|| !targets.AddSpec (localPort, property.second.get_value<std::string> (), error))
						{
							std::string reason = error.empty () ? std::string ("invalid port") : error;
							LogPrint (eLogError, "Clients: invalid targetForPort entry for server tunnel ", name, ": ",
								reason);
						}
					}
					serverTunnel->SetTargets (targets);

					std::string filterDefinition
						= section.second.get<std::string> (I2P_SERVER_TUNNEL_FILTER_DEFINITION, "");
					std::string limitAction = section.second.get<std::string> (I2P_SERVER_TUNNEL_LIMIT_ACTION, "");
					if (!filterDefinition.empty ())
					{
						std::string error;
						auto definition = FilterDefinition::Parse (filterDefinition, error);
						if (!definition)
						{
							// An invalid definition must not fall back to an
							// open tunnel; the section is skipped with a
							// non-secret description.
							LogPrint (eLogError, "Clients: invalid filterDefinition for server tunnel ", name, ": ",
								error, "; the tunnel is not started");
							continue;
						}
						serverTunnel->SetAccessFilter (definition, limitAction);
					}
					else
					{
						if (!accessList.empty ())
						{
							// Every entry is validated: a zero hash used to be
							// installed for an invalid value and then matched
							// nothing, silently dropping every peer. The list is
							// upgraded to an explicit filter so a denied peer is
							// refused before the SYNACK instead of after it.
							std::vector<i2p::data::IdentHash> idents;
							size_t invalid = 0;
							size_t pos = 0, comma = 0;
							do
							{
								comma = accessList.find (',', pos);
								auto entry = accessList.substr (
									pos, comma != std::string::npos ? comma - pos : std::string::npos);
								auto begin = entry.find_first_not_of (" \t");
								auto end = entry.find_last_not_of (" \t");
								if (begin != std::string::npos)
								{
									entry = entry.substr (begin, end - begin + 1);
									i2p::data::IdentHash ident;
									if (ident.FromBase32 (entry) == ident.len)
										idents.push_back (ident);
									else
										++invalid;
								}
								pos = comma + 1;
							} while (comma != std::string::npos);
							if (invalid)
								LogPrint (eLogWarning, "Clients: ", invalid, " accesslist entries of ", name,
									" are invalid and were skipped");
							if (!idents.empty ())
								serverTunnel->SetAccessFilter (FilterDefinition::FromAccessList (idents), limitAction);
							else
							{
								LogPrint (eLogWarning, "Clients: accesslist of ", name,
									" has no valid entry; no whitelist was installed");
								serverTunnel->SetLimitAction (limitAction);
							}
						}
						else
							serverTunnel->SetLimitAction (limitAction);
					}

					if (type == I2P_TUNNELS_SECTION_TYPE_HTTP)
					{
						auto httpTunnel = std::static_pointer_cast<I2PServerTunnelHTTP> (serverTunnel);

						for (const auto & property : section.second)
						{
							if (!property.second.empty ()) continue;
							auto lower = ToLowerCopy (property.first);
							if (!lower.starts_with (I2P_SERVER_TUNNEL_SPOOFED_HOST)) continue;
							uint16_t localPort = 0;
							std::string error;
							if (!ParseLocalPort (
									lower.substr (std::string_view (I2P_SERVER_TUNNEL_SPOOFED_HOST).size ()), localPort)
								|| !httpTunnel->SetSpoofedHost (
									localPort, property.second.get_value<std::string> (), error))
							{
								std::string reason = error.empty () ? std::string ("invalid port") : error;
								LogPrint (eLogError, "Clients: invalid spoofedHost entry for server tunnel ", name,
									": ", reason);
							}
						}

						httpTunnel->SetRejectInproxy (
							section.second.get<bool> (I2P_SERVER_TUNNEL_REJECT_INPROXY, false));
						httpTunnel->SetRejectReferer (
							section.second.get<bool> (I2P_SERVER_TUNNEL_REJECT_REFERER, false));
						const std::string rejectUserAgents
							= section.second.get<std::string> (I2P_SERVER_TUNNEL_REJECT_USER_AGENTS, "");
						if (!rejectUserAgents.empty ())
						{
							std::string error;
							if (!httpTunnel->SetRejectUserAgents (rejectUserAgents, error))
								LogPrint (eLogError, "Clients: invalid rejectUserAgents for server tunnel ", name, ": ",
									error);
						}
						if (auto postMax = section.second.get_optional<int> (I2P_SERVER_TUNNEL_POST_MAX))
							httpTunnel->SetPostLimit (*postMax);
					}
					bool inserted = false;
					std::shared_ptr<I2PServerTunnel> existing;
					{
						std::lock_guard<std::mutex> lock (m_ProxyMutex);
						auto ins = m_ServerTunnels.insert (
							std::make_pair (std::make_pair (localDestination->GetIdentHash (), inPort), serverTunnel));
						inserted = ins.second;
						if (!inserted)
						{
							existing = ins.first->second;
							if (existing) existing->isUpdated = true;
						}
					}
					if (inserted)
					{
						if (enable)
						{
							serverTunnel->Start ();
							numServerTunnels++;
						}
						else
							LogPrint (eLogInfo, "Clients: I2P server tunnel ", name, " is disabled (enable=false)");
					}
					else if (existing)
					{
						// TODO: update
						if (!enable && existing->IsRunning ())
						{
							LogPrint (eLogInfo, "Clients: stopping disabled I2P server tunnel ", name);
							existing->Stop (true);
						}
						else if (enable && !existing->IsRunning ())
						{
							LogPrint (eLogInfo, "Clients: starting newly enabled I2P server tunnel ", name);
							existing->Start ();
						}
						else if (existing->IsRunning ()
							&& existing->GetLocalDestination () != serverTunnel->GetLocalDestination ())
						{
							LogPrint (eLogInfo, "Clients: I2P server tunnel destination updated");
							existing->Stop ();
							existing->SetLocalDestination (serverTunnel->GetLocalDestination ());
							existing->Start ();
						}
						else if (existing->IsRunning ())
							existing->Accept (); // still in the config, take the acceptor back
						else
							LogPrint (eLogInfo, "Clients: I2P server tunnel ", name,
								" stays disabled; its previous acceptor was detached");
						LogPrint (eLogInfo, "Clients: I2P server tunnel for destination/port ",
							m_AddressBook.ToAddress (localDestination->GetIdentHash ()), "/", inPort,
							" already exists");
					}
				}
				else if (type == I2P_TUNNELS_SECTION_TYPE_TORRENTS)
				{
#ifndef NO_TORRENTS
					// mandatory params
					std::string torrentsDir = section.second.get<std::string> (TORRENTS_TUNNEL_TORRENTS_DIR);
					// optional params
					if (!claimName (torrentsDir)) continue;
					std::string keys = section.second.get<std::string> (TORRENTS_TUNNEL_KEYS, "transient");
					std::string trackers = section.second.get<std::string> (TORRENTS_TUNNEL_TRACKERS, "");
					i2p::data::SigningKeyType sigType = section.second.get (
						TORRENTS_TUNNEL_SIGNATURE_TYPE, i2p::data::SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519);
					if (sigType > i2p::data::SIGNING_KEY_TYPE_REDDSA_SHA512_ED25519)
						sigType = i2p::data::SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519;
					bool dht = section.second.get<bool> (TORRENTS_TUNNEL_DHT, true);
					std::string dhtFileName = section.second.get<std::string> (TORRENTS_TUNNEL_DHT_FILE, "");
					if (dhtFileName.empty () && keys.starts_with ("transient"))
					{
						// finename with zero in first byte means non persistent
						dhtFileName.resize (1);
						dhtFileName[0] = '\x00';
					}

					// I2CP
					i2p::util::Mapping options;
					ReadI2CPOptions (section, true, options);

					// Set I2CP name if not set
					if (!options.Contains (I2CP_PARAM_INBOUND_NICKNAME))
						options.Insert (I2CP_PARAM_INBOUND_NICKNAME, name);

					std::shared_ptr<ClientDestination> localDestination = nullptr;
					auto it = destinations.find (keys);
					if (it != destinations.end ())
					{
						localDestination = it->second;
						localDestination->SetPublic (true);
					}
					else
					{
						i2p::data::PrivateKeys k;
						if (!LoadPrivateKeys (k, keys, sigType, i2p::data::CRYPTO_KEY_TYPE_ELGAMAL)) continue;
						localDestination = FindLocalDestination (k.GetPublic ()->GetIdentHash ());
						if (!localDestination)
						{
							localDestination = CreateNewLocalDestination (k, true, &options);
							destinations[keys] = localDestination;
						}
						else
							localDestination->SetPublic (true);
					}
					auto torrentsTunnel = std::make_shared<i2p::torrents::TorrentsTunnel> (name, localDestination,
						torrentsDir, trackers, dht, dhtFileName, section.second.get<bool> ("isolatedstorage", false));
					bool inserted = false;
					{
						std::lock_guard<std::mutex> l (m_TorrentsTunnelsMutex);
						inserted = m_TorrentsTunnels.emplace (localDestination->GetIdentHash (), torrentsTunnel).second;
					}
					if (inserted)
					{
						if (enable)
							torrentsTunnel->Start ();
						else
							LogPrint (eLogInfo, "Clients: torrents tunnel ", name, " is disabled (enable=false)");
						// try to create optional local-only RPC server
						uint16_t port = section.second.get<uint16_t> (TORRENTS_TUNNEL_RPC_PORT, 0);
						if (port)
						{
							auto address = section.second.get<std::string> (TORRENTS_TUNNEL_RPC_ADDRESS, "127.0.0.1");
							auto path = section.second.get<std::string> (TORRENTS_TUNNEL_RPC_PATH, "");
							auto auth = section.second.get<bool> (TORRENTS_TUNNEL_RPC_AUTH, false);
							std::string user, pass;
							if (auth)
							{
								user = section.second.get<std::string> (TORRENTS_TUNNEL_RPC_USER, "");
								pass = section.second.get<std::string> (TORRENTS_TUNNEL_RPC_PASS, "");
							}
							auto isValidCredential = [] (const std::string & value) -> bool
							{
								return !value.empty () && value.size () <= 256 && value.find ('\0') == std::string::npos
									&& value.find ('\r') == std::string::npos && value.find ('\n') == std::string::npos;
							};
							boost::system::error_code addressError;
							auto bindAddress = boost::asio::ip::make_address (address, addressError);
							if (addressError || !bindAddress.is_loopback ())
								LogPrint (eLogError, "Clients: Torrents RPC address ", address,
									" is not a loopback address. Refusing to start the RPC server");
							else if (auth && (!isValidCredential (user) || !isValidCredential (pass)))
								LogPrint (eLogError,
									"Clients: Torrents RPC authentication requires non-empty, bounded rpcuser and "
									"rpcpass. Refusing to start the RPC server");
							else
							{
								auto server = CreateTorrentsRPCServer (address, port);
								if (server) server->AddTunnel (path, torrentsTunnel, auth, user, pass);
							}
						}
					}
					else
						LogPrint (
							eLogError, "Clients: Failed to create torrents tunnel ", name, ". Duplicate keys ", keys);
#else
					LogPrint (eLogError, "Clients: Torrents tunnels are not supported. Compile without NO_TORRENTS");
#endif
				}
				else
					LogPrint (eLogError, "Clients: Unknown section type = ", type, " of ", name, " in ", tunConf);
			}
			catch (std::exception & ex)
			{
				LogPrint (eLogCritical, "Clients: Can't read tunnel ", name, " params: ", ex.what ());
				ThrowFatal ("Unable to start tunnel ", name, ": ", ex.what ());
			}
		}
	}

	void ClientContext::ReadHttpProxy ()
	{
		std::shared_ptr<ClientDestination> localDestination;
		bool httproxy;
		i2p::config::GetOption ("httpproxy.enabled", httproxy);
		if (httproxy)
		{
			std::string httpProxyKeys;
			i2p::config::GetOption ("httpproxy.keys", httpProxyKeys);
			std::string httpProxyAddr;
			i2p::config::GetOption ("httpproxy.address", httpProxyAddr);
			uint16_t httpProxyPort;
			i2p::config::GetOption ("httpproxy.port", httpProxyPort);
			std::string httpOutProxyURL;
			i2p::config::GetOption ("httpproxy.outproxy", httpOutProxyURL);
			bool httpAddresshelper;
			i2p::config::GetOption ("httpproxy.addresshelper", httpAddresshelper);
			bool httpSendUserAgent;
			i2p::config::GetOption ("httpproxy.senduseragent", httpSendUserAgent);
			if (httpAddresshelper)
				i2p::config::GetOption (
					"addressbook.enabled", httpAddresshelper); // addresshelper is not supported without address book
			i2p::data::SigningKeyType sigType;
			i2p::config::GetOption ("httpproxy.signaturetype", sigType);
			if (sigType > i2p::data::SIGNING_KEY_TYPE_REDDSA_SHA512_ED25519)
				sigType = i2p::data::SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519;
			LogPrint (eLogInfo, "Clients: Starting HTTP Proxy at ", httpProxyAddr, ":", httpProxyPort);
			if (httpProxyKeys == "shareddest")
				localDestination = GetSharedLocalDestination ();
			else if (httpProxyKeys.length () > 0)
			{
				i2p::data::PrivateKeys keys;
				if (LoadPrivateKeys (keys, httpProxyKeys, sigType))
				{
					i2p::util::Mapping params;
					ReadI2CPOptionsFromConfig ("httpproxy.", params);
					params.Insert (I2CP_PARAM_OUTBOUND_NICKNAME, "HTTPProxy");
					localDestination = CreateNewLocalDestination (keys, false, &params);
				}
				else
					LogPrint (eLogCritical, "Clients: Failed to load HTTP Proxy key");
			}
			try
			{
				// publish only after it is started, so readers never see a half-built proxy
				auto proxy = std::make_shared<i2p::proxy::HTTPProxy> ("HTTP Proxy", httpProxyAddr, httpProxyPort,
					httpOutProxyURL, httpAddresshelper, httpSendUserAgent, localDestination);
				uint64_t closeIdleTime;
				i2p::config::GetOption ("httpproxy.i2cp.closeIdleTime", closeIdleTime);
				if (closeIdleTime)
				{
					proxy->SetCloseIdleTime (closeIdleTime);
					bool newDestOnResume;
					i2p::config::GetOption ("httpproxy.i2cp.newDestOnResume", newDestOnResume);
					proxy->SetNewDestOnResume (newDestOnResume);
				}
				proxy->Start ();
				std::lock_guard<std::mutex> l (m_ProxyMutex);
				m_HttpProxy = proxy;
			}
			catch (std::exception & e)
			{
				LogPrint (eLogCritical, "Clients: Exception in HTTP Proxy: ", e.what ());
				ThrowFatal ("Unable to start HTTP Proxy at ", httpProxyAddr, ":", httpProxyPort, ": ", e.what ());
			}
		}
	}

	void ClientContext::ReadSocksProxy ()
	{
		std::shared_ptr<ClientDestination> localDestination;
		bool socksproxy;
		i2p::config::GetOption ("socksproxy.enabled", socksproxy);
		if (socksproxy)
		{
			std::string httpProxyKeys;
			i2p::config::GetOption ("httpproxy.keys", httpProxyKeys);
			// we still need httpProxyKeys to compare with sockProxyKeys
			std::string socksProxyKeys;
			i2p::config::GetOption ("socksproxy.keys", socksProxyKeys);
			std::string socksProxyAddr;
			i2p::config::GetOption ("socksproxy.address", socksProxyAddr);
			uint16_t socksProxyPort;
			i2p::config::GetOption ("socksproxy.port", socksProxyPort);
			bool socksOutProxy;
			i2p::config::GetOption ("socksproxy.outproxy.enabled", socksOutProxy);
			std::string socksOutProxyAddr;
			i2p::config::GetOption ("socksproxy.outproxy", socksOutProxyAddr);
			uint16_t socksOutProxyPort;
			i2p::config::GetOption ("socksproxy.outproxyport", socksOutProxyPort);
			i2p::data::SigningKeyType sigType;
			i2p::config::GetOption ("socksproxy.signaturetype", sigType);
			if (sigType > i2p::data::SIGNING_KEY_TYPE_REDDSA_SHA512_ED25519)
				sigType = i2p::data::SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519;
			LogPrint (eLogInfo, "Clients: Starting SOCKS Proxy at ", socksProxyAddr, ":", socksProxyPort);
			std::shared_ptr<I2PService> httpProxy;
			{
				std::lock_guard<std::mutex> l (m_ProxyMutex);
				httpProxy = m_HttpProxy;
			}
			if (socksProxyKeys == "shareddest")
				localDestination = GetSharedLocalDestination ();
			else if (httpProxyKeys == socksProxyKeys && httpProxy)
				localDestination = httpProxy->GetLocalDestination ();
			else if (socksProxyKeys.length () > 0)
			{
				i2p::data::PrivateKeys keys;
				if (LoadPrivateKeys (keys, socksProxyKeys, sigType))
				{
					i2p::util::Mapping params;
					ReadI2CPOptionsFromConfig ("socksproxy.", params);
					params.Insert (I2CP_PARAM_OUTBOUND_NICKNAME, "SOCKSProxy");
					localDestination = CreateNewLocalDestination (keys, false, &params);
				}
				else
					LogPrint (eLogCritical, "Clients: Failed to load SOCKS Proxy key");
			}
			try
			{
				auto proxy = std::make_shared<i2p::proxy::SOCKSProxy> ("SOCKS", socksProxyAddr, socksProxyPort,
					socksOutProxy, socksOutProxyAddr, socksOutProxyPort, localDestination);
				uint64_t closeIdleTime;
				i2p::config::GetOption ("socksproxy.i2cp.closeIdleTime", closeIdleTime);
				if (closeIdleTime)
				{
					proxy->SetCloseIdleTime (closeIdleTime);
					bool newDestOnResume;
					i2p::config::GetOption ("socksproxy.i2cp.newDestOnResume", newDestOnResume);
					proxy->SetNewDestOnResume (newDestOnResume);
				}
				proxy->Start ();
				std::lock_guard<std::mutex> l (m_ProxyMutex);
				m_SocksProxy = proxy;
			}
			catch (std::exception & e)
			{
				LogPrint (eLogCritical, "Clients: Exception in SOCKS Proxy: ", e.what ());
				ThrowFatal ("Unable to start SOCKS Proxy at ", socksProxyAddr, ":", socksProxyPort, ": ", e.what ());
			}
		}
	}

	void ClientContext::VisitTunnels (bool clean)
	{
		// Collect the removed objects under the locks, exit the lock and only
		// then stop/join them.
		std::vector<std::shared_ptr<I2PService>> tcpTunnels;
		std::vector<std::shared_ptr<I2PUDPClientTunnel>> clientForwards;
		std::vector<std::shared_ptr<I2PUDPServerTunnel>> serverForwards;
		{
			std::lock_guard<std::mutex> l (m_ProxyMutex);
			for (auto it = m_ClientTunnels.begin (); it != m_ClientTunnels.end ();)
			{
				if (clean && (!it->second || !it->second->isUpdated))
				{
					if (it->second) tcpTunnels.push_back (it->second);
					it = m_ClientTunnels.erase (it);
				}
				else
				{
					if (it->second) it->second->isUpdated = false;
					it++;
				}
			}
			for (auto it = m_ServerTunnels.begin (); it != m_ServerTunnels.end ();)
			{
				if (clean && (!it->second || !it->second->isUpdated))
				{
					if (it->second) tcpTunnels.push_back (it->second);
					it = m_ServerTunnels.erase (it);
				}
				else
				{
					if (it->second) it->second->isUpdated = false;
					it++;
				}
			}
		}
		{
			std::lock_guard<std::mutex> l (m_ForwardsMutex);
			for (auto it = m_ClientForwards.begin (); it != m_ClientForwards.end ();)
			{
				if (clean && (!it->second || !it->second->isUpdated))
				{
					if (it->second) clientForwards.push_back (it->second);
					it = m_ClientForwards.erase (it);
				}
				else
				{
					if (it->second) it->second->isUpdated = false;
					it++;
				}
			}
			for (auto it = m_ServerForwards.begin (); it != m_ServerForwards.end ();)
			{
				if (clean && (!it->second || !it->second->isUpdated))
				{
					if (it->second) serverForwards.push_back (it->second);
					it = m_ServerForwards.erase (it);
				}
				else
				{
					if (it->second) it->second->isUpdated = false;
					it++;
				}
			}
		}
		for (const auto & tunnel : tcpTunnels) tunnel->Stop ();
		for (const auto & tunnel : clientForwards) tunnel->Stop ();
		for (const auto & tunnel : serverForwards) tunnel->Stop ();
	}

#ifndef NO_TORRENTS
	std::shared_ptr<i2p::torrents::TorrentsRPCServer> ClientContext::CreateTorrentsRPCServer (
		std::string_view address, uint16_t port)
	{
		std::shared_ptr<i2p::torrents::TorrentsRPCServer> server;
		try
		{
			server = std::make_shared<i2p::torrents::TorrentsRPCServer> (address, port);
		}
		catch (std::exception & ex)
		{
			LogPrint (eLogError, "Clients: Can't create torrents RPC server of port: ", port, " :", ex.what ());
		}
		std::lock_guard<std::mutex> l (m_TorrentsTunnelsMutex);
		if (server) return m_TorrentsRPCServers.try_emplace (port, server).first->second;
		auto it = m_TorrentsRPCServers.find (port);
		if (it != m_TorrentsRPCServers.end ()) return it->second;
		return nullptr;
	}
#endif
}
}

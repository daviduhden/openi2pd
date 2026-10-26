/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: ClientContext.
 *
 * Declares the process-wide client context that owns destinations, proxies,
 * tunnel collections, the address book and optional SAM/BOB/I2CP/torrent
 * services. It exposes destination creation, key loading and the tunnel
 * configuration identifiers shared by the client modules.
 *
 */

#ifndef CLIENT_CONTEXT_H__
#define CLIENT_CONTEXT_H__

#include <map>
#include <unordered_map>
#include <vector>
#include <set>
#include <utility>
#include <mutex>
#include <memory>
#include <algorithm>
#include <string>
#include <string_view>
#include <boost/asio.hpp>
#include <boost/property_tree/ptree_fwd.hpp>
#include "Destination.h"
#include "I2PService.h"
#include "I2PTunnel.h"
#include "UDPTunnel.h"
#include "SAM.h"
#include "BOB.h"
#include "I2CP.h"
#include "AddressBook.h"
#include "TorrentsTunnel.h"
#include "TorrentsRPC.h"
#include "I18N_langs.h"

namespace i2p
{
namespace client
{
	const char I2P_TUNNELS_SECTION_TYPE[] = "type";
	const char I2P_TUNNELS_SECTION_TYPE_CLIENT[] = "client";
	const char I2P_TUNNELS_SECTION_TYPE_SERVER[] = "server";
	const char I2P_TUNNELS_SECTION_TYPE_HTTP[] = "http";
	const char I2P_TUNNELS_SECTION_TYPE_IRC[] = "irc";
	const char I2P_TUNNELS_SECTION_TYPE_UDPCLIENT[] = "udpclient";
	const char I2P_TUNNELS_SECTION_TYPE_UDPSERVER[] = "udpserver";
	const char I2P_TUNNELS_SECTION_TYPE_SOCKS[] = "socks";
	const char I2P_TUNNELS_SECTION_TYPE_WEBSOCKS[] = "websocks";
	const char I2P_TUNNELS_SECTION_TYPE_HTTPPROXY[] = "httpproxy";
	const char I2P_TUNNELS_SECTION_TYPE_TORRENTS[] = "torrents";
	const char I2P_CLIENT_TUNNEL_PORT[] = "port";
	const char I2P_CLIENT_TUNNEL_ADDRESS[] = "address";
	const char I2P_CLIENT_TUNNEL_DESTINATION[] = "destination";
	const char I2P_CLIENT_TUNNEL_KEYS[] = "keys";
	const char I2P_CLIENT_TUNNEL_GZIP[] = "gzip";
	const char I2P_CLIENT_TUNNEL_SIGNATURE_TYPE[] = "signaturetype";
	const char I2P_CLIENT_TUNNEL_CRYPTO_TYPE[] = "cryptotype";
	const char I2P_CLIENT_TUNNEL_DESTINATION_PORT[] = "destinationport";
	const char I2P_CLIENT_TUNNEL_MATCH_TUNNELS[] = "matchtunnels";
	const char I2P_CLIENT_TUNNEL_CONNECT_TIMEOUT[] = "connecttimeout";
	const char I2P_CLIENT_TUNNEL_KEEP_ALIVE_INTERVAL[] = "keepaliveinterval";
	const char I2P_CLIENT_TUNNEL_CLOSE_IDLE_TIME[] = "i2cp.closeIdleTime";
	const char I2P_CLIENT_TUNNEL_NEW_DEST_ON_RESUME[] = "i2cp.newDestOnResume";
	const char I2P_SERVER_TUNNEL_HOST[] = "host";
	const char I2P_SERVER_TUNNEL_HOST_OVERRIDE[] = "hostoverride";
	const char I2P_SERVER_TUNNEL_I2P_HEADERS[] = "i2pheaders";
	const char I2P_SERVER_TUNNEL_PORT[] = "port";
	const char I2P_SERVER_TUNNEL_KEYS[] = "keys";
	const char I2P_SERVER_TUNNEL_SIGNATURE_TYPE[] = "signaturetype";
	const char I2P_SERVER_TUNNEL_INPORT[] = "inport";
	const char I2P_SERVER_TUNNEL_ACCESS_LIST[] = "accesslist";
	const char I2P_SERVER_TUNNEL_WHITE_LIST[] = "whitelist";
	const char I2P_SERVER_TUNNEL_FILTER_DEFINITION[] = "filterDefinition";
	const char I2P_SERVER_TUNNEL_LIMIT_ACTION[] = "limitAction";
	const char I2P_SERVER_TUNNEL_TARGET_FOR_PORT[] = "targetForPort.";
	const char I2P_SERVER_TUNNEL_SPOOFED_HOST[] = "spoofedHost.";
	const char I2P_SERVER_TUNNEL_DEFAULT_ACCEPTOR[] = "defaultacceptor";
	const char I2P_SERVER_TUNNEL_REJECT_INPROXY[] = "rejectInproxy";
	const char I2P_SERVER_TUNNEL_REJECT_REFERER[] = "rejectReferer";
	const char I2P_SERVER_TUNNEL_REJECT_USER_AGENTS[] = "rejectUserAgents";
	const char I2P_SERVER_TUNNEL_POST_MAX[] = "postMax";
	const char I2P_SERVER_TUNNEL_DELAY_OPEN[] = "delayOpen";
	const char I2P_SERVER_TUNNEL_GZIP[] = "gzip";
	const char I2P_SERVER_TUNNEL_WEBIRC_PASSWORD[] = "webircpassword";
	const char I2P_SERVER_TUNNEL_ADDRESS[] = "address";
	const char I2P_SERVER_TUNNEL_ENABLE_UNIQUE_LOCAL[] = "enableuniquelocal";
	const char I2P_SERVER_TUNNEL_SSL[] = "ssl";
	// Administrative lifecycle keys honoured by ReadTunnels.
	const char I2P_TUNNEL_NAME[] = "name";
	const char I2P_TUNNEL_ENABLE[] = "enable";
	const char I2P_TUNNEL_START_ON_LOAD[] = "startOnLoad";
	const char UDP_CLIENT_TUNNEL_DATAGRAM_VERSION[] = "datagramversion";
	const char UDP_TUNNEL_MAX_WINDOW[] = "maxwindow";
	const char TORRENTS_TUNNEL_KEYS[] = "keys";
	const char TORRENTS_TUNNEL_SIGNATURE_TYPE[] = "signaturetype";
	const char TORRENTS_TUNNEL_TORRENTS_DIR[] = "torrentsdir";
	const char TORRENTS_TUNNEL_TRACKERS[] = "trackers";
	const char TORRENTS_TUNNEL_RPC_PORT[] = "rpcport";
	const char TORRENTS_TUNNEL_RPC_PATH[] = "rpcpath";
	const char TORRENTS_TUNNEL_RPC_ADDRESS[] = "rpcaddress";
	const char TORRENTS_TUNNEL_RPC_AUTH[] = "rpcauth";
	const char TORRENTS_TUNNEL_RPC_USER[] = "rpcuser";
	const char TORRENTS_TUNNEL_RPC_PASS[] = "rpcpass";
	const char TORRENTS_TUNNEL_DHT[] = "dht";
	const char TORRENTS_TUNNEL_DHT_FILE[] = "dhtfile";

	constexpr int STOP_DESTINATIONS_TIMEOUT = 8; // in seconds
	constexpr size_t I2P_TUNNEL_SERVICE_NAME_MAX_LENGTH = 64;

	// Administrative view of one configured client or server tunnel. Names are
	// unique across tunnels.conf and tunnels.d so the typed StartTunnel and
	// StopTunnel verbs have exactly one target.
	struct TunnelServiceInfo
	{
		std::string name;
		std::string kind; // client, httpproxy, socksproxy, server, http, irc
		// Server tunnels: the local destination in base32. Client tunnels: the
		// configured destination list, comma separated.
		std::string destination;
		// Client tunnels: the bound 127.0.0.1:<port>. Server tunnels:
		// i2p:<inport>, the port clients connect to.
		std::string localEndpoint;
		// Client tunnels: the configured destination list. Server tunnels:
		// the configured local host:port upstream.
		std::string target;
		bool running = false;
	};

	// Stable administrative service name: the configured `name=` value when
	// set, the INI section name otherwise and `<type>://<endpoint>` as the last
	// fallback for a nameless section.
	std::string MakeTunnelServiceName (std::string_view configuredName, std::string_view sectionName,
		std::string_view type, std::string_view endpoint);
	// Rejects empty, over-long and control-character-bearing names.
	bool IsValidTunnelServiceName (std::string_view name);
	// `enable`/`startOnLoad` gate. Both default to true; a false `enable`
	// always wins so a disabled tunnel stays disabled regardless of startOnLoad.
	bool ShouldStartTunnelOnLoad (bool enable, bool startOnLoad);

	// Tracks the configured service names of one configuration reload so a
	// duplicate is a configuration error instead of an ambiguous verb target.
	class TunnelServiceNameRegistry
	{
	public:
		void Clear ();
		bool Register (std::string_view name);
		size_t Size () const;
		bool Contains (std::string_view name) const;

	private:
		mutable std::mutex m_Mutex;
		std::set<std::string> m_Names;
	};

	/// Owns the client destination registry and configured client services.
	/// Registry snapshots retain shared objects after the lock is released, but
	/// do not guarantee that those objects are still running. Lifecycle changes
	/// stop services outside registry locks because shutdown can re-enter them.
	class ClientContext
	{
	public:
		ClientContext ();
		~ClientContext ();

		void Start ();
		void Stop ();

		/// Re-read the live client configuration. It replaces the shared local
		/// destination, the HTTP/SOCKS proxies and the configured tunnels, then
		/// drops destinations that no configured tunnel references any more.
		///
		/// Server tunnel acceptors are detached before the tunnels are re-read,
		/// so incoming streams queue while a tunnel that still exists re-installs
		/// its own acceptor, and `isUpdated` (a transient reload flag) decides
		/// which tunnels are removed afterwards. The already loaded
		/// configuration object and the tunnel files are read again, but the
		/// process-wide configuration is not reparsed.
		///
		/// @pre The caller is the single owner of the reload path (the daemon
		///   administration thread); the path stops and joins destination threads
		///   and must not run on a destination or TUI handler thread.
		/// @warning Any reference returned by the legacy accessors (or a raw
		///   service pointer) is invalid once this returns. Use the snapshot
		///   accessors instead.
		void ReloadConfig ();

		std::shared_ptr<ClientDestination> GetSharedLocalDestination () const
		{
			std::lock_guard<std::mutex> l (m_DestinationsMutex);
			return m_SharedLocalDestination;
		};
		std::shared_ptr<ClientDestination> CreateNewLocalDestination (bool isPublic = false, // transient
			i2p::data::SigningKeyType sigType = i2p::data::SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519,
			i2p::data::CryptoKeyType cryptoType = i2p::data::CRYPTO_KEY_TYPE_ELGAMAL,
			const i2p::util::Mapping * params = nullptr); // used by SAM only
		std::shared_ptr<ClientDestination> CreateNewLocalDestination (boost::asio::io_context & service,
			bool isPublic = false, i2p::data::SigningKeyType sigType = i2p::data::SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519,
			i2p::data::CryptoKeyType cryptoType = i2p::data::CRYPTO_KEY_TYPE_ELGAMAL,
			const i2p::util::Mapping * params = nullptr); // same as previous but on external io_service
		std::shared_ptr<ClientDestination> CreateNewLocalDestination (
			const i2p::data::PrivateKeys & keys, bool isPublic = true, const i2p::util::Mapping * params = nullptr);
		std::shared_ptr<ClientDestination> CreateNewLocalDestination (boost::asio::io_context & service,
			const i2p::data::PrivateKeys & keys, bool isPublic = true,
			const i2p::util::Mapping * params = nullptr); // same as previous but on external io_service
		std::shared_ptr<ClientDestination> CreateNewMatchedTunnelDestination (
			const i2p::data::PrivateKeys & keys, const std::string & name, const i2p::util::Mapping * params = nullptr);
		/// Remove and stop the exact registered destination object, including
		/// when its map key has been replaced. An unregistered object is ignored.
		void DeleteLocalDestination (std::shared_ptr<ClientDestination> destination);
		bool ReplaceLocalDestinationHash (
			const i2p::data::IdentHash & oldIdentHash, const i2p::data::IdentHash & newIdentHash);
		std::shared_ptr<ClientDestination> FindLocalDestination (const i2p::data::IdentHash & destination) const;
		/// Load a destination's private keys, generating a fresh identity when
		/// @a filename starts with "transient".
		/// The file is read relative to the data directory, its size is bounded
		/// before reading, and only the generated or loaded public address is
		/// ever logged. A missing file is not an error: a new identity is
		/// created and written back. A file that exists but is empty, oversized,
		/// truncated or unparsable fails the call instead of silently creating a
		/// replacement identity, so a damaged key file cannot rotate an
		/// operator's address.
		/// @return false only for an existing file that cannot be used as a key.
		bool LoadPrivateKeys (i2p::data::PrivateKeys & keys, std::string_view filename,
			i2p::data::SigningKeyType sigType = i2p::data::SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519,
			i2p::data::CryptoKeyType cryptoType = i2p::data::CRYPTO_KEY_TYPE_ELGAMAL);

		AddressBook & GetAddressBook () { return m_AddressBook; };
		const BOBCommandChannel * GetBOBCommandChannel () const { return m_BOBCommandChannel.get (); };
		const SAMBridge * GetSAMBridge () const { return m_SamBridge.get (); };
		const I2CPServer * GetI2CPServer () const { return m_I2CPServer.get (); };

		// --- administrative tunnel inventory and lifecycle -------------------
		// Sorted, stable snapshot of every configured TCP client and server
		// tunnel. Returns copies; callers never hold a ClientContext lock.
		std::vector<TunnelServiceInfo> GetTunnelServices () const;
		// Start or stop one configured tunnel by its unique service name. Both
		// are idempotent (starting a running tunnel succeeds without a second
		// bind) and never touch another tunnel that shares a destination.
		// `error` receives a non-secret description when the call fails.
		bool StartTunnel (std::string_view name, std::string & error);
		bool StopTunnel (std::string_view name, std::string & error);

		std::vector<std::shared_ptr<DatagramSessionInfo>> GetForwardInfosFor (const i2p::data::IdentHash & destination);

		// i18n
		std::shared_ptr<const i2p::i18n::Locale> GetLanguage () { return m_Language; };
		void SetLanguage (const std::shared_ptr<const i2p::i18n::Locale> language) { m_Language = language; };

	private:
		void ReadTunnels ();
		void ReadTunnels (const std::string & tunConf, int & numClientTunnels, int & numServerTunnels);
		int ReadTunnelsBySection (const std::string & tunConf, boost::property_tree::ptree & pt);
		void AddSection (
			boost::property_tree::ptree & pt, const std::string & name, const std::string & chunk, int & numSections);
		void ReadHttpProxy ();
		void ReadSocksProxy ();
		// take the proxy out under the lock, so it can be stopped without holding it
		std::shared_ptr<I2PService> DetachHttpProxy ();
		std::shared_ptr<I2PService> DetachSocksProxy ();
		template <typename Section, typename Type>
		std::string GetI2CPOption (const Section & section, const std::string & name, const Type & value) const;
		template <typename Section>
		std::string GetI2CPStringOption (const Section & section, const std::string & name,
			const std::string & value) const; // GetI2CPOption with string default value
		template <typename Section>
		void ReadI2CPOptionsGroup (
			const Section & section, const std::string & group, i2p::util::Mapping & options) const;
		template <typename Section>
		void ReadI2CPOptions (
			const Section & section, bool isServer, i2p::util::Mapping & options) const; // for tunnels
		void ReadI2CPOptionsFromConfig (
			const std::string & prefix, i2p::util::Mapping & options) const; // for HTTP and SOCKS proxy

		void VisitTunnels (bool clean);

		void CreateNewSharedLocalDestination ();
		void AddLocalDestination (std::shared_ptr<ClientDestination> localDestination);

#ifndef NO_TORRENTS
		std::shared_ptr<i2p::torrents::TorrentsRPCServer> CreateTorrentsRPCServer (
			std::string_view address, uint16_t port);
#endif

	private:
		mutable std::mutex m_DestinationsMutex;
		std::map<i2p::data::IdentHash, std::shared_ptr<ClientDestination>> m_Destinations;
		std::shared_ptr<ClientDestination> m_SharedLocalDestination;

		AddressBook m_AddressBook;

		mutable std::mutex m_ProxyMutex;
		std::shared_ptr<I2PService> m_HttpProxy, m_SocksProxy;
		std::map<boost::asio::ip::tcp::endpoint, std::shared_ptr<I2PService>>
			m_ClientTunnels; // local endpoint -> tunnel
		std::map<std::pair<i2p::data::IdentHash, int>, std::shared_ptr<I2PServerTunnel>>
			m_ServerTunnels; // <destination,port> -> tunnel

		mutable std::mutex m_ForwardsMutex;
		std::map<boost::asio::ip::udp::endpoint, std::shared_ptr<I2PUDPClientTunnel>>
			m_ClientForwards; // local endpoint -> udp tunnel
		std::map<std::pair<i2p::data::IdentHash, int>, std::shared_ptr<I2PUDPServerTunnel>>
			m_ServerForwards; // <destination,port> -> udp tunnel

		TunnelServiceNameRegistry m_ServiceNames;

#ifndef NO_TORRENTS
		std::unordered_map<i2p::data::IdentHash, std::shared_ptr<i2p::torrents::TorrentsTunnel>>
			m_TorrentsTunnels; // local destination -> torrents tunnel
		mutable std::mutex m_TorrentsTunnelsMutex;
		std::unordered_map<uint16_t, std::shared_ptr<i2p::torrents::TorrentsRPCServer>> m_TorrentsRPCServers;
#endif

		std::unique_ptr<SAMBridge> m_SamBridge;
		std::unique_ptr<BOBCommandChannel> m_BOBCommandChannel;
		std::unique_ptr<I2CPServer> m_I2CPServer;

		// i18n
		std::shared_ptr<const i2p::i18n::Locale> m_Language;

	public:
		// for HTTP; thread-safe snapshot, safe to iterate without holding any lock
		std::vector<std::pair<i2p::data::IdentHash, std::shared_ptr<ClientDestination>>> GetDestinationsList () const
		{
			std::lock_guard<std::mutex> l (m_DestinationsMutex);
			return std::vector<std::pair<i2p::data::IdentHash, std::shared_ptr<ClientDestination>>> (
				m_Destinations.begin (), m_Destinations.end ());
		}
		// Legacy reference accessors. They are kept for source compatibility
		// with the read-only admin/TUI consumers, but a returned reference is
		// not safe while ReloadConfig()/Stop() can run. New code must use the
		// snapshot methods below, which copy under the corresponding mutex.
		const decltype (m_ClientTunnels) & GetClientTunnels () const { return m_ClientTunnels; };
		const decltype (m_ServerTunnels) & GetServerTunnels () const { return m_ServerTunnels; };
		const decltype (m_ClientForwards) & GetClientForwards () const { return m_ClientForwards; }
		const decltype (m_ServerForwards) & GetServerForwards () const { return m_ServerForwards; }

		std::vector<std::pair<boost::asio::ip::tcp::endpoint, std::shared_ptr<I2PService>>>
		GetClientTunnelsSnapshot () const
		{
			std::lock_guard<std::mutex> l (m_ProxyMutex);
			return { m_ClientTunnels.begin (), m_ClientTunnels.end () };
		}
		std::vector<std::pair<std::pair<i2p::data::IdentHash, int>, std::shared_ptr<I2PServerTunnel>>>
		GetServerTunnelsSnapshot () const
		{
			std::lock_guard<std::mutex> l (m_ProxyMutex);
			return { m_ServerTunnels.begin (), m_ServerTunnels.end () };
		}
		std::vector<std::pair<boost::asio::ip::udp::endpoint, std::shared_ptr<I2PUDPClientTunnel>>>
		GetClientForwardsSnapshot () const
		{
			std::lock_guard<std::mutex> l (m_ForwardsMutex);
			return { m_ClientForwards.begin (), m_ClientForwards.end () };
		}
		std::vector<std::pair<std::pair<i2p::data::IdentHash, int>, std::shared_ptr<I2PUDPServerTunnel>>>
		GetServerForwardsSnapshot () const
		{
			std::lock_guard<std::mutex> l (m_ForwardsMutex);
			return { m_ServerForwards.begin (), m_ServerForwards.end () };
		}
#ifndef NO_TORRENTS
		// Stable, sorted copy of the configured torrents tunnels. Callers
		// iterate this without holding any ClientContext lock; the TUI
		// tunnel index is an index into this vector.
		std::vector<std::shared_ptr<i2p::torrents::TorrentsTunnel>> GetTorrentsTunnelsSnapshot () const
		{
			std::lock_guard<std::mutex> l (m_TorrentsTunnelsMutex);
			std::vector<std::shared_ptr<i2p::torrents::TorrentsTunnel>> snapshot;
			snapshot.reserve (m_TorrentsTunnels.size ());
			for (const auto & it : m_TorrentsTunnels)
				if (it.second) snapshot.push_back (it.second);
			std::sort (snapshot.begin (), snapshot.end (),
				[] (const auto & first, const auto & second)
				{
					auto firstName = std::string_view (first->GetName ());
					auto secondName = std::string_view (second->GetName ());
					if (firstName != secondName) return firstName < secondName;
					return first.get () < second.get ();
				});
			return snapshot;
		}
		// Legacy reference accessor; see the tunnel accessors above.
		const decltype (m_TorrentsRPCServers) & GetTorrentsRPCServers () const { return m_TorrentsRPCServers; }
		std::vector<std::shared_ptr<i2p::torrents::TorrentsRPCServer>> GetTorrentsRPCServersSnapshot () const
		{
			std::lock_guard<std::mutex> l (m_TorrentsTunnelsMutex);
			std::vector<std::shared_ptr<i2p::torrents::TorrentsRPCServer>> snapshot;
			snapshot.reserve (m_TorrentsRPCServers.size ());
			for (const auto & item : m_TorrentsRPCServers)
				if (item.second) snapshot.push_back (item.second);
			return snapshot;
		}
#endif
		std::shared_ptr<const I2PService> GetHttpProxy () const
		{
			std::lock_guard<std::mutex> l (m_ProxyMutex);
			return m_HttpProxy;
		}
		std::shared_ptr<const I2PService> GetSocksProxy () const
		{
			std::lock_guard<std::mutex> l (m_ProxyMutex);
			return m_SocksProxy;
		}
	};

	extern ClientContext context;
}
}

#endif

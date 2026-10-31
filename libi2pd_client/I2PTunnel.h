/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: I2PTunnel.
 *
 * Declares the local-socket/I2P stream connection classes, their HTTP
 * and IRC variants, client keep-alive and server-side access-list
 * handling.
 *
 */

#ifndef I2PTUNNEL_H__
#define I2PTUNNEL_H__

#include <inttypes.h>
#include <atomic>
#include <functional>
#include <string>
#include <string_view>
#include "I2PTunnelHTTP.h"
#include "I2PTunnelAccessFilter.h"
#include <map>
#include <mutex>
#include <set>
#include <vector>
#include <tuple>
#include <utility>
#include <memory>
#include <sstream>
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include "Identity.h"
#include "Destination.h"
#include "Streaming.h"
#include "I2PService.h"
#include "AddressBook.h"

namespace i2p
{
namespace client
{
	constexpr size_t I2P_TUNNEL_CONNECTION_BUFFER_SIZE = 8192;
	constexpr size_t I2P_TUNNEL_CONNECTION_STREAM_MAX_SEND_BUFFER_SIZE = 8 * I2P_TUNNEL_CONNECTION_BUFFER_SIZE;
	constexpr size_t I2P_TUNNEL_CONNECTION_STREAM_BUFFER_SIZE = 16384;
	constexpr int I2P_TUNNEL_CONNECTION_MAX_IDLE = 3600; // in seconds
	constexpr int I2P_TUNNEL_DESTINATION_REQUEST_TIMEOUT = 10; // in seconds
	// Server-tunnel option bounds (Java i2ptunnel defaults).
	constexpr size_t I2P_SERVER_TUNNEL_MAX_PORT_TARGETS = 16;
	constexpr int I2P_SERVER_TUNNEL_DEFAULT_POST_MAX = 6;
	constexpr uint64_t I2P_SERVER_TUNNEL_POST_WINDOW = 60; // in seconds
	// for HTTP tunnels
	constexpr std::string_view X_I2P_DEST_HASH { "X-I2P-DestHash" }; // hash in base64
	constexpr std::string_view X_I2P_DEST_B64 { "X-I2P-DestB64" }; // full address in base64
	constexpr std::string_view X_I2P_DEST_B32 { "X-I2P-DestB32" }; // .b32.i2p address

	class I2PServerTunnelHTTPPolicy;
	class I2PServerTunnelTargetMap;

	/// Bridges a local TCP socket and an I2P stream, optionally through
	/// TLS. The service holds the handler while its reverse reference is
	/// weak; Terminate closes its transports and removes the handler from
	/// that owner.
	class I2PTunnelConnection : public I2PServiceHandler, public std::enable_shared_from_this<I2PTunnelConnection>
	{
	public:
		I2PTunnelConnection (std::shared_ptr<I2PService> owner, std::shared_ptr<boost::asio::ip::tcp::socket> socket,
			std::shared_ptr<const i2p::data::LeaseSet> leaseSet, uint16_t port = 0); // to I2P
		I2PTunnelConnection (std::shared_ptr<I2PService> owner, std::shared_ptr<boost::asio::ip::tcp::socket> socket,
			std::shared_ptr<i2p::stream::Stream> stream); // to I2P using simplified API
		I2PTunnelConnection (std::shared_ptr<I2PService> owner, std::shared_ptr<i2p::stream::Stream> stream,
			const boost::asio::ip::tcp::endpoint & target,
			std::shared_ptr<boost::asio::ssl::context> sslCtx = nullptr); // from I2P
		~I2PTunnelConnection ();
		/// Start the stream and bidirectional receive paths.
		/// @warning Without @a msg, currently available local TCP
		///          bytes can be read synchronously; that initial read has no
		///          explicit operation deadline.
		void I2PConnect (const uint8_t * msg = nullptr, size_t len = 0);
		void Connect (bool isUniqueLocal = true);
		void Connect (const boost::asio::ip::address & localAddress);

	protected:
		virtual void Established ();
		// Virtual teardown: it owns the socket/stream/TLS objects, so a
		// ClearHandlers() call through I2PServiceHandler must reach it.
		void Terminate () override;

		void Receive ();
		void StreamReceive ();
		void HandleStreamReceive (const boost::system::error_code & ecode, std::size_t bytes_transferred);
		virtual void Write (const uint8_t * buf, size_t len); // can be overloaded
		virtual void WriteToStream (const uint8_t * buf, size_t len); // can be overloaded

		std::shared_ptr<boost::asio::ip::tcp::socket> GetSocket () const { return m_Socket; };
		std::shared_ptr<i2p::stream::Stream> GetStream () const { return m_Stream; };
		std::shared_ptr<boost::asio::ssl::stream<boost::asio::ip::tcp::socket &>> GetSSL () const { return m_SSL; };
		uint8_t * GetStreamBuffer () { return m_StreamBuffer; };

	private:
		void HandleConnect (const boost::system::error_code & ecode);
		void HandleHandshake (const boost::system::error_code & ecode);
		void HandleReceive (const boost::system::error_code & ecode, std::size_t bytes_transferred);
		void HandleWrite (const boost::system::error_code & ecode);

	private:
		uint8_t m_Buffer[I2P_TUNNEL_CONNECTION_BUFFER_SIZE], m_StreamBuffer[I2P_TUNNEL_CONNECTION_STREAM_BUFFER_SIZE];
		std::shared_ptr<boost::asio::ip::tcp::socket> m_Socket;
		std::shared_ptr<boost::asio::ssl::stream<boost::asio::ip::tcp::socket &>> m_SSL;
		std::shared_ptr<i2p::stream::Stream> m_Stream;
		boost::asio::ip::tcp::endpoint m_RemoteEndpoint;
		bool m_IsReceiving;
	};

	/// Client-side HTTP tunnel connection: it parses the first header
	/// block it forwards towards the local socket with the ClientRequest
	/// mode, so the bytes are bounded and carry a single `Connection:
	/// close`.
	/// @warning The transform is applied in the stream-to-local
	///          direction, and it is the *request* transform; do not reuse
	///          this class for a proxied response, where a ClientResponse
	///          transform is required. The HTTP proxy therefore uses its
	///          own connection type for that direction.
	class I2PClientTunnelConnectionHTTP : public I2PTunnelConnection
	{
	public:
		I2PClientTunnelConnectionHTTP (std::shared_ptr<I2PService> owner,
			std::shared_ptr<boost::asio::ip::tcp::socket> socket, std::shared_ptr<i2p::stream::Stream> stream)
			: I2PTunnelConnection (owner, socket, stream)
			, m_RequestHeader (I2PTunnelHTTPHeader::Mode::ClientRequest) { };

	protected:
		void Write (const uint8_t * buf, size_t len) override;

	private:
		I2PTunnelHTTPHeader m_RequestHeader;
	};

	/// Server-side HTTP tunnel connection: injects the configured host /
	/// X-I2P identity header, evaluates the request policy and, when it
	/// refuses, writes exactly one generated response before closing.
	/// @warning The rejection response is derived only from the
	///          policy decision and a status/reason pair; the client's
	///          request line, target and headers are never echoed, so a
	///          refused request cannot be reflected back into the
	///          response.
	class I2PServerTunnelConnectionHTTP : public I2PTunnelConnection
	{
	public:
		I2PServerTunnelConnectionHTTP (std::shared_ptr<I2PService> owner, std::shared_ptr<i2p::stream::Stream> stream,
			const boost::asio::ip::tcp::endpoint & target, const std::string & host, const std::string & XI2P,
			std::shared_ptr<boost::asio::ssl::context> sslCtx = nullptr,
			std::shared_ptr<I2PServerTunnelHTTPPolicy> policy = nullptr);

	protected:
		void Write (const uint8_t * buf, size_t len) override;
		void WriteToStream (const uint8_t * buf, size_t len) override;

	private:
		// Answers a request refused by the tunnel policy and closes the
		// stream.
		void RejectRequest (int status, std::string_view reason);

		I2PTunnelHTTPHeader m_RequestHeader, m_ResponseHeader;
		std::shared_ptr<I2PServerTunnelHTTPPolicy> m_Policy;
		bool m_RequestRejected = false;
	};

	/// IRC server-tunnel connection: it prefixes the first outbound line
	/// with a WEBIRC command (`WEBIRC &lt;pass&gt; cgiirc
	/// &lt;destination&gt; &lt;local address&gt;`) and replaces the
	/// hostname field of a `USER` line with the peer destination, so the
	/// upstream ircd attributes the session to the I2P identity instead of
	/// the tunnel's local address.
	/// @warning The WEBIRC password is a credential. It is
	///          written only into that outbound line and must never reach
	///          a log, an error page or a diagnostic string.
	class I2PTunnelConnectionIRC : public I2PTunnelConnection
	{
	public:
		I2PTunnelConnectionIRC (std::shared_ptr<I2PService> owner, std::shared_ptr<i2p::stream::Stream> stream,
			const boost::asio::ip::tcp::endpoint & target, const std::string & m_WebircPass,
			std::shared_ptr<boost::asio::ssl::context> sslCtx = nullptr);

	protected:
		void Write (const uint8_t * buf, size_t len) override;

	private:
		std::shared_ptr<const i2p::data::IdentityEx> m_From;
		std::stringstream m_OutPacket, m_InPacket;
		bool m_NeedsWebIrc;
		std::string m_WebircPass;
	};

	/// Client tunnel (local TCP listener to an I2P destination).
	///
	/// One configured section may name several destinations: every
	/// accepted local socket is sent to the next resolvable destination in
	/// round-robin order, and an unresolvable entry is skipped so one dead
	/// server cannot block the whole listener. An optional keep-alive
	/// timer pings every resolved destination so the spare ones do not go
	/// cold while the primary carries the traffic.
	///
	/// The address list is resolved lazily and cached for the running
	/// tunnel; a name that could not be resolved is retried on the next
	/// accepted socket, and Stop() clears the cache.
	class I2PClientTunnel : public TCPIPAcceptor
	{
	protected:
		// Implements TCPIPAcceptor
		std::shared_ptr<I2PServiceHandler> CreateHandler (
			std::shared_ptr<boost::asio::ip::tcp::socket> socket) override;

	public:
		I2PClientTunnel (const std::string & name, const std::string & destination, const std::string & address,
			uint16_t port, std::shared_ptr<ClientDestination> localDestination, uint16_t destinationPort = 0);
		~I2PClientTunnel () { }

		void Start () override;
		void Stop () override;

		const char * GetName () const override { return m_Name.c_str (); }
		void SetKeepAliveInterval (uint32_t keepAliveInterval);
		// Configured destination names, in round-robin order, for the
		// administrative inventory.
		const std::vector<std::string> & GetDestinations () const { return m_Destinations; }

	private:
		std::shared_ptr<const Address> GetAddress ();

		void ScheduleKeepAliveTimer ();
		void HandleKeepAliveTimer (const boost::system::error_code & ecode);

	private:
		std::string m_Name;
		std::vector<std::string> m_Destinations;
		std::vector<std::shared_ptr<const Address>> m_Addresses; // resolved lazily, same order
		size_t m_NextAddress;
		uint16_t m_DestinationPort;
		uint32_t m_KeepAliveInterval;
		std::unique_ptr<boost::asio::steady_timer> m_KeepAliveTimer;
	};

	// Immutable, bounded `targetForPort.&lt;port&gt;=host:port` map. At
	// most I2P_SERVER_TUNNEL_MAX_PORT_TARGETS entries; malformed values,
	// duplicate local ports and out-of-range ports are rejected at
	// configuration load.
	class I2PServerTunnelTargetMap
	{
	public:
		struct Entry
		{
			std::string host;
			uint16_t port = 0;
		};
		struct Selection
		{
			enum class Status
			{
				eDefault,
				eTarget,
				eReset
			};
			Status status = Status::eDefault;
			std::string host;
			uint16_t port = 0;
		};

		bool Add (uint16_t localPort, std::string host, uint16_t port, std::string & error);
		// Parses one `host:port` specification; `[v6]:port` is accepted.
		bool AddSpec (uint16_t localPort, std::string_view specification, std::string & error);
		Selection Select (uint16_t acceptedLocalPort) const;
		// Java requires an explicit mapping for these local ports.
		static bool IsReservedLocalPort (uint16_t localPort) { return localPort == 443 || localPort == 22; }
		bool Empty () const { return m_Targets.empty (); }
		size_t Size () const { return m_Targets.size (); }
		const std::map<uint16_t, Entry> & Entries () const { return m_Targets; }

	private:
		std::map<uint16_t, Entry> m_Targets;
	};

	/// Server tunnel (I2P destination to a local TCP service).
	///
	/// The tunnel registers one acceptor on a streaming destination,
	/// either the port-specific one (`inport`) or the destination-wide
	/// default when `defaultacceptor=true`. A port-specific tunnel never
	/// adopts the default destination implicitly: doing so would silently
	/// serve every unregistered port. The acceptor callback holds a weak
	/// owner plus the start generation, so a completion from an earlier
	/// run answers with a RESET instead of reaching a stopped tunnel.
	///
	/// Admission is decided before a stream exists: the optional
	/// `filterDefinition` predicate runs inside the SYN path, and a denial
	/// is answered with either silence or one signed RESET according to
	/// `limitAction`. Stop() (the forced form) additionally closes the
	/// live stream/socket pairs; Stop(false) leaves them to drain.
	class I2PServerTunnel : public I2PService
	{
	public:
		I2PServerTunnel (const std::string & name, const std::string & address, uint16_t port,
			std::shared_ptr<ClientDestination> localDestination, uint16_t inport = 0, bool gzip = true);

		void Start () override;
		void Stop () override;
		// Java I2PTunnelServer.stop(forced): stop accepting in both cases;
		// close the live stream/socket pairs only when forced or when nothing
		// is in use. Stop() is the forced form used by daemon shutdown.
		void Stop (bool forced);
		bool IsInUse () const { return GetNumHandlers () > 0; }

		void SetAccessList (const std::set<i2p::data::IdentHash> & accessList);
		// Installs the parsed `filterDefinition` and its rejection action.
		// The predicate runs inside the streaming SYN path, before any stream
		// is created; `limitAction=reset` answers a denial with a signed
		// RESET, anything else (the default) keeps the peer in silence.
		void SetAccessFilter (std::shared_ptr<const FilterDefinition> definition, std::string_view limitAction);
		void SetLimitAction (std::string_view limitAction);
		// Wire action used for a refused SYN (`limitAction`). Filter denials
		// default to a silent drop; `limitAction=reset` opts into a signed
		// RESET.
		TunnelAccessRejectAction GetAccessRejectAction () const { return m_AccessRejectAction; }

		void SetTargets (const I2PServerTunnelTargetMap & targets);
		const I2PServerTunnelTargetMap & GetTargets () const { return m_Targets; }
		void SetDefaultAcceptor (bool isDefaultAcceptor) { m_IsDefaultAcceptor = isDefaultAcceptor; }
		bool IsDefaultAcceptor () const { return m_IsDefaultAcceptor; }

		void SetUniqueLocal (bool isUniqueLocal) { m_IsUniqueLocal = isUniqueLocal; }
		bool IsUniqueLocal () const { return m_IsUniqueLocal; }

		void SetSSL (bool ssl);
		std::shared_ptr<boost::asio::ssl::context> GetSSLCtx () const { return m_SSLCtx; };

		void SetLocalAddress (const std::string & localAddress);

		const std::string & GetAddress () const { return m_Address; }
		uint16_t GetPort () const { return m_Port; };
		uint16_t GetLocalPort () const { return m_PortDestination->GetLocalPort (); };
		const boost::asio::ip::tcp::endpoint & GetEndpoint () const { return m_Endpoint; }
		// Local target for a stream accepted on `localPort`. Returns false
		// when the port has no usable target and the stream must be reset:
		// Java requires an explicit `targetForPort` mapping for 443 and 22.
		bool SelectTarget (uint16_t localPort, boost::asio::ip::tcp::endpoint & target) const;

		const char * GetName () const override { return m_Name.c_str (); }

		void Accept ();
		void DetachAcceptor (); // called before the tunnels are read again on reload

	protected:
		virtual void HandleAccept (std::shared_ptr<i2p::stream::Stream> stream);
		void Connect (std::shared_ptr<i2p::stream::Stream> stream, const boost::asio::ip::tcp::endpoint & target);
		virtual std::shared_ptr<I2PTunnelConnection> CreateI2PConnection (
			std::shared_ptr<i2p::stream::Stream> stream, const boost::asio::ip::tcp::endpoint & target);

	private:
		bool Resolve (std::shared_ptr<i2p::stream::Stream> stream);
		void HandleResolve (const boost::system::error_code & ecode,
			boost::asio::ip::tcp::resolver::results_type endpoints, std::shared_ptr<i2p::stream::Stream> stream);
		// Registers the acceptor and admission predicate on one streaming
		// destination. The lambda holds a weak owner plus the start
		// generation.
		void InstallAcceptor (const std::shared_ptr<i2p::stream::StreamingDestination> & destination);
		void InstallAdmissionPolicy (const std::shared_ptr<i2p::stream::StreamingDestination> & destination);
		// Resolves `targetForPort` hostnames asynchronously; a nameless (IP
		// literal) entry is installed immediately.
		void ResolveTargets ();
		void HandleTargetResolve (const boost::system::error_code & ecode,
			boost::asio::ip::tcp::resolver::results_type endpoints, uint16_t localPort);

	private:
		bool m_IsUniqueLocal;
		bool m_IsDefaultAcceptor; // this tunnel is the one accepting on the destination
		bool m_HasAcceptor;
		std::string m_Name, m_Address;
		uint16_t m_Port;
		boost::asio::ip::tcp::endpoint m_Endpoint;
		std::shared_ptr<i2p::stream::StreamingDestination> m_PortDestination;
		// The default streaming destination when `defaultacceptor=true` made
		// a port-specific tunnel take it over as well.
		std::shared_ptr<i2p::stream::StreamingDestination> m_DefaultAcceptorDestination;
		std::set<i2p::data::IdentHash> m_AccessList;
		bool m_IsAccessList;
		std::shared_ptr<const FilterDefinition> m_AccessFilterDefinition;
		std::shared_ptr<TunnelAccessFilter> m_AccessFilter;
		TunnelAccessRejectAction m_AccessRejectAction;
		bool m_LimitActionConfigured;
		I2PServerTunnelTargetMap m_Targets;
		mutable std::mutex m_TargetsMutex;
		std::map<uint16_t, boost::asio::ip::tcp::endpoint> m_ResolvedTargets;
		std::vector<std::shared_ptr<boost::asio::ip::tcp::resolver>> m_TargetResolvers;
		std::atomic<uint64_t> m_Generation;
		std::unique_ptr<boost::asio::ip::address> m_LocalAddress;
		std::shared_ptr<boost::asio::ssl::context> m_SSLCtx;
		std::shared_ptr<boost::asio::ip::tcp::resolver> m_Resolver;
	};

	enum class I2PServerTunnelRequestDecision
	{
		eAllow,
		eForbidden, // rejectInproxy / rejectReferer / rejectUserAgents
		eTooManyRequests // POST/PUT throttle
	};

	// Server-tunnel HTTP request policy: `spoofedhost.&lt;port&gt;` with a
	// `hostoverride` fallback, the inproxy/referer/user-agent rejects and
	// the bounded POST/PUT throttle. Header names and values are never
	// logged.
	class I2PServerTunnelHTTPPolicy
	{
	public:
		void SetHostOverride (std::string host)
		{
			std::lock_guard<std::mutex> lock (m_Mutex);
			m_HostOverride = std::move (host);
		}
		bool SetSpoofedHost (uint16_t port, std::string host, std::string & error);
		// The spoofed host for the accepted local port, else the override.
		std::string HostFor (uint16_t port) const;

		void SetRejectInproxy (bool reject)
		{
			std::lock_guard<std::mutex> lock (m_Mutex);
			m_RejectInproxy = reject;
		}
		void SetRejectReferer (bool reject)
		{
			std::lock_guard<std::mutex> lock (m_Mutex);
			m_RejectReferer = reject;
		}
		// Comma separated, case-insensitive substrings.
		bool SetRejectUserAgents (std::string_view list, std::string & error);
		void SetPostLimit (int perWindow);

		int GetPostLimit () const
		{
			std::lock_guard<std::mutex> lock (m_Mutex);
			return m_PostLimit;
		}
		bool IsRejectInproxy () const
		{
			std::lock_guard<std::mutex> lock (m_Mutex);
			return m_RejectInproxy;
		}
		bool IsRejectReferer () const
		{
			std::lock_guard<std::mutex> lock (m_Mutex);
			return m_RejectReferer;
		}
		size_t GetNumRejectUserAgents () const
		{
			std::lock_guard<std::mutex> lock (m_Mutex);
			return m_RejectUserAgents.size ();
		}

		// Inspects one complete transformed request header (request line and
		// header lines, CRLF or LF terminated).
		I2PServerTunnelRequestDecision Check (std::string_view header, uint64_t nowSeconds);

	private:
		static constexpr size_t MAX_SPOOFED_HOSTS = 16;
		static constexpr size_t MAX_REJECT_USER_AGENTS = 32;
		static constexpr size_t MAX_PATTERN_LENGTH = 256;

		mutable std::mutex m_Mutex;
		std::map<uint16_t, std::string> m_SpoofedHosts;
		std::string m_HostOverride;
		std::vector<std::string> m_RejectUserAgents; // lowercased substrings
		bool m_RejectInproxy = false, m_RejectReferer = false;
		int m_PostLimit = I2P_SERVER_TUNNEL_DEFAULT_POST_MAX;
		uint64_t m_PostWindowStart = 0;
		uint32_t m_PostCount = 0;
	};

	class I2PServerTunnelHTTP : public I2PServerTunnel
	{
	public:
		I2PServerTunnelHTTP (const std::string & name, const std::string & address, uint16_t port,
			std::shared_ptr<ClientDestination> localDestination, const std::string & host, uint16_t inport = 0,
			bool gzip = true, bool i2pheaders = true);

		void SetHostOverride (const std::string & host);
		bool SetSpoofedHost (uint16_t port, const std::string & host, std::string & error);
		void SetRejectInproxy (bool reject);
		void SetRejectReferer (bool reject);
		bool SetRejectUserAgents (const std::string & list, std::string & error);
		void SetPostLimit (int perWindow);
		const I2PServerTunnelHTTPPolicy & GetPolicy () const { return *m_Policy; }

	protected:
		std::shared_ptr<I2PTunnelConnection> CreateI2PConnection (
			std::shared_ptr<i2p::stream::Stream> stream, const boost::asio::ip::tcp::endpoint & target) override;

	private:
		std::string m_Host, m_XI2P;
		std::weak_ptr<const i2p::data::IdentityEx> m_From;
		bool m_I2PHeaders;
		std::shared_ptr<I2PServerTunnelHTTPPolicy> m_Policy;
	};

	class I2PServerTunnelIRC : public I2PServerTunnel
	{
	public:
		I2PServerTunnelIRC (const std::string & name, const std::string & address, uint16_t port,
			std::shared_ptr<ClientDestination> localDestination, const std::string & webircpass, uint16_t inport = 0,
			bool gzip = true);

	protected:
		std::shared_ptr<I2PTunnelConnection> CreateI2PConnection (
			std::shared_ptr<i2p::stream::Stream> stream, const boost::asio::ip::tcp::endpoint & target) override;

	private:
		std::string m_WebircPass;
	};

	boost::asio::ip::address GetLoopbackAddressFor (const i2p::data::IdentHash & addr);
}
}

#endif

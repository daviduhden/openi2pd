/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: TorrentsRPC.
 *
 * Declares the loopback JSON-RPC HTTP server, its per-path tunnel/auth
 * registry and the request session used by Transmission-style RPC clients.
 *
 */

#ifndef TORRENTS_RPC_H__
#define TORRENTS_RPC_H__

#ifndef NO_TORRENTS

#include <inttypes.h>
#include <memory>
#include <string>
#include <string_view>
#include <map>
#include <mutex>
#include <utility>
#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include "util.h"

namespace i2p
{
namespace torrents
{
	class TorrentsRPCServer;
	class TorrentsTunnel;
	/// One loopback HTTP request/response. Asio handlers retain the session via
	/// shared ownership; the referenced server must outlive all such handlers.
	class TorrentsRPCSession : public std::enable_shared_from_this<TorrentsRPCSession>
	{
	public:
		TorrentsRPCSession (TorrentsRPCServer & server, boost::asio::ip::tcp::socket && s);

		void ReceiveRequest ();

	private:
		void HandleRequest (const boost::system::error_code & ecode, size_t bytes_transferred);
		void SendResponse (boost::beast::http::status result, std::string_view data = "");
		void SendUnauthorizedResponse ();

	private:
		TorrentsRPCServer & m_Server;
		boost::asio::ip::tcp::socket m_Socket;
		boost::beast::flat_buffer m_ReceiveBuffer;
		boost::beast::http::request_parser<boost::beast::http::string_body> m_RequestParser;
		boost::beast::http::response<boost::beast::http::string_body> m_Response;
	};

	/// Loopback-only Transmission-style RPC listener with optional per-path
	/// Basic authentication. The registry holds weak tunnel references.
	/// @warning Stop cancels acceptance and stops the service but does not track
	/// and close accepted sessions; session admission and IO deadlines are not
	/// currently bounded. The server must outlive pending callbacks.
	class TorrentsRPCServer final : private i2p::util::RunnableServiceWithWork
	{
	public:
		TorrentsRPCServer (std::string_view address, uint16_t port);

		auto & GetService () { return GetIOService (); }
		void Start ();
		void Stop ();

		/// Register (or replace) the tunnel served at one loopback path.
		/// @param auth Require HTTP Basic authentication for that path.
		/// @param user/pass Bounded non-empty credentials; an empty pair with
		/// @a auth set is rejected and no entry is installed.
		/// @warning The registry holds a weak tunnel reference, so the caller
		/// owns the tunnel lifetime. The credential is stored as a Basic header
		/// value and must never be logged or included in a snapshot.
		void AddTunnel (std::string_view path, std::shared_ptr<TorrentsTunnel> tunnel, bool auth = false,
			const std::string & user = "", const std::string & pass = "");
		std::shared_ptr<TorrentsTunnel> GetTunnel (std::string_view path) const;
		/// Return whether the path needs authentication and its credential value.
		/// The returned string is sensitive and must not be logged.
		std::pair<bool, std::string> GetAuth (std::string_view path) const;

		std::pair<boost::asio::ip::tcp::endpoint, std::string> GetTunnelEndpoint (
			std::shared_ptr<TorrentsTunnel> tunnel) const;

		/// Borrow the registry without locking. The caller must keep the server
		/// alive and prevent concurrent registration while using this reference.
		const auto & GetTunnels () const { return m_Tunnels; }
		const auto & GetAcceptor () const { return m_Acceptor; }

	private:
		void Accept ();

	private:
		boost::asio::ip::tcp::acceptor m_Acceptor;
		mutable std::mutex m_TunnelsMutex;
		std::map<std::string, std::weak_ptr<TorrentsTunnel>, std::less<>> m_Tunnels; // path->tunnel
		std::map<std::string, std::string, std::less<>> m_Auth; // path-> basic authorization string
	};
}
}

#endif

#endif

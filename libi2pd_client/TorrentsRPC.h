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

	class TorrentsRPCServer final : private i2p::util::RunnableServiceWithWork
	{
	public:
		TorrentsRPCServer (std::string_view address, uint16_t port);

		auto & GetService () { return GetIOService (); }
		void Start ();
		void Stop ();

		void AddTunnel (std::string_view path, std::shared_ptr<TorrentsTunnel> tunnel, bool auth = false,
			const std::string & user = "", const std::string & pass = "");
		std::shared_ptr<TorrentsTunnel> GetTunnel (std::string_view path) const;
		std::pair<bool, std::string> GetAuth (std::string_view path) const; // return (requered, auth string)

		std::pair<boost::asio::ip::tcp::endpoint, std::string> GetTunnelEndpoint (
			std::shared_ptr<TorrentsTunnel> tunnel) const;

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

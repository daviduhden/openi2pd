/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: TorrentsRPC.
 *
 * Declares the bounded loopback JSON-RPC listener and request session.
 */

#ifndef TORRENTS_RPC_H__
#define TORRENTS_RPC_H__

#ifndef NO_TORRENTS

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <mutex>
#include <utility>
#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include "util.h"

namespace i2p
{
namespace torrents
{
	struct TorrentsRPCState;
	class TorrentsTunnel;

	/// One bounded request. Completions own the session, and the weak
	/// state reference never extends the listener's admission lifetime.
	class TorrentsRPCSession : public std::enable_shared_from_this<TorrentsRPCSession>
	{
	public:
		TorrentsRPCSession (const std::shared_ptr<TorrentsRPCState> & state,
			uint64_t id, boost::asio::ip::tcp::socket && socket);
		void ReceiveRequest ();
		/// Cancel socket and timer on the service thread or after its join.
		void Close ();

	private:
		void ArmDeadline ();
		void HandleRequest (const boost::system::error_code & ecode, size_t bytes_transferred);
		void SendResponse (boost::beast::http::status result, std::string_view data = "");
		void SendUnauthorizedResponse ();

	private:
		std::weak_ptr<TorrentsRPCState> m_State;
		uint64_t m_Id;
		bool m_Closed = false;
		boost::asio::ip::tcp::socket m_Socket;
		boost::asio::steady_timer m_Deadline;
		boost::beast::flat_buffer m_ReceiveBuffer;
		boost::beast::http::request_parser<boost::beast::http::string_body> m_RequestParser;
		boost::beast::http::response<boost::beast::http::string_body> m_Response;
	};

	/// Loopback RPC with bounded sessions and read/write deadlines.
	/// Start/Stop are serialized. Stop closes admission, joins the worker,
	/// closes all sockets, and drains completions before another Start.
	/// Call lifecycle methods from the owning control thread.
	class TorrentsRPCServer final : private i2p::util::RunnableServiceWithWork
	{
	public:
		static constexpr size_t MAX_SESSIONS = 16;
		TorrentsRPCServer (std::string_view address, uint16_t port);
		~TorrentsRPCServer ();
		void Start ();
		void Stop ();
		boost::asio::ip::tcp::endpoint GetEndpoint () const;
		size_t GetSessionCount () const;

		/// Atomically replace both path spellings and their authentication.
		/// The tunnel remains weakly owned. Credentials are bounded and
		/// must never be logged or included in status snapshots.
		void AddTunnel (std::string_view path, std::shared_ptr<TorrentsTunnel> tunnel, bool auth = false,
			const std::string & user = "", const std::string & pass = "");
		std::shared_ptr<TorrentsTunnel> GetTunnel (std::string_view path) const;
		std::pair<bool, std::string> GetAuth (std::string_view path) const;
		std::pair<boost::asio::ip::tcp::endpoint, std::string> GetTunnelEndpoint (
			std::shared_ptr<TorrentsTunnel> tunnel) const;

	private:
		auto & GetService () { return GetIOService (); }
		static void Accept (const std::shared_ptr<TorrentsRPCState> & state);
		std::mutex m_LifecycleMutex;
		std::shared_ptr<TorrentsRPCState> m_State;
	};
}
}

#endif
#endif

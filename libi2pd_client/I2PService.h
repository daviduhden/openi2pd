/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: I2PService.
 *
 * Declares the base class for client services, its handler lifetime
 * protocol and the bidirectional socket pipe used by tunnel
 * implementations. It also defines the readiness and idle-close policy
 * shared by acceptors.
 *
 */

#ifndef I2PSERVICE_H__
#define I2PSERVICE_H__

#include <atomic>
#include <mutex>
#include <unordered_set>
#include <memory>
#include <functional>
#include <vector>
#include <string_view>
#include <boost/asio.hpp>
#include "Destination.h"
#include "Identity.h"
#include "AddressBook.h"
#include "Log.h"

namespace i2p
{
namespace client
{
	constexpr int I2P_SERVICE_READINESS_CHECK_INTERVAL = 1; // in seconds
	constexpr uint64_t I2P_SERVICE_MIN_CLOSE_IDLE_TIME = 30000; // in milliseconds

	class I2PServiceHandler;
	/// Shared owner for a client destination and its active service
	/// handlers. Handler entries retain shared ownership; handlers refer
	/// back weakly so the registry does not itself create an ownership
	/// cycle.
	class I2PService : public std::enable_shared_from_this<I2PService>
	{
	public:
		typedef std::function<void (const boost::system::error_code &)> ReadyCallback;

	public:
		I2PService (std::shared_ptr<ClientDestination> localDestination = nullptr);
		I2PService (i2p::data::SigningKeyType kt);
		virtual ~I2PService ();

		inline void AddHandler (std::shared_ptr<I2PServiceHandler> conn)
		{
			std::unique_lock<std::mutex> l (m_HandlersMutex);
			m_Handlers.insert (conn);
		}
		inline void RemoveHandler (std::shared_ptr<I2PServiceHandler> conn)
		{
			std::unique_lock<std::mutex> l (m_HandlersMutex);
			m_Handlers.erase (conn);
		}
		/// Detach a snapshot under the registry lock, then terminate its
		/// handlers outside that lock because termination can re-enter
		/// RemoveHandler().
		void ClearHandlers ();
		size_t GetNumHandlers () const
		{
			std::unique_lock<std::mutex> l (m_HandlersMutex);
			return m_Handlers.size ();
		}

		void SetConnectTimeout (uint64_t timeout);
		void SetCloseIdleTime (uint64_t idleTime);
		void SetNewDestOnResume (bool newDestOnResume) { m_NewDestOnResume = newDestOnResume; }
		void UpdateLastActivityTime ();

		void AddReadyCallback (ReadyCallback cb);

		inline std::shared_ptr<ClientDestination> GetLocalDestination () { return m_LocalDestination; }
		inline std::shared_ptr<const ClientDestination> GetLocalDestination () const { return m_LocalDestination; }
		inline void SetLocalDestination (std::shared_ptr<ClientDestination> dest)
		{
			if (m_LocalDestination) m_LocalDestination->Release ();
			if (dest) dest->Acquire ();
			m_LocalDestination = dest;
		}
		/// Resolve @a dest through the address book and open one I2P stream.
		/// The name comes from a local client or proxy request and is
		/// therefore untrusted: an unresolvable or invalid name completes
		/// with `nullptr` instead of throwing. Completion runs on the
		/// destination's io_context, and a b33/blinded name takes the
		/// blinded-key path rather than being looked up as a b32 hash.
		/// @param dest full base64 destination, `.i2p` host name or b33 label
		/// @param port destination port, 0 when the service supplies it
		void CreateStream (StreamRequestComplete streamRequestComplete, std::string_view dest, uint16_t port = 0);
		/// Same as above for an address that was already resolved by the
		/// caller. When a connect timeout is configured and the destination
		/// is not yet ready, the request waits on a readiness callback that
		/// captures a weak owner; a service stopped in the meantime completes
		/// the request with `nullptr` instead of resuming work for a dead
		/// service.
		void CreateStream (StreamRequestComplete complete, std::shared_ptr<const Address> address, uint16_t port);
		auto & GetService () { return m_LocalDestination->GetService (); }

		virtual void Start ();
		virtual void Stop ();

		// True between a successful Start() and Stop(). ServiceAcceptor,
		// client and server tunnels update it through the base methods so the
		// administrative inventory can report a disabled or stopped tunnel.
		bool IsRunning () const { return m_Running.load (); }

		virtual const char * GetName () const { return "Generic I2P Service"; }

	protected:
		template <typename Callback> void IterateHandlers (Callback && callback)
		{
			std::unique_lock<std::mutex> l (m_HandlersMutex);
			for (auto it : m_Handlers) callback (it);
		}

	private:
		void TriggerReadyCheckTimer ();
		void HandleReadyCheckTimer (const boost::system::error_code & ec);
		void ScheduleIdleCheckTimer ();
		void HandleIdleCheckTimer (const boost::system::error_code & ec);
		void Resume ();

	private:
		std::shared_ptr<ClientDestination> m_LocalDestination;
		std::unordered_set<std::shared_ptr<I2PServiceHandler>> m_Handlers;
		mutable std::mutex m_HandlersMutex;
		std::vector<std::pair<ReadyCallback, uint64_t>> m_ReadyCallbacks;
		boost::asio::steady_timer m_ReadyTimer;
		bool m_ReadyTimerTriggered;
		uint64_t m_ConnectTimeout; // in seconds
		uint64_t m_CloseIdleTime; // in milliseconds
		bool m_NewDestOnResume;
		uint64_t m_LastActivityTime; // monotonic milliseconds
		std::unique_ptr<boost::asio::steady_timer> m_IdleCheckTimer;
		std::atomic<bool> m_Running { false };

		static constexpr size_t NEVER_TIMES_OUT = 0;

	public:
		bool isUpdated; // transient, used during reload only
	};

	/// Base for a service-owned handler. Kill() makes derived teardown
	/// idempotent; Done() removes the handler if its weak owner still
	/// exists.
	class I2PServiceHandler
	{
	public:
		I2PServiceHandler (std::shared_ptr<I2PService> parent)
			: m_Service (parent)
		{
		}
		virtual ~I2PServiceHandler () { }
		//If you override this make sure you call it from the children
		virtual void Handle () { }; //Start handling the socket
		virtual void Start () { };

		// Virtual: a derived handler owns the resource that must actually be
		// released (socket, stream, TLS object). ClearHandlers dispatches
		// through this base, so a non-virtual Terminate used to set m_Dead
		// first and make the real teardown a no-op, leaking every live
		// connection of a stopping service.
		virtual void Terminate () { Kill (); };

	protected:
		// Call when terminating or handing over to avoid race conditions
		inline bool Kill () { return m_Dead.test_and_set (); }
		// Call when done to clean up (make sure Kill is called first)
		inline void Done (std::shared_ptr<I2PServiceHandler> me)
		{
			auto owner = GetOwner ();
			if (owner) owner->RemoveHandler (me);
		}
		// Call to talk with the owner
		// the owner may be destroyed while a handler is still working on
		// another thread, so it is only reachable through a weak pointer
		inline std::shared_ptr<I2PService> GetOwner () const { return m_Service.lock (); }

	private:
		std::weak_ptr<I2PService> m_Service;
		std::atomic_flag m_Dead; //To avoid cleaning up multiple times
	};

	const size_t SOCKETS_PIPE_BUFFER_SIZE = 8192 * 8;

	/// Bidirectional asynchronous pipe with one fixed buffer per
	/// direction. Each read is followed by a complete write before reusing
	/// that buffer; callbacks retain the pipe and both endpoints while IO
	/// is in flight.
	/// @warning Normal termination must precede release of the
	///          final shared owner: the active teardown path uses
	///          shared_from_this().
	template <typename SocketUpstream, typename SocketDownstream>
	class SocketsPipe : public I2PServiceHandler,
						public std::enable_shared_from_this<SocketsPipe<SocketUpstream, SocketDownstream>>
	{
	public:
		SocketsPipe (std::shared_ptr<I2PService> owner, std::shared_ptr<SocketUpstream> upstream,
			std::shared_ptr<SocketDownstream> downstream)
			: I2PServiceHandler (owner)
			, m_up (upstream)
			, m_down (downstream)
		{
			try
			{
				boost::asio::socket_base::receive_buffer_size option (SOCKETS_PIPE_BUFFER_SIZE);
				upstream->set_option (option);
				downstream->set_option (option);
			}
			catch (boost::system::system_error & ex)
			{
				std::string name;
				if (GetOwner ()) name = GetOwner ()->GetName ();
				LogPrint (eLogWarning, "SocketsPipe: ", name, " failed to set receive buffer size: ", ex.what ());
			}
		}
		~SocketsPipe () { Terminate (); }

		void Start () override
		{
			Transfer (m_up, m_down, m_upstream_to_down_buf, SOCKETS_PIPE_BUFFER_SIZE); // receive from upstream
			Transfer (m_down, m_up, m_downstream_to_up_buf, SOCKETS_PIPE_BUFFER_SIZE); // receive from upstream
		}

	private:
		void Terminate () override
		{
			if (Kill ()) return;
			if (m_up)
			{
				if (m_up->is_open ()) m_up->close ();
				m_up = nullptr;
			}
			if (m_down)
			{
				if (m_down->is_open ()) m_down->close ();
				m_down = nullptr;
			}
			Done (SocketsPipe<SocketUpstream, SocketDownstream>::shared_from_this ());
		}

		template <typename From, typename To>
		void Transfer (std::shared_ptr<From> from, std::shared_ptr<To> to, uint8_t * buf, size_t len)
		{
			if (!from || !to || !buf) return;
			auto s = SocketsPipe<SocketUpstream, SocketDownstream>::shared_from_this ();
			from->async_read_some (boost::asio::buffer (buf, len),
				[from, to, s, buf, len] (const boost::system::error_code & ecode, std::size_t transferred)
				{
					if (ecode == boost::asio::error::operation_aborted) return;
					if (!ecode)
					{
						boost::asio::async_write (*to, boost::asio::buffer (buf, transferred),
							boost::asio::transfer_all (),
							[from, to, s, buf, len] (const boost::system::error_code & ecode, std::size_t transferred)
							{
								(void)transferred;
								if (ecode == boost::asio::error::operation_aborted) return;
								if (!ecode)
									s->Transfer (from, to, buf, len);
								else
								{
									LogPrint (eLogWarning, "SocketsPipe: Write error:", ecode.message ());
									s->Terminate ();
								}
							});
					}
					else
					{
						LogPrint (eLogWarning, "SocketsPipe: Read error:", ecode.message ());
						s->Terminate ();
					}
				});
		}

	private:
		uint8_t m_upstream_to_down_buf[SOCKETS_PIPE_BUFFER_SIZE], m_downstream_to_up_buf[SOCKETS_PIPE_BUFFER_SIZE];
		std::shared_ptr<SocketUpstream> m_up;
		std::shared_ptr<SocketDownstream> m_down;
	};

	template <typename SocketUpstream, typename SocketDownstream>
	std::shared_ptr<I2PServiceHandler> CreateSocketsPipe (std::shared_ptr<I2PService> owner,
		std::shared_ptr<SocketUpstream> upstream, std::shared_ptr<SocketDownstream> downstream)
	{
		return std::make_shared<SocketsPipe<SocketUpstream, SocketDownstream>> (owner, upstream, downstream);
	}

	// A local listener must not let an unauthenticated peer allocate
	// handlers without bound. The cap is per listener and applies to every
	// accepted socket, including those still in a proxy handshake.
	constexpr size_t MAX_ACCEPTED_HANDLERS = 1024;

	/// Listener that creates one service handler per accepted socket.
	/// @warning A successful accept completion can race Stop()
	///          and publish after the handler snapshot; Stop() does not
	///          share an admission lock with it.
	template <typename Protocol> class ServiceAcceptor : public I2PService
	{
	public:
		ServiceAcceptor (const typename Protocol::endpoint & localEndpoint,
			std::shared_ptr<ClientDestination> localDestination = nullptr)
			: I2PService (localDestination)
			, m_LocalEndpoint (localEndpoint)
		{
		}

		virtual ~ServiceAcceptor () { Stop (); }
		void Start () override
		{
			I2PService::Start ();
			m_Acceptor.reset (new typename Protocol::acceptor (GetService (), m_LocalEndpoint));
			// update the local end point in case port has been set zero and got
			// updated now
			m_LocalEndpoint = m_Acceptor->local_endpoint ();
			m_Acceptor->listen ();
			Accept ();
		}
		void Stop () override
		{
			if (m_Acceptor)
			{
				m_Acceptor->close ();
				m_Acceptor.reset (nullptr);
			}
			// Mark the service stopped before clearing the current handlers. The
			// accept callback does not check this flag under the registry lock,
			// so an already-completed accept may still race this snapshot.
			I2PService::Stop ();
			ClearHandlers ();
		}
		const typename Protocol::endpoint & GetLocalEndpoint () const { return m_LocalEndpoint; };

		const char * GetName () const override { return "Generic TCP/IP accepting daemon"; }

	protected:
		virtual std::shared_ptr<I2PServiceHandler> CreateHandler (std::shared_ptr<typename Protocol::socket> socket)
			= 0;

	private:
		void Accept ()
		{
			auto newSocket = std::make_shared<typename Protocol::socket> (GetService ());
			// The pending accept outlives a stop/destroy of this service, so the
			// completion must not dereference a raw owner. Accept() runs from
			// Start() on an already shared service; before that the weak form
			// keeps the historical behaviour.
			std::weak_ptr<ServiceAcceptor> weakSelf
				= std::static_pointer_cast<ServiceAcceptor> (weak_from_this ().lock ());
			m_Acceptor->async_accept (*newSocket,
				[newSocket, weakSelf] (const boost::system::error_code & ecode)
				{
					auto self = weakSelf.lock ();
					if (!self)
					{
						boost::system::error_code ignored;
						newSocket->close (ignored);
						return;
					}
					if (ecode == boost::asio::error::operation_aborted) return;
					if (!ecode)
					{
						if (self->GetNumHandlers () >= MAX_ACCEPTED_HANDLERS)
						{
							LogPrint (eLogWarning, "ServiceAcceptor: ", self->GetName (),
								" rejected a connection: ", MAX_ACCEPTED_HANDLERS, " handlers already active");
							boost::system::error_code ignored;
							newSocket->close (ignored);
							self->Accept ();
							return;
						}
						LogPrint (eLogDebug, "ServiceAcceptor: ", self->GetName (), " accepted");
						auto handler = self->CreateHandler (newSocket);
						if (handler)
						{
							self->AddHandler (handler);
							handler->Handle ();
						}
						else
							newSocket->close ();
						self->Accept ();
					}
					else
						LogPrint (eLogError, "ServiceAcceptor: ", self->GetName (),
							" closing socket on accept because: ", ecode.message ());
				});
		}

	private:
		typename Protocol::endpoint m_LocalEndpoint;
		std::unique_ptr<typename Protocol::acceptor> m_Acceptor;
	};

	class TCPIPAcceptor : public ServiceAcceptor<boost::asio::ip::tcp>
	{
	public:
		TCPIPAcceptor (
			const std::string & address, uint16_t port, std::shared_ptr<ClientDestination> localDestination = nullptr)
			: ServiceAcceptor (
				  boost::asio::ip::tcp::endpoint (boost::asio::ip::make_address (address), port), localDestination)
		{
		}
	};
}
}

#endif

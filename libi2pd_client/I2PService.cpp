/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: I2PService.
 *
 * Provides shared service lifecycle behavior: destination reference
 * counting, handler registration, readiness callbacks, connect timeouts
 * and close-idle resume logic. Resuming can regenerate the destination
 * identity and re-register it in the client context.
 *
 */

#include "Destination.h"
#include "Identity.h"
#include "ClientContext.h"
#include "I2PService.h"
#include <limits>
#include <boost/asio/error.hpp>
#include <boost/system/system_error.hpp>

namespace i2p
{
namespace client
{
	static const i2p::data::SigningKeyType I2P_SERVICE_DEFAULT_KEY_TYPE
		= i2p::data::SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519;

	I2PService::I2PService (std::shared_ptr<ClientDestination> localDestination)
		: m_LocalDestination (localDestination
				  ? localDestination
				  : i2p::client::context.CreateNewLocalDestination (false, I2P_SERVICE_DEFAULT_KEY_TYPE))
		, m_ReadyGeneration (std::make_shared<ReadyGeneration> (m_LocalDestination))
		, m_ConnectTimeout (0)
		, m_CloseIdleTime (0)
		, m_NewDestOnResume (false)
		, m_LastActivityTime (0)
		, isUpdated (true)
	{
		m_LocalDestination->Acquire ();
	}

	I2PService::I2PService (i2p::data::SigningKeyType kt)
		: I2PService (i2p::client::context.CreateNewLocalDestination (false, kt))
	{
	}

	I2PService::~I2PService ()
	{
		ResetReadyGeneration (false);
		ClearHandlers ();
		if (m_LocalDestination) m_LocalDestination->Release ();
	}

	void I2PService::Start ()
	{
		ResetReadyGeneration (true);
		m_Running.store (true);
		if (m_CloseIdleTime)
		{
			if (m_LocalDestination && m_LocalDestination->GetRefCounter () <= 1)
			{
				if (!m_IdleCheckTimer)
					m_IdleCheckTimer.reset (new boost::asio::steady_timer (m_LocalDestination->GetService ()));
				m_LastActivityTime = i2p::util::GetMonotonicMilliseconds ();
				ScheduleIdleCheckTimer ();
			}
			else
				LogPrint (eLogError, "I2PService: i2cp.closeIdleTime can't be set for ", GetName (),
					" on shared destination");
		}
	}

	void I2PService::Stop ()
	{
		ResetReadyGeneration (false);
		m_Running.store (false);
		if (m_IdleCheckTimer)
		{
			m_IdleCheckTimer->cancel ();
			m_IdleCheckTimer = nullptr;
		}
	}

	void I2PService::ClearHandlers ()
	{
		// Clearing handlers also retires in-flight stream completions. Keep
		// an open service usable, but never reopen a stopped generation.
		ResetReadyGeneration (true, true);
		// Snapshot and detach under the lock, then terminate outside it: a
		// handler's Terminate() re-enters RemoveHandler through Done() and
		// may close sockets whose completion handlers run on another thread.
		std::vector<std::shared_ptr<I2PServiceHandler>> handlers;
		{
			std::unique_lock<std::mutex> l (m_HandlersMutex);
			handlers.assign (m_Handlers.begin (), m_Handlers.end ());
			m_Handlers.clear ();
		}
		for (auto & handler : handlers)
			if (handler) handler->Terminate ();
	}

	void I2PService::SetConnectTimeout (uint64_t timeout)
	{
		m_ConnectTimeout = timeout;
	}

	void I2PService::SetCloseIdleTime (uint64_t idleTime)
	{
		if (idleTime > 0 && idleTime < I2P_SERVICE_MIN_CLOSE_IDLE_TIME) idleTime = I2P_SERVICE_MIN_CLOSE_IDLE_TIME;
		m_CloseIdleTime = idleTime;
	}

	void I2PService::UpdateLastActivityTime ()
	{
		if (m_CloseIdleTime)
		{
			m_LastActivityTime = i2p::util::GetMonotonicMilliseconds ();
			if (m_LocalDestination->IsIdling ()) Resume ();
		}
	}

	void I2PService::Resume ()
	{
		if (m_CloseIdleTime)
		{
			if (m_NewDestOnResume)
			{
				auto ident = m_LocalDestination->GetPrivateKeys ().GetPublic ();
				if (ident)
				{
					m_LocalDestination->SetPrivateKeys (i2p::data::PrivateKeys::CreateRandomKeys (
						ident->GetSigningKeyType (), ident->GetCryptoKeyType (), true));
					i2p::client::context.ReplaceLocalDestinationHash (
						ident->GetIdentHash (), m_LocalDestination->GetIdentHash ());
				}
			}
			ScheduleIdleCheckTimer ();
		}
		m_LocalDestination->SetIsIdling (false);
	}

	void I2PService::AddReadyCallback (ReadyCallback cb)
	{
		RegisterReadyCallback (GetReadyGeneration (), std::move (cb));
	}

	std::shared_ptr<I2PService::ReadyGeneration> I2PService::GetReadyGeneration () const
	{
		std::lock_guard lock (m_ReadinessMutex);
		return m_ReadyGeneration;
	}

	void I2PService::ResetReadyGeneration (bool accepting, bool preserveClosed)
	{
		std::shared_ptr<ReadyGeneration> previous;
		{
			std::lock_guard publication (m_ReadinessMutex);
			const bool reopen = accepting && (!preserveClosed || m_ReadyGeneration);
			auto replacement = reopen ? std::make_shared<ReadyGeneration> (m_LocalDestination) : nullptr;
			previous = std::exchange (m_ReadyGeneration, std::move (replacement));
			if (previous) previous->accepting.store (false, std::memory_order_release);
		}
		if (previous)
		{
			std::lock_guard admission (previous->mutex);
			// Retire authority before cancellation. Newer Asio timer APIs
			// expose only the throwing overload; teardown must still finish
			// if the timer service reports a cancellation failure.
			try { previous->timer.cancel (); }
			catch (const boost::system::system_error &) { }
		}
	}

	void I2PService::InvokeReadyCallback (ReadyCallback & callback, const boost::system::error_code & ec)
	{
		try { callback (ec); }
		catch (...) { LogPrint (eLogError, "I2PService: Readiness callback failed"); }
	}

	void I2PService::RegisterReadyCallback (
		const std::shared_ptr<ReadyGeneration> & generation, ReadyCallback cb)
	{
		assert (cb);
		boost::system::error_code rejection = boost::asio::error::operation_aborted;
		if (generation)
		{
			std::lock_guard admission (generation->mutex);
			if (generation->accepting.load (std::memory_order_acquire))
			{
				if (generation->callbacks.size () >= MAX_SERVICE_READY_CALLBACKS)
					rejection = boost::asio::error::no_buffer_space;
				else
				{
					const auto now = i2p::util::GetMonotonicSeconds ();
					const auto timeout = m_ConnectTimeout.load (std::memory_order_relaxed);
					const auto limit = (std::numeric_limits<uint64_t>::max) ();
					const auto deadline = !timeout ? NEVER_TIMES_OUT : timeout > limit - now ? limit : now + timeout;
					generation->callbacks.emplace_back (std::move (cb), deadline);
					if (!generation->wait) TriggerReadyCheckTimer (generation);
					return;
				}
			}
		}
		// Refusal is backpressure, not another unbounded executor item.
		InvokeReadyCallback (cb, rejection);
	}

	void I2PService::TriggerReadyCheckTimer (const std::shared_ptr<ReadyGeneration> & generation)
	{
		// The generation mutex serializes all operations on this timer.
		auto wait = std::make_shared<ReadyWait> ();
		generation->wait = wait;
		generation->timer.expires_after (std::chrono::seconds (I2P_SERVICE_READINESS_CHECK_INTERVAL));
		generation->timer.async_wait ([generation, wait] (const boost::system::error_code & ec)
			{ HandleReadyCheckTimer (ec, generation, wait); });
	}

	void I2PService::HandleReadyCheckTimer (const boost::system::error_code & ec,
		const std::shared_ptr<ReadyGeneration> & generation, const std::shared_ptr<ReadyWait> & wait)
	{
		decltype (generation->callbacks) selected;
		boost::system::error_code outcome = ec;
		{
			std::lock_guard admission (generation->mutex);
			if (generation->wait != wait) return;
			generation->wait.reset ();
			if (!generation->accepting.load (std::memory_order_acquire))
				outcome = boost::asio::error::operation_aborted;
			auto destination = generation->destination.lock ();
			if (!destination)
			{
				generation->accepting.store (false, std::memory_order_release);
				outcome = boost::asio::error::operation_aborted;
			}
			if (outcome || destination->IsReady ())
				selected.swap (generation->callbacks);
			else
			{
				const auto now = i2p::util::GetMonotonicSeconds ();
				auto & pending = generation->callbacks;
				for (auto it = pending.begin (); it != pending.end ();)
				{
					if (it->second != NEVER_TIMES_OUT && now >= it->second)
					{
						selected.push_back (std::move (*it));
						it = pending.erase (it);
					}
					else ++it;
				}
				outcome = boost::asio::error::timed_out;
			}
			if (generation->accepting.load (std::memory_order_acquire) && !generation->callbacks.empty ())
				TriggerReadyCheckTimer (generation);
		}
		// Reentrant registration cannot mutate the detached callback list.
		for (auto & entry : selected)
		{
			const auto result = generation->accepting.load (std::memory_order_acquire)
				? outcome : boost::system::error_code (boost::asio::error::operation_aborted);
			InvokeReadyCallback (entry.first, result);
		}
	}

	StreamRequestComplete I2PService::GuardStreamCompletion (
		const std::shared_ptr<ReadyGeneration> & generation, StreamRequestComplete complete)
	{
		std::weak_ptr<ReadyGeneration> authority = generation;
		return [authority, complete = std::move (complete)] (std::shared_ptr<i2p::stream::Stream> stream)
		{
			auto generation = authority.lock ();
			if (!generation || !generation->accepting.load (std::memory_order_acquire))
			{
				if (stream) stream->AsyncClose ();
				stream = nullptr;
			}
			complete (std::move (stream));
		};
	}

	void I2PService::ScheduleIdleCheckTimer ()
	{
		if (!m_IdleCheckTimer || !m_CloseIdleTime) return;
		m_IdleCheckTimer->expires_after (std::chrono::milliseconds (m_CloseIdleTime / 2));
		m_IdleCheckTimer->async_wait (
			std::bind (&I2PService::HandleIdleCheckTimer, shared_from_this (), std::placeholders::_1));
	}

	void I2PService::HandleIdleCheckTimer (const boost::system::error_code & ec)
	{
		if (ec != boost::asio::error::operation_aborted)
		{
			auto ts = i2p::util::GetMonotonicMilliseconds ();
			if (ts > m_LastActivityTime + m_CloseIdleTime)
				m_LocalDestination->SetIsIdling (true);
			else
				ScheduleIdleCheckTimer ();
		}
	}

	void I2PService::CreateStream (StreamRequestComplete streamRequestComplete, std::string_view dest, uint16_t port)
	{
		assert (streamRequestComplete);
		auto address = i2p::client::context.GetAddressBook ().GetAddress (dest);
		if (address)
			CreateStream (streamRequestComplete, address, port);
		else
		{
			LogPrint (eLogWarning, "I2PService: Remote destination not found: ", dest);
			streamRequestComplete (nullptr);
		}
	}

	void I2PService::CreateStream (
		StreamRequestComplete streamRequestComplete, std::shared_ptr<const Address> address, uint16_t port)
	{
		assert (streamRequestComplete);
		auto generation = GetReadyGeneration ();
		auto complete = GuardStreamCompletion (generation, std::move (streamRequestComplete));
		if (!generation || !generation->accepting.load (std::memory_order_acquire))
		{
			complete (nullptr);
			return;
		}
		auto localDestination = generation->destination.lock ();
		if (!localDestination)
		{
			complete (nullptr);
			return;
		}
		if (m_ConnectTimeout.load (std::memory_order_relaxed) && !localDestination->IsReady ())
		{
			// Readiness and the eventual stream result borrow the same
			// generation. A restart cannot revive an earlier request.
			std::weak_ptr<ReadyGeneration> authority = generation;
			std::weak_ptr<ClientDestination> destination = localDestination;
			RegisterReadyCallback (generation,
				[authority, destination, complete = std::move (complete), address, port] (
					const boost::system::error_code & ec)
				{
					auto current = authority.lock ();
					auto localDestination = destination.lock ();
					if (ec || !current || !localDestination || !current->accepting.load (std::memory_order_acquire))
					{
						complete (nullptr);
					}
					else
					{
						if (address->IsIdentHash ())
							localDestination->CreateStream (complete, address->identHash, port);
						else
							localDestination->CreateStream (complete, address->blindedPublicKey, port);
					}
				});
		}
		else
		{
			if (address->IsIdentHash ())
				localDestination->CreateStream (complete, address->identHash, port);
			else
				localDestination->CreateStream (complete, address->blindedPublicKey, port);
		}
	}
}
}

/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: MatchedDestination.
 *
 * Builds a client destination whose outbound tunnel pool reuses the
 * remote lease set's inbound gateway as the last hop, keeping traffic
 * to a service on matched peers. The remote lease set is resolved
 * asynchronously and refreshed when it expires.
 *
 */

#include "MatchedDestination.h"
#include "Log.h"
#include "ClientContext.h"
#include "Random.h"

namespace i2p
{
namespace client
{
	MatchedTunnelDestination::MatchedTunnelDestination (
		const i2p::data::PrivateKeys & keys, const std::string & remoteName, const i2p::util::Mapping * params)
		: RunnableClientDestination (keys, false, params)
		, m_RemoteName (remoteName)
	{
	}

	MatchedTunnelDestination::~MatchedTunnelDestination ()
	{
		if (auto pool = GetTunnelPool ()) pool->UnsetCustomPeerSelector ();
		if (m_ResolveTimer) m_ResolveTimer->cancel ();
	}

	void MatchedTunnelDestination::ResolveCurrentLeaseSet ()
	{
		auto addr = i2p::client::context.GetAddressBook ().GetAddress (m_RemoteName);
		if (addr && addr->IsIdentHash ())
		{
			i2p::data::IdentHash ident = addr->identHash;
			{
				std::lock_guard<std::mutex> lock (m_RemoteMutex);
				m_RemoteIdent = ident;
			}
			auto ls = FindLeaseSet (ident);
			if (ls)
				HandleFoundCurrentLeaseSet (ls);
			else
			{
				// The lookup completes on the destination's service thread and
				// may outlive this object; pin a weak reference instead of
				// capturing a raw this.
				std::weak_ptr<MatchedTunnelDestination> weak = GetSharedFromThis ();
				RequestDestination (ident,
					[weak] (std::shared_ptr<i2p::data::LeaseSet> leaseSet)
					{
						if (auto self = weak.lock ()) self->HandleFoundCurrentLeaseSet (std::move (leaseSet));
					});
			}
		}
		else
			LogPrint (eLogWarning, "Destination: Failed to resolve ", m_RemoteName);
	}

	void MatchedTunnelDestination::ScheduleResolveRetry ()
	{
		if (!m_ResolveTimer) return;
		m_ResolveTimer->expires_after (std::chrono::seconds (1));
		std::weak_ptr<MatchedTunnelDestination> weak = GetSharedFromThis ();
		m_ResolveTimer->async_wait (
			[weak] (const boost::system::error_code & ec)
			{
				if (ec) return; // cancelled by Stop()/destruction
				if (auto self = weak.lock ()) self->ResolveCurrentLeaseSet ();
			});
	}

	void MatchedTunnelDestination::HandleFoundCurrentLeaseSet (std::shared_ptr<const i2p::data::LeaseSet> ls)
	{
		if (ls)
		{
			LogPrint (eLogDebug, "Destination: Resolved remote lease set for ", m_RemoteName);
			std::lock_guard<std::mutex> lock (m_RemoteMutex);
			m_RemoteLeaseSet = ls;
		}
		else
			ScheduleResolveRetry ();
	}

	void MatchedTunnelDestination::Start ()
	{
		// Start the runnable service as well: the resolve timer and the lease
		// set request callbacks run on this destination's io_context, which
		// ClientDestination::Start() alone does not run.
		RunnableClientDestination::Start ();
		m_ResolveTimer = std::make_shared<boost::asio::steady_timer> (GetService ());
		GetTunnelPool ()->SetCustomPeerSelector (this, shared_from_this ());
		ResolveCurrentLeaseSet ();
	}

	void MatchedTunnelDestination::Stop ()
	{
		if (auto pool = GetTunnelPool ()) pool->UnsetCustomPeerSelector ();
		if (m_ResolveTimer) m_ResolveTimer->cancel ();
		RunnableClientDestination::Stop ();
	}

	bool MatchedTunnelDestination::SelectPeers (i2p::tunnel::Path & path, int hops, bool inbound)
	{
		auto pool = GetTunnelPool ();
		if (!pool
			|| !pool->StandardSelectPeers (path, hops, inbound,
				std::bind (&i2p::tunnel::TunnelPool::SelectNextHop, pool, std::placeholders::_1, std::placeholders::_2,
					std::placeholders::_3)))
			return false;
		// more here for outbound tunnels
		std::shared_ptr<const i2p::data::LeaseSet> remoteLeaseSet;
		{
			std::lock_guard<std::mutex> lock (m_RemoteMutex);
			remoteLeaseSet = m_RemoteLeaseSet;
		}
		if (!inbound)
		{
			if (remoteLeaseSet && remoteLeaseSet->IsExpired ())
			{
				ResolveCurrentLeaseSet ();
				remoteLeaseSet = nullptr;
			}
			bool matched = false;
			if (remoteLeaseSet)
			{
				// remote lease set is good
				auto leases = remoteLeaseSet->GetNonExpiredLeases ();
				// pick lease
				std::shared_ptr<i2p::data::RouterInfo> obep;
				while (!obep && leases.size () > 0)
				{
					auto idx = i2p::crypto::RandomBelow (static_cast<uint32_t> (leases.size ()));
					auto lease = leases[idx];
					leases.erase (leases.begin () + idx);
					auto candidate = i2p::data::netdb.FindRouter (lease->tunnelGateway);
					// Only an ECIES-capable gateway can serve as our OBEP.
					if (candidate && candidate->IsECIES () && !candidate->IsUnreachable ()) obep = candidate;
				}
				if (obep)
				{
					path.Add (obep);
					matched = true;
					LogPrint (eLogDebug, "Destination: Found OBEP matching IBGW");
				}
				else
					LogPrint (eLogWarning, "Destination: Could not find proper IBGW for matched outbound tunnel");
			}
			if (!matched)
			{
				// An unmatched outbound path would defeat the purpose of this
				// destination, so report the failure instead of returning a
				// path that silently is not matched.
				return false;
			}
		}
		return !path.peers.empty ();
	}
}
}

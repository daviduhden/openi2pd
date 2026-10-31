/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: MatchedDestination.
 *
 * Declares a destination that overrides tunnel peer selection so each
 * outbound path ends at the gateway of the remote destination's current
 * lease.
 *
 */

#ifndef MATCHED_DESTINATION_H_
#define MATCHED_DESTINATION_H_

#include <string>
#include <mutex>
#include <memory>
#include "Destination.h"

namespace i2p
{
namespace client
{
	/**
	 * @brief Client destination whose outbound tunnels end at the inbound
	 *   gateway of the remote destination's current lease.
	 *
	 * Each outbound path is built by the normal pool selection first and
	 * then has one more hop appended: a gateway taken from a non-expired
	 * remote lease. The result is a path whose OBEP is the remote IBGW,
	 * which keeps the round trip on peers the service already uses. Only
	 * an inbound tunnel is left unmatched, because it has no remote
	 * gateway to reuse.
	 *
	 * The remote name is resolved through the address book on the
	 * destination's own io_context; only an ident-hash entry (b32 or
	 * base64 destination) is accepted, because a blinded b33 name does not
	 * name a stable gateway. The lease set is refreshed when it expires,
	 * and a failed lookup schedules a bounded retry instead of blocking
	 * the caller.
	 *
	 * @warning SelectPeers() reports failure when no reachable
	 *          ECIES-capable gateway is known. It never falls back to an
	 *          unmatched path: a silently unmatched tunnel would defeat
	 *          the purpose of this destination, so the pool records a
	 *          build failure instead.
	 *
	 * Lifetime: the tunnel pool holds this selector weakly, and Stop()
	 * clears it before cancelling the retry timer; resolve callbacks hold
	 * a weak reference so a lease set that arrives after shutdown is
	 * ignored.
	 */
	class MatchedTunnelDestination : public RunnableClientDestination, public i2p::tunnel::ITunnelPeerSelector
	{
	public:
		MatchedTunnelDestination (const i2p::data::PrivateKeys & keys, const std::string & remoteName,
			const i2p::util::Mapping * params = nullptr);
		~MatchedTunnelDestination ();
		void Start ();
		void Stop ();

		bool SelectPeers (i2p::tunnel::Path & peers, int hops, bool inbound);

	private:
		std::shared_ptr<MatchedTunnelDestination> GetSharedFromThis ()
		{
			return std::static_pointer_cast<MatchedTunnelDestination> (shared_from_this ());
		}
		void ResolveCurrentLeaseSet ();
		void HandleFoundCurrentLeaseSet (std::shared_ptr<const i2p::data::LeaseSet> ls);
		void ScheduleResolveRetry ();

	private:
		std::string m_RemoteName;
		std::mutex m_RemoteMutex;
		i2p::data::IdentHash m_RemoteIdent;
		std::shared_ptr<const i2p::data::LeaseSet> m_RemoteLeaseSet;
		std::shared_ptr<boost::asio::steady_timer> m_ResolveTimer;
	};
}
}

#endif

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
	 * client tunnel that uses same OBEP as IBGW of each remote lease for a remote destination
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

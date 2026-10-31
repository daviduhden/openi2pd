/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: IdentMetrics.
 *
 * Kademlia XOR distance metric and routing-key derivation used by the
 * network database. PeerOrdering derives peer ordering groups from a
 * periodically rotated SipHash key so that first and last tunnel hops
 * can be chosen without clustering.
 *
 */

#ifndef IDENT_METRICS_H__
#define IDENT_METRICS_H__

#include <inttypes.h>
#include <string.h>
#include <unordered_map>
#include "Tag.h"
#include "Identity.h"
#include "Crypto.h"

namespace i2p
{
namespace data
{
	// kademlia
	struct XORMetric
	{
		union
		{
			uint8_t metric[32];
			uint64_t metric_ll[4];
		};

		void SetMin () { memset (metric, 0, 32); };
		void SetMax () { memset (metric, 0xFF, 32); };
		bool operator< (const XORMetric & other) const { return memcmp (metric, other.metric, 32) < 0; };
	};

	IdentHash CreateRoutingKey (const IdentHash & ident, bool nextDay = false);
	/// XOR distance metric used for closest-floodfill ordering.
	XORMetric operator^ (const IdentHash & key1, const IdentHash & key2);

	// peer ordering
	constexpr uint64_t PEER_ORDERING_INACTIVITY_TIMEOUT = 400; // in seconds
	/// Keyed peer-ordering groups for tunnel hop placement. The ordering
	/// key is a short-lived SipHash key rotated by the router, so a remote
	/// peer cannot predict or bias which group it lands in; it is secret
	/// material and must never be logged or published. Cached groups
	/// expire after PEER_ORDERING_INACTIVITY_TIMEOUT and are recomputed
	/// from the new key.
	class PeerOrdering
	{
	public:
		PeerOrdering ();
		~PeerOrdering ();
		void CleanUp (uint64_t ts);

		/// Install a 16-byte ordering key and invalidate cached groups. A
		/// null key draws a fresh CSPRNG key; when the provider fails the key
		/// stays invalid (wiped) and group lookups must not treat the peer as
		/// ordered. The caller owns the source buffer; only the bytes are
		/// copied.
		void SetKey (const uint8_t * key);
		const Tag<16> & GetKey () const { return m_PeerOrderingKey; }

		int GetPeerOrderingGroup (const IdentHash & routerIdent);
		bool IsFirstHop (const IdentHash & routerIdent) { return !GetPeerOrderingGroup (routerIdent); };
		bool IsLastHop (const IdentHash & routerIdent)
		{
			return GetPeerOrderingGroup (routerIdent) & 0x02;
		}; // IBGW or OBEP

	private:
		int CalculatePeerOrderingGroup (const IdentHash & routerIdent);

	private:
		Tag<16> m_PeerOrderingKey;
		bool m_IsKeyValid = false;
		std::unordered_map<IdentHash, std::pair<int, uint64_t>>
			m_OrderingGroups; // router ident hash -> (group, last request time)
#if OPENSSL_SIPHASH
		EVP_MD_CTX * m_MDCtx = nullptr;
#endif
	};
}
}

#endif

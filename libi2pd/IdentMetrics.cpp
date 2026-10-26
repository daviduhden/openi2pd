/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: IdentMetrics.
 *
 * Implements daily Kademlia routing-key derivation, XOR distance metrics and
 * the SipHash-based peer ordering groups used to spread tunnel selection.
 * Ordering groups are cached per router with an inactivity timeout, and the
 * SipHash key can be reseeded or fixed for deterministic grouping.
 *
 */

#include <openssl/evp.h>
#include <openssl/rand.h>
#include <cstring>
#include "Siphash.h"
#include "Log.h"
#include "util.h"
#include "Timestamp.h"
#include "IdentMetrics.h"
#if OPENSSL_SIPHASH
#include <openssl/core_names.h>
#endif

namespace i2p
{
namespace data
{
	IdentHash CreateRoutingKey (const IdentHash & ident, bool nextDay)
	{
		uint8_t buf[41]; // ident + yyyymmdd
		memcpy (buf, (const uint8_t *)ident, 32);
		if (nextDay)
			i2p::util::GetNextDayDate ((char *)(buf + 32));
		else
			i2p::util::GetCurrentDate ((char *)(buf + 32));
		IdentHash key;
		SHA256 (buf, 40, key);
		return key;
	}

	XORMetric operator^ (const IdentHash & key1, const IdentHash & key2)
	{
		XORMetric m;

		const uint64_t *hash1 = key1.GetLL (), *hash2 = key2.GetLL ();
		m.metric_ll[0] = hash1[0] ^ hash2[0];
		m.metric_ll[1] = hash1[1] ^ hash2[1];
		m.metric_ll[2] = hash1[2] ^ hash2[2];
		m.metric_ll[3] = hash1[3] ^ hash2[3];

		return m;
	}

	PeerOrdering::PeerOrdering ()
	{
		SetKey (nullptr);
	}

	PeerOrdering::~PeerOrdering ()
	{
		m_PeerOrderingKey.Wipe ();
#if OPENSSL_SIPHASH
		if (m_MDCtx) EVP_MD_CTX_free (m_MDCtx);
#endif
	}

	void PeerOrdering::SetKey (const uint8_t * key)
	{
		m_IsKeyValid = false;
		m_PeerOrderingKey.Wipe ();
		if (key)
			memcpy (m_PeerOrderingKey, key, 16);
		else if (RAND_bytes (m_PeerOrderingKey, 16) != 1)
		{
			LogPrint (eLogError, "IdentMetrics: CSPRNG unavailable for peer ordering key");
			m_PeerOrderingKey.Wipe ();
			return;
		}
#if OPENSSL_SIPHASH
		if (m_MDCtx)
		{
			EVP_MD_CTX_free (m_MDCtx);
			m_MDCtx = nullptr;
		}
		EVP_MD_CTX * md = EVP_MD_CTX_new ();
		EVP_PKEY * sipKey = EVP_PKEY_new_raw_private_key (EVP_PKEY_SIPHASH, nullptr, m_PeerOrderingKey, 16);
		EVP_PKEY_CTX * pctx = nullptr;
		size_t macSize = 16;
		OSSL_PARAM params[2] = {
			OSSL_PARAM_construct_size_t (OSSL_MAC_PARAM_SIZE, &macSize), OSSL_PARAM_construct_end ()};
		bool ok = md && sipKey && EVP_DigestSignInit (md, &pctx, nullptr, nullptr, sipKey) == 1;
		// SipHash defaults to an 8-byte tag. The I2P peer-ordering group uses
		// the first byte of a 16-byte digest, matching the project fallback.
		if (ok) ok = EVP_PKEY_CTX_set_params (pctx, params) == 1;
		if (sipKey) EVP_PKEY_free (sipKey);
		if (!ok)
		{
			if (md) EVP_MD_CTX_free (md);
			LogPrint (eLogError, "IdentMetrics: SipHash peer ordering unavailable");
			m_PeerOrderingKey.Wipe ();
			return;
		}
		m_MDCtx = md;
#endif
		m_IsKeyValid = true;
	}

	int PeerOrdering::CalculatePeerOrderingGroup (const IdentHash & routerIdent)
	{
		if (!m_IsKeyValid) return 0;
		uint8_t hash[16] { };
#if OPENSSL_SIPHASH
		size_t l = sizeof (hash);
		const bool ok = m_MDCtx && EVP_DigestSignInit (m_MDCtx, nullptr, nullptr, nullptr, nullptr) == 1
			&& EVP_DigestSignUpdate (m_MDCtx, routerIdent, 32) == 1 && EVP_DigestSignFinal (m_MDCtx, hash, &l) == 1
			&& l == sizeof (hash);
		if (!ok)
		{
			// A provider without SipHash must not turn uninitialised stack
			// bytes into a peer-ordering decision.
			LogPrint (eLogError, "IdentMetrics: SipHash peer ordering failed");
			return 0;
		}
#else
		i2p::crypto::Siphash<16> (hash, routerIdent, 32, m_PeerOrderingKey);
#endif
		return hash[0] & 0x03;
	}

	int PeerOrdering::GetPeerOrderingGroup (const IdentHash & routerIdent)
	{
		auto ts = i2p::util::GetSecondsSinceEpoch ();
		auto it = m_OrderingGroups.find (routerIdent);
		if (it != m_OrderingGroups.end ())
		{
			it->second.second = ts;
			return it->second.first;
		}
		int group = CalculatePeerOrderingGroup (routerIdent);
		m_OrderingGroups.emplace (routerIdent, std::pair { group, ts });
		return group;
	}

	void PeerOrdering::CleanUp (uint64_t ts)
	{
		for (auto it = m_OrderingGroups.begin (); it != m_OrderingGroups.end ();)
		{
			if (ts > it->second.second + PEER_ORDERING_INACTIVITY_TIMEOUT)
				it = m_OrderingGroups.erase (it);
			else
				it++;
		}
	}
}
}

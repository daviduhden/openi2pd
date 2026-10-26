/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: crypto.
 *
 * RAII around the libi2pd crypto lifetime used by native tools. Every early
 * return now terminates the crypto state instead of leaking it.
 */

#ifndef I2PD_TOOLS_COMMON_CRYPTO_HPP
#define I2PD_TOOLS_COMMON_CRYPTO_HPP

#include "Crypto.h"

namespace i2p::tools
{
/**
 * @brief Own the process-wide libi2pd crypto lifetime for one tool run.
 *
 * `InitCrypto(false)` probes the provider and skips the ElGamal precomputation
 * table, so it is cheap but also not free to repeat, and it is not reference
 * counted. Exactly one guard may exist per process. Declare it before the keys,
 * signers and `IdentityEx` instances it serves: locals are destroyed in reverse
 * order, so that placement makes the guard outlive its users. Today
 * `TerminateCrypto()` only releases that ElGamal table, which this guard never
 * builds, so the ordering is a forward-compatible invariant rather than a
 * present-day hazard; it still keeps the rule true if termination grows.
 * Early returns are covered by the destructor, which is why tools never call
 * `InitCrypto`/`TerminateCrypto()` themselves.
 *
 * Limitation: the constructor discards the `InitCrypto` provider check, so a
 * libcrypto build without a required algorithm is not reported here. A tool
 * that must fail closed still has to treat its own key/sign results as
 * authoritative.
 */
class CryptoGuard
{
public:
	CryptoGuard () { i2p::crypto::InitCrypto (false); }
	~CryptoGuard () { i2p::crypto::TerminateCrypto (); }
	CryptoGuard (const CryptoGuard &) = delete;
	CryptoGuard & operator= (const CryptoGuard &) = delete;
};
}

#endif

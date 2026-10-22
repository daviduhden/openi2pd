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

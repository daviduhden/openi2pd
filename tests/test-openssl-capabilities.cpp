/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: test OpenSSL capabilities.
 *
 * Exercises the provider/algorithm capability probe used by startup and the
 * runtime ML-KEM publication gate. It does not load a non-default provider or
 * make a network connection.
 */

#include <cassert>

#include "Crypto.h"
#if OPENSSL_PQ
#include "PostQuantum.h"
#endif

int main ()
{
	i2p::crypto::InitCrypto (false);
	const auto & capabilities = i2p::crypto::GetCryptoCapabilities ();

	// These primitives are required by the router's wire protocols. The
	// LibreSSL branch reports the same capabilities through its legacy
	// helpers, so the assertions are backend-independent.
	assert (capabilities.sha256);
	assert (capabilities.sha512);
	assert (capabilities.aes256Cbc);
	assert (capabilities.aes256Ecb);
	assert (capabilities.chacha20Poly1305);
	assert (capabilities.hkdf);
	assert (capabilities.sipHash);

#if OPENSSL_PQ
	// A version may be advertised only when its exact ML-KEM parameter set is
	// usable; the selection helper must never return an unsupported version.
	const int highest = i2p::crypto::HighestSupportedPQVersion (5, 5);
	if (highest)
	{
		assert (highest >= 3 && highest <= 5);
		assert (i2p::crypto::CanPublishPQ (highest));
	}
#endif

	i2p::crypto::TerminateCrypto ();
	return 0;
}

/*
 * Copyright (c) 2026, David Uhden Collado
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Regression checks for strict b33 parsing in BlindedPublicKey. Overlong
 * addresses were previously truncated and malformed addresses could still
 * report IsValid() because the public-key buffer was left zeroed while the
 * signature type remained acceptable.
 */

#include <cassert>
#include <string>

#include "Blinding.h"
#include "Identity.h"

int main ()
{
	using namespace i2p::data;

	auto keys = PrivateKeys::CreateRandomKeys (SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519);
	BlindedPublicKey original (keys.GetPublic ());
	assert (original.IsValid ());

	const std::string b33 = original.ToB33 ();
	assert (!b33.empty ());

	BlindedPublicKey parsed (b33);
	assert (parsed.IsValid ());
	assert (parsed.ToB33 () == b33);
	assert (parsed.GetPublicKeyLen () == 32);

	BlindedPublicKey truncated (b33.substr (0, b33.size () - 1));
	assert (!truncated.IsValid ());
	assert (truncated.ToB33 ().empty ());

	// One byte of trailing data is inside the old 40-byte limit but does not
	// match the exact b33 layout for a 32-byte key.
	BlindedPublicKey extraByte (b33 + "aa");
	assert (!extraByte.IsValid ());
	assert (extraByte.ToB33 ().empty ());

	// Well beyond the maximum accepted address size.
	BlindedPublicKey overlong (b33 + std::string (40, 'a'));
	assert (!overlong.IsValid ());
	assert (overlong.ToB33 ().empty ());

	return 0;
}

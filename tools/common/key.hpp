/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: key.
 *
 * Maps numeric or textual signature type names to I2P SigningKeyType values
 * and back to stable display names for the tools. Empty, unknown or
 * out-of-range values return std::nullopt instead of a sentinel.
 *
 */

#ifndef I2PD_TOOLS_COMMON_KEY_HPP
#define I2PD_TOOLS_COMMON_KEY_HPP

#include <algorithm>
#include <cctype>
#include <charconv>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>

#include "Identity.h"

/** @brief returns string representation of a signing key type */
inline std::string SigTypeToName (uint16_t keytype)
{
	switch (keytype)
	{
	case i2p::data::SIGNING_KEY_TYPE_DSA_SHA1: return "DSA-SHA1";
	case i2p::data::SIGNING_KEY_TYPE_ECDSA_SHA256_P256: return "ECDSA-P256";
	case i2p::data::SIGNING_KEY_TYPE_ECDSA_SHA384_P384: return "ECDSA-P384";
	case i2p::data::SIGNING_KEY_TYPE_ECDSA_SHA512_P521: return "ECDSA-P521";
	case i2p::data::SIGNING_KEY_TYPE_RSA_SHA256_2048: return "RSA-2048-SHA256";
	case i2p::data::SIGNING_KEY_TYPE_RSA_SHA384_3072: return "RSA-3072-SHA384";
	case i2p::data::SIGNING_KEY_TYPE_RSA_SHA512_4096: return "RSA-4096-SHA512";
	case i2p::data::SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519: return "ED25519-SHA512";
	case i2p::data::SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519ph: return "ED25519PH-SHA512";
	case i2p::data::SIGNING_KEY_TYPE_GOSTR3410_CRYPTO_PRO_A_GOSTR3411_256: return "GOSTR3410-A-GOSTR3411-256";
	case i2p::data::SIGNING_KEY_TYPE_GOSTR3410_TC26_A_512_GOSTR3411_512: return "GOSTR3410-TC26-A-GOSTR3411-512";
	case i2p::data::SIGNING_KEY_TYPE_REDDSA_SHA512_ED25519: return "RED25519-SHA512";
	default:
	{
		std::ostringstream ss;
		ss << "unknown: " << keytype;
		return ss.str ();
	}
	}
}

inline void ToUpper (std::string & str)
{
	std::transform (str.begin (), str.end (), str.begin (),
		[] (unsigned char ch) { return static_cast<char> (std::toupper (ch)); });
}

/**
 * Map a numeric or textual signature type to the protocol constant.
 * Returns std::nullopt for invalid or unknown input; no sentinel value is
 * passed to CreateRandomKeys.
 */
inline std::optional<uint16_t> NameToSigType (std::string_view keyname)
{
	if (keyname.empty ()) return std::nullopt;

	if (keyname.size () <= 2)
	{
		uint16_t value = 0;
		auto result = std::from_chars (keyname.data (), keyname.data () + keyname.size (), value);
		if (result.ec != std::errc () || result.ptr != keyname.data () + keyname.size ()) return std::nullopt;
		if (value > i2p::data::SIGNING_KEY_TYPE_REDDSA_SHA512_ED25519) return std::nullopt;
		return value;
	}

	std::string name (keyname);
	ToUpper (name);
	const auto npos = std::string::npos;
	if (name.find ("DSA") == 0) return i2p::data::SIGNING_KEY_TYPE_DSA_SHA1;
	if (name.find ("P256") != npos) return i2p::data::SIGNING_KEY_TYPE_ECDSA_SHA256_P256;
	if (name.find ("P384") != npos) return i2p::data::SIGNING_KEY_TYPE_ECDSA_SHA384_P384;
	if (name.find ("RSA-SHA256") != npos) return i2p::data::SIGNING_KEY_TYPE_RSA_SHA256_2048;
	if (name.find ("RSA-SHA384") != npos) return i2p::data::SIGNING_KEY_TYPE_RSA_SHA384_3072;
	if (name.find ("RSA-SHA512") != npos) return i2p::data::SIGNING_KEY_TYPE_RSA_SHA512_4096;
	if (name.find ("RED25519") != npos) return i2p::data::SIGNING_KEY_TYPE_REDDSA_SHA512_ED25519;
	if (name.find ("ED25519PH") != npos) return i2p::data::SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519ph;
	if (name.find ("ED25519") != npos) return i2p::data::SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519;
	if (name.find ("GOSTR3410-A-GOSTR3411-256") != npos)
		return i2p::data::SIGNING_KEY_TYPE_GOSTR3410_CRYPTO_PRO_A_GOSTR3411_256;
	if (name.find ("GOSTR3410-TC26-A-GOSTR3411-512") != npos)
		return i2p::data::SIGNING_KEY_TYPE_GOSTR3410_TC26_A_512_GOSTR3411_512;
	return std::nullopt;
}

#endif

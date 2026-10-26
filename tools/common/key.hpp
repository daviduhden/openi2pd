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
#include <array>
#include <cctype>
#include <charconv>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>

#include "Identity.h"
#include "file.hpp"
#include "keys.hpp"

struct SigTypeName
{
	std::string_view name;
	uint16_t type;
};

inline constexpr std::array<SigTypeName, 16> SIG_TYPE_NAMES { {
	{ "DSA-SHA1", i2p::data::SIGNING_KEY_TYPE_DSA_SHA1 },
	{ "ECDSA-P256", i2p::data::SIGNING_KEY_TYPE_ECDSA_SHA256_P256 },
	{ "ECDSA-SHA256-P256", i2p::data::SIGNING_KEY_TYPE_ECDSA_SHA256_P256 },
	{ "ECDSA-P384", i2p::data::SIGNING_KEY_TYPE_ECDSA_SHA384_P384 },
	{ "ECDSA-SHA384-P384", i2p::data::SIGNING_KEY_TYPE_ECDSA_SHA384_P384 },
	{ "ECDSA-P521", i2p::data::SIGNING_KEY_TYPE_ECDSA_SHA512_P521 },
	{ "ECDSA-SHA512-P521", i2p::data::SIGNING_KEY_TYPE_ECDSA_SHA512_P521 },
	{ "RSA-2048-SHA256", i2p::data::SIGNING_KEY_TYPE_RSA_SHA256_2048 },
	{ "RSA-3072-SHA384", i2p::data::SIGNING_KEY_TYPE_RSA_SHA384_3072 },
	{ "RSA-4096-SHA512", i2p::data::SIGNING_KEY_TYPE_RSA_SHA512_4096 },
	{ "ED25519-SHA512", i2p::data::SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519 },
	{ "EDDSA-SHA512-ED25519", i2p::data::SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519 },
	{ "ED25519PH-SHA512", i2p::data::SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519ph },
	{ "RED25519-SHA512", i2p::data::SIGNING_KEY_TYPE_REDDSA_SHA512_ED25519 },
	{ "GOSTR3410-A-GOSTR3411-256", i2p::data::SIGNING_KEY_TYPE_GOSTR3410_CRYPTO_PRO_A_GOSTR3411_256 },
	{ "GOSTR3410-TC26-A-GOSTR3411-512", i2p::data::SIGNING_KEY_TYPE_GOSTR3410_TC26_A_512_GOSTR3411_512 },
} };

/**
 * @brief returns stable string representation of a signing key type
 *
 * The first table entry with a matching type wins, so an alias must never be
 * listed before the canonical I2P spelling: `ECDSA-P256` therefore renders as
 * `ECDSA-P256` while `ECDSA-SHA256-P256` stays an accepted input alias. The
 * result is display and diagnostic text (`keygen` and `b33address` print it)
 * and is never parsed back, so an unnamed type degrades to
 * `unknown: &lt;number&gt;` instead of failing.
 */
inline std::string SigTypeToName (uint16_t keytype)
{
	for (const auto & entry : SIG_TYPE_NAMES)
		if (entry.type == keytype) return std::string (entry.name);
	std::ostringstream ss;
	ss << "unknown: " << keytype;
	return ss.str ();
}

/**
 * A registration record is `name=destination#!sig=...`; a name carrying a
 * separator, control byte or whitespace could forge extra fields. The check is
 * deliberately about the record grammar, not DNS policy.
 *
 * Rejecting `=` and `#` keeps the signed prefix parseable, and refusing every
 * byte at or below space plus DEL keeps it one LF-terminated line on both
 * platforms. Backslash is refused because the registration services accept
 * escaped fields. Length is capped at 255 bytes; the DNS/label rules a
 * registration service applies are a separate, later decision. Callers run
 * this before loading a key file, so a malformed name is refused without
 * touching the key.
 */
inline bool IsValidRecordName (std::string_view name)
{
	if (name.empty () || name.size () > 255) return false;
	for (unsigned char ch : name)
	{
		if (ch <= 0x20 || ch == 0x7f || ch == '#' || ch == '=' || ch == '\\') return false;
	}
	return true;
}

/**
 * Parse a complete decimal integer inside a closed range. Trailing characters,
 * overflow and an empty input are rejected instead of silently truncated.
 */
template <typename TValue> std::optional<TValue> ParseBounded (std::string_view text, TValue minimum, TValue maximum)
{
	if (text.empty ()) return std::nullopt;
	TValue value { };
	const auto result = std::from_chars (text.data (), text.data () + text.size (), value);
	if (result.ec != std::errc () || result.ptr != text.data () + text.size ()) return std::nullopt;
	if (value < minimum || value > maximum) return std::nullopt;
	return value;
}

/// @brief Resolve a numeric or textual signature-type spelling to its wire value.
///
/// Two spellings are accepted because the CLI historically allowed both. A
/// one- or two-character argument is a decimal type number, bounded to the
/// highest type this build names; anything longer is a name, matched
/// case-insensitively after folding '_' to '-' (so `eddsa_sha512_ed25519` and
/// `EDDSA-SHA512-ED25519` are the same alias). The table intentionally keeps
/// alternate short names such as `ECDSA-P256` next to the canonical I2P name.
///
/// @return The signing key type, or std::nullopt for an unknown, empty or
///         out-of-range spelling; callers report a CLI error instead of
///         silently defaulting.
///
/// The one/two-character gate is part of the contract: `7` and `11` are
/// numbers, while a zero-padded `007` is treated as a name and refused. Numeric
/// input stops at `RED25519-SHA512`, so the remaining wire values stay
/// unnameable here rather than being accepted for a key this build cannot
/// generate or label.
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
	std::ranges::transform (
		name, name.begin (), [] (unsigned char ch) { return static_cast<char> (std::toupper (ch)); });
	std::replace (name.begin (), name.end (), '_', '-');
	for (const auto & entry : SIG_TYPE_NAMES)
		if (name == entry.name) return entry.type;
	if (name == "GOSTR3410-CRYPTO-PRO-A-GOSTR3411-256")
		return i2p::data::SIGNING_KEY_TYPE_GOSTR3410_CRYPTO_PRO_A_GOSTR3411_256;
	if (name == "GOSTR3410-TC26-A-GOSTR3411-512") return i2p::data::SIGNING_KEY_TYPE_GOSTR3410_TC26_A_512_GOSTR3411_512;
	return std::nullopt;
}

#endif

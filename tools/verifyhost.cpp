/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: verifyhost.
 *
 * Verifies the destination signature of a host-registration record and, when
 * present, the old destination's counter-signature for an `olddest` transition.
 * It prints a validity diagnostic and exits non-zero on any malformed or
 * invalid signature.
 *
 */

#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "Base.h"
#include "Crypto.h"
#include "Identity.h"

namespace
{
struct CryptoGuard
{
	CryptoGuard () { i2p::crypto::InitCrypto (false); }
	~CryptoGuard () { i2p::crypto::TerminateCrypto (); }
};

std::optional<std::string_view> FindValue (std::string_view record, std::string_view key)
{
	const auto marker = record.find (key);
	if (marker == std::string_view::npos) return std::nullopt;
	auto value = record.substr (marker + key.size ());
	// A record can carry an internal newline between the old signature and a
	// later marker. Stop at record delimiters, not only at '#'.
	const auto end = value.find_first_of ("#\r\n");
	if (end != std::string_view::npos) value = value.substr (0, end);
	return value;
}
}

int main (int argc, char * argv[])
{
	if (argc < 2 || argc > 2)
	{
		std::cerr << "Usage: verifyhost '<host record>'" << std::endl;
		return 1;
	}

	CryptoGuard guard;
	i2p::data::IdentityEx identity, oldIdentity;
	std::string record (argv[1]);
	// CMake and the Windows text-mode tool chain may hand us CRLF while the
	// signer canonicalized the same record with LF. Signatures cover the
	// logical record, so normalize the line endings before verification.
	if (record.find ('\r') != std::string::npos)
	{
		std::string normalized;
		normalized.reserve (record.size ());
		for (size_t i = 0; i < record.size (); ++i)
		{
			if (record[i] == '\r')
			{
				if (i + 1 < record.size () && record[i + 1] == '\n') continue;
				normalized.push_back ('\n');
			}
			else
				normalized.push_back (record[i]);
		}
		record = std::move (normalized);
	}

	const auto command = record.find ("#!");
	const std::string_view hostRecord = std::string_view (record).substr (0, command);
	const auto equals = hostRecord.find ('=');
	if (equals == std::string_view::npos)
	{
		std::cout << "Invalid destination record." << std::endl;
		return 1;
	}
	const auto hostBase64 = hostRecord.substr (equals + 1);
	if (!identity.FromBase64 (hostBase64))
	{
		std::cout << "Invalid destination base64." << std::endl;
		return 1;
	}

	auto sigStart = record.find ("#sig=");
	if (sigStart == std::string::npos) sigStart = record.find ("!sig=");
	if (sigStart == std::string::npos)
	{
		std::cout << "Destination signature not found." << std::endl;
		return 1;
	}
	const size_t sigMarkerOffset = (sigStart > 0 && record[sigStart - 1] == '#') ? 1 : 0;
	const size_t sigKeyOffset = 5;
	const std::string_view hostNoSig = std::string_view (record).substr (0, sigStart - sigMarkerOffset);
	const std::string_view signatureText = std::string_view (record).substr (sigStart + sigKeyOffset);
	std::vector<uint8_t> signature (identity.GetSignatureLen ());
	if (i2p::data::Base64ToByteStream (signatureText, signature) != signature.size ())
	{
		std::cout << "Invalid destination signature encoding." << std::endl;
		return 1;
	}
	if (!identity.Verify (reinterpret_cast<const uint8_t *> (hostNoSig.data ()), hostNoSig.size (), signature.data ()))
	{
		std::cout << "Invalid destination signature." << std::endl;
		return 1;
	}

	if (const auto oldDest = FindValue (record, "olddest="))
	{
		if (!oldIdentity.FromBase64 (*oldDest))
		{
			std::cout << "Invalid old destination base64." << std::endl;
			return 1;
		}

		const auto oldSig = FindValue (record, "oldsig=");
		if (!oldSig)
		{
			std::cout << "Old destination signature not found." << std::endl;
			return 1;
		}

		const auto oldSigStart = record.find ("#oldsig=");
		if (oldSigStart == std::string::npos)
		{
			std::cout << "Old destination signature not found." << std::endl;
			return 1;
		}
		const std::string_view hostNoOldSig (record.data (), oldSigStart);
		std::vector<uint8_t> oldSignature (oldIdentity.GetSignatureLen ());
		if (i2p::data::Base64ToByteStream (*oldSig, oldSignature) != oldSignature.size ()
			|| !oldIdentity.Verify (
				reinterpret_cast<const uint8_t *> (hostNoOldSig.data ()), hostNoOldSig.size (), oldSignature.data ()))
		{
			std::cout << "Invalid old destination signature." << std::endl;
			return 1;
		}
	}

	std::cout << "Valid destination" << std::endl;
	return 0;
}

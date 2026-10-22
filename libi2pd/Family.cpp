/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: Family.
 *
 * Loads P-256 family certificates from the certificates/family directory,
 * extracts the CN family name and verifies ECDSA-SHA256 signatures over the
 * family name concatenated with the router identity. It also generates the
 * corresponding base64 signature from the family private key; certificates on
 * other curves and duplicate family names are rejected.
 *
 */

#include <array>
#include <cstring>
#include <memory>
#include <mutex>
#include <string_view>
#include <openssl/ssl.h>
#include <openssl/ecdsa.h>
#include <openssl/objects.h>
#include "Crypto.h"
#include "FS.h"
#include "Log.h"
#include "Family.h"
#include "Config.h"

namespace i2p
{
namespace data
{
	namespace
	{
		using SslCtxPtr = std::unique_ptr<SSL_CTX, decltype (&SSL_CTX_free)>;
		using SslPtr = std::unique_ptr<SSL, decltype (&SSL_free)>;
		using PkeyPtr = std::unique_ptr<EVP_PKEY, decltype (&EVP_PKEY_free)>;
		using MdCtxPtr = std::unique_ptr<EVP_MD_CTX, decltype (&EVP_MD_CTX_free)>;
		using EcdsaSigPtr = std::unique_ptr<ECDSA_SIG, decltype (&ECDSA_SIG_free)>;
		using BnPtr = std::unique_ptr<BIGNUM, decltype (&BN_free)>;

		std::string GetFamilyName (X509 * cert)
		{
			if (!cert) return {};
			std::array<char, 100> name {};
			if (!X509_NAME_oneline (X509_get_issuer_name (cert), name.data (), name.size ())) return {};
			std::string_view view (name.data ());
			const auto cn = view.find ("CN=");
			if (cn == std::string_view::npos) return {};
			const auto start = cn + 3;
			auto end = view.find (".family", start);
			if (end == std::string_view::npos) end = view.find ('/', start);
			if (end == std::string_view::npos) end = view.size ();
			return std::string (view.substr (start, end - start));
		}
	}

	static int GetFamilyKeyCurve (EVP_PKEY * pkey)
	{
#if I2PD_HAVE_EVP_PKEY_IS_A && I2PD_HAVE_EVP_PKEY_GROUP_NAME
		if (!pkey || EVP_PKEY_is_a (pkey, "EC") != 1) return -1;
		char groupName[80];
		size_t groupLength = 0;
		if (EVP_PKEY_get_group_name (pkey, groupName, sizeof (groupName), &groupLength) != 1
			|| groupLength == 0)
			return -1;
		return OBJ_txt2nid (groupName);
#else
		if (!pkey) return -1;
		const EC_KEY * ec = EVP_PKEY_get0_EC_KEY (pkey);
		const EC_GROUP * group = ec ? EC_KEY_get0_group (ec) : nullptr;
		return group ? EC_GROUP_get_curve_name (group) : -1;
#endif
	}

	Families::Families () = default;
	Families::~Families () = default;

	void Families::LoadCertificate (const std::string & filename)
	{
		SslCtxPtr ctx (SSL_CTX_new (TLS_method ()), SSL_CTX_free);
		if (!ctx)
		{
			LogPrint (eLogError, "Family: Can't create SSL context for ", filename);
			return;
		}
		if (SSL_CTX_use_certificate_file (ctx.get (), filename.c_str (), SSL_FILETYPE_PEM) != 1)
		{
			LogPrint (eLogError, "Family: Can't open certificate file ", filename);
			return;
		}

		SslPtr ssl (SSL_new (ctx.get ()), SSL_free);
		if (!ssl) return;
		X509 * cert = SSL_get_certificate (ssl.get ());
		if (!cert) return;

		const std::string family = GetFamilyName (cert);
		if (family.empty () || family.find_first_of ("/\\\r\n") != std::string::npos)
		{
			LogPrint (eLogWarning, "Family: Invalid family name in ", filename);
			return;
		}

		PkeyPtr pkey (X509_get_pubkey (cert), EVP_PKEY_free);
		if (!pkey) return;
		const int curve = GetFamilyKeyCurve (pkey.get ());
		if (curve != NID_X9_62_prime256v1)
		{
			LogPrint (eLogWarning, "Family: elliptic curve ", curve, " is not supported");
			return;
		}

		FamilySigningKey entry { std::move (pkey), static_cast<FamilyID> (m_SigningKeys.size () + 1) };
		if (!m_SigningKeys.emplace (family, std::move (entry)).second)
			LogPrint (eLogError, "Family: Duplicated family name ", family);
	}

	void Families::LoadCertificates ()
	{
		std::unique_lock lock (m_SigningKeysMutex);
		std::string certDir = i2p::fs::GetCertsDir () + i2p::fs::dirSep + "family";

		std::vector<std::string> files;
		int numCertificates = 0;

		if (!i2p::fs::ReadDir (certDir, files))
		{
			LogPrint (eLogWarning, "Family: Can't load family certificates from ", certDir);
			return;
		}

		for (const std::string & file : files)
		{
			if (file.size () < 4 || file.compare (file.size () - 4, 4, ".crt") != 0)
			{
				LogPrint (eLogWarning, "Family: ignoring file ", file);
				continue;
			}
			LoadCertificate (file);
			numCertificates++;
		}
		LogPrint (eLogInfo, "Family: ", numCertificates, " certificates loaded");
	}

	bool Families::VerifyFamily (
		const std::string & family, const IdentHash & ident, std::string_view signature, const char * /* key */) const
	{
		std::array<uint8_t, 100> buf {};
		const size_t familyLength = family.length ();
		if (familyLength + IdentHash::len > buf.size ())
		{
			LogPrint (eLogError, "Family: ", family, " is too long");
			return false;
		}

		std::shared_lock lock (m_SigningKeysMutex);
		auto it = m_SigningKeys.find (family);
		if (it == m_SigningKeys.end () || !it->second.pkey)
		{
			// Preserve the existing policy: an unknown family is not a
			// signature failure here; RouterInfo policy decides whether it is
			// acceptable. No key material is available to verify.
			return true;
		}

		std::array<uint8_t, 64> signatureBuf {};
		const auto signatureBufLen = Base64ToByteStream (signature, signatureBuf);
		if (signatureBufLen != signatureBuf.size ())
		{
			// A known family must authenticate with exactly one P-256 r||s
			// signature. Returning true here would let any peer claim family
			// membership with malformed base64.
			LogPrint (eLogWarning, "Family: ", family, " signature is not 64 bytes");
			return false;
		}

		std::memcpy (buf.data (), family.data (), familyLength);
		std::memcpy (buf.data () + familyLength, ident.data (), IdentHash::len);
		const size_t messageLength = familyLength + IdentHash::len;

		EcdsaSigPtr sig (ECDSA_SIG_new (), ECDSA_SIG_free);
		BnPtr r (BN_bin2bn (signatureBuf.data (), 32, nullptr), BN_free);
		BnPtr s (BN_bin2bn (signatureBuf.data () + 32, 32, nullptr), BN_free);
		if (!sig || !r || !s || ECDSA_SIG_set0 (sig.get (), r.get (), s.get ()) != 1) return false;
		// ECDSA_SIG_set0() took ownership on success.
		(void)r.release ();
		(void)s.release ();

		std::array<uint8_t, 72> der {};
		uint8_t * derPos = der.data ();
		const int derLength = i2d_ECDSA_SIG (sig.get (), &derPos);
		if (derLength <= 0 || static_cast<size_t> (derLength) > der.size ()) return false;

		MdCtxPtr ctx (EVP_MD_CTX_new (), EVP_MD_CTX_free);
		if (!ctx || EVP_DigestVerifyInit (ctx.get (), nullptr, EVP_sha256 (), nullptr, it->second.pkey.get ()) != 1)
			return false;
		return EVP_DigestVerify (ctx.get (), der.data (), static_cast<size_t> (derLength), buf.data (), messageLength)
			== 1;
	}

	FamilyID Families::GetFamilyID (const std::string & family) const
	{
		std::shared_lock lock (m_SigningKeysMutex);
		auto it = m_SigningKeys.find (family);
		if (it != m_SigningKeys.end ()) return it->second.id;
		return 0;
	}

	std::string CreateFamilySignature (const std::string & family, const IdentHash & ident)
	{
		const auto filename = i2p::fs::DataDirPath ("family", (family + ".key"));
		if (family.length () + IdentHash::len > 100)
		{
			LogPrint (eLogError, "Family: ", family, " is too long");
			return {};
		}

		SslCtxPtr ctx (SSL_CTX_new (TLS_method ()), SSL_CTX_free);
		if (!ctx)
		{
			LogPrint (eLogError, "Family: Can't create SSL context for keys file: ", filename);
			return {};
		}
		if (SSL_CTX_use_PrivateKey_file (ctx.get (), filename.c_str (), SSL_FILETYPE_PEM) != 1)
		{
			LogPrint (eLogError, "Family: Can't open keys file: ", filename);
			return {};
		}

		SslPtr ssl (SSL_new (ctx.get ()), SSL_free);
		if (!ssl) return {};
		EVP_PKEY * pkey = SSL_get_privatekey (ssl.get ());
		if (!pkey)
		{
			LogPrint (eLogError, "Family: No private key in ", filename);
			return {};
		}
		const int curve = GetFamilyKeyCurve (pkey);
		if (curve != NID_X9_62_prime256v1)
		{
			LogPrint (eLogWarning, "Family: elliptic curve ", curve, " is not supported");
			return {};
		}

		std::array<uint8_t, 100> buf {};
		std::memcpy (buf.data (), family.data (), family.size ());
		std::memcpy (buf.data () + family.size (), ident.data (), IdentHash::len);
		const size_t messageLength = family.size () + IdentHash::len;

		MdCtxPtr mdCtx (EVP_MD_CTX_new (), EVP_MD_CTX_free);
		if (!mdCtx || EVP_DigestSignInit (mdCtx.get (), nullptr, EVP_sha256 (), nullptr, pkey) != 1) return {};

		std::array<uint8_t, 72> der {};
		size_t derLength = der.size ();
		if (EVP_DigestSign (mdCtx.get (), der.data (), &derLength, buf.data (), messageLength) != 1
			|| derLength == 0 || derLength > der.size ())
			return {};

		const uint8_t * derPos = der.data ();
		EcdsaSigPtr sig (d2i_ECDSA_SIG (nullptr, &derPos, static_cast<long> (derLength)), ECDSA_SIG_free);
		if (!sig || derPos != der.data () + derLength) return {};
		const BIGNUM *r = nullptr, *s = nullptr;
		ECDSA_SIG_get0 (sig.get (), &r, &s);
		std::array<uint8_t, 64> signature {};
		if (!r || !s || !i2p::crypto::bn2buf (r, signature.data (), 32)
			|| !i2p::crypto::bn2buf (s, signature.data () + 32, 32))
			return {};
		return ByteStreamToBase64 (signature.data (), signature.size ());
	}
}
}

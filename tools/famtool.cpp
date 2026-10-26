/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: famtool.
 *
 * Creates a router-family signing key and certificate, signs a RouterInfo's
 * family properties, or verifies signed family membership. The family name is
 * capped so the name-plus-identity signature input stays bounded.
 *
 */

/**
 * famtool - a tool for creating and verifying router families
 */
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include "common/getopt.h"
#include "Crypto.h"
#include "RouterInfo.h"
#include "Base.h"
#include "FS.h"
#include "common/file.hpp"
#include "common/keys.hpp"
#include "common/crypto.hpp"
#include <openssl/x509.h>
#include <openssl/pem.h>
#include <openssl/evp.h>
#include <openssl/ec.h>
#include <openssl/ecdsa.h>
#if I2PD_HAVE_EVP_PKEY_BN_PARAM && !defined(LIBRESSL_VERSION_NUMBER)
#include <openssl/core_names.h>
#endif

using namespace i2p::crypto;
using namespace i2p::data;

namespace
{
// family name || identity hash is signed as a single buffer
constexpr size_t MAX_FAMILY_SIGNATURE_INPUT = 50;
constexpr size_t MAX_FAMILY_NAME_LENGTH = MAX_FAMILY_SIGNATURE_INPUT - IdentHash::len;

/**
 * A family name is written into RouterInfo properties and the certificate CN.
 * Keep it to the documented label alphabet; ';' or a newline would inject an
 * extra property.
 */
bool IsValidFamilyName (std::string_view name)
{
	if (name.empty () || name.size () > MAX_FAMILY_NAME_LENGTH) return false;
	for (unsigned char ch : name)
	{
		const bool allowed = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9')
			|| ch == '-' || ch == '_' || ch == '.';
		if (!allowed) return false;
	}
	return true;
}

/** RouterInfo's constructor throws; tools report a value instead. */
std::unique_ptr<RouterInfo> LoadRouterInfo (const std::string & path)
{
	try
	{
		return std::make_unique<RouterInfo> (path);
	}
	catch (...)
	{
		return nullptr;
	}
}

struct FileCloser
{
	void operator() (FILE * file) const
	{
		if (file) std::fclose (file);
	}
};
using FilePtr = std::unique_ptr<FILE, FileCloser>;
}

static void usage (const std::string & name)
{
	std::cout << "usage: " << name
			  << " [-h] [-v] [-g -n family -c family.crt -k family.pem] [-s -n family -k family.pem -i router.keys -f "
				 "router.info] [-V -c family.crt -f router.info]"
			  << std::endl;
}

static void printhelp (const std::string & name)
{
	usage (name);
	std::cout << std::endl;
	std::cout << "generate a new family signing key for family called ``i2pfam''" << std::endl;
	std::cout << name << " -g -n i2pfam -c myfam.crt -k myfam.pem" << std::endl << std::endl;
	std::cout << "sign a router info with family signing key" << std::endl;
	std::cout << name << " -s -n i2pfam -k myfam.pem -i router.keys -f router.info" << std::endl << std::endl;
	std::cout << "verify signed router.info" << std::endl;
	std::cout << name << " -V -n i2pfam -c myfam.pem -f router.info" << std::endl << std::endl;
}

static bool IsP256Key (EVP_PKEY * pkey)
{
#if I2PD_HAVE_EVP_PKEY_IS_A && I2PD_HAVE_EVP_PKEY_GROUP_NAME
	if (!pkey || EVP_PKEY_is_a (pkey, "EC") != 1) return false;
	char group[80];
	size_t groupLength = 0;
	if (EVP_PKEY_get_group_name (pkey, group, sizeof (group), &groupLength) != 1) return false;
	return groupLength > 0 && OBJ_txt2nid (group) == NID_X9_62_prime256v1;
#else
	// LibreSSL and older compatibility builds retain the EC_KEY accessor.
	// The OpenSSL 3/4 path above never relies on a deprecated low-level API.
	std::unique_ptr<EC_KEY, decltype (&EC_KEY_free)> ecKey (EVP_PKEY_get1_EC_KEY (pkey), EC_KEY_free);
	if (!ecKey) return false;
	const EC_GROUP * group = EC_KEY_get0_group (ecKey.get ());
	return group && EC_GROUP_get_curve_name (group) == NID_X9_62_prime256v1;
#endif
}

static bool GetP256PublicKey (EVP_PKEY * pkey, uint8_t * signingKey)
{
	std::unique_ptr<BIGNUM, decltype (&BN_free)> x (nullptr, BN_free);
	std::unique_ptr<BIGNUM, decltype (&BN_free)> y (nullptr, BN_free);
#if I2PD_HAVE_EVP_PKEY_BN_PARAM && !defined(LIBRESSL_VERSION_NUMBER)
	BIGNUM * xRaw = nullptr;
	BIGNUM * yRaw = nullptr;
	if (EVP_PKEY_get_bn_param (pkey, OSSL_PKEY_PARAM_EC_PUB_X, &xRaw) != 1
		|| EVP_PKEY_get_bn_param (pkey, OSSL_PKEY_PARAM_EC_PUB_Y, &yRaw) != 1)
	{
		BN_free (xRaw);
		BN_free (yRaw);
		return false;
	}
	x.reset (xRaw);
	y.reset (yRaw);
#else
	std::unique_ptr<EC_KEY, decltype (&EC_KEY_free)> ecKey (EVP_PKEY_get1_EC_KEY (pkey), EC_KEY_free);
	if (!ecKey) return false;
	const EC_GROUP * group = EC_KEY_get0_group (ecKey.get ());
	const EC_POINT * point = EC_KEY_get0_public_key (ecKey.get ());
	if (!group || !point) return false;
	x.reset (BN_new ());
	y.reset (BN_new ());
	if (!x || !y) return false;
	if (EC_POINT_get_affine_coordinates_GFp (group, point, x.get (), y.get (), nullptr) != 1) return false;
#endif
	return bn2buf (x.get (), signingKey, ECDSAP256_KEY_LENGTH / 2)
		&& bn2buf (y.get (), signingKey + ECDSAP256_KEY_LENGTH / 2, ECDSAP256_KEY_LENGTH / 2);
}

static std::shared_ptr<Verifier> LoadCertificate (const std::string & filename)
{
	std::unique_ptr<BIO, decltype (&BIO_free)> bio (BIO_new_file (filename.c_str (), "r"), BIO_free);
	std::unique_ptr<X509, decltype (&X509_free)> cert (
		bio ? PEM_read_bio_X509 (bio.get (), nullptr, nullptr, nullptr) : nullptr, X509_free);
	if (!cert) return nullptr;

	std::unique_ptr<EVP_PKEY, decltype (&EVP_PKEY_free)> pkey (X509_get_pubkey (cert.get ()), EVP_PKEY_free);
	if (!pkey || !IsP256Key (pkey.get ())) return nullptr;

	uint8_t signingKey[ECDSAP256_KEY_LENGTH];
	if (!GetP256PublicKey (pkey.get (), signingKey)) return nullptr;

	auto verifier = std::make_shared<ECDSAP256Verifier> ();
	verifier->SetPublicKey (signingKey);
	return verifier;
}

/// @brief Sign `family || identityHash` with the family's P-256 key.
///
/// The signed input is the family label followed by the 32-byte identity hash,
/// bounded by MAX_FAMILY_SIGNATURE_INPUT so a family name cannot make the
/// signature cover an unbounded buffer. The private key is read from PEM, must
/// be P-256 (nothing else is a valid family key), and the result is the two
/// fixed 32-byte r/s values in I2P family-signature wire form.
///
/// @param family Validated family label.
/// @param ident Router identity the signature is bound to.
/// @param filename PEM private key of the family.
/// @param sig Receives base64 of the raw signature; cleared on entry.
/// @return false when the inputs are unusable or the key is not P-256.
/// @warning The key file contents are never printed or logged.
static bool CreateFamilySignature (
	const std::string & family, const IdentHash & ident, const std::string & filename, std::string & sig)
{
	sig.clear ();
	if (family.size () + IdentHash::len > MAX_FAMILY_SIGNATURE_INPUT) return false;

	std::unique_ptr<BIO, decltype (&BIO_free)> bio (BIO_new_file (filename.c_str (), "r"), BIO_free);
	std::unique_ptr<EVP_PKEY, decltype (&EVP_PKEY_free)> pkey (
		bio ? PEM_read_bio_PrivateKey (bio.get (), nullptr, nullptr, nullptr) : nullptr, EVP_PKEY_free);
	if (!pkey || !IsP256Key (pkey.get ())) return false;

	std::vector<uint8_t> message;
	message.reserve (family.size () + IdentHash::len);
	message.insert (message.end (), family.begin (), family.end ());
	message.insert (message.end (), ident.data (), ident.data () + IdentHash::len);

	// Sign the family/identity buffer through EVP so OpenSSL 3/4 and LibreSSL
	// share the same ownership and error handling. The wire format is still
	// the two fixed 32-byte r/s values expected by I2P family signatures.
	std::unique_ptr<EVP_MD_CTX, decltype (&EVP_MD_CTX_free)> mdCtx (EVP_MD_CTX_new (), EVP_MD_CTX_free);
	if (!mdCtx || EVP_DigestSignInit (mdCtx.get (), nullptr, EVP_sha256 (), nullptr, pkey.get ()) != 1) return false;

	if (EVP_DigestSignUpdate (mdCtx.get (), message.data (), message.size ()) != 1) return false;

	size_t derLength = 0;
	if (EVP_DigestSignFinal (mdCtx.get (), nullptr, &derLength) != 1 || derLength == 0 || derLength > 128) return false;

	std::vector<uint8_t> der (derLength);
	if (EVP_DigestSignFinal (mdCtx.get (), der.data (), &derLength) != 1) return false;
	der.resize (derLength);

	const unsigned char * derPos = der.data ();
	std::unique_ptr<ECDSA_SIG, decltype (&ECDSA_SIG_free)> ecdsaSig (
		d2i_ECDSA_SIG (nullptr, &derPos, static_cast<long> (der.size ())), ECDSA_SIG_free);
	if (!ecdsaSig || derPos != der.data () + der.size ()) return false;

	uint8_t signature[ECDSAP256_KEY_LENGTH];
	const BIGNUM *r = nullptr, *s = nullptr;
	ECDSA_SIG_get0 (ecdsaSig.get (), &r, &s);
	if (!r || !s || !bn2buf (r, signature, ECDSAP256_KEY_LENGTH / 2)
		|| !bn2buf (s, signature + ECDSAP256_KEY_LENGTH / 2, ECDSAP256_KEY_LENGTH / 2))
		return false;

	sig = ByteStreamToBase64 (signature, sizeof (signature));
	return !sig.empty ();
}

/// @brief Generate a family key/certificate, sign a RouterInfo, or verify one.
///
/// At least one of `-g` (generate), `-s` (sign) or `-V` (verify) is required,
/// together with a family name (`-n`); the selected modes run in that order.
/// File roles differ per mode: `-k` is the PEM private key (the new key when
/// generating, the signing key otherwise), `-c` the certificate (the new
/// certificate when generating, the verification certificate otherwise);
/// omitted paths default to `&lt;family&gt;.key`/`&lt;family&gt;.crt` when generating.
///
/// Generation writes both PEM objects in memory and installs them through the
/// validated atomic helpers: the private key uses the core owner-only writer
/// (no in-place truncation, no symlink follow) and the certificate the public
/// writer. Each file is replaced atomically, but the pair is not one
/// transaction: a failure after the certificate was installed leaves that
/// certificate in place without a matching key file.
///
/// `-o` is accepted for compatibility with the historical tool and ignored;
/// `-v` only enables progress lines on stdout. Diagnostics stay on stderr.
///
/// @return 0 on success, 1 for usage, mode, key/certificate or write failures.
int main (int argc, char * argv[])
{
	if (argc == 1)
	{
		usage (argv[0]);
		return 1;
	}
	int opt;
	bool verbose = false;
	bool help = false;
	bool gen = false;
	bool sign = false;
	bool verify = false;
	std::string fam;
	std::string privkey;
	std::string certfile;
	std::string infile;
	std::string infofile;
	std::string outfile;
	while ((opt = getopt (argc, argv, "vVhgsn:i:c:k:o:f:")) != -1)
	{
		switch (opt)
		{
		case 'v': verbose = true; break;
		case 'h': help = true; break;
		case 'g': gen = true; break;
		case 'n':
			if (!optarg)
			{
				usage (argv[0]);
				return 1;
			}
			fam = optarg;
			if (!IsValidFamilyName (fam))
			{
				std::cerr << "family name must use letters, digits, '.', '_' or '-' and fit " << MAX_FAMILY_NAME_LENGTH
						  << " bytes" << std::endl;
				return 1;
			}
			break;
		case 'f':
			if (!optarg)
			{
				usage (argv[0]);
				return 1;
			}
			infofile = optarg;
			break;
		case 'i':
			if (!optarg)
			{
				usage (argv[0]);
				return 1;
			}
			infile = optarg;
			break;
		case 'o':
			if (!optarg)
			{
				usage (argv[0]);
				return 1;
			}
			outfile = optarg;
			break;
		case 'c':
			if (!optarg)
			{
				usage (argv[0]);
				return 1;
			}
			certfile = optarg;
			break;
		case 'k':
			if (!optarg)
			{
				usage (argv[0]);
				return 1;
			}
			privkey = optarg;
			break;
		case 'V': verify = true; break;
		case 's': sign = true; break;
		default: usage (argv[0]); return 1;
		}
	}
	if (help)
	{
		printhelp (argv[0]);
		return 0;
	}
	// -o is accepted for compatibility with the original tool but unused
	(void)outfile;
	if (optind < argc)
	{
		std::cerr << "unexpected argument: " << argv[optind] << std::endl;
		usage (argv[0]);
		return 1;
	}

	i2p::tools::CryptoGuard cryptoGuard;

	if (!fam.size ())
	{
		// no family name
		std::cerr << "no family name specified" << std::endl;
		return 1;
	}
	if (!gen && !sign && !verify)
	{
		std::cerr << "no mode selected: use -g, -s or -V" << std::endl;
		usage (argv[0]);
		return 1;
	}
	// generate family key code
	if (gen)
	{
		if (!privkey.size ()) privkey = fam + ".key";
		if (!certfile.size ()) certfile = fam + ".crt";

		std::string cn = fam + ".family.i2p.net";

#if I2PD_HAVE_EVP_EC_GEN
		std::unique_ptr<EVP_PKEY, decltype (&EVP_PKEY_free)> evKey (EVP_EC_gen ("P-256"), EVP_PKEY_free);
		if (!evKey)
		{
			std::cerr << "cannot generate EC key" << std::endl;
			return 1;
		}
#else
		std::unique_ptr<EC_KEY, decltype (&EC_KEY_free)> key (
			EC_KEY_new_by_curve_name (NID_X9_62_prime256v1), EC_KEY_free);
		if (!key)
		{
			std::cerr << "cannot create EC key" << std::endl;
			return 1;
		}
		EC_KEY_set_asn1_flag (key.get (), OPENSSL_EC_NAMED_CURVE);
		if (EC_KEY_generate_key (key.get ()) != 1 || EC_KEY_check_key (key.get ()) != 1)
		{
			std::cerr << "cannot generate EC key" << std::endl;
			return 1;
		}
		std::unique_ptr<EVP_PKEY, decltype (&EVP_PKEY_free)> evKey (EVP_PKEY_new (), EVP_PKEY_free);
		if (!evKey || EVP_PKEY_assign_EC_KEY (evKey.get (), key.get ()) != 1)
		{
			std::cerr << "cannot allocate EC key" << std::endl;
			return 1;
		}
		EC_KEY * k_priv = key.release ();
#endif
		if (verbose) std::cout << "generated key" << std::endl;

		std::unique_ptr<X509, decltype (&X509_free)> x (X509_new (), X509_free);
		if (!x)
		{
			std::cerr << "cannot allocate certificate" << std::endl;
			return 1;
		}

		if (X509_set_version (x.get (), 2) != 1 || ASN1_INTEGER_set (X509_get_serialNumber (x.get ()), 0) != 1
			|| !X509_gmtime_adj (X509_getm_notBefore (x.get ()), 0) ||
			// TODO: make expiration date configurable
			!X509_gmtime_adj (X509_getm_notAfter (x.get ()), (long)60 * 60 * 24 * 365 * 10)
			|| X509_set_pubkey (x.get (), evKey.get ()) != 1)
		{
			std::cerr << "cannot initialize certificate" << std::endl;
			return 1;
		}

		// X509_get_subject_name() is const in OpenSSL 3/4. Work on an owned
		// duplicate instead of casting away const, then install it explicitly.
		std::unique_ptr<X509_NAME, decltype (&X509_NAME_free)> name (
			X509_NAME_dup (X509_get_subject_name (x.get ())), X509_NAME_free);
		if (!name)
		{
			std::cerr << "certificate has no subject" << std::endl;
			return 1;
		}
		const auto addName = [&name] (const char * field, const char * value)
		{
			return X509_NAME_add_entry_by_txt (
					   name.get (), field, MBSTRING_ASC, reinterpret_cast<const unsigned char *> (value), -1, -1, 0)
				== 1;
		};
		if (!addName ("C", "XX") || !addName ("ST", "XX") || !addName ("L", "XX")
			|| !addName ("O", "I2P Anonymous Network") || !addName ("OU", "family") || !addName ("CN", cn.c_str ())
			|| X509_set_subject_name (x.get (), name.get ()) != 1 || X509_set_issuer_name (x.get (), name.get ()) != 1)
		{
			std::cerr << "cannot set certificate name" << std::endl;
			return 1;
		}

		if (verbose) std::cout << "signing cert" << std::endl;
		if (X509_sign (x.get (), evKey.get (), EVP_sha256 ()) == 0)
		{
			std::cerr << "cannot sign certificate" << std::endl;
			return 1;
		}

		// Encode both PEM objects in memory and install them through the
		// validated no-follow/atomic helpers. A direct fopen() follows symlinks,
		// truncates in place and briefly creates a key with the process umask.
		using BioPtr = std::unique_ptr<BIO, decltype (&BIO_free)>;
		BioPtr keyBio (BIO_new (BIO_s_mem ()), BIO_free);
		BioPtr certBio (BIO_new (BIO_s_mem ()), BIO_free);
		if (!keyBio || !certBio)
		{
			std::cerr << "cannot allocate PEM buffers" << std::endl;
			return 1;
		}
#if I2PD_HAVE_EVP_EC_GEN
		const bool keyWritten
			= PEM_write_bio_PrivateKey (keyBio.get (), evKey.get (), nullptr, nullptr, 0, nullptr, nullptr) == 1;
#else
		const bool keyWritten
			= PEM_write_bio_ECPrivateKey (keyBio.get (), k_priv, nullptr, nullptr, 0, nullptr, nullptr) == 1;
#endif
		const bool certWritten = PEM_write_bio_X509 (certBio.get (), x.get ()) == 1;
		char * keyData = nullptr;
		char * certData = nullptr;
		const long keyLen = keyWritten ? BIO_get_mem_data (keyBio.get (), &keyData) : 0;
		const long certLen = certWritten ? BIO_get_mem_data (certBio.get (), &certData) : 0;
		std::string keyPem (keyData ? keyData : "", keyLen > 0 ? static_cast<size_t> (keyLen) : 0);
		std::string certPem (certData ? certData : "", certLen > 0 ? static_cast<size_t> (certLen) : 0);
		const bool wrote = keyWritten && certWritten && !keyPem.empty () && !certPem.empty ()
			&& i2p::fs::AtomicWrite (certfile, certPem) && i2p::fs::AtomicWritePrivate (privkey, keyPem);
		if (!keyPem.empty ()) OPENSSL_cleanse (keyPem.data (), keyPem.size ());
		if (!wrote)
		{
			std::cerr << "cannot write family key/certificate" << std::endl;
			return 1;
		}
		if (verbose) std::cout << "wrote private key and certificate" << std::endl;

		std::cout << "family " << fam << " made" << std::endl;
	}

	if (sign)
	{
		// sign
		if (!infile.size ())
		{
			// no router info specified
			std::cerr << "no router keys file specified" << std::endl;
			return 1;
		}
		if (!privkey.size ())
		{
			// no private key specified
			std::cerr << "no private key specified" << std::endl;
			return 1;
		}
		if (!infofile.size ())
		{
			std::cerr << "no router info file specified" << std::endl;
			return 1;
		}

		if (verbose) std::cout << "load " << infofile << std::endl;

		PrivateKeys keys;
		if (auto loaded = i2p::tools::LoadPrivateKeys (infile, keys); !loaded)
		{
			std::cerr << "invalid key file " << infile << ": " << loaded.error () << std::endl;
			return 1;
		}

		auto routerInfo = LoadRouterInfo (infofile);
		if (!routerInfo)
		{
			std::cerr << "cannot read router info " << infofile << std::endl;
			return 1;
		}
		auto identity = routerInfo->GetRouterIdentity ();
		if (!identity || !routerInfo->GetBuffer () || !routerInfo->GetBufferLen ())
		{
			std::cerr << "cannot read router info " << infofile << std::endl;
			return 1;
		}

		LocalRouterInfo ri;
		ri.SetRouterIdentity (identity);
		if (!ri.Update (routerInfo->GetBuffer (), routerInfo->GetBufferLen ()) || ri.IsUnreachable ())
		{
			std::cerr << "invalid router info " << infofile << std::endl;
			return 1;
		}

		auto keysIdentity = keys.GetPublic ();
		if (!keysIdentity || keysIdentity->GetIdentHash () != ri.GetIdentHash ())
		{
			std::cerr << "router keys do not match " << infofile << std::endl;
			return 1;
		}

		auto ident = ri.GetIdentHash ();

		if (verbose) std::cout << "add " << ident.ToBase64 () << " to " << fam << std::endl;
		std::string sig;
		if (!CreateFamilySignature (fam, ident, privkey, sig))
		{
			std::cerr << "failed to sign router info" << std::endl;
			return 1;
		}
		ri.SetProperty (ROUTER_INFO_PROPERTY_FAMILY, fam);
		ri.SetProperty (ROUTER_INFO_PROPERTY_FAMILY_SIG, sig);
		if (verbose) std::cout << "signed " << sig << std::endl;
		ri.CreateBuffer (keys);
		if (!ri.SaveToFile (infofile))
		{
			std::cerr << "failed to save to " << infofile << std::endl;
			return 1;
		}
		std::cout << "signed" << std::endl;
	}

	if (verify)
	{
		if (!infofile.size ())
		{
			std::cout << "no router info file specified" << std::endl;
			return 1;
		}
		if (!certfile.size ())
		{
			std::cout << "no family certificate specified" << std::endl;
			return 1;
		}
		auto v = LoadCertificate (certfile);
		if (!v)
		{
			std::cout << "invalid certificate" << std::endl;
			return 1;
		}

		if (verbose) std::cout << "load " << infofile << std::endl;

		auto routerInfo = LoadRouterInfo (infofile);
		if (!routerInfo)
		{
			std::cerr << "cannot read router info " << infofile << std::endl;
			return 1;
		}
		auto identity = routerInfo->GetRouterIdentity ();
		if (!identity || !routerInfo->GetBuffer () || !routerInfo->GetBufferLen ())
		{
			std::cerr << "cannot read router info " << infofile << std::endl;
			return 1;
		}

		LocalRouterInfo ri;
		ri.SetRouterIdentity (identity);
		if (!ri.Update (routerInfo->GetBuffer (), routerInfo->GetBufferLen ()) || ri.IsUnreachable ())
		{
			std::cout << "invalid router info " << infofile << std::endl;
			return 1;
		}

		auto sig = ri.GetProperty (ROUTER_INFO_PROPERTY_FAMILY_SIG);
		if (ri.GetProperty (ROUTER_INFO_PROPERTY_FAMILY) != fam)
		{
			std::cout << infofile << " does not belong to " << fam << std::endl;
			return 1;
		}
		auto ident = ri.GetIdentHash ();

		if (fam.size () + IdentHash::len > MAX_FAMILY_SIGNATURE_INPUT)
		{
			std::cout << "family name too long" << std::endl;
			return 1;
		}

		std::vector<uint8_t> message;
		message.reserve (fam.size () + IdentHash::len);
		message.insert (message.end (), fam.begin (), fam.end ());
		message.insert (message.end (), ident.data (), ident.data () + IdentHash::len);

		std::vector<uint8_t> sigbuf (v->GetSignatureLen ());
		if (sig.empty () || sigbuf.empty () || i2p::data::Base64ToByteStream (sig, sigbuf) != sigbuf.size ()
			|| !v->Verify (message.data (), message.size (), sigbuf.data ()))
		{
			std::cout << "invalid signature" << std::endl;
			return 1;
		}
		std::cout << "verified" << std::endl;
	}
	return 0;
}

/*
 * Copyright (c) 2013-2026, The PurpleI2P Project
 * Copyright (c) 2026, David Uhden Collado
 * SPDX-License-Identifier: BSD-3-Clause
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
#include <vector>
#include <unistd.h>
#include "Crypto.h"
#include "RouterInfo.h"
#include "Base.h"
#include "common/file.hpp"
#include <openssl/x509.h>
#include <openssl/pem.h>
#include <openssl/evp.h>
#include <openssl/ec.h>
#include <openssl/ecdsa.h>
#include <openssl/ssl.h>
#include <openssl/sha.h>

using namespace i2p::crypto;
using namespace i2p::data;

namespace
{
// family name || identity hash is signed as a single buffer
constexpr size_t MAX_FAMILY_SIGNATURE_INPUT = 50;
constexpr size_t MAX_FAMILY_NAME_LENGTH = MAX_FAMILY_SIGNATURE_INPUT - IdentHash::len;

struct CryptoGuard
{
	CryptoGuard () { i2p::crypto::InitCrypto (false); }
	~CryptoGuard () { i2p::crypto::TerminateCrypto (); }
};

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

static std::shared_ptr<Verifier> LoadCertificate (const std::string & filename)
{
	std::unique_ptr<SSL_CTX, decltype (&SSL_CTX_free)> ctx (SSL_CTX_new (TLS_method ()), SSL_CTX_free);
	if (!ctx) return nullptr;

	if (SSL_CTX_use_certificate_file (ctx.get (), filename.c_str (), SSL_FILETYPE_PEM) != 1) return nullptr;

	std::unique_ptr<SSL, decltype (&SSL_free)> ssl (SSL_new (ctx.get ()), SSL_free);
	if (!ssl) return nullptr;

	X509 * cert = SSL_get_certificate (ssl.get ());
	if (!cert) return nullptr;

	std::unique_ptr<EVP_PKEY, decltype (&EVP_PKEY_free)> pkey (X509_get_pubkey (cert), EVP_PKEY_free);
	if (!pkey) return nullptr;

	// EVP_PKEY_get1_EC_KEY returns a new reference
	std::unique_ptr<EC_KEY, decltype (&EC_KEY_free)> ecKey (EVP_PKEY_get1_EC_KEY (pkey.get ()), EC_KEY_free);
	if (!ecKey) return nullptr;

	const EC_GROUP * group = EC_KEY_get0_group (ecKey.get ());
	if (!group || EC_GROUP_get_curve_name (group) != NID_X9_62_prime256v1) return nullptr;

	const EC_POINT * point = EC_KEY_get0_public_key (ecKey.get ());
	if (!point) return nullptr;

	std::unique_ptr<BIGNUM, decltype (&BN_free)> x (BN_new (), BN_free);
	std::unique_ptr<BIGNUM, decltype (&BN_free)> y (BN_new (), BN_free);
	if (!x || !y) return nullptr;

	if (EC_POINT_get_affine_coordinates_GFp (group, point, x.get (), y.get (), nullptr) != 1) return nullptr;

	uint8_t signingKey[ECDSAP256_KEY_LENGTH];
	if (!bn2buf (x.get (), signingKey, ECDSAP256_KEY_LENGTH / 2)
		|| !bn2buf (y.get (), signingKey + ECDSAP256_KEY_LENGTH / 2, ECDSAP256_KEY_LENGTH / 2))
		return nullptr;

	auto verifier = std::make_shared<ECDSAP256Verifier> ();
	verifier->SetPublicKey (signingKey);
	return verifier;
}

static bool CreateFamilySignature (
	const std::string & family, const IdentHash & ident, const std::string & filename, std::string & sig)
{
	sig.clear ();
	if (family.size () + IdentHash::len > MAX_FAMILY_SIGNATURE_INPUT) return false;

	std::unique_ptr<SSL_CTX, decltype (&SSL_CTX_free)> ctx (SSL_CTX_new (TLS_method ()), SSL_CTX_free);
	if (!ctx) return false;

	if (SSL_CTX_use_PrivateKey_file (ctx.get (), filename.c_str (), SSL_FILETYPE_PEM) != 1) return false;

	std::unique_ptr<SSL, decltype (&SSL_free)> ssl (SSL_new (ctx.get ()), SSL_free);
	if (!ssl) return false;

	EVP_PKEY * pkey = SSL_get_privatekey (ssl.get ());
	if (!pkey) return false;

	std::unique_ptr<EC_KEY, decltype (&EC_KEY_free)> ecKey (EVP_PKEY_get1_EC_KEY (pkey), EC_KEY_free);
	if (!ecKey) return false;

	const EC_GROUP * group = EC_KEY_get0_group (ecKey.get ());
	if (!group || EC_GROUP_get_curve_name (group) != NID_X9_62_prime256v1) return false;

	if (EC_KEY_check_key (ecKey.get ()) != 1) return false;

	uint8_t signingPrivateKey[ECDSAP256_KEY_LENGTH / 2];
	if (!bn2buf (EC_KEY_get0_private_key (ecKey.get ()), signingPrivateKey, sizeof (signingPrivateKey))) return false;

	std::vector<uint8_t> message;
	message.reserve (family.size () + IdentHash::len);
	message.insert (message.end (), family.begin (), family.end ());
	message.insert (message.end (), ident.data (), ident.data () + IdentHash::len);

	uint8_t digest[SHA256_DIGEST_LENGTH];
	SHA256 (message.data (), message.size (), digest);

	ECDSA_SIG * ecdsaSig = ECDSA_do_sign (digest, sizeof (digest), ecKey.get ());
	if (!ecdsaSig)
	{
		OPENSSL_cleanse (signingPrivateKey, sizeof (signingPrivateKey));
		return false;
	}

	uint8_t signature[ECDSAP256_KEY_LENGTH];
	const BIGNUM *r = nullptr, *s = nullptr;
	ECDSA_SIG_get0 (ecdsaSig, &r, &s);
	const bool encoded = bn2buf (r, signature, ECDSAP256_KEY_LENGTH / 2)
		&& bn2buf (s, signature + ECDSAP256_KEY_LENGTH / 2, ECDSAP256_KEY_LENGTH / 2);
	ECDSA_SIG_free (ecdsaSig);
	OPENSSL_cleanse (signingPrivateKey, sizeof (signingPrivateKey));
	OPENSSL_cleanse (digest, sizeof (digest));

	if (!encoded) return false;

	sig = ByteStreamToBase64 (signature, sizeof (signature));
	return !sig.empty ();
}

int main (int argc, char * argv[])
{
	if (argc == 1)
	{
		usage (argv[0]);
		return -1;
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
			if (fam.size () > MAX_FAMILY_NAME_LENGTH)
			{
				std::cout << "family name too long" << std::endl;
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
		default: usage (argv[0]); return -1;
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

	CryptoGuard cryptoGuard;

	if (!fam.size ())
	{
		// no family name
		std::cout << "no family name specified" << std::endl;
		return 1;
	}
	// generate family key code
	if (gen)
	{
		if (!privkey.size ()) privkey = fam + ".key";
		if (!certfile.size ()) certfile = fam + ".crt";

		std::string cn = fam + ".family.i2p.net";

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
		if (verbose) std::cout << "generated key" << std::endl;

		std::unique_ptr<EVP_PKEY, decltype (&EVP_PKEY_free)> evKey (EVP_PKEY_new (), EVP_PKEY_free);
		if (!evKey)
		{
			std::cerr << "cannot allocate key" << std::endl;
			return 1;
		}
		// evKey takes ownership of the EC key on success
		if (EVP_PKEY_assign_EC_KEY (evKey.get (), key.get ()) != 1)
		{
			std::cerr << "cannot assign EC key" << std::endl;
			return 1;
		}
		EC_KEY * k_priv = key.release ();

		std::unique_ptr<X509, decltype (&X509_free)> x (X509_new (), X509_free);
		if (!x)
		{
			std::cerr << "cannot allocate certificate" << std::endl;
			return 1;
		}

		if (X509_set_version (x.get (), 2) != 1 || ASN1_INTEGER_set (X509_get_serialNumber (x.get ()), 0) != 1
			|| !X509_gmtime_adj (X509_get_notBefore (x.get ()), 0) ||
			// TODO: make expiration date configurable
			!X509_gmtime_adj (X509_get_notAfter (x.get ()), (long)60 * 60 * 24 * 365 * 10)
			|| X509_set_pubkey (x.get (), evKey.get ()) != 1)
		{
			std::cerr << "cannot initialize certificate" << std::endl;
			return 1;
		}

		X509_NAME * name = const_cast<X509_NAME *> (X509_get_subject_name (x.get ()));
		if (!name)
		{
			std::cerr << "certificate has no subject" << std::endl;
			return 1;
		}
		const auto addName = [name] (const char * field, const char * value)
		{
			return X509_NAME_add_entry_by_txt (
					   name, field, MBSTRING_ASC, reinterpret_cast<const unsigned char *> (value), -1, -1, 0)
				== 1;
		};
		if (!addName ("C", "XX") || !addName ("ST", "XX") || !addName ("L", "XX")
			|| !addName ("O", "I2P Anonymous Network") || !addName ("OU", "family") || !addName ("CN", cn.c_str ())
			|| X509_set_issuer_name (x.get (), name) != 1)
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

		FilePtr privf (std::fopen (privkey.c_str (), "w"));
		if (!privf)
		{
			std::fprintf (stderr, "cannot open %s: %s\n", privkey.c_str (), std::strerror (errno));
			return 1;
		}

		FilePtr certf (std::fopen (certfile.c_str (), "w"));
		if (!certf)
		{
			std::fprintf (stderr, "cannot open %s: %s\n", certfile.c_str (), std::strerror (errno));
			return 1;
		}

		if (verbose) std::cout << "wrote private key" << std::endl;
		// TODO: password protection
		if (PEM_write_ECPrivateKey (privf.get (), k_priv, nullptr, nullptr, 0, nullptr, nullptr) != 1)
		{
			std::cerr << "cannot write private key to " << privkey << std::endl;
			return 1;
		}
		if (std::fclose (privf.release ()) != 0)
		{
			std::fprintf (stderr, "cannot close %s: %s\n", privkey.c_str (), std::strerror (errno));
			return 1;
		}

		if (verbose) std::cout << "writing certificate" << std::endl;
		if (PEM_write_X509 (certf.get (), x.get ()) != 1)
		{
			std::cerr << "cannot write certificate to " << certfile << std::endl;
			return 1;
		}
		if (std::fclose (certf.release ()) != 0)
		{
			std::fprintf (stderr, "cannot close %s: %s\n", certfile.c_str (), std::strerror (errno));
			return 1;
		}

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
		{
			auto keyData = i2p::tools::ReadFile (infile);
			if (!keyData)
			{
				std::cerr << keyData.error () << std::endl;
				return 1;
			}
			const bool parsed = keys.FromBuffer (keyData->data (), keyData->size ());
			i2p::tools::Wipe (*keyData);
			if (!parsed)
			{
				std::cerr << "invalid key file " << infile << std::endl;
				return 1;
			}
		}

		RouterInfo routerInfo (infofile);
		auto identity = routerInfo.GetRouterIdentity ();
		if (!identity || !routerInfo.GetBuffer () || !routerInfo.GetBufferLen ())
		{
			std::cerr << "cannot read router info " << infofile << std::endl;
			return 1;
		}

		LocalRouterInfo ri;
		ri.SetRouterIdentity (identity);
		if (!ri.Update (routerInfo.GetBuffer (), routerInfo.GetBufferLen ()) || ri.IsUnreachable ())
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

		RouterInfo routerInfo (infofile);
		auto identity = routerInfo.GetRouterIdentity ();
		if (!identity || !routerInfo.GetBuffer () || !routerInfo.GetBufferLen ())
		{
			std::cout << "cannot read router info " << infofile << std::endl;
			return 1;
		}

		LocalRouterInfo ri;
		ri.SetRouterIdentity (identity);
		if (!ri.Update (routerInfo.GetBuffer (), routerInfo.GetBufferLen ()) || ri.IsUnreachable ())
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

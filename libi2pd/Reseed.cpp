/*
* Copyright (c) 2013-2026, The PurpleI2P Project
* Copyright (c) 2026, David Uhden Collado
* SPDX-License-Identifier: BSD-3-Clause
*
* This file is part of openi2pd and licensed under BSD-3-Clause
*
* See full license text in LICENSE file at top of project tree
*/

#include <array>
#include <string.h>
#include <random>
#include <fstream>
#include <memory>
#include <sstream>
#include <utility>
#include <vector>
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/algorithm/string.hpp>
#include <openssl/ssl.h>
#include <openssl/err.h>
#ifndef LIBRESSL_VERSION_NUMBER // OpenSSL 3.0.2 is the minimum
#include <openssl/core_names.h>
#endif
#include <zlib.h>

#include "Crypto.h"
#include "I2PEndian.h"
#include "Reseed.h"
#include "FS.h"
#include "Log.h"
#include "Identity.h"
#include "NetDb.hpp"
#include "HTTP.h"
#include "util.h"
#include "Config.h"
#include "Socks5.h"

namespace i2p
{
namespace data
{
	const uint64_t MAX_RESEED_CONTENT_LENGTH = 128 * 1024 * 1024; // 128 MB, sanity cap for SU3 content length
	const uint32_t MAX_RESEED_FILE_SIZE = 64 * 1024 * 1024; // 64 MB, sanity cap for a single file inside the SU3 zip
	// HTTP headers, chunk framing and redirect bodies are small compared with
	// the signed payload. Refuse to buffer more than the SU3 cap plus a small
	// allowance so a malicious or broken reseed server cannot grow the
	// response string without bound.
	const size_t MAX_RESEED_HTTP_RESPONSE_SIZE = static_cast<size_t> (MAX_RESEED_CONTENT_LENGTH) + 1024 * 1024;

	Reseeder::Reseeder () { }

	Reseeder::~Reseeder () { }

	/**
	 @brief tries to bootstrap into I2P network (from local files and servers, with respect of options)
	 */
	void Reseeder::Bootstrap ()
	{
		std::string su3FileName;
		i2p::config::GetOption ("reseed.file", su3FileName);
		std::string zipFileName;
		i2p::config::GetOption ("reseed.zipfile", zipFileName);

		if (su3FileName.length () > 0) // bootstrap from SU3 file or URL
		{
			int num;
			if (su3FileName.length () > 8 && su3FileName.substr (0, 8) == "https://")
			{
				num = ReseedFromSU3Url (su3FileName); // from https URL
			}
			else
			{
				num = ProcessSU3File (su3FileName.c_str ());
			}
			if (num == 0) LogPrint (eLogWarning, "Reseed: Failed to reseed from ", su3FileName);
		}
		else if (zipFileName.length () > 0) // bootstrap from ZIP file
		{
			int num = ProcessZIPFile (zipFileName.c_str ());
			if (num == 0) LogPrint (eLogWarning, "Reseed: Failed to reseed from ", zipFileName);
		}
		else // bootstrap from reseed servers
		{
			auto start = i2p::util::GetMonotonicSeconds ();
			int num = 0;
			while (!num)
			{
				num = ReseedFromServers ();
				if (num) break; // success or no reseed servers specified

				if (i2p::util::GetMonotonicSeconds () < start + RESEED_GIVEUP_TIMEOUT)
				{
					LogPrint (eLogWarning, "Reseed: Failed to reseed from servers. Waiting for ",
						RESEED_WAITING_INTERVAL, " seconds");
					std::this_thread::sleep_for (std::chrono::seconds (RESEED_WAITING_INTERVAL));
				}
				else
					break; // give up
			}
			if (num <= 0) LogPrint (eLogWarning, "Reseed: Failed to reseed from servers. Give up");
		}
	}

	/**
	 * @brief bootstrap from random server, retry 10 times
	 * @return number of entries added to netDb
	 */
	int Reseeder::ReseedFromServers ()
	{
		bool ipv6;
		i2p::config::GetOption ("ipv6", ipv6);
		bool ipv4;
		i2p::config::GetOption ("ipv4", ipv4);
		bool yggdrasil;
		i2p::config::GetOption ("meshnets.yggdrasil", yggdrasil);

		std::vector<std::string> httpsReseedHostList;
		if (ipv4 || ipv6)
		{
			std::string reseedURLs;
			i2p::config::GetOption ("reseed.urls", reseedURLs);
			if (!reseedURLs.empty ())
				boost::split (httpsReseedHostList, reseedURLs, boost::is_any_of (","), boost::token_compress_on);
		}

		std::vector<std::string> yggReseedHostList;
		if (yggdrasil && !i2p::util::net::GetYggdrasilAddress ().is_unspecified ())
		{
			LogPrint (eLogInfo, "Reseed: Yggdrasil is supported");
			std::string yggReseedURLs;
			i2p::config::GetOption ("reseed.yggurls", yggReseedURLs);
			if (!yggReseedURLs.empty ())
				boost::split (yggReseedHostList, yggReseedURLs, boost::is_any_of (","), boost::token_compress_on);
		}

		if (httpsReseedHostList.empty () && yggReseedHostList.empty ())
		{
			LogPrint (eLogWarning, "Reseed: No reseed servers specified");
			return -1;
		}

		int numReseeds = httpsReseedHostList.size () + yggReseedHostList.size ();
		int reseedAttempts = std::min (numReseeds, MAX_NUM_RESEED_ATTEMPTS);
		if (reseedAttempts)
		{
			std::mt19937 rng (i2p::util::GetRngSeed ());
			for (int i = 0; i < reseedAttempts; i++)
			{
				auto ind = rng () % numReseeds;
				bool isHttps = ind < httpsReseedHostList.size ();
				std::string reseedUrl
					= isHttps ? httpsReseedHostList[ind] : yggReseedHostList[ind - httpsReseedHostList.size ()];
				reseedUrl += "i2pseeds.su3";
				auto num = ReseedFromSU3Url (reseedUrl, isHttps);
				if (num > 0) return num; // success

				if (isHttps)
					httpsReseedHostList.erase (httpsReseedHostList.begin () + ind);
				else
					yggReseedHostList.erase (yggReseedHostList.begin () + (ind - httpsReseedHostList.size ()));
				numReseeds--;
				if (!numReseeds) break;
			}
		}
		LogPrint (
			eLogWarning, "Reseed: Failed to reseed from ", numReseeds, " servers after ", reseedAttempts, " attempts");
		return 0;
	}

	/**
	 * @brief bootstrap from HTTPS URL with SU3 file
	 * @param url
	 * @return number of entries added to netDb
	 */
	int Reseeder::ReseedFromSU3Url (const std::string & url, bool isHttps)
	{
		LogPrint (eLogInfo, "Reseed: Downloading SU3 from ", url);
		std::string su3 = isHttps ? HttpsRequest (url) : YggdrasilRequest (url);
		if (su3.length () > 0)
		{
			std::stringstream s (su3);
			return ProcessSU3Stream (s);
		}
		else
		{
			LogPrint (eLogWarning, "Reseed: SU3 download failed");
			return 0;
		}
	}

	int Reseeder::ProcessSU3File (const char * filename)
	{
		std::ifstream s (filename, std::ifstream::binary);
		if (s.is_open ())
			return ProcessSU3Stream (s);
		else
		{
			LogPrint (eLogCritical, "Reseed: Can't open file ", filename);
			return 0;
		}
	}

	int Reseeder::ProcessZIPFile (const char * filename)
	{
		std::ifstream s (filename, std::ifstream::binary);
		if (s.is_open ())
		{
			s.seekg (0, std::ios::end);
			auto len = s.tellg ();
			s.seekg (0, std::ios::beg);
			if (len < 0) return 0;
			return ProcessZIPStream (s, static_cast<uint64_t> (len));
		}
		else
		{
			LogPrint (eLogCritical, "Reseed: Can't open file ", filename);
			return 0;
		}
	}

	const char SU3_MAGIC_NUMBER[] = "I2Psu3";

	namespace
	{
		size_t GetSU3SignatureLength (SigningKeyType type)
		{
			switch (type)
			{
			case SIGNING_KEY_TYPE_DSA_SHA1: return 40;
			case SIGNING_KEY_TYPE_ECDSA_SHA256_P256: return 64;
			case SIGNING_KEY_TYPE_ECDSA_SHA384_P384: return 96;
			case SIGNING_KEY_TYPE_ECDSA_SHA512_P521: return 132;
			case SIGNING_KEY_TYPE_RSA_SHA256_2048: return 256;
			case SIGNING_KEY_TYPE_RSA_SHA384_3072: return 384;
			case SIGNING_KEY_TYPE_RSA_SHA512_4096: return 512;
			case SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519:
			case SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519ph:
			case SIGNING_KEY_TYPE_REDDSA_SHA512_ED25519: return 64;
			case SIGNING_KEY_TYPE_GOSTR3410_CRYPTO_PRO_A_GOSTR3411_256: return 64;
			case SIGNING_KEY_TYPE_GOSTR3410_TC26_A_512_GOSTR3411_512: return 128;
			default: return 0;
			}
		}
	} // namespace

	int Reseeder::ProcessSU3Stream (std::istream & s)
	{
		auto readExact = [&s] (void * buffer, size_t size) -> bool
		{
			s.read (static_cast<char *> (buffer), static_cast<std::streamsize> (size));
			return s.good () && static_cast<size_t> (s.gcount ()) == size;
		};
		auto skip = [&s] (std::streamoff size) -> bool
		{
			s.seekg (size, std::ios::cur);
			return s.good ();
		};

		// SU3File.java: six-byte magic, one unused byte, then the file version.
		constexpr size_t SU3_MAGIC_LENGTH = 6;
		char magicNumber[SU3_MAGIC_LENGTH];
		if (!readExact (magicNumber, sizeof (magicNumber))
			|| memcmp (magicNumber, SU3_MAGIC_NUMBER, sizeof (magicNumber)) != 0)
		{
			LogPrint (eLogError, "Reseed: Unexpected or truncated SU3 header");
			return 0;
		}
		if (!skip (1)) return 0; // unused
		uint8_t fileVersion = 0;
		if (!readExact (&fileVersion, sizeof (fileVersion)) || fileVersion != 0)
		{
			LogPrint (eLogError, "Reseed: Unsupported SU3 file version ", (int)fileVersion);
			return 0;
		}
		uint16_t signatureTypeRaw = 0, signatureLengthRaw = 0;
		if (!readExact (&signatureTypeRaw, sizeof (signatureTypeRaw))
			|| !readExact (&signatureLengthRaw, sizeof (signatureLengthRaw)))
			return 0;
		SigningKeyType signatureType = static_cast<SigningKeyType> (be16toh (signatureTypeRaw));
		uint16_t signatureLength = be16toh (signatureLengthRaw);
		const size_t expectedSignatureLength = GetSU3SignatureLength (signatureType);
		if (!expectedSignatureLength || signatureLength != expectedSignatureLength)
		{
			LogPrint (eLogError, "Reseed: Invalid SU3 signature type/length ", (int)signatureType, "/",
				signatureLength);
			return 0;
		}
		if (!skip (1)) return 0; // unused
		uint8_t versionLength = 0;
		if (!readExact (&versionLength, sizeof (versionLength))) return 0;
		if (versionLength < 16)
		{
			LogPrint (eLogError, "Reseed: SU3 version field too short: ", (int)versionLength);
			return 0;
		}
		if (!skip (1)) return 0; // unused
		uint8_t signerIDLength = 0;
		if (!readExact (&signerIDLength, sizeof (signerIDLength))) return 0;
		if (!signerIDLength)
		{
			LogPrint (eLogError, "Reseed: SU3 signer ID is empty");
			return 0;
		}
		uint64_t contentLengthRaw = 0;
		if (!readExact (&contentLengthRaw, sizeof (contentLengthRaw))) return 0;
		uint64_t contentLength = be64toh (contentLengthRaw);
		if (!contentLength)
		{
			LogPrint (eLogError, "Reseed: SU3 content is empty");
			return 0;
		}
		if (contentLength > MAX_RESEED_CONTENT_LENGTH)
		{
			LogPrint (eLogError, "Reseed: SU3 content length too large: ", contentLength);
			return 0;
		}
		if (!skip (1)) return 0; // unused
		uint8_t fileType = 0;
		if (!readExact (&fileType, sizeof (fileType))) return 0;
		if (fileType != 0x00) // zip file
		{
			LogPrint (eLogError, "Reseed: Can't handle file type ", (int)fileType);
			return 0;
		}
		if (!skip (1)) return 0; // unused
		uint8_t contentType = 0;
		if (!readExact (&contentType, sizeof (contentType))) return 0;
		if (contentType != 0x03) // reseed data
		{
			LogPrint (eLogError, "Reseed: Unexpected content type ", (int)contentType);
			return 0;
		}
		if (!skip (12)) return 0; // unused

		if (!skip (versionLength)) return 0; // skip version
		std::array<char, 256> signerID { };
		if (signerIDLength >= signerID.size ()) return 0;
		if (!readExact (signerID.data (), signerIDLength)) return 0;
		signerID[signerIDLength] = 0;

		bool verify;
		i2p::config::GetOption ("reseed.verify", verify);
		if (verify)
		{
			//try to verify signature
			auto it = m_SigningKeys.find (signerID.data ());
			if (it != m_SigningKeys.end ())
			{
				// TODO: implement all signature types
				if (signatureType == SIGNING_KEY_TYPE_RSA_SHA512_4096 && signatureLength >= SHA512_DIGEST_LENGTH)
				{
					auto pos = s.tellg ();
					if (pos < 0) return 0;
					const size_t headerLen = static_cast<size_t> (pos);
					if (contentLength > MAX_RESEED_CONTENT_LENGTH
						|| headerLen > MAX_RESEED_CONTENT_LENGTH - contentLength)
						return 0;
					const size_t tbsLen = headerLen + static_cast<size_t> (contentLength);
					std::vector<uint8_t> tbs (tbsLen);
					std::vector<uint8_t> signature (signatureLength);
					s.seekg (0, std::ios::beg);
					if (!readExact (tbs.data (), tbs.size ()) || !readExact (signature.data (), signature.size ()))
						return 0;
					if (s.peek () != std::char_traits<char>::eof ())
					{
						LogPrint (eLogError, "Reseed: SU3 has data after the signature");
						return 0;
					}

					// RSA-raw
					{
						// calculate digest
						std::array<uint8_t, SHA512_DIGEST_LENGTH> digest { };
						SHA512 (tbs.data (), tbsLen, digest.data ());
						// encrypt signature
						BN_CTX * bnctx = BN_CTX_new ();
						BIGNUM *s = BN_new (), *n = BN_new ();
						if (!bnctx || !s || !n)
						{
							if (bnctx) BN_CTX_free (bnctx);
							if (s) BN_free (s);
							if (n) BN_free (n);
							return 0;
						}
						BN_bin2bn (signature.data (), signatureLength, s);
						BN_bin2bn (it->second, 512, n); // RSA 4096 assumed
						BN_mod_exp (s, s, i2p::crypto::GetRSAE (), n, bnctx); // s = s^e mod n
						std::vector<uint8_t> enSigBuf (signatureLength);
						// digest is right aligned
						// we can't use RSA_verify due wrong padding in SU3
						if (!i2p::crypto::bn2buf (s, enSigBuf.data (), signatureLength))
							LogPrint (eLogWarning, "Reseed: SU3 signature does not fit the declared length");
						else if (memcmp (enSigBuf.data () + (signatureLength - SHA512_DIGEST_LENGTH), digest.data (),
									 SHA512_DIGEST_LENGTH))
							LogPrint (eLogWarning, "Reseed: SU3 signature verification failed");
						else
							verify = false; // verified
						BN_free (s);
						BN_free (n);
						BN_CTX_free (bnctx);
					}
					s.seekg (pos, std::ios::beg);
				}
				else
					LogPrint (eLogWarning, "Reseed: Signature type ", signatureType, " is not supported");
			}
			else
				LogPrint (eLogWarning, "Reseed: Certificate for ", signerID.data (), " not loaded");
		}

		if (verify) // not verified
		{
			LogPrint (eLogCritical, "Reseed: SU3 verification failed");
			return 0;
		}

		// handle content
		return ProcessZIPStream (s, contentLength);
	}

	const uint32_t ZIP_HEADER_SIGNATURE = 0x04034B50;
	const uint32_t ZIP_CENTRAL_DIRECTORY_HEADER_SIGNATURE = 0x02014B50;
	const uint16_t ZIP_BIT_FLAG_DATA_DESCRIPTOR = 0x0008;
	int Reseeder::ProcessZIPStream (std::istream & s, uint64_t contentLength)
	{
		int numFiles = 0;
		auto contentPosition = s.tellg ();
		if (contentPosition < 0) return 0;
		size_t contentPos = static_cast<size_t> (contentPosition);
		// not !eof: seekg clears eofbit, so a stream broken in the middle of a
		// header stays not-eof forever and the loop never ends
		while (s.good ())
		{
			uint32_t signature = 0;
			s.read ((char *)&signature, sizeof (signature));
			if (s.gcount () != static_cast<std::streamsize> (sizeof (signature))) break;
			signature = le32toh (signature);
			if (signature == ZIP_HEADER_SIGNATURE)
			{
				// next local file
				s.seekg (2, std::ios::cur); // version
				uint16_t bitFlag = 0;
				s.read ((char *)&bitFlag, 2);
				bitFlag = le16toh (bitFlag);
				uint16_t compressionMethod = 0;
				s.read ((char *)&compressionMethod, 2);
				compressionMethod = le16toh (compressionMethod);
				s.seekg (4, std::ios::cur); // skip fields we don't care about
				uint32_t compressedSize = 0, uncompressedSize = 0;
				uint32_t crc_32 = 0;
				s.read ((char *)&crc_32, 4);
				crc_32 = le32toh (crc_32);
				s.read ((char *)&compressedSize, 4);
				compressedSize = le32toh (compressedSize);
				s.read ((char *)&uncompressedSize, 4);
				uncompressedSize = le32toh (uncompressedSize);
				uint16_t fileNameLength, extraFieldLength;
				s.read ((char *)&fileNameLength, 2);
				fileNameLength = le16toh (fileNameLength);
				s.read ((char *)&extraFieldLength, 2);
				extraFieldLength = le16toh (extraFieldLength);
				if (!s.good ())
				{
					LogPrint (eLogError, "Reseed: Truncated zip local file header");
					return numFiles;
				}
				if (fileNameLength >= 255)
				{
					// too big
					LogPrint (eLogError, "Reseed: SU3 fileNameLength too large: ", fileNameLength);
					return numFiles;
				}
				char localFileName[255];
				s.read (localFileName, fileNameLength);
				localFileName[fileNameLength] = 0;
				s.seekg (extraFieldLength, std::ios::cur);
				// take care about data descriptor if presented
				if (bitFlag & ZIP_BIT_FLAG_DATA_DESCRIPTOR)
				{
					auto pos = s.tellg ();
					if (!FindZipDataDescriptor (s))
					{
						LogPrint (eLogError, "Reseed: SU3 archive data descriptor not found");
						return numFiles;
					}
					s.read ((char *)&crc_32, 4);
					crc_32 = le32toh (crc_32);
					s.read ((char *)&compressedSize, 4);
					compressedSize = le32toh (compressedSize);
					// The data-descriptor signature is consumed as part of the
					// compressed data. Reject values that would wrap uint32_t
					// before the size cap below.
					if (compressedSize > MAX_RESEED_FILE_SIZE - 4)
					{
						LogPrint (eLogError, "Reseed: SU3 compressed size too large: ", compressedSize);
						return numFiles;
					}
					compressedSize += 4;
					s.read ((char *)&uncompressedSize, 4);
					uncompressedSize = le32toh (uncompressedSize);

					// now we know compressed and uncompressed size
					s.seekg (pos, std::ios::beg); // back to compressed data
				}

				LogPrint (eLogDebug, "Reseed: Processing file ", localFileName, " ", compressedSize, " bytes");
				if (!compressedSize)
				{
					LogPrint (eLogWarning, "Reseed: Unexpected size 0. Skipped");
					continue;
				}
				if (compressedSize > MAX_RESEED_FILE_SIZE || uncompressedSize > MAX_RESEED_FILE_SIZE)
				{
					LogPrint (eLogError, "Reseed: SU3 file size too large, compressed=", compressedSize,
						" uncompressed=", uncompressedSize);
					return numFiles;
				}

				// Both sizes were validated against MAX_RESEED_FILE_SIZE above.
				// The buffers are owned by the vectors so every early return
				// (truncated stream, zlib error, CRC failure) releases them.
				std::vector<uint8_t> compressed (compressedSize);
				s.read ((char *)compressed.data (), compressedSize);
				if (!s.good ())
				{
					LogPrint (eLogError, "Reseed: Truncated zip file data");
					return numFiles;
				}
				if (compressionMethod) // we assume Deflate
				{
					// Allocate before touching zlib state so an allocation
					// failure cannot leave an initialised stream behind.
					std::vector<uint8_t> uncompressed (uncompressedSize);
					z_stream inflator;
					memset (&inflator, 0, sizeof (inflator));
					inflateInit2 (&inflator, -MAX_WBITS); // no zlib header
					inflator.next_in = compressed.data ();
					inflator.avail_in = compressedSize;
					inflator.next_out = uncompressed.data ();
					inflator.avail_out = uncompressedSize;
					int err;
					// Require the complete deflate stream. Z_OK only means that
					// more input or output is needed, so accepting it could let
					// a truncated file reach CRC32/AddRouterInfo.
					err = inflate (&inflator, Z_SYNC_FLUSH);
					const size_t produced = uncompressedSize - inflator.avail_out;
					// Release the zlib state before CRC/netDb work so a later
					// exception cannot leak it.
					inflateEnd (&inflator);
					if (err == Z_STREAM_END)
					{
						uncompressed.resize (produced);
						if (crc32 (0, uncompressed.data (), uncompressed.size ()) == crc_32)
						{
							if (i2p::data::netdb.AddRouterInfo (
									uncompressed.data (), static_cast<int> (uncompressed.size ())))
								numFiles++;
						}
						else
							LogPrint (eLogError, "Reseed: CRC32 verification failed");
					}
					else
						LogPrint (eLogError, "Reseed: SU3 decompression error ", err);
				}
				else // no compression
				{
					if (i2p::data::netdb.AddRouterInfo (
							compressed.data (), static_cast<int> (compressed.size ())))
						numFiles++;
				}
				if (bitFlag & ZIP_BIT_FLAG_DATA_DESCRIPTOR)
					s.seekg (12, std::ios::cur); // skip data descriptor section if presented (12 = 16 - 4)
			}
			else
			{
				if (signature != ZIP_CENTRAL_DIRECTORY_HEADER_SIGNATURE)
					LogPrint (eLogWarning, "Reseed: Missing zip central directory header");
				break; // no more files
			}
			size_t end = s.tellg ();
			if (end - contentPos >= contentLength) break; // we are beyond contentLength
		}
		if (numFiles) // check if routers are not outdated
		{
			auto ts = i2p::util::GetMillisecondsSinceEpoch ();
			int numOutdated = 0;
			i2p::data::netdb.VisitRouterInfos (
				[&numOutdated, ts] (std::shared_ptr<const RouterInfo> r)
				{
					if (r
						&& ts > r->GetTimestamp () + 10 * i2p::data::NETDB_MAX_EXPIRATION_TIMEOUT * 1000LL) // 270 hours
					{
						LogPrint (eLogError, "Reseed: Router ", r->GetIdentHash ().ToBase64 (), " is outdated by ",
							(ts - r->GetTimestamp ()) / 1000LL / 3600LL, " hours");
						numOutdated++;
					}
				});
			if (numOutdated > numFiles / 2) // more than half
			{
				LogPrint (eLogError,
					"Reseed: Mammoth's shit\n"
					"	   *_____*\n"
					"	  *_*****_*\n"
					"	 *_(O)_(O)_*\n"
					"	**____V____**\n"
					"	**_________**\n"
					"	**_________**\n"
					"	 *_________*\n"
					"	  ***___***");
				i2p::data::netdb.ClearRouterInfos ();
				numFiles = 0;
			}
		}
		return numFiles;
	}

	const uint8_t ZIP_DATA_DESCRIPTOR_SIGNATURE[] = { 0x50, 0x4B, 0x07, 0x08 };
	bool Reseeder::FindZipDataDescriptor (std::istream & s)
	{
		size_t nextInd = 0;
		while (s.good ()) // a broken stream never reaches eof, see ProcessZIPStream
		{
			uint8_t nextByte;
			s.read ((char *)&nextByte, 1);
			if (nextByte == ZIP_DATA_DESCRIPTOR_SIGNATURE[nextInd])
			{
				nextInd++;
				if (nextInd >= sizeof (ZIP_DATA_DESCRIPTOR_SIGNATURE)) return true;
			}
			else
				nextInd = 0;
		}
		return false;
	}

	void Reseeder::LoadCertificate (const std::string & filename)
	{
		using SslCtxPtr = std::unique_ptr<SSL_CTX, decltype (&SSL_CTX_free)>;
		using SslPtr = std::unique_ptr<SSL, decltype (&SSL_free)>;
		using PkeyPtr = std::unique_ptr<EVP_PKEY, decltype (&EVP_PKEY_free)>;
		using BnPtr = std::unique_ptr<BIGNUM, decltype (&BN_free)>;

		SslCtxPtr ctx (SSL_CTX_new (TLS_method ()), SSL_CTX_free);
		if (!ctx)
		{
			LogPrint (eLogCritical, "Reseed: Can't create SSL context for ", filename);
			return;
		}
		if (!SSL_CTX_use_certificate_file (ctx.get (), filename.c_str (), SSL_FILETYPE_PEM))
		{
			LogPrint (eLogCritical, "Reseed: Can't open certificate file ", filename);
			return;
		}

		SslPtr ssl (SSL_new (ctx.get ()), SSL_free);
		X509 * cert = ssl ? SSL_get_certificate (ssl.get ()) : nullptr; // owned by SSL
		if (!cert) return;

		// extract issuer name
		char name[100];
		if (!X509_NAME_oneline (X509_get_issuer_name (cert), name, sizeof (name))) name[0] = 0;
		char * cn = strstr (name, "CN=");
		if (cn)
		{
			cn += 3;
			char * terminator = strchr (cn, '/');
			if (terminator) terminator[0] = 0;
		}

		// extract RSA key (we need n only, e = 65537)
		PkeyPtr pubKey (X509_get_pubkey (cert), EVP_PKEY_free);
#ifndef LIBRESSL_VERSION_NUMBER // OpenSSL 3.0.2 is the minimum
		BnPtr n1 (nullptr, BN_free);
		BIGNUM * rawN = nullptr;
		if (pubKey) (void)EVP_PKEY_get_bn_param (pubKey.get (), OSSL_PKEY_PARAM_RSA_N, &rawN);
		n1.reset (rawN);
		const BIGNUM * n = n1.get ();
#else
		const RSA * key = pubKey ? EVP_PKEY_get0_RSA (pubKey.get ()) : nullptr;
		const BIGNUM *e = nullptr, *d = nullptr, *n = nullptr;
		if (key) RSA_get0_key (key, &n, &e, &d);
#endif
		if (n)
		{
			PublicKey value;
			if (i2p::crypto::bn2buf (n, value, sizeof (value)))
			{
				if (cn)
					m_SigningKeys[cn] = value;
				else
					LogPrint (eLogError, "Reseed: Can't find CN field in ", filename);
			}
			else
				LogPrint (eLogError, "Reseed: RSA key is too large in ", filename);
		}
		else
			LogPrint (eLogError, "Reseed: Can't extract RSA key from ", filename);
	}

	void Reseeder::LoadCertificates ()
	{
		std::string certDir = i2p::fs::GetCertsDir () + i2p::fs::dirSep + "reseed";

		std::vector<std::string> files;
		int numCertificates = 0;

		if (!i2p::fs::ReadDir (certDir, files))
		{
			LogPrint (eLogWarning, "Reseed: Can't load reseed certificates from ", certDir);
			return;
		}

		for (const std::string & file : files)
		{
			// file.size () - 4 underflows for a short name and compare()
			// throws std::out_of_range, terminating the router during startup.
			if (file.size () < 4 || file.compare (file.size () - 4, 4, ".crt") != 0)
			{
				LogPrint (eLogWarning, "Reseed: Ignoring file ", file);
				continue;
			}
			LoadCertificate (file);
			numCertificates++;
		}
		LogPrint (eLogInfo, "Reseed: ", numCertificates, " certificates loaded");
	}

	std::string Reseeder::HttpsRequest (const std::string & address, int redirects)
	{
		i2p::http::URL proxyUrl;
		std::string proxy;
		i2p::config::GetOption ("reseed.proxy", proxy);
		// check for proxy url
		if (proxy.size ())
		{
			// parse
			if (proxyUrl.parse (proxy))
			{
				if (proxyUrl.schema == "http" && !proxyUrl.port)
				{
					proxyUrl.port = 80;
				}
				else if (proxyUrl.schema == "socks" && !proxyUrl.port)
				{
					proxyUrl.port = 1080;
				}
				// check for valid proxy url schema
				if (proxyUrl.schema != "http" && proxyUrl.schema != "socks")
				{
					LogPrint (eLogCritical, "Reseed: Bad proxy url: ", proxy);
					return "";
				}
			}
			else
			{
				LogPrint (eLogCritical, "Reseed: Bad proxy url: ", proxy);
				return "";
			}
		}
		i2p::http::URL url;
		if (!url.parse (address))
		{
			LogPrint (eLogCritical, "Reseed: Failed to parse url: ", address);
			return "";
		}
		url.schema = "https";
		if (!url.port) url.port = 443;

		boost::asio::io_context service;
		boost::system::error_code ecode;

		boost::asio::ssl::context ctx (boost::asio::ssl::context::sslv23);
		ctx.set_verify_mode (boost::asio::ssl::context::verify_none);
		boost::asio::ssl::stream<boost::asio::ip::tcp::socket> s (service, ctx);

		if (proxyUrl.schema.size ())
		{
			// proxy connection
			auto it = boost::asio::ip::tcp::resolver (service).resolve (
				proxyUrl.host, std::to_string (proxyUrl.port), ecode);
			if (!ecode)
			{
				s.lowest_layer ().connect (*it.begin (), ecode);
				if (!ecode)
				{
					auto & sock = s.next_layer ();
					if (proxyUrl.schema == "http")
					{
						i2p::http::HTTPReq proxyReq;
						i2p::http::HTTPRes proxyRes;
						proxyReq.method = "CONNECT";
						proxyReq.version = "HTTP/1.1";
						proxyReq.uri = url.host + ":" + std::to_string (url.port);
						auto auth = i2p::http::CreateBasicAuthorizationString (proxyUrl.user, proxyUrl.pass);
						if (!auth.empty ()) proxyReq.AddHeader ("Proxy-Authorization", auth);

						boost::asio::streambuf writebuf, readbuf;
						std::ostream out (&writebuf);
						out << proxyReq.to_string ();

						boost::asio::write (sock, writebuf.data (), boost::asio::transfer_all (), ecode);
						if (ecode)
						{
							sock.close ();
							LogPrint (eLogError, "Reseed: HTTP CONNECT write error: ", ecode.message ());
							return "";
						}
						boost::asio::read_until (sock, readbuf, "\r\n\r\n", ecode);
						if (ecode)
						{
							sock.close ();
							LogPrint (eLogError, "Reseed: HTTP CONNECT read error: ", ecode.message ());
							return "";
						}
						if (proxyRes.parse (std::string { boost::asio::buffers_begin (readbuf.data ()),
								boost::asio::buffers_begin (readbuf.data ()) + readbuf.size () })
							<= 0)
						{
							sock.close ();
							LogPrint (eLogError, "Reseed: HTTP CONNECT malformed reply");
							return "";
						}
						if (proxyRes.code != 200)
						{
							sock.close ();
							LogPrint (eLogError, "Reseed: HTTP CONNECT got bad status: ", proxyRes.code);
							return "";
						}
					}
					else
					{
						// assume socks if not http, is checked before this for other types
						// TODO: support username/password auth etc
						bool success = false;
						i2p::transport::Socks5Handshake (sock, std::make_pair (url.host, url.port),
							[&success] (const boost::system::error_code & ec)
							{
								if (!ec)
									success = true;
								else
									LogPrint (eLogError, "Reseed: SOCKS handshake failed: ", ec.message ());
							});
						service.run (); // execute all async operations
						if (!success)
						{
							sock.close ();
							return "";
						}
					}
				}
			}
		}
		else
		{
			// direct connection
			auto endpoints
				= boost::asio::ip::tcp::resolver (service).resolve (url.host, std::to_string (url.port), ecode);
			if (!ecode)
			{
				bool connected = false;
				for (const auto & it : endpoints)
				{
					boost::asio::ip::tcp::endpoint ep = it;
					bool supported = false;
					if (!ep.address ().is_unspecified ())
					{
						if (ep.address ().is_v4 ())
							supported = i2p::context.SupportsV4 ();
						else if (ep.address ().is_v6 ())
							supported = i2p::util::net::IsYggdrasilAddress (ep.address ())
								? i2p::context.SupportsMesh ()
								: i2p::context.SupportsV6 ();
					}
					if (supported)
					{
						s.lowest_layer ().async_connect (
							ep, [&ecode] (const boost::system::error_code & ec) { ecode = ec; });
						service.run_for (std::chrono::seconds (RESEED_CONNECT_TIMEOUT));
						if (!service.stopped ())
						{
							// the handler never ran, so ecode still holds success from resolve:
							// without this the timeout reads as a connection and the request goes
							// out on a socket that was just closed
							ecode = boost::asio::error::timed_out;
							s.lowest_layer ().close ();
						}

						if (!ecode)
						{
							LogPrint (eLogDebug, "Reseed: Connected to ", ep.address ());
							connected = true;
							break;
						}
					}
				}
				if (!connected)
				{
					LogPrint (eLogError, "Reseed: Failed to connect to ", url.host);
					return "";
				}
			}
		}
		if (!ecode)
		{
			SSL_set_tlsext_host_name (s.native_handle (), url.host.c_str ());
			s.handshake (boost::asio::ssl::stream_base::client, ecode);
			if (!ecode)
			{
				LogPrint (eLogDebug, "Reseed: Connected to ", url.host, ":", url.port);
				return ReseedRequest (s, url.to_string (), redirects);
			}
			else
				LogPrint (eLogError, "Reseed: SSL handshake failed: ", ecode.message ());
		}
		else
			LogPrint (eLogError, "Reseed: Couldn't connect to ", url.host, ": ", ecode.message ());
		return "";
	}

	template <typename Stream> std::string Reseeder::ReseedRequest (Stream & s, const std::string & uri, int redirects)
	{
		bool follow;
		i2p::config::GetOption ("reseed.followredirect", follow);
		boost::system::error_code ecode;
		i2p::http::HTTPReq req;
		i2p::http::HTTPRes res;

		req.uri = uri;
		req.AddHeader ("User-Agent", "Wget/1.11.4");
		req.AddHeader ("Connection", "close");
		s.write_some (boost::asio::buffer (req.to_string ()));

		// read response
		std::stringstream rs;
		char recv_buf[1024];
		size_t l = 0;
		size_t received = 0;
		do
		{
			l = s.read_some (boost::asio::buffer (recv_buf, sizeof (recv_buf)), ecode);
			if (l)
			{
				if (l > MAX_RESEED_HTTP_RESPONSE_SIZE - received)
				{
					LogPrint (eLogError, "Reseed: Response from ", uri, " exceeds size limit");
					return "";
				}
				received += l;
				rs.write (recv_buf, l);
			}
		} while (!ecode && l);

		// process response
		std::string data = rs.str ();
		int len = res.parse (data);
		if (len <= 0)
		{
			LogPrint (eLogWarning, "Reseed: Incomplete/broken response from ", uri);
			return "";
		}

		if ((res.code == 301 || res.code == 302 || res.code == 307) && follow)
		{
			LogPrint (eLogDebug, "Reseed: Received redirect from ", uri);

			std::string location = res.get_header ("Location");
			if (location.length () == 0)
			{
				LogPrint (eLogWarning, "Reseed: Broken redirect from ", uri);
				return "";
			}
			if (redirects >= MAX_RESEED_REDIRECTS)
			{
				LogPrint (eLogWarning, "Reseed: Too many redirects from ", uri);
				return "";
			}
			bool isHttps = (location.length () > 8 && location.substr (0, 8) == "https://");
			return (isHttps ? HttpsRequest (location, redirects + 1) : YggdrasilRequest (location, redirects + 1));
		}

		if (res.code != 200)
		{
			LogPrint (eLogError, "Reseed: Failed to reseed from ", uri, ", http code ", res.code);
			return "";
		}

		data.erase (0, len); /* drop http headers from response */
		LogPrint (eLogDebug, "Reseed: Got ", data.length (), " bytes of data from ", uri);
		if (res.is_chunked ())
		{
			std::stringstream in (data), out;
			if (!i2p::http::MergeChunkedResponse (in, out))
			{
				LogPrint (eLogWarning, "Reseed: Failed to merge chunked response from ", uri);
				return "";
			}
			LogPrint (eLogDebug, "Reseed: Got ", data.length (), "(", out.tellg (), ") bytes of data from ", uri);
			data = out.str ();
		}
		return data;
	}

	std::string Reseeder::YggdrasilRequest (const std::string & address, int redirects)
	{
		i2p::http::URL url;
		if (!url.parse (address))
		{
			LogPrint (eLogError, "Reseed: Failed to parse url: ", address);
			return "";
		}
		url.schema = "http";
		if (!url.port) url.port = 80;

		boost::system::error_code ecode;
		boost::asio::io_context service;
		boost::asio::ip::tcp::socket s (service, boost::asio::ip::tcp::v6 ());

		auto endpoints = boost::asio::ip::tcp::resolver (service).resolve (url.host, std::to_string (url.port), ecode);
		if (!ecode)
		{
			bool connected = false;
			for (const auto & it : endpoints)
			{
				boost::asio::ip::tcp::endpoint ep = it;
				if (i2p::util::net::IsYggdrasilAddress (ep.address ()) && i2p::context.SupportsMesh ())
				{
					LogPrint (eLogDebug, "Reseed: Yggdrasil: Resolved to ", ep.address ());
					s.async_connect (ep, [&ecode] (const boost::system::error_code & ec) { ecode = ec; });
					service.run_for (std::chrono::seconds (2 * RESEED_CONNECT_TIMEOUT));
					if (!service.stopped ())
					{
						ecode = boost::asio::error::timed_out;
						s.close ();
					}

					if (!ecode)
					{
						connected = true;
						break;
					}
				}
			}
			if (!connected)
			{
				LogPrint (eLogError, "Reseed: Yggdrasil: Failed to connect to ", url.host);
				return "";
			}
		}

		if (!ecode)
		{
			LogPrint (eLogDebug, "Reseed: Yggdrasil: Connected to ", url.host, ":", url.port);
			return ReseedRequest (s, url.to_string (), redirects);
		}
		else
			LogPrint (eLogError, "Reseed: Yggdrasil: Couldn't connect to ", url.host, ": ", ecode.message ());

		return "";
	}
}
}

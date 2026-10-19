/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: Reseed.
 *
 * Initial RouterInfo bootstrap from signed SU3 reseed bundles or ZIP files fetched over HTTPS
 * or Yggdrasil. Bundles are verified against configured signing keys, redirects are bounded,
 * and connection, attempt and give-up timeouts cap the whole operation.
 *
 */

#ifndef RESEED_H
#define RESEED_H

#include <iostream>
#include <string>
#include <vector>
#include <map>
#include "Identity.h"
#include "Crypto.h"

namespace i2p
{
namespace data
{
	constexpr int MAX_NUM_RESEED_ATTEMPTS = 10;
	constexpr int MAX_RESEED_REDIRECTS = 5;
	constexpr int RESEED_CONNECT_TIMEOUT = 5; // in seconds
	constexpr int RESEED_WAITING_INTERVAL = 30; // in second
	constexpr int RESEED_GIVEUP_TIMEOUT = 180; // in seconds

	class Reseeder
	{
		typedef Tag<512> PublicKey;

	public:
		Reseeder ();
		~Reseeder ();
		void Bootstrap ();
		int ReseedFromServers ();
		int ProcessSU3File (const char * filename);
		int ProcessZIPFile (const char * filename);

		void LoadCertificates ();

	private:
		int ReseedFromSU3Url (const std::string & url, bool isHttps = true);
		void LoadCertificate (const std::string & filename);

		int ProcessSU3Stream (std::istream & s);
		int ProcessZIPStream (std::istream & s, uint64_t contentLength);

		bool FindZipDataDescriptor (std::istream & s);

		std::string HttpsRequest (const std::string & address, int redirects = 0);
		std::string YggdrasilRequest (const std::string & address, int redirects = 0);
		template <typename Stream> std::string ReseedRequest (Stream & s, const std::string & uri, int redirects = 0);

	private:
		std::map<std::string, PublicKey> m_SigningKeys;
	};
}
}

#endif

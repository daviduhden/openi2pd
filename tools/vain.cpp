#include <algorithm>
#include <atomic>
#include <charconv>
#include <cstdio>
#include <cstring>
#include <exception>
#include <filesystem>
#include <functional>
#include <getopt.h>
#include <iostream>
#include <mutex>
#include <regex>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "Crypto.h"
#include "Identity.h"
#include "I2PEndian.h"
#include "common/file.hpp"
#include "common/key.hpp"
// Included last: it defines single-letter function-like macros (S0/S1/Ch),
// which must not leak into the standard or libi2pd headers above.
#include "vanity.hpp"

namespace
{
	using namespace i2p::tools::vanity;

	constexpr size_t ADDRESS_BUFFER_LEN = ADDRESS_LEN + 1;
	constexpr unsigned int MAX_THREADS = 1024;
	static_assert (NONCE_OFFSET + sizeof (uint32_t) <= IDENTITY_LEN,
		"nonce must stay inside the hashed identity prefix");
	// Magic number inherited from the upstream tool; it only spreads the
	// starting nonce between workers, it does not affect the search space.
	constexpr uint64_t THROUGHPUT_PER_THREAD = 0x4F4B5A37ULL;

	struct Options
	{
		bool reg = false;
		int threads = -1;
		i2p::data::SigningKeyType signature =
			i2p::data::SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519;
		std::string outputpath;
		bool multiple = false;
	};

	struct SearchState
	{
		std::atomic<bool> found{false};
		std::atomic<bool> failed{false};
		std::atomic<uint32_t> nonce{0};
		std::atomic<uint64_t> hashes{0};
		std::mutex mutex;
		std::string address; // guarded by mutex
		size_t keyId = 0;     // guarded by mutex
	};

	/** Zeroing owner for a private key buffer. */
	class KeyBuffer
	{
		public:

			explicit KeyBuffer (size_t size) : m_Data (size) {}
			KeyBuffer (const KeyBuffer&) = delete;
			KeyBuffer& operator= (const KeyBuffer&) = delete;
			KeyBuffer (KeyBuffer&&) noexcept = default;
			KeyBuffer& operator= (KeyBuffer&&) = delete;

			~KeyBuffer ()
			{
				if (!m_Data.empty ())
					OPENSSL_cleanse (m_Data.data (), m_Data.size ());
			}

			uint8_t * data () { return m_Data.data (); }
			const uint8_t * data () const { return m_Data.data (); }

		private:

			std::vector<uint8_t> m_Data;
	};

	void CalculateW (const uint8_t block[64], uint32_t W[64])
	{
		/*
		implementation of the original algorithm
		*/
		for (int i = 0; i < 16; i++)
		{
			uint32_t word = 0;
			std::memcpy (&word, block + i * 4, sizeof (word));
#ifdef _WIN32
			W[i] = htobe32 (word);
#else // from big endian to little endian ( swap )
			W[i] = be32toh (word);
#endif
		}

		for (int i = 16; i < 64; i++)
			W[i] = s1 (W[i - 2]) + W[i - 7] + s0 (W[i - 15]) + W[i - 16];
	}

	void TransformBlock (uint32_t state[8], const uint32_t W[64])
	{
		/*
		implementation of the original algorithm
		*/
		uint32_t S[8];
		std::memcpy (S, state, 32);

		uint32_t t0, t1;
		RNDr(S, W, 0, 0x428a2f98); RNDr(S, W, 1, 0x71374491); RNDr(S, W, 2, 0xb5c0fbcf); RNDr(S, W, 3, 0xe9b5dba5);
		RNDr(S, W, 4, 0x3956c25b); RNDr(S, W, 5, 0x59f111f1); RNDr(S, W, 6, 0x923f82a4); RNDr(S, W, 7, 0xab1c5ed5);
		RNDr(S, W, 8, 0xd807aa98); RNDr(S, W, 9, 0x12835b01); RNDr(S, W, 10, 0x243185be); RNDr(S, W, 11, 0x550c7dc3);
		RNDr(S, W, 12, 0x72be5d74); RNDr(S, W, 13, 0x80deb1fe); RNDr(S, W, 14, 0x9bdc06a7); RNDr(S, W, 15, 0xc19bf174);
		RNDr(S, W, 16, 0xe49b69c1); RNDr(S, W, 17, 0xefbe4786); RNDr(S, W, 18, 0x0fc19dc6); RNDr(S, W, 19, 0x240ca1cc);
		RNDr(S, W, 20, 0x2de92c6f); RNDr(S, W, 21, 0x4a7484aa); RNDr(S, W, 22, 0x5cb0a9dc); RNDr(S, W, 23, 0x76f988da);
		RNDr(S, W, 24, 0x983e5152); RNDr(S, W, 25, 0xa831c66d); RNDr(S, W, 26, 0xb00327c8); RNDr(S, W, 27, 0xbf597fc7);
		RNDr(S, W, 28, 0xc6e00bf3); RNDr(S, W, 29, 0xd5a79147); RNDr(S, W, 30, 0x06ca6351); RNDr(S, W, 31, 0x14292967);
		RNDr(S, W, 32, 0x27b70a85); RNDr(S, W, 33, 0x2e1b2138); RNDr(S, W, 34, 0x4d2c6dfc); RNDr(S, W, 35, 0x53380d13);
		RNDr(S, W, 36, 0x650a7354); RNDr(S, W, 37, 0x766a0abb); RNDr(S, W, 38, 0x81c2c92e); RNDr(S, W, 39, 0x92722c85);
		RNDr(S, W, 40, 0xa2bfe8a1); RNDr(S, W, 41, 0xa81a664b); RNDr(S, W, 42, 0xc24b8b70); RNDr(S, W, 43, 0xc76c51a3);
		RNDr(S, W, 44, 0xd192e819); RNDr(S, W, 45, 0xd6990624); RNDr(S, W, 46, 0xf40e3585); RNDr(S, W, 47, 0x106aa070);
		RNDr(S, W, 48, 0x19a4c116); RNDr(S, W, 49, 0x1e376c08); RNDr(S, W, 50, 0x2748774c); RNDr(S, W, 51, 0x34b0bcb5);
		RNDr(S, W, 52, 0x391c0cb3); RNDr(S, W, 53, 0x4ed8aa4a); RNDr(S, W, 54, 0x5b9cca4f); RNDr(S, W, 55, 0x682e6ff3);
		RNDr(S, W, 56, 0x748f82ee); RNDr(S, W, 57, 0x78a5636f); RNDr(S, W, 58, 0x84c87814); RNDr(S, W, 59, 0x8cc70208);
		RNDr(S, W, 60, 0x90befffa); RNDr(S, W, 61, 0xa4506ceb); RNDr(S, W, 62, 0xbef9a3f7); RNDr(S, W, 63, 0xc67178f2);

		for (int i = 0; i < 8; i++)	state[i] += S[i];
	}

	void HashNextBlock (uint32_t state[8], const uint8_t * block)
	{
		/*
		implementation of the original algorithm
		*/
		uint32_t W[64];
		CalculateW (block, W);
		TransformBlock (state, W);
	}

	/**
	 * Encode at most outLen base32 characters and NUL-terminate. The caller
	 * must provide outLen + 1 bytes of storage. Returns the number of
	 * characters written (excluding the terminator).
	 */
	size_t ByteStreamToBase32 (const uint8_t * inBuf, size_t len, char * outBuf, size_t outLen)
	{
		if (outLen == 0 || len == 0)
		{
			outBuf[0] = '\0';
			return 0;
		}

		size_t ret = 0, pos = 1;
		int bits = 8;
		// Unsigned accumulator: the bit reservoir intentionally wraps once it
		// grows past 32 bits; only the low (bits + 5) bits are ever read.
		uint32_t tmp = inBuf[0];
		while (ret < outLen && (bits > 0 || pos < len))
		{
			if (bits < 5)
			{
				if (pos < len)
				{
					tmp <<= 8;
					tmp |= inBuf[pos] & 0xFF;
					pos++;
					bits += 8;
				}
				else // last byte
				{
					tmp <<= (5 - bits);
					bits = 5;
				}
			}

			bits -= 5;
			const int ind = (tmp >> bits) & 0x1F;
			outBuf[ret] = (ind < 26) ? static_cast<char> (ind + 'a') :
				static_cast<char> ((ind - 26) + '2');
			ret++;
		}
		outBuf[ret] = '\0';
		return ret;
	}

	bool MatchesPrefix (std::string_view address, std::string_view prefix)
	{
		return address.size () >= prefix.size () &&
			address.compare (0, prefix.size (), prefix) == 0;
	}

	/** Plain patterns are base32 destination prefixes only. */
	bool IsBase32Prefix (std::string_view prefix)
	{
		if (prefix.empty () || prefix.size () > ADDRESS_LEN)
			return false;
		return std::all_of (prefix.begin (), prefix.end (), [] (unsigned char ch) {
			return (ch >= 'a' && ch <= 'z') || (ch >= '2' && ch <= '7');
		});
	}

	void ThreadFind (std::span<const uint8_t, IDENTITY_LEN> key, std::string_view prefix,
		const std::regex * pattern, size_t idThread, uint64_t throughput,
		SearchState& state)
	{
		/*
		Thanks to orignal ^-^
		For idea and example ^-^
		Orignal is sensei of crypto ;)
		*/
		std::cout << "Thread " << idThread << " binded" << std::endl;

		std::array<uint8_t, IDENTITY_LEN> b;
		std::copy (key.begin (), key.end (), b.begin ());

		const size_t matchLen = std::min (prefix.size (), ADDRESS_LEN);
		// precalculate first 5 blocks (320 bytes)
		uint32_t hashState[8] = { 0x6A09E667, 0xBB67AE85, 0x3C6EF372, 0xA54FF53A, 0x510E527F, 0x9B05688C, 0x1F83D9AB, 0x5BE0CD19 };
		HashNextBlock (hashState, b.data ());
		HashNextBlock (hashState, b.data () + 64);
		HashNextBlock (hashState, b.data () + 128);
		HashNextBlock (hashState, b.data () + 192);
		HashNextBlock (hashState, b.data () + 256);

		// pre-calculate last W
		uint32_t lastW[64];
		CalculateW (LAST_BLOCK.data (), lastW);

		// The nonce lives inside the key buffer. It is copied through a local
		// variable so the buffer is never accessed through a misaligned
		// uint32_t pointer.
		uint32_t nonce = 0;
		std::memcpy (&nonce, b.data () + NONCE_OFFSET, sizeof (nonce));
		nonce += static_cast<uint32_t> (static_cast<uint64_t> (idThread) * throughput);

		std::array<char, ADDRESS_BUFFER_LEN> addr{};
		uint32_t hash[8];
		uint32_t hashState1[8];

		try
		{
			while (throughput-- && !state.found.load ())
			{
				std::memcpy (b.data () + NONCE_OFFSET, &nonce, sizeof (nonce));
				std::memcpy (hashState1, hashState, sizeof (hashState1));
				// calculate hash of block with nonce
				HashNextBlock (hashState1, b.data () + NONCE_OFFSET);
				// apply last block
				TransformBlock (hashState1, lastW);
				// get final hash
				for (int j = 8; j--;)
					hash[j] = htobe32 (hashState1[j]);
				ByteStreamToBase32 (reinterpret_cast<const uint8_t *> (hash), sizeof (hash),
					addr.data (), matchLen);

				const bool matched = pattern ?
					std::regex_match (addr.data (), addr.data () + matchLen, *pattern) :
					MatchesPrefix (std::string_view (addr.data (), matchLen), prefix);

				if (matched)
				{
					ByteStreamToBase32 (reinterpret_cast<const uint8_t *> (hash), sizeof (hash),
						addr.data (), ADDRESS_LEN);
					if (!state.found.exchange (true))
					{
						std::cout << "Address found " << addr.data () << " in " << idThread << std::endl;
						state.nonce.store (nonce);
						std::lock_guard<std::mutex> lock (state.mutex);
						state.address.assign (addr.data (), ADDRESS_LEN);
						state.keyId = idThread;
					}
					return;
				}

				++nonce;
				state.hashes.fetch_add (1, std::memory_order_relaxed);
			}
		}
		catch (const std::exception& error)
		{
			// An exception escaping a thread function calls std::terminate;
			// turn it into a failed search that the main thread can report.
			std::cerr << "vain: worker " << idThread << " failed: " << error.what () << std::endl;
			state.failed.store (true);
			state.found.store (true); // stop the other workers
		}
	}

	enum class Outcome
	{
		Found,
		Retry,
		Failed
	};

	Outcome RunSearch (Options& options, std::string_view prefix, const std::regex * pattern,
		const std::string& defaultOutputPath, unsigned int& attempts,
		unsigned int& foundKeys, SearchState& state)
	{
		state.found.store (false);
		state.failed.store (false);
		state.nonce.store (0);
		{
			std::lock_guard<std::mutex> lock (state.mutex);
			state.address.clear ();
			state.keyId = 0;
		}

		std::vector<KeyBuffer> keyBuffers;
		keyBuffers.reserve (static_cast<size_t> (options.threads));
		size_t keyFileLen = 0;
		for (int i = 0; i < options.threads; ++i)
		{
			auto keys = i2p::data::PrivateKeys::CreateRandomKeys (options.signature);
			// The nonce search reads and mutates the public IdentityEx prefix
			// (391 bytes for Ed25519). The complete private-key file is larger
			// and is still written in full, apart from the four nonce bytes.
			const auto publicKeys = keys.GetPublic ();
			const size_t publicLen = publicKeys ? publicKeys->GetFullLen () : 0;
			const size_t len = keys.GetFullLen ();
			if (publicLen != IDENTITY_LEN || len < IDENTITY_LEN)
			{
				std::cerr << "vain: unsupported key layout; only ED25519-SHA512 is supported" << std::endl;
				return Outcome::Failed;
			}
			if (i == 0)
				keyFileLen = len;
			else if (len != keyFileLen)
			{
				std::cerr << "vain: inconsistent generated key lengths" << std::endl;
				return Outcome::Failed;
			}

			keyBuffers.emplace_back (len);
			if (keys.ToBuffer (keyBuffers.back ().data (), len) != len)
			{
				std::cerr << "vain: failed to serialize a generated key" << std::endl;
				return Outcome::Failed;
			}
			// The search uses a precomputed final SHA-256 block, which embeds
			// the last seven identity bytes (certificate and key-type fields).
			// Verify them so a different key/crypto layout cannot silently
			// produce addresses for a hash other than the written key.
			if (!std::equal (LAST_BLOCK.begin (), LAST_BLOCK.begin () + 7,
				keyBuffers.back ().data () + IDENTITY_LEN - 7))
			{
				std::cerr << "vain: key type fields do not match the fixed final hash block" << std::endl;
				return Outcome::Failed;
			}
		}

		std::cout << "Start vanity generator in " << options.threads << " threads" << std::endl;

		// jthread joins on destruction, so an exception during thread startup
		// cannot leave joinable threads behind.
		std::vector<std::jthread> workers;
		workers.reserve (static_cast<size_t> (options.threads));
		uint64_t throughput = THROUGHPUT_PER_THREAD;
		for (size_t i = 0; i < keyBuffers.size (); ++i)
		{
			std::cout << "Use " << i << " key" << std::endl;
			workers.emplace_back (ThreadFind,
				std::span<const uint8_t, IDENTITY_LEN> (keyBuffers[i].data (), IDENTITY_LEN),
				prefix, pattern, i, throughput, std::ref (state));
			throughput += 1000;
		}

		for (auto& worker : workers)
			worker.join ();

		if (state.failed.load ())
			return Outcome::Failed;

		if (!state.found.load ())
		{
			std::cout << "(Generate a new keypair) Attempts #" << ++attempts << std::endl;
			return Outcome::Retry;
		}

		size_t keyId = 0;
		std::string address;
		{
			std::lock_guard<std::mutex> lock (state.mutex);
			keyId = state.keyId;
			address = state.address;
		}
		if (keyId >= keyBuffers.size ())
		{
			std::cerr << "vain: inconsistent worker key selection" << std::endl;
			return Outcome::Failed;
		}

		auto& keyBuffer = keyBuffers[keyId];
		const uint32_t foundNonce = state.nonce.load ();
		std::memcpy (keyBuffer.data () + NONCE_OFFSET, &foundNonce, sizeof (foundNonce));
		std::cout << "Hashes: " << state.hashes.load () << std::endl;

		if (options.outputpath.empty ())
			options.outputpath.assign (address + ".dat");
		else
			options.outputpath = options.outputpath + std::to_string (foundKeys) + std::string (".dat");

		std::error_code ec;
		// symlink_status() also reports dangling symlinks, which a plain
		// exists() would treat as free and then follow on write. A missing
		// output path is the normal case: clear ENOENT instead of treating it
		// as an inspection failure.
		for (;;)
		{
			const auto status = std::filesystem::symlink_status (options.outputpath, ec);
			if (ec)
			{
				if (ec != std::errc::no_such_file_or_directory)
				{
					std::cerr << "vain: cannot inspect output path: " << ec.message () << std::endl;
					return Outcome::Failed;
				}
				ec.clear ();
				break;
			}
			if (!std::filesystem::exists (status))
				break;
			options.outputpath.assign (defaultOutputPath);
			options.outputpath = options.outputpath + std::to_string (foundKeys) + std::string (".dat");
			foundKeys++;
		}

		std::cout << "outpath for a now: " << options.outputpath << std::endl;

		if (!i2p::tools::WriteFile (options.outputpath, keyBuffer.data (), keyFileLen))
		{
			std::cout << "Can't create file " << options.outputpath << std::endl;
			return Outcome::Failed;
		}

		return Outcome::Found;
	}

	void usage ()
	{
		constexpr const char * help = "vain pattern [options]\n"
			"-h --help help menu\n"
			"-r --reg  regexp instead just text pattern\n"
			"--threads -t (default count of system)\n"
			"-s --signature signature type (only ED25519-SHA512)\n"
			"-o --output output file prefix (default <address>.dat)\n"
			"--multiplymode -m - multiple addresses search\n";
		std::puts (help);
	}

	enum class ParseResult
	{
		Run,
		Help,
		Error
	};

	ParseResult ParseArguments (int argc, char ** argv, Options& options)
	{
		int optionIndex = 0;
		static const option longOptions[] = {
			{"help", no_argument, nullptr, 'h'},
			{"reg", no_argument, nullptr, 'r'},
			{"threads", required_argument, nullptr, 't'},
			{"signature", required_argument, nullptr, 's'},
			{"output", required_argument, nullptr, 'o'},
			{"multiplymode", no_argument, nullptr, 'm'},
			{nullptr, 0, nullptr, 0}
		};

		int c;
		while ((c = getopt_long (argc, argv, "hrt:s:o:m", longOptions, &optionIndex)) != -1)
		{
			switch (c)
			{
				case 'm':
					options.multiple = true;
					break;
				case 'h':
					usage ();
					return ParseResult::Help;
				case 'r':
					options.reg = true;
					break;
				case 't':
				{
					const std::string_view input (optarg);
					int value = 0;
					const auto result = std::from_chars (input.data (), input.data () + input.size (), value);
					if (result.ec != std::errc () || result.ptr != input.data () + input.size () ||
						value < 1 || value > static_cast<int> (MAX_THREADS))
					{
						std::cerr << "Invalid thread count" << std::endl;
						return ParseResult::Error;
					}
					options.threads = value;
					break;
				}
				case 's':
				{
					const auto signature = NameToSigType (optarg);
					if (!signature)
					{
						std::cerr << "Invalid signature type" << std::endl;
						return ParseResult::Error;
					}
					options.signature = static_cast<i2p::data::SigningKeyType> (*signature);
					break;
				}
				case 'o':
					options.outputpath = optarg;
					break;
				case '?':
					std::cerr << "Undefined argument" << std::endl;
					[[fallthrough]];
				default:
					std::cerr << argv[0] << " --help" << std::endl;
					return ParseResult::Error;
			}
		}
		return ParseResult::Run;
	}
}

int main (int argc, char * argv[])
{
	if (argc < 2)
	{
		usage ();
		return 0;
	}

	Options options;
	const auto parseResult = ParseArguments (argc, argv, options);
	if (parseResult == ParseResult::Help) return 0;
	if (parseResult == ParseResult::Error) return 1;

	// getopt_long permutes arguments, so the pattern is whatever positional
	// argument is left; this preserves the historical "vain pattern [options]"
	// form and also accepts options before the pattern.
	if (optind >= argc)
	{
		usage ();
		return 1;
	}
	const std::string pattern (argv[optind]);

	if (!options.reg && !IsBase32Prefix (pattern))
	{
		std::cout << "Not correct prefix(just string)" << std::endl;
		return 1;
	}

	std::regex expression;
	const std::regex * expressionPtr = nullptr;
	if (options.reg)
	{
		try
		{
			expression = std::regex (pattern);
			expressionPtr = &expression;
		}
		catch (const std::regex_error& error)
		{
			std::cerr << "Invalid regular expression: " << error.what () << std::endl;
			return 1;
		}
	}

	if (options.signature != i2p::data::SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519)
	{
		std::cout << "For a while only ED25519-SHA512" << std::endl;
		return 1;
	}

	// if threads less than 0, then we get from system count of CPUs cores
	if (options.threads <= 0)
	{
		const unsigned int available = std::thread::hardware_concurrency ();
		options.threads = available ?
			static_cast<int> (std::min (available, MAX_THREADS)) : 1;
	}

	i2p::crypto::InitCrypto (false);
	struct CryptoGuard
	{
		~CryptoGuard () { i2p::crypto::TerminateCrypto (); }
	} cryptoGuard;

	const std::string defaultOutputPath = options.outputpath;
	unsigned int attempts = 0;
	unsigned int foundKeys = 0;
	SearchState state;

	int exitCode = 0;
	try
	{
		for (;;)
		{
			const auto outcome = RunSearch (options, pattern, expressionPtr,
				defaultOutputPath, attempts, foundKeys, state);
			if (outcome == Outcome::Failed)
			{
				exitCode = 1;
				break;
			}
			if (outcome == Outcome::Found && !options.multiple) break;

			// Reset per-search output state for retries and multiple-address
			// mode. The found nonce is only valid for the current key pair.
			options.outputpath.assign (defaultOutputPath);
		}
	}
	catch (const std::exception& error)
	{
		// Allocation, filesystem or thread-start failures are reported as a
		// normal tool error; worker threads are joined during unwinding.
		std::cerr << "vain: " << error.what () << std::endl;
		exitCode = 1;
	}

	return exitCode;
}

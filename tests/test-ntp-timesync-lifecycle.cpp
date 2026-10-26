/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: test ntp timesync lifecycle.
 *
 * Checks NTPTimeSync start/stop restartability with no configured servers,
 * repeated cycles and destruction while the worker runs, without contacting a
 * time server. It also exercises the SNTP reply validator and the three-reply
 * agreement rule with synthetic packets: no socket, DNS lookup or real NTP
 * server is used.
 *
 */

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include "Timestamp.h"

namespace i2p
{
namespace util
{
	struct NTPTimeSyncTestAccess
	{
		static void SetServers (NTPTimeSync & sync, std::vector<std::string> servers)
		{
			sync.m_NTPServersList = std::move (servers);
		}
		static void SetInterval (NTPTimeSync & sync, int hours) { sync.m_SyncInterval = hours; }
	};
}
}

namespace
{
	int failures = 0;

	void Check (bool condition, const std::string & what)
	{
		if (condition) return;
		std::cerr << "FAIL: " << what << std::endl;
		++failures;
	}

	void PutBigEndian32 (std::array<uint8_t, i2p::util::NTP_PACKET_SIZE> & packet, size_t offset, uint32_t value)
	{
		packet[offset] = static_cast<uint8_t> (value >> 24);
		packet[offset + 1] = static_cast<uint8_t> ((value >> 16) & 0xFF);
		packet[offset + 2] = static_cast<uint8_t> ((value >> 8) & 0xFF);
		packet[offset + 3] = static_cast<uint8_t> (value & 0xFF);
	}

	void PutBigEndian64 (std::array<uint8_t, i2p::util::NTP_PACKET_SIZE> & packet, size_t offset, uint64_t value)
	{
		PutBigEndian32 (packet, offset, static_cast<uint32_t> (value >> 32));
		PutBigEndian32 (packet, offset + 4, static_cast<uint32_t> (value & 0xFFFFFFFFULL));
	}

	struct ReplyFields
	{
		uint8_t leap = 0;
		uint8_t version = 4;
		uint8_t mode = 4;
		uint8_t stratum = 2;
		uint32_t rootDelaySeconds = 0;
		uint32_t rootDispersionSeconds = 0;
		uint32_t ntpSeconds = 0;
		uint64_t origin = 0;
	};

	std::array<uint8_t, i2p::util::NTP_PACKET_SIZE> MakeReply (const ReplyFields & fields)
	{
		std::array<uint8_t, i2p::util::NTP_PACKET_SIZE> packet { };
		packet[0] = static_cast<uint8_t> ((fields.leap << 6) | (fields.version << 3) | fields.mode);
		packet[1] = fields.stratum;
		PutBigEndian32 (packet, 4, fields.rootDelaySeconds << 16);
		PutBigEndian32 (packet, 8, fields.rootDispersionSeconds << 16);
		PutBigEndian64 (packet, 24, fields.origin);
		PutBigEndian64 (packet, 32, static_cast<uint64_t> (fields.ntpSeconds) << 32);
		return packet;
	}

	uint32_t UnixToNTP (uint64_t unixSeconds)
	{
		return static_cast<uint32_t> (unixSeconds + i2p::util::NTP_UNIX_EPOCH_DELTA);
	}

	void CheckReply (const ReplyFields & fields, uint64_t origin, i2p::util::NTPReplyError expected,
		const std::string & what)
	{
		const auto packet = MakeReply (fields);
		uint32_t seconds = 0;
		const auto result = i2p::util::ValidateNTPReply (packet, origin, seconds);
		Check (result == expected, what);
		if (expected == i2p::util::NTPReplyError::None)
			Check (seconds == fields.ntpSeconds - i2p::util::NTP_UNIX_EPOCH_DELTA, what + " (time extracted)");
	}
}

int main ()
{
	using namespace i2p::util;

	// No configured servers: Start() is a no-op and Stop() is harmless.
	{
		NTPTimeSync sync;
		NTPTimeSyncTestAccess::SetServers (sync, { });
		sync.Start ();
		sync.Start ();
		sync.Stop ();
		sync.Stop ();
	}

	// Repeated start/stop cycles must restart the io_context.
	NTPTimeSync sync;
	NTPTimeSyncTestAccess::SetServers (sync, { "127.0.0.1" });
	NTPTimeSyncTestAccess::SetInterval (sync, 1);

	for (int round = 0; round < 3; round++)
	{
		sync.Start ();
		sync.Start (); // already running: no-op
		const auto started = std::chrono::steady_clock::now ();
		sync.Stop ();
		sync.Stop (); // idempotent
		const auto elapsed = std::chrono::steady_clock::now () - started;
		if (elapsed > std::chrono::seconds (5))
		{
			std::cerr << "NTPTimeSync::Stop() blocked for "
					  << std::chrono::duration_cast<std::chrono::seconds> (elapsed).count () << " seconds" << std::endl;
			return 1;
		}
	}

	// Destruction while running must join the worker.
	{
		NTPTimeSync running;
		NTPTimeSyncTestAccess::SetServers (running, { "127.0.0.1" });
		NTPTimeSyncTestAccess::SetInterval (running, 1);
		running.Start ();
	}

	// --- request/response packet format -----------------------------------
	const uint64_t nowUnix = 1700000000ULL;
	const uint64_t transmit = MakeNTPTransmitTimestamp (nowUnix, 0x12345678U);
	{
		std::array<uint8_t, NTP_PACKET_SIZE> request { };
		BuildNTPRequest (request, transmit);
		Check ((request[0] & 0x07) == 3, "request mode is client");
		Check (((request[0] >> 3) & 0x07) == 3, "request version is 3");
		Check (((request[0] >> 6) & 0x03) == 0, "request leap indicator is zero");
		Check ((static_cast<uint64_t> (request[40]) << 56 | static_cast<uint64_t> (request[41]) << 48
				   | static_cast<uint64_t> (request[42]) << 40 | static_cast<uint64_t> (request[43]) << 32
				   | static_cast<uint64_t> (request[44]) << 24 | static_cast<uint64_t> (request[45]) << 16
				   | static_cast<uint64_t> (request[46]) << 8 | static_cast<uint64_t> (request[47]))
				== transmit,
			"request carries the transmit timestamp");

		ReplyFields good;
		good.ntpSeconds = UnixToNTP (nowUnix + 100);
		good.origin = transmit;
		const auto reply = MakeReply (good);
		uint32_t seconds = 0;
		Check (ValidateNTPReply (reply, transmit, seconds) == NTPReplyError::None, "matching reply is accepted");
		Check (seconds == nowUnix + 100, "server time is extracted");
	}

	// --- rejections -------------------------------------------------------
	{
		ReplyFields fields;
		fields.ntpSeconds = UnixToNTP (nowUnix);
		fields.origin = transmit;

		ReplyFields wrongMode = fields;
		wrongMode.mode = 3; // a client request is not a server reply
		CheckReply (wrongMode, transmit, NTPReplyError::Mode, "wrong mode is rejected");

		ReplyFields wrongVersion = fields;
		wrongVersion.version = 2;
		CheckReply (wrongVersion, transmit, NTPReplyError::Version, "version 2 is rejected");

		wrongVersion.version = 5;
		CheckReply (wrongVersion, transmit, NTPReplyError::Version, "version 5 is rejected");

		ReplyFields wrongLeap = fields;
		wrongLeap.leap = 3; // unsynchronized server
		CheckReply (wrongLeap, transmit, NTPReplyError::Leap, "leap indicator 3 is rejected");

		ReplyFields kissOfDeath = fields;
		kissOfDeath.stratum = 0;
		CheckReply (kissOfDeath, transmit, NTPReplyError::Stratum, "stratum 0 is rejected");

		ReplyFields unsynchronized = fields;
		unsynchronized.stratum = 16;
		CheckReply (unsynchronized, transmit, NTPReplyError::Stratum, "stratum 16 is rejected");

		ReplyFields slowPath = fields;
		slowPath.rootDelaySeconds = NTP_MAX_ROOT_DELAY_SECONDS + 1;
		CheckReply (slowPath, transmit, NTPReplyError::RootDelay, "excessive root delay is rejected");

		ReplyFields dispersed = fields;
		dispersed.rootDispersionSeconds = NTP_MAX_ROOT_DISPERSION_SECONDS + 1;
		CheckReply (dispersed, transmit, NTPReplyError::RootDispersion, "excessive root dispersion is rejected");

		// A reply for a different request is ignored.
		CheckReply (fields, transmit ^ 1ULL, NTPReplyError::Origin, "origin mismatch is rejected");

		ReplyFields zeroTransmit = fields;
		zeroTransmit.ntpSeconds = 0;
		CheckReply (zeroTransmit, transmit, NTPReplyError::TransmitTime, "zero transmit time is rejected");

		// A short datagram is never trusted.
		std::array<uint8_t, 8> truncated { };
		uint32_t seconds = 0;
		Check (ValidateNTPReply (truncated, transmit, seconds) == NTPReplyError::Size, "short reply is rejected");

		// Boundary values that must stay accepted.
		ReplyFields boundary = fields;
		boundary.version = 3;
		boundary.leap = 2;
		boundary.stratum = 15;
		boundary.rootDelaySeconds = NTP_MAX_ROOT_DELAY_SECONDS;
		boundary.rootDispersionSeconds = NTP_MAX_ROOT_DISPERSION_SECONDS;
		CheckReply (boundary, transmit, NTPReplyError::None, "boundary values are accepted");
	}

	// --- agreement before a large jump ------------------------------------
	{
		NTPOffsetAgreement agreement;
		// A small offset is applied from one validated reply.
		const auto small = agreement.Add (1, 1000);
		Check (small.has_value () && *small == 1, "small offset applied from one reply");
		const auto negative = agreement.Add (-1, 1000);
		Check (negative.has_value () && *negative == -1, "small negative offset applied");

		agreement.Reset ();
		Check (!agreement.Add (100, 2000).has_value (), "one large offset is not applied");
		Check (!agreement.Add (101, 2001).has_value (), "two large offsets are not applied");
		const auto agreed = agreement.Add (100, 2002);
		Check (agreed.has_value (), "three agreeing offsets are applied");
		if (agreed) Check (*agreed >= 100 && *agreed <= 101, "applied offset is the agreeing mean");

		agreement.Reset ();
		Check (!agreement.Add (100, 3000).has_value (), "first disagreeing offset");
		Check (!agreement.Add (105, 3001).has_value (), "second disagreeing offset");
		Check (!agreement.Add (100, 3002).has_value (), "split vote is not applied");

		// Stale samples must not corroborate a new jump.
		agreement.Reset ();
		Check (!agreement.Add (200, 4000).has_value (), "first stale sample");
		Check (!agreement.Add (200, 4000 + NTP_AGREEMENT_SAMPLE_LIFETIME + 1).has_value (),
			"expired sample does not count");
		Check (!agreement.Add (200, 4000 + 2 * NTP_AGREEMENT_SAMPLE_LIFETIME + 2).has_value (),
			"repeatedly expired samples never agree");
	}

	if (failures)
	{
		std::cerr << failures << " NTP checks failed" << std::endl;
		return 1;
	}
	std::cout << "NTP time sync checks passed" << std::endl;
	return 0;
}

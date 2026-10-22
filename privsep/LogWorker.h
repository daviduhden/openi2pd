/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: LogWorker.
 *
 * Declares the parent-side LogSink and the isolated log-worker entry point.
 * The sink serialises bounded records and supports reattachment after a
 * restart, while the worker owns log-file or syslog output and text
 * sanitisation.
 *
 */

#ifndef I2PD_PRIVSEP_LOG_WORKER_H__
#define I2PD_PRIVSEP_LOG_WORKER_H__

#include <cstdint>
#include <ctime>
#include <mutex>
#include <string>
#include <string_view>

#include "Channel.h"

namespace i2p::privsep
{
/**
	 * Parent-side log sink. It only serializes structured records to the log
	 * worker; the router process never opens or writes the log file itself.
	 *
	 * The sink object stays alive for the router lifetime so the logger's sink
	 * callback never points at a replaced object. The in-process restart
	 * monitor swaps the underlying channel with Reattach() instead.
	 */
class LogSink
{
public:
	explicit LogSink (Channel channel)
		: m_Channel (std::move (channel))
	{
	}

	/** Send the one-time configuration and remember it for restarts. */
	i2p::expected<void, std::string> Configure (bool useSyslog, std::string_view path, std::string_view tag);
	/**
	 * Configure the next process without opening the path locally. Used by
	 * the supervisor when the log worker itself owns the filesystem
	 * capability; the supervisor never opens the log file.
	 */
	i2p::expected<void, std::string> ConfigureProxy (bool useSyslog, std::string_view path, std::string_view tag);
	/** Attach a freshly spawned worker after re-sending the configuration. */
	i2p::expected<void, std::string> Reattach (Channel channel);
	/** Reattach a proxy-owned worker without opening the log file locally. */
	i2p::expected<void, std::string> ReattachProxy (Channel channel);
	void Write (uint8_t level, std::time_t timestamp, std::string_view text);
	i2p::expected<void, std::string> Shutdown ();
	bool IsOpen () const;
	bool IsShutdown () const;
	/** Records dropped because the sink was closed or a send failed. */
	uint64_t DroppedCount () const;

private:
	/** Send one configuration frame and prove the worker is live/configured. */
	i2p::expected<void, std::string> ConfigureChannel (
		Channel & channel, MessageType type, bool useSyslog, std::string_view path, std::string_view tag, int fd);
	i2p::expected<void, std::string> WaitForPong (Channel & channel, uint32_t expected, int timeoutMs);
	mutable std::mutex m_Mutex;
	Channel m_Channel;
	uint32_t m_NextRequest = 0;
	uint64_t m_Dropped = 0;
	bool m_Configured = false;
	bool m_Shutdown = false;
	bool m_Syslog = false;
	std::string m_Path;
	std::string m_Tag;
};

/** Entry point for the isolated log worker process. */
int RunLogWorker (int fd);

/**
 * Flatten control characters and bound one log text fragment. The file sink
 * and the syslog sink both use this so a peer cannot inject extra physical
 * log lines through '\n', '\r', ESC or other C0/DEL bytes.
 */
std::string SanitizeLogText (std::string_view text);
}

#endif

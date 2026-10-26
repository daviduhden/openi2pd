/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: LogWorker.
 *
 * Declares the parent-side LogSink and the isolated log-worker entry point.
 * The sink serialises bounded records and supports reattachment after a
 * restart, while the worker owns log-file or syslog output, secret redaction,
 * text sanitisation and the sink byte budget.
 *
 */

#ifndef I2PD_PRIVSEP_LOG_WORKER_H__
#define I2PD_PRIVSEP_LOG_WORKER_H__

#include <cstdint>
#include <ctime>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>

#include "Channel.h"

namespace i2p::privsep
{
/**
 * Appended to a log fragment that had to be cut at the text bound. The marker
 * lives inside the bound, so one sanitised record never exceeds MAX_TEXT_SIZE.
 */
inline constexpr std::string_view LOG_TRUNCATION_MARKER = "...[truncated]";
/** Replacement for the value of a sensitive `<key>=<value>` pair. */
inline constexpr std::string_view LOG_REDACTED_VALUE = "<redacted>";
/** Bound on redactions per record; keeps the pass cheap for hostile input. */
inline constexpr size_t MAX_LOG_REDACTIONS = 64;

/**
 * Byte budget for the log file sink. Reaching it stops appends; there is no
 * rotation and existing content is never truncated or rewritten. The
 * ConfigureLog payload has no spare field (and its decoder requires the exact
 * frame length), so this is a compile-time policy rather than a wire field; a
 * future protocol version may carry it explicitly.
 */
inline constexpr uint64_t MAX_LOG_SINK_BYTES = 64 * 1024 * 1024;

/**
 * Whether one more record still fits the file-sink budget. Pure so the bound
 * can be tested without writing 64 MiB; the worker passes its own counters.
 */
constexpr bool FitsLogSinkBudget (
	uint64_t currentBytes, uint64_t recordBytes, uint64_t cap = MAX_LOG_SINK_BYTES)
{
	return currentBytes <= cap && recordBytes <= cap - currentBytes;
}

/**
 * Parent-side log sink. It only serializes structured records to the log
 * worker; the router process never opens or writes the log file itself.
 *
 * The sink object stays alive for the router lifetime so the logger's sink
 * callback never points at a replaced object. The in-process restart
 * monitor swaps the underlying channel with Reattach() instead.
 *
 * Concurrency: every field is guarded by m_Mutex, but Write() only snapshots
 * the channel under the lock and performs the bounded send outside it. A wedged
 * worker therefore costs one slow record instead of stalling the router's
 * logger thread (and every other sink reader) for the send timeout.
 */
class LogSink
{
public:
	explicit LogSink (Channel channel)
		: m_Channel (std::make_shared<Channel> (std::move (channel)))
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
	/** Records dropped because the sink was closed, detached or a send failed. */
	uint64_t DroppedCount () const;
	/** Documented alias of DroppedCount(); records this process discarded. */
	uint64_t GetDroppedCount () const { return DroppedCount (); }
	/**
	 * The subset of the drop counter caused by worker unavailability (closed
	 * channel, detached worker or a failed send) rather than by a rejected
	 * level or a shutdown. The worker cannot report its own drops because the
	 * protocol has no such message, so this stays a client-side attribution.
	 */
	uint64_t GetWorkerDroppedCount () const;

private:
	/** Send one configuration frame and prove the worker is live/configured. */
	i2p::expected<void, std::string> ConfigureChannel (
		Channel & channel, MessageType type, bool useSyslog, std::string_view path, std::string_view tag, int fd);
	i2p::expected<void, std::string> WaitForPong (Channel & channel, uint32_t expected, int timeoutMs);
	void CountDroppedLocked (bool workerUnavailable);
	mutable std::mutex m_Mutex;
	/**
	 * Shared so a concurrent Write can keep the channel alive while it sends
	 * outside the lock: the descriptor is only closed when the last in-flight
	 * sender releases it.
	 */
	std::shared_ptr<Channel> m_Channel;
	uint32_t m_NextRequest = 0;
	uint64_t m_Dropped = 0;
	uint64_t m_WorkerDropped = 0;
	bool m_Configured = false;
	bool m_Shutdown = false;
	/** A send failed; the channel is unusable until a reattach replaces it. */
	bool m_ChannelFailed = false;
	bool m_Syslog = false;
	std::string m_Path;
	std::string m_Tag;
};

/** Entry point for the isolated log worker process. */
int RunLogWorker (int fd);

/**
 * Bound one log text fragment and neutralise anything that could forge a
 * physical line or drive a terminal. C0 controls other than tab, DEL and the C1
 * block (U+0080..U+009F, which contains the single-byte CSI of several legacy
 * 8-bit encodings) become spaces; invalid UTF-8, or a sequence cut by the text
 * bound, becomes '?'. A multi-byte sequence is never split, and text longer
 * than the bound is cut at a sequence boundary and marked with
 * LOG_TRUNCATION_MARKER.
 */
std::string SanitizeLogText (std::string_view text);

/**
 * Replace the value of a `<sensitive key><separator><value>` pair with
 * LOG_REDACTED_VALUE. The key set is compiled in (password, private key,
 * privatekey, passphrase, token, secret), matched case-insensitively at an
 * identifier boundary, with '=' or ':' as separator. Everything that is not a
 * matched pair is copied byte for byte. The pass is linear in the input and the
 * redaction count is capped at MAX_LOG_REDACTIONS, so the output is bounded by
 * the input plus MAX_LOG_REDACTIONS × (LOG_REDACTED_VALUE.size () - 1) bytes and
 * hostile input cannot make the pass expensive.
 */
std::string RedactLogSecrets (std::string_view text);
}

#endif

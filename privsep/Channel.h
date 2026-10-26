/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: Channel.
 *
 * Declares the SOCK_SEQPACKET channel abstraction for isolated workers,
 * including bounded sends and receives, SCM_RIGHTS descriptor transfer and
 * peer credential lookup. Received descriptors are owned by the caller, and
 * Linux per-message credentials are checked against SO_PEERCRED.
 *
 */

#ifndef I2PD_PRIVSEP_CHANNEL_H__
#define I2PD_PRIVSEP_CHANNEL_H__

#include "Expected.h"
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "Protocol.h"

namespace i2p::privsep
{
struct PeerCredentials
{
	uint32_t uid = 0;
	uint32_t gid = 0;
	uint32_t pid = 0;
};

struct ReceivedMessage
{
	Message message;
	int fd = -1; // SCM_RIGHTS descriptor, owned by the receiver
};

/**
 * Expected identity of the process at the other end of a channel.
 *
 * Linux `SO_PEERCRED` reports the credentials captured when the socket was
 * created, not the live sender, so comparing every frame against it breaks as
 * soon as the spawner drops the child to another uid. A channel therefore
 * carries an explicit expectation set by the party that knows the topology:
 * the core expects the worker uid/gid/pid, the worker expects its own uid and
 * accepts the root core that delegated the role. `configured=false` keeps the
 * legacy creator-credential check for raw transport tests only.
 */
struct PeerExpectation
{
	uint32_t uid = 0;
	uint32_t gid = 0;
	uint32_t pid = 0;
	bool requireGid = false;
	bool requirePid = false;
	/** Accept uid 0 in addition to @ref uid (unprivileged worker + root core). */
	bool allowRoot = false;
	bool configured = false;
};

/**
 * How much of the message registry a channel enforces.
 *
 * `Transport` (the default) checks framing, size, credentials and the
 * one-descriptor rule only; it is what the raw transport tests and a
 * request-issuing client need.
 *
 * `Endpoint` is what a serving worker sets: it additionally enforces the
 * per-type descriptor policy (kind and requiredness), answers an attributable
 * frame fault with a typed `Error` correlated by request id instead of
 * dropping the connection, and accounts every tolerated violation against
 * `MAX_PROTOCOL_VIOLATIONS`. Credential and ancillary-data failures stay
 * fatal in both modes.
 */
enum class EnforcementPolicy : uint8_t
{
	Transport = 0,
	Endpoint = 1
};

class Channel
{
public:
	Channel () = default;
	explicit Channel (int fd)
		: m_Fd (fd)
	{
	}
	~Channel ();
	Channel (Channel && other) noexcept
		: m_Fd (other.m_Fd)
		, m_EnforcementPolicy (other.m_EnforcementPolicy)
		, m_ProtocolViolations (other.m_ProtocolViolations)
		, m_PeerExpectation (other.m_PeerExpectation)
	{
		other.m_Fd = -1;
	}
	Channel & operator= (Channel && other) noexcept;
	Channel (const Channel &) = delete;
	Channel & operator= (const Channel &) = delete;

	static i2p::expected<std::pair<Channel, Channel>, std::string> CreatePair ();
	/** Adopt an inherited worker channel; expects this uid (or root). */
	static Channel Adopt (int fd);
	/**
	 * Adopt a descriptor received from another process after validating that
	 * it is an AF_UNIX SOCK_SEQPACKET socket with the expected peer UID. On
	 * failure the descriptor is closed and the caller receives an error; it
	 * is never returned half-validated.
	 */
	static i2p::expected<Channel, std::string> AdoptValidated (int fd, uint32_t expectedUid);

	bool IsOpen () const { return m_Fd >= 0; }
	int Fd () const { return m_Fd; }
	/** Detach the descriptor without closing it; ownership passes to the caller. */
	int Release ()
	{
		const int fd = m_Fd;
		m_Fd = -1;
		return fd;
	}
	void Close ();

	i2p::expected<void, std::string> Send (const Message & message, int timeoutMs = 5000);
	/**
	 * Send one message with at most one SCM_RIGHTS descriptor. The descriptor
	 * is an explicitly authorized side channel; the protocol payload remains
	 * unchanged. Callers must only pass descriptors already covered by the
	 * operation policy (for example a pre-opened log or storage file).
	 *
	 * The send is atomic (SOCK_SEQPACKET) and bounded by @a timeoutMs even if
	 * the peer drains the queue concurrently: the implementation polls with
	 * an absolute deadline and retries EAGAIN using MSG_DONTWAIT. A negative
	 * timeout keeps the historical infinite-wait meaning.
	 */
	i2p::expected<void, std::string> SendWithFd (const Message & message, int fd, int timeoutMs = 5000);
	i2p::expected<std::optional<Message>, std::string> Receive (int timeoutMs = 1000);
	/**
	 * Receive one frame and optionally one descriptor. Any SCM_RIGHTS
	 * descriptor is returned to the caller and must be closed by it. More
	 * than one descriptor is a protocol error. On Linux the per-message
	 * SCM_CREDENTIALS are checked against SO_PEERCRED before the frame is
	 * accepted.
	 *
	 * Concurrency contract: each endpoint must have exactly one reader.
	 * SOCK_SEQPACKET delivers every record to exactly one recvmsg, so a
	 * second concurrent Receive/ReceiveWithFd caller would steal frames from
	 * the first instead of duplicating them. Send is atomic per record, but
	 * callers that require ordering must serialize their writers as well.
	 *
	 * The receive is bounded by @a timeoutMs. A ready record can still be
	 * taken by another reader between poll() and recvmsg(); the resulting
	 * EAGAIN is retried until the absolute deadline rather than blocking.
	 * A negative timeout keeps the historical infinite-wait meaning.
	 */
	i2p::expected<std::optional<ReceivedMessage>, std::string> ReceiveWithFd (int timeoutMs = 1000);
	i2p::expected<uint32_t, std::string> PeerUid () const;
	i2p::expected<PeerCredentials, std::string> Credentials () const;

	/** Set the identity every received frame must carry (Linux per-message). */
	void ExpectPeer (const PeerExpectation & expectation) { m_PeerExpectation = expectation; }
	const PeerExpectation & GetPeerExpectation () const { return m_PeerExpectation; }
	/** Testable predicate used by the Linux `SCM_CREDENTIALS` path. */
	bool MatchesPeerExpectation (uint32_t uid, uint32_t gid, uint32_t pid) const;

	void SetEnforcementPolicy (EnforcementPolicy policy) { m_EnforcementPolicy = policy; }
	EnforcementPolicy GetEnforcementPolicy () const { return m_EnforcementPolicy; }
	/** Frames answered with a typed error and discarded since construction. */
	unsigned ProtocolViolations () const { return m_ProtocolViolations; }
	/**
	 * A peer that keeps sending frames its own role may not receive (or a
	 * descriptor on a type that forbids one) is disconnected once this bound
	 * is exceeded. It is deliberately small: every tolerated frame is work a
	 * hostile same-UID peer can force.
	 */
	static constexpr unsigned MAX_PROTOCOL_VIOLATIONS = 8;

private:
	/** Reply with a typed Error and account the violation; false means "close". */
	bool RejectFrame (const Message & frame, ErrorCode code, std::string_view detail, int timeoutMs);

	int m_Fd = -1;
	EnforcementPolicy m_EnforcementPolicy = EnforcementPolicy::Transport;
	unsigned m_ProtocolViolations = 0;
	PeerExpectation m_PeerExpectation;
};
}

#endif

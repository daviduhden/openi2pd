/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: OpenBSDImsg.
 *
 * Declares the OpenBSD imsg adapter, which carries the same versioned worker
 * frames over native imsg message boundaries and SCM_RIGHTS. It stays
 * separate from the portable seqpacket channel and is available only on
 * OpenBSD.
 *
 */

#ifndef I2PD_PRIVSEP_OPENBSD_IMSG_H__
#define I2PD_PRIVSEP_OPENBSD_IMSG_H__

#include <cstdint>
#include <optional>
#include <string>

#include "Channel.h"

#if defined(__OpenBSD__)
// OpenBSD's imsg.h has no __BEGIN_DECLS guard; without this wrapper the C++
// compiler expects mangled symbols that libutil does not export.
extern "C"
{
#include <imsg.h>
}
#endif

namespace i2p::privsep
{
/**
 * OpenBSD imsg adapter for the versioned worker frame.
 *
 * The frame remains encoded by Protocol.h and is carried as the imsg payload.
 * This preserves the protocol version, request-id, type allowlist and size
 * checks while using the native OpenBSD imsg implementation for message
 * boundaries and SCM_RIGHTS (imsgbuf_allow_fdpass).
 *
 * This adapter is intentionally separate from the portable Channel: the
 * supervisor/log-worker path still uses the versioned SOCK_SEQPACKET channel
 * until every OpenBSD backend is migrated. It is exercised by the manual
 * OpenBSD CI job.
 */
class ImsgChannel
{
public:
	ImsgChannel () = default;
	~ImsgChannel ();
	ImsgChannel (ImsgChannel && other) noexcept;
	ImsgChannel & operator= (ImsgChannel && other) noexcept;
	ImsgChannel (const ImsgChannel &) = delete;
	ImsgChannel & operator= (const ImsgChannel &) = delete;

	/**
	 * Adopt an already-connected descriptor as an imsg channel.
	 *
	 * Initializes the imsg buffer, enables fd passing (imsgbuf_allow_fdpass) and
	 * switches the descriptor to non-blocking so the bounded Send/Receive loops
	 * keep their own poll-based deadlines. Ownership of @a fd passes to the
	 * channel on success; on failure it is closed before the error is returned.
	 * This entry point validates nothing: use AdoptValidated() for a descriptor
	 * received from another process.
	 */
	static i2p::expected<ImsgChannel, std::string> Adopt (int fd);
	/**
	 * Validate an inherited stream socket and peer UID before enabling imsg
	 * fd passing. The imsg adapter is not production-wired yet; this is the
	 * mandatory gate for any future supervisor/network control channel.
	 */
	static i2p::expected<ImsgChannel, std::string> AdoptValidated (int fd, uint32_t expectedUid);

	bool IsOpen () const { return m_Fd >= 0; }
	int Fd () const { return m_Fd; }
	void Close ();

	/**
	 * Send one encoded frame. When @a fd is non-negative it is passed with
	 * exactly one SCM_RIGHTS descriptor and ownership is transferred to the
	 * imsg layer after a successful compose (matching imsg_compose(3)).
	 */
	i2p::expected<void, std::string> Send (const Message & message, int fd = -1, int timeoutMs = 5000);
	/**
	 * Receive one frame, draining the imsg user buffer before polling so a
	 * second message delivered by the same read is not stranded.
	 *
	 * @warning Unlike the portable Channel, this adapter has no per-record
	 * credential check: `getpeereid` is connection-scoped and is compared once
	 * in AdoptValidated(). It also inherits the imsg ceiling - a frame larger
	 * than MAX_IMSGSIZE (16 KiB) minus the imsg header cannot be carried at all,
	 * so a per-type ceiling above that is POSIX/Windows-only until chunking
	 * exists. A received descriptor still gets FD_CLOEXEC before it is returned.
	 */
	i2p::expected<std::optional<ReceivedMessage>, std::string> Receive (int timeoutMs = 1000);

	i2p::expected<PeerCredentials, std::string> Credentials () const;
	i2p::expected<uint32_t, std::string> PeerUid () const;

private:
	int m_Fd = -1;
#ifdef __OpenBSD__
	struct imsgbuf * m_Buffer = nullptr;
#endif
};
}

#endif

/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: daemon initialization rollback.
 *
 * Records daemon initialization phases before acquisition so false returns
 * and exceptions unwind completed or partially completed phases in reverse.
 * The ledger covers process-owned resources only; it cannot undo config or
 * filesystem changes, nor can it relax OpenBSD pledge/unveil policy.
 *
 */

#ifndef I2PD_DAEMON_INIT_H
#define I2PD_DAEMON_INIT_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <utility>

namespace i2p::util
{
enum class DaemonInitPhase : uint8_t
{
	/** Log destination/sink setup; rollback stops the logger and resets it. */
	Logging,
	/** Crypto provider initialization; rollback terminates the provider. */
	Crypto,
	/**
	 * Fixed router-file write authority (router.keys/info, ntcp2/ssu2 keys).
	 * Rollback first replaces the persistence writer with a refusing callback so
	 * a failed isolated setup can never fall back to core filesystem writes.
	 */
	RouterStorage,
	/** Hash-named artifact write authority (netDb, peer profiles, destination keys). */
	DataStorage,
	/** Router context and its identity/configuration state. */
	RouterContext
};

/**
 * LIFO rollback ledger for daemon initialization.
 *
 * A phase is recorded with Begin() *before* the resource is acquired, so a
 * later false return or exception unwinds every completed or partially
 * completed phase in reverse order. A rollback callback must tolerate a partially
 * completed phase, because acquisition can fail after it created part of its
 * state, and it must not throw: Rollback() swallows a failing callback so the
 * original initialization error is the one the caller sees.
 *
 * Commit() happens once, at the end of a successful init; it clears the ledger
 * and makes the destructor a no-op. The ledger cannot undo configuration or
 * filesystem changes and cannot relax an already installed pledge/unveil
 * policy; it only releases process-owned resources.
 */
template <typename RollbackFunction> class DaemonInitTransaction
{
public:
	explicit DaemonInitTransaction (RollbackFunction rollback)
		: m_RollbackFunction (std::move (rollback))
	{
	}

	DaemonInitTransaction (const DaemonInitTransaction &) = delete;
	DaemonInitTransaction & operator= (const DaemonInitTransaction &) = delete;

	~DaemonInitTransaction () { Rollback (); }

	/** Record @a phase as acquired from this point on. */
	void Begin (DaemonInitPhase phase)
	{
		if (m_PhaseCount == m_Phases.size ()) throw std::length_error ("too many daemon initialization phases");
		m_Phases[m_PhaseCount++] = phase;
	}

	/** Record the phase, then run the operation that acquires it. */
	template <typename Operation> decltype (auto) Run (DaemonInitPhase phase, Operation && operation)
	{
		Begin (phase);
		return std::forward<Operation> (operation) ();
	}

	/** Keep every acquired phase and disable the destructor rollback. */
	void Commit () noexcept
	{
		m_Committed = true;
		m_PhaseCount = 0;
	}

	/**
	 * Unwind recorded phases in reverse order. Safe to call repeatedly and from
	 * the destructor; a committed transaction does nothing.
	 */
	void Rollback () noexcept
	{
		if (m_Committed) return;
		while (m_PhaseCount)
		{
			const auto phase = m_Phases[--m_PhaseCount];
			try
			{
				m_RollbackFunction (phase);
			}
			catch (...)
			{
				// Continue unwinding; the original initialization error wins.
			}
		}
		m_Committed = true;
	}

private:
	RollbackFunction m_RollbackFunction;
	/// One slot per DaemonInitPhase; Begin() rejects a sixth registration.
	std::array<DaemonInitPhase, 5> m_Phases { };
	std::size_t m_PhaseCount = 0;
	bool m_Committed = false;
};
}

#endif // I2PD_DAEMON_INIT_H

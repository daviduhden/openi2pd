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
		Logging,
		Crypto,
		RouterStorage,
		RouterContext
	};

	template <typename RollbackFunction>
	class DaemonInitTransaction
	{
	public:
		explicit DaemonInitTransaction (RollbackFunction rollback)
			: m_RollbackFunction (std::move (rollback))
		{
		}

		DaemonInitTransaction (const DaemonInitTransaction &) = delete;
		DaemonInitTransaction & operator= (const DaemonInitTransaction &) = delete;

		~DaemonInitTransaction () { Rollback (); }

		void Begin (DaemonInitPhase phase)
		{
			if (m_PhaseCount == m_Phases.size ()) throw std::length_error ("too many daemon initialization phases");
			m_Phases[m_PhaseCount++] = phase;
		}

		template <typename Operation>
		decltype (auto) Run (DaemonInitPhase phase, Operation && operation)
		{
			Begin (phase);
			return std::forward<Operation> (operation) ();
		}

		void Commit () noexcept
		{
			m_Committed = true;
			m_PhaseCount = 0;
		}

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
		std::array<DaemonInitPhase, 4> m_Phases { };
		std::size_t m_PhaseCount = 0;
		bool m_Committed = false;
	};
}

#endif // I2PD_DAEMON_INIT_H

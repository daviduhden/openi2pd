/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * Exercises ACK completion authority without a router or network.
 * Retained handlers model already queued successful timer completions.
 */

#include <atomic>
#include <cassert>
#include <functional>
#include <memory>
#include <future>
#include <thread>
#include "UDPTunnel.h"

namespace
{
class AckFixture final : public i2p::client::UDPConnection
{
public:
	explicit AckFixture (boost::asio::io_context & service)
		: UDPConnection (service, nullptr)
	{
	}

	AckHandler Prepare (uint32_t sequence)
	{
		std::lock_guard lock (m_AckAdmission->mutex);
		m_AckTimerSeqn = sequence;
		return MakeAckHandlerLocked ();
	}

	unsigned Actions () const { return m_Actions; }
	void WaitForStopRequest () const { m_AckAdmission->stopRequested.wait (false); }
	std::function<void ()> beforeAction;

protected:
	void HandleAckTimeout () override
	{
		if (beforeAction) beforeAction ();
		++m_Actions;
	}

private:
	unsigned m_Actions = 0;
};
}

int main ()
{
	boost::asio::io_context service;
	auto connection = std::make_shared<AckFixture> (service);

	// Acknowledgment retires a successful completion before its dispatch.
	auto acknowledged = connection->Prepare (7);
	connection->Acked (7);
	acknowledged ({ });
	assert (connection->Actions () == 0);

	// An old wait cannot consume or mutate a successor's authority.
	auto stale = connection->Prepare (8);
	connection->CancelAckTimer ();
	assert (!connection->GetAckCancellationError ());
	auto current = connection->Prepare (9);
	stale ({ });
	assert (connection->Actions () == 0);
	current ({ });
	current ({ });
	assert (connection->Actions () == 1);

	// A zero-sequence probe is still a real wait. Clearing its whole
	// outstanding window must revoke that wait before a queued success.
	auto probe = connection->Prepare (0);
	connection->m_UnackedDatagrams.push_back ({ 9, 1 });
	connection->Acked (9);
	probe ({ });
	assert (connection->Actions () == 1);
	assert (connection->m_UnackedDatagrams.empty ());

	// An error retires the active sequence, allowing subsequent waits.
	auto failed = connection->Prepare (15);
	failed (boost::asio::error::operation_aborted);
	assert (connection->m_AckTimerSeqn.load () == 0);
	failed ({ });
	assert (connection->Actions () == 1);

	// Restart cannot revive a completion admitted by a prior run.
	auto stopped = connection->Prepare (10);
	connection->Stop ();
	connection->Start ();
	auto restarted = connection->Prepare (11);
	stopped ({ });
	assert (connection->Actions () == 1);
	restarted ({ });
	assert (connection->Actions () == 2);

	// The timeout action is admitted before Stop requests the gate.
	// Wait on the actual stop request, not a signal sent before Stop.
	std::promise<void> admitted;
	std::promise<void> releaseAction;
	auto mayFinish = releaseAction.get_future ().share ();
	std::atomic<bool> actionFinished { false }, stopReturned { false };
	std::atomic<bool> stoppedAfterAction { false };
	connection->beforeAction = [&]
	{
		admitted.set_value ();
		mayFinish.wait ();
		actionFinished.store (true);
	};
	auto blocked = connection->Prepare (16);
	std::jthread dispatch ([blocked] { blocked ({ }); });
	admitted.get_future ().wait ();
	std::jthread stop (
		[&]
		{
			connection->Stop ();
			stoppedAfterAction.store (actionFinished.load ());
			stopReturned.store (true);
		});
	connection->WaitForStopRequest ();
	const bool returnedBeforeRelease = stopReturned.load ();
	releaseAction.set_value ();
	dispatch.join ();
	stop.join ();
	assert (!returnedBeforeRelease);
	assert (stoppedAfterAction.load ());
	assert (stopReturned.load ());
	connection->beforeAction = { };
	connection->Start ();

	// Even a real undrained wait must not retain its owning connection.
	connection->ScheduleAckTimer (12);
	std::weak_ptr<AckFixture> released = connection;
	auto retained = connection->Prepare (13);
	connection.reset ();
	assert (released.expired ());
	retained ({ });
	return 0;
}

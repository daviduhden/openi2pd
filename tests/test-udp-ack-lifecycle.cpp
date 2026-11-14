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
#include <limits>
#include <future>
#include <thread>
#include "UDPTunnel.h"

namespace i2p::client
{
struct UDPPeriodicTestAccess
{
	template <typename Owner> static void Activate (Owner & owner)
	{
		owner.RetirePeriodicTimers ();
		owner.m_PeriodicAdmission = std::make_shared<UDPPeriodicAdmission> ();
		owner.m_IsStarted = true;
	}
	template <typename Owner> static void ArmStats (Owner & owner)
	{
		std::lock_guard lock (owner.m_PeriodicAdmission->mutex);
		owner.m_StatsTimer = std::make_unique<boost::asio::steady_timer> (owner.m_LocalDest->GetService ());
		owner.ScheduleStatsTimer ();
	}
	template <typename Owner> static auto State (const Owner & owner) { return owner.m_PeriodicAdmission; }
	template <typename Owner>
	static UDPPeriodicHandler Prepare (
		Owner & owner, UDPPeriodicSlot slot, std::function<void (Owner &, const boost::system::error_code &)> action)
	{
		std::lock_guard lock (owner.m_PeriodicAdmission->mutex);
		return owner.MakePeriodicHandlerLocked (slot, std::move (action));
	}
};
}

namespace
{
// Drive each real tunnel's callback factory without starting sockets,
// receiver registrations, resolver threads or an executor.
template <typename Owner> void CheckPeriodicAdmission (std::shared_ptr<Owner> owner)
{
	using namespace i2p::client;
	UDPPeriodicTestAccess::Activate (*owner);
	unsigned actions = 0;
	std::function<void (Owner &, const boost::system::error_code &)> action
		= [&actions] (Owner &, const boost::system::error_code &) { ++actions; };
	for (auto slot : { UDPPeriodicSlot::Stats, UDPPeriodicSlot::Maintenance })
	{
		auto old = UDPPeriodicTestAccess::Prepare (*owner, slot, action);
		auto current = UDPPeriodicTestAccess::Prepare (*owner, slot, action);
		const auto previous = actions;
		old ({ });
		old (boost::asio::error::operation_aborted);
		assert (actions == previous);
		current ({ });
		current ({ });
		assert (actions == previous + 1);
		auto cancelled = UDPPeriodicTestAccess::Prepare (*owner, slot, action);
		cancelled (boost::asio::error::operation_aborted);
		cancelled ({ });
		assert (actions == previous + 1);
	}
	auto stopped = UDPPeriodicTestAccess::Prepare (*owner, UDPPeriodicSlot::Stats, action);
	auto oldState = UDPPeriodicTestAccess::State (*owner);
	owner->Stop ();
	UDPPeriodicTestAccess::Activate (*owner);
	assert (UDPPeriodicTestAccess::State (*owner) != oldState);
	const auto beforeRestart = actions;
	stopped ({ });
	assert (actions == beforeRestart);

	std::promise<void> admitted, release;
	auto mayFinish = release.get_future ().share ();
	std::atomic<bool> stopReturned { false };
	std::function<void (Owner &, const boost::system::error_code &)> blockingAction
		= [&admitted, mayFinish] (Owner &, const boost::system::error_code &)
	{
		admitted.set_value ();
		mayFinish.wait ();
	};
	auto blocked = UDPPeriodicTestAccess::Prepare (*owner, UDPPeriodicSlot::Maintenance, blockingAction);
	auto state = UDPPeriodicTestAccess::State (*owner);
	std::jthread dispatch ([blocked] { blocked ({ }); });
	admitted.get_future ().wait ();
	std::jthread stopping (
		[&]
		{
			owner->Stop ();
			stopReturned.store (true);
		});
	state->stopRequested.wait (false);
	const bool returnedBeforeRelease = stopReturned.load ();
	release.set_value ();
	dispatch.join ();
	stopping.join ();
	assert (!returnedBeforeRelease && stopReturned.load ());
	assert (!owner->GetPeriodicCancellationError ());

	UDPPeriodicTestAccess::Activate (*owner);
	UDPPeriodicTestAccess::ArmStats (*owner);
	auto orphan = UDPPeriodicTestAccess::Prepare (*owner, UDPPeriodicSlot::Stats, action);
	std::weak_ptr<Owner> weak = owner;
	const auto beforeRelease = actions;
	owner.reset ();
	assert (weak.expired ());
	orphan ({ });
	assert (actions == beforeRelease);
}

void TestKeepAliveDeadlineBounds ()
{
	using i2p::client::UDPKeepAliveDue;
	constexpr auto longest = std::numeric_limits<uint32_t>::max ();
	constexpr auto delay = static_cast<uint64_t> (longest) * 1000;
	assert (!UDPKeepAliveDue (delay, 0, longest));
	assert (UDPKeepAliveDue (delay + 1, 0, longest));
	constexpr auto latest = std::numeric_limits<uint64_t>::max ();
	assert (!UDPKeepAliveDue (latest, latest - 500, 1));
	assert (!UDPKeepAliveDue (1000, 2000, 1));
	assert (!UDPKeepAliveDue (2000, 1000, 1));
	assert (UDPKeepAliveDue (2001, 1000, 1));
}

void TestTunnelPeriodicAdmission ()
{
	boost::asio::io_context service;
	auto keys = i2p::data::PrivateKeys::CreateRandomKeys (
		i2p::data::SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519, i2p::data::CRYPTO_KEY_TYPE_ECIES_X25519_AEAD, true);
	auto destination = std::make_shared<i2p::client::ClientDestination> (service, keys, false);
	CheckPeriodicAdmission (std::make_shared<i2p::client::I2PUDPServerTunnel> ("periodic-server", destination,
		boost::asio::ip::address_v4::loopback (),
		boost::asio::ip::udp::endpoint (boost::asio::ip::address_v4::loopback (), 1), 1, false));
	CheckPeriodicAdmission (std::make_shared<i2p::client::I2PUDPClientTunnel> ("periodic-client", "unused.i2p",
		boost::asio::ip::udp::endpoint (boost::asio::ip::address_v4::loopback (), 0), destination, 1, false,
		i2p::datagram::eDatagramV3));
}

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
	TestKeepAliveDeadlineBounds ();
	TestTunnelPeriodicAdmission ();
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

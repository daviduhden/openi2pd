/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: test router profile synchronization.
 *
 * Exercises concurrent profile mutation/read access and canonical profile
 * publication when several callers observe the same uncached identity.
 *
 */

#include <atomic>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <future>
#include <memory>
#include <thread>
#include <vector>

#include "Profiling.h"

int main ()
{
	i2p::data::RouterProfile profile;
	std::atomic<bool> start = false;
	std::vector<std::jthread> workers;
	for (int worker = 0; worker < 4; ++worker)
		workers.emplace_back ([&, worker]
			{
				while (!start.load (std::memory_order_acquire)) std::this_thread::yield ();
				for (int i = 0; i < 2000; ++i)
				{
					profile.TunnelBuildResponse ((i + worker) % 3 ? 0 : 1);
					profile.TunnelNonReplied ((i & 1) ? 1 : 2);
					profile.Unreachable ((i & 7) == 0);
					profile.Connected ();
					profile.SetLastEndpoint ({ boost::asio::ip::address_v4::loopback (), 7656 });
				}
			});
	for (int reader = 0; reader < 4; ++reader)
		workers.emplace_back ([&]
			{
				while (!start.load (std::memory_order_acquire)) std::this_thread::yield ();
				for (int i = 0; i < 4000; ++i)
				{
					(void)profile.IsBad ();
					(void)profile.IsUnreachable ();
					(void)profile.IsUseful ();
					(void)profile.GetLastEndpoint ();
					(void)profile.IsUpdated ();
				}
			});
	start.store (true, std::memory_order_release);
	workers.clear ();
	assert (profile.IsReal ());
	assert (profile.IsUseful ());
	assert (profile.HasLastEndpoint (true));

	i2p::data::RouterProfile persistenceProfile;
	persistenceProfile.Connected ();
	i2p::data::IdentHash uninitializedStorageIdent;
	uninitializedStorageIdent.Fill (0x3C);
	assert (!persistenceProfile.Save (uninitializedStorageIdent));
	assert (persistenceProfile.IsUpdated ());
	const uint64_t claimTime = i2p::data::PEER_PROFILE_PERSIST_INTERVAL + 1;
	assert (persistenceProfile.MarkPersistenceDue (claimTime));
	assert (!persistenceProfile.IsUpdated ());
	assert (!persistenceProfile.MarkPersistenceDue (claimTime + 1));
	persistenceProfile.Connected ();
	assert (persistenceProfile.IsUpdated ());
	assert (!persistenceProfile.MarkPersistenceDue (claimTime + 1));
	assert (persistenceProfile.IsUpdated ());
	persistenceProfile.MarkPersistenceFailed ();
	assert (persistenceProfile.IsUpdated ());
	assert (persistenceProfile.MarkPersistenceDue (claimTime));
	assert (!persistenceProfile.IsUpdated ());

	const auto profileRoot = std::filesystem::temp_directory_path ()
		/ ("openi2pd-profile-race-" + std::to_string (
			std::chrono::steady_clock::now ().time_since_epoch ().count ()));
	std::filesystem::create_directories (profileRoot);
	i2p::data::InitProfilesStorage (profileRoot.string ());

	i2p::data::IdentHash ident;
	ident.Fill (0x5A);
	start.store (false, std::memory_order_relaxed);
	std::vector<std::shared_ptr<i2p::data::RouterProfile>> published (24);
	workers.clear ();
	for (size_t i = 0; i < published.size (); ++i)
		workers.emplace_back ([&, i]
			{
				while (!start.load (std::memory_order_acquire)) std::this_thread::yield ();
				published[i] = i2p::data::GetRouterProfile (ident);
				published[i]->Connected ();
			});
	start.store (true, std::memory_order_release);
	workers.clear ();
	for (const auto & candidate : published) assert (candidate == published.front ());
	assert (published.front ()->IsReal ());

	i2p::data::IdentHash queuedIdent;
	queuedIdent.Fill (0xA5);
	std::atomic<unsigned> applied = 0;
	start.store (false, std::memory_order_relaxed);
	workers.clear ();
	for (int producer = 0; producer < 4; ++producer)
		workers.emplace_back ([&]
			{
				while (!start.load (std::memory_order_acquire)) std::this_thread::yield ();
				for (int i = 0; i < 250; ++i)
					i2p::data::UpdateRouterProfile (queuedIdent, [&] (auto targetProfile)
						{
							targetProfile->Connected ();
							applied.fetch_add (1, std::memory_order_relaxed);
						});
			});
	for (int flusher = 0; flusher < 4; ++flusher)
		workers.emplace_back ([&]
			{
				while (!start.load (std::memory_order_acquire)) std::this_thread::yield ();
				for (int i = 0; i < 32; ++i)
				{
					auto pending = i2p::data::FlushPostponedRouterProfileUpdates ();
					if (pending.valid ()) pending.wait ();
				}
			});
	start.store (true, std::memory_order_release);
	workers.clear ();
	auto pending = i2p::data::FlushPostponedRouterProfileUpdates ();
	if (pending.valid ()) pending.wait ();
	assert (applied.load (std::memory_order_relaxed) == 1000);
	std::filesystem::remove_all (profileRoot);
	return 0;
}

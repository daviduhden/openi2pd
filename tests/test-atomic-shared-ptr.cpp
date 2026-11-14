/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: atomic shared ownership regressions.
 *
 * Exercise the selected implementation and the forced mutex fallback.
 * CAS must match pointer and owner identity.
 * Destructors may reenter, so final releases must happen outside locks.
 * This suite is prepared for manual CI; it is not run locally.
 */

#include <array>
#include <atomic>
#include <cassert>
#include <functional>
#include <memory>
#include <utility>

#include "AtomicSharedPtr.h"

namespace
{
template <typename T> using DefaultStorage = i2p::util::AtomicSharedPtr<T>;

template <typename T> using FallbackStorage = i2p::util::AtomicSharedPtr<T, false>;

template <typename T> bool SameOwner (const std::shared_ptr<T> & left, const std::shared_ptr<T> & right)
{
	return !left.owner_before (right) && !right.owner_before (left);
}

template <template <typename> typename Storage> void TestPublication ()
{
	Storage<int> slot;
	assert (!slot.Load ());
	auto first = std::make_shared<int> (11);
	slot.Store (first);
	assert (SameOwner (slot.Load (), first));
	assert (*slot.Load () == 11);

	auto second = std::make_shared<int> (22);
	auto previous = slot.Exchange (second);
	assert (SameOwner (previous, first));
	assert (SameOwner (slot.Load (), second));
	slot = first;
	assert (SameOwner (slot.Load (), first));
	slot.Store ({ });
	assert (!slot.Load ());
}

template <template <typename> typename Storage> void TestCompareExchange ()
{
	Storage<int> slot;
	std::shared_ptr<int> expected;
	auto first = std::make_shared<int> (11);
	assert (slot.CompareExchangeStrong (expected, first));
	assert (!expected);
	assert (SameOwner (slot.Load (), first));

	auto desired = std::make_shared<int> (22);
	expected = std::make_shared<int> (33);
	assert (!slot.CompareExchangeStrong (expected, desired));
	assert (expected.get () == first.get ());
	assert (SameOwner (expected, first));
	assert (SameOwner (slot.Load (), first));

	assert (slot.CompareExchangeStrong (expected, desired, std::memory_order_acq_rel, std::memory_order_acquire));
	assert (SameOwner (expected, first));
	assert (SameOwner (slot.Load (), desired));

	assert (slot.CompareExchangeStrong (desired, { }));
	assert (!slot.Load ());
	expected = first;
	assert (!slot.CompareExchangeStrong (expected, first));
	assert (!expected);
	assert (!slot.Load ());
}

template <template <typename> typename Storage> void TestDistinctOwnersAtSameAddress ()
{
	int value = 7;
	const auto borrow = [] (int *) { };
	auto current = std::shared_ptr<int> (&value, borrow);
	auto expected = std::shared_ptr<int> (&value, borrow);
	assert (current.get () == expected.get ());
	assert (!SameOwner (current, expected));
	Storage<int> slot (current);
	auto desired = std::make_shared<int> (9);
	assert (!slot.CompareExchangeStrong (expected, desired));
	assert (expected.get () == current.get ());
	assert (SameOwner (expected, current));
	assert (SameOwner (slot.Load (), current));
}

template <template <typename> typename Storage> void TestOwnedNullIsNotEmpty ()
{
	auto owner = std::make_shared<int> (7);
	std::shared_ptr<int> ownedNull (owner, nullptr);
	Storage<int> slot (ownedNull);
	std::shared_ptr<int> expected;
	auto desired = std::make_shared<int> (9);
	// Equal null addresses do not erase a nonempty control block.
	assert (!slot.CompareExchangeStrong (expected, desired));
	assert (!expected);
	assert (SameOwner (expected, ownedNull));
	assert (SameOwner (slot.Load (), ownedNull));
	assert (slot.CompareExchangeStrong (expected, desired));
	assert (SameOwner (slot.Load (), desired));
}

template <template <typename> typename Storage> void TestDistinctAliasesOfOneOwner ()
{
	auto owner = std::make_shared<std::array<int, 2>> (std::array<int, 2> { 7, 8 });
	std::shared_ptr<int> current (owner, &(*owner)[0]);
	std::shared_ptr<int> expected (owner, &(*owner)[1]);
	assert (SameOwner (current, expected));
	assert (current.get () != expected.get ());
	Storage<int> slot (current);
	auto desired = std::make_shared<int> (9);
	assert (!slot.CompareExchangeStrong (expected, desired));
	assert (expected.get () == current.get ());
	assert (SameOwner (expected, current));
	assert (SameOwner (slot.Load (), current));
}

struct ReentrantOwner
{
	int value = 0;
	std::function<void ()> onDestroy;

	~ReentrantOwner ()
	{
		if (onDestroy) onDestroy ();
	}
};

template <template <typename> typename Storage> void TestStoreReleasesOutsideLock ()
{
	Storage<ReentrantOwner> slot;
	bool destroyed = false;
	std::weak_ptr<ReentrantOwner> retired;
	{
		auto first = std::make_shared<ReentrantOwner> ();
		first->value = 1;
		first->onDestroy = [&slot, &destroyed]
		{
			auto visible = slot.Load ();
			assert (visible && visible->value == 2);
			destroyed = true;
		};
		retired = first;
		slot.Store (std::move (first));
	}
	auto second = std::make_shared<ReentrantOwner> ();
	second->value = 2;
	// The old sole owner is destroyed during Store. Holding the fallback
	// mutex during its callback would deterministically deadlock Load.
	slot.Store (second);
	assert (destroyed);
	assert (retired.expired ());
}

template <template <typename> typename Storage> void TestFailedCompareExchangeReleasesOutsideLock ()
{
	auto current = std::make_shared<ReentrantOwner> ();
	current->value = 2;
	Storage<ReentrantOwner> slot (current);
	bool destroyed = false;
	auto expected = std::make_shared<ReentrantOwner> ();
	expected->onDestroy = [&slot, &destroyed]
	{
		auto visible = slot.Load ();
		assert (visible && visible->value == 2);
		destroyed = true;
	};
	const std::weak_ptr<ReentrantOwner> retired = expected;
	auto desired = std::make_shared<ReentrantOwner> ();
	// Failure replaces expected with current. Its prior sole owner must
	// reach final destruction after the internal lock has been released.
	assert (!slot.CompareExchangeStrong (expected, desired));
	assert (destroyed);
	assert (retired.expired ());
	assert (SameOwner (expected, current));
	assert (SameOwner (slot.Load (), current));
}

template <template <typename> typename Storage> void RunBackend ()
{
	TestPublication<Storage> ();
	TestCompareExchange<Storage> ();
	TestDistinctOwnersAtSameAddress<Storage> ();
	TestOwnedNullIsNotEmpty<Storage> ();
	TestDistinctAliasesOfOneOwner<Storage> ();
	TestStoreReleasesOutsideLock<Storage> ();
	TestFailedCompareExchangeReleasesOutsideLock<Storage> ();
}
}

int main ()
{
	i2p::util::AtomicSharedPtr inferred (std::make_shared<int> (17));
	assert (*inferred.Load () == 17);
	RunBackend<DefaultStorage> ();
	RunBackend<FallbackStorage> ();
	return 0;
}

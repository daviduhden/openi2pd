/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: test privsep capabilities.
 *
 * Pins the role/capability matrix: the "no external network together with
 * persistent writes" invariant, the documented per-role masks, the recorded
 * migration state of the core, and the structural self-check used by the
 * supervisor at startup. No process, sandbox or socket is involved.
 *
 */

#include <cassert>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "privsep/Capabilities.h"

using namespace i2p::privsep;

namespace
{
/** Human-readable mask for assertion diagnostics. */
std::string MaskText (CapabilityMask mask)
{
	std::string text;
	for (size_t index = 0; index < CAPABILITY_COUNT; ++index)
	{
		const auto capability = static_cast<Capability> (index);
		if (!mask.Has (capability)) continue;
		if (!text.empty ()) text += '|';
		text += CapabilityName (capability);
	}
	if (text.empty ()) text = "<none>";
	return text;
}

void TestMatrixValidates ()
{
	const auto validation = ValidateCapabilityMatrix ();
	assert (validation.valid);
	assert (validation.error.empty ());
	assert (ROLE_CAPABILITY_ROW_COUNT >= PROCESS_ROLE_COUNT);
}

void TestNamesCoverEveryEnumerator ()
{
	for (size_t index = 0; index < CAPABILITY_COUNT; ++index)
	{
		const auto name = std::string_view (CapabilityName (static_cast<Capability> (index)));
		assert (!name.empty ());
		assert (name != "unknown");
	}
	assert (std::string_view (CapabilityName (Capability::Count)) == "unknown");
	for (size_t index = 0; index < PROCESS_ROLE_COUNT; ++index)
	{
		const auto name = std::string_view (ProcessRoleName (static_cast<ProcessRole> (index)));
		assert (!name.empty ());
		assert (name != "unknown");
	}
	assert (std::string_view (ProcessRoleName (ProcessRole::Count)) == "unknown");
}

void TestEveryTargetRejectsNetworkPlusWrite ()
{
	for (size_t index = 0; index < PROCESS_ROLE_COUNT; ++index)
	{
		const auto role = static_cast<ProcessRole> (index);
		const auto mask = TargetMask (role);
		assert (mask.has_value ());
		assert (!(mask->Has (Capability::ExternalNetwork) && mask->Has (Capability::PersistentWrite)));
		assert (IsRoleCapabilitySafe (role));
	}
	// The invariant itself, independent of the table.
	const auto unsafeMask = NETWORK_CAPABILITIES.With (Capability::PersistentWrite);
	assert (!SatisfiesCapabilityInvariant (unsafeMask));
	assert (unsafeMask.Has (Capability::ExternalNetwork) && unsafeMask.Has (Capability::PersistentWrite));
	assert (SatisfiesCapabilityInvariant (CapabilityMask ()));
}

void TestCoreMigrationState ()
{
	// The target is network + control only: every durable write must move to a
	// storage role, so the target mask has no persistent-write bit.
	const auto target = TargetMask (ProcessRole::Core);
	assert (target.has_value ());
	assert (*target == CORE_TARGET_CAPABILITIES);
	assert (target->Has (Capability::ExternalNetwork));
	assert (!target->Has (Capability::PersistentWrite));
	assert (IsRoleCapabilitySafe (ProcessRole::Core));

	// The legacy row records today's reality and must still violate the
	// invariant, otherwise the documented migration gap would be wrong.
	const auto legacy = LegacyMask (ProcessRole::Core);
	assert (legacy.has_value ());
	assert (*legacy == CORE_LEGACY_CAPABILITIES);
	assert (legacy->Has (Capability::PersistentWrite));
	assert (legacy->Has (Capability::ExternalNetwork));
	assert (!SatisfiesCapabilityInvariant (*legacy));
	assert (!(*legacy == *target));

	// Only the core carries a legacy row; every other role is already declared
	// at its target.
	for (size_t index = 0; index < PROCESS_ROLE_COUNT; ++index)
	{
		const auto role = static_cast<ProcessRole> (index);
		if (role == ProcessRole::Core) continue;
		assert (!LegacyMask (role).has_value ());
	}
}

void TestNetworkFreeRoles ()
{
	// Storage, log, supervisor, broker and crypto never reach the network.
	const ProcessRole networkFree[] = { ProcessRole::Log, ProcessRole::RouterStorage, ProcessRole::DataStorage,
		ProcessRole::TorrentStorage, ProcessRole::StorageBroker, ProcessRole::Crypto };
	for (const auto role : networkFree)
	{
		const auto mask = TargetMask (role);
		assert (mask.has_value ());
		assert (!mask->Has (Capability::ExternalNetwork));
	}

	// The network worker may not write anything durably, and the crypto worker
	// holds IPC only.
	const auto network = TargetMask (ProcessRole::Network);
	assert (network.has_value ());
	assert (network->Has (Capability::ExternalNetwork));
	assert (!network->Has (Capability::PersistentWrite));
	const auto crypto = TargetMask (ProcessRole::Crypto);
	assert (crypto.has_value ());
	assert (*crypto == CRYPTO_CAPABILITIES);
	assert (crypto->Bits () == CapabilityMask::Bit (Capability::Ipc));

	// Storage workers write only a delegated root, so they carry the write bit
	// and never the network bit.
	for (const auto role : { ProcessRole::RouterStorage, ProcessRole::DataStorage, ProcessRole::TorrentStorage })
	{
		const auto mask = TargetMask (role);
		assert (mask.has_value ());
		assert (mask->Has (Capability::PersistentWrite));
		assert (mask->Has (Capability::FilesystemRead));
		assert (!mask->Has (Capability::ExternalNetwork));
		assert (*mask == STORAGE_CAPABILITIES);
	}

	// The broker is the only role allowed to hand a descriptor to a child.
	for (size_t index = 0; index < PROCESS_ROLE_COUNT; ++index)
	{
		const auto role = static_cast<ProcessRole> (index);
		const auto mask = TargetMask (role);
		assert (mask.has_value ());
		if (role == ProcessRole::StorageBroker) continue;
		assert (!mask->Has (Capability::FdPassSend));
		assert (!mask->Has (Capability::FdPassReceive));
	}
}

void TestMasksAreDistinctAndDocumented ()
{
	std::vector<uint32_t> distinct;
	for (const auto & row : ROLE_CAPABILITY_MATRIX)
	{
		assert (!row.rationale.empty ());
		assert (row.mask.Bits () != 0);
		assert ((row.mask.Bits () & ~ALL_CAPABILITY_BITS) == 0);
		bool known = false;
		for (const auto bits : distinct)
		{
			if (bits == row.mask.Bits ()) known = true;
		}
		if (!known) distinct.push_back (row.mask.Bits ());
	}
	// core-legacy, core-target, supervisor, log, storage, broker, network and
	// crypto are eight distinct capability sets. The three storage roles share
	// one on purpose: they differ in their delegated root and protocol, not in
	// what they may do.
	assert (distinct.size () == 8);

	// No capability is dead, and every capability except the universal framed
	// IPC channel is withheld from at least one role.
	for (size_t index = 0; index < CAPABILITY_COUNT; ++index)
	{
		const auto capability = static_cast<Capability> (index);
		size_t granted = 0;
		size_t withheld = 0;
		for (const auto & row : ROLE_CAPABILITY_MATRIX)
		{
			if (row.mask.Has (capability))
				++granted;
			else
				++withheld;
		}
		assert (granted > 0);
		if (capability == Capability::Ipc)
			assert (withheld == 0);
		else
			assert (withheld > 0);
	}

	// Masks are composable and subtraction is exact.
	const auto combined = LOG_CAPABILITIES | STORAGE_CAPABILITIES;
	assert (combined.Has (Capability::PersistentWrite));
	assert (combined.Has (Capability::FilesystemRead));
	assert (!combined.Has (Capability::ExternalNetwork));
	assert (combined.Without (Capability::PersistentWrite) == STORAGE_CAPABILITIES.Without (Capability::PersistentWrite));
	assert (combined.Without (Capability::FilesystemRead) == LOG_CAPABILITIES);
	assert (MaskText (CapabilityMask ()) == "<none>");
}
}

int main ()
{
	TestMatrixValidates ();
	TestNamesCoverEveryEnumerator ();
	TestEveryTargetRejectsNetworkPlusWrite ();
	TestCoreMigrationState ();
	TestNetworkFreeRoles ();
	TestMasksAreDistinctAndDocumented ();
	return 0;
}

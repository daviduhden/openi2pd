/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: test privsep windows isolation.
 *
 * Windows-only checks for the per-role AppContainer isolation model: the role
 * table invariant (never an Internet capability together with a persistent
 * write root), the capability mapping back onto the platform-free capability
 * matrix, profile/SID derivation and reuse, and the delegated-root DACL grant
 * (one container-SID entry, never Everyone or ALL APPLICATION PACKAGES).
 *
 * The test creates the durable `openi2pd.storage` and `openi2pd.network`
 * profiles on the host. They are intentionally not deleted: profile removal is
 * an explicit operator action after every worker of that role is decommissioned.
 *
 */

#ifndef _WIN32

#include <cstdio>

int main ()
{
	std::puts ("SKIP: the Windows AppContainer isolation model is Windows-only");
	return 77;
}

#else

#ifndef WINVER
#define WINVER 0x0A00
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
// The CMake target already supplies these as compile definitions; only provide
// them when the test is compiled standalone.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <aclapi.h>
#include <sddl.h>

#include <cassert>
#include <cstdio>
#include <string>
#include <vector>

#include "privsep/Capabilities.h"
#include "privsep/WindowsIpc.h"
#include "privsep/WindowsIsolation.h"

using namespace i2p::privsep;

namespace
{
// Concrete (non-generic) write rights. FILE_GENERIC_WRITE cannot be used as a
// probe mask: it shares STANDARD_RIGHTS_WRITE and SYNCHRONIZE with
// FILE_GENERIC_READ, so a read-only ACE would look writable.
constexpr DWORD WRITE_ACCESS_MASK
	= static_cast<DWORD> (FILE_WRITE_DATA | FILE_APPEND_DATA | FILE_WRITE_EA | FILE_WRITE_ATTRIBUTES | DELETE);

bool MatchesStringSid (PSID sid, const wchar_t * stringSid)
{
	PSID expected = nullptr;
	if (!ConvertStringSidToSidW (stringSid, &expected)) return false;
	const bool equal = EqualSid (sid, expected) != FALSE;
	LocalFree (expected);
	return equal;
}

struct DaclSummary
{
	bool containerAce = false;
	bool containerWrites = false;
	/** Everyone (S-1-1-0) must never be added by this component. */
	bool everyone = false;
	/** ALL [RESTRICTED] APPLICATION PACKAGES must never gain write access. */
	bool anyPackageWrites = false;
};

DaclSummary InspectDacl (const std::wstring & path, PSID containerSid)
{
	DaclSummary summary;
	PACL dacl = nullptr;
	PSECURITY_DESCRIPTOR descriptor = nullptr;
	const DWORD result = GetNamedSecurityInfoW (const_cast<wchar_t *> (path.c_str ()), SE_FILE_OBJECT,
		DACL_SECURITY_INFORMATION, nullptr, nullptr, &dacl, nullptr, &descriptor);
	assert (result == ERROR_SUCCESS);
	assert (descriptor && dacl);

	ACL_SIZE_INFORMATION info { };
	assert (GetAclInformation (dacl, &info, sizeof (info), AclSizeInformation));
	for (DWORD index = 0; index < info.AceCount; ++index)
	{
		void * raw = nullptr;
		assert (GetAce (dacl, index, &raw));
		auto * ace = static_cast<ACCESS_ALLOWED_ACE *> (raw);
		if (ace->Header.AceType != ACCESS_ALLOWED_ACE_TYPE) continue;
		PSID sid = reinterpret_cast<PSID> (&ace->SidStart);
		if (EqualSid (sid, containerSid))
		{
			summary.containerAce = true;
			if ((ace->Mask & WRITE_ACCESS_MASK) != 0) summary.containerWrites = true;
			continue;
		}
		if (MatchesStringSid (sid, L"S-1-1-0")) summary.everyone = true;
		// S-1-15-2-1 is ALL APPLICATION PACKAGES, S-1-15-2-2 is ALL RESTRICTED
		// APPLICATION PACKAGES.
		if ((MatchesStringSid (sid, L"S-1-15-2-1") || MatchesStringSid (sid, L"S-1-15-2-2"))
			&& (ace->Mask & WRITE_ACCESS_MASK) != 0)
			summary.anyPackageWrites = true;
	}
	LocalFree (descriptor);
	return summary;
}

std::wstring MakeFixtureDirectory (const wchar_t * suffix)
{
	wchar_t tempPath[MAX_PATH + 1] { };
	const DWORD length = GetTempPathW (MAX_PATH + 1, tempPath);
	assert (length > 0 && length < MAX_PATH + 1);
	std::wstring path (tempPath);
	path += L"openi2pd-appcontainer-";
	path += std::to_wstring (GetCurrentProcessId ());
	path += L"-";
	path += std::to_wstring (GetTickCount64 ());
	path += L"-";
	path += suffix;
	assert (CreateDirectoryW (path.c_str (), nullptr));
	return path;
}

void TestRoleTableInvariant ()
{
	const WindowsWorkerRole roles[] { WindowsWorkerRole::Supervisor, WindowsWorkerRole::Log, WindowsWorkerRole::Storage,
		WindowsWorkerRole::Network, WindowsWorkerRole::Crypto, WindowsWorkerRole::Admin };
	for (const auto role : roles)
	{
		const auto isolation = GetWindowsRoleIsolation (role);
		// The load-bearing invariant: an Internet capability and a persistent
		// write root never coexist in one worker.
		assert (!(isolation.internetClient && isolation.persistentWrites));
		assert (!(isolation.privateNetworkClientServer && isolation.persistentWrites));
		// Network access or durable writes require the AppContainer boundary; a
		// restricted token alone is not a filesystem/network boundary.
		if (isolation.internetClient || isolation.privateNetworkClientServer || isolation.persistentWrites)
			assert (isolation.appContainer);
		// Privileges are dropped with and without the container.
		assert (isolation.restrictedToken);
		// A low-privilege AppContainer is capability-free.
		if (isolation.lowPrivilegeAppContainer)
			assert (!isolation.internetClient && !isolation.privateNetworkClientServer);
	}

	// The supervisor spawns workers, so it must not be an AppContainer child of
	// its own profile; it has no network and no delegated root.
	const auto supervisor = GetWindowsRoleIsolation (WindowsWorkerRole::Supervisor);
	assert (!supervisor.appContainer);
	assert (!supervisor.internetClient && !supervisor.privateNetworkClientServer);
	assert (!supervisor.persistentWrites);

	// Log and storage own a delegated write root and never the network.
	for (const auto role : { WindowsWorkerRole::Log, WindowsWorkerRole::Storage })
	{
		const auto isolation = GetWindowsRoleIsolation (role);
		assert (isolation.appContainer);
		assert (isolation.lowPrivilegeAppContainer);
		assert (isolation.persistentWrites);
		assert (!isolation.internetClient && !isolation.privateNetworkClientServer);
	}

	// The network worker has both client capabilities and no write root, and it
	// needs a capability, so it cannot be a low-privilege AppContainer.
	const auto network = GetWindowsRoleIsolation (WindowsWorkerRole::Network);
	assert (network.appContainer);
	assert (!network.lowPrivilegeAppContainer);
	assert (network.internetClient);
	assert (network.privateNetworkClientServer);
	assert (!network.persistentWrites);

	// Crypto and admin are control-plane only.
	for (const auto role : { WindowsWorkerRole::Crypto, WindowsWorkerRole::Admin })
	{
		const auto isolation = GetWindowsRoleIsolation (role);
		assert (isolation.appContainer);
		assert (isolation.lowPrivilegeAppContainer);
		assert (!isolation.internetClient && !isolation.privateNetworkClientServer);
		assert (!isolation.persistentWrites);
	}
}

void TestCapabilityMapping ()
{
	assert (ValidateCapabilityMatrix ().valid);
	assert (ValidateWindowsRoleMapping ().valid);

	for (size_t index = 0; index < WINDOWS_ISOLATION_ROLE_COUNT; ++index)
	{
		const auto windowsRole = static_cast<WindowsIsolationRole> (index);
		assert (index < WINDOWS_ROLE_MAPPING.size ());
		assert (WINDOWS_ROLE_MAPPING[index].role == windowsRole);
		assert (WindowsRoleToProcessRole (windowsRole) == WINDOWS_ROLE_MAPPING[index].processRole);

		const auto mapped = WindowsRoleToProcessRole (windowsRole);
		if (!mapped) continue;
		// Every mapped Windows role names a documented target row that keeps the
		// project invariant.
		assert (IsRoleCapabilitySafe (*mapped));
		const auto mask = TargetMask (*mapped);
		assert (mask.has_value ());

		// The Windows enforcement table must not exceed that row.
		const auto isolation = GetWindowsRoleIsolation (static_cast<WindowsWorkerRole> (index));
		const bool windowsNetwork = isolation.internetClient || isolation.privateNetworkClientServer;
		assert (windowsNetwork == mask->Has (Capability::ExternalNetwork));
		assert (isolation.persistentWrites == mask->Has (Capability::PersistentWrite));
	}

	// The admin worker has no process capability row yet; the gap is explicit
	// instead of being hidden by a default.
	assert (!WindowsRoleToProcessRole (WindowsIsolationRole::Admin).has_value ());
}

void TestAppContainerCreation ()
{
	if (!ProbeWindowsIsolation ().appContainer)
	{
		// Unsupported build: a typed error and never a container with a null
		// SID standing in for a real profile.
		const auto storage = CreateWindowsAppContainer (WindowsWorkerRole::Storage);
		assert (!storage.has_value ());
		assert (!storage.error ().empty ());
		return;
	}

	const auto storage = CreateWindowsAppContainer (WindowsWorkerRole::Storage);
	assert (storage.has_value ());
	assert (storage->IsValid ());
	assert (storage->Sid () != nullptr);
	assert (IsValidSid (storage->Sid ()));
	assert (storage->Name () == L"openi2pd.storage");
	assert (storage->SidString ().rfind (L"S-1-15-2-", 0) == 0);
	assert (storage->LowPrivilege ());
	// Capability-free: a low-privilege AppContainer must not carry capability
	// SIDs.
	assert (storage->Capabilities ().empty ());

	const auto network = CreateWindowsAppContainer (WindowsWorkerRole::Network);
	assert (network.has_value ());
	assert (network->IsValid ());
	assert (!network->LowPrivilege ());
	assert (network->Name () == L"openi2pd.network");
	assert (network->SidString () != storage->SidString ());
	// The network role carries exactly the Internet client and private-network
	// capability SIDs from the role table.
	assert (network->Capabilities ().size () == 2);
	for (const auto & capability : network->Capabilities ())
	{
		assert (capability.Sid != nullptr);
		assert (IsValidSid (capability.Sid));
		assert ((capability.Attributes & SE_GROUP_ENABLED) != 0);
	}
	BYTE buffer[SECURITY_MAX_SID_SIZE] { };
	DWORD size = sizeof (buffer);
	assert (CreateWellKnownSid (WinCapabilityInternetClientSid, nullptr, buffer, &size));
	assert (EqualSid (network->Capabilities ()[0].Sid, buffer));
	size = sizeof (buffer);
	assert (CreateWellKnownSid (WinCapabilityPrivateNetworkClientServerSid, nullptr, buffer, &size));
	assert (EqualSid (network->Capabilities ()[1].Sid, buffer));

	// A second call reuses the durable profile instead of creating another one:
	// the derived SID is identical.
	const auto storageAgain = CreateWindowsAppContainer (WindowsWorkerRole::Storage);
	assert (storageAgain.has_value ());
	assert (storageAgain->IsValid ());
	assert (storageAgain->SidString () == storage->SidString ());

	// The supervisor must not receive an AppContainer: it has to spawn workers.
	const auto supervisor = CreateWindowsAppContainer (WindowsWorkerRole::Supervisor);
	assert (!supervisor.has_value ());
	assert (!supervisor.error ().empty ());
}

void TestPathGrant ()
{
	if (!ProbeWindowsIsolation ().appContainer) return;
	const auto storage = CreateWindowsAppContainer (WindowsWorkerRole::Storage);
	assert (storage.has_value ());

	const std::wstring directory = MakeFixtureDirectory (L"write");
	const std::wstring readOnly = MakeFixtureDirectory (L"read");

	assert (GrantAppContainerPath (*storage, directory, true).has_value ());
	const auto writable = InspectDacl (directory, storage->Sid ());
	assert (writable.containerAce);
	assert (writable.containerWrites);
	// Exactly one container entry was added; no broad group was introduced.
	assert (!writable.everyone);
	assert (!writable.anyPackageWrites);

	// The grant is idempotent: a repeated call merges into the existing DACL.
	assert (GrantAppContainerPath (*storage, directory, true).has_value ());
	const auto repeated = InspectDacl (directory, storage->Sid ());
	assert (repeated.containerAce);
	assert (!repeated.everyone);

	assert (GrantAppContainerPath (*storage, readOnly, false).has_value ());
	const auto readable = InspectDacl (readOnly, storage->Sid ());
	assert (readable.containerAce);
	assert (!readable.containerWrites);
	assert (!readable.everyone);

	// A missing path is refused, never created.
	const auto missing = GrantAppContainerPath (*storage, directory + L"\\missing", true);
	assert (!missing.has_value ());
	assert (!missing.error ().empty ());
	// A relative path is refused before any security API runs.
	const auto relative = GrantAppContainerPath (*storage, L"relative\\root", true);
	assert (!relative.has_value ());
	// The device namespace is not a delegated filesystem root.
	const auto device = GrantAppContainerPath (*storage, L"\\\\.\\PhysicalDrive0", true);
	assert (!device.has_value ());
	// A container that was never created for a requested role is invalid and
	// cannot authorize a grant.
	const WindowsAppContainer unbuilt;
	assert (!unbuilt.IsValid ());
	assert (!unbuilt.Sid ());
	assert (unbuilt.SidString ().empty ());
	assert (WindowsAppContainerSidString (&unbuilt).empty ());
	assert (WindowsAppContainerSidString (nullptr).empty ());
	assert (!GrantAppContainerPath (unbuilt, directory, true).has_value ());

	assert (RemoveDirectoryW (directory.c_str ()));
	assert (RemoveDirectoryW (readOnly.c_str ()));
}
}

int main ()
{
	TestRoleTableInvariant ();
	TestCapabilityMapping ();
	TestAppContainerCreation ();
	TestPathGrant ();
	std::puts ("test-privsep-windows-isolation: ok");
	return 0;
}

#endif // _WIN32

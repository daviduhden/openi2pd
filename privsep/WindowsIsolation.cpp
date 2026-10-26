/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: WindowsIsolation.
 *
 * Implements the Windows role-isolation model: the per-role AppContainer
 * profile with its derived SID and capability SIDs, and the delegated-root DACL
 * grant. AppContainer membership is the filesystem/network boundary; the
 * restricted token, the Job Object and the pipe DACL are separate layers owned
 * by WindowsIpc. A profile is created once per role and reused; it is never
 * deleted here because it is a durable operator-owned object.
 *
 */

#ifdef _WIN32
// Self-sufficient TU: the project build defines these, but the AppContainer
// declarations in userenv.h are selected by WINVER, so pin them here too.
#ifndef WINVER
#define WINVER 0x0A00
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif

#include "WindowsIsolation.h"

#ifdef _WIN32

#include <windows.h>
#include <aclapi.h>
#include <sddl.h>
#include <userenv.h>

#include <array>
#include <iomanip>
#include <memory>
#include <new>
#include <sstream>
#include <string>
#include <vector>

#include "Capabilities.h"

namespace i2p::privsep
{
namespace
{
	constexpr size_t WINDOWS_ROLE_COUNT = static_cast<size_t> (WindowsWorkerRole::Admin) + 1;

	// The Windows vocabulary is duplicated in the platform-free capability
	// header so the mapping can be reviewed and tested everywhere. Pin the two
	// enumerations together: a reordering or a new role must not silently
	// shift a capability row.
	static_assert (WINDOWS_ROLE_COUNT == WINDOWS_ISOLATION_ROLE_COUNT,
		"the Windows worker roles drifted from the capability mapping");
	static_assert (
		static_cast<uint8_t> (WindowsWorkerRole::Supervisor) == static_cast<uint8_t> (WindowsIsolationRole::Supervisor),
		"Windows role order drifted: Supervisor");
	static_assert (static_cast<uint8_t> (WindowsWorkerRole::Log) == static_cast<uint8_t> (WindowsIsolationRole::Log),
		"Windows role order drifted: Log");
	static_assert (
		static_cast<uint8_t> (WindowsWorkerRole::Storage) == static_cast<uint8_t> (WindowsIsolationRole::Storage),
		"Windows role order drifted: Storage");
	static_assert (
		static_cast<uint8_t> (WindowsWorkerRole::Network) == static_cast<uint8_t> (WindowsIsolationRole::Network),
		"Windows role order drifted: Network");
	static_assert (
		static_cast<uint8_t> (WindowsWorkerRole::Crypto) == static_cast<uint8_t> (WindowsIsolationRole::Crypto),
		"Windows role order drifted: Crypto");
	static_assert (
		static_cast<uint8_t> (WindowsWorkerRole::Admin) == static_cast<uint8_t> (WindowsIsolationRole::Admin),
		"Windows role order drifted: Admin");

	const wchar_t * RoleToken (WindowsWorkerRole role)
	{
		switch (role)
		{
		case WindowsWorkerRole::Supervisor: return L"supervisor";
		case WindowsWorkerRole::Log: return L"log";
		case WindowsWorkerRole::Storage: return L"storage";
		case WindowsWorkerRole::Network: return L"network";
		case WindowsWorkerRole::Crypto: return L"crypto";
		case WindowsWorkerRole::Admin: return L"admin";
		}
		return L"unknown";
	}

	/** ASCII-only narrowing for diagnostics and profile names. */
	std::string NarrowAscii (const std::wstring & text)
	{
		std::string result;
		result.reserve (text.size ());
		for (const wchar_t ch : text) result.push_back (ch < 0x80 ? static_cast<char> (ch) : '?');
		return result;
	}

	std::wstring ProfileName (WindowsWorkerRole role)
	{
		// Deterministic per role: a second spawn of the same role derives the
		// same SID instead of creating a second, unrelated profile.
		return std::wstring (L"openi2pd.") + RoleToken (role);
	}

	std::wstring ProfileDisplayName (WindowsWorkerRole role)
	{
		return std::wstring (L"openi2pd ") + RoleToken (role) + L" worker";
	}

	std::wstring ProfileDescription (WindowsWorkerRole role)
	{
		// No secret, path or operator identity is placed in a profile object:
		// the description is readable by any local process that can enumerate
		// AppContainer profiles.
		return std::wstring (L"Privilege-separated openi2pd worker role: ") + RoleToken (role);
	}

	std::string Win32ErrorText (DWORD error)
	{
		char * message = nullptr;
		const DWORD length = FormatMessageA (
			FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, error,
			MAKELANGID (LANG_NEUTRAL, SUBLANG_DEFAULT), reinterpret_cast<LPSTR> (&message), 0, nullptr);
		if (!length || !message) return "Windows error " + std::to_string (error);
		std::string text (message, length);
		LocalFree (message);
		while (!text.empty () && (text.back () == '\r' || text.back () == '\n' || text.back () == ' '))
			text.pop_back ();
		return text;
	}

	std::string HResultText (HRESULT result)
	{
		if (HRESULT_FACILITY (result) == FACILITY_WIN32)
			return Win32ErrorText (static_cast<DWORD> (HRESULT_CODE (result)));
		std::ostringstream text;
		text << "HRESULT 0x" << std::hex << std::setw (8) << std::setfill ('0') << static_cast<unsigned long> (result);
		return text.str ();
	}

	struct LocalFreeDeleter
	{
		void operator() (void * pointer) const
		{
			if (pointer) LocalFree (pointer);
		}
	};
	using LocalPtr = std::unique_ptr<void, LocalFreeDeleter>;

	bool IsAsciiLetter (wchar_t ch)
	{
		return (ch >= L'A' && ch <= L'Z') || (ch >= L'a' && ch <= L'z');
	}

	/**
	 * A delegated root is resolved by the caller and handed in as an absolute
	 * local or UNC path. A relative path would depend on the worker's current
	 * directory, and the device namespace would let the caller name a device
	 * object instead of a filesystem directory: both are refused before any
	 * security API is called.
	 */
	bool IsAbsoluteFilesystemPath (const std::wstring & path)
	{
		if (path.size () >= 3 && IsAsciiLetter (path[0]) && path[1] == L':' && (path[2] == L'\\' || path[2] == L'/'))
			return true;
		const bool leadingPair
			= path.size () >= 2 && (path[0] == L'\\' || path[0] == L'/') && (path[1] == L'\\' || path[1] == L'/');
		if (!leadingPair) return false;
		const bool deviceNamespace = path.size () >= 4 && path[2] == L'.' && (path[3] == L'\\' || path[3] == L'/');
		return !deviceNamespace;
	}

	/**
	 * One capability SID, owned by the caller's SID_AND_ATTRIBUTES entry. The
	 * SID is a LocalAlloc block because the profile/token APIs consume a PSID
	 * and every owner in this file frees with LocalFree.
	 */
	bool AppendCapability (
		WELL_KNOWN_SID_TYPE type, std::vector<SID_AND_ATTRIBUTES> & capabilities, std::string & error)
	{
		LocalPtr sid (LocalAlloc (LPTR, SECURITY_MAX_SID_SIZE));
		if (!sid)
		{
			error = "LocalAlloc for a capability SID failed";
			return false;
		}
		DWORD size = SECURITY_MAX_SID_SIZE;
		if (!CreateWellKnownSid (type, nullptr, sid.get (), &size))
		{
			error = "CreateWellKnownSid: " + Win32ErrorText (GetLastError ());
			return false;
		}
		SID_AND_ATTRIBUTES entry { };
		entry.Sid = sid.get ();
		entry.Attributes = SE_GROUP_ENABLED;
		// A vector growth failure must not leak the SID: push_back first, then
		// transfer ownership to the vector.
		try
		{
			capabilities.push_back (entry);
		}
		catch (const std::bad_alloc &)
		{
			error = "out of memory while assembling AppContainer capabilities";
			return false;
		}
		sid.release ();
		return true;
	}

	constexpr WindowsRoleIsolation MakeIsolation (bool appContainer, bool lowPrivilege, bool internetClient,
		bool privateNetworkClientServer, bool persistentWrites)
	{
		WindowsRoleIsolation isolation { };
		isolation.appContainer = appContainer;
		isolation.lowPrivilegeAppContainer = appContainer && lowPrivilege;
		// An AppContainer worker also receives a restricted token: the token is
		// a second reduction, never the boundary on its own.
		isolation.restrictedToken = true;
		// An AppContainer token already carries Low integrity; a plain
		// restricted token keeps the caller's level (reduced by LUA_TOKEN only).
		isolation.lowIntegrity = appContainer;
		isolation.internetClient = appContainer && internetClient;
		isolation.privateNetworkClientServer = appContainer && privateNetworkClientServer;
		isolation.persistentWrites = appContainer && persistentWrites;
		return isolation;
	}

	/**
	 * The Windows isolation authority. Every OS policy for a Windows worker role
	 * must be reviewable against exactly one row.
	 *
	 * A row is a policy, not a backend. The supervisor can spawn any role, but
	 * RunWindowsWorker serves control frames only: no Windows role executes
	 * router, storage or network work yet, so these rows never justify a claim
	 * that Windows privilege separation is wired.
	 */
	constexpr std::array<WindowsRoleIsolation, WINDOWS_ROLE_COUNT> WINDOWS_ROLE_ISOLATION_TABLE {
		// Supervisor: it must spawn workers, so it cannot be an AppContainer
		// child of its own profile. Restricted token only, no network, no write
		// root; the spawned worker is what receives the container.
		MakeIsolation (false, false, false, false, false),
		// Log: writes one delegated log root, no capability and no network.
		MakeIsolation (true, true, false, false, true),
		// Storage: writes one delegated root, no capability and no network.
		MakeIsolation (true, true, false, false, true),
		// Network: external sockets, no durable write root. It needs a
		// capability, so it cannot be a low-privilege (capability-free)
		// AppContainer.
		MakeIsolation (true, false, true, true, false),
		// Crypto: key operations over the local channel only.
		MakeIsolation (true, true, false, false, false),
		// Admin: control-plane requests only.
		MakeIsolation (true, true, false, false, false)
	};

	constexpr bool ValidateRoleIsolationTable ()
	{
		for (const auto & row : WINDOWS_ROLE_ISOLATION_TABLE)
		{
			// The project invariant, restated on the Windows side: an Internet
			// capability and a persistent-write root never coexist.
			if (row.internetClient && row.persistentWrites) return false;
			if (row.privateNetworkClientServer && row.persistentWrites) return false;
			// Network or writes require the container; a restricted token alone
			// is a privilege reduction, not a filesystem/network boundary.
			if ((row.internetClient || row.privateNetworkClientServer || row.persistentWrites) && !row.appContainer)
				return false;
			// Low-privilege means capability-free: an LPAC row cannot carry a
			// capability SID.
			if (row.lowPrivilegeAppContainer && (row.internetClient || row.privateNetworkClientServer)) return false;
		}
		return true;
	}

	static_assert (ValidateRoleIsolationTable (), "the Windows role isolation table is not internally consistent");
	static_assert (!WINDOWS_ROLE_ISOLATION_TABLE[static_cast<size_t> (WindowsWorkerRole::Supervisor)].appContainer,
		"the supervisor spawns workers and must not be an AppContainer child");
	static_assert (WINDOWS_ROLE_ISOLATION_TABLE[static_cast<size_t> (WindowsWorkerRole::Network)].internetClient,
		"the network worker needs the Internet client capability");
	static_assert (WINDOWS_ROLE_ISOLATION_TABLE[static_cast<size_t> (WindowsWorkerRole::Storage)].persistentWrites,
		"the storage worker needs a delegated write root");
	static_assert (!WINDOWS_ROLE_ISOLATION_TABLE[static_cast<size_t> (WindowsWorkerRole::Log)].internetClient,
		"the log worker must not have network capability");
}

WindowsRoleIsolation GetWindowsRoleIsolation (WindowsWorkerRole role)
{
	const auto index = static_cast<size_t> (role);
	if (index >= WINDOWS_ROLE_ISOLATION_TABLE.size ())
	{
		// Unknown role: deny everything that can be denied. The default row has
		// no container, no network, no write root and no low-privilege claim.
		return WindowsRoleIsolation { };
	}
	return WINDOWS_ROLE_ISOLATION_TABLE[index];
}

/**
 * The owned AppContainer identity: the profile name, the derived package SID,
 * its string form and the capability SID array handed to the token. Every SID
 * is a LocalAlloc block freed by this object, including on a partially
 * constructed (error) instance, so no error path can leak one.
 */
struct WindowsAppContainer::Impl
{
	explicit Impl (WindowsWorkerRole role)
	{
		const auto isolation = GetWindowsRoleIsolation (role);
		if (!isolation.appContainer)
		{
			error = std::string ("worker role '") + NarrowAscii (RoleToken (role)) + "' must not use an AppContainer";
			return;
		}
		// The probe is the single authority for availability; without it the call
		// fails closed instead of silently producing a weaker restricted-token
		// worker for a role that requires the container boundary.
		if (!ProbeWindowsIsolation ().appContainer)
		{
			error = std::string ("AppContainer profiles are unavailable on this Windows build; role '")
				+ NarrowAscii (RoleToken (role)) + "' keeps its container boundary and cannot start";
			return;
		}

		name = ProfileName (role);
		lowPrivilege = isolation.lowPrivilegeAppContainer;
		if (isolation.internetClient && !AppendCapability (WinCapabilityInternetClientSid, capabilities, error)) return;
		if (isolation.privateNetworkClientServer
			&& !AppendCapability (WinCapabilityPrivateNetworkClientServerSid, capabilities, error))
			return;

		// A low-privilege AppContainer is capability-free by definition; the
		// spawner applies the LPAC attribute to the token. Refuse a contradiction
		// rather than creating a profile that cannot satisfy the policy.
		if (lowPrivilege && !capabilities.empty ())
		{
			error = std::string ("role '") + NarrowAscii (RoleToken (role))
				+ "' claims a low-privilege container with capability SIDs";
			return;
		}

		const std::wstring displayName = ProfileDisplayName (role);
		const std::wstring description = ProfileDescription (role);
		PSID profileSid = nullptr;
		HRESULT result = CreateAppContainerProfile (name.c_str (), displayName.c_str (), description.c_str (),
			capabilities.empty () ? nullptr : capabilities.data (), static_cast<DWORD> (capabilities.size ()),
			&profileSid);
		if (result == HRESULT_FROM_WIN32 (ERROR_ALREADY_EXISTS) || result == S_FALSE)
		{
			// The profile is durable and operator-owned. Reuse it by deriving its
			// SID; never delete or recreate it here (DeleteAppContainerProfile is
			// an explicit operator action after decommissioning every worker).
			if (profileSid)
			{
				LocalFree (profileSid);
				profileSid = nullptr;
			}
			result = DeriveAppContainerSidFromAppContainerName (name.c_str (), &profileSid);
		}
		if (FAILED (result))
		{
			error = std::string ("AppContainer profile for role '") + NarrowAscii (RoleToken (role))
				+ "': " + HResultText (result);
			if (profileSid) LocalFree (profileSid);
			return;
		}
		if (!profileSid || !IsValidSid (profileSid) || GetLengthSid (profileSid) == 0)
		{
			error = std::string ("AppContainer profile for role '") + NarrowAscii (RoleToken (role))
				+ "' returned no valid SID";
			if (profileSid) LocalFree (profileSid);
			return;
		}
		// Ownership moves to this object; every later early return frees it.
		sid = profileSid;

		LPWSTR sidText = nullptr;
		if (!ConvertSidToStringSidW (sid, &sidText))
		{
			error = "ConvertSidToStringSid: " + Win32ErrorText (GetLastError ());
			return;
		}
		sidString.assign (sidText);
		LocalFree (sidText);
		// An AppContainer package SID is S-1-15-2-&lt;hash&gt;. Any other shape means
		// the deterministic name resolved to something this component did not
		// create, so the token would be built against an unrelated identity.
		if (sidString.rfind (L"S-1-15-2-", 0) != 0)
		{
			error = std::string ("AppContainer profile for role '") + NarrowAscii (RoleToken (role))
				+ "' produced a SID that is not an AppContainer package SID";
			return;
		}
		valid = true;
	}

	~Impl ()
	{
		for (const auto & capability : capabilities)
		{
			if (capability.Sid) LocalFree (capability.Sid);
		}
		capabilities.clear ();
		if (sid) LocalFree (sid);
		sid = nullptr;
		valid = false;
	}

	Impl (const Impl &) = delete;
	Impl & operator= (const Impl &) = delete;

	std::wstring name;
	std::wstring sidString;
	PSID sid = nullptr;
	std::vector<SID_AND_ATTRIBUTES> capabilities;
	bool lowPrivilege = false;
	bool valid = false;
	std::string error;
};

WindowsAppContainer::WindowsAppContainer () = default;

bool WindowsAppContainer::Create (WindowsWorkerRole role, WindowsAppContainer & out, std::string & error)
{
	auto impl = std::make_unique<Impl> (role);
	if (!impl->valid)
	{
		error = impl->error.empty ()
			? std::string ("AppContainer profile creation failed for role '") + NarrowAscii (RoleToken (role)) + "'"
			: impl->error;
		return false;
	}
	out.m_Impl = std::move (impl);
	return true;
}

WindowsAppContainer::~WindowsAppContainer () = default;

WindowsAppContainer::WindowsAppContainer (WindowsAppContainer && other) noexcept
	: m_Impl (std::move (other.m_Impl))
{
}

WindowsAppContainer & WindowsAppContainer::operator= (WindowsAppContainer && other) noexcept
{
	if (this != &other) m_Impl = std::move (other.m_Impl);
	return *this;
}

bool WindowsAppContainer::IsValid () const
{
	return m_Impl && m_Impl->valid;
}

const std::wstring & WindowsAppContainer::Name () const
{
	static const std::wstring empty;
	return m_Impl ? m_Impl->name : empty;
}

const std::wstring & WindowsAppContainer::SidString () const
{
	static const std::wstring empty;
	return m_Impl ? m_Impl->sidString : empty;
}

PSID WindowsAppContainer::Sid () const
{
	return m_Impl ? m_Impl->sid : nullptr;
}

bool WindowsAppContainer::LowPrivilege () const
{
	return m_Impl && m_Impl->lowPrivilege;
}

const std::vector<SID_AND_ATTRIBUTES> & WindowsAppContainer::Capabilities () const
{
	static const std::vector<SID_AND_ATTRIBUTES> empty;
	return m_Impl ? m_Impl->capabilities : empty;
}

i2p::expected<WindowsAppContainer, std::string> CreateWindowsAppContainer (WindowsWorkerRole role)
{
	WindowsAppContainer container;
	std::string error;
	if (!WindowsAppContainer::Create (role, container, error)) return i2p::unexpected (std::move (error));
	return container;
}

// The grant is additive and scoped to one object: SetEntriesInAcl merges the
// container ACE into the existing DACL, so an administrator's entries survive
// and nothing is granted to Everyone, Authenticated Users or ALL APPLICATION
// PACKAGES. Inheriting ACEs are added only for a directory, so a delegated root
// stays usable for its children.
//
// Known limitation: the path is resolved by name here, so a hostile ancestor
// that can rename or reparse between the attribute query and
// SetNamedSecurityInfoW can redirect the grant. Callers must pass a root whose
// ancestors are already trusted (see the Windows notes in the platform skill);
// this helper does not turn a hostile path into a safe one.
i2p::expected<void, std::string> GrantAppContainerPath (
	const WindowsAppContainer & container, const std::wstring & path, bool write)
{
	if (!container.IsValid () || !container.Sid ())
		return i2p::unexpected ("invalid AppContainer SID; refusing a delegated-root grant");
	if (path.empty ()) return i2p::unexpected ("empty AppContainer grant path");
	if (path.find (L'\0') != std::wstring::npos)
		return i2p::unexpected ("AppContainer grant path contains an embedded NUL");
	if (!IsAbsoluteFilesystemPath (path))
		return i2p::unexpected ("AppContainer grant path is not an absolute local/UNC path");
	// Never create the path: a grant on a path the caller does not already own
	// would let a worker be pointed at an arbitrary directory.
	const DWORD attributes = GetFileAttributesW (path.c_str ());
	if (attributes == INVALID_FILE_ATTRIBUTES)
		return i2p::unexpected ("AppContainer grant path does not exist: " + Win32ErrorText (GetLastError ()));

	// A local copy keeps the const API of this function while still matching
	// the non-const parameter of the security APIs. They do not modify the
	// buffer.
	std::wstring target = path;
	PACL existing = nullptr;
	PSECURITY_DESCRIPTOR descriptor = nullptr;
	const DWORD queried = GetNamedSecurityInfoW (
		target.data (), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, nullptr, nullptr, &existing, nullptr, &descriptor);
	if (queried != ERROR_SUCCESS) return i2p::unexpected ("GetNamedSecurityInfo: " + Win32ErrorText (queried));
	LocalPtr descriptorGuard (descriptor);
	if (!descriptor) return i2p::unexpected ("GetNamedSecurityInfo returned no security descriptor");
	if (!existing) return i2p::unexpected ("AppContainer grant path has a null DACL; refusing to grant on it");

	EXPLICIT_ACCESS_W access { };
	// Only the container SID is added: no Everyone, no Authenticated Users and
	// no ALL APPLICATION PACKAGES. The existing entries are preserved by
	// SetEntriesInAcl.
	access.grfAccessPermissions = write
		? static_cast<DWORD> (FILE_GENERIC_READ | FILE_GENERIC_WRITE | FILE_GENERIC_EXECUTE | DELETE)
		: static_cast<DWORD> (FILE_GENERIC_READ | FILE_GENERIC_EXECUTE);
	access.grfAccessMode = GRANT_ACCESS;
	access.grfInheritance = 0;
	if ((attributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
		// The delegated root is a directory: children must be usable without a
		// second grant.
		access.grfInheritance = static_cast<DWORD> (CONTAINER_INHERIT_ACE | OBJECT_INHERIT_ACE);
	access.Trustee.TrusteeForm = TRUSTEE_IS_SID;
	access.Trustee.TrusteeType = TRUSTEE_IS_UNKNOWN;
	access.Trustee.ptstrName = static_cast<LPWSTR> (container.Sid ());

	PACL merged = nullptr;
	const DWORD combined = SetEntriesInAclW (1, &access, existing, &merged);
	if (combined != ERROR_SUCCESS) return i2p::unexpected ("SetEntriesInAcl: " + Win32ErrorText (combined));
	LocalPtr mergedGuard (merged);
	if (!merged) return i2p::unexpected ("SetEntriesInAcl returned no ACL");

	SECURITY_DESCRIPTOR_CONTROL control = 0;
	DWORD revision = 0;
	SECURITY_INFORMATION information = DACL_SECURITY_INFORMATION;
	if (GetSecurityDescriptorControl (descriptor, &control, &revision) && (control & SE_DACL_PROTECTED))
		// Preserve an existing protected DACL instead of silently letting the
		// parent start contributing inherited entries again.
		information |= PROTECTED_DACL_SECURITY_INFORMATION;

	const DWORD applied
		= SetNamedSecurityInfoW (target.data (), SE_FILE_OBJECT, information, nullptr, nullptr, merged, nullptr);
	if (applied != ERROR_SUCCESS) return i2p::unexpected ("SetNamedSecurityInfo: " + Win32ErrorText (applied));
	return { };
}

std::wstring WindowsAppContainerSidString (const WindowsAppContainer * container)
{
	return container ? container->SidString () : std::wstring ();
}
}

#endif // _WIN32

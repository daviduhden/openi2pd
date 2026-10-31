/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: WindowsIsolation.
 *
 * Declares the Windows role-isolation model: AppContainer profiles and
 * capability SIDs for storage/log/crypto/admin (no network) and network
 * (no persistent write root), a restricted-token fallback for control
 * roles, the pipe DACL identity and the delegated-root grant. The model
 * is the single authority for the invariant "no worker gets both an
 * Internet capability and a persistent-write root".
 *
 */

#ifndef I2PD_PRIVSEP_WINDOWS_ISOLATION_H__
#define I2PD_PRIVSEP_WINDOWS_ISOLATION_H__

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "Expected.h"
#include "WindowsIpc.h"

#ifdef _WIN32
namespace i2p::privsep
{
/**
 * Isolation mechanisms for one worker role. `appContainer` is mandatory
 * for a role with external network or persistent writes: a restricted
 * token alone is a privilege reduction, not a filesystem/network
 * boundary.
 */
struct WindowsRoleIsolation
{
	bool appContainer = false;
	bool lowPrivilegeAppContainer = false;
	bool restrictedToken = true;
	bool lowIntegrity = false;
	bool internetClient = false;
	bool privateNetworkClientServer = false;
	/**
	 * The role needs an explicit delegated root granted to the
	 * container SID. */
	bool persistentWrites = false;
};

WindowsRoleIsolation GetWindowsRoleIsolation (WindowsWorkerRole role);

/**
 * One AppContainer profile and its derived SID. The profile is created
 * once per role and reused; `CreateWindowsAppContainer` never deletes
 * an existing profile. The SID and capability SIDs are owned by this
 * object.
 */
class WindowsAppContainer
{
public:
	WindowsAppContainer ();
	~WindowsAppContainer ();
	WindowsAppContainer (WindowsAppContainer && other) noexcept;
	WindowsAppContainer & operator= (WindowsAppContainer && other) noexcept;
	WindowsAppContainer (const WindowsAppContainer &) = delete;
	WindowsAppContainer & operator= (const WindowsAppContainer &) = delete;

	bool IsValid () const;
	const std::wstring & Name () const;
	const std::wstring & SidString () const;
	PSID Sid () const;
	bool LowPrivilege () const;
	/** Capability SIDs for PROC_THREAD_ATTRIBUTE_SECURITY_CAPABILITIES. */
	const std::vector<SID_AND_ATTRIBUTES> & Capabilities () const;

	/**
	 * Build the profile/SID into @a out. A static factory rather than a
	 * free function keeps the private pimpl and avoids returning an
	 * incomplete type from a declaration that precedes the class
	 * definition.
	 */
	static bool Create (WindowsWorkerRole role, WindowsAppContainer & out, std::string & error);

private:
	struct Impl;
	std::unique_ptr<Impl> m_Impl;
};

/**
 * Create (or derive) the profile for a role and return its SID and
 * capability set. Fails closed on an unsupported Windows build, a
 * missing capability SID or a profile/SID mismatch. A network role gets
 * `internetClient`; storage/log/ crypto/admin get no capability and
 * (for non-supervisor roles) a low-privilege AppContainer.
 */
i2p::expected<WindowsAppContainer, std::string> CreateWindowsAppContainer (WindowsWorkerRole role);

/**
 * Grant `path` to the container SID. `write` adds GENERIC_WRITE and
 * DELETE; without it the grant is read/execute only. The existing DACL
 * is preserved (no `ALL APPLICATION PACKAGES`, no Everyone); the call
 * is fail-closed and never creates a path.
 */
i2p::expected<void, std::string> GrantAppContainerPath (
	const WindowsAppContainer & container, const std::wstring & path, bool write);

/**
 * String SID used in the pipe DACL; empty when AppContainer is
 * unavailable. */
std::wstring WindowsAppContainerSidString (const WindowsAppContainer * container);
}
#endif // _WIN32

#endif

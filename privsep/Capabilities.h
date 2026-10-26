/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: Capabilities.
 *
 * Declares the capability model for every openi2pd process role and the single
 * invariant that no role may combine external network access with persistent
 * writes. This header is deliberately platform-free: it is the declaration and
 * the compile-time contract, not the enforcement. Enforcement lives in the
 * per-platform sandbox (Landlock/seccomp, pledge/unveil, Windows tokens) and a
 * backend must never grant more than the mask declared here.
 *
 */

#ifndef I2PD_PRIVSEP_CAPABILITIES_H__
#define I2PD_PRIVSEP_CAPABILITIES_H__

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace i2p::privsep
{
/**
 * Coarse capabilities. They are intentionally few: each one is a capability a
 * compromised worker could abuse, so the model must stay small enough to audit
 * against every backend policy.
 */
enum class Capability : uint8_t
{
	/** Sockets outside the process: TCP/UDP to the external network. */
	ExternalNetwork = 0,
	/**
	 * Durable writes (create/replace/delete/fsync). Always scoped to a root
	 * that was delegated and validated before the sandbox is installed; there
	 * is no ambient "write anywhere" capability.
	 */
	PersistentWrite,
	/** Read regular files/directories, including a delegated root. */
	FilesystemRead,
	/** Signal and reap owned child processes. */
	Signal,
	/** execve() a path resolved before the sandbox was installed. */
	Exec,
	/** Send an SCM_RIGHTS descriptor (local IPC only). */
	FdPassSend,
	/** Receive an SCM_RIGHTS descriptor (local IPC only). */
	FdPassReceive,
	/** Framed local IPC channel to the core; never an external socket. */
	Ipc,
	Count
};

inline constexpr size_t CAPABILITY_COUNT = static_cast<size_t> (Capability::Count);
/** Every declared bit; used to reject a mask with an unknown capability. */
inline constexpr uint32_t ALL_CAPABILITY_BITS = (1u << CAPABILITY_COUNT) - 1;

constexpr const char * CapabilityName (Capability capability)
{
	switch (capability)
	{
	case Capability::ExternalNetwork: return "external-network";
	case Capability::PersistentWrite: return "persistent-write";
	case Capability::FilesystemRead: return "filesystem-read";
	case Capability::Signal: return "signal";
	case Capability::Exec: return "exec";
	case Capability::FdPassSend: return "fd-pass-send";
	case Capability::FdPassReceive: return "fd-pass-receive";
	case Capability::Ipc: return "ipc";
	case Capability::Count: break;
	}
	return "unknown";
}

/**
 * Process roles that own (or will own) an isolated capability set. The enum is
 * the vocabulary of the matrix below; it is not a claim that every role is
 * already split into its own process.
 */
enum class ProcessRole : uint8_t
{
	Core = 0,
	Supervisor,
	Log,
	RouterStorage,
	DataStorage,
	TorrentStorage,
	StorageBroker,
	Network,
	Crypto,
	Count
};

inline constexpr size_t PROCESS_ROLE_COUNT = static_cast<size_t> (ProcessRole::Count);

constexpr const char * ProcessRoleName (ProcessRole role)
{
	switch (role)
	{
	case ProcessRole::Core: return "core";
	case ProcessRole::Supervisor: return "supervisor";
	case ProcessRole::Log: return "log";
	case ProcessRole::RouterStorage: return "router-storage";
	case ProcessRole::DataStorage: return "data-storage";
	case ProcessRole::TorrentStorage: return "torrent-storage";
	case ProcessRole::StorageBroker: return "storage-broker";
	case ProcessRole::Network: return "network";
	case ProcessRole::Crypto: return "crypto";
	case ProcessRole::Count: break;
	}
	return "unknown";
}

/** A small, hashable-by-hand set of capabilities. */
class CapabilityMask
{
public:
	constexpr CapabilityMask () = default;
	constexpr explicit CapabilityMask (uint32_t bits)
		: m_Bits (bits)
	{
	}

	static constexpr uint32_t Bit (Capability capability) { return 1u << static_cast<uint32_t> (capability); }

	constexpr bool Has (Capability capability) const { return (m_Bits & Bit (capability)) != 0; }
	constexpr uint32_t Bits () const { return m_Bits; }
	constexpr CapabilityMask With (Capability capability) const { return CapabilityMask (m_Bits | Bit (capability)); }
	constexpr CapabilityMask Without (Capability capability) const
	{
		return CapabilityMask (m_Bits & ~Bit (capability));
	}
	constexpr bool operator== (const CapabilityMask &) const = default;

private:
	uint32_t m_Bits = 0;
};

constexpr CapabilityMask operator| (CapabilityMask left, CapabilityMask right)
{
	return CapabilityMask (left.Bits () | right.Bits ());
}

/**
 * One documented row of the matrix. `legacy` marks a set that exists today but
 * is not the target: it is recorded so the migration state is explicit instead
 * of being silently assumed to be compliant.
 */
struct RoleCapabilityRow
{
	ProcessRole role;
	bool legacy;
	CapabilityMask mask;
	std::string_view rationale;
};

/**
 * The single authority for the role/capability contract. Every backend policy
 * is reviewable against exactly one row, and the invariant below is enforced
 * for every non-legacy (target) row.
 */
inline constexpr CapabilityMask CORE_TARGET_CAPABILITIES = CapabilityMask ()
															   .With (Capability::ExternalNetwork)
															   .With (Capability::FilesystemRead)
															   .With (Capability::Signal)
															   .With (Capability::Ipc);
/**
 * Migration state, not compliance: the router core still performs netDb,
 * profile, destination-key and configuration persistence in-process. This row
 * documents reality, and it deliberately violates the invariant so the gap
 * cannot be mistaken for a satisfied requirement.
 */
inline constexpr CapabilityMask CORE_LEGACY_CAPABILITIES = CORE_TARGET_CAPABILITIES.With (Capability::PersistentWrite);

inline constexpr CapabilityMask SUPERVISOR_CAPABILITIES
	= CapabilityMask ().With (Capability::Exec).With (Capability::Signal).With (Capability::Ipc);
inline constexpr CapabilityMask LOG_CAPABILITIES
	= CapabilityMask ().With (Capability::PersistentWrite).With (Capability::Ipc);
// The three storage roles differ in their delegated root and their protocol,
// not in capability: each writes and reads only the root it was handed.
inline constexpr CapabilityMask STORAGE_CAPABILITIES
	= CapabilityMask ().With (Capability::PersistentWrite).With (Capability::FilesystemRead).With (Capability::Ipc);
inline constexpr CapabilityMask STORAGE_BROKER_CAPABILITIES = CapabilityMask ()
																  .With (Capability::Exec)
																  .With (Capability::Signal)
																  .With (Capability::FdPassSend)
																  .With (Capability::FdPassReceive)
																  .With (Capability::Ipc);
inline constexpr CapabilityMask NETWORK_CAPABILITIES
	= CapabilityMask ().With (Capability::ExternalNetwork).With (Capability::Ipc);
inline constexpr CapabilityMask CRYPTO_CAPABILITIES = CapabilityMask ().With (Capability::Ipc);

inline constexpr std::array<RoleCapabilityRow, 10> ROLE_CAPABILITY_MATRIX {
	RoleCapabilityRow { ProcessRole::Core, true, CORE_LEGACY_CAPABILITIES,
		"Current core: netDb/profile/destination/config persistence is still "
		"performed in-process. Migration backlog, not the target." },
	RoleCapabilityRow { ProcessRole::Core, false, CORE_TARGET_CAPABILITIES,
		"Network and control only. Every durable write must be delegated to a "
		"storage role, so the target mask carries no persistent-write bit." },
	RoleCapabilityRow { ProcessRole::Supervisor, false, SUPERVISOR_CAPABILITIES,
		"Owns and replaces one log worker; never opens the log path and has no "
		"network access." },
	RoleCapabilityRow { ProcessRole::Log, false, LOG_CAPABILITIES,
		"Appends to one delegated log file (or syslog over AF_UNIX); no network "
		"and no filesystem read." },
	RoleCapabilityRow { ProcessRole::RouterStorage, false, STORAGE_CAPABILITIES,
		"Owns the fixed router persistence files under its delegated root." },
	RoleCapabilityRow { ProcessRole::DataStorage, false, STORAGE_CAPABILITIES,
		"Owns hash-named runtime artifacts under its delegated root." },
	RoleCapabilityRow { ProcessRole::TorrentStorage, false, STORAGE_CAPABILITIES,
		"Owns torrent payload and metainfo under its delegated root." },
	RoleCapabilityRow { ProcessRole::StorageBroker, false, STORAGE_BROKER_CAPABILITIES,
		"Pre-pledge broker: spawns delegated storage workers and forwards exactly "
		"one channel descriptor per worker." },
	RoleCapabilityRow { ProcessRole::Network, false, NETWORK_CAPABILITIES,
		"Transport worker: external sockets only, no durable write." },
	RoleCapabilityRow {
		ProcessRole::Crypto, false, CRYPTO_CAPABILITIES, "Key/crypto worker: no network, no durable write, no exec." }
};

inline constexpr size_t ROLE_CAPABILITY_ROW_COUNT = ROLE_CAPABILITY_MATRIX.size ();

/** The target (non-legacy) mask of @a role, or nullopt when it has no row. */
constexpr std::optional<CapabilityMask> TargetMask (ProcessRole role)
{
	for (const auto & row : ROLE_CAPABILITY_MATRIX)
	{
		if (row.role == role && !row.legacy) return row.mask;
	}
	return std::nullopt;
}

/** The documented pre-migration mask of @a role, when one exists. */
constexpr std::optional<CapabilityMask> LegacyMask (ProcessRole role)
{
	for (const auto & row : ROLE_CAPABILITY_MATRIX)
	{
		if (row.role == role && row.legacy) return row.mask;
	}
	return std::nullopt;
}

/**
 * The invariant: external network access and durable writes never coexist in
 * one process. A process that can both reach the network and rewrite durable
 * state can exfiltrate or corrupt that state without crossing another boundary.
 */
constexpr bool SatisfiesCapabilityInvariant (CapabilityMask mask)
{
	return !(mask.Has (Capability::ExternalNetwork) && mask.Has (Capability::PersistentWrite));
}

/**
 * Whether the declared target of @a role satisfies the invariant. Only target
 * rows are judged: a legacy row may still violate it while the migration is
 * pending, and calling that "safe" would be a false claim.
 */
constexpr bool IsRoleCapabilitySafe (ProcessRole role)
{
	const auto mask = TargetMask (role);
	return mask.has_value () && SatisfiesCapabilityInvariant (*mask);
}

struct CapabilityValidation
{
	bool valid = false;
	std::string_view error;
};

/**
 * Structural validation of the matrix itself. Used by the unit test and by a
 * startup-time assertion in the supervisor, so a future edit that makes a
 * target row unsafe (or drops a role) fails instead of being published.
 */
constexpr CapabilityValidation ValidateCapabilityMatrix ()
{
	for (size_t index = 0; index < PROCESS_ROLE_COUNT; ++index)
	{
		const auto role = static_cast<ProcessRole> (index);
		size_t targets = 0;
		for (const auto & row : ROLE_CAPABILITY_MATRIX)
		{
			if (row.role == role && !row.legacy) ++targets;
		}
		if (targets != 1) return CapabilityValidation { false, "every role needs exactly one target row" };
	}
	for (const auto & row : ROLE_CAPABILITY_MATRIX)
	{
		if (row.mask.Bits () == 0) return CapabilityValidation { false, "a role row declares no capability" };
		if ((row.mask.Bits () & ~ALL_CAPABILITY_BITS) != 0)
			return CapabilityValidation { false, "a role row declares an unknown capability" };
		if (row.rationale.empty ()) return CapabilityValidation { false, "a role row is undocumented" };
		if (row.legacy) continue;
		if (!SatisfiesCapabilityInvariant (row.mask))
			return CapabilityValidation { false, "a target role combines external network with persistent writes" };
	}
	return CapabilityValidation { true, { } };
}

static_assert (ValidateCapabilityMatrix ().valid, "the role capability matrix is not internally consistent");
static_assert (IsRoleCapabilitySafe (ProcessRole::Core), "the core target must not combine network and durable writes");
// The migration state is pinned: the legacy row must still violate the
// invariant, otherwise the comment above would be wrong.
static_assert (!SatisfiesCapabilityInvariant (CORE_LEGACY_CAPABILITIES),
	"the recorded core legacy mask no longer exceeds the target");
static_assert (!SatisfiesCapabilityInvariant (NETWORK_CAPABILITIES.With (Capability::PersistentWrite)),
	"the invariant must reject a network role with durable writes");

/**
 * Windows worker roles. The names and order mirror `WindowsWorkerRole` in
 * privsep/WindowsIpc.h; that header is Windows-only, while this one stays
 * platform-free so the mapping below is reviewable and unit-testable on every
 * build host. The Windows isolation translation unit pins the two
 * enumerations together with static assertions.
 */
enum class WindowsIsolationRole : uint8_t
{
	Supervisor = 0,
	Log,
	Storage,
	Network,
	Crypto,
	Admin,
	Count
};

inline constexpr size_t WINDOWS_ISOLATION_ROLE_COUNT = static_cast<size_t> (WindowsIsolationRole::Count);

constexpr const char * WindowsIsolationRoleName (WindowsIsolationRole role)
{
	switch (role)
	{
	case WindowsIsolationRole::Supervisor: return "supervisor";
	case WindowsIsolationRole::Log: return "log";
	case WindowsIsolationRole::Storage: return "storage";
	case WindowsIsolationRole::Network: return "network";
	case WindowsIsolationRole::Crypto: return "crypto";
	case WindowsIsolationRole::Admin: return "admin";
	case WindowsIsolationRole::Count: break;
	}
	return "unknown";
}

/**
 * Which documented capability row a Windows worker must be reviewable against.
 * This is a mapping, not a new capability: the Windows role table
 * (privsep/WindowsIsolation.cpp) is the enforcement authority and the mask
 * here is the contract that table may not exceed.
 */
constexpr std::optional<ProcessRole> WindowsRoleToProcessRole (WindowsIsolationRole role)
{
	switch (role)
	{
	case WindowsIsolationRole::Supervisor: return ProcessRole::Supervisor;
	case WindowsIsolationRole::Log: return ProcessRole::Log;
	// The Windows storage worker covers the router, data and torrent storage
	// roles; all three declare the same capability mask, so one row is enough.
	case WindowsIsolationRole::Storage: return ProcessRole::RouterStorage;
	case WindowsIsolationRole::Network: return ProcessRole::Network;
	case WindowsIsolationRole::Crypto: return ProcessRole::Crypto;
	// The administration worker has no documented process row yet: it is
	// control-plane only. Report that instead of inventing a capability set.
	case WindowsIsolationRole::Admin: return std::nullopt;
	case WindowsIsolationRole::Count: break;
	}
	return std::nullopt;
}

struct WindowsRoleMapping
{
	WindowsIsolationRole role;
	/** Target capability row, or nullopt when no process role exists yet. */
	std::optional<ProcessRole> processRole;
	std::string_view rationale;
};

inline constexpr std::array<WindowsRoleMapping, WINDOWS_ISOLATION_ROLE_COUNT> WINDOWS_ROLE_MAPPING {
	{ { WindowsIsolationRole::Supervisor, ProcessRole::Supervisor,
		  "Owns and replaces workers; no network and no delegated write root." },
		{ WindowsIsolationRole::Log, ProcessRole::Log, "Appends to one delegated log root; no network." },
		{ WindowsIsolationRole::Storage, ProcessRole::RouterStorage,
			"Writes one delegated root; no network. Router, data and torrent "
			"storage declare the same mask." },
		{ WindowsIsolationRole::Network, ProcessRole::Network,
			"External sockets only; never a persistent-write root." },
		{ WindowsIsolationRole::Crypto, ProcessRole::Crypto, "No network, no durable write, no exec." },
		{ WindowsIsolationRole::Admin, std::nullopt,
			"Control-plane administration worker; no process capability row exists "
			"yet, so it stays Ipc-only and is tracked as a mapping backlog item." } }
};

/**
 * Structural validation of the Windows mapping. Every Windows worker role has
 * exactly one row, and every mapped row must be a target row that satisfies the
 * external-network/persistent-write invariant.
 */
constexpr CapabilityValidation ValidateWindowsRoleMapping ()
{
	for (size_t index = 0; index < WINDOWS_ISOLATION_ROLE_COUNT; ++index)
	{
		const auto role = static_cast<WindowsIsolationRole> (index);
		size_t rows = 0;
		for (const auto & row : WINDOWS_ROLE_MAPPING)
		{
			if (row.role == role) ++rows;
		}
		if (rows != 1) return CapabilityValidation { false, "every Windows role needs exactly one mapping row" };
		const auto mapped = WindowsRoleToProcessRole (role);
		if (!mapped) continue;
		if (!IsRoleCapabilitySafe (*mapped))
			return CapabilityValidation { false, "a Windows role maps to an unsafe capability row" };
	}
	for (const auto & row : WINDOWS_ROLE_MAPPING)
	{
		if (row.processRole != WindowsRoleToProcessRole (row.role))
			return CapabilityValidation { false, "a Windows mapping row disagrees with WindowsRoleToProcessRole" };
		if (row.rationale.empty ()) return CapabilityValidation { false, "a Windows mapping row is undocumented" };
	}
	return CapabilityValidation { true, { } };
}

static_assert (ValidateWindowsRoleMapping ().valid, "the Windows role mapping is not internally consistent");
// The admin worker is the single documented mapping gap; pin it so a later
// ProcessRole addition is a deliberate change and not a silent default.
static_assert (!WindowsRoleToProcessRole (WindowsIsolationRole::Admin).has_value (),
	"the admin worker mapping must stay explicit until a process role exists");
static_assert (WindowsRoleToProcessRole (WindowsIsolationRole::Network) == ProcessRole::Network,
	"the Windows network worker must map to the network capability row");
}

#endif

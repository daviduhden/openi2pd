/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: RouterContext.
 *
 * Router-wide state: the local RouterInfo and private keys, NTCP2/SSU2 static keys, status,
 * bandwidth and congestion settings, publish/cleanup timers and the current ECIES noise state.
 * Provides mutex-protected mutation and immutable snapshots for TUI/IPC readers, and
 * implements LocalDestination plus GarlicDestination so router-targeted garlic and
 * tunnel-build records can be decrypted.
 *
 */

#ifndef ROUTER_CONTEXT_H__
#define ROUTER_CONTEXT_H__

#include <inttypes.h>
#include <atomic>
#include <future>
#include <string>
#include <memory>
#include <mutex>
#include <unordered_set>
#include <utility>
#include <boost/asio.hpp>
#include "Identity.h"
#include "RouterInfo.h"
#include "Garlic.h"
#include "util.h"
#include "AtomicSharedPtr.h"

namespace i2p
{
namespace garlic
{
	class RouterIncomingRatchetSession;
}

const char ROUTER_INFO[] = "router.info";
const char ROUTER_KEYS[] = "router.keys";
const char NTCP2_KEYS[] = "ntcp2.keys";
const char SSU2_KEYS[] = "ssu2.keys";
const int ROUTER_INFO_UPDATE_INTERVAL = 30 * 60; // 30 minutes
const int ROUTER_INFO_PUBLISH_INTERVAL = 39 * 60; // in seconds
const int ROUTER_INFO_INITIAL_PUBLISH_INTERVAL = 10; // in seconds
const int ROUTER_INFO_PUBLISH_INTERVAL_VARIANCE = 105; // in seconds
const int ROUTER_INFO_CONFIRMATION_TIMEOUT = 1600; // in milliseconds
const int ROUTER_INFO_MAX_PUBLISH_EXCLUDED_FLOODFILLS = 15;
const int ROUTER_INFO_CONGESTION_UPDATE_INTERVAL = 11 * 60; // in seconds
const int ROUTER_INFO_CONGESTION_UPDATE_INTERVAL_VARIANCE = 130; // in seconds
const int ROUTER_INFO_CLEANUP_INTERVAL = 102; // in seconds

enum RouterStatus
{
	eRouterStatusOK = 0,
	eRouterStatusFirewalled = 1,
	eRouterStatusUnknown = 2,
	eRouterStatusProxy = 3,
	eRouterStatusMesh = 4,
	eRouterStatusStan = 5
};

const char * const ROUTER_STATUS_NAMES[] = {
	"OK", // 0
	"Firewalled", // 1
	"Unknown", // 2
	"Proxy", // 3
	"Mesh", // 4
	"Stan" // 5
};

enum RouterError
{
	eRouterErrorNone = 0,
	eRouterErrorClockSkew = 1,
	eRouterErrorOffline = 2,
	eRouterErrorSymmetricNAT = 3,
	eRouterErrorFullConeNAT = 4,
	eRouterErrorNoDescriptors = 5
};

class RouterContext : public i2p::garlic::GarlicDestination
{
private:
	struct NTCP2PrivateKeys
	{
		uint8_t staticPublicKey[32];
		uint8_t staticPrivateKey[32];
		uint8_t iv[16];
	};

	struct SSU2PrivateKeys
	{
		uint8_t staticPublicKey[32];
		uint8_t staticPrivateKey[32];
		uint8_t intro[32];
	};

	class RouterService : public i2p::util::RunnableServiceWithWork
	{
	public:
		RouterService ()
			: RunnableServiceWithWork ("Router") { };
		auto & GetService () { return GetIOService (); };
		void Start () { StartIOService (); };
		void Stop () { StopIOService (); };
	};

public:
	RouterContext ();
	~RouterContext ();
	void Init ();
	void Start ();
	void Stop ();

	const i2p::data::PrivateKeys & GetPrivateKeys () const { return m_Keys; };
	// Returns the live RouterInfo by reference. Mutators take m_RouterInfoMutex,
	// but this accessor cannot hold it for the caller: code that needs a
	// coherent snapshot while mutations can run concurrently must use
	// CopyRouterInfoBuffer() instead.
	i2p::data::LocalRouterInfo & GetRouterInfo () { return m_RouterInfo; };
	// Alias of the live m_RouterInfo; same caveat as GetRouterInfo().
	std::shared_ptr<i2p::data::RouterInfo> GetSharedRouterInfo ()
	{
		return std::shared_ptr<i2p::data::RouterInfo> (&m_RouterInfo, [] (i2p::data::RouterInfo *) { });
	}
	std::shared_ptr<i2p::garlic::GarlicDestination> GetSharedDestination ()
	{
		return std::shared_ptr<i2p::garlic::GarlicDestination> (this, [] (i2p::garlic::GarlicDestination *) { });
	}
	std::shared_ptr<i2p::data::RouterInfo::Buffer> CopyRouterInfoBuffer () const;
	// An immutable copy for readers outside the router service (TUI, IPC,
	// tools). The live GetRouterInfo()/GetSharedRouterInfo() accessors remain
	// for hot paths that only compare identity hashes.
	std::shared_ptr<const i2p::data::RouterInfo> GetRouterInfoSnapshot () const;

	const uint8_t * GetNTCP2StaticPublicKey () const { return m_NTCP2Keys ? m_NTCP2Keys->staticPublicKey : nullptr; };
	const uint8_t * GetNTCP2StaticPrivateKey () const { return m_NTCP2Keys ? m_NTCP2Keys->staticPrivateKey : nullptr; };
	const uint8_t * GetNTCP2IV () const { return m_NTCP2Keys ? m_NTCP2Keys->iv : nullptr; };
	i2p::crypto::X25519Keys & GetNTCP2StaticKeys ();

	const uint8_t * GetSSU2StaticPublicKey () const { return m_SSU2Keys ? m_SSU2Keys->staticPublicKey : nullptr; };
	const uint8_t * GetSSU2StaticPrivateKey () const { return m_SSU2Keys ? m_SSU2Keys->staticPrivateKey : nullptr; };
	const uint8_t * GetSSU2IntroKey () const { return m_SSU2Keys ? m_SSU2Keys->intro : nullptr; };
	i2p::crypto::X25519Keys & GetSSU2StaticKeys ();

	uint32_t GetUptime () const; // in seconds
	uint64_t GetLastUpdateTime () const { return m_LastUpdateTime.load (std::memory_order_relaxed); };
	uint64_t GetBandwidthLimit () const { return m_BandwidthLimit.load (std::memory_order_relaxed); };
	uint64_t GetTransitBandwidthLimit () const
	{
		return (m_BandwidthLimit.load (std::memory_order_relaxed) * m_ShareRatio.load (std::memory_order_relaxed))
			/ 100LL;
	};
	bool GetTesting () const { return m_Testing.load (std::memory_order_relaxed); };
	void SetTesting (bool testing);
	RouterStatus GetStatus () const { return m_Status.load (std::memory_order_relaxed); };
	void SetStatus (RouterStatus status);
	RouterError GetError () const { return m_Error.load (std::memory_order_relaxed); };
	void SetError (RouterError error) { m_Error.store (error, std::memory_order_relaxed); };
	bool GetTestingV6 () const { return m_TestingV6.load (std::memory_order_relaxed); };
	void SetTestingV6 (bool testing);
	RouterStatus GetStatusV6 () const { return m_StatusV6.load (std::memory_order_relaxed); };
	void SetStatusV6 (RouterStatus status);
	RouterError GetErrorV6 () const { return m_ErrorV6.load (std::memory_order_relaxed); };
	void SetErrorV6 (RouterError error) { m_ErrorV6.store (error, std::memory_order_relaxed); };
	int GetNetID () const { return m_NetID.load (std::memory_order_relaxed); };
	void SetNetID (int netID) { m_NetID.store (netID, std::memory_order_relaxed); };
	bool DecryptTunnelBuildRecord (
		const uint8_t * encrypted, uint8_t * data, i2p::crypto::NoiseSymmetricState & noiseState);
	bool DecryptTunnelShortRequestRecord (
		const uint8_t * encrypted, uint8_t * data, i2p::crypto::NoiseSymmetricState & noiseState);

	void UpdatePort (int port); // called from Daemon
	void UpdateAddress (const boost::asio::ip::address & host); // called from SSU2 or Daemon
	void PublishNTCP2Address (int port, bool publish, bool v4, bool v6, bool ygg, int version);
	void PublishSSU2Address (int port, bool publish, bool v4, bool v6, int version);
	bool AddSSU2Introducer (const i2p::data::RouterInfo::Introducer & introducer, bool v4);
	void RemoveSSU2Introducer (const i2p::data::IdentHash & h, bool v4);
	void UpdateSSU2Introducer (const i2p::data::IdentHash & h, bool v4, uint32_t iTag, uint32_t iExp);
	void ClearSSU2Introducers (bool v4);
	bool IsUnreachable () const;
	void SetUnreachable (bool v4, bool v6);
	void SetReachable (bool v4, bool v6);
	bool IsFloodfill () const { return m_IsFloodfill.load (std::memory_order_relaxed); };
	void SetFloodfill (bool floodfill);
	void SetFamily (const std::string & family);
	std::string GetFamily () const;
	// Read-only access to a property on the live RouterInfo under the same
	// mutex used by its mutators. Prefer this over GetRouterInfo().GetProperty()
	// when the caller is not the router service thread.
	std::string GetRouterInfoProperty (const std::string & key) const;
	void SetBandwidth (int limit); /* in kilobytes */
	void SetBandwidth (char L); /* by letter */
	void SetShareRatio (int percents); // 0 - 100
	bool AcceptsTunnels () const { return m_AcceptsTunnels.load (std::memory_order_relaxed); };
	void SetAcceptsTunnels (bool acceptsTunnels)
	{
		m_AcceptsTunnels.store (acceptsTunnels, std::memory_order_relaxed);
	};
	int GetCongestionLevel (bool longTerm) const;
	bool SupportsV6 () const { return m_RouterInfo.IsV6 (); };
	bool SupportsV4 () const { return m_RouterInfo.IsV4 (); };
	bool SupportsMesh () const { return m_RouterInfo.IsMesh (); };
	void SetSupportsV6 (bool supportsV6);
	void SetSupportsV4 (bool supportsV4);
	void SetSupportsMesh (bool supportsmesh, const boost::asio::ip::address_v6 & host);
	void SetMTU (int mtu, bool v4);
	void SetHidden (bool hide) { m_IsHiddenMode.store (hide, std::memory_order_relaxed); };
	bool IsHidden () const { return m_IsHiddenMode.load (std::memory_order_relaxed); };
	bool IsLimitedConnectivity () const
	{
		auto status = m_Status.load (std::memory_order_relaxed);
		return status == eRouterStatusProxy || status == eRouterStatusStan;
	};


	void UpdateNTCP2V6Address (const boost::asio::ip::address & host); // called from Daemon. TODO: remove
	void UpdateStats ();
	void UpdateTimestamp (uint64_t ts); // in seconds, called from NetDb before publishing

	// implements LocalDestination
	std::shared_ptr<const i2p::data::IdentityEx> GetIdentity () const override { return m_Keys.GetPublic (); };
	bool Decrypt (const uint8_t * encrypted, uint8_t * data, i2p::data::CryptoKeyType preferredCrypto) const override;
	void SetLeaseSetUpdated (bool /* post */) override { };

	// implements GarlicDestination
	std::shared_ptr<const i2p::data::LocalLeaseSet> GetLeaseSet () override { return nullptr; };
	std::shared_ptr<i2p::tunnel::TunnelPool> GetTunnelPool () const override;

	// override GarlicDestination
	void ProcessGarlicMessage (std::shared_ptr<I2NPMessage> msg) override;
	void ProcessDeliveryStatusMessage (std::shared_ptr<I2NPMessage> msg) override;
	void SubmitECIESx25519Key (const uint8_t * key, uint64_t tag) override;

protected:
	// implements GarlicDestination
	void HandleI2NPMessage (const uint8_t * buf, size_t len) override;
	bool HandleCloveI2NPMessage (I2NPMessageType typeID, const uint8_t * payload, size_t len, uint32_t msgID,
		i2p::garlic::ECIESX25519AEADRatchetSession * from) override;

private:
	void CreateNewRouter ();
	void NewRouterInfo ();
	void UpdateRouterInfo ();
	// Helpers below require m_RouterInfoMutex to be held by the caller. Keeping
	// the rebuild and the cap update lock-free prevents self-deadlock when a
	// public mutator has to perform several related changes atomically.
	void UpdateRouterInfoLocked ();
	void UpdateCapsLocked (uint8_t caps);
	// Caller must hold m_RouterInfoMutex. Returns true if an address changed.
	bool PublishNTCP2AddressLocked (int port, bool publish, bool v4, bool v6, bool ygg, int version);
	void UpdateCaps (uint8_t caps);
	void NewNTCP2Keys ();
	void NewSSU2Keys ();
	void UpdateNTCP2Keys ();
	void UpdateSSU2Keys ();
	bool Load ();
	void SaveKeys ();
	void Sign (const uint8_t * buf, int len, uint8_t * signature) const { m_Keys.Sign (buf, len, signature); };
	uint16_t SelectRandomPort () const;
	void PublishNTCP2Address (
		std::shared_ptr<i2p::data::RouterInfo::Address> address, int port, bool publish, int version) const;
	void UpdateSSU2AddressCapsIntroducer (
		std::shared_ptr<i2p::data::RouterInfo::Address> address, bool isIntroducer) const;
	void UpdateSSU2AddressCapsTesting (std::shared_ptr<i2p::data::RouterInfo::Address> address, bool isTesting) const;

	bool DecryptECIESTunnelBuildRecord (const uint8_t * encrypted, uint8_t * data, size_t clearTextSize,
		i2p::crypto::NoiseSymmetricState & noiseState);
	void PostGarlicMessage (std::shared_ptr<I2NPMessage> msg);
	void PostDeliveryStatusMessage (std::shared_ptr<I2NPMessage> msg);

	void ScheduleInitialPublish ();
	void HandleInitialPublishTimer (const boost::system::error_code & ecode);
	void SchedulePublish ();
	void HandlePublishTimer (const boost::system::error_code & ecode);
	uint64_t Publish (); // return actual publish confirmation timeout in milliseconds
	void SchedulePublishResend (uint64_t publishConfirmationTimeout);
	void HandlePublishResendTimer (const boost::system::error_code & ecode);
	void ScheduleCongestionUpdate ();
	void HandleCongestionUpdateTimer (const boost::system::error_code & ecode);
	void UpdateCongestion ();
	void ScheduleCleanupTimer ();
	void HandleCleanupTimer (const boost::system::error_code & ecode);

private:
	i2p::data::LocalRouterInfo m_RouterInfo;
	i2p::data::PrivateKeys m_Keys;
	std::shared_ptr<i2p::crypto::CryptoKeyDecryptor> m_Decryptor, m_TunnelDecryptor;
	std::shared_ptr<i2p::garlic::RouterIncomingRatchetSession> m_ECIESSession;
	// Snapshotted concurrently by TUI/IPC. These are independent scalars, so
	// relaxed ordering is sufficient; no other state is published through them.
	std::atomic<uint64_t> m_LastUpdateTime; // in seconds
	std::atomic<bool> m_AcceptsTunnels, m_IsFloodfill;
	std::atomic<uint64_t> m_StartupTime; // monotonic seconds
	std::atomic<uint64_t> m_BandwidthLimit; // allowed bandwidth
	std::atomic<int> m_ShareRatio;
	std::atomic<RouterStatus> m_Status, m_StatusV6;
	std::atomic<RouterError> m_Error, m_ErrorV6;
	std::atomic<bool> m_Testing, m_TestingV6;
	std::atomic<int> m_NetID;
	std::mutex m_StaticKeysMutex;
	std::once_flag m_NTCP2KeysOnce, m_SSU2KeysOnce;
	std::unique_ptr<NTCP2PrivateKeys> m_NTCP2Keys;
	std::unique_ptr<SSU2PrivateKeys> m_SSU2Keys;
	std::unique_ptr<i2p::crypto::X25519Keys> m_NTCP2StaticKeys, m_SSU2StaticKeys;
	// for ECIESx25519
	i2p::crypto::NoiseSymmetricState m_InitialNoiseState;
	// publish
	std::mutex m_ServiceLifecycleMutex;
	// Portable atomic shared_ptr storage with a libc++ fallback.
	mutable i2p::util::AtomicSharedPtr<RouterService> m_Service;
	std::unique_ptr<boost::asio::steady_timer> m_PublishTimer, m_CongestionUpdateTimer, m_CleanupTimer;
	std::unordered_set<i2p::data::IdentHash> m_PublishExcluded;
	uint32_t m_PublishReplyToken;
	std::atomic<bool> m_IsHiddenMode; // not publish
	// Guards m_RouterInfo, its m_Properties map and every Address reachable
	// through it, including the signed-buffer rebuild. Helpers suffixed
	// "Locked" require it to be held by the caller.
	mutable std::mutex m_RouterInfoMutex;
	std::shared_ptr<i2p::data::RouterInfo::Buffer> m_SaveBuffer;
	std::mutex m_SaveBufferMutex; // guards m_SaveBuffer
	std::future<void> m_SavingRouterInfo; // asynchronous router.info writer
	std::atomic_flag m_IsSaving = ATOMIC_FLAG_INIT;
};

extern RouterContext context;
}

#endif

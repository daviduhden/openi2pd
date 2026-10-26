/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: RouterContext.
 *
 * Implements the process-wide router context: key/identity loading or
 * generation, RouterInfo construction with NTCP2 and SSU2 addresses,
 * capabilities, family and congestion data, publication and router.info
 * persistence. Status, bandwidth and reachability updates are made safe for
 * concurrent transport/tunnel access under locks and atomics, with timers on
 * the context's asio service.
 *
 */

#include <cstring>
#include "PersistentStorage.h"
#include <algorithm>
#include <atomic>
#include <fstream>
#include <mutex>
#include <span>
#include <stdexcept>
#include <vector>
#include <openssl/crypto.h>
#include <openssl/rand.h>
#include "Config.h"
#include "Crypto.h"
#include "Ed25519.h"
#include "Timestamp.h"
#include "I2NPProtocol.h"
#include "Modern.h"
#include "NetDb.hpp"
#include "FS.h"
#include "util.h"
#include "version.h"
#include "Log.h"
#include "Family.h"
#include "ECIESX25519AEADRatchetSession.h"
#include "Transports.h"
#include "Tunnel.h"
#include "CryptoKey.h"
#include "RouterContext.h"

namespace i2p
{
namespace
{
	/**
	 * One retained fixed-file write after a failed isolated persistence
	 * attempt.
	 *
	 * `router.keys`, `ntcp2.keys` and `ssu2.keys` are only written when the
	 * material is (re)generated, so a dropped write would silently leave the
	 * process running with key material that is not durable. Exactly one
	 * serialized record is retained; the next save tick retries it and a
	 * successful write wipes the copy. The slot is process-wide because the
	 * network process owns one router context; a second context in the same
	 * process (tests) is sequential and only ever sees an empty slot.
	 *
	 * This is deliberately not a general write queue: it never grows, never
	 * retries in a loop and never falls back to a local filesystem write.
	 */
	class PendingFixedWrite
	{
	public:
		bool Store (i2p::data::RouterPersistentFile file, std::span<const uint8_t> bytes)
		{
			std::lock_guard<std::mutex> lock (m_Mutex);
			m_File = file;
			m_Bytes.assign (bytes.begin (), bytes.end ());
			m_Pending.store (true, std::memory_order_release);
			return true;
		}

		/** Retry the retained write once. True when nothing is pending. */
		bool Flush ()
		{
			if (!m_Pending.load (std::memory_order_acquire)) return true;
			std::lock_guard<std::mutex> lock (m_Mutex);
			if (!m_Pending.load (std::memory_order_relaxed)) return true;
			if (!i2p::data::WriteRouterPersistentFile (m_File, m_Bytes)) return false;
			std::fill (m_Bytes.begin (), m_Bytes.end (), 0);
			m_Bytes.clear ();
			m_Pending.store (false, std::memory_order_release);
			return true;
		}

		/**
		 * Drop the retained record without writing. Used when initialization is
		 * rolled back: no later tick may publish key material from a failed
		 * start attempt.
		 */
		void Discard ()
		{
			std::lock_guard<std::mutex> lock (m_Mutex);
			std::fill (m_Bytes.begin (), m_Bytes.end (), 0);
			m_Bytes.clear ();
			m_Pending.store (false, std::memory_order_release);
		}

	private:
		std::mutex m_Mutex;
		std::atomic<bool> m_Pending { false };
		i2p::data::RouterPersistentFile m_File = i2p::data::RouterPersistentFile::RouterKeys;
		std::vector<uint8_t> m_Bytes;
	};

	PendingFixedWrite & RetainedFixedWrite ()
	{
		static PendingFixedWrite slot;
		return slot;
	}

	/**
	 * Write one fixed router file through the installed persistence authority.
	 * A failure is retained for one later retry; it never becomes a local
	 * write in the network process.
	 */
	bool WriteRetainedFixedFile (i2p::data::RouterPersistentFile file, std::span<const uint8_t> bytes)
	{
		if (i2p::data::WriteRouterPersistentFile (file, bytes)) return true;
		RetainedFixedWrite ().Store (file, bytes);
		// The isolated writer may simply have been restarting with a bumped
		// generation, so one immediate retry is meaningful. A second failure
		// is left to the next save tick instead of spinning here.
		return RetainedFixedWrite ().Flush ();
	}
}
RouterContext context;

namespace
{
	// Origin of the garlic currently being decrypted on this thread.
	// RouterContext::ProcessGarlicMessage() posts to the router service and
	// PostGarlicMessage() decrypts synchronously there, so every clove
	// callback of that garlic runs on the same thread before the enclosing
	// call returns. A nested garlic is posted again, never decrypted inline,
	// so the value cannot leak into an unrelated garlic. Java makes the same
	// distinction between HandleGarlicMessageJob and InboundMessageDistributor.
	thread_local bool g_IncomingGarlicFromTunnel = false;
}

RouterContext::RouterContext ()
	: m_LastUpdateTime (0)
	, m_AcceptsTunnels (true)
	, m_IsFloodfill (false)
	, m_StartupTime (0)
	, m_BandwidthLimit (0)
	, m_ShareRatio (100)
	, m_Status (eRouterStatusUnknown)
	, m_StatusV6 (eRouterStatusUnknown)
	, m_Error (eRouterErrorNone)
	, m_ErrorV6 (eRouterErrorNone)
	, m_Testing (false)
	, m_TestingV6 (false)
	, m_NetID (I2PD_NET_ID)
	, m_PublishReplyToken (0)
	, m_IsHiddenMode (false)
{
}

RouterContext::~RouterContext ()
{
	Stop ();
	// Wait for the asynchronous router.info writer outside all RouterInfo
	// locks. A discarded std::async future would block during static
	// destruction without a defined order relative to the logger.
	if (m_SavingRouterInfo.valid ())
	{
		try
		{
			m_SavingRouterInfo.get ();
		}
		catch (const std::exception & ex)
		{
			LogPrint (eLogError, "Router: router.info save failed: ", ex.what ());
		}
	}
}

void RouterContext::Init ()
{
	// Randomness comes from the CSPRNG-backed helpers; no process-global seed.
	m_StartupTime.store (i2p::util::GetMonotonicSeconds (), std::memory_order_relaxed);

	if (!Load ()) CreateNewRouter ();
	m_Decryptor = m_Keys.CreateDecryptor (nullptr);
	m_TunnelDecryptor = m_Keys.CreateDecryptor (nullptr);
	UpdateRouterInfo ();
	i2p::crypto::InitNoiseNState (m_InitialNoiseState, GetIdentity ()->GetEncryptionPublicKey ());
	m_ECIESSession = std::make_shared<i2p::garlic::RouterIncomingRatchetSession> (m_InitialNoiseState);
}

void RouterContext::ResetAfterFailedInit ()
{
	// Init() may have submitted an asynchronous router.info write before a
	// later phase failed. Join it before Daemon rollback changes or stops the
	// persistence writer and its storage authority.
	if (m_SavingRouterInfo.valid ())
	{
		try
		{
			m_SavingRouterInfo.get ();
		}
		catch (const std::exception & ex)
		{
			LogPrint (eLogError, "Router: failed-init router.info save failed: ", ex.what ());
		}
		catch (...)
		{
			LogPrint (eLogError, "Router: failed-init router.info save failed with an unknown exception");
		}
	}
	{
		std::lock_guard<std::mutex> l (m_SaveBufferMutex);
		m_SaveBuffer.reset ();
	}
	m_IsSaving.clear ();
	// A rolled-back initialization must not retry identity or transport key
	// material from the failed attempt on a later tick.
	RetainedFixedWrite ().Discard ();
	Stop ();
	m_ECIESSession.reset ();
	m_Decryptor.reset ();
	m_TunnelDecryptor.reset ();
	m_InitialNoiseState = i2p::crypto::NoiseSymmetricState { };
	// Do not discard persisted identity or transport keys. Init may have
	// generated them successfully before a later phase failed; these durable
	// files remain authoritative for a retry against the same data directory.
}

void RouterContext::Start ()
{
	std::lock_guard<std::mutex> lifecycle (m_ServiceLifecycleMutex);
	if (m_Service.Load ()) return;
	auto service = std::make_shared<RouterService> ();
	m_PublishTimer.reset (new boost::asio::steady_timer (service->GetService ()));
	m_CongestionUpdateTimer.reset (new boost::asio::steady_timer (service->GetService ()));
	m_CleanupTimer.reset (new boost::asio::steady_timer (service->GetService ()));
	// Publish the service before scheduling so timer handlers that run once
	// the worker starts observe a consistent object.
	m_Service.Store (service);
	ScheduleInitialPublish ();
	ScheduleCongestionUpdate ();
	ScheduleCleanupTimer ();
	service->Start ();
}

void RouterContext::Stop ()
{
	std::lock_guard<std::mutex> lifecycle (m_ServiceLifecycleMutex);
	auto service = m_Service.Exchange (std::shared_ptr<RouterService> { });
	if (!service)
	{
		// A partially initialised context may still own timers.
		m_PublishTimer.reset ();
		m_CongestionUpdateTimer.reset ();
		m_CleanupTimer.reset ();
		return;
	}
	// StopIOService() is the documented cross-thread-safe operation and
	// joins the router thread. Cancel the timers only afterwards, when no
	// handler can be using them concurrently.
	service->Stop ();
	if (m_PublishTimer) m_PublishTimer->cancel ();
	if (m_CongestionUpdateTimer) m_CongestionUpdateTimer->cancel ();
	if (m_CleanupTimer) m_CleanupTimer->cancel ();
	CleanUp (); // GarlicDestination
	// Release the stopped service and its timers. Start() can then create
	// a fresh io_context for a later start/stop cycle instead of silently
	// keeping a permanently stopped service. Posters hold their own
	// shared_ptr snapshot, so an in-flight post cannot outlive the object.
	m_PublishTimer.reset ();
	m_CongestionUpdateTimer.reset ();
	m_CleanupTimer.reset ();
}

std::shared_ptr<i2p::data::RouterInfo::Buffer> RouterContext::CopyRouterInfoBuffer () const
{
	std::lock_guard<std::mutex> l (m_RouterInfoMutex);
	return m_RouterInfo.CopyBuffer ();
}

std::shared_ptr<const i2p::data::RouterInfo> RouterContext::GetRouterInfoSnapshot () const
{
	auto buffer = CopyRouterInfoBuffer ();
	if (!buffer) return nullptr;
	return std::make_shared<i2p::data::RouterInfo> (buffer->data (), buffer->GetBufferLen ());
}

void RouterContext::CreateNewRouter ()
{
	m_Keys = i2p::data::PrivateKeys::CreateRandomKeys (
		i2p::data::SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519, i2p::data::CRYPTO_KEY_TYPE_ECIES_X25519_AEAD);
	SaveKeys ();
	NewRouterInfo ();
}

void RouterContext::NewRouterInfo ()
{
	i2p::data::LocalRouterInfo routerInfo;
	routerInfo.SetRouterIdentity (GetIdentity ());
	uint16_t port;
	i2p::config::GetOption ("port", port);
	if (!port) port = SelectRandomPort ();
	bool ipv4;
	i2p::config::GetOption ("ipv4", ipv4);
	bool ipv6;
	i2p::config::GetOption ("ipv6", ipv6);
	bool ntcp2;
	i2p::config::GetOption ("ntcp2.enabled", ntcp2);
	bool ssu2;
	i2p::config::GetOption ("ssu2.enabled", ssu2);
	bool ygg;
	i2p::config::GetOption ("meshnets.yggdrasil", ygg);
	bool nat;
	i2p::config::GetOption ("nat", nat);

	if ((ntcp2 || ygg) && !m_NTCP2Keys) NewNTCP2Keys ();
	if (ssu2 && !m_SSU2Keys) NewSSU2Keys ();
	bool ntcp2Published = false;
	if (ntcp2)
	{
		i2p::config::GetOption ("ntcp2.published", ntcp2Published);
		if (ntcp2Published)
		{
			std::string ntcp2proxy;
			i2p::config::GetOption ("ntcp2.proxy", ntcp2proxy);
			if (!ntcp2proxy.empty ()) ntcp2Published = false;
		}
	}
	bool ssu2Published = false;
	if (ssu2) i2p::config::GetOption ("ssu2.published", ssu2Published);
	uint8_t caps = 0;
	if (ipv4)
	{
		std::string host;
		if (!nat)
			// we have no NAT so set external address from local address
			i2p::config::GetOption ("address4", host);
		if (host.empty ()) i2p::config::GetOption ("host", host);

		if (ntcp2)
		{
			uint16_t ntcp2Port;
			i2p::config::GetOption ("ntcp2.port", ntcp2Port);
			if (!ntcp2Port) ntcp2Port = port;
			if (ntcp2Published && ntcp2Port)
			{
				boost::asio::ip::address addr;
				if (!host.empty ()) addr = boost::asio::ip::make_address (host);
				if (!addr.is_v4 ()) addr = boost::asio::ip::address_v4 ();
				routerInfo.AddNTCP2Address (m_NTCP2Keys->staticPublicKey, m_NTCP2Keys->iv, addr, ntcp2Port);
			}
			else
			{
				// add non-published NTCP2 address
				uint8_t addressCaps = i2p::data::RouterInfo::AddressCaps::eV4;
				if (ipv6) addressCaps |= i2p::data::RouterInfo::AddressCaps::eV6;
				routerInfo.AddNTCP2Address (m_NTCP2Keys->staticPublicKey, m_NTCP2Keys->iv, ntcp2Port, addressCaps);
			}
		}
		if (ssu2)
		{
			uint16_t ssu2Port;
			i2p::config::GetOption ("ssu2.port", ssu2Port);
			if (!ssu2Port) ssu2Port = port;
			if (ssu2Published && ssu2Port)
			{
				boost::asio::ip::address addr;
				if (!host.empty ()) addr = boost::asio::ip::make_address (host);
				if (!addr.is_v4 ()) addr = boost::asio::ip::address_v4 ();
				routerInfo.AddSSU2Address (m_SSU2Keys->staticPublicKey, m_SSU2Keys->intro, addr, ssu2Port);
			}
			else
			{
				uint8_t addressCaps = i2p::data::RouterInfo::AddressCaps::eV4;
				if (ipv6) addressCaps |= i2p::data::RouterInfo::AddressCaps::eV6;
				routerInfo.AddSSU2Address (m_SSU2Keys->staticPublicKey, m_SSU2Keys->intro, ssu2Port, addressCaps);
			}
		}
	}
	if (ipv6)
	{
		auto ipv6addr = i2p::util::net::GetClearnetIPV6Address ();
		if (!ipv6addr.is_unspecified ())
		{
			std::string host;
			i2p::config::GetOption ("address6", host);
			if (host.empty () && !ipv4)
				i2p::config::GetOption ("host", host); // use host for ipv6 only if ipv4 is not presented

			if (ntcp2)
			{
				uint16_t ntcp2Port;
				i2p::config::GetOption ("ntcp2.port", ntcp2Port);
				if (!ntcp2Port) ntcp2Port = port;
				if (ntcp2Published && ntcp2Port)
				{
					std::string ntcp2Host;
					if (!i2p::config::IsDefault ("ntcp2.addressv6"))
						i2p::config::GetOption ("ntcp2.addressv6", ntcp2Host);
					else
						ntcp2Host = host;
					boost::asio::ip::address addr;
					if (!ntcp2Host.empty ()) addr = boost::asio::ip::make_address (ntcp2Host);
					if (!addr.is_v6 ()) addr = boost::asio::ip::address_v6 ();
					routerInfo.AddNTCP2Address (m_NTCP2Keys->staticPublicKey, m_NTCP2Keys->iv, addr, ntcp2Port);
				}
				else
				{
					if (!ipv4) // no other ntcp2 addresses yet
						routerInfo.AddNTCP2Address (m_NTCP2Keys->staticPublicKey, m_NTCP2Keys->iv, ntcp2Port,
							i2p::data::RouterInfo::AddressCaps::eV6);
				}
			}
			if (ssu2)
			{
				uint16_t ssu2Port;
				i2p::config::GetOption ("ssu2.port", ssu2Port);
				if (!ssu2Port) ssu2Port = port;
				if (ssu2Published && ssu2Port)
				{
					boost::asio::ip::address addr;
					if (!host.empty ()) addr = boost::asio::ip::make_address (host);
					if (!addr.is_v6 ()) addr = boost::asio::ip::address_v6 ();
					routerInfo.AddSSU2Address (m_SSU2Keys->staticPublicKey, m_SSU2Keys->intro, addr, ssu2Port);
				}
				else
				{
					if (!ipv4) // no other ssu2 addresses yet
						routerInfo.AddSSU2Address (m_SSU2Keys->staticPublicKey, m_SSU2Keys->intro, ssu2Port,
							i2p::data::RouterInfo::AddressCaps::eV6);
				}
			}
		}
	}
	if (ygg)
	{
		auto yggaddr = i2p::util::net::GetYggdrasilAddress ();
		if (!yggaddr.is_unspecified ())
			routerInfo.AddNTCP2Address (m_NTCP2Keys->staticPublicKey, m_NTCP2Keys->iv, yggaddr, port);
	}

	routerInfo.UpdateCaps (caps); // caps + L
	routerInfo.SetProperty ("netId", std::to_string (m_NetID.load (std::memory_order_relaxed)));
	routerInfo.SetProperty ("router.version", I2P_VERSION);
	routerInfo.CreateBuffer (m_Keys);
	{
		std::lock_guard<std::mutex> l (m_RouterInfoMutex);
		m_RouterInfo.SetRouterIdentity (GetIdentity ());
		m_RouterInfo.Update (routerInfo.GetBuffer (), routerInfo.GetBufferLen ());
		m_RouterInfo.SetUnreachable (false);
	}
}

uint16_t RouterContext::SelectRandomPort () const
{
	uint16_t port;
	do
	{
		port = static_cast<uint16_t> (i2p::crypto::RandomBelow (30777 - 9111) + 9111); // I2P network ports range
	} while (i2p::util::net::IsPortInReservedRange (port));

	return port;
}

void RouterContext::UpdateRouterInfoLocked ()
{
	m_RouterInfo.CreateBuffer (m_Keys);
	auto buffer = m_RouterInfo.CopyBuffer ();
	{
		// Empty the save slot and hand the latest snapshot to the writer. The
		// lock order is m_RouterInfoMutex -> m_SaveBufferMutex; the save worker
		// never takes m_RouterInfoMutex, so this cannot deadlock.
		std::lock_guard<std::mutex> l (m_SaveBufferMutex);
		m_SaveBuffer = buffer;
	}
	bool isSaving = m_IsSaving.test_and_set ();
	if (!isSaving) // try to save only if not being saved
	{
		// Never wait for disk I/O while m_RouterInfoMutex is held. A worker
		// that has already cleared m_IsSaving but whose future is not marked
		// ready yet leaves the newest buffer in m_SaveBuffer; the next update
		// (or management pass) will start a writer for it.
		const bool previousReady = !m_SavingRouterInfo.valid ()
			|| m_SavingRouterInfo.wait_for (std::chrono::seconds (0)) == std::future_status::ready;
		if (previousReady)
		{
			if (m_SavingRouterInfo.valid ())
			{
				try
				{
					m_SavingRouterInfo.get ();
				}
				catch (const std::exception & ex)
				{
					LogPrint (eLogError, "Router: router.info save failed: ", ex.what ());
				}
			}
			m_SavingRouterInfo = std::async (std::launch::async,
				[this] ()
				{
					while (true)
					{
						// A retained fixed-file write (router.keys/ntcp2.keys/
						// ssu2.keys) is retried on the same tick, outside every
						// context lock. Failure keeps it pending for a later
						// tick; it never becomes a local write.
						(void)RetainedFixedWrite ().Flush ();
						std::shared_ptr<i2p::data::RouterInfo::Buffer> buffer;
						{
							std::lock_guard<std::mutex> l (m_SaveBufferMutex);
							buffer = m_SaveBuffer;
							m_SaveBuffer = nullptr;
						}
						if (!buffer) break;
						if (i2p::data::WriteRouterPersistentFile (i2p::data::RouterPersistentFile::RouterInfo,
								std::span<const uint8_t> (buffer->data (), buffer->GetBufferLen ())))
							continue;
						{
							// The newest snapshot must survive a failed write:
							// dropping it would silently persist an older
							// address/port set that the next start would use.
							// Exactly one buffer is retained (a newer update
							// supersedes it) and this writer stops instead of
							// spinning; the next update tick retries.
							std::lock_guard<std::mutex> l (m_SaveBufferMutex);
							if (!m_SaveBuffer) m_SaveBuffer = std::move (buffer);
						}
						LogPrint (eLogError,
							"Router: isolated router.info persistence failed; retaining the newest snapshot for retry");
						break;
					}
					m_IsSaving.clear ();
				});
		}
		else
			m_IsSaving.clear (); // no writer was started; retry on the next update
	}
	m_LastUpdateTime.store (i2p::util::GetSecondsSinceEpoch (), std::memory_order_relaxed);
}

void RouterContext::UpdateRouterInfo ()
{
	std::lock_guard<std::mutex> l (m_RouterInfoMutex);
	UpdateRouterInfoLocked ();
}

void RouterContext::UpdateCapsLocked (uint8_t caps)
{
	m_RouterInfo.UpdateCaps (caps);
}

void RouterContext::UpdateCaps (uint8_t caps)
{
	std::lock_guard<std::mutex> l (m_RouterInfoMutex);
	UpdateCapsLocked (caps);
}

void RouterContext::NewNTCP2Keys ()
{
	std::lock_guard<std::mutex> l (m_StaticKeysMutex);
	if (m_NTCP2Keys) return; // idempotent; never reset loaded keys
	auto staticKeys = std::make_unique<i2p::crypto::X25519Keys> ();
	if (!staticKeys->GenerateKeys ())
		throw std::runtime_error ("Router: NTCP2 static key generation failed");
	auto keys = std::make_unique<NTCP2PrivateKeys> ();
	staticKeys->GetPrivateKey (keys->staticPrivateKey);
	memcpy (keys->staticPublicKey, staticKeys->GetPublicKey (), 32);
	RAND_bytes (keys->iv, 16);
	// save
	if (!WriteRetainedFixedFile (i2p::data::RouterPersistentFile::NTCP2Keys,
			std::span<const uint8_t> (reinterpret_cast<const uint8_t *> (keys.get ()), sizeof (NTCP2PrivateKeys))))
		throw std::runtime_error ("Router: NTCP2 key persistence failed");
	m_NTCP2StaticKeys = std::move (staticKeys);
	m_NTCP2Keys = std::move (keys);
}

void RouterContext::NewSSU2Keys ()
{
	std::lock_guard<std::mutex> l (m_StaticKeysMutex);
	if (m_SSU2Keys) return; // idempotent; never reset loaded keys
	auto staticKeys = std::make_unique<i2p::crypto::X25519Keys> ();
	if (!staticKeys->GenerateKeys ())
		throw std::runtime_error ("Router: SSU2 static key generation failed");
	auto keys = std::make_unique<SSU2PrivateKeys> ();
	staticKeys->GetPrivateKey (keys->staticPrivateKey);
	memcpy (keys->staticPublicKey, staticKeys->GetPublicKey (), 32);
	RAND_bytes (keys->intro, 32);
	// save
	if (!WriteRetainedFixedFile (i2p::data::RouterPersistentFile::SSU2Keys,
			std::span<const uint8_t> (reinterpret_cast<const uint8_t *> (keys.get ()), sizeof (SSU2PrivateKeys))))
		throw std::runtime_error ("Router: SSU2 key persistence failed");
	m_SSU2StaticKeys = std::move (staticKeys);
	m_SSU2Keys = std::move (keys);
}

void RouterContext::SetTesting (bool testing)
{
	if (m_Testing.exchange (testing, std::memory_order_relaxed) != testing && testing)
		m_Error.store (eRouterErrorNone, std::memory_order_relaxed);
}

void RouterContext::SetTestingV6 (bool testing)
{
	if (m_TestingV6.exchange (testing, std::memory_order_relaxed) != testing && testing)
		m_ErrorV6.store (eRouterErrorNone, std::memory_order_relaxed);
}

void RouterContext::SetStatus (RouterStatus status)
{
	SetTesting (false);
	auto previous = m_Status.exchange (status, std::memory_order_relaxed);
	if (previous != status)
	{
		LogPrint (eLogInfo, "Router: network status v4 changed ", ROUTER_STATUS_NAMES[previous], " -> ",
			ROUTER_STATUS_NAMES[status]);
		switch (status)
		{
		case eRouterStatusOK:
			SetReachable (true, false); // ipv4
			break;
		case eRouterStatusFirewalled:
			SetUnreachable (true, false); // ipv4
			break;
		case eRouterStatusMesh:
		{
			std::lock_guard<std::mutex> l (m_RouterInfoMutex);
			UpdateCapsLocked (m_RouterInfo.GetCaps () | i2p::data::RouterInfo::eReachable);
			break;
		}
		case eRouterStatusProxy:
		case eRouterStatusStan:
		{
			std::lock_guard<std::mutex> l (m_RouterInfoMutex);
			UpdateCapsLocked (
				(m_RouterInfo.GetCaps () | i2p::data::RouterInfo::eUnreachable) & ~i2p::data::RouterInfo::eReachable);
			break;
		}
		default:;
		}
	}
}

void RouterContext::SetStatusV6 (RouterStatus status)
{
	SetTestingV6 (false);
	auto previous = m_StatusV6.exchange (status, std::memory_order_relaxed);
	if (previous != status)
	{
		LogPrint (eLogInfo, "Router: network status v6 changed ", ROUTER_STATUS_NAMES[previous], " -> ",
			ROUTER_STATUS_NAMES[status]);
		switch (status)
		{
		case eRouterStatusOK:
			SetReachable (false, true); // ipv6
			break;
		case eRouterStatusFirewalled:
			SetUnreachable (false, true); // ipv6
			break;
		default:;
		}
	}
}

void RouterContext::UpdatePort (int port)
{
	std::lock_guard<std::mutex> l (m_RouterInfoMutex);
	auto addresses = m_RouterInfo.GetAddresses ();
	if (!addresses) return;
	bool updated = false;
	for (auto & address : *addresses)
	{
		if (address && address->port != port)
		{
			address->port = port;
			updated = true;
		}
	}
	if (updated) UpdateRouterInfoLocked ();
}

void RouterContext::PublishNTCP2Address (
	std::shared_ptr<i2p::data::RouterInfo::Address> address, int port, bool publish, int version) const
{
	if (!address) return;
	if (!port && !address->port) port = SelectRandomPort ();
	if (port) address->port = port;
	address->published = publish;
	address->v = version;
	memcpy (address->i, m_NTCP2Keys->iv, 16);
}

bool RouterContext::PublishNTCP2AddressLocked (int port, bool publish, bool v4, bool v6, bool ygg, int version)
{
	if (!m_NTCP2Keys) return false;
	auto addresses = m_RouterInfo.GetAddresses ();
	if (!addresses) return false;
	bool updated = false;
	if (v4)
	{
		auto addr = (*addresses)[i2p::data::RouterInfo::eNTCP2V4Idx];
		if (addr && (addr->port != port || addr->published != publish || addr->v != version))
		{
			PublishNTCP2Address (addr, port, publish, version);
			updated = true;
		}
	}
	if (v6)
	{
		auto addr = (*addresses)[i2p::data::RouterInfo::eNTCP2V6Idx];
		if (addr && (addr->port != port || addr->published != publish || addr->v != version))
		{
			PublishNTCP2Address (addr, port, publish, version);
			updated = true;
		}
	}
	if (ygg)
	{
		auto addr = (*addresses)[i2p::data::RouterInfo::eNTCP2V6MeshIdx];
		if (addr && (addr->port != port || addr->published != publish))
		{
			PublishNTCP2Address (addr, port, publish, 2);
			updated = true;
		}
	}

	return updated;
}

void RouterContext::PublishNTCP2Address (int port, bool publish, bool v4, bool v6, bool ygg, int version)
{
	std::lock_guard<std::mutex> l (m_RouterInfoMutex);
	if (PublishNTCP2AddressLocked (port, publish, v4, v6, ygg, version)) UpdateRouterInfoLocked ();
}

void RouterContext::UpdateNTCP2Keys ()
{
	if (!m_NTCP2Keys) return;
	std::lock_guard<std::mutex> l (m_RouterInfoMutex);
	auto addresses = m_RouterInfo.GetAddresses ();
	if (!addresses) return;
	for (auto & it : *addresses)
	{
		if (it && it->IsNTCP2 ())
		{
			it->s = m_NTCP2Keys->staticPublicKey;
			memcpy (it->i, m_NTCP2Keys->iv, 16);
		}
	}
}

void RouterContext::PublishSSU2Address (int port, bool publish, bool v4, bool v6, int version)
{
	if (!m_SSU2Keys) return;
	std::lock_guard<std::mutex> l (m_RouterInfoMutex);
	auto addresses = m_RouterInfo.GetAddresses ();
	if (!addresses) return;
	int newPort = 0;
	if (!port)
	{
		for (const auto & address : *addresses)
			if (address && address->port)
			{
				newPort = address->port;
				break;
			}
		if (!newPort) newPort = SelectRandomPort ();
	}
	bool updated = false;
	const bool wantIntroducer = publish && !m_IsFloodfill.load (std::memory_order_relaxed);
	for (auto & address : *addresses)
	{
		if (address && address->IsSSU2 ()
			&& (!address->port || address->port != port || address->published != publish
				|| address->v != version
				// A loaded RouterInfo may already have the right port/published/
				// version but lack the current B/C caps (older builds did not
				// publish B on non-published addresses). Refresh them too.
				|| (publish && (!address->IsPeerTesting () || address->IsIntroducer () != wantIntroducer))
				|| (!publish && address->ssu && address->ssu->HasIntroducers ()))
			&& ((v4 && address->IsV4 ()) || (v6 && address->IsV6 ())))
		{
			if (port)
				address->port = port;
			else if (!address->port)
				address->port = newPort;
			address->published = publish;
			address->v = version;
			if (publish)
			{
				UpdateSSU2AddressCapsTesting (address, true);
				UpdateSSU2AddressCapsIntroducer (address, !m_IsFloodfill.load (std::memory_order_relaxed));
			}
			else
			{
				UpdateSSU2AddressCapsTesting (address, false);
				UpdateSSU2AddressCapsIntroducer (address, false);
				// Unpublishing must also drop introducers. Otherwise
				// IsReachableSSU() would still report the address as reachable
				// through stale introducers even though no host/port is
				// serialized.
				if (address->ssu)
				{
					std::lock_guard<std::mutex> lock (address->ssu->mutex);
					address->ssu->introducers.clear ();
				}
			}
			updated = true;
		}
	}
	if (updated)
	{
		// Keep supported/reachable transports in sync with the per-address
		// state changed above (SetReachable()/SetUnreachable() do the same).
		m_RouterInfo.UpdateSupportedTransports ();
		UpdateRouterInfoLocked ();
	}
}

void RouterContext::UpdateSSU2AddressCapsIntroducer (
	std::shared_ptr<i2p::data::RouterInfo::Address> address, bool isIntroducer) const
{
	// Caller holds m_RouterInfoMutex (or owns the address during setup).
	if (address)
	{
		if (isIntroducer)
			address->caps |= i2p::data::RouterInfo::eSSUIntroducer;
		else
			address->caps &= ~i2p::data::RouterInfo::eSSUIntroducer;
	}
}

void RouterContext::UpdateSSU2AddressCapsTesting (
	std::shared_ptr<i2p::data::RouterInfo::Address> address, bool isTesting) const
{
	// Caller holds m_RouterInfoMutex (or owns the address during setup).
	if (address)
	{
		if (isTesting)
			address->caps |= i2p::data::RouterInfo::eSSUTesting;
		else
			address->caps &= ~i2p::data::RouterInfo::eSSUTesting;
	}
}

void RouterContext::UpdateSSU2Keys ()
{
	if (!m_SSU2Keys) return;
	std::lock_guard<std::mutex> l (m_RouterInfoMutex);
	auto addresses = m_RouterInfo.GetAddresses ();
	if (!addresses) return;
	for (auto & it : *addresses)
	{
		if (it && it->IsSSU2 ())
		{
			it->s = m_SSU2Keys->staticPublicKey;
			it->i = m_SSU2Keys->intro;
		}
	}
}

void RouterContext::UpdateAddress (const boost::asio::ip::address & host)
{
	std::lock_guard<std::mutex> l (m_RouterInfoMutex);
	auto addresses = m_RouterInfo.GetAddresses ();
	if (!addresses) return;
	bool updated = false;
	if (host.is_v4 ())
	{
		auto addr = (*addresses)[i2p::data::RouterInfo::eNTCP2V4Idx];
		if (addr && addr->host != host)
		{
			addr->host = host;
			updated = true;
		}
		addr = (*addresses)[i2p::data::RouterInfo::eSSU2V4Idx];
		if (addr && addr->ssu && addr->host != host)
		{
			addr->host = host;
			// Same policy as the IPv6 path below: only probe the interface MTU
			// for a local address, so a NAT external address (which is not in
			// getifaddrs()) cannot fall back to the Unix 620-byte default and
			// make the published SSU2 address unusable. Honour ssu2.mtu4 and
			// skip proxy mode. Probe only when the host changed, not on every
			// Address block, to avoid re-signing our RouterInfo continuously.
			uint16_t configuredMTU;
			i2p::config::GetOption ("ssu2.mtu4", configuredMTU);
			if (!configuredMTU && m_Status.load (std::memory_order_relaxed) != eRouterStatusProxy
				&& i2p::util::net::IsLocalAddress (host))
			{
				auto mtu = i2p::util::net::GetMTU (host);
				if (mtu < 1280)
					mtu = 1280; // SSU2 minimum (SSU2 spec, MTU)
				else if (mtu > 1500)
					mtu = 1500;
				LogPrint (eLogDebug, "Router: Our v4 MTU=", mtu);
				addr->ssu->mtu = mtu;
			}
			updated = true;
		}
	}
	else if (host.is_v6 ())
	{
		auto addr = (*addresses)[i2p::data::RouterInfo::eNTCP2V6Idx];
		if (addr && addr->host != host)
		{
			addr->host = host;
			updated = true;
		}
		addr = (*addresses)[i2p::data::RouterInfo::eSSU2V6Idx];
		if (addr && addr->ssu && addr->host != host)
		{
			addr->host = host;
			const auto statusV6 = m_StatusV6.load (std::memory_order_relaxed);
			// The address came from an Address block/peer test, i.e. it is the
			// peer-observed external address. Only query the interface MTU when
			// the address is actually local: getifaddrs() falls back to 1280
			// for NAT66/privacy/tunnel addresses, which would wrongly limit the
			// published ML-KEM preference. Honour an explicitly configured MTU
			// on every platform, not only OpenBSD, and skip proxy mode where
			// the SOCKS5 encapsulation already caps the packet size.
			uint16_t configuredMTU;
			i2p::config::GetOption ("ssu2.mtu6", configuredMTU);
			if (!configuredMTU && statusV6 != eRouterStatusProxy && i2p::util::net::IsLocalAddress (host))
			{
				// update MTU
				auto mtu = i2p::util::net::GetMTU (host);
				if (mtu)
				{
					LogPrint (eLogDebug, "Router: Our v6 MTU=", mtu);
					if (mtu < (int)i2p::transport::SSU2_MIN_PACKET_SIZE)
						mtu = i2p::transport::SSU2_MIN_PACKET_SIZE; // SSU2 minimum (SSU2 spec, MTU)
					int maxMTU = i2p::util::net::GetMaxMTU (host.to_v6 ());
					if (mtu > maxMTU)
					{
						mtu = maxMTU;
						LogPrint (eLogWarning, "Router: MTU dropped to upper limit of ", maxMTU, " bytes");
					}
					// Java caps published IPv6 SSU2 MTU at 1488: IPv6 routers do
					// not fragment, so advertising 1500 on a 1480/1488 path would
					// drop our largest datagrams.
					if (mtu > (int)i2p::transport::SSU2_MAX_IPV6_MTU)
						mtu = i2p::transport::SSU2_MAX_IPV6_MTU;
					addr->ssu->mtu = mtu;
				}
			}
			updated = true;
		}
	}

	auto ts = i2p::util::GetSecondsSinceEpoch ();
	if (updated || ts > m_LastUpdateTime.load (std::memory_order_relaxed) + ROUTER_INFO_UPDATE_INTERVAL)
		UpdateRouterInfoLocked ();
}

bool RouterContext::AddSSU2Introducer (const i2p::data::RouterInfo::Introducer & introducer, bool v4)
{
	std::lock_guard<std::mutex> l (m_RouterInfoMutex);
	bool ret = m_RouterInfo.AddSSU2Introducer (introducer, v4);
	if (ret) UpdateRouterInfoLocked ();
	return ret;
}

void RouterContext::RemoveSSU2Introducer (const i2p::data::IdentHash & h, bool v4)
{
	std::lock_guard<std::mutex> l (m_RouterInfoMutex);
	if (m_RouterInfo.RemoveSSU2Introducer (h, v4)) UpdateRouterInfoLocked ();
}

void RouterContext::UpdateSSU2Introducer (const i2p::data::IdentHash & h, bool v4, uint32_t iTag, uint32_t iExp)
{
	std::lock_guard<std::mutex> l (m_RouterInfoMutex);
	if (m_RouterInfo.UpdateSSU2Introducer (h, v4, iTag, iExp)) UpdateRouterInfoLocked ();
}

void RouterContext::ClearSSU2Introducers (bool v4)
{
	std::lock_guard<std::mutex> l (m_RouterInfoMutex);
	auto addr = m_RouterInfo.GetSSU2Address (v4);
	if (addr && addr->ssu)
	{
		bool cleared = false;
		{
			std::lock_guard<std::mutex> lock (addr->ssu->mutex);
			cleared = !addr->ssu->introducers.empty ();
			addr->ssu->introducers.clear ();
		}
		if (cleared) UpdateRouterInfoLocked ();
	}
}

void RouterContext::SetFloodfill (bool floodfill)
{
	if (m_IsFloodfill.exchange (floodfill, std::memory_order_relaxed) != floodfill)
	{
		std::lock_guard<std::mutex> l (m_RouterInfoMutex);
		auto addresses = m_RouterInfo.GetAddresses ();
		if (floodfill)
		{
			m_RouterInfo.UpdateFloodfillProperty (true);
			if (addresses)
			{
				// disable introducer for all floodfill's SSU2 addresses
				UpdateSSU2AddressCapsIntroducer ((*addresses)[i2p::data::RouterInfo::eSSU2V4Idx], false);
				UpdateSSU2AddressCapsIntroducer ((*addresses)[i2p::data::RouterInfo::eSSU2V6Idx], false);
			}
		}
		else
		{
			m_RouterInfo.UpdateFloodfillProperty (false);
			// we don't publish number of routers and leaseset for non-floodfill
			m_RouterInfo.DeleteProperty (i2p::data::ROUTER_INFO_PROPERTY_LEASESETS);
			m_RouterInfo.DeleteProperty (i2p::data::ROUTER_INFO_PROPERTY_ROUTERS);
			if (addresses)
			{
				// enable introducers for published non-floodfill's SSU2 addresses
				auto addr = (*addresses)[i2p::data::RouterInfo::eSSU2V4Idx];
				if (addr && addr->published) UpdateSSU2AddressCapsIntroducer (addr, true);
				addr = (*addresses)[i2p::data::RouterInfo::eSSU2V6Idx];
				if (addr && addr->published) UpdateSSU2AddressCapsIntroducer (addr, true);
			}
		}
		UpdateRouterInfoLocked ();
	}
}

std::string RouterContext::GetFamily () const
{
	std::lock_guard<std::mutex> l (m_RouterInfoMutex);
	return m_RouterInfo.GetProperty (i2p::data::ROUTER_INFO_PROPERTY_FAMILY);
}

std::string RouterContext::GetRouterInfoProperty (const std::string & key) const
{
	std::lock_guard<std::mutex> l (m_RouterInfoMutex);
	return m_RouterInfo.GetProperty (key);
}

void RouterContext::SetFamily (const std::string & family)
{
	std::string signature;
	if (family.length () > 0) signature = i2p::data::CreateFamilySignature (family, GetIdentHash ());
	std::lock_guard<std::mutex> l (m_RouterInfoMutex);
	if (signature.length () > 0)
	{
		m_RouterInfo.SetProperty (i2p::data::ROUTER_INFO_PROPERTY_FAMILY, family);
		m_RouterInfo.SetProperty (i2p::data::ROUTER_INFO_PROPERTY_FAMILY_SIG, signature);
	}
	else
	{
		m_RouterInfo.DeleteProperty (i2p::data::ROUTER_INFO_PROPERTY_FAMILY);
		m_RouterInfo.DeleteProperty (i2p::data::ROUTER_INFO_PROPERTY_FAMILY_SIG);
	}
}

void RouterContext::SetBandwidth (char L)
{
	uint32_t limit = 0;
	enum
	{
		low,
		high,
		extra,
		unlim
	} type = high;
	/* detect parameters */
	switch (L)
	{
	case i2p::data::CAPS_FLAG_LOW_BANDWIDTH1:
		limit = 12;
		type = low;
		break;
	case i2p::data::CAPS_FLAG_LOW_BANDWIDTH2:
		limit = i2p::data::LOW_BANDWIDTH_LIMIT;
		type = low;
		break; // 48
	case i2p::data::CAPS_FLAG_LOW_BANDWIDTH3:
		limit = 64;
		type = low;
		break;
	case i2p::data::CAPS_FLAG_LOW_BANDWIDTH4:
		limit = 128;
		type = low;
		break;
	case i2p::data::CAPS_FLAG_HIGH_BANDWIDTH:
		limit = i2p::data::HIGH_BANDWIDTH_LIMIT;
		type = high;
		break; // 256
	case i2p::data::CAPS_FLAG_EXTRA_BANDWIDTH1:
		limit = i2p::data::EXTRA_BANDWIDTH_LIMIT;
		type = extra;
		break; // 2048
	case i2p::data::CAPS_FLAG_EXTRA_BANDWIDTH2:
		limit = 1000000;
		type = unlim;
		break; // 1Gbyte/s
	default: limit = i2p::data::LOW_BANDWIDTH_LIMIT; type = low; // 48
	}
	/* update caps & flags in RI */
	std::lock_guard<std::mutex> l (m_RouterInfoMutex);
	auto caps = m_RouterInfo.GetCaps ();
	caps &= ~i2p::data::RouterInfo::eHighBandwidth;
	caps &= ~i2p::data::RouterInfo::eExtraBandwidth;
	switch (type)
	{
	case low: /* not set */; break;
	case extra: caps |= i2p::data::RouterInfo::eExtraBandwidth; break; // 'P'
	case unlim: caps |= i2p::data::RouterInfo::eExtraBandwidth; [[fallthrough]];
	// no break here, extra + high means 'X'
	case high: caps |= i2p::data::RouterInfo::eHighBandwidth; break;
	}
	UpdateCapsLocked (caps);
	UpdateRouterInfoLocked ();
	m_BandwidthLimit.store (limit, std::memory_order_relaxed);
}

void RouterContext::SetBandwidth (int limit)
{
	if (limit > (int)i2p::data::EXTRA_BANDWIDTH_LIMIT)
	{
		SetBandwidth ('X');
	}
	else if (limit > (int)i2p::data::HIGH_BANDWIDTH_LIMIT)
	{
		SetBandwidth ('P');
	}
	else if (limit > 128)
	{
		SetBandwidth ('O');
	}
	else if (limit > 64)
	{
		SetBandwidth ('N');
	}
	else if (limit > (int)i2p::data::LOW_BANDWIDTH_LIMIT)
	{
		SetBandwidth ('M');
	}
	else if (limit > 12)
	{
		SetBandwidth ('L');
	}
	else
	{
		SetBandwidth ('K');
	}

	LogPrint (eLogInfo, "RouterContext: Set bandwidth ", limit, ". kb/s");
	m_BandwidthLimit.store (limit, std::memory_order_relaxed); // set precise limit
}

void RouterContext::SetShareRatio (int percents)
{
	if (percents < 0) percents = 0;
	if (percents > 100) percents = 100;
	m_ShareRatio.store (percents, std::memory_order_relaxed);
}

bool RouterContext::IsUnreachable () const
{
	return m_RouterInfo.GetCaps () & i2p::data::RouterInfo::eUnreachable;
}

void RouterContext::SetUnreachable (bool v4, bool v6)
{
	std::lock_guard<std::mutex> l (m_RouterInfoMutex);
	if (v4 || (v6 && !m_RouterInfo.IsV4 ()))
	{
		// set caps
		uint8_t caps = m_RouterInfo.GetCaps ();
		caps &= ~i2p::data::RouterInfo::eReachable;
		caps |= i2p::data::RouterInfo::eUnreachable;
		if (v6 || !m_RouterInfo.IsV6 ()) caps &= ~i2p::data::RouterInfo::eFloodfill; // can't be floodfill
		UpdateCapsLocked (caps);
	}
	uint16_t port = 0;
	// delete previous introducers
	auto addresses = m_RouterInfo.GetAddresses ();
	if (addresses)
	{
		for (auto & addr : *addresses)
			if (addr && addr->ssu && ((v4 && addr->IsV4 ()) || (v6 && addr->IsV6 ())))
			{
				addr->published = false;
				UpdateSSU2AddressCapsIntroducer (addr, false); // can't be introducer
				{
					std::lock_guard<std::mutex> lock (addr->ssu->mutex);
					addr->ssu->introducers.clear ();
				}
				port = static_cast<uint16_t> (addr->port);
			}
	}
	// unpublish NTCP2 addreeses
	bool ntcp2;
	i2p::config::GetOption ("ntcp2.enabled", ntcp2);
	if (ntcp2)
	{
		int ntcp2version = 2;
#if OPENSSL_PQ
		i2p::config::GetOption ("ntcp2.version", ntcp2version);
#ifdef LIBRESSL_VERSION_NUMBER
		if (ntcp2version == 3) ntcp2version = 2; // ML-KEM-512 is unavailable
#endif
#endif
		PublishNTCP2AddressLocked (port, false, v4, v6, false, ntcp2version);
	}
	// update
	m_RouterInfo.UpdateSupportedTransports ();
	UpdateRouterInfoLocked ();
}

void RouterContext::SetReachable (bool v4, bool v6)
{
	std::lock_guard<std::mutex> l (m_RouterInfoMutex);
	if (v4 || (v6 && !m_RouterInfo.IsV4 ()))
	{
		// update caps
		uint8_t caps = m_RouterInfo.GetCaps ();
		caps &= ~i2p::data::RouterInfo::eUnreachable;
		caps |= i2p::data::RouterInfo::eReachable;
		if (m_IsFloodfill.load (std::memory_order_relaxed)) caps |= i2p::data::RouterInfo::eFloodfill;
		UpdateCapsLocked (caps);
	}
	uint16_t port = 0;
	// delete previous introducers
	bool isSSU2Published;
	i2p::config::GetOption ("ssu2.published", isSSU2Published);
	auto addresses = m_RouterInfo.GetAddresses ();
	if (addresses)
	{
		for (auto & addr : *addresses)
			if (addr && addr->ssu && isSSU2Published && ((v4 && addr->IsV4 ()) || (v6 && addr->IsV6 ())))
			{
				addr->published = true;
				UpdateSSU2AddressCapsIntroducer (addr, !m_IsFloodfill.load (std::memory_order_relaxed));
				{
					std::lock_guard<std::mutex> lock (addr->ssu->mutex);
					addr->ssu->introducers.clear ();
				}
				if (addr->port) port = static_cast<uint16_t> (addr->port);
			}
	}
	// publish NTCP2
	bool ntcp2;
	i2p::config::GetOption ("ntcp2.enabled", ntcp2);
	if (ntcp2)
	{
		bool published;
		i2p::config::GetOption ("ntcp2.published", published);
		if (published)
		{
			uint16_t ntcp2Port;
			i2p::config::GetOption ("ntcp2.port", ntcp2Port);
			if (!ntcp2Port) ntcp2Port = port;
			int ntcp2version = 2;
#if OPENSSL_PQ
			i2p::config::GetOption ("ntcp2.version", ntcp2version);
#ifdef LIBRESSL_VERSION_NUMBER
			if (ntcp2version == 3) ntcp2version = 2; // ML-KEM-512 is unavailable
#endif
#endif
			PublishNTCP2AddressLocked (ntcp2Port, true, v4, v6, false, ntcp2version);
		}
	}
	// update
	m_RouterInfo.UpdateSupportedTransports ();
	UpdateRouterInfoLocked ();
}

void RouterContext::SetSupportsV6 (bool supportsV6)
{
	std::lock_guard<std::mutex> l (m_RouterInfoMutex);
	if (supportsV6)
	{
		// insert v6 addresses if necessary
		bool foundNTCP2 = false, foundSSU2 = false;
		uint16_t port = 0;
		auto addresses = m_RouterInfo.GetAddresses ();
		if (addresses)
		{
			for (auto & addr : *addresses)
			{
				if (addr && addr->IsV6 () && !i2p::util::net::IsYggdrasilAddress (addr->host))
				{
					switch (addr->transportStyle)
					{
					case i2p::data::RouterInfo::eTransportNTCP2: foundNTCP2 = true; break;
					case i2p::data::RouterInfo::eTransportSSU2: foundSSU2 = true; break;
					default:;
					}
				}
				if (addr) port = static_cast<uint16_t> (addr->port);
			}
		}
		if (!port)
		{
			i2p::config::GetOption ("port", port);
			if (!port) port = SelectRandomPort ();
		}
		// NTCP2
		bool ntcp2;
		i2p::config::GetOption ("ntcp2.enabled", ntcp2);
		if (ntcp2)
		{
			if (!foundNTCP2)
			{
				uint16_t ntcp2Port;
				i2p::config::GetOption ("ntcp2.port", ntcp2Port);
				if (!ntcp2Port) ntcp2Port = port;
				bool added = false;
				bool ntcp2Published;
				i2p::config::GetOption ("ntcp2.published", ntcp2Published);
				if (ntcp2Published)
				{
					std::string ntcp2Host;
					if (!i2p::config::IsDefault ("ntcp2.addressv6"))
						i2p::config::GetOption ("ntcp2.addressv6", ntcp2Host);
					else
						i2p::config::GetOption ("host", ntcp2Host);
					if (!ntcp2Host.empty () && ntcp2Port)
					{
						auto addr = boost::asio::ip::make_address (ntcp2Host);
						if (addr.is_v6 ())
						{
							m_RouterInfo.AddNTCP2Address (
								m_NTCP2Keys->staticPublicKey, m_NTCP2Keys->iv, addr, ntcp2Port);
							added = true;
						}
					}
				}
				if (!added)
					m_RouterInfo.AddNTCP2Address (
						m_NTCP2Keys->staticPublicKey, m_NTCP2Keys->iv, ntcp2Port, i2p::data::RouterInfo::eV6);
			}
		}
		else
			m_RouterInfo.RemoveNTCP2Address (false);
		// SSU2
		bool ssu2;
		i2p::config::GetOption ("ssu2.enabled", ssu2);
		if (ssu2)
		{
			if (!foundSSU2)
			{
				uint16_t ssu2Port;
				i2p::config::GetOption ("ssu2.port", ssu2Port);
				if (!ssu2Port) ssu2Port = port;
				bool added = false;
				bool ssu2Published;
				i2p::config::GetOption ("ssu2.published", ssu2Published);
				if (ssu2Published && ssu2Port)
				{
					std::string host;
					i2p::config::GetOption ("host", host);
					if (!host.empty ())
					{
						auto addr = boost::asio::ip::make_address (host);
						if (addr.is_v6 ())
						{
							m_RouterInfo.AddSSU2Address (
								m_SSU2Keys->staticPublicKey, m_SSU2Keys->intro, addr, ssu2Port);
							added = true;
						}
					}
				}
				if (!added)
					m_RouterInfo.AddSSU2Address (
						m_SSU2Keys->staticPublicKey, m_SSU2Keys->intro, ssu2Port, i2p::data::RouterInfo::eV6);
			}
		}
		else
			m_RouterInfo.RemoveSSU2Address (false);
		if (ntcp2 || ssu2) m_RouterInfo.EnableV6 ();
	}
	else
		m_RouterInfo.DisableV6 ();
	UpdateRouterInfoLocked ();
}

void RouterContext::SetSupportsV4 (bool supportsV4)
{
	std::lock_guard<std::mutex> l (m_RouterInfoMutex);
	if (supportsV4)
	{
		bool foundNTCP2 = false, foundSSU2 = false;
		uint16_t port = 0;
		auto addresses = m_RouterInfo.GetAddresses ();
		if (addresses)
		{
			for (auto & addr : *addresses)
			{
				if (addr && addr->IsV4 ())
				{
					switch (addr->transportStyle)
					{
					case i2p::data::RouterInfo::eTransportNTCP2: foundNTCP2 = true; break;
					case i2p::data::RouterInfo::eTransportSSU2: foundSSU2 = true; break;
					default:;
					}
				}
				if (addr && addr->port) port = static_cast<uint16_t> (addr->port);
			}
		}
		if (!port)
		{
			i2p::config::GetOption ("port", port);
			if (!port) port = SelectRandomPort ();
		}
		// NTCP2
		bool ntcp2;
		i2p::config::GetOption ("ntcp2.enabled", ntcp2);
		if (ntcp2)
		{
			if (!foundNTCP2)
			{
				uint16_t ntcp2Port;
				i2p::config::GetOption ("ntcp2.port", ntcp2Port);
				if (!ntcp2Port) ntcp2Port = port;
				bool added = false;
				bool ntcp2Published;
				i2p::config::GetOption ("ntcp2.published", ntcp2Published);
				if (ntcp2Published && ntcp2Port)
				{
					std::string host;
					i2p::config::GetOption ("host", host);
					if (!host.empty ())
					{
						auto addr = boost::asio::ip::make_address (host);
						if (addr.is_v4 ())
						{
							m_RouterInfo.AddNTCP2Address (
								m_NTCP2Keys->staticPublicKey, m_NTCP2Keys->iv, addr, ntcp2Port);
							added = true;
						}
					}
				}
				if (!added)
					m_RouterInfo.AddNTCP2Address (
						m_NTCP2Keys->staticPublicKey, m_NTCP2Keys->iv, ntcp2Port, i2p::data::RouterInfo::eV4);
			}
		}
		else
			m_RouterInfo.RemoveNTCP2Address (true);
		// SSU2
		bool ssu2;
		i2p::config::GetOption ("ssu2.enabled", ssu2);
		if (ssu2)
		{
			if (!foundSSU2)
			{
				uint16_t ssu2Port;
				i2p::config::GetOption ("ssu2.port", ssu2Port);
				if (!ssu2Port) ssu2Port = port;
				bool added = false;
				bool ssu2Published;
				i2p::config::GetOption ("ssu2.published", ssu2Published);
				std::string host;
				i2p::config::GetOption ("host", host);
				if (ssu2Published && ssu2Port)
				{
					if (!host.empty ())
					{
						auto addr = boost::asio::ip::make_address (host);
						if (addr.is_v4 ())
						{
							m_RouterInfo.AddSSU2Address (
								m_SSU2Keys->staticPublicKey, m_SSU2Keys->intro, addr, ssu2Port);
							added = true;
						}
					}
				}
				if (!added)
					m_RouterInfo.AddSSU2Address (
						m_SSU2Keys->staticPublicKey, m_SSU2Keys->intro, ssu2Port, i2p::data::RouterInfo::eV4);
			}
		}
		else
			m_RouterInfo.RemoveSSU2Address (true);
		if (ntcp2 || ssu2) m_RouterInfo.EnableV4 ();
	}
	else
		m_RouterInfo.DisableV4 ();
	UpdateRouterInfoLocked ();
}

void RouterContext::SetSupportsMesh (bool supportsmesh, const boost::asio::ip::address_v6 & host)
{
	std::lock_guard<std::mutex> l (m_RouterInfoMutex);
	if (supportsmesh)
	{
		auto addresses = m_RouterInfo.GetAddresses ();
		if (!addresses) return;
		m_RouterInfo.EnableMesh ();
		if ((*addresses)[i2p::data::RouterInfo::eNTCP2V6MeshIdx]) return; // we have mesh address already
		uint16_t port = 0;
		i2p::config::GetOption ("ntcp2.port", port);
		if (!port) i2p::config::GetOption ("port", port);
		if (!port)
		{
			for (auto & addr : *addresses)
			{
				if (addr && addr->port)
				{
					port = static_cast<uint16_t> (addr->port);
					break;
				}
			}
		}
		if (!port) port = SelectRandomPort ();
		m_RouterInfo.AddNTCP2Address (m_NTCP2Keys->staticPublicKey, m_NTCP2Keys->iv, host, port);
	}
	else
		m_RouterInfo.DisableMesh ();
	UpdateRouterInfoLocked ();
}

void RouterContext::SetMTU (int mtu, bool v4)
{
	if (mtu < 1280 || mtu > 1500) return;
	std::lock_guard<std::mutex> l (m_RouterInfoMutex);
	auto addresses = m_RouterInfo.GetAddresses ();
	if (!addresses) return;
	bool updated = false;
	for (auto & addr : *addresses)
	{
		if (addr && addr->ssu && ((v4 && addr->IsV4 ()) || (!v4 && addr->IsV6 ())) && addr->ssu->mtu != mtu)
		{
			addr->ssu->mtu = mtu;
			LogPrint (eLogDebug, "Router: MTU for ", v4 ? "ipv4" : "ipv6", " address ",
				i2p::util::net::FormatAddress (addr->host), " is set to ", mtu);
			updated = true;
		}
	}
	// The MTU controls the published "mtu" option and whether "pq" may offer
	// ML-KEM-768. Persist it now: Start() sets the MTU after the last
	// RouterInfo rebuild, so without this the first published RI would still
	// advertise the old (or absent) MTU.
	if (updated) UpdateRouterInfoLocked ();
}

void RouterContext::UpdateNTCP2V6Address (const boost::asio::ip::address & host)
{
	std::lock_guard<std::mutex> l (m_RouterInfoMutex);
	auto addresses = m_RouterInfo.GetAddresses ();
	if (!addresses) return;
	std::shared_ptr<i2p::data::RouterInfo::Address> addr;
	if (i2p::util::net::IsYggdrasilAddress (host)) // yggdrasil
		addr = (*addresses)[i2p::data::RouterInfo::eNTCP2V6MeshIdx];
	else if (host.is_v6 ())
		addr = (*addresses)[i2p::data::RouterInfo::eNTCP2V6Idx];
	if (addr && addr->IsPublishedNTCP2 () && addr->host != host)
	{
		addr->host = host;
		UpdateRouterInfoLocked ();
	}
}

void RouterContext::UpdateStats ()
{
	if (m_IsFloodfill.load (std::memory_order_relaxed))
	{
		// Update the properties under the same mutex used by CreateBuffer();
		// otherwise a transport-thread address update can serialize a map while
		// these properties are being inserted.
		std::lock_guard<std::mutex> l (m_RouterInfoMutex);
		m_RouterInfo.SetProperty (
			i2p::data::ROUTER_INFO_PROPERTY_LEASESETS, std::to_string (i2p::data::netdb.GetNumLeaseSets ()));
		m_RouterInfo.SetProperty (
			i2p::data::ROUTER_INFO_PROPERTY_ROUTERS, std::to_string (i2p::data::netdb.GetNumRouters ()));
		UpdateRouterInfoLocked ();
	}
}

void RouterContext::UpdateTimestamp (uint64_t ts)
{
	if (ts > m_LastUpdateTime.load (std::memory_order_relaxed) + ROUTER_INFO_UPDATE_INTERVAL) UpdateRouterInfo ();
}

bool RouterContext::Load ()
{
	{
		std::ifstream fk (i2p::fs::DataDirPath (ROUTER_KEYS), std::ifstream::in | std::ifstream::binary);
		if (!fk.is_open ()) return false;
		fk.seekg (0, std::ios::end);
		const auto endPos = fk.tellg ();
		constexpr std::streamoff MAX_ROUTER_KEYS_FILE_SIZE = 64 * 1024;
		if (endPos == std::streampos (-1))
		{
			LogPrint (eLogError, "Router: cannot determine router.keys size");
			return false;
		}
		const auto end = static_cast<std::streamoff> (endPos);
		if (end <= 0 || end > MAX_ROUTER_KEYS_FILE_SIZE)
		{
			LogPrint (eLogError, "Router: invalid router.keys size");
			return false;
		}
		const size_t len = static_cast<size_t> (end);
		fk.seekg (0, std::ios::beg);

		if (len == sizeof (i2p::data::Keys)) // old keys file format
		{
			i2p::data::Keys keys;
			if (!fk.read (reinterpret_cast<char *> (&keys), sizeof (keys))) return false;
			m_Keys = keys;
		}
		else // new keys file format
		{
			std::vector<uint8_t> buf (len);
			if (!fk.read (reinterpret_cast<char *> (buf.data ()), len))
			{
				LogPrint (eLogError, "Router: truncated router.keys");
				return false;
			}
			if (!m_Keys.FromBuffer (buf.data (), buf.size ()))
			{
				LogPrint (eLogError, "Router: malformed router.keys");
				return false;
			}
		}
	}
	if (!m_Keys.GetPublic ())
	{
		LogPrint (eLogError, "Router: router.keys produced no public identity");
		return false;
	}
	std::shared_ptr<const i2p::data::IdentityEx> oldIdentity;
	if (m_Keys.GetPublic ()->GetSigningKeyType () == i2p::data::SIGNING_KEY_TYPE_DSA_SHA1
		|| m_Keys.GetPublic ()->GetCryptoKeyType () == i2p::data::CRYPTO_KEY_TYPE_ELGAMAL)
	{
		// update keys
		LogPrint (eLogInfo, "Router: router keys are obsolete. Creating new");
		oldIdentity = m_Keys.GetPublic ();
		m_Keys = i2p::data::PrivateKeys::CreateRandomKeys (
			i2p::data::SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519, i2p::data::CRYPTO_KEY_TYPE_ECIES_X25519_AEAD);
		SaveKeys ();
	}
	// read NTCP2 keys if available
	std::ifstream n2k (i2p::fs::DataDirPath (NTCP2_KEYS), std::ifstream::in | std::ifstream::binary);
	if (n2k)
	{
		n2k.seekg (0, std::ios::end);
		size_t len = n2k.tellg ();
		n2k.seekg (0, std::ios::beg);
		if (len == sizeof (NTCP2PrivateKeys))
		{
			auto keys = std::make_unique<NTCP2PrivateKeys> ();
			n2k.read ((char *)keys.get (), sizeof (NTCP2PrivateKeys));
			std::lock_guard<std::mutex> l (m_StaticKeysMutex);
			m_NTCP2Keys = std::move (keys);
		}
		n2k.close ();
	}
	// read SSU2 keys if available
	std::ifstream s2k (i2p::fs::DataDirPath (SSU2_KEYS), std::ifstream::in | std::ifstream::binary);
	if (s2k)
	{
		s2k.seekg (0, std::ios::end);
		size_t len = s2k.tellg ();
		s2k.seekg (0, std::ios::beg);
		if (len == sizeof (SSU2PrivateKeys))
		{
			auto keys = std::make_unique<SSU2PrivateKeys> ();
			s2k.read ((char *)keys.get (), sizeof (SSU2PrivateKeys));
			std::lock_guard<std::mutex> l (m_StaticKeysMutex);
			m_SSU2Keys = std::move (keys);
		}
		s2k.close ();
	}
	// read RouterInfo
	i2p::data::RouterInfo routerInfo (i2p::fs::DataDirPath (ROUTER_INFO));
	if (!routerInfo.IsUnreachable ()) // router.info looks good
	{
		std::lock_guard<std::mutex> l (m_RouterInfoMutex);
		m_RouterInfo.SetRouterIdentity (oldIdentity ? oldIdentity : GetIdentity ());
		m_RouterInfo.Update (routerInfo.GetBuffer (), routerInfo.GetBufferLen ());
		if (oldIdentity) m_RouterInfo.SetRouterIdentity (GetIdentity ()); // from new keys
		m_IsFloodfill.store (m_RouterInfo.IsDeclaredFloodfill (), std::memory_order_relaxed);
		m_RouterInfo.SetProperty ("router.version", I2P_VERSION);
		m_RouterInfo.DeleteProperty ("coreVersion"); // TODO: remove later
	}
	else
	{
		LogPrint (eLogError, ROUTER_INFO, " is malformed. Creating new");
		NewRouterInfo ();
	}

	if (IsUnreachable ())
		SetReachable (true, true); // we assume reachable until we discover firewall through peer tests

	bool updated = false;
	// update NTCP2 keys in RouterInfo
	bool ntcp2;
	i2p::config::GetOption ("ntcp2.enabled", ntcp2);
	bool ygg;
	i2p::config::GetOption ("meshnets.yggdrasil", ygg);
	if (ntcp2 || ygg)
	{
		if (!m_NTCP2Keys) NewNTCP2Keys (); // create new NTCP2 keys
		UpdateNTCP2Keys ();
		updated = true;
	}
	// update SSU2 keys in RouterInfo
	bool ssu2;
	i2p::config::GetOption ("ssu2.enabled", ssu2);
	if (ssu2)
	{
		if (!m_SSU2Keys) NewSSU2Keys (); // create new SSU2 keys
		UpdateSSU2Keys ();
		updated = true;
	}
	{
		std::lock_guard<std::mutex> l (m_RouterInfoMutex);
		if (m_RouterInfo.UpdateCongestion (i2p::data::RouterInfo::eLowCongestion)) updated = true;
		if (updated) UpdateRouterInfoLocked ();
	}

	return true;
}

void RouterContext::SaveKeys ()
{
	// Preserve the exact existing key serialization; only IO authority changes.
	std::vector<uint8_t> buffer (m_Keys.GetFullLen ());
	m_Keys.ToBuffer (buffer.data (), buffer.size ());
	const bool saved = WriteRetainedFixedFile (i2p::data::RouterPersistentFile::RouterKeys, buffer);
	OPENSSL_cleanse (buffer.data (), buffer.size ());
	if (!saved) throw std::runtime_error ("Router: identity key persistence failed");
}

std::shared_ptr<i2p::tunnel::TunnelPool> RouterContext::GetTunnelPool () const
{
	return i2p::tunnel::tunnels.GetExploratoryPool ();
}

int RouterContext::GetCongestionLevel (bool longTerm) const
{
	return std::max (
		i2p::tunnel::tunnels.GetCongestionLevel (), i2p::transport::transports.GetCongestionLevel (longTerm));
}

void RouterContext::HandleI2NPMessage (const uint8_t * buf, size_t len)
{
	auto msg = CreateI2NPMessage (buf, GetI2NPMessageLength (buf, len));
	if (msg)
	{
		msg->fromClove = true;
		// The enclosing garlic's tunnel pointer is not visible here; the
		// decrypt entry point recorded it for the duration of this call.
		msg->fromTunnel = g_IncomingGarlicFromTunnel;
	}
	else
	{
		static I2NPDropCounter cloveAllocationDrops;
		if (cloveAllocationDrops.Record ())
			LogPrint (eLogError, "Router: Can't allocate a clove message of ", len, " bytes (",
				cloveAllocationDrops.GetCount (), " dropped)");
		return;
	}
	i2p::HandleI2NPMessage (msg);
}

bool RouterContext::HandleCloveI2NPMessage (I2NPMessageType typeID, const uint8_t * payload, size_t len, uint32_t msgID,
	i2p::garlic::ECIESX25519AEADRatchetSession * /* from */)
{
	if (typeID == eI2NPTunnelTest)
	{
		// TunnelTest payloads carry msgID and timestamp; a garlic clove can be
		// shorter than the fixed offsets and the generic clove header does not
		// guarantee the payload length.
		if (len < TUNNEL_TEST_SIZE) return false;
		// try tunnel test
		auto pool = GetTunnelPool ();
		if (pool
			&& pool->ProcessTunnelTest (
				bufbe32toh (payload + TUNNEL_TEST_MSGID_OFFSET), bufbe64toh (payload + TUNNEL_TEST_TIMESTAMP_OFFSET)))
			return true;
	}
	auto msg = CreateI2NPMessage (typeID, payload, len, msgID);
	if (!msg)
	{
		static I2NPDropCounter cloveAllocationDrops;
		if (cloveAllocationDrops.Record ())
			LogPrint (eLogError, "Router: Can't allocate clove message type ", (int)typeID, " of ", len,
				" bytes (", cloveAllocationDrops.GetCount (), " dropped)");
		return false;
	}
	msg->fromClove = true;
	msg->fromTunnel = g_IncomingGarlicFromTunnel;
	i2p::HandleI2NPMessage (msg);
	return true;
}

void RouterContext::ProcessGarlicMessage (std::shared_ptr<I2NPMessage> msg)
{
	auto service = m_Service.Load (std::memory_order_acquire);
	if (service)
		boost::asio::post (service->GetService (), std::bind (&RouterContext::PostGarlicMessage, this, msg));
	else
		LogPrint (eLogError, "Router: service is NULL");
}

void RouterContext::PostGarlicMessage (std::shared_ptr<I2NPMessage> msg)
{
	const size_t payloadLength = msg && msg->GetLength () >= I2NP_HEADER_SIZE ? msg->GetPayloadLength () : 0;
	if (payloadLength < 4)
	{
		LogPrint (eLogWarning, "Router: garlic message is shorter than its length prefix");
		return;
	}
	uint8_t * buf = msg->GetPayload ();
	const uint32_t len = bufbe32toh (buf);
	if (len < 8 || len > payloadLength - 4)
	{
		LogPrint (eLogWarning, "Router: garlic message length ", len, " exceeds payload length ", payloadLength - 4);
		return;
	}
	buf += 4;
	// Publish the enclosing garlic's delivery path for the clove callbacks
	// below. Direct transport garlic has no InboundTunnel; a tunnel-delivered
	// one is marked so a clove cannot be mistaken for a direct message. A
	// garlic clove can itself contain a garlic message, so the marker has to
	// survive that nesting as well.
	const bool previousOrigin = g_IncomingGarlicFromTunnel;
	g_IncomingGarlicFromTunnel = msg->from != nullptr || msg->fromTunnel;
	auto restoreOrigin = i2p::modern::ScopeExit ([previousOrigin] { g_IncomingGarlicFromTunnel = previousOrigin; });
	if (!HandleECIESx25519TagMessage (buf, len)) // try tag first
	{
		// then Noise_N one-time decryption
		if (m_ECIESSession)
			m_ECIESSession->HandleNextMessage (buf, len);
		else
			LogPrint (eLogError, "Router: Session is not set for ECIES router");
	}
}

void RouterContext::ProcessDeliveryStatusMessage (std::shared_ptr<I2NPMessage> msg)
{
	auto service = m_Service.Load (std::memory_order_acquire);
	if (service)
		boost::asio::post (service->GetService (), std::bind (&RouterContext::PostDeliveryStatusMessage, this, msg));
	else
		LogPrint (eLogError, "Router: service is NULL");
}

void RouterContext::PostDeliveryStatusMessage (std::shared_ptr<I2NPMessage> msg)
{
	// Java DeliveryStatusMessage is exactly msgID (4) + timestamp (8). A
	// different length means the layout is not the one this handler reads.
	if (!msg || msg->GetLength () < I2NP_HEADER_SIZE || !IsValidDeliveryStatusPayload (msg->GetPayloadLength ()))
	{
		static I2NPDropCounter badDeliveryStatus;
		if (badDeliveryStatus.Record ())
			LogPrint (eLogWarning, "Router: DeliveryStatus message is not ", DELIVERY_STATUS_SIZE, " bytes (",
				badDeliveryStatus.GetCount (), " such messages)");
		return;
	}
	if (m_PublishReplyToken == bufbe32toh (msg->GetPayload () + DELIVERY_STATUS_MSGID_OFFSET))
	{
		LogPrint (eLogInfo, "Router: Publishing confirmed. reply token=", m_PublishReplyToken);
		m_PublishExcluded.clear ();
		m_PublishReplyToken = 0;
		SchedulePublish ();
	}
	else
		i2p::garlic::GarlicDestination::ProcessDeliveryStatusMessage (msg);
}

void RouterContext::SubmitECIESx25519Key (const uint8_t * key, uint64_t tag)
{
	auto service = m_Service.Load (std::memory_order_acquire);
	if (service)
	{
		struct
		{
			uint8_t k[32];
			uint64_t t;
		} data;
		memcpy (data.k, key, 32);
		data.t = tag;
		boost::asio::post (service->GetService (), [this, data] (void) { AddECIESx25519Key (data.k, data.t); });
	}
	else
		LogPrint (eLogError, "Router: service is NULL");
}

uint32_t RouterContext::GetUptime () const
{
	return static_cast<uint32_t> (i2p::util::GetMonotonicSeconds () - m_StartupTime.load (std::memory_order_relaxed));
}

bool RouterContext::Decrypt (
	const uint8_t * encrypted, uint8_t * data, i2p::data::CryptoKeyType /* preferredCrypto */) const
{
	return m_Decryptor ? m_Decryptor->Decrypt (encrypted, data) : false;
}

bool RouterContext::DecryptTunnelBuildRecord (
	const uint8_t * encrypted, uint8_t * data, i2p::crypto::NoiseSymmetricState & noiseState)
{
	return DecryptECIESTunnelBuildRecord (encrypted, data, ECIES_BUILD_REQUEST_RECORD_CLEAR_TEXT_SIZE, noiseState);
}

bool RouterContext::DecryptECIESTunnelBuildRecord (
	const uint8_t * encrypted, uint8_t * data, size_t clearTextSize, i2p::crypto::NoiseSymmetricState & noiseState)
{
	// The caller owns this scratch state; never share it between concurrent
	// build records arriving on different transport threads.
	noiseState = m_InitialNoiseState;
	noiseState.MixHash (encrypted, 32); // h = SHA256(h || sepk)
	uint8_t sharedSecret[32];
	if (!m_TunnelDecryptor->Decrypt (encrypted, sharedSecret))
	{
		LogPrint (eLogWarning, "Router: Incorrect ephemeral public key");
		return false;
	}
	noiseState.MixKey (sharedSecret);
	encrypted += 32;
	uint8_t nonce[12];
	memset (nonce, 0, 12);
	if (!i2p::crypto::AEADChaCha20Poly1305 (encrypted, clearTextSize, noiseState.m_H, 32, noiseState.m_CK + 32, nonce,
			data, clearTextSize, false)) // decrypt
	{
		LogPrint (eLogWarning, "Router: Tunnel record AEAD decryption failed");
		return false;
	}
	noiseState.MixHash (encrypted, clearTextSize + 16); // h = SHA256(h || ciphertext)
	return true;
}

bool RouterContext::DecryptTunnelShortRequestRecord (
	const uint8_t * encrypted, uint8_t * data, i2p::crypto::NoiseSymmetricState & noiseState)
{
	return DecryptECIESTunnelBuildRecord (encrypted, data, SHORT_REQUEST_RECORD_CLEAR_TEXT_SIZE, noiseState);
}

i2p::crypto::X25519Keys & RouterContext::GetNTCP2StaticKeys ()
{
	std::call_once (m_NTCP2KeysOnce,
		[this] ()
		{
			NewNTCP2Keys (); // idempotent, locks m_StaticKeysMutex
			std::lock_guard<std::mutex> l (m_StaticKeysMutex);
			if (!m_NTCP2StaticKeys)
				m_NTCP2StaticKeys = std::make_unique<i2p::crypto::X25519Keys> (
					m_NTCP2Keys->staticPrivateKey, m_NTCP2Keys->staticPublicKey);
		});
	return *m_NTCP2StaticKeys;
}

i2p::crypto::X25519Keys & RouterContext::GetSSU2StaticKeys ()
{
	std::call_once (m_SSU2KeysOnce,
		[this] ()
		{
			NewSSU2Keys (); // idempotent, locks m_StaticKeysMutex
			std::lock_guard<std::mutex> l (m_StaticKeysMutex);
			if (!m_SSU2StaticKeys)
				m_SSU2StaticKeys = std::make_unique<i2p::crypto::X25519Keys> (
					m_SSU2Keys->staticPrivateKey, m_SSU2Keys->staticPublicKey);
		});
	return *m_SSU2StaticKeys;
}

void RouterContext::ScheduleInitialPublish ()
{
	if (m_PublishTimer)
	{
		m_PublishTimer->expires_after (std::chrono::seconds (ROUTER_INFO_INITIAL_PUBLISH_INTERVAL));
		m_PublishTimer->async_wait (std::bind (&RouterContext::HandleInitialPublishTimer, this, std::placeholders::_1));
	}
	else
		LogPrint (eLogError, "Router: Publish timer is NULL");
}

void RouterContext::HandleInitialPublishTimer (const boost::system::error_code & ecode)
{
	if (ecode != boost::asio::error::operation_aborted)
	{
		if (m_RouterInfo.IsReachableBy (i2p::data::RouterInfo::eAllTransports))
		{
			UpdateCongestion ();
			HandlePublishTimer (ecode);
		}
		else
		{
			UpdateTimestamp (i2p::util::GetSecondsSinceEpoch ());
			ScheduleInitialPublish ();
		}
	}
}

void RouterContext::SchedulePublish ()
{
	if (m_PublishTimer)
	{
		m_PublishTimer->cancel ();
		m_PublishTimer->expires_after (
			std::chrono::seconds (ROUTER_INFO_PUBLISH_INTERVAL + GetRng () () % ROUTER_INFO_PUBLISH_INTERVAL_VARIANCE));
		m_PublishTimer->async_wait (std::bind (&RouterContext::HandlePublishTimer, this, std::placeholders::_1));
	}
	else
		LogPrint (eLogError, "Router: Publish timer is NULL");
}

void RouterContext::HandlePublishTimer (const boost::system::error_code & ecode)
{
	if (ecode != boost::asio::error::operation_aborted)
	{
		UpdateTimestamp (i2p::util::GetSecondsSinceEpoch ());
		if (!m_IsHiddenMode.load (std::memory_order_relaxed) && !IsLimitedConnectivity ())
		{
			m_PublishExcluded.clear ();
			m_PublishReplyToken = 0;
			if (IsFloodfill ())
			{
				UpdateStats (); // for floodfill
				m_PublishExcluded.insert (i2p::context.GetIdentHash ()); // don't publish to ourselves
			}
			SchedulePublishResend (Publish ());
		}
		else
			SchedulePublish ();
	}
}

uint64_t RouterContext::Publish ()
{
	if (!i2p::transport::transports.IsOnline ()) return ROUTER_INFO_INITIAL_PUBLISH_INTERVAL * 1000LL;
	if (m_PublishExcluded.size () > ROUTER_INFO_MAX_PUBLISH_EXCLUDED_FLOODFILLS)
	{
		LogPrint (eLogError, "Router: Couldn't publish our RouterInfo to ", ROUTER_INFO_MAX_PUBLISH_EXCLUDED_FLOODFILLS,
			" closest routers. Try again");
		m_PublishExcluded.clear ();
		UpdateTimestamp (i2p::util::GetSecondsSinceEpoch ());
	}

	uint64_t publishConfirmationTimeout = ROUTER_INFO_CONFIRMATION_TIMEOUT;
	auto floodfill = i2p::data::netdb.GetClosestFloodfill (i2p::context.GetIdentHash (), m_PublishExcluded);
	if (floodfill)
	{
		uint32_t replyToken;
		RAND_bytes ((uint8_t *)&replyToken, 4);
		LogPrint (eLogInfo, "Router: Publishing our RouterInfo to ",
			i2p::data::GetIdentHashAbbreviation (floodfill->GetIdentHash ()), ". reply token=", replyToken);
		auto onDrop = [this] ()
		{
			auto service = m_Service.Load (std::memory_order_acquire);
			if (service)
				boost::asio::post (
					service->GetService (), [this] () { HandlePublishResendTimer (boost::system::error_code ()); });
		};
		if (i2p::transport::transports.IsConnected (floodfill->GetIdentHash ()) || // already connected
			(floodfill->IsReachableFrom (i2p::context.GetRouterInfo ()) && // are we able to connect
				!IsLimitedConnectivity () && // and not limited connectivity
				!i2p::transport::transports.RoutesRestricted ())) // and routes not restricted
		{
			// send directly
			auto msg = CreateDatabaseStoreMsg (i2p::context.GetSharedRouterInfo (), replyToken);
			msg->onDrop = onDrop;
			i2p::transport::transports.SendMessage (floodfill->GetIdentHash (), msg);
		}
		else
		{
			// otherwise through exploratory
			auto exploratoryPool = i2p::tunnel::tunnels.GetExploratoryPool ();
			auto outbound = exploratoryPool
				? exploratoryPool->GetNextOutboundTunnel (nullptr, floodfill->GetCompatibleTransports (false))
				: nullptr;
			auto inbound = exploratoryPool
				? exploratoryPool->GetNextInboundTunnel (nullptr, floodfill->GetCompatibleTransports (true))
				: nullptr;
			if (inbound && outbound)
			{
				// encrypt for floodfill
				auto msg = CreateDatabaseStoreMsg (i2p::context.GetSharedRouterInfo (), replyToken, inbound);
				msg->onDrop = onDrop;
				outbound->SendTunnelDataMsgTo (floodfill->GetIdentHash (), 0,
					i2p::garlic::WrapECIESX25519MessageForRouter (
						msg, floodfill->GetIdentity ()->GetEncryptionPublicKey ()));
				publishConfirmationTimeout += inbound->LatencyIsKnown ()
					? (uint64_t)(inbound->GetMeanLatency ()) / 1000LL
					: inbound->GetNumHops () * (uint64_t)(i2p::tunnel::HIGH_LATENCY_PER_HOP) / 1000LL;
				publishConfirmationTimeout += outbound->LatencyIsKnown ()
					? (uint64_t)(outbound->GetMeanLatency ()) / 1000LL
					: outbound->GetNumHops () * (uint64_t)(i2p::tunnel::HIGH_LATENCY_PER_HOP) / 1000LL;
			}
			else
			{
				publishConfirmationTimeout = ROUTER_INFO_INITIAL_PUBLISH_INTERVAL * 1000LL;
				LogPrint (eLogWarning, "Router: Can't publish our RouterInfo. No tunnels. Try again in ",
					publishConfirmationTimeout, " milliseconds");
			}
		}
		m_PublishExcluded.insert (floodfill->GetIdentHash ());
		m_PublishReplyToken = replyToken;
	}
	else
	{
		publishConfirmationTimeout = ROUTER_INFO_INITIAL_PUBLISH_INTERVAL * 1000LL;
		LogPrint (eLogWarning, "Router: Can't find floodfill to publish our RouterInfo");
	}
	return publishConfirmationTimeout;
}

void RouterContext::SchedulePublishResend (uint64_t publishConfirmationTimeout)
{
	if (m_PublishTimer)
	{
		m_PublishTimer->cancel ();
		m_PublishTimer->expires_after (std::chrono::milliseconds (publishConfirmationTimeout));
		m_PublishTimer->async_wait (std::bind (&RouterContext::HandlePublishResendTimer, this, std::placeholders::_1));
	}
	else
		LogPrint (eLogError, "Router: Publish timer is NULL");
}

void RouterContext::HandlePublishResendTimer (const boost::system::error_code & ecode)
{
	if (ecode != boost::asio::error::operation_aborted)
	{
		i2p::context.UpdateTimestamp (i2p::util::GetSecondsSinceEpoch ());
		SchedulePublishResend (Publish ());
	}
}

void RouterContext::ScheduleCongestionUpdate ()
{
	if (m_CongestionUpdateTimer)
	{
		m_CongestionUpdateTimer->cancel ();
		m_CongestionUpdateTimer->expires_after (std::chrono::seconds (
			ROUTER_INFO_CONGESTION_UPDATE_INTERVAL + GetRng () () % ROUTER_INFO_CONGESTION_UPDATE_INTERVAL_VARIANCE));
		m_CongestionUpdateTimer->async_wait (
			std::bind (&RouterContext::HandleCongestionUpdateTimer, this, std::placeholders::_1));
	}
	else
		LogPrint (eLogError, "Router: Congestion update timer is NULL");
}

void RouterContext::HandleCongestionUpdateTimer (const boost::system::error_code & ecode)
{
	if (ecode != boost::asio::error::operation_aborted)
	{
		UpdateCongestion ();
		ScheduleCongestionUpdate ();
	}
}

void RouterContext::UpdateCongestion ()
{
	auto c = i2p::data::RouterInfo::eLowCongestion;
	if (!AcceptsTunnels () || !m_ShareRatio.load (std::memory_order_relaxed))
		c = i2p::data::RouterInfo::eRejectAll;
	else
	{
		int congestionLevel = GetCongestionLevel (true);
		if (congestionLevel > CONGESTION_LEVEL_HIGH)
			c = i2p::data::RouterInfo::eHighCongestion;
		else if (congestionLevel > CONGESTION_LEVEL_MEDIUM)
			c = i2p::data::RouterInfo::eMediumCongestion;
	}
	bool updated = false;
	{
		std::lock_guard<std::mutex> l (m_RouterInfoMutex);
		updated = m_RouterInfo.UpdateCongestion (c);
		if (updated) UpdateRouterInfoLocked ();
	}
}

void RouterContext::ScheduleCleanupTimer ()
{
	if (m_CleanupTimer)
	{
		m_CleanupTimer->cancel ();
		m_CleanupTimer->expires_after (std::chrono::seconds (ROUTER_INFO_CLEANUP_INTERVAL));
		m_CleanupTimer->async_wait (std::bind (&RouterContext::HandleCleanupTimer, this, std::placeholders::_1));
	}
	else
		LogPrint (eLogError, "Router: Cleanup timer is NULL");
}

void RouterContext::HandleCleanupTimer (const boost::system::error_code & ecode)
{
	if (ecode != boost::asio::error::operation_aborted)
	{
		CleanupExpiredTags ();
		ScheduleCleanupTimer ();
	}
}
}

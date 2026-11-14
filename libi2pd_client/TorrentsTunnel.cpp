/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: TorrentsTunnel.
 *
 * Starts and supervises the torrent engine: it loads torrents and
 * resume or administrative state from a confined directory, runs
 * tracker and DHT announce, reconnect and keep-alive timers, connects
 * peers and marshals control commands onto the service thread. Disk I/O
 * and hashing run on a separate worker and every path is checked to
 * stay within the torrents directory.
 *
 */

#ifndef NO_TORRENTS

#include <fstream>
#include <utility>
#include <future>
#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstring>
#include <exception>
#include <limits>
#include <system_error>
#include <boost/algorithm/string.hpp>
#include <boost/algorithm/hex.hpp>
#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include "Log.h"
#include "Timestamp.h"
#include "I2PEndian.h"
#include "AddressBook.h"
#include "Base.h"
#include "ClientContext.h"
#include "TorrentsTunnel.h"
#include "TorrentsCreator.h"

namespace i2p
{
namespace torrents
{
	namespace
	{
		std::mutex payloadFactoryMutex;
		TorrentPayloadStorageFactory payloadFactory;

		std::string PeerFlags (const PeerConnection & peer)
		{
			std::string flags;
			if (peer.IsDownloading ())
				flags.push_back ('D');
			else if (peer.IsInterested ())
				flags.push_back ('d');
			else if (!peer.IsChoked ())
				flags.push_back ('K');
			if (peer.IsUploading ())
				flags.push_back ('U');
			else if (peer.IsRemoteInterested ())
				flags.push_back ('u');
			else if (!peer.IsRemoteChoked ())
				flags.push_back ('?');
			switch (peer.GetOrigin ())
			{
			case ePeerConnectionOriginIncoming: flags.push_back ('I'); break;
			case ePeerConnectionOriginPEX: flags.push_back ('X'); break;
			case ePeerConnectionOriginDHT: flags.push_back ('H'); break;
			default: break;
			}
			return flags;
		}

		std::string PeerClientName (const PeerConnection::PeerID & peerID)
		{
			if (peerID.size () >= 6 && !memcmp (peerID.data (), "-I2PD-", 6)) return "i2pd";
			std::string name = "Unknown (";
			for (auto ch : peerID) name.push_back (std::isprint (static_cast<unsigned char> (ch)) ? ch : '?');
			name.push_back (')');
			return name;
		}

		int ClampToInt (size_t value)
		{
			return static_cast<int> (std::min (value, static_cast<size_t> (std::numeric_limits<int>::max ())));
		}

		bool IsI2PTrackerURL (const i2p::http::URL & url)
		{
			return (url.schema == "http" || url.schema == "udp") && IsI2PTrackerHost (url.host) && url.user.empty ()
				&& url.pass.empty () && !url.ipv6;
		}

		// Percent-encodes a query value. The tracker query is assembled by
		// URL::create_query(), which does not encode parameters, so a peer id
		// with reserved bytes would otherwise corrupt the request.
		std::string PercentEncode (std::string_view value)
		{
			static constexpr char hex[] = "0123456789ABCDEF";
			std::string out;
			out.reserve (value.size () * 3);
			for (const unsigned char ch : value)
			{
				if (std::isalnum (ch) || ch == '-' || ch == '_' || ch == '.' || ch == '~')
					out.push_back (static_cast<char> (ch));
				else
				{
					out.push_back ('%');
					out.push_back (hex[ch >> 4]);
					out.push_back (hex[ch & 0x0F]);
				}
			}
			return out;
		}
	}

	std::string PercentEncodeQueryValue (std::string_view value)
	{
		return PercentEncode (value);
	}

	TorrentsTunnel::TorrentsTunnel (std::string_view name,
		std::shared_ptr<i2p::client::ClientDestination> localDestination, std::string_view torrentsDir,
		std::string_view trackers, bool dht, std::string_view dhtFileName, bool isolatedStorage)
		: i2p::client::I2PService (localDestination)
		, m_Name (name)
		, m_TorrentsDir (torrentsDir)
		, m_TrackerRequestsCheckTimer (GetService ())
		, m_KeepAliveCheckTimer (GetService ())
		, m_ReconnectCheckTimer (GetService ())
		, m_TorrentsStatusUpdateTimer (GetService ())
	{
		m_UseIsolatedStorage = isolatedStorage;
		if (!trackers.empty ())
		{
			// parse param
			std::vector<std::string> trackersList;
			boost::split (trackersList, trackers, boost::is_any_of (","), boost::token_compress_on);
			// exclude duplicates
			std::unordered_map<i2p::data::IdentHash, i2p::http::URL> hosts;
			for (const auto & it : trackersList)
			{
				if (hosts.size () >= MAX_NUM_TRACKERS)
				{
					LogPrint (eLogWarning, "TorrentsTunnel: Ignoring trackers above the limit ", MAX_NUM_TRACKERS);
					break;
				}
				if (it.size () > MAX_TRACKER_ANNOUNCE_LENGTH) continue;
				i2p::http::URL url (it);
				if (!IsI2PTrackerURL (url))
				{
					LogPrint (eLogInfo, "TorrentsTunnel: Rejecting non-I2P tracker host ", url.host);
					continue;
				}
				auto address = i2p::client::context.GetAddressBook ().GetAddress (url.host);
				if (address && address->IsIdentHash ())
				{
					auto [it1, inserted] = hosts.emplace (address->identHash, url);
					if (!inserted && url.schema == "udp" && it1->second.schema == "http")
						it1->second = url; // replace http address by udp address
				}
				else
					LogPrint (eLogInfo, "TorrentsTunnel: Unexepcted tracker host ", url.host);
			}
			// create common trackers
			for (const auto & it : hosts)
			{
				m_Trackers.emplace_back (TrackerInfo { it.second, true, 0, 0, 0 });
				m_TrackerSlots.emplace_back (TrackerSlotState { 0, true });
			}
		}
		if (dht)
		{
			auto candidate = std::make_unique<TorrentsDHT> (*this,
				TORRENT_PORT + (localDestination ? localDestination->GetRng () () % (65535 - TORRENT_PORT - 1) : 1),
				dhtFileName);
			if (candidate->IsInitialized ()) m_DHT = std::move (candidate);
		}
	}

	TorrentsTunnel::~TorrentsTunnel ()
	{
		RevokeInboundAccepts ();
		// Normally Stop() has already run on the service thread. The
		// destructor cannot call shared_from_this() to marshal another
		// teardown, but it must still join the disk worker: a joinable
		// std::thread would otherwise call std::terminate. Everything else is
		// owned by the service object and is released by the base destructor.
		if (!m_Stopping.load (std::memory_order_acquire))
		{
			LogPrint (eLogWarning, "TorrentsTunnel: destroyed without a completed Stop(); joining disk I/O only");
			m_DiskIOService.Stop ();
		}
	}

	void TorrentsTunnel::Start ()
	{
		RevokeInboundAccepts ();
		{
			std::lock_guard<std::mutex> publication (m_InboundAcceptMutex);
			m_InboundAcceptStopped = false;
		}
		m_Stopping.store (false, std::memory_order_release);
		StartPeerDials ();
		m_StartupRecoveryPending.store (true);
		if (m_UseIsolatedStorage)
		{
			std::string error;
			m_StorageWorker = std::make_shared<TorrentStorageWorker> ();
			if (!CreateDirectoriesWithinRootNoFollow (m_TorrentsDir, { })
				|| !m_StorageWorker->Start (m_TorrentsDir, error))
			{
				m_Stopping = true;
				LogPrint (eLogError, "TorrentsTunnel: Required isolated storage failed: ", error);
				return;
			}
		}

		if (m_DHT && m_UseIsolatedStorage)
		{
			const auto root = std::filesystem::path (i2p::fs::GetDataDir ()) / "torrents";
			std::string error;
			m_DHTStorageWorker = std::make_shared<TorrentStorageWorker> ();
			if (!CreateDirectoriesWithinRootNoFollow (root, { }) || !m_DHTStorageWorker->Start (root, error))
			{
				m_Stopping = true;
				LogPrint (eLogError, "TorrentsTunnel: Required DHT storage failed: ", error);
				m_DHTStorageWorker->Stop ();
				if (m_StorageWorker) m_StorageWorker->Stop ();
				return;
			}
			m_DHT->SetPersistence (
				[worker = m_DHTStorageWorker, root] (const auto & path, auto & bytes, auto & error)
				{
					auto name = path.lexically_relative (root);
					return worker->AuthorizeDHTMetadata (name, error) && worker->ReadMetadata (name, bytes, error);
				},
				[worker = m_DHTStorageWorker, root] (const auto & path, auto bytes, auto & error)
				{
					auto name = path.lexically_relative (root);
					return worker->AuthorizeDHTMetadata (name, error) && worker->ReplaceMetadata (name, bytes, error);
				});
		}

		i2p::client::I2PService::Start ();
		m_DiskIOService.Start ();
		auto localDestination = GetLocalDestination ();
		if (!localDestination) return;
		auto dgramDest = localDestination->CreateDatagramDestination (true, i2p::datagram::eDatagramV1); // V1 for DHT
		if (dgramDest)
		{
			m_RawDatagramReceiver.Reset ();
			m_RawDatagramReceiver = dgramDest->SetRawReceiver (std::bind (&TorrentsTunnel::HandleRecvFromI2PRaw,
				std::static_pointer_cast<TorrentsTunnel> (shared_from_this ()), std::placeholders::_1,
				std::placeholders::_2, std::placeholders::_3, std::placeholders::_4, std::placeholders::_5));
		}
		if (m_DHT) m_DHT->Start ();

		Accept ();
		LoadInitialTorrents ();

		ScheduleTrackerRequestsCheck ();
		ScheduleReconnectCheck ();
		ScheduleKeepAliveCheck ();
		ScheduleStatusUpdate ();
	}

	void TorrentsTunnel::Stop ()
	{
		// Revoke admission now, even when service teardown remains queued.
		RevokeInboundAccepts ();
		if (m_Stopping.load (std::memory_order_acquire)) return;
		auto localDestination = GetLocalDestination ();
		if (!localDestination)
		{
			// No service is available to marshal to; the timers belong to the
			// (already released) destination, so only the owned worker and
			// handlers can be torn down.
			m_Stopping.store (true, std::memory_order_release);
			m_RawDatagramReceiver.Reset ();
			// The destination (and its service) is already gone, so the DHT
			// timers cannot be cancelled safely. Stop the worker processes that
			// do own live threads; their metadata state is discarded with the
			// released tunnel.
			m_DiskIOService.Stop ();
			if (m_DHTStorageWorker) m_DHTStorageWorker->Stop ();
			if (m_StorageWorker) m_StorageWorker->Stop ();
			i2p::client::I2PService::ClearHandlers ();
			i2p::client::I2PService::Stop ();
			return;
		}

		auto & service = localDestination->GetService ();
		if (service.get_executor ().running_in_this_thread ())
		{
			StopOnService ();
			return;
		}

		std::shared_ptr<TorrentsTunnel> self;
		try
		{
			self = std::static_pointer_cast<TorrentsTunnel> (shared_from_this ());
		}
		catch (const std::exception &)
		{
			// Without an owner there is no way to keep the object alive while
			// the service thread runs the closure. Do not mutate DHT, timers
			// or torrent state from the caller; the destructor still joins the
			// owned disk worker and the base destructor clears handlers.
			LogPrint (eLogWarning, "TorrentsTunnel: Stop() without a shared owner; cannot marshal teardown");
			return;
		}

		auto promise = std::make_shared<std::promise<void>> ();
		auto future = promise->get_future ();
		try
		{
			boost::asio::post (service,
				[self, promise] ()
				{
					self->StopOnService ();
					try
					{
						promise->set_value ();
					}
					catch (const std::exception &)
					{
					}
				});
		}
		catch (const std::exception &)
		{
			// The service rejected the task (already destroyed or stopping).
			// Do not fall back to mutating service state from this thread.
			LogPrint (eLogWarning, "TorrentsTunnel: Couldn't queue teardown; leaving it to the service owner");
			return;
		}

		if (future.wait_for (TORRENTS_STOP_TIMEOUT) != std::future_status::ready)
			LogPrint (eLogWarning, "TorrentsTunnel: Teardown is still queued on the service thread after ",
				TORRENTS_STOP_TIMEOUT.count (), " ms");
	}

	void TorrentsTunnel::StopOnService ()
	{
		RevokeInboundAccepts ();
		bool expected = false;
		if (!m_Stopping.compare_exchange_strong (expected, true, std::memory_order_acq_rel)) return;
		if (m_Creation.pending)
		{
			m_Creation.pending = false;
			m_Creation.message = "creation cancelled by shutdown";
		}
		if (m_DHT) m_DHT->Stop ();
		auto localDestination = GetLocalDestination ();
		if (localDestination)
		{
			localDestination->StopAcceptingStreams ();
		}
		m_RawDatagramReceiver.Reset ();
		m_TrackerRequestsCheckTimer.cancel ();
		m_KeepAliveCheckTimer.cancel ();
		m_ReconnectCheckTimer.cancel ();
		m_TorrentsStatusUpdateTimer.cancel ();
		CancelAllTrackerRequests ();
		for (auto it : GetTorrents ())
		{
			SaveTorrentAdminState (it, true);
			StopTorrent (it);
		}
		m_PendingDials.clear ();
		{
			std::lock_guard<std::mutex> l (m_TorrentsMutex);
			m_Torrents.clear ();
			m_TorrentsByID.clear ();
			m_AdminState.clear ();
		}
		// The stop announces above were admitted as fresh requests; retire
		// them and the per-torrent memberships so a later Start() begins with
		// the configured common trackers only and no stale reference count
		// survives.
		CancelAllTrackerRequests ();
		{
			// The RPC-facing getters read the slot table from another thread;
			// clearing it under the same registry mutex prevents a torn URL
			// read.
			std::lock_guard<std::mutex> trackerLock (m_TrackerRegistryMutex);
			m_TrackerMemberships.clear ();
			for (size_t i = 0; i < m_TrackerSlots.size (); i++)
			{
				if (m_TrackerSlots[i].isCommon) continue;
				m_TrackerSlots[i] = TrackerSlotState { };
				m_Trackers[i] = TrackerInfo { i2p::http::URL { }, false, 0, 0, 0 };
			}
		}
		m_DiskIOService.Stop ();
		if (m_DHTStorageWorker) m_DHTStorageWorker->Stop ();
		if (m_StorageWorker) m_StorageWorker->Stop ();
		i2p::client::I2PService::ClearHandlers (); // close connections
		i2p::client::I2PService::Stop ();
	}

	void TorrentsTunnel::LoadInitialTorrents ()
	{
		std::shared_ptr<TorrentsTunnel> self;
		try
		{
			self = std::static_pointer_cast<TorrentsTunnel> (shared_from_this ());
		}
		catch (const std::exception &)
		{
			LogPrint (eLogWarning, "TorrentsTunnel: Can't load torrents without a shared owner");
			return;
		}

		try
		{
			boost::asio::post (GetDiskIOService (),
				[self] ()
				{
					// This callback is the storage execution domain. It may
					// inspect and read the configured directory, but it never
					// parses into the live session or opens a network stream.
					try
					{
						std::vector<std::vector<uint8_t>> loaded;
						self->m_ResolvedPersistence.clear ();
						std::vector<std::pair<std::string, std::vector<uint8_t>>> magnets;
						// Recovery is independently bounded; snapshot page limits do
						// not constrain the number of engine records on disk.
						constexpr size_t MAX_STARTUP_ENTRIES = 256, MAX_STARTUP_VISITS = 4096;
						constexpr size_t MAX_STARTUP_BYTES = 64 * 1024 * 1024;
						size_t bytesRead = 0, visits = 0;
						std::error_code ec;
						// Pause policy must be scanned before resolved metainfo.
						bool recoveryPolicyComplete = false;
						const auto stateRoot = self->m_TorrentsDir / ".openi2pd-state";
						if (self->IsContainedPath (stateRoot, true))
						{
							ec.clear ();
							std::filesystem::directory_iterator it (stateRoot, ec), end;
							const bool missingPolicy = ec == std::errc::no_such_file_or_directory;
							while (!ec && it != end && visits < MAX_STARTUP_VISITS
								&& loaded.size () + magnets.size () < MAX_STARTUP_ENTRIES
								&& bytesRead < MAX_STARTUP_BYTES)
							{
								++visits;
								const auto path = it->path ();
								std::error_code statusError;
								const auto status = it->symlink_status (statusError);
								if (path.extension () == ".magnet")
								{
									std::vector<uint8_t> bytes;
									std::string error;
									if (statusError || !std::filesystem::is_regular_file (status)
										|| std::filesystem::is_symlink (status)
										|| !ReadFileBoundedNoFollow (self->m_TorrentsDir,
											path.lexically_relative (self->m_TorrentsDir),
											std::min (MAX_TORRENT_MAGNET_STATE_BYTES, MAX_STARTUP_BYTES - bytesRead),
											bytes, error))
										bytes.clear ();
									bytesRead += bytes.size ();
									magnets.emplace_back (path.filename ().string (), std::move (bytes));
								}
								it.increment (ec);
							}
							recoveryPolicyComplete = (!ec && it == end) || missingPolicy;
						}
						ec.clear ();
						if (recoveryPolicyComplete && !self->m_TorrentsDir.empty ()
							&& std::filesystem::is_directory (self->m_TorrentsDir, ec))
						{
							std::filesystem::directory_iterator it (self->m_TorrentsDir, ec);
							std::filesystem::directory_iterator end;
							if (ec)
								LogPrint (eLogError, "TorrentsTunnel: Can't enumerate torrents directory ",
									self->m_TorrentsDir, " : ", ec.message ());
							while (!ec && it != end)
							{
								if (++visits > MAX_STARTUP_VISITS
									|| loaded.size () + magnets.size () >= MAX_STARTUP_ENTRIES
									|| bytesRead >= MAX_STARTUP_BYTES)
									break;
								auto path = it->path ();
								std::error_code statusError;
								auto status = it->symlink_status (statusError);
								// Inspect entries without following them. A symlinked
								// .torrent is never loaded from outside the root.
								if (!statusError && !std::filesystem::is_symlink (status)
									&& std::filesystem::is_regular_file (status) && path.extension () == ".torrent"
									&& self->IsContainedPath (path, false))
								{
									auto relative = path.lexically_relative (self->m_TorrentsDir);
									std::vector<uint8_t> content;
									std::string error;
									if (!relative.empty () && IsSafeRelativePath (relative)
										&& ReadFileBoundedNoFollow (self->m_TorrentsDir, relative,
											std::min (MAX_CONTROL_FILE_BYTES, MAX_STARTUP_BYTES - bytesRead), content,
											error))
									{
										bytesRead += content.size ();
										loaded.push_back (std::move (content));
									}
									else
										LogPrint (eLogError, "TorrentsTunnel: Can't read ", path, ": ",
											error.empty () ? "unsafe torrent file path" : error);
								}
								it.increment (ec);
							}
							if (ec)
								LogPrint (eLogError, "TorrentsTunnel: Error while enumerating torrents directory ",
									self->m_TorrentsDir, " : ", ec.message ());
						}
						else if (ec && ec != std::errc::no_such_file_or_directory)
							LogPrint (eLogError, "TorrentsTunnel: Can't inspect torrents directory ",
								self->m_TorrentsDir, " : ", ec.message ());
						if (!recoveryPolicyComplete || visits >= MAX_STARTUP_VISITS
							|| loaded.size () + magnets.size () >= MAX_STARTUP_ENTRIES
							|| bytesRead >= MAX_STARTUP_BYTES)
							LogPrint (eLogWarning,
								"TorrentsTunnel: Startup recovery budget reached; additional records remain on disk");

						if (self->m_Stopping.load (std::memory_order_acquire)) return;
						// Hand only bounded byte buffers to the network/control
						// service. Insertion and tracker scheduling stay there.
						boost::asio::post (self->GetService (),
							[self, loaded = std::move (loaded), magnets = std::move (magnets)] () mutable
							{
								if (self->m_Stopping.load (std::memory_order_acquire)) return;
								try
								{
									std::vector<TorrentMagnetState> records;
									for (const auto & [name, bytes] : magnets)
									{
										if (name.size () != 47 || !name.ends_with (".magnet")) continue;
										auto expected
											= ParseTorrentMagnet ("magnet:?xt=urn:btih:" + name.substr (0, 40));
										if (!expected || name != ToHexString (expected->infoHash) + ".magnet") continue;
										TorrentMagnetState state;
										std::string error;
										const bool valid
											= DecodeTorrentMagnetState (
												  std::string_view (
													  reinterpret_cast<const char *> (bytes.data ()), bytes.size ()),
												  state, error)
											&& state.infoHash == expected->infoHash;
										self->m_MagnetRecoveryPause[expected->infoHash] = !valid || state.stopped;
										if (!valid)
										{
											state = { expected->infoHash, ToHexString (expected->infoHash), { }, true };
											LogPrint (eLogWarning,
												"TorrentsTunnel: Invalid magnet recovery record retained paused");
										}
										records.push_back (std::move (state));
									}
									for (const auto & content : loaded)
										(void)self->AddTorrent (std::string_view (
											reinterpret_cast<const char *> (content.data ()), content.size ()));
									for (const auto & state : records) (void)self->AddMagnetState (state, true);
									self->m_MagnetRecoveryPause.clear ();
									self->m_StartupRecoveryPending.store (false);
									for (const auto & torrent : self->GetTorrents ())
										self->DiscoverAddedTorrent (torrent);
								}
								catch (const std::exception & ex)
								{
									LogPrint (eLogError, "TorrentsTunnel: Can't insert initial torrent: ", ex.what ());
								}
							});
					}
					catch (const std::exception & ex)
					{
						LogPrint (eLogError, "TorrentsTunnel: Initial torrent scan failed: ", ex.what ());
					}
				});
		}
		catch (const std::exception & ex)
		{
			LogPrint (eLogError, "TorrentsTunnel: Can't queue initial torrent scan: ", ex.what ());
		}
	}

	bool TorrentsTunnel::ReadTorrentFileForControl (
		const std::filesystem::path & path, std::vector<uint8_t> & content, std::string & error)
	{
		// A control command runs on the network service thread. File
		// enumeration, size checks and the actual read belong to the disk
		// service; the network thread only waits for the bounded result.
		struct Result
		{
			bool ok = false;
			std::vector<uint8_t> content;
			std::string error;
		};
		auto result = std::make_shared<Result> ();
		auto promise = std::make_shared<std::promise<void>> ();
		auto future = promise->get_future ();
		try
		{
			boost::asio::post (GetDiskIOService (),
				[path, result, promise] ()
				{
					try
					{
						if (path.extension () != ".torrent")
							result->error = "not a .torrent file";
						else
						{
							// The control command names an arbitrary user path. Its
							// parent directory may legitimately be a symlink, so
							// resolve the parent but never the leaf: the final
							// component is still opened with O_NOFOLLOW.
							std::error_code ec;
							auto resolved = path;
							auto parent = std::filesystem::weakly_canonical (path.parent_path (), ec);
							if (!ec && !parent.empty ()) resolved = parent / path.filename ();
							result->ok = ReadFileBoundedNoFollow (
								resolved, MAX_CONTROL_FILE_BYTES, result->content, result->error);
						}
					}
					catch (const std::exception & ex)
					{
						result->ok = false;
						result->content.clear ();
						result->error = std::string ("can't read torrent file: ") + ex.what ();
					}
					promise->set_value ();
				});
		}
		catch (const std::exception &)
		{
			error = "can't queue torrent file read";
			return false;
		}
		if (future.wait_for (CONTROL_DEFAULT_TIMEOUT) != std::future_status::ready)
		{
			error = "torrent file read timed out";
			return false;
		}
		content = std::move (result->content);
		error = std::move (result->error);
		return result->ok;
	}

	bool TorrentsTunnel::IsContainedPath (const std::filesystem::path & path, bool allowDirectoryLeaf) const
	{
		return IsPathWithinRoot (m_TorrentsDir, path, allowDirectoryLeaf);
	}

	bool TorrentsTunnel::SetTorrentPathWithinRoot (const std::shared_ptr<Torrent> & torrent) const
	{
		if (!torrent || torrent->GetName () == ".openi2pd-state") return false;
		auto fullPath = m_TorrentsDir / std::filesystem::path (torrent->GetName ());
		// This method runs on the network/control service thread. Only use
		// lexical path operations here; the storage service revalidates with
		// IsPathWithinRoot and the no-follow helpers before any file access.
		if (!IsPathLexicallyWithinRoot (m_TorrentsDir, fullPath))
		{
			LogPrint (eLogError, "TorrentsTunnel: Refusing torrent name outside ", m_TorrentsDir);
			return false;
		}
		torrent->SetFullPath (fullPath);
		return true;
	}

	std::string TorrentsTunnel::GetDisplayFilePath (const std::filesystem::path & path) const
	{
		if (path.empty ()) return "";
		auto relative = path.lexically_relative (m_TorrentsDir);
		if (!relative.empty () && !relative.is_absolute () && relative != "." && *relative.begin () != "..")
			return relative.generic_string ();
		return path.filename ().string ();
	}

	bool TorrentsTunnel::SaveTorrentFile (std::shared_ptr<Torrent> torrent, const std::string & content)
	{
		if (!torrent || torrent->IsRemoved () || FindTorrent (torrent->GetInfoHash ()) != torrent) return false;
		auto torrentFilePath = torrent->GetFullPath ();
		torrentFilePath += ".torrent";
		if (!IsContainedPath (torrentFilePath, false))
		{
			LogPrint (eLogError, "TorrentsTunnel: Refusing to save torrent file outside ", m_TorrentsDir);
			return false;
		}
		auto relative = torrentFilePath.lexically_relative (m_TorrentsDir);
		std::string error;
		bool saved = false;
		if (m_StorageWorker)
		{
			saved = !content.empty ()
				&& m_StorageWorker->ReplaceMetadata (
					relative, { reinterpret_cast<const uint8_t *> (content.data ()), content.size () }, error);
			if (!saved) LogPrint (eLogError, "TorrentsTunnel: Isolated metainfo persistence failed: ", error);
		}
		else
		{
			saved = !content.empty () && !relative.empty () && IsSafeRelativePath (relative)
				&& CreateDirectoriesWithinRootNoFollow (m_TorrentsDir, relative.parent_path ())
				&& WriteFileAtomicNoFollow (m_TorrentsDir, relative, content.data (), content.size (), error);
			if (!saved) LogPrint (eLogError, "TorrentsTunnel: Metainfo persistence failed");
		}
		if (saved) RecordResolvedPersistence (torrent, true);
		return saved;
	}

	void TorrentsTunnel::RecordResolvedPersistence (const std::shared_ptr<Torrent> & torrent, bool metainfo)
	{
		// One disk executor serializes these flags with immutable snapshots.
		// A resolved file alone must never retire a paused magnet's fallback.
		std::erase_if (m_ResolvedPersistence, [] (const auto & entry) { return entry.second.owner.expired (); });
		auto & record = m_ResolvedPersistence[torrent->GetInfoHash ()];
		if (record.owner.lock () != torrent) record = { torrent, false, false };
		if (metainfo)
			record.metainfo = true;
		else
			record.settings = true;
		if (!record.metainfo || !record.settings || torrent->IsRemoved ()) return;
		const auto magnet
			= std::filesystem::path (".openi2pd-state") / (ToHexString (torrent->GetInfoHash ()) + ".magnet");
		std::string error;
		const bool removed = m_StorageWorker ? m_StorageWorker->RemoveMetadata (magnet, error)
											 : RemoveFileNoFollow (m_TorrentsDir, magnet, error);
		if (!removed) LogPrint (eLogWarning, "TorrentsTunnel: Resolved magnet cleanup remains pending");
	}

	void SetTorrentPayloadStorageFactory (TorrentPayloadStorageFactory factory)
	{
		std::lock_guard lock (payloadFactoryMutex);
		payloadFactory = std::move (factory);
	}

	void TorrentsTunnel::InitTorrentFiles (std::shared_ptr<Torrent> torrent)
	{
		if (!torrent || torrent->IsRemoved () || torrent->GetError () == eTorrentErrorMalformedMetaInfo) return;
		if (!IsContainedPath (torrent->GetFullPath (), true))
		{
			LogPrint (eLogError, "TorrentsTunnel: Refusing to initialise torrent outside ", m_TorrentsDir);
			torrent->SetError (eTorrentErrorFilesystem);
			return;
		}

		TorrentPayloadStorageFactory factory;
		{
			std::lock_guard lock (payloadFactoryMutex);
			factory = payloadFactory;
		}
		auto payload = factory ? factory (m_Name, ToHexString (torrent->GetInfoHash ())) : nullptr;
		const bool documentStorage = payload && payload->UsesFinalNames ();
		if (m_StorageWorker)
		{
			auto relativeResume = torrent->GetFullPath ().lexically_relative (m_TorrentsDir);
			relativeResume += ".resume";
			torrent->SetResumePersistence (
				[worker = m_StorageWorker, relativeResume] (std::span<const uint8_t> bytes, bool remove)
				{
					std::string error;
					return remove ? worker->RemoveMetadata (relativeResume, error)
								  : worker->ReplaceMetadata (relativeResume, bytes, error);
				});
		}
		const size_t fileCount = torrent->GetFiles ().size ();
		std::vector<bool> presentFiles (fileCount, false);
		size_t fileIndex = 0;
		for (auto it : torrent->GetFiles ())
		{
			const size_t currentFile = fileIndex++;
			if (payload)
				it->SetPayloadStorage (payload, m_TorrentsDir);
			else if (m_StorageWorker)
				it->SetStorageWorker (m_StorageWorker, m_TorrentsDir);
			if (torrent->IsSingleFile ())
			{
				it->SetFullPath (torrent->GetFullPath ());
				it->SetRootDir (m_TorrentsDir);
			}
			else if (!it->UpdateFullPath (torrent->GetFullPath ()))
			{
				LogPrint (eLogError, "TorrentsTunnel: Refusing unsafe file path in ", torrent->GetName ());
				torrent->SetError (eTorrentErrorFilesystem);
				continue;
			}
			if (!IsContainedPath (it->GetFullFilePath (), false))
			{
				LogPrint (eLogError, "TorrentsTunnel: Refusing to create file outside ", m_TorrentsDir);
				torrent->SetError (eTorrentErrorFilesystem);
				continue;
			}
			// Documents have no POSIX payload path and require no .part rename.
			// Probe each document through the backend instead of assuming it
			// exists: a deleted document or a revoked grant must leave that
			// file missing so the piece can be re-downloaded, while a present
			// exact-size document is still hashed before it is published.
			if (documentStorage)
			{
				it->SetIsPart (false);
				if (!it->GetFileLength ())
				{
					presentFiles[currentFile] = true;
					continue;
				}
				presentFiles[currentFile] = it->ProbePayload ();
				if (!presentFiles[currentFile])
					LogPrint (eLogInfo, "Torrents: SAF document unavailable; treating file as missing");
				continue;
			}
			std::error_code ec;
			bool finalExists = std::filesystem::exists (it->GetFullFilePath (), ec);
			if (ec)
			{
				LogPrint (eLogError, "TorrentsTunnel: Can't inspect ", it->GetFullFilePath (), " : ", ec.message ());
				torrent->SetError (eTorrentErrorFilesystem);
				continue;
			}
			if (!finalExists)
			{
				it->SetIsPart (true);
				auto partFilePath = it->GetFullFilePath ();
				partFilePath += ".part";
				ec.clear ();
				bool partExists = std::filesystem::exists (partFilePath, ec);
				if (ec)
				{
					LogPrint (eLogError, "TorrentsTunnel: Can't inspect ", partFilePath, " : ", ec.message ());
					torrent->SetError (eTorrentErrorFilesystem);
				}
				else if (!partExists)
				{
					// A freshly created part file is empty: it is prepared but
					// not "present" data, so it keeps the original
					// verification contract. The disk worker owns file
					// preparation, so a failure is published through the
					// atomic error state read by the service thread.
					if (!CreateAndReserveFile (partFilePath, it->GetFileLength ()))
					{
						LogPrint (eLogError, "TorrentsTunnel: Can't create part file ", partFilePath);
						torrent->SetError (eTorrentErrorFilesystem);
					}
				}
				else
					presentFiles[currentFile] = true;
			}
			else
			{
				// Data that already exists under its final name is read from
				// the final path (no .part rename) but is still unverified.
				it->SetIsPart (false);
				presentFiles[currentFile] = true;
			}
		}
		// Document backends do not preallocate or rename. Only the documents
		// that responded to the exact-size probe are marked present, so the
		// bounded verification chain hashes existing files and leaves pieces
		// spanning a missing/revoked document downloadable. Never trust a
		// cached bitfield or block the disk worker with a full rehash.
		if (std::any_of (presentFiles.begin (), presentFiles.end (), [] (bool value) { return value; }))
		{
			// Some files may already exist under final or .part names after an
			// interrupted promotion or a partial restore. Hash exactly the
			// pieces fully backed by existing files; pieces spanning a missing
			// file remain downloadable instead of forcing a full re-download.
			const auto hash = torrent->GetInfoHash ();
			boost::asio::post (GetService (),
				[this, torrent, hash, presentFiles = std::move (presentFiles)] ()
				{
					if (m_Stopping || FindTorrent (hash) != torrent) return;
					if (torrent->RequestLocalVerificationForExistingFiles (presentFiles))
						ScheduleLocalVerification (torrent);
				});
		}

		auto resumeFilePath = torrent->GetFullPath ();
		resumeFilePath += ".resume";
		if (!IsContainedPath (resumeFilePath, false))
		{
			LogPrint (eLogError, "TorrentsTunnel: Refusing to access resume file outside ", m_TorrentsDir);
			torrent->SetError (eTorrentErrorFilesystem);
			return;
		}
		std::error_code resumeStatusError;
		bool resumeExists = std::filesystem::exists (resumeFilePath, resumeStatusError);
		if (resumeStatusError)
			LogPrint (eLogWarning, "TorrentsTunnel: Can't inspect resume file ", resumeFilePath, " : ",
				resumeStatusError.message ());
		if (resumeExists)
		{
			if (!torrent->IsComplete ())
			{
				// A resume file is derived data. Only the exact bitfield for
				// this torrent is acceptable and reads are bounded by that
				// size; the read itself never follows a symlink leaf.
				size_t maxResumeSize = (torrent->GetNumPieces () + 7) / 8;
				std::vector<uint8_t> bitfield;
				std::string error;
				bool invalid = false;
				auto relativeResume = resumeFilePath.lexically_relative (m_TorrentsDir);
				if (relativeResume.empty () || !IsSafeRelativePath (relativeResume)
					|| !ReadFileBoundedNoFollow (m_TorrentsDir, relativeResume, maxResumeSize, bitfield, error))
				{
					if (error.empty ()) error = "unsafe resume file path";
					LogPrint (eLogWarning, "TorrentsTunnel: Ignoring resume file ", resumeFilePath, " : ", error);
					invalid = true;
				}
				if (invalid)
				{
					std::error_code ec;
					if (m_StorageWorker)
						(void)m_StorageWorker->RemoveMetadata (
							resumeFilePath.lexically_relative (m_TorrentsDir), error);
					else
						std::filesystem::remove (resumeFilePath, ec);
				}
				else
				{
					// A resume bitfield only says which blocks were written. It
					// is applied on the service thread, the owner of the piece
					// state that peers observe, and every restored piece is
					// marked for local hashing before availability is published.
					const auto hash = torrent->GetInfoHash ();
					boost::asio::post (GetService (),
						[this, torrent, hash, relativeResume, bitfield = std::move (bitfield)] ()
						{
							if (m_Stopping || FindTorrent (hash) != torrent) return;
							if (!torrent->IsBitfieldValid (bitfield))
							{
								LogPrint (eLogWarning, "TorrentsTunnel: Ignoring invalid resume bitfield for ",
									torrent->GetFullPath ());
								boost::asio::post (GetDiskIOService (),
									[this, relativeResume] ()
									{
										std::string removeError;
										if (m_StorageWorker)
											(void)m_StorageWorker->RemoveMetadata (relativeResume, removeError);
										else if (!RemoveFileNoFollow (m_TorrentsDir, relativeResume, removeError))
											LogPrint (eLogWarning,
												"TorrentsTunnel: Can't remove invalid resume file: ", removeError);
									});
								return;
							}
							torrent->ApplyBitfield (bitfield);
							torrent->RequestLocalVerification (false);
							LogPrint (eLogInfo, "TorrentsTunnel: Resume bitfield applied for ", torrent->GetFullPath (),
								"; verifying local data before availability is published");
							ScheduleLocalVerification (torrent);
						});
				}
			}
			else
			{
				std::error_code ec;
				if (m_StorageWorker)
				{
					std::string error;
					(void)m_StorageWorker->RemoveMetadata (resumeFilePath.lexically_relative (m_TorrentsDir), error);
				}
				else
					std::filesystem::remove (resumeFilePath, ec);
				if (ec)
					LogPrint (
						eLogWarning, "TorrentsTunnel: Can't remove resume file ", resumeFilePath, " : ", ec.message ());
			}
		}
	}

	void TorrentsTunnel::LoadTorrentAdminState (const std::shared_ptr<Torrent> & torrent)
	{
		if (!torrent || torrent->IsRemoved ()) return;
		auto self = std::static_pointer_cast<TorrentsTunnel> (shared_from_this ());
		const auto hash = torrent->GetInfoHash ();
		const bool fallbackStopped
			= m_MagnetRecoveryPause.contains (hash) ? m_MagnetRecoveryPause.at (hash) : torrent->IsStopped ();
		auto & status = m_AdminState[hash];
		status = { };
		// Do not announce before the persisted pause/privacy policy is
		// loaded.
		torrent->SetStopped (true);
		const auto relative = std::filesystem::path (".openi2pd-state") / (ToHexString (hash) + ".state");
		boost::asio::post (GetDiskIOService (),
			[self, torrent, relative, hash, fallbackStopped]
			{
				std::vector<uint8_t> bytes;
				std::string error;
				std::error_code ec;
				auto fileStatus = std::filesystem::symlink_status (self->m_TorrentsDir / relative, ec);
				if (ec && ec != std::errc::no_such_file_or_directory) error = "cannot inspect torrent settings";
				if (!ec && std::filesystem::exists (fileStatus))
				{
					(void)ReadFileBoundedNoFollow (
						self->m_TorrentsDir, relative, MAX_TORRENT_LOCAL_STATE_BYTES, bytes, error);
					if (bytes.empty () && error.empty ()) error = "empty torrent settings";
				}
				boost::asio::post (self->GetService (),
					[self, torrent, hash, fallbackStopped, bytes = std::move (bytes),
						error = std::move (error)] () mutable
					{
						if (self->m_Stopping || self->FindTorrent (hash) != torrent) return;
						auto & status = self->m_AdminState[hash];
						status.loaded = true;
						// Local commands issued during the read take precedence.
						if (status.generation)
						{
							self->SaveTorrentAdminState (torrent);
							return;
						}
						TorrentLocalState state;
						state.stopped = fallbackStopped;
						bool valid = error.empty ();
						if (valid && !bytes.empty ())
							valid
								= DecodeTorrentLocalState (
									  std::string_view (reinterpret_cast<const char *> (bytes.data ()), bytes.size ()),
									  i2p::util::GetSecondsSinceEpoch (), state, error)
								&& state.priorities.size () == torrent->GetFiles ().size ();
						if (valid && !bytes.empty ())
						{
							valid = torrent->SetFilePriorities (state.priorities);
							if (valid)
							{
								torrent->GetCommentStore () = std::move (state.comments);
								torrent->SetCommentsEnabled (
									torrent->GetCommentStore ().IsEnabled () && !torrent->IsPrivate ());
							}
						}
						status.error = valid
							? std::string { }
							: (error.empty () ? "invalid torrent settings; explicit resume required" : error);
						status.commentRevision = torrent->GetCommentStore ().Revision ();
						torrent->SetStopped (!valid || state.stopped);
						if (valid) self->SaveTorrentAdminState (torrent);
						if (status.initialDiscoveryPending)
							self->DiscoverAddedTorrent (torrent);
						else if (!torrent->IsStopped ())
							self->RequestTorrentTrackers (torrent, eTrackerAnnounceEventStarted);
					});
			});
	}

	void TorrentsTunnel::SaveTorrentAdminState (const std::shared_ptr<Torrent> & torrent, bool final)
	{
		if (!torrent || torrent->IsRemoved ()) return;
		const bool magnet = !torrent->IsValid ();
		auto & status = m_AdminState[torrent->GetInfoHash ()];
		if (magnet) status.loaded = true;
		++status.generation;
		status.dirty = true;
		if (!status.loaded || (status.pending && !final)) return;
		TorrentLocalState state;
		state.stopped = torrent->IsStopped ();
		state.comments = torrent->GetCommentStore ();
		state.priorities = torrent->GetFilePriorities ();
		std::string error;
		std::string content;
		if (magnet)
		{
			TorrentMagnetState record { torrent->GetInfoHash (), std::string (torrent->GetName ()), { },
				torrent->IsStopped () };
			for (const auto & tier : torrent->GetTrackerTiers ())
				for (const auto & tracker : tier) record.trackers.push_back (tracker);
			content = EncodeTorrentMagnetState (record, error);
		}
		else
			content = EncodeTorrentLocalState (state, error);
		if (content.empty ())
		{
			status.error = error;
			return;
		}
		status.pending = true;
		status.dirty = false;
		status.commentRevision = torrent->GetCommentStore ().Revision ();
		const auto hash = torrent->GetInfoHash ();
		const auto generation = status.generation;
		auto self = std::static_pointer_cast<TorrentsTunnel> (shared_from_this ());
		const auto relative
			= std::filesystem::path (".openi2pd-state") / (ToHexString (hash) + (magnet ? ".magnet" : ".state"));
		try
		{
			boost::asio::post (GetDiskIOService (),
				[self, torrent, hash, relative, generation, final, magnet, content = std::move (content)]
				{
					// Only shutdown's final immutable snapshot may outlive map
					// membership. Removal revokes even that authority permanently.
					if (torrent->IsRemoved ()
						|| (self->FindTorrent (hash) != torrent
							&& !(final && self->m_Stopping.load (std::memory_order_acquire))))
						return;
					std::string error;
					bool ok = self->m_StorageWorker
						? self->m_StorageWorker->ReplaceMetadata (
							  relative, { reinterpret_cast<const uint8_t *> (content.data ()), content.size () }, error)
						: (CreateDirectoriesWithinRootNoFollow (self->m_TorrentsDir, relative.parent_path ())
							  && WriteFileAtomicNoFollow (
								  self->m_TorrentsDir, relative, content.data (), content.size (), error));
					if (ok && !magnet) self->RecordResolvedPersistence (torrent, false);
					boost::asio::post (self->GetService (),
						[self, torrent, hash, generation, magnet, ok, error = std::move (error)]
						{
							if (self->m_Stopping || self->FindTorrent (hash) != torrent) return;
							if (magnet && torrent->IsValid ()) return;
							auto & status = self->m_AdminState[hash];
							status.pending = false;
							status.error
								= ok ? std::string { } : (error.empty () ? "cannot persist torrent settings" : error);
							if (!ok) status.dirty = true;
							// Coalesced local updates get another immutable write; failures
							// retry only on the bounded status timer, never a hot loop.
							if (ok && status.generation != generation)
								self->SaveTorrentAdminState (torrent);
							else if (ok)
							{
								if (magnet) status.initialDiscoveryReady = true;
								self->DiscoverAddedTorrent (torrent);
							}
						});
				});
		}
		catch (...)
		{
			status.pending = false;
			status.dirty = true;
			status.error = "cannot queue torrent settings";
		}
	}

	bool TorrentsTunnel::CreateAndReserveFile (const std::filesystem::path & filePath, size_t reserve)
	{
		if (!IsContainedPath (filePath, false))
		{
			LogPrint (eLogError, "TorrentsTunnel: Refusing to create file outside ", m_TorrentsDir);
			return false;
		}
		std::error_code statusError;
		bool exists = std::filesystem::exists (filePath, statusError);
		if (statusError || exists)
		{
			if (statusError)
				LogPrint (eLogError, "TorrentsTunnel: Can't inspect ", filePath, " : ", statusError.message ());
			return false;
		}

		auto relative = filePath.lexically_relative (m_TorrentsDir);
		if (relative.empty () || !IsSafeRelativePath (relative))
		{
			LogPrint (eLogError, "TorrentsTunnel: Refusing unsafe file path ", filePath);
			return false;
		}
		if (m_StorageWorker)
		{
			// Allocation belongs to the storage capability, not to the network-
			// capable parent. Failure never retries through direct filesystem
			// IO.
			std::string error;
			const auto handle = m_StorageWorker->Register (relative, reserve, error, true);
			if (!handle)
			{
				LogPrint (eLogError, "TorrentsTunnel: Storage allocation failed: ", error);
				return false;
			}
			return m_StorageWorker->Release (handle, error);
		}
		// Create every missing directory with mkdirat/openat(O_NOFOLLOW) on
		// POSIX instead of create_directories(), which would follow a symlink
		// planted in an intermediate component. On Windows the components are
		// checked for reparse points before creation.
		if (!CreateDirectoriesWithinRootNoFollow (m_TorrentsDir, relative.parent_path ()))
		{
			LogPrint (eLogError, "TorrentsTunnel: Can't create subdirs for ", filePath);
			return false;
		}
		if (!CreateFileWithSizeNoFollow (m_TorrentsDir, relative, reserve))
		{
			LogPrint (eLogError, "TorrentsTunnel: Can't create file ", filePath);
			return false;
		}
		if (!IsContainedPath (filePath, false))
		{
			std::error_code ec;
			std::filesystem::remove (filePath, ec);
			LogPrint (eLogError, "TorrentsTunnel: Refusing to use changed file path ", filePath);
			return false;
		}
		return true;
	}

	void TorrentsTunnel::CompleteTorrent (std::shared_ptr<Torrent> torrent)
	{
		auto self = std::static_pointer_cast<TorrentsTunnel> (weak_from_this ().lock ());
		if (!self || self->m_Stopping || !torrent || torrent->IsRemoved ()
			|| self->FindTorrent (torrent->GetInfoHash ()) != torrent)
			return;
		// Selection belongs to the service thread. Capture its immutable
		// admission snapshot instead of reading priority vectors on disk.
		// Recheck both selection and verification before publishing success.
		if (!torrent->AreAllSelectedPiecesVerified ()) return;
		const auto priorities = torrent->GetFilePriorities ();
		std::vector<std::pair<size_t, std::shared_ptr<TorrentFile>>> selectedFiles;
		size_t fileIndex = 0;
		for (const auto & file : torrent->GetFiles ())
		{
			const auto index = fileIndex++;
			if (torrent->GetFilePriority (index)) selectedFiles.emplace_back (index, file);
		}
		// Queued work must not keep the tunnel and its executor alive in
		// a cycle when the destination stops without draining its queue.
		const std::weak_ptr<TorrentsTunnel> weakSelf = self;
		boost::asio::post (self->GetDiskIOService (),
			[weakSelf, torrent, priorities, selectedFiles = std::move (selectedFiles)] ()
			{
				auto self = weakSelf.lock ();
				if (!self) return;
				// FindTorrent serializes its registry lookup with removal.
				// Exact object identity also rejects a later run's same hash.
				// This check does not cancel an already executing disk call.
				if (self->m_Stopping || torrent->IsRemoved () || self->FindTorrent (torrent->GetInfoHash ()) != torrent)
					return;
				bool completed = true;
				for (const auto & [currentFile, it] : selectedFiles)
				{
					if (self->m_Stopping || torrent->IsRemoved ()
						|| self->FindTorrent (torrent->GetInfoHash ()) != torrent)
						return;
					if (!it->GetFileLength ())
					{
						it->Complete ();
						continue;
					}
					if (!torrent->IsFileComplete (currentFile))
					{
						completed = false;
						continue;
					}
					if (it->HasFinalNameStorage ())
					{
						it->Complete ();
						continue;
					}
					auto partFilePath = it->GetFullFilePath ();
					partFilePath += ".part";
					if (self->m_StorageWorker)
					{
						it->Close ();
						std::string error;
						if (self->m_StorageWorker->Promote (partFilePath.lexically_relative (self->m_TorrentsDir),
								it->GetFullFilePath ().lexically_relative (self->m_TorrentsDir), it->GetFileLength (),
								error))
							it->Complete ();
						else
						{
							completed = false;
							LogPrint (eLogError, "TorrentsTunnel: Isolated promotion failed: ", error);
						}
						continue;
					}
					// Recheck both ends immediately before the rename. A part
					// file that became a symlink is not a completed download,
					// and a symlinked final path is never replaced.
					if (!self->IsContainedPath (partFilePath, false)
						|| !self->IsContainedPath (it->GetFullFilePath (), false))
					{
						completed = false;
						LogPrint (eLogError, "TorrentsTunnel: Refusing to rename through unsafe path ", partFilePath);
						continue;
					}
					if (std::filesystem::exists (partFilePath))
					{
						it->Close (); // make sure file is closed before renaming
						std::error_code ec;
						std::filesystem::rename (partFilePath, it->GetFullFilePath (), ec);
						if (!ec)
							it->Complete ();
						else
						{
							completed = false;
							LogPrint (eLogError, "TorrentsTunnel: Can't rename ", partFilePath);
						}
					}
					else if (self->IsContainedPath (it->GetFullFilePath (), false)
						&& std::filesystem::exists (it->GetFullFilePath ()))
					{
						// The data was already under its final name (for example
						// rechecked after a restart). Every piece was hashed from
						// that file, so there is nothing to rename.
						it->Complete ();
					}
					else
					{
						// Neither the final name nor its .part payload exists,
						// so the pieces this file claims cannot be backed by
						// data on disk. Fail closed: report the storage error
						// and make the covering pieces downloadable again
						// instead of publishing the file (and the torrent) as
						// complete with missing payload.
						completed = false;
						LogPrint (eLogError, "TorrentsTunnel: ", partFilePath,
							" doesn't exist; withholding completion of ", it->GetFullFilePath ());
						torrent->SetError (eTorrentErrorFilesystem);
						// Piece state belongs to the service thread; the disk
						// worker only reports the failure.
						const auto hash = torrent->GetInfoHash ();
						boost::asio::post (self->GetService (),
							[weakSelf, torrent, hash, currentFile] ()
							{
								auto self = weakSelf.lock ();
								if (!self) return;
								if (self->m_Stopping || self->FindTorrent (hash) != torrent) return;
								if (torrent->InvalidateFilePieces (currentFile))
								{
									LogPrint (eLogError,
										"TorrentsTunnel: Marked pieces for re-download after a missing payload in ",
										torrent->GetFullPath ());
									// Persist the withdrawn pieces, so the stale
									// bitmap cannot survive a restart even
									// before the next successful write.
									boost::asio::post (self->GetDiskIOService (),
										[weakSelf, torrent, hash] ()
										{
											auto self = weakSelf.lock ();
											if (!self) return;
											if (self->m_Stopping || self->FindTorrent (hash) != torrent) return;
											torrent->SaveTorrentResumeFile ();
										});
								}
							});
					}
				}

				if (completed)
				{
					// Publish only to the same live session and selection.
					// Piece invalidation or a priority edit during promotion
					// requires another pass instead of stale completion.
					boost::asio::post (self->GetService (),
						[weakSelf, torrent, priorities] ()
						{
							auto self = weakSelf.lock ();
							if (!self) return;
							if (self->m_Stopping || torrent->IsRemoved ()
								|| self->FindTorrent (torrent->GetInfoHash ()) != torrent
								|| priorities != torrent->GetFilePriorities ()
								|| !torrent->AreAllSelectedPiecesVerified ())
								return;
							LogPrint (eLogInfo, "TorrentsTunnel: Download complete ", torrent->GetFullPath ());
							torrent->SetSelectedComplete ();
							// The resume bitfield remains for skipped/unwanted
							// ranges; SaveTorrentResumeFile removes it only when
							// every piece is complete.
							const auto hash = torrent->GetInfoHash ();
							boost::asio::post (self->GetDiskIOService (),
								[weakSelf, torrent, hash] ()
								{
									auto self = weakSelf.lock ();
									if (!self) return;
									if (self->m_Stopping || self->FindTorrent (hash) != torrent) return;
									torrent->SaveTorrentResumeFile ();
								});
							// Tracker parity: event=completed is a whole-torrent
							// statement. A skip-selected torrent is complete for
							// the UI, but its unselected pieces are still missing,
							// so it sends a regular announce now and the completed
							// event only after the last piece has been fetched and
							// verified too. RequestTracker() enforces the same gate
							// defensively through GetAnnounceProgress().
							const auto trackerEvent = torrent->GetAnnounceProgress ().completed
								? eTrackerAnnounceEventCompleted
								: eTrackerAnnounceEventNone;
							self->RequestTorrentTrackers (torrent, trackerEvent);
							// close seed connections, reset stats and tell remaining
							// peers that we are no longer interested.
							auto conns = torrent->GetConnections ();
							for (auto it : conns) it->TorrentComplete ();
						});
				}
			});
	}

	void TorrentsTunnel::ScheduleLocalVerification (const std::shared_ptr<Torrent> & torrent)
	{
		if (m_Stopping || !torrent || !torrent->IsLocalVerificationRequested ()) return;
		if (!torrent->BeginLocalVerification ()) return; // a pass is already chained
		VerifyNextLocalPiece (torrent);
	}

	void TorrentsTunnel::VerifyNextLocalPiece (const std::shared_ptr<Torrent> & torrent)
	{
		if (!torrent) return;
		auto self = std::static_pointer_cast<TorrentsTunnel> (weak_from_this ().lock ());
		const auto infoHash = torrent->GetInfoHash ();
		if (!self || m_Stopping || torrent->IsRemoved () || FindTorrent (infoHash) != torrent)
		{
			torrent->EndLocalVerification ();
			return;
		}
		// Only the running handler retains the owner. An abandoned queue
		// must not keep the tunnel and destination executor alive together.
		const std::weak_ptr<TorrentsTunnel> weakSelf = self;
		const int index = torrent->GetNextPieceToVerify ();
		if (index < 0)
		{
			torrent->EndLocalVerification ();
			LogPrint (eLogInfo, "TorrentsTunnel: Local data verification finished for ", torrent->GetFullPath ());
			// Persist the corrected bitfield (a mismatch invalidated its
			// piece). Promotion/completion is decided by CompleteTorrent(),
			// which requires every piece to be verified.
			const auto hash = infoHash;
			boost::asio::post (GetDiskIOService (),
				[weakSelf, torrent, hash] ()
				{
					auto self = weakSelf.lock ();
					if (!self || self->m_Stopping || torrent->IsRemoved () || self->FindTorrent (hash) != torrent)
					{
						torrent->EndLocalVerification ();
						return;
					}
					torrent->SaveTorrentResumeFile ();
					boost::asio::post (self->GetService (),
						[weakSelf, torrent, hash] ()
						{
							auto self = weakSelf.lock ();
							if (!self || self->m_Stopping || torrent->IsRemoved ()
								|| self->FindTorrent (hash) != torrent)
								return;
							// Selection can change while persistence is running.
							// Its priority vectors belong to this service thread.
							if (!torrent->IsComplete () && torrent->AreAllSelectedPiecesVerified ())
								self->CompleteTorrent (torrent);
							self->DiscoverAddedTorrent (torrent);
						});
				});
			return;
		}
		// Exactly one piece per storage job: the pass is bounded in memory
		// (one piece buffer) and the service thread stays responsive between
		// pieces, so tracker/control/peer work is never blocked by a recheck.
		boost::asio::post (GetDiskIOService (),
			[weakSelf, torrent, hash = infoHash, index] ()
			{
				auto self = weakSelf.lock ();
				if (!self || self->m_Stopping || torrent->IsRemoved () || self->FindTorrent (hash) != torrent)
				{
					// EndLocalVerification only clears an atomic activity flag;
					// pending-piece/cursor state remains service-owned. Removal
					// is terminal, and a restarted tunnel has another instance.
					torrent->EndLocalVerification ();
					return;
				}
				Piece & piece = torrent->GetPiece (index);
				piece.SetIsLoading (true);
				bool loaded = true;
				for (auto & fragment : torrent->GetPieceFileFragments (index))
				{
					if (!piece.Load (std::move (fragment)))
					{
						loaded = false;
						break;
					}
				}
				const bool verified = loaded && piece.VerifyHash ();
				piece.SetIsLoading (false);
				if (!verified)
					LogPrint (
						eLogWarning, "TorrentsTunnel: Local piece ", index, " failed verification; marking it corrupt");
				boost::asio::post (self->GetService (),
					[weakSelf, torrent, hash, index, verified] ()
					{
						auto self = weakSelf.lock ();
						if (!self || self->m_Stopping || torrent->IsRemoved () || self->FindTorrent (hash) != torrent)
						{
							torrent->EndLocalVerification ();
							return;
						}
						if (verified)
						{
							torrent->FinishPieceVerification (static_cast<size_t> (index));
							torrent->GetPiece (index).ReleaseData ();
						}
						else
							torrent->InvalidatePiece (static_cast<size_t> (index));
						self->VerifyNextLocalPiece (torrent);
					});
			});
	}

	std::shared_ptr<Torrent> TorrentsTunnel::FindTorrent (const Torrent::InfoHash & infoHash) const
	{
		std::lock_guard<std::mutex> l (m_TorrentsMutex);
		auto it = m_Torrents.find (infoHash);
		if (it != m_Torrents.end ()) return it->second;
		return nullptr;
	}

	std::shared_ptr<Torrent> TorrentsTunnel::FindTorrentByID (int id) const
	{
		std::lock_guard<std::mutex> l (m_TorrentsMutex);
		auto it = m_TorrentsByID.find (id);
		if (it != m_TorrentsByID.end ()) return it->second.lock ();
		return nullptr;
	}

	std::vector<int> TorrentsTunnel::GetTorrentIDs () const
	{
		std::vector<int> ids;
		std::lock_guard<std::mutex> l (m_TorrentsMutex);
		for (const auto & it : m_TorrentsByID)
			if (!it.second.expired ()) ids.push_back (it.first);
		return ids;
	}

	std::list<std::shared_ptr<Torrent>> TorrentsTunnel::GetTorrents () const
	{
		std::list<std::shared_ptr<Torrent>> torrents;
		std::lock_guard<std::mutex> l (m_TorrentsMutex);
		for (const auto & it : m_Torrents) torrents.push_back (it.second);
		return torrents;
	}

	std::optional<TorrentsSnapshot> TorrentsTunnel::GetControlSnapshot (std::chrono::milliseconds timeout)
	{
		std::shared_ptr<TorrentsTunnel> self;
		try
		{
			self = std::static_pointer_cast<TorrentsTunnel> (shared_from_this ());
		}
		catch (const std::exception &)
		{
			return std::nullopt;
		}

		auto promise = std::make_shared<std::promise<std::optional<TorrentsSnapshot>>> ();
		auto future = promise->get_future ();
		try
		{
			boost::asio::post (GetService (),
				[self, promise] ()
				{
					try
					{
						promise->set_value (self->BuildControlSnapshot ());
					}
					catch (...)
					{
						promise->set_exception (std::current_exception ());
					}
				});
		}
		catch (const std::exception &)
		{
			return std::nullopt;
		}

		if (future.wait_for (timeout) != std::future_status::ready) return std::nullopt;
		try
		{
			return future.get ();
		}
		catch (const std::exception &)
		{
			return std::nullopt;
		}
	}

	std::optional<TorrentsSnapshot> TorrentsTunnel::BuildControlSnapshot () const
	{
		TorrentsSnapshot snapshot { };
		snapshot.tunnelName = m_Name;
		snapshot.creation = m_Creation;
		if (m_CreationProgress)
		{
			snapshot.creation.totalBytes = m_CreationProgress->totalBytes.load ();
			snapshot.creation.bytesDone
				= std::min (m_CreationProgress->bytesDone.load (), snapshot.creation.totalBytes);
			snapshot.creation.totalFiles = m_CreationProgress->totalFiles.load ();
			snapshot.creation.filesDone
				= std::min (m_CreationProgress->filesDone.load (), snapshot.creation.totalFiles);
		}
		snapshot.dht.supported = SupportsDHT ();
		snapshot.maxPeersPerTorrent = ClampToInt (MAX_PEERS_PER_TORRENT);
		snapshot.maxTotalPeers = ClampToInt (MAX_TOTAL_PEERS);
		if (m_DHT)
		{
			snapshot.dht.numNodes = ClampToInt (m_DHT->GetNumNodes ());
			snapshot.dht.numBuckets = ClampToInt (m_DHT->GetNumBuckets ());
			snapshot.dht.numTorrents = ClampToInt (m_DHT->GetNumTorrents ());
			snapshot.dht.numHeardFrom = ClampToInt (m_DHT->GetNumHeardFrom ());
			snapshot.dht.numFailedPeers = ClampToInt (m_DHT->GetNumFailedPeers ());
		}

		auto ids = GetTorrentIDs ();
		std::sort (ids.begin (), ids.end ());
		auto nowMonotonic = i2p::util::GetMonotonicMilliseconds ();
		auto nowEpoch = i2p::util::GetMillisecondsSinceEpoch ();

		for (int id : ids)
		{
			if (snapshot.torrents.size () >= MAX_CONTROL_TORRENTS) break;
			auto torrent = FindTorrentByID (id);
			if (!torrent) continue;

			TorrentInfo info { };
			info.id = id;
			info.name = std::string (torrent->GetName ());
			info.status = std::string (TorrentStatusToken (torrent->GetStatus ()));
			info.error = std::string (torrent->GetErrorStr ());
			info.hash = ToHexString (torrent->GetInfoHash ());
			info.magnet = BuildTorrentMagnet (torrent->GetInfoHash (), info.name);
			info.metaComment = std::string (torrent->GetMetaComment ());
			info.createdBy = std::string (torrent->GetCreatedBy ());

			const auto total = torrent->GetLength ();
			const auto wanted = torrent->GetSelectedWantedBytes ();
			const auto left = torrent->GetLeft ();
			const auto have = torrent->GetSelectedVerifiedBytes ();
			info.totalSize = total;
			info.wantedSize = wanted;
			info.have = have;
			info.privateTorrent = torrent->IsPrivate ();
			info.downloaded = torrent->GetDownloaded ();
			info.uploaded = torrent->GetUploaded ();
			info.downRate = torrent->GetDownloadRate ();
			info.upRate = torrent->GetUploadRate ();
			info.peers = ClampToInt (torrent->GetConnections ().size ());
			info.maxPeers = ClampToInt (MAX_PEERS_PER_TORRENT);
			info.percent = wanted ? std::min (100.0, 100.0 * static_cast<double> (have) / static_cast<double> (wanted))
								  : (torrent->IsComplete () ? 100.0 : 0.0);
			if (!torrent->IsComplete () && left && info.downRate)
				info.etaSeconds = (left + info.downRate - 1) / info.downRate;

			int seeders = 0, leechers = 0;
			for (size_t trackerID = 0; trackerID < m_Trackers.size (); trackerID++)
			{
				if (!IsTorrentTracker (trackerID, *torrent)) continue;
				seeders = std::max (seeders, torrent->GetNumSeeders (trackerID));
				leechers = std::max (leechers, torrent->GetNumLeechers (trackerID));
			}
			info.seeders = seeders;
			info.leechers = leechers;
			info.commentsEnabled = torrent->GetCommentStore ().IsEnabled ();
			info.comments = torrent->GetCommentStore ().Snapshot ();
			std::sort (info.comments.begin (), info.comments.end (),
				[] (const i2p::torrents::TorrentComment & left, const i2p::torrents::TorrentComment & right)
				{
					if (left.time != right.time) return left.time > right.time;
					return left.localId > right.localId;
				});
			info.commentCount = info.comments.size ();
			// Classification for clients that cannot see every record at
			// once: TORRENT-DETAIL emits a bounded page, so the operator needs
			// totals to page and to decide whether hidden entries exist.
			for (const auto & comment : info.comments)
			{
				if (comment.hidden)
					++info.commentHiddenCount;
				else
					++info.commentVisibleCount;
				if (comment.local) ++info.commentLocalCount;
			}
			info.ratingCount = torrent->GetCommentStore ().RatingCount ();
			info.averageRating = torrent->GetCommentStore ().AverageRating ();
			if (auto state = m_AdminState.find (torrent->GetInfoHash ()); state != m_AdminState.end ())
				info.commentPersistenceError = state->second.error;

			auto filesCompleted = torrent->GetFilesCompleted ();
			size_t fileIndex = 0;
			for (const auto & file : torrent->GetFiles ())
			{
				if (info.files.size () >= MAX_CONTROL_FILES) break;
				TorrentFileInfo fileInfo { };
				fileInfo.name = GetDisplayFilePath (file->GetFullFilePath ());
				fileInfo.length = file->GetFileLength ();
				fileInfo.completed = fileIndex < filesCompleted.size () ? filesCompleted[fileIndex] : 0;
				fileInfo.priority = torrent->GetFilePriority (fileIndex);
				info.files.push_back (std::move (fileInfo));
				fileIndex++;
			}

			for (const auto & connection : torrent->GetConnections ())
			{
				if (info.peerDetails.size () >= MAX_CONTROL_PEERS) break;
				if (!connection || !connection->IsEstablished ()) continue;

				TorrentPeerInfo peer { };
				auto ident = connection->GetRemoteIdentHash ();
				if (ident) peer.ident = ident->ToBase64 ();
				peer.client = connection->GetRemoteName ().empty () ? PeerClientName (connection->GetRemotePeerID ())
																	: std::string (connection->GetRemoteName ());
				peer.flags = PeerFlags (*connection);
				const auto & bitfield = connection->GetRemoteBitfield ();
				peer.progress = bitfield.size ()
					? static_cast<double> (bitfield.count ()) / static_cast<double> (bitfield.size ())
					: 0.0;
				peer.downRate = connection->GetDownloadRate ();
				peer.upRate = connection->GetUploadRate ();
				info.peerDetails.push_back (std::move (peer));
			}

			for (size_t trackerID = 0; trackerID < m_Trackers.size (); trackerID++)
			{
				if (info.trackers.size () >= MAX_CONTROL_TRACKERS) break;
				if (!IsTorrentTracker (trackerID, *torrent)) continue;

				TorrentTrackerInfo tracker { };
				tracker.announce = GetTrackerAnnounce (trackerID);
				const auto normalized = [] (std::string_view value) { return i2p::http::URL (value).to_string (); };
				int tierIndex = 0;
				for (const auto & tier : torrent->GetTrackerTiers ())
				{
					for (const auto & announce : tier)
						if (normalized (announce) == normalized (tracker.announce))
						{
							tracker.tier = tierIndex;
							break;
						}
					if (tracker.tier >= 0) break;
					++tierIndex;
				}
				tracker.peers = torrent->GetNumPeers (trackerID);
				tracker.seeders = torrent->GetNumSeeders (trackerID);
				tracker.leechers = torrent->GetNumLeechers (trackerID);
				auto error = torrent->GetTrackerError (trackerID);
				tracker.lastResult = error.empty () ? "Success" : error;
				tracker.lastAnnounce = torrent->GetLastTrackerUpdateTime (trackerID);
				auto nextRequestTime = torrent->GetNextTrackerRequestTime (trackerID);
				tracker.nextAnnounce
					= (nextRequestTime > nowMonotonic ? nowEpoch + (nextRequestTime - nowMonotonic) : nowEpoch) / 1000;
				info.trackers.push_back (std::move (tracker));
			}

			snapshot.session.downloaded += info.downloaded;
			snapshot.session.uploaded += info.uploaded;
			if (torrent->IsStopped ())
				snapshot.session.paused++;
			else
				snapshot.session.active++;

			snapshot.torrents.push_back (std::move (info));
		}
		return snapshot;
	}

	TorrentCommandResult TorrentsTunnel::ExecuteControlCommand (
		const TorrentCommand & command, std::chrono::milliseconds timeout)
	{
		if ((command.kind == TorrentCommandKind::AddMagnet || command.kind == TorrentCommandKind::AddFile)
			&& (command.argument.empty () || command.argument.size () > MAX_CONTROL_ARGUMENT))
			return { false, "invalid command argument", command.torrentId };

		std::shared_ptr<TorrentsTunnel> self;
		try
		{
			self = std::static_pointer_cast<TorrentsTunnel> (shared_from_this ());
		}
		catch (const std::exception &)
		{
			return { false, "torrent tunnel is not available", command.torrentId };
		}

		auto promise = std::make_shared<std::promise<TorrentCommandResult>> ();
		auto future = promise->get_future ();
		try
		{
			boost::asio::post (GetService (),
				[self, promise, command] ()
				{
					try
					{
						promise->set_value (self->ExecuteControlCommandOnService (command));
					}
					catch (...)
					{
						promise->set_exception (std::current_exception ());
					}
				});
		}
		catch (const std::exception &)
		{
			return { false, "can't queue command", command.torrentId };
		}

		if (future.wait_for (timeout) != std::future_status::ready)
			return { false, "command timed out", command.torrentId };
		try
		{
			return future.get ();
		}
		catch (const std::exception &)
		{
			return { false, "command failed", command.torrentId };
		}
	}

	TorrentCommandResult TorrentsTunnel::ExecuteControlCommandOnService (const TorrentCommand & command)
	{
		if (m_Stopping) return { false, "torrent service is stopped", command.torrentId };
		switch (command.kind)
		{
		case TorrentCommandKind::Start:
		case TorrentCommandKind::Resume:
		case TorrentCommandKind::Stop:
		case TorrentCommandKind::Pause:
		{
			auto torrent = FindTorrentByID (command.torrentId);
			if (!torrent) return { false, "torrent not found", command.torrentId };
			if (torrent->IsValid ()
				&& (!m_AdminState.contains (torrent->GetInfoHash ())
					|| !m_AdminState.at (torrent->GetInfoHash ()).loaded))
				return { false, "torrent settings are loading; retry shortly", command.torrentId };
			if (command.kind == TorrentCommandKind::Stop || command.kind == TorrentCommandKind::Pause)
				StopTorrent (torrent);
			else
			{
				torrent->SetStopped (false);
				RequestTorrentTrackers (torrent, eTrackerAnnounceEventStarted);
			}
			SaveTorrentAdminState (torrent);
			return { true, "state updated; persistence queued", command.torrentId };
		}
		case TorrentCommandKind::Reannounce:
			return ReannounceTorrent (command.torrentId)
				? TorrentCommandResult { true, "reannounce queued", command.torrentId }
				: TorrentCommandResult { false, "torrent not found", command.torrentId };
		case TorrentCommandKind::Recheck:
		{
			// Engine-internal re-hash: every piece is treated as unverified
			// until the storage service has read and hashed it again. A
			// mismatch makes the piece downloadable instead of trusting the
			// existing bytes.
			auto torrent = FindTorrentByID (command.torrentId);
			if (!torrent || !torrent->IsValid ())
				return { false, "complete torrent metadata required", command.torrentId };
			if (!torrent->IsStopped ()) return { false, "pause torrent before recheck", command.torrentId };
			torrent->RequestLocalVerification (true, true);
			ScheduleLocalVerification (torrent);
			return { true, "local data recheck queued", command.torrentId };
		}
		case TorrentCommandKind::Remove:
			return RemoveTorrent (command.torrentId, command.deleteFiles)
				? TorrentCommandResult { true, command.deleteFiles ? "remove with files queued" : "remove queued",
					  command.torrentId }
				: TorrentCommandResult { false, "torrent not found", command.torrentId };
		case TorrentCommandKind::RemoveAll:
			RemoveAllTorrents (command.deleteFiles);
			return { true, command.deleteFiles ? "remove all with files queued" : "remove all queued", 0 };
		case TorrentCommandKind::StartAll:
			for (const auto & torrent : GetTorrents ())
			{
				if (torrent->IsValid ()
					&& (!m_AdminState.contains (torrent->GetInfoHash ())
						|| !m_AdminState.at (torrent->GetInfoHash ()).loaded))
					continue;
				torrent->SetStopped (false);
				RequestTorrentTrackers (torrent, eTrackerAnnounceEventStarted);
				SaveTorrentAdminState (torrent);
			}
			return { true, "start all queued", 0 };
		case TorrentCommandKind::StopAll:
			for (const auto & torrent : GetTorrents ())
			{
				if (torrent->IsValid ()
					&& (!m_AdminState.contains (torrent->GetInfoHash ())
						|| !m_AdminState.at (torrent->GetInfoHash ()).loaded))
					continue;
				StopTorrent (torrent);
				SaveTorrentAdminState (torrent);
			}
			return { true, "stop all queued", 0 };
		case TorrentCommandKind::ReannounceAll: ReannounceAllTorrents (); return { true, "reannounce all queued", 0 };
		case TorrentCommandKind::CommentsEnable:
		case TorrentCommandKind::CommentAdd:
		case TorrentCommandKind::CommentHide:
		case TorrentCommandKind::CommentUnhide:
		case TorrentCommandKind::FilePriority:
		{
			auto torrent = FindTorrentByID (command.torrentId);
			if (!torrent || !torrent->IsValid ())
				return { false, "complete torrent metadata required", command.torrentId };
			if (!m_AdminState.contains (torrent->GetInfoHash ()) || !m_AdminState.at (torrent->GetInfoHash ()).loaded)
				return { false, "torrent settings are loading; retry shortly", command.torrentId };
			const auto number = [] (std::string_view input, uint64_t & value)
			{
				auto parsed = std::from_chars (input.data (), input.data () + input.size (), value);
				return !input.empty () && parsed.ec == std::errc { } && parsed.ptr == input.data () + input.size ();
			};
			if (command.kind == TorrentCommandKind::CommentsEnable)
			{
				if (command.argument != "ON" && command.argument != "OFF")
					return { false, "expected ON or OFF", command.torrentId };
				if (torrent->IsPrivate () && command.argument == "ON")
					return { false, "comments disabled for private torrents", command.torrentId };
				torrent->SetCommentsEnabled (command.argument == "ON");
			}
			else if (command.kind == TorrentCommandKind::CommentHide
				|| command.kind == TorrentCommandKind::CommentUnhide)
			{
				uint64_t id = 0;
				const bool ok = number (command.argument, id)
					&& (command.kind == TorrentCommandKind::CommentHide ? torrent->GetCommentStore ().HideId (id)
																		: torrent->GetCommentStore ().UnhideId (id));
				if (!ok)
					return { false,
						command.kind == TorrentCommandKind::CommentUnhide
							? "comment ID not found or visible comment budget full"
							: "comment ID not found",
						command.torrentId };
			}
			else if (command.kind == TorrentCommandKind::FilePriority)
			{
				auto argument = std::string_view (command.argument);
				auto split = argument.find (' ');
				uint64_t index = 0, priority = 0;
				if (split == std::string_view::npos || !number (argument.substr (0, split), index)
					|| !number (argument.substr (split + 1), priority) || priority > 2
					|| index > std::numeric_limits<size_t>::max ()
					|| !torrent->SetFilePriority (static_cast<size_t> (index), static_cast<uint8_t> (priority)))
					return { false, "expected file index and priority 0, 1 or 2", command.torrentId };
			}
			else
			{
				auto argument = std::string_view (command.argument);
				auto split = argument.find (' ');
				uint64_t rating = 0;
				if (split == std::string_view::npos || !number (argument.substr (0, split), rating) || rating > 5)
					return { false, "expected rating and owner|text", command.torrentId };
				argument.remove_prefix (split + 1);
				auto separator = argument.find ('|');
				if (separator == std::string_view::npos) return { false, "expected owner|text", command.torrentId };
				TorrentComment comment;
				comment.rating = static_cast<uint8_t> (rating);
				comment.owner = argument.substr (0, separator);
				comment.text = argument.substr (separator + 1);
				comment.time = i2p::util::GetSecondsSinceEpoch ();
				comment.local = true;
				if ((comment.text.empty () && !rating) || !torrent->GetCommentStore ().Add (std::move (comment)))
					return { false, "comments disabled, duplicate, invalid or full", command.torrentId };
				// Re-announce support and request within the normal bounded
				// throttle; comments are never unsolicited-pushed.
				torrent->PublishComments ();
			}
			SaveTorrentAdminState (torrent);
			return { true, "settings updated; persistence queued", command.torrentId };
		}
		case TorrentCommandKind::SetMetaComment:
		case TorrentCommandKind::SetCreatedBy:
		{
			auto torrent = FindTorrentByID (command.torrentId);
			if (!torrent || !torrent->IsValid ())
				return { false, "complete torrent metadata required", command.torrentId };
			if (!m_AdminState.contains (torrent->GetInfoHash ()) || !m_AdminState.at (torrent->GetInfoHash ()).loaded)
				return { false, "torrent settings are loading; retry shortly", command.torrentId };
			if (!torrent->IsStopped ())
				return { false, "pause the torrent before editing metadata", command.torrentId };
			const bool comment = command.kind == TorrentCommandKind::SetMetaComment;
			const std::string previous
				= comment ? std::string (torrent->GetMetaComment ()) : std::string (torrent->GetCreatedBy ());
			if (!(comment ? torrent->SetMetaComment (command.argument) : torrent->SetCreatedBy (command.argument)))
				return { false, comment ? "invalid or oversized comment" : "invalid or oversized created-by",
					command.torrentId };
			auto content = torrent->CreateTorrentFileContent ();
			if (content.empty ())
			{
				// Do not leave an in-memory edit that was never persisted.
				if (comment)
					(void)torrent->SetMetaComment (previous);
				else
					(void)torrent->SetCreatedBy (previous);
				return { false, "torrent metadata cannot be rewritten safely", command.torrentId };
			}
			boost::asio::post (GetDiskIOService (),
				[this, torrent, content = std::move (content)] { SaveTorrentFile (torrent, content); });
			return { true, "metadata updated; persistence queued", command.torrentId };
		}
		case TorrentCommandKind::AddTracker:
		case TorrentCommandKind::RemoveTracker:
		{
			auto torrent = FindTorrentByID (command.torrentId);
			if (!torrent) return { false, "torrent not found", command.torrentId };
			if (torrent->IsValid ()
				&& (!m_AdminState.contains (torrent->GetInfoHash ())
					|| !m_AdminState.at (torrent->GetInfoHash ()).loaded))
				return { false, "torrent settings are loading; retry shortly", command.torrentId };
			// Avoid live cross-tracker peer provenance changes. Editing resumes
			// through the ordinary start path after persistence has been queued.
			if (!torrent->IsStopped () || !torrent->IsValid ())
				return { false, "pause a torrent with complete metadata before editing trackers", command.torrentId };
			if (torrent->IsPrivate ()) return { false, "private torrent tracker list is fixed", command.torrentId };
			i2p::http::URL url (command.argument);
			if (command.argument.empty () || command.argument.size () > MAX_TRACKER_ANNOUNCE_LENGTH
				|| std::any_of (command.argument.begin (), command.argument.end (),
					[] (unsigned char c) { return c <= 32 || c == 127; })
				|| !IsI2PTrackerURL (url))
				return { false, "invalid I2P tracker URL", command.torrentId };
			auto tiers = torrent->GetTrackerTiers ();
			if (!torrent->HasExplicitTrackers ())
			{
				// Materialize the currently authorized common/default endpoints
				// before editing, so removing a common tracker is effective and
				// the other current endpoints retain their membership.
				for (size_t i = 0; i < m_Trackers.size (); ++i)
					if (IsTorrentTracker (i, *torrent)) tiers.push_back ({ GetTrackerAnnounce (i) });
			}
			bool found = false;
			for (auto & tier : tiers)
			{
				for (auto it = tier.begin (); it != tier.end ();)
				{
					if (i2p::http::URL (*it).to_string () == url.to_string ())
					{
						found = true;
						if (command.kind == TorrentCommandKind::RemoveTracker)
						{
							it = tier.erase (it);
							continue;
						}
					}
					++it;
				}
			}
			std::erase_if (tiers, [] (const auto & tier) { return tier.empty (); });
			const bool adding = command.kind == TorrentCommandKind::AddTracker;
			if (adding)
			{
				if (found) return { false, "tracker already present", command.torrentId };
				// Reserve registry capacity before the tier list is rewritten: an
				// already registered URL reuses its slot, otherwise a reusable slot
				// or a free index below MAX_NUM_TRACKERS is required.
				if (!ResolveTrackerSlot (command.argument, false, false) && !FindReusableTrackerSlot (m_TrackerSlots)
					&& m_Trackers.size () >= MAX_NUM_TRACKERS)
					return { false, "tracker limit reached", command.torrentId };
				tiers.push_back ({ command.argument });
			}
			else if (!found)
				return { false, "tracker not found", command.torrentId };
			auto previousTiers = torrent->GetTrackerTiers ();
			auto previousAnnounce = std::string (torrent->GetAnnounce ());
			const bool previousExplicit = torrent->HasExplicitTrackers ();
			auto restore = [&] ()
			{
				torrent->SetTrackerTiers (previousTiers);
				torrent->SetHasExplicitTrackers (previousExplicit);
				torrent->SetAnnounce (previousAnnounce);
			};
			if (!torrent->SetTrackerTiers (tiers)) return { false, "tracker limit reached", command.torrentId };
			torrent->SetAnnounce (tiers.empty () ? std::string_view { } : std::string_view (tiers.front ().front ()));
			auto content = torrent->CreateTorrentFileContent ();
			if (content.empty ())
			{
				restore ();
				return { false, "torrent metadata cannot be rewritten safely", command.torrentId };
			}
			// Only rewrite registry membership after the complete policy and
			// bytes are known to be valid, so a rejected edit cannot leak
			// capacity. The removed URL is released exactly once and retained
			// slots keep their index, so live tracker statistics stay bound to
			// their announce URL.
			RegisterTorrentTrackers (*torrent);
			boost::asio::post (GetDiskIOService (),
				[this, torrent, content = std::move (content)] { SaveTorrentFile (torrent, content); });
			return { true, "trackers updated; persistence queued", command.torrentId };
		}
		case TorrentCommandKind::Create: return QueueTorrentCreation (command);
		case TorrentCommandKind::CancelCreate:
			if (!m_Creation.pending || !m_CreationProgress) return { false, "no creation job pending", 0 };
			m_CreationProgress->cancelled = true;
			return { true, "creation cancellation requested", 0 };
		case TorrentCommandKind::AddMagnet:
		{
			auto [torrent, id] = AddMagnet (command.argument);
			if (!torrent || !id) return { false, "invalid or duplicate magnet", 0 };
			return { true, "torrent added", id };
		}
		case TorrentCommandKind::AddFile:
		{
			std::vector<uint8_t> content;
			std::string error;
			if (!ReadTorrentFileForControl (command.argument, content, error)) return { false, error, 0 };
			auto [torrent, id]
				= AddTorrent (std::string_view (reinterpret_cast<const char *> (content.data ()), content.size ()));
			if (!torrent || !id) return { false, "invalid torrent file", 0 };
			return { true, "torrent added", id };
		}
		default: return { false, "unsupported command", command.torrentId };
		}
	}

	TorrentCommandResult TorrentsTunnel::QueueTorrentCreation (const TorrentCommand & command)
	{
		const auto source = std::string_view (command.argument);
		if (m_Stopping || m_Creation.pending) return { false, "creation unavailable or already pending", 0 };
		std::filesystem::path relative { std::string (source) };
		if (source.empty () || source == ".openi2pd-state" || source.size () > MAX_CONTROL_ARGUMENT
			|| !IsSafeRelativePath (relative) || !relative.parent_path ().empty ())
			return { false, "source must be one file or directory directly in the torrent root", 0 };
		if (m_Creation.job == std::numeric_limits<uint64_t>::max ()) return { false, "creation job IDs exhausted", 0 };
		for (const auto & existing : GetTorrents ())
			if (existing->GetName () == source) return { false, "source already belongs to a torrent session", 0 };
		if (command.createPieceLength
			&& (command.createPieceLength < MIN_PIECE_LENGTH || command.createPieceLength > MAX_PIECE_LENGTH
				|| (command.createPieceLength & (command.createPieceLength - 1))))
			return { false, "piece length must be zero or a supported power of two", 0 };
		size_t trackers = 0;
		if (command.createTrackers.size () > MAX_NUM_TRACKERS) return { false, "too many tracker tiers", 0 };
		for (const auto & tier : command.createTrackers)
			for (const auto & tracker : tier)
				if (++trackers > MAX_NUM_TRACKERS || tracker.empty () || tracker.size () > MAX_TRACKER_ANNOUNCE_LENGTH
					|| std::any_of (
						tracker.begin (), tracker.end (), [] (unsigned char c) { return c <= 32 || c == 127; })
					|| !IsI2PTrackerURL (i2p::http::URL (tracker)))
					return { false, "invalid I2P creation tracker", 0 };
		if (command.createPrivate && !trackers)
			return { false, "private torrents require at least one I2P tracker", 0 };
		auto self = std::static_pointer_cast<TorrentsTunnel> (shared_from_this ());
		m_Creation = { m_Creation.job + 1, true, false, 0, "hashing source files" };
		const auto job = m_Creation.job;
		TorrentCreateOptions options;
		options.pieceLength = command.createPieceLength;
		options.privateTorrent = command.createPrivate;
		options.trackers = command.createTrackers;
		auto progress = std::make_shared<CreationProgress> ();
		m_CreationProgress = progress;
		options.cancelled = [self, progress] { return self->m_Stopping.load () || progress->cancelled.load (); };
		options.progress = [progress] (uint64_t done, uint64_t total, size_t filesDone, size_t filesTotal)
		{
			progress->totalBytes = total;
			progress->totalFiles = filesTotal;
			progress->bytesDone = done;
			progress->filesDone = filesDone;
		};
		try
		{
			boost::asio::post (GetDiskIOService (),
				[self, relative, options = std::move (options), job] ()
				{
					std::string error, content;
					try
					{
						auto torrent = CreateTorrentFromPath (self->m_TorrentsDir, relative, options, error);
						if (torrent) content = torrent->CreateTorrentFileContent ();
					}
					catch (...)
					{
						error = "torrent creation failed";
					}
					boost::asio::post (self->GetService (),
						[self, content = std::move (content), error = std::move (error), job] ()
						{
							if (self->m_Creation.job != job || !self->m_Creation.pending) return;
							self->m_Creation.pending = false;
							if (self->m_Stopping || content.empty ()
								|| (self->m_CreationProgress && self->m_CreationProgress->cancelled))
							{
								self->m_Creation.message = error.empty () ? "creation cancelled" : error;
								return;
							}
							try
							{
								auto [torrent, id] = self->AddTorrent (content);
								self->m_Creation.success = torrent && id;
								self->m_Creation.torrentId = id;
								self->m_Creation.message = torrent && id
									? "torrent created; metainfo persistence queued"
									: "unable to add created torrent";
							}
							catch (...)
							{
								self->m_Creation.message = "unable to add created torrent";
							}
						});
				});
		}
		catch (...)
		{
			m_Creation.pending = false;
			m_Creation.message = "unable to queue creation";
			return { false, m_Creation.message, 0 };
		}
		return { true, "creation queued; inspect session for completion", 0 };
	}

	template <typename F>
	static std::pair<std::shared_ptr<Torrent>, int> RunAddOnService (
		const std::shared_ptr<TorrentsTunnel> & self, F && operation)
	{
		auto promise = std::make_shared<std::promise<std::pair<std::shared_ptr<Torrent>, int>>> ();
		auto future = promise->get_future ();
		try
		{
			boost::asio::post (self->GetService (),
				[self, promise, operation = std::forward<F> (operation)] () mutable
				{
					try
					{
						promise->set_value (operation ());
					}
					catch (...)
					{
						promise->set_exception (std::current_exception ());
					}
				});
		}
		catch (const std::exception &)
		{
			return { nullptr, 0 };
		}
		if (future.wait_for (CONTROL_DEFAULT_TIMEOUT) != std::future_status::ready) return { nullptr, 0 };
		try
		{
			return future.get ();
		}
		catch (const std::exception &)
		{
			return { nullptr, 0 };
		}
	}

	std::pair<std::shared_ptr<Torrent>, int> TorrentsTunnel::AddTorrentFromRpc (std::string_view torrentFileContent)
	{
		auto self = std::static_pointer_cast<TorrentsTunnel> (shared_from_this ());
		std::string content (torrentFileContent);
		return RunAddOnService (self, [self, content = std::move (content)] () { return self->AddTorrent (content); });
	}

	std::pair<std::shared_ptr<Torrent>, int> TorrentsTunnel::AddMagnetFromRpc (std::string_view magnet)
	{
		auto self = std::static_pointer_cast<TorrentsTunnel> (shared_from_this ());
		std::string value (magnet);
		return RunAddOnService (self, [self, value = std::move (value)] () { return self->AddMagnet (value); });
	}

	std::pair<std::shared_ptr<Torrent>, int> TorrentsTunnel::AddTorrent (std::string_view torrentFileContent)
	{
		if (m_Stopping) return { nullptr, 0 };
		auto torrent = std::make_shared<Torrent> (torrentFileContent);
		// An unparseable or incomplete metainfo must not enter the session:
		// it would otherwise be reported as added and persisted with no
		// usable files. Startup loading already rejects these with IsValid
		// ().
		if (!torrent || torrent->GetError () != eTorrentErrorNoError || !torrent->IsValid ())
		{
			LogPrint (eLogWarning, "TorrentsTunnel: Rejecting invalid torrent file");
			return { nullptr, 0 };
		}
		// Only retain validated I2P tracker hints in persisted metainfo. The
		// info dictionary is unchanged, so sanitizing tiers preserves
		// infohash.
		const auto validTracker = [] (std::string_view text)
		{
			i2p::http::URL url (text);
			return !std::any_of (text.begin (), text.end (), [] (unsigned char c) { return c <= 32 || c == 127; })
				&& IsI2PTrackerURL (url);
		};
		if (!torrent->GetAnnounce ().empty () && !validTracker (torrent->GetAnnounce ())) torrent->SetAnnounce ({ });
		Torrent::TrackerTiers tiers;
		for (const auto & tier : torrent->GetTrackerTiers ())
		{
			std::vector<std::string> filtered;
			for (const auto & tracker : tier)
				if (validTracker (tracker)) filtered.push_back (tracker);
			if (!filtered.empty ()) tiers.push_back (std::move (filtered));
		}
		if (torrent->HasExplicitTrackers ())
		{
			// Some clients keep a primary announce outside a non-empty
			// announce-list. It is still an authorized I2P endpoint and must
			// remain in the tier policy; otherwise a swarm reachable only via
			// that primary becomes unreachable and cannot be edited.
			const auto normalized = [] (std::string_view text) { return i2p::http::URL (text).to_string (); };
			bool announcePresent = torrent->GetAnnounce ().empty ();
			if (!announcePresent)
				for (const auto & tier : tiers)
					for (const auto & tracker : tier)
						if (normalized (tracker) == normalized (torrent->GetAnnounce ())) announcePresent = true;
			if (!announcePresent)
			{
				if (tiers.empty ())
					tiers.push_back ({ std::string (torrent->GetAnnounce ()) });
				else
					tiers.front ().insert (tiers.front ().begin (), std::string (torrent->GetAnnounce ()));
			}
		}
		if ((torrent->HasExplicitTrackers () || !tiers.empty ()) && !torrent->SetTrackerTiers (tiers))
			return { nullptr, 0 };
		if (torrent->IsPrivate () && torrent->GetAnnounce ().empty () && tiers.empty ())
		{
			LogPrint (eLogWarning, "TorrentsTunnel: Rejecting private torrent without an I2P tracker");
			return { nullptr, 0 };
		}
		auto existing = FindTorrent (torrent->GetInfoHash ());
		if (!existing)
		{
			if (!SetTorrentPathWithinRoot (torrent)) return { nullptr, 0 };
			auto [id, inserted] = InsertTorrent (torrent);
			if (inserted)
			{
				auto & initial = m_AdminState[torrent->GetInfoHash ()];
				initial.initialDiscoveryPending = true;
				initial.initialDiscoveryOwner = torrent;
				// The disk job and its service-thread continuation outlive the
				// call, so they carry an owner reference instead of raw `this`.
				auto self = std::static_pointer_cast<TorrentsTunnel> (weak_from_this ().lock ());
				if (self)
					boost::asio::post (GetDiskIOService (),
						[self, torrent, content = torrent->CreateTorrentFileContent ()] ()
						{
							if (self->m_Stopping || torrent->IsRemoved ()
								|| self->FindTorrent (torrent->GetInfoHash ()) != torrent)
								return;
							const bool persisted = self->SaveTorrentFile (torrent, content);
							self->InitTorrentFiles (torrent);
							// Announce only after file preparation: a torrent
							// whose part file or directories could not be
							// created must not appear to trackers as a live
							// peer. The disk worker above published the error,
							// so this read is ordered.
							if (torrent->GetError () != eTorrentErrorNoError) return;
							boost::asio::post (self->GetService (),
								[self, torrent, persisted]
								{
									if (self->m_Stopping || torrent->IsRemoved ()
										|| self->FindTorrent (torrent->GetInfoHash ()) != torrent)
										return;
									auto & state = self->m_AdminState[torrent->GetInfoHash ()];
									state.initialDiscoveryReady = persisted;
									if (!persisted) state.error = "cannot persist torrent metainfo";
									self->DiscoverAddedTorrent (torrent);
								});
						});
				return { torrent, id };
			}
			// Lost an insertion race with a removal or another add.
			existing = FindTorrentByID (id);
			if (existing) return { existing, id };
			return { nullptr, 0 };
		}
		return { existing, FindTorrentID (existing) };
	}

	bool TorrentsTunnel::CanDiscoverTorrent (const std::shared_ptr<Torrent> & torrent) const
	{
		if (m_Stopping || m_StartupRecoveryPending || !torrent || !torrent->HasPeerID () || torrent->IsStopped ()
			|| torrent->IsRemoved () || torrent->GetError () != eTorrentErrorNoError
			|| FindTorrent (torrent->GetInfoHash ()) != torrent)
			return false;
		auto state = m_AdminState.find (torrent->GetInfoHash ());
		return state == m_AdminState.end () || !state->second.initialDiscoveryPending
			|| (state->second.initialDiscoveryOwner.lock () == torrent && state->second.loaded
				&& state->second.initialDiscoveryReady && !state->second.pending && !state->second.dirty
				&& state->second.error.empty () && !torrent->IsLocalVerificationRequested ());
	}

	bool TorrentsTunnel::DispatchInitialDiscovery (const std::shared_ptr<Torrent> & torrent,
		const std::function<void ()> & trackers, const std::function<void ()> & dht)
	{
		if (!CanDiscoverTorrent (torrent)) return false;
		auto state = m_AdminState.find (torrent->GetInfoHash ());
		if (state == m_AdminState.end () || !state->second.initialDiscoveryPending) return false;
		// Consume before invoking either backend so reentrant completion
		// cannot announce this insertion twice. Private swarms use trackers.
		state->second.initialDiscoveryPending = false;
		if (trackers) trackers ();
		if (CanDiscoverTorrent (torrent) && !torrent->IsPrivate () && dht) dht ();
		return true;
	}

	void TorrentsTunnel::DiscoverAddedTorrent (const std::shared_ptr<Torrent> & torrent)
	{
		DispatchInitialDiscovery (
			torrent, [this, &torrent] { RequestTorrentTrackers (torrent, eTrackerAnnounceEventNone); },
			[this, &torrent]
			{
				if (m_DHT) m_DHT->GetPeersAndAnnounce (torrent);
			});
	}

	std::pair<std::shared_ptr<Torrent>, int> TorrentsTunnel::AddMagnet (std::string_view magnet)
	{
		if (m_Stopping) return { nullptr, 0 };
		auto parsed = ParseTorrentMagnet (magnet);
		if (!parsed) return { nullptr, 0 };
		return AddMagnetState (
			{ parsed->infoHash, parsed->name.empty () ? ToHexString (parsed->infoHash) : parsed->name, parsed->trackers,
				false },
			false);
	}

	std::pair<std::shared_ptr<Torrent>, int> TorrentsTunnel::AddMagnetState (
		const TorrentMagnetState & state, bool restoring)
	{
		if (m_Stopping) return { nullptr, 0 };
		std::string error;
		if (EncodeTorrentMagnetState (state, error).empty ()) return { nullptr, 0 };
		auto existing = FindTorrent (state.infoHash);
		if (existing && existing->IsValid ()) return { existing, FindTorrentID (existing) };
		TorrentMagnetState merged = state;
		if (existing)
		{
			merged.name = std::string (existing->GetName ());
			if (merged.name == ToHexString (state.infoHash)) merged.name = state.name;
			merged.stopped = existing->IsStopped (); // duplicate hints never resume an owner-paused magnet
			merged.trackers.clear ();
			for (const auto & tier : existing->GetTrackerTiers ())
				for (const auto & tracker : tier) merged.trackers.push_back (tracker);
			for (const auto & tracker : state.trackers)
				if (std::none_of (merged.trackers.begin (), merged.trackers.end (), [&] (const auto & current)
						{ return i2p::http::URL (current).to_string () == i2p::http::URL (tracker).to_string (); }))
					merged.trackers.push_back (tracker);
			if (EncodeTorrentMagnetState (merged, error).empty ()) return { nullptr, 0 };
		}
		auto torrent = existing ? existing : std::make_shared<Torrent> (state.infoHash);
		if (!torrent->HasPeerID ()) return { nullptr, 0 };
		Torrent::TrackerTiers tiers;
		for (const auto & tracker : merged.trackers) tiers.push_back ({ tracker });
		if (!tiers.empty () && !torrent->SetTrackerTiers (tiers)) return { nullptr, 0 };
		if (!merged.trackers.empty ()) torrent->SetAnnounce (merged.trackers.front ());
		torrent->SetName (merged.name);
		if (torrent->GetName ().empty ()) torrent->SetName (ToHexString (state.infoHash));
		torrent->SetStopped (merged.stopped);
		if (existing)
		{
			RegisterTorrentTrackers (*torrent);
			SaveTorrentAdminState (torrent);
			return { torrent, FindTorrentID (torrent) };
		}
		auto [id, inserted] = InsertTorrent (torrent);
		if (inserted)
		{
			auto & status = m_AdminState[state.infoHash];
			status.loaded = true;
			status.initialDiscoveryPending = true;
			status.initialDiscoveryReady = restoring;
			status.initialDiscoveryOwner = torrent;
			if (!restoring) SaveTorrentAdminState (torrent);
			auto self = std::static_pointer_cast<TorrentsTunnel> (weak_from_this ().lock ());
			if (self && !torrent->IsStopped ())
				boost::asio::post (GetService (), [self, torrent] { self->DiscoverAddedTorrent (torrent); });
			return { torrent, id };
		}
		return { FindTorrentByID (id), id };
	}

	std::pair<int, bool> TorrentsTunnel::InsertTorrent (std::shared_ptr<Torrent> torrent)
	{
		if (!torrent || torrent->IsRemoved ()) return { 0, false };
		std::lock_guard<std::mutex> l (m_TorrentsMutex);
		if (m_RemovingTorrents.contains (torrent->GetInfoHash ())) return { 0, false };
		if (m_Torrents.emplace (torrent->GetInfoHash (), torrent).second)
		{
			// Reuse freed ids instead of overflowing int after INT_MAX
			// additions. The map is ordered, so a hole scan is bounded by the
			// number of live torrents and only happens at the wrap point.
			int id = 1;
			if (!m_TorrentsByID.empty ())
			{
				int last = m_TorrentsByID.rbegin ()->first;
				if (last < std::numeric_limits<int>::max ())
					id = last + 1;
				else
				{
					id = 0;
					for (int candidate = 1; candidate < std::numeric_limits<int>::max (); candidate++)
					{
						if (!m_TorrentsByID.contains (candidate))
						{
							id = candidate;
							break;
						}
					}
					if (!id)
					{
						LogPrint (eLogError, "TorrentsTunnel: No free torrent id");
						m_Torrents.erase (torrent->GetInfoHash ());
						return { 0, false };
					}
				}
			}
			m_TorrentsByID.emplace (id, torrent);
			if (torrent->IsValid ()) LoadTorrentAdminState (torrent);
			// Reference the announce URL and every tier once, keyed by this
			// torrent's membership so removal releases each slot exactly once.
			RegisterTorrentTrackers (*torrent);
			if (!torrent->GetAnnounce ().empty ())
			{
				const auto announceSlot = ResolveTrackerSlot (torrent->GetAnnounce (), false, false);
				torrent->SetAnnounceTrackerID (announceSlot ? static_cast<int> (*announceSlot) : -1);
			}
			// A torrent whose metadata is already private never uses the public
			// DHT, and any state collected before insertion is purged.
			if (m_DHT && torrent->IsPrivate ()) m_DHT->MarkPrivate (torrent->GetInfoHash ());
			return { id, true };
		}
		else // already exists, find id without re-locking m_TorrentsMutex
		{
			for (const auto & [id, torrentByID] : m_TorrentsByID)
				if (torrentByID.lock () == torrent) return { id, false };
			return { 0, false };
		}
	}

	bool TorrentsTunnel::RemoveTorrent (int id, bool deleteFiles)
	{
		std::shared_ptr<Torrent> torrent;
		{
			std::lock_guard<std::mutex> l (m_TorrentsMutex);
			auto it = m_TorrentsByID.find (id);
			if (it == m_TorrentsByID.end ()) return false;
			torrent = it->second.lock ();
			m_TorrentsByID.erase (it);
			if (!torrent) return false;
			// No IO lock or wait here: revoke new jobs immediately. Any IO
			// already running finishes before the serialized disk deletion.
			torrent->MarkRemoved ();
			m_RemovingTorrents.insert (torrent->GetInfoHash ());
			m_Torrents.erase (torrent->GetInfoHash ());
		}
		auto self = std::static_pointer_cast<TorrentsTunnel> (shared_from_this ());
		boost::asio::post (
			GetService (), [self, torrent, deleteFiles] () { self->RemoveTorrent (torrent, deleteFiles); });
		return true;
	}

	void TorrentsTunnel::RemoveAllTorrents (bool deleteFiles)
	{
		std::list<std::shared_ptr<Torrent>> torrentsToRemove;
		{
			std::lock_guard<std::mutex> l (m_TorrentsMutex);
			for (const auto & it : m_Torrents)
			{
				it.second->MarkRemoved ();
				m_RemovingTorrents.insert (it.first);
				torrentsToRemove.push_back (it.second);
			}
			m_Torrents.clear ();
			m_TorrentsByID.clear ();
		}
		auto self = std::static_pointer_cast<TorrentsTunnel> (shared_from_this ());
		boost::asio::post (GetService (),
			[self, torrentsToRemove = std::move (torrentsToRemove), deleteFiles] ()
			{
				for (auto it : torrentsToRemove) self->RemoveTorrent (it, deleteFiles);
			});
	}

	void TorrentsTunnel::RemoveTorrent (std::shared_ptr<Torrent> torrent, bool deleteFiles)
	{
		if (!torrent) return;
		const auto infoHash = torrent->GetInfoHash ();
		m_AdminState.erase (infoHash); // service-thread state only
		// Retire announces that are still airborne for this torrent before
		// the stop announce below creates its own, bounded requests.
		CancelTrackerRequests (infoHash);
		StopTorrent (torrent);
		// Release registry membership only after the event=stopped announces
		// were created with the original URLs, and exactly once per
		// membership even when the payload cleanup below fails.
		ReleaseTorrentTrackers (infoHash);
		// A removed hash must not keep a served peer set, our rendezvous
		// identity or any token we issued for it.
		if (m_DHT) m_DHT->Forget (infoHash);
		const bool hasMetadata = torrent->IsValid ();
		auto self = std::static_pointer_cast<TorrentsTunnel> (shared_from_this ());
		boost::asio::post (GetDiskIOService (),
			[this, self, torrent, deleteFiles, hasMetadata] ()
			{
				bool cleanupSucceeded = false;
				bool cleanupFailed = false;
				// Release on the service domain only after all disk mutations.
				// Failed cleanup keeps the hash reserved until restart, so a
				// replacement cannot inherit partially deleted old state.
				auto finish = [self, &cleanupSucceeded] (Torrent * removed)
				{
					if (!cleanupSucceeded)
					{
						LogPrint (eLogError, "TorrentsTunnel: Removal incomplete; re-add blocked until restart");
						return;
					}
					boost::asio::post (self->GetService (),
						[self, hash = removed->GetInfoHash ()]
						{
							std::lock_guard lock (self->m_TorrentsMutex);
							self->m_RemovingTorrents.erase (hash);
						});
				};
				std::unique_ptr<Torrent, decltype (finish)> completion (torrent.get (), finish);
				self->m_ResolvedPersistence.erase (torrent->GetInfoHash ());
				for (const auto suffix : { ".state", ".magnet" })
				{
					const auto relative
						= std::filesystem::path (".openi2pd-state") / (ToHexString (torrent->GetInfoHash ()) + suffix);
					std::string error;
					const bool removed = m_StorageWorker ? m_StorageWorker->RemoveMetadata (relative, error)
														 : RemoveFileNoFollow (m_TorrentsDir, relative, error);
					if (!removed) cleanupFailed = true;
				}
				// An unresolved magnet owns only hash-bound recovery files. Its
				// display name never becomes a payload or deletion authority.
				if (!hasMetadata)
				{
					cleanupSucceeded = !cleanupFailed;
					return;
				}
				auto fullPath = torrent->GetFullPath ();
				if (!IsContainedPath (fullPath, true))
				{
					LogPrint (eLogError, "TorrentsTunnel: Refusing to remove path outside ", m_TorrentsDir);
					cleanupFailed = true;
					return;
				}
				if (m_StorageWorker)
				{
					std::string error;
					auto relative = fullPath.lexically_relative (m_TorrentsDir);
					for (const auto & suffix : { ".torrent", ".resume" })
					{
						auto metadata = relative;
						metadata += suffix;
						if (!m_StorageWorker->RemoveMetadata (metadata, error))
						{
							cleanupFailed = true;
							LogPrint (eLogError, "TorrentsTunnel: Isolated metadata removal failed: ", error);
						}
					}
					if (deleteFiles)
					{
						std::vector<std::filesystem::path> directories;
						for (const auto & file : torrent->GetFiles ())
						{
							if (file->HasFinalNameStorage ())
							{
								if (!file->RemovePayload (error))
								{
									cleanupFailed = true;
									LogPrint (eLogError, "TorrentsTunnel: SAF deletion failed: ", error);
								}
								continue;
							}
							file->Close ();
							auto name = file->GetFullFilePath ().lexically_relative (m_TorrentsDir);
							for (const auto & suffix : { "", ".part" })
							{
								auto payload = name;
								payload += suffix;
								if (!m_StorageWorker->Remove (payload, file->GetFileLength (), error))
								{
									cleanupFailed = true;
									LogPrint (eLogError, "TorrentsTunnel: Isolated payload removal failed: ", error);
								}
							}
							for (auto parent = name.parent_path (); !parent.empty (); parent = parent.parent_path ())
								directories.push_back (parent);
						}
						std::sort (directories.begin (), directories.end (),
							[] (const auto & a, const auto & b) { return a.native ().size () > b.native ().size (); });
						directories.erase (std::unique (directories.begin (), directories.end ()), directories.end ());
						for (const auto & directory : directories)
							(void)m_StorageWorker->RemoveEmptyDirectory (directory, error); // Only empty directories.
					}
					cleanupSucceeded = !cleanupFailed;
					return; // No direct filesystem fallback after any worker error.
				}
				// Settings belong to the torrent registration, not its payload:
				// remove them even when deleteFiles is false, in both backends.
				std::string metadataError;
				auto relative = fullPath.lexically_relative (m_TorrentsDir);
				for (const auto & suffix : { ".torrent", ".resume" })
				{
					auto metadata = relative;
					metadata += suffix;
					if (!RemoveFileNoFollow (m_TorrentsDir, metadata, metadataError))
					{
						cleanupFailed = true;
						LogPrint (eLogError, "TorrentsTunnel: Metadata removal failed: ", metadataError);
					}
				}
				std::error_code ec;
				const auto files = torrent->GetFiles ();
				const bool documents = std::any_of (
					files.begin (), files.end (), [] (const auto & file) { return file->HasFinalNameStorage (); });
				if (deleteFiles && documents)
				{
					std::string error;
					for (const auto & file : files)
						if (!file->RemovePayload (error))
						{
							cleanupFailed = true;
							LogPrint (eLogError, "TorrentsTunnel: SAF deletion failed: ", error);
						}
				}
				if (deleteFiles && !documents)
				{
					// remove_all removes a symlink entry itself and never
					// descends through links, so a directory swapped in after
					// the containment check cannot redirect the deletion.
					if (!IsContainedPath (fullPath, true))
					{
						cleanupFailed = true;
						LogPrint (eLogError, "TorrentsTunnel: Refusing changed payload path ", fullPath);
					}
					else if (std::filesystem::exists (fullPath))
					{
						std::filesystem::remove_all (fullPath, ec);
						if (ec)
						{
							cleanupFailed = true;
							LogPrint (eLogError, "TorrentsTunnel: Can't delete ", fullPath);
						}
					}
					auto partFilePath = fullPath;
					partFilePath += ".part";
					if (IsContainedPath (partFilePath, false) && std::filesystem::exists (partFilePath))
					{
						std::filesystem::remove (partFilePath, ec);
						if (ec)
						{
							cleanupFailed = true;
							LogPrint (eLogError, "TorrentsTunnel: Can't delete ", partFilePath);
						}
					}
					else if (!IsContainedPath (partFilePath, false))
					{
						cleanupFailed = true;
						LogPrint (eLogError, "TorrentsTunnel: Refusing to delete ", partFilePath);
					}
				}
				cleanupSucceeded = !cleanupFailed;
			});
	}

	int TorrentsTunnel::FindTorrentID (std::shared_ptr<Torrent> torrent) const
	{
		if (torrent)
		{
			std::lock_guard<std::mutex> l (m_TorrentsMutex);
			auto it = std::find_if (m_TorrentsByID.begin (), m_TorrentsByID.end (),
				[torrent] (const auto & torrentByID) { return torrentByID.second.lock () == torrent; });
			if (it != m_TorrentsByID.end ()) return it->first;
		}
		return 0;
	}

	bool TorrentsTunnel::StopTorrent (int id)
	{
		std::shared_ptr<Torrent> torrent;
		{
			std::lock_guard<std::mutex> l (m_TorrentsMutex);
			auto it = m_TorrentsByID.find (id);
			if (it == m_TorrentsByID.end ()) return false;
			torrent = it->second.lock ();
			if (!torrent) return false;
		}
		auto self = std::static_pointer_cast<TorrentsTunnel> (weak_from_this ().lock ());
		if (!self) return false;
		boost::asio::post (GetService (),
			[self, torrent] ()
			{
				self->StopTorrent (torrent);
				self->SaveTorrentAdminState (torrent);
			});
		return true;
	}

	void TorrentsTunnel::StopTorrent (std::shared_ptr<Torrent> torrent)
	{
		if (!torrent) return;
		const auto infoHash = torrent->GetInfoHash ();
		torrent->SetStopped (true);
		// A paused torrent keeps no announce in flight; the stopped announce
		// below is admitted as a fresh, bounded request.
		CancelTrackerRequests (infoHash);
		if (m_DHT)
		{
			// A private swarm was never announced: drop every trace instead of
			// performing a public self-unannounce for a hash we never published.
			if (torrent->IsPrivate ())
				m_DHT->Forget (infoHash);
			else
				m_DHT->Unannounce (infoHash);
		}
		torrent->UpdateStatus (i2p::util::GetMonotonicSeconds ());
		// Persist the current verified/complete set while the session still
		// owns its payload and before shutdown tears the disk queue down.
		if (torrent->IsValid ())
			boost::asio::post (GetDiskIOService (),
				[torrent]
				{
					// Release payload/part/SAF handles before the resume write;
					// paused torrents must not keep files open until the
					// inactivity sweep (or forever on Windows).
					for (const auto & file : torrent->GetFiles ()) file->Close ();
					torrent->SaveTorrentResumeFile ();
				});
		// inform trackers that we stopped
		RequestTorrentTrackers (torrent, eTrackerAnnounceEventStopped);
		// close connections
		auto connections = torrent->GetConnections ();
		for (auto it : connections) it->Close ();
	}

	void TorrentsTunnel::StopAllTorrents ()
	{
		std::list<std::shared_ptr<Torrent>> torrentsToStop;
		{
			std::lock_guard<std::mutex> l (m_TorrentsMutex);
			for (const auto & it : m_Torrents) torrentsToStop.push_back (it.second);
		}
		auto self = std::static_pointer_cast<TorrentsTunnel> (weak_from_this ().lock ());
		if (!self) return;
		boost::asio::post (GetService (),
			[self, torrentsToStop = std::move (torrentsToStop)] ()
			{
				for (auto it : torrentsToStop) // TODO: announce with random intervals
				{
					self->StopTorrent (it);
					self->SaveTorrentAdminState (it);
				}
			});
	}

	bool TorrentsTunnel::StartTorrent (int id)
	{
		std::shared_ptr<Torrent> torrent;
		{
			std::lock_guard<std::mutex> l (m_TorrentsMutex);
			auto it = m_TorrentsByID.find (id);
			if (it == m_TorrentsByID.end ()) return false;
			torrent = it->second.lock ();
			if (!torrent) return false;
		}
		auto self = std::static_pointer_cast<TorrentsTunnel> (weak_from_this ().lock ());
		if (!self) return false;
		boost::asio::post (GetService (),
			[self, torrent] ()
			{
				torrent->SetStopped (false);
				// inform trackers that we started, unless storage/metadata is
				// known to be unusable
				if (torrent->GetError () == eTorrentErrorNoError)
					self->RequestTorrentTrackers (torrent, eTrackerAnnounceEventStarted);
				self->SaveTorrentAdminState (torrent);
			});
		return true;
	}

	void TorrentsTunnel::StartAllTorrents ()
	{
		std::list<std::shared_ptr<Torrent>> torrentsToStart;
		{
			std::lock_guard<std::mutex> l (m_TorrentsMutex);
			for (const auto & it : m_Torrents) torrentsToStart.push_back (it.second);
		}
		auto self = std::static_pointer_cast<TorrentsTunnel> (weak_from_this ().lock ());
		if (!self) return;
		boost::asio::post (GetService (),
			[self, torrentsToStart = std::move (torrentsToStart)] ()
			{
				for (auto it : torrentsToStart) // TODO: announce with random intervals
				{
					it->SetStopped (false);
					if (it->GetError () == eTorrentErrorNoError)
						self->RequestTorrentTrackers (it, eTrackerAnnounceEventStarted);
					self->SaveTorrentAdminState (it);
				}
			});
	}

	bool TorrentsTunnel::ReannounceTorrent (int id)
	{
		std::shared_ptr<Torrent> torrent;
		{
			std::lock_guard<std::mutex> l (m_TorrentsMutex);
			auto it = m_TorrentsByID.find (id);
			if (it == m_TorrentsByID.end ()) return false;
			torrent = it->second.lock ();
		}
		if (!torrent) return false;
		auto self = std::static_pointer_cast<TorrentsTunnel> (weak_from_this ().lock ());
		if (!self) return false;
		boost::asio::post (GetService (),
			[self, torrent] ()
			{
				if (!self->CanDiscoverTorrent (torrent)) return;
				// An explicit re-announce supersedes an announce that is still
				// airborne for this torrent, so retire it (closing its stream)
				// before admitting the fresh requests.
				self->CancelTrackerRequests (torrent->GetInfoHash ());
				self->RequestTorrentTrackers (torrent, eTrackerAnnounceEventNone);
				if (self->m_DHT && !torrent->IsPrivate ()) self->m_DHT->GetPeersAndAnnounce (torrent);
			});
		return true;
	}

	void TorrentsTunnel::ReannounceAllTorrents ()
	{
		std::list<std::shared_ptr<Torrent>> torrentsToReannounce;
		{
			std::lock_guard<std::mutex> l (m_TorrentsMutex);
			for (const auto & it : m_Torrents) torrentsToReannounce.push_back (it.second);
		}
		auto self = std::static_pointer_cast<TorrentsTunnel> (weak_from_this ().lock ());
		if (!self) return;
		boost::asio::post (GetService (),
			[self, torrentsToReannounce = std::move (torrentsToReannounce)] ()
			{
				for (auto it : torrentsToReannounce) // TODO: announce with random intervals
				{
					if (!self->CanDiscoverTorrent (it)) continue;
					self->CancelTrackerRequests (it->GetInfoHash ());
					self->RequestTorrentTrackers (it, eTrackerAnnounceEventNone);
					if (self->m_DHT && !it->IsPrivate ()) self->m_DHT->GetPeersAndAnnounce (it);
				}
			});
	}

	void TorrentsTunnel::UpdateTorrentInfo (std::shared_ptr<Torrent> torrent, std::string_view info)
	{
		if (!torrent || torrent->IsRemoved () || torrent->IsStopped ()) return;
		torrent->ParseInfo (info);
		// The info hash was already verified, but a hash-valid document can
		// still be unusable: an unsupported version or a broken file layout
		// records an error instead of building a file list. Persisting it or
		// creating payload files for a torrent that can never run would leave
		// half-initialised state behind, so stop before either step.
		if (torrent->GetError () != eTorrentErrorNoError)
		{
			LogPrint (eLogError, "TorrentsTunnel: Rejecting unusable magnet metadata: ", torrent->GetErrorStr ());
			return;
		}
		if (torrent->IsValid ()) LoadTorrentAdminState (torrent);
		if (torrent->IsPrivate ())
		{
			// Metadata may reveal a private swarm after trackerless discovery.
			// Purge every DHT trace collected before the metadata arrived and
			// refuse later get_peers/announce_peer for this hash, then drop all
			// pre-metadata peers; resume only from authorized trackers.
			if (m_DHT) m_DHT->MarkPrivate (torrent->GetInfoHash ());
			auto connections = torrent->GetConnections ();
			for (const auto & connection : connections) connection->Close ();
		}
		if (!SetTorrentPathWithinRoot (torrent))
		{
			torrent->SetError (eTorrentErrorMalformedMetaInfo);
			LogPrint (eLogError, "TorrentsTunnel: Magnet metadata has an unsafe name");
			return;
		}
		auto self = std::static_pointer_cast<TorrentsTunnel> (weak_from_this ().lock ());
		if (!self) return;
		boost::asio::post (GetDiskIOService (),
			[self, torrent, content = torrent->CreateTorrentFileContent ()] ()
			{
				if (self->m_Stopping || torrent->IsRemoved () || self->FindTorrent (torrent->GetInfoHash ()) != torrent)
					return;
				self->SaveTorrentFile (torrent, content);
				self->InitTorrentFiles (torrent);
			});
	}

	void TorrentsTunnel::RevokeInboundAccepts ()
	{
		std::lock_guard<std::mutex> publication (m_InboundAcceptMutex);
		m_InboundAcceptStopped = true;
		if (m_PeerDialGeneration)
		{
			std::lock_guard<std::mutex> admission (m_PeerDialGeneration->mutex);
			m_PeerDialGeneration->active = false;
		}
		if (m_InboundAcceptGeneration)
		{
			std::lock_guard<std::mutex> admission (m_InboundAcceptGeneration->mutex);
			m_InboundAcceptGeneration->active = false;
		}
	}

	i2p::stream::StreamingDestination::Acceptor TorrentsTunnel::CreateInboundAcceptor ()
	{
		std::weak_ptr<TorrentsTunnel> owner = std::static_pointer_cast<TorrentsTunnel> (weak_from_this ().lock ());
		if (owner.expired ()) return { };
		auto generation = std::make_shared<InboundAcceptGeneration> ();
		{
			std::lock_guard<std::mutex> publication (m_InboundAcceptMutex);
			if (m_InboundAcceptStopped) return { };
			if (m_InboundAcceptGeneration)
			{
				std::lock_guard<std::mutex> admission (m_InboundAcceptGeneration->mutex);
				m_InboundAcceptGeneration->active = false;
			}
			m_InboundAcceptGeneration = generation;
		}
		return [owner, generation] (std::shared_ptr<i2p::stream::Stream> stream)
		{
			if (!stream) return;
			auto self = owner.lock ();
			if (!self)
			{
				stream->Close ();
				return;
			}
			// Streaming dispatches on the destination service. Keep shutdown
			// admission serialized until the handler and its receive exist.
			std::unique_lock<std::mutex> admission (generation->mutex);
			if (!generation->active || self->m_Stopping)
			{
				admission.unlock ();
				stream->Close ();
				return;
			}
			if (self->CountPeerHandlers () >= MAX_TOTAL_PEERS)
			{
				admission.unlock ();
				LogPrint (
					eLogWarning, "TorrentsTunnel: Refusing inbound peer at the connection limit ", MAX_TOTAL_PEERS);
				stream->Close ();
				return;
			}
			auto connection = std::make_shared<PeerConnection> (self, stream);
			self->AddHandler (connection);
			connection->ReceiveHandshake ();
		};
	}

	void TorrentsTunnel::Accept ()
	{
		auto localDestination = GetLocalDestination ();
		if (localDestination)
		{
			if (!localDestination->IsAcceptingStreams ()) // set it as default if not set yet
			{
				auto acceptor = CreateInboundAcceptor ();
				if (acceptor)
				{
					// SetAcceptor publishes immediately but posts dispatch, so
					// registration can share the synchronous stop barrier.
					std::lock_guard<std::mutex> publication (m_InboundAcceptMutex);
					if (!m_InboundAcceptStopped && !m_Stopping) localDestination->AcceptStreams (acceptor);
				}
			}
		}
		else
			LogPrint (eLogError, "TorrentsTunnel: Local destination not set");
	}

	size_t TorrentsTunnel::CountPeerHandlers ()
	{
		size_t count = 0;
		IterateHandlers (
			[&count] (std::shared_ptr<i2p::client::I2PServiceHandler> handler)
			{
				if (handler) count++;
			});
		return count;
	}

	bool TorrentsTunnel::CanDial (Torrent & torrent)
	{
		size_t torrentDials = 0;
		for (const auto & pending : m_PendingDials)
			if (pending.second.torrent.lock ().get () == &torrent) torrentDials++;
		if (torrentDials >= MAX_DIALS_PER_TORRENT) return false;
		if (m_PendingDials.size () >= MAX_TOTAL_DIALS) return false;
		if (torrent.GetConnections ().size () + torrentDials >= MAX_PEERS_PER_TORRENT) return false;
		return CountPeerHandlers () + m_PendingDials.size () < MAX_TOTAL_PEERS;
	}

	void TorrentsTunnel::FinishDial (uint64_t dial)
	{
		m_PendingDials.erase (dial);
	}

	void TorrentsTunnel::ExpireDials (uint64_t now)
	{
		for (auto it = m_PendingDials.begin (); it != m_PendingDials.end ();)
		{
			// A completed dial erases itself; these entries only survive when
			// the completion callback never ran (for example a destination
			// that was not ready). Never let them pin a slot forever.
			if (it->second.torrent.expired () || now > it->second.started + PEER_DIAL_TIMEOUT)
				it = m_PendingDials.erase (it);
			else
				++it;
		}
	}

	void TorrentsTunnel::RequestTorrentTrackers (std::shared_ptr<Torrent> torrent, TrackerAnnounceEvent event)
	{
		if (m_StartupRecoveryPending.load ()) return;
		if (!torrent || !torrent->HasPeerID () || (torrent->IsStopped () && event != eTrackerAnnounceEventStopped))
			return;
		for (size_t i = 0; i < m_Trackers.size (); i++) RequestTracker (i, torrent, event);
	}

	bool TorrentsTunnel::IsTorrentTracker (size_t trackerID, const Torrent & torrent) const
	{
		// A released, non-common slot keeps its index but carries no announce
		// URL: it must never match, be announced to or appear in a snapshot.
		if (!IsTrackerSlotLive (m_TrackerSlots, trackerID)) return false;
		const auto & url = std::get<0> (m_Trackers[trackerID]);
		const auto matches = [&url] (std::string_view announce)
		{ return !announce.empty () && url.to_string () == i2p::http::URL (announce).to_string (); };
		if (torrent.IsPrivate ())
		{
			// A private swarm announces to its primary announce URL only: the
			// "announce" key or the first entry of the first tier. Secondary
			// tiers are never contacted, which keeps the swarm membership
			// closed.
			if (matches (torrent.GetAnnounce ())) return true;
			const auto & tiers = torrent.GetTrackerTiers ();
			return !tiers.empty () && !tiers.front ().empty () && matches (tiers.front ().front ());
		}
		for (const auto & tier : torrent.GetTrackerTiers ())
			for (const auto & announce : tier)
				if (matches (announce)) return true;
		if (torrent.GetTrackerTiers ().empty () && matches (torrent.GetAnnounce ())) return true;
		// Common trackers are only announced for public torrents.
		return !torrent.HasExplicitTrackers () && std::get<1> (m_Trackers[trackerID]);
	}

	bool TorrentsTunnel::RequestTracker (size_t trackerID, std::shared_ptr<Torrent> torrent, TrackerAnnounceEvent event)
	{
		if (!torrent || !torrent->HasPeerID () || !IsTorrentTracker (trackerID, *torrent)) return false;
		if (event != eTrackerAnnounceEventStopped && !CanDiscoverTorrent (torrent)) return false;
		i2p::http::URL reqURL = std::get<0> (m_Trackers[trackerID]);
		if (!IsI2PTrackerURL (reqURL))
		{
			LogPrint (
				eLogWarning, "TorrentsTunnel: Non-I2P address ", reqURL.host, " for torrent ", torrent->GetName ());
			return false;
		}
		if (reqURL.schema == "udp")
		{
			// BEP 15 defaults an omitted datagram-tracker port to 6969. Apply
			// it at the use site: rewriting the stored URL would break the
			// exact announce-URL identity used by IsTorrentTracker().
			const uint16_t datagramPort = reqURL.port ? reqURL.port : DATAGRAM_TRACKER_DEFAULT_PORT;
			auto ts = i2p::util::GetMonotonicMilliseconds ();
			auto & [announce, common, connectionID, expiration, fromPort] = m_Trackers[trackerID];
			if (connectionID && ts <= expiration)
				SendAnnounceToDatagramTracker (
					trackerID, connectionID, torrent, reqURL.host, datagramPort, fromPort, event);
			else
			{
				if (!expiration || ts >= expiration) // don't connect if pending connection
				{
					connectionID = 0;
					expiration = ts + DATAGRAM_TRACKER_TRANSACTION_TIMEOUT;
					ConnectToDatagramTracker (trackerID, reqURL.host, datagramPort);
				}
			}
			return true;
		}
		// Single-flight admission, before any announce parameter is built: a
		// tracker that never answers must not accumulate parallel requests
		// from the retry ladder, and the deadline armed below is the only
		// path that retires a request whose completion never arrives.
		const TrackerRequestKey key { torrent->GetInfoHash (), trackerID };
		if (!AcquireTrackerRequest (key, torrent))
		{
			LogPrint (eLogDebug, "TorrentsTunnel: Tracker ", trackerID,
				" announce already in flight or at the request limit");
			return false;
		}
		const auto authority = m_TrackerRequests.Authority (key);
		std::map<std::string, std::string> params;
		params.emplace ("info_hash", torrent->GetHexStringInfoHash ());
		// Percent-encoded per-torrent random peer id. The tunnel identity is
		// never announced and never used as a peer id.
		const auto & peerID = torrent->GetPeerID ();
		params.emplace ("peer_id",
			PercentEncodeQueryValue (
				std::string_view (reinterpret_cast<const char *> (peerID.data ()), peerID.size ())));
		params.emplace ("ip", GetLocalDestination ()->GetIdentity ()->ToBase64 () + ".i2p");
		params.emplace ("port", std::to_string (TORRENT_PORT)); // 6881
		params.emplace ("compact", "1");
		params.emplace ("uploaded", std::to_string (torrent->GetUploaded ()));
		const auto progress = torrent->GetAnnounceProgress ();
		params.emplace ("downloaded", std::to_string (torrent->GetLength () - torrent->GetFullLeft ()));
		// Whole-torrent "left" (BEP 3 / Java TrackerClient), with the phony 1
		// for a magnet that has no metadata yet. A skip-selected torrent
		// still reports its missing pieces here even though the UI shows it
		// complete.
		params.emplace ("left", std::to_string (progress.left));
		// Echo the opaque tracker id a previous response supplied (BEP 3).
		auto trackerIDValue = torrent->GetTrackerID (trackerID);
		if (!trackerIDValue.empty ()) params.emplace ("trackerid", PercentEncodeQueryValue (trackerIDValue));
		// event=completed is a whole-torrent statement: never send it while
		// any piece is still missing, even when the file selection is
		// complete.
		if (event == eTrackerAnnounceEventCompleted && !progress.completed) event = eTrackerAnnounceEventNone;
		int numWant = 0;
		if (!torrent->IsComplete () && (event == eTrackerAnnounceEventNone || event == eTrackerAnnounceEventStarted))
			numWant = TRACKER_MAX_NUM_WANT;
		params.emplace ("numwant", std::to_string (numWant));
		if (event != eTrackerAnnounceEventNone) params.emplace ("event", TrackerAnnounceEventStr[event]);
		reqURL.create_query (params);

		auto req = std::make_shared<boost::beast::http::request<boost::beast::http::string_body>> (
			boost::beast::http::verb::get, reqURL.to_string (true), 11); // HTTP 1.1
		req->set (boost::beast::http::field::host, reqURL.host);
		req->set (boost::beast::http::field::user_agent, "I2PSocketEepGet");
		req->keep_alive (false); // Connection: close
		std::weak_ptr<i2p::client::I2PService> weakSelf = weak_from_this ();
		CreateStream (
			[weakSelf, req, torrent, key, authority, event] (std::shared_ptr<i2p::stream::Stream> stream)
			{
				auto self = std::static_pointer_cast<TorrentsTunnel> (weakSelf.lock ());
				if (!self)
				{
					if (stream) stream->AsyncClose ();
					return;
				}
				// A dial that never produced a stream is retired by the deadline
				// timer, which reports the failure and releases the key.
				if (!stream) return;
				// A request retired by the deadline (or by a stop/cancel) must not
				// keep a dialed stream open. A live request still requires a live
				// slot and matching membership, except for an event=stopped
				// announce: it was authorized when the request was created and must
				// reach the tracker even when removal released the slot meanwhile.
				const bool stoppedEvent = event == eTrackerAnnounceEventStopped;
				const bool authorized = stoppedEvent
					|| (IsTrackerSlotLive (self->m_TrackerSlots, key.trackerID)
						&& self->IsTorrentTracker (key.trackerID, *torrent));
				if (!self->m_TrackerRequests.Find (key, authority) || !authorized
					|| (!stoppedEvent && torrent->IsStopped ()))
				{
					stream->AsyncClose ();
					self->ReleaseTrackerRequest (key, authority);
					return;
				}
				if (auto * state = self->m_TrackerRequests.Find (key, authority)) state->stream = stream;
				auto httpStream = std::make_shared<i2p::client::BoostAsyncStream> (stream);
				boost::beast::http::async_write (*httpStream, *req,
					[weakSelf, httpStream, torrent, req, key, authority] (
						const boost::beast::error_code & ecode, size_t bytes_transferred)
					{
						auto self = std::static_pointer_cast<TorrentsTunnel> (weakSelf.lock ());
						if (!self) return;
						self->TrackerRequestSent (ecode, bytes_transferred, httpStream, torrent, req, key, authority);
					});
			},
			reqURL.host, reqURL.port);
		return true;
	}

	void TorrentsTunnel::AcquireTrackerSlot (size_t trackerID)
	{
		std::lock_guard<std::mutex> lock (m_TrackerRegistryMutex);
		AcquireTrackerSlotReference (m_TrackerSlots, trackerID);
	}

	bool TorrentsTunnel::ReleaseTrackerSlot (size_t trackerID)
	{
		std::lock_guard<std::mutex> lock (m_TrackerRegistryMutex);
		const auto outcome = ReleaseTrackerSlotReference (m_TrackerSlots, trackerID);
		if (outcome == TrackerSlotRelease::Invalid) return false;
		if (outcome == TrackerSlotRelease::Freed && trackerID < m_Trackers.size ())
		{
			// Free slot: keep the index (it is never renumbered) but drop the
			// announce URL so no membership, snapshot or retry can match it. The
			// datagram connection state is reset with it.
			m_Trackers[trackerID] = TrackerInfo { i2p::http::URL { }, false, 0, 0, 0 };
		}
		return true;
	}

	std::optional<size_t> TorrentsTunnel::ResolveTrackerSlot (std::string_view announce, bool isCommon, bool create)
	{
		std::lock_guard<std::mutex> lock (m_TrackerRegistryMutex);
		if (announce.empty ()
			|| std::any_of (announce.begin (), announce.end (), [] (unsigned char c) { return c <= 32 || c == 127; }))
			return std::nullopt;
		if (announce.size () > MAX_TRACKER_ANNOUNCE_LENGTH)
		{
			LogPrint (eLogWarning, "TorrentsTunnel: Ignoring over-long tracker announce");
			return std::nullopt;
		}
		i2p::http::URL reqURL (announce);
		if (!IsI2PTrackerURL (reqURL))
		{
			LogPrint (eLogInfo, "TorrentsTunnel: Non-I2P address ", reqURL.host, " in tracker announce");
			return std::nullopt;
		}
		const auto normalized = reqURL.to_string ();
		std::optional<size_t> existing;
		for (size_t i = 0; i < m_Trackers.size (); i++)
		{
			if (!IsTrackerSlotLive (m_TrackerSlots, i)) continue;
			// Paths and schemes identify distinct tracker endpoints even on the
			// same I2P destination; do not merge private announce URLs.
			if (std::get<0> (m_Trackers[i]).to_string () == normalized)
			{
				existing = i;
				break;
			}
		}
		if (!create) return existing;
		const auto admission = AdmitTrackerSlot (m_TrackerSlots, existing);
		if (!admission.trackerID)
		{
			LogPrint (eLogWarning, "TorrentsTunnel: Ignoring tracker above the limit ", MAX_NUM_TRACKERS);
			return std::nullopt;
		}
		const size_t index = *admission.trackerID;
		if (!admission.existing)
		{
			// A released slot is re-initialised here: no connection id, no
			// expiration and no from_port survive from the previous announce
			// URL.
			if (index < m_Trackers.size ())
			{
				m_Trackers[index] = TrackerInfo { reqURL, isCommon, 0, 0, 0 };
				m_TrackerSlots[index] = TrackerSlotState { 0, isCommon };
			}
			else
			{
				m_Trackers.emplace_back (TrackerInfo { reqURL, isCommon, 0, 0, 0 });
				m_TrackerSlots.emplace_back (TrackerSlotState { 0, isCommon });
			}
		}
		return index;
	}

	void TorrentsTunnel::TrackerRequestSent (const boost::beast::error_code & ecode, size_t,
		std::shared_ptr<i2p::client::BoostAsyncStream> httpStream, std::shared_ptr<Torrent> torrent,
		std::shared_ptr<boost::beast::http::request<boost::beast::http::string_body>>, const TrackerRequestKey & key,
		uint64_t authority)
	{
		const size_t trackerID = key.trackerID;
		// A completion that arrives after the deadline (or after a
		// stop/cancel) retired the request must not add a second step to the
		// failure ladder.
		const bool wasInFlight = m_TrackerRequests.Find (key, authority);
		if (!wasInFlight)
		{
			httpStream->GetStream ()->AsyncClose ();
			return;
		}
		if (ecode)
		{
			ReleaseTrackerRequest (key, authority);
			LogPrint (eLogWarning, "TorrentsTunnel: Tracker ", trackerID, " request failed: ", ecode.message ());
			if (wasInFlight && !torrent->IsStopped ()) torrent->ReportTrackerFailure (trackerID, "request failed");
			httpStream->GetStream ()->AsyncClose ();
		}
		else
		{
			// The request stays admitted until the response read completes. The
			// deadline armed at dispatch is the read deadline too: releasing it
			// here would let a tracker stall the announce forever after
			// accepting the request. Receive. A parser (rather than a bare
			// response) enforces a body limit, so a hostile tracker cannot make
			// the client buffer an unbounded document before it is parsed.
			auto buf = std::make_shared<boost::beast::flat_buffer> ();
			auto parser = std::make_shared<boost::beast::http::response_parser<boost::beast::http::string_body>> ();
			parser->body_limit (MAX_TRACKER_RESPONSE_BODY);
			std::weak_ptr<i2p::client::I2PService> weakSelf = weak_from_this ();
			boost::beast::http::async_read (*httpStream, *buf, *parser,
				[weakSelf, httpStream, torrent, buf, parser, key, authority] (
					const boost::beast::error_code & ecode, size_t)
				{
					auto self = std::static_pointer_cast<TorrentsTunnel> (weakSelf.lock ());
					if (!self) return;
					const size_t trackerID = key.trackerID;
					// Retire only this admission, including its deadline. A stale
					// successful body must not update a replacement admission.
					const bool wasInFlight = self->m_TrackerRequests.Find (key, authority);
					self->ReleaseTrackerRequest (key, authority);
					httpStream->GetStream ()->AsyncClose ();
					if (!wasInFlight) return;
					if (ecode)
					{
						LogPrint (eLogWarning, "TorrentsTunnel: Tracker ", trackerID,
							" response rejected: ", ecode.message ());
						if (wasInFlight && !torrent->IsStopped ())
							torrent->ReportTrackerFailure (trackerID, "response rejected");
						return;
					}
					if (!torrent->IsStopped () && self->IsTorrentTracker (trackerID, *torrent))
					{
						const auto & res = parser->get ();
						if (res.result () == boost::beast::http::status::ok)
						{
							torrent->ParseTrackerResponse (trackerID, res.body ());
							self->ConnectToPeers (torrent, trackerID);
						}
						else
						{
							LogPrint (eLogWarning, "TorrentsTunnel: Tracker ", trackerID, " response code ",
								res.result_int ());
							torrent->ReportTrackerFailure (trackerID, "HTTP error");
						}
					}
				});
		}
	}

	bool TorrentsTunnel::AcquireTrackerRequest (const TrackerRequestKey & key, const std::shared_ptr<Torrent> & torrent)
	{
		const auto admission = m_TrackerRequests.TryAcquire (
			key, TrackerRequestState { nullptr, { }, torrent, i2p::util::GetMonotonicMilliseconds () });
		if (admission != TrackerInflightRegistry<TrackerRequestState>::Admission::Accepted) return false;
		ScheduleTrackerRequestDeadline (key, m_TrackerRequests.Authority (key));
		return true;
	}

	void TorrentsTunnel::ReleaseTrackerRequest (const TrackerRequestKey & key, uint64_t authority)
	{
		// Cancellation may race an already successful timer completion.
		// Authority also rejects that completion after key reuse.
		m_TrackerRequests.Release (key, authority);
	}

	void TorrentsTunnel::CancelTrackerRequests (const Torrent::InfoHash & infoHash)
	{
		// Close every matching stream before dropping the entries: an erased
		// entry also erases its deadline, and a stream left open would never
		// be bounded again. Late callbacks carry the retired authority and
		// cannot release or update a replacement request with the same key.
		std::vector<std::shared_ptr<i2p::stream::Stream>> streams;
		m_TrackerRequests.ForEachTorrent (infoHash,
			[&streams] (const TrackerRequestKey &, TrackerRequestState & state)
			{
				if (auto stream = state.stream.lock ()) streams.push_back (std::move (stream));
			});
		const auto released = m_TrackerRequests.ReleaseTorrent (infoHash);
		for (const auto & stream : streams) stream->AsyncClose ();
		if (!released.empty ())
			LogPrint (eLogDebug, "TorrentsTunnel: Cancelled ", released.size (), " in-flight tracker request(s)");
	}

	void TorrentsTunnel::CancelAllTrackerRequests ()
	{
		std::vector<std::shared_ptr<i2p::stream::Stream>> streams;
		m_TrackerRequests.ForEach (
			[&streams] (const TrackerRequestKey &, TrackerRequestState & state)
			{
				if (auto stream = state.stream.lock ()) streams.push_back (std::move (stream));
			});
		m_TrackerRequests.ReleaseAll ();
		for (const auto & stream : streams) stream->AsyncClose ();
	}

	void TorrentsTunnel::ScheduleTrackerRequestDeadline (const TrackerRequestKey & key, uint64_t authority)
	{
		auto * state = m_TrackerRequests.Find (key, authority);
		if (!state) return;
		auto deadline = std::make_shared<boost::asio::steady_timer> (GetService ());
		deadline->expires_after (std::chrono::seconds (TRACKER_RESPONSE_TIMEOUT));
		state->deadline = deadline;
		std::weak_ptr<i2p::client::I2PService> weakSelf = weak_from_this ();
		deadline->async_wait (
			[weakSelf, key, authority] (const boost::system::error_code & ecode)
			{
				// A canceled wait does nothing. An already successful completion
				// also needs its admission authority checked after retirement.
				if (ecode) return;
				auto self = std::static_pointer_cast<TorrentsTunnel> (weakSelf.lock ());
				if (self) self->HandleTrackerRequestTimeout (key, authority);
			});
	}

	void TorrentsTunnel::HandleTrackerRequestTimeout (const TrackerRequestKey & key, uint64_t authority)
	{
		auto * state = m_TrackerRequests.Find (key, authority);
		if (!state) return;
		// The timer that is dispatching this handler is owned by the entry;
		// keep it alive across the erase below.
		[[maybe_unused]] auto keepAlive = state->deadline;
		auto torrent = state->torrent.lock ();
		auto stream = state->stream.lock ();
		m_TrackerRequests.Release (key, authority);
		LogPrint (eLogWarning, "TorrentsTunnel: Tracker ", key.trackerID, " announce timed out after ",
			TRACKER_RESPONSE_TIMEOUT, " seconds");
		if (torrent && !torrent->IsStopped ()) torrent->ReportTrackerFailure (key.trackerID, "No response");
		if (stream) stream->AsyncClose ();
	}

	void TorrentsTunnel::ConnectToPeer (
		std::shared_ptr<Torrent> torrent, const i2p::data::IdentHash & peer, PeerConnectionOrigin origin)
	{
		if (!torrent || torrent->IsStopped () || torrent->IsConnectedToPeer (peer)
			|| (torrent->IsPrivate () && origin != ePeerConnectionOriginTracker))
			return;
		if (!CanDial (*torrent))
		{
			LogPrint (eLogDebug, "TorrentsTunnel: Peer dial limit reached; deferring connection");
			return;
		}
		LogPrint (eLogDebug, "TorrentsTunnel: Connecting to peer ", peer.ToBase32 () + ".b32.i2p");
		if (peer == GetLocalDestination ()->GetIdentHash ())
		{
			LogPrint (eLogInfo, "TorrentsTunnel: Can't connect to self");
			return;
		}
		auto complete = CreatePeerDialCompletion (torrent, peer, origin);
		if (complete) CreateStream (std::move (complete), std::make_shared<i2p::client::Address> (peer), TORRENT_PORT);
	}

	void TorrentsTunnel::StartPeerDials ()
	{
		std::lock_guard<std::mutex> publication (m_InboundAcceptMutex);
		if (m_PeerDialGeneration)
		{
			std::lock_guard<std::mutex> admission (m_PeerDialGeneration->mutex);
			m_PeerDialGeneration->active = false;
		}
		m_PeerDialGeneration = std::make_shared<InboundAcceptGeneration> ();
	}

	i2p::client::StreamRequestComplete TorrentsTunnel::CreatePeerDialCompletion (
		std::shared_ptr<Torrent> torrent, const i2p::data::IdentHash & peer, PeerConnectionOrigin origin)
	{
		std::weak_ptr<TorrentsTunnel> owner = std::static_pointer_cast<TorrentsTunnel> (weak_from_this ().lock ());
		std::shared_ptr<InboundAcceptGeneration> generation;
		uint64_t dial;
		{
			std::lock_guard<std::mutex> publication (m_InboundAcceptMutex);
			generation = m_PeerDialGeneration;
			if (!generation || owner.expired ()) return { };
			std::lock_guard<std::mutex> admission (generation->mutex);
			if (!generation->active || m_InboundAcceptStopped || m_Stopping || !torrent || !m_NextDialID
				|| !CanDial (*torrent))
				return { };
			const uint64_t now = i2p::util::GetMonotonicSeconds ();
			dial = m_NextDialID++;
			m_PendingDials.emplace (dial, PendingDial { torrent, now });
			torrent->MarkPeerCandidateAttempt (peer, now);
		}
		return [owner, generation, torrent, peer, origin, dial] (std::shared_ptr<i2p::stream::Stream> stream)
		{
			auto self = owner.lock ();
			if (!self)
			{
				if (stream) stream->AsyncClose ();
				return;
			}
			// Stop revokes through this gate before clearing handlers. Connect
			// posts IO and does not synchronously enter tunnel lifecycle calls.
			std::unique_lock<std::mutex> admission (generation->mutex);
			if (!generation->active || self->m_Stopping)
			{
				admission.unlock ();
				if (stream) stream->AsyncClose ();
				return;
			}
			auto pending = self->m_PendingDials.find (dial);
			if (pending == self->m_PendingDials.end () || pending->second.torrent.lock () != torrent)
			{
				admission.unlock ();
				if (stream) stream->AsyncClose ();
				return;
			}
			self->FinishDial (dial);
			if (stream && !torrent->IsStopped () && !torrent->IsRemoved ()
				&& self->FindTorrent (torrent->GetInfoHash ()) == torrent
				&& (!torrent->IsPrivate () || origin == ePeerConnectionOriginTracker))
			{
				torrent->MarkPeerCandidateConnected (peer);
				auto connection = std::make_shared<PeerConnection> (self, stream, torrent, origin);
				self->AddHandler (connection);
				connection->Connect ();
			}
			else
			{
				if (stream) stream->AsyncClose ();
				if (!torrent->IsStopped () && !torrent->IsRemoved ())
					torrent->MarkPeerCandidateFailed (peer, i2p::util::GetMonotonicSeconds ());
			}
		};
	}

	size_t TorrentsTunnel::ConnectToPeers (std::shared_ptr<Torrent> torrent)
	{
		if (!torrent || torrent->IsStopped () || torrent->IsComplete ()) return 0;
		if (torrent->IsPrivate ())
		{
			size_t count = 0;
			for (size_t i = 0; i < m_Trackers.size (); ++i)
				if (IsTorrentTracker (i, *torrent)) count += ConnectToPeers (torrent, i);
			return count;
		}
		auto peersToConnect = torrent->GetNonConnectedPeers (); // from trackers
		if (!peersToConnect.empty ())
		{
			for (const auto & it : peersToConnect) ConnectToPeer (torrent, it, ePeerConnectionOriginTracker);
		}
		return peersToConnect.size ();
	}

	size_t TorrentsTunnel::ConnectToPeers (std::shared_ptr<Torrent> torrent, size_t trackerID)
	{
		if (!torrent || torrent->IsStopped () || torrent->IsComplete () || !IsTorrentTracker (trackerID, *torrent))
			return 0;
		auto peersToConnect = torrent->GetNonConnectedPeers (trackerID);
		if (!peersToConnect.empty ())
		{
			for (const auto & it : peersToConnect) ConnectToPeer (torrent, it, ePeerConnectionOriginTracker);
		}
		return peersToConnect.size ();
	}

	void TorrentsTunnel::ConnectToNewPeers (std::shared_ptr<Torrent> torrent,
		std::unordered_set<i2p::data::IdentHash> & newPeers, PeerConnectionOrigin origin)
	{
		if (!torrent || torrent->IsStopped () || torrent->IsComplete () || torrent->IsPrivate ()) return;
		if (newPeers.empty ()) return;
		const uint64_t now = i2p::util::GetMonotonicSeconds ();
		if (!torrent->AdmitPeerCandidates (origin, newPeers.size (), now)) return;
		for (const auto & peer : newPeers) torrent->AddPeerCandidate (peer, origin, now);
		DrainPeerCandidates (torrent, now);
	}

	void TorrentsTunnel::DrainPeerCandidates (std::shared_ptr<Torrent> torrent, uint64_t now)
	{
		if (!torrent || torrent->IsStopped () || torrent->IsComplete () || torrent->IsPrivate ()) return;
		// The table returns candidates whose per-origin TTL and retry backoff
		// allow another attempt. ConnectToPeer re-checks the torrent/private
		// state and the per-torrent dial budget.
		for (const auto & peer : torrent->GetPeerCandidatesToConnect (now, MAX_TOTAL_DIALS))
			ConnectToPeer (torrent, peer, torrent->GetPeerCandidateOrigin (peer));
	}

	void TorrentsTunnel::ScheduleTrackerRequestsCheck ()
	{
		m_TrackerRequestsCheckTimer.expires_after (std::chrono::milliseconds (TRACKER_REQUESTS_CHECK_TIMEOUT));
		m_TrackerRequestsCheckTimer.async_wait (
			std::bind (&TorrentsTunnel::HandleTrackerRequestsCheckTimer, this, std::placeholders::_1));
	}

	void TorrentsTunnel::HandleTrackerRequestsCheckTimer (const boost::system::error_code & ecode)
	{
		if (ecode != boost::asio::error::operation_aborted)
		{
			auto ts = i2p::util::GetMonotonicMilliseconds ();
			if (!m_DatragramTrackerTransactions.empty ())
			{
				// cleanup  expired transactions
				auto it = m_DatragramTrackerTransactions.begin ();
				while (it != m_DatragramTrackerTransactions.end ())
				{
					if (ts > std::get<3> (it->second) + DATAGRAM_TRACKER_TRANSACTION_TIMEOUT)
						it = m_DatragramTrackerTransactions.erase (it);
					else
						it++;
				}
			}
			// Take a snapshot: RPC removal commands mutate the session map from
			// their own thread. Iterating m_Torrents directly can invalidate the
			// iterator and dereference a freed torrent.
			const auto nowEpoch = i2p::util::GetSecondsSinceEpoch ();
			for (auto torrent : GetTorrents ())
			{
				torrent->ExpireTrackerPeers (nowEpoch);
				// A torrent with a metadata or filesystem error has no valid
				// storage contract; do not advertise it as a live peer.
				if (CanDiscoverTorrent (torrent))
				{
					for (size_t i = 0; i < m_Trackers.size (); i++)
					{
						// A released, non-common slot keeps its index but must not be
						// polled, retried or announced to.
						if (!IsTrackerSlotLive (m_TrackerSlots, i)) continue;
						if (!torrent->GetNextTrackerRequestTime (i) && // first time
							IsTorrentTracker (i, *torrent))
						{
							auto initialInterval
								= GetLocalDestination ()->GetRng () () % TRACKER_INITIAL_REQUEST_INTERVAL_VARIANCE;
							if (initialInterval <= TRACKER_REQUESTS_CHECK_TIMEOUT)
								initialInterval = 0; // request immeditely
							torrent->SetNextTrackerRequestTime (i, ts + initialInterval);
						}
						if (ts >= torrent->GetNextTrackerRequestTime (i))
						{
							if (i2p::util::GetSecondsSinceEpoch ()
									> torrent->GetLastTrackerUpdateTime (i) + 2 * torrent->GetInterval (i) / 1000
								&& torrent->GetTrackerError (i).empty ())
								torrent->ReportTrackerFailure (i, "No response");
							if (RequestTracker (i, torrent, eTrackerAnnounceEventNone))
							{
								// Dispatching a request is not a success. While the
								// tracker is still failing, the bounded retry ladder
								// (already applied by ReportTrackerFailure) owns the next
								// attempt; copying the advertised 15-minute interval over
								// it would silence the retry for the whole floor.
								torrent->SetNextTrackerRequestTime (i,
									NextTrackerDispatchTime (ts, !torrent->GetTrackerError (i).empty (),
										torrent->GetNextTrackerRequestTime (i), torrent->GetInterval (i),
										torrent->GetMinInterval (i),
										GetLocalDestination ()->GetRng () () % TRACKER_REQUESTS_INTERVAL_VARIANCE));
							}
						}
					}
					if (SupportsDHT () && !torrent->IsPrivate ())
					{
						uint64_t nextUpdateTime = torrent->GetNextDHTUpdateTime () * 1000;
						if (nextUpdateTime)
						{
							if (ts >= nextUpdateTime)
							{
								if (m_DHT && !torrent->IsPrivate ()) m_DHT->GetPeersAndAnnounce (torrent);
								if (torrent->GetLastDHTAnnounceTime ())
									torrent->SetNextDHTUpdateTime (ts / 1000 + DHT_TORRENT_UPDATE_INTERVAL
										+ GetLocalDestination ()->GetRng () () % DHT_TORRENT_UPDATE_INTERVAL_VARIANCE);
								else
									torrent->SetNextDHTUpdateTime (ts / 1000 + DHT_TORRENT_INITIAL_UPDATE_INTERVAL
										+ GetLocalDestination ()->GetRng () ()
											% DHT_TORRENT_INITIAL_UPDATE_INTERVAL_VARIANCE);
							}
						}
						else
							torrent->SetNextDHTUpdateTime (ts / 1000 + DHT_TORRENT_INITIAL_UPDATE_INTERVAL
								+ GetLocalDestination ()->GetRng () () % DHT_TORRENT_INITIAL_UPDATE_INTERVAL_VARIANCE);
					}
				}
			}
			ScheduleTrackerRequestsCheck ();
		}
	}

	void TorrentsTunnel::ScheduleKeepAliveCheck ()
	{
		m_KeepAliveCheckTimer.expires_after (std::chrono::seconds (PEER_KEEP_ALIVE_CHECK_INTERVAL));
		m_KeepAliveCheckTimer.async_wait (
			std::bind (&TorrentsTunnel::HandleKeepAliveCheckTimer, this, std::placeholders::_1));
	}

	void TorrentsTunnel::HandleKeepAliveCheckTimer (const boost::system::error_code & ecode)
	{
		if (ecode != boost::asio::error::operation_aborted)
		{
			auto ts = i2p::util::GetMonotonicSeconds ();
			IterateHandlers (
				[ts] (std::shared_ptr<i2p::client::I2PServiceHandler> handler)
				{
					if (handler) std::static_pointer_cast<PeerConnection> (handler)->CheckKeepAlive (ts);
				});
			ScheduleKeepAliveCheck ();
		}
	}

	void TorrentsTunnel::ScheduleReconnectCheck ()
	{
		m_ReconnectCheckTimer.cancel ();
		m_ReconnectCheckTimer.expires_after (std::chrono::seconds (RECONNECT_CHECK_INTERVAL));
		m_ReconnectCheckTimer.async_wait (
			std::bind (&TorrentsTunnel::HandleReconnectCheckTimer, this, std::placeholders::_1));
	}

	void TorrentsTunnel::HandleReconnectCheckTimer (const boost::system::error_code & ecode)
	{
		if (ecode != boost::asio::error::operation_aborted)
		{
			auto ts = i2p::util::GetMonotonicSeconds ();
			for (auto torrent : GetTorrents ())
				if (ts > torrent->GetNextReconnectTime ())
				{
					if (!torrent->IsComplete () && !torrent->IsStopped ())
					{
						auto numPeers = ConnectToPeers (torrent);
						DrainPeerCandidates (torrent, ts);
						if (numPeers) LogPrint (eLogDebug, "TorrentsTunnel: Reconnecting to ", numPeers, " peers");
					}
					torrent->SetNextReconnectTime (
						ts + RECONNECT_INTERVAL + GetLocalDestination ()->GetRng () () % RECONNECT_INTERVAL_VARIANCE);
				}

			ScheduleReconnectCheck ();
		}
	}

	void TorrentsTunnel::ScheduleStatusUpdate ()
	{
		m_TorrentsStatusUpdateTimer.cancel ();
		m_TorrentsStatusUpdateTimer.expires_after (std::chrono::seconds (TORRENTS_STATUS_UPDATE_CHECK_INTERVAL));
		m_TorrentsStatusUpdateTimer.async_wait (
			std::bind (&TorrentsTunnel::HandleTorrentsStatusUpdateTimer, this, std::placeholders::_1));
	}

	void TorrentsTunnel::HandleTorrentsStatusUpdateTimer (const boost::system::error_code & ecode)
	{
		if (ecode != boost::asio::error::operation_aborted)
		{
			auto ts = i2p::util::GetMonotonicSeconds ();
			ExpireDials (ts);
			for (auto torrent : GetTorrents ())
			{
				torrent->ExpirePeerCandidates (ts);
				DrainPeerCandidates (torrent, ts);
				// Safety net: a verification pass that was never started (or
				// was interrupted by an add/remove cycle) must not stall
				// promotion forever. ScheduleLocalVerification() is a no-op
				// while a pass is already chained.
				if (!m_Stopping && torrent->IsLocalVerificationRequested ()) ScheduleLocalVerification (torrent);
				DiscoverAddedTorrent (torrent);
				if (ts > torrent->GetNextUpdateStatusTime ())
				{
					if (!torrent->IsStopped () && (torrent->IsActive () || !torrent->IsComplete ()))
					{
						if (torrent->UpdateStatus (ts))
						{
							if (!torrent->IsComplete ()) CompleteTorrent (torrent);
						}
						else
							UpdatePeersPerPiece (torrent);
					}
					boost::asio::post (GetDiskIOService (),
						[torrent, ts] ()
						{
							for (auto it : torrent->GetFiles ())
								if (ts > it->GetLastAccessTime () + TORRENT_FILE_INACTIVITY_TIMEOUT) it->Close ();
						});
					auto state = m_AdminState.find (torrent->GetInfoHash ());
					if (state != m_AdminState.end () && state->second.loaded
						&& (state->second.dirty
							|| state->second.commentRevision != torrent->GetCommentStore ().Revision ()))
						SaveTorrentAdminState (torrent);
					torrent->SetNextUpdateStatusTime (ts + TORRENTS_STATUS_UPDATE_INTERVAL
						+ GetLocalDestination ()->GetRng () () % TORRENTS_STATUS_UPDATE_INTERVAL_VARIANCE);
				}
			}
			ScheduleStatusUpdate ();
		}
	}

	void TorrentsTunnel::UpdatePeersPerPiece (std::shared_ptr<Torrent> torrent)
	{
		if (!torrent) return;
		torrent->StartCountingPeers ();
		IterateHandlers (
			[torrent] (std::shared_ptr<i2p::client::I2PServiceHandler> handler)
			{
				if (handler)
				{
					auto conn = std::static_pointer_cast<PeerConnection> (handler);
					if (conn->GetTorrent () == torrent) torrent->ApplyPeerRemoteBitfield (conn->GetRemoteBitfield ());
				}
			});
	}

	void TorrentsTunnel::HandleRecvFromI2PRaw (
		uint8_t protocol, uint16_t fromPort, uint16_t toPort, const uint8_t * buf, size_t len)
	{
		// This callback runs on the inbound datagram/garlic thread. DHT
		// routing, query and tracker state are service-thread owned, so copy
		// the bounded frame and marshal it instead of parsing or replying
		// here.
		constexpr size_t MAX_RAW_DATAGRAM_SIZE = 32768;
		if (!buf || !len || len > MAX_RAW_DATAGRAM_SIZE) return;
		std::vector<uint8_t> copy (buf, buf + len);
		std::weak_ptr<i2p::client::I2PService> weakSelf = weak_from_this ();
		boost::asio::post (GetService (),
			[weakSelf, protocol, fromPort, toPort, payload = std::move (copy)] () mutable
			{
				auto self = std::static_pointer_cast<TorrentsTunnel> (weakSelf.lock ());
				if (!self || self->m_Stopping.load (std::memory_order_acquire)) return;
				self->HandleRecvFromI2PRawOnService (protocol, fromPort, toPort, payload.data (), payload.size ());
			});
	}

	void TorrentsTunnel::HandleRecvFromI2PRawOnService (
		uint8_t /*protocol*/, uint16_t fromPort, uint16_t toPort, const uint8_t * buf, size_t len)
	{
		if (m_DHT && toPort == m_DHT->GetRPort ())
		{
			m_DHT->HandleRawDatagram (buf, len, fromPort, toPort);
			return;
		}
		// response from tracker
		if (len < 8) return;
		uint32_t action = bufbe32toh (buf);
		switch (action)
		{
		case eDatagramTrackerActionConnect: HandleConnectResponse (buf + 4, len - 4, fromPort, toPort); break;
		case eDatagramTrackerActionAnnounce: HandleAnnounceResponse (buf + 4, len - 4, fromPort, toPort); break;
		case eDatagramTrackerActionError: HandleErrorResponse (buf + 4, len - 4, fromPort, toPort); break;
		default: LogPrint (eLogInfo, "TorrentsTunnel: Unexpected action ", action, " from tracker");
		}
	}

	void TorrentsTunnel::HandleErrorResponse (const uint8_t * buf, size_t len, uint16_t /*fromPort*/, uint16_t toPort)
	{
		uint32_t transactionID = bufbe32toh (buf);
		LogPrint (eLogDebug, "TorrentsTunnel: Datagram tracker action error response ", transactionID);
		auto it = m_DatragramTrackerTransactions.find (transactionID);
		if (it == m_DatragramTrackerTransactions.end ())
		{
			LogPrint (eLogInfo, "TorrentsTunnel: Datagram tracker transaction ", transactionID, " not found");
			return;
		}
		std::string_view error ((const char *)(buf + 4), len - 4);
		auto [trackerID, storedPort, torrent, ts] = it->second;
		if (toPort != storedPort) return;
		// A remote error can contain credentials or other opaque payload.
		LogPrint (eLogInfo, "TorrentsTunnel: Datagram tracker error response");
		// Retain the announce owner once; expiry cannot grant lifetime.
		if (auto current = torrent.lock ()) current->ReportTrackerFailure (trackerID, error.substr (0, 512));
		m_DatragramTrackerTransactions.erase (it);
	}

	void TorrentsTunnel::ConnectToDatagramTracker (size_t trackerID, std::string_view dest, uint16_t port)
	{
		LogPrint (eLogDebug, "TorrentsTunnel: Connecting to datagram tracker ", dest, ":", port);
		auto address = i2p::client::context.GetAddressBook ().GetAddress (dest);
		if (address && address->IsIdentHash ())
		{
			auto localDestination = GetLocalDestination ();
			auto dgramDest = localDestination->GetDatagramDestination ();
			if (dgramDest)
			{
				uint8_t connectRequest[16];
				htobe64buf (connectRequest, 0x41727101980); // protocol_id
				htobe32buf (connectRequest + 8, eDatagramTrackerActionConnect); // action
				uint32_t transactionID = 0;
				for (int attempt = 0; attempt < 8; ++attempt)
				{
					transactionID = localDestination->GetRng () ();
					if (!m_DatragramTrackerTransactions.contains (transactionID)) break;
					transactionID = 0;
				}
				if (!transactionID)
				{
					LogPrint (eLogWarning, "TorrentsTunnel: Datagram tracker transaction id collision");
					return;
				}
				htobe32buf (connectRequest + 12, transactionID); // transactionID
				uint16_t fromPort = localDestination->GetRng () () % 1000 + 6000;
				if (m_DHT && fromPort == m_DHT->GetRPort ()) fromPort++;
				auto session = dgramDest->GetSession (address->identHash);
				if (session)
				{
					m_DatragramTrackerTransactions.emplace (transactionID,
						std::make_tuple (trackerID, fromPort, std::shared_ptr<Torrent> (nullptr),
							i2p::util::GetMonotonicMilliseconds ()));
					session->SetVersion (i2p::datagram::eDatagramV2); // send datagram2
					dgramDest->SendDatagram (session, connectRequest, 16, fromPort, port);
					dgramDest->FlushSendQueue (session);
				}
				else
					LogPrint (eLogInfo, "TorrentsTunnel: Can't obtain datagram session to ", dest);
			}
			else
				LogPrint (eLogError, "TorrentsTunnel: Datagram destination is not avaliable");
		}
		else
			LogPrint (eLogInfo, "TorrentsTunnel: Tracker not found: ", dest);
	}

	void TorrentsTunnel::HandleConnectResponse (const uint8_t * buf, size_t len, uint16_t /*fromPort*/, uint16_t toPort)
	{
		if (len < 12)
		{
			LogPrint (eLogInfo, "TorrentsTunnel: Unexpected connect response length ", len + 4);
			return;
		}
		uint32_t transactionID = bufbe32toh (buf);
		LogPrint (eLogDebug, "TorrentsTunnel: Datagram tracker action connect response ", transactionID);
		auto it = m_DatragramTrackerTransactions.find (transactionID);
		if (it == m_DatragramTrackerTransactions.end ())
		{
			LogPrint (eLogInfo, "TorrentsTunnel: Datagram tracker transaction ", transactionID, " not found");
			return;
		}
		uint64_t connectionID = bufbe64toh (buf + 4);
		int lifetime = DATAGRAM_TRACKER_CONNECTION_EXPIRATION;
		if (len >= 14)
		{
			lifetime = std::max (bufbe16toh (buf + 12) * 1000, DATAGRAM_TRACKER_CONNECTION_EXPIRATION);
			LogPrint (
				eLogDebug, "TorrentsTunnel: Datagram tracker connection lifetime set to ", lifetime / 1000, " seconds");
		}
		auto [trackerID, storedPort, torrent, ts] = it->second;
		if (toPort != storedPort) return;
		// The slot may have been released and reused for another announce URL
		// while the connect transaction was outstanding; never write a
		// connection id into a slot that no longer belongs to that tracker.
		if (trackerID < m_Trackers.size () && IsTrackerSlotLive (m_TrackerSlots, trackerID))
		{
			auto & [announce, common, connID, connExpiration, connFromPort] = m_Trackers[trackerID];
			connID = connectionID;
			connExpiration = i2p::util::GetMonotonicMilliseconds () + lifetime;
			connFromPort = storedPort;
		}
		m_DatragramTrackerTransactions.erase (it);
	}

	void TorrentsTunnel::SendAnnounceToDatagramTracker (size_t trackerID, uint64_t connectionID,
		std::shared_ptr<Torrent> torrent, std::string_view dest, uint16_t port, uint16_t fromPort,
		TrackerAnnounceEvent event)
	{
		if (!torrent || !torrent->HasPeerID ()) return;
		LogPrint (eLogDebug, "TorrentsTunnel: Sending announce to datagram tracker ", dest, ":", port);
		auto address = i2p::client::context.GetAddressBook ().GetAddress (dest);
		if (address && address->IsIdentHash ())
		{
			auto localDestination = GetLocalDestination ();
			auto dgramDest = localDestination->GetDatagramDestination ();
			if (dgramDest)
			{
				uint8_t announce[98];
				htobe64buf (announce, connectionID); // connection_id
				htobe32buf (announce + 8, eDatagramTrackerActionAnnounce); // action
				uint32_t transactionID = 0;
				for (int attempt = 0; attempt < 8; ++attempt)
				{
					transactionID = localDestination->GetRng () ();
					if (!m_DatragramTrackerTransactions.contains (transactionID)) break;
					transactionID = 0;
				}
				if (!transactionID)
				{
					LogPrint (eLogWarning, "TorrentsTunnel: Datagram tracker transaction id collision");
					return;
				}
				htobe32buf (announce + 12, transactionID); // transaction_id
				memcpy (announce + 16, torrent->GetInfoHash ().data (), 20); // info_hash
				memcpy (announce + 36, torrent->GetPeerID ().data (), 20); // per-torrent peer_id
				const auto progress = torrent->GetAnnounceProgress ();
				htobe64buf (announce + 56, torrent->GetLength () - torrent->GetFullLeft ()); // downloaded
				// Whole-torrent left, phony 1 without metadata, exactly like the
				// HTTP announce and Java's TrackerClient.
				htobe64buf (announce + 64, static_cast<uint64_t> (progress.left));
				htobe64buf (announce + 72, torrent->GetUploaded ()); // uploaded
				// A completed event is withheld until every piece is verified.
				if (event == eTrackerAnnounceEventCompleted && !progress.completed) event = eTrackerAnnounceEventNone;
				htobe32buf (announce + 80, event); // event
				htobe32buf (announce + 84, 0); // IP address 0:not used
				htobe32buf (announce + 88, 0); // key, ignored
				int numWant = 0;
				if (!torrent->IsComplete ()
					&& (event == eTrackerAnnounceEventNone || event == eTrackerAnnounceEventStarted))
					numWant = TRACKER_MAX_NUM_WANT;
				htobe32buf (announce + 92, numWant); // num_want
				htobe16buf (announce + 96, fromPort); // from port
				auto session = dgramDest->GetSession (address->identHash);
				if (session)
				{
					m_DatragramTrackerTransactions.emplace (transactionID,
						std::make_tuple (trackerID, fromPort, torrent, i2p::util::GetMonotonicMilliseconds ()));
					session->SetVersion (i2p::datagram::eDatagramV3); // send datagram3
					dgramDest->SendDatagram (session, announce, 98, fromPort, port);
					dgramDest->FlushSendQueue (session);
				}
				else
					LogPrint (eLogInfo, "TorrentsTunnel: Can't obtain datagram session");
			}
		}
		else
			LogPrint (eLogInfo, "TorrentsTunnel: Tracker not found: ", dest);
	}

	void TorrentsTunnel::HandleAnnounceResponse (
		const uint8_t * buf, size_t len, uint16_t /*fromPort*/, uint16_t toPort)
	{
		if (len < 16)
		{
			LogPrint (eLogInfo, "TorrentsTunnel: Unexpected announce response length ", len + 4);
			return;
		}
		uint32_t transactionID = bufbe32toh (buf);
		LogPrint (eLogDebug, "TorrentsTunnel: Datagram tracker action announce response ", transactionID);
		auto it = m_DatragramTrackerTransactions.find (transactionID);
		if (it == m_DatragramTrackerTransactions.end ())
		{
			LogPrint (eLogInfo, "TorrentsTunnel: Datagram tracker transaction ", transactionID, " not found");
			return;
		}
		auto [trackerID, storedPort, torrent, ts] = it->second;
		if (toPort != storedPort) return;
		if (auto active = torrent.lock (); active && !active->IsStopped () && IsTorrentTracker (trackerID, *active))
		{
			uint32_t interval = bufbe32toh (buf + 4);
			uint32_t leechers = bufbe32toh (buf + 8);
			uint32_t seeders = bufbe32toh (buf + 12);
			active->HandleDatagramTrackerResponse (trackerID, interval, buf + 16, len - 16, seeders, leechers);
		}
		m_DatragramTrackerTransactions.erase (it);
	}

	int TorrentsTunnel::AddTracker (std::string_view announce, bool isCommon)
	{
		const auto slot = ResolveTrackerSlot (announce, isCommon, true);
		if (!slot) return -1;
		AcquireTrackerSlot (*slot);
		return static_cast<int> (*slot);
	}

	void TorrentsTunnel::RegisterTorrentTrackers (const Torrent & torrent)
	{
		const auto infoHash = torrent.GetInfoHash ();
		// Admit the new tier list first: an entry that stays registered keeps
		// its index, so a torrent edit can never move live statistics to
		// another announce URL. Duplicated tier entries count once.
		std::set<size_t> wanted;
		const auto acquire = [this, &wanted] (std::string_view announce)
		{
			if (announce.empty ()) return;
			const auto slot = ResolveTrackerSlot (announce, false, true);
			if (slot && wanted.insert (*slot).second) AcquireTrackerSlot (*slot);
		};
		acquire (torrent.GetAnnounce ());
		for (const auto & tier : torrent.GetTrackerTiers ())
			for (const auto & announce : tier) acquire (announce);

		if (auto previous = m_TrackerMemberships.find (infoHash); previous != m_TrackerMemberships.end ())
		{
			// Release the old membership after the new one is counted: a
			// retained slot transiently holds two references and settles back to
			// one, while a dropped slot is freed only once nothing else uses it.
			for (auto trackerID : previous->second) ReleaseTrackerSlot (trackerID);
			m_TrackerMemberships.erase (previous);
		}
		if (!wanted.empty ())
			m_TrackerMemberships.emplace (infoHash, std::vector<size_t> (wanted.begin (), wanted.end ()));
	}

	void TorrentsTunnel::ReleaseTorrentTrackers (const Torrent::InfoHash & infoHash)
	{
		auto it = m_TrackerMemberships.find (infoHash);
		if (it == m_TrackerMemberships.end ()) return;
		for (auto trackerID : it->second) ReleaseTrackerSlot (trackerID);
		m_TrackerMemberships.erase (it);
	}

	void TorrentsTunnel::SendDHTPingQuery (const i2p::data::IdentHash & toIdent, uint16_t toPort)
	{
		boost::asio::post (GetService (),
			[this, toIdent, toPort] ()
			{
				if (m_DHT) m_DHT->SendPingQuery (toIdent, toPort);
			});
	}
}
}

#endif // NO_TORRENTS

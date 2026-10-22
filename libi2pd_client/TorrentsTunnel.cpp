/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: TorrentsTunnel.
 *
 * Starts and supervises the torrent engine: it loads torrents and resume or
 * administrative state from a confined directory, runs tracker and DHT
 * announce, reconnect and keep-alive timers, connects peers and marshals
 * control commands onto the service thread. Disk I/O and hashing run on a
 * separate worker and every path is checked to stay within the torrents
 * directory.
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
		std::string_view TorrentStatusName (TorrentStatus status)
		{
			switch (status)
			{
			case eTorrentStatusStopped: return "stopped";
			case eTorrentStatusQueuedToVerifyLocalData: return "queued-verify";
			case eTorrentStatusVerifyingLocalData: return "verifying";
			case eTorrentStatusQueuedToDownload: return "queued";
			case eTorrentStatusDownloading: return "downloading";
			case eTorrentStatusQueuedToSeed: return "queued-seed";
			case eTorrentStatusSeeding: return "seeding";
			default: return "unknown";
			}
		}

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
			for (const auto & it : hosts) m_Trackers.emplace_back (TrackerInfo { it.second, true, 0, 0, 0 });
		}
		if (dht)
			m_DHT = std::make_unique<TorrentsDHT> (*this,
				TORRENT_PORT + (localDestination ? localDestination->GetRng () () % (65535 - TORRENT_PORT - 1) : 1),
				dhtFileName);
	}

	TorrentsTunnel::~TorrentsTunnel ()
	{
		// Normally Stop() has already run on the service thread. The destructor
		// cannot call shared_from_this() to marshal another teardown, but it
		// must still join the disk worker: a joinable std::thread would
		// otherwise call std::terminate. Everything else is owned by the
		// service object and is released by the base destructor.
		if (!m_Stopping.load (std::memory_order_acquire))
		{
			LogPrint (eLogWarning, "TorrentsTunnel: destroyed without a completed Stop(); joining disk I/O only");
			m_DiskIOService.Stop ();
		}
	}

	void TorrentsTunnel::Start ()
	{
		m_Stopping.store (false, std::memory_order_release);
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
		if (m_Stopping.load (std::memory_order_acquire)) return;
		auto localDestination = GetLocalDestination ();
		if (!localDestination)
		{
			// No service is available to marshal to; the timers belong to the
			// (already released) destination, so only the owned worker and
			// handlers can be torn down.
			m_Stopping.store (true, std::memory_order_release);
			m_RawDatagramReceiver.Reset ();
			m_DiskIOService.Stop ();
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
						std::error_code ec;
						if (!self->m_TorrentsDir.empty () && std::filesystem::is_directory (self->m_TorrentsDir, ec))
						{
							std::filesystem::directory_iterator it (self->m_TorrentsDir, ec);
							std::filesystem::directory_iterator end;
							if (ec)
								LogPrint (eLogError, "TorrentsTunnel: Can't enumerate torrents directory ",
									self->m_TorrentsDir, " : ", ec.message ());
							while (!ec && it != end)
							{
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
										&& ReadFileBoundedNoFollow (
											self->m_TorrentsDir, relative, MAX_CONTROL_FILE_BYTES, content, error))
										loaded.push_back (std::move (content));
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

						if (self->m_Stopping.load (std::memory_order_acquire)) return;
						// Hand only bounded byte buffers to the network/control
						// service. Insertion and tracker scheduling stay there.
						boost::asio::post (self->GetService (),
							[self, loaded = std::move (loaded)] () mutable
							{
								if (self->m_Stopping.load (std::memory_order_acquire)) return;
								try
								{
									for (const auto & content : loaded)
										(void)self->AddTorrent (std::string_view (
											reinterpret_cast<const char *> (content.data ()), content.size ()));
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

	void TorrentsTunnel::SaveTorrentFile (std::shared_ptr<Torrent> torrent, const std::string & content)
	{
		if (!torrent || torrent->IsRemoved () || FindTorrent (torrent->GetInfoHash ()) != torrent) return;
		auto torrentFilePath = torrent->GetFullPath ();
		torrentFilePath += ".torrent";
		if (!IsContainedPath (torrentFilePath, false))
		{
			LogPrint (eLogError, "TorrentsTunnel: Refusing to save torrent file outside ", m_TorrentsDir);
			return;
		}
		auto relative = torrentFilePath.lexically_relative (m_TorrentsDir);
		std::string error;
		if (m_StorageWorker)
		{
			if (content.empty ()
				|| !m_StorageWorker->ReplaceMetadata (
					relative, { reinterpret_cast<const uint8_t *> (content.data ()), content.size () }, error))
				LogPrint (eLogError, "TorrentsTunnel: Isolated metainfo persistence failed: ", error);
			return;
		}
		if (content.empty () || relative.empty () || !IsSafeRelativePath (relative)
			|| !CreateDirectoriesWithinRootNoFollow (m_TorrentsDir, relative.parent_path ())
			|| !WriteFileAtomicNoFollow (m_TorrentsDir, relative, content.data (), content.size (), error))
			LogPrint (eLogError, "TorrentsTunnel: Can't persist ", torrentFilePath, ": ", error);
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
				continue;
			}
			if (!IsContainedPath (it->GetFullFilePath (), false))
			{
				LogPrint (eLogError, "TorrentsTunnel: Refusing to create file outside ", m_TorrentsDir);
				continue;
			}
			// Documents have no POSIX payload path and require no .part rename.
			if (documentStorage)
			{
				it->Complete ();
				presentFiles[currentFile] = true;
				continue;
			}
			std::error_code ec;
			bool finalExists = std::filesystem::exists (it->GetFullFilePath (), ec);
			if (ec)
			{
				LogPrint (eLogError, "TorrentsTunnel: Can't inspect ", it->GetFullFilePath (), " : ", ec.message ());
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
					LogPrint (eLogError, "TorrentsTunnel: Can't inspect ", partFilePath, " : ", ec.message ());
				else if (!partExists)
					CreateAndReserveFile (partFilePath, it->GetFileLength ());
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
		// Document backends do not preallocate or rename. Their presence is
		// proven by the same bounded verification chain used by POSIX, one
		// piece per disk job. Never trust a cached bitfield or block the disk
		// worker with a full rehash during initialization.
		if (documentStorage) std::fill (presentFiles.begin (), presentFiles.end (), true);
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
		auto & status = m_AdminState[hash];
		status = { };
		// Do not announce before the persisted pause/privacy policy is loaded.
		torrent->SetStopped (true);
		const auto relative = std::filesystem::path (".openi2pd-state") / (ToHexString (hash) + ".state");
		boost::asio::post (GetDiskIOService (),
			[self, torrent, relative, hash]
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
					[self, torrent, hash, bytes = std::move (bytes), error = std::move (error)] () mutable
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
						if (!torrent->IsStopped ())
							self->RequestTorrentTrackers (torrent, eTrackerAnnounceEventStarted);
					});
			});
	}

	void TorrentsTunnel::SaveTorrentAdminState (const std::shared_ptr<Torrent> & torrent, bool final)
	{
		if (!torrent || torrent->IsRemoved () || !torrent->IsValid ()) return;
		auto & status = m_AdminState[torrent->GetInfoHash ()];
		++status.generation;
		status.dirty = true;
		if (!status.loaded || (status.pending && !final)) return;
		TorrentLocalState state;
		state.stopped = torrent->IsStopped ();
		state.comments = torrent->GetCommentStore ();
		state.priorities = torrent->GetFilePriorities ();
		std::string error;
		auto content = EncodeTorrentLocalState (state, error);
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
		const auto relative = std::filesystem::path (".openi2pd-state") / (ToHexString (hash) + ".state");
		try
		{
			boost::asio::post (GetDiskIOService (),
				[self, torrent, hash, relative, generation, final, content = std::move (content)]
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
					boost::asio::post (self->GetService (),
						[self, torrent, hash, generation, ok, error = std::move (error)]
						{
							if (self->m_Stopping || self->FindTorrent (hash) != torrent) return;
							auto & status = self->m_AdminState[hash];
							status.pending = false;
							status.error
								= ok ? std::string { } : (error.empty () ? "cannot persist torrent settings" : error);
							if (!ok) status.dirty = true;
							// Coalesced local updates get another immutable write; failures
							// retry only on the bounded status timer, never a hot loop.
							if (ok && status.generation != generation) self->SaveTorrentAdminState (torrent);
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
			// capable parent. Failure never retries through direct filesystem IO.
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
		boost::asio::post (GetDiskIOService (),
			[this, torrent] ()
			{
				if (torrent->IsRemoved ()) return;
				// Never promote .part files based on a resume bitfield. Data
				// coming from the network is verified in HandlePieceMsg and
				// reloaded data is verified in the request path, so this gate
				// only delays promotion until selected pieces are verified.
				if (!torrent->AreAllSelectedPiecesVerified ())
				{
					LogPrint (eLogDebug, "TorrentsTunnel: Deferring completion of ", torrent->GetFullPath (),
						" until selected local pieces are verified");
					return;
				}
				bool completed = true;
				size_t fileIndex = 0;
				for (auto it : torrent->GetFiles ())
				{
					const size_t currentFile = fileIndex++;
					if (!torrent->GetFilePriority (currentFile)) continue;
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
					if (m_StorageWorker)
					{
						it->Close ();
						std::string error;
						if (m_StorageWorker->Promote (partFilePath.lexically_relative (m_TorrentsDir),
								it->GetFullFilePath ().lexically_relative (m_TorrentsDir), it->GetFileLength (), error))
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
					if (!IsContainedPath (partFilePath, false) || !IsContainedPath (it->GetFullFilePath (), false))
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
					else if (IsContainedPath (it->GetFullFilePath (), false)
						&& std::filesystem::exists (it->GetFullFilePath ()))
					{
						// The data was already under its final name (for example
						// rechecked after a restart). Every piece was hashed from
						// that file, so there is nothing to rename.
						it->Complete ();
					}
					else
					{
						LogPrint (eLogError, "TorrentsTunnel: ", partFilePath, " doesn't exist");
						it->Complete ();
					}
				}

				if (completed)
				{
					LogPrint (eLogInfo, "TorrentsTunnel: Download complete ", torrent->GetFullPath ());

					// SetComplete() mutates Torrent (m_IsComplete) and the
					// piece vector; run it on the service thread, which is the
					// owner of that state, not on the disk worker.
					boost::asio::post (GetService (),
						[this, torrent] ()
						{
							if (torrent->IsRemoved () || FindTorrent (torrent->GetInfoHash ()) != torrent) return;
							torrent->SetSelectedComplete ();
							// The resume bitfield remains for skipped/unwanted
							// ranges; SaveTorrentResumeFile removes it only when
							// every piece is complete.
							const auto hash = torrent->GetInfoHash ();
							boost::asio::post (GetDiskIOService (),
								[this, torrent, hash] ()
								{
									if (m_Stopping || FindTorrent (hash) != torrent) return;
									torrent->SaveTorrentResumeFile ();
								});
							// inform trackers that we are done
							RequestTorrentTrackers (torrent, eTrackerAnnounceEventCompleted);
							// close connections with seeds and reset stats for remaining
							auto conns = torrent->GetConnections ();
							for (auto it : conns)
							{
								if (it->GetRemoteBitfield ().all ()) // seed
									it->Close ();
								else
									it->ResetStats ();
							}
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
		const auto infoHash = torrent->GetInfoHash ();
		if (m_Stopping || FindTorrent (infoHash) != torrent)
		{
			torrent->EndLocalVerification ();
			return;
		}
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
				[this, torrent, hash] ()
				{
					if (m_Stopping || FindTorrent (hash) != torrent)
					{
						torrent->EndLocalVerification ();
						return;
					}
					torrent->SaveTorrentResumeFile ();
					bool complete = torrent->AreAllSelectedPiecesVerified ();
					boost::asio::post (GetService (),
						[this, torrent, hash, complete] ()
						{
							if (m_Stopping || FindTorrent (hash) != torrent || !complete) return;
							if (!torrent->IsComplete ()) CompleteTorrent (torrent);
						});
				});
			return;
		}
		// Exactly one piece per storage job: the pass is bounded in memory
		// (one piece buffer) and the service thread stays responsive between
		// pieces, so tracker/control/peer work is never blocked by a recheck.
		boost::asio::post (GetDiskIOService (),
			[this, torrent, hash = infoHash, index] ()
			{
				if (m_Stopping || FindTorrent (hash) != torrent)
				{
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
				boost::asio::post (GetService (),
					[this, torrent, hash, index, verified] ()
					{
						if (m_Stopping || FindTorrent (hash) != torrent) return;
						if (verified)
						{
							torrent->FinishPieceVerification (static_cast<size_t> (index));
							torrent->GetPiece (index).ReleaseData ();
						}
						else
							torrent->InvalidatePiece (static_cast<size_t> (index));
						VerifyNextLocalPiece (torrent);
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
			info.status = std::string (TorrentStatusName (torrent->GetStatus ()));
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
					|| !torrent->SetFilePriority (static_cast<size_t> (index), priority))
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
			// Only allocate a registry slot after the complete policy and bytes
			// are known to be valid, so a rejected edit cannot leak capacity.
			if (adding && AddTracker (command.argument, false) < 0)
			{
				restore ();
				return { false, "tracker limit reached", command.torrentId };
			}
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
		// An unparseable or incomplete metainfo must not enter the session: it
		// would otherwise be reported as added and persisted with no usable
		// files. Startup loading already rejects these with IsValid ().
		if (!torrent || torrent->GetError () != eTorrentErrorNoError || !torrent->IsValid ())
		{
			LogPrint (eLogWarning, "TorrentsTunnel: Rejecting invalid torrent file");
			return { nullptr, 0 };
		}
		// Only retain validated I2P tracker hints in persisted metainfo. The
		// info dictionary is unchanged, so sanitizing tiers preserves infohash.
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
				boost::asio::post (GetDiskIOService (),
					[this, torrent, content = torrent->CreateTorrentFileContent ()] ()
					{
						SaveTorrentFile (torrent, content);
						InitTorrentFiles (torrent);
					});
				if (!torrent->GetError ())
					boost::asio::post (GetService (),
						[this, torrent] { RequestTorrentTrackers (torrent, eTrackerAnnounceEventNone); });
				return { torrent, id };
			}
			// Lost an insertion race with a removal or another add.
			existing = FindTorrentByID (id);
			if (existing) return { existing, id };
			return { nullptr, 0 };
		}
		return { existing, FindTorrentID (existing) };
	}

	std::pair<std::shared_ptr<Torrent>, int> TorrentsTunnel::AddMagnet (std::string_view magnet)
	{
		if (m_Stopping) return { nullptr, 0 };
		auto parsed = ParseTorrentMagnet (magnet);
		if (!parsed) return { nullptr, 0 };
		if (auto existing = FindTorrent (parsed->infoHash)) return { existing, FindTorrentID (existing) };
		auto torrent = std::make_shared<Torrent> (parsed->infoHash);
		Torrent::TrackerTiers tiers;
		for (const auto & tracker : parsed->trackers) tiers.push_back ({ tracker });
		if (!tiers.empty () && !torrent->SetTrackerTiers (tiers)) return { nullptr, 0 };
		if (!parsed->trackers.empty ()) torrent->SetAnnounce (parsed->trackers.front ());
		torrent->SetName (parsed->name.empty () ? ToHexString (parsed->infoHash) : parsed->name);
		if (torrent->GetName ().empty ()) torrent->SetName (ToHexString (parsed->infoHash));
		auto [id, inserted] = InsertTorrent (torrent);
		if (inserted)
		{
			boost::asio::post (
				GetService (), [this, torrent] { RequestTorrentTrackers (torrent, eTrackerAnnounceEventNone); });
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
			if (!torrent->GetAnnounce ().empty ())
				torrent->SetAnnounceTrackerID (AddTracker (torrent->GetAnnounce (), false));
			for (const auto & tier : torrent->GetTrackerTiers ())
				for (const auto & tracker : tier) AddTracker (tracker, false);
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
		m_AdminState.erase (torrent->GetInfoHash ()); // service-thread state only
		StopTorrent (torrent);
		auto self = std::static_pointer_cast<TorrentsTunnel> (shared_from_this ());
		boost::asio::post (GetDiskIOService (),
			[this, self, torrent, deleteFiles] ()
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
					const auto state = std::filesystem::path (".openi2pd-state")
						/ (ToHexString (torrent->GetInfoHash ()) + ".state");
					if (!m_StorageWorker->RemoveMetadata (state, error))
					{
						cleanupFailed = true;
						LogPrint (eLogError, "TorrentsTunnel: Isolated settings removal failed: ", error);
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
				const auto state
					= std::filesystem::path (".openi2pd-state") / (ToHexString (torrent->GetInfoHash ()) + ".state");
				if (!RemoveFileNoFollow (m_TorrentsDir, state, metadataError))
				{
					cleanupFailed = true;
					LogPrint (eLogError, "TorrentsTunnel: Settings removal failed: ", metadataError);
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
		boost::asio::post (GetService (), [this, torrent] () { StopTorrent (torrent); });
		return true;
	}

	void TorrentsTunnel::StopTorrent (std::shared_ptr<Torrent> torrent)
	{
		if (!torrent) return;
		torrent->SetStopped (true);
		if (m_DHT) m_DHT->Unannounce (torrent->GetInfoHash ());
		torrent->UpdateStatus (i2p::util::GetMonotonicSeconds ());
		// Persist the current verified/complete set while the session still
		// owns its payload and before shutdown tears the disk queue down.
		if (torrent->IsValid ())
			boost::asio::post (GetDiskIOService (), [torrent] { torrent->SaveTorrentResumeFile (); });
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
		boost::asio::post (GetService (),
			[this, torrentsToStop = std::move (torrentsToStop)] ()
			{
				for (auto it : torrentsToStop) // TODO: announce with random intervals
					StopTorrent (it);
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
		boost::asio::post (GetService (),
			[this, torrent] ()
			{
				torrent->SetStopped (false);
				// inform trackers that we started
				RequestTorrentTrackers (torrent, eTrackerAnnounceEventStarted);
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
		boost::asio::post (GetService (),
			[this, torrentsToStart = std::move (torrentsToStart)] ()
			{
				for (auto it : torrentsToStart) // TODO: announce with random intervals
				{
					it->SetStopped (false);
					RequestTorrentTrackers (it, eTrackerAnnounceEventStarted);
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
		boost::asio::post (GetService (),
			[this, torrent] ()
			{
				RequestTorrentTrackers (torrent, eTrackerAnnounceEventNone);
				if (m_DHT && !torrent->IsPrivate ()) m_DHT->GetPeersAndAnnounce (torrent);
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
		boost::asio::post (GetService (),
			[this, torrentsToReannounce = std::move (torrentsToReannounce)] ()
			{
				for (auto it : torrentsToReannounce) // TODO: announce with random intervals
				{
					RequestTorrentTrackers (it, eTrackerAnnounceEventNone);
					if (m_DHT && !it->IsPrivate ()) m_DHT->GetPeersAndAnnounce (it);
				}
			});
	}

	void TorrentsTunnel::UpdateTorrentInfo (std::shared_ptr<Torrent> torrent, std::string_view info)
	{
		if (!torrent || torrent->IsRemoved () || torrent->IsStopped ()) return;
		torrent->ParseInfo (info);
		if (torrent->IsValid () && !torrent->GetError ()) LoadTorrentAdminState (torrent);
		if (torrent->IsPrivate ())
		{
			// Metadata may reveal a private swarm after trackerless discovery.
			// Drop all pre-metadata peers; resume only from authorized trackers.
			auto connections = torrent->GetConnections ();
			for (const auto & connection : connections) connection->Close ();
		}
		if (!SetTorrentPathWithinRoot (torrent))
		{
			torrent->SetError (eTorrentErrorMalformedMetaInfo);
			LogPrint (eLogError, "TorrentsTunnel: Magnet metadata has an unsafe name");
			return;
		}
		boost::asio::post (GetDiskIOService (),
			[this, torrent, content = torrent->CreateTorrentFileContent ()] ()
			{
				SaveTorrentFile (torrent, content);
				InitTorrentFiles (torrent);
			});
	}

	void TorrentsTunnel::Accept ()
	{
		auto localDestination = GetLocalDestination ();
		if (localDestination)
		{
			if (!localDestination->IsAcceptingStreams ()) // set it as default if not set yet
				localDestination->AcceptStreams (
					[this] (std::shared_ptr<i2p::stream::Stream> stream)
					{
						if (!stream) return;
						if (m_Stopping || CountPeerHandlers () >= MAX_TOTAL_PEERS)
						{
							// Bound inbound accepts before any handshake or
							// torrent lookup happens: unknown streams must not
							// accumulate handlers.
							LogPrint (eLogWarning, "TorrentsTunnel: Refusing inbound peer at the connection limit ",
								MAX_TOTAL_PEERS);
							stream->Close ();
							return;
						}
						auto conn = std::make_shared<PeerConnection> (shared_from_this (), stream);
						AddHandler (conn);
						conn->ReceiveHandshake ();
					});
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
		if (!torrent || (torrent->IsStopped () && event != eTrackerAnnounceEventStopped)) return;
		for (size_t i = 0; i < m_Trackers.size (); i++) RequestTracker (i, torrent, event);
	}

	bool TorrentsTunnel::IsTorrentTracker (size_t trackerID, const Torrent & torrent) const
	{
		if (trackerID >= m_Trackers.size ()) return false;
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
		if (!torrent || !IsTorrentTracker (trackerID, *torrent)) return false;
		i2p::http::URL reqURL = std::get<0> (m_Trackers[trackerID]);
		if (!IsI2PTrackerURL (reqURL))
		{
			LogPrint (
				eLogWarning, "TorrentsTunnel: Non-I2P address ", reqURL.host, " for torrent ", torrent->GetName ());
			return false;
		}
		if (reqURL.schema == "udp")
		{
			auto ts = i2p::util::GetMonotonicMilliseconds ();
			auto & [announce, common, connectionID, expiration, fromPort] = m_Trackers[trackerID];
			if (connectionID && ts <= expiration)
				SendAnnounceToDatagramTracker (
					trackerID, connectionID, torrent, reqURL.host, reqURL.port, fromPort, event);
			else
			{
				if (!expiration || ts >= expiration) // don't connect if pending connection
				{
					connectionID = 0;
					expiration = ts + DATAGRAM_TRACKER_TRANSACTION_TIMEOUT;
					ConnectToDatagramTracker (trackerID, reqURL.host, reqURL.port);
				}
			}
			return true;
		}
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
		params.emplace ("downloaded", std::to_string (torrent->GetLength () - torrent->GetFullLeft ()));
		params.emplace (
			"left", torrent->GetLength () ? std::to_string (torrent->GetLeft ()) : "1"); // phony 1 for magnet
		// Echo the opaque tracker id a previous response supplied (BEP 3).
		auto trackerIDValue = torrent->GetTrackerID (trackerID);
		if (!trackerIDValue.empty ()) params.emplace ("trackerid", PercentEncodeQueryValue (trackerIDValue));
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
		CreateStream (
			[this, req, torrent, trackerID, event] (std::shared_ptr<i2p::stream::Stream> stream)
			{
				if (stream && IsTorrentTracker (trackerID, *torrent)
					&& (!torrent->IsStopped () || event == eTrackerAnnounceEventStopped))
				{
					auto httpStream = std::make_shared<i2p::client::BoostAsyncStream> (stream);
					boost::beast::http::async_write (*httpStream, *req,
						std::bind (&TorrentsTunnel::TrackerRequestSent, this, std::placeholders::_1,
							std::placeholders::_2, httpStream, torrent, req, trackerID));
				}
			},
			reqURL.host, reqURL.port);
		return true;
	}

	void TorrentsTunnel::TrackerRequestSent (const boost::beast::error_code & ecode, size_t bytes_transferred,
		std::shared_ptr<i2p::client::BoostAsyncStream> httpStream, std::shared_ptr<Torrent> torrent,
		std::shared_ptr<boost::beast::http::request<boost::beast::http::string_body>> req, size_t trackerID)
	{
		if (ecode)
		{
			LogPrint (eLogWarning, "TorrentsTunnel: Tracker ", trackerID, " request failed: ", ecode.message ());
			if (!torrent->IsStopped ()) torrent->ReportTrackerFailure (trackerID, "request failed");
			httpStream->GetStream ()->AsyncClose ();
		}
		else
		{
			// Receive. A parser (rather than a bare response) enforces a body
			// limit, so a hostile tracker cannot make the client buffer an
			// unbounded document before it is parsed.
			auto buf = std::make_shared<boost::beast::flat_buffer> ();
			auto parser = std::make_shared<boost::beast::http::response_parser<boost::beast::http::string_body>> ();
			parser->body_limit (MAX_TRACKER_RESPONSE_BODY);
			boost::beast::http::async_read (*httpStream, *buf, *parser,
				[this, httpStream, torrent, buf, parser, trackerID] (
					const boost::beast::error_code & ecode, size_t bytes_transferred)
				{
					httpStream->GetStream ()->AsyncClose ();
					if (ecode)
					{
						LogPrint (eLogWarning, "TorrentsTunnel: Tracker ", trackerID,
							" response rejected: ", ecode.message ());
						if (!torrent->IsStopped ()) torrent->ReportTrackerFailure (trackerID, "response rejected");
						return;
					}
					if (!torrent->IsStopped () && IsTorrentTracker (trackerID, *torrent))
					{
						const auto & res = parser->get ();
						if (res.result () == boost::beast::http::status::ok)
						{
							torrent->ParseTrackerResponse (trackerID, res.body ());
							ConnectToPeers (torrent, trackerID);
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
		const uint64_t dial = m_NextDialID++;
		m_PendingDials.emplace (dial, PendingDial { torrent, i2p::util::GetMonotonicSeconds () });
		CreateStream (
			[this, torrent, peer, origin, dial] (std::shared_ptr<i2p::stream::Stream> stream)
			{
				FinishDial (dial);
				if (stream && !torrent->IsStopped ()
					&& (!torrent->IsPrivate () || origin == ePeerConnectionOriginTracker))
				{
					LogPrint (eLogDebug, "TorrentsTunnel: Connected to peer ", peer.ToBase32 () + ".b32.i2p");
					auto connection = std::make_shared<PeerConnection> (shared_from_this (), stream, torrent, origin);
					AddHandler (connection);
					connection->Connect ();
				}
				else
					LogPrint (eLogInfo, "TorrentsTunnel: Can't connect to peer ", peer.ToBase32 () + ".b32.i2p");
			},
			std::make_shared<i2p::client::Address> (peer), TORRENT_PORT);
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
		if (!torrent || torrent->IsComplete ()) return;
		if (!newPeers.empty ())
		{
			for (const auto & it : newPeers)
				if (!torrent->IsConnectedToPeer (it)) ConnectToPeer (torrent, it, origin);
		}
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
				if (!torrent->IsStopped ())
				{
					for (size_t i = 0; i < m_Trackers.size (); i++)
					{
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
								auto nextInterval = torrent->GetInterval (i)
									+ GetLocalDestination ()->GetRng () () % TRACKER_REQUESTS_INTERVAL_VARIANCE;
								torrent->SetNextTrackerRequestTime (i, ts + nextInterval);
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
				// Safety net: a verification pass that was never started (or
				// was interrupted by an add/remove cycle) must not stall
				// promotion forever. ScheduleLocalVerification() is a no-op
				// while a pass is already chained.
				if (!m_Stopping && torrent->IsLocalVerificationRequested ()) ScheduleLocalVerification (torrent);
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
		LogPrint (eLogInfo, "TorrentsTunnel: Datagram tracker error response: ", error);
		auto [trackerID, storedPort, torrent, ts] = it->second;
		if (toPort != storedPort) return;
		if (!torrent.expired ()) // response to announce
			torrent.lock ()->ReportTrackerFailure (trackerID, error.substr (0, 512));
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
		if (trackerID < m_Trackers.size ())
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
				auto left = torrent->GetLeft ();
				htobe64buf (announce + 56, torrent->GetLength () - torrent->GetFullLeft ()); // downloaded
				htobe64buf (announce + 64, torrent->GetLength () ? left : 1); // left, phony 1 for magnet
				htobe64buf (announce + 72, torrent->GetUploaded ()); // uploaded
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
		if (announce.empty ()
			|| std::any_of (announce.begin (), announce.end (), [] (unsigned char c) { return c <= 32 || c == 127; }))
			return -1;
		if (announce.size () > MAX_TRACKER_ANNOUNCE_LENGTH)
		{
			LogPrint (eLogWarning, "TorrentsTunnel: Ignoring over-long tracker announce");
			return -1;
		}
		i2p::http::URL reqURL (announce);
		if (!IsI2PTrackerURL (reqURL))
		{
			LogPrint (eLogInfo, "TorrentsTunnel: Non-I2P address ", reqURL.host, " in tracker announce");
			return -1;
		}
		auto it = std::find_if (m_Trackers.begin (), m_Trackers.end (),
			[&reqURL] (const TrackerInfo & tracker)
			{
				// Paths and schemes identify distinct tracker endpoints even on
				// the same I2P destination; do not merge private announce URLs.
				return std::get<0> (tracker).to_string () == reqURL.to_string ();
			});
		if (it != m_Trackers.end ()) // existing
			return it - m_Trackers.begin ();
		if (m_Trackers.size () >= MAX_NUM_TRACKERS)
		{
			LogPrint (eLogWarning, "TorrentsTunnel: Ignoring tracker above the limit ", MAX_NUM_TRACKERS);
			return -1;
		}

		m_Trackers.emplace_back (TrackerInfo { announce, isCommon, 0, 0, 0 });
		return m_Trackers.size () - 1;
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

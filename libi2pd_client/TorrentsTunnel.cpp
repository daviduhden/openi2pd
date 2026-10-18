/*
* Copyright (c) 2026, The PurpleI2P Project
*
* This file is part of Open I2Pd and licensed under BSD3
*
* See full license text in LICENSE file at top of project tree
*/

#ifndef NO_TORRENTS

#include <fstream>
#include <utility>
#include <future>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <exception>
#include <limits>
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

namespace i2p
{
namespace torrents
{
	namespace
	{
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

		std::string PeerFlags (const PeerConnection& peer)
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

		std::string PeerClientName (const PeerConnection::PeerID& peerID)
		{
			if (peerID.size () >= 6 && !memcmp (peerID.data (), "-I2PD-", 6))
				return "i2pd";
			std::string name = "Unknown (";
			for (auto ch : peerID)
				name.push_back (std::isprint (static_cast<unsigned char> (ch)) ? ch : '?');
			name.push_back (')');
			return name;
		}

		int ClampToInt (size_t value)
		{
			return static_cast<int> (std::min (value, static_cast<size_t> (std::numeric_limits<int>::max ())));
		}
	}

	TorrentsTunnel::TorrentsTunnel (std::string_view name, std::shared_ptr<i2p::client::ClientDestination> localDestination,
		std::string_view torrentsDir, std::string_view trackers, bool dht, std::string_view dhtFileName):
		i2p::client::I2PService (localDestination), m_Name (name), m_PeerID ("-I2PD-"),
		m_TorrentsDir (torrentsDir), m_TrackerRequestsCheckTimer (GetService ()),
		m_KeepAliveCheckTimer (GetService ()), m_ReconnectCheckTimer (GetService ()),
		m_TorrentsStatusUpdateTimer (GetService ())
	{
		if (localDestination)
			m_PeerID += localDestination->GetIdentHash ().ToBase64 ();
		m_PeerID.resize (20, '0');
		if (!trackers.empty ())
		{
			// parse param
			std::vector<std::string> trackersList;
			boost::split(trackersList, trackers, boost::is_any_of(","), boost::token_compress_on);
			// exclude duplicates
			std::unordered_map<i2p::data::IdentHash, i2p::http::URL> hosts;
			for (const auto& it: trackersList)
			{
				i2p::http::URL url (it);
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
			for (const auto& it: hosts)
				m_Trackers.emplace_back (TrackerInfo{ it.second, true, 0, 0, 0 });
		}
		if (dht)
			m_DHT = std::make_unique<TorrentsDHT>(*this, TORRENT_PORT + (localDestination ? localDestination->GetRng()() % (65535 - TORRENT_PORT - 1) : 1), dhtFileName);
	}

	void TorrentsTunnel::Start ()
	{
		i2p::client::I2PService::Start ();
		m_DiskIOService.Start ();
		auto dgramDest = GetLocalDestination ()->CreateDatagramDestination (true, i2p::datagram::eDatagramV1); // V1 for DHT
		if (dgramDest)
			dgramDest->SetRawReceiver (std::bind (&TorrentsTunnel::HandleRecvFromI2PRaw,
				std::static_pointer_cast<TorrentsTunnel>(shared_from_this ()),
				std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4));
		if (m_DHT) m_DHT->Start ();

		Accept ();

		if (!m_TorrentsDir.empty() && std::filesystem::exists (m_TorrentsDir) &&
			std::filesystem::is_directory (m_TorrentsDir))
		{
			for (const auto& it: std::filesystem::directory_iterator (m_TorrentsDir))
				if (std::filesystem::is_regular_file (it.status()) && it.path ().extension () == ".torrent")
					ReadTorrentFile (it.path ());
        }

		ScheduleTrackerRequestsCheck ();
		ScheduleReconnectCheck ();
		ScheduleKeepAliveCheck ();
		ScheduleStatusUpdate ();
	}

	void TorrentsTunnel::Stop ()
	{
		if (m_DHT) m_DHT->Stop ();
		auto localDestination = GetLocalDestination ();
		if (localDestination)
		{
			localDestination->StopAcceptingStreams ();
			auto dgramDest = localDestination->GetDatagramDestination ();
			if (dgramDest)
				dgramDest->ResetRawReceiver ();
		}
		m_TrackerRequestsCheckTimer.cancel ();
		m_KeepAliveCheckTimer.cancel ();
		m_ReconnectCheckTimer.cancel ();
		m_TorrentsStatusUpdateTimer.cancel ();
		for (auto it: m_Torrents)
			StopTorrent (it.second);
		m_Torrents.clear ();
		m_DiskIOService.Stop ();
		i2p::client::I2PService::ClearHandlers (); // close connections
		i2p::client::I2PService::Stop ();
	}

	void TorrentsTunnel::ReadTorrentFile (const std::filesystem::path& torrentFilePath)
	{
		std::vector<uint8_t> content;
		std::string error;
		if (!ReadTorrentFileBounded (torrentFilePath, MAX_CONTROL_FILE_BYTES, content, error))
		{
			LogPrint (eLogError, "TorrentsTunnel: Can't read ", torrentFilePath, ": ", error);
			return;
		}
		auto torrent = std::make_shared<Torrent> (std::string_view (
			reinterpret_cast<const char *> (content.data ()), content.size ()));
		if (!torrent->IsValid () || torrent->GetError () != eTorrentErrorNoError)
		{
			LogPrint (eLogError, "TorrentsTunnel: Invalid torrent file ", torrentFilePath, ". Skipped");
			return;
		}
		torrent->SetFullPath (m_TorrentsDir/std::filesystem::path (torrent->GetName ()));
		InsertTorrent (torrent);
		InitTorrentFiles (torrent);
	}

	bool TorrentsTunnel::IsContainedPath (const std::filesystem::path& path, bool allowDirectoryLeaf) const
	{
		if (path.empty () || m_TorrentsDir.empty ()) return false;

		std::error_code ec;
		auto root = std::filesystem::absolute (m_TorrentsDir, ec).lexically_normal ();
		if (ec || root.empty ()) return false;
		auto target = std::filesystem::absolute (path, ec).lexically_normal ();
		if (ec || target.empty ()) return false;

		auto rootStatus = std::filesystem::symlink_status (root, ec);
		if (ec)
		{
			if (ec != std::errc::no_such_file_or_directory) return false;
			ec.clear ();
		}
		else if (std::filesystem::is_symlink (rootStatus))
			return false;

		auto rootIt = root.begin ();
		auto targetIt = target.begin ();
		for (; rootIt != root.end (); ++rootIt, ++targetIt)
		{
			if (targetIt == target.end () || *rootIt != *targetIt)
				return false;
		}
		if (targetIt == target.end ())
			return false; // the torrents directory itself is not a file target

		auto current = root;
		while (targetIt != target.end ())
		{
			current /= *targetIt;
			auto nextIt = targetIt;
			++nextIt;
			bool isLeaf = nextIt == target.end ();

			auto status = std::filesystem::symlink_status (current, ec);
			if (ec)
			{
				if (ec != std::errc::no_such_file_or_directory) return false;
				ec.clear ();
				// A missing component will be created by us. Its parent has
				// already been checked, and the path is checked again before
				// any file is opened.
			}
			else
			{
				if (std::filesystem::is_symlink (status)) return false;
				if (isLeaf)
				{
					if (!std::filesystem::is_regular_file (status) &&
						!(allowDirectoryLeaf && std::filesystem::is_directory (status)))
						return false;
				}
				else if (!std::filesystem::is_directory (status))
					return false;
			}
			targetIt = nextIt;
		}
		return true;
	}

	std::string TorrentsTunnel::GetDisplayFilePath (const std::filesystem::path& path) const
	{
		if (path.empty ()) return "";
		std::error_code ec;
		auto relative = std::filesystem::relative (path, m_TorrentsDir, ec);
		if (!ec && !relative.empty () && !relative.is_absolute () && relative != "." &&
			*relative.begin () != "..")
			return relative.generic_string ();
		return path.filename ().string ();
	}

	void TorrentsTunnel::SaveTorrentFile (std::shared_ptr<Torrent> torrent)
	{
		if (!torrent) return;
		auto torrentFilePath = torrent->GetFullPath (); torrentFilePath += ".torrent";
		if (!IsContainedPath (torrentFilePath, false))
		{
			LogPrint (eLogError, "TorrentsTunnel: Refusing to save torrent file outside ", m_TorrentsDir);
			return;
		}
		std::ofstream f(torrentFilePath, std::ofstream::binary);
		if (f)
		{
			auto content = torrent->CreateTorrentFileContent ();
			f.write (content.data (), content.size ());
		}
		else
			LogPrint (eLogError, "TorrentsTunnel: Can't open ", torrentFilePath);
	}

	void TorrentsTunnel::InitTorrentFiles (std::shared_ptr<Torrent> torrent)
	{
		if (!torrent || torrent->GetError () == eTorrentErrorMalformedMetaInfo) return;
		if (!IsContainedPath (torrent->GetFullPath (), true))
		{
			LogPrint (eLogError, "TorrentsTunnel: Refusing to initialise torrent outside ", m_TorrentsDir);
			return;
		}

		bool completed = true;
		for (auto it: torrent->GetFiles ())
		{
			if (torrent->IsSingleFile ())
				it->SetFullPath (torrent->GetFullPath ());
			else
				it->UpdateFullPath (torrent->GetFullPath ());
			if (!IsContainedPath (it->GetFullFilePath (), false))
			{
				LogPrint (eLogError, "TorrentsTunnel: Refusing to create file outside ", m_TorrentsDir);
				completed = false;
				continue;
			}
			if (!std::filesystem::exists (it->GetFullFilePath ()))
			{
				auto partFilePath = it->GetFullFilePath (); partFilePath += ".part";
				if (!std::filesystem::exists (partFilePath))
					CreateAndReserveFile (partFilePath, it->GetFileLength ());
				completed = false;
			}
		}
		if (completed) torrent->SetComplete ();

		auto resumeFilePath = torrent->GetFullPath (); resumeFilePath += ".resume";
		if (!IsContainedPath (resumeFilePath, false))
		{
			LogPrint (eLogError, "TorrentsTunnel: Refusing to access resume file outside ", m_TorrentsDir);
			return;
		}
		if (std::filesystem::exists (resumeFilePath))
		{
			if (!torrent->IsComplete ())
			{
				std::ifstream rs(resumeFilePath, std::ifstream::binary);
				if (rs)
				{
					rs.seekg (0,std::ios::end);
					size_t l = rs.tellg ();
					if (l > 0)
					{
						rs.seekg(0, std::ios::beg);
						std::vector<uint8_t> bitfield(l);
						rs.read((char *)bitfield.data (), l);
						if (torrent->ApplyBitfield (bitfield))
							CompleteTorrent (torrent);
					}
				}
			}
			else
				std::filesystem::remove (resumeFilePath);
		}
	}

	bool TorrentsTunnel::CreateAndReserveFile (const std::filesystem::path& filePath, size_t reserve)
	{
		if (!IsContainedPath (filePath, false))
		{
			LogPrint (eLogError, "TorrentsTunnel: Refusing to create file outside ", m_TorrentsDir);
			return false;
		}
		if (std::filesystem::exists (filePath)) return false;
		auto subdirs = filePath.parent_path ();
		if (!subdirs.empty ())
		{
			// try to create all subdirs
			try
			{
				std::filesystem::create_directories (subdirs);
			}
			catch (std::exception& ex)
			{
				LogPrint (eLogError, "TorrentsTunnel: Can't create subdirs ", subdirs, " : ", ex.what());
				return false;
			}
			// Directory creation may have raced with a symlink. Never continue
			// if the path now resolves through one.
			if (!IsContainedPath (filePath, false))
			{
				LogPrint (eLogError, "TorrentsTunnel: Refusing to create file through a changed path ", filePath);
				return false;
			}
		}
		// create file
		std::ofstream f(filePath, std::ios::binary);
		if (!f) return false;
		f.close ();
		if (!IsContainedPath (filePath, false))
		{
			std::error_code ec;
			std::filesystem::remove (filePath, ec);
			LogPrint (eLogError, "TorrentsTunnel: Refusing to use changed file path ", filePath);
			return false;
		}
		if (reserve > 0)
		{
			// resize
			try
			{
				std::filesystem::resize_file (filePath, reserve);
			}
			catch (std::exception& ex)
			{
				LogPrint (eLogError, "TorrentsTunnel: Can't resize file ", filePath, " to ", reserve, " : ", ex.what());
				return false;
			}
		}
		return true;
	}

	void TorrentsTunnel::CompleteTorrent (std::shared_ptr<Torrent> torrent)
	{
		boost::asio::post (GetDiskIOService (), [this, torrent]()
		{
			bool completed = true;
			for (auto it: torrent->GetFiles ())
			{
				auto partFilePath = it->GetFullFilePath (); partFilePath += ".part";
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
				else
				{
					LogPrint (eLogError, "TorrentsTunnel: ", partFilePath, " doesn't exist");
					it->Complete ();
				}
			}

			if (completed)
			{
				torrent->SetComplete ();
				LogPrint (eLogInfo, "TorrentsTunnel: Download complete ", torrent->GetFullPath ());

				boost::asio::post (GetService (), [this, torrent]()
					{
						// inform trackers that we are done
						RequestTorrentTrackers (torrent, eTrackerAnnounceEventCompleted);
						// close connections with seeds and reset stats for remaining
						auto conns = torrent->GetConnections ();
						for (auto it: conns)
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

	std::shared_ptr<Torrent> TorrentsTunnel::FindTorrent (const Torrent::InfoHash& infoHash) const
	{
		std::lock_guard<std::mutex> l(m_TorrentsMutex);
		auto it = m_Torrents.find (infoHash);
		if (it != m_Torrents.end ())
			return it->second;
		return nullptr;
	}

	std::shared_ptr<Torrent> TorrentsTunnel::FindTorrentByID (int id) const
	{
		std::lock_guard<std::mutex> l(m_TorrentsMutex);
		auto it = m_TorrentsByID.find (id);
		if (it != m_TorrentsByID.end ())
			return it->second.lock ();
		return nullptr;
	}

	std::vector<int> TorrentsTunnel::GetTorrentIDs () const
	{
		std::vector<int> ids;
		std::lock_guard<std::mutex> l(m_TorrentsMutex);
		for (const auto& it: m_TorrentsByID)
			if (!it.second.expired ()) ids.push_back (it.first);
		return ids;
	}

	std::list<std::shared_ptr<Torrent> > TorrentsTunnel:: GetTorrents () const
	{
		std::list<std::shared_ptr<Torrent> > torrents;
		std::lock_guard<std::mutex> l(m_TorrentsMutex);
		for (const auto& it: m_Torrents)
			torrents.push_back (it.second);
		return torrents;
	}

	std::optional<TorrentsSnapshot> TorrentsTunnel::GetControlSnapshot (std::chrono::milliseconds timeout)
	{
		std::shared_ptr<TorrentsTunnel> self;
		try
		{
			self = std::static_pointer_cast<TorrentsTunnel> (shared_from_this ());
		}
		catch (const std::exception&)
		{
			return std::nullopt;
		}

		auto promise = std::make_shared<std::promise<std::optional<TorrentsSnapshot> > > ();
		auto future = promise->get_future ();
		try
		{
			boost::asio::post (GetService (), [self, promise]()
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
		catch (const std::exception&)
		{
			return std::nullopt;
		}

		if (future.wait_for (timeout) != std::future_status::ready)
			return std::nullopt;
		try
		{
			return future.get ();
		}
		catch (const std::exception&)
		{
			return std::nullopt;
		}
	}

	std::optional<TorrentsSnapshot> TorrentsTunnel::BuildControlSnapshot () const
	{
		TorrentsSnapshot snapshot;
		snapshot.tunnelName = m_Name;
		snapshot.dht.supported = SupportsDHT ();
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

			TorrentInfo info{};
			info.id = id;
			info.name = std::string (torrent->GetName ());
			info.status = std::string (TorrentStatusName (torrent->GetStatus ()));
			info.error = std::string (torrent->GetErrorStr ());
			info.hash = ToHexString (torrent->GetInfoHash ());
			info.magnet = BuildTorrentMagnet (torrent->GetInfoHash (), info.name);

			auto total = torrent->GetLength ();
			auto left = torrent->GetLeft ();
			auto have = total > left ? total - left : 0;
			info.totalSize = total;
			info.have = have;
			info.downloaded = torrent->GetDownloaded ();
			info.uploaded = torrent->GetUploaded ();
			info.downRate = torrent->GetDownloadRate ();
			info.upRate = torrent->GetUploadRate ();
			info.peers = ClampToInt (torrent->GetConnections ().size ());
			info.percent = total ? std::min (100.0, 100.0 * static_cast<double> (have) / static_cast<double> (total)) : 0.0;
			if (!torrent->IsComplete () && left && info.downRate)
				info.etaSeconds = (left + info.downRate - 1) / info.downRate;

			int seeders = 0, leechers = 0;
			for (size_t trackerID = 0; trackerID < m_Trackers.size (); trackerID++)
			{
				seeders = std::max (seeders, torrent->GetNumSeeders (trackerID));
				leechers = std::max (leechers, torrent->GetNumLeechers (trackerID));
			}
			info.seeders = seeders;
			info.leechers = leechers;

			auto filesCompleted = torrent->GetFilesCompleted ();
			size_t fileIndex = 0;
			for (const auto& file : torrent->GetFiles ())
			{
				if (info.files.size () >= MAX_CONTROL_FILES) break;
				TorrentFileInfo fileInfo{};
				fileInfo.name = GetDisplayFilePath (file->GetFullFilePath ());
				fileInfo.length = file->GetFileLength ();
				fileInfo.completed = fileIndex < filesCompleted.size () ? filesCompleted[fileIndex] : 0;
				info.files.push_back (std::move (fileInfo));
				fileIndex++;
			}

			for (const auto& connection : torrent->GetConnections ())
			{
				if (info.peerDetails.size () >= MAX_CONTROL_PEERS) break;
				if (!connection || !connection->IsEstablished ()) continue;

				TorrentPeerInfo peer{};
				auto ident = connection->GetRemoteIdentHash ();
				if (ident) peer.ident = ident->ToBase64 ();
				peer.client = connection->GetRemoteName ().empty () ?
					PeerClientName (connection->GetRemotePeerID ()) : std::string (connection->GetRemoteName ());
				peer.flags = PeerFlags (*connection);
				const auto& bitfield = connection->GetRemoteBitfield ();
				peer.progress = bitfield.size () ? static_cast<double> (bitfield.count ()) / static_cast<double> (bitfield.size ()) : 0.0;
				peer.downRate = connection->GetDownloadRate ();
				peer.upRate = connection->GetUploadRate ();
				info.peerDetails.push_back (std::move (peer));
			}

			for (size_t trackerID = 0; trackerID < m_Trackers.size (); trackerID++)
			{
				if (info.trackers.size () >= MAX_CONTROL_TRACKERS) break;
				if (!torrent->GetNextTrackerRequestTime (trackerID)) continue;

				TorrentTrackerInfo tracker{};
				tracker.announce = GetTrackerAnnounce (trackerID);
				tracker.peers = torrent->GetNumPeers (trackerID);
				tracker.seeders = torrent->GetNumSeeders (trackerID);
				tracker.leechers = torrent->GetNumLeechers (trackerID);
				auto error = torrent->GetTrackerError (trackerID);
				tracker.lastResult = error.empty () ? "Success" : error;
				tracker.lastAnnounce = torrent->GetLastTrackerUpdateTime (trackerID);
				auto nextRequestTime = torrent->GetNextTrackerRequestTime (trackerID);
				tracker.nextAnnounce = (nextRequestTime > nowMonotonic ?
					nowEpoch + (nextRequestTime - nowMonotonic) : nowEpoch) / 1000;
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

	TorrentCommandResult TorrentsTunnel::ExecuteControlCommand (const TorrentCommand& command,
		std::chrono::milliseconds timeout)
	{
		if ((command.kind == TorrentCommandKind::AddMagnet || command.kind == TorrentCommandKind::AddFile) &&
			(command.argument.empty () || command.argument.size () > MAX_CONTROL_ARGUMENT))
			return { false, "invalid command argument", command.torrentId };

		std::shared_ptr<TorrentsTunnel> self;
		try
		{
			self = std::static_pointer_cast<TorrentsTunnel> (shared_from_this ());
		}
		catch (const std::exception&)
		{
			return { false, "torrent tunnel is not available", command.torrentId };
		}

		auto promise = std::make_shared<std::promise<TorrentCommandResult> > ();
		auto future = promise->get_future ();
		try
		{
			boost::asio::post (GetService (), [self, promise, command]()
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
		catch (const std::exception&)
		{
			return { false, "can't queue command", command.torrentId };
		}

		if (future.wait_for (timeout) != std::future_status::ready)
			return { false, "command timed out", command.torrentId };
		try
		{
			return future.get ();
		}
		catch (const std::exception&)
		{
			return { false, "command failed", command.torrentId };
		}
	}

	TorrentCommandResult TorrentsTunnel::ExecuteControlCommandOnService (const TorrentCommand& command)
	{
		switch (command.kind)
		{
			case TorrentCommandKind::Start:
				return StartTorrent (command.torrentId) ?
					TorrentCommandResult{ true, "start queued", command.torrentId } :
					TorrentCommandResult{ false, "torrent not found", command.torrentId };
			case TorrentCommandKind::Stop:
				return StopTorrent (command.torrentId) ?
					TorrentCommandResult{ true, "stop queued", command.torrentId } :
					TorrentCommandResult{ false, "torrent not found", command.torrentId };
			case TorrentCommandKind::Reannounce:
				return ReannounceTorrent (command.torrentId) ?
					TorrentCommandResult{ true, "reannounce queued", command.torrentId } :
					TorrentCommandResult{ false, "torrent not found", command.torrentId };
			case TorrentCommandKind::Remove:
				return RemoveTorrent (command.torrentId, command.deleteFiles) ?
					TorrentCommandResult{ true, command.deleteFiles ? "remove with files queued" : "remove queued", command.torrentId } :
					TorrentCommandResult{ false, "torrent not found", command.torrentId };
			case TorrentCommandKind::RemoveAll:
				RemoveAllTorrents (command.deleteFiles);
				return { true, command.deleteFiles ? "remove all with files queued" : "remove all queued", 0 };
			case TorrentCommandKind::StartAll:
				StartAllTorrents ();
				return { true, "start all queued", 0 };
			case TorrentCommandKind::StopAll:
				StopAllTorrents ();
				return { true, "stop all queued", 0 };
			case TorrentCommandKind::ReannounceAll:
				ReannounceAllTorrents ();
				return { true, "reannounce all queued", 0 };
			case TorrentCommandKind::AddMagnet:
			{
				auto [torrent, id] = AddMagnet (command.argument);
				if (!torrent || !id)
					return { false, "invalid or duplicate magnet", 0 };
				return { true, "torrent added", id };
			}
			case TorrentCommandKind::AddFile:
			{
				std::vector<uint8_t> content;
				std::string error;
				if (!ReadTorrentFileBounded (command.argument, MAX_CONTROL_FILE_BYTES, content, error))
					return { false, error, 0 };
				auto [torrent, id] = AddTorrent (std::string_view (
					reinterpret_cast<const char *> (content.data ()), content.size ()));
				if (!torrent || !id)
					return { false, "invalid torrent file", 0 };
				return { true, "torrent added", id };
			}
			default:
				return { false, "unsupported command", command.torrentId };
		}
	}

	template<typename F>
	static std::pair<std::shared_ptr<Torrent>, int> RunAddOnService (
		const std::shared_ptr<TorrentsTunnel>& self, F&& operation)
	{
		auto promise = std::make_shared<std::promise<std::pair<std::shared_ptr<Torrent>, int> > > ();
		auto future = promise->get_future ();
		try
		{
			boost::asio::post (self->GetService (), [self, promise, operation = std::forward<F> (operation)]() mutable
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
		catch (const std::exception&)
		{
			return { nullptr, 0 };
		}
		if (future.wait_for (CONTROL_DEFAULT_TIMEOUT) != std::future_status::ready)
			return { nullptr, 0 };
		try
		{
			return future.get ();
		}
		catch (const std::exception&)
		{
			return { nullptr, 0 };
		}
	}

	std::pair<std::shared_ptr<Torrent>, int> TorrentsTunnel::AddTorrentFromRpc (std::string_view torrentFileContent)
	{
		auto self = std::static_pointer_cast<TorrentsTunnel> (shared_from_this ());
		std::string content (torrentFileContent);
		return RunAddOnService (self, [self, content = std::move (content)]()
		{
			return self->AddTorrent (content);
		});
	}

	std::pair<std::shared_ptr<Torrent>, int> TorrentsTunnel::AddMagnetFromRpc (std::string_view magnet)
	{
		auto self = std::static_pointer_cast<TorrentsTunnel> (shared_from_this ());
		std::string value (magnet);
		return RunAddOnService (self, [self, value = std::move (value)]()
		{
			return self->AddMagnet (value);
		});
	}

	std::pair<std::shared_ptr<Torrent>, int> TorrentsTunnel::AddTorrent (std::string_view torrentFileContent)
	{
		auto torrent = std::make_shared<Torrent> (torrentFileContent);
		// An unparseable or incomplete metainfo must not enter the session: it
		// would otherwise be reported as added and persisted with no usable
		// files. Startup loading already rejects these with IsValid ().
		if (!torrent || torrent->GetError () != eTorrentErrorNoError || !torrent->IsValid ())
		{
			LogPrint (eLogWarning, "TorrentsTunnel: Rejecting invalid torrent file");
			return { nullptr, 0 };
		}
		auto it = m_Torrents.find (torrent->GetInfoHash ());
		if (it == m_Torrents.end ())
		{
			torrent->SetFullPath (m_TorrentsDir/std::filesystem::path (torrent->GetName ()));
			auto [id, inserted] = InsertTorrent (torrent);
			if (inserted)
				boost::asio::post (GetDiskIOService (), [this, torrent]()
					{
						SaveTorrentFile (torrent);
						InitTorrentFiles (torrent);
					});
			if (!torrent->GetError ())
				boost::asio::post (GetService (), [this, torrent] { RequestTorrentTrackers (torrent, eTrackerAnnounceEventNone); });
			return { torrent, id };
		}
		else
			return { it->second, FindTorrentID (it->second) };
		return { torrent, 0 };
	}

	std::pair<std::shared_ptr<Torrent>, int> TorrentsTunnel::AddMagnet (std::string_view magnet)
	{
		// magnet:?xt=urn:btih:<hash>&dn=<name>&tr=<tracker>
		static constexpr std::string_view magnetPrefix { "magnet:?" };
		if (magnet.starts_with (magnetPrefix))
		{
			Torrent::InfoHash infoHash;
			bool isInfoHashFound = false;
			std::string announce, name, hexStr;
			magnet = magnet.substr (magnetPrefix.size ());
			while (!magnet.empty())
			{
				auto pos = magnet.find ('&');
				std::string_view param;
				if (pos != std::string::npos)
				{
					param = magnet.substr (0, pos);
					magnet = magnet.substr (pos + 1);
				}
				else
				{
					param = magnet;
					magnet = "";
				}
				static constexpr std::string_view hashPrefix { "xt=urn:btih:" };
				static constexpr std::string_view trackerPrefix { "tr=" };
				static constexpr std::string_view namePrefix { "dn=" };
				if (param.starts_with (hashPrefix))
				{
					auto hashText = param.substr (hashPrefix.size ());
					// BEP 9 magnet xt may use a 40-character hexadecimal info
					// hash or a 32-character base32 hash. Reject partial or
					// overlong values instead of accepting a zero-padded hash.
					if (hashText.size () == infoHash.size ()*2)
					{
						try
						{
							boost::algorithm::unhex (hashText.begin(), hashText.end(), infoHash.begin());
							hexStr = std::string (hashText);
							isInfoHashFound = true;
						}
						catch (std::exception& ex)
						{
							LogPrint (eLogInfo, "TorrentsTunnel: Can't unhex magnet hash ", hashText);
						}
					}
					else if (hashText.size () == 32)
					{
						if (i2p::data::Base32ToByteStream (hashText, infoHash.data (), infoHash.size ()) == infoHash.size ())
						{
							hexStr = i2p::data::ByteStreamToBase32 (infoHash.data (), infoHash.size ());
							isInfoHashFound = true;
						}
						else
							LogPrint (eLogInfo, "TorrentsTunnel: Can't decode base32 magnet hash");
					}
					else
						LogPrint (eLogInfo, "TorrentsTunnel: Invalid magnet hash length ", hashText.size ());
				}
				else if (param.starts_with (trackerPrefix))
					announce = i2p::http::UrlDecode (param.substr (trackerPrefix.size ()));
				else if (param.starts_with (namePrefix))
					name = i2p::http::UrlDecode (param.substr (namePrefix.size ()));
			}
			if (isInfoHashFound)
			{
				auto it = m_Torrents.find (infoHash);
				if (it == m_Torrents.end ())
				{
					auto torrent = std::make_shared<Torrent> (infoHash);
					if (!announce.empty ()) torrent->SetAnnounce (announce);
					torrent->SetName (name.empty () ? hexStr : name);
					if (torrent->GetName ().empty ())
						torrent->SetName (hexStr);
					if (torrent->GetName ().empty ()) return { nullptr, 0 };
					auto [id, inserted] = InsertTorrent (torrent);
					if (inserted)
						boost::asio::post (GetService (), [this, torrent] { RequestTorrentTrackers (torrent, eTrackerAnnounceEventNone); });
					return { torrent, id };
				}
				else
					return { it->second, FindTorrentID (it->second) };
			}
		}
		return { nullptr, 0 };
	}

	std::pair<int, bool> TorrentsTunnel::InsertTorrent (std::shared_ptr<Torrent> torrent)
	{
		if (!torrent) return { 0, false };
		std::lock_guard<std::mutex> l(m_TorrentsMutex);
		if (m_Torrents.emplace (torrent->GetInfoHash (), torrent).second)
		{
			int id = 1;
			if (!m_TorrentsByID.empty ())
				id = m_TorrentsByID.rbegin ()->first + 1;
			m_TorrentsByID.emplace (id, torrent);
			if (!torrent->GetAnnounce ().empty ())
				torrent->SetAnnounceTrackerID (AddTracker (torrent->GetAnnounce (), false)); // add announce to trackers
			return { id, true };
 		}
 		else // already exists, find id
			return { FindTorrentID (torrent), false };
		return  { 0, false };
	}

	bool TorrentsTunnel::RemoveTorrent (int id, bool deleteFiles)
	{
		std::shared_ptr<Torrent> torrent;
		{
			std::lock_guard<std::mutex> l(m_TorrentsMutex);
			auto it = m_TorrentsByID.find (id);
			if (it == m_TorrentsByID.end ()) return false;
			torrent = it->second.lock ();
			m_TorrentsByID.erase (it);
			if (!torrent) return false;
			m_Torrents.erase (torrent->GetInfoHash ());
		}
		boost::asio::post (GetService (), [this, torrent, deleteFiles]()
			{
				RemoveTorrent (torrent, deleteFiles);
			});
		return true;
	}

	void TorrentsTunnel::RemoveAllTorrents (bool deleteFiles)
	{
		std::list<std::shared_ptr<Torrent> > torrentsToRemove;
		{
			std::lock_guard<std::mutex> l(m_TorrentsMutex);
			for (const auto& it: m_Torrents)
				torrentsToRemove.push_back (it.second);
			m_Torrents.clear ();
			m_TorrentsByID.clear ();
		}
		boost::asio::post (GetService (), [this, torrentsToRemove = std::move (torrentsToRemove), deleteFiles]()
		{
			for (auto it: torrentsToRemove)
				RemoveTorrent (it, deleteFiles);
		});
	}

	void TorrentsTunnel::RemoveTorrent (std::shared_ptr<Torrent> torrent, bool deleteFiles)
	{
		if (!torrent) return;
		StopTorrent (torrent);
		boost::asio::post (GetDiskIOService (), [this, torrent, deleteFiles]()
			{
				auto fullPath = torrent->GetFullPath ();
				if (!IsContainedPath (fullPath, true))
				{
					LogPrint (eLogError, "TorrentsTunnel: Refusing to remove path outside ", m_TorrentsDir);
					return;
				}
				auto torrentFilePath = fullPath; torrentFilePath += ".torrent";
				std::error_code ec;
				if (IsContainedPath (torrentFilePath, false))
					std::filesystem::remove (torrentFilePath, ec);
				else
					LogPrint (eLogError, "TorrentsTunnel: Refusing to delete ", torrentFilePath);
				if (ec)
					LogPrint (eLogError, "TorrentsTunnel: Can't delete ", torrentFilePath);
				auto resumeFilePath = fullPath; resumeFilePath += ".resume";
				if (IsContainedPath (resumeFilePath, false) && std::filesystem::exists (resumeFilePath))
				{
					std::filesystem::remove (resumeFilePath, ec);
					if (ec)
						LogPrint (eLogError, "TorrentsTunnel: Can't delete ", resumeFilePath);
				}
				else if (!IsContainedPath (resumeFilePath, false))
					LogPrint (eLogError, "TorrentsTunnel: Refusing to delete ", resumeFilePath);
				if (deleteFiles)
				{
					if (IsContainedPath (fullPath, true) && std::filesystem::exists (fullPath))
					{
						std::filesystem::remove_all (fullPath, ec);
						if (ec)
							LogPrint (eLogError, "TorrentsTunnel: Can't delete ", fullPath);
					}
					auto partFilePath = fullPath; partFilePath += ".part";
					if (IsContainedPath (partFilePath, false) && std::filesystem::exists (partFilePath))
					{
						std::filesystem::remove (partFilePath, ec);
						if (ec)
							LogPrint (eLogError, "TorrentsTunnel: Can't delete ", partFilePath);
					}
					else if (!IsContainedPath (partFilePath, false))
						LogPrint (eLogError, "TorrentsTunnel: Refusing to delete ", partFilePath);
				}
			});
	}

	int TorrentsTunnel::FindTorrentID (std::shared_ptr<Torrent> torrent) const
	{
		if (torrent)
		{
			auto it = std::find_if (m_TorrentsByID.begin (), m_TorrentsByID.end (),
				[torrent](const auto& torrentByID)
				{
					return torrentByID.second.lock () == torrent;
				});
			if (it != m_TorrentsByID.end ())
				return it->first;
		}
		return 0;
	}

	bool TorrentsTunnel::StopTorrent (int id)
	{
		std::shared_ptr<Torrent> torrent;
		{
			std::lock_guard<std::mutex> l(m_TorrentsMutex);
			auto it = m_TorrentsByID.find (id);
			if (it == m_TorrentsByID.end ()) return false;
			torrent = it->second.lock ();
			if (!torrent) return false;
		}
		boost::asio::post (GetService (), [this, torrent]()
			{
				StopTorrent (torrent);
			});
		return true;
	}

	void TorrentsTunnel::StopTorrent (std::shared_ptr<Torrent> torrent)
	{
		if (!torrent) return;
		torrent->SetStopped (true);
		torrent->UpdateStatus (i2p::util::GetMonotonicSeconds ());
		// inform trackers that we stopped
		RequestTorrentTrackers (torrent, eTrackerAnnounceEventStopped);
		// close connections
		auto connections = torrent->GetConnections ();
		for (auto it: connections)
			it->Close ();
	}

	void TorrentsTunnel::StopAllTorrents ()
	{
		std::list<std::shared_ptr<Torrent> > torrentsToStop;
		{
			std::lock_guard<std::mutex> l(m_TorrentsMutex);
			for (const auto& it: m_Torrents)
				torrentsToStop.push_back (it.second);
		}
		boost::asio::post (GetService (), [this, torrentsToStop = std::move (torrentsToStop)]()
		{
			for (auto it: torrentsToStop) // TODO: announce with random intervals
				StopTorrent (it);
		});
	}

	bool TorrentsTunnel::StartTorrent (int id)
	{
		std::shared_ptr<Torrent> torrent;
		{
			std::lock_guard<std::mutex> l(m_TorrentsMutex);
			auto it = m_TorrentsByID.find (id);
			if (it == m_TorrentsByID.end ()) return false;
			torrent = it->second.lock ();
			if (!torrent) return false;
		}
		boost::asio::post (GetService (), [this, torrent]()
			{
				torrent->SetStopped (false);
				// inform trackers that we started
				RequestTorrentTrackers (torrent, eTrackerAnnounceEventStarted);
			});
		return true;
	}

	void TorrentsTunnel::StartAllTorrents ()
	{
		std::list<std::shared_ptr<Torrent> > torrentsToStart;
		{
			std::lock_guard<std::mutex> l(m_TorrentsMutex);
			for (const auto& it: m_Torrents)
				torrentsToStart.push_back (it.second);
		}
		boost::asio::post (GetService (), [this, torrentsToStart = std::move (torrentsToStart)]()
		{
			for (auto it: torrentsToStart) // TODO: announce with random intervals
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
			std::lock_guard<std::mutex> l(m_TorrentsMutex);
			auto it = m_TorrentsByID.find (id);
			if (it == m_TorrentsByID.end ()) return false;
			torrent = it->second.lock ();
		}
		if (!torrent) return false;
		boost::asio::post (GetService (), [this, torrent]()
			{
				RequestTorrentTrackers (torrent, eTrackerAnnounceEventNone);
				if (m_DHT) m_DHT->GetPeersAndAnnounce (torrent);
			});
		return true;
	}

	void TorrentsTunnel::ReannounceAllTorrents ()
	{
		std::list<std::shared_ptr<Torrent> > torrentsToReannounce;
		{
			std::lock_guard<std::mutex> l(m_TorrentsMutex);
			for (const auto& it: m_Torrents)
				torrentsToReannounce.push_back (it.second);
		}
		boost::asio::post (GetService (), [this, torrentsToReannounce = std::move (torrentsToReannounce)]()
		{
			for (auto it: torrentsToReannounce) // TODO: announce with random intervals
			{
				RequestTorrentTrackers (it, eTrackerAnnounceEventNone);
				if (m_DHT) m_DHT->GetPeersAndAnnounce (it);
			}
		});
	}

	void TorrentsTunnel::UpdateTorrentInfo (std::shared_ptr<Torrent> torrent, std::string_view info)
	{
		if (!torrent || torrent->IsStopped ()) return;
		torrent->ParseInfo (info);
		torrent->SetFullPath (m_TorrentsDir/std::filesystem::path (torrent->GetName ()));
		boost::asio::post (GetDiskIOService (), [this, torrent]()
			{
				SaveTorrentFile (torrent);
				InitTorrentFiles (torrent);
			});
	}

	void TorrentsTunnel::Accept ()
	{
		auto localDestination = GetLocalDestination ();
		if (localDestination)
		{
			if (!localDestination->IsAcceptingStreams ()) // set it as default if not set yet
				localDestination->AcceptStreams ([this](std::shared_ptr<i2p::stream::Stream> stream)
					{
						if (stream)
						{
							auto conn = std::make_shared<PeerConnection> (shared_from_this (), stream);
							AddHandler (conn);
							conn->ReceiveHandshake ();
						}
					});
		}
		else
			LogPrint (eLogError, "TorrentsTunnel: Local destination not set");
	}

	void TorrentsTunnel::RequestTorrentTrackers (std::shared_ptr<Torrent> torrent, TrackerAnnounceEvent event)
	{
		for (size_t i = 0; i < m_Trackers.size (); i++)
			RequestTracker (i, torrent, event);
	}

	bool TorrentsTunnel::RequestTracker (size_t trackerID, std::shared_ptr<Torrent> torrent, TrackerAnnounceEvent event)
	{
		if (!torrent || trackerID >= m_Trackers.size() ||
			(!std::get<1>(m_Trackers[trackerID]) && std::get<0>(m_Trackers[trackerID]).host != i2p::http::URL (torrent->GetAnnounce ()).host)) // not common tracker or with torrent's announce
			return false;
		i2p::http::URL reqURL = std::get<0>(m_Trackers[trackerID]);
		if (!reqURL.host.ends_with (".i2p"))
		{
			LogPrint (eLogWarning, "TorrentsTunnel: Non-I2P address ", reqURL.host, " for torrent ", torrent->GetName ());
			return false;
		}
		if (reqURL.schema == "udp")
		{
			auto ts = i2p::util::GetMonotonicMilliseconds ();
			auto& [announce, common, connectionID, expiration, fromPort] = m_Trackers[trackerID];
			if (connectionID && ts <= expiration)
				SendAnnounceToDatagramTracker (trackerID, connectionID, torrent, reqURL.host, reqURL.port, fromPort, event);
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
		params.emplace ("peer_id", m_PeerID);
		params.emplace ("ip", GetLocalDestination ()->GetIdentity ()->ToBase64 () + ".i2p");
		params.emplace ("port", std::to_string (TORRENT_PORT)); // 6881
		params.emplace ("compact", "1");
		params.emplace ("uploaded", std::to_string (torrent->GetUploaded ()));
		params.emplace ("downloaded", std::to_string (torrent->GetLength () - torrent->GetLeft ()));
		params.emplace ("left", torrent->GetLength () ? std::to_string (torrent->GetLeft ()): "1"); // phony 1 for magnet
		int numWant = 0;
		if (!torrent->IsComplete () && (event == eTrackerAnnounceEventNone || event == eTrackerAnnounceEventStarted))
			numWant = TRACKER_MAX_NUM_WANT;
		params.emplace ("numwant", std::to_string (numWant));
		if (event != eTrackerAnnounceEventNone)
			params.emplace ("event", TrackerAnnounceEventStr[event]);
		reqURL.create_query (params);

		auto req = std::make_shared<boost::beast::http::request<boost::beast::http::string_body> >(boost::beast::http::verb::get, reqURL.to_string (true), 11); // HTTP 1.1
		req->set (boost::beast::http::field::host, reqURL.host);
		req->set (boost::beast::http::field::user_agent, "I2PSocketEepGet");
		req->keep_alive (false); // Connection: close
		CreateStream ([this, req, torrent, trackerID](std::shared_ptr<i2p::stream::Stream> stream)
			{
				if (stream)
				{
					auto httpStream = std::make_shared<i2p::client::BoostAsyncStream>(stream);
					boost::beast::http::async_write (*httpStream, *req,
						std::bind (&TorrentsTunnel::TrackerRequestSent, this, std::placeholders::_1,
							std::placeholders::_2, httpStream, torrent, req, trackerID));
				}
			}, reqURL.host, reqURL.port);
		return true;
	}

	void TorrentsTunnel::TrackerRequestSent (const boost::beast::error_code& ecode, size_t bytes_transferred,
		std::shared_ptr<i2p::client::BoostAsyncStream> httpStream, std::shared_ptr<Torrent> torrent,
		std::shared_ptr<boost::beast::http::request<boost::beast::http::string_body> > req, size_t trackerID)
	{
		if (!ecode)
		{
			// receive
			auto buf = std::make_shared<boost::beast::flat_buffer> ();
			auto res = std::make_shared<boost::beast::http::response<boost::beast::http::string_body> >();
			boost::beast::http::async_read (*httpStream, *buf, *res,
				[this, httpStream, torrent, buf, res, trackerID](const boost::beast::error_code& ecode, size_t bytes_transferred)
				{
					httpStream->GetStream ()->AsyncClose ();
					if (!ecode)
					{
						if (res->result () == boost::beast::http::status::ok)
						{
							torrent->ParseTrackerResponse (trackerID, res->body ());
							ConnectToPeers (torrent, trackerID);
						}
						else
							LogPrint (eLogWarning, "TorrentsTunnel: Tracker ", trackerID, " response code ", res->result_int());
					}
				});
		}
	}

	void TorrentsTunnel::ConnectToPeer (std::shared_ptr<Torrent> torrent, const i2p::data::IdentHash& peer,
		PeerConnectionOrigin origin)
	{
		if (!torrent || torrent->IsConnectedToPeer (peer)) return;
		LogPrint (eLogDebug, "TorrentsTunnel: Connecting to peer ", peer.ToBase32 () + ".b32.i2p");
		if (peer == GetLocalDestination ()->GetIdentHash ())
		{
			LogPrint (eLogInfo, "TorrentsTunnel: Can't connect to self");
			return;
		}
		CreateStream ([this, torrent, peer, origin](std::shared_ptr<i2p::stream::Stream> stream)
			{
				if (stream)
				{
					LogPrint (eLogDebug, "TorrentsTunnel: Connected to peer ", peer.ToBase32 () + ".b32.i2p");
					auto connection = std::make_shared<PeerConnection>(shared_from_this (), stream, torrent, origin);
					AddHandler (connection);
					connection->Connect ();
				}
				else
					LogPrint (eLogInfo, "TorrentsTunnel: Can't connect to peer ", peer.ToBase32 () + ".b32.i2p");
			}, std::make_shared<i2p::client::Address>(peer), TORRENT_PORT);
	}

	size_t TorrentsTunnel::ConnectToPeers (std::shared_ptr<Torrent> torrent)
	{
		if (!torrent || torrent->IsComplete ()) return 0;
		auto peersToConnect = torrent->GetNonConnectedPeers (); // from trackers
		if (!peersToConnect.empty ())
		{
			for (const auto& it: peersToConnect)
				ConnectToPeer (torrent, it, ePeerConnectionOriginTracker);
		}
		return peersToConnect.size ();
	}

	size_t TorrentsTunnel::ConnectToPeers (std::shared_ptr<Torrent> torrent, size_t trackerID)
	{
		if (!torrent || torrent->IsComplete ()) return 0;
		auto peersToConnect = torrent->GetNonConnectedPeers (trackerID);
		if (!peersToConnect.empty ())
		{
			for (const auto& it: peersToConnect)
				ConnectToPeer (torrent, it, ePeerConnectionOriginTracker);
		}
		return peersToConnect.size ();
	}

	void TorrentsTunnel::ConnectToNewPeers (std::shared_ptr<Torrent> torrent,
		std::unordered_set<i2p::data::IdentHash>& newPeers, PeerConnectionOrigin origin)
	{
		if (!torrent || torrent->IsComplete ()) return;
		if (!newPeers.empty ())
		{
			for (const auto& it: newPeers)
				if (!torrent->IsConnectedToPeer (it))
					ConnectToPeer (torrent, it, origin);
		}
	}

	void TorrentsTunnel::ScheduleTrackerRequestsCheck ()
	{
		m_TrackerRequestsCheckTimer.expires_after (std::chrono::milliseconds(TRACKER_REQUESTS_CHECK_TIMEOUT));
		m_TrackerRequestsCheckTimer.async_wait (std::bind (&TorrentsTunnel::HandleTrackerRequestsCheckTimer,
			this, std::placeholders::_1));
	}

	void TorrentsTunnel::HandleTrackerRequestsCheckTimer (const boost::system::error_code& ecode)
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
					if (ts > std::get<3>(it->second) + DATAGRAM_TRACKER_TRANSACTION_TIMEOUT)
						it = m_DatragramTrackerTransactions.erase (it);
					else
						it++;
				}
			}
			for (auto it: m_Torrents)
			{
				if (!it.second->IsStopped ())
				{
					for (size_t i = 0; i < m_Trackers.size (); i++)
					{
						if (!it.second->GetNextTrackerRequestTime (i) &&  // first time
							(std::get<1>(m_Trackers[i]) || it.second->GetAnnounceTrackerID () == (int)i))
						{
							auto initialInterval = GetLocalDestination ()->GetRng()() % TRACKER_INITIAL_REQUEST_INTERVAL_VARIANCE;
							if (initialInterval <= TRACKER_REQUESTS_CHECK_TIMEOUT) initialInterval = 0; // request immeditely
							it.second->SetNextTrackerRequestTime (i, ts + initialInterval);
						}
						if (ts >= it.second->GetNextTrackerRequestTime (i))
						{
							if (i2p::util::GetSecondsSinceEpoch () > it.second->GetLastTrackerUpdateTime (i) + 2*it.second->GetInterval (i)/1000 && it.second->GetTrackerError (i).empty ())
							{
								it.second->SetTrackerError (i, "No response");
								it.second->SetInterval (i, MIN_TRACKER_REQUESTS_INTERVAL); // try again shortly
							}
							if (RequestTracker (i, it.second, eTrackerAnnounceEventNone))
							{
								auto nextInterval = it.second->GetInterval (i) + GetLocalDestination ()->GetRng()() % TRACKER_REQUESTS_INTERVAL_VARIANCE;
								it.second->SetNextTrackerRequestTime (i, ts + nextInterval);
							}
						}
					}
					if (SupportsDHT ())
					{
						uint64_t nextUpdateTime = it.second->GetNextDHTUpdateTime ()*1000;
						if (nextUpdateTime)
						{
							if (ts >= nextUpdateTime)
							{
								if (m_DHT) m_DHT->GetPeersAndAnnounce (it.second);
								if (it.second->GetLastDHTAnnounceTime ())
									it.second->SetNextDHTUpdateTime (ts/1000 + DHT_TORRENT_UPDATE_INTERVAL +
										GetLocalDestination ()->GetRng()() % DHT_TORRENT_UPDATE_INTERVAL_VARIANCE);
								else
									it.second->SetNextDHTUpdateTime (ts/1000 + DHT_TORRENT_INITIAL_UPDATE_INTERVAL +
										GetLocalDestination ()->GetRng()() % DHT_TORRENT_INITIAL_UPDATE_INTERVAL_VARIANCE);
							}
						}
						else
							it.second->SetNextDHTUpdateTime (ts/1000 + DHT_TORRENT_INITIAL_UPDATE_INTERVAL +
								GetLocalDestination ()->GetRng()() % DHT_TORRENT_INITIAL_UPDATE_INTERVAL_VARIANCE);
					}
				}
			}
			ScheduleTrackerRequestsCheck ();
		}
	}

	void TorrentsTunnel::ScheduleKeepAliveCheck ()
	{
		m_KeepAliveCheckTimer.expires_after (std::chrono::seconds(PEER_KEEP_ALIVE_CHECK_INTERVAL));
		m_KeepAliveCheckTimer.async_wait (std::bind (&TorrentsTunnel::HandleKeepAliveCheckTimer,
			this, std::placeholders::_1));
	}

	void TorrentsTunnel::HandleKeepAliveCheckTimer (const boost::system::error_code& ecode)
	{
		if (ecode != boost::asio::error::operation_aborted)
		{
			auto ts = i2p::util::GetMonotonicSeconds ();
			IterateHandlers ([ts](std::shared_ptr<i2p::client::I2PServiceHandler> handler)
				{
					if (handler)
						std::static_pointer_cast<PeerConnection>(handler)->CheckKeepAlive (ts);
				});
			ScheduleKeepAliveCheck ();
		}
	}

	void TorrentsTunnel::ScheduleReconnectCheck ()
	{
		m_ReconnectCheckTimer.cancel ();
		m_ReconnectCheckTimer.expires_after (std::chrono::seconds(RECONNECT_CHECK_INTERVAL));
		m_ReconnectCheckTimer.async_wait (std::bind (&TorrentsTunnel::HandleReconnectCheckTimer,
			this, std::placeholders::_1));
	}

	void TorrentsTunnel::HandleReconnectCheckTimer (const boost::system::error_code& ecode)
	{
		if (ecode != boost::asio::error::operation_aborted)
		{
			auto ts = i2p::util::GetMonotonicSeconds ();
			for (auto it: m_Torrents)
				if (ts > it.second->GetNextReconnectTime ())
				{
					if (!it.second->IsComplete () && !it.second->IsStopped ())
					{
						auto numPeers = ConnectToPeers (it.second);
						if (numPeers)
							LogPrint (eLogDebug, "TorrentsTunnel: Reconnecting to ", numPeers, " peers");
					}
					it.second->SetNextReconnectTime (ts + RECONNECT_INTERVAL + GetLocalDestination ()->GetRng ()() % RECONNECT_INTERVAL_VARIANCE);
				}

			ScheduleReconnectCheck ();
		}
	}

	void TorrentsTunnel::ScheduleStatusUpdate ()
	{
		m_TorrentsStatusUpdateTimer.cancel ();
		m_TorrentsStatusUpdateTimer.expires_after (std::chrono::seconds(TORRENTS_STATUS_UPDATE_CHECK_INTERVAL));
		m_TorrentsStatusUpdateTimer.async_wait (std::bind (&TorrentsTunnel::HandleTorrentsStatusUpdateTimer,
			this, std::placeholders::_1));
	}

	void TorrentsTunnel::HandleTorrentsStatusUpdateTimer (const boost::system::error_code& ecode)
	{
		if (ecode != boost::asio::error::operation_aborted)
		{
			auto ts = i2p::util::GetMonotonicSeconds ();
			for (auto it: m_Torrents)
				if (ts > it.second->GetNextUpdateStatusTime ())
				{
					if (!it.second->IsStopped () && (it.second->IsActive () || !it.second->IsComplete ()))
					{
						if (it.second->UpdateStatus (ts))
						{
							if (!it.second->IsComplete ())
								CompleteTorrent (it.second);
						}
						else
							UpdatePeersPerPiece (it.second);
					}
					boost::asio::post (GetDiskIOService (), [torrent = it.second, ts]()
					{
						for (auto it: torrent->GetFiles ())
							if (ts > it->GetLastAccessTime () + TORRENT_FILE_INACTIVITY_TIMEOUT)
								it->Close ();
					});
					it.second->SetNextUpdateStatusTime (ts + TORRENTS_STATUS_UPDATE_INTERVAL + GetLocalDestination ()->GetRng ()() % TORRENTS_STATUS_UPDATE_INTERVAL_VARIANCE);
				}
			ScheduleStatusUpdate ();
		}
	}

	void TorrentsTunnel::UpdatePeersPerPiece (std::shared_ptr<Torrent> torrent)
	{
		if (!torrent) return;
		torrent->StartCountingPeers ();
		IterateHandlers ([torrent](std::shared_ptr<i2p::client::I2PServiceHandler> handler)
			{
				if (handler)
				{
					auto conn = std::static_pointer_cast<PeerConnection>(handler);
					if (conn->GetTorrent () == torrent)
						torrent->ApplyPeerRemoteBitfield (conn->GetRemoteBitfield ());
				}
			});
	}

	void TorrentsTunnel::HandleRecvFromI2PRaw (uint16_t fromPort, uint16_t toPort, const uint8_t * buf, size_t len)
	{
		if (m_DHT && toPort == m_DHT->GetRPort ())
		{
			m_DHT->HandleRawDatagram (buf, len);
			return;
		}
		// response from tracker
		if (len < 8) return;
		uint32_t action = bufbe32toh (buf);
		switch (action)
		{
			case eDatagramTrackerActionConnect:
				HandleConnectResponse (buf + 4, len - 4);
			break;
			case eDatagramTrackerActionAnnounce:
				HandleAnnounceResponse (buf + 4, len - 4);
			break;
			case eDatagramTrackerActionError:
				HandleErrorResponse (buf + 4, len - 4);
			break;
			default:
				LogPrint (eLogInfo, "TorrentsTunnel: Unexpected action ", action, " from tracker");
		}
	}

	void TorrentsTunnel::HandleErrorResponse (const uint8_t * buf, size_t len)
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
		auto [trackerID, fromPort, torrent, ts] = it->second;
		if (!torrent.expired ()) // response to announce
			torrent.lock ()->SetTrackerError (trackerID, error);
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
				uint32_t transactionID = localDestination->GetRng()();
				htobe32buf (connectRequest + 12, transactionID); // transactionID
				uint16_t fromPort = localDestination->GetRng()() % 1000 + 6000;
				if (m_DHT && fromPort == m_DHT->GetRPort ()) fromPort++;
				auto session = dgramDest->GetSession (address->identHash);
				if (session)
				{
					m_DatragramTrackerTransactions.emplace (transactionID, std::make_tuple (
						trackerID, fromPort, std::shared_ptr<Torrent>(nullptr), i2p::util::GetMonotonicMilliseconds ()));
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

	void TorrentsTunnel::HandleConnectResponse (const uint8_t * buf, size_t len)
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
			lifetime = std::max (bufbe16toh (buf + 12)*1000, DATAGRAM_TRACKER_CONNECTION_EXPIRATION);
			LogPrint (eLogDebug, "TorrentsTunnel: Datagram tracker connection lifetime set to ", lifetime/1000, " seconds");
		}
		auto [trackerID, fromPort, torrent, ts] = it->second;
		if (trackerID < m_Trackers.size ())
		{
			auto& [announce, common, connID, connExpiration, connFromPort] = m_Trackers[trackerID];
			connID = connectionID;
			connExpiration = i2p::util::GetMonotonicMilliseconds () + lifetime;
			connFromPort = fromPort;
		}
		m_DatragramTrackerTransactions.erase (it);
	}

	void TorrentsTunnel::SendAnnounceToDatagramTracker (size_t trackerID, uint64_t connectionID,
		std::shared_ptr<Torrent> torrent, std::string_view dest, uint16_t port,
		uint16_t fromPort, TrackerAnnounceEvent event)
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
				uint32_t transactionID = localDestination->GetRng()();
				htobe32buf (announce + 12, transactionID); // transaction_id
				memcpy (announce + 16, torrent->GetInfoHash ().data (), 20); // info_hash
				memcpy (announce + 36, m_PeerID.data (), 20); // peer_id
				auto left = torrent->GetLeft ();
				htobe64buf (announce + 56, torrent->GetLength () - left); // downloaded
				htobe64buf (announce + 64, torrent->GetLength () ? left : 1); // left, phony 1 for magnet
				htobe64buf (announce + 72, torrent->GetUploaded ()); // uploaded
				htobe32buf (announce + 80, event); // event
				htobe32buf (announce + 84, 0); // IP address 0:not used
				htobe32buf (announce + 88, 0); // key, ignored
				int numWant = 0;
				if (!torrent->IsComplete () && (event == eTrackerAnnounceEventNone || event == eTrackerAnnounceEventStarted))
					numWant = TRACKER_MAX_NUM_WANT;
				htobe32buf (announce + 92, numWant); // num_want
				htobe16buf (announce + 96, fromPort); // from port
				auto session = dgramDest->GetSession (address->identHash);
				if (session)
				{
					m_DatragramTrackerTransactions.emplace (transactionID, std::make_tuple (
						trackerID, fromPort, torrent, i2p::util::GetMonotonicMilliseconds ()));
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

	void TorrentsTunnel::HandleAnnounceResponse (const uint8_t * buf, size_t len)
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
		auto [trackerID, fromPort, torrent, ts] = it->second;
		if (!torrent.expired ())
		{
			uint32_t interval = bufbe32toh (buf + 4);
			uint32_t leechers = bufbe32toh (buf + 8);
			uint32_t seeders = bufbe32toh (buf + 12);
			torrent.lock ()->HandleDatagramTrackerResponse (trackerID, interval, buf + 16, len - 16, seeders, leechers);
		}
		m_DatragramTrackerTransactions.erase (it);
	}

	int TorrentsTunnel::AddTracker (std::string_view announce, bool isCommon)
	{
		if (announce.empty ()) return -1;
		i2p::http::URL reqURL (announce);
		if (!reqURL.host.ends_with (".i2p"))
		{
			LogPrint (eLogInfo, "TorrentsTunnel: Non-I2P address ", reqURL.host, " in tracker announce");
			return -1;
		}
		auto reqAddr = i2p::client::context.GetAddressBook ().GetAddress (reqURL.host);
		if (reqAddr)
		{
			auto it = std::find_if (m_Trackers.begin (), m_Trackers.end (),
				[host = std::string_view (reqURL.host), reqAddr](const TrackerInfo& tracker)
				{
					const auto& url = std::get<0>(tracker);
					if (url.host == host) return true;
					auto addr = i2p::client::context.GetAddressBook ().GetAddress (url.host);
					if (!addr) return false;
					return *addr == *reqAddr;
				});
			if (it != m_Trackers.end ()) // existing
				return it - m_Trackers.begin();
		}

		m_Trackers.emplace_back (TrackerInfo{ announce, isCommon, 0, 0, 0 });
		return m_Trackers.size () - 1;
	}

	void TorrentsTunnel::SendDHTPingQuery (const i2p::data::IdentHash& toIdent, uint16_t toPort)
	{
		boost::asio::post (GetService (), [this, toIdent, toPort]()
			{
				if (m_DHT) m_DHT->SendPingQuery (toIdent, toPort);
			});
	}
}
}

#endif // NO_TORRENTS

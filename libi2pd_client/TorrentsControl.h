/*
* Copyright (c) 2026, David Uhden Collado
* SPDX-License-Identifier: BSD-3-Clause
*
* This file is part of openi2pd and licensed under BSD-3-Clause
*
* See full license text in LICENSE file at top of project tree
*/

#ifndef TORRENTS_CONTROL_H__
#define TORRENTS_CONTROL_H__

#ifndef NO_TORRENTS

#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace i2p
{
namespace torrents
{
	// Upper bounds for one control snapshot. They keep both the snapshot and
	// the resulting TUI response bounded regardless of how many records a
	// torrent announces.
	inline constexpr size_t MAX_CONTROL_TORRENTS = 200;
	inline constexpr size_t MAX_CONTROL_FILES = 1000;
	inline constexpr size_t MAX_CONTROL_PEERS = 200;
	inline constexpr size_t MAX_CONTROL_TRACKERS = 100;
	inline constexpr size_t MAX_CONTROL_FILE_BYTES = 32 * 1024 * 1024;
	inline constexpr size_t MAX_CONTROL_ARGUMENT = 2048;
	inline constexpr std::chrono::milliseconds CONTROL_DEFAULT_TIMEOUT { 2000 };

	enum class TorrentCommandKind
	{
		Start,
		Stop,
		Pause,
		Resume,
		Reannounce,
		Remove,
		RemoveAll,
		StartAll,
		StopAll,
		ReannounceAll,
		AddMagnet,
		AddFile
	};

	struct TorrentFileInfo
	{
		std::string name;
		uint64_t length;
		uint64_t completed;
	};

	struct TorrentPeerInfo
	{
		std::string ident;
		std::string client;
		std::string flags;
		double progress;
		uint64_t downRate;
		uint64_t upRate;
	};

	struct TorrentTrackerInfo
	{
		std::string announce;
		int peers;
		int seeders;
		int leechers;
		std::string lastResult;
		uint64_t lastAnnounce;
		uint64_t nextAnnounce;
	};

	struct TorrentInfo
	{
		int id;
		std::string name;
		std::string status;
		std::string error;
		std::string hash;
		std::string magnet;
		uint64_t totalSize;
		uint64_t have;
		uint64_t downloaded;
		uint64_t uploaded;
		uint64_t downRate;
		uint64_t upRate;
		int peers;
		int seeders;
		int leechers;
		double percent;
		std::optional<uint64_t> etaSeconds;
		std::vector<TorrentFileInfo> files;
		std::vector<TorrentPeerInfo> peerDetails;
		std::vector<TorrentTrackerInfo> trackers;
	};

	struct TorrentSessionInfo
	{
		uint64_t downloaded;
		uint64_t uploaded;
		int active;
		int paused;
	};

	struct TorrentDhtInfo
	{
		bool supported;
		int numNodes;
		int numBuckets;
		int numTorrents;
		int numHeardFrom;
		int numFailedPeers;
	};

	struct TorrentsSnapshot
	{
		std::string tunnelName;
		std::vector<TorrentInfo> torrents;
		TorrentSessionInfo session;
		TorrentDhtInfo dht;
	};

	struct TorrentCommand
	{
		TorrentCommandKind kind;
		int torrentId = 0;
		bool deleteFiles = false;
		std::string argument;
	};

	struct TorrentCommandResult
	{
		bool success;
		std::string message;
		int id;
	};

	// True only for a well-formed I2P host name ending in ".i2p". The check
	// is case-insensitive and rejects empty labels, whitespace, path
	// separators, credentials, ports and trailing dots. It is deliberately
	// lexical: callers must still resolve the host through the I2P
	// AddressBook before opening a stream.
	bool IsI2PTrackerHost (std::string_view host);

	// Lower-case hexadecimal representation, using exactly two characters per
	// input byte. Used for torrent hashes and display magnets.
	std::string ToHexString (std::span<const uint8_t> data);

	// Builds a display-only magnet from an info hash and an optional name. The
	// name is percent-encoded; no tracker or filesystem data is included.
	std::string BuildTorrentMagnet (const std::array<uint8_t, 20> & infoHash, std::string_view name);

	// Reads a local .torrent file for TORRENT-ADD-FILE. The caller is the
	// daemon; the TUI never reads the file. Symlinks, non-regular files,
	// non-.torrent names, empty files and files larger than maxBytes are
	// rejected. On failure a human-readable reason is written to error.
	bool ReadTorrentFileBounded (
		const std::filesystem::path & path, size_t maxBytes, std::vector<uint8_t> & content, std::string & error);
}
}

#endif // NO_TORRENTS

#endif // TORRENTS_CONTROL_H__

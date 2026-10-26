/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: Torrents.
 *
 * Declares the torrent metadata, piece/file and peer-connection model plus
 * the BEP-encoded parser, path-containment checks and bounded file I/O
 * helpers used by the torrent engine.
 *
 */

#ifndef TORRENTS_H__
#define TORRENTS_H__

#ifndef NO_TORRENTS

#include <inttypes.h>
#include <string.h>
#include <openssl/sha.h>
#include <boost/asio.hpp>
#include <boost/dynamic_bitset.hpp>
#include <boost/logic/tribool.hpp>
#include <memory>
#include <functional>
#include <vector>
#include <array>
#include <atomic>
#include <list>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <random>
#include <tuple>
#include <utility>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <filesystem>
#include <fstream>
#include <mutex>
#include "util.h"
#include "TorrentsComments.h"
#include "TorrentPayloadStorage.h"
#include "Streaming.h"
#include "I2PService.h"

namespace i2p
{
namespace torrents
{
	// BEncoded
	std::pair<std::string_view, size_t> ExtractByteString (std::string_view buf);
	std::pair<int64_t, size_t> ExtractInteger (std::string_view buf);
	size_t ParseDictionary (std::string_view buf,
		std::function<size_t (std::string_view key, std::string_view buf)> handler = nullptr, size_t depth = 0);
	std::pair<std::map<std::string_view, std::string_view>, size_t> ExtractDictionary (
		std::string_view buf); // decoded keys, encoded values
	size_t ParseList (
		std::string_view buf, std::function<size_t (std::string_view buf)> handler = nullptr, size_t depth = 0);
	std::pair<std::vector<std::string_view>, size_t> ParseStringList (std::string_view buf);
	std::string CreateByteString (std::string_view str);
	std::string CreateInteger (int64_t v);
	std::string CreateDictionary (const std::vector<std::pair<std::string_view, std::string_view>> & items);
	std::string CreateList (const std::vector<std::string> & items);
	template <size_t sz> std::pair<size_t, bool> ParseByteArray (std::string_view buf, std::array<uint8_t, sz> & arr)
	{
		auto [str, len] = ExtractByteString (buf);
		if (len && str.size () == sz)
		{
			memcpy (arr.data (), str.data (), sz);
			return { len, true };
		}
		return { len, false };
	}

	constexpr size_t REQUEST_BLOCK_SIZE = 16384;
	constexpr size_t MIN_PIECE_LENGTH = 16 * 1024; // 16K
	constexpr size_t MAX_PIECE_LENGTH = 256 * 1024 * 1024; // 256M
	constexpr size_t MAX_NUM_TORRENT_PIECES = 1024 * 1024; // 1M
	// Bound the eager per-piece std::vector<BlockStatus> allocation. This is
	// independent of the nominal piece count: 1M pieces of 256 MiB would
	// otherwise request ~64 GiB of status arrays before any data arrives.
	constexpr size_t MAX_TORRENT_BLOCKS = 4 * 1024 * 1024;
	// Keep the arithmetic in 64 bits. On ILP32 the product below is 2^48 and
	// previously wrapped to zero because both operands are size_t.
	constexpr uint64_t MAX_TORRENT_LENGTH
		= static_cast<uint64_t> (MAX_PIECE_LENGTH) * static_cast<uint64_t> (MAX_NUM_TORRENT_PIECES); // 256T
	static_assert (MAX_TORRENT_LENGTH == (uint64_t { 256 } << 40), "MAX_TORRENT_LENGTH must stay at 256 TiB");
	static_assert (MAX_TORRENT_LENGTH > 0, "MAX_TORRENT_LENGTH must not overflow");
	static_assert (sizeof (MAX_TORRENT_LENGTH) == sizeof (uint64_t), "limit must be a 64-bit constant");
	constexpr size_t MAX_NUM_TORRENT_FILES = 10000;
	constexpr size_t MAX_NUM_TRACKERS = 512;
	constexpr size_t MAX_TRACKER_ANNOUNCE_LENGTH = 2048;
	constexpr uint16_t TORRENT_PORT = 6881; //  not used by required by protocol
	constexpr int MIN_TRACKER_REQUESTS_INTERVAL = 15 * 1000; // in milliseconds
	constexpr int MAX_TRACKER_REQUESTS_INTERVAL = 24 * 3600 * 1000; // in milliseconds
	// BEP 3 "interval"/"min interval" arrive in seconds. Java 2.14.0's
	// TrackerClient floors a tracker-advertised interval at
	// MIN_TRACKER_ANNOUNCE_INTERVAL (15 min) and keeps a larger "min interval",
	// so a tracker answering "interval=30" is announced to every 15 minutes and
	// not 30 times more often than Java. This floor applies to values a tracker
	// advertised only. MIN_TRACKER_REQUESTS_INTERVAL above stays the internal
	// retry/status granularity: a retry after a failure must never wait 15 min.
	constexpr int MIN_TRACKER_ANNOUNCE_INTERVAL = 15 * 60; // in seconds
	// Java's consecutive-failure sleep cap (TrackerClient LONG_SLEEP). The
	// retry interval doubles per failure and stops here instead of growing to
	// MAX_TRACKER_REQUESTS_INTERVAL (24 h).
	constexpr int MAX_TRACKER_FAILURE_BACKOFF = 30 * 60; // in seconds
	constexpr int DHT_TORRENT_UPDATE_INTERVAL = 880; // in seconds
	constexpr int DHT_TORRENT_UPDATE_INTERVAL_VARIANCE = 130; // in seconds
	constexpr int DHT_TORRENT_INITIAL_UPDATE_INTERVAL = 100; // in seconds
	constexpr int DHT_TORRENT_INITIAL_UPDATE_INTERVAL_VARIANCE = 40; // in seconds
	// Peer/connection limits. They bound outbound dialing and inbound accepts
	// per torrent and for the whole tunnel, so neither tracker responses, PEX,
	// DHT answers nor incoming streams can create an unbounded set of
	// connections. Rotation evicts only idle established peers.
	constexpr size_t MAX_PEERS_PER_TORRENT = 40;
	constexpr size_t MAX_TOTAL_PEERS = 200;
	// Concurrent outbound dials whose stream is still being established.
	constexpr size_t MAX_DIALS_PER_TORRENT = 5;
	constexpr size_t MAX_TOTAL_DIALS = 32;
	constexpr uint64_t PEER_DIAL_TIMEOUT = 60; // in seconds
	// One tracker response may only contribute this many candidates.
	constexpr size_t MAX_PEERS_PER_TRACKER_RESPONSE = 200;
	// Reject oversized tracker bodies before bencode parsing; a response
	// cannot need megabytes to describe a bounded peer set.
	constexpr size_t MAX_TRACKER_RESPONSE_BODY = 512 * 1024;
	constexpr size_t MAX_TRACKER_ID_LENGTH = 64;
	// I2P PEX keeps its 32-byte destination format: bound the number of
	// destinations per message and the per-connection announcement memory.
	constexpr size_t MAX_PEX_PEERS_PER_MESSAGE = 50;
	constexpr size_t MAX_PEX_TRACKED_PEERS = 200;
	constexpr int PEER_PEX_SEND_INTERVAL = 120; // in seconds
	constexpr int PEER_PEX_RECEIVE_WINDOW = 60; // in seconds
	constexpr size_t MAX_PEX_RECEIVE_MESSAGES = 2;
	constexpr size_t MAX_PEX_RECEIVE_PEERS = 100;
	constexpr int PEER_DHT_HINT_INTERVAL = 60; // in seconds (received hints)
	// Outgoing i2p_dht advertisement interval. BEP10 handshakes can be repeated
	// by a peer, so the advertisement needs its own per-connection gate instead
	// of relying on "it only happens once at negotiation".
	constexpr uint64_t PEER_DHT_ADVERTISE_INTERVAL = 60; // in seconds
	constexpr size_t PEER_CONNECTION_RECEIVE_BUFFER_SIZE = 65535;
	// A peer-wire message is prefixed by a 4-byte big-endian length. The whole
	// frame (prefix included) must fit in the receive buffer, otherwise a
	// message of exactly 65535 bytes would set m_NextMsgLength to 65539 and
	// leave the connection permanently unable to make progress.
	constexpr size_t MAX_PEER_MSG_LEN = PEER_CONNECTION_RECEIVE_BUFFER_SIZE - 4;
	static_assert (MAX_PEER_MSG_LEN + 4 == PEER_CONNECTION_RECEIVE_BUFFER_SIZE, "peer frame must fit the buffer");
	constexpr bool IsPeerMessageLengthAllowed (uint64_t msgLen)
	{
		return msgLen <= MAX_PEER_MSG_LEN;
	}
	constexpr int PEER_CONNECTION_MAX_IDLE = 3600; // in seconds
	constexpr int PEER_KEEP_ALIVE_TIMEOUT = 120; // in seconds
	constexpr int PEER_KEEP_SEND_INTERVAL = 95; // in seconds
	constexpr size_t MIN_NUM_REQUESTS = 8;
	constexpr size_t MAX_NUM_REQUESTS = 24;
	constexpr size_t MAX_NUM_PIECES = 6;
	constexpr size_t MAX_INCOMING_REQUESTS_QUEUE_SIZE = 32;
	// Per-request stall recovery. An outbound block request that the peer has
	// not delivered within this many seconds is released so the block becomes
	// requestable again (here or on another connection). Deliberately below
	// PEER_KEEP_SEND_INTERVAL (95 s) so the per-request deadline fires first;
	// the blanket release on a send-idle connection stays a safety net for
	// connections that keep sending keep-alives or other blocks.
	constexpr uint64_t PEER_REQUEST_TIMEOUT = 60; // in seconds
	// Idle peer rate decay. A peer rate is sampled only while blocks move, so
	// without decay an idle connection would report the last burst forever.
	// CheckKeepAlive runs every 15 s and halves a direction's rate after one
	// idle window and zeroes it after two. The window is deliberately much
	// longer than BANDWIDTH_RATE_SAMPLING_INTERVAL (20 ms): a 20 ms idle test
	// would zero every rate at each check and carry no information.
	constexpr uint64_t PEER_RATE_DECAY_INTERVAL = 30; // in seconds
	constexpr int PIECE_INACTIVITY_TIMEOUT = 22; // in seconds
	constexpr int HANDSHAKE_RECEIVE_TIMEOUT = 20; // in seconds
	constexpr int BANDWIDTH_RATE_SAMPLING_INTERVAL = 20; // in milliseconds
	constexpr int TORRENT_FILE_FLUSH_INTERVAL = 30; // in seconds
	constexpr int TORRENT_FILE_INACTIVITY_TIMEOUT = 90; // in seconds

	constexpr size_t HANDSHAKE_MSG_LENGTH = 68;
	constexpr size_t INTERESTED_MSG_LENGTH = 5;
	constexpr size_t NOTINTERESTED_MSG_LENGTH = 5;
	constexpr size_t CHOKE_MSG_LENGTH = 5;
	constexpr size_t UNCHOKE_MSG_LENGTH = 5;
	constexpr size_t HAVE_ALL_MSG_LENGTH = 5;
	constexpr size_t HAVE_NONE_MSG_LENGTH = 5;
	constexpr size_t REQUEST_MSG_PAYLOAD_LENGTH = 12;
	constexpr size_t REQUEST_MSG_LENGTH = REQUEST_MSG_PAYLOAD_LENGTH + 5;
	constexpr size_t REJECT_REQUEST_MSG_PAYLOAD_LENGTH = 12;
	constexpr size_t REJECT_REQUEST_MSG_LENGTH = REJECT_REQUEST_MSG_PAYLOAD_LENGTH + 5;
	constexpr size_t HAVE_MSG_PAYLOAD_LENGTH = 4;
	constexpr size_t PIECE_MSG_LENGTH = 13; // length + id + index + offset
	constexpr size_t CANCEL_MSG_PAYLOAD_LENGTH = 12;

	// Pure transfer-scheduling rules. They are constexpr and socket-free so the
	// regression test can pin them without a connection, a timer or a thread.

	// True when an outbound block request sent at `requestedAt` (monotonic
	// seconds) has exceeded `timeout`. A zero `requestedAt` (unknown, e.g. a
	// request queued before this bookkeeping existed) or a zero timeout never
	// expires, and clock non-monotonicity is treated as "not yet".
	constexpr bool ShouldExpireOutgoingRequest (uint64_t requestedAt, uint64_t now, uint64_t timeout)
	{
		if (!requestedAt || !timeout || now < requestedAt) return false;
		return now - requestedAt >= timeout;
	}

	// Decayed peer rate after `idleSeconds` without a sample. Unchanged inside
	// the first window, halved after one window and zero after two or more.
	constexpr uint64_t DecayPeerRate (uint64_t rate, uint64_t idleSeconds)
	{
		if (!rate || idleSeconds < PEER_RATE_DECAY_INTERVAL) return rate;
		return (idleSeconds / PEER_RATE_DECAY_INTERVAL >= 2) ? 0 : rate / 2;
	}

	// Bytes one transferred block charges to the peer and torrent counters.
	// BEP 3 requests carry their own length and the final block of a piece is
	// shorter than REQUEST_BLOCK_SIZE, so the nominal block size must never be
	// used to account a variable-length block.
	constexpr size_t BlockTransferBytes (size_t payloadLength)
	{
		return payloadLength;
	}

	// A piece counts as transferred progress only when its bytes are present,
	// hashed against the metainfo and flushed to storage. The same rule guards
	// IsFileComplete so a saving piece cannot report a finished file.
	constexpr bool IsPieceProgressComplete (bool complete, bool verified, bool saving)
	{
		return complete && verified && !saving;
	}

	// extensions
	constexpr std::string_view EXTENSION_NAME_UT_METADATA { "ut_metadata" };
	constexpr uint8_t EXTENSION_MSGID_UT_METADATA = 1;
	constexpr std::string_view EXTENSION_NAME_I2P_PEX { "i2p_pex" };
	constexpr uint8_t EXTENSION_MSGID_I2P_PEX = 2;
	constexpr std::string_view EXTENSION_NAME_I2P_DHT { "i2p_dht" };
	constexpr uint8_t EXTENSION_MSGID_I2P_DHT = 3;
	// BEP10 "v" client string. Kept only for diagnostics and bounded, so one
	// handshake cannot grow per-connection state arbitrarily.
	constexpr size_t MAX_EXTENDED_CLIENT_VERSION_LENGTH = 64;
	// Bound the extension map a single handshake may install.
	constexpr size_t MAX_EXTENDED_HANDSHAKE_EXTENSIONS = 64;

	// Pure BEP11 / i2p_dht decoders. They are side-effect free: no logging, no
	// network and no torrent/connection state, so the peer-wire handlers and
	// the fuzz/regression tests share exactly one parser. `malformed` means the
	// whole message must be ignored; otherwise `peers`/`port` is the bounded
	// subset that may be used.

	// BEP11 for I2P: "added" is a plain concatenation of 32-byte destination
	// hashes. There is no "added.f"/"dropped" in this profile. Zero hashes and
	// duplicates are dropped, the result is capped at MAX_PEX_RECEIVE_PEERS and
	// the dictionary must be consumed exactly.
	struct PEXDecodeResult
	{
		std::vector<i2p::data::IdentHash> peers;
		bool malformed = false;
	};
	PEXDecodeResult DecodePEXAdded (std::string_view dictionary);

	// i2p_dht port hint. "port" must be 1..65534; "rport" is optional and only
	// informative (Java uses "port"), so a present but unrelated "rport" is
	// tolerated and reported through rportMismatch for a debug log. The
	// dictionary must be consumed exactly.
	struct DHTHintResult
	{
		std::optional<uint16_t> port;
		bool malformed = false;
		bool rportMismatch = false;
	};
	DHTHintResult DecodeDHTHint (std::string_view dictionary);

	enum MessageType
	{
		eMessageTypeChoke = 0,
		eMessageTypeUnchoke = 1,
		eMessageTypeInterested = 2,
		eMessageTypeNotInterested = 3,
		eMessageTypeHave = 4,
		eMessageTypeBitfield = 5,
		eMessageTypeRequest = 6,
		eMessageTypePiece = 7,
		eMessageTypeCancel = 8,
		// BEP6
		eMessageTypeSuggestPiece = 13,
		eMessageTypeHaveAll = 14,
		eMessageTypeHaveNone = 15,
		eMessageTypeRejectRequest = 16,
		eMessageTypeAllowedFast = 17,
		// BEP10
		eMessageTypeExtended = 20
	};

	// Path helpers shared by the torrent metadata parser, the download session
	// and the tests. They are purely lexical unless stated otherwise.
	//
	// IsSafeRelativePath accepts only a non-empty relative path whose
	// components are neither "." nor ".." and which has no root name or root
	// directory. A caller must still anchor the result at a trusted root.
	bool IsSafeRelativePath (const std::filesystem::path & path);

	// Purely lexical containment check for callers on the network/control
	// thread. It performs no filesystem access (no symlink or existence
	// checks); the storage service must still call IsPathWithinRoot and the
	// no-follow helpers before opening, creating, renaming or deleting.
	bool IsPathLexicallyWithinRoot (const std::filesystem::path & root, const std::filesystem::path & target);

	// True when target is lexically inside root and no existing component
	// (including root itself) is a symlink/reparse point. Missing trailing
	// components are accepted so files can be created, but the caller must
	// recheck before use (and the safe file helpers below do not follow
	// symlinks at the leaf or below root).
	bool IsPathWithinRoot (
		const std::filesystem::path & root, const std::filesystem::path & target, bool allowDirectoryLeaf);

	// Bounded file helpers. Reads stop at maxBytes instead of trusting an
	// on-disk size, reject symlink leaves/reparse points and non-regular files
	// (FIFOs and devices cannot stall the disk service), and report truncated
	// files as failure. Writes replace/create only a non-symlink, regular leaf. On
	// Windows the reparse point check is performed before the CRT open
	// (residual TOCTOU); POSIX uses O_NOFOLLOW on every component below the
	// trusted root, so no such window exists there. Hard links are not
	// distinguishable portably; the torrent root must already be trusted and
	// not writable by other users.
	bool ReadFileBoundedNoFollow (
		const std::filesystem::path & path, size_t maxBytes, std::vector<uint8_t> & content, std::string & error);
	bool WriteFileNoFollow (const std::filesystem::path & path, const void * data, size_t len);
	// Atomic replace beneath the authorized root. POSIX anchors traversal and
	// fsyncs data and parent; Windows retains check/open reparse-point races.
	// Failure after rename may mean the replacement exists but durability is
	// uncertain; callers must report the error and must not truncate/retry.
	bool WriteFileAtomicNoFollow (const std::filesystem::path & root, const std::filesystem::path & relativePath,
		const void * data, size_t len, std::string & error);
	// Missing files are success; POSIX anchors parents without following links.
	// Windows retains the existing path-check/delete reparse-point race.
	bool RemoveFileNoFollow (
		const std::filesystem::path & root, const std::filesystem::path & relativePath, std::string & error);
	// Creates path exclusively (O_EXCL) and reserves exactly `size` bytes.
	bool CreateFileWithSizeNoFollow (const std::filesystem::path & path, size_t size);
	// Anchored variants: `relativePath` is traversed from `root` one component
	// at a time with O_NOFOLLOW, so an intermediate symlink cannot redirect
	// the operation.
	bool ReadFileBoundedNoFollow (const std::filesystem::path & root, const std::filesystem::path & relativePath,
		size_t maxBytes, std::vector<uint8_t> & content, std::string & error);
	bool WriteFileNoFollow (
		const std::filesystem::path & root, const std::filesystem::path & relativePath, const void * data, size_t len);
	bool CreateFileWithSizeNoFollow (
		const std::filesystem::path & root, const std::filesystem::path & relativePath, size_t size);
	// Creates the missing directories of `relativeDirectory` below `root`
	// without traversing symlinks. The root itself is created when missing but
	// must not be a symlink. `relativeDirectory` must be a safe relative path
	// (or empty).
	bool CreateDirectoriesWithinRootNoFollow (
		const std::filesystem::path & root, const std::filesystem::path & relativeDirectory);

	class TorrentFile
	{
	public:
		TorrentFile (const std::filesystem::path & fullFilePath, size_t fileLength)
			: m_FullFilePath (fullFilePath)
			, m_FileLength (fileLength)
			, m_IsPart (true)
			, m_File (-1)
			, m_LastAccessTime (0) { };
		~TorrentFile ()
		{
			std::lock_guard<std::mutex> lock (m_Mutex);
			CloseLocked ();
		}
		TorrentFile (const TorrentFile &) = delete;
		TorrentFile & operator= (const TorrentFile &) = delete;

		bool Save (size_t offset, const uint8_t * buf, size_t len);
		bool Load (size_t offset, uint8_t * buf, size_t len);
		void Complete ();
		void Open (bool create = false);
		void Close ();
		// True when a payload handle (POSIX fd or backend capability) is
		// currently held. Used by the startup scan to detect a missing or
		// unreadable document without opening the file twice.
		bool IsOpen () const;
		// One-shot capability probe for document backends: opens the exact
		// final name, closes it and reports whether it exists with the declared
		// length. It never logs (a new torrent's documents legitimately do not
		// exist yet) and never falls back to a POSIX path.
		bool ProbePayload ();

		std::filesystem::path GetFullFilePath () const
		{
			std::lock_guard<std::mutex> lock (m_Mutex);
			return m_FullFilePath;
		}
		void SetFullPath (const std::filesystem::path & p)
		{
			std::lock_guard<std::mutex> lock (m_Mutex);
			m_FullFilePath = p;
		}
		// Trusted anchor used to open every component below it with
		// O_NOFOLLOW. Must contain m_FullFilePath.
		void SetRootDir (const std::filesystem::path & root)
		{
			std::lock_guard<std::mutex> lock (m_Mutex);
			m_RootDir = root;
		}
		void SetStorageWorker (const std::shared_ptr<TorrentStorageWorker> & worker, const std::filesystem::path & root)
		{
			SetPayloadStorage (worker ? std::make_shared<WorkerPayloadStorage> (worker) : nullptr, root);
		}
		// Administrative capability injection; never derives authority from peer paths.
		void SetPayloadStorage (std::shared_ptr<TorrentPayloadStorage> storage, const std::filesystem::path & root)
		{
			std::lock_guard<std::mutex> lock (m_Mutex);
			CloseLocked ();
			m_PayloadStorage = std::move (storage);
			m_StorageRootDir = root;
		}
		bool HasPayloadStorage () const
		{
			std::lock_guard<std::mutex> lock (m_Mutex);
			return static_cast<bool> (m_PayloadStorage);
		}
		bool HasFinalNameStorage () const
		{
			std::lock_guard<std::mutex> lock (m_Mutex);
			return m_PayloadStorage && m_PayloadStorage->UsesFinalNames ();
		}
		bool RemovePayload (std::string & error)
		{
			std::lock_guard<std::mutex> lock (m_Mutex);
			if (!m_PayloadStorage || !m_PayloadStorage->UsesFinalNames ())
			{
				error = "document storage unavailable";
				return false;
			}
			CloseLocked ();
			auto relative = m_FullFilePath.lexically_relative (m_StorageRootDir);
			if (!IsSafeRelativePath (relative))
			{
				error = "unsafe document path";
				return false;
			}
			return m_PayloadStorage->Remove (relative, m_FileLength, error);
		}
		size_t GetFileLength () const { return m_FileLength; }
		uint64_t GetLastAccessTime () const { return m_LastAccessTime.load (std::memory_order_relaxed); }
		void SetIsPart (bool isPart)
		{
			std::lock_guard<std::mutex> lock (m_Mutex);
			m_IsPart = isPart;
		}
		// Returns false and clears the path when it is not a safe relative
		// path; callers must not use the file afterwards.
		bool UpdateFullPath (const std::filesystem::path & rootDir);

	private:
		// The disk worker reads/writes through Load()/Save() while the torrent
		// service thread completes files or replaces their authority. The handle
		// and storage capability must never be closed or reopened concurrently.
		void OpenLocked (bool create);
		void CloseLocked ();

		mutable std::mutex m_Mutex;
		std::filesystem::path m_FullFilePath;
		std::filesystem::path m_RootDir;
		size_t m_FileLength;
		bool m_IsPart;
		int m_File;
		bool m_FileWritable = false;
		std::shared_ptr<TorrentPayloadStorage> m_PayloadStorage;
		std::filesystem::path m_StorageRootDir;
		uint64_t m_StorageHandle = 0;
		bool m_StorageWritable = false;
		std::atomic<uint64_t> m_LastAccessTime; // monotonic seconds, read by the inactivity sweeper
	};

	struct PieceFileFragment // fragment to save to/load from file
	{
		std::shared_ptr<TorrentFile> file;
		size_t fileOffset;
		size_t fragmentOffset; // from start of piece
		size_t fragmentSize;

		PieceFileFragment (
			std::shared_ptr<TorrentFile> file1, size_t fileOffset1, size_t fragmentOffset1, size_t fragmentSize1)
			: file (file1)
			, fileOffset (fileOffset1)
			, fragmentOffset (fragmentOffset1)
			, fragmentSize (fragmentSize1) { };
		PieceFileFragment (PieceFileFragment &&) = default;
		PieceFileFragment (const PieceFileFragment &) = default;
	};

	class PeerConnection;
	class Piece final
	{
		enum class BlockStatus
		{
			Missing,
			Available,
			Requested
		};

	public:
		Piece (size_t size, const uint8_t * hash);
		// A per-piece mutex makes the object non-copyable. The vector of
		// pieces only needs the explicit move constructor; it is reserved up
		// front and never rearranged concurrently.
		Piece (Piece && other) noexcept;
		Piece (const Piece &) = delete;
		Piece & operator= (const Piece &) = delete;
		Piece & operator= (Piece &&) = delete;
		~Piece ();

		// All mutable state below is guarded by m_Mutex. That includes the
		// data buffer, the block map and the sending/loading flags, because
		// the service thread and the disk I/O thread both touch a piece.
		bool IsComplete () const;
		// Pending local verification must not publish candidate availability.
		void Complete (bool pendingVerification = false);
		bool VerifyHash (); // marks the piece verified when the digest matches
		bool IsVerified () const;
		// Local data re-verification. A resume bitfield or a pre-existing final
		// file only claims which bytes should be on disk; this state marks a
		// complete piece whose bytes have not been hashed in this session.
		// It is owned by the tunnel's bounded verification pass and is not the
		// same as a pending write (m_IsSaving).
		void MarkPendingVerification ();
		bool IsVerificationPending () const;
		void FinishLocalVerification ();
		void SetIsSaving (bool saving);
		bool IsSaving () const;
		void SetIsSending (bool isSending);
		void SetIsLoading (bool loading);
		bool IsSending () const;
		bool IsLoading () const;
		uint64_t GetLastActivityTimestamp () const;
		bool IsRequested () const;
		size_t GetNumPeers () const;
		void SetNumPeers (size_t numPeers);

		bool BlockReceived (const uint8_t * block, size_t len, size_t offset); // true only if newly applied
		bool Dump (PieceFileFragment && fragment);
		bool Load (PieceFileFragment && fragment);
		bool HasData () const;
		// Copies a validated block under the piece lock. The caller never
		// retains a pointer into a buffer that another thread can free.
		bool CopyBlock (size_t offset, size_t len, std::vector<uint8_t> & out) const;
		size_t GetSize () const { return m_Size; }
		bool HasBlock (size_t offset) const;
		std::pair<size_t, size_t>
		GetNextBlockToRequest (); // return (offset, len) of next buffer, len = 0 if no next buffer
		void ClearAllRequests ();
		// Releases one outstanding block owned by a connection. No-op if the
		// block is already Available, complete or no longer Requested.
		void ReleaseBlock (size_t offset, uint64_t epoch);
		uint64_t GetBlockEpoch () const;
		// Rebuilds the block map as Missing, drops the data buffer and clears
		// the verified flag, so a complete-but-corrupt piece becomes
		// downloadable again.
		void MarkCorrupt ();
		void InvalidateAllBlocks () { MarkCorrupt (); }
		// Drops the data buffer while preserving the complete/verified state.
		// Used to evict idle pieces without forgetting that they are complete.
		void ReleaseData ();
		void Reset ();

	private:
		bool IsAvailableLocked (int block) const;
		size_t GetNumBlocks (size_t len) const;
		void NewDataBufferLocked ();
		void DeleteDataBufferLocked ();
		void MarkCorruptLocked ();
		void ReleaseDataLocked ();

	private:
		mutable std::mutex m_Mutex;
		size_t m_Size;
		uint64_t m_BlockEpoch = 0;
		std::unique_ptr<uint8_t[]> m_Data;
		uint8_t m_Hash[SHA_DIGEST_LENGTH];
		std::unique_ptr<std::vector<BlockStatus>> m_Blocks;
		bool m_IsSending, m_IsLoading, m_IsRequested, m_IsVerified;
		bool m_IsSaving = false;
		bool m_IsVerificationPending = false;
		uint64_t m_LastActivityTimestamp; // monotonic seconds
		size_t m_NumPeers; // where this piece is available
	};

	enum TorrentStatus
	{
		eTorrentStatusStopped = 0,
		eTorrentStatusQueuedToVerifyLocalData = 1,
		eTorrentStatusVerifyingLocalData = 2,
		eTorrentStatusQueuedToDownload = 3,
		eTorrentStatusDownloading = 4,
		eTorrentStatusQueuedToSeed = 5,
		eTorrentStatusSeeding = 6
	};

	enum TorrentError
	{
		eTorrentErrorNoError = 0,
		eTorrentErrorMalformedMetaInfo = 1,
		eTorrentErrorNonSupportedVersion = 2,
		eTorrentErrorFilesystem = 3,
		eNumTorrentErrors
	};

	constexpr std::array<std::string_view, eNumTorrentErrors> TorrentErrorStr { "", "Malformed metaInfo",
		"Non-supported version", "Filesystem error" };

	using RequestedBlock = std::tuple<uint32_t, uint32_t, uint32_t>; // (index, offset, len)

	// Pure tracker announce scheduling arithmetic, shared by the HTTP and the
	// datagram tracker responses. Wire values are seconds (BEP 3); results are
	// milliseconds to match Torrent::TrackerStats.
	struct TrackerSchedule
	{
		// Tracker-advertised "interval" in seconds, floored at
		// MIN_TRACKER_ANNOUNCE_INTERVAL and capped at
		// MAX_TRACKER_REQUESTS_INTERVAL.
		static int AdvertisedInterval (int64_t intervalSeconds);
		// Tracker-advertised "min interval" in seconds. A value below the
		// announce floor is stored as sent: it can only raise, never lower,
		// the effective interval.
		static int AdvertisedMinInterval (int64_t minIntervalSeconds);
		// Effective interval: the advertised value raised to a larger advertised
		// minimum and to MIN_TRACKER_ANNOUNCE_INTERVAL.
		static int EffectiveInterval (int interval, int minInterval);
		// Response without "interval": re-base at the announce floor.
		static int DefaultInterval (int minInterval);
		// Interval after a consecutive failure. The first failure starts at
		// MIN_TRACKER_REQUESTS_INTERVAL so a retry stays short, each later one
		// doubles, and the ladder stops at MAX_TRACKER_FAILURE_BACKOFF
		// (30 min). A larger advertised minimum still governs. A
		// previousFailureInterval of 0 means no failure is pending.
		static int FailureInterval (int previousFailureInterval, int minInterval);
	};

	// Tracker "left"/"completed" announce parameters. Deliberately separate
	// from UI/RPC progress and from Torrent::IsComplete(): BEP 3 describes the
	// whole torrent to the swarm, while a skip-selected torrent is reported as
	// complete locally and keeps seeding. See Torrent::GetAnnounceProgress().
	struct AnnounceProgress
	{
		int64_t left; // bytes still missing in the whole torrent ("left")
		bool completed; // every piece verified, so event=completed is truthful
	};

	enum PeerConnectionOrigin
	{
		ePeerConnectionOriginTracker = 0,
		ePeerConnectionOriginIncoming,
		ePeerConnectionOriginPEX,
		ePeerConnectionOriginDHT
	};

	// Per-torrent peer-candidate table. PEX and DHT offer destinations that are
	// then dialed once and forgotten; without a table the same candidate is
	// re-dialed at every message, and 40 connections times 100 candidates per
	// minute can starve the dial budget. The table is bounded, dedupes, expires
	// per origin, remembers failed attempts and is only ever touched on the
	// torrent service thread (like the connection and piece state).
	constexpr size_t MAX_PEER_CANDIDATES = 256;
	// Per-origin fixed (non-sliding) admission window.
	constexpr size_t MAX_PEER_CANDIDATES_PER_WINDOW = 100;
	constexpr uint64_t PEER_CANDIDATE_WINDOW = 60; // in seconds
	// A PEX candidate is short-lived gossip; a DHT peer is worth keeping longer.
	constexpr uint64_t PEER_CANDIDATE_PEX_TTL = 1800; // in seconds
	constexpr uint64_t PEER_CANDIDATE_DHT_TTL = 10800; // in seconds
	constexpr uint64_t PEER_CANDIDATE_RETRY_DELAY = 30; // in seconds
	constexpr uint64_t PEER_CANDIDATE_MAX_RETRY_DELAY = 900; // in seconds
	constexpr uint32_t PEER_CANDIDATE_MAX_FAILURES = 4;

	struct PeerCandidate
	{
		i2p::data::IdentHash ident { };
		PeerConnectionOrigin origin = ePeerConnectionOriginTracker;
		uint64_t added = 0; // monotonic seconds
		uint64_t lastAttempt = 0; // monotonic seconds, 0 = never dialed
		uint32_t failures = 0;
	};

	struct PeerCandidateWindow
	{
		uint64_t start = 0; // monotonic seconds
		size_t admitted = 0;
	};

	// Pure candidate lifetime/backoff rules, socket-free and clock-parameterised
	// so the regression test can pin them without a Torrent or a timer.
	struct PeerCandidatePolicy
	{
		static constexpr uint64_t Ttl (PeerConnectionOrigin origin)
		{
			return origin == ePeerConnectionOriginDHT ? PEER_CANDIDATE_DHT_TTL : PEER_CANDIDATE_PEX_TTL;
		}

		// Retry delay after `failures` failed dials: 30 s doubling to 900 s.
		// The loop returns as soon as the cap is reached, so an arbitrary
		// failure count cannot spin.
		static constexpr uint64_t RetryDelay (uint32_t failures)
		{
			uint64_t delay = PEER_CANDIDATE_RETRY_DELAY;
			for (uint32_t failure = 1; failure < failures; ++failure)
			{
				if (delay >= PEER_CANDIDATE_MAX_RETRY_DELAY) return PEER_CANDIDATE_MAX_RETRY_DELAY;
				delay *= 2;
			}
			return delay < PEER_CANDIDATE_MAX_RETRY_DELAY ? delay : PEER_CANDIDATE_MAX_RETRY_DELAY;
		}

		// True when a remembered candidate may be dialed at `now`.
		// Not dialable: without a clock, outside its per-origin TTL, while the
		// backoff after a previous attempt is still running, after the failure
		// budget is spent, or when the monotonic clock went backwards (a
		// regression must never wrap `now - added` into a fresh lifetime).
		static constexpr bool IsUsable (
			uint64_t added, uint64_t lastAttempt, uint32_t failures, uint64_t now, PeerConnectionOrigin origin)
		{
			if (!now || !added || now < added) return false;
			if (now - added >= Ttl (origin)) return false;
			if (failures >= PEER_CANDIDATE_MAX_FAILURES) return false;
			if (!lastAttempt) return true;
			if (now < lastAttempt) return false;
			return now - lastAttempt >= RetryDelay (failures);
		}
	};

	class PeerConnection;
	class Torrent final
	{
		using TrackerStats = std::tuple<std::unordered_set<i2p::data::IdentHash>, int, uint64_t, int, int, uint64_t,
			std::string, int, int>;
		// (peers, interval, next tracker request time in monotonic milliseconds,
		//  seeders, leechers, last update time in seconds since epoch, error,
		//  minimum interval announced by the tracker in milliseconds,
		//  consecutive-failure retry interval in milliseconds, 0 on success)
	public:
		using InfoHash = std::array<uint8_t, SHA_DIGEST_LENGTH>;
		// Per-torrent random peer id. Never derived from the tunnel identity.
		using PeerID = std::array<uint8_t, 20>;
		using TrackerTiers = std::vector<std::vector<std::string>>;
		// Pure metadata builder for the storage/hash worker. Piece hashes cover
		// concatenated files in the supplied order, including file boundaries.
		// Empty relative paths denote a single file; otherwise all paths are
		// relative to name. No filesystem or network operation is performed.
		static std::shared_ptr<Torrent> CreateV1 (std::string_view name,
			const std::vector<std::pair<std::filesystem::path, size_t>> & files, size_t pieceLength,
			const std::vector<InfoHash> & pieceHashes, const TrackerTiers & trackers = { },
			bool privateTorrent = false);

		Torrent (std::string_view buf);
		Torrent (const InfoHash & infoHash);
		size_t ParseInfo (std::string_view buf);
		std::string CreateTorrentFileContent () const;
		void ParseTrackerResponse (size_t trackerID, std::string_view buf);
		void HandleDatagramTrackerResponse (size_t trackerID, uint32_t interval, const uint8_t * hashes,
			size_t hashesLen, int numSeeders, int numLeechers);

		bool IsComplete () const { return m_IsComplete.load (std::memory_order_acquire); }
		void SetComplete ();
		bool IsStopped () const { return m_IsStopped; }
		// Stopping also purges the peer-candidate table: a stopped torrent must
		// not keep a dialable backlog across a stop/start cycle.
		void SetStopped (bool stopped);
		bool IsActive () const { return !m_Connections.empty (); }
		TorrentStatus GetStatus () const;
		bool IsSingleFile () const { return m_IsSingleFile; };

		std::string_view GetAnnounce () const { return m_Announce; }
		void SetAnnounce (std::string_view announce) { m_Announce = announce; }
		const TrackerTiers & GetTrackerTiers () const { return m_TrackerTiers; }
		bool HasExplicitTrackers () const { return m_HasExplicitTrackers; }
		void SetHasExplicitTrackers (bool explicitTrackers) { m_HasExplicitTrackers = explicitTrackers; }
		bool SetTrackerTiers (const TrackerTiers & tiers);
		bool IsPrivate () const { return m_IsPrivate; }
		// Service-thread only; storage workers receive encoded snapshots.
		TorrentCommentStore & GetCommentStore () { return m_Comments; }
		const TorrentCommentStore & GetCommentStore () const { return m_Comments; }
		void SetCommentsEnabled (bool enabled);
		// Re-announce support and request the current view from established
		// peers. The revision-aware request throttle still applies; no
		// unsolicited comment push is invented.
		void PublishComments ();
		// Service-thread only. Per-torrent admission for new comment entries: a
		// fixed 60 s window (MAX_COMMENT_ENTRIES_PER_TORRENT_WINDOW, monotonic
		// seconds) that is independent of peers, so reconnecting cannot refill
		// the store faster than one torrent-wide budget. Charges `count` and
		// returns false when the window cannot take all of it (nothing is
		// charged then). count == 0 always succeeds without consuming the
		// window. Callers must already have checked enabled/private/valid.
		bool AdmitCommentEntries (size_t count, uint64_t nowSeconds);
		int GetAnnounceTrackerID () const { return m_AnnounceTrackerID; }
		void SetAnnounceTrackerID (int trackerID) { m_AnnounceTrackerID = trackerID; }
		// Per-torrent random 20-byte peer id used in handshakes and announces.
		const PeerID & GetPeerID () const { return m_PeerID; }
		// Opaque tracker id (BEP 3 "tracker id") echoed on later announces.
		std::string GetTrackerID (size_t trackerID) const;
		void SetTrackerID (size_t trackerID, std::string_view id);
		int GetMinInterval (size_t trackerID) const
		{
			return (trackerID < m_TrackerStats.size ()) ? std::get<7> (m_TrackerStats[trackerID])
														: MIN_TRACKER_REQUESTS_INTERVAL;
		}
		// Clamped to the supported range. A tracker minimum below
		// MIN_TRACKER_ANNOUNCE_INTERVAL is stored as sent and can never lower
		// the effective announce interval.
		void SetMinInterval (size_t trackerID, int interval);
		std::string_view GetName () const { return m_Name; }
		std::string_view GetMetaComment () const { return m_MetaComment; }
		std::string_view GetCreatedBy () const { return m_CreatedBy; }
		bool SetMetaComment (std::string_view comment);
		bool SetCreatedBy (std::string_view createdBy);
		void SetName (std::string_view name) { m_Name = AdjustName (name); }
		bool IsValid () const { return !m_Name.empty () && m_PieceLength && (m_Length || !m_Files.empty ()); }
		TorrentError GetError () const { return m_Error.load (std::memory_order_acquire); }
		std::string_view GetErrorStr () const { return TorrentErrorStr[GetError ()]; }
		void SetError (TorrentError error) { m_Error.store (error, std::memory_order_release); }
		const std::filesystem::path & GetFullPath () const { return m_FullPath; }
		void SetFullPath (const std::filesystem::path & fullPath) { m_FullPath = fullPath; }
		const std::list<std::shared_ptr<TorrentFile>> & GetFiles () const { return m_Files; }
		std::list<std::shared_ptr<TorrentFile>> & GetFiles () { return m_Files; }
		size_t GetLength () const { return m_Length; }
		size_t GetPieceLength () const { return m_PieceLength; }
		// Service-thread policy only; metadata, payload and resume bitfields
		// retain all files. Shared boundary pieces use the maximum priority.
		int GetFilePriority (size_t index) const;
		bool SetFilePriority (size_t index, uint8_t priority);
		std::vector<uint8_t> GetFilePriorities () const { return m_FilePriorities; }
		bool SetFilePriorities (const std::vector<uint8_t> & priorities);
		uint8_t GetPiecePriority (size_t index) const;
		size_t GetSelectedWantedBytes () const;
		size_t GetSelectedVerifiedBytes () const;
		// Empty selection is complete; this does not mark the torrent or any
		// skipped piece complete, verified, or available for upload.
		bool IsSelectedComplete () const;
		// Effective announce interval in milliseconds: the tracker-advertised
		// value, raised to a larger advertised minimum and floored at
		// MIN_TRACKER_ANNOUNCE_INTERVAL. Never used for failure retries.
		int GetInterval (size_t trackerID) const
		{
			return (trackerID < m_TrackerStats.size ()) ? std::get<1> (m_TrackerStats[trackerID])
														: MIN_TRACKER_REQUESTS_INTERVAL;
		}
		void SetInterval (size_t trackerID, int interval);
		const std::vector<uint8_t> & GetInfo () const { return m_Info; }
		const InfoHash & GetInfoHash () const { return m_InfoHash; }
		// Selected-file left bytes. Skips priority-0 files, whose pieces are
		// never requested. GetFullLeft() retains the whole-torrent truth used
		// by resume persistence and diagnostics.
		size_t GetLeft () const;
		size_t GetFullLeft () const;
		// Tracker announce parameters. "left" is the whole-torrent remaining
		// bytes (GetFullLeft()), or the phony 1 while no piece exists yet
		// (magnet/BEP 9 pending); "completed" requires every piece to be
		// verified, matching Java 2.14.0's TrackerClient. A skip-selected torrent
		// can be IsComplete() and still advertise left > 0 without
		// event=completed; only the tracker view changes, never the local
		// progress semantics.
		AnnounceProgress GetAnnounceProgress () const;
		std::string GetHexStringInfoHash () const; // in url format
		size_t GetNumPieces () const { return m_Pieces.size (); }
		Piece & GetPiece (int index) { return m_Pieces[index]; }
		void InvalidatePiece (size_t index)
		{
			if (index >= m_Pieces.size ()) return;
			m_Pieces[index].MarkCorrupt ();
			m_IsComplete.store (false, std::memory_order_release);
		}
		// A peer-supplied block is acceptable only while the torrent is
		// running and only when it is a non-empty, in-range slice of the
		// actual piece (the final piece may be shorter than m_PieceLength).
		// The subtraction form keeps offset + len from wrapping on ILP32.
		bool CanAcceptPieceBlock (uint32_t index, size_t offset, size_t len) const;
		// True only when every piece is complete and its SHA-1 has actually
		// been checked against the metainfo. A resume bitfield alone does not
		// satisfy this, so .part files are not promoted to final names until
		// the local data has been verified.
		bool AreAllPiecesVerified () const;
		// True when every piece that belongs to at least one selected file has
		// been completed and hash-verified in this session. An empty selection
		// is complete by definition.
		bool AreAllSelectedPiecesVerified () const;
		bool IsFileComplete (size_t index) const;
		// Best-effort corrupt-peer accounting. A rejected piece may have been
		// assembled from several peers' blocks, so a hash mismatch can only
		// charge the connection that delivered the block completing it; a peer
		// that contributed an earlier, fine-looking block is not identifiable
		// after the fact. The table is in-memory, per-torrent and bounded: no
		// ban is persisted, no wire format changes, and the only effect is that
		// the charged connection is closed at PEER_PENALTY_THRESHOLD.
		static constexpr size_t MAX_PENALIZED_PEERS = 64;
		static constexpr uint32_t PEER_PENALTY_THRESHOLD = 3;
		// Service-thread only. Returns the peer's failure count after the
		// charge, or 0 when the bounded table is full: a new peer is then not
		// tracked (existing counts are never evicted to make room).
		uint32_t ChargePieceFailure (const i2p::data::IdentHash & peer);
		uint32_t GetPeerPieceFailures (const i2p::data::IdentHash & peer) const;
		bool IsPeerPenalized (const i2p::data::IdentHash & peer) const
		{
			return GetPeerPieceFailures (peer) >= PEER_PENALTY_THRESHOLD;
		}
		size_t GetNumPieceFailurePeers () const { return m_PeerPieceFailures.size (); }
		void SetSelectedComplete ();
		std::pair<std::vector<uint8_t>, boost::logic::tribool>
		CreateBitfield () const; // (bitfield, true - all false - none)
		// Exact wire length and zero spare bits. Separate from ApplyBitfield
		// because a valid partial resume is not "complete".
		bool IsBitfieldValid (const std::vector<uint8_t> & bitfield) const;
		bool ApplyBitfield (const std::vector<uint8_t> & bitfield); // return true if complete
		std::unordered_set<i2p::data::IdentHash> GetNonConnectedPeers ();
		std::unordered_set<i2p::data::IdentHash> GetNonConnectedPeers (size_t trackerID);
		RequestedBlock GetNextBlockToRequest (std::shared_ptr<PeerConnection> conn, bool skipRequested = true);
		std::vector<PieceFileFragment> GetPieceFileFragments (int index) const;
		// Verified-and-flushed bytes per file. A complete-but-unverified or
		// currently-saving piece contributes nothing, so a file never reports
		// 100% while HAVE and serving still withhold its pieces.
		std::vector<size_t> GetFilesCompleted () const;
		bool UpdateStatus (uint64_t ts); // return true if complete
		uint64_t GetNextUpdateStatusTime () { return m_NextUpdateStatusTime; }
		void SetNextUpdateStatusTime (uint64_t nextUpdateStatusTime) { m_NextUpdateStatusTime = nextUpdateStatusTime; }
		uint64_t GetNextReconnectTime () { return m_NextReconnectTime; }
		void SetNextReconnectTime (uint64_t nextReconnectTime) { m_NextReconnectTime = nextReconnectTime; }
		uint64_t GetNextDHTUpdateTime () { return m_NextDHTUpdateTime; }
		void SetNextDHTUpdateTime (uint64_t nextDHTUpdateTime) { m_NextDHTUpdateTime = nextDHTUpdateTime; }
		uint64_t GetLastDHTAnnounceTime () const { return m_LastDHTAnnounceTime; }
		void SetLastDHTAnnounceTime (uint64_t lastDHTAnnounceTime) { m_LastDHTAnnounceTime = lastDHTAnnounceTime; }
		bool AddConnection (std::shared_ptr<PeerConnection> conn);
		void RemoveConnection (std::shared_ptr<PeerConnection> conn);
		std::list<std::shared_ptr<PeerConnection>> GetConnections ();
		bool IsConnectedToPeer (const i2p::data::IdentHash & peer);

		// Peer-candidate table. Service-thread only, never persisted and never
		// logged with wire operands. PEX and DHT candidates are remembered until
		// they are dialed, expire, or exhaust their failure budget instead of
		// being dialed once and forgotten.
		//
		// AddPeerCandidate rejects a zero identity, an already connected peer
		// and a duplicate (an existing entry keeps its origin, age and failure
		// history). At MAX_PEER_CANDIDATES it expires dead/old entries and then
		// evicts one; it fails only if nothing can be evicted. A private,
		// stopped or removed torrent never accepts candidates.
		bool AddPeerCandidate (const i2p::data::IdentHash & peer, PeerConnectionOrigin origin, uint64_t nowSeconds);
		// Charges the per-origin fixed window (100 PEX and 100 DHT candidates
		// per 60 s, tracked independently) and returns false when `count` does
		// not fit. Tracker and incoming origins are not peer-supplied gossip and
		// are unbudgeted. count == 0 always succeeds.
		bool AdmitPeerCandidates (PeerConnectionOrigin origin, size_t count, uint64_t nowSeconds);
		// Candidates that are due now (see PeerCandidatePolicy::IsUsable),
		// excluding peers that are already connected. Order is insertion order,
		// which the caller may shuffle; at most `max` entries are returned.
		std::vector<i2p::data::IdentHash> GetPeerCandidatesToConnect (uint64_t nowSeconds, size_t max) const;
		void MarkPeerCandidateAttempt (const i2p::data::IdentHash & peer, uint64_t nowSeconds);
		// Removes the entry: the connection table owns the peer from then on.
		void MarkPeerCandidateConnected (const i2p::data::IdentHash & peer);
		// Records a failed dial (backoff 30 s doubling to 900 s). The entry is
		// kept after PEER_CANDIDATE_MAX_FAILURES so a re-advertisement cannot
		// reset the history; it is then never dialable and is dropped by
		// expiration or eviction.
		void MarkPeerCandidateFailed (const i2p::data::IdentHash & peer, uint64_t nowSeconds);
		// Drops candidates past their per-origin TTL and permanently failed
		// ones. Driven by the periodic status tick.
		void ExpirePeerCandidates (uint64_t nowSeconds);
		void ClearPeerCandidates ();
		size_t GetNumPeerCandidates () const { return m_PeerCandidates.size (); }
		// Origin recorded when the candidate was admitted (PEX or DHT). Returns
		// PEX for an unknown hash so a dialer cannot invent tracker authority.
		PeerConnectionOrigin GetPeerCandidateOrigin (const i2p::data::IdentHash & peer) const;

		// Service-thread connection accounting. Outbound dials in progress are
		// accounted for by the tunnel, which also enforces the global limit.
		bool HasConnectionRoom () const { return m_Connections.size () < MAX_PEERS_PER_TORRENT; }
		// Closes one established, idle peer to make room for a new one. Peers
		// that transfer data or are interested in either direction are never
		// rotated; eviction uses the posted Close() path so handler lifetime
		// stays owned by the service thread.
		bool EvictIdleConnection ();

		// Bounded local data verification. markAllPieces claims every piece
		// (used when final-named data already exists); otherwise only pieces
		// that are already complete and unverified become pending. The pass is
		// driven one piece at a time by the tunnel's storage service.
		void RequestLocalVerification (bool markAllPieces, bool includeIncomplete = false);
		// Mark every piece whose fragments are all backed by an existing file
		// (final or .part). Used on restart when only part of a multi-file
		// torrent is present, so intact data is not downloaded again.
		size_t RequestLocalVerificationForExistingFiles (const std::vector<bool> & presentFiles);
		bool IsLocalVerificationRequested () const
		{
			return m_LocalVerificationRequested.load (std::memory_order_acquire);
		}
		// True only for the caller that starts the single verification chain.
		bool BeginLocalVerification ();
		void EndLocalVerification () { m_LocalVerificationActive.store (false, std::memory_order_release); }
		// Service thread: next complete piece that still needs hashing, or -1.
		// Clears the request flag when the whole set has been walked.
		int GetNextPieceToVerify ();
		// Service thread: clears the pending flag; a mismatch has already been
		// invalidated by the caller.
		void FinishPieceVerification (size_t index);

		uint64_t GetNextTrackerRequestTime (size_t trackerID) const;
		void SetNextTrackerRequestTime (size_t trackerID, uint64_t ts);
		size_t GetUploaded () const { return m_Uploaded; }
		void AddUploaded (size_t add) { m_Uploaded += add; }
		size_t GetDownloaded () const { return m_Downloaded; }
		void AddDownloaded (size_t add) { m_Downloaded += add; }

		void SaveTorrentResumeFile ();
		// Irreversible instance tombstone. IO already running may finish, but
		// the single disk queue drains it before removal; later jobs must stop.
		// Also purges the peer-candidate table.
		void MarkRemoved ();
		bool IsRemoved () const { return m_Removed.load (std::memory_order_acquire); }
		using ResumePersistence = std::function<bool (std::span<const uint8_t>, bool remove)>;
		void SetResumePersistence (ResumePersistence persistence) { m_ResumePersistence = std::move (persistence); }

		void StartCountingPeers ();
		void ApplyPeerRemoteBitfield (const boost::dynamic_bitset<> & peerRemoteBitfield);
		bool HasIncompletePieces (
			const boost::dynamic_bitset<> & peerRemoteBitfield) const; // if remote bitfie;d has incomplete pieces

		uint64_t GetDownloadRate ();
		uint64_t GetUploadRate ();
		int GetNumDownloadingFromPeers ();
		int GetNumUploadingToPeers ();

		int GetNumSeeders (size_t trackerID) const
		{
			return (trackerID < m_TrackerStats.size ()) ? std::get<3> (m_TrackerStats[trackerID]) : 0;
		}
		int GetNumLeechers (size_t trackerID) const
		{
			return (trackerID < m_TrackerStats.size ()) ? std::get<4> (m_TrackerStats[trackerID]) : 0;
		}
		int GetNumPeers (size_t trackerID) const
		{
			return (trackerID < m_TrackerStats.size ()) ? std::get<0> (m_TrackerStats[trackerID]).size () : 0;
		}
		uint64_t GetLastTrackerUpdateTime (size_t trackerID) const
		{
			return (trackerID < m_TrackerStats.size ()) ? std::get<5> (m_TrackerStats[trackerID]) : 0;
		}
		std::string GetTrackerError (size_t trackerID) const
		{
			return (trackerID < m_TrackerStats.size ()) ? std::get<6> (m_TrackerStats[trackerID]) : "";
		}
		void SetTrackerError (size_t trackerID, std::string_view error);
		// Records a transport/parse/tracker-level failure and schedules the next
		// attempt on the internal retry ladder: MIN_TRACKER_REQUESTS_INTERVAL
		// for the first failure, doubling up to MAX_TRACKER_FAILURE_BACKOFF
		// (30 min). The tracker-advertised interval is left untouched, so a
		// retry is never delayed to the 15-minute announce floor.
		void ReportTrackerFailure (size_t trackerID, std::string_view reason);
		// Drops tracker peers after the tracker has not produced a successful
		// response for max(2*interval, 30 min), capped at two hours.
		void ExpireTrackerPeers (uint64_t nowEpoch);

	private:
		Torrent ();
		void InitPeerID ();
		size_t ParsePieces (std::string_view buf);
		size_t ParsePeers (size_t trackerID, std::string_view buf);
		size_t ParseFiles (std::string_view buf);
		static std::string AdjustName (
			std::string_view name); // name field from torrent file before to become file path
		void CheckTrackerStatsSize (size_t trackerID);
		void RebuildPiecePriorities ();
		// Candidate-table helpers (service thread only).
		PeerCandidate * FindPeerCandidate (const i2p::data::IdentHash & peer);
		void RemovePeerCandidate (const i2p::data::IdentHash & peer);
		bool EvictPeerCandidate ();
		bool IsPeerConnected (const i2p::data::IdentHash & peer) const;

	private:
		ResumePersistence m_ResumePersistence;
		std::atomic<bool> m_Removed { false };
		std::string m_Name, m_Announce;
		std::string m_MetaComment, m_CreatedBy;
		// Raw top-level bencode values other than announce/announce-list/info.
		// Bounded so a hostile metainfo cannot make a rewrite allocate freely.
		std::vector<std::pair<std::string, std::string>> m_TopLevelExtras;
		bool m_MetaInfoComplete = true;
		PeerID m_PeerID { };
		TrackerTiers m_TrackerTiers;
		bool m_HasExplicitTrackers = false;
		bool m_IsPrivate = false;
		TorrentCommentStore m_Comments;
		// Comment-entry admission window (service thread only): bounds how many
		// new peer-supplied entries this torrent stores per minute.
		CommentAdmissionWindow m_CommentAdmission;
		int m_AnnounceTrackerID; // -1 not set
		std::filesystem::path m_FullPath;
		size_t m_Length, m_PieceLength;
		std::vector<uint8_t> m_Info; // for BEP9
		InfoHash m_InfoHash; // SHA1
		std::vector<Piece> m_Pieces;
		std::vector<uint8_t> m_FilePriorities, m_PiecePriorities;
		std::vector<TrackerStats> m_TrackerStats;
		std::unordered_map<i2p::data::IdentHash, std::weak_ptr<PeerConnection>>
			m_Connections; // remote ident hash -> connection
		// Bounded best-effort corrupt-peer counters (service thread only).
		std::unordered_map<i2p::data::IdentHash, uint32_t> m_PeerPieceFailures;
		// Bounded PEX/DHT candidate table and its per-origin admission windows
		// (service thread only; see PeerCandidatePolicy).
		std::vector<PeerCandidate> m_PeerCandidates;
		PeerCandidateWindow m_PexCandidateWindow;
		PeerCandidateWindow m_DhtCandidateWindow;
		std::map<size_t, std::string> m_TrackerIDs; // bounded opaque tracker ids
		std::atomic<bool> m_IsComplete;
		bool m_IsStopped, m_IsSingleFile;
		std::list<std::shared_ptr<TorrentFile>> m_Files;
		size_t m_Uploaded, m_Downloaded;
		uint64_t m_NextUpdateStatusTime, m_NextReconnectTime, m_NextDHTUpdateTime,
			m_LastDHTAnnounceTime; // in monotonic seconds
		std::atomic<TorrentError> m_Error;
		// Local verification state is only mutated on the tunnel service
		// thread. The atomics make the status-timer safety net cheap.
		size_t m_VerifyCursor = 0;
		std::atomic<bool> m_LocalVerificationRequested { false };
		std::atomic<bool> m_LocalVerificationActive { false };
	};

	class TorrentsTunnel;
	class PeerConnection : public i2p::client::I2PServiceHandler, public std::enable_shared_from_this<PeerConnection>
	{
		using ExtendedMessageHandler = void (PeerConnection::*) (const uint8_t * buf, size_t len);

	public:
		using PeerID = std::array<uint8_t, 20>;

		PeerConnection (
			std::shared_ptr<i2p::client::I2PService> owner, std::shared_ptr<i2p::stream::Stream> stream); // incoming
		PeerConnection (std::shared_ptr<i2p::client::I2PService> owner, std::shared_ptr<i2p::stream::Stream> stream,
			std::shared_ptr<Torrent> torrent, PeerConnectionOrigin origin = ePeerConnectionOriginTracker); // outgoing
		~PeerConnection ();

		void Connect ();
		void Close ();
		void ReceiveHandshake ();
		void CheckKeepAlive (uint64_t ts);
		void RefreshComments ();
		void RefreshFileSelection ();

		bool IsEstablished () const { return m_IsEstablished; };
		bool IsPieceAvailable (size_t ind) const;
		std::shared_ptr<i2p::stream::Stream> GetStream () const { return m_Stream; }
		std::shared_ptr<Torrent> GetTorrent () const { return m_Torrent; }
		int GetLastRequestedPieceIndex () const { return m_LastRequestedPieceIndex; }
		bool IsEstablishedAndIdle () const
		{
			return m_IsEstablished && !IsDownloading () && !IsUploading () && !m_IsInterested && !m_IsRemoteInterested;
		}
		int ResetSuggestedPieceIndex ()
		{
			auto index = m_SuggestedPieceIndex;
			m_SuggestedPieceIndex = -1;
			return index;
		}
		const boost::dynamic_bitset<> & GetRemoteBitfield () const { return m_RemoteBitfield; }
		// An empty bitfield is `.all()` too, so `.all()` alone cannot mean "the
		// remote is a seed". The flag is set by bitfield/have_all/have_none
		// handling and identifies "the remote advertised a set"; the size check
		// keeps a zero-piece bitset from counting as complete.
		bool IsRemoteBitfieldReceived () const { return m_RemoteBitfieldReceived; }
		bool IsRemoteSeed () const
		{
			return m_RemoteBitfieldReceived && m_RemoteBitfield.size () != 0 && m_RemoteBitfield.all ();
		}
		const PeerID & GetRemotePeerID () const { return m_RemotePeerID; }
		std::string_view GetRemoteName () const { return m_RemoteName; }
		std::optional<i2p::data::IdentHash> GetRemoteIdentHash () const;
		PeerConnectionOrigin GetOrigin () const { return m_Origin; }

		// stats
		void ResetStats ();
		void TorrentComplete ();
		uint64_t GetDownloadRate () const { return m_DownloadRate; }
		uint64_t GetUploadRate () const { return m_UploadRate; }
		bool IsDownloading () const { return m_IsInterested && !m_IsChoked; }
		bool IsUploading () const { return m_IsRemoteInterested && !m_IsRemoteChoked; }
		bool IsInterested () const { return m_IsInterested; }
		bool IsRemoteInterested () const { return m_IsRemoteInterested; }
		bool IsChoked () const { return m_IsChoked; }
		bool IsRemoteChoked () const { return m_IsRemoteChoked; }
		size_t GetDownloaded () const { return m_Downloaded; }
		size_t GetUploaded () const { return m_Uploaded; }

	private:
		void Terminate ();
		void ScheduleHandshakeReceiveTimer ();
		std::shared_ptr<TorrentsTunnel> GetTorrentsTunnel () const;

		void WriteToStream (const uint8_t * buf, size_t len);
		void StreamReceive ();
		void HandleStreamReceive (const boost::system::error_code & ecode, size_t bytes_transferred);
		void HandleReceived ();
		size_t HandleNextMsg (size_t offset);

		size_t HandleHandshakeMsg ();
		void SendHandshakeMsg ();

		void HandleHaveMsg (const uint8_t * buf, size_t len);
		void SendHaveMsg (uint32_t index);
		void HandleBitfieldMsg (const uint8_t * buf, size_t len);
		void SendBitfieldMsg (const uint8_t * bitfield, size_t bitfieldLen);
		void HandleHaveAllMsg ();
		void SendHaveAllMsg ();
		void HandleHaveNoneMsg ();
		void SendHaveNoneMsg ();
		void HandlePieceMsg (const uint8_t * buf, size_t len);
		void SendPieceMsg (uint32_t index, uint32_t offset, const uint8_t * data, size_t len);
		void HandleRejectRequestMsg (const uint8_t * buf, size_t len);
		void SendRejectRequestMsg (uint32_t index, uint32_t offset, uint32_t len);
		void HandleRequestMsg (const uint8_t * buf, size_t len);
		void HandleCancelMsg (const uint8_t * buf, size_t len);
		void SendRequestMsg (uint32_t index, uint32_t offset, uint32_t len);
		size_t FillRequestMsg (uint8_t * buf, uint32_t index, uint32_t offset, uint32_t len);
		void SendInterestedMsg ();
		void SendNotinterestedMsg ();
		void SendUnchokeMsg ();
		void SendChokeMsg ();
		void HandleChokeMsg ();
		void HandleSuggestPieceMsg (const uint8_t * buf, size_t len);
		void HandleAllowedFastMsg (const uint8_t * buf, size_t len);
		void HandleExtendedMsg (const uint8_t * buf, size_t len);
		void SendExtendedMsg (uint8_t extendedMsgID = 0, std::string_view payload = "", std::string_view data = "");
		void AddExtendedMsgHandler (std::string_view extensionName, int64_t msgID);
		void HandleUtMetadataExtension (const uint8_t * buf, size_t len); // BEP9
		void RequestUtMetadata (); // BEP9
		void HandleCommentExtension (const uint8_t * buf, size_t len);
		void RequestComments ();
		void HandleI2PPEXExtension (const uint8_t * buf, size_t len); // BEP11
		void NotifyPEXPeers (); // BEP11
		void HandleI2PDHTExtension (const uint8_t * buf, size_t len); // BEP5
		void SendDHTPortAdvertisement ();

		std::optional<RequestedBlock> GetNextBlockToRequest ();
		bool RequestNextBlocks ();
		bool SendRequestedBlock (const RequestedBlock & requestedBlock);
		void ReleaseOutgoingRequest (uint32_t index, uint32_t offset);
		void ReleaseOutgoingRequests ();
		// Releases every outbound request older than PEER_REQUEST_TIMEOUT and
		// re-arms the scheduler. Never touches the incoming/upload queue.
		bool ReleaseExpiredOutgoingRequests (uint64_t now);
		// Halves/zeroes a direction's reported rate after idle windows.
		void UpdateIdleRates (uint64_t nowSeconds);

	private:
		std::shared_ptr<i2p::stream::Stream> m_Stream;
		PeerConnectionOrigin m_Origin;
		uint8_t m_ReceiveBuffer[PEER_CONNECTION_RECEIVE_BUFFER_SIZE];
		size_t m_ReceiveBufferOffset, m_NextMsgLength;
		std::shared_ptr<Torrent> m_Torrent;
		PeerID m_RemotePeerID;
		std::string m_RemoteName; // from BEP10
		size_t m_MaxNumRequests; // min or from BEP10
		boost::dynamic_bitset<> m_RemoteBitfield;
		// Distinguishes a received (even empty) bitfield from "nothing yet", so
		// an empty bitset is not mistaken for a seed.
		bool m_RemoteBitfieldReceived = false;
		bool m_IsHandshakeSent, m_IsEstablished, m_IsChoked, m_IsRemoteChoked, m_IsInterested, m_IsRemoteInterested;
		uint64_t m_LastReceiveTime, m_LastSendTime; // monotonic seconds
		size_t m_NumRequests, m_NumPieces; // outgoing
		struct OutgoingRequest
		{
			uint32_t index;
			uint32_t offset;
			uint32_t length;
			uint64_t epoch;
			uint64_t requestedAt; // monotonic seconds, for PEER_REQUEST_TIMEOUT
		};
		std::list<OutgoingRequest> m_OutgoingRequests;
		std::list<RequestedBlock> m_IncomingRequestsQueue;
		int m_LastRequestedPieceIndex;
		std::unique_ptr<boost::asio::steady_timer> m_HandshakeReceiveTimer;
		// BEP10
		std::unordered_map<uint8_t, PeerConnection::ExtendedMessageHandler> m_ExtendedMessageHandlers;
		// Only the first well-formed BEP10 handshake negotiates and triggers
		// extensions; a repeated ID-0 frame must not re-run the trigger block.
		bool m_ExtendedHandshakeReceived = false;
		uint8_t m_RemoteMsgIDUtMetadata, m_RemoteMsgIDI2PPEX, m_RemoteMsgIDI2PDHT;
		// ut_comment: peer capability does not imply local consent.
		uint8_t m_RemoteMsgIDComment = 0;
		uint64_t m_CommentWindow = 0, m_LastCommentRequest = 0, m_LastCommentReply = 0;
		// Store revision observed at the last request: a local mutation may ask
		// for an immediate refresh, an unchanged revision keeps the 600 s
		// cadence.
		uint64_t m_LastCommentRequestRevision = 0;
		size_t m_CommentBytes = 0, m_CommentMessages = 0;
		// New entries this connection admitted in the current m_CommentWindow.
		// It is rolled with that window and capped per connection so a peer
		// cannot refill the store by reconnecting while the torrent-wide budget
		// keeps its own limit.
		CommentAdmissionWindow m_CommentEntryWindow;
		// BEP9
		size_t m_RemoteMetadataSize;
		std::vector<uint8_t> m_RemoteMetadata;
		// BEP6
		bool m_IsFast;
		int m_SuggestedPieceIndex;
		// BEP11: bounded incremental announcement bookkeeping.
		std::unordered_set<i2p::data::IdentHash> m_SentPEXPeers;
		uint64_t m_LastPEXTime = 0; // monotonic seconds
		uint64_t m_PexReceiveWindow = 0;
		size_t m_PexReceiveMessages = 0;
		size_t m_PexReceivePeers = 0;
		uint64_t m_LastDHTHintTime = 0;
		uint64_t m_LastDHTAdvertisementTime = 0;
		// stats
		uint64_t m_DownloadRate, m_UploadRate; // B/sec
		uint64_t m_LastBlockDownloadTimestamp, m_LastBlockUploadTimestamp; // monotonic milliseconds
		size_t m_ReceivedSinceLastTimestamp, m_SentSinceLastTimestamp; // bytes
		size_t m_Downloaded, m_Uploaded; // bytes
	};
}
}

#endif // NO_TORRENTS

#endif

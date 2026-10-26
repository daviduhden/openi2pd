/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: main.
 *
 * Implements the i2pd-tui application: a tabbed terminal interface that polls
 * the router over local IPC and renders status, peers, tunnels, torrents,
 * netDb, logs, services, destinations, configuration and connections. It handles resize
 * and key events, editable prompts, UTF-8-aware truncation and periodic
 * background refreshes while retaining the last complete snapshot.
 *
 */

#include "TuiClient.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <chrono>
#include <climits>
#include <clocale>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <cwchar>
#include <deque>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <wchar.h>

#ifndef _WIN32
#include <signal.h>
#else
#include <windows.h>
#endif

#include "I18N.h"
#include "Log.h"
#include "Terminal.h"
#include "Theme.h"
#include "TuiProtocol.h"
#include "version.h"

namespace
{
using namespace std::chrono_literals;
using i2p::tui::Theme;
using i2p::tui::ThemeRole;
namespace terminal = i2p::tui::terminal;

volatile std::sig_atomic_t g_Resize = 0;
volatile std::sig_atomic_t g_Stop = 0;
#ifdef _WIN32
// Console close, logoff, shutdown, Ctrl-C and Ctrl-Break arrive on a helper
// thread. Only the stop flag is touched; the main loop restores the console
// through the normal shutdown path.
BOOL WINAPI HandleConsoleEvent (DWORD type)
{
	g_Stop = 1;
	return TRUE;
}
#endif

#ifndef _WIN32
void HandleResize (int)
{
	g_Resize = 1;
}
void HandleStop (int)
{
	g_Stop = 1;
}
#endif

enum class Tab
{
	Status = 0,
	Peers,
	Tunnels,
	Torrents,
	NetDb,
	Logs,
	Services,
	Help,
	Destinations,
	Config,
	Connections
};

// The original 1..9 bindings are preserved; Connections is appended as 0 and
// Services uses v so existing muscle memory/help text keeps working.
// Tab/arrow navigation visits all eleven views in this order.
constexpr std::array<std::string_view, 11> TAB_NAMES = { "Status", "Peers", "Tunnels", "Torrents", "NetDb", "Logs",
	"Services", "Help", "Destinations", "Config", "Connections" };
constexpr std::array<std::string_view, 11> TAB_SHORT_NAMES
	= { "Status", "Peers", "Tunnels", "Torrents", "NetDb", "Logs", "Svcs", "Help", "Dest", "Config", "Conn" };
constexpr std::array<std::string_view, 11> TAB_KEYS = { "1", "2", "3", "4", "5", "6", "v", "7", "8", "9", "0" };

enum class NetDbView
{
	Routers = 0,
	LeaseSets
};

enum class TorrentSort
{
	Name = 0,
	Status,
	Progress,
	Down,
	Up,
	Peers
};

enum class TorrentDetailTab
{
	General = 0,
	Files,
	Peers,
	Trackers,
	Comments
};

enum class PromptKind
{
	None = 0,
	Filter,
	LogFilter,
	AddMagnet,
	AddFile,
	CreateTorrent,
	CreateTorrentAdvanced,
	AddTracker,
	RemoveTracker,
	FilePriority,
	CommentsToggle,
	CommentAdd,
	CommentHide,
	CommentUnhide,
	ConfirmCommentHide,
	ConfirmCommentUnhide,
	TorrentMetaComment,
	TorrentMetaCreatedBy,
	ConfirmRemove,
	ConfirmRemoveFiles,
	ConfirmRemoveAll,
	ConfirmRemoveAllFiles,
	ConfirmTransit,
	ConfirmOnline,
	ConfirmReseed,
	ConfirmReloadTunnels,
	ConfirmShutdownGraceful,
	ConfirmShutdownImmediate,
	Bandwidth,
	Share,
	AddressAdd,
	AddressReplace,
	AddressRemove,
	AddressBlock,
	AddressUnblock,
	AddressSubAdd,
	AddressSubRemove,
	NetDbBan,
	NetDbUnban,
	ConfirmTunnelStart,
	ConfirmTunnelStop
};

struct PeerRow
{
	std::string hash;
	long attempts = 0;
	long sessions = 0;
	long transports = 0;
	std::string transportNames;
	bool highBandwidth = false;
};

struct NetDbRouterRow
{
	std::string hash;
	long version = 0;
	long caps = 0;
	bool floodfill = false;
	bool v4 = false;
	bool v6 = false;
	bool reachable = false;
	uint64_t timestamp = 0;
};

struct ServiceRow
{
	std::string kind;
	std::string name;
	std::string endpoint;
	std::string address;
	bool enabled = false;
};

// One configured client or server tunnel from the bounded SERVICES snapshot.
// `name` is the unique service name the typed TUNNEL-START/TUNNEL-STOP verbs
// address; `destination` is a public label only (no keys, no identity file).
struct TunnelServiceRow
{
	std::string name;
	std::string kind;
	std::string endpoint;
	std::string destination;
	bool running = false;
};

struct NetDbLeaseSetRow
{
	std::string hash;
	long storeType = 0;
	long encryptionType = 0;
	uint64_t expiration = 0;
	uint64_t earliestLease = 0;
	bool valid = false;
	bool expired = false;
};

struct TunnelRow
{
	std::string id;
	std::string direction;
	std::string state;
	std::string hops;
	std::string created;
};

struct LogRow
{
	uint64_t sequence = 0;
	long timestamp = 0;
	int level = 0;
	std::string text;
};

struct TorrentRow
{
	int id = -1;
	std::string name;
	std::string status;
	std::string error;
	std::string hash;
	std::string magnet;
	std::string metaComment;
	std::string createdBy;
	uint64_t total = 0;
	uint64_t wanted = 0;
	uint64_t have = 0;
	uint64_t downloaded = 0;
	uint64_t uploaded = 0;
	uint64_t downRate = 0;
	uint64_t upRate = 0;
	int peers = 0;
	int seeders = 0;
	int leechers = 0;
	double percent = -1.0;
	bool privateTorrent = false;
	std::optional<uint64_t> eta;
};

struct TorrentFileRow
{
	long index = 0;
	int priority = 1;
	std::string name;
	uint64_t length = 0;
	uint64_t completed = 0;
};

struct TorrentPeerRow
{
	std::string ident;
	std::string client;
	std::string flags;
	double progress = 0.0;
	uint64_t downRate = 0;
	uint64_t upRate = 0;
};

struct TorrentTrackerRow
{
	std::string announce;
	int tier = -1;
	int peers = 0;
	int seeders = 0;
	int leechers = 0;
	std::string lastResult;
	uint64_t lastAnnounce = 0;
	uint64_t nextAnnounce = 0;
};

struct TorrentDetail
{
	bool loaded = false;
	TorrentRow info;
	std::vector<TorrentFileRow> files;
	std::vector<TorrentPeerRow> peers;
	std::vector<TorrentTrackerRow> trackers;
	std::map<std::string, std::string> comments;
};

struct TorrentTunnelRow
{
	int index = -1;
	std::string name;
	int torrents = 0;
};

struct TorrentSessionInfo
{
	uint64_t downloaded = 0;
	uint64_t uploaded = 0;
	int active = 0;
	int paused = 0;
	uint64_t creationJob = 0;
	uint64_t creationBytesDone = 0;
	uint64_t creationTotalBytes = 0;
	uint64_t creationFilesDone = 0;
	uint64_t creationTotalFiles = 0;
	bool creationPending = false;
	bool creationSuccess = false;
	int creationTorrentId = 0;
	std::string creationMessage;
};

struct TorrentDhtInfo
{
	bool supported = false;
	int numNodes = 0;
	int numBuckets = 0;
	int numTorrents = 0;
	int numHeardFrom = 0;
	int numFailedPeers = 0;
};

struct ConfigOptionRow
{
	std::string name;
	std::string type;
	std::string value;
};

struct TorrentLayout
{
	bool showSize = false;
	bool showDown = false;
	bool showUp = false;
	bool showPeers = false;
	bool showRatio = false;
	bool showError = false;
	int name = 0;
};

constexpr int TORRENT_STATUS_COLS = 9;
constexpr int TORRENT_SIZE_COLS = 9;
constexpr int TORRENT_PROGRESS_COLS = 7;
constexpr int TORRENT_DOWN_COLS = 9;
constexpr int TORRENT_UP_COLS = 9;
constexpr int TORRENT_PEERS_COLS = 11;
constexpr int TORRENT_RATIO_COLS = 11;
constexpr int TORRENT_ERROR_COLS = 14;

std::vector<std::string_view> Split (std::string_view value, char separator)
{
	std::vector<std::string_view> parts;
	size_t start = 0;
	while (true)
	{
		auto pos = value.find (separator, start);
		if (pos == std::string_view::npos)
		{
			parts.push_back (value.substr (start));
			break;
		}
		parts.push_back (value.substr (start, pos - start));
		start = pos + 1;
	}
	return parts;
}

std::string Trim (std::string_view value)
{
	while (!value.empty () && (value.front () == ' ' || value.front () == '\t')) value.remove_prefix (1);
	while (!value.empty () && (value.back () == ' ' || value.back () == '\t' || value.back () == '\r'))
		value.remove_suffix (1);
	return std::string (value);
}

// Count Java-style UTF-16 units for the shared ut_comment field limits.
// Invalid UTF-8 (overlong, surrogate, truncated or above U+10FFFF) fails closed.
size_t Utf16Units (std::string_view text)
{
	size_t units = 0;
	for (size_t i = 0; i < text.size ();)
	{
		auto ch = static_cast<unsigned char> (text[i++]);
		uint32_t cp = ch;
		size_t extra = 0;
		if (ch < 0x80)
		{
			if (ch < 0x20 || ch == 0x7f) return static_cast<size_t> (-1);
		}
		else if (ch >= 0xC2 && ch <= 0xDF)
		{
			cp = ch & 0x1F;
			extra = 1;
		}
		else if (ch >= 0xE0 && ch <= 0xEF)
		{
			cp = ch & 0x0F;
			extra = 2;
		}
		else if (ch >= 0xF0 && ch <= 0xF4)
		{
			cp = ch & 0x07;
			extra = 3;
		}
		else
			return static_cast<size_t> (-1);
		if (extra > text.size () - i) return static_cast<size_t> (-1);
		for (size_t n = 0; n < extra; ++n)
		{
			auto next = static_cast<unsigned char> (text[i++]);
			if ((next & 0xC0) != 0x80) return static_cast<size_t> (-1);
			cp = (cp << 6) | (next & 0x3F);
		}
		if ((extra == 1 && cp < 0x80) || (extra == 2 && cp < 0x800) || (extra == 3 && cp < 0x10000) || cp > 0x10FFFF
			|| (cp >= 0xD800 && cp <= 0xDFFF) || cp < 0x20 || (cp >= 0x7F && cp <= 0x9F))
			return static_cast<size_t> (-1);
		units += cp >= 0x10000 ? 2 : 1;
	}
	return units;
}

long ToLong (std::string_view value)
{
	long result = 0;
	for (char ch : value)
	{
		if (ch < '0' || ch > '9') return 0;
		if (result > (LONG_MAX - (ch - '0')) / 10) return 0;
		result = result * 10 + (ch - '0');
	}
	return result;
}

uint64_t ToU64 (std::string_view value, uint64_t fallback = 0)
{
	if (value.empty ()) return fallback;
	uint64_t result = 0;
	auto parsed = std::from_chars (value.data (), value.data () + value.size (), result);
	if (parsed.ec != std::errc () || parsed.ptr != value.data () + value.size ()) return fallback;
	return result;
}

int ToInt (std::string_view value, int fallback = 0)
{
	if (value.empty ()) return fallback;
	int64_t result = 0;
	auto parsed = std::from_chars (value.data (), value.data () + value.size (), result);
	if (parsed.ec != std::errc () || parsed.ptr != value.data () + value.size ()) return fallback;
	if (result < 0) return fallback;
	if (result > INT_MAX) return INT_MAX;
	return static_cast<int> (result);
}

double ToDouble (std::string_view value, double fallback = 0.0)
{
	if (value.empty ()) return fallback;
#ifdef _WIN32
	// Some MSVC/MinGW standard libraries still lack floating-point
	// from_chars.  The TUI runs with the C locale explicitly selected, so
	// strtod has the same decimal-point behavior here.
	std::string copy (value);
	char * end = nullptr;
	const double result = std::strtod (copy.c_str (), &end);
	if (end != copy.c_str () + copy.size ()) return fallback;
	return result;
#else
	double result = 0.0;
	auto parsed = std::from_chars (value.data (), value.data () + value.size (), result);
	if (parsed.ec != std::errc () || parsed.ptr != value.data () + value.size ()) return fallback;
	return result;
#endif
}

std::string ToLowerAscii (std::string_view value)
{
	std::string result (value);
	for (char & ch : result)
		if (ch >= 'A' && ch <= 'Z') ch = static_cast<char> (ch - 'A' + 'a');
	return result;
}

// A confirmation prompt is rendered from a translated catalog, so the
// advertised affirmative letter must be accepted too. Spanish uses s/sí and
// Russian uses д/да; Chinese keeps y. Byte comparison is safe because these
// tokens are short and the input is already trimmed.
bool IsAffirmative (std::string_view input)
{
	const std::string answer = ToLowerAscii (Trim (input));
	// ToLowerAscii only folds ASCII, so the uppercase Cyrillic forms are
	// listed explicitly instead of relying on a locale-dependent tolower.
	return answer == "y" || answer == "yes" || answer == "s" || answer == "si" || answer == "sí"
		|| answer == "д" || answer == "да" || answer == "Д" || answer == "ДА" || answer == "是"
		|| answer == "好";
}

std::string TransportNamesFromMask (long mask)
{
	// These bits mirror RouterInfo::SupportedTransports in the router. The
	// TUI deliberately does not link libi2pd, so the numeric contract is
	// decoded locally and rendered as stable ASCII labels.
	std::string names;
	auto append = [&names] (const char * name)
	{
		if (!names.empty ()) names.push_back (',');
		names.append (name);
	};
	if (mask & 0x01) append ("NTCP2v4");
	if (mask & 0x02) append ("NTCP2v6");
	if (mask & 0x04) append ("SSU2v4");
	if (mask & 0x08) append ("SSU2v6");
	if (mask & 0x10) append ("Mesh");
	return names.empty () ? std::string ("-") : names;
}

std::string BoundUtf8 (std::string_view value, size_t limit)
{
	if (value.size () <= limit) return std::string (value);
	size_t cut = limit;
	while (cut > 0 && cut < value.size () && (static_cast<unsigned char> (value[cut]) & 0xC0) == 0x80) --cut;
	return std::string (value.substr (0, cut));
}

struct Utf8Character
{
	size_t length = 1;
	int width = 1;
};

#ifdef _WIN32
int CodePointWidth (std::uint32_t codePoint)
{
	// MSVC does not provide POSIX wcwidth().  Mirror the terminal backend's
	// conservative Unicode width table instead of treating UTF-8 byte length
	// as a terminal cell width.
	if ((codePoint >= 0x0300 && codePoint <= 0x036F) || (codePoint >= 0x1AB0 && codePoint <= 0x1AFF)
		|| (codePoint >= 0x1DC0 && codePoint <= 0x1DFF) || (codePoint >= 0x20D0 && codePoint <= 0x20FF)
		|| (codePoint >= 0xFE00 && codePoint <= 0xFE0F) || (codePoint >= 0xFE20 && codePoint <= 0xFE2F))
		return 0;
	if ((codePoint >= 0x1100 && codePoint <= 0x115F) || codePoint == 0x2329 || codePoint == 0x232A
		|| (codePoint >= 0x2E80 && codePoint <= 0xA4CF && codePoint != 0x303F)
		|| (codePoint >= 0xAC00 && codePoint <= 0xD7A3) || (codePoint >= 0xF900 && codePoint <= 0xFAFF)
		|| (codePoint >= 0xFE10 && codePoint <= 0xFE19) || (codePoint >= 0xFE30 && codePoint <= 0xFE6F)
		|| (codePoint >= 0xFF00 && codePoint <= 0xFF60) || (codePoint >= 0xFFE0 && codePoint <= 0xFFE6)
		|| (codePoint >= 0x1F300 && codePoint <= 0x1FAFF) || (codePoint >= 0x20000 && codePoint <= 0x3FFFD))
		return 2;
	return 1;
}
#endif

Utf8Character NextCharacter (std::string_view text, size_t offset)
{
	if (offset >= text.size ()) return { 1, 0 };
#ifdef _WIN32
	const auto first = static_cast<unsigned char> (text[offset]);
	if (first < 0x80) return { 1, CodePointWidth (first) };
	size_t length = 0;
	std::uint32_t codePoint = 0;
	if ((first & 0xE0) == 0xC0)
	{
		length = 2;
		codePoint = first & 0x1F;
	}
	else if ((first & 0xF0) == 0xE0)
	{
		length = 3;
		codePoint = first & 0x0F;
	}
	else if ((first & 0xF8) == 0xF0)
	{
		length = 4;
		codePoint = first & 0x07;
	}
	else
		return { 1, 1 };
	if (offset + length > text.size ()) return { 1, 1 };
	for (size_t i = 1; i < length; ++i)
	{
		const auto next = static_cast<unsigned char> (text[offset + i]);
		if ((next & 0xC0) != 0x80) return { 1, 1 };
		codePoint = (codePoint << 6) | (next & 0x3F);
	}
	return { length, CodePointWidth (codePoint) };
#else
	wchar_t character = 0;
	std::mbstate_t state { };
	auto consumed = std::mbrtowc (&character, text.data () + offset, text.size () - offset, &state);
	if (consumed == static_cast<size_t> (-1) || consumed == static_cast<size_t> (-2) || consumed == 0) return { 1, 1 };
	int width = ::wcwidth (character);
	if (width < 0) width = 1;
	return { consumed, width };
#endif
}

int DisplayWidth (std::string_view text)
{
	int width = 0;
	size_t offset = 0;
	while (offset < text.size ())
	{
		auto character = NextCharacter (text, offset);
		width += character.width;
		offset += character.length;
	}
	return width;
}

bool DecodeUtf8 (std::string_view text, size_t offset, std::uint32_t & codePoint, size_t & length)
{
	const auto first = static_cast<unsigned char> (text[offset]);
	if (first < 0x80)
	{
		codePoint = first;
		length = 1;
		return true;
	}
	if (first >= 0xC2 && first <= 0xDF)
	{
		length = 2;
		codePoint = first & 0x1F;
	}
	else if (first >= 0xE0 && first <= 0xEF)
	{
		length = 3;
		codePoint = first & 0x0F;
	}
	else if (first >= 0xF0 && first <= 0xF4)
	{
		length = 4;
		codePoint = first & 0x07;
	}
	else
		return false;
	if (offset + length > text.size ()) return false;
	for (size_t i = 1; i < length; ++i)
	{
		const auto next = static_cast<unsigned char> (text[offset + i]);
		if ((next & 0xC0) != 0x80) return false;
		codePoint = (codePoint << 6) | (next & 0x3F);
	}
	// Reject overlong encodings, UTF-16 surrogates and values above U+10FFFF.
	if ((length == 3 && codePoint < 0x800) || (length == 4 && codePoint < 0x10000)
		|| codePoint > 0x10FFFF || (codePoint >= 0xD800 && codePoint <= 0xDFFF))
		return false;
	return true;
}

std::string SanitizeText (std::string_view text)
{
	std::string result;
	result.reserve (text.size ());
	size_t offset = 0;
	while (offset < text.size ())
	{
		const auto byte = static_cast<unsigned char> (text[offset]);
		if (byte < 0x20 || byte == 0x7F)
		{
			// Newlines/tabs are layout, not data; other C0/DEL controls are
			// replaced so a peer cannot inject terminal state.
			result.push_back ((byte == '\n' || byte == '\r' || byte == '\t') ? ' ' : '?');
			++offset;
			continue;
		}
		std::uint32_t codePoint = 0;
		size_t length = 0;
		if (!DecodeUtf8 (text, offset, codePoint, length))
		{
			// Invalid or C1 byte: never forward a raw byte to ncursesw, which
			// can swallow the rest of the row in a UTF-8 locale.
			result.push_back ('?');
			++offset;
			continue;
		}
		const bool bidi
			= (codePoint >= 0x202A && codePoint <= 0x202E) || (codePoint >= 0x2066 && codePoint <= 0x2069);
		if (bidi)
		{
			offset += length;
			continue;
		}
		if (codePoint >= 0x80 && codePoint <= 0x9F)
		{
			result.push_back ('?');
			offset += length;
			continue;
		}
		result.append (text.substr (offset, length));
		offset += length;
	}
	return result;
}

// Comment owners and texts come from unauthenticated peers. On top of the
// C0/DEL replacement SanitizeText performs, drop the Unicode bidi embedding,
// override and isolate controls (U+202A..U+202E and U+2066..U+2069). They
// occupy no cells but reorder the surrounding rows, so a peer-supplied label
// could otherwise spoof which line a rating or the "yours" marker belongs to.
// Invalid UTF-8 sequences are passed through unchanged, exactly like
// SanitizeText, and never make the decoder skip more than one byte.
std::string SanitizeCommentText (std::string_view text)
{
	// SanitizeText now strips bidi controls and replaces invalid/C1 bytes for
	// every untrusted field, not only comments.
	return SanitizeText (text);
}

std::string TruncateUtf8 (std::string_view text, int maxWidth, bool ellipsis = true)
{
	if (maxWidth <= 0 || text.empty ()) return { };
	if (DisplayWidth (text) <= maxWidth) return std::string (text);

	std::string suffix;
	if (ellipsis && maxWidth >= 3) suffix = "...";
	int budget = maxWidth - DisplayWidth (suffix);
	if (budget < 0)
	{
		suffix.clear ();
		budget = maxWidth;
	}

	std::string result;
	int used = 0;
	size_t offset = 0;
	while (offset < text.size ())
	{
		auto character = NextCharacter (text, offset);
		if (used + character.width > budget) break;
		result.append (text.substr (offset, character.length));
		used += character.width;
		offset += character.length;
	}
	result += suffix;
	return result;
}

std::string FitUtf8 (std::string_view text, int columns, bool rightAlign = false)
{
	if (columns <= 0) return { };
	auto clean = SanitizeText (text);
	auto cut = TruncateUtf8 (clean, columns, true);
	int padding = columns - DisplayWidth (cut);
	if (padding < 0) padding = 0;
	if (rightAlign) return std::string (static_cast<size_t> (padding), ' ') + cut;
	return cut + std::string (static_cast<size_t> (padding), ' ');
}

std::string TailUtf8 (std::string_view text, int maxWidth)
{
	if (maxWidth <= 0 || text.empty ()) return { };
	struct Piece
	{
		size_t offset;
		int width;
	};
	std::vector<Piece> pieces;
	int total = 0;
	size_t offset = 0;
	while (offset < text.size ())
	{
		auto character = NextCharacter (text, offset);
		pieces.push_back ({ offset, character.width });
		total += character.width;
		offset += character.length;
	}
	size_t first = 0;
	while (first < pieces.size () && total > maxWidth)
	{
		total -= pieces[first].width;
		++first;
	}
	if (first >= pieces.size ()) return { };
	return std::string (text.substr (pieces[first].offset));
}

std::vector<std::string> WrapUtf8 (std::string_view text, int maxWidth)
{
	std::vector<std::string> lines;
	if (maxWidth <= 0) return lines;
	std::string line;
	int used = 0;
	size_t offset = 0;
	while (offset < text.size ())
	{
		auto character = NextCharacter (text, offset);
		if (used > 0 && used + character.width > maxWidth)
		{
			lines.push_back (line);
			line.clear ();
			used = 0;
			if (lines.size () >= 32) break;
		}
		line.append (text.substr (offset, character.length));
		used += character.width;
		offset += character.length;
	}
	if (!line.empty ()) lines.push_back (line);
	if (lines.empty ()) lines.emplace_back ();
	return lines;
}

void RemoveLastUtf8 (std::string & value)
{
	if (value.empty ()) return;
	value.pop_back ();
	while (!value.empty () && (static_cast<unsigned char> (value.back ()) & 0xC0) == 0x80) value.pop_back ();
}

std::string HumanBytes (uint64_t value)
{
	static constexpr std::array<const char *, 6> units = { "B", "KiB", "MiB", "GiB", "TiB", "PiB" };
	double amount = static_cast<double> (value);
	size_t unit = 0;
	while (amount >= 1024.0 && unit + 1 < units.size ())
	{
		amount /= 1024.0;
		++unit;
	}
	char buffer[64];
	snprintf (buffer, sizeof (buffer), "%.2f %s", amount, units[unit]);
	return buffer;
}

std::string HumanRate (uint64_t bytesPerSecond)
{
	return HumanBytes (bytesPerSecond) + "/s";
}

std::string HumanRateShort (uint64_t bytesPerSecond)
{
	static constexpr std::array<const char *, 6> units = { "B/s", "K/s", "M/s", "G/s", "T/s", "P/s" };
	double amount = static_cast<double> (bytesPerSecond);
	size_t unit = 0;
	while (amount >= 1024.0 && unit + 1 < units.size ())
	{
		amount /= 1024.0;
		++unit;
	}
	char buffer[32];
	if (amount >= 100.0)
		snprintf (buffer, sizeof (buffer), "%.0f%s", amount, units[unit]);
	else if (amount >= 10.0)
		snprintf (buffer, sizeof (buffer), "%.1f%s", amount, units[unit]);
	else
		snprintf (buffer, sizeof (buffer), "%.2f%s", amount, units[unit]);
	return buffer;
}

std::string HumanDuration (uint64_t seconds)
{
	uint64_t days = seconds / 86400;
	uint64_t hours = (seconds % 86400) / 3600;
	uint64_t minutes = (seconds % 3600) / 60;
	uint64_t secs = seconds % 60;
	char buffer[64];
	if (days)
		snprintf (buffer, sizeof (buffer), "%llud %02lluh", static_cast<unsigned long long> (days),
			static_cast<unsigned long long> (hours));
	else if (hours)
		snprintf (buffer, sizeof (buffer), "%lluh %02llum", static_cast<unsigned long long> (hours),
			static_cast<unsigned long long> (minutes));
	else if (minutes)
		snprintf (buffer, sizeof (buffer), "%llum %02llus", static_cast<unsigned long long> (minutes),
			static_cast<unsigned long long> (secs));
	else
		snprintf (buffer, sizeof (buffer), "%llus", static_cast<unsigned long long> (secs));
	return buffer;
}

std::string HumanUptime (long seconds)
{
	if (seconds < 0) seconds = 0;
	const long days = seconds / 86400;
	const long hours = (seconds % 86400) / 3600;
	const long minutes = (seconds % 3600) / 60;
	const long secs = seconds % 60;
	char buffer[64];
	if (days)
		snprintf (buffer, sizeof (buffer), "%ldd %02ld:%02ld:%02ld", days, hours, minutes, secs);
	else
		snprintf (buffer, sizeof (buffer), "%02ld:%02ld:%02ld", hours, minutes, secs);
	return buffer;
}

std::string Timestamp (long epoch)
{
	std::time_t time = static_cast<std::time_t> (epoch);
	std::tm tm { };
#ifdef _WIN32
	if (localtime_s (&tm, &time) != 0) return "--:--:--";
#else
	if (!localtime_r (&time, &tm)) return "--:--:--";
#endif
	char buffer[16];
	if (!strftime (buffer, sizeof (buffer), "%H:%M:%S", &tm)) return "--:--:--";
	return buffer;
}

std::string Timestamp64 (uint64_t epoch)
{
	if (!epoch || epoch > static_cast<uint64_t> (LONG_MAX)) return "--:--:--";
	return Timestamp (static_cast<long> (epoch));
}

std::string TimestampAged (uint64_t epoch)
{
	if (!epoch || epoch > static_cast<uint64_t> (LONG_MAX)) return "--";
	const std::time_t value = static_cast<std::time_t> (epoch);
	std::tm when { }, now { };
#ifdef _WIN32
	if (localtime_s (&when, &value) != 0) return "--";
	const std::time_t current = std::time (nullptr);
	if (localtime_s (&now, &current) != 0) return "--";
#else
	if (!localtime_r (&value, &when)) return "--";
	const std::time_t current = std::time (nullptr);
	if (!localtime_r (&current, &now)) return "--";
#endif
	char buffer[24];
	const char * format = when.tm_year == now.tm_year && when.tm_yday == now.tm_yday ? "%H:%M:%S" : "%m-%d %H:%M";
	if (!strftime (buffer, sizeof (buffer), format, &when)) return "--";
	return buffer;
}

std::string_view LevelName (int level)
{
	switch (level)
	{
	case eLogCritical: return "CRIT";
	case eLogError: return "ERROR";
	case eLogWarning: return "WARN";
	case eLogInfo: return "INFO";
	case eLogDebug: return "DEBUG";
	default: return "NONE";
	}
}

std::string_view DaemonLogLevelName (int level)
{
	switch (level)
	{
	case eLogNone: return "none";
	case eLogCritical: return "critical";
	case eLogError: return "error";
	case eLogWarning: return "warn";
	case eLogInfo: return "info";
	case eLogDebug: return "debug";
	default: return "unknown";
	}
}

int LevelAttribute (const Theme & theme, int level)
{
	switch (level)
	{
	case eLogCritical: return theme.Attributes (ThemeRole::LogCritical);
	case eLogError: return theme.Attributes (ThemeRole::LogError);
	case eLogWarning: return theme.Attributes (ThemeRole::LogWarning);
	case eLogDebug: return theme.Attributes (ThemeRole::LogDebug);
	case eLogInfo: return theme.Attributes (ThemeRole::LogInfo);
	default: return terminal::ATTR_NORMAL;
	}
}

std::map<std::string, std::string> ParseFieldMap (const std::string & payload)
{
	std::map<std::string, std::string> fields;
	size_t count = 0;
	for (auto line : Split (payload, '\n'))
	{
		if (line.empty ()) continue;
		if (++count > 8192) break;
		auto equals = line.find ('=');
		if (equals == std::string_view::npos || equals == 0) continue;
		auto name = line.substr (0, equals);
		if (name.size () > i2p::tui::MAX_FIELD_NAME) continue;
		fields[std::string (name)] = BoundUtf8 (i2p::tui::UnescapeValue (line.substr (equals + 1)), 8192);
	}
	return fields;
}

const std::string & MapValue (const std::map<std::string, std::string> & fields, std::string_view name)
{
	static const std::string empty;
	auto it = fields.find (std::string (name));
	return it == fields.end () ? empty : it->second;
}

bool SplitIndexedKey (std::string_view key, std::string_view prefix, long & index, std::string_view & field)
{
	if (!key.starts_with (prefix)) return false;
	auto rest = key.substr (prefix.size ());
	auto dot = rest.find ('.');
	if (dot == std::string_view::npos || dot == 0) return false;
	auto digits = rest.substr (0, dot);
	if (digits.size () > 9) return false;
	long value = 0;
	for (char ch : digits)
	{
		if (ch < '0' || ch > '9') return false;
		value = value * 10 + (ch - '0');
		if (value > INT_MAX) return false;
	}
	index = value;
	field = rest.substr (dot + 1);
	return !field.empty ();
}

void AssignTorrentField (TorrentRow & row, std::string_view field, const std::string & value)
{
	if (field == "id")
		row.id = ToInt (value, -1);
	else if (field == "name")
		row.name = BoundUtf8 (value, 4096);
	else if (field == "status")
		row.status = BoundUtf8 (value, 128);
	else if (field == "error")
		row.error = BoundUtf8 (value, 2048);
	else if (field == "hash")
		row.hash = BoundUtf8 (value, 256);
	else if (field == "magnet")
		row.magnet = BoundUtf8 (value, 2048);
	else if (field == "comment")
		row.metaComment = BoundUtf8 (value, 4096);
	else if (field == "createdBy")
		row.createdBy = BoundUtf8 (value, 1024);
	else if (field == "total")
		row.total = ToU64 (value);
	else if (field == "wanted")
		row.wanted = ToU64 (value);
	else if (field == "have")
		row.have = ToU64 (value);
	else if (field == "private")
		row.privateTorrent = value == "1";
	else if (field == "downloaded")
		row.downloaded = ToU64 (value);
	else if (field == "uploaded")
		row.uploaded = ToU64 (value);
	else if (field == "downRate")
		row.downRate = ToU64 (value);
	else if (field == "upRate")
		row.upRate = ToU64 (value);
	else if (field == "peers")
		row.peers = ToInt (value);
	else if (field == "seeders")
		row.seeders = ToInt (value);
	else if (field == "leechers")
		row.leechers = ToInt (value);
	else if (field == "percent")
		row.percent = ToDouble (value, row.percent);
	else if (field == "eta")
	{
		auto eta = ToU64 (value);
		if (eta)
			row.eta = eta;
		else
			row.eta.reset ();
	}
}

void FinalizeTorrent (TorrentRow & row)
{
	if (row.id < 0) row.id = -1;
	if (row.status.empty ()) row.status = "unknown";
	if (row.percent < 0.0)
	{
		const auto basis = row.wanted ? row.wanted : row.total;
		if (basis)
			row.percent = 100.0 * static_cast<double> (row.have) / static_cast<double> (basis);
		else
			row.percent = 0.0;
	}
	if (row.percent < 0.0) row.percent = 0.0;
	if (row.percent > 100.0) row.percent = 100.0;
	if (row.eta && !*row.eta) row.eta.reset ();
}

std::vector<TorrentRow> BuildTorrentRows (const std::map<std::string, std::string> & fields)
{
	std::map<long, TorrentRow> rows;
	for (const auto & [key, value] : fields)
	{
		long index = 0;
		std::string_view field;
		if (!SplitIndexedKey (key, "torrent.", index, field)) continue;
		auto it = rows.find (index);
		if (it == rows.end ())
		{
			if (rows.size () >= 1000) continue;
			it = rows.emplace (index, TorrentRow { }).first;
		}
		AssignTorrentField (it->second, field, value);
	}

	std::vector<TorrentRow> result;
	std::set<int> seen;
	for (auto & [index, row] : rows)
	{
		(void)index;
		if (row.id < 0) continue;
		if (!seen.insert (row.id).second) continue;
		FinalizeTorrent (row);
		result.push_back (std::move (row));
		if (result.size () >= 1000) break;
	}
	return result;
}

TorrentDetail BuildTorrentDetail (const std::map<std::string, std::string> & fields, int fallbackId)
{
	TorrentDetail detail;
	detail.info.id = -1;
	detail.info.percent = -1.0;
	std::map<long, TorrentFileRow> files;
	std::map<long, TorrentPeerRow> peers;
	std::map<long, TorrentTrackerRow> trackers;

	for (const auto & [key, value] : fields)
	{
		if (key.starts_with ("comments.") || key.starts_with ("comment."))
		{
			if (detail.comments.size () < 2048) detail.comments.emplace (key, BoundUtf8 (value, 4096));
			continue;
		}
		if (key.starts_with ("detail."))
		{
			AssignTorrentField (detail.info, std::string_view (key).substr (7), value);
			continue;
		}

		long index = 0;
		std::string_view field;
		if (SplitIndexedKey (key, "file.", index, field))
		{
			auto it = files.find (index);
			if (it == files.end ())
			{
				if (files.size () >= 500) continue;
				it = files.emplace (index, TorrentFileRow { }).first;
			}
			auto & file = it->second;
			file.index = index;
			if (field == "name")
				file.name = BoundUtf8 (value, 2048);
			else if (field == "length")
				file.length = ToU64 (value);
			else if (field == "completed")
				file.completed = ToU64 (value);
			else if (field == "priority")
				file.priority = std::clamp (ToInt (value), 0, 2);
		}
		else if (SplitIndexedKey (key, "peer.", index, field))
		{
			auto it = peers.find (index);
			if (it == peers.end ())
			{
				if (peers.size () >= 500) continue;
				it = peers.emplace (index, TorrentPeerRow { }).first;
			}
			auto & peer = it->second;
			if (field == "ident")
				peer.ident = BoundUtf8 (value, 512);
			else if (field == "client")
				peer.client = BoundUtf8 (value, 128);
			else if (field == "flags")
				peer.flags = BoundUtf8 (value, 64);
			else if (field == "progress")
				peer.progress = ToDouble (value);
			else if (field == "downRate")
				peer.downRate = ToU64 (value);
			else if (field == "upRate")
				peer.upRate = ToU64 (value);
		}
		else if (SplitIndexedKey (key, "tracker.", index, field))
		{
			auto it = trackers.find (index);
			if (it == trackers.end ())
			{
				if (trackers.size () >= 500) continue;
				it = trackers.emplace (index, TorrentTrackerRow { }).first;
			}
			auto & tracker = it->second;
			if (field == "announce")
				tracker.announce = BoundUtf8 (value, 2048);
			else if (field == "tier")
				tracker.tier = ToInt (value);
			else if (field == "peers")
				tracker.peers = ToInt (value);
			else if (field == "seeders")
				tracker.seeders = ToInt (value);
			else if (field == "leechers")
				tracker.leechers = ToInt (value);
			else if (field == "lastResult")
				tracker.lastResult = BoundUtf8 (value, 512);
			else if (field == "lastAnnounce")
				tracker.lastAnnounce = ToU64 (value);
			else if (field == "nextAnnounce")
				tracker.nextAnnounce = ToU64 (value);
		}
	}

	if (detail.info.id < 0) detail.info.id = fallbackId;
	if (detail.info.id < 0) detail.info.id = 0;
	FinalizeTorrent (detail.info);
	detail.loaded = !fields.empty ();
	for (auto & [index, file] : files)
	{
		(void)index;
		detail.files.push_back (std::move (file));
		if (detail.files.size () >= 500) break;
	}
	for (auto & [index, peer] : peers)
	{
		(void)index;
		detail.peers.push_back (std::move (peer));
		if (detail.peers.size () >= 500) break;
	}
	for (auto & [index, tracker] : trackers)
	{
		(void)index;
		detail.trackers.push_back (std::move (tracker));
		if (detail.trackers.size () >= 500) break;
	}
	return detail;
}

TorrentLayout ComputeTorrentLayout (int width)
{
	TorrentLayout layout;
	layout.showSize = width >= 60;
	layout.showDown = width >= 52;
	layout.showUp = width >= 70;
	layout.showPeers = width >= 64;
	layout.showRatio = width >= 84;
	layout.showError = width >= 96;

	auto calculate = [&] ()
	{
		int fixed = TORRENT_STATUS_COLS + TORRENT_PROGRESS_COLS;
		if (layout.showSize) fixed += TORRENT_SIZE_COLS;
		if (layout.showDown) fixed += TORRENT_DOWN_COLS;
		if (layout.showUp) fixed += TORRENT_UP_COLS;
		if (layout.showPeers) fixed += TORRENT_PEERS_COLS;
		if (layout.showRatio) fixed += TORRENT_RATIO_COLS;
		if (layout.showError) fixed += TORRENT_ERROR_COLS;
		layout.name = width - 2 - fixed;
	};
	calculate ();

	while (layout.name < 12)
	{
		if (layout.showError)
			layout.showError = false;
		else if (layout.showRatio)
			layout.showRatio = false;
		else if (layout.showUp)
			layout.showUp = false;
		else if (layout.showSize)
			layout.showSize = false;
		else if (layout.showDown)
			layout.showDown = false;
		else if (layout.showPeers)
			layout.showPeers = false;
		else
			break;
		calculate ();
	}
	if (layout.name < 4) layout.name = 4;
	return layout;
}

std::string SortName (TorrentSort sort)
{
	switch (sort)
	{
	case TorrentSort::Name: return "name";
	case TorrentSort::Status: return "status";
	case TorrentSort::Progress: return "progress";
	case TorrentSort::Down: return "down";
	case TorrentSort::Up: return "up";
	case TorrentSort::Peers: return "peers";
	}
	return "name";
}

bool IsStoppedStatus (std::string_view status)
{
	auto lower = ToLowerAscii (status);
	return lower.find ("stop") != std::string::npos || lower.find ("pause") != std::string::npos
		|| lower.find ("inactive") != std::string::npos || lower.find ("idle") != std::string::npos;
}

bool IsActiveStatus (std::string_view status)
{
	auto lower = ToLowerAscii (status);
	return lower.find ("start") != std::string::npos || lower.find ("active") != std::string::npos
		|| lower.find ("download") != std::string::npos || lower.find ("seed") != std::string::npos
		|| lower.find ("check") != std::string::npos || lower.find ("announc") != std::string::npos;
}

// Torrent status is an English wire token shared with Android. The protocol
// value itself never changes; only the table/detail rendering is localized.
std::string_view TorrentStatusText (std::string_view status)
{
	if (status == "downloading") return i2p::i18n::tr ("downloading");
	if (status == "seeding") return i2p::i18n::tr ("seeding");
	if (status == "verifying") return i2p::i18n::tr ("verifying");
	if (status == "queued") return i2p::i18n::tr ("queued");
	if (status == "queued-verify") return i2p::i18n::tr ("queued for verification");
	if (status == "queued-seed") return i2p::i18n::tr ("queued to seed");
	if (status == "stopped") return i2p::i18n::tr ("stopped");
	if (status == "unknown") return i2p::i18n::tr ("unknown");
	return status;
}

std::string RatioText (const TorrentRow & torrent)
{
	double ratio
		= torrent.downloaded ? static_cast<double> (torrent.uploaded) / static_cast<double> (torrent.downloaded) : 0.0;
	char buffer[32];
	snprintf (buffer, sizeof (buffer), "%.2f", ratio);
	return buffer;
}

std::string RatioEtaText (const TorrentRow & torrent)
{
	std::string text = RatioText (torrent);
	if (torrent.percent >= 99.95)
		text += " done";
	else if (torrent.eta)
		text += " " + HumanDuration (*torrent.eta);
	return text;
}

class App
{
public:
	explicit App (i2p::tui::ClientOptions options, const Theme & theme)
		: m_Options (std::move (options))
		, m_Client (m_Options.socketPath)
		, m_Theme (theme)
	{
		m_NextConnect = std::chrono::steady_clock::now ();
		m_NextRefresh = m_NextConnect;
		m_NextLogs = m_NextConnect;
		m_NextNetDbList = m_NextConnect;
		m_NextTorrentTunnels = m_NextConnect;
		m_NextTorrentList = m_NextConnect;
		m_NextTorrentSession = m_NextConnect;
		m_NextTorrentDht = m_NextConnect;
		m_NextTorrentDetail = m_NextConnect;
	}

	int Run ();

private:
	void ConnectIfNeeded ();
	void Refresh ();
	bool Request (std::string_view command, std::string & payload, bool reportRemoteError = true);
	void ParseState (const std::string & payload);
	void ParseNetDb (const std::string & payload);
	void ParseNetDbRouters (const std::string & payload);
	void ParseNetDbLeaseSets (const std::string & payload);
	void ParseConfig (const std::string & payload);
	void ParsePeers (const std::string & payload);
	void ParseConnections (const std::string & payload);
	void ParseTunnels (const std::string & payload);
	void ParseDestinations (const std::string & payload);
	void ParseServices (const std::string & payload);
	void ParseAddressBook (const std::string & payload);
	void ParseBlockedAddresses (const std::string & payload);
	void ParseSubscriptions (const std::string & payload);
	void ParseLogs (const std::string & payload);
	const std::string & Field (std::string_view name) const;

	void RefreshTorrents (std::chrono::steady_clock::time_point now);
	void ParseTorrentTunnels (const std::string & payload);
	void ParseTorrents (const std::string & payload);
	void ParseTorrentSession (const std::string & payload);
	void ParseTorrentDht (const std::string & payload);
	void ParseTorrentDetail (const std::string & payload);
	bool ParseCommandResult (const std::string & payload);
	std::string AdminOutcomeText (std::string_view outcome);
	bool SendAdminCommand (std::string_view command);
	void BeginTransitPrompt ();
	void BeginOnlinePrompt ();
	void CycleDaemonLogLevel ();
	void BeginReseedPrompt ();
	void BeginReloadTunnelsPrompt ();
	void BeginShutdownPrompt (bool immediate);
	void BeginBandwidthPrompt ();
	void BeginSharePrompt ();
	void BeginTunnelServicePrompt (bool start);
	void AskAddressReplace (std::string name, std::string destination);
	const TunnelServiceRow * SelectedTunnelService () const;
	void EnsureServiceSelectionVisible (int visible);
	void SendTorrentCommand (std::string_view command);
	void ScheduleTorrentRefresh (bool includeTunnels);
	void ResetTorrentAuxState ();
	void RebuildTorrentView (int preferredId);
	int CurrentSelectedId () const;
	const TorrentRow * SelectedTorrent () const;
	const TorrentTunnelRow * ActiveTunnel () const;
	bool RequireTorrentTunnel ();
	void SetStatus (std::string message, bool error);
	void EnterTorrentDetails ();
	void LeaveTorrentDetails ();
	void PageTorrentComments (int direction);
	bool TerminalTooSmall () const;
	void BeginPrompt (PromptKind kind, std::string label, std::string initial = { });
	bool HandlePromptKey (int key);
	void CommitPrompt ();
	void CancelPrompt ();
	void ToggleSelected ();
	void ReannounceSelected ();
	void RemoveSelected (bool files);
	void RemoveAllTorrents (bool files);
	void CycleTorrentSort ();
	void ToggleTorrentSortDirection ();
	void CycleTorrentTunnel (int delta);
	void EnsureTorrentSelectionVisible (int visible);
	int TorrentPageRows () const;

	void Draw ();
	void DrawHeader (int width);
	void DrawFooter (int height, int width);
	void DrawStatus (int top, int height, int width);
	void DrawRateHistory (int & row, int top, int height, int width);
	void DrawPeers (int top, int height, int width);
	void DrawConnections (int top, int height, int width);
	void DrawTunnels (int top, int height, int width);
	void DrawNetDb (int top, int height, int width);
	void DrawConfig (int top, int height, int width);
	void DrawDestinations (int top, int height, int width);
	void DrawServices (int top, int height, int width);
	void DrawLogs (int top, int height, int width);
	void DrawHelp (int top, int height, int width);
	void DrawTorrentContext (int width);
	void DrawTorrents (int top, int height, int width);
	void DrawTorrentDetails (int top, int height, int width);
	void DrawTorrentStatusLine (int height, int width);
	void BuildDetailGeneral (std::vector<std::string> & lines, const TorrentRow & info, int width) const;
	void BuildDetailFiles (std::vector<std::string> & lines, const TorrentDetail & detail, int width) const;
	void BuildDetailPeers (std::vector<std::string> & lines, const TorrentDetail & detail, int width) const;
	void BuildDetailTrackers (std::vector<std::string> & lines, const TorrentDetail & detail, int width) const;
	void BuildDetailComments (std::vector<std::string> & lines, const TorrentDetail & detail, int width) const;
	void PutLine (int row, const std::string & text, int width, int attributes = 0);

	void HandleKey (int key);

private:
	i2p::tui::ClientOptions m_Options;
	i2p::tui::Client m_Client;
	const Theme & m_Theme;
	bool m_Connected = false;
	// True once a successful snapshot arrived. A disconnected client keeps the
	// last snapshot for reference, but every panel marks it stale instead of
	// presenting cached rows as live.
	bool m_StateLoaded = false, m_PeersLoaded = false, m_TunnelsLoaded = false;
	bool m_DestinationsLoaded = false, m_ServicesLoaded = false, m_LogsLoaded = false;
	std::string m_Error;
	std::chrono::steady_clock::time_point m_NextConnect;
	std::chrono::steady_clock::time_point m_NextRefresh;
	std::chrono::steady_clock::time_point m_NextLogs;
	std::chrono::steady_clock::time_point m_NextNetDbList;
	std::chrono::steady_clock::time_point m_NextDestinationsAux;
	int m_DestinationsAux = 0;

	Tab m_Tab = Tab::Status;
	std::map<std::string, std::string> m_State;
	std::map<std::string, std::string> m_NetDb;
	NetDbView m_NetDbView = NetDbView::Routers;
	std::vector<NetDbRouterRow> m_NetDbRouters;
	std::vector<NetDbLeaseSetRow> m_NetDbLeaseSets;
	bool m_NetDbRoutersLoaded = false;
	bool m_NetDbLeaseSetsLoaded = false;
	std::vector<ConfigOptionRow> m_Config;
	int m_ConfigSchema = 0;
	bool m_ConfigReadOnly = true;
	bool m_ConfigLoaded = false;
	std::vector<PeerRow> m_Peers;
	std::vector<PeerRow> m_Connections;
	std::vector<TunnelRow> m_Tunnels;
	std::vector<std::string> m_Destinations;
	std::vector<ServiceRow> m_Services;
	std::vector<TunnelServiceRow> m_TunnelServices;
	int m_ServiceSelection = 0;
	int m_ServiceVisible = 10;
	// Names present in the bounded address-book snapshot, used to decide
	// between the add and replace prompts.
	std::set<std::string> m_AddressBookNames;
	std::deque<LogRow> m_Logs;
	std::string m_LogFilter;
	uint64_t m_LastLogSequence = 0;
	uint64_t m_LastUptime = 0;
	int m_LogLevel = eLogDebug;
	std::string m_DaemonLogLevel = "info";
	std::deque<uint32_t> m_InHistory;
	std::deque<uint32_t> m_OutHistory;
	int m_Scroll = 0;
	int m_HelpScroll = 0;
	bool m_Dirty = true;

	std::vector<TorrentTunnelRow> m_TorrentTunnels;
	int m_TorrentTunnel = -1;
	std::string m_TorrentTunnelName;
	std::vector<TorrentRow> m_Torrents;
	std::vector<size_t> m_TorrentView;
	int m_TorrentSelection = 0;
	int m_TorrentScroll = 0;
	TorrentSort m_TorrentSort = TorrentSort::Name;
	bool m_TorrentSortDescending = false;
	std::string m_TorrentFilter;
	bool m_TorrentsLoaded = false;
	TorrentSessionInfo m_TorrentSession;
	bool m_TorrentSessionLoaded = false;
	TorrentDhtInfo m_TorrentDht;
	bool m_TorrentDhtLoaded = false;
	bool m_TorrentDetails = false;
	TorrentDetail m_TorrentDetail;
	TorrentDetailTab m_TorrentDetailTab = TorrentDetailTab::General;
	int m_TorrentDetailScroll = 0;
	// TORRENT-DETAIL comment page: the daemon retains up to 256 records but
	// emits at most 16 per response, so the comments tab pages explicitly.
	int m_TorrentCommentOffset = 0;

	std::chrono::steady_clock::time_point m_NextTorrentTunnels;
	std::chrono::steady_clock::time_point m_NextTorrentList;
	std::chrono::steady_clock::time_point m_NextTorrentSession;
	std::chrono::steady_clock::time_point m_NextTorrentDht;
	std::chrono::steady_clock::time_point m_NextTorrentDetail;

	PromptKind m_Prompt = PromptKind::None;
	std::string m_PromptLabel;
	std::string m_InputBuffer;
	int m_PendingTorrentId = -1;
	int m_PendingTorrentTunnel = -1;
	std::string m_PendingTorrentName;
	std::string m_PendingTrackerHash;
	// Local comment id captured by the hide/unhide prompt and consumed by the
	// [y/N] confirmation, so the operator never retypes it.
	std::string m_PendingCommentId;
	std::string m_PendingTunnelName;
	bool m_PendingTransit = false;
	bool m_PendingOnline = false;
	// Pending address add/replace target: set by the add prompt and reused by
	// the confirmation prompt so the destination never has to be retyped.
	std::string m_PendingAddressName;
	std::string m_PendingAddressDestination;
	std::string m_PendingServiceName;
	// Last typed ADMIN outcome token (started/stopped/already-running/...);
	// empty when the verb reports no discrete outcome.
	std::string m_LastAdminOutcome;

	std::string m_StatusMessage;
	bool m_StatusError = false;
	std::chrono::steady_clock::time_point m_StatusExpiry;
};

int App::Run ()
{
	while (!g_Stop)
	{
		if (g_Resize)
		{
			g_Resize = 0;
			m_Dirty = true;
		}
		ConnectIfNeeded ();
		// Draw before issuing snapshot requests: a slow daemon may block the
		// synchronous client for up to its socket timeout, but the screen
		// already reflects the last complete snapshot.
		if (m_Dirty) Draw ();
		Refresh ();

		int key = terminal::ReadKey (200);
		if (key == terminal::KEY_RESIZE)
		{
			m_Dirty = true;
			continue;
		}
		if (key == terminal::KEY_CTRL_C)
		{
			g_Stop = 1;
			continue;
		}
		if (key != terminal::KEY_NONE) HandleKey (key);
	}
	return 0;
}

void App::ConnectIfNeeded ()
{
	if (m_Connected) return;
	auto now = std::chrono::steady_clock::now ();
	if (now < m_NextConnect) return;
	m_NextConnect = now + 2s;

	auto connected = m_Client.Connect ();
	if (!connected)
	{
		m_Error = connected.error ();
		m_Dirty = true;
		return;
	}
	m_Connected = true;
	m_Error.clear ();
	m_NextRefresh = now;
	ResetTorrentAuxState ();
	ScheduleTorrentRefresh (true);
	SetStatus ("connected to daemon", false);
	m_Dirty = true;
}

bool App::Request (std::string_view command, std::string & payload, bool reportRemoteError)
{
	auto response = m_Client.Request (command);
	if (!response)
	{
		const auto & error = response.error ();
		if (error.kind == i2p::tui::RequestErrorKind::Remote)
		{
			// A valid remote error leaves the socket usable; show both the
			// server code and its bounded text instead of hiding the cause.
			// Optional companion snapshots pass false so an older daemon
			// does not leave a permanent error in the status bar.
			if (reportRemoteError)
				SetStatus (std::string (i2p::tui::ErrorCodeName (error.code)) + ": " + error.message, true);
			return false;
		}
		m_Error = error.message;
		m_Connected = false;
		m_Client.Disconnect ();
		m_NextConnect = std::chrono::steady_clock::now () + 2s;
		m_Dirty = true;
		return false;
	}
	payload = std::move (*response);
	return true;
}

void App::Refresh ()
{
	if (!m_Connected) return;
	auto now = std::chrono::steady_clock::now ();

	if (now >= m_NextRefresh)
	{
		m_NextRefresh = now + 2s;
		std::string payload;
		if (Request ("STATE", payload))
		{
			ParseState (payload);
			m_StateLoaded = true;
		}

		if (m_Tab == Tab::Peers && Request ("PEERS 200", payload))
		{
			ParsePeers (payload);
			m_PeersLoaded = true;
		}
		else if (m_Tab == Tab::Connections && Request ("CONNECTIONS 200", payload))
			ParseConnections (payload);
		else if (m_Tab == Tab::Tunnels && Request ("TUNNELS", payload))
		{
			ParseTunnels (payload);
			m_TunnelsLoaded = true;
		}
		else if (m_Tab == Tab::NetDb)
		{
			if (Request ("NETDB", payload)) ParseNetDb (payload);
			if (now >= m_NextNetDbList)
			{
				m_NextNetDbList = now + 5s;
				const auto command = m_NetDbView == NetDbView::Routers ? "NETDB-ROUTERS 200" : "NETDB-LEASESETS 200";
				if (Request (command, payload))
				{
					if (m_NetDbView == NetDbView::Routers)
						ParseNetDbRouters (payload);
					else
						ParseNetDbLeaseSets (payload);
				}
			}
		}
		else if (m_Tab == Tab::Config && Request ("CONFIG", payload))
			ParseConfig (payload);
		else if (m_Tab == Tab::Services && Request ("SERVICES 200", payload))
		{
			ParseServices (payload);
			m_ServicesLoaded = true;
		}
		else if (m_Tab == Tab::Destinations)
		{
			if (Request ("DESTINATIONS", payload))
			{
				ParseDestinations (payload);
				m_DestinationsLoaded = true;
			}
			else
				m_Destinations.clear ();
			// One auxiliary address-book request per slow pass keeps the input
			// loop responsive: the previous code issued up to four blocking
			// requests (worst case ~12 s) in a single tick. The names still
			// refresh in a bounded rotation and are rebuilt, not accumulated.
			if (now >= m_NextDestinationsAux)
			{
				m_NextDestinationsAux = now + 6s;
				switch (m_DestinationsAux++ % 3)
				{
				case 0:
					m_AddressBookNames.clear ();
					if (Request ("ADDRESSBOOK 200", payload, false)) ParseAddressBook (payload);
					break;
				case 1:
					if (Request ("ADDRESSBOOK-BLOCKED 200", payload, false)) ParseBlockedAddresses (payload);
					break;
				default:
					if (Request ("ADDRESSBOOK-SUBSCRIPTIONS 16", payload, false)) ParseSubscriptions (payload);
					break;
				}
			}
		}

		if (m_Tab == Tab::Logs && now >= m_NextLogs)
		{
			m_NextLogs = now + 1s;
			std::string command = "LOGS " + std::to_string (m_LastLogSequence) + " 200";
			if (Request (command, payload))
			{
				ParseLogs (payload);
				m_LogsLoaded = true;
			}
		}
	}

	if (m_Tab == Tab::Torrents) RefreshTorrents (now);
}

void App::RefreshTorrents (std::chrono::steady_clock::time_point now)
{
	std::string payload;

	if (now >= m_NextTorrentTunnels)
	{
		m_NextTorrentTunnels = now + 5s;
		if (Request ("TORRENT-TUNNELS", payload)) ParseTorrentTunnels (payload);
		return;
	}
	if (m_TorrentTunnel < 0) return;

	if (now >= m_NextTorrentList)
	{
		m_NextTorrentList = now + 2s;
		std::string command = "TORRENTS " + std::to_string (m_TorrentTunnel) + " "
			+ std::to_string (i2p::tui::MAX_TORRENT_RECORDS_PER_REQUEST);
		if (Request (command, payload)) ParseTorrents (payload);
		return;
	}
	if (now >= m_NextTorrentSession)
	{
		m_NextTorrentSession = now + 5s;
		std::string command = "TORRENT-SESSION " + std::to_string (m_TorrentTunnel);
		if (Request (command, payload)) ParseTorrentSession (payload);
		return;
	}
	if (now >= m_NextTorrentDht)
	{
		m_NextTorrentDht = now + 5s;
		std::string command = "TORRENT-DHT " + std::to_string (m_TorrentTunnel);
		if (Request (command, payload)) ParseTorrentDht (payload);
		return;
	}
	if (m_TorrentDetails && now >= m_NextTorrentDetail)
	{
		auto selected = SelectedTorrent ();
		int id = selected ? selected->id : m_TorrentDetail.info.id;
		if (id < 0) return;
		m_NextTorrentDetail = now + 2s;
		std::string command = "TORRENT-DETAIL " + std::to_string (m_TorrentTunnel) + " " + std::to_string (id);
		// The daemon emits at most one bounded comment page per response, so
		// the request carries the page explicitly. Offset 0 with the default
		// size keeps the pre-paging request line for the common case.
		if (m_TorrentCommentOffset > 0)
			command += " " + std::to_string (m_TorrentCommentOffset) + " "
				+ std::to_string (i2p::tui::MAX_TORRENT_COMMENT_RECORDS_PER_REQUEST);
		if (Request (command, payload)) ParseTorrentDetail (payload);
	}
}

void App::ParseState (const std::string & payload)
{
	m_State.clear ();
	for (auto line : Split (payload, '\n'))
	{
		if (line.empty ()) continue;
		auto equals = line.find ('=');
		if (equals == std::string_view::npos) continue;
		auto name = std::string (line.substr (0, equals));
		if (name.size () > i2p::tui::MAX_FIELD_NAME) continue;
		m_State[name] = i2p::tui::UnescapeValue (line.substr (equals + 1));
	}

	auto appendHistory = [] (std::deque<uint32_t> & history, std::string_view value)
	{
		history.push_back (static_cast<uint32_t> (ToU64 (value)));
		while (history.size () > 60) history.pop_front ();
	};
	if (m_State.contains ("bandwidth.in15") && m_State.contains ("bandwidth.out15"))
	{
		appendHistory (m_InHistory, m_State["bandwidth.in15"]);
		appendHistory (m_OutHistory, m_State["bandwidth.out15"]);
	}
	if (m_State.contains ("log.level"))
		m_DaemonLogLevel = std::string (DaemonLogLevelName (ToInt (m_State["log.level"])));
	if (m_State.contains ("router.uptime"))
	{
		const uint64_t uptime = ToU64 (m_State["router.uptime"]);
		// A new daemon instance restarts its log sequence at 1. Without this
		// reset the Logs view remains frozen forever after a router restart.
		if (m_LastUptime != 0 && uptime < m_LastUptime)
		{
			m_Logs.clear ();
			m_LastLogSequence = 0;
		}
		m_LastUptime = uptime;
	}
	m_Dirty = true;
}

void App::ParseNetDb (const std::string & payload)
{
	m_NetDb.clear ();
	for (auto line : Split (payload, '\n'))
	{
		if (line.empty ()) continue;
		auto equals = line.find ('=');
		if (equals == std::string_view::npos) continue;
		auto name = std::string (line.substr (0, equals));
		if (name.size () > i2p::tui::MAX_FIELD_NAME) continue;
		m_NetDb[name] = i2p::tui::UnescapeValue (line.substr (equals + 1));
	}
}

void App::ParseNetDbRouters (const std::string & payload)
{
	m_NetDbRouters.clear ();
	for (auto line : Split (payload, '\n'))
	{
		if (!line.starts_with ("netdb.router=")) continue;
		auto value = i2p::tui::UnescapeValue (line.substr (13));
		auto parts = Split (value, '|');
		if (parts.size () != 8) continue;
		NetDbRouterRow row;
		row.hash = std::string (parts[0]);
		row.version = ToLong (parts[1]);
		row.caps = ToLong (parts[2]);
		row.floodfill = parts[3] == "1";
		row.v4 = parts[4] == "1";
		row.v6 = parts[5] == "1";
		row.timestamp = ToU64 (parts[6]);
		row.reachable = parts[7] == "1";
		m_NetDbRouters.push_back (std::move (row));
		if (m_NetDbRouters.size () >= i2p::tui::MAX_NETDB_RECORDS_PER_REQUEST) break;
	}
	std::sort (m_NetDbRouters.begin (), m_NetDbRouters.end (),
		[] (const NetDbRouterRow & left, const NetDbRouterRow & right) { return left.hash < right.hash; });
	m_NetDbRoutersLoaded = true;
	m_Dirty = true;
}

void App::ParseNetDbLeaseSets (const std::string & payload)
{
	m_NetDbLeaseSets.clear ();
	for (auto line : Split (payload, '\n'))
	{
		if (!line.starts_with ("netdb.leaseset=")) continue;
		auto value = i2p::tui::UnescapeValue (line.substr (15));
		auto parts = Split (value, '|');
		if (parts.size () != 7) continue;
		NetDbLeaseSetRow row;
		row.hash = std::string (parts[0]);
		row.storeType = ToLong (parts[1]);
		row.encryptionType = ToLong (parts[2]);
		row.expiration = ToU64 (parts[3]);
		row.earliestLease = ToU64 (parts[4]);
		row.valid = parts[5] == "1";
		row.expired = parts[6] == "1";
		m_NetDbLeaseSets.push_back (std::move (row));
		if (m_NetDbLeaseSets.size () >= i2p::tui::MAX_NETDB_RECORDS_PER_REQUEST) break;
	}
	std::sort (m_NetDbLeaseSets.begin (), m_NetDbLeaseSets.end (),
		[] (const NetDbLeaseSetRow & left, const NetDbLeaseSetRow & right) { return left.hash < right.hash; });
	m_NetDbLeaseSetsLoaded = true;
	m_Dirty = true;
}

void App::ParseConfig (const std::string & payload)
{
	auto fields = ParseFieldMap (payload);
	m_ConfigSchema = ToInt (MapValue (fields, "config.schema"));
	auto readonly = ToLowerAscii (MapValue (fields, "config.readonly"));
	m_ConfigReadOnly = readonly == "1" || readonly == "true" || readonly == "yes";

	std::map<long, ConfigOptionRow> rows;
	for (const auto & [key, value] : fields)
	{
		long index = 0;
		std::string_view field;
		if (!SplitIndexedKey (key, "config.option.", index, field)) continue;
		auto it = rows.find (index);
		if (it == rows.end ())
		{
			if (rows.size () >= i2p::tui::MAX_CONFIG_RECORDS_PER_REQUEST) continue;
			it = rows.emplace (index, ConfigOptionRow { }).first;
		}
		auto & option = it->second;
		if (field == "name")
			option.name = BoundUtf8 (value, 128);
		else if (field == "type")
			option.type = BoundUtf8 (value, 16);
		else if (field == "value")
			option.value = BoundUtf8 (value, i2p::tui::MAX_CONFIG_VALUE_LENGTH);
	}

	m_Config.clear ();
	for (auto & [index, option] : rows)
	{
		(void)index;
		if (option.name.empty ()) continue;
		if (option.type.empty ()) option.type = "unknown";
		m_Config.push_back (std::move (option));
		if (m_Config.size () >= i2p::tui::MAX_CONFIG_RECORDS_PER_REQUEST) break;
	}
	for (const auto & option : m_Config)
	{
		if (option.name == "loglevel" && !option.value.empty ())
		{
			m_DaemonLogLevel = ToLowerAscii (option.value);
			break;
		}
	}
	m_ConfigLoaded = true;
	m_Dirty = true;
}

bool ParsePeerRow (std::string_view value, PeerRow & row)
{
	auto parts = Split (value, '|');
	if (parts.size () != 5 && parts.size () != 6) return false;
	row.hash = std::string (parts[0]);
	row.attempts = ToLong (parts[1]);
	row.sessions = ToLong (parts[2]);
	row.transports = ToLong (parts[3]);
	row.highBandwidth = parts[4] == "1";
	row.transportNames = parts.size () == 6 ? std::string (parts[5]) : TransportNamesFromMask (row.transports);
	return true;
}

void App::ParsePeers (const std::string & payload)
{
	m_Peers.clear ();
	for (auto line : Split (payload, '\n'))
	{
		if (!line.starts_with ("peer=")) continue;
		auto value = i2p::tui::UnescapeValue (line.substr (5));
		PeerRow row;
		if (!ParsePeerRow (value, row)) continue;
		m_Peers.push_back (std::move (row));
		if (m_Peers.size () >= i2p::tui::MAX_PEER_RECORDS_PER_REQUEST) break;
	}
}

void App::ParseConnections (const std::string & payload)
{
	m_Connections.clear ();
	for (auto line : Split (payload, '\n'))
	{
		if (!line.starts_with ("connection=")) continue;
		auto value = i2p::tui::UnescapeValue (line.substr (11));
		PeerRow row;
		if (!ParsePeerRow (value, row)) continue;
		m_Connections.push_back (std::move (row));
		if (m_Connections.size () >= i2p::tui::MAX_CONNECTION_RECORDS_PER_REQUEST) break;
	}
	std::sort (m_Connections.begin (), m_Connections.end (),
		[] (const PeerRow & left, const PeerRow & right)
		{
			if (left.sessions != right.sessions) return left.sessions > right.sessions;
			return left.hash < right.hash;
		});
	m_Dirty = true;
}

void App::ParseTunnels (const std::string & payload)
{
	m_Tunnels.clear ();
	for (auto line : Split (payload, '\n'))
	{
		if (!line.starts_with ("tunnel=")) continue;
		auto value = i2p::tui::UnescapeValue (line.substr (7));
		auto parts = Split (value, '|');
		if (parts.size () != 5) continue;
		m_Tunnels.push_back (TunnelRow { std::string (parts[0]), std::string (parts[1]), std::string (parts[2]),
			std::string (parts[3]), std::string (parts[4]) });
	}
}

void App::ParseDestinations (const std::string & payload)
{
	m_Destinations.clear ();
	for (auto line : Split (payload, '\n'))
	{
		if (!line.starts_with ("destination=")) continue;
		auto value = i2p::tui::UnescapeValue (line.substr (12));
		auto separator = value.find ('|');
		if (separator != std::string::npos) value.resize (separator);
		if (m_Destinations.size () < 600) m_Destinations.push_back (std::move (value));
	}
}

void App::ParseServices (const std::string & payload)
{
	m_Services.clear ();
	m_TunnelServices.clear ();
	for (auto line : Split (payload, '\n'))
	{
		if (line.starts_with ("tunnel.service="))
		{
			auto value = i2p::tui::UnescapeValue (line.substr (15));
			auto parts = Split (value, '|');
			if (parts.size () != 5) continue;
			TunnelServiceRow row;
			row.name = std::string (parts[0]);
			row.kind = std::string (parts[1]);
			row.running = parts[2] == "1";
			row.endpoint = std::string (parts[3]);
			row.destination = std::string (parts[4]);
			if (row.name.empty ()) continue;
			if (m_TunnelServices.size () < i2p::tui::MAX_SERVICE_RECORDS_PER_REQUEST)
				m_TunnelServices.push_back (std::move (row));
			continue;
		}
		if (!line.starts_with ("service=")) continue;
		auto value = i2p::tui::UnescapeValue (line.substr (8));
		auto parts = Split (value, '|');
		if (parts.size () != 5) continue;
		ServiceRow row;
		row.kind = std::string (parts[0]);
		row.name = std::string (parts[1]);
		row.endpoint = std::string (parts[2]);
		row.address = std::string (parts[3]);
		row.enabled = parts[4] == "1";
		if (m_Services.size () < i2p::tui::MAX_SERVICE_RECORDS_PER_REQUEST) m_Services.push_back (std::move (row));
	}
	const int maximum = std::max (0, static_cast<int> (m_TunnelServices.size ()) - 1);
	m_ServiceSelection = std::clamp (m_ServiceSelection, 0, maximum);
	m_Dirty = true;
}

void App::ParseAddressBook (const std::string & payload)
{
	for (auto line : Split (payload, '\n'))
	{
		if (!line.starts_with ("address=")) continue;
		auto value = i2p::tui::UnescapeValue (line.substr (8));
		auto parts = Split (value, '|');
		if (parts.size () != 3) continue;
		if (!parts[0].empty ()) m_AddressBookNames.insert (std::string (parts[0]));
		std::string row = "book " + std::string (parts[0]) + " -> " + std::string (parts[1]);
		if (parts[2] == "1") row.append (" (blinded)");
		// The server already bounds ADDRESSBOOK to 200 rows. Do not hide them
		// behind the local-destination rows when both lists are populated.
		if (m_Destinations.size () < 600) m_Destinations.push_back (std::move (row));
	}
	m_Dirty = true;
}

void App::ParseBlockedAddresses (const std::string & payload)
{
	for (auto line : Split (payload, '\n'))
	{
		if (!line.starts_with ("blocked=")) continue;
		auto value = i2p::tui::UnescapeValue (line.substr (8));
		if (value.empty ()) continue;
		if (m_Destinations.size () < 600) m_Destinations.push_back ("blocked " + std::string (value));
	}
	m_Dirty = true;
}

void App::ParseSubscriptions (const std::string & payload)
{
	for (auto line : Split (payload, '\n'))
	{
		if (!line.starts_with ("subscription=")) continue;
		auto value = i2p::tui::UnescapeValue (line.substr (13));
		if (value.empty ()) continue;
		if (m_Destinations.size () < 600) m_Destinations.push_back ("subscription " + std::string (value));
	}
	m_Dirty = true;
}

void App::ParseLogs (const std::string & payload)
{
	for (auto line : Split (payload, '\n'))
	{
		if (!line.starts_with ("log=")) continue;
		auto value = i2p::tui::UnescapeValue (line.substr (4));
		auto parts = Split (value, '|');
		if (parts.size () < 4) continue;
		LogRow row;
		row.sequence = static_cast<uint64_t> (ToLong (parts[0]));
		if (row.sequence <= m_LastLogSequence) continue;
		row.timestamp = ToLong (parts[1]);
		row.level = static_cast<int> (ToLong (parts[2]));
		// The message may contain '|'; only the first three are separators.
		auto textStart = value.find ('|', value.find ('|', value.find ('|') + 1) + 1);
		row.text = textStart == std::string::npos ? std::string (parts[3]) : value.substr (textStart + 1);
		m_LastLogSequence = row.sequence;
		m_Logs.push_back (std::move (row));
		if (m_Logs.size () > 2000) m_Logs.pop_front ();
	}
}

const std::string & App::Field (std::string_view name) const
{
	static const std::string empty;
	auto it = m_State.find (std::string (name));
	return it == m_State.end () ? empty : it->second;
}

void App::ParseTorrentTunnels (const std::string & payload)
{
	auto fields = ParseFieldMap (payload);
	std::map<long, TorrentTunnelRow> rows;
	for (const auto & [key, value] : fields)
	{
		long index = 0;
		std::string_view field;
		if (!SplitIndexedKey (key, "torrent.tunnel.", index, field)) continue;
		auto it = rows.find (index);
		if (it == rows.end ())
		{
			if (rows.size () >= 256) continue;
			it = rows.emplace (index, TorrentTunnelRow { }).first;
		}
		auto & row = it->second;
		if (row.index < 0) row.index = static_cast<int> (index);
		if (field == "name")
			row.name = BoundUtf8 (value, 512);
		else if (field == "torrents")
			row.torrents = ToInt (value);
		else if (field == "index")
			row.index = ToInt (value, row.index);
	}

	std::vector<TorrentTunnelRow> tunnels;
	for (auto & [index, row] : rows)
	{
		(void)index;
		if (row.index < 0) continue;
		if (row.name.empty ()) row.name = "tunnel " + std::to_string (row.index);
		tunnels.push_back (std::move (row));
		if (tunnels.size () >= 256) break;
	}
	std::sort (tunnels.begin (), tunnels.end (),
		[] (const TorrentTunnelRow & left, const TorrentTunnelRow & right)
		{
			if (left.index != right.index) return left.index < right.index;
			return left.name < right.name;
		});

	int previous = m_TorrentTunnel;
	auto previousName = m_TorrentTunnelName;
	m_TorrentTunnels = std::move (tunnels);
	bool found = false;
	for (const auto & tunnel : m_TorrentTunnels)
	{
		// The index is only a position in the sorted snapshot: after a
		// tunnel is added or removed it can name a different tunnel. Keep
		// the previous selection only if both index and name still match.
		if (tunnel.index == previous && tunnel.name == previousName)
		{
			found = true;
			break;
		}
	}
	m_TorrentTunnel = found ? previous : (m_TorrentTunnels.empty () ? -1 : m_TorrentTunnels.front ().index);
	m_TorrentTunnelName.clear ();
	for (const auto & tunnel : m_TorrentTunnels)
	{
		if (tunnel.index == m_TorrentTunnel)
		{
			m_TorrentTunnelName = tunnel.name;
			break;
		}
	}

	bool clear = !found || (m_TorrentTunnels.empty () && !m_Torrents.empty ());
	if (clear)
	{
		m_Torrents.clear ();
		m_TorrentView.clear ();
		m_TorrentsLoaded = false;
		m_TorrentDetails = false;
		m_TorrentDetail = TorrentDetail { };
		m_TorrentSelection = 0;
		m_TorrentScroll = 0;
		ResetTorrentAuxState ();
		auto now = std::chrono::steady_clock::now ();
		m_NextTorrentList = now;
		m_NextTorrentSession = now;
		m_NextTorrentDht = now;
		m_NextTorrentDetail = now;
	}
	m_Dirty = true;
}

void App::ParseTorrents (const std::string & payload)
{
	int preferred = CurrentSelectedId ();
	if (preferred < 0 && m_TorrentDetails) preferred = m_TorrentDetail.info.id;

	auto fields = ParseFieldMap (payload);
	m_Torrents = BuildTorrentRows (fields);
	m_TorrentsLoaded = true;
	RebuildTorrentView (preferred);

	if (m_TorrentDetails)
	{
		bool present = false;
		for (const auto & torrent : m_Torrents)
		{
			if (torrent.id == m_TorrentDetail.info.id)
			{
				present = true;
				break;
			}
		}
		if (!present)
		{
			m_TorrentDetails = false;
			m_TorrentDetail = TorrentDetail { };
			SetStatus ("selected torrent is no longer present", true);
		}
	}
	m_Dirty = true;
}

void App::ParseTorrentSession (const std::string & payload)
{
	auto fields = ParseFieldMap (payload);
	m_TorrentSession.downloaded = ToU64 (MapValue (fields, "session.downloaded"));
	m_TorrentSession.uploaded = ToU64 (MapValue (fields, "session.uploaded"));
	m_TorrentSession.active = ToInt (MapValue (fields, "session.active"));
	m_TorrentSession.paused = ToInt (MapValue (fields, "session.paused"));
	m_TorrentSession.creationJob = ToU64 (MapValue (fields, "creation.job"));
	m_TorrentSession.creationBytesDone = ToU64 (MapValue (fields, "creation.bytesDone"));
	m_TorrentSession.creationTotalBytes = ToU64 (MapValue (fields, "creation.totalBytes"));
	m_TorrentSession.creationFilesDone = ToU64 (MapValue (fields, "creation.filesDone"));
	m_TorrentSession.creationTotalFiles = ToU64 (MapValue (fields, "creation.totalFiles"));
	m_TorrentSession.creationPending = ToInt (MapValue (fields, "creation.pending")) != 0;
	m_TorrentSession.creationSuccess = ToInt (MapValue (fields, "creation.success")) != 0;
	m_TorrentSession.creationTorrentId = ToInt (MapValue (fields, "creation.torrentId"));
	m_TorrentSession.creationMessage = SanitizeText (MapValue (fields, "creation.message"));
	m_TorrentSessionLoaded = true;
	m_Dirty = true;
}

void App::ParseTorrentDht (const std::string & payload)
{
	auto fields = ParseFieldMap (payload);
	auto supported = MapValue (fields, "dht.supported");
	m_TorrentDht.supported
		= supported == "1" || ToLowerAscii (supported) == "true" || ToLowerAscii (supported) == "yes";
	m_TorrentDht.numNodes = ToInt (MapValue (fields, "dht.numNodes"));
	m_TorrentDht.numBuckets = ToInt (MapValue (fields, "dht.numBuckets"));
	m_TorrentDht.numTorrents = ToInt (MapValue (fields, "dht.numTorrents"));
	m_TorrentDht.numHeardFrom = ToInt (MapValue (fields, "dht.numHeardFrom"));
	m_TorrentDht.numFailedPeers = ToInt (MapValue (fields, "dht.numFailedPeers"));
	m_TorrentDhtLoaded = true;
	m_Dirty = true;
}

void App::ParseTorrentDetail (const std::string & payload)
{
	int fallbackId = CurrentSelectedId ();
	if (fallbackId < 0) fallbackId = m_TorrentDetail.info.id;
	const int previousId = m_TorrentDetail.info.id;
	m_TorrentDetail = BuildTorrentDetail (ParseFieldMap (payload), fallbackId);
	m_TorrentDetailScroll = std::max (0, m_TorrentDetailScroll);
	if (m_TorrentDetail.info.id != previousId) m_TorrentCommentOffset = 0;

	// Keep the requested page inside the retained set: moderation, expiry or
	// a reload can shrink the comment vector between requests, and an offset
	// past the end would otherwise show an empty tab forever.
	auto field = [] (const std::map<std::string, std::string> & fields, const char * key)
	{ return ToU64 (MapValue (fields, key)); };
	const auto & comments = m_TorrentDetail.comments;
	const auto total = std::min<uint64_t> (field (comments, "comments.total"),
		static_cast<uint64_t> (i2p::tui::MAX_TORRENT_COMMENT_OFFSET) + 1);
	const auto count = std::min<uint64_t> (field (comments, "comments.count"),
		static_cast<uint64_t> (i2p::tui::MAX_TORRENT_COMMENT_RECORDS_PER_REQUEST));
	const auto offset = std::min<uint64_t> (field (comments, "comments.offset"),
		static_cast<uint64_t> (i2p::tui::MAX_TORRENT_COMMENT_OFFSET));
	const int previousOffset = m_TorrentCommentOffset;
	if (!comments.empty () && count == 0 && total > 0 && offset > 0)
	{
		const auto page = static_cast<uint64_t> (i2p::tui::MAX_TORRENT_COMMENT_RECORDS_PER_REQUEST);
		m_TorrentCommentOffset = static_cast<int> (std::min<uint64_t> (
			((total - 1) / page) * page, static_cast<uint64_t> (i2p::tui::MAX_TORRENT_COMMENT_OFFSET)));
		if (m_TorrentCommentOffset < 0) m_TorrentCommentOffset = 0;
	}
	else if (!comments.empty ())
		m_TorrentCommentOffset = static_cast<int> (offset);
	if (m_TorrentCommentOffset != previousOffset)
		m_NextTorrentDetail = std::chrono::steady_clock::now ();
	m_Dirty = true;
}

bool App::ParseCommandResult (const std::string & payload)
{
	auto fields = ParseFieldMap (payload);
	auto result = ToLowerAscii (MapValue (fields, "result"));
	auto message = MapValue (fields, "message");
	// Typed outcome of the verbs that have one. Protocol tokens stay
	// untranslated; the caller maps them to localized status text.
	m_LastAdminOutcome = ToLowerAscii (MapValue (fields, "outcome"));
	bool error = result == "error";
	if (message.empty ()) message = error ? "daemon rejected the command" : "command accepted";
	SetStatus (message, error);
	return !error;
}

// Localized text for a typed ADMIN outcome. The tokens themselves are never
// shown, so a translated build still reports the exact protocol result.
std::string App::AdminOutcomeText (std::string_view outcome)
{
	if (outcome == "started") return std::string (i2p::i18n::tr ("tunnel started"));
	if (outcome == "stopped") return std::string (i2p::i18n::tr ("tunnel stopped"));
	if (outcome == "already-running") return std::string (i2p::i18n::tr ("tunnel is already running"));
	if (outcome == "already-stopped") return std::string (i2p::i18n::tr ("tunnel is already stopped"));
	if (outcome == "unknown") return std::string (i2p::i18n::tr ("no configured tunnel has that name"));
	if (outcome == "disabled") return std::string (i2p::i18n::tr ("reseed is disabled by configuration"));
	if (outcome == "exists") return std::string (i2p::i18n::tr ("that name already exists"));
	return { };
}

bool App::SendAdminCommand (std::string_view command)
{
	// Never let a previous verb's outcome be observed by this one.
	m_LastAdminOutcome.clear ();
	if (!m_Connected)
	{
		SetStatus ("daemon is not connected", true);
		return false;
	}
	std::string payload;
	if (!Request (command, payload)) return false;
	bool accepted = ParseCommandResult (payload);
	if (accepted)
	{
		m_NextRefresh = std::chrono::steady_clock::now ();
		m_Dirty = true;
	}
	return accepted;
}

void App::BeginTransitPrompt ()
{
	if (!m_Connected)
	{
		SetStatus ("daemon is not connected", true);
		return;
	}
	const auto & current = Field ("router.accepts_tunnels");
	if (current.empty ())
	{
		SetStatus ("transit state is not available yet", true);
		return;
	}
	m_PendingTransit = current != "1";
	BeginPrompt (PromptKind::ConfirmTransit,
		m_PendingTransit ? "Enable transit tunnels? [y/N] " : "Disable transit tunnels? [y/N] ");
}

void App::BeginOnlinePrompt ()
{
	if (!m_Connected)
	{
		SetStatus ("daemon is not connected", true);
		return;
	}
	const auto & current = Field ("transport.online");
	if (current.empty ())
	{
		SetStatus ("transport state is not available yet", true);
		return;
	}
	m_PendingOnline = current != "1";
	BeginPrompt (PromptKind::ConfirmOnline,
		m_PendingOnline ? "Bring transports online? [y/N] "
						: "Take transports offline (the router keeps running)? [y/N] ");
}

void App::CycleDaemonLogLevel ()
{
	static constexpr std::array<std::string_view, 6> levels = { "none", "critical", "error", "warn", "info", "debug" };
	size_t current = 0;
	for (size_t i = 0; i < levels.size (); ++i)
		if (levels[i] == m_DaemonLogLevel)
		{
			current = i;
			break;
		}
	const auto next = levels[(current + 1) % levels.size ()];
	if (SendAdminCommand ("ADMIN LOGLEVEL " + std::string (next))) m_DaemonLogLevel = std::string (next);
}

void App::BeginReseedPrompt ()
{
	if (!m_Connected)
	{
		SetStatus ("daemon is not connected", true);
		return;
	}
	BeginPrompt (PromptKind::ConfirmReseed,
		std::string (i2p::i18n::tr ("Reseed the NetDb from the network? [y/N] ")));
}

void App::BeginReloadTunnelsPrompt ()
{
	if (!m_Connected)
	{
		SetStatus ("daemon is not connected", true);
		return;
	}
	BeginPrompt (PromptKind::ConfirmReloadTunnels, "Reload tunnel configuration? [y/N] ");
}

void App::BeginShutdownPrompt (bool immediate)
{
	if (!m_Connected)
	{
		SetStatus ("daemon is not connected", true);
		return;
	}
	if (immediate)
		BeginPrompt (PromptKind::ConfirmShutdownImmediate, "Stop the daemon immediately? Type SHUTDOWN to confirm: ");
	else
		BeginPrompt (PromptKind::ConfirmShutdownGraceful, "Begin graceful shutdown (stop accepting tunnels)? [y/N] ");
}

void App::BeginBandwidthPrompt ()
{
	if (!m_Connected)
	{
		SetStatus ("daemon is not connected", true);
		return;
	}
	BeginPrompt (PromptKind::Bandwidth, "bandwidth class K/L/M/N/O/P/X: ");
}

void App::BeginSharePrompt ()
{
	if (!m_Connected)
	{
		SetStatus ("daemon is not connected", true);
		return;
	}
	BeginPrompt (PromptKind::Share, "share ratio percent 0-100: ");
}

void App::AskAddressReplace (std::string name, std::string destination)
{
	if (name.empty () || destination.empty ()) return;
	m_PendingAddressName = std::move (name);
	m_PendingAddressDestination = std::move (destination);
	BeginPrompt (PromptKind::AddressReplace,
		i2p::i18n::tr (
			"Address '%s' already exists. Replace its destination? [y/N] ", m_PendingAddressName.c_str ()));
}

const TunnelServiceRow * App::SelectedTunnelService () const
{
	if (m_ServiceSelection < 0 || m_ServiceSelection >= static_cast<int> (m_TunnelServices.size ())) return nullptr;
	return &m_TunnelServices[static_cast<size_t> (m_ServiceSelection)];
}

void App::EnsureServiceSelectionVisible (int visible)
{
	if (visible <= 0) return;
	if (m_ServiceSelection < m_Scroll) m_Scroll = m_ServiceSelection;
	else if (m_ServiceSelection >= m_Scroll + visible) m_Scroll = m_ServiceSelection - visible + 1;
	if (m_Scroll < 0) m_Scroll = 0;
	m_Dirty = true;
}

void App::BeginTunnelServicePrompt (bool start)
{
	if (!m_Connected)
	{
		SetStatus ("daemon is not connected", true);
		return;
	}
	const auto * service = SelectedTunnelService ();
	if (!service)
	{
		SetStatus (std::string (i2p::i18n::tr ("no configured tunnel is selected")), true);
		return;
	}
	if (start == service->running)
	{
		SetStatus (std::string (start ? i2p::i18n::tr ("tunnel is already running")
									  : i2p::i18n::tr ("tunnel is already stopped")),
			false);
		return;
	}
	// The selected name is a bounded, printable service name from the daemon's
	// own snapshot; it is never assembled from typed input.
	m_PendingServiceName = service->name;
	const std::string label = start ? i2p::i18n::tr ("Start tunnel '%s'?", service->name.c_str ())
								    : i2p::i18n::tr ("Stop tunnel '%s'?", service->name.c_str ());
	BeginPrompt (start ? PromptKind::ConfirmTunnelStart : PromptKind::ConfirmTunnelStop,
		label + " " + std::string (i2p::i18n::tr ("[y/N] ")));
}

void App::SendTorrentCommand (std::string_view command)
{
	if (!m_Connected)
	{
		SetStatus ("daemon is not connected", true);
		return;
	}
	if (command.size () > 4000)
	{
		SetStatus ("command is too long", true);
		return;
	}
	std::string payload;
	if (!Request (command, payload)) return;
	ParseCommandResult (payload);
	ScheduleTorrentRefresh (false);
}

void App::ScheduleTorrentRefresh (bool includeTunnels)
{
	auto now = std::chrono::steady_clock::now ();
	if (includeTunnels) m_NextTorrentTunnels = now;
	m_NextTorrentList = now;
	m_NextTorrentSession = now;
	m_NextTorrentDht = now;
	m_NextTorrentDetail = now;
}

void App::ResetTorrentAuxState ()
{
	// Session and DHT counters belong to one tunnel (and one daemon
	// instance). Never keep showing the previous values while the new
	// snapshot is pending or if it fails.
	m_TorrentSession = TorrentSessionInfo { };
	m_TorrentSessionLoaded = false;
	m_TorrentDht = TorrentDhtInfo { };
	m_TorrentDhtLoaded = false;
	// A comment page belongs to one torrent of one tunnel; never carry an
	// offset across a session or selection change.
	m_TorrentCommentOffset = 0;
}

int App::CurrentSelectedId () const
{
	if (m_TorrentSelection < 0 || m_TorrentSelection >= static_cast<int> (m_TorrentView.size ())) return -1;
	size_t index = m_TorrentView[m_TorrentSelection];
	if (index >= m_Torrents.size ()) return -1;
	return m_Torrents[index].id;
}

const TorrentRow * App::SelectedTorrent () const
{
	if (m_TorrentSelection < 0 || m_TorrentSelection >= static_cast<int> (m_TorrentView.size ())) return nullptr;
	size_t index = m_TorrentView[m_TorrentSelection];
	if (index >= m_Torrents.size ()) return nullptr;
	return &m_Torrents[index];
}

const TorrentTunnelRow * App::ActiveTunnel () const
{
	for (const auto & tunnel : m_TorrentTunnels)
		if (tunnel.index == m_TorrentTunnel) return &tunnel;
	return nullptr;
}

bool App::RequireTorrentTunnel ()
{
	if (!m_Connected)
	{
		SetStatus ("daemon is not connected", true);
		return false;
	}
	if (m_TorrentTunnel < 0 || !ActiveTunnel ())
	{
		SetStatus ("no torrent tunnel is available", true);
		return false;
	}
	return true;
}

void App::SetStatus (std::string message, bool error)
{
	// Translate at the status boundary. Rendering only translated the exact
	// full line, but the status line is prefixed with "> "/"! " later, so a
	// catalog entry for a status message was unreachable.
	const auto localized = i2p::i18n::tr (std::string_view (message));
	if (localized != std::string_view (message)) message.assign (localized);
	m_StatusMessage = BoundUtf8 (SanitizeText (message), 1024);
	m_StatusError = error;
	m_StatusExpiry = std::chrono::steady_clock::now () + (error ? 20s : 8s);
	m_Dirty = true;
}

void App::RebuildTorrentView (int preferredId)
{
	auto filter = ToLowerAscii (m_TorrentFilter);
	m_TorrentView.clear ();
	for (size_t i = 0; i < m_Torrents.size (); ++i)
	{
		const auto & torrent = m_Torrents[i];
		if (!filter.empty ())
		{
			auto name = ToLowerAscii (torrent.name);
			auto hash = ToLowerAscii (torrent.hash);
			if (name.find (filter) == std::string::npos && hash.find (filter) == std::string::npos) continue;
		}
		m_TorrentView.push_back (i);
	}

	auto less = [&] (size_t leftIndex, size_t rightIndex)
	{
		const auto & left = m_Torrents[leftIndex];
		const auto & right = m_Torrents[rightIndex];
		int comparison = 0;
		switch (m_TorrentSort)
		{
		case TorrentSort::Name: comparison = ToLowerAscii (left.name).compare (ToLowerAscii (right.name)); break;
		case TorrentSort::Status: comparison = ToLowerAscii (left.status).compare (ToLowerAscii (right.status)); break;
		case TorrentSort::Progress:
			comparison = left.percent < right.percent ? -1 : (left.percent > right.percent ? 1 : 0);
			break;
		case TorrentSort::Down:
			comparison = left.downRate < right.downRate ? -1 : (left.downRate > right.downRate ? 1 : 0);
			break;
		case TorrentSort::Up:
			comparison = left.upRate < right.upRate ? -1 : (left.upRate > right.upRate ? 1 : 0);
			break;
		case TorrentSort::Peers: comparison = left.peers < right.peers ? -1 : (left.peers > right.peers ? 1 : 0); break;
		}
		if (comparison == 0) comparison = left.id < right.id ? -1 : (left.id > right.id ? 1 : 0);
		return m_TorrentSortDescending ? comparison > 0 : comparison < 0;
	};
	std::stable_sort (m_TorrentView.begin (), m_TorrentView.end (), less);

	int selection = 0;
	if (preferredId >= 0)
	{
		for (size_t i = 0; i < m_TorrentView.size (); ++i)
		{
			if (m_Torrents[m_TorrentView[i]].id == preferredId)
			{
				selection = static_cast<int> (i);
				break;
			}
		}
	}
	if (m_TorrentView.empty ())
		selection = 0;
	else
		selection = std::clamp (selection, 0, static_cast<int> (m_TorrentView.size ()) - 1);
	m_TorrentSelection = selection;
	m_TorrentScroll = std::clamp (m_TorrentScroll, 0, std::max (0, static_cast<int> (m_TorrentView.size ()) - 1));
	if (m_TorrentSelection < m_TorrentScroll) m_TorrentScroll = m_TorrentSelection;
}

void App::EnsureTorrentSelectionVisible (int visible)
{
	if (m_TorrentView.empty () || visible <= 0) return;
	m_TorrentSelection = std::clamp (m_TorrentSelection, 0, static_cast<int> (m_TorrentView.size ()) - 1);
	if (m_TorrentSelection < m_TorrentScroll) m_TorrentScroll = m_TorrentSelection;
	if (m_TorrentSelection >= m_TorrentScroll + visible) m_TorrentScroll = m_TorrentSelection - visible + 1;
	int maximum = std::max (0, static_cast<int> (m_TorrentView.size ()) - visible);
	m_TorrentScroll = std::clamp (m_TorrentScroll, 0, maximum);
}

int App::TorrentPageRows () const
{
	int height = 0;
	int width = 0;
	terminal::Dimensions (height, width);
	(void)width;
	int content = std::max (0, height - 4);
	return std::max (1, content - 1);
}

void App::EnterTorrentDetails ()
{
	auto torrent = SelectedTorrent ();
	if (!torrent)
	{
		SetStatus ("no torrent selected", true);
		return;
	}
	if (!RequireTorrentTunnel ()) return;
	m_TorrentDetails = true;
	m_TorrentDetail = TorrentDetail { };
	m_TorrentDetail.info = *torrent;
	m_TorrentDetail.loaded = false;
	m_TorrentDetailTab = TorrentDetailTab::General;
	m_TorrentDetailScroll = 0;
	m_TorrentCommentOffset = 0;
	m_NextTorrentDetail = std::chrono::steady_clock::now ();
	m_Dirty = true;
}

void App::LeaveTorrentDetails ()
{
	m_TorrentDetails = false;
	m_TorrentDetail = TorrentDetail { };
	m_TorrentCommentOffset = 0;
	m_Dirty = true;
}

void App::PageTorrentComments (int direction)
{
	if (!direction) return;
	if (!m_TorrentDetail.comments.contains ("comments.offset"))
	{
		// A daemon without page reporting would reject the offset argument;
		// say so instead of showing a remote bad-argument error.
		SetStatus ("this daemon does not report comment paging", true);
		return;
	}
	const uint64_t page = i2p::tui::MAX_TORRENT_COMMENT_RECORDS_PER_REQUEST;
	const uint64_t total = std::min<uint64_t> (ToU64 (MapValue (m_TorrentDetail.comments, "comments.total")),
		static_cast<uint64_t> (i2p::tui::MAX_TORRENT_COMMENT_OFFSET) + 1);
	int64_t next = static_cast<int64_t> (m_TorrentCommentOffset)
		+ (direction > 0 ? static_cast<int64_t> (page) : -static_cast<int64_t> (page));
	if (next < 0) next = 0;
	// The last page is the one that holds the oldest retained record; never
	// request an offset the parser would reject.
	const int64_t lastPage = total ? static_cast<int64_t> (((total - 1) / page) * page) : 0;
	if (next > lastPage) next = lastPage;
	if (next != m_TorrentCommentOffset)
	{
		m_TorrentCommentOffset = static_cast<int> (next);
		m_TorrentDetailScroll = 0;
		SetStatus (std::string (i2p::i18n::tr ("comments page from record "))
			+ std::to_string (m_TorrentCommentOffset + 1) + " " + std::string (i2p::i18n::tr ("of "))
				+ std::to_string (total),
			false);
	}
	// Refresh even when the page did not move: the retained set may have shrunk
	// and only the daemon knows the real offset.
	m_NextTorrentDetail = std::chrono::steady_clock::now ();
	m_Dirty = true;
}

bool App::TerminalTooSmall () const
{
	int height = 0, width = 0;
	terminal::Dimensions (height, width);
	return height < 6 || width < 40;
}

void App::BeginPrompt (PromptKind kind, std::string label, std::string initial)
{
	if (TerminalTooSmall ())
	{
		SetStatus ("Terminal is too small for an input prompt", true);
		return;
	}
	m_Prompt = kind;
	m_PendingTunnelName = m_TorrentTunnelName;
	const auto localized = i2p::i18n::tr (std::string_view (label));
	m_PromptLabel = localized == std::string_view (label) ? std::move (label) : std::string (localized);
	m_InputBuffer = std::move (initial);
	if (m_InputBuffer.size () > 2048) m_InputBuffer.resize (2048);
	m_Dirty = true;
}

bool App::HandlePromptKey (int key)
{
	if (TerminalTooSmall ())
	{
		// Never commit a destructive prompt that cannot be displayed.
		if (key == 27) CancelPrompt ();
		return true;
	}
	if (key == 27)
	{
		CancelPrompt ();
		return true;
	}
	if (key == '\n' || key == '\r' || key == terminal::KEY_ENTER)
	{
		CommitPrompt ();
		return true;
	}
	if (key == terminal::KEY_BACKSPACE || key == 127 || key == 8)
	{
		RemoveLastUtf8 (m_InputBuffer);
		m_Dirty = true;
		return true;
	}
	if (key == terminal::KEY_DC)
	{
		m_InputBuffer.clear ();
		m_Dirty = true;
		return true;
	}
	if (key >= 32 && key < 256 && key != 127)
	{
		if (m_InputBuffer.size () < 2048) m_InputBuffer.push_back (static_cast<char> (key));
		m_Dirty = true;
		return true;
	}
	// Modal input swallows navigation and action keys.
	return true;
}

void App::CommitPrompt ()
{
	auto kind = m_Prompt;
	auto tunnelName = std::move (m_PendingTunnelName);
	auto input = m_InputBuffer;
	m_Prompt = PromptKind::None;
	m_InputBuffer.clear ();
	m_Dirty = true;

	if (kind == PromptKind::Filter)
	{
		m_TorrentFilter = BoundUtf8 (Trim (input), 256);
		int preferred = CurrentSelectedId ();
		RebuildTorrentView (preferred);
		m_TorrentScroll = 0;
		SetStatus (m_TorrentFilter.empty ()
				? std::string (i2p::i18n::tr ("filter cleared"))
				: std::string (i2p::i18n::tr ("filter: ")) + m_TorrentFilter,
			false);
		return;
	}

	if (kind == PromptKind::LogFilter)
	{
		m_LogFilter = BoundUtf8 (Trim (input), 128);
		m_Scroll = 0;
		SetStatus (m_LogFilter.empty ()
				? std::string (i2p::i18n::tr ("log filter cleared"))
				: std::string (i2p::i18n::tr ("log filter: ")) + m_LogFilter,
			false);
		return;
	}

	if (kind == PromptKind::ConfirmTransit)
	{
		auto answer = ToLowerAscii (Trim (input));
		if (!IsAffirmative (answer))
		{
			SetStatus ("transit change cancelled", false);
			return;
		}
		SendAdminCommand (m_PendingTransit ? "ADMIN TRANSIT ON" : "ADMIN TRANSIT OFF");
		m_PendingTransit = false;
		return;
	}

	if (kind == PromptKind::ConfirmOnline)
	{
		auto answer = ToLowerAscii (Trim (input));
		if (!IsAffirmative (answer))
		{
			SetStatus ("transport state change cancelled", false);
			m_PendingOnline = false;
			return;
		}
		SendAdminCommand (m_PendingOnline ? "ADMIN ONLINE ON" : "ADMIN ONLINE OFF");
		m_PendingOnline = false;
		return;
	}

	if (kind == PromptKind::ConfirmReseed)
	{
		auto answer = ToLowerAscii (Trim (input));
		if (!IsAffirmative (answer))
		{
			SetStatus (std::string (i2p::i18n::tr ("reseed cancelled")), false);
			return;
		}
		const bool accepted = SendAdminCommand ("ADMIN RESEED");
		// The daemon starts the job and returns immediately; the typed outcome
		// distinguishes a new request from an already running or disabled one.
		if (m_LastAdminOutcome == "already-running")
			SetStatus (std::string (i2p::i18n::tr ("reseed is already running")), false);
		else if (m_LastAdminOutcome == "disabled")
			SetStatus (std::string (i2p::i18n::tr ("reseed is disabled by configuration")), true);
		else if (m_LastAdminOutcome == "started")
			SetStatus (std::string (i2p::i18n::tr ("reseed started")), false);
		else if (!accepted)
			SetStatus (std::string (i2p::i18n::tr ("reseed request failed")), true);
		return;
	}

	if (kind == PromptKind::ConfirmTunnelStart || kind == PromptKind::ConfirmTunnelStop)
	{
		auto answer = ToLowerAscii (Trim (input));
		const bool start = kind == PromptKind::ConfirmTunnelStart;
		const std::string name = m_PendingServiceName;
		m_PendingServiceName.clear ();
		if (!IsAffirmative (answer))
		{
			SetStatus (std::string (i2p::i18n::tr ("tunnel action cancelled")), false);
			return;
		}
		if (name.empty ()) return;
		const std::string command = (start ? "ADMIN TUNNEL-START " : "ADMIN TUNNEL-STOP ") + name;
		if (!SendAdminCommand (command))
		{
			const auto text = AdminOutcomeText (m_LastAdminOutcome);
			if (!text.empty ()) SetStatus (text, true);
			return;
		}
		const auto text = AdminOutcomeText (m_LastAdminOutcome);
		if (!text.empty ()) SetStatus (text, false);
		return;
	}

	if (kind == PromptKind::ConfirmReloadTunnels)
	{
		auto answer = ToLowerAscii (Trim (input));
		if (!IsAffirmative (answer))
		{
			SetStatus ("tunnel reload cancelled", false);
			return;
		}
		SendAdminCommand ("ADMIN RELOAD TUNNELS");
		return;
	}

	if (kind == PromptKind::ConfirmShutdownGraceful)
	{
		auto answer = ToLowerAscii (Trim (input));
		if (!IsAffirmative (answer))
		{
			SetStatus ("shutdown cancelled", false);
			return;
		}
		SendAdminCommand ("ADMIN SHUTDOWN GRACEFUL");
		return;
	}

	if (kind == PromptKind::ConfirmShutdownImmediate)
	{
		// Immediate shutdown is destructive to all active sessions, so the
		// confirmation is typed rather than a single key.
		if (Trim (input) != "SHUTDOWN")
		{
			SetStatus ("immediate shutdown cancelled", false);
			return;
		}
		SendAdminCommand ("ADMIN SHUTDOWN IMMEDIATE");
		return;
	}

	if (kind == PromptKind::Bandwidth)
	{
		auto value = Trim (input);
		bool valid = value.size () == 1
			&& (value[0] == 'K' || value[0] == 'L' || value[0] == 'M' || value[0] == 'N' || value[0] == 'O'
				|| value[0] == 'P' || value[0] == 'X');
		if (!valid)
		{
			SetStatus ("bandwidth class must be one of K/L/M/N/O/P/X", true);
			return;
		}
		SendAdminCommand ("ADMIN BANDWIDTH " + value);
		return;
	}

	if (kind == PromptKind::Share)
	{
		auto value = Trim (input);
		int percent = -1;
		auto parsed = std::from_chars (value.data (), value.data () + value.size (), percent);
		if (value.empty () || parsed.ec != std::errc () || parsed.ptr != value.data () + value.size () || percent < 0
			|| percent > 100)
		{
			SetStatus ("share ratio must be an integer from 0 to 100", true);
			return;
		}
		SendAdminCommand ("ADMIN SHARE " + std::to_string (percent));
		return;
	}

	if (kind == PromptKind::AddressAdd)
	{
		auto value = Trim (input);
		auto separator = value.find (' ');
		if (separator == std::string::npos)
		{
			SetStatus (std::string (i2p::i18n::tr ("address input must be: name.i2p base64-destination")), true);
			return;
		}
		auto name = Trim (value.substr (0, separator));
		auto destination = Trim (value.substr (separator + 1));
		if (name.empty () || destination.empty () || destination.find (' ') != std::string::npos
			|| name.size () > 255 || destination.size () > 1024)
		{
			SetStatus (std::string (i2p::i18n::tr ("address input must be: name.i2p base64-destination")), true);
			return;
		}
		// The daemon never re-points an existing name for ADDRESS-ADD. Ask
		// first when the bounded address-book snapshot already knows the name,
		// so the operator chooses the overwrite instead of discovering it
		// afterwards.
		if (m_AddressBookNames.contains (std::string (name)))
		{
			AskAddressReplace (std::string (name), std::string (destination));
			return;
		}
		if (!SendAdminCommand ("ADMIN ADDRESS-ADD " + std::string (name) + " " + std::string (destination)))
		{
			if (m_LastAdminOutcome == "exists")
			{
				// The snapshot was stale: the daemon knows the name. Offer the
				// explicit replacement instead of retrying the add.
				AskAddressReplace (std::string (name), std::string (destination));
				return;
			}
			return;
		}
		SetStatus (std::string (i2p::i18n::tr ("address added")), false);
		m_NextRefresh = std::chrono::steady_clock::now ();
		return;
	}

	if (kind == PromptKind::AddressReplace)
	{
		auto answer = ToLowerAscii (Trim (input));
		if (!IsAffirmative (answer))
		{
			SetStatus (std::string (i2p::i18n::tr ("address replace cancelled")), false);
			return;
		}
		// The pending pair was bounded and validated by the add prompt.
		if (m_PendingAddressName.empty () || m_PendingAddressDestination.empty ())
		{
			SetStatus (std::string (i2p::i18n::tr ("no address replacement pending")), true);
			return;
		}
		const std::string command
			= "ADMIN ADDRESS-REPLACE " + m_PendingAddressName + " " + m_PendingAddressDestination;
		if (SendAdminCommand (command)) SetStatus (std::string (i2p::i18n::tr ("address replaced")), false);
		return;
	}

	if (kind == PromptKind::AddressRemove)
	{
		auto name = Trim (input);
		if (name.empty () || name.find (' ') != std::string::npos)
		{
			SetStatus (std::string (i2p::i18n::tr ("address name must not be empty or contain spaces")), true);
			return;
		}
		SendAdminCommand ("ADMIN ADDRESS-REMOVE " + name);
		return;
	}

	if (kind == PromptKind::AddressBlock || kind == PromptKind::AddressUnblock)
	{
		auto name = Trim (input);
		if (name.empty () || name.find (' ') != std::string::npos)
		{
			SetStatus ("address pattern must not be empty or contain spaces", true);
			return;
		}
		SendAdminCommand (
			(kind == PromptKind::AddressBlock ? "ADMIN ADDRESS-BLOCK " : "ADMIN ADDRESS-UNBLOCK ") + name);
		return;
	}

	if (kind == PromptKind::AddressSubAdd || kind == PromptKind::AddressSubRemove)
	{
		auto url = Trim (input);
		// The address book lowercases the scheme; accept the same spellings here
		// instead of requiring one exact case at the prompt.
		auto hasScheme = [] (std::string_view value, std::string_view prefix)
		{
			if (value.size () < prefix.size ()) return false;
			for (size_t i = 0; i < prefix.size (); ++i)
				if (std::tolower (static_cast<unsigned char> (value[i])) != prefix[i]) return false;
			return true;
		};
		if (url.empty () || url.size () > i2p::tui::MAX_SUBSCRIPTION_URL_LENGTH || url.find (' ') != std::string::npos
			|| url.find ('#') != std::string::npos
			|| (!hasScheme (url, "http://") && !hasScheme (url, "https://")))
		{
			SetStatus ("subscription URL must be http(s)://host/path with no spaces or fragment", true);
			return;
		}
		SendAdminCommand ((kind == PromptKind::AddressSubAdd ? "ADMIN ADDRESS-SUB-ADD " : "ADMIN ADDRESS-SUB-REMOVE ")
			+ url);
		return;
	}

	if (kind == PromptKind::NetDbBan || kind == PromptKind::NetDbUnban)
	{
		auto hash = Trim (input);
		const bool valid = !hash.empty () && hash.size () <= i2p::tui::MAX_ROUTER_HASH_TEXT_LENGTH
			&& std::all_of (hash.begin (), hash.end (),
				[] (unsigned char ch)
				{
					return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9')
						|| ch == '+' || ch == '/' || ch == '=' || ch == '~' || ch == '-';
				});
		if (!valid)
		{
			SetStatus ("router hash must be the base64 identity hash", true);
			return;
		}
		SendAdminCommand ((kind == PromptKind::NetDbBan ? "ADMIN NETDB-BAN " : "ADMIN NETDB-UNBAN ") + hash);
		return;
	}

	if (kind == PromptKind::AddMagnet)
	{
		auto magnet = Trim (input);
		if (magnet.empty ())
		{
			SetStatus ("no magnet supplied", true);
			return;
		}
		if (magnet.size () > 2048)
		{
			SetStatus ("magnet is too long", true);
			return;
		}
		if (magnet.find ('\n') != std::string::npos || magnet.find ('\r') != std::string::npos)
		{
			SetStatus ("magnet contains a line break", true);
			return;
		}
		if (!RequireTorrentTunnel ()) return;
		SendTorrentCommand ("TORRENT-ADD-MAGNET " + std::to_string (m_TorrentTunnel) + " " + magnet);
		return;
	}

	if (kind == PromptKind::CreateTorrent || kind == PromptKind::CreateTorrentAdvanced)
	{
		const int tunnel = m_PendingTorrentTunnel;
		m_PendingTorrentTunnel = -1;
		auto source = Trim (input);
		if (source.empty () || source.size () > 2048
			|| std::any_of (source.begin (), source.end (), [] (unsigned char c) { return c < 32 || c == 127; }))
		{
			SetStatus ("invalid source name", true);
			return;
		}
		if (!RequireTorrentTunnel () || tunnel != m_TorrentTunnel || tunnelName != m_TorrentTunnelName)
		{
			SetStatus ("torrent session changed; creation cancelled", true);
			return;
		}
		// The daemon validates sizes, tracker tiers and paths. The terminal never opens the source.
		SendTorrentCommand (std::string (kind == PromptKind::CreateTorrent ? "TORRENT-CREATE " : "TORRENT-CREATE-V1 ")
			+ std::to_string (tunnel) + " " + source);
		return;
	}

	if (kind == PromptKind::AddFile)
	{
		if (input.empty () || Trim (input).empty ())
		{
			SetStatus ("no .torrent path supplied", true);
			return;
		}
		if (input.size () > 2048)
		{
			SetStatus ("path is too long", true);
			return;
		}
		if (input.find ('\n') != std::string::npos || input.find ('\r') != std::string::npos)
		{
			SetStatus ("path contains a line break", true);
			return;
		}
		if (!RequireTorrentTunnel ()) return;
		// The daemon validates and reads the file; the TUI never opens it.
		SendTorrentCommand ("TORRENT-ADD-FILE " + std::to_string (m_TorrentTunnel) + " " + input);
		return;
	}

	if (kind == PromptKind::FilePriority || kind == PromptKind::CommentsToggle || kind == PromptKind::CommentAdd
		|| kind == PromptKind::CommentHide || kind == PromptKind::CommentUnhide
		|| kind == PromptKind::TorrentMetaComment || kind == PromptKind::TorrentMetaCreatedBy)
	{
		const auto id = m_PendingTorrentId;
		const auto tunnel = m_PendingTorrentTunnel;
		auto hash = std::move (m_PendingTrackerHash);
		m_PendingTorrentId = m_PendingTorrentTunnel = -1;
		m_PendingTorrentName.clear ();
		bool present = false;
		if (tunnel == m_TorrentTunnel && tunnelName == m_TorrentTunnelName && !hash.empty ())
			for (const auto & torrent : m_Torrents)
				if (torrent.id == id && torrent.hash == hash) present = true;
		if (!present)
		{
			SetStatus ("selected torrent changed; edit cancelled", true);
			return;
		}
		auto value = Trim (input);
		bool valid = !value.empty () && value.size () <= 2048;
		for (unsigned char ch : value)
			if (ch < 32 || ch == 127) valid = false;
		auto digits = [] (std::string_view text)
		{
			return !text.empty () && text.size () <= 20
				&& std::all_of (text.begin (), text.end (), [] (char c) { return c >= '0' && c <= '9'; });
		};
		std::string command;
		if (kind == PromptKind::FilePriority)
		{
			auto split = value.find (' ');
			valid = valid && split != std::string::npos && digits (value.substr (0, split))
				&& split + 2 == value.size () && value.back () >= '0' && value.back () <= '2';
			command = "TORRENT-FILE-PRIORITY ";
		}
		else if (kind == PromptKind::CommentsToggle)
		{
			valid = valid && (value == "ON" || value == "OFF");
			command = "TORRENT-COMMENTS ";
		}
		else if (kind == PromptKind::CommentHide || kind == PromptKind::CommentUnhide)
		{
			valid = valid && digits (value);
			command = kind == PromptKind::CommentHide ? "TORRENT-COMMENT-HIDE " : "TORRENT-COMMENT-UNHIDE ";
		}
		else if (kind == PromptKind::TorrentMetaComment || kind == PromptKind::TorrentMetaCreatedBy)
		{
			valid = valid && value.size () <= (kind == PromptKind::TorrentMetaComment ? 1024u : 255u);
			command = kind == PromptKind::TorrentMetaComment ? "TORRENT-META-COMMENT " : "TORRENT-META-CREATED-BY ";
		}
		else
		{
			auto separator = value.find ('|');
			valid = valid && value.size () >= 4 && value[0] >= '0' && value[0] <= '5' && value[1] == ' '
				&& separator != std::string::npos && separator >= 2
				&& Utf16Units (value.substr (2, separator - 2)) <= 32
				&& Utf16Units (value.substr (separator + 1)) <= 512;
			command = "TORRENT-COMMENT-ADD ";
		}
		if (!valid)
		{
			SetStatus ("invalid edit; check the prompt format and size limits", true);
			return;
		}
		if (kind == PromptKind::CommentHide || kind == PromptKind::CommentUnhide)
		{
			// Moderation changes the retained local store, so confirm it
			// explicitly. The torrent id/tunnel/hash captured above stay
			// pending and are re-validated when the confirmation arrives.
			const bool hide = kind == PromptKind::CommentHide;
			m_PendingTorrentId = id;
			m_PendingTorrentTunnel = tunnel;
			m_PendingTrackerHash = std::move (hash);
			m_PendingCommentId = value;
			BeginPrompt (hide ? PromptKind::ConfirmCommentHide : PromptKind::ConfirmCommentUnhide,
				std::string (hide ? "hide local comment " : "unhide local comment ") + value + "? [y/N] ");
			return;
		}
		SendTorrentCommand (command + std::to_string (tunnel) + " " + std::to_string (id) + " " + value);
		return;
	}
	if (kind == PromptKind::AddTracker || kind == PromptKind::RemoveTracker)
	{
		const int id = m_PendingTorrentId;
		const int tunnel = m_PendingTorrentTunnel;
		auto hash = std::move (m_PendingTrackerHash);
		m_PendingTorrentId = -1;
		m_PendingTorrentTunnel = -1;
		m_PendingTorrentName.clear ();
		if (!RequireTorrentTunnel ()) return;
		bool present = tunnel == m_TorrentTunnel && tunnelName == m_TorrentTunnelName && !hash.empty ();
		if (present)
		{
			present = false;
			for (const auto & torrent : m_Torrents)
				if (torrent.id == id && torrent.hash == hash)
				{
					present = true;
					break;
				}
		}
		if (!present)
		{
			SetStatus ("selected torrent changed; tracker edit cancelled", true);
			return;
		}
		auto url = Trim (input);
		if (url.empty () || url.size () > 2048)
		{
			SetStatus ("supply a tracker URL of at most 2048 bytes", true);
			return;
		}
		for (unsigned char ch : url)
			if (ch <= 0x20 || ch == 0x7F)
			{
				SetStatus ("tracker URL contains whitespace or a control character", true);
				return;
			}
		// Only the daemon parses and authorizes the I2P URL. No DNS, tracker
		// connection or torrent-file access occurs in this terminal process.
		SendTorrentCommand (
			std::string (kind == PromptKind::AddTracker ? "TORRENT-TRACKER-ADD " : "TORRENT-TRACKER-REMOVE ")
			+ std::to_string (tunnel) + " " + std::to_string (id) + " " + url);
		return;
	}

	if (kind == PromptKind::ConfirmCommentHide || kind == PromptKind::ConfirmCommentUnhide)
	{
		const bool hide = kind == PromptKind::ConfirmCommentHide;
		const auto answer = ToLowerAscii (Trim (input));
		const int id = m_PendingTorrentId;
		const int tunnel = m_PendingTorrentTunnel;
		auto hash = std::move (m_PendingTrackerHash);
		auto commentId = std::move (m_PendingCommentId);
		m_PendingTorrentId = -1;
		m_PendingTorrentTunnel = -1;
		m_PendingTorrentName.clear ();
		m_PendingCommentId.clear ();
		if (!IsAffirmative (answer))
		{
			SetStatus ("comment moderation cancelled", false);
			return;
		}
		if (!RequireTorrentTunnel () || tunnel != m_TorrentTunnel || tunnelName != m_TorrentTunnelName)
		{
			SetStatus ("torrent session changed; comment moderation cancelled", true);
			return;
		}
		bool present = false;
		if (id >= 0 && !hash.empty ())
			for (const auto & torrent : m_Torrents)
				if (torrent.id == id && torrent.hash == hash) present = true;
		if (!present)
		{
			SetStatus ("selected torrent changed; comment moderation cancelled", true);
			return;
		}
		const bool digits = !commentId.empty () && commentId.size () <= 20
			&& std::all_of (commentId.begin (), commentId.end (), [] (char ch) { return ch >= '0' && ch <= '9'; });
		if (!digits)
		{
			SetStatus ("invalid comment ID", true);
			return;
		}
		SendTorrentCommand (std::string (hide ? "TORRENT-COMMENT-HIDE " : "TORRENT-COMMENT-UNHIDE ")
			+ std::to_string (tunnel) + " " + std::to_string (id) + " " + commentId);
		return;
	}

	if (kind == PromptKind::ConfirmRemove || kind == PromptKind::ConfirmRemoveFiles)
	{
		auto answer = ToLowerAscii (Trim (input));
		bool confirmed = kind == PromptKind::ConfirmRemoveFiles ? answer == "yes" : (answer == "y" || answer == "yes");
		if (!confirmed)
		{
			SetStatus ("removal cancelled", false);
			m_PendingTorrentId = -1;
			m_PendingTorrentTunnel = -1;
			m_PendingTorrentName.clear ();
			return;
		}
		if (!RequireTorrentTunnel ())
		{
			m_PendingTorrentId = -1;
			m_PendingTorrentTunnel = -1;
			m_PendingTorrentName.clear ();
			return;
		}
		bool present = false;
		if (m_PendingTorrentId >= 0 && m_PendingTorrentTunnel == m_TorrentTunnel && tunnelName == m_TorrentTunnelName)
		{
			for (const auto & torrent : m_Torrents)
			{
				// The id alone is not enough: a reloaded session can reuse
				// ids. Match the name captured when the prompt was opened.
				if (torrent.id == m_PendingTorrentId && torrent.name == m_PendingTorrentName)
				{
					present = true;
					break;
				}
			}
		}
		if (!present)
		{
			SetStatus ("selected torrent changed; removal cancelled", true);
			m_PendingTorrentId = -1;
			m_PendingTorrentTunnel = -1;
			m_PendingTorrentName.clear ();
			return;
		}
		std::string command
			= "TORRENT-REMOVE " + std::to_string (m_TorrentTunnel) + " " + std::to_string (m_PendingTorrentId);
		if (kind == PromptKind::ConfirmRemoveFiles) command += " files";
		m_PendingTorrentId = -1;
		m_PendingTorrentTunnel = -1;
		m_PendingTorrentName.clear ();
		SendTorrentCommand (command);
		return;
	}

	if (kind == PromptKind::ConfirmRemoveAll || kind == PromptKind::ConfirmRemoveAllFiles)
	{
		auto answer = ToLowerAscii (Trim (input));
		bool confirmed = kind == PromptKind::ConfirmRemoveAllFiles ? answer == "yes"
																   : (answer == "y" || answer == "yes");
		if (!confirmed)
		{
			SetStatus ("removal cancelled", false);
			m_PendingTorrentId = -1;
			m_PendingTorrentTunnel = -1;
			m_PendingTorrentName.clear ();
			return;
		}
		if (!RequireTorrentTunnel ())
		{
			m_PendingTorrentId = -1;
			m_PendingTorrentTunnel = -1;
			m_PendingTorrentName.clear ();
			return;
		}
		// The prompt is only valid for the session it was opened on: the
		// snapshot may have been reloaded or switched meanwhile.
		if (m_PendingTorrentTunnel != m_TorrentTunnel || tunnelName != m_TorrentTunnelName)
		{
			SetStatus ("torrent session changed; removal cancelled", true);
			m_PendingTorrentId = -1;
			m_PendingTorrentTunnel = -1;
			m_PendingTorrentName.clear ();
			return;
		}
		if (m_Torrents.empty ())
		{
			SetStatus ("no torrent to remove", true);
			m_PendingTorrentId = -1;
			m_PendingTorrentTunnel = -1;
			m_PendingTorrentName.clear ();
			return;
		}
		std::string command = "TORRENT-REMOVE-ALL " + std::to_string (m_TorrentTunnel);
		if (kind == PromptKind::ConfirmRemoveAllFiles) command += " files";
		m_PendingTorrentId = -1;
		m_PendingTorrentTunnel = -1;
		m_PendingTorrentName.clear ();
		SendTorrentCommand (command);
		return;
	}
}

void App::CancelPrompt ()
{
	m_Prompt = PromptKind::None;
	m_PendingTunnelName.clear ();
	m_InputBuffer.clear ();
	m_PendingTorrentId = -1;
	m_PendingTorrentTunnel = -1;
	m_PendingTorrentName.clear ();
	m_PendingTransit = false;
	m_PendingTrackerHash.clear ();
	m_PendingCommentId.clear ();
	SetStatus ("cancelled", false);
}

void App::ToggleSelected ()
{
	auto torrent = SelectedTorrent ();
	if (!torrent)
	{
		SetStatus ("no torrent selected", true);
		return;
	}
	if (!RequireTorrentTunnel ()) return;
	bool stop = IsActiveStatus (torrent->status) && !IsStoppedStatus (torrent->status);
	std::string command = std::string (stop ? "TORRENT-PAUSE " : "TORRENT-RESUME ") + std::to_string (m_TorrentTunnel)
		+ " " + std::to_string (torrent->id);
	SendTorrentCommand (command);
}

void App::ReannounceSelected ()
{
	auto torrent = SelectedTorrent ();
	if (!torrent)
	{
		SetStatus ("no torrent selected", true);
		return;
	}
	if (!RequireTorrentTunnel ()) return;
	SendTorrentCommand ("TORRENT-REANNOUNCE " + std::to_string (m_TorrentTunnel) + " " + std::to_string (torrent->id));
}

void App::RemoveSelected (bool files)
{
	auto torrent = SelectedTorrent ();
	if (!torrent)
	{
		SetStatus ("no torrent selected", true);
		return;
	}
	if (!RequireTorrentTunnel ()) return;
	m_PendingTorrentId = torrent->id;
	m_PendingTorrentTunnel = m_TorrentTunnel;
	m_PendingTorrentName = torrent->name;
	auto name = TruncateUtf8 (SanitizeText (torrent->name), 60);
	if (files)
		BeginPrompt (PromptKind::ConfirmRemoveFiles,
			i2p::i18n::tr ("Type YES to remove \"%s\" and local data: ", name.c_str ()));
	else
		BeginPrompt (PromptKind::ConfirmRemove, i2p::i18n::tr ("Remove \"%s\"? [y/N] ", name.c_str ()));
}

void App::RemoveAllTorrents (bool files)
{
	if (!RequireTorrentTunnel ()) return;
	if (m_Torrents.empty ())
	{
		SetStatus ("no torrent to remove", true);
		return;
	}
	// Session-wide removal is bound to the tunnel that owns the list, so a
	// tunnel switch while the prompt is open must cancel it.
	m_PendingTorrentId = -1;
	m_PendingTorrentTunnel = m_TorrentTunnel;
	m_PendingTorrentName.clear ();
	// The snapshot is bounded, so the prompt names the session instead of a
	// count that could understate a truncated list.
	auto session = TruncateUtf8 (SanitizeText (m_TorrentTunnelName), 60);
	if (files)
		BeginPrompt (PromptKind::ConfirmRemoveAllFiles,
			i2p::i18n::tr ("Type YES to remove all torrents of \"%s\" and local data: ", session.c_str ()));
	else
		BeginPrompt (PromptKind::ConfirmRemoveAll,
			i2p::i18n::tr ("Remove all torrents of \"%s\"? [y/N] ", session.c_str ()));
}

void App::CycleTorrentSort ()
{
	switch (m_TorrentSort)
	{
	case TorrentSort::Name:
		m_TorrentSort = TorrentSort::Status;
		m_TorrentSortDescending = false;
		break;
	case TorrentSort::Status:
		m_TorrentSort = TorrentSort::Progress;
		m_TorrentSortDescending = true;
		break;
	case TorrentSort::Progress:
		m_TorrentSort = TorrentSort::Down;
		m_TorrentSortDescending = true;
		break;
	case TorrentSort::Down:
		m_TorrentSort = TorrentSort::Up;
		m_TorrentSortDescending = true;
		break;
	case TorrentSort::Up:
		m_TorrentSort = TorrentSort::Peers;
		m_TorrentSortDescending = true;
		break;
	case TorrentSort::Peers:
		m_TorrentSort = TorrentSort::Name;
		m_TorrentSortDescending = false;
		break;
	}
	int preferred = CurrentSelectedId ();
	RebuildTorrentView (preferred);
	SetStatus (std::string (i2p::i18n::tr ("sort: ")) + std::string (i2p::i18n::tr (SortName (m_TorrentSort)))
			+ (m_TorrentSortDescending ? " " + std::string (i2p::i18n::tr ("desc"))
									   : " " + std::string (i2p::i18n::tr ("asc"))),
		false);
}

void App::ToggleTorrentSortDirection ()
{
	m_TorrentSortDescending = !m_TorrentSortDescending;
	int preferred = CurrentSelectedId ();
	RebuildTorrentView (preferred);
	SetStatus (std::string (i2p::i18n::tr ("sort: ")) + std::string (i2p::i18n::tr (SortName (m_TorrentSort)))
			+ (m_TorrentSortDescending ? " " + std::string (i2p::i18n::tr ("desc"))
									   : " " + std::string (i2p::i18n::tr ("asc"))),
		false);
}

void App::CycleTorrentTunnel (int delta)
{
	if (m_TorrentTunnels.empty ())
	{
		m_TorrentTunnel = -1;
		m_TorrentTunnelName.clear ();
		SetStatus ("no torrent tunnel is available", true);
		return;
	}
	int position = 0;
	for (size_t i = 0; i < m_TorrentTunnels.size (); ++i)
	{
		if (m_TorrentTunnels[i].index == m_TorrentTunnel)
		{
			position = static_cast<int> (i);
			break;
		}
	}
	int count = static_cast<int> (m_TorrentTunnels.size ());
	position = (position + delta % count + count) % count;
	m_TorrentTunnel = m_TorrentTunnels[position].index;
	m_TorrentTunnelName = m_TorrentTunnels[position].name;
	m_Torrents.clear ();
	m_TorrentView.clear ();
	m_TorrentsLoaded = false;
	m_TorrentDetails = false;
	m_TorrentDetail = TorrentDetail { };
	m_TorrentSelection = 0;
	m_TorrentScroll = 0;
	ResetTorrentAuxState ();
	m_NextTorrentList = std::chrono::steady_clock::now ();
	m_NextTorrentSession = m_NextTorrentList;
	m_NextTorrentDht = m_NextTorrentList;
	m_NextTorrentDetail = m_NextTorrentList;
	SetStatus (std::string (i2p::i18n::tr ("tunnel: ")) + m_TorrentTunnels[position].name, false);
}

void App::PutLine (int row, const std::string & text, int width, int attributes)
{
	terminal::Move (row, 0);
	terminal::ClearToEol ();
	if (attributes) terminal::SetAttribute (attributes);
	// Translate exact catalog entries at the rendering boundary. Lines with
	// leading layout spaces keep the prefix while the visible text is looked
	// up without it; composed strings remain unchanged unless their full text
	// has a catalog entry.
	std::string translated (i2p::i18n::tr (text));
	if (translated == text)
	{
		const auto first = text.find_first_not_of (' ');
		if (first != std::string::npos && first > 0)
		{
			const std::string_view core (text.data () + first, text.size () - first);
			const auto localized = i2p::i18n::tr (core);
			if (localized != core) translated = std::string (text.data (), first) + std::string (localized);
		}
	}
	auto safe = TruncateUtf8 (SanitizeText (translated), width, true);
	if (!safe.empty ()) terminal::Put (safe);
	if (attributes) terminal::SetAttribute (terminal::ATTR_NORMAL);
}

void App::DrawHeader (int width)
{
	PutLine (0, " openi2pd TUI ", width, m_Theme.Attributes (ThemeRole::Title));

	auto keyFor = [] (size_t index) { return std::string (TAB_KEYS[std::min (index, TAB_KEYS.size () - 1)]); };
	auto active = static_cast<size_t> (m_Tab);
	const int column = 18;
	if (column >= width) return;

	// Narrow terminals get the active view instead of a truncated row of
	// unreachable tab labels. Tab/Left/Right still cycle every view.
	if (width < 112)
	{
		std::string label = " " + keyFor (active) + ":" + std::string (i2p::i18n::tr (TAB_NAMES[active])) + " ("
			+ std::to_string (active + 1) + "/" + std::to_string (TAB_NAMES.size ()) + ") ";
		terminal::SetAttribute (m_Theme.Attributes (ThemeRole::SelectedTab));
		terminal::Move (0, column);
		terminal::Put (label, width - column);
		terminal::SetAttribute (terminal::ATTR_NORMAL);
		return;
	}

	const bool compact = width < 150;
	int position = column;
	for (size_t i = 0; i < TAB_NAMES.size (); ++i)
	{
		if (position >= width) break;
		const auto name = compact ? TAB_SHORT_NAMES[i] : TAB_NAMES[i];
		std::string label = " " + keyFor (i) + ":" + std::string (i2p::i18n::tr (name)) + " ";
		terminal::SetAttribute (m_Theme.Attributes (i == active ? ThemeRole::SelectedTab : ThemeRole::HeaderBar));
		terminal::Move (0, position);
		terminal::Put (label, width - position);
		position += DisplayWidth (label);
	}
	terminal::SetAttribute (terminal::ATTR_NORMAL);
}

void App::DrawFooter (int height, int width)
{
	std::string line = " ";
	line += m_Connected ? i2p::i18n::tr ("connected") : i2p::i18n::tr ("disconnected");
	if (!m_Error.empty ()) line += " | " + m_Error;
	line += " | socket " + m_Options.socketPath;
	if (m_Tab == Tab::Torrents)
		line += i2p::i18n::tr (" | Enter detail Esc/q back a/A add n/N create t/T trackers p pause/resume R reannounce "
							  "V recheck d/D remove Ctrl-X/Ctrl-D remove all x/z start/stop all / search s sort ? help");
	else if (m_Tab == Tab::Config)
		line += i2p::i18n::tr (" | read-only | Up/Down scroll | q quit, Tab/0-9/v switch, r refresh, ? help");
	else if (m_Tab == Tab::Services)
		line += i2p::i18n::tr (" | v services, Up/Down scroll | q quit, Tab/0-9/v switch, r refresh, ? help");
	else if (m_Tab == Tab::Status)
		line += i2p::i18n::tr (" | Up/Down scroll e reseed o online H reload s/S shutdown c cancel b bandwidth share percent "
							  "t transit | q quit, Tab/0-9 switch, r refresh, ? help");
	else if (m_Tab == Tab::Connections)
		line += i2p::i18n::tr (" | t transit | q quit, Tab/0-9 switch, r refresh, ? help");
	else if (m_Tab == Tab::NetDb)
		line += i2p::i18n::tr (" | n routers/leasesets b/u ban/unban e reseed | q quit, Tab/0-9 switch, r refresh, ? help");
	else if (m_Tab == Tab::Logs)
		line += i2p::i18n::tr (" | / filter c clear l level L daemon level O reopen log | q quit, Tab/0-9 switch, ? help");
	else if (m_Tab == Tab::Destinations)
		line += i2p::i18n::tr (" | a add d remove b/u block/unblock s/x subscriptions | q quit, Tab/0-9 switch, r refresh, ? help");
	else
		line += i2p::i18n::tr (" | q quit, Tab/0-9 switch, r refresh, ? help");
	PutLine (height - 1, line, width, m_Theme.Attributes (ThemeRole::StatusBar));
}

void App::DrawTorrentContext (int width)
{
	std::string line = " " + std::string (i2p::i18n::tr ("Torrents")) + ": ";
	if (!m_Connected)
	{
		line += i2p::i18n::tr ("daemon disconnected, reconnecting");
	}
	else if (m_TorrentTunnels.empty ())
	{
		line += i2p::i18n::tr ("no torrent service tunnel");
	}
	else
	{
		auto tunnel = ActiveTunnel ();
		size_t position = 0;
		for (size_t i = 0; i < m_TorrentTunnels.size (); ++i)
			if (m_TorrentTunnels[i].index == m_TorrentTunnel) position = i;
		line += std::string (i2p::i18n::tr ("tunnel ")) + (tunnel ? tunnel->name : std::string ("?")) + " ["
			+ std::to_string (m_TorrentTunnel) + "] " + std::to_string (position + 1) + "/"
			+ std::to_string (m_TorrentTunnels.size ());
		line += " | " + std::string (i2p::i18n::tr ("sort ")) + std::string (i2p::i18n::tr (SortName (m_TorrentSort)))
			+ (m_TorrentSortDescending ? " " + std::string (i2p::i18n::tr ("desc"))
									   : " " + std::string (i2p::i18n::tr ("asc")));
		if (!m_TorrentFilter.empty ()) line += " | " + std::string (i2p::i18n::tr ("filter "))
			+ TruncateUtf8 (m_TorrentFilter, 40);
		line += " | ";
		if (m_TorrentSessionLoaded)
			line += std::string (i2p::i18n::tr ("session down ")) + HumanBytes (m_TorrentSession.downloaded)
				+ " " + std::string (i2p::i18n::tr ("up")) + " " + HumanBytes (m_TorrentSession.uploaded) + " "
				+ std::string (i2p::i18n::tr ("active")) + " " + std::to_string (m_TorrentSession.active) + " "
				+ std::string (i2p::i18n::tr ("paused")) + " " + std::to_string (m_TorrentSession.paused);
		else
			line += i2p::i18n::tr ("session loading");
		line += " | DHT ";
		if (!m_TorrentDhtLoaded)
			line += i2p::i18n::tr ("loading");
		else if (!m_TorrentDht.supported)
			line += i2p::i18n::tr ("unsupported");
		else
			line += std::to_string (m_TorrentDht.numNodes) + " " + ntr ("node", "nodes", m_TorrentDht.numNodes)
				+ ", " + std::to_string (m_TorrentDht.numBuckets) + " "
				+ ntr ("bucket", "buckets", m_TorrentDht.numBuckets) + ", "
				+ std::to_string (m_TorrentDht.numTorrents) + " "
				+ ntr ("torrent", "torrents", m_TorrentDht.numTorrents);
	}
	PutLine (1, line, width, m_Theme.Attributes (ThemeRole::Title));
}

void App::DrawStatus (int top, int height, int width)
{
	int row = top;
	if (!m_StateLoaded && m_Connected)
	{
		PutLine (row, "  " + std::string (i2p::i18n::tr ("waiting for the first router state snapshot...")), width,
			m_Theme.Attributes (ThemeRole::Muted));
		return;
	}
	// The Status page has more rows than a 24-line terminal. Scroll over the
	// fixed rows; the bandwidth history is only drawn on the first page so a
	// scrolled view never hides a status field behind it.
	const int scroll = std::clamp (m_Scroll, 0, 25);
	int lineIndex = 0;
	auto show = [&] (std::string_view label, std::string_view value)
	{
		if (lineIndex++ < scroll) return;
		if (row >= top + height) return;
		terminal::Move (row, 1);
		terminal::SetAttribute (m_Theme.Attributes (ThemeRole::Primary));
		terminal::Put (FitUtf8 (i2p::i18n::tr (label), 26), 26);
		terminal::SetAttribute (terminal::ATTR_NORMAL);
		auto text = TruncateUtf8 (SanitizeText (value), width > 26 ? width - 26 : 0);
		if (!text.empty ()) terminal::Put (text);
		++row;
	};

	show ("Router identity", Field ("router.hash"));
	show ("Version / net ID", Field ("router.version") + " / " + Field ("router.netid") + " (protocol "
			+ Field ("protocol") + ")");
	show ("Uptime", HumanUptime (ToLong (Field ("router.uptime"))));
	show ("Network status (IPv4)", Field ("network.status4") + " " + Field ("network.error4"));
	show ("Network status (IPv6)", Field ("network.status6") + " " + Field ("network.error6"));
	show ("Capabilities", Field ("router.caps"));
	show ("Floodfill", Field ("router.floodfill") == "1" ? "yes" : "no");
	show ("NAT / testing",
		std::string ("v4 ")
			+ std::string (Field ("network.testing4") == "1" ? i2p::i18n::tr ("testing") : i2p::i18n::tr ("ok"))
			+ ", v6 "
			+ std::string (Field ("network.testing6") == "1" ? i2p::i18n::tr ("testing") : i2p::i18n::tr ("ok")));
	show ("NTCP2 / SSU2",
		std::string (Field ("transport.ntcp2") == "1" ? "NTCP2" : "-") + " / "
			+ (Field ("transport.ssu2") == "1" ? "SSU2" : "-"));
	show ("Transport online", Field ("transport.online") == "1" ? "yes" : "no");
	show ("Bandwidth in / out",
		HumanRate (ToU64 (Field ("bandwidth.in"))) + " / " + HumanRate (ToU64 (Field ("bandwidth.out"))));
	show ("Bandwidth 15s in / out",
		HumanRate (ToU64 (Field ("bandwidth.in15"))) + " / " + HumanRate (ToU64 (Field ("bandwidth.out15"))));
	show ("Transferred",
		HumanBytes (ToU64 (Field ("traffic.received"))) + " " + std::string (i2p::i18n::tr ("received")) + ", "
			+ HumanBytes (ToU64 (Field ("traffic.sent"))) + " " + std::string (i2p::i18n::tr ("sent")));
	show ("Transit bandwidth", HumanRate (ToU64 (Field ("bandwidth.transit"))));
	show ("Transit transferred", HumanBytes (ToU64 (Field ("traffic.transit"))));
	show ("Peers / connections", Field ("peers.count") + " / " + Field ("connections.count"));
	show ("Routers / floodfills", Field ("netdb.routers") + " / " + Field ("netdb.floodfills"));
	show ("LeaseSets", Field ("netdb.leasesets"));
	show ("Tunnels in / out / transit",
		Field ("tunnels.inbound") + " / " + Field ("tunnels.outbound") + " / " + Field ("tunnels.transit"));
	show ("Transit tunnels",
		Field ("router.accepts_tunnels") == "1" ? i2p::i18n::tr ("accepted (t to change)")
												: i2p::i18n::tr ("declined (t to change)"));
	show ("Tunnel creation success", Field ("tunnels.success") + "%");
	show ("Tunnel queues", Field ("tunnels.queue") + " data / " + Field ("tunnels.build_queue") + " build");
	show ("Reseeding", Field ("netdb.reseeding") == "1" ? "active" : "idle");
	show ("Daemon log level", m_DaemonLogLevel);
	show ("Log queue drops", Field ("log.dropped"));
	show ("Max bandwidth", HumanBytes (ToLong (Field ("router.bandwidth_limit")) * 1024) + "/s");
	show ("Local time", Timestamp (static_cast<int64_t> (std::time (nullptr))));
	if (scroll == 0) DrawRateHistory (row, top, height, width);
}

void App::DrawRateHistory (int & row, int top, int height, int width)
{
	if (m_InHistory.empty () || width < 28 || row + 4 > top + height) return;
	PutLine (row++, "", width);
	PutLine (row++, " Bandwidth history (15-second samples)", width, m_Theme.Attributes (ThemeRole::Muted));
	static constexpr std::array<const char *, 8> blocks = { " ", "\xE2\x96\x81", "\xE2\x96\x82", "\xE2\x96\x83",
		"\xE2\x96\x84", "\xE2\x96\x85", "\xE2\x96\x86", "\xE2\x96\x87" };
	uint32_t maximum = 1;
	for (auto value : m_InHistory) maximum = std::max (maximum, value);
	for (auto value : m_OutHistory) maximum = std::max (maximum, value);
	auto draw = [&] (std::string_view label, const std::deque<uint32_t> & history, ThemeRole role)
	{
		const int available = std::max (0, width - 9);
		if (available <= 0) return;
		const size_t start = history.size () > static_cast<size_t> (available) ? history.size () - available : 0;
		std::string line = "  " + std::string (i2p::i18n::tr (label)) + " ";
		for (size_t i = start; i < history.size (); ++i)
		{
			const uint64_t scaled = static_cast<uint64_t> (history[i]) * 7 / maximum;
			const size_t index = history[i] ? static_cast<size_t> (std::max<uint64_t> (1, scaled)) : 0;
			line.append (blocks[std::min (index, blocks.size () - 1)]);
		}
		PutLine (row++, line, width, m_Theme.Attributes (role));
	};
	draw ("in ", m_InHistory, ThemeRole::TorrentDownload);
	draw ("out", m_OutHistory, ThemeRole::TorrentUpload);
}

void App::DrawPeers (int top, int height, int width)
{
	int row = top;
	std::string title = "  " + std::string (i2p::i18n::tr ("Known peers: ")) + std::to_string (m_Peers.size ())
		+ " " + std::string (i2p::i18n::tr ("(bounded snapshot, Up/Down scroll)"));
	if (!m_Connected) title += " " + std::string (i2p::i18n::tr ("[stale]"));
	PutLine (row++, title, width, m_Theme.Attributes (ThemeRole::Primary));
	std::string header = "  " + std::string (i2p::i18n::tr (
		"Peer hash                                      Sess Attempts Transports            High"));
	PutLine (row++, header, width, m_Theme.Attributes (ThemeRole::Primary));
	if (m_Peers.empty ())
		PutLine (row, m_PeersLoaded ? "  " + std::string (i2p::i18n::tr ("no known peers"))
								   : "  " + std::string (i2p::i18n::tr ("waiting for the first peer snapshot")),
			width, m_Theme.Attributes (ThemeRole::Muted));
	const int visible = std::max (0, height - 2);
	const int maximum = std::max (0, static_cast<int> (m_Peers.size ()) - visible);
	m_Scroll = std::clamp (m_Scroll, 0, maximum);
	const int start = maximum - m_Scroll;
	for (int i = start; i < static_cast<int> (m_Peers.size ()) && row < top + height; ++i)
	{
		const auto & peer = m_Peers[i];
		std::string line = "  " + FitUtf8 (peer.hash, 46) + FitUtf8 (std::to_string (peer.sessions), 5, true)
			+ FitUtf8 (std::to_string (peer.attempts), 9, true) + " " + FitUtf8 (peer.transportNames, 20)
			+ FitUtf8 (peer.highBandwidth ? "yes" : "", 5);
		PutLine (row++, line, width);
	}
}

void App::DrawConnections (int top, int height, int width)
{
	int row = top;
	std::string title = "  " + std::string (i2p::i18n::tr ("Active transport connections: "))
		+ std::to_string (m_Connections.size ());
	if (!m_Connected)
		title = "  " + std::string (i2p::i18n::tr ("Active transport connections: daemon disconnected"));
	PutLine (row++, title, width, m_Theme.Attributes (ThemeRole::Primary));
	PutLine (row++, "  " + std::string (i2p::i18n::tr (
		"Peer hash                                      Sess Attempts Transports            High")), width,
		m_Theme.Attributes (ThemeRole::Primary));
	if (m_Connections.empty ())
	{
		PutLine (row, m_Connected ? "  " + std::string (i2p::i18n::tr ("no established transport connections"))
								 : "  " + std::string (i2p::i18n::tr ("waiting for daemon")),
			width, m_Theme.Attributes (ThemeRole::Muted));
		return;
	}
	const int visible = std::max (0, height - 2);
	const int maximum = std::max (0, static_cast<int> (m_Connections.size ()) - visible);
	m_Scroll = std::clamp (m_Scroll, 0, maximum);
	for (int i = m_Scroll; i < static_cast<int> (m_Connections.size ()) && row < top + height; ++i)
	{
		const auto & peer = m_Connections[i];
		std::string line = "  " + FitUtf8 (peer.hash, 46) + FitUtf8 (std::to_string (peer.sessions), 5, true)
			+ FitUtf8 (std::to_string (peer.attempts), 9, true) + " " + FitUtf8 (peer.transportNames, 20)
			+ FitUtf8 (peer.highBandwidth ? "yes" : "", 5);
		PutLine (row++, line, width);
	}
}

void App::DrawTunnels (int top, int height, int width)
{
	int row = top;
	std::string title = "  " + std::string (i2p::i18n::tr ("Tunnels: in ")) + Field ("tunnels.inbound")
		+ ", " + std::string (i2p::i18n::tr ("out")) + " " + Field ("tunnels.outbound") + ", " + std::string (i2p::i18n::tr ("transit")) + " "
		+ Field ("tunnels.transit") + ", " + std::string (i2p::i18n::tr ("success")) + " " + Field ("tunnels.success") + "%";
	if (!m_Connected) title += " " + std::string (i2p::i18n::tr ("[stale]"));
	PutLine (row++, title, width, m_Theme.Attributes (ThemeRole::Primary));
	PutLine (row++, "  " + std::string (i2p::i18n::tr ("Tunnel ID   Dir      State         Hops  Created")),
		width, m_Theme.Attributes (ThemeRole::Primary));
	if (m_Tunnels.empty ())
		PutLine (row, m_TunnelsLoaded ? "  " + std::string (i2p::i18n::tr ("no tunnels"))
									 : "  " + std::string (i2p::i18n::tr ("waiting for the first tunnel snapshot")),
			width, m_Theme.Attributes (ThemeRole::Muted));
	const int visible = std::max (0, height - 2);
	const int maximum = std::max (0, static_cast<int> (m_Tunnels.size ()) - visible);
	m_Scroll = std::clamp (m_Scroll, 0, maximum);
	const int start = maximum - m_Scroll;
	for (int i = start; i < static_cast<int> (m_Tunnels.size ()) && row < top + height; ++i)
	{
		const auto & tunnel = m_Tunnels[i];
		char buffer[160];
		snprintf (buffer, sizeof (buffer), "  %-10.10s %-8.8s %-13.13s %5.5s %s", tunnel.id.c_str (),
			tunnel.direction.c_str (), tunnel.state.c_str (), tunnel.hops.c_str (),
			Timestamp (ToLong (tunnel.created)).c_str ());
		ThemeRole role = ThemeRole::Muted;
		if (tunnel.state == "established")
			role = ThemeRole::Success;
		else if (tunnel.state == "failed" || tunnel.state == "build-failed" || tunnel.state == "test-failed")
			role = ThemeRole::Error;
		else if (tunnel.state == "pending" || tunnel.state == "build-reply" || tunnel.state == "expiring")
			role = ThemeRole::Warning;
		PutLine (row++, buffer, width, m_Theme.Attributes (role));
	}
}

void App::DrawNetDb (int top, int height, int width)
{
	int row = top;
	if (!m_Connected && row < top + height)
		PutLine (row++, "  " + std::string (i2p::i18n::tr ("netDb snapshot is stale: daemon disconnected")), width,
			m_Theme.Attributes (ThemeRole::Warning));
	auto value = [this] (std::string_view name)
	{
		auto it = m_NetDb.find (std::string (name));
		if (it != m_NetDb.end ()) return it->second;
		return Field (name);
	};
	auto show = [&] (std::string_view label, std::string value)
	{
		if (row >= top + height) return;
		PutLine (row++, "  " + FitUtf8 (i2p::i18n::tr (label), 26) + std::move (value), width);
	};
	// Timestamps are milliseconds since the epoch in the protocol; the view
	// shows them through the same age formatter the rest of the panel uses.
	auto millis = [] (std::string_view text)
	{
		if (text.empty ()) return std::string ();
		return TimestampAged (ToU64 (text) / 1000);
	};
	show ("Routers", value ("netdb.routers"));
	show ("Floodfills", value ("netdb.floodfills"));
	show ("LeaseSets", value ("netdb.leasesets"));
	show ("Reseeding", value ("netdb.reseeding") == "1" ? std::string (i2p::i18n::tr ("active"))
														 : std::string (i2p::i18n::tr ("idle")));
	const auto phase = value ("netdb.reseed.phase");
	show ("Reseed phase",
		phase.empty () ? std::string (i2p::i18n::tr ("unknown")) : phase);
	show ("Reseed enabled",
		std::string (value ("netdb.reseed.enabled") == "0" ? i2p::i18n::tr ("disabled")
														  : i2p::i18n::tr ("enabled")));
	show ("Reseed attempts",
		value ("netdb.reseed.attempts") + " (" + value ("netdb.reseed.succeeded") + " "
			+ std::string (i2p::i18n::tr ("succeeded")) + ")");
	show ("Last reseed attempt", millis (value ("netdb.reseed.last_attempt_ms")));
	show ("Last reseed success", millis (value ("netdb.reseed.last_success_ms")));
	const auto reseedError = value ("netdb.reseed.last_error");
	show ("Reseed last error",
		reseedError.empty () ? std::string (i2p::i18n::tr ("none")) : reseedError);
	show ("Floodfill mode", value ("router.floodfill") == "1" ? std::string (i2p::i18n::tr ("enabled"))
															 : std::string (i2p::i18n::tr ("disabled")));
	show ("Hidden mode", value ("router.hidden") == "1" ? std::string (i2p::i18n::tr ("enabled"))
													   : std::string (i2p::i18n::tr ("disabled")));
	show ("Family", value ("router.family").empty () ? std::string (i2p::i18n::tr ("none")) : value ("router.family"));
	show ("Peers / connections", value ("peers.count") + " / " + value ("connections.count"));
	show ("Tunnel build queue", value ("tunnels.build_queue"));
	const int v4Ranges = static_cast<int> (std::min<int64_t> (ToLong (value ("blocklist.ipv4")), 1000000));
	const int v6Addresses = static_cast<int> (std::min<int64_t> (ToLong (value ("blocklist.ipv6")), 1000000));
	show ("Blocklist IPv4/CIDR", value ("blocklist.ipv4") + " " + ntr ("range", "ranges", v4Ranges));
	show ("Blocklist IPv6", value ("blocklist.ipv6") + " " + ntr ("address", "addresses", v6Addresses));
	show ("Blocklist router hashes", value ("blocklist.hashes"));
	show ("Banned routers", value ("blocklist.runtime")
		+ (value ("blocklist.loaded") == "1" ? "" : std::string (" ") + std::string (i2p::i18n::tr ("(blocklist not loaded)"))));

	if (row >= top + height) return;
	PutLine (row++, "  " + std::string (i2p::i18n::tr ("e reseed now, b/u ban or unban a router hash")),
		width, m_Theme.Attributes (ThemeRole::Muted));

	if (row >= top + height) return;
	PutLine (row++, "", width);
	const bool routers = m_NetDbView == NetDbView::Routers;
	const size_t count = routers ? m_NetDbRouters.size () : m_NetDbLeaseSets.size ();
	std::string title = routers ? "  " + std::string (i2p::i18n::tr ("Router directory ("))
								: "  " + std::string (i2p::i18n::tr ("LeaseSet directory ("));
	title += std::to_string (count) + " " + ntr ("record listed", "records listed", static_cast<int> (count))
		+ ") | " + std::string (i2p::i18n::tr ("n switches view"));
	PutLine (row++, title, width, m_Theme.Attributes (ThemeRole::Primary));
	const bool loaded = routers ? m_NetDbRoutersLoaded : m_NetDbLeaseSetsLoaded;
	if (!loaded)
	{
		PutLine (row, "  " + std::string (i2p::i18n::tr ("loading directory...")), width,
			m_Theme.Attributes (ThemeRole::Muted));
		return;
	}
	if (!count)
	{
		PutLine (row, routers ? "  " + std::string (i2p::i18n::tr ("no router records"))
							  : "  " + std::string (i2p::i18n::tr ("no LeaseSet records")),
			width, m_Theme.Attributes (ThemeRole::Muted));
		return;
	}

	const int visible = std::max (0, top + height - row);
	const int maximum = std::max (0, static_cast<int> (count) - visible);
	m_Scroll = std::clamp (m_Scroll, 0, maximum);
	if (routers)
	{
		PutLine (row++, "  " + std::string (i2p::i18n::tr (
			"Hash                                      Ver  Caps# F  V4 V6 Updated     Reach")), width,
			m_Theme.Attributes (ThemeRole::Primary));
		for (int i = m_Scroll; i < static_cast<int> (m_NetDbRouters.size ()) && row < top + height; ++i)
		{
			const auto & router = m_NetDbRouters[i];
			std::string line = "  " + FitUtf8 (router.hash, 42) + FitUtf8 (std::to_string (router.version), 5, true)
				+ FitUtf8 (std::to_string (router.caps), 5, true) + FitUtf8 (router.floodfill ? "F" : "-", 2);
			line += FitUtf8 (router.v4 ? "Y" : "-", 3) + FitUtf8 (router.v6 ? "Y" : "-", 3)
				+ FitUtf8 (TimestampAged (router.timestamp), 12) + FitUtf8 (router.reachable ? "yes" : "no", 6);
			PutLine (row++, line, width);
		}
	}
	else
	{
		PutLine (row++, "  " + std::string (i2p::i18n::tr (
			"Hash                                      Store Enc    Expires     Earliest    State")), width,
			m_Theme.Attributes (ThemeRole::Primary));
		for (int i = m_Scroll; i < static_cast<int> (m_NetDbLeaseSets.size ()) && row < top + height; ++i)
		{
			const auto & leaseSet = m_NetDbLeaseSets[i];
			std::string state = leaseSet.expired ? std::string (i2p::i18n::tr ("expired"))
												 : (leaseSet.valid ? std::string (i2p::i18n::tr ("valid"))
																   : std::string (i2p::i18n::tr ("invalid")));
			std::string line = "  " + FitUtf8 (leaseSet.hash, 42)
				+ FitUtf8 (std::to_string (leaseSet.storeType), 6, true)
				+ FitUtf8 (std::to_string (leaseSet.encryptionType), 5, true)
				+ FitUtf8 (TimestampAged (leaseSet.expiration / 1000), 12) + " "
				+ FitUtf8 (TimestampAged (leaseSet.earliestLease / 1000), 12) + " " + FitUtf8 (state, 8);
			PutLine (row++, line, width);
		}
	}
}

void App::DrawConfig (int top, int height, int width)
{
	int row = top;
	std::string title = "  " + std::string (i2p::i18n::tr ("Router configuration ("));
	title += m_ConfigReadOnly ? std::string (i2p::i18n::tr ("read-only")) : std::string (i2p::i18n::tr ("snapshot"));
	if (m_ConfigSchema > 0) title += ", " + std::string (i2p::i18n::tr ("schema ")) + std::to_string (m_ConfigSchema);
	title += ")";
	if (!m_Connected) title += " " + std::string (i2p::i18n::tr ("[stale]"));
	PutLine (row++, title, width, m_Theme.Attributes (ThemeRole::Primary));

	if (!m_ConfigLoaded)
	{
		PutLine (row, "  " + std::string (i2p::i18n::tr ("loading configuration snapshot...")), width);
		return;
	}
	if (m_Config.empty ())
	{
		PutLine (row, "  " + std::string (i2p::i18n::tr ("no allowlisted configuration options reported")), width);
		return;
	}

	const int nameColumns = width >= 72 ? 32 : (width >= 56 ? 24 : 18);
	constexpr int typeColumns = 9;
	const int valueColumns = std::max (8, width - 2 - nameColumns - typeColumns);
	std::string header = "  " + FitUtf8 (i2p::i18n::tr ("Option"), nameColumns)
		+ FitUtf8 (i2p::i18n::tr ("Type"), typeColumns) + FitUtf8 (i2p::i18n::tr ("Value"), valueColumns);
	PutLine (row++, header, width, m_Theme.Attributes (ThemeRole::Primary));

	const int visible = std::max (0, top + height - row);
	const int maximum = std::max (0, static_cast<int> (m_Config.size ()) - visible);
	m_Scroll = std::clamp (m_Scroll, 0, maximum);
	for (int i = m_Scroll; i < static_cast<int> (m_Config.size ()) && row < top + height; ++i)
	{
		const auto & option = m_Config[i];
		std::string line = "  " + FitUtf8 (option.name, nameColumns) + FitUtf8 (option.type, typeColumns);
		line += FitUtf8 (option.value, valueColumns);
		PutLine (row++, line, width);
	}

	if (row < top + height)
	{
		PutLine (row++, "", width);
		PutLine (row, "  " + std::string (i2p::i18n::tr (
			"Editing is unavailable in protocol version 1; use the daemon configuration and restart.")),
			width, m_Theme.Attributes (ThemeRole::Muted));
	}
}

void App::DrawDestinations (int top, int height, int width)
{
	if (m_Destinations.empty ())
	{
		PutLine (top, m_DestinationsLoaded
				? "  " + std::string (i2p::i18n::tr ("No local client destinations or address-book entries."))
				: "  " + std::string (i2p::i18n::tr ("Waiting for the first destination snapshot.")),
			width, m_Theme.Attributes (ThemeRole::Muted));
		PutLine (top + 1,
			m_Connected
				? "  " + std::string (i2p::i18n::tr (
					"Configure a client tunnel or press a to add an address-book entry."))
				: "  " + std::string (i2p::i18n::tr ("daemon disconnected; showing no cached destinations")),
			width, m_Theme.Attributes (ThemeRole::Muted));
		return;
	}
	std::string title = "  " + std::string (i2p::i18n::tr (
		"Local destinations and address book (a add, d remove, b/u block/unblock)"));
	if (!m_Connected) title += " " + std::string (i2p::i18n::tr ("[stale]"));
	PutLine (top, title, width, m_Theme.Attributes (ThemeRole::Primary));
	const int visible = std::max (0, height - 1);
	const int maximum = std::max (0, static_cast<int> (m_Destinations.size ()) - visible);
	m_Scroll = std::clamp (m_Scroll, 0, maximum);
	for (int i = m_Scroll; i < static_cast<int> (m_Destinations.size ()) && top + 1 + (i - m_Scroll) < top + height;
		++i)
		PutLine (top + 1 + (i - m_Scroll), "  " + m_Destinations[i], width);
}

void App::DrawServices (int top, int height, int width)
{
	int row = top;
	const std::string hint = std::string (i2p::i18n::tr ("Up/Down select, s start, x stop (both confirmed)"));
	std::string title = "  " + std::string (i2p::i18n::tr ("Configured client tunnels: "))
		+ std::to_string (m_TunnelServices.size ());
	if (!m_Connected) title += " [stale]";
	PutLine (row++, title, width, m_Theme.Attributes (ThemeRole::Primary));
	if (m_TunnelServices.empty ())
	{
		PutLine (row++,
			m_Connected ? "  " + std::string (i2p::i18n::tr ("no client or server tunnels configured"))
						: "  " + std::string (i2p::i18n::tr ("waiting for daemon")),
			width, m_Theme.Attributes (ThemeRole::Muted));
	}
	else
	{
		PutLine (row++,
			"   " + FitUtf8 (i2p::i18n::tr ("Name"), 25) + FitUtf8 (i2p::i18n::tr ("Kind"), 12)
				+ FitUtf8 (i2p::i18n::tr ("State"), 8) + FitUtf8 (i2p::i18n::tr ("Endpoint"), 26)
				+ FitUtf8 (i2p::i18n::tr ("Destination"), 24),
			width, m_Theme.Attributes (ThemeRole::Primary));
		const int visible = std::max (0, top + height - row - 1);
		const int maximum = std::max (0, static_cast<int> (m_TunnelServices.size ()) - visible);
		m_Scroll = std::clamp (m_Scroll, 0, maximum);
		m_ServiceVisible = std::max (1, visible);
		for (int i = m_Scroll; i < static_cast<int> (m_TunnelServices.size ()) && row < top + height - 1; ++i)
		{
			const auto & service = m_TunnelServices[static_cast<size_t> (i)];
			const bool selected = i == m_ServiceSelection;
			std::string line = selected ? " > " : "   ";
			line += FitUtf8 (service.name, 25) + FitUtf8 (service.kind, 12)
				+ FitUtf8 (i2p::i18n::tr (service.running ? "running" : "stopped"), 8)
				+ FitUtf8 (service.endpoint, 26) + FitUtf8 (service.destination, 24);
			auto role = service.running ? ThemeRole::Success : ThemeRole::Muted;
			if (selected) role = ThemeRole::Primary;
			PutLine (row++, line, width, m_Theme.Attributes (role));
		}
	}

	if (row < top + height)
		PutLine (row++, "  " + hint, width, m_Theme.Attributes (ThemeRole::Muted));
	if (row >= top + height) return;

	std::string servicesTitle = "  " + std::string (i2p::i18n::tr ("Protocol servers and proxies: "))
		+ std::to_string (m_Services.size ());
	PutLine (row++, servicesTitle, width, m_Theme.Attributes (ThemeRole::Primary));
	if (row >= top + height) return;
	PutLine (row++,
		"  " + FitUtf8 (i2p::i18n::tr ("Type"), 12) + FitUtf8 (i2p::i18n::tr ("Name"), 25)
			+ FitUtf8 (i2p::i18n::tr ("Endpoint"), 26) + FitUtf8 (i2p::i18n::tr ("Destination"), 24),
		width, m_Theme.Attributes (ThemeRole::Primary));
	if (m_Services.empty ())
	{
		PutLine (row,
			m_Connected ? "  " + std::string (i2p::i18n::tr ("no proxies or protocol servers configured"))
						: "  " + std::string (i2p::i18n::tr ("waiting for daemon")),
			width, m_Theme.Attributes (ThemeRole::Muted));
		return;
	}
	for (size_t i = 0; i < m_Services.size () && row < top + height; ++i)
	{
		const auto & service = m_Services[i];
		std::string line = "  " + FitUtf8 (service.kind, 12) + FitUtf8 (service.name, 25)
			+ FitUtf8 (service.endpoint, 26) + FitUtf8 (service.address, 24);
		line += service.enabled ? std::string (i2p::i18n::tr ("up")) : std::string (i2p::i18n::tr ("off"));
		PutLine (row++, line, width, m_Theme.Attributes (service.enabled ? ThemeRole::Success : ThemeRole::Muted));
	}
}

void App::DrawLogs (int top, int height, int width)
{
	std::vector<const LogRow *> visible;
	const auto filter = ToLowerAscii (m_LogFilter);
	for (const auto & record : m_Logs)
	{
		if (record.level > m_LogLevel) continue;
		if (!filter.empty () && ToLowerAscii (record.text).find (filter) == std::string::npos) continue;
		visible.push_back (&record);
	}
	const int lines = std::max (0, height - 1);
	const int maximum = std::max (0, static_cast<int> (visible.size ()) - lines);
	m_Scroll = std::clamp (m_Scroll, 0, maximum);
	const int start = maximum - m_Scroll;
	int row = top;
	std::string header = "  " + std::string (i2p::i18n::tr ("Time      Level  Message (l local level, / filter"));
	if (!m_LogFilter.empty ()) header += ": " + TruncateUtf8 (m_LogFilter, 32);
	header += ", " + std::string (i2p::i18n::tr ("L daemon: ")) + m_DaemonLogLevel + ")";
	if (!m_Connected) header += " " + std::string (i2p::i18n::tr ("[stale]"));
	PutLine (row++, header, width, m_Theme.Attributes (ThemeRole::Primary));
	if (visible.empty ())
	{
		PutLine (row, !m_LogsLoaded
			? "  " + std::string (i2p::i18n::tr ("waiting for the first log snapshot"))
			: (filter.empty () ? "  " + std::string (i2p::i18n::tr ("no log records at this level"))
							   : "  " + std::string (i2p::i18n::tr ("no log records match the filter"))),
			width, m_Theme.Attributes (ThemeRole::Muted));
		return;
	}
	for (int i = start; i < static_cast<int> (visible.size ()) && row < top + height; ++i)
	{
		const auto & record = *visible[i];
		terminal::Move (row, 0);
		terminal::ClearToEol ();
		const int attributes = LevelAttribute (m_Theme, record.level);
		if (attributes) terminal::SetAttribute (attributes);
		std::string prefix = "  " + Timestamp (record.timestamp) + " " + std::string (LevelName (record.level)) + "  ";
		auto safePrefix = TruncateUtf8 (prefix, width, false);
		terminal::Put (safePrefix);
		int remaining = width - DisplayWidth (safePrefix);
		auto message = TruncateUtf8 (SanitizeText (record.text), std::max (0, remaining), true);
		if (!message.empty ()) terminal::Put (message);
		terminal::SetAttribute (terminal::ATTR_NORMAL);
		++row;
	}
}

void App::DrawHelp (int top, int height, int width)
{
	static constexpr std::string_view lines[] = { "  Keys - global",
		"  1..9, v, 0, Tab, Left/Right switch view (0 is Connections, v Services)",
		"  q                       quit the TUI; in a torrent detail it goes back (router keeps running)",
		"  r                       refresh immediately", "  ?                       this help", "",
		"  NetDb (5): b / u        ban/unban a base64 router hash until restart",
		"  Status (1): Up/Down, PgUp/PgDn scroll; Home/End first/last field", "",
		"  Status (1), Peers (2), Tunnels (3), Torrents (4), NetDb (5), Logs (6), Services (v), Help (7)",
		"  Destinations (8) lists local b32 addresses and address-book names.",
		"  a / d                   add name.i2p + base64 destination (asks before replacing) / remove address",
		"  b / u                   block/unblock subscription name or wildcard (e.g. *spam*.i2p)",
		"  s / x                   add/remove an address-book subscription URL",
		"  Config (9) is a read-only allowlisted snapshot. Connections (0) lists live peers.", "", "  Status (1)",
		"  e                       request a NetDb reseed from the network (confirmed)",
		"  o                       bring transports online/offline (confirmed; router keeps running)",
		"  H                       reload tunnel configuration (confirmed)",
		"  s / S                   graceful / immediate shutdown (confirmed; S requires SHUTDOWN)",
		"  c                       cancel a pending graceful shutdown",
		"  b / %                   set bandwidth class / share ratio percent",
		"  t                       toggle transit tunnel acceptance (confirmed)",
		"  The bandwidth history keeps the last 60 fifteen-second samples.", "", "  Connections (0)",
		"  Up/Down, PgUp/PgDn      scroll established transport sessions",
		"  t                       toggle transit tunnel acceptance (confirmed)", "", "  NetDb (5)",
		"  n                       switch between router and LeaseSet directories",
		"  Up/Down, PgUp/PgDn      scroll the selected directory", "", "  Logs (6)",
		"  l                       cycle the local display threshold",
		"  L                       cycle the daemon log level through secure IPC",
		"  O                       reopen the daemon log file",
		"  /                       filter records by text", "  c                       clear the log filter", "",
		"  Services (v)", "  Up/Down, PgUp/PgDn      select a configured client or server tunnel",
		"  s / x                   start / stop the selected tunnel (confirmed)",
		"  The protocol/proxy table below is display-only and bounded.",
		"  NetDb (5) also shows e   reseed phase, last error and last success; e requests a reseed",
		"  The panel is a bounded snapshot: no private keys, files or listener files are read.", "", "  Torrents (4)",
		"  Up/Down, PgUp/PgDn      select a torrent", "  Home/End                first/last torrent",
		"  Enter                   open torrent details", "  Esc / q                 back from details, or cancel a prompt",
		"  a / A                   add magnet / add .torrent file path",
		"  n                       create from file/directory in torrent root",
		"  N                       advanced creation: pieceBytes public|private trackerCount tier:URL... source",
		"                          pieceBytes 0=auto; tracker tiers contiguous 0..99",
		"  K                       cancel creation for the selected torrent session",
		"  t / T                   add / remove tracker URL for selected torrent",
		"  p                       pause or resume the selected torrent",
		"  R                       reannounce the selected torrent",
		"  V                       force recheck a paused, complete torrent",
		"  Ctrl-R                  reannounce all torrents",
		"  d / D                   remove / remove with local data", "  D                       requires typing YES",
		"  Ctrl-X / Ctrl-D         remove all torrents / remove all with local data",
		"  x / z                   start all / stop all",
		"  s / S                   cycle sort / reverse sort direction",
		"  /                       filter by torrent name or hash", "  c                       clear the filter",
		"  [ / ]                   previous/next torrent tunnel", "", "  Torrent details",
		"  Left/Right, Tab         general/files/peers/trackers/comments",
		"  f                       set file priority (index 0|1|2)",
		"  c / C / H / U           enable comments / add local comment / hide/unhide local ID",
		"  H / U                   hide or unhide a local comment id (asks [y/N])",
		"  [ / ]                   previous/next comment page on the comments tab",
		"  e / E                   edit metainfo comment / created-by (paused torrent)",
		"  Up/Down, PgUp/PgDn      scroll detail lines", "  Home/End                first/last detail line",
		"  Esc                     return to the torrent table", "", "  Notes",
		"  Actions are sent to the daemon over the local IPC endpoint.",
		"  Reseed, shutdown, tunnel reload, per-tunnel start/stop, bandwidth,",
		"  share, transit, address add/replace and daemon",
		"  log level use a closed, typed command set; there is no generic",
		"  configuration editor or command execution.",
		"  The TUI never opens torrent files, peers or trackers directly.",
		"  Removal with local data is a separate, typed confirmation.",
		"  Long UTF-8 values are truncated without splitting characters." };
	int total = static_cast<int> (sizeof (lines) / sizeof (lines[0]));
	int visible = std::max (0, height);
	int maximum = std::max (0, total - visible);
	m_HelpScroll = std::clamp (m_HelpScroll, 0, maximum);
	int row = top;
	for (int i = m_HelpScroll; i < total && row < top + height; ++i) PutLine (row++, std::string (lines[i]), width);
}

void App::DrawTorrents (int top, int height, int width)
{
	if (!m_Connected)
	{
		PutLine (top, "  " + std::string (i2p::i18n::tr ("daemon is disconnected; torrent state is unavailable")),
			width, m_Theme.Attributes (ThemeRole::Error));
		PutLine (top + 1, "  " + std::string (i2p::i18n::tr ("reconnecting automatically - see the footer for details")),
			width, m_Theme.Attributes (ThemeRole::Muted));
		return;
	}
	if (m_TorrentTunnels.empty ())
	{
		PutLine (top, "  " + std::string (i2p::i18n::tr ("No torrent service tunnel is available on this router.")),
			width, m_Theme.Attributes (ThemeRole::Warning));
		PutLine (top + 1, "  " + std::string (i2p::i18n::tr ("Start a torrent client tunnel in openi2pd, then press r.")),
			width, m_Theme.Attributes (ThemeRole::Muted));
		PutLine (top + 2, "  " + std::string (i2p::i18n::tr (
			"Torrent administration does not open any files or network links here.")),
			width, m_Theme.Attributes (ThemeRole::Muted));
		return;
	}

	auto layout = ComputeTorrentLayout (width);
	auto column
		= [] (std::string_view text, int columns, bool right = false) { return FitUtf8 (text, columns, right); };

	std::string header = "  ";
	header += column (i2p::i18n::tr ("Name"), layout.name);
	header += column (i2p::i18n::tr ("Status"), TORRENT_STATUS_COLS);
	if (layout.showSize) header += column (i2p::i18n::tr ("Size"), TORRENT_SIZE_COLS, true);
	header += column (i2p::i18n::tr ("Prog"), TORRENT_PROGRESS_COLS, true);
	if (layout.showDown) header += column (i2p::i18n::tr ("Down"), TORRENT_DOWN_COLS, true);
	if (layout.showUp) header += column (i2p::i18n::tr ("Up"), TORRENT_UP_COLS, true);
	if (layout.showPeers) header += column (i2p::i18n::tr ("Peers S/L"), TORRENT_PEERS_COLS);
	if (layout.showRatio) header += column (i2p::i18n::tr ("Ratio/ETA"), TORRENT_RATIO_COLS);
	if (layout.showError) header += column (i2p::i18n::tr ("Error"), TORRENT_ERROR_COLS);
	PutLine (top, header, width, m_Theme.Attributes (ThemeRole::Primary));

	if (!m_TorrentsLoaded)
	{
		PutLine (top + 1, "  " + std::string (i2p::i18n::tr ("loading torrent snapshot...")), width);
		return;
	}
	if (m_Torrents.empty ())
	{
		PutLine (top + 1, "  " + std::string (i2p::i18n::tr ("no torrents in this tunnel")), width);
		PutLine (top + 2, "  " + std::string (i2p::i18n::tr (
			"press a to add a magnet, A to add a .torrent by path")), width);
		return;
	}
	if (m_TorrentView.empty ())
	{
		PutLine (top + 1, "  " + std::string (i2p::i18n::tr ("no torrent matches the current filter")), width);
		PutLine (top + 2, "  " + std::string (i2p::i18n::tr ("press / to change the filter or c to clear it")), width);
		return;
	}

	int visible = std::max (0, height - 1);
	EnsureTorrentSelectionVisible (visible);
	int row = top + 1;
	for (int i = m_TorrentScroll; i < static_cast<int> (m_TorrentView.size ()) && row < top + height; ++i)
	{
		const auto & torrent = m_Torrents[m_TorrentView[i]];
		bool selected = i == m_TorrentSelection;
		std::string line = selected ? "> " : "  ";
		line += column (torrent.name, layout.name);
		line += column (TorrentStatusText (torrent.status), TORRENT_STATUS_COLS);
		if (layout.showSize) line += column (HumanBytes (torrent.total), TORRENT_SIZE_COLS, true);
		char progress[32];
		snprintf (progress, sizeof (progress), "%.1f%%", torrent.percent);
		std::string progressText = column (progress, TORRENT_PROGRESS_COLS, true);
		line += progressText;
		std::string downText;
		std::string upText;
		if (layout.showDown)
		{
			downText = column (HumanRateShort (torrent.downRate), TORRENT_DOWN_COLS, true);
			line += downText;
		}
		if (layout.showUp)
		{
			upText = column (HumanRateShort (torrent.upRate), TORRENT_UP_COLS, true);
			line += upText;
		}
		if (layout.showPeers)
		{
			char peers[64];
			snprintf (peers, sizeof (peers), "%d %d/%d", torrent.peers, torrent.seeders, torrent.leechers);
			line += column (peers, TORRENT_PEERS_COLS);
		}
		if (layout.showRatio) line += column (RatioEtaText (torrent), TORRENT_RATIO_COLS);
		if (layout.showError)
		{
			std::string error = torrent.error.empty () ? std::string () : "! " + torrent.error;
			line += column (error, TORRENT_ERROR_COLS);
		}
		PutLine (row, line, width, selected ? m_Theme.Attributes (ThemeRole::SelectedRow) : 0);

		if (!selected)
		{
			// Progress and rate columns are ASCII, so their byte count
			// equals their display width; overlaying them only changes
			// colour, not layout.
			auto overlay = [&] (int at, const std::string & text, ThemeRole role)
			{
				if (at >= width || text.empty ()) return;
				const int bytes = std::min (static_cast<int> (text.size ()), width - at);
				terminal::SetAttribute (m_Theme.Attributes (role));
				terminal::Move (row, at);
				terminal::Put (text, bytes);
				terminal::SetAttribute (terminal::ATTR_NORMAL);
			};

			int columnOffset = 2 + layout.name + TORRENT_STATUS_COLS;
			if (layout.showSize) columnOffset += TORRENT_SIZE_COLS;
			const ThemeRole progressRole = torrent.percent >= 99.95 ? ThemeRole::Success : ThemeRole::TorrentProgress;
			overlay (columnOffset, progressText, progressRole);
			columnOffset += TORRENT_PROGRESS_COLS;
			if (layout.showDown)
			{
				overlay (columnOffset, downText, ThemeRole::TorrentDownload);
				columnOffset += TORRENT_DOWN_COLS;
			}
			if (layout.showUp) overlay (columnOffset, upText, ThemeRole::TorrentUpload);
		}
		++row;
	}
}

void App::BuildDetailGeneral (std::vector<std::string> & lines, const TorrentRow & info, int width) const
{
	auto add = [&] (std::string_view label, std::string_view value)
	{
		// Display width, not byte length: a Russian or Chinese label must not
		// shrink the wrap budget or shift the continuation indent.
		const std::string prefix = "  " + std::string (i2p::i18n::tr (label)) + ": ";
		const int prefixColumns = DisplayWidth (prefix);
		int available = std::max (8, width - prefixColumns);
		auto text = SanitizeText (value);
		bool clipped = false;
		if (text.size () > 4096)
		{
			text = BoundUtf8 (text, 4096);
			clipped = true;
		}
		auto wrapped = WrapUtf8 (text, available);
		if (wrapped.empty ()) wrapped.emplace_back ();
		for (size_t i = 0; i < wrapped.size (); ++i)
		{
			if (i == 0)
				lines.push_back (prefix + wrapped[i]);
			else
				lines.push_back (std::string (static_cast<size_t> (prefixColumns), ' ') + wrapped[i]);
		}
		if (clipped) lines.push_back (std::string (static_cast<size_t> (prefixColumns), ' ') + "...");
	};

	lines.push_back ("  " + std::string (i2p::i18n::tr ("General")));
	add ("Name", info.name);
	add ("ID", std::to_string (info.id));
	add ("Status", TorrentStatusText (info.status));
	add ("Privacy", info.privateTorrent
			? std::string (i2p::i18n::tr ("private swarm (DHT/PEX/comments disabled)"))
			: std::string (i2p::i18n::tr ("public")));
	add ("Hash", info.hash.empty () ? std::string (i2p::i18n::tr ("unknown")) : info.hash);
	add ("Magnet", info.magnet.empty () ? std::string (i2p::i18n::tr ("not reported")) : info.magnet);
	if (!info.metaComment.empty ()) add ("Comment", info.metaComment);
	if (!info.createdBy.empty ()) add ("Created by", info.createdBy);
	char percent[32];
	snprintf (percent, sizeof (percent), "%.1f%%", info.percent);
	add ("Size", HumanBytes (info.total) + " (" + percent + " " + std::string (i2p::i18n::tr ("complete)")));
	if (info.wanted && info.wanted != info.total)
		add ("Selected size", HumanBytes (info.wanted) + " ("
			+ std::string (i2p::i18n::tr ("skipped bytes are not requested)")));
	add ("Have", HumanBytes (info.have));
	add ("Downloaded / uploaded", HumanBytes (info.downloaded) + " / " + HumanBytes (info.uploaded));
	add ("Rate down / up", HumanRate (info.downRate) + " / " + HumanRate (info.upRate));
	add ("Peers",
		std::to_string (info.peers) + " (seeders " + std::to_string (info.seeders) + ", leechers "
			+ std::to_string (info.leechers) + ")");
	add ("Ratio", RatioText (info));
	add ("ETA", info.eta ? HumanDuration (*info.eta) : std::string (i2p::i18n::tr ("not reported")));
	add ("Error", info.error.empty () ? std::string (i2p::i18n::tr ("none")) : info.error);
	add ("Detail", m_TorrentDetail.loaded ? std::string (i2p::i18n::tr ("loaded from TORRENT-DETAIL"))
										  : std::string (i2p::i18n::tr ("waiting for daemon snapshot")));
	lines.push_back ("");
	lines.push_back ("  " + std::string (i2p::i18n::tr ("Session")));
	if (m_TorrentSessionLoaded)
	{
		add ("Downloaded / uploaded",
			HumanBytes (m_TorrentSession.downloaded) + " / " + HumanBytes (m_TorrentSession.uploaded));
		add ("Active / paused",
			std::to_string (m_TorrentSession.active) + " / " + std::to_string (m_TorrentSession.paused));
		if (m_TorrentSession.creationJob)
		{
			add (std::string (i2p::i18n::tr ("Creation job ")) + std::to_string (m_TorrentSession.creationJob),
				(m_TorrentSession.creationPending
						? std::string (i2p::i18n::tr ("working: "))
						: m_TorrentSession.creationSuccess ? std::string (i2p::i18n::tr ("complete: "))
														   : std::string (i2p::i18n::tr ("failed: ")))
					+ m_TorrentSession.creationMessage);
			add ("Creation progress",
				HumanBytes (m_TorrentSession.creationBytesDone) + " / "
					+ HumanBytes (m_TorrentSession.creationTotalBytes) + " | files "
					+ std::to_string (m_TorrentSession.creationFilesDone) + " / "
					+ std::to_string (m_TorrentSession.creationTotalFiles));
		}
	}
	else
		add ("State", std::string (i2p::i18n::tr ("not loaded yet")));
	lines.push_back ("");
	lines.push_back ("  DHT");
	if (!m_TorrentDhtLoaded)
		add ("State", std::string (i2p::i18n::tr ("not loaded yet")));
	else if (!m_TorrentDht.supported)
		add ("State", std::string (i2p::i18n::tr ("not supported by this daemon")));
	else
	{
		add ("Nodes / buckets",
			std::to_string (m_TorrentDht.numNodes) + " / " + std::to_string (m_TorrentDht.numBuckets));
		add ("Torrents / heard from",
			std::to_string (m_TorrentDht.numTorrents) + " / " + std::to_string (m_TorrentDht.numHeardFrom));
		add ("Failed peers", std::to_string (m_TorrentDht.numFailedPeers));
	}
	lines.push_back ("");
	lines.push_back ("  " + std::string (i2p::i18n::tr (
		"Esc returns to the torrent table. Left/Right changes detail tab.")));
}

void App::BuildDetailFiles (std::vector<std::string> & lines, const TorrentDetail & detail, int width) const
{
	lines.push_back ("  " + std::string (i2p::i18n::tr (
		"f set file index and priority: 0 skip, 1 normal, 2 high")));
	int nameColumns = std::max (8, width - 2 - 9 - 9 - 7);
	lines.push_back ("  " + FitUtf8 (i2p::i18n::tr ("File"), nameColumns)
		+ FitUtf8 (i2p::i18n::tr ("Size"), 9, true) + FitUtf8 (i2p::i18n::tr ("Done"), 9, true)
		+ FitUtf8 (i2p::i18n::tr ("Prog"), 7, true));
	if (detail.files.empty ())
	{
		lines.push_back ("  " + std::string (i2p::i18n::tr ("no file information reported")));
		return;
	}
	for (const auto & file : detail.files)
	{
		double percent
			= file.length ? 100.0 * static_cast<double> (file.completed) / static_cast<double> (file.length) : 0.0;
		if (percent > 100.0) percent = 100.0;
		char progress[32];
		snprintf (progress, sizeof (progress), "%.1f%%", percent);
		lines.push_back ("  "
			+ FitUtf8 ("[" + std::to_string (file.index) + "] p" + std::to_string (file.priority) + " "
					+ SanitizeText (file.name),
				nameColumns)
			+ FitUtf8 (HumanBytes (file.length), 9, true) + FitUtf8 (HumanBytes (file.completed), 9, true)
			+ FitUtf8 (progress, 7, true));
	}
}

void App::BuildDetailPeers (std::vector<std::string> & lines, const TorrentDetail & detail, int width) const
{
	bool showClient = width >= 72;
	bool showFlags = width >= 60;
	bool showRates = width >= 84;
	int fixed = 2 + 7;
	if (showClient) fixed += 14;
	if (showFlags) fixed += 7;
	if (showRates) fixed += 18;
	int identColumns = std::max (8, width - fixed);
	std::string header = "  " + FitUtf8 (i2p::i18n::tr ("Peer"), identColumns);
	if (showClient) header += FitUtf8 (i2p::i18n::tr ("Client"), 14);
	if (showFlags) header += FitUtf8 (i2p::i18n::tr ("Flags"), 7);
	header += FitUtf8 (i2p::i18n::tr ("Prog"), 7, true);
	if (showRates) header += FitUtf8 (i2p::i18n::tr ("Down"), 9, true) + FitUtf8 (i2p::i18n::tr ("Up"), 9, true);
	lines.push_back (header);
	if (detail.peers.empty ())
	{
		lines.push_back ("  " + std::string (i2p::i18n::tr ("no peer information reported")));
		return;
	}
	for (const auto & peer : detail.peers)
	{
		char progress[32];
		snprintf (progress, sizeof (progress), "%.1f%%", std::clamp (peer.progress, 0.0, 100.0));
		std::string line = "  " + FitUtf8 (peer.ident, identColumns);
		if (showClient) line += FitUtf8 (peer.client, 14);
		if (showFlags) line += FitUtf8 (peer.flags, 7);
		line += FitUtf8 (progress, 7, true);
		if (showRates)
			line += FitUtf8 (HumanRateShort (peer.downRate), 9, true) + FitUtf8 (HumanRateShort (peer.upRate), 9, true);
		lines.push_back (line);
	}
}

void App::BuildDetailTrackers (std::vector<std::string> & lines, const TorrentDetail & detail, int width) const
{
	lines.push_back ("  " + std::string (i2p::i18n::tr ("Trackers")));
	if (detail.trackers.empty ())
	{
		lines.push_back ("  " + std::string (i2p::i18n::tr ("no tracker information reported")));
		return;
	}
	for (const auto & tracker : detail.trackers)
	{
		lines.push_back ("  " + TruncateUtf8 (SanitizeText (tracker.announce), std::max (8, width - 2), true));
		lines.push_back ("    " + std::string (i2p::i18n::tr ("tier "))
			+ (tracker.tier >= 0 ? std::to_string (tracker.tier) : std::string (i2p::i18n::tr ("common")))
			+ " | " + std::string (i2p::i18n::tr ("peers")) + " " + std::to_string (tracker.peers) + " ("
			+ std::string (i2p::i18n::tr ("seeders")) + " " + std::to_string (tracker.seeders) + ", "
			+ std::string (i2p::i18n::tr ("leechers")) + " " + std::to_string (tracker.leechers) + ") | "
			+ std::string (i2p::i18n::tr ("result: "))
			+ TruncateUtf8 (SanitizeText (tracker.lastResult.empty () ? std::string (i2p::i18n::tr ("none"))
																	  : tracker.lastResult),
				std::max (8, width - 48), true));
		lines.push_back ("    " + std::string (i2p::i18n::tr ("last announce "))
			+ Timestamp64 (tracker.lastAnnounce) + ", " + std::string (i2p::i18n::tr ("next announce "))
			+ Timestamp64 (tracker.nextAnnounce));
	}
}

void App::BuildDetailComments (std::vector<std::string> & lines, const TorrentDetail & detail, int width) const
{
	lines.push_back ("  " + std::string (i2p::i18n::tr (
		"c enable/disable | C add local comment | H/U hide/unhide local ID (confirmed)")));
	lines.push_back ("  " + std::string (i2p::i18n::tr (
		"[ / ] previous/next comment page | author is an unverified pseudonym")));
	if (detail.comments.empty ())
	{
		lines.push_back ("  " + std::string (i2p::i18n::tr ("no comment snapshot reported")));
		return;
	}
	auto summary = [&] (std::string_view key)
	{
		auto it = detail.comments.find (std::string (key));
		return it == detail.comments.end () ? std::string () : SanitizeText (it->second);
	};
	// Every displayed bound is clamped again locally: the daemon bounds the
	// page, but this renderer must stay correct with any snapshot.
	const uint64_t maxOffset = i2p::tui::MAX_TORRENT_COMMENT_OFFSET;
	const uint64_t total = std::min<uint64_t> (ToU64 (summary ("comments.total")), maxOffset + 1);
	const uint64_t count = std::min<uint64_t> (
		ToU64 (summary ("comments.count")), i2p::tui::MAX_TORRENT_COMMENT_RECORDS_PER_REQUEST);
	const uint64_t offset = std::min<uint64_t> (ToU64 (summary ("comments.offset")), maxOffset);
	const bool truncated = summary ("comments.truncated") == "1";

	const std::string enabled = summary ("comments.enabled");
	const bool commentsEnabled = enabled == "1" || enabled == "true";
	std::string state = "  " + std::string (i2p::i18n::tr ("comments: "))
		+ std::string (commentsEnabled ? i2p::i18n::tr ("enabled") : i2p::i18n::tr ("disabled")) + " | "
		+ std::string (i2p::i18n::tr ("ratings: ")) + summary ("comments.ratingCount") + " "
		+ std::string (i2p::i18n::tr ("avg ")) + summary ("comments.average");
	lines.push_back (state);
	std::string page = "  " + std::string (i2p::i18n::tr ("records "));
	page += count ? std::to_string (offset + 1) + "-" + std::to_string (offset + count)
				  : std::string (i2p::i18n::tr ("none"));
	page += " " + std::string (i2p::i18n::tr ("of ")) + std::to_string (total) + " | "
		+ std::string (i2p::i18n::tr ("visible ")) + summary ("comments.visible") + ", " + std::string (i2p::i18n::tr ("hidden "))
		+ summary ("comments.hidden") + ", " + std::string (i2p::i18n::tr ("local ")) + summary ("comments.local");
	if (offset > 0 && truncated) page += " | " + std::string (i2p::i18n::tr ("[ newer, ] older"));
	else if (offset > 0) page += " | " + std::string (i2p::i18n::tr ("[ newer"));
	else if (truncated) page += " | " + std::string (i2p::i18n::tr ("] older"));
	lines.push_back (page);
	const std::string persistence = summary ("comments.persistenceError");
	// A failed local-state write is the reason a moderation decision may not
	// survive a restart; never hide it.
	if (!persistence.empty()) lines.push_back("  " + std::string(i2p::i18n::tr("persistence: ")) + persistence);

	const int textColumns = std::max (16, width - 4);
	for (uint64_t i = 0; i < count; ++i)
	{
		const auto prefix = "comment." + std::to_string (i);
		auto value = [&] (const char * suffix) -> const std::string &
		{
			static const std::string empty;
			auto it = detail.comments.find (prefix + suffix);
			return it == detail.comments.end () ? empty : it->second;
		};
		const std::string id = SanitizeText (value (".id"));
		// The local id is a positive decimal integer; anything else is not an
		// addressable record and cannot be shown as one.
		if (id.empty () || id.size () > 20u
			|| !std::all_of (id.begin (), id.end (), [] (char ch) { return ch >= '0' && ch <= '9'; }))
			continue;
		const bool hidden = value (".hidden") == "1" || value (".hidden") == "true";
		const bool localComment = value (".local") == "1" || value (".local") == "true";
		std::string owner = SanitizeCommentText (value (".owner"));
		if (owner.empty()) owner = std::string(i2p::i18n::tr("anonymous"));
		std::string rating = SanitizeText (value (".rating"));
		rating = rating == "0" || rating.empty() ? std::string(i2p::i18n::tr("unrated")) : rating + "/5";
		std::string header = "  [" + id + "] " + rating + " | " + owner + " | "
			+ TimestampAged (ToU64 (value (".time")));
		if (localComment) header += " | " + std::string(i2p::i18n::tr("yours"));
		if (hidden) header += " | " + std::string(i2p::i18n::tr("hidden locally (U unhides)"));
		lines.push_back (TruncateUtf8 (header, std::max (8, width - 1), true));
		// Hidden records keep their peer-supplied preview so an operator can
		// decide whether to unhide instead of only seeing an opaque id.
		const auto text = SanitizeCommentText (value (".text"));
		if (!text.empty ()) lines.push_back ("    " + TruncateUtf8 (text, textColumns, true));
	}
	if (!count) lines.push_back ("  " + std::string(i2p::i18n::tr("this page has no comment records")));
}

void App::DrawTorrentDetails (int top, int height, int width)
{
	const TorrentRow * selected = SelectedTorrent ();
	const TorrentRow * info = selected;
	if (m_TorrentDetail.loaded && selected && m_TorrentDetail.info.id == selected->id) info = &m_TorrentDetail.info;
	if (!info)
	{
		PutLine (top, "  " + std::string(i2p::i18n::tr("torrent is no longer available; press Esc")), width);
		return;
	}

	int row = top;
	PutLine (
		row++, "  " + FitUtf8 (info->name, std::max (1, width - 2)), width, m_Theme.Attributes (ThemeRole::Primary));

	static constexpr std::array<std::string_view, 5> tabNames = { "General", "Files", "Peers", "Trackers", "Comments" };
	std::string tabs = "  ";
	for (size_t i = 0; i < tabNames.size (); ++i)
	{
		if (static_cast<size_t> (m_TorrentDetailTab) == i)
			tabs += "[" + std::string (i2p::i18n::tr (tabNames[i])) + "] ";
		else
			tabs += " " + std::string (i2p::i18n::tr (tabNames[i])) + "  ";
	}
	tabs += " " + std::string (i2p::i18n::tr ("(Left/Right, Tab)"));
	if (m_TorrentDetailTab == TorrentDetailTab::Comments)
		tabs += " " + std::string (i2p::i18n::tr ("[ / ] page"));
	PutLine (row++, tabs, width);

	std::vector<std::string> lines;
	switch (m_TorrentDetailTab)
	{
	case TorrentDetailTab::General: BuildDetailGeneral (lines, *info, width); break;
	case TorrentDetailTab::Files: BuildDetailFiles (lines, m_TorrentDetail, width); break;
	case TorrentDetailTab::Peers: BuildDetailPeers (lines, m_TorrentDetail, width); break;
	case TorrentDetailTab::Trackers: BuildDetailTrackers (lines, m_TorrentDetail, width); break;
	case TorrentDetailTab::Comments: BuildDetailComments (lines, m_TorrentDetail, width); break;
	}

	int visible = std::max (0, top + height - row);
	int maximum = std::max (0, static_cast<int> (lines.size ()) - visible);
	m_TorrentDetailScroll = std::clamp (m_TorrentDetailScroll, 0, maximum);
	for (int i = m_TorrentDetailScroll; i < static_cast<int> (lines.size ()) && row < top + height; ++i)
		PutLine (row++, lines[i], width);
}

void App::DrawTorrentStatusLine (int height, int width)
{
	int row = height - 2;
	if (row < 1) return;
	if (m_Prompt != PromptKind::None)
	{
		int labelColumns = std::max (8, width / 2);
		auto label = TruncateUtf8 (SanitizeText (m_PromptLabel), labelColumns, true);
		int available = std::max (0, width - DisplayWidth (label) - 1);
		auto input = TailUtf8 (m_InputBuffer, available);
		std::string line = " " + label + input;
		terminal::SetAttribute (m_Theme.Attributes (ThemeRole::SelectedRow));
		PutLine (row, line, width);
		terminal::SetAttribute (terminal::ATTR_NORMAL);
		int column = std::min (width - 1, DisplayWidth (line));
		terminal::ShowCursor (true);
		terminal::Move (row, std::max (0, column));
		return;
	}

	terminal::ShowCursor (false);
	if (!m_StatusMessage.empty ())
	{
		if (std::chrono::steady_clock::now () >= m_StatusExpiry)
		{
			m_StatusMessage.clear ();
			m_StatusError = false;
			return;
		}
		std::string line = " " + std::string (m_StatusError ? "! " : "> ") + m_StatusMessage;
		PutLine (row, line, width, m_Theme.Attributes (m_StatusError ? ThemeRole::Error : ThemeRole::Success));
		return;
	}

	if (m_Tab == Tab::Torrents && m_TorrentsLoaded)
	{
		uint64_t total = 0, have = 0, down = 0, up = 0;
		int active = 0;
		for (auto index : m_TorrentView)
		{
			if (index >= m_Torrents.size ()) continue;
			const auto & torrent = m_Torrents[index];
			total += torrent.total;
			have += torrent.have;
			down += torrent.downRate;
			up += torrent.upRate;
			if (IsActiveStatus (torrent.status)) ++active;
		}
		std::string line = "  " + std::to_string (m_TorrentView.size ()) + "/" + std::to_string (m_Torrents.size ())
			+ " shown, " + std::to_string (active) + " active | " + HumanBytes (have) + " / " + HumanBytes (total)
			+ " | " + HumanRateShort (down) + " down " + HumanRateShort (up) + " up";
		PutLine (row, line, width, m_Theme.Attributes (ThemeRole::Muted));
	}
}

void App::Draw ()
{
	int height = 0, width = 0;
	terminal::Dimensions (height, width);
	if (height < 6 || width < 40)
	{
		terminal::Clear ();
		terminal::ShowCursor (false);
		terminal::SetAttribute (m_Theme.Attributes (ThemeRole::Error));
		terminal::Move (0, 0);
		terminal::Put (i2p::i18n::tr ("Terminal too small"), std::max (0, width));
		if (m_Prompt != PromptKind::None && height > 1)
		{
			terminal::Move (1, 0);
			terminal::Put (i2p::i18n::tr ("Prompt suspended; Esc cancels"), std::max (0, width));
		}
		terminal::SetAttribute (terminal::ATTR_NORMAL);
		terminal::Refresh ();
		m_Dirty = false;
		return;
	}

	terminal::Clear ();
	DrawHeader (width);
	if (m_Tab == Tab::Torrents) DrawTorrentContext (width);
	DrawFooter (height, width);
	const int top = 2;
	const int contentHeight = height - 4;
	switch (m_Tab)
	{
	case Tab::Status: DrawStatus (top, contentHeight, width); break;
	case Tab::Peers: DrawPeers (top, contentHeight, width); break;
	case Tab::Connections: DrawConnections (top, contentHeight, width); break;
	case Tab::Tunnels: DrawTunnels (top, contentHeight, width); break;
	case Tab::NetDb: DrawNetDb (top, contentHeight, width); break;
	case Tab::Destinations: DrawDestinations (top, contentHeight, width); break;
	case Tab::Torrents:
		if (m_TorrentDetails)
			DrawTorrentDetails (top, contentHeight, width);
		else
			DrawTorrents (top, contentHeight, width);
		break;
	case Tab::Logs: DrawLogs (top, contentHeight, width); break;
	case Tab::Services: DrawServices (top, contentHeight, width); break;
	case Tab::Help: DrawHelp (top, contentHeight, width); break;
	case Tab::Config: DrawConfig (top, contentHeight, width); break;
	}
	DrawTorrentStatusLine (height, width);
	terminal::Refresh ();
	m_Dirty = false;
}

void App::HandleKey (int key)
{
	if (m_Prompt != PromptKind::None)
	{
		HandlePromptKey (key);
		return;
	}

	if (m_Tab == Tab::Torrents)
	{
		if (key == 'K')
		{
			if (RequireTorrentTunnel ())
				SendTorrentCommand ("TORRENT-CREATE-CANCEL " + std::to_string (m_TorrentTunnel));
			return;
		}
		if (m_TorrentDetails)
		{
			switch (key)
			{
			case 27:
			case 'q':
				// q in a modal detail view leaves the detail; quitting the TUI
				// is Q (and works from the table). The footer documents this.
				LeaveTorrentDetails ();
				return;
			case 'f':
			case 'c':
			case 'C':
			case 'H':
			case 'U':
			case 'e':
			case 'E':
			{
				const auto * torrent = SelectedTorrent ();
				if (!RequireTorrentTunnel () || !torrent || torrent->hash.empty ()) return;
				if ((key == 'c' || key == 'C') && m_TorrentDetail.info.privateTorrent)
				{
					SetStatus ("comments are disabled for private torrents", true);
					return;
				}
				if (key == 'C')
				{
					auto enabled = m_TorrentDetail.comments.find ("comments.enabled");
					if (enabled == m_TorrentDetail.comments.end () || enabled->second != "1")
					{
						SetStatus ("enable comments before adding a local comment", true);
						return;
					}
				}
				m_PendingTorrentId = torrent->id;
				m_PendingTorrentTunnel = m_TorrentTunnel;
				m_PendingTrackerHash = torrent->hash;
				if (key == 'f')
					BeginPrompt (PromptKind::FilePriority, "file index and priority (0 skip, 1 normal, 2 high): ");
				else if (key == 'c')
					BeginPrompt (PromptKind::CommentsToggle, "comments ON or OFF: ");
				else if (key == 'C')
					BeginPrompt (PromptKind::CommentAdd, "rating (0-5) author|text (author optional): ");
				else if (key == 'e')
					BeginPrompt (PromptKind::TorrentMetaComment, "metainfo comment (paused torrent): ");
				else if (key == 'E')
					BeginPrompt (PromptKind::TorrentMetaCreatedBy, "metainfo created-by (paused torrent): ");
				else if (key == 'U')
					BeginPrompt (PromptKind::CommentUnhide, "unhide local comment ID: ");
				else
					BeginPrompt (PromptKind::CommentHide, "hide local comment ID: ");
				return;
			}
			case terminal::KEY_LEFT:
			case terminal::KEY_BTAB:
			{
				int tab = static_cast<int> (m_TorrentDetailTab);
				m_TorrentDetailTab = static_cast<TorrentDetailTab> ((tab + 4) % 5);
				m_TorrentDetailScroll = 0;
				m_Dirty = true;
				return;
			}
			case terminal::KEY_RIGHT:
			case '\t':
			{
				int tab = static_cast<int> (m_TorrentDetailTab);
				m_TorrentDetailTab = static_cast<TorrentDetailTab> ((tab + 1) % 5);
				m_TorrentDetailScroll = 0;
				m_Dirty = true;
				return;
			}
			case '[':
			case ']':
			{
				// Comment paging is only meaningful on the comments tab; on the
				// other tabs the keys do nothing so they can never be mistaken
				// for the torrent table's tunnel cycling.
				if (m_TorrentDetailTab != TorrentDetailTab::Comments) return;
				PageTorrentComments (key == ']' ? 1 : -1);
				return;
			}
			case terminal::KEY_UP:
				if (m_TorrentDetailScroll > 0) --m_TorrentDetailScroll;
				m_Dirty = true;
				return;
			case terminal::KEY_DOWN:
				++m_TorrentDetailScroll;
				m_Dirty = true;
				return;
			case terminal::KEY_PPAGE:
				m_TorrentDetailScroll = std::max (0, m_TorrentDetailScroll - 10);
				m_Dirty = true;
				return;
			case terminal::KEY_NPAGE:
				m_TorrentDetailScroll += 10;
				m_Dirty = true;
				return;
			case terminal::KEY_HOME:
				m_TorrentDetailScroll = 0;
				m_Dirty = true;
				return;
			case terminal::KEY_END:
				m_TorrentDetailScroll = INT_MAX;
				m_Dirty = true;
				return;
			default: break;
			}
		}
		else
		{
			int page = TorrentPageRows ();
			switch (key)
			{
			case terminal::KEY_UP:
				if (m_TorrentSelection > 0) --m_TorrentSelection;
				EnsureTorrentSelectionVisible (page);
				m_Dirty = true;
				return;
			case terminal::KEY_DOWN:
				if (m_TorrentSelection + 1 < static_cast<int> (m_TorrentView.size ())) ++m_TorrentSelection;
				EnsureTorrentSelectionVisible (page);
				m_Dirty = true;
				return;
			case terminal::KEY_PPAGE:
				m_TorrentSelection = std::max (0, m_TorrentSelection - page);
				EnsureTorrentSelectionVisible (page);
				m_Dirty = true;
				return;
			case terminal::KEY_NPAGE:
				m_TorrentSelection = std::min (static_cast<int> (m_TorrentView.size ()) - 1, m_TorrentSelection + page);
				EnsureTorrentSelectionVisible (page);
				m_Dirty = true;
				return;
			case terminal::KEY_HOME:
				m_TorrentSelection = 0;
				EnsureTorrentSelectionVisible (page);
				m_Dirty = true;
				return;
			case terminal::KEY_END:
				m_TorrentSelection = std::max (0, static_cast<int> (m_TorrentView.size ()) - 1);
				EnsureTorrentSelectionVisible (page);
				m_Dirty = true;
				return;
			case '\n':
			case '\r':
			case terminal::KEY_ENTER: EnterTorrentDetails (); return;
			case '/': BeginPrompt (PromptKind::Filter, "filter name/hash: ", m_TorrentFilter); return;
			case 's': CycleTorrentSort (); return;
			case 'S': ToggleTorrentSortDirection (); return;
			case 'c':
			case 'C':
				m_TorrentFilter.clear ();
				RebuildTorrentView (CurrentSelectedId ());
				m_TorrentScroll = 0;
				SetStatus ("filter cleared", false);
				return;
			case '[': CycleTorrentTunnel (-1); return;
			case ']': CycleTorrentTunnel (1); return;
			case 'a': BeginPrompt (PromptKind::AddMagnet, "add magnet URI: "); return;
			case 'A': BeginPrompt (PromptKind::AddFile, "add .torrent path: "); return;
			case 'n':
			case 'N':
				if (!RequireTorrentTunnel ()) return;
				m_PendingTorrentTunnel = m_TorrentTunnel;
				if (key == 'n')
					BeginPrompt (PromptKind::CreateTorrent, "create from file/directory name in torrent root: ");
				else
					BeginPrompt (PromptKind::CreateTorrentAdvanced,
						"create: pieceBytes public|private trackerCount tier:URL... source: ", "0 public 0 ");
				return;
			case 't':
			case 'T':
			{
				if (!RequireTorrentTunnel ()) return;
				const auto * torrent = SelectedTorrent ();
				if (!torrent || torrent->hash.empty ())
				{
					SetStatus ("select a torrent first", true);
					return;
				}
				if (torrent->privateTorrent)
				{
					SetStatus ("private torrent tracker list is fixed", true);
					return;
				}
				m_PendingTorrentId = CurrentSelectedId ();
				m_PendingTorrentTunnel = m_TorrentTunnel;
				m_PendingTrackerHash = torrent->hash;
				BeginPrompt (key == 't' ? PromptKind::AddTracker : PromptKind::RemoveTracker,
					key == 't' ? "add I2P tracker URL: " : "remove I2P tracker URL: ");
				return;
			}
			case 'p':
			case 'P': ToggleSelected (); return;
			case 'R': ReannounceSelected (); return;
			case 'V':
			{
				if (!RequireTorrentTunnel ()) return;
				auto id = CurrentSelectedId ();
				if (id < 0)
				{
					SetStatus ("select a torrent first", true);
					return;
				}
				SendTorrentCommand ("TORRENT-RECHECK " + std::to_string (m_TorrentTunnel) + " " + std::to_string (id));
				return;
			}
			case 'd': RemoveSelected (false); return;
			case 'D': RemoveSelected (true); return;
			case 24: // Ctrl-X: session-wide removal, keeping local data
				RemoveAllTorrents (false);
				return;
			case 4: // Ctrl-D: session-wide removal with local data
				RemoveAllTorrents (true);
				return;
			case 'x':
			case 'X':
				if (RequireTorrentTunnel ())
					SendTorrentCommand ("TORRENT-START-ALL " + std::to_string (m_TorrentTunnel));
				return;
			case 'z':
			case 'Z':
				if (RequireTorrentTunnel ())
					SendTorrentCommand ("TORRENT-STOP-ALL " + std::to_string (m_TorrentTunnel));
				return;
			case 18: // Ctrl-R
				if (RequireTorrentTunnel ())
					SendTorrentCommand ("TORRENT-REANNOUNCE-ALL " + std::to_string (m_TorrentTunnel));
				return;
			default: break;
			}
		}
	}

	if ((m_Tab == Tab::Status || m_Tab == Tab::Connections) && (key == 't' || key == 'T'))
	{
		BeginTransitPrompt ();
		return;
	}

	if (m_Tab == Tab::Logs)
	{
		if (key == '/')
		{
			BeginPrompt (PromptKind::LogFilter, "filter log text: ", m_LogFilter);
			return;
		}
		if (key == 'c' || key == 'C')
		{
			m_LogFilter.clear ();
			m_Scroll = 0;
			SetStatus ("log filter cleared", false);
			return;
		}
		if (key == 'L')
		{
			CycleDaemonLogLevel ();
			return;
		}
		if (key == 'O')
		{
			SendAdminCommand ("ADMIN LOG-REOPEN");
			return;
		}
	}

	if (m_Tab == Tab::NetDb && (key == 'n' || key == 'N'))
	{
		m_NetDbView = m_NetDbView == NetDbView::Routers ? NetDbView::LeaseSets : NetDbView::Routers;
		m_Scroll = 0;
		m_NextNetDbList = std::chrono::steady_clock::now ();
		SetStatus (m_NetDbView == NetDbView::Routers ? "netDb: router directory" : "netDb: LeaseSet directory", false);
		return;
	}

	if (m_Tab == Tab::Services)
	{
		// Per-tunnel selection and lifecycle. Both verbs are typed ADMIN
		// commands addressed by the unique configured service name and are
		// confirmed because stopping a tunnel closes its live connections.
		const int count = static_cast<int> (m_TunnelServices.size ());
		const int page = std::max (1, m_ServiceVisible);
		switch (key)
		{
		case terminal::KEY_UP:
			if (m_ServiceSelection > 0) --m_ServiceSelection;
			EnsureServiceSelectionVisible (page);
			return;
		case terminal::KEY_DOWN:
			if (m_ServiceSelection + 1 < count) ++m_ServiceSelection;
			EnsureServiceSelectionVisible (page);
			return;
		case terminal::KEY_PPAGE:
			m_ServiceSelection = std::max (0, m_ServiceSelection - page);
			EnsureServiceSelectionVisible (page);
			return;
		case terminal::KEY_NPAGE:
			m_ServiceSelection = std::min (std::max (0, count - 1), m_ServiceSelection + page);
			EnsureServiceSelectionVisible (page);
			return;
		case terminal::KEY_HOME:
			m_ServiceSelection = 0;
			EnsureServiceSelectionVisible (page);
			return;
		case terminal::KEY_END:
			m_ServiceSelection = std::max (0, count - 1);
			EnsureServiceSelectionVisible (page);
			return;
		case 's':
		case 'S': BeginTunnelServicePrompt (true); return;
		case 'x':
		case 'X': BeginTunnelServicePrompt (false); return;
		default: break;
		}
	}

	if (m_Tab == Tab::NetDb && (key == 'e' || key == 'E'))
	{
		BeginReseedPrompt ();
		return;
	}

	if (m_Tab == Tab::Destinations && (key == 'a' || key == 'A'))
	{
		BeginPrompt (PromptKind::AddressAdd,
			std::string (i2p::i18n::tr ("address name.i2p and full base64 destination: ")));
		return;
	}
	if (m_Tab == Tab::Destinations && (key == 'd' || key == 'D'))
	{
		BeginPrompt (PromptKind::AddressRemove, "address name.i2p to remove: ");
		return;
	}
	if (m_Tab == Tab::Destinations && (key == 'b' || key == 'B'))
	{
		BeginPrompt (PromptKind::AddressBlock, "address name/wildcard.i2p to block from subscriptions: ");
		return;
	}
	if (m_Tab == Tab::Destinations && (key == 'u' || key == 'U'))
	{
		BeginPrompt (PromptKind::AddressUnblock, "address name/wildcard.i2p to unblock: ");
		return;
	}
	if (m_Tab == Tab::Destinations && (key == 's' || key == 'S'))
	{
		BeginPrompt (PromptKind::AddressSubAdd, "address-book subscription URL (http://host/path/hosts.txt): ");
		return;
	}
	if (m_Tab == Tab::Destinations && (key == 'x' || key == 'X'))
	{
		BeginPrompt (PromptKind::AddressSubRemove, "subscription URL to remove: ");
		return;
	}
	if (m_Tab == Tab::NetDb && (key == 'b' || key == 'B'))
	{
		BeginPrompt (PromptKind::NetDbBan, "base64 router hash to ban until restart: ");
		return;
	}
	if (m_Tab == Tab::NetDb && (key == 'u' || key == 'U'))
	{
		BeginPrompt (PromptKind::NetDbUnban, "base64 router hash to unban: ");
		return;
	}

	if (m_Tab == Tab::Status)
	{
		switch (key)
		{
		case 'e':
		case 'E': BeginReseedPrompt (); return;
		case 'o':
		case 'O': BeginOnlinePrompt (); return;
		case 'H': BeginReloadTunnelsPrompt (); return;
		case 's': BeginShutdownPrompt (false); return;
		case 'S': BeginShutdownPrompt (true); return;
		case 'b':
		case 'B': BeginBandwidthPrompt (); return;
		case '%': BeginSharePrompt (); return;
		case 'c':
		case 'C': SendAdminCommand ("ADMIN SHUTDOWN CANCEL"); return;
		default: break;
		}
	}

	switch (key)
	{
	case 'q':
	case 'Q': g_Stop = 1; return;
	case '1':
		m_Tab = Tab::Status;
		m_Scroll = 0;
		break;
	case '2':
		m_Tab = Tab::Peers;
		m_Scroll = 0;
		break;
	case '3':
		m_Tab = Tab::Tunnels;
		m_Scroll = 0;
		break;
	case '4':
		m_Tab = Tab::Torrents;
		m_Scroll = 0;
		ScheduleTorrentRefresh (true);
		break;
	case '5':
		m_Tab = Tab::NetDb;
		m_Scroll = 0;
		m_NextNetDbList = std::chrono::steady_clock::now ();
		break;
	case '6':
		m_Tab = Tab::Logs;
		m_Scroll = 0;
		break;
	case 'v':
	case 'V':
		m_Tab = Tab::Services;
		m_Scroll = 0;
		m_NextRefresh = std::chrono::steady_clock::now ();
		break;
	case '7':
	case '?':
		m_Tab = Tab::Help;
		m_Scroll = 0;
		m_HelpScroll = 0;
		break;
	case '8':
		m_Tab = Tab::Destinations;
		m_Scroll = 0;
		break;
	case '9':
		m_Tab = Tab::Config;
		m_Scroll = 0;
		break;
	case '0':
		m_Tab = Tab::Connections;
		m_Scroll = 0;
		m_NextRefresh = std::chrono::steady_clock::now ();
		break;
	case '\t':
	{
		auto next = (static_cast<int> (m_Tab) + 1) % static_cast<int> (TAB_NAMES.size ());
		m_Tab = static_cast<Tab> (next);
		m_Scroll = 0;
		if (m_Tab == Tab::Torrents) ScheduleTorrentRefresh (true);
		if (m_Tab == Tab::NetDb) m_NextNetDbList = std::chrono::steady_clock::now ();
		if (m_Tab == Tab::Connections) m_NextRefresh = std::chrono::steady_clock::now ();
		break;
	}
	case terminal::KEY_RIGHT:
	{
		auto next = (static_cast<int> (m_Tab) + 1) % static_cast<int> (TAB_NAMES.size ());
		m_Tab = static_cast<Tab> (next);
		m_Scroll = 0;
		if (m_Tab == Tab::Torrents) ScheduleTorrentRefresh (true);
		if (m_Tab == Tab::NetDb) m_NextNetDbList = std::chrono::steady_clock::now ();
		if (m_Tab == Tab::Connections) m_NextRefresh = std::chrono::steady_clock::now ();
		break;
	}
	case terminal::KEY_LEFT:
	{
		auto previous = (static_cast<int> (m_Tab) + static_cast<int> (TAB_NAMES.size ()) - 1)
			% static_cast<int> (TAB_NAMES.size ());
		m_Tab = static_cast<Tab> (previous);
		m_Scroll = 0;
		if (m_Tab == Tab::Torrents) ScheduleTorrentRefresh (true);
		if (m_Tab == Tab::NetDb) m_NextNetDbList = std::chrono::steady_clock::now ();
		if (m_Tab == Tab::Connections) m_NextRefresh = std::chrono::steady_clock::now ();
		break;
	}
	case 'r':
		m_NextRefresh = std::chrono::steady_clock::now ();
		if (m_Tab == Tab::Torrents) ScheduleTorrentRefresh (true);
		m_Dirty = true;
		return;
	case 'l':
		if (m_LogLevel > eLogCritical)
			--m_LogLevel;
		else
			m_LogLevel = eLogDebug;
		break;
	case terminal::KEY_UP:
		if (m_Tab == Tab::Help)
		{
			if (m_HelpScroll > 0) --m_HelpScroll;
		}
		else if (m_Tab == Tab::Status || m_Tab == Tab::Config || m_Tab == Tab::Destinations
			|| m_Tab == Tab::Connections || m_Tab == Tab::NetDb || m_Tab == Tab::Services)
		{
			if (m_Scroll > 0) --m_Scroll;
		}
		else
			++m_Scroll;
		m_Dirty = true;
		return;
	case terminal::KEY_DOWN:
		if (m_Tab == Tab::Help)
			++m_HelpScroll;
		else if (m_Tab == Tab::Status || m_Tab == Tab::Config || m_Tab == Tab::Destinations
			|| m_Tab == Tab::Connections || m_Tab == Tab::NetDb || m_Tab == Tab::Services)
			++m_Scroll;
		else if (m_Scroll > 0)
			--m_Scroll;
		m_Dirty = true;
		return;
	case terminal::KEY_PPAGE:
		if (m_Tab == Tab::Help)
			m_HelpScroll = std::max (0, m_HelpScroll - 10);
		else if (m_Tab == Tab::Status || m_Tab == Tab::Config || m_Tab == Tab::Destinations
			|| m_Tab == Tab::Connections || m_Tab == Tab::NetDb || m_Tab == Tab::Services)
			m_Scroll = std::max (0, m_Scroll - 10);
		else
			m_Scroll += 10;
		m_Dirty = true;
		return;
	case terminal::KEY_NPAGE:
		if (m_Tab == Tab::Help)
			m_HelpScroll += 10;
		else if (m_Tab == Tab::Status || m_Tab == Tab::Config || m_Tab == Tab::Destinations
			|| m_Tab == Tab::Connections || m_Tab == Tab::NetDb || m_Tab == Tab::Services)
			m_Scroll += 10;
		else if (m_Scroll > 10)
			m_Scroll -= 10;
		else
			m_Scroll = 0;
		m_Dirty = true;
		return;
	case terminal::KEY_HOME:
		if (m_Tab == Tab::Help)
			m_HelpScroll = 0;
		else if (m_Tab == Tab::Peers || m_Tab == Tab::Tunnels || m_Tab == Tab::Logs)
			// These views anchor at the newest/bottom row; a large offset
			// clamps to the first row when drawn.
			m_Scroll = INT_MAX;
		else
			m_Scroll = 0;
		m_Dirty = true;
		return;
	case terminal::KEY_END:
		if (m_Tab == Tab::Help)
			m_HelpScroll = INT_MAX;
		else if (m_Tab == Tab::Peers || m_Tab == Tab::Tunnels || m_Tab == Tab::Logs)
			m_Scroll = 0;
		else if (m_Tab == Tab::Status || m_Tab == Tab::Config || m_Tab == Tab::Destinations
			|| m_Tab == Tab::Connections || m_Tab == Tab::NetDb || m_Tab == Tab::Services)
			m_Scroll = INT_MAX;
		m_Dirty = true;
		return;
	default: return;
	}
	m_Dirty = true;
}
}

int main (int argc, char * argv[])
{
	auto options = i2p::tui::ParseOptions (argc, argv);
	if (!options)
	{
		std::fprintf (stderr, "i2pd-tui: %s\n", options.error ().c_str ());
		return EXIT_FAILURE;
	}
	if (options->showHelp)
	{
		std::printf ("i2pd-tui " VERSION " - local console for the openi2pd router\n"
					 "\n"
					 "Usage: i2pd-tui [--socket PATH] [--datadir PATH] [--conf PATH] [--service]\n"
					 "\n"
					 "  --socket ENDPOINT  local IPC endpoint (Unix socket; Windows pipe name)\n"
					 "  --datadir PATH  data directory (default: user data directory)\n"
					 "  --conf PATH     i2pd.conf used to read [tui] socket\n"
					 "  --service       use the system service data directory\n"
					 "  --lang LANG     UI language: en, es, ru, zh-Hans\n"
					 "  --help          show this help\n"
					 "  --version       show the version\n");
		return EXIT_SUCCESS;
	}
	if (options->showVersion)
	{
		std::printf ("i2pd-tui %s\n", VERSION);
		return EXIT_SUCCESS;
	}

	auto resolved = i2p::tui::ResolveSocket (*options);
	if (!resolved)
	{
		std::fprintf (stderr, "i2pd-tui: %s\n", resolved.error ().c_str ());
		return EXIT_FAILURE;
	}

#ifdef _WIN32
	// The shared text helpers decode payloads with mbrtowc()/wcwidth(), so
	// the CRT must use UTF-8.  Fall back to the system locale if a very old
	// CRT rejects the explicit name.
	if (!setlocale (LC_ALL, ".UTF-8")) setlocale (LC_ALL, "");
	// A console close/logoff would otherwise terminate without restoring the
	// alternate screen and input modes. The handler only publishes g_Stop.
	(void)SetConsoleCtrlHandler (HandleConsoleEvent, TRUE);
#else
	setlocale (LC_ALL, "");
	// The shared renderer measures UTF-8 with mbrtowc()/wcwidth(). Ensure a
	// multibyte LC_CTYPE even when LANG/LC_ALL is unset or C/POSIX.
	if (MB_CUR_MAX == 1 && !setlocale (LC_CTYPE, "C.UTF-8"))
		(void)setlocale (LC_CTYPE, "en_US.UTF-8");
	struct sigaction resizeAction { };
	resizeAction.sa_handler = HandleResize;
	sigemptyset (&resizeAction.sa_mask);
	sigaction (SIGWINCH, &resizeAction, nullptr);
	struct sigaction stopAction { };
	stopAction.sa_handler = HandleStop;
	sigemptyset (&stopAction.sa_mask);
	sigaction (SIGINT, &stopAction, nullptr);
	sigaction (SIGTERM, &stopAction, nullptr);
#endif

	// Select the UI catalog after the platform locale is established. An
	// explicit --lang/[tui] language wins; otherwise LC_ALL/LC_MESSAGES/LANG
	// is consulted and unsupported locales fall back to English.
	i2p::i18n::SetLanguage (options->language);

	if (!i2p::tui::terminal::Init ())
	{
		const char * error = i2p::tui::terminal::LastError ();
		std::fprintf (stderr, "i2pd-tui: %s\n", error ? error : "unable to initialize the terminal");
		return EXIT_FAILURE;
	}
	// The central theme owns colour discovery and all colour-pair
	// allocation.  It degrades to 16/8 colours or monochrome attributes
	// when the terminal cannot provide the requested combination.
	i2p::tui::Theme theme;
	theme.Init ();

	App app (*resolved, theme);
	int result = app.Run ();
	i2p::tui::terminal::Shutdown ();
	return result;
}

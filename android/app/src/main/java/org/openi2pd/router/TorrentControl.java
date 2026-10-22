/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: TorrentControl.
 *
 * Decoder and command builder for the bounded desktop-compatible torrent
 * control protocol. Responses are checked against their declared UTF-8 length,
 * field counts and key lengths are capped, duplicate fields are rejected and
 * magnet links are validated before being embedded in a command.
 *
 */

package org.openi2pd.router;

import java.nio.charset.StandardCharsets;
import java.util.LinkedHashMap;
import java.util.Map;

/** Bounded desktop-compatible torrent snapshots; no engine handles reach the UI. */
final class TorrentControl {
    static final int MAX_IMPORT_BYTES = 8 * 1024 * 1024;
    private TorrentControl() {
    }

    static Map<String, String> fields(String response) {
        if (response == null || response.length() > 262272) {
            throw new IllegalArgumentException("Invalid control response");
        }
        int newline = response.indexOf('\n');
        if (newline < 0 || newline > 128)
            throw new IllegalArgumentException("Invalid control header");
        String[] header = response.substring(0, newline).split(" ");
        boolean ok = header.length == 3 && "OK".equals(header[1]);
        boolean error = header.length == 4 && "ERR".equals(header[1]);
        if ((!ok && !error) || !"I2PDTUI/1".equals(header[0])) {
            throw new IllegalArgumentException("Invalid control header");
        }
        int size;
        try {
            size = Integer.parseInt(header[header.length - 1]);
        } catch (NumberFormatException exception) {
            throw new IllegalArgumentException("Invalid response length");
        }
        String payload = response.substring(newline + 1);
        if (size < 0 || size > 262144 || payload.getBytes(StandardCharsets.UTF_8).length != size) {
            throw new IllegalArgumentException("Invalid response length");
        }
        if (error)
            throw new IllegalArgumentException(payload);
        Map<String, String> result = new LinkedHashMap<>();
        if (payload.isEmpty())
            return java.util.Collections.emptyMap();
        for (String line : payload.split("\n")) {
            int equals = line.indexOf('=');
            if (equals <= 0 || equals > 64 || result.size() >= 16384) {
                throw new IllegalArgumentException("Invalid response field");
            }
            String key = line.substring(0, equals);
            if (result.put(key, unescape(line.substring(equals + 1))) != null) {
                throw new IllegalArgumentException("Duplicate response field");
            }
        }
        if ("error".equals(result.get("result"))) {
            throw new IllegalArgumentException(
                    result.getOrDefault("message", "Torrent operation failed"));
        }
        return java.util.Collections.unmodifiableMap(result);
    }

    static String magnet(int tunnel, String magnet) {
        if (tunnel < 0 || magnet == null || !magnet.startsWith("magnet:?") || magnet.length() > 2048
                || magnet.getBytes(StandardCharsets.UTF_8).length > 2048
                || magnet.chars().anyMatch(value -> value <= 32 || value >= 127)) {
            throw new IllegalArgumentException("Invalid magnet link");
        }
        return "TORRENT-ADD-MAGNET " + tunnel + " " + magnet;
    }

    static String unescape(String value) {
        StringBuilder text = new StringBuilder(value.length());
        for (int i = 0; i < value.length(); ++i) {
            char c = value.charAt(i);
            if (c == '\\' && i + 1 < value.length()) {
                char next = value.charAt(++i);
                switch (next) {
                case 'n' -> text.append('\n');
                case 'r' -> text.append('\r');
                case '\\' -> text.append('\\');
                default -> text.append('\\').append(next);
                }
            } else
                text.append(c);
        }
        return text.toString();
    }

    static String recheck(int tunnel, String torrentId) {
        if (tunnel < 0 || torrentId == null || !torrentId.matches("[0-9]{1,10}"))
            throw new IllegalArgumentException("Invalid torrent selection");
        try {
            // Torrent ids start at 1 in the native engine; 0 is the "not
            // found" sentinel and must never be sent as a command target.
            if (Integer.parseInt(torrentId) <= 0)
                throw new IllegalArgumentException("Invalid torrent selection");
        } catch (NumberFormatException error) {
            throw new IllegalArgumentException("Invalid torrent selection");
        }
        return "TORRENT-RECHECK " + tunnel + " " + torrentId;
    }

    static String remove(int tunnel, String torrentId, boolean deleteFiles) {
        if (tunnel < 0 || torrentId == null || !torrentId.matches("[0-9]{1,10}"))
            throw new IllegalArgumentException("Invalid torrent selection");
        try {
            if (Integer.parseInt(torrentId) <= 0)
                throw new IllegalArgumentException("Invalid torrent selection");
        } catch (NumberFormatException error) {
            throw new IllegalArgumentException("Invalid torrent selection");
        }
        return "TORRENT-REMOVE " + tunnel + " " + torrentId + (deleteFiles ? " files" : "");
    }

    static String metaComment(int tunnel, String torrentId, String text) {
        return meta(tunnel, torrentId, text, false);
    }

    static String metaCreatedBy(int tunnel, String torrentId, String text) {
        return meta(tunnel, torrentId, text, true);
    }

    private static String meta(int tunnel, String torrentId, String text, boolean createdBy) {
        int max = createdBy ? 255 : 1024;
        if (tunnel < 0 || torrentId == null || !torrentId.matches("[0-9]{1,10}") || text == null
                || text.isEmpty() || text.getBytes(StandardCharsets.UTF_8).length > max
                || !safeCommentText(text)) {
            throw new IllegalArgumentException("Invalid metainfo text");
        }
        try {
            if (Integer.parseInt(torrentId) <= 0)
                throw new IllegalArgumentException("Invalid torrent selection");
        } catch (NumberFormatException error) {
            throw new IllegalArgumentException("Invalid torrent selection");
        }
        return (createdBy ? "TORRENT-META-CREATED-BY " : "TORRENT-META-COMMENT ") + tunnel + " "
                + torrentId + " " + text;
    }

    static String tracker(boolean add, int tunnel, String torrentId, String tracker) {
        if (tunnel < 0 || torrentId == null || !torrentId.matches("[0-9]{1,10}")) {
            throw new IllegalArgumentException("Invalid torrent selection");
        }
        try {
            if (Integer.parseInt(torrentId) <= 0)
                throw new IllegalArgumentException("Invalid torrent selection");
        } catch (NumberFormatException error) {
            throw new IllegalArgumentException("Invalid torrent selection");
        }
        if (tracker == null || tracker.length() > 2048
                || tracker.chars().anyMatch(value -> value <= 32 || value >= 127)) {
            throw new IllegalArgumentException("Invalid I2P tracker URL");
        }
        try {
            java.net.URI uri = new java.net.URI(tracker);
            String host = uri.getHost();
            if (!("http".equalsIgnoreCase(uri.getScheme())
                        || "udp".equalsIgnoreCase(uri.getScheme()))
                    || host == null || !host.toLowerCase(java.util.Locale.ROOT).endsWith(".i2p")
                    || host.length() <= 4 || uri.getRawUserInfo() != null || uri.getPort() == 0
                    || uri.getPort() > 65535 || uri.getRawFragment() != null) {
                throw new IllegalArgumentException(
                        "Use an http:// or udp:// I2P tracker without credentials");
            }
        } catch (java.net.URISyntaxException error) {
            throw new IllegalArgumentException("Invalid I2P tracker URL");
        }
        return (add ? "TORRENT-TRACKER-ADD " : "TORRENT-TRACKER-REMOVE ") + tunnel + " " + torrentId
                + " " + tracker;
    }

    static String create(int tunnel, String source) {
        if (tunnel < 0 || source == null || source.isEmpty() || source.length() > 2048
                || source.equals(".") || source.equals("..") || source.indexOf('/') >= 0
                || source.indexOf('\\') >= 0
                || source.chars().anyMatch(value -> value < 32 || value >= 127)) {
            throw new IllegalArgumentException(
                    "Choose an existing file or folder name in torrent storage");
        }
        return "TORRENT-CREATE " + tunnel + " " + source;
    }

    static String commentId(int tunnel, String torrentId, String commentId, boolean hide) {
        if (tunnel < 0 || torrentId == null || !torrentId.matches("[0-9]{1,10}")
                || commentId == null || !commentId.matches("[0-9]{1,19}")) {
            throw new IllegalArgumentException("Invalid comment selection");
        }
        try {
            if (Integer.parseInt(torrentId) <= 0 || Long.parseLong(commentId) <= 0)
                throw new IllegalArgumentException("Invalid comment selection");
        } catch (NumberFormatException error) {
            throw new IllegalArgumentException("Invalid comment selection");
        }
        return (hide ? "TORRENT-COMMENT-HIDE " : "TORRENT-COMMENT-UNHIDE ") + tunnel + " "
                + torrentId + " " + commentId;
    }

    static String comment(int tunnel, String torrentId, int rating, String owner, String text) {
        if (tunnel < 0 || torrentId == null || !torrentId.matches("[0-9]{1,10}") || rating < 0
                || rating > 5 || owner == null || text == null || owner.length() > 32
                || text.length() > 512 || owner.indexOf('|') >= 0 || (text.isEmpty() && rating == 0)
                || !safeCommentText(owner) || !safeCommentText(text)
                || (owner + "|" + text).getBytes(StandardCharsets.UTF_8).length > 2046) {
            throw new IllegalArgumentException("Invalid comment or rating");
        }
        try {
            if (Integer.parseInt(torrentId) == 0)
                throw new IllegalArgumentException("Invalid torrent selection");
        } catch (NumberFormatException error) {
            throw new IllegalArgumentException("Invalid torrent selection");
        }
        return "TORRENT-COMMENT-ADD " + tunnel + " " + torrentId + " " + rating + " " + owner + "|"
                + text;
    }

    private static boolean safeCommentText(String text) {
        return text.codePoints().noneMatch(
                value -> Character.isISOControl(value) || (value >= 0xD800 && value <= 0xDFFF));
    }

    enum Bulk { START_ALL, STOP_ALL, REANNOUNCE_ALL }

    static String bulk(int tunnel, Bulk action) {
        if (tunnel < 0 || action == null)
            throw new IllegalArgumentException("Invalid torrent session");
        return switch (action) {
            case START_ALL -> "TORRENT-START-ALL " + tunnel;
            case STOP_ALL -> "TORRENT-STOP-ALL " + tunnel;
            case REANNOUNCE_ALL -> "TORRENT-REANNOUNCE-ALL " + tunnel;
        };
    }
}

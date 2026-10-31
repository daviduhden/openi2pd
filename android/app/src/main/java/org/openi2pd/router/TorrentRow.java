/*
 * See the LICENSE file at the top of the project tree for copyright and
 * license details.
 *
 * openi2pd: TorrentRow.
 *
 * Typed, bounded view of one TORRENTS snapshot row. The
 * desktop-compatible control protocol returns a flat key/value map;
 * parsing it in the fragment mixed bounds checks, presentation and
 * accessibility text. A record makes the field set explicit, rejects an
 * out-of-range value once and lets the JVM test the decoder without an
 * Activity, a device or the native library.
 *
 */

package org.openi2pd.router;

import java.util.Map;
import java.util.regex.Pattern;

/**
 * Immutable torrent summary. Every string is capped, every counter is
 * non-negative and a malformed identifier is rejected rather than
 * replaced by an invented value.
 */
record TorrentRow(String id, String name, String status, String error, String hash, String magnet,
        long total, long wanted, long have, boolean privateTorrent, long downloaded, long uploaded,
        long downRate, long upRate, int peers, int seeders, int leechers, int percent,
        int etaSeconds) {
    private static final int MAX_NAME = 512;
    private static final int MAX_STATUS = 64;
    private static final int MAX_ERROR = 256;
    private static final int MAX_MAGNET = 2048;
    /** Compiled once: a snapshot decodes up to 200 rows per refresh. */
    private static final Pattern HASH = Pattern.compile("[0-9a-fA-F]{40}");

    /**
     * Decode one {@code <prefix>key} snapshot row.
     *
     * <p>The engine id is mandatory: it must be one to ten ASCII
     * digits, otherwise the row is rejected so no later command can be
     * aimed at the wrong torrent. Optional fields degrade instead of
     * failing: a malformed hash or magnet is dropped, counters and the
     * percentage are clamped, and an absent or negative ETA stays at
     * {@code -1}.</p>
     *
     * @throws IllegalArgumentException when the id is absent,
     *                                  non-numeric or too long
     */
    static TorrentRow from(Map<String, String> fields, String prefix) {
        String id = fields.getOrDefault(prefix + "id", "");
        if (id.isEmpty() || id.length() > 10) {
            throw new IllegalArgumentException("Invalid torrent id");
        }
        for (int i = 0; i < id.length(); ++i) {
            char c = id.charAt(i);
            if (c < '0' || c > '9') {
                throw new IllegalArgumentException("Invalid torrent id");
            }
        }
        String hash = fields.getOrDefault(prefix + "hash", "");
        if (!hash.isEmpty() && !HASH.matcher(hash).matches()) {
            hash = "";
        }
        String magnet = fields.getOrDefault(prefix + "magnet", "");
        if (!magnet.startsWith("magnet:?") || magnet.length() > MAX_MAGNET) {
            magnet = "";
        }
        return new TorrentRow(id, text(fields, prefix + "name", MAX_NAME),
                text(fields, prefix + "status", MAX_STATUS),
                text(fields, prefix + "error", MAX_ERROR), hash, magnet,
                counter(fields, prefix + "total"), counter(fields, prefix + "wanted"),
                counter(fields, prefix + "have"), "1".equals(fields.get(prefix + "private")),
                counter(fields, prefix + "downloaded"), counter(fields, prefix + "uploaded"),
                counter(fields, prefix + "downRate"), counter(fields, prefix + "upRate"),
                boundedInt(fields, prefix + "peers"), boundedInt(fields, prefix + "seeders"),
                boundedInt(fields, prefix + "leechers"), percent(fields, prefix + "percent"),
                eta(fields, prefix + "eta"));
    }

    boolean isComplete() {
        return percent >= 100;
    }

    boolean hasError() {
        return !error.isEmpty();
    }

    /**
     * Two-line row label. The private badge is a caller-supplied
     * localized string, so this record never carries UI text of its
     * own.
     */
    String displayLabel(String privateBadge) {
        StringBuilder label = new StringBuilder(name.length() + status.length() + 48);
        label.append(name).append('\n').append(status).append(" · ").append(percent).append('%');
        if (privateTorrent && !privateBadge.isEmpty()) {
            label.append(" · ").append(privateBadge);
        }
        return label.toString();
    }

    /**
     * TalkBack description: name, state and percentage in one
     * utterance.
     */
    String accessibilityLabel(String privateBadge) {
        StringBuilder label = new StringBuilder(name.length() + status.length() + 48);
        label.append(name).append(", ").append(status).append(", ").append(percent).append('%');
        if (privateTorrent && !privateBadge.isEmpty()) {
            label.append(", ").append(privateBadge);
        }
        if (hasError()) {
            label.append(", ").append(error);
        }
        return label.toString();
    }

    private static String text(Map<String, String> fields, String key, int max) {
        String value = fields.getOrDefault(key, "");
        if (value.length() > max) {
            return value.substring(0, max);
        }
        return value;
    }

    private static long counter(Map<String, String> fields, String key) {
        String value = fields.getOrDefault(key, "0");
        if (value.isEmpty() || value.length() > 19) {
            return 0L;
        }
        try {
            long parsed = Long.parseLong(value);
            return parsed < 0 ? 0L : parsed;
        } catch (NumberFormatException exception) {
            return 0L;
        }
    }

    private static int boundedInt(Map<String, String> fields, String key) {
        String value = fields.getOrDefault(key, "0");
        if (value.isEmpty() || value.length() > 9) {
            return 0;
        }
        try {
            int parsed = Integer.parseInt(value);
            return parsed < 0 ? 0 : parsed;
        } catch (NumberFormatException exception) {
            return 0;
        }
    }

    /**
     * The daemon emits the percentage as {@code
     * std::to_string(double)}, for example {@code 42.500000}. Integer
     * parsing returned 0 for every row and would silently blank the
     * whole list, so parse a bounded double and round it for display.
     */
    private static int percent(Map<String, String> fields, String key) {
        String value = fields.getOrDefault(key, "0");
        if (value.isEmpty() || value.length() > 24) {
            return 0;
        }
        try {
            double parsed = Double.parseDouble(value);
            if (Double.isNaN(parsed) || parsed <= 0.0) {
                return 0;
            }
            if (parsed >= 100.0) {
                return 100;
            }
            return (int) Math.round(parsed);
        } catch (NumberFormatException exception) {
            return 0;
        }
    }

    /** The engine reports a negative or absent ETA as -1. */
    private static int eta(Map<String, String> fields, String key) {
        String value = fields.getOrDefault(key, "-1");
        if (value.isEmpty() || value.length() > 10) {
            return -1;
        }
        try {
            return Integer.parseInt(value);
        } catch (NumberFormatException exception) {
            return -1;
        }
    }
}

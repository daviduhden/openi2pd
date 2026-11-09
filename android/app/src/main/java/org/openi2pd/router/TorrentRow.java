/*
 * See the LICENSE file at the top of the project tree for copyright and
 * license details.
 *
 * openi2pd: TorrentRow.
 *
 * Typed, bounded view of one TORRENTS snapshot row.
 *
 * The desktop-compatible control protocol returns a flat key/value map;
 * parsing it in the fragment mixed bounds checks, presentation and
 * accessibility text.
 *
 * A record makes the field set explicit, rejects an out-of-range value
 * once and lets the JVM test the decoder without an Activity, a device
 * or the native library.
 *
 */

package org.openi2pd.router;

import java.util.Map;
import java.util.regex.Pattern;

/**
 * Immutable torrent summary decoded through {@link #from}.
 *
 * <p>The decoder bounds display text and counters. Malformed IDs are
 * rejected rather than replaced with invented targets. These checks
 * belong to the decoder; the canonical constructor does not validate
 * arbitrary caller-supplied components.</p>
 */
record TorrentRow(String id, String name, String status, String error, String hash, String magnet,
        long total, long wanted, long have, boolean privateTorrent, long downloaded, long uploaded,
        long downRate, long upRate, int peers, int seeders, int leechers, int percent,
        int etaSeconds) {
    private static final int MAX_NAME = 512;
    private static final int MAX_STATUS = 64;
    private static final int MAX_ERROR = 256;
    private static final int MAX_MAGNET = 2048;
    /* Compiled once: a snapshot decodes up to 200 rows per refresh. */
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
     * <p>The map is borrowed only for this synchronous decode. Callers
     * must not mutate it concurrently; the result retains strings and
     * primitives, not the map. Bounded state and error text remain
     * data until a presentation mapper recognizes an exact token.</p>
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

    /*
     * Two-line row label. All presentation vocabulary and the formatted
     * percentage come from the active UI locale, not the wire tokens.
     *
     * <p>The name is appended verbatim. Callers supply non-null labels;
     * this method neither translates user text nor escapes protocol
     * operands. Its result must not be used to construct commands.</p>
     */
    String displayLabel(String privateBadge, String stateLabel, String progress) {
        StringBuilder label = new StringBuilder(name.length() + status.length() + 48);
        label.append(name).append('\n').append(stateLabel).append(" · ").append(progress);
        if (privateTorrent && !privateBadge.isEmpty()) {
            label.append(" · ").append(privateBadge);
        }
        return label.toString();
    }

    /*
     * TalkBack description combining name, state and percentage.
     */
    String accessibilityLabel(String privateBadge, String stateLabel, String progress, String errorLabel) {
        StringBuilder label = new StringBuilder(name.length() + status.length() + 48);
        label.append(name).append(", ").append(stateLabel).append(", ").append(progress);
        if (privateTorrent && !privateBadge.isEmpty()) {
            label.append(", ").append(privateBadge);
        }
        if (hasError()) {
            label.append(", ").append(errorLabel);
        }
        return label.toString();
    }

    /*
     * Cap display text in UTF-16 units without splitting a valid pair.
     *
     * <p>This preserves a surrogate pair crossing the truncation point;
     * it does not validate malformed UTF-16 already inside the input or
     * preserve grapheme clusters. Decoder limits are positive.</p>
     */
    private static String text(Map<String, String> fields, String key, int max) {
        String value = fields.getOrDefault(key, "");
        return DisplayText.bound(value, max);
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

    /*
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

    /*
     * Preserve a parsed signed ETA; absent or invalid input becomes -1.
     * Other negative values remain negative for unknown-ETA rendering.
     */
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

/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: RouterStatusParser.
 *
 * Parsers for the native status snapshot and log stream; both inputs are
 * treated as untrusted even though they come from the in-process library.
 * Fields are length-limited, numeric values range-checked and addresses capped
 * at 32 entries, so malformed input degrades to defaults instead of throwing.
 *
 */

package org.openi2pd.router;

import java.util.ArrayList;
import java.util.Collections;
import java.util.HashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;

/**
 * Parsers for the two bounded native text formats.
 *
 * <p>Both formats are line oriented and are treated as untrusted input even
 * though they come from the in-process native library: values are length
 * bounded, numeric fields are range checked and no value is ever interpreted
 * as a path, command or format string.</p>
 *
 * <p>The parsers are pure and static: they perform no I/O, hold no state and
 * never throw for malformed input, so they run on the service worker thread
 * that fetched the text rather than on the main thread. The enforced limits are
 * {@value #MAX_ADDRESSES} addresses, {@value #MAX_RAW_LENGTH} characters of
 * input, a 64-character key and status field, a 128-character host and error
 * field and {@value #MAX_LOG_TEXT_LENGTH} characters of log text; a value
 * beyond its bound is truncated or ignored, never trusted.</p>
 *
 * <p>Every numeric or boolean field falls back to a default rather than
 * propagating an exception. A snapshot without a parsable {@code statusCode}
 * keeps the negative sentinel, so it is never reported as observed state even
 * though the remaining fields may have parsed.</p>
 */
final class RouterStatusParser {
    private static final int MAX_ADDRESSES = 32;
    private static final int MAX_RAW_LENGTH = 128 * 1024;
    private static final int MAX_LOG_TEXT_LENGTH = 16 * 1024;

    private RouterStatusParser() {
        // Static helpers.
    }

    /**
     * Parse the newline-separated {@code key=value} snapshot produced by
     * {@code I2PD_JNI.getStatusSnapshot()}.
     *
     * <p>A missing {@code statusCode} stays {@code -1} (the native error
     * snapshot value); {@code 0} is a valid code and must not be used as a
     * sentinel, because {@link RouterSnapshot#hasStatus()} would then report the
     * online state incorrectly. {@code online} defaults to "state started and
     * statusCode >= 0", so a truncated snapshot cannot make a failed router look
     * reachable.</p>
     *
     * <p>The returned record is display input only and carries no native handle.
     * Addresses are appended from the {@code ipv4}/{@code ipv6} fields in that
     * order and truncated to {@value #MAX_ADDRESSES}; entries without a
     * transport, host or positive port are dropped.</p>
     *
     * <p>Runs on the service worker thread; the {@code state}, {@code services},
     * grace deadline and diagnostic strings are supplied by the caller rather
     * than parsed from the text.</p>
     */
    static RouterSnapshot parseStatus(String raw, DaemonWrapper.State state, ServiceStates services,
            long graceRemainingMillis, String errorText, String startResult) {
        Map<String, String> values = parseFields(raw);

        DaemonWrapper.State safeState = state == null ? DaemonWrapper.State.uninitialized : state;
        boolean started = safeState.isStartedOkay();

        String status = bound(firstNonEmpty(values.get("status"), "Unknown"), 64);
        int statusCode = intValue(values.get("statusCode"), -1, true);
        String routerError = normalizeError(values.get("error"));
        long uptime = longValue(values.get("uptime"), 0L);
        // The native error snapshot uses statusCode=-1. If a key is missing
        // from a malformed snapshot, do not report a failed router as online
        // just because the Java state says the native process started.
        boolean online = booleanValue(values.get("online"), started && statusCode >= 0);
        boolean ntcp2 = booleanValue(values.get("ntcp2"), false);
        boolean ssu2 = booleanValue(values.get("ssu2"), false);
        int peers = intValue(values.get("peers"), 0, false);
        int routers = intValue(values.get("routers"), 0, false);
        int inbound = intValue(values.get("inbound"), 0, false);
        int outbound = intValue(values.get("outbound"), 0, false);
        int transit = intValue(values.get("transit"), 0, false);
        boolean reseeding = booleanValue(values.get("reseeding"), false);
        long inRate = longValue(values.get("inRate"), 0L);
        long outRate = longValue(values.get("outRate"), 0L);
        long inRate15 = longValue(values.get("inRate15"), 0L);
        long outRate15 = longValue(values.get("outRate15"), 0L);
        long totalIn = longValue(values.get("totalIn"), 0L);
        long totalOut = longValue(values.get("totalOut"), 0L);
        long totalTransit = longValue(values.get("totalTransit"), 0L);

        List<AddressInfo> addresses = new ArrayList<>(4);
        appendAddresses(values.get("ipv4"), addresses);
        appendAddresses(values.get("ipv6"), addresses);

        return new RouterSnapshot(safeState, started, online, status, statusCode, routerError,
                uptime, peers, routers, inbound, outbound, transit, reseeding, ntcp2, ssu2, inRate,
                outRate, inRate15, outRate15, totalIn, totalOut, totalTransit, addresses,
                services == null ? ServiceStates.NONE : services, graceRemainingMillis, errorText,
                startResult);
    }

    /**
     * Parse records of the form {@code sequence|timestampSeconds|level|text}.
     * The text field may contain {@code |}, so the split is limited to four
     * fields. Records at or below {@code afterSequence} are ignored.
     *
     * <p>The sequence cursor is the only ordering contract: entries are returned
     * in input order and the caller advances its cursor to the highest returned
     * sequence. Text is truncated to {@value #MAX_LOG_TEXT_LENGTH} characters,
     * the batch is rejected entirely above {@value #MAX_RAW_LENGTH} characters,
     * the line split stops at 512 records and an unparsable or negative sequence
     * drops that record instead of failing the batch. The level is normalised to
     * the textual native names; an unknown level is preserved verbatim so the
     * display filter can show it rather than silently reclassifying it.</p>
     *
     * <p>Runs on the service worker thread; log text is untrusted display data
     * and must not be logged again or interpreted as a format string.</p>
     */
    static List<LogEntry> parseLogs(String raw, long afterSequence) {
        if (raw == null || raw.isEmpty() || raw.length() > MAX_RAW_LENGTH) {
            return Collections.emptyList();
        }
        String[] lines = raw.split("\n", 512);
        List<LogEntry> entries = new ArrayList<>(Math.min(lines.length, 256));
        for (String line : lines) {
            if (line.isEmpty()) {
                continue;
            }
            String[] parts = line.split("\\|", 4);
            if (parts.length < 4) {
                continue;
            }
            long sequence = longValue(parts[0], -1L);
            long timestamp = longValue(parts[1], 0L);
            if (sequence < 0 || sequence <= afterSequence) {
                continue;
            }
            String text = parts[3];
            if (text.length() > MAX_LOG_TEXT_LENGTH) {
                text = text.substring(0, MAX_LOG_TEXT_LENGTH);
            }
            entries.add(new LogEntry(sequence, timestamp, normalizeLevel(parts[2]), text));
        }
        return entries;
    }

    private static Map<String, String> parseFields(String raw) {
        Map<String, String> values = new HashMap<>();
        if (raw == null || raw.isEmpty() || raw.length() > MAX_RAW_LENGTH) {
            return values;
        }
        String[] lines = raw.split("\n", -1);
        for (String line : lines) {
            int separator = line.indexOf('=');
            if (separator <= 0) {
                continue;
            }
            String key = line.substring(0, separator).trim();
            String value = line.substring(separator + 1).trim();
            if (!key.isEmpty() && key.length() <= 64 && value.length() <= MAX_RAW_LENGTH) {
                values.put(key, value);
            }
        }
        return values;
    }

    private static void appendAddresses(String raw, List<AddressInfo> addresses) {
        if (raw == null || raw.isEmpty()) {
            return;
        }
        String[] records = raw.split(";", MAX_ADDRESSES);
        for (String record : records) {
            if (addresses.size() >= MAX_ADDRESSES) {
                return;
            }
            String[] parts = record.split("\\|", 4);
            if (parts.length != 4) {
                continue;
            }
            String transport = bound(parts[0].trim(), 16);
            String host = bound(stripBrackets(parts[1].trim()), 128);
            int port = intValue(parts[2], 0, false);
            boolean published = booleanValue(parts[3], false);
            if (transport.isEmpty() || host.isEmpty() || port <= 0) {
                continue;
            }
            addresses.add(new AddressInfo(transport, host, port, published));
        }
    }

    private static String stripBrackets(String host) {
        if (host.length() >= 2 && host.charAt(0) == '[' && host.charAt(host.length() - 1) == ']') {
            return host.substring(1, host.length() - 1);
        }
        return host;
    }

    private static String firstNonEmpty(String value, String fallback) {
        return value == null || value.isEmpty() ? fallback : value;
    }

    private static String bound(String value, int maximum) {
        if (value == null) {
            return "";
        }
        return value.length() <= maximum ? value : value.substring(0, maximum);
    }

    private static String normalizeError(String error) {
        if (error == null || error.isEmpty() || "none".equalsIgnoreCase(error)
                || "ok".equalsIgnoreCase(error)) {
            return "";
        }
        return error.length() <= 128 ? error : error.substring(0, 128);
    }

    /**
     * The native ring currently serialises its numeric {@code LogLevel}
     * (0 none, 1 critical, 2 error, 3 warn, 4 info, 5 debug). Textual levels
     * are accepted too so the parser also works with the documented
     * sequence|timestamp|level|text variant.
     */
    private static String normalizeLevel(String raw) {
        String level = raw == null ? "" : raw.trim().toLowerCase(Locale.ROOT);
        return switch (level) {
            case "0", "none" -> "none";
            case "1", "2", "critical", "err", "error" -> "error";
            case "3", "warning", "warn" -> "warn";
            case "4", "information", "info" -> "info";
            case "5", "debug", "trace" -> "debug";
            default -> level.isEmpty() ? "info" : level;
        };
    }

    private static int intValue(String value, int fallback, boolean allowNegative) {
        long parsed = longValue(value, fallback);
        if (parsed > Integer.MAX_VALUE) {
            return Integer.MAX_VALUE;
        }
        if (parsed < 0 && !allowNegative) {
            return 0;
        }
        return (int) parsed;
    }

    private static long longValue(String value, long fallback) {
        if (value == null || value.isEmpty() || value.length() > 20) {
            return fallback;
        }
        try {
            return Long.parseLong(value.trim());
        } catch (NumberFormatException e) {
            return fallback;
        }
    }

    private static boolean booleanValue(String value, boolean fallback) {
        if (value == null) {
            return fallback;
        }
        return switch (value.trim().toLowerCase(Locale.ROOT)) {
            case "1", "true", "yes", "on" -> true;
            case "0", "false", "no", "off" -> false;
            default -> fallback;
        };
    }
}

/*
 * Copyright (c) 2013-2026, The PurpleI2P Project
 * Copyright (c) 2026, David Uhden Collado
 * SPDX-License-Identifier: BSD-3-Clause
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
     */
    static RouterSnapshot parseStatus(String raw, DaemonWrapper.State state, ServiceStates services,
            long graceRemainingMillis, String errorText, String startResult) {
        Map<String, String> values = parseFields(raw);

        DaemonWrapper.State safeState = state == null ? DaemonWrapper.State.uninitialized : state;
        boolean started = safeState.isStartedOkay();

        String status = bound(firstNonEmpty(values.get("status"), "Unknown"), 64);
        int statusCode = intValue(values.get("statusCode"), 0, true);
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
        switch (level) {
        case "0":
        case "none":
            return "none";
        case "1":
        case "2":
        case "critical":
        case "err":
        case "error":
            return "error";
        case "3":
        case "warning":
        case "warn":
            return "warn";
        case "4":
        case "information":
        case "info":
            return "info";
        case "5":
        case "debug":
        case "trace":
            return "debug";
        default:
            return level.isEmpty() ? "info" : level;
        }
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
        switch (value.trim().toLowerCase(Locale.ROOT)) {
        case "1":
        case "true":
        case "yes":
        case "on":
            return true;
        case "0":
        case "false":
        case "no":
        case "off":
            return false;
        default:
            return fallback;
        }
    }
}

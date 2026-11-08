/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * Localizes closed presentation vocabularies at the Android boundary.
 */

package org.openi2pd.router;

import android.content.Context;

import java.text.NumberFormat;
import java.util.Locale;

/**
 * Presentation-only mappings.
 *
 * <p>Protocol tokens remain unchanged in snapshots and commands.
 * Unknown text is returned unchanged; it is not treated as a resource
 * key or a format string. This mapper does not redact diagnostics, so
 * callers retain responsibility for safe publication.</p>
 *
 * <p>Never apply these mappings to names, paths, comments, tracker URLs
 * or other user data.</p>
 */
final class UiText {
    private UiText() {}

    /**
     * Canonical preference value for an explicit per-app locale.
     *
     * <p>An explicit Chinese script takes precedence over the region.
     * Hant and unsupported scripts fall back to English; a bare Chinese
     * tag, CN or SG selects Simplified Chinese. The System preference
     * is represented by an empty AppCompat locale list outside this
     * method, not by a synthetic locale.</p>
     */
    static String languageChoice(Locale locale) {
        if (locale == null) return "en";
        return switch (locale.getLanguage()) {
            case "en", "es", "ru" -> locale.getLanguage();
            case "zh" -> {
                String script = locale.getScript();
                String region = locale.getCountry();
                if ("Hant".equals(script)) yield "en";
                if ("Hans".equals(script)) yield "zh-CN";
                if (!script.isEmpty()) yield "en";
                yield region.isEmpty() || "CN".equals(region) || "SG".equals(region) ? "zh-CN" : "en";
            }
            default -> "en";
        };
    }

    /**
     * Translate the closed torrent-state vocabulary at rendering time.
     *
     * <p>Null or empty tokens render as empty text. New tokens remain
     * visible unchanged, without modifying the snapshot value.
     * The supplied context determines resource selection.</p>
     */
    static String torrentStatus(Context context, String token) {
        if (token == null || token.isEmpty()) return "";
        int resource = switch (token) {
            case "stopped" -> R.string.torrent_status_stopped;
            case "queued-verify" -> R.string.torrent_status_queued_verify;
            case "verifying" -> R.string.torrent_status_verifying;
            case "queued" -> R.string.torrent_status_queued;
            case "downloading" -> R.string.torrent_status_downloading;
            case "queued-seed" -> R.string.torrent_status_queued_seed;
            case "seeding" -> R.string.torrent_status_seeding;
            case "unknown" -> R.string.status_unknown;
            default -> 0;
        };
        return resource == 0 ? token : context.getString(resource);
    }

    static String routerStatus(Context context, String token) {
        if (token == null || token.isEmpty()) return "";
        int resource = switch (token) {
            case "OK" -> R.string.state_connected;
            case "Firewalled" -> R.string.router_status_firewalled;
            case "Proxy" -> R.string.router_status_proxy;
            case "Mesh" -> R.string.router_status_mesh;
            case "Unknown" -> R.string.status_unknown;
            default -> 0;
        };
        return resource == 0 ? token : context.getString(resource);
    }

    /**
     * Map only exact, known diagnostics; remote text is not
     * reinterpreted.
     */
    static String diagnostic(Context context, String text) {
        if (text == null || text.isEmpty()) return "";
        int resource = switch (text) {
            case "Permission denied" -> R.string.operation_permission_denied;
            case "I/O operation failed", "request failed" -> R.string.operation_io_failed;
            case "Operation timed out", "No response" -> R.string.operation_timed_out;
            case "Operation cancelled" -> R.string.operation_cancelled;
            case "Service is stopping" -> R.string.operation_service_stopping;
            case "Native router unavailable" -> R.string.operation_native_unavailable;
            case "Invalid operation" -> R.string.operation_invalid;
            case "Operation failed" -> R.string.operation_failed;
            case "Malformed metaInfo" -> R.string.torrent_error_metainfo;
            case "Non-supported version" -> R.string.torrent_error_version;
            case "Filesystem error" -> R.string.torrent_error_filesystem;
            case "Random source failure" -> R.string.torrent_error_random;
            case "Invalid magnet link" -> R.string.torrent_invalid_magnet;
            case "Invalid torrent selection", "Invalid comment selection", "Invalid comment ID" ->
                    R.string.torrent_invalid_selection;
            case "Invalid metainfo text" -> R.string.torrent_invalid_metainfo;
            case "Invalid I2P tracker URL", "Use an http:// or udp:// I2P tracker without credentials" ->
                    R.string.torrent_invalid_tracker;
            case "Invalid comment or rating", "Invalid comment page" -> R.string.torrent_invalid_comment;
            case "Invalid torrent session" -> R.string.torrent_invalid_session;
            case "Invalid control response", "Invalid control header", "Invalid response length",
                    "Invalid response field", "Duplicate response field", "malformed response",
                    "oversized response" -> R.string.torrent_invalid_response;
            case "clock-skew", "Clock skew" -> R.string.router_error_clock;
            case "offline", "Offline" -> R.string.status_offline;
            case "symmetric-nat", "Symmetric NAT" -> R.string.router_error_symmetric_nat;
            case "full-cone-nat", "Full cone NAT" -> R.string.router_error_full_cone_nat;
            case "no-descriptors", "No descriptors" -> R.string.router_error_descriptors;
            default -> 0;
        };
        return resource == 0 ? text : context.getString(resource);
    }

    /**
     * Format a whole percentage after clamping it to the display range.
     *
     * <p>Formatters are created per call rather than shared: mutable
     * NumberFormat instances must not carry locale or synchronization
     * state between independently rendered snapshots.</p>
     */
    static String percent(Context context, int value) {
        return NumberFormat.getPercentInstance(locale(context))
                .format(Math.max(0, Math.min(value, 100)) / 100.0);
    }

    /**
     * Numeric control fields use invariant wire syntax until rendering.
     */
    static String counter(Context context, String token) {
        if (token == null) return context.getString(R.string.value_unknown);
        try {
            return UiFormat.count(context, Long.parseLong(token));
        } catch (NumberFormatException ignored) {
            return context.getString(R.string.value_unknown);
        }
    }

    /**
     * Parse invariant peer progress, then format in the context locale.
     *
     * <p>Malformed or non-finite input becomes the unknown-value label.
     * Finite values are clamped to 0..100 and shown with at most one
     * decimal. Localized output is never a command operand.</p>
     */
    static String peerProgress(Context context, String token) {
        if (token == null) return context.getString(R.string.value_unknown);
        try {
            double value = Double.parseDouble(token);
            if (Double.isNaN(value) || Double.isInfinite(value))
                return context.getString(R.string.value_unknown);
            NumberFormat format = NumberFormat.getPercentInstance(locale(context));
            format.setMaximumFractionDigits(1);
            return format.format(Math.max(0.0, Math.min(value, 100.0)) / 100.0);
        } catch (NumberFormatException ignored) {
            return context.getString(R.string.value_unknown);
        }
    }

    private static Locale locale(Context context) {
        var locales = context.getResources().getConfiguration().getLocales();
        return locales.isEmpty() ? Locale.ENGLISH : locales.get(0);
    }
}

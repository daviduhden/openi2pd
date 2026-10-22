/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: UiFormat.
 *
 * Pure presentation helpers mapping snapshots to state labels, colours,
 * addresses, rates, durations, byte counts and percentages with
 * English-pinned number formatting. They never call JNI or perform I/O and
 * only rewrite a TextView when its text actually changes.
 *
 */

package org.openi2pd.router;

import android.content.Context;
import android.text.TextUtils;
import android.widget.TextView;

import androidx.annotation.ColorRes;
import androidx.annotation.NonNull;
import androidx.annotation.Nullable;

import java.text.NumberFormat;
import java.util.List;
import java.util.Locale;

/**
 * Presentation helpers for router snapshots.
 *
 * <p>This class never calls JNI and never performs I/O. It only converts the
 * immutable snapshot published by {@link RouterViewModel} into text that is
 * safe to show on the UI thread.</p>
 */
public final class UiFormat {
    private static final int MAX_ERROR_LENGTH = 240;

    public enum RouterState { UNKNOWN, STOPPED, STARTING, CONNECTED, DEGRADED, STOPPING, ERROR }

    private UiFormat() {
        // Static helpers only.
    }

    @NonNull
    public static RouterState stateOf(@Nullable RouterSnapshot snapshot) {
        if (snapshot == null || snapshot.state() == null) {
            return RouterState.UNKNOWN;
        }
        return switch (snapshot.state()) {
            case uninitialized, starting -> RouterState.STARTING;
            case startedOkay ->
                snapshot.routerStatusCode() < 0
                        ? RouterState.ERROR
                        : (snapshot.hasStatus() && snapshot.online() ? RouterState.CONNECTED
                                                                     : RouterState.DEGRADED);
            case startFailed -> RouterState.ERROR;
            case gracefulShutdownInProgress, stopping -> RouterState.STOPPING;
            case stopped -> RouterState.STOPPED;
        };
    }

    /**
     * Colour role for the router state. The status title is the primary state
     * cue, so it must not rely on text alone.
     */
    @ColorRes
    public static int stateColorRes(@NonNull RouterState state) {
        return switch (state) {
            case CONNECTED -> R.color.brand_success;
            case DEGRADED -> R.color.brand_warning;
            case STARTING, STOPPING -> R.color.brand_primary;
            case ERROR -> R.color.brand_error;
            case STOPPED, UNKNOWN -> R.color.brand_on_surface_variant;
        };
    }

    /** Native status name plus the router error, or empty when not parsed. */
    @NonNull
    public static String routerDetail(@Nullable RouterSnapshot snapshot) {
        if (snapshot == null || !snapshot.hasStatus()) return "";
        String status = snapshot.routerStatus() == null ? "" : snapshot.routerStatus().trim();
        String error = snapshot.routerError() == null ? "" : snapshot.routerError().trim();
        if (status.isEmpty()) return "";
        return error.isEmpty() ? status : status + " · " + error;
    }

    /** True only when live counters from a parsed native status are available. */
    public static boolean hasLiveMetrics(@Nullable RouterSnapshot snapshot) {
        return snapshot != null && snapshot.hasLiveMetrics();
    }

    /** True when the daemon itself has completed its native start. */
    public static boolean isStarted(@Nullable RouterSnapshot snapshot) {
        return snapshot != null && snapshot.state() != null && snapshot.state().isStartedOkay();
    }

    public static int stateLabelRes(@NonNull RouterState state) {
        return switch (state) {
            case CONNECTED -> R.string.state_connected;
            case DEGRADED -> R.string.state_degraded;
            case STARTING -> R.string.state_starting;
            case STOPPING -> R.string.state_stopping;
            case STOPPED -> R.string.state_stopped;
            case ERROR -> R.string.state_error;
            case UNKNOWN -> R.string.state_unknown;
        };
    }

    public static boolean isRunning(@Nullable RouterSnapshot snapshot) {
        RouterState state = stateOf(snapshot);
        return state == RouterState.CONNECTED || state == RouterState.DEGRADED
                || state == RouterState.STOPPING;
    }

    public static boolean isConnected(@Nullable RouterSnapshot snapshot) {
        RouterState state = stateOf(snapshot);
        return state == RouterState.CONNECTED || state == RouterState.DEGRADED;
    }

    public static boolean canReseed(@Nullable RouterSnapshot snapshot) {
        return isConnected(snapshot) && snapshot != null && !snapshot.reseeding();
    }

    public static boolean canStart(@Nullable RouterSnapshot snapshot) {
        if (snapshot == null || snapshot.state() == null) {
            return false;
        }
        return snapshot.state() == DaemonWrapper.State.stopped
                || snapshot.state() == DaemonWrapper.State.startFailed;
    }

    /**
     * Stop is based on the real daemon lifecycle, not on presentation text. A
     * started router whose status snapshot failed must still be stoppable.
     */
    public static boolean canStop(@Nullable RouterSnapshot snapshot) {
        if (snapshot == null || snapshot.state() == null) {
            return false;
        }
        DaemonWrapper.State state = snapshot.state();
        return state.isStartedOkay() || state == DaemonWrapper.State.starting
                || state == DaemonWrapper.State.stopping
                || state == DaemonWrapper.State.startFailed;
    }

    public static boolean canRestart(@Nullable RouterSnapshot snapshot) {
        return snapshot != null && snapshot.state() == DaemonWrapper.State.startedOkay;
    }

    /** Begin (or cancel) a graceful stop only for a normally started router. */
    public static boolean canGraceful(@Nullable RouterSnapshot snapshot) {
        return canRestart(snapshot);
    }

    public static boolean gracefulActive(@Nullable RouterSnapshot snapshot) {
        return snapshot != null
                && (snapshot.graceRemainingMillis() > 0L
                        || snapshot.state() == DaemonWrapper.State.gracefulShutdownInProgress);
    }

    @NonNull
    public static String statusSubtitle(
            @NonNull Context context, @Nullable RouterSnapshot snapshot) {
        if (snapshot == null) {
            return context.getString(R.string.status_service_connecting);
        }
        RouterState state = stateOf(snapshot);
        return switch (state) {
            case STARTING -> context.getString(R.string.status_router_starting);
            case STOPPED -> context.getString(R.string.status_router_stopped);
            case STOPPING ->
                snapshot.state() == DaemonWrapper.State.stopping
                        ? context.getString(R.string.state_stopping)
                        : context.getString(R.string.status_router_graceful,
                                  grace(context, snapshot.graceRemainingMillis()));
            case CONNECTED ->
                snapshot.reseeding() ? context.getString(R.string.status_router_reseeding)
                                     : context.getString(R.string.status_router_online);
            case DEGRADED ->
                snapshot.nativeStarted() && !snapshot.hasStatus()
                        ? context.getString(R.string.status_status_unavailable)
                        : context.getString(R.string.status_router_degraded);
            case ERROR -> {
                String detail = errorDetail(snapshot);
                if (snapshot.state() == DaemonWrapper.State.startFailed) {
                    yield detail.isEmpty() ? context.getString(R.string.state_error)
                                           : context.getString(R.string.status_start_failed_format,
                                                     summarize(detail));
                }
                yield detail.isEmpty()
                        ? context.getString(R.string.status_status_unavailable)
                        : context.getString(
                                  R.string.status_status_unavailable_format, summarize(detail));
            }
            case UNKNOWN -> context.getString(R.string.status_service_connecting);
        };
    }

    @Nullable
    public static String warningText(@NonNull Context context, @Nullable RouterSnapshot snapshot) {
        if (snapshot == null) {
            return null;
        }
        RouterState state = stateOf(snapshot);
        if (state == RouterState.ERROR) {
            String detail = errorDetail(snapshot);
            if (snapshot.state() == DaemonWrapper.State.startFailed) {
                return detail.isEmpty() ? context.getString(R.string.warning_router_error)
                                        : context.getString(R.string.warning_start_failed_format,
                                                  summarize(detail));
            }
            return detail.isEmpty() ? context.getString(R.string.warning_status_unavailable)
                                    : context.getString(R.string.warning_status_unavailable_format,
                                              summarize(detail));
        }
        if (state == RouterState.DEGRADED && snapshot.nativeStarted() && !snapshot.hasStatus()) {
            return context.getString(R.string.warning_status_unavailable);
        }
        if (state == RouterState.CONNECTED || state == RouterState.DEGRADED) {
            String error = snapshot.errorText();
            if (!TextUtils.isEmpty(error)) {
                return context.getString(R.string.warning_error_format, summarize(error));
            }
            if (!snapshot.online()) {
                return context.getString(R.string.warning_offline);
            }
        }
        return null;
    }

    @NonNull
    public static String connectivity(@NonNull Context context, @Nullable RouterSnapshot snapshot) {
        if (!hasLiveMetrics(snapshot) || stateOf(snapshot) == RouterState.UNKNOWN
                || stateOf(snapshot) == RouterState.STARTING) {
            return context.getString(R.string.value_unknown);
        }
        return context.getString(
                snapshot.online() ? R.string.status_online : R.string.status_offline);
    }

    @NonNull
    public static String transport(
            @NonNull Context context, @Nullable RouterSnapshot snapshot, boolean ntcp2) {
        String value = hasLiveMetrics(snapshot)
                ? enabledDisabled(context, ntcp2 ? snapshot.ntcp2() : snapshot.ssu2())
                : context.getString(R.string.value_unknown);
        return context.getString(R.string.network_transport_format,
                context.getString(ntcp2 ? R.string.network_ntcp2 : R.string.network_ssu2), value);
    }

    @NonNull
    public static String transports(@NonNull Context context, @Nullable RouterSnapshot snapshot) {
        return context.getString(R.string.dashboard_transports_format,
                transport(context, snapshot, true), transport(context, snapshot, false));
    }

    @NonNull
    public static String enabledDisabled(@NonNull Context context, boolean enabled) {
        return context.getString(enabled ? R.string.value_enabled : R.string.value_disabled);
    }

    @NonNull
    public static String yesNo(@NonNull Context context, boolean value) {
        return context.getString(value ? R.string.value_yes : R.string.value_no);
    }

    @NonNull
    public static String published(@NonNull Context context, boolean value) {
        return context.getString(value ? R.string.value_published : R.string.value_not_published);
    }

    @NonNull
    public static String serviceState(@NonNull Context context, boolean running) {
        return context.getString(running ? R.string.value_running : R.string.value_stopped);
    }

    @NonNull
    public static String address(@NonNull Context context, @NonNull AddressInfo address) {
        String transport = TextUtils.isEmpty(address.transport())
                ? context.getString(R.string.value_unknown_long)
                : address.transport();
        String host = address.host() == null ? "" : address.host().trim();
        if (host.isEmpty()) {
            host = context.getString(R.string.value_unknown_long);
        } else if (isIpv6(host) && !host.startsWith("[")) {
            host = "[" + host + "]";
        }
        return context.getString(R.string.address_format, transport, host, address.port(),
                published(context, address.published()));
    }

    public static boolean isIpv6(@Nullable String host) {
        return host != null && host.indexOf(':') >= 0;
    }

    @NonNull
    public static String count(@NonNull Context context, long value) {
        return NumberFormat.getIntegerInstance(locale(context)).format(Math.max(0L, value));
    }

    @NonNull
    public static String rate(@NonNull Context context, long bytesPerSecond) {
        long value = Math.max(0L, bytesPerSecond);
        if (value < 1024L) {
            return context.getString(R.string.rate_bps, decimal(context, value));
        }
        double kib = value / 1024.0d;
        if (kib < 1024.0d) {
            return context.getString(R.string.rate_kib_s, decimal(context, kib));
        }
        double mib = kib / 1024.0d;
        if (mib < 1024.0d) {
            return context.getString(R.string.rate_mib_s, decimal(context, mib));
        }
        return context.getString(R.string.rate_gib_s, decimal(context, mib / 1024.0d));
    }

    @NonNull
    public static String bytes(@NonNull Context context, long byteCount) {
        long value = Math.max(0L, byteCount);
        if (value < 1024L) {
            return context.getString(R.string.size_b, decimal(context, value));
        }
        double kib = value / 1024.0d;
        if (kib < 1024.0d) {
            return context.getString(R.string.size_kib, decimal(context, kib));
        }
        double mib = kib / 1024.0d;
        if (mib < 1024.0d) {
            return context.getString(R.string.size_mib, decimal(context, mib));
        }
        return context.getString(R.string.size_gib, decimal(context, mib / 1024.0d));
    }

    @NonNull
    public static String duration(@NonNull Context context, long seconds) {
        long value = Math.max(0L, seconds);
        if (value < 60L) {
            return context.getString(R.string.duration_seconds, value);
        }
        long minutes = value / 60L;
        if (minutes < 60L) {
            return context.getString(R.string.duration_minutes_seconds, minutes, value % 60L);
        }
        long hours = minutes / 60L;
        if (hours < 24L) {
            return context.getString(R.string.duration_hours_minutes, hours, minutes % 60L);
        }
        return context.getString(R.string.duration_days_hours, hours / 24L, hours % 24L);
    }

    @NonNull
    public static String grace(@NonNull Context context, long remainingMillis) {
        long seconds = Math.max(0L, Math.round(remainingMillis / 1000.0d));
        long minutes = seconds / 60L;
        long rest = seconds % 60L;
        return context.getString(R.string.duration_mm_ss, minutes, rest);
    }

    @NonNull
    public static String addressList(
            @NonNull Context context, @Nullable List<AddressInfo> addresses, boolean ipv6) {
        if (addresses == null || addresses.isEmpty()) {
            return context.getString(R.string.network_no_addresses);
        }
        StringBuilder result = new StringBuilder();
        for (AddressInfo address : addresses) {
            if (address == null || isIpv6(address.host()) != ipv6) {
                continue;
            }
            if (result.length() > 0) {
                result.append('\n');
            }
            result.append(address(context, address));
        }
        return result.length() == 0 ? context.getString(R.string.network_no_addresses)
                                    : result.toString();
    }

    public static boolean hasAddress(@Nullable List<AddressInfo> addresses, boolean ipv6) {
        if (addresses == null) {
            return false;
        }
        for (AddressInfo address : addresses) {
            if (address != null && isIpv6(address.host()) == ipv6) {
                return true;
            }
        }
        return false;
    }

    @NonNull
    public static String dash(@NonNull Context context) {
        return context.getString(R.string.value_unknown);
    }

    @NonNull
    public static String safe(@NonNull Context context, @Nullable String value) {
        return TextUtils.isEmpty(value) ? context.getString(R.string.value_unknown) : value;
    }

    /**
     * Avoids re-layout when a one-second snapshot carries an unchanged field.
     * All text still comes from the immutable snapshot; this only skips the
     * redundant {@link TextView#setText(CharSequence)} call.
     */
    public static void setTextIfChanged(@NonNull TextView view, @NonNull CharSequence text) {
        if (!TextUtils.equals(view.getText(), text)) {
            view.setText(text);
        }
    }

    @NonNull
    private static String errorDetail(@NonNull RouterSnapshot snapshot) {
        return firstNonEmpty(snapshot.routerError(), snapshot.errorText(), snapshot.startResult());
    }

    @NonNull
    private static String firstNonEmpty(@Nullable String... values) {
        if (values == null) {
            return "";
        }
        for (String value : values) {
            if (!TextUtils.isEmpty(value) && !"N/A".equals(value) && !"None".equals(value)
                    && !"ok".equalsIgnoreCase(value)) {
                return value;
            }
        }
        return "";
    }

    @NonNull
    private static String summarize(@Nullable String value) {
        if (TextUtils.isEmpty(value)) {
            return "";
        }
        String first = value;
        int newline = first.indexOf('\n');
        if (newline >= 0) {
            first = first.substring(0, newline);
        }
        first = first.trim();
        if (first.length() > MAX_ERROR_LENGTH) {
            return first.substring(0, MAX_ERROR_LENGTH) + "…";
        }
        return first;
    }

    @NonNull
    private static Locale locale(@NonNull Context context) {
        // Use the active per-app UI locale, not the device default. Token
        // classification elsewhere still uses Locale.ROOT because protocol
        // names and log levels are not user text.
        android.content.res.Configuration configuration =
                context.getResources().getConfiguration();
        if (configuration.getLocales().isEmpty()) {
            return Locale.ENGLISH;
        }
        Locale active = configuration.getLocales().get(0);
        return active == null ? Locale.ENGLISH : active;
    }

    @NonNull
    private static String decimal(@NonNull Context context, double value) {
        NumberFormat format = NumberFormat.getNumberInstance(locale(context));
        format.setMaximumFractionDigits(1);
        format.setMinimumFractionDigits(0);
        return format.format(value);
    }
}

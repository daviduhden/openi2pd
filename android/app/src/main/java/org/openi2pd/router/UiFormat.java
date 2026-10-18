/*
 * Copyright (c) 2013-2026, The PurpleI2P Project
 * Copyright (c) 2026, David Uhden Collado
 * SPDX-License-Identifier: BSD-3-Clause
 */
package org.openi2pd.router;

import android.content.Context;
import android.text.TextUtils;

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
                snapshot.online() ? RouterState.CONNECTED
                                  : (snapshot.routerStatusCode() < 0 ? RouterState.ERROR
                                                                     : RouterState.DEGRADED);
            case startFailed -> RouterState.ERROR;
            case gracefulShutdownInProgress -> RouterState.STOPPING;
            case stopped -> RouterState.STOPPED;
        };
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

    public static boolean canStart(@Nullable RouterSnapshot snapshot) {
        RouterState state = stateOf(snapshot);
        return state == RouterState.STOPPED || state == RouterState.ERROR;
    }

    public static boolean canStop(@Nullable RouterSnapshot snapshot) {
        RouterState state = stateOf(snapshot);
        return state == RouterState.CONNECTED || state == RouterState.DEGRADED
                || state == RouterState.STARTING || state == RouterState.STOPPING;
    }

    public static boolean canRestart(@Nullable RouterSnapshot snapshot) {
        RouterState state = stateOf(snapshot);
        return state == RouterState.CONNECTED || state == RouterState.DEGRADED;
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
                context.getString(R.string.status_router_graceful,
                        grace(context, snapshot.graceRemainingMillis()));
            case CONNECTED ->
                snapshot.reseeding() ? context.getString(R.string.status_router_reseeding)
                                     : context.getString(R.string.status_router_online);
            case DEGRADED -> context.getString(R.string.status_router_degraded);
            case ERROR -> {
                String detail = firstNonEmpty(
                        snapshot.routerError(), snapshot.errorText(), snapshot.startResult());
                yield detail.isEmpty()
                        ? context.getString(R.string.state_error)
                        : context.getString(R.string.status_start_failed_format, summarize(detail));
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
            String detail = firstNonEmpty(
                    snapshot.routerError(), snapshot.errorText(), snapshot.startResult());
            if (!detail.isEmpty()) {
                return context.getString(R.string.warning_start_failed_format, summarize(detail));
            }
            return context.getString(R.string.warning_service_disconnected);
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
        if (snapshot == null || stateOf(snapshot) == RouterState.UNKNOWN) {
            return context.getString(R.string.value_unknown);
        }
        if (stateOf(snapshot) == RouterState.STARTING) {
            return context.getString(R.string.value_unknown);
        }
        return context.getString(
                snapshot.online() ? R.string.status_online : R.string.status_offline);
    }

    @NonNull
    public static String transports(@NonNull Context context, @Nullable RouterSnapshot snapshot) {
        if (snapshot == null) {
            return context.getString(R.string.value_unknown);
        }
        String ntcp2 = context.getString(R.string.network_transport_format,
                context.getString(R.string.network_ntcp2),
                enabledDisabled(context, snapshot.ntcp2()));
        String ssu2 = context.getString(R.string.network_transport_format,
                context.getString(R.string.network_ssu2),
                enabledDisabled(context, snapshot.ssu2()));
        return context.getString(R.string.dashboard_transports_format, ntcp2, ssu2);
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
        return context.getResources().getConfiguration().getLocales().get(0);
    }

    @NonNull
    private static String decimal(@NonNull Context context, double value) {
        NumberFormat format = NumberFormat.getNumberInstance(locale(context));
        format.setMaximumFractionDigits(1);
        format.setMinimumFractionDigits(0);
        return format.format(value);
    }
}

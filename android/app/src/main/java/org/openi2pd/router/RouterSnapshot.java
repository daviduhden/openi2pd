/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: RouterSnapshot.
 *
 * Immutable record of everything the UI may display: daemon state, parsed
 * status, counters, addresses, client-service flags and error text. The
 * compact constructor clamps counters and copies the address list, while
 * {@link #hasStatus()} lets screens distinguish observed metrics from
 * zero-initialised defaults.
 *
 */

package org.openi2pd.router;

import java.util.ArrayList;
import java.util.Collections;
import java.util.List;

/**
 * Immutable, presentation-safe view of the router.
 *
 * <p>The service publishes this object to the UI on the main thread. It holds
 * counters and status strings rather than native handles or sockets. The
 * diagnostic strings can include exception details; consumers must not
 * assume they have been redacted.</p>
 */
public record RouterSnapshot(DaemonWrapper.State state, boolean nativeStarted, boolean online,
        String routerStatus, int routerStatusCode, String routerError, long uptimeSeconds,
        int peers, int routers, int inboundTunnels, int outboundTunnels, int transitTunnels,
        boolean reseeding, boolean ntcp2, boolean ssu2, long inRate, long outRate, long inRate15,
        long outRate15, long totalReceived, long totalSent, long totalTransit,
        List<AddressInfo> addresses, ServiceStates services, long graceRemainingMillis,
        String errorText, String startResult) {
    public RouterSnapshot {
        state = state == null ? DaemonWrapper.State.uninitialized : state;
        routerStatus = routerStatus == null ? "" : routerStatus;
        routerError = routerError == null ? "" : routerError;
        errorText = errorText == null ? "" : errorText;
        startResult = startResult == null ? "" : startResult;
        uptimeSeconds = Math.max(0L, uptimeSeconds);
        peers = Math.max(0, peers);
        routers = Math.max(0, routers);
        inboundTunnels = Math.max(0, inboundTunnels);
        outboundTunnels = Math.max(0, outboundTunnels);
        transitTunnels = Math.max(0, transitTunnels);
        inRate = Math.max(0L, inRate);
        outRate = Math.max(0L, outRate);
        inRate15 = Math.max(0L, inRate15);
        outRate15 = Math.max(0L, outRate15);
        totalReceived = Math.max(0L, totalReceived);
        totalSent = Math.max(0L, totalSent);
        totalTransit = Math.max(0L, totalTransit);
        graceRemainingMillis = Math.max(0L, graceRemainingMillis);
        services = services == null ? ServiceStates.NONE : services;
        if (addresses == null || addresses.isEmpty()) {
            addresses = Collections.emptyList();
        } else {
            addresses = Collections.unmodifiableList(new ArrayList<>(addresses));
        }
    }

    /**
     * True when the native status snapshot has actually been parsed. The
     * daemon can be started while the status bridge is still unavailable (for
     * example on an older native build or immediately after start); callers
     * must not present the zero-initialised counters as observed metrics.
     */
    public boolean hasStatus() {
        return nativeStarted && routerStatusCode >= 0 && !routerStatus.isEmpty()
                && !"Unknown".equalsIgnoreCase(routerStatus);
    }

    /** True when this snapshot contains live, observed router metrics. */
    public boolean hasLiveMetrics() {
        return hasStatus();
    }

    /**
     * Snapshot without native counters. Safe to build on the main thread: it
     * only reads already-cached Java state from {@link DaemonWrapper}.
     */
    public static RouterSnapshot empty(DaemonWrapper.State state, String errorText,
            String startResult, long graceRemainingMillis) {
        DaemonWrapper.State safeState = state == null ? DaemonWrapper.State.uninitialized : state;
        return new RouterSnapshot(safeState, safeState.isStartedOkay(), false, "", 0, "", 0L, 0, 0,
                0, 0, 0, false, false, false, 0L, 0L, 0L, 0L, 0L, 0L, 0L, Collections.emptyList(),
                ServiceStates.NONE, graceRemainingMillis, errorText, startResult);
    }
}

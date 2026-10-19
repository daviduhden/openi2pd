/*
 * Copyright (c) 2026, David Uhden Collado
 * SPDX-License-Identifier: BSD-3-Clause
 */
package org.openi2pd.router;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotEquals;
import static org.junit.Assert.assertTrue;

import org.junit.Test;

import java.util.Collections;

/**
 * Pure-JVM tests for the state/format helpers. These methods do not touch
 * Android resources, so no emulator or Robolectric is required.
 */
public class UiFormatTest {
    private static RouterSnapshot snapshot(DaemonWrapper.State state, boolean online) {
        return new RouterSnapshot(state, state != null && state.isStartedOkay(), online, "", 0, "",
                0L, 0, 0, 0, 0, 0, false, false, false, 0L, 0L, 0L, 0L, 0L, 0L, 0L,
                Collections.emptyList(), ServiceStates.NONE, 0L, "", "");
    }

    private static RouterSnapshot snapshotWithStatus(DaemonWrapper.State state, boolean online,
            int statusCode, String status, String error) {
        return new RouterSnapshot(state, state != null && state.isStartedOkay(), online, status,
                statusCode, error, 0L, 1, 2, 3, 4, 5, false, true, true, 6L, 7L, 8L, 9L, 10L,
                11L, 12L, Collections.emptyList(), ServiceStates.NONE, 0L, error, "ok");
    }

    @Test
    public void mapsDaemonStatesToPresentationStates() {
        assertEquals(UiFormat.RouterState.UNKNOWN, UiFormat.stateOf(null));
        assertEquals(UiFormat.RouterState.STOPPED,
                UiFormat.stateOf(snapshot(DaemonWrapper.State.stopped, false)));
        assertEquals(UiFormat.RouterState.STARTING,
                UiFormat.stateOf(snapshot(DaemonWrapper.State.starting, false)));
        assertEquals(UiFormat.RouterState.ERROR,
                UiFormat.stateOf(snapshot(DaemonWrapper.State.startFailed, false)));
        assertEquals(UiFormat.RouterState.CONNECTED,
                UiFormat.stateOf(snapshot(DaemonWrapper.State.startedOkay, true)));
        assertEquals(UiFormat.RouterState.DEGRADED,
                UiFormat.stateOf(snapshot(DaemonWrapper.State.startedOkay, false)));
        assertEquals(UiFormat.RouterState.STOPPING,
                UiFormat.stateOf(snapshot(DaemonWrapper.State.gracefulShutdownInProgress, true)));
        assertEquals(UiFormat.RouterState.STOPPING,
                UiFormat.stateOf(snapshot(DaemonWrapper.State.stopping, false)));
    }

    @Test
    public void enablementFollowsThePublishedState() {
        RouterSnapshot stopped = snapshot(DaemonWrapper.State.stopped, false);
        RouterSnapshot connected = snapshot(DaemonWrapper.State.startedOkay, true);
        RouterSnapshot degraded = snapshot(DaemonWrapper.State.startedOkay, false);
        RouterSnapshot starting = snapshot(DaemonWrapper.State.starting, false);
        RouterSnapshot stopping = snapshot(DaemonWrapper.State.stopping, false);
        RouterSnapshot failed = snapshot(DaemonWrapper.State.startFailed, false);

        assertTrue(UiFormat.canStart(stopped));
        assertTrue(UiFormat.canStart(failed));
        assertFalse(UiFormat.canStart(connected));
        assertFalse(UiFormat.canStart(starting));

        assertTrue(UiFormat.canStop(connected));
        assertTrue(UiFormat.canStop(degraded));
        assertTrue(UiFormat.canStop(starting));
        assertTrue(UiFormat.canStop(stopping));
        assertTrue("a failed start must be dismissible/stopped", UiFormat.canStop(failed));
        assertFalse(UiFormat.canStop(stopped));

        assertTrue(UiFormat.canRestart(connected));
        assertTrue(UiFormat.canRestart(degraded));
        assertFalse(UiFormat.canRestart(starting));
        assertFalse(UiFormat.canRestart(stopped));
        assertTrue(UiFormat.canGraceful(connected));
        assertFalse(UiFormat.canGraceful(failed));

        assertTrue(UiFormat.isRunning(connected));
        assertTrue(UiFormat.isRunning(degraded));
        assertTrue(UiFormat.isRunning(stopping));
        assertFalse(UiFormat.isRunning(stopped));
        assertTrue(UiFormat.isConnected(connected));
        assertFalse(UiFormat.isConnected(starting));
    }

    @Test
    public void gracefulStopStateSurvivesAZeroRemainingValue() {
        RouterSnapshot graceful = snapshot(DaemonWrapper.State.gracefulShutdownInProgress, true);
        assertTrue(UiFormat.gracefulActive(graceful));
        assertFalse(UiFormat.gracefulActive(snapshot(DaemonWrapper.State.startedOkay, true)));
        assertFalse(UiFormat.gracefulActive(null));

        RouterSnapshot draining = new RouterSnapshot(DaemonWrapper.State.startedOkay, true, true,
                "", 0, "", 0L, 0, 0, 0, 0, 0, false, false, false, 0L, 0L, 0L, 0L, 0L, 0L, 0L,
                Collections.emptyList(), ServiceStates.NONE, 5_000L, "", "");
        assertTrue(UiFormat.gracefulActive(draining));
    }

    @Test
    public void nativeSnapshotFailureIsAnErrorNotADegradedRouter() {
        RouterSnapshot failed =
                RouterStatusParser.parseStatus("status=Error\nstatusCode=-1\nerror=snapshot-failed",
                        DaemonWrapper.State.startedOkay, ServiceStates.NONE, 0L, "", "ok");
        assertEquals(UiFormat.RouterState.ERROR, UiFormat.stateOf(failed));
        assertFalse(UiFormat.isConnected(failed));
        assertTrue("Stop must stay available when only the status snapshot failed",
                UiFormat.canStop(failed));
    }

    @Test
    public void zeroInitialisedCountersAreNotLiveMetrics() {
        RouterSnapshot startedWithoutStatus =
                snapshotWithStatus(DaemonWrapper.State.startedOkay, false, 0, "", "");
        RouterSnapshot startedWithStatus =
                snapshotWithStatus(DaemonWrapper.State.startedOkay, true, 0, "OK", "");
        RouterSnapshot unknownStatus =
                snapshotWithStatus(DaemonWrapper.State.startedOkay, true, 0, "Unknown", "");

        assertFalse(startedWithoutStatus.hasLiveMetrics());
        assertFalse(UiFormat.hasLiveMetrics(startedWithoutStatus));
        assertFalse(unknownStatus.hasLiveMetrics());
        assertTrue(startedWithStatus.hasLiveMetrics());
        assertTrue(UiFormat.hasLiveMetrics(startedWithStatus));
        assertTrue(UiFormat.isStarted(startedWithoutStatus));
    }

    @Test
    public void detectsIpv6LiteralsWithoutApplyingThemToIpv4() {
        assertFalse(UiFormat.isIpv6(null));
        assertFalse(UiFormat.isIpv6(""));
        assertFalse(UiFormat.isIpv6("192.0.2.1"));
        assertTrue(UiFormat.isIpv6("2001:db8::1"));
        assertTrue(UiFormat.isIpv6("[2001:db8::1]"));
    }

    @Test
    public void everyRouterStateHasADistinctLabel() {
        int connected = UiFormat.stateLabelRes(UiFormat.RouterState.CONNECTED);
        assertNotEquals(connected, UiFormat.stateLabelRes(UiFormat.RouterState.DEGRADED));
        assertNotEquals(connected, UiFormat.stateLabelRes(UiFormat.RouterState.STOPPED));
        assertNotEquals(connected, UiFormat.stateLabelRes(UiFormat.RouterState.ERROR));
    }
}

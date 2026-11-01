/*
 * See the LICENSE file at the top of the project tree for copyright and
 * license details.
 *
 * openi2pd: BootResumePolicyTest.
 *
 * Pure-JVM regression for the boot/package-replace resume rule: a
 * broadcast resumes a persisted run intent and never starts a router
 * that the operator stopped or a drain that already finished. The
 * decision is a Context-free predicate, so no service, receiver
 * instance, BroadcastReceiver registration or device is required.
 *
 */

package org.openi2pd.router;

import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import android.content.Intent;

import org.junit.Test;
import org.openi2pd.router.receivers.BootUpReceiver;

/**
 * JVM checks for {@link BootUpReceiver#mayResume(String, boolean,
 * boolean)}.
 *
 * <p>The receiver used to send an explicit start command, which
 * persisted a running intent before the service reconciled it: a router
 * stopped by the operator, or a graceful drain whose deadline had
 * already passed, could be resurrected by {@code BOOT_COMPLETED} or a
 * package update. This file pins the corrected rule; the service-side
 * reconciliation it relies on is covered by {@link RouterRunStateTest}
 * and the null-action sticky-restore mapping by {@link
 * RouterCommandTest}.</p>
 */
public final class BootResumePolicyTest {
    @Test
    public void bootResumesOnlyARequestedRunWithTheAutostartMarker() {
        assertTrue(BootUpReceiver.mayResume(Intent.ACTION_BOOT_COMPLETED, true, true));
        assertFalse("boot autostart disabled",
                BootUpReceiver.mayResume(Intent.ACTION_BOOT_COMPLETED, false, true));
        assertFalse("nothing was requested",
                BootUpReceiver.mayResume(Intent.ACTION_BOOT_COMPLETED, true, false));
        assertFalse(BootUpReceiver.mayResume(Intent.ACTION_BOOT_COMPLETED, false, false));
    }

    @Test
    public void aStoppedRouterIsNeverResumedByABroadcast() {
        for (boolean marker : new boolean[] {true, false}) {
            assertFalse("an explicit stop must survive boot",
                    BootUpReceiver.mayResume(Intent.ACTION_BOOT_COMPLETED, marker, false));
            assertFalse("an explicit stop must survive a package update",
                    BootUpReceiver.mayResume(Intent.ACTION_MY_PACKAGE_REPLACED, marker, false));
        }
    }

    @Test
    public void packageReplacementResumesARequestedRunWithoutTheBootMarker() {
        assertTrue(BootUpReceiver.mayResume(Intent.ACTION_MY_PACKAGE_REPLACED, false, true));
        assertTrue(BootUpReceiver.mayResume(Intent.ACTION_MY_PACKAGE_REPLACED, true, true));
        assertFalse(BootUpReceiver.mayResume(Intent.ACTION_MY_PACKAGE_REPLACED, true, false));
    }

    @Test
    public void unrelatedActionsAndActionsWithoutAValueAreIgnored() {
        for (String action : new String[] {null, "", Intent.ACTION_SCREEN_ON,
                     Intent.ACTION_MY_PACKAGE_SUSPENDED, Intent.ACTION_TIME_CHANGED}) {
            assertFalse(String.valueOf(action),
                    BootUpReceiver.mayResume(action, true, true));
            assertFalse(String.valueOf(action),
                    BootUpReceiver.mayResume(action, true, false));
        }
    }
}

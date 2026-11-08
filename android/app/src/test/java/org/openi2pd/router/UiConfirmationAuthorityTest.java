/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 * openi2pd: one-shot Activity confirmation regressions, manual JVM CI.
 */
package org.openi2pd.router;

import static org.junit.Assert.*;
import org.junit.Test;

public final class UiConfirmationAuthorityTest {
    @Test
    public void stoppedOwnerRetainsNoWindow() {
        var authority = new UiConfirmationAuthority();
        assertNull(authority.register(() -> fail("stopped window retained")));
        assertFalse(authority.consume(null, true));
    }

    @Test
    public void currentTicketConsumesOnlyOnceAndRechecksAdmission() {
        var authority = new UiConfirmationAuthority();
        authority.start();
        int[] closed = {0};
        var ticket = authority.register(() -> closed[0]++);
        assertFalse(authority.consume(ticket, false));
        assertEquals(0, closed[0]);
        assertTrue(authority.consume(ticket, true));
        assertFalse(authority.consume(ticket, true));
        authority.stop();
        assertEquals(1, closed[0]);
    }

    @Test
    public void replacementAndOldDismissalCannotReviveOldConsent() {
        var authority = new UiConfirmationAuthority();
        authority.start();
        int[] closed = {0};
        var old = authority.register(() -> closed[0]++);
        var current = authority.register(() -> closed[0]++);
        assertEquals(1, closed[0]);
        assertFalse(authority.consume(old, true));
        authority.dismissed(old);
        assertTrue(authority.consume(current, true));
        assertEquals(2, closed[0]);
    }

    @Test
    public void naturalDismissalRevokesWithoutDismissingAgain() {
        var authority = new UiConfirmationAuthority();
        authority.start();
        var ticket = authority.register(() -> fail("already dismissed"));
        authority.dismissed(ticket);
        authority.dismissed(ticket);
        assertFalse(authority.consume(ticket, true));
        authority.stop();
    }

    @Test
    public void foregroundRestartCannotReactivateOldCallback() {
        var authority = new UiConfirmationAuthority();
        authority.start();
        int[] closed = {0};
        var old = authority.register(() -> closed[0]++);
        authority.stop();
        authority.stop();
        authority.start();
        assertFalse(authority.consume(old, true));
        assertEquals(1, closed[0]);
        assertTrue(authority.consume(authority.register(() -> {}), true));
    }

    @Test
    public void sessionReplacementRevokesBeforeCleanup() {
        var authority = new UiConfirmationAuthority();
        authority.start();
        UiConfirmationAuthority.Ticket[] ticket = {null};
        ticket[0] = authority.register(() -> assertFalse(authority.consume(ticket[0], true)));
        authority.sessionChanged();
        assertFalse(authority.consume(ticket[0], true));
        assertTrue(authority.consume(authority.register(() -> {}), true));
    }

    @Test
    public void foreignOwnerCannotConsumeOrDismissTicket() {
        var first = new UiConfirmationAuthority();
        var second = new UiConfirmationAuthority();
        first.start();
        second.start();
        int[] closed = {0};
        var ticket = first.register(() -> closed[0]++);
        assertFalse(second.consume(ticket, true));
        second.dismissed(ticket);
        assertTrue(first.consume(ticket, true));
        assertEquals(1, closed[0]);
    }

    @Test
    public void cleanupFailureCannotRestoreAuthority() {
        var authority = new UiConfirmationAuthority();
        authority.start();
        var ticket = authority.register(() -> { throw new IllegalStateException(); });
        authority.stop();
        assertFalse(authority.consume(ticket, true));
        authority.start();
        assertTrue(authority.consume(authority.register(() -> {}), true));
    }

    @Test
    public void cleanupThatStopsOwnerCannotAdmitReplacementOrAction() {
        var authority = new UiConfirmationAuthority();
        authority.start();
        authority.register(authority::stop);
        assertNull(authority.register(() -> {}));
        authority.start();
        var ticket = authority.register(authority::stop);
        assertFalse(authority.consume(ticket, true));
    }

    @Test
    public void cleanupCannotRetainReentrantSuccessor() {
        var authority = new UiConfirmationAuthority();
        authority.start();
        UiConfirmationAuthority.Ticket[] successor = {null};
        var old = authority.register(() -> successor[0] = authority.register(() -> {}));
        assertTrue(authority.consume(old, true));
        authority.dismissed(old);
        assertNull(successor[0]);
        assertTrue(authority.consume(authority.register(() -> {}), true));
    }

    @Test
    public void startRetirementCannotPublishHiddenSuccessor() {
        var authority = new UiConfirmationAuthority();
        authority.start();
        authority.register(() -> {
            authority.start();
            assertNull(authority.register(() -> fail("hidden successor retained")));
        });
        authority.start();
        assertTrue(authority.consume(authority.register(() -> {}), true));
    }

    @Test
    public void stopDuringStartRetirementWinsOverOuterStart() {
        var authority = new UiConfirmationAuthority();
        authority.start();
        authority.register(authority::stop);
        authority.start();
        assertNull(authority.register(() -> fail("stopped owner reopened")));
    }

    @Test
    public void stopAndSessionRetirementRejectReentrantOpening() {
        for (boolean stop : new boolean[] {false, true}) {
            var authority = new UiConfirmationAuthority();
            authority.start();
            authority.register(() -> {
                authority.start();
                assertNull(authority.register(() -> fail("retirement reopened window")));
            });
            if (stop)
                authority.stop();
            else
                authority.sessionChanged();
            if (stop)
                assertNull(authority.register(() -> fail("stopped window retained")));
            else
                assertTrue(authority.consume(authority.register(() -> {}), true));
        }
    }
}

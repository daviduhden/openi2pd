/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 * openi2pd: dialog view-authority regressions, manual JVM CI.
 */
package org.openi2pd.router;

import static org.junit.Assert.*;
import org.junit.Test;

public final class UiDialogEpochTest {
    @Test
    public void oldDialogCannotSubmitAfterViewRecreation() {
        var dialogs = new UiDialogEpoch();
        assertNull(dialogs.token());
        dialogs.openView();
        var old = dialogs.token();
        int[] submitted = {0};
        Runnable click = () -> {
            if (dialogs.maySubmit(old, true, false))
                submitted[0]++;
        };
        dialogs.retireView();
        click.run();
        dialogs.openView();
        click.run();
        assertEquals(0, submitted[0]);
        assertFalse(dialogs.isCurrent(old));
        assertTrue(dialogs.maySubmit(dialogs.token(), true, false));
    }

    @Test
    public void clickAdmissionRechecksBusyAndViewAvailability() {
        var dialogs = new UiDialogEpoch();
        dialogs.openView();
        var token = dialogs.token();
        assertTrue(dialogs.maySubmit(token, true, false));
        assertFalse(dialogs.maySubmit(token, true, true));
        assertFalse(dialogs.maySubmit(token, false, false));
        assertFalse(dialogs.maySubmit(null, true, false));
        assertTrue(dialogs.maySubmit(token, true, false));
    }

    @Test
    public void retirementRevokesBeforeDismissalAndOnlyDismissesOnce() {
        var dialogs = new UiDialogEpoch();
        dialogs.openView();
        var token = dialogs.token();
        int[] dismissals = {0};
        assertNotNull(dialogs.replace(token, () -> {
            dismissals[0]++;
            assertFalse(dialogs.isCurrent(token));
            assertFalse(dialogs.maySubmit(token, true, false));
        }));
        dialogs.retireView();
        dialogs.retireView();
        assertEquals(1, dismissals[0]);
    }

    @Test
    public void oldDismissCallbackCannotForgetReplacementDialog() {
        var dialogs = new UiDialogEpoch();
        dialogs.openView();
        var token = dialogs.token();
        int[] first = {0}, second = {0};
        var old = dialogs.replace(token, () -> first[0]++);
        var replacement = dialogs.replace(token, () -> second[0]++);
        assertNotNull(replacement);
        assertEquals(1, first[0]);
        dialogs.dismissed(old);
        dialogs.retireView();
        assertEquals(1, second[0]);
        dialogs.dismissed(replacement);
    }

    @Test
    public void staleAndForeignViewsCannotReplaceCurrentDialog() {
        var dialogs = new UiDialogEpoch();
        var foreign = new UiDialogEpoch();
        dialogs.openView();
        foreign.openView();
        var old = dialogs.token();
        dialogs.openView();
        int[] current = {0};
        var registration = dialogs.replace(dialogs.token(), () -> current[0]++);
        assertNull(dialogs.replace(old, () -> fail("stale dialog must never be retained")));
        assertNull(dialogs.replace(foreign.token(), () -> fail("foreign dialog retained")));
        assertNull(dialogs.replace(dialogs.token(), null));
        assertEquals(0, current[0]);
        dialogs.dismissed(registration);
        dialogs.retireView();
        assertEquals(0, current[0]);
    }

    @Test
    public void nestedChoiceKeepsParentAndRetiresChildrenFirst() {
        var dialogs = new UiDialogEpoch();
        dialogs.openView();
        var token = dialogs.token();
        var closed = new java.util.ArrayList<Integer>();
        var parent = dialogs.replace(token, () -> closed.add(0));
        var child = dialogs.nest(token, () -> closed.add(1));
        assertNotNull(parent);
        assertNotNull(child);
        assertTrue(closed.isEmpty());
        dialogs.retireView();
        assertEquals(java.util.List.of(1, 0), closed);
    }

    @Test
    public void nestedAdmissionIsBoundedAndExactDismissalReleasesCapacity() {
        var dialogs = new UiDialogEpoch();
        dialogs.openView();
        var token = dialogs.token();
        var registrations = new java.util.ArrayList<UiDialogEpoch.Registration>();
        int[] closed = {0};
        for (int i = 0; i < UiDialogEpoch.MAX_DIALOGS; ++i)
            registrations.add(dialogs.nest(token, () -> closed[0]++));
        for (var registration : registrations)
            assertNotNull(registration);
        assertNull(dialogs.nest(token, () -> fail("over-cap dialog retained")));
        dialogs.dismissed(registrations.get(0));
        assertNotNull(dialogs.nest(token, () -> closed[0]++));
        dialogs.dismissed(registrations.get(0));
        assertNull(dialogs.nest(token, () -> fail("double-dismiss released capacity")));
        dialogs.retireView();
        assertEquals(UiDialogEpoch.MAX_DIALOGS, closed[0]);
    }

    @Test
    public void replacementRetiresAllNestedDialogsAndCleanupFailureDoesNotLeakOthers() {
        var dialogs = new UiDialogEpoch();
        dialogs.openView();
        var token = dialogs.token();
        int[] closed = {0};
        dialogs.replace(token, () -> closed[0]++);
        dialogs.nest(token, () -> { throw new IllegalStateException(); });
        dialogs.nest(token, () -> closed[0]++);
        dialogs.replace(token, () -> closed[0]++);
        assertEquals(2, closed[0]);
        assertTrue(dialogs.isCurrent(token));
        dialogs.retireView();
        assertEquals(3, closed[0]);
    }
}

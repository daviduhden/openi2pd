/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 * openi2pd: view-owned dialog lifetime authority.
 */
package org.openi2pd.router;

import java.util.ArrayList;
import java.util.List;

/**
 * Main-thread authority for one view's dialogs. A token belongs to the
 * view that opened it, so recreating a view cannot revive old click
 * callbacks. Replacement retains one dialog; explicit nesting admits
 * at most eight forms/choices. View retirement revokes all dialog
 * registrations before dismissing them. Callers also
 * check current view availability and mutation admission at click time.
 * No method performs background work or authorizes a daemon operation.
 */
final class UiDialogEpoch {
    static final int MAX_DIALOGS = 8;
    /** Opaque identity, never a reusable generation number. */
    static final class Token {
        private Token() {}
    }

    /** Exact dialog registration, independent of its view token. */
    static final class Registration {
        private final Runnable dismiss;

        private Registration(Runnable dismiss) {
            this.dismiss = dismiss;
        }
    }

    private Token current;
    private final ArrayList<Registration> visible = new ArrayList<>();

    /** Retire any old view before admitting dialogs for its successor. */
    void openView() {
        retireView();
        current = new Token();
    }

    Token token() {
        return current;
    }

    boolean isCurrent(Token token) {
        return token != null && token == current;
    }

    /** Recheck mutable admission when the user activates a dialog. */
    boolean maySubmit(Token token, boolean viewReady, boolean busy) {
        return isCurrent(token) && viewReady && !busy;
    }

    /**
     * Register one dialog before showing it. Stale callers cannot
     * replace the current dialog. The caller installs an on-dismiss
     * listener carrying the returned registration before showing it.
     */
    Registration replace(Token token, Runnable dismiss) {
        if (!isCurrent(token) || dismiss == null)
            return null;
        var previous = new ArrayList<>(visible);
        Registration next = new Registration(dismiss);
        visible.clear();
        visible.add(next);
        close(previous);
        return next;
    }

    /** Keep a parent form while displaying one bounded nested choice. */
    Registration nest(Token token, Runnable dismiss) {
        if (!isCurrent(token) || dismiss == null || visible.size() >= MAX_DIALOGS)
            return null;
        Registration next = new Registration(dismiss);
        visible.add(next);
        return next;
    }

    /** An old dialog's delayed dismissal cannot clear its successor. */
    void dismissed(Registration registration) {
        if (registration != null)
            visible.remove(registration);
    }

    /** Revoke click authority before invoking framework dismissal. */
    void retireView() {
        current = null;
        var previous = new ArrayList<>(visible);
        visible.clear();
        close(previous);
    }

    // Children close first. Every registration is already revoked, so
    // dismissal callbacks cannot remove a successor's registration.
    private static void close(List<Registration> previous) {
        for (int i = previous.size() - 1; i >= 0; --i)
            try {
                previous.get(i).dismiss.run();
            } catch (RuntimeException ignored) {
                // A failed window teardown must not retain other forms.
            }
    }
}

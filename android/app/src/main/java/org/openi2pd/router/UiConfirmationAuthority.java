/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 * openi2pd: foreground-owned, one-shot confirmation authority.
 */
package org.openi2pd.router;

/*
 * Main-thread ownership for one Activity confirmation. Foreground and
 * service sessions have opaque identities: restarting either cannot
 * reactivate callbacks retained by an older window. Replacement and
 * dismissal revoke exact tickets, and consumption revokes before the
 * action or framework dismissal can reenter this owner.
 *
 * <p>This class owns no router or Android object. Its sole retained
 * callback dismisses the current window; retirement releases it before
 * invoking framework cleanup. Cleanup cannot reopen or register another
 * window; a reentrant stop still retires the foreground interval.
 * Callers supply current action admission and must not treat this UI
 * authority as daemon authorization.</p>
 */
final class UiConfirmationAuthority {
    /* Opaque registration for one owner and session. */
    static final class Ticket {
        private final Object foreground;
        private final Object session;
        private Runnable dismiss;

        private Ticket(Object foreground, Object session, Runnable dismiss) {
            this.foreground = foreground;
            this.session = session;
            this.dismiss = dismiss;
        }
    }

    private Object foreground;
    private Object session = new Object();
    private Ticket current;
    private boolean retiring;

    /* Start a fresh foreground interval, retiring any prior window. */
    void start() {
        if (retiring)
            return;
        foreground = new Object();
        Ticket previous = current;
        current = null;
        dismiss(previous);
    }

    /* Revoke before dismissal; repeated cleanup is harmless. */
    void stop() {
        foreground = null;
        Ticket previous = current;
        current = null;
        dismiss(previous);
    }

    /* Reconnection or native restart invalidates pending consent. */
    void sessionChanged() {
        session = new Object();
        Ticket previous = current;
        current = null;
        dismiss(previous);
    }

    /*
     * Replace the foreground confirmation. A stopped owner retains no
     * callback; callers must not show a window when this returns null.
     */
    Ticket register(Runnable dismiss) {
        if (retiring || foreground == null || dismiss == null)
            return null;
        Object expectedForeground = foreground;
        Object expectedSession = session;
        Ticket previous = current;
        current = null;
        dismiss(previous);
        if (foreground != expectedForeground || session != expectedSession)
            return null;
        Ticket next = new Ticket(foreground, session, dismiss);
        current = next;
        return next;
    }

    /*
     * Claim one action and close its window before returning success.
     * Denied admission leaves the confirmation usable until its owner
     * explicitly dismisses or retires it.
     */
    boolean consume(Ticket ticket, boolean admitted) {
        if (retiring || !admitted || ticket == null || ticket != current
                || ticket.foreground != foreground || ticket.session != session)
            return false;
        current = null;
        dismiss(ticket);
        return ticket.foreground == foreground && ticket.session == session;
    }

    /* A delayed old-window dismissal cannot revoke its replacement. */
    void dismissed(Ticket ticket) {
        if (ticket == null || ticket != current)
            return;
        current = null;
        ticket.dismiss = null;
    }

    private void dismiss(Ticket ticket) {
        if (ticket == null)
            return;
        Runnable cleanup = ticket.dismiss;
        ticket.dismiss = null;
        if (cleanup != null) {
            boolean previousRetirement = retiring;
            retiring = true;
            try {
                cleanup.run();
            } catch (RuntimeException ignored) {
                // Failed cleanup must not restore click authority.
            } finally {
                retiring = previousRetirement;
            }
        }
    }
}

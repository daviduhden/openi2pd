/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: deferred restart admission.
 *
 */

package org.openi2pd.router;

import java.util.Objects;
import java.util.concurrent.Executor;
import java.util.concurrent.RejectedExecutionException;
import java.util.function.BooleanSupplier;
import java.util.function.Consumer;
import java.util.function.Predicate;
import java.util.function.Supplier;

/**
 * One pending restart, fenced against cancellation and owner retirement.
 * Readiness and admission share the owner's state monitor. Admission
 * makes only a short state transition and returns the launch action;
 * launching and completion run outside that monitor.
 */
final class DeferredRestart {
    private final Object monitor;
    private final BooleanSupplier ready;
    private final Supplier<BooleanSupplier> admit;
    private Ticket pending;

    private static final class Ticket {
        final Executor executor;
        final Predicate<Runnable> authority;
        final Consumer<Boolean> completion;
        boolean scheduled, completed;

        Ticket(Executor executor, Predicate<Runnable> authority, Consumer<Boolean> completion) {
            this.executor = Objects.requireNonNull(executor);
            this.authority = Objects.requireNonNull(authority);
            this.completion = Objects.requireNonNull(completion);
        }
    }

    DeferredRestart(Object monitor, BooleanSupplier ready, Supplier<BooleanSupplier> admit) {
        this.monitor = Objects.requireNonNull(monitor);
        this.ready = Objects.requireNonNull(ready);
        this.admit = Objects.requireNonNull(admit);
    }

    /**
     * Replace any pending request. Authority synchronously gates the
     * short admission action against owner retirement; it must not
     * retain the action. Each completion must be nonblocking.
     */
    void request(Executor executor, Predicate<Runnable> authority, Consumer<Boolean> completion) {
        Ticket ticket = new Ticket(executor, authority, completion);
        Ticket previous;
        synchronized (monitor) {
            previous = pending;
            pending = ticket;
        }
        complete(previous, false);
        ready();
    }

    /** Invalidate even a continuation already queued on the executor. */
    void cancel() {
        Ticket previous;
        synchronized (monitor) {
            previous = pending;
            pending = null;
        }
        complete(previous, false);
    }

    /** Signal that a start generation has finished all of its cleanup. */
    void ready() {
        Ticket ticket;
        synchronized (monitor) {
            ticket = pending;
            if (ticket == null || ticket.scheduled || !ready.getAsBoolean())
                return;
            ticket.scheduled = true;
        }
        try {
            ticket.executor.execute(() -> run(ticket));
        } catch (RejectedExecutionException rejected) {
            synchronized (monitor) {
                if (pending == ticket)
                    pending = null;
            }
            complete(ticket, false);
        }
    }

    private void run(Ticket ticket) {
        boolean accepted = false;
        try {
            BooleanSupplier[] launch = {null};
            if (!ticket.authority.test(() -> {
                    synchronized (monitor) {
                        if (pending != ticket)
                            return;
                        launch[0] = admit.get();
                        pending = null;
                    }
                }))
                return;
            if (launch[0] != null)
                accepted = launch[0].getAsBoolean();
        } finally {
            synchronized (monitor) {
                if (pending == ticket)
                    pending = null;
            }
            complete(ticket, accepted);
        }
    }

    private void complete(Ticket ticket, boolean accepted) {
        if (ticket == null)
            return;
        synchronized (monitor) {
            if (ticket.completed)
                return;
            ticket.completed = true;
        }
        ticket.completion.accept(accepted);
    }
}

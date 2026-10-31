/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: Android service-owner epoch.
 *
 */

package org.openi2pd.router;

import java.util.HashSet;
import java.util.Set;
import java.util.concurrent.Executor;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.RejectedExecutionException;
import java.util.function.Consumer;

/**
 * Fences callbacks and worker submissions retained by an obsolete
 * RouterService instance after a destroy/recreate cycle. The
 * owner-scoped executor serializes Java service work through one
 * process-local FIFO; the native daemon-call guard remains responsible
 * for JNI serialization.
 */
final class ServiceOwnerEpoch {
    private static final ExecutorService PROCESS_WORKER =
            Executors.newSingleThreadExecutor(runnable -> {
                Thread thread = new Thread(runnable, "i2pd-router");
                thread.setDaemon(true);
                return thread;
            });
    private static long nextEpoch;
    private static long currentEpoch;
    private static final Set<Long> drainingEpochs = new HashSet<>();

    private ServiceOwnerEpoch() {
    }

    static ExecutorService processWorker() {
        return PROCESS_WORKER;
    }

    static synchronized long acquire() {
        currentEpoch = ++nextEpoch;
        return currentEpoch;
    }

    static synchronized boolean isCurrent(long epoch) {
        return epoch != 0L && currentEpoch == epoch;
    }

    /**
     * Run an owner callback only if its epoch is current. The check and
     * action share the epoch monitor with acquisition and retirement,
     * so the callback must remain short and must not enter JNI or wait
     * for worker completion.
     *
     * @return {@code true} if the callback ran
     */
    static synchronized boolean runIfCurrent(long epoch, Runnable action) {
        if (epoch == 0L || currentEpoch != epoch) {
            return false;
        }
        action.run();
        return true;
    }

    /**
     * Create an owner-scoped submission view over the process FIFO.
     * Submission and retirement share this monitor, so accepted work is
     * queued before the owner's final stop barrier; execution is then
     * allowed only while that owner is active or draining ahead of its
     * barrier.
     */
    static Executor ownerExecutor(long epoch) {
        return command -> {
            synchronized (ServiceOwnerEpoch.class) {
                if (epoch == 0L || currentEpoch != epoch) {
                    throw new RejectedExecutionException("service owner has retired");
                }
                PROCESS_WORKER.execute(() -> {
                    if (isRunnable(epoch)) {
                        command.run();
                    }
                });
            }
        };
    }

    /**
     * Retire an owner and append its mandatory stop atomically with
     * respect to owner submissions/acquisition. The bookkeeping
     * callback must not block or call JNI; the stop itself runs later,
     * outside this monitor.
     */
    static synchronized void retireAndQueueStop(
            long epoch, Consumer<Boolean> onRetired, Runnable stop) {
        boolean wasCurrent = epoch != 0L && currentEpoch == epoch;
        if (wasCurrent) {
            currentEpoch = ++nextEpoch;
            drainingEpochs.add(epoch);
        }
        onRetired.accept(wasCurrent);
        if (!wasCurrent) {
            return;
        }
        PROCESS_WORKER.execute(() -> {
            try {
                stop.run();
            } finally {
                synchronized (ServiceOwnerEpoch.class) {
                    drainingEpochs.remove(epoch);
                }
            }
        });
    }

    private static synchronized boolean isRunnable(long epoch) {
        return currentEpoch == epoch || drainingEpochs.contains(epoch);
    }
}

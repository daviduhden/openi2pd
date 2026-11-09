/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: bounded service SAF task admission.
 *
 */

package org.openi2pd.router;

import java.util.concurrent.ArrayBlockingQueue;
import java.util.concurrent.ThreadPoolExecutor;
import java.util.concurrent.TimeUnit;

/*
 * Creates the service-owned SAF lane. One provider operation runs at a
 * time, with at most sixteen pending tasks retaining grants, status
 * consumers or import/export state. A small burst fits without letting
 * an unresponsive provider accumulate requests indefinitely.
 */
final class TorrentSafTaskExecutor {
    static final int MAX_QUEUED_TASKS = 16;

    private TorrentSafTaskExecutor() {
    }

    /*
     * Return a new owner-scoped executor. Saturation and shutdown throw
     * {@link java.util.concurrent.RejectedExecutionException}; callers
     * publish their typed fallback through the owner's epoch fence.
     * Provider work never falls back to running on the submitting UI
     * thread. Destruction uses shutdownNow to discard queued work and
     * interrupt the active task; providers may ignore interruption.
     */
    static ThreadPoolExecutor create() {
        return new ThreadPoolExecutor(1, 1, 0L, TimeUnit.SECONDS,
                new ArrayBlockingQueue<>(MAX_QUEUED_TASKS), runnable -> {
                    Thread thread = new Thread(runnable, "openi2pd-saf");
                    thread.setDaemon(true);
                    return thread;
                }, new ThreadPoolExecutor.AbortPolicy());
    }
}

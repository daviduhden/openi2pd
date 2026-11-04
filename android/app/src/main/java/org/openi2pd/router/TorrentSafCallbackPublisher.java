/*
 * See the LICENSE file at the top of the project tree for copyright and
 * license details.
 *
 * openi2pd: torrent SAF callback publication.
 *
 */

package org.openi2pd.router;

import java.util.Objects;
import java.util.concurrent.Executor;
import java.util.concurrent.RejectedExecutionException;

/**
 * Queues a SAF callback and fences its delivery against service-owner
 * retirement. The callback runs on the supplied executor only while its
 * captured owner epoch remains current.
 */
final class TorrentSafCallbackPublisher {
    private TorrentSafCallbackPublisher() {
    }

    /**
     * Queue a callback whose owner is checked again at delivery time.
     *
     * <p>The callback executes under the service-owner epoch monitor,
     * so it must remain short and must not enter JNI or wait for worker
     * completion.</p>
     *
     * @param ownerEpoch service generation that accepted the operation
     * @param executor executor for callback delivery
     * @param callback callback to run if the owner is still current
     * @return {@code true} if queued, or {@code false} if rejected
     */
    static boolean post(long ownerEpoch, Executor executor, Runnable callback) {
        Objects.requireNonNull(executor);
        Objects.requireNonNull(callback);
        try {
            executor.execute(() -> ServiceOwnerEpoch.runIfCurrent(ownerEpoch, callback));
            return true;
        } catch (RejectedExecutionException ignored) {
            return false;
        }
    }
}

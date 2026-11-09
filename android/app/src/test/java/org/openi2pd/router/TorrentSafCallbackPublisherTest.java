/*
 * See the LICENSE file at the top of the project tree for copyright and
 * license details.
 *
 * openi2pd: torrent SAF callback publication regression.
 *
 */

package org.openi2pd.router;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import java.util.ArrayDeque;
import java.util.Queue;
import java.util.concurrent.Executor;

import org.junit.Test;

/* Pure-JVM checks for SAF callbacks crossing service destruction. */
public final class TorrentSafCallbackPublisherTest {
    @Test
    public void queuedProgressAndCompletionAreFencedAfterRetirement() {
        long owner = ServiceOwnerEpoch.acquire();
        Queue<Runnable> queued = new ArrayDeque<>();
        Executor executor = queued::add;
        int[] publications = {0};

        assertTrue(TorrentSafCallbackPublisher.post(owner, executor,
                () -> publications[0]++));
        assertTrue(TorrentSafCallbackPublisher.post(owner, executor,
                () -> publications[0]++));
        assertEquals(2, queued.size());

        ServiceOwnerEpoch.retireAndQueueStop(owner, ignored -> {}, () -> {});
        queued.remove().run();
        queued.remove().run();

        assertEquals(0, publications[0]);
    }

    @Test
    public void workerCallbackQueuedAfterRetirementIsFencedAtDelivery() {
        long owner = ServiceOwnerEpoch.acquire();
        Queue<Runnable> queued = new ArrayDeque<>();
        int[] publications = {0};

        ServiceOwnerEpoch.retireAndQueueStop(owner, ignored -> {}, () -> {});
        assertTrue(TorrentSafCallbackPublisher.post(owner, queued::add,
                () -> publications[0]++));
        queued.remove().run();

        assertEquals(0, publications[0]);
        assertFalse(ServiceOwnerEpoch.isCurrent(owner));
    }

    @Test
    public void currentOwnerReceivesCallback() {
        long owner = ServiceOwnerEpoch.acquire();
        int[] publications = {0};

        assertTrue(TorrentSafCallbackPublisher.post(owner, Runnable::run,
                () -> publications[0]++));

        assertEquals(1, publications[0]);
    }
}

/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: service owner epoch regression.
 *
 */

package org.openi2pd.router;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;
import static org.junit.Assert.fail;

import org.junit.Test;

import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.Executor;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.RejectedExecutionException;
import java.util.concurrent.TimeUnit;

/** Pure-JVM checks for stale owner callback fencing. */
public final class ServiceOwnerEpochTest {
    @Test
    public void newerOwnerFencesCallbacksFromPriorService() {
        long oldOwner = ServiceOwnerEpoch.acquire();
        assertTrue(ServiceOwnerEpoch.isCurrent(oldOwner));

        long newOwner = ServiceOwnerEpoch.acquire();
        assertFalse(ServiceOwnerEpoch.isCurrent(oldOwner));
        assertTrue(ServiceOwnerEpoch.isCurrent(newOwner));

        int[] delivered = {0};
        assertFalse(ServiceOwnerEpoch.runIfCurrent(oldOwner, () -> delivered[0]++));
        assertTrue(ServiceOwnerEpoch.runIfCurrent(newOwner, () -> delivered[0]++));
        assertEquals(1, delivered[0]);
        assertTrue(ServiceOwnerEpoch.isCurrent(newOwner));
    }

    @Test
    public void retiringCurrentOwnerFencesItsQueuedCallbacks() throws Exception {
        long owner = ServiceOwnerEpoch.acquire();
        assertTrue(ServiceOwnerEpoch.isCurrent(owner));

        ServiceOwnerEpoch.retireAndQueueStop(owner, wasCurrent -> assertTrue(wasCurrent), () -> {});
        assertFalse(ServiceOwnerEpoch.isCurrent(owner));
        assertFalse(ServiceOwnerEpoch.runIfCurrent(
                owner, () -> { throw new AssertionError("retired owner callback must not run"); }));
        ServiceOwnerEpoch.processWorker().submit(() -> {}).get(5, TimeUnit.SECONDS);
    }

    @Test
    public void staleRetirementCannotStopOrInvalidateNewOwner() throws Exception {
        long oldOwner = ServiceOwnerEpoch.acquire();
        long newOwner = ServiceOwnerEpoch.acquire();
        int[] stops = {0};

        ServiceOwnerEpoch.retireAndQueueStop(
                oldOwner, wasCurrent -> assertFalse(wasCurrent), () -> stops[0]++);
        ServiceOwnerEpoch.processWorker().submit(() -> {}).get(5, TimeUnit.SECONDS);

        assertTrue(ServiceOwnerEpoch.isCurrent(newOwner));
        assertEquals(0, stops[0]);
    }

    @Test
    public void ownerExecutorFencesStaleSubmissionsAndDrainsAcceptedWorkBeforeStop()
            throws Exception {
        long oldOwner = ServiceOwnerEpoch.acquire();
        Executor oldExecutor = ServiceOwnerEpoch.ownerExecutor(oldOwner);
        ExecutorService worker = ServiceOwnerEpoch.processWorker();
        List<String> order = Collections.synchronizedList(new ArrayList<>());
        CountDownLatch blockerStarted = new CountDownLatch(1);
        CountDownLatch releaseBlocker = new CountDownLatch(1);
        var blocker = worker.submit(() -> {
            blockerStarted.countDown();
            try {
                if (!releaseBlocker.await(5, TimeUnit.SECONDS)) {
                    throw new AssertionError("worker blocker timed out");
                }
            } catch (InterruptedException error) {
                Thread.currentThread().interrupt();
                throw new AssertionError("worker blocker interrupted", error);
            }
        });
        try {
            assertTrue(blockerStarted.await(5, TimeUnit.SECONDS));

            oldExecutor.execute(() -> order.add("old-work"));
            ServiceOwnerEpoch.retireAndQueueStop(
                    oldOwner, wasCurrent -> assertTrue(wasCurrent), () -> order.add("old-stop"));
            long newOwner = ServiceOwnerEpoch.acquire();
            Executor newExecutor = ServiceOwnerEpoch.ownerExecutor(newOwner);
            try {
                oldExecutor.execute(() -> order.add("stale-work"));
                fail("retired owner submissions must be rejected");
            } catch (RejectedExecutionException expected) {
                // The old service cannot append work beyond its stop
                // barrier.
            }
            newExecutor.execute(() -> order.add("new-start"));

            releaseBlocker.countDown();
            blocker.get(5, TimeUnit.SECONDS);
            worker.submit(() -> {}).get(5, TimeUnit.SECONDS);
            assertEquals(List.of("old-work", "old-stop", "new-start"), order);
        } finally {
            releaseBlocker.countDown();
        }
    }
}

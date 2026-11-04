/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: SAF task admission regression.
 *
 */

package org.openi2pd.router;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;
import static org.junit.Assert.fail;

import java.util.concurrent.CountDownLatch;
import java.util.concurrent.RejectedExecutionException;
import java.util.concurrent.ThreadPoolExecutor;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicBoolean;

import org.junit.Test;

/** Pure-JVM checks; no provider, Android service or native calls. */
public final class TorrentSafTaskExecutorTest {
    @Test
    public void blockedProviderBoundsQueueAndRejectsWithoutCallerExecution() throws Exception {
        ThreadPoolExecutor executor = TorrentSafTaskExecutor.create();
        CountDownLatch started = new CountDownLatch(1);
        CountDownLatch release = new CountDownLatch(1);
        CountDownLatch drained = new CountDownLatch(TorrentSafTaskExecutor.MAX_QUEUED_TASKS);
        AtomicBoolean rejectedTaskRan = new AtomicBoolean();
        var blocker = executor.submit(() -> {
            started.countDown();
            assertTrue(release.await(5, TimeUnit.SECONDS));
            return null;
        });
        try {
            assertTrue(started.await(5, TimeUnit.SECONDS));
            for (int i = 0; i < TorrentSafTaskExecutor.MAX_QUEUED_TASKS; ++i)
                executor.execute(drained::countDown);
            assertEquals(TorrentSafTaskExecutor.MAX_QUEUED_TASKS, executor.getQueue().size());
            assertEquals(1, executor.getPoolSize());
            try {
                executor.execute(() -> rejectedTaskRan.set(true));
                fail("a full SAF queue must reject admission");
            } catch (RejectedExecutionException expected) {
                assertFalse(rejectedTaskRan.get());
            }
            release.countDown();
            blocker.get(5, TimeUnit.SECONDS);
            assertTrue(drained.await(5, TimeUnit.SECONDS));
            assertEquals("openi2pd-saf", executor.submit(
                    () -> Thread.currentThread().getName()).get(5, TimeUnit.SECONDS));
            assertFalse(rejectedTaskRan.get());
        } finally {
            release.countDown();
            executor.shutdownNow();
            assertTrue(executor.awaitTermination(5, TimeUnit.SECONDS));
        }
    }

    @Test
    public void retirementDiscardsQueuedTasksAndRejectsNewAdmission() throws Exception {
        ThreadPoolExecutor executor = TorrentSafTaskExecutor.create();
        CountDownLatch started = new CountDownLatch(1);
        CountDownLatch release = new CountDownLatch(1);
        AtomicBoolean queuedTaskRan = new AtomicBoolean();
        executor.execute(() -> {
            started.countDown();
            try {
                release.await();
            } catch (InterruptedException expected) {
                Thread.currentThread().interrupt();
            }
        });
        try {
            assertTrue(started.await(5, TimeUnit.SECONDS));
            executor.execute(() -> queuedTaskRan.set(true));
            assertEquals(1, executor.shutdownNow().size());
            try {
                executor.execute(() -> queuedTaskRan.set(true));
                fail("a retired SAF lane must reject admission");
            } catch (RejectedExecutionException expected) {
                assertFalse(queuedTaskRan.get());
            }
            assertTrue(executor.awaitTermination(5, TimeUnit.SECONDS));
            assertFalse(queuedTaskRan.get());
        } finally {
            release.countDown();
            executor.shutdownNow();
        }
    }
}

/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: deferred restart regression.
 *
 */

package org.openi2pd.router;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.List;
import java.util.Queue;
import java.util.concurrent.RejectedExecutionException;
import java.util.function.Predicate;

import org.junit.Test;

/* Deterministic restart ordering without Android, JNI or sleeping. */
public final class DeferredRestartTest {
    private static final class Fixture {
        final Object monitor = new Object();
        final Queue<Runnable> queued = new ArrayDeque<>();
        final List<Boolean> completions = new ArrayList<>();
        boolean idle, current = true, launchResult = true;
        int admissions, launches;
        final DeferredRestart restart = new DeferredRestart(monitor, () -> idle, () -> {
            assertTrue(Thread.holdsLock(monitor));
            admissions++;
            idle = false;
            return () -> {
                assertFalse(Thread.holdsLock(monitor));
                launches++;
                return launchResult;
            };
        });
        final Predicate<Runnable> authority = admission -> {
            assertFalse(Thread.holdsLock(monitor));
            if (!current)
                return false;
            admission.run();
            return true;
        };

        void request() {
            restart.request(queued::add, authority, result -> {
                assertFalse(Thread.holdsLock(monitor));
                completions.add(result);
            });
        }
    }

    @Test
    public void busyStartDefersOneContinuationUntilCleanupFinishes() {
        Fixture fixture = new Fixture();
        fixture.request();
        assertTrue(fixture.queued.isEmpty());
        assertTrue(fixture.completions.isEmpty());

        fixture.idle = true;
        fixture.restart.ready();
        fixture.restart.ready();
        assertEquals(1, fixture.queued.size());
        fixture.queued.remove().run();
        assertEquals(1, fixture.admissions);
        assertEquals(1, fixture.launches);
        assertEquals(List.of(true), fixture.completions);
    }

    @Test
    public void stopInvalidatesAnAlreadyQueuedContinuationExactlyOnce() {
        Fixture fixture = new Fixture();
        fixture.idle = true;
        fixture.request();
        fixture.restart.cancel();
        fixture.restart.cancel();
        fixture.queued.remove().run();

        assertEquals(0, fixture.admissions);
        assertEquals(0, fixture.launches);
        assertEquals(List.of(false), fixture.completions);
    }

    @Test
    public void newRequestReplacesTheOldGeneration() {
        Fixture fixture = new Fixture();
        fixture.idle = true;
        fixture.request();
        fixture.request();
        fixture.queued.remove().run();
        fixture.queued.remove().run();

        assertEquals(1, fixture.admissions);
        assertEquals(1, fixture.launches);
        assertEquals(List.of(false, true), fixture.completions);
    }

    @Test
    public void retiredOwnerCannotAdmitQueuedRestart() {
        Fixture fixture = new Fixture();
        fixture.idle = true;
        fixture.request();
        fixture.current = false;
        fixture.queued.remove().run();

        assertEquals(0, fixture.admissions);
        assertEquals(0, fixture.launches);
        assertEquals(List.of(false), fixture.completions);
    }

    @Test
    public void authorityRunsOutsideMonitorAndCancellationBeforeAdmissionWins() {
        Fixture fixture = new Fixture();
        fixture.idle = true;
        fixture.restart.request(fixture.queued::add, admission -> {
            assertFalse(Thread.holdsLock(fixture.monitor));
            fixture.restart.cancel();
            admission.run();
            return true;
        }, fixture.completions::add);
        fixture.queued.remove().run();

        assertEquals(0, fixture.admissions);
        assertEquals(0, fixture.launches);
        assertEquals(List.of(false), fixture.completions);
    }

    @Test
    public void executorRejectionCompletesOnceAndRetainsNoRequest() {
        Fixture fixture = new Fixture();
        fixture.idle = true;
        fixture.restart.request(command -> { throw new RejectedExecutionException(); },
                fixture.authority, fixture.completions::add);
        fixture.restart.ready();
        fixture.restart.cancel();

        assertEquals(0, fixture.admissions);
        assertEquals(List.of(false), fixture.completions);
    }

    @Test
    public void launchFailureCompletesFalseAndAllowsAnotherRequest() {
        Fixture fixture = new Fixture();
        fixture.idle = true;
        fixture.launchResult = false;
        fixture.request();
        fixture.queued.remove().run();

        fixture.idle = true; // The wrapper unwinds its failed start.
        fixture.launchResult = true;
        fixture.request();
        fixture.queued.remove().run();
        assertEquals(2, fixture.admissions);
        assertEquals(2, fixture.launches);
        assertEquals(List.of(false, true), fixture.completions);
    }
}

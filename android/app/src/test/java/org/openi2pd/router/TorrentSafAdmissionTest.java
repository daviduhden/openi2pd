/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * Remote capability publication and stale-completion regression controls.
 */
package org.openi2pd.router;

import static org.junit.Assert.*;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.atomic.AtomicReference;
import org.junit.Test;

public final class TorrentSafAdmissionTest {
    @Test
    public void commitsWithoutReleasing() {
        List<Long> released = new ArrayList<>();
        assertEquals(71, TorrentSafAdmission.publish(71, id -> id == 71, released::add));
        assertTrue(released.isEmpty());
    }

    @Test
    public void lateCompletionRetiresOriginalAuthority() {
        List<Long> oldBroker = new ArrayList<>();
        List<Long> successor = new ArrayList<>();
        AtomicReference<TorrentSafAdmission.Release> current =
                new AtomicReference<>(oldBroker::add);
        TorrentSafAdmission.Release admittingBroker = current.get();
        current.set(successor::add);
        assertEquals(0, TorrentSafAdmission.publish(72, id -> false, admittingBroker));
        assertEquals(List.of(72L), oldBroker);
        assertTrue(successor.isEmpty());
    }

    @Test
    public void publicationExceptionSurvivesCleanupFailure() {
        RuntimeException original = new IllegalStateException("publication fixture");
        List<Long> released = new ArrayList<>();
        try {
            TorrentSafAdmission.publish(73,
                    id
                    -> { throw original; },
                    id -> {
                        released.add(id);
                        throw new Exception("cleanup fixture");
                    });
            fail("publication must fail");
        } catch (RuntimeException observed) {
            assertSame(original, observed);
        }
        assertEquals(List.of(73L), released);
    }

    @Test
    public void refusalDoesNotPublishOrRelease() {
        assertEquals(0, TorrentSafAdmission.publish(0, id -> {
            fail("refused ID must not publish");
            return true;
        }, id -> fail("refused ID must not release")));
    }
    @Test
    public void acquisitionPrecedesPublicationAndCleanup() throws Exception {
        List<String> steps = new ArrayList<>();
        assertEquals(0,
                TorrentSafAdmission.acquire(
                        ()
                                -> {
                            steps.add("acquire");
                            return 74;
                        },
                        id
                        -> {
                            assertEquals(74, id);
                            steps.add("stale");
                            return false;
                        },
                        id -> {
                            assertEquals(74, id);
                            steps.add("release");
                        }));
        assertEquals(List.of("acquire", "stale", "release"), steps);
    }

    @Test
    public void failedAcquisitionHasNoCapabilityToRelease() {
        Exception original = new Exception("acquisition fixture");
        try {
            TorrentSafAdmission.acquire(()
                                                -> { throw original; },
                    id
                    -> {
                        fail("failed admission must not publish");
                        return true;
                    },
                    id -> fail("failed admission must not release"));
            fail("admission must fail");
        } catch (Exception observed) {
            assertSame(original, observed);
        }
    }
    @Test
    public void cleanupErrorDoesNotReplacePublicationFailure() {
        RuntimeException original = new IllegalStateException("publication fixture");
        try {
            TorrentSafAdmission.publish(75,
                    id
                    -> { throw original; },
                    id -> { throw new AssertionError("cleanup fixture"); });
            fail("publication must fail");
        } catch (RuntimeException observed) {
            assertSame(original, observed);
        }
    }

    @Test
    public void cleanupErrorWithoutPrimaryIsNotHidden() {
        AssertionError cleanup = new AssertionError("cleanup fixture");
        try {
            TorrentSafAdmission.publish(76, id -> false, id -> { throw cleanup; });
            fail("cleanup error must propagate");
        } catch (AssertionError observed) {
            assertSame(cleanup, observed);
        }
    }
}

/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * Publication of a newly admitted broker capability.
 */
package org.openi2pd.router;

import java.util.function.LongPredicate;

/** Keeps remote admission under cleanup authority until publication. */
final class TorrentSafAdmission {
    @FunctionalInterface
    interface Release {
        void release(long capability) throws Exception;
    }

    @FunctionalInterface
    interface Acquire {
        long acquire() throws Exception;
    }

    // Callers prepare every callback before the remote admission starts.
    // No allocating closure construction may strand an admitted result.
    static long acquire(Acquire operation, LongPredicate publisher, Release release)
            throws Exception {
        return publish(operation.acquire(), publisher, release);
    }

    private TorrentSafAdmission() {
    }

    /**
     * Publish to the still-current local owner or retire the exact result.
     * The publisher owns its synchronization; cleanup runs after it returns,
     * outside that authority. The releaser must capture the admitting broker,
     * never resolve a replacement binding. Cleanup failure cannot replace a
     * publication exception or authorize fallback storage.
     *
     * @param capability newly admitted positive remote ID; zero is refusal
     * @param publisher returns true only after local ownership commits
     * @param release retires an uncommitted ID on the original broker
     * @return the committed capability, or zero on refusal/stale ownership
     */
    static long publish(long capability, LongPredicate publisher, Release release) {
        if (capability <= 0)
            return 0;
        boolean committed = false;
        Throwable primary = null;
        try {
            committed = publisher.test(capability);
            return committed ? capability : 0;
        } catch (RuntimeException | Error failure) {
            primary = failure;
            throw failure;
        } finally {
            if (!committed) {
                try {
                    release.release(capability);
                } catch (Exception ignored) {
                    // Dead brokers already retired their capabilities.
                    // Other cleanup exceptions remain best effort.
                } catch (Error cleanupFailure) {
                    // Preserve an existing failure without allocating a
                    // suppressed-exception record during memory pressure.
                    if (primary == null)
                        throw cleanupFailure;
                }
            }
        }
    }
}

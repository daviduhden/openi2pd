/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: TorrentSafPolicy.
 *
 * Framework-free SAF bounds shared by the broker and JVM tests.
 */

package org.openi2pd.router;

/**
 * Framework-free bounds shared by the SAF broker, its client and JVM
 * tests.
 *
 * <p>Every helper answers a pure admission question and performs no IO
 * and no synchronization, so the same code decides the outcome in the
 * broker process, in the client and in a plain JVM test. Untrusted
 * values are only compared, never used to compute an allocation: ranges
 * are checked by subtraction after {@code offset <= length} and
 * transfers are capped at {@link #MAX_TRANSFER} bytes per Binder call.
 * Rejection is always a typed {@code false}; a defect here would weaken
 * an authorization decision, so the caps are part of the AIDL v2
 * contract and must not be raised without coordinator review.</p>
 */
final class TorrentSafPolicy {
    /** Largest read/write payload carried by one Binder transaction. */
    static final int MAX_TRANSFER = 16384;

    private TorrentSafPolicy() {
    }

    /**
     * Check a transfer against both its document length and the IPC
     * chunk cap. Subtraction after {@code offset <= length} avoids
     * overflowing an {@code offset + count} calculation on an untrusted
     * request.
     */
    static boolean validRange(long length, long offset, int count) {
        return length >= 0 && offset >= 0 && count >= 0 && count <= MAX_TRANSFER && offset <= length
                && count <= length - offset;
    }

    // Exports grow from zero to the declared maximum; only the final
    // commit requires exact equality.
    static boolean exportSizeAllowed(long declared, long current) {
        return declared > 0 && current >= 0 && current <= declared;
    }

    static boolean documentIdentityMatches(String expected, String current) {
        return expected != null && !expected.isEmpty() && expected.equals(current);
    }

    static boolean canExtendExisting(boolean create, long actual, long declared) {
        return create && actual >= 0 && declared >= 0 && actual < declared;
    }
}

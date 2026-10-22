/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: TorrentSafPolicy.
 *
 * Framework-free SAF bounds shared by the broker and JVM tests.
 */

package org.openi2pd.router;

final class TorrentSafPolicy {
    static final int MAX_TRANSFER = 16384;

    private TorrentSafPolicy() {
    }

    static boolean validRange(long length, long offset, int count) {
        return length >= 0 && offset >= 0 && count >= 0 && count <= MAX_TRANSFER && offset <= length
                && count <= length - offset;
    }

    // Exports grow from zero to the declared maximum; only the final commit
    // requires exact equality.
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

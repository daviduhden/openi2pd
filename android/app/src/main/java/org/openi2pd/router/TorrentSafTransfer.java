/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: exact positional SAF transfers.
 */
package org.openi2pd.router;

/**
 * Framework-free exact-transfer loop shared by broker reads and writes.
 * The caller retains descriptor ownership, authorization, synchronization
 * and the existing operation deadline. This helper neither retries failed
 * calls nor extends their lifetime; a failed write may have changed bytes.
 */
final class TorrentSafTransfer {
    @FunctionalInterface
    interface Cancellation {
        void check() throws Exception;
    }

    @FunctionalInterface
    interface Fragment {
        int transfer(int bufferOffset, int remaining, long fileOffset) throws Exception;
    }

    private TorrentSafTransfer() {
    }

    /**
     * Complete one bounded range, checking cancellation before each call.
     * Zero bytes require no calls. EOF, zero progress and invalid progress
     * return false; exceptions propagate unchanged to the broker's existing
     * handle-retirement path. Success does not fsync or commit a write.
     */
    static boolean exact(long offset, int count, Cancellation cancellation, Fragment fragment)
            throws Exception {
        if (offset < 0 || count < 0 || count > TorrentSafPolicy.MAX_TRANSFER
                || offset > Long.MAX_VALUE - count)
            return false;
        int done = 0;
        while (done < count) {
            cancellation.check();
            int remaining = count - done;
            int transferred = fragment.transfer(done, remaining, offset + done);
            if (transferred <= 0 || transferred > remaining)
                return false;
            done += transferred;
        }
        return true;
    }
}

/*
 * See the LICENSE file at the top of the project tree for copyright and
 * license details.
 *
 * openi2pd: RouterErrorText.
 *
 * Maps internal failures to bounded, non-sensitive snapshot text.
 *
 */

package org.openi2pd.router;

import java.io.IOException;
import java.util.concurrent.CancellationException;
import java.util.concurrent.RejectedExecutionException;
import java.util.concurrent.TimeoutException;

/* Converts internal exceptions to safe text for UI snapshots. */
final class RouterErrorText {
    private RouterErrorText() {}

    /*
     * Return a stable public category without exception messages, causes,
     * class names or stack traces. Those fields can contain paths and
     * provider-controlled data.
     */
    static String describe(Throwable error) {
        if (error == null) {
            return "";
        }
        if (error instanceof SecurityException) {
            return "Permission denied";
        }
        if (error instanceof IOException) {
            return "I/O operation failed";
        }
        if (error instanceof TimeoutException) {
            return "Operation timed out";
        }
        if (error instanceof CancellationException) {
            return "Operation cancelled";
        }
        if (error instanceof RejectedExecutionException) {
            return "Service is stopping";
        }
        if (error instanceof UnsatisfiedLinkError) {
            return "Native router unavailable";
        }
        if (error instanceof IllegalArgumentException) {
            return "Invalid operation";
        }
        return "Operation failed";
    }
}

/*
 * See the LICENSE file at the top of the project tree for copyright and
 * license details.
 *
 * openi2pd: RouterErrorTextTest.
 *
 * Pure-JVM regressions for safe error text published in router snapshots.
 *
 */

package org.openi2pd.router;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import org.junit.Test;

import java.io.IOException;
import java.util.concurrent.TimeoutException;

/** Ensures exception data is not copied into UI-facing snapshots. */
public class RouterErrorTextTest {
    @Test
    public void mapsKnownFailuresToStableCategories() {
        assertEquals("", RouterErrorText.describe(null));
        assertEquals("Permission denied", RouterErrorText.describe(new SecurityException("private")));
        assertEquals("I/O operation failed", RouterErrorText.describe(new IOException("private")));
        assertEquals("Operation timed out", RouterErrorText.describe(new TimeoutException("private")));
        assertEquals("Invalid operation", RouterErrorText.describe(new IllegalArgumentException("private")));
        assertEquals("Operation failed", RouterErrorText.describe(new RuntimeException("private")));
    }

    @Test
    public void doesNotPublishMessageCausePathOrProviderMarkers() {
        String marker = "provider-secret-/data/user/0/app/private.bin";
        IOException providerFailure = new IOException(marker);
        RuntimeException wrapped = new RuntimeException("wrapper-" + marker, providerFailure);

        String published = RouterErrorText.describe(wrapped);

        assertEquals("Operation failed", published);
        assertFalse(published.contains(marker));
        assertFalse(published.contains("/data/user/0"));
        assertFalse(published.contains("provider-secret"));
        assertTrue(published.length() <= 128);
    }
}

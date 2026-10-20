/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: TorrentMetadataJniTest.
 *
 * Device regressions for the native metadata bridge used by SAF creation.
 * Execution is reserved for manual Android CI.
 */
package org.openi2pd.router;

import static org.junit.Assert.*;
import androidx.test.ext.junit.runners.AndroidJUnit4;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import org.junit.Test;
import org.junit.runner.RunWith;

@RunWith(AndroidJUnit4.class)
public final class TorrentMetadataJniTest {
    private static byte[] utf8(String value) { return value.getBytes(StandardCharsets.UTF_8); }

    @Test public void singleFileAndSharedBoundaryMetadata() throws Exception {
        I2PD_JNI.loadLibraries();
        byte[] digest = MessageDigest.getInstance("SHA-1").digest(utf8("abcdef"));
        byte[] single = I2PD_JNI.buildTorrentMetadata(utf8("source"),
                new byte[][] {utf8("")}, new long[] {6}, 1024 * 1024, digest, new byte[0][], false);
        assertNotNull(single);
        assertTrue(new String(single, StandardCharsets.ISO_8859_1).contains("6:lengthi6e"));
        byte[] multi = I2PD_JNI.buildTorrentMetadata(utf8("folder"),
                new byte[][] {utf8("a"), utf8("b")}, new long[] {2, 4}, 1024 * 1024, digest, new byte[0][], false);
        assertNotNull(multi);
        String encoded = new String(multi, StandardCharsets.ISO_8859_1);
        assertTrue(encoded.contains("5:filesl"));
        assertTrue(encoded.contains(new String(digest, StandardCharsets.ISO_8859_1)));
    }

    @Test public void rejectTraversalBadHashesAndOversizedLengths() throws Exception {
        I2PD_JNI.loadLibraries();
        byte[] digest = MessageDigest.getInstance("SHA-1").digest(utf8("a"));
        assertNull(I2PD_JNI.buildTorrentMetadata(utf8("folder"),
                new byte[][] {utf8("../outside")}, new long[] {1}, 1024 * 1024, digest, new byte[0][], false));
        assertNull(I2PD_JNI.buildTorrentMetadata(utf8("source"),
                new byte[][] {utf8("")}, new long[] {-1}, 1024 * 1024, digest, new byte[0][], false));
        assertNull(I2PD_JNI.buildTorrentMetadata(utf8("source"),
                new byte[][] {utf8("")}, new long[] {1}, 1024 * 1024, new byte[19], new byte[0][], false));
        assertNull(I2PD_JNI.buildTorrentMetadata(utf8("source"),
                new byte[][] {utf8("")}, new long[] {Long.MAX_VALUE}, 1024 * 1024, digest, new byte[0][], false));
    }
}

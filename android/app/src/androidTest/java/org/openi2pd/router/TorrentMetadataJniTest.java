/*
 * See the LICENSE file at the top of the project tree for copyright and
 * license details.
 *
 * openi2pd: TorrentMetadataJniTest.
 *
 * Device regressions for the native metadata bridge used by SAF
 * creation. Execution is reserved for manual Android CI.
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
    private static byte[] utf8(String value) {
        return value.getBytes(StandardCharsets.UTF_8);
    }

    // String's repeat instance method is unavailable on API 29.
    private static String repeat(String value, int count) {
        StringBuilder result = new StringBuilder();
        for (int i = 0; i < count; ++i)
            result.append(value);
        return result.toString();
    }

    @Test
    public void singleFileAndSharedBoundaryMetadata() throws Exception {
        I2PD_JNI.loadLibraries();
        byte[] digest = MessageDigest.getInstance("SHA-1").digest(utf8("abcdef"));
        byte[] single = I2PD_JNI.buildTorrentMetadata(utf8("source"), new byte[][] {utf8("")},
                new long[] {6}, 1024 * 1024, digest, new byte[0][], false);
        assertNotNull(single);
        assertTrue(new String(single, StandardCharsets.ISO_8859_1).contains("6:lengthi6e"));
        byte[] multi =
                I2PD_JNI.buildTorrentMetadata(utf8("folder"), new byte[][] {utf8("a"), utf8("b")},
                        new long[] {2, 4}, 1024 * 1024, digest, new byte[0][], false);
        assertNotNull(multi);
        String encoded = new String(multi, StandardCharsets.ISO_8859_1);
        assertTrue(encoded.contains("5:filesl"));
        assertTrue(encoded.contains(new String(digest, StandardCharsets.ISO_8859_1)));
    }

    @Test
    public void rejectTraversalBadHashesAndOversizedLengths() throws Exception {
        I2PD_JNI.loadLibraries();
        byte[] digest = MessageDigest.getInstance("SHA-1").digest(utf8("a"));
        assertNull(I2PD_JNI.buildTorrentMetadata(utf8("folder"), new byte[][] {utf8("../outside")},
                new long[] {1}, 1024 * 1024, digest, new byte[0][], false));
        assertNull(I2PD_JNI.buildTorrentMetadata(utf8("source"), new byte[][] {utf8("")},
                new long[] {-1}, 1024 * 1024, digest, new byte[0][], false));
        assertNull(I2PD_JNI.buildTorrentMetadata(utf8("source"), new byte[][] {utf8("")},
                new long[] {1}, 1024 * 1024, new byte[19], new byte[0][], false));
        assertNull(I2PD_JNI.buildTorrentMetadata(utf8("source"), new byte[][] {utf8("")},
                new long[] {Long.MAX_VALUE}, 1024 * 1024, digest, new byte[0][], false));
    }

    @Test
    public void aggregatePathBudgetRejectsAmplificationFromManyValidPaths() throws Exception {
        I2PD_JNI.loadLibraries();
        String prefix = repeat(repeat("a", 255) + "/", 31);
        byte[][] paths = new byte[1100][];
        long[] lengths = new long[paths.length];
        java.util.Arrays.fill(lengths, 1);
        for (int i = 0; i < paths.length; ++i)
            paths[i] = utf8(prefix + repeat("b", 250) + i);
        byte[] digest = MessageDigest.getInstance("SHA-1").digest(new byte[paths.length]);
        assertNull(I2PD_JNI.buildTorrentMetadata(
                utf8("folder"), paths, lengths, 1024 * 1024, digest, new byte[0][], false));
        // The individual paths remain valid below the aggregate cap.
        byte[] smallDigest = MessageDigest.getInstance("SHA-1").digest(new byte[2]);
        assertNotNull(
                I2PD_JNI.buildTorrentMetadata(utf8("folder"), new byte[][] {paths[0], paths[1]},
                        new long[] {1, 1}, 1024 * 1024, smallDigest, new byte[0][], false));
    }

    @Test
    public void supplementaryNamesUseStandardUtf8RatherThanModifiedUtf8() throws Exception {
        I2PD_JNI.loadLibraries();
        byte[] digest = MessageDigest.getInstance("SHA-1").digest(utf8("a"));
        String supplementary = "\uD83D\uDE00";
        assertNotNull(I2PD_JNI.buildTorrentMetadata(utf8(repeat(supplementary, 63)),
                new byte[][] {utf8("")}, new long[] {1}, 1024 * 1024, digest, new byte[0][],
                false));
        assertNull(I2PD_JNI.buildTorrentMetadata(utf8(repeat(supplementary, 64)),
                new byte[][] {utf8("")}, new long[] {1}, 1024 * 1024, digest, new byte[0][],
                false));
    }
}

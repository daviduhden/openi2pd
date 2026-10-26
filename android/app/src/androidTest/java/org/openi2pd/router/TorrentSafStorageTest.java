/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 * openi2pd: SAF capability input regressions, manual device CI only.
 */
package org.openi2pd.router;

import static org.junit.Assert.*;
import androidx.test.ext.junit.runners.AndroidJUnit4;
import org.junit.Test;
import org.junit.runner.RunWith;

@RunWith(AndroidJUnit4.class)
public final class TorrentSafStorageTest {
    @Test
    public void rejectsTraversalAndAmbiguousNames() {
        for (String path :
                new String[] {"", "/file", "a/../file", "a//file", "a/./b", "a\u0000b", "a/"})
            assertFalse(path, TorrentSafStorageService.validPath(path));
        assertTrue(TorrentSafStorageService.validPath("torrent/subdirectory/file.bin"));
        assertTrue(TorrentSafStorageService.validPath("日本語/part.bin"));
        // Android display names may legally contain these; they are not POSIX separators here.
        assertTrue(TorrentSafStorageService.validPath("Album/Track 1: Intro.flac"));
        assertTrue(TorrentSafStorageService.validPath("weird\\name.bin"));
    }
    @Test
    public void rejectsOverflowAndOversizeTransfers() {
        assertTrue(TorrentSafStorageService.validRange(32768, 16384, 16384));
        assertTrue(TorrentSafStorageService.validRange(0, 0, 0));
        assertFalse(TorrentSafStorageService.validRange(32768, 16384, 16385));
        assertFalse(TorrentSafStorageService.validRange(Long.MAX_VALUE, Long.MAX_VALUE, 1));
        assertFalse(TorrentSafStorageService.validRange(1, -1, 1));
        assertFalse(TorrentSafStorageService.validRange(1, 0, -1));
        assertFalse(TorrentSafStorageService.validRange(10, 11, 0));
    }
    @Test
    public void deletionRejectsReplacedDocumentWithSameLength() throws Exception {
        java.io.File directory =
                androidx.test.platform.app.InstrumentationRegistry.getInstrumentation()
                        .getTargetContext()
                        .getCacheDir();
        java.io.File source = java.io.File.createTempFile("saf-delete", ".bin", directory);
        java.io.File replacement = java.io.File.createTempFile("saf-replaced", ".bin", directory);
        try {
            try (var data = new java.io.RandomAccessFile(source, "rw")) {
                data.setLength(16);
            }
            try (var data = new java.io.RandomAccessFile(replacement, "rw")) {
                data.setLength(16);
            }
            try (var opened = android.os.ParcelFileDescriptor.open(
                         source, android.os.ParcelFileDescriptor.MODE_READ_ONLY)) {
                var original = android.system.Os.fstat(opened.getFileDescriptor());
                assertTrue(TorrentSafStorageService.sameDocument(original, original, 16));
                assertFalse(TorrentSafStorageService.sameDocument(original, original, 17));
                android.system.Os.rename(replacement.getAbsolutePath(), source.getAbsolutePath());
                try (var fresh = android.os.ParcelFileDescriptor.open(
                             source, android.os.ParcelFileDescriptor.MODE_READ_ONLY)) {
                    assertFalse(TorrentSafStorageService.sameDocument(
                            original, android.system.Os.fstat(fresh.getFileDescriptor()), 16));
                }
            }
        } finally {
            source.delete();
            replacement.delete();
        }
    }
    @Test
    public void rejectsUnboundedPaths() {
        assertFalse(TorrentSafStorageService.validPath(repeat("x", 256)));
        assertFalse(TorrentSafStorageService.validPath(repeat("a/", 33) + "file"));
    }
    @Test
    public void nestedFixturePathsFollowThePathPolicy() {
        // The multi-file fixture layout is exactly what the engine opens.
        for (String path : new String[] {
                     "torrent/part-00001.bin", "torrent/sub/part-00002.bin", "torrent/empty.bin"})
            assertTrue(path, TorrentSafStorageService.validPath(path));
        for (String path : new String[] {"/torrent/part-00001.bin", "torrent/../part-00001.bin",
                     "torrent//part-00001.bin", "torrent/./part-00001.bin", "torrent/",
                     "torrent/part-00001.bin\u0000", "torrent/part-00001.bin\n",
                     "torrent/part-00001.bin\t"})
            assertFalse(path, TorrentSafStorageService.validPath(path));
    }
    @Test
    public void exactSizeRulesAreEnforcedOnOpen() {
        // A read-only open must match the declared length exactly; only a
        // write-mode open may extend an existing shorter document.
        assertFalse(TorrentSafPolicy.canExtendExisting(false, 0, 16384));
        assertTrue(TorrentSafPolicy.canExtendExisting(true, 0, 16384));
        assertTrue(TorrentSafPolicy.canExtendExisting(true, 12000, 16384));
        assertFalse(TorrentSafPolicy.canExtendExisting(true, 16384, 16384));
        assertFalse(TorrentSafPolicy.canExtendExisting(true, 16385, 16384));
        // The final commit of an export still requires the exact size.
        assertTrue(TorrentSafPolicy.exportSizeAllowed(16384, 0));
        assertTrue(TorrentSafPolicy.exportSizeAllowed(16384, 16384));
        assertFalse(TorrentSafPolicy.exportSizeAllowed(16384, 16385));
    }
    @Test
    public void zeroLengthDocumentsTransferNothing() {
        assertTrue(TorrentSafStorageService.validRange(0, 0, 0));
        assertFalse(TorrentSafStorageService.validRange(0, 0, 1));
        assertFalse(TorrentSafStorageService.validRange(0, 1, 0));
    }
    // String.repeat is API 33; instrumentation must still run on API 29.
    private static String repeat(String value, int count) {
        StringBuilder result = new StringBuilder(value.length() * count);
        for (int i = 0; i < count; ++i)
            result.append(value);
        return result.toString();
    }
}

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
    @Test public void rejectsTraversalAndAmbiguousNames() {
        for (String path : new String[]{"", "/file", "a/../file", "a//file", "a/./b", "a\\b", "a:b", "a\u0000b", "a/"})
            assertFalse(path, TorrentSafStorageService.validPath(path));
        assertTrue(TorrentSafStorageService.validPath("torrent/subdirectory/file.bin"));
        assertTrue(TorrentSafStorageService.validPath("日本語/part.bin"));
    }
    @Test public void rejectsOverflowAndOversizeTransfers() {
        assertTrue(TorrentSafStorageService.validRange(32768, 16384, 16384));
        assertTrue(TorrentSafStorageService.validRange(0, 0, 0));
        assertFalse(TorrentSafStorageService.validRange(32768, 16384, 16385));
        assertFalse(TorrentSafStorageService.validRange(Long.MAX_VALUE, Long.MAX_VALUE, 1));
        assertFalse(TorrentSafStorageService.validRange(1, -1, 1));
        assertFalse(TorrentSafStorageService.validRange(1, 0, -1));
        assertFalse(TorrentSafStorageService.validRange(10, 11, 0));
    }
    @Test public void deletionRejectsReplacedDocumentWithSameLength() throws Exception {
        java.io.File directory = androidx.test.platform.app.InstrumentationRegistry.getInstrumentation()
                .getTargetContext().getCacheDir();
        java.io.File source = java.io.File.createTempFile("saf-delete", ".bin", directory);
        java.io.File replacement = java.io.File.createTempFile("saf-replaced", ".bin", directory);
        try {
            try (var data = new java.io.RandomAccessFile(source, "rw")) { data.setLength(16); }
            try (var data = new java.io.RandomAccessFile(replacement, "rw")) { data.setLength(16); }
            try (var opened = android.os.ParcelFileDescriptor.open(source, android.os.ParcelFileDescriptor.MODE_READ_ONLY)) {
                var original = android.system.Os.fstat(opened.getFileDescriptor());
                assertTrue(TorrentSafStorageService.sameDocument(original, original, 16));
                assertFalse(TorrentSafStorageService.sameDocument(original, original, 17));
                android.system.Os.rename(replacement.getAbsolutePath(), source.getAbsolutePath());
                try (var fresh = android.os.ParcelFileDescriptor.open(source, android.os.ParcelFileDescriptor.MODE_READ_ONLY)) {
                    assertFalse(TorrentSafStorageService.sameDocument(original,
                            android.system.Os.fstat(fresh.getFileDescriptor()), 16));
                }
            }
        } finally { source.delete(); replacement.delete(); }
    }
    @Test public void rejectsUnboundedPaths() {
        assertFalse(TorrentSafStorageService.validPath("x".repeat(256)));
        assertFalse(TorrentSafStorageService.validPath("a/".repeat(33) + "file"));
    }
}

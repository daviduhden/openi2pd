/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 * openi2pd: provider fault behavior regressions for manual device CI.
 */
package org.openi2pd.router;

import static org.junit.Assert.*;
import android.content.pm.ProviderInfo;
import android.database.Cursor;
import android.os.CancellationSignal;
import android.os.OperationCanceledException;
import android.os.ParcelFileDescriptor;
import android.provider.DocumentsContract;
import android.system.ErrnoException;
import android.system.Os;
import android.system.OsConstants;
import android.system.StructStat;
import androidx.test.platform.app.InstrumentationRegistry;
import androidx.test.ext.junit.runners.AndroidJUnit4;
import java.io.FileNotFoundException;
import java.util.Arrays;
import java.util.concurrent.*;
import org.junit.Test;
import org.junit.runner.RunWith;

@RunWith(AndroidJUnit4.class)
public final class TorrentSafProviderCancellationTest {
    private TorrentSafFixtureProvider provider() {
        TorrentSafFixtureProvider provider = new TorrentSafFixtureProvider();
        ProviderInfo info = new ProviderInfo();
        info.authority = TorrentSafFixtureProvider.AUTHORITY;
        info.exported = true;
        info.grantUriPermissions = true;
        info.readPermission = info.writePermission = "android.permission.MANAGE_DOCUMENTS";
        provider.attachInfo(InstrumentationRegistry.getInstrumentation().getTargetContext(), info);
        return provider;
    }
    // These tests exercise real DocumentsProvider cancellation and
    // descriptor behavior. Persistable-grant acquisition and broker
    // death/rebind remain separate end-to-end CI cases.
    @Test
    public void cooperativeProviderObservesCancellation() throws Exception {
        TorrentSafFixtureProvider provider = provider();
        provider.configure(TorrentSafFixtureProvider.COOPERATIVE, "open");
        CancellationSignal signal = new CancellationSignal();
        ExecutorService worker = Executors.newSingleThreadExecutor();
        Future<ParcelFileDescriptor> call =
                worker.submit(() -> provider.openDocument("payload", "rw", signal));
        try {
            assertTrue(provider.awaitEntered());
            signal.cancel();
            try {
                call.get(2, TimeUnit.SECONDS);
                fail("Expected provider cancellation");
            } catch (ExecutionException canceled) {
                assertTrue(canceled.getCause() instanceof OperationCanceledException);
            }
            assertEquals(1, provider.cancellations());
        } finally {
            provider.unblock();
            worker.shutdownNow();
        }
    }
    @Test
    public void ignoredCancellationRequiresBrokerRetirement() throws Exception {
        TorrentSafFixtureProvider provider = provider();
        provider.configure(TorrentSafFixtureProvider.IGNORE_CANCELLATION, "open");
        CancellationSignal signal = new CancellationSignal();
        ExecutorService worker = Executors.newSingleThreadExecutor();
        Future<ParcelFileDescriptor> call =
                worker.submit(() -> provider.openDocument("payload", "rw", signal));
        try {
            assertTrue(provider.awaitEntered());
            signal.cancel();
            try {
                call.get(100, TimeUnit.MILLISECONDS);
                fail("Fixture must ignore cancellation");
            } catch (TimeoutException expected) {
                assertEquals(1, provider.cancellations());
            }
            provider.unblock();
            try (ParcelFileDescriptor fd = call.get(2, TimeUnit.SECONDS)) {
                assertNotNull(fd);
            }
        } finally {
            provider.unblock();
            worker.shutdownNow();
        }
    }
    @Test
    public void nonSeekableProviderCannotBecomeRandomAccessStorage() throws Exception {
        TorrentSafFixtureProvider provider = provider();
        provider.configure(TorrentSafFixtureProvider.PIPE, "open");
        try (ParcelFileDescriptor fd =
                        provider.openDocument("payload", "r", new CancellationSignal())) {
            try {
                Os.lseek(fd.getFileDescriptor(), 0, OsConstants.SEEK_SET);
                fail("Expected ESPIPE");
            } catch (ErrnoException unsupported) {
                assertEquals(OsConstants.ESPIPE, unsupported.errno);
            }
        }
    }
    @Test
    public void sequentialSocketProviderIsRejectedWithoutStaging() throws Exception {
        TorrentSafFixtureProvider provider = provider();
        provider.configure(TorrentSafFixtureProvider.NO_SEEK, "open");
        try (ParcelFileDescriptor fd = provider.openDocument(
                     TorrentSafFixtureProvider.PART_ONE, "rw", new CancellationSignal())) {
            try {
                Os.lseek(fd.getFileDescriptor(), 0, OsConstants.SEEK_SET);
                fail("Expected ESPIPE");
            } catch (ErrnoException unsupported) {
                assertEquals(OsConstants.ESPIPE, unsupported.errno);
            }
            // The broker's regular-file check fails too, so no
            // temporary copy is ever attempted for this provider.
            assertFalse(OsConstants.S_ISREG(Os.fstat(fd.getFileDescriptor()).st_mode));
        }
    }
    @Test
    public void revokedProviderFailsClosed() throws Exception {
        TorrentSafFixtureProvider provider = provider();
        provider.configure(TorrentSafFixtureProvider.REVOKED, "open");
        try {
            provider.openDocument("payload", "rw", new CancellationSignal()).close();
            fail("Expected missing grant");
        } catch (FileNotFoundException expected) {
        } catch (java.io.IOException ignored) {
        }
    }
    @Test
    public void nestedTreeListsTheMultiFileLayout() throws Exception {
        TorrentSafFixtureProvider provider = provider();
        provider.configure(TorrentSafFixtureProvider.NORMAL, "open");
        // This is the layout the native engine opens by relative path.
        assertTrue(hasChild(provider, TorrentSafFixtureProvider.ROOT, "torrent"));
        assertTrue(hasChild(provider, TorrentSafFixtureProvider.TORRENT, "part-00001.bin"));
        assertTrue(hasChild(provider, TorrentSafFixtureProvider.TORRENT, "sub"));
        assertTrue(hasChild(provider, TorrentSafFixtureProvider.TORRENT, "empty.bin"));
        assertTrue(hasChild(provider, TorrentSafFixtureProvider.SUBDIRECTORY, "part-00002.bin"));
        // Document IDs round-trip through the tree URI exactly as
        // resolve() uses them.
        for (String id : new String[] {TorrentSafFixtureProvider.PART_ONE,
                     TorrentSafFixtureProvider.PART_TWO, TorrentSafFixtureProvider.EMPTY})
            assertEquals(id,
                    DocumentsContract.getDocumentId(
                            DocumentsContract.buildDocumentUriUsingTree(treeUri(), id)));
    }
    @Test
    public void nestedDocumentsSupportExactRangeWrites() throws Exception {
        TorrentSafFixtureProvider provider = provider();
        provider.configure(TorrentSafFixtureProvider.NORMAL, "open");
        try (ParcelFileDescriptor fd = provider.openDocument(
                     TorrentSafFixtureProvider.PART_TWO, "rw", new CancellationSignal())) {
            assertEquals(TorrentSafFixtureProvider.PART_TWO_SIZE,
                    Os.fstat(fd.getFileDescriptor()).st_size);
            // The final block of a file whose length is not a chunk
            // multiple is written at the exact range the engine
            // computes.
            int finalBlock = 1000;
            long offset = TorrentSafFixtureProvider.PART_TWO_SIZE - finalBlock;
            byte[] tail = new byte[finalBlock];
            Arrays.fill(tail, (byte) 0x5a);
            assertEquals(
                    finalBlock, Os.pwrite(fd.getFileDescriptor(), tail, 0, finalBlock, offset));
            Os.fsync(fd.getFileDescriptor());
            byte[] read = new byte[finalBlock];
            int done = 0;
            while (done < read.length)
                done += Os.pread(
                        fd.getFileDescriptor(), read, done, read.length - done, offset + done);
            assertArrayEquals(tail, read);
            // A transfer past the declared end is refused by the shared
            // range rule before the provider is reached.
            assertTrue(TorrentSafStorageService.validRange(
                    TorrentSafFixtureProvider.PART_TWO_SIZE, offset, finalBlock));
            assertFalse(TorrentSafStorageService.validRange(
                    TorrentSafFixtureProvider.PART_TWO_SIZE, offset, finalBlock + 1));
        }
    }
    @Test
    public void zeroLengthDocumentNeedsNoFragment() throws Exception {
        TorrentSafFixtureProvider provider = provider();
        provider.configure(TorrentSafFixtureProvider.NORMAL, "open");
        try (ParcelFileDescriptor fd = provider.openDocument(
                     TorrentSafFixtureProvider.EMPTY, "r", new CancellationSignal())) {
            assertEquals(0, Os.fstat(fd.getFileDescriptor()).st_size);
            // Empty documents emit no fragment and can never serve one
            // byte.
            assertFalse(TorrentSafStorageService.validRange(0, 0, 1));
            assertTrue(TorrentSafStorageService.validRange(0, 0, 0));
        }
    }
    @Test
    public void deletedDocumentFailsClosedAfterOpen() throws Exception {
        TorrentSafFixtureProvider provider = provider();
        provider.configure(TorrentSafFixtureProvider.DELETED_AFTER_OPEN, "open");
        try (ParcelFileDescriptor fd = provider.openDocument(
                     TorrentSafFixtureProvider.LEGACY, "r", new CancellationSignal())) {
            assertNotNull(fd);
            // The open descriptor still names the unlinked inode, but
            // the broker's authorized() recheck resolves the document
            // again and must now fail: the piece is invalidated, never
            // served stale.
            assertDocumentGone(provider, TorrentSafFixtureProvider.LEGACY);
        }
    }
    @Test
    public void sameLengthReplacementChangesIdentity() throws Exception {
        TorrentSafFixtureProvider provider = provider();
        provider.configure(TorrentSafFixtureProvider.REPLACED_SAME_LENGTH, "open");
        try (ParcelFileDescriptor first = provider.openDocument(
                     TorrentSafFixtureProvider.PART_ONE, "r", new CancellationSignal());
                ParcelFileDescriptor second = provider.openDocument(
                        TorrentSafFixtureProvider.PART_ONE, "r", new CancellationSignal())) {
            StructStat before = Os.fstat(first.getFileDescriptor());
            StructStat after = Os.fstat(second.getFileDescriptor());
            assertEquals(TorrentSafFixtureProvider.PART_ONE_SIZE, before.st_size);
            assertEquals(before.st_size, after.st_size);
            assertTrue("A same-length replacement must change the inode",
                    before.st_ino != after.st_ino);
            assertFalse("A same-length replacement must not pass the deletion check",
                    TorrentSafStorageService.sameDocument(
                            before, after, TorrentSafFixtureProvider.PART_ONE_SIZE));
            assertFalse(TorrentSafStorageService.sameDocumentIdentity(before, after));
            byte[] original = new byte[64], replaced = new byte[64];
            Os.pread(first.getFileDescriptor(), original, 0, original.length, 0);
            Os.pread(second.getFileDescriptor(), replaced, 0, replaced.length, 0);
            assertFalse(
                    "The replacement carries different bytes", Arrays.equals(original, replaced));
        }
    }
    @Test
    public void failedWriteIsNeverAcknowledgedAsPartialSuccess() throws Exception {
        TorrentSafFixtureProvider provider = provider();
        provider.configure(TorrentSafFixtureProvider.FAILS_WRITE, "open");
        try (ParcelFileDescriptor fd = provider.openDocument(
                     TorrentSafFixtureProvider.PART_ONE, "rw", new CancellationSignal())) {
            assertTrue(OsConstants.S_ISREG(Os.fstat(fd.getFileDescriptor()).st_mode));
            try {
                Os.pwrite(fd.getFileDescriptor(), new byte[4096], 0, 4096, 0);
                fail("Expected a refused write");
            } catch (ErrnoException rejected) {
                assertEquals(OsConstants.EBADF, rejected.errno);
            }
        }
    }
    @Test
    public void deleteRemovesTheDocumentFromTheCatalog() throws Exception {
        TorrentSafFixtureProvider provider = provider();
        provider.configure(TorrentSafFixtureProvider.NORMAL, "open");
        provider.deleteDocument(TorrentSafFixtureProvider.PART_TWO);
        assertFalse(hasChild(provider, TorrentSafFixtureProvider.SUBDIRECTORY, "part-00002.bin"));
        // Deletion is not a recursive tree removal: the sibling part
        // survives.
        assertTrue(hasChild(provider, TorrentSafFixtureProvider.TORRENT, "part-00001.bin"));
        assertDocumentGone(provider, TorrentSafFixtureProvider.PART_TWO);
    }
    @Test
    public void revocationAfterOpenFailsClosed() throws Exception {
        TorrentSafFixtureProvider provider = provider();
        provider.configure(TorrentSafFixtureProvider.REVOKE_AFTER_OPEN, "open");
        try (ParcelFileDescriptor fd = provider.openDocument(
                     TorrentSafFixtureProvider.LEGACY, "r", new CancellationSignal())) {
            assertNotNull(fd);
            assertDocumentGone(provider, TorrentSafFixtureProvider.LEGACY);
        }
    }
    private static android.net.Uri treeUri() {
        return DocumentsContract.buildTreeDocumentUri(
                TorrentSafFixtureProvider.AUTHORITY, TorrentSafFixtureProvider.ROOT);
    }
    private static boolean hasChild(TorrentSafFixtureProvider provider, String parent, String name)
            throws Exception {
        try (Cursor cursor = provider.queryChildDocuments(parent, null, (String) null)) {
            int column =
                    cursor.getColumnIndexOrThrow(DocumentsContract.Document.COLUMN_DISPLAY_NAME);
            while (cursor.moveToNext())
                if (name.equals(cursor.getString(column)))
                    return true;
            return false;
        }
    }
    private static void assertDocumentGone(TorrentSafFixtureProvider provider, String id) {
        try {
            provider.queryDocument(id, null);
            fail("Expected a missing document: " + id);
        } catch (FileNotFoundException expected) {
        }
        try {
            provider.openDocument(id, "r", new CancellationSignal()).close();
            fail("Expected a refused open: " + id);
        } catch (FileNotFoundException expected) {
        } catch (java.io.IOException ignored) {
        }
    }
}

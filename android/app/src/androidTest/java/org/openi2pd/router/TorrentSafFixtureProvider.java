/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 * openi2pd: bounded manual-CI DocumentsProvider fault fixture.
 */
package org.openi2pd.router;

import android.database.Cursor;
import android.database.MatrixCursor;
import android.os.CancellationSignal;
import android.os.ParcelFileDescriptor;
import android.provider.DocumentsContract;
import android.provider.DocumentsProvider;
import java.io.File;
import java.io.FileNotFoundException;
import java.io.IOException;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicInteger;

public final class TorrentSafFixtureProvider extends DocumentsProvider {
    static final int NORMAL = 0, COOPERATIVE = 1, IGNORE_CANCELLATION = 2, PIPE = 3, REVOKED = 4;
    private volatile int mode;
    private volatile String blockedPhase = "open";
    private volatile CountDownLatch entered = new CountDownLatch(1), release = new CountDownLatch(1);
    private final AtomicInteger cancellations = new AtomicInteger();
    void configure(int value, String phase) {
        release.countDown(); mode = value; blockedPhase = phase;
        entered = new CountDownLatch(1); release = new CountDownLatch(1); cancellations.set(0);
    }
    boolean awaitEntered() throws InterruptedException { return entered.await(3, TimeUnit.SECONDS); }
    void unblock() { release.countDown(); }
    int cancellations() { return cancellations.get(); }
    @Override public boolean onCreate() { return true; }
    private void gate(String phase, CancellationSignal signal) throws FileNotFoundException {
        if (mode == REVOKED) throw new FileNotFoundException("Fixture permission revoked");
        if (!phase.equals(blockedPhase) || mode != COOPERATIVE && mode != IGNORE_CANCELLATION) return;
        CountDownLatch gate = release; entered.countDown();
        if (signal != null) signal.setOnCancelListener(cancellations::incrementAndGet);
        long deadline = android.os.SystemClock.elapsedRealtime() + 60000;
        try {
            while (!gate.await(20, TimeUnit.MILLISECONDS)) {
                if (mode == COOPERATIVE && signal != null) signal.throwIfCanceled();
                if (android.os.SystemClock.elapsedRealtime() >= deadline) throw new FileNotFoundException("Fixture deadline");
            }
        } catch (InterruptedException stopped) { Thread.currentThread().interrupt(); throw new FileNotFoundException("Fixture interrupted"); }
        finally { if (signal != null) signal.setOnCancelListener(null); }
    }
    @Override public Cursor queryRoots(String[] projection) {
        MatrixCursor cursor = new MatrixCursor(new String[]{DocumentsContract.Root.COLUMN_ROOT_ID,
                DocumentsContract.Root.COLUMN_DOCUMENT_ID, DocumentsContract.Root.COLUMN_TITLE,
                DocumentsContract.Root.COLUMN_FLAGS});
        cursor.addRow(new Object[]{"fixture", "root", "SAF fault fixture", DocumentsContract.Root.FLAG_SUPPORTS_CREATE});
        return cursor;
    }
    @Override public Cursor queryDocument(String id, String[] projection) {
        MatrixCursor cursor = documents(); add(cursor, id); return cursor;
    }
    private MatrixCursor documents() {
        return new MatrixCursor(new String[]{DocumentsContract.Document.COLUMN_DOCUMENT_ID,
                DocumentsContract.Document.COLUMN_DISPLAY_NAME, DocumentsContract.Document.COLUMN_MIME_TYPE,
                DocumentsContract.Document.COLUMN_FLAGS, DocumentsContract.Document.COLUMN_SIZE});
    }
    private void add(MatrixCursor cursor, String id) {
        cursor.addRow(new Object[]{id, id.equals("root") ? "Fixture" : "payload.bin",
                id.equals("root") ? DocumentsContract.Document.MIME_TYPE_DIR : "application/octet-stream",
                DocumentsContract.Document.FLAG_SUPPORTS_WRITE | DocumentsContract.Document.FLAG_DIR_SUPPORTS_CREATE, 16384});
    }
    @Override public Cursor queryChildDocuments(String id, String[] projection, String sort) {
        MatrixCursor cursor = documents(); add(cursor, "payload"); return cursor;
    }
    @Override public ParcelFileDescriptor openDocument(String id, String access, CancellationSignal signal) throws FileNotFoundException {
        gate("open", signal);
        try {
            if (mode == PIPE) {
                ParcelFileDescriptor[] pipe = ParcelFileDescriptor.createPipe(); pipe[1].close(); return pipe[0];
            }
            File file = new File(getContext().getCacheDir(), "saf-fixture-payload");
            try (java.io.RandomAccessFile data = new java.io.RandomAccessFile(file, "rw")) { data.setLength(16384); }
            return ParcelFileDescriptor.open(file, ParcelFileDescriptor.parseMode(access));
        } catch (IOException failure) { throw new FileNotFoundException(failure.toString()); }
    }
    @Override public String createDocument(String parent, String mime, String name) throws FileNotFoundException {
        gate("create", null); return "payload";
    }
}

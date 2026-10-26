/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 * openi2pd: bounded manual-CI DocumentsProvider fault fixture.
 *
 * The catalog mirrors the multi-file layout the native engine opens by relative
 * path (`torrent/part-00001.bin`, `torrent/sub/part-00002.bin`, a zero-length
 * document) and every fault is an explicit mode, so a regression never depends
 * on a real provider. Documents live in this app's cache directory only; the
 * fixture never accepts a host path and its catalog is bounded.
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
import java.io.RandomAccessFile;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicInteger;

public final class TorrentSafFixtureProvider extends DocumentsProvider {
    /** Healthy provider: no injected fault. */
    static final int NORMAL = 0;
    /** Blocks inside openDocument and honours the CancellationSignal. */
    static final int COOPERATIVE = 1;
    /** Blocks inside openDocument and ignores the CancellationSignal. */
    static final int IGNORE_CANCELLATION = 2;
    /** Returns a non-seekable pipe instead of a document. */
    static final int PIPE = 3;
    /** Every operation fails as if the persisted grant had been revoked. */
    static final int REVOKED = 4;
    /** The first open succeeds and the document is gone immediately afterwards. */
    static final int DELETED_AFTER_OPEN = 5;
    /** Every open returns a new inode with the same length and other bytes. */
    static final int REPLACED_SAME_LENGTH = 6;
    /** Returns a sequential socket: lseek fails and fstat is not a regular file. */
    static final int NO_SEEK = 7;
    /** Returns a read-only descriptor for a write open, so pwrite fails. */
    static final int FAILS_WRITE = 8;
    /** The first open succeeds and every later operation reports a lost grant. */
    static final int REVOKE_AFTER_OPEN = 9;

    static final String AUTHORITY = "org.openi2pd.router.test.saf";
    static final String ROOT = "root";
    static final String TORRENT = "torrent";
    static final String SUBDIRECTORY = "torrent/sub";
    static final String PART_ONE = "torrent/part-00001.bin";
    static final String PART_TWO = "torrent/sub/part-00002.bin";
    static final String EMPTY = "torrent/empty.bin";
    // Kept for the pre-existing single-document cases.
    static final String LEGACY = "payload";
    static final long PART_ONE_SIZE = 16384, PART_TWO_SIZE = 12000;

    private static final int MAX_DOCUMENTS = 64;
    private static final String FIXTURE_DIRECTORY = "saf-fixture";
    private static final String OCTET_STREAM = "application/octet-stream";

    private record Entry(String id, String parent, String name, String mime, long size) {}

    private final Map<String, Entry> entries = new LinkedHashMap<>();
    private final Map<String, File> files = new LinkedHashMap<>();
    private volatile int mode;
    private volatile String blockedPhase = "open";
    private volatile CountDownLatch entered = new CountDownLatch(1),
                                    release = new CountDownLatch(1);
    private final AtomicInteger cancellations = new AtomicInteger();
    private int opened, sequence;

    void configure(int value, String phase) {
        release.countDown();
        mode = value;
        blockedPhase = phase;
        entered = new CountDownLatch(1);
        release = new CountDownLatch(1);
        cancellations.set(0);
        synchronized (this) {
            clearFiles();
            opened = 0;
            buildTree();
        }
    }

    boolean awaitEntered() throws InterruptedException {
        return entered.await(3, TimeUnit.SECONDS);
    }

    void unblock() {
        release.countDown();
    }

    int cancellations() {
        return cancellations.get();
    }

    @Override
    public boolean onCreate() {
        synchronized (this) {
            buildTree();
        }
        return true;
    }

    private void buildTree() {
        entries.clear();
        put(ROOT, null, "Fixture", DocumentsContract.Document.MIME_TYPE_DIR, 0);
        put(TORRENT, ROOT, "torrent", DocumentsContract.Document.MIME_TYPE_DIR, 0);
        put(SUBDIRECTORY, TORRENT, "sub", DocumentsContract.Document.MIME_TYPE_DIR, 0);
        put(PART_ONE, TORRENT, "part-00001.bin", OCTET_STREAM, PART_ONE_SIZE);
        put(PART_TWO, SUBDIRECTORY, "part-00002.bin", OCTET_STREAM, PART_TWO_SIZE);
        put(EMPTY, TORRENT, "empty.bin", OCTET_STREAM, 0);
        put(LEGACY, ROOT, "payload.bin", OCTET_STREAM, PART_ONE_SIZE);
    }

    private void put(String id, String parent, String name, String mime, long size) {
        entries.put(id, new Entry(id, parent, name, mime, size));
    }

    private void clearFiles() {
        for (File file : files.values())
            if (file != null)
                file.delete();
        files.clear();
    }

    private void gate(String phase, CancellationSignal signal) throws FileNotFoundException {
        if (mode == REVOKED)
            throw new FileNotFoundException("Fixture permission revoked");
        if (!phase.equals(blockedPhase) || mode != COOPERATIVE && mode != IGNORE_CANCELLATION)
            return;
        CountDownLatch gate = release;
        entered.countDown();
        if (signal != null)
            signal.setOnCancelListener(cancellations::incrementAndGet);
        long deadline = android.os.SystemClock.elapsedRealtime() + 60000;
        try {
            while (!gate.await(20, TimeUnit.MILLISECONDS)) {
                if (mode == COOPERATIVE && signal != null)
                    signal.throwIfCanceled();
                if (android.os.SystemClock.elapsedRealtime() >= deadline)
                    throw new FileNotFoundException("Fixture deadline");
            }
        } catch (InterruptedException stopped) {
            Thread.currentThread().interrupt();
            throw new FileNotFoundException("Fixture interrupted");
        } finally {
            if (signal != null)
                signal.setOnCancelListener(null);
        }
    }

    @Override
    public Cursor queryRoots(String[] projection) {
        MatrixCursor cursor = new MatrixCursor(new String[] {DocumentsContract.Root.COLUMN_ROOT_ID,
                DocumentsContract.Root.COLUMN_DOCUMENT_ID, DocumentsContract.Root.COLUMN_TITLE,
                DocumentsContract.Root.COLUMN_FLAGS});
        cursor.addRow(new Object[] {
                "fixture", ROOT, "SAF fault fixture", DocumentsContract.Root.FLAG_SUPPORTS_CREATE});
        return cursor;
    }

    @Override
    public Cursor queryDocument(String id, String[] projection) throws FileNotFoundException {
        Entry entry = served(id);
        MatrixCursor cursor = documents();
        add(cursor, entry);
        return cursor;
    }

    private MatrixCursor documents() {
        return new MatrixCursor(new String[] {DocumentsContract.Document.COLUMN_DOCUMENT_ID,
                DocumentsContract.Document.COLUMN_DISPLAY_NAME,
                DocumentsContract.Document.COLUMN_MIME_TYPE,
                DocumentsContract.Document.COLUMN_FLAGS, DocumentsContract.Document.COLUMN_SIZE});
    }

    private void add(MatrixCursor cursor, Entry entry) {
        boolean directory = DocumentsContract.Document.MIME_TYPE_DIR.equals(entry.mime());
        int flags = directory ? DocumentsContract.Document.FLAG_DIR_SUPPORTS_CREATE
                              : DocumentsContract.Document.FLAG_SUPPORTS_WRITE
                        | DocumentsContract.Document.FLAG_SUPPORTS_DELETE;
        cursor.addRow(new Object[] {entry.id(), entry.name(), entry.mime(), flags, entry.size()});
    }

    @Override
    public Cursor queryChildDocuments(String id, String[] projection, String sort)
            throws FileNotFoundException {
        synchronized (this) {
            Entry parent = served(id);
            if (!DocumentsContract.Document.MIME_TYPE_DIR.equals(parent.mime()))
                throw new FileNotFoundException("Fixture document is not a directory");
            List<Entry> children = new ArrayList<>();
            for (Entry entry : entries.values())
                if (id.equals(entry.parent()))
                    children.add(entry);
            MatrixCursor cursor = documents();
            for (Entry child : children)
                add(cursor, child);
            return cursor;
        }
    }

    @Override
    public ParcelFileDescriptor openDocument(String id, String access, CancellationSignal signal)
            throws FileNotFoundException {
        gate("open", signal);
        int configured = mode;
        Entry entry;
        synchronized (this) {
            entry = served(id);
            if (DocumentsContract.Document.MIME_TYPE_DIR.equals(entry.mime()))
                throw new FileNotFoundException("Fixture directory is not a document");
            opened++;
        }
        try {
            switch (configured) {
            case PIPE: {
                ParcelFileDescriptor[] pipe = ParcelFileDescriptor.createPipe();
                pipe[1].close();
                return pipe[0];
            }
            case NO_SEEK: {
                // Bidirectional but non-seekable: lseek fails with ESPIPE and
                // fstat is not a regular file, so the broker rejects the
                // document before any transfer or temporary copy.
                ParcelFileDescriptor[] pair = ParcelFileDescriptor.createSocketPair();
                closeQuietly(pair[1]);
                return pair[0];
            }
            case FAILS_WRITE:
                // A read-only descriptor for a write open is the
                // deterministic, signal-free stand-in for a provider
                // ENOSPC: pwrite and fsync fail instead of a partial write
                // being reported as success.
                return ParcelFileDescriptor.open(
                        document(entry), ParcelFileDescriptor.MODE_READ_ONLY);
            case REPLACED_SAME_LENGTH:
                return ParcelFileDescriptor.open(
                        replace(entry), ParcelFileDescriptor.parseMode(access));
            default: {
                ParcelFileDescriptor fd = ParcelFileDescriptor.open(
                        document(entry), ParcelFileDescriptor.parseMode(access));
                if (configured == DELETED_AFTER_OPEN)
                    // The descriptor still names the old inode, exactly as a
                    // provider that removed the document after the open.
                    discard(entry);
                return fd;
            }
            }
        } catch (IOException failure) {
            throw new FileNotFoundException(failure.toString());
        }
    }

    @Override
    public String createDocument(String parent, String mime, String name)
            throws FileNotFoundException {
        gate("create", null);
        boolean directory = DocumentsContract.Document.MIME_TYPE_DIR.equals(mime);
        synchronized (this) {
            Entry parentEntry = served(parent);
            if (!DocumentsContract.Document.MIME_TYPE_DIR.equals(parentEntry.mime()) || name == null
                    || name.isEmpty() || name.length() > 255 || name.indexOf('/') >= 0
                    || name.equals(".") || name.equals(".."))
                throw new FileNotFoundException("Fixture refuses the display name");
            String id = parent + "/" + name;
            if (entries.containsKey(id))
                return id;
            if (entries.size() >= MAX_DOCUMENTS)
                throw new FileNotFoundException("Fixture catalog is full");
            put(id, parent, name,
                    directory ? DocumentsContract.Document.MIME_TYPE_DIR : OCTET_STREAM, 0);
            return id;
        }
    }

    @Override
    public void deleteDocument(String id) throws FileNotFoundException {
        gate("delete", null);
        synchronized (this) {
            if (revoked() || entries.remove(id) == null)
                throw new FileNotFoundException("Fixture document is gone: " + id);
            File file = files.remove(id);
            if (file != null)
                file.delete();
        }
    }

    private synchronized Entry served(String id) throws FileNotFoundException {
        if (revoked())
            throw new FileNotFoundException("Fixture permission revoked");
        Entry entry = entries.get(id);
        if (entry == null)
            throw new FileNotFoundException("Fixture document is gone: " + id);
        return entry;
    }

    private synchronized boolean revoked() {
        if (mode == REVOKED)
            return true;
        return opened > 0 && mode == REVOKE_AFTER_OPEN;
    }

    private synchronized File document(Entry entry) throws IOException {
        File file = files.get(entry.id());
        if (file == null) {
            file = allocate();
            files.put(entry.id(), file);
        }
        if (!file.exists() || file.length() != entry.size())
            try (RandomAccessFile data = new RandomAccessFile(file, "rw")) {
                data.setLength(entry.size());
                pattern(data, entry.size());
            }
        return file;
    }

    private synchronized File replace(Entry entry) throws IOException {
        File previous = files.get(entry.id());
        File fresh = allocate();
        try (RandomAccessFile data = new RandomAccessFile(fresh, "rw")) {
            data.setLength(entry.size());
            pattern(data, entry.size());
        }
        if (previous != null && previous.exists() && !previous.delete())
            throw new IOException("Fixture replacement refused");
        files.put(entry.id(), fresh);
        return fresh;
    }

    private void discard(Entry entry) {
        synchronized (this) {
            entries.remove(entry.id());
            File file = files.remove(entry.id());
            if (file != null)
                file.delete();
        }
    }

    private File allocate() throws IOException {
        File directory = new File(getContext().getCacheDir(), FIXTURE_DIRECTORY);
        if (!directory.isDirectory() && !directory.mkdirs())
            throw new IOException("Fixture directory is unavailable");
        return new File(directory, "doc-" + (sequence++));
    }

    /** Bounded fixture pattern; consecutive inodes carry different bytes. */
    private void pattern(RandomAccessFile data, long size) throws IOException {
        if (size <= 0)
            return;
        byte[] block = new byte[(int) Math.min(size, 4096)];
        Arrays.fill(block, (byte) (0x40 + (sequence & 0x1f)));
        data.seek(0);
        data.write(block);
    }

    private static void closeQuietly(ParcelFileDescriptor fd) {
        if (fd == null)
            return;
        try {
            fd.close();
        } catch (IOException ignored) {
        }
    }
}

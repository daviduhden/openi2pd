/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: capability-based SAF payload broker.
 * Owns document descriptors in a separate process; never exports raw FDs.
 */
package org.openi2pd.router;

import android.app.Service;
import android.content.Intent;
import android.content.UriPermission;
import android.database.Cursor;
import android.net.Uri;
import android.os.Binder;
import android.os.CancellationSignal;
import java.util.concurrent.TimeUnit;
import android.os.IBinder;
import android.os.ParcelFileDescriptor;
import android.provider.DocumentsContract;
import android.system.Os;
import android.system.OsConstants;
import java.io.IOException;
import java.util.HashMap;
import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

public final class TorrentSafStorageService extends Service {
    private static final int MAX_HANDLES = 4096;
    private static final long MAX_FILE = 1L << 40;
    private record Root(Uri uri, boolean writable) {}
    private record File(long root, Uri uri, ParcelFileDescriptor fd, long length, boolean writable,
            String documentId) {}
    // Data work may block in a remote provider. Control transactions never take ioLock.
    private final Object ioLock = new Object(), operationLock = new Object();
    private final Map<Long, CancellationSignal> operations = new HashMap<>();
    private final java.util.concurrent.ScheduledThreadPoolExecutor watchdog =
            new java.util.concurrent.ScheduledThreadPoolExecutor(1);
    private final Map<Long, java.util.concurrent.ScheduledFuture<?>> deadlines = new HashMap<>();
    private volatile boolean retiring;
    private CancellationSignal begin(long token) {
        synchronized (operationLock) {
            if (retiring || token <= 0 || operations.size() >= 8 || operations.containsKey(token))
                throw new IllegalStateException("Invalid storage operation");
            CancellationSignal signal = new CancellationSignal();
            operations.put(token, signal);
            // Independent hard deadline also covers providers that ignore cancellation and blocking POSIX IO.
            deadlines.put(token, watchdog.schedule(() -> {
                synchronized (operationLock) {
                    if (operations.get(token) != signal)
                        return;
                    retiring = true;
                }
                android.os.Process.killProcess(android.os.Process.myPid());
            }, 15, TimeUnit.SECONDS));
            return signal;
        }
    }
    private void end(long token) {
        synchronized (operationLock) {
            operations.remove(token);
            java.util.concurrent.ScheduledFuture<?> deadline = deadlines.remove(token);
            if (deadline != null)
                deadline.cancel(false);
        }
    }
    private void retireProcess() {
        retiring = true;
        // Death, rather than closing an FD from another thread, fences every old writer.
        android.os.Process.killProcess(android.os.Process.myPid());
    }
    private final Map<Long, Root> roots = new HashMap<>();
    private final Map<Long, File> files = new ConcurrentHashMap<>();
    private final ExecutorService reaper = Executors.newSingleThreadExecutor(runnable -> {
        Thread thread = new Thread(runnable, "saf-close-reaper");
        thread.setDaemon(true);
        return thread;
    });
    private final java.security.SecureRandom random = new java.security.SecureRandom();
    private void caller() {
        if (Binder.getCallingUid() != android.os.Process.myUid())
            throw new SecurityException("Foreign storage caller");
    }
    private long id() {
        long value;
        do {
            value = random.nextLong() & Long.MAX_VALUE;
        } while (value == 0 || roots.containsKey(value) || files.containsKey(value));
        return value;
    }
    private final ITorrentSafStorage.Stub binder = new ITorrentSafStorage.Stub() {
        @Override
        public long openExport(String text, long length, long operation) {
            caller();
            CancellationSignal signal = begin(operation);
            try {
                synchronized (ioLock) {
                    if (text == null || text.length() > 8192 || length <= 0
                            || length > 8 * 1024 * 1024 || files.size() >= MAX_HANDLES)
                        return 0;
                    Uri uri = Uri.parse(text);
                    if (!"content".equals(uri.getScheme()) || !documentGrant(uri, true))
                        return 0;
                    ParcelFileDescriptor fd = null;
                    try {
                        fd = getContentResolver().openFileDescriptor(uri, "rw", signal);
                        if (fd == null
                                || !OsConstants.S_ISREG(Os.fstat(fd.getFileDescriptor()).st_mode))
                            return 0;
                        Os.lseek(fd.getFileDescriptor(), 0, OsConstants.SEEK_SET);
                        // Start empty rather than pre-sizing: a failed first
                        // write must not leave a full-length zero-filled file.
                        Os.ftruncate(fd.getFileDescriptor(), 0);
                        signal.throwIfCanceled();
                        String documentId = currentDocumentId(uri, signal);
                        long key = id();
                        files.put(key, new File(0, uri, fd, length, true, documentId));
                        fd = null;
                        return key;
                    } catch (Exception failure) {
                        return 0;
                    } finally {
                        if (fd != null)
                            try {
                                fd.close();
                            } catch (IOException ignored) {
                            }
                    }
                }
            } finally {
                end(operation);
            }
        }
        @Override
        public boolean finishExport(long handle, boolean success, long operation) {
            caller();
            CancellationSignal signal = begin(operation);
            try {
                synchronized (ioLock) {
                    File file = files.get(handle);
                    if (file == null || file.root != 0)
                        return false;
                    boolean ok = false;
                    try {
                        signal.throwIfCanceled();
                        if (success) {
                            if (!authorized(file, true, signal))
                                throw new IOException("Export target changed");
                            ok = Os.fstat(file.fd.getFileDescriptor()).st_size == file.length;
                            if (!ok)
                                Os.ftruncate(file.fd.getFileDescriptor(), 0);
                        } else {
                            Os.ftruncate(file.fd.getFileDescriptor(), 0);
                            ok = true;
                        }
                    } catch (Exception failure) {
                        try {
                            Os.ftruncate(file.fd.getFileDescriptor(), 0);
                        } catch (Exception ignored) {
                        }
                    } finally {
                        files.remove(handle);
                        try {
                            file.fd.close();
                        } catch (IOException ignored) {
                        }
                    }
                    return ok;
                }
            } finally {
                end(operation);
            }
        }
        @Override
        public long authorizeTree(String text, long operation) {
            caller();
            CancellationSignal signal = begin(operation);
            try {
                synchronized (ioLock) {
                    signal.throwIfCanceled();
                    if (text == null || text.length() > 8192 || roots.size() >= 128)
                        return 0;
                    Uri uri = Uri.parse(text);
                    if (!"content".equals(uri.getScheme()) || !DocumentsContract.isTreeUri(uri))
                        return 0;
                    for (UriPermission permission :
                            getContentResolver().getPersistedUriPermissions()) {
                        if (permission.getUri().equals(uri) && permission.isReadPermission()) {
                            long key = id();
                            roots.put(key, new Root(uri, permission.isWritePermission()));
                            return key;
                        }
                    }
                    return 0;
                }
            } finally {
                end(operation);
            }
        }
        @Override
        public long openFile(
                long rootId, String relative, long length, boolean create, long operation) {
            caller();
            CancellationSignal signal = begin(operation);
            try {
                synchronized (ioLock) {
                    signal.throwIfCanceled();
                    Root root = roots.get(rootId);
                    if (root == null || create && !root.writable || files.size() >= MAX_HANDLES
                            || length < 0 || length > MAX_FILE || !validPath(relative))
                        return 0;
                    ParcelFileDescriptor fd = null;
                    try {
                        Uri doc = DocumentsContract.buildDocumentUriUsingTree(
                                root.uri, DocumentsContract.getTreeDocumentId(root.uri));
                        String[] parts = relative.split("/", -1);
                        for (int i = 0; i < parts.length; i++) {
                            boolean directory = i + 1 < parts.length;
                            Uri child = find(root.uri, doc, parts[i], directory, signal);
                            signal.throwIfCanceled();
                            if (child == null && create) {
                                child = DocumentsContract.createDocument(getContentResolver(), doc,
                                        directory ? DocumentsContract.Document.MIME_TYPE_DIR
                                                  : "application/octet-stream",
                                        parts[i]);
                                if (child != null) {
                                    // Providers can return a different name or
                                    // type than requested. Validate every created
                                    // component before using it as a parent.
                                    try (Cursor cursor = getContentResolver().query(child,
                                                 new String[] {DocumentsContract.Document
                                                                       .COLUMN_DISPLAY_NAME,
                                                         DocumentsContract.Document
                                                                 .COLUMN_MIME_TYPE},
                                                 null, null, null, signal)) {
                                        boolean matches = cursor != null && cursor.moveToFirst()
                                                && parts[i].equals(cursor.getString(0))
                                                && (directory
                                                        == DocumentsContract.Document.MIME_TYPE_DIR
                                                                .equals(cursor.getString(1)));
                                        if (!matches) {
                                            try {
                                                DocumentsContract.deleteDocument(
                                                        getContentResolver(), child);
                                            } catch (Exception ignored) {
                                            }
                                            return 0;
                                        }
                                    }
                                }
                            }
                            if (child == null)
                                return 0;
                            doc = child;
                        }
                        fd = getContentResolver().openFileDescriptor(
                                doc, create ? "rw" : "r", signal);
                        if (fd == null)
                            return 0;
                        // Reject pipes and providers without random access. No unbounded staging fallback.
                        Os.lseek(fd.getFileDescriptor(), 0, OsConstants.SEEK_SET);
                        android.system.StructStat stat = Os.fstat(fd.getFileDescriptor());
                        if (!OsConstants.S_ISREG(stat.st_mode))
                            throw new IOException("Document is not a regular seekable file");
                        long actual = stat.st_size;
                        if (!create && actual != length)
                            throw new IOException("Document size changed");
                        if (create && actual > length)
                            throw new IOException("Document exceeds declared length");
                        if (TorrentSafPolicy.canExtendExisting(create, actual, length)) {
                            // A write-mode open may extend an existing shorter
                            // document. Existing bytes remain unverified and are
                            // rehashed before availability is published.
                            Os.ftruncate(fd.getFileDescriptor(), length);
                        }
                        signal.throwIfCanceled();
                        long key = id();
                        files.put(key,
                                new File(rootId, doc, fd, length, create,
                                        currentDocumentId(doc, signal)));
                        fd = null;
                        return key;
                    } catch (Exception failure) {
                        return 0;
                    } finally {
                        if (fd != null)
                            try {
                                fd.close();
                            } catch (IOException ignored) {
                            }
                    }
                }
            } finally {
                end(operation);
            }
        }
        @Override
        public byte[] read(long handle, long offset, int count, long operation) {
            caller();
            CancellationSignal signal = begin(operation);
            try {
                synchronized (ioLock) {
                    signal.throwIfCanceled();
                    File file = files.get(handle);
                    if (!range(file, offset, count) || !authorized(file, false, signal))
                        return null;
                    byte[] result = new byte[count];
                    try {
                        int done = 0;
                        while (done < count) {
                            signal.throwIfCanceled();
                            int n = Os.pread(file.fd.getFileDescriptor(), result, done,
                                    count - done, offset + done);
                            if (n <= 0)
                                return null;
                            done += n;
                        }
                        return result;
                    } catch (Exception failure) {
                        closeFile(handle);
                        return null;
                    }
                }
            } finally {
                end(operation);
            }
        }
        @Override
        public boolean write(long handle, long offset, byte[] bytes, long operation) {
            caller();
            CancellationSignal signal = begin(operation);
            try {
                synchronized (ioLock) {
                    signal.throwIfCanceled();
                    File file = files.get(handle);
                    if (bytes == null || !range(file, offset, bytes.length) || !file.writable
                            || !authorized(file, true, signal))
                        return false;
                    try {
                        int done = 0;
                        while (done < bytes.length) {
                            signal.throwIfCanceled();
                            int n = Os.pwrite(file.fd.getFileDescriptor(), bytes, done,
                                    bytes.length - done, offset + done);
                            if (n <= 0)
                                return false;
                            done += n;
                        }
                        Os.fsync(file.fd.getFileDescriptor());
                        return true;
                    } catch (Exception failure) {
                        closeFile(handle);
                        return false;
                    }
                }
            } finally {
                end(operation);
            }
        }
        @Override
        public boolean deleteFile(long handle, long operation) {
            caller();
            CancellationSignal signal = begin(operation);
            try {
                synchronized (ioLock) {
                    signal.throwIfCanceled();
                    File file = files.get(handle);
                    // A read-only descriptor may authorize deletion only when the
                    // persisted root grant also grants write. Never delete a tree.
                    if (file == null || file.root == 0 || !authorized(file, true, signal))
                        return false;
                    try {
                        // Reject directories even if a provider changes document type.
                        try (Cursor cursor = getContentResolver().query(file.uri,
                                     new String[] {DocumentsContract.Document.COLUMN_MIME_TYPE},
                                     null, null, null, signal)) {
                            if (cursor == null || !cursor.moveToFirst()
                                    || DocumentsContract.Document.MIME_TYPE_DIR.equals(
                                            cursor.getString(0)))
                                return false;
                        }
                        // A provider may replace an existing document ID. Compare
                        // a freshly opened descriptor with the registered object.
                        try (ParcelFileDescriptor current = getContentResolver().openFileDescriptor(
                                     file.uri, "r", signal)) {
                            if (current == null)
                                return false;
                            var original = Os.fstat(file.fd.getFileDescriptor());
                            var now = Os.fstat(current.getFileDescriptor());
                            if (!sameDocument(original, now, file.length))
                                return false;
                        }
                        signal.throwIfCanceled();
                        boolean removed =
                                DocumentsContract.deleteDocument(getContentResolver(), file.uri);
                        if (removed) {
                            // Invalidate aliases to the same document before later IO.
                            for (long key : new java.util.ArrayList<>(files.keySet())) {
                                File alias = files.get(key);
                                if (alias != null && alias.uri.equals(file.uri))
                                    closeFile(key);
                            }
                        }
                        return removed;
                    } catch (Exception failure) {
                        return false;
                    }
                }
            } finally {
                end(operation);
            }
        }
        @Override
        public void closeFile(long handle) {
            caller();
            File file = files.remove(handle);
            if (file == null)
                return;
            // Return promptly to the client; close after any in-flight data
            // operation releases ioLock. Closing one handle must not block the
            // broker's control plane or trigger a process-wide retirement.
            reaper.execute(() -> {
                synchronized (ioLock) {
                    try {
                        file.fd.close();
                    } catch (IOException ignored) {
                    }
                }
            });
        }
        @Override
        public void revokeTree(long root) {
            caller();
            synchronized (ioLock) {
                roots.remove(root);
                for (long key : new java.util.ArrayList<>(files.keySet())) {
                    File file = files.get(key);
                    if (file != null && file.root == root)
                        closeFile(key);
                }
            }
        }
        @Override
        public void cancel(long operation) {
            caller();
            // Cancellation is scoped to the operation, not the broker process.
            // A slow export or close must not kill every torrent's descriptors.
            CancellationSignal signal;
            synchronized (operationLock) {
                signal = operations.get(operation);
            }
            if (signal != null) {
                Thread cancellation = new Thread(signal::cancel, "saf-provider-cancel");
                cancellation.setDaemon(true);
                cancellation.start();
            }
        }
        @Override
        public void retire() {
            caller();
            retireProcess();
        }
    };
    static boolean validPath(String path) {
        // Display names may legally contain ':' and '\\' on Android; they are
        // not POSIX path separators here and must not be treated as traversal.
        if (path == null || path.isEmpty() || path.length() > 8192)
            return false;
        String[] parts = path.split("/", -1);
        if (parts.length > 32)
            return false;
        for (String part : parts) {
            if (part.isEmpty() || part.equals(".") || part.equals("..") || part.length() > 255)
                return false;
            for (int i = 0; i < part.length(); i++)
                if (Character.isISOControl(part.charAt(i)))
                    return false;
        }
        return true;
    }
    private static boolean range(File file, long offset, int count) {
        return file != null && validRange(file.length, offset, count);
    }
    static boolean validRange(long length, long offset, int count) {
        return TorrentSafPolicy.validRange(length, offset, count);
    }
    private boolean authorized(File file, boolean write, CancellationSignal signal) {
        Root root = roots.get(file.root);
        if (file.root == 0) {
            try {
                return documentGrant(file.uri, true)
                        && ((file.documentId == null || file.documentId.isEmpty())
                                || documentIdentity(file, signal))
                        && TorrentSafPolicy.exportSizeAllowed(
                                file.length, Os.fstat(file.fd.getFileDescriptor()).st_size);
            } catch (Exception failure) {
                return false;
            }
        }
        if (root == null)
            return false;
        boolean granted = false;
        for (UriPermission grant : getContentResolver().getPersistedUriPermissions())
            if (grant.getUri().equals(root.uri) && grant.isReadPermission()
                    && (!write || grant.isWritePermission()))
                granted = true;
        if (!granted)
            return false;
        try {
            return documentIdentity(file, signal)
                    && Os.fstat(file.fd.getFileDescriptor()).st_size == file.length;
        } catch (Exception failure) {
            return false;
        }
    }
    private boolean documentIdentity(File file, CancellationSignal signal) {
        try (Cursor cursor = getContentResolver().query(file.uri,
                     new String[] {DocumentsContract.Document.COLUMN_DOCUMENT_ID}, null, null, null,
                     signal)) {
            return cursor != null && cursor.moveToFirst()
                    && TorrentSafPolicy.documentIdentityMatches(
                            file.documentId, cursor.getString(0));
        } catch (Exception failure) {
            return false;
        }
    }
    private String currentDocumentId(Uri uri, CancellationSignal signal) {
        try (Cursor cursor = getContentResolver().query(uri,
                     new String[] {DocumentsContract.Document.COLUMN_DOCUMENT_ID}, null, null, null,
                     signal)) {
            if (cursor != null && cursor.moveToFirst())
                return cursor.getString(0);
        } catch (Exception ignored) {
        }
        try {
            return DocumentsContract.getDocumentId(uri);
        } catch (Exception ignored) {
            return "";
        }
    }
    private Uri find(Uri tree, Uri parent, String name, boolean directory,
            CancellationSignal signal) throws IOException {
        Uri children = DocumentsContract.buildChildDocumentsUriUsingTree(
                tree, DocumentsContract.getDocumentId(parent));
        String[] columns = {DocumentsContract.Document.COLUMN_DOCUMENT_ID,
                DocumentsContract.Document.COLUMN_DISPLAY_NAME,
                DocumentsContract.Document.COLUMN_MIME_TYPE};
        try (Cursor cursor =
                        getContentResolver().query(children, columns, null, null, null, signal)) {
            if (cursor == null)
                throw new IOException("Document listing unavailable");
            int visited = 0;
            Uri found = null;
            while (cursor.moveToNext()) {
                signal.throwIfCanceled();
                if (++visited > 20000)
                    throw new IOException("Directory listing limit");
                if (!name.equals(cursor.getString(1)))
                    continue;
                if (found != null
                        || directory
                                != DocumentsContract.Document.MIME_TYPE_DIR.equals(
                                        cursor.getString(2)))
                    throw new IOException("Ambiguous document");
                found = DocumentsContract.buildDocumentUriUsingTree(tree, cursor.getString(0));
            }
            return found;
        }
    }
    static boolean sameDocument(
            android.system.StructStat opened, android.system.StructStat current, long length) {
        return opened.st_dev == current.st_dev && opened.st_ino == current.st_ino
                && current.st_size == length && opened.st_size == length
                && OsConstants.S_ISREG(opened.st_mode) && OsConstants.S_ISREG(current.st_mode);
    }
    private boolean documentGrant(Uri uri, boolean write) {
        return checkUriPermission(uri, android.os.Process.myPid(), android.os.Process.myUid(),
                       write ? Intent.FLAG_GRANT_WRITE_URI_PERMISSION
                             : Intent.FLAG_GRANT_READ_URI_PERMISSION)
                == android.content.pm.PackageManager.PERMISSION_GRANTED;
    }
    private boolean restricted;
    @Override
    public void onCreate() {
        super.onCreate();
        watchdog.setRemoveOnCancelPolicy(true);
        restricted = I2PD_JNI.tryLoadLibraries() && I2PD_JNI.restrictSafStorageProcess();
        if (!restricted)
            stopSelf();
    }
    @Override
    public IBinder onBind(Intent intent) {
        return restricted ? binder : null;
    }
    @Override
    public boolean onUnbind(Intent intent) {
        retireProcess();
        return false;
    }
    @Override
    public void onDestroy() {
        reaper.shutdownNow();
        retireProcess();
        super.onDestroy();
    }
}

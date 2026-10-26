/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: capability-based SAF payload broker (protocol v2).
 *
 * Owns document descriptors in a separate process and never exports raw FDs.
 * Authorization is the application UID plus the exact (uid, pid) owner recorded
 * on every root, handle, operation and export token. Admission is bounded and
 * answers with a typed failure instead of throwing across Binder. Data
 * operations escalate in two stages: cancel the provider call first, retire the
 * process only when the same operation is still registered afterwards.
 */
package org.openi2pd.router;

import android.app.Service;
import android.content.Intent;
import android.content.UriPermission;
import android.database.Cursor;
import android.net.Uri;
import android.os.Binder;
import android.os.CancellationSignal;
import android.os.IBinder;
import android.os.ParcelFileDescriptor;
import android.os.SystemClock;
import android.provider.DocumentsContract;
import android.system.Os;
import android.system.OsConstants;
import android.system.StructStat;
import java.io.IOException;
import java.security.SecureRandom;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.RejectedExecutionException;
import java.util.concurrent.ScheduledFuture;
import java.util.concurrent.ScheduledThreadPoolExecutor;
import java.util.concurrent.ThreadFactory;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicBoolean;

public final class TorrentSafStorageService extends Service {
    /** Wire contract version reported by {@link ITorrentSafStorage#apiVersion()}. */
    static final int API_VERSION = 2;
    // Admission bounds. An over-limit request returns its typed failure and
    // never retires the broker process.
    static final int MAX_OPERATIONS = 8;
    static final int MAX_HANDLES = 4096;
    static final int MAX_ROOTS = 128;
    static final int MAX_EXPORTS = 8;
    static final long MAX_FILE = 1L << 40;
    static final long MAX_EXPORT = 8L * 1024 * 1024;
    static final long MAX_TEXT = 8192;
    private static final int MAX_PARTS = 32;
    private static final int MAX_PART = 255;
    private static final int MAX_CHILDREN = 20000;
    /** One-shot lifetime of a prepared export capability. */
    static final long EXPORT_TTL_MS = 30_000;
    /** First watchdog stage: abort the provider call, keep every other handle. */
    static final long CANCEL_DEADLINE_MS = 15_000;
    /** Second stage: fence only a provider that ignored cancellation. */
    static final long KILL_DEADLINE_MS = 30_000;

    /** Exact process identity of one Binder caller. */
    record Caller(int uid, int pid) {}

    private record Root(Uri uri, boolean writable, Caller owner) {}

    private record File(long root, Uri uri, ParcelFileDescriptor fd, long length, boolean writable,
            String documentId, Caller owner) {}

    /**
     * Broker process identity. The generation is a non-zero CSPRNG value chosen
     * at service creation; retirement is a one-shot latch so a duplicated,
     * stale or replayed retire(generation) cannot fence a replacement broker.
     * Framework-free so JVM tests can pin the contract.
     */
    static final class BrokerIdentity {
        private final long generation;
        private final AtomicBoolean retired = new AtomicBoolean();

        BrokerIdentity(long candidate) {
            long positive = candidate & Long.MAX_VALUE;
            generation = positive == 0 ? 1 : positive;
        }

        long generation() {
            return generation;
        }

        boolean retired() {
            return retired.get();
        }

        /** One-shot process fence used by the internal watchdog escalation. */
        boolean firstRetirement() {
            return retired.compareAndSet(false, true);
        }

        /** One-shot retirement requested by a client for this exact generation. */
        boolean claimRetirement(long requested) {
            return requested > 0 && requested == generation && firstRetirement();
        }
    }

    /**
     * Bounded, framework-free operation registry shared with JVM tests. It
     * performs no synchronization of its own: every caller holds the broker's
     * operation lock.
     */
    static final class Admission<T> {
        record Slot<T>(long operation, Caller owner, T value) {}

        private final Map<Long, Slot<T>> slots = new HashMap<>();

        Slot<T> admit(long operation, Caller owner, T value) {
            if (owner == null || operation <= 0 || slots.size() >= MAX_OPERATIONS
                    || slots.containsKey(operation))
                return null;
            Slot<T> slot = new Slot<>(operation, owner, value);
            slots.put(operation, slot);
            return slot;
        }

        Slot<T> find(long operation) {
            return slots.get(operation);
        }

        Slot<T> release(long operation) {
            return slots.remove(operation);
        }

        boolean registered(long operation, T value) {
            Slot<T> slot = slots.get(operation);
            return slot != null && slot.value() == value;
        }

        int size() {
            return slots.size();
        }

        void clear() {
            slots.clear();
        }
    }

    /**
     * One-shot export capabilities. A token authorizes exactly one truncating
     * open of exactly one document identity by exactly one caller. Guarded by
     * the broker's io lock; it performs no synchronization of its own.
     */
    static final class ExportGrants {
        record Grant(Caller owner, String uri, long length, String documentId, long expiresAt) {}

        private final Map<Long, Grant> grants = new HashMap<>();

        boolean prepare(long token, Caller owner, String uri, long length, String documentId,
                long now) {
            prune(now);
            if (owner == null || token <= 0 || uri == null || uri.isEmpty()
                    || uri.length() > MAX_TEXT || length <= 0 || length > MAX_EXPORT
                    || documentId == null || documentId.isEmpty() || grants.size() >= MAX_EXPORTS
                    || grants.containsKey(token))
                return false;
            grants.put(token, new Grant(owner, uri, length, documentId, now + EXPORT_TTL_MS));
            return true;
        }

        /**
         * Consumes one token. Returns null for an unknown, expired or foreign
         * token; a foreign presentation does not burn the owner's capability.
         */
        Grant consume(long token, Caller caller, long now) {
            prune(now);
            Grant grant = grants.get(token);
            if (grant == null || caller == null || !grant.owner().equals(caller))
                return null;
            grants.remove(token);
            return grant;
        }

        boolean contains(long token) {
            return grants.containsKey(token);
        }

        int size() {
            return grants.size();
        }

        void clear() {
            grants.clear();
        }

        private void prune(long now) {
            grants.values().removeIf(grant -> grant.expiresAt() <= now);
        }
    }

    /** One admitted operation and its two watchdog stages. */
    private static final class Operation {
        final long token;
        final Caller owner;
        final CancellationSignal signal = new CancellationSignal();
        ScheduledFuture<?> cancelDeadline;
        ScheduledFuture<?> killDeadline;

        Operation(long token, Caller owner) {
            this.token = token;
            this.owner = owner;
        }
    }

    private static final ThreadFactory WATCHDOG_THREADS = runnable -> {
        Thread thread = new Thread(runnable, "saf-watchdog");
        thread.setDaemon(true);
        return thread;
    };

    // Lock order: ioLock may take operationLock; nothing takes ioLock while
    // holding operationLock. Control transactions never take ioLock.
    private final Object ioLock = new Object(), operationLock = new Object();
    private final SecureRandom random = new SecureRandom();
    private final BrokerIdentity identity = new BrokerIdentity(random.nextLong());
    private final Admission<Operation> admission = new Admission<>();
    // Control-table state below is guarded by ioLock.
    private final ExportGrants exports = new ExportGrants();
    private final Map<Long, Root> roots = new HashMap<>();
    private final Map<Long, File> files = new ConcurrentHashMap<>();
    // Two watchdog threads so a provider listener that blocks inside cancel()
    // cannot delay the second stage that fences the process.
    private final ScheduledThreadPoolExecutor watchdog =
            new ScheduledThreadPoolExecutor(2, WATCHDOG_THREADS);
    private final ExecutorService reaper = Executors.newSingleThreadExecutor(runnable -> {
        Thread thread = new Thread(runnable, "saf-close-reaper");
        thread.setDaemon(true);
        return thread;
    });
    private volatile Caller primaryCaller;

    static boolean sameOwner(Caller owner, Caller caller) {
        return owner != null && owner.equals(caller);
    }

    /** One-shot latch plus process death; death fences every old writer. */
    private void retireProcess() {
        identity.firstRetirement();
        android.os.Process.killProcess(android.os.Process.myPid());
    }

    /**
     * Registers one bounded operation. Returns null when the broker is already
     * retiring, the token is invalid or duplicated, or every slot is taken;
     * the caller then answers with its typed failure.
     *
     * <p>The two provider deadlines are deliberately NOT armed here. An
     * operation that is still queued behind another one has not touched any
     * document, so charging it would let a single stalled provider abort or
     * fence every later operation. {@link #armWatchdog} starts the deadlines
     * once this operation owns {@code ioLock}, immediately before it can query,
     * open, read, write, sync or delete.
     */
    private Operation begin(long token, Caller owner) {
        synchronized (operationLock) {
            if (identity.retired())
                return null;
            Operation operation = new Operation(token, owner);
            if (admission.admit(token, owner, operation) == null)
                return null;
            return operation;
        }
    }

    /**
     * A provider deadline may only be charged once the operation owns the
     * provider lock. Framework-free so the ordering contract is testable.
     */
    static boolean providerDeadlineApplies(boolean holdsProviderLock, boolean registered) {
        return holdsProviderLock && registered;
    }

    /**
     * Arms the 15 s cancel and 30 s fence stages for one admitted operation.
     * Called under {@code ioLock}, i.e. only when the operation is about to hold
     * the provider; returns false when the broker is retiring or the operation
     * is no longer registered, and the caller then answers with its typed
     * fallback instead of entering a provider call.
     */
    private boolean armWatchdog(Operation operation) {
        synchronized (operationLock) {
            if (operation.cancelDeadline != null || operation.killDeadline != null)
                return true;
            // Reached only from inside `synchronized (ioLock)`, so the provider
            // lock is held by construction; the predicate keeps that ordering an
            // explicit, testable contract instead of an implicit convention.
            Admission.Slot<Operation> slot = admission.find(operation.token);
            if (!providerDeadlineApplies(true, slot != null && slot.value() == operation))
                return false;
            if (identity.retired())
                return false;
            try {
                operation.cancelDeadline = watchdog.schedule(
                        () -> escalateCancel(operation), CANCEL_DEADLINE_MS, TimeUnit.MILLISECONDS);
                operation.killDeadline = watchdog.schedule(
                        () -> escalateRetire(operation), KILL_DEADLINE_MS, TimeUnit.MILLISECONDS);
            } catch (RejectedExecutionException gone) {
                return false;
            }
            return true;
        }
    }

    /** Cancels both watchdog stages; a completed operation is never escalated. */
    private void end(Operation operation) {
        if (operation == null)
            return;
        synchronized (operationLock) {
            admission.release(operation.token);
            if (operation.cancelDeadline != null)
                operation.cancelDeadline.cancel(false);
            if (operation.killDeadline != null)
                operation.killDeadline.cancel(false);
        }
    }

    /**
     * First stage: abort the provider call. This never retires the broker, so a
     * single slow document cannot drop every unrelated torrent descriptor.
     */
    private void escalateCancel(Operation operation) {
        try {
            operation.signal.cancel();
        } catch (RuntimeException ignored) {
        }
    }

    /**
     * Second stage: fence the process only when the same operation is still
     * registered 15 s after cancellation, because a provider that ignored
     * cancellation may still be writing the descriptor.
     */
    private void escalateRetire(Operation operation) {
        synchronized (operationLock) {
            if (!admission.registered(operation.token, operation))
                return;
            admission.release(operation.token);
        }
        if (identity.firstRetirement())
            retireProcess();
    }

    private Caller caller() {
        int uid = Binder.getCallingUid();
        if (uid != android.os.Process.myUid())
            throw new SecurityException("Foreign storage caller");
        return new Caller(uid, Binder.getCallingPid());
    }

    // Unique among roots, handles and live export tokens. Called under ioLock.
    private long id() {
        long value;
        do {
            value = random.nextLong() & Long.MAX_VALUE;
        } while (value == 0 || roots.containsKey(value) || files.containsKey(value)
                || exports.contains(value));
        return value;
    }

    /** Releases one handle without any caller check; internal transitions only. */
    private void releaseHandle(long handle) {
        File file = files.remove(handle);
        if (file == null)
            return;
        // Return promptly; close after any in-flight data operation releases
        // ioLock. Closing one handle must not block the control plane.
        try {
            reaper.execute(() -> {
                synchronized (ioLock) {
                    try {
                        file.fd.close();
                    } catch (IOException ignored) {
                    }
                }
            });
        } catch (RejectedExecutionException gone) {
            // onDestroy already stopped the reaper; the descriptor dies with the
            // process and never left it.
        }
    }

    /** Releases every handle of this owner that aliases a deleted document. */
    private void releaseAliases(Uri uri, Caller owner) {
        for (long key : new ArrayList<>(files.keySet())) {
            File alias = files.get(key);
            if (alias != null && alias.uri.equals(uri) && sameOwner(alias.owner, owner))
                releaseHandle(key);
        }
    }

    private final ITorrentSafStorage.Stub binder = new ITorrentSafStorage.Stub() {
        @Override
        public long handshake() {
            Caller owner = caller();
            // The most recent handshake holds retirement authority, so a broker
            // process that outlives an application restart still follows the
            // client that currently owns the binding.
            primaryCaller = owner;
            return identity.generation();
        }

        @Override
        public int apiVersion() {
            caller();
            // Qualified so the value is the broker constant, not the generated
            // interface field of the same name.
            return TorrentSafStorageService.API_VERSION;
        }

        @Override
        public long prepareExport(String text, long length, long operation) {
            Caller owner = caller();
            Operation current = begin(operation, owner);
            if (current == null)
                return 0;
            try {
                synchronized (ioLock) {
                    if (!armWatchdog(current))
                        return 0;
                    if (text == null || text.length() > MAX_TEXT || length <= 0
                            || length > MAX_EXPORT || exports.size() >= MAX_EXPORTS)
                        return 0;
                    Uri uri = Uri.parse(text);
                    if (!"content".equals(uri.getScheme()) || !documentGrant(uri, true))
                        return 0;
                    current.signal.throwIfCanceled();
                    String documentId = currentDocumentId(uri, current.signal);
                    if (documentId == null || documentId.isEmpty())
                        return 0;
                    long token = id();
                    // The grant binds caller, URI, declared length and the
                    // document identity observed now; it authorizes one
                    // truncating open and nothing else.
                    return exports.prepare(token, owner, text, length, documentId,
                                   SystemClock.elapsedRealtime())
                            ? token
                            : 0;
                }
            } catch (Exception failure) {
                return 0;
            } finally {
                end(current);
            }
        }

        @Override
        public long openExport(long token, long operation) {
            Caller owner = caller();
            Operation current = begin(operation, owner);
            if (current == null)
                return 0;
            try {
                synchronized (ioLock) {
                    if (!armWatchdog(current))
                        return 0;
                    ExportGrants.Grant grant =
                            exports.consume(token, owner, SystemClock.elapsedRealtime());
                    // Unknown, expired, already used or another caller's token:
                    // nothing is opened and nothing is truncated.
                    if (grant == null || files.size() >= MAX_HANDLES)
                        return 0;
                    Uri uri = Uri.parse(grant.uri());
                    if (!documentGrant(uri, true)
                            || !TorrentSafPolicy.documentIdentityMatches(
                                    grant.documentId(), currentDocumentId(uri, current.signal)))
                        return 0;
                    ParcelFileDescriptor fd = null;
                    try {
                        fd = getContentResolver().openFileDescriptor(uri, "rw", current.signal);
                        if (fd == null
                                || !OsConstants.S_ISREG(Os.fstat(fd.getFileDescriptor()).st_mode))
                            return 0;
                        current.signal.throwIfCanceled();
                        Os.lseek(fd.getFileDescriptor(), 0, OsConstants.SEEK_SET);
                        // Only the prepared document is truncated, and only after
                        // the caller, the grant, the identity and the length
                        // matched. Start empty rather than pre-sizing: a failed
                        // first write must not leave a full-length zero file.
                        Os.ftruncate(fd.getFileDescriptor(), 0);
                        long key = id();
                        files.put(key, new File(0, uri, fd, grant.length(), true,
                                               grant.documentId(), owner));
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
            } catch (Exception failure) {
                return 0;
            } finally {
                end(current);
            }
        }

        @Override
        public boolean finishExport(long handle, boolean success, long operation) {
            Caller owner = caller();
            Operation current = begin(operation, owner);
            if (current == null)
                return false;
            try {
                synchronized (ioLock) {
                    if (!armWatchdog(current))
                        return false;
                    File file = files.get(handle);
                    if (file == null || file.root != 0 || !sameOwner(file.owner, owner))
                        return false;
                    boolean ok = false;
                    try {
                        current.signal.throwIfCanceled();
                        if (success) {
                            if (!authorized(file, true, current.signal))
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
            } catch (Exception failure) {
                return false;
            } finally {
                end(current);
            }
        }

        @Override
        public long authorizeTree(String text, long operation) {
            Caller owner = caller();
            Operation current = begin(operation, owner);
            if (current == null)
                return 0;
            try {
                synchronized (ioLock) {
                    if (!armWatchdog(current))
                        return 0;
                    current.signal.throwIfCanceled();
                    if (text == null || text.length() > MAX_TEXT || roots.size() >= MAX_ROOTS)
                        return 0;
                    Uri uri = Uri.parse(text);
                    if (!"content".equals(uri.getScheme()) || !DocumentsContract.isTreeUri(uri))
                        return 0;
                    for (UriPermission permission :
                            getContentResolver().getPersistedUriPermissions()) {
                        if (permission.getUri().equals(uri) && permission.isReadPermission()) {
                            long key = id();
                            roots.put(key, new Root(uri, permission.isWritePermission(), owner));
                            return key;
                        }
                    }
                    return 0;
                }
            } catch (Exception failure) {
                return 0;
            } finally {
                end(current);
            }
        }

        @Override
        public long openFile(
                long rootId, String relative, long length, boolean create, long operation) {
            Caller owner = caller();
            Operation current = begin(operation, owner);
            if (current == null)
                return 0;
            try {
                synchronized (ioLock) {
                    if (!armWatchdog(current))
                        return 0;
                    current.signal.throwIfCanceled();
                    Root root = roots.get(rootId);
                    if (root == null || !sameOwner(root.owner, owner)
                            || create && !root.writable || files.size() >= MAX_HANDLES
                            || length < 0 || length > MAX_FILE || !validPath(relative))
                        return 0;
                    ParcelFileDescriptor fd = null;
                    try {
                        Uri doc = resolve(root.uri, relative, create, current.signal);
                        if (doc == null)
                            return 0;
                        fd = getContentResolver().openFileDescriptor(
                                doc, create ? "rw" : "r", current.signal);
                        if (fd == null)
                            return 0;
                        // Reject pipes and providers without random access. No
                        // unbounded staging fallback.
                        Os.lseek(fd.getFileDescriptor(), 0, OsConstants.SEEK_SET);
                        StructStat stat = Os.fstat(fd.getFileDescriptor());
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
                        current.signal.throwIfCanceled();
                        long key = id();
                        files.put(key, new File(rootId, doc, fd, length, create,
                                               currentDocumentId(doc, current.signal), owner));
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
            } catch (Exception failure) {
                return 0;
            } finally {
                end(current);
            }
        }

        @Override
        public byte[] read(long handle, long offset, int count, long operation) {
            Caller owner = caller();
            Operation current = begin(operation, owner);
            if (current == null)
                return null;
            try {
                synchronized (ioLock) {
                    if (!armWatchdog(current))
                        return null;
                    current.signal.throwIfCanceled();
                    File file = files.get(handle);
                    if (file == null || !sameOwner(file.owner, owner))
                        return null;
                    if (!range(file, offset, count) || !authorized(file, false, current.signal))
                        return null;
                    byte[] result = new byte[count];
                    try {
                        int done = 0;
                        while (done < count) {
                            current.signal.throwIfCanceled();
                            int n = Os.pread(file.fd.getFileDescriptor(), result, done,
                                    count - done, offset + done);
                            if (n <= 0)
                                return null;
                            done += n;
                        }
                        return result;
                    } catch (Exception failure) {
                        releaseHandle(handle);
                        return null;
                    }
                }
            } catch (Exception failure) {
                return null;
            } finally {
                end(current);
            }
        }

        @Override
        public boolean write(long handle, long offset, byte[] bytes, long operation) {
            Caller owner = caller();
            Operation current = begin(operation, owner);
            if (current == null)
                return false;
            try {
                synchronized (ioLock) {
                    if (!armWatchdog(current))
                        return false;
                    current.signal.throwIfCanceled();
                    File file = files.get(handle);
                    if (file == null || !sameOwner(file.owner, owner) || bytes == null
                            || !range(file, offset, bytes.length) || !file.writable
                            || !authorized(file, true, current.signal))
                        return false;
                    try {
                        int done = 0;
                        while (done < bytes.length) {
                            current.signal.throwIfCanceled();
                            int n = Os.pwrite(file.fd.getFileDescriptor(), bytes, done,
                                    bytes.length - done, offset + done);
                            if (n <= 0)
                                return false;
                            done += n;
                        }
                        Os.fsync(file.fd.getFileDescriptor());
                        return true;
                    } catch (Exception failure) {
                        releaseHandle(handle);
                        return false;
                    }
                }
            } catch (Exception failure) {
                return false;
            } finally {
                end(current);
            }
        }

        @Override
        public boolean deleteFile(long handle, long operation) {
            Caller owner = caller();
            Operation current = begin(operation, owner);
            if (current == null)
                return false;
            try {
                synchronized (ioLock) {
                    if (!armWatchdog(current))
                        return false;
                    current.signal.throwIfCanceled();
                    File file = files.get(handle);
                    // A read-only descriptor may authorize deletion only when the
                    // persisted root grant also grants write. Never delete a tree
                    // and never delete a user-chosen single-document export.
                    if (file == null || file.root == 0 || !sameOwner(file.owner, owner)
                            || !authorized(file, true, current.signal))
                        return false;
                    try {
                        // Reject directories even if a provider changes document type.
                        try (Cursor cursor = getContentResolver().query(file.uri,
                                     new String[] {DocumentsContract.Document.COLUMN_MIME_TYPE},
                                     null, null, null, current.signal)) {
                            if (cursor == null || !cursor.moveToFirst()
                                    || DocumentsContract.Document.MIME_TYPE_DIR.equals(
                                            cursor.getString(0)))
                                return false;
                        }
                        // A provider may replace an existing document ID. Compare
                        // a freshly opened descriptor with the registered object.
                        try (ParcelFileDescriptor fresh =
                                        getContentResolver().openFileDescriptor(
                                                file.uri, "r", current.signal)) {
                            if (fresh == null)
                                return false;
                            if (!sameDocument(Os.fstat(file.fd.getFileDescriptor()),
                                        Os.fstat(fresh.getFileDescriptor()), file.length))
                                return false;
                        }
                        current.signal.throwIfCanceled();
                        boolean removed =
                                DocumentsContract.deleteDocument(getContentResolver(), file.uri);
                        if (removed)
                            // Invalidate aliases to the same document before later IO.
                            releaseAliases(file.uri, owner);
                        return removed;
                    } catch (Exception failure) {
                        return false;
                    }
                }
            } catch (Exception failure) {
                return false;
            } finally {
                end(current);
            }
        }

        @Override
        public boolean deleteByRelative(long rootId, String relative, long operation) {
            Caller owner = caller();
            Operation current = begin(operation, owner);
            if (current == null)
                return false;
            try {
                synchronized (ioLock) {
                    if (!armWatchdog(current))
                        return false;
                    current.signal.throwIfCanceled();
                    Root root = roots.get(rootId);
                    if (root == null || !sameOwner(root.owner, owner) || !root.writable
                            || !validPath(relative))
                        return false;
                    Uri doc = resolve(root.uri, relative, false, current.signal);
                    if (doc == null || !regularDocument(doc, current.signal))
                        return false;
                    // Identity must survive the resolution: a provider may rebind
                    // the display name to a different document between the two
                    // opens, so compare device/inode across both descriptors.
                    try (ParcelFileDescriptor opened =
                                    getContentResolver().openFileDescriptor(
                                            doc, "r", current.signal)) {
                        if (opened == null)
                            return false;
                        StructStat first = Os.fstat(opened.getFileDescriptor());
                        if (!OsConstants.S_ISREG(first.st_mode))
                            return false;
                        try (ParcelFileDescriptor fresh =
                                        getContentResolver().openFileDescriptor(
                                                doc, "r", current.signal)) {
                            if (fresh == null
                                    || !sameDocumentIdentity(
                                            first, Os.fstat(fresh.getFileDescriptor())))
                                return false;
                        }
                        current.signal.throwIfCanceled();
                        boolean removed = DocumentsContract.deleteDocument(
                                getContentResolver(), doc);
                        if (removed)
                            releaseAliases(doc, owner);
                        return removed;
                    } catch (Exception failure) {
                        return false;
                    }
                }
            } catch (Exception failure) {
                return false;
            } finally {
                end(current);
            }
        }

        @Override
        public boolean removeEmptyDirectory(long rootId, String relative, long operation) {
            Caller owner = caller();
            Operation current = begin(operation, owner);
            if (current == null)
                return false;
            try {
                synchronized (ioLock) {
                    if (!armWatchdog(current))
                        return false;
                    current.signal.throwIfCanceled();
                    Root root = roots.get(rootId);
                    if (root == null || !sameOwner(root.owner, owner) || !root.writable
                            || !validPath(relative))
                        return false;
                    Uri doc = resolve(root.uri, relative, false, current.signal);
                    // Only an empty directory may be removed: a provider is
                    // allowed to delete recursively, so enumerate first.
                    if (doc == null || !emptyDirectory(root.uri, doc, current.signal))
                        return false;
                    current.signal.throwIfCanceled();
                    boolean removed =
                            DocumentsContract.deleteDocument(getContentResolver(), doc);
                    if (removed)
                        releaseAliases(doc, owner);
                    return removed;
                }
            } catch (Exception failure) {
                return false;
            } finally {
                end(current);
            }
        }

        @Override
        public void closeFile(long handle) {
            Caller owner = caller();
            File file = files.get(handle);
            if (file == null || !sameOwner(file.owner, owner))
                return;
            releaseHandle(handle);
        }

        @Override
        public void revokeTree(long rootId) {
            Caller owner = caller();
            try {
                synchronized (ioLock) {
                    Root root = roots.get(rootId);
                    if (root == null || !sameOwner(root.owner, owner))
                        return;
                    roots.remove(rootId);
                    for (long key : new ArrayList<>(files.keySet())) {
                        File file = files.get(key);
                        if (file != null && file.root == rootId
                                && sameOwner(file.owner, owner))
                            releaseHandle(key);
                    }
                }
            } catch (RuntimeException ignored) {
                // A revocation is best effort; the grant is already revoked.
            }
        }

        @Override
        public void cancel(long operation) {
            Caller owner = caller();
            Operation current;
            synchronized (operationLock) {
                Admission.Slot<Operation> slot = admission.find(operation);
                current = slot == null ? null : slot.value();
            }
            // Cancellation is scoped to one operation and to the exact process
            // that registered it. A slow export or close must not kill every
            // torrent's descriptors, and a sibling component cannot abort ours.
            if (current == null || !current.owner.equals(owner))
                return;
            // Oneway: no caller waits for this, and the broker's provider call
            // is unwound by its own cancellation signal.
            try {
                current.signal.cancel();
            } catch (RuntimeException ignored) {
            }
        }

        @Override
        public void retire(long generation) {
            Caller owner = caller();
            Caller primary = primaryCaller;
            // Only the process that performed the handshake may retire the
            // broker, and only for the generation it was given.
            if (primary != null && !primary.equals(owner))
                return;
            if (!identity.claimRetirement(generation))
                return;
            retireProcess();
        }
    };

    /**
     * Resolves (and optionally creates) every component of a bounded relative
     * path below the tree root. Each created component is validated before it
     * becomes a parent, so a provider that returns another name or type cannot
     * redirect the walk.
     */
    private Uri resolve(Uri tree, String relative, boolean create, CancellationSignal signal)
            throws IOException {
        Uri doc = DocumentsContract.buildDocumentUriUsingTree(
                tree, DocumentsContract.getTreeDocumentId(tree));
        String[] parts = relative.split("/", -1);
        for (int i = 0; i < parts.length; i++) {
            boolean directory = i + 1 < parts.length;
            Uri child = find(tree, doc, parts[i], directory, signal);
            signal.throwIfCanceled();
            if (child == null && create) {
                child = DocumentsContract.createDocument(getContentResolver(), doc,
                        directory ? DocumentsContract.Document.MIME_TYPE_DIR
                                  : "application/octet-stream",
                        parts[i]);
                if (child != null) {
                    try (Cursor cursor = getContentResolver().query(child,
                                 new String[] {DocumentsContract.Document.COLUMN_DISPLAY_NAME,
                                         DocumentsContract.Document.COLUMN_MIME_TYPE},
                                 null, null, null, signal)) {
                        boolean matches = cursor != null && cursor.moveToFirst()
                                && parts[i].equals(cursor.getString(0))
                                && directory == DocumentsContract.Document.MIME_TYPE_DIR
                                                        .equals(cursor.getString(1));
                        if (!matches) {
                            try {
                                DocumentsContract.deleteDocument(getContentResolver(), child);
                            } catch (Exception ignored) {
                            }
                            throw new IOException("Created document mismatch");
                        }
                    }
                }
            }
            if (child == null)
                return null;
            doc = child;
        }
        return doc;
    }

    static boolean validPath(String path) {
        // Display names may legally contain ':' and '\\' on Android; they are
        // not POSIX path separators here and must not be treated as traversal.
        if (path == null || path.isEmpty() || path.length() > MAX_TEXT)
            return false;
        String[] parts = path.split("/", -1);
        if (parts.length > MAX_PARTS)
            return false;
        for (String part : parts) {
            if (part.isEmpty() || part.equals(".") || part.equals("..")
                    || part.length() > MAX_PART)
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
        if (root == null || !sameOwner(root.owner, file.owner))
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

    /** True when the resolved document exists and is not a directory. */
    private boolean regularDocument(Uri doc, CancellationSignal signal) {
        try (Cursor cursor = getContentResolver().query(doc,
                     new String[] {DocumentsContract.Document.COLUMN_MIME_TYPE}, null, null, null,
                     signal)) {
            return cursor != null && cursor.moveToFirst()
                    && !DocumentsContract.Document.MIME_TYPE_DIR.equals(cursor.getString(0));
        } catch (Exception failure) {
            return false;
        }
    }

    /** True only for an existing directory whose child listing is empty. */
    private boolean emptyDirectory(Uri tree, Uri doc, CancellationSignal signal) {
        try (Cursor cursor = getContentResolver().query(doc,
                     new String[] {DocumentsContract.Document.COLUMN_MIME_TYPE}, null, null, null,
                     signal)) {
            if (cursor == null || !cursor.moveToFirst()
                    || !DocumentsContract.Document.MIME_TYPE_DIR.equals(cursor.getString(0)))
                return false;
        } catch (Exception failure) {
            return false;
        }
        try (Cursor children = getContentResolver().query(
                     DocumentsContract.buildChildDocumentsUriUsingTree(
                             tree, DocumentsContract.getDocumentId(doc)),
                     new String[] {DocumentsContract.Document.COLUMN_DOCUMENT_ID}, null, null, null,
                     signal)) {
            return children != null && !children.moveToFirst();
        } catch (Exception failure) {
            return false;
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
                if (++visited > MAX_CHILDREN)
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

    static boolean sameDocument(StructStat opened, StructStat current, long length) {
        return opened.st_dev == current.st_dev && opened.st_ino == current.st_ino
                && current.st_size == length && opened.st_size == length
                && OsConstants.S_ISREG(opened.st_mode) && OsConstants.S_ISREG(current.st_mode);
    }

    /**
     * Size-independent identity check used by deletion: a failed or partial
     * write legitimately changes the length, but the device/inode pair and the
     * regular-file type must still match the document that was opened twice
     * under the same grant.
     */
    static boolean sameDocumentIdentity(StructStat opened, StructStat current) {
        return opened.st_dev == current.st_dev && opened.st_ino == current.st_ino
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
        watchdog.shutdownNow();
        retireProcess();
        super.onDestroy();
    }
}

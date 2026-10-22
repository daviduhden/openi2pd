/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 * openi2pd: bounded SAF Binder client and persisted torrent grants.
 */
package org.openi2pd.router;

import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.content.ServiceConnection;
import android.net.Uri;
import android.os.IBinder;
import android.provider.DocumentsContract;
import java.util.concurrent.ArrayBlockingQueue;
import java.util.concurrent.ThreadPoolExecutor;
import java.util.concurrent.TimeUnit;

final class TorrentSafStorage {
    private static final Object lock = new Object();
    private static final android.os.Handler main =
            new android.os.Handler(android.os.Looper.getMainLooper());
    private static volatile Context context;
    private static ITorrentSafStorage remote;
    private static long generation, nextId = 1, nextOperation = 1;
    private static volatile long activeOperation;
    private static volatile IBinder retiringBinder;
    private static final int MAX_BIND_ATTEMPTS = 6;
    private static int retries;
    private static boolean bound, retryScheduled;
    private static volatile boolean bindUnavailable;
    private static final java.util.Map<String, Long> authorized = new java.util.HashMap<>();
    private static final java.util.Map<Long, Root> roots = new java.util.HashMap<>();
    private static final java.util.Map<Long, File> files = new java.util.HashMap<>();
    private static final class Root {
        final String uri;
        long generation, remoteId;
        Root(String uri) {
            this.uri = uri;
        }
    }
    private static final class File {
        final long root;
        final String path;
        final long length;
        final boolean create;
        long generation, remoteId;
        File(long root, String path, long length, boolean create) {
            this.root = root;
            this.path = path;
            this.length = length;
            this.create = create;
        }
    }
    private record Session(ITorrentSafStorage remote, long generation, long operation) {}
    private interface Operation<T> {
        T run(Session session) throws Exception;
    }
    // A blocked Binder transaction can occupy only this single thread. Recovery
    // never creates a replacement executor or accumulates queued operations.
    private static final ThreadPoolExecutor calls =
            new ThreadPoolExecutor(1, 1, 0, TimeUnit.SECONDS, new ArrayBlockingQueue<>(1), task -> {
                Thread thread = new Thread(task, "torrent-saf-ipc");
                thread.setDaemon(true);
                return thread;
            });
    private static final ServiceConnection connection = new ServiceConnection() {
        @Override
        public void onServiceConnected(ComponentName name, IBinder binder) {
            synchronized (lock) {
                if (retiringBinder != null)
                    return;
                if (remote != null) {
                    if (remote.asBinder() == binder)
                        return;
                    // A replacement callback is not proof that the previous
                    // process has stopped touching its authorized documents.
                    retire(new Session(remote, generation, 0));
                    return;
                }
                remote = ITorrentSafStorage.Stub.asInterface(binder);
                generation++;
                long current = generation;
                try {
                    binder.linkToDeath(() -> brokerDied(current, binder), 0);
                } catch (android.os.RemoteException dead) {
                    brokerDied(current, binder);
                }
                lock.notifyAll();
            }
        }
        @Override
        public void onServiceDisconnected(ComponentName name) {
            connectionLost();
        }
        @Override
        public void onBindingDied(ComponentName name) {
            connectionLost();
        }
        @Override
        public void onNullBinding(ComponentName name) {
            connectionLost();
        }
    };
    private static void connectionLost() {
        Session lost;
        synchronized (lock) {
            if (remote == null) {
                scheduleBind();
                return;
            }
            lost = new Session(remote, generation, 0);
        }
        // Lifecycle callbacks alone do not prove death. Retire and fence the
        // actual binder; only its death recipient permits a replacement.
        retire(lost);
        if (!lost.remote.asBinder().isBinderAlive())
            brokerDied(lost.generation, lost.remote.asBinder());
    }
    private static void brokerDied(long expected, IBinder binder) {
        synchronized (lock) {
            if (retiringBinder == binder)
                retiringBinder = null;
            disconnected(expected);
            if (remote == null)
                scheduleBind();
        }
    }
    private static void retire(Session session) {
        synchronized (lock) {
            if (generation != session.generation || remote != session.remote)
                return;
            retiringBinder = session.remote.asBinder();
            remote = null;
            generation++;
            lock.notifyAll();
        }
        // Both methods are oneway and bypass the broker's provider I/O lock.
        // Do not create a new generation until its death recipient has fired.
        try {
            session.remote.cancel(session.operation);
            session.remote.retire();
        } catch (android.os.RemoteException | RuntimeException dead) {
            if (!session.remote.asBinder().isBinderAlive())
                brokerDied(session.generation, session.remote.asBinder());
        }
    }
    private static void disconnected(long expected) {
        synchronized (lock) {
            if (expected >= 0 && expected != generation)
                return;
            remote = null;
            generation++;
            lock.notifyAll();
            scheduleBind();
        }
    }
    private static void scheduleBind() {
        if (context == null || retryScheduled || retiringBinder != null || bindUnavailable)
            return;
        if (retries >= MAX_BIND_ATTEMPTS) {
            // A permanently failing policy/service must not spawn and kill the
            // broker process every few seconds forever.
            bindUnavailable = true;
            return;
        }
        retryScheduled = true;
        long delay = Math.min(10000L, 250L << Math.min(retries++, 5));
        main.postDelayed(() -> {
            synchronized (lock) {
                retryScheduled = false;
                if (remote != null || retiringBinder != null)
                    return;
                if (bound) {
                    try {
                        context.unbindService(connection);
                    } catch (IllegalArgumentException ignored) {
                    }
                    bound = false;
                }
                try {
                    bound = context.bindService(new Intent(context, TorrentSafStorageService.class),
                            connection, Context.BIND_AUTO_CREATE);
                } catch (RuntimeException unavailable) {
                    bound = false;
                }
                if (!bound)
                    scheduleBind();
            }
        }, delay);
    }
    static void initialize(Context app) {
        synchronized (lock) {
            if (context == null) {
                context = app.getApplicationContext();
                bindUnavailable = false;
                retries = 0;
                scheduleBind();
            }
        }
        // Library load and bridge installation run on the daemon start worker,
        // not on the service main thread. Binding alone only starts the broker.
    }
    static boolean select(Context app, String tunnel, String hash, Uri tree) {
        if (tunnel == null || tunnel.isEmpty() || tunnel.length() > 1024 || hash == null
                || !hash.matches("[0-9a-fA-F]{40}") || tree == null
                || !"content".equals(tree.getScheme()) || !DocumentsContract.isTreeUri(tree))
            return false;
        try {
            int flags =
                    Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_WRITE_URI_PERMISSION;
            app.getContentResolver().takePersistableUriPermission(tree, flags);
            boolean read = false, write = false;
            for (android.content.UriPermission grant :
                    app.getContentResolver().getPersistedUriPermissions()) {
                if (!grant.getUri().equals(tree))
                    continue;
                read |= grant.isReadPermission();
                write |= grant.isWritePermission();
            }
            if (!read || !write) {
                releaseUnmapped(app, tree, flags);
                return false;
            }
            String key = tunnel + ":" + hash.toLowerCase(java.util.Locale.ROOT);
            var prefs = app.getSharedPreferences("torrent-saf-grants", Context.MODE_PRIVATE);
            String previous = prefs.getString(key, null);
            boolean saved = prefs.edit().putString(key, tree.toString()).commit();
            if (!saved) {
                releaseUnmapped(app, tree, flags);
                return false;
            }
            if (previous != null && !previous.equals(tree.toString())
                    && !TorrentSafGrants.referenced(prefs.getAll(), previous, key)) {
                try {
                    app.getContentResolver().releasePersistableUriPermission(
                            Uri.parse(previous), flags);
                } catch (SecurityException ignored) {
                }
            }
            synchronized (lock) {
                authorized.remove(key);
                bindUnavailable = false;
                retries = 0;
                if (remote == null && retiringBinder == null)
                    scheduleBind();
            }
            return true;
        } catch (Exception failure) {
            return false;
        }
    }

    private static void releaseUnmapped(Context app, Uri tree, int flags) {
        String uri = tree.toString();
        for (Object value : app.getSharedPreferences("torrent-saf-grants", Context.MODE_PRIVATE)
                        .getAll()
                        .values())
            if (uri.equals(value))
                return;
        try {
            app.getContentResolver().releasePersistableUriPermission(tree, flags);
        } catch (RuntimeException ignored) {
        }
    }

    static boolean detach(Context app, String tunnel, String hash) {
        if (app == null || tunnel == null || hash == null || !hash.matches("[0-9a-fA-F]{40}"))
            return false;
        String key = tunnel + ":" + hash.toLowerCase(java.util.Locale.ROOT);
        var prefs = app.getSharedPreferences("torrent-saf-grants", Context.MODE_PRIVATE);
        String previous = prefs.getString(key, null);
        boolean saved = prefs.edit().remove(key).commit();
        // A failed commit means the key still points at this tree; never
        // revoke the permission underneath it.
        if (!saved)
            return false;
        if (previous != null && !TorrentSafGrants.referenced(prefs.getAll(), previous, key)) {
            try {
                app.getContentResolver().releasePersistableUriPermission(Uri.parse(previous),
                        Intent.FLAG_GRANT_READ_URI_PERMISSION
                                | Intent.FLAG_GRANT_WRITE_URI_PERMISSION);
            } catch (Exception ignored) {
            }
        }
        synchronized (lock) {
            authorized.remove(key);
        }
        return saved;
    }
    // Called only from native disk workers, never the main thread. A selected but
    // revoked/unavailable mapping remains selected and fails closed.
    static boolean hasAnyGrant() {
        return context != null
                && !context.getSharedPreferences("torrent-saf-grants", Context.MODE_PRIVATE)
                            .getAll()
                            .isEmpty();
    }
    public static boolean configured(String tunnel, String hash) {
        // A missing context must fail closed for the native backend: claiming
        // "not configured" would let a selected torrent fall back to an
        // ordinary filesystem path. SafPayload.root() then returns zero.
        if (context == null)
            return true;
        return tunnel != null && hash != null
                && context.getSharedPreferences("torrent-saf-grants", Context.MODE_PRIVATE)
                           .contains(tunnel + ":" + hash.toLowerCase(java.util.Locale.ROOT));
    }
    public static long root(String tunnel, String hash) {
        synchronized (lock) {
            if (context == null)
                return 0;
            String key = tunnel + ":" + hash.toLowerCase(java.util.Locale.ROOT);
            Long existing = authorized.get(key);
            if (existing != null)
                return existing;
            String uri = context.getSharedPreferences("torrent-saf-grants", Context.MODE_PRIVATE)
                                 .getString(key, null);
            if (uri == null || roots.size() >= 128 || nextId == Long.MAX_VALUE)
                return 0;
            long id = nextId++;
            roots.put(id, new Root(uri));
            authorized.put(key, id);
            return id;
        }
    }
    private static long rootId(Session session, long id) throws Exception {
        Root root;
        synchronized (lock) {
            root = roots.get(id);
            if (root == null)
                return 0;
            if (root.generation == session.generation && root.remoteId != 0)
                return root.remoteId;
        }
        long result = session.remote.authorizeTree(root.uri, session.operation);
        synchronized (lock) {
            if (generation != session.generation || roots.get(id) != root)
                return 0;
            root.generation = session.generation;
            root.remoteId = result;
        }
        return result;
    }
    private static long fileId(Session session, long id) throws Exception {
        File file;
        synchronized (lock) {
            file = files.get(id);
            if (file == null)
                return 0;
            if (file.generation == session.generation && file.remoteId != 0)
                return file.remoteId;
        }
        long root = rootId(session, file.root);
        if (root == 0)
            return 0;
        long result = session.remote.openFile(
                root, file.path, file.length, file.create, session.operation);
        synchronized (lock) {
            if (generation != session.generation || files.get(id) != file)
                return 0;
            file.generation = session.generation;
            file.remoteId = result;
        }
        return result;
    }
    public static long open(long root, String relative, long length, boolean create) {
        long id;
        synchronized (lock) {
            if (!roots.containsKey(root) || files.size() >= 4096 || nextId == Long.MAX_VALUE)
                return 0;
            id = nextId++;
            files.put(id, new File(root, relative, length, create));
        }
        boolean opened = call(session -> fileId(session, id) != 0, false);
        if (!opened) {
            synchronized (lock) {
                files.remove(id);
            }
            return 0;
        }
        return id;
    }
    public static byte[] read(long handle, long offset, int count) {
        if (offset < 0 || count < 0 || count > 16384)
            return null;
        return call(session -> {
            long id = fileId(session, handle);
            byte[] result =
                    id == 0 ? null : session.remote.read(id, offset, count, session.operation);
            if (result == null || result.length != count) {
                invalidateFile(session, handle);
                return null;
            }
            return result;
        }, null);
    }
    public static boolean write(long handle, long offset, byte[] bytes) {
        if (offset < 0 || bytes == null || bytes.length > 16384)
            return false;
        byte[] owned = bytes.clone();
        // A write interrupted by death/timeout is ambiguous: return failure and
        // let the engine invalidate/reverify the piece. Never replay it here.
        return call(session -> {
            long id = fileId(session, handle);
            boolean written = id != 0 && session.remote.write(id, offset, owned, session.operation);
            if (!written)
                invalidateFile(session, handle);
            return written;
        }, false);
    }
    static boolean export(
            Uri target, byte[] metadata, java.util.concurrent.atomic.AtomicBoolean canceled) {
        if (target == null || !"content".equals(target.getScheme()) || metadata == null
                || metadata.length == 0 || metadata.length > 8 * 1024 * 1024)
            return false;
        byte[] owned = metadata.clone();
        // Each chunk is its own bounded Binder operation. A multi-megabyte
        // export must not consume one five-second broker deadline and then
        // destroy every unrelated torrent handle.
        long handle = call(session
                -> session.remote.openExport(target.toString(), owned.length, session.operation),
                0L);
        if (handle == 0)
            return false;
        boolean committed = false;
        try {
            for (int offset = 0; offset < owned.length; offset += 16384) {
                if (canceled.get())
                    return false;
                final int at = offset;
                byte[] chunk =
                        java.util.Arrays.copyOfRange(owned, at, Math.min(owned.length, at + 16384));
                if (!call(session
                            -> session.remote.write(handle, at, chunk, session.operation),
                            false))
                    return false;
            }
            committed = call(
                    session -> session.remote.finishExport(handle, true, session.operation), false);
            return committed;
        } finally {
            if (!committed) {
                try {
                    call(session
                            -> session.remote.finishExport(handle, false, session.operation),
                            false);
                } catch (RuntimeException ignored) {
                }
            }
        }
    }
    static void cancelPending() {
        Session session;
        long operation;
        synchronized (lock) {
            session = remote == null ? null : new Session(remote, generation, activeOperation);
            operation = activeOperation;
        }
        if (session == null)
            return;
        try {
            session.remote.cancel(operation);
        } catch (android.os.RemoteException | RuntimeException failure) {
            if (!session.remote.asBinder().isBinderAlive())
                brokerDied(session.generation, session.remote.asBinder());
        }
    }
    public static boolean remove(long root, String path, long length) {
        if (!TorrentSafStorageService.validPath(path) || length < 0)
            return false;
        // Resolve through a read-only registered capability. No creation,
        // truncation, path-based recursive deletion or ambiguous replay.
        long handle = open(root, path, length, false);
        if (handle == 0)
            return false;
        try {
            return call(session -> {
                long id = fileId(session, handle);
                return id != 0 && session.remote.deleteFile(id, session.operation);
            }, false);
        } finally {
            close(handle);
        }
    }
    static String status(String tunnel, String hash) {
        synchronized (lock) {
            if (context == null)
                return "Storage bridge unavailable";
            String uri = context.getSharedPreferences("torrent-saf-grants", Context.MODE_PRIVATE)
                                 .getString(tunnel + ":" + hash.toLowerCase(java.util.Locale.ROOT),
                                         null);
            if (uri == null)
                return "Native private storage";
            boolean permitted = false;
            for (android.content.UriPermission grant :
                    context.getContentResolver().getPersistedUriPermissions())
                if (grant.getUri().toString().equals(uri) && grant.isReadPermission()
                        && grant.isWritePermission())
                    permitted = true;
            if (!permitted)
                return "Document permission revoked. Select the directory again.";
            if (bindUnavailable)
                return "Storage broker unavailable; reselect the directory or restart the app.";
            if (retiringBinder != null)
                return "Storage provider cancellation pending; waiting for broker termination";
            if (remote == null)
                return "Storage broker reconnecting. Failed pieces will be verified before reuse.";
            return "SAF storage connected; random-access provider required";
        }
    }
    private static void invalidateFile(Session session, long handle) throws Exception {
        long remoteId = 0;
        synchronized (lock) {
            File file = files.remove(handle);
            if (file != null && file.generation == session.generation)
                remoteId = file.remoteId;
        }
        if (remoteId != 0)
            session.remote.closeFile(remoteId);
    }
    public static void close(long handle) {
        File file;
        Session session;
        synchronized (lock) {
            file = files.remove(handle);
            // Local teardown must never wait for a broker which no longer owns
            // this descriptor, or trigger recovery just to close a dead token.
            if (file == null || remote == null || file.generation != generation
                    || file.remoteId == 0)
                return;
            session = sessionLocked();
        }
        if (session != null)
            execute(session, current -> {
                current.remote.closeFile(file.remoteId);
                return true;
            }, false, android.os.SystemClock.elapsedRealtime() + 5000, false);
    }
    public static void release(long root) {
        Root entry;
        Session session;
        synchronized (lock) {
            entry = roots.remove(root);
            authorized.entrySet().removeIf(item -> item.getValue() == root);
            files.entrySet().removeIf(item -> item.getValue().root == root);
            if (entry == null || remote == null || entry.generation != generation
                    || entry.remoteId == 0)
                return;
            session = sessionLocked();
        }
        if (session != null)
            execute(session, current -> {
                current.remote.revokeTree(entry.remoteId);
                return true;
            }, false, android.os.SystemClock.elapsedRealtime() + 5000, false);
    }
    // Caller holds lock. Tokens are never reused, including across generations.
    private static Session sessionLocked() {
        if (nextOperation == Long.MAX_VALUE)
            return null;
        activeOperation = nextOperation;
        return new Session(remote, generation, nextOperation++);
    }
    private static <T> T call(Operation<T> action, T fallback) {
        Session session;
        long deadline = android.os.SystemClock.elapsedRealtime() + 5000;
        synchronized (lock) {
            while (remote == null) {
                scheduleBind();
                long remaining = deadline - android.os.SystemClock.elapsedRealtime();
                if (remaining <= 0
                        || android.os.Looper.myLooper() == android.os.Looper.getMainLooper())
                    return fallback;
                try {
                    lock.wait(remaining);
                } catch (InterruptedException interrupted) {
                    Thread.currentThread().interrupt();
                    return fallback;
                }
            }
            session = sessionLocked();
        }
        return session == null ? fallback : execute(session, action, fallback, deadline, true);
    }
    private static <T> T execute(Session session, Operation<T> action, T fallback, long deadline,
            boolean retireOnFailure) {
        java.util.concurrent.Future<T> future = null;
        try {
            long remaining = deadline - android.os.SystemClock.elapsedRealtime();
            if (remaining <= 0)
                return fallback;
            future = calls.submit(() -> {
                synchronized (lock) {
                    if (generation != session.generation || remote != session.remote)
                        return fallback;
                }
                return action.run(session);
            });
            remaining = deadline - android.os.SystemClock.elapsedRealtime();
            if (remaining <= 0)
                throw new java.util.concurrent.TimeoutException();
            T result = future.get(remaining, TimeUnit.MILLISECONDS);
            synchronized (lock) {
                if (generation != session.generation || remote != session.remote)
                    return fallback;
                retries = 0;
                return result;
            }
        } catch (java.util.concurrent.RejectedExecutionException busy) {
            return fallback;
        } catch (Exception failure) {
            if (future != null)
                future.cancel(true);
            if (failure instanceof InterruptedException)
                Thread.currentThread().interrupt();
            if (retireOnFailure)
                retire(session);
            return fallback;
        }
    }
    private TorrentSafStorage() {
    }
}

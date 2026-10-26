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
import java.util.concurrent.atomic.AtomicBoolean;

/**
 * Client-side SAF capability registry and Binder adapter for torrent payloads.
 * Local root and file IDs survive a broker replacement, while the remote IDs
 * cached in each entry are valid only for one client binding generation.
 * Paths remain relative to an authorized tree and are resolved by the broker.
 *
 * <p>Binder transactions use a bounded worker queue and typed fallbacks.
 * Calls that can wait for a binding or a provider belong on a worker thread;
 * the main-thread binding check returns unavailable rather than waiting.</p>
 */
final class TorrentSafStorage {
    private static final Object lock = new Object();
    private static final android.os.Handler main =
            new android.os.Handler(android.os.Looper.getMainLooper());
    private static volatile Context context;
    private static ITorrentSafStorage remote;
    private static long generation, nextId = 1, nextOperation = 1;
    // Broker generation returned by handshake(). It is deliberately separate
    // from `generation`, which fences this client's own bindings: a broker
    // replacement never reuses the previous process generation.
    private static volatile long brokerGeneration;
    // The one metadata export in progress, if any. Only this token may be
    // cancelled administratively; payload IO belongs to the native engine.
    private static volatile long exportOperation;
    private static final AtomicBoolean exportCanceled = new AtomicBoolean();
    private static volatile IBinder retiringBinder;
    private static final int REQUIRED_API = 2; // ITorrentSafStorage.API_VERSION
    // A bind is awaited at most this long before the typed fallback is returned.
    private static final long BIND_WAIT_MS = 5000;
    // Client deadline for one transaction. The broker has its own 15 s cancel
    // and 30 s fence stages; the client never kills the broker for a deadline.
    private static final long OPERATION_TIMEOUT_MS = 5000;
    // Upper bound on the "retiring" state: a lost oneway retire() or a provider
    // that keeps the broker unresponsive must not prevent rebinding forever.
    private static final long RETIREMENT_BOUND_MS = 5000;
    private static final int RETIREMENT_CHECKS = 2;
    // Bounded bind-retry state. Guarded by `lock`.
    private static final BindBudget budget = new BindBudget();
    private static boolean bound, retryScheduled;
    private static final java.util.Map<String, Long> authorized = new java.util.HashMap<>();
    /**
     * Framework-free bind-retry budget. A permanently broken policy must not
     * spawn and kill the broker process forever, while a transient crash must
     * not disable SAF storage until the application restarts. Guarded by the
     * enclosing client lock; it performs no synchronization of its own.
     */
    static final class BindBudget {
        static final int MAX_ATTEMPTS = 6;
        // A binding that lived less than this is not evidence of a healthy
        // broker, and an explicit re-arm is honoured at most once per window.
        static final long STABLE_BINDING_MS = 30_000;
        private static final long BASE_DELAY_MS = 250, MAX_DELAY_MS = 10_000;
        private int attempts;
        private long connectedAt, retriedAt;
        private boolean connected, retried, exhausted;

        /** Charges one attempt and returns its backoff; -1 means exhausted. */
        long charge() {
            if (exhausted)
                return -1;
            if (attempts >= MAX_ATTEMPTS) {
                exhausted = true;
                return -1;
            }
            return Math.min(MAX_DELAY_MS, BASE_DELAY_MS << Math.min(attempts++, 5));
        }

        boolean exhausted() {
            return exhausted;
        }

        int attempts() {
            return attempts;
        }

        /**
         * Records a successful connection. The latch is always cleared; the
         * attempt budget is restored only when the previous binding was stable,
         * so a broker that connects and immediately dies stays bounded.
         */
        void connected(long now) {
            exhausted = false;
            if (!connected || now - connectedAt >= STABLE_BINDING_MS)
                attempts = 0;
            connected = true;
            connectedAt = now;
        }

        /**
         * A completed transaction is proof of a usable binding, but it restores
         * the budget only for a binding past the stability floor.
         */
        void answered(long now) {
            if (!connected || now - connectedAt < STABLE_BINDING_MS)
                return;
            attempts = 0;
            exhausted = false;
        }

        /**
         * Explicit recovery: clears the exhausted latch at most once per
         * stability window. Returns true when a bind attempt may be scheduled.
         */
        boolean rearm(long now) {
            if (!exhausted)
                return true;
            if (retried && now - retriedAt < STABLE_BINDING_MS)
                return false;
            retried = true;
            retriedAt = now;
            exhausted = false;
            attempts = 0;
            return true;
        }

        /** An explicit selection or daemon start begins a fresh episode. */
        void reset() {
            attempts = 0;
            exhausted = false;
            retried = false;
            connected = false;
            connectedAt = 0;
        }
    }
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

    /** Why a transaction produced its fallback; the caller decides what to do. */
    private enum Failure { NONE, UNAVAILABLE, BUSY, TIMEOUT, PROTOCOL }

    private record Outcome<T>(T value, Failure failure) {
        static <T> Outcome<T> of(T value, Failure failure) {
            return new Outcome<>(value, failure);
        }
    }

    private static boolean retryable(Failure failure) {
        return failure == Failure.BUSY || failure == Failure.TIMEOUT
                || failure == Failure.UNAVAILABLE;
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
            Session accepted = null, replaced = null;
            synchronized (lock) {
                if (retiringBinder != null)
                    return;
                if (remote != null) {
                    if (remote.asBinder() == binder)
                        return;
                    // A replacement callback is not proof that the previous
                    // process has stopped touching its authorized documents.
                    replaced = new Session(remote, generation, 0);
                } else {
                    remote = ITorrentSafStorage.Stub.asInterface(binder);
                    generation++;
                    brokerGeneration = 0;
                    // A successful connection ends the failed-bind episode: a
                    // provider/service crash must never disable SAF storage
                    // until the application restarts. The budget is restored
                    // only for a binding that stayed up, so a connect/crash
                    // loop is still bounded by MAX_ATTEMPTS.
                    budget.connected(android.os.SystemClock.elapsedRealtime());
                    long current = generation;
                    try {
                        binder.linkToDeath(() -> brokerDied(current, binder), 0);
                    } catch (android.os.RemoteException dead) {
                        brokerDied(current, binder);
                    }
                    accepted = remote == null ? null : new Session(remote, current, 0);
                    lock.notifyAll();
                }
            }
            if (replaced != null)
                retire(replaced);
            else if (accepted != null)
                authenticate(accepted);
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
    /**
     * Binds the broker identity (apiVersion + generation) on the single IPC
     * thread so the service main thread never waits for a saturated broker. The
     * next transaction re-checks it, so a rejected submission only defers it.
     */
    private static void authenticate(Session session) {
        try {
            calls.execute(() -> ensureBroker(session));
        } catch (java.util.concurrent.RejectedExecutionException busy) {
            // The IPC thread is occupied; the next call resolves it first.
        }
    }
    /**
     * Resolves the broker generation for this binding exactly once and refuses
     * a broker from another build, for example a process that survived an
     * in-place update and does not implement protocol v2.
     */
    private static boolean ensureBroker(Session session) {
        synchronized (lock) {
            if (brokerGeneration != 0)
                return true;
        }
        int api;
        long generation;
        try {
            api = session.remote.apiVersion();
            generation = session.remote.handshake();
        } catch (android.os.RemoteException dead) {
            brokerDied(session.generation, session.remote.asBinder());
            return false;
        } catch (RuntimeException broken) {
            // An unmarshallable answer means this process does not speak the v2
            // contract. The generation is unknown, so retire(0) is a no-op by
            // contract and the bounded retirement watch replaces the process.
            retire(session);
            return false;
        }
        if (api != REQUIRED_API || generation == 0) {
            // The generation is unknown, so retire(0) is a no-op by contract; the
            // bounded retirement watch replaces the process by unbinding it.
            retire(session);
            return false;
        }
        synchronized (lock) {
            if (TorrentSafStorage.generation != session.generation || remote != session.remote)
                return false;
            brokerGeneration = generation;
            return true;
        }
    }
    private static void retire(Session session) {
        long broker;
        synchronized (lock) {
            if (generation != session.generation || remote != session.remote)
                return;
            retiringBinder = session.remote.asBinder();
            broker = brokerGeneration;
            brokerGeneration = 0;
            exportOperation = 0;
            remote = null;
            generation++;
            lock.notifyAll();
        }
        // The retire request is generation-fenced and one-shot, so a delayed or
        // duplicated frame cannot fence a replacement broker process.
        try {
            session.remote.retire(broker);
        } catch (android.os.RemoteException | RuntimeException dead) {
            if (!session.remote.asBinder().isBinderAlive())
                brokerDied(session.generation, session.remote.asBinder());
        }
        watchRetirement(session.remote.asBinder());
    }
    /**
     * Bounds the retiring state. If the broker is still alive the binding is
     * dropped so the process is destroyed and a fresh broker can be bound; a
     * dead binder is normally resolved by its death recipient, which is given
     * one extra check before the transition is forced.
     */
    private static void watchRetirement(IBinder binder) {
        main.postDelayed(() -> checkRetirement(binder, 1), RETIREMENT_BOUND_MS);
    }
    private static void checkRetirement(IBinder binder, int attempt) {
        synchronized (lock) {
            if (retiringBinder != binder)
                return; // The death recipient already completed the transition.
            if (!binder.isBinderAlive() && attempt < RETIREMENT_CHECKS) {
                main.postDelayed(() -> checkRetirement(binder, attempt + 1), RETIREMENT_BOUND_MS);
                return;
            }
            retiringBinder = null;
            if (remote != null && remote.asBinder() == binder)
                remote = null;
        }
        dropStaleBinding();
    }
    /** Unbinds a binding whose broker ignored (or never received) retirement. */
    private static void dropStaleBinding() {
        boolean unbind;
        synchronized (lock) {
            unbind = bound && context != null;
            bound = false;
        }
        if (unbind) {
            try {
                context.unbindService(connection);
            } catch (IllegalArgumentException ignored) {
            }
        }
        synchronized (lock) {
            scheduleBind();
        }
    }
    private static void disconnected(long expected) {
        synchronized (lock) {
            if (expected >= 0 && expected != generation)
                return;
            remote = null;
            brokerGeneration = 0;
            exportOperation = 0;
            generation++;
            lock.notifyAll();
            scheduleBind();
        }
    }
    private static void scheduleBind() {
        // A stale retiringBinder is cleared by the bounded retirement watch, so
        // this never returns forever on an unresolved retirement.
        if (context == null || retryScheduled || retiringBinder != null)
            return;
        long delay = budget.charge();
        if (delay < 0)
            // The budget is closed: a permanently failing policy or service
            // must not spawn and kill the broker process every few seconds
            // forever. initialize()/select()/status() can re-arm it explicitly.
            return;
        retryScheduled = true;
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
    /**
     * Explicit, bounded recovery from a transient bind failure. Called by
     * {@link #initialize}, by an explicit tree selection and by the status
     * refresh the UI performs; a permanently broken policy still stops after
     * {@link BindBudget#MAX_ATTEMPTS} attempts plus at most one manual re-arm per
     * stability window, so no unbounded spawn/kill loop is possible.
     */
    static void retryBinding() {
        synchronized (lock) {
            retryBindingLocked();
        }
    }
    // Caller holds lock.
    private static void retryBindingLocked() {
        if (context == null || remote != null || retiringBinder != null || retryScheduled)
            return;
        if (!budget.rearm(android.os.SystemClock.elapsedRealtime()))
            return;
        scheduleBind();
    }
    /**
     * Install the application context and begin a fresh bind episode.
     *
     * <p>Called from {@link RouterService#onCreate()} on the main thread. It
     * only records the context and schedules a broker bind; library loading and
     * the native bridge installation stay on the daemon start worker. Resetting
     * the retry budget here means a transient bind failure cannot keep SAF
     * storage disabled for the rest of the process lifetime.</p>
     */
    static void initialize(Context app) {
        synchronized (lock) {
            if (context == null)
                context = app.getApplicationContext();
            // Every daemon start begins a fresh bind episode, so a transient
            // bind failure never keeps SAF storage disabled for the rest of the
            // process lifetime.
            budget.reset();
        }
        retryBinding();
        // Library load and bridge installation run on the daemon start worker,
        // not on the service main thread. Binding alone only starts the broker.
    }
    /**
     * Persist a torrent-to-tree mapping after obtaining at least read access.
     * A read-only grant may seed existing content, but cannot authorize writes.
     * Existing mappings sharing a URI keep their grant when this one changes.
     *
     * @return whether the grant and mapping were stored successfully
     */
    static boolean select(Context app, String tunnel, String hash, Uri tree) {
        if (tunnel == null || tunnel.isEmpty() || tunnel.length() > 1024 || hash == null
                || !hash.matches("[0-9a-fA-F]{40}") || tree == null
                || !"content".equals(tree.getScheme()) || !DocumentsContract.isTreeUri(tree))
            return false;
        try {
            final int readFlag = Intent.FLAG_GRANT_READ_URI_PERMISSION;
            final int writeFlag = Intent.FLAG_GRANT_WRITE_URI_PERMISSION;
            int flags = readFlag | writeFlag;
            try {
                app.getContentResolver().takePersistableUriPermission(tree, flags);
            } catch (SecurityException writeDenied) {
                // A provider may expose a read-only tree. Seeding existing
                // documents only needs read authority; creation/download then
                // fails closed at the broker's writable-root check.
                flags = readFlag;
                app.getContentResolver().takePersistableUriPermission(tree, flags);
            }
            boolean read = false, write = false;
            for (android.content.UriPermission grant :
                    app.getContentResolver().getPersistedUriPermissions()) {
                if (!grant.getUri().equals(tree))
                    continue;
                read |= grant.isReadPermission();
                write |= grant.isWritePermission();
            }
            if (!read) {
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
                // An explicit selection is the operator's recovery action: it
                // begins a fresh bind episode without an application restart.
                budget.reset();
            }
            retryBinding();
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

    /**
     * Remove one persisted mapping and release its tree permission only when
     * no remaining mapping references that URI. A failed preference commit
     * leaves the permission intact.
     *
     * @return whether removal of the mapping was committed
     */
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
    /**
     * True when any torrent-to-tree mapping is persisted.
     *
     * <p>Called from {@link DaemonWrapper}'s start worker through the native
     * bridge, never from the main thread. A mapping counts even when its grant
     * was revoked or the broker is unavailable: the torrent stays selected and
     * fails closed instead of reverting to private filesystem storage.</p>
     */
    static boolean hasAnyGrant() {
        return context != null
                && !context.getSharedPreferences("torrent-saf-grants", Context.MODE_PRIVATE)
                            .getAll()
                            .isEmpty();
    }
    /**
     * Whether the native backend must treat this torrent as provider-backed.
     *
     * <p>This is the fail-closed answer for native callers: a missing context
     * reports {@code true} because claiming "not configured" would let a
     * selected torrent fall back to an ordinary filesystem path. A missing
     * grant then makes {@link #root(String, String)} return zero, so the
     * payload stays unavailable rather than silently changing storage
     * authority.</p>
     *
     * @param tunnel tunnel index as a decimal string
     * @param hash   lowercase or uppercase 40-character infohash
     */
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
    /**
     * Create or return the local tree capability for one torrent's grant.
     *
     * <p>A local ID is stable across broker replacements; the remote root ID is
     * cached per binding generation and re-authorized after a reconnect. The
     * cap {@link TorrentSafStorageService#MAX_ROOTS} and an exhausted ID space
     * both answer zero, and a torrent without a persisted grant gets no
     * capability at all.</p>
     *
     * @return a non-zero local tree ID, or {@code 0} when unavailable
     */
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
            if (uri == null || roots.size() >= TorrentSafStorageService.MAX_ROOTS
                    || nextId == Long.MAX_VALUE)
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
    /**
     * Create a local file capability and open it through the current broker.
     *
     * @param root local tree capability returned by {@link #root(String, String)}
     * @param relative path below that tree, validated by the broker
     * @param length expected file length in bytes
     * @param create whether the broker may create or extend the document
     * @return a nonzero local handle, or zero if opening fails
     */
    public static long open(long root, String relative, long length, boolean create) {
        long id;
        synchronized (lock) {
            if (!roots.containsKey(root) || files.size() >= TorrentSafStorageService.MAX_HANDLES
                    || nextId == Long.MAX_VALUE)
                return 0;
            id = nextId++;
            files.put(id, new File(root, relative, length, create));
        }
        if (!Boolean.TRUE.equals(run(session -> fileId(session, id) != 0, false).value())) {
            synchronized (lock) {
                files.remove(id);
            }
            return 0;
        }
        return id;
    }
    /**
     * Read an exact bounded range through the broker.
     * @return exactly {@code count} bytes, or {@code null} on refusal or failure
     */
    public static byte[] read(long handle, long offset, int count) {
        if (offset < 0 || count < 0 || count > 16384)
            return null;
        Outcome<byte[]> outcome = run(session -> {
            long id = fileId(session, handle);
            return id == 0 ? null : session.remote.read(id, offset, count, session.operation);
        }, null);
        byte[] result = outcome.value();
        if (result != null && result.length == count)
            return result;
        if (result == null && retryable(outcome.failure()))
            // Busy queue or client deadline: keep the capability and let the
            // caller retry. Native invalidation remains caller-driven, so a
            // queued read never destroys a piece.
            return null;
        invalidate(handle);
        return null;
    }
    /**
     * Write a bounded range; the copied buffer belongs to this transaction.
     * Ambiguous failed writes are reported to the engine and are not replayed.
     * @return whether the broker completed the write
     */
    public static boolean write(long handle, long offset, byte[] bytes) {
        if (offset < 0 || bytes == null || bytes.length > 16384)
            return false;
        byte[] owned = bytes.clone();
        // A write interrupted by death or a broker-side deadline is ambiguous:
        // return failure and let the engine invalidate/reverify the piece. Never
        // replay it here, and never invalidate it for a busy local queue.
        Outcome<Boolean> outcome = run(session -> {
            long id = fileId(session, handle);
            return id != 0 && session.remote.write(id, offset, owned, session.operation);
        }, false);
        if (Boolean.TRUE.equals(outcome.value()))
            return true;
        if (retryable(outcome.failure()))
            return false;
        invalidate(handle);
        return false;
    }
    static boolean export(Uri target, byte[] metadata, AtomicBoolean canceled) {
        if (target == null || !"content".equals(target.getScheme()) || metadata == null
                || metadata.length == 0 || metadata.length > 8 * 1024 * 1024)
            return false;
        byte[] owned = metadata.clone();
        exportCanceled.set(false);
        // Each chunk is its own bounded Binder operation. A multi-megabyte
        // export must not consume one broker deadline and then destroy every
        // unrelated torrent handle.
        // The truncating open is authorized by a one-shot capability that binds
        // this caller, this URI, this length and this document identity.
        long token = exportCall(session
                -> session.remote.prepareExport(target.toString(), owned.length, session.operation),
                0L);
        if (token == 0)
            return false;
        long handle =
                exportCall(session -> session.remote.openExport(token, session.operation), 0L);
        if (handle == 0)
            return false;
        boolean committed = false;
        try {
            for (int offset = 0; offset < owned.length; offset += 16384) {
                if (canceled.get() || exportCanceled.get())
                    return false;
                final int at = offset;
                byte[] chunk =
                        java.util.Arrays.copyOfRange(owned, at, Math.min(owned.length, at + 16384));
                if (!exportCall(session
                            -> session.remote.write(handle, at, chunk, session.operation),
                            false))
                    return false;
            }
            committed = exportCall(
                    session -> session.remote.finishExport(handle, true, session.operation), false);
            return committed;
        } finally {
            if (!committed) {
                try {
                    exportCall(session
                            -> session.remote.finishExport(handle, false, session.operation),
                            false);
                } catch (RuntimeException ignored) {
                }
            }
        }
    }
    /**
     * Cancels the metadata export in progress and nothing else. Payload reads
     * and writes belong to the native engine, which owns their invalidation, so
     * an administrative "cancel export" must never abort a piece transfer.
     */
    static void cancelPending() {
        exportCanceled.set(true);
        Session session;
        long operation;
        synchronized (lock) {
            operation = exportOperation;
            if (operation == 0 || remote == null)
                return;
            session = new Session(remote, generation, operation);
        }
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
        // Deletion is size-independent: after a failed or partial export/write
        // the document no longer matches the declared size and openFile() would
        // refuse to resolve it. The relative resolution still requires the
        // persisted tree grant, the exact document identity and the same caller.
        // `length` remains part of the native call contract; it is not trusted.
        return Boolean.TRUE.equals(run(session -> {
            long id = rootId(session, root);
            return id != 0 && session.remote.deleteByRelative(id, path, session.operation);
        }, false).value());
    }
    /**
     * Removes one empty directory below a tree root. Prepared for engine cleanup
     * of a fully removed torrent; the broker refuses a directory with children.
     */
    static boolean removeEmptyDirectory(long root, String path) {
        if (!TorrentSafStorageService.validPath(path))
            return false;
        return Boolean.TRUE.equals(run(session -> {
            long id = rootId(session, root);
            return id != 0 && session.remote.removeEmptyDirectory(id, path, session.operation);
        }, false).value());
    }
    /**
     * Human-readable payload-storage state for the torrent list.
     *
     * <p>Strings are fixed diagnostics (no URI, document name or account data)
     * and double as the bounded UI recovery path: an exhausted bind budget is
     * re-armed at most once per stability window, so a refresh can recover a
     * transient failure without letting a broken provider spawn and kill a
     * broker on every tick. Reports "native private storage" when the torrent
     * has no mapping.</p>
     */
    static String status(String tunnel, String hash) {
        synchronized (lock) {
            if (context == null)
                return "Storage bridge unavailable";
            String uri = context.getSharedPreferences("torrent-saf-grants", Context.MODE_PRIVATE)
                                 .getString(tunnel + ":" + hash.toLowerCase(java.util.Locale.ROOT),
                                         null);
            if (uri == null)
                return "Native private storage";
            boolean readPermission = false, writePermission = false;
            for (android.content.UriPermission grant :
                    context.getContentResolver().getPersistedUriPermissions())
                if (grant.getUri().toString().equals(uri)) {
                    readPermission |= grant.isReadPermission();
                    writePermission |= grant.isWritePermission();
                }
            if (!readPermission)
                return "Document permission revoked. Select the directory again.";
            if (!writePermission)
                return "SAF storage is read-only; existing data can seed but downloads need write "
                       + "access.";
            if (retiringBinder != null)
                return "Storage provider cancellation pending; waiting for broker termination";
            if (budget.exhausted()) {
                // A status refresh is the bounded UI recovery path: a transient
                // bind failure must not require an application restart, while
                // the stability floor keeps a broken policy from spawning and
                // killing a broker on every refresh.
                retryBindingLocked();
                if (budget.exhausted())
                    return "Storage broker unavailable; reselect the directory or restart the app.";
            }
            if (remote == null)
                return "Storage broker reconnecting. Failed pieces will be verified before reuse.";
            return "SAF storage connected; random-access provider required";
        }
    }
    /**
     * Drops a local capability whose broker descriptor can no longer be used.
     * Only an authoritative failure reaches this path: a busy local queue or a
     * client deadline keeps the capability and leaves the piece to the caller.
     */
    private static void invalidate(long handle) {
        File file;
        Session session;
        long remoteId;
        synchronized (lock) {
            file = files.remove(handle);
            if (file == null || file.remoteId == 0 || remote == null
                    || file.generation != generation)
                return;
            remoteId = file.remoteId;
            session = sessionLocked();
        }
        if (session == null)
            return;
        final long id = remoteId;
        execute(session, current -> {
            current.remote.closeFile(id);
            return true;
        }, false, android.os.SystemClock.elapsedRealtime() + OPERATION_TIMEOUT_MS, false);
    }
    public static void close(long handle) {
        Session session;
        long remoteId;
        synchronized (lock) {
            File file = files.remove(handle);
            // Local teardown must never wait for a broker which no longer owns
            // this descriptor, or trigger recovery just to close a dead token.
            if (file == null || remote == null || file.generation != generation
                    || file.remoteId == 0)
                return;
            remoteId = file.remoteId;
            session = sessionLocked();
        }
        if (session == null)
            return;
        final long id = remoteId;
        execute(session, current -> {
            current.remote.closeFile(id);
            return true;
        }, false, android.os.SystemClock.elapsedRealtime() + OPERATION_TIMEOUT_MS, false);
    }
    public static void release(long root) {
        Session session;
        long remoteId;
        synchronized (lock) {
            Root entry = roots.remove(root);
            authorized.entrySet().removeIf(item -> item.getValue() == root);
            files.entrySet().removeIf(item -> item.getValue().root == root);
            if (entry == null || remote == null || entry.generation != generation
                    || entry.remoteId == 0)
                return;
            remoteId = entry.remoteId;
            session = sessionLocked();
        }
        if (session == null)
            return;
        final long id = remoteId;
        execute(session, current -> {
            current.remote.revokeTree(id);
            return true;
        }, false, android.os.SystemClock.elapsedRealtime() + OPERATION_TIMEOUT_MS, false);
    }
    // Caller holds lock. Tokens are never reused, including across generations.
    private static Session sessionLocked() {
        if (nextOperation == Long.MAX_VALUE)
            return null;
        return new Session(remote, generation, nextOperation++);
    }
    private static <T> T call(Operation<T> action, T fallback) {
        return run(action, fallback).value();
    }
    /**
     * Runs one metadata-export transaction and publishes its token so that
     * cancelPending() can abort exactly this export and nothing else.
     */
    private static <T> T exportCall(Operation<T> action, T fallback) {
        return call(session -> {
            synchronized (lock) {
                exportOperation = session.operation;
            }
            try {
                if (exportCanceled.get())
                    return fallback;
                return action.run(session);
            } finally {
                synchronized (lock) {
                    if (exportOperation == session.operation)
                        exportOperation = 0;
                }
            }
        }, fallback);
    }
    private static <T> Outcome<T> run(Operation<T> action, T fallback) {
        Session session;
        long deadline = android.os.SystemClock.elapsedRealtime() + BIND_WAIT_MS;
        synchronized (lock) {
            while (remote == null) {
                scheduleBind();
                long remaining = deadline - android.os.SystemClock.elapsedRealtime();
                if (remaining <= 0
                        || android.os.Looper.myLooper() == android.os.Looper.getMainLooper())
                    return Outcome.of(fallback, Failure.UNAVAILABLE);
                try {
                    lock.wait(remaining);
                } catch (InterruptedException interrupted) {
                    Thread.currentThread().interrupt();
                    return Outcome.of(fallback, Failure.UNAVAILABLE);
                }
            }
            session = sessionLocked();
        }
        if (session == null)
            return Outcome.of(fallback, Failure.BUSY);
        return execute(session, action, fallback,
                android.os.SystemClock.elapsedRealtime() + OPERATION_TIMEOUT_MS, true);
    }
    /**
     * One bounded transaction on the single IPC thread. Only death or an
     * unusable frame may replace the broker: a busy local queue, a slow provider
     * and a client deadline return a typed retryable fallback.
     */
    private static <T> Outcome<T> execute(
            Session session, Operation<T> action, T fallback, long deadline, boolean recover) {
        java.util.concurrent.Future<Outcome<T>> future = null;
        try {
            if (deadline - android.os.SystemClock.elapsedRealtime() <= 0)
                return Outcome.of(fallback, Failure.TIMEOUT);
            future = calls.submit(() -> {
                synchronized (lock) {
                    if (generation != session.generation || remote != session.remote)
                        return Outcome.of(fallback, Failure.UNAVAILABLE);
                }
                // Never send a payload frame before the broker identity for this
                // binding has been authenticated.
                if (!ensureBroker(session))
                    return Outcome.of(fallback, Failure.PROTOCOL);
                return Outcome.of(action.run(session), Failure.NONE);
            });
            long remaining = deadline - android.os.SystemClock.elapsedRealtime();
            if (remaining <= 0)
                throw new java.util.concurrent.TimeoutException();
            Outcome<T> outcome = future.get(remaining, TimeUnit.MILLISECONDS);
            synchronized (lock) {
                if (generation != session.generation || remote != session.remote)
                    return Outcome.of(fallback, Failure.UNAVAILABLE);
                budget.answered(android.os.SystemClock.elapsedRealtime());
            }
            return outcome;
        } catch (java.util.concurrent.RejectedExecutionException busy) {
            // The IPC thread is occupied by an earlier call. Answer with the
            // typed fallback and let the caller retry; nothing is retired.
            return Outcome.of(fallback, Failure.BUSY);
        } catch (java.util.concurrent.TimeoutException timeout) {
            if (future != null)
                future.cancel(true);
            // Advisory cancellation only. The broker owns its own deadlines and
            // process lifetime; the client must not retire over a slow provider.
            cancelOperation(session);
            return Outcome.of(fallback, Failure.TIMEOUT);
        } catch (java.util.concurrent.ExecutionException failed) {
            if (future != null)
                future.cancel(true);
            Throwable cause = failed.getCause();
            boolean ambiguous = cause instanceof android.os.RemoteException
                    || !session.remote.asBinder().isBinderAlive();
            if (ambiguous && recover)
                // Death or an unmarshallable frame is the only ambiguity that
                // justifies replacing the broker process.
                retire(session);
            return Outcome.of(fallback, ambiguous ? Failure.PROTOCOL : Failure.UNAVAILABLE);
        } catch (InterruptedException interrupted) {
            if (future != null)
                future.cancel(true);
            Thread.currentThread().interrupt();
            return Outcome.of(fallback, Failure.UNAVAILABLE);
        }
    }
    private static void cancelOperation(Session session) {
        try {
            session.remote.cancel(session.operation);
        } catch (android.os.RemoteException | RuntimeException ignored) {
        }
    }
    private TorrentSafStorage() {
    }
}

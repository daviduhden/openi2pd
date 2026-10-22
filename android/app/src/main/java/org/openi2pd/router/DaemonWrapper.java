/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: DaemonWrapper.
 *
 * Process-wide owner of the native daemon: asset extraction into app-specific
 * storage, start on a dedicated thread, stop, restart, tunnel reload, netDb
 * reseed, runtime log level, the graceful-shutdown deadline and connectivity
 * callbacks forwarded to JNI.
 * State transitions are lock-guarded and listeners are invoked after the
 * monitor is released, so no Activity or Service reference is retained.
 *
 */

package org.openi2pd.router;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileOutputStream;
import java.io.FileReader;
import java.io.FileWriter;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.ArrayList;
import java.util.Collections;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Objects;
import java.util.Set;
import java.util.concurrent.Executor;
import java.util.concurrent.RejectedExecutionException;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicLong;

import android.content.Context;
import android.content.res.AssetManager;
import android.net.ConnectivityManager;
import android.net.Network;
import android.net.NetworkCapabilities;
import android.util.Log;

/**
 * Process-wide owner of the native i2pd daemon.
 *
 * <p>The wrapper deliberately keeps no reference to an Activity or Service. It
 * is created with the application context and may outlive any UI component.
 * All mutable state is guarded by {@link #lock}; listeners are copied while the
 * lock is held and invoked after it is released, so a listener cannot deadlock
 * against another component's monitor.</p>
 */
public class DaemonWrapper {
    private static final String TAG = "open-i2pd";
    private static final String DATA_DIR_NAME = "open-i2pd";
    private static final int COPY_BUFFER_SIZE = 8192;

    /**
     * Runtime log levels accepted by the native logger, in severity order.
     * The native {@code setRuntimeLogLevel} entry point validates the same
     * exact, case-sensitive closed set before it changes any state.
     */
    private static final String[] RUNTIME_LOG_LEVELS = {
            "none", "critical", "error", "warn", "info", "debug"};

    /**
     * Time transit tunnels are given to drain during a graceful shutdown.
     */
    public static final long GRACEFUL_STOP_DELAY_MILLIS = 10L * 60L * 1000L;

    public interface StateUpdateListener {
        void daemonStateUpdate(State oldValue, State newValue);
    }

    private record
            StateChange(State oldState, State newState, List<StateUpdateListener> listeners) {}

    public enum State {
        uninitialized(R.string.uninitialized),
        starting(R.string.starting),
        startedOkay(R.string.startedOkay),
        startFailed(R.string.startFailed),
        gracefulShutdownInProgress(R.string.gracefulShutdownInProgress),
        stopping(R.string.state_stopping),
        stopped(R.string.stopped);

        private final int statusStringResourceId;

        State(int statusStringResourceId) {
            this.statusStringResourceId = statusStringResourceId;
        }

        public int getStatusStringResourceId() {
            return statusStringResourceId;
        }

        public boolean isStartedOkay() {
            return switch (this) {
                case startedOkay, gracefulShutdownInProgress -> true;
                default -> false;
            };
        }
    }

    private final Object lock = new Object();
    private final AssetManager assetManager;
    private final ConnectivityManager connectivityManager;
    private final List<File> appStorageRoots;
    private final File torrentStorage;
    private final Set<StateUpdateListener> stateUpdateListeners = new LinkedHashSet<>();
    private final Object assetsLock = new Object();
    private final AtomicBoolean stopRequested = new AtomicBoolean(false);
    private final AtomicLong gracefulStopDeadlineMillis = new AtomicLong(0L);

    /**
     * Connectivity callbacks are delivered on the main thread, while the native
     * update takes the native-call lock that start, stop and reseed hold. This
     * dispatcher keeps that JNI call off the UI thread and preserves callback
     * order, so a reseed that runs for minutes cannot turn a network change
     * into an ANR. {@link RouterService} passes its serial control worker; the
     * inline fallback preserves the previous behaviour for other callers.
     */
    private final Executor networkStateExecutor;

    private volatile File dataDir;
    private volatile State state = State.uninitialized;
    private volatile Throwable lastThrowable;
    private volatile String daemonStartResult = "N/A";
    private volatile boolean startThreadRunning;
    private volatile boolean networkCallbackRegistered;

    private final ConnectivityManager.NetworkCallback networkCallback =
            new ConnectivityManager.NetworkCallback() {
                @Override
                public void onAvailable(Network network) {
                    super.onAvailable(network);
                    notifyNetworkState(true);
                    Log.d(TAG, "network available");
                }

                @Override
                public void onLost(Network network) {
                    super.onLost(network);
                    notifyNetworkState(false);
                    Log.d(TAG, "network lost");
                }

                @Override
                public void onCapabilitiesChanged(
                        Network network, NetworkCapabilities capabilities) {
                    super.onCapabilitiesChanged(network, capabilities);
                    notifyNetworkState(capabilities != null
                            && capabilities.hasCapability(
                                    NetworkCapabilities.NET_CAPABILITY_INTERNET));
                }
            };

    public DaemonWrapper(
            Context context, AssetManager assetManager, ConnectivityManager connectivityManager) {
        this(context, assetManager, connectivityManager, true, null);
    }

    /**
     * @param autoStart when {@code false} the wrapper stays in
     *                  {@link State#stopped} until the owner calls
     *                  {@link #startDaemon()}. {@link RouterService} uses
     *                  this so a stopped router does not spring back to life
     *                  merely because the UI bound the service.
     */
    public DaemonWrapper(Context context, AssetManager assetManager,
            ConnectivityManager connectivityManager, boolean autoStart) {
        this(context, assetManager, connectivityManager, autoStart, null);
    }

    /**
     * @param networkStateExecutor serial executor for the connectivity update.
     *                             Connectivity callbacks arrive on the main
     *                             thread; {@link RouterService} passes its
     *                             control worker so the blocking native call
     *                             never runs on the UI thread. {@code null}
     *                             keeps the inline behaviour for other callers.
     */
    public DaemonWrapper(Context context, AssetManager assetManager,
            ConnectivityManager connectivityManager, boolean autoStart,
            Executor networkStateExecutor) {
        Objects.requireNonNull(context, "context");
        Context appContext = context.getApplicationContext();
        if (appContext == null) {
            appContext = context;
        }

        this.assetManager = assetManager != null ? assetManager : appContext.getAssets();
        this.connectivityManager = connectivityManager;
        this.networkStateExecutor =
                networkStateExecutor != null ? networkStateExecutor : Runnable::run;

        File external = appContext.getExternalFilesDir(null);
        this.dataDir = defaultDataDir(appContext);

        List<File> roots = new ArrayList<>(2);
        File internalFiles = appContext.getFilesDir();
        this.torrentStorage =
                new File(Objects.requireNonNull(internalFiles, "internal files"), "torrents");
        if (internalFiles != null) {
            roots.add(internalFiles);
        }
        if (external != null) {
            roots.add(external);
        }
        this.appStorageRoots = Collections.unmodifiableList(roots);

        if (autoStart) {
            setState(State.starting);
            startDaemonInternal();
        } else {
            setState(State.stopped);
        }
    }

    /**
     * Default app-private data directory. Shared with {@link RouterConfigStore}
     * so configuration edits and the native router always agree on the path.
     */
    static File defaultDataDir(Context context) {
        Objects.requireNonNull(context, "context");
        Context appContext = context.getApplicationContext();
        if (appContext == null) {
            appContext = context;
        }
        File external = appContext.getExternalFilesDir(null);
        File base = external != null ? external : appContext.getFilesDir();
        return new File(base, DATA_DIR_NAME).getAbsoluteFile();
    }

    public void addStateChangeListener(StateUpdateListener listener) {
        if (listener == null) {
            return;
        }
        synchronized (lock) {
            stateUpdateListeners.add(listener);
        }
    }

    public void removeStateChangeListener(StateUpdateListener listener) {
        if (listener == null) {
            return;
        }
        synchronized (lock) {
            stateUpdateListeners.remove(listener);
        }
    }

    public State getState() {
        return state;
    }

    public boolean isStartedOkay() {
        return state.isStartedOkay();
    }

    public Throwable getLastThrowable() {
        return lastThrowable;
    }

    public String getDaemonStartResult() {
        return daemonStartResult;
    }

    public static String getDataDir() { // for the settings INI editor
        return I2PD_JNI.getDataDir();
    }

    /**
     * Resolve a file inside the daemon data directory, refusing paths that
     * escape the application's own storage.
     */
    public static File resolveDataFile(Context context, String fileName) throws IOException {
        Objects.requireNonNull(context, "context");
        Objects.requireNonNull(fileName, "fileName");
        String configuredDir = I2PD_JNI.getDataDir();
        if (configuredDir == null || configuredDir.trim().isEmpty()) {
            throw new IOException("daemon data directory is not initialised");
        }

        File base = new File(configuredDir).getCanonicalFile();
        Context appContext = context.getApplicationContext();
        if (appContext == null) {
            appContext = context;
        }
        if (!isInsideAppStorage(appContext, base)) {
            throw new IOException("daemon data directory is outside app-specific storage");
        }

        File file = new File(base, fileName).getCanonicalFile();
        if (!base.equals(file.getParentFile())) {
            throw new IOException("refusing file outside the daemon data directory");
        }
        return file;
    }

    private static boolean isInsideAppStorage(Context context, File candidate) throws IOException {
        String candidatePath = candidate.getCanonicalPath();
        List<File> roots = new ArrayList<>(2);
        File internal = context.getFilesDir();
        if (internal != null) {
            roots.add(internal);
        }
        File external = context.getExternalFilesDir(null);
        if (external != null) {
            roots.add(external);
        }
        for (File root : roots) {
            String rootPath = root.getCanonicalPath();
            if (candidatePath.equals(rootPath)
                    || candidatePath.startsWith(rootPath + File.separator)) {
                return true;
            }
        }
        return false;
    }

    /**
     * Change the native data directory. Only app-specific storage is accepted.
     * Returns {@code false} when the path is rejected.
     */
    public boolean changeDataDir(String newDataDir, Boolean updateAssets) {
        if (newDataDir == null || newDataDir.trim().isEmpty()) {
            Log.w(TAG, "refusing empty data directory");
            return false;
        }
        final File candidate;
        try {
            candidate = new File(newDataDir).getCanonicalFile();
            if (!isInsideOwnStorage(candidate)) {
                Log.w(TAG, "refusing data directory outside app-specific storage");
                return false;
            }
        } catch (IOException e) {
            Log.w(TAG, "refusing invalid data directory", e);
            return false;
        }

        try {
            synchronized (lock) {
                I2PD_JNI.setDataDir(candidate.getAbsolutePath());
                dataDir = candidate;
            }
        } catch (RuntimeException | LinkageError e) {
            Log.e(TAG, "unable to set data directory", e);
            return false;
        }
        if (Boolean.TRUE.equals(updateAssets)) {
            processAssets();
        }
        return true;
    }

    private boolean isInsideOwnStorage(File candidate) throws IOException {
        String candidatePath = candidate.getCanonicalPath();
        for (File root : appStorageRoots) {
            String rootPath = root.getCanonicalPath();
            if (candidatePath.equals(rootPath)
                    || candidatePath.startsWith(rootPath + File.separator)) {
                return true;
            }
        }
        return false;
    }

    public synchronized void stopDaemon() {
        stopRequested.set(true);
        final boolean callNative;
        StateChange beginChange = null;
        StateChange finishChange = null;
        synchronized (lock) {
            cancelGracefulStopLocked();
            callNative = state.isStartedOkay();
            beginChange = setStateLocked(callNative ? State.stopping : State.stopped);
        }
        // Publish Stopping before entering native teardown. A status poll
        // that cannot take the native-call lock can then say that the router
        // is going away instead of reading a stale Running state.
        notifyListeners(beginChange);
        if (callNative) {
            try {
                I2PD_JNI.stopDaemon();
            } catch (Throwable tr) {
                Log.e(TAG, "native stop failed", tr);
            }
            synchronized (lock) {
                finishChange = setStateLocked(State.stopped);
            }
        }
        unregisterNetworkCallback();
        notifyListeners(finishChange);
    }

    public void startDaemon() {
        startDaemonInternal();
    }

    /**
     * Stop and start the native router in order. Used by the service's restart
     * command. The start thread is allowed to finish unwinding before a new
     * start is requested so two native initialisations can never overlap.
     *
     * @return {@code false} if a previous start thread did not finish in time
     */
    public boolean restartDaemon() {
        stopDaemon();
        long deadline = System.currentTimeMillis() + 10_000L;
        while (startThreadRunning && System.currentTimeMillis() < deadline) {
            try {
                Thread.sleep(25L);
            } catch (InterruptedException e) {
                Thread.currentThread().interrupt();
                Log.w(TAG, "interrupted while waiting for daemon start thread");
                return false;
            }
        }
        if (startThreadRunning) {
            Log.e(TAG, "refusing restart while the previous start is still running");
            return false;
        }
        startDaemon();
        return true;
    }

    private void startDaemonInternal() {
        StateChange change;
        synchronized (lock) {
            if (startThreadRunning) {
                return;
            }
            if (state != State.stopped && state != State.starting && state != State.startFailed) {
                return;
            }
            startThreadRunning = true;
            stopRequested.set(false);
            // Do not let a previous successful result mask a failure in this
            // start attempt (for example an asset or library load error).
            daemonStartResult = "N/A";
            change = setStateLocked(State.starting);
        }
        notifyListeners(change);

        Thread thread = new Thread(this::startDaemonBlocking, "i2pdDaemonStart");
        thread.setDaemon(true);
        thread.start();
    }

    private void startDaemonBlocking() {
        lastThrowable = null;
        StateChange change = null;
        try {
            processAssets();
            prepareTorrentSession();
            I2PD_JNI.loadLibraries();
            if (!I2PD_JNI.installSafStorageBridge(TorrentSafStorage.class)
                    && TorrentSafStorage.hasAnyGrant())
                throw new IllegalStateException("SAF storage bridge initialization failed");
        } catch (Throwable tr) {
            lastThrowable = tr;
            synchronized (lock) {
                change = setStateLocked(stopRequested.get() ? State.stopped : State.startFailed);
                startThreadRunning = false;
            }
            notifyListeners(change);
            return;
        }

        boolean started = false;
        try {
            synchronized (lock) {
                if (!stopRequested.get()) {
                    I2PD_JNI.setDataDir(dataDir.getAbsolutePath());

                    String result = I2PD_JNI.startDaemon();
                    daemonStartResult = result;
                    started = "ok".equals(result) && !stopRequested.get();
                    change = setStateLocked(started       ? State.startedOkay
                                    : stopRequested.get() ? State.stopped
                                                          : State.startFailed);
                    if (!started && "ok".equals(result)) {
                        try {
                            I2PD_JNI.stopDaemon();
                        } catch (Throwable tr) {
                            Log.e(TAG, "native stop after cancelled start failed", tr);
                        }
                    }
                } else {
                    change = setStateLocked(State.stopped);
                }
            }
        } catch (Throwable tr) {
            lastThrowable = tr;
            synchronized (lock) {
                change = setStateLocked(stopRequested.get() ? State.stopped : State.startFailed);
            }
        } finally {
            synchronized (lock) {
                startThreadRunning = false;
            }
        }

        notifyListeners(change);
        if (started) {
            registerNetworkCallback();
        }
    }

    public void reloadTunnelsConfigs() {
        if (state.isStartedOkay()) {
            I2PD_JNI.reloadTunnelsConfigs();
        }
    }

    /** Bounded address-book rows; empty when the daemon is not running. */
    public String getAddressBook() {
        synchronized (lock) {
            if (!state.isStartedOkay())
                return "";
        }
        try {
            return I2PD_JNI.getAddressBook();
        } catch (RuntimeException | LinkageError e) {
            Log.e(TAG, "native address-book read failed", e);
            return "";
        }
    }

    /** Add or replace one validated name; false on any validation failure. */
    public boolean addAddress(String name, String destination) {
        synchronized (lock) {
            if (!state.isStartedOkay())
                return false;
        }
        try {
            return I2PD_JNI.addAddress(name, destination);
        } catch (RuntimeException | LinkageError e) {
            Log.e(TAG, "native address-book add failed", e);
            return false;
        }
    }

    /** Remove one name; false when absent or invalid. */
    public boolean removeAddress(String name) {
        synchronized (lock) {
            if (!state.isStartedOkay())
                return false;
        }
        try {
            return I2PD_JNI.removeAddress(name);
        } catch (RuntimeException | LinkageError e) {
            Log.e(TAG, "native address-book remove failed", e);
            return false;
        }
    }

    /** Block a subscription name or wildcard pattern. */
    public boolean blockAddress(String name) {
        synchronized (lock) {
            if (!state.isStartedOkay())
                return false;
        }
        try {
            return I2PD_JNI.blockAddress(name);
        } catch (RuntimeException | LinkageError e) {
            Log.e(TAG, "native address-block failed", e);
            return false;
        }
    }

    /** Remove one blocked subscription pattern. */
    public boolean unblockAddress(String name) {
        synchronized (lock) {
            if (!state.isStartedOkay())
                return false;
        }
        try {
            return I2PD_JNI.unblockAddress(name);
        } catch (RuntimeException | LinkageError e) {
            Log.e(TAG, "native address-unblock failed", e);
            return false;
        }
    }

    /** Bounded deleted-host patterns, one per line. */
    public String getBlockedAddresses() {
        synchronized (lock) {
            if (!state.isStartedOkay())
                return "";
        }
        try {
            return I2PD_JNI.getBlockedAddresses();
        } catch (RuntimeException | LinkageError e) {
            Log.e(TAG, "native blocked-address read failed", e);
            return "";
        }
    }

    /**
     * Exact, case-sensitive check against the closed runtime log level set.
     * Shared by {@link RouterService}'s binder and intent paths so an invalid
     * value is rejected before any native call is attempted.
     */
    public static boolean isRuntimeLogLevel(String level) {
        if (level == null) {
            return false;
        }
        for (String candidate : RUNTIME_LOG_LEVELS) {
            if (candidate.equals(level)) {
                return true;
            }
        }
        return false;
    }

    /**
     * Ask the running netDb to reseed from the configured reseed servers.
     *
     * <p>This is only valid while the daemon is started. The native call is
     * synchronous and single-flight, so the caller must be the service worker:
     * it can block for minutes on a poor connection and must stay ordered
     * against stop and reload commands.</p>
     *
     * @return {@code true} when the request was issued; {@code false} when the
     *         daemon is not started, a reseed is already running or the native
     *         call failed
     */
    public boolean requestReseed() {
        synchronized (lock) {
            if (!state.isStartedOkay()) {
                return false;
            }
        }
        try {
            return I2PD_JNI.requestReseed();
        } catch (RuntimeException | LinkageError e) {
            Log.e(TAG, "native reseed request failed", e);
            return false;
        }
    }

    /**
     * Apply a runtime log level to the running logger without restarting the
     * daemon. The value is validated here and again in native code; this path
     * never writes {@code i2pd.conf}, so persisting a preference stays a
     * separate, explicit step.
     *
     * @return {@code true} only when the running logger accepted the level
     */
    public boolean applyRuntimeLogLevel(String level) {
        if (!isRuntimeLogLevel(level)) {
            Log.w(TAG, "refusing unknown runtime log level");
            return false;
        }
        synchronized (lock) {
            if (!state.isStartedOkay()) {
                return false;
            }
        }
        try {
            return I2PD_JNI.setRuntimeLogLevel(level);
        } catch (RuntimeException | LinkageError e) {
            Log.e(TAG, "native log level update failed", e);
            return false;
        }
    }

    public int getTransitTunnelsCount() {
        return I2PD_JNI.getTransitTunnelsCount();
    }

    /**
     * Start a graceful shutdown: stop accepting transit tunnels and give the
     * remaining ones time to drain. Scheduling belongs to RouterService so the
     * eventual native stop is enqueued on its worker; this wrapper owns only
     * the state and the deadline.
     *
     * @return the delay that the caller must schedule, or {@code 0} when the
     *         stop was already performed immediately
     */
    public long beginGracefulShutdown(long delayMillis) {
        StateChange change = null;
        boolean stopImmediately = false;
        long scheduledDelay = 0L;
        synchronized (lock) {
            if (!state.isStartedOkay()) {
                stopImmediately = true;
            } else {
                long deadline = gracefulStopDeadlineMillis.get();
                if (deadline != 0L) {
                    return Math.max(0L, deadline - System.currentTimeMillis());
                }

                change = setStateLocked(State.gracefulShutdownInProgress);
                I2PD_JNI.stopAcceptingTunnels();

                long delay = Math.max(0L, delayMillis);
                if (delay == 0L || getTransitTunnelsCount() <= 0) {
                    stopImmediately = true;
                } else {
                    gracefulStopDeadlineMillis.set(System.currentTimeMillis() + delay);
                    scheduledDelay = delay;
                }
            }
        }
        notifyListeners(change);
        if (stopImmediately) {
            stopDaemon();
        }
        return scheduledDelay;
    }

    public void cancelGracefulShutdown() {
        StateChange change = null;
        synchronized (lock) {
            cancelGracefulStopLocked();
            if (state == State.gracefulShutdownInProgress) {
                change = setStateLocked(State.startedOkay);
                I2PD_JNI.startAcceptingTunnels();
            }
        }
        notifyListeners(change);
    }

    public boolean isGracefulShutdownScheduled() {
        return gracefulStopDeadlineMillis.get() > 0L;
    }

    public long getGracefulStopRemainingMillis() {
        long deadline = gracefulStopDeadlineMillis.get();
        if (deadline == 0L) {
            return 0L;
        }
        return Math.max(0L, deadline - System.currentTimeMillis());
    }

    private void cancelGracefulStopLocked() {
        gracefulStopDeadlineMillis.set(0L);
    }

    private StateChange setState(State newState) {
        StateChange change;
        synchronized (lock) {
            change = setStateLocked(newState);
        }
        notifyListeners(change);
        return change;
    }

    private StateChange setStateLocked(State newState) {
        Objects.requireNonNull(newState, "newState");
        State oldState = state;
        if (oldState == newState) {
            return null;
        }
        state = newState;
        return new StateChange(oldState, newState, new ArrayList<>(stateUpdateListeners));
    }

    private void notifyListeners(StateChange change) {
        if (change == null) {
            return;
        }
        Log.d(TAG, "daemon state change: " + change.newState());
        for (StateUpdateListener listener : change.listeners()) {
            try {
                listener.daemonStateUpdate(change.oldState(), change.newState());
            } catch (Throwable tr) {
                Log.e(TAG, "exception in listener ignored", tr);
            }
        }
    }

    private void notifyNetworkState(boolean connected) {
        // Do not call into JNI before the daemon is up or after it stopped.
        if (!state.isStartedOkay()) {
            return;
        }
        // The callback arrives on the main thread; hand the blocking JNI call
        // to the serial dispatcher described above. The state is re-checked
        // there so a queued update cannot reach a router that has just stopped.
        try {
            networkStateExecutor.execute(() -> {
                if (!state.isStartedOkay()) {
                    return;
                }
                try {
                    I2PD_JNI.onNetworkStateChanged(connected);
                } catch (Throwable tr) {
                    Log.e(TAG, "native network state update failed", tr);
                }
            });
        } catch (RejectedExecutionException e) {
            Log.w(TAG, "network state update rejected after shutdown", e);
        }
    }

    private void registerNetworkCallback() {
        ConnectivityManager manager = connectivityManager;
        if (manager == null) {
            return;
        }
        synchronized (lock) {
            if (!state.isStartedOkay() || networkCallbackRegistered) {
                return;
            }
            try {
                manager.registerDefaultNetworkCallback(networkCallback);
                networkCallbackRegistered = true;
            } catch (RuntimeException e) {
                Log.w(TAG, "unable to register network callback", e);
            }
        }
    }

    private void unregisterNetworkCallback() {
        ConnectivityManager manager = connectivityManager;
        final boolean registered;
        synchronized (lock) {
            registered = networkCallbackRegistered;
            networkCallbackRegistered = false;
        }
        if (manager == null || !registered) {
            return;
        }
        try {
            manager.unregisterNetworkCallback(networkCallback);
        } catch (RuntimeException e) {
            Log.w(TAG, "unable to unregister network callback", e);
        }
    }

    /** Install a native session once, without enabling JSON-RPC or a web listener. */
    private void prepareTorrentSession() throws IOException {
        synchronized (assetsLock) {
            File directory = new File(dataDir, "tunnels.d");
            if (!directory.exists() && !directory.mkdirs()) {
                throw new IOException("Unable to create tunnel configuration directory");
            }
            if (isSymbolicLink(directory) || !isInsideDataDir(directory)) {
                throw new IOException("Unsafe torrent configuration directory");
            }
            // Piece storage stays app-private. SAF export/import requires a
            // separate bounded storage broker and is not emulated with paths.
            File downloads = torrentStorage;
            if (!downloads.exists() && !downloads.mkdirs()) {
                throw new IOException("Unable to create torrent storage directory");
            }
            if (isSymbolicLink(downloads)) {
                throw new IOException("Unsafe torrent storage directory");
            }
            File config = new File(directory, "AndroidTorrents.conf");
            String content = "# Copyright (c) 2026, David Uhden Collado\n"
                    + "# SPDX-License-Identifier: BSD-3-Clause\n"
                    + "[AndroidTorrents]\ntype = torrents\nkeys = android-torrents.dat\n"
                    + "torrentsdir = " + downloads.getAbsolutePath() + "\ndht = true\n";
            try {
                int openFlags = android.system.OsConstants.O_CREAT
                        | android.system.OsConstants.O_EXCL | android.system.OsConstants.O_WRONLY
                        | android.system.OsConstants.O_NOFOLLOW
                        | android.system.OsConstants.O_CLOEXEC;
                java.io.FileDescriptor descriptor =
                        android.system.Os.open(config.getAbsolutePath(), openFlags, 0600);
                try (FileOutputStream output = new FileOutputStream(descriptor)) {
                    output.write(content.getBytes(java.nio.charset.StandardCharsets.UTF_8));
                    output.getFD().sync();
                }
            } catch (android.system.ErrnoException existing) {
                if (existing.errno != android.system.OsConstants.EEXIST) {
                    throw new IOException("Unable to install torrent configuration", existing);
                }
                // Preserve user changes, including a deliberately disabled session.
                try {
                    if (!android.system.OsConstants.S_ISREG(
                                android.system.Os.lstat(config.getAbsolutePath()).st_mode)) {
                        throw new IOException("Unsafe torrent configuration file", existing);
                    }
                } catch (android.system.ErrnoException error) {
                    throw new IOException("Unable to inspect torrent configuration", error);
                }
            }
        }
    }

    private static boolean isSymbolicLink(File file) throws IOException {
        try {
            return android.system.OsConstants.S_ISLNK(
                    android.system.Os.lstat(file.getAbsolutePath()).st_mode);
        } catch (android.system.ErrnoException error) {
            throw new IOException("Unable to inspect torrent directory", error);
        }
    }

    private void processAssets() {
        synchronized (assetsLock) {
            File appPath = dataDir;
            Log.d(TAG, "checking app directory");
            if (!appPath.exists() && !appPath.mkdirs() && !appPath.isDirectory()) {
                Log.e(TAG, "unable to create app directory " + appPath);
                return;
            }

            File holderFile = new File(appPath, "assets.ready");
            String versionName = BuildConfig.VERSION_NAME;
            String assetVersion = "";
            if (holderFile.exists()) {
                try (BufferedReader reader = new BufferedReader(new FileReader(holderFile))) {
                    StringBuilder text = new StringBuilder();
                    String line;
                    while ((line = reader.readLine()) != null) {
                        text.append(line);
                    }
                    assetVersion = text.toString();
                } catch (IOException e) {
                    Log.e(TAG, "unable to read asset version marker", e);
                }
            }

            if (assetVersion.contains(versionName)) {
                return;
            }

            try {
                if (holderFile.exists() && !holderFile.delete()) {
                    Log.w(TAG, "unable to delete stale asset version marker");
                }
                deleteRecursive(new File(appPath, "certificates"));

                // Copy assets. Existing user-modified files are never overwritten.
                copyAsset("addressbook");
                copyAsset("certificates");
                copyAsset("tunnels.d");
                copyAsset("i2pd.conf");
                copyAsset("tunnels.conf");

                try (FileWriter writer = new FileWriter(holderFile)) {
                    writer.append(versionName);
                }
            } catch (IOException tr) {
                Log.e(TAG, "asset copying failed", tr);
            }
        }
    }

    /**
     * Copy the asset at the specified path to the app data directory. If the
     * asset is a directory, its contents are also copied.
     */
    private void copyAsset(String path) {
        if (path == null || path.contains("..")) {
            Log.e(TAG, "refusing suspicious asset path");
            return;
        }
        try {
            String[] contents = assetManager.list(path);

            // AssetManager.list() returns an empty array for files, not null.
            if (contents == null || contents.length == 0) {
                copyFileAsset(path);
                return;
            }

            File directory = new File(dataDir, path);
            if (!isInsideDataDir(directory)) {
                Log.e(TAG, "refusing asset path outside data directory");
                return;
            }
            if (!directory.mkdirs() && !directory.isDirectory()) {
                Log.e(TAG, "unable to create asset directory " + path);
                return;
            }

            for (String entry : contents) {
                copyAsset(path + '/' + entry);
            }
        } catch (IOException e) {
            Log.e(TAG, "asset directory copy failed for '" + path + "'", e);
        }
    }

    private void copyFileAsset(String path) {
        File file = new File(dataDir, path);
        if (!isInsideDataDir(file)) {
            Log.e(TAG, "refusing asset file outside data directory");
            return;
        }
        if (file.exists()) {
            return;
        }
        try (InputStream in = assetManager.open(path);
                OutputStream out = new FileOutputStream(file)) {
            byte[] buffer = new byte[COPY_BUFFER_SIZE];
            int read;
            while ((read = in.read(buffer)) != -1) {
                out.write(buffer, 0, read);
            }
        } catch (IOException e) {
            Log.e(TAG, "asset file copy failed for '" + path + "'", e);
        }
    }

    private boolean isInsideDataDir(File file) {
        try {
            String base = dataDir.getCanonicalPath();
            String candidate = file.getCanonicalPath();
            return candidate.equals(base) || candidate.startsWith(base + File.separator);
        } catch (IOException e) {
            return false;
        }
    }

    private void deleteRecursive(File fileOrDirectory) {
        if (fileOrDirectory == null || !fileOrDirectory.exists()
                || !isInsideDataDir(fileOrDirectory)) {
            return;
        }
        if (fileOrDirectory.isDirectory()) {
            File[] files = fileOrDirectory.listFiles();
            if (files != null) {
                for (File child : files) {
                    deleteRecursive(child);
                }
            }
        }
        if (!fileOrDirectory.delete() && fileOrDirectory.exists()) {
            Log.w(TAG, "unable to delete " + fileOrDirectory.getName());
        }
    }
}

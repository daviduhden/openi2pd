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
import java.util.Locale;
import java.util.Objects;
import java.util.Set;
import java.util.Timer;
import java.util.TimerTask;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicLong;

import android.content.Context;
import android.content.res.AssetManager;
import android.net.ConnectivityManager;
import android.net.Network;
import android.net.NetworkCapabilities;
import android.os.Build;
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
     * Time transit tunnels are given to drain during a graceful shutdown.
     */
    public static final long GRACEFUL_STOP_DELAY_MILLIS = 10L * 60L * 1000L;

    private static final String appLocale =
            Locale.getDefault().getDisplayLanguage(Locale.ENGLISH).toLowerCase(Locale.ROOT);

    public interface StateUpdateListener {
        void daemonStateUpdate(State oldValue, State newValue);
    }

    private record StateChange(State oldState, State newState, List<StateUpdateListener> listeners) {
    }

    public enum State {
        uninitialized(R.string.uninitialized),
        starting(R.string.starting),
        startedOkay(R.string.startedOkay),
        startFailed(R.string.startFailed),
        gracefulShutdownInProgress(R.string.gracefulShutdownInProgress),
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
    private final Set<StateUpdateListener> stateUpdateListeners = new LinkedHashSet<>();
    private final Object assetsLock = new Object();
    private final AtomicBoolean stopRequested = new AtomicBoolean(false);
    private final AtomicLong gracefulStopDeadlineMillis = new AtomicLong(0L);

    private volatile File dataDir;
    private volatile State state = State.uninitialized;
    private volatile Throwable lastThrowable;
    private volatile String daemonStartResult = "N/A";
    private volatile Timer gracefulStopTimer;
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
                public void onCapabilitiesChanged(Network network, NetworkCapabilities capabilities) {
                    super.onCapabilitiesChanged(network, capabilities);
                    notifyNetworkState(capabilities != null
                            && capabilities.hasCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET));
                }
            };

    public DaemonWrapper(Context context, AssetManager assetManager,
                         ConnectivityManager connectivityManager) {
        Objects.requireNonNull(context, "context");
        Context appContext = context.getApplicationContext();
        if (appContext == null) {
            appContext = context;
        }

        this.assetManager = assetManager != null ? assetManager : appContext.getAssets();
        this.connectivityManager = connectivityManager;

        File external = appContext.getExternalFilesDir(null);
        File base = external != null ? external : appContext.getFilesDir();
        this.dataDir = new File(base, DATA_DIR_NAME).getAbsoluteFile();

        List<File> roots = new ArrayList<>(2);
        File internalFiles = appContext.getFilesDir();
        if (internalFiles != null) {
            roots.add(internalFiles);
        }
        if (external != null) {
            roots.add(external);
        }
        this.appStorageRoots = Collections.unmodifiableList(roots);

        setState(State.starting);
        startDaemon();
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

        I2PD_JNI.setDataDir(candidate.getAbsolutePath());
        dataDir = candidate;
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
        StateChange change = null;
        synchronized (lock) {
            cancelGracefulStopLocked();
            callNative = state.isStartedOkay();
            if (!callNative) {
                change = setStateLocked(State.stopped);
            }
        }
        if (callNative) {
            try {
                I2PD_JNI.stopDaemon();
            } catch (Throwable tr) {
                Log.e(TAG, "native stop failed", tr);
            }
            synchronized (lock) {
                change = setStateLocked(State.stopped);
            }
        }
        unregisterNetworkCallback();
        notifyListeners(change);
    }

    public void startDaemon() {
        synchronized (lock) {
            if (startThreadRunning) {
                return;
            }
            if (state != State.stopped && state != State.starting) {
                return;
            }
            startThreadRunning = true;
            stopRequested.set(false);
        }

        Thread thread = new Thread(this::startDaemonBlocking, "i2pdDaemonStart");
        thread.setDaemon(true);
        thread.start();
    }

    private void startDaemonBlocking() {
        lastThrowable = null;
        StateChange change = null;
        try {
            processAssets();
            I2PD_JNI.loadLibraries();
        } catch (Throwable tr) {
            lastThrowable = tr;
            synchronized (lock) {
                change = setStateLocked(State.startFailed);
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
                    Log.i(TAG, "setting language to " + appLocale);
                    I2PD_JNI.setLanguage(appLocale);

                    String result = I2PD_JNI.startDaemon();
                    daemonStartResult = result;
                    started = "ok".equals(result) && !stopRequested.get();
                    change = setStateLocked(started ? State.startedOkay
                            : stopRequested.get() ? State.stopped : State.startFailed);
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
                change = setStateLocked(State.startFailed);
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

    public void stopAcceptingTunnels() {
        StateChange change;
        synchronized (lock) {
            if (!state.isStartedOkay()) {
                return;
            }
            change = setStateLocked(State.gracefulShutdownInProgress);
            I2PD_JNI.stopAcceptingTunnels();
        }
        notifyListeners(change);
    }

    public void startAcceptingTunnels() {
        StateChange change;
        synchronized (lock) {
            if (state != State.gracefulShutdownInProgress) {
                return;
            }
            change = setStateLocked(State.startedOkay);
            I2PD_JNI.startAcceptingTunnels();
        }
        notifyListeners(change);
    }

    public void reloadTunnelsConfigs() {
        if (state.isStartedOkay()) {
            I2PD_JNI.reloadTunnelsConfigs();
        }
    }

    public int getTransitTunnelsCount() {
        return I2PD_JNI.getTransitTunnelsCount();
    }

    /**
     * Start a graceful shutdown: stop accepting transit tunnels and give the
     * remaining ones time to drain before stopping the daemon. The timer is
     * owned by this process-wide object, never by an Activity.
     */
    public void beginGracefulShutdown(long delayMillis) {
        StateChange change = null;
        boolean stopImmediately;
        synchronized (lock) {
            if (!state.isStartedOkay()) {
                stopImmediately = true;
            } else if (gracefulStopTimer != null) {
                return;
            } else {
                change = setStateLocked(State.gracefulShutdownInProgress);
                I2PD_JNI.stopAcceptingTunnels();

                long delay = Math.max(0L, delayMillis);
                long deadline = System.currentTimeMillis() + delay;
                gracefulStopDeadlineMillis.set(deadline);

                stopImmediately = getTransitTunnelsCount() <= 0;
                if (!stopImmediately) {
                    final long scheduledDeadline = deadline;
                    final Timer timer = new Timer("i2pdGracefulStop", true);
                    gracefulStopTimer = timer;
                    timer.schedule(new TimerTask() {
                        @Override
                        public void run() {
                            synchronized (lock) {
                                if (gracefulStopTimer != timer
                                        || gracefulStopDeadlineMillis.get() != scheduledDeadline) {
                                    return;
                                }
                            }
                            stopDaemon();
                        }
                    }, delay);
                }
            }
        }
        notifyListeners(change);
        if (stopImmediately) {
            stopDaemon();
        }
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
        return gracefulStopTimer != null;
    }

    public long getGracefulStopRemainingMillis() {
        long deadline = gracefulStopDeadlineMillis.get();
        if (deadline == 0L) {
            return 0L;
        }
        return Math.max(0L, deadline - System.currentTimeMillis());
    }

    private void cancelGracefulStopLocked() {
        Timer timer = gracefulStopTimer;
        gracefulStopTimer = null;
        gracefulStopDeadlineMillis.set(0L);
        if (timer != null) {
            timer.cancel();
        }
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
        try {
            I2PD_JNI.onNetworkStateChanged(connected);
        } catch (Throwable tr) {
            Log.e(TAG, "native network state update failed", tr);
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

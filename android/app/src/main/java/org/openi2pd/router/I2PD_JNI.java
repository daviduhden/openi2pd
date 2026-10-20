/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: I2PD JNI.
 *
 * Non-instantiable declaration of the native methods loaded from
 * {@code libi2pd.so}, with a repeatable fail-closed loader that caches load
 * errors. Only {@link RouterService} calls these control and status entry
 * points; UI code must go through {@link RouterRepository} and
 * {@link RouterViewModel} so JNI never runs on the main thread.
 *
 */

package org.openi2pd.router;

import android.util.Log;

/**
 * Thin, auditable Java surface over the in-tree C++23 JNI library.
 *
 * <p>Only the router service calls the methods below. UI classes must go
 * through {@link RouterRepository}/{@link RouterViewModel} so native calls
 * never run on the main thread and so no component can keep a native handle
 * alive after the service is gone.</p>
 *
 * <p>Library loading is deliberately fail-closed and repeatable: a failed
 * load is remembered and reported as a link error instead of leaving a
 * partially initialised process. Start/stop calls are serialised by the
 * native-call guard; RouterService posts control/status work to its worker,
 * while DaemonWrapper uses a dedicated start thread for asset copying and
 * native initialisation.</p>
 */
public final class I2PD_JNI {
    private static final String TAG = "I2PD_JNI";
    private static final Object LOAD_LOCK = new Object();

    private static boolean loaded;
    private static Throwable loadError;

    private I2PD_JNI() {
        // Native entry points only.
    }

    public static native byte[] buildTorrentMetadata(byte[] name, byte[][] paths, long[] lengths,
            int pieceLength, byte[] hashes, byte[][] trackers, boolean privateTorrent);

    static native boolean installSafStorageBridge(Class<?> bridge);
    static native boolean restrictSafStorageProcess();

    public static native String getABICompiledWith();

    /**
     * Load {@code libi2pd.so}.
     *
     * @throws UnsatisfiedLinkError if the library or one of its dependencies
     *                              cannot be loaded. The original failure is
     *                              retained and reused for later calls.
     */
    public static void loadLibraries() {
        synchronized (LOAD_LOCK) {
            if (loaded) {
                return;
            }
            if (loadError != null) {
                throw linkError(loadError);
            }
            try {
                System.loadLibrary("i2pd");
                loaded = true;
            } catch (UnsatisfiedLinkError | SecurityException e) {
                loadError = e;
                Log.e(TAG, "unable to load libi2pd", e);
                throw linkError(e);
            }
        }
    }

    /**
     * Best-effort variant for non-critical paths (settings/status probes).
     * Never throws and never leaves a pending load failure unreported.
     */
    public static boolean tryLoadLibraries() {
        try {
            loadLibraries();
            return true;
        } catch (LinkageError | RuntimeException e) {
            Log.w(TAG, "native library unavailable", e);
            return false;
        }
    }

    public static boolean isLoaded() {
        synchronized (LOAD_LOCK) {
            return loaded;
        }
    }

    public static Throwable getLoadError() {
        synchronized (LOAD_LOCK) {
            return loadError;
        }
    }

    private static UnsatisfiedLinkError linkError(Throwable cause) {
        UnsatisfiedLinkError error =
                new UnsatisfiedLinkError("libi2pd could not be loaded: " + cause);
        error.initCause(cause);
        return error;
    }

    /**
     * returns error info if failed
     * returns "ok" if daemon initialized and started okay
     */
    public static native String startDaemon();
    public static native void stopDaemon();

    public static native void startAcceptingTunnels();
    public static native void stopAcceptingTunnels();
    public static native void reloadTunnelsConfigs();

    /**
     * Ask the running netDb to reseed from the configured reseed servers.
     *
     * <p>The native call is synchronous and single-flight: it performs the SU3
     * bootstrap on the calling thread, so it must run on the service worker and
     * never on the main thread. No URL, path or option crosses this boundary.</p>
     *
     * @return {@code true} when a reseed request was issued and
     *         {@code NetDb::Reseed()} returned; {@code false} when the router
     *         is not started, a reseed is already running or native code failed
     */
    public static native boolean requestReseed();

    /**
     * Apply one runtime log level to the running logger without a restart.
     *
     * @param level one of {@code none}, {@code critical}, {@code error},
     *              {@code warn}, {@code info} or {@code debug}. Any other value
     *              (including different case) is rejected before native state
     *              is touched.
     * @return {@code true} only when the running logger accepted the level
     */
    public static native boolean setRuntimeLogLevel(String level);

    public static native void setDataDir(String jdataDir);

    public static native int getTransitTunnelsCount();
    public static native String getDataDir();
    public static native String getStatusSummary();

    public static native boolean getHTTPProxyState();
    public static native boolean getSOCKSProxyState();
    public static native boolean getBOBState();
    public static native boolean getSAMState();
    public static native boolean getI2CPState();

    public static native void onNetworkStateChanged(boolean isConnected);

    /**
     * Bounded newline-separated {@code key=value} status snapshot.
     * Native exceptions are converted to an {@code error=} snapshot.
     */
    public static native String getStatusSnapshot();

    /**
     * Recent log records as {@code sequence|timestampSeconds|level|text}.
     *
     * @param afterSequence only records with a larger sequence are returned
     * @param limit         clamped natively to 1..256; a value outside that
     *                      range must not allocate unbounded memory
     */
    public static native String getRecentLogs(long afterSequence, int limit);

    /** Torrent-only bounded control protocol; called exclusively by RouterService. */
    static native String torrentCommand(byte[] utf8Request);

    /** Raw bounded torrent metadata from a user-selected SAF document. */
    static native String importTorrent(int tunnel, byte[] metadata);
}

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
 *
 * <p>The process role is claimed exactly once: the router process claims
 * {@link #PROCESS_ROLE_ROUTER} before any router-capable call and the SAF
 * storage broker claims {@link #PROCESS_ROLE_STORAGE_BROKER} through
 * {@link #restrictSafStorageProcess()}. A conflicting claim never starts a
 * daemon. A stop request is published through {@link #requestStop()}, which
 * takes no lock so it can abort a start that owns the native guard.</p>
 */
public final class I2PD_JNI {
    private static final String TAG = "I2PD_JNI";
    private static final Object LOAD_LOCK = new Object();

    /**
     * Process-role tokens mirrored by the native {@code claimProcessRole}
     * contract. Android runs the router and the opt-in SAF storage broker in
     * separate processes of the same UID, so native code cannot infer the role
     * from credentials; each process claims exactly one role before its first
     * router- or document-capable native call.
     */
    public static final int PROCESS_ROLE_NONE = 0;
    public static final int PROCESS_ROLE_ROUTER = 1;
    public static final int PROCESS_ROLE_STORAGE_BROKER = 2;

    /**
     * Fixed, non-secret token carried by a role conflict. It contains no path,
     * identity, grant or credential and is safe to display and to log.
     */
    static final String PROCESS_ROLE_CONFLICT = "process-role-conflict";

    /**
     * Result token returned when a stop request aborted an in-progress start.
     * {@link DaemonWrapper} maps it to the stopped state.
     */
    public static final String START_CANCELED = "start-canceled";

    private static boolean loaded;
    private static Throwable loadError;

    private I2PD_JNI() {
        // Native entry points only.
    }

    public static native byte[] buildTorrentMetadata(byte[] name, byte[][] paths, long[] lengths,
            int pieceLength, byte[] hashes, byte[][] trackers, boolean privateTorrent);

    static native boolean installSafStorageBridge(Class<?> bridge);

    /**
     * Apply the storage-broker process sandbox and claim
     * {@link #PROCESS_ROLE_STORAGE_BROKER}.
     *
     * <p>The claim happens before the sandbox installation and is
     * irreversible. A process that already claimed the router role gets
     * {@code false} here (and never a sandbox), so the caller stops the broker
     * service instead of running document IO in a router process.</p>
     */
    static native boolean restrictSafStorageProcess();

    /**
     * Claim one process role. Accepted once per process and irreversible.
     *
     * @return {@code true} when this call claimed the role or the process
     *         already serves exactly that role
     */
    private static native boolean claimProcessRole(int role);

    /** Currently claimed process role, or {@link #PROCESS_ROLE_NONE}. */
    public static native int processRole();

    /**
     * Claim {@link #PROCESS_ROLE_ROUTER}. Called by {@link DaemonWrapper}
     * before the first router-capable native entry point.
     *
     * @throws IllegalStateException when this process is already bound to a
     *                               different role; the fixed message token
     *                               carries no private detail and the daemon
     *                               must not be started
     */
    public static void claimRouterProcessRole() {
        if (!claimProcessRole(PROCESS_ROLE_ROUTER)) {
            Log.e(TAG, "refusing router role: " + PROCESS_ROLE_CONFLICT);
            throw new IllegalStateException(PROCESS_ROLE_CONFLICT);
        }
    }

    /**
     * Ask the native lifecycle to abort an in-progress start.
     *
     * <p>This sets one native atomic and returns; it deliberately takes no
     * native-call lock, because the start transition owns that guard until it
     * finishes. A no-op before the library is loaded, and never throws.</p>
     */
    public static void requestStop() {
        synchronized (LOAD_LOCK) {
            if (!loaded) {
                return;
            }
        }
        try {
            requestStopNative();
        } catch (RuntimeException | LinkageError e) {
            Log.w(TAG, "native start-cancel request unavailable", e);
        }
    }

    private static native void requestStopNative();

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
     * Ask the running netDb to reseed without blocking the caller.
     *
     * <p>The native call only starts the owned, cancellable NetDb job and
     * reports the typed outcome, so the service worker (and the UI) is never
     * held for the length of a bootstrap. A second request while a job runs is
     * reported instead of starting another. No URL, path or option crosses
     * this boundary.</p>
     *
     * @return one of {@code started}, {@code already-running},
     *         {@code disabled}, {@code accepted}, {@code busy},
     *         {@code unavailable} or {@code failed}
     */
    public static native String requestReseed();

    /**
     * Bounded, non-secret reseed status:
     * {@code phase|enabled|attempts|succeeded|lastAttemptMs|lastSuccessMs|lastError}.
     * An empty string means the router is not running.
     */
    public static native String getReseedStatus();

    /**
     * Configured client/server tunnels, one {@code name|kind|running|endpoint|destination}
     * record per line. Only public labels cross the boundary.
     */
    public static native String getTunnelServices();

    /** Start one configured tunnel by its unique, bounded service name. */
    public static native String startTunnel(String name);

    /** Stop one configured tunnel by its unique, bounded service name. */
    public static native String stopTunnel(String name);

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

    /**
     * Bounded address-book rows as {@code name|b32-or-b33|blinded\n}.
     * The call is made only from the service worker while the daemon runs.
     */
    static native String getAddressBook();

    /**
     * Add or replace one validated address-book name with a typed result.
     *
     * @param replace {@code false} never re-points an existing name and reports
     *                {@code exists}; {@code true} is the explicit overwrite
     * @return {@code ok}, {@code exists}, {@code invalid}, {@code unavailable}
     *         or {@code failed}
     */
    static native String addAddressWithResult(String name, String destination, boolean replace);

    /** Remove one name without deleting destination key material. */
    static native boolean removeAddress(String name);

    /** Block a deleted-host subscription pattern; returns false on invalid input. */
    static native boolean blockAddress(String name);

    /** Remove one exact deleted-host pattern. */
    static native boolean unblockAddress(String name);

    /** Bounded deleted-host patterns, one per line. */
    static native String getBlockedAddresses();

    /** Address-book subscription sources, one bounded URL per line. */
    static native String getSubscriptions();

    /** Add one validated subscription source; returns the typed outcome token. */
    static native String addSubscription(String url);

    /** Remove one subscription source by its exact URL. */
    static native String removeSubscription(String url);

    /** Ban one base64 router identity until the router restarts. */
    static native String banRouter(String hash);

    /** Remove one in-memory router ban. */
    static native String unbanRouter(String hash);

    /** Torrent-only bounded control protocol; called exclusively by RouterService. */
    static native String torrentCommand(byte[] utf8Request);

    /** Raw bounded torrent metadata from a user-selected SAF document. */
    static native String importTorrent(int tunnel, byte[] metadata);
}

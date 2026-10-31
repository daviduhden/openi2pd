/*
 * See the LICENSE file at the top of the project tree for copyright and
 * license details.
 *
 * openi2pd: I2PD JNI.
 *
 * Non-instantiable declaration of the native methods loaded from {@code
 * libi2pd.so}, with a repeatable fail-closed loader that caches load
 * errors. Router control and status belong to the service and its
 * wrapper; the SAF creator and broker also use dedicated native entry
 * points. UI code goes through {@link RouterRepository} and {@link
 * RouterViewModel}.
 *
 */

package org.openi2pd.router;

import android.util.Log;

/**
 * Thin, auditable Java surface over the in-tree C++23 JNI library.
 *
 * <p>{@link RouterService} and {@link DaemonWrapper} own router control
 * and status calls. {@link TorrentSafCreator} invokes metadata
 * serialization on the service's SAF worker, while {@link
 * TorrentSafStorageService} claims the broker process role. UI classes
 * use {@link RouterRepository} and {@link RouterViewModel}, rather than
 * making native calls directly.</p>
 *
 * <p>Library loading is deliberately fail-closed and repeatable: a
 * failed load is remembered and reported as a link error instead of
 * leaving a partially initialised process. Start/stop calls are
 * serialised by the native-call guard; RouterService posts
 * control/status work to its worker, while DaemonWrapper uses a
 * dedicated start thread for asset copying and native
 * initialisation.</p>
 *
 * <p>The process role is claimed exactly once: the router process
 * claims {@link #PROCESS_ROLE_ROUTER} before any router-capable call
 * and the SAF storage broker claims {@link
 * #PROCESS_ROLE_STORAGE_BROKER} through {@link
 * #restrictSafStorageProcess()}. A conflicting claim never starts a
 * daemon. A stop request is published through {@link #requestStop()},
 * which takes no lock so it can abort a start that owns the native
 * guard.</p>
 */
public final class I2PD_JNI {
    private static final String TAG = "I2PD_JNI";
    private static final Object LOAD_LOCK = new Object();

    /**
     * Process-role tokens mirrored by the native {@code
     * claimProcessRole} contract. Android runs the router and the
     * opt-in SAF storage broker in separate processes of the same UID,
     * so native code cannot infer the role from credentials; each
     * process claims exactly one role before its first router- or
     * document-capable native call.
     */
    public static final int PROCESS_ROLE_NONE = 0;
    public static final int PROCESS_ROLE_ROUTER = 1;
    public static final int PROCESS_ROLE_STORAGE_BROKER = 2;

    /**
     * Fixed, non-secret token carried by a role conflict. It contains
     * no path, identity, grant or credential and is safe to display and
     * to log.
     */
    static final String PROCESS_ROLE_CONFLICT = "process-role-conflict";

    /**
     * Result token returned when a stop request aborted an in-progress
     * start. {@link DaemonWrapper} maps it to the stopped state.
     */
    public static final String START_CANCELED = "start-canceled";

    private static boolean loaded;
    private static Throwable loadError;

    private I2PD_JNI() {
        // Native entry points only.
    }

    /**
     * Serialize a v1 torrent from already hashed SAF sources. The
     * native encoder validates all input again and does not read the
     * source documents. Call from a worker because validation and
     * encoding scale with file and piece counts.
     *
     * @param name UTF-8 torrent name
     * @param paths UTF-8 relative paths, in the same order as {@code
     *              lengths}
     * @param lengths byte lengths of the source files
     * @param pieceLength power-of-two piece size in the supported range
     * @param hashes concatenated 20-byte SHA-1 piece digests
     * @param trackers UTF-8 tracker URLs, subject to native I2P-only
     *                 validation
     * @param privateTorrent whether the metadata marks a private swarm
     * @return encoded metainfo, or {@code null} if validation or
     *                 encoding fails
     */
    public static native byte[] buildTorrentMetadata(byte[] name, byte[][] paths, long[] lengths,
            int pieceLength, byte[] hashes, byte[][] trackers, boolean privateTorrent);

    /**
     * Install the process-owned Java SAF bridge after claiming the
     * router role. Existing payloads retain their bridge while a
     * replacement is installed.
     *
     * @return whether the native payload factory was installed
     *                 successfully
     */
    static native boolean installSafStorageBridge(Class<?> bridge);

    /**
     * Apply the storage-broker process sandbox and claim {@link
     * #PROCESS_ROLE_STORAGE_BROKER}.
     *
     * <p>The claim happens before the sandbox installation and is
     * irreversible. A process that already claimed the router role gets
     * {@code false} here (and never a sandbox), so the caller stops the
     * broker service instead of running document IO in a router
     * process.</p>
     */
    static native boolean restrictSafStorageProcess();

    /**
     * Claim one process role. Accepted once per process and
     * irreversible.
     *
     * @return {@code true} when this call claimed the role or the
     *                process already serves exactly that role
     */
    private static native boolean claimProcessRole(int role);

    /**
     * Currently claimed process role, or {@link #PROCESS_ROLE_NONE}.
     */
    public static native int processRole();

    /**
     * Claim {@link #PROCESS_ROLE_ROUTER}. Called by {@link
     * DaemonWrapper} before the first router-capable native entry
     * point.
     *
     * @throws IllegalStateException when this process is already bound
     *                               to a different role; the fixed
     *                               message token carries no private
     *                               detail and the daemon must not be
     *                               started
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
     * <p>This sets one native atomic and returns; it deliberately takes
     * no native-call lock, because the start transition owns that guard
     * until it finishes. A no-op before the library is loaded, and
     * never throws.</p>
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
     * @throws UnsatisfiedLinkError if the library or one of its
     *                              dependencies cannot be loaded. The
     *                              original failure is retained and
     *                              reused for later calls.
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
     * Best-effort variant for non-critical paths (settings/status
     * probes). Never throws and never leaves a pending load failure
     * unreported.
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
     * Start the native router and block until its initialisation
     * transition has finished.
     *
     * <p>The call takes the native-call lock for the whole transition,
     * so it runs on a worker: {@link DaemonWrapper} uses a dedicated
     * start thread and publishes its own state around the call. Only a
     * process that already claimed {@link #PROCESS_ROLE_ROUTER} may
     * start the router; any other process receives {@link
     * #PROCESS_ROLE_CONFLICT}.</p>
     *
     * @return {@code ok} after startup, or an error/status token on
     *                failure (including {@link #START_CANCELED} when a
     *                concurrent {@link #requestStop()} won the race)
     */
    public static native String startDaemon();

    /**
     * Stop and tear down the running router synchronously.
     *
     * <p>Listeners, client and transit tunnels, netDb and the torrent
     * workers are joined before the call returns, and the remaining SAF
     * payload capabilities are released on this attached JNI thread
     * rather than from a payload destructor. The native-call lock
     * serialises it against start and status, so it must not run on the
     * main thread, and it does not cancel a start that is still
     * initialising — that is what {@link #requestStop()} publishes.</p>
     */
    public static native void stopDaemon();

    public static native void startAcceptingTunnels();
    public static native void stopAcceptingTunnels();
    public static native void reloadTunnelsConfigs();

    /**
     * Ask the running netDb to reseed without blocking the caller.
     *
     * <p>The native call only starts the owned, cancellable NetDb job
     * and reports the typed outcome, so the service worker (and the UI)
     * is never held for the length of a bootstrap. A second request
     * while a job runs is reported instead of starting another. No URL,
     * path or option crosses this boundary.</p>
     *
     * @return one of {@code started}, {@code already-running}, {@code
     *             disabled}, {@code accepted}, {@code busy}, {@code
     *             unavailable} or {@code failed}
     */
    public static native String requestReseed();

    /** Apply transit acceptance through the typed admin queue. */
    static native String setTransit(boolean enabled);

    /** Apply one exact bandwidth class: K, L, M, N, O, P or X. */
    static native String setBandwidth(String bandwidthClass);

    /** Apply a bounded transit share percentage, 0..100. */
    static native String setShareRatio(int percent);

    /**
     * Bounded, non-secret reseed status with ordered phase, enabled,
     * attempts, succeeded, lastAttemptMs, lastSuccessMs and lastError.
     * An empty string means the router is not running.
     */
    public static native String getReseedStatus();

    /**
     * Configured client/server tunnels, one {@code
     * name|kind|running|endpoint|destination} record per line. Only
     * public labels cross the boundary.
     */
    public static native String getTunnelServices();

    /**
     * Start one configured tunnel by its unique, bounded service name.
     */
    public static native String startTunnel(String name);

    /**
     * Stop one configured tunnel by its unique, bounded service name.
     */
    public static native String stopTunnel(String name);

    /**
     * Apply one runtime log level to the running logger without a
     * restart.
     *
     * @param level one of {@code none}, {@code critical}, {@code
     *              error}, {@code warn}, {@code info} or {@code debug}.
     *              Any other value (including different case) is
     *              rejected before native state is touched.
     * @return {@code true} only when the running logger accepted the
     *                level
     */
    public static native boolean setRuntimeLogLevel(String level);

    /**
     * Publish the app-private data directory the router will use.
     *
     * <p>State only: it copies no assets and does not start the daemon,
     * and it deliberately does not take the native-call lock so it
     * cannot wait behind a multi-second start. {@link DaemonWrapper}
     * sets it immediately before {@link #startDaemon()}.</p>
     */
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
     * Recent log records as {@code
     * sequence|timestampSeconds|level|text}.
     *
     * @param afterSequence only records with a larger sequence are
     *                      returned
     * @param limit clamped natively to 1..256; a value outside that
     *              range must not allocate unbounded memory
     */
    public static native String getRecentLogs(long afterSequence, int limit);

    /**
     * Bounded address-book rows as {@code name|b32-or-b33|blinded\n}.
     * The call is made only from the service worker while the daemon
     * runs.
     */
    static native String getAddressBook();

    /**
     * Add or replace one validated address-book name with a typed
     * result.
     *
     * @param replace {@code false} never re-points an existing name and
     *                reports {@code exists}; {@code true} is the
     *                explicit overwrite
     * @return {@code ok}, {@code exists}, {@code invalid}, {@code
     *                unavailable} or {@code failed}
     */
    static native String addAddressWithResult(String name, String destination, boolean replace);

    /** Remove one name without deleting destination key material. */
    static native boolean removeAddress(String name);

    /**
     * Block a deleted-host subscription pattern; returns false on
     * invalid input.
     */
    static native boolean blockAddress(String name);

    /** Remove one exact deleted-host pattern. */
    static native boolean unblockAddress(String name);

    /** Bounded deleted-host patterns, one per line. */
    static native String getBlockedAddresses();

    /** Address-book subscription sources, one bounded URL per line. */
    static native String getSubscriptions();

    /**
     * Add one validated subscription source; returns the typed outcome
     * token.
     */
    static native String addSubscription(String url);

    /** Remove one subscription source by its exact URL. */
    static native String removeSubscription(String url);

    /** Ban one base64 router identity until the router restarts. */
    static native String banRouter(String hash);

    /** Remove one in-memory router ban. */
    static native String unbanRouter(String hash);

    /**
     * Torrent-only bounded control protocol; called exclusively by
     * RouterService.
     */
    static native String torrentCommand(byte[] utf8Request);

    /**
     * Raw bounded torrent metadata from a user-selected SAF document.
     */
    static native String importTorrent(int tunnel, byte[] metadata);
}
